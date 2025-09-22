#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.56])
sinclude(acx_nlnetlabs.m4)
sinclude(ax_build_date_epoch.m4)
sinclude(ax_pthread.m4)
sinclude(acx_python.m4)
sinclude(ax_pkg_swig.m4)
sinclude(dnstap/dnstap.m4)
sinclude(dnscrypt/dnscrypt.m4)

# must be numbers. ac_defun because of later processing
m4_define([VERSION_MAJOR],[1])
m4_define([VERSION_MINOR],[23])
m4_define([VERSION_MICRO],[1])
AC_INIT([unbound],m4_defn([VERSION_MAJOR]).m4_defn([VERSION_MINOR]).m4_defn([VERSION_MICRO]),[unbound-bugs@nlnetlabs.nl or https://github.com/NLnetLabs/unbound/issues],[unbound])
AC_SUBST(UNBOUND_VERSION_MAJOR, [VERSION_MAJOR])
AC_SUBST(UNBOUND_VERSION_MINOR, [VERSION_MINOR])
AC_SUBST(UNBOUND_VERSION_MICRO, [VERSION_MICRO])

LIBUNBOUND_CURRENT=9
LIBUNBOUND_REVISION=32
LIBUNBOUND_AGE=1
# 1.0.0 had 0:12:0
# 1.0.1 had 0:13:0
# 1.0.2 had 0:14:0
# 1.1.0 had 0:15:0
# 1.1.1 had 0:16:0
# 1.2.0 had 0:17:0
# 1.2.1 had 0:18:0
# 1.3.0 had 1:0:0   # ub_cancel and -export-symbols.
# 1.3.1 had 1:1:0
# 1.3.2 had 1:2:0
# 1.3.3 had 1:3:0
# 1.3.4 had 1:4:0
# 1.4.0-snapshots had 1:5:0
# 1.4.0 had 1:5:0 (not 2:0:0)   # ub_result.why_bogus
# 1.4.1 had 2:1:0
# 1.4.2 had 2:2:0
# 1.4.3 had 2:3:0
# 1.4.4 had 2:4:0
# 1.4.5 had 2:5:0
# 1.4.6 had 2:6:0
# 1.4.7 had 2:7:0
# 1.4.8 had 2:8:0
# 1.4.9 had 2:9:0
# 1.4.10 had 2:10:0
# 1.4.11 had 2:11:0
# 1.4.12 had 2:12:0
# 1.4.13 had 2:13:0
# and 1.4.13p1 and 1.4.13.p2
# 1.4.14 had 2:14:0
# 1.4.15 had 3:0:1 # adds ub_version()
# 1.4.16 had 3:1:1
# 1.4.17 had 3:2:1
# 1.4.18 had 3:3:1
# 1.4.19 had 3:4:1
# 1.4.20 had 4:0:2 # adds libunbound.ttl # but shipped 3:5:1
# 1.4.21 had 4:1:2
# 1.4.22 had 4:1:2
# 1.5.0 had 5:3:3 # adds ub_ctx_add_ta_autr
# 1.5.1 had 5:3:3
# 1.5.2 had 5:5:3
# 1.5.3 had 5:6:3
# 1.5.4 had 5:7:3
# 1.5.5 had 5:8:3
# 1.5.6 had 5:9:3
# 1.5.7 had 5:10:3
# 1.5.8 had 6:0:4 # adds ub_ctx_set_stub
# 1.5.9 had 6:1:4
# 1.5.10 had 6:2:4
# 1.6.0 had 6:3:4
# 1.6.1 had 7:0:5 # ub_callback_t typedef renamed to ub_callback_type
# 1.6.2 had 7:1:5
# 1.6.3 had 7:2:5
# 1.6.4 had 7:3:5
# 1.6.5 had 7:4:5
# 1.6.6 had 7:5:5
# 1.6.7 had 7:6:5
# 1.6.8 had 7:7:5
# 1.7.0 had 7:8:5
# 1.7.1 had 7:9:5
# 1.7.2 had 7:10:5
# 1.7.3 had 7:11:5
# 1.8.0 had 8:0:0 # changes the event callback function signature
# 1.8.1 had 8:1:0
# 1.8.2 had 8:2:0
# 1.8.3 had 8:3:0
# 1.9.0 had 9:0:1 # add ub_ctx_set_tls
# 1.9.1 had 9:1:1
# 1.9.2 had 9:2:1
# 1.9.3 had 9:3:1
# 1.9.4 had 9:4:1
# 1.9.5 had 9:5:1
# 1.9.6 had 9:6:1
# 1.10.0 had 9:7:1
# 1.10.1 had 9:8:1
# 1.11.0 had 9:9:1
# 1.12.0 had 9:10:1
# 1.13.0 had 9:11:1
# 1.13.1 had 9:12:1
# 1.13.2 had 9:13:1
# 1.14.0 had 9:14:1
# 1.15.0 had 9:15:1
# 1.16.0 had 9:16:1
# 1.16.1 had 9:17:1
# 1.16.2 had 9:18:1
# 1.16.3 had 9:19:1
# 1.17.0 had 9:20:1
# 1.17.1 had 9:21:1
# 1.18.0 had 9:22:1
# 1.19.0 had 9:23:1
# 1.19.1 had 9:24:1
# 1.19.2 had 9:25:1
# 1.19.3 had 9:26:1
# 1.20.0 had 9:27:1
# 1.21.0 had 9:28:1
# 1.21.1 had 9:29:1
# 1.22.0 had 9:30:1
# 1.23.0 had 9:31:1
# 1.23.1 had 9:32:1

#   Current  -- the number of the binary API that we're implementing
#   Revision -- which iteration of the implementation of the binary
#               API are we supplying?
#   Age      -- How many previous binary API versions do we also
#               support?
#
# If we release a new version that does not change the binary API,
# increment Revision.
#
# If we release a new version that changes the binary API, but does
# not break programs compiled against the old binary API, increment
# Current and Age.  Set Revision to 0, since this is the first
# implementation of the new API.
#
# Otherwise, we're changing the binary API and breaking backward
# compatibility with old binaries.  Increment Current.  Set Age to 0,
# since we're backward compatible with no previous APIs.  Set Revision
# to 0 too.
AC_SUBST(LIBUNBOUND_CURRENT)
AC_SUBST(LIBUNBOUND_REVISION)
AC_SUBST(LIBUNBOUND_AGE)


cmdln="`echo $@ | sed -e 's/\\\\/\\\\\\\\/g' | sed -e 's/"/\\\\"/'g`"
AC_DEFINE_UNQUOTED(CONFCMDLINE, ["$cmdln"], [Command line arguments used with configure])

CFLAGS="$CFLAGS"
AC_USE_SYSTEM_EXTENSIONS
if test "$ac_cv_header_minix_config_h" = "yes"; then
	AC_DEFINE(_NETBSD_SOURCE,1, [Enable for compile on Minix])
fi

dnl
dnl By default set prefix to /usr/local
dnl
case "$prefix" in
        NONE)
		prefix="/usr/local"
        ;;
esac
case "$exec_prefix" in
        NONE)
		exec_prefix="$prefix"
        ;;
esac

# are we on MinGW?
if uname -s 2>&1 | grep MINGW >/dev/null; then on_mingw="yes"
else
	if echo $host | grep mingw >/dev/null; then on_mingw="yes"
	else on_mingw="no"; fi
fi

#
# Determine configuration file
# the eval is to evaluate shell expansion twice
UNBOUND_SBIN_DIR=`eval echo "${sbindir}"`
AC_SUBST(UNBOUND_SBIN_DIR)
UNBOUND_SYSCONF_DIR=`eval echo "${sysconfdir}"`
AC_SUBST(UNBOUND_SYSCONF_DIR)
UNBOUND_LOCALSTATE_DIR=`eval echo "${localstatedir}"`
AC_SUBST(UNBOUND_LOCALSTATE_DIR)
if test $on_mingw = "no"; then
  ub_conf_file=`eval echo "${sysconfdir}/unbound/unbound.conf"`
else
  ub_conf_file="C:\\Program Files\\Unbound\\service.conf"
fi
AC_ARG_WITH([conf_file],
        AS_HELP_STRING([--with-conf-file=path],[Pathname to the Unbound configuration file]),
	[ub_conf_file="$withval"])
AC_SUBST(ub_conf_file)
ACX_ESCAPE_BACKSLASH($ub_conf_file, hdr_config)
AC_DEFINE_UNQUOTED(CONFIGFILE, ["$hdr_config"], [Pathname to the Unbound configuration file])
ub_conf_dir=`AS_DIRNAME(["$ub_conf_file"])`
AC_SUBST(ub_conf_dir)

# Determine run, chroot directory and pidfile locations
AC_ARG_WITH(run-dir,
    AS_HELP_STRING([--with-run-dir=path],[set default directory to chdir to (by default dir part of cfg file)]),
    UNBOUND_RUN_DIR="$withval",
if test $on_mingw = no; then
    UNBOUND_RUN_DIR=`dirname "$ub_conf_file"`
else
    UNBOUND_RUN_DIR=""
fi
)
AC_SUBST(UNBOUND_RUN_DIR)
ACX_ESCAPE_BACKSLASH($UNBOUND_RUN_DIR, hdr_run)
AC_DEFINE_UNQUOTED(RUN_DIR, ["$hdr_run"], [Directory to chdir to])

AC_ARG_WITH(chroot-dir,
    AS_HELP_STRING([--with-chroot-dir=path],[set default directory to chroot to (by default same as run-dir)]),
    UNBOUND_CHROOT_DIR="$withval",
if test $on_mingw = no; then
    UNBOUND_CHROOT_DIR="$UNBOUND_RUN_DIR"
else
    UNBOUND_CHROOT_DIR=""
fi
)
AC_SUBST(UNBOUND_CHROOT_DIR)
ACX_ESCAPE_BACKSLASH($UNBOUND_CHROOT_DIR, hdr_chroot)
AC_DEFINE_UNQUOTED(CHROOT_DIR, ["$hdr_chroot"], [Directory to chroot to])

AC_ARG_WITH(share-dir,
    AS_HELP_STRING([--with-share-dir=path],[set default directory with shared data (by default same as share/unbound)]),
    UNBOUND_SHARE_DIR="$withval",
    UNBOUND_SHARE_DIR="$UNBOUND_RUN_DIR")
AC_SUBST(UNBOUND_SHARE_DIR)
AC_DEFINE_UNQUOTED(SHARE_DIR, ["$UNBOUND_SHARE_DIR"], [Shared data])

AC_ARG_WITH(pidfile,
    AS_HELP_STRING([--with-pidfile=filename],[set default pathname to unbound pidfile (default run-dir/unbound.pid)]),
    UNBOUND_PIDFILE="$withval",
if test $on_mingw = no; then
    UNBOUND_PIDFILE="$UNBOUND_RUN_DIR/unbound.pid"
else
    UNBOUND_PIDFILE=""
fi
)
AC_SUBST(UNBOUND_PIDFILE)
ACX_ESCAPE_BACKSLASH($UNBOUND_PIDFILE, hdr_pid)
AC_DEFINE_UNQUOTED(PIDFILE, ["$hdr_pid"], [default pidfile location])

AC_ARG_WITH(rootkey-file,
    AS_HELP_STRING([--with-rootkey-file=filename],[set default pathname to root key file (default run-dir/root.key). This file is read and written.]),
    UNBOUND_ROOTKEY_FILE="$withval",
if test $on_mingw = no; then
    UNBOUND_ROOTKEY_FILE="$UNBOUND_RUN_DIR/root.key"
else
    UNBOUND_ROOTKEY_FILE="C:\\Program Files\\Unbound\\root.key"
fi
)
AC_SUBST(UNBOUND_ROOTKEY_FILE)
ACX_ESCAPE_BACKSLASH($UNBOUND_ROOTKEY_FILE, hdr_rkey)
AC_DEFINE_UNQUOTED(ROOT_ANCHOR_FILE, ["$hdr_rkey"], [default rootkey location])

AC_ARG_WITH(rootcert-file,
    AS_HELP_STRING([--with-rootcert-file=filename],[set default pathname to root update certificate file (default run-dir/icannbundle.pem).  This file need not exist if you are content with the builtin.]),
    UNBOUND_ROOTCERT_FILE="$withval",
if test $on_mingw = no; then
    UNBOUND_ROOTCERT_FILE="$UNBOUND_RUN_DIR/icannbundle.pem"
else
    UNBOUND_ROOTCERT_FILE="C:\\Program Files\\Unbound\\icannbundle.pem"
fi
)
AC_SUBST(UNBOUND_ROOTCERT_FILE)
ACX_ESCAPE_BACKSLASH($UNBOUND_ROOTCERT_FILE, hdr_rpem)
AC_DEFINE_UNQUOTED(ROOT_CERT_FILE, ["$hdr_rpem"], [default rootcert location])

AC_ARG_WITH(username,
    AS_HELP_STRING([--with-username=user],[set default user that unbound changes to (default user is unbound)]),
    UNBOUND_USERNAME="$withval",
    UNBOUND_USERNAME="unbound")
AC_SUBST(UNBOUND_USERNAME)
AC_DEFINE_UNQUOTED(UB_USERNAME, ["$UNBOUND_USERNAME"], [default username])

AC_DEFINE(WINVER, 0x0502, [the version of the windows API enabled])
ACX_RSRC_VERSION(wnvs)
AC_DEFINE_UNQUOTED(RSRC_PACKAGE_VERSION, [$wnvs], [version number for resource files])

