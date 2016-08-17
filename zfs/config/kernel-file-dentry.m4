dnl #
dnl # 4.1 API change
dnl # struct access file->f_path.dentry was replaced by accessor function
dnl # since fix torvalds/linux@4bacc9c9234c ("overlayfs: Make f_path always
dnl # point to the overlay and f_inode to the underlay").
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FILE_DENTRY], [
	AC_MSG_CHECKING([whether file_dentry() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct file *f = NULL;
		file_dentry(f);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_DENTRY, 1, [file_dentry() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
