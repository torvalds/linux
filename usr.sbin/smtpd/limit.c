/*	$OpenBSD: limit.c,v 1.7 2021/06/14 17:58:15 eric Exp $	*/

/*
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

#include <string.h>

#include "smtpd.h"

void
limit_mta_set_defaults(struct mta_limits *limits)
{
	limits->maxconn_per_host = 10;
	limits->maxconn_per_route = 5;
	limits->maxconn_per_source = 100;
	limits->maxconn_per_connector = 20;
	limits->maxconn_per_relay = 100;
	limits->maxconn_per_domain = 100;

	limits->conndelay_host = 0;
	limits->conndelay_route = 5;
	limits->conndelay_source = 0;
	limits->conndelay_connector = 0;
	limits->conndelay_relay = 2;
	limits->conndelay_domain = 0;

	limits->discdelay_route = 3;

	limits->max_mail_per_session = 100;
	limits->sessdelay_transaction = 0;
	limits->sessdelay_keepalive = 10;

	limits->max_failures_per_session = 25;

	limits->family = AF_UNSPEC;

	limits->task_hiwat = 50;
	limits->task_lowat = 30;
	limits->task_release = 10;
}

int
limit_mta_set(struct mta_limits *limits, const char *key, int64_t value)
{
	if (!strcmp(key, "max-conn-per-host"))
		limits->maxconn_per_host = value;
	else if (!strcmp(key, "max-conn-per-route"))
		limits->maxconn_per_route = value;
	else if (!strcmp(key, "max-conn-per-source"))
		limits->maxconn_per_source = value;
	else if (!strcmp(key, "max-conn-per-connector"))
		limits->maxconn_per_connector = value;
	else if (!strcmp(key, "max-conn-per-relay"))
		limits->maxconn_per_relay = value;
	else if (!strcmp(key, "max-conn-per-domain"))
		limits->maxconn_per_domain = value;

	else if (!strcmp(key, "conn-delay-host"))
		limits->conndelay_host = value;
	else if (!strcmp(key, "conn-delay-route"))
		limits->conndelay_route = value;
	else if (!strcmp(key, "conn-delay-source"))
		limits->conndelay_source = value;
	else if (!strcmp(key, "conn-delay-connector"))
		limits->conndelay_connector = value;
	else if (!strcmp(key, "conn-delay-relay"))
		limits->conndelay_relay = value;
	else if (!strcmp(key, "conn-delay-domain"))
		limits->conndelay_domain = value;

	else if (!strcmp(key, "reconn-delay-route"))
		limits->discdelay_route = value;

	else if (!strcmp(key, "session-mail-max"))
		limits->max_mail_per_session = value;
	else if (!strcmp(key, "session-transaction-delay"))
		limits->sessdelay_transaction = value;
	else if (!strcmp(key, "session-keepalive"))
		limits->sessdelay_keepalive = value;

	else if (!strcmp(key, "max-failures-per-session"))
		limits->max_failures_per_session = value;

	else if (!strcmp(key, "task-hiwat"))
		limits->task_hiwat = value;
	else if (!strcmp(key, "task-lowat"))
		limits->task_lowat = value;
	else if (!strcmp(key, "task-release"))
		limits->task_release = value;

	else
		return (0);

	return (1);
}
