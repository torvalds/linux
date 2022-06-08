#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#please run as root

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

mnt=./huge
exitcode=0

#get huge pagesize and freepages from /proc/meminfo
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

#set proper nr_hugepages
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

#filter 64bit architectures
ARCH64STR="arm64 ia64 mips64 parisc64 ppc64 ppc64le riscv64 s390x sh64 sparc64 x86_64"
if [ -z "$ARCH" ]; then
	ARCH=$(uname -m 2>/dev/null | sed -e 's/aarch64.*/arm64/')
fi
VADDR64=0
echo "$ARCH64STR" | grep "$ARCH" && VADDR64=1

# Usage: run_test [test binary] [arbitrary test arguments...]
run_test() {
	local title="running $*"
	local sep=$(echo -n "$title" | tr "[:graph:][:space:]" -)
	printf "%s\n%s\n%s\n" "$sep" "$title" "$sep"

	"$@"
	local ret=$?
	if [ $ret -eq 0 ]; then
		echo "[PASS]"
	elif [ $ret -eq $ksft_skip ]; then
		echo "[SKIP]"
		exitcode=$ksft_skip
	else
		echo "[FAIL]"
		exitcode=1
	fi
}

mkdir "$mnt"
mount -t hugetlbfs none "$mnt"

run_test ./hugepage-mmap

shmmax=$(cat /proc/sys/kernel/shmmax)
shmall=$(cat /proc/sys/kernel/shmall)
echo 268435456 > /proc/sys/kernel/shmmax
echo 4194304 > /proc/sys/kernel/shmall
run_test ./hugepage-shm
echo "$shmmax" > /proc/sys/kernel/shmmax
echo "$shmall" > /proc/sys/kernel/shmall

run_test ./map_hugetlb

run_test ./hugepage-mremap "$mnt"/huge_mremap
rm -f "$mnt"/huge_mremap

run_test ./hugepage-vmemmap

run_test ./hugetlb-madvise "$mnt"/madvise-test
rm -f "$mnt"/madvise-test

echo "NOTE: The above hugetlb tests provide minimal coverage.  Use"
echo "      https://github.com/libhugetlbfs/libhugetlbfs.git for"
echo "      hugetlb regression testing."

run_test ./map_fixed_noreplace

# get_user_pages_fast() benchmark
run_test ./gup_test -u
# pin_user_pages_fast() benchmark
run_test ./gup_test -a
# Dump pages 0, 19, and 4096, using pin_user_pages:
run_test ./gup_test -ct -F 0x1 0 19 0x1000

run_test ./userfaultfd anon 20 16
# Test requires source and destination huge pages.  Size of source
# (half_ufd_size_MB) is passed as argument to test.
run_test ./userfaultfd hugetlb "$half_ufd_size_MB" 32
run_test ./userfaultfd shmem 20 16

#cleanup
umount "$mnt"
rm -rf "$mnt"
echo "$nr_hugepgs" > /proc/sys/vm/nr_hugepages

run_test ./compaction_test

run_test sudo -u nobody ./on-fault-limit

run_test ./map_populate

run_test ./mlock-random-test

run_test ./mlock2-tests

run_test ./mrelease_test

run_test ./mremap_test

run_test ./thuge-gen

if [ $VADDR64 -ne 0 ]; then
	run_test ./virtual_address_range

	# virtual address 128TB switch test
	run_test ./va_128TBswitch
fi # VADDR64

# vmalloc stability smoke test
run_test ./test_vmalloc.sh smoke

run_test ./mremap_dontunmap

run_test ./test_hmm.sh smoke

# MADV_POPULATE_READ and MADV_POPULATE_WRITE tests
run_test ./madv_populate

run_test ./memfd_secret

# KSM MADV_MERGEABLE test with 10 identical pages
run_test ./ksm_tests -M -p 10
# KSM unmerge test
run_test ./ksm_tests -U
# KSM test with 10 zero pages and use_zero_pages = 0
run_test ./ksm_tests -Z -p 10 -z 0
# KSM test with 10 zero pages and use_zero_pages = 1
run_test ./ksm_tests -Z -p 10 -z 1
# KSM test with 2 NUMA nodes and merge_across_nodes = 1
run_test ./ksm_tests -N -m 1
# KSM test with 2 NUMA nodes and merge_across_nodes = 0
run_test ./ksm_tests -N -m 0

exit $exitcode
