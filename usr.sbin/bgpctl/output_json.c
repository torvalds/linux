/*	$OpenBSD: output_json.c,v 1.52 2025/03/10 14:08:25 claudio Exp $ */

/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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
#include <arpa/inet.h>

#include <endian.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bgpd.h"
#include "session.h"
#include "rde.h"

#include "bgpctl.h"
#include "parser.h"
#include "json.h"

static void
json_head(struct parse_result *res)
{
	json_do_start(stdout);
}

static void
json_neighbor_capabilities(struct capabilities *capa)
{
	int hascapamp = 0, hascapaap = 0;
	uint8_t i;

	for (i = AID_MIN; i < AID_MAX; i++) {
		if (capa->mp[i])
			hascapamp = 1;
		if (capa->add_path[i])
			hascapaap = 1;
	}
	if (!hascapamp && !hascapaap && !capa->grestart.restart &&
	    !capa->refresh && !capa->enhanced_rr && !capa->as4byte)
		return;

	json_do_object("capabilities", 0);
	json_do_bool("as4byte", capa->as4byte);
	json_do_bool("refresh", capa->refresh);
	json_do_bool("enhanced_refresh", capa->enhanced_rr);
	json_do_bool("extended_message", capa->ext_msg);

	if (hascapamp) {
		json_do_array("multiprotocol");
		for (i = AID_MIN; i < AID_MAX; i++)
			if (capa->mp[i])
				json_do_string("mp", aid2str(i));
		json_do_end();
	}
	if (capa->grestart.restart) {
		int restarted = 0, present = 0;

		for (i = AID_MIN; i < AID_MAX; i++)
			if (capa->grestart.flags[i] & CAPA_GR_PRESENT) {
				present = 1;
				if (capa->grestart.flags[i] & CAPA_GR_RESTART)
					restarted = 1;
				break;
			}
		json_do_object("graceful_restart", 0);
		json_do_bool("eor", 1);
		json_do_bool("restart", restarted);

		if (capa->grestart.timeout)
			json_do_uint("timeout", capa->grestart.timeout);
		if (capa->grestart.grnotification)
			json_do_bool("graceful_notification", 1);

		if (present) {
			json_do_array("protocols");
			for (i = AID_MIN; i < AID_MAX; i++)
				if (capa->grestart.flags[i] & CAPA_GR_PRESENT) {
					json_do_object("family", 1);
					json_do_string("family", aid2str(i));
					json_do_bool("preserved",
					    capa->grestart.flags[i] &
					    CAPA_GR_FORWARD);
					json_do_end();
				}
			json_do_end();
		}

		json_do_end();
	}
	if (hascapaap) {
		json_do_array("add-path");
		for (i = AID_MIN; i < AID_MAX; i++)
			if (capa->add_path[i]) {
				json_do_object("add-path-elm", 1);
				json_do_string("family", aid2str(i));
				switch (capa->add_path[i]) {
				case CAPA_AP_RECV:
					json_do_string("mode", "recv");
					break;
				case CAPA_AP_SEND:
					json_do_string("mode", "send");
					break;
				case CAPA_AP_BIDIR:
					json_do_string("mode", "bidir");
					break;
				default:
					json_do_printf("mode", "unknown %d",
					    capa->add_path[i]);
					break;
				}
				json_do_end();
			}
		json_do_end();
	}

	if (capa->policy) {
		json_do_string("open_policy",
		    capa->policy == 2 ? "enforce" : "present");
	}

	json_do_end();
}

