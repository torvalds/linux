/*	$OpenBSD: pfctl_table.c,v 1.67 2008/06/10 20:55:02 mcbride Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2002 Cedric Berger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>
#include <arpa/inet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "pfctl_parser.h"
#include "pfctl.h"

extern void	usage(void);
static int	pfctl_table(int, char *[], char *, const char *, char *,
		    const char *, int);
static void	print_table(struct pfr_table *, int, int);
static void	print_tstats(struct pfr_tstats *, int);
static int	load_addr(struct pfr_buffer *, int, char *[], char *, int);
static void	print_addrx(struct pfr_addr *, struct pfr_addr *, int);
static void	print_astats(struct pfr_astats *, int);
static void	radix_perror(void);
static void	xprintf(int, const char *, ...);
static void	print_iface(struct pfi_kif *, int);

static const char	*stats_text[PFR_DIR_MAX][PFR_OP_TABLE_MAX] = {
	{ "In/Block:",	"In/Pass:",	"In/XPass:" },
	{ "Out/Block:",	"Out/Pass:",	"Out/XPass:" }
};

static const char	*istats_text[2][2][2] = {
	{ { "In4/Pass:", "In4/Block:" }, { "Out4/Pass:", "Out4/Block:" } },
	{ { "In6/Pass:", "In6/Block:" }, { "Out6/Pass:", "Out6/Block:" } }
};

#define RVTEST(fct) do {				\
		if ((!(opts & PF_OPT_NOACTION) ||	\
		    (opts & PF_OPT_DUMMYACTION)) &&	\
		    (fct)) {				\
			radix_perror();			\
			goto _error;			\
		}					\
	} while (0)

#define CREATE_TABLE do {						\
		table.pfrt_flags |= PFR_TFLAG_PERSIST;			\
		if ((!(opts & PF_OPT_NOACTION) ||			\
		    (opts & PF_OPT_DUMMYACTION)) &&			\
		    (pfr_add_tables(&table, 1, &nadd, flags)) &&	\
		    (errno != EPERM)) {					\
			radix_perror();					\
			goto _error;					\
		}							\
		if (nadd) {						\
			warn_namespace_collision(table.pfrt_name);	\
			xprintf(opts, "%d table created", nadd);	\
			if (opts & PF_OPT_NOACTION)			\
				return (0);				\
		}							\
		table.pfrt_flags &= ~PFR_TFLAG_PERSIST;			\
	} while(0)

int
pfctl_clear_tables(const char *anchor, int opts)
{
	return pfctl_table(0, NULL, NULL, "-F", NULL, anchor, opts);
}

int
pfctl_show_tables(const char *anchor, int opts)
{
	return pfctl_table(0, NULL, NULL, "-s", NULL, anchor, opts);
}

int
pfctl_command_tables(int argc, char *argv[], char *tname,
    const char *command, char *file, const char *anchor, int opts)
{
	if (tname == NULL || command == NULL)
		usage();
	return pfctl_table(argc, argv, tname, command, file, anchor, opts);
}

int
pfctl_table(int argc, char *argv[], char *tname, const char *command,
    char *file, const char *anchor, int opts)
{
	struct pfr_table	 table;
	struct pfr_buffer	 b, b2;
	struct pfr_addr		*a, *a2;
	int			 nadd = 0, ndel = 0, nchange = 0, nzero = 0;
	int			 rv = 0, flags = 0, nmatch = 0;
	void			*p;

	if (command == NULL)
		usage();
	if (opts & PF_OPT_NOACTION)
		flags |= PFR_FLAG_DUMMY;

	bzero(&b, sizeof(b));
	bzero(&b2, sizeof(b2));
	bzero(&table, sizeof(table));
	if (tname != NULL) {
		if (strlen(tname) >= PF_TABLE_NAME_SIZE)
			usage();
		if (strlcpy(table.pfrt_name, tname,
		    sizeof(table.pfrt_name)) >= sizeof(table.pfrt_name))
			errx(1, "pfctl_table: strlcpy");
	}
	if (strlcpy(table.pfrt_anchor, anchor,
	    sizeof(table.pfrt_anchor)) >= sizeof(table.pfrt_anchor))
		errx(1, "pfctl_table: strlcpy");

	if (!strcmp(command, "-F")) {
		if (argc || file != NULL)
			usage();
		RVTEST(pfr_clr_tables(&table, &ndel, flags));
		xprintf(opts, "%d tables deleted", ndel);
	} else if (!strcmp(command, "-s")) {
		b.pfrb_type = (opts & PF_OPT_VERBOSE2) ?
		    PFRB_TSTATS : PFRB_TABLES;
		if (argc || file != NULL)
			usage();
		for (;;) {
			pfr_buf_grow(&b, b.pfrb_size);
			b.pfrb_size = b.pfrb_msize;
			if (opts & PF_OPT_VERBOSE2)
				RVTEST(pfr_get_tstats(&table,
				    b.pfrb_caddr, &b.pfrb_size, flags));
			else
				RVTEST(pfr_get_tables(&table,
				    b.pfrb_caddr, &b.pfrb_size, flags));
			if (b.pfrb_size <= b.pfrb_msize)
				break;
		}

		if ((opts & PF_OPT_SHOWALL) && b.pfrb_size > 0)
			pfctl_print_title("TABLES:");

		PFRB_FOREACH(p, &b)
			if (opts & PF_OPT_VERBOSE2)
				print_tstats(p, opts & PF_OPT_DEBUG);
			else
				print_table(p, opts & PF_OPT_VERBOSE,
				    opts & PF_OPT_DEBUG);
	} else if (!strcmp(command, "kill")) {
		if (argc || file != NULL)
			usage();
		RVTEST(pfr_del_tables(&table, 1, &ndel, flags));
		xprintf(opts, "%d table deleted", ndel);
	} else if (!strcmp(command, "flush")) {
		if (argc || file != NULL)
			usage();
		RVTEST(pfr_clr_addrs(&table, &ndel, flags));
		xprintf(opts, "%d addresses deleted", ndel);
	} else if (!strcmp(command, "add")) {
		b.pfrb_type = PFRB_ADDRS;
		if (load_addr(&b, argc, argv, file, 0))
			goto _error;
		CREATE_TABLE;
		if (opts & PF_OPT_VERBOSE)
			flags |= PFR_FLAG_FEEDBACK;
		RVTEST(pfr_add_addrs(&table, b.pfrb_caddr, b.pfrb_size,
		    &nadd, flags));
		xprintf(opts, "%d/%d addresses added", nadd, b.pfrb_size);
		if (opts & PF_OPT_VERBOSE)
			PFRB_FOREACH(a, &b)
				if ((opts & PF_OPT_VERBOSE2) || a->pfra_fback)
					print_addrx(a, NULL,
					    opts & PF_OPT_USEDNS);
	} else if (!strcmp(command, "delete")) {
		b.pfrb_type = PFRB_ADDRS;
		if (load_addr(&b, argc, argv, file, 0))
			goto _error;
		if (opts & PF_OPT_VERBOSE)
			flags |= PFR_FLAG_FEEDBACK;
		RVTEST(pfr_del_addrs(&table, b.pfrb_caddr, b.pfrb_size,
		    &ndel, flags));
		xprintf(opts, "%d/%d addresses deleted", ndel, b.pfrb_size);
		if (opts & PF_OPT_VERBOSE)
			PFRB_FOREACH(a, &b)
				if ((opts & PF_OPT_VERBOSE2) || a->pfra_fback)
					print_addrx(a, NULL,
					    opts & PF_OPT_USEDNS);
	} else if (!strcmp(command, "replace")) {
		b.pfrb_type = PFRB_ADDRS;
		if (load_addr(&b, argc, argv, file, 0))
			goto _error;
		CREATE_TABLE;
		if (opts & PF_OPT_VERBOSE)
			flags |= PFR_FLAG_FEEDBACK;
		for (;;) {
			int sz2 = b.pfrb_msize;

			RVTEST(pfr_set_addrs(&table, b.pfrb_caddr, b.pfrb_size,
			    &sz2, &nadd, &ndel, &nchange, flags));
			if (sz2 <= b.pfrb_msize) {
				b.pfrb_size = sz2;
				break;
			} else
				pfr_buf_grow(&b, sz2);
		}
		if (nadd)
			xprintf(opts, "%d addresses added", nadd);
		if (ndel)
			xprintf(opts, "%d addresses deleted", ndel);
		if (nchange)
			xprintf(opts, "%d addresses changed", nchange);
		if (!nadd && !ndel && !nchange)
			xprintf(opts, "no changes");
		if (opts & PF_OPT_VERBOSE)
			PFRB_FOREACH(a, &b)
				if ((opts & PF_OPT_VERBOSE2) || a->pfra_fback)
					print_addrx(a, NULL,
					    opts & PF_OPT_USEDNS);
	} else if (!strcmp(command, "expire")) {
		const char		*errstr;
		u_int			 lifetime;

		b.pfrb_type = PFRB_ASTATS;
		b2.pfrb_type = PFRB_ADDRS;
		if (argc != 1 || file != NULL)
			usage();
		lifetime = strtonum(*argv, 0, UINT_MAX, &errstr);
		if (errstr)
			errx(1, "expiry time: %s", errstr);
		for (;;) {
			pfr_buf_grow(&b, b.pfrb_size);
			b.pfrb_size = b.pfrb_msize;
			RVTEST(pfr_get_astats(&table, b.pfrb_caddr,
			    &b.pfrb_size, flags));
			if (b.pfrb_size <= b.pfrb_msize)
				break;
		}
		PFRB_FOREACH(p, &b) {
			((struct pfr_astats *)p)->pfras_a.pfra_fback = 0;
			if (time(NULL) - ((struct pfr_astats *)p)->pfras_tzero >
			    lifetime)
				if (pfr_buf_add(&b2,
				    &((struct pfr_astats *)p)->pfras_a))
					err(1, "duplicate buffer");
		}

		if (opts & PF_OPT_VERBOSE)
			flags |= PFR_FLAG_FEEDBACK;
		RVTEST(pfr_del_addrs(&table, b2.pfrb_caddr, b2.pfrb_size,
		    &ndel, flags));
		xprintf(opts, "%d/%d addresses expired", ndel, b2.pfrb_size);
		if (opts & PF_OPT_VERBOSE)
			PFRB_FOREACH(a, &b2)
				if ((opts & PF_OPT_VERBOSE2) || a->pfra_fback)
					print_addrx(a, NULL,
					    opts & PF_OPT_USEDNS);
	} else if (!strcmp(command, "show")) {
		b.pfrb_type = (opts & PF_OPT_VERBOSE) ?
			PFRB_ASTATS : PFRB_ADDRS;
		if (argc || file != NULL)
			usage();
		for (;;) {
			pfr_buf_grow(&b, b.pfrb_size);
			b.pfrb_size = b.pfrb_msize;
			if (opts & PF_OPT_VERBOSE)
				RVTEST(pfr_get_astats(&table, b.pfrb_caddr,
				    &b.pfrb_size, flags));
			else
				RVTEST(pfr_get_addrs(&table, b.pfrb_caddr,
				    &b.pfrb_size, flags));
			if (b.pfrb_size <= b.pfrb_msize)
				break;
		}
		PFRB_FOREACH(p, &b)
			if (opts & PF_OPT_VERBOSE)
				print_astats(p, opts & PF_OPT_USEDNS);
			else
				print_addrx(p, NULL, opts & PF_OPT_USEDNS);
	} else if (!strcmp(command, "test")) {
		b.pfrb_type = PFRB_ADDRS;
		b2.pfrb_type = PFRB_ADDRS;

		if (load_addr(&b, argc, argv, file, 1))
			goto _error;
		if (opts & PF_OPT_VERBOSE2) {
			flags |= PFR_FLAG_REPLACE;
			PFRB_FOREACH(a, &b)
				if (pfr_buf_add(&b2, a))
					err(1, "duplicate buffer");
		}
		RVTEST(pfr_tst_addrs(&table, b.pfrb_caddr, b.pfrb_size,
		    &nmatch, flags));
		xprintf(opts, "%d/%d addresses match", nmatch, b.pfrb_size);
		if ((opts & PF_OPT_VERBOSE) && !(opts & PF_OPT_VERBOSE2))
			PFRB_FOREACH(a, &b)
				if (a->pfra_fback == PFR_FB_MATCH)
					print_addrx(a, NULL,
					    opts & PF_OPT_USEDNS);
		if (opts & PF_OPT_VERBOSE2) {
			a2 = NULL;
			PFRB_FOREACH(a, &b) {
				a2 = pfr_buf_next(&b2, a2);
				print_addrx(a2, a, opts & PF_OPT_USEDNS);
			}
		}
		if (nmatch < b.pfrb_size)
			rv = 2;
	} else if (!strcmp(command, "zero")) {
		if (argc || file != NULL)
			usage();
		flags |= PFR_FLAG_ADDRSTOO;
		RVTEST(pfr_clr_tstats(&table, 1, &nzero, flags));
		xprintf(opts, "%d table/stats cleared", nzero);
	} else
		warnx("pfctl_table: unknown command '%s'", command);
	goto _cleanup;

_error:
	rv = -1;
_cleanup:
	pfr_buf_clear(&b);
	pfr_buf_clear(&b2);
	return (rv);
}

void
print_table(struct pfr_table *ta, int verbose, int debug)
{
	if (!debug && !(ta->pfrt_flags & PFR_TFLAG_ACTIVE))
		return;
	if (verbose) {
		printf("%c%c%c%c%c%c%c\t%s",
		    (ta->pfrt_flags & PFR_TFLAG_CONST) ? 'c' : '-',
		    (ta->pfrt_flags & PFR_TFLAG_PERSIST) ? 'p' : '-',
		    (ta->pfrt_flags & PFR_TFLAG_ACTIVE) ? 'a' : '-',
		    (ta->pfrt_flags & PFR_TFLAG_INACTIVE) ? 'i' : '-',
		    (ta->pfrt_flags & PFR_TFLAG_REFERENCED) ? 'r' : '-',
		    (ta->pfrt_flags & PFR_TFLAG_REFDANCHOR) ? 'h' : '-',
		    (ta->pfrt_flags & PFR_TFLAG_COUNTERS) ? 'C' : '-',
		    ta->pfrt_name);
		if (ta->pfrt_anchor[0])
			printf("\t%s", ta->pfrt_anchor);
		puts("");
	} else
		puts(ta->pfrt_name);
}

void
print_tstats(struct pfr_tstats *ts, int debug)
{
	time_t	time = ts->pfrts_tzero;
	int	dir, op;

	if (!debug && !(ts->pfrts_flags & PFR_TFLAG_ACTIVE))
		return;
	print_table(&ts->pfrts_t, 1, debug);
	printf("\tAddresses:   %d\n", ts->pfrts_cnt);
	printf("\tCleared:     %s", ctime(&time));
	printf("\tReferences:  [ Anchors: %-18d Rules: %-18d ]\n",
	    ts->pfrts_refcnt[PFR_REFCNT_ANCHOR],
	    ts->pfrts_refcnt[PFR_REFCNT_RULE]);
	printf("\tEvaluations: [ NoMatch: %-18llu Match: %-18llu ]\n",
	    (unsigned long long)ts->pfrts_nomatch,
	    (unsigned long long)ts->pfrts_match);
	for (dir = 0; dir < PFR_DIR_MAX; dir++)
		for (op = 0; op < PFR_OP_TABLE_MAX; op++)
			printf("\t%-12s [ Packets: %-18llu Bytes: %-18llu ]\n",
			    stats_text[dir][op],
			    (unsigned long long)ts->pfrts_packets[dir][op],
			    (unsigned long long)ts->pfrts_bytes[dir][op]);
}

int
load_addr(struct pfr_buffer *b, int argc, char *argv[], char *file,
    int nonetwork)
{
	while (argc--)
		if (append_addr(b, *argv++, nonetwork)) {
			if (errno)
				warn("cannot decode %s", argv[-1]);
			return (-1);
		}
	if (pfr_buf_load(b, file, nonetwork, append_addr)) {
		warn("cannot load %s", file);
		return (-1);
	}
	return (0);
}

void
print_addrx(struct pfr_addr *ad, struct pfr_addr *rad, int dns)
{
	char		ch, buf[256] = "{error}";
	char		fb[] = { ' ', 'M', 'A', 'D', 'C', 'Z', 'X', ' ', 'Y', ' ' };
	unsigned int	fback, hostnet;

	fback = (rad != NULL) ? rad->pfra_fback : ad->pfra_fback;
	ch = (fback < sizeof(fb)/sizeof(*fb)) ? fb[fback] : '?';
	hostnet = (ad->pfra_af == AF_INET6) ? 128 : 32;
	inet_ntop(ad->pfra_af, &ad->pfra_u, buf, sizeof(buf));
	printf("%c %c%s", ch, (ad->pfra_not?'!':' '), buf);
	if (ad->pfra_net < hostnet)
		printf("/%d", ad->pfra_net);
	if (rad != NULL && fback != PFR_FB_NONE) {
		if (strlcpy(buf, "{error}", sizeof(buf)) >= sizeof(buf))
			errx(1, "print_addrx: strlcpy");
		inet_ntop(rad->pfra_af, &rad->pfra_u, buf, sizeof(buf));
		printf("\t%c%s", (rad->pfra_not?'!':' '), buf);
		if (rad->pfra_net < hostnet)
			printf("/%d", rad->pfra_net);
	}
	if (rad != NULL && fback == PFR_FB_NONE)
		printf("\t nomatch");
	if (dns && ad->pfra_net == hostnet) {
		char host[NI_MAXHOST];
		union sockaddr_union sa;

		strlcpy(host, "?", sizeof(host));
		bzero(&sa, sizeof(sa));
		sa.sa.sa_family = ad->pfra_af;
		if (sa.sa.sa_family == AF_INET) {
			sa.sa.sa_len = sizeof(sa.sin);
			sa.sin.sin_addr = ad->pfra_ip4addr;
		} else {
			sa.sa.sa_len = sizeof(sa.sin6);
			sa.sin6.sin6_addr = ad->pfra_ip6addr;
		}
		if (getnameinfo(&sa.sa, sa.sa.sa_len, host, sizeof(host),
		    NULL, 0, NI_NAMEREQD) == 0)
			printf("\t(%s)", host);
	}
	printf("\n");
}

void
print_astats(struct pfr_astats *as, int dns)
{
	time_t	time = as->pfras_tzero;
	int	dir, op;

	print_addrx(&as->pfras_a, NULL, dns);
	printf("\tCleared:     %s", ctime(&time));
 	if (as->pfras_a.pfra_fback == PFR_FB_NOCOUNT)
		return;
	for (dir = 0; dir < PFR_DIR_MAX; dir++)
		for (op = 0; op < PFR_OP_ADDR_MAX; op++)
			printf("\t%-12s [ Packets: %-18llu Bytes: %-18llu ]\n",
			    stats_text[dir][op],
			    (unsigned long long)as->pfras_packets[dir][op],
			    (unsigned long long)as->pfras_bytes[dir][op]);
}

void
radix_perror(void)
{
	extern char *__progname;
	fprintf(stderr, "%s: %s.\n", __progname, pfr_strerror(errno));
}

int
pfctl_define_table(char *name, int flags, int addrs, const char *anchor,
    struct pfr_buffer *ab, u_int32_t ticket)
{
	struct pfr_table tbl;

	bzero(&tbl, sizeof(tbl));
	if (strlcpy(tbl.pfrt_name, name, sizeof(tbl.pfrt_name)) >=
	    sizeof(tbl.pfrt_name) || strlcpy(tbl.pfrt_anchor, anchor,
	    sizeof(tbl.pfrt_anchor)) >= sizeof(tbl.pfrt_anchor))
		errx(1, "pfctl_define_table: strlcpy");
	tbl.pfrt_flags = flags;

	return pfr_ina_define(&tbl, ab->pfrb_caddr, ab->pfrb_size, NULL,
	    NULL, ticket, addrs ? PFR_FLAG_ADDRSTOO : 0);
}

void
warn_namespace_collision(const char *filter)
{
	struct pfr_buffer b;
	struct pfr_table *t;
	const char *name = NULL, *lastcoll;
	int coll = 0;

	bzero(&b, sizeof(b));
	b.pfrb_type = PFRB_TABLES;
	for (;;) {
		pfr_buf_grow(&b, b.pfrb_size);
		b.pfrb_size = b.pfrb_msize;
		if (pfr_get_tables(NULL, b.pfrb_caddr,
		    &b.pfrb_size, PFR_FLAG_ALLRSETS))
			err(1, "pfr_get_tables");
		if (b.pfrb_size <= b.pfrb_msize)
			break;
	}
	PFRB_FOREACH(t, &b) {
		if (!(t->pfrt_flags & PFR_TFLAG_ACTIVE))
			continue;
		if (filter != NULL && strcmp(filter, t->pfrt_name))
			continue;
		if (!t->pfrt_anchor[0])
			name = t->pfrt_name;
		else if (name != NULL && !strcmp(name, t->pfrt_name)) {
			coll++;
			lastcoll = name;
			name = NULL;
		}
	}
	if (coll == 1)
		warnx("warning: namespace collision with <%s> global table.",
		    lastcoll);
	else if (coll > 1)
		warnx("warning: namespace collisions with %d global tables.",
		    coll);
	pfr_buf_clear(&b);
}

void
xprintf(int opts, const char *fmt, ...)
{
	va_list args;

	if (opts & PF_OPT_QUIET)
		return;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	if (opts & PF_OPT_DUMMYACTION)
		fprintf(stderr, " (dummy).\n");
	else if (opts & PF_OPT_NOACTION)
		fprintf(stderr, " (syntax only).\n");
	else
		fprintf(stderr, ".\n");
}


/* interface stuff */

