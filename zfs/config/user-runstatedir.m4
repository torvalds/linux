dnl For backwards compatibility; runstatedir added in autoconf 2.70.
AC_DEFUN([ZFS_AC_CONFIG_USER_RUNSTATEDIR], [
	if test "x$runstatedir" = x; then
		AC_SUBST([runstatedir], ['${localstatedir}/run'])
	fi
])
