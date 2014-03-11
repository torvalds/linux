/* audit.h -- Auditing support
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Written by Rickard E. (Rik) Faith <faith@redhat.com>
 *
 */
#ifndef _LINUX_AUDIT_H_
#define _LINUX_AUDIT_H_

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <uapi/linux/audit.h>
#include <asm/syscall.h>

struct audit_sig_info {
	uid_t		uid;
	pid_t		pid;
	char		ctx[0];
};

struct audit_buffer;
struct audit_context;
struct inode;
struct netlink_skb_parms;
struct path;
struct linux_binprm;
struct mq_attr;
struct mqstat;
struct audit_watch;
struct audit_tree;
struct sk_buff;

struct audit_krule {
	int			vers_ops;
	u32			flags;
	u32			listnr;
	u32			action;
	u32			mask[AUDIT_BITMASK_SIZE];
	u32			buflen; /* for data alloc on list rules */
	u32			field_count;
	char			*filterkey; /* ties events to rules */
	struct audit_field	*fields;
	struct audit_field	*arch_f; /* quick access to arch field */
	struct audit_field	*inode_f; /* quick access to an inode field */
	struct audit_watch	*watch;	/* associated watch */
	struct audit_tree	*tree;	/* associated watched tree */
	struct list_head	rlist;	/* entry in audit_{watch,tree}.rules list */
	struct list_head	list;	/* for AUDIT_LIST* purposes only */
	u64			prio;
};

struct audit_field {
	u32				type;
	u32				val;
	kuid_t				uid;
	kgid_t				gid;
	u32				op;
	char				*lsm_str;
	void				*lsm_rule;
};

extern int is_audit_feature_set(int which);

extern int __init audit_register_class(int class, unsigned *list);
extern int audit_classify_syscall(int abi, unsigned syscall);
extern int audit_classify_arch(int arch);
/* only for compat system calls */
extern unsigned compat_write_class[];
extern unsigned compat_read_class[];
extern unsigned compat_dir_class[];
extern unsigned compat_chattr_class[];
extern unsigned compat_signal_class[];

extern int __weak audit_classify_compat_syscall(int abi, unsigned syscall);

/* audit_names->type values */
#define	AUDIT_TYPE_UNKNOWN	0	/* we don't know yet */
#define	AUDIT_TYPE_NORMAL	1	/* a "normal" audit record */
#define	AUDIT_TYPE_PARENT	2	/* a parent audit record */
#define	AUDIT_TYPE_CHILD_DELETE 3	/* a child being deleted */
#define	AUDIT_TYPE_CHILD_CREATE 4	/* a child being created */

/* maximized args number that audit_socketcall can process */
#define AUDITSC_ARGS		6

struct filename;

extern void audit_log_session_info(struct audit_buffer *ab);

#ifdef CONFIG_AUDIT_COMPAT_GENERIC
#define audit_is_compat(arch)  (!((arch) & __AUDIT_ARCH_64BIT))
#else
#define audit_is_compat(arch)  false
#endif

#ifdef CONFIG_AUDITSYSCALL
/* These are defined in auditsc.c */
				/* Public API */
extern int  audit_alloc(struct task_struct *task);
extern void __audit_free(struct task_struct *task);
extern void __audit_syscall_entry(int arch,
				  int major, unsigned long a0, unsigned long a1,
				  unsigned long a2, unsigned long a3);
extern void __audit_syscall_exit(int ret_success, long ret_value);
extern struct filename *__audit_reusename(const __user char *uptr);
extern void __audit_getname(struct filename *name);
extern void audit_putname(struct filename *name);

#define AUDIT_INODE_PARENT	1	/* dentry represents the parent */
#define AUDIT_INODE_HIDDEN	2	/* audit record should be hidden */
extern void __audit_inode(struct filename *name, const struct dentry *dentry,
				unsigned int flags);
extern void __audit_inode_child(const struct inode *parent,
				const struct dentry *dentry,
				const unsigned char type);
extern void __audit_seccomp(unsigned long syscall, long signr, int code);
extern void __audit_ptrace(struct task_struct *t);

