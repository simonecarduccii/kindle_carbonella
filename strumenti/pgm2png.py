#!/usr/bin/env python3
"""Converte in PNG le schermate .pgm salvate dal simulatore (serve Pillow: pip install pillow)."""
import pathlib
import sys

from PIL import Image

for folder in sys.argv[1:]:
    for pgm in sorted(pathlib.Path(folder).glob("*.pgm")):
        Image.open(pgm).save(pgm.with_suffix(".png"))
        pgm.unlink()
    print(f"schermate PNG in {folder}")
