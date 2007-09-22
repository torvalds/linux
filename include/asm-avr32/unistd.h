/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_UNISTD_H
#define __ASM_AVR32_UNISTD_H

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
#define __NR_umask		  7
#define __NR_creat		  8
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_execve		 11
#define __NR_chdir		 12
#define __NR_time		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_chown		 16
#define __NR_lchown		 17
#define __NR_lseek		 18
#define __NR__llseek		 19
#define __NR_getpid		 20
#define __NR_mount		 21
#define __NR_umount2		 22
#define __NR_setuid		 23
#define __NR_getuid		 24
#define __NR_stime		 25
#define __NR_ptrace		 26
#define __NR_alarm		 27
#define __NR_pause		 28
#define __NR_utime		 29
#define __NR_stat		 30
#define __NR_fstat		 31
#define __NR_lstat		 32
#define __NR_access		 33
#define __NR_chroot		 34
#define __NR_sync		 35
#define __NR_fsync		 36
#define __NR_kill		 37
#define __NR_rename		 38
#define __NR_mkdir		 39
#define __NR_rmdir		 40
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_times		 43
#define __NR_clone		 44
#define __NR_brk		 45
#define __NR_setgid		 46
#define __NR_getgid		 47
#define __NR_getcwd		 48
#define __NR_geteuid		 49
#define __NR_getegid		 50
#define __NR_acct		 51
#define __NR_setfsuid		 52
#define __NR_setfsgid		 53
#define __NR_ioctl		 54
#define __NR_fcntl		 55
#define __NR_setpgid		 56
#define __NR_mremap		 57
#define __NR_setresuid		 58
#define __NR_getresuid		 59
#define __NR_setreuid		 60
#define __NR_setregid		 61
#define __NR_ustat		 62
#define __NR_dup2		 63
#define __NR_getppid		 64
#define __NR_getpgrp		 65
#define __NR_setsid		 66
#define __NR_rt_sigaction	 67
#define __NR_rt_sigreturn	 68
#define __NR_rt_sigprocmask	 69
#define __NR_rt_sigpending	 70
#define __NR_rt_sigtimedwait	 71
#define __NR_rt_sigqueueinfo	 72
#define __NR_rt_sigsuspend	 73
#define __NR_sethostname	 74
#define __NR_setrlimit		 75
#define __NR_getrlimit		 76	/* SuS compliant getrlimit */
#define __NR_getrusage		 77
#define __NR_gettimeofday	 78
#define __NR_settimeofday	 79
#define __NR_getgroups		 80
#define __NR_setgroups		 81
#define __NR_select		 82
#define __NR_symlink		 83
#define __NR_fchdir		 84
#define __NR_readlink		 85
#define __NR_pread		 86
#define __NR_pwrite		 87
#define __NR_swapon		 88
#define __NR_reboot		 89
#define __NR_mmap2		 90
#define __NR_munmap		 91
#define __NR_truncate		 92
#define __NR_ftruncate		 93
#define __NR_fchmod		 94
#define __NR_fchown		 95
#define __NR_getpriority	 96
#define __NR_setpriority	 97
#define __NR_wait4		 98
#define __NR_statfs		 99
#define __NR_fstatfs		100
#define __NR_vhangup		101
#define __NR_sigaltstack	102
#define __NR_syslog		103
#define __NR_setitimer		104
#define __NR_getitimer		105
#define __NR_swapoff		106
#define __NR_sysinfo		107
/* 108 was __NR_ipc for a little while */
#define __NR_sendfile		109
#define __NR_setdomainname	110
#define __NR_uname		111
#define __NR_adjtimex		112
#define __NR_mprotect		113
#define __NR_vfork		114
#define __NR_init_module	115
#define __NR_delete_module	116
#define __NR_quotactl		117
#define __NR_getpgid		118
#define __NR_bdflush		119
#define __NR_sysfs		120
#define __NR_personality	121
#define __NR_afs_syscall	122 /* Syscall for Andrew File System */
#define __NR_getdents		123
#define __NR_flock		124
#define __NR_msync		125
#define __NR_readv		126
#define __NR_writev		127
#define __NR_getsid		128
#define __NR_fdatasync		129
#define __NR__sysctl		130
#define __NR_mlock		131
#define __NR_munlock		132
#define __NR_mlockall		133
#define __NR_munlockall		134
#define __NR_sched_setparam		135
#define __NR_sched_getparam		136
#define __NR_sched_setscheduler		137
#define __NR_sched_getscheduler		138
#define __NR_sched_yield		139
#define __NR_sched_get_priority_max	140
#define __NR_sched_get_priority_min	141
#define __NR_sched_rr_get_interval	142
#define __NR_nanosleep		143
#define __NR_poll		144
#define __NR_nfsservctl		145
#define __NR_setresgid		146
#define __NR_getresgid		147
#define __NR_prctl              148
#define __NR_socket		149
#define __NR_bind		150
#define __NR_connect		151
#define __NR_listen		152
#define __NR_accept		153
#define __NR_getsockname	154
#define __NR_getpeername	155
#define __NR_socketpair		156
#define __NR_send		157
#define __NR_recv		158
#define __NR_sendto		159
#define __NR_recvfrom		160
#define __NR_shutdown		161
#define __NR_setsockopt		162
#define __NR_getsockopt		163
#define __NR_sendmsg		164
#define __NR_recvmsg		165
#define __NR_truncate64		166
#define __NR_ftruncate64	167
#define __NR_stat64		168
#define __NR_lstat64		169
#define __NR_fstat64		170
#define __NR_pivot_root		171
#define __NR_mincore		172
#define __NR_madvise		173
#define __NR_getdents64		174
#define __NR_fcntl64		175
#define __NR_gettid		176
#define __NR_readahead		177
#define __NR_setxattr		178
#define __NR_lsetxattr		179
#define __NR_fsetxattr		180
#define __NR_getxattr		181
#define __NR_lgetxattr		182
#define __NR_fgetxattr		183
#define __NR_listxattr		184
#define __NR_llistxattr		185
#define __NR_flistxattr		186
#define __NR_removexattr	187
#define __NR_lremovexattr	188
#define __NR_fremovexattr	189
#define __NR_tkill		190
#define __NR_sendfile64		191
#define __NR_futex		192
#define __NR_sched_setaffinity	193
#define __NR_sched_getaffinity	194
#define __NR_capget		195
#define __NR_capset		196
#define __NR_io_setup		197
#define __NR_io_destroy		198
#define __NR_io_getevents	199
#define __NR_io_submit		200
#define __NR_io_cancel		201
#define __NR_fadvise64		202
#define __NR_exit_group		203
#define __NR_lookup_dcookie	204
#define __NR_epoll_create	205
#define __NR_epoll_ctl		206
#define __NR_epoll_wait		207
#define __NR_remap_file_pages	208
#define __NR_set_tid_address	209

