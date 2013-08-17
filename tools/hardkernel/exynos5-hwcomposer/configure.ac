#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([exynos5-hwcomposer], [0.1], [tobetter@gmail.com])
AM_INIT_AUTOMAKE(exynos5-hwcomposer, 0.1)
AC_CONFIG_SRCDIR([tools/exynos5-hwcomposer.cpp])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
LT_INIT
AC_SUBST([LIBTOOL_DEPS])
AC_PROG_CXX
AC_PROG_CC
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h sys/ioctl.h sys/time.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT32_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CONFIG_FILES([Makefile src/Makefile tools/Makefile])
AC_CHECK_FUNCS([gettimeofday memset strerror strtoull])

AC_OUTPUT
