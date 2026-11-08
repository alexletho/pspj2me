# Builds small jars for the detection test into the directory given as argv[1].
import os, sys, struct, zipfile, zlib
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
def png(w, h):
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IEND', b'')
def jar(name, manifest='Manifest-Version: 1.0\nMIDlet-1: G, , G\n', files={}):
    with zipfile.ZipFile(os.path.join(out, name), 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('META-INF/MANIFEST.MF', manifest)
        for k, v in files.items(): z.writestr(k, v)
jar('plain.jar')
jar('Manifest.jar', 'MIDlet-1: G, , G\nNokia-MIDlet-Original-Display-Size: 176,208\n')
jar('Jadsize.jar')
open(os.path.join(out, 'Jadsize.jad'), 'w').write('MIDlet-Name: x\nMIDlet-Screen-Width: 128\nMIDlet-Screen-Height: 160\n')
jar('Images.jar', files={'splash.png': png(240, 320), 'menu.png': png(240, 320), 'title.png': png(176, 208), 'icon.png': png(16, 16)})
jar('ApiOne.jar', files={'a.class': b'\xca\xfe\xba\xbe....com/siemens/mp/game/Light....'})
jar('ApiTwo.jar', files={'a.class': b'\xca\xfe\xba\xbe....com/motorola/funlight/FunLight....'})
jar('ApiBoth.jar', files={'a.class': b'\xca\xfe\xba\xbe com/siemens/mp/game/Light com/nokia/mid/ui/FullCanvas'})
