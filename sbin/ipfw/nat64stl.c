/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015-2019 Andrey V. Elsukov <ae@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/socket.h>

#include "ipfw2.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip_fw.h>
#include <netinet6/ip_fw_nat64.h>
#include <arpa/inet.h>

typedef int (nat64stl_cb_t)(ipfw_nat64stl_cfg *i, const char *name,
    uint8_t set);
static int nat64stl_foreach(nat64stl_cb_t *f, const char *name, uint8_t set,
    int sort);

static void nat64stl_create(const char *name, uint8_t set, int ac, char **av);
static void nat64stl_config(const char *name, uint8_t set, int ac, char **av);
static void nat64stl_destroy(const char *name, uint8_t set);
static void nat64stl_stats(const char *name, uint8_t set);
static void nat64stl_reset_stats(const char *name, uint8_t set);
static int nat64stl_show_cb(ipfw_nat64stl_cfg *cfg, const char *name,
    uint8_t set);
static int nat64stl_destroy_cb(ipfw_nat64stl_cfg *cfg, const char *name,
    uint8_t set);

static struct _s_x nat64cmds[] = {
      { "create",	TOK_CREATE },
      { "config",	TOK_CONFIG },
      { "destroy",	TOK_DESTROY },
      { "list",		TOK_LIST },
      { "show",		TOK_LIST },
      { "stats",	TOK_STATS },
      { NULL, 0 }
};

#define	IPV6_ADDR_INT32_WKPFX	htonl(0x64ff9b)
#define	IN6_IS_ADDR_WKPFX(a)					\
    ((a)->__u6_addr.__u6_addr32[0] == IPV6_ADDR_INT32_WKPFX &&	\
	(a)->__u6_addr.__u6_addr32[1] == 0 &&			\
	(a)->__u6_addr.__u6_addr32[2] == 0)
int
ipfw_check_nat64prefix(const struct in6_addr *prefix, int length)
{

	switch (length) {
	case 32:
	case 40:
	case 48:
	case 56:
	case 64:
		/* Well-known prefix has 96 prefix length */
		if (IN6_IS_ADDR_WKPFX(prefix))
			return (EINVAL);
		/* FALLTHROUGH */
	case 96:
		/* Bits 64 to 71 must be set to zero */
		if (prefix->__u6_addr.__u6_addr8[8] != 0)
			return (EINVAL);
		/* XXX: looks incorrect */
		if (IN6_IS_ADDR_MULTICAST(prefix) ||
		    IN6_IS_ADDR_UNSPECIFIED(prefix) ||
		    IN6_IS_ADDR_LOOPBACK(prefix))
			return (EINVAL);
		return (0);
	}
	return (EINVAL);
}

static struct _s_x nat64statscmds[] = {
      { "reset",	TOK_RESET },
      { NULL, 0 }
};

/*
 * This one handles all nat64stl-related commands
 *	ipfw [set N] nat64stl NAME {create | config} ...
 *	ipfw [set N] nat64stl NAME stats [reset]
 *	ipfw [set N] nat64stl {NAME | all} destroy
 *	ipfw [set N] nat64stl {NAME | all} {list | show}
 */
#define	nat64stl_check_name	table_check_name
void
ipfw_nat64stl_handler(int ac, char *av[])
{
	const char *name;
	int tcmd;
	uint8_t set;

	if (co.use_set != 0)
		set = co.use_set - 1;
	else
		set = 0;
	ac--; av++;

	NEED1("nat64stl needs instance name");
	name = *av;
	if (nat64stl_check_name(name) != 0) {
		if (strcmp(name, "all") == 0)
			name = NULL;
		else
			errx(EX_USAGE, "nat64stl instance name %s is invalid",
			    name);
	}
	ac--; av++;
	NEED1("nat64stl needs command");

	tcmd = get_token(nat64cmds, *av, "nat64stl command");
	if (name == NULL && tcmd != TOK_DESTROY && tcmd != TOK_LIST)
		errx(EX_USAGE, "nat64stl instance name required");
	switch (tcmd) {
	case TOK_CREATE:
		ac--; av++;
		nat64stl_create(name, set, ac, av);
		break;
	case TOK_CONFIG:
		ac--; av++;
		nat64stl_config(name, set, ac, av);
		break;
	case TOK_LIST:
		nat64stl_foreach(nat64stl_show_cb, name, set, 1);
		break;
	case TOK_DESTROY:
		if (name == NULL)
			nat64stl_foreach(nat64stl_destroy_cb, NULL, set, 0);
		else
			nat64stl_destroy(name, set);
		break;
	case TOK_STATS:
		ac--; av++;
		if (ac == 0) {
			nat64stl_stats(name, set);
			break;
		}
		tcmd = get_token(nat64statscmds, *av, "stats command");
		if (tcmd == TOK_RESET)
			nat64stl_reset_stats(name, set);
	}
}


