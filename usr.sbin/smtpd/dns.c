/*	$OpenBSD: dns.c,v 1.92 2023/11/16 10:23:21 op Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
 * Copyright (c) 2011-2014 Eric Faurot <eric@faurot.net>
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

#include <sys/socket.h>

#include <netinet/in.h>

#include <asr.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"
#include "unpack_dns.h"

struct dns_lookup {
	struct dns_session	*session;
	char			*host;
	int			 preference;
};

struct dns_session {
	struct mproc		*p;
	uint64_t		 reqid;
	int			 type;
	char			 name[HOST_NAME_MAX+1];
	size_t			 mxfound;
	int			 error;
	int			 refcount;
};

static void dns_lookup_host(struct dns_session *, const char *, int);
static void dns_dispatch_host(struct asr_result *, void *);
static void dns_dispatch_mx(struct asr_result *, void *);
static void dns_dispatch_mx_preference(struct asr_result *, void *);

static int
domainname_is_addr(const char *s, struct sockaddr *sa, socklen_t *sl)
{
	struct addrinfo	hints, *res;
	socklen_t	sl2;
	size_t		l;
	char		buf[SMTPD_MAXDOMAINPARTSIZE];
	int		i6, error;

	if (*s != '[')
		return (0);

	i6 = (strncasecmp("[IPv6:", s, 6) == 0);
	s += i6 ? 6 : 1;

	l = strlcpy(buf, s, sizeof(buf));
	if (l >= sizeof(buf) || l == 0 || buf[l - 1] != ']')
		return (0);

	buf[l - 1] = '\0';
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_socktype = SOCK_STREAM;
	if (i6)
		hints.ai_family = AF_INET6;

	res = NULL;
	if ((error = getaddrinfo(buf, NULL, &hints, &res))) {
		log_warnx("getaddrinfo: %s", gai_strerror(error));
	}

	if (!res)
		return (0);

	if (sa && sl) {
		sl2 = *sl;
		if (sl2 > res->ai_addrlen)
			sl2 = res->ai_addrlen;
		memmove(sa, res->ai_addr, sl2);
		*sl = res->ai_addrlen;
	}

	freeaddrinfo(res);
	return (1);
}

void
dns_imsg(struct mproc *p, struct imsg *imsg)
{
	struct sockaddr_storage	 ss;
	struct dns_session	*s;
	struct sockaddr		*sa;
	struct asr_query	*as;
	struct msg		 m;
	const char		*domain, *mx, *host;
	socklen_t		 sl;

	s = xcalloc(1, sizeof *s);
	s->type = imsg->hdr.type;
	s->p = p;

	m_msg(&m, imsg);
	m_get_id(&m, &s->reqid);

	switch (s->type) {

	case IMSG_MTA_DNS_HOST:
		m_get_string(&m, &host);
		m_end(&m);
		dns_lookup_host(s, host, -1);
		return;

	case IMSG_MTA_DNS_MX:
		m_get_string(&m, &domain);
		m_end(&m);
		(void)strlcpy(s->name, domain, sizeof(s->name));

		sa = (struct sockaddr *)&ss;
		sl = sizeof(ss);

		if (domainname_is_addr(domain, sa, &sl)) {
			m_create(s->p, IMSG_MTA_DNS_HOST, 0, 0, -1);
			m_add_id(s->p, s->reqid);
			m_add_string(s->p, sockaddr_to_text(sa));
			m_add_sockaddr(s->p, sa);
			m_add_int(s->p, -1);
			m_close(s->p);

			m_create(s->p, IMSG_MTA_DNS_HOST_END, 0, 0, -1);
			m_add_id(s->p, s->reqid);
			m_add_int(s->p, DNS_OK);
			m_close(s->p);
			free(s);
			return;
		}

		as = res_query_async(s->name, C_IN, T_MX, NULL);
		if (as == NULL) {
			log_warn("warn: res_query_async: %s", s->name);
			m_create(s->p, IMSG_MTA_DNS_HOST_END, 0, 0, -1);
			m_add_id(s->p, s->reqid);
			m_add_int(s->p, DNS_EINVAL);
			m_close(s->p);
			free(s);
			return;
		}

		event_asr_run(as, dns_dispatch_mx, s);
		return;

	case IMSG_MTA_DNS_MX_PREFERENCE:
		m_get_string(&m, &domain);
		m_get_string(&m, &mx);
		m_end(&m);
		(void)strlcpy(s->name, mx, sizeof(s->name));

		as = res_query_async(domain, C_IN, T_MX, NULL);
		if (as == NULL) {
			m_create(s->p, IMSG_MTA_DNS_MX_PREFERENCE, 0, 0, -1);
			m_add_id(s->p, s->reqid);
			m_add_int(s->p, DNS_ENOTFOUND);
			m_close(s->p);
			free(s);
			return;
		}

		event_asr_run(as, dns_dispatch_mx_preference, s);
		return;

	default:
		log_warnx("warn: bad dns request %d", s->type);
		fatal(NULL);
	}
}

static void
dns_dispatch_host(struct asr_result *ar, void *arg)
{
	struct dns_session	*s;
	struct dns_lookup	*lookup = arg;
	struct addrinfo		*ai;

	s = lookup->session;

	for (ai = ar->ar_addrinfo; ai; ai = ai->ai_next) {
		s->mxfound++;
		m_create(s->p, IMSG_MTA_DNS_HOST, 0, 0, -1);
		m_add_id(s->p, s->reqid);
		m_add_string(s->p, lookup->host);
		m_add_sockaddr(s->p, ai->ai_addr);
		m_add_int(s->p, lookup->preference);
		m_close(s->p);
	}
	free(lookup->host);
	free(lookup);
	if (ar->ar_addrinfo)
		freeaddrinfo(ar->ar_addrinfo);

	if (ar->ar_gai_errno)
		s->error = ar->ar_gai_errno;

	if (--s->refcount)
		return;

	m_create(s->p, IMSG_MTA_DNS_HOST_END, 0, 0, -1);
	m_add_id(s->p, s->reqid);
	m_add_int(s->p, s->mxfound ? DNS_OK : DNS_ENOTFOUND);
	m_close(s->p);
	free(s);
}

static void
dns_dispatch_mx(struct asr_result *ar, void *arg)
{
	struct dns_session	*s = arg;
	struct unpack		 pack;
	struct dns_header	 h;
	struct dns_query	 q;
	struct dns_rr		 rr;
	char			 buf[512];
	size_t			 found;
	int			 nullmx = 0;

	if (ar->ar_h_errno && ar->ar_h_errno != NO_DATA &&
	    ar->ar_h_errno != NOTIMP) {
		m_create(s->p,  IMSG_MTA_DNS_HOST_END, 0, 0, -1);
		m_add_id(s->p, s->reqid);
		if (ar->ar_rcode == NXDOMAIN)
			m_add_int(s->p, DNS_ENONAME);
		else if (ar->ar_h_errno == NO_RECOVERY)
			m_add_int(s->p, DNS_EINVAL);
		else
			m_add_int(s->p, DNS_RETRY);
		m_close(s->p);
		free(s);
		free(ar->ar_data);
		return;
	}

	unpack_init(&pack, ar->ar_data, ar->ar_datalen);
	unpack_header(&pack, &h);
	unpack_query(&pack, &q);

	found = 0;
	for (; h.ancount; h.ancount--) {
		unpack_rr(&pack, &rr);
		if (rr.rr_type != T_MX)
			continue;

		print_dname(rr.rr.mx.exchange, buf, sizeof(buf));
		buf[strlen(buf) - 1] = '\0';

		if ((rr.rr.mx.preference == 0 && !strcmp(buf, "")) ||
		    !strcmp(buf, "localhost")) {
			nullmx = 1;
			continue;
		}

		dns_lookup_host(s, buf, rr.rr.mx.preference);
		found++;
	}
	free(ar->ar_data);

	if (nullmx && found == 0) {
		m_create(s->p, IMSG_MTA_DNS_HOST_END, 0, 0, -1);
		m_add_id(s->p, s->reqid);
		m_add_int(s->p, DNS_NULLMX);
		m_close(s->p);
		free(s);
		return;
	}

	/* fallback to host if no MX is found. */
	if (found == 0)
		dns_lookup_host(s, s->name, 0);
}

