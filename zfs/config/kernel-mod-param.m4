dnl #
dnl # Grsecurity kernel API change
dnl # constified parameters of module_param_call() methods
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MODULE_PARAM_CALL_CONST], [
	AC_MSG_CHECKING([whether module_param_call() is hardened])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
		#include <linux/moduleparam.h>

		int param_get(char *b, const struct kernel_param *kp)
		{
			return (0);
		}

		int param_set(const char *b, const struct kernel_param *kp)
		{
			return (0);
		}

		module_param_call(p, param_set, param_get, NULL, 0644);
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(MODULE_PARAM_CALL_CONST, 1,
		    [hardened module_param_call])
	],[
		AC_MSG_RESULT(no)
	])
])
