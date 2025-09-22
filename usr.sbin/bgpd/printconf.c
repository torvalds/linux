/*	$OpenBSD: printconf.c,v 1.182 2025/03/10 14:11:38 claudio Exp $	*/

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2016 Job Snijders <job@instituut.net>
 * Copyright (c) 2016 Peter Hessler <phessler@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA, PROFITS OR MIND, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <arpa/inet.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bgpd.h"
#include "session.h"
#include "rde.h"
#include "log.h"

void		 print_prefix(struct filter_prefix *p);
const char	*community_type(struct community *c);
void		 print_community(struct community *c);
void		 print_origin(uint8_t);
void		 print_set(struct filter_set_head *);
void		 print_mainconf(struct bgpd_config *);
void		 print_l3vpn_targets(struct filter_set_head *, const char *);
void		 print_l3vpn(struct l3vpn *);
const char	*print_af(uint8_t);
void		 print_network(struct network_config *, const char *);
void		 print_flowspec(struct flowspec_config *, const char *);
void		 print_as_sets(struct as_set_head *);
void		 print_prefixsets(struct prefixset_head *);
void		 print_originsets(struct prefixset_head *);
void		 print_roa(struct roa_tree *);
void		 print_aspa(struct aspa_tree *);
void		 print_rtrs(struct rtr_config_head *);
void		 print_peer(struct peer *, struct bgpd_config *, const char *);
const char	*print_auth_alg(enum auth_alg);
const char	*print_enc_alg(enum auth_enc_alg);
void		 print_announce(struct peer_config *, const char *);
void		 print_as(struct filter_rule *);
void		 print_rule(struct bgpd_config *, struct filter_rule *);
const char	*mrt_type(enum mrt_type);
void		 print_mrt(struct bgpd_config *, uint32_t, uint32_t,
		    const char *, const char *);
void		 print_groups(struct bgpd_config *);
int		 peer_compare(const void *, const void *);

void
print_prefix(struct filter_prefix *p)
{
	uint8_t max_len = 0;

	switch (p->addr.aid) {
	case AID_INET:
	case AID_VPN_IPv4:
		max_len = 32;
		break;
	case AID_INET6:
	case AID_VPN_IPv6:
		max_len = 128;
		break;
	case AID_UNSPEC:
		/* no prefix to print */
		return;
	}

	printf("%s/%u", log_addr(&p->addr), p->len);

	switch (p->op) {
	case OP_NONE:
		break;
	case OP_NE:
		printf(" prefixlen != %u", p->len_min);
		break;
	case OP_XRANGE:
		printf(" prefixlen %u >< %u ", p->len_min, p->len_max);
		break;
	case OP_RANGE:
		if (p->len_min == p->len_max && p->len != p->len_min)
			printf(" prefixlen = %u", p->len_min);
		else if (p->len == p->len_min && p->len_max == max_len)
			printf(" or-longer");
		else if (p->len == p->len_min && p->len != p->len_max)
			printf(" maxlen %u", p->len_max);
		else if (p->len_max == max_len)
			printf(" prefixlen >= %u", p->len_min);
		else
			printf(" prefixlen %u - %u", p->len_min, p->len_max);
		break;
	default:
		printf(" prefixlen %u ??? %u", p->len_min, p->len_max);
		break;
	}
}

const char *
community_type(struct community *c)
{
	switch ((uint8_t)c->flags) {
	case COMMUNITY_TYPE_BASIC:
		return "community";
	case COMMUNITY_TYPE_LARGE:
		return "large-community";
	case COMMUNITY_TYPE_EXT:
		return "ext-community";
	default:
		return "???";
	}
}

