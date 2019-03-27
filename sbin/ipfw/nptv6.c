/*-
 * Copyright (c) 2016 Yandex LLC
 * Copyright (c) 2016 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>

#include "ipfw2.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_fw.h>
#include <netinet6/ip_fw_nptv6.h>
#include <arpa/inet.h>


typedef int (nptv6_cb_t)(ipfw_nptv6_cfg *i, const char *name, uint8_t set);
static int nptv6_foreach(nptv6_cb_t *f, const char *name, uint8_t set,
    int sort);

static void nptv6_create(const char *name, uint8_t set, int ac, char **av);
static void nptv6_destroy(const char *name, uint8_t set);
static void nptv6_stats(const char *name, uint8_t set);
static void nptv6_reset_stats(const char *name, uint8_t set);
static int nptv6_show_cb(ipfw_nptv6_cfg *cfg, const char *name, uint8_t set);
static int nptv6_destroy_cb(ipfw_nptv6_cfg *cfg, const char *name, uint8_t set);

static struct _s_x nptv6cmds[] = {
      { "create",	TOK_CREATE },
      { "destroy",	TOK_DESTROY },
      { "list",		TOK_LIST },
      { "show",		TOK_LIST },
      { "stats",	TOK_STATS },
      { NULL, 0 }
};

static struct _s_x nptv6statscmds[] = {
      { "reset",	TOK_RESET },
      { NULL, 0 }
};

/*
 * This one handles all NPTv6-related commands
 *	ipfw [set N] nptv6 NAME {create | config} ...
 *	ipfw [set N] nptv6 NAME stats [reset]
 *	ipfw [set N] nptv6 {NAME | all} destroy
 *	ipfw [set N] nptv6 {NAME | all} {list | show}
 */
#define	nptv6_check_name	table_check_name
void
ipfw_nptv6_handler(int ac, char *av[])
{
	const char *name;
	int tcmd;
	uint8_t set;

	if (co.use_set != 0)
		set = co.use_set - 1;
	else
		set = 0;
	ac--; av++;

	NEED1("nptv6 needs instance name");
	name = *av;
	if (nptv6_check_name(name) != 0) {
		if (strcmp(name, "all") == 0) {
			name = NULL;
		} else
			errx(EX_USAGE, "nptv6 instance name %s is invalid",
			    name);
	}
	ac--; av++;
	NEED1("nptv6 needs command");

	tcmd = get_token(nptv6cmds, *av, "nptv6 command");
	if (name == NULL && tcmd != TOK_DESTROY && tcmd != TOK_LIST)
		errx(EX_USAGE, "nptv6 instance name required");
	switch (tcmd) {
	case TOK_CREATE:
		ac--; av++;
		nptv6_create(name, set, ac, av);
		break;
	case TOK_LIST:
		nptv6_foreach(nptv6_show_cb, name, set, 1);
		break;
	case TOK_DESTROY:
		if (name == NULL)
			nptv6_foreach(nptv6_destroy_cb, NULL, set, 0);
		else
			nptv6_destroy(name, set);
		break;
	case TOK_STATS:
		ac--; av++;
		if (ac == 0) {
			nptv6_stats(name, set);
			break;
		}
		tcmd = get_token(nptv6statscmds, *av, "stats command");
		if (tcmd == TOK_RESET)
			nptv6_reset_stats(name, set);
	}
}


static void
nptv6_fill_ntlv(ipfw_obj_ntlv *ntlv, const char *name, uint8_t set)
{

	ntlv->head.type = IPFW_TLV_EACTION_NAME(1); /* it doesn't matter */
	ntlv->head.length = sizeof(ipfw_obj_ntlv);
	ntlv->idx = 1;
	ntlv->set = set;
	strlcpy(ntlv->name, name, sizeof(ntlv->name));
}

static struct _s_x nptv6newcmds[] = {
      { "int_prefix",	TOK_INTPREFIX },
      { "ext_prefix",	TOK_EXTPREFIX },
      { "prefixlen",	TOK_PREFIXLEN },
      { "ext_if",	TOK_EXTIF },
      { NULL, 0 }
};


static void
nptv6_parse_prefix(const char *arg, struct in6_addr *prefix, int *len)
{
	char *p, *l;

	p = strdup(arg);
	if (p == NULL)
		err(EX_OSERR, NULL);
	if ((l = strchr(p, '/')) != NULL)
		*l++ = '\0';
	if (inet_pton(AF_INET6, p, prefix) != 1)
		errx(EX_USAGE, "Bad prefix: %s", p);
	if (l != NULL) {
		*len = (int)strtol(l, &l, 10);
		if (*l != '\0' || *len <= 0 || *len > 64)
			errx(EX_USAGE, "Bad prefix length: %s", arg);
	} else
		*len = 0;
	free(p);
}
/*
 * Creates new nptv6 instance
 * ipfw nptv6 <NAME> create int_prefix <prefix> ext_prefix <prefix>
 * Request: [ ipfw_obj_lheader ipfw_nptv6_cfg ]
 */