static inline int audit_dummy_context(void)
{
	void *p = current->audit_context;
	return !p || *(int *)p;
}
static inline void audit_free(struct task_struct *task)
{
	if (unlikely(task->audit_context))
		__audit_free(task);
}
static inline void audit_syscall_entry(int major, unsigned long a0,
				       unsigned long a1, unsigned long a2,
				       unsigned long a3)
{
	if (unlikely(current->audit_context))
		__audit_syscall_entry(syscall_get_arch(), major, a0, a1, a2, a3);
}
static inline void audit_syscall_exit(void *pt_regs)
{
	if (unlikely(current->audit_context)) {
		int success = is_syscall_success(pt_regs);
		long return_code = regs_return_value(pt_regs);

		__audit_syscall_exit(success, return_code);
	}
}
static inline struct filename *audit_reusename(const __user char *name)
{
	if (unlikely(!audit_dummy_context()))
		return __audit_reusename(name);
	return NULL;
}
static inline void audit_getname(struct filename *name)
{
	if (unlikely(!audit_dummy_context()))
		__audit_getname(name);
}
static inline void audit_inode(struct filename *name,
				const struct dentry *dentry,
				unsigned int parent) {
	if (unlikely(!audit_dummy_context())) {
		unsigned int flags = 0;
		if (parent)
			flags |= AUDIT_INODE_PARENT;
		__audit_inode(name, dentry, flags);
	}
}
static inline void audit_inode_parent_hidden(struct filename *name,
						const struct dentry *dentry)
{
	if (unlikely(!audit_dummy_context()))
		__audit_inode(name, dentry,
				AUDIT_INODE_PARENT | AUDIT_INODE_HIDDEN);
}
static inline void audit_inode_child(const struct inode *parent,
				     const struct dentry *dentry,
				     const unsigned char type) {
	if (unlikely(!audit_dummy_context()))
		__audit_inode_child(parent, dentry, type);
}
void audit_core_dumps(long signr);

static inline void audit_seccomp(unsigned long syscall, long signr, int code)
{
	/* Force a record to be reported if a signal was delivered. */
	if (signr || unlikely(!audit_dummy_context()))
		__audit_seccomp(syscall, signr, code);
}

static inline void audit_ptrace(struct task_struct *t)
{
	if (unlikely(!audit_dummy_context()))
		__audit_ptrace(t);
}

				/* Private API (for audit.c only) */
extern unsigned int audit_serial(void);
extern int auditsc_get_stamp(struct audit_context *ctx,
			      struct timespec *t, unsigned int *serial);
extern int audit_set_loginuid(kuid_t loginuid);

static inline kuid_t audit_get_loginuid(struct task_struct *tsk)
{
	return tsk->loginuid;
}

static inline unsigned int audit_get_sessionid(struct task_struct *tsk)
{
	return tsk->sessionid;
}

extern void __audit_ipc_obj(struct kern_ipc_perm *ipcp);
extern void __audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid, umode_t mode);
extern void __audit_bprm(struct linux_binprm *bprm);
extern int __audit_socketcall(int nargs, unsigned long *args);
extern int __audit_sockaddr(int len, void *addr);
extern void __audit_fd_pair(int fd1, int fd2);
extern void __audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr);
extern void __audit_mq_sendrecv(mqd_t mqdes, size_t msg_len, unsigned int msg_prio, const struct timespec *abs_timeout);
extern void __audit_mq_notify(mqd_t mqdes, const struct sigevent *notification);
extern void __audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat);
extern int __audit_log_bprm_fcaps(struct linux_binprm *bprm,
				  const struct cred *new,
				  const struct cred *old);
extern void __audit_log_capset(const struct cred *new, const struct cred *old);
extern void __audit_mmap_fd(int fd, int flags);

static inline void audit_ipc_obj(struct kern_ipc_perm *ipcp)
{
	if (unlikely(!audit_dummy_context()))
		__audit_ipc_obj(ipcp);
}
static inline void audit_fd_pair(int fd1, int fd2)
{
	if (unlikely(!audit_dummy_context()))
		__audit_fd_pair(fd1, fd2);
}
static inline void audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid, umode_t mode)
{
	if (unlikely(!audit_dummy_context()))
		__audit_ipc_set_perm(qbytes, uid, gid, mode);
}
static inline void audit_bprm(struct linux_binprm *bprm)
{
	if (unlikely(!audit_dummy_context()))
		__audit_bprm(bprm);
}
static inline int audit_socketcall(int nargs, unsigned long *args)
{
	if (unlikely(!audit_dummy_context()))
		return __audit_socketcall(nargs, args);
	return 0;
}
static inline int audit_sockaddr(int len, void *addr)
{
	if (unlikely(!audit_dummy_context()))
		return __audit_sockaddr(len, addr);
	return 0;
}
static inline void audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr)
{
	if (unlikely(!audit_dummy_context()))
		__audit_mq_open(oflag, mode, attr);
}
static inline void audit_mq_sendrecv(mqd_t mqdes, size_t msg_len, unsigned int msg_prio, const struct timespec *abs_timeout)
{
	if (unlikely(!audit_dummy_context()))
		__audit_mq_sendrecv(mqdes, msg_len, msg_prio, abs_timeout);
}
static inline void audit_mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
	if (unlikely(!audit_dummy_context()))
		__audit_mq_notify(mqdes, notification);
}
static inline void audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
{
	if (unlikely(!audit_dummy_context()))
		__audit_mq_getsetattr(mqdes, mqstat);
}

