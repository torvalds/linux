#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2022 Adam Sindelar (Meta) <adam@wowsignal.io>
#
# This is a test for mmap behavior with 5-level paging. This script wraps the
# real test to check that the kernel is configured to support at least 5
# pagetable levels.

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
orig_nr_hugepages=0

skip()
{
	echo "$1"
	exit $ksft_skip
}

check_supported_x86_64()
{
	local config="/proc/config.gz"
	[[ -f "${config}" ]] || config="/boot/config-$(uname -r)"
	[[ -f "${config}" ]] || skip "Cannot find kernel config in /proc or /boot"

	# gzip -dcfq automatically handles both compressed and plaintext input.
	# See man 1 gzip under '-f'.
	local pg_table_levels=$(gzip -dcfq "${config}" | grep PGTABLE_LEVELS | cut -d'=' -f 2)

	local cpu_supports_pl5=$(awk '/^flags/ {if (/la57/) {print 0;}
		else {print 1}; exit}' /proc/cpuinfo 2>/dev/null)

	if [[ "${pg_table_levels}" -lt 5 ]]; then
		skip "$0: PGTABLE_LEVELS=${pg_table_levels}, must be >= 5 to run this test"
	elif [[ "${cpu_supports_pl5}" -ne 0 ]]; then
		skip "$0: CPU does not have the necessary la57 flag to support page table level 5"
	fi
}

check_supported_ppc64()
{
	local config="/proc/config.gz"
	[[ -f "${config}" ]] || config="/boot/config-$(uname -r)"
	[[ -f "${config}" ]] || skip "Cannot find kernel config in /proc or /boot"

	local pg_table_levels=$(gzip -dcfq "${config}" | grep PGTABLE_LEVELS | cut -d'=' -f 2)
	if [[ "${pg_table_levels}" -lt 5 ]]; then
		skip "$0: PGTABLE_LEVELS=${pg_table_levels}, must be >= 5 to run this test"
	fi

	local mmu_support=$(grep -m1 "mmu" /proc/cpuinfo | awk '{print $3}')
	if [[ "$mmu_support" != "radix" ]]; then
		skip "$0: System does not use Radix MMU, required for 5-level paging"
	fi

	local hugepages_total=$(awk '/HugePages_Total/ {print $2}' /proc/meminfo)
	if [[ "${hugepages_total}" -eq 0 ]]; then
		skip "$0: HugePages are not enabled, required for some tests"
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
		"ppc64le"|"ppc64")
			check_supported_ppc64
		;;
		*)
			return 0
		;;
	esac
}

save_nr_hugepages()
{
	orig_nr_hugepages=$(cat /proc/sys/vm/nr_hugepages)
}

restore_nr_hugepages()
{
	echo "$orig_nr_hugepages" > /proc/sys/vm/nr_hugepages
}

setup_nr_hugepages()
{
	local needpgs=$1
	while read -r name size unit; do
		if [ "$name" = "HugePages_Free:" ]; then
			freepgs="$size"
			break
		fi
	done < /proc/meminfo
	if [ "$freepgs" -ge "$needpgs" ]; then
		return
	fi
	local hpgs=$((orig_nr_hugepages + needpgs))
	echo $hpgs > /proc/sys/vm/nr_hugepages

	local nr_hugepgs=$(cat /proc/sys/vm/nr_hugepages)
	if [ "$nr_hugepgs" != "$hpgs" ]; then
		restore_nr_hugepages
		skip "$0: no enough hugepages for testing"
	fi
}

check_test_requirements
save_nr_hugepages
# 4 keep_mapped pages, and one for tmp usage
setup_nr_hugepages 5
./va_high_addr_switch --run-hugetlb
restore_nr_hugepages
