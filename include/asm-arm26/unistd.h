/*
 *  linux/include/asm-arm/unistd.h
 *
 *  Copyright (C) 2001-2003 Russell King
 *  Modified 25/11/04 Ian Molton for arm26.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Please forward _all_ changes to this file to spyro@f2s.com
 * no matter what the change is.  Thanks!
 */
#ifndef __ASM_ARM_UNISTD_H
#define __ASM_ARM_UNISTD_H

#include <linux/linkage.h>

#define __NR_SYSCALL_BASE	0x900000

/*
 * This file contains the system call numbers.
 */

#define __NR_restart_syscall		(__NR_SYSCALL_BASE+  0)
#define __NR_exit			(__NR_SYSCALL_BASE+  1)
#define __NR_fork			(__NR_SYSCALL_BASE+  2)
#define __NR_read			(__NR_SYSCALL_BASE+  3)
#define __NR_write			(__NR_SYSCALL_BASE+  4)
#define __NR_open			(__NR_SYSCALL_BASE+  5)
#define __NR_close			(__NR_SYSCALL_BASE+  6)
					/* 7 was sys_waitpid */
#define __NR_creat			(__NR_SYSCALL_BASE+  8)
#define __NR_link			(__NR_SYSCALL_BASE+  9)
#define __NR_unlink			(__NR_SYSCALL_BASE+ 10)
#define __NR_execve			(__NR_SYSCALL_BASE+ 11)
#define __NR_chdir			(__NR_SYSCALL_BASE+ 12)
#define __NR_time			(__NR_SYSCALL_BASE+ 13)
#define __NR_mknod			(__NR_SYSCALL_BASE+ 14)
#define __NR_chmod			(__NR_SYSCALL_BASE+ 15)
#define __NR_lchown			(__NR_SYSCALL_BASE+ 16)
					/* 17 was sys_break */
					/* 18 was sys_stat */
#define __NR_lseek			(__NR_SYSCALL_BASE+ 19)
#define __NR_getpid			(__NR_SYSCALL_BASE+ 20)
#define __NR_mount			(__NR_SYSCALL_BASE+ 21)
#define __NR_umount			(__NR_SYSCALL_BASE+ 22)
#define __NR_setuid			(__NR_SYSCALL_BASE+ 23)
#define __NR_getuid			(__NR_SYSCALL_BASE+ 24)
#define __NR_stime			(__NR_SYSCALL_BASE+ 25)
#define __NR_ptrace			(__NR_SYSCALL_BASE+ 26)
#define __NR_alarm			(__NR_SYSCALL_BASE+ 27)
					/* 28 was sys_fstat */
#define __NR_pause			(__NR_SYSCALL_BASE+ 29)
#define __NR_utime			(__NR_SYSCALL_BASE+ 30)
					/* 31 was sys_stty */
					/* 32 was sys_gtty */
#define __NR_access			(__NR_SYSCALL_BASE+ 33)
#define __NR_nice			(__NR_SYSCALL_BASE+ 34)
					/* 35 was sys_ftime */
#define __NR_sync			(__NR_SYSCALL_BASE+ 36)
#define __NR_kill			(__NR_SYSCALL_BASE+ 37)
#define __NR_rename			(__NR_SYSCALL_BASE+ 38)
#define __NR_mkdir			(__NR_SYSCALL_BASE+ 39)
#define __NR_rmdir			(__NR_SYSCALL_BASE+ 40)
#define __NR_dup			(__NR_SYSCALL_BASE+ 41)
#define __NR_pipe			(__NR_SYSCALL_BASE+ 42)
#define __NR_times			(__NR_SYSCALL_BASE+ 43)
					/* 44 was sys_prof */
#define __NR_brk			(__NR_SYSCALL_BASE+ 45)
#define __NR_setgid			(__NR_SYSCALL_BASE+ 46)
#define __NR_getgid			(__NR_SYSCALL_BASE+ 47)
					/* 48 was sys_signal */
#define __NR_geteuid			(__NR_SYSCALL_BASE+ 49)
#define __NR_getegid			(__NR_SYSCALL_BASE+ 50)
#define __NR_acct			(__NR_SYSCALL_BASE+ 51)
#define __NR_umount2			(__NR_SYSCALL_BASE+ 52)
					/* 53 was sys_lock */
