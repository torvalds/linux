/* mac_label_proto.h */
/* $OpenBSD: mac_label_proto.h,v 1.1 2008/03/08 15:36:12 espie Exp $ */
/*
 * Copyright (c) 2008 Marc Espie <espie@openbsd.org>
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

#ifndef MAC_LABEL_PROTO_H
#define MAC_LABEL_PROTO_H

struct deferred_write {
	struct deferred_write * next;
	char		      * table;
	unsigned int		extent;
	unsigned int		size;
	char		      * name;
#ifdef APPLE_HYB
	struct directory_entry *s_entry;
	unsigned int		pad;
	unsigned int		off;
#endif
};

typedef struct deferred_write defer;

extern int gen_mac_label(defer *);
extern int autostart(void);

#endif
