dnl #
dnl # 3.10.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID], [
	AC_MSG_CHECKING([whether block_device_operations.release is void])
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
		          [struct block_device_operations.release returns void])
	],[
		AC_MSG_RESULT(int)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
