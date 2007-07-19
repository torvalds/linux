/* auditsc.c -- System-call auditing support
 * Handles all system-call specific auditing features.
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2005, 2006 IBM Corporation
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
 * Many of the ideas implemented here are from Stephen C. Tweedie,
 * especially the idea of avoiding a copy by using getname.
 *
 * The method for actual interception of syscall entry and exit (not in
 * this file -- see entry.S) is based on a GPL'd patch written by
 * okir@suse.de and Copyright 2003 SuSE Linux AG.
 *
 * POSIX message queue support added by George Wilson <ltcgcw@us.ibm.com>,
 * 2006.
 *
 * The support of additional filter rules compares (>, <, >=, <=) was
 * added by Dustin Kirkland <dustin.kirkland@us.ibm.com>, 2005.
 *
 * Modified by Amy Griffis <amy.griffis@hp.com> to collect additional
 * filesystem information.
 *
 * Subject and object context labeling support added by <danjones@us.ibm.com>
 * and <dustin.kirkland@us.ibm.com> for LSPP certification compliance.
 */

#include <linux/init.h>
#include <asm/types.h>
#include <asm/atomic.h>
#include <asm/types.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/socket.h>
#include <linux/mqueue.h>
#include <linux/audit.h>
#include <linux/personality.h>
#include <linux/time.h>
#include <linux/netlink.h>
#include <linux/compiler.h>
#include <asm/unistd.h>
#include <linux/security.h>
#include <linux/list.h>
#include <linux/tty.h>
#include <linux/selinux.h>
#include <linux/binfmts.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>

#include "audit.h"

extern struct list_head audit_filter_list[];

/* AUDIT_NAMES is the number of slots we reserve in the audit_context
 * for saving names from getname(). */
#define AUDIT_NAMES    20

/* Indicates that audit should log the full pathname. */
#define AUDIT_NAME_FULL -1

/* number of audit rules */
int audit_n_rules;

/* determines whether we collect data for signals sent */
int audit_signals;

/* When fs/namei.c:getname() is called, we store the pointer in name and
 * we don't let putname() free it (instead we free all of the saved
 * pointers at syscall exit time).
 *
 * Further, in fs/namei.c:path_lookup() we store the inode and device. */
struct audit_names {
	const char	*name;
	int		name_len;	/* number of name's characters to log */
	unsigned	name_put;	/* call __putname() for this name */
	unsigned long	ino;
	dev_t		dev;
	umode_t		mode;
	uid_t		uid;
	gid_t		gid;
	dev_t		rdev;
	u32		osid;
};

struct audit_aux_data {
	struct audit_aux_data	*next;
	int			type;
};

#define AUDIT_AUX_IPCPERM	0

/* Number of target pids per aux struct. */
#define AUDIT_AUX_PIDS	16

struct audit_aux_data_mq_open {
	struct audit_aux_data	d;
	int			oflag;
	mode_t			mode;
	struct mq_attr		attr;
};

struct audit_aux_data_mq_sendrecv {
	struct audit_aux_data	d;
	mqd_t			mqdes;
	size_t			msg_len;
	unsigned int		msg_prio;
	struct timespec		abs_timeout;
};

struct audit_aux_data_mq_notify {
	struct audit_aux_data	d;
	mqd_t			mqdes;
	struct sigevent 	notification;
};

struct audit_aux_data_mq_getsetattr {
	struct audit_aux_data	d;
	mqd_t			mqdes;
	struct mq_attr 		mqstat;
};

struct audit_aux_data_ipcctl {
	struct audit_aux_data	d;
	struct ipc_perm		p;
	unsigned long		qbytes;
	uid_t			uid;
	gid_t			gid;
	mode_t			mode;
	u32			osid;
};

struct audit_aux_data_execve {
	struct audit_aux_data	d;
	int argc;
	int envc;
	struct mm_struct *mm;
};

struct audit_aux_data_socketcall {
	struct audit_aux_data	d;
	int			nargs;
	unsigned long		args[0];
};

struct audit_aux_data_sockaddr {
	struct audit_aux_data	d;
	int			len;
	char			a[0];
};

struct audit_aux_data_fd_pair {
	struct	audit_aux_data d;
	int	fd[2];
};

struct audit_aux_data_path {
	struct audit_aux_data	d;
	struct dentry		*dentry;
	struct vfsmount		*mnt;
};

struct audit_aux_data_pids {
	struct audit_aux_data	d;
	pid_t			target_pid[AUDIT_AUX_PIDS];
	u32			target_sid[AUDIT_AUX_PIDS];
	int			pid_count;
};

/* The per-task audit context. */
struct audit_context {
	int		    dummy;	/* must be the first element */
	int		    in_syscall;	/* 1 if task is in a syscall */
	enum audit_state    state;
	unsigned int	    serial;     /* serial number for record */
	struct timespec	    ctime;      /* time of syscall entry */
	uid_t		    loginuid;   /* login uid (identity) */
	int		    major;      /* syscall number */
	unsigned long	    argv[4];    /* syscall arguments */
	int		    return_valid; /* return code is valid */
	long		    return_code;/* syscall return code */
	int		    auditable;  /* 1 if record should be written */
	int		    name_count;
	struct audit_names  names[AUDIT_NAMES];
	char *		    filterkey;	/* key for rule that triggered record */
	struct dentry *	    pwd;
	struct vfsmount *   pwdmnt;
	struct audit_context *previous; /* For nested syscalls */
	struct audit_aux_data *aux;
	struct audit_aux_data *aux_pids;

				/* Save things to print about task_struct */
	pid_t		    pid, ppid;
	uid_t		    uid, euid, suid, fsuid;
	gid_t		    gid, egid, sgid, fsgid;
	unsigned long	    personality;
	int		    arch;

	pid_t		    target_pid;
	u32		    target_sid;

#if AUDIT_DEBUG
	int		    put_count;
	int		    ino_count;
#endif
};

#define ACC_MODE(x) ("\004\002\006\006"[(x)&O_ACCMODE])
static inline int open_arg(int flags, int mask)
{
	int n = ACC_MODE(flags);
	if (flags & (O_TRUNC | O_CREAT))
		n |= AUDIT_PERM_WRITE;
	return n & mask;
}

static int audit_match_perm(struct audit_context *ctx, int mask)
{
	unsigned n = ctx->major;
	switch (audit_classify_syscall(ctx->arch, n)) {
	case 0:	/* native */
		if ((mask & AUDIT_PERM_WRITE) &&
		     audit_match_class(AUDIT_CLASS_WRITE, n))
			return 1;
		if ((mask & AUDIT_PERM_READ) &&
		     audit_match_class(AUDIT_CLASS_READ, n))
			return 1;
		if ((mask & AUDIT_PERM_ATTR) &&
		     audit_match_class(AUDIT_CLASS_CHATTR, n))
			return 1;
		return 0;
	case 1: /* 32bit on biarch */
		if ((mask & AUDIT_PERM_WRITE) &&
		     audit_match_class(AUDIT_CLASS_WRITE_32, n))
			return 1;
		if ((mask & AUDIT_PERM_READ) &&
		     audit_match_class(AUDIT_CLASS_READ_32, n))
			return 1;
		if ((mask & AUDIT_PERM_ATTR) &&
		     audit_match_class(AUDIT_CLASS_CHATTR_32, n))
			return 1;
		return 0;
	case 2: /* open */
		return mask & ACC_MODE(ctx->argv[1]);
	case 3: /* openat */
		return mask & ACC_MODE(ctx->argv[2]);
	case 4: /* socketcall */
		return ((mask & AUDIT_PERM_WRITE) && ctx->argv[0] == SYS_BIND);
	case 5: /* execve */
		return mask & AUDIT_PERM_EXEC;
	default:
		return 0;
	}
}

