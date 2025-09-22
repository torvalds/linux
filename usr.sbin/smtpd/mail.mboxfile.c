/*
 * Copyright (c) 2018 Gilles Chehade <gilles@poolp.org>
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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

static void	mboxfile_engine(const char *sender, const char *filename);

int
main(int argc, char *argv[])
{
	int	ch;
	char   *sender = "<unknown>";

	if (! geteuid())
		errx(1, "mail.mboxfile: may not be executed as root");

	while ((ch = getopt(argc, argv, "f:")) != -1) {
		switch (ch) {
		case 'f':
			sender = optarg;
			break;
		default:
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		errx(1, "mail.mboxfile: only one mboxfile is allowed");

	mboxfile_engine(sender, argv[0]);

	return (0);
}

static void
mboxfile_engine(const char *sender, const char *filename)
{
	int	fd;
	FILE    *fp;
	char	*line = NULL;
	size_t	linesize = 0;
	time_t	now;

	time(&now);

	fd = open(filename, O_CREAT | O_APPEND | O_WRONLY | O_EXLOCK, 0600);
	if (fd == -1)
		err(EX_TEMPFAIL, NULL);

	if ((fp = fdopen(fd, "w")) == NULL)
		err(EX_TEMPFAIL, NULL);

	fprintf(fp, "From %s %s", sender, ctime(&now));
	while (getline(&line, &linesize, stdin) != -1) {
		line[strcspn(line, "\n")] = '\0';
		if (strncmp(line, "From ", 5) == 0)
			fprintf(fp, ">%s\n", line);
		else
			fprintf(fp, "%s\n", line);
	}
	fprintf(fp, "\n");
	free(line);
	if (ferror(stdin))
		err(EX_TEMPFAIL, NULL);

	if (fflush(fp) == EOF ||
	    ferror(fp) ||
	    (fsync(fd) == -1 && errno != EINVAL) ||
	    fclose(fp) == EOF)
		err(EX_TEMPFAIL, NULL);
}
