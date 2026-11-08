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
# phone key profiles: what a game receives for up / soft1 / fire / soft2 / right
KEYS="300:up,400:soft1,500:fire,600:soft2,700:right"
for P in "1:-1 -6 -5 -7 -4" "2:-59 -1 -26 -4 -62" "3:-1 -21 -20 -22 -5" "4:-1 -202 -5 -203 -4"; do
  N=${P%%:*}; WANT=${P#*:}
  GOT=$(timeout 30 ./build/j2me_host --phone $N --quit 1500 --keys "$KEYS" tests/midlets/jars/KeyTest.jar 2>&1 | grep "KeyTest key" | awk '{print $3}' | tr '\n' ' ' | sed 's/ $//')
  [ "$GOT" = "$WANT" ] || { echo "KeyTest phone $N: got '$GOT', expected '$WANT'"; exit 1; }
done
echo "KeyTest: key codes of 4 phone types ok"
# game build detection (screen size, phone type)
python3 tests/detect/make_jars.py build/detect
gcc -Wall -O1 -Isrc/platform -o build/detect_test tests/detect/detect_test.c src/platform/detect.c src/vm/zip.c
./build/detect_test build/detect > build/detect_test.log || { cat build/detect_test.log; echo "detect test FAILED"; exit 1; }
tail -1 build/detect_test.log
# game saves: the second start must find what the first one saved
RMSDATA=$(mktemp -d)
for P in 1 2; do
  PSPJ2ME_DATA=$RMSDATA timeout 60 ./build/j2me_host --quit 15000 tests/midlets/jars/RmsTest.jar > shots/RmsTest$P.log 2>&1 || true
  grep -q "RmsTest: PASS $P" shots/RmsTest$P.log || { grep "RmsTest" shots/RmsTest$P.log; echo "RmsTest run $P FAILED"; exit 1; }
done
rm -rf "$RMSDATA"
echo "RmsTest: $(cat shots/RmsTest1.log shots/RmsTest2.log | grep -c 'RmsTest ok') checks passed over two starts"
# MMAPI / Nokia Sound behaviour (pause/resume, times, loops, controls, eviction)
PSPJ2ME_DATA=$DATA timeout 60 ./build/j2me_host --quit 20000 tests/midlets/jars/AudioTest.jar > shots/AudioTest.log 2>&1 || true
grep -q "AudioTest: PASS" shots/AudioTest.log || { grep "AudioTest" shots/AudioTest.log; echo "AudioTest FAILED"; exit 1; }
echo "AudioTest: $(grep -c 'AudioTest ok' shots/AudioTest.log) checks passed"
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
  # the explanation the player gets
  case $(basename $J .jar) in
    Needs3D) WANT="needs 3D graphics" ;;
    NoClass) WANT="main class does.not.Exist is missing" ;;
    RandomBytes|Empty|Truncated) WANT="damaged or is not a .jar" ;;
    NoMidletEntry) WANT="names no MIDlet" ;;
    OutOfMemory) WANT="ran out of memory" ;;
    Recursion) WANT="endless recursion" ;;
    CtorThrows) WANT="crashed while loading" ;;
    StartThrows) WANT="crashed while starting" ;;
    CrashLater) WANT="crashed (NullPointerException)" ;;
    *) WANT="" ;;
  esac
  if [ -n "$WANT" ] && ! grep "PROBLEM:" build/robust.log | grep -q "$WANT"; then
    echo "robustness $(basename $J): expected a message containing '$WANT'"; grep "PROBLEM" build/robust.log; exit 1
  fi
done
echo "robustness: $(ls tests/robustness/jars/*.jar | wc -l) broken/misbehaving games handled"
# short fuzz pass over the decoders (sanitizers when the compiler has them)
sh tests/fuzz/build.sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 600 ./build/fuzz 300 > build/fuzz.log 2>&1 || { tail -30 build/fuzz.log; echo "fuzz FAILED"; exit 1; }
tail -1 build/fuzz.log
rm -rf "$DATA"
echo "all host tests passed"
