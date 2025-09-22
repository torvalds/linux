/*	$OpenBSD: forward.c,v 1.40 2021/06/14 17:58:15 eric Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
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

#include <sys/stat.h>

#include <stdlib.h>
#include <util.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

#define	MAX_FORWARD_SIZE	(4 * 1024)
#define	MAX_EXPAND_NODES	(100)

int
forwards_get(int fd, struct expand *expand)
{
	FILE	       *fp = NULL;
	char	       *line = NULL;
	size_t		len;
	size_t		lineno;
	size_t		save;
	int		ret;
	struct stat	sb;

	ret = -1;
	if (fstat(fd, &sb) == -1)
		goto end;

	/* if it's empty just pretend that no expansion took place */
	if (sb.st_size == 0) {
		log_info("info: forward file is empty");
		ret = 0;
		goto end;
	}

	/* over MAX_FORWARD_SIZE, temporarily fail */
	if (sb.st_size >= MAX_FORWARD_SIZE) {
		log_info("info: forward file exceeds max size");
		goto end;
	}

	if ((fp = fdopen(fd, "r")) == NULL) {
		log_warn("warn: fdopen failure in forwards_get()");
		goto end;
	}

	lineno = 0;
	save = expand->nb_nodes;
	while ((line = fparseln(fp, &len, &lineno, NULL, 0)) != NULL) {
		if (!expand_line(expand, line, 0)) {
			log_info("info: parse error in forward file");
			goto end;
		}
		if (expand->nb_nodes > MAX_EXPAND_NODES) {
			log_info("info: forward file expanded too many nodes");
			goto end;
		}
		free(line);
	}

	ret = expand->nb_nodes > save ? 1 : 0;

end:
	free(line);
	if (fp)
		fclose(fp);
	else
		close(fd);
	return ret;
}
