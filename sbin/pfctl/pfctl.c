/*	$OpenBSD: pfctl.c,v 1.397 2025/05/26 20:55:30 sashan Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <net/if.h>
#include <netinet/in.h>
#include <net/pfvar.h>
#include <arpa/inet.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <libgen.h>

#include "pfctl_parser.h"
#include "pfctl.h"

void	 usage(void);
int	 pfctl_enable(int, int);
int	 pfctl_disable(int, int);
void	 pfctl_clear_queues(struct pf_qihead *);
void	 pfctl_clear_stats(int, const char *, int);
void	 pfctl_clear_interface_flags(int, int);
int	 pfctl_clear_rules(int, int, char *);
void	 pfctl_clear_src_nodes(int, int);
void	 pfctl_clear_states(int, const char *, int);
struct addrinfo *
	 pfctl_addrprefix(char *, struct pf_addr *, int);
void	 pfctl_kill_src_nodes(int, int);
void	 pfctl_net_kill_states(int, const char *, int, int);
void	 pfctl_label_kill_states(int, const char *, int, int);
void	 pfctl_id_kill_states(int, int);
void	 pfctl_key_kill_states(int, const char *, int, int);
int	 pfctl_parse_host(char *, struct pf_rule_addr *);
void	 pfctl_init_options(struct pfctl *);
int	 pfctl_load_options(struct pfctl *);
int	 pfctl_load_limit(struct pfctl *, unsigned int, unsigned int);
int	 pfctl_load_timeout(struct pfctl *, unsigned int, unsigned int);
int	 pfctl_load_debug(struct pfctl *, unsigned int);
int	 pfctl_load_logif(struct pfctl *, char *);
int	 pfctl_load_hostid(struct pfctl *, unsigned int);
int	 pfctl_load_reassembly(struct pfctl *, u_int32_t);
int	 pfctl_load_syncookies(struct pfctl *, u_int8_t);
int	 pfctl_set_synflwats(struct pfctl *, u_int32_t, u_int32_t);
void	 pfctl_print_rule_counters(struct pf_rule *, int);
int	 pfctl_show_rules(int, char *, int, enum pfctl_show, char *, int, int,
	    long);
int	 pfctl_show_src_nodes(int, int);
int	 pfctl_show_states(int, const char *, int, long);
int	 pfctl_show_status(int, int);
int	 pfctl_show_timeouts(int, int);
int	 pfctl_show_limits(int, int);
void	 pfctl_read_limits(int);
void	 pfctl_restore_limits(void);
void	 pfctl_debug(int, u_int32_t, int);
int	 pfctl_show_anchors(int, int, char *);
int	 pfctl_ruleset_trans(struct pfctl *, char *, struct pf_anchor *);
u_int	 pfctl_find_childqs(struct pfctl_qsitem *);
void	 pfctl_load_queue(struct pfctl *, u_int32_t, struct pfctl_qsitem *);
int	 pfctl_load_queues(struct pfctl *);
u_int	 pfctl_leafqueue_check(char *);
u_int	 pfctl_check_qassignments(struct pf_ruleset *);
int	 pfctl_load_ruleset(struct pfctl *, char *, struct pf_ruleset *, int);
int	 pfctl_load_rule(struct pfctl *, char *, struct pf_rule *, int);
const char	*pfctl_lookup_option(char *, const char **);
void	pfctl_state_store(int, const char *);
void	pfctl_state_load(int, const char *);
void	pfctl_reset(int, int);
int	pfctl_walk_show(int, struct pfioc_ruleset *, void *);
int	pfctl_walk_get(int, struct pfioc_ruleset *, void *);
int	pfctl_walk_anchors(int, int, const char *,
    int(*)(int, struct pfioc_ruleset *, void *), void *);
struct pfr_anchors *
	pfctl_get_anchors(int, const char *, int);
int	pfctl_recurse(int, int, const char *,
	    int(*)(int, int, struct pfr_anchoritem *));
int	pfctl_call_clearrules(int, int, struct pfr_anchoritem *);
int	pfctl_call_cleartables(int, int, struct pfr_anchoritem *);
int	pfctl_call_clearanchors(int, int, struct pfr_anchoritem *);
int	pfctl_call_showtables(int, int, struct pfr_anchoritem *);

const char	*clearopt;
char		*rulesopt;
const char	*showopt;
const char	*debugopt;
char		*anchoropt;
const char	*optiopt = NULL;
char		*pf_device = "/dev/pf";
char		*ifaceopt;
char		*tableopt;
const char	*tblcmdopt;
int		 src_node_killers;
char		*src_node_kill[2];
int		 state_killers;
char		*state_kill[2];

int		 dev = -1;
int		 first_title = 1;
int		 labels = 0;
int		 exit_val = 0;

#define INDENT(d, o)	do {						\
				if (o) {				\
					int i;				\
					for (i=0; i < d; i++)		\
						printf("  ");		\
				}					\
			} while (0)					\


static const struct {
	const char	*name;
	int		index;
} pf_limits[] = {
	{ "states",		PF_LIMIT_STATES },
	{ "src-nodes",		PF_LIMIT_SRC_NODES },
	{ "frags",		PF_LIMIT_FRAGS },
	{ "tables",		PF_LIMIT_TABLES },
	{ "table-entries",	PF_LIMIT_TABLE_ENTRIES },
	{ "pktdelay-pkts",	PF_LIMIT_PKTDELAY_PKTS },
	{ "anchors",		PF_LIMIT_ANCHORS },
	{ NULL,			0 }
};

static unsigned int	limit_curr[PF_LIMIT_MAX];

struct pf_hint {
	const char	*name;
	int		timeout;
};
static const struct pf_hint pf_hint_normal[] = {
	{ "tcp.first",		2 * 60 },
	{ "tcp.opening",	30 },
	{ "tcp.established",	24 * 60 * 60 },
	{ "tcp.closing",	15 * 60 },
	{ "tcp.finwait",	45 },
	{ "tcp.closed",		90 },
	{ "tcp.tsdiff",		30 },
	{ NULL,			0 }
};
static const struct pf_hint pf_hint_satellite[] = {
	{ "tcp.first",		3 * 60 },
	{ "tcp.opening",	30 + 5 },
	{ "tcp.established",	24 * 60 * 60 },
	{ "tcp.closing",	15 * 60 + 5 },
	{ "tcp.finwait",	45 + 5 },
	{ "tcp.closed",		90 + 5 },
	{ "tcp.tsdiff",		60 },
	{ NULL,			0 }
};
static const struct pf_hint pf_hint_conservative[] = {
	{ "tcp.first",		60 * 60 },
	{ "tcp.opening",	15 * 60 },
	{ "tcp.established",	5 * 24 * 60 * 60 },
	{ "tcp.closing",	60 * 60 },
	{ "tcp.finwait",	10 * 60 },
	{ "tcp.closed",		3 * 60 },
	{ "tcp.tsdiff",		60 },
	{ NULL,			0 }
};
static const struct pf_hint pf_hint_aggressive[] = {
	{ "tcp.first",		30 },
	{ "tcp.opening",	5 },
	{ "tcp.established",	5 * 60 * 60 },
	{ "tcp.closing",	60 },
	{ "tcp.finwait",	30 },
	{ "tcp.closed",		30 },
	{ "tcp.tsdiff",		10 },
	{ NULL,			0 }
};

static const struct {
	const char *name;
	const struct pf_hint *hint;
} pf_hints[] = {
	{ "normal",		pf_hint_normal },
	{ "satellite",		pf_hint_satellite },
	{ "high-latency",	pf_hint_satellite },
	{ "conservative",	pf_hint_conservative },
	{ "aggressive",		pf_hint_aggressive },
	{ NULL,			NULL }
};

static const char *clearopt_list[] = {
	"rules", "Sources", "states", "info", "Tables", "osfp", "Reset",
	"all", NULL
};

static const char *showopt_list[] = {
	"queue", "rules", "Anchors", "Sources", "states", "info",
	"Interfaces", "labels", "timeouts", "memory", "Tables", "osfp",
	"all", NULL
};

static const char *tblcmdopt_list[] = {
	"kill", "flush", "add", "delete", "replace", "show",
	"test", "zero", "expire", NULL
};

static const char *debugopt_list[] = {
	"debug", "info", "notice", "warning",
	"error", "crit", "alert", "emerg",
	NULL
};

static const char *optiopt_list[] = {
	"none", "basic", "profile", NULL
};

struct pf_qihead qspecs = TAILQ_HEAD_INITIALIZER(qspecs);
struct pf_qihead rootqs = TAILQ_HEAD_INITIALIZER(rootqs);

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-deghNnPqrvz] ", __progname);
	fprintf(stderr, "[-a anchor] [-D macro=value] [-F modifier]");
	fprintf(stderr, " [-f file]\n");
	fprintf(stderr, "\t[-i interface] [-K key] [-k key] [-L statefile]");
	fprintf(stderr, " [-o level]\n");
	fprintf(stderr, "\t[-p device] [-S statefile] [-s modifier [-R id]]\n");
	fprintf(stderr, "\t[-t table -T command [address ...]]");
	fprintf(stderr, " [-V rdomain] [-x level]\n");
	exit(1);
}

void
pfctl_err(int opts, int eval, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);

	if ((opts & PF_OPT_IGNFAIL) == 0)
		verr(eval, fmt, ap);
	else
		vwarn(fmt, ap);

	va_end(ap);

	exit_val = eval;
}

void
pfctl_errx(int opts, int eval, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);

	if ((opts & PF_OPT_IGNFAIL) == 0)
		verrx(eval, fmt, ap);
	else
		vwarnx(fmt, ap);

	va_end(ap);

	exit_val = eval;
}

int
pfctl_enable(int dev, int opts)
{
	if (ioctl(dev, DIOCSTART) == -1) {
		if (errno == EEXIST)
			errx(1, "pf already enabled");
		else
			err(1, "DIOCSTART");
	}
	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "pf enabled\n");

	return (0);
}

int
pfctl_disable(int dev, int opts)
{
	if (ioctl(dev, DIOCSTOP) == -1) {
		if (errno == ENOENT)
			errx(1, "pf not enabled");
		else
			err(1, "DIOCSTOP");
	}
	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "pf disabled\n");

	return (0);
}

void
pfctl_clear_stats(int dev, const char *iface, int opts)
{
	struct pfioc_iface pi;

	memset(&pi, 0, sizeof(pi));
	if (iface != NULL && strlcpy(pi.pfiio_name, iface,
	    sizeof(pi.pfiio_name)) >= sizeof(pi.pfiio_name))
		pfctl_errx(opts, 1, "invalid interface: %s", iface);

	if (ioctl(dev, DIOCCLRSTATUS, &pi) == -1)
		pfctl_err(opts, 1, "DIOCCLRSTATUS");
	if ((opts & PF_OPT_QUIET) == 0) {
		fprintf(stderr, "pf: statistics cleared");
		if (iface != NULL)
			fprintf(stderr, " for interface %s", iface);
		fprintf(stderr, "\n");
	}
}

void
pfctl_clear_interface_flags(int dev, int opts)
{
	struct pfioc_iface	pi;

	if ((opts & PF_OPT_NOACTION) == 0) {
		bzero(&pi, sizeof(pi));
		pi.pfiio_flags = PFI_IFLAG_SKIP;

		if (ioctl(dev, DIOCCLRIFFLAG, &pi) == -1)
			pfctl_err(opts, 1, "DIOCCLRIFFLAG");
		if ((opts & PF_OPT_QUIET) == 0)
			fprintf(stderr, "pf: interface flags reset\n");
	}
}

int
pfctl_clear_rules(int dev, int opts, char *anchorname)
{
	struct pfr_buffer	t;

	memset(&t, 0, sizeof(t));
	t.pfrb_type = PFRB_TRANS;
	if (pfctl_add_trans(&t, PF_TRANS_RULESET, anchorname) ||
	    pfctl_trans(dev, &t, DIOCXBEGIN, 0) ||
	    pfctl_trans(dev, &t, DIOCXCOMMIT, 0)) {
		pfctl_err(opts, 1, "%s", __func__);
		return (1);
	} else if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "rules cleared\n");

	return (0);
}

void
pfctl_clear_src_nodes(int dev, int opts)
{
	if (ioctl(dev, DIOCCLRSRCNODES) == -1)
		pfctl_err(opts, 1, "DIOCCLRSRCNODES");
	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "source tracking entries cleared\n");
}

void
pfctl_clear_states(int dev, const char *iface, int opts)
{
	struct pfioc_state_kill psk;

	memset(&psk, 0, sizeof(psk));
	if (iface != NULL && strlcpy(psk.psk_ifname, iface,
	    sizeof(psk.psk_ifname)) >= sizeof(psk.psk_ifname))
		pfctl_errx(opts, 1, "invalid interface: %s", iface);

	if (ioctl(dev, DIOCCLRSTATES, &psk) == -1)
		pfctl_err(opts, 1, "DIOCCLRSTATES");
	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "%d states cleared\n", psk.psk_killed);
}

struct addrinfo *
pfctl_addrprefix(char *addr, struct pf_addr *mask, int numeric)
{
	char *p;
	const char *errstr;
	int prefix, ret_ga, q, r;
	struct addrinfo hints, *res;

	bzero(&hints, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;	/* dummy */
	if (numeric)
		hints.ai_flags = AI_NUMERICHOST;

	if ((p = strchr(addr, '/')) != NULL) {
		*p++ = '\0';
		/* prefix only with numeric addresses */
		hints.ai_flags |= AI_NUMERICHOST;
	}

	if ((ret_ga = getaddrinfo(addr, NULL, &hints, &res))) {
		errx(1, "getaddrinfo: %s", gai_strerror(ret_ga));
		/* NOTREACHED */
	}

	if (p == NULL)
		return res;

	prefix = strtonum(p, 0, res->ai_family == AF_INET6 ? 128 : 32, &errstr);
	if (errstr)
		errx(1, "prefix is %s: %s", errstr, p);

	q = prefix >> 3;
	r = prefix & 7;
	switch (res->ai_family) {
	case AF_INET:
		bzero(&mask->v4, sizeof(mask->v4));
		mask->v4.s_addr = htonl((u_int32_t)
		    (0xffffffffffULL << (32 - prefix)));
		break;
	case AF_INET6:
		bzero(&mask->v6, sizeof(mask->v6));
		if (q > 0)
			memset((void *)&mask->v6, 0xff, q);
		if (r > 0)
			*((u_char *)&mask->v6 + q) =
			    (0xff00 >> r) & 0xff;
		break;
	}

	return res;
}

