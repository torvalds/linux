# SYNOPSIS
#
#   AX_CHECK_CATCHABLE_ABRT
#
# DESCRIPTION
#
#  Check whether SIGABRT can be caught using signal handlers.

#serial 1

AC_DEFUN([AX_CHECK_CATCHABLE_ABRT], [dnl
    AC_PREREQ(2.64)
    AS_VAR_PUSHDEF([CACHEVAR], [ax_cv_check_[]_AC_LANG_ABBREV[]CATCHABLE_ABRT])dnl
    AC_CACHE_CHECK([whether SIGABRT can be caught when using the _AC_LANG compiler], CACHEVAR, [
        AC_RUN_IFELSE([
            AC_LANG_PROGRAM([[
#include <signal.h>
#include <stdlib.h>

#ifndef SIGABRT
# error SIGABRT is not defined
#endif

static void sigabrt_handler_3(int _)
{
    exit(0);
}

static void sigabrt_handler_2(int _)
{
    signal(SIGABRT, sigabrt_handler_3);
    abort();
    exit(1);
}

static void sigabrt_handler_1(int _)
{
    signal(SIGABRT, sigabrt_handler_2);
    abort();
    exit(1);
}
            ]], [[
signal(SIGABRT, sigabrt_handler_1);
abort();
exit(1);
            ]])],
            [AS_VAR_SET(CACHEVAR, [yes])],
            [AS_VAR_SET(CACHEVAR, [no])],
            [AS_VAR_SET(CACHEVAR, [unknown])]
        )
    ])
    AS_VAR_IF(CACHEVAR, yes,
        [AC_DEFINE([HAVE_CATCHABLE_ABRT], [1], [Define if SIGABRT can be caught using signal handlers])],
        [AC_MSG_WARN([On this platform, SIGABRT cannot be caught using signal handlers.])]
    )
    AS_VAR_POPDEF([CACHEVAR])dnl
])
