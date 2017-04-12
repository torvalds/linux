/*
 * Linux Security plug
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2001 James Morris <jmorris@intercode.com.au>
 * Copyright (C) 2001 Silicon Graphics, Inc. (Trust Technology Group)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Due to this file being licensed under the GPL there is controversy over
 *	whether this permits you to write a module that #includes this file
 *	without placing your module under the GPL.  Please consult a lawyer for
 *	advice before doing this.
 *
 */

#ifndef __LINUX_SECURITY_H
#define __LINUX_SECURITY_H

#include <linux/key.h>
#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>

struct linux_binprm;
struct cred;
struct rlimit;
struct siginfo;
struct sem_array;
struct sembuf;
struct kern_ipc_perm;
struct audit_context;
struct super_block;
struct inode;
struct dentry;
struct file;
struct vfsmount;
struct path;
struct qstr;
struct iattr;
struct fown_struct;
struct file_operations;
struct shmid_kernel;
struct msg_msg;
struct msg_queue;
struct xattr;
struct xfrm_sec_ctx;
struct mm_struct;

/* If capable should audit the security request */
#define SECURITY_CAP_NOAUDIT 0
#define SECURITY_CAP_AUDIT 1

/* LSM Agnostic defines for sb_set_mnt_opts */
#define SECURITY_LSM_NATIVE_LABELS	1

struct ctl_table;
struct audit_krule;
struct user_namespace;
struct timezone;

/* These functions are in security/commoncap.c */
extern int cap_capable(const struct cred *cred, struct user_namespace *ns,
		       int cap, int audit);
extern int cap_settime(const struct timespec64 *ts, const struct timezone *tz);
extern int cap_ptrace_access_check(struct task_struct *child, unsigned int mode);
extern int cap_ptrace_traceme(struct task_struct *parent);
extern int cap_capget(struct task_struct *target, kernel_cap_t *effective, kernel_cap_t *inheritable, kernel_cap_t *permitted);
extern int cap_capset(struct cred *new, const struct cred *old,
		      const kernel_cap_t *effective,
		      const kernel_cap_t *inheritable,
		      const kernel_cap_t *permitted);
extern int cap_bprm_set_creds(struct linux_binprm *bprm);
extern int cap_bprm_secureexec(struct linux_binprm *bprm);
extern int cap_inode_setxattr(struct dentry *dentry, const char *name,
			      const void *value, size_t size, int flags);
extern int cap_inode_removexattr(struct dentry *dentry, const char *name);
extern int cap_inode_need_killpriv(struct dentry *dentry);
extern int cap_inode_killpriv(struct dentry *dentry);
extern int cap_mmap_addr(unsigned long addr);
extern int cap_mmap_file(struct file *file, unsigned long reqprot,
			 unsigned long prot, unsigned long flags);
extern int cap_task_fix_setuid(struct cred *new, const struct cred *old, int flags);
extern int cap_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			  unsigned long arg4, unsigned long arg5);
extern int cap_task_setscheduler(struct task_struct *p);
extern int cap_task_setioprio(struct task_struct *p, int ioprio);
extern int cap_task_setnice(struct task_struct *p, int nice);
extern int cap_vm_enough_memory(struct mm_struct *mm, long pages);

struct msghdr;
struct sk_buff;
struct sock;
struct sockaddr;
struct socket;
struct flowi;
struct dst_entry;
struct xfrm_selector;
struct xfrm_policy;
struct xfrm_state;
struct xfrm_user_sec_ctx;
struct seq_file;

#ifdef CONFIG_MMU
extern unsigned long mmap_min_addr;
extern unsigned long dac_mmap_min_addr;
#else
#define mmap_min_addr		0UL
#define dac_mmap_min_addr	0UL
#endif

/*
 * Values used in the task_security_ops calls
 */
/* setuid or setgid, id0 == uid or gid */
#define LSM_SETID_ID	1

/* setreuid or setregid, id0 == real, id1 == eff */
#define LSM_SETID_RE	2

/* setresuid or setresgid, id0 == real, id1 == eff, uid2 == saved */
#define LSM_SETID_RES	4

/* setfsuid or setfsgid, id0 == fsuid or fsgid */
#define LSM_SETID_FS	8

/* forward declares to avoid warnings */
struct sched_param;
struct request_sock;

/* bprm->unsafe reasons */
#define LSM_UNSAFE_SHARE	1
#define LSM_UNSAFE_PTRACE	2
#define LSM_UNSAFE_NO_NEW_PRIVS	4

#ifdef CONFIG_MMU
extern int mmap_min_addr_handler(struct ctl_table *table, int write,
				 void __user *buffer, size_t *lenp, loff_t *ppos);
#endif

/* security_inode_init_security callback function to write xattrs */
typedef int (*initxattrs) (struct inode *inode,
			   const struct xattr *xattr_array, void *fs_data);

#ifdef CONFIG_SECURITY

struct security_mnt_opts {
	char **mnt_opts;
	int *mnt_opts_flags;
	int num_mnt_opts;
};

static inline void security_init_mnt_opts(struct security_mnt_opts *opts)
{
	opts->mnt_opts = NULL;
	opts->mnt_opts_flags = NULL;
	opts->num_mnt_opts = 0;
}

