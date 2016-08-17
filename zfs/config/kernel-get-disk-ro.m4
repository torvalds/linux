dnl #
dnl # 2.6.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GET_DISK_RO], [
	AC_MSG_CHECKING([whether get_disk_ro() is available])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct gendisk *disk = NULL;
		(void) get_disk_ro(disk);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_DISK_RO, 1,
		          [blk_disk_ro() is available])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
