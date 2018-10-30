#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

FILES='
arch/x86/lib/insn.c
arch/x86/lib/inat.c
arch/x86/lib/x86-opcode-map.txt
arch/x86/tools/gen-insn-attr-x86.awk
arch/x86/include/asm/insn.h
arch/x86/include/asm/inat.h
arch/x86/include/asm/inat_types.h
arch/x86/include/asm/orc_types.h
'

check()
{
	local file=$1

	diff $file ../../$file > /dev/null ||
		echo "Warning: synced file at 'tools/objtool/$file' differs from latest kernel version at '$file'"
}

if [ ! -d ../../kernel ] || [ ! -d ../../tools ] || [ ! -d ../objtool ]; then
	exit 0
fi

for i in $FILES; do
  check $i
done
