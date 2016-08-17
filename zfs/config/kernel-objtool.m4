dnl #
dnl # 4.6 API for compile-time stack validation
dnl #
AC_DEFUN([ZFS_AC_KERNEL_OBJTOOL], [
	AC_MSG_CHECKING([for compile-time stack validation (objtool)])
	ZFS_LINUX_TRY_COMPILE([
		#undef __ASSEMBLY__
		#include <asm/frame.h>
	],[
		#if !defined(FRAME_BEGIN)
		CTASSERT(1);
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_OBJTOOL, 1, [kernel does stack verification])
	],[
		AC_MSG_RESULT(no)
	])
])
