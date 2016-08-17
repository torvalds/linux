dnl #
dnl # 2.6.28 API change
dnl # Added insert_inode_locked() helper function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INSERT_INODE_LOCKED],
	[AC_MSG_CHECKING([whether insert_inode_locked() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/fs.h>
	], [
		insert_inode_locked(NULL);
	], [insert_inode_locked], [fs/inode.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INSERT_INODE_LOCKED, 1,
		          [insert_inode_locked() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
