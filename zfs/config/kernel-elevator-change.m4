dnl #
dnl # 2.6.36 API change
dnl # Verify the elevator_change() symbol is available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_ELEVATOR_CHANGE], [
	AC_MSG_CHECKING([whether elevator_change() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
		#include <linux/elevator.h>
	],[
		int ret;
		struct request_queue *q = NULL;
		char *elevator = NULL;
		ret = elevator_change(q, elevator);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ELEVATOR_CHANGE, 1,
			  [elevator_change() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
