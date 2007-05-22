#ifndef _ASM_IA64_UNISTD_H
#define _ASM_IA64_UNISTD_H

/*
 * IA-64 Linux syscall numbers and inline-functions.
 *
 * Copyright (C) 1998-2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/break.h>

#define __BREAK_SYSCALL			__IA64_BREAK_SYSCALL

#define __NR_ni_syscall			1024
#define __NR_exit			1025
#define __NR_read			1026
#define __NR_write			1027
#define __NR_open			1028
#define __NR_close			1029
#define __NR_creat			1030
#define __NR_link			1031
#define __NR_unlink			1032
#define __NR_execve			1033
#define __NR_chdir			1034
#define __NR_fchdir			1035
#define __NR_utimes			1036
#define __NR_mknod			1037
#define __NR_chmod			1038
#define __NR_chown			1039
#define __NR_lseek			1040
#define __NR_getpid			1041
#define __NR_getppid			1042
#define __NR_mount			1043
#define __NR_umount			1044
#define __NR_setuid			1045
#define __NR_getuid			1046
#define __NR_geteuid			1047
#define __NR_ptrace			1048
#define __NR_access			1049
#define __NR_sync			1050
#define __NR_fsync			1051
#define __NR_fdatasync			1052
#define __NR_kill			1053
#define __NR_rename			1054
#define __NR_mkdir			1055
#define __NR_rmdir			1056
#define __NR_dup			1057
#define __NR_pipe			1058
#define __NR_times			1059
#define __NR_brk			1060
#define __NR_setgid			1061
#define __NR_getgid			1062
#define __NR_getegid			1063
#define __NR_acct			1064
#define __NR_ioctl			1065
#define __NR_fcntl			1066
#define __NR_umask			1067
#define __NR_chroot			1068
#define __NR_ustat			1069
#define __NR_dup2			1070
#define __NR_setreuid			1071
#define __NR_setregid			1072
#define __NR_getresuid			1073
#define __NR_setresuid			1074
#define __NR_getresgid			1075
#define __NR_setresgid			1076
#define __NR_getgroups			1077
#define __NR_setgroups			1078
#define __NR_getpgid			1079
#define __NR_setpgid			1080
#define __NR_setsid			1081
#define __NR_getsid			1082
#define __NR_sethostname		1083
#define __NR_setrlimit			1084
#define __NR_getrlimit			1085
#define __NR_getrusage			1086
#define __NR_gettimeofday		1087
#define __NR_settimeofday		1088
#define __NR_select			1089
#define __NR_poll			1090
#define __NR_symlink			1091
#define __NR_readlink			1092
#define __NR_uselib			1093
#define __NR_swapon			1094
#define __NR_swapoff			1095
#define __NR_reboot			1096
#define __NR_truncate			1097
#define __NR_ftruncate			1098
#define __NR_fchmod			1099
#define __NR_fchown			1100
#define __NR_getpriority		1101
#define __NR_setpriority		1102
#define __NR_statfs			1103
#define __NR_fstatfs			1104
#define __NR_gettid			1105
#define __NR_semget			1106
#define __NR_semop			1107
#define __NR_semctl			1108
#define __NR_msgget			1109
#define __NR_msgsnd			1110
#define __NR_msgrcv			1111
#define __NR_msgctl			1112
#define __NR_shmget			1113
#define __NR_shmat			1114
#define __NR_shmdt			1115
#define __NR_shmctl			1116
/* also known as klogctl() in GNU libc: */
#define __NR_syslog			1117
#define __NR_setitimer			1118
#define __NR_getitimer			1119
/* 1120 was __NR_old_stat */
/* 1121 was __NR_old_lstat */
/* 1122 was __NR_old_fstat */
#define __NR_vhangup			1123
#define __NR_lchown			1124
#define __NR_remap_file_pages		1125
#define __NR_wait4			1126
#define __NR_sysinfo			1127
#define __NR_clone			1128
#define __NR_setdomainname		1129
#define __NR_uname			1130
#define __NR_adjtimex			1131
/* 1132 was __NR_create_module */
#define __NR_init_module		1133
#define __NR_delete_module		1134
/* 1135 was __NR_get_kernel_syms */
/* 1136 was __NR_query_module */
#define __NR_quotactl			1137
#define __NR_bdflush			1138
#define __NR_sysfs			1139
#define __NR_personality		1140
#define __NR_afs_syscall		1141
#define __NR_setfsuid			1142
#define __NR_setfsgid			1143
#define __NR_getdents			1144
#define __NR_flock			1145
#define __NR_readv			1146
#define __NR_writev			1147
#define __NR_pread64			1148
#define __NR_pwrite64			1149
#define __NR__sysctl			1150
#define __NR_mmap			1151
#define __NR_munmap			1152
#define __NR_mlock			1153
#define __NR_mlockall			1154
#define __NR_mprotect			1155
#define __NR_mremap			1156
#define __NR_msync			1157
#define __NR_munlock			1158
#define __NR_munlockall			1159
#define __NR_sched_getparam		1160
#define __NR_sched_setparam		1161
#define __NR_sched_getscheduler		1162
#define __NR_sched_setscheduler		1163
#define __NR_sched_yield		1164
#define __NR_sched_get_priority_max	1165
#define __NR_sched_get_priority_min	1166
#define __NR_sched_rr_get_interval	1167
#define __NR_nanosleep			1168
#define __NR_nfsservctl			1169
#define __NR_prctl			1170
/* 1171 is reserved for backwards compatibility with old __NR_getpagesize */
#define __NR_mmap2			1172
#define __NR_pciconfig_read		1173
#define __NR_pciconfig_write		1174
#define __NR_perfmonctl			1175
#define __NR_sigaltstack		1176
#define __NR_rt_sigaction		1177
#define __NR_rt_sigpending		1178
#define __NR_rt_sigprocmask		1179
#define __NR_rt_sigqueueinfo		1180
#define __NR_rt_sigreturn		1181
#define __NR_rt_sigsuspend		1182
#define __NR_rt_sigtimedwait		1183
#define __NR_getcwd			1184
#define __NR_capget			1185
#define __NR_capset			1186
#define __NR_sendfile			1187
#define __NR_getpmsg			1188
#define __NR_putpmsg			1189
#define __NR_socket			1190
#define __NR_bind			1191
#define __NR_connect			1192
#define __NR_listen			1193
#define __NR_accept			1194
#define __NR_getsockname		1195
#define __NR_getpeername		1196
#define __NR_socketpair			1197
#define __NR_send			1198
#define __NR_sendto			1199
#define __NR_recv			1200
#define __NR_recvfrom			1201
#define __NR_shutdown			1202
#define __NR_setsockopt			1203
#define __NR_getsockopt			1204
#define __NR_sendmsg			1205
#define __NR_recvmsg			1206
#define __NR_pivot_root			1207
#define __NR_mincore			1208
#define __NR_madvise			1209
#define __NR_stat			1210
#define __NR_lstat			1211
#define __NR_fstat			1212
#define __NR_clone2			1213
#define __NR_getdents64			1214
#define __NR_getunwind			1215
#define __NR_readahead			1216
#define __NR_setxattr			1217
#define __NR_lsetxattr			1218
#define __NR_fsetxattr			1219
#define __NR_getxattr			1220
#define __NR_lgetxattr			1221
#define __NR_fgetxattr			1222
#define __NR_listxattr			1223
#define __NR_llistxattr			1224
#define __NR_flistxattr			1225
#define __NR_removexattr		1226
#define __NR_lremovexattr		1227
#define __NR_fremovexattr		1228
#define __NR_tkill			1229
#define __NR_futex			1230
#define __NR_sched_setaffinity		1231
#define __NR_sched_getaffinity		1232
#define __NR_set_tid_address		1233
#define __NR_fadvise64			1234
#define __NR_tgkill			1235
#define __NR_exit_group			1236
#define __NR_lookup_dcookie		1237
#define __NR_io_setup			1238
#define __NR_io_destroy			1239
#define __NR_io_getevents		1240
#define __NR_io_submit			1241
#define __NR_io_cancel			1242
#define __NR_epoll_create		1243
#define __NR_epoll_ctl			1244
#define __NR_epoll_wait			1245
#define __NR_restart_syscall		1246
#define __NR_semtimedop			1247
#define __NR_timer_create		1248
#define __NR_timer_settime		1249
#define __NR_timer_gettime		1250
#define __NR_timer_getoverrun		1251
#define __NR_timer_delete		1252
#define __NR_clock_settime		1253
#define __NR_clock_gettime		1254
#define __NR_clock_getres		1255
#define __NR_clock_nanosleep		1256
#define __NR_fstatfs64			1257
#define __NR_statfs64			1258
#define __NR_mbind			1259
#define __NR_get_mempolicy		1260
#define __NR_set_mempolicy		1261
#define __NR_mq_open			1262
#define __NR_mq_unlink			1263
#define __NR_mq_timedsend		1264
#define __NR_mq_timedreceive		1265
#define __NR_mq_notify			1266
#define __NR_mq_getsetattr		1267
#define __NR_kexec_load			1268
#define __NR_vserver			1269
#define __NR_waitid			1270
#define __NR_add_key			1271
#define __NR_request_key		1272
#define __NR_keyctl			1273
#define __NR_ioprio_set			1274
#define __NR_ioprio_get			1275
#define __NR_move_pages			1276
#define __NR_inotify_init		1277
#define __NR_inotify_add_watch		1278
#define __NR_inotify_rm_watch		1279
#define __NR_migrate_pages		1280
#define __NR_openat			1281
#define __NR_mkdirat			1282
#define __NR_mknodat			1283
#define __NR_fchownat			1284
#define __NR_futimesat			1285
#define __NR_newfstatat			1286
#define __NR_unlinkat			1287
#define __NR_renameat			1288
#define __NR_linkat			1289
#define __NR_symlinkat			1290
#define __NR_readlinkat			1291
#define __NR_fchmodat			1292
#define __NR_faccessat			1293
#define __NR_pselect6			1294
#define __NR_ppoll			1295
#define __NR_unshare			1296
#define __NR_splice			1297
#define __NR_set_robust_list		1298
#define __NR_get_robust_list		1299
#define __NR_sync_file_range		1300
#define __NR_tee			1301
#define __NR_vmsplice			1302
/* 1303 reserved for move_pages */
#define __NR_getcpu			1304
#define __NR_epoll_pwait		1305
#define __NR_utimensat			1306
#define __NR_signalfd			1307
#define __NR_timerfd			1308
#define __NR_eventfd			1309

