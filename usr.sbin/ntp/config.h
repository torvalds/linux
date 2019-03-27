/* config.h.  Generated from config.h.in by configure.  */
/* config.h.in.  Generated from configure.ac by autoheader.  */
/* $FreeBSD$ */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Is adjtime() accurate? */
/* #undef ADJTIME_IS_ACCURATE */

/* Support NTP Autokey protocol? */
/* #define AUTOKEY 1 */

/* why not HAVE_P_S? */
/* #undef CALL_PTHREAD_SETCONCURRENCY */

/* ACTS modem service */
#define CLOCK_ACTS 1

/* Arbiter 1088A/B GPS receiver */
#define CLOCK_ARBITER 1

/* ARCRON support? */
#define CLOCK_ARCRON_MSF 1

/* Austron 2200A/2201A GPS receiver? */
#define CLOCK_AS2201 1

/* PPS interface? */
#define CLOCK_ATOM 1

/* Datum/Bancomm bc635/VME interface? */
/* #undef CLOCK_BANC */

/* Chronolog K-series WWVB receiver? */
#define CLOCK_CHRONOLOG 1

/* CHU modem/decoder */
#define CLOCK_CHU 1

/* Diems Computime Radio Clock? */
/* #undef CLOCK_COMPUTIME */

/* Datum Programmable Time System? */
#define CLOCK_DATUM 1

/* ELV/DCF7000 clock? */
/* #undef CLOCK_DCF7000 */

/* Dumb generic hh:mm:ss local clock? */
#define CLOCK_DUMBCLOCK 1

/* Forum Graphic GPS datating station driver? */
#define CLOCK_FG 1

/* GPSD JSON receiver */
#define CLOCK_GPSDJSON 1

/* TrueTime GPS receiver/VME interface? */
/* #undef CLOCK_GPSVME */

/* Heath GC-1000 WWV/WWVH receiver? */
#define CLOCK_HEATH 1

/* HOPF 6021 clock? */
/* #undef CLOCK_HOPF6021 */

/* HOPF PCI clock device? */
#define CLOCK_HOPF_PCI 1

/* HOPF serial clock device? */
#define CLOCK_HOPF_SERIAL 1

/* HP 58503A GPS receiver? */
#define CLOCK_HPGPS 1

/* IRIG audio decoder? */
#define CLOCK_IRIG 1

/* JJY receiver? */
#define CLOCK_JJY 1

/* Rockwell Jupiter GPS clock? */
#define CLOCK_JUPITER 1

/* Leitch CSD 5300 Master Clock System Driver? */
#define CLOCK_LEITCH 1

/* local clock reference? */
#define CLOCK_LOCAL 1

/* Meinberg clocks */
#define CLOCK_MEINBERG 1

/* Magnavox MX4200 GPS receiver */
/* #undef CLOCK_MX4200 */

/* NeoClock4X */
#define CLOCK_NEOCLOCK4X 1

/* NMEA GPS receiver */
#define CLOCK_NMEA 1

/* Motorola UT Oncore GPS */
#define CLOCK_ONCORE 1

/* Palisade clock */
#define CLOCK_PALISADE 1

/* PARSE driver interface */
#define CLOCK_PARSE 1

/* Conrad parallel port radio clock */
#define CLOCK_PCF 1

/* PCL 720 clock support */
/* #undef CLOCK_PPS720 */

/* PST/Traconex 1020 WWV/WWVH receiver */
#define CLOCK_PST 1

/* DCF77 raw time code */
#define CLOCK_RAWDCF 1

/* RCC 8000 clock */
/* #undef CLOCK_RCC8000 */

/* RIPE NCC Trimble clock */
/* #undef CLOCK_RIPENCC */

/* Schmid DCF77 clock */
/* #undef CLOCK_SCHMID */

/* SEL240X protocol */
/* #undef CLOCK_SEL240X */

/* clock thru shared memory */
#define CLOCK_SHM 1

/* Spectracom 8170/Netclock/2 WWVB receiver */
#define CLOCK_SPECTRACOM 1

/* KSI/Odetics TPRO/S GPS receiver/IRIG interface */
/* #undef CLOCK_TPRO */

/* Trimble GPS receiver/TAIP protocol */
/* #undef CLOCK_TRIMTAIP */

/* Trimble GPS receiver/TSIP protocol */
/* #undef CLOCK_TRIMTSIP */

/* Kinemetrics/TrueTime receivers */
#define CLOCK_TRUETIME 1

/* Spectracom TSYNC timing board */
/* #undef CLOCK_TSYNCPCI */

/* TrueTime 560 IRIG-B decoder? */
/* #undef CLOCK_TT560 */

/* Ultralink M320 WWVB receiver? */
#define CLOCK_ULINK 1

/* VARITEXT clock */
/* #undef CLOCK_VARITEXT */

/* WHARTON 400A Series clock */
/* #undef CLOCK_WHARTON_400A */

/* WWV audio driver */
#define CLOCK_WWV 1

/* Zyfer GPStarplus */
#define CLOCK_ZYFER 1

/* Define to one of `_getb67', `GETB67', `getb67' for Cray-2 and Cray-YMP
   systems. This function is required for `alloca.c' support on those systems.
   */
/* #undef CRAY_STACKSEG_END */

/* Define to 1 if using `alloca.c'. */
/* #undef C_ALLOCA */

/* Enable debugging code? */
/* #undef DEBUG */

/* Enable processing time debugging? */
/* #undef DEBUG_TIMING */

/* Declaration style */
/* #undef DECL_ADJTIME_0 */

/* Declaration style */
/* #undef DECL_BCOPY_0 */

/* Declaration style */
/* #undef DECL_BZERO_0 */

/* Declaration style */
/* #undef DECL_CFSETISPEED_0 */

/* Declare errno? */
/* #undef DECL_ERRNO */

/* Declaration style */
/* #undef DECL_HSTRERROR_0 */

/* Declare h_errno? */
#define DECL_H_ERRNO 1

/* Declaration style */
/* #undef DECL_INET_NTOA_0 */

/* Declaration style */
/* #undef DECL_IOCTL_0 */

/* Declaration style */
/* #undef DECL_IPC_0 */

/* Declaration style */
/* #undef DECL_MEMMOVE_0 */

/* Declaration style */
/* #undef DECL_MKSTEMP_0 */

/* Declaration style */
/* #undef DECL_MKTEMP_0 */

/* Declaration style */
/* #undef DECL_NLIST_0 */

/* Declaration style */
/* #undef DECL_PLOCK_0 */

/* Declaration style */
/* #undef DECL_RENAME_0 */

/* Declaration style */
/* #undef DECL_SELECT_0 */

/* Declaration style */
/* #undef DECL_SETITIMER_0 */

/* Declaration style */
/* #undef DECL_SETPRIORITY_0 */

/* Declaration style */
/* #undef DECL_SETPRIORITY_1 */

