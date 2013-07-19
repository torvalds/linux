/*
 *  Capabilities Linux Security Module
 *
 *  This is the default security module in case no other module is loaded.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/security.h>

static int cap_syslog(int type)
{
	return 0;
}

static int cap_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	return 0;
}

static int cap_quota_on(struct dentry *dentry)
{
	return 0;
}

static int cap_bprm_check_security(struct linux_binprm *bprm)
{
	return 0;
}

static void cap_bprm_committing_creds(struct linux_binprm *bprm)
{
}

static void cap_bprm_committed_creds(struct linux_binprm *bprm)
{
}

static int cap_sb_alloc_security(struct super_block *sb)
{
	return 0;
}

static void cap_sb_free_security(struct super_block *sb)
{
}

static int cap_sb_copy_data(char *orig, char *copy)
{
	return 0;
}

static int cap_sb_remount(struct super_block *sb, void *data)
{
	return 0;
}

static int cap_sb_kern_mount(struct super_block *sb, int flags, void *data)
{
	return 0;
}

static int cap_sb_show_options(struct seq_file *m, struct super_block *sb)
{
	return 0;
}

static int cap_sb_statfs(struct dentry *dentry)
{
	return 0;
}

static int cap_sb_mount(const char *dev_name, struct path *path,
			const char *type, unsigned long flags, void *data)
{
	return 0;
}

static int cap_sb_umount(struct vfsmount *mnt, int flags)
{
	return 0;
}

static int cap_sb_pivotroot(struct path *old_path, struct path *new_path)
{
	return 0;
}

static int cap_sb_set_mnt_opts(struct super_block *sb,
			       struct security_mnt_opts *opts,
			       unsigned long kern_flags,
			       unsigned long *set_kern_flags)

{
	if (unlikely(opts->num_mnt_opts))
		return -EOPNOTSUPP;
	return 0;
}

static int cap_sb_clone_mnt_opts(const struct super_block *oldsb,
				  struct super_block *newsb)
{
	return 0;
}

static int cap_sb_parse_opts_str(char *options, struct security_mnt_opts *opts)
{
	return 0;
}

static int cap_dentry_init_security(struct dentry *dentry, int mode,
					struct qstr *name, void **ctx,
					u32 *ctxlen)
{
	return 0;
}

static int cap_inode_alloc_security(struct inode *inode)
{
	return 0;
}

static void cap_inode_free_security(struct inode *inode)
{
}

static int cap_inode_init_security(struct inode *inode, struct inode *dir,
				   const struct qstr *qstr, char **name,
				   void **value, size_t *len)
{
	return -EOPNOTSUPP;
}

static int cap_inode_create(struct inode *inode, struct dentry *dentry,
			    umode_t mask)
{
	return 0;
}

static int cap_inode_link(struct dentry *old_dentry, struct inode *inode,
			  struct dentry *new_dentry)
{
	return 0;
}

static int cap_inode_unlink(struct inode *inode, struct dentry *dentry)
{
	return 0;
}

static int cap_inode_symlink(struct inode *inode, struct dentry *dentry,
			     const char *name)
{
	return 0;
}

static int cap_inode_mkdir(struct inode *inode, struct dentry *dentry,
			   umode_t mask)
{
	return 0;
}

static int cap_inode_rmdir(struct inode *inode, struct dentry *dentry)
{
	return 0;
}

static int cap_inode_mknod(struct inode *inode, struct dentry *dentry,
			   umode_t mode, dev_t dev)
{
	return 0;
}

static int cap_inode_rename(struct inode *old_inode, struct dentry *old_dentry,
			    struct inode *new_inode, struct dentry *new_dentry)
{
	return 0;
}

static int cap_inode_readlink(struct dentry *dentry)
{
	return 0;
}

static int cap_inode_follow_link(struct dentry *dentry,
				 struct nameidata *nameidata)
{
	return 0;
}

static int cap_inode_permission(struct inode *inode, int mask)
{
	return 0;
}

static int cap_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	return 0;
}

static int cap_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	return 0;
}

static void cap_inode_post_setxattr(struct dentry *dentry, const char *name,
				    const void *value, size_t size, int flags)
{
}

static int cap_inode_getxattr(struct dentry *dentry, const char *name)
{
	return 0;
}

static int cap_inode_listxattr(struct dentry *dentry)
{
	return 0;
}

static int cap_inode_getsecurity(const struct inode *inode, const char *name,
				 void **buffer, bool alloc)
{
	return -EOPNOTSUPP;
}

static int cap_inode_setsecurity(struct inode *inode, const char *name,
				 const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static int cap_inode_listsecurity(struct inode *inode, char *buffer,
				  size_t buffer_size)
{
	return 0;
}

static void cap_inode_getsecid(const struct inode *inode, u32 *secid)
{
	*secid = 0;
}

#ifdef CONFIG_SECURITY_PATH
static int cap_path_mknod(struct path *dir, struct dentry *dentry, umode_t mode,
			  unsigned int dev)
{
	return 0;
}

static int cap_path_mkdir(struct path *dir, struct dentry *dentry, umode_t mode)
{
	return 0;
}

static int cap_path_rmdir(struct path *dir, struct dentry *dentry)
{
	return 0;
}

static int cap_path_unlink(struct path *dir, struct dentry *dentry)
{
	return 0;
}

static int cap_path_symlink(struct path *dir, struct dentry *dentry,
			    const char *old_name)
{
	return 0;
}

static int cap_path_link(struct dentry *old_dentry, struct path *new_dir,
			 struct dentry *new_dentry)
{
	return 0;
}

static int cap_path_rename(struct path *old_path, struct dentry *old_dentry,
			   struct path *new_path, struct dentry *new_dentry)
{
	return 0;
}

static int cap_path_truncate(struct path *path)
{
	return 0;
}

static int cap_path_chmod(struct path *path, umode_t mode)
{
	return 0;
}

static int cap_path_chown(struct path *path, kuid_t uid, kgid_t gid)
{
	return 0;
}

static int cap_path_chroot(struct path *root)
{
	return 0;
}
#endif

static int cap_file_permission(struct file *file, int mask)
{
	return 0;
}

static int cap_file_alloc_security(struct file *file)
{
	return 0;
}

static void cap_file_free_security(struct file *file)
{
}

static int cap_file_ioctl(struct file *file, unsigned int command,
			  unsigned long arg)
{
	return 0;
}

static int cap_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			     unsigned long prot)
{
	return 0;
}

static int cap_file_lock(struct file *file, unsigned int cmd)
{
	return 0;
}

static int cap_file_fcntl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	return 0;
}

static int cap_file_set_fowner(struct file *file)
{
	return 0;
}

static int cap_file_send_sigiotask(struct task_struct *tsk,
				   struct fown_struct *fown, int sig)
{
	return 0;
}

static int cap_file_receive(struct file *file)
{
	return 0;
}

static int cap_file_open(struct file *file, const struct cred *cred)
{
	return 0;
}

static int cap_task_create(unsigned long clone_flags)
{
	return 0;
}

static void cap_task_free(struct task_struct *task)
{
}

static int cap_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	return 0;
}

static void cap_cred_free(struct cred *cred)
{
}

static int cap_cred_prepare(struct cred *new, const struct cred *old, gfp_t gfp)
{
	return 0;
}

static void cap_cred_transfer(struct cred *new, const struct cred *old)
{
}

static int cap_kernel_act_as(struct cred *new, u32 secid)
{
	return 0;
}

static int cap_kernel_create_files_as(struct cred *new, struct inode *inode)
{
	return 0;
}

static int cap_kernel_module_request(char *kmod_name)
{
	return 0;
}

static int cap_kernel_module_from_file(struct file *file)
{
	return 0;
}

static int cap_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return 0;
}

static int cap_task_getpgid(struct task_struct *p)
{
	return 0;
}

static int cap_task_getsid(struct task_struct *p)
{
	return 0;
}

static void cap_task_getsecid(struct task_struct *p, u32 *secid)
{
	*secid = 0;
}

static int cap_task_getioprio(struct task_struct *p)
{
	return 0;
}

static int cap_task_setrlimit(struct task_struct *p, unsigned int resource,
		struct rlimit *new_rlim)
{
	return 0;
}

static int cap_task_getscheduler(struct task_struct *p)
{
	return 0;
}

static int cap_task_movememory(struct task_struct *p)
{
	return 0;
}

static int cap_task_wait(struct task_struct *p)
{
	return 0;
}

static int cap_task_kill(struct task_struct *p, struct siginfo *info,
			 int sig, u32 secid)
{
	return 0;
}

static void cap_task_to_inode(struct task_struct *p, struct inode *inode)
{
}

static int cap_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	return 0;
}

static void cap_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	*secid = 0;
}

static int cap_msg_msg_alloc_security(struct msg_msg *msg)
{
	return 0;
}

static void cap_msg_msg_free_security(struct msg_msg *msg)
{
}

static int cap_msg_queue_alloc_security(struct msg_queue *msq)
{
	return 0;
}

static void cap_msg_queue_free_security(struct msg_queue *msq)
{
}

static int cap_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	return 0;
}

static int cap_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	return 0;
}

static int cap_msg_queue_msgsnd(struct msg_queue *msq, struct msg_msg *msg,
				int msgflg)
{
	return 0;
}

static int cap_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
				struct task_struct *target, long type, int mode)
{
	return 0;
}

static int cap_shm_alloc_security(struct shmid_kernel *shp)
{
	return 0;
}

static void cap_shm_free_security(struct shmid_kernel *shp)
{
}

static int cap_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	return 0;
}

static int cap_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	return 0;
}

static int cap_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr,
			 int shmflg)
{
	return 0;
}

static int cap_sem_alloc_security(struct sem_array *sma)
{
	return 0;
}

static void cap_sem_free_security(struct sem_array *sma)
{
}

static int cap_sem_associate(struct sem_array *sma, int semflg)
{
	return 0;
}

static int cap_sem_semctl(struct sem_array *sma, int cmd)
{
	return 0;
}

static int cap_sem_semop(struct sem_array *sma, struct sembuf *sops,
			 unsigned nsops, int alter)
{
	return 0;
}

#ifdef CONFIG_SECURITY_NETWORK
static int cap_unix_stream_connect(struct sock *sock, struct sock *other,
				   struct sock *newsk)
{
	return 0;
}

static int cap_unix_may_send(struct socket *sock, struct socket *other)
{
	return 0;
}

static int cap_socket_create(int family, int type, int protocol, int kern)
{
	return 0;
}

static int cap_socket_post_create(struct socket *sock, int family, int type,
				  int protocol, int kern)
{
	return 0;
}

static int cap_socket_bind(struct socket *sock, struct sockaddr *address,
			   int addrlen)
{
	return 0;
}

static int cap_socket_connect(struct socket *sock, struct sockaddr *address,
			      int addrlen)
{
	return 0;
}

static int cap_socket_listen(struct socket *sock, int backlog)
{
	return 0;
}

static int cap_socket_accept(struct socket *sock, struct socket *newsock)
{
	return 0;
}

static int cap_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	return 0;
}

static int cap_socket_recvmsg(struct socket *sock, struct msghdr *msg,
			      int size, int flags)
{
	return 0;
}

static int cap_socket_getsockname(struct socket *sock)
{
	return 0;
}

static int cap_socket_getpeername(struct socket *sock)
{
	return 0;
}

static int cap_socket_setsockopt(struct socket *sock, int level, int optname)
{
	return 0;
}

static int cap_socket_getsockopt(struct socket *sock, int level, int optname)
{
	return 0;
}

static int cap_socket_shutdown(struct socket *sock, int how)
{
	return 0;
}

static int cap_socket_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

static int cap_socket_getpeersec_stream(struct socket *sock,
					char __user *optval,
					int __user *optlen, unsigned len)
{
	return -ENOPROTOOPT;
}

static int cap_socket_getpeersec_dgram(struct socket *sock,
				       struct sk_buff *skb, u32 *secid)
{
	return -ENOPROTOOPT;
}

static int cap_sk_alloc_security(struct sock *sk, int family, gfp_t priority)
{
	return 0;
}

static void cap_sk_free_security(struct sock *sk)
{
}

static void cap_sk_clone_security(const struct sock *sk, struct sock *newsk)
{
}

static void cap_sk_getsecid(struct sock *sk, u32 *secid)
{
}

static void cap_sock_graft(struct sock *sk, struct socket *parent)
{
}

static int cap_inet_conn_request(struct sock *sk, struct sk_buff *skb,
				 struct request_sock *req)
{
	return 0;
}

static void cap_inet_csk_clone(struct sock *newsk,
			       const struct request_sock *req)
{
}

static void cap_inet_conn_established(struct sock *sk, struct sk_buff *skb)
{
}

static int cap_secmark_relabel_packet(u32 secid)
{
	return 0;
}

static void cap_secmark_refcount_inc(void)
{
}

static void cap_secmark_refcount_dec(void)
{
}

static void cap_req_classify_flow(const struct request_sock *req,
				  struct flowi *fl)
{
}

static int cap_tun_dev_alloc_security(void **security)
{
	return 0;
}

static void cap_tun_dev_free_security(void *security)
{
}

static int cap_tun_dev_create(void)
{
	return 0;
}

static int cap_tun_dev_attach_queue(void *security)
{
	return 0;
}

static int cap_tun_dev_attach(struct sock *sk, void *security)
{
	return 0;
}

static int cap_tun_dev_open(void *security)
{
	return 0;
}

static void cap_skb_owned_by(struct sk_buff *skb, struct sock *sk)
{
}

#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM
static int cap_xfrm_policy_alloc_security(struct xfrm_sec_ctx **ctxp,
					  struct xfrm_user_sec_ctx *sec_ctx)
{
	return 0;
}

static int cap_xfrm_policy_clone_security(struct xfrm_sec_ctx *old_ctx,
					  struct xfrm_sec_ctx **new_ctxp)
{
	return 0;
}

static void cap_xfrm_policy_free_security(struct xfrm_sec_ctx *ctx)
{
}

static int cap_xfrm_policy_delete_security(struct xfrm_sec_ctx *ctx)
{
	return 0;
}

static int cap_xfrm_state_alloc_security(struct xfrm_state *x,
					 struct xfrm_user_sec_ctx *sec_ctx,
					 u32 secid)
{
	return 0;
}

static void cap_xfrm_state_free_security(struct xfrm_state *x)
{
}

static int cap_xfrm_state_delete_security(struct xfrm_state *x)
{
	return 0;
}

static int cap_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 sk_sid, u8 dir)
{
	return 0;
}

static int cap_xfrm_state_pol_flow_match(struct xfrm_state *x,
					 struct xfrm_policy *xp,
					 const struct flowi *fl)
{
	return 1;
}

static int cap_xfrm_decode_session(struct sk_buff *skb, u32 *fl, int ckall)
{
	return 0;
}

#endif /* CONFIG_SECURITY_NETWORK_XFRM */
static void cap_d_instantiate(struct dentry *dentry, struct inode *inode)
{
}

