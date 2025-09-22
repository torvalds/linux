sinclude(../config/acx.m4)
sinclude(../config/gettext.m4)
sinclude(../config/progtest.m4)
sinclude(../config/warnings.m4)

dnl See whether we need a declaration for a function.
dnl The result is highly dependent on the INCLUDES passed in, so make sure
dnl to use a different cache variable name in this macro if it is invoked
dnl in a different context somewhere else.
dnl gcc_AC_CHECK_DECL(SYMBOL,
dnl 	[ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND [, INCLUDES]]])
AC_DEFUN([gcc_AC_CHECK_DECL],
[AC_MSG_CHECKING([whether $1 is declared])
AC_CACHE_VAL(gcc_cv_have_decl_$1,
[AC_TRY_COMPILE([$4],
[#ifndef $1
char *(*pfn) = (char *(*)) $1 ;
#endif], eval "gcc_cv_have_decl_$1=yes", eval "gcc_cv_have_decl_$1=no")])
if eval "test \"`echo '$gcc_cv_have_decl_'$1`\" = yes"; then
  AC_MSG_RESULT(yes) ; ifelse([$2], , :, [$2])
else
  AC_MSG_RESULT(no) ; ifelse([$3], , :, [$3])
fi
])dnl

dnl Check multiple functions to see whether each needs a declaration.
dnl Arrange to define HAVE_DECL_<FUNCTION> to 0 or 1 as appropriate.
dnl gcc_AC_CHECK_DECLS(SYMBOLS,
dnl 	[ACTION-IF-NEEDED [, ACTION-IF-NOT-NEEDED [, INCLUDES]]])
AC_DEFUN([gcc_AC_CHECK_DECLS],
[AC_FOREACH([gcc_AC_Func], [$1],
  [AH_TEMPLATE(AS_TR_CPP(HAVE_DECL_[]gcc_AC_Func),
  [Define to 1 if we found a declaration for ']gcc_AC_Func[', otherwise
   define to 0.])])dnl
for ac_func in $1
do
  ac_tr_decl=AS_TR_CPP([HAVE_DECL_$ac_func])
gcc_AC_CHECK_DECL($ac_func,
  [AC_DEFINE_UNQUOTED($ac_tr_decl, 1) $2],
  [AC_DEFINE_UNQUOTED($ac_tr_decl, 0) $3],
dnl It is possible that the include files passed in here are local headers
dnl which supply a backup declaration for the relevant prototype based on
dnl the definition of (or lack of) the HAVE_DECL_ macro.  If so, this test
dnl will always return success.  E.g. see libiberty.h's handling of
dnl `basename'.  To avoid this, we define the relevant HAVE_DECL_ macro to
dnl 1 so that any local headers used do not provide their own prototype
dnl during this test.
#undef $ac_tr_decl
#define $ac_tr_decl 1
  $4
)
done
])

dnl 'make compare' can be significantly faster, if cmp itself can
dnl skip bytes instead of using tail.  The test being performed is
dnl "if cmp --ignore-initial=2 t1 t2 && ! cmp --ignore-initial=1 t1 t2"
dnl but we need to sink errors and handle broken shells.  We also test
dnl for the parameter format "cmp file1 file2 skip1 skip2" which is
dnl accepted by cmp on some systems.
AC_DEFUN([gcc_AC_PROG_CMP_IGNORE_INITIAL],
[AC_CACHE_CHECK([for cmp's capabilities], gcc_cv_prog_cmp_skip,
[ echo abfoo >t1
  echo cdfoo >t2
  gcc_cv_prog_cmp_skip=slowcompare
  if cmp --ignore-initial=2 t1 t2 > /dev/null 2>&1; then
    if cmp --ignore-initial=1 t1 t2 > /dev/null 2>&1; then
      :
    else
      gcc_cv_prog_cmp_skip=gnucompare
    fi
  fi
  if test $gcc_cv_prog_cmp_skip = slowcompare ; then
    if cmp t1 t2 2 2 > /dev/null 2>&1; then
      if cmp t1 t2 1 1 > /dev/null 2>&1; then
        :
      else
        gcc_cv_prog_cmp_skip=fastcompare
      fi
    fi
  fi
  rm t1 t2
])
make_compare_target=$gcc_cv_prog_cmp_skip
AC_SUBST(make_compare_target)
])

dnl See if the printf functions in libc support %p in format strings.
AC_DEFUN([gcc_AC_FUNC_PRINTF_PTR],
[AC_CACHE_CHECK(whether the printf functions support %p,
  gcc_cv_func_printf_ptr,
[AC_TRY_RUN([#include <stdio.h>

int main()
{
  char buf[64];
  char *p = buf, *q = NULL;
  sprintf(buf, "%p", p);
  sscanf(buf, "%p", &q);
  return (p != q);
}], gcc_cv_func_printf_ptr=yes, gcc_cv_func_printf_ptr=no,
	gcc_cv_func_printf_ptr=no)
rm -f core core.* *.core])
if test $gcc_cv_func_printf_ptr = yes ; then
  AC_DEFINE(HAVE_PRINTF_PTR, 1, [Define if printf supports "%p".])
fi
])

dnl See if symbolic links work and if not, try to substitute either hard links or simple copy.
AC_DEFUN([gcc_AC_PROG_LN_S],
[AC_MSG_CHECKING(whether ln -s works)
AC_CACHE_VAL(gcc_cv_prog_LN_S,
[rm -f conftestdata_t
echo >conftestdata_f
if ln -s conftestdata_f conftestdata_t 2>/dev/null
then
  gcc_cv_prog_LN_S="ln -s"
else
  if ln conftestdata_f conftestdata_t 2>/dev/null
  then
    gcc_cv_prog_LN_S=ln
  else
    gcc_cv_prog_LN_S=cp
  fi
fi
rm -f conftestdata_f conftestdata_t
])dnl
LN_S="$gcc_cv_prog_LN_S"
if test "$gcc_cv_prog_LN_S" = "ln -s"; then
  AC_MSG_RESULT(yes)
else
  if test "$gcc_cv_prog_LN_S" = "ln"; then
    AC_MSG_RESULT([no, using ln])
  else
    AC_MSG_RESULT([no, and neither does ln, so using cp])
  fi
fi
AC_SUBST(LN_S)dnl
])

dnl Define MKDIR_TAKES_ONE_ARG if mkdir accepts only one argument instead
dnl of the usual 2.
AC_DEFUN([gcc_AC_FUNC_MKDIR_TAKES_ONE_ARG],
[AC_CACHE_CHECK([if mkdir takes one argument], gcc_cv_mkdir_takes_one_arg,
[AC_TRY_COMPILE([
#include <sys/types.h>
#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_DIRECT_H
# include <direct.h>
#endif], [mkdir ("foo", 0);], 
        gcc_cv_mkdir_takes_one_arg=no, gcc_cv_mkdir_takes_one_arg=yes)])
if test $gcc_cv_mkdir_takes_one_arg = yes ; then
  AC_DEFINE(MKDIR_TAKES_ONE_ARG, 1, [Define if host mkdir takes a single argument.])
fi
])

AC_DEFUN([gcc_AC_PROG_INSTALL],
[AC_REQUIRE([AC_CONFIG_AUX_DIR_DEFAULT])dnl
# Find a good install program.  We prefer a C program (faster),
# so one script is as good as another.  But avoid the broken or
# incompatible versions:
# SysV /etc/install, /usr/sbin/install
# SunOS /usr/etc/install
# IRIX /sbin/install
# AIX /bin/install
# AFS /usr/afsws/bin/install, which mishandles nonexistent args
# SVR4 /usr/ucb/install, which tries to use the nonexistent group "staff"
# ./install, which can be erroneously created by make from ./install.sh.
AC_MSG_CHECKING(for a BSD compatible install)
if test -z "$INSTALL"; then
AC_CACHE_VAL(ac_cv_path_install,
[  IFS="${IFS= 	}"; ac_save_IFS="$IFS"; IFS="${IFS}:"
  for ac_dir in $PATH; do
    # Account for people who put trailing slashes in PATH elements.
    case "$ac_dir/" in
    /|./|.//|/etc/*|/usr/sbin/*|/usr/etc/*|/sbin/*|/usr/afsws/bin/*|/usr/ucb/*) ;;
    *)
      # OSF1 and SCO ODT 3.0 have their own names for install.
      for ac_prog in ginstall scoinst install; do
        if test -f $ac_dir/$ac_prog; then
	  if test $ac_prog = install &&
            grep dspmsg $ac_dir/$ac_prog >/dev/null 2>&1; then
	    # AIX install.  It has an incompatible calling convention.
	    # OSF/1 installbsd also uses dspmsg, but is usable.
	    :
	  else
	    ac_cv_path_install="$ac_dir/$ac_prog -c"
	    break 2
	  fi
	fi
      done
      ;;
    esac
  done
  IFS="$ac_save_IFS"
])dnl
  if test "${ac_cv_path_install+set}" = set; then
    INSTALL="$ac_cv_path_install"
  else
    # As a last resort, use the slow shell script.  We don't cache a
    # path for INSTALL within a source directory, because that will
    # break other packages using the cache if that directory is
    # removed, or if the path is relative.
    INSTALL="$ac_install_sh"
  fi
fi
dnl We do special magic for INSTALL instead of AC_SUBST, to get
dnl relative paths right.
AC_MSG_RESULT($INSTALL)
AC_SUBST(INSTALL)dnl

# Use test -z because SunOS4 sh mishandles braces in ${var-val}.
# It thinks the first close brace ends the variable substitution.
test -z "$INSTALL_PROGRAM" && INSTALL_PROGRAM='${INSTALL}'
AC_SUBST(INSTALL_PROGRAM)dnl

test -z "$INSTALL_DATA" && INSTALL_DATA='${INSTALL} -m 644'
AC_SUBST(INSTALL_DATA)dnl
])

dnl GCC_PATH_PROG(VARIABLE, PROG-TO-CHECK-FOR [, VALUE-IF-NOT-FOUND [, PATH]])
dnl like AC_PATH_PROG but use other cache variables
AC_DEFUN([GCC_PATH_PROG],
[# Extract the first word of "$2", so it can be a program name with args.
set dummy $2; ac_word=[$]2
AC_MSG_CHECKING([for $ac_word])
AC_CACHE_VAL(gcc_cv_path_$1,
[case "[$]$1" in
  /*)
  gcc_cv_path_$1="[$]$1" # Let the user override the test with a path.
  ;;
  ?:/*)			 
  gcc_cv_path_$1="[$]$1" # Let the user override the test with a dos path.
  ;;
  *)
  IFS="${IFS= 	}"; ac_save_ifs="$IFS"; IFS=":"
dnl $ac_dummy forces splitting on constant user-supplied paths.
dnl POSIX.2 word splitting is done only on the output of word expansions,
dnl not every word.  This closes a longstanding sh security hole.
  ac_dummy="ifelse([$4], , $PATH, [$4])"
  for ac_dir in $ac_dummy; do 
    test -z "$ac_dir" && ac_dir=.
    if test -f $ac_dir/$ac_word; then
      gcc_cv_path_$1="$ac_dir/$ac_word"
      break
    fi
  done
  IFS="$ac_save_ifs"
dnl If no 3rd arg is given, leave the cache variable unset,
dnl so GCC_PATH_PROGS will keep looking.
ifelse([$3], , , [  test -z "[$]gcc_cv_path_$1" && gcc_cv_path_$1="$3"
])dnl
  ;;
esac])dnl
$1="$gcc_cv_path_$1"
if test -n "[$]$1"; then
  AC_MSG_RESULT([$]$1)
else
  AC_MSG_RESULT(no)
fi
AC_SUBST($1)dnl
])

# mmap(2) blacklisting.  Some platforms provide the mmap library routine
# but don't support all of the features we need from it.
AC_DEFUN([gcc_AC_FUNC_MMAP_BLACKLIST],
[
AC_CHECK_HEADER([sys/mman.h],
		[gcc_header_sys_mman_h=yes], [gcc_header_sys_mman_h=no])
AC_CHECK_FUNC([mmap], [gcc_func_mmap=yes], [gcc_func_mmap=no])
if test "$gcc_header_sys_mman_h" != yes \
 || test "$gcc_func_mmap" != yes; then
   gcc_cv_func_mmap_file=no
   gcc_cv_func_mmap_dev_zero=no
   gcc_cv_func_mmap_anon=no
else
   AC_CACHE_CHECK([whether read-only mmap of a plain file works], 
  gcc_cv_func_mmap_file,
  [# Add a system to this blacklist if 
   # mmap(0, stat_size, PROT_READ, MAP_PRIVATE, fd, 0) doesn't return a
   # memory area containing the same data that you'd get if you applied
   # read() to the same fd.  The only system known to have a problem here
   # is VMS, where text files have record structure.
   case "$host_os" in
     vms* | ultrix*) 
        gcc_cv_func_mmap_file=no ;;
     *)
        gcc_cv_func_mmap_file=yes;;
   esac])
   AC_CACHE_CHECK([whether mmap from /dev/zero works],
  gcc_cv_func_mmap_dev_zero,
  [# Add a system to this blacklist if it has mmap() but /dev/zero
   # does not exist, or if mmapping /dev/zero does not give anonymous
   # zeroed pages with both the following properties:
   # 1. If you map N consecutive pages in with one call, and then
   #    unmap any subset of those pages, the pages that were not
   #    explicitly unmapped remain accessible.
   # 2. If you map two adjacent blocks of memory and then unmap them
   #    both at once, they must both go away.
   # Systems known to be in this category are Windows (all variants),
   # VMS, and Darwin.
   case "$host_os" in
     vms* | cygwin* | pe | mingw* | darwin* | ultrix* | hpux10* | hpux11.00)
        gcc_cv_func_mmap_dev_zero=no ;;
     *)
        gcc_cv_func_mmap_dev_zero=yes;;
   esac])

   # Unlike /dev/zero, the MAP_ANON(YMOUS) defines can be probed for.
   AC_CACHE_CHECK([for MAP_ANON(YMOUS)], gcc_cv_decl_map_anon,
    [AC_TRY_COMPILE(
[#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
],
[int n = MAP_ANONYMOUS;],
    gcc_cv_decl_map_anon=yes,
    gcc_cv_decl_map_anon=no)])

   if test $gcc_cv_decl_map_anon = no; then
     gcc_cv_func_mmap_anon=no
   else
     AC_CACHE_CHECK([whether mmap with MAP_ANON(YMOUS) works],
     gcc_cv_func_mmap_anon,
  [# Add a system to this blacklist if it has mmap() and MAP_ANON or
   # MAP_ANONYMOUS, but using mmap(..., MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
   # doesn't give anonymous zeroed pages with the same properties listed
   # above for use of /dev/zero.
   # Systems known to be in this category are Windows, VMS, and SCO Unix.
   case "$host_os" in
     vms* | cygwin* | pe | mingw* | sco* | udk* )
        gcc_cv_func_mmap_anon=no ;;
     *)
        gcc_cv_func_mmap_anon=yes;;
   esac])
   fi
fi

if test $gcc_cv_func_mmap_file = yes; then
  AC_DEFINE(HAVE_MMAP_FILE, 1,
	    [Define if read-only mmap of a plain file works.])
fi
if test $gcc_cv_func_mmap_dev_zero = yes; then
  AC_DEFINE(HAVE_MMAP_DEV_ZERO, 1,
	    [Define if mmap of /dev/zero works.])
fi
if test $gcc_cv_func_mmap_anon = yes; then
  AC_DEFINE(HAVE_MMAP_ANON, 1,
	    [Define if mmap with MAP_ANON(YMOUS) works.])
fi
])

dnl Locate a program and check that its version is acceptable.
dnl AC_PROG_CHECK_VER(var, name, version-switch,
dnl                  version-extract-regexp, version-glob)
AC_DEFUN([gcc_AC_CHECK_PROG_VER],
[AC_CHECK_PROG([$1], [$2], [$2])
if test -n "[$]$1"; then
  # Found it, now check the version.
  AC_CACHE_CHECK(for modern $2, gcc_cv_prog_$2_modern,
[changequote(<<,>>)dnl
  ac_prog_version=`<<$>>$1 $3 2>&1 |
                   sed -n 's/^.*patsubst(<<$4>>,/,\/).*$/\1/p'`
changequote([,])dnl
  echo "configure:__oline__: version of $2 is $ac_prog_version" >&AC_FD_CC
changequote(<<,>>)dnl
  case $ac_prog_version in
    '')     gcc_cv_prog_$2_modern=no;;
    <<$5>>)
            gcc_cv_prog_$2_modern=yes;;
    *)      gcc_cv_prog_$2_modern=no;;
  esac
changequote([,])dnl
])
else
  gcc_cv_prog_$2_modern=no
fi
])

dnl Determine if enumerated bitfields are unsigned.   ISO C says they can 
dnl be either signed or unsigned.
dnl
AC_DEFUN([gcc_AC_C_ENUM_BF_UNSIGNED],
[AC_CACHE_CHECK(for unsigned enumerated bitfields, gcc_cv_enum_bf_unsigned,
[AC_TRY_RUN(#include <stdlib.h>
enum t { BLAH = 128 } ;
struct s_t { enum t member : 8; } s ;
int main(void)
{            
        s.member = BLAH;
        if (s.member < 0) exit(1);
        exit(0);

}, gcc_cv_enum_bf_unsigned=yes, gcc_cv_enum_bf_unsigned=no, gcc_cv_enum_bf_unsigned=yes)])
if test $gcc_cv_enum_bf_unsigned = yes; then
  AC_DEFINE(ENUM_BITFIELDS_ARE_UNSIGNED, 1,
    [Define if enumerated bitfields are treated as unsigned values.])
fi])

dnl Probe number of bits in a byte.
dnl Note C89 requires CHAR_BIT >= 8.
dnl
AC_DEFUN([gcc_AC_C_CHAR_BIT],
[AC_CACHE_CHECK(for CHAR_BIT, gcc_cv_decl_char_bit,
[AC_EGREP_CPP(found,
[#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#ifdef CHAR_BIT
found
#endif], gcc_cv_decl_char_bit=yes, gcc_cv_decl_char_bit=no)
])
if test $gcc_cv_decl_char_bit = no; then
  AC_CACHE_CHECK(number of bits in a byte, gcc_cv_c_nbby,
[i=8
 gcc_cv_c_nbby=
 while test $i -lt 65; do
   AC_TRY_COMPILE(,
     [switch(0) {
  case (unsigned char)((unsigned long)1 << $i) == ((unsigned long)1 << $i):
  case (unsigned char)((unsigned long)1<<($i-1)) == ((unsigned long)1<<($i-1)):
  ; }], 
     [gcc_cv_c_nbby=$i; break])
   i=`expr $i + 1`
 done
 test -z "$gcc_cv_c_nbby" && gcc_cv_c_nbby=failed
])
if test $gcc_cv_c_nbby = failed; then
  AC_MSG_ERROR(cannot determine number of bits in a byte)
else
  AC_DEFINE_UNQUOTED(CHAR_BIT, $gcc_cv_c_nbby,
  [Define as the number of bits in a byte, if \`limits.h' doesn't.])
fi
fi])

dnl Checking for long long.
dnl By Caolan McNamara <caolan@skynet.ie>
dnl Added check for __int64, Zack Weinberg <zackw@stanford.edu>
dnl
AC_DEFUN([gcc_AC_C_LONG_LONG],
[AC_CACHE_CHECK(for long long int, ac_cv_c_long_long,
  [AC_TRY_COMPILE(,[long long int i;],
         ac_cv_c_long_long=yes,
         ac_cv_c_long_long=no)])
  if test $ac_cv_c_long_long = yes; then
    AC_DEFINE(HAVE_LONG_LONG, 1,
      [Define if your compiler supports the \`long long' type.])
  fi
AC_CACHE_CHECK(for __int64, ac_cv_c___int64,
  [AC_TRY_COMPILE(,[__int64 i;],
	ac_cv_c___int64=yes,
	ac_cv_c___int64=no)])
  if test $ac_cv_c___int64 = yes; then
    AC_DEFINE(HAVE___INT64, 1,
      [Define if your compiler supports the \`__int64' type.])
  fi
])

#serial AM2

dnl From Bruno Haible.

AC_DEFUN([AM_ICONV],
[
  dnl Some systems have iconv in libc, some have it in libiconv (OSF/1 and
  dnl those with the standalone portable GNU libiconv installed).

  am_cv_lib_iconv_ldpath=
  AC_ARG_WITH([libiconv-prefix],
[  --with-libiconv-prefix=DIR  search for libiconv in DIR/include and DIR/lib], [
    for dir in `echo "$withval" | tr : ' '`; do
      if test -d $dir/include; then CPPFLAGS="$CPPFLAGS -I$dir/include"; fi
      if test -d $dir/lib; then am_cv_lib_iconv_ldpath="-L$dir/lib"; fi
    done
   ])

  AC_CHECK_HEADERS([iconv.h])

  AC_CACHE_CHECK(for iconv, am_cv_func_iconv, [
    am_cv_func_iconv="no, consider installing GNU libiconv"
    am_cv_lib_iconv=no
    AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
      [iconv_t cd = iconv_open("","");
       iconv(cd,NULL,NULL,NULL,NULL);
       iconv_close(cd);],
      am_cv_func_iconv=yes)
    if test "$am_cv_func_iconv" != yes; then
      am_save_LIBS="$LIBS"
      LIBS="$LIBS $am_cv_libiconv_ldpath -liconv"
      AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
        [iconv_t cd = iconv_open("","");
         iconv(cd,NULL,NULL,NULL,NULL);
         iconv_close(cd);],
        am_cv_lib_iconv=yes
        am_cv_func_iconv=yes)
      LIBS="$am_save_LIBS"
    fi
  ])
  if test "$am_cv_func_iconv" = yes; then
    AC_DEFINE(HAVE_ICONV, 1, [Define if you have the iconv() function.])
    AC_MSG_CHECKING([for iconv declaration])
    AC_CACHE_VAL(am_cv_proto_iconv, [
      AC_TRY_COMPILE([
#include <stdlib.h>
#include <iconv.h>
extern
#ifdef __cplusplus
"C"
#endif
#if defined(__STDC__) || defined(__cplusplus)
size_t iconv (iconv_t cd, char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);
#else
size_t iconv();
#endif
], [], am_cv_proto_iconv_arg1="", am_cv_proto_iconv_arg1="const")
      am_cv_proto_iconv="extern size_t iconv (iconv_t cd, $am_cv_proto_iconv_arg1 char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);"])
    am_cv_proto_iconv=`echo "[$]am_cv_proto_iconv" | tr -s ' ' | sed -e 's/( /(/'`
    AC_MSG_RESULT([$]{ac_t:-
         }[$]am_cv_proto_iconv)
    AC_DEFINE_UNQUOTED(ICONV_CONST, $am_cv_proto_iconv_arg1,
      [Define as const if the declaration of iconv() needs const.])
  fi
  LIBICONV=
  if test "$am_cv_lib_iconv" = yes; then
    LIBICONV="$am_cv_lib_iconv_ldpath -liconv"
  fi
  AC_SUBST(LIBICONV)
])

AC_DEFUN([gcc_AC_INITFINI_ARRAY],
[AC_ARG_ENABLE(initfini-array,
	[  --enable-initfini-array	use .init_array/.fini_array sections],
	[], [
AC_CACHE_CHECK(for .preinit_array/.init_array/.fini_array support,
		 gcc_cv_initfini_array, [dnl
  AC_TRY_RUN([
static int x = -1;
int main (void) { return x; }
int foo (void) { x = 0; }
int (*fp) (void) __attribute__ ((section (".init_array"))) = foo;],
	     [gcc_cv_initfini_array=yes], [gcc_cv_initfini_array=no],
	     [gcc_cv_initfini_array=no])])
  enable_initfini_array=$gcc_cv_initfini_array
])
if test $enable_initfini_array = yes; then
  AC_DEFINE(HAVE_INITFINI_ARRAY, 1,
    [Define .init_array/.fini_array sections are available and working.])
fi])

dnl # _gcc_COMPUTE_GAS_VERSION
dnl # Used by gcc_GAS_VERSION_GTE_IFELSE
dnl #
dnl # WARNING:
dnl # gcc_cv_as_gas_srcdir must be defined before this.
dnl # This gross requirement will go away eventually.
AC_DEFUN([_gcc_COMPUTE_GAS_VERSION],
[gcc_cv_as_bfd_srcdir=`echo $srcdir | sed -e 's,/gcc$,,'`/bfd
for f in $gcc_cv_as_bfd_srcdir/configure \
         $gcc_cv_as_gas_srcdir/configure \
         $gcc_cv_as_gas_srcdir/configure.in \
         $gcc_cv_as_gas_srcdir/Makefile.in ; do
  gcc_cv_gas_version=`grep '^VERSION=[[0-9]]*\.[[0-9]]*' $f`
  if test x$gcc_cv_gas_version != x; then
    break
  fi
done
gcc_cv_gas_major_version=`expr "$gcc_cv_gas_version" : "VERSION=\([[0-9]]*\)"`
gcc_cv_gas_minor_version=`expr "$gcc_cv_gas_version" : "VERSION=[[0-9]]*\.\([[0-9]]*\)"`
gcc_cv_gas_patch_version=`expr "$gcc_cv_gas_version" : "VERSION=[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)"`
case $gcc_cv_gas_patch_version in
  "") gcc_cv_gas_patch_version="0" ;;
esac
gcc_cv_gas_vers=`expr \( \( $gcc_cv_gas_major_version \* 1000 \) \
			    + $gcc_cv_gas_minor_version \) \* 1000 \
			    + $gcc_cv_gas_patch_version`
]) []dnl # _gcc_COMPUTE_GAS_VERSION

dnl # gcc_GAS_VERSION_GTE_IFELSE([elf,] major, minor, patchlevel,
dnl #                     [command_if_true = :], [command_if_false = :])
dnl # Check to see if the version of GAS is greater than or
dnl # equal to the specified version.
dnl #
dnl # The first ifelse() shortens the shell code if the patchlevel
dnl # is unimportant (the usual case).  The others handle missing
dnl # commands.  Note that the tests are structured so that the most
dnl # common version number cases are tested first.
AC_DEFUN([_gcc_GAS_VERSION_GTE_IFELSE],
[ifelse([$1], elf,
 [if test $in_tree_gas_is_elf = yes \
  &&],
 [if]) test $gcc_cv_gas_vers -ge `expr \( \( $2 \* 1000 \) + $3 \) \* 1000 + $4`
  then dnl
ifelse([$5],,:,[$5])[]dnl
ifelse([$6],,,[
  else $6])
fi])

AC_DEFUN([gcc_GAS_VERSION_GTE_IFELSE],
[AC_REQUIRE([_gcc_COMPUTE_GAS_VERSION])dnl
ifelse([$1], elf, [_gcc_GAS_VERSION_GTE_IFELSE($@)],
                  [_gcc_GAS_VERSION_GTE_IFELSE(,$@)])])

dnl gcc_GAS_CHECK_FEATURE(description, cv, [[elf,]major,minor,patchlevel],
dnl [extra switches to as], [assembler input],
dnl [extra testing logic], [command if feature available])
dnl
dnl Checks for an assembler feature.  If we are building an in-tree
dnl gas, the feature is available if the associated assembler version
dnl is greater than or equal to major.minor.patchlevel.  If not, then
dnl ASSEMBLER INPUT is fed to the assembler and the feature is available
dnl if assembly succeeds.  If EXTRA TESTING LOGIC is not the empty string,
dnl then it is run instead of simply setting CV to "yes" - it is responsible
dnl for doing so, if appropriate.
AC_DEFUN([gcc_GAS_CHECK_FEATURE],
[AC_CACHE_CHECK([assembler for $1], [$2],
 [[$2]=no
  ifelse([$3],,,[dnl
  if test $in_tree_gas = yes; then
    gcc_GAS_VERSION_GTE_IFELSE($3, [[$2]=yes])
  el])if test x$gcc_cv_as != x; then
    echo ifelse(m4_substr([$5],0,1),[$], "[$5]", '[$5]') > conftest.s
    if AC_TRY_COMMAND([$gcc_cv_as $4 -o conftest.o conftest.s >&AC_FD_CC])
    then
	ifelse([$6],, [$2]=yes, [$6])
    else
      echo "configure: failed program was" >&AC_FD_CC
      cat conftest.s >&AC_FD_CC
    fi
    rm -f conftest.o conftest.s
  fi])
ifelse([$7],,,[dnl
if test $[$2] = yes; then
  $7
fi])])

# lcmessage.m4 serial 3 (gettext-0.11.3)
dnl Copyright (C) 1995-2002 Free Software Foundation, Inc.
dnl This file is free software, distributed under the terms of the GNU
dnl General Public License.  As a special exception to the GNU General
dnl Public License, this file may be distributed as part of a program
dnl that contains a configuration script generated by Autoconf, under
dnl the same distribution terms as the rest of that program.
dnl
dnl This file can can be used in projects which are not available under
dnl the GNU General Public License or the GNU Library General Public
dnl License but which still want to provide support for the GNU gettext
dnl functionality.
dnl Please note that the actual code of the GNU gettext library is covered
dnl by the GNU Library General Public License, and the rest of the GNU
dnl gettext package package is covered by the GNU General Public License.
dnl They are *not* in the public domain.

dnl Authors:
dnl   Ulrich Drepper <drepper@cygnus.com>, 1995.

# Check whether LC_MESSAGES is available in <locale.h>.

AC_DEFUN([AM_LC_MESSAGES],
[
  AC_CACHE_CHECK([for LC_MESSAGES], am_cv_val_LC_MESSAGES,
    [AC_TRY_LINK([#include <locale.h>], [return LC_MESSAGES],
       am_cv_val_LC_MESSAGES=yes, am_cv_val_LC_MESSAGES=no)])
  if test $am_cv_val_LC_MESSAGES = yes; then
    AC_DEFINE(HAVE_LC_MESSAGES, 1,
      [Define if your <locale.h> file defines LC_MESSAGES.])
  fi
])
