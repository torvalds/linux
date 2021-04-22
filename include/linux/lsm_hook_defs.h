/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Linux Security Module Hook declarations.
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 * Copyright (C) 2015 Intel Corporation.
 * Copyright (C) 2015 Casey Schaufler <casey@schaufler-ca.com>
 * Copyright (C) 2016 Mellanox Techonologies
 * Copyright (C) 2020 Google LLC.
 */

/*
 * The macro LSM_HOOK is used to define the data structures required by
 * the LSM framework using the pattern:
 *
 *	LSM_HOOK(<return_type>, <default_value>, <hook_name>, args...)
 *
 * struct security_hook_heads {
 *   #define LSM_HOOK(RET, DEFAULT, NAME, ...) struct hlist_head NAME;
 *   #include <linux/lsm_hook_defs.h>
 *   #undef LSM_HOOK
 * };
 */
LSM_HOOK(int, 0, binder_set_context_mgr, struct task_struct *mgr)
LSM_HOOK(int, 0, binder_transaction, struct task_struct *from,
	 struct task_struct *to)
LSM_HOOK(int, 0, binder_transfer_binder, struct task_struct *from,
	 struct task_struct *to)
LSM_HOOK(int, 0, binder_transfer_file, struct task_struct *from,
	 struct task_struct *to, struct file *file)
LSM_HOOK(int, 0, ptrace_access_check, struct task_struct *child,
	 unsigned int mode)
LSM_HOOK(int, 0, ptrace_traceme, struct task_struct *parent)
LSM_HOOK(int, 0, capget, struct task_struct *target, kernel_cap_t *effective,
	 kernel_cap_t *inheritable, kernel_cap_t *permitted)
LSM_HOOK(int, 0, capset, struct cred *new, const struct cred *old,
	 const kernel_cap_t *effective, const kernel_cap_t *inheritable,
	 const kernel_cap_t *permitted)
LSM_HOOK(int, 0, capable, const struct cred *cred, struct user_namespace *ns,
	 int cap, unsigned int opts)
LSM_HOOK(int, 0, quotactl, int cmds, int type, int id, struct super_block *sb)
LSM_HOOK(int, 0, quota_on, struct dentry *dentry)
LSM_HOOK(int, 0, syslog, int type)
LSM_HOOK(int, 0, settime, const struct timespec64 *ts,
	 const struct timezone *tz)
LSM_HOOK(int, 0, vm_enough_memory, struct mm_struct *mm, long pages)
LSM_HOOK(int, 0, bprm_creds_for_exec, struct linux_binprm *bprm)
LSM_HOOK(int, 0, bprm_creds_from_file, struct linux_binprm *bprm, struct file *file)
LSM_HOOK(int, 0, bprm_check_security, struct linux_binprm *bprm)
LSM_HOOK(void, LSM_RET_VOID, bprm_committing_creds, struct linux_binprm *bprm)
LSM_HOOK(void, LSM_RET_VOID, bprm_committed_creds, struct linux_binprm *bprm)
LSM_HOOK(int, 0, fs_context_dup, struct fs_context *fc,
	 struct fs_context *src_sc)
LSM_HOOK(int, -ENOPARAM, fs_context_parse_param, struct fs_context *fc,
	 struct fs_parameter *param)
LSM_HOOK(int, 0, sb_alloc_security, struct super_block *sb)
LSM_HOOK(void, LSM_RET_VOID, sb_delete, struct super_block *sb)
LSM_HOOK(void, LSM_RET_VOID, sb_free_security, struct super_block *sb)
LSM_HOOK(void, LSM_RET_VOID, sb_free_mnt_opts, void *mnt_opts)
LSM_HOOK(int, 0, sb_eat_lsm_opts, char *orig, void **mnt_opts)
LSM_HOOK(int, 0, sb_remount, struct super_block *sb, void *mnt_opts)
LSM_HOOK(int, 0, sb_kern_mount, struct super_block *sb)
LSM_HOOK(int, 0, sb_show_options, struct seq_file *m, struct super_block *sb)
LSM_HOOK(int, 0, sb_statfs, struct dentry *dentry)
LSM_HOOK(int, 0, sb_mount, const char *dev_name, const struct path *path,
	 const char *type, unsigned long flags, void *data)
