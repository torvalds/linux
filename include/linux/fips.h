/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FIPS_H
#define _FIPS_H

#ifdef BUILD_FIPS140_KO
/*
 * In fips140.ko, enable the behavior that the upstream fips_enabled flag
 * controls, such as the XTS weak key check.
 */
#define fips_enabled 1
#define CONFIG_CRYPTO_FIPS 1

#elif defined(CONFIG_CRYPTO_FIPS)
extern int fips_enabled;
extern struct atomic_notifier_head fips_fail_notif_chain;

void fips_fail_notify(void);

#else
#define fips_enabled 0

static inline void fips_fail_notify(void) {}

#endif

#endif
