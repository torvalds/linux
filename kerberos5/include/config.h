/* include/config.h.  Generated from config.h.in by configure.  */
/* include/config.h.in.  Generated from configure.ac by autoheader.  */

/* $FreeBSD$ */

#ifndef RCSID
#define RCSID(msg) \
static /**/const char *const rcsid[] = { (const char *)rcsid, "@(#)" msg }
#endif

/* Maximum values on all known systems */
#define MaxHostNameLen (64+4)
#define MaxPathLen (1024+4)



#ifdef BUILD_KRB5_LIB
#ifndef KRB5_LIB
#ifdef _WIN32_
#define KRB5_LIB_FUNCTION __declspec(dllexport)
#define KRB5_LIB_CALL __stdcall
#define KRB5_LIB_VARIABLE __declspec(dllexport)
#else
#define KRB5_LIB_FUNCTION
#define KRB5_LIB_CALL
#define KRB5_LIB_VARIABLE
#endif
#endif
#endif


#ifdef BUILD_ROKEN_LIB
#ifndef ROKEN_LIB
#ifdef _WIN32_
#define ROKEN_LIB_FUNCTION __declspec(dllexport)
#define ROKEN_LIB_CALL __stdcall
#define ROKEN_LIB_VARIABLE __declspec(dllexport)
#else
#define ROKEN_LIB_FUNCTION
#define ROKEN_LIB_CALL
#define ROKEN_LIB_VARIABLE
#endif
#endif
#endif


#ifdef BUILD_GSSAPI_LIB
#ifndef GSSAPI_LIB
#ifdef _WIN32_
#define GSSAPI_LIB_FUNCTION __declspec(dllexport)
#define GSSAPI_LIB_CALL __stdcall
#define GSSAPI_LIB_VARIABLE __declspec(dllexport)
#else
#define GSSAPI_LIB_FUNCTION
#define GSSAPI_LIB_CALL
#define GSSAPI_LIB_VARIABLE
#endif
#endif
#endif


/* Define if you want authentication support in telnet. */
#define AUTHENTICATION 1

/* path to bin */
#define BINDIR "/usr/bin"

/* Define if realloc(NULL) doesn't work. */
/* #undef BROKEN_REALLOC */

/* Define if you want support for DCE/DFS PAG's. */
/* #undef DCE */

/* Define if you want to use DES encryption in telnet. */
#define DES_ENCRYPTION 1

/* Define this to enable diagnostics in telnet. */
#define DIAGNOSTICS 1

/* Define to enable DIGEST. */
#define DIGEST 1

/* Define if want to use the weak AFS string to key functions. */
#define ENABLE_AFS_STRING_TO_KEY 1

/* Define if you want have a thread safe libraries */
#define ENABLE_PTHREAD_SUPPORT 1

/* Define if you want encryption support in telnet. */
#define ENCRYPTION 1

/* define if sys/param.h defines the endiness */
#define ENDIANESS_IN_SYS_PARAM_H 1

/* Define this if you want support for broken ENV_{VAR,VAL} telnets. */
/* #undef ENV_HACK */

/* define if prototype of gethostbyaddr is compatible with struct hostent
   *gethostbyaddr(const void *, size_t, int) */
/* #undef GETHOSTBYADDR_PROTO_COMPATIBLE */

/* define if prototype of gethostbyname is compatible with struct hostent
   *gethostbyname(const char *) */
#define GETHOSTBYNAME_PROTO_COMPATIBLE 1

/* define if prototype of getservbyname is compatible with struct servent
   *getservbyname(const char *, const char *) */
#define GETSERVBYNAME_PROTO_COMPATIBLE 1

/* define if prototype of getsockname is compatible with int getsockname(int,
   struct sockaddr*, socklen_t*) */
#define GETSOCKNAME_PROTO_COMPATIBLE 1

/* Define if you have the `altzone' variable. */
/* #undef HAVE_ALTZONE */

/* Define to 1 if you have the `arc4random' function. */
#define HAVE_ARC4RANDOM 1

/* Define to 1 if you have the <arpa/ftp.h> header file. */
#define HAVE_ARPA_FTP_H 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#define HAVE_ARPA_NAMESER_H 1

/* Define to 1 if you have the <arpa/telnet.h> header file. */
#define HAVE_ARPA_TELNET_H 1

/* Define to 1 if you have the <asl.h> header file. */
/* #undef HAVE_ASL_H */

/* Define to 1 if you have the `asnprintf' function. */
/* #undef HAVE_ASNPRINTF */

/* Define to 1 if you have the `asprintf' function. */
#define HAVE_ASPRINTF 1

/* Define to 1 if you have the `atexit' function. */
#define HAVE_ATEXIT 1

/* Define to 1 if you have the `backtrace' function. */
/* #undef HAVE_BACKTRACE */

/* Define to 1 if you have the <bind/bitypes.h> header file. */
/* #undef HAVE_BIND_BITYPES_H */

/* Define to 1 if you have the <bsdsetjmp.h> header file. */
/* #undef HAVE_BSDSETJMP_H */

/* Define to 1 if you have the `bswap16' function. */
/* #undef HAVE_BSWAP16 */

/* Define to 1 if you have the `bswap32' function. */
/* #undef HAVE_BSWAP32 */

/* Define to 1 if you have the <capability.h> header file. */
/* #undef HAVE_CAPABILITY_H */

/* whether capng is available for privilege reduction */
/* #undef HAVE_CAPNG */

/* Define to 1 if you have the `cap_set_proc' function. */
/* #undef HAVE_CAP_SET_PROC */

/* Define to 1 if you have the `cgetent' function. */
#define HAVE_CGETENT 1

/* Define if you have the function `chown'. */
#define HAVE_CHOWN 1

/* Define if you have the function `closefrom'. */
#define HAVE_CLOSEFROM 1

/* Define to 1 if you have the <CommonCrypto/CommonCryptor.h> header file. */
/* #undef HAVE_COMMONCRYPTO_COMMONCRYPTOR_H */

/* Define to 1 if you have the <CommonCrypto/CommonDigest.h> header file. */
/* #undef HAVE_COMMONCRYPTO_COMMONDIGEST_H */

