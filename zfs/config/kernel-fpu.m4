dnl #
dnl # 4.2 API change
dnl # asm/i387.h is replaced by asm/fpu/api.h
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FPU], [
	AC_MSG_CHECKING([whether asm/fpu/api.h exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/kernel.h>
		#include <asm/fpu/api.h>
	],[
		__kernel_fpu_begin();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FPU_API_H, 1, [kernel has <asm/fpu/api.h> interface])
	],[
		AC_MSG_RESULT(no)
	])
])