#define __NR_timer_create	210
#define __NR_timer_settime	211
#define __NR_timer_gettime	212
#define __NR_timer_getoverrun	213
#define __NR_timer_delete	214
#define __NR_clock_settime	215
#define __NR_clock_gettime	216
#define __NR_clock_getres	217
#define __NR_clock_nanosleep	218
#define __NR_statfs64		219
#define __NR_fstatfs64		220
#define __NR_tgkill		221
				/* 222 reserved for tux */
#define __NR_utimes		223
#define __NR_fadvise64_64	224

#define __NR_cacheflush		225

#define __NR_vserver		226
#define __NR_mq_open		227
#define __NR_mq_unlink		228
#define __NR_mq_timedsend	229
#define __NR_mq_timedreceive	230
#define __NR_mq_notify		231
#define __NR_mq_getsetattr	232
#define __NR_kexec_load		233
#define __NR_waitid		234
#define __NR_add_key		235
#define __NR_request_key	236
#define __NR_keyctl		237
#define __NR_ioprio_set		238
#define __NR_ioprio_get		239
#define __NR_inotify_init	240
#define __NR_inotify_add_watch	241
#define __NR_inotify_rm_watch	242
#define __NR_openat		243
#define __NR_mkdirat		244
#define __NR_mknodat		245
#define __NR_fchownat		246
#define __NR_futimesat		247
#define __NR_fstatat64		248
#define __NR_unlinkat		249
#define __NR_renameat		250
#define __NR_linkat		251
#define __NR_symlinkat		252
#define __NR_readlinkat		253
#define __NR_fchmodat		254
#define __NR_faccessat		255
#define __NR_pselect6		256
#define __NR_ppoll		257
#define __NR_unshare		258
#define __NR_set_robust_list	259
#define __NR_get_robust_list	260
#define __NR_splice		261
#define __NR_sync_file_range	262
#define __NR_tee		263
#define __NR_vmsplice		264
#define __NR_epoll_pwait	265

#define __NR_msgget		266
#define __NR_msgsnd		267
#define __NR_msgrcv		268
#define __NR_msgctl		269
#define __NR_semget		270
#define __NR_semop		271
#define __NR_semctl		272
#define __NR_semtimedop		273
#define __NR_shmat		274
#define __NR_shmget		275
#define __NR_shmdt		276
#define __NR_shmctl		277

#define __NR_utimensat		278
#define __NR_signalfd		279
#define __NR_timerfd		280
#define __NR_eventfd		281

#ifdef __KERNEL__
#define NR_syscalls		282

/* Old stuff */
#define __IGNORE_uselib
#define __IGNORE_mmap

/* NUMA stuff */
#define __IGNORE_mbind
#define __IGNORE_get_mempolicy
#define __IGNORE_set_mempolicy
#define __IGNORE_migrate_pages
#define __IGNORE_move_pages

/* SMP stuff */
#define __IGNORE_getcpu

#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t" #x "\n\t.set\t" #x ",sys_ni_syscall");

#endif /* __KERNEL__ */

#endif /* __ASM_AVR32_UNISTD_H */
