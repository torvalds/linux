/* auditsc.c -- System-call auditing support
 * Handles all system-call specific auditing features.
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
 * Many of the ideas implemented here are from Stephen C. Tweedie,
 * especially the idea of avoiding a copy by using getname.
 *
 * The method for actual interception of syscall entry and exit (not in
 * this file -- see entry.S) is based on a GPL'd patch written by
 * okir@suse.de and Copyright 2003 SuSE Linux AG.
 *
 */

#include <linux/init.h>
#include <asm/types.h>
#include <asm/atomic.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/socket.h>
#include <linux/audit.h>
#include <linux/personality.h>
#include <linux/time.h>
#include <linux/kthread.h>
#include <linux/netlink.h>
#include <linux/compiler.h>
#include <asm/unistd.h>

/* 0 = no checking
   1 = put_count checking
   2 = verbose put_count checking
*/
#define AUDIT_DEBUG 0

/* No syscall auditing will take place unless audit_enabled != 0. */
extern int audit_enabled;

/* AUDIT_NAMES is the number of slots we reserve in the audit_context
 * for saving names from getname(). */
#define AUDIT_NAMES    20

/* AUDIT_NAMES_RESERVED is the number of slots we reserve in the
 * audit_context from being used for nameless inodes from
 * path_lookup. */
#define AUDIT_NAMES_RESERVED 7

/* At task start time, the audit_state is set in the audit_context using
   a per-task filter.  At syscall entry, the audit_state is augmented by
   the syscall filter. */
enum audit_state {
	AUDIT_DISABLED,		/* Do not create per-task audit_context.
				 * No syscall-specific audit records can
				 * be generated. */
	AUDIT_SETUP_CONTEXT,	/* Create the per-task audit_context,
				 * but don't necessarily fill it in at
				 * syscall entry time (i.e., filter
				 * instead). */
	AUDIT_BUILD_CONTEXT,	/* Create the per-task audit_context,
				 * and always fill it in at syscall
				 * entry time.  This makes a full
				 * syscall record available if some
				 * other part of the kernel decides it
				 * should be recorded. */
	AUDIT_RECORD_CONTEXT	/* Create the per-task audit_context,
				 * always fill it in at syscall entry
				 * time, and always write out the audit
				 * record at syscall exit time.  */
};

/* When fs/namei.c:getname() is called, we store the pointer in name and
 * we don't let putname() free it (instead we free all of the saved
 * pointers at syscall exit time).
 *
 * Further, in fs/namei.c:path_lookup() we store the inode and device. */
struct audit_names {
	const char	*name;
	unsigned long	ino;
	dev_t		dev;
	umode_t		mode;
	uid_t		uid;
	gid_t		gid;
	dev_t		rdev;
	unsigned	flags;
};

struct audit_aux_data {
	struct audit_aux_data	*next;
	int			type;
};

#define AUDIT_AUX_IPCPERM	0

struct audit_aux_data_ipcctl {
	struct audit_aux_data	d;
	struct ipc_perm		p;
	unsigned long		qbytes;
	uid_t			uid;
	gid_t			gid;
	mode_t			mode;
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

struct audit_aux_data_path {
	struct audit_aux_data	d;
	struct dentry		*dentry;
	struct vfsmount		*mnt;
};

/* The per-task audit context. */
struct audit_context {
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
	struct dentry *	    pwd;
	struct vfsmount *   pwdmnt;
	struct audit_context *previous; /* For nested syscalls */
	struct audit_aux_data *aux;

				/* Save things to print about task_struct */
	pid_t		    pid;
	uid_t		    uid, euid, suid, fsuid;
	gid_t		    gid, egid, sgid, fsgid;
	unsigned long	    personality;
	int		    arch;

#if AUDIT_DEBUG
	int		    put_count;
	int		    ino_count;
#endif
};