static void
json_neighbor_stats(struct peer *p)
{
	json_do_object("stats", 0);
	json_do_string("last_read", fmt_monotime(p->stats.last_read));
	json_do_int("last_read_sec", get_rel_monotime(p->stats.last_read));
	json_do_string("last_write", fmt_monotime(p->stats.last_write));
	json_do_int("last_write_sec", get_rel_monotime(p->stats.last_write));

	json_do_object("prefixes", 1);
	json_do_uint("sent", p->stats.prefix_out_cnt);
	json_do_uint("received", p->stats.prefix_cnt);
	json_do_end();

	json_do_object("message", 0);

	json_do_object("sent", 0);
	json_do_uint("open", p->stats.msg_sent_open);
	json_do_uint("notifications", p->stats.msg_sent_notification);
	json_do_uint("updates", p->stats.msg_sent_update);
	json_do_uint("keepalives", p->stats.msg_sent_keepalive);
	json_do_uint("route_refresh", p->stats.msg_sent_rrefresh);
	json_do_uint("total",
	    p->stats.msg_sent_open + p->stats.msg_sent_notification +
	    p->stats.msg_sent_update + p->stats.msg_sent_keepalive +
	    p->stats.msg_sent_rrefresh);
	json_do_end();

	json_do_object("received", 0);
	json_do_uint("open", p->stats.msg_rcvd_open);
	json_do_uint("notifications", p->stats.msg_rcvd_notification);
	json_do_uint("updates", p->stats.msg_rcvd_update);
	json_do_uint("keepalives", p->stats.msg_rcvd_keepalive);
	json_do_uint("route_refresh", p->stats.msg_rcvd_rrefresh);
	json_do_uint("total",
	    p->stats.msg_rcvd_open + p->stats.msg_rcvd_notification +
	    p->stats.msg_rcvd_update + p->stats.msg_rcvd_keepalive +
	    p->stats.msg_rcvd_rrefresh);
	json_do_end();

	json_do_end();

	json_do_object("update", 0);

	json_do_object("sent", 1);
	json_do_uint("updates", p->stats.prefix_sent_update);
	json_do_uint("withdraws", p->stats.prefix_sent_withdraw);
	json_do_uint("eor", p->stats.prefix_sent_eor);
	json_do_end();

	json_do_object("received", 1);
	json_do_uint("updates", p->stats.prefix_rcvd_update);
	json_do_uint("withdraws", p->stats.prefix_rcvd_withdraw);
	json_do_uint("eor", p->stats.prefix_rcvd_eor);
	json_do_end();

	json_do_object("pending", 1);
	json_do_uint("updates", p->stats.pending_update);
	json_do_uint("withdraws", p->stats.pending_withdraw);
	json_do_end();

	json_do_end();

	json_do_object("route-refresh", 0);

	json_do_object("sent", 1);
	json_do_uint("request", p->stats.refresh_sent_req);
	json_do_uint("borr", p->stats.refresh_sent_borr);
	json_do_uint("eorr", p->stats.refresh_sent_eorr);
	json_do_end();

	json_do_object("received", 1);
	json_do_uint("request", p->stats.refresh_rcvd_req);
	json_do_uint("borr", p->stats.refresh_rcvd_borr);
	json_do_uint("eorr", p->stats.refresh_rcvd_eorr);
	json_do_end();

	json_do_end();

	json_do_end();
}

static void
json_neighbor_full(struct peer *p)
{
	const char *errstr;

	/* config */
	json_do_object("config", 0);
	json_do_bool("template", p->conf.template);
	json_do_bool("cloned", p->template != NULL);
	json_do_bool("passive", p->conf.passive);
	json_do_bool("down", p->conf.down);
	json_do_bool("multihop", p->conf.ebgp && p->conf.distance > 1);
	if (p->conf.ebgp && p->conf.distance > 1)
		json_do_uint("multihop_distance", p->conf.distance);
	if (p->conf.max_prefix) {
		json_do_uint("max_prefix", p->conf.max_prefix);
		if (p->conf.max_prefix_restart)
			json_do_uint("max_prefix_restart",
			    p->conf.max_prefix_restart);
	}
	if (p->conf.max_out_prefix) {
		json_do_uint("max_out_prefix", p->conf.max_out_prefix);
		if (p->conf.max_out_prefix_restart)
			json_do_uint("max_out_prefix_restart",
			    p->conf.max_out_prefix_restart);
	}
	if (p->auth_conf.method != AUTH_NONE)
		json_do_string("authentication",
		    fmt_auth_method(p->auth_conf.method));
	json_do_bool("ttl_security", p->conf.ttlsec);
	json_do_uint("holdtime", p->conf.holdtime);
	json_do_uint("min_holdtime", p->conf.min_holdtime);
	if (p->conf.ebgp && p->conf.role != ROLE_NONE)
		json_do_string("role", log_policy(p->conf.role));

	/* capabilities */
	json_neighbor_capabilities(&p->conf.capabilities);

	json_do_end();


	/* stats */
	json_neighbor_stats(p);

	/* errors */
	if (p->conf.reason[0])
		json_do_string("my_shutdown_reason",
		    log_reason(p->conf.reason));
	if (p->stats.last_reason[0])
		json_do_string("last_shutdown_reason",
		    log_reason(p->stats.last_reason));
	errstr = fmt_errstr(p->stats.last_sent_errcode,
	    p->stats.last_sent_suberr);
	if (errstr)
		json_do_string("last_error_sent", errstr);
	errstr = fmt_errstr(p->stats.last_rcvd_errcode,
	    p->stats.last_rcvd_suberr);
	if (errstr)
		json_do_string("last_error_received", errstr);

	/* connection info */
	if (p->state >= STATE_OPENSENT) {
		json_do_object("session", 0);
		json_do_uint("holdtime", p->holdtime);
		json_do_uint("keepalive", p->holdtime / 3);

		json_do_object("local", 0);
		json_do_string("address", log_addr(&p->local));
		json_do_uint("port", p->local_port);
		json_neighbor_capabilities(&p->capa.ann);
		json_do_end();

		json_do_object("remote", 0);
		json_do_string("address", log_addr(&p->remote));
		json_do_uint("port", p->remote_port);
		json_neighbor_capabilities(&p->capa.peer);
		json_do_end();

		/* capabilities */
		json_neighbor_capabilities(&p->capa.neg);

		if (p->conf.ebgp && p->conf.role != ROLE_NONE) {
			json_do_string("remote_role",
			    log_policy(p->remote_role));
			json_do_string("local_role",
			    log_policy(p->conf.role));
		}
		json_do_end();
	}
}

