/*	$OpenBSD: sioctl_priv.h,v 1.3 2024/05/21 06:07:06 jsg Exp $	*/
/*
 * Copyright (c) 2014-2020 Alexandre Ratchov <alex@caoua.org>
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
#ifndef SIOCTL_PRIV_H
#define SIOCTL_PRIV_H

#include <sndio.h>

#define SIOCTL_MAXNFDS	4

/*
 * private ``handle'' structure
 */
struct sioctl_hdl {
	struct sioctl_ops *ops;
	void (*desc_cb)(void *, struct sioctl_desc *, int);
	void *desc_arg;
	void (*ctl_cb)(void *, unsigned int, unsigned int);
	void *ctl_arg;
	unsigned int mode;		/* SIOCTL_READ | SIOCTL_WRITE */
	int nbio;			/* true if non-blocking io */
	int eof;			/* true if error occurred */
};

/*
 * operations every device should support
 */
struct sioctl_ops {
	void (*close)(struct sioctl_hdl *);
	int (*nfds)(struct sioctl_hdl *);
	int (*pollfd)(struct sioctl_hdl *, struct pollfd *, int);
	int (*revents)(struct sioctl_hdl *, struct pollfd *);
	int (*setctl)(struct sioctl_hdl *, unsigned int, unsigned int);
	int (*onctl)(struct sioctl_hdl *);
	int (*ondesc)(struct sioctl_hdl *);
};

struct sioctl_hdl *_sioctl_aucat_open(const char *, unsigned int, int);
struct sioctl_hdl *_sioctl_sun_open(const char *, unsigned int, int);
void _sioctl_create(struct sioctl_hdl *,
    struct sioctl_ops *, unsigned int, int);
void _sioctl_ondesc_cb(struct sioctl_hdl *,
    struct sioctl_desc *, unsigned int);
void _sioctl_onval_cb(struct sioctl_hdl *, unsigned int, unsigned int);
int _sioctl_psleep(struct sioctl_hdl *, int);

#endif /* !defined(SIOCTL_PRIV_H) */
