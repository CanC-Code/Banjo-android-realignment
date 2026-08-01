#include "gfx_interpreter.h"
#include <android/log.h>
#include <cstring>
#include <cstdlib>

#define LOG_TAG "BKA_GFX"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// =======================================================================
// Gfx Command Structure
// =======================================================================
typedef struct {
    uint32_t w0;
    uint32_t w1;
} GfxCommand;

#define GFX_OPCODE(cmd)   (((cmd).w0 >> 24) & 0xFF)

// =======================================================================
// N64 TMEM (Texture Memory)
// =======================================================================
#define TMEM_SIZE 4096

// =======================================================================
// Framebuffer
// =======================================================================
#define FB_WIDTH  292
#define FB_HEIGHT 216

// These symbols are defined in C files (lowlevel_bridge.cpp and vimgr.c)
// and need extern "C" linkage when accessed from C++.
extern "C" {
    uint16_t gFramebuffers[2][FB_WIDTH * FB_HEIGHT];
    int getActiveFramebuffer(void);
    uint8_t* gN64_RDRAM;
}

// =======================================================================
// RDP State
// =======================================================================
struct RDPState {
    uint32_t otherModeL;
    uint32_t otherModeH;
    uint32_t combineMode;
    
    uint8_t primR, primG, primB, primA;
    uint8_t envR, envG, envB, envA;
    uint8_t fillR, fillG, fillB, fillA;
    uint8_t blendR, blendG, blendB, blendA;
    uint8_t fogR, fogG, fogB, fogA;
    
    bool textureEnabled;
    
    struct {
        uint32_t format;
        uint32_t size;
        uint32_t line;
        uint32_t tmemAddr;
        uint32_t palette;
        uint32_t clampT, mirrorT, maskT, shiftT;
        uint32_t clampS, mirrorS, maskS, shiftS;
        uint32_t sl, tl, sh, th;
    } tiles[8];
    
    uint8_t* texAddr;
    uint32_t texWidth;
    uint32_t texFmt;
    uint32_t texSize;
    
    int activeTile;
    uint8_t tmem[TMEM_SIZE];
};

static RDPState s_rdp;

// =======================================================================
// Helpers
// =======================================================================

static inline uint32_t RDP_BPP(uint32_t size) {
    static const uint32_t bpp[] = {0, 1, 2, 2};
    return bpp[size & 3];
}

static inline void RDP_FetchTexel(int tile, uint32_t s, uint32_t t, uint8_t* outRGBA) {
    uint32_t u = s >> 5;
    uint32_t v = t >> 5;
    
    auto& tdesc = s_rdp.tiles[tile];
    uint32_t bpp = RDP_BPP(tdesc.size);
    
    uint32_t texW = (tdesc.sh >> 2) + 1;
    uint32_t texH = (tdesc.th >> 2) + 1;
    
    if (tdesc.clampS) {
        if (u >= texW) u = texW - 1;
    } else {
        u = u & (texW - 1);
    }
    if (tdesc.clampT) {
        if (v >= texH) v = texH - 1;
    } else {
        v = v & (texH - 1);
    }
    
    uint32_t tmemBase = tdesc.tmemAddr * 8;
    uint32_t lineBytes = tdesc.line * 8;
    
    if (bpp == 2) {
        uint32_t offset = tmemBase + v * lineBytes + u * 2;
        if (offset + 1 < TMEM_SIZE) {
            uint16_t pixel = (s_rdp.tmem[offset] << 8) | s_rdp.tmem[offset + 1];
            if (tdesc.format == 0) {
                outRGBA[0] = ((pixel >> 11) & 0x1F) << 3;
                outRGBA[1] = ((pixel >> 6) & 0x1F) << 3;
                outRGBA[2] = ((pixel >> 1) & 0x1F) << 3;
                outRGBA[3] = (pixel & 1) ? 255 : 0;
            } else if (tdesc.format == 5) {
                outRGBA[0] = outRGBA[1] = outRGBA[2] = (pixel >> 8) & 0xFF;
                outRGBA[3] = pixel & 0xFF;
            } else {
                outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 255;
            }
        } else {
            outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 0;
        }
    } else if (bpp == 1) {
        uint32_t offset = tmemBase + v * lineBytes + u;
        if (offset < TMEM_SIZE) {
            uint8_t pixel = s_rdp.tmem[offset];
            if (tdesc.format == 4) {
                outRGBA[0] = outRGBA[1] = outRGBA[2] = (pixel & 0xF0);
                outRGBA[3] = (pixel & 0x0F) << 4;
            } else if (tdesc.format == 3) {
                outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 255;
            } else {
                outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = pixel;
            }
        } else {
            outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 0;
        }
    } else {
        outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 255;
    }
}

