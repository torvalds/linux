/*	$OpenBSD: session_bgp.c,v 1.5 2025/08/21 15:15:25 claudio Exp $ */

/*
 * Copyright (c) 2004 - 2025 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004, 2005 Henning Brauer <henning@openbsd.org>
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
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

static void		 start_timer_holdtime(struct peer *);
static void		 start_timer_keepalive(struct peer *);
static int		 capa_neg_calc(struct peer *);

static const uint8_t	 marker[MSGSIZE_HEADER_MARKER] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static struct ibuf *
session_newmsg(enum msg_type msgtype, uint16_t len)
{
	struct ibuf		*buf;
	int			 errs = 0;

	if ((buf = ibuf_open(len)) == NULL)
		return (NULL);

	errs += ibuf_add(buf, marker, sizeof(marker));
	errs += ibuf_add_n16(buf, len);
	errs += ibuf_add_n8(buf, msgtype);

	if (errs) {
		ibuf_free(buf);
		return (NULL);
	}

	return (buf);
}

static void
session_sendmsg(struct ibuf *msg, struct peer *p, enum msg_type msgtype)
{
	session_mrt_dump_bgp_msg(p, msg, msgtype, DIR_OUT);

	ibuf_close(p->wbuf, msg);
}

/*
 * Translate between internal roles and the value expected by RFC 9234.
 */
static uint8_t
role2capa(enum role role)
{
	switch (role) {
	case ROLE_CUSTOMER:
		return CAPA_ROLE_CUSTOMER;
	case ROLE_PROVIDER:
		return CAPA_ROLE_PROVIDER;
	case ROLE_RS:
		return CAPA_ROLE_RS;
	case ROLE_RS_CLIENT:
		return CAPA_ROLE_RS_CLIENT;
	case ROLE_PEER:
		return CAPA_ROLE_PEER;
	default:
		fatalx("Unsupported role for role capability");
	}
}

static enum role
capa2role(uint8_t val)
{
	switch (val) {
	case CAPA_ROLE_PROVIDER:
		return ROLE_PROVIDER;
	case CAPA_ROLE_RS:
		return ROLE_RS;
	case CAPA_ROLE_RS_CLIENT:
		return ROLE_RS_CLIENT;
	case CAPA_ROLE_CUSTOMER:
		return ROLE_CUSTOMER;
	case CAPA_ROLE_PEER:
		return ROLE_PEER;
	default:
		return ROLE_NONE;
	}
}

static int
session_capa_add(struct ibuf *opb, uint8_t capa_code, uint8_t capa_len)
{
	int errs = 0;

	errs += ibuf_add_n8(opb, capa_code);
	errs += ibuf_add_n8(opb, capa_len);
	return (errs);
}

static int
session_capa_add_mp(struct ibuf *buf, uint8_t aid)
{
	uint16_t		 afi;
	uint8_t			 safi;
	int			 errs = 0;

	if (aid2afi(aid, &afi, &safi) == -1) {
		log_warn("%s: bad AID", __func__);
		return (-1);
	}

	errs += ibuf_add_n16(buf, afi);
	errs += ibuf_add_zero(buf, 1);
	errs += ibuf_add_n8(buf, safi);

	return (errs);
}

static int
session_capa_add_afi(struct ibuf *b, uint8_t aid, uint8_t flags)
{
	int		errs = 0;
	uint16_t	afi;
	uint8_t		safi;

	if (aid2afi(aid, &afi, &safi)) {
		log_warn("%s: bad AID", __func__);
		return (-1);
	}

	errs += ibuf_add_n16(b, afi);
	errs += ibuf_add_n8(b, safi);
	errs += ibuf_add_n8(b, flags);

	return (errs);
}

static int
session_capa_add_ext_nh(struct ibuf *b, uint8_t aid)
{
	int		errs = 0;
	uint16_t	afi;
	uint8_t		safi;

	if (aid2afi(aid, &afi, &safi)) {
		log_warn("%s: bad AID", __func__);
		return (-1);
	}

	errs += ibuf_add_n16(b, afi);
	errs += ibuf_add_n16(b, safi);
	errs += ibuf_add_n16(b, AFI_IPv6);

	return (errs);
}