/* Define to 1 if you have the <config.h> header file. */
/* #undef HAVE_CONFIG_H */

/* Define if you have the function `copyhostent'. */
/* #undef HAVE_COPYHOSTENT */

/* Define to 1 if you have the `crypt' function. */
#define HAVE_CRYPT 1

/* Define to 1 if you have the <crypt.h> header file. */
/* #undef HAVE_CRYPT_H */

/* Define to 1 if you have the <curses.h> header file. */
#define HAVE_CURSES_H 1

/* Define if you have the function `daemon'. */
#define HAVE_DAEMON 1

/* define if you have a berkeley db1/2 library */
#define HAVE_DB1 1

/* define if you have a berkeley db3/4/5 library */
/* #undef HAVE_DB3 */

/* Define to 1 if you have the <db3/db.h> header file. */
/* #undef HAVE_DB3_DB_H */

/* Define to 1 if you have the <db4/db.h> header file. */
/* #undef HAVE_DB4_DB_H */

/* Define to 1 if you have the <db5/db.h> header file. */
/* #undef HAVE_DB5_DB_H */

/* Define if you have user supplied header location */
/* #undef HAVE_DBHEADER */

/* Define to 1 if you have the `dbm_firstkey' function. */
#define HAVE_DBM_FIRSTKEY 1

/* Define to 1 if you have the <dbm.h> header file. */
/* #undef HAVE_DBM_H */

/* Define to 1 if you have the `dbopen' function. */
#define HAVE_DBOPEN 1

/* Define to 1 if you have the `db_create' function. */
/* #undef HAVE_DB_CREATE */

/* Define to 1 if you have the <db.h> header file. */
#define HAVE_DB_H 1

/* define if you have ndbm compat in db */
/* #undef HAVE_DB_NDBM */

/* Define to 1 if you have the declaration of `altzone', and to 0 if you
   don't. */
/* #undef HAVE_DECL_ALTZONE */

/* Define to 1 if you have the declaration of `environ', and to 0 if you
   don't. */
#define HAVE_DECL_ENVIRON 0

/* Define to 1 if you have the declaration of `h_errlist', and to 0 if you
   don't. */
#define HAVE_DECL_H_ERRLIST 0

/* Define to 1 if you have the declaration of `h_errno', and to 0 if you
   don't. */
#define HAVE_DECL_H_ERRNO 1

/* Define to 1 if you have the declaration of `h_nerr', and to 0 if you don't.
   */
/* #undef HAVE_DECL_H_NERR */

/* Define to 1 if you have the declaration of `optarg', and to 0 if you don't.
   */
#define HAVE_DECL_OPTARG 1

/* Define to 1 if you have the declaration of `opterr', and to 0 if you don't.
   */
#define HAVE_DECL_OPTERR 1

/* Define to 1 if you have the declaration of `optind', and to 0 if you don't.
   */
#define HAVE_DECL_OPTIND 1

/* Define to 1 if you have the declaration of `optopt', and to 0 if you don't.
   */
#define HAVE_DECL_OPTOPT 1

/* Define to 1 if you have the declaration of `timezone', and to 0 if you
   don't. */
#define HAVE_DECL_TIMEZONE 1

/* Define to 1 if you have the declaration of `_res', and to 0 if you don't.
   */
#define HAVE_DECL__RES 1

/* Define to 1 if you have the declaration of `__progname', and to 0 if you
   don't. */
#define HAVE_DECL___PROGNAME 0

/* Define to 1 if you have the <dirent.h> header file. */
#define HAVE_DIRENT_H 1

/* have a dirfd function/macro */
#define HAVE_DIRFD 1

/* Define if DIR has field dd_fd. */
#define HAVE_DIR_DD_FD 1

/* Define to 1 if you have the `dispatch_async_f' function. */
/* #undef HAVE_DISPATCH_ASYNC_F */

/* Define to 1 if you have the <dispatch/dispatch.h> header file. */
/* #undef HAVE_DISPATCH_DISPATCH_H */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if you have the `dlopen' function. */
#define HAVE_DLOPEN 1

/* Define to 1 if you have the <dns.h> header file. */
/* #undef HAVE_DNS_H */

/* Define to 1 if you have the `dns_search' function. */
/* #undef HAVE_DNS_SEARCH */

/* Define to 1 if you have the `dn_expand' function. */
#define HAVE_DN_EXPAND 1

/* Define to 1 if you have the `door_create' function. */
/* #undef HAVE_DOOR_CREATE */

/* Define if you have the function `ecalloc'. */
/* #undef HAVE_ECALLOC */

/* Define if you have the function `emalloc'. */
/* #undef HAVE_EMALLOC */

/* Define if you have the function `erealloc'. */
/* #undef HAVE_EREALLOC */

/* Define if you have the function `err'. */
#define HAVE_ERR 1

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define if you have the function `errx'. */
#define HAVE_ERRX 1

/* Define to 1 if you have the <err.h> header file. */
#define HAVE_ERR_H 1

/* Define if you have the function `estrdup'. */
/* #undef HAVE_ESTRDUP */

/* Define to 1 if you have the <execinfo.h> header file. */
/* #undef HAVE_EXECINFO_H */

/* Define if you have the function `fchown'. */
#define HAVE_FCHOWN 1

/* Define to 1 if you have the `fcntl' function. */
#define HAVE_FCNTL 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define if you have the function `flock'. */
#define HAVE_FLOCK 1

/* Define if you have the function `fnmatch'. */
#define HAVE_FNMATCH 1

/* Define to 1 if you have the <fnmatch.h> header file. */
#define HAVE_FNMATCH_H 1

/* Have -framework Security */
/* #undef HAVE_FRAMEWORK_SECURITY */

/* Define to 1 if you have the `freeaddrinfo' function. */
#define HAVE_FREEADDRINFO 1

/* Define if you have the function `freehostent'. */
#define HAVE_FREEHOSTENT 1

/* Define to 1 if you have the `gai_strerror' function. */
#define HAVE_GAI_STRERROR 1