static inline void security_free_mnt_opts(struct security_mnt_opts *opts)
{
	int i;
	if (opts->mnt_opts)
		for (i = 0; i < opts->num_mnt_opts; i++)
			kfree(opts->mnt_opts[i]);
	kfree(opts->mnt_opts);
	opts->mnt_opts = NULL;
	kfree(opts->mnt_opts_flags);
	opts->mnt_opts_flags = NULL;
	opts->num_mnt_opts = 0;
}

/* prototypes */
extern int security_init(void);

/* Security operations */
int security_binder_set_context_mgr(struct task_struct *mgr);
int security_binder_transaction(struct task_struct *from,
				struct task_struct *to);
int security_binder_transfer_binder(struct task_struct *from,
				    struct task_struct *to);
int security_binder_transfer_file(struct task_struct *from,
				  struct task_struct *to, struct file *file);
int security_ptrace_access_check(struct task_struct *child, unsigned int mode);
int security_ptrace_traceme(struct task_struct *parent);
int security_capget(struct task_struct *target,
		    kernel_cap_t *effective,
		    kernel_cap_t *inheritable,
		    kernel_cap_t *permitted);
int security_capset(struct cred *new, const struct cred *old,
		    const kernel_cap_t *effective,
		    const kernel_cap_t *inheritable,
		    const kernel_cap_t *permitted);
int security_capable(const struct cred *cred, struct user_namespace *ns,
			int cap);
int security_capable_noaudit(const struct cred *cred, struct user_namespace *ns,
			     int cap);
int security_quotactl(int cmds, int type, int id, struct super_block *sb);
int security_quota_on(struct dentry *dentry);
int security_syslog(int type);
int security_settime64(const struct timespec64 *ts, const struct timezone *tz);
static inline int security_settime(const struct timespec *ts, const struct timezone *tz)
{
	struct timespec64 ts64 = timespec_to_timespec64(*ts);

	return security_settime64(&ts64, tz);
}
int security_vm_enough_memory_mm(struct mm_struct *mm, long pages);
int security_bprm_set_creds(struct linux_binprm *bprm);
int security_bprm_check(struct linux_binprm *bprm);
void security_bprm_committing_creds(struct linux_binprm *bprm);
void security_bprm_committed_creds(struct linux_binprm *bprm);
int security_bprm_secureexec(struct linux_binprm *bprm);
int security_sb_alloc(struct super_block *sb);
void security_sb_free(struct super_block *sb);
int security_sb_copy_data(char *orig, char *copy);
int security_sb_remount(struct super_block *sb, void *data);
int security_sb_kern_mount(struct super_block *sb, int flags, void *data);
int security_sb_show_options(struct seq_file *m, struct super_block *sb);
int security_sb_statfs(struct dentry *dentry);
int security_sb_mount(const char *dev_name, const struct path *path,
		      const char *type, unsigned long flags, void *data);
int security_sb_umount(struct vfsmount *mnt, int flags);
int security_sb_pivotroot(const struct path *old_path, const struct path *new_path);
int security_sb_set_mnt_opts(struct super_block *sb,
				struct security_mnt_opts *opts,
				unsigned long kern_flags,
				unsigned long *set_kern_flags);
int security_sb_clone_mnt_opts(const struct super_block *oldsb,
				struct super_block *newsb);
int security_sb_parse_opts_str(char *options, struct security_mnt_opts *opts);
int security_dentry_init_security(struct dentry *dentry, int mode,
					const struct qstr *name, void **ctx,
					u32 *ctxlen);
int security_dentry_create_files_as(struct dentry *dentry, int mode,
					struct qstr *name,
					const struct cred *old,
					struct cred *new);

int security_inode_alloc(struct inode *inode);
void security_inode_free(struct inode *inode);
int security_inode_init_security(struct inode *inode, struct inode *dir,
				 const struct qstr *qstr,
				 initxattrs initxattrs, void *fs_data);
int security_old_inode_init_security(struct inode *inode, struct inode *dir,
				     const struct qstr *qstr, const char **name,
				     void **value, size_t *len);
int security_inode_create(struct inode *dir, struct dentry *dentry, umode_t mode);
int security_inode_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry);
int security_inode_unlink(struct inode *dir, struct dentry *dentry);
int security_inode_symlink(struct inode *dir, struct dentry *dentry,
			   const char *old_name);
int security_inode_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
int security_inode_rmdir(struct inode *dir, struct dentry *dentry);
int security_inode_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev);
int security_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
			  struct inode *new_dir, struct dentry *new_dentry,
			  unsigned int flags);
int security_inode_readlink(struct dentry *dentry);
int security_inode_follow_link(struct dentry *dentry, struct inode *inode,
			       bool rcu);
int security_inode_permission(struct inode *inode, int mask);
int security_inode_setattr(struct dentry *dentry, struct iattr *attr);
int security_inode_getattr(const struct path *path);
int security_inode_setxattr(struct dentry *dentry, const char *name,
			    const void *value, size_t size, int flags);
void security_inode_post_setxattr(struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags);
int security_inode_getxattr(struct dentry *dentry, const char *name);
int security_inode_listxattr(struct dentry *dentry);
int security_inode_removexattr(struct dentry *dentry, const char *name);
int security_inode_need_killpriv(struct dentry *dentry);
int security_inode_killpriv(struct dentry *dentry);
int security_inode_getsecurity(struct inode *inode, const char *name, void **buffer, bool alloc);
int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags);
int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size);
void security_inode_getsecid(struct inode *inode, u32 *secid);
int security_inode_copy_up(struct dentry *src, struct cred **new);
int security_inode_copy_up_xattr(const char *name);
int security_file_permission(struct file *file, int mask);
int security_file_alloc(struct file *file);
void security_file_free(struct file *file);
int security_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int security_mmap_file(struct file *file, unsigned long prot,
			unsigned long flags);
