dnl #
dnl # 3.5.0 API change
dnl # torvalds/linux@b0b0382bb4904965a9e9fca77ad87514dfda0d1c changed the
dnl # ->encode_fh() callback to pass the child inode and its parents inode
dnl # rather than a dentry and a boolean saying whether we want the parent.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_ENCODE_FH_WITH_INODE], [
	AC_MSG_CHECKING([whether eops->encode_fh() wants inode])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/exportfs.h>
		int encode_fh(struct inode *inode, __u32 *fh, int *max_len,
		              struct inode *parent) { return 0; }
		static struct export_operations eops __attribute__ ((unused))={
			.encode_fh = encode_fh,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ENCODE_FH_WITH_INODE, 1,
		          [eops->encode_fh() wants child and parent inodes])
	],[
		AC_MSG_RESULT(no)
	])
])
