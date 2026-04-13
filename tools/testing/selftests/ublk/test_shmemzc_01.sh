#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Test: shmem_zc with hugetlbfs buffer on null target
#
# kublk and fio both mmap the same hugetlbfs file (MAP_SHARED),
# so they share physical pages.  The kernel PFN match enables
# zero-copy I/O without socket-based fd passing.

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

_prep_test "shmem_zc" "null target hugetlbfs shmem zero-copy test"

if ! _have_program fio; then
	echo "SKIP: fio not available"
	exit "$UBLK_SKIP_CODE"
fi

if ! grep -q hugetlbfs /proc/filesystems; then
	echo "SKIP: hugetlbfs not supported"
	exit "$UBLK_SKIP_CODE"
fi

# Allocate hugepages
OLD_NR_HP=$(cat /proc/sys/vm/nr_hugepages)
echo 10 > /proc/sys/vm/nr_hugepages
NR_HP=$(cat /proc/sys/vm/nr_hugepages)
if [ "$NR_HP" -lt 2 ]; then
	echo "SKIP: cannot allocate hugepages"
	echo "$OLD_NR_HP" > /proc/sys/vm/nr_hugepages
	exit "$UBLK_SKIP_CODE"
fi

# Mount hugetlbfs
HTLB_MNT=$(mktemp -d "${UBLK_TEST_DIR}/htlb_mnt_XXXXXX")
if ! mount -t hugetlbfs none "$HTLB_MNT"; then
	echo "SKIP: cannot mount hugetlbfs"
	rmdir "$HTLB_MNT"
	echo "$OLD_NR_HP" > /proc/sys/vm/nr_hugepages
	exit "$UBLK_SKIP_CODE"
fi

HTLB_FILE="$HTLB_MNT/ublk_buf"
fallocate -l 4M "$HTLB_FILE"

dev_id=$(_add_ublk_dev -t null --shmem_zc --htlb "$HTLB_FILE")
_check_add_dev $TID $?

fio --name=htlb_zc \
	--filename=/dev/ublkb"${dev_id}" \
	--ioengine=io_uring \
	--rw=randwrite \
	--direct=1 \
	--bs=4k \
	--size=4M \
	--iodepth=32 \
	--mem=mmaphuge:"$HTLB_FILE" \
	> /dev/null 2>&1
ERR_CODE=$?

# Delete device first so daemon releases the htlb mmap
_ublk_del_dev "${dev_id}"

rm -f "$HTLB_FILE"
umount "$HTLB_MNT"
rmdir "$HTLB_MNT"
echo "$OLD_NR_HP" > /proc/sys/vm/nr_hugepages

_cleanup_test "shmem_zc"

_show_result $TID $ERR_CODE