void
session_open(struct peer *p)
{
	struct ibuf		*buf, *opb;
	size_t			 len, optparamlen;
	uint8_t			 i;
	int			 errs = 0, extlen = 0;
	int			 mpcapa = 0;


	if ((opb = ibuf_dynamic(0, MAX_PKTSIZE - MSGSIZE_OPEN_MIN - 6)) ==
	    NULL) {
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	/* multiprotocol extensions, RFC 4760 */
	for (i = AID_MIN; i < AID_MAX; i++)
		if (p->capa.ann.mp[i]) {	/* 4 bytes data */
			errs += session_capa_add(opb, CAPA_MP, 4);
			errs += session_capa_add_mp(opb, i);
			mpcapa++;
		}

	/* route refresh, RFC 2918 */
	if (p->capa.ann.refresh)	/* no data */
		errs += session_capa_add(opb, CAPA_REFRESH, 0);

	/* extended nexthop encoding, RFC 8950 */
	if (p->capa.ann.ext_nh[AID_INET]) {
		uint8_t enhlen = 0;

		if (p->capa.ann.mp[AID_INET])
			enhlen += 6;
		if (p->capa.ann.mp[AID_VPN_IPv4])
			enhlen += 6;
		errs += session_capa_add(opb, CAPA_EXT_NEXTHOP, enhlen);
		if (p->capa.ann.mp[AID_INET])
			errs += session_capa_add_ext_nh(opb, AID_INET);
		if (p->capa.ann.mp[AID_VPN_IPv4])
			errs += session_capa_add_ext_nh(opb, AID_VPN_IPv4);
	}

	/* extended message support, RFC 8654 */
	if (p->capa.ann.ext_msg)	/* no data */
		errs += session_capa_add(opb, CAPA_EXT_MSG, 0);

	/* BGP open policy, RFC 9234, only for ebgp sessions */
	if (p->conf.ebgp && p->capa.ann.policy &&
	    p->conf.role != ROLE_NONE &&
	    (p->capa.ann.mp[AID_INET] || p->capa.ann.mp[AID_INET6] ||
	    mpcapa == 0)) {
		errs += session_capa_add(opb, CAPA_ROLE, 1);
		errs += ibuf_add_n8(opb, role2capa(p->conf.role));
	}

	/* graceful restart and End-of-RIB marker, RFC 4724 */
	if (p->capa.ann.grestart.restart) {
		int		rst = 0;
		uint16_t	hdr = 0;

		for (i = AID_MIN; i < AID_MAX; i++) {
			if (p->capa.neg.grestart.flags[i] & CAPA_GR_RESTARTING)
				rst++;
		}

		/* Only set the R-flag if no graceful restart is ongoing */
		if (!rst)
			hdr |= CAPA_GR_R_FLAG;
		if (p->capa.ann.grestart.grnotification)
			hdr |= CAPA_GR_N_FLAG;
		errs += session_capa_add(opb, CAPA_RESTART, sizeof(hdr));
		errs += ibuf_add_n16(opb, hdr);
	}

	/* 4-bytes AS numbers, RFC6793 */
	if (p->capa.ann.as4byte) {	/* 4 bytes data */
		errs += session_capa_add(opb, CAPA_AS4BYTE, sizeof(uint32_t));
		errs += ibuf_add_n32(opb, p->conf.local_as);
	}

	/* advertisement of multiple paths, RFC7911 */
	if (p->capa.ann.add_path[AID_MIN]) {	/* variable */
		uint8_t	aplen;

		if (mpcapa)
			aplen = 4 * mpcapa;
		else	/* AID_INET */
			aplen = 4;
		errs += session_capa_add(opb, CAPA_ADD_PATH, aplen);
		if (mpcapa) {
			for (i = AID_MIN; i < AID_MAX; i++) {
				if (p->capa.ann.mp[i]) {
					errs += session_capa_add_afi(opb,
					    i, p->capa.ann.add_path[i] &
					    CAPA_AP_MASK);
				}
			}
		} else {	/* AID_INET */
			errs += session_capa_add_afi(opb, AID_INET,
			    p->capa.ann.add_path[AID_INET] & CAPA_AP_MASK);
		}
	}

	/* enhanced route-refresh, RFC7313 */
	if (p->capa.ann.enhanced_rr)	/* no data */
		errs += session_capa_add(opb, CAPA_ENHANCED_RR, 0);

	if (errs) {
		ibuf_free(opb);
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	optparamlen = ibuf_size(opb);
	len = MSGSIZE_OPEN_MIN + optparamlen;
	if (optparamlen == 0) {
		/* nothing */
	} else if (optparamlen + 2 >= 255) {
		/* RFC9072: use 255 as magic size and request extra header */
		optparamlen = 255;
		extlen = 1;
		/* 3 byte OPT_PARAM_EXT_LEN and OPT_PARAM_CAPABILITIES */
		len += 2 * 3;
	} else {
		/* regular capabilities header */
		optparamlen += 2;
		len += 2;
	}

	if ((buf = session_newmsg(BGP_OPEN, len)) == NULL) {
		ibuf_free(opb);
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	errs += ibuf_add_n8(buf, 4);
	errs += ibuf_add_n16(buf, p->conf.local_short_as);
	errs += ibuf_add_n16(buf, p->conf.holdtime);
	/* is already in network byte order */
	errs += ibuf_add_n32(buf, p->local_bgpid);
	errs += ibuf_add_n8(buf, optparamlen);

	if (extlen) {
		/* RFC9072 extra header which spans over the capabilities hdr */
		errs += ibuf_add_n8(buf, OPT_PARAM_EXT_LEN);
		errs += ibuf_add_n16(buf, ibuf_size(opb) + 1 + 2);
	}

	if (optparamlen) {
		errs += ibuf_add_n8(buf, OPT_PARAM_CAPABILITIES);

		if (extlen) {
			/* RFC9072: 2-byte extended length */
			errs += ibuf_add_n16(buf, ibuf_size(opb));
		} else {
			errs += ibuf_add_n8(buf, ibuf_size(opb));
		}
		errs += ibuf_add_ibuf(buf, opb);
	}

	ibuf_free(opb);

	if (errs) {
		ibuf_free(buf);
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	session_sendmsg(buf, p, BGP_OPEN);
	p->stats.msg_sent_open++;
}

void
session_keepalive(struct peer *p)
{
	struct ibuf		*buf;

	if ((buf = session_newmsg(BGP_KEEPALIVE, MSGSIZE_KEEPALIVE)) == NULL) {
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	session_sendmsg(buf, p, BGP_KEEPALIVE);
	start_timer_keepalive(p);
	p->stats.msg_sent_keepalive++;
}

void
session_update(struct peer *p, struct ibuf *ibuf)
{
	struct ibuf	*buf;
	size_t		 len, maxsize = MAX_PKTSIZE;

	if (p->state != STATE_ESTABLISHED)
		return;

	if (p->capa.neg.ext_msg)
		maxsize = MAX_EXT_PKTSIZE;
	len = ibuf_size(ibuf);
	if (len < MSGSIZE_UPDATE_MIN - MSGSIZE_HEADER ||
	    len > maxsize - MSGSIZE_HEADER) {
		log_peer_warnx(&p->conf, "bad UPDATE from RDE");
		return;
	}

	if ((buf = session_newmsg(BGP_UPDATE, MSGSIZE_HEADER + len)) == NULL) {
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	if (ibuf_add_ibuf(buf, ibuf)) {
		ibuf_free(buf);
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	session_sendmsg(buf, p, BGP_UPDATE);
	start_timer_keepalive(p);
	p->stats.msg_sent_update++;
}

/* Return 1 if a hard reset should be issued, 0 for a graceful notification */
static int
session_req_hard_reset(enum err_codes errcode, uint8_t subcode)
{
	switch (errcode) {
	case ERR_HEADER:
	case ERR_OPEN:
	case ERR_UPDATE:
	case ERR_FSM:
	case ERR_RREFRESH:
		/*
		 * Protocol errors trigger a hard reset. The peer
		 * is not trustworthy and so there is no realistic
		 * hope that forwarding can continue.
		 */
		break;
	case ERR_HOLDTIMEREXPIRED:
	case ERR_SENDHOLDTIMEREXPIRED:
		/* Keep forwarding and hope the other side is back soon. */
		return 0;
	case ERR_CEASE:
		switch (subcode) {
		case ERR_CEASE_CONN_REJECT:
		case ERR_CEASE_OTHER_CHANGE:
		case ERR_CEASE_COLLISION:
		case ERR_CEASE_RSRC_EXHAUST:
			/* Per RFC8538 suggestion make these graceful. */
			return 0;
		}
		break;
	}
	return 1;
}

void
session_notification_data(struct peer *p, uint8_t errcode, uint8_t subcode,
    void *data, size_t datalen)
{
	struct ibuf ibuf;

	ibuf_from_buffer(&ibuf, data, datalen);
	session_notification(p, errcode, subcode, &ibuf);
}

void
session_notification(struct peer *p, uint8_t errcode, uint8_t subcode,
    struct ibuf *ibuf)
{
	struct ibuf		*buf;
	const char		*reason = "sending";
	int			 errs = 0, need_hard_reset = 0;
	size_t			 datalen = 0;

	switch (p->state) {
	case STATE_OPENSENT:
	case STATE_OPENCONFIRM:
	case STATE_ESTABLISHED:
		break;
	default:
		/* session not open, no need to send notification */
		log_notification(p, errcode, subcode, ibuf, "dropping");
		return;
	}

	if (p->capa.neg.grestart.grnotification) {
		if (session_req_hard_reset(errcode, subcode)) {
			need_hard_reset = 1;
			datalen += 2;
			reason = "sending hard-reset";
		} else {
			reason = "sending graceful";
		}
	}

	log_notification(p, errcode, subcode, ibuf, reason);

	/* cap to maximum size */
	if (ibuf != NULL) {
		if (ibuf_size(ibuf) >
		    MAX_PKTSIZE - MSGSIZE_NOTIFICATION_MIN - datalen) {
			log_peer_warnx(&p->conf,
			    "oversized notification, data trunkated");
			ibuf_truncate(ibuf, MAX_PKTSIZE -
			    MSGSIZE_NOTIFICATION_MIN - datalen);
		}
		datalen += ibuf_size(ibuf);
	}

	if ((buf = session_newmsg(BGP_NOTIFICATION,
	    MSGSIZE_NOTIFICATION_MIN + datalen)) == NULL) {
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	if (need_hard_reset) {
		errs += ibuf_add_n8(buf, ERR_CEASE);
		errs += ibuf_add_n8(buf, ERR_CEASE_HARD_RESET);
	}

	errs += ibuf_add_n8(buf, errcode);
	errs += ibuf_add_n8(buf, subcode);

	if (ibuf != NULL)
		errs += ibuf_add_ibuf(buf, ibuf);

	if (errs) {
		ibuf_free(buf);
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	session_sendmsg(buf, p, BGP_NOTIFICATION);
	p->stats.msg_sent_notification++;
	p->stats.last_sent_errcode = errcode;
	p->stats.last_sent_suberr = subcode;
}

int
session_neighbor_rrefresh(struct peer *p)
{
	uint8_t	i;

	if (!(p->capa.neg.refresh || p->capa.neg.enhanced_rr))
		return (-1);

	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.neg.mp[i] != 0)
			session_rrefresh(p, i, ROUTE_REFRESH_REQUEST);
	}

	return (0);
}

void
session_rrefresh(struct peer *p, uint8_t aid, uint8_t subtype)
{
	struct ibuf		*buf;
	int			 errs = 0;
	uint16_t		 afi;
	uint8_t			 safi;

	switch (subtype) {
	case ROUTE_REFRESH_REQUEST:
		p->stats.refresh_sent_req++;
		break;
	case ROUTE_REFRESH_BEGIN_RR:
	case ROUTE_REFRESH_END_RR:
		/* requires enhanced route refresh */
		if (!p->capa.neg.enhanced_rr)
			return;
		if (subtype == ROUTE_REFRESH_BEGIN_RR)
			p->stats.refresh_sent_borr++;
		else
			p->stats.refresh_sent_eorr++;
		break;
	default:
		fatalx("session_rrefresh: bad subtype %d", subtype);
	}

	if (aid2afi(aid, &afi, &safi) == -1)
		fatalx("session_rrefresh: bad afi/safi pair");

	if ((buf = session_newmsg(BGP_RREFRESH, MSGSIZE_RREFRESH)) == NULL) {
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	errs += ibuf_add_n16(buf, afi);
	errs += ibuf_add_n8(buf, subtype);
	errs += ibuf_add_n8(buf, safi);

	if (errs) {
		ibuf_free(buf);
		bgp_fsm(p, EVNT_CON_FATAL, NULL);
		return;
	}

	session_sendmsg(buf, p, BGP_RREFRESH);
	p->stats.msg_sent_rrefresh++;
}

struct ibuf *
parse_header(struct ibuf *msg, void *arg, int *fd)
{
	struct peer		*peer = arg;
	struct ibuf		*b;
	u_char			 m[MSGSIZE_HEADER_MARKER];
	uint16_t		 len, maxlen = MAX_PKTSIZE;
	uint8_t			 type;

	if (ibuf_get(msg, m, sizeof(m)) == -1 ||
	    ibuf_get_n16(msg, &len) == -1 ||
	    ibuf_get_n8(msg, &type) == -1)
		return (NULL);
	/* caller MUST make sure we are getting 19 bytes! */
	if (memcmp(m, marker, sizeof(marker))) {
		log_peer_warnx(&peer->conf, "sync error");
		session_notification(peer, ERR_HEADER, ERR_HDR_SYNC, NULL);
		bgp_fsm(peer, EVNT_CON_FATAL, NULL);
		errno = EINVAL;
		return (NULL);
	}

	if (peer->capa.ann.ext_msg)
		maxlen = MAX_EXT_PKTSIZE;

	if (len < MSGSIZE_HEADER || len > maxlen) {
		log_peer_warnx(&peer->conf,
		    "received message: illegal length: %u byte", len);
		goto badlen;
	}

	switch (type) {
	case BGP_OPEN:
		if (len < MSGSIZE_OPEN_MIN || len > MAX_PKTSIZE) {
			log_peer_warnx(&peer->conf,
			    "received OPEN: illegal len: %u byte", len);
			goto badlen;
		}
		break;
	case BGP_NOTIFICATION:
		if (len < MSGSIZE_NOTIFICATION_MIN) {
			log_peer_warnx(&peer->conf,
			    "received NOTIFICATION: illegal len: %u byte", len);
			goto badlen;
		}
		break;
	case BGP_UPDATE:
		if (len < MSGSIZE_UPDATE_MIN) {
			log_peer_warnx(&peer->conf,
			    "received UPDATE: illegal len: %u byte", len);
			goto badlen;
		}
		break;
	case BGP_KEEPALIVE:
		if (len != MSGSIZE_KEEPALIVE) {
			log_peer_warnx(&peer->conf,
			    "received KEEPALIVE: illegal len: %u byte", len);
			goto badlen;
		}
		break;
	case BGP_RREFRESH:
		if (len < MSGSIZE_RREFRESH_MIN) {
			log_peer_warnx(&peer->conf,
			    "received RREFRESH: illegal len: %u byte", len);
			goto badlen;
		}
		break;
	default:
		log_peer_warnx(&peer->conf,
		    "received msg with unknown type %u", type);
		session_notification_data(peer, ERR_HEADER, ERR_HDR_TYPE,
		    &type, sizeof(type));
		bgp_fsm(peer, EVNT_CON_FATAL, NULL);
		errno = EINVAL;
		return (NULL);
	}

	if ((b = ibuf_open(len)) == NULL)
		return (NULL);
	return (b);

 badlen:
	len = htons(len);
	session_notification_data(peer, ERR_HEADER, ERR_HDR_LEN,
	    &len, sizeof(len));
	bgp_fsm(peer, EVNT_CON_FATAL, NULL);
	errno = ERANGE;
	return (NULL);
}

static int
parse_capabilities(struct peer *peer, struct ibuf *buf, uint32_t *as)
{
	struct ibuf	 capabuf;
	uint16_t	 afi, nhafi, gr_header;
	uint8_t		 capa_code, capa_len;
	uint8_t		 safi, aid, role, flags;

	while (ibuf_size(buf) > 0) {
		if (ibuf_get_n8(buf, &capa_code) == -1 ||
		    ibuf_get_n8(buf, &capa_len) == -1) {
			log_peer_warnx(&peer->conf, "Bad capabilities attr "
			    "length: too short");
			return (-1);
		}
		if (ibuf_get_ibuf(buf, capa_len, &capabuf) == -1) {
			log_peer_warnx(&peer->conf,
			    "Received bad capabilities attr length: "
			    "len %zu smaller than capa_len %u",
			    ibuf_size(buf), capa_len);
			return (-1);
		}

		switch (capa_code) {
		case CAPA_MP:			/* RFC 4760 */
			if (capa_len != 4 ||
			    ibuf_get_n16(&capabuf, &afi) == -1 ||
			    ibuf_skip(&capabuf, 1) == -1 ||
			    ibuf_get_n8(&capabuf, &safi) == -1) {
				log_peer_warnx(&peer->conf,
				    "Received bad multi protocol capability");
				break;
			}
			if (afi2aid(afi, safi, &aid) == -1) {
				log_peer_warnx(&peer->conf,
				    "Received multi protocol capability: "
				    " unknown AFI %u, safi %u pair",
				    afi, safi);
				peer->capa.peer.mp[AID_UNSPEC] = 1;
				break;
			}
			peer->capa.peer.mp[aid] = 1;
			break;
		case CAPA_REFRESH:
			peer->capa.peer.refresh = 1;
			break;
		case CAPA_EXT_NEXTHOP:
			while (ibuf_size(&capabuf) > 0) {
				uint16_t tmp16;
				if (ibuf_get_n16(&capabuf, &afi) == -1 ||
				    ibuf_get_n16(&capabuf, &tmp16) == -1 ||
				    ibuf_get_n16(&capabuf, &nhafi) == -1) {
					log_peer_warnx(&peer->conf,
					    "Received bad %s capability",
					    log_capability(CAPA_EXT_NEXTHOP));
					memset(peer->capa.peer.ext_nh, 0,
					    sizeof(peer->capa.peer.ext_nh));
					break;
				}
				safi = tmp16;
				if (afi2aid(afi, safi, &aid) == -1 ||
				    !(aid == AID_INET || aid == AID_VPN_IPv4)) {
					log_peer_warnx(&peer->conf,
					    "Received %s capability: "
					    " unsupported AFI %u, safi %u pair",
					    log_capability(CAPA_EXT_NEXTHOP),
					    afi, safi);
					continue;
				}
				if (nhafi != AFI_IPv6) {
					log_peer_warnx(&peer->conf,
					    "Received %s capability: "
					    " unsupported nexthop AFI %u",
					    log_capability(CAPA_EXT_NEXTHOP),
					    nhafi);
					continue;
				}
				peer->capa.peer.ext_nh[aid] = 1;
			}
			break;
		case CAPA_EXT_MSG:
			peer->capa.peer.ext_msg = 1;
			break;
		case CAPA_ROLE:
			if (capa_len != 1 ||
			    ibuf_get_n8(&capabuf, &role) == -1) {
				log_peer_warnx(&peer->conf,
				    "Received bad role capability");
				break;
			}
			if (!peer->conf.ebgp) {
				log_peer_warnx(&peer->conf,
				    "Received role capability on iBGP session");
				break;
			}
			peer->capa.peer.policy = 1;
			peer->remote_role = capa2role(role);
			break;
		case CAPA_RESTART:
			if (capa_len == 2) {
				/* peer only supports EoR marker */
				peer->capa.peer.grestart.restart = 1;
				peer->capa.peer.grestart.timeout = 0;
				break;
			} else if (capa_len % 4 != 2) {
				log_peer_warnx(&peer->conf,
				    "Bad graceful restart capability");
				peer->capa.peer.grestart.restart = 0;
				peer->capa.peer.grestart.timeout = 0;
				break;
			}

			if (ibuf_get_n16(&capabuf, &gr_header) == -1) {
 bad_gr_restart:
				log_peer_warnx(&peer->conf,
				    "Bad graceful restart capability");
				peer->capa.peer.grestart.restart = 0;
				peer->capa.peer.grestart.timeout = 0;
				break;
			}

			peer->capa.peer.grestart.timeout =
			    gr_header & CAPA_GR_TIMEMASK;
			if (peer->capa.peer.grestart.timeout == 0) {
				log_peer_warnx(&peer->conf, "Received "
				    "graceful restart with zero timeout");
				peer->capa.peer.grestart.restart = 0;
				break;
			}

			while (ibuf_size(&capabuf) > 0) {
				if (ibuf_get_n16(&capabuf, &afi) == -1 ||
				    ibuf_get_n8(&capabuf, &safi) == -1 ||
				    ibuf_get_n8(&capabuf, &flags) == -1)
					goto bad_gr_restart;
				if (afi2aid(afi, safi, &aid) == -1) {
					log_peer_warnx(&peer->conf,
					    "Received graceful restart capa: "
					    " unknown AFI %u, safi %u pair",
					    afi, safi);
					continue;
				}
				peer->capa.peer.grestart.flags[aid] |=
				    CAPA_GR_PRESENT;
				if (flags & CAPA_GR_F_FLAG)
					peer->capa.peer.grestart.flags[aid] |=
					    CAPA_GR_FORWARD;
				if (gr_header & CAPA_GR_R_FLAG)
					peer->capa.peer.grestart.flags[aid] |=
					    CAPA_GR_RESTART;
				peer->capa.peer.grestart.restart = 2;
			}
			if (gr_header & CAPA_GR_N_FLAG)
				peer->capa.peer.grestart.grnotification = 1;
			break;
		case CAPA_AS4BYTE:
			if (capa_len != 4 ||
			    ibuf_get_n32(&capabuf, as) == -1) {
				log_peer_warnx(&peer->conf,
				    "Received bad AS4BYTE capability");
				peer->capa.peer.as4byte = 0;
				break;
			}
			if (*as == 0) {
				log_peer_warnx(&peer->conf,
				    "peer requests unacceptable AS %u", *as);
				session_notification(peer, ERR_OPEN,
				    ERR_OPEN_AS, NULL);
				change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
				return (-1);
			}
			peer->capa.peer.as4byte = 1;
			break;
		case CAPA_ADD_PATH:
			if (capa_len % 4 != 0) {
 bad_add_path:
				log_peer_warnx(&peer->conf,
				    "Received bad ADD-PATH capability");
				memset(peer->capa.peer.add_path, 0,
				    sizeof(peer->capa.peer.add_path));
				break;
			}
			while (ibuf_size(&capabuf) > 0) {
				if (ibuf_get_n16(&capabuf, &afi) == -1 ||
				    ibuf_get_n8(&capabuf, &safi) == -1 ||
				    ibuf_get_n8(&capabuf, &flags) == -1)
					goto bad_add_path;
				if (afi2aid(afi, safi, &aid) == -1) {
					log_peer_warnx(&peer->conf,
					    "Received ADD-PATH capa: "
					    " unknown AFI %u, safi %u pair",
					    afi, safi);
					memset(peer->capa.peer.add_path, 0,
					    sizeof(peer->capa.peer.add_path));
					break;
				}
				if (flags & ~CAPA_AP_BIDIR) {
					log_peer_warnx(&peer->conf,
					    "Received ADD-PATH capa: "
					    " bad flags %x", flags);
					memset(peer->capa.peer.add_path, 0,
					    sizeof(peer->capa.peer.add_path));
					break;
				}
				peer->capa.peer.add_path[aid] = flags;
			}
			break;
		case CAPA_ENHANCED_RR:
			peer->capa.peer.enhanced_rr = 1;
			break;
		default:
			break;
		}
	}

	return (0);
}

static int
parse_open(struct peer *peer, struct ibuf *msg)
{
	uint8_t		 version, rversion;
	uint16_t	 short_as;
	uint16_t	 holdtime;
	uint32_t	 as, bgpid;
	uint8_t		 optparamlen;

	if (ibuf_get_n8(msg, &version) == -1 ||
	    ibuf_get_n16(msg, &short_as) == -1 ||
	    ibuf_get_n16(msg, &holdtime) == -1 ||
	    ibuf_get_n32(msg, &bgpid) == -1 ||
	    ibuf_get_n8(msg, &optparamlen) == -1)
		goto bad_len;

	if (version != BGP_VERSION) {
		log_peer_warnx(&peer->conf,
		    "peer wants unrecognized version %u", version);
		if (version > BGP_VERSION)
			rversion = version - BGP_VERSION;
		else
			rversion = BGP_VERSION;
		session_notification_data(peer, ERR_OPEN, ERR_OPEN_VERSION,
		    &rversion, sizeof(rversion));
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	as = peer->short_as = short_as;
	if (as == 0) {
		log_peer_warnx(&peer->conf,
		    "peer requests unacceptable AS %u", as);
		session_notification(peer, ERR_OPEN, ERR_OPEN_AS, NULL);
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	if (holdtime != 0 && holdtime < peer->conf.min_holdtime) {
		log_peer_warnx(&peer->conf,
		    "peer requests unacceptable holdtime %u", holdtime);
		session_notification(peer, ERR_OPEN, ERR_OPEN_HOLDTIME, NULL);
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	if (holdtime < peer->conf.holdtime)
		peer->holdtime = holdtime;
	else
		peer->holdtime = peer->conf.holdtime;

	/* check bgpid for validity - just disallow 0 */
	if (bgpid == 0) {
		log_peer_warnx(&peer->conf, "peer BGPID 0 unacceptable");
		session_notification(peer, ERR_OPEN, ERR_OPEN_BGPID, NULL);
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}
	peer->remote_bgpid = bgpid;

	if (optparamlen != 0) {
		struct ibuf oparams, op;
		uint8_t ext_type, op_type;
		uint16_t ext_len, op_len;

		ibuf_from_ibuf(&oparams, msg);

		/* check for RFC9072 encoding */
		if (ibuf_get_n8(&oparams, &ext_type) == -1)
			goto bad_len;
		if (ext_type == OPT_PARAM_EXT_LEN) {
			if (ibuf_get_n16(&oparams, &ext_len) == -1)
				goto bad_len;
			/* skip RFC9072 header */
			if (ibuf_skip(msg, 3) == -1)
				goto bad_len;
		} else {
			ext_len = optparamlen;
			ibuf_rewind(&oparams);
		}

		if (ibuf_truncate(&oparams, ext_len) == -1 ||
		    ibuf_skip(msg, ext_len) == -1)
			goto bad_len;

		while (ibuf_size(&oparams) > 0) {
			if (ibuf_get_n8(&oparams, &op_type) == -1)
				goto bad_len;

			if (ext_type == OPT_PARAM_EXT_LEN) {
				if (ibuf_get_n16(&oparams, &op_len) == -1)
					goto bad_len;
			} else {
				uint8_t tmp;
				if (ibuf_get_n8(&oparams, &tmp) == -1)
					goto bad_len;
				op_len = tmp;
			}

			if (ibuf_get_ibuf(&oparams, op_len, &op) == -1)
				goto bad_len;

			switch (op_type) {
			case OPT_PARAM_CAPABILITIES:		/* RFC 3392 */
				if (parse_capabilities(peer, &op, &as) == -1) {
					session_notification(peer, ERR_OPEN, 0,
					    NULL);
					change_state(peer, STATE_IDLE,
					    EVNT_RCVD_OPEN);
					return (-1);
				}
				break;
			case OPT_PARAM_AUTH:			/* deprecated */
			default:
				/*
				 * unsupported type
				 * the RFCs tell us to leave the data section
				 * empty and notify the peer with ERR_OPEN,
				 * ERR_OPEN_OPT. How the peer should know
				 * _which_ optional parameter we don't support
				 * is beyond me.
				 */
				log_peer_warnx(&peer->conf,
				    "received OPEN message with unsupported "
				    "optional parameter: type %u", op_type);
				session_notification(peer, ERR_OPEN,
				    ERR_OPEN_OPT, NULL);
				change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
				return (-1);
			}
		}
	}

	if (ibuf_size(msg) != 0) {
 bad_len:
		log_peer_warnx(&peer->conf,
		    "corrupt OPEN message received: length mismatch");
		session_notification(peer, ERR_OPEN, 0, NULL);
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	/*
	 * if remote-as is zero and it's a cloned neighbor, accept any
	 * but only on the first connect, after that the remote-as needs
	 * to remain the same.
	 */
	if (peer->template && !peer->conf.remote_as && as != AS_TRANS) {
		peer->conf.remote_as = as;
		peer->conf.ebgp = (peer->conf.remote_as != peer->conf.local_as);
		if (!peer->conf.ebgp)
			/* force enforce_as off for iBGP sessions */
			peer->conf.enforce_as = ENFORCE_AS_OFF;
	}

	if (peer->conf.remote_as != as) {
		log_peer_warnx(&peer->conf, "peer sent wrong AS %s",
		    log_as(as));
		session_notification(peer, ERR_OPEN, ERR_OPEN_AS, NULL);
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	/* on iBGP sessions check for bgpid collision */
	if (!peer->conf.ebgp && peer->remote_bgpid == peer->local_bgpid) {
		struct in_addr ina;
		ina.s_addr = htonl(bgpid);
		log_peer_warnx(&peer->conf, "peer BGPID %s conflicts with ours",
		    inet_ntoa(ina));
		session_notification(peer, ERR_OPEN, ERR_OPEN_BGPID, NULL);
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	if (capa_neg_calc(peer) == -1) {
		change_state(peer, STATE_IDLE, EVNT_RCVD_OPEN);
		return (-1);
	}

	return (0);
}

static void
parse_update(struct peer *peer, struct ibuf *msg)
{
	session_handle_update(peer, msg);
}

static void
parse_rrefresh(struct peer *peer, struct ibuf *msg)
{
	struct route_refresh rr;
	uint16_t afi, datalen;
	uint8_t aid, safi, subtype;

	datalen = ibuf_size(msg) + MSGSIZE_HEADER;

	if (ibuf_get_n16(msg, &afi) == -1 ||
	    ibuf_get_n8(msg, &subtype) == -1 ||
	    ibuf_get_n8(msg, &safi) == -1) {
		/* minimum size checked in session_process_msg() */
		fatalx("%s: message too small", __func__);
	}

	/* check subtype if peer announced enhanced route refresh */
	if (peer->capa.neg.enhanced_rr) {
		switch (subtype) {
		case ROUTE_REFRESH_REQUEST:
			/* no ORF support, so no oversized RREFRESH msgs */
			if (datalen != MSGSIZE_RREFRESH) {
				log_peer_warnx(&peer->conf,
				    "received RREFRESH: illegal len: %u byte",
				    datalen);
				datalen = htons(datalen);
				session_notification_data(peer, ERR_HEADER,
				    ERR_HDR_LEN, &datalen, sizeof(datalen));
				bgp_fsm(peer, EVNT_CON_FATAL, NULL);
				return;
			}
			peer->stats.refresh_rcvd_req++;
			break;
		case ROUTE_REFRESH_BEGIN_RR:
		case ROUTE_REFRESH_END_RR:
			/* special handling for RFC7313 */
			if (datalen != MSGSIZE_RREFRESH) {
				log_peer_warnx(&peer->conf,
				    "received RREFRESH: illegal len: %u byte",
				    datalen);
				ibuf_rewind(msg);
				session_notification(peer, ERR_RREFRESH,
				    ERR_RR_INV_LEN, msg);
				bgp_fsm(peer, EVNT_CON_FATAL, NULL);
				return;
			}
			if (subtype == ROUTE_REFRESH_BEGIN_RR)
				peer->stats.refresh_rcvd_borr++;
			else
				peer->stats.refresh_rcvd_eorr++;
			break;
		default:
			log_peer_warnx(&peer->conf, "peer sent bad refresh, "
			    "bad subtype %d", subtype);
			return;
		}
	} else {
		/* force subtype to default */
		subtype = ROUTE_REFRESH_REQUEST;
		peer->stats.refresh_rcvd_req++;
	}

	/* afi/safi unchecked -	unrecognized values will be ignored anyway */
	if (afi2aid(afi, safi, &aid) == -1) {
		log_peer_warnx(&peer->conf, "peer sent bad refresh, "
		    "invalid afi/safi pair");
		return;
	}

	if (!peer->capa.neg.refresh && !peer->capa.neg.enhanced_rr) {
		log_peer_warnx(&peer->conf, "peer sent unexpected refresh");
		return;
	}

	rr.aid = aid;
	rr.subtype = subtype;

	session_handle_rrefresh(peer, &rr);
}

static void
parse_notification(struct peer *peer, struct ibuf *msg)
{
	const char		*reason = "received";
	uint8_t			 errcode, subcode;
	uint8_t			 reason_len;
	enum session_events	 event = EVNT_RCVD_NOTIFICATION;

	if (ibuf_get_n8(msg, &errcode) == -1 ||
	    ibuf_get_n8(msg, &subcode) == -1) {
		log_peer_warnx(&peer->conf, "received bad notification");
		goto done;
	}

	/* RFC8538: check for hard-reset or graceful notification */
	if (peer->capa.neg.grestart.grnotification) {
		if (errcode == ERR_CEASE && subcode == ERR_CEASE_HARD_RESET) {
			if (ibuf_get_n8(msg, &errcode) == -1 ||
			    ibuf_get_n8(msg, &subcode) == -1) {
				log_peer_warnx(&peer->conf,
				    "received bad hard-reset notification");
				goto done;
			}
			reason = "received hard-reset";
		} else {
			reason = "received graceful";
			event = EVNT_RCVD_GRACE_NOTIFICATION;
		}
	}

	peer->errcnt++;
	peer->stats.last_rcvd_errcode = errcode;
	peer->stats.last_rcvd_suberr = subcode;

	log_notification(peer, errcode, subcode, msg, reason);

	CTASSERT(sizeof(peer->stats.last_reason) > UINT8_MAX);
	memset(peer->stats.last_reason, 0, sizeof(peer->stats.last_reason));
	if (errcode == ERR_CEASE &&
	    (subcode == ERR_CEASE_ADMIN_DOWN ||
	     subcode == ERR_CEASE_ADMIN_RESET)) {
		/* check if shutdown reason is included */
		if (ibuf_get_n8(msg, &reason_len) != -1 && reason_len != 0) {
			if (ibuf_get(msg, peer->stats.last_reason,
			    reason_len) == -1)
				log_peer_warnx(&peer->conf,
				    "received truncated shutdown reason");
		}
	}

done:
	change_state(peer, STATE_IDLE, event);
}

void
session_process_msg(struct peer *p)
{
	struct ibuf	*msg;
	int		processed = 0;
	uint8_t		msgtype;

	p->rpending = 0;
	if (p->wbuf == NULL)
		return;

	/*
	 * session might drop to IDLE -> all buffers are flushed
	 */
	while ((msg = msgbuf_get(p->wbuf)) != NULL) {
		/* skip msg header and extract type */
		if (ibuf_skip(msg, MSGSIZE_HEADER_MARKER) == -1 ||
		    ibuf_skip(msg, sizeof(uint16_t)) == -1 ||
		    ibuf_get_n8(msg, &msgtype) == -1) {
			log_peer_warn(&p->conf, "process message failed");
			bgp_fsm(p, EVNT_CON_FATAL, NULL);
			ibuf_free(msg);
			return;
		}
		ibuf_rewind(msg);

		session_mrt_dump_bgp_msg(p, msg, msgtype, DIR_IN);

		ibuf_skip(msg, MSGSIZE_HEADER);

		switch (msgtype) {
		case BGP_OPEN:
			bgp_fsm(p, EVNT_RCVD_OPEN, msg);
			p->stats.msg_rcvd_open++;
			break;
		case BGP_UPDATE:
			bgp_fsm(p, EVNT_RCVD_UPDATE, msg);
			p->stats.msg_rcvd_update++;
			break;
		case BGP_NOTIFICATION:
			bgp_fsm(p, EVNT_RCVD_NOTIFICATION, msg);
			p->stats.msg_rcvd_notification++;
			break;
		case BGP_KEEPALIVE:
			bgp_fsm(p, EVNT_RCVD_KEEPALIVE, msg);
			p->stats.msg_rcvd_keepalive++;
			break;
		case BGP_RREFRESH:
			parse_rrefresh(p, msg);
			p->stats.msg_rcvd_rrefresh++;
			break;
		default:	/* cannot happen */
			session_notification_data(p, ERR_HEADER, ERR_HDR_TYPE,
			    &msgtype, 1);
			log_peer_warnx(&p->conf,
			    "received message with unknown type %u", msgtype);
			bgp_fsm(p, EVNT_CON_FATAL, NULL);
		}
		ibuf_free(msg);
		if (++processed > MSG_PROCESS_LIMIT) {
			p->rpending = 1;
			break;
		}
	}
}

static int
capa_neg_calc(struct peer *p)
{
	struct ibuf *ebuf;
	uint8_t	i, hasmp = 0, capa_code, capa_len, capa_aid = 0;

	/* a capability is accepted only if both sides announced it */

	p->capa.neg.refresh =
	    (p->capa.ann.refresh && p->capa.peer.refresh) != 0;
	p->capa.neg.enhanced_rr =
	    (p->capa.ann.enhanced_rr && p->capa.peer.enhanced_rr) != 0;
	p->capa.neg.as4byte =
	    (p->capa.ann.as4byte && p->capa.peer.as4byte) != 0;
	p->capa.neg.ext_msg =
	    (p->capa.ann.ext_msg && p->capa.peer.ext_msg) != 0;

	/* MP: both side must agree on the AFI,SAFI pair */
	if (p->capa.peer.mp[AID_UNSPEC])
		hasmp = 1;
	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.ann.mp[i] && p->capa.peer.mp[i])
			p->capa.neg.mp[i] = 1;
		else
			p->capa.neg.mp[i] = 0;
		if (p->capa.ann.mp[i] || p->capa.peer.mp[i])
			hasmp = 1;
	}
	/* if no MP capability present default to IPv4 unicast mode */
	if (!hasmp)
		p->capa.neg.mp[AID_INET] = 1;

	/*
	 * graceful restart: the peer capabilities are of interest here.
	 * It is necessary to compare the new values with the previous ones
	 * and act accordingly. AFI/SAFI that are not part in the MP capability
	 * are treated as not being present.
	 * Also make sure that a flush happens if the session stopped
	 * supporting graceful restart.
	 */

	for (i = AID_MIN; i < AID_MAX; i++) {
		int8_t	negflags;

		/* disable GR if the AFI/SAFI is not present */
		if ((p->capa.peer.grestart.flags[i] & CAPA_GR_PRESENT &&
		    p->capa.neg.mp[i] == 0))
			p->capa.peer.grestart.flags[i] = 0;	/* disable */
		/* look at current GR state and decide what to do */
		negflags = p->capa.neg.grestart.flags[i];
		p->capa.neg.grestart.flags[i] = p->capa.peer.grestart.flags[i];
		if (negflags & CAPA_GR_RESTARTING) {
			if (p->capa.ann.grestart.restart != 0 &&
			    p->capa.peer.grestart.flags[i] & CAPA_GR_FORWARD) {
				p->capa.neg.grestart.flags[i] |=
				    CAPA_GR_RESTARTING;
			} else {
				session_graceful_flush(p, i, "not restarted");
			}
		}
	}
	p->capa.neg.grestart.timeout = p->capa.peer.grestart.timeout;
	p->capa.neg.grestart.restart = p->capa.peer.grestart.restart;
	if (p->capa.ann.grestart.restart == 0)
		p->capa.neg.grestart.restart = 0;

	/* RFC 8538 graceful notification: both sides need to agree */
	p->capa.neg.grestart.grnotification =
	    (p->capa.ann.grestart.grnotification &&
	    p->capa.peer.grestart.grnotification) != 0;

	/* RFC 8950 extended nexthop encoding: both sides need to agree */
	memset(p->capa.neg.ext_nh, 0, sizeof(p->capa.neg.ext_nh));
	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.neg.mp[i] == 0)
			continue;
		if (p->capa.ann.ext_nh[i] && p->capa.peer.ext_nh[i]) {
			p->capa.neg.ext_nh[i] = 1;
		}
	}

	/*
	 * ADD-PATH: set only those bits where both sides agree.
	 * For this compare our send bit with the recv bit from the peer
	 * and vice versa.
	 * The flags are stored from this systems view point.
	 * At index 0 the flags are set if any per-AID flag is set.
	 */
	memset(p->capa.neg.add_path, 0, sizeof(p->capa.neg.add_path));
	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.neg.mp[i] == 0)
			continue;
		if ((p->capa.ann.add_path[i] & CAPA_AP_RECV) &&
		    (p->capa.peer.add_path[i] & CAPA_AP_SEND)) {
			p->capa.neg.add_path[i] |= CAPA_AP_RECV;
			p->capa.neg.add_path[0] |= CAPA_AP_RECV;
		}
		if ((p->capa.ann.add_path[i] & CAPA_AP_SEND) &&
		    (p->capa.peer.add_path[i] & CAPA_AP_RECV)) {
			p->capa.neg.add_path[i] |= CAPA_AP_SEND;
			p->capa.neg.add_path[0] |= CAPA_AP_SEND;
		}
	}

	/*
	 * Open policy: check that the policy is sensible.
	 *
	 * Make sure that the roles match and set the negotiated capability
	 * to the role of the peer. So the RDE can inject the OTC attribute.
	 * See RFC 9234, section 4.2.
	 * These checks should only happen on ebgp sessions.
	 */
	if (p->capa.ann.policy != 0 && p->capa.peer.policy != 0 &&
	    p->conf.ebgp) {
		switch (p->conf.role) {
		case ROLE_PROVIDER:
			if (p->remote_role != ROLE_CUSTOMER)
				goto policyfail;
			break;
		case ROLE_RS:
			if (p->remote_role != ROLE_RS_CLIENT)
				goto policyfail;
			break;
		case ROLE_RS_CLIENT:
			if (p->remote_role != ROLE_RS)
				goto policyfail;
			break;
		case ROLE_CUSTOMER:
			if (p->remote_role != ROLE_PROVIDER)
				goto policyfail;
			break;
		case ROLE_PEER:
			if (p->remote_role != ROLE_PEER)
				goto policyfail;
			break;
		default:
 policyfail:
			log_peer_warnx(&p->conf, "open policy role mismatch: "
			    "our role %s, their role %s",
			    log_policy(p->conf.role),
			    log_policy(p->remote_role));
			session_notification(p, ERR_OPEN, ERR_OPEN_ROLE, NULL);
			return (-1);
		}
		p->capa.neg.policy = 1;
	}

	/* enforce presence of open policy role capability */
	if (p->capa.ann.policy == 2 && p->capa.peer.policy == 0 &&
	    p->conf.ebgp) {
		log_peer_warnx(&p->conf, "open policy role enforced but "
		    "not present");
		session_notification(p, ERR_OPEN, ERR_OPEN_ROLE, NULL);
		return (-1);
	}

	/* enforce presence of other capabilities */
	if (p->capa.ann.refresh == 2 && p->capa.neg.refresh == 0) {
		capa_code = CAPA_REFRESH;
		capa_len = 0;
		goto fail;
	}
	/* enforce presence of other capabilities */
	if (p->capa.ann.ext_msg == 2 && p->capa.neg.ext_msg == 0) {
		capa_code = CAPA_EXT_MSG;
		capa_len = 0;
		goto fail;
	}
	if (p->capa.ann.enhanced_rr == 2 && p->capa.neg.enhanced_rr == 0) {
		capa_code = CAPA_ENHANCED_RR;
		capa_len = 0;
		goto fail;
	}
	if (p->capa.ann.as4byte == 2 && p->capa.neg.as4byte == 0) {
		capa_code = CAPA_AS4BYTE;
		capa_len = 4;
		goto fail;
	}
	if (p->capa.ann.grestart.restart == 2 &&
	    p->capa.neg.grestart.restart == 0) {
		capa_code = CAPA_RESTART;
		capa_len = 2;
		goto fail;
	}
	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.ann.mp[i] == 2 && p->capa.neg.mp[i] == 0) {
			capa_code = CAPA_MP;
			capa_len = 4;
			capa_aid = i;
			goto fail;
		}
	}

	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.neg.mp[i] == 0)
			continue;
		if ((p->capa.ann.add_path[i] & CAPA_AP_RECV_ENFORCE) &&
		    (p->capa.neg.add_path[i] & CAPA_AP_RECV) == 0) {
			capa_code = CAPA_ADD_PATH;
			capa_len = 4;
			capa_aid = i;
			goto fail;
		}
		if ((p->capa.ann.add_path[i] & CAPA_AP_SEND_ENFORCE) &&
		    (p->capa.neg.add_path[i] & CAPA_AP_SEND) == 0) {
			capa_code = CAPA_ADD_PATH;
			capa_len = 4;
			capa_aid = i;
			goto fail;
		}
	}

	for (i = AID_MIN; i < AID_MAX; i++) {
		if (p->capa.neg.mp[i] == 0)
			continue;
		if (p->capa.ann.ext_nh[i] == 2 &&
		    p->capa.neg.ext_nh[i] == 0) {
			capa_code = CAPA_EXT_NEXTHOP;
			capa_len = 6;
			capa_aid = i;
			goto fail;
		}
	}
	return (0);

 fail:
	if ((ebuf = ibuf_dynamic(2, 256)) == NULL)
		return (-1);
	/* best effort, no problem if it fails */
	session_capa_add(ebuf, capa_code, capa_len);
	if (capa_code == CAPA_MP)
		session_capa_add_mp(ebuf, capa_aid);
	else if (capa_code == CAPA_ADD_PATH)
		session_capa_add_afi(ebuf, capa_aid, 0);
	else if (capa_code == CAPA_EXT_NEXTHOP)
		session_capa_add_ext_nh(ebuf, capa_aid);
	else if (capa_len > 0)
		ibuf_add_zero(ebuf, capa_len);

	session_notification(p, ERR_OPEN, ERR_OPEN_CAPA, ebuf);
	ibuf_free(ebuf);
	return (-1);
}

static void
session_tcp_established(struct peer *peer)
{
	struct sockaddr_storage	ss;
	socklen_t		len;

	len = sizeof(ss);
	if (getsockname(peer->fd, (struct sockaddr *)&ss, &len) == -1)
		log_warn("getsockname");
	sa2addr((struct sockaddr *)&ss, &peer->local, &peer->local_port);
	len = sizeof(ss);
	if (getpeername(peer->fd, (struct sockaddr *)&ss, &len) == -1)
		log_warn("getpeername");
	sa2addr((struct sockaddr *)&ss, &peer->remote, &peer->remote_port);

	get_alternate_addr(&peer->local, &peer->remote, &peer->local_alt,
	    &peer->if_scope);
}

void
bgp_fsm(struct peer *peer, enum session_events event, struct ibuf *msg)
{
	switch (peer->state) {
	case STATE_NONE:
		/* nothing */
		break;
	case STATE_IDLE:
		switch (event) {
		case EVNT_START:
			timer_stop(&peer->timers, Timer_Hold);
			timer_stop(&peer->timers, Timer_SendHold);
			timer_stop(&peer->timers, Timer_Keepalive);
			timer_stop(&peer->timers, Timer_IdleHold);

			if (!peer->depend_ok)
				timer_stop(&peer->timers, Timer_ConnectRetry);
			else if (peer->passive || peer->conf.passive ||
			    peer->conf.template) {
				change_state(peer, STATE_ACTIVE, event);
				timer_stop(&peer->timers, Timer_ConnectRetry);
			} else {
				change_state(peer, STATE_CONNECT, event);
				timer_set(&peer->timers, Timer_ConnectRetry,
				    peer->conf.connectretry);
				session_connect(peer);
			}
			peer->passive = 0;
			break;
		case EVNT_STOP:
			timer_stop(&peer->timers, Timer_IdleHold);
			break;
		default:
			/* ignore */
			break;
		}
		break;
	case STATE_CONNECT:
		switch (event) {
		case EVNT_START:
			/* ignore */
			break;
		case EVNT_CON_OPEN:
			session_tcp_established(peer);
			session_open(peer);
			timer_stop(&peer->timers, Timer_ConnectRetry);
			peer->holdtime = INTERVAL_HOLD_INITIAL;
			start_timer_holdtime(peer);
			change_state(peer, STATE_OPENSENT, event);
			break;
		case EVNT_CON_OPENFAIL:
			timer_set(&peer->timers, Timer_ConnectRetry,
			    peer->conf.connectretry);
			session_close(peer);
			change_state(peer, STATE_ACTIVE, event);
			break;
		case EVNT_TIMER_CONNRETRY:
			timer_set(&peer->timers, Timer_ConnectRetry,
			    peer->conf.connectretry);
			session_connect(peer);
			break;
		default:
			change_state(peer, STATE_IDLE, event);
			break;
		}
		break;
	case STATE_ACTIVE:
		switch (event) {
		case EVNT_START:
			/* ignore */
			break;
		case EVNT_CON_OPEN:
			session_tcp_established(peer);
			session_open(peer);
			timer_stop(&peer->timers, Timer_ConnectRetry);
			peer->holdtime = INTERVAL_HOLD_INITIAL;
			start_timer_holdtime(peer);
			change_state(peer, STATE_OPENSENT, event);
			break;
		case EVNT_CON_OPENFAIL:
			timer_set(&peer->timers, Timer_ConnectRetry,
			    peer->conf.connectretry);
			session_close(peer);
			change_state(peer, STATE_ACTIVE, event);
			break;
		case EVNT_TIMER_CONNRETRY:
			timer_set(&peer->timers, Timer_ConnectRetry,
			    peer->holdtime);
			change_state(peer, STATE_CONNECT, event);
			session_connect(peer);
			break;
		default:
			change_state(peer, STATE_IDLE, event);
			break;
		}
		break;
	case STATE_OPENSENT:
		switch (event) {
		case EVNT_START:
			/* ignore */
			break;
		case EVNT_STOP:
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_CON_CLOSED:
			session_close(peer);
			timer_set(&peer->timers, Timer_ConnectRetry,
			    peer->conf.connectretry);
			change_state(peer, STATE_ACTIVE, event);
			break;
		case EVNT_CON_FATAL:
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_HOLDTIME:
			session_notification(peer, ERR_HOLDTIMEREXPIRED,
			    0, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_SENDHOLD:
			session_notification(peer, ERR_SENDHOLDTIMEREXPIRED,
			    0, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_RCVD_OPEN:
			/* parse_open calls change_state itself on failure */
			if (parse_open(peer, msg))
				break;
			session_keepalive(peer);
			change_state(peer, STATE_OPENCONFIRM, event);
			break;
		case EVNT_RCVD_NOTIFICATION:
			parse_notification(peer, msg);
			break;
		default:
			session_notification(peer,
			    ERR_FSM, ERR_FSM_UNEX_OPENSENT, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		}
		break;
	case STATE_OPENCONFIRM:
		switch (event) {
		case EVNT_START:
			/* ignore */
			break;
		case EVNT_STOP:
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_CON_CLOSED:
		case EVNT_CON_FATAL:
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_HOLDTIME:
			session_notification(peer, ERR_HOLDTIMEREXPIRED,
			    0, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_SENDHOLD:
			session_notification(peer, ERR_SENDHOLDTIMEREXPIRED,
			    0, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_KEEPALIVE:
			session_keepalive(peer);
			break;
		case EVNT_RCVD_KEEPALIVE:
			start_timer_holdtime(peer);
			change_state(peer, STATE_ESTABLISHED, event);
			break;
		case EVNT_RCVD_NOTIFICATION:
			parse_notification(peer, msg);
			break;
		default:
			session_notification(peer,
			    ERR_FSM, ERR_FSM_UNEX_OPENCONFIRM, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		}
		break;
	case STATE_ESTABLISHED:
		switch (event) {
		case EVNT_START:
			/* ignore */
			break;
		case EVNT_STOP:
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_CON_CLOSED:
		case EVNT_CON_FATAL:
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_HOLDTIME:
			session_notification(peer, ERR_HOLDTIMEREXPIRED,
			    0, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_SENDHOLD:
			session_notification(peer, ERR_SENDHOLDTIMEREXPIRED,
			    0, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		case EVNT_TIMER_KEEPALIVE:
			session_keepalive(peer);
			break;
		case EVNT_RCVD_KEEPALIVE:
			start_timer_holdtime(peer);
			break;
		case EVNT_RCVD_UPDATE:
			start_timer_holdtime(peer);
			parse_update(peer, msg);
			break;
		case EVNT_RCVD_NOTIFICATION:
			parse_notification(peer, msg);
			break;
		default:
			session_notification(peer,
			    ERR_FSM, ERR_FSM_UNEX_ESTABLISHED, NULL);
			change_state(peer, STATE_IDLE, event);
			break;
		}
		break;
	}
}

static void
start_timer_holdtime(struct peer *peer)
{
	if (peer->holdtime > 0)
		timer_set(&peer->timers, Timer_Hold, peer->holdtime);
	else
		timer_stop(&peer->timers, Timer_Hold);
}

void
start_timer_sendholdtime(struct peer *peer)
{
	uint16_t holdtime = INTERVAL_HOLD;

	if (peer->holdtime > INTERVAL_HOLD)
		holdtime = peer->holdtime;

	if (peer->holdtime > 0)
		timer_set(&peer->timers, Timer_SendHold, holdtime);
	else
		timer_stop(&peer->timers, Timer_SendHold);
}

static void
start_timer_keepalive(struct peer *peer)
{
	if (peer->holdtime > 0)
		timer_set(&peer->timers, Timer_Keepalive, peer->holdtime / 3);
	else
		timer_stop(&peer->timers, Timer_Keepalive);
}

void
change_state(struct peer *peer, enum session_state state,
    enum session_events event)
{
	enum session_state ostate;

	/* first apply new state */
	ostate = peer->prev_state;
	peer->prev_state = peer->state;
	peer->state = state;

	/* then act on it */
	switch (peer->state) {
	case STATE_IDLE:
		/* carp demotion first. new peers handled in init_peer */
		if (peer->prev_state == STATE_ESTABLISHED &&
		    peer->conf.demote_group[0] && !peer->demoted)
			session_demote(peer, +1);

		/*
		 * try to write out what's buffered (maybe a notification),
		 * don't bother if it fails
		 */
		if (peer->prev_state >= STATE_OPENSENT &&
		    msgbuf_queuelen(peer->wbuf) > 0)
			ibuf_write(peer->fd, peer->wbuf);

		/*
		 * we must start the timer for the next EVNT_START
		 * if we are coming here due to an error and the
		 * session was not established successfully before, the
		 * starttimerinterval needs to be exponentially increased
		 */
		if (peer->IdleHoldTime == 0)
			peer->IdleHoldTime = INTERVAL_IDLE_HOLD_INITIAL;
		peer->holdtime = INTERVAL_HOLD_INITIAL;
		timer_stop(&peer->timers, Timer_ConnectRetry);
		timer_stop(&peer->timers, Timer_Keepalive);
		timer_stop(&peer->timers, Timer_Hold);
		timer_stop(&peer->timers, Timer_SendHold);
		timer_stop(&peer->timers, Timer_IdleHold);
		timer_stop(&peer->timers, Timer_IdleHoldReset);
		session_close(peer);
		msgbuf_clear(peer->wbuf);
		peer->rpending = 0;
		memset(&peer->capa.peer, 0, sizeof(peer->capa.peer));
		session_md5_reload(peer);

		if (peer->prev_state == STATE_ESTABLISHED) {
			if (peer->capa.neg.grestart.restart == 2 &&
			    (event == EVNT_CON_CLOSED ||
			    event == EVNT_CON_FATAL ||
			    (peer->capa.neg.grestart.grnotification &&
			    (event == EVNT_RCVD_GRACE_NOTIFICATION ||
			    event == EVNT_TIMER_HOLDTIME ||
			    event == EVNT_TIMER_SENDHOLD)))) {
				/* don't punish graceful restart */
				timer_set(&peer->timers, Timer_IdleHold, 0);
				session_graceful_restart(peer);
			} else if (event != EVNT_STOP) {
				timer_set(&peer->timers, Timer_IdleHold,
				    peer->IdleHoldTime);
				if (event != EVNT_NONE &&
				    peer->IdleHoldTime < MAX_IDLE_HOLD/2)
					peer->IdleHoldTime *= 2;
				session_down(peer);
			} else {
				session_down(peer);
			}
		} else if (event != EVNT_STOP) {
			timer_set(&peer->timers, Timer_IdleHold,
			    peer->IdleHoldTime);
			if (event != EVNT_NONE &&
			    peer->IdleHoldTime < MAX_IDLE_HOLD / 2)
				peer->IdleHoldTime *= 2;
		}

		if (peer->prev_state == STATE_NONE ||
		    peer->prev_state == STATE_ESTABLISHED) {
			/* initialize capability negotiation structures */
			memcpy(&peer->capa.ann, &peer->conf.capabilities,
			    sizeof(peer->capa.ann));
		}
		break;
	case STATE_CONNECT:
		if (peer->prev_state == STATE_ESTABLISHED &&
		    peer->capa.neg.grestart.restart == 2) {
			/* do the graceful restart dance */
			session_graceful_restart(peer);
			peer->holdtime = INTERVAL_HOLD_INITIAL;
			timer_stop(&peer->timers, Timer_ConnectRetry);
			timer_stop(&peer->timers, Timer_Keepalive);
			timer_stop(&peer->timers, Timer_Hold);
			timer_stop(&peer->timers, Timer_SendHold);
			timer_stop(&peer->timers, Timer_IdleHold);
			timer_stop(&peer->timers, Timer_IdleHoldReset);
			session_close(peer);
			msgbuf_clear(peer->wbuf);
			memset(&peer->capa.peer, 0, sizeof(peer->capa.peer));
		}
		break;
	case STATE_ACTIVE:
		session_md5_reload(peer);
		break;
	case STATE_OPENSENT:
		break;
	case STATE_OPENCONFIRM:
		break;
	case STATE_ESTABLISHED:
		timer_set(&peer->timers, Timer_IdleHoldReset,
		    peer->IdleHoldTime);
		if (peer->demoted)
			timer_set(&peer->timers, Timer_CarpUndemote,
			    INTERVAL_HOLD_DEMOTED);
		session_up(peer);
		break;
	default:		/* something seriously fucked */
		break;
	}

	log_statechange(peer, ostate, event);
	session_mrt_dump_state(peer);
}
