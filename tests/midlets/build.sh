#!/bin/sh
# Builds the test MIDlets into individual jars (needs ../../build/classes from the host build).
set -e
cd "$(dirname "$0")"
rm -rf out && mkdir -p out/classes jars
javac -nowarn -source 8 -target 8 -Xlint:-options -bootclasspath ../../build/classes -d out/classes src/*.java
for M in GfxTest GameTest UiTest SerialTest SwitchTest SoundTest Bench; do
  rm -rf out/$M && mkdir -p out/$M/META-INF
  cp out/classes/$M*.class out/$M/
  cp res/* out/$M/
  printf "Manifest-Version: 1.0\nMIDlet-Name: $M\nMIDlet-Vendor: PSPJ2ME\nMIDlet-Version: 1.0\nMIDlet-1: $M, , $M\nMicroEdition-Configuration: CLDC-1.1\nMicroEdition-Profile: MIDP-2.0\n" > out/$M/META-INF/MANIFEST.MF
  python3 ../../tools/mkjar.py jars/$M.jar out/$M
done
ls -la jars
