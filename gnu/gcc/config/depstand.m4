##                                                          -*- Autoconf -*-

# Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
# Free Software Foundation, Inc.
#
# This file is free software; the Free Software Foundation
# gives unlimited permission to copy and/or distribute it,
# with or without modifications, as long as this notice is preserved.

# serial 8

# Based on depend.m4 from automake 1.9, modified for standalone use in
# an environment where GNU make is required. 

# ZW_PROG_COMPILER_DEPENDENCIES
# -----------------------------
# Variant of _AM_DEPENDENCIES which just does the dependency probe and
# sets fooDEPMODE accordingly.  Cache-variable compatible with
# original; not side-effect compatible.  As the users of this macro
# may require accurate dependencies for correct builds, it does *not*
# honor --disable-dependency-checking, and failure to detect a usable
# method is an error.  depcomp is assumed to be located in
# $ac_aux_dir.
#
# FIXME: Should use the Autoconf 2.5x language-selection mechanism.

AC_DEFUN([ZW_PROG_COMPILER_DEPENDENCIES],
[ifelse([$1], CC,   [depcc="$CC"   am_compiler_list=],
        [$1], CXX,  [depcc="$CXX"  am_compiler_list=],
        [$1], OBJC, [depcc="$OBJC" am_compiler_list='gcc3 gcc'],
        [$1], GCJ,  [depcc="$GCJ"  am_compiler_list='gcc3 gcc'],
                    [depcc="$$1"   am_compiler_list=])

am_depcomp=$ac_aux_dir/depcomp
AC_CACHE_CHECK([dependency style of $depcc],
               [am_cv_$1_dependencies_compiler_type],
[if test -f "$am_depcomp"; then
  # We make a subdir and do the tests there.  Otherwise we can end up
  # making bogus files that we don't know about and never remove.  For
  # instance it was reported that on HP-UX the gcc test will end up
  # making a dummy file named `D' -- because `-MD' means `put the output
  # in D'.
  mkdir conftest.dir
  # Copy depcomp to subdir because otherwise we won't find it if we're
  # using a relative directory.
  cp "$am_depcomp" conftest.dir
  cd conftest.dir
  # We will build objects and dependencies in a subdirectory because
  # it helps to detect inapplicable dependency modes.  For instance
  # both Tru64's cc and ICC support -MD to output dependencies as a
  # side effect of compilation, but ICC will put the dependencies in
  # the current directory while Tru64 will put them in the object
  # directory.
  mkdir sub

  am_cv_$1_dependencies_compiler_type=none
  if test "$am_compiler_list" = ""; then
     am_compiler_list=`sed -n ['s/^\([a-zA-Z0-9]*\))$/\1/p'] < ./depcomp`
  fi
  for depmode in $am_compiler_list; do
    if test $depmode = none; then break; fi

    _AS_ECHO([$as_me:$LINENO: trying $depmode], AS_MESSAGE_LOG_FD)
    # Setup a source with many dependencies, because some compilers
    # like to wrap large dependency lists on column 80 (with \), and
    # we should not choose a depcomp mode which is confused by this.
    #
    # We need to recreate these files for each test, as the compiler may
    # overwrite some of them when testing with obscure command lines.
    # This happens at least with the AIX C compiler.
    : > sub/conftest.c
    for i in 1 2 3 4 5 6; do
      echo '#include "conftst'$i'.h"' >> sub/conftest.c
      # Using `: > sub/conftst$i.h' creates only sub/conftst1.h with
      # Solaris 8's {/usr,}/bin/sh.
      touch sub/conftst$i.h
    done
    echo "include sub/conftest.Po" > confmf

    # We check with `-c' and `-o' for the sake of the "dashmstdout"
    # mode.  It turns out that the SunPro C++ compiler does not properly
    # handle `-M -o', and we need to detect this.
    depcmd="depmode=$depmode \
       source=sub/conftest.c object=sub/conftest.${OBJEXT-o} \
       depfile=sub/conftest.Po tmpdepfile=sub/conftest.TPo \
       $SHELL ./depcomp $depcc -c -o sub/conftest.${OBJEXT-o} sub/conftest.c"
    echo "| $depcmd" | sed -e 's/  */ /g' >&AS_MESSAGE_LOG_FD
    if env $depcmd > conftest.err 2>&1 &&
       grep sub/conftst6.h sub/conftest.Po >>conftest.err 2>&1 &&
       grep sub/conftest.${OBJEXT-o} sub/conftest.Po >>conftest.err 2>&1 &&
       ${MAKE-make} -s -f confmf >>conftest.err 2>&1; then
      # icc doesn't choke on unknown options, it will just issue warnings
      # or remarks (even with -Werror).  So we grep stderr for any message
      # that says an option was ignored or not supported.
      # When given -MP, icc 7.0 and 7.1 complain thusly:
      #   icc: Command line warning: ignoring option '-M'; no argument required
      # The diagnosis changed in icc 8.0:
      #   icc: Command line remark: option '-MP' not supported
      if (grep 'ignoring option' conftest.err ||
          grep 'not supported' conftest.err) >/dev/null 2>&1; then :; else
        am_cv_$1_dependencies_compiler_type=$depmode
	_AS_ECHO([$as_me:$LINENO: success], AS_MESSAGE_LOG_FD)
        break
      fi
    fi
    _AS_ECHO([$as_me:$LINENO: failure, diagnostics are:], AS_MESSAGE_LOG_FD)
    sed -e 's/^/| /' < conftest.err >&AS_MESSAGE_LOG_FD
  done

  cd ..
  rm -rf conftest.dir
else
  am_cv_$1_dependencies_compiler_type=none
fi
])
if test x${am_cv_$1_dependencies_compiler_type-none} = xnone
then AC_MSG_ERROR([no usable dependency style found])
else AC_SUBST([$1DEPMODE], [depmode=$am_cv_$1_dependencies_compiler_type])
fi
])

# AM_SET_DEPDIR
# -------------
# Choose a directory name for dependency files.
AC_DEFUN([AM_SET_DEPDIR],
[AC_REQUIRE([AM_SET_LEADING_DOT])dnl
AC_SUBST([DEPDIR], ["${am__leading_dot}deps"])dnl
])

# ZW_CREATE_DEPDIR
# ----------------
# As AM_SET_DEPDIR, but also create the directory at config.status time.
AC_DEFUN([ZW_CREATE_DEPDIR],
[AC_REQUIRE([AM_SET_DEPDIR])dnl
AC_CONFIG_COMMANDS([depdir], [mkdir $DEPDIR], [DEPDIR=$DEPDIR])
])
