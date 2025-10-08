// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/audit.h>
#include <linux/types.h>
#include <crypto/sha2.h>

#include "ipe.h"
#include "eval.h"
#include "hooks.h"
#include "policy.h"
#include "audit.h"
#include "digest.h"

#define ACTSTR(x) ((x) == IPE_ACTION_ALLOW ? "ALLOW" : "DENY")

#define IPE_AUDIT_HASH_ALG "sha256" /* keep in sync with audit_policy() */

#define AUDIT_POLICY_LOAD_FMT "policy_name=\"%s\" policy_version=%hu.%hu.%hu "\
			      "policy_digest=" IPE_AUDIT_HASH_ALG ":"
#define AUDIT_POLICY_LOAD_NULL_FMT "policy_name=? policy_version=? "\
				   "policy_digest=?"
#define AUDIT_OLD_ACTIVE_POLICY_FMT "old_active_pol_name=\"%s\" "\
				    "old_active_pol_version=%hu.%hu.%hu "\
				    "old_policy_digest=" IPE_AUDIT_HASH_ALG ":"
#define AUDIT_OLD_ACTIVE_POLICY_NULL_FMT "old_active_pol_name=? "\
					 "old_active_pol_version=? "\
					 "old_policy_digest=?"
#define AUDIT_NEW_ACTIVE_POLICY_FMT "new_active_pol_name=\"%s\" "\
				    "new_active_pol_version=%hu.%hu.%hu "\
				    "new_policy_digest=" IPE_AUDIT_HASH_ALG ":"

static const char *const audit_op_names[__IPE_OP_MAX + 1] = {
	"EXECUTE",
	"FIRMWARE",
	"KMODULE",
	"KEXEC_IMAGE",
	"KEXEC_INITRAMFS",
	"POLICY",
	"X509_CERT",
	"UNKNOWN",
};

static const char *const audit_hook_names[__IPE_HOOK_MAX] = {
	"BPRM_CHECK",
	"MMAP",
	"MPROTECT",
	"KERNEL_READ",
	"KERNEL_LOAD",
};

static const char *const audit_prop_names[__IPE_PROP_MAX] = {
	"boot_verified=FALSE",
	"boot_verified=TRUE",
	"dmverity_roothash=",
	"dmverity_signature=FALSE",
	"dmverity_signature=TRUE",
	"fsverity_digest=",
	"fsverity_signature=FALSE",
	"fsverity_signature=TRUE",
};

/**
 * audit_dmv_roothash() - audit the roothash of a dmverity_roothash property.
 * @ab: Supplies a pointer to the audit_buffer to append to.
 * @rh: Supplies a pointer to the digest structure.
 */
static void audit_dmv_roothash(struct audit_buffer *ab, const void *rh)
{
	audit_log_format(ab, "%s", audit_prop_names[IPE_PROP_DMV_ROOTHASH]);
	ipe_digest_audit(ab, rh);
}

/**
 * audit_fsv_digest() - audit the digest of a fsverity_digest property.
 * @ab: Supplies a pointer to the audit_buffer to append to.
 * @d: Supplies a pointer to the digest structure.
 */
static void audit_fsv_digest(struct audit_buffer *ab, const void *d)
{
	audit_log_format(ab, "%s", audit_prop_names[IPE_PROP_FSV_DIGEST]);
	ipe_digest_audit(ab, d);
}

/**
 * audit_rule() - audit an IPE policy rule.
 * @ab: Supplies a pointer to the audit_buffer to append to.
 * @r: Supplies a pointer to the ipe_rule to approximate a string form for.
 */
static void audit_rule(struct audit_buffer *ab, const struct ipe_rule *r)
{
	const struct ipe_prop *ptr;

	audit_log_format(ab, " rule=\"op=%s ", audit_op_names[r->op]);

	list_for_each_entry(ptr, &r->props, next) {
		switch (ptr->type) {
		case IPE_PROP_DMV_ROOTHASH:
			audit_dmv_roothash(ab, ptr->value);
			break;
		case IPE_PROP_FSV_DIGEST:
			audit_fsv_digest(ab, ptr->value);
			break;
		default:
			audit_log_format(ab, "%s", audit_prop_names[ptr->type]);
			break;
		}

		audit_log_format(ab, " ");
	}

	audit_log_format(ab, "action=%s\"", ACTSTR(r->action));
}

/**
 * ipe_audit_match() - Audit a rule match in a policy evaluation.
 * @ctx: Supplies a pointer to the evaluation context that was used in the
 *	 evaluation.
 * @match_type: Supplies the scope of the match: rule, operation default,
 *		global default.
 * @act: Supplies the IPE's evaluation decision, deny or allow.
 * @r: Supplies a pointer to the rule that was matched, if possible.
 */
void ipe_audit_match(const struct ipe_eval_ctx *const ctx,
		     enum ipe_match match_type,
		     enum ipe_action_type act, const struct ipe_rule *const r)
{
	const char *op = audit_op_names[ctx->op];
	char comm[sizeof(current->comm)];
	struct audit_buffer *ab;
	struct inode *inode;

	if (act != IPE_ACTION_DENY && !READ_ONCE(success_audit))
		return;

