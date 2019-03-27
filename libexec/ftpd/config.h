/* $FreeBSD$ */


/* config.h.  Generated automatically by configure.  */
/* config.h.in.  Generated automatically from configure.in by autoheader.  */
/* $Id: config.h.in,v 1.15 2001/04/28 07:11:46 lukem Exp $ */


/* Define if the closedir function returns void instead of int.  */
/* #undef CLOSEDIR_VOID */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define if your C compiler doesn't accept -c and -o together.  */
/* #undef NO_MINUS_C_MINUS_O */

/* Define if your Fortran 77 compiler doesn't accept -c and -o together. */
/* #undef F77_NO_MINUS_C_MINUS_O */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define to the type of arg1 for select(). */
/* #undef SELECT_TYPE_ARG1 */

/* Define to the type of args 2, 3 and 4 for select(). */
/* #undef SELECT_TYPE_ARG234 */

/* Define to the type of arg5 for select(). */
/* #undef SELECT_TYPE_ARG5 */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
#define TIME_WITH_SYS_TIME 1

/* Define if the closedir function returns void instead of int.  */
/* #undef VOID_CLOSEDIR */

/* The number of bytes in a off_t.  */
#define SIZEOF_OFF_T 0

/* Define if you have the err function.  */
#define HAVE_ERR 1

/* Define if you have the fgetln function.  */
#define HAVE_FGETLN 1

/* Define if you have the flock function.  */
#define HAVE_FLOCK 1

/* Define if you have the fparseln function.  */
#define HAVE_FPARSELN 1

/* Define if you have the fts_open function.  */
#define HAVE_FTS_OPEN 1

/* Define if you have the getaddrinfo function.  */
#define HAVE_GETADDRINFO 1

/* Define if you have the getgrouplist function.  */
#define HAVE_GETGROUPLIST 1

/* Define if you have the getnameinfo function.  */
#define HAVE_GETNAMEINFO 1

/* Define if you have the getspnam function.  */
/* #undef HAVE_GETSPNAM */

/* Define if you have the getusershell function.  */
#define HAVE_GETUSERSHELL 1

/* Define if you have the inet_net_pton function.  */
#define HAVE_INET_NET_PTON 1

/* Define if you have the inet_ntop function.  */
#define HAVE_INET_NTOP 1

/* Define if you have the inet_pton function.  */
#define HAVE_INET_PTON 1

/* Define if you have the lockf function.  */
#define HAVE_LOCKF 1

/* Define if you have the mkstemp function.  */
#define HAVE_MKSTEMP 1

/* Define if you have the setlogin function.  */
#define HAVE_SETLOGIN 1

/* Define if you have the setproctitle function.  */
#define HAVE_SETPROCTITLE 1

/* Define if you have the sl_init function.  */
#define HAVE_SL_INIT 1

/* Define if you have the snprintf function.  */
#define HAVE_SNPRINTF 1

/* Define if you have the strdup function.  */
#define HAVE_STRDUP 1

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strlcat function.  */
#define HAVE_STRLCAT 1

/* Define if you have the strlcpy function.  */
#define HAVE_STRLCPY 1

/* Define if you have the strmode function.  */
#define HAVE_STRMODE 1

/* Define if you have the strsep function.  */
#define HAVE_STRSEP 1

/* Define if you have the strtoll function.  */
#define HAVE_STRTOLL 1

/* Define if you have the user_from_uid function.  */
#define HAVE_USER_FROM_UID 1

/* Define if you have the usleep function.  */
#define HAVE_USLEEP 1

/* Define if you have the vfork function.  */
#define HAVE_VFORK 1

/* Define if you have the vsyslog function.  */
#define HAVE_VSYSLOG 1

/* Define if you have the <arpa/nameser.h> header file.  */
#define HAVE_ARPA_NAMESER_H 1

/* Define if you have the <dirent.h> header file.  */
#define HAVE_DIRENT_H 1

/* Define if you have the <err.h> header file.  */
#define HAVE_ERR_H 1

/* Define if you have the <fts.h> header file.  */
#define HAVE_FTS_H 1

/* Define if you have the <libutil.h> header file.  */
#define HAVE_LIBUTIL_H 1

/* Define if you have the <ndir.h> header file.  */
/* #undef HAVE_NDIR_H */