void
print_community(struct community *c)
{
	struct in_addr addr;
	int type;
	uint8_t subtype;

	switch ((uint8_t)c->flags) {
	case COMMUNITY_TYPE_BASIC:
		switch ((c->flags >> 8) & 0xff) {
		case COMMUNITY_ANY:
			printf("*:");
			break;
		case COMMUNITY_NEIGHBOR_AS:
			printf("neighbor-as:");
			break;
		case COMMUNITY_LOCAL_AS:
			printf("local-as:");
			break;
		default:
			printf("%u:", c->data1);
			break;
		}
		switch ((c->flags >> 16) & 0xff) {
		case COMMUNITY_ANY:
			printf("* ");
			break;
		case COMMUNITY_NEIGHBOR_AS:
			printf("neighbor-as ");
			break;
		case COMMUNITY_LOCAL_AS:
			printf("local-as ");
			break;
		default:
			printf("%u ", c->data2);
			break;
		}
		break;
	case COMMUNITY_TYPE_LARGE:
		switch ((c->flags >> 8) & 0xff) {
		case COMMUNITY_ANY:
			printf("*:");
			break;
		case COMMUNITY_NEIGHBOR_AS:
			printf("neighbor-as:");
			break;
		case COMMUNITY_LOCAL_AS:
			printf("local-as:");
			break;
		default:
			printf("%u:", c->data1);
			break;
		}
		switch ((c->flags >> 16) & 0xff) {
		case COMMUNITY_ANY:
			printf("*:");
			break;
		case COMMUNITY_NEIGHBOR_AS:
			printf("neighbor-as:");
			break;
		case COMMUNITY_LOCAL_AS:
			printf("local-as:");
			break;
		default:
			printf("%u:", c->data2);
			break;
		}
		switch ((c->flags >> 24) & 0xff) {
		case COMMUNITY_ANY:
			printf("* ");
			break;
		case COMMUNITY_NEIGHBOR_AS:
			printf("neighbor-as ");
			break;
		case COMMUNITY_LOCAL_AS:
			printf("local-as ");
			break;
		default:
			printf("%u ", c->data3);
			break;
		}
		break;
	case COMMUNITY_TYPE_EXT:
		if ((c->flags >> 24 & 0xff) == COMMUNITY_ANY) {
			printf("* * ");
			break;
		}
		type = (int32_t)c->data3 >> 8;
		subtype = c->data3;
		printf("%s ", log_ext_subtype(type, subtype));
		if ((c->flags >> 8 & 0xff) == COMMUNITY_ANY) {
			printf("* ");
			break;
		}

		switch (type) {
		case EXT_COMMUNITY_TRANS_TWO_AS:
		case EXT_COMMUNITY_TRANS_FOUR_AS:
		case EXT_COMMUNITY_GEN_TWO_AS:
		case EXT_COMMUNITY_GEN_FOUR_AS:
			if ((c->flags >> 8 & 0xff) == COMMUNITY_NEIGHBOR_AS)
				printf("neighbor-as:");
			else if ((c->flags >> 8 & 0xff) == COMMUNITY_LOCAL_AS)
				printf("local-as:");
			else
				printf("%s:", log_as(c->data1));
			break;
		case EXT_COMMUNITY_TRANS_IPV4:
		case EXT_COMMUNITY_GEN_IPV4:
			addr.s_addr = htonl(c->data1);
			printf("%s:", inet_ntoa(addr));
			break;
		}

		switch (type) {
		case EXT_COMMUNITY_TRANS_TWO_AS:
		case EXT_COMMUNITY_TRANS_FOUR_AS:
		case EXT_COMMUNITY_TRANS_IPV4:
		case EXT_COMMUNITY_GEN_TWO_AS:
		case EXT_COMMUNITY_GEN_FOUR_AS:
		case EXT_COMMUNITY_GEN_IPV4:
			if ((c->flags >> 16 & 0xff) == COMMUNITY_ANY)
				printf("* ");
			else if ((c->flags >> 16 & 0xff) ==
			    COMMUNITY_NEIGHBOR_AS)
				printf("neighbor-as ");
			else if ((c->flags >> 16 & 0xff) == COMMUNITY_LOCAL_AS)
				printf("local-as ");
			else
				printf("%u ", c->data2);
			break;
		case EXT_COMMUNITY_NON_TRANS_OPAQUE:
			if (subtype == EXT_COMMUNITY_SUBTYPE_OVS) {
				switch (c->data2) {
				case EXT_COMMUNITY_OVS_VALID:
					printf("valid ");
					break;
				case EXT_COMMUNITY_OVS_NOTFOUND:
					printf("not-found ");
					break;
				case EXT_COMMUNITY_OVS_INVALID:
					printf("invalid ");
					break;
				}
				break;
			}
			printf("0x%x%08x ", c->data1 & 0xffff, c->data2);
			break;
		case EXT_COMMUNITY_TRANS_OPAQUE:
		case EXT_COMMUNITY_TRANS_EVPN:
		default:
			printf("0x%x%08x ", c->data1 & 0xffff, c->data2);
			break;
		}
	}
}

void
print_origin(uint8_t o)
{
	if (o == ORIGIN_IGP)
		printf("igp ");
	else if (o == ORIGIN_EGP)
		printf("egp ");
	else if (o == ORIGIN_INCOMPLETE)
		printf("incomplete ");
	else
		printf("%u ", o);
}

void
print_set(struct filter_set_head *set)
{
	struct filter_set	*s;

	if (TAILQ_EMPTY(set))
		return;

	printf("set { ");
	TAILQ_FOREACH(s, set, entry) {
		switch (s->type) {
		case ACTION_SET_LOCALPREF:
			printf("localpref %u ", s->action.metric);
			break;
		case ACTION_SET_RELATIVE_LOCALPREF:
			printf("localpref %+d ", s->action.relative);
			break;
		case ACTION_SET_MED:
			printf("metric %u ", s->action.metric);
			break;
		case ACTION_SET_RELATIVE_MED:
			printf("metric %+d ", s->action.relative);
			break;
		case ACTION_SET_WEIGHT:
			printf("weight %u ", s->action.metric);
			break;
		case ACTION_SET_RELATIVE_WEIGHT:
			printf("weight %+d ", s->action.relative);
			break;
		case ACTION_SET_NEXTHOP:
			printf("nexthop %s ", log_addr(&s->action.nexthop));
			break;
		case ACTION_SET_NEXTHOP_REJECT:
			printf("nexthop reject ");
			break;
		case ACTION_SET_NEXTHOP_BLACKHOLE:
			printf("nexthop blackhole ");
			break;
		case ACTION_SET_NEXTHOP_NOMODIFY:
			printf("nexthop no-modify ");
			break;
		case ACTION_SET_NEXTHOP_SELF:
			printf("nexthop self ");
			break;
		case ACTION_SET_PREPEND_SELF:
			printf("prepend-self %u ", s->action.prepend);
			break;
		case ACTION_SET_PREPEND_PEER:
			printf("prepend-neighbor %u ", s->action.prepend);
			break;
		case ACTION_SET_AS_OVERRIDE:
			printf("as-override ");
			break;
		case ACTION_DEL_COMMUNITY:
			printf("%s delete ",
			    community_type(&s->action.community));
			print_community(&s->action.community);
			break;
		case ACTION_SET_COMMUNITY:
			printf("%s ", community_type(&s->action.community));
			print_community(&s->action.community);
			break;
		case ACTION_PFTABLE:
			printf("pftable %s ", s->action.pftable);
			break;
		case ACTION_RTLABEL:
			printf("rtlabel %s ", s->action.rtlabel);
			break;
		case ACTION_SET_ORIGIN:
			printf("origin ");
			print_origin(s->action.origin);
			break;
		case ACTION_RTLABEL_ID:
		case ACTION_PFTABLE_ID:
		case ACTION_SET_NEXTHOP_REF:
			/* not possible */
			printf("king bula saiz: config broken");
			break;
		}
	}
	printf("}");
}

