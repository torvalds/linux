// SPDX-License-Identifier: GPL-2.0
/*
 * base64 encoding, lifted from fs/crypto/fname.c.
 */

#ifndef _LINUX_BASE64_H
#define _LINUX_BASE64_H

#include <linux/types.h>

#define BASE64_CHARS(nbytes)   DIV_ROUND_UP((nbytes) * 4, 3)

int base64_encode(const u8 *src, int len, char *dst);
int base64_decode(const char *src, int len, u8 *dst);

#endif /* _LINUX_BASE64_H */
