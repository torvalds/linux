#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

if [ -z "$SRCARCH" ]; then
	echo 'sync-check.sh: error: missing $SRCARCH environment variable' >&2
	exit 1
fi

FILES="include/linux/objtool.h"

if [ "$SRCARCH" = "x86" ]; then
FILES="$FILES
arch/x86/include/asm/inat_types.h
arch/x86/include/asm/orc_types.h
arch/x86/include/asm/emulate_prefix.h
arch/x86/lib/x86-opcode-map.txt
arch/x86/tools/gen-insn-attr-x86.awk
include/linux/static_call_types.h
arch/x86/include/asm/inat.h     -I '^#include [\"<]\(asm/\)*inat_types.h[\">]'
arch/x86/include/asm/insn.h     -I '^#include [\"<]\(asm/\)*inat.h[\">]'
arch/x86/lib/inat.c             -I '^#include [\"<]\(../include/\)*asm/insn.h[\">]'
arch/x86/lib/insn.c             -I '^#include [\"<]\(../include/\)*asm/in\(at\|sn\).h[\">]' -I '^#include [\"<]\(../include/\)*asm/emulate_prefix.h[\">]'
"
fi

check_2 () {
  file1=$1
  file2=$2

  shift
  shift

  cmd="diff $* $file1 $file2 > /dev/null"

  test -f $file2 && {
    eval $cmd || {
      echo "Warning: Kernel ABI header at '$file1' differs from latest version at '$file2'" >&2
      echo diff -u $file1 $file2
    }
  }
}

check () {
  file=$1

  shift

  check_2 tools/$file $file $*
}

if [ ! -d ../../kernel ] || [ ! -d ../../tools ] || [ ! -d ../objtool ]; then
	exit 0
fi

cd ../..

while read -r file_entry; do
    if [ -z "$file_entry" ]; then
	continue
    fi

    check $file_entry
done <<EOF
$FILES
EOF