void
print_mainconf(struct bgpd_config *conf)
{
	struct in_addr		 ina;
	struct listen_addr	*la;

	printf("AS %s", log_as(conf->as));
	if (conf->as > USHRT_MAX && conf->short_as != AS_TRANS)
		printf(" %u", conf->short_as);
	ina.s_addr = htonl(conf->bgpid);
	printf("\nrouter-id %s\n", inet_ntoa(ina));

	printf("socket \"%s\"\n", conf->csock);
	if (conf->rcsock)
		printf("socket \"%s\" restricted\n", conf->rcsock);
	if (conf->holdtime != INTERVAL_HOLD)
		printf("holdtime %u\n", conf->holdtime);
	if (conf->min_holdtime != MIN_HOLDTIME)
		printf("holdtime min %u\n", conf->min_holdtime);
	if (conf->connectretry != INTERVAL_CONNECTRETRY)
		printf("connect-retry %u\n", conf->connectretry);
	if (conf->staletime != INTERVAL_STALE)
		printf("staletime %u\n", conf->staletime);

	if (conf->flags & BGPD_FLAG_DECISION_ROUTEAGE)
		printf("rde route-age evaluate\n");
	if (conf->flags & BGPD_FLAG_DECISION_MED_ALWAYS)
		printf("rde med compare always\n");
	if (conf->flags & BGPD_FLAG_DECISION_ALL_PATHS)
		printf("rde evaluate all\n");

	if (conf->flags & BGPD_FLAG_PERMIT_AS_SET)
		printf("reject as-set no\n");

	if (conf->log & BGPD_LOG_UPDATES)
		printf("log updates\n");

	TAILQ_FOREACH(la, conf->listen_addrs, entry) {
		struct bgpd_addr addr;
		uint16_t port;

		sa2addr((struct sockaddr *)&la->sa, &addr, &port);
		printf("listen on %s",
		    log_sockaddr((struct sockaddr *)&la->sa, la->sa_len));
		if (port != BGP_PORT)
			printf(" port %hu", port);
		printf("\n");
	}

	if (conf->flags & BGPD_FLAG_NEXTHOP_BGP)
		printf("nexthop qualify via bgp\n");
	if (conf->flags & BGPD_FLAG_NEXTHOP_DEFAULT)
		printf("nexthop qualify via default\n");
	if (conf->fib_priority != kr_default_prio())
		printf("fib-priority %hhu\n", conf->fib_priority);
	printf("\n");
}

void
print_l3vpn_targets(struct filter_set_head *set, const char *tgt)
{
	struct filter_set	*s;
	TAILQ_FOREACH(s, set, entry) {
		printf("\t%s ", tgt);
		print_community(&s->action.community);
		printf("\n");
	}
}

void
print_l3vpn(struct l3vpn *vpn)
{
	struct network *n;

	printf("vpn \"%s\" on %s {\n", vpn->descr, vpn->ifmpe);
	printf("\t%s\n", log_rd(vpn->rd));

	print_l3vpn_targets(&vpn->export, "export-target");
	print_l3vpn_targets(&vpn->import, "import-target");

	if (vpn->flags & F_RIB_NOFIBSYNC)
		printf("\tfib-update no\n");
	else
		printf("\tfib-update yes\n");

	TAILQ_FOREACH(n, &vpn->net_l, entry)
		print_network(&n->net, "\t");

	printf("}\n");
}

const char *
print_af(uint8_t aid)
{
	/*
	 * Hack around the fact that aid2str() will return "IPv4 unicast"
	 * for AID_INET. AID_INET, AID_INET6 and the flowspec AID need
	 * special handling and the other AID should never end up here.
	 */
	if (aid == AID_INET || aid == AID_FLOWSPECv4)
		return ("inet");
	if (aid == AID_INET6 || aid == AID_FLOWSPECv6)
		return ("inet6");
	return (aid2str(aid));
}

void
print_network(struct network_config *n, const char *c)
{
	switch (n->type) {
	case NETWORK_STATIC:
		printf("%snetwork %s static", c, print_af(n->prefix.aid));
		break;
	case NETWORK_CONNECTED:
		printf("%snetwork %s connected", c, print_af(n->prefix.aid));
		break;
	case NETWORK_RTLABEL:
		printf("%snetwork %s rtlabel \"%s\"", c,
		    print_af(n->prefix.aid), rtlabel_id2name(n->rtlabel));
		break;
	case NETWORK_PRIORITY:
		printf("%snetwork %s priority %d", c,
		    print_af(n->prefix.aid), n->priority);
		break;
	case NETWORK_PREFIXSET:
		printf("%snetwork prefix-set %s", c, n->psname);
		break;
	default:
		printf("%snetwork %s/%u", c, log_addr(&n->prefix),
		    n->prefixlen);
		break;
	}
	if (!TAILQ_EMPTY(&n->attrset))
		printf(" ");
	print_set(&n->attrset);
	printf("\n");
}

