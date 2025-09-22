/*	$OpenBSD: table_getpwnam.c,v 1.16 2024/05/14 13:30:37 op Exp $	*/

/*
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
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

#include <errno.h>
#include <pwd.h>

#include "smtpd.h"

/* getpwnam(3) backend */
static int table_getpwnam_config(struct table *);
static int table_getpwnam_lookup(struct table *, enum table_service, const char *,
    char **);

struct table_backend table_backend_getpwnam = {
	.name = "getpwnam",
	.services = K_USERINFO,
	.config = table_getpwnam_config,
	.add = NULL,
	.dump = NULL,
	.open = NULL,
	.update = NULL,
	.close = NULL,
	.lookup = table_getpwnam_lookup,
	.fetch = NULL,
};


static int
table_getpwnam_config(struct table *table)
{
	if (table->t_config[0])
		return 0;
	return 1;
}

static int
table_getpwnam_lookup(struct table *table, enum table_service kind, const char *key,
    char **dst)
{
	struct passwd	       *pw;

	if (kind != K_USERINFO)
		return -1;

	errno = 0;
	do {
		pw = getpwnam(key);
	} while (pw == NULL && errno == EINTR);

	if (pw == NULL) {
		if (errno)
			return -1;
		return 0;
	}
	if (dst == NULL)
		return 1;

	if (asprintf(dst, "%d:%d:%s",
	    pw->pw_uid,
	    pw->pw_gid,
	    pw->pw_dir) == -1) {
		*dst = NULL;
		return -1;
	}

	return (1);
}
