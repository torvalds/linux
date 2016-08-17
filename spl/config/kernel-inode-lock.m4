dnl #
dnl # 4.7 API change
dnl # i_mutex is changed to i_rwsem. Instead of directly using
dnl # i_mutex/i_rwsem, we should use inode_lock() and inode_lock_shared()
dnl # We test inode_lock_shared because inode_lock is introduced earlier.
dnl #
AC_DEFUN([SPL_AC_INODE_LOCK], [
	AC_MSG_CHECKING([whether inode_lock_shared() exists])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct inode *inode = NULL;
		inode_lock_shared(inode);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_LOCK_SHARED, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
