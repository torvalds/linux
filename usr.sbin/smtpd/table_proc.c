/*	$OpenBSD: table_proc.c,v 1.23 2024/05/28 07:10:30 op Exp $	*/

/*
 * Copyright (c) 2024 Omar Polo <op@openbsd.org>
 * Copyright (c) 2013 Eric Faurot <eric@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

#define PROTOCOL_VERSION	"0.1"

struct table_proc_priv {
	FILE		*in;
	FILE		*out;
	char		*line;
	size_t		 linesize;

	/*
	 * The last ID used in a request.  At the moment the protocol
	 * is synchronous from our point of view, so it's used to
	 * assert that the table replied with the correct ID.
	 */
	char		 lastid[16];
};

static char *
table_proc_nextid(struct table *table)
{
	struct table_proc_priv	*priv = table->t_handle;
	int			 r;

	r = snprintf(priv->lastid, sizeof(priv->lastid), "%08x", arc4random());
	if (r < 0 || (size_t)r >= sizeof(priv->lastid))
		fatal("table-proc: snprintf");

	return (priv->lastid);
}

static void
table_proc_send(struct table *table, const char *type, int service,
    const char *param)
{
	struct table_proc_priv	*priv = table->t_handle;
	struct timeval		 tv;

	gettimeofday(&tv, NULL);
	fprintf(priv->out, "table|%s|%lld.%06ld|%s|%s",
	    PROTOCOL_VERSION, (long long)tv.tv_sec, (long)tv.tv_usec,
	    table->t_name, type);
	if (service != -1) {
		fprintf(priv->out, "|%s|%s", table_service_name(service),
		    table_proc_nextid(table));
		if (param)
			fprintf(priv->out, "|%s", param);
		fputc('\n', priv->out);
	} else
		fprintf(priv->out, "|%s\n", table_proc_nextid(table));

	if (fflush(priv->out) == EOF)
		fatal("table-proc: fflush");
}

static const char *
table_proc_recv(struct table *table, const char *type)
{
	struct table_proc_priv	*priv = table->t_handle;
	const char		*l;
	ssize_t			 linelen;
	size_t			 len;

	if ((linelen = getline(&priv->line, &priv->linesize, priv->in)) == -1)
		fatal("table-proc: getline");
	priv->line[strcspn(priv->line, "\n")] = '\0';
	l = priv->line;

	len = strlen(type);
	if (strncmp(l, type, len) != 0)
		goto err;
	l += len;

	if (*l != '|')
		goto err;
	l++;

	len = strlen(priv->lastid);
	if (strncmp(l, priv->lastid, len) != 0)
		goto err;
	l += len;

	if (*l != '|')
		goto err;
	return (++l);

 err:
	log_warnx("warn: table-proc: failed to parse reply");
	fatalx("table-proc: exiting");
}

/*
 * API
 */

static int
table_proc_open(struct table *table)
{
	struct table_proc_priv	*priv;
	const char		*s;
	ssize_t			 len;
	int			 service, services = 0;
	int			 fd, fdd;

	priv = xcalloc(1, sizeof(*priv));

	fd = fork_proc_backend("table", table->t_config, table->t_name, 1);
	if (fd == -1)
		fatalx("table-proc: exiting");
	if ((fdd = dup(fd)) == -1) {
		log_warnx("warn: table-proc: dup");
		fatalx("table-proc: exiting");
	}
	if ((priv->in = fdopen(fd, "r")) == NULL)
		fatalx("table-proc: fdopen");
	if ((priv->out = fdopen(fdd, "w")) == NULL)
		fatalx("table-proc: fdopen");

	fprintf(priv->out, "config|smtpd-version|"SMTPD_VERSION"\n");
	fprintf(priv->out, "config|protocol|"PROTOCOL_VERSION"\n");
	fprintf(priv->out, "config|tablename|%s\n", table->t_name);
	fprintf(priv->out, "config|ready\n");
	if (fflush(priv->out) == EOF)
		fatalx("table-proc: fflush");

	while ((len = getline(&priv->line, &priv->linesize, priv->in)) != -1) {
		priv->line[strcspn(priv->line, "\n")] = '\0';

		if (strncmp(priv->line, "register|", 9) != 0)
			fatalx("table-proc: invalid handshake reply");

		s = priv->line + 9;
		if (!strcmp(s, "ready"))
			break;
		service = table_service_from_name(s);
		if (service == -1 || service == K_NONE)
			fatalx("table-proc: unknown service %s", s);

		services |= service;
	}

	if (ferror(priv->in))
		fatal("table-proc: getline");
	if (feof(priv->in))
		fatalx("table-proc: unexpected EOF during handshake");
	if (services == 0)
		fatalx("table-proc: no services registered");

	table->t_services = services;
	table->t_handle = priv;

	return (1);
}

