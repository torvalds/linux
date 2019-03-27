/* $FreeBSD$ */

/* subversion/svn_private_config.h.tmp.  Generated from svn_private_config.h.in by configure.  */
/* subversion/svn_private_config.h.in.  Generated from configure.ac by autoheader.  */

/* The fs type to use by default */
#define DEFAULT_FS_TYPE "fsfs"

/* The http library to use by default */
#define DEFAULT_HTTP_LIBRARY "serf"

/* Define to 1 if Ev2 implementations should be used. */
/* #undef ENABLE_EV2_IMPL */

/* Define to 1 if translation of program messages to the user's native
   language is requested. */
/* #undef ENABLE_NLS */

/* Define to 1 if you have the `bind_textdomain_codeset' function. */
/* #undef HAVE_BIND_TEXTDOMAIN_CODESET */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you don't have `vprintf' but do have `_doprnt.' */
/* #undef HAVE_DOPRNT */

/* Define to 1 if you have the `getpid' function. */
#define HAVE_GETPID 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `iconv' library (-liconv). */
/* #undef HAVE_LIBICONV */

/* Define to 1 if you have the `socket' library (-lsocket). */
/* #undef HAVE_LIBSOCKET */

/* Define to 1 if you have the <magic.h> header file. */
/* #undef HAVE_MAGIC_H */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `rb_errinfo' function. */
/* #undef HAVE_RB_ERRINFO */

/* Define to 1 if you have the `readlink' function. */
#define HAVE_READLINK 1

/* Define to 1 if you have the <serf.h> header file. */
#define HAVE_SERF_H 1

/* Define to use internal LZ4 code */
#define SVN_INTERNAL_LZ4 1
 
/* Define to use internal UTF8PROC code */
#define SVN_INTERNAL_UTF8PROC 1

/* Define to 1 if you have the <stdbool.h> header file. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `symlink' function. */
#define HAVE_SYMLINK 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/utsname.h> header file. */
#define HAVE_SYS_UTSNAME_H 1

/* Define to 1 if you have the `tcgetattr' function. */
#define HAVE_TCGETATTR 1

/* Define to 1 if you have the `tcsetattr' function. */
#define HAVE_TCSETATTR 1

/* Defined if we have a usable termios library. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if you have the `uname' function. */
#define HAVE_UNAME 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `vprintf' function. */
#define HAVE_VPRINTF 1

/* Define to 1 if you have the <zlib.h> header file. */
/* #undef HAVE_ZLIB_H */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "http://subversion.apache.org/"

/* Define to the full name of this package. */
#define PACKAGE_NAME "subversion"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "subversion 1.10.0"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "subversion"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.10.0"

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Defined to build against httpd 2.4 with broken auth */
/* #undef SVN_ALLOW_BROKEN_HTTPD_AUTH */

/* Define to the Python/C API format character suitable for apr_int64_t */
#define SVN_APR_INT64_T_PYCFMT "l"

/* Defined to be the path to the installed binaries */
#define SVN_BINDIR "/usr/bin"

/* Defined to the config.guess name of the build system */
#define SVN_BUILD_HOST "bikeshed-rgb-freebsd"

/* Defined to the config.guess name of the build target */
#define SVN_BUILD_TARGET "bikeshed-rgb-freebsd"

/* The path of a default editor for the client. */
/* #undef SVN_CLIENT_EDITOR */

/* Defined if the full version matching rules are disabled */
/* #undef SVN_DISABLE_FULL_VERSION_MATCH */

/* Defined if plaintext password/passphrase storage is disabled */
/* #undef SVN_DISABLE_PLAINTEXT_PASSWORD_STORAGE */

/* Shared library file name suffix format */
#undef SVN_DSO_SUFFIX_FMT

/* The desired major version for the Berkeley DB */
#define SVN_FS_WANT_DB_MAJOR 4

/* The desired minor version for the Berkeley DB */
#define SVN_FS_WANT_DB_MINOR 0

/* The desired patch version for the Berkeley DB */
#define SVN_FS_WANT_DB_PATCH 14

/* Define if compiler provides atomic builtins */
/* #undef SVN_HAS_ATOMIC_BUILTINS */

/* Is GNOME Keyring support enabled? */
/* #undef SVN_HAVE_GNOME_KEYRING */

/* Is GPG Agent support enabled? */
#define SVN_HAVE_GPG_AGENT 1

/* Is Mac OS KeyChain support enabled? */
/* #undef SVN_HAVE_KEYCHAIN_SERVICES */

/* Defined if KF5 available */
#undef SVN_HAVE_KF5

