dnl #
dnl # Check if gcc supports -Wno-format-truncation option.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_NO_FORMAT_TRUNCATION], [
	AC_MSG_CHECKING([for -Wno-format-truncation support])

	saved_flags="$CFLAGS"
	CFLAGS="$CFLAGS -Wno-format-truncation"

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], [])],
	[
		NO_FORMAT_TRUNCATION=-Wno-format-truncation
		AC_MSG_RESULT([yes])
	],
	[
		NO_FORMAT_TRUNCATION=
		AC_MSG_RESULT([no])
	])

	CFLAGS="$saved_flags"
	AC_SUBST([NO_FORMAT_TRUNCATION])
])
