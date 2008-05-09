/*
 * Stub functions for the default security function pointers in case no
 * security model is loaded.
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002  Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#undef DEBUG

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <linux/xattr.h>
#include <linux/hugetlb.h>
#include <linux/ptrace.h>
#include <linux/file.h>

static int dummy_ptrace (struct task_struct *parent, struct task_struct *child)
{
	return 0;
}

static int dummy_capget (struct task_struct *target, kernel_cap_t * effective,
			 kernel_cap_t * inheritable, kernel_cap_t * permitted)
{
	if (target->euid == 0) {
		cap_set_full(*permitted);
		cap_set_init_eff(*effective);
	} else {
		cap_clear(*permitted);
		cap_clear(*effective);
	}

	cap_clear(*inheritable);

	if (target->fsuid != 0) {
		*permitted = cap_drop_fs_set(*permitted);
		*effective = cap_drop_fs_set(*effective);
	}
	return 0;
}

static int dummy_capset_check (struct task_struct *target,
			       kernel_cap_t * effective,
			       kernel_cap_t * inheritable,
			       kernel_cap_t * permitted)
{
	return -EPERM;
}

static void dummy_capset_set (struct task_struct *target,
			      kernel_cap_t * effective,
			      kernel_cap_t * inheritable,
			      kernel_cap_t * permitted)
{
	return;
}

static int dummy_acct (struct file *file)
{
	return 0;
}

static int dummy_capable (struct task_struct *tsk, int cap)
{
	if (cap_raised (tsk->cap_effective, cap))
		return 0;
	return -EPERM;
}

static int dummy_sysctl (ctl_table * table, int op)
{
	return 0;
}

static int dummy_quotactl (int cmds, int type, int id, struct super_block *sb)
{
	return 0;
}

static int dummy_quota_on (struct dentry *dentry)
{
	return 0;
}

static int dummy_syslog (int type)
{
	if ((type != 3 && type != 10) && current->euid)
		return -EPERM;
	return 0;
}

static int dummy_settime(struct timespec *ts, struct timezone *tz)
{
	if (!capable(CAP_SYS_TIME))
		return -EPERM;
	return 0;
}

static int dummy_vm_enough_memory(struct mm_struct *mm, long pages)
{
	int cap_sys_admin = 0;

	if (dummy_capable(current, CAP_SYS_ADMIN) == 0)
		cap_sys_admin = 1;
	return __vm_enough_memory(mm, pages, cap_sys_admin);
}

static int dummy_bprm_alloc_security (struct linux_binprm *bprm)
{
	return 0;
}

static void dummy_bprm_free_security (struct linux_binprm *bprm)
{
	return;
}

static void dummy_bprm_apply_creds (struct linux_binprm *bprm, int unsafe)
{
	if (bprm->e_uid != current->uid || bprm->e_gid != current->gid) {
		set_dumpable(current->mm, suid_dumpable);

		if ((unsafe & ~LSM_UNSAFE_PTRACE_CAP) && !capable(CAP_SETUID)) {
			bprm->e_uid = current->uid;
			bprm->e_gid = current->gid;
		}
	}

	current->suid = current->euid = current->fsuid = bprm->e_uid;
	current->sgid = current->egid = current->fsgid = bprm->e_gid;

	dummy_capget(current, &current->cap_effective, &current->cap_inheritable, &current->cap_permitted);
}

static void dummy_bprm_post_apply_creds (struct linux_binprm *bprm)
{
	return;
}

static int dummy_bprm_set_security (struct linux_binprm *bprm)
{
	return 0;
}

static int dummy_bprm_check_security (struct linux_binprm *bprm)
{
	return 0;
}

static int dummy_bprm_secureexec (struct linux_binprm *bprm)
{
	/* The new userland will simply use the value provided
	   in the AT_SECURE field to decide whether secure mode
	   is required.  Hence, this logic is required to preserve
	   the legacy decision algorithm used by the old userland. */
	return (current->euid != current->uid ||
		current->egid != current->gid);
}

static int dummy_sb_alloc_security (struct super_block *sb)
{
	return 0;
}

