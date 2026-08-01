#ifndef ENUM_COMPAT_H
#define ENUM_COMPAT_H

// The Android-ported source files use descriptive names, but the original
// enums.h uses numbered names. These aliases make both work.

// BGS level-specific flags
#define BGS_SPECIFIC_FLAG_WALKWAY_JIGGY_RESET              BGS_SPECIFIC_FLAG_2_WALKWAY_JIGGY_RESET
#define BGS_SPECIFIC_FLAG_WALKWAY_JIGGY_TIMER_RUNNING      BGS_SPECIFIC_FLAG_3_WALKWAY_JIGGY_TIMER_RUNNING
#define BGS_SPECIFIC_FLAG_WALKWAY_JIGGY                    BGS_SPECIFIC_FLAG_4_WALKWAY_JIGGY
#define BGS_SPECIFIC_FLAG_WALKWAY_JIGGY_SWITCH_PRESSED     BGS_SPECIFIC_FLAG_5_WALKWAY_JIGGY_SWITCH_PRESSED
#define BGS_SPECIFIC_FLAG_MAZE_JIGGY_SWITCH_PRESSED        BGS_SPECIFIC_FLAG_9_MAZE_JIGGY_SWITCH_PRESSED
#define BGS_SPECIFIC_FLAG_MAZE_JIGGY_RESET                 BGS_SPECIFIC_FLAG_B_MAZE_JIGGY_RESET
#define BGS_SPECIFIC_FLAG_MAZE_JIGGY_TIMER_RUNNING         BGS_SPECIFIC_FLAG_C_MAZE_JIGGY_TIMER_RUNNING
#define BGS_SPECIFIC_FLAG_MAZE_JIGGY                       BGS_SPECIFIC_FLAG_D_MAZE_JIGGY

#endif // ENUM_COMPAT_H

// Actor name aliases — Android source files use descriptive names
// that differ from the original decomp's numbered names
#define ACTOR_298_ZUBBA_DOOR    ACTOR_29C_ZUBBA_DOCILE
#define ACTOR_29A_ZUBBA_DOOR    ACTOR_29C_ZUBBA_DOCILE

// CCW season bare-name aliases (used by Android-ported source files)
#define SPRING  CCW_SEASON_0_SPRING
#define SUMMER  CCW_SEASON_1_SUMMER
#define AUTUMN  CCW_SEASON_2_AUTUMN
#define WINTER  CCW_SEASON_3_WINTER

// Level-specific flag aliases
#define LEVEL_FLAG_25_CCW_UNKNOWN    LEVEL_FLAG_38_CCW_UNKNOWN

// Grublin animation aliases (Android port uses different names)
#define ASSET_62_ANIM_GRUBLIN_IDLE    ASSET_62_ANIM_GRUBLIN_IDLE_WALK
#define ASSET_63_ANIM_GRUBLIN_WALK    ASSET_63_ANIM_GRUBLIN_CHASE
#define ASSET_64_ANIM_GRUBLIN_JUMP    ASSET_64_ANIM_GRUBLIN_ALERT

#define ACTOR_175_MODEL_RUSTY_BUCKET_REAR_PROPELLER    ACTOR_175_RUSTY_BUCKET_REAR_PROPELLER

#define MARKER_184_RBB_EGG_TOLL    MARKER_182_RBB_EGG_TOLL
#define MARKER_183_RBB_EGG_TOLL    MARKER_182_RBB_EGG_TOLL

// SM vegetable actors
#define ACTOR_TOPPER_THE_CARROT_B    ACTOR_36F_TOPPER_THE_CARROT_B
#define ACTOR_BAWL_THE_ONION_B       ACTOR_36E_BAWL_THE_ONION_B
#define ACTOR_COLLYWOBBLE_B          ACTOR_36D_COLLYWOBBLE_B

// SM unknown
#define MARKER_1F0_SM_UNKNOWN        MARKER_1F5_UNKNOWN
#define ACTOR_3BD_SM_UNKNOWN         ACTOR_3BA_UNKNOWN