# Check for 'grep -e' program, here, since ACX_CHECK_FLTO needs that.
AC_PROG_GREP

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_LANG([C])
# allow user to override the -g -O2 flags.
default_cflags=no
if test "x$CFLAGS" = "x" ; then
ACX_CHECK_COMPILER_FLAG(g, [CFLAGS="$CFLAGS -g"])
ACX_CHECK_COMPILER_FLAG(O2, [CFLAGS="$CFLAGS -O2"])
default_cflags=yes
fi
m4_version_prereq([2.70], [AC_PROG_CC], [AC_PROG_CC_STDC])
ACX_DEPFLAG
ACX_DETERMINE_EXT_FLAGS_UNBOUND

# debug mode flags warnings
AC_ARG_ENABLE(checking, AS_HELP_STRING([--enable-checking],[Enable warnings, asserts, makefile-dependencies]))
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug],[same as enable-checking]))
if test "$enable_debug" = "yes"; then debug_enabled="$enable_debug";
else debug_enabled="$enable_checking"; fi
AC_SUBST(debug_enabled)
case "$debug_enabled" in
        yes)
		ACX_CHECK_COMPILER_FLAG(W, [CFLAGS="$CFLAGS -W"])
		ACX_CHECK_COMPILER_FLAG(Wall, [CFLAGS="$CFLAGS -Wall"])
		ACX_CHECK_COMPILER_FLAG(Wextra, [CFLAGS="$CFLAGS -Wextra"])
		ACX_CHECK_COMPILER_FLAG(Wdeclaration-after-statement, [CFLAGS="$CFLAGS -Wdeclaration-after-statement"])
		AC_DEFINE([UNBOUND_DEBUG], [], [define this to enable debug checks.])
		;;
	no|*)
		# nothing to do.
		;;
esac
if test "$default_cflags" = "yes"; then
	# only when CFLAGS was "" at the start, if the users wants to
	# override we shouldn't add default cflags, because they wouldn't
	# be able to turn off these options and set the CFLAGS wanted.
	ACX_CHECK_FLTO
	ACX_CHECK_PIE
	ACX_CHECK_RELRO_NOW
fi

AC_C_INLINE
ACX_CHECK_FORMAT_ATTRIBUTE
ACX_CHECK_UNUSED_ATTRIBUTE

AC_DEFUN([CHECK_WEAK_ATTRIBUTE],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether the C compiler (${CC-cc}) accepts the "weak" attribute)
AC_CACHE_VAL(ac_cv_c_weak_attribute,
[ac_cv_c_weak_attribute=no
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <stdio.h>
__attribute__((weak)) void f(int x) { printf("%d", x); }
]], [[
   f(1);
]])],[ac_cv_c_weak_attribute="yes"],[ac_cv_c_weak_attribute="no"])
])

AC_MSG_RESULT($ac_cv_c_weak_attribute)
if test $ac_cv_c_weak_attribute = yes; then
  AC_DEFINE(HAVE_ATTR_WEAK, 1, [Whether the C compiler accepts the "weak" attribute])
  AC_DEFINE(ATTR_WEAK, [__attribute__((weak))], [apply the weak attribute to a symbol])
else
  AC_DEFINE(ATTR_WEAK,[], [apply the weak attribute to a symbol])
fi
])dnl End of CHECK_WEAK_ATTRIBUTE

CHECK_WEAK_ATTRIBUTE

AC_DEFUN([CHECK_NORETURN_ATTRIBUTE],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether the C compiler (${CC-cc}) accepts the "noreturn" attribute)
AC_CACHE_VAL(ac_cv_c_noreturn_attribute,
[ac_cv_c_noreturn_attribute=no
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <stdio.h>
__attribute__((noreturn)) void f(int x) { printf("%d", x); }
]], [[
   f(1);
]])],[ac_cv_c_noreturn_attribute="yes"],[ac_cv_c_noreturn_attribute="no"])
])

AC_MSG_RESULT($ac_cv_c_noreturn_attribute)
if test $ac_cv_c_noreturn_attribute = yes; then
  AC_DEFINE(HAVE_ATTR_NORETURN, 1, [Whether the C compiler accepts the "noreturn" attribute])
  AC_DEFINE(ATTR_NORETURN, [__attribute__((__noreturn__))], [apply the noreturn attribute to a function that exits the program])
else
  AC_DEFINE(ATTR_NORETURN,[], [apply the noreturn attribute to a function that exits the program])
fi
])dnl End of CHECK_NORETURN_ATTRIBUTE

CHECK_NORETURN_ATTRIBUTE

AC_DEFUN([CHECK_FALLTHROUGH_ATTRIBUTE],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether the C compiler (${CC-cc}) accepts the "fallthrough" attribute)
BAKCFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_CACHE_VAL(ac_cv_c_fallthrough_attribute,
[ac_cv_c_fallthrough_attribute=no
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ #include <stdio.h>
void f(int x) {
	int y = 0;
	switch(x) {
	case 1:
		y = 1;
		__attribute__((fallthrough));
		/* fallthrough */
	case 2:
		y++;
		break;
	case 3:
		y = 3;
		break;
	}
	printf("%d", y);
}
]], [[
   f(1);
]])],[ac_cv_c_fallthrough_attribute="yes"],[ac_cv_c_fallthrough_attribute="no"])
])
CFLAGS="$BAKCFLAGS"

AC_MSG_RESULT($ac_cv_c_fallthrough_attribute)
if test $ac_cv_c_fallthrough_attribute = yes; then
  AC_DEFINE(HAVE_ATTR_FALLTHROUGH, 1, [Whether the C compiler accepts the "fallthrough" attribute])
  AC_DEFINE(ATTR_FALLTHROUGH, [__attribute__((fallthrough));], [apply the fallthrough attribute.])
else
  AC_DEFINE(ATTR_FALLTHROUGH,[], [apply the fallthrough attribute.])
fi
])dnl End of CHECK_FALLTHROUGH_ATTRIBUTE

CHECK_FALLTHROUGH_ATTRIBUTE

if test "$srcdir" != "."; then
	CPPFLAGS="$CPPFLAGS -I$srcdir"
fi

AC_DEFUN([ACX_YYLEX_DESTROY], [
	AC_MSG_CHECKING([for yylex_destroy])
	if echo %% | $LEX -t 2>&1 | grep yylex_destroy >/dev/null 2>&1; then
		AC_DEFINE(LEX_HAS_YYLEX_DESTROY, 1, [if lex has yylex_destroy])
		AC_MSG_RESULT(yes)
	else AC_MSG_RESULT(no);
		LEX=":"
	fi
])

AC_DEFUN([ACX_YYLEX_OPTION], [
	AC_MSG_CHECKING([for lex %option])
	if cat <<EOF | $LEX -t 2>&1 | grep yy_delete_buffer >/dev/null 2>&1; then
%option nounput
%%
EOF
		AC_MSG_RESULT(yes)
	else AC_MSG_RESULT(no);
		LEX=":"
	fi
])

AC_PROG_LEX([noyywrap])
if test "$LEX" != "" -a "$LEX" != ":"; then
ACX_YYLEX_DESTROY
fi
if test "$LEX" != "" -a "$LEX" != ":"; then
ACX_YYLEX_OPTION
fi
if test "$LEX" = "" -o "$LEX" = ":"; then
	if test ! -f util/configlexer.c; then
		AC_MSG_ERROR([no lex and no util/configlexer.c: need flex and bison to compile from source repository.])
	fi
fi
AC_PROG_YACC
if test "$YACC" = "" -o "$YACC" = ":"; then
	if test ! -f util/configparser.c; then
		AC_MSG_ERROR([no yacc and no util/configparser.c: need flex and bison to compile from source repository.])
	fi
fi
AC_CHECK_PROG(doxygen, doxygen, doxygen)
AC_CHECK_TOOL(STRIP, strip)
ACX_LIBTOOL_C_ONLY

# pkg-config is only needed for these options, do not require it otherwise
if test "$enable_systemd" = "yes" -o "$enable_dnstap" = "yes" -o "$with_pyunbound" = "yes" -o "$with_pythonmod" = "yes"; then
PKG_PROG_PKG_CONFIG
fi

# Checks for header files.
AC_CHECK_HEADERS([stdarg.h stdbool.h netinet/in.h netinet/tcp.h sys/param.h sys/select.h sys/socket.h sys/un.h sys/uio.h sys/resource.h arpa/inet.h syslog.h netdb.h sys/wait.h pwd.h glob.h grp.h login_cap.h winsock2.h ws2tcpip.h endian.h sys/endian.h libkern/OSByteOrder.h sys/ipc.h sys/shm.h ifaddrs.h poll.h],,, [AC_INCLUDES_DEFAULT])
# net/if.h portability for Darwin see:
# https://www.gnu.org/software/autoconf/manual/autoconf-2.69/html_node/Header-Portability.html
AC_CHECK_HEADERS([net/if.h],,, [
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
])

# Check for Apple header. This uncovers TARGET_OS_IPHONE, TARGET_OS_TV or TARGET_OS_WATCH
AC_CHECK_HEADERS([TargetConditionals.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([netioapi.h],,, [AC_INCLUDES_DEFAULT
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif
])

# Check for Linux timestamping headers
AC_CHECK_HEADERS([linux/net_tstamp.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([stdatomic.h],,, [AC_INCLUDES_DEFAULT])

# check for types.  
# Using own tests for int64* because autoconf builtin only give 32bit.
AC_CHECK_TYPE(int8_t, signed char)
AC_CHECK_TYPE(int16_t, short)
AC_CHECK_TYPE(int32_t, int)
AC_CHECK_TYPE(int64_t, long long)
AC_CHECK_TYPE(uint8_t, unsigned char)
AC_CHECK_TYPE(uint16_t, unsigned short)
AC_CHECK_TYPE(uint32_t, unsigned int)
AC_CHECK_TYPE(uint64_t, unsigned long long)
AC_TYPE_SIZE_T
AC_CHECK_TYPE(ssize_t, int)
AC_TYPE_UID_T
AC_TYPE_PID_T
AC_TYPE_OFF_T
ACX_TYPE_U_CHAR
ACX_TYPE_RLIM_T
ACX_TYPE_SOCKLEN_T
ACX_TYPE_IN_ADDR_T
ACX_TYPE_IN_PORT_T
ACX_CHECK_MEMCMP_SIGNED

AC_CHECK_SIZEOF(time_t,,[
AC_INCLUDES_DEFAULT
#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
])
AC_CHECK_SIZEOF(size_t)

# add option to disable the evil rpath
ACX_ARG_RPATH
AC_SUBST(RUNTIME_PATH)

# check to see if libraries are needed for these functions.
AC_SEARCH_LIBS([inet_pton], [nsl])
AC_SEARCH_LIBS([socket], [socket])

# check whether strptime also works
AC_DEFUN([AC_CHECK_STRPTIME_WORKS],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING(whether strptime works)
if test c${cross_compiling} = cno; then
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#define _XOPEN_SOURCE 600
#include <time.h>
int main(void) { struct tm tm; char *res;
res = strptime("2010-07-15T00:00:00+00:00", "%t%Y%t-%t%m%t-%t%d%tT%t%H%t:%t%M%t:%t%S%t", &tm);
if (!res) return 2;
res = strptime("20070207111842", "%Y%m%d%H%M%S", &tm);
if (!res) return 1; return 0; }
]])] , [eval "ac_cv_c_strptime_works=yes"], [eval "ac_cv_c_strptime_works=no"],
[eval "ac_cv_c_strptime_works=maybe"])
else
eval "ac_cv_c_strptime_works=maybe"
fi
AC_MSG_RESULT($ac_cv_c_strptime_works)
if test $ac_cv_c_strptime_works = no; then
AC_LIBOBJ(strptime)
else
AC_DEFINE_UNQUOTED([STRPTIME_WORKS], 1, [use default strptime.])
fi
])dnl

# check some functions of the OS before linking libs (while still runnable).
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_DEFINE(RETSIGTYPE,void,[Return type of signal handlers, but autoconf 2.70 says 'your code may safely assume C89 semantics that RETSIGTYPE is void.'])
AC_FUNC_FSEEKO
ACX_SYS_LARGEFILE
ACX_CHECK_NONBLOCKING_BROKEN
ACX_MKDIR_ONE_ARG
AC_CHECK_FUNCS([strptime],[AC_CHECK_STRPTIME_WORKS],[AC_LIBOBJ([strptime])])

# check if we can use SO_REUSEPORT
reuseport_default=0
if echo "$host" | $GREP -i -e linux >/dev/null; then reuseport_default=1; fi
if echo "$host" | $GREP -i -e dragonfly >/dev/null; then reuseport_default=1; fi
if test "$reuseport_default" = 1; then
	AC_DEFINE(REUSEPORT_DEFAULT, 1, [if REUSEPORT is enabled by default])
else
	AC_DEFINE(REUSEPORT_DEFAULT, 0, [if REUSEPORT is enabled by default])
fi

# Include systemd.m4 - begin
sinclude(systemd.m4)
# Include systemd.m4 - end

# set memory allocation checking if requested
AC_ARG_ENABLE(alloc-checks, AS_HELP_STRING([--enable-alloc-checks],[ enable to memory allocation statistics, for debug purposes ]),
	, )
AC_ARG_ENABLE(alloc-lite, AS_HELP_STRING([--enable-alloc-lite],[ enable for lightweight alloc assertions, for debug purposes ]),
	, )
AC_ARG_ENABLE(alloc-nonregional, AS_HELP_STRING([--enable-alloc-nonregional],[ enable nonregional allocs, slow but exposes regional allocations to other memory purifiers, for debug purposes ]),
	, )