static int cap_getprocattr(struct task_struct *p, char *name, char **value)
{
	return -EINVAL;
}

static int cap_setprocattr(struct task_struct *p, char *name, void *value,
			   size_t size)
{
	return -EINVAL;
}

static int cap_ismaclabel(const char *name)
{
	return 0;
}

static int cap_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return -EOPNOTSUPP;
}

static int cap_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	*secid = 0;
	return 0;
}

static void cap_release_secctx(char *secdata, u32 seclen)
{
}

static int cap_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	return 0;
}

static int cap_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return 0;
}

static int cap_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	return -EOPNOTSUPP;
}
#ifdef CONFIG_KEYS
static int cap_key_alloc(struct key *key, const struct cred *cred,
			 unsigned long flags)
{
	return 0;
}

static void cap_key_free(struct key *key)
{
}

static int cap_key_permission(key_ref_t key_ref, const struct cred *cred,
			      key_perm_t perm)
{
	return 0;
}

static int cap_key_getsecurity(struct key *key, char **_buffer)
{
	*_buffer = NULL;
	return 0;
}

#endif /* CONFIG_KEYS */

#ifdef CONFIG_AUDIT
static int cap_audit_rule_init(u32 field, u32 op, char *rulestr, void **lsmrule)
{
	return 0;
}

