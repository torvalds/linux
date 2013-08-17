# Helper functions for option handling.                    -*- Autoconf -*-
#
#   Copyright (C) 2004, 2005, 2007, 2008, 2009 Free Software Foundation,
#   Inc.
#   Written by Gary V. Vaughan, 2004
#
# This file is free software; the Free Software Foundation gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.

# serial 7 ltoptions.m4

# This is to help aclocal find these macros, as it can't see m4_define.
AC_DEFUN([LTOPTIONS_VERSION], [m4_if([1])])


# _LT_MANGLE_OPTION(MACRO-NAME, OPTION-NAME)
# ------------------------------------------
m4_define([_LT_MANGLE_OPTION],
[[_LT_OPTION_]m4_bpatsubst($1__$2, [[^a-zA-Z0-9_]], [_])])


# _LT_SET_OPTION(MACRO-NAME, OPTION-NAME)
# ---------------------------------------
# Set option OPTION-NAME for macro MACRO-NAME, and if there is a
# matching handler defined, dispatch to it.  Other OPTION-NAMEs are
# saved as a flag.
m4_define([_LT_SET_OPTION],
[m4_define(_LT_MANGLE_OPTION([$1], [$2]))dnl
m4_ifdef(_LT_MANGLE_DEFUN([$1], [$2]),
        _LT_MANGLE_DEFUN([$1], [$2]),
    [m4_warning([Unknown $1 option `$2'])])[]dnl
])


# _LT_IF_OPTION(MACRO-NAME, OPTION-NAME, IF-SET, [IF-NOT-SET])
# ------------------------------------------------------------
# Execute IF-SET if OPTION is set, IF-NOT-SET otherwise.
m4_define([_LT_IF_OPTION],
[m4_ifdef(_LT_MANGLE_OPTION([$1], [$2]), [$3], [$4])])


# _LT_UNLESS_OPTIONS(MACRO-NAME, OPTION-LIST, IF-NOT-SET)
# -------------------------------------------------------
# Execute IF-NOT-SET unless all options in OPTION-LIST for MACRO-NAME
# are set.
m4_define([_LT_UNLESS_OPTIONS],
[m4_foreach([_LT_Option], m4_split(m4_normalize([$2])),
	    [m4_ifdef(_LT_MANGLE_OPTION([$1], _LT_Option),
		      [m4_define([$0_found])])])[]dnl
m4_ifdef([$0_found], [m4_undefine([$0_found])], [$3
])[]dnl
])


# _LT_SET_OPTIONS(MACRO-NAME, OPTION-LIST)
# ----------------------------------------
# OPTION-LIST is a space-separated list of Libtool options associated
# with MACRO-NAME.  If any OPTION has a matching handler declared with
# LT_OPTION_DEFINE, dispatch to that macro; otherwise complain about
# the unknown option and exit.
m4_defun([_LT_SET_OPTIONS],
[# Set options
m4_foreach([_LT_Option], m4_split(m4_normalize([$2])),
    [_LT_SET_OPTION([$1], _LT_Option)])

m4_if([$1],[LT_INIT],[
  dnl
  dnl Simply set some default values (i.e off) if boolean options were not
  dnl specified:
  _LT_UNLESS_OPTIONS([LT_INIT], [dlopen], [enable_dlopen=no
  ])
  _LT_UNLESS_OPTIONS([LT_INIT], [win32-dll], [enable_win32_dll=no
  ])
  dnl
  dnl If no reference was made to various pairs of opposing options, then
  dnl we run the default mode handler for the pair.  For example, if neither
  dnl `shared' nor `disable-shared' was passed, we enable building of shared
  dnl archives by default:
  _LT_UNLESS_OPTIONS([LT_INIT], [shared disable-shared], [_LT_ENABLE_SHARED])
  _LT_UNLESS_OPTIONS([LT_INIT], [static disable-static], [_LT_ENABLE_STATIC])
  _LT_UNLESS_OPTIONS([LT_INIT], [pic-only no-pic], [_LT_WITH_PIC])
  _LT_UNLESS_OPTIONS([LT_INIT], [fast-install disable-fast-install],
  		   [_LT_ENABLE_FAST_INSTALL])
  ])
])# _LT_SET_OPTIONS


## --------------------------------- ##
## Macros to handle LT_INIT options. ##
## --------------------------------- ##

# _LT_MANGLE_DEFUN(MACRO-NAME, OPTION-NAME)
# -----------------------------------------
m4_define([_LT_MANGLE_DEFUN],
[[_LT_OPTION_DEFUN_]m4_bpatsubst(m4_toupper([$1__$2]), [[^A-Z0-9_]], [_])])


# LT_OPTION_DEFINE(MACRO-NAME, OPTION-NAME, CODE)
# -----------------------------------------------
m4_define([LT_OPTION_DEFINE],
[m4_define(_LT_MANGLE_DEFUN([$1], [$2]), [$3])[]dnl
])# LT_OPTION_DEFINE


# dlopen
# ------
LT_OPTION_DEFINE([LT_INIT], [dlopen], [enable_dlopen=yes
])

AU_DEFUN([AC_LIBTOOL_DLOPEN],
[_LT_SET_OPTION([LT_INIT], [dlopen])
AC_DIAGNOSE([obsolete],
[$0: Remove this warning and the call to _LT_SET_OPTION when you
put the `dlopen' option into LT_INIT's first parameter.])
])

dnl aclocal-1.4 backwards compatibility:
dnl AC_DEFUN([AC_LIBTOOL_DLOPEN], [])


# win32-dll
# ---------
# Declare package support for building win32 dll's.
LT_OPTION_DEFINE([LT_INIT], [win32-dll],
[enable_win32_dll=yes

case $host in
*-*-cygwin* | *-*-mingw* | *-*-pw32* | *-*-cegcc*)
  AC_CHECK_TOOL(AS, as, false)
  AC_CHECK_TOOL(DLLTOOL, dlltool, false)
  AC_CHECK_TOOL(OBJDUMP, objdump, false)
  ;;
esac

test -z "$AS" && AS=as
_LT_DECL([], [AS],      [1], [Assembler program])dnl

test -z "$DLLTOOL" && DLLTOOL=dlltool
_LT_DECL([], [DLLTOOL], [1], [DLL creation program])dnl

test -z "$OBJDUMP" && OBJDUMP=objdump
_LT_DECL([], [OBJDUMP], [1], [Object dumper program])dnl
])# win32-dll

AU_DEFUN([AC_LIBTOOL_WIN32_DLL],
[AC_REQUIRE([AC_CANONICAL_HOST])dnl
_LT_SET_OPTION([LT_INIT], [win32-dll])
AC_DIAGNOSE([obsolete],
[$0: Remove this warning and the call to _LT_SET_OPTION when you
put the `win32-dll' option into LT_INIT's first parameter.])
])

dnl aclocal-1.4 backwards compatibility:
dnl AC_DEFUN([AC_LIBTOOL_WIN32_DLL], [])


# _LT_ENABLE_SHARED([DEFAULT])
# ----------------------------
# implement the --enable-shared flag, and supports the `shared' and
# `disable-shared' LT_INIT options.
# DEFAULT is either `yes' or `no'.  If omitted, it defaults to `yes'.
m4_define([_LT_ENABLE_SHARED],
[m4_define([_LT_ENABLE_SHARED_DEFAULT], [m4_if($1, no, no, yes)])dnl
AC_ARG_ENABLE([shared],
    [AS_HELP_STRING([--enable-shared@<:@=PKGS@:>@],
	[build shared libraries @<:@default=]_LT_ENABLE_SHARED_DEFAULT[@:>@])],
    [p=${PACKAGE-default}
    case $enableval in
    yes) enable_shared=yes ;;
    no) enable_shared=no ;;
    *)
      enable_shared=no
      # Look at the argument we got.  We use all the common list separators.
      lt_save_ifs="$IFS"; IFS="${IFS}$PATH_SEPARATOR,"
      for pkg in $enableval; do
	IFS="$lt_save_ifs"
	if test "X$pkg" = "X$p"; then
	  enable_shared=yes
	fi
      done
      IFS="$lt_save_ifs"
      ;;
    esac],
    [enable_shared=]_LT_ENABLE_SHARED_DEFAULT)

    _LT_DECL([build_libtool_libs], [enable_shared], [0],
	[Whether or not to build shared libraries])
])# _LT_ENABLE_SHARED