if test x_$enable_alloc_nonregional = x_yes; then
	AC_DEFINE(UNBOUND_ALLOC_NONREGIONAL, 1, [use malloc not regions, for debug use])
fi
if test x_$enable_alloc_checks = x_yes; then
	AC_DEFINE(UNBOUND_ALLOC_STATS, 1, [use statistics for allocs and frees, for debug use])
	SLDNS_ALLOCCHECK_EXTRA_OBJ="alloc.lo log.lo"
	AC_SUBST(SLDNS_ALLOCCHECK_EXTRA_OBJ)
	ASYNCLOOK_ALLOCCHECK_EXTRA_OBJ="alloc.lo"
	AC_SUBST(ASYNCLOOK_ALLOCCHECK_EXTRA_OBJ)
else
	if test x_$enable_alloc_lite = x_yes; then
		AC_DEFINE(UNBOUND_ALLOC_LITE, 1, [use to enable lightweight alloc assertions, for debug use])
	else
		ACX_FUNC_MALLOC([unbound])
	fi
fi

# check windows threads (we use them, not pthreads, on windows).
if test "$on_mingw" = "yes"; then
# check windows threads
	AC_CHECK_HEADERS([windows.h],,, [AC_INCLUDES_DEFAULT])
	AC_MSG_CHECKING([for CreateThread])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif
], [
	HANDLE t = CreateThread(NULL, 0, NULL, NULL, 0, NULL);
])],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_WINDOWS_THREADS, 1, [Using Windows threads])
,
	AC_MSG_RESULT(no)
)

else
# not on mingw, check thread libraries.

# check for thread library.
# check this first, so that the pthread lib does not get linked in via
# libssl or libpython, and thus distorts the tests, and we end up using
# the non-threadsafe C libraries.
AC_ARG_WITH(pthreads, AS_HELP_STRING([--with-pthreads],[use pthreads library, or --without-pthreads to disable threading support.]),
 [ ],[ withval="yes" ])
ub_have_pthreads=no
if test x_$withval != x_no; then
	AX_PTHREAD([
		AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.])
		if test -n "$PTHREAD_LIBS"; then
		  LIBS="$PTHREAD_LIBS $LIBS"
		fi
		CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
		CC="$PTHREAD_CC"
		ub_have_pthreads=yes
		AC_CHECK_TYPES([pthread_spinlock_t, pthread_rwlock_t],,,[#include <pthread.h>])
		AC_CHECK_SIZEOF([unsigned long])
		AC_CHECK_SIZEOF(pthread_t)

		if echo "$CFLAGS" | $GREP -e "-pthread" >/dev/null; then
		AC_MSG_CHECKING([if -pthread unused during linking])
		# catch clang warning 'argument unused during compilation'
		AC_LANG_CONFTEST([AC_LANG_SOURCE(AC_INCLUDES_DEFAULT
[[
int main(void) {return 0;}
]])])
		pthread_unused="yes"
		# first compile
		echo "$CC $CFLAGS -c conftest.c -o conftest.o" >&AS_MESSAGE_LOG_FD
		$CC $CFLAGS -c conftest.c -o conftest.o 2>&AS_MESSAGE_LOG_FD >&AS_MESSAGE_LOG_FD
		if test $? = 0; then
			# then link
			echo "$CC $CFLAGS -Werror $LDFLAGS $LIBS -o conftest contest.o" >&AS_MESSAGE_LOG_FD
			$CC $CFLAGS -Werror $LDFLAGS $LIBS -o conftest conftest.o 2>&AS_MESSAGE_LOG_FD >&AS_MESSAGE_LOG_FD
			if test $? -ne 0; then
				AC_MSG_RESULT(yes)
				CFLAGS=`echo "$CFLAGS" | sed -e 's/-pthread//'`
				PTHREAD_CFLAGS_ONLY="-pthread"
				AC_SUBST(PTHREAD_CFLAGS_ONLY)
			else
				AC_MSG_RESULT(no)
			fi
		else
			AC_MSG_RESULT(no)
		fi # endif cc successful
		rm -f conftest conftest.c conftest.o
		fi # endif -pthread in CFLAGS

		])
fi

# check solaris thread library
AC_ARG_WITH(solaris-threads, AS_HELP_STRING([--with-solaris-threads],[use solaris native thread library.]), [ ],[ withval="no" ])
ub_have_sol_threads=no
if test x_$withval != x_no; then
	if test x_$ub_have_pthreads != x_no; then
	    AC_MSG_WARN([Have pthreads already, ignoring --with-solaris-threads])
	else
	AC_SEARCH_LIBS(thr_create, [thread],
	[
    		AC_DEFINE(HAVE_SOLARIS_THREADS, 1, [Using Solaris threads])

		ACX_CHECK_COMPILER_FLAG(mt, [CFLAGS="$CFLAGS -mt"],
			[CFLAGS="$CFLAGS -D_REENTRANT"])
		ub_have_sol_threads=yes
	] , [
		AC_MSG_ERROR([no solaris threads found.])
	])
	fi
fi

fi # end of non-mingw check of thread libraries

# Check for SYSLOG_FACILITY
AC_ARG_WITH(syslog-facility, AS_HELP_STRING([--with-syslog-facility=LOCAL0 - LOCAL7],[ set SYSLOG_FACILITY, default DAEMON ]),
	[ UNBOUND_SYSLOG_FACILITY="$withval" ], [])
case "${UNBOUND_SYSLOG_FACILITY}" in

  LOCAL[[0-7]]) UNBOUND_SYSLOG_FACILITY="LOG_${UNBOUND_SYSLOG_FACILITY}" ;;

           *) UNBOUND_SYSLOG_FACILITY="LOG_DAEMON" ;;

esac
AC_DEFINE_UNQUOTED(UB_SYSLOG_FACILITY,${UNBOUND_SYSLOG_FACILITY},[the SYSLOG_FACILITY to use, default LOG_DAEMON])

# Check for dynamic library module
AC_ARG_WITH(dynlibmodule,
   AS_HELP_STRING([--with-dynlibmodule],[build dynamic library module, or --without-dynlibmodule to disable it. (default=no)]),
   [], [ withval="no" ])

if test x_$withval != x_no; then
  AC_DEFINE(WITH_DYNLIBMODULE, 1, [Define if you want dynlib module.])
  WITH_DYNLIBMODULE=yes
  AC_SUBST(WITH_DYNLIBMODULE)
  DYNLIBMOD_OBJ="dynlibmod.lo"
  AC_SUBST(DYNLIBMOD_OBJ)
  DYNLIBMOD_HEADER='$(srcdir)/dynlibmod/dynlibmod.h'
  AC_SUBST(DYNLIBMOD_HEADER)
  if test $on_mingw = "no"; then
    # link with -ldl if not already there, for all executables because
    # dlopen call is in the dynlib module.  For unbound executable, also
    # export symbols.
    AC_SEARCH_LIBS([dlopen], [dl])
    DYNLIBMOD_EXTRALIBS="-export-dynamic"
  else
    DYNLIBMOD_EXTRALIBS="-Wl,--export-all-symbols,--out-implib,libunbound.dll.a"
  fi
  AC_SUBST(DYNLIBMOD_EXTRALIBS)
fi

# Check for PyUnbound
AC_ARG_WITH(pyunbound,
   AS_HELP_STRING([--with-pyunbound],[build PyUnbound, or --without-pyunbound to skip it. (default=no)]),
   [], [ withval="no" ])

ub_test_python=no
ub_with_pyunbound=no
if test x_$withval != x_no; then
   ub_with_pyunbound=yes
   ub_test_python=yes
fi

# Check for Python module
AC_ARG_WITH(pythonmodule,
   AS_HELP_STRING([--with-pythonmodule],[build Python module, or --without-pythonmodule to disable script engine. (default=no)]),
   [], [ withval="no" ])

ub_with_pythonmod=no
if test x_$withval != x_no; then
   ub_with_pythonmod=yes
   ub_test_python=yes
fi

# Check for Python & SWIG only on PyUnbound or PyModule
if test x_$ub_test_python != x_no; then

   # Check for Python
   ub_have_python=no
   ac_save_LIBS="$LIBS" dnl otherwise AC_PYTHON_DEVEL thrashes $LIBS
   AC_PYTHON_DEVEL
   if test ! -z "$PYTHON_VERSION"; then
	badversion="no"
	if test "$PYTHON_VERSION_MAJOR" -lt 2; then
		badversion="yes"
	fi
	if test "$PYTHON_VERSION_MAJOR" -eq 2 -a "$PYTHON_VERSION_MINOR" -lt 4; then
		badversion="yes"
	fi
	if test "$badversion" = "yes"; then
		AC_MSG_ERROR([Python version >= 2.4.0 is required])
	fi

      [PY_MAJOR_VERSION="`$PYTHON -c \"import sys; print(sys.version_info[0])\"`"]
      AC_SUBST(PY_MAJOR_VERSION)
      # Have Python
      AC_DEFINE(HAVE_PYTHON,1,[Define if you have Python libraries and header files.])
      if test x_$ub_with_pythonmod != x_no; then
        if test -n "$LIBS"; then
          LIBS="$PYTHON_LDFLAGS $LIBS"
        else
          LIBS="$PYTHON_LDFLAGS"
        fi
      fi
      PYTHON_LIBS="$PYTHON_LDFLAGS"
      AC_SUBST(PYTHON_LIBS)
      if test -n "$CPPFLAGS"; then
        CPPFLAGS="$CPPFLAGS $PYTHON_CPPFLAGS"
      else
        CPPFLAGS="$PYTHON_CPPFLAGS"
      fi
      if test "$PYTHON_LIBDIR" != "/usr/lib" -a "$PYTHON_LIBDIR" != "" -a "$PYTHON_LIBDIR" != "/usr/lib64"; then
        ACX_RUNTIME_PATH_ADD([$PYTHON_LIBDIR])
      fi
      ub_have_python=yes
      PKG_CHECK_EXISTS(["python${PY_MAJOR_VERSION}"],
                       [PC_PY_DEPENDENCY="python${PY_MAJOR_VERSION}"],
                       [PC_PY_DEPENDENCY="python"])
      AC_SUBST(PC_PY_DEPENDENCY)

      # Check for SWIG
      ub_have_swig=no
      AC_ARG_ENABLE(swig-version-check, AS_HELP_STRING([--disable-swig-version-check],[Disable swig version check to build python modules with older swig even though that is unreliable]))
      if test "$enable_swig_version_check" = "yes"; then
      	AX_PKG_SWIG(2.0.1)
      else
      	AX_PKG_SWIG
      fi
      AC_MSG_CHECKING(SWIG)
      if test ! -x "$SWIG"; then
         AC_MSG_ERROR([failed to find swig tool, install it, or do not build Python module and PyUnbound])
      else
         AC_DEFINE(HAVE_SWIG, 1, [Define if you have Swig libraries and header files.])
         AC_SUBST(swig, "$SWIG")
         AC_MSG_RESULT(present)

         # If have Python & SWIG
         # Declare PythonMod
         if test x_$ub_with_pythonmod != x_no; then
            AC_DEFINE(WITH_PYTHONMODULE, 1, [Define if you want Python module.])
            WITH_PYTHONMODULE=yes
            AC_SUBST(WITH_PYTHONMODULE)
	    PYTHONMOD_OBJ="pythonmod.lo pythonmod_utils.lo"
	    AC_SUBST(PYTHONMOD_OBJ)
	    PYTHONMOD_HEADER='$(srcdir)/pythonmod/pythonmod.h'
	    AC_SUBST(PYTHONMOD_HEADER)
	    PYTHONMOD_INSTALL=pythonmod-install
	    AC_SUBST(PYTHONMOD_INSTALL)
	    PYTHONMOD_UNINSTALL=pythonmod-uninstall
	    AC_SUBST(PYTHONMOD_UNINSTALL)
         fi

         # Declare PyUnbound
         if test x_$ub_with_pyunbound != x_no; then
            AC_DEFINE(WITH_PYUNBOUND, 1, [Define if you want PyUnbound.])
            WITH_PYUNBOUND=yes
            AC_SUBST(WITH_PYUNBOUND)
	    PYUNBOUND_OBJ="libunbound_wrap.lo"
	    AC_SUBST(PYUNBOUND_OBJ)
	    PYUNBOUND_TARGET="_unbound.la"
	    AC_SUBST(PYUNBOUND_TARGET)
	    PYUNBOUND_INSTALL=pyunbound-install
	    AC_SUBST(PYUNBOUND_INSTALL)
	    PYUNBOUND_UNINSTALL=pyunbound-uninstall
	    AC_SUBST(PYUNBOUND_UNINSTALL)
         fi
      fi
   else
      AC_MSG_RESULT([*** Python libraries not found, won't build PythonMod or PyUnbound ***])
      ub_with_pyunbound=no
      ub_with_pythonmod=no
   fi
fi

if test "`uname`" = "NetBSD"; then
	NETBSD_LINTFLAGS='"-D__RENAME(x)=" -D_NETINET_IN_H_'
	AC_SUBST(NETBSD_LINTFLAGS)
fi

if test "`uname`" = "Linux"; then
	# splint cannot parse modern c99 header files
	GCC_DOCKER_LINTFLAGS='-syntax'
	AC_SUBST(GCC_DOCKER_LINTFLAGS)
fi
AX_BUILD_DATE_EPOCH(CONFIG_DATE, [%Y%m%d])
AC_ARG_VAR(SOURCE_DATE_EPOCH, [If it is set, it uses the value of that variable instead of the current time as the build timestamp. The format is a unix timestamp. This enables reproducible build output.])
AC_SUBST(CONFIG_DATE)

# Checks for libraries.

# libnss
USE_NSS="no"
AC_ARG_WITH([nss], AS_HELP_STRING([--with-nss=path],[use libnss instead of openssl, installed at path.]),
	[
	USE_NSS="yes"
	AC_DEFINE(HAVE_NSS, 1, [Use libnss for crypto])
	if test "$withval" != "" -a "$withval" != "yes"; then
		CPPFLAGS="$CPPFLAGS -I$withval/include/nss3"
		LDFLAGS="$LDFLAGS -L$withval/lib"
		ACX_RUNTIME_PATH_ADD([$withval/lib])
		CPPFLAGS="-I$withval/include/nspr4 $CPPFLAGS"
	else
		CPPFLAGS="$CPPFLAGS -I/usr/include/nss3"
		CPPFLAGS="-I/usr/include/nspr4 $CPPFLAGS"
	fi
        LIBS="$LIBS -lnss3 -lnspr4"
	SSLLIB=""
	PC_CRYPTO_DEPENDENCY="nss nspr"
	AC_SUBST(PC_CRYPTO_DEPENDENCY)
	]
)

# libnettle
USE_NETTLE="no"
AC_ARG_WITH([nettle], AS_HELP_STRING([--with-nettle=path],[use libnettle as crypto library, installed at path.]),
	[
	USE_NETTLE="yes"
	AC_DEFINE(HAVE_NETTLE, 1, [Use libnettle for crypto])
	AC_CHECK_HEADERS([nettle/dsa-compat.h],,, [AC_INCLUDES_DEFAULT])
	if test "$withval" != "" -a "$withval" != "yes"; then
		CPPFLAGS="$CPPFLAGS -I$withval/include/nettle"
		LDFLAGS="$LDFLAGS -L$withval/lib"
		ACX_RUNTIME_PATH_ADD([$withval/lib])
	else
		CPPFLAGS="$CPPFLAGS -I/usr/include/nettle"
	fi
        LIBS="$LIBS -lhogweed -lnettle -lgmp"
	SSLLIB=""
	PC_CRYPTO_DEPENDENCY="hogweed nettle"
	AC_SUBST(PC_CRYPTO_DEPENDENCY)
	]
)

# openssl
if test $USE_NSS = "no" -a $USE_NETTLE = "no"; then
ACX_WITH_SSL
ACX_LIB_SSL
SSLLIB="-lssl"

PC_CRYPTO_DEPENDENCY=""
AC_SUBST(PC_CRYPTO_DEPENDENCY)

# check if -lcrypt32 is needed because CAPIENG needs that. (on windows)
BAKLIBS="$LIBS"
LIBS="-lssl $LIBS"
AC_MSG_CHECKING([if libssl needs -lcrypt32])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[
	int EVP_sha256(void);
	(void)EVP_sha256();
]])], [
	AC_MSG_RESULT([no])
	LIBS="$BAKLIBS"
], [
	AC_MSG_RESULT([yes])
	LIBS="$BAKLIBS"
	LIBS="$LIBS -lcrypt32"
])

