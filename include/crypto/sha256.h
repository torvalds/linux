/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2014 Red Hat Inc.
 *
 *  Author: Vivek Goyal <vgoyal@redhat.com>
 */

#ifndef SHA256_H
#define SHA256_H

#include <linux/types.h>
#include <crypto/sha.h>

/*
 * Stand-alone implementation of the SHA256 algorithm. It is designed to
 * have as little dependencies as possible so it can be used in the
 * kexec_file purgatory. In other cases you should generally use the
 * hash APIs from include/crypto/hash.h. Especially when hashing large
 * amounts of data as those APIs may be hw-accelerated.
 *
 * For details see lib/crypto/sha256.c
 */

extern int sha256_init(struct sha256_state *sctx);
extern int sha256_update(struct sha256_state *sctx, const u8 *input,
			 unsigned int length);
extern int sha256_final(struct sha256_state *sctx, u8 *hash);

extern int sha224_init(struct sha256_state *sctx);
extern int sha224_update(struct sha256_state *sctx, const u8 *input,
			 unsigned int length);
extern int sha224_final(struct sha256_state *sctx, u8 *hash);

#endif /* SHA256_H */
