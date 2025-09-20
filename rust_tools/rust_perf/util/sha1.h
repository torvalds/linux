/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/types.h>

#define SHA1_DIGEST_SIZE 20

void sha1(const void *data, size_t len, u8 out[SHA1_DIGEST_SIZE]);
