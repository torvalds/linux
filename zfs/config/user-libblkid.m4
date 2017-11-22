dnl #
dnl # Check for libblkid.  Basic support for detecting ZFS pools
dnl # has existing in blkid since 2008.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBBLKID], [
	LIBBLKID=

	AC_CHECK_HEADER([blkid/blkid.h], [], [AC_MSG_FAILURE([
        *** blkid.h missing, libblkid-devel package required])])

	AC_SUBST([LIBBLKID], ["-lblkid"])
	AC_DEFINE([HAVE_LIBBLKID], 1, [Define if you have libblkid])
])