int security_mmap_addr(unsigned long addr);
int security_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			   unsigned long prot);
int security_file_lock(struct file *file, unsigned int cmd);
int security_file_fcntl(struct file *file, unsigned int cmd, unsigned long arg);
void security_file_set_fowner(struct file *file);
int security_file_send_sigiotask(struct task_struct *tsk,
				 struct fown_struct *fown, int sig);
int security_file_receive(struct file *file);
int security_file_open(struct file *file, const struct cred *cred);
int security_task_create(unsigned long clone_flags);
void security_task_free(struct task_struct *task);
int security_cred_alloc_blank(struct cred *cred, gfp_t gfp);
void security_cred_free(struct cred *cred);
int security_prepare_creds(struct cred *new, const struct cred *old, gfp_t gfp);
void security_transfer_creds(struct cred *new, const struct cred *old);
int security_kernel_act_as(struct cred *new, u32 secid);
int security_kernel_create_files_as(struct cred *new, struct inode *inode);
int security_kernel_module_request(char *kmod_name);
int security_kernel_read_file(struct file *file, enum kernel_read_file_id id);
int security_kernel_post_read_file(struct file *file, char *buf, loff_t size,
				   enum kernel_read_file_id id);
int security_task_fix_setuid(struct cred *new, const struct cred *old,
			     int flags);
int security_task_setpgid(struct task_struct *p, pid_t pgid);
int security_task_getpgid(struct task_struct *p);
int security_task_getsid(struct task_struct *p);
void security_task_getsecid(struct task_struct *p, u32 *secid);
int security_task_setnice(struct task_struct *p, int nice);
int security_task_setioprio(struct task_struct *p, int ioprio);
int security_task_getioprio(struct task_struct *p);
int security_task_setrlimit(struct task_struct *p, unsigned int resource,
		struct rlimit *new_rlim);
int security_task_setscheduler(struct task_struct *p);
int security_task_getscheduler(struct task_struct *p);
int security_task_movememory(struct task_struct *p);
int security_task_kill(struct task_struct *p, struct siginfo *info,
			int sig, u32 secid);
int security_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			unsigned long arg4, unsigned long arg5);
void security_task_to_inode(struct task_struct *p, struct inode *inode);
int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag);
void security_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid);
int security_msg_msg_alloc(struct msg_msg *msg);
void security_msg_msg_free(struct msg_msg *msg);
int security_msg_queue_alloc(struct msg_queue *msq);
void security_msg_queue_free(struct msg_queue *msq);
int security_msg_queue_associate(struct msg_queue *msq, int msqflg);
int security_msg_queue_msgctl(struct msg_queue *msq, int cmd);
int security_msg_queue_msgsnd(struct msg_queue *msq,
			      struct msg_msg *msg, int msqflg);
int security_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
			      struct task_struct *target, long type, int mode);
int security_shm_alloc(struct shmid_kernel *shp);
void security_shm_free(struct shmid_kernel *shp);
int security_shm_associate(struct shmid_kernel *shp, int shmflg);
int security_shm_shmctl(struct shmid_kernel *shp, int cmd);
int security_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr, int shmflg);
int security_sem_alloc(struct sem_array *sma);
void security_sem_free(struct sem_array *sma);
int security_sem_associate(struct sem_array *sma, int semflg);
int security_sem_semctl(struct sem_array *sma, int cmd);
int security_sem_semop(struct sem_array *sma, struct sembuf *sops,
			unsigned nsops, int alter);
void security_d_instantiate(struct dentry *dentry, struct inode *inode);
int security_getprocattr(struct task_struct *p, char *name, char **value);
int security_setprocattr(const char *name, void *value, size_t size);
int security_netlink_send(struct sock *sk, struct sk_buff *skb);
int security_ismaclabel(const char *name);
int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen);
int security_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid);
void security_release_secctx(char *secdata, u32 seclen);

void security_inode_invalidate_secctx(struct inode *inode);
int security_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen);
int security_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen);
int security_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen);
#else /* CONFIG_SECURITY */
struct security_mnt_opts {
};

static inline void security_init_mnt_opts(struct security_mnt_opts *opts)
{
}

static inline void security_free_mnt_opts(struct security_mnt_opts *opts)
{
}

/*
 * This is the default capabilities functionality.  Most of these functions
 * are just stubbed out, but a few must call the proper capable code.
 */

static inline int security_init(void)
{
	return 0;
}

static inline int security_binder_set_context_mgr(struct task_struct *mgr)
{
	return 0;
}

static inline int security_binder_transaction(struct task_struct *from,
					      struct task_struct *to)
{
	return 0;
}

static inline int security_binder_transfer_binder(struct task_struct *from,
						  struct task_struct *to)
{
	return 0;
}

static inline int security_binder_transfer_file(struct task_struct *from,
						struct task_struct *to,
						struct file *file)
{
	return 0;
}

static inline int security_ptrace_access_check(struct task_struct *child,
					     unsigned int mode)
{
	return cap_ptrace_access_check(child, mode);
}

static inline int security_ptrace_traceme(struct task_struct *parent)
{
	return cap_ptrace_traceme(parent);
}

