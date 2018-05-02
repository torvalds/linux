#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

export TESTNAME=test_libbpf

# Determine selftest success via shell exit code
exit_handler()
{
	if (( $? == 0 )); then
		echo "selftests: $TESTNAME [PASS]";
	else
		echo "$TESTNAME: failed at file $LAST_LOADED" 1>&2
		echo "selftests: $TESTNAME [FAILED]";
	fi
}

libbpf_open_file()
{
	LAST_LOADED=$1
	if [ -n "$VERBOSE" ]; then
	    ./test_libbpf_open $1
	else
	    ./test_libbpf_open --quiet $1
	fi
}

# Exit script immediately (well catched by trap handler) if any
# program/thing exits with a non-zero status.
set -e

# (Use 'trap -l' to list meaning of numbers)
trap exit_handler 0 2 3 6 9

libbpf_open_file test_l4lb.o

# TODO: fix libbpf to load noinline functions
# [warning] libbpf: incorrect bpf_call opcode
#libbpf_open_file test_l4lb_noinline.o

# TODO: fix test_xdp_meta.c to load with libbpf
# [warning] libbpf: test_xdp_meta.o doesn't provide kernel version
#libbpf_open_file test_xdp_meta.o

# TODO: fix libbpf to handle .eh_frame
# [warning] libbpf: relocation failed: no section(10)
#libbpf_open_file ../../../../samples/bpf/tracex3_kern.o

# Success
exit 0
