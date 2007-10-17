/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>


/* things that live in dummy.c */
extern struct security_operations dummy_security_ops;
extern void security_fixup_ops(struct security_operations *ops);

struct security_operations *security_ops;	/* Initialized to NULL */
unsigned long mmap_min_addr;		/* 0 means no protection */

static inline int verify(struct security_operations *ops)
{
	/* verify the security_operations structure exists */
	if (!ops)
		return -EINVAL;
	security_fixup_ops(ops);
	return 0;
}

static void __init do_security_initcalls(void)
{
	initcall_t *call;
	call = __security_initcall_start;
	while (call < __security_initcall_end) {
		(*call) ();
		call++;
	}
}

/**
 * security_init - initializes the security framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int __init security_init(void)
{
	printk(KERN_INFO "Security Framework initialized\n");

	if (verify(&dummy_security_ops)) {
		printk(KERN_ERR "%s could not verify "
		       "dummy_security_ops structure.\n", __FUNCTION__);
		return -EIO;
	}

	security_ops = &dummy_security_ops;
	do_security_initcalls();

	return 0;
}

/**
 * register_security - registers a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function is to allow a security module to register itself with the
 * kernel security subsystem.  Some rudimentary checking is done on the @ops
 * value passed to this function.  A call to unregister_security() should be
 * done to remove this security_options structure from the kernel.
 *
 * If there is already a security module registered with the kernel,
 * an error will be returned.  Otherwise 0 is returned on success.
 */
int register_security(struct security_operations *ops)
{
	if (verify(ops)) {
		printk(KERN_DEBUG "%s could not verify "
		       "security_operations structure.\n", __FUNCTION__);
		return -EINVAL;
	}

	if (security_ops != &dummy_security_ops)
		return -EAGAIN;

	security_ops = ops;

	return 0;
}

/**
 * unregister_security - unregisters a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function removes a struct security_operations variable that had
 * previously been registered with a successful call to register_security().
 *
 * If @ops does not match the valued previously passed to register_security()
 * an error is returned.  Otherwise the default security options is set to the
 * the dummy_security_ops structure, and 0 is returned.
 */
int unregister_security(struct security_operations *ops)
{
	if (ops != security_ops) {
		printk(KERN_INFO "%s: trying to unregister "
		       "a security_opts structure that is not "
		       "registered, failing.\n", __FUNCTION__);
		return -EINVAL;
	}

	security_ops = &dummy_security_ops;

	return 0;
}

/**
 * mod_reg_security - allows security modules to be "stacked"
 * @name: a pointer to a string with the name of the security_options to be registered
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function allows security modules to be stacked if the currently loaded
 * security module allows this to happen.  It passes the @name and @ops to the
 * register_security function of the currently loaded security module.
 *
 * The return value depends on the currently loaded security module, with 0 as
 * success.
 */
int mod_reg_security(const char *name, struct security_operations *ops)
{
	if (verify(ops)) {
		printk(KERN_INFO "%s could not verify "
		       "security operations.\n", __FUNCTION__);
		return -EINVAL;
	}

	if (ops == security_ops) {
		printk(KERN_INFO "%s security operations "
		       "already registered.\n", __FUNCTION__);
		return -EINVAL;
	}

	return security_ops->register_security(name, ops);
}

/**
 * mod_unreg_security - allows a security module registered with mod_reg_security() to be unloaded
 * @name: a pointer to a string with the name of the security_options to be removed
 * @ops: a pointer to the struct security_options that is to be removed
 *
 * This function allows security modules that have been successfully registered
 * with a call to mod_reg_security() to be unloaded from the system.
 * This calls the currently loaded security module's unregister_security() call
 * with the @name and @ops variables.
 *
 * The return value depends on the currently loaded security module, with 0 as
 * success.
 */
int mod_unreg_security(const char *name, struct security_operations *ops)
{
	if (ops == security_ops) {
		printk(KERN_INFO "%s invalid attempt to unregister "
		       " primary security ops.\n", __FUNCTION__);
		return -EINVAL;
	}

	return security_ops->unregister_security(name, ops);
}

/* Security operations */

int security_ptrace(struct task_struct *parent, struct task_struct *child)
{
	return security_ops->ptrace(parent, child);
}

int security_capget(struct task_struct *target,
		     kernel_cap_t *effective,
		     kernel_cap_t *inheritable,
		     kernel_cap_t *permitted)
{
	return security_ops->capget(target, effective, inheritable, permitted);
}