void
pfctl_kill_src_nodes(int dev, int opts)
{
	struct pfioc_src_node_kill psnk;
	struct addrinfo *res[2], *resp[2];
	struct sockaddr last_src, last_dst;
	int killed, sources, dests;

	killed = sources = dests = 0;

	memset(&psnk, 0, sizeof(psnk));
	memset(&psnk.psnk_src.addr.v.a.mask, 0xff,
	    sizeof(psnk.psnk_src.addr.v.a.mask));
	memset(&last_src, 0xff, sizeof(last_src));
	memset(&last_dst, 0xff, sizeof(last_dst));

	res[0] = pfctl_addrprefix(src_node_kill[0],
	    &psnk.psnk_src.addr.v.a.mask, (opts & PF_OPT_NODNS));

	for (resp[0] = res[0]; resp[0]; resp[0] = resp[0]->ai_next) {
		if (resp[0]->ai_addr == NULL)
			continue;
		/* We get lots of duplicates.  Catch the easy ones */
		if (memcmp(&last_src, resp[0]->ai_addr, sizeof(last_src)) == 0)
			continue;
		last_src = *(struct sockaddr *)resp[0]->ai_addr;

		psnk.psnk_af = resp[0]->ai_family;
		sources++;

		copy_satopfaddr(&psnk.psnk_src.addr.v.a.addr, resp[0]->ai_addr);

		if (src_node_killers > 1) {
			dests = 0;
			memset(&psnk.psnk_dst.addr.v.a.mask, 0xff,
			    sizeof(psnk.psnk_dst.addr.v.a.mask));
			memset(&last_dst, 0xff, sizeof(last_dst));
			res[1] = pfctl_addrprefix(src_node_kill[1],
			    &psnk.psnk_dst.addr.v.a.mask,
			    (opts & PF_OPT_NODNS));
			for (resp[1] = res[1]; resp[1];
			    resp[1] = resp[1]->ai_next) {
				if (resp[1]->ai_addr == NULL)
					continue;
				if (psnk.psnk_af != resp[1]->ai_family)
					continue;

				if (memcmp(&last_dst, resp[1]->ai_addr,
				    sizeof(last_dst)) == 0)
					continue;
				last_dst = *(struct sockaddr *)resp[1]->ai_addr;

				dests++;

				copy_satopfaddr(&psnk.psnk_dst.addr.v.a.addr,
				    resp[1]->ai_addr);

				if (ioctl(dev, DIOCKILLSRCNODES, &psnk) == -1)
					err(1, "DIOCKILLSRCNODES");
				killed += psnk.psnk_killed;
			}
			freeaddrinfo(res[1]);
		} else {
			if (ioctl(dev, DIOCKILLSRCNODES, &psnk) == -1)
				err(1, "DIOCKILLSRCNODES");
			killed += psnk.psnk_killed;
		}
	}

	freeaddrinfo(res[0]);

	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "killed %d src nodes from %d sources and %d "
		    "destinations\n", killed, sources, dests);
}

void
pfctl_net_kill_states(int dev, const char *iface, int opts, int rdomain)
{
	struct pfioc_state_kill psk;
	struct addrinfo *res[2], *resp[2];
	struct sockaddr last_src, last_dst;
	int killed, sources, dests;

	killed = sources = dests = 0;

	memset(&psk, 0, sizeof(psk));
	memset(&psk.psk_src.addr.v.a.mask, 0xff,
	    sizeof(psk.psk_src.addr.v.a.mask));
	memset(&last_src, 0xff, sizeof(last_src));
	memset(&last_dst, 0xff, sizeof(last_dst));
	if (iface != NULL && strlcpy(psk.psk_ifname, iface,
	    sizeof(psk.psk_ifname)) >= sizeof(psk.psk_ifname))
		errx(1, "invalid interface: %s", iface);

	psk.psk_rdomain = rdomain;

	res[0] = pfctl_addrprefix(state_kill[0],
	    &psk.psk_src.addr.v.a.mask, (opts & PF_OPT_NODNS));

	for (resp[0] = res[0]; resp[0]; resp[0] = resp[0]->ai_next) {
		if (resp[0]->ai_addr == NULL)
			continue;
		/* We get lots of duplicates.  Catch the easy ones */
		if (memcmp(&last_src, resp[0]->ai_addr, sizeof(last_src)) == 0)
			continue;
		last_src = *(struct sockaddr *)resp[0]->ai_addr;

		psk.psk_af = resp[0]->ai_family;
		sources++;

		copy_satopfaddr(&psk.psk_src.addr.v.a.addr, resp[0]->ai_addr);

		if (state_killers > 1) {
			dests = 0;
			memset(&psk.psk_dst.addr.v.a.mask, 0xff,
			    sizeof(psk.psk_dst.addr.v.a.mask));
			memset(&last_dst, 0xff, sizeof(last_dst));
			res[1] = pfctl_addrprefix(state_kill[1],
			    &psk.psk_dst.addr.v.a.mask,
			    (opts & PF_OPT_NODNS));
			for (resp[1] = res[1]; resp[1];
			    resp[1] = resp[1]->ai_next) {
				if (resp[1]->ai_addr == NULL)
					continue;
				if (psk.psk_af != resp[1]->ai_family)
					continue;

				if (memcmp(&last_dst, resp[1]->ai_addr,
				    sizeof(last_dst)) == 0)
					continue;
				last_dst = *(struct sockaddr *)resp[1]->ai_addr;

				dests++;

				copy_satopfaddr(&psk.psk_dst.addr.v.a.addr,
				    resp[1]->ai_addr);

				if (ioctl(dev, DIOCKILLSTATES, &psk) == -1)
					err(1, "DIOCKILLSTATES");
				killed += psk.psk_killed;
			}
			freeaddrinfo(res[1]);
		} else {
			if (ioctl(dev, DIOCKILLSTATES, &psk) == -1)
				err(1, "DIOCKILLSTATES");
			killed += psk.psk_killed;
		}
	}

	freeaddrinfo(res[0]);

	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "killed %d states from %d sources and %d "
		    "destinations\n", killed, sources, dests);
}

void
pfctl_label_kill_states(int dev, const char *iface, int opts, int rdomain)
{
	struct pfioc_state_kill psk;

	if (state_killers != 2 || (strlen(state_kill[1]) == 0)) {
		warnx("no label specified");
		usage();
	}
	memset(&psk, 0, sizeof(psk));
	if (iface != NULL && strlcpy(psk.psk_ifname, iface,
	    sizeof(psk.psk_ifname)) >= sizeof(psk.psk_ifname))
		errx(1, "invalid interface: %s", iface);

	if (strlcpy(psk.psk_label, state_kill[1], sizeof(psk.psk_label)) >=
	    sizeof(psk.psk_label))
		errx(1, "label too long: %s", state_kill[1]);

	psk.psk_rdomain = rdomain;

	if (ioctl(dev, DIOCKILLSTATES, &psk) == -1)
		err(1, "DIOCKILLSTATES");

	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "killed %d states\n", psk.psk_killed);
}

void
pfctl_id_kill_states(int dev, int opts)
{
	struct pfioc_state_kill psk;

	if (state_killers != 2 || (strlen(state_kill[1]) == 0)) {
		warnx("no id specified");
		usage();
	}

	memset(&psk, 0, sizeof(psk));
	if ((sscanf(state_kill[1], "%llx/%x",
	    &psk.psk_pfcmp.id, &psk.psk_pfcmp.creatorid)) == 2)
		HTONL(psk.psk_pfcmp.creatorid);
	else if ((sscanf(state_kill[1], "%llx", &psk.psk_pfcmp.id)) == 1) {
		psk.psk_pfcmp.creatorid = 0;
	} else {
		warnx("wrong id format specified");
		usage();
	}
	if (psk.psk_pfcmp.id == 0) {
		warnx("cannot kill id 0");
		usage();
	}

	psk.psk_pfcmp.id = htobe64(psk.psk_pfcmp.id);
	if (ioctl(dev, DIOCKILLSTATES, &psk) == -1)
		err(1, "DIOCKILLSTATES");

	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "killed %d states\n", psk.psk_killed);
}

void
pfctl_key_kill_states(int dev, const char *iface, int opts, int rdomain)
{
	struct pfioc_state_kill psk;
	char *s, *token, *tokens[4];
	struct protoent *p;
	u_int i, sidx, didx;

	if (state_killers != 2 || (strlen(state_kill[1]) == 0)) {
		warnx("no key specified");
		usage();
	}
	memset(&psk, 0, sizeof(psk));

	if (iface != NULL && strlcpy(psk.psk_ifname, iface,
	    sizeof(psk.psk_ifname)) >= sizeof(psk.psk_ifname))
		errx(1, "invalid interface: %s", iface);

	psk.psk_rdomain = rdomain;

	s = strdup(state_kill[1]);
	if (!s)
		errx(1, "pfctl_key_kill_states: strdup");
	i = 0;
	while ((token = strsep(&s, " \t")) != NULL)
		if (*token != '\0') {
			if (i < 4)
				tokens[i] = token;
			i++;
		}
	if (i != 4)
		errx(1, "pfctl_key_kill_states: key must be "
		    "\"protocol host1:port1 direction host2:port2\" format");

	if ((p = getprotobyname(tokens[0])) == NULL)
		errx(1, "invalid protocol: %s", tokens[0]);
	psk.psk_proto = p->p_proto;

	if (strcmp(tokens[2], "->") == 0) {
		sidx = 1;
		didx = 3;
	} else if (strcmp(tokens[2], "<-") == 0) {
		sidx = 3;
		didx = 1;
	} else
		errx(1, "invalid direction: %s", tokens[2]);

	if (pfctl_parse_host(tokens[sidx], &psk.psk_src) == -1)
		errx(1, "invalid host: %s", tokens[sidx]);
	if (pfctl_parse_host(tokens[didx], &psk.psk_dst) == -1)
		errx(1, "invalid host: %s", tokens[didx]);

	if (ioctl(dev, DIOCKILLSTATES, &psk) == -1)
		err(1, "DIOCKILLSTATES");

	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "killed %d states\n", psk.psk_killed);
}