/* Determine if any context name data matches a rule's watch data */
/* Compare a task_struct with an audit_rule.  Return 1 on match, 0
 * otherwise. */
static int audit_filter_rules(struct task_struct *tsk,
			      struct audit_krule *rule,
			      struct audit_context *ctx,
			      struct audit_names *name,
			      enum audit_state *state)
{
	int i, j, need_sid = 1;
	u32 sid;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];
		int result = 0;

		switch (f->type) {
		case AUDIT_PID:
			result = audit_comparator(tsk->pid, f->op, f->val);
			break;
		case AUDIT_PPID:
			if (ctx) {
				if (!ctx->ppid)
					ctx->ppid = sys_getppid();
				result = audit_comparator(ctx->ppid, f->op, f->val);
			}
			break;
		case AUDIT_UID:
			result = audit_comparator(tsk->uid, f->op, f->val);
			break;
		case AUDIT_EUID:
			result = audit_comparator(tsk->euid, f->op, f->val);
			break;
		case AUDIT_SUID:
			result = audit_comparator(tsk->suid, f->op, f->val);
			break;
		case AUDIT_FSUID:
			result = audit_comparator(tsk->fsuid, f->op, f->val);
			break;
		case AUDIT_GID:
			result = audit_comparator(tsk->gid, f->op, f->val);
			break;
		case AUDIT_EGID:
			result = audit_comparator(tsk->egid, f->op, f->val);
			break;
		case AUDIT_SGID:
			result = audit_comparator(tsk->sgid, f->op, f->val);
			break;
		case AUDIT_FSGID:
			result = audit_comparator(tsk->fsgid, f->op, f->val);
			break;
		case AUDIT_PERS:
			result = audit_comparator(tsk->personality, f->op, f->val);
			break;
		case AUDIT_ARCH:
 			if (ctx)
				result = audit_comparator(ctx->arch, f->op, f->val);
			break;

		case AUDIT_EXIT:
			if (ctx && ctx->return_valid)
				result = audit_comparator(ctx->return_code, f->op, f->val);
			break;
		case AUDIT_SUCCESS:
			if (ctx && ctx->return_valid) {
				if (f->val)
					result = audit_comparator(ctx->return_valid, f->op, AUDITSC_SUCCESS);
				else
					result = audit_comparator(ctx->return_valid, f->op, AUDITSC_FAILURE);
			}
			break;
		case AUDIT_DEVMAJOR:
			if (name)
				result = audit_comparator(MAJOR(name->dev),
							  f->op, f->val);
			else if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (audit_comparator(MAJOR(ctx->names[j].dev),	f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_DEVMINOR:
			if (name)
				result = audit_comparator(MINOR(name->dev),
							  f->op, f->val);
			else if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (audit_comparator(MINOR(ctx->names[j].dev), f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_INODE:
			if (name)
				result = (name->ino == f->val);
			else if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (audit_comparator(ctx->names[j].ino, f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_WATCH:
			if (name && rule->watch->ino != (unsigned long)-1)
				result = (name->dev == rule->watch->dev &&
					  name->ino == rule->watch->ino);
			break;
		case AUDIT_LOGINUID:
			result = 0;
			if (ctx)
				result = audit_comparator(ctx->loginuid, f->op, f->val);
			break;
		case AUDIT_SUBJ_USER:
		case AUDIT_SUBJ_ROLE:
		case AUDIT_SUBJ_TYPE:
		case AUDIT_SUBJ_SEN:
		case AUDIT_SUBJ_CLR:
			/* NOTE: this may return negative values indicating
			   a temporary error.  We simply treat this as a
			   match for now to avoid losing information that
			   may be wanted.   An error message will also be
			   logged upon error */
			if (f->se_rule) {
				if (need_sid) {
					selinux_get_task_sid(tsk, &sid);
					need_sid = 0;
				}
				result = selinux_audit_rule_match(sid, f->type,
				                                  f->op,
				                                  f->se_rule,
				                                  ctx);
			}
			break;
		case AUDIT_OBJ_USER:
		case AUDIT_OBJ_ROLE:
		case AUDIT_OBJ_TYPE:
		case AUDIT_OBJ_LEV_LOW:
		case AUDIT_OBJ_LEV_HIGH:
			/* The above note for AUDIT_SUBJ_USER...AUDIT_SUBJ_CLR
			   also applies here */
			if (f->se_rule) {
				/* Find files that match */
				if (name) {
					result = selinux_audit_rule_match(
					           name->osid, f->type, f->op,
					           f->se_rule, ctx);
				} else if (ctx) {
					for (j = 0; j < ctx->name_count; j++) {
						if (selinux_audit_rule_match(
						      ctx->names[j].osid,
						      f->type, f->op,
						      f->se_rule, ctx)) {
							++result;
							break;
						}
					}
				}
				/* Find ipc objects that match */
				if (ctx) {
					struct audit_aux_data *aux;
					for (aux = ctx->aux; aux;
					     aux = aux->next) {
						if (aux->type == AUDIT_IPC) {
							struct audit_aux_data_ipcctl *axi = (void *)aux;
							if (selinux_audit_rule_match(axi->osid, f->type, f->op, f->se_rule, ctx)) {
								++result;
								break;
							}
						}
					}
				}
			}
			break;
		case AUDIT_ARG0:
		case AUDIT_ARG1:
		case AUDIT_ARG2:
		case AUDIT_ARG3:
			if (ctx)
				result = audit_comparator(ctx->argv[f->type-AUDIT_ARG0], f->op, f->val);
			break;
		case AUDIT_FILTERKEY:
			/* ignore this field for filtering */
			result = 1;
			break;
		case AUDIT_PERM:
			result = audit_match_perm(ctx, f->val);
			break;
		}

		if (!result)
			return 0;
	}
	if (rule->filterkey)
		ctx->filterkey = kstrdup(rule->filterkey, GFP_ATOMIC);
	switch (rule->action) {
	case AUDIT_NEVER:    *state = AUDIT_DISABLED;	    break;
	case AUDIT_ALWAYS:   *state = AUDIT_RECORD_CONTEXT; break;
	}
	return 1;
}

/* At process creation time, we can determine if system-call auditing is
 * completely disabled for this task.  Since we only have the task
 * structure at this point, we can only check uid and gid.
 */
static enum audit_state audit_filter_task(struct task_struct *tsk)
{
	struct audit_entry *e;
	enum audit_state   state;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &audit_filter_list[AUDIT_FILTER_TASK], list) {
		if (audit_filter_rules(tsk, &e->rule, NULL, NULL, &state)) {
			rcu_read_unlock();
			return state;
		}
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
}

/* At syscall entry and exit time, this filter is called if the
 * audit_state is not low enough that auditing cannot take place, but is
 * also not high enough that we already know we have to write an audit
 * record (i.e., the state is AUDIT_SETUP_CONTEXT or AUDIT_BUILD_CONTEXT).
 */
static enum audit_state audit_filter_syscall(struct task_struct *tsk,
					     struct audit_context *ctx,
					     struct list_head *list)
{
	struct audit_entry *e;
	enum audit_state state;

	if (audit_pid && tsk->tgid == audit_pid)
		return AUDIT_DISABLED;

	rcu_read_lock();
	if (!list_empty(list)) {
		int word = AUDIT_WORD(ctx->major);
		int bit  = AUDIT_BIT(ctx->major);

		list_for_each_entry_rcu(e, list, list) {
			if ((e->rule.mask[word] & bit) == bit &&
			    audit_filter_rules(tsk, &e->rule, ctx, NULL,
					       &state)) {
				rcu_read_unlock();
				return state;
			}
		}
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
}

/* At syscall exit time, this filter is called if any audit_names[] have been
 * collected during syscall processing.  We only check rules in sublists at hash
 * buckets applicable to the inode numbers in audit_names[].
 * Regarding audit_state, same rules apply as for audit_filter_syscall().
 */
enum audit_state audit_filter_inodes(struct task_struct *tsk,
				     struct audit_context *ctx)
{
	int i;
	struct audit_entry *e;
	enum audit_state state;

	if (audit_pid && tsk->tgid == audit_pid)
		return AUDIT_DISABLED;

	rcu_read_lock();
	for (i = 0; i < ctx->name_count; i++) {
		int word = AUDIT_WORD(ctx->major);
		int bit  = AUDIT_BIT(ctx->major);
		struct audit_names *n = &ctx->names[i];
		int h = audit_hash_ino((u32)n->ino);
		struct list_head *list = &audit_inode_hash[h];

		if (list_empty(list))
			continue;

		list_for_each_entry_rcu(e, list, list) {
			if ((e->rule.mask[word] & bit) == bit &&
			    audit_filter_rules(tsk, &e->rule, ctx, n, &state)) {
				rcu_read_unlock();
				return state;
			}
		}
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
}

void audit_set_auditable(struct audit_context *ctx)
{
	ctx->auditable = 1;
}

static inline struct audit_context *audit_get_context(struct task_struct *tsk,
						      int return_valid,
						      int return_code)
{
	struct audit_context *context = tsk->audit_context;

	if (likely(!context))
		return NULL;
	context->return_valid = return_valid;
	context->return_code  = return_code;

	if (context->in_syscall && !context->dummy && !context->auditable) {
		enum audit_state state;

		state = audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_EXIT]);
		if (state == AUDIT_RECORD_CONTEXT) {
			context->auditable = 1;
			goto get_context;
		}

		state = audit_filter_inodes(tsk, context);
		if (state == AUDIT_RECORD_CONTEXT)
			context->auditable = 1;

	}

get_context:

	tsk->audit_context = NULL;
	return context;
}

static inline void audit_free_names(struct audit_context *context)
{
	int i;

#if AUDIT_DEBUG == 2
	if (context->auditable
	    ||context->put_count + context->ino_count != context->name_count) {
		printk(KERN_ERR "%s:%d(:%d): major=%d in_syscall=%d"
		       " name_count=%d put_count=%d"
		       " ino_count=%d [NOT freeing]\n",
		       __FILE__, __LINE__,
		       context->serial, context->major, context->in_syscall,
		       context->name_count, context->put_count,
		       context->ino_count);
		for (i = 0; i < context->name_count; i++) {
			printk(KERN_ERR "names[%d] = %p = %s\n", i,
			       context->names[i].name,
			       context->names[i].name ?: "(null)");
		}
		dump_stack();
		return;
	}
#endif
#if AUDIT_DEBUG
	context->put_count  = 0;
	context->ino_count  = 0;
#endif

	for (i = 0; i < context->name_count; i++) {
		if (context->names[i].name && context->names[i].name_put)
			__putname(context->names[i].name);
	}
	context->name_count = 0;
	if (context->pwd)
		dput(context->pwd);
	if (context->pwdmnt)
		mntput(context->pwdmnt);
	context->pwd = NULL;
	context->pwdmnt = NULL;
}

static inline void audit_free_aux(struct audit_context *context)
{
	struct audit_aux_data *aux;

	while ((aux = context->aux)) {
		if (aux->type == AUDIT_AVC_PATH) {
			struct audit_aux_data_path *axi = (void *)aux;
			dput(axi->dentry);
			mntput(axi->mnt);
		}

		context->aux = aux->next;
		kfree(aux);
	}
	while ((aux = context->aux_pids)) {
		context->aux_pids = aux->next;
		kfree(aux);
	}
}

static inline void audit_zero_context(struct audit_context *context,
				      enum audit_state state)
{
	uid_t loginuid = context->loginuid;

	memset(context, 0, sizeof(*context));
	context->state      = state;
	context->loginuid   = loginuid;
}

static inline struct audit_context *audit_alloc_context(enum audit_state state)
{
	struct audit_context *context;

	if (!(context = kmalloc(sizeof(*context), GFP_KERNEL)))
		return NULL;
	audit_zero_context(context, state);
	return context;
}

/**
 * audit_alloc - allocate an audit context block for a task
 * @tsk: task
 *
 * Filter on the task information and allocate a per-task audit context
 * if necessary.  Doing so turns on system call auditing for the
 * specified task.  This is called from copy_process, so no lock is
 * needed.
 */
int audit_alloc(struct task_struct *tsk)
{
	struct audit_context *context;
	enum audit_state     state;

	if (likely(!audit_enabled))
		return 0; /* Return if not auditing. */

	state = audit_filter_task(tsk);
	if (likely(state == AUDIT_DISABLED))
		return 0;

	if (!(context = audit_alloc_context(state))) {
		audit_log_lost("out of memory in audit_alloc");
		return -ENOMEM;
	}

				/* Preserve login uid */
	context->loginuid = -1;
	if (current->audit_context)
		context->loginuid = current->audit_context->loginuid;

	tsk->audit_context  = context;
	set_tsk_thread_flag(tsk, TIF_SYSCALL_AUDIT);
	return 0;
}

static inline void audit_free_context(struct audit_context *context)
{
	struct audit_context *previous;
	int		     count = 0;

	do {
		previous = context->previous;
		if (previous || (count &&  count < 10)) {
			++count;
			printk(KERN_ERR "audit(:%d): major=%d name_count=%d:"
			       " freeing multiple contexts (%d)\n",
			       context->serial, context->major,
			       context->name_count, count);
		}
		audit_free_names(context);
		audit_free_aux(context);
		kfree(context->filterkey);
		kfree(context);
		context  = previous;
	} while (context);
	if (count >= 10)
		printk(KERN_ERR "audit: freed %d contexts\n", count);
}

void audit_log_task_context(struct audit_buffer *ab)
{
	char *ctx = NULL;
	unsigned len;
	int error;
	u32 sid;

	selinux_get_task_sid(current, &sid);
	if (!sid)
		return;

	error = selinux_sid_to_string(sid, &ctx, &len);
	if (error) {
		if (error != -EINVAL)
			goto error_path;
		return;
	}

	audit_log_format(ab, " subj=%s", ctx);
	kfree(ctx);
	return;

error_path:
	audit_panic("error in audit_log_task_context");
	return;
}

EXPORT_SYMBOL(audit_log_task_context);

static void audit_log_task_info(struct audit_buffer *ab, struct task_struct *tsk)
{
	char name[sizeof(tsk->comm)];
	struct mm_struct *mm = tsk->mm;
	struct vm_area_struct *vma;

	/* tsk == current */

	get_task_comm(name, tsk);
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, name);

	if (mm) {
		down_read(&mm->mmap_sem);
		vma = mm->mmap;
		while (vma) {
			if ((vma->vm_flags & VM_EXECUTABLE) &&
			    vma->vm_file) {
				audit_log_d_path(ab, "exe=",
						 vma->vm_file->f_path.dentry,
						 vma->vm_file->f_path.mnt);
				break;
			}
			vma = vma->vm_next;
		}
		up_read(&mm->mmap_sem);
	}
	audit_log_task_context(ab);
}

static int audit_log_pid_context(struct audit_context *context, pid_t pid,
				 u32 sid)
{
	struct audit_buffer *ab;
	char *s = NULL;
	u32 len;
	int rc = 0;

	ab = audit_log_start(context, GFP_KERNEL, AUDIT_OBJ_PID);
	if (!ab)
		return 1;

	if (selinux_sid_to_string(sid, &s, &len)) {
		audit_log_format(ab, "opid=%d obj=(none)", pid);
		rc = 1;
	} else
		audit_log_format(ab, "opid=%d  obj=%s", pid, s);
	audit_log_end(ab);
	kfree(s);

	return rc;
}

static void audit_log_execve_info(struct audit_buffer *ab,
		struct audit_aux_data_execve *axi)
{
	int i;
	long len, ret;
	const char __user *p = (const char __user *)axi->mm->arg_start;
	char *buf;

	if (axi->mm != current->mm)
		return; /* execve failed, no additional info */

	for (i = 0; i < axi->argc; i++, p += len) {
		len = strnlen_user(p, MAX_ARG_PAGES*PAGE_SIZE);
		/*
		 * We just created this mm, if we can't find the strings
		 * we just copied into it something is _very_ wrong. Similar
		 * for strings that are too long, we should not have created
		 * any.
		 */
		if (!len || len > MAX_ARG_STRLEN) {
			WARN_ON(1);
			send_sig(SIGKILL, current, 0);
		}

		buf = kmalloc(len, GFP_KERNEL);
		if (!buf) {
			audit_panic("out of memory for argv string\n");
			break;
		}

		ret = copy_from_user(buf, p, len);
		/*
		 * There is no reason for this copy to be short. We just
		 * copied them here, and the mm hasn't been exposed to user-
		 * space yet.
		 */
		if (!ret) {
			WARN_ON(1);
			send_sig(SIGKILL, current, 0);
		}

		audit_log_format(ab, "a%d=", i);
		audit_log_untrustedstring(ab, buf);
		audit_log_format(ab, "\n");

		kfree(buf);
	}
}

static void audit_log_exit(struct audit_context *context, struct task_struct *tsk)
{
	int i, call_panic = 0;
	struct audit_buffer *ab;
	struct audit_aux_data *aux;
	const char *tty;

	/* tsk == current */
	context->pid = tsk->pid;
	if (!context->ppid)
		context->ppid = sys_getppid();
	context->uid = tsk->uid;
	context->gid = tsk->gid;
	context->euid = tsk->euid;
	context->suid = tsk->suid;
	context->fsuid = tsk->fsuid;
	context->egid = tsk->egid;
	context->sgid = tsk->sgid;
	context->fsgid = tsk->fsgid;
	context->personality = tsk->personality;

	ab = audit_log_start(context, GFP_KERNEL, AUDIT_SYSCALL);
	if (!ab)
		return;		/* audit_panic has been called */
	audit_log_format(ab, "arch=%x syscall=%d",
			 context->arch, context->major);
	if (context->personality != PER_LINUX)
		audit_log_format(ab, " per=%lx", context->personality);
	if (context->return_valid)
		audit_log_format(ab, " success=%s exit=%ld", 
				 (context->return_valid==AUDITSC_SUCCESS)?"yes":"no",
				 context->return_code);

	mutex_lock(&tty_mutex);
	read_lock(&tasklist_lock);
	if (tsk->signal && tsk->signal->tty && tsk->signal->tty->name)
		tty = tsk->signal->tty->name;
	else
		tty = "(none)";
	read_unlock(&tasklist_lock);
	audit_log_format(ab,
		  " a0=%lx a1=%lx a2=%lx a3=%lx items=%d"
		  " ppid=%d pid=%d auid=%u uid=%u gid=%u"
		  " euid=%u suid=%u fsuid=%u"
		  " egid=%u sgid=%u fsgid=%u tty=%s",
		  context->argv[0],
		  context->argv[1],
		  context->argv[2],
		  context->argv[3],
		  context->name_count,
		  context->ppid,
		  context->pid,
		  context->loginuid,
		  context->uid,
		  context->gid,
		  context->euid, context->suid, context->fsuid,
		  context->egid, context->sgid, context->fsgid, tty);

	mutex_unlock(&tty_mutex);

	audit_log_task_info(ab, tsk);
	if (context->filterkey) {
		audit_log_format(ab, " key=");
		audit_log_untrustedstring(ab, context->filterkey);
	} else
		audit_log_format(ab, " key=(null)");
	audit_log_end(ab);

	for (aux = context->aux; aux; aux = aux->next) {

		ab = audit_log_start(context, GFP_KERNEL, aux->type);
		if (!ab)
			continue; /* audit_panic has been called */

		switch (aux->type) {
		case AUDIT_MQ_OPEN: {
			struct audit_aux_data_mq_open *axi = (void *)aux;
			audit_log_format(ab,
				"oflag=0x%x mode=%#o mq_flags=0x%lx mq_maxmsg=%ld "
				"mq_msgsize=%ld mq_curmsgs=%ld",
				axi->oflag, axi->mode, axi->attr.mq_flags,
				axi->attr.mq_maxmsg, axi->attr.mq_msgsize,
				axi->attr.mq_curmsgs);
			break; }

		case AUDIT_MQ_SENDRECV: {
			struct audit_aux_data_mq_sendrecv *axi = (void *)aux;
			audit_log_format(ab,
				"mqdes=%d msg_len=%zd msg_prio=%u "
				"abs_timeout_sec=%ld abs_timeout_nsec=%ld",
				axi->mqdes, axi->msg_len, axi->msg_prio,
				axi->abs_timeout.tv_sec, axi->abs_timeout.tv_nsec);
			break; }

		case AUDIT_MQ_NOTIFY: {
			struct audit_aux_data_mq_notify *axi = (void *)aux;
			audit_log_format(ab,
				"mqdes=%d sigev_signo=%d",
				axi->mqdes,
				axi->notification.sigev_signo);
			break; }

		case AUDIT_MQ_GETSETATTR: {
			struct audit_aux_data_mq_getsetattr *axi = (void *)aux;
			audit_log_format(ab,
				"mqdes=%d mq_flags=0x%lx mq_maxmsg=%ld mq_msgsize=%ld "
				"mq_curmsgs=%ld ",
				axi->mqdes,
				axi->mqstat.mq_flags, axi->mqstat.mq_maxmsg,
				axi->mqstat.mq_msgsize, axi->mqstat.mq_curmsgs);
			break; }

		case AUDIT_IPC: {
			struct audit_aux_data_ipcctl *axi = (void *)aux;
			audit_log_format(ab, 
				 "ouid=%u ogid=%u mode=%x",
				 axi->uid, axi->gid, axi->mode);
			if (axi->osid != 0) {
				char *ctx = NULL;
				u32 len;
				if (selinux_sid_to_string(
						axi->osid, &ctx, &len)) {
					audit_log_format(ab, " osid=%u",
							axi->osid);
					call_panic = 1;
				} else
					audit_log_format(ab, " obj=%s", ctx);
				kfree(ctx);
			}
			break; }

		case AUDIT_IPC_SET_PERM: {
			struct audit_aux_data_ipcctl *axi = (void *)aux;
			audit_log_format(ab,
				"qbytes=%lx ouid=%u ogid=%u mode=%x",
				axi->qbytes, axi->uid, axi->gid, axi->mode);
			break; }

		case AUDIT_EXECVE: {
			struct audit_aux_data_execve *axi = (void *)aux;
			audit_log_execve_info(ab, axi);
			break; }

		case AUDIT_SOCKETCALL: {
			int i;
			struct audit_aux_data_socketcall *axs = (void *)aux;
			audit_log_format(ab, "nargs=%d", axs->nargs);
			for (i=0; i<axs->nargs; i++)
				audit_log_format(ab, " a%d=%lx", i, axs->args[i]);
			break; }

		case AUDIT_SOCKADDR: {
			struct audit_aux_data_sockaddr *axs = (void *)aux;

			audit_log_format(ab, "saddr=");
			audit_log_hex(ab, axs->a, axs->len);
			break; }

		case AUDIT_AVC_PATH: {
			struct audit_aux_data_path *axi = (void *)aux;
			audit_log_d_path(ab, "path=", axi->dentry, axi->mnt);
			break; }

		case AUDIT_FD_PAIR: {
			struct audit_aux_data_fd_pair *axs = (void *)aux;
			audit_log_format(ab, "fd0=%d fd1=%d", axs->fd[0], axs->fd[1]);
			break; }

		}
		audit_log_end(ab);
	}

	for (aux = context->aux_pids; aux; aux = aux->next) {
		struct audit_aux_data_pids *axs = (void *)aux;
		int i;

		for (i = 0; i < axs->pid_count; i++)
			if (audit_log_pid_context(context, axs->target_pid[i],
						  axs->target_sid[i]))
				call_panic = 1;
	}

	if (context->target_pid &&
	    audit_log_pid_context(context, context->target_pid,
				  context->target_sid))
			call_panic = 1;

	if (context->pwd && context->pwdmnt) {
		ab = audit_log_start(context, GFP_KERNEL, AUDIT_CWD);
		if (ab) {
			audit_log_d_path(ab, "cwd=", context->pwd, context->pwdmnt);
			audit_log_end(ab);
		}
	}
	for (i = 0; i < context->name_count; i++) {
		struct audit_names *n = &context->names[i];

		ab = audit_log_start(context, GFP_KERNEL, AUDIT_PATH);
		if (!ab)
			continue; /* audit_panic has been called */

		audit_log_format(ab, "item=%d", i);

		if (n->name) {
			switch(n->name_len) {
			case AUDIT_NAME_FULL:
				/* log the full path */
				audit_log_format(ab, " name=");
				audit_log_untrustedstring(ab, n->name);
				break;
			case 0:
				/* name was specified as a relative path and the
				 * directory component is the cwd */
				audit_log_d_path(ab, " name=", context->pwd,
						 context->pwdmnt);
				break;
			default:
				/* log the name's directory component */
				audit_log_format(ab, " name=");
				audit_log_n_untrustedstring(ab, n->name_len,
							    n->name);
			}
		} else
			audit_log_format(ab, " name=(null)");

		if (n->ino != (unsigned long)-1) {
			audit_log_format(ab, " inode=%lu"
					 " dev=%02x:%02x mode=%#o"
					 " ouid=%u ogid=%u rdev=%02x:%02x",
					 n->ino,
					 MAJOR(n->dev),
					 MINOR(n->dev),
					 n->mode,
					 n->uid,
					 n->gid,
					 MAJOR(n->rdev),
					 MINOR(n->rdev));
		}
		if (n->osid != 0) {
			char *ctx = NULL;
			u32 len;
			if (selinux_sid_to_string(
				n->osid, &ctx, &len)) {
				audit_log_format(ab, " osid=%u", n->osid);
				call_panic = 2;
			} else
				audit_log_format(ab, " obj=%s", ctx);
			kfree(ctx);
		}

		audit_log_end(ab);
	}
	if (call_panic)
		audit_panic("error converting sid to string");
}

/**
 * audit_free - free a per-task audit context
 * @tsk: task whose audit context block to free
 *
 * Called from copy_process and do_exit
 */
void audit_free(struct task_struct *tsk)
{
	struct audit_context *context;

	context = audit_get_context(tsk, 0, 0);
	if (likely(!context))
		return;

	/* Check for system calls that do not go through the exit
	 * function (e.g., exit_group), then free context block. 
	 * We use GFP_ATOMIC here because we might be doing this 
	 * in the context of the idle thread */
	/* that can happen only if we are called from do_exit() */
	if (context->in_syscall && context->auditable)
		audit_log_exit(context, tsk);

	audit_free_context(context);
}

/**
 * audit_syscall_entry - fill in an audit record at syscall entry
 * @tsk: task being audited
 * @arch: architecture type
 * @major: major syscall type (function)
 * @a1: additional syscall register 1
 * @a2: additional syscall register 2
 * @a3: additional syscall register 3
 * @a4: additional syscall register 4
 *
 * Fill in audit context at syscall entry.  This only happens if the
 * audit context was created when the task was created and the state or
 * filters demand the audit context be built.  If the state from the
 * per-task filter or from the per-syscall filter is AUDIT_RECORD_CONTEXT,
 * then the record will be written at syscall exit time (otherwise, it
 * will only be written if another part of the kernel requests that it
 * be written).
 */
void audit_syscall_entry(int arch, int major,
			 unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4)
{
	struct task_struct *tsk = current;
	struct audit_context *context = tsk->audit_context;
	enum audit_state     state;

	BUG_ON(!context);

	/*
	 * This happens only on certain architectures that make system
	 * calls in kernel_thread via the entry.S interface, instead of
	 * with direct calls.  (If you are porting to a new
	 * architecture, hitting this condition can indicate that you
	 * got the _exit/_leave calls backward in entry.S.)
	 *
	 * i386     no
	 * x86_64   no
	 * ppc64    yes (see arch/powerpc/platforms/iseries/misc.S)
	 *
	 * This also happens with vm86 emulation in a non-nested manner
	 * (entries without exits), so this case must be caught.
	 */
	if (context->in_syscall) {
		struct audit_context *newctx;

#if AUDIT_DEBUG
		printk(KERN_ERR
		       "audit(:%d) pid=%d in syscall=%d;"
		       " entering syscall=%d\n",
		       context->serial, tsk->pid, context->major, major);
#endif
		newctx = audit_alloc_context(context->state);
		if (newctx) {
			newctx->previous   = context;
			context		   = newctx;
			tsk->audit_context = newctx;
		} else	{
			/* If we can't alloc a new context, the best we
			 * can do is to leak memory (any pending putname
			 * will be lost).  The only other alternative is
			 * to abandon auditing. */
			audit_zero_context(context, context->state);
		}
	}
	BUG_ON(context->in_syscall || context->name_count);

	if (!audit_enabled)
		return;

	context->arch	    = arch;
	context->major      = major;
	context->argv[0]    = a1;
	context->argv[1]    = a2;
	context->argv[2]    = a3;
	context->argv[3]    = a4;

	state = context->state;
	context->dummy = !audit_n_rules;
	if (!context->dummy && (state == AUDIT_SETUP_CONTEXT || state == AUDIT_BUILD_CONTEXT))
		state = audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_ENTRY]);
	if (likely(state == AUDIT_DISABLED))
		return;

	context->serial     = 0;
	context->ctime      = CURRENT_TIME;
	context->in_syscall = 1;
	context->auditable  = !!(state == AUDIT_RECORD_CONTEXT);
	context->ppid       = 0;
}

/**
 * audit_syscall_exit - deallocate audit context after a system call
 * @tsk: task being audited
 * @valid: success/failure flag
 * @return_code: syscall return value
 *
 * Tear down after system call.  If the audit context has been marked as
 * auditable (either because of the AUDIT_RECORD_CONTEXT state from
 * filtering, or because some other part of the kernel write an audit
 * message), then write out the syscall information.  In call cases,
 * free the names stored from getname().
 */
void audit_syscall_exit(int valid, long return_code)
{
	struct task_struct *tsk = current;
	struct audit_context *context;

	context = audit_get_context(tsk, valid, return_code);

	if (likely(!context))
		return;

	if (context->in_syscall && context->auditable)
		audit_log_exit(context, tsk);

	context->in_syscall = 0;
	context->auditable  = 0;

	if (context->previous) {
		struct audit_context *new_context = context->previous;
		context->previous  = NULL;
		audit_free_context(context);
		tsk->audit_context = new_context;
	} else {
		audit_free_names(context);
		audit_free_aux(context);
		context->aux = NULL;
		context->aux_pids = NULL;
		context->target_pid = 0;
		context->target_sid = 0;
		kfree(context->filterkey);
		context->filterkey = NULL;
		tsk->audit_context = context;
	}
}

/**
 * audit_getname - add a name to the list
 * @name: name to add
 *
 * Add a name to the list of audit names for this context.
 * Called from fs/namei.c:getname().
 */
void __audit_getname(const char *name)
{
	struct audit_context *context = current->audit_context;

	if (IS_ERR(name) || !name)
		return;

	if (!context->in_syscall) {
#if AUDIT_DEBUG == 2
		printk(KERN_ERR "%s:%d(:%d): ignoring getname(%p)\n",
		       __FILE__, __LINE__, context->serial, name);
		dump_stack();
#endif
		return;
	}
	BUG_ON(context->name_count >= AUDIT_NAMES);
	context->names[context->name_count].name = name;
	context->names[context->name_count].name_len = AUDIT_NAME_FULL;
	context->names[context->name_count].name_put = 1;
	context->names[context->name_count].ino  = (unsigned long)-1;
	context->names[context->name_count].osid = 0;
	++context->name_count;
	if (!context->pwd) {
		read_lock(&current->fs->lock);
		context->pwd = dget(current->fs->pwd);
		context->pwdmnt = mntget(current->fs->pwdmnt);
		read_unlock(&current->fs->lock);
	}
		
}

/* audit_putname - intercept a putname request
 * @name: name to intercept and delay for putname
 *
 * If we have stored the name from getname in the audit context,
 * then we delay the putname until syscall exit.
 * Called from include/linux/fs.h:putname().
 */
void audit_putname(const char *name)
{
	struct audit_context *context = current->audit_context;

	BUG_ON(!context);
	if (!context->in_syscall) {
#if AUDIT_DEBUG == 2
		printk(KERN_ERR "%s:%d(:%d): __putname(%p)\n",
		       __FILE__, __LINE__, context->serial, name);
		if (context->name_count) {
			int i;
			for (i = 0; i < context->name_count; i++)
				printk(KERN_ERR "name[%d] = %p = %s\n", i,
				       context->names[i].name,
				       context->names[i].name ?: "(null)");
		}
#endif
		__putname(name);
	}
#if AUDIT_DEBUG
	else {
		++context->put_count;
		if (context->put_count > context->name_count) {
			printk(KERN_ERR "%s:%d(:%d): major=%d"
			       " in_syscall=%d putname(%p) name_count=%d"
			       " put_count=%d\n",
			       __FILE__, __LINE__,
			       context->serial, context->major,
			       context->in_syscall, name, context->name_count,
			       context->put_count);
			dump_stack();
		}
	}
#endif
}

static int audit_inc_name_count(struct audit_context *context,
				const struct inode *inode)
{
	if (context->name_count >= AUDIT_NAMES) {
		if (inode)
			printk(KERN_DEBUG "name_count maxed, losing inode data: "
			       "dev=%02x:%02x, inode=%lu",
			       MAJOR(inode->i_sb->s_dev),
			       MINOR(inode->i_sb->s_dev),
			       inode->i_ino);

		else
			printk(KERN_DEBUG "name_count maxed, losing inode data");
		return 1;
	}
	context->name_count++;
#if AUDIT_DEBUG
	context->ino_count++;
#endif
	return 0;
}

/* Copy inode data into an audit_names. */
static void audit_copy_inode(struct audit_names *name, const struct inode *inode)
{
	name->ino   = inode->i_ino;
	name->dev   = inode->i_sb->s_dev;
	name->mode  = inode->i_mode;
	name->uid   = inode->i_uid;
	name->gid   = inode->i_gid;
	name->rdev  = inode->i_rdev;
	selinux_get_inode_sid(inode, &name->osid);
}

/**
 * audit_inode - store the inode and device from a lookup
 * @name: name being audited
 * @inode: inode being audited
 *
 * Called from fs/namei.c:path_lookup().
 */
void __audit_inode(const char *name, const struct inode *inode)
{
	int idx;
	struct audit_context *context = current->audit_context;

	if (!context->in_syscall)
		return;
	if (context->name_count
	    && context->names[context->name_count-1].name
	    && context->names[context->name_count-1].name == name)
		idx = context->name_count - 1;
	else if (context->name_count > 1
		 && context->names[context->name_count-2].name
		 && context->names[context->name_count-2].name == name)
		idx = context->name_count - 2;
	else {
		/* FIXME: how much do we care about inodes that have no
		 * associated name? */
		if (audit_inc_name_count(context, inode))
			return;
		idx = context->name_count - 1;
		context->names[idx].name = NULL;
	}
	audit_copy_inode(&context->names[idx], inode);
}

/**
 * audit_inode_child - collect inode info for created/removed objects
 * @dname: inode's dentry name
 * @inode: inode being audited
 * @parent: inode of dentry parent
 *
 * For syscalls that create or remove filesystem objects, audit_inode
 * can only collect information for the filesystem object's parent.
 * This call updates the audit context with the child's information.
 * Syscalls that create a new filesystem object must be hooked after
 * the object is created.  Syscalls that remove a filesystem object
 * must be hooked prior, in order to capture the target inode during
 * unsuccessful attempts.
 */
void __audit_inode_child(const char *dname, const struct inode *inode,
			 const struct inode *parent)
{
	int idx;
	struct audit_context *context = current->audit_context;
	const char *found_parent = NULL, *found_child = NULL;
	int dirlen = 0;

	if (!context->in_syscall)
		return;

	/* determine matching parent */
	if (!dname)
		goto add_names;

	/* parent is more likely, look for it first */
	for (idx = 0; idx < context->name_count; idx++) {
		struct audit_names *n = &context->names[idx];

		if (!n->name)
			continue;

		if (n->ino == parent->i_ino &&
		    !audit_compare_dname_path(dname, n->name, &dirlen)) {
			n->name_len = dirlen; /* update parent data in place */
			found_parent = n->name;
			goto add_names;
		}
	}

	/* no matching parent, look for matching child */
	for (idx = 0; idx < context->name_count; idx++) {
		struct audit_names *n = &context->names[idx];

		if (!n->name)
			continue;

		/* strcmp() is the more likely scenario */
		if (!strcmp(dname, n->name) ||
		     !audit_compare_dname_path(dname, n->name, &dirlen)) {
			if (inode)
				audit_copy_inode(n, inode);
			else
				n->ino = (unsigned long)-1;
			found_child = n->name;
			goto add_names;
		}
	}

add_names:
	if (!found_parent) {
		if (audit_inc_name_count(context, parent))
			return;
		idx = context->name_count - 1;
		context->names[idx].name = NULL;
		audit_copy_inode(&context->names[idx], parent);
	}

	if (!found_child) {
		if (audit_inc_name_count(context, inode))
			return;
		idx = context->name_count - 1;

		/* Re-use the name belonging to the slot for a matching parent
		 * directory. All names for this context are relinquished in
		 * audit_free_names() */
		if (found_parent) {
			context->names[idx].name = found_parent;
			context->names[idx].name_len = AUDIT_NAME_FULL;
			/* don't call __putname() */
			context->names[idx].name_put = 0;
		} else {
			context->names[idx].name = NULL;
		}

		if (inode)
			audit_copy_inode(&context->names[idx], inode);
		else
			context->names[idx].ino = (unsigned long)-1;
	}
}

/**
 * auditsc_get_stamp - get local copies of audit_context values
 * @ctx: audit_context for the task
 * @t: timespec to store time recorded in the audit_context
 * @serial: serial value that is recorded in the audit_context
 *
 * Also sets the context as auditable.
 */
void auditsc_get_stamp(struct audit_context *ctx,
		       struct timespec *t, unsigned int *serial)
{
	if (!ctx->serial)
		ctx->serial = audit_serial();
	t->tv_sec  = ctx->ctime.tv_sec;
	t->tv_nsec = ctx->ctime.tv_nsec;
	*serial    = ctx->serial;
	ctx->auditable = 1;
}

/**
 * audit_set_loginuid - set a task's audit_context loginuid
 * @task: task whose audit context is being modified
 * @loginuid: loginuid value
 *
 * Returns 0.
 *
 * Called (set) from fs/proc/base.c::proc_loginuid_write().
 */
int audit_set_loginuid(struct task_struct *task, uid_t loginuid)
{
	struct audit_context *context = task->audit_context;

	if (context) {
		/* Only log if audit is enabled */
		if (context->in_syscall) {
			struct audit_buffer *ab;

			ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_LOGIN);
			if (ab) {
				audit_log_format(ab, "login pid=%d uid=%u "
					"old auid=%u new auid=%u",
					task->pid, task->uid,
					context->loginuid, loginuid);
				audit_log_end(ab);
			}
		}
		context->loginuid = loginuid;
	}
	return 0;
}

