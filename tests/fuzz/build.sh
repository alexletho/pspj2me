#!/bin/sh
# Builds the decoder fuzzer into build/fuzz. Uses ASan+UBSan when the compiler has them.
# The opencore-amr library gets ASan only: its fixed-point code shifts negative values on
# purpose (compiled with -fwrapv), which UBSan would report.
set -e
cd "$(dirname "$0")/../.."
OUT=build/fuzz-obj
mkdir -p $OUT
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
echo 'int main(void){return 0;}' > $OUT/t.c
gcc $SAN $OUT/t.c -o $OUT/t 2>/dev/null || SAN=""
ASAN=""; [ -n "$SAN" ] && ASAN="-fsanitize=address -fno-omit-frame-pointer"
AMR_DIR=src/audio/third_party/opencore-amr
B=$AMR_DIR/opencore/codecs_v2/audio/gsm_amr
AMR_INC="-I$AMR_DIR/oscl -I$B/amr_nb/dec/src -I$B/amr_nb/common/include -I$B/amr_nb/dec/include -I$B/common/dec/include"
for f in $(find $AMR_DIR -name '*.cpp'); do
  o=$OUT/$(basename $f .cpp).o
  [ -f $o ] && [ $o -nt $f ] && continue
  gcc -O1 -g $ASAN -x c -std=c99 -fwrapv -w $AMR_INC -c $f -o $o
done
gcc -O1 -g $SAN -std=gnu99 $AMR_INC -o build/fuzz tests/fuzz/fuzz.c src/gfx/png.c src/gfx/jpeg.c src/vm/zip.c \
    src/audio/mixer.c src/audio/mp3.c src/audio/amr.c $OUT/*.o -lm
