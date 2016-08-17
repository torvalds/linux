dnl #
dnl # 4.15 API change
dnl # https://lkml.org/lkml/2017/11/25/90
dnl # Check if timer_list.func get passed a timer_list or an unsigned long
dnl # (older kernels).  Also sanity check the from_timer() and timer_setup()
dnl # macros are available as well, since they will be used in the same newer
dnl # kernels that support the new timer_list.func signature.
dnl #
AC_DEFUN([SPL_AC_KERNEL_TIMER_FUNCTION_TIMER_LIST], [
	AC_MSG_CHECKING([whether timer_list.function gets a timer_list])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/timer.h>
		void task_expire(struct timer_list *tl) {}
	],[
		#ifndef from_timer
		#error "No from_timer() macro"
		#endif

		struct timer_list timer;
		timer.function = task_expire;
		timer_setup(&timer, NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_TIMER_FUNCTION_TIMER_LIST, 1,
		    [timer_list.function gets a timer_list])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
