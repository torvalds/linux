# ld-output-def.m4 serial 2
dnl Copyright (C) 2008-2013 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl From Simon Josefsson

# gl_LD_OUTPUT_DEF()
# -------------
# Check if linker supports -Wl,--output-def and define automake
# conditional HAVE_LD_OUTPUT_DEF if it is.
AC_DEFUN([gl_LD_OUTPUT_DEF],
[
  AC_CACHE_CHECK([if gcc/ld supports -Wl,--output-def],
    [gl_cv_ld_output_def],
    [if test "$enable_shared" = no; then
       gl_cv_ld_output_def="not needed, shared libraries are disabled"
     else
       gl_ldflags_save=$LDFLAGS
       LDFLAGS="-Wl,--output-def,conftest.def"
       AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
                   [gl_cv_ld_output_def=yes],
                   [gl_cv_ld_output_def=no])
       rm -f conftest.def
       LDFLAGS="$gl_ldflags_save"
     fi])
  AM_CONDITIONAL([HAVE_LD_OUTPUT_DEF], test "x$gl_cv_ld_output_def" = "xyes")
])
