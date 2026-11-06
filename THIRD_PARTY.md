# Third-party material

## DejaVu Sans (built-in text font)

`src/gfx/fontdata.c` contains bitmap glyphs rasterized by `tools/genfont.py` from the
DejaVu Sans fonts (https://dejavu-fonts.github.io/). DejaVu is derived from Bitstream Vera;
DejaVu changes are in the public domain.

```
Copyright (c) 2003 by Bitstream, Inc. All Rights Reserved.
Bitstream Vera is a trademark of Bitstream, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of the fonts accompanying this license ("Fonts") and associated
documentation files (the "Font Software"), to reproduce and distribute the
Font Software, including without limitation the rights to use, copy, merge,
publish, distribute, and/or sell copies of the Font Software, and to permit
persons to whom the Font Software is furnished to do so, subject to the
following conditions:

The above copyright and trademark notices and this permission notice shall
be included in all copies of one or more of the Font Software typefaces.

The Font Software may be modified, altered, or added to, and in particular
the designs of glyphs or characters in the Fonts may be modified and
additional glyphs or characters may be added to the Fonts, only if the fonts
are renamed to names not containing either the words "Bitstream" or the word
"Vera".

This License becomes null and void to the extent applicable to Fonts or Font
Software that has been modified and is distributed under the "Bitstream
Vera" names.

The Font Software may be sold as part of a larger software package but no
copy of one or more of the Font Software typefaces may be sold by itself.

THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF COPYRIGHT, PATENT,
TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL BITSTREAM OR THE GNOME
FOUNDATION BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, INCLUDING
ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM OTHER DEALINGS IN THE
FONT SOFTWARE.

Except as contained in this notice, the names of Gnome, the Gnome
Foundation, and Bitstream Inc., shall not be used in advertising or
otherwise to promote the sale, use or other dealings in this Font Software
without prior written authorization from the Gnome Foundation or Bitstream
Inc., respectively. For further information, contact: fonts at gnome dot
org.
```

## puff (DEFLATE decoder)

`src/vm/zip.c` contains an altered DEFLATE decoder modeled on **puff.c** by Mark Adler
(Copyright (C) 2002-2013 Mark Adler, zlib license). Full notice: [licenses/puff-zlib-LICENSE.txt](licenses/puff-zlib-LICENSE.txt).

## Libraries linked into EBOOT.PBP

The PSP binary is built with the [pspdev](https://github.com/pspdev/pspdev) toolchain and links:

* **pspsdk**: Copyright (c) 2005 adresd, Marcus R. Brown, James Forshaw, John Kelley,
  Jesper Svennevid. BSD license: [licenses/pspsdk-LICENSE.txt](licenses/pspsdk-LICENSE.txt).
* **newlib** (C library): various permissive licenses, see
  [licenses/newlib-COPYING.NEWLIB.txt](licenses/newlib-COPYING.NEWLIB.txt).
* **libgcc**: GCC Runtime Library Exception (no notice requirements for the binary).

These license files are also included in every release zip.

## Testing only (not linked, not distributed)

* [PPSSPP](https://github.com/hrydgard/ppsspp) (GPL-2.0+): runs the EBOOT in automated tests.

## No Sony or Oracle code

PSPJ2ME contains no code, firmware, keys or other material from Sony, Oracle/Sun or Nokia. The
Java class library is an independent implementation of the published J2ME API specifications.
The PSP build uses only the community-developed, open-source pspsdk.
