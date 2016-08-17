dnl #
dnl # 2.6.28 API change,
dnl # check if fmode_t typedef is defined
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TYPE_FMODE_T],
	[AC_MSG_CHECKING([whether kernel defines fmode_t])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/types.h>
	],[
		fmode_t *ptr __attribute__ ((unused));
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_FMODE_T, 1,
		          [kernel defines fmode_t])
	],[
		AC_MSG_RESULT([no])
	])
])
