# ===========================================================================
#       https://www.gnu.org/software/autoconf-archive/ax_pkg_swig.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_PKG_SWIG([major.minor.micro], [action-if-found], [action-if-not-found])
#
# DESCRIPTION
#
#   This macro searches for a SWIG installation on your system. If found,
#   then SWIG is AC_SUBST'd; if not found, then $SWIG is empty.  If SWIG is
#   found, then SWIG_LIB is set to the SWIG library path, and AC_SUBST'd.
#
#   You can use the optional first argument to check if the version of the
#   available SWIG is greater than or equal to the value of the argument. It
#   should have the format: N[.N[.N]] (N is a number between 0 and 999. Only
#   the first N is mandatory.) If the version argument is given (e.g.
#   1.3.17), AX_PKG_SWIG checks that the swig package is this version number
#   or higher.
#
#   As usual, action-if-found is executed if SWIG is found, otherwise
#   action-if-not-found is executed.
#
#   In configure.in, use as:
#
#     AX_PKG_SWIG(1.3.17, [], [ AC_MSG_ERROR([SWIG is required to build..]) ])
#     AX_SWIG_ENABLE_CXX
#     AX_SWIG_MULTI_MODULE_SUPPORT
#     AX_SWIG_PYTHON
#
# LICENSE
#
#   Copyright (c) 2008 Sebastian Huber <sebastian-huber@web.de>
#   Copyright (c) 2008 Alan W. Irwin
#   Copyright (c) 2008 Rafael Laboissiere <rafael@laboissiere.net>
#   Copyright (c) 2008 Andrew Collier
#   Copyright (c) 2011 Murray Cumming <murrayc@openismus.com>
#   Copyright (c) 2018 Reini Urban <rurban@cpan.org>
#   Copyright (c) 2021 Vincent Danjean <Vincent.Danjean@ens-lyon.org>
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation; either version 2 of the License, or (at your
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

#serial 15

AC_DEFUN([AX_PKG_SWIG],[
        # Find path to the "swig" executable.
        AC_PATH_PROGS([SWIG],[swig swig3.0 swig2.0])
        if test -z "$SWIG" ; then
                m4_ifval([$3],[$3],[:])
        elif test -z "$1" ; then
                m4_ifval([$2],[$2],[:])
	else
                AC_MSG_CHECKING([SWIG version])
                [swig_version=`$SWIG -version 2>&1 | grep 'SWIG Version' | sed 's/.*\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/g'`]
                AC_MSG_RESULT([$swig_version])
                if test -n "$swig_version" ; then
                        # Calculate the required version number components
                        [required=$1]
                        [required_major=`echo $required | sed 's/[^0-9].*//'`]
                        if test -z "$required_major" ; then
                                [required_major=0]
                        fi
                        [required=`echo $required. | sed 's/[0-9]*[^0-9]//'`]
                        [required_minor=`echo $required | sed 's/[^0-9].*//'`]
                        if test -z "$required_minor" ; then
                                [required_minor=0]
                        fi
                        [required=`echo $required. | sed 's/[0-9]*[^0-9]//'`]
                        [required_patch=`echo $required | sed 's/[^0-9].*//'`]
                        if test -z "$required_patch" ; then
                                [required_patch=0]
                        fi
                        # Calculate the available version number components
                        [available=$swig_version]
                        [available_major=`echo $available | sed 's/[^0-9].*//'`]
                        if test -z "$available_major" ; then
                                [available_major=0]
                        fi
                        [available=`echo $available | sed 's/[0-9]*[^0-9]//'`]
                        [available_minor=`echo $available | sed 's/[^0-9].*//'`]
                        if test -z "$available_minor" ; then
                                [available_minor=0]
                        fi
                        [available=`echo $available | sed 's/[0-9]*[^0-9]//'`]
                        [available_patch=`echo $available | sed 's/[^0-9].*//'`]
                        if test -z "$available_patch" ; then
                                [available_patch=0]
                        fi
                        # Convert the version tuple into a single number for easier comparison.
                        # Using base 100 should be safe since SWIG internally uses BCD values
                        # to encode its version number.
                        required_swig_vernum=`expr $required_major \* 10000 \
                            \+ $required_minor \* 100 \+ $required_patch`
                        available_swig_vernum=`expr $available_major \* 10000 \
                            \+ $available_minor \* 100 \+ $available_patch`

                        if test $available_swig_vernum -lt $required_swig_vernum; then
                                AC_MSG_WARN([SWIG version >= $1 is required.  You have $swig_version.])
                                SWIG=''
                                m4_ifval([$3],[$3],[])
                        else
                                AC_MSG_CHECKING([for SWIG library])
                                SWIG_LIB=`$SWIG -swiglib | tr '\r\n' '  '`
                                AC_MSG_RESULT([$SWIG_LIB])
                                m4_ifval([$2],[$2],[])
                        fi
                else
                        AC_MSG_WARN([cannot determine SWIG version])
                        SWIG=''
                        m4_ifval([$3],[$3],[])
                fi
        fi
        AC_SUBST([SWIG_LIB])
])
