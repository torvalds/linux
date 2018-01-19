/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FIPS_H
#define _FIPS_H

#ifdef CONFIG_CRYPTO_FIPS
extern int fips_enabled;
#else
#define fips_enabled 0
#endif

#endif
