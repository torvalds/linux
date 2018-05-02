#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate system call table for perf
#
# Copyright IBM Corp. 2017, 2018
# Author(s):  Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
#

SYSCALL_TBL=$1

if ! test -r $SYSCALL_TBL; then
	echo "Could not read input file" >&2
	exit 1
fi

create_table()
{
	local max_nr nr abi sc discard

	echo 'static const char *syscalltbl_s390_64[] = {'
	while read nr abi sc discard; do
		printf '\t[%d] = "%s",\n' $nr $sc
		max_nr=$nr
	done
	echo '};'
	echo "#define SYSCALLTBL_S390_64_MAX_ID $max_nr"
}

grep -E "^[[:digit:]]+[[:space:]]+(common|64)" $SYSCALL_TBL	\
	|sort -k1 -n					\
	|create_table
