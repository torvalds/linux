/*
 *  include/asm-s390/unistd.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/unistd.h"
 */

#ifndef _ASM_S390_UNISTD_H_
#define _ASM_S390_UNISTD_H_

/*
 * This file contains the system call numbers.
 */

#define __NR_exit                 1
#define __NR_fork                 2
#define __NR_read                 3
#define __NR_write                4
#define __NR_open                 5
#define __NR_close                6
#define __NR_restart_syscall	  7
#define __NR_creat                8
#define __NR_link                 9
#define __NR_unlink              10
#define __NR_execve              11
#define __NR_chdir               12
#define __NR_time                13
#define __NR_mknod               14
#define __NR_chmod               15
#define __NR_lchown              16
#define __NR_lseek               19
#define __NR_getpid              20
#define __NR_mount               21
#define __NR_umount              22
#define __NR_setuid              23
#define __NR_getuid              24
#define __NR_stime               25
#define __NR_ptrace              26
#define __NR_alarm               27
#define __NR_pause               29
#define __NR_utime               30
#define __NR_access              33
#define __NR_nice                34
#define __NR_sync                36
#define __NR_kill                37
#define __NR_rename              38
#define __NR_mkdir               39
#define __NR_rmdir               40
#define __NR_dup                 41
#define __NR_pipe                42
#define __NR_times               43
#define __NR_brk                 45
#define __NR_setgid              46
#define __NR_getgid              47
#define __NR_signal              48
#define __NR_geteuid             49
#define __NR_getegid             50
#define __NR_acct                51
#define __NR_umount2             52
#define __NR_ioctl               54
#define __NR_fcntl               55
#define __NR_setpgid             57
#define __NR_umask               60
#define __NR_chroot              61
#define __NR_ustat               62
#define __NR_dup2                63
#define __NR_getppid             64
#define __NR_getpgrp             65
#define __NR_setsid              66
#define __NR_sigaction           67
#define __NR_setreuid            70
#define __NR_setregid            71
#define __NR_sigsuspend          72
#define __NR_sigpending          73
#define __NR_sethostname         74
#define __NR_setrlimit           75
#define __NR_getrlimit           76
#define __NR_getrusage           77
#define __NR_gettimeofday        78
#define __NR_settimeofday        79
#define __NR_getgroups           80
#define __NR_setgroups           81
#define __NR_symlink             83
#define __NR_readlink            85
#define __NR_uselib              86
#define __NR_swapon              87
#define __NR_reboot              88
#define __NR_readdir             89
#define __NR_mmap                90
#define __NR_munmap              91
#define __NR_truncate            92
#define __NR_ftruncate           93
#define __NR_fchmod              94
#define __NR_fchown              95
#define __NR_getpriority         96
#define __NR_setpriority         97
#define __NR_statfs              99
#define __NR_fstatfs            100
#define __NR_ioperm             101
#define __NR_socketcall         102
#define __NR_syslog             103
#define __NR_setitimer          104
#define __NR_getitimer          105
#define __NR_stat               106
#define __NR_lstat              107
#define __NR_fstat              108
#define __NR_lookup_dcookie     110
#define __NR_vhangup            111
#define __NR_idle               112
#define __NR_wait4              114
#define __NR_swapoff            115
#define __NR_sysinfo            116
#define __NR_ipc                117
#define __NR_fsync              118
#define __NR_sigreturn          119
#define __NR_clone              120
#define __NR_setdomainname      121
#define __NR_uname              122
#define __NR_adjtimex           124
#define __NR_mprotect           125
#define __NR_sigprocmask        126
#define __NR_create_module      127
#define __NR_init_module        128
#define __NR_delete_module      129
#define __NR_get_kernel_syms    130
#define __NR_quotactl           131
#define __NR_getpgid            132
#define __NR_fchdir             133
#define __NR_bdflush            134
#define __NR_sysfs              135
#define __NR_personality        136
#define __NR_afs_syscall        137 /* Syscall for Andrew File System */
#define __NR_setfsuid           138
#define __NR_setfsgid           139
#define __NR__llseek            140
#define __NR_getdents           141
#define __NR__newselect         142
#define __NR_flock              143
#define __NR_msync              144
#define __NR_readv              145
#define __NR_writev             146
#define __NR_getsid             147
#define __NR_fdatasync          148
#define __NR__sysctl            149
#define __NR_mlock              150
#define __NR_munlock            151
#define __NR_mlockall           152
#define __NR_munlockall         153
#define __NR_sched_setparam             154
#define __NR_sched_getparam             155
#define __NR_sched_setscheduler         156
#define __NR_sched_getscheduler         157
#define __NR_sched_yield                158
#define __NR_sched_get_priority_max     159
#define __NR_sched_get_priority_min     160
#define __NR_sched_rr_get_interval      161
#define __NR_nanosleep          162
#define __NR_mremap             163
#define __NR_setresuid          164
#define __NR_getresuid          165
#define __NR_query_module       167
#define __NR_poll               168
#define __NR_nfsservctl         169
#define __NR_setresgid          170
#define __NR_getresgid          171
#define __NR_prctl              172
#define __NR_rt_sigreturn       173
#define __NR_rt_sigaction       174
#define __NR_rt_sigprocmask     175
#define __NR_rt_sigpending      176
#define __NR_rt_sigtimedwait    177
#define __NR_rt_sigqueueinfo    178
#define __NR_rt_sigsuspend      179
#define __NR_pread64            180
#define __NR_pwrite64           181
#define __NR_chown              182
#define __NR_getcwd             183
#define __NR_capget             184
#define __NR_capset             185
#define __NR_sigaltstack        186
#define __NR_sendfile           187
#define __NR_getpmsg		188
#define __NR_putpmsg		189
#define __NR_vfork		190
#define __NR_ugetrlimit		191	/* SuS compliant getrlimit */
#define __NR_mmap2		192
#define __NR_truncate64		193
#define __NR_ftruncate64	194
#define __NR_stat64		195
#define __NR_lstat64		196
#define __NR_fstat64		197
#define __NR_lchown32		198
#define __NR_getuid32		199
#define __NR_getgid32		200
#define __NR_geteuid32		201
#define __NR_getegid32		202
#define __NR_setreuid32		203
#define __NR_setregid32		204
#define __NR_getgroups32	205
#define __NR_setgroups32	206
#define __NR_fchown32		207
#define __NR_setresuid32	208
#define __NR_getresuid32	209
#define __NR_setresgid32	210
#define __NR_getresgid32	211
#define __NR_chown32		212
#define __NR_setuid32		213
#define __NR_setgid32		214
#define __NR_setfsuid32		215
#define __NR_setfsgid32		216
#define __NR_pivot_root         217
#define __NR_mincore            218
#define __NR_madvise            219
#define __NR_getdents64		220
#define __NR_fcntl64		221
#define __NR_readahead		222
#define __NR_sendfile64		223
#define __NR_setxattr		224
#define __NR_lsetxattr		225
#define __NR_fsetxattr		226
#define __NR_getxattr		227
#define __NR_lgetxattr		228
#define __NR_fgetxattr		229
#define __NR_listxattr		230
#define __NR_llistxattr		231
#define __NR_flistxattr		232
#define __NR_removexattr	233
#define __NR_lremovexattr	234
#define __NR_fremovexattr	235
#define __NR_gettid		236
#define __NR_tkill		237
#define __NR_futex		238
#define __NR_sched_setaffinity	239
#define __NR_sched_getaffinity	240
#define __NR_tgkill		241
/* Number 242 is reserved for tux */
#define __NR_io_setup		243
#define __NR_io_destroy		244
#define __NR_io_getevents	245
#define __NR_io_submit		246
#define __NR_io_cancel		247
#define __NR_exit_group		248
#define __NR_epoll_create	249
#define __NR_epoll_ctl		250
#define __NR_epoll_wait		251
#define __NR_set_tid_address	252
#define __NR_fadvise64		253
#define __NR_timer_create	254
#define __NR_timer_settime	(__NR_timer_create+1)
#define __NR_timer_gettime	(__NR_timer_create+2)
#define __NR_timer_getoverrun	(__NR_timer_create+3)
#define __NR_timer_delete	(__NR_timer_create+4)
#define __NR_clock_settime	(__NR_timer_create+5)
#define __NR_clock_gettime	(__NR_timer_create+6)
#define __NR_clock_getres	(__NR_timer_create+7)
#define __NR_clock_nanosleep	(__NR_timer_create+8)
/* Number 263 is reserved for vserver */
#define __NR_fadvise64_64	264
#define __NR_statfs64		265
#define __NR_fstatfs64		266
#define __NR_remap_file_pages	267
/* Number 268 is reserved for new sys_mbind */
/* Number 269 is reserved for new sys_get_mempolicy */
/* Number 270 is reserved for new sys_set_mempolicy */
#define __NR_mq_open		271
#define __NR_mq_unlink		272
#define __NR_mq_timedsend	273
#define __NR_mq_timedreceive	274
#define __NR_mq_notify		275
#define __NR_mq_getsetattr	276
#define __NR_kexec_load		277
#define __NR_add_key		278
#define __NR_request_key	279
#define __NR_keyctl		280
#define __NR_waitid		281
#define __NR_ioprio_set		282
#define __NR_ioprio_get		283
#define __NR_inotify_init	284
#define __NR_inotify_add_watch	285
#define __NR_inotify_rm_watch	286
/* Number 287 is reserved for new sys_migrate_pages */
#define __NR_openat		288
#define __NR_mkdirat		289
#define __NR_mknodat		290
#define __NR_fchownat		291
#define __NR_futimesat		292
#define __NR_fstatat64		293
#define __NR_unlinkat		294
#define __NR_renameat		295
#define __NR_linkat		296
#define __NR_symlinkat		297
#define __NR_readlinkat		298
#define __NR_fchmodat		299
#define __NR_faccessat		300
#define __NR_pselect6		301
#define __NR_ppoll		302
#define __NR_unshare		303

