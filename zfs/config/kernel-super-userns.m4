dnl #
dnl # 4.8 API change
dnl # struct user_namespace was added to struct super_block as
dnl # super->s_user_ns member
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SUPER_USER_NS], [
	AC_MSG_CHECKING([whether super_block->s_user_ns exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/user_namespace.h>
	],[
		struct super_block super;
		super.s_user_ns = (struct user_namespace *)NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SUPER_USER_NS, 1,
		    [super_block->s_user_ns exists])
	],[
		AC_MSG_RESULT(no)
	])
])