int
pfctl_parse_host(char *str, struct pf_rule_addr *addr)
{
	char *s = NULL, *sbs, *sbe;
	struct addrinfo hints, *ai;

	s = strdup(str);
	if (!s)
		errx(1, "pfctl_parse_host: strdup");

	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_DGRAM;	/* dummy */
	hints.ai_flags = AI_NUMERICHOST;

	if ((sbs = strchr(s, '[')) != NULL && (sbe = strrchr(s, ']')) != NULL) {
		hints.ai_family = AF_INET6;
		*(sbs++) = *sbe = '\0';
	} else if ((sbs = strchr(s, ':')) != NULL) {
		hints.ai_family = AF_INET;
		*(sbs++) = '\0';
	} else
		goto error;

	if (getaddrinfo(s, sbs, &hints, &ai) != 0)
		goto error;

	copy_satopfaddr(&addr->addr.v.a.addr, ai->ai_addr);
	addr->port[0] = ai->ai_family == AF_INET6 ?
	    ((struct sockaddr_in6 *)ai->ai_addr)->sin6_port :
	    ((struct sockaddr_in *)ai->ai_addr)->sin_port;

	freeaddrinfo(ai);
	free(s);

	memset(&addr->addr.v.a.mask, 0xff, sizeof(struct pf_addr));
	addr->port_op = PF_OP_EQ;
	addr->addr.type = PF_ADDR_ADDRMASK;

	return (0);

 error:
	free(s);
	return (-1);
}

void
pfctl_print_rule_counters(struct pf_rule *rule, int opts)
{
	if ((rule->rule_flag & PFRULE_EXPIRED) &&
	    !(opts & (PF_OPT_VERBOSE2 | PF_OPT_DEBUG)))
		return;

	if (opts & PF_OPT_DEBUG) {
		const char *t[PF_SKIP_COUNT] = { "i", "d", "r", "f",
		    "p", "sa", "da", "sp", "dp" };
		int i;

		printf("  [ Skip steps: ");
		for (i = 0; i < PF_SKIP_COUNT; ++i) {
			if (rule->skip[i].nr == rule->nr + 1)
				continue;
			printf("%s=", t[i]);
			if (rule->skip[i].nr == -1)
				printf("end ");
			else
				printf("%u ", rule->skip[i].nr);
		}
		printf("]\n");

		printf("  [ queue: qname=%s qid=%u pqname=%s pqid=%u ]\n",
		    rule->qname, rule->qid, rule->pqname, rule->pqid);
	}
	if (opts & PF_OPT_VERBOSE) {
		printf("  [ Evaluations: %-8llu  Packets: %-8llu  "
			    "Bytes: %-10llu  States: %-6u]\n",
			    (unsigned long long)rule->evaluations,
			    (unsigned long long)(rule->packets[0] +
			    rule->packets[1]),
			    (unsigned long long)(rule->bytes[0] +
			    rule->bytes[1]), rule->states_cur);
		if (!(opts & PF_OPT_DEBUG))
			printf("  [ Inserted: uid %lu pid %lu "
			    "State Creations: %-6u]\n",
			    (unsigned long)rule->cuid, (unsigned long)rule->cpid,
			    rule->states_tot);
	}
}

void
pfctl_print_title(char *title)
{
	if (!first_title)
		printf("\n");
	first_title = 0;
	printf("%s\n", title);
}

int
pfctl_show_rules(int dev, char *path, int opts, enum pfctl_show format,
    char *anchorname, int depth, int wildcard, long shownr)
{
	struct pfioc_rule pr;
	u_int32_t header = 0;
	int len = strlen(path), ret = 0;
	char *npath, *p;

	if (depth > PF_ANCHOR_STACK_MAX) {
		warnx("%s: max stack depth exceeded for %s", __func__, path);
		return (-1);
	}

	/*
	 * Truncate a trailing / and * on an anchorname before searching for
	 * the ruleset, this is syntactic sugar that doesn't actually make it
	 * to the kernel.
	 */
	if ((p = strrchr(anchorname, '/')) != NULL &&
	    p[1] == '*' && p[2] == '\0') {
		p[0] = '\0';
	}

	memset(&pr, 0, sizeof(pr));
	if (anchorname[0] == '/') {
		if ((npath = calloc(1, PATH_MAX)) == NULL)
			err(1, "calloc");
		strlcpy(npath, anchorname, PATH_MAX);
	} else {
		if (path[0])
			snprintf(&path[len], PATH_MAX - len, "/%s", anchorname);
		else
			snprintf(&path[len], PATH_MAX - len, "%s", anchorname);
		npath = path;
	}

	memcpy(pr.anchor, npath, sizeof(pr.anchor));
	if (opts & PF_OPT_SHOWALL) {
		pr.rule.action = PF_PASS;
		if (ioctl(dev, DIOCGETRULES, &pr) == -1) {
			warnx("%s", pf_strerror(errno));
			ret = -1;
			goto error;
		}
		header++;
		if (format == PFCTL_SHOW_RULES && (pr.nr > 0 || header))
			pfctl_print_title("FILTER RULES:");
		else if (format == PFCTL_SHOW_LABELS && labels)
			pfctl_print_title("LABEL COUNTERS:");
	}
	if (opts & PF_OPT_CLRRULECTRS)
		pr.action = PF_GET_CLR_CNTR;

	pr.rule.action = PF_PASS;
	if (ioctl(dev, DIOCGETRULES, &pr) == -1) {
		warnx("%s", pf_strerror(errno));
		ret = -1;
		goto error;
	}

	while (ioctl(dev, DIOCGETRULE, &pr) != -1) {
		if (shownr != -1 && shownr != pr.nr)
			continue;

		/* anchor is the same for all rules in it */
		if (pr.rule.anchor_wildcard == 0)
			wildcard = 0;

		switch (format) {
		case PFCTL_SHOW_LABELS:
			if (pr.rule.label[0]) {
				INDENT(depth, !(opts & PF_OPT_VERBOSE));
				printf("%s %llu %llu %llu %llu"
				    " %llu %llu %llu %llu\n",
				    pr.rule.label,
				    (unsigned long long)pr.rule.evaluations,
				    (unsigned long long)(pr.rule.packets[0] +
				    pr.rule.packets[1]),
				    (unsigned long long)(pr.rule.bytes[0] +
				    pr.rule.bytes[1]),
				    (unsigned long long)pr.rule.packets[0],
				    (unsigned long long)pr.rule.bytes[0],
				    (unsigned long long)pr.rule.packets[1],
				    (unsigned long long)pr.rule.bytes[1],
				    (unsigned long long)pr.rule.states_tot);
			}
			break;
		case PFCTL_SHOW_RULES:
			if (pr.rule.label[0] && (opts & PF_OPT_SHOWALL))
				labels = 1;
			INDENT(depth, !(opts & PF_OPT_VERBOSE));
			print_rule(&pr.rule, pr.anchor_call, opts);

			/*
			 * If this is an 'unnamed' brace notation anchor OR
			 * the user has explicitly requested recursion,
			 * print it recursively.
			 */
		        if (pr.anchor_call[0] &&
			    (((p = strrchr(pr.anchor_call, '/')) ?
			    p[1] == '_' : pr.anchor_call[0] == '_') ||
			    opts & PF_OPT_RECURSE)) {
				printf(" {\n");
				pfctl_print_rule_counters(&pr.rule, opts);
				pfctl_show_rules(dev, npath, opts, format,
				    pr.anchor_call, depth + 1,
				    pr.rule.anchor_wildcard, -1);
				INDENT(depth, !(opts & PF_OPT_VERBOSE));
				printf("}\n");
			} else {
				if ((pr.rule.rule_flag & PFRULE_EXPIRED) &&
				    !(opts & (PF_OPT_VERBOSE2 | PF_OPT_DEBUG)))
					break;
				printf("\n");
				pfctl_print_rule_counters(&pr.rule, opts);
			}
			break;
		case PFCTL_SHOW_NOTHING:
			break;
		}
		errno = 0;
	}

	if (errno != 0 && errno != ENOENT) {
		warn("DIOCGETRULE");
		ret = -1;
		goto error;
	}

	/*
	 * If this anchor was called with a wildcard path, go through
	 * the rulesets in the anchor rather than the rules.
	 */
	if (wildcard && (opts & PF_OPT_RECURSE)) {
		struct pfioc_ruleset	 prs;
		u_int32_t		 mnr, nr;

		memset(&prs, 0, sizeof(prs));
		memcpy(prs.path, npath, sizeof(prs.path));
		if (ioctl(dev, DIOCGETRULESETS, &prs) == -1)
			errx(1, "%s", pf_strerror(errno));
		mnr = prs.nr;

		for (nr = 0; nr < mnr; ++nr) {
			prs.nr = nr;
			if (ioctl(dev, DIOCGETRULESET, &prs) == -1)
				errx(1, "%s", pf_strerror(errno));
			INDENT(depth, !(opts & PF_OPT_VERBOSE));
			printf("anchor \"%s\" all {\n", prs.name);
			pfctl_show_rules(dev, npath, opts,
			    format, prs.name, depth + 1, 0, shownr);
			INDENT(depth, !(opts & PF_OPT_VERBOSE));
			printf("}\n");
		}
		path[len] = '\0';
		return (0);
	}

 error:
	if (path != npath)
		free(npath);
	path[len] = '\0';
	return (ret);
}

int
pfctl_show_src_nodes(int dev, int opts)
{
	struct pfioc_src_nodes psn;
	struct pf_src_node *p;
	char *inbuf = NULL, *newinbuf = NULL;
	size_t i, len = 0;

	memset(&psn, 0, sizeof(psn));
	for (;;) {
		psn.psn_len = len;
		if (len) {
			newinbuf = realloc(inbuf, len);
			if (newinbuf == NULL)
				err(1, "realloc");
			psn.psn_buf = inbuf = newinbuf;
		}
		if (ioctl(dev, DIOCGETSRCNODES, &psn) == -1) {
			warn("DIOCGETSRCNODES");
			free(inbuf);
			return (-1);
		}
		if (psn.psn_len + sizeof(struct pfioc_src_nodes) < len)
			break;
		if (len == 0 && psn.psn_len == 0)
			goto done;
		if (len == 0 && psn.psn_len != 0)
			len = psn.psn_len;
		if (psn.psn_len == 0)
			goto done;	/* no src_nodes */
		len *= 2;
	}
	p = psn.psn_src_nodes;
	if (psn.psn_len > 0 && (opts & PF_OPT_SHOWALL))
		pfctl_print_title("SOURCE TRACKING NODES:");
	for (i = 0; i < psn.psn_len; i += sizeof(*p)) {
		print_src_node(p, opts);
		p++;
	}
done:
	free(inbuf);
	return (0);
}

int
pfctl_show_states(int dev, const char *iface, int opts, long shownr)
{
	struct pfioc_states ps;
	struct pfsync_state *p;
	char *inbuf = NULL, *newinbuf = NULL;
	size_t i, len = 0;
	int dotitle = (opts & PF_OPT_SHOWALL);

	memset(&ps, 0, sizeof(ps));
	for (;;) {
		ps.ps_len = len;
		if (len) {
			newinbuf = realloc(inbuf, len);
			if (newinbuf == NULL)
				err(1, "realloc");
			ps.ps_buf = inbuf = newinbuf;
		}
		if (ioctl(dev, DIOCGETSTATES, &ps) == -1) {
			warn("DIOCGETSTATES");
			free(inbuf);
			return (-1);
		}
		if (ps.ps_len + sizeof(struct pfioc_states) < len)
			break;
		if (len == 0 && ps.ps_len == 0)
			goto done;
		if (len == 0 && ps.ps_len != 0)
			len = ps.ps_len;
		if (ps.ps_len == 0)
			goto done;	/* no states */
		len *= 2;
	}
	p = ps.ps_states;
	for (i = 0; i < ps.ps_len; i += sizeof(*p), p++) {
		if (iface != NULL && strcmp(p->ifname, iface))
			continue;
		if (dotitle) {
			pfctl_print_title("STATES:");
			dotitle = 0;
		}
		if (shownr < 0 || ntohl(p->rule) == shownr)
			print_state(p, opts);
	}
done:
	free(inbuf);
	return (0);
}

int
pfctl_show_status(int dev, int opts)
{
	struct pf_status status;
	struct pfctl_watermarks wats;
	struct pfioc_synflwats iocwats;

	if (ioctl(dev, DIOCGETSTATUS, &status) == -1) {
		warn("DIOCGETSTATUS");
		return (-1);
	}
	if (ioctl(dev, DIOCGETSYNFLWATS, &iocwats) == -1) {
		warn("DIOCGETSYNFLWATS");
		return (-1);
	}
	wats.hi = iocwats.hiwat;
	wats.lo = iocwats.lowat;
	if (opts & PF_OPT_SHOWALL)
		pfctl_print_title("INFO:");
	print_status(&status, &wats, opts);
	return (0);
}