static void RDP_InitState() {
    memset(&s_rdp, 0, sizeof(s_rdp));
    s_rdp.primR = s_rdp.primG = s_rdp.primB = s_rdp.primA = 255;
    s_rdp.envR = s_rdp.envG = s_rdp.envB = s_rdp.envA = 255;
    s_rdp.fillR = s_rdp.fillG = s_rdp.fillB = 255; s_rdp.fillA = 255;
    s_rdp.blendR = s_rdp.blendG = s_rdp.blendB = 0; s_rdp.blendA = 0;
    s_rdp.fogR = s_rdp.fogG = s_rdp.fogB = 0; s_rdp.fogA = 0;
    s_rdp.activeTile = 0;
    s_rdp.textureEnabled = false;
}

static inline uint16_t RGBA8_TO_RGB565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// =======================================================================
// Command Handlers
// =======================================================================

static void Cmd_SetPrimColor(GfxCommand cmd) {
    s_rdp.primR = (cmd.w1 >> 24) & 0xFF;
    s_rdp.primG = (cmd.w1 >> 16) & 0xFF;
    s_rdp.primB = (cmd.w1 >> 8) & 0xFF;
    s_rdp.primA = cmd.w1 & 0xFF;
}

static void Cmd_SetEnvColor(GfxCommand cmd) {
    s_rdp.envR = (cmd.w1 >> 24) & 0xFF;
    s_rdp.envG = (cmd.w1 >> 16) & 0xFF;
    s_rdp.envB = (cmd.w1 >> 8) & 0xFF;
    s_rdp.envA = cmd.w1 & 0xFF;
}

static void Cmd_SetOtherModeL(GfxCommand cmd) {
    uint32_t length = (cmd.w0 >> 8) & 0xFF;
    uint32_t shift  = cmd.w0 & 0xFF;
    uint32_t data   = cmd.w1;
    uint32_t mask   = ((1 << (length + 1)) - 1) << shift;
    s_rdp.otherModeL = (s_rdp.otherModeL & ~mask) | ((data << shift) & mask);
}

static void Cmd_SetOtherModeH(GfxCommand cmd) {
    uint32_t length = (cmd.w0 >> 8) & 0xFF;
    uint32_t shift  = cmd.w0 & 0xFF;
    uint32_t data   = cmd.w1;
    uint32_t mask   = ((1 << (length + 1)) - 1) << shift;
    s_rdp.otherModeH = (s_rdp.otherModeH & ~mask) | ((data << shift) & mask);
}

static void Cmd_SetCombine(GfxCommand cmd) {
    s_rdp.combineMode = (cmd.w0 & 0x00FFFFFF) | (cmd.w1 & 0xFF000000);
}

static void Cmd_Texture(GfxCommand cmd) {
    uint32_t enable = (cmd.w0 >> 16) & 0xFF;
    uint32_t tile   = (cmd.w0 >> 8) & 0xFF;
    s_rdp.textureEnabled = (enable != 0);
    if (tile < 8) s_rdp.activeTile = tile;
}

