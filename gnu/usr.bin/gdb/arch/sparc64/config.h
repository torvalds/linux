/* $FreeBSD$ */

/* config.h.  Generated automatically by configure.  */
/* config.in.  Generated automatically from configure.in by autoheader.  */

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

/* Define if the `long double' type works.  */
#define HAVE_LONG_DOUBLE 1

/* Define if you have a working `mmap' system call.  */
#define HAVE_MMAP 1

/* Define if you have <vfork.h>.  */
/* #undef HAVE_VFORK_H */

/* Define as __inline if that's what the C compiler calls it.  */
/* #undef inline */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define to `int' if <sys/types.h> doesn't define.  */
/* #undef pid_t */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define if the `setpgrp' function takes no argument.  */
/* #undef SETPGRP_VOID */

/* Define to `unsigned' if <sys/types.h> doesn't define.  */
/* #undef size_t */

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at run-time.
 STACK_DIRECTION > 0 => grows toward higher addresses
 STACK_DIRECTION < 0 => grows toward lower addresses
 STACK_DIRECTION = 0 => direction of growth unknown
 */
/* #undef STACK_DIRECTION */

/* Define if the `S_IS*' macros in <sys/stat.h> do not work properly.  */
/* #undef STAT_MACROS_BROKEN */

/* Define if you have the ANSI C header files.  */
#define STDC_HEADERS 1

/* Define vfork as fork if vfork does not work.  */
/* #undef vfork */

/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

/* Define if your struct reg has r_fs.  */
/* #undef HAVE_STRUCT_REG_R_FS */

/* Define if your struct stat has st_blocks.  */
#define HAVE_STRUCT_STAT_ST_BLOCKS 1

/* Define if your struct reg has r_gs.  */
/* #undef HAVE_STRUCT_REG_R_GS */

/* Define if <link.h> exists and defines struct link_map which has
   members with an ``l_'' prefix.  (For Solaris, SVR4, and
   SVR4-like systems.) */
#define HAVE_STRUCT_LINK_MAP_WITH_L_MEMBERS 1

/* Define if <link.h> exists and defines struct link_map which has
  members with an ``lm_'' prefix.  (For SunOS.)  */
/* #undef HAVE_STRUCT_LINK_MAP_WITH_LM_MEMBERS */

/* Define if <link.h> exists and defines a struct so_map which has
  members with an ``som_'' prefix.  (Found on older *BSD systems.)  */
/* #undef HAVE_STRUCT_SO_MAP_WITH_SOM_MEMBERS */

/* Define if <sys/link.h> has struct link_map32 */
/* #undef HAVE_STRUCT_LINK_MAP32 */

/* Define if <sys/link.h> has link_map32 (solaris sparc-64 target) */
/* #undef _SYSCALL32 */

/* Define if the prfpregset_t type is broken. */
/* #undef PRFPREGSET_T_BROKEN */

/* Define if you want to use new multi-fd /proc interface
   (replaces HAVE_MULTIPLE_PROC_FDS as well as other macros). */
/* #undef NEW_PROC_API */

/* Define if ioctl argument PIOCSET is available. */
/* #undef HAVE_PROCFS_PIOCSET */

/* Define if the `long long' type works.  */
#define CC_HAS_LONG_LONG 1

/* Define if the "ll" format works to print long long ints. */
#define PRINTF_HAS_LONG_LONG 1

/* Define if the "%Lg" format works to print long doubles. */
#define PRINTF_HAS_LONG_DOUBLE 1

/* Define if the "%Lg" format works to scan long doubles. */
#define SCANF_HAS_LONG_DOUBLE 1

/* Define if using Solaris thread debugging.  */
/* #undef HAVE_THREAD_DB_LIB */

/* Define on a GNU/Linux system to work around problems in sys/procfs.h.  */
/* #undef START_INFERIOR_TRAPS_EXPECTED */
/* #undef sys_quotactl */

/* Define if you have HPUX threads */
/* #undef HAVE_HPUX_THREAD_SUPPORT */

/* Define if <proc_service.h> on solaris uses int instead of
   size_t, and assorted other type changes. */
/* #undef PROC_SERVICE_IS_OLD */

/* Define if the simulator is being linked in.  */
#define WITH_SIM 1

/* Set to true if the save_state_t structure is present */
/* #undef HAVE_STRUCT_SAVE_STATE_T */

/* Set to true if the save_state_t structure has the ss_wide member */
/* #undef HAVE_STRUCT_MEMBER_SS_WIDE */

