/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FIPS_H
#define _FIPS_H

#ifdef CONFIG_CRYPTO_FIPS
extern int fips_enabled;
extern struct atomic_yestifier_head fips_fail_yestif_chain;

void fips_fail_yestify(void);

#else
#define fips_enabled 0

static inline void fips_fail_yestify(void) {}

#endif

#endif
