dnl #
dnl # 3.0 API change
dnl # The sops->dirty_inode() callbacks were updated to take a flags
dnl # argument.  This allows the greater control over whether the
dnl # filesystem needs to push out a transaction or not.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_DIRTY_INODE_WITH_FLAGS], [
	AC_MSG_CHECKING([whether sops->dirty_inode() wants flags])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		void dirty_inode(struct inode *a, int b) { return; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.dirty_inode = dirty_inode,
		};
	],[
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_DIRTY_INODE_WITH_FLAGS, 1,
			[sops->dirty_inode() wants flags])
	],[
		AC_MSG_RESULT([no])
	])
])
