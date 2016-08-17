dnl #
dnl # 4.5 API change
dnl # Added in_compat_syscall() which can be overridden on a per-
dnl # architecture basis.  Prior to this is_compat_task() was the
dnl # provided interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_IN_COMPAT_SYSCALL], [
	AC_MSG_CHECKING([whether in_compat_syscall() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/compat.h>
	],[
		in_compat_syscall();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IN_COMPAT_SYSCALL, 1,
		    [in_compat_syscall() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
