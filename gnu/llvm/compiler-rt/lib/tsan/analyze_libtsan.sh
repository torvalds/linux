#!/usr/bin/env bash
#
# Script that prints information about generated code in TSan runtime.

set -e
set -u

if [[ "$#" != 1 ]]; then
  echo "Usage: $0 /path/to/binary/built/with/tsan"
  exit 1
fi

get_asm() {
  grep __tsan_$1.: -A 10000 ${OBJDUMP_CONTENTS} | \
    awk "/[^:]$/ {print;} />:/ {c++; if (c == 2) {exit}}"
}

list="write1 \
      write2 \
      write4 \
      write8 \
      read1 \
      read2 \
      read4 \
      read8 \
      func_entry \
      func_exit"

BIN=$1
OUTPUT_DIR=$(mktemp -t -d analyze_libtsan_out.XXXXXXXX)
OBJDUMP_CONTENTS=${OUTPUT_DIR}/libtsan_objdump
NM_CONTENTS=${OUTPUT_DIR}/libtsan_nm

objdump -d $BIN  > ${OBJDUMP_CONTENTS}
nm -S $BIN | grep "__tsan_" > ${NM_CONTENTS}

for f in $list; do
  file=${OUTPUT_DIR}/asm_$f.s
  get_asm $f > $file
  tot=$(wc -l < $file)
  size=$(grep __tsan_$f$ ${NM_CONTENTS} | awk --non-decimal-data '{print ("0x"$2)+0}')
  rsp=$(grep '(%rsp)' $file | wc -l)
  push=$(grep 'push' $file | wc -l)
  pop=$(grep 'pop' $file | wc -l)
  call=$(grep 'call' $file | wc -l)
  load=$(egrep 'mov .*\,.*\(.*\)|cmp .*\,.*\(.*\)' $file | wc -l)
  store=$(egrep 'mov .*\(.*\),' $file | wc -l)
  mov=$(grep 'mov' $file | wc -l)
  lea=$(grep 'lea' $file | wc -l)
  sh=$(grep 'shr\|shl' $file | wc -l)
  cmp=$(grep 'cmp\|test' $file | wc -l)
  printf "%10s tot %3d; size %4d; rsp %d; push %d; pop %d; call %d; load %2d; store %2d; sh %3d; mov %3d; lea %3d; cmp %3d\n" \
    $f $tot $size $rsp $push $pop $call $load $store $sh $mov $lea $cmp;
done