static void
print_flowspec_list(struct flowspec *f, int type, int is_v6)
{
	const uint8_t *comp;
	const char *fmt;
	int complen, off = 0;

	if (flowspec_get_component(f->data, f->len, type, is_v6,
	    &comp, &complen) != 1)
		return;

	printf("%s ", flowspec_fmt_label(type));
	fmt = flowspec_fmt_num_op(comp, complen, &off);
	if (off == -1) {
		printf("%s ", fmt);
	} else {
		printf("{ %s ", fmt);
		do {
			fmt = flowspec_fmt_num_op(comp, complen, &off);
			printf("%s ", fmt);
		} while (off != -1);
		printf("} ");
	}
}

static void
print_flowspec_flags(struct flowspec *f, int type, int is_v6)
{
	const uint8_t *comp;
	const char *fmt, *flags;
	int complen, off = 0;

	switch (type) {
	case FLOWSPEC_TYPE_TCP_FLAGS:
		flags = FLOWSPEC_TCP_FLAG_STRING;
		break;
	case FLOWSPEC_TYPE_FRAG:
		if (!is_v6)
			flags = FLOWSPEC_FRAG_STRING4;
		else
			flags = FLOWSPEC_FRAG_STRING6;
		break;
	default:
		printf("??? ");
		return;
	}

	if (flowspec_get_component(f->data, f->len, type, is_v6,
	    &comp, &complen) != 1)
		return;

	printf("%s ", flowspec_fmt_label(type));

	fmt = flowspec_fmt_bin_op(comp, complen, &off, flags);
	if (off == -1) {
		printf("%s ", fmt);
	} else {
		printf("{ %s ", fmt);
		do {
			fmt = flowspec_fmt_bin_op(comp, complen, &off, flags);
			printf("%s ", fmt);
		} while (off != -1);
		printf("} ");
	}
}

static void
print_flowspec_addr(struct flowspec *f, int type, int is_v6)
{
	struct bgpd_addr addr;
	uint8_t plen;

	flowspec_get_addr(f->data, f->len, type, is_v6, &addr, &plen, NULL);
	if (plen == 0)
		printf("%s any ", flowspec_fmt_label(type));
	else
		printf("%s %s/%u ", flowspec_fmt_label(type),
		    log_addr(&addr), plen);
}

void
print_flowspec(struct flowspec_config *fconf, const char *c)
{
	struct flowspec *f = fconf->flow;
	int is_v6 = (f->aid == AID_FLOWSPECv6);

	printf("%sflowspec %s ", c, print_af(f->aid));

	print_flowspec_list(f, FLOWSPEC_TYPE_PROTO, is_v6);

	print_flowspec_addr(f, FLOWSPEC_TYPE_SOURCE, is_v6);
	print_flowspec_list(f, FLOWSPEC_TYPE_SRC_PORT, is_v6);

	print_flowspec_addr(f, FLOWSPEC_TYPE_DEST, is_v6);
	print_flowspec_list(f, FLOWSPEC_TYPE_DST_PORT, is_v6);

	print_flowspec_list(f, FLOWSPEC_TYPE_DSCP, is_v6);
	print_flowspec_list(f, FLOWSPEC_TYPE_PKT_LEN, is_v6);
	print_flowspec_flags(f, FLOWSPEC_TYPE_TCP_FLAGS, is_v6);
	print_flowspec_flags(f, FLOWSPEC_TYPE_FRAG, is_v6);

	/* TODO: fixup the code handling to be like in the parser */
	print_flowspec_list(f, FLOWSPEC_TYPE_ICMP_TYPE, is_v6);
	print_flowspec_list(f, FLOWSPEC_TYPE_ICMP_CODE, is_v6);

	print_set(&fconf->attrset);
	printf("\n");
}

void
print_as_sets(struct as_set_head *as_sets)
{
	struct as_set *aset;
	uint32_t *as;
	size_t i, n;
	int len;

	SIMPLEQ_FOREACH(aset, as_sets, entry) {
		printf("as-set \"%s\" {\n\t", aset->name);
		as = set_get(aset->set, &n);
		for (i = 0, len = 8; i < n; i++) {
			if (len > 72) {
				printf("\n\t");
				len = 8;
			}
			len += printf("%u ", as[i]);
		}
		printf("\n}\n\n");
	}
}

void
print_prefixsets(struct prefixset_head *psh)
{
	struct prefixset	*ps;
	struct prefixset_item	*psi;

	SIMPLEQ_FOREACH(ps, psh, entry) {
		int count = 0;
		printf("prefix-set \"%s\" {", ps->name);
		RB_FOREACH(psi, prefixset_tree, &ps->psitems) {
			if (count++ % 2 == 0)
				printf("\n\t");
			else
				printf(", ");
			print_prefix(&psi->p);
		}
		printf("\n}\n\n");
	}
}