LSM_HOOK(int, 0, sb_umount, struct vfsmount *mnt, int flags)
LSM_HOOK(int, 0, sb_pivotroot, const struct path *old_path,
	 const struct path *new_path)
LSM_HOOK(int, 0, sb_set_mnt_opts, struct super_block *sb, void *mnt_opts,
	 unsigned long kern_flags, unsigned long *set_kern_flags)
LSM_HOOK(int, 0, sb_clone_mnt_opts, const struct super_block *oldsb,
	 struct super_block *newsb, unsigned long kern_flags,
	 unsigned long *set_kern_flags)
LSM_HOOK(int, 0, sb_add_mnt_opt, const char *option, const char *val,
	 int len, void **mnt_opts)
LSM_HOOK(int, 0, move_mount, const struct path *from_path,
	 const struct path *to_path)
LSM_HOOK(int, 0, dentry_init_security, struct dentry *dentry,
	 int mode, const struct qstr *name, void **ctx, u32 *ctxlen)
LSM_HOOK(int, 0, dentry_create_files_as, struct dentry *dentry, int mode,
	 struct qstr *name, const struct cred *old, struct cred *new)

#ifdef CONFIG_SECURITY_PATH
LSM_HOOK(int, 0, path_unlink, const struct path *dir, struct dentry *dentry)
LSM_HOOK(int, 0, path_mkdir, const struct path *dir, struct dentry *dentry,
	 umode_t mode)
LSM_HOOK(int, 0, path_rmdir, const struct path *dir, struct dentry *dentry)
LSM_HOOK(int, 0, path_mknod, const struct path *dir, struct dentry *dentry,
	 umode_t mode, unsigned int dev)
LSM_HOOK(int, 0, path_truncate, const struct path *path)
LSM_HOOK(int, 0, path_symlink, const struct path *dir, struct dentry *dentry,
	 const char *old_name)
LSM_HOOK(int, 0, path_link, struct dentry *old_dentry,
	 const struct path *new_dir, struct dentry *new_dentry)
LSM_HOOK(int, 0, path_rename, const struct path *old_dir,
	 struct dentry *old_dentry, const struct path *new_dir,
	 struct dentry *new_dentry)
LSM_HOOK(int, 0, path_chmod, const struct path *path, umode_t mode)
LSM_HOOK(int, 0, path_chown, const struct path *path, kuid_t uid, kgid_t gid)
LSM_HOOK(int, 0, path_chroot, const struct path *path)
#endif /* CONFIG_SECURITY_PATH */

/* Needed for inode based security check */
LSM_HOOK(int, 0, path_notify, const struct path *path, u64 mask,
	 unsigned int obj_type)
LSM_HOOK(int, 0, inode_alloc_security, struct inode *inode)
LSM_HOOK(void, LSM_RET_VOID, inode_free_security, struct inode *inode)
LSM_HOOK(int, 0, inode_init_security, struct inode *inode,
	 struct inode *dir, const struct qstr *qstr, const char **name,
	 void **value, size_t *len)
LSM_HOOK(int, 0, inode_init_security_anon, struct inode *inode,
	 const struct qstr *name, const struct inode *context_inode)
LSM_HOOK(int, 0, inode_create, struct inode *dir, struct dentry *dentry,
	 umode_t mode)
LSM_HOOK(int, 0, inode_link, struct dentry *old_dentry, struct inode *dir,
	 struct dentry *new_dentry)
LSM_HOOK(int, 0, inode_unlink, struct inode *dir, struct dentry *dentry)
LSM_HOOK(int, 0, inode_symlink, struct inode *dir, struct dentry *dentry,
	 const char *old_name)
LSM_HOOK(int, 0, inode_mkdir, struct inode *dir, struct dentry *dentry,
	 umode_t mode)
LSM_HOOK(int, 0, inode_rmdir, struct inode *dir, struct dentry *dentry)
LSM_HOOK(int, 0, inode_mknod, struct inode *dir, struct dentry *dentry,
	 umode_t mode, dev_t dev)