static void
json_neighbor(struct peer *p, struct parse_result *res)
{
	json_do_array("neighbors");

	json_do_object("neighbor", 0);

	json_do_string("remote_as", log_as(p->conf.remote_as));
	if (p->conf.descr[0])
		json_do_string("description", p->conf.descr);
	if (p->conf.group[0])
		json_do_string("group", p->conf.group);
	if (!p->conf.template)
		json_do_string("remote_addr", log_addr(&p->conf.remote_addr));
	else
		json_do_printf("remote_addr", "%s/%u",
		    log_addr(&p->conf.remote_addr), p->conf.remote_masklen);
	if (p->state == STATE_ESTABLISHED) {
		struct in_addr ina;
		ina.s_addr = htonl(p->remote_bgpid);
		json_do_string("bgpid", inet_ntoa(ina));
	}
	json_do_string("state", statenames[p->state]);
	json_do_string("last_updown", fmt_monotime(p->stats.last_updown));
	json_do_int("last_updown_sec", get_rel_monotime(p->stats.last_updown));

	switch (res->action) {
	case SHOW:
	case SHOW_SUMMARY:
	case SHOW_SUMMARY_TERSE:
		/* only show basic data */
		break;
	case SHOW_NEIGHBOR:
	case SHOW_NEIGHBOR_TIMERS:
	case SHOW_NEIGHBOR_TERSE:
		json_neighbor_full(p);
		break;
	default:
		break;
	}

	/* keep the object open in case there are timers */
}

static void
json_timer(struct ctl_timer *t)
{
	json_do_array("timers");

	json_do_object("timer", 1);
	json_do_string("name", timernames[t->type]);
	json_do_int("due", -get_rel_monotime(t->val));
	json_do_end();
}

static void
json_fib(struct kroute_full *kf)
{
	const char *origin;

	json_do_array("fib");

	json_do_object("fib_entry", 0);

	json_do_printf("prefix", "%s/%u", log_addr(&kf->prefix), kf->prefixlen);
	json_do_uint("priority", kf->priority);
	if (kf->flags & F_BGPD)
		origin = "bgp";
	else if (kf->flags & F_CONNECTED)
		origin = "connected";
	else if (kf->flags & F_STATIC)
		origin = "static";
	else
		origin = "unknown";
	json_do_string("origin", origin);
	json_do_bool("used_by_nexthop", kf->flags & F_NEXTHOP);
	json_do_bool("blackhole", kf->flags & F_BLACKHOLE);
	json_do_bool("reject", kf->flags & F_REJECT);

	if (kf->flags & F_CONNECTED)
		json_do_printf("nexthop", "link#%u", kf->ifindex);
	else
		json_do_string("nexthop", log_addr(&kf->nexthop));

	if (kf->flags & F_MPLS) {
		json_do_array("mplslabel");
		json_do_uint("mplslabel",
		    ntohl(kf->mplslabel) >> MPLS_LABEL_OFFSET);
		json_do_end();
	}
	json_do_end();
}

