dnl #
dnl # 2.6.27, lookup_bdev() was exported.
dnl # 4.4.0-6.21 - x.y on Ubuntu, lookup_bdev() takes 2 arguments.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_LOOKUP_BDEV],
	[AC_MSG_CHECKING([whether lookup_bdev() wants 1 arg])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		lookup_bdev(NULL);
	], [lookup_bdev], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_LOOKUP_BDEV, 1, [lookup_bdev() wants 1 arg])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether lookup_bdev() wants 2 args])
		ZFS_LINUX_TRY_COMPILE_SYMBOL([
			#include <linux/fs.h>
		], [
			lookup_bdev(NULL, FMODE_READ);
		], [lookup_bdev], [fs/block_dev.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_2ARGS_LOOKUP_BDEV, 1,
			    [lookup_bdev() wants 2 args])
		], [
			AC_MSG_RESULT(no)
		])
	])
])