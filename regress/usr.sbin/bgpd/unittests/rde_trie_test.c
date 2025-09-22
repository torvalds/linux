/*	$OpenBSD: rde_trie_test.c,v 1.15 2025/03/24 10:10:27 claudio Exp $ */

/*
 * Copyright (c) 2018 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <err.h>
#include <limits.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <util.h>

#include "bgpd.h"
#include "rde.h"

struct rde_memstats rdemem;

int roa;
int orlonger;

int
host_ip(const char *s, struct bgpd_addr *h, uint8_t *len)
{
	struct addrinfo	hints, *res;
	int		bits;

	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(s, NULL, &hints, &res) == 0) {
		*len = res->ai_family == AF_INET6 ? 128 : 32;
		sa2addr(res->ai_addr, h, NULL);
		freeaddrinfo(res);
	} else {        /* ie. for 10/8 parsing */
		if ((bits = inet_net_pton(AF_INET, s, &h->v4, sizeof(h->v4))) == -1)
			return (0);
		*len = bits;
		h->aid = AID_INET;
	}

	return (1);
}

int
host(const char *s, struct bgpd_addr *h, uint8_t *len)
{
	int		 mask = 128;
	char		*p, *ps;
	const char	*errstr;

	if ((ps = strdup(s)) == NULL)
		errx(1, "%s: strdup", __func__);

	if ((p = strrchr(ps, '/')) != NULL) {
		mask = strtonum(p+1, 0, 128, &errstr);
		if (errstr) {
			warnx("prefixlen is %s: %s", errstr, p+1);
			return (0);
		}
		p[0] = '\0';
	}

	bzero(h, sizeof(*h));

	if (host_ip(ps, h, len) == 0) {
		free(ps);
		return (0);
	}

	if (p != NULL)
		*len = mask;

	free(ps);
	return (1);
}


static const char *
print_prefix(struct bgpd_addr *p)
{
	static char buf[48];

	if (p->aid == AID_INET) {
		if (inet_ntop(AF_INET, &p->v4, buf, sizeof(buf)) == NULL)
			return "?";
	} else if (p->aid == AID_INET6) {
		if (inet_ntop(AF_INET6, &p->v6, buf, sizeof(buf)) == NULL)
			return "?";
	} else {
		return "???";
	}
	return buf;
}

static void
parse_file(FILE *in, struct trie_head *th)
{
	const char *errstr;
	char *line, *s;
	struct bgpd_addr prefix;
	uint8_t plen;

	while ((line = fparseln(in, NULL, NULL, NULL, FPARSELN_UNESCALL))) {
		int state = 0;
		uint8_t min = 255, max = 255, maskmax = 0;

		while ((s = strsep(&line, " \t\n"))) {
			if (*s == '\0')
				continue;
			switch (state) {
			case 0:
				if (!host(s, &prefix, &plen))
					errx(1, "%s: could not parse "
					    "prefix \"%s\"", __func__, s);
				if (prefix.aid == AID_INET6)
					maskmax = 128;
				else
					maskmax = 32;
				break;
			case 1:
				min = strtonum(s, 0, maskmax, &errstr);
				if (errstr != NULL)
					errx(1, "min is %s: %s", errstr, s);
				break;
			case 2:
				max = strtonum(s, 0, maskmax, &errstr);
				if (errstr != NULL)
					errx(1, "max is %s: %s", errstr, s);
				break;
			default:
				errx(1, "could not parse \"%s\", confused", s);
			}
			state++;
		}
		if (state == 0)
			continue;
		if (max == 255)
			max = maskmax;
		if (min == 255)
			min = plen;

		if (trie_add(th, &prefix, plen, min, max) != 0)
			errx(1, "trie_add(%s, %u, %u, %u) failed",
			    print_prefix(&prefix), plen, min, max);

		free(line);
	}
}

