dnl
dnl Initialize basic configure bits, set toplevel_srcdir for Makefiles.
dnl
dnl GLIBCPP_TOPREL_CONFIGURE
AC_DEFUN(GLIBCPP_TOPREL_CONFIGURE, [
  dnl Default to --enable-multilib (this is also passed by default
  dnl from the ubercommon-top-level configure)
  AC_ARG_ENABLE(multilib,
  [  --enable-multilib       build hella library versions (default)],
  [case "${enableval}" in
    yes) multilib=yes ;;
    no)  multilib=no ;;
    *)   AC_MSG_ERROR(bad value ${enableval} for multilib option) ;;
   esac], [multilib=yes])dnl

  # When building with srcdir == objdir, links to the source files will
  # be created in directories within the target_subdir.  We have to
  # adjust toplevel_srcdir accordingly, so that configure finds
  # install-sh and other auxiliary files that live in the top-level
  # source directory.
  if test "${srcdir}" = "."; then
    if test -z "${with_target_subdir}"; then
      toprel=".."
    else
      if test "${with_target_subdir}" != "."; then
        toprel="${with_multisrctop}../.."
      else
        toprel="${with_multisrctop}.."
      fi
    fi
  else
    toprel=".."
  fi
  AC_CONFIG_AUX_DIR(${srcdir}/$toprel)
  toplevel_srcdir=\${top_srcdir}/$toprel
  AC_SUBST(toplevel_srcdir)
])

dnl
dnl Initialize the rest of the library configury.
dnl
dnl GLIBCPP_CONFIGURE
AC_DEFUN(GLIBCPP_CONFIGURE, [
  # Export build and source directories.
  # These need to be absolute paths, yet at the same time need to
  # canonicalize only relative paths, because then amd will not unmount
  # drives. Thus the use of PWDCMD: set it to 'pawd' or 'amq -w' if using amd.
  glibcpp_builddir=`${PWDCMD-pwd}`
  case $srcdir in
  [\\/$]* | ?:[\\/]*) glibcpp_srcdir=${srcdir} ;;
  *) glibcpp_srcdir=`cd "$srcdir" && ${PWDCMD-pwd} || echo "$srcdir"` ;;
  esac
  AC_SUBST(glibcpp_builddir)
  AC_SUBST(glibcpp_srcdir)

  dnl This is here just to satisfy automake.
  ifelse(not,equal,[AC_CONFIG_AUX_DIR(..)])

  AC_PROG_AWK
  # Will set LN_S to either 'ln -s' or 'ln'.  With autoconf 2.5x, can also
  # be 'cp -p' if linking isn't available.  Uncomment the next line to
  # force a particular method.
  #ac_cv_prog_LN_S='cp -p'
  AC_PROG_LN_S

  # We use these options to decide which functions to include.
  AC_ARG_WITH(newlib,
  [  --with-newlib        use newlib headers])
  AC_ARG_WITH(target-subdir,
  [  --with-target-subdir=SUBDIR
                          configuring in a subdirectory])
  AC_ARG_WITH(cross-host,
  [  --with-cross-host=HOST  configuring with a cross compiler])

  glibcpp_basedir=$srcdir/$toprel/$1/libstdc++-v3
  AC_SUBST(glibcpp_basedir)

  # Never versions of autoconf add an underscore to these functions.
  # Prevent future problems ...
  ifdef([AC_PROG_CC_G],[],[define([AC_PROG_CC_G],defn([_AC_PROG_CC_G]))])
  ifdef([AC_PROG_CC_GNU],[],[define([AC_PROG_CC_GNU],defn([_AC_PROG_CC_GNU]))])
  ifdef([AC_PROG_CXX_G],[],[define([AC_PROG_CXX_G],defn([_AC_PROG_CXX_G]))])
  ifdef([AC_PROG_CXX_GNU],[],[define([AC_PROG_CXX_GNU],defn([_AC_PROG_CXX_GNU]))])

  # AC_PROG_CC
  # FIXME: We temporarily define our own version of AC_PROG_CC.  This is
  # copied from autoconf 2.12, but does not call AC_PROG_CC_WORKS.  We
  # are probably using a cross compiler, which will not be able to fully
  # link an executable.  This is addressed in later versions of autoconf.

  AC_DEFUN(LIB_AC_PROG_CC,
  [AC_BEFORE([$0], [AC_PROG_CPP])dnl
  dnl Fool anybody using AC_PROG_CC.
  AC_PROVIDE([AC_PROG_CC])
  AC_CHECK_PROG(CC, gcc, gcc)
  if test -z "$CC"; then
    AC_CHECK_PROG(CC, cc, cc, , , /usr/ucb/cc)
    test -z "$CC" && AC_MSG_ERROR([no acceptable cc found in \$PATH])
  fi

  AC_PROG_CC_GNU

  if test $ac_cv_prog_gcc = yes; then
    GCC=yes
  dnl Check whether -g works, even if CFLAGS is set, in case the package
  dnl plays around with CFLAGS (such as to build both debugging and
  dnl normal versions of a library), tasteless as that idea is.
    ac_test_CFLAGS="${CFLAGS+set}"
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS=
    AC_PROG_CC_G
    if test "$ac_test_CFLAGS" = set; then
      CFLAGS="$ac_save_CFLAGS"
    elif test $ac_cv_prog_cc_g = yes; then
      CFLAGS="-g -O2"
    else
      CFLAGS="-O2"
    fi
  else
    GCC=
    test "${CFLAGS+set}" = set || CFLAGS="-g"
  fi
  ])

  LIB_AC_PROG_CC

  # Likewise for AC_PROG_CXX.  We can't just call it directly because g++
  # will try to link in libstdc++.
  AC_DEFUN(LIB_AC_PROG_CXX,
  [AC_BEFORE([$0], [AC_PROG_CXXCPP])dnl
  dnl Fool anybody using AC_PROG_CXX.
  AC_PROVIDE([AC_PROG_CXX])
  # Use glibcpp_CXX so that we do not cause CXX to be cached with the
  # flags that come in CXX while configuring libstdc++.  They're different
  # from those used for all other target libraries.  If CXX is set in
  # the environment, respect that here.
  glibcpp_CXX=$CXX
  AC_CHECK_PROGS(glibcpp_CXX, $CCC c++ g++ gcc CC cxx cc++, gcc)
  AC_SUBST(glibcpp_CXX)
  CXX=$glibcpp_CXX
  test -z "$glibcpp_CXX" && AC_MSG_ERROR([no acceptable c++ found in \$PATH])

  AC_PROG_CXX_GNU

  if test $ac_cv_prog_gxx = yes; then
    GXX=yes
    dnl Check whether -g works, even if CXXFLAGS is set, in case the package
    dnl plays around with CXXFLAGS (such as to build both debugging and
    dnl normal versions of a library), tasteless as that idea is.
    ac_test_CXXFLAGS="${CXXFLAGS+set}"
    ac_save_CXXFLAGS="$CXXFLAGS"
    CXXFLAGS=
    AC_PROG_CXX_G
    if test "$ac_test_CXXFLAGS" = set; then
      CXXFLAGS="$ac_save_CXXFLAGS"
    elif test $ac_cv_prog_cxx_g = yes; then
      CXXFLAGS="-g -O2"
    else
      CXXFLAGS="-O2"
    fi
  else
    GXX=
    test "${CXXFLAGS+set}" = set || CXXFLAGS="-g"
  fi
  ])

  LIB_AC_PROG_CXX

  # For directory versioning (e.g., headers) and other variables.
  AC_MSG_CHECKING([for GCC version number])
  gcc_version=`$glibcpp_CXX -dumpversion`
  AC_MSG_RESULT($gcc_version)

  # For some reason, gettext needs this.
  AC_ISC_POSIX

  AC_CHECK_TOOL(AS, as)
  AC_CHECK_TOOL(AR, ar)
  AC_CHECK_TOOL(RANLIB, ranlib, ranlib-not-found-in-path-error)
  AC_PROG_INSTALL

  AM_MAINTAINER_MODE

  # We need AC_EXEEXT to keep automake happy in cygnus mode.  However,
  # at least currently, we never actually build a program, so we never
  # need to use $(EXEEXT).  Moreover, the test for EXEEXT normally
  # fails, because we are probably configuring with a cross compiler
  # which can't create executables.  So we include AC_EXEEXT to keep
  # automake happy, but we don't execute it, since we don't care about
  # the result.
  if false; then
    # autoconf 2.50 runs AC_EXEEXT by default, and the macro expands
    # to nothing, so nothing would remain between `then' and `fi' if it
    # were not for the `:' below.
    :
    AC_EXEEXT
  fi

  case [$]{glibcpp_basedir} in
    /* | [A-Za-z]:[\\/]*) libgcj_flagbasedir=[$]{glibcpp_basedir} ;;
    *) glibcpp_flagbasedir='[$](top_builddir)/'[$]{glibcpp_basedir} ;;
  esac

  # Find platform-specific directories containing configuration info.  In
  # addition to possibly modifying the same flags, it also sets up symlinks.
  GLIBCPP_CHECK_TARGET
])


dnl
dnl Check to see if g++ can compile this library, and if so, if any version-
dnl specific precautions need to be taken. 
dnl 
dnl GLIBCPP_CHECK_COMPILER_VERSION
AC_DEFUN(GLIBCPP_CHECK_COMPILER_VERSION, [
if test ! -f stamp-sanity-compiler; then
  AC_MSG_CHECKING([for g++ that will successfully compile libstdc++-v3])
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  AC_TRY_COMPILE(, [
  #if __GNUC__ < 3
    not_ok
  #endif
  ], gpp_satisfactory=yes, AC_MSG_ERROR([please upgrade to GCC 3.0 or above]))
  AC_LANG_RESTORE
  AC_MSG_RESULT($gpp_satisfactory)
  touch stamp-sanity-compiler
fi
])


dnl
dnl Tests for newer compiler features, or features that are present in newer
dnl compiler versions but not older compiler versions still in use, should
dnl be placed here.
dnl
dnl Define WERROR='-Werror' if requested and possible; g++'s that lack the
dnl new inlining code or the new system_header pragma will die on -Werror.
dnl Leave it out by default and use maint-mode to use it.
dnl
dnl Define SECTION_FLAGS='-ffunction-sections -fdata-sections' if
dnl compiler supports it and the user has not requested debug mode.
dnl
dnl GLIBCPP_CHECK_COMPILER_FEATURES
AC_DEFUN(GLIBCPP_CHECK_COMPILER_FEATURES, [
  # All these tests are for C++; save the language and the compiler flags.
  # The CXXFLAGS thing is suspicious, but based on similar bits previously
  # found in GLIBCPP_CONFIGURE.
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"

  # Check for maintainer-mode bits.
  if test x"$USE_MAINTAINER_MODE" = xno; then
    WERROR=''
  else
    WERROR='-Werror'
  fi

  # Check for -ffunction-sections -fdata-sections
  AC_MSG_CHECKING([for g++ that supports -ffunction-sections -fdata-sections])
  CXXFLAGS='-Werror -ffunction-sections -fdata-sections'
  AC_TRY_COMPILE(, [int foo;
  ], [ac_fdsections=yes], [ac_fdsections=no])
  if test "$ac_test_CXXFLAGS" = set; then
    CXXFLAGS="$ac_save_CXXFLAGS"
  else
    # this is the suspicious part
    CXXFLAGS=''
  fi
  if test x"$ac_fdsections" = x"yes"; then
    SECTION_FLAGS='-ffunction-sections -fdata-sections'
  fi
  AC_MSG_RESULT($ac_fdsections)

  AC_LANG_RESTORE
  AC_SUBST(WERROR)
  AC_SUBST(SECTION_FLAGS)
])


dnl
dnl If GNU ld is in use, check to see if tricky linker opts can be used.  If
dnl the native linker is in use, all variables will be defined to something
dnl safe (like an empty string).
dnl
dnl Define SECTION_LDFLAGS='-Wl,--gc-sections' if possible.
dnl Define OPT_LDFLAGS='-Wl,-O1' if possible.
dnl Define LD, with_gnu_ld, and (possibly) glibcpp_gnu_ld_version as
dnl side-effects of testing.
dnl
dnl GLIBCPP_CHECK_LINKER_FEATURES
AC_DEFUN(GLIBCPP_CHECK_LINKER_FEATURES, [
  # If we're not using GNU ld, then there's no point in even trying these
  # tests.  Check for that first.  We should have already tested for gld
  # by now (in libtool), but require it now just to be safe...
  test -z "$SECTION_LDFLAGS" && SECTION_LDFLAGS=''
  test -z "$OPT_LDFLAGS" && OPT_LDFLAGS=''
  AC_REQUIRE([AC_PROG_LD])

  # The name set by libtool depends on the version of libtool.  Shame on us
  # for depending on an impl detail, but c'est la vie.  Older versions used
  # ac_cv_prog_gnu_ld, but now it's lt_cv_prog_gnu_ld, and is copied back on
  # top of with_gnu_ld (which is also set by --with-gnu-ld, so that actually
  # makes sense).  We'll test with_gnu_ld everywhere else, so if that isn't
  # set (hence we're using an older libtool), then set it.
  if test x${with_gnu_ld+set} != xset; then
    if test x${ac_cv_prog_gnu_ld+set} != xset; then
      # We got through "ac_require(ac_prog_ld)" and still not set?  Huh?
      with_gnu_ld=no
    else
      with_gnu_ld=$ac_cv_prog_gnu_ld
    fi
  fi

  # Start by getting the version number.  I think the libtool test already
  # does some of this, but throws away the result.
  changequote(,)
  ldver=`$LD --version 2>/dev/null | head -1 | \
         sed -e 's/GNU ld version \([0-9.][0-9.]*\).*/\1/'`
  changequote([,])
  glibcpp_gnu_ld_version=`echo $ldver | \
         $AWK -F. '{ if (NF<3) [$]3=0; print ([$]1*100+[$]2)*100+[$]3 }'`

  # Set --gc-sections.
  if test "$with_gnu_ld" = "notbroken"; then
    # GNU ld it is!  Joy and bunny rabbits!

    # All these tests are for C++; save the language and the compiler flags.
    # Need to do this so that g++ won't try to link in libstdc++
    ac_test_CFLAGS="${CFLAGS+set}"
    ac_save_CFLAGS="$CFLAGS"
    CFLAGS='-x c++  -Wl,--gc-sections'

    # Check for -Wl,--gc-sections
    # XXX This test is broken at the moment, as symbols required for
    # linking are now in libsupc++ (not built yet.....). In addition, 
    # this test has cored on solaris in the past. In addition,
    # --gc-sections doesn't really work at the moment (keeps on discarding
    # used sections, first .eh_frame and now some of the glibc sections for
    # iconv). Bzzzzt. Thanks for playing, maybe next time.
    AC_MSG_CHECKING([for ld that supports -Wl,--gc-sections])
    AC_TRY_RUN([
     int main(void) 
     {
       try { throw 1; }
       catch (...) { };
       return 0;
     }
    ], [ac_sectionLDflags=yes],[ac_sectionLDflags=no], [ac_sectionLDflags=yes])
    if test "$ac_test_CFLAGS" = set; then
      CFLAGS="$ac_save_CFLAGS"
    else
      # this is the suspicious part
      CFLAGS=''
    fi
    if test "$ac_sectionLDflags" = "yes"; then
      SECTION_LDFLAGS="-Wl,--gc-sections $SECTION_LDFLAGS"
    fi
    AC_MSG_RESULT($ac_sectionLDflags)
  fi

  # Set linker optimization flags.
  if test x"$with_gnu_ld" = x"yes"; then
    OPT_LDFLAGS="-Wl,-O1 $OPT_LDFLAGS"
  fi

  AC_SUBST(SECTION_LDFLAGS)
  AC_SUBST(OPT_LDFLAGS)
])