				/* Public API */
/* There are three lists of rules -- one to search at task creation
 * time, one to search at syscall entry time, and another to search at
 * syscall exit time. */
static struct list_head audit_filter_list[AUDIT_NR_FILTERS] = {
	LIST_HEAD_INIT(audit_filter_list[0]),
	LIST_HEAD_INIT(audit_filter_list[1]),
	LIST_HEAD_INIT(audit_filter_list[2]),
	LIST_HEAD_INIT(audit_filter_list[3]),
	LIST_HEAD_INIT(audit_filter_list[4]),
#if AUDIT_NR_FILTERS != 5
#error Fix audit_filter_list initialiser
#endif
};

struct audit_entry {
	struct list_head  list;
	struct rcu_head   rcu;
	struct audit_rule rule;
};

extern int audit_pid;

/* Copy rule from user-space to kernel-space.  Called from 
 * audit_add_rule during AUDIT_ADD. */
static inline int audit_copy_rule(struct audit_rule *d, struct audit_rule *s)
{
	int i;

	if (s->action != AUDIT_NEVER
	    && s->action != AUDIT_POSSIBLE
	    && s->action != AUDIT_ALWAYS)
		return -1;
	if (s->field_count < 0 || s->field_count > AUDIT_MAX_FIELDS)
		return -1;
	if ((s->flags & ~AUDIT_FILTER_PREPEND) >= AUDIT_NR_FILTERS)
		return -1;

	d->flags	= s->flags;
	d->action	= s->action;
	d->field_count	= s->field_count;
	for (i = 0; i < d->field_count; i++) {
		d->fields[i] = s->fields[i];
		d->values[i] = s->values[i];
	}
	for (i = 0; i < AUDIT_BITMASK_SIZE; i++) d->mask[i] = s->mask[i];
	return 0;
}

/* Check to see if two rules are identical.  It is called from
 * audit_add_rule during AUDIT_ADD and 
 * audit_del_rule during AUDIT_DEL. */
static inline int audit_compare_rule(struct audit_rule *a, struct audit_rule *b)
{
	int i;

	if (a->flags != b->flags)
		return 1;

	if (a->action != b->action)
		return 1;

	if (a->field_count != b->field_count)
		return 1;

	for (i = 0; i < a->field_count; i++) {
		if (a->fields[i] != b->fields[i]
		    || a->values[i] != b->values[i])
			return 1;
	}

	for (i = 0; i < AUDIT_BITMASK_SIZE; i++)
		if (a->mask[i] != b->mask[i])
			return 1;

	return 0;
}

/* Note that audit_add_rule and audit_del_rule are called via
 * audit_receive() in audit.c, and are protected by
 * audit_netlink_sem. */
static inline int audit_add_rule(struct audit_rule *rule,
				  struct list_head *list)
{
	struct audit_entry  *entry;

	/* Do not use the _rcu iterator here, since this is the only
	 * addition routine. */
	list_for_each_entry(entry, list, list) {
		if (!audit_compare_rule(rule, &entry->rule)) {
			return -EEXIST;
		}
	}

	if (!(entry = kmalloc(sizeof(*entry), GFP_KERNEL)))
		return -ENOMEM;
	if (audit_copy_rule(&entry->rule, rule)) {
		kfree(entry);
		return -EINVAL;
	}

	if (entry->rule.flags & AUDIT_FILTER_PREPEND) {
		entry->rule.flags &= ~AUDIT_FILTER_PREPEND;
		list_add_rcu(&entry->list, list);
	} else {
		list_add_tail_rcu(&entry->list, list);
	}

	return 0;
}

static inline void audit_free_rule(struct rcu_head *head)
{
	struct audit_entry *e = container_of(head, struct audit_entry, rcu);
	kfree(e);
}

/* Note that audit_add_rule and audit_del_rule are called via
 * audit_receive() in audit.c, and are protected by
 * audit_netlink_sem. */
static inline int audit_del_rule(struct audit_rule *rule,
				 struct list_head *list)
{
	struct audit_entry  *e;