#define NR_syscalls 304

/* 
 * There are some system calls that are not present on 64 bit, some
 * have a different name although they do the same (e.g. __NR_chown32
 * is __NR_chown on 64 bit).
 */
#ifdef __s390x__
#undef  __NR_time
#undef  __NR_lchown
#undef  __NR_setuid
#undef  __NR_getuid
#undef  __NR_stime
#undef  __NR_setgid
#undef  __NR_getgid
#undef  __NR_geteuid
#undef  __NR_getegid
#undef  __NR_setreuid
#undef  __NR_setregid
#undef  __NR_getrlimit
#undef  __NR_getgroups
#undef  __NR_setgroups
#undef  __NR_fchown
#undef  __NR_ioperm
#undef  __NR_setfsuid
#undef  __NR_setfsgid
#undef  __NR__llseek
#undef  __NR__newselect
#undef  __NR_setresuid
#undef  __NR_getresuid
#undef  __NR_setresgid
#undef  __NR_getresgid
#undef  __NR_chown
#undef  __NR_ugetrlimit
#undef  __NR_mmap2
#undef  __NR_truncate64
#undef  __NR_ftruncate64
#undef  __NR_stat64
#undef  __NR_lstat64
#undef  __NR_fstat64
#undef  __NR_lchown32
#undef  __NR_getuid32
#undef  __NR_getgid32
#undef  __NR_geteuid32
#undef  __NR_getegid32
#undef  __NR_setreuid32
#undef  __NR_setregid32
#undef  __NR_getgroups32
#undef  __NR_setgroups32
#undef  __NR_fchown32
#undef  __NR_setresuid32
#undef  __NR_getresuid32
#undef  __NR_setresgid32
#undef  __NR_getresgid32
#undef  __NR_chown32
#undef  __NR_setuid32
#undef  __NR_setgid32
#undef  __NR_setfsuid32
#undef  __NR_setfsgid32
#undef  __NR_fcntl64
#undef  __NR_sendfile64
#undef  __NR_fadvise64_64
#undef  __NR_fstatat64

