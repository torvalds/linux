dnl #
dnl # 2.6.39 API change
dnl # The .get_sb callback has been replaced by a .mount callback
dnl # in the file_system_type structure.  When using the new
dnl # interface the caller must now use the mount_nodev() helper.
dnl # This updated callback and helper no longer pass the vfsmount.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MOUNT_NODEV],
	[AC_MSG_CHECKING([whether mount_nodev() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		mount_nodev(NULL, 0, NULL, NULL);
	], [mount_nodev], [fs/super.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MOUNT_NODEV, 1, [mount_nodev() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
