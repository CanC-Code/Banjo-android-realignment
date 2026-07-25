import os
import yaml
import struct
import sys

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

# Hard cap on a single asset's contribution to manifest size.
# Prevents a padding gap between two sparsely-placed segments from ballooning
# one entry's reported size into hundreds of MBs of ROM garbage.
# 1 MB is a safe upper bound for any individual N64 asset in BK.
MAX_ASSET_SIZE = 0x100000  # 1 MiB

# Minimum fallback size when offset arithmetic produces 0 or negative.
# 4 bytes is intentionally tiny — the C++ side should treat it as "present
# but empty" rather than trying to decompress/copy garbage.
MIN_ASSET_SIZE = 4

# Binary record layout: Offset(u32) + Size(u32) + Name(32s) + Type(8s) = 48 bytes
RECORD_FORMAT  = '<II32s8s'
RECORD_SIZE    = struct.calcsize(RECORD_FORMAT)  # must be 48
assert RECORD_SIZE == 48, f"Record layout drift: {RECORD_SIZE} bytes (expected 48)"

NAME_MAX_BYTES = 32  # includes mandatory null terminator → 31 usable chars
TYPE_MAX_BYTES = 8   # includes mandatory null terminator → 7 usable chars


# ---------------------------------------------------------------------------
# YAML parsing
# ---------------------------------------------------------------------------

