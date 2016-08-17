dnl #
dnl # 3.19 API change
dnl # struct access f->f_dentry->d_inode was replaced by accessor function
dnl # file_inode(f)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FILE_INODE], [
	AC_MSG_CHECKING([whether file_inode() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct file *f = NULL;
		file_inode(f);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_INODE, 1, [file_inode() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
