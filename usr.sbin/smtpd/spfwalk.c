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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/tree.h>

#include <netinet/in.h>

#include <arpa/inet.h>
#include <asr.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <event.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "smtpd-defines.h"
#include "smtpd-api.h"
#include "unpack_dns.h"
#include "parser.h"

struct target {
	void	(*dispatch)(struct dns_rr *, struct target *);
	int	  cidr4;
	int	  cidr6;
};

int spfwalk(int, struct parameter *);

static void	dispatch_txt(struct dns_rr *, struct target *);
static void	dispatch_mx(struct dns_rr *, struct target *);
static void	dispatch_a(struct dns_rr *, struct target *);
static void	dispatch_aaaa(struct dns_rr *, struct target *);
static void	lookup_record(int, char *, struct target *);
static void	dispatch_record(struct asr_result *, void *);
static ssize_t	parse_txt(const char *, size_t, char *, size_t);
static int	parse_target(char *, struct target *);
void *xmalloc(size_t size);

int     ip_v4 = 0;
int     ip_v6 = 0;
int     ip_both = 1;

struct dict seen;

int
spfwalk(int argc, struct parameter *argv)
{
	struct target	 tgt;
	const char	*ip_family = NULL;
	char		*line = NULL;
	size_t		 linesize = 0;
	ssize_t		 linelen;

	if (argv)
		ip_family = argv[0].u.u_str;

	if (ip_family) {
		if (strcmp(ip_family, "-4") == 0) {
			ip_both = 0;
			ip_v4 = 1;
		} else if (strcmp(ip_family, "-6") == 0) {
			ip_both = 0;
			ip_v6 = 1;
		} else
			errx(1, "invalid ip_family");
	}

	dict_init(&seen);
  	event_init();

	tgt.cidr4 = tgt.cidr6 = -1;
	tgt.dispatch = dispatch_txt;

	while ((linelen = getline(&line, &linesize, stdin)) != -1) {
		while (linelen-- > 0 && isspace((unsigned char)line[linelen]))
			line[linelen] = '\0';

		if (linelen > 0)
			lookup_record(T_TXT, line, &tgt);
	}

	free(line);

	if (pledge("dns stdio", NULL) == -1)
		err(1, "pledge");

  	event_dispatch();

	return 0;
}

void
lookup_record(int type, char *record, struct target *tgt)
{
	struct asr_query *as;
	struct target *ntgt;
	size_t i;

	if (strchr(record, '%') != NULL) {
		for (i = 0; record[i] != '\0'; i++) {
			if (!isprint(record[i]))
				record[i] = '?';
		}
		warnx("%s: %s contains macros and can't be resolved", __func__,
		    record);
		return;
	}
	as = res_query_async(record, C_IN, type, NULL);
	if (as == NULL)
		err(1, "res_query_async");
	ntgt = xmalloc(sizeof(*ntgt));
	*ntgt = *tgt;
	event_asr_run(as, dispatch_record, (void *)ntgt);
}

void
dispatch_record(struct asr_result *ar, void *arg)
{
	struct target *tgt = arg;
	struct unpack pack;
	struct dns_header h;
	struct dns_query q;
	struct dns_rr rr;

	/* best effort */
	if (ar->ar_h_errno && ar->ar_h_errno != NO_DATA)
		goto end;

	unpack_init(&pack, ar->ar_data, ar->ar_datalen);
	unpack_header(&pack, &h);
	unpack_query(&pack, &q);

	for (; h.ancount; h.ancount--) {
		unpack_rr(&pack, &rr);
		/**/
		tgt->dispatch(&rr, tgt);
	}
end:
	free(tgt);
}