#ifdef __KERNEL__


#define NR_syscalls			286 /* length of syscall table */

/*
 * The following defines stop scripts/checksyscalls.sh from complaining about
 * unimplemented system calls.  Glibc provides for each of these by using
 * more modern equivalent system calls.
 */
#define __IGNORE_fork		/* clone() */
#define __IGNORE_time		/* gettimeofday() */
#define __IGNORE_alarm		/* setitimer(ITIMER_REAL, ... */
#define __IGNORE_pause		/* rt_sigprocmask(), rt_sigsuspend() */
#define __IGNORE_utime		/* utimes() */
#define __IGNORE_getpgrp	/* getpgid() */
#define __IGNORE_vfork		/* clone() */

#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND

#ifdef CONFIG_IA32_SUPPORT
# define __ARCH_WANT_SYS_FADVISE64
# define __ARCH_WANT_SYS_GETPGRP
# define __ARCH_WANT_SYS_LLSEEK
# define __ARCH_WANT_SYS_NICE
# define __ARCH_WANT_SYS_OLD_GETRLIMIT
# define __ARCH_WANT_SYS_OLDUMOUNT
# define __ARCH_WANT_SYS_SIGPENDING
# define __ARCH_WANT_SYS_SIGPROCMASK
# define __ARCH_WANT_COMPAT_SYS_RT_SIGSUSPEND
# define __ARCH_WANT_COMPAT_SYS_TIME
#endif

