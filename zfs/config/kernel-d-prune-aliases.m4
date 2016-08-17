dnl #
dnl # 2.6.12 API change
dnl # d_prune_aliases() helper function available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_PRUNE_ALIASES],
	[AC_MSG_CHECKING([whether d_prune_aliases() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/dcache.h>
	], [
		struct inode *ip = NULL;
		d_prune_aliases(ip);
	], [d_prune_aliases], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_PRUNE_ALIASES, 1,
		          [d_prune_aliases() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