/* Define if os support gcd. */
/* #undef HAVE_GCD */

/* Define to 1 if you have the <gdbm/ndbm.h> header file. */
/* #undef HAVE_GDBM_NDBM_H */

/* Define to 1 if you have the `getaddrinfo' function. */
#define HAVE_GETADDRINFO 1

/* Define to 1 if you have the `getconfattr' function. */
/* #undef HAVE_GETCONFATTR */

/* Define if you have the function `getcwd'. */
#define HAVE_GETCWD 1

/* Define if you have the function `getdtablesize'. */
#define HAVE_GETDTABLESIZE 1

/* Define if you have the function `getegid'. */
#define HAVE_GETEGID 1

/* Define if you have the function `geteuid'. */
#define HAVE_GETEUID 1

/* Define if you have the function `getgid'. */
#define HAVE_GETGID 1

/* Define to 1 if you have the `gethostbyname' function. */
#define HAVE_GETHOSTBYNAME 1

/* Define to 1 if you have the `gethostbyname2' function. */
#define HAVE_GETHOSTBYNAME2 1

/* Define if you have the function `gethostname'. */
#define HAVE_GETHOSTNAME 1

/* Define if you have the function `getifaddrs'. */
#define HAVE_GETIFADDRS 1

/* Define if you have the function `getipnodebyaddr'. */
#define HAVE_GETIPNODEBYADDR 1

/* Define if you have the function `getipnodebyname'. */
#define HAVE_GETIPNODEBYNAME 1

/* Define to 1 if you have the `getlogin' function. */
#define HAVE_GETLOGIN 1

/* Define if you have a working getmsg. */
/* #undef HAVE_GETMSG */

/* Define to 1 if you have the `getnameinfo' function. */
#define HAVE_GETNAMEINFO 1

/* Define if you have the function `getopt'. */
#define HAVE_GETOPT 1

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the `getpeereid' function. */
#define HAVE_GETPEEREID 1

/* Define to 1 if you have the `getpeerucred' function. */
/* #undef HAVE_GETPEERUCRED */

/* Define to 1 if you have the `getprogname' function. */
#define HAVE_GETPROGNAME 1

/* Define to 1 if you have the `getpwnam_r' function. */
#define HAVE_GETPWNAM_R 1

/* Define to 1 if you have the `getrlimit' function. */
#define HAVE_GETRLIMIT 1

/* Define to 1 if you have the `getsockopt' function. */
#define HAVE_GETSOCKOPT 1

/* Define to 1 if you have the `getspnam' function. */
/* #undef HAVE_GETSPNAM */

/* Define if you have the function `gettimeofday'. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the `getudbnam' function. */
/* #undef HAVE_GETUDBNAM */

/* Define if you have the function `getuid'. */
#define HAVE_GETUID 1

/* Define if you have the function `getusershell'. */
#define HAVE_GETUSERSHELL 1

/* define if you have a glob() that groks GLOB_BRACE, GLOB_NOCHECK,
   GLOB_QUOTE, GLOB_TILDE, and GLOB_LIMIT */
#define HAVE_GLOB 1

/* Define to 1 if you have the `grantpt' function. */
#define HAVE_GRANTPT 1

/* Define to 1 if you have the <grp.h> header file. */
#define HAVE_GRP_H 1

/* Define to 1 if you have the `hstrerror' function. */
#define HAVE_HSTRERROR 1

/* Define if you have the `h_errlist' variable. */
#define HAVE_H_ERRLIST 1

/* Define if you have the `h_errno' variable. */
#define HAVE_H_ERRNO 1

/* Define if you have the `h_nerr' variable. */
/* #undef HAVE_H_NERR */

/* Define to 1 if you have the <ifaddrs.h> header file. */
#define HAVE_IFADDRS_H 1

/* Define if you have the in6addr_loopback variable */
#define HAVE_IN6ADDR_LOOPBACK 1

/* define */
#define HAVE_INET_ATON 1

/* define */
#define HAVE_INET_NTOP 1

/* define */
#define HAVE_INET_PTON 1

/* Define if you have the function `initgroups'. */
#define HAVE_INITGROUPS 1

/* Define if you have the function `innetgr'. */
#define HAVE_INNETGR 1

/* Define to 1 if the system has the type `int16_t'. */
#define HAVE_INT16_T 1

/* Define to 1 if the system has the type `int32_t'. */
#define HAVE_INT32_T 1

/* Define to 1 if the system has the type `int64_t'. */
#define HAVE_INT64_T 1

/* Define to 1 if the system has the type `int8_t'. */
#define HAVE_INT8_T 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <io.h> header file. */
/* #undef HAVE_IO_H */

/* Define if you have IPv6. */
#define HAVE_IPV6 1

/* Define if you have the function `iruserok'. */
#define HAVE_IRUSEROK 1

/* Define to 1 if you have the `issetugid' function. */
#define HAVE_ISSETUGID 1

/* Define if you want to use the Kerberos Credentials Manager. */
#define HAVE_KCM 1

/* Define to 1 if you have the <libutil.h> header file. */
#define HAVE_LIBUTIL_H 1

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the `loadquery' function. */
/* #undef HAVE_LOADQUERY */

/* Define to 1 if you have the <locale.h> header file. */
#define HAVE_LOCALE_H 1

/* Define if you have the function `localtime_r'. */
#define HAVE_LOCALTIME_R 1

/* Define to 1 if you have the `logout' function. */
/* #undef HAVE_LOGOUT */

/* Define to 1 if you have the `logwtmp' function. */
/* #undef HAVE_LOGWTMP */

/* Define to 1 if the system has the type `long long'. */
#define HAVE_LONG_LONG 1

/* Define if you have the function `lstat'. */
#define HAVE_LSTAT 1

/* Define to 1 if you have the <maillock.h> header file. */
/* #undef HAVE_MAILLOCK_H */

/* Define if you have the function `memmove'. */
#define HAVE_MEMMOVE 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define if you have the function `mkstemp'. */
#define HAVE_MKSTEMP 1

/* Define to 1 if you have the `mktime' function. */
#define HAVE_MKTIME 1

