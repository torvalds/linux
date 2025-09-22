#!/bin/sh
LIBFUZZER_SRC_DIR=$(dirname $0)
CXX="${CXX:-clang}"
for f in $LIBFUZZER_SRC_DIR/*.cpp; do
  $CXX -g -O2 -fno-omit-frame-pointer -std=c++17 $f -c &
done
wait
rm -f libFuzzer.a
ar r libFuzzer.a Fuzzer*.o
rm -f Fuzzer*.o

