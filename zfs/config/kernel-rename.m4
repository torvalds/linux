dnl #
dnl # 4.9 API change,
dnl # iops->rename2() merged into iops->rename(), and iops->rename() now wants
dnl # flags.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_RENAME_WANTS_FLAGS], [
	AC_MSG_CHECKING([whether iops->rename() wants flags])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		int rename_fn(struct inode *sip, struct dentry *sdp,
			struct inode *tip, struct dentry *tdp,
			unsigned int flags) { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.rename = rename_fn,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RENAME_WANTS_FLAGS, 1, [iops->rename() wants flags])
	],[
		AC_MSG_RESULT(no)
	])
])
