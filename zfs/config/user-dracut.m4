AC_DEFUN([ZFS_AC_CONFIG_USER_DRACUT], [
	AC_MSG_CHECKING(for dracut directory)
	AC_ARG_WITH([dracutdir],
		AC_HELP_STRING([--with-dracutdir=DIR],
		[install dracut helpers @<:@default=check@:>@]),
		[dracutdir=$withval],
		[dracutdir=check])

	AS_IF([test "x$dracutdir" = xcheck], [
		path1=/usr/share/dracut
		path2=/usr/lib/dracut
		default=$path2

		AS_IF([test -d "$path1"], [dracutdir="$path1"], [
			AS_IF([test -d "$path2"], [dracutdir="$path2"],
				[dracutdir="$default"])
		])
	])

	AC_SUBST(dracutdir)
	AC_MSG_RESULT([$dracutdir])
])