/**
 * audit_get_loginuid - get the loginuid for an audit_context
 * @ctx: the audit_context
 *
 * Returns the context's loginuid or -1 if @ctx is NULL.
 */
uid_t audit_get_loginuid(struct audit_context *ctx)
{
	return ctx ? ctx->loginuid : -1;
}

EXPORT_SYMBOL(audit_get_loginuid);

/**
 * __audit_mq_open - record audit data for a POSIX MQ open
 * @oflag: open flag
 * @mode: mode bits
 * @u_attr: queue attributes
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_mq_open(int oflag, mode_t mode, struct mq_attr __user *u_attr)
{
	struct audit_aux_data_mq_open *ax;
	struct audit_context *context = current->audit_context;

	if (!audit_enabled)
		return 0;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	if (u_attr != NULL) {
		if (copy_from_user(&ax->attr, u_attr, sizeof(ax->attr))) {
			kfree(ax);
			return -EFAULT;
		}
	} else
		memset(&ax->attr, 0, sizeof(ax->attr));

	ax->oflag = oflag;
	ax->mode = mode;

	ax->d.type = AUDIT_MQ_OPEN;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * __audit_mq_timedsend - record audit data for a POSIX MQ timed send
 * @mqdes: MQ descriptor
 * @msg_len: Message length
 * @msg_prio: Message priority
 * @u_abs_timeout: Message timeout in absolute time
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_mq_timedsend(mqd_t mqdes, size_t msg_len, unsigned int msg_prio,
			const struct timespec __user *u_abs_timeout)
{
	struct audit_aux_data_mq_sendrecv *ax;
	struct audit_context *context = current->audit_context;

	if (!audit_enabled)
		return 0;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	if (u_abs_timeout != NULL) {
		if (copy_from_user(&ax->abs_timeout, u_abs_timeout, sizeof(ax->abs_timeout))) {
			kfree(ax);
			return -EFAULT;
		}
	} else
		memset(&ax->abs_timeout, 0, sizeof(ax->abs_timeout));

	ax->mqdes = mqdes;
	ax->msg_len = msg_len;
	ax->msg_prio = msg_prio;

	ax->d.type = AUDIT_MQ_SENDRECV;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * __audit_mq_timedreceive - record audit data for a POSIX MQ timed receive
 * @mqdes: MQ descriptor
 * @msg_len: Message length
 * @u_msg_prio: Message priority
 * @u_abs_timeout: Message timeout in absolute time
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_mq_timedreceive(mqd_t mqdes, size_t msg_len,
				unsigned int __user *u_msg_prio,
				const struct timespec __user *u_abs_timeout)
{
	struct audit_aux_data_mq_sendrecv *ax;
	struct audit_context *context = current->audit_context;

	if (!audit_enabled)
		return 0;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	if (u_msg_prio != NULL) {
		if (get_user(ax->msg_prio, u_msg_prio)) {
			kfree(ax);
			return -EFAULT;
		}
	} else
		ax->msg_prio = 0;

	if (u_abs_timeout != NULL) {
		if (copy_from_user(&ax->abs_timeout, u_abs_timeout, sizeof(ax->abs_timeout))) {
			kfree(ax);
			return -EFAULT;
		}
	} else
		memset(&ax->abs_timeout, 0, sizeof(ax->abs_timeout));

	ax->mqdes = mqdes;
	ax->msg_len = msg_len;

	ax->d.type = AUDIT_MQ_SENDRECV;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * __audit_mq_notify - record audit data for a POSIX MQ notify
 * @mqdes: MQ descriptor
 * @u_notification: Notification event
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */

