dnl #
dnl # 3.5 API change,
dnl # inode_operations.truncate_range removed
dnl #
AC_DEFUN([SPL_AC_INODE_TRUNCATE_RANGE], [
	AC_MSG_CHECKING([whether truncate_range() inode operation is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode_operations ops;
		ops.truncate_range = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TRUNCATE_RANGE, 1,
			[truncate_range() inode operation is available])
	],[
		AC_MSG_RESULT(no)
	])
])
