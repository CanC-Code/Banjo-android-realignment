#include <ultra64.h>
#include "bka_safe_base.h"          // for BKA_TRANSLATE_ADDR
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

typedef struct struct_2a_s{
    char *name;
    u32 ram_start;                  // N64 address
    u32 ram_end;
    u32 unkC;                       // uncompressed_rom_range_start
    u32 unk10;                      // uncompressed_rom_range_end
    u32 code_start;
    u32 code_end;
    u32 data_start;
    u32 data_end;
    u32 bss_start;
    u32 bss_end;
} OverlayAddressMap;

// External u32 variables defined in missing_stubs.c
#define SEGMENT_EXTERNS(segname) \
    extern u32 segname##_VRAM; \
    extern u32 segname##_VRAM_END; \
    extern u32 segname##_ROM_START; \
    extern u32 segname##_ROM_END; \
    extern u32 segname##_TEXT_START; \
    extern u32 segname##_TEXT_END; \
    extern u32 segname##_DATA_START; \
    extern u32 segname##_DATA_END; \
    extern u32 segname##_RODATA_START; \
    extern u32 segname##_RODATA_END; \
    extern u32 segname##_BSS_START; \
    extern u32 segname##_BSS_END

SEGMENT_EXTERNS(core2);
SEGMENT_EXTERNS(emptyLvl);
SEGMENT_EXTERNS(CC);
SEGMENT_EXTERNS(MMM);
SEGMENT_EXTERNS(GV);
SEGMENT_EXTERNS(TTC);
SEGMENT_EXTERNS(MM);
SEGMENT_EXTERNS(BGS);
SEGMENT_EXTERNS(RBB);
SEGMENT_EXTERNS(FP);
SEGMENT_EXTERNS(CCW);
SEGMENT_EXTERNS(SM);
SEGMENT_EXTERNS(cutscenes);
SEGMENT_EXTERNS(lair);
SEGMENT_EXTERNS(fight);

// Number of overlay entries
#define OVERLAY_COUNT 15

/* .data – initialized at runtime */
static OverlayAddressMap overlayAddressMap[OVERLAY_COUNT];