static void
dns_dispatch_mx_preference(struct asr_result *ar, void *arg)
{
	struct dns_session	*s = arg;
	struct unpack		 pack;
	struct dns_header	 h;
	struct dns_query	 q;
	struct dns_rr		 rr;
	char			 buf[512];
	int			 error;

	if (ar->ar_h_errno) {
		if (ar->ar_rcode == NXDOMAIN)
			error = DNS_ENONAME;
		else if (ar->ar_h_errno == NO_RECOVERY
		    || ar->ar_h_errno == NO_DATA)
			error = DNS_EINVAL;
		else
			error = DNS_RETRY;
	}
	else {
		error = DNS_ENOTFOUND;
		unpack_init(&pack, ar->ar_data, ar->ar_datalen);
		unpack_header(&pack, &h);
		unpack_query(&pack, &q);
		for (; h.ancount; h.ancount--) {
			unpack_rr(&pack, &rr);
			if (rr.rr_type != T_MX)
				continue;
			print_dname(rr.rr.mx.exchange, buf, sizeof(buf));
			buf[strlen(buf) - 1] = '\0';
			if (!strcasecmp(s->name, buf)) {
				error = DNS_OK;
				break;
			}
		}
	}

	free(ar->ar_data);

	m_create(s->p, IMSG_MTA_DNS_MX_PREFERENCE, 0, 0, -1);
	m_add_id(s->p, s->reqid);
	m_add_int(s->p, error);
	if (error == DNS_OK)
		m_add_int(s->p, rr.rr.mx.preference);
	m_close(s->p);
	free(s);
}

static void
dns_lookup_host(struct dns_session *s, const char *host, int preference)
{
	struct dns_lookup	*lookup;
	struct addrinfo		 hints;
	char			 hostcopy[HOST_NAME_MAX+1];
	char			*p;
	void			*as;

	lookup = xcalloc(1, sizeof *lookup);
	lookup->preference = preference;
	lookup->host = xstrdup(host);
	lookup->session = s;
	s->refcount++;

	if (*host == '[') {
		if (strncasecmp("[IPv6:", host, 6) == 0)
			host += 6;
		else
			host += 1;
		(void)strlcpy(hostcopy, host, sizeof hostcopy);
		p = strchr(hostcopy, ']');
		if (p)
			*p = 0;
		host = hostcopy;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	as = getaddrinfo_async(host, NULL, &hints, NULL);
	event_asr_run(as, dns_dispatch_host, lookup);
}
