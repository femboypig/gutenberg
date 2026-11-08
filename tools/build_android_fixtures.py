#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys
import zipfile


def run(*arguments):
    subprocess.run([str(value) for value in arguments], check=True)


def version(path):
    return tuple(int(part) if part.isdigit() else 0 for part in path.name.replace("-", ".").split("."))


def main():
    root = Path(__file__).resolve().parent.parent
    output = Path(sys.argv[1] if len(sys.argv) > 1 else root / "build" / "android").resolve()
    sdk = Path(os.environ.get("ANDROID_HOME", os.environ.get("ANDROID_SDK_ROOT", "")))
    tools = max((sdk / "build-tools").iterdir(), key=version)
    platform = max((sdk / "platforms").glob("android-*"), key=version) / "android.jar"
    classes = output / "classes"
    generated = output / "generated"
    dex = output / "dex"
    for path in (classes, generated, dex):
        path.mkdir(parents=True, exist_ok=True)
    fixture = root / "tests" / "android"
    resources = output / "resources.zip"
    unsigned = output / "resources.apk"
    run(tools / "aapt2", "compile", "--dir", fixture / "res", "-o", resources)
    run(tools / "aapt2", "link", "-o", unsigned, "--manifest", fixture / "AndroidManifest.xml", "-I", platform,
        "--java", generated, "--min-sdk-version", "26", "--target-sdk-version", "28", resources)
    sources = sorted((fixture / "src").rglob("*.java")) + sorted(generated.rglob("*.java"))
    run("javac", "--release", "8", "-cp", platform, "-d", classes, *sources)
    run(tools / "d8", "--min-api", "26", "--lib", platform, "--output", dex, *sorted(classes.rglob("*.class")))
    apk = output / "conformance.apk"
    with zipfile.ZipFile(unsigned) as source, zipfile.ZipFile(apk, "w", zipfile.ZIP_DEFLATED) as target:
        for entry in source.infolist():
            target.writestr(entry.filename, source.read(entry.filename))
        for entry in sorted(dex.glob("*.dex")):
            target.write(entry, entry.name)
    print(apk)


if __name__ == "__main__":
    main()
