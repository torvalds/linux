/*	$KAME: ip6addrctl.c,v 1.3 2003/12/16 08:14:28 suz Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2001 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>

#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <err.h>

static char *configfile;

struct policyqueue {
	TAILQ_ENTRY(policyqueue) pc_entry;
	struct in6_addrpolicy pc_policy;
};
TAILQ_HEAD(policyhead, policyqueue);
static struct policyhead policyhead;

static void usage(void);
static void get_policy(void);
static void dump_policy(void);
static int mask2plen(struct sockaddr_in6 *);
static int parse_prefix(const char *, struct in6_addrpolicy *);
static void make_policy_fromfile(char *);
static void plen2mask(struct sockaddr_in6 *, int);
static void set_policy(void);
static void add_policy(char *, char *, char *);
static void delete_policy(char *);
static void flush_policy(void);

int
main(int argc, char *argv[])
{
	TAILQ_INIT(&policyhead);

	if (argc == 1 || strcasecmp(argv[1], "show") == 0) {
		get_policy();
		dump_policy();
	} else if (strcasecmp(argv[1], "add") == 0) {
		if (argc < 5)
			usage();
		add_policy(argv[2], argv[3], argv[4]);
	} else if (strcasecmp(argv[1], "delete") == 0) {
		if (argc < 3)
			usage();
		delete_policy(argv[2]);
	} else if (strcasecmp(argv[1], "flush") == 0) {
		get_policy();
		flush_policy();
	} else if (strcasecmp(argv[1], "install") == 0) {
		if (argc < 3)
			usage();
		configfile = argv[2];
		make_policy_fromfile(configfile);
		set_policy();
	} else
		usage();

	exit(0);
}

static void
get_policy(void)
{
	int mib[] = { CTL_NET, PF_INET6, IPPROTO_IPV6, IPV6CTL_ADDRCTLPOLICY };
	size_t l;
	struct in6_addrpolicy *buf;
	struct in6_addrpolicy *pol, *ep;

	if (sysctl(mib, nitems(mib), NULL, &l, NULL, 0) < 0) {
		err(1, "sysctl(IPV6CTL_ADDRCTLPOLICY)");
		/* NOTREACHED */
	}
	if (l == 0) {
		printf("no source-address-selection policy is installed\n");
		return;
	}
	if ((buf = malloc(l)) == NULL) {
		errx(1, "malloc failed");
		/* NOTREACHED */
	}
	if (sysctl(mib, nitems(mib), buf, &l, NULL, 0) < 0) {
		err(1, "sysctl(IPV6CTL_ADDRCTLPOLICY)");
		/* NOTREACHED */
	}

	ep = buf + l/sizeof(*buf);
	for (pol = buf; pol + 1 <= ep; pol++) {
		struct policyqueue *new;

		if ((new = malloc(sizeof(*new))) == NULL)
			errx(1, "malloc failed\n");
		new->pc_policy = *pol;
		TAILQ_INSERT_TAIL(&policyhead, new, pc_entry);
	}

	free(buf);
}

static void
dump_policy(void)
{
	size_t addrlen;
	char addrbuf[NI_MAXHOST];
	struct in6_addrpolicy *pol;
	struct policyqueue *ent;
	int plen, first = 1;

	for (ent = TAILQ_FIRST(&policyhead); ent;
	     ent = TAILQ_NEXT(ent, pc_entry)) {
		pol = &ent->pc_policy;
		if (first) {
			printf("%-30s %5s %5s %8s\n",
			       "Prefix", "Prec", "Label", "Use");
			first = 0;
		}

		if ((getnameinfo((struct sockaddr *)&pol->addr,
				 sizeof(pol->addr), addrbuf, sizeof(addrbuf),
				 NULL, 0, NI_NUMERICHOST))) {
			warnx("getnameinfo for prefix address failed");
			continue;
		}
		if ((plen = mask2plen(&pol->addrmask)) < 0) {
			warnx("invalid address mask");
			continue;
		}
		addrlen = strlen(addrbuf);
		if (addrlen + sizeof("/128") < sizeof(addrbuf)) {
			snprintf(&addrbuf[addrlen],
				 sizeof(addrbuf) - addrlen - 1,
				 "/%d", plen);
			printf("%-30s", addrbuf);
		} else		/* XXX */
			printf("%s/%d", addrbuf, plen);
		printf(" %5d %5d %8llu\n", pol->preced, pol->label,
		    (unsigned long long)pol->use);
	}
}

#define SKIP_WHITE(p, emptyok) \
	do { \
		while((*(p) == ' ' || *(p) == '\t')) \
			(p)++; \
 		if ((*(p) == '\0' || (*(p) == '\n')) && !(emptyok)) \
			goto bad; \
	} while (0);
#define SKIP_WORD(p) \
	do { \
		while(*(p) != ' ' && *(p) != '\t') \
			(p)++; \
 		if (*(p) == '\0' || *(p) == '\n') \
			goto bad; \
	} while (0);