static void Cmd_SetTile(GfxCommand cmd) {
    uint32_t tile = (cmd.w1 >> 24) & 0x7;
    if (tile >= 8) return;
    
    auto& t = s_rdp.tiles[tile];
    t.format  = (cmd.w0 >> 21) & 0x7;
    t.size    = (cmd.w0 >> 19) & 0x3;
    t.line    = (cmd.w0 >> 9) & 0x1FF;
    t.tmemAddr = cmd.w0 & 0x1FF;
    t.palette = (cmd.w0 >> 20) & 0xF;
    t.clampT  = (cmd.w0 >> 18) & 0x1;
    t.mirrorT = (cmd.w0 >> 17) & 0x1;
    t.maskT   = (cmd.w0 >> 13) & 0xF;
    t.shiftT  = (cmd.w0 >> 9) & 0xF;
    t.clampS  = (cmd.w1 >> 31) & 0x1;
    t.mirrorS = (cmd.w1 >> 30) & 0x1;
    t.maskS   = (cmd.w1 >> 26) & 0xF;
    t.shiftS  = (cmd.w1 >> 22) & 0xF;
}

static void Cmd_SetTileSize(GfxCommand cmd) {
    uint32_t tile = (cmd.w1 >> 24) & 0x7;
    if (tile >= 8) return;
    
    auto& t = s_rdp.tiles[tile];
    t.sl = (cmd.w0 >> 12) & 0xFFF;
    t.tl = cmd.w0 & 0xFFF;
    t.sh = (cmd.w1 >> 12) & 0xFFF;
    t.th = cmd.w1 & 0xFFF;
}

static void Cmd_SetTImg(GfxCommand cmd) {
    s_rdp.texFmt  = (cmd.w0 >> 21) & 0x7;
    s_rdp.texSize = (cmd.w0 >> 19) & 0x3;
    s_rdp.texWidth = (cmd.w0 >> 9) & 0x3FF;
    s_rdp.texAddr = gN64_RDRAM + (cmd.w1 & 0x0FFFFFFF);
}

static void Cmd_LoadBlock(GfxCommand cmd) {
    uint32_t tile = (cmd.w0 >> 24) & 0x7;
    if (tile >= 8) return;
    
    auto& t = s_rdp.tiles[tile];
    uint32_t sl = (cmd.w0 >> 12) & 0xFFF;
    uint32_t tl = cmd.w0 & 0xFFF;
    uint32_t sh = (cmd.w1 >> 12) & 0xFFF;
    uint32_t th = cmd.w1 & 0xFFF;
    
    t.sl = sl;
    t.tl = tl;
    t.sh = sh;
    t.th = th;
    
    uint32_t bpp = RDP_BPP(t.size);
    if (bpp == 0) bpp = 1;
    
    uint32_t texWidth = (sh >> 2) + 1;
    uint32_t texHeight = (th >> 2) + 1;
    uint32_t texSize = texWidth * texHeight * bpp;
    uint32_t lineWords = (texWidth * bpp + 7) / 8;
    
    if (s_rdp.texAddr && texSize <= TMEM_SIZE) {
        uint32_t tmemBase = t.tmemAddr * 8;
        for (uint32_t row = 0; row < texHeight; row++) {
            uint32_t srcOffset = row * lineWords * 8;
            uint32_t dstOffset = tmemBase + row * lineWords * 8;
            if (dstOffset + lineWords * 8 <= TMEM_SIZE) {
                memcpy(s_rdp.tmem + dstOffset, s_rdp.texAddr + srcOffset, lineWords * 8);
            }
        }
        t.line = lineWords;
    }
}

static void Cmd_LoadTile(GfxCommand cmd) {
    uint32_t tile = (cmd.w0 >> 24) & 0x7;
    if (tile >= 8) return;
    
    auto& t = s_rdp.tiles[tile];
    uint32_t sl = (cmd.w0 >> 12) & 0xFFF;
    uint32_t tl = cmd.w0 & 0xFFF;
    uint32_t sh = (cmd.w1 >> 12) & 0xFFF;
    uint32_t th = cmd.w1 & 0xFFF;
    
    t.sl = sl;
    t.tl = tl;
    t.sh = sh;
    t.th = th;
    
    uint32_t bpp = RDP_BPP(t.size);
    if (bpp == 0) bpp = 1;
    
    uint32_t texWidth = (sh >> 2) + 1;
    uint32_t texHeight = (th >> 2) + 1;
    uint32_t texSize = texWidth * texHeight * bpp;
    uint32_t lineWords = (texWidth * bpp + 7) / 8;
    
    if (s_rdp.texAddr && texSize <= TMEM_SIZE) {
        uint32_t tmemBase = t.tmemAddr * 8;
        uint32_t srcLineStride = s_rdp.texWidth * bpp;
        for (uint32_t row = 0; row < texHeight; row++) {
            uint32_t srcOffset = (tl + row) * srcLineStride + sl * bpp;
            uint32_t dstOffset = tmemBase + row * lineWords * 8;
            if (dstOffset + lineWords * 8 <= TMEM_SIZE) {
                memcpy(s_rdp.tmem + dstOffset, s_rdp.texAddr + srcOffset, lineWords * 8);
            }
        }
        t.line = lineWords;
    }
}