#define	NPTV6_HAS_INTPREFIX	0x01
#define	NPTV6_HAS_EXTPREFIX	0x02
#define	NPTV6_HAS_PREFIXLEN	0x04
static void
nptv6_create(const char *name, uint8_t set, int ac, char *av[])
{
	char buf[sizeof(ipfw_obj_lheader) + sizeof(ipfw_nptv6_cfg)];
	struct in6_addr mask;
	ipfw_nptv6_cfg *cfg;
	ipfw_obj_lheader *olh;
	int tcmd, flags, plen;
	char *p = "\0";

	plen = 0;
	memset(buf, 0, sizeof(buf));
	olh = (ipfw_obj_lheader *)buf;
	cfg = (ipfw_nptv6_cfg *)(olh + 1);
	cfg->set = set;
	flags = 0;
	while (ac > 0) {
		tcmd = get_token(nptv6newcmds, *av, "option");
		ac--; av++;

		switch (tcmd) {
		case TOK_INTPREFIX:
			NEED1("IPv6 prefix required");
			nptv6_parse_prefix(*av, &cfg->internal, &plen);
			flags |= NPTV6_HAS_INTPREFIX;
			if (plen > 0)
				goto check_prefix;
			ac--; av++;
			break;
		case TOK_EXTPREFIX:
			if (flags & NPTV6_HAS_EXTPREFIX)
				errx(EX_USAGE,
				    "Only one ext_prefix or ext_if allowed");
			NEED1("IPv6 prefix required");
			nptv6_parse_prefix(*av, &cfg->external, &plen);
			flags |= NPTV6_HAS_EXTPREFIX;
			if (plen > 0)
				goto check_prefix;
			ac--; av++;
			break;
		case TOK_EXTIF:
			if (flags & NPTV6_HAS_EXTPREFIX)
				errx(EX_USAGE,
				    "Only one ext_prefix or ext_if allowed");
			NEED1("Interface name required");
			if (strlen(*av) >= sizeof(cfg->if_name))
				errx(EX_USAGE, "Invalid interface name");
			flags |= NPTV6_HAS_EXTPREFIX;
			cfg->flags |= NPTV6_DYNAMIC_PREFIX;
			strncpy(cfg->if_name, *av, sizeof(cfg->if_name));
			ac--; av++;
			break;
		case TOK_PREFIXLEN:
			NEED1("IPv6 prefix length required");
			plen = strtol(*av, &p, 10);
check_prefix:
			if (*p != '\0' || plen < 8 || plen > 64)
				errx(EX_USAGE, "wrong prefix length: %s", *av);
			/* RFC 6296 Sec. 3.1 */
			if (cfg->plen > 0 && cfg->plen != plen) {
				warnx("Prefix length mismatch (%d vs %d).  "
				    "It was extended up to %d",
				    cfg->plen, plen, MAX(plen, cfg->plen));
				plen = MAX(plen, cfg->plen);
			}
			cfg->plen = plen;
			flags |= NPTV6_HAS_PREFIXLEN;
			ac--; av++;
			break;
		}
	}

	/* Check validness */
	if ((flags & NPTV6_HAS_INTPREFIX) != NPTV6_HAS_INTPREFIX)
		errx(EX_USAGE, "int_prefix required");
	if ((flags & NPTV6_HAS_EXTPREFIX) != NPTV6_HAS_EXTPREFIX)
		errx(EX_USAGE, "ext_prefix or ext_if required");
	if ((flags & NPTV6_HAS_PREFIXLEN) != NPTV6_HAS_PREFIXLEN)
		errx(EX_USAGE, "prefixlen required");

	n2mask(&mask, cfg->plen);
	APPLY_MASK(&cfg->internal, &mask);
	if ((cfg->flags & NPTV6_DYNAMIC_PREFIX) == 0)
		APPLY_MASK(&cfg->external, &mask);

	olh->count = 1;
	olh->objsize = sizeof(*cfg);
	olh->size = sizeof(buf);
	strlcpy(cfg->name, name, sizeof(cfg->name));
	if (do_set3(IP_FW_NPTV6_CREATE, &olh->opheader, sizeof(buf)) != 0)
		err(EX_OSERR, "nptv6 instance creation failed");
}

/*
 * Destroys NPTv6 instance.
 * Request: [ ipfw_obj_header ]
 */
static void
nptv6_destroy(const char *name, uint8_t set)
{
	ipfw_obj_header oh;

	memset(&oh, 0, sizeof(oh));
	nptv6_fill_ntlv(&oh.ntlv, name, set);
	if (do_set3(IP_FW_NPTV6_DESTROY, &oh.opheader, sizeof(oh)) != 0)
		err(EX_OSERR, "failed to destroy nat instance %s", name);
}

/*
 * Get NPTv6 instance statistics.
 * Request: [ ipfw_obj_header ]
 * Reply: [ ipfw_obj_header ipfw_obj_ctlv [ uint64_t x N ] ]
 */