static void initOverlayAddressMap(void) {
    // This function must be called before any overlay load.
    // Fill each entry using the external u32 variables.
    #define ASSIGN_ENTRY(segname, realname) \
        overlayAddressMap[1] = (OverlayAddressMap){#realname, \
            segname##_VRAM, segname##_VRAM_END, \
            segname##_ROM_START, segname##_ROM_END, \
            segname##_TEXT_START, segname##_TEXT_END, \
            segname##_DATA_START, segname##_RODATA_END, \
            segname##_BSS_START, segname##_BSS_END};

    // index 0: core2
    overlayAddressMap[0] = (OverlayAddressMap){"gs",
        core2_VRAM, core2_VRAM_END,
        core2_ROM_START, core2_ROM_END,
        core2_TEXT_START, core2_TEXT_END,
        core2_DATA_START, core2_RODATA_END,
        core2_BSS_START, core2_BSS_END};
    // index 1: emptyLvl (dummy)
    overlayAddressMap[1] = (OverlayAddressMap){"coshow",
        emptyLvl_VRAM, emptyLvl_VRAM_END,
        emptyLvl_ROM_START, emptyLvl_ROM_END,
        0,0,0,0,0,0};
    overlayAddressMap[2] = (OverlayAddressMap){"whale",
        CC_VRAM, CC_VRAM_END,
        CC_ROM_START, CC_ROM_END,
        CC_TEXT_START, CC_TEXT_END,
        CC_DATA_START, CC_RODATA_END,
        CC_BSS_START, CC_BSS_END};
    overlayAddressMap[3] = (OverlayAddressMap){"haunted",
        MMM_VRAM, MMM_VRAM_END,
        MMM_ROM_START, MMM_ROM_END,
        MMM_TEXT_START, MMM_TEXT_END,
        MMM_DATA_START, MMM_RODATA_END,
        MMM_BSS_START, MMM_BSS_END};
    overlayAddressMap[4] = (OverlayAddressMap){"desert",
        GV_VRAM, GV_VRAM_END,
        GV_ROM_START, GV_ROM_END,
        GV_TEXT_START, GV_TEXT_END,
        GV_DATA_START, GV_RODATA_END,
        GV_BSS_START, GV_BSS_END};
    overlayAddressMap[5] = (OverlayAddressMap){"beach",
        TTC_VRAM, TTC_VRAM_END,
        TTC_ROM_START, TTC_ROM_END,
        TTC_TEXT_START, TTC_TEXT_END,
        TTC_DATA_START, TTC_RODATA_END,
        TTC_BSS_START, TTC_BSS_END};
    overlayAddressMap[6] = (OverlayAddressMap){"jungle",
        MM_VRAM, MM_VRAM_END,
        MM_ROM_START, MM_ROM_END,
        MM_TEXT_START, MM_TEXT_END,
        MM_DATA_START, MM_RODATA_END,
        MM_BSS_START, MM_BSS_END};
    overlayAddressMap[7] = (OverlayAddressMap){"swamp",
        BGS_VRAM, BGS_VRAM_END,
        BGS_ROM_START, BGS_ROM_END,
        BGS_TEXT_START, BGS_TEXT_END,
        BGS_DATA_START, BGS_RODATA_END,
        BGS_BSS_START, BGS_BSS_END};
    overlayAddressMap[8] = (OverlayAddressMap){"ship",
        RBB_VRAM, RBB_VRAM_END,
        RBB_ROM_START, RBB_ROM_END,
        RBB_TEXT_START, RBB_TEXT_END,
        RBB_DATA_START, RBB_RODATA_END,
        RBB_BSS_START, RBB_BSS_END};
    overlayAddressMap[9] = (OverlayAddressMap){"snow",
        FP_VRAM, FP_VRAM_END,
        FP_ROM_START, FP_ROM_END,
        FP_TEXT_START, FP_TEXT_END,
        FP_DATA_START, FP_RODATA_END,
        FP_BSS_START, FP_BSS_END};
    overlayAddressMap[10] = (OverlayAddressMap){"tree",
        CCW_VRAM, CCW_VRAM_END,
        CCW_ROM_START, CCW_ROM_END,
        CCW_TEXT_START, CCW_TEXT_END,
        CCW_DATA_START, CCW_RODATA_END,
        CCW_BSS_START, CCW_BSS_END};
    overlayAddressMap[11] = (OverlayAddressMap){"training",
        SM_VRAM, SM_VRAM_END,
        SM_ROM_START, SM_ROM_END,
        SM_TEXT_START, SM_TEXT_END,
        SM_DATA_START, SM_RODATA_END,
        SM_BSS_START, SM_BSS_END};
    overlayAddressMap[12] = (OverlayAddressMap){"intro",
        cutscenes_VRAM, cutscenes_VRAM_END,
        cutscenes_ROM_START, cutscenes_ROM_END,
        cutscenes_TEXT_START, cutscenes_TEXT_END,
        cutscenes_DATA_START, cutscenes_RODATA_END,
        cutscenes_BSS_START, cutscenes_BSS_END};
    overlayAddressMap[13] = (OverlayAddressMap){"witch",
        lair_VRAM, lair_VRAM_END,
        lair_ROM_START, lair_ROM_END,
        lair_TEXT_START, lair_TEXT_END,
        lair_DATA_START, lair_RODATA_END,
        lair_BSS_START, lair_BSS_END};
    overlayAddressMap[14] = (OverlayAddressMap){"battle",
        fight_VRAM, fight_VRAM_END,
        fight_ROM_START, fight_ROM_END,
        fight_TEXT_START, fight_TEXT_END,
        fight_DATA_START, fight_RODATA_END,
        fight_BSS_START, fight_BSS_END};
}

static s32 overlayCount = OVERLAY_COUNT;

/* .bss */
enum overlay_e overlayMgrLoadedId;

void overlayManagerdebug(void);

/* .code */
OverlayAddressMap *__overlayManagergetLargetOverlayAddressMap(void){
    //returns OverlayAddressMap ptr with largest RAM size
    int i;
    OverlayAddressMap * largest_overlay;

    largest_overlay = &overlayAddressMap[1];
    for(i = 1; i < overlayCount; i++){
        if(largest_overlay->ram_end - largest_overlay->ram_start < (u32)(overlayAddressMap[i].ram_end - overlayAddressMap[i].ram_start)){
            largest_overlay = &overlayAddressMap[i];
        }
    }
    return largest_overlay;
}

s32 __overlayManager80251170(void){
    return 0;
}

s32 __overlayManager80251178(void){
    int sp24;
    OverlayAddressMap *largest_overlay;
    s32 sp1C;
    s32 sp18;

    largest_overlay = __overlayManagergetLargetOverlayAddressMap();
    sp18 = func_802546DC();
    sp1C = __overlayManager80251170();

    return ((sp1C + (u8 *)gFramebuffers) - largest_overlay->ram_end) + sp18;
}

void __overlayManager802511C4(void){
    s32 sp24;
    int sp20;
    int sp1C;
    int heap_size;
    u32 tmp_v0;

    sp24 = __overlayManager80251178();
    heap_size = heap_get_size();
    sp20 = func_802546DC();
    sp1C = heap_size - sp20;

    if(sp24 < 0){
        overlayManagerdebug();
        tmp_v0 = sp1C + sp24;
        while( tmp_v0 & 0xF){tmp_v0--;}
    }
}

int overlayManagergetLoadedId(void){
    return overlayMgrLoadedId;
}

n64_bool overlayManagerisOverlayLoaded(int overlay_id){
    return overlayMgrLoadedId == overlay_id;
}

n64_bool overlayManagerload(enum overlay_e overlay_id){
    s32 rom_addr;

    if(overlay_id == 0)
        return FALSE;

    if(overlay_id == overlayMgrLoadedId)
        return FALSE;

    overlayMgrLoadedId = overlay_id;
    rom_addr = (s32)(overlayAddressMap + overlay_id);

    overlay_load(
        overlay_id,
        ((OverlayAddressMap*)rom_addr)->ram_start,
        ((OverlayAddressMap*)rom_addr)->ram_end,
        ((OverlayAddressMap*)rom_addr)->unkC,
        ((OverlayAddressMap*)rom_addr)->unk10,
        ((OverlayAddressMap*)rom_addr)->code_start,
        ((OverlayAddressMap*)rom_addr)->code_end,
        ((OverlayAddressMap*)rom_addr)->data_start,
        ((OverlayAddressMap*)rom_addr)->data_end,
        ((OverlayAddressMap*)rom_addr)->bss_start,
        ((OverlayAddressMap*)rom_addr)->bss_end
    );
    return TRUE;
}

s32 overlayManagerclearLoadedId(void){
    overlayMgrLoadedId = 0;
}

void overlayManagerloadCore2(void){
    // Ensure the map is initialized before any overlay load
    static int mapInitialized = 0;
    if (!mapInitialized) {
        initOverlayAddressMap();
        mapInitialized = 1;
    }

    overlayManagerclearLoadedId();
    overlay_load(0,
        core2_VRAM, core2_VRAM_END,
        core2_ROM_START, core2_ROM_END,
        core2_TEXT_START, core2_TEXT_END,
        core2_DATA_START, core2_RODATA_END,
        core2_BSS_START, core2_BSS_END
    );
    __overlayManager802511C4();
}

void overlayManagerdebug(void){}