int
pfctl_show_timeouts(int dev, int opts)
{
	struct pfioc_tm pt;
	int i;

	if (opts & PF_OPT_SHOWALL)
		pfctl_print_title("TIMEOUTS:");
	memset(&pt, 0, sizeof(pt));
	for (i = 0; pf_timeouts[i].name; i++) {
		pt.timeout = pf_timeouts[i].timeout;
		if (ioctl(dev, DIOCGETTIMEOUT, &pt) == -1)
			err(1, "DIOCGETTIMEOUT");
		printf("%-20s %10d", pf_timeouts[i].name, pt.seconds);
		if (pf_timeouts[i].timeout >= PFTM_ADAPTIVE_START &&
		    pf_timeouts[i].timeout <= PFTM_ADAPTIVE_END)
			printf(" states");
		else
			printf("s");
		printf("\n");
	}
	return (0);

}

int
pfctl_show_limits(int dev, int opts)
{
	struct pfioc_limit pl;
	int i;

	if (opts & PF_OPT_SHOWALL)
		pfctl_print_title("LIMITS:");
	memset(&pl, 0, sizeof(pl));
	for (i = 0; pf_limits[i].name; i++) {
		pl.index = pf_limits[i].index;
		if (ioctl(dev, DIOCGETLIMIT, &pl) == -1)
			err(1, "DIOCGETLIMIT");
		printf("%-13s ", pf_limits[i].name);
		if (pl.limit == UINT_MAX)
			printf("unlimited\n");
		else
			printf("hard limit %8u\n", pl.limit);
	}
	return (0);
}

void
pfctl_read_limits(int dev)
{
	struct pfioc_limit pl;
	int i;

	for (i = 0; pf_limits[i].name; i++) {
		pl.index = pf_limits[i].index;
		if (ioctl(dev, DIOCGETLIMIT, &pl) == -1)
			err(1, "DIOCGETLIMIT");
		limit_curr[i] = pl.limit;
	}
}

void
pfctl_restore_limits(void)
{
	struct pfioc_limit pl;
	int i;

	if (dev == -1)
		return;

	for (i = 0; pf_limits[i].name; i++) {
		pl.index = pf_limits[i].index;
		pl.limit = limit_curr[i];
		if (ioctl(dev, DIOCSETLIMIT, &pl) == -1)
			warn("DIOCSETLIMIT (%s)", pf_limits[i].name);
	}
}

/* callbacks for rule/nat/rdr/addr */
void
pfctl_add_rule(struct pfctl *pf, struct pf_rule *r)
{
	struct pf_rule		*rule;
	struct pf_ruleset	*rs;

	rs = &pf->anchor->ruleset;

	if ((rule = calloc(1, sizeof(*rule))) == NULL)
		err(1, "calloc");
	bcopy(r, rule, sizeof(*rule));

	TAILQ_INSERT_TAIL(rs->rules.active.ptr, rule, entries);
}

int
pfctl_ruleset_trans(struct pfctl *pf, char *path, struct pf_anchor *a)
{
	int osize = pf->trans->pfrb_size;

	if (pfctl_add_trans(pf->trans, PF_TRANS_RULESET, path))
		return (3);
	if (pfctl_add_trans(pf->trans, PF_TRANS_TABLE, path))
		return (4);
	if (pfctl_trans(pf->dev, pf->trans, DIOCXBEGIN, osize))
		return (5);

	return (0);
}

int
pfctl_add_queue(struct pfctl *pf, struct pf_queuespec *q)
{
	struct pfctl_qsitem	*qi;

	if (pf->anchor->name[0]) {
		printf("must not have queue definitions in an anchor\n");
		return (1);
	}

	if (q->parent[0] == '\0') {
		TAILQ_FOREACH(qi, &rootqs, entries) {
			if (strcmp(q->ifname, qi->qs.ifname))
			    continue;
			printf("A root queue is already defined on %s\n",
			    qi->qs.ifname);
			return (1);
		}
	}

	if ((qi = calloc(1, sizeof(*qi))) == NULL)
		err(1, "calloc");
	bcopy(q, &qi->qs, sizeof(qi->qs));
	TAILQ_INIT(&qi->children);

	if (qi->qs.parent[0])
		TAILQ_INSERT_TAIL(&qspecs, qi, entries);
	else
		TAILQ_INSERT_TAIL(&rootqs, qi, entries);

	return (0);
}

struct pfctl_qsitem *
pfctl_find_queue(char *what, struct pf_qihead *where)
{
	struct pfctl_qsitem *q;

	TAILQ_FOREACH(q, where, entries)
		if (strcmp(q->qs.qname, what) == 0)
			return (q);

	return (NULL);
}

u_int
pfctl_find_childqs(struct pfctl_qsitem *qi)
{
	struct pfctl_qsitem	*n, *p, *q;
	u_int			 flags = qi->qs.flags;

	TAILQ_FOREACH(p, &qspecs, entries) {
		if (strcmp(p->qs.parent, qi->qs.qname))
			continue;
		if (p->qs.ifname[0] && strcmp(p->qs.ifname, qi->qs.ifname))
			continue;
		if (++p->matches > 10000)
			errx(1, "pfctl_find_childqs: excessive matches, loop?");

		if ((q = pfctl_find_queue(p->qs.qname, &qi->children)) == NULL) {
			/* insert */
			if ((n = calloc(1, sizeof(*n))) == NULL)
				err(1, "calloc");
			TAILQ_INIT(&n->children);
			bcopy(&p->qs, &n->qs, sizeof(n->qs));
			TAILQ_INSERT_TAIL(&qi->children, n, entries);
		} else {
			if ((q->qs.ifname[0] && p->qs.ifname[0]))
				errx(1, "queue %s on %s respecified",
				    q->qs.qname, q->qs.ifname);
			if (!q->qs.ifname[0] && !p->qs.ifname[0])
				errx(1, "queue %s respecified",
				    q->qs.qname);
			/* ifbound beats floating */
			if (!q->qs.ifname[0])
				bcopy(&p->qs, &q->qs, sizeof(q->qs));
		}
	}

	TAILQ_FOREACH(p, &qi->children, entries)
		flags |= pfctl_find_childqs(p);

	if (!TAILQ_EMPTY(&qi->children)) {
		if (qi->qs.flags & PFQS_DEFAULT)
			errx(1, "default queue %s is not a leaf queue",
			    qi->qs.qname);
		if (qi->qs.flags & PFQS_FLOWQUEUE)
			errx(1, "flow queue %s is not a leaf queue",
			    qi->qs.qname);
	}

	return (flags);
}

void
pfctl_load_queue(struct pfctl *pf, u_int32_t ticket, struct pfctl_qsitem *qi)
{
	struct pfioc_queue	 q;
	struct pfctl_qsitem	*p;

	q.ticket = ticket;
	bcopy(&qi->qs, &q.queue, sizeof(q.queue));
	if ((pf->opts & PF_OPT_NOACTION) == 0)
		if (ioctl(pf->dev, DIOCADDQUEUE, &q) == -1)
			err(1, "DIOCADDQUEUE");
	if (pf->opts & PF_OPT_VERBOSE)
		print_queuespec(&qi->qs);

	TAILQ_FOREACH(p, &qi->children, entries) {
		strlcpy(p->qs.ifname, qi->qs.ifname, IFNAMSIZ);
		pfctl_load_queue(pf, ticket, p);
	}
}

int
pfctl_load_queues(struct pfctl *pf)
{
	struct pfctl_qsitem	*qi, *tempqi;
	struct pf_queue_scspec	*rtsc, *lssc, *ulsc;
	u_int32_t		 ticket;

	TAILQ_FOREACH(qi, &qspecs, entries) {
		if (qi->matches == 0)
			errx(1, "queue %s: parent %s not found", qi->qs.qname,
			    qi->qs.parent);

		rtsc = &qi->qs.realtime;
		lssc = &qi->qs.linkshare;
		ulsc = &qi->qs.upperlimit;

		if (rtsc->m1.percent || rtsc->m2.percent ||
		    lssc->m1.percent || lssc->m2.percent ||
		    ulsc->m1.percent || ulsc->m2.percent)
			errx(1, "only absolute bandwidth specs for now");

		/* Link sharing policy must be specified for child classes */
		if (qi->qs.parent[0] != '\0' &&
		    lssc->m1.absolute == 0 && lssc->m2.absolute == 0)
			errx(1, "queue %s: no bandwidth was specified",
			    qi->qs.qname);
	}

	if ((pf->opts & PF_OPT_NOACTION) == 0)
		ticket = pfctl_get_ticket(pf->trans, PF_TRANS_RULESET, "");

	TAILQ_FOREACH_SAFE(qi, &rootqs, entries, tempqi) {
		TAILQ_REMOVE(&rootqs, qi, entries);
		pfctl_load_queue(pf, ticket, qi);
		TAILQ_INSERT_HEAD(&rootqs, qi, entries);
	}

	return (0);
}

void
pfctl_clear_queues(struct pf_qihead *head)
{
	struct pfctl_qsitem *qi;

	while ((qi = TAILQ_FIRST(head)) != NULL) {
		TAILQ_REMOVE(head, qi, entries);
		pfctl_clear_queues(&qi->children);
		free(qi);
	}
}

u_int
pfctl_leafqueue_check(char *qname)
{
	struct pfctl_qsitem	*qi;
	if (qname == NULL || qname[0] == 0)
		return (0);

	TAILQ_FOREACH(qi, &rootqs, entries) {
		if (strcmp(qname, qi->qs.qname))
			continue;
		if (!TAILQ_EMPTY(&qi->children)) {
			printf("queue %s: packets must be assigned to leaf "
			    "queues only\n", qname);
			return (1);
		}
	}
	TAILQ_FOREACH(qi, &qspecs, entries) {
		if (strcmp(qname, qi->qs.qname))
			continue;
		if (!TAILQ_EMPTY(&qi->children)) {
			printf("queue %s: packets must be assigned to leaf "
			    "queues only\n", qname);
			return (1);
		}
	}
	return (0);
}

u_int
pfctl_check_qassignments(struct pf_ruleset *rs)
{
	struct pf_rule		*r;
	struct pfctl_qsitem	*qi;
	u_int			 flags, errs = 0;

	/* main ruleset: need find_childqs to populate qi->children */
	if (rs->anchor->path[0] == 0) {
		TAILQ_FOREACH(qi, &rootqs, entries) {
			flags = pfctl_find_childqs(qi);
			if (!(qi->qs.flags & PFQS_ROOTCLASS) &&
			    !TAILQ_EMPTY(&qi->children)) {
				if (qi->qs.flags & PFQS_FLOWQUEUE)
					errx(1, "root queue %s doesn't "
					    "support hierarchy",
					    qi->qs.qname);
				else
					errx(1, "no bandwidth was specified "
					    "for root queue %s", qi->qs.qname);
			}
			if ((qi->qs.flags & PFQS_ROOTCLASS) &&
			    !(flags & PFQS_DEFAULT))
				errx(1, "no default queue specified");
		}
	}

	TAILQ_FOREACH(r, rs->rules.active.ptr, entries) {
		if (r->anchor)
			errs += pfctl_check_qassignments(&r->anchor->ruleset);
		if (pfctl_leafqueue_check(r->qname) ||
		    pfctl_leafqueue_check(r->pqname))
			errs++;
	}
	return (errs);
}

static int
pfctl_load_tables(struct pfctl *pf, char *path, struct pf_anchor *a)
{
	struct pfr_ktable *kt, *ktw;
	struct pfr_uktable *ukt;
	uint32_t ticket;
	char anchor_path[PF_ANCHOR_MAXPATH];
	int e;

	RB_FOREACH_SAFE(kt, pfr_ktablehead, &pfr_ktables, ktw) {
		if (strcmp(kt->pfrkt_anchor, a->path) != 0)
			continue;

		if (path != NULL && *path) {
			strlcpy(anchor_path, kt->pfrkt_anchor,
			    sizeof (anchor_path));
			snprintf(kt->pfrkt_anchor, PF_ANCHOR_MAXPATH, "%s/%s",
			    path, anchor_path);
		}
		ukt = (struct pfr_uktable *) kt;
		ticket = pfctl_get_ticket(pf->trans, PF_TRANS_TABLE, path);
		e = pfr_ina_define(&ukt->pfrukt_t, ukt->pfrukt_addrs.pfrb_caddr,
		    ukt->pfrukt_addrs.pfrb_size, NULL, NULL, ticket,
		    ukt->pfrukt_init_addr ? PFR_FLAG_ADDRSTOO : 0);
		if (e != 0)
			err(1, "%s pfr_ina_define() %s@%s", __func__,
			    kt->pfrkt_name, kt->pfrkt_anchor);
		RB_REMOVE(pfr_ktablehead, &pfr_ktables, kt);
		pfr_buf_clear(&ukt->pfrukt_addrs);
		free(ukt);
	}

	return (0);
}

