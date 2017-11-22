dnl #
dnl # 3.11 API change
dnl # Add support for i_op->tmpfile
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TMPFILE], [
	AC_MSG_CHECKING([whether i_op->tmpfile() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		int tmpfile(struct inode *inode, struct dentry *dentry,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TMPFILE, 1,
		    [i_op->tmpfile() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