static inline int security_capget(struct task_struct *target,
				   kernel_cap_t *effective,
				   kernel_cap_t *inheritable,
				   kernel_cap_t *permitted)
{
	return cap_capget(target, effective, inheritable, permitted);
}

static inline int security_capset(struct cred *new,
				   const struct cred *old,
				   const kernel_cap_t *effective,
				   const kernel_cap_t *inheritable,
				   const kernel_cap_t *permitted)
{
	return cap_capset(new, old, effective, inheritable, permitted);
}

static inline int security_capable(const struct cred *cred,
				   struct user_namespace *ns, int cap)
{
	return cap_capable(cred, ns, cap, SECURITY_CAP_AUDIT);
}

static inline int security_capable_noaudit(const struct cred *cred,
					   struct user_namespace *ns, int cap) {
	return cap_capable(cred, ns, cap, SECURITY_CAP_NOAUDIT);
}

static inline int security_quotactl(int cmds, int type, int id,
				     struct super_block *sb)
{
	return 0;
}

static inline int security_quota_on(struct dentry *dentry)
{
	return 0;
}

static inline int security_syslog(int type)
{
	return 0;
}

static inline int security_settime64(const struct timespec64 *ts,
				     const struct timezone *tz)
{
	return cap_settime(ts, tz);
}

static inline int security_settime(const struct timespec *ts,
				   const struct timezone *tz)
{
	struct timespec64 ts64 = timespec_to_timespec64(*ts);

	return cap_settime(&ts64, tz);
}

static inline int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return __vm_enough_memory(mm, pages, cap_vm_enough_memory(mm, pages));
}

static inline int security_bprm_set_creds(struct linux_binprm *bprm)
{
	return cap_bprm_set_creds(bprm);
}

static inline int security_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline void security_bprm_committing_creds(struct linux_binprm *bprm)
{
}

static inline void security_bprm_committed_creds(struct linux_binprm *bprm)
{
}

static inline int security_bprm_secureexec(struct linux_binprm *bprm)
{
	return cap_bprm_secureexec(bprm);
}

static inline int security_sb_alloc(struct super_block *sb)
{
	return 0;
}

static inline void security_sb_free(struct super_block *sb)
{ }

static inline int security_sb_copy_data(char *orig, char *copy)
{
	return 0;
}

static inline int security_sb_remount(struct super_block *sb, void *data)
{
	return 0;
}

static inline int security_sb_kern_mount(struct super_block *sb, int flags, void *data)
{
	return 0;
}

static inline int security_sb_show_options(struct seq_file *m,
					   struct super_block *sb)
{
	return 0;
}

static inline int security_sb_statfs(struct dentry *dentry)
{
	return 0;
}

static inline int security_sb_mount(const char *dev_name, const struct path *path,
				    const char *type, unsigned long flags,
				    void *data)
{
	return 0;
}

static inline int security_sb_umount(struct vfsmount *mnt, int flags)
{
	return 0;
}

static inline int security_sb_pivotroot(const struct path *old_path,
					const struct path *new_path)
{
	return 0;
}

static inline int security_sb_set_mnt_opts(struct super_block *sb,
					   struct security_mnt_opts *opts,
					   unsigned long kern_flags,
					   unsigned long *set_kern_flags)
{
	return 0;
}

static inline int security_sb_clone_mnt_opts(const struct super_block *oldsb,
					      struct super_block *newsb)
{
	return 0;
}

static inline int security_sb_parse_opts_str(char *options, struct security_mnt_opts *opts)
{
	return 0;
}

static inline int security_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void security_inode_free(struct inode *inode)
{ }

static inline int security_dentry_init_security(struct dentry *dentry,
						 int mode,
						 const struct qstr *name,
						 void **ctx,
						 u32 *ctxlen)
{
	return -EOPNOTSUPP;
}

static inline int security_dentry_create_files_as(struct dentry *dentry,
						  int mode, struct qstr *name,
						  const struct cred *old,
						  struct cred *new)
{
	return 0;
}


static inline int security_inode_init_security(struct inode *inode,
						struct inode *dir,
						const struct qstr *qstr,
						const initxattrs xattrs,
						void *fs_data)
{
	return 0;
}

static inline int security_old_inode_init_security(struct inode *inode,
						   struct inode *dir,
						   const struct qstr *qstr,
						   const char **name,
						   void **value, size_t *len)
{
	return -EOPNOTSUPP;
}

static inline int security_inode_create(struct inode *dir,
					 struct dentry *dentry,
					 umode_t mode)
{
	return 0;
}

static inline int security_inode_link(struct dentry *old_dentry,
				       struct inode *dir,
				       struct dentry *new_dentry)
{
	return 0;
}

