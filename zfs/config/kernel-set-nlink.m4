dnl #
dnl # Linux v3.2-rc1 API change
dnl # SHA: bfe8684869601dacfcb2cd69ef8cfd9045f62170
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SET_NLINK], [
	AC_MSG_CHECKING([whether set_nlink() is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode node;
		unsigned int link = 0;
		(void) set_nlink(&node, link);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_NLINK, 1,
		          [set_nlink() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
