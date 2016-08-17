dnl #
dnl # 2.6.30 API change
dnl # bdev_hardsect_size() replaced with bdev_logical_block_size().  While
dnl # it has been true for a while that there was no strict 1:1 mapping
dnl # between physical sector size and logical block size this change makes
dnl # it explicit.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDEV_LOGICAL_BLOCK_SIZE], [
	AC_MSG_CHECKING([whether bdev_logical_block_size() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct block_device *bdev = NULL;
		bdev_logical_block_size(bdev);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_LOGICAL_BLOCK_SIZE, 1,
		          [bdev_logical_block_size() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