static inline int security_inode_unlink(struct inode *dir,
					 struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_symlink(struct inode *dir,
					  struct dentry *dentry,
					  const char *old_name)
{
	return 0;
}

static inline int security_inode_mkdir(struct inode *dir,
					struct dentry *dentry,
					int mode)
{
	return 0;
}

static inline int security_inode_rmdir(struct inode *dir,
					struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_mknod(struct inode *dir,
					struct dentry *dentry,
					int mode, dev_t dev)
{
	return 0;
}

static inline int security_inode_rename(struct inode *old_dir,
					 struct dentry *old_dentry,
					 struct inode *new_dir,
					 struct dentry *new_dentry,
					 unsigned int flags)
{
	return 0;
}

static inline int security_inode_readlink(struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_follow_link(struct dentry *dentry,
					     struct inode *inode,
					     bool rcu)
{
	return 0;
}

static inline int security_inode_permission(struct inode *inode, int mask)
{
	return 0;
}

static inline int security_inode_setattr(struct dentry *dentry,
					  struct iattr *attr)
{
	return 0;
}

static inline int security_inode_getattr(const struct path *path)
{
	return 0;
}

static inline int security_inode_setxattr(struct dentry *dentry,
		const char *name, const void *value, size_t size, int flags)
{
	return cap_inode_setxattr(dentry, name, value, size, flags);
}

static inline void security_inode_post_setxattr(struct dentry *dentry,
		const char *name, const void *value, size_t size, int flags)
{ }

static inline int security_inode_getxattr(struct dentry *dentry,
			const char *name)
{
	return 0;
}

static inline int security_inode_listxattr(struct dentry *dentry)
{
	return 0;
}

static inline int security_inode_removexattr(struct dentry *dentry,
			const char *name)
{
	return cap_inode_removexattr(dentry, name);
}

static inline int security_inode_need_killpriv(struct dentry *dentry)
{
	return cap_inode_need_killpriv(dentry);
}

static inline int security_inode_killpriv(struct dentry *dentry)
{
	return cap_inode_killpriv(dentry);
}

static inline int security_inode_getsecurity(struct inode *inode, const char *name, void **buffer, bool alloc)
{
	return -EOPNOTSUPP;
}

static inline int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	return 0;
}

static inline void security_inode_getsecid(struct inode *inode, u32 *secid)
{
	*secid = 0;
}

static inline int security_inode_copy_up(struct dentry *src, struct cred **new)
{
	return 0;
}

static inline int security_inode_copy_up_xattr(const char *name)
{
	return -EOPNOTSUPP;
}

static inline int security_file_permission(struct file *file, int mask)
{
	return 0;
}

static inline int security_file_alloc(struct file *file)
{
	return 0;
}

static inline void security_file_free(struct file *file)
{ }

static inline int security_file_ioctl(struct file *file, unsigned int cmd,
				      unsigned long arg)
{
	return 0;
}

static inline int security_mmap_file(struct file *file, unsigned long prot,
				     unsigned long flags)
{
	return 0;
}

static inline int security_mmap_addr(unsigned long addr)
{
	return cap_mmap_addr(addr);
}

static inline int security_file_mprotect(struct vm_area_struct *vma,
					 unsigned long reqprot,
					 unsigned long prot)
{
	return 0;
}

static inline int security_file_lock(struct file *file, unsigned int cmd)
{
	return 0;
}

static inline int security_file_fcntl(struct file *file, unsigned int cmd,
				      unsigned long arg)
{
	return 0;
}

static inline void security_file_set_fowner(struct file *file)
{
	return;
}

static inline int security_file_send_sigiotask(struct task_struct *tsk,
					       struct fown_struct *fown,
					       int sig)
{
	return 0;
}

static inline int security_file_receive(struct file *file)
{
	return 0;
}

static inline int security_file_open(struct file *file,
				     const struct cred *cred)
{
	return 0;
}

static inline int security_task_create(unsigned long clone_flags)
{
	return 0;
}

static inline void security_task_free(struct task_struct *task)
{ }

static inline int security_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	return 0;
}

static inline void security_cred_free(struct cred *cred)
{ }

static inline int security_prepare_creds(struct cred *new,
					 const struct cred *old,
					 gfp_t gfp)
{
	return 0;
}

static inline void security_transfer_creds(struct cred *new,
					   const struct cred *old)
{
}

static inline int security_kernel_act_as(struct cred *cred, u32 secid)
{
	return 0;
}

static inline int security_kernel_create_files_as(struct cred *cred,
						  struct inode *inode)
{
	return 0;
}

static inline int security_kernel_module_request(char *kmod_name)
{
	return 0;
}

static inline int security_kernel_read_file(struct file *file,
					    enum kernel_read_file_id id)
{
	return 0;
}

static inline int security_kernel_post_read_file(struct file *file,
						 char *buf, loff_t size,
						 enum kernel_read_file_id id)
{
	return 0;
}

static inline int security_task_fix_setuid(struct cred *new,
					   const struct cred *old,
					   int flags)
{
	return cap_task_fix_setuid(new, old, flags);
}

static inline int security_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return 0;
}

static inline int security_task_getpgid(struct task_struct *p)
{
	return 0;
}

static inline int security_task_getsid(struct task_struct *p)
{
	return 0;
}

static inline void security_task_getsecid(struct task_struct *p, u32 *secid)
{
	*secid = 0;
}

static inline int security_task_setnice(struct task_struct *p, int nice)
{
	return cap_task_setnice(p, nice);
}

static inline int security_task_setioprio(struct task_struct *p, int ioprio)
{
	return cap_task_setioprio(p, ioprio);
}

static inline int security_task_getioprio(struct task_struct *p)
{
	return 0;
}

static inline int security_task_setrlimit(struct task_struct *p,
					  unsigned int resource,
					  struct rlimit *new_rlim)
{
	return 0;
}

static inline int security_task_setscheduler(struct task_struct *p)
{
	return cap_task_setscheduler(p);
}

static inline int security_task_getscheduler(struct task_struct *p)
{
	return 0;
}

static inline int security_task_movememory(struct task_struct *p)
{
	return 0;
}

