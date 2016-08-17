dnl #
dnl # 3.9 API change
dnl # set_fs_pwd takes const struct path *
dnl #
AC_DEFUN([SPL_AC_SET_FS_PWD_WITH_CONST],
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	[AC_MSG_CHECKING([whether set_fs_pwd() requires const struct path *])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/spinlock.h>
		#include <linux/fs_struct.h>
		#include <linux/path.h>
		void (*const set_fs_pwd_func)
			(struct fs_struct *, const struct path *)
			= set_fs_pwd;
	],[
		return 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_FS_PWD_WITH_CONST, 1,
			[set_fs_pwd() needs const path *])
	],[
		SPL_LINUX_TRY_COMPILE([
			#include <linux/spinlock.h>
			#include <linux/fs_struct.h>
			#include <linux/path.h>
			void (*const set_fs_pwd_func)
				(struct fs_struct *, struct path *)
				= set_fs_pwd;
		],[
			return 0;
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_ERROR(unknown)
		])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
