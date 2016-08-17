dnl #
dnl # Check if gcc supports -Wno-bool-compare option.
dnl #
dnl # We actually invoke gcc with the -Wbool-compare option
dnl # and infer the 'no-' version does or doesn't exist based upon
dnl # the results.  This is required because when checking any of
dnl # no- prefixed options gcc always returns success.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_NO_BOOL_COMPARE], [
	AC_MSG_CHECKING([for -Wno-bool-compare support])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Wbool-compare"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
	[
		NO_BOOL_COMPARE=-Wno-bool-compare
		AC_MSG_RESULT([yes])
	],
	[
		NO_BOOL_COMPARE=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_BOOL_COMPARE])
])