def parse_splat_yaml(yaml_path: str):
    """
    Parse a Splat YAML config and return:
      - asset_entries : list of dicts  {offset, type, name, explicit_size}
      - rom_end       : int  authoritative ROM ceiling for last-entry size calc

    Key behaviours for the BK decompressed.us.v10.yaml structure:
    * Bare-list top-level segment entries like `- [0x010BCD20]` are the
      Splat convention for marking the end of ROM. We parse these explicitly
      to get an authoritative rom_end rather than guessing.
    * Virtual section subsegments (.bss, .data, .rodata, linker_offset,
      rodatabin, textbin) exist only to drive the linker; they do not
      correspond to distinct ROM byte ranges and must be excluded from the
      manifest so ResourceMgr doesn't try to memcpy them.
    * Only subsegment types that represent actual ROM-resident binary payloads
      (bin, hasm, asm, c, and the header type) are emitted as manifest entries.
    * Deduplicates entries at the same ROM offset (keeps first occurrence),
      which handles the many .bss blocks that all share the same address.
    * Hard-exits with a descriptive message on any unrecoverable error.
    """
    # Types that correspond to real ROM bytes we need to copy.
    # Everything else (.bss, .data, .rodata, linker_offset, textbin,
    # rodatabin) is a virtual section marker for the linker only.
    ROM_RESIDENT_TYPES = {
        'bin', 'c', 'hasm', 'asm', 'header',
    }

    if not os.path.exists(yaml_path):
        _fatal(f"Critical file missing: {yaml_path}")

    with open(yaml_path, 'r') as f:
        config = yaml.safe_load(f)

    asset_entries: list[dict] = []
    seen_offsets:  set[int]   = set()
    rom_end = 0

    segments = config.get('segments', [])
    if not segments:
        _fatal(f"YAML has no 'segments' key: {yaml_path}")

    for seg in segments:
        # -------------------------------------------------------------------
        # Bare-list segments: `- [0x010BCD20]`
        # Splat uses a single-element list as an end-of-ROM sentinel.
        # Capture the offset and update rom_end; nothing else to do.
        # -------------------------------------------------------------------
        if isinstance(seg, list):
            if len(seg) >= 1 and isinstance(seg[0], int):
                if seg[0] > rom_end:
                    rom_end = seg[0]
                    print(f"  INFO: ROM end sentinel found: {rom_end:#010x}")
            continue

        if not isinstance(seg, dict):
            continue

        seg_start = seg.get('start', 0)
        seg_size  = seg.get('size')  # None if absent — don't default to 0

        # Advance rom_end using explicit segment size when available.
        # Fall back to just the start address if no size is given.
        if isinstance(seg_start, int):
            if isinstance(seg_size, int):
                candidate_end = seg_start + seg_size
                if candidate_end > rom_end:
                    rom_end = candidate_end
            elif seg_start > rom_end:
                rom_end = seg_start

        subsegments = seg.get('subsegments', [])
        
        # -------------------------------------------------------------------
        # [FIX] Capture top-level segments lacking subsegments
        # Without this, standalone binary assets like 'header' and 
        # 'soundfont' segment arrays will be completely omitted.
        # -------------------------------------------------------------------
        if not subsegments:
            offset = seg.get('start')
            if offset is not None:
                subsegments = [{
                    'offset': offset,
                    'type': seg.get('type', 'unk'),
                    'name': seg.get('name', f'asset_{offset:#010x}'),
                    'size': seg.get('size')
                }]

        for sub in subsegments:
            # Splat subsegment formats:
            #   [offset, type, name]          — 3-element list  (most common)
            #   [offset, type, name, size]    — 4-element list  (less common)
            #   {offset: …, type: …, …}       — dict form       (rare)
            if isinstance(sub, dict):
                offset        = sub.get('start', sub.get('offset'))
                seg_type      = str(sub.get('type', 'unk'))
                name          = str(sub.get('name', f'asset_{offset:#010x}'))
                explicit_size = sub.get('size')
            elif isinstance(sub, list) and len(sub) >= 3:
                offset        = sub[0]
                seg_type      = str(sub[1])
                name          = str(sub[2])
                explicit_size = sub[3] if len(sub) >= 4 else None
            elif isinstance(sub, list) and len(sub) == 1 and isinstance(sub[0], int):
                # Single-element list inside subsegments — treat as local sentinel
                if sub[0] > rom_end:
                    rom_end = sub[0]
                continue
            else:
                continue  # malformed or pure scalar — skip

            if not isinstance(offset, int):
                print(f"  WARNING: Non-integer offset in subsegment {sub!r} — skipped")
                continue

            # Strip leading dot so '.bss' and 'bss' both match.
            bare_type = seg_type.lstrip('.')

            # Intercept code segments and assign a specialized identifier
            if seg_type == 'code' or bare_type == 'code':
                seg_type = 'code_bin'  # Specialized identifier for the runtime handler
            elif bare_type not in ROM_RESIDENT_TYPES:
                continue

            if offset in seen_offsets:
                # Duplicate ROM offsets are normal for shared .bss blocks;
                # suppress the warning for those, it would flood the log.
                continue
            seen_offsets.add(offset)

            asset_entries.append({
                'offset':        offset,
                'type':          seg_type[:TYPE_MAX_BYTES - 1],
                'name':          name,
                'explicit_size': int(explicit_size) if explicit_size is not None else None,
            })

    if not asset_entries:
        _fatal(f"No assets found in {yaml_path}")

    # Mandatory sort — size calculation depends on ascending order
    asset_entries.sort(key=lambda x: x['offset'])

    # If rom_end still isn't past the last known offset, apply a safe 4 KiB
    # padding so the final entry gets a non-zero size.
    last_offset = asset_entries[-1]['offset']
    if rom_end <= last_offset:
        rom_end = last_offset + 0x1000
        print(f"  WARNING: Could not determine ROM end from YAML; "
              f"using {rom_end:#010x} as ceiling for last entry")

    return asset_entries, rom_end


# ---------------------------------------------------------------------------
# Binary manifest writer
# ---------------------------------------------------------------------------

