#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Test: shmem_zc with fio verify over filesystem on loop target
#
# mkfs + mount ext4 on the ublk device, then run fio verify on a
# file inside that filesystem.  Exercises the full stack:
# filesystem -> block layer -> ublk shmem_zc -> loop target backing file.

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

_prep_test "shmem_zc" "loop target hugetlbfs shmem zero-copy fs verify test"

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

_create_backfile 0 256M
BACKFILE="${UBLK_BACKFILES[0]}"

dev_id=$(_add_ublk_dev -t loop --shmem_zc --htlb "$HTLB_FILE" "$BACKFILE")
_check_add_dev $TID $?

_mkfs_mount_test /dev/ublkb"${dev_id}" \
	_run_fio_verify_io --filename=testfile \
		--size=128M \
		--mem=mmaphuge:"$HTLB_FILE"
ERR_CODE=$?

# Delete device first so daemon releases the htlb mmap
_ublk_del_dev "${dev_id}"

rm -f "$HTLB_FILE"
umount "$HTLB_MNT"
rmdir "$HTLB_MNT"
echo "$OLD_NR_HP" > /proc/sys/vm/nr_hugepages

_cleanup_test "shmem_zc"

_show_result $TID $ERR_CODE