int __audit_mq_notify(mqd_t mqdes, const struct sigevent __user *u_notification)
{
	struct audit_aux_data_mq_notify *ax;
	struct audit_context *context = current->audit_context;

	if (!audit_enabled)
		return 0;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	if (u_notification != NULL) {
		if (copy_from_user(&ax->notification, u_notification, sizeof(ax->notification))) {
			kfree(ax);
			return -EFAULT;
		}
	} else
		memset(&ax->notification, 0, sizeof(ax->notification));

	ax->mqdes = mqdes;

	ax->d.type = AUDIT_MQ_NOTIFY;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * __audit_mq_getsetattr - record audit data for a POSIX MQ get/set attribute
 * @mqdes: MQ descriptor
 * @mqstat: MQ flags
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
{
	struct audit_aux_data_mq_getsetattr *ax;
	struct audit_context *context = current->audit_context;

	if (!audit_enabled)
		return 0;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	ax->mqdes = mqdes;
	ax->mqstat = *mqstat;

	ax->d.type = AUDIT_MQ_GETSETATTR;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * audit_ipc_obj - record audit data for ipc object
 * @ipcp: ipc permissions
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_ipc_obj(struct kern_ipc_perm *ipcp)
{
	struct audit_aux_data_ipcctl *ax;
	struct audit_context *context = current->audit_context;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	ax->uid = ipcp->uid;
	ax->gid = ipcp->gid;
	ax->mode = ipcp->mode;
	selinux_get_ipc_sid(ipcp, &ax->osid);

	ax->d.type = AUDIT_IPC;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * audit_ipc_set_perm - record audit data for new ipc permissions
 * @qbytes: msgq bytes
 * @uid: msgq user id
 * @gid: msgq group id
 * @mode: msgq mode (permissions)
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid, mode_t mode)
{
	struct audit_aux_data_ipcctl *ax;
	struct audit_context *context = current->audit_context;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	ax->qbytes = qbytes;
	ax->uid = uid;
	ax->gid = gid;
	ax->mode = mode;

	ax->d.type = AUDIT_IPC_SET_PERM;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

int audit_argv_kb = 32;

int audit_bprm(struct linux_binprm *bprm)
{
	struct audit_aux_data_execve *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!audit_enabled || !context || context->dummy))
		return 0;

	/*
	 * Even though the stack code doesn't limit the arg+env size any more,
	 * the audit code requires that _all_ arguments be logged in a single
	 * netlink skb. Hence cap it :-(
	 */
	if (bprm->argv_len > (audit_argv_kb << 10))
		return -E2BIG;

	ax = kmalloc(sizeof(*ax), GFP_KERNEL);
	if (!ax)
		return -ENOMEM;

	ax->argc = bprm->argc;
	ax->envc = bprm->envc;
	ax->mm = bprm->mm;
	ax->d.type = AUDIT_EXECVE;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}


/**
 * audit_socketcall - record audit data for sys_socketcall
 * @nargs: number of args
 * @args: args array
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int audit_socketcall(int nargs, unsigned long *args)
{
	struct audit_aux_data_socketcall *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context || context->dummy))
		return 0;

	ax = kmalloc(sizeof(*ax) + nargs * sizeof(unsigned long), GFP_KERNEL);
	if (!ax)
		return -ENOMEM;

	ax->nargs = nargs;
	memcpy(ax->args, args, nargs * sizeof(unsigned long));

	ax->d.type = AUDIT_SOCKETCALL;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * __audit_fd_pair - record audit data for pipe and socketpair
 * @fd1: the first file descriptor
 * @fd2: the second file descriptor
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_fd_pair(int fd1, int fd2)
{
	struct audit_context *context = current->audit_context;
	struct audit_aux_data_fd_pair *ax;

	if (likely(!context)) {
		return 0;
	}

	ax = kmalloc(sizeof(*ax), GFP_KERNEL);
	if (!ax) {
		return -ENOMEM;
	}

	ax->fd[0] = fd1;
	ax->fd[1] = fd2;

	ax->d.type = AUDIT_FD_PAIR;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * audit_sockaddr - record audit data for sys_bind, sys_connect, sys_sendto
 * @len: data length in user space
 * @a: data address in kernel space
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int audit_sockaddr(int len, void *a)
{
	struct audit_aux_data_sockaddr *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context || context->dummy))
		return 0;

	ax = kmalloc(sizeof(*ax) + len, GFP_KERNEL);
	if (!ax)
		return -ENOMEM;

	ax->len = len;
	memcpy(ax->a, a, len);

	ax->d.type = AUDIT_SOCKADDR;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

void __audit_ptrace(struct task_struct *t)
{
	struct audit_context *context = current->audit_context;

	context->target_pid = t->pid;
	selinux_get_task_sid(t, &context->target_sid);
}

/**
 * audit_avc_path - record the granting or denial of permissions
 * @dentry: dentry to record
 * @mnt: mnt to record
 *
 * Returns 0 for success or NULL context or < 0 on error.
 *
 * Called from security/selinux/avc.c::avc_audit()
 */
int audit_avc_path(struct dentry *dentry, struct vfsmount *mnt)
{
	struct audit_aux_data_path *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	ax->dentry = dget(dentry);
	ax->mnt = mntget(mnt);

	ax->d.type = AUDIT_AVC_PATH;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

/**
 * audit_signal_info - record signal info for shutting down audit subsystem
 * @sig: signal value
 * @t: task being signaled
 *
 * If the audit subsystem is being terminated, record the task (pid)
 * and uid that is doing that.
 */
int __audit_signal_info(int sig, struct task_struct *t)
{
	struct audit_aux_data_pids *axp;
	struct task_struct *tsk = current;
	struct audit_context *ctx = tsk->audit_context;
	extern pid_t audit_sig_pid;
	extern uid_t audit_sig_uid;
	extern u32 audit_sig_sid;

	if (audit_pid && t->tgid == audit_pid &&
	    (sig == SIGTERM || sig == SIGHUP || sig == SIGUSR1)) {
		audit_sig_pid = tsk->pid;
		if (ctx)
			audit_sig_uid = ctx->loginuid;
		else
			audit_sig_uid = tsk->uid;
		selinux_get_task_sid(tsk, &audit_sig_sid);
	}

	if (!audit_signals) /* audit_context checked in wrapper */
		return 0;

	/* optimize the common case by putting first signal recipient directly
	 * in audit_context */
	if (!ctx->target_pid) {
		ctx->target_pid = t->tgid;
		selinux_get_task_sid(t, &ctx->target_sid);
		return 0;
	}

	axp = (void *)ctx->aux_pids;
	if (!axp || axp->pid_count == AUDIT_AUX_PIDS) {
		axp = kzalloc(sizeof(*axp), GFP_ATOMIC);
		if (!axp)
			return -ENOMEM;

		axp->d.type = AUDIT_OBJ_PID;
		axp->d.next = ctx->aux_pids;
		ctx->aux_pids = (void *)axp;
	}
	BUG_ON(axp->pid_count > AUDIT_AUX_PIDS);

	axp->target_pid[axp->pid_count] = t->tgid;
	selinux_get_task_sid(t, &axp->target_sid[axp->pid_count]);
	axp->pid_count++;

	return 0;
}

/**
 * audit_core_dumps - record information about processes that end abnormally
 * @signr: signal value
 *
 * If a process ends with a core dump, something fishy is going on and we
 * should record the event for investigation.
 */
void audit_core_dumps(long signr)
{
	struct audit_buffer *ab;
	u32 sid;

	if (!audit_enabled)
		return;

	if (signr == SIGQUIT)	/* don't care for those */
		return;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_ANOM_ABEND);
	audit_log_format(ab, "auid=%u uid=%u gid=%u",
			audit_get_loginuid(current->audit_context),
			current->uid, current->gid);
	selinux_get_task_sid(current, &sid);
	if (sid) {
		char *ctx = NULL;
		u32 len;

		if (selinux_sid_to_string(sid, &ctx, &len))
			audit_log_format(ab, " ssid=%u", sid);
		else
			audit_log_format(ab, " subj=%s", ctx);
		kfree(ctx);
	}
	audit_log_format(ab, " pid=%d comm=", current->pid);
	audit_log_untrustedstring(ab, current->comm);
	audit_log_format(ab, " sig=%ld", signr);
	audit_log_end(ab);
}
