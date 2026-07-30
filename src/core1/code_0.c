void mainLoop(void){
    s32 x, y;
    s32 r, g, b, a;
    u16 tmp;
    u16 rgba;
    s32 offset;

    // Clear framebuffer every frame to wipe stale data.
    viMgr_clearFramebuffers();

    if((globalTimer_getTime() & 0x7f) == 0x11)
        sns_write_payload_over_heap();
    func_8023DA74();

    if(D_8027A130 != 3 || getGameMode() != GAME_MODE_4_PAUSED)
        globalTimer_incTimer();

    if (!sDisableInput)
        pfsManager_update();
    sDisableInput = FALSE;

    baMotor_80250C08();

    if(!mapSpecificFlags_validateCRC1()){
        eeprom_writeBlocks(0, 0, 0x80397AD0, 0x40);
    }

    switch(D_8027A130){
        case 4:
            func_802E35D8();
            break;
        case 3:
            func_80255524();
            func_80255ACC();
            spawnQueue_func_802C3A18();

            // ---- TEST: fill framebuffer with solid red ----
            for(y = 0; y < gFramebufferHeight; y++) {
                for(x = 0; x < gFramebufferWidth; x++) {
                    offset = x + y * gFramebufferWidth;
                    // RGBA: R=31, G=0, B=0, A=1
                    gFramebuffers[0][offset] = 0xF800 | 0x0001;
                    gFramebuffers[1][offset] = 0xF800 | 0x0001;
                }
            }
            // Remove the normal game_draw for now
            // if(func_802E4424()) game_draw(0);

            spawnQueue_flush();
            break;
    }

    if(D_80275610){
        func_8023DA9C(D_80275610 - 1);
        D_80275610 = 0;
    }

#if 0
    if( !func_8032056C()
        || !levelSpecificFlags_validateCRC1()
        || !dummy_func_80320240()
    ){
        s32 offset;
        for(y= 0x1e; y < gFramebufferHeight - 0x1e; y++){
            for(x = 0x14; x < 0xeb; x++){
                tmp = ((8 * globalTimer_getTime()) + ((x*x) + (y*y)));

                r = _SHIFTL(x>>3, 11, 5);
                g = _SHIFTL(y>>3, 6, 5);
                b = _SHIFTL(tmp>>3, 1, 5);
                a = 1;

                rgba = b | r | g | a;

                offset = ((gFramebufferWidth - 0xFF) / 2) + x + (y*gFramebufferWidth);
                gFramebuffers[0][offset] = (s32) rgba;
                gFramebuffers[1][offset] = (s32) rgba;
            }
        }
    }
#endif
}