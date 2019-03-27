# SYNOPSIS
#
#   AX_CHECK_CATCHABLE_SEGV
#
# DESCRIPTION
#
#  Check whether segmentation violations can be caught using signal handlers.

#serial 1

AC_DEFUN([AX_CHECK_CATCHABLE_SEGV], [dnl
    AC_PREREQ(2.64)
    AS_VAR_PUSHDEF([CACHEVAR], [ax_cv_check_[]_AC_LANG_ABBREV[]CATCHABLE_SEGV])dnl
    AC_CACHE_CHECK([whether segmentation violations can be caught when using the _AC_LANG compiler], CACHEVAR, [
        AC_RUN_IFELSE([
            AC_LANG_PROGRAM([[
#include <signal.h>
#include <stdlib.h>
static void sig(int _) { exit(0); }
            ]], [[
volatile unsigned char * volatile x = (volatile unsigned char *) malloc(8);
size_t i;

signal(SIGSEGV, sig);
signal(SIGBUS, sig);
#if !defined(__SANITIZE_ADDRESS__) && !defined(__EMSCRIPTEN__)
for (i = 0; i < 10000000; i += 1024) { x[-i] = x[i] = (unsigned char) i; }
#endif
free((void *) x);
exit(1)
            ]])],
            [AS_VAR_SET(CACHEVAR, [yes])],
            [AS_VAR_SET(CACHEVAR, [no])],
            [AS_VAR_SET(CACHEVAR, [unknown])]
        )
    ])
    AS_VAR_IF(CACHEVAR, yes,
        [AC_DEFINE([HAVE_CATCHABLE_SEGV], [1], [Define if segmentation violations can be caught using signal handlers])],
        [AC_MSG_WARN([On this platform, segmentation violations cannot be caught using signal handlers. This is expected if you enabled a tool such as Address Sanitizer (-fsanitize=address), but be aware that using Address Sanitizer may also significantly reduce performance.])]
    )
    AS_VAR_POPDEF([CACHEVAR])dnl
])