/* Defined if KWallet support is enabled */
/* #undef SVN_HAVE_KWALLET */

/* Defined if libmagic support is enabled */
#define SVN_HAVE_LIBMAGIC 1

/* Is libsecret support enabled? */
#undef SVN_HAVE_LIBSECRET

/* Is Mach-O low-level _dyld API available? */
/* #undef SVN_HAVE_MACHO_ITERATE */

/* Is Mac OS property list API available? */
/* #undef SVN_HAVE_MACOS_PLIST */

/* Defined if apr_memcache (standalone or in apr-util) is present */
#define SVN_HAVE_MEMCACHE 1

/* Defined if Expat 1.0 or 1.1 was found */
/* #undef SVN_HAVE_OLD_EXPAT */

/* Defined if Cyrus SASL v2 is present on the system */
/* #undef SVN_HAVE_SASL */

/* Defined if support for Serf is enabled */
#define SVN_HAVE_SERF 1

/* Defined if libsvn_fs should link against libsvn_fs_base */
/* #undef SVN_LIBSVN_FS_LINKS_FS_BASE */

/* Defined if libsvn_fs should link against libsvn_fs_fs */
#define SVN_LIBSVN_FS_LINKS_FS_FS 1

/* Defined if libsvn_fs should link against libsvn_fs_x */
#define SVN_LIBSVN_FS_LINKS_FS_X 1

/* Defined if libsvn_ra should link against libsvn_ra_local */
#define SVN_LIBSVN_RA_LINKS_RA_LOCAL 1

/* Defined if libsvn_ra should link against libsvn_ra_serf */
#define SVN_LIBSVN_RA_LINKS_RA_SERF 1

/* Defined if libsvn_ra should link against libsvn_ra_svn */
#define SVN_LIBSVN_RA_LINKS_RA_SVN 1

/* Defined to be the path to the installed locale dirs */
#define SVN_LOCALE_DIR "NONE/share/locale"

/* Defined to be the null device for the system */
#define SVN_NULL_DEVICE_NAME "/dev/null"

/* Defined to be the path separator used on your local filesystem */
#define SVN_PATH_LOCAL_SEPARATOR '/'

/* Subversion library major verson */
#define SVN_SOVERSION 0

/* Defined if svn should use the amalgamated version of sqlite */
/* #undef SVN_SQLITE_INLINE */

/* Defined if svn should try to load DSOs */
/* #undef SVN_USE_DSO */

/* Defined to build with patched httpd 2.4 and working auth */
/* #undef SVN_USE_FORCE_AUTHN */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

#ifdef SVN_WANT_BDB
#define APU_WANT_DB

#endif



/* Indicate to translators that string X should be translated.  Do not look
   up the translation at run time; just expand to X.  This macro is suitable
   for use where a constant string is required at compile time. */
#define N_(x) x
/* Indicate to translators that we have decided the string X should not be
   translated.  Expand to X. */
#define U_(x) x
#ifdef ENABLE_NLS
#include <locale.h>
#include <libintl.h>
/* Indicate to translators that string X should be translated.  At run time,
   look up and return the translation of X. */
#define _(x) dgettext(PACKAGE_NAME, x)
/* Indicate to translators that strings X1 and X2 are singular and plural
   forms of the same message, and should be translated.  At run time, return
   an appropriate translation depending on the number N. */
#define Q_(x1, x2, n) dngettext(PACKAGE_NAME, x1, x2, n)
#else
#define _(x) (x)
#define Q_(x1, x2, n) (((n) == 1) ? x1 : x2)
#define gettext(x) (x)
#define dgettext(domain, x) (x)
#endif

/* compiler hints */
#if defined(__GNUC__) && (__GNUC__ >= 3)
# define SVN__PREDICT_FALSE(x) (__builtin_expect(x, 0))
# define SVN__PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
# define SVN__PREDICT_FALSE(x) (x)
# define SVN__PREDICT_TRUE(x) (x)
#endif

#if defined(SVN_DEBUG)
# define SVN__FORCE_INLINE
# define SVN__PREVENT_INLINE
#elif defined(__GNUC__)
# define SVN__FORCE_INLINE APR_INLINE __attribute__ ((always_inline))
# define SVN__PREVENT_INLINE __attribute__ ((noinline))
#else
# define SVN__FORCE_INLINE APR_INLINE
# define SVN__PREVENT_INLINE
#endif

/* Macro used to specify that a variable is intentionally left unused.
   Supresses compiler warnings about the variable being unused.  */
#define SVN_UNUSED(v) ( (void)(v) )

