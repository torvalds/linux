/* $FreeBSD$ */
/* config.h.  Generated automatically by configure.  */
/* config.h.in.  Generated automatically from configure.in by autoheader.  */

/* Define if on AIX 3.
   System headers sometimes define this.
   We just want to avoid a redefinition error message.  */
#ifndef _ALL_SOURCE
/* #undef _ALL_SOURCE */
#endif

/* Define if using alloca.c.  */
/* #undef C_ALLOCA */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define to one of _getb67, GETB67, getb67 for Cray-2 and Cray-YMP systems.
   This function is required for alloca.c support on those systems.  */
/* #undef CRAY_STACKSEG_END */

/* Define if you have alloca, as a function or macro.  */
#define HAVE_ALLOCA 1

/* Define if you have <alloca.h> and it should be used (not on Ultrix).  */
/* #undef HAVE_ALLOCA_H */

/* Define if you have <sys/wait.h> that is POSIX.1 compatible.  */
#define HAVE_SYS_WAIT_H 1

/* Define if on MINIX.  */
/* #undef _MINIX */

/* Define if the system does not provide POSIX.1 features except
   with this defined.  */
/* #undef _POSIX_1_SOURCE */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at run-time.
 STACK_DIRECTION > 0 => grows toward higher addresses
 STACK_DIRECTION < 0 => grows toward lower addresses
 STACK_DIRECTION = 0 => direction of growth unknown
 */
/* #undef STACK_DIRECTION */

/* Define if you want the FTP daemon to support anonymous logins. */
/* #undef DOANONYMOUS */

/* The default value of the PATH environment variable */
#define DEFAULT_PATH "/usr/bin:/bin:/usr/sbin:/sbin"

/* Defined if the file /etc/default/login exists 
   (and, presumably, should be looked at by login) */
/* #undef HAVE_ETC_DEFAULT_LOGIN */

/* Defined to the name of a file that contains a list of files whose
   permissions and ownerships should be changed on login. */
/* #undef HAVE_LOGIN_PERMFILE */

/* Defined to the name of a file that contains a list of environment
   values that should be set on login. */
/* #undef HAVE_LOGIN_ENVFILE */

/* Defined if the file /etc/securetty exists
   (and, presumably, should be looked at by login) */
/* #undef HAVE_SECURETTY */

/* Defined if the file /etc/shadow exists
   (and, presumably, should be looked at for shadow passwords) */
/* #undef HAVE_ETC_SHADOW */

/* The path to the access file, if we're going to use it */
/* #undef PATH_ACCESS_FILE */

/* The path to the mail spool, if we know it */
#define PATH_MAIL "/var/mail"

/* The path to the utmp file, if we know it */
#define PATH_UTMP_AC "/var/run/utmp"

/* The path to the wtmp file, if we know it */
#define PATH_WTMP_AC "/var/log/wtmp"

/* The path to the wtmpx file, if we know it */
/* #undef PATH_WTMPX_AC */

/* Defined if the system's profile (/etc/profile) displays
   the motd file */
/* #undef HAVE_MOTD_IN_PROFILE */

/* Defined if the system's profile (/etc/profile) informs the
   user of new mail */
/* #undef HAVE_MAILCHECK_IN_PROFILE */

/* Define if you have a nonstandard gettimeofday() that takes one argument
   instead of two. */
/* #undef HAVE_ONE_ARG_GETTIMEOFDAY */

/* Define if the system has the getenv function */
#define HAVE_GETENV 1

/* Define if the system has the setenv function */
#define HAVE_SETENV 1

/* Define if the system has the /var/adm/sulog file */
/* #undef HAVE_SULOG */

/* Define if the system has the unsetenv function */
#define HAVE_UNSETENV 1

/* Define if the compiler can handle ANSI-style argument lists */
#define HAVE_ANSIDECL 1

/* Define if the compiler can handle ANSI-style prototypes */
#define HAVE_ANSIPROTO 1

/* Define if the system has an ANSI-style printf (returns int instead of char *) */
#define HAVE_ANSISPRINTF 1

/* Define if the compiler can handle ANSI-style variable argument lists */
#define HAVE_ANSISTDARG 1

/* Define if the compiler can handle void argument lists to functions */
#define HAVE_VOIDARG 1

/* Define if the compiler can handle void return "values" from functions */
#define HAVE_VOIDRET 1

/* Define if the compiler can handle void pointers to our liking */
#define HAVE_VOIDPTR 1

/* Define if the /bin/ls command seems to support the -g flag */
/* #undef HAVE_LS_G_FLAG */

/* Define if there is a ut_pid field in struct utmp */
/* #undef HAVE_UT_PID */

/* Define if there is a ut_type field in struct utmp */
/* #undef HAVE_UT_TYPE */

/* Define if there is a ut_name field in struct utmp */
#define HAVE_UT_NAME 1

/* Define if there is a ut_host field in struct utmp */
#define HAVE_UT_HOST 1

/* Define if the system has getutline() */
/* #undef HAVE_GETUTLINE */

/* Defined if the system has SunOS C2 security shadow passwords */
/* #undef HAVE_SUNOS_C2_SHADOW */

/* Defined if you want to disable utmp support */
/* #undef DISABLE_UTMP */

/* Defined if you want to allow users to override the insecure checks */
/* #undef INSECURE_OVERRIDE */

/* Defined to the default hash value, always defined */
#define MDX 5

/* Defined if new-style prompts are to be used */
#define NEW_PROMPTS 1

/* Defined to the path of the OPIE lock directory */
#define OPIE_LOCK_DIR "/var/spool/opielocks"

/* Defined if users are to be asked to re-type secret pass phrases */
/* #undef RETYPE */

/* Defined if su should not switch to disabled accounts */
/* #undef SU_STAR_CHECK */

