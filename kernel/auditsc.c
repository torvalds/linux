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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <asm/types.h>
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <linux/slab.h>
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
#include <linux/binfmts.h>
#include <linux/highmem.h>
#include <linux/syscalls.h>
#include <asm/syscall.h>
#include <linux/capability.h>
#include <linux/fs_struct.h>
#include <linux/compat.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <uapi/linux/limits.h>

#include "audit.h"

/* flags stating the success for a syscall */
#define AUDITSC_INVALID 0
#define AUDITSC_SUCCESS 1
#define AUDITSC_FAILURE 2

/* no execve audit message should be longer than this (userspace limits),
 * see the note near the top of audit_log_execve_info() about this value */
#define MAX_EXECVE_AUDIT_LEN 7500

/* max length to print of cmdline/proctitle value during audit */
#define MAX_PROCTITLE_AUDIT_LEN 128

/* number of audit rules */
int audit_n_rules;

/* determines whether we collect data for signals sent */
int audit_signals;

struct audit_aux_data {
	struct audit_aux_data	*next;
	int			type;
};

#define AUDIT_AUX_IPCPERM	0

/* Number of target pids per aux struct. */
#define AUDIT_AUX_PIDS	16

struct audit_aux_data_pids {
	struct audit_aux_data	d;
	pid_t			target_pid[AUDIT_AUX_PIDS];
	kuid_t			target_auid[AUDIT_AUX_PIDS];
	kuid_t			target_uid[AUDIT_AUX_PIDS];
	unsigned int		target_sessionid[AUDIT_AUX_PIDS];
	u32			target_sid[AUDIT_AUX_PIDS];
	char 			target_comm[AUDIT_AUX_PIDS][TASK_COMM_LEN];
	int			pid_count;
};

struct audit_aux_data_bprm_fcaps {
	struct audit_aux_data	d;
	struct audit_cap_data	fcap;
	unsigned int		fcap_ver;
	struct audit_cap_data	old_pcap;
	struct audit_cap_data	new_pcap;
};

struct audit_tree_refs {
	struct audit_tree_refs *next;
	struct audit_chunk *c[31];
};

