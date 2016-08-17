dnl #
dnl # 2.6.37 API change
dnl # Added 3rd argument for the active holder, previously this was
dnl # hardcoded to NULL.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_3ARG_BLKDEV_GET], [
	AC_MSG_CHECKING([whether blkdev_get() wants 3 args])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct block_device *bdev = NULL;
		(void) blkdev_get(bdev, 0, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARG_BLKDEV_GET, 1, [blkdev_get() wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
])
