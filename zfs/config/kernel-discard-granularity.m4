dnl #
dnl # 2.6.33 API change
dnl # Discard granularity and alignment restrictions may now be set.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_DISCARD_GRANULARITY], [
	AC_MSG_CHECKING([whether ql->discard_granularity is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct queue_limits ql __attribute__ ((unused));

		ql.discard_granularity = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DISCARD_GRANULARITY, 1,
		          [ql->discard_granularity is available])
	],[
		AC_MSG_RESULT(no)
	])
])