int security_capset_check(struct task_struct *target,
			   kernel_cap_t *effective,
			   kernel_cap_t *inheritable,
			   kernel_cap_t *permitted)
{
	return security_ops->capset_check(target, effective, inheritable, permitted);
}

void security_capset_set(struct task_struct *target,
			  kernel_cap_t *effective,
			  kernel_cap_t *inheritable,
			  kernel_cap_t *permitted)
{
	security_ops->capset_set(target, effective, inheritable, permitted);
}

int security_capable(struct task_struct *tsk, int cap)
{
	return security_ops->capable(tsk, cap);
}

int security_acct(struct file *file)
{
	return security_ops->acct(file);
}

int security_sysctl(struct ctl_table *table, int op)
{
	return security_ops->sysctl(table, op);
}

int security_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	return security_ops->quotactl(cmds, type, id, sb);
}

int security_quota_on(struct dentry *dentry)
{
	return security_ops->quota_on(dentry);
}

int security_syslog(int type)
{
	return security_ops->syslog(type);
}

int security_settime(struct timespec *ts, struct timezone *tz)
{
	return security_ops->settime(ts, tz);
}

int security_vm_enough_memory(long pages)
{
	return security_ops->vm_enough_memory(current->mm, pages);
}

int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return security_ops->vm_enough_memory(mm, pages);
}

int security_bprm_alloc(struct linux_binprm *bprm)
{
	return security_ops->bprm_alloc_security(bprm);
}

void security_bprm_free(struct linux_binprm *bprm)
{
	security_ops->bprm_free_security(bprm);
}

void security_bprm_apply_creds(struct linux_binprm *bprm, int unsafe)
{
	security_ops->bprm_apply_creds(bprm, unsafe);
}

void security_bprm_post_apply_creds(struct linux_binprm *bprm)
{
	security_ops->bprm_post_apply_creds(bprm);
}

int security_bprm_set(struct linux_binprm *bprm)
{
	return security_ops->bprm_set_security(bprm);
}

int security_bprm_check(struct linux_binprm *bprm)
{
	return security_ops->bprm_check_security(bprm);
}

int security_bprm_secureexec(struct linux_binprm *bprm)
{
	return security_ops->bprm_secureexec(bprm);
}

int security_sb_alloc(struct super_block *sb)
{
	return security_ops->sb_alloc_security(sb);
}

void security_sb_free(struct super_block *sb)
{
	security_ops->sb_free_security(sb);
}

int security_sb_copy_data(struct file_system_type *type, void *orig, void *copy)
{
	return security_ops->sb_copy_data(type, orig, copy);
}

int security_sb_kern_mount(struct super_block *sb, void *data)
{
	return security_ops->sb_kern_mount(sb, data);
}

int security_sb_statfs(struct dentry *dentry)
{
	return security_ops->sb_statfs(dentry);
}

int security_sb_mount(char *dev_name, struct nameidata *nd,
                       char *type, unsigned long flags, void *data)
{
	return security_ops->sb_mount(dev_name, nd, type, flags, data);
}

int security_sb_check_sb(struct vfsmount *mnt, struct nameidata *nd)
{
	return security_ops->sb_check_sb(mnt, nd);
}

int security_sb_umount(struct vfsmount *mnt, int flags)
{
	return security_ops->sb_umount(mnt, flags);
}

void security_sb_umount_close(struct vfsmount *mnt)
{
	security_ops->sb_umount_close(mnt);
}

void security_sb_umount_busy(struct vfsmount *mnt)
{
	security_ops->sb_umount_busy(mnt);
}

void security_sb_post_remount(struct vfsmount *mnt, unsigned long flags, void *data)
{
	security_ops->sb_post_remount(mnt, flags, data);
}

void security_sb_post_mountroot(void)
{
	security_ops->sb_post_mountroot();
}

void security_sb_post_addmount(struct vfsmount *mnt, struct nameidata *mountpoint_nd)
{
	security_ops->sb_post_addmount(mnt, mountpoint_nd);
}

int security_sb_pivotroot(struct nameidata *old_nd, struct nameidata *new_nd)
{
	return security_ops->sb_pivotroot(old_nd, new_nd);
}