static void
parse_roa_file(FILE *in, struct trie_head *th)
{
	const char *errstr;
	char *line, *s;
	struct set_table *set = NULL;
	struct roa roa;
	struct bgpd_addr prefix;
	uint8_t plen;

	while ((line = fparseln(in, NULL, NULL, NULL, FPARSELN_UNESCALL))) {
		int state = 0;
		uint32_t as;
		uint8_t max = 0;

		while ((s = strsep(&line, " \t\n"))) {
			if (*s == '\0')
				continue;
			if (strcmp(s, "source-as") == 0) {
				state = 4;
				continue;
			}
			if (strcmp(s, "maxlen") == 0) {
				state = 2;
				continue;
			}
			if (strcmp(s, "prefix") == 0) {
				state = 0;
				continue;
			}
			switch (state) {
			case 0:
				if (!host(s, &prefix, &plen))
					errx(1, "%s: could not parse "
					    "prefix \"%s\"", __func__, s);
				break;
			case 2:
				max = strtonum(s, 0, 128, &errstr);
				if (errstr != NULL)
					errx(1, "max is %s: %s", errstr, s);
				break;
			case 4:
				as = strtonum(s, 0, UINT_MAX, &errstr);
				if (errstr != NULL)
					errx(1, "source-as is %s: %s", errstr,
					    s);
				break;
			default:
				errx(1, "could not parse \"%s\", confused", s);
			}
		}

		roa.aid = prefix.aid;
		roa.prefix.inet6 = prefix.v6;
		roa.prefixlen = plen;
		roa.maxlen = max;
		roa.asnum = as;
		if (trie_roa_add(th, &roa) != 0)
			errx(1, "trie_roa_add(%s, %u) failed",
			    print_prefix(&prefix), plen);

		free(line);
	}
}

static void
test_file(FILE *in, struct trie_head *th)
{
	char *line;
	struct bgpd_addr prefix;
	uint8_t plen;

	while ((line = fparseln(in, NULL, NULL, NULL, FPARSELN_UNESCALL))) {
		if (!host(line, &prefix, &plen))
			errx(1, "%s: could not parse prefix \"%s\"",
			    __func__, line);
		printf("%s/%u ", print_prefix(&prefix), plen);
		if (trie_match(th, &prefix, plen, orlonger))
			printf("MATCH\n");
		else
			printf("miss\n");
		free(line);
	}
}

static void
test_roa_file(FILE *in, struct trie_head *th)
{
	const char *errstr;
	char *line, *s;
	struct bgpd_addr prefix;
	uint8_t plen;
	uint32_t as;
	int r;

	while ((line = fparseln(in, NULL, NULL, NULL, FPARSELN_UNESCALL))) {
		s = strchr(line, ' ');
		if (s)
			*s++ = '\0';
		if (!host(line, &prefix, &plen))
			errx(1, "%s: could not parse prefix \"%s\"",
			    __func__, line);
		if (s)
			s = strstr(s, "source-as");
		if (s) {
			s += strlen("source-as");
			as = strtonum(s, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "source-as is %s: %s", errstr, s);
		} else
			as = 0;
		printf("%s/%u source-as %u is ",
		    print_prefix(&prefix), plen, as);
		r = trie_roa_check(th, &prefix, plen, as);
		switch (r) {
		case ROA_NOTFOUND:
			printf("not found\n");
			break;
		case ROA_VALID:
			printf("VALID\n");
			break;
		case ROA_INVALID:
			printf("invalid\n");
			break;
		default:
			printf("UNEXPECTED %d\n", r);
			break;
		}
		free(line);
	}
}

static void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s [-or] prefixfile testfile\n", __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	struct trie_head th = { 0 };
	FILE *in, *tin;
	int ch;

	while ((ch = getopt(argc, argv, "or")) != -1) {
		switch (ch) {
		case 'o':
			orlonger = 1;
			break;
		case 'r':
			roa = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	in = fopen(argv[0], "r");
	if (in == NULL)
		err(1, "fopen(%s)", argv[0]);
	tin = fopen(argv[1], "r");
	if (tin == NULL)
		err(1, "fopen(%s)", argv[1]);

	if (roa)
		parse_roa_file(in, &th);
	else
		parse_file(in, &th);
	/* trie_dump(&th); */
	if (trie_equal(&th, &th) == 0)
		errx(1, "trie_equal failure");
	if (roa)
		test_roa_file(tin, &th);
	else
		test_file(tin, &th);

	trie_free(&th);
}
