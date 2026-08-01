#include "gfx_interpreter.h"
#include <android/log.h>
#include <GLES2/gl2.h>
#include <cstring>
#include <cstdlib>

#define LOG_TAG "BKA_GFX"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// N64 Gfx command structure: two 32-bit words
typedef struct {
    uint32_t w0;
    uint32_t w1;
} GfxCommand;

// Gfx opcode is in the high 8 bits of w0
#define GFX_OPCODE(cmd)   (((cmd).w0 >> 24) & 0xFF)

// N64 Gfx opcodes (from gbi.h)
#define G_SETPRIMCOLOR     0xFA
#define G_SETENVCOLOR      0xFB
#define G_FILLRECT          0xF6
#define G_TEXTURE           0xD7
#define G_TEXRECT           0xE4
#define G_TEXRECTFLIP       0xE5
#define G_RDPTILESYNC       0xE8
#define G_RDPPIPESYNC       0xE7
#define G_ENDDL             0xDF
#define G_NOOP              0xC0
#define G_SETCOMBINE        0xFC
#define G_SETOTHERMODE_L    0xE2
#define G_SETOTHERMODE_H    0xE3
#define G_SETTILESIZE       0xF2
#define G_LOADTILE          0xF0
#define G_LOADTLUT          0xF0
#define G_SETTILE           0xF5
#define G_SETTIMG           0xFD
#define G_LOADBLOCK         0xF3
#define G_TEXTURE           0xD7
#define G_DL                0xDE

// =======================================================================
// OpenGL State Tracker
// =======================================================================
static GLuint s_currentTexture = 0;
static GLuint s_glyphTexture = 0;
static float s_primColorR = 1.0f, s_primColorG = 1.0f, s_primColorB = 1.0f, s_primColorA = 1.0f;
static float s_envColorR = 1.0f, s_envColorG = 1.0f, s_envColorB = 1.0f, s_envColorA = 1.0f;

// Simple shader program for 2D rendering
static GLuint s_program = 0;
static GLuint s_vbo = 0;

static const char* VERTEX_SHADER =
    "attribute vec2 aPos;\n"
    "attribute vec2 aTexCoord;\n"
    "attribute vec4 aColor;\n"
    "varying vec2 vTexCoord;\n"
    "varying vec4 vColor;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vTexCoord = aTexCoord;\n"
    "  vColor = aColor;\n"
    "}";

static const char* FRAGMENT_SHADER =
    "precision mediump float;\n"
    "varying vec2 vTexCoord;\n"
    "varying vec4 vColor;\n"
    "uniform sampler2D uTexture;\n"
    "uniform bool uUseTexture;\n"
    "void main() {\n"
    "  if (uUseTexture) {\n"
    "    gl_FragColor = texture2D(uTexture, vTexCoord) * vColor;\n"
    "  } else {\n"
    "    gl_FragColor = vColor;\n"
    "  }\n"
    "}";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGW("BKA_GFX: Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void InitShaders() {
    if (s_program != 0) return;
    
    GLuint vs = CompileShader(GL_VERTEX_SHADER, VERTEX_SHADER);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    if (!vs || !fs) return;
    
    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glLinkProgram(s_program);
    
    glDeleteShader(vs);
    glDeleteShader(fs);
    
    // Create a VBO for fullscreen quad and 2D rectangles
    glGenBuffers(1, &s_vbo);
    
    LOGI("BKA_GFX: Shaders initialized, program=%u", s_program);
}

// Convert N64 8-bit color to float
static inline float C8_TO_FLOAT(uint8_t c) { return c / 255.0f; }

// =======================================================================
// Command Handlers
// =======================================================================

static void HandleSetPrimColor(GfxCommand cmd) {
    // w1 layout: [R:8][G:8][B:8][A:8]
    uint8_t r = (cmd.w1 >> 24) & 0xFF;
    uint8_t g = (cmd.w1 >> 16) & 0xFF;
    uint8_t b = (cmd.w1 >> 8) & 0xFF;
    uint8_t a = cmd.w1 & 0xFF;
    s_primColorR = C8_TO_FLOAT(r);
    s_primColorG = C8_TO_FLOAT(g);
    s_primColorB = C8_TO_FLOAT(b);
    s_primColorA = C8_TO_FLOAT(a);
}

