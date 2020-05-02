/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CRYPTOHASH_H
#define __CRYPTOHASH_H

#include <uapi/linux/types.h>

/*
 * An implementation of SHA-1's compression function.  Don't use in new code!
 * You shouldn't be using SHA-1, and even if you *have* to use SHA-1, this isn't
 * the correct way to hash something with SHA-1 (use crypto_shash instead).
 */
#define SHA1_DIGEST_WORDS 5
#define SHA1_BLOCK_SIZE 64
#define SHA1_WORKSPACE_WORDS 16
void sha1_init(__u32 *buf);
void sha1_transform(__u32 *digest, const char *data, __u32 *W);

#endif