LSM_HOOK(int, 0, inode_rename, struct inode *old_dir, struct dentry *old_dentry,
	 struct inode *new_dir, struct dentry *new_dentry)
LSM_HOOK(int, 0, inode_readlink, struct dentry *dentry)
LSM_HOOK(int, 0, inode_follow_link, struct dentry *dentry, struct inode *inode,
	 bool rcu)
LSM_HOOK(int, 0, inode_permission, struct inode *inode, int mask)
LSM_HOOK(int, 0, inode_setattr, struct dentry *dentry, struct iattr *attr)
LSM_HOOK(int, 0, inode_getattr, const struct path *path)
LSM_HOOK(int, 0, inode_setxattr, struct user_namespace *mnt_userns,
	 struct dentry *dentry, const char *name, const void *value,
	 size_t size, int flags)
LSM_HOOK(void, LSM_RET_VOID, inode_post_setxattr, struct dentry *dentry,
	 const char *name, const void *value, size_t size, int flags)
LSM_HOOK(int, 0, inode_getxattr, struct dentry *dentry, const char *name)
LSM_HOOK(int, 0, inode_listxattr, struct dentry *dentry)
LSM_HOOK(int, 0, inode_removexattr, struct user_namespace *mnt_userns,
	 struct dentry *dentry, const char *name)
LSM_HOOK(int, 0, inode_need_killpriv, struct dentry *dentry)
LSM_HOOK(int, 0, inode_killpriv, struct user_namespace *mnt_userns,
	 struct dentry *dentry)
LSM_HOOK(int, -EOPNOTSUPP, inode_getsecurity, struct user_namespace *mnt_userns,
	 struct inode *inode, const char *name, void **buffer, bool alloc)
LSM_HOOK(int, -EOPNOTSUPP, inode_setsecurity, struct inode *inode,
	 const char *name, const void *value, size_t size, int flags)
LSM_HOOK(int, 0, inode_listsecurity, struct inode *inode, char *buffer,
	 size_t buffer_size)
LSM_HOOK(void, LSM_RET_VOID, inode_getsecid, struct inode *inode, u32 *secid)
LSM_HOOK(int, 0, inode_copy_up, struct dentry *src, struct cred **new)
LSM_HOOK(int, -EOPNOTSUPP, inode_copy_up_xattr, const char *name)
LSM_HOOK(int, 0, kernfs_init_security, struct kernfs_node *kn_dir,
	 struct kernfs_node *kn)
LSM_HOOK(int, 0, file_permission, struct file *file, int mask)
LSM_HOOK(int, 0, file_alloc_security, struct file *file)
LSM_HOOK(void, LSM_RET_VOID, file_free_security, struct file *file)
LSM_HOOK(int, 0, file_ioctl, struct file *file, unsigned int cmd,
	 unsigned long arg)
LSM_HOOK(int, 0, mmap_addr, unsigned long addr)
LSM_HOOK(int, 0, mmap_file, struct file *file, unsigned long reqprot,
	 unsigned long prot, unsigned long flags)
LSM_HOOK(int, 0, file_mprotect, struct vm_area_struct *vma,
	 unsigned long reqprot, unsigned long prot)
LSM_HOOK(int, 0, file_lock, struct file *file, unsigned int cmd)
LSM_HOOK(int, 0, file_fcntl, struct file *file, unsigned int cmd,
	 unsigned long arg)
LSM_HOOK(void, LSM_RET_VOID, file_set_fowner, struct file *file)
LSM_HOOK(int, 0, file_send_sigiotask, struct task_struct *tsk,
	 struct fown_struct *fown, int sig)
LSM_HOOK(int, 0, file_receive, struct file *file)
LSM_HOOK(int, 0, file_open, struct file *file)
LSM_HOOK(int, 0, task_alloc, struct task_struct *task,
	 unsigned long clone_flags)
LSM_HOOK(void, LSM_RET_VOID, task_free, struct task_struct *task)
LSM_HOOK(int, 0, cred_alloc_blank, struct cred *cred, gfp_t gfp)
LSM_HOOK(void, LSM_RET_VOID, cred_free, struct cred *cred)
LSM_HOOK(int, 0, cred_prepare, struct cred *new, const struct cred *old,
	 gfp_t gfp)