/* Define if <sys/ptrace.h> defines the PTRACE_GETREGS request.  */
/* #undef HAVE_PTRACE_GETREGS */

/* Define if <sys/ptrace.h> defines the PTRACE_GETFPXREGS request.  */
/* #undef HAVE_PTRACE_GETFPXREGS */

/* Define if <sys/ptrace.h> defines the PT_GETDBREGS request.  */
#define HAVE_PT_GETDBREGS 1

/* Define if <sys/ptrace.h> defines the PT_GETXMMREGS request.  */
/* #undef HAVE_PT_GETXMMREGS */

/* Define if libunwind library is being used.  */
/* #undef HAVE_LIBUNWIND */

/* hostfile */
/* #undef GDB_XM_FILE */

/* targetfile */
#define GDB_TM_FILE config/sparc/tm-fbsd.h

/* nativefile */
#ifndef CROSS_DEBUGGER
#define GDB_NM_FILE config/sparc/nm-fbsd.h
#endif

/* Define to 1 so <sys/proc.h> gets a definition of anon_hdl.  Works
   around a <sys/proc.h> problem on IRIX 5.  */
#ifndef _KMEMUSER
/* #undef _KMEMUSER */
#endif

/* Define if you have the __argz_count function.  */
/* #undef HAVE___ARGZ_COUNT */

/* Define if you have the __argz_next function.  */
/* #undef HAVE___ARGZ_NEXT */

/* Define if you have the __argz_stringify function.  */
/* #undef HAVE___ARGZ_STRINGIFY */

/* Define if you have the _mcleanup function.  */
#define HAVE__MCLEANUP 1

/* Define if you have the canonicalize_file_name function.  */
/* #undef HAVE_CANONICALIZE_FILE_NAME */

/* Define if you have the dcgettext function.  */
/* #undef HAVE_DCGETTEXT */

/* Define if you have the getcwd function.  */
#define HAVE_GETCWD 1

/* Define if you have the getpagesize function.  */
#define HAVE_GETPAGESIZE 1

/* Define if you have the monstartup function.  */
#define HAVE_MONSTARTUP 1

/* Define if you have the munmap function.  */
#define HAVE_MUNMAP 1

/* Define if you have the poll function.  */
#define HAVE_POLL 1

/* Define if you have the pread64 function.  */
/* #undef HAVE_PREAD64 */

/* Define if you have the putenv function.  */
#define HAVE_PUTENV 1

/* Define if you have the realpath function.  */
#define HAVE_REALPATH 1

/* Define if you have the sbrk function.  */
/* #undef HAVE_SBRK */

/* Define if you have the setenv function.  */
#define HAVE_SETENV 1

/* Define if you have the setlocale function.  */
#define HAVE_SETLOCALE 1

/* Define if you have the setpgid function.  */
#define HAVE_SETPGID 1

/* Define if you have the setpgrp function.  */
#define HAVE_SETPGRP 1

/* Define if you have the sigaction function.  */
#define HAVE_SIGACTION 1

/* Define if you have the sigprocmask function.  */
#define HAVE_SIGPROCMASK 1

/* Define if you have the sigsetmask function.  */
#define HAVE_SIGSETMASK 1

/* Define if you have the socketpair function.  */
#define HAVE_SOCKETPAIR 1

/* Define if you have the stpcpy function.  */
#define HAVE_STPCPY 1

/* Define if you have the strcasecmp function.  */
#define HAVE_STRCASECMP 1

/* Define if you have the strchr function.  */
#define HAVE_STRCHR 1

/* Define if you have the syscall function.  */
#define HAVE_SYSCALL 1

/* Define if you have the <argz.h> header file.  */
/* #undef HAVE_ARGZ_H */

/* Define if you have the <ctype.h> header file.  */
#define HAVE_CTYPE_H 1

/* Define if you have the <curses.h> header file.  */
#define HAVE_CURSES_H 1

/* Define if you have the <dirent.h> header file.  */
#define HAVE_DIRENT_H 1

/* Define if you have the <libunwind-ia64.h> header file.  */
/* #undef HAVE_LIBUNWIND_IA64_H */

/* Define if you have the <libunwind.h> header file.  */
/* #undef HAVE_LIBUNWIND_H */

/* Define if you have the <limits.h> header file.  */
#define HAVE_LIMITS_H 1

/* Define if you have the <link.h> header file.  */
#define HAVE_LINK_H 1

