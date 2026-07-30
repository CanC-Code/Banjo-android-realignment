#include <sys/mman.h>
#include <errno.h>
#include <android/log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdint.h>
#include <GLES2/gl2.h>

#define LOG_TAG "BKA_MEM"

#define BKA_RDRAM_ALLOC_SIZE  0x1001000
#define N64_REG_SPACE_SIZE    0x1000000
#define N64_PIF_SPACE_SIZE    0x0010000
#define N64_ROM_SPACE_SIZE    0x04000000

#define MI_INTR_REG_IDX       (0x00300008 / 4)
#define MI_INTR_VI            0x08

#define N64_HEAP_OFFSET       0x002D500
#define N64_HEAP_SIZE         0x211120

uint8_t* gN64_RDRAM    = nullptr;
uint32_t* gN64_Reg_Base = nullptr;
uint32_t* gN64_PIF_Base = nullptr;
uint8_t* gN64_ROM_Base = nullptr;

extern "C" {

    void HLE_TriggerN64Event(int event_id);

    extern void* gFramebuffers[3];
    extern s32 gFramebufferWidth;
    extern s32 gFramebufferHeight;
    extern uint32_t g_active_fb_offset;

    void InitN64Registers(const char* assetDir) {
        if (gN64_RDRAM != nullptr && gN64_Reg_Base != nullptr &&
            gN64_PIF_Base != nullptr && gN64_ROM_Base != nullptr) {
            return;
        }

        gN64_RDRAM = (uint8_t*)mmap(nullptr, BKA_RDRAM_ALLOC_SIZE,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        gN64_Reg_Base = (uint32_t*)mmap(nullptr, N64_REG_SPACE_SIZE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        gN64_PIF_Base = (uint32_t*)mmap(nullptr, N64_PIF_SPACE_SIZE,
                                        PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        gN64_ROM_Base = (uint8_t*)mmap(nullptr, N64_ROM_SPACE_SIZE,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (gN64_RDRAM == MAP_FAILED || gN64_Reg_Base == MAP_FAILED ||
            gN64_PIF_Base == MAP_FAILED || gN64_ROM_Base == MAP_FAILED) {
            __android_log_print(ANDROID_LOG_FATAL, LOG_TAG,
                "Critical virtual memory mapping failure: %s", strerror(errno));
            if (gN64_RDRAM    != MAP_FAILED && gN64_RDRAM    != nullptr) munmap(gN64_RDRAM,    BKA_RDRAM_ALLOC_SIZE);
            if (gN64_Reg_Base != MAP_FAILED && gN64_Reg_Base != nullptr) munmap(gN64_Reg_Base, N64_REG_SPACE_SIZE);
            if (gN64_PIF_Base != MAP_FAILED && gN64_PIF_Base != nullptr) munmap(gN64_PIF_Base, N64_PIF_SPACE_SIZE);
            if (gN64_ROM_Base != MAP_FAILED && gN64_ROM_Base != nullptr) munmap(gN64_ROM_Base, N64_ROM_SPACE_SIZE);
            gN64_RDRAM    = nullptr;
            gN64_Reg_Base = nullptr;
            gN64_PIF_Base = nullptr;
            gN64_ROM_Base = nullptr;
            abort();
        }

        memset(gN64_RDRAM,    0, BKA_RDRAM_ALLOC_SIZE);
        memset(gN64_Reg_Base, 0, N64_REG_SPACE_SIZE);
        memset(gN64_PIF_Base, 0, N64_PIF_SPACE_SIZE);
        memset(gN64_ROM_Base, 0, N64_ROM_SPACE_SIZE);

        if (N64_HEAP_OFFSET + N64_HEAP_SIZE <= BKA_RDRAM_ALLOC_SIZE) {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                "Heap region validated: RDRAM+0x%X (0x%X bytes) for D_8002D500",
                N64_HEAP_OFFSET, N64_HEAP_SIZE);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                "FATAL: Heap region exceeds RDRAM allocation!");
        }
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
            "RDRAM allocated: %zu bytes (16MB usable + 4KB overflow guard)",
            (size_t)BKA_RDRAM_ALLOC_SIZE);

        char romPath[512];
        snprintf(romPath, sizeof(romPath), "%s/rom_base.bin", assetDir);
        FILE* f = fopen(romPath, "rb");
        if (f) {
            size_t bytesRead = fread(gN64_ROM_Base, 1, N64_ROM_SPACE_SIZE, f);
            fclose(f);
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                "Memory Engine Stabilized: physical ROM mapped from %s (%zu bytes).", romPath, bytesRead);
        } else {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "WARNING: rom_base.bin missing, fallback memory will be zeroed.");
            gN64_ROM_Base[0x3B] = 'N'; gN64_ROM_Base[0x3C] = 'B';
            gN64_ROM_Base[0x3D] = 'K'; gN64_ROM_Base[0x3E] = 'E';
        }
    }

    void HardwareRegs_Shutdown() {
        if (gN64_RDRAM)    { munmap(gN64_RDRAM,    BKA_RDRAM_ALLOC_SIZE); gN64_RDRAM    = nullptr; }
        if (gN64_Reg_Base) { munmap(gN64_Reg_Base, N64_REG_SPACE_SIZE);   gN64_Reg_Base = nullptr; }
        if (gN64_PIF_Base) { munmap(gN64_PIF_Base, N64_PIF_SPACE_SIZE);   gN64_PIF_Base = nullptr; }
        if (gN64_ROM_Base) { munmap(gN64_ROM_Base, N64_ROM_SPACE_SIZE);   gN64_ROM_Base = nullptr; }
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Memory Engine Closed down cleanly.");
    }

    struct BKA_ControllerPad {
        uint16_t button;
        int8_t   stick_x;
        int8_t   stick_y;
        uint8_t  errno_val;
    };
    BKA_ControllerPad gN64_ControllerData[4] = {{0, 0, 0, 0}};

    void N64_TriggerVirtualVBlankInterrupt(void) {
        if (!gN64_Reg_Base) return;
        gN64_Reg_Base[MI_INTR_REG_IDX] |= MI_INTR_VI;
        HLE_TriggerN64Event(14);
    }

    void VideoPlugin_OutputFrameTexture(uint32_t hostTextureId) {
        static int diagCount = 0;
        if (++diagCount <= 5) {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                "VideoPlugin: call=%d texId=%u rdr=%p fb_ofs=%08X w=%d h=%d",
                diagCount, hostTextureId, gN64_RDRAM,
                g_active_fb_offset, gFramebufferWidth, gFramebufferHeight);
        }

        if (!gN64_RDRAM || hostTextureId == 0) return;

        uint32_t fbPhysAddr = g_active_fb_offset;
        if (fbPhysAddr == 0) return;

        uint8_t* fbBase = gN64_RDRAM + fbPhysAddr;
        if (fbBase < gN64_RDRAM || fbBase >= gN64_RDRAM + BKA_RDRAM_ALLOC_SIZE) return;

        s32 fbWidth  = gFramebufferWidth;
        s32 fbHeight = gFramebufferHeight;
        if (fbWidth <= 0 || fbWidth > 640)  fbWidth  = 320;
        if (fbHeight <= 0 || fbHeight > 480) fbHeight = 240;

        size_t fbSize = (size_t)fbWidth * fbHeight * 2;
        if (fbBase + fbSize > gN64_RDRAM + BKA_RDRAM_ALLOC_SIZE) return;

        glBindTexture(GL_TEXTURE_2D, hostTextureId);

        static uint32_t* s_convBuffer = nullptr;
        static size_t    s_convBufferSize = 0;
        size_t neededSize = (size_t)fbWidth * fbHeight * 4;
        if (!s_convBuffer || s_convBufferSize < neededSize) {
            free(s_convBuffer);
            s_convBuffer = (uint32_t*)malloc(neededSize);
            s_convBufferSize = neededSize;
        }

        if (s_convBuffer) {
            uint16_t* src = (uint16_t*)fbBase;
            uint32_t* dst = s_convBuffer;
            for (s32 y = 0; y < fbHeight; y++) {
                for (s32 x = 0; x < fbWidth; x++) {
                    uint16_t pixel = *src++;
                    uint8_t r = (uint8_t)(((pixel >> 11) & 0x1F) << 3);
                    uint8_t g = (uint8_t)(((pixel >> 6)  & 0x1F) << 3);
                    uint8_t b = (uint8_t)(((pixel >> 1)  & 0x1F) << 3);
                    uint8_t a = (pixel & 1) ? 0xFF : 0x00;
                    *dst++ = (r << 24) | (g << 16) | (b << 8) | a;
                }
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbWidth, fbHeight, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, s_convBuffer);
        }
    }

}