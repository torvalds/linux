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
#include <asm/atomic.h>
#include <asm/types.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <linux/audit.h>
#include <linux/personality.h>
#include <linux/time.h>
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
static LIST_HEAD(audit_tsklist);
static LIST_HEAD(audit_entlist);
static LIST_HEAD(audit_extlist);

struct audit_entry {
	struct list_head  list;
	struct rcu_head   rcu;
	struct audit_rule rule;
};

/* Check to see if two rules are identical.  It is called from
 * audit_del_rule during AUDIT_DEL. */
static int audit_compare_rule(struct audit_rule *a, struct audit_rule *b)
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
static inline int audit_add_rule(struct audit_entry *entry,
				 struct list_head *list)
{
	if (entry->rule.flags & AUDIT_PREPEND) {
		entry->rule.flags &= ~AUDIT_PREPEND;
		list_add_rcu(&entry->list, list);
	} else {
		list_add_tail_rcu(&entry->list, list);
	}
	return 0;
}

static void audit_free_rule(struct rcu_head *head)
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
	return -EFAULT;		/* No matching rule */
}

#ifdef CONFIG_NET
/* Copy rule from user-space to kernel-space.  Called during
 * AUDIT_ADD. */
static int audit_copy_rule(struct audit_rule *d, struct audit_rule *s)
{
	int i;

	if (s->action != AUDIT_NEVER
	    && s->action != AUDIT_POSSIBLE
	    && s->action != AUDIT_ALWAYS)
		return -1;
	if (s->field_count < 0 || s->field_count > AUDIT_MAX_FIELDS)
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

int audit_receive_filter(int type, int pid, int uid, int seq, void *data,
							uid_t loginuid)
{
	u32		   flags;
	struct audit_entry *entry;
	int		   err = 0;

	switch (type) {
	case AUDIT_LIST:
		/* The *_rcu iterators not needed here because we are
		   always called with audit_netlink_sem held. */
		list_for_each_entry(entry, &audit_tsklist, list)
			audit_send_reply(pid, seq, AUDIT_LIST, 0, 1,
					 &entry->rule, sizeof(entry->rule));
		list_for_each_entry(entry, &audit_entlist, list)
			audit_send_reply(pid, seq, AUDIT_LIST, 0, 1,
					 &entry->rule, sizeof(entry->rule));
		list_for_each_entry(entry, &audit_extlist, list)
			audit_send_reply(pid, seq, AUDIT_LIST, 0, 1,
					 &entry->rule, sizeof(entry->rule));
		audit_send_reply(pid, seq, AUDIT_LIST, 1, 1, NULL, 0);
		break;
	case AUDIT_ADD:
		if (!(entry = kmalloc(sizeof(*entry), GFP_KERNEL)))
			return -ENOMEM;
		if (audit_copy_rule(&entry->rule, data)) {
			kfree(entry);
			return -EINVAL;
		}
		flags = entry->rule.flags;
		if (!err && (flags & AUDIT_PER_TASK))
			err = audit_add_rule(entry, &audit_tsklist);
		if (!err && (flags & AUDIT_AT_ENTRY))
			err = audit_add_rule(entry, &audit_entlist);
		if (!err && (flags & AUDIT_AT_EXIT))
			err = audit_add_rule(entry, &audit_extlist);
		audit_log(NULL, "auid %u added an audit rule\n", loginuid);
		break;
	case AUDIT_DEL:
		flags =((struct audit_rule *)data)->flags;
		if (!err && (flags & AUDIT_PER_TASK))
			err = audit_del_rule(data, &audit_tsklist);
		if (!err && (flags & AUDIT_AT_ENTRY))
			err = audit_del_rule(data, &audit_entlist);
		if (!err && (flags & AUDIT_AT_EXIT))
			err = audit_del_rule(data, &audit_extlist);
		audit_log(NULL, "auid %u removed an audit rule\n", loginuid);
		break;
	default:
		return -EINVAL;
	}

	return err;
}
#endif

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
			if (ctx && ctx->return_valid)
				result = (ctx->return_valid == AUDITSC_SUCCESS);
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
	list_for_each_entry_rcu(e, &audit_tsklist, list) {
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
 * also not high enough that we already know we have to write and audit
 * record (i.e., the state is AUDIT_SETUP_CONTEXT or  AUDIT_BUILD_CONTEXT).
 */
static enum audit_state audit_filter_syscall(struct task_struct *tsk,
					     struct audit_context *ctx,
					     struct list_head *list)
{
	struct audit_entry *e;
	enum audit_state   state;
	int		   word = AUDIT_WORD(ctx->major);
	int		   bit  = AUDIT_BIT(ctx->major);

	rcu_read_lock();
	list_for_each_entry_rcu(e, list, list) {
		if ((e->rule.mask[word] & bit) == bit
 		    && audit_filter_rules(tsk, &e->rule, ctx, &state)) {
			rcu_read_unlock();
			return state;
		}
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
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
		state = audit_filter_syscall(tsk, context, &audit_extlist);
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
}

static inline void audit_free_aux(struct audit_context *context)
{
	struct audit_aux_data *aux;

	while ((aux = context->aux)) {
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
	audit_log_format(ab, " comm=%s", name);

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

static void audit_log_exit(struct audit_context *context)
{
	int i;
	struct audit_buffer *ab;

	ab = audit_log_start(context);
	if (!ab)
		return;		/* audit_panic has been called */
	audit_log_format(ab, "syscall=%d", context->major);
	if (context->personality != PER_LINUX)
		audit_log_format(ab, " per=%lx", context->personality);
	audit_log_format(ab, " arch=%x", context->arch);
	if (context->return_valid)
		audit_log_format(ab, " success=%s exit=%ld", 
				 (context->return_valid==AUDITSC_SUCCESS)?"yes":"no",
				 context->return_code);
	audit_log_format(ab,
		  " a0=%lx a1=%lx a2=%lx a3=%lx items=%d"
		  " pid=%d loginuid=%d uid=%d gid=%d"
		  " euid=%d suid=%d fsuid=%d"
		  " egid=%d sgid=%d fsgid=%d",
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
	while (context->aux) {
		struct audit_aux_data *aux;

		ab = audit_log_start(context);
		if (!ab)
			continue; /* audit_panic has been called */

		aux = context->aux;
		context->aux = aux->next;

		audit_log_format(ab, "auxitem=%d", aux->type);
		switch (aux->type) {
		case AUDIT_AUX_IPCPERM: {
			struct audit_aux_data_ipcctl *axi = (void *)aux;
			audit_log_format(ab, 
					 " qbytes=%lx uid=%d gid=%d mode=%x",
					 axi->qbytes, axi->uid, axi->gid, axi->mode);
			}
		}
		audit_log_end(ab);
		kfree(aux);
	}

	for (i = 0; i < context->name_count; i++) {
		ab = audit_log_start(context);
		if (!ab)
			continue; /* audit_panic has been called */
		audit_log_format(ab, "item=%d", i);
		if (context->names[i].name) {
			audit_log_format(ab, " name=");
			audit_log_untrustedstring(ab, context->names[i].name);
		}
		if (context->names[i].ino != (unsigned long)-1)
			audit_log_format(ab, " inode=%lu dev=%02x:%02x mode=%#o"
					     " uid=%d gid=%d rdev=%02x:%02x",
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
	 * function (e.g., exit_group), then free context block. */
	if (context->in_syscall && context->auditable)
		audit_log_exit(context);

	audit_free_context(context);
}

/* Compute a serial number for the audit record.  Audit records are
 * written to user-space as soon as they are generated, so a complete
 * audit record may be written in several pieces.  The timestamp of the
 * record and this serial number are used by the user-space daemon to
 * determine which pieces belong to the same audit record.  The
 * (timestamp,serial) tuple is unique for each syscall and is live from
 * syscall entry to syscall exit.
 *
 * Atomic values are only guaranteed to be 24-bit, so we count down.
 *
 * NOTE: Another possibility is to store the formatted records off the
 * audit context (for those records that have a context), and emit them
 * all at syscall exit.  However, this could delay the reporting of
 * significant errors until syscall exit (or never, if the system
 * halts). */
static inline unsigned int audit_serial(void)
{
	static atomic_t serial = ATOMIC_INIT(0xffffff);
	unsigned int a, b;

	do {
		a = atomic_read(&serial);
		if (atomic_dec_and_test(&serial))
			atomic_set(&serial, 0xffffff);
		b = atomic_read(&serial);
	} while (b != a - 1);

	return 0xffffff - b;
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
	 * ppc64    yes (see arch/ppc64/kernel/misc.S)
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
		state = audit_filter_syscall(tsk, context, &audit_entlist);
	if (likely(state == AUDIT_DISABLED))
		return;

	context->serial     = audit_serial();
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
		return;

	if (context->in_syscall && context->auditable)
		audit_log_exit(context);

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
		audit_zero_context(context, context->state);
		tsk->audit_context = context;
	}
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
void audit_inode(const char *name, const struct inode *inode)
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
	context->names[idx].ino  = inode->i_ino;
	context->names[idx].dev	 = inode->i_sb->s_dev;
	context->names[idx].mode = inode->i_mode;
	context->names[idx].uid  = inode->i_uid;
	context->names[idx].gid  = inode->i_gid;
	context->names[idx].rdev = inode->i_rdev;
}

void audit_get_stamp(struct audit_context *ctx,
		     struct timespec *t, unsigned int *serial)
{
	if (ctx) {
		t->tv_sec  = ctx->ctime.tv_sec;
		t->tv_nsec = ctx->ctime.tv_nsec;
		*serial    = ctx->serial;
		ctx->auditable = 1;
	} else {
		*t      = CURRENT_TIME;
		*serial = 0;
	}
}

extern int audit_set_type(struct audit_buffer *ab, int type);

int audit_set_loginuid(struct task_struct *task, uid_t loginuid)
{
	if (task->audit_context) {
		struct audit_buffer *ab;

		ab = audit_log_start(NULL);
		if (ab) {
			audit_log_format(ab, "login pid=%d uid=%u "
				"old loginuid=%u new loginuid=%u",
				task->pid, task->uid, 
				task->audit_context->loginuid, loginuid);
			audit_set_type(ab, AUDIT_LOGIN);
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

	ax->d.type = AUDIT_AUX_IPCPERM;
	ax->d.next = context->aux;
	context->aux = (void *)ax;
	return 0;
}