int
pfctl_load_ruleset(struct pfctl *pf, char *path, struct pf_ruleset *rs,
    int depth)
{
	struct pf_rule *r;
	int		error, len = strlen(path);
	int		brace = 0;
	unsigned int	rno = 0;

	pf->anchor = rs->anchor;

	if (path[0])
		snprintf(&path[len], PATH_MAX - len, "/%s", pf->anchor->name);
	else
		snprintf(&path[len], PATH_MAX - len, "%s", pf->anchor->path);

	if (depth) {
		if (TAILQ_FIRST(rs->rules.active.ptr) != NULL) {
			brace++;
			if (pf->opts & PF_OPT_VERBOSE)
				printf(" {\n");
			if ((pf->opts & PF_OPT_NOACTION) == 0 &&
			    (error = pfctl_ruleset_trans(pf,
			    path, rs->anchor))) {
				printf("pfctl_load_ruleset: "
				    "pfctl_ruleset_trans %d\n", error);
				goto error;
			}
		} else if (pf->opts & PF_OPT_VERBOSE)
			printf("\n");
	}

	if (pf->optimize)
		pfctl_optimize_ruleset(pf, rs);

	while ((r = TAILQ_FIRST(rs->rules.active.ptr)) != NULL) {
		TAILQ_REMOVE(rs->rules.active.ptr, r, entries);
		pfctl_expand_label_nr(r, rno);
		rno++;
		if ((error = pfctl_load_rule(pf, path, r, depth)))
			goto error;
		if (r->anchor) {
			if ((error = pfctl_load_ruleset(pf, path,
			    &r->anchor->ruleset, depth + 1)))
				goto error;
			if ((error = pfctl_load_tables(pf, path, r->anchor)))
				goto error;
		} else if (pf->opts & PF_OPT_VERBOSE)
			printf("\n");
		free(r);
	}
	if (brace && pf->opts & PF_OPT_VERBOSE) {
		INDENT(depth - 1, (pf->opts & PF_OPT_VERBOSE));
		printf("}\n");
	}
	path[len] = '\0';
	return (0);

 error:
	path[len] = '\0';
	return (error);

}

int
pfctl_load_rule(struct pfctl *pf, char *path, struct pf_rule *r, int depth)
{
	char			*name;
	struct pfioc_rule	pr;
	int			len = strlen(path);

	bzero(&pr, sizeof(pr));
	/* set up anchor before adding to path for anchor_call */
	if ((pf->opts & PF_OPT_NOACTION) == 0) {
		if (pf->trans == NULL)
			errx(1, "pfctl_load_rule: no transaction");
		pr.ticket = pfctl_get_ticket(pf->trans, PF_TRANS_RULESET, path);
	}
	if (strlcpy(pr.anchor, path, sizeof(pr.anchor)) >= sizeof(pr.anchor))
		errx(1, "pfctl_load_rule: strlcpy");

	if (r->anchor) {
		if (r->anchor->match) {
			if (path[0])
				snprintf(&path[len], PATH_MAX - len,
				    "/%s", r->anchor->name);
			else
				snprintf(&path[len], PATH_MAX - len,
				    "%s", r->anchor->name);
			name = r->anchor->name;
		} else
			name = r->anchor->path;
	} else
		name = "";

	if ((pf->opts & PF_OPT_NOACTION) == 0) {
		memcpy(&pr.rule, r, sizeof(pr.rule));
		if (r->anchor && strlcpy(pr.anchor_call, name,
		    sizeof(pr.anchor_call)) >= sizeof(pr.anchor_call))
			errx(1, "pfctl_load_rule: strlcpy");
		if (ioctl(pf->dev, DIOCADDRULE, &pr) == -1)
			err(1, "DIOCADDRULE");
	}

	if (pf->opts & PF_OPT_VERBOSE) {
		INDENT(depth, !(pf->opts & PF_OPT_VERBOSE2));
		print_rule(r, name, pf->opts);
	}
	path[len] = '\0';
	return (0);
}

int
pfctl_rules(int dev, char *filename, int opts, int optimize,
    char *anchorname, struct pfr_buffer *trans)
{
#define ERR(...) do { warn(__VA_ARGS__); goto _error; } while(0)
#define ERRX(...) do { warnx(__VA_ARGS__); goto _error; } while(0)

	struct pfr_buffer	*t, buf;
	struct pfctl		 pf;
	struct pf_ruleset	*rs;
	struct pfr_table	 trs;
	char			*path = NULL;
	int			 osize;
	char			*p;

	RB_INIT(&pf_anchors);
	memset(&pf_main_anchor, 0, sizeof(pf_main_anchor));
	pf_init_ruleset(&pf_main_anchor.ruleset);
	memset(&pf, 0, sizeof(pf));
	memset(&trs, 0, sizeof(trs));

	if (trans == NULL) {
		bzero(&buf, sizeof(buf));
		buf.pfrb_type = PFRB_TRANS;
		pf.trans = &buf;
		t = &buf;
		osize = 0;
	} else {
		t = trans;
		osize = t->pfrb_size;
	}

	if ((path = calloc(1, PATH_MAX)) == NULL)
		ERR("%s: calloc", __func__);
	if (strlcpy(trs.pfrt_anchor, anchorname,
	    sizeof(trs.pfrt_anchor)) >= sizeof(trs.pfrt_anchor))
		ERRX("%s: strlcpy", __func__);
	pf.dev = dev;
	pf.opts = opts;
	pf.optimize = optimize;

	/* non-brace anchor, create without resolving the path */
	if ((pf.anchor = calloc(1, sizeof(*pf.anchor))) == NULL)
		ERR("%s: calloc", __func__);
	rs = &pf.anchor->ruleset;
	pf_init_ruleset(rs);
	rs->anchor = pf.anchor;
	if (strlcpy(pf.anchor->path, anchorname,
	    sizeof(pf.anchor->path)) >= sizeof(pf.anchor->path))
		errx(1, "%s: strlcpy", __func__);

	if ((p = strrchr(anchorname, '/')) != NULL) {
		if (strlen(p) == 1)
			errx(1, "%s: bad anchor name %s", __func__, anchorname);
	} else
		p = anchorname;

	if (strlcpy(pf.anchor->name, p,
	    sizeof(pf.anchor->name)) >= sizeof(pf.anchor->name))
		errx(1, "%s: strlcpy", __func__);

	pf.astack[0] = pf.anchor;
	pf.asd = 0;
	pf.trans = t;
	pfctl_init_options(&pf);

	if ((opts & PF_OPT_NOACTION) == 0) {
		/*
		 * XXX For the time being we need to open transactions for
		 * the main ruleset before parsing, because tables are still
		 * loaded at parse time.
		 */
		if (pfctl_ruleset_trans(&pf, anchorname, pf.anchor))
			ERRX("pfctl_rules");
		pf.astack[0]->ruleset.tticket =
		    pfctl_get_ticket(t, PF_TRANS_TABLE, anchorname);
	}

	if (parse_config(filename, &pf) < 0) {
		if ((opts & PF_OPT_NOACTION) == 0)
			ERRX("Syntax error in config file: "
			    "pf rules not loaded");
		else
			goto _error;
	}

	if (!anchorname[0] && (pfctl_check_qassignments(&pf.anchor->ruleset) ||
	    pfctl_load_queues(&pf))) {
		if ((opts & PF_OPT_NOACTION) == 0)
			ERRX("Unable to load queues into kernel");
		else
			goto _error;
	}

	if (pfctl_load_ruleset(&pf, path, rs, 0)) {
		if ((opts & PF_OPT_NOACTION) == 0)
			ERRX("Unable to load rules into kernel");
		else
			goto _error;
	}

	free(path);
	path = NULL;

	if (trans == NULL) {
		/*
		 * process "load anchor" directives that might have used queues
		 */
		if (pfctl_load_anchors(dev, &pf) == -1)
			ERRX("load anchors");
		pfctl_clear_queues(&qspecs);
		pfctl_clear_queues(&rootqs);

		if ((opts & PF_OPT_NOACTION) == 0) {
			if (!anchorname[0] && pfctl_load_options(&pf))
				goto _error;
			if (pfctl_trans(dev, t, DIOCXCOMMIT, osize))
				ERR("DIOCXCOMMIT");
		}
	}
	return (0);

_error:
	if (trans == NULL) {	/* main ruleset */
		if ((opts & PF_OPT_NOACTION) == 0)
			if (pfctl_trans(dev, t, DIOCXROLLBACK, osize))
				err(1, "DIOCXROLLBACK");
		exit(1);
	} else {		/* sub ruleset */
		free(path);
		return (-1);
	}

#undef ERR
#undef ERRX
}

FILE *
pfctl_fopen(const char *name, const char *mode)
{
	struct stat	 st;
	FILE		*fp;

	fp = fopen(name, mode);
	if (fp == NULL)
		return (NULL);
	if (fstat(fileno(fp), &st) == -1) {
		fclose(fp);
		return (NULL);
	}
	if (S_ISDIR(st.st_mode)) {
		fclose(fp);
		errno = EISDIR;
		return (NULL);
	}
	return (fp);
}