static void dummy_sb_free_security (struct super_block *sb)
{
	return;
}

static int dummy_sb_copy_data (char *orig, char *copy)
{
	return 0;
}

static int dummy_sb_kern_mount (struct super_block *sb, void *data)
{
	return 0;
}

static int dummy_sb_statfs (struct dentry *dentry)
{
	return 0;
}

static int dummy_sb_mount (char *dev_name, struct path *path, char *type,
			   unsigned long flags, void *data)
{
	return 0;
}

static int dummy_sb_check_sb (struct vfsmount *mnt, struct path *path)
{
	return 0;
}

static int dummy_sb_umount (struct vfsmount *mnt, int flags)
{
	return 0;
}

static void dummy_sb_umount_close (struct vfsmount *mnt)
{
	return;
}

static void dummy_sb_umount_busy (struct vfsmount *mnt)
{
	return;
}

static void dummy_sb_post_remount (struct vfsmount *mnt, unsigned long flags,
				   void *data)
{
	return;
}


static void dummy_sb_post_addmount (struct vfsmount *mnt, struct path *path)
{
	return;
}

static int dummy_sb_pivotroot (struct path *old_path, struct path *new_path)
{
	return 0;
}

static void dummy_sb_post_pivotroot (struct path *old_path, struct path *new_path)
{
	return;
}

static int dummy_sb_get_mnt_opts(const struct super_block *sb,
				 struct security_mnt_opts *opts)
{
	security_init_mnt_opts(opts);
	return 0;
}

static int dummy_sb_set_mnt_opts(struct super_block *sb,
				 struct security_mnt_opts *opts)
{
	if (unlikely(opts->num_mnt_opts))
		return -EOPNOTSUPP;
	return 0;
}

static void dummy_sb_clone_mnt_opts(const struct super_block *oldsb,
				    struct super_block *newsb)
{
	return;
}

static int dummy_sb_parse_opts_str(char *options, struct security_mnt_opts *opts)
{
	return 0;
}

static int dummy_inode_alloc_security (struct inode *inode)
{
	return 0;
}

static void dummy_inode_free_security (struct inode *inode)
{
	return;
}

static int dummy_inode_init_security (struct inode *inode, struct inode *dir,
				      char **name, void **value, size_t *len)
{
	return -EOPNOTSUPP;
}

static int dummy_inode_create (struct inode *inode, struct dentry *dentry,
			       int mask)
{
	return 0;
}

static int dummy_inode_link (struct dentry *old_dentry, struct inode *inode,
			     struct dentry *new_dentry)
{
	return 0;
}