/* Don't turn it on! It allows intruder easily disable whole OPIE for user */
/* Defined if user locking is to be used */
/* #undef USER_LOCKING */

/* Define if you have the bcopy function.  */
/* #undef HAVE_BCOPY */

/* Define if you have the bzero function.  */
/* #undef HAVE_BZERO */

/* Define if you have the endspent function.  */
/* #undef HAVE_ENDSPENT */

/* Define if you have the fpurge function.  */
#define HAVE_FPURGE 1

/* Define if you have the getdtablesize function.  */
/* #undef HAVE_GETDTABLESIZE */

/* Define if you have the getgroups function.  */
#define HAVE_GETGROUPS 1

/* Define if you have the gethostname function.  */
/* #undef HAVE_GETHOSTNAME */

/* Define if you have the getspnam function.  */
/* #undef HAVE_GETSPNAM */

/* Define if you have the gettimeofday function.  */
#define HAVE_GETTIMEOFDAY 1

/* Define if you have the getttynam function.  */
#define HAVE_GETTTYNAM 1

/* Define if you have the getusershell function.  */
#define HAVE_GETUSERSHELL 1

/* Define if you have the getutxline function.  */
#define HAVE_GETUTXLINE 1

/* Define if you have the getwd function.  */
/* #undef HAVE_GETWD */

/* Define if you have the index function.  */
/* #undef HAVE_INDEX */

/* Define if you have the lstat function.  */
#define HAVE_LSTAT 1

/* Define if you have the on_exit function.  */
/* #undef HAVE_ON_EXIT */

/* Define if you have the pututxline function.  */
#define HAVE_PUTUTXLINE 1

/* Define if you have the rindex function.  */
/* #undef HAVE_RINDEX */

/* Define if you have the setgroups function.  */
#define HAVE_SETGROUPS 1

/* Define if you have the setlogin function.  */
#define HAVE_SETLOGIN 1

/* Define if you have the setpriority function.  */
#define HAVE_SETPRIORITY 1

/* Define if you have the setregid function.  */
/* #undef HAVE_SETREGID */

/* Define if you have the setresgid function.  */
/* #undef HAVE_SETRESGID */

/* Define if you have the setresuid function.  */
/* #undef HAVE_SETRESUID */

/* Define if you have the setreuid function.  */
/* #undef HAVE_SETREUID */

/* Define if you have the setvbuf function.  */
#define HAVE_SETVBUF 1

/* Define if you have the sigaddset function.  */
#define HAVE_SIGADDSET 1

/* Define if you have the sigblock function.  */
/* #undef HAVE_SIGBLOCK */

/* Define if you have the sigemptyset function.  */
#define HAVE_SIGEMPTYSET 1

/* Define if you have the sigsetmask function.  */
/* #undef HAVE_SIGSETMASK */

/* Define if you have the socket function.  */
#define HAVE_SOCKET 1

/* Define if you have the strerror function.  */
#define HAVE_STRERROR 1

/* Define if you have the strftime function.  */
#define HAVE_STRFTIME 1

/* Define if you have the strncasecmp function.  */
#define HAVE_STRNCASECMP 1

/* Define if you have the strstr function.  */
#define HAVE_STRSTR 1

/* Define if you have the ttyslot function.  */
#define HAVE_TTYSLOT 1

/* Define if you have the usleep function.  */
#define HAVE_USLEEP 1

/* Define if you have the <crypt.h> header file.  */
/* #undef HAVE_CRYPT_H */

/* Define if you have the <dirent.h> header file.  */
#define HAVE_DIRENT_H 1

/* Define if you have the <fcntl.h> header file.  */
#define HAVE_FCNTL_H 1

/* Define if you have the <lastlog.h> header file.  */
/* #undef HAVE_LASTLOG_H */

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <ndir.h> header file.  */
/* #undef HAVE_NDIR_H */

/* Define if you have the <paths.h> header file.  */
#define HAVE_PATHS_H 1

/* Define if you have the <pwd.h> header file.  */
#define HAVE_PWD_H 1

/* Define if you have the <shadow.h> header file.  */
/* #undef HAVE_SHADOW_H */

/* Define if you have the <signal.h> header file.  */
#define HAVE_SIGNAL_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <sys/dir.h> header file.  */
/* #undef HAVE_SYS_DIR_H */

/* Define if you have the <sys/file.h> header file.  */
#define HAVE_SYS_FILE_H 1

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H 1

/* Define if you have the <sys/ndir.h> header file.  */
/* #undef HAVE_SYS_NDIR_H */

/* Define if you have the <sys/param.h> header file.  */
#define HAVE_SYS_PARAM_H 1

/* Define if you have the <sys/select.h> header file.  */
#define HAVE_SYS_SELECT_H 1

/* Define if you have the <sys/signal.h> header file.  */
#define HAVE_SYS_SIGNAL_H 1

/* Define if you have the <sys/time.h> header file.  */
#define HAVE_SYS_TIME_H 1

/* Define if you have the <sys/utsname.h> header file.  */
#define HAVE_SYS_UTSNAME_H 1

/* Define if you have the <syslog.h> header file.  */
#define HAVE_SYSLOG_H 1

/* Define if you have the <termios.h> header file.  */
#define HAVE_TERMIOS_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the <utmpx.h> header file.  */
#define HAVE_UTMPX_H 1

/* Define if you have the crypt library (-lcrypt).  */
#define HAVE_LIBCRYPT 1

/* Define if you have the nsl library (-lnsl).  */
/* #undef HAVE_LIBNSL */

/* Define if you have the posix library (-lposix).  */
/* #undef HAVE_LIBPOSIX */

/* Define if you have the socket library (-lsocket).  */
/* #undef HAVE_LIBSOCKET */
