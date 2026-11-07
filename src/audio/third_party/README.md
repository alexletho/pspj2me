# Third-party audio decoders

* `minimp3/`: [minimp3](https://github.com/lieff/minimp3) by lieff, public domain (CC0).
  Unmodified copy of `minimp3.h`; commit in `minimp3/VERSION.txt`. Used by `src/audio/mp3.c`.
* `opencore-amr/`: the AMR-NB **decoder** from
  [opencore-amr](https://sourceforge.net/projects/opencore-amr/) (via the
  BelledonneCommunications/opencore-amr mirror), Apache License 2.0, see `LICENSE` and `NOTICE`.
  Unmodified files; only the decoder sources and the headers they need are included (the
  encoder, the C++ wrappers and the build system are left out). Commit in
  `opencore-amr/VERSION.txt`. The `.cpp` files are C code and are compiled as C (`amr.mk`).
  Used by `src/audio/amr.c`.
