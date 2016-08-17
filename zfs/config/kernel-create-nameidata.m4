dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CREATE_NAMEIDATA], [
	AC_MSG_CHECKING([whether iops->create() passes nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/sched.h>

		#ifdef HAVE_MKDIR_UMODE_T
		int inode_create(struct inode *inode ,struct dentry *dentry,
		    umode_t umode, struct nameidata *nidata) { return 0; }
		#else
		int inode_create(struct inode *inode,struct dentry *dentry,
		    int umode, struct nameidata * nidata) { return 0; }
		#endif

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.create		= inode_create,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CREATE_NAMEIDATA, 1,
		          [iops->create() passes nameidata])
	],[
		AC_MSG_RESULT(no)
	])
])
