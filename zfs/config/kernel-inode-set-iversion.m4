dnl #
dnl # 4.16 API change
dnl # inode_set_iversion introduced to set i_version
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_SET_IVERSION], [
	AC_MSG_CHECKING([whether inode_set_iversion() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/iversion.h>
	],[
		struct inode inode;
		inode_set_iversion(&inode, 1);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_SET_IVERSION, 1,
		    [inode_set_iversion() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