static inline int security_task_kill(struct task_struct *p,
				     struct siginfo *info, int sig,
				     u32 secid)
{
	return 0;
}

static inline int security_task_prctl(int option, unsigned long arg2,
				      unsigned long arg3,
				      unsigned long arg4,
				      unsigned long arg5)
{
	return cap_task_prctl(option, arg2, arg3, arg4, arg5);
}

static inline void security_task_to_inode(struct task_struct *p, struct inode *inode)
{ }

static inline int security_ipc_permission(struct kern_ipc_perm *ipcp,
					  short flag)
{
	return 0;
}

static inline void security_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	*secid = 0;
}

static inline int security_msg_msg_alloc(struct msg_msg *msg)
{
	return 0;
}

static inline void security_msg_msg_free(struct msg_msg *msg)
{ }

static inline int security_msg_queue_alloc(struct msg_queue *msq)
{
	return 0;
}

static inline void security_msg_queue_free(struct msg_queue *msq)
{ }

static inline int security_msg_queue_associate(struct msg_queue *msq,
					       int msqflg)
{
	return 0;
}

static inline int security_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	return 0;
}

static inline int security_msg_queue_msgsnd(struct msg_queue *msq,
					    struct msg_msg *msg, int msqflg)
{
	return 0;
}

static inline int security_msg_queue_msgrcv(struct msg_queue *msq,
					    struct msg_msg *msg,
					    struct task_struct *target,
					    long type, int mode)
{
	return 0;
}

static inline int security_shm_alloc(struct shmid_kernel *shp)
{
	return 0;
}

static inline void security_shm_free(struct shmid_kernel *shp)
{ }

static inline int security_shm_associate(struct shmid_kernel *shp,
					 int shmflg)
{
	return 0;
}

static inline int security_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	return 0;
}

static inline int security_shm_shmat(struct shmid_kernel *shp,
				     char __user *shmaddr, int shmflg)
{
	return 0;
}

static inline int security_sem_alloc(struct sem_array *sma)
{
	return 0;
}

static inline void security_sem_free(struct sem_array *sma)
{ }

static inline int security_sem_associate(struct sem_array *sma, int semflg)
{
	return 0;
}

static inline int security_sem_semctl(struct sem_array *sma, int cmd)
{
	return 0;
}

static inline int security_sem_semop(struct sem_array *sma,
				     struct sembuf *sops, unsigned nsops,
				     int alter)
{
	return 0;
}

static inline void security_d_instantiate(struct dentry *dentry, struct inode *inode)
{ }

static inline int security_getprocattr(struct task_struct *p, char *name, char **value)
{
	return -EINVAL;
}

static inline int security_setprocattr(char *name, void *value, size_t size)
{
	return -EINVAL;
}

static inline int security_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return 0;
}

static inline int security_ismaclabel(const char *name)
{
	return 0;
}

static inline int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return -EOPNOTSUPP;
}

static inline int security_secctx_to_secid(const char *secdata,
					   u32 seclen,
					   u32 *secid)
{
	return -EOPNOTSUPP;
}

static inline void security_release_secctx(char *secdata, u32 seclen)
{
}

static inline void security_inode_invalidate_secctx(struct inode *inode)
{
}

static inline int security_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	return -EOPNOTSUPP;
}
static inline int security_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return -EOPNOTSUPP;
}
static inline int security_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	return -EOPNOTSUPP;
}
#endif	/* CONFIG_SECURITY */

#ifdef CONFIG_SECURITY_NETWORK

int security_unix_stream_connect(struct sock *sock, struct sock *other, struct sock *newsk);
int security_unix_may_send(struct socket *sock,  struct socket *other);
int security_socket_create(int family, int type, int protocol, int kern);
int security_socket_post_create(struct socket *sock, int family,
				int type, int protocol, int kern);
int security_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen);
int security_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen);
int security_socket_listen(struct socket *sock, int backlog);
int security_socket_accept(struct socket *sock, struct socket *newsock);
int security_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size);
int security_socket_recvmsg(struct socket *sock, struct msghdr *msg,
			    int size, int flags);
int security_socket_getsockname(struct socket *sock);
int security_socket_getpeername(struct socket *sock);
int security_socket_getsockopt(struct socket *sock, int level, int optname);
int security_socket_setsockopt(struct socket *sock, int level, int optname);
int security_socket_shutdown(struct socket *sock, int how);
int security_sock_rcv_skb(struct sock *sk, struct sk_buff *skb);
int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
				      int __user *optlen, unsigned len);
int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid);
int security_sk_alloc(struct sock *sk, int family, gfp_t priority);
void security_sk_free(struct sock *sk);
void security_sk_clone(const struct sock *sk, struct sock *newsk);
void security_sk_classify_flow(struct sock *sk, struct flowi *fl);
void security_req_classify_flow(const struct request_sock *req, struct flowi *fl);
void security_sock_graft(struct sock*sk, struct socket *parent);
int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req);
void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req);
void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb);
int security_secmark_relabel_packet(u32 secid);
void security_secmark_refcount_inc(void);
void security_secmark_refcount_dec(void);
int security_tun_dev_alloc_security(void **security);
void security_tun_dev_free_security(void *security);
int security_tun_dev_create(void);
int security_tun_dev_attach_queue(void *security);
int security_tun_dev_attach(struct sock *sk, void *security);
int security_tun_dev_open(void *security);

#else	/* CONFIG_SECURITY_NETWORK */
static inline int security_unix_stream_connect(struct sock *sock,
					       struct sock *other,
					       struct sock *newsk)
{
	return 0;
}

