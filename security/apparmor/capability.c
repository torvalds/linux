// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor capability mediation functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/security.h>

#include "include/apparmor.h"
#include "include/capability.h"
#include "include/cred.h"
#include "include/policy.h"
#include "include/audit.h"

/*
 * Table of capability names: we generate it from capabilities.h.
 */
#include "capability_names.h"

struct aa_sfs_entry aa_sfs_entry_caps[] = {
	AA_SFS_FILE_STRING("mask", AA_SFS_CAPS_MASK),
	{ }
};

struct audit_cache {
	struct aa_profile *profile;
	kernel_cap_t caps;
};

static DEFINE_PER_CPU(struct audit_cache, audit_cache);

/**
 * audit_cb - call back for capability components of audit struct
 * @ab: audit buffer   (NOT NULL)
 * @va: audit struct to audit data from  (NOT NULL)
 */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	audit_log_format(ab, " capname=");
	audit_log_untrustedstring(ab, capability_names[sa->u.cap]);
}

/**
 * audit_caps - audit a capability
 * @as: audit data
 * @profile: profile being tested for confinement (NOT NULL)
 * @cap: capability tested
 * @error: error code returned by test
 *
 * Do auditing of capability and handle, audit/complain/kill modes switching
 * and duplicate message elimination.
 *
 * Returns: 0 or ad->error on success,  error code on failure
 */
static int audit_caps(struct apparmor_audit_data *ad, struct aa_profile *profile,
		      int cap, int error)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	struct audit_cache *ent;
	int type = AUDIT_APPARMOR_AUTO;

	ad->error = error;

	if (likely(!error)) {
		/* test if auditing is being forced */
		if (likely((AUDIT_MODE(profile) != AUDIT_ALL) &&
			   !cap_raised(rules->caps.audit, cap)))
			return 0;
		type = AUDIT_APPARMOR_AUDIT;
	} else if (KILL_MODE(profile) ||
		   cap_raised(rules->caps.kill, cap)) {
		type = AUDIT_APPARMOR_KILL;
	} else if (cap_raised(rules->caps.quiet, cap) &&
		   AUDIT_MODE(profile) != AUDIT_NOQUIET &&
		   AUDIT_MODE(profile) != AUDIT_ALL) {
		/* quiet auditing */
		return error;
	}

	/* Do simple duplicate message elimination */
	ent = &get_cpu_var(audit_cache);
	if (profile == ent->profile && cap_raised(ent->caps, cap)) {
		put_cpu_var(audit_cache);
		if (COMPLAIN_MODE(profile))
			return complain_error(error);
		return error;
	} else {
		aa_put_profile(ent->profile);
		ent->profile = aa_get_profile(profile);
		cap_raise(ent->caps, cap);
	}
	put_cpu_var(audit_cache);

	return aa_audit(type, profile, ad, audit_cb);
}

/**
 * profile_capable - test if profile allows use of capability @cap
 * @profile: profile being enforced    (NOT NULL, NOT unconfined)
 * @cap: capability to test if allowed
 * @opts: CAP_OPT_NOAUDIT bit determines whether audit record is generated
 * @ad: audit data (MAY BE NULL indicating no auditing)
 *
 * Returns: 0 if allowed else -EPERM
 */
static int profile_capable(struct aa_profile *profile, int cap,
			   unsigned int opts, struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	int error;

	if (cap_raised(rules->caps.allow, cap) &&
	    !cap_raised(rules->caps.denied, cap))
		error = 0;
	else
		error = -EPERM;

	if (opts & CAP_OPT_NOAUDIT) {
		if (!COMPLAIN_MODE(profile))
			return error;
		/* audit the cap request in complain mode but note that it
		 * should be optional.
		 */
		ad->info = "optional: no audit";
	}

	return audit_caps(ad, profile, cap, error);
}

/**
 * aa_capable - test permission to use capability
 * @subj_cread: cred we are testing capability against
 * @label: label being tested for capability (NOT NULL)
 * @cap: capability to be tested
 * @opts: CAP_OPT_NOAUDIT bit determines whether audit record is generated
 *
 * Look up capability in profile capability set.
 *
 * Returns: 0 on success, or else an error code.
 */
int aa_capable(const struct cred *subj_cred, struct aa_label *label,
	       int cap, unsigned int opts)
{
	struct aa_profile *profile;
	int error = 0;
	DEFINE_AUDIT_DATA(ad, LSM_AUDIT_DATA_CAP, AA_CLASS_CAP, OP_CAPABLE);

	ad.subj_cred = subj_cred;
	ad.common.u.cap = cap;
	error = fn_for_each_confined(label, profile,
			profile_capable(profile, cap, opts, &ad));

	return error;
}
