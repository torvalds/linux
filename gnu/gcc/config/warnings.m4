# Autoconf include file defining macros related to compile-time warnings.

# Copyright 2004, 2005 Free Software Foundation, Inc.

#This file is part of GCC.

#GCC is free software; you can redistribute it and/or modify it under
#the terms of the GNU General Public License as published by the Free
#Software Foundation; either version 2, or (at your option) any later
#version.

#GCC is distributed in the hope that it will be useful, but WITHOUT
#ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
#for more details.

#You should have received a copy of the GNU General Public License
#along with GCC; see the file COPYING.  If not, write to the Free
#Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
#02110-1301, USA.

# ACX_PROG_CC_WARNING_OPTS([-Wfoo -Wbar -Wbaz])
#   Sets @WARN_CFLAGS@ to the subset of the given options which the
#   compiler accepts.
AC_DEFUN([ACX_PROG_CC_WARNING_OPTS],
[AC_REQUIRE([AC_PROG_CC])dnl
AC_SUBST([WARN_CFLAGS])dnl
WARN_CFLAGS=
save_CFLAGS="$CFLAGS"
for option in $1; do
  AS_VAR_PUSHDEF([acx_Woption], [acx_cv_prog_cc_warning_$option])
  AC_CACHE_CHECK([whether $CC supports $option], acx_Woption,
    [CFLAGS="$option"
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])],
      [AS_VAR_SET(acx_Woption, yes)],
      [AS_VAR_SET(acx_Woption, no)])
  ])
  AS_IF([test AS_VAR_GET(acx_Woption) = yes],
        [WARN_CFLAGS="$WARN_CFLAGS${WARN_CFLAGS:+ }$option"])
  AS_VAR_POPDEF([acx_Woption])dnl
done
CFLAGS="$save_CFLAGS"
])# ACX_PROG_CC_WARNING_OPTS

# ACX_PROG_CC_WARNING_ALMOST_PEDANTIC([-Wno-long-long ...])
#   Sets WARN_PEDANTIC to "-pedantic" + the argument, if the compiler is GCC
#   and accepts all of those options simultaneously, otherwise to nothing.
AC_DEFUN([ACX_PROG_CC_WARNING_ALMOST_PEDANTIC],
[AC_REQUIRE([AC_PROG_CC])dnl
AC_SUBST([WARN_PEDANTIC])dnl
AS_VAR_PUSHDEF([acx_Pedantic], [acx_cv_prog_cc_pedantic_$1])dnl
WARN_PEDANTIC=
AS_IF([test "$GCC" = yes],
[AC_CACHE_CHECK([whether $CC supports -pedantic $1], acx_Pedantic,
[save_CFLAGS="$CFLAGS"
CFLAGS="-pedantic $1"
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],[])],
   [AS_VAR_SET(acx_Pedantic, yes)],
   [AS_VAR_SET(acx_Pedantic, no)])
CFLAGS="$save_CFLAGS"])
AS_IF([test AS_VAR_GET(acx_Pedantic) = yes],
      [WARN_PEDANTIC="-pedantic $1"])
])
AS_VAR_POPDEF([acx_Pedantic])dnl
])# ACX_PROG_CC_WARNING_ALMOST_PEDANTIC

# ACX_PROG_CC_WARNINGS_ARE_ERRORS([x.y.z])
#   sets WERROR to "-Werror" if the compiler is GCC >=x.y.z, or if
#   --enable-werror-always was given on the command line, otherwise
#   to nothing.
#   If the argument is the word "manual" instead of a version number,
#   then WERROR will be set to -Werror only if --enable-werror-always
#   appeared on the configure command line.
AC_DEFUN([ACX_PROG_CC_WARNINGS_ARE_ERRORS],
[AC_REQUIRE([AC_PROG_CC])dnl
AC_SUBST([WERROR])dnl
WERROR=
AC_ARG_ENABLE(werror-always, 
    AS_HELP_STRING([--enable-werror-always],
		   [enable -Werror despite compiler version]),
[], [enable_werror_always=no])
AS_IF([test $enable_werror_always = yes],
      [WERROR=-Werror],
 m4_if($1, [manual],,
 [AS_VAR_PUSHDEF([acx_GCCvers], [acx_cv_prog_cc_gcc_$1_or_newer])dnl
  AC_CACHE_CHECK([whether $CC is GCC >=$1], acx_GCCvers,
    [set fnord `echo $1 | tr '.' ' '`
     shift
     AC_PREPROC_IFELSE(
[#if __GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__ \
  < [$]1 * 10000 + [$]2 * 100 + [$]3
#error insufficient
#endif],
   [AS_VAR_SET(acx_GCCvers, yes)],
   [AS_VAR_SET(acx_GCCvers, no)])])
 AS_IF([test AS_VAR_GET(acx_GCCvers) = yes],
       [WERROR=-WerrorB])
  AS_VAR_POPDEF([acx_GCCvers])]))
])# ACX_PROG_CC_WARNINGS_ARE_ERRORS
