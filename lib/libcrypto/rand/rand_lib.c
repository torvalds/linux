/* $OpenBSD: rand_lib.c,v 1.24 2024/04/10 14:53:01 beck Exp $ */
/*
 * Copyright (c) 2014 Ted Unangst <tedu@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>

#include <openssl/opensslconf.h>

#include <openssl/rand.h>

/*
 * The useful functions in this file are at the bottom.
 */
int
RAND_set_rand_method(const RAND_METHOD *meth)
{
	return 1;
}
LCRYPTO_ALIAS(RAND_set_rand_method);

const RAND_METHOD *
RAND_get_rand_method(void)
{
	return NULL;
}
LCRYPTO_ALIAS(RAND_get_rand_method);

RAND_METHOD *
RAND_SSLeay(void)
{
	return NULL;
}
LCRYPTO_ALIAS(RAND_SSLeay);

void
RAND_cleanup(void)
{

}
LCRYPTO_ALIAS(RAND_cleanup);

void
RAND_seed(const void *buf, int num)
{

}
LCRYPTO_ALIAS(RAND_seed);

void
RAND_add(const void *buf, int num, double entropy)
{

}
LCRYPTO_ALIAS(RAND_add);

int
RAND_status(void)
{
	return 1;
}
LCRYPTO_ALIAS(RAND_status);

int
RAND_poll(void)
{
	return 1;
}
LCRYPTO_ALIAS(RAND_poll);

/*
 * Hurray. You've made it to the good parts.
 */
int
RAND_bytes(unsigned char *buf, int num)
{
	if (num > 0)
		arc4random_buf(buf, num);
	return 1;
}
LCRYPTO_ALIAS(RAND_bytes);

int
RAND_pseudo_bytes(unsigned char *buf, int num)
{
	if (num > 0)
		arc4random_buf(buf, num);
	return 1;
}
LCRYPTO_ALIAS(RAND_pseudo_bytes);
