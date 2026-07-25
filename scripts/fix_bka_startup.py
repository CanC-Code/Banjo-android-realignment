import os

def fix_files():
    # Target the exact text replacement for code_0.c
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

if __name__ == "__main__":
    fix_files()