/* Declaration style */
/* #undef DECL_SIGVEC_0 */

/* Declaration style */
/* #undef DECL_STDIO_0 */

/* Declaration style */
/* #undef DECL_STIME_0 */

/* Declaration style */
/* #undef DECL_STIME_1 */

/* Declaration style */
/* #undef DECL_STRERROR_0 */

/* Declaration style */
/* #undef DECL_STRTOL_0 */

/* Declare syscall()? */
/* #undef DECL_SYSCALL */

/* Declaration style */
/* #undef DECL_SYSLOG_0 */

/* Declaration style */
/* #undef DECL_TIMEOFDAY_0 */

/* Declaration style */
/* #undef DECL_TIME_0 */

/* Declaration style */
/* #undef DECL_TOLOWER_0 */

/* Declaration style */
/* #undef DECL_TOUPPER_0 */

/* What is the fallback value for HZ? */
#define DEFAULT_HZ 100

/* Default number of megabytes for RLIMIT_MEMLOCK */
#define DFLT_RLIMIT_MEMLOCK 32

/* Default number of 4k pages for RLIMIT_STACK */
#define DFLT_RLIMIT_STACK 50

/* Directory separator character, usually / or \\ */
#define DIR_SEP '/'

/* use old autokey session key behavior? */
/* #undef DISABLE_BUG1243_FIX */

/* synch TODR hourly? */
/* #undef DOSYNCTODR */

/* The number of minutes in a DST adjustment */
#define DSTMINUTES 60

/* support dynamic interleave? */
#define DYNAMIC_INTERLEAVE 0

/* number of args to el_init() */
#define EL_INIT_ARGS 4

/* Provide the explicit 127.0.0.0/8 martian filter? */
#define ENABLE_BUG3020_FIX 1

/* Enable CMAC support? */
#define ENABLE_CMAC 1

/* nls support in libopts */
/* #undef ENABLE_NLS */

/* force ntpdate to step the clock if !defined(STEP_SLEW) ? */
/* #undef FORCE_NTPDATE_STEP */

/* What is getsockname()'s socklen type? */
#define GETSOCKNAME_SOCKLEN_TYPE socklen_t

/* Do we have a routing socket (rt_msghdr or rtattr)? */
#define HAS_ROUTING_SOCKET 1

/* via __adjtimex */
/* #undef HAVE_ADJTIMEX */

/* Define to 1 if you have `alloca', as a function or macro. */
#define HAVE_ALLOCA 1

/* Define to 1 if you have <alloca.h> and it should be used (not on Ultrix).
   */
/* #undef HAVE_ALLOCA_H */

/* Define to 1 if you have the `arc4random_buf' function. */
#define HAVE_ARC4RANDOM_BUF 1

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#define HAVE_ARPA_NAMESER_H 1

/* Define to 1 if you have the `atomic_thread_fence' function. */
/* #undef HAVE_ATOMIC_THREAD_FENCE */

/* Do we have audio support? */
#define HAVE_AUDIO /**/

/* Define to 1 if you have the <bstring.h> header file. */
/* #undef HAVE_BSTRING_H */

/* Define to 1 if you have the `canonicalize_file_name' function. */
/* #undef HAVE_CANONICALIZE_FILE_NAME */

/* Define to 1 if you have the `chmod' function. */
#define HAVE_CHMOD 1

/* Do we have the CIOGETEV ioctl (SunOS, Linux)? */
/* #undef HAVE_CIOGETEV */

/* Define to 1 if you have the `clock_getres' function. */
#define HAVE_CLOCK_GETRES 1

/* Define to 1 if you have the `clock_gettime' function. */
#define HAVE_CLOCK_GETTIME 1

/* Define to 1 if you have the `clock_settime' function. */
#define HAVE_CLOCK_SETTIME 1

/* Define to 1 if you have the <cthreads.h> header file. */
/* #undef HAVE_CTHREADS_H */

/* Define to 1 if you have the `daemon' function. */
#define HAVE_DAEMON 1

/* Define to 1 if you have the declaration of `siglongjmp', and to 0 if you
   don't. */
#define HAVE_DECL_SIGLONGJMP 1

/* Define to 1 if you have the declaration of `sigsetjmp', and to 0 if you
   don't. */
#define HAVE_DECL_SIGSETJMP 1

/* Define to 1 if you have the declaration of `strerror_r', and to 0 if you
   don't. */
#define HAVE_DECL_STRERROR_R 1

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'.
   */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Use Rendezvous/DNS-SD registration */
/* #undef HAVE_DNSREGISTRATION */

/* Define to 1 if you don't have `vprintf' but do have `_doprnt.' */
/* #undef HAVE_DOPRNT */

/* Can we drop root privileges? */
#define HAVE_DROPROOT

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* Define to 1 if you have the `EVP_MD_do_all_sorted' function. */
#define HAVE_EVP_MD_DO_ALL_SORTED 1

/* Define to 1 if you have the `fchmod' function. */
#define HAVE_FCHMOD 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if you have the `finite' function. */
/* #undef HAVE_FINITE */

/* Define to 1 if you have the `fnmatch' function. */
#define HAVE_FNMATCH 1

/* Define to 1 if you have the <fnmatch.h> header file. */
#define HAVE_FNMATCH_H 1

/* Define to 1 if you have the `fork' function. */
#define HAVE_FORK 1

/* Define to 1 if you have the `fstat' function. */
#define HAVE_FSTAT 1

/* Define to 1 if you have the `getbootfile' function. */
#define HAVE_GETBOOTFILE 1

/* Define to 1 if you have the `getclock' function. */
/* #undef HAVE_GETCLOCK */

/* Define to 1 if you have the `getdtablesize' function. */
#define HAVE_GETDTABLESIZE 1

/* Define to 1 if you have the `getifaddrs' function. */
#define HAVE_GETIFADDRS 1

/* Define to 1 if you have the `getpassphrase' function. */
/* #undef HAVE_GETPASSPHRASE */

/* Define to 1 if you have the `getrusage' function. */
#define HAVE_GETRUSAGE 1

/* Define to 1 if you have the `getuid' function. */
#define HAVE_GETUID 1

/* if you have GNU Pth */
/* #undef HAVE_GNU_PTH */

/* Define to 1 if you have the <histedit.h> header file. */
#define HAVE_HISTEDIT_H 1

/* Define to 1 if you have the <history.h> header file. */
/* #undef HAVE_HISTORY_H */

/* Obvious */
#define HAVE_HZ_IN_STRUCT_CLOCKINFO 1

/* Define to 1 if you have the <ieeefp.h> header file. */
#define HAVE_IEEEFP_H 1

/* have iflist_sysctl? */
#define HAVE_IFLIST_SYSCTL 1

/* Define to 1 if you have the `if_nametoindex' function. */
#define HAVE_IF_NAMETOINDEX 1

/* inline keyword or macro available */
#define HAVE_INLINE 1

