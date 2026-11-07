#!/bin/sh
# Builds one jar per misbehaving MIDlet, plus broken jars for the game-list tests.
set -e
cd "$(dirname "$0")"
rm -rf out jars && mkdir -p out/classes jars
javac -nowarn -source 8 -target 8 -Xlint:-options -bootclasspath ../../build/classes -d out/classes src/Bad.java stub/BtUser.java stub/javax/microedition/m3g/Graphics3D.java
rm -r out/classes/BtUser.class out/classes/javax
for M in Needs3D CrashLater HarmlessError CtorThrows StartThrows Recursion OutOfMemory MissingApi InstantExit SysExit Hang BadRms BadImages; do
  rm -rf out/$M && mkdir -p out/$M/META-INF
  cp out/classes/*.class out/$M/
  printf "Manifest-Version: 1.0\nMIDlet-Name: Bad$M\nMIDlet-Vendor: Tests\nMIDlet-Version: 1.0\nMIDlet-1: $M, , Bad\$$M\n" > out/$M/META-INF/MANIFEST.MF
  python3 ../../tools/mkjar.py jars/$M.jar out/$M
done
# wrong MIDlet class / missing class
mkdir -p out/WrongClass/META-INF out/NoClass/META-INF
cp out/classes/*.class out/WrongClass/
printf "Manifest-Version: 1.0\nMIDlet-Name: WrongClass\nMIDlet-1: Wrong, , Bad\$NotAMidlet\n" > out/WrongClass/META-INF/MANIFEST.MF
python3 ../../tools/mkjar.py jars/WrongClass.jar out/WrongClass
printf "Manifest-Version: 1.0\nMIDlet-Name: NoClass\nMIDlet-1: NoClass, , does.not.Exist\n" > out/NoClass/META-INF/MANIFEST.MF
python3 ../../tools/mkjar.py jars/NoClass.jar out/NoClass
python3 - <<'PY'
import os, zipfile, random
random.seed(1)
open("jars/RandomBytes.jar", "wb").write(bytes(random.getrandbits(8) for _ in range(5000)))
open("jars/Empty.jar", "wb").close()
with zipfile.ZipFile("jars/NoManifest.jar", "w") as z: z.writestr("a.txt", "hello")
with zipfile.ZipFile("jars/NoMidletEntry.jar", "w") as z: z.writestr("META-INF/MANIFEST.MF", "Manifest-Version: 1.0\nMIDlet-Name: NoEntry\n")
# truncated real jar
data = open("jars/BadRms.jar", "rb").read()
open("jars/Truncated.jar", "wb").write(data[: len(data) // 2])
PY
ls jars
