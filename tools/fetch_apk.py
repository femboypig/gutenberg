#!/usr/bin/env python3
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import re
import time
import urllib.parse
import urllib.error
import urllib.request
import zipfile


def fetch(url, destination, checksum=None, limit=256 * 1024 * 1024):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme not in ("http", "https"):
        raise ValueError("APK URL must use HTTP or HTTPS")
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_name(destination.name + ".partial-" + hashlib.sha256(url.encode()).hexdigest()[:12])
    total = None
    for attempt in range(4):
        offset = partial.stat().st_size if partial.exists() else 0
        headers = {"User-Agent": "Gutenberg-compatibility", "Accept-Encoding": "identity"}
        if offset:
            headers["Range"] = f"bytes={offset}-"
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
                if response.status == 206:
                    match = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", response.headers.get("Content-Range", ""))
                    if not match or int(match[1]) != offset:
                        raise ValueError("server returned an invalid APK byte range")
                    declared = int(match[3])
                    if int(match[2]) < offset or int(match[2]) >= declared:
                        raise ValueError("server returned an invalid APK range length")
                elif response.status == 200:
                    offset = 0
                    declared = int(response.headers["Content-Length"]) if "Content-Length" in response.headers else None
                else:
                    raise ValueError(f"unexpected APK download status {response.status}")
                if declared is not None and (declared > limit or (offset and total not in (None, declared))):
                    raise ValueError("APK size exceeds its limit or changed while downloading")
                total = declared
                with partial.open("ab" if offset else "wb") as output:
                    while block := response.read(256 * 1024):
                        if len(block) > limit - output.tell():
                            raise ValueError("APK download size limit exceeded")
                        output.write(block)
                if total is not None and partial.stat().st_size != total:
                    raise OSError("APK download ended before its declared size")
            break
        except (OSError, urllib.error.URLError, http.client.IncompleteRead):
            if attempt == 3:
                raise
            time.sleep(min(attempt + 1, 3))
    with partial.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    if checksum and digest != checksum.lower():
        partial.unlink()
        raise ValueError("APK checksum does not match the requested build")
    if not zipfile.is_zipfile(partial):
        partial.unlink()
        raise ValueError("downloaded file is not an APK archive")
    partial.replace(destination)
    return {"source": urllib.parse.urlunsplit(parsed._replace(query="", fragment="")),
            "sha256": digest, "bytes": destination.stat().st_size}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url")
    parser.add_argument("destination", type=Path)
    parser.add_argument("--sha256")
    args = parser.parse_args()
    metadata = fetch(args.url, args.destination, args.sha256)
    args.destination.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata))


if __name__ == "__main__":
    main()