/* Define if you have the <locale.h> header file.  */
#define HAVE_LOCALE_H 1

/* Define if you have the <machine/reg.h> header file.  */
#define HAVE_MACHINE_REG_H 1

/* Define if you have the <malloc.h> header file.  */
/* #undef HAVE_MALLOC_H */

/* Define if you have the <memory.h> header file.  */
#define HAVE_MEMORY_H 1

/* Define if you have the <ncurses.h> header file.  */
#define HAVE_NCURSES_H 1

/* Define if you have the <ndir.h> header file.  */
/* #undef HAVE_NDIR_H */

/* Define if you have the <nl_types.h> header file.  */
#define HAVE_NL_TYPES_H 1

/* Define if you have the <nlist.h> header file.  */
#define HAVE_NLIST_H 1

/* Define if you have the <poll.h> header file.  */
#define HAVE_POLL_H 1

/* Define if you have the <proc_service.h> header file.  */
/* #undef HAVE_PROC_SERVICE_H */

/* Define if you have the <ptrace.h> header file.  */
/* #undef HAVE_PTRACE_H */

/* Define if you have the <sgtty.h> header file.  */
#define HAVE_SGTTY_H 1

/* Define if you have the <stddef.h> header file.  */
#define HAVE_STDDEF_H 1

/* Define if you have the <stdint.h> header file.  */
#define HAVE_STDINT_H 1

/* Define if you have the <stdlib.h> header file.  */
#define HAVE_STDLIB_H 1

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H 1

/* Define if you have the <strings.h> header file.  */
#define HAVE_STRINGS_H 1

/* Define if you have the <sys/debugreg.h> header file.  */
/* #undef HAVE_SYS_DEBUGREG_H */

/* Define if you have the <sys/dir.h> header file.  */
/* #undef HAVE_SYS_DIR_H */

/* Define if you have the <sys/fault.h> header file.  */
/* #undef HAVE_SYS_FAULT_H */

/* Define if you have the <sys/file.h> header file.  */
#define HAVE_SYS_FILE_H 1

/* Define if you have the <sys/filio.h> header file.  */
#define HAVE_SYS_FILIO_H 1

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H 1

/* Define if you have the <sys/ndir.h> header file.  */
/* #undef HAVE_SYS_NDIR_H */

/* Define if you have the <sys/param.h> header file.  */
#define HAVE_SYS_PARAM_H 1

/* Define if you have the <sys/poll.h> header file.  */
#define HAVE_SYS_POLL_H 1

/* Define if you have the <sys/proc.h> header file.  */
#define HAVE_SYS_PROC_H 1

/* Define if you have the <sys/procfs.h> header file.  */
#define HAVE_SYS_PROCFS_H 1

/* Define if you have the <sys/ptrace.h> header file.  */
#define HAVE_SYS_PTRACE_H 1

/* Define if you have the <sys/reg.h> header file.  */
/* #undef HAVE_SYS_REG_H */

/* Define if you have the <sys/select.h> header file.  */
#define HAVE_SYS_SELECT_H 1

/* Define if you have the <sys/syscall.h> header file.  */
#define HAVE_SYS_SYSCALL_H 1

/* Define if you have the <sys/user.h> header file.  */
#define HAVE_SYS_USER_H 1

/* Define if you have the <sys/wait.h> header file.  */
#define HAVE_SYS_WAIT_H 1

/* Define if you have the <term.h> header file.  */
#define HAVE_TERM_H 1

/* Define if you have the <termio.h> header file.  */
/* #undef HAVE_TERMIO_H */

/* Define if you have the <termios.h> header file.  */
#define HAVE_TERMIOS_H 1

/* Define if you have the <thread_db.h> header file.  */
/* #undef HAVE_THREAD_DB_H */

/* Define if you have the <time.h> header file.  */
#define HAVE_TIME_H 1

/* Define if you have the <unistd.h> header file.  */
#define HAVE_UNISTD_H 1

/* Define if you have the <values.h> header file.  */
/* #undef HAVE_VALUES_H */

/* Define if you have the <wait.h> header file.  */
/* #undef HAVE_WAIT_H */

/* Define if you have the dl library (-ldl).  */
/* #undef HAVE_LIBDL */

/* Define if you have the m library (-lm).  */
#define HAVE_LIBM 1

/* Define if you have the w library (-lw).  */
/* #undef HAVE_LIBW */

/* Define if you have the stpcpy function */
#define HAVE_STPCPY 1