static int dummy_inode_unlink (struct inode *inode, struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_symlink (struct inode *inode, struct dentry *dentry,
				const char *name)
{
	return 0;
}

static int dummy_inode_mkdir (struct inode *inode, struct dentry *dentry,
			      int mask)
{
	return 0;
}

static int dummy_inode_rmdir (struct inode *inode, struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_mknod (struct inode *inode, struct dentry *dentry,
			      int mode, dev_t dev)
{
	return 0;
}

static int dummy_inode_rename (struct inode *old_inode,
			       struct dentry *old_dentry,
			       struct inode *new_inode,
			       struct dentry *new_dentry)
{
	return 0;
}

static int dummy_inode_readlink (struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_follow_link (struct dentry *dentry,
				    struct nameidata *nameidata)
{
	return 0;
}

static int dummy_inode_permission (struct inode *inode, int mask, struct nameidata *nd)
{
	return 0;
}

static int dummy_inode_setattr (struct dentry *dentry, struct iattr *iattr)
{
	return 0;
}

static int dummy_inode_getattr (struct vfsmount *mnt, struct dentry *dentry)
{
	return 0;
}

static void dummy_inode_delete (struct inode *ino)
{
	return;
}

static int dummy_inode_setxattr (struct dentry *dentry, const char *name,
				 const void *value, size_t size, int flags)
{
	if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

static void dummy_inode_post_setxattr (struct dentry *dentry, const char *name,
				       const void *value, size_t size,
				       int flags)
{
}

static int dummy_inode_getxattr (struct dentry *dentry, const char *name)
{
	return 0;
}

static int dummy_inode_listxattr (struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_removexattr (struct dentry *dentry, const char *name)
{
	if (!strncmp(name, XATTR_SECURITY_PREFIX,
		     sizeof(XATTR_SECURITY_PREFIX) - 1) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;
	return 0;
}

static int dummy_inode_need_killpriv(struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_killpriv(struct dentry *dentry)
{
	return 0;
}

static int dummy_inode_getsecurity(const struct inode *inode, const char *name, void **buffer, bool alloc)
{
	return -EOPNOTSUPP;
}

static int dummy_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static int dummy_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	return 0;
}

static void dummy_inode_getsecid(const struct inode *inode, u32 *secid)
{
	*secid = 0;
}

static int dummy_file_permission (struct file *file, int mask)
{
	return 0;
}

static int dummy_file_alloc_security (struct file *file)
{
	return 0;
}

static void dummy_file_free_security (struct file *file)
{
	return;
}

static int dummy_file_ioctl (struct file *file, unsigned int command,
			     unsigned long arg)
{
	return 0;
}

static int dummy_file_mmap (struct file *file, unsigned long reqprot,
			    unsigned long prot,
			    unsigned long flags,
			    unsigned long addr,
			    unsigned long addr_only)
{
	if ((addr < mmap_min_addr) && !capable(CAP_SYS_RAWIO))
		return -EACCES;
	return 0;
}

static int dummy_file_mprotect (struct vm_area_struct *vma,
				unsigned long reqprot,
				unsigned long prot)
{
	return 0;
}

static int dummy_file_lock (struct file *file, unsigned int cmd)
{
	return 0;
}

static int dummy_file_fcntl (struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return 0;
}

static int dummy_file_set_fowner (struct file *file)
{
	return 0;
}

static int dummy_file_send_sigiotask (struct task_struct *tsk,
				      struct fown_struct *fown, int sig)
{
	return 0;
}

static int dummy_file_receive (struct file *file)
{
	return 0;
}

static int dummy_dentry_open (struct file *file)
{
	return 0;
}

static int dummy_task_create (unsigned long clone_flags)
{
	return 0;
}

static int dummy_task_alloc_security (struct task_struct *p)
{
	return 0;
}

static void dummy_task_free_security (struct task_struct *p)
{
	return;
}

static int dummy_task_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return 0;
}

static int dummy_task_post_setuid (uid_t id0, uid_t id1, uid_t id2, int flags)
{
	dummy_capget(current, &current->cap_effective, &current->cap_inheritable, &current->cap_permitted);
	return 0;
}

static int dummy_task_setgid (gid_t id0, gid_t id1, gid_t id2, int flags)
{
	return 0;
}

static int dummy_task_setpgid (struct task_struct *p, pid_t pgid)
{
	return 0;
}

static int dummy_task_getpgid (struct task_struct *p)
{
	return 0;
}

static int dummy_task_getsid (struct task_struct *p)
{
	return 0;
}

static void dummy_task_getsecid (struct task_struct *p, u32 *secid)
{
	*secid = 0;
}

static int dummy_task_setgroups (struct group_info *group_info)
{
	return 0;
}

static int dummy_task_setnice (struct task_struct *p, int nice)
{
	return 0;
}

static int dummy_task_setioprio (struct task_struct *p, int ioprio)
{
	return 0;
}

static int dummy_task_getioprio (struct task_struct *p)
{
	return 0;
}

static int dummy_task_setrlimit (unsigned int resource, struct rlimit *new_rlim)
{
	return 0;
}

static int dummy_task_setscheduler (struct task_struct *p, int policy,
				    struct sched_param *lp)
{
	return 0;
}

static int dummy_task_getscheduler (struct task_struct *p)
{
	return 0;
}

static int dummy_task_movememory (struct task_struct *p)
{
	return 0;
}

static int dummy_task_wait (struct task_struct *p)
{
	return 0;
}

static int dummy_task_kill (struct task_struct *p, struct siginfo *info,
			    int sig, u32 secid)
{
	return 0;
}

static int dummy_task_prctl (int option, unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5, long *rc_p)
{
	return 0;
}

static void dummy_task_reparent_to_init (struct task_struct *p)
{
	p->euid = p->fsuid = 0;
	return;
}

static void dummy_task_to_inode(struct task_struct *p, struct inode *inode)
{ }

static int dummy_ipc_permission (struct kern_ipc_perm *ipcp, short flag)
{
	return 0;
}

static void dummy_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	*secid = 0;
}

static int dummy_msg_msg_alloc_security (struct msg_msg *msg)
{
	return 0;
}

static void dummy_msg_msg_free_security (struct msg_msg *msg)
{
	return;
}

static int dummy_msg_queue_alloc_security (struct msg_queue *msq)
{
	return 0;
}

static void dummy_msg_queue_free_security (struct msg_queue *msq)
{
	return;
}

static int dummy_msg_queue_associate (struct msg_queue *msq, 
				      int msqflg)
{
	return 0;
}

static int dummy_msg_queue_msgctl (struct msg_queue *msq, int cmd)
{
	return 0;
}

static int dummy_msg_queue_msgsnd (struct msg_queue *msq, struct msg_msg *msg,
				   int msgflg)
{
	return 0;
}

static int dummy_msg_queue_msgrcv (struct msg_queue *msq, struct msg_msg *msg,
				   struct task_struct *target, long type,
				   int mode)
{
	return 0;
}

static int dummy_shm_alloc_security (struct shmid_kernel *shp)
{
	return 0;
}

static void dummy_shm_free_security (struct shmid_kernel *shp)
{
	return;
}

static int dummy_shm_associate (struct shmid_kernel *shp, int shmflg)
{
	return 0;
}

static int dummy_shm_shmctl (struct shmid_kernel *shp, int cmd)
{
	return 0;
}

static int dummy_shm_shmat (struct shmid_kernel *shp, char __user *shmaddr,
			    int shmflg)
{
	return 0;
}

static int dummy_sem_alloc_security (struct sem_array *sma)
{
	return 0;
}

static void dummy_sem_free_security (struct sem_array *sma)
{
	return;
}

static int dummy_sem_associate (struct sem_array *sma, int semflg)
{
	return 0;
}

static int dummy_sem_semctl (struct sem_array *sma, int cmd)
{
	return 0;
}

static int dummy_sem_semop (struct sem_array *sma, 
			    struct sembuf *sops, unsigned nsops, int alter)
{
	return 0;
}

static int dummy_netlink_send (struct sock *sk, struct sk_buff *skb)
{
	NETLINK_CB(skb).eff_cap = current->cap_effective;
	return 0;
}

static int dummy_netlink_recv (struct sk_buff *skb, int cap)
{
	if (!cap_raised (NETLINK_CB (skb).eff_cap, cap))
		return -EPERM;
	return 0;
}

#ifdef CONFIG_SECURITY_NETWORK
static int dummy_unix_stream_connect (struct socket *sock,
				      struct socket *other,
				      struct sock *newsk)
{
	return 0;
}

static int dummy_unix_may_send (struct socket *sock,
				struct socket *other)
{
	return 0;
}

static int dummy_socket_create (int family, int type,
				int protocol, int kern)
{
	return 0;
}

static int dummy_socket_post_create (struct socket *sock, int family, int type,
				     int protocol, int kern)
{
	return 0;
}

static int dummy_socket_bind (struct socket *sock, struct sockaddr *address,
			      int addrlen)
{
	return 0;
}

static int dummy_socket_connect (struct socket *sock, struct sockaddr *address,
				 int addrlen)
{
	return 0;
}

static int dummy_socket_listen (struct socket *sock, int backlog)
{
	return 0;
}

static int dummy_socket_accept (struct socket *sock, struct socket *newsock)
{
	return 0;
}

static void dummy_socket_post_accept (struct socket *sock, 
				      struct socket *newsock)
{
	return;
}

static int dummy_socket_sendmsg (struct socket *sock, struct msghdr *msg,
				 int size)
{
	return 0;
}

static int dummy_socket_recvmsg (struct socket *sock, struct msghdr *msg,
				 int size, int flags)
{
	return 0;
}

static int dummy_socket_getsockname (struct socket *sock)
{
	return 0;
}

static int dummy_socket_getpeername (struct socket *sock)
{
	return 0;
}

static int dummy_socket_setsockopt (struct socket *sock, int level, int optname)
{
	return 0;
}

static int dummy_socket_getsockopt (struct socket *sock, int level, int optname)
{
	return 0;
}

static int dummy_socket_shutdown (struct socket *sock, int how)
{
	return 0;
}

static int dummy_socket_sock_rcv_skb (struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

static int dummy_socket_getpeersec_stream(struct socket *sock, char __user *optval,
					  int __user *optlen, unsigned len)
{
	return -ENOPROTOOPT;
}

static int dummy_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return -ENOPROTOOPT;
}

static inline int dummy_sk_alloc_security (struct sock *sk, int family, gfp_t priority)
{
	return 0;
}

static inline void dummy_sk_free_security (struct sock *sk)
{
}

static inline void dummy_sk_clone_security (const struct sock *sk, struct sock *newsk)
{
}

static inline void dummy_sk_getsecid(struct sock *sk, u32 *secid)
{
}

static inline void dummy_sock_graft(struct sock* sk, struct socket *parent)
{
}

static inline int dummy_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return 0;
}

static inline void dummy_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
}

static inline void dummy_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
}

static inline void dummy_req_classify_flow(const struct request_sock *req,
			struct flowi *fl)
{
}
#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM
static int dummy_xfrm_policy_alloc_security(struct xfrm_sec_ctx **ctxp,
					    struct xfrm_user_sec_ctx *sec_ctx)
{
	return 0;
}

static inline int dummy_xfrm_policy_clone_security(struct xfrm_sec_ctx *old_ctx,
					   struct xfrm_sec_ctx **new_ctxp)
{
	return 0;
}

static void dummy_xfrm_policy_free_security(struct xfrm_sec_ctx *ctx)
{
}

static int dummy_xfrm_policy_delete_security(struct xfrm_sec_ctx *ctx)
{
	return 0;
}

static int dummy_xfrm_state_alloc_security(struct xfrm_state *x,
	struct xfrm_user_sec_ctx *sec_ctx, u32 secid)
{
	return 0;
}

static void dummy_xfrm_state_free_security(struct xfrm_state *x)
{
}

static int dummy_xfrm_state_delete_security(struct xfrm_state *x)
{
	return 0;
}

static int dummy_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx,
				    u32 sk_sid, u8 dir)
{
	return 0;
}

