/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Capability hooks
 *
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_CAP_H
#define _SECURITY_LANDLOCK_CAP_H

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/capability.h>
#include <linux/types.h>

/**
 * landlock_cap_to_bit - Convert a capability number to a compact bitmask
 *
 * @cap: Capability number (CAP_*).
 *
 * Return: BIT_ULL(@cap), or 0 if @cap is invalid (with a WARN).
 */
static inline u64 landlock_cap_to_bit(const int cap)
{
	if (WARN_ON_ONCE(!cap_valid(cap)))
		return 0;

	return BIT_ULL(cap);
}

/**
 * landlock_caps_to_bits - Validate and mask a capability bitmask
 *
 * @capabilities: Bitmask of capabilities (e.g. from user space).
 *
 * Return: @capabilities masked to known capabilities.  Warns if unknown bits
 * are present (callers must pre-mask for user input).
 */
static inline u64 landlock_caps_to_bits(const u64 capabilities)
{
	/* Callers pre-mask (CAP_VALID_MASK); the WARN guards future callers. */
	WARN_ON_ONCE(capabilities & ~CAP_VALID_MASK);
	return capabilities & CAP_VALID_MASK;
}

__init void landlock_add_cap_hooks(void);

#endif /* _SECURITY_LANDLOCK_CAP_H */
