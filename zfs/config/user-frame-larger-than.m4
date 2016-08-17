dnl #
dnl # Check if gcc supports -Wframe-larger-than=<size> option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_FRAME_LARGER_THAN], [
	AC_MSG_CHECKING([for -Wframe-larger-than=<size> support])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Wframe-larger-than=1024"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
	[
		FRAME_LARGER_THAN=-Wframe-larger-than=1024
		AC_MSG_RESULT([yes])
	],
	[
		FRAME_LARGER_THAN=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
        AC_SUBST([FRAME_LARGER_THAN])
])