static void Cmd_FillRect(GfxCommand cmd) {
    int32_t xl = (int32_t)((cmd.w0 >> 12) & 0xFFF);
    int32_t yl = (int32_t)(cmd.w0 & 0xFFF);
    int32_t xh = (int32_t)((cmd.w1 >> 12) & 0xFFF);
    int32_t yh = (int32_t)(cmd.w1 & 0xFFF);
    
    xl >>= 2; yl >>= 2;
    xh >>= 2; yh >>= 2;
    
    if (xl < 0) xl = 0;
    if (yl < 0) yl = 0;
    if (xh > FB_WIDTH) xh = FB_WIDTH;
    if (yh > FB_HEIGHT) yh = FB_HEIGHT;
    if (xl >= xh || yl >= yh) return;
    
    uint16_t color = RGBA8_TO_RGB565(s_rdp.fillR, s_rdp.fillG, s_rdp.fillB);
    int activeFb = getActiveFramebuffer();
    uint16_t* fb = gFramebuffers[activeFb];
    
    for (int32_t y = yl; y < yh; y++) {
        for (int32_t x = xl; x < xh; x++) {
            fb[y * FB_WIDTH + x] = color;
        }
    }
}

static void Cmd_TexRect(GfxCommand cmd) {
    int32_t xh = (int32_t)((cmd.w0 >> 12) & 0xFFF);
    int32_t yh = (int32_t)(cmd.w0 & 0xFFF);
    int32_t xl = (int32_t)((cmd.w1 >> 12) & 0xFFF);
    int32_t yl = (int32_t)(cmd.w1 & 0xFFF);
    
    xl >>= 2; yl >>= 2;
    xh >>= 2; yh >>= 2;
    
    if (xl < 0) xl = 0;
    if (yl < 0) yl = 0;
    if (xh > FB_WIDTH) xh = FB_WIDTH;
    if (yh > FB_HEIGHT) yh = FB_HEIGHT;
    if (xl >= xh || yl >= yh) return;
    
    int tile = s_rdp.activeTile;
    if (tile < 0 || tile >= 8) return;
    auto& tdesc = s_rdp.tiles[tile];
    
    int32_t texW = (tdesc.sh >> 2) + 1;
    int32_t texH = (tdesc.th >> 2) + 1;
    if (texW <= 0) texW = 1;
    if (texH <= 0) texH = 1;
    
    int32_t rectW = xh - xl;
    int32_t rectH = yh - yl;
    if (rectW <= 0 || rectH <= 0) return;
    
    int32_t sBase = (tdesc.sl >> 5);
    int32_t tBase = (tdesc.tl >> 5);
    
    int activeFb = getActiveFramebuffer();
    uint16_t* fb = gFramebuffers[activeFb];
    uint8_t texel[4];
    
    for (int32_t dy = 0; dy < rectH; dy++) {
        for (int32_t dx = 0; dx < rectW; dx++) {
            int32_t s = sBase + (dx * texW) / rectW;
            int32_t t = tBase + (dy * texH) / rectH;
            
            RDP_FetchTexel(tile, s << 5, t << 5, texel);
            
            uint8_t r = (uint8_t)(((uint16_t)texel[0] * s_rdp.primR) / 255);
            uint8_t g = (uint8_t)(((uint16_t)texel[1] * s_rdp.primG) / 255);
            uint8_t b = (uint8_t)(((uint16_t)texel[2] * s_rdp.primB) / 255);
            uint8_t a = (uint8_t)(((uint16_t)texel[3] * s_rdp.primA) / 255);
            
            if (a < 8) continue;
            
            int32_t px = xl + dx;
            int32_t py = yl + dy;
            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT) {
                fb[py * FB_WIDTH + px] = RGBA8_TO_RGB565(r, g, b);
            }
        }
    }
}