void security_sb_post_pivotroot(struct nameidata *old_nd, struct nameidata *new_nd)
{
	security_ops->sb_post_pivotroot(old_nd, new_nd);
}

int security_inode_alloc(struct inode *inode)
{
	inode->i_security = NULL;
	return security_ops->inode_alloc_security(inode);
}

void security_inode_free(struct inode *inode)
{
	security_ops->inode_free_security(inode);
}

int security_inode_init_security(struct inode *inode, struct inode *dir,
				  char **name, void **value, size_t *len)
{
	if (unlikely(IS_PRIVATE(inode)))
		return -EOPNOTSUPP;
	return security_ops->inode_init_security(inode, dir, name, value, len);
}
EXPORT_SYMBOL(security_inode_init_security);

int security_inode_create(struct inode *dir, struct dentry *dentry, int mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_create(dir, dentry, mode);
}

int security_inode_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(old_dentry->d_inode)))
		return 0;
	return security_ops->inode_link(old_dentry, dir, new_dentry);
}

int security_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_unlink(dir, dentry);
}

int security_inode_symlink(struct inode *dir, struct dentry *dentry,
			    const char *old_name)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_symlink(dir, dentry, old_name);
}

int security_inode_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_mkdir(dir, dentry, mode);
}

int security_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_rmdir(dir, dentry);
}

int security_inode_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_mknod(dir, dentry, mode, dev);
}

int security_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry)
{
        if (unlikely(IS_PRIVATE(old_dentry->d_inode) ||
            (new_dentry->d_inode && IS_PRIVATE(new_dentry->d_inode))))
		return 0;
	return security_ops->inode_rename(old_dir, old_dentry,
					   new_dir, new_dentry);
}

int security_inode_readlink(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_readlink(dentry);
}

int security_inode_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_follow_link(dentry, nd);
}

int security_inode_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return security_ops->inode_permission(inode, mask, nd);
}

int security_inode_setattr(struct dentry *dentry, struct iattr *attr)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_setattr(dentry, attr);
}

int security_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_getattr(mnt, dentry);
}

void security_inode_delete(struct inode *inode)
{
	if (unlikely(IS_PRIVATE(inode)))
		return;
	security_ops->inode_delete(inode);
}

int security_inode_setxattr(struct dentry *dentry, char *name,
			     void *value, size_t size, int flags)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_setxattr(dentry, name, value, size, flags);
}

void security_inode_post_setxattr(struct dentry *dentry, char *name,
				   void *value, size_t size, int flags)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return;
	security_ops->inode_post_setxattr(dentry, name, value, size, flags);
}

int security_inode_getxattr(struct dentry *dentry, char *name)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_getxattr(dentry, name);
}

int security_inode_listxattr(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_listxattr(dentry);
}

int security_inode_removexattr(struct dentry *dentry, char *name)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_removexattr(dentry, name);
}

const char *security_inode_xattr_getsuffix(void)
{
	return security_ops->inode_xattr_getsuffix();
}

int security_inode_getsecurity(const struct inode *inode, const char *name, void *buffer, size_t size, int err)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return security_ops->inode_getsecurity(inode, name, buffer, size, err);
}

int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return security_ops->inode_setsecurity(inode, name, value, size, flags);
}

int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return security_ops->inode_listsecurity(inode, buffer, buffer_size);
}

int security_file_permission(struct file *file, int mask)
{
	return security_ops->file_permission(file, mask);
}

int security_file_alloc(struct file *file)
{
	return security_ops->file_alloc_security(file);
}

void security_file_free(struct file *file)
{
	security_ops->file_free_security(file);
}

int security_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return security_ops->file_ioctl(file, cmd, arg);
}

int security_file_mmap(struct file *file, unsigned long reqprot,
			unsigned long prot, unsigned long flags,
			unsigned long addr, unsigned long addr_only)
{
	return security_ops->file_mmap(file, reqprot, prot, flags, addr, addr_only);
}

int security_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			    unsigned long prot)
{
	return security_ops->file_mprotect(vma, reqprot, prot);
}

int security_file_lock(struct file *file, unsigned int cmd)
{
	return security_ops->file_lock(file, cmd);
}

int security_file_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return security_ops->file_fcntl(file, cmd, arg);
}

int security_file_set_fowner(struct file *file)
{
	return security_ops->file_set_fowner(file);
}

