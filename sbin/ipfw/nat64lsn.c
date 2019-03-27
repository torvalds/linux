/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015-2016 Alexander V. Chernikov <melifaro@FreeBSD.org>
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

static void nat64lsn_fill_ntlv(ipfw_obj_ntlv *ntlv, const char *name,
    uint8_t set);
typedef int (nat64lsn_cb_t)(ipfw_nat64lsn_cfg *cfg, const char *name,
    uint8_t set);
static int nat64lsn_foreach(nat64lsn_cb_t *f, const char *name, uint8_t set,
    int sort);

static void nat64lsn_create(const char *name, uint8_t set, int ac, char **av);
static void nat64lsn_config(const char *name, uint8_t set, int ac, char **av);
static void nat64lsn_destroy(const char *name, uint8_t set);
static void nat64lsn_stats(const char *name, uint8_t set);
static void nat64lsn_reset_stats(const char *name, uint8_t set);
static int nat64lsn_show_cb(ipfw_nat64lsn_cfg *cfg, const char *name,
    uint8_t set);
static int nat64lsn_destroy_cb(ipfw_nat64lsn_cfg *cfg, const char *name,
    uint8_t set);
static int nat64lsn_states_cb(ipfw_nat64lsn_cfg *cfg, const char *name,
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

static uint64_t
nat64lsn_print_states(void *buf)
{
	char s[INET6_ADDRSTRLEN], a[INET_ADDRSTRLEN], f[INET_ADDRSTRLEN];
	char sflags[4], *sf, *proto;
	ipfw_obj_header *oh;
	ipfw_obj_data *od;
	ipfw_nat64lsn_stg_v1 *stg;
	ipfw_nat64lsn_state_v1 *ste;
	uint64_t next_idx;
	int i, sz;

	oh = (ipfw_obj_header *)buf;
	od = (ipfw_obj_data *)(oh + 1);
	stg = (ipfw_nat64lsn_stg_v1 *)(od + 1);
	sz = od->head.length - sizeof(*od);
	next_idx = 0;
	while (sz > 0 && next_idx != 0xFF) {
		next_idx = stg->next.index;
		sz -= sizeof(*stg);
		if (stg->count == 0) {
			stg++;
			continue;
		}
		/*
		 * NOTE: addresses are in network byte order,
		 * ports are in host byte order.
		 */
		inet_ntop(AF_INET, &stg->alias4, a, sizeof(a));
		ste = (ipfw_nat64lsn_state_v1 *)(stg + 1);
		for (i = 0; i < stg->count && sz > 0; i++) {
			sf = sflags;
			inet_ntop(AF_INET6, &ste->host6, s, sizeof(s));
			inet_ntop(AF_INET, &ste->daddr, f, sizeof(f));
			switch (ste->proto) {
			case IPPROTO_TCP:
				proto = "TCP";
				if (ste->flags & 0x02)
					*sf++ = 'S';
				if (ste->flags & 0x04)
					*sf++ = 'E';
				if (ste->flags & 0x01)
					*sf++ = 'F';
				break;
			case IPPROTO_UDP:
				proto = "UDP";
				break;
			case IPPROTO_ICMP:
				proto = "ICMPv6";
				break;
			}
			*sf = '\0';
			switch (ste->proto) {
			case IPPROTO_TCP:
			case IPPROTO_UDP:
				printf("%s:%d\t%s:%d\t%s\t%s\t%d\t%s:%d\n",
				    s, ste->sport, a, ste->aport, proto,
				    sflags, ste->idle, f, ste->dport);
				break;
			case IPPROTO_ICMP:
				printf("%s\t%s\t%s\t\t%d\t%s\n",
				    s, a, proto, ste->idle, f);
				break;
			default:
				printf("%s\t%s\t%d\t\t%d\t%s\n",
				    s, a, ste->proto, ste->idle, f);
			}
			ste++;
			sz -= sizeof(*ste);
		}
		stg = (ipfw_nat64lsn_stg_v1 *)ste;
	}
	return (next_idx);
}

static int
nat64lsn_states_cb(ipfw_nat64lsn_cfg *cfg, const char *name, uint8_t set)
{
	ipfw_obj_header *oh;
	ipfw_obj_data *od;
	void *buf;
	uint64_t next_idx;
	size_t sz;

	if (name != NULL && strcmp(cfg->name, name) != 0)
		return (ESRCH);

	if (set != 0 && cfg->set != set)
		return (ESRCH);

	next_idx = 0;
	sz = 4096;
	if ((buf = calloc(1, sz)) == NULL)
		err(EX_OSERR, NULL);
	do {
		oh = (ipfw_obj_header *)buf;
		oh->opheader.version = 1; /* Force using ov new API */
		od = (ipfw_obj_data *)(oh + 1);
		nat64lsn_fill_ntlv(&oh->ntlv, cfg->name, set);
		od->head.type = IPFW_TLV_OBJDATA;
		od->head.length = sizeof(*od) + sizeof(next_idx);
		*((uint64_t *)(od + 1)) = next_idx;
		if (do_get3(IP_FW_NAT64LSN_LIST_STATES, &oh->opheader, &sz))
			err(EX_OSERR, "Error reading nat64lsn states");
		next_idx = nat64lsn_print_states(buf);
		sz = 4096;
		memset(buf, 0, sz);
	} while (next_idx != 0xFF);

	free(buf);
	return (0);
}

static struct _s_x nat64statscmds[] = {
      { "reset",	TOK_RESET },
      { NULL, 0 }
};

static void
ipfw_nat64lsn_stats_handler(const char *name, uint8_t set, int ac, char *av[])
{
	int tcmd;

	if (ac == 0) {
		nat64lsn_stats(name, set);
		return;
	}
	NEED1("nat64lsn stats needs command");
	tcmd = get_token(nat64statscmds, *av, "nat64lsn stats command");
	switch (tcmd) {
	case TOK_RESET:
		nat64lsn_reset_stats(name, set);
	}
}

static struct _s_x nat64listcmds[] = {
      { "states",	TOK_STATES },
      { "config",	TOK_CONFIG },
      { NULL, 0 }
};

static void
ipfw_nat64lsn_list_handler(const char *name, uint8_t set, int ac, char *av[])
{
	int tcmd;

	if (ac == 0) {
		nat64lsn_foreach(nat64lsn_show_cb, name, set, 1);
		return;
	}
	NEED1("nat64lsn list needs command");
	tcmd = get_token(nat64listcmds, *av, "nat64lsn list command");
	switch (tcmd) {
	case TOK_STATES:
		nat64lsn_foreach(nat64lsn_states_cb, name, set, 1);
		break;
	case TOK_CONFIG:
		nat64lsn_foreach(nat64lsn_show_cb, name, set, 1);
	}
}

/*
 * This one handles all nat64lsn-related commands
 *	ipfw [set N] nat64lsn NAME {create | config} ...
 *	ipfw [set N] nat64lsn NAME stats
 *	ipfw [set N] nat64lsn {NAME | all} destroy
 *	ipfw [set N] nat64lsn {NAME | all} {list | show} [config | states]
 */
#define	nat64lsn_check_name	table_check_name
void
ipfw_nat64lsn_handler(int ac, char *av[])
{
	const char *name;
	int tcmd;
	uint8_t set;

	if (co.use_set != 0)
		set = co.use_set - 1;
	else
		set = 0;
	ac--; av++;

	NEED1("nat64lsn needs instance name");
	name = *av;
	if (nat64lsn_check_name(name) != 0) {
		if (strcmp(name, "all") == 0)
			name = NULL;
		else
			errx(EX_USAGE, "nat64lsn instance name %s is invalid",
			    name);
	}
	ac--; av++;
	NEED1("nat64lsn needs command");

	tcmd = get_token(nat64cmds, *av, "nat64lsn command");
	if (name == NULL && tcmd != TOK_DESTROY && tcmd != TOK_LIST)
		errx(EX_USAGE, "nat64lsn instance name required");
	switch (tcmd) {
	case TOK_CREATE:
		ac--; av++;
		nat64lsn_create(name, set, ac, av);
		break;
	case TOK_CONFIG:
		ac--; av++;
		nat64lsn_config(name, set, ac, av);
		break;
	case TOK_LIST:
		ac--; av++;
		ipfw_nat64lsn_list_handler(name, set, ac, av);
		break;
	case TOK_DESTROY:
		if (name == NULL)
			nat64lsn_foreach(nat64lsn_destroy_cb, NULL, set, 0);
		else
			nat64lsn_destroy(name, set);
		break;
	case TOK_STATS:
		ac--; av++;
		ipfw_nat64lsn_stats_handler(name, set, ac, av);
	}
}

static void
nat64lsn_fill_ntlv(ipfw_obj_ntlv *ntlv, const char *name, uint8_t set)
{

	ntlv->head.type = IPFW_TLV_EACTION_NAME(1); /* it doesn't matter */
	ntlv->head.length = sizeof(ipfw_obj_ntlv);
	ntlv->idx = 1;
	ntlv->set = set;
	strlcpy(ntlv->name, name, sizeof(ntlv->name));
}

static void
nat64lsn_apply_mask(int af, void *prefix, uint16_t plen)
{
	struct in6_addr mask6, *p6;
	struct in_addr mask4, *p4;

	if (af == AF_INET) {
		p4 = (struct in_addr *)prefix;
		mask4.s_addr = htonl(~((1 << (32 - plen)) - 1));
		p4->s_addr &= mask4.s_addr;
	} else if (af == AF_INET6) {
		p6 = (struct in6_addr *)prefix;
		n2mask(&mask6, plen);
		APPLY_MASK(p6, &mask6);
	}
}

static void
nat64lsn_parse_prefix(const char *arg, int af, void *prefix, uint16_t *plen)
{
	char *p, *l;

	p = strdup(arg);
	if (p == NULL)
		err(EX_OSERR, NULL);
	if ((l = strchr(p, '/')) != NULL)
		*l++ = '\0';
	if (l == NULL)
		errx(EX_USAGE, "Prefix length required");
	if (inet_pton(af, p, prefix) != 1)
		errx(EX_USAGE, "Bad prefix: %s", p);
	*plen = (uint16_t)strtol(l, &l, 10);
	if (*l != '\0' || *plen == 0 || (af == AF_INET && *plen > 32) ||
	    (af == AF_INET6 && *plen > 96))
		errx(EX_USAGE, "Bad prefix length: %s", arg);
	nat64lsn_apply_mask(af, prefix, *plen);
	free(p);
}

static uint32_t
nat64lsn_parse_int(const char *arg, const char *desc)
{
	char *p;
	uint32_t val;

	val = (uint32_t)strtol(arg, &p, 10);
	if (*p != '\0')
		errx(EX_USAGE, "Invalid %s value: %s\n", desc, arg);
	return (val);
}

static struct _s_x nat64newcmds[] = {
      { "prefix6",	TOK_PREFIX6 },
      { "jmaxlen",	TOK_JMAXLEN },
      { "prefix4",	TOK_PREFIX4 },
      { "host_del_age",	TOK_HOST_DEL_AGE },
      { "pg_del_age",	TOK_PG_DEL_AGE },
      { "tcp_syn_age",	TOK_TCP_SYN_AGE },
      { "tcp_close_age",TOK_TCP_CLOSE_AGE },
      { "tcp_est_age",	TOK_TCP_EST_AGE },
      { "udp_age",	TOK_UDP_AGE },
      { "icmp_age",	TOK_ICMP_AGE },
      { "states_chunks",TOK_STATES_CHUNKS },
      { "log",		TOK_LOG },
      { "-log",		TOK_LOGOFF },
      { "allow_private", TOK_PRIVATE },
      { "-allow_private", TOK_PRIVATEOFF },
      /* for compatibility with old configurations */
      { "max_ports",	TOK_MAX_PORTS },	/* unused */
      { NULL, 0 }
};

/*
 * Creates new nat64lsn instance
 * ipfw nat64lsn <NAME> create
 *     [ max_ports <N> ]
 * Request: [ ipfw_obj_lheader ipfw_nat64lsn_cfg ]
 */
#define	NAT64LSN_HAS_PREFIX4	0x01
#define	NAT64LSN_HAS_PREFIX6	0x02
static void
nat64lsn_create(const char *name, uint8_t set, int ac, char **av)
{
	char buf[sizeof(ipfw_obj_lheader) + sizeof(ipfw_nat64lsn_cfg)];
	ipfw_nat64lsn_cfg *cfg;
	ipfw_obj_lheader *olh;
	int tcmd, flags;
	char *opt;

	memset(&buf, 0, sizeof(buf));
	olh = (ipfw_obj_lheader *)buf;
	cfg = (ipfw_nat64lsn_cfg *)(olh + 1);

	/* Some reasonable defaults */
	inet_pton(AF_INET6, "64:ff9b::", &cfg->prefix6);
	cfg->plen6 = 96;
	cfg->set = set;
	cfg->max_ports = NAT64LSN_MAX_PORTS;
	cfg->jmaxlen = NAT64LSN_JMAXLEN;
	cfg->nh_delete_delay = NAT64LSN_HOST_AGE;
	cfg->pg_delete_delay = NAT64LSN_PG_AGE;
	cfg->st_syn_ttl = NAT64LSN_TCP_SYN_AGE;
	cfg->st_estab_ttl = NAT64LSN_TCP_EST_AGE;
	cfg->st_close_ttl = NAT64LSN_TCP_FIN_AGE;
	cfg->st_udp_ttl = NAT64LSN_UDP_AGE;
	cfg->st_icmp_ttl = NAT64LSN_ICMP_AGE;
	flags = NAT64LSN_HAS_PREFIX6;
	while (ac > 0) {
		tcmd = get_token(nat64newcmds, *av, "option");
		opt = *av;
		ac--; av++;

		switch (tcmd) {
		case TOK_PREFIX4:
			NEED1("IPv4 prefix required");
			nat64lsn_parse_prefix(*av, AF_INET, &cfg->prefix4,
			    &cfg->plen4);
			flags |= NAT64LSN_HAS_PREFIX4;
			ac--; av++;
			break;
		case TOK_PREFIX6:
			NEED1("IPv6 prefix required");
			nat64lsn_parse_prefix(*av, AF_INET6, &cfg->prefix6,
			    &cfg->plen6);
			if (ipfw_check_nat64prefix(&cfg->prefix6,
			    cfg->plen6) != 0 &&
			    !IN6_IS_ADDR_UNSPECIFIED(&cfg->prefix6))
				errx(EX_USAGE, "Bad prefix6 %s", *av);

			ac--; av++;
			break;
		case TOK_JMAXLEN:
			NEED1("job queue length required");
			cfg->jmaxlen = nat64lsn_parse_int(*av, opt);
			ac--; av++;
			break;
		case TOK_MAX_PORTS:
			NEED1("Max per-user ports required");
			cfg->max_ports = nat64lsn_parse_int(*av, opt);
			ac--; av++;
			break;
		case TOK_HOST_DEL_AGE:
			NEED1("host delete delay required");
			cfg->nh_delete_delay = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_PG_DEL_AGE:
			NEED1("portgroup delete delay required");
			cfg->pg_delete_delay = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_TCP_SYN_AGE:
			NEED1("tcp syn age required");
			cfg->st_syn_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_TCP_CLOSE_AGE:
			NEED1("tcp close age required");
			cfg->st_close_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_TCP_EST_AGE:
			NEED1("tcp est age required");
			cfg->st_estab_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_UDP_AGE:
			NEED1("udp age required");
			cfg->st_udp_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_ICMP_AGE:
			NEED1("icmp age required");
			cfg->st_icmp_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_STATES_CHUNKS:
			NEED1("number of chunks required");
			cfg->states_chunks = (uint8_t)nat64lsn_parse_int(
			    *av, opt);
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
	if ((flags & NAT64LSN_HAS_PREFIX4) != NAT64LSN_HAS_PREFIX4)
		errx(EX_USAGE, "prefix4 required");

	olh->count = 1;
	olh->objsize = sizeof(*cfg);
	olh->size = sizeof(buf);
	strlcpy(cfg->name, name, sizeof(cfg->name));
	if (do_set3(IP_FW_NAT64LSN_CREATE, &olh->opheader, sizeof(buf)) != 0)
		err(EX_OSERR, "nat64lsn instance creation failed");
}

/*
 * Configures existing nat64lsn instance
 * ipfw nat64lsn <NAME> config <options>
 * Request: [ ipfw_obj_header ipfw_nat64lsn_cfg ]
 */
static void
nat64lsn_config(const char *name, uint8_t set, int ac, char **av)
{
	char buf[sizeof(ipfw_obj_header) + sizeof(ipfw_nat64lsn_cfg)];
	ipfw_nat64lsn_cfg *cfg;
	ipfw_obj_header *oh;
	size_t sz;
	char *opt;
	int tcmd;

	if (ac == 0)
		errx(EX_USAGE, "config options required");
	memset(&buf, 0, sizeof(buf));
	oh = (ipfw_obj_header *)buf;
	cfg = (ipfw_nat64lsn_cfg *)(oh + 1);
	sz = sizeof(buf);

	nat64lsn_fill_ntlv(&oh->ntlv, name, set);
	if (do_get3(IP_FW_NAT64LSN_CONFIG, &oh->opheader, &sz) != 0)
		err(EX_OSERR, "failed to get config for instance %s", name);

	while (ac > 0) {
		tcmd = get_token(nat64newcmds, *av, "option");
		opt = *av;
		ac--; av++;

		switch (tcmd) {
		case TOK_MAX_PORTS:
			NEED1("Max per-user ports required");
			cfg->max_ports = nat64lsn_parse_int(*av, opt);
			ac--; av++;
			break;
		case TOK_JMAXLEN:
			NEED1("job queue length required");
			cfg->jmaxlen = nat64lsn_parse_int(*av, opt);
			ac--; av++;
			break;
		case TOK_HOST_DEL_AGE:
			NEED1("host delete delay required");
			cfg->nh_delete_delay = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_PG_DEL_AGE:
			NEED1("portgroup delete delay required");
			cfg->pg_delete_delay = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_TCP_SYN_AGE:
			NEED1("tcp syn age required");
			cfg->st_syn_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_TCP_CLOSE_AGE:
			NEED1("tcp close age required");
			cfg->st_close_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_TCP_EST_AGE:
			NEED1("tcp est age required");
			cfg->st_estab_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_UDP_AGE:
			NEED1("udp age required");
			cfg->st_udp_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_ICMP_AGE:
			NEED1("icmp age required");
			cfg->st_icmp_ttl = (uint16_t)nat64lsn_parse_int(
			    *av, opt);
			ac--; av++;
			break;
		case TOK_STATES_CHUNKS:
			NEED1("number of chunks required");
			cfg->states_chunks = (uint8_t)nat64lsn_parse_int(
			    *av, opt);
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
		default:
			errx(EX_USAGE, "Can't change %s option", opt);
		}
	}

	if (do_set3(IP_FW_NAT64LSN_CONFIG, &oh->opheader, sizeof(buf)) != 0)
		err(EX_OSERR, "nat64lsn instance configuration failed");
}

/*
 * Reset nat64lsn instance statistics specified by @oh->ntlv.
 * Request: [ ipfw_obj_header ]
 */
static void
nat64lsn_reset_stats(const char *name, uint8_t set)
{
	ipfw_obj_header oh;

	memset(&oh, 0, sizeof(oh));
	nat64lsn_fill_ntlv(&oh.ntlv, name, set);
	if (do_set3(IP_FW_NAT64LSN_RESET_STATS, &oh.opheader, sizeof(oh)) != 0)
		err(EX_OSERR, "failed to reset stats for instance %s", name);
}

/*
 * Destroys nat64lsn instance specified by @oh->ntlv.
 * Request: [ ipfw_obj_header ]
 */
static void
nat64lsn_destroy(const char *name, uint8_t set)
{
	ipfw_obj_header oh;

	memset(&oh, 0, sizeof(oh));
	nat64lsn_fill_ntlv(&oh.ntlv, name, set);
	if (do_set3(IP_FW_NAT64LSN_DESTROY, &oh.opheader, sizeof(oh)) != 0)
		err(EX_OSERR, "failed to destroy nat instance %s", name);
}

/*
 * Get nat64lsn instance statistics.
 * Request: [ ipfw_obj_header ]
 * Reply: [ ipfw_obj_header ipfw_obj_ctlv [ uint64_t x N ] ]
 */
static int
nat64lsn_get_stats(const char *name, uint8_t set,
    struct ipfw_nat64lsn_stats *stats)
{
	ipfw_obj_header *oh;
	ipfw_obj_ctlv *oc;
	size_t sz;

	sz = sizeof(*oh) + sizeof(*oc) + sizeof(*stats);
	oh = calloc(1, sz);
	nat64lsn_fill_ntlv(&oh->ntlv, name, set);
	if (do_get3(IP_FW_NAT64LSN_STATS, &oh->opheader, &sz) == 0) {
		oc = (ipfw_obj_ctlv *)(oh + 1);
		memcpy(stats, oc + 1, sizeof(*stats));
		free(oh);
		return (0);
	}
	free(oh);
	return (-1);
}

static void
nat64lsn_stats(const char *name, uint8_t set)
{
	struct ipfw_nat64lsn_stats stats;

	if (nat64lsn_get_stats(name, set, &stats) != 0)
		err(EX_OSERR, "Error retrieving stats");

	if (co.use_set != 0 || set != 0)
		printf("set %u ", set);
	printf("nat64lsn %s\n", name);
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
	printf("\t%ju packets not matched with IPv4 prefix\n",
	    (uintmax_t)stats.nomatch4);

	printf("\t%ju mbufs queued for post processing\n",
	    (uintmax_t)stats.jreinjected);
	printf("\t%ju times the job queue was processed\n",
	    (uintmax_t)stats.jcalls);
	printf("\t%ju job requests queued\n",
	    (uintmax_t)stats.jrequests);
	printf("\t%ju job requests queue limit reached\n",
	    (uintmax_t)stats.jmaxlen);
	printf("\t%ju job requests failed due to memory allocation problems\n",
	    (uintmax_t)stats.jnomem);

	printf("\t%ju hosts allocated\n", (uintmax_t)stats.hostcount);
	printf("\t%ju hosts requested\n", (uintmax_t)stats.jhostsreq);
	printf("\t%ju host requests failed\n", (uintmax_t)stats.jhostfails);

	printf("\t%ju portgroups requested\n", (uintmax_t)stats.jportreq);
	printf("\t%ju portgroups allocated\n", (uintmax_t)stats.spgcreated);
	printf("\t%ju portgroups deleted\n", (uintmax_t)stats.spgdeleted);
	printf("\t%ju portgroup requests failed\n",
	    (uintmax_t)stats.jportfails);
	printf("\t%ju portgroups allocated for TCP\n",
	    (uintmax_t)stats.tcpchunks);
	printf("\t%ju portgroups allocated for UDP\n",
	    (uintmax_t)stats.udpchunks);
	printf("\t%ju portgroups allocated for ICMP\n",
	    (uintmax_t)stats.icmpchunks);

	printf("\t%ju states created\n", (uintmax_t)stats.screated);
	printf("\t%ju states deleted\n", (uintmax_t)stats.sdeleted);
}

static int
nat64lsn_show_cb(ipfw_nat64lsn_cfg *cfg, const char *name, uint8_t set)
{
	char abuf[INET6_ADDRSTRLEN];

	if (name != NULL && strcmp(cfg->name, name) != 0)
		return (ESRCH);

	if (co.use_set != 0 && cfg->set != set)
		return (ESRCH);

	if (co.use_set != 0 || cfg->set != 0)
		printf("set %u ", cfg->set);
	inet_ntop(AF_INET, &cfg->prefix4, abuf, sizeof(abuf));
	printf("nat64lsn %s prefix4 %s/%u", cfg->name, abuf, cfg->plen4);
	inet_ntop(AF_INET6, &cfg->prefix6, abuf, sizeof(abuf));
	printf(" prefix6 %s/%u", abuf, cfg->plen6);
	if (co.verbose || cfg->states_chunks > 1)
		printf(" states_chunks %u", cfg->states_chunks);
	if (co.verbose || cfg->nh_delete_delay != NAT64LSN_HOST_AGE)
		printf(" host_del_age %u", cfg->nh_delete_delay);
	if (co.verbose || cfg->pg_delete_delay != NAT64LSN_PG_AGE)
		printf(" pg_del_age %u", cfg->pg_delete_delay);
	if (co.verbose || cfg->st_syn_ttl != NAT64LSN_TCP_SYN_AGE)
		printf(" tcp_syn_age %u", cfg->st_syn_ttl);
	if (co.verbose || cfg->st_close_ttl != NAT64LSN_TCP_FIN_AGE)
		printf(" tcp_close_age %u", cfg->st_close_ttl);
	if (co.verbose || cfg->st_estab_ttl != NAT64LSN_TCP_EST_AGE)
		printf(" tcp_est_age %u", cfg->st_estab_ttl);
	if (co.verbose || cfg->st_udp_ttl != NAT64LSN_UDP_AGE)
		printf(" udp_age %u", cfg->st_udp_ttl);
	if (co.verbose || cfg->st_icmp_ttl != NAT64LSN_ICMP_AGE)
		printf(" icmp_age %u", cfg->st_icmp_ttl);
	if (co.verbose || cfg->jmaxlen != NAT64LSN_JMAXLEN)
		printf(" jmaxlen %u", cfg->jmaxlen);
	if (cfg->flags & NAT64_LOG)
		printf(" log");
	if (cfg->flags & NAT64_ALLOW_PRIVATE)
		printf(" allow_private");
	printf("\n");
	return (0);
}

static int
nat64lsn_destroy_cb(ipfw_nat64lsn_cfg *cfg, const char *name, uint8_t set)
{

	if (co.use_set != 0 && cfg->set != set)
		return (ESRCH);

	nat64lsn_destroy(cfg->name, cfg->set);
	return (0);
}


/*
 * Compare nat64lsn instances names.
 * Honor number comparison.
 */
static int
nat64name_cmp(const void *a, const void *b)
{
	ipfw_nat64lsn_cfg *ca, *cb;

	ca = (ipfw_nat64lsn_cfg *)a;
	cb = (ipfw_nat64lsn_cfg *)b;

	if (ca->set > cb->set)
		return (1);
	else if (ca->set < cb->set)
		return (-1);
	return (stringnum_cmp(ca->name, cb->name));
}

/*
 * Retrieves nat64lsn instance list from kernel,
 * optionally sorts it and calls requested function for each instance.
 *
 * Request: [ ipfw_obj_lheader ]
 * Reply: [ ipfw_obj_lheader ipfw_nat64lsn_cfg x N ]
 */
static int
nat64lsn_foreach(nat64lsn_cb_t *f, const char *name, uint8_t set,  int sort)
{
	ipfw_obj_lheader *olh;
	ipfw_nat64lsn_cfg *cfg;
	size_t sz;
	int i, error;

	/* Start with reasonable default */
	sz = sizeof(*olh) + 16 * sizeof(ipfw_nat64lsn_cfg);

	for (;;) {
		if ((olh = calloc(1, sz)) == NULL)
			return (ENOMEM);

		olh->size = sz;
		if (do_get3(IP_FW_NAT64LSN_LIST, &olh->opheader, &sz) != 0) {
			sz = olh->size;
			free(olh);
			if (errno != ENOMEM)
				return (errno);
			continue;
		}

		if (sort != 0)
			qsort(olh + 1, olh->count, olh->objsize,
			    nat64name_cmp);

		cfg = (ipfw_nat64lsn_cfg *)(olh + 1);
		for (i = 0; i < olh->count; i++) {
			error = f(cfg, name, set); /* Ignore errors for now */
			cfg = (ipfw_nat64lsn_cfg *)((caddr_t)cfg +
			    olh->objsize);
		}
		free(olh);
		break;
	}
	return (0);
}

