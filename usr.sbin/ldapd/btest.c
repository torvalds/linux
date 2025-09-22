/*	$OpenBSD: btest.c,v 1.3 2017/02/22 14:40:46 gsoares Exp $ */

/* Simple test program for the btree database. */
/*
 * Copyright (c) 2009 Martin Hedenfalk <martin@bzero.se>
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

#include <sys/types.h>

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include "btree.h"

int
main(int argc, char **argv)
{
	int		 c, rc = BT_FAIL;
	unsigned int	 flags = 0;
	struct btree	*bt;
	struct cursor	*cursor;
	const char	*filename = "test.db";
	struct btval	 key, data, maxkey;

	while ((c = getopt(argc, argv, "rf:")) != -1) {
		switch (c) {
		case 'r':
			flags |= BT_REVERSEKEY;
			break;
		case 'f':
			filename = optarg;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		errx(1, "missing command");

	bt = btree_open(filename, flags | BT_NOSYNC, 0644);
	if (bt == NULL)
		err(1, filename);

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	memset(&maxkey, 0, sizeof(maxkey));

	if (strcmp(argv[0], "put") == 0) {
		if (argc < 3)
			errx(1, "missing arguments");
		key.data = argv[1];
		key.size = strlen(key.data);
		data.data = argv[2];
		data.size = strlen(data.data);
		rc = btree_put(bt, &key, &data, 0);
		if (rc == BT_SUCCESS)
			printf("OK\n");
		else
			printf("FAIL\n");
	} else if (strcmp(argv[0], "del") == 0) {
		if (argc < 2)
			errx(1, "missing argument");
		key.data = argv[1];
		key.size = strlen(key.data);
		rc = btree_del(bt, &key, NULL);
		if (rc == BT_SUCCESS)
			printf("OK\n");
		else
			printf("FAIL\n");
	} else if (strcmp(argv[0], "get") == 0) {
		if (argc < 2)
			errx(1, "missing arguments");
		key.data = argv[1];
		key.size = strlen(key.data);
		rc = btree_get(bt, &key, &data);
		if (rc == BT_SUCCESS) {
			printf("OK %.*s\n", (int)data.size, (char *)data.data);
		} else {
			printf("FAIL\n");
		}
	} else if (strcmp(argv[0], "scan") == 0) {
		if (argc > 1) {
			key.data = argv[1];
			key.size = strlen(key.data);
			flags = BT_CURSOR;
		}
		else
			flags = BT_FIRST;
		if (argc > 2) {
			maxkey.data = argv[2];
			maxkey.size = strlen(key.data);
		}

		cursor = btree_cursor_open(bt);
		while ((rc = btree_cursor_get(cursor, &key, &data,
		    flags)) == BT_SUCCESS) {
			if (argc > 2 && btree_cmp(bt, &key, &maxkey) > 0)
				break;
			printf("OK %zi %.*s\n",
			    key.size, (int)key.size, (char *)key.data);
			flags = BT_NEXT;
		}
		btree_cursor_close(cursor);
	} else if (strcmp(argv[0], "compact") == 0) {
		if ((rc = btree_compact(bt)) != BT_SUCCESS)
			warn("compact");
	} else if (strcmp(argv[0], "revert") == 0) {
		if ((rc = btree_revert(bt)) != BT_SUCCESS)
			warn("revert");
	} else
		errx(1, "%s: invalid command", argv[0]);

	btree_close(bt);

	return rc;
}