LT_OPTION_DEFINE([LT_INIT], [shared], [_LT_ENABLE_SHARED([yes])])
LT_OPTION_DEFINE([LT_INIT], [disable-shared], [_LT_ENABLE_SHARED([no])])

# Old names:
AC_DEFUN([AC_ENABLE_SHARED],
[_LT_SET_OPTION([LT_INIT], m4_if([$1], [no], [disable-])[shared])
])

AC_DEFUN([AC_DISABLE_SHARED],
[_LT_SET_OPTION([LT_INIT], [disable-shared])
])

AU_DEFUN([AM_ENABLE_SHARED], [AC_ENABLE_SHARED($@)])
AU_DEFUN([AM_DISABLE_SHARED], [AC_DISABLE_SHARED($@)])

dnl aclocal-1.4 backwards compatibility:
dnl AC_DEFUN([AM_ENABLE_SHARED], [])
dnl AC_DEFUN([AM_DISABLE_SHARED], [])



# _LT_ENABLE_STATIC([DEFAULT])
# ----------------------------
# implement the --enable-static flag, and support the `static' and
# `disable-static' LT_INIT options.
# DEFAULT is either `yes' or `no'.  If omitted, it defaults to `yes'.
m4_define([_LT_ENABLE_STATIC],
[m4_define([_LT_ENABLE_STATIC_DEFAULT], [m4_if($1, no, no, yes)])dnl
AC_ARG_ENABLE([static],
    [AS_HELP_STRING([--enable-static@<:@=PKGS@:>@],
	[build static libraries @<:@default=]_LT_ENABLE_STATIC_DEFAULT[@:>@])],
    [p=${PACKAGE-default}
    case $enableval in
    yes) enable_static=yes ;;
    no) enable_static=no ;;
    *)
     enable_static=no
      # Look at the argument we got.  We use all the common list separators.
      lt_save_ifs="$IFS"; IFS="${IFS}$PATH_SEPARATOR,"
      for pkg in $enableval; do
	IFS="$lt_save_ifs"
	if test "X$pkg" = "X$p"; then
	  enable_static=yes
	fi
      done
      IFS="$lt_save_ifs"
      ;;
    esac],
    [enable_static=]_LT_ENABLE_STATIC_DEFAULT)

    _LT_DECL([build_old_libs], [enable_static], [0],
	[Whether or not to build static libraries])
])# _LT_ENABLE_STATIC