int security_file_send_sigiotask(struct task_struct *tsk,
				  struct fown_struct *fown, int sig)
{
	return security_ops->file_send_sigiotask(tsk, fown, sig);
}

int security_file_receive(struct file *file)
{
	return security_ops->file_receive(file);
}

int security_dentry_open(struct file *file)
{
	return security_ops->dentry_open(file);
}

int security_task_create(unsigned long clone_flags)
{
	return security_ops->task_create(clone_flags);
}

int security_task_alloc(struct task_struct *p)
{
	return security_ops->task_alloc_security(p);
}

void security_task_free(struct task_struct *p)
{
	security_ops->task_free_security(p);
}

int security_task_setuid(uid_t id0, uid_t id1, uid_t id2, int flags)
{
	return security_ops->task_setuid(id0, id1, id2, flags);
}

int security_task_post_setuid(uid_t old_ruid, uid_t old_euid,
			       uid_t old_suid, int flags)
{
	return security_ops->task_post_setuid(old_ruid, old_euid, old_suid, flags);
}

int security_task_setgid(gid_t id0, gid_t id1, gid_t id2, int flags)
{
	return security_ops->task_setgid(id0, id1, id2, flags);
}

int security_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return security_ops->task_setpgid(p, pgid);
}

int security_task_getpgid(struct task_struct *p)
{
	return security_ops->task_getpgid(p);
}

int security_task_getsid(struct task_struct *p)
{
	return security_ops->task_getsid(p);
}

void security_task_getsecid(struct task_struct *p, u32 *secid)
{
	security_ops->task_getsecid(p, secid);
}
EXPORT_SYMBOL(security_task_getsecid);

int security_task_setgroups(struct group_info *group_info)
{
	return security_ops->task_setgroups(group_info);
}

int security_task_setnice(struct task_struct *p, int nice)
{
	return security_ops->task_setnice(p, nice);
}

int security_task_setioprio(struct task_struct *p, int ioprio)
{
	return security_ops->task_setioprio(p, ioprio);
}

int security_task_getioprio(struct task_struct *p)
{
	return security_ops->task_getioprio(p);
}

int security_task_setrlimit(unsigned int resource, struct rlimit *new_rlim)
{
	return security_ops->task_setrlimit(resource, new_rlim);
}

int security_task_setscheduler(struct task_struct *p,
				int policy, struct sched_param *lp)
{
	return security_ops->task_setscheduler(p, policy, lp);
}

int security_task_getscheduler(struct task_struct *p)
{
	return security_ops->task_getscheduler(p);
}

int security_task_movememory(struct task_struct *p)
{
	return security_ops->task_movememory(p);
}

int security_task_kill(struct task_struct *p, struct siginfo *info,
			int sig, u32 secid)
{
	return security_ops->task_kill(p, info, sig, secid);
}

int security_task_wait(struct task_struct *p)
{
	return security_ops->task_wait(p);
}

int security_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			 unsigned long arg4, unsigned long arg5)
{
	return security_ops->task_prctl(option, arg2, arg3, arg4, arg5);
}

void security_task_reparent_to_init(struct task_struct *p)
{
	security_ops->task_reparent_to_init(p);
}

void security_task_to_inode(struct task_struct *p, struct inode *inode)
{
	security_ops->task_to_inode(p, inode);
}

int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	return security_ops->ipc_permission(ipcp, flag);
}

int security_msg_msg_alloc(struct msg_msg *msg)
{
	return security_ops->msg_msg_alloc_security(msg);
}

void security_msg_msg_free(struct msg_msg *msg)
{
	security_ops->msg_msg_free_security(msg);
}

int security_msg_queue_alloc(struct msg_queue *msq)
{
	return security_ops->msg_queue_alloc_security(msq);
}

void security_msg_queue_free(struct msg_queue *msq)
{
	security_ops->msg_queue_free_security(msq);
}

int security_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	return security_ops->msg_queue_associate(msq, msqflg);
}

int security_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	return security_ops->msg_queue_msgctl(msq, cmd);
}

int security_msg_queue_msgsnd(struct msg_queue *msq,
			       struct msg_msg *msg, int msqflg)
{
	return security_ops->msg_queue_msgsnd(msq, msg, msqflg);
}

int security_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
			       struct task_struct *target, long type, int mode)
{
	return security_ops->msg_queue_msgrcv(msq, msg, target, type, mode);
}

int security_shm_alloc(struct shmid_kernel *shp)
{
	return security_ops->shm_alloc_security(shp);
}

