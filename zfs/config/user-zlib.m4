dnl #
dnl # Check for zlib
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_ZLIB], [
	ZLIB=

	AC_CHECK_HEADER([zlib.h], [], [AC_MSG_FAILURE([
	*** zlib.h missing, zlib-devel package required])])

	AC_CHECK_LIB([z], [compress2], [], [AC_MSG_FAILURE([
	*** compress2() missing, zlib-devel package required])])

	AC_CHECK_LIB([z], [uncompress], [], [AC_MSG_FAILURE([
	*** uncompress() missing, zlib-devel package required])])

	AC_CHECK_LIB([z], [crc32], [], [AC_MSG_FAILURE([
	*** crc32() missing, zlib-devel package required])])

	AC_SUBST([ZLIB], ["-lz"])
	AC_DEFINE([HAVE_ZLIB], 1, [Define if you have zlib])
])
