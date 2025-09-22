/*	$OpenBSD: main.c,v 1.6 2025/07/10 05:28:13 dlg Exp $ */

/*
 * Copyright (c) 2015 Martin Pieuchot
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

#include "srp_compat.h"

#include <sys/socket.h>
#include <sys/rwlock.h>
#include <net/route.h>
#include <net/rtable.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

__dead void
usage(void)
{
	extern const char *__progname;
	fprintf(stderr, "Usage: %s [inet|inet6] <file>\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *filename;
	sa_family_t af;

	if (argc != 3)
		usage();

	af = strncmp(argv[1], "inet6", 5) ? AF_INET : AF_INET6;
	filename = argv[2];

	rtable_init();

	do_from_file(0, af, filename, route_insert);
	do_from_file(0, af, filename, route_lookup);

	rtable_walk(0, af, NULL, rtentry_dump, NULL);

	do_from_file(0, af, filename, route_delete);

	return (0);
}
