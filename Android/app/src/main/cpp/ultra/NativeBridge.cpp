#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <string>
#include <cstdio>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <GLES2/gl2.h>

#define LOG_TAG "NativeBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* g_jvm = nullptr;
static std::string g_otrPath;

// Exported globally for internal engine resource managers to access pre-embedded assets natively
AAssetManager* g_assetManager = nullptr;

static int g_surfaceWidth  = 320;
static int g_surfaceHeight = 240;
static bool g_engineThreadActive = false;

struct BKA_ControllerPad {
    uint16_t button;
    int8_t   stick_x;
    int8_t   stick_y;
    uint8_t  errno_val;
};

static BKA_ControllerPad g_inputMirror  = {0, 0, 0, 0};
static pthread_mutex_t   g_inputMutex   = PTHREAD_MUTEX_INITIALIZER;

static volatile bool   g_vblankRequested = false;
static pthread_cond_t  g_vblankCond      = pthread_cond_t(PTHREAD_COND_INITIALIZER);
static pthread_mutex_t g_vblankMutex     = PTHREAD_MUTEX_INITIALIZER;

// Airtight Bridge-Level Resource Synchronization Gate
static pthread_mutex_t g_bridgeGateMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_bridgeGateCond  = PTHREAD_COND_INITIALIZER;
static bool            g_bridgeResourcesReady = false;

extern "C" {
    extern uint8_t* gN64_RDRAM;
    extern uint32_t* gN64_Reg_Base;

    void InitN64Registers(const char* assetDir);
    void HardwareRegs_Shutdown(void);

    void BKA_StartEngine(void);
    void BKA_DropEngineLock(void);
    void BKA_ClaimEngineLock(void);

    void ResourceMgr_Init(const char* assetDir);
    void BKA_SignalResourcesReady(void);

    extern BKA_ControllerPad gN64_ControllerData[4];
    void N64_TriggerVirtualVBlankInterrupt(void);
    void VideoPlugin_OutputFrameTexture(uint32_t hostTextureId);

    void BKA_FrameSyncHook(void) {
        pthread_mutex_lock(&g_vblankMutex);
        g_vblankRequested = true;

        BKA_DropEngineLock();

        while (g_vblankRequested) {
            pthread_cond_wait(&g_vblankCond, &g_vblankMutex);
        }

        // CRITICAL FIX: Relinquish the VBlank mutex before attempting to reclaim the Engine Lock 
        // to prevent lock-order inversion and hard deadlocks against the render thread.
        pthread_mutex_unlock(&g_vblankMutex);

        BKA_ClaimEngineLock();
    }
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    LOGI("NativeBridge: JNI Link established securely.");
    return JNI_VERSION_1_6;
}

void* game_thread_fn(void* arg) {
    LOGI("NativeBridge: Game thread execution loop initialized.");
    JNIEnv* env    = nullptr;
    bool  attached = false;

    if (g_jvm != nullptr) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
            LOGI("NativeBridge: Game thread securely attached to JVM environment.");
        } else {
            LOGE("NativeBridge: WARNING - Failed to attach game thread context to JVM.");
        }
    }

    // AIRTIGHT LOCK: Intercept thread execution before it can reach engine ignition
    LOGI("NativeBridge: Game thread evaluating bridge verification lock state...");
    pthread_mutex_lock(&g_bridgeGateMutex);
    while (!g_bridgeResourcesReady) {
        LOGI("NativeBridge: Resource mapping incomplete. Holding engine ignition thread state.");
        pthread_cond_wait(&g_bridgeGateCond, &g_bridgeGateMutex);
    }
    pthread_mutex_unlock(&g_bridgeGateMutex);
    LOGI("NativeBridge: Bridge resource lock released safely. Igniting runtime translation engine.");

    LOGI("NativeBridge: Invoking BKA_StartEngine runtime entry point.");
    BKA_StartEngine();

    LOGI("NativeBridge: Bootloader finalized execution. Engine runtime loop active.");

    while (true) {
        sleep(1000);
    }

    HardwareRegs_Shutdown();

    if (attached && g_jvm != nullptr) {
        g_jvm->DetachCurrentThread();
    }

    g_engineThreadActive = false;
    return nullptr;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_bkawrapper_NativeBridge_nativeInit(JNIEnv* env, jclass clazz, jobject context) {
    if (g_jvm == nullptr) {
        env->GetJavaVM(&g_jvm);
        LOGI("NativeBridge: nativeInit configured JavaVM context reference.");
    }
}

