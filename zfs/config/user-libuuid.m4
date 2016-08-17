dnl #
dnl # Check for libuuid
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBUUID], [
	LIBUUID=

	AC_CHECK_HEADER([uuid/uuid.h], [], [AC_MSG_FAILURE([
	*** uuid/uuid.h missing, libuuid-devel package required])])

	AC_CHECK_LIB([uuid], [uuid_generate], [], [AC_MSG_FAILURE([
	*** uuid_generate() missing, libuuid-devel package required])])

	AC_CHECK_LIB([uuid], [uuid_is_null], [], [AC_MSG_FAILURE([
	*** uuid_is_null() missing, libuuid-devel package required])])

	AC_SUBST([LIBUUID], ["-luuid"])
	AC_DEFINE([HAVE_LIBUUID], 1, [Define if you have libuuid])
])
