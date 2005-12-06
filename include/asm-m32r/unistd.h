#ifndef _ASM_M32R_UNISTD_H
#define _ASM_M32R_UNISTD_H

/* $Id$ */

#include <asm/syscall.h>	/* SYSCALL_* */

/*
 * This file contains the system call numbers.
 */

#define __NR_restart_syscall	  0
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
/* 16 is unused */
/* 17 is unused */
/* 18 is unused */
#define __NR_lseek		 19
#define __NR_getpid		 20
#define __NR_mount		 21
#define __NR_umount		 22
/* 23 is unused */
/* 24 is unused */
#define __NR_stime		 25
#define __NR_ptrace		 26
#define __NR_alarm		 27
/* 28 is unused */
#define __NR_pause		 29
#define __NR_utime		 30
/* 31 is unused */
#define __NR_cachectl		 32 /* old #define __NR_gtty		 32*/
#define __NR_access		 33
/* 34 is unused */
/* 35 is unused */
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_rename		 38
#define __NR_mkdir		 39
#define __NR_rmdir		 40
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_times		 43
/* 44 is unused */
#define __NR_brk		 45
/* 46 is unused */
/* 47 is unused (getgid16) */
/* 48 is unused */
/* 49 is unused */
/* 50 is unused */
#define __NR_acct		 51
#define __NR_umount2		 52
/* 53 is unused */
#define __NR_ioctl		 54
/* 55 is unused (fcntl) */
/* 56 is unused */
#define __NR_setpgid		 57
/* 58 is unused */
/* 59 is unused */
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_ustat		 62
#define __NR_dup2		 63
#define __NR_getppid		 64
#define __NR_getpgrp		 65
#define __NR_setsid		 66
/* 67 is unused */
/* 68 is unused*/
/* 69 is unused*/
/* 70 is unused */
/* 71 is unused */
/* 72 is unused */
/* 73 is unused */
#define __NR_sethostname	 74
#define __NR_setrlimit		 75
/* 76 is unused (old getrlimit) */
#define __NR_getrusage		 77
#define __NR_gettimeofday	 78
#define __NR_settimeofday	 79
/* 80 is unused */
/* 81 is unused */
/* 82 is unused */
#define __NR_symlink		 83
/* 84 is unused */
#define __NR_readlink		 85
#define __NR_uselib		 86
#define __NR_swapon		 87
#define __NR_reboot		 88
/* 89 is unused */
/* 90 is unused */
#define __NR_munmap		 91
#define __NR_truncate		 92
#define __NR_ftruncate		 93
#define __NR_fchmod		 94
/* 95 is unused */
#define __NR_getpriority	 96
#define __NR_setpriority	 97
/* 98 is unused */
#define __NR_statfs		 99
#define __NR_fstatfs		100
/* 101 is unused */
#define __NR_socketcall		102
#define __NR_syslog		103
#define __NR_setitimer		104
#define __NR_getitimer		105
#define __NR_stat		106
#define __NR_lstat		107
#define __NR_fstat		108
/* 109 is unused */
/* 110 is unused */
#define __NR_vhangup		111
/* 112 is unused */
/* 113 is unused */
#define __NR_wait4		114
#define __NR_swapoff		115
#define __NR_sysinfo		116
#define __NR_ipc		117
#define __NR_fsync		118
/* 119 is unused */
#define __NR_clone		120
#define __NR_setdomainname	121
#define __NR_uname		122
/* 123 is unused */
#define __NR_adjtimex		124
#define __NR_mprotect		125
/* 126 is unused */
/* 127 is unused */
#define __NR_init_module	128
#define __NR_delete_module	129
/* 130 is unused */
#define __NR_quotactl		131
#define __NR_getpgid		132
#define __NR_fchdir		133
#define __NR_bdflush		134
#define __NR_sysfs		135
#define __NR_personality	136
/* 137 is unused */
/* 138 is unused */
/* 139 is unused */
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
/* 164 is unused */
/* 165 is unused */
#define __NR_tas		166
/* 167 is unused */
#define __NR_poll		168
#define __NR_nfsservctl		169
/* 170 is unused */
/* 171 is unused */
#define __NR_prctl              172
#define __NR_rt_sigreturn	173
#define __NR_rt_sigaction	174
#define __NR_rt_sigprocmask	175
#define __NR_rt_sigpending	176
#define __NR_rt_sigtimedwait	177
#define __NR_rt_sigqueueinfo	178
#define __NR_rt_sigsuspend	179
#define __NR_pread64		180
#define __NR_pwrite64		181
/* 182 is unused */
#define __NR_getcwd		183
#define __NR_capget		184
#define __NR_capset		185
#define __NR_sigaltstack	186
#define __NR_sendfile		187
/* 188 is unused */
/* 189 is unused */
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
#define __NR_pivot_root		217
#define __NR_mincore		218
#define __NR_madvise		219
#define __NR_getdents64		220
#define __NR_fcntl64		221
/* 222 is unused */
/* 223 is unused */
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
#define __NR_fadvise64		250
/* 251 is unused */
#define __NR_exit_group		252
#define __NR_lookup_dcookie	253
#define __NR_epoll_create	254
#define __NR_epoll_ctl		255
#define __NR_epoll_wait		256
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
#define __NR_mq_open		277
#define __NR_mq_unlink		(__NR_mq_open+1)
#define __NR_mq_timedsend	(__NR_mq_open+2)
#define __NR_mq_timedreceive	(__NR_mq_open+3)
#define __NR_mq_notify		(__NR_mq_open+4)
#define __NR_mq_getsetattr	(__NR_mq_open+5)
#define __NR_sys_kexec_load	283
#define __NR_waitid		284