	/* Do not use the _rcu iterator here, since this is the only
	 * deletion routine. */
	list_for_each_entry(e, list, list) {
		if (!audit_compare_rule(rule, &e->rule)) {
			list_del_rcu(&e->list);
			call_rcu(&e->rcu, audit_free_rule);
			return 0;
		}
	}
	return -ENOENT;		/* No matching rule */
}

static int audit_list_rules(void *_dest)
{
	int pid, seq;
	int *dest = _dest;
	struct audit_entry *entry;
	int i;

	pid = dest[0];
	seq = dest[1];
	kfree(dest);

	down(&audit_netlink_sem);

	/* The *_rcu iterators not needed here because we are
	   always called with audit_netlink_sem held. */
	for (i=0; i<AUDIT_NR_FILTERS; i++) {
		list_for_each_entry(entry, &audit_filter_list[i], list)
			audit_send_reply(pid, seq, AUDIT_LIST, 0, 1,
					 &entry->rule, sizeof(entry->rule));
	}
	audit_send_reply(pid, seq, AUDIT_LIST, 1, 1, NULL, 0);
	
	up(&audit_netlink_sem);
	return 0;
}

int audit_receive_filter(int type, int pid, int uid, int seq, void *data,
							uid_t loginuid)
{
	struct task_struct *tsk;
	int *dest;
	int		   err = 0;
	unsigned listnr;

	switch (type) {
	case AUDIT_LIST:
		/* We can't just spew out the rules here because we might fill
		 * the available socket buffer space and deadlock waiting for
		 * auditctl to read from it... which isn't ever going to
		 * happen if we're actually running in the context of auditctl
		 * trying to _send_ the stuff */
		 
		dest = kmalloc(2 * sizeof(int), GFP_KERNEL);
		if (!dest)
			return -ENOMEM;
		dest[0] = pid;
		dest[1] = seq;

		tsk = kthread_run(audit_list_rules, dest, "audit_list_rules");
		if (IS_ERR(tsk)) {
			kfree(dest);
			err = PTR_ERR(tsk);
		}
		break;
	case AUDIT_ADD:
		listnr =((struct audit_rule *)data)->flags & ~AUDIT_FILTER_PREPEND;
		if (listnr >= AUDIT_NR_FILTERS)
			return -EINVAL;

		err = audit_add_rule(data, &audit_filter_list[listnr]);
		if (!err)
			audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				  "auid=%u added an audit rule\n", loginuid);
		break;
	case AUDIT_DEL:
		listnr =((struct audit_rule *)data)->flags & ~AUDIT_FILTER_PREPEND;
		if (listnr >= AUDIT_NR_FILTERS)
			return -EINVAL;

		err = audit_del_rule(data, &audit_filter_list[listnr]);
		if (!err)
			audit_log(NULL, GFP_KERNEL, AUDIT_CONFIG_CHANGE,
				  "auid=%u removed an audit rule\n", loginuid);
		break;
	default:
		return -EINVAL;
	}

	return err;
}

/* Compare a task_struct with an audit_rule.  Return 1 on match, 0
 * otherwise. */
static int audit_filter_rules(struct task_struct *tsk,
			      struct audit_rule *rule,
			      struct audit_context *ctx,
			      enum audit_state *state)
{
	int i, j;

