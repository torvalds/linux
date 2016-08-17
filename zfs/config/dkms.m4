dnl #
dnl # Prevent manual building in DKMS source tree.
dnl #
AC_DEFUN([ZFS_AC_DKMS_INHIBIT], [
	AC_MSG_CHECKING([for dkms.conf file])
        AS_IF([test -e dkms.conf], [
		AC_MSG_ERROR([
	*** ZFS should not be manually built in the DKMS source tree.
	*** Remove all ZFS packages before compiling the ZoL sources.
	*** Running "make install" breaks ZFS packages.])
        ], [
		AC_MSG_RESULT([not found])
        ])
])