void
print_originsets(struct prefixset_head *psh)
{
	struct prefixset	*ps;
	struct roa		*roa;
	struct bgpd_addr	 addr;

	SIMPLEQ_FOREACH(ps, psh, entry) {
		printf("origin-set \"%s\" {", ps->name);
		RB_FOREACH(roa, roa_tree, &ps->roaitems) {
			printf("\n\t");
			addr.aid = roa->aid;
			addr.v6 = roa->prefix.inet6;
			printf("%s/%u", log_addr(&addr), roa->prefixlen);
			if (roa->prefixlen != roa->maxlen)
				printf(" maxlen %u", roa->maxlen);
			printf(" source-as %u", roa->asnum);
		}
		printf("\n}\n\n");
	}
}

void
print_roa(struct roa_tree *r)
{
	struct roa	*roa;

	if (RB_EMPTY(r))
		return;

	printf("roa-set {");
	RB_FOREACH(roa, roa_tree, r) {
		printf("\n\t%s", log_roa(roa));
	}
	printf("\n}\n\n");
}

void
print_aspa(struct aspa_tree *a)
{
	struct aspa_set	*aspa;

	if (RB_EMPTY(a))
		return;

	printf("aspa-set {");
	RB_FOREACH(aspa, aspa_tree, a) {
		printf("\n\t%s", log_aspa(aspa));
	}
	printf("\n}\n\n");
}

static void
print_auth(struct auth_config *auth, const char *c)
{
	char *method;

	if (auth->method == AUTH_MD5SIG)
		printf("%s\ttcp md5sig\n", c);
	else if (auth->method == AUTH_IPSEC_MANUAL_ESP ||
	    auth->method == AUTH_IPSEC_MANUAL_AH) {
		if (auth->method == AUTH_IPSEC_MANUAL_ESP)
			method = "esp";
		else
			method = "ah";

		printf("%s\tipsec %s in spi %u %s XXXXXX", c, method,
		    auth->spi_in, print_auth_alg(auth->auth_alg_in));
		if (auth->enc_alg_in)
			printf(" %s XXXXXX", print_enc_alg(auth->enc_alg_in));
		printf("\n");

		printf("%s\tipsec %s out spi %u %s XXXXXX", c, method,
		    auth->spi_out, print_auth_alg(auth->auth_alg_out));
		if (auth->enc_alg_out)
			printf(" %s XXXXXX",
			    print_enc_alg(auth->enc_alg_out));
		printf("\n");
	} else if (auth->method == AUTH_IPSEC_IKE_AH)
		printf("%s\tipsec ah ike\n", c);
	else if (auth->method == AUTH_IPSEC_IKE_ESP)
		printf("%s\tipsec esp ike\n", c);

}

void
print_rtrs(struct rtr_config_head *rh)
{
	struct rtr_config *r;

	SIMPLEQ_FOREACH(r, rh, entry) {
		printf("rtr %s {\n", log_addr(&r->remote_addr));
		printf("\tdescr \"%s\"\n", r->descr);
		printf("\tport %u\n", r->remote_port);
		if (r->local_addr.aid != AID_UNSPEC)
			printf("local-addr %s\n", log_addr(&r->local_addr));
		print_auth(&r->auth, "");
		printf("}\n\n");
	}
}

void
print_peer(struct peer *peer, struct bgpd_config *conf, const char *c)
{
	struct in_addr		 ina;
	struct peer_config	*p = &peer->conf;

	if ((p->remote_addr.aid == AID_INET && p->remote_masklen != 32) ||
	    (p->remote_addr.aid == AID_INET6 && p->remote_masklen != 128))
		printf("%sneighbor %s/%u {\n", c, log_addr(&p->remote_addr),
		    p->remote_masklen);
	else
		printf("%sneighbor %s {\n", c, log_addr(&p->remote_addr));
	if (p->descr[0])
		printf("%s\tdescr \"%s\"\n", c, p->descr);
	if (p->rib[0])
		printf("%s\trib \"%s\"\n", c, p->rib);
	if (p->remote_as)
		printf("%s\tremote-as %s\n", c, log_as(p->remote_as));
	if (p->local_as != conf->as) {
		printf("%s\tlocal-as %s", c, log_as(p->local_as));
		if (p->local_as > USHRT_MAX && p->local_short_as != AS_TRANS)
			printf(" %u", p->local_short_as);
		printf("\n");
	}
	if (p->down)
		printf("%s\tdown\n", c);
	if (p->distance > 1)
		printf("%s\tmultihop %u\n", c, p->distance);
	if (p->passive)
		printf("%s\tpassive\n", c);
	if (p->local_addr_v4.aid)
		printf("%s\tlocal-address %s\n", c,
		    log_addr(&p->local_addr_v4));
	if (p->local_addr_v6.aid)
		printf("%s\tlocal-address %s\n", c,
		    log_addr(&p->local_addr_v6));
	if (p->remote_port != BGP_PORT)
		printf("%s\tport %hu\n", c, p->remote_port);
	if (p->role != ROLE_NONE)
		printf("%s\trole %s\n", c, log_policy(p->role));
	if (p->max_prefix) {
		printf("%s\tmax-prefix %u", c, p->max_prefix);
		if (p->max_prefix_restart)
			printf(" restart %u", p->max_prefix_restart);
		printf("\n");
	}
	if (p->max_out_prefix) {
		printf("%s\tmax-prefix %u out", c, p->max_out_prefix);
		if (p->max_out_prefix_restart)
			printf(" restart %u", p->max_out_prefix_restart);
		printf("\n");
	}
	if (p->holdtime)
		printf("%s\tholdtime %u\n", c, p->holdtime);
	if (p->min_holdtime)
		printf("%s\tholdtime min %u\n", c, p->min_holdtime);
	if (p->staletime)
		printf("%s\tstaletime %u\n", c, p->staletime);
	if (p->export_type == EXPORT_NONE)
		printf("%s\texport none\n", c);
	else if (p->export_type == EXPORT_DEFAULT_ROUTE)
		printf("%s\texport default-route\n", c);
	if (p->enforce_as == ENFORCE_AS_ON)
		printf("%s\tenforce neighbor-as yes\n", c);
	else
		printf("%s\tenforce neighbor-as no\n", c);
	if (p->enforce_local_as == ENFORCE_AS_ON)
		printf("%s\tenforce local-as yes\n", c);
	else
		printf("%s\tenforce local-as no\n", c);
	if (p->reflector_client) {
		if (conf->clusterid == 0)
			printf("%s\troute-reflector\n", c);
		else {
			ina.s_addr = htonl(conf->clusterid);
			printf("%s\troute-reflector %s\n", c,
			    inet_ntoa(ina));
		}
	}
	if (p->demote_group[0])
		printf("%s\tdemote %s\n", c, p->demote_group);
	if (p->if_depend[0])
		printf("%s\tdepend on \"%s\"\n", c, p->if_depend);
	if (p->flags & PEERFLAG_TRANS_AS)
		printf("%s\ttransparent-as yes\n", c);

	if (conf->flags & BGPD_FLAG_DECISION_ALL_PATHS) {
		if (!(p->flags & PEERFLAG_EVALUATE_ALL))
			printf("%s\trde evaluate default\n", c);
	} else {
		if (p->flags & PEERFLAG_EVALUATE_ALL)
			printf("%s\trde evaluate all\n", c);
	}

	if (conf->flags & BGPD_FLAG_PERMIT_AS_SET) {
		if (!(p->flags & PEERFLAG_PERMIT_AS_SET))
			printf("%s\treject as-set yes\n", c);
	} else {
		if (p->flags & PEERFLAG_PERMIT_AS_SET)
			printf("%s\treject as-set no\n", c);
	}

	if (p->flags & PEERFLAG_LOG_UPDATES)
		printf("%s\tlog updates\n", c);

	print_auth(&peer->auth_conf, c);

	if (p->ttlsec)
		printf("%s\tttl-security yes\n", c);

	print_announce(p, c);

	print_mrt(conf, p->id, p->groupid, c, "\t");

	printf("%s}\n", c);
}

