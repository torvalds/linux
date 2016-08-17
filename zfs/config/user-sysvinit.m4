AC_DEFUN([ZFS_AC_CONFIG_USER_SYSVINIT], [
	AC_ARG_ENABLE(sysvinit,
		AC_HELP_STRING([--enable-sysvinit],
		[install SysV init scripts [default: yes]]),
		[],enable_sysvinit=yes)

	AS_IF([test "x$enable_sysvinit" = xyes],
		[ZFS_INIT_SYSV=init.d])

	AC_SUBST(ZFS_INIT_SYSV)
])
