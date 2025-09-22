/*	$OpenBSD: mio_priv.h,v 1.14 2024/05/21 06:07:06 jsg Exp $	*/
/*
 * Copyright (c) 2008 Alexandre Ratchov <alex@caoua.org>
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
#ifndef MIO_PRIV_H
#define MIO_PRIV_H

#include "sndio.h"

#define MIO_MAXNFDS	16

/*
 * private ``handle'' structure
 */
struct mio_hdl {
	struct mio_ops *ops;
	unsigned mode;			/* MIO_IN | MIO_OUT */
	int nbio;			/* true if non-blocking io */
	int eof;			/* true if error occurred */
};

/*
 * operations every device should support
 */
struct mio_ops {
	void (*close)(struct mio_hdl *);
	size_t (*write)(struct mio_hdl *, const void *, size_t);
	size_t (*read)(struct mio_hdl *, void *, size_t);
	int (*nfds)(struct mio_hdl *);
	int (*pollfd)(struct mio_hdl *, struct pollfd *, int);
	int (*revents)(struct mio_hdl *, struct pollfd *);
};

struct mio_hdl *_mio_rmidi_open(const char *, unsigned, int);
struct mio_hdl *_mio_aucat_open(const char *, unsigned, int);
void _mio_create(struct mio_hdl *, struct mio_ops *, unsigned, int);

#endif /* !defined(MIO_PRIV_H) */
