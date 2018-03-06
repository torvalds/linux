// SPDX-License-Identifier: GPL-2.0

#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/unistd.h>

/*  we can't #include <linux/syscalls.h> here,
    but tell gcc to not warn with -Wmissing-prototypes  */
asmlinkage long sys_ni_syscall(void);

/*
 * Non-implemented system calls get redirected here.
 */
asmlinkage long sys_ni_syscall(void)
{
	return -ENOSYS;
}

/*
 * This list is kept in the same order as include/uapi/asm-generic/unistd.h.
 * Architecture specific entries go below, followed by deprecated or obsolete
 * system calls.
 */

cond_syscall(sys_io_setup);
cond_syscall(compat_sys_io_setup);
cond_syscall(sys_io_destroy);
cond_syscall(sys_io_submit);
cond_syscall(compat_sys_io_submit);
cond_syscall(sys_io_cancel);
cond_syscall(sys_io_getevents);
cond_syscall(compat_sys_io_getevents);

/* fs/xattr.c */

/* fs/dcache.c */

/* fs/cookies.c */
cond_syscall(sys_lookup_dcookie);
cond_syscall(compat_sys_lookup_dcookie);

/* fs/eventfd.c */
cond_syscall(sys_eventfd2);

/* fs/eventfd.c */
cond_syscall(sys_epoll_create1);
cond_syscall(sys_epoll_ctl);
cond_syscall(sys_epoll_pwait);
cond_syscall(compat_sys_epoll_pwait);

/* fs/fcntl.c */

/* fs/inotify_user.c */
cond_syscall(sys_inotify_init1);
cond_syscall(sys_inotify_add_watch);
cond_syscall(sys_inotify_rm_watch);

/* fs/ioctl.c */

/* fs/ioprio.c */
cond_syscall(sys_ioprio_set);
cond_syscall(sys_ioprio_get);

/* fs/locks.c */
cond_syscall(sys_flock);

/* fs/namei.c */

/* fs/namespace.c */

/* fs/nfsctl.c */

/* fs/open.c */

/* fs/pipe.c */

/* fs/quota.c */
cond_syscall(sys_quotactl);

/* fs/readdir.c */

/* fs/read_write.c */

/* fs/sendfile.c */

/* fs/select.c */

/* fs/signalfd.c */
cond_syscall(sys_signalfd4);
cond_syscall(compat_sys_signalfd4);

/* fs/splice.c */

/* fs/stat.c */

/* fs/sync.c */

/* fs/timerfd.c */
cond_syscall(sys_timerfd_create);
cond_syscall(sys_timerfd_settime);
cond_syscall(compat_sys_timerfd_settime);
cond_syscall(sys_timerfd_gettime);
cond_syscall(compat_sys_timerfd_gettime);

/* fs/utimes.c */

/* kernel/acct.c */
cond_syscall(sys_acct);

/* kernel/capability.c */
cond_syscall(sys_capget);
cond_syscall(sys_capset);

/* kernel/exec_domain.c */

/* kernel/exit.c */

/* kernel/fork.c */

/* kernel/futex.c */
cond_syscall(sys_futex);
cond_syscall(compat_sys_futex);
cond_syscall(sys_set_robust_list);
cond_syscall(compat_sys_set_robust_list);
cond_syscall(sys_get_robust_list);
cond_syscall(compat_sys_get_robust_list);

/* kernel/hrtimer.c */

/* kernel/itimer.c */

/* kernel/kexec.c */
cond_syscall(sys_kexec_load);
cond_syscall(compat_sys_kexec_load);

/* kernel/module.c */
cond_syscall(sys_init_module);
cond_syscall(sys_delete_module);

/* kernel/posix-timers.c */

/* kernel/printk.c */
cond_syscall(sys_syslog);

/* kernel/ptrace.c */

/* kernel/sched/core.c */

/* kernel/signal.c */

/* kernel/sys.c */
cond_syscall(sys_setregid);
cond_syscall(sys_setgid);
cond_syscall(sys_setreuid);
cond_syscall(sys_setuid);
cond_syscall(sys_setresuid);
cond_syscall(sys_getresuid);
cond_syscall(sys_setresgid);
cond_syscall(sys_getresgid);
cond_syscall(sys_setfsuid);
cond_syscall(sys_setfsgid);
cond_syscall(sys_setgroups);
cond_syscall(sys_getgroups);