/* Define to 1 if you have a working `mmap' system call. */
#define HAVE_MMAP 1

/* define if you have a ndbm library */
#define HAVE_NDBM 1

/* Define to 1 if you have the <ndbm.h> header file. */
#define HAVE_NDBM_H 1

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netgroup.h> header file. */
/* #undef HAVE_NETGROUP_H */

/* Define to 1 if you have the <netinet6/in6.h> header file. */
/* #undef HAVE_NETINET6_IN6_H */

/* Define to 1 if you have the <netinet6/in6_var.h> header file. */
/* #undef HAVE_NETINET6_IN6_VAR_H */

/* Define to 1 if you have the <netinet/in6.h> header file. */
/* #undef HAVE_NETINET_IN6_H */

/* Define to 1 if you have the <netinet/in6_machtypes.h> header file. */
/* #undef HAVE_NETINET_IN6_MACHTYPES_H */

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet/in_systm.h> header file. */
#define HAVE_NETINET_IN_SYSTM_H 1

/* Define to 1 if you have the <netinet/ip.h> header file. */
#define HAVE_NETINET_IP_H 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H 1

/* Define to 1 if you have the <net/if.h> header file. */
#define HAVE_NET_IF_H 1

/* Define if NDBM really is DB (creates files *.db) */
#define HAVE_NEW_DB 1

/* Define to 1 if you have the `on_exit' function. */
/* #undef HAVE_ON_EXIT */

/* Define to 1 if you have the `openpty' function. */
#define HAVE_OPENPTY 1

/* define to use openssl's libcrypto */
#define HAVE_OPENSSL 1

/* Define to enable basic OSF C2 support. */
/* #undef HAVE_OSFC2 */

/* Define to 1 if you have the <paths.h> header file. */
#define HAVE_PATHS_H 1

/* Define to 1 if you have the `pidfile' function. */
/* #undef HAVE_PIDFILE */

/* Define to 1 if you have the `poll' function. */
#define HAVE_POLL 1

/* Define to 1 if you have the <poll.h> header file. */
#define HAVE_POLL_H 1

/* Define to 1 if you have the <pthread.h> header file. */
#define HAVE_PTHREAD_H 1

/* Define to 1 if you have the `ptsname' function. */
#define HAVE_PTSNAME 1

/* Define to 1 if you have the <pty.h> header file. */
/* #undef HAVE_PTY_H */

/* Define if you have the function `putenv'. */
#define HAVE_PUTENV 1

/* Define to 1 if you have the <pwd.h> header file. */
#define HAVE_PWD_H 1

/* Define to 1 if you have the `rand' function. */
#define HAVE_RAND 1

/* Define to 1 if you have the `random' function. */
#define HAVE_RANDOM 1

/* Define if you have the function `rcmd'. */
#define HAVE_RCMD 1

/* Define if you have a readline compatible library. */
#define HAVE_READLINE 1

/* Define to 1 if you have the
   <[readline.h])[][]_AH_CHECK_HEADER([readline/readline.h]> header file. */
/* #undef HAVE_READLINE_H */

/* Define to 1 if you have the <readline/readline.h > header file. */
/* #undef HAVE_READLINE_READLINE_H_ */

/* Define if you have the function `readv'. */
#define HAVE_READV 1

/* Define if you have the function `recvmsg'. */
#define HAVE_RECVMSG 1

/* Define to 1 if you have the <resolv.h> header file. */
#define HAVE_RESOLV_H 1

/* Define to 1 if you have the `res_ndestroy' function. */
#define HAVE_RES_NDESTROY 1

/* Define to 1 if you have the `res_nsearch' function. */
#define HAVE_RES_NSEARCH 1

/* Define to 1 if you have the `res_search' function. */
#define HAVE_RES_SEARCH 1

/* Define to 1 if you have the `revoke' function. */
#define HAVE_REVOKE 1

/* Define to 1 if you have the <rpcsvc/ypclnt.h> header file. */
#define HAVE_RPCSVC_YPCLNT_H 1

/* Define to 1 if you have the <sac.h> header file. */
/* #undef HAVE_SAC_H */

/* Define to 1 if the system has the type `sa_family_t'. */
#define HAVE_SA_FAMILY_T 1

/* Define if you want support for cache in sqlite. */
#undef HAVE_SCC

/* Define to 1 if you have the <search.h> header file. */
#define HAVE_SEARCH_H 1

/* Define to 1 if you have the <security/pam_modules.h> header file. */
#define HAVE_SECURITY_PAM_MODULES_H 1

/* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define if you have the function `sendmsg'. */
#define HAVE_SENDMSG 1

/* Define if you have the function `setegid'. */
#define HAVE_SETEGID 1

/* Define if you have the function `setenv'. */
#define HAVE_SETENV 1

/* Define if you have the function `seteuid'. */
#define HAVE_SETEUID 1

/* Define to 1 if you have the `setitimer' function. */
#define HAVE_SETITIMER 1

/* Define to 1 if you have the `setlim' function. */
/* #undef HAVE_SETLIM */

/* Define to 1 if you have the `setlogin' function. */
#define HAVE_SETLOGIN 1

/* Define to 1 if you have the `setpcred' function. */
/* #undef HAVE_SETPCRED */

/* Define to 1 if you have the `setpgid' function. */
#define HAVE_SETPGID 1

/* Define to 1 if you have the `setproctitle' function. */
#define HAVE_SETPROCTITLE 1

/* Define to 1 if you have the `setprogname' function. */
#define HAVE_SETPROGNAME 1

/* Define to 1 if you have the `setregid' function. */
#define HAVE_SETREGID 1

/* Define to 1 if you have the `setresgid' function. */
#define HAVE_SETRESGID 1

/* Define to 1 if you have the `setresuid' function. */
#define HAVE_SETRESUID 1

/* Define to 1 if you have the `setreuid' function. */
#define HAVE_SETREUID 1

/* Define to 1 if you have the `setsid' function. */
#define HAVE_SETSID 1

/* Define to 1 if you have the `setsockopt' function. */
#define HAVE_SETSOCKOPT 1