static int audit_match_perm(struct audit_context *ctx, int mask)
{
	unsigned n;
	if (unlikely(!ctx))
		return 0;
	n = ctx->major;

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

static int audit_match_filetype(struct audit_context *ctx, int val)
{
	struct audit_names *n;
	umode_t mode = (umode_t)val;

	if (unlikely(!ctx))
		return 0;

	list_for_each_entry(n, &ctx->names_list, list) {
		if ((n->ino != AUDIT_INO_UNSET) &&
		    ((n->mode & S_IFMT) == mode))
			return 1;
	}

	return 0;
}

/*
 * We keep a linked list of fixed-sized (31 pointer) arrays of audit_chunk *;
 * ->first_trees points to its beginning, ->trees - to the current end of data.
 * ->tree_count is the number of free entries in array pointed to by ->trees.
 * Original condition is (NULL, NULL, 0); as soon as it grows we never revert to NULL,
 * "empty" becomes (p, p, 31) afterwards.  We don't shrink the list (and seriously,
 * it's going to remain 1-element for almost any setup) until we free context itself.
 * References in it _are_ dropped - at the same time we free/drop aux stuff.
 */

#ifdef CONFIG_AUDIT_TREE
static void audit_set_auditable(struct audit_context *ctx)
{
	if (!ctx->prio) {
		ctx->prio = 1;
		ctx->current_state = AUDIT_RECORD_CONTEXT;
	}
}

static int put_tree_ref(struct audit_context *ctx, struct audit_chunk *chunk)
{
	struct audit_tree_refs *p = ctx->trees;
	int left = ctx->tree_count;
	if (likely(left)) {
		p->c[--left] = chunk;
		ctx->tree_count = left;
		return 1;
	}
	if (!p)
		return 0;
	p = p->next;
	if (p) {
		p->c[30] = chunk;
		ctx->trees = p;
		ctx->tree_count = 30;
		return 1;
	}
	return 0;
}

static int grow_tree_refs(struct audit_context *ctx)
{
	struct audit_tree_refs *p = ctx->trees;
	ctx->trees = kzalloc(sizeof(struct audit_tree_refs), GFP_KERNEL);
	if (!ctx->trees) {
		ctx->trees = p;
		return 0;
	}
	if (p)
		p->next = ctx->trees;
	else
		ctx->first_trees = ctx->trees;
	ctx->tree_count = 31;
	return 1;
}
#endif

static void unroll_tree_refs(struct audit_context *ctx,
		      struct audit_tree_refs *p, int count)
{
#ifdef CONFIG_AUDIT_TREE
	struct audit_tree_refs *q;
	int n;
	if (!p) {
		/* we started with empty chain */
		p = ctx->first_trees;
		count = 31;
		/* if the very first allocation has failed, nothing to do */
		if (!p)
			return;
	}
	n = count;
	for (q = p; q != ctx->trees; q = q->next, n = 31) {
		while (n--) {
			audit_put_chunk(q->c[n]);
			q->c[n] = NULL;
		}
	}
	while (n-- > ctx->tree_count) {
		audit_put_chunk(q->c[n]);
		q->c[n] = NULL;
	}
	ctx->trees = p;
	ctx->tree_count = count;
#endif
}

static void free_tree_refs(struct audit_context *ctx)
{
	struct audit_tree_refs *p, *q;
	for (p = ctx->first_trees; p; p = q) {
		q = p->next;
		kfree(p);
	}
}

static int match_tree_refs(struct audit_context *ctx, struct audit_tree *tree)
{
#ifdef CONFIG_AUDIT_TREE
	struct audit_tree_refs *p;
	int n;
	if (!tree)
		return 0;
	/* full ones */
	for (p = ctx->first_trees; p != ctx->trees; p = p->next) {
		for (n = 0; n < 31; n++)
			if (audit_tree_match(p->c[n], tree))
				return 1;
	}
	/* partial */
	if (p) {
		for (n = ctx->tree_count; n < 31; n++)
			if (audit_tree_match(p->c[n], tree))
				return 1;
	}
#endif
	return 0;
}

static int audit_compare_uid(kuid_t uid,
			     struct audit_names *name,
			     struct audit_field *f,
			     struct audit_context *ctx)
{
	struct audit_names *n;
	int rc;
 
	if (name) {
		rc = audit_uid_comparator(uid, f->op, name->uid);
		if (rc)
			return rc;
	}
 
	if (ctx) {
		list_for_each_entry(n, &ctx->names_list, list) {
			rc = audit_uid_comparator(uid, f->op, n->uid);
			if (rc)
				return rc;
		}
	}
	return 0;
}

static int audit_compare_gid(kgid_t gid,
			     struct audit_names *name,
			     struct audit_field *f,
			     struct audit_context *ctx)
{
	struct audit_names *n;
	int rc;
 
	if (name) {
		rc = audit_gid_comparator(gid, f->op, name->gid);
		if (rc)
			return rc;
	}
 
	if (ctx) {
		list_for_each_entry(n, &ctx->names_list, list) {
			rc = audit_gid_comparator(gid, f->op, n->gid);
			if (rc)
				return rc;
		}
	}
	return 0;
}

static int audit_field_compare(struct task_struct *tsk,
			       const struct cred *cred,
			       struct audit_field *f,
			       struct audit_context *ctx,
			       struct audit_names *name)
{
	switch (f->val) {
	/* process to file object comparisons */
	case AUDIT_COMPARE_UID_TO_OBJ_UID:
		return audit_compare_uid(cred->uid, name, f, ctx);
	case AUDIT_COMPARE_GID_TO_OBJ_GID:
		return audit_compare_gid(cred->gid, name, f, ctx);
	case AUDIT_COMPARE_EUID_TO_OBJ_UID:
		return audit_compare_uid(cred->euid, name, f, ctx);
	case AUDIT_COMPARE_EGID_TO_OBJ_GID:
		return audit_compare_gid(cred->egid, name, f, ctx);
	case AUDIT_COMPARE_AUID_TO_OBJ_UID:
		return audit_compare_uid(tsk->loginuid, name, f, ctx);
	case AUDIT_COMPARE_SUID_TO_OBJ_UID:
		return audit_compare_uid(cred->suid, name, f, ctx);
	case AUDIT_COMPARE_SGID_TO_OBJ_GID:
		return audit_compare_gid(cred->sgid, name, f, ctx);
	case AUDIT_COMPARE_FSUID_TO_OBJ_UID:
		return audit_compare_uid(cred->fsuid, name, f, ctx);
	case AUDIT_COMPARE_FSGID_TO_OBJ_GID:
		return audit_compare_gid(cred->fsgid, name, f, ctx);
	/* uid comparisons */
	case AUDIT_COMPARE_UID_TO_AUID:
		return audit_uid_comparator(cred->uid, f->op, tsk->loginuid);
	case AUDIT_COMPARE_UID_TO_EUID:
		return audit_uid_comparator(cred->uid, f->op, cred->euid);
	case AUDIT_COMPARE_UID_TO_SUID:
		return audit_uid_comparator(cred->uid, f->op, cred->suid);
	case AUDIT_COMPARE_UID_TO_FSUID:
		return audit_uid_comparator(cred->uid, f->op, cred->fsuid);
	/* auid comparisons */
	case AUDIT_COMPARE_AUID_TO_EUID:
		return audit_uid_comparator(tsk->loginuid, f->op, cred->euid);
	case AUDIT_COMPARE_AUID_TO_SUID:
		return audit_uid_comparator(tsk->loginuid, f->op, cred->suid);
	case AUDIT_COMPARE_AUID_TO_FSUID:
		return audit_uid_comparator(tsk->loginuid, f->op, cred->fsuid);
	/* euid comparisons */
	case AUDIT_COMPARE_EUID_TO_SUID:
		return audit_uid_comparator(cred->euid, f->op, cred->suid);
	case AUDIT_COMPARE_EUID_TO_FSUID:
		return audit_uid_comparator(cred->euid, f->op, cred->fsuid);
	/* suid comparisons */
	case AUDIT_COMPARE_SUID_TO_FSUID:
		return audit_uid_comparator(cred->suid, f->op, cred->fsuid);
	/* gid comparisons */
	case AUDIT_COMPARE_GID_TO_EGID:
		return audit_gid_comparator(cred->gid, f->op, cred->egid);
	case AUDIT_COMPARE_GID_TO_SGID:
		return audit_gid_comparator(cred->gid, f->op, cred->sgid);
	case AUDIT_COMPARE_GID_TO_FSGID:
		return audit_gid_comparator(cred->gid, f->op, cred->fsgid);
	/* egid comparisons */
	case AUDIT_COMPARE_EGID_TO_SGID:
		return audit_gid_comparator(cred->egid, f->op, cred->sgid);
	case AUDIT_COMPARE_EGID_TO_FSGID:
		return audit_gid_comparator(cred->egid, f->op, cred->fsgid);
	/* sgid comparison */
	case AUDIT_COMPARE_SGID_TO_FSGID:
		return audit_gid_comparator(cred->sgid, f->op, cred->fsgid);
	default:
		WARN(1, "Missing AUDIT_COMPARE define.  Report as a bug\n");
		return 0;
	}
	return 0;
}

/* Determine if any context name data matches a rule's watch data */
/* Compare a task_struct with an audit_rule.  Return 1 on match, 0
 * otherwise.
 *
 * If task_creation is true, this is an explicit indication that we are
 * filtering a task rule at task creation time.  This and tsk == current are
 * the only situations where tsk->cred may be accessed without an rcu read lock.
 */
static int audit_filter_rules(struct task_struct *tsk,
			      struct audit_krule *rule,
			      struct audit_context *ctx,
			      struct audit_names *name,
			      enum audit_state *state,
			      bool task_creation)
{
	const struct cred *cred;
	int i, need_sid = 1;
	u32 sid;

	cred = rcu_dereference_check(tsk->cred, tsk == current || task_creation);

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];
		struct audit_names *n;
		int result = 0;
		pid_t pid;

		switch (f->type) {
		case AUDIT_PID:
			pid = task_pid_nr(tsk);
			result = audit_comparator(pid, f->op, f->val);
			break;
		case AUDIT_PPID:
			if (ctx) {
				if (!ctx->ppid)
					ctx->ppid = task_ppid_nr(tsk);
				result = audit_comparator(ctx->ppid, f->op, f->val);
			}
			break;
		case AUDIT_EXE:
			result = audit_exe_compare(tsk, rule->exe);
			break;
		case AUDIT_UID:
			result = audit_uid_comparator(cred->uid, f->op, f->uid);
			break;
		case AUDIT_EUID:
			result = audit_uid_comparator(cred->euid, f->op, f->uid);
			break;
		case AUDIT_SUID:
			result = audit_uid_comparator(cred->suid, f->op, f->uid);
			break;
		case AUDIT_FSUID:
			result = audit_uid_comparator(cred->fsuid, f->op, f->uid);
			break;
		case AUDIT_GID:
			result = audit_gid_comparator(cred->gid, f->op, f->gid);
			if (f->op == Audit_equal) {
				if (!result)
					result = in_group_p(f->gid);
			} else if (f->op == Audit_not_equal) {
				if (result)
					result = !in_group_p(f->gid);
			}
			break;
		case AUDIT_EGID:
			result = audit_gid_comparator(cred->egid, f->op, f->gid);
			if (f->op == Audit_equal) {
				if (!result)
					result = in_egroup_p(f->gid);
			} else if (f->op == Audit_not_equal) {
				if (result)
					result = !in_egroup_p(f->gid);
			}
			break;
		case AUDIT_SGID:
			result = audit_gid_comparator(cred->sgid, f->op, f->gid);
			break;
		case AUDIT_FSGID:
			result = audit_gid_comparator(cred->fsgid, f->op, f->gid);
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
			if (name) {
				if (audit_comparator(MAJOR(name->dev), f->op, f->val) ||
				    audit_comparator(MAJOR(name->rdev), f->op, f->val))
					++result;
			} else if (ctx) {
				list_for_each_entry(n, &ctx->names_list, list) {
					if (audit_comparator(MAJOR(n->dev), f->op, f->val) ||
					    audit_comparator(MAJOR(n->rdev), f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_DEVMINOR:
			if (name) {
				if (audit_comparator(MINOR(name->dev), f->op, f->val) ||
				    audit_comparator(MINOR(name->rdev), f->op, f->val))
					++result;
			} else if (ctx) {
				list_for_each_entry(n, &ctx->names_list, list) {
					if (audit_comparator(MINOR(n->dev), f->op, f->val) ||
					    audit_comparator(MINOR(n->rdev), f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_INODE:
			if (name)
				result = audit_comparator(name->ino, f->op, f->val);
			else if (ctx) {
				list_for_each_entry(n, &ctx->names_list, list) {
					if (audit_comparator(n->ino, f->op, f->val)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_OBJ_UID:
			if (name) {
				result = audit_uid_comparator(name->uid, f->op, f->uid);
			} else if (ctx) {
				list_for_each_entry(n, &ctx->names_list, list) {
					if (audit_uid_comparator(n->uid, f->op, f->uid)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_OBJ_GID:
			if (name) {
				result = audit_gid_comparator(name->gid, f->op, f->gid);
			} else if (ctx) {
				list_for_each_entry(n, &ctx->names_list, list) {
					if (audit_gid_comparator(n->gid, f->op, f->gid)) {
						++result;
						break;
					}
				}
			}
			break;
		case AUDIT_WATCH:
			if (name)
				result = audit_watch_compare(rule->watch, name->ino, name->dev);
			break;
		case AUDIT_DIR:
			if (ctx)
				result = match_tree_refs(ctx, rule->tree);
			break;
		case AUDIT_LOGINUID:
			result = audit_uid_comparator(tsk->loginuid, f->op, f->uid);
			break;
		case AUDIT_LOGINUID_SET:
			result = audit_comparator(audit_loginuid_set(tsk), f->op, f->val);
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
			if (f->lsm_rule) {
				if (need_sid) {
					security_task_getsecid(tsk, &sid);
					need_sid = 0;
				}
				result = security_audit_rule_match(sid, f->type,
				                                  f->op,
				                                  f->lsm_rule,
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
			if (f->lsm_rule) {
				/* Find files that match */
				if (name) {
					result = security_audit_rule_match(
					           name->osid, f->type, f->op,
					           f->lsm_rule, ctx);
				} else if (ctx) {
					list_for_each_entry(n, &ctx->names_list, list) {
						if (security_audit_rule_match(n->osid, f->type,
									      f->op, f->lsm_rule,
									      ctx)) {
							++result;
							break;
						}
					}
				}
				/* Find ipc objects that match */
				if (!ctx || ctx->type != AUDIT_IPC)
					break;
				if (security_audit_rule_match(ctx->ipc.osid,
							      f->type, f->op,
							      f->lsm_rule, ctx))
					++result;
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
		case AUDIT_FILETYPE:
			result = audit_match_filetype(ctx, f->val);
			break;
		case AUDIT_FIELD_COMPARE:
			result = audit_field_compare(tsk, cred, f, ctx, name);
			break;
		}
		if (!result)
			return 0;
	}

	if (ctx) {
		if (rule->prio <= ctx->prio)
			return 0;
		if (rule->filterkey) {
			kfree(ctx->filterkey);
			ctx->filterkey = kstrdup(rule->filterkey, GFP_ATOMIC);
		}
		ctx->prio = rule->prio;
	}
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
static enum audit_state audit_filter_task(struct task_struct *tsk, char **key)
{
	struct audit_entry *e;
	enum audit_state   state;

	rcu_read_lock();
	list_for_each_entry_rcu(e, &audit_filter_list[AUDIT_FILTER_TASK], list) {
		if (audit_filter_rules(tsk, &e->rule, NULL, NULL,
				       &state, true)) {
			if (state == AUDIT_RECORD_CONTEXT)
				*key = kstrdup(e->rule.filterkey, GFP_ATOMIC);
			rcu_read_unlock();
			return state;
		}
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
}

static int audit_in_mask(const struct audit_krule *rule, unsigned long val)
{
	int word, bit;

	if (val > 0xffffffff)
		return false;

	word = AUDIT_WORD(val);
	if (word >= AUDIT_BITMASK_SIZE)
		return false;

	bit = AUDIT_BIT(val);

	return rule->mask[word] & bit;
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
		list_for_each_entry_rcu(e, list, list) {
			if (audit_in_mask(&e->rule, ctx->major) &&
			    audit_filter_rules(tsk, &e->rule, ctx, NULL,
					       &state, false)) {
				rcu_read_unlock();
				ctx->current_state = state;
				return state;
			}
		}
	}
	rcu_read_unlock();
	return AUDIT_BUILD_CONTEXT;
}

/*
 * Given an audit_name check the inode hash table to see if they match.
 * Called holding the rcu read lock to protect the use of audit_inode_hash
 */
static int audit_filter_inode_name(struct task_struct *tsk,
				   struct audit_names *n,
				   struct audit_context *ctx) {
	int h = audit_hash_ino((u32)n->ino);
	struct list_head *list = &audit_inode_hash[h];
	struct audit_entry *e;
	enum audit_state state;

	if (list_empty(list))
		return 0;

	list_for_each_entry_rcu(e, list, list) {
		if (audit_in_mask(&e->rule, ctx->major) &&
		    audit_filter_rules(tsk, &e->rule, ctx, n, &state, false)) {
			ctx->current_state = state;
			return 1;
		}
	}

	return 0;
}

/* At syscall exit time, this filter is called if any audit_names have been
 * collected during syscall processing.  We only check rules in sublists at hash
 * buckets applicable to the inode numbers in audit_names.
 * Regarding audit_state, same rules apply as for audit_filter_syscall().
 */
void audit_filter_inodes(struct task_struct *tsk, struct audit_context *ctx)
{
	struct audit_names *n;

	if (audit_pid && tsk->tgid == audit_pid)
		return;

	rcu_read_lock();

	list_for_each_entry(n, &ctx->names_list, list) {
		if (audit_filter_inode_name(tsk, n, ctx))
			break;
	}
	rcu_read_unlock();
}

/* Transfer the audit context pointer to the caller, clearing it in the tsk's struct */
static inline struct audit_context *audit_take_context(struct task_struct *tsk,
						      int return_valid,
						      long return_code)
{
	struct audit_context *context = tsk->audit_context;

	if (!context)
		return NULL;
	context->return_valid = return_valid;

	/*
	 * we need to fix up the return code in the audit logs if the actual
	 * return codes are later going to be fixed up by the arch specific
	 * signal handlers
	 *
	 * This is actually a test for:
	 * (rc == ERESTARTSYS ) || (rc == ERESTARTNOINTR) ||
	 * (rc == ERESTARTNOHAND) || (rc == ERESTART_RESTARTBLOCK)
	 *
	 * but is faster than a bunch of ||
	 */
	if (unlikely(return_code <= -ERESTARTSYS) &&
	    (return_code >= -ERESTART_RESTARTBLOCK) &&
	    (return_code != -ENOIOCTLCMD))
		context->return_code = -EINTR;
	else
		context->return_code  = return_code;

	if (context->in_syscall && !context->dummy) {
		audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_EXIT]);
		audit_filter_inodes(tsk, context);
	}

	tsk->audit_context = NULL;
	return context;
}

static inline void audit_proctitle_free(struct audit_context *context)
{
	kfree(context->proctitle.value);
	context->proctitle.value = NULL;
	context->proctitle.len = 0;
}

static inline void audit_free_names(struct audit_context *context)
{
	struct audit_names *n, *next;

	list_for_each_entry_safe(n, next, &context->names_list, list) {
		list_del(&n->list);
		if (n->name)
			putname(n->name);
		if (n->should_free)
			kfree(n);
	}
	context->name_count = 0;
	path_put(&context->pwd);
	context->pwd.dentry = NULL;
	context->pwd.mnt = NULL;
}

static inline void audit_free_aux(struct audit_context *context)
{
	struct audit_aux_data *aux;

	while ((aux = context->aux)) {
		context->aux = aux->next;
		kfree(aux);
	}
	while ((aux = context->aux_pids)) {
		context->aux_pids = aux->next;
		kfree(aux);
	}
}

static inline struct audit_context *audit_alloc_context(enum audit_state state)
{
	struct audit_context *context;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return NULL;
	context->state = state;
	context->prio = state == AUDIT_RECORD_CONTEXT ? ~0ULL : 0;
	INIT_LIST_HEAD(&context->killed_trees);
	INIT_LIST_HEAD(&context->names_list);
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
	char *key = NULL;

	if (likely(!audit_ever_enabled))
		return 0; /* Return if not auditing. */

	state = audit_filter_task(tsk, &key);
	if (state == AUDIT_DISABLED) {
		clear_tsk_thread_flag(tsk, TIF_SYSCALL_AUDIT);
		return 0;
	}

	if (!(context = audit_alloc_context(state))) {
		kfree(key);
		audit_log_lost("out of memory in audit_alloc");
		return -ENOMEM;
	}
	context->filterkey = key;

	tsk->audit_context  = context;
	set_tsk_thread_flag(tsk, TIF_SYSCALL_AUDIT);
	return 0;
}

static inline void audit_free_context(struct audit_context *context)
{
	audit_free_names(context);
	unroll_tree_refs(context, NULL, 0);
	free_tree_refs(context);
	audit_free_aux(context);
	kfree(context->filterkey);
	kfree(context->sockaddr);
	audit_proctitle_free(context);
	kfree(context);
}

static int audit_log_pid_context(struct audit_context *context, pid_t pid,
				 kuid_t auid, kuid_t uid, unsigned int sessionid,
				 u32 sid, char *comm)
{
	struct audit_buffer *ab;
	char *ctx = NULL;
	u32 len;
	int rc = 0;

	ab = audit_log_start(context, GFP_KERNEL, AUDIT_OBJ_PID);
	if (!ab)
		return rc;

	audit_log_format(ab, "opid=%d oauid=%d ouid=%d oses=%d", pid,
			 from_kuid(&init_user_ns, auid),
			 from_kuid(&init_user_ns, uid), sessionid);
	if (sid) {
		if (security_secid_to_secctx(sid, &ctx, &len)) {
			audit_log_format(ab, " obj=(none)");
			rc = 1;
		} else {
			audit_log_format(ab, " obj=%s", ctx);
			security_release_secctx(ctx, len);
		}
	}
	audit_log_format(ab, " ocomm=");
	audit_log_untrustedstring(ab, comm);
	audit_log_end(ab);

	return rc;
}

static void audit_log_execve_info(struct audit_context *context,
				  struct audit_buffer **ab)
{
	long len_max;
	long len_rem;
	long len_full;
	long len_buf;
	long len_abuf;
	long len_tmp;
	bool require_data;
	bool encode;
	unsigned int iter;
	unsigned int arg;
	char *buf_head;
	char *buf;
	const char __user *p = (const char __user *)current->mm->arg_start;

	/* NOTE: this buffer needs to be large enough to hold all the non-arg
	 *       data we put in the audit record for this argument (see the
	 *       code below) ... at this point in time 96 is plenty */
	char abuf[96];

	/* NOTE: we set MAX_EXECVE_AUDIT_LEN to a rather arbitrary limit, the
	 *       current value of 7500 is not as important as the fact that it
	 *       is less than 8k, a setting of 7500 gives us plenty of wiggle
	 *       room if we go over a little bit in the logging below */
	WARN_ON_ONCE(MAX_EXECVE_AUDIT_LEN > 7500);
	len_max = MAX_EXECVE_AUDIT_LEN;

	/* scratch buffer to hold the userspace args */
	buf_head = kmalloc(MAX_EXECVE_AUDIT_LEN + 1, GFP_KERNEL);
	if (!buf_head) {
		audit_panic("out of memory for argv string");
		return;
	}
	buf = buf_head;

	audit_log_format(*ab, "argc=%d", context->execve.argc);

	len_rem = len_max;
	len_buf = 0;
	len_full = 0;
	require_data = true;
	encode = false;
	iter = 0;
	arg = 0;
	do {
		/* NOTE: we don't ever want to trust this value for anything
		 *       serious, but the audit record format insists we
		 *       provide an argument length for really long arguments,
		 *       e.g. > MAX_EXECVE_AUDIT_LEN, so we have no choice but
		 *       to use strncpy_from_user() to obtain this value for
		 *       recording in the log, although we don't use it
		 *       anywhere here to avoid a double-fetch problem */
		if (len_full == 0)
			len_full = strnlen_user(p, MAX_ARG_STRLEN) - 1;

		/* read more data from userspace */
		if (require_data) {
			/* can we make more room in the buffer? */
			if (buf != buf_head) {
				memmove(buf_head, buf, len_buf);
				buf = buf_head;
			}

			/* fetch as much as we can of the argument */
			len_tmp = strncpy_from_user(&buf_head[len_buf], p,
						    len_max - len_buf);
			if (len_tmp == -EFAULT) {
				/* unable to copy from userspace */
				send_sig(SIGKILL, current, 0);
				goto out;
			} else if (len_tmp == (len_max - len_buf)) {
				/* buffer is not large enough */
				require_data = true;
				/* NOTE: if we are going to span multiple
				 *       buffers force the encoding so we stand
				 *       a chance at a sane len_full value and
				 *       consistent record encoding */
				encode = true;
				len_full = len_full * 2;
				p += len_tmp;
			} else {
				require_data = false;
				if (!encode)
					encode = audit_string_contains_control(
								buf, len_tmp);
				/* try to use a trusted value for len_full */
				if (len_full < len_max)
					len_full = (encode ?
						    len_tmp * 2 : len_tmp);
				p += len_tmp + 1;
			}
			len_buf += len_tmp;
			buf_head[len_buf] = '\0';

			/* length of the buffer in the audit record? */
			len_abuf = (encode ? len_buf * 2 : len_buf + 2);
		}

		/* write as much as we can to the audit log */
		if (len_buf > 0) {
			/* NOTE: some magic numbers here - basically if we
			 *       can't fit a reasonable amount of data into the
			 *       existing audit buffer, flush it and start with
			 *       a new buffer */
			if ((sizeof(abuf) + 8) > len_rem) {
				len_rem = len_max;
				audit_log_end(*ab);
				*ab = audit_log_start(context,
						      GFP_KERNEL, AUDIT_EXECVE);
				if (!*ab)
					goto out;
			}

			/* create the non-arg portion of the arg record */
			len_tmp = 0;
			if (require_data || (iter > 0) ||
			    ((len_abuf + sizeof(abuf)) > len_rem)) {
				if (iter == 0) {
					len_tmp += snprintf(&abuf[len_tmp],
							sizeof(abuf) - len_tmp,
							" a%d_len=%lu",
							arg, len_full);
				}
				len_tmp += snprintf(&abuf[len_tmp],
						    sizeof(abuf) - len_tmp,
						    " a%d[%d]=", arg, iter++);
			} else
				len_tmp += snprintf(&abuf[len_tmp],
						    sizeof(abuf) - len_tmp,
						    " a%d=", arg);
			WARN_ON(len_tmp >= sizeof(abuf));
			abuf[sizeof(abuf) - 1] = '\0';

			/* log the arg in the audit record */
			audit_log_format(*ab, "%s", abuf);
			len_rem -= len_tmp;
			len_tmp = len_buf;
			if (encode) {
				if (len_abuf > len_rem)
					len_tmp = len_rem / 2; /* encoding */
				audit_log_n_hex(*ab, buf, len_tmp);
				len_rem -= len_tmp * 2;
				len_abuf -= len_tmp * 2;
			} else {
				if (len_abuf > len_rem)
					len_tmp = len_rem - 2; /* quotes */
				audit_log_n_string(*ab, buf, len_tmp);
				len_rem -= len_tmp + 2;
				/* don't subtract the "2" because we still need
				 * to add quotes to the remaining string */
				len_abuf -= len_tmp;
			}
			len_buf -= len_tmp;
			buf += len_tmp;
		}

		/* ready to move to the next argument? */
		if ((len_buf == 0) && !require_data) {
			arg++;
			iter = 0;
			len_full = 0;
			require_data = true;
			encode = false;
		}
	} while (arg < context->execve.argc);

	/* NOTE: the caller handles the final audit_log_end() call */

out:
	kfree(buf_head);
}

static void show_special(struct audit_context *context, int *call_panic)
{
	struct audit_buffer *ab;
	int i;

	ab = audit_log_start(context, GFP_KERNEL, context->type);
	if (!ab)
		return;

	switch (context->type) {
	case AUDIT_SOCKETCALL: {
		int nargs = context->socketcall.nargs;
		audit_log_format(ab, "nargs=%d", nargs);
		for (i = 0; i < nargs; i++)
			audit_log_format(ab, " a%d=%lx", i,
				context->socketcall.args[i]);
		break; }
	case AUDIT_IPC: {
		u32 osid = context->ipc.osid;

		audit_log_format(ab, "ouid=%u ogid=%u mode=%#ho",
				 from_kuid(&init_user_ns, context->ipc.uid),
				 from_kgid(&init_user_ns, context->ipc.gid),
				 context->ipc.mode);
		if (osid) {
			char *ctx = NULL;
			u32 len;
			if (security_secid_to_secctx(osid, &ctx, &len)) {
				audit_log_format(ab, " osid=%u", osid);
				*call_panic = 1;
			} else {
				audit_log_format(ab, " obj=%s", ctx);
				security_release_secctx(ctx, len);
			}
		}
		if (context->ipc.has_perm) {
			audit_log_end(ab);
			ab = audit_log_start(context, GFP_KERNEL,
					     AUDIT_IPC_SET_PERM);
			if (unlikely(!ab))
				return;
			audit_log_format(ab,
				"qbytes=%lx ouid=%u ogid=%u mode=%#ho",
				context->ipc.qbytes,
				context->ipc.perm_uid,
				context->ipc.perm_gid,
				context->ipc.perm_mode);
		}
		break; }
	case AUDIT_MQ_OPEN: {
		audit_log_format(ab,
			"oflag=0x%x mode=%#ho mq_flags=0x%lx mq_maxmsg=%ld "
			"mq_msgsize=%ld mq_curmsgs=%ld",
			context->mq_open.oflag, context->mq_open.mode,
			context->mq_open.attr.mq_flags,
			context->mq_open.attr.mq_maxmsg,
			context->mq_open.attr.mq_msgsize,
			context->mq_open.attr.mq_curmsgs);
		break; }
	case AUDIT_MQ_SENDRECV: {
		audit_log_format(ab,
			"mqdes=%d msg_len=%zd msg_prio=%u "
			"abs_timeout_sec=%ld abs_timeout_nsec=%ld",
			context->mq_sendrecv.mqdes,
			context->mq_sendrecv.msg_len,
			context->mq_sendrecv.msg_prio,
			context->mq_sendrecv.abs_timeout.tv_sec,
			context->mq_sendrecv.abs_timeout.tv_nsec);
		break; }
	case AUDIT_MQ_NOTIFY: {
		audit_log_format(ab, "mqdes=%d sigev_signo=%d",
				context->mq_notify.mqdes,
				context->mq_notify.sigev_signo);
		break; }
	case AUDIT_MQ_GETSETATTR: {
		struct mq_attr *attr = &context->mq_getsetattr.mqstat;
		audit_log_format(ab,
			"mqdes=%d mq_flags=0x%lx mq_maxmsg=%ld mq_msgsize=%ld "
			"mq_curmsgs=%ld ",
			context->mq_getsetattr.mqdes,
			attr->mq_flags, attr->mq_maxmsg,
			attr->mq_msgsize, attr->mq_curmsgs);
		break; }
	case AUDIT_CAPSET: {
		audit_log_format(ab, "pid=%d", context->capset.pid);
		audit_log_cap(ab, "cap_pi", &context->capset.cap.inheritable);
		audit_log_cap(ab, "cap_pp", &context->capset.cap.permitted);
		audit_log_cap(ab, "cap_pe", &context->capset.cap.effective);
		break; }
	case AUDIT_MMAP: {
		audit_log_format(ab, "fd=%d flags=0x%x", context->mmap.fd,
				 context->mmap.flags);
		break; }
	case AUDIT_EXECVE: {
		audit_log_execve_info(context, &ab);
		break; }
	}
	audit_log_end(ab);
}

static inline int audit_proctitle_rtrim(char *proctitle, int len)
{
	char *end = proctitle + len - 1;
	while (end > proctitle && !isprint(*end))
		end--;

	/* catch the case where proctitle is only 1 non-print character */
	len = end - proctitle + 1;
	len -= isprint(proctitle[len-1]) == 0;
	return len;
}

static void audit_log_proctitle(struct task_struct *tsk,
			 struct audit_context *context)
{
	int res;
	char *buf;
	char *msg = "(null)";
	int len = strlen(msg);
	struct audit_buffer *ab;

	ab = audit_log_start(context, GFP_KERNEL, AUDIT_PROCTITLE);
	if (!ab)
		return;	/* audit_panic or being filtered */

	audit_log_format(ab, "proctitle=");

	/* Not  cached */
	if (!context->proctitle.value) {
		buf = kmalloc(MAX_PROCTITLE_AUDIT_LEN, GFP_KERNEL);
		if (!buf)
			goto out;
		/* Historically called this from procfs naming */
		res = get_cmdline(tsk, buf, MAX_PROCTITLE_AUDIT_LEN);
		if (res == 0) {
			kfree(buf);
			goto out;
		}
		res = audit_proctitle_rtrim(buf, res);
		if (res == 0) {
			kfree(buf);
			goto out;
		}
		context->proctitle.value = buf;
		context->proctitle.len = res;
	}
	msg = context->proctitle.value;
	len = context->proctitle.len;
out:
	audit_log_n_untrustedstring(ab, msg, len);
	audit_log_end(ab);
}

static void audit_log_exit(struct audit_context *context, struct task_struct *tsk)
{
	int i, call_panic = 0;
	struct audit_buffer *ab;
	struct audit_aux_data *aux;
	struct audit_names *n;

	/* tsk == current */
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

	audit_log_format(ab,
			 " a0=%lx a1=%lx a2=%lx a3=%lx items=%d",
			 context->argv[0],
			 context->argv[1],
			 context->argv[2],
			 context->argv[3],
			 context->name_count);

	audit_log_task_info(ab, tsk);
	audit_log_key(ab, context->filterkey);
	audit_log_end(ab);

	for (aux = context->aux; aux; aux = aux->next) {

		ab = audit_log_start(context, GFP_KERNEL, aux->type);
		if (!ab)
			continue; /* audit_panic has been called */

		switch (aux->type) {

		case AUDIT_BPRM_FCAPS: {
			struct audit_aux_data_bprm_fcaps *axs = (void *)aux;
			audit_log_format(ab, "fver=%x", axs->fcap_ver);
			audit_log_cap(ab, "fp", &axs->fcap.permitted);
			audit_log_cap(ab, "fi", &axs->fcap.inheritable);
			audit_log_format(ab, " fe=%d", axs->fcap.fE);
			audit_log_cap(ab, "old_pp", &axs->old_pcap.permitted);
			audit_log_cap(ab, "old_pi", &axs->old_pcap.inheritable);
			audit_log_cap(ab, "old_pe", &axs->old_pcap.effective);
			audit_log_cap(ab, "new_pp", &axs->new_pcap.permitted);
			audit_log_cap(ab, "new_pi", &axs->new_pcap.inheritable);
			audit_log_cap(ab, "new_pe", &axs->new_pcap.effective);
			break; }

		}
		audit_log_end(ab);
	}

	if (context->type)
		show_special(context, &call_panic);

	if (context->fds[0] >= 0) {
		ab = audit_log_start(context, GFP_KERNEL, AUDIT_FD_PAIR);
		if (ab) {
			audit_log_format(ab, "fd0=%d fd1=%d",
					context->fds[0], context->fds[1]);
			audit_log_end(ab);
		}
	}

	if (context->sockaddr_len) {
		ab = audit_log_start(context, GFP_KERNEL, AUDIT_SOCKADDR);
		if (ab) {
			audit_log_format(ab, "saddr=");
			audit_log_n_hex(ab, (void *)context->sockaddr,
					context->sockaddr_len);
			audit_log_end(ab);
		}
	}

	for (aux = context->aux_pids; aux; aux = aux->next) {
		struct audit_aux_data_pids *axs = (void *)aux;

		for (i = 0; i < axs->pid_count; i++)
			if (audit_log_pid_context(context, axs->target_pid[i],
						  axs->target_auid[i],
						  axs->target_uid[i],
						  axs->target_sessionid[i],
						  axs->target_sid[i],
						  axs->target_comm[i]))
				call_panic = 1;
	}

	if (context->target_pid &&
	    audit_log_pid_context(context, context->target_pid,
				  context->target_auid, context->target_uid,
				  context->target_sessionid,
				  context->target_sid, context->target_comm))
			call_panic = 1;

	if (context->pwd.dentry && context->pwd.mnt) {
		ab = audit_log_start(context, GFP_KERNEL, AUDIT_CWD);
		if (ab) {
			audit_log_d_path(ab, " cwd=", &context->pwd);
			audit_log_end(ab);
		}
	}

	i = 0;
	list_for_each_entry(n, &context->names_list, list) {
		if (n->hidden)
			continue;
		audit_log_name(context, n, NULL, i++, &call_panic);
	}

	audit_log_proctitle(tsk, context);

	/* Send end of event record to help user space know we are finished */
	ab = audit_log_start(context, GFP_KERNEL, AUDIT_EOE);
	if (ab)
		audit_log_end(ab);
	if (call_panic)
		audit_panic("error converting sid to string");
}

/**
 * audit_free - free a per-task audit context
 * @tsk: task whose audit context block to free
 *
 * Called from copy_process and do_exit
 */
void __audit_free(struct task_struct *tsk)
{
	struct audit_context *context;

	context = audit_take_context(tsk, 0, 0);
	if (!context)
		return;

	/* Check for system calls that do not go through the exit
	 * function (e.g., exit_group), then free context block.
	 * We use GFP_ATOMIC here because we might be doing this
	 * in the context of the idle thread */
	/* that can happen only if we are called from do_exit() */
	if (context->in_syscall && context->current_state == AUDIT_RECORD_CONTEXT)
		audit_log_exit(context, tsk);
	if (!list_empty(&context->killed_trees))
		audit_kill_trees(&context->killed_trees);

	audit_free_context(context);
}

/**
 * audit_syscall_entry - fill in an audit record at syscall entry
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
void __audit_syscall_entry(int major, unsigned long a1, unsigned long a2,
			   unsigned long a3, unsigned long a4)
{
	struct task_struct *tsk = current;
	struct audit_context *context = tsk->audit_context;
	enum audit_state     state;

	if (!context)
		return;

	BUG_ON(context->in_syscall || context->name_count);

	if (!audit_enabled)
		return;

	context->arch	    = syscall_get_arch();
	context->major      = major;
	context->argv[0]    = a1;
	context->argv[1]    = a2;
	context->argv[2]    = a3;
	context->argv[3]    = a4;

	state = context->state;
	context->dummy = !audit_n_rules;
	if (!context->dummy && state == AUDIT_BUILD_CONTEXT) {
		context->prio = 0;
		state = audit_filter_syscall(tsk, context, &audit_filter_list[AUDIT_FILTER_ENTRY]);
	}
	if (state == AUDIT_DISABLED)
		return;

	context->serial     = 0;
	context->ctime      = CURRENT_TIME;
	context->in_syscall = 1;
	context->current_state  = state;
	context->ppid       = 0;
}

/**
 * audit_syscall_exit - deallocate audit context after a system call
 * @success: success value of the syscall
 * @return_code: return value of the syscall
 *
 * Tear down after system call.  If the audit context has been marked as
 * auditable (either because of the AUDIT_RECORD_CONTEXT state from
 * filtering, or because some other part of the kernel wrote an audit
 * message), then write out the syscall information.  In call cases,
 * free the names stored from getname().
 */
void __audit_syscall_exit(int success, long return_code)
{
	struct task_struct *tsk = current;
	struct audit_context *context;

	if (success)
		success = AUDITSC_SUCCESS;
	else
		success = AUDITSC_FAILURE;

	context = audit_take_context(tsk, success, return_code);
	if (!context)
		return;

	if (context->in_syscall && context->current_state == AUDIT_RECORD_CONTEXT)
		audit_log_exit(context, tsk);

	context->in_syscall = 0;
	context->prio = context->state == AUDIT_RECORD_CONTEXT ? ~0ULL : 0;

	if (!list_empty(&context->killed_trees))
		audit_kill_trees(&context->killed_trees);

	audit_free_names(context);
	unroll_tree_refs(context, NULL, 0);
	audit_free_aux(context);
	context->aux = NULL;
	context->aux_pids = NULL;
	context->target_pid = 0;
	context->target_sid = 0;
	context->sockaddr_len = 0;
	context->type = 0;
	context->fds[0] = -1;
	if (context->state != AUDIT_RECORD_CONTEXT) {
		kfree(context->filterkey);
		context->filterkey = NULL;
	}
	tsk->audit_context = context;
}

static inline void handle_one(const struct inode *inode)
{
#ifdef CONFIG_AUDIT_TREE
	struct audit_context *context;
	struct audit_tree_refs *p;
	struct audit_chunk *chunk;
	int count;
	if (likely(hlist_empty(&inode->i_fsnotify_marks)))
		return;
	context = current->audit_context;
	p = context->trees;
	count = context->tree_count;
	rcu_read_lock();
	chunk = audit_tree_lookup(inode);
	rcu_read_unlock();
	if (!chunk)
		return;
	if (likely(put_tree_ref(context, chunk)))
		return;
	if (unlikely(!grow_tree_refs(context))) {
		pr_warn("out of memory, audit has lost a tree reference\n");
		audit_set_auditable(context);
		audit_put_chunk(chunk);
		unroll_tree_refs(context, p, count);
		return;
	}
	put_tree_ref(context, chunk);
#endif
}

static void handle_path(const struct dentry *dentry)
{
#ifdef CONFIG_AUDIT_TREE
	struct audit_context *context;
	struct audit_tree_refs *p;
	const struct dentry *d, *parent;
	struct audit_chunk *drop;
	unsigned long seq;
	int count;

	context = current->audit_context;
	p = context->trees;
	count = context->tree_count;
retry:
	drop = NULL;
	d = dentry;
	rcu_read_lock();
	seq = read_seqbegin(&rename_lock);
	for(;;) {
		struct inode *inode = d_backing_inode(d);
		if (inode && unlikely(!hlist_empty(&inode->i_fsnotify_marks))) {
			struct audit_chunk *chunk;
			chunk = audit_tree_lookup(inode);
			if (chunk) {
				if (unlikely(!put_tree_ref(context, chunk))) {
					drop = chunk;
					break;
				}
			}
		}
		parent = d->d_parent;
		if (parent == d)
			break;
		d = parent;
	}
	if (unlikely(read_seqretry(&rename_lock, seq) || drop)) {  /* in this order */
		rcu_read_unlock();
		if (!drop) {
			/* just a race with rename */
			unroll_tree_refs(context, p, count);
			goto retry;
		}
		audit_put_chunk(drop);
		if (grow_tree_refs(context)) {
			/* OK, got more space */
			unroll_tree_refs(context, p, count);
			goto retry;
		}
		/* too bad */
		pr_warn("out of memory, audit has lost a tree reference\n");
		unroll_tree_refs(context, p, count);
		audit_set_auditable(context);
		return;
	}
	rcu_read_unlock();
#endif
}

static struct audit_names *audit_alloc_name(struct audit_context *context,
						unsigned char type)
{
	struct audit_names *aname;

	if (context->name_count < AUDIT_NAMES) {
		aname = &context->preallocated_names[context->name_count];
		memset(aname, 0, sizeof(*aname));
	} else {
		aname = kzalloc(sizeof(*aname), GFP_NOFS);
		if (!aname)
			return NULL;
		aname->should_free = true;
	}

	aname->ino = AUDIT_INO_UNSET;
	aname->type = type;
	list_add_tail(&aname->list, &context->names_list);

	context->name_count++;
	return aname;
}

/**
 * audit_reusename - fill out filename with info from existing entry
 * @uptr: userland ptr to pathname
 *
 * Search the audit_names list for the current audit context. If there is an
 * existing entry with a matching "uptr" then return the filename
 * associated with that audit_name. If not, return NULL.
 */
struct filename *
__audit_reusename(const __user char *uptr)
{
	struct audit_context *context = current->audit_context;
	struct audit_names *n;

	list_for_each_entry(n, &context->names_list, list) {
		if (!n->name)
			continue;
		if (n->name->uptr == uptr) {
			n->name->refcnt++;
			return n->name;
		}
	}
	return NULL;
}

/**
 * audit_getname - add a name to the list
 * @name: name to add
 *
 * Add a name to the list of audit names for this context.
 * Called from fs/namei.c:getname().
 */
void __audit_getname(struct filename *name)
{
	struct audit_context *context = current->audit_context;
	struct audit_names *n;

	if (!context->in_syscall)
		return;

	n = audit_alloc_name(context, AUDIT_TYPE_UNKNOWN);
	if (!n)
		return;

	n->name = name;
	n->name_len = AUDIT_NAME_FULL;
	name->aname = n;
	name->refcnt++;

	if (!context->pwd.dentry)
		get_fs_pwd(current->fs, &context->pwd);
}

/**
 * __audit_inode - store the inode and device from a lookup
 * @name: name being audited
 * @dentry: dentry being audited
 * @flags: attributes for this particular entry
 */
void __audit_inode(struct filename *name, const struct dentry *dentry,
		   unsigned int flags)
{
	struct audit_context *context = current->audit_context;
	const struct inode *inode = d_backing_inode(dentry);
	struct audit_names *n;
	bool parent = flags & AUDIT_INODE_PARENT;

	if (!context->in_syscall)
		return;

	if (!name)
		goto out_alloc;

	/*
	 * If we have a pointer to an audit_names entry already, then we can
	 * just use it directly if the type is correct.
	 */
	n = name->aname;
	if (n) {
		if (parent) {
			if (n->type == AUDIT_TYPE_PARENT ||
			    n->type == AUDIT_TYPE_UNKNOWN)
				goto out;
		} else {
			if (n->type != AUDIT_TYPE_PARENT)
				goto out;
		}
	}

	list_for_each_entry_reverse(n, &context->names_list, list) {
		if (n->ino) {
			/* valid inode number, use that for the comparison */
			if (n->ino != inode->i_ino ||
			    n->dev != inode->i_sb->s_dev)
				continue;
		} else if (n->name) {
			/* inode number has not been set, check the name */
			if (strcmp(n->name->name, name->name))
				continue;
		} else
			/* no inode and no name (?!) ... this is odd ... */
			continue;

		/* match the correct record type */
		if (parent) {
			if (n->type == AUDIT_TYPE_PARENT ||
			    n->type == AUDIT_TYPE_UNKNOWN)
				goto out;
		} else {
			if (n->type != AUDIT_TYPE_PARENT)
				goto out;
		}
	}

out_alloc:
	/* unable to find an entry with both a matching name and type */
	n = audit_alloc_name(context, AUDIT_TYPE_UNKNOWN);
	if (!n)
		return;
	if (name) {
		n->name = name;
		name->refcnt++;
	}

out:
	if (parent) {
		n->name_len = n->name ? parent_len(n->name->name) : AUDIT_NAME_FULL;
		n->type = AUDIT_TYPE_PARENT;
		if (flags & AUDIT_INODE_HIDDEN)
			n->hidden = true;
	} else {
		n->name_len = AUDIT_NAME_FULL;
		n->type = AUDIT_TYPE_NORMAL;
	}
	handle_path(dentry);
	audit_copy_inode(n, dentry, inode);
}

void __audit_file(const struct file *file)
{
	__audit_inode(NULL, file->f_path.dentry, 0);
}

/**
 * __audit_inode_child - collect inode info for created/removed objects
 * @parent: inode of dentry parent
 * @dentry: dentry being audited
 * @type:   AUDIT_TYPE_* value that we're looking for
 *
 * For syscalls that create or remove filesystem objects, audit_inode
 * can only collect information for the filesystem object's parent.
 * This call updates the audit context with the child's information.
 * Syscalls that create a new filesystem object must be hooked after
 * the object is created.  Syscalls that remove a filesystem object
 * must be hooked prior, in order to capture the target inode during
 * unsuccessful attempts.
 */
void __audit_inode_child(const struct inode *parent,
			 const struct dentry *dentry,
			 const unsigned char type)
{
	struct audit_context *context = current->audit_context;
	const struct inode *inode = d_backing_inode(dentry);
	const char *dname = dentry->d_name.name;
	struct audit_names *n, *found_parent = NULL, *found_child = NULL;

	if (!context->in_syscall)
		return;

	if (inode)
		handle_one(inode);

	/* look for a parent entry first */
	list_for_each_entry(n, &context->names_list, list) {
		if (!n->name ||
		    (n->type != AUDIT_TYPE_PARENT &&
		     n->type != AUDIT_TYPE_UNKNOWN))
			continue;

		if (n->ino == parent->i_ino && n->dev == parent->i_sb->s_dev &&
		    !audit_compare_dname_path(dname,
					      n->name->name, n->name_len)) {
			if (n->type == AUDIT_TYPE_UNKNOWN)
				n->type = AUDIT_TYPE_PARENT;
			found_parent = n;
			break;
		}
	}

	/* is there a matching child entry? */
	list_for_each_entry(n, &context->names_list, list) {
		/* can only match entries that have a name */
		if (!n->name ||
		    (n->type != type && n->type != AUDIT_TYPE_UNKNOWN))
			continue;

		if (!strcmp(dname, n->name->name) ||
		    !audit_compare_dname_path(dname, n->name->name,
						found_parent ?
						found_parent->name_len :
						AUDIT_NAME_FULL)) {
			if (n->type == AUDIT_TYPE_UNKNOWN)
				n->type = type;
			found_child = n;
			break;
		}
	}

	if (!found_parent) {
		/* create a new, "anonymous" parent record */
		n = audit_alloc_name(context, AUDIT_TYPE_PARENT);
		if (!n)
			return;
		audit_copy_inode(n, NULL, parent);
	}

	if (!found_child) {
		found_child = audit_alloc_name(context, type);
		if (!found_child)
			return;

		/* Re-use the name belonging to the slot for a matching parent
		 * directory. All names for this context are relinquished in
		 * audit_free_names() */
		if (found_parent) {
			found_child->name = found_parent->name;
			found_child->name_len = AUDIT_NAME_FULL;
			found_child->name->refcnt++;
		}
	}

	if (inode)
		audit_copy_inode(found_child, dentry, inode);
	else
		found_child->ino = AUDIT_INO_UNSET;
}
EXPORT_SYMBOL_GPL(__audit_inode_child);

/**
 * auditsc_get_stamp - get local copies of audit_context values
 * @ctx: audit_context for the task
 * @t: timespec to store time recorded in the audit_context
 * @serial: serial value that is recorded in the audit_context
 *
 * Also sets the context as auditable.
 */
int auditsc_get_stamp(struct audit_context *ctx,
		       struct timespec *t, unsigned int *serial)
{
	if (!ctx->in_syscall)
		return 0;
	if (!ctx->serial)
		ctx->serial = audit_serial();
	t->tv_sec  = ctx->ctime.tv_sec;
	t->tv_nsec = ctx->ctime.tv_nsec;
	*serial    = ctx->serial;
	if (!ctx->prio) {
		ctx->prio = 1;
		ctx->current_state = AUDIT_RECORD_CONTEXT;
	}
	return 1;
}

/* global counter which is incremented every time something logs in */
static atomic_t session_id = ATOMIC_INIT(0);

static int audit_set_loginuid_perm(kuid_t loginuid)
{
	/* if we are unset, we don't need privs */
	if (!audit_loginuid_set(current))
		return 0;
	/* if AUDIT_FEATURE_LOGINUID_IMMUTABLE means never ever allow a change*/
	if (is_audit_feature_set(AUDIT_FEATURE_LOGINUID_IMMUTABLE))
		return -EPERM;
	/* it is set, you need permission */
	if (!capable(CAP_AUDIT_CONTROL))
		return -EPERM;
	/* reject if this is not an unset and we don't allow that */
	if (is_audit_feature_set(AUDIT_FEATURE_ONLY_UNSET_LOGINUID) && uid_valid(loginuid))
		return -EPERM;
	return 0;
}

static void audit_log_set_loginuid(kuid_t koldloginuid, kuid_t kloginuid,
				   unsigned int oldsessionid, unsigned int sessionid,
				   int rc)
{
	struct audit_buffer *ab;
	uid_t uid, oldloginuid, loginuid;
	struct tty_struct *tty;

	if (!audit_enabled)
		return;

	uid = from_kuid(&init_user_ns, task_uid(current));
	oldloginuid = from_kuid(&init_user_ns, koldloginuid);
	loginuid = from_kuid(&init_user_ns, kloginuid),
	tty = audit_get_tty(current);

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_LOGIN);
	if (!ab)
		return;
	audit_log_format(ab, "pid=%d uid=%u", task_pid_nr(current), uid);
	audit_log_task_context(ab);
	audit_log_format(ab, " old-auid=%u auid=%u tty=%s old-ses=%u ses=%u res=%d",
			 oldloginuid, loginuid, tty ? tty_name(tty) : "(none)",
			 oldsessionid, sessionid, !rc);
	audit_put_tty(tty);
	audit_log_end(ab);
}

/**
 * audit_set_loginuid - set current task's audit_context loginuid
 * @loginuid: loginuid value
 *
 * Returns 0.
 *
 * Called (set) from fs/proc/base.c::proc_loginuid_write().
 */
int audit_set_loginuid(kuid_t loginuid)
{
	struct task_struct *task = current;
	unsigned int oldsessionid, sessionid = (unsigned int)-1;
	kuid_t oldloginuid;
	int rc;

	oldloginuid = audit_get_loginuid(current);
	oldsessionid = audit_get_sessionid(current);

	rc = audit_set_loginuid_perm(loginuid);
	if (rc)
		goto out;

	/* are we setting or clearing? */
	if (uid_valid(loginuid))
		sessionid = (unsigned int)atomic_inc_return(&session_id);

	task->sessionid = sessionid;
	task->loginuid = loginuid;
out:
	audit_log_set_loginuid(oldloginuid, loginuid, oldsessionid, sessionid, rc);
	return rc;
}

/**
 * __audit_mq_open - record audit data for a POSIX MQ open
 * @oflag: open flag
 * @mode: mode bits
 * @attr: queue attributes
 *
 */
void __audit_mq_open(int oflag, umode_t mode, struct mq_attr *attr)
{
	struct audit_context *context = current->audit_context;

	if (attr)
		memcpy(&context->mq_open.attr, attr, sizeof(struct mq_attr));
	else
		memset(&context->mq_open.attr, 0, sizeof(struct mq_attr));

	context->mq_open.oflag = oflag;
	context->mq_open.mode = mode;

	context->type = AUDIT_MQ_OPEN;
}

/**
 * __audit_mq_sendrecv - record audit data for a POSIX MQ timed send/receive
 * @mqdes: MQ descriptor
 * @msg_len: Message length
 * @msg_prio: Message priority
 * @abs_timeout: Message timeout in absolute time
 *
 */
void __audit_mq_sendrecv(mqd_t mqdes, size_t msg_len, unsigned int msg_prio,
			const struct timespec *abs_timeout)
{
	struct audit_context *context = current->audit_context;
	struct timespec *p = &context->mq_sendrecv.abs_timeout;

	if (abs_timeout)
		memcpy(p, abs_timeout, sizeof(struct timespec));
	else
		memset(p, 0, sizeof(struct timespec));

	context->mq_sendrecv.mqdes = mqdes;
	context->mq_sendrecv.msg_len = msg_len;
	context->mq_sendrecv.msg_prio = msg_prio;

	context->type = AUDIT_MQ_SENDRECV;
}

/**
 * __audit_mq_notify - record audit data for a POSIX MQ notify
 * @mqdes: MQ descriptor
 * @notification: Notification event
 *
 */

void __audit_mq_notify(mqd_t mqdes, const struct sigevent *notification)
{
	struct audit_context *context = current->audit_context;

	if (notification)
		context->mq_notify.sigev_signo = notification->sigev_signo;
	else
		context->mq_notify.sigev_signo = 0;

	context->mq_notify.mqdes = mqdes;
	context->type = AUDIT_MQ_NOTIFY;
}

/**
 * __audit_mq_getsetattr - record audit data for a POSIX MQ get/set attribute
 * @mqdes: MQ descriptor
 * @mqstat: MQ flags
 *
 */
void __audit_mq_getsetattr(mqd_t mqdes, struct mq_attr *mqstat)
{
	struct audit_context *context = current->audit_context;
	context->mq_getsetattr.mqdes = mqdes;
	context->mq_getsetattr.mqstat = *mqstat;
	context->type = AUDIT_MQ_GETSETATTR;
}

/**
 * audit_ipc_obj - record audit data for ipc object
 * @ipcp: ipc permissions
 *
 */
void __audit_ipc_obj(struct kern_ipc_perm *ipcp)
{
	struct audit_context *context = current->audit_context;
	context->ipc.uid = ipcp->uid;
	context->ipc.gid = ipcp->gid;
	context->ipc.mode = ipcp->mode;
	context->ipc.has_perm = 0;
	security_ipc_getsecid(ipcp, &context->ipc.osid);
	context->type = AUDIT_IPC;
}

/**
 * audit_ipc_set_perm - record audit data for new ipc permissions
 * @qbytes: msgq bytes
 * @uid: msgq user id
 * @gid: msgq group id
 * @mode: msgq mode (permissions)
 *
 * Called only after audit_ipc_obj().
 */
void __audit_ipc_set_perm(unsigned long qbytes, uid_t uid, gid_t gid, umode_t mode)
{
	struct audit_context *context = current->audit_context;

	context->ipc.qbytes = qbytes;
	context->ipc.perm_uid = uid;
	context->ipc.perm_gid = gid;
	context->ipc.perm_mode = mode;
	context->ipc.has_perm = 1;
}

void __audit_bprm(struct linux_binprm *bprm)
{
	struct audit_context *context = current->audit_context;

	context->type = AUDIT_EXECVE;
	context->execve.argc = bprm->argc;
}


/**
 * audit_socketcall - record audit data for sys_socketcall
 * @nargs: number of args, which should not be more than AUDITSC_ARGS.
 * @args: args array
 *
 */
int __audit_socketcall(int nargs, unsigned long *args)
{
	struct audit_context *context = current->audit_context;

	if (nargs <= 0 || nargs > AUDITSC_ARGS || !args)
		return -EINVAL;
	context->type = AUDIT_SOCKETCALL;
	context->socketcall.nargs = nargs;
	memcpy(context->socketcall.args, args, nargs * sizeof(unsigned long));
	return 0;
}

/**
 * __audit_fd_pair - record audit data for pipe and socketpair
 * @fd1: the first file descriptor
 * @fd2: the second file descriptor
 *
 */
void __audit_fd_pair(int fd1, int fd2)
{
	struct audit_context *context = current->audit_context;
	context->fds[0] = fd1;
	context->fds[1] = fd2;
}

/**
 * audit_sockaddr - record audit data for sys_bind, sys_connect, sys_sendto
 * @len: data length in user space
 * @a: data address in kernel space
 *
 * Returns 0 for success or NULL context or < 0 on error.
 */
int __audit_sockaddr(int len, void *a)
{
	struct audit_context *context = current->audit_context;

	if (!context->sockaddr) {
		void *p = kmalloc(sizeof(struct sockaddr_storage), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		context->sockaddr = p;
	}

	context->sockaddr_len = len;
	memcpy(context->sockaddr, a, len);
	return 0;
}

void __audit_ptrace(struct task_struct *t)
{
	struct audit_context *context = current->audit_context;

	context->target_pid = task_pid_nr(t);
	context->target_auid = audit_get_loginuid(t);
	context->target_uid = task_uid(t);
	context->target_sessionid = audit_get_sessionid(t);
	security_task_getsecid(t, &context->target_sid);
	memcpy(context->target_comm, t->comm, TASK_COMM_LEN);
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
	kuid_t uid = current_uid(), t_uid = task_uid(t);

	if (audit_pid && t->tgid == audit_pid) {
		if (sig == SIGTERM || sig == SIGHUP || sig == SIGUSR1 || sig == SIGUSR2) {
			audit_sig_pid = task_pid_nr(tsk);
			if (uid_valid(tsk->loginuid))
				audit_sig_uid = tsk->loginuid;
			else
				audit_sig_uid = uid;
			security_task_getsecid(tsk, &audit_sig_sid);
		}
		if (!audit_signals || audit_dummy_context())
			return 0;
	}

	/* optimize the common case by putting first signal recipient directly
	 * in audit_context */
	if (!ctx->target_pid) {
		ctx->target_pid = task_tgid_nr(t);
		ctx->target_auid = audit_get_loginuid(t);
		ctx->target_uid = t_uid;
		ctx->target_sessionid = audit_get_sessionid(t);
		security_task_getsecid(t, &ctx->target_sid);
		memcpy(ctx->target_comm, t->comm, TASK_COMM_LEN);
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
	BUG_ON(axp->pid_count >= AUDIT_AUX_PIDS);

	axp->target_pid[axp->pid_count] = task_tgid_nr(t);
	axp->target_auid[axp->pid_count] = audit_get_loginuid(t);
	axp->target_uid[axp->pid_count] = t_uid;
	axp->target_sessionid[axp->pid_count] = audit_get_sessionid(t);
	security_task_getsecid(t, &axp->target_sid[axp->pid_count]);
	memcpy(axp->target_comm[axp->pid_count], t->comm, TASK_COMM_LEN);
	axp->pid_count++;

	return 0;
}

/**
 * __audit_log_bprm_fcaps - store information about a loading bprm and relevant fcaps
 * @bprm: pointer to the bprm being processed
 * @new: the proposed new credentials
 * @old: the old credentials
 *
 * Simply check if the proc already has the caps given by the file and if not
 * store the priv escalation info for later auditing at the end of the syscall
 *
 * -Eric
 */
int __audit_log_bprm_fcaps(struct linux_binprm *bprm,
			   const struct cred *new, const struct cred *old)
{
	struct audit_aux_data_bprm_fcaps *ax;
	struct audit_context *context = current->audit_context;
	struct cpu_vfs_cap_data vcaps;

	ax = kmalloc(sizeof(*ax), GFP_KERNEL);
	if (!ax)
		return -ENOMEM;

	ax->d.type = AUDIT_BPRM_FCAPS;
	ax->d.next = context->aux;
	context->aux = (void *)ax;

	get_vfs_caps_from_disk(bprm->file->f_path.dentry, &vcaps);

	ax->fcap.permitted = vcaps.permitted;
	ax->fcap.inheritable = vcaps.inheritable;
	ax->fcap.fE = !!(vcaps.magic_etc & VFS_CAP_FLAGS_EFFECTIVE);
	ax->fcap_ver = (vcaps.magic_etc & VFS_CAP_REVISION_MASK) >> VFS_CAP_REVISION_SHIFT;

	ax->old_pcap.permitted   = old->cap_permitted;
	ax->old_pcap.inheritable = old->cap_inheritable;
	ax->old_pcap.effective   = old->cap_effective;

	ax->new_pcap.permitted   = new->cap_permitted;
	ax->new_pcap.inheritable = new->cap_inheritable;
	ax->new_pcap.effective   = new->cap_effective;
	return 0;
}

/**
 * __audit_log_capset - store information about the arguments to the capset syscall
 * @new: the new credentials
 * @old: the old (current) credentials
 *
 * Record the arguments userspace sent to sys_capset for later printing by the
 * audit system if applicable
 */
void __audit_log_capset(const struct cred *new, const struct cred *old)
{
	struct audit_context *context = current->audit_context;
	context->capset.pid = task_pid_nr(current);
	context->capset.cap.effective   = new->cap_effective;
	context->capset.cap.inheritable = new->cap_effective;
	context->capset.cap.permitted   = new->cap_permitted;
	context->type = AUDIT_CAPSET;
}

void __audit_mmap_fd(int fd, int flags)
{
	struct audit_context *context = current->audit_context;
	context->mmap.fd = fd;
	context->mmap.flags = flags;
	context->type = AUDIT_MMAP;
}

static void audit_log_task(struct audit_buffer *ab)
{
	kuid_t auid, uid;
	kgid_t gid;
	unsigned int sessionid;
	char comm[sizeof(current->comm)];

	auid = audit_get_loginuid(current);
	sessionid = audit_get_sessionid(current);
	current_uid_gid(&uid, &gid);

	audit_log_format(ab, "auid=%u uid=%u gid=%u ses=%u",
			 from_kuid(&init_user_ns, auid),
			 from_kuid(&init_user_ns, uid),
			 from_kgid(&init_user_ns, gid),
			 sessionid);
	audit_log_task_context(ab);
	audit_log_format(ab, " pid=%d comm=", task_pid_nr(current));
	audit_log_untrustedstring(ab, get_task_comm(comm, current));
	audit_log_d_path_exe(ab, current->mm);
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

	if (!audit_enabled)
		return;

	if (signr == SIGQUIT)	/* don't care for those */
		return;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_ANOM_ABEND);
	if (unlikely(!ab))
		return;
	audit_log_task(ab);
	audit_log_format(ab, " sig=%ld", signr);
	audit_log_end(ab);
}

void __audit_seccomp(unsigned long syscall, long signr, int code)
{
	struct audit_buffer *ab;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_SECCOMP);
	if (unlikely(!ab))
		return;
	audit_log_task(ab);
	audit_log_format(ab, " sig=%ld arch=%x syscall=%ld compat=%d ip=0x%lx code=0x%x",
			 signr, syscall_get_arch(), syscall, is_compat_task(),
			 KSTK_EIP(current), code);
	audit_log_end(ab);
}

struct list_head *audit_killed_trees(void)
{
	struct audit_context *ctx = current->audit_context;
	if (likely(!ctx || !ctx->in_syscall))
		return NULL;
	return &ctx->killed_trees;
}