dnl
dnl Check to see if the (math function) argument passed is
dnl declared when using the c++ compiler
dnl ASSUMES argument is a math function with ONE parameter
dnl
dnl GLIBCPP_CHECK_MATH_DECL_1
AC_DEFUN(GLIBCPP_CHECK_MATH_DECL_1, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>
		      #ifdef HAVE_IEEEFP_H
		      #include <ieeefp.h>
		      #endif
		     ], 
                     [ $1(0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
])

dnl
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl 3) if not, see if 1) and 2) for argument prepended with '_'
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with ONE parameter
dnl
dnl GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1
AC_DEFUN(GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1, [
  GLIBCPP_CHECK_MATH_DECL_1($1)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)    
  else
    GLIBCPP_CHECK_MATH_DECL_1(_$1)
    if test x$glibcpp_cv_func__$1_use = x"yes"; then
      AC_CHECK_FUNCS(_$1)    
    fi
  fi
])


dnl
dnl Like GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1, but does a bunch of
dnl of functions at once.  It's an all-or-nothing check -- either 
dnl HAVE_XYZ is defined for each of the functions, or for none of them.
dnl Doing it this way saves significant configure time.
AC_DEFUN(GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1, [
  AC_MSG_CHECKING([for $1 functions])
  AC_CACHE_VAL(glibcpp_cv_func_$2_use, [
    AC_LANG_SAVE
    AC_LANG_CPLUSPLUS
    AC_TRY_COMPILE([#include <math.h>],
                   [ `for x in $3; do echo "$x (0);"; done` ],
                   [glibcpp_cv_func_$2_use=yes],
                   [glibcpp_cv_func_$2_use=no])
    AC_LANG_RESTORE])
  AC_MSG_RESULT($glibcpp_cv_func_$2_use)
  if test x$glibcpp_cv_func_$2_use = x"yes"; then
    AC_CHECK_FUNCS($3)
  fi
])

dnl
dnl Check to see if the (math function) argument passed is
dnl declared when using the c++ compiler
dnl ASSUMES argument is a math function with TWO parameters
dnl
dnl GLIBCPP_CHECK_MATH_DECL_2
AC_DEFUN(GLIBCPP_CHECK_MATH_DECL_2, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>], 
                     [ $1(0, 0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
])

dnl
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with TWO parameters
dnl
dnl GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2
AC_DEFUN(GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2, [
  GLIBCPP_CHECK_MATH_DECL_2($1)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)    
  else
    GLIBCPP_CHECK_MATH_DECL_2(_$1)
    if test x$glibcpp_cv_func__$1_use = x"yes"; then
      AC_CHECK_FUNCS(_$1)    
    fi
  fi
])


dnl
dnl Check to see if the (math function) argument passed is
dnl declared when using the c++ compiler
dnl ASSUMES argument is a math function with THREE parameters
dnl
dnl GLIBCPP_CHECK_MATH_DECL_3
AC_DEFUN(GLIBCPP_CHECK_MATH_DECL_3, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>], 
                     [ $1(0, 0, 0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
])

dnl
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with THREE parameters
dnl
dnl GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_3
AC_DEFUN(GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_3, [
  GLIBCPP_CHECK_MATH_DECL_3($1)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)    
  else
    GLIBCPP_CHECK_MATH_DECL_3(_$1)
    if test x$glibcpp_cv_func__$1_use = x"yes"; then
      AC_CHECK_FUNCS(_$1)    
    fi
  fi
])


dnl
dnl Check to see if the (stdlib function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with TWO parameters
dnl
dnl GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_2
AC_DEFUN(GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_2, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <stdlib.h>], 
                     [ $1(0, 0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)    
  fi
])


dnl
dnl Check to see if the (stdlib function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a function with THREE parameters
dnl
dnl GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_3
AC_DEFUN(GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_3, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <stdlib.h>], 
                     [ $1(0, 0, 0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)    
  fi
])

dnl
dnl Check to see if the (unistd function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a function with ONE parameter
dnl
dnl GLIBCPP_CHECK_UNISTD_DECL_AND_LINKAGE_1
AC_DEFUN(GLIBCPP_CHECK_UNISTD_DECL_AND_LINKAGE_1, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <unistd.h>], 
                     [ $1(0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_CHECK_FUNCS($1)    
  fi
])

dnl
dnl Because the builtins are picky picky picky about the arguments they take, 
dnl do an explict linkage tests here.
dnl Check to see if the (math function) argument passed is
dnl 1) declared when using the c++ compiler
dnl 2) has "C" linkage
dnl
dnl Define HAVE_CARGF etc if "cargf" is declared and links
dnl
dnl argument 1 is name of function to check
dnl
dnl ASSUMES argument is a math function with ONE parameter
dnl
dnl GLIBCPP_CHECK_BUILTIN_MATH_DECL_LINKAGE_1
AC_DEFUN(GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1, [
  AC_MSG_CHECKING([for $1 declaration])
  if test x${glibcpp_cv_func_$1_use+set} != xset; then
    AC_CACHE_VAL(glibcpp_cv_func_$1_use, [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([#include <math.h>], 
                     [ $1(0);], 
                     [glibcpp_cv_func_$1_use=yes], [glibcpp_cv_func_$1_use=no])
      AC_LANG_RESTORE
    ])
  fi
  AC_MSG_RESULT($glibcpp_cv_func_$1_use)
  if test x$glibcpp_cv_func_$1_use = x"yes"; then
    AC_MSG_CHECKING([for $1 linkage])
    if test x${glibcpp_cv_func_$1_link+set} != xset; then
      AC_CACHE_VAL(glibcpp_cv_func_$1_link, [
        AC_TRY_LINK([#include <math.h>], 
                    [ $1(0);], 
                    [glibcpp_cv_func_$1_link=yes], [glibcpp_cv_func_$1_link=no])
      ])
    fi
    AC_MSG_RESULT($glibcpp_cv_func_$1_link)
    if test x$glibcpp_cv_func_$1_link = x"yes"; then
      ac_tr_func=HAVE_`echo $1 | tr 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'`
      AC_DEFINE_UNQUOTED(${ac_tr_func})
    fi
  fi
])


dnl
dnl Check to see what builtin math functions are supported
dnl
dnl check for __builtin_abs
dnl check for __builtin_fabsf
dnl check for __builtin_fabs
dnl check for __builtin_fabl
dnl check for __builtin_labs
dnl check for __builtin_sqrtf
dnl check for __builtin_sqrtl
dnl check for __builtin_sqrt
dnl check for __builtin_sinf
dnl check for __builtin_sin
dnl check for __builtin_sinl
dnl check for __builtin_cosf
dnl check for __builtin_cos
dnl check for __builtin_cosl
dnl
dnl GLIBCPP_CHECK_BUILTIN_MATH_SUPPORT
AC_DEFUN(GLIBCPP_CHECK_BUILTIN_MATH_SUPPORT, [
  dnl Test for builtin math functions.
  dnl These are made in gcc/c-common.c 
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_abs)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_fabsf)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_fabs)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_fabsl)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_labs)

  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sqrtf)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sqrt)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sqrtl)

  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sinf)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sin)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_sinl)

  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_cosf)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_cos)
  GLIBCPP_CHECK_BUILTIN_MATH_DECL_AND_LINKAGE_1(__builtin_cosl)

  dnl There is, without a doubt, a more elegant way to have these
  dnl names exported so that they won't be stripped out of acconfig.h by
  dnl autoheader. I leave this as an exercise to somebody less frustrated
  dnl than I.... please email the libstdc++ list if you can figure out a
  dnl more elegant approach (see autoconf/acgen.m4 and specifically
  dnl AC_CHECK_FUNC for things to steal.)
  dummyvar=no
  if test x$dummyvar = x"yes"; then
    AC_DEFINE(HAVE___BUILTIN_ABS)
    AC_DEFINE(HAVE___BUILTIN_LABS)
    AC_DEFINE(HAVE___BUILTIN_COS)
    AC_DEFINE(HAVE___BUILTIN_COSF)
    AC_DEFINE(HAVE___BUILTIN_COSL)
    AC_DEFINE(HAVE___BUILTIN_FABS)
    AC_DEFINE(HAVE___BUILTIN_FABSF)
    AC_DEFINE(HAVE___BUILTIN_FABSL)
    AC_DEFINE(HAVE___BUILTIN_SIN)
    AC_DEFINE(HAVE___BUILTIN_SINF)
    AC_DEFINE(HAVE___BUILTIN_SINL)
    AC_DEFINE(HAVE___BUILTIN_SQRT)
    AC_DEFINE(HAVE___BUILTIN_SQRTF)
    AC_DEFINE(HAVE___BUILTIN_SQRTL)
  fi
])