/* Define to 1 if the system has the type `int16_t'. */
#define HAVE_INT16_T 1

/* Define to 1 if the system has the type `int32'. */
/* #undef HAVE_INT32 */

/* int32 type in DNS headers, not others. */
/* #undef HAVE_INT32_ONLY_WITH_DNS */

/* Define to 1 if the system has the type `int32_t'. */
#define HAVE_INT32_T 1

/* Define to 1 if the system has the type `int8_t'. */
#define HAVE_INT8_T 1

/* Define to 1 if the system has the type `intmax_t'. */
/* #undef HAVE_INTMAX_T */

/* Define to 1 if the system has the type `intptr_t'. */
#define HAVE_INTPTR_T 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `isfinite' function. */
#define HAVE_ISFINITE 1

/* Define to 1 if you have the <kvm.h> header file. */
#define HAVE_KVM_H 1

/* Define to 1 if you have the `kvm_open' function. */
/* #undef HAVE_KVM_OPEN */

/* Define to 1 if you have the `gen' library (-lgen). */
/* #undef HAVE_LIBGEN */

/* Define to 1 if you have the <libgen.h> header file. */
#define HAVE_LIBGEN_H 1

/* Define to 1 if you have the `intl' library (-lintl). */
/* #undef HAVE_LIBINTL */

/* Define to 1 if you have the <libintl.h> header file. */
/* #undef HAVE_LIBINTL_H */

/* Define to 1 if you have the <libscf.h> header file. */
/* #undef HAVE_LIBSCF_H */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* using Linux pthread? */
/* #undef HAVE_LINUXTHREADS */

/* Do we have Linux capabilities? */
/* #undef HAVE_LINUX_CAPABILITIES */

/* Define to 1 if you have the <linux/if_addr.h> header file. */
/* #undef HAVE_LINUX_IF_ADDR_H */

/* if you have LinuxThreads */
/* #undef HAVE_LINUX_THREADS */

/* Define to 1 if you have the `localeconv' function. */
/* #undef HAVE_LOCALECONV */

/* Define to 1 if you have the <locale.h> header file. */
/* #undef HAVE_LOCALE_H */

/* Define to 1 if the system has the type `long double'. */
/* #undef HAVE_LONG_DOUBLE */

/* Define to 1 if the system has the type `long long'. */
#define HAVE_LONG_LONG 1

/* Define to 1 if the system has the type `long long int'. */
/* #undef HAVE_LONG_LONG_INT */

/* if you have SunOS LWP package */
/* #undef HAVE_LWP */

/* Define to 1 if you have the <lwp/lwp.h> header file. */
/* #undef HAVE_LWP_LWP_H */

/* Define to 1 if you have the <machine/inline.h> header file. */
/* #undef HAVE_MACHINE_INLINE_H */

/* Define to 1 if you have the <machine/soundcard.h> header file. */
/* #undef HAVE_MACHINE_SOUNDCARD_H */

/* define if you have Mach Cthreads */
/* #undef HAVE_MACH_CTHREADS */

/* Define to 1 if you have the <mach/cthreads.h> header file. */
/* #undef HAVE_MACH_CTHREADS_H */

/* Define to 1 if you have the <math.h> header file. */
#define HAVE_MATH_H 1

/* Define to 1 if you have the `MD5Init' function. */
#define HAVE_MD5INIT 1

/* Define to 1 if you have the <md5.h> header file. */
#define HAVE_MD5_H 1

/* Define to 1 if you have the `memlk' function. */
/* #undef HAVE_MEMLK */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `mkstemp' function. */
#define HAVE_MKSTEMP 1

/* Define to 1 if you have the `mktime' function. */
#define HAVE_MKTIME 1

/* Define to 1 if you have the `mlockall' function. */
#define HAVE_MLOCKALL 1

/* Define to 1 if you have the `mmap' function. */
#define HAVE_MMAP 1

/* Define to 1 if you have the `nanosleep' function. */
#define HAVE_NANOSLEEP 1

/* Define to 1 if you have the <ndir.h> header file, and it defines `DIR'. */
/* #undef HAVE_NDIR_H */

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <netinet/in_system.h> header file. */
/* #undef HAVE_NETINET_IN_SYSTEM_H */

/* Define to 1 if you have the <netinet/in_systm.h> header file. */
#define HAVE_NETINET_IN_SYSTM_H 1

/* Define to 1 if you have the <netinet/in_var.h> header file. */
#define HAVE_NETINET_IN_VAR_H 1

/* Define to 1 if you have the <netinet/ip.h> header file. */
#define HAVE_NETINET_IP_H 1

/* NetInfo support? */
/* #undef HAVE_NETINFO */

/* Define to 1 if you have the <netinfo/ni.h> header file. */
/* #undef HAVE_NETINFO_NI_H */

/* Define to 1 if you have the <net/if6.h> header file. */
/* #undef HAVE_NET_IF6_H */

/* Define to 1 if you have the <net/if.h> header file. */
#define HAVE_NET_IF_H 1

/* Define to 1 if you have the <net/if_var.h> header file. */
#define HAVE_NET_IF_VAR_H 1

/* Define to 1 if you have the <net/route.h> header file. */
#define HAVE_NET_ROUTE_H 1

/* Define to 1 if you have the `nice' function. */
#define HAVE_NICE 1

/* Define to 1 if you have the <nlist.h> header file. */
#define HAVE_NLIST_H 1

/* via __adjtimex */
#define HAVE_NTP_ADJTIME 1

/* via __ntp_gettime */
#define HAVE_NTP_GETTIME 1

/* Do we want support for Samba's signing daemon? */
#define HAVE_NTP_SIGND 1

/* if you have NT Event Log */
/* #undef HAVE_NT_EVENT_LOG */

/* if you have NT Service Manager */
/* #undef HAVE_NT_SERVICE_MANAGER */

/* if you have NT Threads */
/* #undef HAVE_NT_THREADS */

/* Define to 1 if you have the <openssl/cmac.h> header file. */
#define HAVE_OPENSSL_CMAC_H 1

/* Define to 1 if you have the <openssl/hmac.h> header file. */
#define HAVE_OPENSSL_HMAC_H 1

/* Define to 1 if the system has the type `pid_t'. */
#define HAVE_PID_T 1

/* Define to 1 if you have the `plock' function. */
/* #undef HAVE_PLOCK */

/* Define to 1 if you have the <poll.h> header file. */
#define HAVE_POLL_H 1

/* Do we have the PPS API per the Draft RFC? */
#define HAVE_PPSAPI 1

/* Define to 1 if you have the <priv.h> header file. */
/* #undef HAVE_PRIV_H */

/* Define if you have POSIX threads libraries and header files. */
/* #undef HAVE_PTHREAD */

/* define to pthreads API spec revision */
#define HAVE_PTHREADS 10

/* Define to 1 if you have the `pthread_attr_getstacksize' function. */
#define HAVE_PTHREAD_ATTR_GETSTACKSIZE 1

