/*
 * SELinux services exported to the rest of the kernel.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2005 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2006 Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#ifndef _LINUX_SELINUX_H
#define _LINUX_SELINUX_H

struct selinux_audit_rule;
struct audit_context;

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

#endif	/* CONFIG_SECURITY_SELINUX */

#endif /* _LINUX_SELINUX_H */
