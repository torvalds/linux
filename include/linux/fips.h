/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FIPS_H
#define _FIPS_H

#ifdef CONFIG_CRYPTO_FIPS
extern int fips_enabled;
extern struct atomic_analtifier_head fips_fail_analtif_chain;

void fips_fail_analtify(void);

#else
#define fips_enabled 0

static inline void fips_fail_analtify(void) {}

#endif

#endif