void
pfctl_init_options(struct pfctl *pf)
{
	int64_t mem;
	int mib[2], mcl;
	size_t size;

	pf->timeout[PFTM_TCP_FIRST_PACKET] = PFTM_TCP_FIRST_PACKET_VAL;
	pf->timeout[PFTM_TCP_OPENING] = PFTM_TCP_OPENING_VAL;
	pf->timeout[PFTM_TCP_ESTABLISHED] = PFTM_TCP_ESTABLISHED_VAL;
	pf->timeout[PFTM_TCP_CLOSING] = PFTM_TCP_CLOSING_VAL;
	pf->timeout[PFTM_TCP_FIN_WAIT] = PFTM_TCP_FIN_WAIT_VAL;
	pf->timeout[PFTM_TCP_CLOSED] = PFTM_TCP_CLOSED_VAL;
	pf->timeout[PFTM_UDP_FIRST_PACKET] = PFTM_UDP_FIRST_PACKET_VAL;
	pf->timeout[PFTM_UDP_SINGLE] = PFTM_UDP_SINGLE_VAL;
	pf->timeout[PFTM_UDP_MULTIPLE] = PFTM_UDP_MULTIPLE_VAL;
	pf->timeout[PFTM_ICMP_FIRST_PACKET] = PFTM_ICMP_FIRST_PACKET_VAL;
	pf->timeout[PFTM_ICMP_ERROR_REPLY] = PFTM_ICMP_ERROR_REPLY_VAL;
	pf->timeout[PFTM_OTHER_FIRST_PACKET] = PFTM_OTHER_FIRST_PACKET_VAL;
	pf->timeout[PFTM_OTHER_SINGLE] = PFTM_OTHER_SINGLE_VAL;
	pf->timeout[PFTM_OTHER_MULTIPLE] = PFTM_OTHER_MULTIPLE_VAL;
	pf->timeout[PFTM_FRAG] = PFTM_FRAG_VAL;
	pf->timeout[PFTM_INTERVAL] = PFTM_INTERVAL_VAL;
	pf->timeout[PFTM_SRC_NODE] = PFTM_SRC_NODE_VAL;
	pf->timeout[PFTM_TS_DIFF] = PFTM_TS_DIFF_VAL;
	pf->timeout[PFTM_ADAPTIVE_START] = PFSTATE_ADAPT_START;
	pf->timeout[PFTM_ADAPTIVE_END] = PFSTATE_ADAPT_END;

	pf->limit[PF_LIMIT_STATES] = PFSTATE_HIWAT;

	pf->syncookieswat[0] = PF_SYNCOOKIES_LOWATPCT;
	pf->syncookieswat[1] = PF_SYNCOOKIES_HIWATPCT;

	/*
	 * limit_curr is populated by pfctl_read_limits() after main() opens
	 * /dev/pf.
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_MAXCLUSTERS;
	size = sizeof(mcl);
	if (sysctl(mib, 2, &mcl, &size, NULL, 0) == -1)
		err(1, "sysctl");
	pf->limit[PF_LIMIT_FRAGS] = (limit_curr[PF_LIMIT_FRAGS] == 0) ?
	    mcl / 4 : limit_curr[PF_LIMIT_FRAGS];

	pf->limit[PF_LIMIT_SRC_NODES] = (limit_curr[PF_LIMIT_SRC_NODES] == 0) ?
	    PFSNODE_HIWAT : limit_curr[PF_LIMIT_SRC_NODES];
	pf->limit[PF_LIMIT_TABLES] = (limit_curr[PF_LIMIT_TABLES] == 0) ?
	    PFR_KTABLE_HIWAT : limit_curr[PF_LIMIT_TABLES];
	pf->limit[PF_LIMIT_TABLE_ENTRIES] =
	    (limit_curr[PF_LIMIT_TABLE_ENTRIES] == 0) ?
		PFR_KENTRY_HIWAT : limit_curr[PF_LIMIT_TABLE_ENTRIES];
	pf->limit[PF_LIMIT_PKTDELAY_PKTS] =
	    (limit_curr[PF_LIMIT_PKTDELAY_PKTS] == 0) ?
		PF_PKTDELAY_MAXPKTS : limit_curr[PF_LIMIT_PKTDELAY_PKTS];
	pf->limit[PF_LIMIT_ANCHORS] = (limit_curr[PF_LIMIT_ANCHORS] == 0) ?
	    PF_ANCHOR_HIWAT : limit_curr[PF_LIMIT_ANCHORS];

	mib[0] = CTL_HW;
	mib[1] = HW_PHYSMEM64;
	size = sizeof(mem);
	if (sysctl(mib, 2, &mem, &size, NULL, 0) == -1)
		err(1, "sysctl");
	if (mem <= 100*1024*1024)
		pf->limit[PF_LIMIT_TABLE_ENTRIES] = PFR_KENTRY_HIWAT_SMALL;

	pf->debug = LOG_ERR;
	pf->debug_set = 0;
	pf->reassemble = PF_REASS_ENABLED;
}

int
pfctl_load_options(struct pfctl *pf)
{
	int i, error = 0;

	/* load limits */
	for (i = 0; i < PF_LIMIT_MAX; i++)
		if (pfctl_load_limit(pf, i, pf->limit[i]))
			error = 1;

	/*
	 * If we've set the states limit, but haven't explicitly set adaptive
	 * timeouts, do it now with a start of 60% and end of 120%.
	 */
	if (pf->limit_set[PF_LIMIT_STATES] &&
	    !pf->timeout_set[PFTM_ADAPTIVE_START] &&
	    !pf->timeout_set[PFTM_ADAPTIVE_END]) {
		pf->timeout[PFTM_ADAPTIVE_START] =
			(pf->limit[PF_LIMIT_STATES] / 10) * 6;
		pf->timeout_set[PFTM_ADAPTIVE_START] = 1;
		pf->timeout[PFTM_ADAPTIVE_END] =
			(pf->limit[PF_LIMIT_STATES] / 10) * 12;
		pf->timeout_set[PFTM_ADAPTIVE_END] = 1;
	}

	/* load timeouts */
	for (i = 0; i < PFTM_MAX; i++)
		if (pfctl_load_timeout(pf, i, pf->timeout[i]))
			error = 1;

	/* load debug */
	if (pf->debug_set && pfctl_load_debug(pf, pf->debug))
		error = 1;

	/* load logif */
	if (pf->ifname_set && pfctl_load_logif(pf, pf->ifname))
		error = 1;

	/* load hostid */
	if (pf->hostid_set && pfctl_load_hostid(pf, pf->hostid))
		error = 1;

	/* load reassembly settings */
	if (pf->reass_set && pfctl_load_reassembly(pf, pf->reassemble))
		error = 1;

	/* load syncookies settings */
	if (pf->syncookies_set && pfctl_load_syncookies(pf, pf->syncookies))
		error = 1;
	if (pf->syncookieswat_set) {
		struct pfioc_limit pl;
		unsigned curlim;

		if (pf->limit_set[PF_LIMIT_STATES])
			curlim = pf->limit[PF_LIMIT_STATES];
		else {
			memset(&pl, 0, sizeof(pl));
			pl.index = pf_limits[PF_LIMIT_STATES].index;
			if (ioctl(dev, DIOCGETLIMIT, &pl) == -1)
				err(1, "DIOCGETLIMIT");
			curlim = pl.limit;
		}
		if (pfctl_set_synflwats(pf, curlim * pf->syncookieswat[0]/100,
		    curlim * pf->syncookieswat[1]/100))
			error = 1;
	}

	return (error);
}

int
pfctl_set_limit(struct pfctl *pf, const char *opt, unsigned int limit)
{
	int i;

	for (i = 0; pf_limits[i].name; i++) {
		if (strcasecmp(opt, pf_limits[i].name) == 0) {
			pf->limit[pf_limits[i].index] = limit;
			pf->limit_set[pf_limits[i].index] = 1;
			break;
		}
	}
	if (pf_limits[i].name == NULL) {
		warnx("Bad pool name.");
		return (1);
	}

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set limit %s %d\n", opt, limit);

	if ((pf->opts & PF_OPT_NOACTION) == 0)
		pfctl_load_options(pf);

	return (0);
}

int
pfctl_load_limit(struct pfctl *pf, unsigned int index, unsigned int limit)
{
	struct pfioc_limit pl;

	memset(&pl, 0, sizeof(pl));
	pl.index = index;
	pl.limit = limit;
	if (ioctl(pf->dev, DIOCSETLIMIT, &pl) == -1) {
		if (errno == EBUSY)
			warnx("Current pool size exceeds requested %s limit %u",
			    pf_limits[index].name, limit);
		else
			warnx("Cannot set %s limit to %u",
			    pf_limits[index].name, limit);
		return (1);
	}
	return (0);
}

int
pfctl_set_timeout(struct pfctl *pf, const char *opt, int seconds, int quiet)
{
	int i;

	for (i = 0; pf_timeouts[i].name; i++) {
		if (strcasecmp(opt, pf_timeouts[i].name) == 0) {
			pf->timeout[pf_timeouts[i].timeout] = seconds;
			pf->timeout_set[pf_timeouts[i].timeout] = 1;
			break;
		}
	}

	if (pf_timeouts[i].name == NULL) {
		warnx("Bad timeout name.");
		return (1);
	}


	if (pf->opts & PF_OPT_VERBOSE && ! quiet)
		printf("set timeout %s %d\n", opt, seconds);

	return (0);
}

int
pfctl_load_timeout(struct pfctl *pf, unsigned int timeout, unsigned int seconds)
{
	struct pfioc_tm pt;

	memset(&pt, 0, sizeof(pt));
	pt.timeout = timeout;
	pt.seconds = seconds;
	if (ioctl(pf->dev, DIOCSETTIMEOUT, &pt) == -1) {
		warnx("DIOCSETTIMEOUT");
		return (1);
	}
	return (0);
}

int
pfctl_set_synflwats(struct pfctl *pf, u_int32_t lowat, u_int32_t hiwat)
{
	struct pfioc_synflwats ps;

	memset(&ps, 0, sizeof(ps));
	ps.hiwat = hiwat;
	ps.lowat = lowat;

	if (ioctl(pf->dev, DIOCSETSYNFLWATS, &ps) == -1) {
		warnx("Cannot set synflood detection watermarks");
		return (1);
	}
	return (0);
}

int
pfctl_set_reassembly(struct pfctl *pf, int on, int nodf)
{
	pf->reass_set = 1;
	if (on) {
		pf->reassemble = PF_REASS_ENABLED;
		if (nodf)
			pf->reassemble |= PF_REASS_NODF;
	} else {
		pf->reassemble = 0;
	}

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set reassemble %s %s\n", on ? "yes" : "no",
		    nodf ? "no-df" : "");

	return (0);
}

int
pfctl_set_syncookies(struct pfctl *pf, u_int8_t val, struct pfctl_watermarks *w)
{
	if (val != PF_SYNCOOKIES_ADAPTIVE && w != NULL) {
		warnx("syncookies start/end only apply to adaptive");
		return (1);
	}
	if (val == PF_SYNCOOKIES_ADAPTIVE && w != NULL) {
		if (!w->hi)
			w->hi = PF_SYNCOOKIES_HIWATPCT;
		if (!w->lo)
			w->lo = w->hi / 2;
		if (w->lo >= w->hi) {
			warnx("start must be higher than end");
			return (1);
		}
		pf->syncookieswat[0] = w->lo;
		pf->syncookieswat[1] = w->hi;
		pf->syncookieswat_set = 1;
	}

	if (pf->opts & PF_OPT_VERBOSE) {
		if (val == PF_SYNCOOKIES_NEVER)
			printf("set syncookies never\n");
		else if (val == PF_SYNCOOKIES_ALWAYS)
			printf("set syncookies always\n");
		else if (val == PF_SYNCOOKIES_ADAPTIVE) {
			if (pf->syncookieswat_set)
				printf("set syncookies adaptive (start %u%%, "
				    "end %u%%)\n", pf->syncookieswat[1],
				    pf->syncookieswat[0]);
			else
				printf("set syncookies adaptive\n");
		} else {	/* cannot happen */
			warnx("king bula ate all syncookies");
			return (1);
		}
	}

	pf->syncookies_set = 1;
	pf->syncookies = val;
	return (0);
}

int
pfctl_set_optimization(struct pfctl *pf, const char *opt)
{
	const struct pf_hint *hint;
	int i, r;

	for (i = 0; pf_hints[i].name; i++)
		if (strcasecmp(opt, pf_hints[i].name) == 0)
			break;

	hint = pf_hints[i].hint;
	if (hint == NULL) {
		warnx("invalid state timeouts optimization");
		return (1);
	}

	for (i = 0; hint[i].name; i++)
		if ((r = pfctl_set_timeout(pf, hint[i].name,
		    hint[i].timeout, 1)))
			return (r);

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set optimization %s\n", opt);

	return (0);
}

int
pfctl_set_logif(struct pfctl *pf, char *ifname)
{
	if (!strcmp(ifname, "none")) {
		free(pf->ifname);
		pf->ifname = NULL;
	} else {
		pf->ifname = strdup(ifname);
		if (!pf->ifname)
			errx(1, "pfctl_set_logif: strdup");
	}
	pf->ifname_set = 1;

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set loginterface %s\n", ifname);

	return (0);
}

int
pfctl_load_logif(struct pfctl *pf, char *ifname)
{
	struct pfioc_iface	pi;

	memset(&pi, 0, sizeof(pi));
	if (ifname && strlcpy(pi.pfiio_name, ifname,
	    sizeof(pi.pfiio_name)) >= sizeof(pi.pfiio_name)) {
		warnx("pfctl_load_logif: strlcpy");
		return (1);
	}
	if (ioctl(pf->dev, DIOCSETSTATUSIF, &pi) == -1) {
		warnx("DIOCSETSTATUSIF");
		return (1);
	}
	return (0);
}

void
pfctl_set_hostid(struct pfctl *pf, u_int32_t hostid)
{
	HTONL(hostid);

	pf->hostid = hostid;
	pf->hostid_set = 1;

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set hostid 0x%08x\n", ntohl(hostid));
}

int
pfctl_load_hostid(struct pfctl *pf, u_int32_t hostid)
{
	if (ioctl(dev, DIOCSETHOSTID, &hostid) == -1) {
		warnx("DIOCSETHOSTID");
		return (1);
	}
	return (0);
}

int
pfctl_load_reassembly(struct pfctl *pf, u_int32_t reassembly)
{
	if (ioctl(dev, DIOCSETREASS, &reassembly) == -1) {
		warnx("DIOCSETREASS");
		return (1);
	}
	return (0);
}

int
pfctl_load_syncookies(struct pfctl *pf, u_int8_t val)
{
	if (ioctl(dev, DIOCSETSYNCOOKIES, &val) == -1) {
		warnx("DIOCSETSYNCOOKIES");
		return (1);
	}
	return (0);
}

int
pfctl_set_debug(struct pfctl *pf, char *d)
{
	u_int32_t	level;
	int		loglevel;

	if ((loglevel = string_to_loglevel(d)) >= 0)
		level = loglevel;
	else {
		warnx("unknown debug level \"%s\"", d);
		return (-1);
	}
	pf->debug = level;
	pf->debug_set = 1;

	if ((pf->opts & PF_OPT_NOACTION) == 0)
		if (ioctl(dev, DIOCSETDEBUG, &level) == -1)
			err(1, "DIOCSETDEBUG");

	if (pf->opts & PF_OPT_VERBOSE)
		printf("set debug %s\n", d);

	return (0);
}

int
pfctl_load_debug(struct pfctl *pf, unsigned int level)
{
	if (ioctl(pf->dev, DIOCSETDEBUG, &level) == -1) {
		warnx("DIOCSETDEBUG");
		return (1);
	}
	return (0);
}

