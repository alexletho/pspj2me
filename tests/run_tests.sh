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
./build/keymap_test | tail -1
rm -rf "$DATA"
echo "all host tests passed"
