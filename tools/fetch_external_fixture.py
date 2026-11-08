#!/usr/bin/env python3
import hashlib
from pathlib import Path
import sys
import urllib.request

URL = (
    "https://raw.githubusercontent.com/agduncan94/Random-Number-Generator-Android/"
    "083631f6176aaefdc204e2955ebe8379f5bae50e/bin/RandomNumberGenerator.apk"
)
SHA256 = "f65d9bfff7d9734151f26d8660ea4394862836b8d4f5b8b4363a0e13741d700f"


def main():
    destination = Path(sys.argv[1])
    with urllib.request.urlopen(URL, timeout=30) as response:
        data = response.read(1024 * 1024)
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise SystemExit("external fixture checksum mismatch")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)


if __name__ == "__main__":
    main()