#define NR_syscalls 285

/* user-visible error numbers are in the range -1 - -124: see
 * <asm-m32r/errno.h>
 */

#define __syscall_return(type, res) \
do { \
	if ((unsigned long)(res) >= (unsigned long)(-(124 + 1))) { \
	/* Avoid using "res" which is declared to be in register r0; \
	   errno might expand to a function call and clobber it.  */ \
		int __err = -(res); \
		errno = __err; \
		res = -1; \
	} \
	return (type) (res); \
} while (0)

#define _syscall0(type,name) \
type name(void) \
{ \
register long __scno __asm__ ("r7") = __NR_##name; \
register long __res __asm__("r0"); \
__asm__ __volatile__ (\
	"trap #" SYSCALL_VECTOR \
	: "=r" (__res) \
	: "r" (__scno) \
	: "memory"); \
__syscall_return(type,__res); \
}

#define _syscall1(type,name,type1,arg1) \
type name(type1 arg1) \
{ \
register long __scno __asm__ ("r7") = __NR_##name; \
register long __res __asm__ ("r0") = (long)(arg1); \
__asm__ __volatile__ (\
	"trap #" SYSCALL_VECTOR \
	: "=r" (__res) \
	: "r" (__scno), "0" (__res) \
	: "memory"); \
__syscall_return(type,__res); \
}

#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
register long __scno __asm__ ("r7") = __NR_##name; \
register long __arg2 __asm__ ("r1") = (long)(arg2); \
register long __res __asm__ ("r0") = (long)(arg1); \
__asm__ __volatile__ (\
	"trap #" SYSCALL_VECTOR \
	: "=r" (__res) \
	: "r" (__scno), "0" (__res), "r" (__arg2) \
	: "memory"); \
__syscall_return(type,__res); \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
register long __scno __asm__ ("r7") = __NR_##name; \
register long __arg3 __asm__ ("r2") = (long)(arg3); \
register long __arg2 __asm__ ("r1") = (long)(arg2); \
register long __res __asm__ ("r0") = (long)(arg1); \
__asm__ __volatile__ (\
	"trap #" SYSCALL_VECTOR \
	: "=r" (__res) \
	: "r" (__scno), "0" (__res), "r" (__arg2), \
		"r" (__arg3) \
	: "memory"); \
__syscall_return(type,__res); \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name(type1 arg1,type2 arg2,type3 arg3,type4 arg4) \
{ \
register long __scno __asm__ ("r7") = __NR_##name; \
register long __arg4 __asm__ ("r3") = (long)(arg4); \
register long __arg3 __asm__ ("r2") = (long)(arg3); \
register long __arg2 __asm__ ("r1") = (long)(arg2); \
register long __res __asm__ ("r0") = (long)(arg1); \
__asm__ __volatile__ (\
	"trap #" SYSCALL_VECTOR \
	: "=r" (__res) \
	: "r" (__scno), "0" (__res), "r" (__arg2), \
		"r" (__arg3), "r" (__arg4) \
	: "memory"); \
__syscall_return(type,__res); \
}

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	type5,arg5) \
type name(type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{ \
register long __scno __asm__ ("r7") = __NR_##name; \
register long __arg5 __asm__ ("r4") = (long)(arg5); \
register long __arg4 __asm__ ("r3") = (long)(arg4); \
register long __arg3 __asm__ ("r2") = (long)(arg3); \
register long __arg2 __asm__ ("r1") = (long)(arg2); \
register long __res __asm__ ("r0") = (long)(arg1); \
__asm__ __volatile__ (\
	"trap #" SYSCALL_VECTOR \
	: "=r" (__res) \
	: "r" (__scno), "0" (__res), "r" (__arg2), \
		"r" (__arg3), "r" (__arg4), "r" (__arg5) \
	: "memory"); \
__syscall_return(type,__res); \
}

#ifdef __KERNEL__
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_OLD_GETRLIMIT /*will be unused*/
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_RT_SIGACTION
#endif

#ifdef __KERNEL_SYSCALLS__

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/linkage.h>
#include <asm/ptrace.h>

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
static __inline__ _syscall3(int,execve,const char *,file,char **,argv,char **,envp)

asmlinkage long sys_mmap2(unsigned long addr, unsigned long len,
			  unsigned long prot, unsigned long flags,
			  unsigned long fd, unsigned long pgoff);
asmlinkage int sys_execve(struct pt_regs regs);
asmlinkage int sys_clone(struct pt_regs regs);
asmlinkage int sys_fork(struct pt_regs regs);
asmlinkage int sys_vfork(struct pt_regs regs);
asmlinkage int sys_pipe(unsigned long __user *fildes);
struct sigaction;
asmlinkage long sys_rt_sigaction(int sig,
				 const struct sigaction __user *act,
				 struct sigaction __user *oact,
				 size_t sigsetsize);

#endif /* __KERNEL_SYSCALLS__ */

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#ifndef cond_syscall
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")
#endif

#endif /* _ASM_M32R_UNISTD_H */
