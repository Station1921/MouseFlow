#!/usr/bin/env python3
"""
Reliable PE dependency / subsystem checker for MouseFlow builds.

Parses the authoritative output of `objdump -p <exe>` instead of hand-rolling
PE parsing (the previous hand-rolled parser produced false positives for
libwinpthread-1.dll).

Requires binutils `objdump` on PATH.
"""
import subprocess
import sys
import os

SUBSYS = {
    0: "Native",
    1: "Native (driver)",
    2: "Windows GUI",
    3: "Windows Console (CUI)",
    5: "OS/2 CUI",
    7: "POSIX CUI",
    9: "Windows CE GUI",
    10: "EFI Application",
}

# DLLs that are part of the Windows OS and therefore NOT a "runtime dependency"
# we need to ship alongside the exe.
SYSTEM_DLLS = {
    "advapi32.dll", "comctl32.dll", "comdlg32.dll", "d2d1.dll", "d3d11.dll",
    "dcomp.dll", "dwmapi.dll", "gdi32.dll", "kernel32.dll", "msimg32.dll",
    "msvcrt.dll", "ole32.dll", "oleaut32.dll", "shell32.dll", "user32.dll",
    "uxtheme.dll", "gdiplus.dll", "shlwapi.dll", "version.dll", "winmm.dll",
    "crypt32.dll", "rpcrt4.dll", "setupapi.dll", "ws2_32.dll", "dwrite.dll",
    "dxgi.dll", "dxguid.dll", "imm32.dll", "combase.dll", "kernelbase.dll",
    "ntdll.dll", "bcrypt.dll", "userenv.dll", "profapi.dll", "powrprof.dll",
}


def main():
    if len(sys.argv) < 2:
        print("usage: pecheck.py <exe>")
        sys.exit(2)
    exe = sys.argv[1]
    if not os.path.isfile(exe):
        print(f"error: not found: {exe}")
        sys.exit(2)

    objdump = os.environ.get("OBJDUMP", "objdump")
    try:
        out = subprocess.run([objdump, "-p", exe], capture_output=True, text=True)
    except FileNotFoundError:
        print("error: objdump not found on PATH")
        sys.exit(3)

    text = out.stdout + out.stderr
    subsystem = None
    dlls = []
    for line in text.splitlines():
        s = line.strip()
        if s.lower().startswith("subsystem"):
            # e.g. "Subsystem   00000002 (Windows GUI)"
            try:
                val = int(s.split()[1], 16)
                subsystem = val
            except Exception:
                pass
        elif s.lower().startswith("dll name:"):
            dlls.append(s.split(":", 1)[1].strip())

    print(f"File: {exe}")
    print(f"Subsystem: {subsystem} "
          f"({SUBSYS.get(subsystem, 'unknown')})")
    print(f"Imported DLLs ({len(dlls)}):")
    non_system = [d for d in dlls if d.lower() not in SYSTEM_DLLS]
    for d in dlls:
        tag = "" if d.lower() in SYSTEM_DLLS else "  <-- NON-SYSTEM / RUNTIME DEP"
        print(f"   {d}{tag}")

    print()
    if subsystem == 3:
        print("WARNING: console subsystem -> --silent launch will open a console window.")
    elif subsystem == 2:
        print("OK: GUI subsystem -> silent launch stays windowless.")

    if non_system:
        print(f"FAIL: {len(non_system)} non-system DLL dependency(ies): {non_system}")
        sys.exit(1)
    else:
        print("OK: zero non-system (MinGW runtime) dependencies -> true single-file exe.")


if __name__ == "__main__":
    main()
