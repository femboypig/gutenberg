#!/usr/bin/env python3
import argparse
import hashlib
from pathlib import Path
import urllib.request

SAMPLES = {
    "randnum": (
        "https://raw.githubusercontent.com/agduncan94/Random-Number-Generator-Android/"
        "083631f6176aaefdc204e2955ebe8379f5bae50e/bin/RandomNumberGenerator.apk",
        "f65d9bfff7d9734151f26d8660ea4394862836b8d4f5b8b4363a0e13741d700f",
    ),
    "snake": (
        "https://raw.githubusercontent.com/peters/manifestparser/"
        "c2cb54cac1fd38fcb5034957cf03be506c2f5c92/tests/fixtures/Snake.apk",
        "30b06c1be93040a0e3414d32d63dbd7815b6489bb0b5d449cf5607ea20066113",
    ),
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("--sample", choices=SAMPLES, default="randnum")
    args = parser.parse_args()
    destination = args.destination
    url, checksum = SAMPLES[args.sample]
    with urllib.request.urlopen(url, timeout=30) as response:
        data = response.read(1024 * 1024)
    if hashlib.sha256(data).hexdigest() != checksum:
        raise SystemExit("external fixture checksum mismatch")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)


if __name__ == "__main__":
    main()
