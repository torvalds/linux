#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -e

ret=0

do_splice()
{
	filename="$1"
	bytes="$2"
	expected="$3"

	out=$(./splice_read "$filename" "$bytes" | cat)
	if [ "$out" = "$expected" ] ; then
		echo "ok: $filename $bytes"
	else
		echo "FAIL: $filename $bytes"
		ret=1
	fi
}

test_splice()
{
	filename="$1"

	full=$(cat "$filename")
	two=$(echo "$full" | grep -m1 . | cut -c-2)

	# Make sure full splice has the same contents as a standard read.
	do_splice "$filename" 4096 "$full"

	# Make sure a partial splice see the first two characters.
	do_splice "$filename" 2 "$two"
}

# proc_single_open(), seq_read()
test_splice /proc/$$/limits
# special open, seq_read()
test_splice /proc/$$/comm

# proc_handler, proc_dointvec_minmax
test_splice /proc/sys/fs/nr_open
# proc_handler, proc_dostring
test_splice /proc/sys/kernel/modprobe
# proc_handler, special read
test_splice /proc/sys/kernel/version

if ! [ -d /sys/module/test_module/sections ] ; then
	modprobe test_module
fi
# kernfs, attr
test_splice /sys/module/test_module/coresize
# kernfs, binattr
test_splice /sys/module/test_module/sections/.init.text

exit $ret
