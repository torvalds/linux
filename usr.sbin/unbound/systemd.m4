#   macros for configuring systemd
#   Copyright 2015, Sami Kerola, CloudFlare.
#   BSD licensed.
AC_ARG_ENABLE([systemd],
	[AS_HELP_STRING([--enable-systemd], [compile with systemd support (requires libsystemd, pkg-config)])],
	[], [enable_systemd=no])
have_systemd=no
AS_IF([test "x$enable_systemd" != xno], [
    if test -n "$PKG_CONFIG"; then
	dnl systemd v209 or newer
	have_systemd=no
	PKG_CHECK_MODULES([SYSTEMD], [libsystemd], [have_systemd=yes], [])
	dnl old systemd library
	AS_IF([test "x$have_systemd" != "xyes"], [
		have_systemd_daemon=no
		PKG_CHECK_MODULES([SYSTEMD_DAEMON], [libsystemd-daemon],
			[have_systemd_daemon=yes], [])
		AS_IF([test "x$have_systemd_daemon" = "xyes"],
			[have_systemd=yes])
	])
	AS_CASE([$enable_systemd:$have_systemd],
	[yes:no],
		[AC_MSG_ERROR([systemd enabled but libsystemd not found])],
	[*:yes],
		[AC_DEFINE([HAVE_SYSTEMD], [1], [Define to 1 if systemd should be used])
		LIBS="$LIBS $SYSTEMD_LIBS"
		]
	)
    else
    	AC_MSG_ERROR([systemd enabled but need pkg-config to configure for it])
    fi
])
AM_CONDITIONAL([USE_SYSTEMD], [test "x$have_systemd" = xyes])
