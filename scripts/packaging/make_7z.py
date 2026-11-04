"""Create / extract a .7z archive used by the self-extracting installer.

The bundled 7zSD.sfx module is from 7-Zip 9.20 and cannot read LZMA2, so the
installer payload is written with LZMA1. (Portable builds use plain zip.)

Usage:
  python make_7z.py write   <source_dir> <archive.7z>
  python make_7z.py extract <archive.7z> <target_dir>
"""
import os
import sys


def write(source_dir: str, archive: str) -> None:
    import py7zr

    source_dir = os.path.abspath(source_dir)
    filters = [{"id": py7zr.FILTER_LZMA, "preset": 5}]
    with py7zr.SevenZipFile(archive, "w", filters=filters) as handle:
        for root, _dirs, files in os.walk(source_dir):
            for name in files:
                full = os.path.join(root, name)
                relative = os.path.relpath(full, source_dir).replace(os.sep, "/")
                handle.write(full, arcname=relative)


def extract(archive: str, target_dir: str) -> None:
    import py7zr

    with py7zr.SevenZipFile(archive, "r") as handle:
        handle.extractall(path=target_dir)


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("write", "extract"):
        raise SystemExit(__doc__)
    if sys.argv[1] == "write":
        write(sys.argv[2], sys.argv[3])
    else:
        extract(sys.argv[2], sys.argv[3])