AC_MSG_CHECKING([for LibreSSL])
if grep VERSION_TEXT $ssldir_include/openssl/opensslv.h | grep "LibreSSL" >/dev/null; then
	AC_MSG_RESULT([yes])
	AC_DEFINE([HAVE_LIBRESSL], [1], [Define if we have LibreSSL])
	# libressl provides these compat functions, but they may also be
	# declared by the OS in libc.  See if they have been declared.
	AC_CHECK_DECLS([strlcpy,strlcat,arc4random,arc4random_uniform])
else
	AC_MSG_RESULT([no])
fi
AC_CHECK_HEADERS([openssl/conf.h openssl/engine.h openssl/bn.h openssl/dh.h openssl/dsa.h openssl/rsa.h openssl/core_names.h openssl/param_build.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_FUNCS([OPENSSL_config EVP_sha1 EVP_sha256 EVP_sha512 FIPS_mode EVP_default_properties_is_fips_enabled EVP_MD_CTX_new OpenSSL_add_all_digests OPENSSL_init_crypto EVP_cleanup ENGINE_cleanup ERR_load_crypto_strings CRYPTO_cleanup_all_ex_data ERR_free_strings RAND_cleanup DSA_SIG_set0 EVP_dss1 EVP_DigestVerify EVP_aes_256_cbc EVP_EncryptInit_ex HMAC_Init_ex CRYPTO_THREADID_set_callback EVP_MAC_CTX_set_params OSSL_PARAM_BLD_new BIO_set_callback_ex SSL_CTX_set_tmp_ecdh])

# these check_funcs need -lssl
BAKLIBS="$LIBS"
LIBS="-lssl $LIBS"
AC_CHECK_FUNCS([OPENSSL_init_ssl SSL_CTX_set_security_level SSL_set1_host SSL_get0_peername X509_VERIFY_PARAM_set1_host SSL_CTX_set_ciphersuites SSL_CTX_set_tlsext_ticket_key_evp_cb SSL_CTX_set_alpn_select_cb SSL_get0_alpn_selected SSL_CTX_set_alpn_protos SSL_get1_peer_certificate])
LIBS="$BAKLIBS"

AC_CHECK_DECLS([SSL_COMP_get_compression_methods,sk_SSL_COMP_pop_free,SSL_CTX_set_ecdh_auto], [], [], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#ifdef HAVE_OPENSSL_CONF_H
#include <openssl/conf.h>
#endif

#ifdef HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif
#include <openssl/ssl.h>
#include <openssl/evp.h>
])

if test "$ac_cv_func_HMAC_Init_ex" = "yes"; then
# check function return type.
AC_MSG_CHECKING(the return type of HMAC_Init_ex)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#ifdef HAVE_OPENSSL_CONF_H
#include <openssl/conf.h>
#endif

#ifdef HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif
#include <openssl/ssl.h>
#include <openssl/evp.h>
], [
	HMAC_CTX* hmac_ctx = NULL;
	void* hmac_key = NULL;
	const EVP_MD* digest = NULL;
	int x = HMAC_Init_ex(hmac_ctx, hmac_key, 32, digest, NULL);
	(void)x;
])], [
	AC_MSG_RESULT(int)
], [
	AC_MSG_RESULT(void)
	AC_DEFINE([HMAC_INIT_EX_RETURNS_VOID], 1, [If HMAC_Init_ex() returns void])
])
fi

fi
AC_SUBST(SSLLIB)

# libbsd
AC_ARG_WITH([libbsd], AS_HELP_STRING([--with-libbsd],[Use portable libbsd functions]), [
	AC_CHECK_HEADERS([bsd/string.h bsd/stdlib.h],,, [AC_INCLUDES_DEFAULT])
	if test "x$ac_cv_header_bsd_string_h" = xyes -a "x$ac_cv_header_bsd_stdlib_h" = xyes; then
		for func in strlcpy strlcat arc4random arc4random_uniform reallocarray; do
			AC_SEARCH_LIBS([$func], [bsd], [
				AC_DEFINE(HAVE_LIBBSD, 1, [Use portable libbsd functions])
				PC_LIBBSD_DEPENDENCY=libbsd
				AC_SUBST(PC_LIBBSD_DEPENDENCY)
			])
		done
	fi
])

AC_ARG_ENABLE(sha1, AS_HELP_STRING([--disable-sha1],[Disable SHA1 RRSIG support, does not disable nsec3 support]))
case "$enable_sha1" in
	no)
	;;
	yes|*)
	AC_DEFINE([USE_SHA1], [1], [Define this to enable SHA1 support.])
	;;
esac


AC_ARG_ENABLE(sha2, AS_HELP_STRING([--disable-sha2],[Disable SHA256 and SHA512 RRSIG support]))
case "$enable_sha2" in
	no)
	;;
	yes|*)
	AC_DEFINE([USE_SHA2], [1], [Define this to enable SHA256 and SHA512 support.])
	;;
esac

AC_ARG_ENABLE(subnet, AS_HELP_STRING([--enable-subnet],[Enable client subnet]))
case "$enable_subnet" in
	yes)
	AC_DEFINE([CLIENT_SUBNET], [1], [Define this to enable client subnet option.])
	SUBNET_OBJ="edns-subnet.lo subnetmod.lo addrtree.lo subnet-whitelist.lo"
	AC_SUBST(SUBNET_OBJ)
	SUBNET_HEADER='$(srcdir)/edns-subnet/subnetmod.h $(srcdir)/edns-subnet/edns-subnet.h $(srcdir)/edns-subnet/subnet-whitelist.h $(srcdir)/edns-subnet/addrtree.h'
	AC_SUBST(SUBNET_HEADER)
	;;
	no|*)
	;;
esac

# check whether gost also works
AC_DEFUN([AC_CHECK_GOST_WORKS],
[AC_REQUIRE([AC_PROG_CC])
AC_MSG_CHECKING([if GOST works])
if test c${cross_compiling} = cno; then
BAKCFLAGS="$CFLAGS"
if test -n "$ssldir"; then
	CFLAGS="$CFLAGS -Wl,-rpath,$ssldir_lib"
fi
AC_RUN_IFELSE([AC_LANG_SOURCE([[
#include <string.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/engine.h>
#include <openssl/conf.h>
/* routine to load gost (from sldns) */
int load_gost_id(void)
{
	static int gost_id = 0;
	const EVP_PKEY_ASN1_METHOD* meth;
	ENGINE* e;

	if(gost_id) return gost_id;

	/* see if configuration loaded gost implementation from other engine*/
	meth = EVP_PKEY_asn1_find_str(NULL, "gost2001", -1);
	if(meth) {
		EVP_PKEY_asn1_get0_info(&gost_id, NULL, NULL, NULL, NULL, meth);
		return gost_id;
	}

	/* see if engine can be loaded already */
	e = ENGINE_by_id("gost");
	if(!e) {
		/* load it ourself, in case statically linked */
		ENGINE_load_builtin_engines();
		ENGINE_load_dynamic();
		e = ENGINE_by_id("gost");
	}
	if(!e) {
		/* no gost engine in openssl */
		return 0;
	}
	if(!ENGINE_set_default(e, ENGINE_METHOD_ALL)) {
		ENGINE_finish(e);
		ENGINE_free(e);
		return 0;
	}

	meth = EVP_PKEY_asn1_find_str(&e, "gost2001", -1);
	if(!meth) {
		/* algo not found */
		ENGINE_finish(e);
		ENGINE_free(e);
		return 0;
	}
	EVP_PKEY_asn1_get0_info(&gost_id, NULL, NULL, NULL, NULL, meth);
	return gost_id;
}
int main(void) {
	EVP_MD_CTX* ctx;
	const EVP_MD* md;
	unsigned char digest[64]; /* its a 256-bit digest, so uses 32 bytes */
	const char* str = "Hello world";
	const unsigned char check[] = {
		0x40 , 0xed , 0xf8 , 0x56 , 0x5a , 0xc5 , 0x36 , 0xe1 ,
		0x33 , 0x7c , 0x7e , 0x87 , 0x62 , 0x1c , 0x42 , 0xe0 ,
		0x17 , 0x1b , 0x5e , 0xce , 0xa8 , 0x46 , 0x65 , 0x4d ,
		0x8d , 0x3e , 0x22 , 0x9b , 0xe1 , 0x30 , 0x19 , 0x9d
	};
	OPENSSL_config(NULL);
	(void)load_gost_id();
	md = EVP_get_digestbyname("md_gost94");
	if(!md) return 1;
	memset(digest, 0, sizeof(digest));
	ctx = EVP_MD_CTX_create();
	if(!ctx) return 2;
	if(!EVP_DigestInit_ex(ctx, md, NULL)) return 3;
	if(!EVP_DigestUpdate(ctx, str, 10)) return 4;
	if(!EVP_DigestFinal_ex(ctx, digest, NULL)) return 5;
	/* uncomment to see the hash calculated.
		{int i;
		for(i=0; i<32; i++)
			printf(" %2.2x", (int)digest[i]);
		printf("\n");}
	*/
	if(memcmp(digest, check, sizeof(check)) != 0)
		return 6;
	return 0;
}
]])] , [eval "ac_cv_c_gost_works=yes"], [eval "ac_cv_c_gost_works=no"],
[eval "ac_cv_c_gost_works=maybe"])
CFLAGS="$BAKCFLAGS"
else
eval "ac_cv_c_gost_works=maybe"
fi
AC_MSG_RESULT($ac_cv_c_gost_works)
])dnl

AC_ARG_ENABLE(gost, AS_HELP_STRING([--disable-gost],[Disable GOST support]))
use_gost="no"
if test $USE_NSS = "no" -a $USE_NETTLE = "no"; then
case "$enable_gost" in
	no)
	;;
	*)
	AC_CHECK_FUNC(EVP_PKEY_set_type_str, [:],[AC_MSG_ERROR([OpenSSL 1.0.0 is needed for GOST support])])
	AC_CHECK_FUNC(EC_KEY_new, [], [AC_MSG_ERROR([OpenSSL does not support ECC, needed for GOST support])])
	AC_CHECK_GOST_WORKS
	if test "$ac_cv_c_gost_works" != no; then
		use_gost="yes"
		AC_DEFINE([USE_GOST], [1], [Define this to enable GOST support.])
	fi
	;;
esac
fi dnl !USE_NSS && !USE_NETTLE

