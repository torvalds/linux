AC_DEFUN([ZFS_AC_CONFIG_USER_UDEV], [
	AC_MSG_CHECKING(for udev directories)
	AC_ARG_WITH(udevdir,
		AC_HELP_STRING([--with-udevdir=DIR],
		[install udev helpers @<:@default=check@:>@]),
		[udevdir=$withval],
		[udevdir=check])

	AS_IF([test "x$udevdir" = xcheck], [
		path1=/lib/udev
		path2=/usr/lib/udev
		default=$path2

		AS_IF([test -d "$path1"], [udevdir="$path1"], [
			AS_IF([test -d "$path2"], [udevdir="$path2"],
				[udevdir="$default"])
		])
	])

	AC_ARG_WITH(udevruledir,
		AC_HELP_STRING([--with-udevruledir=DIR],
		[install udev rules [[UDEVDIR/rules.d]]]),
		[udevruledir=$withval],
		[udevruledir="${udevdir}/rules.d"])

	AC_SUBST(udevdir)
	AC_SUBST(udevruledir)
	AC_MSG_RESULT([$udevdir;$udevruledir])
])
