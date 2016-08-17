dnl #
dnl # 3.3 API change
dnl # The VFS .create, .mkdir and .mknod callbacks were updated to take a
dnl # umode_t type rather than an int.  The expectation is that any backport
dnl # would also change all three prototypes.  However, if it turns out that
dnl # some distribution doesn't backport the whole thing this could be
dnl # broken apart in to three seperate checks.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MKDIR_UMODE_T], [
	AC_MSG_CHECKING([whether iops->create()/mkdir()/mknod() take umode_t])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int mkdir(struct inode *inode, struct dentry *dentry,
		    umode_t umode) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.mkdir = mkdir,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MKDIR_UMODE_T, 1,
		    [iops->create()/mkdir()/mknod() take umode_t])
	],[
		AC_MSG_RESULT(no)
	])
])
