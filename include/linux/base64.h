// SPDX-License-Identifier: GPL-2.0
/*
 * base64 encoding, lifted from fs/crypto/fname.c.
 */

#ifndef _LINUX_BASE64_H
#define _LINUX_BASE64_H

#include <linux/types.h>

enum base64_variant {
	BASE64_STD,       /* RFC 4648 (standard) */
	BASE64_URLSAFE,   /* RFC 4648 (base64url) */
	BASE64_IMAP,      /* RFC 3501 */
};

#define BASE64_CHARS(nbytes)   DIV_ROUND_UP((nbytes) * 4, 3)

int base64_encode(const u8 *src, int len, char *dst, bool padding, enum base64_variant variant);
int base64_decode(const char *src, int len, u8 *dst, bool padding, enum base64_variant variant);

#endif /* _LINUX_BASE64_H */
