dnl #
dnl # 2.6.36 API change,
dnl # The 'struct fs_struct->lock' was changed from a rwlock_t to
dnl # a spinlock_t to improve the fastpath performance.
dnl #
AC_DEFUN([SPL_AC_FS_STRUCT_SPINLOCK], [
	AC_MSG_CHECKING([whether struct fs_struct uses spinlock_t])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sched.h>
		#include <linux/fs_struct.h>
	],[
		static struct fs_struct fs;
		spin_lock_init(&fs.lock);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_STRUCT_SPINLOCK, 1,
		          [struct fs_struct uses spinlock_t])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