static void
json_fib_table(struct ktable *kt)
{
	json_do_array("fibtables");

	json_do_object("fibtable", 0);
	json_do_uint("rtableid", kt->rtableid);
	json_do_string("description", kt->descr);
	json_do_bool("coupled", kt->fib_sync);
	json_do_bool("admin_change", kt->fib_sync != kt->fib_conf);
	json_do_end();
}

static void
json_do_interface(struct ctl_show_interface *iface)
{
	json_do_object("interface", 0);

	json_do_string("name", iface->ifname);
	json_do_uint("rdomain", iface->rdomain);
	json_do_bool("is_up", iface->is_up);
	json_do_bool("nh_reachable", iface->nh_reachable);

	if (iface->media[0])
		json_do_string("media", iface->media);

	json_do_string("linkstate", iface->linkstate);
	if (iface->baudrate > 0)
		json_do_uint("baudrate", iface->baudrate);

	json_do_end();
}

static void
json_nexthop(struct ctl_show_nexthop *nh)
{
	json_do_array("nexthops");

	json_do_object("nexthop", 0);

	json_do_string("address", log_addr(&nh->addr));
	json_do_bool("valid", nh->valid);

	if (!nh->krvalid)
		goto done;

	json_do_printf("prefix", "%s/%u", log_addr(&nh->kr.prefix),
	    nh->kr.prefixlen);
	json_do_uint("priority", nh->kr.priority);
	json_do_bool("connected", nh->kr.flags & F_CONNECTED);
	json_do_string("nexthop", log_addr(&nh->kr.nexthop));
	if (nh->iface.ifname[0])
		json_do_interface(&nh->iface);
done:
	json_do_end();
	/* keep array open */
}

static void
json_interface(struct ctl_show_interface *iface)
{
	json_do_array("interfaces");
	json_do_interface(iface);
}

static void
json_communities(struct ibuf *data, struct parse_result *res)
{
	struct community c;
	uint64_t ext;


	while (ibuf_size(data) != 0) {
		if (ibuf_get(data, &c, sizeof(c)) == -1) {
			warn("communities");
			return;
		}

		switch (c.flags) {
		case COMMUNITY_TYPE_BASIC:
			json_do_array("communities");
			json_do_string("community",
			    fmt_community(c.data1, c.data2));
			break;
		case COMMUNITY_TYPE_LARGE:
			json_do_array("large_communities");
			json_do_string("community",
			    fmt_large_community(c.data1, c.data2, c.data3));
			break;
		case COMMUNITY_TYPE_EXT:
			ext = (uint64_t)c.data3 << 48;
			switch ((c.data3 >> 8) & EXT_COMMUNITY_VALUE) {
			case EXT_COMMUNITY_TRANS_TWO_AS:
			case EXT_COMMUNITY_TRANS_OPAQUE:
			case EXT_COMMUNITY_TRANS_EVPN:
				ext |= ((uint64_t)c.data1 & 0xffff) << 32;
				ext |= (uint64_t)c.data2;
				break;
			case EXT_COMMUNITY_TRANS_FOUR_AS:
			case EXT_COMMUNITY_TRANS_IPV4:
				ext |= (uint64_t)c.data1 << 16;
				ext |= (uint64_t)c.data2 & 0xffff;
				break;
			}

			json_do_array("extended_communities");
			json_do_string("community", fmt_ext_community(ext));
			break;
		}
	}
}

static void
json_do_community(struct ibuf *buf)
{
	uint16_t a, v;

	json_do_array("communities");

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n16(buf, &a) == -1 ||
		    ibuf_get_n16(buf, &v) == -1) {
			json_do_string("error", "bad length");
			return;
		}
		json_do_string("community", fmt_community(a, v));
	}

	json_do_end();
}

static void
json_do_large_community(struct ibuf *buf)
{
	uint32_t a, l1, l2;

	json_do_array("large_communities");

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n32(buf, &a) == -1 ||
		    ibuf_get_n32(buf, &l1) == -1 ||
		    ibuf_get_n32(buf, &l2) == -1) {
			json_do_string("error", "bad length");
			return;
		}
		json_do_string("community", fmt_large_community(a, l1, l2));
	}

	json_do_end();
}

