#!/bin/sh
# Host test: builds logic.h + screens.h on the PC, checks them against data.json,
# and renders the screens to out/*.png. Needs g++ and Python 3 with Pillow.
# Pack first:  python3 ../tools/build_pack.py   (cards are firmware/data/cards)
set -e
cd "$(dirname "$0")"
OUT=${1:-out}
mkdir -p "$OUT"
# data.json: the published site file, or pass your own as the second argument
DATA=${2:-"$OUT/data.json"}
[ -n "$2" ] || curl -sfL https://raw.githubusercontent.com/Heladoo/Birdnet_kids/main/data.json -o "$DATA"
python3 fixture.py "$DATA" "$OUT/fixture.tsv"
g++ -std=c++17 -Wall -Wextra -O1 -o "$OUT/host_test" host_test.cpp
"$OUT/host_test" "$OUT/fixture.tsv" ../data/cards "$OUT"
python3 pbm2png.py "$OUT"