/* Define to 1 if you have the `pthread_attr_setstacksize' function. */
#define HAVE_PTHREAD_ATTR_SETSTACKSIZE 1

/* define if you have pthread_detach function */
#define HAVE_PTHREAD_DETACH 1

/* Define to 1 if you have the `pthread_getconcurrency' function. */
#define HAVE_PTHREAD_GETCONCURRENCY 1

/* Define to 1 if you have the <pthread.h> header file. */
#define HAVE_PTHREAD_H 1

/* Define to 1 if you have the `pthread_kill' function. */
#define HAVE_PTHREAD_KILL 1

/* Define to 1 if you have the `pthread_kill_other_threads_np' function. */
/* #undef HAVE_PTHREAD_KILL_OTHER_THREADS_NP */

/* define if you have pthread_rwlock_destroy function */
#define HAVE_PTHREAD_RWLOCK_DESTROY 1

/* Define to 1 if you have the `pthread_setconcurrency' function. */
#define HAVE_PTHREAD_SETCONCURRENCY 1

/* Define to 1 if you have the `pthread_yield' function. */
#define HAVE_PTHREAD_YIELD 1

/* Define to 1 if you have the <pth.h> header file. */
/* #undef HAVE_PTH_H */

/* Define to 1 if the system has the type `ptrdiff_t'. */
#define HAVE_PTRDIFF_T 1

/* Define to 1 if you have the `pututline' function. */
/* #undef HAVE_PUTUTLINE */

/* Define to 1 if you have the `pututxline' function. */
#define HAVE_PUTUTXLINE 1

/* Define to 1 if you have the `RAND_bytes' function. */
#define HAVE_RAND_BYTES 1

/* Define to 1 if you have the `RAND_poll' function. */
#define HAVE_RAND_POLL 1

/* Define to 1 if you have the <readline.h> header file. */
/* #undef HAVE_READLINE_H */

/* Define if your readline library has \`add_history' */
#define HAVE_READLINE_HISTORY 1

/* Define to 1 if you have the <readline/history.h> header file. */
#define HAVE_READLINE_HISTORY_H 1

/* Define to 1 if you have the <readline/readline.h> header file. */
#define HAVE_READLINE_READLINE_H 1

/* Define to 1 if you have the `readlink' function. */
#define HAVE_READLINK 1

/* Define to 1 if you have the `recvmsg' function. */
#define HAVE_RECVMSG 1

/* Define to 1 if you have the <resolv.h> header file. */
#define HAVE_RESOLV_H 1

/* Define to 1 if you have the `res_init' function. */
#define HAVE_RES_INIT 1

/* Do we have Linux routing socket? */
/* #undef HAVE_RTNETLINK */

/* Define to 1 if you have the `rtprio' function. */
#define HAVE_RTPRIO 1

/* Define to 1 if you have the <runetype.h> header file. */
#define HAVE_RUNETYPE_H 1

/* Obvious */
#define HAVE_SA_SIGACTION_IN_STRUCT_SIGACTION 1

/* Define to 1 if you have the <sched.h> header file. */
#define HAVE_SCHED_H 1

/* Define to 1 if you have the `sched_setscheduler' function. */
#define HAVE_SCHED_SETSCHEDULER 1

/* Define to 1 if you have the `sched_yield' function. */
#define HAVE_SCHED_YIELD 1

/* Define to 1 if you have the <semaphore.h> header file. */
#define HAVE_SEMAPHORE_H 1

/* Define to 1 if you have the `sem_timedwait' function. */
#define HAVE_SEM_TIMEDWAIT 1

/* Define to 1 if you have the <setjmp.h> header file. */
#define HAVE_SETJMP_H 1

/* Define to 1 if you have the `setlinebuf' function. */
#define HAVE_SETLINEBUF 1

/* Define to 1 if you have the `setpgid' function. */
#define HAVE_SETPGID 1

/* define if setpgrp takes 0 arguments */
/* #undef HAVE_SETPGRP_0 */

/* Define to 1 if you have the `setpriority' function. */
#define HAVE_SETPRIORITY 1

/* Define to 1 if you have the `setrlimit' function. */
#define HAVE_SETRLIMIT 1

/* Define to 1 if you have the `setsid' function. */
#define HAVE_SETSID 1

/* Define to 1 if you have the `settimeofday' function. */
#define HAVE_SETTIMEOFDAY 1

/* Define to 1 if you have the `setvbuf' function. */
#define HAVE_SETVBUF 1

/* Define to 1 if you have the <sgtty.h> header file. */
/* #undef HAVE_SGTTY_H */

/* Define to 1 if you have the `sigaction' function. */
#define HAVE_SIGACTION 1

/* Can we use SIGIO for tcp and udp IO? */
/* #undef HAVE_SIGNALED_IO */

/* Define to 1 if you have the `sigset' function. */
#define HAVE_SIGSET 1

/* Define to 1 if you have the `sigvec' function. */
#define HAVE_SIGVEC 1

/* sigwait() available? */
#define HAVE_SIGWAIT 1

/* Define to 1 if the system has the type `size_t'. */
#define HAVE_SIZE_T 1

/* Define if C99-compliant `snprintf' is available. */
#define HAVE_SNPRINTF 1

/* Define to 1 if you have the `socketpair' function. */
#define HAVE_SOCKETPAIR 1

/* Are Solaris privileges available? */
/* #undef HAVE_SOLARIS_PRIVS */

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the <stdatomic.h> header file. */
#define HAVE_STDATOMIC_H 1

/* Define to 1 if stdbool.h conforms to C99. */
#define HAVE_STDBOOL_H 1

/* Define to 1 if you have the <stddef.h> header file. */
/* #undef HAVE_STDDEF_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `stime' function. */
/* #undef HAVE_STIME */

/* Define to 1 if you have the `strchr' function. */
#define HAVE_STRCHR 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the `strerror_r' function. */
#define HAVE_STRERROR_R 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strlcat' function. */
#define HAVE_STRLCAT 1

/* Define to 1 if you have the `strlcpy' function. */
#define HAVE_STRLCPY 1

/* Define to 1 if you have the <stropts.h> header file. */
/* #undef HAVE_STROPTS_H */

/* Define to 1 if you have the `strrchr' function. */
#define HAVE_STRRCHR 1

/* Define to 1 if you have the `strsignal' function. */
#define HAVE_STRSIGNAL 1

/* Define to 1 if you have the `strtoll' function. */
#define HAVE_STRTOLL 1

/* Define to 1 if `decimal_point' is a member of `struct lconv'. */
/* #undef HAVE_STRUCT_LCONV_DECIMAL_POINT */

/* Define to 1 if `thousands_sep' is a member of `struct lconv'. */
/* #undef HAVE_STRUCT_LCONV_THOUSANDS_SEP */

/* Do we have struct ntptimeval? */
#define HAVE_STRUCT_NTPTIMEVAL 1