static int dummy_xfrm_state_pol_flow_match(struct xfrm_state *x,
				struct xfrm_policy *xp, struct flowi *fl)
{
	return 1;
}

static int dummy_xfrm_decode_session(struct sk_buff *skb, u32 *fl, int ckall)
{
	return 0;
}

#endif /* CONFIG_SECURITY_NETWORK_XFRM */
static int dummy_register_security (const char *name, struct security_operations *ops)
{
	return -EINVAL;
}

static void dummy_d_instantiate (struct dentry *dentry, struct inode *inode)
{
	return;
}

static int dummy_getprocattr(struct task_struct *p, char *name, char **value)
{
	return -EINVAL;
}

static int dummy_setprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
	return -EINVAL;
}

static int dummy_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return -EOPNOTSUPP;
}

static int dummy_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	return -EOPNOTSUPP;
}

static void dummy_release_secctx(char *secdata, u32 seclen)
{
}

#ifdef CONFIG_KEYS
static inline int dummy_key_alloc(struct key *key, struct task_struct *ctx,
				  unsigned long flags)
{
	return 0;
}

static inline void dummy_key_free(struct key *key)
{
}

static inline int dummy_key_permission(key_ref_t key_ref,
				       struct task_struct *context,
				       key_perm_t perm)
{
	return 0;
}

