/*	$OpenBSD: mailaddr.c,v 1.5 2021/06/14 17:58:15 eric Exp $	*/

/*
 * Copyright (c) 2015 Gilles Chehade <gilles@poolp.org>
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

#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

static int
mailaddr_line_split(char **line, char **ret)
{
	static char	buffer[LINE_MAX];
	int		esc, dq, sq;
	size_t		i;
	char	       *s;

	memset(buffer, 0, sizeof buffer);
	esc = dq = sq = 0;
	i = 0;
	for (s = *line; (*s) && (i < sizeof(buffer)); ++s) {
		if (esc) {
			buffer[i++] = *s;
			esc = 0;
			continue;
		}
		if (*s == '\\') {
			esc = 1;
			continue;
		}
		if (*s == ',' && !dq && !sq) {
			*ret = buffer;
			*line = s+1;
			return (1);
		}

		buffer[i++] = *s;
		esc = 0;

		if (*s == '"' && !sq)
			dq ^= 1;
		if (*s == '\'' && !dq)
			sq ^= 1;
	}

	if (esc || dq || sq || i == sizeof(buffer))
		return (-1);

	*ret = buffer;
	*line = s;
	return (i ? 1 : 0);
}

int
mailaddr_line(struct maddrmap *maddrmap, const char *s)
{
	struct maddrnode	mn;
	char		       *p, *subrcpt, *buffer;
	int			ret;

	if ((buffer = strdup(s)) == NULL)
		return 0;

	p = buffer;
	while ((ret = mailaddr_line_split(&p, &subrcpt)) > 0) {
		subrcpt = strip(subrcpt);
		if (subrcpt[0] == '\0')
			continue;
		if (!text_to_mailaddr(&mn.mailaddr, subrcpt)) {
			free(buffer);
			return 0;
		}
		log_debug("subrcpt: [%s]", subrcpt);
		maddrmap_insert(maddrmap, &mn);
	}

	free(buffer);

	if (ret >= 0)
		return 1;
	/* expand_line_split() returned < 0 */
	return 0;
}

void
maddrmap_init(struct maddrmap *maddrmap)
{
	TAILQ_INIT(&maddrmap->queue);
}

void
maddrmap_insert(struct maddrmap *maddrmap, struct maddrnode *maddrnode)
{
	struct maddrnode	*mn;

	mn = xmemdup(maddrnode, sizeof *maddrnode);
	TAILQ_INSERT_TAIL(&maddrmap->queue, mn, entries);
}

void
maddrmap_free(struct maddrmap *maddrmap)
{
	struct maddrnode       *mn;

	while ((mn = TAILQ_FIRST(&maddrmap->queue))) {
		TAILQ_REMOVE(&maddrmap->queue, mn, entries);
		free(mn);
	}
	free(maddrmap);
}
