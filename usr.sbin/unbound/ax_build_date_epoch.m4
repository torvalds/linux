# ===========================================================================
#   https://www.gnu.org/software/autoconf-archive/ax_build_date_epoch.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BUILD_DATE_EPOCH(VARIABLE[, FORMAT[, ACTION-IF-FAIL]])
#
# DESCRIPTION
#
#   Sets VARIABLE to a string representing the current time.  It is
#   formatted according to FORMAT if specified, otherwise it is formatted as
#   the number of seconds (excluding leap seconds) since the UNIX epoch (01
#   Jan 1970 00:00:00 UTC).
#
#   If the SOURCE_DATE_EPOCH environment variable is set, it uses the value
#   of that variable instead of the current time.  See
#   https://reproducible-builds.org/specs/source-date-epoch).  If
#   SOURCE_DATE_EPOCH is set but cannot be properly interpreted as a UNIX
#   timestamp, then execute ACTION-IF-FAIL if specified, otherwise error.
#
#   VARIABLE is AC_SUBST-ed.
#
# LICENSE
#
#   Copyright (c) 2016 Eric Bavier <bavier@member.fsf.org>
#
#   This program is free software: you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <https://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 2

AC_DEFUN([AX_BUILD_DATE_EPOCH],
[dnl
AC_MSG_CHECKING([for build time])
ax_date_fmt="m4_default($2,%s)"
AS_IF([test x"$SOURCE_DATE_EPOCH" = x],
 [$1=`date "+$ax_date_fmt"`],
 [ax_build_date=`date -u -d "@$SOURCE_DATE_EPOCH" "+$ax_date_fmt" 2>/dev/null \
                 || date -u -r "$SOURCE_DATE_EPOCH" "+$ax_date_fmt" 2>/dev/null`
  AS_IF([test x"$ax_build_date" = x],
   [m4_ifval([$3],
      [$3],
      [AC_MSG_ERROR([malformed SOURCE_DATE_EPOCH])])],
   [$1=$ax_build_date])])
AC_MSG_RESULT([$$1])
])dnl AX_BUILD_DATE_EPOCH
