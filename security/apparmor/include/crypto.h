/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright 2013 Canonical Ltd.
 */

#ifndef __APPARMOR_CRYPTO_H
#define __APPARMOR_CRYPTO_H

#include "policy.h"

#ifdef CONFIG_SECURITY_APPARMOR_HASH
unsigned int aa_hash_size(void);
char *aa_calc_hash(void *data, size_t len);
int aa_calc_profile_hash(struct aa_profile *profile, u32 version, void *start,
			 size_t len);
#else
static inline char *aa_calc_hash(void *data, size_t len)
{
	return NULL;
}
static inline int aa_calc_profile_hash(struct aa_profile *profile, u32 version,
				       void *start, size_t len)
{
	return 0;
}

static inline unsigned int aa_hash_size(void)
{
	return 0;
}
#endif

#endif /* __APPARMOR_CRYPTO_H */
