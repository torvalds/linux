dnl #
dnl # Linux 4.11 API
dnl # See torvalds/linux@a528d35
dnl #
AC_DEFUN([ZFS_AC_PATH_KERNEL_IOPS_GETATTR], [
	AC_MSG_CHECKING([whether iops->getattr() takes a path])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int test_getattr(
		    const struct path *p, struct kstat *k,
		    u32 request_mask, unsigned int query_flags)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.getattr = test_getattr,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PATH_IOPS_GETATTR, 1,
		    [iops->getattr() takes a path])
	],[
		AC_MSG_RESULT(no)
	])
])



dnl #
dnl # Linux 3.9 - 4.10 API
dnl #
AC_DEFUN([ZFS_AC_VFSMOUNT_KERNEL_IOPS_GETATTR], [
	AC_MSG_CHECKING([whether iops->getattr() takes a vfsmount])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		int test_getattr(
		    struct vfsmount *mnt, struct dentry *d,
		    struct kstat *k)
		    { return 0; }

		static const struct inode_operations
		    iops __attribute__ ((unused)) = {
			.getattr = test_getattr,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFSMOUNT_IOPS_GETATTR, 1,
		    [iops->getattr() takes a vfsmount])
	],[
		AC_MSG_RESULT(no)
	])
])


dnl #
dnl # The interface of the getattr callback from the inode_operations
dnl # structure changed.  Also, the interface of the simple_getattr()
dnl # function provided by the kernel changed.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_OPERATIONS_GETATTR], [
	ZFS_AC_PATH_KERNEL_IOPS_GETATTR
	ZFS_AC_VFSMOUNT_KERNEL_IOPS_GETATTR
])