const char *
print_auth_alg(enum auth_alg alg)
{
	switch (alg) {
	case AUTH_AALG_SHA1HMAC:
		return ("sha1");
	case AUTH_AALG_MD5HMAC:
		return ("md5");
	default:
		return ("???");
	}
}

const char *
print_enc_alg(enum auth_enc_alg alg)
{
	switch (alg) {
	case AUTH_EALG_3DESCBC:
		return ("3des");
	case AUTH_EALG_AES:
		return ("aes");
	default:
		return ("???");
	}
}

static const char *
print_addpath_mode(enum addpath_mode mode)
{
	switch (mode) {
	case ADDPATH_EVAL_NONE:
		return "none";
	case ADDPATH_EVAL_BEST:
		return "best";
	case ADDPATH_EVAL_ECMP:
		return "ecmp";
	case ADDPATH_EVAL_AS_WIDE:
		return "as-wide-best";
	case ADDPATH_EVAL_ALL:
		return "all";
	default:
		return "???";
	}
}

void
print_announce(struct peer_config *p, const char *c)
{
	uint8_t	aid;
	int match = 0;

	for (aid = AID_MIN; aid < AID_MAX; aid++)
		if (p->capabilities.mp[aid] == 2) {
			printf("%s\tannounce %s enforce\n", c, aid2str(aid));
			match = 1;
		} else if (p->capabilities.mp[aid]) {
			printf("%s\tannounce %s\n", c, aid2str(aid));
			match = 1;
		}
	if (!match) {
		printf("%s\tannounce IPv4 none\n", c);
		printf("%s\tannounce IPv6 none\n", c);
	}

	if (p->capabilities.refresh == 2)
		printf("%s\tannounce refresh enforce\n", c);
	else if (p->capabilities.refresh == 0)
		printf("%s\tannounce refresh no\n", c);

	if (p->capabilities.enhanced_rr == 2)
		printf("%s\tannounce enhanced refresh enforce\n", c);
	else if (p->capabilities.enhanced_rr == 1)
		printf("%s\tannounce enhanced refresh yes\n", c);

	if (p->capabilities.grestart.restart == 2)
		printf("%s\tannounce restart enforce\n", c);
	else if (p->capabilities.grestart.restart == 0)
		printf("%s\tannounce restart no\n", c);

	if (p->capabilities.grestart.restart != 0 &&
	    p->capabilities.grestart.grnotification)
		printf("%s\tannounce graceful notification yes\n", c);

	if (p->capabilities.as4byte == 2)
		printf("%s\tannounce as4byte enforce\n", c);
	else if (p->capabilities.as4byte == 0)
		printf("%s\tannounce as4byte no\n", c);

	if (p->capabilities.ext_msg == 2)
		printf("%s\tannounce extended message enforce\n", c);
	else if (p->capabilities.ext_msg == 1)
		printf("%s\tannounce extended message yes\n", c);

	if (p->capabilities.ext_nh[AID_INET] == 2)
		printf("%s\tannounce extended nexthop enforce\n", c);
	else if (p->capabilities.ext_nh[AID_INET] == 1)
		printf("%s\tannounce extended nexthop yes\n", c);

	if (p->capabilities.add_path[AID_MIN] & CAPA_AP_RECV_ENFORCE)
		printf("%s\tannounce add-path recv enforce\n", c);
	else if (p->capabilities.add_path[AID_MIN] & CAPA_AP_RECV)
		printf("%s\tannounce add-path recv yes\n", c);

	if (p->capabilities.add_path[AID_MIN] & CAPA_AP_SEND) {
		printf("%s\tannounce add-path send %s", c,
		    print_addpath_mode(p->eval.mode));
		if (p->eval.extrapaths != 0)
			printf(" plus %d", p->eval.extrapaths);
		if (p->eval.maxpaths != 0)
			printf(" max %d", p->eval.maxpaths);
		if (p->capabilities.add_path[AID_MIN] & CAPA_AP_SEND_ENFORCE)
			printf(" enforce");
		printf("\n");
	}

	if (p->capabilities.policy == 2)
		printf("%s\tannounce policy enforce\n", c);
	else if (p->capabilities.policy == 1)
		printf("%s\tannounce policy yes\n", c);
	else
		printf("%s\tannounce policy no\n", c);
}