void security_shm_free(struct shmid_kernel *shp)
{
	security_ops->shm_free_security(shp);
}

int security_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	return security_ops->shm_associate(shp, shmflg);
}

int security_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	return security_ops->shm_shmctl(shp, cmd);
}

int security_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr, int shmflg)
{
	return security_ops->shm_shmat(shp, shmaddr, shmflg);
}

int security_sem_alloc(struct sem_array *sma)
{
	return security_ops->sem_alloc_security(sma);
}

void security_sem_free(struct sem_array *sma)
{
	security_ops->sem_free_security(sma);
}

int security_sem_associate(struct sem_array *sma, int semflg)
{
	return security_ops->sem_associate(sma, semflg);
}

int security_sem_semctl(struct sem_array *sma, int cmd)
{
	return security_ops->sem_semctl(sma, cmd);
}

int security_sem_semop(struct sem_array *sma, struct sembuf *sops,
			unsigned nsops, int alter)
{
	return security_ops->sem_semop(sma, sops, nsops, alter);
}

void security_d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (unlikely(inode && IS_PRIVATE(inode)))
		return;
	security_ops->d_instantiate(dentry, inode);
}
EXPORT_SYMBOL(security_d_instantiate);

int security_getprocattr(struct task_struct *p, char *name, char **value)
{
	return security_ops->getprocattr(p, name, value);
}

int security_setprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
	return security_ops->setprocattr(p, name, value, size);
}

int security_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return security_ops->netlink_send(sk, skb);
}
EXPORT_SYMBOL(security_netlink_send);

int security_netlink_recv(struct sk_buff *skb, int cap)
{
	return security_ops->netlink_recv(skb, cap);
}
EXPORT_SYMBOL(security_netlink_recv);

int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_ops->secid_to_secctx(secid, secdata, seclen);
}
EXPORT_SYMBOL(security_secid_to_secctx);

void security_release_secctx(char *secdata, u32 seclen)
{
	return security_ops->release_secctx(secdata, seclen);
}
EXPORT_SYMBOL(security_release_secctx);

#ifdef CONFIG_SECURITY_NETWORK

int security_unix_stream_connect(struct socket *sock, struct socket *other,
				 struct sock *newsk)
{
	return security_ops->unix_stream_connect(sock, other, newsk);
}
EXPORT_SYMBOL(security_unix_stream_connect);

int security_unix_may_send(struct socket *sock,  struct socket *other)
{
	return security_ops->unix_may_send(sock, other);
}
EXPORT_SYMBOL(security_unix_may_send);

int security_socket_create(int family, int type, int protocol, int kern)
{
	return security_ops->socket_create(family, type, protocol, kern);
}

int security_socket_post_create(struct socket *sock, int family,
				int type, int protocol, int kern)
{
	return security_ops->socket_post_create(sock, family, type,
						protocol, kern);
}

int security_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	return security_ops->socket_bind(sock, address, addrlen);
}

int security_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen)
{
	return security_ops->socket_connect(sock, address, addrlen);
}

int security_socket_listen(struct socket *sock, int backlog)
{
	return security_ops->socket_listen(sock, backlog);
}

int security_socket_accept(struct socket *sock, struct socket *newsock)
{
	return security_ops->socket_accept(sock, newsock);
}

void security_socket_post_accept(struct socket *sock, struct socket *newsock)
{
	security_ops->socket_post_accept(sock, newsock);
}

int security_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	return security_ops->socket_sendmsg(sock, msg, size);
}

int security_socket_recvmsg(struct socket *sock, struct msghdr *msg,
			    int size, int flags)
{
	return security_ops->socket_recvmsg(sock, msg, size, flags);
}

int security_socket_getsockname(struct socket *sock)
{
	return security_ops->socket_getsockname(sock);
}

int security_socket_getpeername(struct socket *sock)
{
	return security_ops->socket_getpeername(sock);
}

int security_socket_getsockopt(struct socket *sock, int level, int optname)
{
	return security_ops->socket_getsockopt(sock, level, optname);
}

int security_socket_setsockopt(struct socket *sock, int level, int optname)
{
	return security_ops->socket_setsockopt(sock, level, optname);
}

int security_socket_shutdown(struct socket *sock, int how)
{
	return security_ops->socket_shutdown(sock, how);
}