/* Define if you have the <paths.h> header file.  */
#define HAVE_PATHS_H 1

/* Define if you have the <sys/dir.h> header file.  */
#define HAVE_SYS_DIR_H 1

/* Define if you have the <sys/ndir.h> header file.  */
/* #undef HAVE_SYS_NDIR_H */

/* Define if you have the <sys/sysmacros.h> header file.  */
/* #undef HAVE_SYS_SYSMACROS_H */

/* Define if you have the <util.h> header file.  */
/* #undef HAVE_UTIL_H */

/* Define if you have the crypt library (-lcrypt).  */
#define HAVE_LIBCRYPT 1

/* Define if you have the nsl library (-lnsl).  */
/* #undef HAVE_LIBNSL */

/* Define if you have the skey library (-lskey).  */
/* #undef HAVE_LIBSKEY */

/* Define if you have the socket library (-lsocket).  */
/* #undef HAVE_LIBSOCKET */

/* Define if you have the util library (-lutil).  */
#define HAVE_LIBUTIL 1

/* Define if your compiler supports `long long' */
#define HAVE_LONG_LONG 1

/* Define if *printf() uses %qd to print `long long' (otherwise uses %lld) */
#define HAVE_PRINTF_QD 1

/* Define if in_port_t exists */
#define HAVE_IN_PORT_T 1

/* Define if struct sockaddr.sa_len exists (implies sockaddr_in.sin_len, etc) */
#define HAVE_SOCKADDR_SA_LEN 1

/* Define if socklen_t exists */
#define HAVE_SOCKLEN_T 1

/* Define if AF_INET6 exists in <sys/socket.h> */
#define HAVE_AF_INET6 1

/* Define if `struct sockaddr_in6' exists in <netinet/in.h> */
#define HAVE_SOCKADDR_IN6 1

/* Define if `struct addrinfo' exists in <netdb.h> */
#define HAVE_ADDRINFO 1

/*
 * Define if <netdb.h> contains AI_NUMERICHOST et al.
 * Systems which only implement RFC2133 will need this.
 */
#define HAVE_RFC2553_NETDB 1

/* Define if `struct direct' has a d_namlen element */
#define HAVE_D_NAMLEN 1

/* Define if struct passwd.pw_expire exists. */
#define HAVE_PW_EXPIRE 1

/* Define if GLOB_BRACE, gl_path and gl_match exist in <glob.h> */
#define HAVE_WORKING_GLOB 1

/* Define if crypt() is declared in <unistd.h> */
#define HAVE_CRYPT_D 1

/* Define if fclose() is declared in <stdio.h> */
#define HAVE_FCLOSE_D 1

/* Define if optarg is declared in <stdlib.h> or <unistd.h> */
#define HAVE_OPTARG_D 1

/* Define if optind is declared in <stdlib.h> or <unistd.h> */
#define HAVE_OPTIND_D 1

/* Define if optreset exists */
#define HAVE_OPTRESET 1

/* Define if pclose() is declared in <stdio.h> */
#define HAVE_PCLOSE_D 1

/* Define if getusershell() is declared in <unistd.h> */
#define HAVE_GETUSERSHELL_D 1

/* Define if `long long' is supported and sizeof(off_t) >= 8 */
#define HAVE_QUAD_SUPPORT 1

/* Define if not using in-built /bin/ls code */
/* #undef NO_INTERNAL_LS */

/* Define if using S/Key */
/* #undef SKEY */

/*
 * Define this if compiling with SOCKS (the firewall traversal library).
 * Also, you must define connect, getsockname, bind, accept, listen, and
 * select to their R-versions.
 */
/* #undef	SOCKS */
/* #undef	SOCKS4 */
/* #undef	SOCKS5 */
/* #undef	connect */
/* #undef	getsockname */
/* #undef	bind */
/* #undef	accept */
/* #undef	listen */
/* #undef	select */
/* #undef	dup */
/* #undef	dup2 */
/* #undef	fclose */
/* #undef	gethostbyname */
/* #undef	getpeername */
/* #undef	read */
/* #undef	recv */
/* #undef	recvfrom */
/* #undef	rresvport */
/* #undef	send */
/* #undef	sendto */
/* #undef	shutdown */
/* #undef	write */

/* Define if you have the <arpa/ftp.h> header file.  */
#define HAVE_FTP_NAMES 1