LT_OPTION_DEFINE([LT_INIT], [static], [_LT_ENABLE_STATIC([yes])])
LT_OPTION_DEFINE([LT_INIT], [disable-static], [_LT_ENABLE_STATIC([no])])

# Old names:
AC_DEFUN([AC_ENABLE_STATIC],
[_LT_SET_OPTION([LT_INIT], m4_if([$1], [no], [disable-])[static])
])

AC_DEFUN([AC_DISABLE_STATIC],
[_LT_SET_OPTION([LT_INIT], [disable-static])
])

AU_DEFUN([AM_ENABLE_STATIC], [AC_ENABLE_STATIC($@)])
AU_DEFUN([AM_DISABLE_STATIC], [AC_DISABLE_STATIC($@)])

dnl aclocal-1.4 backwards compatibility:
dnl AC_DEFUN([AM_ENABLE_STATIC], [])
dnl AC_DEFUN([AM_DISABLE_STATIC], [])



# _LT_ENABLE_FAST_INSTALL([DEFAULT])
# ----------------------------------
# implement the --enable-fast-install flag, and support the `fast-install'
# and `disable-fast-install' LT_INIT options.
# DEFAULT is either `yes' or `no'.  If omitted, it defaults to `yes'.
m4_define([_LT_ENABLE_FAST_INSTALL],
[m4_define([_LT_ENABLE_FAST_INSTALL_DEFAULT], [m4_if($1, no, no, yes)])dnl
AC_ARG_ENABLE([fast-install],
    [AS_HELP_STRING([--enable-fast-install@<:@=PKGS@:>@],
    [optimize for fast installation @<:@default=]_LT_ENABLE_FAST_INSTALL_DEFAULT[@:>@])],
    [p=${PACKAGE-default}
    case $enableval in
    yes) enable_fast_install=yes ;;
    no) enable_fast_install=no ;;
    *)
      enable_fast_install=no
      # Look at the argument we got.  We use all the common list separators.
      lt_save_ifs="$IFS"; IFS="${IFS}$PATH_SEPARATOR,"
      for pkg in $enableval; do
	IFS="$lt_save_ifs"
	if test "X$pkg" = "X$p"; then
	  enable_fast_install=yes
	fi
      done
      IFS="$lt_save_ifs"
      ;;
    esac],
    [enable_fast_install=]_LT_ENABLE_FAST_INSTALL_DEFAULT)

