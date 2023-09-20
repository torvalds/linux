/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * sm2.h - SM2 asymmetric public-key algorithm
 * as specified by OSCCA GM/T 0003.1-2012 -- 0003.5-2012 SM2 and
 * described at https://tools.ietf.org/html/draft-shen-sm2-ecdsa-02
 *
 * Copyright (c) 2020, Alibaba Group.
 * Written by Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#ifndef _CRYPTO_SM2_H
#define _CRYPTO_SM2_H

struct shash_desc;

#if IS_REACHABLE(CONFIG_CRYPTO_SM2)
int sm2_compute_z_digest(struct shash_desc *desc,
			 const void *key, unsigned int keylen, void *dgst);
#else
static inline int sm2_compute_z_digest(struct shash_desc *desc,
				       const void *key, unsigned int keylen,
				       void *dgst)
{
	return -ENOTSUPP;
}
#endif

#endif /* _CRYPTO_SM2_H */
