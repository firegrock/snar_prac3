#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests/out
cc -std=c11 -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter -Icomponents/lab_core/include components/lab_core/core.c components/lab_core/wire.c tests/native.c -lm -pthread -o tests/out/native
tests/out/native
if command -v cmake >/dev/null 2>&1; then
  if cmake -S . -B tests/out/invalid-id -DLAB_DEVICE_ID=5 > tests/out/invalid-id.log 2>&1; then
    echo 'FAIL invalid LAB_DEVICE_ID accepted'; exit 1
  else
    grep -q 'LAB_DEVICE_ID must be 1..4' tests/out/invalid-id.log
    echo 'PASS CMake rejects LAB_DEVICE_ID=5'
  fi
else echo 'NOT RUN CMake invalid id: cmake unavailable'; fi
if [ -x .venv/bin/python ]; then default_python=.venv/bin/python; else default_python=python3; fi
PYTHONPATH=host "${PYTHON:-$default_python}" -m unittest discover -s tests -p 'test_*.py' -v
if command -v node >/dev/null 2>&1; then
  node tests/web_codec.js
elif [ -x /System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc ]; then
  /System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc tests/web_codec_jsc.js
  /System/Library/Frameworks/JavaScriptCore.framework/Versions/A/Helpers/jsc tests/web_connection_jsc.js
else echo 'NOT RUN JS codec: node/JavaScriptCore unavailable'; fi
if command -v octave >/dev/null 2>&1; then octave --no-gui --quiet --eval "addpath('octave/reference'); test_fixture('tests/out/mahony_fixture.csv');"; else echo 'NOT RUN C/Octave comparison: octave unavailable'; fi