#define __NR_ioctl			(__NR_SYSCALL_BASE+ 54)
#define __NR_fcntl			(__NR_SYSCALL_BASE+ 55)
					/* 56 was sys_mpx */
#define __NR_setpgid			(__NR_SYSCALL_BASE+ 57)
					/* 58 was sys_ulimit */
					/* 59 was sys_olduname */
#define __NR_umask			(__NR_SYSCALL_BASE+ 60)
#define __NR_chroot			(__NR_SYSCALL_BASE+ 61)
#define __NR_ustat			(__NR_SYSCALL_BASE+ 62)
#define __NR_dup2			(__NR_SYSCALL_BASE+ 63)
#define __NR_getppid			(__NR_SYSCALL_BASE+ 64)
#define __NR_getpgrp			(__NR_SYSCALL_BASE+ 65)
#define __NR_setsid			(__NR_SYSCALL_BASE+ 66)
#define __NR_sigaction			(__NR_SYSCALL_BASE+ 67)
					/* 68 was sys_sgetmask */
					/* 69 was sys_ssetmask */
#define __NR_setreuid			(__NR_SYSCALL_BASE+ 70)
#define __NR_setregid			(__NR_SYSCALL_BASE+ 71)
#define __NR_sigsuspend			(__NR_SYSCALL_BASE+ 72)
#define __NR_sigpending			(__NR_SYSCALL_BASE+ 73)
#define __NR_sethostname		(__NR_SYSCALL_BASE+ 74)
#define __NR_setrlimit			(__NR_SYSCALL_BASE+ 75)
#define __NR_getrlimit			(__NR_SYSCALL_BASE+ 76)	/* Back compat 2GB limited rlimit */
#define __NR_getrusage			(__NR_SYSCALL_BASE+ 77)
#define __NR_gettimeofday		(__NR_SYSCALL_BASE+ 78)
#define __NR_settimeofday		(__NR_SYSCALL_BASE+ 79)
#define __NR_getgroups			(__NR_SYSCALL_BASE+ 80)
#define __NR_setgroups			(__NR_SYSCALL_BASE+ 81)
#define __NR_select			(__NR_SYSCALL_BASE+ 82)
#define __NR_symlink			(__NR_SYSCALL_BASE+ 83)
					/* 84 was sys_lstat */
#define __NR_readlink			(__NR_SYSCALL_BASE+ 85)
#define __NR_uselib			(__NR_SYSCALL_BASE+ 86)
#define __NR_swapon			(__NR_SYSCALL_BASE+ 87)
#define __NR_reboot			(__NR_SYSCALL_BASE+ 88)
#define __NR_readdir			(__NR_SYSCALL_BASE+ 89)
#define __NR_mmap			(__NR_SYSCALL_BASE+ 90)
#define __NR_munmap			(__NR_SYSCALL_BASE+ 91)
#define __NR_truncate			(__NR_SYSCALL_BASE+ 92)
#define __NR_ftruncate			(__NR_SYSCALL_BASE+ 93)
#define __NR_fchmod			(__NR_SYSCALL_BASE+ 94)
#define __NR_fchown			(__NR_SYSCALL_BASE+ 95)
#define __NR_getpriority		(__NR_SYSCALL_BASE+ 96)
#define __NR_setpriority		(__NR_SYSCALL_BASE+ 97)
					/* 98 was sys_profil */
#define __NR_statfs			(__NR_SYSCALL_BASE+ 99)
#define __NR_fstatfs			(__NR_SYSCALL_BASE+100)
					/* 101 was sys_ioperm */
#define __NR_socketcall			(__NR_SYSCALL_BASE+102)
#define __NR_syslog			(__NR_SYSCALL_BASE+103)
#define __NR_setitimer			(__NR_SYSCALL_BASE+104)
#define __NR_getitimer			(__NR_SYSCALL_BASE+105)
#define __NR_stat			(__NR_SYSCALL_BASE+106)
#define __NR_lstat			(__NR_SYSCALL_BASE+107)
#define __NR_fstat			(__NR_SYSCALL_BASE+108)
					/* 109 was sys_uname */
					/* 110 was sys_iopl */