#define __NR_select		142
#define __NR_getrlimit		191	/* SuS compliant getrlimit */
#define __NR_lchown  		198
#define __NR_getuid  		199
#define __NR_getgid  		200
#define __NR_geteuid  		201
#define __NR_getegid  		202
#define __NR_setreuid  		203
#define __NR_setregid  		204
#define __NR_getgroups  	205
#define __NR_setgroups  	206
#define __NR_fchown  		207
#define __NR_setresuid  	208
#define __NR_getresuid  	209
#define __NR_setresgid  	210
#define __NR_getresgid  	211
#define __NR_chown  		212
#define __NR_setuid  		213
#define __NR_setgid  		214
#define __NR_setfsuid  		215
#define __NR_setfsgid  		216
#define __NR_newfstatat		293

#endif

/* user-visible error numbers are in the range -1 - -122: see <asm-s390/errno.h> */

#define __syscall_return(type, res)			     \
do {							     \
	if ((unsigned long)(res) >= (unsigned long)(-125)) { \
		errno = -(res);				     \
		res = -1;				     \
	}						     \
	return (type) (res);				     \
} while (0)

#define _svc_clobber "1", "cc", "memory"

#define _syscall0(type,name)				     \
type name(void) {					     \
	register long __svcres asm("2");		     \
	long __res;					     \
	__asm__ __volatile__ (				     \
		"    .if %1 < 256\n"			     \
		"    svc %b1\n"				     \
		"    .else\n"				     \
		"    la  %%r1,%1\n"			     \
		"    svc 0\n"				     \
		"    .endif"				     \
		: "=d" (__svcres)			     \
		: "i" (__NR_##name)			     \
		: _svc_clobber );			     \
	__res = __svcres;				     \
	__syscall_return(type,__res);			     \
}

#define _syscall1(type,name,type1,arg1)			     \
type name(type1 arg1) {					     \
	register type1 __arg1 asm("2") = arg1;		     \
	register long __svcres asm("2");		     \
	long __res;					     \
	__asm__ __volatile__ (				     \
		"    .if %1 < 256\n"			     \
		"    svc %b1\n"				     \
		"    .else\n"				     \
		"    la  %%r1,%1\n"			     \
		"    svc 0\n"				     \
		"    .endif"				     \
		: "=d" (__svcres)			     \
		: "i" (__NR_##name),			     \
		  "0" (__arg1)				     \
		: _svc_clobber );			     \
	__res = __svcres;				     \
	__syscall_return(type,__res);			     \
}

#define _syscall2(type,name,type1,arg1,type2,arg2)	     \
type name(type1 arg1, type2 arg2) {			     \
	register type1 __arg1 asm("2") = arg1;		     \
	register type2 __arg2 asm("3") = arg2;		     \
	register long __svcres asm("2");		     \
	long __res;					     \
	__asm__ __volatile__ (				     \
		"    .if %1 < 256\n"			     \
		"    svc %b1\n"				     \
		"    .else\n"				     \
		"    la %%r1,%1\n"			     \
		"    svc 0\n"				     \
		"    .endif"				     \
		: "=d" (__svcres)			     \
		: "i" (__NR_##name),			     \
		  "0" (__arg1),				     \
		  "d" (__arg2)				     \
		: _svc_clobber );			     \
	__res = __svcres;				     \
	__syscall_return(type,__res);			     \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)\
type name(type1 arg1, type2 arg2, type3 arg3) {		     \
	register type1 __arg1 asm("2") = arg1;		     \
	register type2 __arg2 asm("3") = arg2;		     \
	register type3 __arg3 asm("4") = arg3;		     \
	register long __svcres asm("2");		     \
	long __res;					     \
	__asm__ __volatile__ (				     \
		"    .if %1 < 256\n"			     \
		"    svc %b1\n"				     \
		"    .else\n"				     \
		"    la  %%r1,%1\n"			     \
		"    svc 0\n"				     \
		"    .endif"				     \
		: "=d" (__svcres)			     \
		: "i" (__NR_##name),			     \
		  "0" (__arg1),				     \
		  "d" (__arg2),				     \
		  "d" (__arg3)				     \
		: _svc_clobber );			     \
	__res = __svcres;				     \
	__syscall_return(type,__res);			     \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,\
		  type4,name4)				     \
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) {  \
	register type1 __arg1 asm("2") = arg1;		     \
	register type2 __arg2 asm("3") = arg2;		     \
	register type3 __arg3 asm("4") = arg3;		     \
	register type4 __arg4 asm("5") = arg4;		     \
	register long __svcres asm("2");		     \
	long __res;					     \
	__asm__ __volatile__ (				     \
		"    .if %1 < 256\n"			     \
		"    svc %b1\n"				     \
		"    .else\n"				     \
		"    la  %%r1,%1\n"			     \
		"    svc 0\n"				     \
		"    .endif"				     \
		: "=d" (__svcres)			     \
		: "i" (__NR_##name),			     \
		  "0" (__arg1),				     \
		  "d" (__arg2),				     \
		  "d" (__arg3),				     \
		  "d" (__arg4)				     \
		: _svc_clobber );			     \
	__res = __svcres;				     \
	__syscall_return(type,__res);			     \
}

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,\
		  type4,name4,type5,name5)		     \
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4,    \
	  type5 arg5) {					     \
	register type1 __arg1 asm("2") = arg1;		     \
	register type2 __arg2 asm("3") = arg2;		     \
	register type3 __arg3 asm("4") = arg3;		     \
	register type4 __arg4 asm("5") = arg4;		     \
	register type5 __arg5 asm("6") = arg5;		     \
	register long __svcres asm("2");		     \
	long __res;					     \
	__asm__ __volatile__ (				     \
		"    .if %1 < 256\n"			     \
		"    svc %b1\n"				     \
		"    .else\n"				     \
		"    la  %%r1,%1\n"			     \
		"    svc 0\n"				     \
		"    .endif"				     \
		: "=d" (__svcres)			     \
		: "i" (__NR_##name),			     \
		  "0" (__arg1),				     \
		  "d" (__arg2),				     \
		  "d" (__arg3),				     \
		  "d" (__arg4),				     \
		  "d" (__arg5)				     \
		: _svc_clobber );			     \
	__res = __svcres;				     \
	__syscall_return(type,__res);			     \
}

#ifdef __KERNEL__
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND
# ifndef CONFIG_64BIT
#   define __ARCH_WANT_STAT64
#   define __ARCH_WANT_SYS_TIME
# endif
# ifdef CONFIG_COMPAT
#   define __ARCH_WANT_COMPAT_SYS_TIME
#   define __ARCH_WANT_COMPAT_SYS_RT_SIGSUSPEND
# endif
#endif

#ifdef __KERNEL_SYSCALLS__

#include <linux/config.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/stat.h>
#include <linux/syscalls.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
#define __NR__exit __NR_exit
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall3(int,read,int,fd,char *,buf,off_t,count)
static inline _syscall3(off_t,lseek,int,fd,off_t,offset,int,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall2(long,stat,char *,filename,struct stat *,statbuf)

static inline pid_t waitpid(int pid, int *wait_stat, int flags)
{
	return sys_wait4(pid, wait_stat, flags, NULL);
}
struct mmap_arg_struct;
asmlinkage long sys_mmap2(struct mmap_arg_struct __user *arg);

asmlinkage long sys_execve(struct pt_regs regs);
asmlinkage long sys_clone(struct pt_regs regs);
asmlinkage long sys_fork(struct pt_regs regs);
asmlinkage long sys_vfork(struct pt_regs regs);
asmlinkage long sys_pipe(unsigned long __user *fildes);
struct sigaction;
asmlinkage long sys_rt_sigaction(int sig,
				const struct sigaction __user *act,
				struct sigaction __user *oact,
				size_t sigsetsize);

#endif

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#endif /* _ASM_S390_UNISTD_H_ */