	for (i = 0; i < rule->field_count; i++) {
		u32 field  = rule->fields[i] & ~AUDIT_NEGATE;
		u32 value  = rule->values[i];
		int result = 0;

		switch (field) {
		case AUDIT_PID:
			result = (tsk->pid == value);
			break;
		case AUDIT_UID:
			result = (tsk->uid == value);
			break;
		case AUDIT_EUID:
			result = (tsk->euid == value);
			break;
		case AUDIT_SUID:
			result = (tsk->suid == value);
			break;
		case AUDIT_FSUID:
			result = (tsk->fsuid == value);
			break;
		case AUDIT_GID:
			result = (tsk->gid == value);
			break;
		case AUDIT_EGID:
			result = (tsk->egid == value);
			break;
		case AUDIT_SGID:
			result = (tsk->sgid == value);
			break;
		case AUDIT_FSGID:
			result = (tsk->fsgid == value);
			break;
		case AUDIT_PERS:
			result = (tsk->personality == value);
			break;
		case AUDIT_ARCH:
			if (ctx) 
				result = (ctx->arch == value);
			break;

		case AUDIT_EXIT:
			if (ctx && ctx->return_valid)
				result = (ctx->return_code == value);
			break;
		case AUDIT_SUCCESS:
			if (ctx && ctx->return_valid) {
				if (value)
					result = (ctx->return_valid == AUDITSC_SUCCESS);
				else
					result = (ctx->return_valid == AUDITSC_FAILURE);
			}
			break;
		case AUDIT_DEVMAJOR:
			if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (MAJOR(ctx->names[j].dev)==value) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_DEVMINOR:
			if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (MINOR(ctx->names[j].dev)==value) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_INODE:
			if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (ctx->names[j].ino == value) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_LOGINUID:
			result = 0;
			if (ctx)
				result = (ctx->loginuid == value);
			break;
		case AUDIT_ARG0:
		case AUDIT_ARG1:
		case AUDIT_ARG2:
		case AUDIT_ARG3:
			if (ctx)
				result = (ctx->argv[field-AUDIT_ARG0]==value);
			break;
		}

		if (rule->fields[i] & AUDIT_NEGATE)
			result = !result;
		if (!result)
			return 0;
	}
	switch (rule->action) {
	case AUDIT_NEVER:    *state = AUDIT_DISABLED;	    break;
	case AUDIT_POSSIBLE: *state = AUDIT_BUILD_CONTEXT;  break;
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
		if (audit_filter_rules(tsk, &e->rule, NULL, &state)) {
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
 * record (i.e., the state is AUDIT_SETUP_CONTEXT or  AUDIT_BUILD_CONTEXT).
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
			    if ((e->rule.mask[word] & bit) == bit
				&& audit_filter_rules(tsk, &e->rule, ctx, &state)) {
				    rcu_read_unlock();
				    return state;
			    }
		    }
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
}

static int audit_filter_user_rules(struct netlink_skb_parms *cb,
			      struct audit_rule *rule,
			      enum audit_state *state)
{
	int i;

	for (i = 0; i < rule->field_count; i++) {
		u32 field  = rule->fields[i] & ~AUDIT_NEGATE;
		u32 value  = rule->values[i];
		int result = 0;

		switch (field) {
		case AUDIT_PID:
			result = (cb->creds.pid == value);
			break;
		case AUDIT_UID:
			result = (cb->creds.uid == value);
			break;
		case AUDIT_GID:
			result = (cb->creds.gid == value);
			break;
		case AUDIT_LOGINUID:
			result = (cb->loginuid == value);
			break;
		}

		if (rule->fields[i] & AUDIT_NEGATE)
			result = !result;
		if (!result)
			return 0;
	}
	switch (rule->action) {
	case AUDIT_NEVER:    *state = AUDIT_DISABLED;	    break;
	case AUDIT_POSSIBLE: *state = AUDIT_BUILD_CONTEXT;  break;
	case AUDIT_ALWAYS:   *state = AUDIT_RECORD_CONTEXT; break;
	}
	return 1;
}

int audit_filter_user(struct netlink_skb_parms *cb, int type)
{
	struct audit_entry *e;
	enum audit_state   state;
	int ret = 1;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &audit_filter_list[AUDIT_FILTER_USER], list) {
		if (audit_filter_user_rules(cb, &e->rule, &state)) {
			if (state == AUDIT_DISABLED)
				ret = 0;
			break;
		}
	}
	rcu_read_unlock();

	return ret; /* Audit by default */
}