#define __NR_vhangup			(__NR_SYSCALL_BASE+111)
					/* 112 was sys_idle */
#define __NR_syscall			(__NR_SYSCALL_BASE+113) /* syscall to call a syscall! */
#define __NR_wait4			(__NR_SYSCALL_BASE+114)
#define __NR_swapoff			(__NR_SYSCALL_BASE+115)
#define __NR_sysinfo			(__NR_SYSCALL_BASE+116)
#define __NR_ipc			(__NR_SYSCALL_BASE+117)
#define __NR_fsync			(__NR_SYSCALL_BASE+118)
#define __NR_sigreturn			(__NR_SYSCALL_BASE+119)
#define __NR_clone			(__NR_SYSCALL_BASE+120)
#define __NR_setdomainname		(__NR_SYSCALL_BASE+121)
#define __NR_uname			(__NR_SYSCALL_BASE+122)
					/* 123 was sys_modify_ldt */
#define __NR_adjtimex			(__NR_SYSCALL_BASE+124)
#define __NR_mprotect			(__NR_SYSCALL_BASE+125)
#define __NR_sigprocmask		(__NR_SYSCALL_BASE+126)
					/* 127 was sys_create_module */
#define __NR_init_module		(__NR_SYSCALL_BASE+128)
#define __NR_delete_module		(__NR_SYSCALL_BASE+129)
					/* 130 was sys_get_kernel_syms */
#define __NR_quotactl			(__NR_SYSCALL_BASE+131)
#define __NR_getpgid			(__NR_SYSCALL_BASE+132)
#define __NR_fchdir			(__NR_SYSCALL_BASE+133)
#define __NR_bdflush			(__NR_SYSCALL_BASE+134)
#define __NR_sysfs			(__NR_SYSCALL_BASE+135)
#define __NR_personality		(__NR_SYSCALL_BASE+136)
					/* 137 was sys_afs_syscall */
#define __NR_setfsuid			(__NR_SYSCALL_BASE+138)
#define __NR_setfsgid			(__NR_SYSCALL_BASE+139)
#define __NR__llseek			(__NR_SYSCALL_BASE+140)
#define __NR_getdents			(__NR_SYSCALL_BASE+141)
#define __NR__newselect			(__NR_SYSCALL_BASE+142)
#define __NR_flock			(__NR_SYSCALL_BASE+143)
#define __NR_msync			(__NR_SYSCALL_BASE+144)
#define __NR_readv			(__NR_SYSCALL_BASE+145)
#define __NR_writev			(__NR_SYSCALL_BASE+146)
#define __NR_getsid			(__NR_SYSCALL_BASE+147)
#define __NR_fdatasync			(__NR_SYSCALL_BASE+148)
#define __NR__sysctl			(__NR_SYSCALL_BASE+149)
#define __NR_mlock			(__NR_SYSCALL_BASE+150)
#define __NR_munlock			(__NR_SYSCALL_BASE+151)
#define __NR_mlockall			(__NR_SYSCALL_BASE+152)
#define __NR_munlockall			(__NR_SYSCALL_BASE+153)
#define __NR_sched_setparam		(__NR_SYSCALL_BASE+154)
#define __NR_sched_getparam		(__NR_SYSCALL_BASE+155)
#define __NR_sched_setscheduler		(__NR_SYSCALL_BASE+156)
#define __NR_sched_getscheduler		(__NR_SYSCALL_BASE+157)
#define __NR_sched_yield		(__NR_SYSCALL_BASE+158)
#define __NR_sched_get_priority_max	(__NR_SYSCALL_BASE+159)
#define __NR_sched_get_priority_min	(__NR_SYSCALL_BASE+160)
#define __NR_sched_rr_get_interval	(__NR_SYSCALL_BASE+161)
#define __NR_nanosleep			(__NR_SYSCALL_BASE+162)
#define __NR_mremap			(__NR_SYSCALL_BASE+163)
#define __NR_setresuid			(__NR_SYSCALL_BASE+164)
#define __NR_getresuid			(__NR_SYSCALL_BASE+165)
					/* 166 was sys_vm86 */
					/* 167 was sys_query_module */
