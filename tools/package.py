#!/usr/bin/env python3
"""Builds the release package PSPJ2ME-<version>.zip from EBOOT.PBP.

Layout inside the zip (unzip to the root of the memory stick):
  PSP/GAME/PSPJ2ME/EBOOT.PBP
  PSP/GAME/PSPJ2ME/games/PUT_JAR_FILES_HERE.txt
  README.md, LICENSE, THIRD_PARTY.md
"""
import os
import sys
import zipfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
version = open(os.path.join(root, "VERSION")).read().strip()
out = os.path.join(root, f"PSPJ2ME-{version}.zip")
eboot = os.path.join(root, "EBOOT.PBP")
if not os.path.exists(eboot):
    sys.exit("EBOOT.PBP not found: run 'make -f Makefile.psp' first")

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(eboot, "PSP/GAME/PSPJ2ME/EBOOT.PBP")
    z.writestr("PSP/GAME/PSPJ2ME/games/PUT_JAR_FILES_HERE.txt",
               "Copy your J2ME games (.jar, optionally with a matching .jad) into this folder.\r\n")
    for name in ("README.md", "LICENSE", "THIRD_PARTY.md"):
        z.write(os.path.join(root, name), name)
    for name in sorted(os.listdir(os.path.join(root, "licenses"))):
        z.write(os.path.join(root, "licenses", name), "licenses/" + name)
print(f"Built {os.path.basename(out)}")
