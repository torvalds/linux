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
 *	@sid: the context ID to check
 *	@field: the field this rule refers to
 *	@op: the operater the rule uses
 *	@rule: pointer to the audit rule to check against
 *	@actx: the audit context (can be NULL) associated with the check
 *
 *	Returns 1 if the context id matches the rule, 0 if it does not, and
 *	-errno on failure.
 */
int selinux_audit_rule_match(u32 sid, u32 field, u32 op,
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
 *     selinux_string_to_sid - map a security context string to a security ID
 *     @str: the security context string to be mapped
 *     @sid: ID value returned via this.
 *
 *     Returns 0 if successful, with the SID stored in sid.  A value
 *     of zero for sid indicates no SID could be determined (but no error
 *     occurred).
 */
int selinux_string_to_sid(char *str, u32 *sid);

/**
 *     selinux_secmark_relabel_packet_permission - secmark permission check
 *     @sid: SECMARK ID value to be applied to network packet
 *
 *     Returns 0 if the current task is allowed to set the SECMARK label of
 *     packets with the supplied security ID.  Note that it is implicit that
 *     the packet is always being relabeled from the default unlabeled value,
 *     and that the access control decision is made in the AVC.
 */
int selinux_secmark_relabel_packet_permission(u32 sid);

/**
 *     selinux_secmark_refcount_inc - increments the secmark use counter
 *
 *     SELinux keeps track of the current SECMARK targets in use so it knows
 *     when to apply SECMARK label access checks to network packets.  This
 *     function incements this reference count to indicate that a new SECMARK
 *     target has been configured.
 */
void selinux_secmark_refcount_inc(void);

/**
 *     selinux_secmark_refcount_dec - decrements the secmark use counter
 *
 *     SELinux keeps track of the current SECMARK targets in use so it knows
 *     when to apply SECMARK label access checks to network packets.  This
 *     function decements this reference count to indicate that one of the
 *     existing SECMARK targets has been removed/flushed.
 */
void selinux_secmark_refcount_dec(void);
#else

static inline int selinux_audit_rule_init(u32 field, u32 op,
                                          char *rulestr,
                                          struct selinux_audit_rule **rule)
{
	return -EOPNOTSUPP;
}

static inline void selinux_audit_rule_free(struct selinux_audit_rule *rule)
{
	return;
}

static inline int selinux_audit_rule_match(u32 sid, u32 field, u32 op,
                                           struct selinux_audit_rule *rule,
                                           struct audit_context *actx)
{
	return 0;
}

static inline void selinux_audit_set_callback(int (*callback)(void))
{
	return;
}

static inline int selinux_string_to_sid(const char *str, u32 *sid)
{
       *sid = 0;
       return 0;
}

static inline int selinux_secmark_relabel_packet_permission(u32 sid)
{
	return 0;
}

static inline void selinux_secmark_refcount_inc(void)
{
	return;
}

static inline void selinux_secmark_refcount_dec(void)
{
	return;
}

#endif	/* CONFIG_SECURITY_SELINUX */

#endif /* _LINUX_SELINUX_H */