LSM_HOOK(void, LSM_RET_VOID, cred_transfer, struct cred *new,
	 const struct cred *old)
LSM_HOOK(void, LSM_RET_VOID, cred_getsecid, const struct cred *c, u32 *secid)
LSM_HOOK(int, 0, kernel_act_as, struct cred *new, u32 secid)
LSM_HOOK(int, 0, kernel_create_files_as, struct cred *new, struct inode *inode)
LSM_HOOK(int, 0, kernel_module_request, char *kmod_name)
LSM_HOOK(int, 0, kernel_load_data, enum kernel_load_data_id id, bool contents)
LSM_HOOK(int, 0, kernel_post_load_data, char *buf, loff_t size,
	 enum kernel_load_data_id id, char *description)
LSM_HOOK(int, 0, kernel_read_file, struct file *file,
	 enum kernel_read_file_id id, bool contents)
LSM_HOOK(int, 0, kernel_post_read_file, struct file *file, char *buf,
	 loff_t size, enum kernel_read_file_id id)
LSM_HOOK(int, 0, task_fix_setuid, struct cred *new, const struct cred *old,
	 int flags)
LSM_HOOK(int, 0, task_fix_setgid, struct cred *new, const struct cred * old,
	 int flags)
LSM_HOOK(int, 0, task_setpgid, struct task_struct *p, pid_t pgid)
LSM_HOOK(int, 0, task_getpgid, struct task_struct *p)
LSM_HOOK(int, 0, task_getsid, struct task_struct *p)
LSM_HOOK(void, LSM_RET_VOID, task_getsecid, struct task_struct *p, u32 *secid)
LSM_HOOK(int, 0, task_setnice, struct task_struct *p, int nice)
LSM_HOOK(int, 0, task_setioprio, struct task_struct *p, int ioprio)
LSM_HOOK(int, 0, task_getioprio, struct task_struct *p)
LSM_HOOK(int, 0, task_prlimit, const struct cred *cred,
	 const struct cred *tcred, unsigned int flags)
LSM_HOOK(int, 0, task_setrlimit, struct task_struct *p, unsigned int resource,
	 struct rlimit *new_rlim)
LSM_HOOK(int, 0, task_setscheduler, struct task_struct *p)
LSM_HOOK(int, 0, task_getscheduler, struct task_struct *p)
LSM_HOOK(int, 0, task_movememory, struct task_struct *p)
LSM_HOOK(int, 0, task_kill, struct task_struct *p, struct kernel_siginfo *info,
	 int sig, const struct cred *cred)
LSM_HOOK(int, -ENOSYS, task_prctl, int option, unsigned long arg2,
	 unsigned long arg3, unsigned long arg4, unsigned long arg5)
LSM_HOOK(void, LSM_RET_VOID, task_to_inode, struct task_struct *p,
	 struct inode *inode)
LSM_HOOK(int, 0, ipc_permission, struct kern_ipc_perm *ipcp, short flag)
LSM_HOOK(void, LSM_RET_VOID, ipc_getsecid, struct kern_ipc_perm *ipcp,
	 u32 *secid)
LSM_HOOK(int, 0, msg_msg_alloc_security, struct msg_msg *msg)
LSM_HOOK(void, LSM_RET_VOID, msg_msg_free_security, struct msg_msg *msg)
LSM_HOOK(int, 0, msg_queue_alloc_security, struct kern_ipc_perm *perm)
LSM_HOOK(void, LSM_RET_VOID, msg_queue_free_security,
	 struct kern_ipc_perm *perm)
LSM_HOOK(int, 0, msg_queue_associate, struct kern_ipc_perm *perm, int msqflg)
LSM_HOOK(int, 0, msg_queue_msgctl, struct kern_ipc_perm *perm, int cmd)
LSM_HOOK(int, 0, msg_queue_msgsnd, struct kern_ipc_perm *perm,
	 struct msg_msg *msg, int msqflg)
