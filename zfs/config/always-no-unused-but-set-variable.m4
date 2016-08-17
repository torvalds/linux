dnl #
dnl # Check if gcc supports -Wno-unused-but-set-variable option.
dnl #
dnl # We actually invoke gcc with the -Wunused-but-set-variable option
dnl # and infer the 'no-' version does or doesn't exist based upon
dnl # the results.  This is required because when checking any of
dnl # no- prefixed options gcc always returns success.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_NO_UNUSED_BUT_SET_VARIABLE], [
	AC_MSG_CHECKING([for -Wno-unused-but-set-variable support])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Wunused-but-set-variable"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
	[
		NO_UNUSED_BUT_SET_VARIABLE=-Wno-unused-but-set-variable
		AC_MSG_RESULT([yes])
	],
	[
		NO_UNUSED_BUT_SET_VARIABLE=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_UNUSED_BUT_SET_VARIABLE])
])
