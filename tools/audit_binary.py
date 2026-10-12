"""Audit release size, OS imports and Linux glibc baseline; Python 3.11+."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def audit(binary, dumpbin=None):
    binary = Path(binary).resolve(strict=True)
    size = binary.stat().st_size
    if size > 15*1024*1024:
        raise ValueError("Binary exceeds 15 MiB hard limit")
    report = {"binary": binary.name, "bytes": size, "MiB": round(size/1024**2, 3), "within_10_MiB_target": size <= 10*1024**2,
              "sha256": hashlib.sha256(binary.read_bytes()).hexdigest()}
    if os.name == "nt":
        output = subprocess.check_output([dumpbin or "dumpbin", "/dependents", str(binary)], text=True)
        imports = re.findall(r"^\s+([A-Za-z0-9_.-]+\.dll)\s*$", output, re.M | re.I)
        allowed = {"kernel32.dll", "user32.dll", "gdi32.dll", "winmm.dll", "imm32.dll", "ole32.dll", "version.dll",
                   "advapi32.dll", "setupapi.dll", "shell32.dll", "ws2_32.dll", "crypt32.dll", "bcrypt.dll",
                   "secur32.dll", "wldap32.dll", "normaliz.dll", "ntdll.dll", "shlwapi.dll", "comdlg32.dll", "iphlpapi.dll",
                   "api-ms-win-core-synch-l1-2-0.dll"}
        if not imports or any(name.lower() not in allowed for name in imports):
            raise ValueError("Non-allowlisted Windows DLL import: " + repr(imports))
        report["os_libraries"] = imports
        report["windows_7_import_blocker"] = "api-ms-win-core-synch-l1-2-0.dll" in {name.lower() for name in imports}
    else:
        output = subprocess.check_output(["ldd", str(binary)], text=True)
        imports = re.findall(r"^\s*(\S+)\s+=>", output, re.M)
        allowed = re.compile(r"^(lib(c|m|dl|pthread|rt|stdc\+\+|gcc_s|X11|Xext|Xcursor|Xi|Xfixes|Xrandr|Xss|Xrender|xcb|Xau|Xdmcp|wayland-client|wayland-cursor|wayland-egl|xkbcommon|EGL|GL|GLX|GLdispatch|gbm|drm|decor-0)\.so)")
        if "not found" in output or not imports or any(not allowed.match(name) for name in imports):
            raise ValueError("Missing or non-OS Linux dependency: " + output)
        versions = subprocess.check_output(["readelf", "--version-info", str(binary)], text=True)
        glibc = sorted({tuple(map(int, v.split("."))) for v in re.findall(r"\bGLIBC_([0-9.]+)", versions)})
        if not glibc or glibc[-1] > (2, 35):
            raise ValueError("Binary exceeds Ubuntu 22.04 glibc 2.35 baseline")
        report.update(os_libraries=imports, maximum_glibc=".".join(map(str, glibc[-1])))
    return report, output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--dumpbin")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    report, imports = audit(args.binary, args.dumpbin)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    args.out.with_suffix(".imports.txt").write_text(imports, encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