/* Define to 1 if you have the `setutent' function. */
/* #undef HAVE_SETUTENT */

/* Define to 1 if you have the `sgi_getcapabilitybyname' function. */
/* #undef HAVE_SGI_GETCAPABILITYBYNAME */

/* Define to 1 if you have the <sgtty.h> header file. */
/* #undef HAVE_SGTTY_H */

/* Define to 1 if you have the <shadow.h> header file. */
/* #undef HAVE_SHADOW_H */

/* Define to 1 if you have the <siad.h> header file. */
/* #undef HAVE_SIAD_H */

/* Define to 1 if you have the `sigaction' function. */
#define HAVE_SIGACTION 1

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* define if you have a working snprintf */
#define HAVE_SNPRINTF 1

/* Define to 1 if you have the `socket' function. */
#define HAVE_SOCKET 1

/* Define to 1 if the system has the type `socklen_t'. */
#define HAVE_SOCKLEN_T 1

/* Define if you want support for sqlite in Heimdal. */
#define HAVE_SQLITE3 1

/* Define to 1 if the system has the type `ssize_t'. */
#define HAVE_SSIZE_T 1

/* Define to 1 if you have the <standards.h> header file. */
/* #undef HAVE_STANDARDS_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define if you have the function `strcasecmp'. */
#define HAVE_STRCASECMP 1

/* Define if you have the function `strdup'. */
#define HAVE_STRDUP 1

/* Define if you have the function `strerror'. */
#define HAVE_STRERROR 1

/* Define if you have the function strerror_r. */
#define HAVE_STRERROR_R 1

/* Define if you have the function `strftime'. */
#define HAVE_STRFTIME 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define if you have the function `strlcat'. */
#define HAVE_STRLCAT 1

/* Define if you have the function `strlcpy'. */
#define HAVE_STRLCPY 1

/* Define if you have the function `strlwr'. */
/* #undef HAVE_STRLWR */

/* Define if you have the function `strncasecmp'. */
#define HAVE_STRNCASECMP 1

/* Define if you have the function `strndup'. */
#define HAVE_STRNDUP 1

/* Define if you have the function `strnlen'. */
#define HAVE_STRNLEN 1

/* Define to 1 if you have the <stropts.h> header file. */
/* #undef HAVE_STROPTS_H */

/* Define if you have the function `strptime'. */
#define HAVE_STRPTIME 1

/* Define if you have the function `strsep'. */
#define HAVE_STRSEP 1

/* Define if you have the function `strsep_copy'. */
/* #undef HAVE_STRSEP_COPY */

/* Define to 1 if you have the `strstr' function. */
#define HAVE_STRSTR 1

/* Define to 1 if you have the `strsvis' function. */
/* #undef HAVE_STRSVIS */

/* Define to 1 if you have the `strsvisx' function. */
/* #undef HAVE_STRSVISX */

/* Define if you have the function `strtok_r'. */
#define HAVE_STRTOK_R 1

/* Define to 1 if the system has the type `struct addrinfo'. */
#define HAVE_STRUCT_ADDRINFO 1

/* Define to 1 if the system has the type `struct ifaddrs'. */
#define HAVE_STRUCT_IFADDRS 1

/* Define to 1 if the system has the type `struct iovec'. */
#define HAVE_STRUCT_IOVEC 1

/* Define to 1 if the system has the type `struct msghdr'. */
#define HAVE_STRUCT_MSGHDR 1

/* Define to 1 if the system has the type `struct sockaddr'. */
#define HAVE_STRUCT_SOCKADDR 1

/* Define if struct sockaddr has field sa_len. */
#define HAVE_STRUCT_SOCKADDR_SA_LEN 1

/* Define to 1 if the system has the type `struct sockaddr_storage'. */
#define HAVE_STRUCT_SOCKADDR_STORAGE 1

/* define if you have struct spwd */
/* #undef HAVE_STRUCT_SPWD */

/* Define if struct tm has field tm_gmtoff. */
#define HAVE_STRUCT_TM_TM_GMTOFF 1

/* Define if struct tm has field tm_zone. */
#define HAVE_STRUCT_TM_TM_ZONE 1

/* Define if struct utmpx has field ut_exit. */
/* #undef HAVE_STRUCT_UTMPX_UT_EXIT */

/* Define if struct utmpx has field ut_host. */
#define HAVE_STRUCT_UTMPX_UT_HOST 1

/* Define if struct utmpx has field ut_id. */
#define HAVE_STRUCT_UTMPX_UT_ID 1

/* Define if struct utmpx has field ut_line. */
#define HAVE_STRUCT_UTMPX_UT_LINE 1

/* Define if struct utmpx has field ut_pid. */
#define HAVE_STRUCT_UTMPX_UT_PID 1

/* Define if struct utmpx has field ut_syslen. */
/* #undef HAVE_STRUCT_UTMPX_UT_SYSLEN */

/* Define if struct utmpx has field ut_tv. */
#define HAVE_STRUCT_UTMPX_UT_TV 1

/* Define if struct utmpx has field ut_type. */
#define HAVE_STRUCT_UTMPX_UT_TYPE 1

/* Define if struct utmpx has field ut_user. */
#define HAVE_STRUCT_UTMPX_UT_USER 1

/* Define if struct utmp has field ut_addr. */
/* #undef HAVE_STRUCT_UTMP_UT_ADDR */

/* Define if struct utmp has field ut_host. */
/* #undef HAVE_STRUCT_UTMP_UT_HOST */

/* Define if struct utmp has field ut_id. */
/* #undef HAVE_STRUCT_UTMP_UT_ID */

/* Define if struct utmp has field ut_pid. */
/* #undef HAVE_STRUCT_UTMP_UT_PID */

/* Define if struct utmp has field ut_type. */
/* #undef HAVE_STRUCT_UTMP_UT_TYPE */

/* Define if struct utmp has field ut_user. */
/* #undef HAVE_STRUCT_UTMP_UT_USER */

/* define if struct winsize is declared in sys/termios.h */
#define HAVE_STRUCT_WINSIZE 1