static inline int audit_log_bprm_fcaps(struct linux_binprm *bprm,
				       const struct cred *new,
				       const struct cred *old)
{
	if (unlikely(!audit_dummy_context()))
		return __audit_log_bprm_fcaps(bprm, new, old);
	return 0;
}

static inline void audit_log_capset(const struct cred *new,
				   const struct cred *old)
{
	if (unlikely(!audit_dummy_context()))
		__audit_log_capset(new, old);
}

static inline void audit_mmap_fd(int fd, int flags)
{
	if (unlikely(!audit_dummy_context()))
		__audit_mmap_fd(fd, flags);
}

extern int audit_n_rules;
extern int audit_signals;
#else /* CONFIG_AUDITSYSCALL */
static inline int audit_alloc(struct task_struct *task)
{
	return 0;
}
static inline void audit_free(struct task_struct *task)
{ }
static inline void audit_syscall_entry(int major, unsigned long a0,
				       unsigned long a1, unsigned long a2,
				       unsigned long a3)
{ }
static inline void audit_syscall_exit(void *pt_regs)
{ }
static inline int audit_dummy_context(void)
{
	return 1;
}
static inline struct filename *audit_reusename(const __user char *name)
{
	return NULL;
}
static inline void audit_getname(struct filename *name)
{ }
static inline void audit_putname(struct filename *name)
{ }
static inline void __audit_inode(struct filename *name,
					const struct dentry *dentry,
					unsigned int flags)
{ }
static inline void __audit_inode_child(const struct inode *parent,
					const struct dentry *dentry,
					const unsigned char type)
{ }
static inline void audit_inode(struct filename *name,
				const struct dentry *dentry,
				unsigned int parent)
{ }
static inline void audit_inode_parent_hidden(struct filename *name,
				const struct dentry *dentry)
{ }
static inline void audit_inode_child(const struct inode *parent,
				     const struct dentry *dentry,
				     const unsigned char type)
{ }
static inline void audit_core_dumps(long signr)
{ }
static inline void __audit_seccomp(unsigned long syscall, long signr, int code)
{ }
static inline void audit_seccomp(unsigned long syscall, long signr, int code)
{ }
static inline int auditsc_get_stamp(struct audit_context *ctx,
			      struct timespec *t, unsigned int *serial)
{
	return 0;
}
static inline kuid_t audit_get_loginuid(struct task_struct *tsk)
{
	return INVALID_UID;
}
static inline unsigned int audit_get_sessionid(struct task_struct *tsk)
{
	return -1;
}
static inline void audit_ipc_obj(struct kern_ipc_perm *ipcp)
{ }
static inline void audit_ipc_set_perm(unsigned long qbytes, uid_t uid,
					gid_t gid, umode_t mode)
{ }
static inline void audit_bprm(struct linux_binprm *bprm)
{ }
static inline int audit_socketcall(int nargs, unsigned long *args)
{
	return 0;
}
static inline void audit_fd_pair(int fd1, int fd2)
{ }
static inline int audit_sockaddr(int len, void *addr)
{
	return 0;
}
static inline void audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr)
{ }
static inline void audit_mq_sendrecv(mqd_t mqdes, size_t msg_len,
				     unsigned int msg_prio,
				     const struct timespec *abs_timeout)
{ }
static inline void audit_mq_notify(mqd_t mqdes,
				   const struct sigevent *notification)
{ }
static inline void audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
{ }
static inline int audit_log_bprm_fcaps(struct linux_binprm *bprm,
				       const struct cred *new,
				       const struct cred *old)
{
	return 0;
}
static inline void audit_log_capset(const struct cred *new,
				    const struct cred *old)
{ }
static inline void audit_mmap_fd(int fd, int flags)
{ }
static inline void audit_ptrace(struct task_struct *t)
{ }
#define audit_n_rules 0
#define audit_signals 0
#endif /* CONFIG_AUDITSYSCALL */

