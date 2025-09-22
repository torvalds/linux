/*	$OpenBSD: imsgev.h,v 1.3 2017/03/01 00:53:39 gsoares Exp $ */

/*
 * Copyright (c) 2009 Eric Faurot <eric@openbsd.org>
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

#ifndef __IMSGEV_H__
#define __IMSGEV_H__

#include <event.h>
#include <imsg.h>

#define IMSG_LEN(m)	((m)->hdr.len - IMSG_HEADER_SIZE)

struct imsgev {
	struct imsgbuf	 ibuf;
	void		(*handler)(int, short, void *);
	struct event	 ev;
	void		*data;
	short		 events;
	int		 terminate;
	void		(*callback)(struct imsgev *, int, struct imsg *);
	void		(*needfd)(struct imsgev *);
};

#define IMSGEV_IMSG	0
#define IMSGEV_DONE	1
#define IMSGEV_EREAD	2
#define IMSGEV_EWRITE	3
#define IMSGEV_EIMSG	4

void imsgev_init(struct imsgev *, int, void *, void (*)(struct imsgev *,
    int, struct imsg *), void (*)(struct imsgev *));
int  imsgev_compose(struct imsgev *, u_int16_t, u_int32_t, u_int32_t, int,
    void *, u_int16_t);
void imsgev_close(struct imsgev *);
void imsgev_clear(struct imsgev *);

#endif /* __IMSGEV_H__ */
