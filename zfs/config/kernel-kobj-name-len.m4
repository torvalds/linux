dnl #
dnl # 2.6.27 API change,
dnl # kobject KOBJ_NAME_LEN static limit removed.  All users of this
dnl # constant were removed prior to 2.6.27, but to be on the safe
dnl # side this check ensures the constant is undefined.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_KOBJ_NAME_LEN], [
	AC_MSG_CHECKING([whether kernel defines KOBJ_NAME_LEN])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/kobject.h>
	],[
		int val __attribute__ ((unused));
		val = KOBJ_NAME_LEN;
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_KOBJ_NAME_LEN, 1,
		          [kernel defines KOBJ_NAME_LEN])
	],[
		AC_MSG_RESULT([no])
	])
])