static inline bool audit_loginuid_set(struct task_struct *tsk)
{
	return uid_valid(audit_get_loginuid(tsk));
}

#ifdef CONFIG_AUDIT
/* These are defined in audit.c */
				/* Public API */
extern __printf(4, 5)
void audit_log(struct audit_context *ctx, gfp_t gfp_mask, int type,
	       const char *fmt, ...);

extern struct audit_buffer *audit_log_start(struct audit_context *ctx, gfp_t gfp_mask, int type);
extern __printf(2, 3)
void audit_log_format(struct audit_buffer *ab, const char *fmt, ...);
extern void		    audit_log_end(struct audit_buffer *ab);
extern int		    audit_string_contains_control(const char *string,
							  size_t len);
extern void		    audit_log_n_hex(struct audit_buffer *ab,
					  const unsigned char *buf,
					  size_t len);
extern void		    audit_log_n_string(struct audit_buffer *ab,
					       const char *buf,
					       size_t n);
extern void		    audit_log_n_untrustedstring(struct audit_buffer *ab,
							const char *string,
							size_t n);
extern void		    audit_log_untrustedstring(struct audit_buffer *ab,
						      const char *string);
extern void		    audit_log_d_path(struct audit_buffer *ab,
					     const char *prefix,
					     const struct path *path);
extern void		    audit_log_key(struct audit_buffer *ab,
					  char *key);
extern void		    audit_log_link_denied(const char *operation,
						  struct path *link);
extern void		    audit_log_lost(const char *message);
#ifdef CONFIG_SECURITY
extern void 		    audit_log_secctx(struct audit_buffer *ab, u32 secid);
#else
static inline void	    audit_log_secctx(struct audit_buffer *ab, u32 secid)
{ }
#endif

extern int audit_log_task_context(struct audit_buffer *ab);
extern void audit_log_task_info(struct audit_buffer *ab,
				struct task_struct *tsk);

extern int		    audit_update_lsm_rules(void);

				/* Private API (for audit.c only) */
extern int audit_filter_user(int type);
extern int audit_filter_type(int type);
extern int audit_rule_change(int type, __u32 portid, int seq,
				void *data, size_t datasz);
extern int audit_list_rules_send(struct sk_buff *request_skb, int seq);

extern u32 audit_enabled;
#else /* CONFIG_AUDIT */
static inline __printf(4, 5)
void audit_log(struct audit_context *ctx, gfp_t gfp_mask, int type,
	       const char *fmt, ...)
{ }
static inline struct audit_buffer *audit_log_start(struct audit_context *ctx,
						   gfp_t gfp_mask, int type)
{
	return NULL;
}
static inline __printf(2, 3)
void audit_log_format(struct audit_buffer *ab, const char *fmt, ...)
{ }
static inline void audit_log_end(struct audit_buffer *ab)
{ }
static inline void audit_log_n_hex(struct audit_buffer *ab,
				   const unsigned char *buf, size_t len)
{ }
static inline void audit_log_n_string(struct audit_buffer *ab,
				      const char *buf, size_t n)
{ }
static inline void  audit_log_n_untrustedstring(struct audit_buffer *ab,
						const char *string, size_t n)
{ }
static inline void audit_log_untrustedstring(struct audit_buffer *ab,
					     const char *string)
{ }
static inline void audit_log_d_path(struct audit_buffer *ab,
				    const char *prefix,
				    const struct path *path)
{ }
static inline void audit_log_key(struct audit_buffer *ab, char *key)
{ }
static inline void audit_log_link_denied(const char *string,
					 const struct path *link)
{ }
static inline void audit_log_secctx(struct audit_buffer *ab, u32 secid)
{ }
static inline int audit_log_task_context(struct audit_buffer *ab)
{
	return 0;
}
static inline void audit_log_task_info(struct audit_buffer *ab,
				       struct task_struct *tsk)
{ }
#define audit_enabled 0
#endif /* CONFIG_AUDIT */
static inline void audit_log_string(struct audit_buffer *ab, const char *buf)
{
	audit_log_n_string(ab, buf, strlen(buf));
}

#endif