AC_ARG_ENABLE(ecdsa, AS_HELP_STRING([--disable-ecdsa],[Disable ECDSA support]))
use_ecdsa="no"
case "$enable_ecdsa" in
    no)
      ;;
    *)
      if test $USE_NSS = "no" -a $USE_NETTLE = "no"; then
	      AC_CHECK_FUNC(EVP_PKEY_fromdata, [
	          # with EVP_PKEY_fromdata, check if EC is not disabled
	          AC_CHECK_DECL([OPENSSL_NO_EC], [AC_MSG_ERROR([OpenSSL does not support ECDSA: please upgrade or rerun with --disable-ecdsa])
		  ], [], [AC_INCLUDES_DEFAULT
#include <openssl/evp.h>
		  ])
		], [
		  # without EVP_PKEY_fromdata, older openssl, check for support
		  AC_CHECK_FUNC(ECDSA_sign, [], [AC_MSG_ERROR([OpenSSL does not support ECDSA: please upgrade or rerun with --disable-ecdsa])])
		  AC_CHECK_FUNC(SHA384_Init, [], [AC_MSG_ERROR([OpenSSL does not support SHA384: please upgrade or rerun with --disable-ecdsa])])
		])
	      AC_CHECK_DECLS([NID_X9_62_prime256v1, NID_secp384r1], [], [AC_MSG_ERROR([OpenSSL does not support the ECDSA curves: please upgrade or rerun with --disable-ecdsa])], [AC_INCLUDES_DEFAULT
#include <openssl/evp.h>
	      ])
	      # see if OPENSSL 1.0.0 or later (has EVP MD and Verify independency)
	      AC_MSG_CHECKING([if openssl supports SHA2 and ECDSA with EVP])
	      if grep OPENSSL_VERSION_TEXT $ssldir_include/openssl/opensslv.h | grep "OpenSSL" >/dev/null; then
		if grep OPENSSL_VERSION_NUMBER $ssldir_include/openssl/opensslv.h | grep 0x0 >/dev/null; then
		  AC_MSG_RESULT([no])
		  AC_DEFINE_UNQUOTED([USE_ECDSA_EVP_WORKAROUND], [1], [Define this to enable an EVP workaround for older openssl])
		else
		  AC_MSG_RESULT([yes])
		fi
	      else
		# not OpenSSL, thus likely LibreSSL, which supports it
		AC_MSG_RESULT([yes])
	      fi
      fi
      # we now know we have ECDSA and the required curves.
      AC_DEFINE_UNQUOTED([USE_ECDSA], [1], [Define this to enable ECDSA support.])
      use_ecdsa="yes"
      ;;
esac

AC_ARG_ENABLE(dsa, AS_HELP_STRING([--disable-dsa],[Disable DSA support]))
use_dsa="no"
case "$enable_dsa" in
    yes)
      # detect if DSA is supported, and turn it off if not.
      if test $USE_NSS = "no" -a $USE_NETTLE = "no"; then
      AC_CHECK_FUNC(DSA_SIG_new, [
      AC_CHECK_TYPE(DSA_SIG*, [
      AC_DEFINE_UNQUOTED([USE_DSA], [1], [Define this to enable DSA support.])
      ], [if test "x$enable_dsa" = "xyes"; then AC_MSG_ERROR([OpenSSL does not support DSA and you used --enable-dsa.])
               fi ], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_OPENSSL_RAND_H
#include <openssl/rand.h>
#endif

#ifdef HAVE_OPENSSL_CONF_H
#include <openssl/conf.h>
#endif

#ifdef HAVE_OPENSSL_ENGINE_H
#include <openssl/engine.h>
#endif
      ])
      ], [if test "x$enable_dsa" = "xyes"; then AC_MSG_ERROR([OpenSSL does not support DSA and you used --enable-dsa.])
               fi ])
      else
      AC_DEFINE_UNQUOTED([USE_DSA], [1], [Define this to enable DSA support.])
      fi
      ;;
    *)
      # disable dsa by default, RFC 8624 section 3.1, validators MUST NOT
      # support DSA for DNSSEC Validation.
      ;;
esac

AC_ARG_WITH(deprecate-rsa-1024, AS_HELP_STRING([--with-deprecate-rsa-1024],[Deprecate RSA 1024 bit length, makes that an unsupported key, for use when OpenSSL FIPS refuses 1024 bit verification]))
if test "$with_deprecate_rsa_1024" = "yes"; then
	AC_DEFINE([DEPRECATE_RSA_1024], [1], [Deprecate RSA 1024 bit length, makes that an unsupported key])
fi

AC_ARG_ENABLE(ed25519, AS_HELP_STRING([--disable-ed25519],[Disable ED25519 support]))
use_ed25519="no"
case "$enable_ed25519" in
    no)
      ;;
    *)
      if test $USE_NSS = "no" -a $USE_NETTLE = "no"; then
	      AC_CHECK_DECLS([NID_ED25519], [
      		use_ed25519="yes"
	      ], [ if test "x$enable_ed25519" = "xyes"; then AC_MSG_ERROR([OpenSSL does not support ED25519 and you used --enable-ed25519.])
	      	fi ], [AC_INCLUDES_DEFAULT
#include <openssl/evp.h>
	      ])
      fi
      if test $USE_NETTLE = "yes"; then
		AC_CHECK_HEADERS([nettle/eddsa.h], use_ed25519="yes",, [AC_INCLUDES_DEFAULT])
      fi
      if test $use_ed25519 = "yes"; then
      		AC_DEFINE_UNQUOTED([USE_ED25519], [1], [Define this to enable ED25519 support.])
      fi
      ;;
esac

AC_ARG_ENABLE(ed448, AS_HELP_STRING([--disable-ed448],[Disable ED448 support]))
use_ed448="no"
case "$enable_ed448" in
    no)
      ;;
    *)
      if test $USE_NSS = "no" -a $USE_NETTLE = "no"; then
	      AC_CHECK_DECLS([NID_ED448], [
      		use_ed448="yes"
	      ], [ if test "x$enable_ed448" = "xyes"; then AC_MSG_ERROR([OpenSSL does not support ED448 and you used --enable-ed448.])
	      	fi ], [AC_INCLUDES_DEFAULT
#include <openssl/evp.h>
	      ])
      fi
      if test $use_ed448 = "yes"; then
      		AC_DEFINE_UNQUOTED([USE_ED448], [1], [Define this to enable ED448 support.])
      fi
      ;;
esac

AC_ARG_ENABLE(event-api, AS_HELP_STRING([--enable-event-api],[Enable (experimental) pluggable event base libunbound API installed to unbound-event.h]))
case "$enable_event_api" in
    yes)
      AC_SUBST(UNBOUND_EVENT_INSTALL, [unbound-event-install])
      AC_SUBST(UNBOUND_EVENT_UNINSTALL, [unbound-event-uninstall])
      ;;
    *)
      ;;
esac

AC_ARG_ENABLE(tfo-client, AS_HELP_STRING([--enable-tfo-client],[Enable TCP Fast Open for client mode]))
case "$enable_tfo_client" in
	yes)
		case "$host_os" in
			linux*) AC_CHECK_DECL([MSG_FASTOPEN], [AC_MSG_WARN([Check the platform specific TFO kernel parameters are correctly configured to support client mode TFO])],
				[AC_MSG_ERROR([TCP Fast Open is not available for client mode: please rerun without --enable-tfo-client])],
				[AC_INCLUDES_DEFAULT
#include <netinet/tcp.h>
])
				AC_DEFINE_UNQUOTED([USE_MSG_FASTOPEN], [1], [Define this to enable client TCP Fast Open.])
				;;
			darwin*) AC_CHECK_DECL([CONNECT_RESUME_ON_READ_WRITE], [AC_MSG_WARN([Check the platform specific TFO kernel parameters are correctly configured to support client mode TFO])],
				[AC_MSG_ERROR([TCP Fast Open is not available for client mode: please rerun without --enable-tfo-client])],
				[AC_INCLUDES_DEFAULT
#include <sys/socket.h>
])
				AC_DEFINE_UNQUOTED([USE_OSX_MSG_FASTOPEN], [1], [Define this to enable client TCP Fast Open.])
				;;
		esac
		;;
	no|*)
		;;
esac

AC_ARG_ENABLE(tfo-server, AS_HELP_STRING([--enable-tfo-server],[Enable TCP Fast Open for server mode]))
case "$enable_tfo_server" in
	yes)
	      AC_CHECK_DECL([TCP_FASTOPEN], [AC_MSG_WARN([Check the platform specific TFO kernel parameters are correctly configured to support server mode TFO])], [AC_MSG_ERROR([TCP Fast Open is not available for server mode: please rerun without --enable-tfo-server])], [AC_INCLUDES_DEFAULT
#include <netinet/tcp.h>
	      ])
		AC_DEFINE_UNQUOTED([USE_TCP_FASTOPEN], [1], [Define this to enable server TCP Fast Open.])
		;;
	no|*)
		;;
esac

# check for libevent
AC_ARG_WITH(libevent, AS_HELP_STRING([--with-libevent=pathname],[use libevent (will check /usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr  or you can specify an explicit path). Slower, but allows use of large outgoing port ranges.]),
    [ ],[ with_libevent="no" ])
if test "x_$with_libevent" != x_no; then
        AC_DEFINE([USE_LIBEVENT], [1], [Define if you enable libevent])
        AC_MSG_CHECKING(for libevent)
        if test "x_$with_libevent" = x_ -o "x_$with_libevent" = x_yes; then
            with_libevent="/usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr"
        fi
        for dir in $with_libevent; do
            thedir="$dir"
            if test -f "$dir/include/event.h" -o -f "$dir/include/event2/event.h"; then
                found_libevent="yes"
				dnl assume /usr is in default path.
				if test "$thedir" != "/usr"; then
				    CPPFLAGS="$CPPFLAGS -I$thedir/include"
				fi
				break;
		    fi
        done
        if test x_$found_libevent != x_yes; then
		if test -f "$dir/event.h" -a \( -f "$dir/libevent.la" -o -f "$dir/libev.la" \) ; then
			# libevent source directory
			AC_MSG_RESULT(found in $thedir)
			CPPFLAGS="$CPPFLAGS -I$thedir -I$thedir/include"
			BAK_LDFLAGS_SET="1"
			BAK_LDFLAGS="$LDFLAGS"
			# remove evdns from linking
			mkdir build >/dev/null 2>&1
			mkdir build/libevent >/dev/null 2>&1
			mkdir build/libevent/.libs >/dev/null 2>&1
			ev_files_o=`ls $thedir/*.o | grep -v evdns\.o | grep -v bufferevent_openssl\.o`
			ev_files_lo=`ls $thedir/*.lo | grep -v evdns\.lo | grep -v bufferevent_openssl\.lo`
			ev_files_libso=`ls $thedir/.libs/*.o | grep -v evdns\.o | grep -v bufferevent_openssl\.o`
			cp $ev_files_o build/libevent
			cp $ev_files_lo build/libevent
			cp $ev_files_libso build/libevent/.libs
			LATE_LDFLAGS="build/libevent/*.lo -lm"
			LDFLAGS="build/libevent/*.o $LDFLAGS -lm"
		else
            AC_MSG_ERROR([Cannot find the libevent library in $with_libevent
You can restart ./configure --with-libevent=no to use a builtin alternative.
Please note that this alternative is not as capable as libevent when using
large outgoing port ranges.  ])
		fi
        else
            AC_MSG_RESULT(found in $thedir)
	    dnl if event2 exists and no event lib in dir itself, use subdir
	    if test ! -f $thedir/lib/libevent.a -a ! -f $thedir/lib/libevent.so -a -d "$thedir/lib/event2"; then
		    LDFLAGS="$LDFLAGS -L$thedir/lib/event2"
		    ACX_RUNTIME_PATH_ADD([$thedir/lib/event2])
	    else
		    dnl assume /usr is in default path, do not add "".
		    if test "$thedir" != "/usr" -a "$thedir" != ""; then
			LDFLAGS="$LDFLAGS -L$thedir/lib"
			ACX_RUNTIME_PATH_ADD([$thedir/lib])
		    fi
	    fi
        fi
	# check for library used by libevent after 1.3c
	AC_SEARCH_LIBS([clock_gettime], [rt])

	# is the event.h header libev or libevent?
	AC_CHECK_HEADERS([event.h],,, [AC_INCLUDES_DEFAULT])
	AC_CHECK_DECL(EV_VERSION_MAJOR, [
		AC_SEARCH_LIBS(event_set, [ev])
	],[
		AC_SEARCH_LIBS(event_set, [event])
	],[AC_INCLUDES_DEFAULT
#include <event.h>
	])
	AC_CHECK_FUNCS([event_base_free]) # only in libevent 1.2 and later
	AC_CHECK_FUNCS([event_base_once]) # only in libevent 1.4.1 and later
	AC_CHECK_FUNCS([event_base_new]) # only in libevent 1.4.1 and later
	AC_CHECK_FUNCS([event_base_get_method]) # only in libevent 1.4.3 and later
	AC_CHECK_FUNCS([ev_loop]) # only in libev. (tested on 3.51)
	AC_CHECK_FUNCS([ev_default_loop]) # only in libev. (tested on 4.00)
	AC_CHECK_FUNCS([event_assign]) # in libevent, for thread-safety
	AC_CHECK_DECLS([evsignal_assign], [], [], [AC_INCLUDES_DEFAULT
#ifdef HAVE_EVENT_H
#  include <event.h>
#else
#  include "event2/event.h"
#endif
	])
        PC_LIBEVENT_DEPENDENCY="libevent"
        AC_SUBST(PC_LIBEVENT_DEPENDENCY)
	if test -n "$BAK_LDFLAGS_SET"; then
		LDFLAGS="$BAK_LDFLAGS"
	fi
else
	AC_DEFINE(USE_MINI_EVENT, 1, [Define if you want to use internal select based events])
fi

# check for libexpat
AC_ARG_WITH(libexpat, AS_HELP_STRING([--with-libexpat=path],[specify explicit path for libexpat.]),
    [ ],[ withval="/usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr" ])