/* Define to 1 if you have the `strunvis' function. */
#define HAVE_STRUNVIS 1

/* Define if you have the function `strupr'. */
/* #undef HAVE_STRUPR */

/* Define to 1 if you have the `strvis' function. */
#define HAVE_STRVIS 1

/* Define to 1 if you have the `strvisx' function. */
#define HAVE_STRVISX 1

/* Define to 1 if you have the `svis' function. */
/* #undef HAVE_SVIS */

/* Define if you have the function `swab'. */
#define HAVE_SWAB 1

/* Define to 1 if you have the `sysconf' function. */
#define HAVE_SYSCONF 1

/* Define to 1 if you have the `sysctl' function. */
#define HAVE_SYSCTL 1

/* Define to 1 if you have the `syslog' function. */
#define HAVE_SYSLOG 1

/* Define to 1 if you have the <syslog.h> header file. */
#define HAVE_SYSLOG_H 1

/* Define to 1 if you have the <sys/bitypes.h> header file. */
/* #undef HAVE_SYS_BITYPES_H */

/* Define to 1 if you have the <sys/bswap.h> header file. */
/* #undef HAVE_SYS_BSWAP_H */

/* Define to 1 if you have the <sys/capability.h> header file. */
#define HAVE_SYS_CAPABILITY_H 1

/* Define to 1 if you have the <sys/category.h> header file. */
/* #undef HAVE_SYS_CATEGORY_H */

/* Define to 1 if you have the <sys/file.h> header file. */
#define HAVE_SYS_FILE_H 1

/* Define to 1 if you have the <sys/filio.h> header file. */
#define HAVE_SYS_FILIO_H 1

/* Define to 1 if you have the <sys/ioccom.h> header file. */
#define HAVE_SYS_IOCCOM_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/proc.h> header file. */
#define HAVE_SYS_PROC_H 1

/* Define to 1 if you have the <sys/ptyio.h> header file. */
/* #undef HAVE_SYS_PTYIO_H */

/* Define to 1 if you have the <sys/ptyvar.h> header file. */
/* #undef HAVE_SYS_PTYVAR_H */

/* Define to 1 if you have the <sys/pty.h> header file. */
/* #undef HAVE_SYS_PTY_H */

/* Define to 1 if you have the <sys/resource.h> header file. */
#define HAVE_SYS_RESOURCE_H 1

/* Define to 1 if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/sockio.h> header file. */
#define HAVE_SYS_SOCKIO_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/stream.h> header file. */
/* #undef HAVE_SYS_STREAM_H */

/* Define to 1 if you have the <sys/stropts.h> header file. */
/* #undef HAVE_SYS_STROPTS_H */

/* Define to 1 if you have the <sys/strtty.h> header file. */
/* #undef HAVE_SYS_STRTTY_H */

/* Define to 1 if you have the <sys/str_tty.h> header file. */
/* #undef HAVE_SYS_STR_TTY_H */

/* Define to 1 if you have the <sys/syscall.h> header file. */
#define HAVE_SYS_SYSCALL_H 1

/* Define to 1 if you have the <sys/sysctl.h> header file. */
#define HAVE_SYS_SYSCTL_H 1

/* Define to 1 if you have the <sys/termio.h> header file. */
/* #undef HAVE_SYS_TERMIO_H */

/* Define to 1 if you have the <sys/timeb.h> header file. */
#define HAVE_SYS_TIMEB_H 1

/* Define to 1 if you have the <sys/times.h> header file. */
#define HAVE_SYS_TIMES_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/tty.h> header file. */
#define HAVE_SYS_TTY_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/ucred.h> header file. */
#define HAVE_SYS_UCRED_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H 1

/* Define to 1 if you have the <sys/un.h> header file. */
#define HAVE_SYS_UN_H 1

/* Define to 1 if you have the <sys/utsname.h> header file. */
#define HAVE_SYS_UTSNAME_H 1

/* Define to 1 if you have the <sys/wait.h> header file. */
#define HAVE_SYS_WAIT_H 1

/* Define to 1 if you have the `tdelete' function. */
#define HAVE_TDELETE 1

/* Define to 1 if you have the <termcap.h> header file. */
#define HAVE_TERMCAP_H 1

/* Define to 1 if you have the <termios.h> header file. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if you have the <termio.h> header file. */
/* #undef HAVE_TERMIO_H */

/* Define to 1 if you have the <term.h> header file. */
#define HAVE_TERM_H 1

/* Define to 1 if you have the `tfind' function. */
#define HAVE_TFIND 1

/* Define to 1 if you have the `tgetent' function. */
#define HAVE_TGETENT 1

/* Define if you have the function `timegm'. */
#define HAVE_TIMEGM 1

/* Define if you have the `timezone' variable. */
#define HAVE_TIMEZONE 1

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the <tmpdir.h> header file. */
/* #undef HAVE_TMPDIR_H */

/* Define if you have the function `tsearch'. */
#define HAVE_TSEARCH 1

/* Define to 1 if you have the `ttyname' function. */
#define HAVE_TTYNAME 1

/* Define to 1 if you have the `ttyslot' function. */
/* #undef HAVE_TTYSLOT */

/* Define to 1 if you have the `twalk' function. */
#define HAVE_TWALK 1

/* Define to 1 if you have the <udb.h> header file. */
/* #undef HAVE_UDB_H */

/* Define to 1 if the system has the type `uint16_t'. */
#define HAVE_UINT16_T 1

/* Define to 1 if the system has the type `uint32_t'. */
#define HAVE_UINT32_T 1

/* Define to 1 if the system has the type `uint64_t'. */
#define HAVE_UINT64_T 1

/* Define to 1 if the system has the type `uint8_t'. */
#define HAVE_UINT8_T 1

/* Define to 1 if the system has the type `uintptr_t'. */
#define HAVE_UINTPTR_T 1

/* Define to 1 if you have the `umask' function. */
#define HAVE_UMASK 1

/* Define to 1 if you have the `uname' function. */
#define HAVE_UNAME 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `unlockpt' function. */
#define HAVE_UNLOCKPT 1