LSM_HOOK(int, 0, msg_queue_msgrcv, struct kern_ipc_perm *perm,
	 struct msg_msg *msg, struct task_struct *target, long type, int mode)
LSM_HOOK(int, 0, shm_alloc_security, struct kern_ipc_perm *perm)
LSM_HOOK(void, LSM_RET_VOID, shm_free_security, struct kern_ipc_perm *perm)
LSM_HOOK(int, 0, shm_associate, struct kern_ipc_perm *perm, int shmflg)
LSM_HOOK(int, 0, shm_shmctl, struct kern_ipc_perm *perm, int cmd)
LSM_HOOK(int, 0, shm_shmat, struct kern_ipc_perm *perm, char __user *shmaddr,
	 int shmflg)
LSM_HOOK(int, 0, sem_alloc_security, struct kern_ipc_perm *perm)
LSM_HOOK(void, LSM_RET_VOID, sem_free_security, struct kern_ipc_perm *perm)
LSM_HOOK(int, 0, sem_associate, struct kern_ipc_perm *perm, int semflg)
LSM_HOOK(int, 0, sem_semctl, struct kern_ipc_perm *perm, int cmd)
LSM_HOOK(int, 0, sem_semop, struct kern_ipc_perm *perm, struct sembuf *sops,
	 unsigned nsops, int alter)
LSM_HOOK(int, 0, netlink_send, struct sock *sk, struct sk_buff *skb)
LSM_HOOK(void, LSM_RET_VOID, d_instantiate, struct dentry *dentry,
	 struct inode *inode)
LSM_HOOK(int, -EINVAL, getprocattr, struct task_struct *p, char *name,
	 char **value)
LSM_HOOK(int, -EINVAL, setprocattr, const char *name, void *value, size_t size)
LSM_HOOK(int, 0, ismaclabel, const char *name)
LSM_HOOK(int, -EOPNOTSUPP, secid_to_secctx, u32 secid, char **secdata,
	 u32 *seclen)
LSM_HOOK(int, 0, secctx_to_secid, const char *secdata, u32 seclen, u32 *secid)
LSM_HOOK(void, LSM_RET_VOID, release_secctx, char *secdata, u32 seclen)
LSM_HOOK(void, LSM_RET_VOID, inode_invalidate_secctx, struct inode *inode)
LSM_HOOK(int, 0, inode_notifysecctx, struct inode *inode, void *ctx, u32 ctxlen)
LSM_HOOK(int, 0, inode_setsecctx, struct dentry *dentry, void *ctx, u32 ctxlen)
LSM_HOOK(int, 0, inode_getsecctx, struct inode *inode, void **ctx,
	 u32 *ctxlen)

#if defined(CONFIG_SECURITY) && defined(CONFIG_WATCH_QUEUE)
LSM_HOOK(int, 0, post_notification, const struct cred *w_cred,
	 const struct cred *cred, struct watch_notification *n)
#endif /* CONFIG_SECURITY && CONFIG_WATCH_QUEUE */

#if defined(CONFIG_SECURITY) && defined(CONFIG_KEY_NOTIFICATIONS)
LSM_HOOK(int, 0, watch_key, struct key *key)
#endif /* CONFIG_SECURITY && CONFIG_KEY_NOTIFICATIONS */

#ifdef CONFIG_SECURITY_NETWORK
LSM_HOOK(int, 0, unix_stream_connect, struct sock *sock, struct sock *other,
	 struct sock *newsk)
LSM_HOOK(int, 0, unix_may_send, struct socket *sock, struct socket *other)
LSM_HOOK(int, 0, socket_create, int family, int type, int protocol, int kern)
LSM_HOOK(int, 0, socket_post_create, struct socket *sock, int family, int type,
	 int protocol, int kern)
LSM_HOOK(int, 0, socket_socketpair, struct socket *socka, struct socket *sockb)
LSM_HOOK(int, 0, socket_bind, struct socket *sock, struct sockaddr *address,
	 int addrlen)
LSM_HOOK(int, 0, socket_connect, struct socket *sock, struct sockaddr *address,
	 int addrlen)
