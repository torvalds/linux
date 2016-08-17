dnl #
dnl # 2.6.34 API change
dnl # Verify the get_gendisk() symbol is available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GET_GENDISK],
	[AC_MSG_CHECKING([whether get_gendisk() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/genhd.h>
	], [
		get_gendisk(0, NULL);
	], [get_gendisk], [block/genhd.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_GENDISK, 1, [get_gendisk() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