AC_MSG_CHECKING(for libexpat)
found_libexpat="no"
for dir in $withval ; do
            if test -f "$dir/include/expat.h"; then
		found_libexpat="yes"
		dnl assume /usr is in default path.
		if test "$dir" != "/usr"; then
                    CPPFLAGS="$CPPFLAGS -I$dir/include"
		    LDFLAGS="$LDFLAGS -L$dir/lib"
		fi
            	AC_MSG_RESULT(found in $dir)
                break;
            fi
done
if test x_$found_libexpat != x_yes; then
	AC_MSG_ERROR([Could not find libexpat, expat.h])
fi
AC_CHECK_HEADERS([expat.h],,, [AC_INCLUDES_DEFAULT])
AC_CHECK_DECLS([XML_StopParser], [], [], [AC_INCLUDES_DEFAULT
#include <expat.h>
])

# hiredis (redis C client for cachedb)
AC_ARG_WITH(libhiredis, AS_HELP_STRING([--with-libhiredis=path],[specify explicit path for libhiredis.]),
    [ ],[ withval="no" ])
found_libhiredis="no"
if test x_$withval = x_yes -o x_$withval != x_no; then
   AC_MSG_CHECKING(for libhiredis)
   if test x_$withval = x_ -o x_$withval = x_yes; then
            withval="/usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr"
   fi
   for dir in $withval ; do
            if test -f "$dir/include/hiredis/hiredis.h"; then
		found_libhiredis="yes"
		dnl assume /usr is in default path.
		if test "$dir" != "/usr"; then
                    CPPFLAGS="$CPPFLAGS -I$dir/include"
		    LDFLAGS="$LDFLAGS -L$dir/lib"
		fi
		AC_MSG_RESULT(found in $dir)
		AC_DEFINE([USE_REDIS], [1], [Define this to use hiredis client.])
		LIBS="$LIBS -lhiredis"
                break;
            fi
    done
    if test x_$found_libhiredis != x_yes; then
	AC_MSG_ERROR([Could not find libhiredis, hiredis.h])
    fi
    AC_CHECK_HEADERS([hiredis/hiredis.h],,, [AC_INCLUDES_DEFAULT])
    AC_CHECK_DECLS([redisConnect], [], [], [AC_INCLUDES_DEFAULT
    #include <hiredis/hiredis.h>
    ])
fi

# nghttp2
AC_ARG_WITH(libnghttp2, AS_HELP_STRING([--with-libnghttp2=path],[specify explicit path for libnghttp2.]),
    [ ],[ withval="no" ])
found_libnghttp2="no"
if test x_$withval = x_yes -o x_$withval != x_no; then
   AC_MSG_CHECKING(for libnghttp2)
   if test x_$withval = x_ -o x_$withval = x_yes; then
            withval="/usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr"
   fi
   for dir in $withval ; do
            if test -f "$dir/include/nghttp2/nghttp2.h"; then
		found_libnghttp2="yes"
		dnl assume /usr is in default path.
		if test "$dir" != "/usr"; then
                    CPPFLAGS="$CPPFLAGS -I$dir/include"
		    LDFLAGS="$LDFLAGS -L$dir/lib"
		fi
		AC_MSG_RESULT(found in $dir)
		AC_DEFINE([HAVE_NGHTTP2], [1], [Define this to use nghttp2 client.])
		LIBS="$LIBS -lnghttp2"
                break;
            fi
    done
    if test x_$found_libnghttp2 != x_yes; then
	AC_MSG_ERROR([Could not find libnghttp2, nghttp2.h])
    fi
    AC_CHECK_HEADERS([nghttp2/nghttp2.h],,, [AC_INCLUDES_DEFAULT])
    AC_CHECK_DECLS([nghttp2_session_server_new], [], [], [AC_INCLUDES_DEFAULT
    #include <nghttp2/nghttp2.h>
    ])
fi

# ngtcp2
AC_ARG_WITH(libngtcp2, AS_HELP_STRING([--with-libngtcp2=path],[specify explicit path for libngtcp2, for QUIC.]),
    [ ],[ withval="no" ])
found_libngtcp2="no"
if test x_$withval = x_yes -o x_$withval != x_no; then
   AC_MSG_CHECKING(for libngtcp2)
   if test x_$withval = x_ -o x_$withval = x_yes; then
            withval="/usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr"
   fi
   for dir in $withval ; do
            if test -f "$dir/include/ngtcp2/ngtcp2.h"; then
		found_libngtcp2="yes"
		dnl assume /usr is in default path.
		if test "$dir" != "/usr"; then
                    CPPFLAGS="$CPPFLAGS -I$dir/include"
		    LDFLAGS="$LDFLAGS -L$dir/lib"
		fi
		AC_MSG_RESULT(found in $dir)
		AC_DEFINE([HAVE_NGTCP2], [1], [Define this to use ngtcp2.])
		LIBS="$LIBS -lngtcp2"
                break;
            fi
    done
    if test x_$found_libngtcp2 != x_yes; then
	AC_MSG_ERROR([Could not find libngtcp2, ngtcp2.h])
    fi
    AC_CHECK_HEADERS([ngtcp2/ngtcp2.h ngtcp2/ngtcp2_crypto_openssl.h ngtcp2/ngtcp2_crypto_quictls.h],,, [AC_INCLUDES_DEFAULT])
    AC_CHECK_DECLS([ngtcp2_conn_server_new], [], [], [AC_INCLUDES_DEFAULT
    #include <ngtcp2/ngtcp2.h>
    ])
    AC_CHECK_DECLS([ngtcp2_crypto_encrypt_cb], [], [], [AC_INCLUDES_DEFAULT
    #include <ngtcp2/ngtcp2_crypto.h>
    ])
    AC_CHECK_LIB([ngtcp2_crypto_openssl], [ngtcp2_crypto_encrypt_cb], [ LIBS="$LIBS -lngtcp2_crypto_openssl" ])
    AC_CHECK_LIB([ngtcp2_crypto_quictls], [ngtcp2_crypto_encrypt_cb], [ LIBS="$LIBS -lngtcp2_crypto_quictls" ])
    AC_CHECK_FUNCS([ngtcp2_crypto_encrypt_cb ngtcp2_ccerr_default ngtcp2_conn_in_closing_period ngtcp2_conn_in_draining_period ngtcp2_conn_get_max_local_streams_uni ngtcp2_crypto_quictls_from_ossl_encryption_level ngtcp2_crypto_quictls_configure_server_context ngtcp2_crypto_quictls_configure_client_context ngtcp2_conn_get_num_scid ngtcp2_conn_tls_early_data_rejected ngtcp2_conn_encode_0rtt_transport_params])
    AC_CHECK_FUNCS([SSL_is_quic], [], [AC_MSG_ERROR([No QUIC support detected in OpenSSL. Need OpenSSL version with QUIC support to enable DNS over QUIC with libngtcp2.])])
    AC_CHECK_TYPES([struct ngtcp2_version_cid, ngtcp2_encryption_level],,,[AC_INCLUDES_DEFAULT
    #include <ngtcp2/ngtcp2.h>
    ])
    AC_CHECK_MEMBERS([struct ngtcp2_pkt_hd.tokenlen, struct ngtcp2_settings.tokenlen, struct ngtcp2_settings.max_tx_udp_payload_size, struct ngtcp2_transport_params.original_dcid_present],,,[AC_INCLUDES_DEFAULT
    #include <ngtcp2/ngtcp2.h>
    ])

    AC_MSG_CHECKING([whether ngtcp2_conn_shutdown_stream has 4 arguments])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([AC_INCLUDES_DEFAULT
#include <ngtcp2/ngtcp2.h>
    ],[
	(void)ngtcp2_conn_shutdown_stream(NULL, 0, 0, 0);
    ])],[
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_NGTCP2_CONN_SHUTDOWN_STREAM4, 1, [Define if ngtcp2_conn_shutdown_stream has 4 arguments.])
    ],[
	AC_MSG_RESULT(no)
    ])

fi

# set static linking for uninstalled libraries if requested
AC_SUBST(staticexe)
staticexe=""
AC_ARG_ENABLE(static-exe, AS_HELP_STRING([--enable-static-exe],[ enable to compile executables statically against (event) uninstalled libs, for debug purposes ]),
	, )
if test x_$enable_static_exe = x_yes; then
	staticexe="-static"
	if test "$on_mingw" = yes; then
		staticexe="-all-static"
		# for static compile, include gdi32 and zlib here.
		if echo "$LIBS" | grep 'lgdi32' >/dev/null; then
			:
		else
			LIBS="$LIBS -lgdi32"
		fi
		AC_CHECK_LIB([z], [compress], [ LIBS="$LIBS -lz" ])
		if echo "$LIBS" | grep -e "libssp.a" -e "lssp" >/dev/null; then
			:
		else
			LIBS="$LIBS -l:libssp.a"
		fi
	fi
fi

# set full static linking if requested
AC_ARG_ENABLE(fully-static, AS_HELP_STRING([--enable-fully-static],[ enable to compile fully static ]),
	, )
if test x_$enable_fully_static = x_yes; then
	staticexe="-all-static"
	if test "$on_mingw" = yes; then
		# for static compile, include gdi32 and zlib here.
		if echo $LIBS | grep 'lgdi32' >/dev/null; then
			:
		else
			LIBS="$LIBS -lgdi32"
		fi
		AC_CHECK_LIB([z], [compress], [ LIBS="$LIBS -lz" ])
		if echo "$LIBS" | grep -e "libssp.a" -e "lssp" >/dev/null; then
			:
		else
			LIBS="$LIBS -l:libssp.a"
		fi
	fi
fi

# set lock checking if requested
AC_ARG_ENABLE(lock_checks, AS_HELP_STRING([--enable-lock-checks],[ enable to check lock and unlock calls, for debug purposes ]),
	, )
if test x_$enable_lock_checks = x_yes; then
	AC_DEFINE(ENABLE_LOCK_CHECKS, 1, [Define if you want to use debug lock checking (slow).])
	CHECKLOCK_OBJ="checklocks.lo"
	AC_SUBST(CHECKLOCK_OBJ)
fi

ACX_CHECK_GETADDRINFO_WITH_INCLUDES
if test "$USE_WINSOCK" = 1; then
	AC_DEFINE(UB_ON_WINDOWS, 1, [Use win32 resources and API])
	AC_CHECK_HEADERS([iphlpapi.h],,, [AC_INCLUDES_DEFAULT
#include <windows.h>
	])
	AC_CHECK_TOOL(WINDRES, windres)
	if echo "$LIBS" | grep crypt32 >/dev/null; then
		LIBS="$LIBS -liphlpapi"
	else
		LIBS="$LIBS -liphlpapi -lcrypt32"
	fi
	WINAPPS="unbound-service-install.exe unbound-service-remove.exe anchor-update.exe"
	AC_SUBST(WINAPPS)
	WIN_DAEMON_SRC="winrc/win_svc.c winrc/w_inst.c"
	AC_SUBST(WIN_DAEMON_SRC)
	WIN_DAEMON_OBJ="win_svc.lo w_inst.lo"
	AC_SUBST(WIN_DAEMON_OBJ)
	WIN_DAEMON_OBJ_LINK="rsrc_unbound.o"
	AC_SUBST(WIN_DAEMON_OBJ_LINK)
	WIN_HOST_OBJ_LINK="rsrc_unbound_host.o"
	AC_SUBST(WIN_HOST_OBJ_LINK)
	WIN_UBANCHOR_OBJ_LINK="rsrc_unbound_anchor.o log.lo locks.lo"
	AC_SUBST(WIN_UBANCHOR_OBJ_LINK)
	WIN_CONTROL_OBJ_LINK="rsrc_unbound_control.o"
	AC_SUBST(WIN_CONTROL_OBJ_LINK)
	WIN_CHECKCONF_OBJ_LINK="rsrc_unbound_checkconf.o"
	AC_SUBST(WIN_CHECKCONF_OBJ_LINK)
	AC_DEFINE(__USE_MINGW_ANSI_STDIO, 1, [defined to use gcc ansi snprintf and sscanf that understands %lld when compiled for windows.])
fi
if test $ac_cv_func_getaddrinfo = no; then
	AC_LIBOBJ([fake-rfc2553])
fi
# check after getaddrinfo for its libraries
ACX_FUNC_IOCTLSOCKET

# see if daemon(3) exists, and if it is deprecated.
AC_CHECK_FUNCS([daemon])
if test $ac_cv_func_daemon = yes; then
	ACX_FUNC_DEPRECATED([daemon], [(void)daemon(0, 0);], [
#include <stdlib.h>
#include <unistd.h>
])
fi

AC_CHECK_MEMBERS([struct sockaddr_un.sun_len],,,[
AC_INCLUDES_DEFAULT
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
])
AC_CHECK_MEMBERS([struct in_pktinfo.ipi_spec_dst],,,[
AC_INCLUDES_DEFAULT
#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif
])

AC_MSG_CHECKING([for htobe64])
AC_LINK_IFELSE([AC_LANG_PROGRAM([
#include <stdio.h>
#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif
#ifdef HAVE_SYS_ENDIAN_H
#  include <sys/endian.h>
#endif
], [unsigned long long x = htobe64(0); printf("%u", (unsigned)x);])],
  AC_MSG_RESULT(yes)
  AC_DEFINE(HAVE_HTOBE64, 1, [If we have htobe64]),
  AC_MSG_RESULT(no))

