/*	$Id: chngproc.c,v 1.17 2022/05/05 19:51:35 florian Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

int
chngproc(int netsock, const char *root)
{
	char		 *tok = NULL, *th = NULL, *fmt = NULL, **fs = NULL;
	size_t		  i, fsz = 0;
	int		  rc = 0, fd = -1, cc;
	long		  lval;
	enum chngop	  op;
	void		 *pp;


	if (unveil(root, "wc") == -1) {
		warn("unveil %s", root);
		goto out;
	}

	if (pledge("stdio cpath wpath", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	/*
	 * Loop while we wait to get a thumbprint and token.
	 * We'll get this for each SAN request.
	 */

	for (;;) {
		op = CHNG__MAX;
		if ((lval = readop(netsock, COMM_CHNG_OP)) == 0)
			op = CHNG_STOP;
		else if (lval == CHNG_SYN)
			op = lval;

		if (op == CHNG__MAX) {
			warnx("unknown operation from netproc");
			goto out;
		} else if (op == CHNG_STOP)
			break;

		assert(op == CHNG_SYN);

		/*
		 * Read the thumbprint and token.
		 * The token is the filename, so store that in a vector
		 * of tokens that we'll later clean up.
		 */

		if ((th = readstr(netsock, COMM_THUMB)) == NULL)
			goto out;
		else if ((tok = readstr(netsock, COMM_TOK)) == NULL)
			goto out;
		else if (strlen(tok) < 1) {
			warnx("token is too short");
			goto out;
		}

		for (i = 0; tok[i]; ++i) {
			int ch = (unsigned char)tok[i];
			if (!isalnum(ch) && ch != '-' && ch != '_') {
				warnx("token is not a valid base64url");
				goto out;
			}
		}

		if (asprintf(&fmt, "%s.%s", tok, th) == -1) {
			warn("asprintf");
			goto out;
		}

		/* Vector appending... */

		pp = reallocarray(fs, (fsz + 1), sizeof(char *));
		if (pp == NULL) {
			warn("realloc");
			goto out;
		}
		fs = pp;
		if (asprintf(&fs[fsz], "%s/%s", root, tok) == -1) {
			warn("asprintf");
			goto out;
		}
		fsz++;
		free(tok);
		tok = NULL;

		/*
		 * Create and write to our challenge file.
		 * Note: we use file descriptors instead of FILE
		 * because we want to minimise our pledges.
		 */
		fd = open(fs[fsz - 1], O_WRONLY|O_CREAT|O_TRUNC, 0444);
		if (fd == -1) {
			warn("%s", fs[fsz - 1]);
			goto out;
		}
		if (write(fd, fmt, strlen(fmt)) == -1) {
			warn("%s", fs[fsz - 1]);
			goto out;
		}
		if (close(fd) == -1) {
			warn("%s", fs[fsz - 1]);
			goto out;
		}
		fd = -1;

		free(th);
		free(fmt);
		th = fmt = NULL;

		dodbg("%s: created", fs[fsz - 1]);

		/*
		 * Write our acknowledgement.
		 * Ignore reader failure.
		 */

		cc = writeop(netsock, COMM_CHNG_ACK, CHNG_ACK);
		if (cc == 0)
			break;
		if (cc < 0)
			goto out;
	}

	rc = 1;
out:
	close(netsock);
	if (fd != -1)
		close(fd);
	for (i = 0; i < fsz; i++) {
		if (unlink(fs[i]) == -1 && errno != ENOENT)
			warn("%s", fs[i]);
		free(fs[i]);
	}
	free(fs);
	free(fmt);
	free(th);
	free(tok);
	return rc;
}
