// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (secid) manipulation fns
 *
 * Copyright 2009-2017 Canonical Ltd.
 *
 * AppArmor allocates a unique secid for every label used. If a label
 * is replaced it receives the secid of the label it is replacing.
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>

#include "include/cred.h"
#include "include/lib.h"
#include "include/secid.h"
#include "include/label.h"
#include "include/policy_ns.h"

/*
 * secids - do not pin labels with a refcount. They rely on the label
 * properly updating/freeing them
 */
#define AA_FIRST_SECID 2

static DEFINE_XARRAY_FLAGS(aa_secids, XA_FLAGS_LOCK_IRQ | XA_FLAGS_TRACK_FREE);

int apparmor_display_secid_mode;

/*
 * TODO: allow policy to reserve a secid range?
 * TODO: add secid pinning
 * TODO: use secid_update in label replace
 */

/**
 * aa_secid_update - update a secid mapping to a new label
 * @secid: secid to update
 * @label: label the secid will now map to
 */
void aa_secid_update(u32 secid, struct aa_label *label)
{
	unsigned long flags;

	xa_lock_irqsave(&aa_secids, flags);
	__xa_store(&aa_secids, secid, label, 0);
	xa_unlock_irqrestore(&aa_secids, flags);
}

/*
 * see label for inverse aa_label_to_secid
 */
struct aa_label *aa_secid_to_label(u32 secid)
{
	return xa_load(&aa_secids, secid);
}

static int apparmor_label_to_secctx(struct aa_label *label, char **secdata,
				    u32 *seclen)
{
	/* TODO: cache secctx and ref count so we don't have to recreate */
	int flags = FLAG_VIEW_SUBNS | FLAG_HIDDEN_UNCONFINED | FLAG_ABS_ROOT;
	int len;

	AA_BUG(!seclen);

	if (!label)
		return -EINVAL;

	if (apparmor_display_secid_mode)
		flags |= FLAG_SHOW_MODE;

	if (secdata)
		len = aa_label_asxprint(secdata, root_ns, label,
					flags, GFP_ATOMIC);
	else
		len = aa_label_snxprint(NULL, 0, root_ns, label, flags);

	if (len < 0)
		return -ENOMEM;

	*seclen = len;

	return 0;
}

int apparmor_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	struct aa_label *label = aa_secid_to_label(secid);

	return apparmor_label_to_secctx(label, secdata, seclen);
}

int apparmor_lsmprop_to_secctx(struct lsm_prop *prop, char **secdata,
			       u32 *seclen)
{
	struct aa_label *label;

	label = prop->apparmor.label;

	return apparmor_label_to_secctx(label, secdata, seclen);
}

int apparmor_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	struct aa_label *label;

	label = aa_label_strn_parse(&root_ns->unconfined->label, secdata,
				    seclen, GFP_KERNEL, false, false);
	if (IS_ERR(label))
		return PTR_ERR(label);
	*secid = label->secid;

	return 0;
}

void apparmor_release_secctx(char *secdata, u32 seclen)
{
	kfree(secdata);
}

/**
 * aa_alloc_secid - allocate a new secid for a profile
 * @label: the label to allocate a secid for
 * @gfp: memory allocation flags
 *
 * Returns: 0 with @label->secid initialized
 *          <0 returns error with @label->secid set to AA_SECID_INVALID
 */
int aa_alloc_secid(struct aa_label *label, gfp_t gfp)
{
	unsigned long flags;
	int ret;

	xa_lock_irqsave(&aa_secids, flags);
	ret = __xa_alloc(&aa_secids, &label->secid, label,
			XA_LIMIT(AA_FIRST_SECID, INT_MAX), gfp);
	xa_unlock_irqrestore(&aa_secids, flags);

	if (ret < 0) {
		label->secid = AA_SECID_INVALID;
		return ret;
	}

	return 0;
}

/**
 * aa_free_secid - free a secid
 * @secid: secid to free
 */
void aa_free_secid(u32 secid)
{
	unsigned long flags;

	xa_lock_irqsave(&aa_secids, flags);
	__xa_erase(&aa_secids, secid);
	xa_unlock_irqrestore(&aa_secids, flags);
}
