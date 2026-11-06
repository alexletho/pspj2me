#!/usr/bin/env python3
"""bin2c.py <input> <output.c> <symbol>: embed a binary file as a C array."""
import sys
data = open(sys.argv[1], "rb").read()
sym = sys.argv[3]
with open(sys.argv[2], "w") as f:
    f.write(f"/* generated from {sys.argv[1]} */\n")
    f.write(f"const unsigned int {sym}Size = {len(data)};\n")
    f.write(f"const unsigned char {sym}[{len(data)}] __attribute__((aligned(16))) = {{\n")
    for i in range(0, len(data), 32):
        f.write(",".join(str(b) for b in data[i:i+32]) + ",\n")
    f.write("};\n")
