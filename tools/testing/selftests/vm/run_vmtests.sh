#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#please run as root

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

mnt=./huge
exitcode=0

#get huge pagesize and freepages from /proc/meminfo
while read name size unit; do
	if [ "$name" = "HugePages_Free:" ]; then
		freepgs=$size
	fi
	if [ "$name" = "Hugepagesize:" ]; then
		hpgsize_KB=$size
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
	nr_hugepgs=`cat /proc/sys/vm/nr_hugepages`
	needpgs=$((needmem_KB / hpgsize_KB))
	tries=2
	while [ $tries -gt 0 ] && [ $freepgs -lt $needpgs ]; do
		lackpgs=$(( $needpgs - $freepgs ))
		echo 3 > /proc/sys/vm/drop_caches
		echo $(( $lackpgs + $nr_hugepgs )) > /proc/sys/vm/nr_hugepages
		if [ $? -ne 0 ]; then
			echo "Please run this test as root"
			exit $ksft_skip
		fi
		while read name size unit; do
			if [ "$name" = "HugePages_Free:" ]; then
				freepgs=$size
			fi
		done < /proc/meminfo
		tries=$((tries - 1))
	done
	if [ $freepgs -lt $needpgs ]; then
		printf "Not enough huge pages available (%d < %d)\n" \
		       $freepgs $needpgs
		exit 1
	fi
else
	echo "no hugetlbfs support in kernel?"
	exit 1
fi

#filter 64bit architectures
ARCH64STR="arm64 ia64 mips64 parisc64 ppc64 ppc64le riscv64 s390x sh64 sparc64 x86_64"
if [ -z $ARCH ]; then
  ARCH=`uname -m 2>/dev/null | sed -e 's/aarch64.*/arm64/'`
fi
VADDR64=0
echo "$ARCH64STR" | grep $ARCH && VADDR64=1

mkdir $mnt
mount -t hugetlbfs none $mnt

echo "---------------------"
echo "running hugepage-mmap"
echo "---------------------"
./hugepage-mmap
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

shmmax=`cat /proc/sys/kernel/shmmax`
shmall=`cat /proc/sys/kernel/shmall`
echo 268435456 > /proc/sys/kernel/shmmax
echo 4194304 > /proc/sys/kernel/shmall
echo "--------------------"
echo "running hugepage-shm"
echo "--------------------"
./hugepage-shm
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi
echo $shmmax > /proc/sys/kernel/shmmax
echo $shmall > /proc/sys/kernel/shmall

echo "-------------------"
echo "running map_hugetlb"
echo "-------------------"
./map_hugetlb
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "NOTE: The above hugetlb tests provide minimal coverage.  Use"
echo "      https://github.com/libhugetlbfs/libhugetlbfs.git for"
echo "      hugetlb regression testing."

echo "---------------------------"
echo "running map_fixed_noreplace"
echo "---------------------------"
./map_fixed_noreplace
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "------------------------------------------------------"
echo "running: gup_test -u # get_user_pages_fast() benchmark"
echo "------------------------------------------------------"
./gup_test -u
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "------------------------------------------------------"
echo "running: gup_test -a # pin_user_pages_fast() benchmark"
echo "------------------------------------------------------"
./gup_test -a
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "------------------------------------------------------------"
echo "# Dump pages 0, 19, and 4096, using pin_user_pages:"
echo "running: gup_test -ct -F 0x1 0 19 0x1000 # dump_page() test"
echo "------------------------------------------------------------"
./gup_test -ct -F 0x1 0 19 0x1000
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "-------------------"
echo "running userfaultfd"
echo "-------------------"
./userfaultfd anon 20 16
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "---------------------------"
echo "running userfaultfd_hugetlb"
echo "---------------------------"
# Test requires source and destination huge pages.  Size of source
# (half_ufd_size_MB) is passed as argument to test.
./userfaultfd hugetlb $half_ufd_size_MB 32 $mnt/ufd_test_file
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi
rm -f $mnt/ufd_test_file

echo "-------------------------"
echo "running userfaultfd_shmem"
echo "-------------------------"
./userfaultfd shmem 20 16
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

#cleanup
umount $mnt
rm -rf $mnt
echo $nr_hugepgs > /proc/sys/vm/nr_hugepages

echo "-----------------------"
echo "running compaction_test"
echo "-----------------------"
./compaction_test
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "----------------------"
echo "running on-fault-limit"
echo "----------------------"
sudo -u nobody ./on-fault-limit
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "--------------------"
echo "running map_populate"
echo "--------------------"
./map_populate
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "-------------------------"
echo "running mlock-random-test"
echo "-------------------------"
./mlock-random-test
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "--------------------"
echo "running mlock2-tests"
echo "--------------------"
./mlock2-tests
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "-------------------"
echo "running mremap_test"
echo "-------------------"
./mremap_test
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "-----------------"
echo "running thuge-gen"
echo "-----------------"
./thuge-gen
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

if [ $VADDR64 -ne 0 ]; then
echo "-----------------------------"
echo "running virtual_address_range"
echo "-----------------------------"
./virtual_address_range
if [ $? -ne 0 ]; then
	echo "[FAIL]"
	exitcode=1
else
	echo "[PASS]"
fi

echo "-----------------------------"
echo "running virtual address 128TB switch test"
echo "-----------------------------"
./va_128TBswitch
if [ $? -ne 0 ]; then
    echo "[FAIL]"
    exitcode=1
else
    echo "[PASS]"
fi
fi # VADDR64

echo "------------------------------------"
echo "running vmalloc stability smoke test"
echo "------------------------------------"
./test_vmalloc.sh smoke
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	 echo "[SKIP]"
	 exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "------------------------------------"
echo "running MREMAP_DONTUNMAP smoke test"
echo "------------------------------------"
./mremap_dontunmap
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	 echo "[SKIP]"
	 exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "running HMM smoke test"
echo "------------------------------------"
./test_hmm.sh smoke
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	echo "[SKIP]"
	exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "--------------------------------------------------------"
echo "running MADV_POPULATE_READ and MADV_POPULATE_WRITE tests"
echo "--------------------------------------------------------"
./madv_populate
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	echo "[SKIP]"
	exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "running memfd_secret test"
echo "------------------------------------"
./memfd_secret
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	echo "[SKIP]"
	exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "-------------------------------------------------------"
echo "running KSM MADV_MERGEABLE test with 10 identical pages"
echo "-------------------------------------------------------"
./ksm_tests -M -p 10
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	 echo "[SKIP]"
	 exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "------------------------"
echo "running KSM unmerge test"
echo "------------------------"
./ksm_tests -U
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	 echo "[SKIP]"
	 exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "----------------------------------------------------------"
echo "running KSM test with 10 zero pages and use_zero_pages = 0"
echo "----------------------------------------------------------"
./ksm_tests -Z -p 10 -z 0
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	 echo "[SKIP]"
	 exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

echo "----------------------------------------------------------"
echo "running KSM test with 10 zero pages and use_zero_pages = 1"
echo "----------------------------------------------------------"
./ksm_tests -Z -p 10 -z 1
ret_val=$?

if [ $ret_val -eq 0 ]; then
	echo "[PASS]"
elif [ $ret_val -eq $ksft_skip ]; then
	 echo "[SKIP]"
	 exitcode=$ksft_skip
else
	echo "[FAIL]"
	exitcode=1
fi

exit $exitcode

exit $exitcode
