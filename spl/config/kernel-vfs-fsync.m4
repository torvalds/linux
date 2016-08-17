dnl #
dnl # 2.6.35 API change,
dnl # Unused 'struct dentry *' removed from vfs_fsync() prototype.
dnl #
AC_DEFUN([SPL_AC_2ARGS_VFS_FSYNC], [
	AC_MSG_CHECKING([whether vfs_fsync() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_fsync(NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_FSYNC, 1, [vfs_fsync() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])