JNIEXPORT void JNICALL
Java_com_bkawrapper_NativeBridge_nativeGameBoot(JNIEnv* env, jclass clazz,
                                                 jstring otrPathStr,
                                                 jobject assetManagerObj) {
    LOGI("NativeBridge: nativeGameBoot sequence triggered.");

    if (!otrPathStr) {
        LOGE("NativeBridge: FATAL ERROR - Target configuration path parameter received as NULL.");
        return;
    }

    if (assetManagerObj != nullptr) {
        g_assetManager = AAssetManager_fromJava(env, assetManagerObj);
        LOGI("NativeBridge: Bound AAssetManager to handle directly pre-embedded configuration files.");
    } else {
        LOGE("NativeBridge: WARNING - AssetManager object is null. Pre-embedded assets may fail to deploy.");
    }

    // Reset initialization state flags safely
    pthread_mutex_lock(&g_bridgeGateMutex);
    g_bridgeResourcesReady = false;
    pthread_mutex_unlock(&g_bridgeGateMutex);

    const char* otrPath = env->GetStringUTFChars(otrPathStr, nullptr);
    if (!otrPath) {
        LOGE("NativeBridge: FATAL ERROR - JNI string layout conversion sequence failed.");
        return;
    }
    g_otrPath = otrPath;
    env->ReleaseStringUTFChars(otrPathStr, otrPath);

    LOGI("NativeBridge: Base tracking path resolved cleanly to: %s", g_otrPath.c_str());

    // CRITICAL FIX: Initialize resources and register mappings BEFORE spawning the game thread.
    // This guarantees that decompression blocks, lookup tables, and file handles are fully 
    // valid prior to BKA_StartEngine() attempting to execute inflate_block().
    LOGI("NativeBridge: Executing ResourceMgr_Init sequence components prior to thread launch...");
    ResourceMgr_Init(g_otrPath.c_str());
    LOGI("NativeBridge: ResourceMgr structural mapping complete.");

    // Check if the engine thread is already active to prevent multi-spawning on Activity recreation
    if (!g_engineThreadActive) {
        LOGI("NativeBridge: Initializing N64 virtual architecture registers...");
        InitN64Registers(g_otrPath.c_str());

        pthread_t gameThread;
        LOGI("NativeBridge: Allocating background worker thread contexts...");
        if (pthread_create(&gameThread, nullptr, game_thread_fn, nullptr) == 0) {
            pthread_detach(gameThread);
            g_engineThreadActive = true;
            LOGI("NativeBridge: Engine thread spawned and bound to waiting sequence state.");
        } else {
            LOGE("NativeBridge: FATAL ERROR - Engine execution context thread creation failed.");
            HardwareRegs_Shutdown();
            return;
        }
    } else {
        LOGI("NativeBridge: Engine thread is already active. Bypassing redundant creation.");
    }

    // Signal safe unlocking states to allow the waiting game thread to proceed into BKA_StartEngine()
    LOGI("NativeBridge: Synchronizing resource gate states to release execution threads...");
    pthread_mutex_lock(&g_bridgeGateMutex);
    g_bridgeResourcesReady = true;
    pthread_cond_broadcast(&g_bridgeGateCond);
    pthread_mutex_unlock(&g_bridgeGateMutex);

    // Maintain native stub fallback signals
    BKA_SignalResourcesReady();
}

JNIEXPORT void JNICALL
Java_com_bkawrapper_NativeBridge_surfaceReady(JNIEnv* env, jclass clazz, jint w, jint h) {
    g_surfaceWidth  = w;
    g_surfaceHeight = h;
    LOGI("NativeBridge: Host viewport surface layout geometry set to: %dx%d", w, h);
}

JNIEXPORT void JNICALL
Java_com_bkawrapper_NativeBridge_updateTexture(JNIEnv* env, jclass clazz, jint textureId) {
    if (gN64_RDRAM == nullptr || gN64_Reg_Base == nullptr) return;

    BKA_ClaimEngineLock();

    pthread_mutex_lock(&g_inputMutex);
    gN64_ControllerData[0] = g_inputMirror;
    pthread_mutex_unlock(&g_inputMutex);

    pthread_mutex_lock(&g_vblankMutex);
    if (g_vblankRequested) {
        N64_TriggerVirtualVBlankInterrupt();
        g_vblankRequested = false;
        pthread_cond_signal(&g_vblankCond);
    }
    pthread_mutex_unlock(&g_vblankMutex);

    VideoPlugin_OutputFrameTexture((uint32_t)textureId);

    BKA_DropEngineLock();
}

JNIEXPORT void JNICALL
Java_com_bkawrapper_NativeBridge_nativeUpdateInput(JNIEnv* env, jclass clazz,
                                                    jint buttons,
                                                    jfloat stickX, jfloat stickY) {
    pthread_mutex_lock(&g_inputMutex);
    g_inputMirror.button    = (uint16_t)buttons;
    g_inputMirror.stick_x   = (int8_t)(stickX * 80.0f);
    g_inputMirror.stick_y   = (int8_t)(stickY * 80.0f);
    g_inputMirror.errno_val = 0;
    pthread_mutex_unlock(&g_inputMutex);
}

} // extern "C"
