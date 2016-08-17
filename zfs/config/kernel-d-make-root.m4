dnl #
dnl # 3.4.0 API change
dnl # Added d_make_root() to replace previous d_alloc_root() function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_D_MAKE_ROOT],
	[AC_MSG_CHECKING([whether d_make_root() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/dcache.h>
	], [
		d_make_root(NULL);
	], [d_make_root], [fs/dcache.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_D_MAKE_ROOT, 1, [d_make_root() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
