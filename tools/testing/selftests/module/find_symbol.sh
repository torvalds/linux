#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later OR copyleft-next-0.3.1
# Copyright (C) 2023 Luis Chamberlain <mcgrof@kernel.org>
#
# This is a stress test script for kallsyms through find_symbol()

set -e

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

test_reqs()
{
	if ! which modprobe 2> /dev/null > /dev/null; then
		echo "$0: You need modprobe installed" >&2
		exit $ksft_skip
	fi

	if ! which kmod 2> /dev/null > /dev/null; then
		echo "$0: You need kmod installed" >&2
		exit $ksft_skip
	fi

	if ! which perf 2> /dev/null > /dev/null; then
		echo "$0: You need perf installed" >&2
		exit $ksft_skip
	fi

	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi
}

load_mod()
{
	local STATS="-e duration_time"
	STATS="$STATS -e user_time"
	STATS="$STATS -e system_time"
	STATS="$STATS -e page-faults"
	local MOD=$1

	local ARCH="$(uname -m)"
	case "${ARCH}" in
	x86_64)
		perf stat $STATS $MODPROBE $MOD
		;;
	*)
		time $MODPROBE $MOD
		exit 1
		;;
	esac
}

remove_all()
{
	$MODPROBE -r test_kallsyms_b
	for i in a b c d; do
		$MODPROBE -r test_kallsyms_$i
	done
}
test_reqs

MODPROBE=$(</proc/sys/kernel/modprobe)

remove_all
load_mod test_kallsyms_b
remove_all

# Now pollute the namespace
$MODPROBE test_kallsyms_c
load_mod test_kallsyms_b

# Now pollute the namespace with twice the number of symbols than the last time
remove_all
$MODPROBE test_kallsyms_c
$MODPROBE test_kallsyms_d
load_mod test_kallsyms_b

exit 0
