dnl #
dnl # glibc 2.25
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_MAKEDEV_IN_SYSMACROS], [
	AC_MSG_CHECKING([makedev() is declared in sys/sysmacros.h])
	AC_TRY_COMPILE(
	[
		#include <sys/sysmacros.h>
	],[
		int k;
		k = makedev(0,0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MAKEDEV_IN_SYSMACROS, 1,
		    [makedev() is declared in sys/sysmacros.h])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # glibc X < Y < 2.25
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_MAKEDEV_IN_MKDEV], [
	AC_MSG_CHECKING([makedev() is declared in sys/mkdev.h])
	AC_TRY_COMPILE(
	[
		#include <sys/mkdev.h>
	],[
		int k;
		k = makedev(0,0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MAKEDEV_IN_MKDEV, 1,
		    [makedev() is declared in sys/mkdev.h])
	],[
		AC_MSG_RESULT(no)
	])
])
