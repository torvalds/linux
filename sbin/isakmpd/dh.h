/*	$OpenBSD: dh.h,v 1.10 2017/11/08 13:33:49 patrick Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

#ifndef _DH_H_
#define _DH_H_

enum group_type {
	GROUP_MODP	= 0,
	GROUP_EC2N	= 1,
	GROUP_ECP	= 2
};

struct group_id {
	enum group_type	 type;
	u_int		 id;
	int		 bits;
	char		*prime;
	char		*generator;
	int		 nid;
};

struct group {
	int		 id;
	struct group_id	*spec;

	void		*dh;
	void		*ec;

	int		(*init)(struct group *);
	int		(*getlen)(struct group *);
	int		(*secretlen)(struct group *);
	int		(*exchange)(struct group *, u_int8_t *);
	int		(*shared)(struct group *, u_int8_t *, u_int8_t *);
};

#define DH_MAXSZ	1024	/* 8192 bits */

void             group_init(void);
void             group_free(struct group *);
struct group	*group_get(u_int32_t);

int		 dh_getlen(struct group *);
int		 dh_secretlen(struct group *);
int		 dh_create_exchange(struct group *, u_int8_t *);
int		 dh_create_shared(struct group *, u_int8_t *, u_int8_t *);

#endif /* _DH_H_ */
