dnl #
dnl # 2.6.34 API change
dnl # blk_queue_max_segments() consolidates blk_queue_max_hw_segments()
dnl # and blk_queue_max_phys_segments().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS], [
	AC_MSG_CHECKING([whether blk_queue_max_segments() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		(void) blk_queue_max_segments(q, BLK_MAX_SEGMENTS);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_MAX_SEGMENTS, 1,
		          [blk_queue_max_segments() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
