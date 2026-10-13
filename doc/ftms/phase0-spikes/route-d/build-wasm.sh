#!/bin/bash
# usage: build-wasm.sh  (run from anywhere)
S=/tmp/claude-0/-home-user/2934c9fe-a343-5108-8d9a-4c6da189a80a/scratchpad
source $S/emsdk/emsdk_env.sh >/dev/null 2>&1
export EMCC_LOCAL_PORTS="sdl2=$S/dl/sdl/SDL2-2.32.10"
cd $S/ot-wasm
mkdir -p $S/web
SRCS=$(ls src/*.c)
emcc -std=gnu99 -O2 -DNDEBUG -DTARGET_UNIX -DTYRIAN_DIR='"/tyrian"' -sUSE_SDL=2 \
  $SRCS -o $S/web/index.html --shell-file web/shell.html --pre-js web/persist-pre.js -lidbfs.js -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS \
  -sASYNCIFY -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB \
  --preload-file $S/data/tyrian21@/tyrian -sSTACK_SIZE=1MB -lm "$@"