/* Define to 1 if `time.tv_nsec' is a member of `struct ntptimeval'. */
#define HAVE_STRUCT_NTPTIMEVAL_TIME_TV_NSEC 1

/* Does a system header define struct ppsclockev? */
/* #undef HAVE_STRUCT_PPSCLOCKEV */

/* Do we have struct snd_size? */
#define HAVE_STRUCT_SND_SIZE 1

/* Does a system header define struct sockaddr_storage? */
#define HAVE_STRUCT_SOCKADDR_STORAGE 1

/* struct timespec declared? */
#define HAVE_STRUCT_TIMESPEC 1

/* Define to 1 if you have the <sun/audioio.h> header file. */
/* #undef HAVE_SUN_AUDIOIO_H */

/* Define to 1 if you have the <synch.h> header file. */
/* #undef HAVE_SYNCH_H */

/* Define to 1 if you have the `sysconf' function. */
#define HAVE_SYSCONF 1

/* Define to 1 if you have the <sysexits.h> header file. */
#define HAVE_SYSEXITS_H 1

/* */
#define HAVE_SYSLOG_FACILITYNAMES 1

/* Define to 1 if you have the <sys/audioio.h> header file. */
/* #undef HAVE_SYS_AUDIOIO_H */

/* Define to 1 if you have the <sys/capability.h> header file. */
#define HAVE_SYS_CAPABILITY_H 1

/* Define to 1 if you have the <sys/clockctl.h> header file. */
/* #undef HAVE_SYS_CLOCKCTL_H */

/* Define to 1 if you have the <sys/dir.h> header file, and it defines `DIR'.
   */
/* #undef HAVE_SYS_DIR_H */

/* Define to 1 if you have the <sys/file.h> header file. */
#define HAVE_SYS_FILE_H 1

/* Define to 1 if you have the <sys/i8253.h> header file. */
/* #undef HAVE_SYS_I8253_H */

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/ipc.h> header file. */
#define HAVE_SYS_IPC_H 1

/* Define to 1 if you have the <sys/limits.h> header file. */
/* #undef HAVE_SYS_LIMITS_H */

/* Define to 1 if you have the <sys/lock.h> header file. */
#define HAVE_SYS_LOCK_H 1

/* Define to 1 if you have the <sys/mac.h> header file. */
#define HAVE_SYS_MAC_H 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/modem.h> header file. */
/* #undef HAVE_SYS_MODEM_H */

/* Define to 1 if you have the <sys/ndir.h> header file, and it defines `DIR'.
   */
/* #undef HAVE_SYS_NDIR_H */

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/pcl720.h> header file. */
/* #undef HAVE_SYS_PCL720_H */

/* Define to 1 if you have the <sys/poll.h> header file. */
#define HAVE_SYS_POLL_H 1

/* Define to 1 if you have the <sys/ppsclock.h> header file. */
/* #undef HAVE_SYS_PPSCLOCK_H */

/* Define to 1 if you have the <sys/ppstime.h> header file. */
/* #undef HAVE_SYS_PPSTIME_H */

/* Define to 1 if you have the <sys/prctl.h> header file. */
/* #undef HAVE_SYS_PRCTL_H */

/* Define to 1 if you have the <sys/procset.h> header file. */
/* #undef HAVE_SYS_PROCSET_H */

/* Define to 1 if you have the <sys/proc.h> header file. */
#define HAVE_SYS_PROC_H 1

/* Define to 1 if you have the <sys/resource.h> header file. */
#define HAVE_SYS_RESOURCE_H 1

/* Define to 1 if you have the <sys/sched.h> header file. */
/* #undef HAVE_SYS_SCHED_H */

/* Define to 1 if you have the <sys/select.h> header file. */
#define HAVE_SYS_SELECT_H 1

/* Define to 1 if you have the <sys/shm.h> header file. */
#define HAVE_SYS_SHM_H 1

/* Define to 1 if you have the <sys/signal.h> header file. */
#define HAVE_SYS_SIGNAL_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/sockio.h> header file. */
#define HAVE_SYS_SOCKIO_H 1

/* Define to 1 if you have the <sys/soundcard.h> header file. */
#define HAVE_SYS_SOUNDCARD_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/stream.h> header file. */
/* #undef HAVE_SYS_STREAM_H */

/* Define to 1 if you have the <sys/stropts.h> header file. */
/* #undef HAVE_SYS_STROPTS_H */

/* Define to 1 if you have the <sys/sysctl.h> header file. */
#define HAVE_SYS_SYSCTL_H 1

/* Define to 1 if you have the <sys/syssgi.h> header file. */
/* #undef HAVE_SYS_SYSSGI_H */

/* Define to 1 if you have the <sys/systune.h> header file. */
/* #undef HAVE_SYS_SYSTUNE_H */

/* Define to 1 if you have the <sys/termios.h> header file. */
#define HAVE_SYS_TERMIOS_H 1

/* Define to 1 if you have the <sys/timepps.h> header file. */
#define HAVE_SYS_TIMEPPS_H 1

/* Define to 1 if you have the <sys/timers.h> header file. */
#define HAVE_SYS_TIMERS_H 1

/* Define to 1 if you have the <sys/timex.h> header file. */
#define HAVE_SYS_TIMEX_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/tpro.h> header file. */
/* #undef HAVE_SYS_TPRO_H */

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Use sys/uio.h for struct iovec help */
/* #undef HAVE_SYS_UIO_H */

/* Define to 1 if you have the <sys/un.h> header file. */
#define HAVE_SYS_UN_H 1

/* Define to 1 if you have the <sys/var.h> header file. */
/* #undef HAVE_SYS_VAR_H */

/* Define to 1 if you have the <sys/wait.h> header file. */
#define HAVE_SYS_WAIT_H 1

/* Define to 1 if the system has the type `s_char'. */
/* #undef HAVE_S_CHAR */

/* Define to 1 if you have the <termios.h> header file. */
#define HAVE_TERMIOS_H 1

/* Define to 1 if you have the <termio.h> header file. */
/* #undef HAVE_TERMIO_H */

/* if you have Solaris LWP (thr) package */
/* #undef HAVE_THR */

/* Define to 1 if you have the <thread.h> header file. */
/* #undef HAVE_THREAD_H */

/* Define to 1 if you have the `thr_getconcurrency' function. */
/* #undef HAVE_THR_GETCONCURRENCY */

/* Define to 1 if you have the `thr_setconcurrency' function. */
/* #undef HAVE_THR_SETCONCURRENCY */

/* Define to 1 if you have the `thr_yield' function. */
/* #undef HAVE_THR_YIELD */

/* Obvious */
#define HAVE_TICKADJ_IN_STRUCT_CLOCKINFO 1

/* Define to 1 if you have the `timegm' function. */
#define HAVE_TIMEGM 1

/* Define to 1 if you have the <timepps.h> header file. */
/* #undef HAVE_TIMEPPS_H */