void
print_as(struct filter_rule *r)
{
	if (r->match.as.flags & AS_FLAG_AS_SET_NAME) {
		printf("as-set \"%s\" ", r->match.as.name);
		return;
	}
	switch (r->match.as.op) {
	case OP_RANGE:
		printf("%s - ", log_as(r->match.as.as_min));
		printf("%s ", log_as(r->match.as.as_max));
		break;
	case OP_XRANGE:
		printf("%s >< ", log_as(r->match.as.as_min));
		printf("%s ", log_as(r->match.as.as_max));
		break;
	case OP_NE:
		printf("!= %s ", log_as(r->match.as.as_min));
		break;
	default:
		printf("%s ", log_as(r->match.as.as_min));
		break;
	}
}

void
print_rule(struct bgpd_config *conf, struct filter_rule *r)
{
	struct peer *p;
	int i;

	if (r->action == ACTION_ALLOW)
		printf("allow ");
	else if (r->action == ACTION_DENY)
		printf("deny ");
	else
		printf("match ");
	if (r->quick)
		printf("quick ");

	if (r->rib[0])
		printf("rib %s ", r->rib);

	if (r->dir == DIR_IN)
		printf("from ");
	else if (r->dir == DIR_OUT)
		printf("to ");
	else
		printf("eeeeeeeps. ");

	if (r->peer.peerid) {
		RB_FOREACH(p, peer_head, &conf->peers)
			if (p->conf.id == r->peer.peerid)
				break;
		if (p == NULL)
			printf("? ");
		else
			printf("%s ", log_addr(&p->conf.remote_addr));
	} else if (r->peer.groupid) {
		RB_FOREACH(p, peer_head, &conf->peers)
			if (p->conf.groupid == r->peer.groupid)
				break;
		if (p == NULL)
			printf("group ? ");
		else
			printf("group \"%s\" ", p->conf.group);
	} else if (r->peer.remote_as) {
		printf("AS %s ", log_as(r->peer.remote_as));
	} else if (r->peer.ebgp) {
		printf("ebgp ");
	} else if (r->peer.ibgp) {
		printf("ibgp ");
	} else
		printf("any ");

	if (r->match.ovs.is_set) {
		switch (r->match.ovs.validity) {
		case ROA_VALID:
			printf("ovs valid ");
			break;
		case ROA_INVALID:
			printf("ovs invalid ");
			break;
		case ROA_NOTFOUND:
			printf("ovs not-found ");
			break;
		default:
			printf("ovs ??? %d ??? ", r->match.ovs.validity);
		}
	}

	if (r->match.avs.is_set) {
		switch (r->match.avs.validity) {
		case ASPA_VALID:
			printf("avs valid ");
			break;
		case ASPA_INVALID:
			printf("avs invalid ");
			break;
		case ASPA_UNKNOWN:
			printf("avs unknown ");
			break;
		default:
			printf("avs ??? %d ??? ", r->match.avs.validity);
		}
	}

	if (r->match.prefix.addr.aid != AID_UNSPEC) {
		printf("prefix ");
		print_prefix(&r->match.prefix);
		printf(" ");
	}

	if (r->match.prefixset.name[0] != '\0')
		printf("prefix-set \"%s\" ", r->match.prefixset.name);
	if (r->match.prefixset.flags & PREFIXSET_FLAG_LONGER)
		printf("or-longer ");

	if (r->match.originset.name[0] != '\0')
		printf("origin-set \"%s\" ", r->match.originset.name);

	if (r->match.nexthop.flags) {
		if (r->match.nexthop.flags == FILTER_NEXTHOP_NEIGHBOR)
			printf("nexthop neighbor ");
		else
			printf("nexthop %s ", log_addr(&r->match.nexthop.addr));
	}

	if (r->match.as.type) {
		if (r->match.as.type == AS_ALL)
			printf("AS ");
		else if (r->match.as.type == AS_SOURCE)
			printf("source-as ");
		else if (r->match.as.type == AS_TRANSIT)
			printf("transit-as ");
		else if (r->match.as.type == AS_PEER)
			printf("peer-as ");
		else
			printf("unfluffy-as ");
		print_as(r);
	}

	if (r->match.aslen.type) {
		printf("%s %u ", r->match.aslen.type == ASLEN_MAX ?
		    "max-as-len" : "max-as-seq", r->match.aslen.aslen);
	}

	for (i = 0; i < MAX_COMM_MATCH; i++) {
		struct community *c = &r->match.community[i];
		if (c->flags != 0) {
			printf("%s ", community_type(c));
			print_community(c);
		}
	}

	if (r->match.maxcomm != 0)
		printf("max-communities %d ", r->match.maxcomm - 1);
	if (r->match.maxextcomm != 0)
		printf("max-ext-communities %d ", r->match.maxextcomm - 1);
	if (r->match.maxlargecomm != 0)
		printf("max-large-communities %d ", r->match.maxlargecomm - 1);

	print_set(&r->set);

	printf("\n");
}

