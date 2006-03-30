/* auditsc.c -- System-call auditing support
 * Handles all system-call specific auditing features.
 *
 * Copyright 2003-2004 Red Hat Inc., Durham, North Carolina.
 * Copyright 2005 Hewlett-Packard Development Company, L.P.
 * Copyright (C) 2005 IBM Corporation
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
#include <linux/audit.h>
#include <linux/personality.h>
#include <linux/time.h>
#include <linux/netlink.h>
#include <linux/compiler.h>
#include <asm/unistd.h>
#include <linux/security.h>
#include <linux/list.h>
#include <linux/tty.h>

#include "audit.h"

extern struct list_head audit_filter_list[];

/* No syscall auditing will take place unless audit_enabled != 0. */
extern int audit_enabled;

/* AUDIT_NAMES is the number of slots we reserve in the audit_context
 * for saving names from getname(). */
#define AUDIT_NAMES    20

/* AUDIT_NAMES_RESERVED is the number of slots we reserve in the
 * audit_context from being used for nameless inodes from
 * path_lookup. */
#define AUDIT_NAMES_RESERVED 7

/* When fs/namei.c:getname() is called, we store the pointer in name and
 * we don't let putname() free it (instead we free all of the saved
 * pointers at syscall exit time).
 *
 * Further, in fs/namei.c:path_lookup() we store the inode and device. */
struct audit_names {
	const char	*name;
	unsigned long	ino;
	unsigned long	pino;
	dev_t		dev;
	umode_t		mode;
	uid_t		uid;
	gid_t		gid;
	dev_t		rdev;
	char		*ctx;
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
	char 			*ctx;
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


/* Compare a task_struct with an audit_rule.  Return 1 on match, 0
 * otherwise. */
static int audit_filter_rules(struct task_struct *tsk,
			      struct audit_krule *rule,
			      struct audit_context *ctx,
			      enum audit_state *state)
{
	int i, j;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];
		int result = 0;

