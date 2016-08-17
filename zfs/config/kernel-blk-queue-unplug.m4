dnl #
dnl # 2.6.32-2.6.35 API - The BIO_RW_UNPLUG enum can be used as a hint
dnl # to unplug the queue.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_HAVE_BIO_RW_UNPLUG], [
	AC_MSG_CHECKING([whether the BIO_RW_UNPLUG enum is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		extern enum bio_rw_flags rw;

		rw = BIO_RW_UNPLUG;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_HAVE_BIO_RW_UNPLUG, 1,
		          [BIO_RW_UNPLUG is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_HAVE_BLK_PLUG], [
	AC_MSG_CHECKING([whether struct blk_plug is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct blk_plug plug;

		blk_start_plug(&plug);
		blk_finish_plug(&plug);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_HAVE_BLK_PLUG, 1,
		          [struct blk_plug is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