static void
nat64stl_fill_ntlv(ipfw_obj_ntlv *ntlv, const char *name, uint8_t set)
{

	ntlv->head.type = IPFW_TLV_EACTION_NAME(1); /* it doesn't matter */
	ntlv->head.length = sizeof(ipfw_obj_ntlv);
	ntlv->idx = 1;
	ntlv->set = set;
	strlcpy(ntlv->name, name, sizeof(ntlv->name));
}

static struct _s_x nat64newcmds[] = {
      { "table4",	TOK_TABLE4 },
      { "table6",	TOK_TABLE6 },
      { "prefix6",	TOK_PREFIX6 },
      { "log",		TOK_LOG },
      { "-log",		TOK_LOGOFF },
      { "allow_private", TOK_PRIVATE },
      { "-allow_private", TOK_PRIVATEOFF },
      { NULL, 0 }
};

/*
 * Creates new nat64stl instance
 * ipfw nat64stl <NAME> create table4 <name> table6 <name> [ prefix6 <prefix>]
 * Request: [ ipfw_obj_lheader ipfw_nat64stl_cfg ]
 */
#define	NAT64STL_HAS_TABLE4	0x01
#define	NAT64STL_HAS_TABLE6	0x02
#define	NAT64STL_HAS_PREFIX6	0x04
static void
nat64stl_create(const char *name, uint8_t set, int ac, char *av[])
{
	char buf[sizeof(ipfw_obj_lheader) + sizeof(ipfw_nat64stl_cfg)];
	ipfw_nat64stl_cfg *cfg;
	ipfw_obj_lheader *olh;
	int tcmd, flags;
	char *p;

	memset(buf, 0, sizeof(buf));
	olh = (ipfw_obj_lheader *)buf;
	cfg = (ipfw_nat64stl_cfg *)(olh + 1);

	/* Some reasonable defaults */
	inet_pton(AF_INET6, "64:ff9b::", &cfg->prefix6);
	cfg->plen6 = 96;
	cfg->set = set;
	flags = NAT64STL_HAS_PREFIX6;
	while (ac > 0) {
		tcmd = get_token(nat64newcmds, *av, "option");
		ac--; av++;

		switch (tcmd) {
		case TOK_TABLE4:
			NEED1("table name required");
			table_fill_ntlv(&cfg->ntlv4, *av, set, 4);
			flags |= NAT64STL_HAS_TABLE4;
			ac--; av++;
			break;
		case TOK_TABLE6:
			NEED1("table name required");
			table_fill_ntlv(&cfg->ntlv6, *av, set, 6);
			flags |= NAT64STL_HAS_TABLE6;
			ac--; av++;
			break;
		case TOK_PREFIX6:
			NEED1("IPv6 prefix6 required");
			if ((p = strchr(*av, '/')) != NULL)
				*p++ = '\0';
			if (inet_pton(AF_INET6, *av, &cfg->prefix6) != 1)
				errx(EX_USAGE,
				    "Bad prefix: %s", *av);
			cfg->plen6 = strtol(p, NULL, 10);
			if (ipfw_check_nat64prefix(&cfg->prefix6,
			    cfg->plen6) != 0)
				errx(EX_USAGE,
				    "Bad prefix length: %s", p);
			flags |= NAT64STL_HAS_PREFIX6;
			ac--; av++;
			break;
		case TOK_LOG:
			cfg->flags |= NAT64_LOG;
			break;
		case TOK_LOGOFF:
			cfg->flags &= ~NAT64_LOG;
			break;
		case TOK_PRIVATE:
			cfg->flags |= NAT64_ALLOW_PRIVATE;
			break;
		case TOK_PRIVATEOFF:
			cfg->flags &= ~NAT64_ALLOW_PRIVATE;
			break;
		}
	}

	/* Check validness */
	if ((flags & NAT64STL_HAS_TABLE4) != NAT64STL_HAS_TABLE4)
		errx(EX_USAGE, "table4 required");
	if ((flags & NAT64STL_HAS_TABLE6) != NAT64STL_HAS_TABLE6)
		errx(EX_USAGE, "table6 required");
	if ((flags & NAT64STL_HAS_PREFIX6) != NAT64STL_HAS_PREFIX6)
		errx(EX_USAGE, "prefix6 required");

	olh->count = 1;
	olh->objsize = sizeof(*cfg);
	olh->size = sizeof(buf);
	strlcpy(cfg->name, name, sizeof(cfg->name));
	if (do_set3(IP_FW_NAT64STL_CREATE, &olh->opheader, sizeof(buf)) != 0)
		err(EX_OSERR, "nat64stl instance creation failed");
}