/* kernel/time.c */

/* kernel/timer.c */

/* ipc/mqueue.c */
cond_syscall(sys_mq_open);
cond_syscall(compat_sys_mq_open);
cond_syscall(sys_mq_unlink);
cond_syscall(sys_mq_timedsend);
cond_syscall(compat_sys_mq_timedsend);
cond_syscall(sys_mq_timedreceive);
cond_syscall(compat_sys_mq_timedreceive);
cond_syscall(sys_mq_notify);
cond_syscall(compat_sys_mq_notify);
cond_syscall(sys_mq_getsetattr);
cond_syscall(compat_sys_mq_getsetattr);

/* ipc/msg.c */
cond_syscall(sys_msgget);
cond_syscall(sys_msgctl);
cond_syscall(compat_sys_msgctl);
cond_syscall(sys_msgrcv);
cond_syscall(compat_sys_msgrcv);
cond_syscall(sys_msgsnd);
cond_syscall(compat_sys_msgsnd);

/* ipc/sem.c */
cond_syscall(sys_semget);
cond_syscall(sys_semctl);
cond_syscall(compat_sys_semctl);
cond_syscall(sys_semtimedop);
cond_syscall(compat_sys_semtimedop);
cond_syscall(sys_semop);

/* ipc/shm.c */
cond_syscall(sys_shmget);
cond_syscall(sys_shmctl);
cond_syscall(compat_sys_shmctl);
cond_syscall(sys_shmat);
cond_syscall(compat_sys_shmat);
cond_syscall(sys_shmdt);

/* net/socket.c */
cond_syscall(sys_socket);
cond_syscall(sys_socketpair);
cond_syscall(sys_bind);
cond_syscall(sys_listen);
cond_syscall(sys_accept);
cond_syscall(sys_connect);
cond_syscall(sys_getsockname);
cond_syscall(sys_getpeername);
cond_syscall(sys_setsockopt);
cond_syscall(compat_sys_setsockopt);
cond_syscall(sys_getsockopt);
cond_syscall(compat_sys_getsockopt);
cond_syscall(sys_sendto);
cond_syscall(sys_shutdown);
cond_syscall(sys_recvfrom);
cond_syscall(compat_sys_recvfrom);
cond_syscall(sys_sendmsg);
cond_syscall(compat_sys_sendmsg);
cond_syscall(sys_recvmsg);
cond_syscall(compat_sys_recvmsg);

/* mm/filemap.c */

/* mm/nommu.c, also with MMU */
cond_syscall(sys_mremap);

/* security/keys/keyctl.c */
cond_syscall(sys_add_key);
cond_syscall(sys_request_key);
cond_syscall(sys_keyctl);
cond_syscall(compat_sys_keyctl);

/* arch/example/kernel/sys_example.c */

/* mm/fadvise.c */
cond_syscall(sys_fadvise64_64);

/* mm/, CONFIG_MMU only */
cond_syscall(sys_swapon);
cond_syscall(sys_swapoff);
cond_syscall(sys_mprotect);
cond_syscall(sys_msync);
cond_syscall(sys_mlock);
cond_syscall(sys_munlock);
cond_syscall(sys_mlockall);
cond_syscall(sys_munlockall);
cond_syscall(sys_mincore);
cond_syscall(sys_madvise);
cond_syscall(sys_remap_file_pages);
cond_syscall(sys_mbind);
cond_syscall(compat_sys_mbind);
cond_syscall(sys_get_mempolicy);
cond_syscall(compat_sys_get_mempolicy);
cond_syscall(sys_set_mempolicy);
cond_syscall(compat_sys_set_mempolicy);
cond_syscall(sys_migrate_pages);
cond_syscall(compat_sys_migrate_pages);
cond_syscall(sys_move_pages);
cond_syscall(compat_sys_move_pages);

cond_syscall(sys_perf_event_open);
cond_syscall(sys_accept4);
cond_syscall(sys_recvmmsg);
cond_syscall(compat_sys_recvmmsg);

/*
 * Architecture specific syscalls: see further below
 */

/* fanotify */
cond_syscall(sys_fanotify_init);
cond_syscall(sys_fanotify_mark);

/* open by handle */
cond_syscall(sys_name_to_handle_at);
cond_syscall(sys_open_by_handle_at);
cond_syscall(compat_sys_open_by_handle_at);