/* Define if you have the function `unsetenv'. */
#define HAVE_UNSETENV 1

/* Define to 1 if you have the `unvis' function. */
#define HAVE_UNVIS 1

/* Define to 1 if you have the <userconf.h> header file. */
/* #undef HAVE_USERCONF_H */

/* Define to 1 if you have the <usersec.h> header file. */
/* #undef HAVE_USERSEC_H */

/* Define to 1 if you have the <util.h> header file. */
/* #undef HAVE_UTIL_H */

/* Define to 1 if you have the <utmpx.h> header file. */
#define HAVE_UTMPX_H 1

/* Define to 1 if you have the <utmp.h> header file. */
/* #undef HAVE_UTMP_H */

/* Define to 1 if the system has the type `u_int16_t'. */
#define HAVE_U_INT16_T 1

/* Define to 1 if the system has the type `u_int32_t'. */
#define HAVE_U_INT32_T 1

/* Define to 1 if the system has the type `u_int64_t'. */
#define HAVE_U_INT64_T 1

/* Define to 1 if the system has the type `u_int8_t'. */
#define HAVE_U_INT8_T 1

/* Define to 1 if you have the `vasnprintf' function. */
/* #undef HAVE_VASNPRINTF */

/* Define to 1 if you have the `vasprintf' function. */
#define HAVE_VASPRINTF 1

/* Define if you have the function `verr'. */
#define HAVE_VERR 1

/* Define if you have the function `verrx'. */
#define HAVE_VERRX 1

/* Define to 1 if you have the `vhangup' function. */
/* #undef HAVE_VHANGUP */

/* Define to 1 if you have the `vis' function. */
#define HAVE_VIS 1

/* Define to 1 if you have the <vis.h> header file. */
#define HAVE_VIS_H 1

/* define if you have a working vsnprintf */
#define HAVE_VSNPRINTF 1

/* Define if you have the function `vsyslog'. */
#define HAVE_VSYSLOG 1

/* Define if you have the function `vwarn'. */
#define HAVE_VWARN 1

/* Define if you have the function `vwarnx'. */
#define HAVE_VWARNX 1

/* Define if you have the function `warn'. */
#define HAVE_WARN 1

/* Define if you have the function `warnx'. */
#define HAVE_WARNX 1

/* Define to 1 if you have the <winsock2.h> header file. */
/* #undef HAVE_WINSOCK2_H */

/* Define if you have the function `writev'. */
#define HAVE_WRITEV 1

/* Define to 1 if you have the <ws2tcpip.h> header file. */
/* #undef HAVE_WS2TCPIP_H */

/* define if struct winsize has ws_xpixel */
#define HAVE_WS_XPIXEL 1

/* define if struct winsize has ws_ypixel */
#define HAVE_WS_YPIXEL 1

/* Define to 1 if you have the `XauFileName' function. */
/* #undef HAVE_XAUFILENAME */

/* Define to 1 if you have the `XauReadAuth' function. */
/* #undef HAVE_XAUREADAUTH */

/* Define to 1 if you have the `XauWriteAuth' function. */
/* #undef HAVE_XAUWRITEAUTH */

/* Define to 1 if you have the `yp_get_default_domain' function. */
#define HAVE_YP_GET_DEFAULT_DOMAIN 1

/* Define to 1 if you have the `_getpty' function. */
/* #undef HAVE__GETPTY */

/* Define if you have the `_res' variable. */
#define HAVE__RES 1

/* Define to 1 if you have the `_scrsize' function. */
/* #undef HAVE__SCRSIZE */

/* define if your compiler has __attribute__ */
#define HAVE___ATTRIBUTE__ 1

/* Define if you have the `__progname' variable. */
#define HAVE___PROGNAME 1

/* have __sync_add_and_fetch */
#if defined(__FreeBSD__) && (defined(__arm__) || defined(__mips__))
#undef HAVE___SYNC_ADD_AND_FETCH	/* Not supported on FreeBSD/arm */
#else
#define HAVE___SYNC_ADD_AND_FETCH 1
#endif

/* Define if you want support for weak crypto */
#define HEIM_WEAK_CRYPTO 1

/* Define if you have the hesiod package. */
/* #undef HESIOD */

/* Enable Kerberos 5 support in applications. */
#define KRB5 1

/* Define to enable kx509. */
#define KX509 1

/* path to lib */
#define LIBDIR "/usr/lib"

/* Define if you have the libedit package. */
/* #undef LIBEDIT */

/* path to libexec */
#define LIBEXECDIR "/usr/libexec"

/* Define if you have the libintl package. */
/* #undef LIBINTL */

/* path to localstate */
#define LOCALSTATEDIR "/var/heimdal"

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* define if the system is missing a prototype for asnprintf() */
#define NEED_ASNPRINTF_PROTO 1

/* define if the system is missing a prototype for asprintf() */
/* #undef NEED_ASPRINTF_PROTO */

/* define if the system is missing a prototype for crypt() */
/* #undef NEED_CRYPT_PROTO */

/* define if the system is missing a prototype for daemon() */
#define NEED_DAEMON_PROTO 1

/* define if the system is missing a prototype for gethostname() */
/* #undef NEED_GETHOSTNAME_PROTO */

/* define if the system is missing a prototype for getusershell() */
/* #undef NEED_GETUSERSHELL_PROTO */

/* define if the system is missing a prototype for glob() */
/* #undef NEED_GLOB_PROTO */

/* define if the system is missing a prototype for hstrerror() */
/* #undef NEED_HSTRERROR_PROTO */

/* define if the system is missing a prototype for inet_aton() */
/* #undef NEED_INET_ATON_PROTO */

/* define if the system is missing a prototype for iruserok() */
/* #undef NEED_IRUSEROK_PROTO */

/* define if the system is missing a prototype for mkstemp() */
/* #undef NEED_MKSTEMP_PROTO */

/* if your qsort is not a stable sort */
/* #undef NEED_QSORT */

/* define if the system is missing a prototype for SecKeyGetCSPHandle() */
/* #undef NEED_SECKEYGETCSPHANDLE_PROTO */

