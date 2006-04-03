/*
 * SELinux services exported to the rest of the kernel.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2005 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2006 Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 * Copyright (C) 2006 IBM Corporation, Timothy R. Chavez <tinytim@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#ifndef _LINUX_SELINUX_H
#define _LINUX_SELINUX_H

struct selinux_audit_rule;
struct audit_context;
struct inode;
struct kern_ipc_perm;

#ifdef CONFIG_SECURITY_SELINUX

/**
 *	selinux_audit_rule_init - alloc/init an selinux audit rule structure.
 *	@field: the field this rule refers to
 *	@op: the operater the rule uses
 *	@rulestr: the text "target" of the rule
 *	@rule: pointer to the new rule structure returned via this
 *
 *	Returns 0 if successful, -errno if not.  On success, the rule structure
 *	will be allocated internally.  The caller must free this structure with
 *	selinux_audit_rule_free() after use.
 */
int selinux_audit_rule_init(u32 field, u32 op, char *rulestr,
                            struct selinux_audit_rule **rule);

/**
 *	selinux_audit_rule_free - free an selinux audit rule structure.
 *	@rule: pointer to the audit rule to be freed
 *
 *	This will free all memory associated with the given rule.
 *	If @rule is NULL, no operation is performed.
 */
void selinux_audit_rule_free(struct selinux_audit_rule *rule);

/**
 *	selinux_audit_rule_match - determine if a context ID matches a rule.
 *	@ctxid: the context ID to check
 *	@field: the field this rule refers to
 *	@op: the operater the rule uses
 *	@rule: pointer to the audit rule to check against
 *	@actx: the audit context (can be NULL) associated with the check
 *
 *	Returns 1 if the context id matches the rule, 0 if it does not, and
 *	-errno on failure.
 */
int selinux_audit_rule_match(u32 ctxid, u32 field, u32 op,
                             struct selinux_audit_rule *rule,
                             struct audit_context *actx);

/**
 *	selinux_audit_set_callback - set the callback for policy reloads.
 *	@callback: the function to call when the policy is reloaded
 *
 *	This sets the function callback function that will update the rules
 *	upon policy reloads.  This callback should rebuild all existing rules
 *	using selinux_audit_rule_init().
 */
void selinux_audit_set_callback(int (*callback)(void));

/**
 *	selinux_task_ctxid - determine a context ID for a process.
 *	@tsk: the task object
 *	@ctxid: ID value returned via this
 *
 *	On return, ctxid will contain an ID for the context.  This value
 *	should only be used opaquely.
 */
void selinux_task_ctxid(struct task_struct *tsk, u32 *ctxid);

/**
 *     selinux_ctxid_to_string - map a security context ID to a string
 *     @ctxid: security context ID to be converted.
 *     @ctx: address of context string to be returned
 *     @ctxlen: length of returned context string.
 *
 *     Returns 0 if successful, -errno if not.  On success, the context
 *     string will be allocated internally, and the caller must call
 *     kfree() on it after use.
 */
int selinux_ctxid_to_string(u32 ctxid, char **ctx, u32 *ctxlen);

/**
 *     selinux_get_inode_sid - get the inode's security context ID
 *     @inode: inode structure to get the sid from.
 *     @sid: pointer to security context ID to be filled in.
 *
 *     Returns nothing
 */
void selinux_get_inode_sid(const struct inode *inode, u32 *sid);

/**
 *     selinux_get_ipc_sid - get the ipc security context ID
 *     @ipcp: ipc structure to get the sid from.
 *     @sid: pointer to security context ID to be filled in.
 *
 *     Returns nothing
 */
void selinux_get_ipc_sid(const struct kern_ipc_perm *ipcp, u32 *sid);

/**
 *     selinux_get_task_sid - return the SID of task
 *     @tsk: the task whose SID will be returned
 *     @sid: pointer to security context ID to be filled in.
 *
 *     Returns nothing
 */
void selinux_get_task_sid(struct task_struct *tsk, u32 *sid);


#else

static inline int selinux_audit_rule_init(u32 field, u32 op,
                                          char *rulestr,
                                          struct selinux_audit_rule **rule)
{
	return -ENOTSUPP;
}

static inline void selinux_audit_rule_free(struct selinux_audit_rule *rule)
{
	return;
}

static inline int selinux_audit_rule_match(u32 ctxid, u32 field, u32 op,
                                           struct selinux_audit_rule *rule,
                                           struct audit_context *actx)
{
	return 0;
}

static inline void selinux_audit_set_callback(int (*callback)(void))
{
	return;
}

static inline void selinux_task_ctxid(struct task_struct *tsk, u32 *ctxid)
{
	*ctxid = 0;
}

static inline int selinux_ctxid_to_string(u32 ctxid, char **ctx, u32 *ctxlen)
{
       *ctx = NULL;
       *ctxlen = 0;
       return 0;
}

static inline void selinux_get_inode_sid(const struct inode *inode, u32 *sid)
{
	*sid = 0;
}

static inline void selinux_get_ipc_sid(const struct kern_ipc_perm *ipcp, u32 *sid)
{
	*sid = 0;
}

static inline void selinux_get_task_sid(struct task_struct *tsk, u32 *sid)
{
	*sid = 0;
}

#endif	/* CONFIG_SECURITY_SELINUX */

#endif /* _LINUX_SELINUX_H */