/* Define to 1 if you have the `timer_create' function. */
/* #undef HAVE_TIMER_CREATE */

/* Define to 1 if you have the <timex.h> header file. */
/* #undef HAVE_TIMEX_H */

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Do we have the TIOCGPPSEV ioctl (Solaris)? */
/* #undef HAVE_TIOCGPPSEV */

/* Do we have the TIOCSPPS ioctl (Solaris)? */
/* #undef HAVE_TIOCSPPS */

/* Do we have the TIO serial stuff? */
/* #undef HAVE_TIO_SERIAL_STUFF */

/* Are TrustedBSD MAC policy privileges available? */
#define HAVE_TRUSTEDBSD_MAC 1

/* Define to 1 if the system has the type `uint16_t'. */
#define HAVE_UINT16_T 1

/* Define to 1 if the system has the type `uint32_t'. */
#define HAVE_UINT32_T 1

/* Define to 1 if the system has the type `uint8_t'. */
#define HAVE_UINT8_T 1

/* Define to 1 if the system has the type `uintmax_t'. */
/* #undef HAVE_UINTMAX_T */

/* Define to 1 if the system has the type `uintptr_t'. */
#define HAVE_UINTPTR_T 1

/* Define to 1 if the system has the type `uint_t'. */
/* #undef HAVE_UINT_T */

/* Define to 1 if you have the `umask' function. */
#define HAVE_UMASK 1

/* Define to 1 if you have the `uname' function. */
#define HAVE_UNAME 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* deviant sigwait? */
/* #undef HAVE_UNIXWARE_SIGWAIT */

/* Define to 1 if the system has the type `unsigned long long int'. */
#define HAVE_UNSIGNED_LONG_LONG_INT 1

/* Define to 1 if you have the `updwtmp' function. */
/* #undef HAVE_UPDWTMP */

/* Define to 1 if you have the `updwtmpx' function. */
/* #undef HAVE_UPDWTMPX */

/* Define to 1 if you have the <utime.h> header file. */
#define HAVE_UTIME_H 1

/* Define to 1 if you have the <utmpx.h> header file. */
#define HAVE_UTMPX_H 1

/* Define to 1 if you have the <utmp.h> header file. */
/* #undef HAVE_UTMP_H */

/* Define to 1 if the system has the type `u_int32'. */
/* #undef HAVE_U_INT32 */

/* u_int32 type in DNS headers, not others. */
/* #undef HAVE_U_INT32_ONLY_WITH_DNS */

/* Define to 1 if you have the <values.h> header file. */
/* #undef HAVE_VALUES_H */

/* Define to 1 if you have the <varargs.h> header file. */
/* #undef HAVE_VARARGS_H */

/* Define to 1 if you have the `vfork' function. */
#define HAVE_VFORK 1

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the `vprintf' function. */
#define HAVE_VPRINTF 1

/* Define if C99-compliant `vsnprintf' is available. */
#define HAVE_VSNPRINTF 1

/* Define to 1 if you have the <wchar.h> header file. */
#define HAVE_WCHAR_H 1

/* Define to 1 if the system has the type `wchar_t'. */
#define HAVE_WCHAR_T 1

/* Define to 1 if the system has the type `wint_t'. */
#define HAVE_WINT_T 1

/* Define to 1 if `fork' works. */
#define HAVE_WORKING_FORK 1

/* Define to 1 if `vfork' works. */
#define HAVE_WORKING_VFORK 1

/* define if select implicitly yields */
#define HAVE_YIELDING_SELECT 1

/* Define to 1 if the system has the type `_Bool'. */
#define HAVE__BOOL 1

/* Define to 1 if you have the `_exit' function. */
#define HAVE__EXIT 1

/* Define to 1 if you have the </sys/sync/queue.h> header file. */
/* #undef HAVE__SYS_SYNC_QUEUE_H */

/* Define to 1 if you have the </sys/sync/sema.h> header file. */
/* #undef HAVE__SYS_SYNC_SEMA_H */

/* Define to 1 if you have the `__adjtimex' function. */
/* #undef HAVE___ADJTIMEX */

/* defined if C compiler supports __attribute__((...)) */
#define HAVE___ATTRIBUTE__ /**/


	/* define away __attribute__() if unsupported */
	#ifndef HAVE___ATTRIBUTE__
	# define __attribute__(x) /* empty */
	#endif
	#define ISC_PLATFORM_NORETURN_PRE
	#define ISC_PLATFORM_NORETURN_POST __attribute__((__noreturn__))
    


/* Define to 1 if you have the `__ntp_gettime' function. */
/* #undef HAVE___NTP_GETTIME */

/* Define to 1 if you have the `__res_init' function. */
/* #undef HAVE___RES_INIT */

/* Does struct sockaddr_storage have __ss_family? */
/* #undef HAVE___SS_FAMILY_IN_SS */


	    /* Handle sockaddr_storage.__ss_family */
	    #ifdef HAVE___SS_FAMILY_IN_SS
	    # define ss_family __ss_family
	    #endif /* HAVE___SS_FAMILY_IN_SS */
	
    

/* Define to provide `rpl_snprintf' function. */
/* #undef HW_WANT_RPL_SNPRINTF */

/* Define to provide `rpl_vsnprintf' function. */
/* #undef HW_WANT_RPL_VSNPRINTF */

/* Retry queries on _any_ DNS error? */
/* #undef IGNORE_DNS_ERRORS */

/* Should we use the IRIG sawtooth filter? */
/* #undef IRIG_SUCKS */

/* Enclose PTHREAD_ONCE_INIT in extra braces? */
/* #undef ISC_PLATFORM_BRACEPTHREADONCEINIT */

/* Do we need to fix in6isaddr? */
/* #undef ISC_PLATFORM_FIXIN6ISADDR */

/* ISC: do we have if_nametoindex()? */
#define ISC_PLATFORM_HAVEIFNAMETOINDEX 1

/* have struct if_laddrconf? */
/* #undef ISC_PLATFORM_HAVEIF_LADDRCONF */

/* have struct if_laddrreq? */
/* #undef ISC_PLATFORM_HAVEIF_LADDRREQ */

/* have struct in6_pktinfo? */
#define ISC_PLATFORM_HAVEIN6PKTINFO 1

/* have IPv6? */
#define ISC_PLATFORM_HAVEIPV6 1

/* struct sockaddr has sa_len? */
#define ISC_PLATFORM_HAVESALEN 1

/* sin6_scope_id? */
#define ISC_PLATFORM_HAVESCOPEID 1

/* missing in6addr_any? */
/* #undef ISC_PLATFORM_NEEDIN6ADDRANY */

/* Do we need netinet6/in6.h? */
/* #undef ISC_PLATFORM_NEEDNETINET6IN6H */

/* ISC: provide inet_ntop() */
/* #undef ISC_PLATFORM_NEEDNTOP */

/* Declare in_port_t? */
/* #undef ISC_PLATFORM_NEEDPORTT */

