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

#include <crypto/sm3.h>
#include <crypto/akcipher.h>

/* The default user id as specified in GM/T 0009-2012 */
#define SM2_DEFAULT_USERID "1234567812345678"
#define SM2_DEFAULT_USERID_LEN 16

extern int sm2_compute_z_digest(struct crypto_akcipher *tfm,
			const unsigned char *id, size_t id_len,
			unsigned char dgst[SM3_DIGEST_SIZE]);

#endif /* _CRYPTO_SM2_H */
