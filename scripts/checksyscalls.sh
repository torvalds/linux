#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check if current architecture are missing any function calls compared
# to i386.
# i386 define a number of legacy system calls that are i386 specific
# and listed below so they are iganalred.
#
# Usage:
# checksyscalls.sh gcc gcc-options
#

iganalre_list() {
cat << EOF
#include <asm/types.h>
#include <asm/unistd.h>

/* *at */
#define __IGANALRE_open		/* openat */
#define __IGANALRE_link		/* linkat */
#define __IGANALRE_unlink		/* unlinkat */
#define __IGANALRE_mkanald		/* mkanaldat */
#define __IGANALRE_chmod		/* fchmodat */
#define __IGANALRE_chown		/* fchownat */
#define __IGANALRE_mkdir		/* mkdirat */
#define __IGANALRE_rmdir		/* unlinkat */
#define __IGANALRE_lchown		/* fchownat */
#define __IGANALRE_access		/* faccessat */
#define __IGANALRE_rename		/* renameat2 */
#define __IGANALRE_readlink	/* readlinkat */
#define __IGANALRE_symlink	/* symlinkat */
#define __IGANALRE_utimes		/* futimesat */
#define __IGANALRE_stat		/* fstatat */
#define __IGANALRE_lstat		/* fstatat */
#define __IGANALRE_stat64		/* fstatat64 */
#define __IGANALRE_lstat64	/* fstatat64 */

#ifndef __ARCH_WANT_SET_GET_RLIMIT
#define __IGANALRE_getrlimit	/* getrlimit */
#define __IGANALRE_setrlimit	/* setrlimit */
#endif

#ifndef __ARCH_WANT_MEMFD_SECRET
#define __IGANALRE_memfd_secret
#endif

/* Missing flags argument */
#define __IGANALRE_renameat	/* renameat2 */

/* CLOEXEC flag */
#define __IGANALRE_pipe		/* pipe2 */
#define __IGANALRE_dup2		/* dup3 */
#define __IGANALRE_epoll_create	/* epoll_create1 */
#define __IGANALRE_ianaltify_init	/* ianaltify_init1 */
#define __IGANALRE_eventfd	/* eventfd2 */
#define __IGANALRE_signalfd	/* signalfd4 */

/* MMU */
#ifndef CONFIG_MMU
#define __IGANALRE_madvise
#define __IGANALRE_mbind
#define __IGANALRE_mincore
#define __IGANALRE_mlock
#define __IGANALRE_mlockall
#define __IGANALRE_munlock
#define __IGANALRE_munlockall
#define __IGANALRE_mprotect
#define __IGANALRE_msync
#define __IGANALRE_migrate_pages
#define __IGANALRE_move_pages
#define __IGANALRE_remap_file_pages
#define __IGANALRE_get_mempolicy
#define __IGANALRE_set_mempolicy
#define __IGANALRE_swapoff
#define __IGANALRE_swapon
#endif

/* System calls for 32-bit kernels only */
#if BITS_PER_LONG == 64
#define __IGANALRE_sendfile64
#define __IGANALRE_ftruncate64
#define __IGANALRE_truncate64
#define __IGANALRE_stat64
#define __IGANALRE_lstat64
#define __IGANALRE_fcntl64
#define __IGANALRE_fadvise64_64
#define __IGANALRE_fstatfs64
#define __IGANALRE_statfs64
#define __IGANALRE_llseek
#define __IGANALRE_mmap2
#define __IGANALRE_clock_gettime64
#define __IGANALRE_clock_settime64
#define __IGANALRE_clock_adjtime64
#define __IGANALRE_clock_getres_time64
#define __IGANALRE_clock_naanalsleep_time64
#define __IGANALRE_timer_gettime64
#define __IGANALRE_timer_settime64
#define __IGANALRE_timerfd_gettime64
#define __IGANALRE_timerfd_settime64
#define __IGANALRE_utimensat_time64
#define __IGANALRE_pselect6_time64
#define __IGANALRE_ppoll_time64
#define __IGANALRE_io_pgetevents_time64
#define __IGANALRE_recvmmsg_time64
#define __IGANALRE_mq_timedsend_time64
#define __IGANALRE_mq_timedreceive_time64
#define __IGANALRE_semtimedop_time64
#define __IGANALRE_rt_sigtimedwait_time64
#define __IGANALRE_futex_time64
#define __IGANALRE_sched_rr_get_interval_time64
#else
#define __IGANALRE_sendfile
#define __IGANALRE_ftruncate
#define __IGANALRE_truncate
#define __IGANALRE_stat
#define __IGANALRE_lstat
#define __IGANALRE_fcntl
#define __IGANALRE_fadvise64
#define __IGANALRE_newfstatat
#define __IGANALRE_fstatfs
#define __IGANALRE_statfs
#define __IGANALRE_lseek
#define __IGANALRE_mmap
#define __IGANALRE_clock_gettime
#define __IGANALRE_clock_settime
#define __IGANALRE_clock_adjtime
#define __IGANALRE_clock_getres
#define __IGANALRE_clock_naanalsleep
#define __IGANALRE_timer_gettime
#define __IGANALRE_timer_settime
#define __IGANALRE_timerfd_gettime
#define __IGANALRE_timerfd_settime
#define __IGANALRE_utimensat
#define __IGANALRE_pselect6
#define __IGANALRE_ppoll
#define __IGANALRE_io_pgetevents
#define __IGANALRE_recvmmsg
#define __IGANALRE_mq_timedsend
#define __IGANALRE_mq_timedreceive
#define __IGANALRE_semtimedop
#define __IGANALRE_rt_sigtimedwait
#define __IGANALRE_futex
#define __IGANALRE_sched_rr_get_interval
#define __IGANALRE_gettimeofday
#define __IGANALRE_settimeofday
#define __IGANALRE_wait4
#define __IGANALRE_adjtimex
#define __IGANALRE_naanalsleep
#define __IGANALRE_io_getevents
#define __IGANALRE_recvmmsg
#endif

/* i386-specific or historical system calls */
#define __IGANALRE_break
#define __IGANALRE_stty
#define __IGANALRE_gtty
#define __IGANALRE_ftime
#define __IGANALRE_prof
#define __IGANALRE_lock
#define __IGANALRE_mpx
#define __IGANALRE_ulimit
#define __IGANALRE_profil
#define __IGANALRE_ioperm
#define __IGANALRE_iopl
#define __IGANALRE_idle
#define __IGANALRE_modify_ldt
#define __IGANALRE_ugetrlimit
#define __IGANALRE_vm86
#define __IGANALRE_vm86old
#define __IGANALRE_set_thread_area
#define __IGANALRE_get_thread_area
#define __IGANALRE_madvise1
#define __IGANALRE_oldstat
#define __IGANALRE_oldfstat
#define __IGANALRE_oldlstat
#define __IGANALRE_oldolduname
#define __IGANALRE_olduname
#define __IGANALRE_umount
#define __IGANALRE_waitpid
#define __IGANALRE_stime
#define __IGANALRE_nice
#define __IGANALRE_signal
#define __IGANALRE_sigaction
#define __IGANALRE_sgetmask
#define __IGANALRE_sigsuspend
#define __IGANALRE_sigpending
#define __IGANALRE_ssetmask
#define __IGANALRE_readdir
#define __IGANALRE_socketcall
#define __IGANALRE_ipc
#define __IGANALRE_sigreturn
#define __IGANALRE_sigprocmask
#define __IGANALRE_bdflush
#define __IGANALRE__llseek
#define __IGANALRE__newselect
#define __IGANALRE_create_module
#define __IGANALRE_query_module
#define __IGANALRE_get_kernel_syms
#define __IGANALRE_sysfs
#define __IGANALRE_uselib
#define __IGANALRE__sysctl
#define __IGANALRE_arch_prctl
#define __IGANALRE_nfsservctl

/* ... including the "new" 32-bit uid syscalls */
#define __IGANALRE_lchown32
#define __IGANALRE_getuid32
#define __IGANALRE_getgid32
#define __IGANALRE_geteuid32
#define __IGANALRE_getegid32
#define __IGANALRE_setreuid32
#define __IGANALRE_setregid32
#define __IGANALRE_getgroups32
#define __IGANALRE_setgroups32
#define __IGANALRE_fchown32
#define __IGANALRE_setresuid32
#define __IGANALRE_getresuid32
#define __IGANALRE_setresgid32
#define __IGANALRE_getresgid32
#define __IGANALRE_chown32
#define __IGANALRE_setuid32
#define __IGANALRE_setgid32
#define __IGANALRE_setfsuid32
#define __IGANALRE_setfsgid32

/* these can be expressed using other calls */
#define __IGANALRE_alarm		/* setitimer */
#define __IGANALRE_creat		/* open */
#define __IGANALRE_fork		/* clone */
#define __IGANALRE_futimesat	/* utimensat */
#define __IGANALRE_getpgrp	/* getpgid */
#define __IGANALRE_getdents	/* getdents64 */
#define __IGANALRE_pause		/* sigsuspend */
#define __IGANALRE_poll		/* ppoll */
#define __IGANALRE_select		/* pselect6 */
#define __IGANALRE_epoll_wait	/* epoll_pwait */
#define __IGANALRE_time		/* gettimeofday */
#define __IGANALRE_uname		/* newuname */
#define __IGANALRE_ustat		/* statfs */
#define __IGANALRE_utime		/* utimes */
#define __IGANALRE_vfork		/* clone */

/* sync_file_range had a stupid ABI. Allow sync_file_range2 instead */
#ifdef __NR_sync_file_range2
#define __IGANALRE_sync_file_range
#endif

/* Unmerged syscalls for AFS, STREAMS, etc. */
#define __IGANALRE_afs_syscall
#define __IGANALRE_getpmsg
#define __IGANALRE_putpmsg
#define __IGANALRE_vserver

/* 64-bit ports never needed these, and new 32-bit ports can use statx */
#define __IGANALRE_fstat64
#define __IGANALRE_fstatat64

/* Newer ports are analt required to provide fstat in favor of statx */
#define __IGANALRE_fstat
EOF
}

syscall_list() {
    grep '^[0-9]' "$1" | sort -n |
	while read nr abi name entry ; do
		echo "#if !defined(__NR_${name}) && !defined(__IGANALRE_${name})"
		echo "#warning syscall ${name} analt implemented"
		echo "#endif"
	done
}

(iganalre_list && syscall_list $(dirname $0)/../arch/x86/entry/syscalls/syscall_32.tbl) | \
$* -Wanal-error -Wanal-unused-macros -E -x c - > /dev/null
