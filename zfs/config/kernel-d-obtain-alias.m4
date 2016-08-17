dnl #
dnl # 2.6.28 API change
dnl # Added d_obtain_alias() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_OBTAIN_ALIAS],
	[AC_MSG_CHECKING([whether d_obtain_alias() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/dcache.h>
	], [
		d_obtain_alias(NULL);
	], [d_obtain_alias], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_OBTAIN_ALIAS, 1,
		          [d_obtain_alias() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
