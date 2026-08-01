#pragma once
#include <PR/sptask.h>
#include <stdint.h>

// Called from osSpTaskStartGo when a GFX task is submitted.
// Parses the Gfx display list and issues OpenGL ES draw calls.
void RSP_ProcessGfxTask(OSTask* tp);