dnl #
dnl # 3.10 API change,
dnl # PDE is replaced by PDE_DATA
dnl #
AC_DEFUN([SPL_AC_PDE_DATA], [
	AC_MSG_CHECKING([whether PDE_DATA() is available])
	SPL_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/proc_fs.h>
	], [
		PDE_DATA(NULL);
	], [PDE_DATA], [], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PDE_DATA, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])
