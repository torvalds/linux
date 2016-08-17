dnl #
dnl # 2.6.36 API change
dnl # In 2.6.36 kernels the blk_queue_ordered() interface has been
dnl # replaced by the simpler blk_queue_flush().  However, while the
dnl # old interface was available to all the new one is GPL-only.
dnl # Thus in addition to detecting if this function is available
dnl # we determine if it is GPL-only.  If the GPL-only interface is
dnl # there we implement our own compatibility function, otherwise
dnl # we use the function.  The hope is that long term this function
dnl # will be opened up.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLK_QUEUE_FLUSH], [
	AC_MSG_CHECKING([whether blk_queue_flush() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct request_queue *q = NULL;
		(void) blk_queue_flush(q, REQ_FLUSH);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_FLUSH, 1,
		          [blk_queue_flush() is available])

		AC_MSG_CHECKING([whether blk_queue_flush() is GPL-only])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/module.h>
			#include <linux/blkdev.h>

			MODULE_LICENSE("$ZFS_META_LICENSE");
		],[
			struct request_queue *q = NULL;
			(void) blk_queue_flush(q, REQ_FLUSH);
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_FLUSH_GPL_ONLY, 1,
				  [blk_queue_flush() is GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])

	dnl #
	dnl # 4.7 API change
	dnl # Replace blk_queue_flush with blk_queue_write_cache
	dnl #
	AC_MSG_CHECKING([whether blk_queue_write_cache() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/kernel.h>
		#include <linux/blkdev.h>

	],[
		struct request_queue *q = NULL;
		blk_queue_write_cache(q, true, true);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLK_QUEUE_WRITE_CACHE, 1,
			[blk_queue_write_cache() exists])

		AC_MSG_CHECKING([whether blk_queue_write_cache() is GPL-only])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/kernel.h>
			#include <linux/module.h>
			#include <linux/blkdev.h>

			MODULE_LICENSE("$ZFS_META_LICENSE");
		],[
			struct request_queue *q = NULL;
			blk_queue_write_cache(q, true, true);
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLK_QUEUE_WRITE_CACHE_GPL_ONLY, 1,
				  [blk_queue_write_cache() is GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])

	EXTRA_KCFLAGS="$tmp_flags"
])
