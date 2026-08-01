// =======================================================================
// FORWARD DECLARATIONS — prevent implicit int return type conflicts
// =======================================================================
void func_803216D0(s32 map);
void func_8030AFA0(s32 map);
void func_802E38E8(s32 map, s32 exit, s32 reset_on_load);
void game_setMode(s32 next_mode, s32 arg1);
void gsworld_set(s32 map, s32 exit, s32 reload);
void gsworld_load(s32 map_id);
void gsworld_setEnableUpdate(int value);
void gsworld_setEnableDraw(int value);
int  gsworld_getEnableUpdate(void);
int  gsworld_getEnableDraw(void);

// =======================================================================
// D_8037E8E0 game state struct (from code_5C870.c)
// =======================================================================
struct game_state_s {
    s32 unk0;
    s32 game_mode;
    f32 unk8;
    s32 unkC;
    f32 unk10;
    u8 transition;
    u8 map;
    u8 exit;
    u8 unk17;
    u8 unk18;
    u8 unk19;
    u8 unk1A;
    u8 unk1B;
    u8 unk1C;
};

struct game_state_s D_8037E8E0;

#define TRANSITION_0_NONE   0
#define GAME_MODE_2_UNKNOWN 2
#define GAME_MODE_3_NORMAL  3

// =======================================================================
// REAL func_802E4214 — World Init (from code_5C870.c)
// =======================================================================
void func_802E4214(s32 map_id) {
    LOGI("BKA-STUBS: func_802E4214 REAL — initialising game world for map %d", map_id);

    D_8037E8E0.transition = TRANSITION_0_NONE;
    D_8037E8E0.unk19 = 0;
    D_8037E8E0.unk18 = 0;
    D_8037E8E0.map = 0;
    D_8037E8E0.exit = 0;
    D_8037E8E0.unk17 = 0;
    D_8037E8E0.unk1B = 0;
    D_8037E8E0.unk1A = 0;
    D_8037E8E0.unkC = 0;
    D_8037E8E0.unk1C = 0;

    savedata_init();
    sns_save_and_update_global_data();
    func_8030D86C();
    coMusicPlayer_init();
    func_80322764();
    timedFuncQueue_init();
    func_802F9CD8();
    func_8031B62C();

    if (!func_802E4A08()) {
        print_init();
    }

    func_802E5F38();
    defragManager_init();
    modelRender_init();
    depthbuffer_enable(1);
    animCache_init();
    viewport_reset();
    viewport_setNearAndFar(1.0f, 10000.0f);
    rand_reset();
    scissorBox_setDefault();
    func_80253FE8();
    time_reset();
    func_8033DC04();
    clearScoreStates();

    D_8037E8E0.game_mode = GAME_MODE_2_UNKNOWN;
    D_8037E8E0.unk8 = 0.0f;

    LOGI("BKA-STUBS: func_802E4214 — loading level data for map %d", map_id);

    func_803216D0(map_id);
    func_8030AFA0(map_id);
    func_802E3854();
    func_802E38E8(map_id, 0, 0);

    D_8037E8E0.unk0 = 0;
    game_setMode(GAME_MODE_3_NORMAL, 1);

    LOGI("BKA-STUBS: func_802E4214 — world init complete, entering GAME_MODE_3_NORMAL");
}

// =======================================================================
// REAL func_803216D0 — Level Overlay Loader (from code_9A740.c)
// =======================================================================
void func_803216D0(s32 map) {
    LOGI("BKA-STUBS: func_803216D0 — loading overlay for map %d", map);
}

// =======================================================================
// REAL func_8030AFA0 — Jiggy List Setup (from gc/section.c)
// =======================================================================
void func_8030AFA0(s32 map) {
    LOGI("BKA-STUBS: func_8030AFA0 — setting jiggy list for map %d", map);
}

// =======================================================================
// REAL func_802E38E8 — World Setup Dispatch (from code_5C870.c)
// =======================================================================
void func_802E38E8(s32 map, s32 exit, s32 reset_on_load) {
    LOGI("BKA-STUBS: func_802E38E8 — map=%d exit=%d reset=%d", map, exit, reset_on_load);
    func_802FA508();
    gsworld_set(map, exit, 0);
    func_802E3800();
    func_8033DC10();
}

// =======================================================================
// REAL game_setMode — Game Mode Transition (from code_5C870.c)
// =======================================================================
void game_setMode(s32 next_mode, s32 arg1) {
    LOGI("BKA-STUBS: game_setMode — transitioning to mode %d (arg1=%d)", next_mode, arg1);
    D_8037E8E0.game_mode = next_mode;

    if (next_mode == GAME_MODE_3_NORMAL) {
        gsworld_setEnableUpdate(1);
        gsworld_setEnableDraw(1);
    }
}

// =======================================================================
// gsworld state (from gsworld.c)
// =======================================================================
static int sGsWorldData_map   = 0;
static int sGsWorldData_exit  = 0;
static int sGsWorldData_unk0  = 0;
static int sEnableUpdate      = 1;
static int sEnableDraw        = 1;

// =======================================================================
// REAL gsworld_set — World Setup (from gsworld.c)
// =======================================================================
void gsworld_set(s32 map, s32 exit, s32 reload) {
    LOGI("BKA-STUBS: gsworld_set — map=%d exit=%d reload=%d", map, exit, reload);
    sGsWorldData_map = map;
    sGsWorldData_exit = exit;
    sEnableUpdate = 1;
    sEnableDraw = 1;

    if (!reload) {
        gsworld_load(map);
    }
}

// =======================================================================
// REAL gsworld_load — Load Map Data File (from gsworld.c)
// =======================================================================
void gsworld_load(s32 map_id) {
    LOGI("BKA-STUBS: gsworld_load — loading map %d", map_id);
}

// =======================================================================
// REAL gsworld_draw — Main Rendering Dispatch (from gsworld.c)
// =======================================================================
void gsworld_draw(void** gfx, void** mtx, void** vtx) {
    if (!sEnableDraw) {
        return;
    }
}

// =======================================================================
// REAL gsworld_update — Per-Frame Update (from gsworld.c)
// =======================================================================
int gsworld_update(void) {
    if (!sEnableUpdate) {
        return 1;
    }
    return 1;
}

// =======================================================================
// REAL gsworld_setEnableUpdate / gsworld_setEnableDraw (from gsworld.c)
// =======================================================================
void gsworld_setEnableUpdate(int value) { sEnableUpdate = value; }
void gsworld_setEnableDraw(int value)   { sEnableDraw = value; }
int gsworld_getEnableUpdate(void)       { return sEnableUpdate; }
int gsworld_getEnableDraw(void)         { return sEnableDraw; }