static int dummy_key_getsecurity(struct key *key, char **_buffer)
{
	*_buffer = NULL;
	return 0;
}

#endif /* CONFIG_KEYS */

#ifdef CONFIG_AUDIT
static inline int dummy_audit_rule_init(u32 field, u32 op, char *rulestr,
					void **lsmrule)
{
	return 0;
}

static inline int dummy_audit_rule_known(struct audit_krule *krule)
{
	return 0;
}

static inline int dummy_audit_rule_match(u32 secid, u32 field, u32 op,
					 void *lsmrule,
					 struct audit_context *actx)
{
	return 0;
}

static inline void dummy_audit_rule_free(void *lsmrule)
{ }

#endif /* CONFIG_AUDIT */

struct security_operations dummy_security_ops = {
	.name = "dummy",
};

#define set_to_dummy_if_null(ops, function)				\
	do {								\
		if (!ops->function) {					\
			ops->function = dummy_##function;		\
			pr_debug("Had to override the " #function	\
				 " security operation with the dummy one.\n");\
			}						\
	} while (0)

void security_fixup_ops (struct security_operations *ops)
{
	set_to_dummy_if_null(ops, ptrace);
	set_to_dummy_if_null(ops, capget);
	set_to_dummy_if_null(ops, capset_check);
	set_to_dummy_if_null(ops, capset_set);
	set_to_dummy_if_null(ops, acct);
	set_to_dummy_if_null(ops, capable);
	set_to_dummy_if_null(ops, quotactl);
	set_to_dummy_if_null(ops, quota_on);
	set_to_dummy_if_null(ops, sysctl);
	set_to_dummy_if_null(ops, syslog);
	set_to_dummy_if_null(ops, settime);
	set_to_dummy_if_null(ops, vm_enough_memory);
	set_to_dummy_if_null(ops, bprm_alloc_security);
	set_to_dummy_if_null(ops, bprm_free_security);
	set_to_dummy_if_null(ops, bprm_apply_creds);
	set_to_dummy_if_null(ops, bprm_post_apply_creds);
	set_to_dummy_if_null(ops, bprm_set_security);
	set_to_dummy_if_null(ops, bprm_check_security);
	set_to_dummy_if_null(ops, bprm_secureexec);
	set_to_dummy_if_null(ops, sb_alloc_security);
	set_to_dummy_if_null(ops, sb_free_security);
	set_to_dummy_if_null(ops, sb_copy_data);
	set_to_dummy_if_null(ops, sb_kern_mount);
	set_to_dummy_if_null(ops, sb_statfs);
	set_to_dummy_if_null(ops, sb_mount);
	set_to_dummy_if_null(ops, sb_check_sb);
	set_to_dummy_if_null(ops, sb_umount);
	set_to_dummy_if_null(ops, sb_umount_close);
	set_to_dummy_if_null(ops, sb_umount_busy);
	set_to_dummy_if_null(ops, sb_post_remount);
	set_to_dummy_if_null(ops, sb_post_addmount);
	set_to_dummy_if_null(ops, sb_pivotroot);
	set_to_dummy_if_null(ops, sb_post_pivotroot);
	set_to_dummy_if_null(ops, sb_get_mnt_opts);
	set_to_dummy_if_null(ops, sb_set_mnt_opts);
	set_to_dummy_if_null(ops, sb_clone_mnt_opts);
	set_to_dummy_if_null(ops, sb_parse_opts_str);
	set_to_dummy_if_null(ops, inode_alloc_security);
	set_to_dummy_if_null(ops, inode_free_security);
	set_to_dummy_if_null(ops, inode_init_security);
	set_to_dummy_if_null(ops, inode_create);
	set_to_dummy_if_null(ops, inode_link);
	set_to_dummy_if_null(ops, inode_unlink);
	set_to_dummy_if_null(ops, inode_symlink);
	set_to_dummy_if_null(ops, inode_mkdir);
	set_to_dummy_if_null(ops, inode_rmdir);
	set_to_dummy_if_null(ops, inode_mknod);
	set_to_dummy_if_null(ops, inode_rename);
	set_to_dummy_if_null(ops, inode_readlink);
	set_to_dummy_if_null(ops, inode_follow_link);
	set_to_dummy_if_null(ops, inode_permission);
	set_to_dummy_if_null(ops, inode_setattr);
	set_to_dummy_if_null(ops, inode_getattr);
	set_to_dummy_if_null(ops, inode_delete);
	set_to_dummy_if_null(ops, inode_setxattr);
	set_to_dummy_if_null(ops, inode_post_setxattr);
	set_to_dummy_if_null(ops, inode_getxattr);
	set_to_dummy_if_null(ops, inode_listxattr);
	set_to_dummy_if_null(ops, inode_removexattr);
	set_to_dummy_if_null(ops, inode_need_killpriv);
	set_to_dummy_if_null(ops, inode_killpriv);
	set_to_dummy_if_null(ops, inode_getsecurity);
	set_to_dummy_if_null(ops, inode_setsecurity);
	set_to_dummy_if_null(ops, inode_listsecurity);
	set_to_dummy_if_null(ops, inode_getsecid);
	set_to_dummy_if_null(ops, file_permission);
	set_to_dummy_if_null(ops, file_alloc_security);
	set_to_dummy_if_null(ops, file_free_security);
	set_to_dummy_if_null(ops, file_ioctl);
	set_to_dummy_if_null(ops, file_mmap);
	set_to_dummy_if_null(ops, file_mprotect);
	set_to_dummy_if_null(ops, file_lock);
	set_to_dummy_if_null(ops, file_fcntl);
	set_to_dummy_if_null(ops, file_set_fowner);
	set_to_dummy_if_null(ops, file_send_sigiotask);
	set_to_dummy_if_null(ops, file_receive);
	set_to_dummy_if_null(ops, dentry_open);
	set_to_dummy_if_null(ops, task_create);
	set_to_dummy_if_null(ops, task_alloc_security);
	set_to_dummy_if_null(ops, task_free_security);
	set_to_dummy_if_null(ops, task_setuid);
	set_to_dummy_if_null(ops, task_post_setuid);
	set_to_dummy_if_null(ops, task_setgid);
	set_to_dummy_if_null(ops, task_setpgid);
	set_to_dummy_if_null(ops, task_getpgid);
	set_to_dummy_if_null(ops, task_getsid);
	set_to_dummy_if_null(ops, task_getsecid);
	set_to_dummy_if_null(ops, task_setgroups);
	set_to_dummy_if_null(ops, task_setnice);
	set_to_dummy_if_null(ops, task_setioprio);
	set_to_dummy_if_null(ops, task_getioprio);
	set_to_dummy_if_null(ops, task_setrlimit);
	set_to_dummy_if_null(ops, task_setscheduler);
	set_to_dummy_if_null(ops, task_getscheduler);
	set_to_dummy_if_null(ops, task_movememory);
	set_to_dummy_if_null(ops, task_wait);
	set_to_dummy_if_null(ops, task_kill);
	set_to_dummy_if_null(ops, task_prctl);
	set_to_dummy_if_null(ops, task_reparent_to_init);
 	set_to_dummy_if_null(ops, task_to_inode);
	set_to_dummy_if_null(ops, ipc_permission);
	set_to_dummy_if_null(ops, ipc_getsecid);
	set_to_dummy_if_null(ops, msg_msg_alloc_security);
	set_to_dummy_if_null(ops, msg_msg_free_security);
	set_to_dummy_if_null(ops, msg_queue_alloc_security);
	set_to_dummy_if_null(ops, msg_queue_free_security);
	set_to_dummy_if_null(ops, msg_queue_associate);
	set_to_dummy_if_null(ops, msg_queue_msgctl);
	set_to_dummy_if_null(ops, msg_queue_msgsnd);
	set_to_dummy_if_null(ops, msg_queue_msgrcv);
	set_to_dummy_if_null(ops, shm_alloc_security);
	set_to_dummy_if_null(ops, shm_free_security);
	set_to_dummy_if_null(ops, shm_associate);
	set_to_dummy_if_null(ops, shm_shmctl);
	set_to_dummy_if_null(ops, shm_shmat);
	set_to_dummy_if_null(ops, sem_alloc_security);
	set_to_dummy_if_null(ops, sem_free_security);
	set_to_dummy_if_null(ops, sem_associate);
	set_to_dummy_if_null(ops, sem_semctl);
	set_to_dummy_if_null(ops, sem_semop);
	set_to_dummy_if_null(ops, netlink_send);
	set_to_dummy_if_null(ops, netlink_recv);
	set_to_dummy_if_null(ops, register_security);
	set_to_dummy_if_null(ops, d_instantiate);
 	set_to_dummy_if_null(ops, getprocattr);
 	set_to_dummy_if_null(ops, setprocattr);
 	set_to_dummy_if_null(ops, secid_to_secctx);
	set_to_dummy_if_null(ops, secctx_to_secid);
 	set_to_dummy_if_null(ops, release_secctx);
#ifdef CONFIG_SECURITY_NETWORK
	set_to_dummy_if_null(ops, unix_stream_connect);
	set_to_dummy_if_null(ops, unix_may_send);
	set_to_dummy_if_null(ops, socket_create);
	set_to_dummy_if_null(ops, socket_post_create);
	set_to_dummy_if_null(ops, socket_bind);
	set_to_dummy_if_null(ops, socket_connect);
	set_to_dummy_if_null(ops, socket_listen);
	set_to_dummy_if_null(ops, socket_accept);
	set_to_dummy_if_null(ops, socket_post_accept);
	set_to_dummy_if_null(ops, socket_sendmsg);
	set_to_dummy_if_null(ops, socket_recvmsg);
	set_to_dummy_if_null(ops, socket_getsockname);
	set_to_dummy_if_null(ops, socket_getpeername);
	set_to_dummy_if_null(ops, socket_setsockopt);
	set_to_dummy_if_null(ops, socket_getsockopt);
	set_to_dummy_if_null(ops, socket_shutdown);
	set_to_dummy_if_null(ops, socket_sock_rcv_skb);
	set_to_dummy_if_null(ops, socket_getpeersec_stream);
	set_to_dummy_if_null(ops, socket_getpeersec_dgram);
	set_to_dummy_if_null(ops, sk_alloc_security);
	set_to_dummy_if_null(ops, sk_free_security);
	set_to_dummy_if_null(ops, sk_clone_security);
	set_to_dummy_if_null(ops, sk_getsecid);
	set_to_dummy_if_null(ops, sock_graft);
	set_to_dummy_if_null(ops, inet_conn_request);
	set_to_dummy_if_null(ops, inet_csk_clone);
	set_to_dummy_if_null(ops, inet_conn_established);
	set_to_dummy_if_null(ops, req_classify_flow);
 #endif	/* CONFIG_SECURITY_NETWORK */
#ifdef  CONFIG_SECURITY_NETWORK_XFRM
	set_to_dummy_if_null(ops, xfrm_policy_alloc_security);
	set_to_dummy_if_null(ops, xfrm_policy_clone_security);
	set_to_dummy_if_null(ops, xfrm_policy_free_security);
	set_to_dummy_if_null(ops, xfrm_policy_delete_security);
	set_to_dummy_if_null(ops, xfrm_state_alloc_security);
	set_to_dummy_if_null(ops, xfrm_state_free_security);
	set_to_dummy_if_null(ops, xfrm_state_delete_security);
	set_to_dummy_if_null(ops, xfrm_policy_lookup);
	set_to_dummy_if_null(ops, xfrm_state_pol_flow_match);
	set_to_dummy_if_null(ops, xfrm_decode_session);
#endif	/* CONFIG_SECURITY_NETWORK_XFRM */
#ifdef CONFIG_KEYS
	set_to_dummy_if_null(ops, key_alloc);
	set_to_dummy_if_null(ops, key_free);
	set_to_dummy_if_null(ops, key_permission);
	set_to_dummy_if_null(ops, key_getsecurity);
#endif	/* CONFIG_KEYS */
#ifdef CONFIG_AUDIT
	set_to_dummy_if_null(ops, audit_rule_init);
	set_to_dummy_if_null(ops, audit_rule_known);
	set_to_dummy_if_null(ops, audit_rule_match);
	set_to_dummy_if_null(ops, audit_rule_free);
#endif
}

