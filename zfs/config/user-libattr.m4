dnl #
dnl # Check for libattr
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBATTR], [
	LIBATTR=

	AC_CHECK_HEADER([attr/xattr.h], [], [AC_MSG_FAILURE([
	*** attr/xattr.h missing, libattr-devel package required])])

	AC_SUBST([LIBATTR], ["-lattr"])
	AC_DEFINE([HAVE_LIBATTR], 1, [Define if you have libattr])
])
