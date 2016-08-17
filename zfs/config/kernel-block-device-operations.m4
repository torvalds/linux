dnl #
dnl # 2.6.38 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS], [
	AC_MSG_CHECKING([whether bops->check_events() exists])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>

		unsigned int blk_check_events(struct gendisk *disk,
		    unsigned int clearing) { return (0); }

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.check_events	= blk_check_events,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS, 1,
		    [bops->check_events() exists])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 3.10.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID], [
	AC_MSG_CHECKING([whether bops->release() is void])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>

		void blk_release(struct gendisk *g, fmode_t mode) { return; }

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.open		= NULL,
			.release	= blk_release,
			.ioctl		= NULL,
			.compat_ioctl	= NULL,
		};
	],[
	],[
		AC_MSG_RESULT(void)
		AC_DEFINE(HAVE_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID, 1,
		          [bops->release() returns void])
	],[
		AC_MSG_RESULT(int)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
