#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Please run as root

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

count_pass=0
count_fail=0
count_skip=0
exitcode=0

usage() {
	cat <<EOF
usage: ${BASH_SOURCE[0]:-$0} [ -h | -t "<categories>"]
  -t: specify specific categories to tests to run
  -h: display this message

The default behavior is to run all tests.

Alternatively, specific groups tests can be run by passing a string
to the -t argument containing one or more of the following categories
separated by spaces:
- mmap
	tests for mmap(2)
- gup_test
	tests for gup
- userfaultfd
	tests for  userfaultfd(2)
- compaction
	a test for the patch "Allow compaction of unevictable pages"
- mlock
	tests for mlock(2)
- mremap
	tests for mremap(2)
- hugevm
	tests for very large virtual address space
- vmalloc
	vmalloc smoke tests
- hmm
	hmm smoke tests
- madv_populate
	test memadvise(2) MADV_POPULATE_{READ,WRITE} options
- memfd_secret
	test memfd_secret(2)
- process_mrelease
	test process_mrelease(2)
- ksm
	ksm tests that do not require >=2 NUMA nodes
- ksm_numa
	ksm tests that require >=2 NUMA nodes
- pkey
	memory protection key tests
- soft_dirty
	test soft dirty page bit semantics
- cow
	test copy-on-write semantics
example: ./run_vmtests.sh -t "hmm mmap ksm"
EOF
	exit 0
}


while getopts "ht:" OPT; do
	case ${OPT} in
		"h") usage ;;
		"t") VM_SELFTEST_ITEMS=${OPTARG} ;;
	esac
done
shift $((OPTIND -1))

# default behavior: run all tests
VM_SELFTEST_ITEMS=${VM_SELFTEST_ITEMS:-default}

test_selected() {
	if [ "$VM_SELFTEST_ITEMS" == "default" ]; then
		# If no VM_SELFTEST_ITEMS are specified, run all tests
		return 0
	fi
	# If test selected argument is one of the test items
	if [[ " ${VM_SELFTEST_ITEMS[*]} " =~ " ${1} " ]]; then
	        return 0
	else
	        return 1
	fi
}

# get huge pagesize and freepages from /proc/meminfo
while read -r name size unit; do
	if [ "$name" = "HugePages_Free:" ]; then
		freepgs="$size"
	fi
	if [ "$name" = "Hugepagesize:" ]; then
		hpgsize_KB="$size"
	fi
done < /proc/meminfo

# Simple hugetlbfs tests have a hardcoded minimum requirement of
# huge pages totaling 256MB (262144KB) in size.  The userfaultfd
# hugetlb test requires a minimum of 2 * nr_cpus huge pages.  Take
# both of these requirements into account and attempt to increase
# number of huge pages available.
nr_cpus=$(nproc)
hpgsize_MB=$((hpgsize_KB / 1024))
half_ufd_size_MB=$((((nr_cpus * hpgsize_MB + 127) / 128) * 128))
needmem_KB=$((half_ufd_size_MB * 2 * 1024))

# set proper nr_hugepages
if [ -n "$freepgs" ] && [ -n "$hpgsize_KB" ]; then
	nr_hugepgs=$(cat /proc/sys/vm/nr_hugepages)
	needpgs=$((needmem_KB / hpgsize_KB))
	tries=2
	while [ "$tries" -gt 0 ] && [ "$freepgs" -lt "$needpgs" ]; do
		lackpgs=$((needpgs - freepgs))
		echo 3 > /proc/sys/vm/drop_caches
		if ! echo $((lackpgs + nr_hugepgs)) > /proc/sys/vm/nr_hugepages; then
			echo "Please run this test as root"
			exit $ksft_skip
		fi
		while read -r name size unit; do
			if [ "$name" = "HugePages_Free:" ]; then
				freepgs=$size
			fi
		done < /proc/meminfo
		tries=$((tries - 1))
	done
	if [ "$freepgs" -lt "$needpgs" ]; then
		printf "Not enough huge pages available (%d < %d)\n" \
		       "$freepgs" "$needpgs"
		exit 1
	fi
else
	echo "no hugetlbfs support in kernel?"
	exit 1
fi

# filter 64bit architectures
ARCH64STR="arm64 ia64 mips64 parisc64 ppc64 ppc64le riscv64 s390x sparc64 x86_64"
if [ -z "$ARCH" ]; then
	ARCH=$(uname -m 2>/dev/null | sed -e 's/aarch64.*/arm64/')
fi
VADDR64=0
echo "$ARCH64STR" | grep "$ARCH" &>/dev/null && VADDR64=1

# Usage: run_test [test binary] [arbitrary test arguments...]
run_test() {
	if test_selected ${CATEGORY}; then
		local title="running $*"
		local sep=$(echo -n "$title" | tr "[:graph:][:space:]" -)
		printf "%s\n%s\n%s\n" "$sep" "$title" "$sep"

		"$@"
		local ret=$?
		if [ $ret -eq 0 ]; then
			count_pass=$(( count_pass + 1 ))
			echo "[PASS]"
		elif [ $ret -eq $ksft_skip ]; then
			count_skip=$(( count_skip + 1 ))
			echo "[SKIP]"
			exitcode=$ksft_skip
		else
			count_fail=$(( count_fail + 1 ))
			echo "[FAIL]"
			exitcode=1
		fi
	fi # test_selected
}

CATEGORY="hugetlb" run_test ./hugepage-mmap