/* define if the system is missing a prototype for setenv() */
/* #undef NEED_SETENV_PROTO */

/* define if the system is missing a prototype for snprintf() */
/* #undef NEED_SNPRINTF_PROTO */

/* define if the system is missing a prototype for strndup() */
/* #undef NEED_STRNDUP_PROTO */

/* define if the system is missing a prototype for strsep() */
/* #undef NEED_STRSEP_PROTO */

/* define if the system is missing a prototype for strsvisx() */
#define NEED_STRSVISX_PROTO 1

/* define if the system is missing a prototype for strsvis() */
#define NEED_STRSVIS_PROTO 1

/* define if the system is missing a prototype for strtok_r() */
/* #undef NEED_STRTOK_R_PROTO */

/* define if the system is missing a prototype for strunvis() */
/* #undef NEED_STRUNVIS_PROTO */

/* define if the system is missing a prototype for strvisx() */
/* #undef NEED_STRVISX_PROTO */

/* define if the system is missing a prototype for strvis() */
/* #undef NEED_STRVIS_PROTO */

/* define if the system is missing a prototype for svis() */
#define NEED_SVIS_PROTO 1

/* define if the system is missing a prototype for unsetenv() */
/* #undef NEED_UNSETENV_PROTO */

/* define if the system is missing a prototype for unvis() */
/* #undef NEED_UNVIS_PROTO */

/* define if the system is missing a prototype for vasnprintf() */
#define NEED_VASNPRINTF_PROTO 1

/* define if the system is missing a prototype for vasprintf() */
/* #undef NEED_VASPRINTF_PROTO */

/* define if the system is missing a prototype for vis() */
/* #undef NEED_VIS_PROTO */

/* define if the system is missing a prototype for vsnprintf() */
/* #undef NEED_VSNPRINTF_PROTO */

/* Define if you don't wan't support for AFS. */
/* #undef NO_AFS */

/* Define to 1 if your C compiler doesn't accept -c and -o together. */
/* #undef NO_MINUS_C_MINUS_O */

/* Define if you don't want to use mmap. */
/* #undef NO_MMAP */

/* Define this to enable old environment option in telnet. */
#define OLD_ENVIRON 1

/* Define if you have the openldap package. */
/* #undef OPENLDAP */

/* Define if you want support for hdb ldap module */
/* #undef OPENLDAP_MODULE */

/* define if prototype of openlog is compatible with void openlog(const char
   *, int, int) */
#define OPENLOG_PROTO_COMPATIBLE 1

/* Define if you want OTP support in applications. */
#define OTP 1

/* Name of package */
#define PACKAGE "heimdal"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "heimdal-bugs@h5l.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "Heimdal"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "Heimdal 1.5.2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "heimdal"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "1.5.2"

/* Define to enable PKINIT. */
#define PKINIT 1

/* Define if getlogin has POSIX flavour (and not BSD). */
/* #undef POSIX_GETLOGIN */

/* Define if getpwnam_r has POSIX flavour. */
#define POSIX_GETPWNAM_R 1

/* Define if you have the readline package. */
/* #undef READLINE */

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* path to sbin */
#define SBINDIR "/usr/sbin"

/* Define if you want to use samba socket wrappers. */
/* #undef SOCKET_WRAPPER_REPLACE */

/* Define if you have the sqlite3 package. */
/* #undef SQLITE3 */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define if you have streams ptys. */
/* #undef STREAMSPTY */

/* define if prototype of strerror_r is compatible with int strerror_r(int,
   char *, size_t) */
#define STRERROR_R_PROTO_COMPATIBLE 1

/* Define if os support want to detach is daemonens. */
#define SUPPORT_DETACH 1

/* Enable use of inetd style startup. */
#define SUPPORT_INETD 1

/* path to sysconf */
#define SYSCONFDIR "/etc"

/* Define to what version of SunOS you are running. */
/* #undef SunOS */

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* Version number of package */
#define VERSION "1.5.2"

/* Define if signal handlers return void. */
#define VOID_RETSIGTYPE 1

/* define if target is big endian */
/* #undef WORDS_BIGENDIAN */

/* Define to 1 if the X Window System is missing or not being used. */
#define X_DISPLAY_MISSING 1

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* Required for functional/sane headers on AIX */
/* #undef _ALL_SOURCE */

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define to enable extensions on glibc-based systems such as Linux. */
#define _GNU_SOURCE 1

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to get POSIX getpwnam_r in some systems. */
/* #undef _POSIX_PTHREAD_SEMANTICS */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef gid_t */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define this to what the type mode_t should be. */
/* #undef mode_t */

/* Define to `long int' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Path name delimiter */
#define rk_PATH_DELIM '/'

/* Define this to what the type sig_atomic_t should be. */
/* #undef sig_atomic_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef uid_t */

#if _AIX
/* XXX this is gross, but kills about a gazillion warnings */
struct ether_addr;
struct sockaddr;
struct sockaddr_dl;
struct sockaddr_in;
#endif

#ifdef __APPLE__
#include <AvailabilityMacros.h>
#endif

#ifdef ROKEN_RENAME
#include "roken_rename.h"
#endif

#ifdef VOID_RETSIGTYPE
#define SIGRETURN(x) return
#else
#define SIGRETURN(x) return (RETSIGTYPE)(x)
#endif

#ifdef BROKEN_REALLOC
#define realloc(X, Y) rk_realloc((X), (Y))
#endif


#ifdef ENDIANESS_IN_SYS_PARAM_H
#  include <sys/types.h>
#  include <sys/param.h>
#  if BYTE_ORDER == BIG_ENDIAN
#  define WORDS_BIGENDIAN 1
#  endif
#endif




/* Set this to the default system lead string for telnetd 
 * can contain %-escapes: %s=sysname, %m=machine, %r=os-release
 * %v=os-version, %t=tty, %h=hostname, %d=date and time
 */
/* #undef USE_IM */

/* Used with login -p */
/* #undef LOGIN_ARGS */

/* set this to a sensible login */
#ifndef LOGIN_PATH
#define LOGIN_PATH BINDIR "/login"
#endif

