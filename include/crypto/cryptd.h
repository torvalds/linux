/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Software async crypto daemon
 *
 * Added AEAD support to cryptd.
 *    Authors: Tadeusz Struk (tadeusz.struk@intel.com)
 *             Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Aidan O'Mahony (aidan.o.mahony@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 */

#ifndef _CRYPTO_CRYPT_H
#define _CRYPTO_CRYPT_H

#include <linux/types.h>

#include <crypto/aead.h>

struct cryptd_aead {
	struct crypto_aead base;
};

static inline struct cryptd_aead *__cryptd_aead_cast(
	struct crypto_aead *tfm)
{
	return (struct cryptd_aead *)tfm;
}

struct cryptd_aead *cryptd_alloc_aead(const char *alg_name,
					  u32 type, u32 mask);

struct crypto_aead *cryptd_aead_child(struct cryptd_aead *tfm);
/* Must be called without moving CPUs. */
bool cryptd_aead_queued(struct cryptd_aead *tfm);

void cryptd_free_aead(struct cryptd_aead *tfm);

#endif
