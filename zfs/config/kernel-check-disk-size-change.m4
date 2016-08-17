dnl #
dnl # 2.6.28 API change
dnl # Added check_disk_size_change() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CHECK_DISK_SIZE_CHANGE],
	[AC_MSG_CHECKING([whether check_disk_size_change() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		check_disk_size_change(NULL, NULL);
	], [check_disk_size_change], [fs/block_dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CHECK_DISK_SIZE_CHANGE, 1,
		          [check_disk_size_change() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
