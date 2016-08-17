dnl #
dnl # 2.6.38 API change
dnl # open_bdev_exclusive() changed to blkdev_get_by_path()
dnl # close_bdev_exclusive() changed to blkdev_put()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH],
	[AC_MSG_CHECKING([whether blkdev_get_by_path() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		blkdev_get_by_path(NULL, 0, NULL);
	], [blkdev_get_by_path], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKDEV_GET_BY_PATH, 1,
		          [blkdev_get_by_path() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
