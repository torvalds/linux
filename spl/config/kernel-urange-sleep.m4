dnl #
dnl # 2.6.36 API compatibility.
dnl # Added usleep_range timer.
dnl # usleep_range is a finer precision implementation of msleep
dnl # designed to be a drop-in replacement for udelay where a precise
dnl # sleep / busy-wait is unnecessary.
dnl #
AC_DEFUN([SPL_AC_USLEEP_RANGE], [
	AC_MSG_CHECKING([whether usleep_range() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/delay.h>
	],[
		usleep_range(0, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_USLEEP_RANGE, 1,
		          [usleep_range is available])
	],[
		AC_MSG_RESULT(no)
	])
])