static void
json_do_ext_community(struct ibuf *buf)
{
	uint64_t ext;
	json_do_array("extended_communities");

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n64(buf, &ext) == -1) {
			json_do_string("error", "bad length");
			return;
		}
		json_do_string("community", fmt_ext_community(ext));
	}

	json_do_end();
}

static void
json_attr(struct ibuf *buf, int reqflags, int addpath)
{
	struct bgpd_addr prefix;
	struct in_addr id;
	struct ibuf asbuf, *path = NULL;
	char *aspath;
	uint32_t as, pathid, val;
	uint16_t alen, afi, short_as;
	uint8_t flags, type, safi, aid, prefixlen, origin;
	int e4, e2;

	json_do_array("attributes");
	json_do_object("attribute", 0);

	if (ibuf_get_n8(buf, &flags) == -1 ||
	    ibuf_get_n8(buf, &type) == -1)
		goto bad_len;

	json_do_string("type", fmt_attr(type, -1));
	json_do_object("flags", 1);
	json_do_bool("partial", flags & ATTR_PARTIAL);
	json_do_bool("transitive", flags & ATTR_TRANSITIVE);
	json_do_bool("optional", flags & ATTR_OPTIONAL);
	json_do_end();

	if (flags & ATTR_EXTLEN) {
		uint16_t attr_len;
		if (ibuf_get_n16(buf, &attr_len) == -1)
			goto bad_len;
		alen = attr_len;
	} else {
		uint8_t attr_len;
		if (ibuf_get_n8(buf, &attr_len) == -1)
			goto bad_len;
		alen = attr_len;
	}

	json_do_uint("length", alen);

	/* bad imsg len how can that happen!? */
	if (alen > ibuf_size(buf))
		goto bad_len;

	switch (type) {
	case ATTR_ORIGIN:
		if (alen != 1 || ibuf_get_n8(buf, &origin) == -1)
			goto bad_len;
		json_do_string("origin", fmt_origin(origin, 0));
		break;
	case ATTR_ASPATH:
	case ATTR_AS4_PATH:
		/* prefer 4-byte AS here */
		e4 = aspath_verify(buf, 1, 0);
		e2 = aspath_verify(buf, 0, 0);
		if (e4 == 0 || e4 == AS_ERR_SOFT) {
			ibuf_from_ibuf(&asbuf, buf);
		} else if (e2 == 0 || e2 == AS_ERR_SOFT) {
			if ((path = aspath_inflate(buf)) == NULL) {
				json_do_string("error",
				    "aspath_inflate failed");
				break;
			}
			ibuf_from_ibuf(&asbuf, path);
		} else {
			json_do_string("error", "bad AS-Path");
			break;
		}
		if (aspath_asprint(&aspath, &asbuf) == -1)
			err(1, NULL);
		json_do_string("aspath", aspath);
		free(aspath);
		ibuf_free(path);
		break;
	case ATTR_NEXTHOP:
		if (alen != 4 || ibuf_get(buf, &id, sizeof(id)) == -1)
			goto bad_len;
		json_do_string("nexthop", inet_ntoa(id));
		break;
	case ATTR_MED:
	case ATTR_LOCALPREF:
		if (alen != 4 || ibuf_get_n32(buf, &val) == -1)
			goto bad_len;
		json_do_uint("metric", val);
		break;
	case ATTR_AGGREGATOR:
	case ATTR_AS4_AGGREGATOR:
		if (alen == 8) {
			if (ibuf_get_n32(buf, &as) == -1 ||
			    ibuf_get(buf, &id, sizeof(id)) == -1)
				goto bad_len;
		} else if (alen == 6) {
			if (ibuf_get_n16(buf, &short_as) == -1 ||
			    ibuf_get(buf, &id, sizeof(id)) == -1)
				goto bad_len;
			as = short_as;
		} else {
			goto bad_len;
		}
		json_do_uint("AS", as);
		json_do_string("router_id", inet_ntoa(id));
		break;
	case ATTR_COMMUNITIES:
		json_do_community(buf);
		break;
	case ATTR_ORIGINATOR_ID:
		if (alen != 4 || ibuf_get(buf, &id, sizeof(id)) == -1)
			goto bad_len;
		json_do_string("originator", inet_ntoa(id));
		break;
	case ATTR_CLUSTER_LIST:
		json_do_array("cluster_list");
		while (ibuf_size(buf) > 0) {
			if (ibuf_get(buf, &id, sizeof(id)) == -1)
				goto bad_len;
			json_do_string("cluster_id", inet_ntoa(id));
		}
		json_do_end();
		break;
	case ATTR_MP_REACH_NLRI:
	case ATTR_MP_UNREACH_NLRI:
		if (ibuf_get_n16(buf, &afi) == -1 ||
		    ibuf_get_n8(buf, &safi) == -1)
			goto bad_len;

		if (afi2aid(afi, safi, &aid) == -1) {
			json_do_printf("error", "bad AFI/SAFI pair: %d/%d",
			    afi, safi);
			break;
		}
		json_do_string("family", aid2str(aid));

		if (type == ATTR_MP_REACH_NLRI) {
			struct bgpd_addr nexthop;
			uint8_t nhlen;
			if (ibuf_get_n8(buf, &nhlen) == -1)
				goto bad_len;
			memset(&nexthop, 0, sizeof(nexthop));
			switch (aid) {
			case AID_INET6:
				nexthop.aid = aid;
				if (nhlen != 16 && nhlen != 32)
					goto bad_len;
				if (ibuf_get(buf, &nexthop.v6,
				    sizeof(nexthop.v6)) == -1)
					goto bad_len;
				break;
			case AID_VPN_IPv4:
				if (nhlen != 12)
					goto bad_len;
				nexthop.aid = AID_INET;
				if (ibuf_skip(buf, sizeof(uint64_t)) == -1 ||
				    ibuf_get(buf, &nexthop.v4,
				    sizeof(nexthop.v4)) == -1)
					goto bad_len;
				break;
			case AID_VPN_IPv6:
				if (nhlen != 24)
					goto bad_len;
				nexthop.aid = AID_INET6;
				if (ibuf_skip(buf, sizeof(uint64_t)) == -1 ||
				    ibuf_get(buf, &nexthop.v6,
				    sizeof(nexthop.v6)) == -1)
					goto bad_len;
				break;
			default:
				json_do_printf("error", "unhandled AID: %d",
				    aid);
				return;
			}
			/* ignore reserved (old SNPA) field as per RFC4760 */
			if (ibuf_skip(buf, 1) == -1)
				goto bad_len;

			json_do_string("nexthop", log_addr(&nexthop));
		}

		json_do_array("NLRI");
		while (ibuf_size(buf) > 0) {
			json_do_object("prefix", 1);
			if (addpath)
				if (ibuf_get_n32(buf, &pathid) == -1)
					goto bad_len;
			switch (aid) {
			case AID_INET6:
				if (nlri_get_prefix6(buf, &prefix,
				    &prefixlen) == -1)
					goto bad_len;
				break;
			case AID_VPN_IPv4:
				if (nlri_get_vpn4(buf, &prefix,
				    &prefixlen, 1) == -1)
					goto bad_len;
				break;
			case AID_VPN_IPv6:
				if (nlri_get_vpn6(buf, &prefix,
				    &prefixlen, 1) == -1)
					goto bad_len;
				break;
			default:
				json_do_printf("error", "unhandled AID: %d",
				    aid);
				return;
			}
			json_do_printf("prefix", "%s/%u", log_addr(&prefix),
			    prefixlen);
			if (addpath)
				 json_do_uint("path_id", pathid);
			json_do_end();
		}
		json_do_end();
		break;
	case ATTR_EXT_COMMUNITIES:
		json_do_ext_community(buf);
		break;
	case ATTR_LARGE_COMMUNITIES:
		json_do_large_community(buf);
		break;
	case ATTR_OTC:
		if (alen != 4 || ibuf_get_n32(buf, &as) == -1)
			goto bad_len;
		json_do_uint("as", as);
		break;
	case ATTR_ATOMIC_AGGREGATE:
	default:
		if (alen)
			json_do_hexdump("data", ibuf_data(buf), ibuf_size(buf));
		break;
	}
	return;

 bad_len:
	json_do_string("error", "bad length");
}

