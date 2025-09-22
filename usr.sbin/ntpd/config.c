/*	$OpenBSD: config.c,v 1.33 2020/04/12 14:20:56 otto Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <resolv.h>
#include <unistd.h>

#include "ntpd.h"

struct ntp_addr	*host_ip(const char *);
int		 host_dns1(const char *, struct ntp_addr **, int);

static u_int32_t		 maxid = 0;
static u_int32_t		 constraint_maxid = 0;
int				 non_numeric;

void
host(const char *s, struct ntp_addr **hn)
{
	struct ntp_addr		*h;

	if (!strcmp(s, "*")) {
		if ((h = calloc(1, sizeof(*h))) == NULL)
			fatal(NULL);
	} else {
		if ((h = host_ip(s)) == NULL) {
			non_numeric = 1;
			return;
		}
	}

	*hn = h;
}

struct ntp_addr	*
host_ip(const char *s)
{
	struct addrinfo		 hints, *res;
	struct ntp_addr		*h = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, "0", &hints, &res) == 0) {
		if (res->ai_family == AF_INET ||
		    res->ai_family == AF_INET6) {
			if ((h = calloc(1, sizeof(*h))) == NULL)
				fatal(NULL);
			memcpy(&h->ss, res->ai_addr, res->ai_addrlen);
		}
		freeaddrinfo(res);
	}

	return (h);
}

void
host_dns_free(struct ntp_addr *hn)
{
	struct ntp_addr	*h = hn, *tmp;
	while (h) {
		tmp = h;
		h = h->next;
		free(tmp);
	}
}

int
host_dns1(const char *s, struct ntp_addr **hn, int notauth)
{
	struct addrinfo		 hints, *res0, *res;
	int			 error, cnt = 0;
	struct ntp_addr		*h, *hh = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* DUMMY */
	hints.ai_flags = AI_ADDRCONFIG;
	error = getaddrinfo(s, NULL, &hints, &res0);
	if (error == EAI_AGAIN || error == EAI_NODATA || error == EAI_NONAME)
			return (0);
	if (error) {
		log_warnx("could not parse \"%s\": %s", s,
		    gai_strerror(error));
		return (-1);
	}

	for (res = res0; res && cnt < MAX_SERVERS_DNS; res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;
		if ((h = calloc(1, sizeof(*h))) == NULL)
			fatal(NULL);
		memcpy(&h->ss, res->ai_addr, res->ai_addrlen);
		h->notauth = notauth;

		h->next = hh;
		hh = h;
		cnt++;
	}
	freeaddrinfo(res0);

	*hn = hh;
	return (cnt);
}

int
host_dns(const char *s, int synced, struct ntp_addr **hn)
{
	int error, save_opts;
	
	log_debug("trying to resolve %s", s);
	error = host_dns1(s, hn, 0);
	if (!synced && error <= 0) {
		log_debug("no luck, trying to resolve %s without checking", s);
		save_opts = _res.options;
		_res.options |= RES_USE_CD;
		error = host_dns1(s, hn, 1);
		_res.options = save_opts;
	}
	log_debug("resolve %s done: %d", s, error);
	return error;
}

struct ntp_peer *
new_peer(void)
{
	struct ntp_peer	*p;

	if ((p = calloc(1, sizeof(struct ntp_peer))) == NULL)
		fatal("new_peer calloc");
	p->id = ++maxid;

	return (p);
}

struct ntp_conf_sensor *
new_sensor(char *device)
{
	struct ntp_conf_sensor	*s;

	if ((s = calloc(1, sizeof(struct ntp_conf_sensor))) == NULL)
		fatal("new_sensor calloc");
	if ((s->device = strdup(device)) == NULL)
		fatal("new_sensor strdup");

	return (s);
}

struct constraint *
new_constraint(void)
{
	struct constraint	*p;

	if ((p = calloc(1, sizeof(struct constraint))) == NULL)
		fatal("new_constraint calloc");
	p->id = ++constraint_maxid;
	p->fd = -1;

	return (p);
}

