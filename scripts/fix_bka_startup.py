import os

def fix_files():
    # 1. Fix overlay manager core2 call
    overlay_path = "src/core1/overlay.c" # Adjust path if located elsewhere
    # If overlay_manager is in a different file path, find and update it:
    
    # Let's target the exact text replacement for code_0.c and overlay manager files
    code_0_file = "src/core1/code_0.c"
    if os.path.exists(code_0_file):
        with open(code_0_file, "r") as f:
            content = f.read()
            
        old_func = """void func_8023DA20(s32 arg0){ 
    bzero(&D_8027A130, core2_TEXT_START - (u8*)&D_8027A130);"""
            
        new_func = """void func_8023DA20(s32 arg0){ 
    if (!core2_TEXT_START || core2_TEXT_START < (u8*)&D_8027A130) {
        // Fallback safety guard against uninitialized segment startup crash
        return;
    }
    bzero(&D_8027A130, core2_TEXT_START - (u8*)&D_8027A130);"""

        if old_func in content:
            content = content.replace(old_func, new_func)
            with open(code_0_file, "w") as f:
                f.write(content)
            print("[+] Successfully patched src/core1/code_0.c with safety bounds.")
        else:
            print("[-] Target function signature in code_0.c not matched exactly. Skipping code_0 patch.")

    # Search for overlay manager file dynamically or patch known path
    for root, dirs, files in os.walk("src"):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                with open(filepath, "r", errors="ignore") as f:
                    c_content = f.read()
                
                if "overlayManagerloadCore2" in c_content:
                    old_load = "core2_DATA_START, core2_RODATA_END,"
                    new_load = "core2_DATA_START, core2_DATA_END,"
                    if old_load in c_content:
                        c_content = c_content.replace(old_load, new_load)
                        with open(filepath, "w") as f:
                            f.write(c_content)
                        print(f"[+] Successfully corrected RODATA to DATA end pointer in {filepath}")

if __name__ == "__main__":
    fix_files()
