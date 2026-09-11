#!/usr/bin/env sh

set -xeuo pipefail

cd sndlib
./build.sh
cd ..

odin build .
