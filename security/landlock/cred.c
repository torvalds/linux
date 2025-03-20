// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Credential hooks
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 */

#include <linux/binfmts.h>
#include <linux/cred.h>
#include <linux/lsm_hooks.h>

#include "common.h"
#include "cred.h"
#include "ruleset.h"
#include "setup.h"

static void hook_cred_transfer(struct cred *const new,
			       const struct cred *const old)
{
	const struct landlock_cred_security *const old_llcred =
		landlock_cred(old);

	if (old_llcred->domain) {
		landlock_get_ruleset(old_llcred->domain);
		*landlock_cred(new) = *old_llcred;
	}
}

static int hook_cred_prepare(struct cred *const new,
			     const struct cred *const old, const gfp_t gfp)
{
	hook_cred_transfer(new, old);
	return 0;
}

static void hook_cred_free(struct cred *const cred)
{
	struct landlock_ruleset *const dom = landlock_cred(cred)->domain;

	if (dom)
		landlock_put_ruleset_deferred(dom);
}

#ifdef CONFIG_AUDIT

static int hook_bprm_creds_for_exec(struct linux_binprm *const bprm)
{
	/* Resets for each execution. */
	landlock_cred(bprm->cred)->domain_exec = 0;
	return 0;
}

#endif /* CONFIG_AUDIT */

static struct security_hook_list landlock_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(cred_prepare, hook_cred_prepare),
	LSM_HOOK_INIT(cred_transfer, hook_cred_transfer),
	LSM_HOOK_INIT(cred_free, hook_cred_free),

#ifdef CONFIG_AUDIT
	LSM_HOOK_INIT(bprm_creds_for_exec, hook_bprm_creds_for_exec),
#endif /* CONFIG_AUDIT */
};

__init void landlock_add_cred_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   &landlock_lsmid);
}