int
pfctl_set_interface_flags(struct pfctl *pf, char *ifname, int flags, int how)
{
	struct pfioc_iface	pi;

	bzero(&pi, sizeof(pi));

	pi.pfiio_flags = flags;

	if (strlcpy(pi.pfiio_name, ifname, sizeof(pi.pfiio_name)) >=
	    sizeof(pi.pfiio_name))
		errx(1, "pfctl_set_interface_flags: strlcpy");

	if ((pf->opts & PF_OPT_NOACTION) == 0) {
		if (how == 0) {
			if (ioctl(pf->dev, DIOCCLRIFFLAG, &pi) == -1)
				err(1, "DIOCCLRIFFLAG");
		} else {
			if (ioctl(pf->dev, DIOCSETIFFLAG, &pi) == -1)
				err(1, "DIOCSETIFFLAG");
		}
	}
	return (0);
}

void
pfctl_debug(int dev, u_int32_t level, int opts)
{
	struct pfr_buffer t;

	memset(&t, 0, sizeof(t));
	t.pfrb_type = PFRB_TRANS;
	if (pfctl_trans(dev, &t, DIOCXBEGIN, 0) ||
	    ioctl(dev, DIOCSETDEBUG, &level) == -1||
	    pfctl_trans(dev, &t, DIOCXCOMMIT, 0))
		err(1, "pfctl_debug ioctl");

	if ((opts & PF_OPT_QUIET) == 0)
		fprintf(stderr, "debug level set to '%s'\n",
		    loglevel_to_string(level));
}

int
pfctl_walk_show(int opts, struct pfioc_ruleset *pr, void *warg)
{
	if (pr->path[0]) {
		if (pr->path[0] != '_' || (opts & PF_OPT_VERBOSE))
			printf("  %s/%s\n", pr->path, pr->name);
	} else if (pr->name[0] != '_' || (opts & PF_OPT_VERBOSE))
		printf("  %s\n", pr->name);

	return (0);
}

int
pfctl_walk_get(int opts, struct pfioc_ruleset *pr, void *warg)
{
	struct pfr_anchoritem	*pfra;
	struct pfr_anchors	*anchors;
	int			 e;

	anchors = (struct pfr_anchors *) warg;

	pfra = malloc(sizeof(*pfra));
	if (pfra == NULL)
		err(1, "%s", __func__);

	if (pr->path[0])
		e = asprintf(&pfra->pfra_anchorname, "%s/%s", pr->path,
		    pr->name);
	else
		e = asprintf(&pfra->pfra_anchorname, "%s", pr->name);

	if (e == -1)
		err(1, "%s", __func__);


	SLIST_INSERT_HEAD(anchors, pfra, pfra_sle);

	return (0);
}

int
pfctl_walk_anchors(int dev, int opts, const char *anchor,
    int(walkf)(int, struct pfioc_ruleset *, void *), void *warg)
{
	struct pfioc_ruleset	 pr;
	u_int32_t		 mnr, nr;

	memset(&pr, 0, sizeof(pr));
	strlcpy(pr.path, anchor, sizeof(pr.path));
	if (ioctl(dev, DIOCGETRULESETS, &pr) == -1)
		errx(1, "%s", pf_strerror(errno));
	mnr = pr.nr;
	for (nr = 0; nr < mnr; ++nr) {
		char sub[PATH_MAX];

		pr.nr = nr;
		if (ioctl(dev, DIOCGETRULESET, &pr) == -1)
			errx(1, "%s", pf_strerror(errno));
		if (!strcmp(pr.name, PF_RESERVED_ANCHOR))
			continue;
		sub[0] = '\0';

		if (walkf(opts, &pr, warg))
			return (-1);

		if (pr.path[0])
			snprintf(sub, sizeof(sub), "%s/%s",
			    pr.path, pr.name);
		else
			snprintf(sub, sizeof(sub), "%s",
			    pr.name);
		if (pfctl_walk_anchors(dev, opts, sub, walkf, warg))
			return (-1);
	}
	return (0);
}

int
pfctl_show_anchors(int dev, int opts, char *anchor)
{
	return (
	    pfctl_walk_anchors(dev, opts, anchor, pfctl_walk_show, NULL));
}

struct pfr_anchors *
pfctl_get_anchors(int dev, const char *anchor, int opts)
{
	struct pfioc_ruleset	pr;
	static struct pfr_anchors anchors;
	char anchorbuf[PATH_MAX];
	char *n;

	SLIST_INIT(&anchors);

	memset(&pr, 0, sizeof(pr));
	if (*anchor != '\0') {
		strlcpy(anchorbuf, anchor, sizeof(anchorbuf));
		n = dirname(anchorbuf);
		if (n[0] != '.' && n[1] != '\0')
			strlcpy(pr.path, n, sizeof(pr.path));
		strlcpy(anchorbuf, anchor, sizeof(anchorbuf));
		n = basename(anchorbuf);
		if (n != NULL)
			strlcpy(pr.name, n, sizeof(pr.name));
	}

	/* insert a root anchor first. */
	pfctl_walk_get(opts, &pr, &anchors);

	if (pfctl_walk_anchors(dev, opts, anchor, pfctl_walk_get, &anchors))
		errx(1,
		    "%s failed to retrieve list of anchors, can't continue",
		    __func__);

	return (&anchors);
}

int
pfctl_call_cleartables(int dev, int opts, struct pfr_anchoritem *pfra)
{
	/*
	 * PF_OPT_QUIET makes pfctl_clear_tables() to stop printing number of
	 * tables cleared for given anchor.
	 */
	opts |= PF_OPT_QUIET;
	return ((pfctl_clear_tables(pfra->pfra_anchorname, opts) == -1) ?
	    1 : 0);
}

int
pfctl_call_clearrules(int dev, int opts, struct pfr_anchoritem *pfra)
{
	/*
	 * PF_OPT_QUIET makes pfctl_clear_rules() to stop printing a 'rules
	 * cleared' message for every anchor it deletes.
	 */
	opts |= PF_OPT_QUIET;
	return (pfctl_clear_rules(dev, opts, pfra->pfra_anchorname));
}

int
pfctl_call_showtables(int dev, int opts, struct pfr_anchoritem *pfra)
{
	pfctl_show_tables(pfra->pfra_anchorname, opts);
	return (0);
}

int
pfctl_call_clearanchors(int dev, int opts, struct pfr_anchoritem *pfra)
{
	int	rv = 0;

	rv |= pfctl_call_cleartables(dev, opts, pfra);
	rv |= pfctl_call_clearrules(dev, opts, pfra);

	return (rv);
}

int
pfctl_recurse(int dev, int opts, const char *anchorname,
    int(*walkf)(int, int, struct pfr_anchoritem *))
{
	int			 rv = 0;
	struct pfr_anchors	*anchors;
	struct pfr_anchoritem	*pfra, *pfra_save;

	anchors = pfctl_get_anchors(dev, anchorname, opts);
	/*
	 * While traversing the list, pfctl_clear_*() must always return
	 * so that failures on one anchor do not prevent clearing others.
	 */
	opts |= PF_OPT_IGNFAIL;
	if ((opts & PF_OPT_CALLSHOW) == 0)
		printf("Removing:\n");
	SLIST_FOREACH_SAFE(pfra, anchors, pfra_sle, pfra_save) {
		if ((opts & PF_OPT_CALLSHOW) == 0)
			printf("  %s\n", (*pfra->pfra_anchorname == '\0') ?
			    "/" : pfra->pfra_anchorname);
		rv |= walkf(dev, opts, pfra);
		SLIST_REMOVE(anchors, pfra, pfr_anchoritem, pfra_sle);
		free(pfra->pfra_anchorname);
		free(pfra);
	}

	return (rv);
}

const char *
pfctl_lookup_option(char *cmd, const char **list)
{
	const char *item = NULL;

	if (cmd != NULL && *cmd)
		for (; *list; list++)
			if (!strncmp(cmd, *list, strlen(cmd))) {
				if (item == NULL)
					item = *list;
				else
					errx(1, "%s is ambigious", cmd);
			}

	return (item);
}


void
pfctl_state_store(int dev, const char *file)
{
	FILE *f;
	struct pfioc_states ps;
	char *inbuf = NULL, *newinbuf = NULL;
	size_t n, len = 0;

	f = fopen(file, "w");
	if (f == NULL)
		err(1, "open: %s", file);

	memset(&ps, 0, sizeof(ps));
	for (;;) {
		ps.ps_len = len;
		if (len) {
			newinbuf = realloc(inbuf, len);
			if (newinbuf == NULL)
				err(1, "realloc");
			ps.ps_buf = inbuf = newinbuf;
		}
		if (ioctl(dev, DIOCGETSTATES, &ps) == -1)
			err(1, "DIOCGETSTATES");

		if (ps.ps_len + sizeof(struct pfioc_states) < len)
			break;
		if (len == 0 && ps.ps_len == 0)
			goto done;
		if (len == 0 && ps.ps_len != 0)
			len = ps.ps_len;
		if (ps.ps_len == 0)
			goto done;	/* no states */
		len *= 2;
	}

	n = ps.ps_len / sizeof(struct pfsync_state);
	if (fwrite(inbuf, sizeof(struct pfsync_state), n, f) < n)
		err(1, "fwrite");

done:
	free(inbuf);
	fclose(f);
}

void
pfctl_state_load(int dev, const char *file)
{
	FILE *f;
	struct pfioc_state ps;

	f = fopen(file, "r");
	if (f == NULL)
		err(1, "open: %s", file);

	while (fread(&ps.state, sizeof(ps.state), 1, f) == 1) {
		if (ioctl(dev, DIOCADDSTATE, &ps) == -1) {
			switch (errno) {
			case EEXIST:
			case EINVAL:
				break;
			default:
				err(1, "DIOCADDSTATE");
			}
		}
	}

	fclose(f);
}

void
pfctl_reset(int dev, int opts)
{
	struct pfctl	pf;
	struct pfr_buffer t;
	int		i;

	memset(&pf, 0, sizeof(pf));
	pf.dev = dev;
	pfctl_init_options(&pf);

	/* Force reset upon pfctl_load_options() */
	pf.debug_set = 1;
	pf.reass_set = 1;
	pf.syncookieswat_set = 1;
	pf.syncookies_set = 1;
	pf.ifname = strdup("none");
	if (pf.ifname == NULL)
		err(1, "%s: strdup", __func__);
	pf.ifname_set = 1;

	memset(&t, 0, sizeof(t));
	t.pfrb_type = PFRB_TRANS;
	if (pfctl_trans(dev, &t, DIOCXBEGIN, 0))
		err(1, "%s: DIOCXBEGIN", __func__);

	for (i = 0; pf_limits[i].name; i++)
		pf.limit_set[pf_limits[i].index] = 1;

	for (i = 0; pf_timeouts[i].name; i++)
		pf.timeout_set[pf_timeouts[i].timeout] = 1;

	pfctl_load_options(&pf);

	if (pfctl_trans(dev, &t, DIOCXCOMMIT, 0))
		err(1, "%s: DIOCXCOMMIT", __func__);

	pfctl_clear_interface_flags(dev, opts);
}