/* Define if your locale.h file contains LC_MESSAGES. */
#define HAVE_LC_MESSAGES 1

/* Define to 1 if NLS is requested */
/* #undef ENABLE_NLS */

/* Define as 1 if you have gettext and don't want to use GNU gettext. */
/* #undef HAVE_GETTEXT */

/* Name of this package.  */
#define PACKAGE "gdb"

/* Define to BFD's default architecture.  */
#define DEFAULT_BFD_ARCH bfd_sparc_arch

/* Define to BFD's default target vector.  */
#define DEFAULT_BFD_VEC bfd_elf64_sparc_vec

/* Define to 1 if your system has the _etext variable.  */
#define HAVE__ETEXT 1

/* Define to 1 to avoid a clash between <widec.h> and <wchar.h> on
   Solaris 2.[78] when using GCC.  */
/* #undef _MSE_INT_H */

/* Define to 1 if we found this declaration otherwise define to 0. */
#define HAVE_DECL_GETOPT 0

/* Define if sigsetjmp is available.  */
#define HAVE_SIGSETJMP 1

/* Define to 1 if the regex included in libiberty should be used. */
#define USE_INCLUDED_REGEX 1

/* Define to 1 if your system has struct reg in <machine/reg.h>. */
#define HAVE_STRUCT_REG 1

/* Define if <stdint.h> provides the uintptr_t type. */
#define HAVE_UINTPTR_T 1

/* Define if malloc is not declared in system header files. */
/* #undef NEED_DECLARATION_MALLOC */

/* Define if realloc is not declared in system header files. */
/* #undef NEED_DECLARATION_REALLOC */

/* Define if free is not declared in system header files. */
/* #undef NEED_DECLARATION_FREE */

/* Define if strerror is not declared in system header files. */
/* #undef NEED_DECLARATION_STRERROR */

/* Define if strdup is not declared in system header files. */
/* #undef NEED_DECLARATION_STRDUP */

/* Define if strstr is not declared in system header files. */
/* #undef NEED_DECLARATION_STRSTR */

/* Define if canonicalize_file_name is not declared in system header files. */
#define NEED_DECLARATION_CANONICALIZE_FILE_NAME 1

/* Define if <sys/procfs.h> has pstatus_t. */
/* #undef HAVE_PSTATUS_T */

/* Define if <sys/procfs.h> has prrun_t. */
/* #undef HAVE_PRRUN_T */

/* Define if <sys/procfs.h> has gregset_t. */
#define HAVE_GREGSET_T 1

/* Define if <sys/procfs.h> has fpregset_t. */
#define HAVE_FPREGSET_T 1

/* Define if <sys/procfs.h> has prgregset_t. */
#define HAVE_PRGREGSET_T 1

/* Define if <sys/procfs.h> has prfpregset_t. */
#define HAVE_PRFPREGSET_T 1

/* Define if <sys/procfs.h> has prgregset32_t. */
/* #undef HAVE_PRGREGSET32_T */

/* Define if <sys/procfs.h> has prfpregset32_t. */
/* #undef HAVE_PRFPREGSET32_T */

/* Define if <sys/procfs.h> has lwpid_t. */
#define HAVE_LWPID_T 1

/* Define if <sys/procfs.h> has psaddr_t. */
#define HAVE_PSADDR_T 1

/* Define if <sys/procfs.h> has prsysent_t. */
/* #undef HAVE_PRSYSENT_T */

/* Define if <sys/procfs.h> has pr_sigset_t. */
/* #undef HAVE_PR_SIGSET_T */

/* Define if <sys/procfs.h> has pr_sigaction64_t. */
/* #undef HAVE_PR_SIGACTION64_T */

/* Define if <sys/procfs.h> has pr_siginfo64_t. */
/* #undef HAVE_PR_SIGINFO64_T */

/* Define if <thread_db.h> has the TD_NOTALLOC error code. */
/* #undef THREAD_DB_HAS_TD_NOTALLOC */

/* Define if we can use the tkill syscall. */
/* #undef HAVE_TKILL_SYSCALL */

/* Define to the default OS ABI for this configuration. */
/* #undef GDB_OSABI_DEFAULT */

/* Define to be a string naming the default host character set. */
#define GDB_DEFAULT_HOST_CHARSET "ISO-8859-1"

/* Define if you have the iconv() function. */
/* #undef HAVE_ICONV */

/* Define as const if the declaration of iconv() needs const. */
/* #undef ICONV_CONST */

