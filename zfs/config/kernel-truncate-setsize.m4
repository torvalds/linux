dnl #
dnl # 2.6.35 API change
dnl # Added truncate_setsize() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TRUNCATE_SETSIZE],
	[AC_MSG_CHECKING([whether truncate_setsize() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		truncate_setsize(NULL, 0);
	], [truncate_setsize], [mm/truncate.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TRUNCATE_SETSIZE, 1,
		          [truncate_setsize() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