_LT_DECL([fast_install], [enable_fast_install], [0],
	 [Whether or not to optimize for fast installation])dnl
])# _LT_ENABLE_FAST_INSTALL

LT_OPTION_DEFINE([LT_INIT], [fast-install], [_LT_ENABLE_FAST_INSTALL([yes])])
LT_OPTION_DEFINE([LT_INIT], [disable-fast-install], [_LT_ENABLE_FAST_INSTALL([no])])

# Old names:
AU_DEFUN([AC_ENABLE_FAST_INSTALL],
[_LT_SET_OPTION([LT_INIT], m4_if([$1], [no], [disable-])[fast-install])
AC_DIAGNOSE([obsolete],
[$0: Remove this warning and the call to _LT_SET_OPTION when you put
the `fast-install' option into LT_INIT's first parameter.])
])

AU_DEFUN([AC_DISABLE_FAST_INSTALL],
[_LT_SET_OPTION([LT_INIT], [disable-fast-install])
AC_DIAGNOSE([obsolete],
[$0: Remove this warning and the call to _LT_SET_OPTION when you put
the `disable-fast-install' option into LT_INIT's first parameter.])
])

dnl aclocal-1.4 backwards compatibility:
dnl AC_DEFUN([AC_ENABLE_FAST_INSTALL], [])
dnl AC_DEFUN([AM_DISABLE_FAST_INSTALL], [])


# _LT_WITH_PIC([MODE])
# --------------------
# implement the --with-pic flag, and support the `pic-only' and `no-pic'
# LT_INIT options.
# MODE is either `yes' or `no'.  If omitted, it defaults to `both'.
m4_define([_LT_WITH_PIC],
[AC_ARG_WITH([pic],
    [AS_HELP_STRING([--with-pic@<:@=PKGS@:>@],
	[try to use only PIC/non-PIC objects @<:@default=use both@:>@])],
    [lt_p=${PACKAGE-default}
    case $withval in
    yes|no) pic_mode=$withval ;;
    *)
      pic_mode=default
      # Look at the argument we got.  We use all the common list separators.
      lt_save_ifs="$IFS"; IFS="${IFS}$PATH_SEPARATOR,"
      for lt_pkg in $withval; do
	IFS="$lt_save_ifs"
	if test "X$lt_pkg" = "X$lt_p"; then
	  pic_mode=yes
	fi
      done
      IFS="$lt_save_ifs"
      ;;
    esac],
    [pic_mode=default])

test -z "$pic_mode" && pic_mode=m4_default([$1], [default])

_LT_DECL([], [pic_mode], [0], [What type of objects to build])dnl
])# _LT_WITH_PIC

LT_OPTION_DEFINE([LT_INIT], [pic-only], [_LT_WITH_PIC([yes])])
LT_OPTION_DEFINE([LT_INIT], [no-pic], [_LT_WITH_PIC([no])])

# Old name:
AU_DEFUN([AC_LIBTOOL_PICMODE],
[_LT_SET_OPTION([LT_INIT], [pic-only])
AC_DIAGNOSE([obsolete],
[$0: Remove this warning and the call to _LT_SET_OPTION when you
put the `pic-only' option into LT_INIT's first parameter.])
])

dnl aclocal-1.4 backwards compatibility:
dnl AC_DEFUN([AC_LIBTOOL_PICMODE], [])

## ----------------- ##
## LTDL_INIT Options ##
## ----------------- ##

m4_define([_LTDL_MODE], [])
LT_OPTION_DEFINE([LTDL_INIT], [nonrecursive],
		 [m4_define([_LTDL_MODE], [nonrecursive])])
LT_OPTION_DEFINE([LTDL_INIT], [recursive],
		 [m4_define([_LTDL_MODE], [recursive])])
LT_OPTION_DEFINE([LTDL_INIT], [subproject],
		 [m4_define([_LTDL_MODE], [subproject])])

m4_define([_LTDL_TYPE], [])
LT_OPTION_DEFINE([LTDL_INIT], [installable],
		 [m4_define([_LTDL_TYPE], [installable])])
LT_OPTION_DEFINE([LTDL_INIT], [convenience],
		 [m4_define([_LTDL_TYPE], [convenience])])
