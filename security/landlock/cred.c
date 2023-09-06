// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Credential hooks
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <linux/cred.h>
#include <linux/lsm_hooks.h>

#include "common.h"
#include "cred.h"
#include "ruleset.h"
#include "setup.h"

static int hook_cred_prepare(struct cred *const new,
			     const struct cred *const old, const gfp_t gfp)
{
	struct landlock_ruleset *const old_dom = landlock_cred(old)->domain;

	if (old_dom) {
		landlock_get_ruleset(old_dom);
		landlock_cred(new)->domain = old_dom;
	}
	return 0;
}

static void hook_cred_free(struct cred *const cred)
{
	struct landlock_ruleset *const dom = landlock_cred(cred)->domain;

	if (dom)
		landlock_put_ruleset_deferred(dom);
}

static struct security_hook_list landlock_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(cred_prepare, hook_cred_prepare),
	LSM_HOOK_INIT(cred_free, hook_cred_free),
};

__init void landlock_add_cred_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   LANDLOCK_NAME);
}