	ab = audit_log_start(audit_context(), GFP_ATOMIC | __GFP_NOWARN,
			     AUDIT_IPE_ACCESS);
	if (!ab)
		return;

	audit_log_format(ab, "ipe_op=%s ipe_hook=%s enforcing=%d pid=%d comm=",
			 op, audit_hook_names[ctx->hook], READ_ONCE(enforce),
			 task_tgid_nr(current));
	audit_log_untrustedstring(ab, get_task_comm(comm, current));

	if (ctx->file) {
		audit_log_d_path(ab, " path=", &ctx->file->f_path);
		inode = file_inode(ctx->file);
		if (inode) {
			audit_log_format(ab, " dev=");
			audit_log_untrustedstring(ab, inode->i_sb->s_id);
			audit_log_format(ab, " ino=%lu", inode->i_ino);
		} else {
			audit_log_format(ab, " dev=? ino=?");
		}
	} else {
		audit_log_format(ab, " path=? dev=? ino=?");
	}

	if (match_type == IPE_MATCH_RULE)
		audit_rule(ab, r);
	else if (match_type == IPE_MATCH_TABLE)
		audit_log_format(ab, " rule=\"DEFAULT op=%s action=%s\"", op,
				 ACTSTR(act));
	else
		audit_log_format(ab, " rule=\"DEFAULT action=%s\"",
				 ACTSTR(act));

	audit_log_end(ab);
}

/**
 * audit_policy() - Audit a policy's name, version and thumbprint to @ab.
 * @ab: Supplies a pointer to the audit buffer to append to.
 * @audit_format: Supplies a pointer to the audit format string
 * @p: Supplies a pointer to the policy to audit.
 */
static void audit_policy(struct audit_buffer *ab,
			 const char *audit_format,
			 const struct ipe_policy *const p)
{
	u8 digest[SHA256_DIGEST_SIZE];

	sha256(p->pkcs7, p->pkcs7len, digest);

	audit_log_format(ab, audit_format, p->parsed->name,
			 p->parsed->version.major, p->parsed->version.minor,
			 p->parsed->version.rev);
	audit_log_n_hex(ab, digest, sizeof(digest));
}

/**
 * ipe_audit_policy_activation() - Audit a policy being activated.
 * @op: Supplies a pointer to the previously activated policy to audit.
 * @np: Supplies a pointer to the newly activated policy to audit.
 */
void ipe_audit_policy_activation(const struct ipe_policy *const op,
				 const struct ipe_policy *const np)
{
	struct audit_buffer *ab;

	ab = audit_log_start(audit_context(), GFP_KERNEL,
			     AUDIT_IPE_CONFIG_CHANGE);
	if (!ab)
		return;

	if (op) {
		audit_policy(ab, AUDIT_OLD_ACTIVE_POLICY_FMT, op);
		audit_log_format(ab, " ");
	} else {
		/*
		 * old active policy can be NULL if there is no kernel
		 * built-in policy
		 */
		audit_log_format(ab, AUDIT_OLD_ACTIVE_POLICY_NULL_FMT);
		audit_log_format(ab, " ");
	}
	audit_policy(ab, AUDIT_NEW_ACTIVE_POLICY_FMT, np);
	audit_log_format(ab, " auid=%u ses=%u lsm=ipe res=1",
			 from_kuid(&init_user_ns, audit_get_loginuid(current)),
			 audit_get_sessionid(current));

	audit_log_end(ab);
}

/**
 * ipe_audit_policy_load() - Audit a policy loading event.
 * @p: Supplies a pointer to the policy to audit or an error pointer.
 */
void ipe_audit_policy_load(const struct ipe_policy *const p)
{
	struct audit_buffer *ab;
	int err = 0;

	ab = audit_log_start(audit_context(), GFP_KERNEL,
			     AUDIT_IPE_POLICY_LOAD);
	if (!ab)
		return;

	if (!IS_ERR(p)) {
		audit_policy(ab, AUDIT_POLICY_LOAD_FMT, p);
	} else {
		audit_log_format(ab, AUDIT_POLICY_LOAD_NULL_FMT);
		err = PTR_ERR(p);
	}

	audit_log_format(ab, " auid=%u ses=%u lsm=ipe res=%d errno=%d",
			 from_kuid(&init_user_ns, audit_get_loginuid(current)),
			 audit_get_sessionid(current), !err, err);

	audit_log_end(ab);
}

/**
 * ipe_audit_enforce() - Audit a change in IPE's enforcement state.
 * @new_enforce: The new value enforce to be set.
 * @old_enforce: The old value currently in enforce.
 */
void ipe_audit_enforce(bool new_enforce, bool old_enforce)
{
	struct audit_buffer *ab;

	ab = audit_log_start(audit_context(), GFP_KERNEL, AUDIT_MAC_STATUS);
	if (!ab)
		return;

	audit_log(audit_context(), GFP_KERNEL, AUDIT_MAC_STATUS,
		  "enforcing=%d old_enforcing=%d auid=%u ses=%u"
		  " enabled=1 old-enabled=1 lsm=ipe res=1",
		  new_enforce, old_enforce,
		  from_kuid(&init_user_ns, audit_get_loginuid(current)),
		  audit_get_sessionid(current));

	audit_log_end(ab);
}