static void
json_rib(struct ctl_show_rib *r, struct ibuf *asbuf, struct parse_result *res)
{
	struct in_addr id;
	char *aspath;

	json_do_array("rib");

	json_do_object("rib_entry", 0);

	json_do_printf("prefix", "%s/%u", log_addr(&r->prefix), r->prefixlen);

	if (aspath_asprint(&aspath, asbuf) == -1)
		err(1, NULL);
	json_do_string("aspath", aspath);
	free(aspath);

	json_do_string("exit_nexthop", log_addr(&r->exit_nexthop));
	json_do_string("true_nexthop", log_addr(&r->true_nexthop));

	json_do_object("neighbor", 1);
	if (r->descr[0])
		json_do_string("description", r->descr);
	json_do_string("remote_addr", log_addr(&r->remote_addr));
	id.s_addr = htonl(r->remote_id);
	json_do_string("bgp_id", inet_ntoa(id));
	json_do_end();

	if (r->flags & F_PREF_PATH_ID)
		json_do_uint("path_id", r->path_id);

	/* flags */
	json_do_bool("valid", r->flags & F_PREF_ELIGIBLE);
	if (r->flags & F_PREF_FILTERED)
		json_do_bool("filtered", 1);
	if (r->flags & F_PREF_BEST)
		json_do_bool("best", 1);
	if (r->flags & F_PREF_ECMP)
		json_do_bool("ecmp", 1);
	if (r->flags & F_PREF_AS_WIDE)
		json_do_bool("as-wide", 1);
	if (r->flags & F_PREF_INTERNAL)
		json_do_string("source", "internal");
	else
		json_do_string("source", "external");
	if (r->flags & F_PREF_STALE)
		json_do_bool("stale", 1);
	if (r->flags & F_PREF_ANNOUNCE)
		json_do_bool("announced", 1);

	/* various attribibutes */
	json_do_string("ovs", fmt_ovs(r->roa_validation_state, 0));
	json_do_string("avs", fmt_avs(r->aspa_validation_state, 0));
	json_do_string("origin", fmt_origin(r->origin, 0));
	json_do_uint("metric", r->med);
	json_do_uint("localpref", r->local_pref);
	json_do_uint("weight", r->weight);
	json_do_int("dmetric", r->dmetric);
	json_do_string("last_update", fmt_monotime(r->lastchange));
	json_do_int("last_update_sec", get_rel_monotime(r->lastchange));

	/* keep the object open for communities and attributes */
}

