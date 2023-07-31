#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2022 Adam Sindelar (Meta) <adam@wowsignal.io>
#
# This is a test for mmap behavior with 5-level paging. This script wraps the
# real test to check that the kernel is configured to support at least 5
# pagetable levels.

# 1 means the test failed
exitcode=1

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

fail()
{
	echo "$1"
	exit $exitcode
}

check_supported_x86_64()
{
	local config="/proc/config.gz"
	[[ -f "${config}" ]] || config="/boot/config-$(uname -r)"
	[[ -f "${config}" ]] || fail "Cannot find kernel config in /proc or /boot"

	# gzip -dcfq automatically handles both compressed and plaintext input.
	# See man 1 gzip under '-f'.
	local pg_table_levels=$(gzip -dcfq "${config}" | grep PGTABLE_LEVELS | cut -d'=' -f 2)

	if [[ "${pg_table_levels}" -lt 5 ]]; then
		echo "$0: PGTABLE_LEVELS=${pg_table_levels}, must be >= 5 to run this test"
		exit $ksft_skip
	fi
}

check_test_requirements()
{
	# The test supports x86_64 and powerpc64. We currently have no useful
	# eligibility check for powerpc64, and the test itself will reject other
	# architectures.
	case `uname -m` in
		"x86_64")
			check_supported_x86_64
		;;
		*)
			return 0
		;;
	esac
}

check_test_requirements
./va_high_addr_switch

# In order to run hugetlb testcases, "--run-hugetlb" must be appended
# to the binary.
./va_high_addr_switch --run-hugetlb
