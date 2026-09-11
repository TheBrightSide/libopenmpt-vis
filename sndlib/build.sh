#!/usr/bin/env bash

set -euo pipefail

LIBS=(libxmp sdl3)
RM=rm
AR=ar
CC=clang
CFLAGS="-g -O3 -Wall -Wextra $(pkg-config --cflags ${LIBS[@]})"

SRC=(snd_xmp.c snd_ring_buffer.c)
OBJ=()

# TODO: add sanitizer in other place

for f in ${SRC[@]}; do
  echo $f
  obj_filename=${f%.*}.o
  $CC -c -o $obj_filename $f $CFLAGS
  OBJ+=($obj_filename)
done

$AR rcs libsnd.a ${OBJ[@]}
$RM ${OBJ[@]}