static void
json_rib_mem_element(const char *name, uint64_t count, uint64_t size,
    uint64_t refs)
{
	json_do_object(name, 1);
	if (count != UINT64_MAX)
		json_do_uint("count", count);
	if (size != UINT64_MAX)
		json_do_uint("size", size);
	if (refs != UINT64_MAX)
		json_do_uint("references", refs);
	json_do_end();
}

static void
json_rib_mem(struct rde_memstats *stats)
{
	size_t pts = 0;
	int i;

	json_do_object("memory", 0);
	for (i = 0; i < AID_MAX; i++) {
		if (stats->pt_cnt[i] == 0)
			continue;
		pts += stats->pt_size[i];
		json_rib_mem_element(aid_vals[i].name, stats->pt_cnt[i],
		    stats->pt_size[i], UINT64_MAX);
	}
	json_rib_mem_element("rib", stats->rib_cnt,
	    stats->rib_cnt * sizeof(struct rib_entry), UINT64_MAX);
	json_rib_mem_element("prefix", stats->prefix_cnt,
	    stats->prefix_cnt * sizeof(struct prefix), UINT64_MAX);
	json_rib_mem_element("rde_aspath", stats->path_cnt,
	    stats->path_cnt * sizeof(struct rde_aspath),
	    stats->path_refs);
	json_rib_mem_element("aspath", stats->aspath_cnt,
	    stats->aspath_size, UINT64_MAX);
	json_rib_mem_element("community_entries", stats->comm_cnt,
	    stats->comm_cnt * sizeof(struct rde_community), UINT64_MAX);
	json_rib_mem_element("community", stats->comm_nmemb,
	    stats->comm_size * sizeof(struct community), stats->comm_refs);
	json_rib_mem_element("attributes_entries", stats->attr_cnt,
	    stats->attr_cnt * sizeof(struct attr), stats->attr_refs);
	json_rib_mem_element("attributes", stats->attr_dcnt,
	    stats->attr_data, UINT64_MAX);
	json_rib_mem_element("total", UINT64_MAX,
	    pts + stats->prefix_cnt * sizeof(struct prefix) +
	    stats->rib_cnt * sizeof(struct rib_entry) +
	    stats->path_cnt * sizeof(struct rde_aspath) +
	    stats->aspath_size + stats->attr_cnt * sizeof(struct attr) +
	    stats->attr_data, UINT64_MAX);
	json_do_end();

	json_do_object("sets", 0);
	json_rib_mem_element("as_set", stats->aset_nmemb,
	    stats->aset_size, UINT64_MAX);
	json_rib_mem_element("as_set_tables", stats->aset_cnt, UINT64_MAX,
	    UINT64_MAX);
	json_rib_mem_element("prefix_set", stats->pset_cnt, stats->pset_size,
	    UINT64_MAX);
	json_rib_mem_element("total", UINT64_MAX,
	    stats->aset_size + stats->pset_size, UINT64_MAX);
	json_do_end();
}

