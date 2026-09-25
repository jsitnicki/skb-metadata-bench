#!/bin/bash

set -o nounset

BPF_OBJ="$1"
OUT_FILE="$2"

./attach "$BPF_OBJ"
./bench.sh "$OUT_FILE"
