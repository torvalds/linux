#!/bin/sh

N_STRUCTS=300

# Utility routine to "hand" check VTTs.

let i=1;
while [ $i != $N_STRUCTS ]; do
  sed -n "/^__ZTT.*s$i:/,/\.[sgm][elo]/p" test-clang.s |
    grep -v '\.[sgm][elo]' | sed -e 's/[()]//g' -e '/^$/d'  >test-clang-ztt
  sed -n "/^__ZTT.*s$i:/,/\.[sgm][elo]/p" test-gcc.s |
    grep -v '\.[sgm][elo]' | sed -e 's/[()]//g' -e 's/ + /+/'  >test-gcc-ztt
  diff -U3 test-gcc-ztt test-clang-ztt
  if [ $? != 0 ]; then
     echo "FAIL: s$i VTT"
  else
     echo "PASS: s$i VTT"
  fi
  let i=i+1
done
