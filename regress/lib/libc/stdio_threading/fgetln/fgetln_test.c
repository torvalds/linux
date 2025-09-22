/*
 * Copyright (c) 2008 Bret S. Lambert <blambert@openbsd.org>
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

#include "local.h"

void
fgetln_thread(void *v)
{
	FILE *file = v;
	size_t len;
	char *buf;
	int i;

	for (i = 0; i < 4096; i++) {
		if ((buf = fgetln(file, &len)) == NULL) {

			if (feof(file))
				break;

			printf("OMG!!!\n");
			fflush(stdout);
			break;
		}
		if (strncmp(buf, TEXT_N, sizeof(TEXT_N)))
			err(1, "fgetln not atomic!!!");
	}
}

int
main(void)
{
	char sfn[24];
	FILE *sfp;
	int fd, i;

	strlcpy(sfn, "/tmp/barnacles.XXXXXXXX", sizeof(sfn));
	if ((fd = mkstemp(sfn)) == -1 ||
	    (sfp = fdopen(fd, "w+")) == NULL) {
		int saved_errno = errno;
		if (fd != -1) {
			unlink(sfn);
			close(fd);
		}
		errc(1, saved_errno, "could not open temporary file");
	}

	for (i = 0; i < 4096 * THREAD_COUNT; i++)
		if (fwrite(TEXT_N, sizeof(char), strlen(TEXT_N), sfp) == 0)
			err(1, "Could not populate test file");

	run_threads(fgetln_thread, sfp);

	unlink(sfn);
	close(fd);

	exit(0);
}