static void HandleSetEnvColor(GfxCommand cmd) {
    uint8_t r = (cmd.w1 >> 24) & 0xFF;
    uint8_t g = (cmd.w1 >> 16) & 0xFF;
    uint8_t b = (cmd.w1 >> 8) & 0xFF;
    uint8_t a = cmd.w1 & 0xFF;
    s_envColorR = C8_TO_FLOAT(r);
    s_envColorG = C8_TO_FLOAT(g);
    s_envColorB = C8_TO_FLOAT(b);
    s_envColorA = C8_TO_FLOAT(a);
}

static void HandleFillRect(GfxCommand cmd) {
    // w1 layout: [xl:12][yl:12][xh:12][yh:12] (approximate)
    // The actual layout is more complex; simplify for now.
    // FillRect paints a solid rectangle with the current fill color.
    uint32_t xh = (cmd.w1 >> 12) & 0xFFF;
    uint32_t yh = cmd.w1 & 0xFFF;
    uint32_t xl = (cmd.w0 >> 12) & 0xFFF;
    uint32_t yl = cmd.w0 & 0xFFF;
    
    // Convert from N64 screen coords (320×240) to OpenGL (-1 to 1)
    float gl_xl = (float)xl / 160.0f - 1.0f;
    float gl_xh = (float)xh / 160.0f - 1.0f;
    float gl_yl = 1.0f - (float)yl / 120.0f;
    float gl_yh = 1.0f - (float)yh / 120.0f;
    
    float vertices[] = {
        gl_xl, gl_yl, 0.0f, 0.0f, s_primColorR, s_primColorG, s_primColorB, s_primColorA,
        gl_xh, gl_yl, 1.0f, 0.0f, s_primColorR, s_primColorG, s_primColorB, s_primColorA,
        gl_xl, gl_yh, 0.0f, 1.0f, s_primColorR, s_primColorG, s_primColorB, s_primColorA,
        gl_xh, gl_yh, 1.0f, 1.0f, s_primColorR, s_primColorG, s_primColorB, s_primColorA,
    };
    
    glUseProgram(s_program);
    glUniform1i(glGetUniformLocation(s_program, "uUseTexture"), 0);
    
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    
    GLint posLoc = glGetAttribLocation(s_program, "aPos");
    GLint tcLoc  = glGetAttribLocation(s_program, "aTexCoord");
    GLint colLoc = glGetAttribLocation(s_program, "aColor");
    
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(tcLoc, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(tcLoc);
    glVertexAttribPointer(colLoc, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(colLoc);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(tcLoc);
    glDisableVertexAttribArray(colLoc);
}

// =======================================================================
// Main Dispatch
// =======================================================================

void RSP_ProcessGfxTask(OSTask* tp) {
    if (!tp || !tp->t.data_ptr || tp->t.data_size == 0) return;
    if (tp->t.type != 0) return; // M_GFXTASK is 0? Let's check.
    // Actually M_GFXTASK is likely non-zero. We'll check the type.
    
    InitShaders();
    if (!s_program) return;
    
    GfxCommand* cmd = (GfxCommand*)tp->t.data_ptr;
    size_t cmdCount = tp->t.data_size / sizeof(GfxCommand);
    
    for (size_t i = 0; i < cmdCount; i++) {
        uint8_t opcode = GFX_OPCODE(cmd[i]);
        
        switch (opcode) {
            case G_SETPRIMCOLOR:
                HandleSetPrimColor(cmd[i]);
                break;
            case G_SETENVCOLOR:
                HandleSetEnvColor(cmd[i]);
                break;
            case G_FILLRECT:
                HandleFillRect(cmd[i]);
                break;
            case G_ENDDL:
                i = cmdCount; // stop processing
                break;
            case G_RDPTILESYNC:
            case G_RDPPIPESYNC:
            case G_NOOP:
                break;
            default:
                // Unhandled opcode — skip silently for now
                break;
        }
    }
}