dnl
dnl Check to see what the underlying c library is like
dnl These checks need to do two things: 
dnl 1) make sure the name is declared when using the c++ compiler
dnl 2) make sure the name has "C" linkage
dnl This might seem like overkill but experience has shown that it's not...
dnl
dnl Define HAVE_STRTOLD if "strtold" is declared and links
dnl Define HAVE_STRTOF if "strtof" is declared and links
dnl Define HAVE_DRAND48 if "drand48" is declared and links
dnl
dnl GLIBCPP_CHECK_STDLIB_SUPPORT
AC_DEFUN(GLIBCPP_CHECK_STDLIB_SUPPORT, [
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS='-fno-builtin -D_GNU_SOURCE'

  GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_2(strtold)
  GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_2(strtof)
  AC_CHECK_FUNCS(drand48)

  CXXFLAGS="$ac_save_CXXFLAGS"
])

dnl
dnl Check to see what the underlying c library is like
dnl These checks need to do two things: 
dnl 1) make sure the name is declared when using the c++ compiler
dnl 2) make sure the name has "C" linkage
dnl This might seem like overkill but experience has shown that it's not...
dnl
dnl Define HAVE_ISATTY if "isatty" is declared and links
dnl
dnl GLIBCPP_CHECK_UNISTD_SUPPORT
AC_DEFUN(GLIBCPP_CHECK_UNISTD_SUPPORT, [
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS='-fno-builtins -D_GNU_SOURCE'

  GLIBCPP_CHECK_UNISTD_DECL_AND_LINKAGE_1(isatty)
  
  CXXFLAGS="$ac_save_CXXFLAGS"
])

