#!/usr/bin/env bash
# Assemble a Thumb-2 patch into a rknano_fw.py patch line.
# The .S file must contain a line:  @ patch <MOD> <EXECADDR>
# Code is linked at EXECADDR, so `bl some_addr` to absolute addresses works
# (define them with .equ, see patches/fw.inc).
# Usage: tools/asm2patch.sh patches/foo.S out/gen/foo.patch
set -euo pipefail
src=$1 out=$2
read -r mod addr < <(sed -nE 's/^@[[:space:]]*patch[[:space:]]+([0-9]+)[[:space:]]+(0x)?([0-9a-fA-F]+).*/\1 \3/p' "$src" | head -1) \
  || { echo "$src: missing '@ patch <MOD> <EXECADDR>' line" >&2; exit 1; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
arm-none-eabi-gcc -c -mthumb -mcpu=cortex-m3 -x assembler-with-cpp -I"$(dirname "$src")" -Ipatches -Iout/gen -DPATCH_BASE=0x$addr "$src" -o "$tmp/p.o"
arm-none-eabi-ld -Ttext=0x$addr -e 0x$addr -o "$tmp/p.elf" "$tmp/p.o"
arm-none-eabi-objcopy -O binary -j .text "$tmp/p.elf" "$tmp/p.bin"
mkdir -p "$(dirname "$out")"
hex=$(python3 -c 'import sys;print(open(sys.argv[1],"rb").read().hex())' "$tmp/p.bin")
[ -n "$hex" ] || { echo "$src: empty patch" >&2; exit 1; }
printf '# generated from %s\n%s:%s:%s\n' "$src" "$mod" "$addr" "$hex" > "$out"
arm-none-eabi-objdump -d "$tmp/p.elf" | sed -n '/<.*>:/,$p'