AC_MSG_CHECKING([for be64toh])
AC_LINK_IFELSE([AC_LANG_PROGRAM([
#include <stdio.h>
#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif
#ifdef HAVE_SYS_ENDIAN_H
#  include <sys/endian.h>
#endif
], [unsigned long long x = be64toh(0); printf("%u", (unsigned)x);])],
  AC_MSG_RESULT(yes)
  AC_DEFINE(HAVE_BE64TOH, 1, [If we have be64toh]),
  AC_MSG_RESULT(no))

AC_SEARCH_LIBS([setusercontext], [util])
AC_CHECK_FUNCS([tzset sigprocmask fcntl getpwnam endpwent getrlimit setrlimit setsid chroot kill chown sleep usleep random srandom recvmsg sendmsg writev socketpair glob initgroups strftime localtime_r setusercontext _beginthreadex endservent endprotoent fsync shmget accept4 getifaddrs if_nametoindex poll gettid])
AC_CHECK_FUNCS([setresuid],,[AC_CHECK_FUNCS([setreuid])])
AC_CHECK_FUNCS([setresgid],,[AC_CHECK_FUNCS([setregid])])

# check if setreuid en setregid fail, on MacOSX10.4(darwin8).
if echo $host_os | grep darwin8 > /dev/null; then
	AC_DEFINE(DARWIN_BROKEN_SETREUID, 1, [Define this if on macOSX10.4-darwin8 and setreuid and setregid do not work])
fi

AC_MSG_CHECKING([for atomic_store])
AC_LINK_IFELSE([AC_LANG_PROGRAM(AC_INCLUDES_DEFAULT [[
#ifdef HAVE_STDATOMIC_H
#include <stdatomic.h>
#endif
]], [[
	int newvar = 5, var = 0;
	atomic_store((_Atomic int*)&var, newvar);
	newvar = 0;
	/* condition to use the variables. */
	if(var == newvar) return 1;
]])], [
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_LINK_ATOMIC_STORE, 1, [If we have atomic_store])
], [
	AC_MSG_RESULT([no])
])

AC_CHECK_DECLS([inet_pton,inet_ntop], [], [], [
AC_INCLUDES_DEFAULT
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif
])
AC_REPLACE_FUNCS(inet_aton)
AC_REPLACE_FUNCS(inet_pton)
AC_REPLACE_FUNCS(inet_ntop)
AC_REPLACE_FUNCS(snprintf)
# test if snprintf return the proper length
if test "x$ac_cv_func_snprintf" = xyes; then
    if test c${cross_compiling} = cno; then
	AC_MSG_CHECKING([for correct snprintf return value])
	AC_RUN_IFELSE([AC_LANG_SOURCE(AC_INCLUDES_DEFAULT
[[
int main(void) { return !(snprintf(NULL, 0, "test") == 4); }
]])], [AC_MSG_RESULT(yes)], [
		AC_MSG_RESULT(no)
		AC_DEFINE([SNPRINTF_RET_BROKEN], [], [define if (v)snprintf does not return length needed, (but length used)])
		AC_LIBOBJ(snprintf)
	  ], [AC_MSG_RESULT(maybe)])
    fi
fi
AC_REPLACE_FUNCS(strlcat)
AC_REPLACE_FUNCS(strlcpy)
AC_REPLACE_FUNCS(memmove)
AC_REPLACE_FUNCS(gmtime_r)
AC_REPLACE_FUNCS(isblank)
AC_REPLACE_FUNCS(explicit_bzero)
dnl without CTIME, ARC4-functions and without reallocarray.
LIBOBJ_WITHOUT_CTIMEARC4="$LIBOBJS"
AC_SUBST(LIBOBJ_WITHOUT_CTIMEARC4)
AC_MSG_CHECKING([for reallocarray])
AC_LINK_IFELSE([AC_LANG_SOURCE(AC_INCLUDES_DEFAULT
[[
#ifndef _OPENBSD_SOURCE
#define _OPENBSD_SOURCE 1
#endif
#include <stdlib.h>
int main(void) {
	void* p = reallocarray(NULL, 10, 100);
	free(p);
	return 0;
}
]])], [AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_REALLOCARRAY, 1, [If we have reallocarray(3)])
], [
	AC_MSG_RESULT(no)
	AC_LIBOBJ(reallocarray)
])
AC_CHECK_DECLS([reallocarray])
if test "$USE_NSS" = "no"; then
	AC_REPLACE_FUNCS(arc4random)
	AC_REPLACE_FUNCS(arc4random_uniform)
	if test "$ac_cv_func_arc4random" = "no"; then
		AC_LIBOBJ(arc4_lock)
		AC_CHECK_FUNCS([getentropy],,[
		    if test "$USE_WINSOCK" = 1; then
			AC_LIBOBJ(getentropy_win)
		    else
			case "$host" in
			Darwin|*darwin*)
				AC_LIBOBJ(getentropy_osx)
			;;
			*solaris*|*sunos*|SunOS)
				AC_LIBOBJ(getentropy_solaris)
				AC_CHECK_HEADERS([sys/sha2.h],, [
					AC_CHECK_FUNCS([SHA512_Update],,[
						AC_LIBOBJ(sha512)
					])
				], [AC_INCLUDES_DEFAULT])
				if test "$ac_cv_header_sys_sha2_h" = "yes"; then
					# this lib needed for sha2 on solaris
					LIBS="$LIBS -lmd"
				fi
				AC_SEARCH_LIBS([clock_gettime], [rt])
			;;
			*freebsd*|*FreeBSD)
				AC_LIBOBJ(getentropy_freebsd)
			;;
			*linux*|Linux|*)
				AC_LIBOBJ(getentropy_linux)
				AC_CHECK_FUNCS([SHA512_Update],,[
					AC_DEFINE([COMPAT_SHA512], [1], [Do sha512 definitions in config.h])
					AC_LIBOBJ(sha512)
				])
				AC_CHECK_HEADERS([sys/sysctl.h],,, [AC_INCLUDES_DEFAULT])
				AC_CHECK_FUNCS([getauxval])
				AC_SEARCH_LIBS([clock_gettime], [rt])
			;;
			esac
		    fi
		])
	fi
fi
LIBOBJ_WITHOUT_CTIME="$LIBOBJS"
AC_SUBST(LIBOBJ_WITHOUT_CTIME)
AC_REPLACE_FUNCS(ctime_r)
AC_REPLACE_FUNCS(strsep)

AC_ARG_ENABLE(allsymbols, AS_HELP_STRING([--enable-allsymbols],[export all symbols from libunbound and link binaries to it, smaller install size but libunbound export table is polluted by internal symbols]))
case "$enable_allsymbols" in
	yes)
	COMMON_OBJ_ALL_SYMBOLS=""
	UBSYMS=""
	EXTRALINK="libunbound.la"
	AC_DEFINE(EXPORT_ALL_SYMBOLS, 1, [Define this if you enabled-allsymbols from libunbound to link binaries to it for smaller install size, but the libunbound export table is polluted by internal symbols])
	;;
	no|*)
	COMMON_OBJ_ALL_SYMBOLS='$(COMMON_OBJ)'
	UBSYMS='-export-symbols $(srcdir)/libunbound/ubsyms.def'
	EXTRALINK=""
	;;
esac
AC_SUBST(COMMON_OBJ_ALL_SYMBOLS)
AC_SUBST(EXTRALINK)
AC_SUBST(UBSYMS)
if test x_$enable_lock_checks = x_yes; then
	UBSYMS="-export-symbols clubsyms.def"
	cp ${srcdir}/libunbound/ubsyms.def clubsyms.def
	echo lock_protect >> clubsyms.def
	echo lock_protect_place >> clubsyms.def
	echo lock_unprotect >> clubsyms.def
	echo lock_get_mem >> clubsyms.def
	echo checklock_start >> clubsyms.def
	echo checklock_stop >> clubsyms.def
	echo checklock_set_output_name >> clubsyms.def
	echo checklock_lock >> clubsyms.def
	echo checklock_unlock >> clubsyms.def
	echo checklock_init >> clubsyms.def
	echo checklock_thrcreate >> clubsyms.def
	echo checklock_thrjoin >> clubsyms.def
fi

# check for dnstap if requested
dt_DNSTAP([$UNBOUND_RUN_DIR/dnstap.sock],
    [
        AC_DEFINE([USE_DNSTAP], [1], [Define to 1 to enable dnstap support])
        AC_SUBST([ENABLE_DNSTAP], [1])

        AC_SUBST([opt_dnstap_socket_path])
        ACX_ESCAPE_BACKSLASH($opt_dnstap_socket_path, hdr_dnstap_socket_path)
        AC_DEFINE_UNQUOTED(DNSTAP_SOCKET_PATH,
            ["$hdr_dnstap_socket_path"], [default dnstap socket path])
	AC_SUBST(DNSTAP_SOCKET_PATH,["$hdr_dnstap_socket_path"])
	AC_SUBST(DNSTAP_SOCKET_TESTBIN,['unbound-dnstap-socket$(EXEEXT)'])
        AC_SUBST([DNSTAP_SRC], ["dnstap/dnstap.c dnstap/dnstap.pb-c.c dnstap/dnstap_fstrm.c dnstap/dtstream.c"])
        AC_SUBST([DNSTAP_OBJ], ["dnstap.lo dnstap.pb-c.lo dnstap_fstrm.lo dtstream.lo"])
    ],
    [
        AC_SUBST([ENABLE_DNSTAP], [0])
    ]
)

# check for dnscrypt if requested
dnsc_DNSCRYPT([
        AC_DEFINE([USE_DNSCRYPT], [1], [Define to 1 to enable dnscrypt support])
        AC_SUBST([ENABLE_DNSCRYPT], [1])

        AC_SUBST([DNSCRYPT_SRC], ["dnscrypt/dnscrypt.c"])
        AC_SUBST([DNSCRYPT_OBJ], ["dnscrypt.lo"])
    ],
    [
        AC_SUBST([ENABLE_DNSCRYPT], [0])
    ]
)

# check for cachedb if requested
AC_ARG_ENABLE(cachedb, AS_HELP_STRING([--enable-cachedb],[enable cachedb module that can use external cache storage]))
# turn on cachedb when hiredis support is enabled.
if test "$found_libhiredis" = "yes"; then enable_cachedb="yes"; fi
case "$enable_cachedb" in
    yes)
    	AC_DEFINE([USE_CACHEDB], [1], [Define to 1 to use cachedb support])
	AC_SUBST([CACHEDB_SRC], ["cachedb/cachedb.c cachedb/redis.c"])
	AC_SUBST([CACHEDB_OBJ], ["cachedb.lo redis.lo"])
    	;;
    no|*)
    	# nothing
    	;;
esac

# check for ipsecmod if requested
AC_ARG_ENABLE(ipsecmod, AS_HELP_STRING([--enable-ipsecmod],[Enable ipsecmod module that facilitates opportunistic IPsec]))
case "$enable_ipsecmod" in
	yes)
		AC_DEFINE([USE_IPSECMOD], [1], [Define to 1 to use ipsecmod support.])
		IPSECMOD_OBJ="ipsecmod.lo ipsecmod-whitelist.lo"
		AC_SUBST(IPSECMOD_OBJ)
		IPSECMOD_HEADER='$(srcdir)/ipsecmod/ipsecmod.h $(srcdir)/ipsecmod/ipsecmod-whitelist.h'
		AC_SUBST(IPSECMOD_HEADER)
		;;
	no|*)
		# nothing
		;;
esac

# check for ipset if requested
AC_ARG_ENABLE(ipset, AS_HELP_STRING([--enable-ipset],[enable ipset module]))
case "$enable_ipset" in
    yes)
		AC_DEFINE([USE_IPSET], [1], [Define to 1 to use ipset support])
		IPSET_SRC="ipset/ipset.c"
		AC_SUBST(IPSET_SRC)
		IPSET_OBJ="ipset.lo"
		AC_SUBST(IPSET_OBJ)

		# BSD's pf
		AC_CHECK_HEADERS([net/pfvar.h], [], [
		    # mnl
		    AC_ARG_WITH(libmnl, AS_HELP_STRING([--with-libmnl=path],[specify explicit path for libmnl.]),
			[ ],[ withval="yes" ])
		    found_libmnl="no"
		    AC_MSG_CHECKING(for libmnl)
		    if test x_$withval = x_ -o x_$withval = x_yes; then
			withval="/usr/local /opt/local /usr/lib /usr/pkg /usr/sfw /usr"
		    fi
		    for dir in $withval ; do
			if test -f "$dir/include/libmnl/libmnl.h" -o -f "$dir/include/libmnl/libmnl/libmnl.h"; then
				found_libmnl="yes"
				dnl assume /usr is in default path.
				extralibmnl=""
				if test -f "$dir/include/libmnl/libmnl/libmnl.h"; then
					extralibmnl="/libmnl"
				fi
				if test "$dir" != "/usr" -o -n "$extralibmnl"; then
					CPPFLAGS="$CPPFLAGS -I$dir/include$extralibmnl"
				fi
				if test "$dir" != "/usr"; then
					LDFLAGS="$LDFLAGS -L$dir/lib"
				fi
				AC_MSG_RESULT(found in $dir)
				LIBS="$LIBS -lmnl"
				break;
			fi
		    done
		    if test x_$found_libmnl != x_yes; then
			  AC_MSG_ERROR([Could not find libmnl, libmnl.h])
		    fi
		], [
		  #include <netinet/in.h>
		  #include <net/if.h>
		])
		;;
    no|*)
    	# nothing
		;;