LSM_HOOK(int, 0, socket_listen, struct socket *sock, int backlog)
LSM_HOOK(int, 0, socket_accept, struct socket *sock, struct socket *newsock)
LSM_HOOK(int, 0, socket_sendmsg, struct socket *sock, struct msghdr *msg,
	 int size)
LSM_HOOK(int, 0, socket_recvmsg, struct socket *sock, struct msghdr *msg,
	 int size, int flags)
LSM_HOOK(int, 0, socket_getsockname, struct socket *sock)
LSM_HOOK(int, 0, socket_getpeername, struct socket *sock)
LSM_HOOK(int, 0, socket_getsockopt, struct socket *sock, int level, int optname)
LSM_HOOK(int, 0, socket_setsockopt, struct socket *sock, int level, int optname)
LSM_HOOK(int, 0, socket_shutdown, struct socket *sock, int how)
LSM_HOOK(int, 0, socket_sock_rcv_skb, struct sock *sk, struct sk_buff *skb)
LSM_HOOK(int, 0, socket_getpeersec_stream, struct socket *sock,
	 char __user *optval, int __user *optlen, unsigned len)
LSM_HOOK(int, 0, socket_getpeersec_dgram, struct socket *sock,
	 struct sk_buff *skb, u32 *secid)
LSM_HOOK(int, 0, sk_alloc_security, struct sock *sk, int family, gfp_t priority)
LSM_HOOK(void, LSM_RET_VOID, sk_free_security, struct sock *sk)
LSM_HOOK(void, LSM_RET_VOID, sk_clone_security, const struct sock *sk,
	 struct sock *newsk)
LSM_HOOK(void, LSM_RET_VOID, sk_getsecid, struct sock *sk, u32 *secid)
LSM_HOOK(void, LSM_RET_VOID, sock_graft, struct sock *sk, struct socket *parent)
LSM_HOOK(int, 0, inet_conn_request, const struct sock *sk, struct sk_buff *skb,
	 struct request_sock *req)
LSM_HOOK(void, LSM_RET_VOID, inet_csk_clone, struct sock *newsk,
	 const struct request_sock *req)
LSM_HOOK(void, LSM_RET_VOID, inet_conn_established, struct sock *sk,
	 struct sk_buff *skb)
LSM_HOOK(int, 0, secmark_relabel_packet, u32 secid)
LSM_HOOK(void, LSM_RET_VOID, secmark_refcount_inc, void)
LSM_HOOK(void, LSM_RET_VOID, secmark_refcount_dec, void)
LSM_HOOK(void, LSM_RET_VOID, req_classify_flow, const struct request_sock *req,
	 struct flowi_common *flic)
LSM_HOOK(int, 0, tun_dev_alloc_security, void **security)
LSM_HOOK(void, LSM_RET_VOID, tun_dev_free_security, void *security)
LSM_HOOK(int, 0, tun_dev_create, void)
LSM_HOOK(int, 0, tun_dev_attach_queue, void *security)
LSM_HOOK(int, 0, tun_dev_attach, struct sock *sk, void *security)
LSM_HOOK(int, 0, tun_dev_open, void *security)
LSM_HOOK(int, 0, sctp_assoc_request, struct sctp_endpoint *ep,
	 struct sk_buff *skb)
LSM_HOOK(int, 0, sctp_bind_connect, struct sock *sk, int optname,
	 struct sockaddr *address, int addrlen)
LSM_HOOK(void, LSM_RET_VOID, sctp_sk_clone, struct sctp_endpoint *ep,
	 struct sock *sk, struct sock *newsk)
#endif /* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_INFINIBAND
LSM_HOOK(int, 0, ib_pkey_access, void *sec, u64 subnet_prefix, u16 pkey)
LSM_HOOK(int, 0, ib_endport_manage_subnet, void *sec, const char *dev_name,
	 u8 port_num)
LSM_HOOK(int, 0, ib_alloc_security, void **sec)
LSM_HOOK(void, LSM_RET_VOID, ib_free_security, void *sec)
#endif /* CONFIG_SECURITY_INFINIBAND */

#ifdef CONFIG_SECURITY_NETWORK_XFRM
LSM_HOOK(int, 0, xfrm_policy_alloc_security, struct xfrm_sec_ctx **ctxp,
	 struct xfrm_user_sec_ctx *sec_ctx, gfp_t gfp)
