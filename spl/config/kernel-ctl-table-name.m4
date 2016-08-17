dnl #
dnl # 2.6.33 API change,
dnl # Removed .ctl_name from struct ctl_table.
dnl #
AC_DEFUN([SPL_AC_CTL_NAME], [
	AC_MSG_CHECKING([whether struct ctl_table has ctl_name])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sysctl.h>
	],[
		struct ctl_table ctl __attribute__ ((unused));
		ctl.ctl_name = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CTL_NAME, 1, [struct ctl_table has ctl_name])
	],[
		AC_MSG_RESULT(no)
	])
])
