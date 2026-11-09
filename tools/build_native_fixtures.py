#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent.parent
    output = Path(sys.argv[1] if len(sys.argv) > 1 else root / "build" / "native").resolve()
    clang = shutil.which("clang")
    if not clang or not shutil.which("ld.lld"):
        raise SystemExit("native ELF fixtures require clang and ld.lld")
    for machine in ("aarch64", "x86_64"):
        directory = output / machine
        directory.mkdir(parents=True, exist_ok=True)
        for variant, packing in (("rela", "none"), ("packed", "android+relr")):
            destination = directory / f"libconformance-{variant}.so"
            subprocess.run([
                clang, f"--target={machine}-linux-android28", "-fuse-ld=lld",
                "-nostdlib", "-shared", "-fPIC", "-O2", "-fno-stack-protector",
                "-Wl,-z,max-page-size=16384", "-Wl,--hash-style=gnu",
                f"-Wl,--pack-dyn-relocs={packing}", f"-Wl,-soname,{destination.name}",
                str(root / "tests" / "native" / "library.c"), "-o", str(destination)
            ], check=True)
            print(destination)


if __name__ == "__main__":
    main()
