/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PKWM_TRUSTED_KEY_H
#define __PKWM_TRUSTED_KEY_H

#include <keys/trusted-type.h>
#include <linux/bitops.h>
#include <linux/printk.h>

extern struct trusted_key_ops pkwm_trusted_key_ops;

struct trusted_pkwm_options {
	u16 wrap_flags;
};

static inline void dump_options(struct trusted_key_options *o)
{
	const struct trusted_pkwm_options *pkwm;
	bool sb_audit_or_enforce_bit;
	bool sb_enforce_bit;

	pkwm = o->private;
	sb_audit_or_enforce_bit = pkwm->wrap_flags & BIT(0);
	sb_enforce_bit = pkwm->wrap_flags & BIT(1);

	if (sb_audit_or_enforce_bit)
		pr_debug("secure boot mode required: audit or enforce");
	else if (sb_enforce_bit)
		pr_debug("secure boot mode required: enforce");
	else
		pr_debug("secure boot mode required: disabled");
}

#endif
