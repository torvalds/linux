#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate system call table for perf. Derived from
# s390 script.
#
# Copyright IBM Corp. 2017
# Author(s):  Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
# Changed by: Ravi Bangoria <ravi.bangoria@linux.vnet.ibm.com>

wordsize=$1
SYSCALL_TBL=$2

if ! test -r $SYSCALL_TBL; then
	echo "Could not read input file" >&2
	exit 1
fi

create_table()
{
	local wordsize=$1
	local max_nr nr abi sc discard
	max_nr=-1
	nr=0

	echo "static const char *const syscalltbl_powerpc_${wordsize}[] = {"
	while read nr abi sc discard; do
		if [ "$max_nr" -lt "$nr" ]; then
			printf '\t[%d] = "%s",\n' $nr $sc
			max_nr=$nr
		fi
	done
	echo '};'
	echo "#define SYSCALLTBL_POWERPC_${wordsize}_MAX_ID $max_nr"
}

grep -E "^[[:digit:]]+[[:space:]]+(common|spu|nospu|${wordsize})" $SYSCALL_TBL \
	|sort -k1 -n                                                           \
	|create_table ${wordsize}
