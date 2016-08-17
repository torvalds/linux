dnl #
dnl # 3.6 API change,
dnl # 'sget' now takes the mount flags as an argument.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_5ARG_SGET],
	[AC_MSG_CHECKING([whether sget() wants 5 args])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct file_system_type *type = NULL;
		int (*test)(struct super_block *,void *) = NULL;
		int (*set)(struct super_block *,void *) = NULL;
		int flags = 0;
		void *data = NULL;
		(void) sget(type, test, set, flags, data);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_5ARG_SGET, 1, [sget() wants 5 args])
	],[
		AC_MSG_RESULT(no)
	])
])