void
dispatch_txt(struct dns_rr *rr, struct target *tgt)
{
	char buf[4096];
	char *argv[512];
	char buf2[512];
	struct target ltgt;
	struct in6_addr ina;
	char **ap = argv;
	char *in = buf;
	char *record, *end;
	ssize_t n;

	if (rr->rr_type != T_TXT)
		return;
	n = parse_txt(rr->rr.other.rdata, rr->rr.other.rdlen, buf, sizeof(buf));
	if (n == -1 || n == sizeof(buf))
		return;
	buf[n] = '\0';

	if (strncasecmp("v=spf1 ", buf, 7))
		return;

	while ((*ap = strsep(&in, " ")) != NULL) {
		if (strcasecmp(*ap, "v=spf1") == 0)
			continue;

		end = *ap + strlen(*ap)-1;
		if (*end == '.')
			*end = '\0';

		if (dict_set(&seen, *ap, &seen))
			continue;

		if (**ap == '-' || **ap == '~')
			continue;

		if (**ap == '+' || **ap == '?')
			(*ap)++;

		ltgt.cidr4 = ltgt.cidr6 = -1;

		if (strncasecmp("ip4:", *ap, 4) == 0) {
			if ((ip_v4 == 1 || ip_both == 1) &&
			    inet_net_pton(AF_INET, *(ap) + 4,
			    &ina, sizeof(ina)) != -1)
				printf("%s\n", *(ap) + 4);
			continue;
		}
		if (strncasecmp("ip6:", *ap, 4) == 0) {
			if ((ip_v6 == 1 || ip_both == 1) &&
			    inet_net_pton(AF_INET6, *(ap) + 4,
			    &ina, sizeof(ina)) != -1)
				printf("%s\n", *(ap) + 4);
			continue;
		}
		if (strcasecmp("a", *ap) == 0) {
			print_dname(rr->rr_dname, buf2, sizeof(buf2));
			buf2[strlen(buf2) - 1] = '\0';
			ltgt.dispatch = dispatch_a;
			lookup_record(T_A, buf2, &ltgt);
			ltgt.dispatch = dispatch_aaaa;
			lookup_record(T_AAAA, buf2, &ltgt);
			continue;
		}
		if (strncasecmp("a:", *ap, 2) == 0) {
			record = *(ap) + 2;
			if (parse_target(record, &ltgt) < 0)
				continue;
			ltgt.dispatch = dispatch_a;
			lookup_record(T_A, record, &ltgt);
			ltgt.dispatch = dispatch_aaaa;
			lookup_record(T_AAAA, record, &ltgt);
			continue;
		}
		if (strncasecmp("exists:", *ap, 7) == 0) {
			ltgt.dispatch = dispatch_a;
			lookup_record(T_A, *(ap) + 7, &ltgt);
			continue;
		}
		if (strncasecmp("include:", *ap, 8) == 0) {
			ltgt.dispatch = dispatch_txt;
			lookup_record(T_TXT, *(ap) + 8, &ltgt);
			continue;
		}
		if (strncasecmp("redirect=", *ap, 9) == 0) {
			ltgt.dispatch = dispatch_txt;
			lookup_record(T_TXT, *(ap) + 9, &ltgt);
			continue;
		}
		if (strcasecmp("mx", *ap) == 0) {
			print_dname(rr->rr_dname, buf2, sizeof(buf2));
			buf2[strlen(buf2) - 1] = '\0';
			ltgt.dispatch = dispatch_mx;
			lookup_record(T_MX, buf2, &ltgt);
			continue;
		}
		if (strncasecmp("mx:", *ap, 3) == 0) {
			record = *(ap) + 3;
			if (parse_target(record, &ltgt) < 0)
				continue;
			ltgt.dispatch = dispatch_mx;
			lookup_record(T_MX, record, &ltgt);
			continue;
		}
	}
	*ap = NULL;
}

void
dispatch_mx(struct dns_rr *rr, struct target *tgt)
{
	char buf[512];
	struct target ltgt;

	if (rr->rr_type != T_MX)
		return;

	print_dname(rr->rr.mx.exchange, buf, sizeof(buf));
	buf[strlen(buf) - 1] = '\0';
	if (buf[strlen(buf) - 1] == '.')
		buf[strlen(buf) - 1] = '\0';

	ltgt = *tgt;
	ltgt.dispatch = dispatch_a;
	lookup_record(T_A, buf, &ltgt);
	ltgt.dispatch = dispatch_aaaa;
	lookup_record(T_AAAA, buf, &ltgt);
}

void
dispatch_a(struct dns_rr *rr, struct target *tgt)
{
	char buffer[512];
	const char *ptr;

	if (rr->rr_type != T_A)
		return;

	if ((ptr = inet_ntop(AF_INET, &rr->rr.in_a.addr,
	    buffer, sizeof buffer))) {
		if (tgt->cidr4 >= 0)
			printf("%s/%d\n", ptr, tgt->cidr4);
		else
			printf("%s\n", ptr);
	}
}

void
dispatch_aaaa(struct dns_rr *rr, struct target *tgt)
{
	char buffer[512];
	const char *ptr;

	if (rr->rr_type != T_AAAA)
		return;

	if ((ptr = inet_ntop(AF_INET6, &rr->rr.in_aaaa.addr6,
	    buffer, sizeof buffer))) {
		if (tgt->cidr6 >= 0)
			printf("%s/%d\n", ptr, tgt->cidr6);
		else
			printf("%s\n", ptr);
	}
}

ssize_t
parse_txt(const char *rdata, size_t rdatalen, char *dst, size_t dstsz)
{
	size_t len;
	ssize_t r = 0;

	while (rdatalen) {
		len = *(const unsigned char *)rdata;
		if (len >= rdatalen) {
			errno = EINVAL;
			return -1;
		}

		rdata++;
		rdatalen--;

		if (len == 0)
			continue;

		if (len >= dstsz) {
			errno = EOVERFLOW;
			return -1;
		}
		memmove(dst, rdata, len);
		dst += len;
		dstsz -= len;

		rdata += len;
		rdatalen -= len;
		r += len;
	}

	return r;
}

int
parse_target(char *record, struct target *tgt)
{
	const char *err;
	char *m4, *m6;

	m4 = record;
	strsep(&m4, "/");
	if (m4 == NULL)
		return 0;

	m6 = m4;
	strsep(&m6, "/");

	if (*m4) {
		tgt->cidr4 = strtonum(m4, 0, 32, &err);
		if (err)
			return tgt->cidr4 = -1;
	}

	if (m6 == NULL)
		return 0;

	tgt->cidr6 = strtonum(m6, 0, 128, &err);
	if (err)
		return tgt->cidr6 = -1;

	return 0;
}