const char *
mrt_type(enum mrt_type t)
{
	switch (t) {
	case MRT_NONE:
		break;
	case MRT_TABLE_DUMP:
		return "table";
	case MRT_TABLE_DUMP_MP:
		return "table-mp";
	case MRT_TABLE_DUMP_V2:
		return "table-v2";
	case MRT_ALL_IN:
		return "all in";
	case MRT_ALL_OUT:
		return "all out";
	case MRT_UPDATE_IN:
		return "updates in";
	case MRT_UPDATE_OUT:
		return "updates out";
	}
	return "unfluffy MRT";
}

void
print_mrt(struct bgpd_config *conf, uint32_t pid, uint32_t gid,
    const char *prep, const char *prep2)
{
	struct mrt	*m;

	if (conf->mrt == NULL)
		return;

	LIST_FOREACH(m, conf->mrt, entry)
		if ((gid != 0 && m->group_id == gid) ||
		    (m->peer_id == pid && m->group_id == gid)) {
			printf("%s%sdump ", prep, prep2);
			if (m->rib[0])
				printf("rib %s ", m->rib);
			printf("%s \"%s\"", mrt_type(m->type),
			    MRT2MC(m)->name);
			if (MRT2MC(m)->ReopenTimerInterval == 0)
				printf("\n");
			else
				printf(" %d\n", MRT2MC(m)->ReopenTimerInterval);
		}
	if (!LIST_EMPTY(conf->mrt))
		printf("\n");
}

void
print_groups(struct bgpd_config *conf)
{
	struct peer	**peerlist;
	struct peer	 *p;
	u_int		  peer_cnt, i;
	uint32_t	  prev_groupid;
	const char	 *c;

	peer_cnt = 0;
	RB_FOREACH(p, peer_head, &conf->peers)
		peer_cnt++;
	if ((peerlist = calloc(peer_cnt, sizeof(*peerlist))) == NULL)
		fatal("print_groups calloc");
	i = 0;
	RB_FOREACH(p, peer_head, &conf->peers)
		peerlist[i++] = p;

	qsort(peerlist, peer_cnt, sizeof(*peerlist), peer_compare);

	prev_groupid = 0;
	for (i = 0; i < peer_cnt; i++) {
		if (peerlist[i]->conf.groupid) {
			c = "\t";
			if (peerlist[i]->conf.groupid != prev_groupid) {
				if (prev_groupid)
					printf("}\n\n");
				printf("group \"%s\" {\n",
				    peerlist[i]->conf.group);
				prev_groupid = peerlist[i]->conf.groupid;
			}
		} else
			c = "";

		print_peer(peerlist[i], conf, c);
	}

	if (prev_groupid)
		printf("}\n\n");

	free(peerlist);
}

int
peer_compare(const void *aa, const void *bb)
{
	const struct peer * const *a;
	const struct peer * const *b;

	a = aa;
	b = bb;

	return ((*a)->conf.groupid - (*b)->conf.groupid);
}

void
print_config(struct bgpd_config *conf, struct rib_names *rib_l)
{
	struct filter_rule	*r;
	struct network		*n;
	struct flowspec_config	*f;
	struct rde_rib		*rr;
	struct l3vpn		*vpn;

	print_mainconf(conf);
	print_rtrs(&conf->rtrs);
	print_roa(&conf->roa);
	print_aspa(&conf->aspa);
	print_as_sets(&conf->as_sets);
	print_prefixsets(&conf->prefixsets);
	print_originsets(&conf->originsets);
	TAILQ_FOREACH(n, &conf->networks, entry)
		print_network(&n->net, "");
	RB_FOREACH(f, flowspec_tree, &conf->flowspecs)
		print_flowspec(f, "");
	if (!SIMPLEQ_EMPTY(&conf->l3vpns))
		printf("\n");
	SIMPLEQ_FOREACH(vpn, &conf->l3vpns, entry)
		print_l3vpn(vpn);
	printf("\n");
	if (conf->filtered_in_locrib)
		printf("rde rib Loc-RIB include filtered\n");
	SIMPLEQ_FOREACH(rr, rib_l, entry) {
		if (rr->flags & F_RIB_NOEVALUATE)
			printf("rde rib %s no evaluate\n", rr->name);
		else if (rr->flags & F_RIB_NOFIB)
			printf("rde rib %s\n", rr->name);
		else
			printf("rde rib %s rtable %u fib-update %s\n", rr->name,
			    rr->rtableid, rr->flags & F_RIB_NOFIBSYNC ?
			    "no" : "yes");
	}
	printf("\n");
	print_mrt(conf, 0, 0, "", "");
	print_groups(conf);
	TAILQ_FOREACH(r, conf->filters, entry)
		print_rule(conf, r);
}