		switch (f->type) {
		case AUDIT_PID:
			result = audit_comparator(tsk->pid, f->op, f->val);
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
			if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (audit_comparator(MAJOR(ctx->names[j].dev),	f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_DEVMINOR:
			if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (audit_comparator(MINOR(ctx->names[j].dev), f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_INODE:
			if (ctx) {
				for (j = 0; j < ctx->name_count; j++) {
					if (audit_comparator(ctx->names[j].ino, f->op, f->val) ||
					    audit_comparator(ctx->names[j].pino, f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_LOGINUID:
			result = 0;
			if (ctx)
				result = audit_comparator(ctx->loginuid, f->op, f->val);
			break;
		case AUDIT_ARG0:
		case AUDIT_ARG1:
		case AUDIT_ARG2:
		case AUDIT_ARG3:
			if (ctx)
				result = audit_comparator(ctx->argv[f->type-AUDIT_ARG0], f->op, f->val);
			break;
		}

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
		char *p = context->names[i].ctx;
		context->names[i].ctx = NULL;
		kfree(p);
		if (context->names[i].name)
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
		if ( aux->type == AUDIT_IPC ) {
			struct audit_aux_data_ipcctl *axi = (void *)aux;
			if (axi->ctx)
				kfree(axi->ctx);
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
		kfree(context);
		context  = previous;
	} while (context);
	if (count >= 10)
		printk(KERN_ERR "audit: freed %d contexts\n", count);
}

static void audit_log_task_context(struct audit_buffer *ab, gfp_t gfp_mask)
{
	char *ctx = NULL;
	ssize_t len = 0;

	len = security_getprocattr(current, "current", NULL, 0);
	if (len < 0) {
		if (len != -EINVAL)
			goto error_path;
		return;
	}

	ctx = kmalloc(len, gfp_mask);
	if (!ctx)
		goto error_path;

	len = security_getprocattr(current, "current", ctx, len);
	if (len < 0 )
		goto error_path;

	audit_log_format(ab, " subj=%s", ctx);
	return;

error_path:
	if (ctx)
		kfree(ctx);
	audit_panic("error in audit_log_task_context");
	return;
}

static void audit_log_task_info(struct audit_buffer *ab, struct task_struct *tsk, gfp_t gfp_mask)
{
	char name[sizeof(tsk->comm)];
	struct mm_struct *mm = tsk->mm;
	struct vm_area_struct *vma;

	get_task_comm(name, tsk);
	audit_log_format(ab, " comm=");
	audit_log_untrustedstring(ab, name);

	if (!mm)
		return;

	/*
	 * this is brittle; all callers that pass GFP_ATOMIC will have
	 * NULL tsk->mm and we won't get here.
	 */
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
	audit_log_task_context(ab, gfp_mask);
}

static void audit_log_exit(struct audit_context *context, struct task_struct *tsk, gfp_t gfp_mask)
{
	int i;
	struct audit_buffer *ab;
	struct audit_aux_data *aux;
	const char *tty;

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
	if (tsk->signal && tsk->signal->tty && tsk->signal->tty->name)
		tty = tsk->signal->tty->name;
	else
		tty = "(none)";
	audit_log_format(ab,
		  " a0=%lx a1=%lx a2=%lx a3=%lx items=%d"
		  " pid=%d auid=%u uid=%u gid=%u"
		  " euid=%u suid=%u fsuid=%u"
		  " egid=%u sgid=%u fsgid=%u tty=%s",
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
		  context->egid, context->sgid, context->fsgid, tty);
	audit_log_task_info(ab, gfp_mask);
	audit_log_end(ab);

	for (aux = context->aux; aux; aux = aux->next) {

		ab = audit_log_start(context, gfp_mask, aux->type);
		if (!ab)
			continue; /* audit_panic has been called */

		switch (aux->type) {
		case AUDIT_IPC: {
			struct audit_aux_data_ipcctl *axi = (void *)aux;
			audit_log_format(ab, 
					 " qbytes=%lx iuid=%u igid=%u mode=%x obj=%s",
					 axi->qbytes, axi->uid, axi->gid, axi->mode, axi->ctx);
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
		unsigned long ino  = context->names[i].ino;
		unsigned long pino = context->names[i].pino;

		ab = audit_log_start(context, gfp_mask, AUDIT_PATH);
		if (!ab)
			continue; /* audit_panic has been called */

		audit_log_format(ab, "item=%d", i);

		audit_log_format(ab, " name=");
		if (context->names[i].name)
			audit_log_untrustedstring(ab, context->names[i].name);
		else
			audit_log_format(ab, "(null)");

		if (pino != (unsigned long)-1)
			audit_log_format(ab, " parent=%lu",  pino);
		if (ino != (unsigned long)-1)
			audit_log_format(ab, " inode=%lu",  ino);
		if ((pino != (unsigned long)-1) || (ino != (unsigned long)-1))
			audit_log_format(ab, " dev=%02x:%02x mode=%#o" 
					 " ouid=%u ogid=%u rdev=%02x:%02x", 
					 MAJOR(context->names[i].dev), 
					 MINOR(context->names[i].dev), 
					 context->names[i].mode, 
					 context->names[i].uid, 
					 context->names[i].gid, 
					 MAJOR(context->names[i].rdev), 
					 MINOR(context->names[i].rdev));
		if (context->names[i].ctx) {
			audit_log_format(ab, " obj=%s",
					context->names[i].ctx);
		}

		audit_log_end(ab);
	}
}

/**
 * audit_free - free a per-task audit context
 * @tsk: task whose audit context block to free
 *
 * Called from copy_process and __put_task_struct.
 */
void audit_free(struct task_struct *tsk)
{
	struct audit_context *context;

	/*
	 * No need to lock the task - when we execute audit_free()
	 * then the task has no external references anymore, and
	 * we are tearing it down. (The locking also confuses
	 * DEBUG_LOCKDEP - this freeing may occur in softirq
	 * contexts as well, via RCU.)
	 */
	context = audit_get_context(tsk, 0, 0);
	if (likely(!context))
		return;

	/* Check for system calls that do not go through the exit
	 * function (e.g., exit_group), then free context block. 
	 * We use GFP_ATOMIC here because we might be doing this 
	 * in the context of the idle thread */
	if (context->in_syscall && context->auditable)
		audit_log_exit(context, tsk, GFP_ATOMIC);

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
void audit_syscall_entry(struct task_struct *tsk, int arch, int major,
			 unsigned long a1, unsigned long a2,
			 unsigned long a3, unsigned long a4)
{
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
	if (state == AUDIT_SETUP_CONTEXT || state == AUDIT_BUILD_CONTEXT)
		state = audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_ENTRY]);
	if (likely(state == AUDIT_DISABLED))
		return;

	context->serial     = 0;
	context->ctime      = CURRENT_TIME;
	context->in_syscall = 1;
	context->auditable  = !!(state == AUDIT_RECORD_CONTEXT);
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
		audit_log_exit(context, tsk, GFP_KERNEL);

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

/**
 * audit_getname - add a name to the list
 * @name: name to add
 *
 * Add a name to the list of audit names for this context.
 * Called from fs/namei.c:getname().
 */
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

void audit_inode_context(int idx, const struct inode *inode)
{
	struct audit_context *context = current->audit_context;
	const char *suffix = security_inode_xattr_getsuffix();
	char *ctx = NULL;
	int len = 0;

	if (!suffix)
		goto ret;

	len = security_inode_getsecurity(inode, suffix, NULL, 0, 0);
	if (len == -EOPNOTSUPP)
		goto ret;
	if (len < 0) 
		goto error_path;

	ctx = kmalloc(len, GFP_KERNEL);
	if (!ctx) 
		goto error_path;

	len = security_inode_getsecurity(inode, suffix, ctx, len, 0);
	if (len < 0)
		goto error_path;

	kfree(context->names[idx].ctx);
	context->names[idx].ctx = ctx;
	goto ret;

error_path:
	if (ctx)
		kfree(ctx);
	audit_panic("error in audit_inode_context");
ret:
	return;
}


/**
 * audit_inode - store the inode and device from a lookup
 * @name: name being audited
 * @inode: inode being audited
 * @flags: lookup flags (as used in path_lookup())
 *
 * Called from fs/namei.c:path_lookup().
 */
void __audit_inode(const char *name, const struct inode *inode, unsigned flags)
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
	context->names[idx].dev	  = inode->i_sb->s_dev;
	context->names[idx].mode  = inode->i_mode;
	context->names[idx].uid   = inode->i_uid;
	context->names[idx].gid   = inode->i_gid;
	context->names[idx].rdev  = inode->i_rdev;
	audit_inode_context(idx, inode);
	if ((flags & LOOKUP_PARENT) && (strcmp(name, "/") != 0) && 
	    (strcmp(name, ".") != 0)) {
		context->names[idx].ino   = (unsigned long)-1;
		context->names[idx].pino  = inode->i_ino;
	} else {
		context->names[idx].ino   = inode->i_ino;
		context->names[idx].pino  = (unsigned long)-1;
	}
}

/**
 * audit_inode_child - collect inode info for created/removed objects
 * @dname: inode's dentry name
 * @inode: inode being audited
 * @pino: inode number of dentry parent
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
			 unsigned long pino)
{
	int idx;
	struct audit_context *context = current->audit_context;

	if (!context->in_syscall)
		return;

	/* determine matching parent */
	if (dname)
		for (idx = 0; idx < context->name_count; idx++)
			if (context->names[idx].pino == pino) {
				const char *n;
				const char *name = context->names[idx].name;
				int dlen = strlen(dname);
				int nlen = name ? strlen(name) : 0;

				if (nlen < dlen)
					continue;
				
				/* disregard trailing slashes */
				n = name + nlen - 1;
				while ((*n == '/') && (n > name))
					n--;

				/* find last path component */
				n = n - dlen + 1;
				if (n < name)
					continue;
				else if (n > name) {
					if (*--n != '/')
						continue;
					else
						n++;
				}

				if (strncmp(n, dname, dlen) == 0)
					goto update_context;
			}

	/* catch-all in case match not found */
	idx = context->name_count++;
	context->names[idx].name  = NULL;
	context->names[idx].pino  = pino;
#if AUDIT_DEBUG
	context->ino_count++;
#endif

update_context:
	if (inode) {
		context->names[idx].ino   = inode->i_ino;
		context->names[idx].dev	  = inode->i_sb->s_dev;
		context->names[idx].mode  = inode->i_mode;
		context->names[idx].uid   = inode->i_uid;
		context->names[idx].gid   = inode->i_gid;
		context->names[idx].rdev  = inode->i_rdev;
		audit_inode_context(idx, inode);
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

static char *audit_ipc_context(struct kern_ipc_perm *ipcp)
{
	struct audit_context *context = current->audit_context;
	char *ctx = NULL;
	int len = 0;

	if (likely(!context))
		return NULL;

	len = security_ipc_getsecurity(ipcp, NULL, 0);
	if (len == -EOPNOTSUPP)
		goto ret;
	if (len < 0)
		goto error_path;

	ctx = kmalloc(len, GFP_ATOMIC);
	if (!ctx)
		goto error_path;

	len = security_ipc_getsecurity(ipcp, ctx, len);
	if (len < 0)
		goto error_path;

	return ctx;

error_path:
	kfree(ctx);
	audit_panic("error in audit_ipc_context");
ret:
	return NULL;
}

/**
 * audit_ipc_perms - record audit data for ipc
 * @qbytes: msgq bytes
 * @uid: msgq user id
 * @gid: msgq group id
 * @mode: msgq mode (permissions)
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int audit_ipc_perms(unsigned long qbytes, uid_t uid, gid_t gid, mode_t mode, struct kern_ipc_perm *ipcp)
{
	struct audit_aux_data_ipcctl *ax;
	struct audit_context *context = current->audit_context;

	if (likely(!context))
		return 0;

	ax = kmalloc(sizeof(*ax), GFP_ATOMIC);
	if (!ax)
		return -ENOMEM;

	ax->qbytes = qbytes;
	ax->uid = uid;
	ax->gid = gid;
	ax->mode = mode;
	ax->ctx = audit_ipc_context(ipcp);

	ax->d.type = AUDIT_IPC;
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
