#!/bin/sh

N_STRUCTS=300

# Utility routine to "hand" check type infos.

let i=1;
while [ $i != $N_STRUCTS ]; do
  sed -n "/^__ZTI.*s$i:/,/\.[sg][el]/p" test-clang.s |
    grep -v '\.[sg][el]' | sed 's/(\([0-9][0-9]*\))/\1/' >test-clang-zti
  sed -n "/^__ZTI.*s$i:/,/\.[sg][el]/p" test-gcc.s |
    grep -v '\.[sg][el]' | sed 's/(\([0-9][0-9]*\))/\1/' >test-gcc-zti
  diff -U3 test-gcc-zti test-clang-zti
  if [ $? != 0 ]; then
     echo "FAIL: s$i type info"
  else
     echo "PASS: s$i type info"
  fi
  let i=i+1
done