/*
 * Configures existing nat64stl instance
 * ipfw nat64stl <NAME> config <options>
 * Request: [ ipfw_obj_header ipfw_nat64stl_cfg ]
 */
static void
nat64stl_config(const char *name, uint8_t set, int ac, char **av)
{
	char buf[sizeof(ipfw_obj_header) + sizeof(ipfw_nat64stl_cfg)];
	ipfw_nat64stl_cfg *cfg;
	ipfw_obj_header *oh;
	char *opt;
	size_t sz;
	int tcmd;

	if (ac == 0)
		errx(EX_USAGE, "config options required");
	memset(&buf, 0, sizeof(buf));
	oh = (ipfw_obj_header *)buf;
	cfg = (ipfw_nat64stl_cfg *)(oh + 1);
	sz = sizeof(buf);

	nat64stl_fill_ntlv(&oh->ntlv, name, set);
	if (do_get3(IP_FW_NAT64STL_CONFIG, &oh->opheader, &sz) != 0)
		err(EX_OSERR, "failed to get config for instance %s", name);

	while (ac > 0) {
		tcmd = get_token(nat64newcmds, *av, "option");
		opt = *av;
		ac--; av++;

		switch (tcmd) {
#if 0
		case TOK_TABLE4:
			NEED1("table name required");
			table_fill_ntlv(&cfg->ntlv4, *av, set, 4);
			ac--; av++;
			break;
		case TOK_TABLE6:
			NEED1("table name required");
			table_fill_ntlv(&cfg->ntlv6, *av, set, 6);
			ac--; av++;
			break;
#endif
		case TOK_LOG:
			cfg->flags |= NAT64_LOG;
			break;
		case TOK_LOGOFF:
			cfg->flags &= ~NAT64_LOG;
			break;
		case TOK_PRIVATE:
			cfg->flags |= NAT64_ALLOW_PRIVATE;
			break;
		case TOK_PRIVATEOFF:
			cfg->flags &= ~NAT64_ALLOW_PRIVATE;
			break;
		default:
			errx(EX_USAGE, "Can't change %s option", opt);
		}
	}

	if (do_set3(IP_FW_NAT64STL_CONFIG, &oh->opheader, sizeof(buf)) != 0)
		err(EX_OSERR, "nat64stl instance configuration failed");
}

/*
 * Destroys nat64stl instance.
 * Request: [ ipfw_obj_header ]
 */
static void
nat64stl_destroy(const char *name, uint8_t set)
{
	ipfw_obj_header oh;

	memset(&oh, 0, sizeof(oh));
	nat64stl_fill_ntlv(&oh.ntlv, name, set);
	if (do_set3(IP_FW_NAT64STL_DESTROY, &oh.opheader, sizeof(oh)) != 0)
		err(EX_OSERR, "failed to destroy nat instance %s", name);
}

/*
 * Get nat64stl instance statistics.
 * Request: [ ipfw_obj_header ]
 * Reply: [ ipfw_obj_header ipfw_obj_ctlv [ uint64_t x N ] ]
 */
static int
nat64stl_get_stats(const char *name, uint8_t set,
    struct ipfw_nat64stl_stats *stats)
{
	ipfw_obj_header *oh;
	ipfw_obj_ctlv *oc;
	size_t sz;

	sz = sizeof(*oh) + sizeof(*oc) + sizeof(*stats);
	oh = calloc(1, sz);
	nat64stl_fill_ntlv(&oh->ntlv, name, set);
	if (do_get3(IP_FW_NAT64STL_STATS, &oh->opheader, &sz) == 0) {
		oc = (ipfw_obj_ctlv *)(oh + 1);
		memcpy(stats, oc + 1, sizeof(*stats));
		free(oh);
		return (0);
	}
	free(oh);
	return (-1);
}

