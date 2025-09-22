/*	$OpenBSD: bio_meth.c,v 1.9 2023/07/05 21:23:37 beck Exp $	*/
/*
 * Copyright (c) 2018 Theo Buehler <tb@openbsd.org>
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

#include <openssl/bio.h>

#include "bio_local.h"

BIO_METHOD *
BIO_meth_new(int type, const char *name)
{
	BIO_METHOD *biom;

	if ((biom = calloc(1, sizeof(*biom))) == NULL)
		return NULL;

	biom->type = type;
	biom->name = name;

	return biom;
}
LCRYPTO_ALIAS(BIO_meth_new);

void
BIO_meth_free(BIO_METHOD *biom)
{
	free(biom);
}
LCRYPTO_ALIAS(BIO_meth_free);

int
(*BIO_meth_get_write(const BIO_METHOD *biom))(BIO *, const char *, int)
{
	return biom->bwrite;
}
LCRYPTO_ALIAS(BIO_meth_get_write);

int
BIO_meth_set_write(BIO_METHOD *biom, int (*write)(BIO *, const char *, int))
{
	biom->bwrite = write;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_write);

int
(*BIO_meth_get_read(const BIO_METHOD *biom))(BIO *, char *, int)
{
	return biom->bread;
}
LCRYPTO_ALIAS(BIO_meth_get_read);

int
BIO_meth_set_read(BIO_METHOD *biom, int (*read)(BIO *, char *, int))
{
	biom->bread = read;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_read);

int
(*BIO_meth_get_puts(const BIO_METHOD *biom))(BIO *, const char *)
{
	return biom->bputs;
}
LCRYPTO_ALIAS(BIO_meth_get_puts);

int
BIO_meth_set_puts(BIO_METHOD *biom, int (*puts)(BIO *, const char *))
{
	biom->bputs = puts;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_puts);

int
(*BIO_meth_get_gets(const BIO_METHOD *biom))(BIO *, char *, int)
{
	return biom->bgets;
}
LCRYPTO_ALIAS(BIO_meth_get_gets);

int
BIO_meth_set_gets(BIO_METHOD *biom, int (*gets)(BIO *, char *, int))
{
	biom->bgets = gets;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_gets);

long
(*BIO_meth_get_ctrl(const BIO_METHOD *biom))(BIO *, int, long, void *)
{
	return biom->ctrl;
}
LCRYPTO_ALIAS(BIO_meth_get_ctrl);

int
BIO_meth_set_ctrl(BIO_METHOD *biom, long (*ctrl)(BIO *, int, long, void *))
{
	biom->ctrl = ctrl;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_ctrl);

int
(*BIO_meth_get_create(const BIO_METHOD *biom))(BIO *)
{
	return biom->create;
}
LCRYPTO_ALIAS(BIO_meth_get_create);

int
BIO_meth_set_create(BIO_METHOD *biom, int (*create)(BIO *))
{
	biom->create = create;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_create);

int
(*BIO_meth_get_destroy(const BIO_METHOD *biom))(BIO *)
{
	return biom->destroy;
}
LCRYPTO_ALIAS(BIO_meth_get_destroy);

int
BIO_meth_set_destroy(BIO_METHOD *biom, int (*destroy)(BIO *))
{
	biom->destroy = destroy;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_destroy);

long
(*BIO_meth_get_callback_ctrl(const BIO_METHOD *biom))(BIO *, int, BIO_info_cb *)
{
	return biom->callback_ctrl;
}
LCRYPTO_ALIAS(BIO_meth_get_callback_ctrl);

int
BIO_meth_set_callback_ctrl(BIO_METHOD *biom,
    long (*callback_ctrl)(BIO *, int, BIO_info_cb *))
{
	biom->callback_ctrl = callback_ctrl;
	return 1;
}
LCRYPTO_ALIAS(BIO_meth_set_callback_ctrl);