static inline int security_unix_may_send(struct socket *sock,
					 struct socket *other)
{
	return 0;
}

static inline int security_socket_create(int family, int type,
					 int protocol, int kern)
{
	return 0;
}

static inline int security_socket_post_create(struct socket *sock,
					      int family,
					      int type,
					      int protocol, int kern)
{
	return 0;
}

static inline int security_socket_bind(struct socket *sock,
				       struct sockaddr *address,
				       int addrlen)
{
	return 0;
}

static inline int security_socket_connect(struct socket *sock,
					  struct sockaddr *address,
					  int addrlen)
{
	return 0;
}

static inline int security_socket_listen(struct socket *sock, int backlog)
{
	return 0;
}

static inline int security_socket_accept(struct socket *sock,
					 struct socket *newsock)
{
	return 0;
}

static inline int security_socket_sendmsg(struct socket *sock,
					  struct msghdr *msg, int size)
{
	return 0;
}

static inline int security_socket_recvmsg(struct socket *sock,
					  struct msghdr *msg, int size,
					  int flags)
{
	return 0;
}

static inline int security_socket_getsockname(struct socket *sock)
{
	return 0;
}

static inline int security_socket_getpeername(struct socket *sock)
{
	return 0;
}

static inline int security_socket_getsockopt(struct socket *sock,
					     int level, int optname)
{
	return 0;
}

static inline int security_socket_setsockopt(struct socket *sock,
					     int level, int optname)
{
	return 0;
}

static inline int security_socket_shutdown(struct socket *sock, int how)
{
	return 0;
}
static inline int security_sock_rcv_skb(struct sock *sk,
					struct sk_buff *skb)
{
	return 0;
}

static inline int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
						    int __user *optlen, unsigned len)
{
	return -ENOPROTOOPT;
}

static inline int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return -ENOPROTOOPT;
}

static inline int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return 0;
}

static inline void security_sk_free(struct sock *sk)
{
}

static inline void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
}

static inline void security_sk_classify_flow(struct sock *sk, struct flowi *fl)
{
}

static inline void security_req_classify_flow(const struct request_sock *req, struct flowi *fl)
{
}

static inline void security_sock_graft(struct sock *sk, struct socket *parent)
{
}

static inline int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return 0;
}

static inline void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
}

static inline void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
}

static inline int security_secmark_relabel_packet(u32 secid)
{
	return 0;
}

static inline void security_secmark_refcount_inc(void)
{
}

static inline void security_secmark_refcount_dec(void)
{
}

static inline int security_tun_dev_alloc_security(void **security)
{
	return 0;
}

static inline void security_tun_dev_free_security(void *security)
{
}

static inline int security_tun_dev_create(void)
{
	return 0;
}

static inline int security_tun_dev_attach_queue(void *security)
{
	return 0;
}

static inline int security_tun_dev_attach(struct sock *sk, void *security)
{
	return 0;
}

static inline int security_tun_dev_open(void *security)
{
	return 0;
}
#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM

int security_xfrm_policy_alloc(struct xfrm_sec_ctx **ctxp,
			       struct xfrm_user_sec_ctx *sec_ctx, gfp_t gfp);
int security_xfrm_policy_clone(struct xfrm_sec_ctx *old_ctx, struct xfrm_sec_ctx **new_ctxp);
void security_xfrm_policy_free(struct xfrm_sec_ctx *ctx);
int security_xfrm_policy_delete(struct xfrm_sec_ctx *ctx);
int security_xfrm_state_alloc(struct xfrm_state *x, struct xfrm_user_sec_ctx *sec_ctx);
int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
				      struct xfrm_sec_ctx *polsec, u32 secid);
int security_xfrm_state_delete(struct xfrm_state *x);
void security_xfrm_state_free(struct xfrm_state *x);
int security_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 fl_secid, u8 dir);
int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
				       struct xfrm_policy *xp,
				       const struct flowi *fl);
int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid);
void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl);

#else	/* CONFIG_SECURITY_NETWORK_XFRM */

static inline int security_xfrm_policy_alloc(struct xfrm_sec_ctx **ctxp,
					     struct xfrm_user_sec_ctx *sec_ctx,
					     gfp_t gfp)
{
	return 0;
}

static inline int security_xfrm_policy_clone(struct xfrm_sec_ctx *old, struct xfrm_sec_ctx **new_ctxp)
{
	return 0;
}

static inline void security_xfrm_policy_free(struct xfrm_sec_ctx *ctx)
{
}

static inline int security_xfrm_policy_delete(struct xfrm_sec_ctx *ctx)
{
	return 0;
}

static inline int security_xfrm_state_alloc(struct xfrm_state *x,
					struct xfrm_user_sec_ctx *sec_ctx)
{
	return 0;
}

static inline int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
					struct xfrm_sec_ctx *polsec, u32 secid)
{
	return 0;
}

static inline void security_xfrm_state_free(struct xfrm_state *x)
{
}

static inline int security_xfrm_state_delete(struct xfrm_state *x)
{
	return 0;
}

static inline int security_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 fl_secid, u8 dir)
{
	return 0;
}

static inline int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
			struct xfrm_policy *xp, const struct flowi *fl)
{
	return 1;
}

static inline int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return 0;
}

static inline void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl)
{
}

#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

