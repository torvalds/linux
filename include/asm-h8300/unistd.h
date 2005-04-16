#ifndef _ASM_H8300_UNISTD_H_
#define _ASM_H8300_UNISTD_H_

/*
 * This file contains the system call numbers.
 */

#define __NR_restart_syscall      0
#define __NR_exit		  1
#define __NR_fork		  2
#define __NR_read		  3
#define __NR_write		  4
#define __NR_open		  5
#define __NR_close		  6
#define __NR_waitpid		  7
#define __NR_creat		  8
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_execve		 11
#define __NR_chdir		 12
#define __NR_time		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_chown		 16
#define __NR_break		 17
#define __NR_oldstat		 18
#define __NR_lseek		 19
#define __NR_getpid		 20
#define __NR_mount		 21
#define __NR_umount		 22
#define __NR_setuid		 23
#define __NR_getuid		 24
#define __NR_stime		 25
#define __NR_ptrace		 26
#define __NR_alarm		 27
#define __NR_oldfstat		 28
#define __NR_pause		 29
#define __NR_utime		 30
#define __NR_stty		 31
#define __NR_gtty		 32
#define __NR_access		 33
#define __NR_nice		 34
#define __NR_ftime		 35
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_rename		 38
#define __NR_mkdir		 39
#define __NR_rmdir		 40
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_times		 43
#define __NR_prof		 44
#define __NR_brk		 45
#define __NR_setgid		 46
#define __NR_getgid		 47
#define __NR_signal		 48
#define __NR_geteuid		 49
#define __NR_getegid		 50
#define __NR_acct		 51
#define __NR_umount2		 52
#define __NR_lock		 53
#define __NR_ioctl		 54
#define __NR_fcntl		 55
#define __NR_mpx		 56
#define __NR_setpgid		 57
#define __NR_ulimit		 58
#define __NR_oldolduname	 59
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_ustat		 62
#define __NR_dup2		 63
#define __NR_getppid		 64
#define __NR_getpgrp		 65
#define __NR_setsid		 66
#define __NR_sigaction		 67
#define __NR_sgetmask		 68
#define __NR_ssetmask		 69
#define __NR_setreuid		 70
#define __NR_setregid		 71
#define __NR_sigsuspend		 72
#define __NR_sigpending		 73
#define __NR_sethostname	 74
#define __NR_setrlimit		 75
#define __NR_getrlimit		 76
#define __NR_getrusage		 77
#define __NR_gettimeofday	 78
#define __NR_settimeofday	 79
#define __NR_getgroups		 80
#define __NR_setgroups		 81
#define __NR_select		 82
#define __NR_symlink		 83
#define __NR_oldlstat		 84
#define __NR_readlink		 85
#define __NR_uselib		 86
#define __NR_swapon		 87
#define __NR_reboot		 88
#define __NR_readdir		 89
#define __NR_mmap		 90
#define __NR_munmap		 91
#define __NR_truncate		 92
#define __NR_ftruncate		 93
#define __NR_fchmod		 94
#define __NR_fchown		 95
#define __NR_getpriority	 96
#define __NR_setpriority	 97
#define __NR_profil		 98
#define __NR_statfs		 99
#define __NR_fstatfs		100
#define __NR_ioperm		101
#define __NR_socketcall		102
#define __NR_syslog		103
#define __NR_setitimer		104
#define __NR_getitimer		105
#define __NR_stat		106
#define __NR_lstat		107
#define __NR_fstat		108
#define __NR_olduname		109
#define __NR_iopl		/* 110 */ not supported
#define __NR_vhangup		111
#define __NR_idle		/* 112 */ Obsolete
#define __NR_vm86		/* 113 */ not supported
#define __NR_wait4		114
#define __NR_swapoff		115
#define __NR_sysinfo		116
#define __NR_ipc		117
#define __NR_fsync		118
#define __NR_sigreturn		119
#define __NR_clone		120
#define __NR_setdomainname	121
#define __NR_uname		122
#define __NR_cacheflush		123
#define __NR_adjtimex		124
#define __NR_mprotect		125
#define __NR_sigprocmask	126
#define __NR_create_module	127
#define __NR_init_module	128
#define __NR_delete_module	129
#define __NR_get_kernel_syms	130
#define __NR_quotactl		131
#define __NR_getpgid		132
#define __NR_fchdir		133
#define __NR_bdflush		134
#define __NR_sysfs		135
#define __NR_personality	136
#define __NR_afs_syscall	137 /* Syscall for Andrew File System */
#define __NR_setfsuid		138
#define __NR_setfsgid		139
#define __NR__llseek		140
#define __NR_getdents		141
#define __NR__newselect		142
#define __NR_flock		143
#define __NR_msync		144
#define __NR_readv		145
#define __NR_writev		146
#define __NR_getsid		147
#define __NR_fdatasync		148
#define __NR__sysctl		149
#define __NR_mlock		150
#define __NR_munlock		151
#define __NR_mlockall		152
#define __NR_munlockall		153
#define __NR_sched_setparam		154
#define __NR_sched_getparam		155
#define __NR_sched_setscheduler		156
#define __NR_sched_getscheduler		157
#define __NR_sched_yield		158
#define __NR_sched_get_priority_max	159
#define __NR_sched_get_priority_min	160
#define __NR_sched_rr_get_interval	161
#define __NR_nanosleep		162
#define __NR_mremap		163
#define __NR_setresuid		164
#define __NR_getresuid		165
#define __NR_getpagesize	166
#define __NR_query_module	167
#define __NR_poll		168
#define __NR_nfsservctl		169
#define __NR_setresgid		170
#define __NR_getresgid		171
#define __NR_prctl		172
#define __NR_rt_sigreturn	173
#define __NR_rt_sigaction	174
#define __NR_rt_sigprocmask	175
#define __NR_rt_sigpending	176
#define __NR_rt_sigtimedwait	177
#define __NR_rt_sigqueueinfo	178
#define __NR_rt_sigsuspend	179
#define __NR_pread64		180
#define __NR_pwrite64		181
#define __NR_lchown		182
#define __NR_getcwd		183
#define __NR_capget		184
#define __NR_capset		185
#define __NR_sigaltstack	186
#define __NR_sendfile		187
#define __NR_getpmsg		188	/* some people actually want streams */
#define __NR_putpmsg		189	/* some people actually want streams */
#define __NR_vfork		190
#define __NR_ugetrlimit		191
#define __NR_mmap2		192
#define __NR_truncate64		193
#define __NR_ftruncate64	194
#define __NR_stat64		195
#define __NR_lstat64		196
#define __NR_fstat64		197
#define __NR_chown32		198
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
#define __NR_lchown32		212
#define __NR_setuid32		213
#define __NR_setgid32		214
#define __NR_setfsuid32		215
#define __NR_setfsgid32		216
#define __NR_pivot_root		217
#define __NR_getdents64		220
#define __NR_fcntl64		221
#define __NR_security		223
#define __NR_gettid		224
#define __NR_readahead		225
#define __NR_setxattr		226
#define __NR_lsetxattr		227
#define __NR_fsetxattr		228
#define __NR_getxattr		229
#define __NR_lgetxattr		230
#define __NR_fgetxattr		231
#define __NR_listxattr		232
#define __NR_llistxattr		233
#define __NR_flistxattr		234
#define __NR_removexattr	235
#define __NR_lremovexattr	236
#define __NR_fremovexattr	237
#define __NR_tkill		238
#define __NR_sendfile64		239
#define __NR_futex		240
#define __NR_sched_setaffinity	241
#define __NR_sched_getaffinity	242
#define __NR_set_thread_area	243
#define __NR_get_thread_area	244
#define __NR_io_setup		245
#define __NR_io_destroy		246
#define __NR_io_getevents	247
#define __NR_io_submit		248
#define __NR_io_cancel		249
#define __NR_alloc_hugepages	250
#define __NR_free_hugepages	251
#define __NR_exit_group		252
#define __NR_lookup_dcookie	253
#define __NR_sys_epoll_create	254
#define __NR_sys_epoll_ctl	255
#define __NR_sys_epoll_wait	256
#define __NR_remap_file_pages	257
#define __NR_set_tid_address	258
#define __NR_timer_create	259
#define __NR_timer_settime	(__NR_timer_create+1)
#define __NR_timer_gettime	(__NR_timer_create+2)
#define __NR_timer_getoverrun	(__NR_timer_create+3)
#define __NR_timer_delete	(__NR_timer_create+4)
#define __NR_clock_settime	(__NR_timer_create+5)
#define __NR_clock_gettime	(__NR_timer_create+6)
#define __NR_clock_getres	(__NR_timer_create+7)
#define __NR_clock_nanosleep	(__NR_timer_create+8)
#define __NR_statfs64		268
#define __NR_fstatfs64		269
#define __NR_tgkill		270
#define __NR_utimes		271
#define __NR_fadvise64_64	272
#define __NR_vserver		273
#define __NR_mbind		274
#define __NR_get_mempolicy	275
#define __NR_set_mempolicy	276
#define __NR_mq_open 		277
#define __NR_mq_unlink		(__NR_mq_open+1)
#define __NR_mq_timedsend	(__NR_mq_open+2)
#define __NR_mq_timedreceive	(__NR_mq_open+3)
#define __NR_mq_notify		(__NR_mq_open+4)
#define __NR_mq_getsetattr	(__NR_mq_open+5)
#define __NR_sys_kexec_load	283
#define __NR_waitid		284
/* #define __NR_sys_setaltroot	285 */
#define __NR_add_key		286
#define __NR_request_key	287
#define __NR_keyctl		288

