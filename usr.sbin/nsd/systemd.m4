#   macros for configuring systemd
#   Copyright 2015, Sami Kerola, CloudFlare.
#   BSD licensed.
AC_ARG_ENABLE([systemd],
	[AS_HELP_STRING([--enable-systemd], [compile with systemd support])],
	[], [enable_systemd=no])
have_systemd=no
AS_IF([test "x$enable_systemd" != xno], [
    ifdef([PKG_CHECK_MODULES], [
	dnl systemd v209 or newer
	PKG_CHECK_MODULES([SYSTEMD], [libsystemd], [have_systemd=yes], [have_systemd=no])
	dnl old systemd library
	AS_IF([test "x$have_systemd" != "xyes"], [
		PKG_CHECK_MODULES([SYSTEMD_DAEMON], [libsystemd-daemon],
			[have_systemd_daemon=yes], [have_systemd_daemon=no])
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
    ], [
    	AC_MSG_ERROR([systemd enabled but need pkg-config to configure for it, also, run aclocal before autoconf, or run autoreconf to include pkgconfig macros])
    ])
])