static int cap_audit_rule_known(struct audit_krule *krule)
{
	return 0;
}

static int cap_audit_rule_match(u32 secid, u32 field, u32 op, void *lsmrule,
				struct audit_context *actx)
{
	return 0;
}

static void cap_audit_rule_free(void *lsmrule)
{
}
#endif /* CONFIG_AUDIT */

#define set_to_cap_if_null(ops, function)				\
	do {								\
		if (!ops->function) {					\
			ops->function = cap_##function;			\
			pr_debug("Had to override the " #function	\
				 " security operation with the default.\n");\
			}						\
	} while (0)

void __init security_fixup_ops(struct security_operations *ops)
{
	set_to_cap_if_null(ops, ptrace_access_check);
	set_to_cap_if_null(ops, ptrace_traceme);
	set_to_cap_if_null(ops, capget);
	set_to_cap_if_null(ops, capset);
	set_to_cap_if_null(ops, capable);
	set_to_cap_if_null(ops, quotactl);
	set_to_cap_if_null(ops, quota_on);
	set_to_cap_if_null(ops, syslog);
	set_to_cap_if_null(ops, settime);
	set_to_cap_if_null(ops, vm_enough_memory);
	set_to_cap_if_null(ops, bprm_set_creds);
	set_to_cap_if_null(ops, bprm_committing_creds);
	set_to_cap_if_null(ops, bprm_committed_creds);
	set_to_cap_if_null(ops, bprm_check_security);
	set_to_cap_if_null(ops, bprm_secureexec);
	set_to_cap_if_null(ops, sb_alloc_security);
	set_to_cap_if_null(ops, sb_free_security);
	set_to_cap_if_null(ops, sb_copy_data);
	set_to_cap_if_null(ops, sb_remount);
	set_to_cap_if_null(ops, sb_kern_mount);
	set_to_cap_if_null(ops, sb_show_options);
	set_to_cap_if_null(ops, sb_statfs);
	set_to_cap_if_null(ops, sb_mount);
	set_to_cap_if_null(ops, sb_umount);
	set_to_cap_if_null(ops, sb_pivotroot);
	set_to_cap_if_null(ops, sb_set_mnt_opts);
	set_to_cap_if_null(ops, sb_clone_mnt_opts);
	set_to_cap_if_null(ops, sb_parse_opts_str);
	set_to_cap_if_null(ops, dentry_init_security);
	set_to_cap_if_null(ops, inode_alloc_security);
	set_to_cap_if_null(ops, inode_free_security);
	set_to_cap_if_null(ops, inode_init_security);
	set_to_cap_if_null(ops, inode_create);
	set_to_cap_if_null(ops, inode_link);
	set_to_cap_if_null(ops, inode_unlink);
	set_to_cap_if_null(ops, inode_symlink);
	set_to_cap_if_null(ops, inode_mkdir);
	set_to_cap_if_null(ops, inode_rmdir);
	set_to_cap_if_null(ops, inode_mknod);
	set_to_cap_if_null(ops, inode_rename);
	set_to_cap_if_null(ops, inode_readlink);
	set_to_cap_if_null(ops, inode_follow_link);
	set_to_cap_if_null(ops, inode_permission);
	set_to_cap_if_null(ops, inode_setattr);
	set_to_cap_if_null(ops, inode_getattr);
	set_to_cap_if_null(ops, inode_setxattr);
	set_to_cap_if_null(ops, inode_post_setxattr);
	set_to_cap_if_null(ops, inode_getxattr);
	set_to_cap_if_null(ops, inode_listxattr);
	set_to_cap_if_null(ops, inode_removexattr);
	set_to_cap_if_null(ops, inode_need_killpriv);
	set_to_cap_if_null(ops, inode_killpriv);
	set_to_cap_if_null(ops, inode_getsecurity);
	set_to_cap_if_null(ops, inode_setsecurity);
	set_to_cap_if_null(ops, inode_listsecurity);
	set_to_cap_if_null(ops, inode_getsecid);
#ifdef CONFIG_SECURITY_PATH
	set_to_cap_if_null(ops, path_mknod);
	set_to_cap_if_null(ops, path_mkdir);
	set_to_cap_if_null(ops, path_rmdir);
	set_to_cap_if_null(ops, path_unlink);
	set_to_cap_if_null(ops, path_symlink);
	set_to_cap_if_null(ops, path_link);
	set_to_cap_if_null(ops, path_rename);
	set_to_cap_if_null(ops, path_truncate);
	set_to_cap_if_null(ops, path_chmod);
	set_to_cap_if_null(ops, path_chown);
	set_to_cap_if_null(ops, path_chroot);
#endif
	set_to_cap_if_null(ops, file_permission);
	set_to_cap_if_null(ops, file_alloc_security);
	set_to_cap_if_null(ops, file_free_security);
	set_to_cap_if_null(ops, file_ioctl);
	set_to_cap_if_null(ops, mmap_addr);
	set_to_cap_if_null(ops, mmap_file);
	set_to_cap_if_null(ops, file_mprotect);
	set_to_cap_if_null(ops, file_lock);
	set_to_cap_if_null(ops, file_fcntl);
	set_to_cap_if_null(ops, file_set_fowner);
	set_to_cap_if_null(ops, file_send_sigiotask);
	set_to_cap_if_null(ops, file_receive);
	set_to_cap_if_null(ops, file_open);
	set_to_cap_if_null(ops, task_create);
	set_to_cap_if_null(ops, task_free);
	set_to_cap_if_null(ops, cred_alloc_blank);
	set_to_cap_if_null(ops, cred_free);
	set_to_cap_if_null(ops, cred_prepare);
	set_to_cap_if_null(ops, cred_transfer);
	set_to_cap_if_null(ops, kernel_act_as);
	set_to_cap_if_null(ops, kernel_create_files_as);
	set_to_cap_if_null(ops, kernel_module_request);
	set_to_cap_if_null(ops, kernel_module_from_file);
	set_to_cap_if_null(ops, task_fix_setuid);
	set_to_cap_if_null(ops, task_setpgid);
	set_to_cap_if_null(ops, task_getpgid);
	set_to_cap_if_null(ops, task_getsid);
	set_to_cap_if_null(ops, task_getsecid);
	set_to_cap_if_null(ops, task_setnice);
	set_to_cap_if_null(ops, task_setioprio);
	set_to_cap_if_null(ops, task_getioprio);
	set_to_cap_if_null(ops, task_setrlimit);
	set_to_cap_if_null(ops, task_setscheduler);
	set_to_cap_if_null(ops, task_getscheduler);
	set_to_cap_if_null(ops, task_movememory);
	set_to_cap_if_null(ops, task_wait);
	set_to_cap_if_null(ops, task_kill);
	set_to_cap_if_null(ops, task_prctl);
	set_to_cap_if_null(ops, task_to_inode);
	set_to_cap_if_null(ops, ipc_permission);
	set_to_cap_if_null(ops, ipc_getsecid);
	set_to_cap_if_null(ops, msg_msg_alloc_security);
	set_to_cap_if_null(ops, msg_msg_free_security);
	set_to_cap_if_null(ops, msg_queue_alloc_security);
	set_to_cap_if_null(ops, msg_queue_free_security);
	set_to_cap_if_null(ops, msg_queue_associate);
	set_to_cap_if_null(ops, msg_queue_msgctl);
	set_to_cap_if_null(ops, msg_queue_msgsnd);
	set_to_cap_if_null(ops, msg_queue_msgrcv);
	set_to_cap_if_null(ops, shm_alloc_security);
	set_to_cap_if_null(ops, shm_free_security);
	set_to_cap_if_null(ops, shm_associate);
	set_to_cap_if_null(ops, shm_shmctl);
	set_to_cap_if_null(ops, shm_shmat);
	set_to_cap_if_null(ops, sem_alloc_security);
	set_to_cap_if_null(ops, sem_free_security);
	set_to_cap_if_null(ops, sem_associate);
	set_to_cap_if_null(ops, sem_semctl);
	set_to_cap_if_null(ops, sem_semop);
	set_to_cap_if_null(ops, netlink_send);
	set_to_cap_if_null(ops, d_instantiate);
	set_to_cap_if_null(ops, getprocattr);
	set_to_cap_if_null(ops, setprocattr);
	set_to_cap_if_null(ops, ismaclabel);
	set_to_cap_if_null(ops, secid_to_secctx);
	set_to_cap_if_null(ops, secctx_to_secid);
	set_to_cap_if_null(ops, release_secctx);
	set_to_cap_if_null(ops, inode_notifysecctx);
	set_to_cap_if_null(ops, inode_setsecctx);
	set_to_cap_if_null(ops, inode_getsecctx);
#ifdef CONFIG_SECURITY_NETWORK
	set_to_cap_if_null(ops, unix_stream_connect);
	set_to_cap_if_null(ops, unix_may_send);
	set_to_cap_if_null(ops, socket_create);
	set_to_cap_if_null(ops, socket_post_create);
	set_to_cap_if_null(ops, socket_bind);
	set_to_cap_if_null(ops, socket_connect);
	set_to_cap_if_null(ops, socket_listen);
	set_to_cap_if_null(ops, socket_accept);
	set_to_cap_if_null(ops, socket_sendmsg);
	set_to_cap_if_null(ops, socket_recvmsg);
	set_to_cap_if_null(ops, socket_getsockname);
	set_to_cap_if_null(ops, socket_getpeername);
	set_to_cap_if_null(ops, socket_setsockopt);
	set_to_cap_if_null(ops, socket_getsockopt);
	set_to_cap_if_null(ops, socket_shutdown);
	set_to_cap_if_null(ops, socket_sock_rcv_skb);
	set_to_cap_if_null(ops, socket_getpeersec_stream);
	set_to_cap_if_null(ops, socket_getpeersec_dgram);
	set_to_cap_if_null(ops, sk_alloc_security);
	set_to_cap_if_null(ops, sk_free_security);
	set_to_cap_if_null(ops, sk_clone_security);
	set_to_cap_if_null(ops, sk_getsecid);
	set_to_cap_if_null(ops, sock_graft);
	set_to_cap_if_null(ops, inet_conn_request);
	set_to_cap_if_null(ops, inet_csk_clone);
	set_to_cap_if_null(ops, inet_conn_established);
	set_to_cap_if_null(ops, secmark_relabel_packet);
	set_to_cap_if_null(ops, secmark_refcount_inc);
	set_to_cap_if_null(ops, secmark_refcount_dec);
	set_to_cap_if_null(ops, req_classify_flow);
	set_to_cap_if_null(ops, tun_dev_alloc_security);
	set_to_cap_if_null(ops, tun_dev_free_security);
	set_to_cap_if_null(ops, tun_dev_create);
	set_to_cap_if_null(ops, tun_dev_open);
	set_to_cap_if_null(ops, tun_dev_attach_queue);
	set_to_cap_if_null(ops, tun_dev_attach);
	set_to_cap_if_null(ops, skb_owned_by);
#endif	/* CONFIG_SECURITY_NETWORK */
#ifdef CONFIG_SECURITY_NETWORK_XFRM
	set_to_cap_if_null(ops, xfrm_policy_alloc_security);
	set_to_cap_if_null(ops, xfrm_policy_clone_security);
	set_to_cap_if_null(ops, xfrm_policy_free_security);
	set_to_cap_if_null(ops, xfrm_policy_delete_security);
	set_to_cap_if_null(ops, xfrm_state_alloc_security);
	set_to_cap_if_null(ops, xfrm_state_free_security);
	set_to_cap_if_null(ops, xfrm_state_delete_security);
	set_to_cap_if_null(ops, xfrm_policy_lookup);
	set_to_cap_if_null(ops, xfrm_state_pol_flow_match);
	set_to_cap_if_null(ops, xfrm_decode_session);
#endif	/* CONFIG_SECURITY_NETWORK_XFRM */
#ifdef CONFIG_KEYS
	set_to_cap_if_null(ops, key_alloc);
	set_to_cap_if_null(ops, key_free);
	set_to_cap_if_null(ops, key_permission);
	set_to_cap_if_null(ops, key_getsecurity);
#endif	/* CONFIG_KEYS */
#ifdef CONFIG_AUDIT
	set_to_cap_if_null(ops, audit_rule_init);
	set_to_cap_if_null(ops, audit_rule_known);
	set_to_cap_if_null(ops, audit_rule_match);
	set_to_cap_if_null(ops, audit_rule_free);
#endif
}