/* ISC: provide inet_pton() */
/* #undef ISC_PLATFORM_NEEDPTON */

/* enable libisc thread support? */
#define ISC_PLATFORM_USETHREADS 1

/* Does the kernel have an FLL bug? */
/* #undef KERNEL_FLL_BUG */

/* Does the kernel support precision time discipline? */
#define KERNEL_PLL 1

/* Define to use libseccomp system call filtering. */
/* #undef KERN_SECCOMP */

/* What is (probably) the name of DOSYNCTODR in the kernel? */
#define K_DOSYNCTODR_NAME "_dosynctodr"

/* What is (probably) the name of NOPRINTF in the kernel? */
#define K_NOPRINTF_NAME "_noprintf"

/* What is the name of TICKADJ in the kernel? */
#define K_TICKADJ_NAME "_tickadj"

/* What is the name of TICK in the kernel? */
#define K_TICK_NAME "_tick"

/* define to 1 if library is thread safe */
#define LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE 1

/* leap smear mechanism */
#define LEAP_SMEAR 1

/* Define to any value to include libseccomp sandboxing. */
/* #undef LIBSECCOMP */

/* Should we align with the NIST lockclock scheme? */
/* #undef LOCKCLOCK */

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#define LT_OBJDIR ".libs/"

/* Does the target support multicast IP? */
#define MCAST 1

/* Should we recommend a minimum value for tickadj? */
/* #undef MIN_REC_TICKADJ */

/* Define to 1 if the compiler does not support C99's structure
   initialization. */
/* #undef MISSING_C99_STRUCT_INIT */

/* having to fork the DNS worker early when doing chroot? */
/* #undef NEED_EARLY_FORK */

/* Do we need HPUX adjtime() library support? */
/* #undef NEED_HPUX_ADJTIME */

/* Do we want the HPUX FindConfig()? */
/* #undef NEED_HPUX_FINDCONFIG */

/* We need to provide netsnmp_daemonize() */
/* #undef NEED_NETSNMP_DAEMONIZE */

/* pthread_init() required? */
/* #undef NEED_PTHREAD_INIT */

/* use PTHREAD_SCOPE_SYSTEM? */
/* #undef NEED_PTHREAD_SCOPE_SYSTEM */

/* Do we need the qnx adjtime call? */
/* #undef NEED_QNX_ADJTIME */

/* Do we need extra room for SO_RCVBUF? (HPUX < 8) */
/* #undef NEED_RCVBUF_SLOP */

/* Do we need an s_char typedef? */
#define NEED_S_CHAR_TYPEDEF 1

/* Might nlist() values require an extra level of indirection (AIX)? */
/* #undef NLIST_EXTRA_INDIRECTION */

/* does struct nlist use a name union? */
/* #undef NLIST_NAME_UNION */

/* nlist stuff */
#define NLIST_STRUCT 1

/* Should we NOT read /dev/kmem? */
#define NOKMEM 1

/* Should we avoid #warning on option name collisions? */
/* #undef NO_OPTION_NAME_WARNINGS */

/* Is there a problem using PARENB and IGNPAR? */
/* #undef NO_PARENB_IGNPAR */

/* define if you have (or want) no threads */
/* #undef NO_THREADS */

/* Default location of crypto key info */
#define NTP_KEYSDIR "/etc/ntp"

/* Path to sign daemon rendezvous socket */
#define NTP_SIGND_PATH "/var/run/ntp_signd"

/* Do we have ntp_{adj,get}time in libc? */
#define NTP_SYSCALLS_LIBC 1

/* Do we have ntp_{adj,get}time in the kernel? */
/* #undef NTP_SYSCALLS_STD */

/* Do we have support for SHMEM_STATUS? */
#define ONCORE_SHMEM_STATUS 1

/* Use OpenSSL? */
/* #define OPENSSL */

/* Should we open the broadcast socket? */
#define OPEN_BCAST_SOCKET 1

/* need to recreate sockets on changed routing? */
/* #undef OS_MISSES_SPECIFIC_ROUTE_UPDATES */

/* wildcard socket needs REUSEADDR to bind interface addresses */
/* #undef OS_NEEDS_REUSEADDR_FOR_IFADDRBIND */

/* Do we need to override the system's idea of HZ? */
#define OVERRIDE_HZ 1

/* Name of package */
#define PACKAGE "ntp"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "http://bugs.ntp.org./"

/* Define to the full name of this package. */
#define PACKAGE_NAME "ntp"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "ntp 4.2.8p12"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "ntp"

/* Define to the home page for this package. */
#define PACKAGE_URL "http://www.ntp.org./"

/* Define to the version of this package. */
#define PACKAGE_VERSION "4.2.8p12"

/* data dir */
#define PERLLIBDIR "/usr/share/ntp/lib"

/* define to a working POSIX compliant shell */
#define POSIX_SHELL "/bin/sh"

/* PARSE kernel PLL PPS support */
/* #undef PPS_SYNC */

/* Preset a value for 'tick'? */
#define PRESET_TICK 1000000L/hz

/* Preset a value for 'tickadj'? */
#define PRESET_TICKADJ 500/hz

/* Should we not IGNPAR (Linux)? */
/* #undef RAWDCF_NO_IGNPAR */

/* enable thread safety */
#define REENTRANT 1

/* Basic refclock support? */
#define REFCLOCK 1

/* Do we want the ReliantUNIX clock hacks? */
/* #undef RELIANTUNIX_CLOCK */

/* define if sched_yield yields the entire process */
/* #undef REPLACE_BROKEN_YIELD */

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* saveconfig mechanism */
#define SAVECONFIG 1

/* Do we want the SCO clock hacks? */
/* #undef SCO5_CLOCK */

/* The size of `char *', as computed by sizeof. */
#ifdef __LP64__
#define SIZEOF_CHARP 8
#else
#define SIZEOF_CHARP 4
#endif

/* The size of `int', as computed by sizeof. */
#define SIZEOF_INT 4

/* The size of `long', as computed by sizeof. */
#ifdef __LP64__
#define SIZEOF_LONG 8
#else
#define SIZEOF_LONG 4
#endif

/* The size of `long long', as computed by sizeof. */
#define SIZEOF_LONG_LONG 8

/* The size of `pthread_t', as computed by sizeof. */
#define SIZEOF_PTHREAD_T 8

/* The size of `short', as computed by sizeof. */
#define SIZEOF_SHORT 2

/* The size of `signed char', as computed by sizeof. */
#define SIZEOF_SIGNED_CHAR 1

/* The size of `time_t', as computed by sizeof. */
#if defined(__i386__) || defined(__powerpc__)
#define SIZEOF_TIME_T 4
#else
#define SIZEOF_TIME_T 8
#endif

/* Does SIOCGIFCONF return size in the buffer? */
/* #undef SIZE_RETURNED_IN_BUFFER */