dnl
dnl Check to see what the underlying c library or math library is like.
dnl These checks need to do two things: 
dnl 1) make sure the name is declared when using the c++ compiler
dnl 2) make sure the name has "C" linkage
dnl This might seem like overkill but experience has shown that it's not...
dnl
dnl Define HAVE_CARGF etc if "cargf" is found.
dnl
dnl GLIBCPP_CHECK_MATH_SUPPORT
AC_DEFUN(GLIBCPP_CHECK_MATH_SUPPORT, [
  ac_test_CXXFLAGS="${CXXFLAGS+set}"
  ac_save_CXXFLAGS="$CXXFLAGS"
  CXXFLAGS='-fno-builtin -D_GNU_SOURCE'

  dnl Check libm
  AC_CHECK_LIB(m, sin, libm="-lm")
  ac_save_LIBS="$LIBS"
  LIBS="$LIBS $libm"

  dnl Check to see if certain C math functions exist.
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(isinf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(isnan)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(finite)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(copysign)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_3(sincos)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(fpclass)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(qfpclass)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(hypot)

  dnl Check to see if basic C math functions have float versions.
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(float trig,
                                          float_trig,
                                          acosf asinf atanf \
                                          cosf sinf tanf \
                                          coshf sinhf tanhf)
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(float round,
                                          float_round,
                                          ceilf floorf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(expf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(isnanf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(isinff)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(atan2f)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(fabsf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(fmodf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(frexpf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(hypotf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(ldexpf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(logf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(log10f)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(modff)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(powf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(sqrtf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_3(sincosf)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(finitef)

  dnl Check to see if basic C math functions have long double versions.
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(long double trig,
                                          long_double_trig,
                                          acosl asinl atanl \
                                          cosl sinl tanl \
                                          coshl sinhl tanhl)
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(long double round,
                                          long_double_round,
                                          ceill floorl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(isnanl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(isinfl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(copysignl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(atan2l)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(expl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(fabsl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(fmodl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(frexpl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(hypotl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(ldexpl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(logl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(log10l)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(modfl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_2(powl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(sqrtl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_3(sincosl)
  GLIBCPP_CHECK_MATH_DECL_AND_LINKAGE_1(finitel)

  dnl Some runtimes have these functions with a preceding underscore. Please
  dnl keep this sync'd with the one above. And if you add any new symbol,
  dnl please add the corresponding block in the @BOTTOM@ section of acconfig.h.
  dnl Check to see if certain C math functions exist.

  dnl Check to see if basic C math functions have float versions.
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(_float trig,
                                          _float_trig,
                                          _acosf _asinf _atanf \
                                          _cosf _sinf _tanf \
                                          _coshf _sinhf _tanhf)
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(_float round,
                                          _float_round,
                                          _ceilf _floorf)

  dnl Check to see if basic C math functions have long double versions.
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(_long double trig,
                                          _long_double_trig,
                                          _acosl _asinl _atanl \
                                          _cosl _sinl _tanl \
                                          _coshl _sinhl _tanhl)
  GLIBCPP_CHECK_MATH_DECLS_AND_LINKAGES_1(_long double round,
                                          _long_double_round,
                                          _ceill _floorl)

  LIBS="$ac_save_LIBS"
  CXXFLAGS="$ac_save_CXXFLAGS"
])


dnl
dnl Check to see if there is native support for complex 
dnl
dnl Don't compile bits in math/* if native support exits.
dnl
dnl Define USE_COMPLEX_LONG_DOUBLE etc if "copysignl" is found.
dnl
dnl GLIBCPP_CHECK_COMPLEX_MATH_SUPPORT
AC_DEFUN(GLIBCPP_CHECK_COMPLEX_MATH_SUPPORT, [
  dnl Check for complex versions of math functions of platform.
  AC_CHECK_LIB(m, main)
  AC_REPLACE_MATHFUNCS(nan copysignf)

  dnl For __signbit to signbit conversions.
  AC_CHECK_FUNCS([__signbit], , [LIBMATHOBJS="$LIBMATHOBJS signbit.lo"])
  AC_CHECK_FUNCS([__signbitf], , [LIBMATHOBJS="$LIBMATHOBJS signbitf.lo"])

  dnl Compile the long double complex functions only if the function 
  dnl provides the non-complex long double functions that are needed.
  dnl Currently this includes copysignl, which should be
  dnl cached from the GLIBCPP_CHECK_MATH_SUPPORT macro, above.
  if test x$ac_cv_func_copysignl = x"yes"; then
    AC_CHECK_FUNCS([__signbitl], , [LIBMATHOBJS="$LIBMATHOBJS signbitl.lo"])
  fi

  if test -n "$LIBMATHOBJS"; then
    need_libmath=yes
  fi
  AC_SUBST(LIBMATHOBJS)
  AM_CONDITIONAL(GLIBCPP_BUILD_LIBMATH,  test "$need_libmath" = yes)
])


dnl Check to see what architecture and operating system we are compiling
dnl for.  Also, if architecture- or OS-specific flags are required for
dnl compilation, pick them up here.
dnl 
dnl GLIBCPP_CHECK_TARGET
AC_DEFUN(GLIBCPP_CHECK_TARGET, [
  . [$]{glibcpp_basedir}/configure.target
  AC_MSG_RESULT(CPU config directory is $cpu_include_dir)
  AC_MSG_RESULT(OS config directory is $os_include_dir)
])


dnl
dnl Check to see if this target can enable the wchar_t parts of libstdc++.
dnl If --disable-c-mbchar was given, no wchar_t stuff is enabled.  (This
dnl must have been previously checked.)
dnl
dnl Define _GLIBCPP_USE_WCHAR_T if all the bits are found 
dnl Define HAVE_MBSTATE_T if mbstate_t is not in wchar.h
dnl
dnl GLIBCPP_CHECK_WCHAR_T_SUPPORT
AC_DEFUN(GLIBCPP_CHECK_WCHAR_T_SUPPORT, [
  dnl Wide characters disabled by default.
  enable_wchar_t=no

  dnl Test wchar.h for mbstate_t, which is needed for char_traits and
  dnl others even if wchar_t support is not on.
  AC_MSG_CHECKING([for mbstate_t])
  AC_TRY_COMPILE([#include <wchar.h>],
  [mbstate_t teststate;], 
  have_mbstate_t=yes, have_mbstate_t=no)
  AC_MSG_RESULT($have_mbstate_t)
  if test x"$have_mbstate_t" = xyes; then
    AC_DEFINE(HAVE_MBSTATE_T)
  fi

  dnl Sanity check for existence of ISO C99 headers for extended encoding.
  AC_CHECK_HEADERS(wchar.h, ac_has_wchar_h=yes, ac_has_wchar_h=no)
  AC_CHECK_HEADERS(wctype.h, ac_has_wctype_h=yes, ac_has_wctype_h=no)
  
  dnl Only continue checking if the ISO C99 headers exist and support is on.
  if test x"$ac_has_wchar_h" = xyes &&
     test x"$ac_has_wctype_h" = xyes &&
     test x"$enable_c_mbchar" != xno; then
      
    dnl Test wchar.h for WCHAR_MIN, WCHAR_MAX, which is needed before
    dnl numeric_limits can instantiate type_traits<wchar_t>
    AC_MSG_CHECKING([for WCHAR_MIN and WCHAR_MAX])
    AC_TRY_COMPILE([#include <wchar.h>],
    [int i = WCHAR_MIN; int j = WCHAR_MAX;], 
    has_wchar_minmax=yes, has_wchar_minmax=no)
    AC_MSG_RESULT($has_wchar_minmax)
    
    dnl Test wchar.h for WEOF, which is what we use to determine whether
    dnl to specialize for char_traits<wchar_t> or not.
    AC_MSG_CHECKING([for WEOF])
    AC_TRY_COMPILE([
      #include <wchar.h>
      #include <stddef.h>],
    [wint_t i = WEOF;],
    has_weof=yes, has_weof=no)
    AC_MSG_RESULT($has_weof)
  
    dnl Tests for wide character functions used in char_traits<wchar_t>.
    ac_wfuncs=yes
    AC_CHECK_FUNCS(wcslen wmemchr wmemcmp wmemcpy wmemmove wmemset,, \
    ac_wfuncs=no)
  
    dnl Checks for names injected into std:: by the c_std headers.
    AC_CHECK_FUNCS(btowc wctob fgetwc fgetws fputwc fputws fwide \
    fwprintf fwscanf swprintf swscanf vfwprintf vfwscanf vswprintf vswscanf \
    vwprintf vwscanf wprintf wscanf getwc getwchar mbsinit mbrlen mbrtowc \
    mbsrtowcs wcsrtombs putwc putwchar ungetwc wcrtomb wcstod wcstof wcstol \
    wcstoul wcscpy wcsncpy wcscat wcsncat wcscmp wcscoll wcsncmp wcsxfrm \
    wcscspn wcsspn wcstok wcsftime wcschr wcspbrk wcsrchr wcsstr,, \
    ac_wfuncs=no)

    AC_MSG_CHECKING([for ISO C99 wchar_t support])
    if test x"$has_weof" = xyes &&
       test x"$has_wchar_minmax" = xyes &&
       test x"$ac_wfuncs" = xyes; then
      ac_isoC99_wchar_t=yes
    else
      ac_isoC99_wchar_t=no
    fi
    AC_MSG_RESULT($ac_isoC99_wchar_t)
  
    dnl Use iconv for wchar_t to char conversions. As such, check for 
    dnl X/Open Portability Guide, version 2 features (XPG2).
    AC_CHECK_HEADER(iconv.h, ac_has_iconv_h=yes, ac_has_iconv_h=no)
    AC_CHECK_HEADER(langinfo.h, ac_has_langinfo_h=yes, ac_has_langinfo_h=no)

    dnl Check for existence of libiconv.a providing XPG2 wchar_t support.
    AC_CHECK_LIB(iconv, iconv, libiconv="-liconv")
    ac_save_LIBS="$LIBS"
    LIBS="$LIBS $libiconv"

    AC_CHECK_FUNCS(iconv_open iconv_close iconv nl_langinfo, \
    ac_XPG2funcs=yes, ac_XPG2funcs=no)
  
    LIBS="$ac_save_LIBS"

    AC_MSG_CHECKING([for XPG2 wchar_t support])
    if test x"$ac_has_iconv_h" = xyes &&
       test x"$ac_has_langinfo_h" = xyes &&
       test x"$ac_XPG2funcs" = xyes; then
      ac_XPG2_wchar_t=yes
    else
      ac_XPG2_wchar_t=no
    fi
    AC_MSG_RESULT($ac_XPG2_wchar_t)
  
    dnl At the moment, only enable wchar_t specializations if all the
    dnl above support is present.
    if test x"$ac_isoC99_wchar_t" = xyes &&
       test x"$ac_XPG2_wchar_t" = xyes; then
       AC_DEFINE(_GLIBCPP_USE_WCHAR_T)
       enable_wchar_t=yes 
    fi
  fi
  AC_MSG_CHECKING([for enabled wchar_t specializations])
  AC_MSG_RESULT($enable_wchar_t)	
  AM_CONDITIONAL(GLIBCPP_TEST_WCHAR_T, test "$enable_wchar_t" = yes)	
])


dnl
dnl Check to see if debugging libraries are to be built.
dnl
dnl GLIBCPP_ENABLE_DEBUG
dnl
dnl --enable-debug
dnl builds a separate set of debugging libraries in addition to the
dnl normal (shared, static) libstdc++ binaries.
dnl
dnl --disable-debug
dnl builds only one (non-debug) version of libstdc++.
dnl
dnl --enable-debug-flags=FLAGS
dnl iff --enable-debug == yes, then use FLAGS to build the debug library.
dnl
dnl  +  Usage:  GLIBCPP_ENABLE_DEBUG[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.  If ommitted, it
dnl       defaults to `no'.
AC_DEFUN(GLIBCPP_ENABLE_DEBUG, [dnl
define([GLIBCPP_ENABLE_DEBUG_DEFAULT], ifelse($1, yes, yes, no))dnl
AC_ARG_ENABLE(debug,
changequote(<<, >>)dnl
<<  --enable-debug          build extra debug library [default=>>GLIBCPP_ENABLE_DEBUG_DEFAULT],
changequote([, ])dnl
[case "${enableval}" in
 yes) enable_debug=yes ;;
 no)  enable_debug=no ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable extra debugging]) ;;
 esac],
enable_debug=GLIBCPP_ENABLE_DEBUG_DEFAULT)dnl
AC_MSG_CHECKING([for additional debug build])
AC_MSG_RESULT($enable_debug)
AM_CONDITIONAL(GLIBCPP_BUILD_DEBUG, test "$enable_debug" = yes)
])


dnl Check for explicit debug flags.
dnl
dnl GLIBCPP_ENABLE_DEBUG_FLAGS
dnl
dnl --enable-debug-flags='-O1'
dnl is a general method for passing flags to be used when
dnl building debug libraries with --enable-debug.
dnl
dnl --disable-debug-flags does nothing.
dnl  +  Usage:  GLIBCPP_ENABLE_DEBUG_FLAGS(default flags)
dnl       If "default flags" is an empty string (or "none"), the effect is
dnl       the same as --disable or --enable=no.
AC_DEFUN(GLIBCPP_ENABLE_DEBUG_FLAGS, [dnl
define([GLIBCPP_ENABLE_DEBUG_FLAGS_DEFAULT], ifelse($1,,, $1))dnl
AC_ARG_ENABLE(debug_flags,
changequote(<<, >>)dnl
<<  --enable-debug-flags=FLAGS    pass compiler FLAGS when building debug
	                library;[default=>>GLIBCPP_ENABLE_DEBUG_FLAGS_DEFAULT],
changequote([, ])dnl
[case "${enableval}" in
 none)  ;;
 -*) enable_debug_flags="${enableval}" ;;
 *)   AC_MSG_ERROR([Unknown argument to extra debugging flags]) ;;
 esac],
enable_debug_flags=GLIBCPP_ENABLE_DEBUG_FLAGS_DEFAULT)dnl

dnl Option parsed, now set things appropriately
case x"$enable_debug" in
    xyes)
        case "$enable_debug_flags" in
	  none)
            DEBUG_FLAGS="-g3 -O0";;
	  -*) #valid input
	    DEBUG_FLAGS="${enableval}"
        esac
        ;;
    xno)
        DEBUG_FLAGS=""
        ;;
esac
AC_SUBST(DEBUG_FLAGS)

AC_MSG_CHECKING([for debug build flags])
AC_MSG_RESULT($DEBUG_FLAGS)
])


dnl
dnl Check for "unusual" flags to pass to the compiler while building.
dnl
dnl GLIBCPP_ENABLE_CXX_FLAGS
dnl --enable-cxx-flags='-foo -bar -baz' is a general method for passing
dnl     experimental flags such as -fhonor-std, -fsquangle, -Dfloat=char, etc.
dnl     Somehow this same set of flags must be passed when [re]building
dnl     libgcc.
dnl --disable-cxx-flags passes nothing.
dnl  +  See http://gcc.gnu.org/ml/libstdc++/2000-q2/msg00131.html
dnl         http://gcc.gnu.org/ml/libstdc++/2000-q2/msg00284.html
dnl         http://gcc.gnu.org/ml/libstdc++/2000-q1/msg00035.html
dnl  +  Usage:  GLIBCPP_ENABLE_CXX_FLAGS(default flags)
dnl       If "default flags" is an empty string (or "none"), the effect is
dnl       the same as --disable or --enable=no.
AC_DEFUN(GLIBCPP_ENABLE_CXX_FLAGS, [dnl
define([GLIBCPP_ENABLE_CXX_FLAGS_DEFAULT], ifelse($1,,, $1))dnl
AC_MSG_CHECKING([for extra compiler flags for building])
AC_ARG_ENABLE(cxx_flags,
changequote(<<, >>)dnl
<<  --enable-cxx-flags=FLAGS      pass compiler FLAGS when building library;
                                  [default=>>GLIBCPP_ENABLE_CXX_FLAGS_DEFAULT],
changequote([, ])dnl
[case "x$enable_cxx_flags" in
  xyes)
    AC_MSG_ERROR([--enable-cxx-flags needs compiler flags as arguments]) ;;
  xno | xnone | x)
    enable_cxx_flags='' ;;
  *)
    enable_cxx_flags="$enableval" ;;
esac],
enable_cxx_flags=GLIBCPP_ENABLE_CXX_FLAGS_DEFAULT)

dnl Run through flags (either default or command-line) and set anything
dnl extra (e.g., #defines) that must accompany particular g++ options.
if test -n "$enable_cxx_flags"; then
  for f in $enable_cxx_flags; do
    case "$f" in
      -fhonor-std)  ;;
      -*)  ;;
      *)   # and we're trying to pass /what/ exactly?
           AC_MSG_ERROR([compiler flags start with a -]) ;;
    esac
  done
fi
EXTRA_CXX_FLAGS="$enable_cxx_flags"
AC_MSG_RESULT($EXTRA_CXX_FLAGS)
AC_SUBST(EXTRA_CXX_FLAGS)
])


dnl
dnl Check for which locale library to use:  gnu or generic.
dnl
dnl GLIBCPP_ENABLE_CLOCALE
dnl --enable-clocale=gnu sets config/locale/c_locale_gnu.cc and friends
dnl --enable-clocale=generic sets config/locale/c_locale_generic.cc and friends
dnl 
dnl default is generic
dnl
AC_DEFUN(GLIBCPP_ENABLE_CLOCALE, [
  AC_MSG_CHECKING([for clocale to use])
  AC_ARG_ENABLE(clocale,
  [  --enable-clocale        enable model for target locale package. 
  --enable-clocale=MODEL  use MODEL target-speific locale package. [default=generic]
  ], 
  if test x$enable_clocale = xno; then
     enable_clocale=no
  fi,
     enable_clocale=no)

  enable_clocale_flag=$enable_clocale

  dnl Probe for locale support if no specific model is specified.
  dnl Default to "generic"
  if test x$enable_clocale_flag = xno; then
    case x${target_os} in
      xlinux* | xgnu*)
	AC_EGREP_CPP([_GLIBCPP_ok], [
        #include <features.h>
        #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2) 
          _GLIBCPP_ok
        #endif
        ], enable_clocale_flag=gnu, enable_clocale_flag=generic)

	# Test for bugs early in glibc-2.2.x series
  	if test x$enable_clocale_flag = xgnu; then
    	  AC_TRY_RUN([
	  #define _GNU_SOURCE 1
	  #include <locale.h>
	  #include <string.h>
	  #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
	  extern __typeof(newlocale) __newlocale;
	  extern __typeof(duplocale) __duplocale;
	  extern __typeof(strcoll_l) __strcoll_l;
	  #endif
	  int main()
	  {
  	    const char __one[] = "Äuglein Augmen";
  	    const char __two[] = "Äuglein";
  	    int i;
  	    int j;
  	    __locale_t	loc;
   	    __locale_t	loc_dup;
  	    loc = __newlocale(1 << LC_ALL, "de_DE", 0);
  	    loc_dup = __duplocale(loc);
  	    i = __strcoll_l(__one, __two, loc);
  	    j = __strcoll_l(__one, __two, loc_dup);
  	    return 0;
	  }
	  ], 
	  [enable_clocale_flag=gnu],[enable_clocale_flag=generic],
	  [enable_clocale_flag=generic])
  	fi

	# ... at some point put __strxfrm_l tests in as well.
        ;;
      *)
	enable_clocale_flag=generic
	;;
    esac
  fi

  dnl Deal with gettext issues.
  AC_ARG_ENABLE(nls,
  [  --enable-nls            use Native Language Support (default)],
  , enable_nls=yes)
  USE_NLS=no

  dnl Set configure bits for specified locale package
  case x${enable_clocale_flag} in
    xgeneric)
      AC_MSG_RESULT(generic)

      CLOCALE_H=config/locale/generic/c_locale.h
      CLOCALE_CC=config/locale/generic/c_locale.cc
      CCODECVT_H=config/locale/generic/codecvt_specializations.h
      CCODECVT_CC=config/locale/generic/codecvt_members.cc
      CCOLLATE_CC=config/locale/generic/collate_members.cc
      CCTYPE_CC=config/locale/generic/ctype_members.cc
      CMESSAGES_H=config/locale/generic/messages_members.h
      CMESSAGES_CC=config/locale/generic/messages_members.cc
      CMONEY_CC=config/locale/generic/monetary_members.cc
      CNUMERIC_CC=config/locale/generic/numeric_members.cc
      CTIME_H=config/locale/generic/time_members.h
      CTIME_CC=config/locale/generic/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/generic/c++locale_internal.h
      ;;
    xgnu)
      AC_MSG_RESULT(gnu)

      # Declare intention to use gettext, and add support for specific
      # languages.
      # For some reason, ALL_LINGUAS has to be before AM-GNU-GETTEXT
      ALL_LINGUAS="de fr"

      # Don't call AM-GNU-GETTEXT here. Instead, assume glibc.
      AC_CHECK_PROG(check_msgfmt, msgfmt, yes, no)
      if test x"$check_msgfmt" = x"yes" && test x"$enable_nls" = x"yes"; then
	USE_NLS=yes
      fi
      # Export the build objects.
      for ling in $ALL_LINGUAS; do \
        glibcpp_MOFILES="$glibcpp_MOFILES $ling.mo"; \
        glibcpp_POFILES="$glibcpp_POFILES $ling.po"; \
      done
      AC_SUBST(glibcpp_MOFILES)
      AC_SUBST(glibcpp_POFILES)

      CLOCALE_H=config/locale/gnu/c_locale.h
      CLOCALE_CC=config/locale/gnu/c_locale.cc
      CCODECVT_H=config/locale/ieee_1003.1-2001/codecvt_specializations.h
      CCODECVT_CC=config/locale/gnu/codecvt_members.cc
      CCOLLATE_CC=config/locale/gnu/collate_members.cc
      CCTYPE_CC=config/locale/gnu/ctype_members.cc
      CMESSAGES_H=config/locale/gnu/messages_members.h
      CMESSAGES_CC=config/locale/gnu/messages_members.cc
      CMONEY_CC=config/locale/gnu/monetary_members.cc
      CNUMERIC_CC=config/locale/gnu/numeric_members.cc
      CTIME_H=config/locale/gnu/time_members.h
      CTIME_CC=config/locale/gnu/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/gnu/c++locale_internal.h
      ;;
    xieee_1003.1-2001)
      AC_MSG_RESULT(generic)

      CLOCALE_H=config/locale/ieee_1003.1-2001/c_locale.h
      CLOCALE_CC=config/locale/ieee_1003.1-2001/c_locale.cc
      CCODECVT_H=config/locale/ieee_1003.1-2001/codecvt_specializations.h
      CCODECVT_CC=config/locale/generic/codecvt_members.cc
      CCOLLATE_CC=config/locale/generic/collate_members.cc
      CCTYPE_CC=config/locale/generic/ctype_members.cc
      CMESSAGES_H=config/locale/ieee_1003.1-2001/messages_members.h
      CMESSAGES_CC=config/locale/ieee_1003.1-2001/messages_members.cc
      CMONEY_CC=config/locale/generic/monetary_members.cc
      CNUMERIC_CC=config/locale/generic/numeric_members.cc
      CTIME_H=config/locale/generic/time_members.h
      CTIME_CC=config/locale/generic/time_members.cc
      CLOCALE_INTERNAL_H=config/locale/generic/c++locale_internal.h
      ;;
    *)
      echo "$enable_clocale is an unknown locale package" 1>&2
      exit 1
      ;;
  esac

  # This is where the testsuite looks for locale catalogs, using the
  # -DLOCALEDIR define during testsuite compilation.
  glibcpp_localedir=${glibcpp_builddir}/po/share/locale
  AC_SUBST(glibcpp_localedir)

  AC_SUBST(USE_NLS)
  AC_SUBST(CLOCALE_H)
  AC_SUBST(CCODECVT_H)
  AC_SUBST(CMESSAGES_H)
  AC_SUBST(CCODECVT_CC)
  AC_SUBST(CCOLLATE_CC)
  AC_SUBST(CCTYPE_CC)
  AC_SUBST(CMESSAGES_CC)
  AC_SUBST(CMONEY_CC)
  AC_SUBST(CNUMERIC_CC)
  AC_SUBST(CTIME_H)
  AC_SUBST(CTIME_CC)
  AC_SUBST(CLOCALE_CC)
  AC_SUBST(CLOCALE_INTERNAL_H)
])


dnl
dnl Check for which I/O library to use:  libio, or something specific.
dnl
dnl GLIBCPP_ENABLE_CSTDIO
dnl --enable-cstdio=libio sets config/io/c_io_libio.h and friends
dnl 
dnl default is stdio
dnl
AC_DEFUN(GLIBCPP_ENABLE_CSTDIO, [
  AC_MSG_CHECKING([for cstdio to use])
  AC_ARG_ENABLE(cstdio,
  [  --enable-cstdio         enable stdio for target io package. 
  --enable-cstdio=LIB     use LIB target-speific io package. [default=stdio]
  ], 
  if test x$enable_cstdio = xno; then
     enable_cstdio=stdio
  fi,
     enable_cstdio=stdio)

  enable_cstdio_flag=$enable_cstdio

  dnl Check if a valid I/O package
  case x${enable_cstdio_flag} in
    xlibio)
      CSTDIO_H=config/io/c_io_libio.h
      BASIC_FILE_H=config/io/basic_file_libio.h
      BASIC_FILE_CC=config/io/basic_file_libio.cc
      AC_MSG_RESULT(libio)

      # see if we are on a system with libio native (ie, linux)
      AC_CHECK_HEADER(libio.h,  has_libio=yes, has_libio=no)

      # Need to check and see what version of glibc is being used. If
      # it's not glibc-2.2 or higher, then we'll need to go ahead and 
      # compile most of libio for linux systems.
      if test x$has_libio = x"yes"; then
        case "$target" in
          *-*-linux*)
              AC_MSG_CHECKING([for glibc version >= 2.2])
              AC_EGREP_CPP([ok], [
            #include <features.h>
              #if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2) 
                    ok
              #endif
              ], glibc_satisfactory=yes, glibc_satisfactory=no)
              AC_MSG_RESULT($glibc_satisfactory)
            ;;
        esac

        # XXX at the moment, admit defeat and force the recompilation
        # XXX of glibc even on glibc-2.2 systems, because libio is not synched.
        glibc_satisfactory=no        

        if test x$glibc_satisfactory = x"yes"; then
           need_libio=no
           need_wlibio=no        
        else
           need_libio=yes
           # bkoz XXX need to add checks to enable this
           # pme XXX here's a first pass at such a check
           if test x$enable_c_mbchar != xno; then
              need_wlibio=yes
           else
              need_wlibio=no
           fi
        fi

      else
         # Using libio, but <libio.h> doesn't exist on the target system. . .
         need_libio=yes
         # bkoz XXX need to add checks to enable this
         # pme XXX here's a first pass at such a check
         if test x$enable_c_mbchar != xno; then
             need_wlibio=yes
         else
             need_wlibio=no
         fi
      fi
      ;;
    xstdio | x | xno | xnone | xyes)
      # default
      CSTDIO_H=config/io/c_io_stdio.h
      BASIC_FILE_H=config/io/basic_file_stdio.h
      BASIC_FILE_CC=config/io/basic_file_stdio.cc
      AC_MSG_RESULT(stdio)

      # We're not using stdio.
      need_libio=no
      need_wlibio=no
      ;;
    *)
      echo "$enable_cstdio is an unknown io package" 1>&2
      exit 1
      ;;
  esac
  AC_SUBST(CSTDIO_H)
  AC_SUBST(BASIC_FILE_H)
  AC_SUBST(BASIC_FILE_CC)

  # 2000-08-04 bkoz hack
  CCODECVT_C=config/io/c_io_libio_codecvt.c
  AC_SUBST(CCODECVT_C)
  # 2000-08-04 bkoz hack

  AM_CONDITIONAL(GLIBCPP_BUILD_LIBIO,
                 test "$need_libio" = yes || test "$need_wlibio" = yes)
  AM_CONDITIONAL(GLIBCPP_NEED_LIBIO, test "$need_libio" = yes)
  AM_CONDITIONAL(GLIBCPP_NEED_WLIBIO, test "$need_wlibio" = yes)
  if test "$need_libio" = yes || test "$need_wlibio" = yes; then
    libio_la=../libio/libio.la
  else
    libio_la=
  fi
  AC_SUBST(libio_la)
])


dnl
dnl Setup to use the gcc gthr.h thread-specific memory and mutex model.
dnl We must stage the required headers so that they will be installed
dnl with the library (unlike libgcc, the STL implementation is provided
dnl solely within headers).  Since we must not inject random user-space
dnl macro names into user-provided C++ code, we first stage into <file>-in
dnl and process to <file> with an output command.  The reason for a two-
dnl stage process here is to correctly handle $srcdir!=$objdir without
dnl having to write complex code (the sed commands to clean the macro
dnl namespace are complex and fragile enough as it is).  We must also
dnl add a relative path so that -I- is supported properly.
dnl
AC_DEFUN(GLIBCPP_ENABLE_THREADS, [
  AC_MSG_CHECKING([for thread model used by GCC])
  target_thread_file=`$CC -v 2>&1 | sed -n 's/^Thread model: //p'`
  AC_MSG_RESULT([$target_thread_file])

  if test $target_thread_file != single; then
    AC_DEFINE(HAVE_GTHR_DEFAULT)
    AC_DEFINE(_GLIBCPP_SUPPORTS_WEAK, __GXX_WEAK__)
  fi

  glibcpp_thread_h=gthr-$target_thread_file.h
  AC_SUBST(glibcpp_thread_h)
])


dnl
dnl Check for exception handling support.  If an explicit enable/disable
dnl sjlj exceptions is given, we don't have to detect.  Otherwise the
dnl target may or may not support call frame exceptions.
dnl
dnl GLIBCPP_ENABLE_SJLJ_EXCEPTIONS
dnl --enable-sjlj-exceptions forces the use of builtin setjmp.
dnl --disable-sjlj-exceptions forces the use of call frame unwinding.
dnl
dnl Define _GLIBCPP_SJLJ_EXCEPTIONS if the compiler is configured for it.
dnl
AC_DEFUN(GLIBCPP_ENABLE_SJLJ_EXCEPTIONS, [
  AC_MSG_CHECKING([for exception model to use])
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS
  AC_ARG_ENABLE(sjlj-exceptions,
  [  --enable-sjlj-exceptions  force use of builtin_setjmp for exceptions],
  [:],
  [dnl Botheration.  Now we've got to detect the exception model.
   dnl Link tests against libgcc.a are problematic since -- at least
   dnl as of this writing -- we've not been given proper -L bits for
   dnl single-tree newlib and libgloss.
   dnl
   dnl This is what AC_TRY_COMPILE would do if it didn't delete the
   dnl conftest files before we got a change to grep them first.
   cat > conftest.$ac_ext << EOF
[#]line __oline__ "configure"
struct S { ~S(); };
void bar();
void foo()
{
  S s;
  bar();
}
EOF
   old_CXXFLAGS="$CXXFLAGS"  
   CXXFLAGS=-S
   if AC_TRY_EVAL(ac_compile); then
     if grep _Unwind_SjLj_Resume conftest.s >/dev/null 2>&1 ; then
       enable_sjlj_exceptions=yes
     elif grep _Unwind_Resume conftest.s >/dev/null 2>&1 ; then
       enable_sjlj_exceptions=no
     fi
   fi
   CXXFLAGS="$old_CXXFLAGS"
   rm -f conftest*])
   if test x$enable_sjlj_exceptions = xyes; then
     AC_DEFINE(_GLIBCPP_SJLJ_EXCEPTIONS, 1,
        [Define if the compiler is configured for setjmp/longjmp exceptions.])
     ac_exception_model_name=sjlj
   elif test x$enable_sjlj_exceptions = xno; then
     ac_exception_model_name="call frame"
   else
     AC_MSG_ERROR([unable to detect exception model])
   fi
   AC_LANG_RESTORE
   AC_MSG_RESULT($ac_exception_model_name)
])


dnl
dnl Check for libunwind exception handling support. If enabled then
dnl we assume that the _Unwind_* functions that make up the Unwind ABI
dnl (_Unwind_RaiseException, _Unwind_Resume, etc.) are defined by
dnl libunwind instead of libgcc and that libstdc++ has a dependency
dnl on libunwind as well as libgcc.
dnl
dnl GLIBCPP_ENABLE_LIBUNWIND_EXCEPTIONS
dnl --enable-libunwind-exceptions forces the use of libunwind.
dnl --disable-libunwind-exceptions assumes there is no libunwind.
dnl
dnl Define _GLIBCPP_LIBUNWIND_EXCEPTIONS if requested.
dnl
AC_DEFUN(GLIBCPP_ENABLE_LIBUNWIND_EXCEPTIONS, [
  AC_MSG_CHECKING([for use of libunwind])
  AC_ARG_ENABLE(libunwind-exceptions,
  [  --enable-libunwind-exceptions  force use of libunwind for exceptions],
  use_libunwind_exceptions=$enableval,
  use_libunwind_exceptions=no)
  AC_MSG_RESULT($use_libunwind_exceptions)
  dnl Option parsed, now set things appropriately
  if test x"$use_libunwind_exceptions" = xyes; then
    LIBUNWIND_FLAG="-lunwind"
  else
    LIBUNWIND_FLAG=""
  fi
  AC_SUBST(LIBUNWIND_FLAG)
])

dnl
dnl Check for ISO/IEC 9899:1999 "C99" support.
dnl
dnl GLIBCPP_ENABLE_C99
dnl --enable-c99 defines _GLIBCPP_USE_C99
dnl --disable-c99 leaves _GLIBCPP_USE_C99 undefined
dnl  +  Usage:  GLIBCPP_ENABLE_C99[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.  If omitted, it
dnl       defaults to `no'.
dnl  +  If 'C99' stuff is not available, ignores DEFAULT and sets `no'.
dnl
dnl GLIBCPP_ENABLE_C99
AC_DEFUN(GLIBCPP_ENABLE_C99, [dnl
  define([GLIBCPP_ENABLE_C99_DEFAULT], ifelse($1, yes, yes, no))dnl

  AC_ARG_ENABLE(c99,
  changequote(<<, >>)dnl
  <<--enable-c99            turns on 'ISO/IEC 9899:1999 support' [default=>>GLIBCPP_ENABLE_C99_DEFAULT],
  changequote([, ])dnl
  [case "$enableval" in
   yes) enable_c99=yes ;;
   no)  enable_c99=no ;;
   *)   AC_MSG_ERROR([Unknown argument to enable/disable C99]) ;;
   esac],
  enable_c99=GLIBCPP_ENABLE_C99_DEFAULT)dnl
 
  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS

  # Check for the existence of <math.h> functions used if C99 is enabled.
  ac_c99_math=yes;
  AC_MSG_CHECKING([for ISO C99 support in <math.h>])
  AC_TRY_COMPILE([#include <math.h>],[fpclassify(0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[isfinite(0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[isinf(0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[isnan(0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[isnormal(0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[signbit(0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[isgreater(0.0,0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],
                 [isgreaterequal(0.0,0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[isless(0.0,0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],[islessequal(0.0,0.0);],,[ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],
	         [islessgreater(0.0,0.0);],, [ac_c99_math=no])
  AC_TRY_COMPILE([#include <math.h>],
	         [isunordered(0.0,0.0);],, [ac_c99_math=no])
  AC_MSG_RESULT($ac_c99_math)

  # Check for the existence in <stdio.h> of vscanf, et. al.
  ac_c99_stdio=yes;
  AC_MSG_CHECKING([for ISO C99 support in <stdio.h>])
  AC_TRY_COMPILE([#include <stdio.h>],
		 [snprintf("12", 0, "%i");],, [ac_c99_stdio=no])
  AC_TRY_COMPILE([#include <stdio.h>
		  #include <stdarg.h>
		  void foo(char* fmt, ...)
		  {va_list args; va_start(args, fmt);
	          vfscanf(stderr, "%i", args);}],
	          [],, [ac_c99_stdio=no])
  AC_TRY_COMPILE([#include <stdio.h>
		  #include <stdarg.h>
		  void foo(char* fmt, ...)
		  {va_list args; va_start(args, fmt);
	          vscanf("%i", args);}],
	          [],, [ac_c99_stdio=no])
  AC_TRY_COMPILE([#include <stdio.h>
		  #include <stdarg.h>
		  void foo(char* fmt, ...)
		  {va_list args; va_start(args, fmt);
	          vsnprintf(fmt, 0, "%i", args);}],
	          [],, [ac_c99_stdio=no])
  AC_TRY_COMPILE([#include <stdio.h>
		  #include <stdarg.h>
		  void foo(char* fmt, ...)
		  {va_list args; va_start(args, fmt);
	          vsscanf(fmt, "%i", args);}],
	          [],, [ac_c99_stdio=no])
  AC_MSG_RESULT($ac_c99_stdio)

  # Check for the existence in <stdlib.h> of lldiv_t, et. al.
  ac_c99_stdlib=yes;
  AC_MSG_CHECKING([for lldiv_t declaration])
  AC_CACHE_VAL(ac_c99_lldiv_t, [
  AC_TRY_COMPILE([#include <stdlib.h>], 
                   [ lldiv_t mydivt;], 
                   [ac_c99_lldiv_t=yes], [ac_c99_lldiv_t=no])
  ])
  AC_MSG_RESULT($ac_c99_lldiv_t)

  AC_MSG_CHECKING([for ISO C99 support in <stdlib.h>])
  AC_TRY_COMPILE([#include <stdlib.h>],
	         [char* tmp; strtof("gnu", &tmp);],, [ac_c99_stdlib=no])
  AC_TRY_COMPILE([#include <stdlib.h>],
	         [char* tmp; strtold("gnu", &tmp);],, [ac_c99_stdlib=no])
  AC_TRY_COMPILE([#include <stdlib.h>], [llabs(10);],, [ac_c99_stdlib=no])
  AC_TRY_COMPILE([#include <stdlib.h>], [lldiv(10,1);],, [ac_c99_stdlib=no])
  AC_TRY_COMPILE([#include <stdlib.h>], [atoll("10");],, [ac_c99_stdlib=no])
  AC_TRY_COMPILE([#include <stdlib.h>], [_Exit(0);],, [ac_c99_stdlib=no])
  if test x"$ac_c99_lldiv_t" = x"no"; then
    ac_c99_stdlib=no; 
  fi; 
  AC_MSG_RESULT($ac_c99_stdlib)

  # Check for the existence of <wchar.h> functions used if C99 is enabled.
  # XXX the wchar.h checks should be rolled into the general C99 bits.
  ac_c99_wchar=yes;
  AC_MSG_CHECKING([for additional ISO C99 support in <wchar.h>])
  AC_TRY_COMPILE([#include <wchar.h>], 
	         [wcstold(L"10.0", NULL);],, [ac_c99_wchar=no])
  AC_TRY_COMPILE([#include <wchar.h>], 
	         [wcstoll(L"10", NULL, 10);],, [ac_c99_wchar=no])
  AC_TRY_COMPILE([#include <wchar.h>], 
	         [wcstoull(L"10", NULL, 10);],, [ac_c99_wchar=no])
  AC_MSG_RESULT($ac_c99_wchar)

  AC_MSG_CHECKING([for enabled ISO C99 support])
  if test x"$ac_c99_math" = x"no" ||
     test x"$ac_c99_stdio" = x"no" ||
     test x"$ac_c99_stdlib" = x"no" ||
     test x"$ac_c99_wchar" = x"no"; then
    enable_c99=no; 
  fi; 
  AC_MSG_RESULT($enable_c99)

  # Option parsed, now set things appropriately
  if test x"$enable_c99" = x"yes"; then
    AC_DEFINE(_GLIBCPP_USE_C99)
  fi

  AC_LANG_RESTORE
])


dnl
dnl Check for template specializations for the 'long long' type extension.
dnl The result determines only whether 'long long' I/O is enabled; things
dnl like numeric_limits<> specializations are always available.
dnl
dnl GLIBCPP_ENABLE_LONG_LONG
dnl --enable-long-long defines _GLIBCPP_USE_LONG_LONG
dnl --disable-long-long leaves _GLIBCPP_USE_LONG_LONG undefined
dnl  +  Usage:  GLIBCPP_ENABLE_LONG_LONG[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.  If omitted, it
dnl       defaults to `no'.
dnl  +  If 'long long' stuff is not available, ignores DEFAULT and sets `no'.
dnl
dnl GLIBCPP_ENABLE_LONG_LONG
AC_DEFUN(GLIBCPP_ENABLE_LONG_LONG, [dnl
  define([GLIBCPP_ENABLE_LONG_LONG_DEFAULT], ifelse($1, yes, yes, no))dnl

  AC_ARG_ENABLE(long-long,
  changequote(<<, >>)dnl
  <<--enable-long-long      turns on 'long long' [default=>>GLIBCPP_ENABLE_LONG_LONG_DEFAULT],
  changequote([, ])dnl
  [case "$enableval" in
   yes) enable_long_long=yes ;;
   no)  enable_long_long=no ;;
   *)   AC_MSG_ERROR([Unknown argument to enable/disable long long]) ;;
   esac],
  enable_long_long=GLIBCPP_ENABLE_LONG_LONG_DEFAULT)dnl

  AC_LANG_SAVE
  AC_LANG_CPLUSPLUS

  AC_MSG_CHECKING([for enabled long long I/O support])
  # iostreams require strtoll, strtoull to compile
  AC_TRY_COMPILE([#include <stdlib.h>],
                 [char* tmp; strtoll("gnu", &tmp, 10);],,[enable_long_long=no])
  AC_TRY_COMPILE([#include <stdlib.h>],
                 [char* tmp; strtoull("gnu", &tmp, 10);],,[enable_long_long=no])

  # Option parsed, now set things appropriately
  if test x"$enable_long_long" = xyes; then
    AC_DEFINE(_GLIBCPP_USE_LONG_LONG)
  fi
  AC_MSG_RESULT($enable_long_long)

  AC_LANG_RESTORE
])


dnl
dnl Check for what type of C headers to use.
dnl
dnl GLIBCPP_ENABLE_CHEADERS
dnl --enable-cheaders= [does stuff].
dnl --disable-cheaders [does not do anything, really].
dnl  +  Usage:  GLIBCPP_ENABLE_CHEADERS[(DEFAULT)]
dnl       Where DEFAULT is either `c' or `c_std'.
dnl       If ommitted, it defaults to `c_std'.
AC_DEFUN(GLIBCPP_ENABLE_CHEADERS, [dnl
define([GLIBCPP_ENABLE_CHEADERS_DEFAULT], ifelse($1, c_std, c_std, c_std))dnl
AC_MSG_CHECKING([for c header strategy to use])
AC_ARG_ENABLE(cheaders,
changequote(<<, >>)dnl
<<  --enable-cheaders=MODEL       construct "C" header files for g++ [default=>>GLIBCPP_ENABLE_CHEADERS_DEFAULT],
changequote([, ])
  [case "$enableval" in
   c) 
        enable_cheaders=c 
        ;;
   c_std)  
        enable_cheaders=c_std 
        ;;
   *)   AC_MSG_ERROR([Unknown argument to enable/disable "C" headers]) 
        ;;
  esac],
  enable_cheaders=GLIBCPP_ENABLE_CHEADERS_DEFAULT)
  AC_MSG_RESULT($enable_cheaders)

  dnl Option parsed, now set things appropriately
  case "$enable_cheaders" in
    c_std)   
        C_INCLUDE_DIR='${glibcpp_srcdir}/include/c_std'
        ;;
    c)   
        C_INCLUDE_DIR='${glibcpp_srcdir}/include/c'
        ;;
  esac

  AC_SUBST(C_INCLUDE_DIR)
  AM_CONDITIONAL(GLIBCPP_C_HEADERS_C, test "$enable_cheaders" = c)
  AM_CONDITIONAL(GLIBCPP_C_HEADERS_C_STD, test "$enable_cheaders" = c_std)
  AM_CONDITIONAL(GLIBCPP_C_HEADERS_COMPATIBILITY, test "$c_compatibility" = yes)
])


dnl
dnl Check for wide character support.  Has the same effect as the option
dnl in gcc's configure, but in a form that autoconf can mess with.
dnl
dnl GLIBCPP_ENABLE_C_MBCHAR
dnl --enable-c-mbchar requests all the wchar_t stuff.
dnl --disable-c-mbchar doesn't.
dnl  +  Usage:  GLIBCPP_ENABLE_C_MBCHAR[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.  If ommitted, it
dnl       defaults to `no'.
AC_DEFUN(GLIBCPP_ENABLE_C_MBCHAR, [dnl
define([GLIBCPP_ENABLE_C_MBCHAR_DEFAULT], ifelse($1, yes, yes, no))dnl
AC_ARG_ENABLE(c-mbchar,
changequote(<<, >>)dnl
<<  --enable-c-mbchar       enable multibyte (wide) characters [default=>>GLIBCPP_ENABLE_C_MBCHAR_DEFAULT],
changequote([, ])dnl
[case "$enableval" in
 yes) enable_c_mbchar=yes ;;
 no)  enable_c_mbchar=no ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable c-mbchar]) ;;
 esac],
enable_c_mbchar=GLIBCPP_ENABLE_C_MBCHAR_DEFAULT)dnl
dnl Option parsed, now other scripts can test enable_c_mbchar for yes/no.
])


dnl
dnl Set up *_INCLUDES and *_INCLUDE_DIR variables for all sundry Makefile.am's.
dnl
dnl TOPLEVEL_INCLUDES
dnl LIBMATH_INCLUDES
dnl LIBSUPCXX_INCLUDES
dnl LIBIO_INCLUDES
dnl
dnl GLIBCPP_EXPORT_INCLUDES
AC_DEFUN(GLIBCPP_EXPORT_INCLUDES, [
  # Root level of the build directory include sources.
  GLIBCPP_INCLUDES="-I${glibcpp_builddir}/include/${target_alias} -I${glibcpp_builddir}/include"

  # Passed down for canadian crosses.
  if test x"$CANADIAN" = xyes; then
    TOPLEVEL_INCLUDES='-I$(includedir)'
  fi

  LIBMATH_INCLUDES='-I$(top_srcdir)/libmath'

  LIBSUPCXX_INCLUDES='-I$(top_srcdir)/libsupc++'

  if test x"$need_libio" = xyes; then
    LIBIO_INCLUDES='-I$(top_builddir)/libio -I$(top_srcdir)/libio'
    AC_SUBST(LIBIO_INCLUDES)
  fi

  # Now, export this to all the little Makefiles....
  AC_SUBST(GLIBCPP_INCLUDES)
  AC_SUBST(TOPLEVEL_INCLUDES)
  AC_SUBST(LIBMATH_INCLUDES)
  AC_SUBST(LIBSUPCXX_INCLUDES)
])


dnl
dnl Set up *_FLAGS and *FLAGS variables for all sundry Makefile.am's.
dnl
AC_DEFUN(GLIBCPP_EXPORT_FLAGS, [
  # Optimization flags that are probably a good idea for thrill-seekers. Just
  # uncomment the lines below and make, everything else is ready to go... 
  # OPTIMIZE_CXXFLAGS = -O3 -fstrict-aliasing -fvtable-gc 
  OPTIMIZE_CXXFLAGS=
  AC_SUBST(OPTIMIZE_CXXFLAGS)

  WARN_FLAGS='-Wall -Wno-format -W -Wwrite-strings'
  AC_SUBST(WARN_FLAGS)
])

dnl
dnl  GLIBCPP_EXPORT_INSTALL_INFO
dnl  calculates gxx_install_dir
dnl  exports glibcpp_toolexecdir
dnl  exports glibcpp_toolexeclibdir
dnl  exports glibcpp_prefixdir
dnl
dnl Assumes cross_compiling bits already done, and with_cross_host in
dnl particular
dnl
dnl GLIBCPP_EXPORT_INSTALL_INFO
AC_DEFUN(GLIBCPP_EXPORT_INSTALL_INFO, [
# Assumes glibcpp_builddir, glibcpp_srcdir are alreay set up and
# exported correctly in GLIBCPP_CONFIGURE.
glibcpp_toolexecdir=no
glibcpp_toolexeclibdir=no
glibcpp_prefixdir=${prefix}

# Process the option --with-gxx-include-dir=<path to include-files directory>
AC_MSG_CHECKING([for --with-gxx-include-dir])
AC_ARG_WITH(gxx-include-dir,
[  --with-gxx-include-dir  the installation directory for include files],
[case "${withval}" in
  yes)
    AC_MSG_ERROR(Missing directory for --with-gxx-include-dir)
    gxx_include_dir=no
    ;;
  no)
    gxx_include_dir=no
    ;;
  *)
    gxx_include_dir=${withval}
    ;;
esac], [gxx_include_dir=no])
AC_MSG_RESULT($gxx_include_dir)

# Process the option "--enable-version-specific-runtime-libs"
AC_MSG_CHECKING([for --enable-version-specific-runtime-libs])
AC_ARG_ENABLE(version-specific-runtime-libs,
[  --enable-version-specific-runtime-libs    Specify that runtime libraries should be installed in a compiler-specific directory ],
[case "$enableval" in
 yes) version_specific_libs=yes ;;
 no)  version_specific_libs=no ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable version-specific libs]);;
 esac],
version_specific_libs=no)dnl
# Option set, now we can test it.
AC_MSG_RESULT($version_specific_libs)

# Default case for install directory for include files.
if test $version_specific_libs = no && test $gxx_include_dir = no; then
  gxx_include_dir='$(prefix)'/include/c++/${gcc_version}
fi

# Version-specific runtime libs processing.
if test $version_specific_libs = yes; then
  # Need the gcc compiler version to know where to install libraries
  # and header files if --enable-version-specific-runtime-libs option
  # is selected.
  if test x"$gxx_include_dir" = x"no"; then
    gxx_include_dir='$(libdir)/gcc-lib/$(target_alias)/'${gcc_version}/include/c++
  fi
  glibcpp_toolexecdir='$(libdir)/gcc-lib/$(target_alias)'
  glibcpp_toolexeclibdir='$(toolexecdir)/'${gcc_version}'$(MULTISUBDIR)'
fi

# Calculate glibcpp_toolexecdir, glibcpp_toolexeclibdir
# Install a library built with a cross compiler in tooldir, not libdir.
if test x"$glibcpp_toolexecdir" = x"no"; then 
  if test -n "$with_cross_host" &&
     test x"$with_cross_host" != x"no"; then
    glibcpp_toolexecdir='$(exec_prefix)/$(target_alias)'
    glibcpp_toolexeclibdir='$(toolexecdir)/lib'
  else
    glibcpp_toolexecdir='$(libdir)/gcc-lib/$(target_alias)'
    glibcpp_toolexeclibdir='$(libdir)'
  fi
  glibcpp_toolexeclibdir=$glibcpp_toolexeclibdir/`$CC -print-multi-os-directory`
fi

AC_MSG_CHECKING([for install location])
AC_MSG_RESULT($gxx_include_dir)

AC_SUBST(glibcpp_prefixdir)
AC_SUBST(gxx_include_dir)
AC_SUBST(glibcpp_toolexecdir)
AC_SUBST(glibcpp_toolexeclibdir)
])


# Check for functions in math library.
# Ulrich Drepper <drepper@cygnus.com>, 1998.
#
# This file can be copied and used freely without restrictions.  It can
# be used in projects which are not available under the GNU Public License
# but which still want to provide support for the GNU gettext functionality.
# Please note that the actual code is *not* freely available.

# serial 1

dnl AC_REPLACE_MATHFUNCS(FUNCTION...)
AC_DEFUN(AC_REPLACE_MATHFUNCS,
[AC_CHECK_FUNCS([$1], , [LIBMATHOBJS="$LIBMATHOBJS ${ac_func}.lo"])])


dnl This macro searches for a GNU version of make.  If a match is found, the
dnl makefile variable `ifGNUmake' is set to the empty string, otherwise it is
dnl set to "#". This is useful for  including a special features in a Makefile,
dnl which cannot be handled by other versions of make.  The variable
dnl _cv_gnu_make_command is set to the command to invoke GNU make if it exists,
dnl the empty string otherwise.
dnl
dnl Here is an example of its use:
dnl
dnl Makefile.in might contain:
dnl
dnl     # A failsafe way of putting a dependency rule into a makefile
dnl     $(DEPEND):
dnl             $(CC) -MM $(srcdir)/*.c > $(DEPEND)
dnl
dnl     @ifGNUmake@ ifeq ($(DEPEND),$(wildcard $(DEPEND)))
dnl     @ifGNUmake@ include $(DEPEND)
dnl     @ifGNUmake@ endif
dnl
dnl Then configure.in would normally contain:
dnl
dnl     CHECK_GNU_MAKE()
dnl     AC_OUTPUT(Makefile)
dnl
dnl Then perhaps to cause gnu make to override any other make, we could do
dnl something like this (note that GNU make always looks for GNUmakefile first):
dnl
dnl     if  ! test x$_cv_gnu_make_command = x ; then
dnl             mv Makefile GNUmakefile
dnl             echo .DEFAULT: > Makefile ;
dnl             echo \  $_cv_gnu_make_command \$@ >> Makefile;
dnl     fi
dnl
dnl Then, if any (well almost any) other make is called, and GNU make also
dnl exists, then the other make wraps the GNU make.
dnl
dnl @author John Darrington <j.darrington@elvis.murdoch.edu.au>
dnl @version 1.1 #### replaced Id string now that Id is for lib-v3; pme
dnl
dnl #### Changes for libstdc++-v3:  reformatting and linewrapping; prepending
dnl #### GLIBCPP_ to the macro name; adding the :-make fallback in the
dnl #### conditional's subshell (" --version" is not a command), using a
dnl #### different option to grep(1).
dnl #### -pme
dnl #### Fixed Bourne shell portability bug (use ${MAKE-make}, not
dnl #### ${MAKE:-make}).
dnl #### -msokolov
AC_DEFUN(
  GLIBCPP_CHECK_GNU_MAKE, [AC_CACHE_CHECK( for GNU make,_cv_gnu_make_command,
          _cv_gnu_make_command='' ;
dnl Search all the common names for GNU make
          for a in "${MAKE-make}" make gmake gnumake ; do
                  if ( $a --version 2> /dev/null | grep -c GNU > /dev/null )
                  then
                          _cv_gnu_make_command=$a ;
                          break;
                  fi
          done ;
  ) ;
dnl If there was a GNU version, then set @ifGNUmake@ to the empty
dnl string, '#' otherwise
  if test  "x$_cv_gnu_make_command" != "x"  ; then
          ifGNUmake='' ;
  else
          ifGNUmake='#' ;
  fi
  AC_SUBST(ifGNUmake)
])


dnl Check for headers for, and arguments to, the setrlimit() function.
dnl Used only in testsuite_hooks.h.
AC_DEFUN(GLIBCPP_CHECK_SETRLIMIT_ancilliary, [
  AC_TRY_COMPILE([#include <unistd.h>
                  #include <sys/time.h>
                  #include <sys/resource.h>
                 ], [ int f = RLIMIT_$1 ; ],
                 [glibcpp_mresult=1], [glibcpp_mresult=0])
  AC_DEFINE_UNQUOTED(HAVE_MEMLIMIT_$1, $glibcpp_mresult,
                     [Only used in build directory testsuite_hooks.h.])
])
AC_DEFUN(GLIBCPP_CHECK_SETRLIMIT, [
  setrlimit_have_headers=yes
  AC_CHECK_HEADERS(unistd.h sys/time.h sys/resource.h,
                   [],
                   setrlimit_have_headers=no)
  # If don't have the headers, then we can't run the tests now, and we
  # won't be seeing any of these during testsuite compilation.
  if test $setrlimit_have_headers = yes; then
    # Can't do these in a loop, else the resulting syntax is wrong.
    GLIBCPP_CHECK_SETRLIMIT_ancilliary(DATA)
    GLIBCPP_CHECK_SETRLIMIT_ancilliary(RSS)
    GLIBCPP_CHECK_SETRLIMIT_ancilliary(VMEM)
    GLIBCPP_CHECK_SETRLIMIT_ancilliary(AS)

    # Check for rlimit, setrlimit.
    AC_CACHE_VAL(ac_setrlimit, [
      AC_TRY_COMPILE([#include <unistd.h>
                  #include <sys/time.h>
                  #include <sys/resource.h>
		     ], 
                     [ struct rlimit r; setrlimit(0, &r);], 
                     [ac_setrlimit=yes], [ac_setrlimit=no])
    ])
  fi

  AC_MSG_CHECKING([for testsuite memory limit support])
  if test $setrlimit_have_headers = yes && test $ac_setrlimit = yes; then
    ac_mem_limits=yes
    AC_DEFINE(_GLIBCPP_MEM_LIMITS)
  else
    ac_mem_limits=no
  fi
  AC_MSG_RESULT($ac_mem_limits)
])


dnl
dnl Does any necessary configuration of the testsuite directory.  Generates
dnl the testsuite_hooks.h header.
dnl
dnl GLIBCPP_CONFIGURE_TESTSUITE  [no args]
AC_DEFUN(GLIBCPP_CONFIGURE_TESTSUITE, [
  if test  x"$GLIBCPP_IS_CROSS_COMPILING" = xfalse; then
    # Do checks for memory limit functions.
    GLIBCPP_CHECK_SETRLIMIT

    # Look for setenv, so that extended locale tests can be performed.
    GLIBCPP_CHECK_STDLIB_DECL_AND_LINKAGE_3(setenv)
  fi

  # Export file names for ABI checking.
  baseline_dir="${glibcpp_srcdir}/config/abi/${abi_baseline_pair}\$(MULTISUBDIR)"
  AC_SUBST(baseline_dir)

  # Determine if checking the ABI is desirable.
  # Only build this as native, since automake does not understand
  # CXX_FOR_BUILD.
  if test x"$GLIBCPP_IS_CROSS_COMPILING" = xtrue || test x$enable_symvers = xno; then
    enable_abi_check=no
  else
    case "$host" in
      *-*-cygwin*) 
        enable_abi_check=no ;;
      *) 
        enable_abi_check=yes ;;
    esac
  fi
  AM_CONDITIONAL(GLIBCPP_TEST_ABI, test "$enable_abi_check" = yes)
])


sinclude(../libtool.m4)
dnl The lines below arrange for aclocal not to bring an installed
dnl libtool.m4 into aclocal.m4, while still arranging for automake to
dnl add a definition of LIBTOOL to Makefile.in.
ifelse(,,,[AC_SUBST(LIBTOOL)
AC_DEFUN([AM_PROG_LIBTOOL])
AC_DEFUN([AC_LIBTOOL_DLOPEN])
AC_DEFUN([AC_PROG_LD])
])

dnl
dnl Check whether S_ISREG (Posix) or S_IFREG is available in <sys/stat.h>.
dnl

AC_DEFUN(GLIBCPP_CHECK_S_ISREG_OR_S_IFREG, [
  AC_CACHE_VAL(glibcpp_cv_S_ISREG, [
    AC_TRY_LINK([#include <sys/stat.h>],
                [struct stat buffer; fstat(0, &buffer); S_ISREG(buffer.st_mode); ],
                [glibcpp_cv_S_ISREG=yes],
                [glibcpp_cv_S_ISREG=no])
  ])
  AC_CACHE_VAL(glibcpp_cv_S_IFREG, [
    AC_TRY_LINK([#include <sys/stat.h>],
                [struct stat buffer; fstat(0, &buffer); S_IFREG & buffer.st_mode; ],
                [glibcpp_cv_S_IFREG=yes],
                [glibcpp_cv_S_IFREG=no])
  ])
  if test x$glibcpp_cv_S_ISREG = xyes; then
    AC_DEFINE(HAVE_S_ISREG)
  elif test x$glibcpp_cv_S_IFREG = xyes; then
    AC_DEFINE(HAVE_S_IFREG)
  fi
])

dnl
dnl Check whether poll is available in <poll.h>.
dnl

AC_DEFUN(GLIBCPP_CHECK_POLL, [
  AC_CACHE_VAL(glibcpp_cv_POLL, [
    AC_TRY_COMPILE([#include <poll.h>],
                [struct pollfd pfd[1]; pfd[0].events = POLLIN; poll(pfd, 1, 0); ],
                [glibcpp_cv_POLL=yes],
                [glibcpp_cv_POLL=no])
  ])
  if test x$glibcpp_cv_POLL = xyes; then
    AC_DEFINE(HAVE_POLL)
  fi
])

# Check whether LC_MESSAGES is available in <locale.h>.
# Ulrich Drepper <drepper@cygnus.com>, 1995.
#
# This file file be copied and used freely without restrictions.  It can
# be used in projects which are not available under the GNU Public License
# but which still want to provide support for the GNU gettext functionality.
# Please note that the actual code is *not* freely available.

# serial 1

AC_DEFUN(AC_LC_MESSAGES, [
  AC_CHECK_HEADER(locale.h, [
    AC_CACHE_CHECK([for LC_MESSAGES], ac_cv_val_LC_MESSAGES,
      [AC_TRY_LINK([#include <locale.h>], [return LC_MESSAGES],
       ac_cv_val_LC_MESSAGES=yes, ac_cv_val_LC_MESSAGES=no)])
    if test $ac_cv_val_LC_MESSAGES = yes; then
      AC_DEFINE(HAVE_LC_MESSAGES)
    fi
  ])
])


dnl
dnl Check for whether the Boost-derived checks should be turned on.
dnl
dnl GLIBCPP_ENABLE_CONCEPT_CHECKS
dnl --enable-concept-checks turns them on.
dnl --disable-concept-checks leaves them off.
dnl  +  Usage:  GLIBCPP_ENABLE_CONCEPT_CHECKS[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.  If ommitted, it
dnl       defaults to `no'.
AC_DEFUN(GLIBCPP_ENABLE_CONCEPT_CHECKS, [dnl
define([GLIBCPP_ENABLE_CONCEPT_CHECKS_DEFAULT], ifelse($1, yes, yes, no))dnl
AC_ARG_ENABLE(concept-checks,
changequote(<<, >>)dnl
<<  --enable-concept-checks use Boost-derived template checks [default=>>GLIBCPP_ENABLE_CONCEPT_CHECKS_DEFAULT],
changequote([, ])dnl
[case "$enableval" in
 yes) enable_concept_checks=yes ;;
 no)  enable_concept_checks=no ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable concept checks]) ;;
 esac],
enable_concept_checks=GLIBCPP_ENABLE_CONCEPT_CHECKS_DEFAULT)dnl
dnl Option parsed, now set things appropriately
if test x"$enable_concept_checks" = xyes; then
  AC_DEFINE(_GLIBCPP_CONCEPT_CHECKS)
fi
])


dnl
dnl Add version tags to symbols in shared library (or not), additionally
dnl marking other symbols as private/local (or not).
dnl
dnl GLIBCPP_ENABLE_SYMVERS
dnl --enable-symvers=style adds a version script to the linker call when
dnl       creating the shared library.  The choice of version script is
dnl       controlled by 'style'.
dnl --disable-symvers does not.
dnl  +  Usage:  GLIBCPP_ENABLE_SYMVERS[(DEFAULT)]
dnl       Where DEFAULT is either `yes' or `no'.  If ommitted, it
dnl       defaults to `no'.  Passing `yes' tries to choose a default style
dnl       based on linker characteristics.  Passing 'no' disables versioning.
AC_DEFUN(GLIBCPP_ENABLE_SYMVERS, [dnl
define([GLIBCPP_ENABLE_SYMVERS_DEFAULT], ifelse($1, yes, yes, no))dnl
AC_ARG_ENABLE(symvers,
changequote(<<, >>)dnl
<<  --enable-symvers=style  enables symbol versioning of the shared library [default=>>GLIBCPP_ENABLE_SYMVERS_DEFAULT],
changequote([, ])dnl
[case "$enableval" in
 yes) enable_symvers=yes ;;
 no)  enable_symvers=no ;;
 # other names here, just as sanity checks
 #gnu|sun|etcetera) enable_symvers=$enableval ;;
 gnu) enable_symvers=$enableval ;;
 *)   AC_MSG_ERROR([Unknown argument to enable/disable symvers]) ;;
 esac],
enable_symvers=GLIBCPP_ENABLE_SYMVERS_DEFAULT)dnl

# If we never went through the GLIBCPP_CHECK_LINKER_FEATURES macro, then we
# don't know enough about $LD to do tricks... 
if test x$enable_shared = xno || 
	test "x$LD" = x || 
	test x$glibcpp_gnu_ld_version = x; then
  enable_symvers=no
fi

# Check to see if libgcc_s exists, indicating that shared libgcc is possible.
if test $enable_symvers != no; then
  AC_MSG_CHECKING([for shared libgcc])
  ac_save_CFLAGS="$CFLAGS"
  CFLAGS=' -lgcc_s'
  AC_TRY_LINK(, [return 0], glibcpp_shared_libgcc=yes, glibcpp_shared_libgcc=no)
  CFLAGS="$ac_save_CFLAGS"
  AC_MSG_RESULT($glibcpp_shared_libgcc)
fi

# For GNU ld, we need at least this version.  It's 2.12 in the same format
# as the tested-for version.  See GLIBCPP_CHECK_LINKER_FEATURES for more.
glibcpp_min_gnu_ld_version=21200

# Check to see if unspecified "yes" value can win, given results
# above.  
if test $enable_symvers = yes ; then
  if test $with_gnu_ld = yes &&
    test $glibcpp_shared_libgcc = yes ;
  then
    if test $glibcpp_gnu_ld_version -ge $glibcpp_min_gnu_ld_version ; then
        enable_symvers=gnu
    else
      ac_test_CFLAGS="${CFLAGS+set}"
      ac_save_CFLAGS="$CFLAGS"
      CFLAGS='-shared -Wl,--version-script,conftest.map'
      enable_symvers=no
      changequote(,)
      echo 'FOO { global: f[a-z]o; local: *; };' > conftest.map
      changequote([,])
      AC_TRY_LINK([int foo;],, enable_symvers=gnu)
      if test "$ac_test_CFLAGS" = set; then
	CFLAGS="$ac_save_CFLAGS"
      else
	# this is the suspicious part
	CFLAGS=''
      fi
      rm -f conftest.map
    fi
  else
    # just fail for now
    enable_symvers=no
  fi
fi

dnl Everything parsed; figure out what file to use.
case $enable_symvers in
  no)
      SYMVER_MAP=config/linker-map.dummy
      ;;
  gnu)
      SYMVER_MAP=config/linker-map.gnu
      AC_DEFINE(_GLIBCPP_SYMVER)	
      ;;
esac

AC_SUBST(SYMVER_MAP)
AM_CONDITIONAL(GLIBCPP_BUILD_VERSIONED_SHLIB, test $enable_symvers != no)
AC_MSG_CHECKING([versioning on shared library symbols])
AC_MSG_RESULT($enable_symvers)
])