#define __NR_poll			(__NR_SYSCALL_BASE+168)
#define __NR_nfsservctl			(__NR_SYSCALL_BASE+169)
#define __NR_setresgid			(__NR_SYSCALL_BASE+170)
#define __NR_getresgid			(__NR_SYSCALL_BASE+171)
#define __NR_prctl			(__NR_SYSCALL_BASE+172)
#define __NR_rt_sigreturn		(__NR_SYSCALL_BASE+173)
#define __NR_rt_sigaction		(__NR_SYSCALL_BASE+174)
#define __NR_rt_sigprocmask		(__NR_SYSCALL_BASE+175)
#define __NR_rt_sigpending		(__NR_SYSCALL_BASE+176)
#define __NR_rt_sigtimedwait		(__NR_SYSCALL_BASE+177)
#define __NR_rt_sigqueueinfo		(__NR_SYSCALL_BASE+178)
#define __NR_rt_sigsuspend		(__NR_SYSCALL_BASE+179)
#define __NR_pread64			(__NR_SYSCALL_BASE+180)
#define __NR_pwrite64			(__NR_SYSCALL_BASE+181)
#define __NR_chown			(__NR_SYSCALL_BASE+182)
#define __NR_getcwd			(__NR_SYSCALL_BASE+183)
#define __NR_capget			(__NR_SYSCALL_BASE+184)
#define __NR_capset			(__NR_SYSCALL_BASE+185)
#define __NR_sigaltstack		(__NR_SYSCALL_BASE+186)
#define __NR_sendfile			(__NR_SYSCALL_BASE+187)
					/* 188 reserved */
					/* 189 reserved */
#define __NR_vfork			(__NR_SYSCALL_BASE+190)
#define __NR_ugetrlimit			(__NR_SYSCALL_BASE+191)	/* SuS compliant getrlimit */
#define __NR_mmap2			(__NR_SYSCALL_BASE+192)
#define __NR_truncate64			(__NR_SYSCALL_BASE+193)
#define __NR_ftruncate64		(__NR_SYSCALL_BASE+194)
#define __NR_stat64			(__NR_SYSCALL_BASE+195)
#define __NR_lstat64			(__NR_SYSCALL_BASE+196)
#define __NR_fstat64			(__NR_SYSCALL_BASE+197)
#define __NR_lchown32			(__NR_SYSCALL_BASE+198)
#define __NR_getuid32			(__NR_SYSCALL_BASE+199)
#define __NR_getgid32			(__NR_SYSCALL_BASE+200)
#define __NR_geteuid32			(__NR_SYSCALL_BASE+201)
#define __NR_getegid32			(__NR_SYSCALL_BASE+202)
#define __NR_setreuid32			(__NR_SYSCALL_BASE+203)
#define __NR_setregid32			(__NR_SYSCALL_BASE+204)
#define __NR_getgroups32		(__NR_SYSCALL_BASE+205)
#define __NR_setgroups32		(__NR_SYSCALL_BASE+206)
#define __NR_fchown32			(__NR_SYSCALL_BASE+207)
#define __NR_setresuid32		(__NR_SYSCALL_BASE+208)
#define __NR_getresuid32		(__NR_SYSCALL_BASE+209)
#define __NR_setresgid32		(__NR_SYSCALL_BASE+210)
#define __NR_getresgid32		(__NR_SYSCALL_BASE+211)
#define __NR_chown32			(__NR_SYSCALL_BASE+212)
#define __NR_setuid32			(__NR_SYSCALL_BASE+213)
#define __NR_setgid32			(__NR_SYSCALL_BASE+214)
#define __NR_setfsuid32			(__NR_SYSCALL_BASE+215)
#define __NR_setfsgid32			(__NR_SYSCALL_BASE+216)
#define __NR_getdents64			(__NR_SYSCALL_BASE+217)
#define __NR_pivot_root			(__NR_SYSCALL_BASE+218)
#define __NR_mincore			(__NR_SYSCALL_BASE+219)
#define __NR_madvise			(__NR_SYSCALL_BASE+220)
#define __NR_fcntl64			(__NR_SYSCALL_BASE+221)
					/* 222 for tux */
					/* 223 is unused */
