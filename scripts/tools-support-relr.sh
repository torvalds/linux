#!/bin/sh -eu
# SPDX-License-Identifier: GPL-2.0

tmp_file=$(mktemp)
trap "rm -f $tmp_file.o $tmp_file $tmp_file.bin" EXIT

cat << "END" | $CC -c -x c - -o $tmp_file.o >/dev/null 2>&1
void *p = &p;
END

# ld.lld before 15 did not support -z pack-relative-relocs.
if ! $LD $tmp_file.o -shared -Bsymbolic --pack-dyn-relocs=relr -o $tmp_file 2>/dev/null; then
	$LD $tmp_file.o -shared -Bsymbolic -z pack-relative-relocs -o $tmp_file 2>&1 |
		grep -q pack-relative-relocs && exit 1
fi

# Despite printing an error message, GNU nm still exits with exit code 0 if it
# sees a relr section. So we need to check that nothing is printed to stderr.
test -z "$($NM $tmp_file 2>&1 >/dev/null)"

$OBJCOPY -O binary $tmp_file $tmp_file.bin