int
pfctl_show_ifaces(const char *filter, int opts)
{
	struct pfr_buffer	 b;
	struct pfi_kif		*p;
	int			 i = 0;

	bzero(&b, sizeof(b));
	b.pfrb_type = PFRB_IFACES;
	for (;;) {
		pfr_buf_grow(&b, b.pfrb_size);
		b.pfrb_size = b.pfrb_msize;
		if (pfi_get_ifaces(filter, b.pfrb_caddr, &b.pfrb_size)) {
			radix_perror();
			return (1);
		}
		if (b.pfrb_size <= b.pfrb_msize)
			break;
		i++;
	}
	if (opts & PF_OPT_SHOWALL)
		pfctl_print_title("INTERFACES:");
	PFRB_FOREACH(p, &b)
		print_iface(p, opts);
	return (0);
}

void
print_iface(struct pfi_kif *p, int opts)
{
	time_t	tzero = p->pfik_tzero;
	int	i, af, dir, act;

	printf("%s", p->pfik_name);
	if (opts & PF_OPT_VERBOSE) {
		if (p->pfik_flags & PFI_IFLAG_SKIP)
			printf(" (skip)");
	}
	printf("\n");

	if (!(opts & PF_OPT_VERBOSE2))
		return;
	printf("\tCleared:     %s", ctime(&tzero));
	printf("\tReferences:  %-18d\n", p->pfik_rulerefs);
	for (i = 0; i < 8; i++) {
		af = (i>>2) & 1;
		dir = (i>>1) &1;
		act = i & 1;
		printf("\t%-12s [ Packets: %-18llu Bytes: %-18llu ]\n",
		    istats_text[af][dir][act],
		    (unsigned long long)p->pfik_packets[af][dir][act],
		    (unsigned long long)p->pfik_bytes[af][dir][act]);
	}
}
