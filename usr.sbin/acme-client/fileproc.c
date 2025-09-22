/*	$Id: fileproc.c,v 1.18 2021/07/12 15:09:20 beck Exp $ */
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

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

static int
serialise(const char *real, const char *v, size_t vsz, const char *v2, size_t v2sz)
{
	int	  fd;
	char	 *tmp;

	/* create backup hardlink */
	if (asprintf(&tmp, "%s.1", real) == -1) {
		warn("asprintf");
		return 0;
	}
	(void) unlink(tmp);
	if (link(real, tmp) == -1 && errno != ENOENT) {
		warn("link");
		free(tmp);
		return 0;
	}
	free(tmp);

	/*
	 * Write into backup location, overwriting.
	 * Then atomically do the rename.
	 */

	if (asprintf(&tmp, "%s.XXXXXXXXXX", real) == -1) {
		warn("asprintf");
		return 0;
	}
	if ((fd = mkstemp(tmp)) == -1) {
		warn("mkstemp");
		goto out;
	}
	if (fchmod(fd, 0444) == -1) {
		warn("fchmod");
		goto out;
	}
	if ((ssize_t)vsz != write(fd, v, vsz)) {
		warnx("write");
		goto out;
	}
	if (v2 != NULL && write(fd, v2, v2sz) != (ssize_t)v2sz) {
		warnx("write");
		goto out;
	}
	if (close(fd) == -1)
		goto out;
	if (rename(tmp, real) == -1) {
		warn("%s", real);
		goto out;
	}

	free(tmp);
	return 1;
out:
	if (fd != -1)
		close(fd);
	(void) unlink(tmp);
	free(tmp);
	return 0;
}

int
fileproc(int certsock, const char *certdir, const char *certfile, const char
    *chainfile, const char *fullchainfile)
{
	char		*csr = NULL, *ch = NULL;
	size_t		 chsz, csz;
	int		 rc = 0;
	long		 lval;
	enum fileop	 op;

	if (unveil(certdir, "rwc") == -1) {
		warn("unveil %s", certdir);
		goto out;
	}

	/*
	 * rpath and cpath for rename, wpath and cpath for
	 * writing to the temporary. fattr for fchmod.
	 */
	if (pledge("stdio cpath wpath rpath fattr", NULL) == -1) {
		warn("pledge");
		goto out;
	}

	/* Read our operation. */

	op = FILE__MAX;
	if ((lval = readop(certsock, COMM_CHAIN_OP)) == 0)
		op = FILE_STOP;
	else if (lval == FILE_CREATE || lval == FILE_REMOVE)
		op = lval;

	if (FILE_STOP == op) {
		rc = 1;
		goto out;
	} else if (FILE__MAX == op) {
		warnx("unknown operation from certproc");
		goto out;
	}

	/*
	 * If revoking certificates, just unlink the files.
	 * We return the special error code of 2 to indicate that the
	 * certificates were removed.
	 */

	if (FILE_REMOVE == op) {
		if (certfile) {
			if (unlink(certfile) == -1 && errno != ENOENT) {
				warn("%s", certfile);
				goto out;
			} else
				dodbg("%s: unlinked", certfile);
		}

		if (chainfile) {
			if (unlink(chainfile) == -1 && errno != ENOENT) {
				warn("%s", chainfile);
				goto out;
			} else
				dodbg("%s: unlinked", chainfile);
		}

		if (fullchainfile) {
			if (unlink(fullchainfile) == -1 && errno != ENOENT) {
				warn("%s", fullchainfile);
				goto out;
			} else
				dodbg("%s: unlinked", fullchainfile);
		}

		rc = 2;
		goto out;
	}

	/*
	 * Start by downloading the chain PEM as a buffer.
	 * This is not NUL-terminated, but we're just going to guess
	 * that it's well-formed and not actually touch the data.
	 */
	if ((ch = readbuf(certsock, COMM_CHAIN, &chsz)) == NULL)
		goto out;

	if (chainfile) {
		if (!serialise(chainfile, ch, chsz, NULL, 0))
			goto out;

		dodbg("%s: created", chainfile);
	}

	/*
	 * Next, wait until we receive the DER encoded (signed)
	 * certificate from the network process.
	 * This comes as a stream of bytes: we don't know how many, so
	 * just keep downloading.
	 */

	if ((csr = readbuf(certsock, COMM_CSR, &csz)) == NULL)
		goto out;

	if (certfile) {
		if (!serialise(certfile, csr, csz, NULL, 0))
			goto out;

		dodbg("%s: created", certfile);
	}

	/*
	 * Finally, create the full-chain file.
	 * This is just the concatenation of the certificate and chain.
	 * We return the special error code 2 to indicate that the
	 * on-file certificates were changed.
	 */
	if (fullchainfile) {
		if (!serialise(fullchainfile, csr, csz, ch,
		    chsz))
			goto out;

		dodbg("%s: created", fullchainfile);
	}

	rc = 2;
out:
	close(certsock);
	free(csr);
	free(ch);
	return rc;
}