static int
nptv6_get_stats(const char *name, uint8_t set, struct ipfw_nptv6_stats *stats)
{
	ipfw_obj_header *oh;
	ipfw_obj_ctlv *oc;
	size_t sz;

	sz = sizeof(*oh) + sizeof(*oc) + sizeof(*stats);
	oh = calloc(1, sz);
	nptv6_fill_ntlv(&oh->ntlv, name, set);
	if (do_get3(IP_FW_NPTV6_STATS, &oh->opheader, &sz) == 0) {
		oc = (ipfw_obj_ctlv *)(oh + 1);
		memcpy(stats, oc + 1, sizeof(*stats));
		free(oh);
		return (0);
	}
	free(oh);
	return (-1);
}

static void
nptv6_stats(const char *name, uint8_t set)
{
	struct ipfw_nptv6_stats stats;

	if (nptv6_get_stats(name, set, &stats) != 0)
		err(EX_OSERR, "Error retrieving stats");

	if (co.use_set != 0 || set != 0)
		printf("set %u ", set);
	printf("nptv6 %s\n", name);
	printf("\t%ju packets translated (internal to external)\n",
	    (uintmax_t)stats.in2ex);
	printf("\t%ju packets translated (external to internal)\n",
	    (uintmax_t)stats.ex2in);
	printf("\t%ju packets dropped due to some error\n",
	    (uintmax_t)stats.dropped);
}

/*
 * Reset NPTv6 instance statistics specified by @oh->ntlv.
 * Request: [ ipfw_obj_header ]
 */
static void
nptv6_reset_stats(const char *name, uint8_t set)
{
	ipfw_obj_header oh;

	memset(&oh, 0, sizeof(oh));
	nptv6_fill_ntlv(&oh.ntlv, name, set);
	if (do_set3(IP_FW_NPTV6_RESET_STATS, &oh.opheader, sizeof(oh)) != 0)
		err(EX_OSERR, "failed to reset stats for instance %s", name);
}

static int
nptv6_show_cb(ipfw_nptv6_cfg *cfg, const char *name, uint8_t set)
{
	char abuf[INET6_ADDRSTRLEN];

	if (name != NULL && strcmp(cfg->name, name) != 0)
		return (ESRCH);

	if (co.use_set != 0 && cfg->set != set)
		return (ESRCH);

	if (co.use_set != 0 || cfg->set != 0)
		printf("set %u ", cfg->set);
	inet_ntop(AF_INET6, &cfg->internal, abuf, sizeof(abuf));
	printf("nptv6 %s int_prefix %s ", cfg->name, abuf);
	if (cfg->flags & NPTV6_DYNAMIC_PREFIX)
		printf("ext_if %s ", cfg->if_name);
	else {
		inet_ntop(AF_INET6, &cfg->external, abuf, sizeof(abuf));
		printf("ext_prefix %s ", abuf);
	}
	printf("prefixlen %u\n", cfg->plen);
	return (0);
}

static int
nptv6_destroy_cb(ipfw_nptv6_cfg *cfg, const char *name, uint8_t set)
{

	if (co.use_set != 0 && cfg->set != set)
		return (ESRCH);

	nptv6_destroy(cfg->name, cfg->set);
	return (0);
}


/*
 * Compare NPTv6 instances names.
 * Honor number comparison.
 */
static int
nptv6name_cmp(const void *a, const void *b)
{
	ipfw_nptv6_cfg *ca, *cb;

	ca = (ipfw_nptv6_cfg *)a;
	cb = (ipfw_nptv6_cfg *)b;

	if (ca->set > cb->set)
		return (1);
	else if (ca->set < cb->set)
		return (-1);
	return (stringnum_cmp(ca->name, cb->name));
}

/*
 * Retrieves NPTv6 instance list from kernel,
 * Request: [ ipfw_obj_lheader ]
 * Reply: [ ipfw_obj_lheader ipfw_nptv6_cfg x N ]
 */
static int
nptv6_foreach(nptv6_cb_t *f, const char *name, uint8_t set, int sort)
{
	ipfw_obj_lheader *olh;
	ipfw_nptv6_cfg *cfg;
	size_t sz;
	int i, error;

	/* Start with reasonable default */
	sz = sizeof(*olh) + 16 * sizeof(*cfg);
	for (;;) {
		if ((olh = calloc(1, sz)) == NULL)
			return (ENOMEM);

		olh->size = sz;
		if (do_get3(IP_FW_NPTV6_LIST, &olh->opheader, &sz) != 0) {
			sz = olh->size;
			free(olh);
			if (errno != ENOMEM)
				return (errno);
			continue;
		}

		if (sort != 0)
			qsort(olh + 1, olh->count, olh->objsize, nptv6name_cmp);

		cfg = (ipfw_nptv6_cfg *)(olh + 1);
		for (i = 0; i < olh->count; i++) {
			error = f(cfg, name, set);
			cfg = (ipfw_nptv6_cfg *)((caddr_t)cfg + olh->objsize);
		}
		free(olh);
		break;
	}
	return (0);
}

