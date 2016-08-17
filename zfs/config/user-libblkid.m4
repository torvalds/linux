dnl #
dnl # Check for ZFS support in libblkid.  This test needs to check
dnl # more than if the library exists because we expect there are
dnl # at least 3 flavors of the library out in the wild:
dnl #
dnl #   1) blkid which has no ZFS support
dnl #   2) blkid with ZFS support and a flawed method of probing
dnl #   3) blkid with ZFS support and a working method of probing
dnl #
dnl # To handle this the check first validates that there is a version
dnl # of the library installed.  If there is it creates a simulated
dnl # ZFS filesystem and then links a small test app which attempts
dnl # to detect the simualated filesystem type.  If it correctly
dnl # identifies the filesystem as ZFS we can safely assume case 3).
dnl # Otherwise we disable blkid support and resort to manual probing.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBBLKID], [
	AC_ARG_WITH([blkid],
		[AS_HELP_STRING([--with-blkid],
		[support blkid caching @<:@default=check@:>@])],
		[],
		[with_blkid=check])

	LIBBLKID=
	AS_IF([test "x$with_blkid" = xyes],
	[
		AC_SUBST([LIBBLKID], ["-lblkid"])
		AC_DEFINE([HAVE_LIBBLKID], 1,
			[Define if you have libblkid])
	])

	AS_IF([test "x$with_blkid" = xcheck],
	[
		AC_CHECK_LIB([blkid], [blkid_get_cache],
		[
			AC_MSG_CHECKING([for blkid zfs support])

			ZFS_DEV=`mktemp`
			truncate -s 64M $ZFS_DEV
			echo -en "\x0c\xb1\xba\0\0\0\0\0" | \
				dd of=$ZFS_DEV bs=1k count=8 \
				seek=128 conv=notrunc &>/dev/null \
				>/dev/null 2>/dev/null
			echo -en "\x0c\xb1\xba\0\0\0\0\0" | \
				dd of=$ZFS_DEV bs=1k count=8 \
				seek=132 conv=notrunc &>/dev/null \
				>/dev/null 2>/dev/null
			echo -en "\x0c\xb1\xba\0\0\0\0\0" | \
				dd of=$ZFS_DEV bs=1k count=8 \
				seek=136 conv=notrunc &>/dev/null \
				>/dev/null 2>/dev/null
			echo -en "\x0c\xb1\xba\0\0\0\0\0" | \
				dd of=$ZFS_DEV bs=1k count=8 \
				seek=140 conv=notrunc &>/dev/null \
				>/dev/null 2>/dev/null

			saved_LIBS="$LIBS"
			LIBS="-lblkid"

			AC_RUN_IFELSE([AC_LANG_PROGRAM(
			[
				#include <stdio.h>
				#include <stdlib.h>
				#include <blkid/blkid.h>
			],
			[
				blkid_cache cache;
				char *value;

			        if (blkid_get_cache(&cache, NULL) < 0)
					return 1;

				value = blkid_get_tag_value(cache, "TYPE",
				                            "$ZFS_DEV");
				if (!value) {
					blkid_put_cache(cache);
					return 2;
				}

				if (strcmp(value, "zfs_member")) {
					free(value);
					blkid_put_cache(cache);
					return 0;
				}

				free(value);
				blkid_put_cache(cache);
			])],
			[
				rm -f $ZFS_DEV
				AC_MSG_RESULT([yes])
				AC_SUBST([LIBBLKID], ["-lblkid"])
				AC_DEFINE([HAVE_LIBBLKID], 1,
					[Define if you have libblkid])
			],
			[
				rm -f $ZFS_DEV
				AC_MSG_RESULT([no])
				AS_IF([test "x$with_blkid" != xcheck],
					[AC_MSG_FAILURE(
					[--with-blkid given but unavailable])])
			])

			LIBS="$saved_LIBS"
		],
		[
			AS_IF([test "x$with_blkid" != xcheck],
				[AC_MSG_FAILURE(
				[--with-blkid given but unavailable])])
		]
		[])
	])
])
