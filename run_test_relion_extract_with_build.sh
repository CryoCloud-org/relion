#!/bin/bash

# test_relion.sh - Script to test Relion preprocessing functionality, with re-build of source code

# rebuild after initial build
CWD=$(pwd)

cd /app/relion/build
make -j$(nproc)

cd $CWD
./test_relion_extract.sh