esac
AC_ARG_ENABLE(explicit-port-randomisation, AS_HELP_STRING([--disable-explicit-port-randomisation],[disable explicit source port randomisation and rely on the kernel to provide random source ports]))
case "$enable_explicit_port_randomisation" in
	no)
		AC_DEFINE([DISABLE_EXPLICIT_PORT_RANDOMISATION], [1], [Define this to enable kernel based UDP source port randomization.])
		;;
	yes|*)
		;;
esac

if echo "$host" | $GREP -i -e linux >/dev/null; then
	AC_ARG_ENABLE(linux-ip-local-port-range, AS_HELP_STRING([--enable-linux-ip-local-port-range], [Define this to enable use of /proc/sys/net/ipv4/ip_local_port_range as a default outgoing port range. This is only for the libunbound on Linux and does not affect unbound resolving daemon itself. This may severely limit the number of available outgoing ports and thus decrease randomness. Define this only when the target system restricts (e.g. some of SELinux enabled distributions) the use of non-ephemeral ports.]))
	case "$enable_linux_ip_local_port_range" in
		yes)
			AC_DEFINE([USE_LINUX_IP_LOCAL_PORT_RANGE], [1], [Define this to enable use of /proc/sys/net/ipv4/ip_local_port_range as a default outgoing port range. This is only for the libunbound on Linux and does not affect unbound resolving daemon itself. This may severely limit the number of available outgoing ports and thus decrease randomness. Define this only when the target system restricts (e.g. some of SELinux enabled distributions) the use of non-ephemeral ports.])
			;;
		no|*)
			;;
	esac
fi


AC_MSG_CHECKING([if ${MAKE:-make} supports $< with implicit rule in scope])
# on openBSD, the implicit rule make $< work.
# on Solaris, it does not work ($? is changed sources, $^ lists dependencies).
# gmake works.
cat >conftest.make <<EOF
all:	conftest.lo

conftest.lo foo.lo bla.lo:
	if test -f "\$<"; then touch \$@; fi

.SUFFIXES: .lo
.c.lo:
	if test -f "\$<"; then touch \$@; fi

conftest.lo:        conftest.dir/conftest.c
EOF
mkdir conftest.dir
touch conftest.dir/conftest.c
rm -f conftest.lo conftest.c
${MAKE:-make} -f conftest.make >/dev/null
rm -f conftest.make conftest.c conftest.dir/conftest.c
rm -rf conftest.dir
if test ! -f conftest.lo; then
	AC_MSG_RESULT(no)
	SOURCEDETERMINE='echo "$^" | awk "-F " "{print \$$1;}" > .source'
	SOURCEFILE='`cat .source`'
else
	AC_MSG_RESULT(yes)
	SOURCEDETERMINE=':'
	SOURCEFILE='$<'
fi
rm -f conftest.lo
AC_SUBST(SOURCEDETERMINE)
AC_SUBST(SOURCEFILE)

# see if we want to build the library or everything
ALLTARGET="alltargets"
INSTALLTARGET="install-all"
AC_ARG_WITH(libunbound-only, AS_HELP_STRING([--with-libunbound-only],[do not build daemon and tool programs]),
	[
	if test "$withval" = "yes"; then
		ALLTARGET="lib"
		INSTALLTARGET="install-lib"
	fi
])
if test $ALLTARGET = "alltargets"; then
	if test $USE_NSS = "yes"; then
		AC_MSG_ERROR([--with-nss can only be used in combination with --with-libunbound-only.])
	fi
	if test $USE_NETTLE = "yes"; then
		AC_MSG_ERROR([--with-nettle can only be used in combination with --with-libunbound-only.])
	fi
fi

AC_SUBST(ALLTARGET)
AC_SUBST(INSTALLTARGET)

ACX_STRIP_EXT_FLAGS
if test -n "$LATE_LDFLAGS"; then
  LDFLAGS="$LATE_LDFLAGS $LDFLAGS"
fi
# remove start spaces
LDFLAGS=`echo "$LDFLAGS"|sed -e 's/^ *//'`
LIBS=`echo "$LIBS"|sed -e 's/^ *//'`

AC_DEFINE_UNQUOTED([MAXSYSLOGMSGLEN], [10240], [Define to the maximum message length to pass to syslog.])

AH_BOTTOM(
dnl this must be first AH_CONFIG, to define the flags before any includes.
AHX_CONFIG_EXT_FLAGS

dnl includes
[
#ifndef _OPENBSD_SOURCE
#define _OPENBSD_SOURCE 1
#endif

#ifndef UNBOUND_DEBUG
# ifndef NDEBUG
#  define NDEBUG
# endif
#endif

/** Use small-ldns codebase */
#define USE_SLDNS 1
#ifdef HAVE_SSL
#  define LDNS_BUILD_CONFIG_HAVE_SSL 1
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <errno.h>

#if HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#if !defined(USE_WINSOCK) || !defined(HAVE_SNPRINTF) || defined(SNPRINTF_RET_BROKEN) || defined(__USE_MINGW_ANSI_STDIO)
#define ARG_LL "%ll"
#else
#define ARG_LL "%I64"
#endif

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif
]

AHX_CONFIG_FORMAT_ATTRIBUTE
AHX_CONFIG_UNUSED_ATTRIBUTE
AHX_CONFIG_FSEEKO
AHX_CONFIG_MAXHOSTNAMELEN
#if !defined(HAVE_SNPRINTF) || defined(SNPRINTF_RET_BROKEN)
#define snprintf snprintf_unbound
#define vsnprintf vsnprintf_unbound
#include <stdarg.h>
int snprintf (char *str, size_t count, const char *fmt, ...);
int vsnprintf (char *str, size_t count, const char *fmt, va_list arg);
#endif /* HAVE_SNPRINTF or SNPRINTF_RET_BROKEN */
AHX_CONFIG_INET_PTON(unbound)
AHX_CONFIG_INET_NTOP(unbound)
AHX_CONFIG_INET_ATON(unbound)
AHX_CONFIG_MEMMOVE(unbound)
AHX_CONFIG_STRLCAT(unbound)
AHX_CONFIG_STRLCPY(unbound)
AHX_CONFIG_GMTIME_R(unbound)
AHX_CONFIG_REALLOCARRAY(unbound)
AHX_CONFIG_W32_SLEEP
AHX_CONFIG_W32_USLEEP
AHX_CONFIG_W32_RANDOM
AHX_CONFIG_W32_SRANDOM
AHX_CONFIG_W32_FD_SET_T
AHX_CONFIG_IPV6_MIN_MTU
AHX_MEMCMP_BROKEN(unbound)

[
#ifndef HAVE_CTIME_R
#define ctime_r unbound_ctime_r
char *ctime_r(const time_t *timep, char *buf);
#endif

#ifndef HAVE_STRSEP
#define strsep unbound_strsep
char *strsep(char **stringp, const char *delim);
#endif

#ifndef HAVE_ISBLANK
#define isblank unbound_isblank
int isblank(int c);
#endif

#ifndef HAVE_EXPLICIT_BZERO
#define explicit_bzero unbound_explicit_bzero
void explicit_bzero(void* buf, size_t len);
#endif

#if defined(HAVE_INET_NTOP) && !HAVE_DECL_INET_NTOP
const char *inet_ntop(int af, const void *src, char *dst, size_t size);
#endif

#if defined(HAVE_INET_PTON) && !HAVE_DECL_INET_PTON
int inet_pton(int af, const char* src, void* dst);
#endif

#if !defined(HAVE_STRPTIME) || !defined(STRPTIME_WORKS)
#define strptime unbound_strptime
struct tm;
char *strptime(const char *s, const char *format, struct tm *tm);
#endif

#if !HAVE_DECL_REALLOCARRAY
void *reallocarray(void *ptr, size_t nmemb, size_t size);
#endif

#ifdef HAVE_LIBBSD
#include <bsd/string.h>
#include <bsd/stdlib.h>
#endif

#ifdef HAVE_LIBRESSL
#  if !HAVE_DECL_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#  endif
#  if !HAVE_DECL_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz);
#  endif
#  if !HAVE_DECL_ARC4RANDOM && defined(HAVE_ARC4RANDOM)
uint32_t arc4random(void);
#  endif
#  if !HAVE_DECL_ARC4RANDOM_UNIFORM && defined(HAVE_ARC4RANDOM_UNIFORM)
uint32_t arc4random_uniform(uint32_t upper_bound);
#  endif
#endif /* HAVE_LIBRESSL */
#ifndef HAVE_ARC4RANDOM
int getentropy(void* buf, size_t len);
uint32_t arc4random(void);
void arc4random_buf(void* buf, size_t n);
void _ARC4_LOCK(void);
void _ARC4_UNLOCK(void);
void _ARC4_LOCK_DESTROY(void);
#endif
#ifndef HAVE_ARC4RANDOM_UNIFORM
uint32_t arc4random_uniform(uint32_t upper_bound);
#endif
#ifdef COMPAT_SHA512
#ifndef SHA512_DIGEST_LENGTH
#define SHA512_BLOCK_LENGTH		128
#define SHA512_DIGEST_LENGTH		64
#define SHA512_DIGEST_STRING_LENGTH	(SHA512_DIGEST_LENGTH * 2 + 1)
typedef struct _SHA512_CTX {
	uint64_t	state[8];
	uint64_t	bitcount[2];
	uint8_t	buffer[SHA512_BLOCK_LENGTH];
} SHA512_CTX;
#endif /* SHA512_DIGEST_LENGTH */
void SHA512_Init(SHA512_CTX*);
void SHA512_Update(SHA512_CTX*, void*, size_t);
void SHA512_Final(uint8_t[SHA512_DIGEST_LENGTH], SHA512_CTX*);
unsigned char *SHA512(void* data, unsigned int data_len, unsigned char *digest);
#endif /* COMPAT_SHA512 */



#if defined(HAVE_EVENT_H) && !defined(HAVE_EVENT_BASE_ONCE) && !(defined(HAVE_EV_LOOP) || defined(HAVE_EV_DEFAULT_LOOP)) && (defined(HAVE_PTHREAD) || defined(HAVE_SOLARIS_THREADS))
   /* using version of libevent that is not threadsafe. */
#  define LIBEVENT_SIGNAL_PROBLEM 1
#endif

#ifndef CHECKED_INET6
#  define CHECKED_INET6
#  ifdef AF_INET6
#    define INET6
#  else
#    define AF_INET6        28
#  endif
#endif /* CHECKED_INET6 */

#ifndef HAVE_GETADDRINFO
struct sockaddr_storage;
#include "compat/fake-rfc2553.h"
#endif

#ifdef UNBOUND_ALLOC_STATS
#  define malloc(s) unbound_stat_malloc_log(s, __FILE__, __LINE__, __func__)
#  define calloc(n,s) unbound_stat_calloc_log(n, s, __FILE__, __LINE__, __func__)
#  define free(p) unbound_stat_free_log(p, __FILE__, __LINE__, __func__)
#  define realloc(p,s) unbound_stat_realloc_log(p, s, __FILE__, __LINE__, __func__)
#  define strdup(s) unbound_stat_strdup_log(s, __FILE__, __LINE__, __func__)
#ifdef HAVE_REALLOCARRAY
#  define reallocarray(p,n,s) unbound_stat_reallocarray_log(p, n, s, __FILE__, __LINE__, __func__)
#endif
void *unbound_stat_malloc(size_t size);
void *unbound_stat_calloc(size_t nmemb, size_t size);
void unbound_stat_free(void *ptr);
void *unbound_stat_realloc(void *ptr, size_t size);
void *unbound_stat_malloc_log(size_t size, const char* file, int line,
	const char* func);
void *unbound_stat_calloc_log(size_t nmemb, size_t size, const char* file,
	int line, const char* func);
void unbound_stat_free_log(void *ptr, const char* file, int line,
	const char* func);
void *unbound_stat_realloc_log(void *ptr, size_t size, const char* file,
	int line, const char* func);
void *unbound_stat_reallocarray_log(void *ptr, size_t nmemb, size_t size,
	const char* file, int line, const char* func);
char *unbound_stat_strdup_log(const char *s, const char* file, int line,
	const char* func);
#elif defined(UNBOUND_ALLOC_LITE)
#  include "util/alloc.h"
#endif /* UNBOUND_ALLOC_LITE and UNBOUND_ALLOC_STATS */

/** default port for DNS traffic. */
#define UNBOUND_DNS_PORT 53
/** default port for DNS over TLS traffic. */
#define UNBOUND_DNS_OVER_TLS_PORT 853
/** default port for DNS over HTTPS traffic. */
#define UNBOUND_DNS_OVER_HTTPS_PORT 443
/** default port for DNS over QUIC traffic. */
#define UNBOUND_DNS_OVER_QUIC_PORT 853
/** default port for unbound control traffic, registered port with IANA,
    ub-dns-control  8953/tcp    unbound dns nameserver control */
#define UNBOUND_CONTROL_PORT 8953
/** the version of unbound-control that this software implements */
#define UNBOUND_CONTROL_VERSION 1

])

dnl if we build from source tree, the man pages need @date@ and @version@
dnl if this is a distro tarball, that was already done by makedist.sh
AC_SUBST(version, [VERSION_MAJOR.VERSION_MINOR.VERSION_MICRO])
AX_BUILD_DATE_EPOCH(date, [[%b %e, %Y]])
AC_SUBST(date)

AC_CONFIG_FILES([Makefile doc/example.conf doc/libunbound.3 doc/unbound.8 doc/unbound-anchor.8 doc/unbound-checkconf.8 doc/unbound.conf.5 doc/unbound-control.8 doc/unbound-host.1 smallapp/unbound-control-setup.sh dnstap/dnstap_config.h dnscrypt/dnscrypt_config.h])
AC_CONFIG_HEADERS([config.h])
AC_OUTPUT
