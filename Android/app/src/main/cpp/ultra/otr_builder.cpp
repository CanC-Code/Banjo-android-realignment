// File: otr-builder.cpp
// Add this after the ROM loading step (STEP 1)

// ---------------------------------------------------------------------------
// STEP 2: Allocate output buffer for rom_base.bin (preserving original layout)
// ---------------------------------------------------------------------------
uint8_t* romBaseBuffer = nullptr;
size_t romBaseSize = romSize; // Use the same size as the original ROM

// Allocate buffer for rom_base.bin (initialized to 0x00 or 0xFF for debugging)
romBaseBuffer = static_cast<uint8_t*>(calloc(romBaseSize, 1));
if (!romBaseBuffer) {
    LOGE("Failed to allocate rom_base buffer (size=%zu)", romBaseSize);
    free(romData);
    goto cleanup_strings;
}

// Copy the original ROM header (first 4 bytes) to preserve byte order
if (romSize >= 4) {
    memcpy(romBaseBuffer, romData, 4);
}

// ---------------------------------------------------------------------------
// STEP 3: Process manifest and decompress files in-place
// ---------------------------------------------------------------------------
extracted = 0;
compressed = 0;
failed = 0;

for (uint32_t i = 0; i < entryCount; i++) {
    ManifestEntry entry;
    if (fread(&entry, sizeof(ManifestEntry), 1, mFile) != 1) {
        LOGE("Manifest read failed at entry %u", i);
        failed++;
        break;
    }

    if (manifestNeedsSwap) {
        entry.offset = swap_uint32(entry.offset);
        entry.size = swap_uint32(entry.size);
    }

    entry.name[sizeof(entry.name)-1] = '\0';
    entry.type[sizeof(entry.type)-1] = '\0';

    if (entry.offset >= romBaseSize) {
        LOGW("Skipping invalid offset asset %.32s offset=%u (romBaseSize=%zu)",
             entry.name, entry.offset, romBaseSize);
        failed++;
        continue;
    }

    if (entry.size == 0) {
        failed++;
        continue;
    }

    uint64_t endOffset = static_cast<uint64_t>(entry.offset) + entry.size;
    if (endOffset > romBaseSize) {
        LOGW("Clamping oversized asset %.32s (end=%llu, romBaseSize=%zu)",
             entry.name, endOffset, romBaseSize);
        entry.size = static_cast<uint32_t>(romBaseSize - entry.offset);
    }

    uint8_t* srcBuffer = romData + entry.offset;
    uint8_t* destBuffer = romBaseBuffer + entry.offset;

    // Check for Rare compression
    bool isRareCompressed = false;
    if (entry.size >= 8 && srcBuffer[0] == 0x11 && srcBuffer[1] == 0x72) {
        uint32_t declaredSize = (srcBuffer[2] << 24) | (srcBuffer[3] << 16) |
                               (srcBuffer[4] << 8) | srcBuffer[5];
        if (declaredSize > 0 && declaredSize <= MAX_ASSET_SIZE) {
            isRareCompressed = true;
            LOGI("Decompressing %.32s (offset=%u, size=%u -> %u)",
                 entry.name, entry.offset, entry.size, declaredSize);
        }
    }

    if (isRareCompressed) {
        uint32_t written = decompress_rare_to_offset(
            srcBuffer, entry.size, destBuffer, entry.offset, declaredSize);
        if (written == declaredSize) {
            extracted++;
            compressed++;
        } else {
            LOGE("Decompression failed for %.32s (wrote %u/%u bytes)",
                 entry.name, written, declaredSize);
            failed++;
        }
    } else {
        // Copy raw data (no decompression)
        memcpy(destBuffer, srcBuffer, entry.size);
        extracted++;
    }

    // Progress update
    int percent = 10 + static_cast<int>(((uint64_t)i * 89) / entryCount);
    if (percent != lastPercent) {
        char status[128];
        snprintf(status, sizeof(status), "Processing: %.32s", entry.name);
        if (!debug_ui(env, callback, progressMid, percent, status)) {
            break;
        }
        lastPercent = percent;
    }
}

// ---------------------------------------------------------------------------
// STEP 4: Write rom_base.bin to disk
// ---------------------------------------------------------------------------
if (!write_rom_base_from_memory(romBaseBuffer, romBaseSize, cOutDir)) {
    LOGE("Failed writing rom_base.bin");
    free(romBaseBuffer);
    free(romData);
    goto cleanup_strings;
}

// Free buffers
free(romBaseBuffer);
free(romData);