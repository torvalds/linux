dnl #
dnl # 4.9 API change
dnl # The inode_change_ok() function has been renamed setattr_prepare()
dnl # and updated to take a dentry rather than an inode.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SETATTR_PREPARE],
	[AC_MSG_CHECKING([whether setattr_prepare() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		struct dentry *dentry = NULL;
		struct iattr *attr = NULL;
		int error;

		error = setattr_prepare(dentry, attr);
	], [setattr_prepare], [fs/attr.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SETATTR_PREPARE, 1,
		    [setattr_prepare() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
