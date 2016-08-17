dnl #
dnl # Check for libudev - needed for vdev auto-online and auto-replace
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBUDEV], [
	LIBUDEV=

	AC_CHECK_HEADER([libudev.h], [
	    user_libudev=yes
	    AC_SUBST([LIBUDEV], ["-ludev"])
	    AC_DEFINE([HAVE_LIBUDEV], 1, [Define if you have libudev])
	], [
	    user_libudev=no
	])

	AC_SEARCH_LIBS([udev_device_get_is_initialized], [udev], [
	    AC_DEFINE([HAVE_LIBUDEV_UDEV_DEVICE_GET_IS_INITIALIZED], 1, [
	    Define if udev_device_get_is_initialized is available])], [])

])
