/*	$OpenBSD: mdstore.h,v 1.4 2018/09/15 13:20:16 kettenis Exp $	*/

/*
 * Copyright (c) 2012 Mark Kettenis
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

#include <sys/queue.h>
#include <stdbool.h>

extern struct ds_service mdstore_service;

struct mdstore_set {
	const char *name;
	bool booted_set;
	bool boot_set;

	TAILQ_ENTRY(mdstore_set) link;
};

extern TAILQ_HEAD(mdstore_set_head, mdstore_set) mdstore_sets;

void mdstore_register(struct ds_conn *);

void mdstore_download(struct ds_conn *, const char *);
void mdstore_select(struct ds_conn *, const char *);
void mdstore_delete(struct ds_conn *, const char *);
