#!/bin/sh
# Run the EBOOT in PPSSPPHeadless with an autostart.txt config.
#   tests/ppsspp_run.sh <ppsspp-headless> <workdir> "<autostart lines separated by ;>"
set -e
PP=$1; W=$2; CFG=$3
G=$W/.ppsspp/PSP/GAME/PSPJ2ME
mkdir -p $G/games
cp EBOOT.PBP $G/
cp tests/midlets/jars/*.jar $G/games/ 2>/dev/null || true
cp tests/vmtest/vmtest.jar $G/ 2>/dev/null || true
rm -f $G/shot_*.ppm $G/pspj2me.log
echo "$CFG" | tr ';' '\n' > $G/autostart.txt
(cd $W && rm -f __testfailure.bmp && HOME=$W timeout 300 $PP --timeout=120 --screenshot=$W/dummy.png $G/EBOOT.PBP) > $W/ppsspp.out 2>&1 || true
cat $G/pspj2me.log