static int
table_proc_update(struct table *table)
{
	const char		*r;

	table_proc_send(table, "update", -1, NULL);
	r = table_proc_recv(table, "update-result");
	if (!strcmp(r, "ok"))
		return (1);

	if (!strncmp(r, "error", 5)) {
		if (r[5] == '|') {
			r += 6;
			log_warnx("warn: table-proc: %s update failed: %s",
			    table->t_name, r);
		}
		return (0);
	}

	log_warnx("warn: table-proc: failed parse reply");
	fatalx("table-proc: exiting");
}

static void
table_proc_close(struct table *table)
{
	struct table_proc_priv	*priv = table->t_handle;

	if (fclose(priv->in) == EOF)
		fatal("table-proc: fclose");
	if (fclose(priv->out) == EOF)
		fatal("table-proc: fclose");
	free(priv->line);
	free(priv);

	table->t_handle = NULL;
}

static int
table_proc_lookup(struct table *table, enum table_service s, const char *k, char **dst)
{
	const char		*req = "lookup", *res = "lookup-result";
	const char		*r;

	if (dst == NULL) {
		req = "check";
		res = "check-result";
	}

	table_proc_send(table, req, s, k);
	r = table_proc_recv(table, res);

	if (!strcmp(r, "not-found"))
		return (0);

	if (!strncmp(r, "error", 5)) {
		if (r[5] == '|') {
			r += 6;
			log_warnx("warn: table-proc: %s %s failed: %s",
			    table->t_name, req, r);
		}
		return (-1);
	}

	if (dst == NULL) {
		/* check op */
		if (!strncmp(r, "found", 5))
			return (1);
		log_warnx("warn: table-proc: failed to parse reply");
		fatalx("table-proc: exiting");
	}

	/* lookup op */
	if (strncmp(r, "found|", 6) != 0) {
		log_warnx("warn: table-proc: failed to parse reply");
		fatalx("table-proc: exiting");
	}
	r += 6;
	if (*r == '\0') {
		log_warnx("warn: table-proc: empty response");
		fatalx("table-proc: exiting");
	}
	if ((*dst = strdup(r)) == NULL)
		return (-1);
	return (1);
}

static int
table_proc_fetch(struct table *table, enum table_service s, char **dst)
{
	const char		*r;

	table_proc_send(table, "fetch", s, NULL);
	r = table_proc_recv(table, "fetch-result");

	if (!strcmp(r, "not-found"))
		return (0);

	if (!strncmp(r, "error", 5)) {
		if (r[5] == '|') {
			r += 6;
			log_warnx("warn: table-proc: %s fetch failed: %s",
			    table->t_name, r);
		}
		return (-1);
	}

	if (strncmp(r, "found|", 6) != 0) {
		log_warnx("warn: table-proc: failed to parse reply");
		fatalx("table-proc: exiting");
	}
	r += 6;
	if (*r == '\0') {
		log_warnx("warn: table-proc: empty response");
		fatalx("table-proc: exiting");
	}

	if ((*dst = strdup(r)) == NULL)
		return (-1);
	return (1);
}

struct table_backend table_backend_proc = {
	.name = "proc",
	.services = K_ANY,
	.config = NULL,
	.add = NULL,
	.dump = NULL,
	.open = table_proc_open,
	.update = table_proc_update,
	.close = table_proc_close,
	.lookup = table_proc_lookup,
	.fetch = table_proc_fetch,
};