#if !defined(__ASSEMBLY__) && !defined(ASSEMBLER)

#include <linux/types.h>
#include <linux/linkage.h>
#include <linux/compiler.h>

extern long __ia64_syscall (long a0, long a1, long a2, long a3, long a4, long nr);

asmlinkage unsigned long sys_mmap(
				unsigned long addr, unsigned long len,
				int prot, int flags,
				int fd, long off);
asmlinkage unsigned long sys_mmap2(
				unsigned long addr, unsigned long len,
				int prot, int flags,
				int fd, long pgoff);
struct pt_regs;
struct sigaction;
long sys_execve(char __user *filename, char __user * __user *argv,
			   char __user * __user *envp, struct pt_regs *regs);
asmlinkage long sys_pipe(void);
asmlinkage long sys_rt_sigaction(int sig,
				 const struct sigaction __user *act,
				 struct sigaction __user *oact,
				 size_t sigsetsize);

/*
 * "Conditional" syscalls
 *
 * Note, this macro can only be used in the file which defines sys_ni_syscall, i.e., in
 * kernel/sys_ni.c.  This version causes warnings because the declaration isn't a
 * proper prototype, but we can't use __typeof__ either, because not all cond_syscall()
 * declarations have prototypes at the moment.
 */
#define cond_syscall(x) asmlinkage long x (void) __attribute__((weak,alias("sys_ni_syscall")))

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_IA64_UNISTD_H */
