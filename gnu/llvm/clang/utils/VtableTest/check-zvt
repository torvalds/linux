#!/bin/sh

N_STRUCTS=300

# Utility routine to "hand" check vtables.

let i=1;
while [ $i != $N_STRUCTS ]; do
  sed -n "/^__ZTV.*s$i:/,/\.[sg][el]/p" test-clang.s | grep -v '\.[sg][el]' >test-clang-ztv
  sed -n "/^__ZTV.*s$i:/,/\.[sg][el]/p" test-gcc.s | grep -v '\.[sg][el]' >test-gcc-ztv
  diff -U3 test-gcc-ztv test-clang-ztv
  if [ $? != 0 ]; then
     echo "FAIL: s$i vtable"
  else
     echo "PASS: s$i vtable"
  fi
  let i=i+1
done
