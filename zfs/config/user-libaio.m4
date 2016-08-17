dnl #
dnl # Check for libaio - only used for libaiot test cases.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBAIO], [
	LIBAIO=

	AC_CHECK_HEADER([libaio.h], [
	    user_libaio=yes
	    AC_SUBST([LIBAIO], ["-laio"])
	    AC_DEFINE([HAVE_LIBAIO], 1, [Define if you have libaio])
	], [
	    user_libaio=no
	])
])