// === Generated from ninja -k 0 error dump ===

// BS (behaviour state) aliases – when “did you mean” existed
#define BS_ANT_JUMP                BS_5_JUMP
#define BS_CROUCH                  BS_7_CROUCH
#define BS_LONGLEG_WALK            BS_27_LONGLEG_WALK
#define BS_LONGLEG_SLIDE           BS_55_LONGLEG_SLIDE
#define BS_LONGLEG_JUMP            BS_28_LONGLEG_JUMP
#define BS_LONGLEG_EXIT            BS_29_LONGLEG_EXIT
#define BS_BSHOCK_JUMP             BS_22_BSHOCK_JUMP
#define BS_BTROT_OW                BS_7B_BTROT_OW
#define BS_CARRY_THROW             BS_5B_CARRY_THROW
#define BS_CROC_WALK               BS_5F_CROC_WALK
#define BS_CROC_JUMP               BS_60_CROC_JUMP
#define BS_CROC_OW                 BS_63_CROC_OW
#define BS_CROC_DIE                BS_64_CROC_DIE
#define BS_CROC_EAT_BAD            BS_6F_CROC_EAT_BAD
#define BS_CROC_BOUNCE             BS_A1_CROC_BOUNCE
#define BS_WALRUS_WALK             BS_68_WALRUS_WALK
#define BS_WALRUS_JUMP             BS_69_WALRUS_JUMP
#define BS_WALRUS_OW               BS_6C_WALRUS_OW
#define BS_WALRUS_DIE              BS_6D_WALRUS_DIE
#define BS_WALRUS_BOUNCE           BS_A2_WALRUS_BOUNCE
#define BS_BEE_OW                  BS_E_OW
#define BS_BEE_BOUNCE              BS_A3_BEE_BOUNCE
#define BS_BSHOCK_CHARGE           BS_21_BSHOCK_CHARGE
#define BS_CLAW                    BS_6_CLAW
#define BS_BBARGE                  BS_13_BBARGE
#define BS_WALK_CREEP              BS_1F_WALK_CREEP
#define BS_WALK                    BS_3_WALK
#define BS_WALK_MUD                BS_7A_WALK_MUD
#define BS_SKID                    BS_C_SKID

// BS states that had no suggestion – use direct hex values from original decomp
#define BS_ANT_WALK                0x62   // verify in original bs.h
#define BS_BOMB                    0x12   // likely BS_12_BOMB
#define BS_FLY_OW                  0x2A   // guess, check original
#define BS_BFLAP                   0x26   // guess, check original
#define BS_ROLL                    BS_31_ROLL
#define BS_SPLAT                   0x30   // guess
#define BS_SLIDE                   0x20   // probably BS_20_SLIDE
#define BS_D_TIMEOUT               0x0D
#define BS_53_TIMEOUT              0x53
#define BS_3F                      0x3F
#define BS_BEE_WALK                0x56   // check original
#define BS_BEE_JUMP                0x57   // check original
#define BS_BEE_FLY                 0x58   // check original
#define BS_BEE_DIE                 0x59   // check original
#define BS_PUMPKIN_BOUNCE          0x9F   // check original
#define BS_ANT_BOUNCE              0xA0   // check original

// Actor aliases – fallback to number when no suggestion
#define ACTOR_235_FP_ENTANCE_DOOR  0x235
#define ACTOR_2E5_DOOR_OF_GRUNTY   0x2E5
#define ACTOR_2FA_BANJOS_HOUSE_ROYSTEN  0x2FA

// Asset aliases – fallback to number
#define ASSET_3B7_MODEL_TTC_STAIRS_1   0x3B7
#define ASSET_475_UNKNOWN              0x475
#define ASSET_35B_FF_PRIZE_TOOTY       0x35B

// Marker fallback
#define MARKER_BB_UNKNOWN           0xBB

// Level flag fallback (probably a single flag value)
#define LEVEL_FLAG_E_CC_UNKNOWN     0xE
