#!/bin/sh
# Host test suite: core VM test + MIDlet smoke tests (screenshots go to shots/).
set -e
cd "$(dirname "$0")/.."
DATA=$(mktemp -d)
mkdir -p shots tests/vmtest/out
javac -nowarn -source 8 -target 8 -Xlint:-options -bootclasspath build/classes -d tests/vmtest/out tests/vmtest/VmTest.java 2>/dev/null
python3 tools/mkjar.py tests/vmtest/vmtest.jar tests/vmtest/out
OUT=$(timeout 60 ./build/j2me_host --main VmTest tests/vmtest/vmtest.jar)
echo "$OUT" | tail -3
echo "$OUT" | grep -q "failed=0" || { echo "VM test FAILED"; exit 1; }
sh tests/midlets/build.sh >/dev/null 2>&1
for M in GfxTest GameTest UiTest SerialTest SwitchTest SoundTest; do
  PSPJ2ME_DATA=$DATA timeout 30 ./build/j2me_host --quit 2000 --final shots/$M.ppm \
     --keys "300:right,500:fire,700:down,900:fire" tests/midlets/jars/$M.jar > shots/$M.log 2>&1
  if grep -q "Uncaught exception\|FATAL\|missing native\|PAINT BEFORE" shots/$M.log; then echo "$M: FAILED"; cat shots/$M.log; exit 1; fi
  echo "$M: ok ($(grep -c . shots/$M.log) log lines)"
done
gcc -Wall -o build/keymap_test tests/keymap/keymap_test.c src/platform/keymap.c -Isrc/platform
./build/keymap_test > build/keymap_test.log || { cat build/keymap_test.log; echo "keymap test FAILED"; exit 1; }
tail -1 build/keymap_test.log
# misbehaving and broken games: each must end cleanly (no crash, no hang)
sh tests/robustness/build.sh >/dev/null 2>&1
for J in tests/robustness/jars/*.jar; do
  set +e
  PSPJ2ME_DATA=$DATA timeout 60 ./build/j2me_host --quit 3000 "$J" > build/robust.log 2>&1
  RC=$?
  set -e
  if [ $RC -ge 124 ]; then echo "robustness $(basename $J): FAILED (exit $RC)"; tail -20 build/robust.log; exit 1; fi
done
echo "robustness: $(ls tests/robustness/jars/*.jar | wc -l) broken/misbehaving games handled"
# short fuzz pass over the decoders (sanitizers when the compiler has them)
FZ="tests/fuzz/fuzz.c src/gfx/png.c src/gfx/jpeg.c src/vm/zip.c src/audio/mixer.c -lm"
gcc -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=gnu99 -o build/fuzz $FZ 2>/dev/null || gcc -O1 -std=gnu99 -o build/fuzz $FZ
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 600 ./build/fuzz 300 > build/fuzz.log 2>&1 || { tail -30 build/fuzz.log; echo "fuzz FAILED"; exit 1; }
tail -1 build/fuzz.log
rm -rf "$DATA"
echo "all host tests passed"
