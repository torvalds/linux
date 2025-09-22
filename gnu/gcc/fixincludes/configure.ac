AC_PREREQ(2.59)

AC_INIT(fixincludes, [ ])
AC_CONFIG_SRCDIR(inclhack.def)
AC_CONFIG_AUX_DIR(..)
AC_CANONICAL_SYSTEM
AC_PROG_CC

# Figure out what compiler warnings we can enable.
# See config/warnings.m4 for details.

ACX_PROG_CC_WARNING_OPTS([-W -Wall -Wwrite-strings -Wstrict-prototypes \
			  -Wmissing-prototypes -Wold-style-definition \
			  -Wmissing-format-attribute -Wno-overlength-strings])
ACX_PROG_CC_WARNING_ALMOST_PEDANTIC([-Wno-long-long])

# Only enable with --enable-werror-always until existing warnings are
# corrected.
ACX_PROG_CC_WARNINGS_ARE_ERRORS([manual])

# Determine the noncanonical target name, for directory use.
ACX_NONCANONICAL_TARGET

# Specify the local prefix
local_prefix=
AC_ARG_WITH(local-prefix,
[  --with-local-prefix=DIR specifies directory to put local include],
[case "${withval}" in
yes)	AC_MSG_ERROR(bad value ${withval} given for local include directory prefix) ;;
no)	;;
*)	local_prefix=$with_local_prefix ;;
esac])

# Default local prefix if it is empty
if test x$local_prefix = x; then
	local_prefix=/usr/local
fi

# Choose one or two-process fix methodology.  Systems that cannot handle
# bi-directional pipes must use the two process method.
#
AC_ARG_ENABLE([twoprocess],
[  --enable-twoprocess       Use a separate process to apply the fixes],
[if test "x$enable_twoprocess" = xyes; then
	TARGET=twoprocess
else
	TARGET=oneprocess
fi],
[case $host in
	i?86-*-msdosdjgpp* | \
	i?86-*-mingw32* | \
	*-*-beos* )
		TARGET=twoprocess
		;;

	* )
		TARGET=oneprocess
		;;
esac])
AC_SUBST(TARGET)

if test $TARGET = twoprocess; then
	AC_DEFINE(SEPARATE_FIX_PROC, 1,
		  [Define if testing and fixing are done by separate process])
fi

case $host in
	vax-dec-bsd* )
		AC_DEFINE(exit, xexit, [Define to xexit if the host system does not support atexit])
		AC_DEFINE(atexit, xatexit, [Define to xatexit if the host system does not support atexit])
		;;
esac

AC_DEFINE_UNQUOTED([EXE_EXT], "$ac_exeext",
  [Defined to the executable file extension on the host system])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stddef.h stdlib.h strings.h unistd.h fcntl.h sys/file.h \
	sys/stat.h])
define(fixincludes_UNLOCKED_FUNCS, clearerr_unlocked feof_unlocked dnl
  ferror_unlocked fflush_unlocked fgetc_unlocked fgets_unlocked dnl
  fileno_unlocked fprintf_unlocked fputc_unlocked fputs_unlocked dnl
  fread_unlocked fwrite_unlocked getchar_unlocked getc_unlocked dnl
  putchar_unlocked putc_unlocked)
AC_CHECK_FUNCS(fixincludes_UNLOCKED_FUNCS)
AC_CHECK_DECLS(m4_split(m4_normalize(abort asprintf basename errno vasprintf fixincludes_UNLOCKED_FUNCS)))

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
gcc_AC_FUNC_MMAP_BLACKLIST

AC_MSG_CHECKING([whether to enable maintainer-specific portions of Makefiles])
AC_ARG_ENABLE(maintainer-mode,
[  --enable-maintainer-mode enable make rules and dependencies not useful
                          (and sometimes confusing) to the casual installer],
      USE_MAINTAINER_MODE=$enableval,
      USE_MAINTAINER_MODE=no)
AC_MSG_RESULT($USE_MAINTAINER_MODE)
if test "$USE_MAINTAINER_MODE" = yes; then
  MAINT=
else
  MAINT='#'
fi	
AC_SUBST(MAINT)

AC_CONFIG_HEADERS(config.h, [echo timestamp > stamp-h])
AC_CONFIG_FILES(Makefile mkheaders.almost:mkheaders.in)
AC_OUTPUT