#define NR_syscalls 289


/* user-visible error numbers are in the range -1 - -122: see
   <asm-m68k/errno.h> */

#define __syscall_return(type, res) \
do { \
	if ((unsigned long)(res) >= (unsigned long)(-125)) { \
	/* avoid using res which is declared to be in register d0; \
	   errno might expand to a function call and clobber it.  */ \
		int __err = -(res); \
		errno = __err; \
		res = -1; \
	} \
	return (type) (res); \
} while (0)

#define _syscall0(type, name)				\
type name(void)						\
{							\
  register long __res __asm__("er0");			\
  __asm__ __volatile__ ("mov.l %1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name)		\
			: "cc", "memory");		\
  __syscall_return(type, __res);			\
}

#define _syscall1(type, name, atype, a)			\
type name(atype a)					\
{							\
  register long __res __asm__("er0");			\
  register long _a __asm__("er1");			\
  _a = (long)a;						\
  __asm__ __volatile__ ("mov.l %1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name),		\
			  "g" (_a)			\
			: "cc", "memory");	 	\
  __syscall_return(type, __res);			\
}

#define _syscall2(type, name, atype, a, btype, b)	\
type name(atype a, btype b)				\
{							\
  register long __res __asm__("er0");			\
  register long _a __asm__("er1");			\
  register long _b __asm__("er2");			\
  _a = (long)a;						\
  _b = (long)b;						\
  __asm__ __volatile__ ("mov.l %1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name),		\
			  "g" (_a),			\
			  "g" (_b)			\
			: "cc", "memory");	 	\
  __syscall_return(type, __res);			\
}

#define _syscall3(type, name, atype, a, btype, b, ctype, c)	\
type name(atype a, btype b, ctype c)			\
{							\
  register long __res __asm__("er0");			\
  register long _a __asm__("er1");			\
  register long _b __asm__("er2");			\
  register long _c __asm__("er3");			\
  _a = (long)a;						\
  _b = (long)b;						\
  _c = (long)c;						\
  __asm__ __volatile__ ("mov.l %1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name),		\
			  "g" (_a),			\
			  "g" (_b),			\
			  "g" (_c)			\
			: "cc", "memory");		\
  __syscall_return(type, __res);			\
}

#define _syscall4(type, name, atype, a, btype, b,	\
                  ctype, c, dtype, d)			\
type name(atype a, btype b, ctype c, dtype d)		\
{							\
  register long __res __asm__("er0");			\
  register long _a __asm__("er1");			\
  register long _b __asm__("er2");			\
  register long _c __asm__("er3");			\
  register long _d __asm__("er4");			\
  _a = (long)a;						\
  _b = (long)b;						\
  _c = (long)c;						\
  _d = (long)d;						\
  __asm__ __volatile__ ("mov.l	%1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name),		\
			  "g" (_a),			\
			  "g" (_b),			\
			  "g" (_c),			\
			  "g" (_d)			\
			: "cc", "memory");		\
  __syscall_return(type, __res);			\
}

#define _syscall5(type, name, atype, a, btype, b,	\
                  ctype, c, dtype, d, etype, e)		\
type name(atype a, btype b, ctype c, dtype d, etype e)	\
{							\
  register long __res __asm__("er0");			\
  register long _a __asm__("er1");			\
  register long _b __asm__("er2");			\
  register long _c __asm__("er3");			\
  register long _d __asm__("er4");			\
  register long _e __asm__("er5");			\
  _a = (long)a;                                       	\
  _b = (long)b;                                       	\
  _c = (long)c;                                       	\
  _d = (long)d;                                       	\
  _e = (long)e;                                       	\
  __asm__ __volatile__ ("mov.l	%1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name),		\
			  "g" (_a),			\
			  "g" (_b),			\
			  "g" (_c),			\
			  "g" (_d),			\
			  "g" (_e)			\
			: "cc", "memory");		\
  __syscall_return(type, __res);			\
}

#define _syscall6(type, name, atype, a, btype, b,	\
                  ctype, c, dtype, d, etype, e, ftype, f)	\
type name(atype a, btype b, ctype c, dtype d, etype e, ftype f)	\
{							\
  register long __res __asm__("er0");			\
  register long _a __asm__("er1");			\
  register long _b __asm__("er2");			\
  register long _c __asm__("er3");			\
  register long _d __asm__("er4");			\
  register long _e __asm__("er5");			\
  register long _f __asm__("er6");			\
  _a = (long)a;                                       	\
  _b = (long)b;                                       	\
  _c = (long)c;                                       	\
  _d = (long)d;                                       	\
  _e = (long)e;                                       	\
  _f = (long)f;                                       	\
  __asm__ __volatile__ ("mov.l	%1,er0\n\t"		\
                        "trapa	#0\n\t"			\
			: "=r" (__res)			\
			: "g" (__NR_##name),		\
			  "g" (_a),			\
			  "g" (_b),			\
			  "g" (_c),			\
			  "g" (_d),			\
			  "g" (_e)			\
			  "g" (_f)			\
			: "cc", "memory");		\
  __syscall_return(type, __res);			\
}

#ifdef __KERNEL__
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_OLD_STAT
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
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
#endif

#ifdef __KERNEL_SYSCALLS__

#include <linux/compiler.h>
#include <linux/types.h>

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
static inline _syscall0(int,pause)
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall3(int,read,int,fd,char *,buf,off_t,count)
static inline _syscall3(off_t,lseek,int,fd,off_t,offset,int,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)
static inline _syscall1(int,delete_module,const char *,name)

static inline pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
}

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff);
asmlinkage int sys_execve(char *name, char **argv, char **envp,
			int dummy, ...);
asmlinkage int sys_pipe(unsigned long *fildes);
asmlinkage int sys_ptrace(long request, long pid, long addr, long data);
struct sigaction;
asmlinkage long sys_rt_sigaction(int sig,
				const struct sigaction __user *act,
				struct sigaction __user *oact,
				size_t sigsetsize);

#endif

/*
 * "Conditional" syscalls
 */
#define cond_syscall(name)						\
  asm (".weak\t_" #name "\n"				\
       ".set\t_" #name ",_sys_ni_syscall");

#endif /* _ASM_H8300_UNISTD_H_ */
