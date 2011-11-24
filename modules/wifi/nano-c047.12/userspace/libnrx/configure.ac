#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(libnrx, 0.0, support@nanoradio.com)
AC_CONFIG_SRCDIR([init.c])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE(foreign no-dependencies 1.8)

# Checks for programs.
AC_PROG_CC
AC_PROG_RANLIB

AC_REPLACE_FUNCS(strlcpy)
AH_BOTTOM([#ifndef HAVE_STRLCPY
size_t _nrx_strlcpy (char*, const char*, size_t);
#define strlcpy _nrx_strlcpy
#endif])

AC_CONFIG_FILES([Makefile autotest/Makefile])
AC_OUTPUT