#define __NR_gettid			(__NR_SYSCALL_BASE+224)
#define __NR_readahead			(__NR_SYSCALL_BASE+225)
#define __NR_setxattr			(__NR_SYSCALL_BASE+226)
#define __NR_lsetxattr			(__NR_SYSCALL_BASE+227)
#define __NR_fsetxattr			(__NR_SYSCALL_BASE+228)
#define __NR_getxattr			(__NR_SYSCALL_BASE+229)
#define __NR_lgetxattr			(__NR_SYSCALL_BASE+230)
#define __NR_fgetxattr			(__NR_SYSCALL_BASE+231)
#define __NR_listxattr			(__NR_SYSCALL_BASE+232)
#define __NR_llistxattr			(__NR_SYSCALL_BASE+233)
#define __NR_flistxattr			(__NR_SYSCALL_BASE+234)
#define __NR_removexattr		(__NR_SYSCALL_BASE+235)
#define __NR_lremovexattr		(__NR_SYSCALL_BASE+236)
#define __NR_fremovexattr		(__NR_SYSCALL_BASE+237)
#define __NR_tkill			(__NR_SYSCALL_BASE+238)
#define __NR_sendfile64			(__NR_SYSCALL_BASE+239)
#define __NR_futex			(__NR_SYSCALL_BASE+240)
#define __NR_sched_setaffinity		(__NR_SYSCALL_BASE+241)
#define __NR_sched_getaffinity		(__NR_SYSCALL_BASE+242)
#define __NR_io_setup			(__NR_SYSCALL_BASE+243)
#define __NR_io_destroy			(__NR_SYSCALL_BASE+244)
#define __NR_io_getevents		(__NR_SYSCALL_BASE+245)
#define __NR_io_submit			(__NR_SYSCALL_BASE+246)
#define __NR_io_cancel			(__NR_SYSCALL_BASE+247)
#define __NR_exit_group			(__NR_SYSCALL_BASE+248)
#define __NR_lookup_dcookie		(__NR_SYSCALL_BASE+249)
#define __NR_epoll_create		(__NR_SYSCALL_BASE+250)
#define __NR_epoll_ctl			(__NR_SYSCALL_BASE+251)
#define __NR_epoll_wait			(__NR_SYSCALL_BASE+252)
#define __NR_remap_file_pages		(__NR_SYSCALL_BASE+253)
					/* 254 for set_thread_area */
					/* 255 for get_thread_area */
					/* 256 for set_tid_address */
#define __NR_timer_create		(__NR_SYSCALL_BASE+257)
#define __NR_timer_settime		(__NR_SYSCALL_BASE+258)
#define __NR_timer_gettime		(__NR_SYSCALL_BASE+259)
#define __NR_timer_getoverrun		(__NR_SYSCALL_BASE+260)
#define __NR_timer_delete		(__NR_SYSCALL_BASE+261)
#define __NR_clock_settime		(__NR_SYSCALL_BASE+262)
#define __NR_clock_gettime		(__NR_SYSCALL_BASE+263)
#define __NR_clock_getres		(__NR_SYSCALL_BASE+264)
#define __NR_clock_nanosleep		(__NR_SYSCALL_BASE+265)
#define __NR_statfs64			(__NR_SYSCALL_BASE+266)
#define __NR_fstatfs64			(__NR_SYSCALL_BASE+267)
#define __NR_tgkill			(__NR_SYSCALL_BASE+268)
#define __NR_utimes			(__NR_SYSCALL_BASE+269)
#define __NR_fadvise64_64		(__NR_SYSCALL_BASE+270)
#define __NR_pciconfig_iobase		(__NR_SYSCALL_BASE+271)
#define __NR_pciconfig_read		(__NR_SYSCALL_BASE+272)
#define __NR_pciconfig_write		(__NR_SYSCALL_BASE+273)
#define __NR_mq_open			(__NR_SYSCALL_BASE+274)
#define __NR_mq_unlink			(__NR_SYSCALL_BASE+275)
#define __NR_mq_timedsend		(__NR_SYSCALL_BASE+276)
#define __NR_mq_timedreceive		(__NR_SYSCALL_BASE+277)
#define __NR_mq_notify			(__NR_SYSCALL_BASE+278)
#define __NR_mq_getsetattr		(__NR_SYSCALL_BASE+279)
#define __NR_waitid			(__NR_SYSCALL_BASE+280)

