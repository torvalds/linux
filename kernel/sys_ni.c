
#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/unistd.h>

/*
 * Non-implemented system calls get redirected here.
 */
asmlinkage long sys_ni_syscall(void)
{
	return -ENOSYS;
}

cond_syscall(sys_nfsservctl);
cond_syscall(sys_quotactl);
cond_syscall(sys_acct);
cond_syscall(sys_lookup_dcookie);
cond_syscall(sys_swapon);
cond_syscall(sys_swapoff);
cond_syscall(sys_kexec_load);
cond_syscall(compat_sys_kexec_load);
cond_syscall(sys_init_module);
cond_syscall(sys_delete_module);
cond_syscall(sys_socketpair);
cond_syscall(sys_bind);
cond_syscall(sys_listen);
cond_syscall(sys_accept);
cond_syscall(sys_connect);
cond_syscall(sys_getsockname);
cond_syscall(sys_getpeername);
cond_syscall(sys_sendto);
cond_syscall(sys_send);
cond_syscall(sys_recvfrom);
cond_syscall(sys_recv);
cond_syscall(sys_socket);
cond_syscall(sys_setsockopt);
cond_syscall(sys_getsockopt);
cond_syscall(sys_shutdown);
cond_syscall(sys_sendmsg);
cond_syscall(sys_recvmsg);
cond_syscall(sys_socketcall);
cond_syscall(sys_futex);
cond_syscall(compat_sys_futex);
cond_syscall(sys_epoll_create);
cond_syscall(sys_epoll_ctl);
cond_syscall(sys_epoll_wait);
cond_syscall(sys_semget);
cond_syscall(sys_semop);
cond_syscall(sys_semtimedop);
cond_syscall(sys_semctl);
cond_syscall(sys_msgget);
cond_syscall(sys_msgsnd);
cond_syscall(sys_msgrcv);
cond_syscall(sys_msgctl);
cond_syscall(sys_shmget);
cond_syscall(sys_shmat);
cond_syscall(sys_shmdt);
cond_syscall(sys_shmctl);
cond_syscall(sys_mq_open);
cond_syscall(sys_mq_unlink);
cond_syscall(sys_mq_timedsend);
cond_syscall(sys_mq_timedreceive);
cond_syscall(sys_mq_notify);
cond_syscall(sys_mq_getsetattr);
cond_syscall(compat_sys_mq_open);
cond_syscall(compat_sys_mq_timedsend);
cond_syscall(compat_sys_mq_timedreceive);
cond_syscall(compat_sys_mq_notify);
cond_syscall(compat_sys_mq_getsetattr);
cond_syscall(sys_mbind);
cond_syscall(sys_get_mempolicy);
cond_syscall(sys_set_mempolicy);
cond_syscall(compat_sys_mbind);
cond_syscall(compat_sys_get_mempolicy);
cond_syscall(compat_sys_set_mempolicy);
cond_syscall(sys_add_key);
cond_syscall(sys_request_key);
cond_syscall(sys_keyctl);
cond_syscall(compat_sys_keyctl);
cond_syscall(compat_sys_socketcall);
cond_syscall(sys_inotify_init);
cond_syscall(sys_inotify_add_watch);
cond_syscall(sys_inotify_rm_watch);

/* arch-specific weak syscall entries */
cond_syscall(sys_pciconfig_read);
cond_syscall(sys_pciconfig_write);
cond_syscall(sys_pciconfig_iobase);
cond_syscall(sys32_ipc);
cond_syscall(sys32_sysctl);
cond_syscall(ppc_rtas);
