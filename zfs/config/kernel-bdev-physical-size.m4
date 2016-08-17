dnl #
dnl # 2.6.30 API change
dnl #
dnl # The bdev_physical_block_size() interface was added to provide a way
dnl # to determine the smallest write which can be performed without a
dnl # read-modify-write operation.  From the kernel documentation:
dnl #
dnl # What:          /sys/block/<disk>/queue/physical_block_size
dnl # Date:          May 2009
dnl # Contact:       Martin K. Petersen <martin.petersen@oracle.com>
dnl # Description:
dnl #                This is the smallest unit the storage device can write
dnl #                without resorting to read-modify-write operation.  It is
dnl #                usually the same as the logical block size but may be
dnl #                bigger.  One example is SATA drives with 4KB sectors
dnl #                that expose a 512-byte logical block size to the
dnl #                operating system.
dnl #
dnl # Unfortunately, this interface isn't entirely reliable because
dnl # drives are sometimes known to misreport this value.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDEV_PHYSICAL_BLOCK_SIZE], [
	AC_MSG_CHECKING([whether bdev_physical_block_size() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		bdev_physical_block_size(bdev);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_PHYSICAL_BLOCK_SIZE, 1,
		          [bdev_physical_block_size() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