/*
 * The following SWIs are ARM private. FIXME - make appropriate for arm26
 */
#define __ARM_NR_BASE			(__NR_SYSCALL_BASE+0x0f0000)
#define __ARM_NR_breakpoint		(__ARM_NR_BASE+1)
#define __ARM_NR_cacheflush		(__ARM_NR_BASE+2)
#define __ARM_NR_usr26			(__ARM_NR_BASE+3)

#define __sys2(x) #x
#define __sys1(x) __sys2(x)

#ifndef __syscall
#define __syscall(name) "swi\t" __sys1(__NR_##name) ""
#endif

#define __syscall_return(type, res)					\
do {									\
	if ((unsigned long)(res) >= (unsigned long)(-125)) {		\
		errno = -(res);						\
		res = -1;						\
	}								\
	return (type) (res);						\
} while (0)

#define _syscall0(type,name)						\
type name(void) {							\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	:								\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}

#define _syscall1(type,name,type1,arg1) 				\
type name(type1 arg1) { 						\
  register long __r0 __asm__("r0") = (long)arg1;			\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	: "r" (__r0)							\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}

#define _syscall2(type,name,type1,arg1,type2,arg2)			\
type name(type1 arg1,type2 arg2) {					\
  register long __r0 __asm__("r0") = (long)arg1;			\
  register long __r1 __asm__("r1") = (long)arg2;			\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	: "r" (__r0),"r" (__r1) 					\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}


#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)		\
type name(type1 arg1,type2 arg2,type3 arg3) {				\
  register long __r0 __asm__("r0") = (long)arg1;			\
  register long __r1 __asm__("r1") = (long)arg2;			\
  register long __r2 __asm__("r2") = (long)arg3;			\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	: "r" (__r0),"r" (__r1),"r" (__r2)				\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}


#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4)\
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) {		\
  register long __r0 __asm__("r0") = (long)arg1;			\
  register long __r1 __asm__("r1") = (long)arg2;			\
  register long __r2 __asm__("r2") = (long)arg3;			\
  register long __r3 __asm__("r3") = (long)arg4;			\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	: "r" (__r0),"r" (__r1),"r" (__r2),"r" (__r3)			\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}
  

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5)	\
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5) {	\
  register long __r0 __asm__("r0") = (long)arg1;			\
  register long __r1 __asm__("r1") = (long)arg2;			\
  register long __r2 __asm__("r2") = (long)arg3;			\
  register long __r3 __asm__("r3") = (long)arg4;			\
  register long __r4 __asm__("r4") = (long)arg5;			\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	: "r" (__r0),"r" (__r1),"r" (__r2),"r" (__r3),"r" (__r4)	\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}

#define _syscall6(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5,type6,arg6)	\
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5, type6 arg6) {	\
  register long __r0 __asm__("r0") = (long)arg1;			\
  register long __r1 __asm__("r1") = (long)arg2;			\
  register long __r2 __asm__("r2") = (long)arg3;			\
  register long __r3 __asm__("r3") = (long)arg4;			\
  register long __r4 __asm__("r4") = (long)arg5;			\
  register long __r5 __asm__("r5") = (long)arg6;			\
  register long __res_r0 __asm__("r0");					\
  long __res;								\
  __asm__ __volatile__ (						\
  __syscall(name)							\
	: "=r" (__res_r0)						\
	: "r" (__r0),"r" (__r1),"r" (__r2),"r" (__r3), "r" (__r4),"r" (__r5)		\
	: "lr");							\
  __res = __res_r0;							\
  __syscall_return(type,__res);						\
}

#ifdef __KERNEL__
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_TIME
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
#endif

#ifdef __KERNEL_SYSCALLS__

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/syscalls.h>

extern long execve(const char *file, char **argv, char **envp);

struct pt_regs;
asmlinkage int sys_execve(char *filenamei, char **argv, char **envp,
			struct pt_regs *regs);
asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
			struct pt_regs *regs);
asmlinkage int sys_fork(struct pt_regs *regs);
asmlinkage int sys_vfork(struct pt_regs *regs);
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
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall")

#endif /* __ASM_ARM_UNISTD_H */