static void
nat64stl_stats(const char *name, uint8_t set)
{
	struct ipfw_nat64stl_stats stats;

	if (nat64stl_get_stats(name, set, &stats) != 0)
		err(EX_OSERR, "Error retrieving stats");

	if (co.use_set != 0 || set != 0)
		printf("set %u ", set);
	printf("nat64stl %s\n", name);

	printf("\t%ju packets translated from IPv6 to IPv4\n",
	    (uintmax_t)stats.opcnt64);
	printf("\t%ju packets translated from IPv4 to IPv6\n",
	    (uintmax_t)stats.opcnt46);
	printf("\t%ju IPv6 fragments created\n",
	    (uintmax_t)stats.ofrags);
	printf("\t%ju IPv4 fragments received\n",
	    (uintmax_t)stats.ifrags);
	printf("\t%ju output packets dropped due to no bufs, etc.\n",
	    (uintmax_t)stats.oerrors);
	printf("\t%ju output packets discarded due to no IPv4 route\n",
	    (uintmax_t)stats.noroute4);
	printf("\t%ju output packets discarded due to no IPv6 route\n",
	    (uintmax_t)stats.noroute6);
	printf("\t%ju packets discarded due to unsupported protocol\n",
	    (uintmax_t)stats.noproto);
	printf("\t%ju packets discarded due to memory allocation problems\n",
	    (uintmax_t)stats.nomem);
	printf("\t%ju packets discarded due to some errors\n",
	    (uintmax_t)stats.dropped);
}

/*
 * Reset nat64stl instance statistics specified by @oh->ntlv.
 * Request: [ ipfw_obj_header ]
 */
static void
nat64stl_reset_stats(const char *name, uint8_t set)
{
	ipfw_obj_header oh;

	memset(&oh, 0, sizeof(oh));
	nat64stl_fill_ntlv(&oh.ntlv, name, set);
	if (do_set3(IP_FW_NAT64STL_RESET_STATS, &oh.opheader, sizeof(oh)) != 0)
		err(EX_OSERR, "failed to reset stats for instance %s", name);
}

static int
nat64stl_show_cb(ipfw_nat64stl_cfg *cfg, const char *name, uint8_t set)
{
	char abuf[INET6_ADDRSTRLEN];

	if (name != NULL && strcmp(cfg->name, name) != 0)
		return (ESRCH);

	if (co.use_set != 0 && cfg->set != set)
		return (ESRCH);

	if (co.use_set != 0 || cfg->set != 0)
		printf("set %u ", cfg->set);

	printf("nat64stl %s table4 %s table6 %s",
	    cfg->name, cfg->ntlv4.name, cfg->ntlv6.name);
	inet_ntop(AF_INET6, &cfg->prefix6, abuf, sizeof(abuf));
	printf(" prefix6 %s/%u", abuf, cfg->plen6);
	if (cfg->flags & NAT64_LOG)
		printf(" log");
	if (cfg->flags & NAT64_ALLOW_PRIVATE)
		printf(" allow_private");
	printf("\n");
	return (0);
}

static int
nat64stl_destroy_cb(ipfw_nat64stl_cfg *cfg, const char *name, uint8_t set)
{

	if (co.use_set != 0 && cfg->set != set)
		return (ESRCH);

	nat64stl_destroy(cfg->name, cfg->set);
	return (0);
}


/*
 * Compare nat64stl instances names.
 * Honor number comparison.
 */
static int
nat64name_cmp(const void *a, const void *b)
{
	ipfw_nat64stl_cfg *ca, *cb;

	ca = (ipfw_nat64stl_cfg *)a;
	cb = (ipfw_nat64stl_cfg *)b;

	if (ca->set > cb->set)
		return (1);
	else if (ca->set < cb->set)
		return (-1);
	return (stringnum_cmp(ca->name, cb->name));
}

/*
 * Retrieves nat64stl instance list from kernel,
 * optionally sorts it and calls requested function for each instance.
 *
 * Request: [ ipfw_obj_lheader ]
 * Reply: [ ipfw_obj_lheader ipfw_nat64stl_cfg x N ]
 */
static int
nat64stl_foreach(nat64stl_cb_t *f, const char *name, uint8_t set, int sort)
{
	ipfw_obj_lheader *olh;
	ipfw_nat64stl_cfg *cfg;
	size_t sz;
	int i, error;

	/* Start with reasonable default */
	sz = sizeof(*olh) + 16 * sizeof(*cfg);
	for (;;) {
		if ((olh = calloc(1, sz)) == NULL)
			return (ENOMEM);

		olh->size = sz;
		if (do_get3(IP_FW_NAT64STL_LIST, &olh->opheader, &sz) != 0) {
			sz = olh->size;
			free(olh);
			if (errno != ENOMEM)
				return (errno);
			continue;
		}

		if (sort != 0)
			qsort(olh + 1, olh->count, olh->objsize,
			    nat64name_cmp);

		cfg = (ipfw_nat64stl_cfg *)(olh + 1);
		for (i = 0; i < olh->count; i++) {
			error = f(cfg, name, set); /* Ignore errors for now */
			cfg = (ipfw_nat64stl_cfg *)((caddr_t)cfg +
			    olh->objsize);
		}
		free(olh);
		break;
	}
	return (0);
}