/* Slew always? */
/* #undef SLEWALWAYS */

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at runtime.
	STACK_DIRECTION > 0 => grows toward higher addresses
	STACK_DIRECTION < 0 => grows toward lower addresses
	STACK_DIRECTION = 0 => direction of growth unknown */
/* #undef STACK_DIRECTION */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Step, then slew the clock? */
/* #undef STEP_SLEW */

/* Define to 1 if strerror_r returns char *. */
/* #undef STRERROR_R_CHAR_P */

/* canonical system (cpu-vendor-os) of where we should run */
#if defined(__alpha__)
#define STR_SYSTEM "alpha-undermydesk-freebsd"
#elif defined(__sparc64__)
#define STR_SYSTEM "sparc64-undermydesk-freebsd"
#elif defined(__amd64__)
#define STR_SYSTEM "amd64-undermydesk-freebsd"
#elif defined(__powerpc64__)
#define STR_SYSTEM "powerpc64-undermydesk-freebsd"
#elif defined(__powerpc__)
#define STR_SYSTEM "powerpc-undermydesk-freebsd"
#elif defined(__mips64)
#define STR_SYSTEM "mips64-undermydesk-freebsd"
#elif defined(__mips__)
#define STR_SYSTEM "mips-undermydesk-freebsd"
#elif defined(__aarch64__)
#define STR_SYSTEM "arm64-undermydesk-freebsd"
#elif defined(__arm__)
#define STR_SYSTEM "arm-undermydesk-freebsd"
#elif defined(__sparc64__)
#define STR_SYSTEM "sparc64-undermydesk-freebsd"
#elif defined(__sparc__)
#define STR_SYSTEM "sparc-undermydesk-freebsd"
#elif defined(__ia64__)
#define STR_SYSTEM "ia64-undermydesk-freebsd"
#else
#define STR_SYSTEM "i386-undermydesk-freebsd"
#endif

/* Does Xettimeofday take 1 arg? */
/* #undef SYSV_TIMEOFDAY */

/* Do we need to #define _SVID3 when we #include <termios.h>? */
/* #undef TERMIOS_NEEDS__SVID3 */

/* enable thread safety */
#define THREADSAFE 1

/* enable thread safety */
#define THREAD_SAFE 1

/* Is K_TICKADJ_NAME in nanoseconds? */
/* #undef TICKADJ_NANO */

/* Is K_TICK_NAME in nanoseconds? */
/* #undef TICK_NANO */

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* Provide a typedef for uintptr_t? */
#ifndef HAVE_UINTPTR_T
typedef unsigned int	uintptr_t;
#define HAVE_UINTPTR_T 1
#endif

/* What type to use for setsockopt */
#define TYPEOF_IP_MULTICAST_LOOP u_char

/* Do we set process groups with -pid? */
/* #undef UDP_BACKWARDS_SETOWN */

/* Must we have a CTTY for fsetown? */
#define USE_FSETOWNCTTY 1

/* Use OpenSSL's crypto random functions */
/* #define USE_OPENSSL_CRYPTO_RAND 1 */

/* OK to use snprintb()? */
/* #undef USE_SNPRINTB */

/* Can we use SIGPOLL for tty IO? */
/* #undef USE_TTY_SIGPOLL */

/* Can we use SIGPOLL for UDP? */
/* #undef USE_UDP_SIGPOLL */

/* Version number of package */
#define VERSION "4.2.8p12"

/* vsnprintf expands "%m" to strerror(errno) */
#define VSNPRINTF_PERCENT_M 1

/* configure --enable-ipv6 */
#define WANT_IPV6 1

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined(__ARMEB__) || defined(__MIPSEB__) || defined(__powerpc__) || \
    defined(__powerpc64__) || defined(__sparc64__)
#define WORDS_BIGENDIAN 1
#endif

/* routine worker child proc uses to exit. */
#define WORKER_CHILD_EXIT exit

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

/* enable thread safety */
#define _REENTRANT 1

/* enable thread safety */
#define _SGI_MP_SOURCE 1

/* enable thread safety */
#define _THREADSAFE 1

/* enable thread safety */
#define _THREAD_SAFE 1

/* Define to 500 only on HP-UX. */
/* #undef _XOPEN_SOURCE */

/* Are we _special_? */
/* #undef __APPLE_USE_RFC_3542 */

/* Define to 1 if type `char' is unsigned and you are not using gcc.  */
#ifndef __CHAR_UNSIGNED__
/* # undef __CHAR_UNSIGNED__ */
#endif

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif


/* deviant */
/* #undef adjtimex */

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef gid_t */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to the widest signed integer type if <stdint.h> and <inttypes.h> do
   not define. */
/* #undef intmax_t */

/* deviant */
/* #undef ntp_adjtime */

/* deviant */
/* #undef ntp_gettime */

/* Define to `long int' if <sys/types.h> does not define. */
/* #undef off_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

	
	    #if !defined(_KERNEL) && !defined(PARSESTREAM)
	    /*
	     * stdio.h must be included after _GNU_SOURCE is defined
	     * but before #define snprintf rpl_snprintf
	     */
	    # include <stdio.h>	
	    #endif
	

/* Define to rpl_snprintf if the replacement function should be used. */
/* #undef snprintf */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef uid_t */

/* Define to the widest unsigned integer type if <stdint.h> and <inttypes.h>
   do not define. */
/* #undef uintmax_t */

/* Define to the type of an unsigned integer type wide enough to hold a
   pointer, if such a type exists, and if the system does not define it. */
/* #undef uintptr_t */

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */

/* Define to empty if the keyword `volatile' does not work. Warning: valid
   code using `volatile' can become incorrect without. Disable with care. */
/* #undef volatile */

/* Define to rpl_vsnprintf if the replacement function should be used. */
/* #undef vsnprintf */


#ifndef MPINFOU_PREDECLARED
# define MPINFOU_PREDECLARED
typedef union mpinfou {
	struct pdk_mpinfo *pdkptr;
	struct mpinfo *pikptr;
} mpinfou_t;
#endif



	#if !defined(_KERNEL) && !defined(PARSESTREAM)
	# if defined(HW_WANT_RPL_VSNPRINTF)
	#  if defined(__cplusplus)
	extern "C" {
	# endif
	# include <stdarg.h>
	int rpl_vsnprintf(char *, size_t, const char *, va_list);
	# if defined(__cplusplus)
	}
	#  endif
	# endif
	# if defined(HW_WANT_RPL_SNPRINTF)
	#  if defined(__cplusplus)
	extern "C" {
	#  endif
	int rpl_snprintf(char *, size_t, const char *, ...);
	#  if defined(__cplusplus)
	}
	#  endif
	# endif
	#endif	/* !defined(_KERNEL) && !defined(PARSESTREAM) */
	
/*
 * FreeBSD specific: Explicitly specify date/time for reproducible build.
 */
#define	MKREPRO_DATE "Aug 19 2018"
#define	MKREPRO_TIME "01:24:29"