cond_syscall(sys_sendmmsg);
cond_syscall(compat_sys_sendmmsg);
cond_syscall(sys_process_vm_readv);
cond_syscall(compat_sys_process_vm_readv);
cond_syscall(sys_process_vm_writev);
cond_syscall(compat_sys_process_vm_writev);

/* compare kernel pointers */
cond_syscall(sys_kcmp);

cond_syscall(sys_finit_module);

/* operate on Secure Computing state */
cond_syscall(sys_seccomp);

cond_syscall(sys_memfd_create);

/* access BPF programs and maps */
cond_syscall(sys_bpf);

/* execveat */
cond_syscall(sys_execveat);

cond_syscall(sys_userfaultfd);

/* membarrier */
cond_syscall(sys_membarrier);

cond_syscall(sys_mlock2);

cond_syscall(sys_copy_file_range);

/* memory protection keys */
cond_syscall(sys_pkey_mprotect);
cond_syscall(sys_pkey_alloc);
cond_syscall(sys_pkey_free);


/*
 * Architecture specific weak syscall entries.
 */

/* pciconfig: alpha, arm, arm64, ia64, sparc */
cond_syscall(sys_pciconfig_read);
cond_syscall(sys_pciconfig_write);
cond_syscall(sys_pciconfig_iobase);

/* sys_socketcall: arm, mips, x86, ... */
cond_syscall(sys_socketcall);
cond_syscall(compat_sys_socketcall);

/* compat syscalls for arm64, x86, ... */
cond_syscall(compat_sys_sysctl);
cond_syscall(compat_sys_fanotify_mark);

/* x86 */
cond_syscall(sys_vm86old);
cond_syscall(sys_modify_ldt);
cond_syscall(compat_sys_quotactl32);
cond_syscall(sys_vm86);
cond_syscall(sys_kexec_file_load);

/* s390 */
cond_syscall(sys_s390_pci_mmio_read);
cond_syscall(sys_s390_pci_mmio_write);
cond_syscall(compat_sys_s390_ipc);

/* powerpc */
cond_syscall(ppc_rtas);
cond_syscall(sys_spu_run);
cond_syscall(sys_spu_create);
cond_syscall(sys_subpage_prot);


/*
 * Deprecated system calls which are still defined in
 * include/uapi/asm-generic/unistd.h and wanted by >= 1 arch
 */

/* __ARCH_WANT_SYSCALL_NO_FLAGS */
cond_syscall(sys_epoll_create);
cond_syscall(sys_inotify_init);
cond_syscall(sys_eventfd);
cond_syscall(sys_signalfd);
cond_syscall(compat_sys_signalfd);

/* __ARCH_WANT_SYSCALL_OFF_T */
cond_syscall(sys_fadvise64);

/* __ARCH_WANT_SYSCALL_DEPRECATED */
cond_syscall(sys_epoll_wait);
cond_syscall(sys_recv);
cond_syscall(compat_sys_recv);
cond_syscall(sys_send);
cond_syscall(sys_bdflush);
cond_syscall(sys_uselib);


/*
 * The syscalls below are not found in include/uapi/asm-generic/unistd.h
 */

/* obsolete: SGETMASK_SYSCALL */
cond_syscall(sys_sgetmask);
cond_syscall(sys_ssetmask);

/* obsolete: SYSFS_SYSCALL */
cond_syscall(sys_sysfs);

/* obsolete: __ARCH_WANT_SYS_IPC */
cond_syscall(sys_ipc);
cond_syscall(compat_sys_ipc);

/* obsolete: UID16 */
cond_syscall(sys_chown16);
cond_syscall(sys_fchown16);
cond_syscall(sys_getegid16);
cond_syscall(sys_geteuid16);
cond_syscall(sys_getgid16);
cond_syscall(sys_getgroups16);
cond_syscall(sys_getresgid16);
cond_syscall(sys_getresuid16);
cond_syscall(sys_getuid16);
cond_syscall(sys_lchown16);
cond_syscall(sys_setfsgid16);
cond_syscall(sys_setfsuid16);
cond_syscall(sys_setgid16);
cond_syscall(sys_setgroups16);
cond_syscall(sys_setregid16);
cond_syscall(sys_setresgid16);
cond_syscall(sys_setresuid16);
cond_syscall(sys_setreuid16);
cond_syscall(sys_setuid16);