/* This should be called with task_lock() held. */
static inline struct audit_context *audit_get_context(struct task_struct *tsk,
						      int return_valid,
						      int return_code)
{
	struct audit_context *context = tsk->audit_context;

	if (likely(!context))
		return NULL;
	context->return_valid = return_valid;
	context->return_code  = return_code;

	if (context->in_syscall && !context->auditable) {
		enum audit_state state;
		state = audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_EXIT]);
		if (state == AUDIT_RECORD_CONTEXT)
			context->auditable = 1;
	}

	context->pid = tsk->pid;
	context->uid = tsk->uid;
	context->gid = tsk->gid;
	context->euid = tsk->euid;
	context->suid = tsk->suid;
	context->fsuid = tsk->fsuid;
	context->egid = tsk->egid;
	context->sgid = tsk->sgid;
	context->fsgid = tsk->fsgid;
	context->personality = tsk->personality;
	tsk->audit_context = NULL;
	return context;
}

static inline void audit_free_names(struct audit_context *context)
{
	int i;

#if AUDIT_DEBUG == 2
	if (context->auditable
	    ||context->put_count + context->ino_count != context->name_count) {
		printk(KERN_ERR "audit.c:%d(:%d): major=%d in_syscall=%d"
		       " name_count=%d put_count=%d"
		       " ino_count=%d [NOT freeing]\n",
		       __LINE__,
		       context->serial, context->major, context->in_syscall,
		       context->name_count, context->put_count,
		       context->ino_count);
		for (i = 0; i < context->name_count; i++)
			printk(KERN_ERR "names[%d] = %p = %s\n", i,
			       context->names[i].name,
			       context->names[i].name);
		dump_stack();
		return;
	}
#endif
#if AUDIT_DEBUG
	context->put_count  = 0;
	context->ino_count  = 0;
#endif

	for (i = 0; i < context->name_count; i++)
		if (context->names[i].name)
			__putname(context->names[i].name);
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

/* Filter on the task information and allocate a per-task audit context
 * if necessary.  Doing so turns on system call auditing for the
 * specified task.  This is called from copy_process, so no lock is
 * needed. */
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
		kfree(context);
		context  = previous;
	} while (context);
	if (count >= 10)
		printk(KERN_ERR "audit: freed %d contexts\n", count);
}

static void audit_log_task_info(struct audit_buffer *ab)
{
	char name[sizeof(current->comm)];
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;

	get_task_comm(name, current);
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, name);

	if (!mm)
		return;

	down_read(&mm->mmap_sem);
	vma = mm->mmap;
	while (vma) {
		if ((vma->vm_flags & VM_EXECUTABLE) &&
		    vma->vm_file) {
			audit_log_d_path(ab, "exe=",
					 vma->vm_file->f_dentry,
					 vma->vm_file->f_vfsmnt);
			break;
		}
		vma = vma->vm_next;
	}
	up_read(&mm->mmap_sem);
}