shmmax=$(cat /proc/sys/kernel/shmmax)
shmall=$(cat /proc/sys/kernel/shmall)
echo 268435456 > /proc/sys/kernel/shmmax
echo 4194304 > /proc/sys/kernel/shmall
CATEGORY="hugetlb" run_test ./hugepage-shm
echo "$shmmax" > /proc/sys/kernel/shmmax
echo "$shmall" > /proc/sys/kernel/shmall

CATEGORY="hugetlb" run_test ./map_hugetlb
CATEGORY="hugetlb" run_test ./hugepage-mremap
CATEGORY="hugetlb" run_test ./hugepage-vmemmap
CATEGORY="hugetlb" run_test ./hugetlb-madvise

if test_selected "hugetlb"; then
	echo "NOTE: These hugetlb tests provide minimal coverage.  Use"
	echo "      https://github.com/libhugetlbfs/libhugetlbfs.git for"
	echo "      hugetlb regression testing."
fi

CATEGORY="mmap" run_test ./map_fixed_noreplace

# get_user_pages_fast() benchmark
CATEGORY="gup_test" run_test ./gup_test -u
# pin_user_pages_fast() benchmark
CATEGORY="gup_test" run_test ./gup_test -a
# Dump pages 0, 19, and 4096, using pin_user_pages:
CATEGORY="gup_test" run_test ./gup_test -ct -F 0x1 0 19 0x1000

CATEGORY="gup_test" run_test ./gup_longterm

CATEGORY="userfaultfd" run_test ./uffd-unit-tests
uffd_stress_bin=./uffd-stress
CATEGORY="userfaultfd" run_test ${uffd_stress_bin} anon 20 16
# Hugetlb tests require source and destination huge pages. Pass in half
# the size ($half_ufd_size_MB), which is used for *each*.
CATEGORY="userfaultfd" run_test ${uffd_stress_bin} hugetlb "$half_ufd_size_MB" 32
CATEGORY="userfaultfd" run_test ${uffd_stress_bin} hugetlb-private "$half_ufd_size_MB" 32
CATEGORY="userfaultfd" run_test ${uffd_stress_bin} shmem 20 16
CATEGORY="userfaultfd" run_test ${uffd_stress_bin} shmem-private 20 16

#cleanup
echo "$nr_hugepgs" > /proc/sys/vm/nr_hugepages

CATEGORY="compaction" run_test ./compaction_test

CATEGORY="mlock" run_test sudo -u nobody ./on-fault-limit

CATEGORY="mmap" run_test ./map_populate

CATEGORY="mlock" run_test ./mlock-random-test

CATEGORY="mlock" run_test ./mlock2-tests

CATEGORY="process_mrelease" run_test ./mrelease_test

CATEGORY="mremap" run_test ./mremap_test

CATEGORY="hugetlb" run_test ./thuge-gen

if [ $VADDR64 -ne 0 ]; then

	# set overcommit_policy as OVERCOMMIT_ALWAYS so that kernel
	# allows high virtual address allocation requests independent
	# of platform's physical memory.

	prev_policy=$(cat /proc/sys/vm/overcommit_memory)
	echo 1 > /proc/sys/vm/overcommit_memory
	CATEGORY="hugevm" run_test ./virtual_address_range
	echo $prev_policy > /proc/sys/vm/overcommit_memory

	# va high address boundary switch test
	ARCH_ARM64="arm64"
	prev_nr_hugepages=$(cat /proc/sys/vm/nr_hugepages)
	if [ "$ARCH" == "$ARCH_ARM64" ]; then
		echo 6 > /proc/sys/vm/nr_hugepages
	fi
	CATEGORY="hugevm" run_test bash ./va_high_addr_switch.sh
	if [ "$ARCH" == "$ARCH_ARM64" ]; then
		echo $prev_nr_hugepages > /proc/sys/vm/nr_hugepages
	fi
fi # VADDR64

# vmalloc stability smoke test
CATEGORY="vmalloc" run_test bash ./test_vmalloc.sh smoke

CATEGORY="mremap" run_test ./mremap_dontunmap

CATEGORY="hmm" run_test bash ./test_hmm.sh smoke

# MADV_POPULATE_READ and MADV_POPULATE_WRITE tests
CATEGORY="madv_populate" run_test ./madv_populate

CATEGORY="memfd_secret" run_test ./memfd_secret

# KSM MADV_MERGEABLE test with 10 identical pages
CATEGORY="ksm" run_test ./ksm_tests -M -p 10
# KSM unmerge test
CATEGORY="ksm" run_test ./ksm_tests -U
# KSM test with 10 zero pages and use_zero_pages = 0
CATEGORY="ksm" run_test ./ksm_tests -Z -p 10 -z 0
# KSM test with 10 zero pages and use_zero_pages = 1
CATEGORY="ksm" run_test ./ksm_tests -Z -p 10 -z 1
# KSM test with 2 NUMA nodes and merge_across_nodes = 1
CATEGORY="ksm_numa" run_test ./ksm_tests -N -m 1
# KSM test with 2 NUMA nodes and merge_across_nodes = 0
CATEGORY="ksm_numa" run_test ./ksm_tests -N -m 0

CATEGORY="ksm" run_test ./ksm_functional_tests

run_test ./ksm_functional_tests

# protection_keys tests
if [ -x ./protection_keys_32 ]
then
	CATEGORY="pkey" run_test ./protection_keys_32
fi

if [ -x ./protection_keys_64 ]
then
	CATEGORY="pkey" run_test ./protection_keys_64
fi

CATEGORY="soft_dirty" run_test ./soft-dirty

# COW tests
CATEGORY="cow" run_test ./cow

echo "SUMMARY: PASS=${count_pass} SKIP=${count_skip} FAIL=${count_fail}"

exit $exitcode
