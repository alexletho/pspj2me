#!/usr/bin/env python3
"""Create a zip/jar from a directory (deflated)."""
import os, sys, zipfile
out, root = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    manifest = None
    if len(sys.argv) > 3:
        manifest = sys.argv[3]
        z.write(manifest, "META-INF/MANIFEST.MF")
    for d, _, files in os.walk(root):
        for f in sorted(files):
            p = os.path.join(d, f)
            arc = os.path.relpath(p, root)
            if arc == "META-INF/MANIFEST.MF" and manifest:
                continue
            z.write(p, arc)