#ifdef CONFIG_SECURITY_PATH
int security_path_unlink(const struct path *dir, struct dentry *dentry);
int security_path_mkdir(const struct path *dir, struct dentry *dentry, umode_t mode);
int security_path_rmdir(const struct path *dir, struct dentry *dentry);
int security_path_mknod(const struct path *dir, struct dentry *dentry, umode_t mode,
			unsigned int dev);
int security_path_truncate(const struct path *path);
int security_path_symlink(const struct path *dir, struct dentry *dentry,
			  const char *old_name);
int security_path_link(struct dentry *old_dentry, const struct path *new_dir,
		       struct dentry *new_dentry);
int security_path_rename(const struct path *old_dir, struct dentry *old_dentry,
			 const struct path *new_dir, struct dentry *new_dentry,
			 unsigned int flags);
int security_path_chmod(const struct path *path, umode_t mode);
int security_path_chown(const struct path *path, kuid_t uid, kgid_t gid);
int security_path_chroot(const struct path *path);
#else	/* CONFIG_SECURITY_PATH */
static inline int security_path_unlink(const struct path *dir, struct dentry *dentry)
{
	return 0;
}

static inline int security_path_mkdir(const struct path *dir, struct dentry *dentry,
				      umode_t mode)
{
	return 0;
}

static inline int security_path_rmdir(const struct path *dir, struct dentry *dentry)
{
	return 0;
}

static inline int security_path_mknod(const struct path *dir, struct dentry *dentry,
				      umode_t mode, unsigned int dev)
{
	return 0;
}

static inline int security_path_truncate(const struct path *path)
{
	return 0;
}

static inline int security_path_symlink(const struct path *dir, struct dentry *dentry,
					const char *old_name)
{
	return 0;
}

static inline int security_path_link(struct dentry *old_dentry,
				     const struct path *new_dir,
				     struct dentry *new_dentry)
{
	return 0;
}

static inline int security_path_rename(const struct path *old_dir,
				       struct dentry *old_dentry,
				       const struct path *new_dir,
				       struct dentry *new_dentry,
				       unsigned int flags)
{
	return 0;
}

static inline int security_path_chmod(const struct path *path, umode_t mode)
{
	return 0;
}

static inline int security_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	return 0;
}

static inline int security_path_chroot(const struct path *path)
{
	return 0;
}
#endif	/* CONFIG_SECURITY_PATH */

#ifdef CONFIG_KEYS
#ifdef CONFIG_SECURITY

int security_key_alloc(struct key *key, const struct cred *cred, unsigned long flags);
void security_key_free(struct key *key);
int security_key_permission(key_ref_t key_ref,
			    const struct cred *cred, unsigned perm);
int security_key_getsecurity(struct key *key, char **_buffer);

#else

static inline int security_key_alloc(struct key *key,
				     const struct cred *cred,
				     unsigned long flags)
{
	return 0;
}

static inline void security_key_free(struct key *key)
{
}

static inline int security_key_permission(key_ref_t key_ref,
					  const struct cred *cred,
					  unsigned perm)
{
	return 0;
}

static inline int security_key_getsecurity(struct key *key, char **_buffer)
{
	*_buffer = NULL;
	return 0;
}

#endif
#endif /* CONFIG_KEYS */

#ifdef CONFIG_AUDIT
#ifdef CONFIG_SECURITY
int security_audit_rule_init(u32 field, u32 op, char *rulestr, void **lsmrule);
int security_audit_rule_known(struct audit_krule *krule);
int security_audit_rule_match(u32 secid, u32 field, u32 op, void *lsmrule,
			      struct audit_context *actx);
void security_audit_rule_free(void *lsmrule);

#else

static inline int security_audit_rule_init(u32 field, u32 op, char *rulestr,
					   void **lsmrule)
{
	return 0;
}

static inline int security_audit_rule_known(struct audit_krule *krule)
{
	return 0;
}

static inline int security_audit_rule_match(u32 secid, u32 field, u32 op,
				   void *lsmrule, struct audit_context *actx)
{
	return 0;
}

static inline void security_audit_rule_free(void *lsmrule)
{ }

#endif /* CONFIG_SECURITY */
#endif /* CONFIG_AUDIT */

#ifdef CONFIG_SECURITYFS

extern struct dentry *securityfs_create_file(const char *name, umode_t mode,
					     struct dentry *parent, void *data,
					     const struct file_operations *fops);
extern struct dentry *securityfs_create_dir(const char *name, struct dentry *parent);
extern void securityfs_remove(struct dentry *dentry);

#else /* CONFIG_SECURITYFS */

static inline struct dentry *securityfs_create_dir(const char *name,
						   struct dentry *parent)
{
	return ERR_PTR(-ENODEV);
}

static inline struct dentry *securityfs_create_file(const char *name,
						    umode_t mode,
						    struct dentry *parent,
						    void *data,
						    const struct file_operations *fops)
{
	return ERR_PTR(-ENODEV);
}

static inline void securityfs_remove(struct dentry *dentry)
{}

#endif

#ifdef CONFIG_SECURITY

static inline char *alloc_secdata(void)
{
	return (char *)get_zeroed_page(GFP_KERNEL);
}

static inline void free_secdata(void *secdata)
{
	free_page((unsigned long)secdata);
}

#else

static inline char *alloc_secdata(void)
{
        return (char *)1;
}

static inline void free_secdata(void *secdata)
{ }
#endif /* CONFIG_SECURITY */

#endif /* ! __LINUX_SECURITY_H */