static void audit_log_exit(struct audit_context *context, gfp_t gfp_mask)
{
	int i;
	struct audit_buffer *ab;
	struct audit_aux_data *aux;

	ab = audit_log_start(context, gfp_mask, AUDIT_SYSCALL);
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
	audit_log_format(ab,
		  " a0=%lx a1=%lx a2=%lx a3=%lx items=%d"
		  " pid=%d auid=%u uid=%u gid=%u"
		  " euid=%u suid=%u fsuid=%u"
		  " egid=%u sgid=%u fsgid=%u",
		  context->argv[0],
		  context->argv[1],
		  context->argv[2],
		  context->argv[3],
		  context->name_count,
		  context->pid,
		  context->loginuid,
		  context->uid,
		  context->gid,
		  context->euid, context->suid, context->fsuid,
		  context->egid, context->sgid, context->fsgid);
	audit_log_task_info(ab);
	audit_log_end(ab);

	for (aux = context->aux; aux; aux = aux->next) {

		ab = audit_log_start(context, gfp_mask, aux->type);
		if (!ab)
			continue; /* audit_panic has been called */

		switch (aux->type) {
		case AUDIT_IPC: {
			struct audit_aux_data_ipcctl *axi = (void *)aux;
			audit_log_format(ab, 
					 " qbytes=%lx iuid=%u igid=%u mode=%x",
					 axi->qbytes, axi->uid, axi->gid, axi->mode);
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

		}
		audit_log_end(ab);
	}

	if (context->pwd && context->pwdmnt) {
		ab = audit_log_start(context, gfp_mask, AUDIT_CWD);
		if (ab) {
			audit_log_d_path(ab, "cwd=", context->pwd, context->pwdmnt);
			audit_log_end(ab);
		}
	}
	for (i = 0; i < context->name_count; i++) {
		ab = audit_log_start(context, gfp_mask, AUDIT_PATH);
		if (!ab)
			continue; /* audit_panic has been called */

		audit_log_format(ab, "item=%d", i);
		if (context->names[i].name) {
			audit_log_format(ab, " name=");
			audit_log_untrustedstring(ab, context->names[i].name);
		}
		audit_log_format(ab, " flags=%x\n", context->names[i].flags);
			 
		if (context->names[i].ino != (unsigned long)-1)
			audit_log_format(ab, " inode=%lu dev=%02x:%02x mode=%#o"
					     " ouid=%u ogid=%u rdev=%02x:%02x",
					 context->names[i].ino,
					 MAJOR(context->names[i].dev),
					 MINOR(context->names[i].dev),
					 context->names[i].mode,
					 context->names[i].uid,
					 context->names[i].gid,
					 MAJOR(context->names[i].rdev),
					 MINOR(context->names[i].rdev));
		audit_log_end(ab);
	}
}

/* Free a per-task audit context.  Called from copy_process and
 * __put_task_struct. */
void audit_free(struct task_struct *tsk)
{
	struct audit_context *context;

	task_lock(tsk);
	context = audit_get_context(tsk, 0, 0);
	task_unlock(tsk);

	if (likely(!context))
		return;

	/* Check for system calls that do not go through the exit
	 * function (e.g., exit_group), then free context block. 
	 * We use GFP_ATOMIC here because we might be doing this 
	 * in the context of the idle thread */
	if (context->in_syscall && context->auditable)
		audit_log_exit(context, GFP_ATOMIC);

	audit_free_context(context);
}

/* Fill in audit context at syscall entry.  This only happens if the
 * audit context was created when the task was created and the state or
 * filters demand the audit context be built.  If the state from the
 * per-task filter or from the per-syscall filter is AUDIT_RECORD_CONTEXT,
 * then the record will be written at syscall exit time (otherwise, it
 * will only be written if another part of the kernel requests that it
 * be written). */
void audit_syscall_entry(struct task_struct *tsk, int arch, int major,
			 unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4)
{
	struct audit_context *context = tsk->audit_context;
	enum audit_state     state;

	BUG_ON(!context);

	/* This happens only on certain architectures that make system
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

#if defined(__NR_vm86) && defined(__NR_vm86old)
		/* vm86 mode should only be entered once */
		if (major == __NR_vm86 || major == __NR_vm86old)
			return;
#endif
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
	if (state == AUDIT_SETUP_CONTEXT || state == AUDIT_BUILD_CONTEXT)
		state = audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_ENTRY]);
	if (likely(state == AUDIT_DISABLED))
		return;

	context->serial     = 0;
	context->ctime      = CURRENT_TIME;
	context->in_syscall = 1;
	context->auditable  = !!(state == AUDIT_RECORD_CONTEXT);
}

/* Tear down after system call.  If the audit context has been marked as
 * auditable (either because of the AUDIT_RECORD_CONTEXT state from
 * filtering, or because some other part of the kernel write an audit
 * message), then write out the syscall information.  In call cases,
 * free the names stored from getname(). */
