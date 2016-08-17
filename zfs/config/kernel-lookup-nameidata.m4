dnl #
dnl # 3.6 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_LOOKUP_NAMEIDATA], [
	AC_MSG_CHECKING([whether iops->lookup() passes nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/sched.h>

		struct dentry *inode_lookup(struct inode *inode,
		    struct dentry *dentry, struct nameidata *nidata)
		    { return NULL; }

		static const struct inode_operations iops
		    __attribute__ ((unused)) = {
			.lookup	= inode_lookup,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_LOOKUP_NAMEIDATA, 1,
		          [iops->lookup() passes nameidata])
	],[
		AC_MSG_RESULT(no)
	])
])
