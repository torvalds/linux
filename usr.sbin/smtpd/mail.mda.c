/*
 * Copyright (c) 2017 Gilles Chehade <gilles@poolp.org>
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

#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	int ch;
	int ret;

	if (! geteuid())
		errx(1, "mail.mda: may not be executed as root");

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		errx(1, "mail.mda: command required");

	if (argc > 1)
		errx(1, "mail.mda: only one command is supported");

	/* could not obtain a shell or could not obtain wait status,
	 * tempfail */
	if ((ret = system(argv[0])) == -1)
		errx(EX_TEMPFAIL, "%s", strerror(errno));

	/* not exited properly but we have no details,
	 * tempfail */
	if (! WIFEXITED(ret))
		exit(EX_TEMPFAIL);

	exit(WEXITSTATUS(ret));
}