static void Cmd_DL(GfxCommand cmd, GfxCommand** outCmd, size_t* outRemaining) {
    uint32_t addr = cmd.w1;
    uint8_t* ptr = gN64_RDRAM + (addr & 0x0FFFFFFF);
    *outCmd = (GfxCommand*)ptr;
    *outRemaining = 0xFFFFFFFF;
}

// =======================================================================
// Main Dispatch
// =======================================================================

void RSP_ProcessGfxTask(OSTask* tp) {
    if (!tp || !tp->t.data_ptr || tp->t.data_size == 0) return;
    if (tp->t.type != 0) return;  // M_GFXTASK type check
    
    RDP_InitState();
    
    GfxCommand* cmdBase = (GfxCommand*)tp->t.data_ptr;
    GfxCommand* cmd = cmdBase;
    size_t cmdCount = tp->t.data_size / sizeof(GfxCommand);
    size_t remaining = cmdCount;
    
    static int frameCount = 0;
    frameCount++;
    bool logFrame = (frameCount <= 10);
    
    while (remaining > 0) {
        GfxCommand c = *cmd;
        uint8_t opcode = GFX_OPCODE(c);
        
        if (logFrame) {
            LOGI("BKA_GFX: cmd[%zu] op=0x%02X w0=0x%08X w1=0x%08X",
                 cmdCount - remaining, opcode, c.w0, c.w1);
        }
        
        switch (opcode) {
            case 0xC0: // G_NOOP
            case 0xE8: // G_RDPTILESYNC
            case 0xE7: // G_RDPPIPESYNC
            case 0xE6: // G_RDPLOADSYNC
                break;
                
            case 0xFA: // G_SETPRIMCOLOR
                Cmd_SetPrimColor(c);
                break;
                
            case 0xFB: // G_SETENVCOLOR
                Cmd_SetEnvColor(c);
                break;
                
            case 0xE2: // G_SETOTHERMODE_L
                Cmd_SetOtherModeL(c);
                break;
                
            case 0xE3: // G_SETOTHERMODE_H
                Cmd_SetOtherModeH(c);
                break;
                
            case 0xFC: // G_SETCOMBINE
                Cmd_SetCombine(c);
                break;
                
            case 0xD7: // G_TEXTURE
                Cmd_Texture(c);
                break;
                
            case 0xF5: // G_SETTILE
                Cmd_SetTile(c);
                break;
                
            case 0xF2: // G_SETTILESIZE
                Cmd_SetTileSize(c);
                break;
                
            case 0xFD: // G_SETTIMG
                Cmd_SetTImg(c);
                break;
                
            case 0xF3: // G_LOADBLOCK
                Cmd_LoadBlock(c);
                break;
                
            case 0xF4: // G_LOADTILE
                Cmd_LoadTile(c);
                break;
                
            case 0xF6: // G_FILLRECT
                Cmd_FillRect(c);
                break;
                
            case 0xE4: // G_TEXRECT
            case 0xE5: // G_TEXRECTFLIP
                Cmd_TexRect(c);
                break;
                
            case 0xDE: // G_DL
                Cmd_DL(c, &cmd, &remaining);
                continue;
                
            case 0xDF: // G_ENDDL
                if (logFrame) {
                    LOGI("BKA_GFX: ENDDL — processed %zu commands", cmdCount - remaining);
                }
                return;
                
            case 0xE1: // G_RDPHALF_1
            case 0xF1: // G_RDPHALF_2
                break;
                
            default:
                if (logFrame) {
                    LOGW("BKA_GFX: Unhandled opcode 0x%02X at cmd %zu", opcode, cmdCount - remaining);
                }
                break;
        }
        
        cmd++;
        remaining--;
    }
}