def write_binary_manifest(entries: list[dict], rom_end: int, output_path: str):
    """
    Write the 48-byte fixed-width binary manifest.

    Record layout (little-endian):
        u32  offset   — ROM byte offset of the asset
        u32  size     — byte length to copy into RDRAM
        char name[32] — null-terminated ASCII name  (31 usable chars)
        char type[8]  — null-terminated ASCII type  ( 7 usable chars)

    Size determination priority:
      1. explicit_size from YAML  (most trustworthy)
      2. next_entry_offset − this_offset, capped at MAX_ASSET_SIZE
         (avoids consuming ROM padding / alignment gaps)
      3. rom_end − this_offset, capped at MAX_ASSET_SIZE  (last entry)
      4. MIN_ASSET_SIZE fallback  (should never be needed in practice)

    The cap at MAX_ASSET_SIZE is the key correctness fix: without it, a large
    alignment gap between two sparsely-placed segments causes ResourceMgr to
    memcpy garbage ROM padding into RDRAM, corrupting textures/models and
    producing the white-screen boot hang.
    """
    out_dir = os.path.dirname(output_path)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)

    written = 0
    capped  = 0
    zero_sz = 0

    with open(output_path, 'wb') as f:
        # File header: entry count (4 bytes, LE)
        f.write(struct.pack('<I', len(entries)))

        for i, entry in enumerate(entries):
            offset = entry['offset']

            # --- Size resolution -----------------------------------------
            if entry['explicit_size'] is not None:
                # Honour the YAML's explicit size but still cap it to avoid
                # a typo in the YAML from blowing up the manifest.
                raw_size = entry['explicit_size']
                source   = "explicit"
            elif i < len(entries) - 1:
                raw_size = entries[i + 1]['offset'] - offset
                source   = "delta"
            else:
                raw_size = rom_end - offset
                source   = "rom_end"

            # Apply cap — this is the core fix for the gap/padding problem
            if raw_size > MAX_ASSET_SIZE:
                print(f"  CAP: '{entry['name']}' raw_size={raw_size:#x} "
                      f"({source}) capped to {MAX_ASSET_SIZE:#x}")
                size  = MAX_ASSET_SIZE
                capped += 1
            elif raw_size <= 0:
                print(f"  WARN: '{entry['name']}' size={raw_size} "
                      f"({source}); using MIN_ASSET_SIZE={MIN_ASSET_SIZE}")
                size   = MIN_ASSET_SIZE
                zero_sz += 1
            else:
                size = raw_size

            # --- String encoding -----------------------------------------
            # Names longer than 31 chars are truncated with a warning so the
            # C++ side's fixed char[32] field always has a null terminator.
            raw_name = entry['name'].encode('ascii', 'replace')
            if len(raw_name) > NAME_MAX_BYTES - 1:
                print(f"  TRUNCATE: name '{entry['name']}' "
                      f"({len(raw_name)} bytes) truncated to {NAME_MAX_BYTES - 1}")
                raw_name = raw_name[:NAME_MAX_BYTES - 1]
            name_bin = raw_name.ljust(NAME_MAX_BYTES, b'\x00')

            raw_type = entry['type'].encode('ascii', 'replace')[:TYPE_MAX_BYTES - 1]
            type_bin = raw_type.ljust(TYPE_MAX_BYTES, b'\x00')

            # --- Write record --------------------------------------------
            try:
                f.write(struct.pack(RECORD_FORMAT, offset, size, name_bin, type_bin))
                written += 1
            except struct.error as e:
                _fatal(f"struct.pack failed on asset '{entry['name']}': {e}")

    # Final report
    file_size = os.path.getsize(output_path)
    expected  = 4 + len(entries) * RECORD_SIZE
    print(f"  Written  : {written} entries  ({file_size} bytes)")
    print(f"  Expected : {expected} bytes — {'OK' if file_size == expected else 'MISMATCH!'}")
    if capped:
        print(f"  Capped   : {capped} entries exceeded MAX_ASSET_SIZE={MAX_ASSET_SIZE:#x}")
    if zero_sz:
        print(f"  Fallback : {zero_sz} entries used MIN_ASSET_SIZE (check YAML gaps)")
    if file_size != expected:
        _fatal(f"Output size mismatch for {output_path}: "
               f"got {file_size}, expected {expected}")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _fatal(msg: str):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    # Project root is two levels up from scripts/
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    configs = [
        ("decompressed.us.v10.yaml", "Android/app/src/main/assets/manifest_us.bin"),
        ("decompressed.pal.yaml",    "Android/app/src/main/assets/manifest_pal.bin"),
    ]

    any_error = False
    for yaml_name, bin_name in configs:
        yaml_path = os.path.join(base_dir, yaml_name)
        bin_path  = os.path.join(base_dir, bin_name)

        print(f"\n{'='*60}")
        print(f"Processing : {yaml_name}")
        print(f"Output     : {bin_path}")
        print(f"{'='*60}")

        try:
            entries, rom_end = parse_splat_yaml(yaml_path)
            print(f"  Parsed   : {len(entries)} assets, ROM ceiling = {rom_end:#010x}")
            write_binary_manifest(entries, rom_end, bin_path)
            print(f"SUCCESS: {bin_name}")
        except SystemExit:
            # _fatal already printed; flag overall failure but continue so
            # the other config still runs and produces its own error output.
            any_error = True

    if any_error:
        sys.exit(1)


if __name__ == "__main__":
    main()