LSM_HOOK(int, 0, xfrm_policy_clone_security, struct xfrm_sec_ctx *old_ctx,
	 struct xfrm_sec_ctx **new_ctx)
LSM_HOOK(void, LSM_RET_VOID, xfrm_policy_free_security,
	 struct xfrm_sec_ctx *ctx)
LSM_HOOK(int, 0, xfrm_policy_delete_security, struct xfrm_sec_ctx *ctx)
LSM_HOOK(int, 0, xfrm_state_alloc, struct xfrm_state *x,
	 struct xfrm_user_sec_ctx *sec_ctx)
LSM_HOOK(int, 0, xfrm_state_alloc_acquire, struct xfrm_state *x,
	 struct xfrm_sec_ctx *polsec, u32 secid)
LSM_HOOK(void, LSM_RET_VOID, xfrm_state_free_security, struct xfrm_state *x)
LSM_HOOK(int, 0, xfrm_state_delete_security, struct xfrm_state *x)
LSM_HOOK(int, 0, xfrm_policy_lookup, struct xfrm_sec_ctx *ctx, u32 fl_secid,
	 u8 dir)
LSM_HOOK(int, 1, xfrm_state_pol_flow_match, struct xfrm_state *x,
	 struct xfrm_policy *xp, const struct flowi_common *flic)
LSM_HOOK(int, 0, xfrm_decode_session, struct sk_buff *skb, u32 *secid,
	 int ckall)
#endif /* CONFIG_SECURITY_NETWORK_XFRM */

/* key management security hooks */
#ifdef CONFIG_KEYS
LSM_HOOK(int, 0, key_alloc, struct key *key, const struct cred *cred,
	 unsigned long flags)
LSM_HOOK(void, LSM_RET_VOID, key_free, struct key *key)
LSM_HOOK(int, 0, key_permission, key_ref_t key_ref, const struct cred *cred,
	 enum key_need_perm need_perm)
LSM_HOOK(int, 0, key_getsecurity, struct key *key, char **_buffer)
#endif /* CONFIG_KEYS */

#ifdef CONFIG_AUDIT
LSM_HOOK(int, 0, audit_rule_init, u32 field, u32 op, char *rulestr,
	 void **lsmrule)
LSM_HOOK(int, 0, audit_rule_known, struct audit_krule *krule)
LSM_HOOK(int, 0, audit_rule_match, u32 secid, u32 field, u32 op, void *lsmrule)
LSM_HOOK(void, LSM_RET_VOID, audit_rule_free, void *lsmrule)
#endif /* CONFIG_AUDIT */

#ifdef CONFIG_BPF_SYSCALL
LSM_HOOK(int, 0, bpf, int cmd, union bpf_attr *attr, unsigned int size)
LSM_HOOK(int, 0, bpf_map, struct bpf_map *map, fmode_t fmode)
LSM_HOOK(int, 0, bpf_prog, struct bpf_prog *prog)
LSM_HOOK(int, 0, bpf_map_alloc_security, struct bpf_map *map)
LSM_HOOK(void, LSM_RET_VOID, bpf_map_free_security, struct bpf_map *map)
LSM_HOOK(int, 0, bpf_prog_alloc_security, struct bpf_prog_aux *aux)
LSM_HOOK(void, LSM_RET_VOID, bpf_prog_free_security, struct bpf_prog_aux *aux)
#endif /* CONFIG_BPF_SYSCALL */

LSM_HOOK(int, 0, locked_down, enum lockdown_reason what)

#ifdef CONFIG_PERF_EVENTS
LSM_HOOK(int, 0, perf_event_open, struct perf_event_attr *attr, int type)
LSM_HOOK(int, 0, perf_event_alloc, struct perf_event *event)
LSM_HOOK(void, LSM_RET_VOID, perf_event_free, struct perf_event *event)
LSM_HOOK(int, 0, perf_event_read, struct perf_event *event)
LSM_HOOK(int, 0, perf_event_write, struct perf_event *event)
#endif /* CONFIG_PERF_EVENTS */