#ifndef	REGRESS_NOMAIN
int
main(int argc, char *argv[])
{
	int	 ch;
	int	 mode = O_RDONLY;
	int	 opts = 0;
	int	 optimize = PF_OPTIMIZE_BASIC;
	int	 level;
	int	 rdomain = 0;
	char	 anchorname[PATH_MAX];
	int	 anchor_wildcard = 0;
	char	*path;
	char	*lfile = NULL, *sfile = NULL;
	const char *errstr;
	long	 shownr = -1;

	if (argc < 2)
		usage();

	while ((ch = getopt(argc, argv,
	    "a:dD:eqf:F:ghi:k:K:L:Nno:Pp:R:rS:s:t:T:vV:x:z")) != -1) {
		switch (ch) {
		case 'a':
			anchoropt = optarg;
			break;
		case 'd':
			opts |= PF_OPT_DISABLE;
			mode = O_RDWR;
			break;
		case 'D':
			if (pfctl_cmdline_symset(optarg) < 0)
				warnx("could not parse macro definition %s",
				    optarg);
			break;
		case 'e':
			opts |= PF_OPT_ENABLE;
			mode = O_RDWR;
			break;
		case 'q':
			opts |= PF_OPT_QUIET;
			break;
		case 'F':
			clearopt = pfctl_lookup_option(optarg, clearopt_list);
			if (clearopt == NULL) {
				warnx("Unknown flush modifier '%s'", optarg);
				usage();
			}
			mode = O_RDWR;
			break;
		case 'i':
			ifaceopt = optarg;
			break;
		case 'k':
			if (state_killers >= 2) {
				warnx("can only specify -k twice");
				usage();
				/* NOTREACHED */
			}
			state_kill[state_killers++] = optarg;
			mode = O_RDWR;
			break;
		case 'K':
			if (src_node_killers >= 2) {
				warnx("can only specify -K twice");
				usage();
				/* NOTREACHED */
			}
			src_node_kill[src_node_killers++] = optarg;
			mode = O_RDWR;
			break;
		case 'N':
			opts |= PF_OPT_NODNS;
			break;
		case 'n':
			opts |= PF_OPT_NOACTION;
			break;
		case 'r':
			opts |= PF_OPT_USEDNS;
			break;
		case 'R':
			shownr = strtonum(optarg, -1, LONG_MAX, &errstr);
			if (errstr) {
				warnx("invalid rule id: %s", errstr);
				usage();
			}
			break;
		case 'f':
			rulesopt = optarg;
			mode = O_RDWR;
			break;
		case 'g':
			opts |= PF_OPT_DEBUG;
			break;
		case 'o':
			optiopt = pfctl_lookup_option(optarg, optiopt_list);
			if (optiopt == NULL) {
				warnx("Unknown optimization '%s'", optarg);
				usage();
			}
			opts |= PF_OPT_OPTIMIZE;
			break;
		case 'P':
			opts |= PF_OPT_PORTNAMES;
			break;
		case 'p':
			pf_device = optarg;
			break;
		case 's':
			showopt = pfctl_lookup_option(optarg, showopt_list);
			if (showopt == NULL) {
				warnx("Unknown show modifier '%s'", optarg);
				usage();
			}
			break;
		case 't':
			tableopt = optarg;
			break;
		case 'T':
			tblcmdopt = pfctl_lookup_option(optarg, tblcmdopt_list);
			if (tblcmdopt == NULL) {
				warnx("Unknown table command '%s'", optarg);
				usage();
			}
			break;
		case 'v':
			if (opts & PF_OPT_VERBOSE)
				opts |= PF_OPT_VERBOSE2;
			opts |= PF_OPT_VERBOSE;
			break;
		case 'V':
			rdomain = strtonum(optarg, 0, RT_TABLEID_MAX, &errstr);
			if (errstr) {
				warnx("Invalid rdomain: %s", errstr);
				usage();
			}
			break;
		case 'x':
			debugopt = pfctl_lookup_option(optarg, debugopt_list);
			if (debugopt == NULL) {
				warnx("Unknown debug level '%s'", optarg);
				usage();
			}
			mode = O_RDWR;
			break;
		case 'z':
			opts |= PF_OPT_CLRRULECTRS;
			mode = O_RDWR;
			break;
		case 'S':
			sfile = optarg;
			break;
		case 'L':
			mode = O_RDWR;
			lfile = optarg;
			break;
		case 'h':
			/* FALLTHROUGH */
		default:
			usage();
			/* NOTREACHED */
		}
	}

	if ((opts & PF_OPT_NODNS) && (opts & PF_OPT_USEDNS))
		errx(1, "-N and -r are mutually exclusive");

	if ((tblcmdopt == NULL) ^ (tableopt == NULL))
		usage();

	if (tblcmdopt != NULL) {
		argc -= optind;
		argv += optind;
		ch = *tblcmdopt;
		mode = strchr("st", ch) ? O_RDONLY : O_RDWR;
	} else if (argc != optind) {
		warnx("unknown command line argument: %s ...", argv[optind]);
		usage();
		/* NOTREACHED */
	}

	memset(anchorname, 0, sizeof(anchorname));
	if (anchoropt != NULL) {
		if (anchoropt[0] == '\0')
			errx(1, "anchor name must not be empty");
		if (mode == O_RDONLY && showopt == NULL && tblcmdopt == NULL) {
			warnx("anchors apply to -f, -F, -s, and -T only");
			usage();
		}
		if (mode == O_RDWR && tblcmdopt == NULL &&
		    (anchoropt[0] == '_' || strstr(anchoropt, "/_") != NULL))
			errx(1, "anchor names beginning with '_' cannot "
			    "be modified from the command line");
		int len = strlen(anchoropt);

		if (anchoropt[len - 1] == '*') {
			if (len >= 2 && anchoropt[len - 2] == '/') {
				anchoropt[len - 2] = '\0';
				anchor_wildcard = 1;
			} else
				anchoropt[len - 1] = '\0';
			opts |= PF_OPT_RECURSE;
		}
		if (strlcpy(anchorname, anchoropt,
		    sizeof(anchorname)) >= sizeof(anchorname))
			errx(1, "anchor name '%s' too long",
			    anchoropt);
	}

	if ((opts & PF_OPT_NOACTION) == 0) {
		dev = open(pf_device, mode);
		if (dev == -1)
			err(1, "%s", pf_device);
		pfctl_read_limits(dev);
		atexit(pfctl_restore_limits);
	} else {
		dev = open(pf_device, O_RDONLY);
		if (dev >= 0) {
			opts |= PF_OPT_DUMMYACTION;
			pfctl_read_limits(dev);
		}
		/* turn off options */
		opts &= ~ (PF_OPT_DISABLE | PF_OPT_ENABLE);
		clearopt = showopt = debugopt = NULL;
	}

	if (opts & PF_OPT_DISABLE)
		if (pfctl_disable(dev, opts))
			exit_val = 1;

	if ((path = calloc(1, PATH_MAX)) == NULL)
		errx(1, "%s: calloc", __func__);

	if (showopt != NULL) {
		switch (*showopt) {
		case 'A':
			pfctl_show_anchors(dev, opts, anchorname);
			break;
		case 'r':
			pfctl_load_fingerprints(dev, opts);
			pfctl_show_rules(dev, path, opts, PFCTL_SHOW_RULES,
			    anchorname, 0, anchor_wildcard, shownr);
			break;
		case 'l':
			pfctl_load_fingerprints(dev, opts);
			pfctl_show_rules(dev, path, opts, PFCTL_SHOW_LABELS,
			    anchorname, 0, anchor_wildcard, shownr);
			break;
		case 'q':
			pfctl_show_queues(dev, ifaceopt, opts,
			    opts & PF_OPT_VERBOSE2);
			break;
		case 's':
			pfctl_show_states(dev, ifaceopt, opts, shownr);
			break;
		case 'S':
			pfctl_show_src_nodes(dev, opts);
			break;
		case 'i':
			pfctl_show_status(dev, opts);
			break;
		case 't':
			pfctl_show_timeouts(dev, opts);
			break;
		case 'm':
			pfctl_show_limits(dev, opts);
			break;
		case 'a':
			opts |= PF_OPT_SHOWALL;
			pfctl_load_fingerprints(dev, opts);

			pfctl_show_rules(dev, path, opts, PFCTL_SHOW_RULES,
			    anchorname, 0, 0, -1);
			pfctl_show_queues(dev, ifaceopt, opts,
			    opts & PF_OPT_VERBOSE2);
			pfctl_show_states(dev, ifaceopt, opts, -1);
			pfctl_show_src_nodes(dev, opts);
			pfctl_show_status(dev, opts);
			pfctl_show_rules(dev, path, opts, PFCTL_SHOW_LABELS,
			    anchorname, 0, 0, -1);
			pfctl_show_timeouts(dev, opts);
			pfctl_show_limits(dev, opts);
			pfctl_show_tables(anchorname, opts);
			pfctl_show_fingerprints(opts);
			break;
		case 'T':
			if (opts & PF_OPT_RECURSE) {
				opts |= PF_OPT_CALLSHOW;
				pfctl_recurse(dev, opts, anchorname,
				    pfctl_call_showtables);
			} else
				pfctl_show_tables(anchorname, opts);
			break;
		case 'o':
			pfctl_load_fingerprints(dev, opts);
			pfctl_show_fingerprints(opts);
			break;
		case 'I':
			pfctl_show_ifaces(ifaceopt, opts);
			break;
		}
	}

	if ((opts & PF_OPT_CLRRULECTRS) && showopt == NULL)
		pfctl_show_rules(dev, path, opts, PFCTL_SHOW_NOTHING,
		    anchorname, 0, 0, -1);

	if (clearopt != NULL) {
		switch (*clearopt) {
		case 'r':
			if (opts & PF_OPT_RECURSE)
				pfctl_recurse(dev, opts, anchorname,
				    pfctl_call_clearrules);
			else
				pfctl_clear_rules(dev, opts, anchorname);
			break;
		case 's':
			pfctl_clear_states(dev, ifaceopt, opts);
			break;
		case 'S':
			pfctl_clear_src_nodes(dev, opts);
			break;
		case 'i':
			pfctl_clear_stats(dev, ifaceopt, opts);
			break;
		case 'a':
			if (ifaceopt) {
				warnx("don't specify an interface with -Fall");
				usage();
				/* NOTREACHED */
			}
			if (opts & PF_OPT_RECURSE)
				pfctl_recurse(dev, opts, anchorname,
				    pfctl_call_clearanchors);
			else {
				pfctl_clear_tables(anchorname, opts);
				pfctl_clear_rules(dev, opts, anchorname);
			}

			if (!*anchorname) {
				pfctl_clear_states(dev, ifaceopt, opts);
				pfctl_clear_src_nodes(dev, opts);
				pfctl_clear_stats(dev, ifaceopt, opts);
				pfctl_clear_fingerprints(dev, opts);
				pfctl_reset(dev, opts);
			}
			break;
		case 'o':
			pfctl_clear_fingerprints(dev, opts);
			break;
		case 'T':
			if ((opts & PF_OPT_RECURSE) == 0)
				pfctl_clear_tables(anchorname, opts);
			else
				pfctl_recurse(dev, opts, anchorname,
				    pfctl_call_cleartables);
			break;
		case 'R':
			pfctl_reset(dev, opts);
			break;
		}
	}
	if (state_killers) {
		if (!strcmp(state_kill[0], "label"))
			pfctl_label_kill_states(dev, ifaceopt, opts, rdomain);
		else if (!strcmp(state_kill[0], "id"))
			pfctl_id_kill_states(dev, opts);
		else if (!strcmp(state_kill[0], "key"))
			pfctl_key_kill_states(dev, ifaceopt, opts, rdomain);
		else
			pfctl_net_kill_states(dev, ifaceopt, opts, rdomain);
	}

	if (src_node_killers)
		pfctl_kill_src_nodes(dev, opts);

	if (tblcmdopt != NULL) {
		exit_val = pfctl_table(argc, argv, tableopt,
		    tblcmdopt, rulesopt, anchorname, opts);
		rulesopt = NULL;
	}
	if (optiopt != NULL) {
		switch (*optiopt) {
		case 'n':
			optimize = 0;
			break;
		case 'b':
			optimize |= PF_OPTIMIZE_BASIC;
			break;
		case 'o':
		case 'p':
			optimize |= PF_OPTIMIZE_PROFILE;
			break;
		}
	}

	if (rulesopt != NULL && !anchorname[0]) {
		pfctl_clear_interface_flags(dev, opts | PF_OPT_QUIET);
		if (pfctl_file_fingerprints(dev, opts, PF_OSFP_FILE))
			exit_val = 1;
	}

	if (rulesopt != NULL) {
		if (pfctl_rules(dev, rulesopt, opts, optimize,
		    anchorname, NULL))
			exit_val = 1;
	}

	if (opts & PF_OPT_ENABLE)
		if (pfctl_enable(dev, opts))
			exit_val = 1;

	if (debugopt != NULL) {
		if ((level = string_to_loglevel((char *)debugopt)) < 0) {
			switch (*debugopt) {
			case 'n':
				level = LOG_CRIT;
				break;
			case 'u':
				level = LOG_ERR;
				break;
			case 'm':
				level = LOG_NOTICE;
				break;
			case 'l':
				level = LOG_DEBUG;
				break;
			}
		}
		if (level >= 0)
			pfctl_debug(dev, level, opts);
	}

	if (sfile != NULL)
		pfctl_state_store(dev, sfile);
	if (lfile != NULL)
		pfctl_state_load(dev, lfile);

	/*
	 * prevent pfctl_restore_limits() exit handler from restoring
	 * pf(4) options settings on successful exit.
	 */
	if (exit_val == 0) {
		close(dev);
		dev = -1;
	}

	return exit_val;
}
#endif	/* REGRESS_NOMAIN */

char *
pf_strerror(int errnum)
{
	switch (errnum) {
	case ESRCH:
		return "Table does not exist";
	case EINVAL:
	case ENOENT:
		return "Anchor does not exist";
	default:
		return strerror(errnum);
	}
}
