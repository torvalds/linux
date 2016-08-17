dnl #
dnl # 3.5.0 API change
dnl # torvalds/linux@17cf28afea2a1112f240a3a2da8af883be024811 removed
dnl # truncate_range(). The file hole punching functionality is now
dnl # provided by fallocate()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TRUNCATE_RANGE], [
	AC_MSG_CHECKING([whether iops->truncate_range() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		void truncate_range(struct inode *inode, loff_t start,
		                    loff_t end) { return; }
		static struct inode_operations iops __attribute__ ((unused)) = {
			.truncate_range	= truncate_range,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TRUNCATE_RANGE, 1,
		          [iops->truncate_range() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