static void
json_rib_set(struct ctl_show_set *set)
{
	json_do_array("sets");

	json_do_object("set", 0);
	json_do_string("name", set->name);
	json_do_string("type", fmt_set_type(set));
	json_do_string("last_change", fmt_monotime(set->lastchange));
	json_do_int("last_change_sec", get_rel_monotime(set->lastchange));
	if (set->type == ASNUM_SET || set->type == ASPA_SET) {
		json_do_uint("num_ASnum", set->as_cnt);
	} else {
		json_do_uint("num_IPv4", set->v4_cnt);
		json_do_uint("num_IPv6", set->v6_cnt);
	}
	json_do_end();
}

static void
json_rtr(struct ctl_show_rtr *rtr)
{
	json_do_array("rtrs");

	json_do_object("rtr", 0);
	if (rtr->descr[0])
		json_do_string("descr", rtr->descr);
	json_do_string("remote_addr", log_addr(&rtr->remote_addr));
	json_do_uint("remote_port", rtr->remote_port);
	if (rtr->local_addr.aid != AID_UNSPEC)
		json_do_string("local_addr", log_addr(&rtr->local_addr));
	json_do_string("state", rtr->state);

	if (rtr->session_id != -1) {
		json_do_uint("version", rtr->version);
		json_do_uint("minimal_version", rtr->min_version);
		json_do_uint("session_id", rtr->session_id);
		json_do_uint("serial", rtr->serial);
	}
	json_do_uint("refresh", rtr->refresh);
	json_do_uint("retry", rtr->retry);
	json_do_uint("expire", rtr->expire);

	if (rtr->last_sent_error != NO_ERROR) {
		json_do_string("last_sent_error",
		    log_rtr_error(rtr->last_sent_error));
		if (rtr->last_sent_msg[0])
			json_do_string("last_sent_msg",
			    log_reason(rtr->last_sent_msg));
	}
	if (rtr->last_recv_error != NO_ERROR) {
		json_do_string("last_recv_error",
		    log_rtr_error(rtr->last_recv_error));
		if (rtr->last_recv_msg[0])
			json_do_string("last_recv_msg",
			    log_reason(rtr->last_recv_msg));
	}
}

static void
json_result(u_int rescode)
{
	if (rescode == 0)
		json_do_string("status", "OK");
	else if (rescode >=
	    sizeof(ctl_res_strerror)/sizeof(ctl_res_strerror[0])) {
		json_do_string("status", "FAILED");
		json_do_printf("error", "unknown error %d", rescode);
	} else {
		json_do_string("status", "FAILED");
		json_do_string("error", ctl_res_strerror[rescode]);
	}
}

static void
json_tail(void)
{
	json_do_finish();
}

const struct output json_output = {
	.head = json_head,
	.neighbor = json_neighbor,
	.timer = json_timer,
	.fib = json_fib,
	.fib_table = json_fib_table,
	.nexthop = json_nexthop,
	.interface = json_interface,
	.communities = json_communities,
	.attr = json_attr,
	.rib = json_rib,
	.rib_mem = json_rib_mem,
	.set = json_rib_set,
	.rtr = json_rtr,
	.result = json_result,
	.tail = json_tail,
};