void audit_syscall_exit(struct task_struct *tsk, int valid, long return_code)
{
	struct audit_context *context;

	get_task_struct(tsk);
	task_lock(tsk);
	context = audit_get_context(tsk, valid, return_code);
	task_unlock(tsk);

	/* Not having a context here is ok, since the parent may have
	 * called __put_task_struct. */
	if (likely(!context))
		goto out;

	if (context->in_syscall && context->auditable)
		audit_log_exit(context, GFP_KERNEL);

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
		tsk->audit_context = context;
	}
 out:
	put_task_struct(tsk);
}

/* Add a name to the list.  Called from fs/namei.c:getname(). */
void audit_getname(const char *name)
{
	struct audit_context *context = current->audit_context;

	if (!context || IS_ERR(name) || !name)
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
	context->names[context->name_count].ino  = (unsigned long)-1;
	++context->name_count;
	if (!context->pwd) {
		read_lock(&current->fs->lock);
		context->pwd = dget(current->fs->pwd);
		context->pwdmnt = mntget(current->fs->pwdmnt);
		read_unlock(&current->fs->lock);
	}
		
}

/* Intercept a putname request.  Called from
 * include/linux/fs.h:putname().  If we have stored the name from
 * getname in the audit context, then we delay the putname until syscall
 * exit. */
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
				       context->names[i].name);
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

/* Store the inode and device from a lookup.  Called from
 * fs/namei.c:path_lookup(). */
void audit_inode(const char *name, const struct inode *inode, unsigned flags)
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
		if (context->name_count >= AUDIT_NAMES - AUDIT_NAMES_RESERVED)
			return;
		idx = context->name_count++;
		context->names[idx].name = NULL;
#if AUDIT_DEBUG
		++context->ino_count;
#endif
	}
	context->names[idx].flags = flags;
	context->names[idx].ino   = inode->i_ino;
	context->names[idx].dev	  = inode->i_sb->s_dev;
	context->names[idx].mode  = inode->i_mode;
	context->names[idx].uid   = inode->i_uid;
	context->names[idx].gid   = inode->i_gid;
	context->names[idx].rdev  = inode->i_rdev;
}

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

int audit_set_loginuid(struct task_struct *task, uid_t loginuid)
{
	if (task->audit_context) {
		struct audit_buffer *ab;

		ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_LOGIN);
		if (ab) {
			audit_log_format(ab, "login pid=%d uid=%u "
				"old auid=%u new auid=%u",
				task->pid, task->uid, 
				task->audit_context->loginuid, loginuid);
			audit_log_end(ab);
		}
		task->audit_context->loginuid = loginuid;
	}
	return 0;
}

uid_t audit_get_loginuid(struct audit_context *ctx)
{
	return ctx ? ctx->loginuid : -1;
}

int audit_ipc_perms(unsigned long qbytes, uid_t uid, gid_t gid, mode_t mode)
{
	struct audit_aux_data_ipcctl *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_KERNEL);
	if (!ax)
		return -ENOMEM;

	ax->qbytes = qbytes;
	ax->uid = uid;
	ax->gid = gid;
	ax->mode = mode;

	ax->d.type = AUDIT_IPC;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}

int audit_socketcall(int nargs, unsigned long *args)
{
	struct audit_aux_data_socketcall *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context))
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

int audit_sockaddr(int len, void *a)
{
	struct audit_aux_data_sockaddr *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context))
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

void audit_signal_info(int sig, struct task_struct *t)
{
	extern pid_t audit_sig_pid;
	extern uid_t audit_sig_uid;

	if (unlikely(audit_pid && t->tgid == audit_pid)) {
		if (sig == SIGTERM || sig == SIGHUP) {
			struct audit_context *ctx = current->audit_context;
			audit_sig_pid = current->pid;
			if (ctx)
				audit_sig_uid = ctx->loginuid;
			else
				audit_sig_uid = current->uid;
		}
	}
}