static void
make_policy_fromfile(char *conf)
{
	char line[_POSIX2_LINE_MAX], *cp;
	char *addrstr;
	FILE *fp;
	int count = 0;
	struct in6_addrpolicy pol0;
	struct policyqueue *new;

	if ((fp = fopen(conf, "r")) == NULL)
		err(1, "fopen: %s", conf);

	while(fgets(line, sizeof(line), fp)) {
		count++;
		cp = line;

		memset(&pol0, 0, sizeof(pol0));

		/* get prefix */
		SKIP_WHITE(cp, 1);
		if (*cp == '\n') /* empty line */
			continue;
		if (*cp == '#')
			continue;
		addrstr = cp;
		if (parse_prefix((const char *)addrstr, &pol0))
			goto bad;

		/* get precedence value */
		SKIP_WORD(cp);
		SKIP_WHITE(cp, 0);
		pol0.preced = atoi(cp);

		/* get label */
		SKIP_WORD(cp);
		SKIP_WHITE(cp, 0);
		pol0.label = atoi(cp);

		/* parse succeeded.  make a control buffer entry. */
		if ((new = malloc(sizeof(*new))) == NULL)
			errx(1, "malloc failed\n");
		memset(new, 0, sizeof(*new));
		new->pc_policy = pol0;
		TAILQ_INSERT_TAIL(&policyhead, new, pc_entry);
	}

	fclose(fp);
	return;

  bad:
	errx(1, "parse failed at line %d", count);
	/* NOTREACHED */
}

static int
parse_prefix(const char *prefix0, struct in6_addrpolicy *pol)
{
	int e = 0, plen;
	char *prefix, *plenstr;
	struct addrinfo hints, *res;

	if ((prefix = strdup(prefix0)) == NULL)
		errx(1, "strdup failed");

	if ((plenstr = strchr(prefix, '/')) == NULL) {
		e = -1;
		goto end;
	}
	*plenstr = '\0';

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST;
	hints.ai_family = AF_INET6;

	if ((e = getaddrinfo(prefix, NULL, &hints, &res)) != 0) {
		warnx("getaddrinfo failed for %s: %s", prefix,
		      gai_strerror(e));
		goto end;
	}
	memcpy(&pol->addr, res->ai_addr, res->ai_addrlen);
	freeaddrinfo(res);
	plen = atoi(plenstr + 1);
	if (plen < 0 || plen > 128) {
		warnx("invalid prefix length: %d", plen);
		e = -1;
		goto end;
	}
	plen2mask(&pol->addrmask, plen);

  end:
	free(prefix);
	return(e);
}

static void
plen2mask(struct sockaddr_in6 *mask, int plen)
{
	u_char *cp = (unsigned char *)&mask->sin6_addr;

	memset(mask, 0, sizeof(*mask));
	mask->sin6_family = AF_INET6; /* just in case */
	mask->sin6_len = sizeof(*mask);

	for(; plen >= 8; plen -= 8)
		*cp++ = 0xff;
	if (plen > 0)
		*cp = (0xff << (8 - plen));
}

static void
set_policy(void)
{
	struct policyqueue *ent;
	int s;

	if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err(1, "socket(UDP)");

	for (ent = TAILQ_FIRST(&policyhead); ent;
	     ent = TAILQ_NEXT(ent, pc_entry)) {
		if (ioctl(s, SIOCAADDRCTL_POLICY, &ent->pc_policy))
			warn("ioctl(SIOCAADDRCTL_POLICY)");
	}

	close(s);
}

static int
mask2plen(struct sockaddr_in6 *mask)
{
	int masklen, final = 0;
	u_char *p, *lim;

	masklen = 0;
	lim = (u_char *)(mask + 1);
	for (p = (u_char *)(&mask->sin6_addr); p < lim; p++) {
		if (final && *p) {
			goto bad;
		}

		switch (*p & 0xff) {
		case 0xff:
			masklen += 8;
			break;
		case 0xfe:
			masklen += 7;
			final++;
			break;
		case 0xfc:
			masklen += 6;
			final++;
			break;
		case 0xf8:
			masklen += 5;
			final++;
			break;
		case 0xf0:
			masklen += 4;
			final++;
			break;
		case 0xe0:
			masklen += 3;
			final++;
			break;
		case 0xc0:
			masklen += 2;
			final++;
			break;
		case 0x80:
			masklen += 1;
			final++;
			break;
		case 0x00:
			final++;
			break;
		default:
			goto bad;
			break;
		}
	}
	return(masklen);

  bad:
	return(-1);
}

static void
add_policy(char *prefix, char *prec, char *label)
{
	struct in6_addrpolicy p;
	int s;

	memset(&p, 0, sizeof(p));

	if (parse_prefix((const char *)prefix, &p))
		errx(1, "bad prefix: %s", prefix);
	p.preced = atoi(prec);
	p.label = atoi(label);

	if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err(1, "socket(UDP)");
	if (ioctl(s, SIOCAADDRCTL_POLICY, &p))
		err(1, "ioctl(SIOCAADDRCTL_POLICY)");

	close(s);
}

static void
delete_policy(char *prefix)
{
	struct in6_addrpolicy p;
	int s;

	memset(&p, 0, sizeof(p));

	if (parse_prefix((const char *)prefix, &p))
		errx(1, "bad prefix: %s", prefix);

	if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err(1, "socket(UDP)");
	if (ioctl(s, SIOCDADDRCTL_POLICY, &p))
		err(1, "ioctl(SIOCDADDRCTL_POLICY)");

	close(s);
}

static void
flush_policy(void)
{
	struct policyqueue *ent;
	int s;

	if ((s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err(1, "socket(UDP)");

	for (ent = TAILQ_FIRST(&policyhead); ent;
	     ent = TAILQ_NEXT(ent, pc_entry)) {
		if (ioctl(s, SIOCDADDRCTL_POLICY, &ent->pc_policy))
			warn("ioctl(SIOCDADDRCTL_POLICY)");
	}

	close(s);
}

static void
usage(void)
{
	fprintf(stderr, "usage: ip6addrctl [show]\n");
	fprintf(stderr, "       ip6addrctl add "
		"<prefix> <precedence> <label>\n");
	fprintf(stderr, "       ip6addrctl delete <prefix>\n");
	fprintf(stderr, "       ip6addrctl flush\n");
	fprintf(stderr, "       ip6addrctl install <configfile>\n");

	exit(1);
}