int security_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	return security_ops->socket_sock_rcv_skb(sk, skb);
}
EXPORT_SYMBOL(security_sock_rcv_skb);

int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
				      int __user *optlen, unsigned len)
{
	return security_ops->socket_getpeersec_stream(sock, optval, optlen, len);
}

int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return security_ops->socket_getpeersec_dgram(sock, skb, secid);
}
EXPORT_SYMBOL(security_socket_getpeersec_dgram);

int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return security_ops->sk_alloc_security(sk, family, priority);
}

void security_sk_free(struct sock *sk)
{
	return security_ops->sk_free_security(sk);
}

void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
	return security_ops->sk_clone_security(sk, newsk);
}

void security_sk_classify_flow(struct sock *sk, struct flowi *fl)
{
	security_ops->sk_getsecid(sk, &fl->secid);
}
EXPORT_SYMBOL(security_sk_classify_flow);

void security_req_classify_flow(const struct request_sock *req, struct flowi *fl)
{
	security_ops->req_classify_flow(req, fl);
}
EXPORT_SYMBOL(security_req_classify_flow);

void security_sock_graft(struct sock *sk, struct socket *parent)
{
	security_ops->sock_graft(sk, parent);
}
EXPORT_SYMBOL(security_sock_graft);

int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return security_ops->inet_conn_request(sk, skb, req);
}
EXPORT_SYMBOL(security_inet_conn_request);

void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
	security_ops->inet_csk_clone(newsk, req);
}

void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
	security_ops->inet_conn_established(sk, skb);
}

#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM

int security_xfrm_policy_alloc(struct xfrm_policy *xp, struct xfrm_user_sec_ctx *sec_ctx)
{
	return security_ops->xfrm_policy_alloc_security(xp, sec_ctx);
}
EXPORT_SYMBOL(security_xfrm_policy_alloc);

int security_xfrm_policy_clone(struct xfrm_policy *old, struct xfrm_policy *new)
{
	return security_ops->xfrm_policy_clone_security(old, new);
}

void security_xfrm_policy_free(struct xfrm_policy *xp)
{
	security_ops->xfrm_policy_free_security(xp);
}
EXPORT_SYMBOL(security_xfrm_policy_free);

int security_xfrm_policy_delete(struct xfrm_policy *xp)
{
	return security_ops->xfrm_policy_delete_security(xp);
}

int security_xfrm_state_alloc(struct xfrm_state *x, struct xfrm_user_sec_ctx *sec_ctx)
{
	return security_ops->xfrm_state_alloc_security(x, sec_ctx, 0);
}
EXPORT_SYMBOL(security_xfrm_state_alloc);

int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
				      struct xfrm_sec_ctx *polsec, u32 secid)
{
	if (!polsec)
		return 0;
	/*
	 * We want the context to be taken from secid which is usually
	 * from the sock.
	 */
	return security_ops->xfrm_state_alloc_security(x, NULL, secid);
}

int security_xfrm_state_delete(struct xfrm_state *x)
{
	return security_ops->xfrm_state_delete_security(x);
}
EXPORT_SYMBOL(security_xfrm_state_delete);

void security_xfrm_state_free(struct xfrm_state *x)
{
	security_ops->xfrm_state_free_security(x);
}

int security_xfrm_policy_lookup(struct xfrm_policy *xp, u32 fl_secid, u8 dir)
{
	return security_ops->xfrm_policy_lookup(xp, fl_secid, dir);
}

int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
				       struct xfrm_policy *xp, struct flowi *fl)
{
	return security_ops->xfrm_state_pol_flow_match(x, xp, fl);
}

int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return security_ops->xfrm_decode_session(skb, secid, 1);
}

void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl)
{
	int rc = security_ops->xfrm_decode_session(skb, &fl->secid, 0);

	BUG_ON(rc);
}
EXPORT_SYMBOL(security_skb_classify_flow);

#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

#ifdef CONFIG_KEYS

int security_key_alloc(struct key *key, struct task_struct *tsk, unsigned long flags)
{
	return security_ops->key_alloc(key, tsk, flags);
}

void security_key_free(struct key *key)
{
	security_ops->key_free(key);
}

int security_key_permission(key_ref_t key_ref,
			    struct task_struct *context, key_perm_t perm)
{
	return security_ops->key_permission(key_ref, context, perm);
}

#endif	/* CONFIG_KEYS */
