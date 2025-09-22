/*	$OpenBSD: hello.c,v 1.59 2023/07/03 11:51:27 claudio Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
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
#include <arpa/inet.h>
#include <string.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

static int	gen_hello_prms_tlv(struct ibuf *buf, uint16_t, uint16_t);
static int	gen_opt4_hello_prms_tlv(struct ibuf *, uint16_t, uint32_t);
static int	gen_opt16_hello_prms_tlv(struct ibuf *, uint16_t, uint8_t *);
static int	gen_ds_hello_prms_tlv(struct ibuf *, uint32_t);
static int	tlv_decode_hello_prms(char *, uint16_t, uint16_t *, uint16_t *);
static int	tlv_decode_opt_hello_prms(char *, uint16_t, int *, int,
		    union ldpd_addr *, uint32_t *, uint16_t *);

int
send_hello(enum hello_type type, struct iface_af *ia, struct tnbr *tnbr)
{
	int			 af;
	union ldpd_addr		 dst;
	uint16_t		 size, holdtime = 0, flags = 0;
	int			 fd = 0;
	struct ibuf		*buf;
	int			 err = 0;

	switch (type) {
	case HELLO_LINK:
		af = ia->af;
		holdtime = ia->hello_holdtime;
		flags = 0;
		fd = (ldp_af_global_get(&global, af))->ldp_disc_socket;

		/* multicast destination address */
		switch (af) {
		case AF_INET:
			if (!(leconf->ipv4.flags & F_LDPD_AF_NO_GTSM))
				flags |= F_HELLO_GTSM;
			dst.v4 = global.mcast_addr_v4;
			break;
		case AF_INET6:
			dst.v6 = global.mcast_addr_v6;
			break;
		default:
			fatalx("send_hello: unknown af");
		}
		break;
	case HELLO_TARGETED:
		af = tnbr->af;
		holdtime = tnbr->hello_holdtime;
		flags = F_HELLO_TARGETED;
		if ((tnbr->flags & F_TNBR_CONFIGURED) || tnbr->pw_count)
			flags |= F_HELLO_REQ_TARG;
		fd = (ldp_af_global_get(&global, af))->ldp_edisc_socket;

		/* unicast destination address */
		dst = tnbr->addr;
		break;
	default:
		fatalx("send_hello: unknown hello type");
	}

	/* calculate message size */
	size = LDP_HDR_SIZE + LDP_MSG_SIZE + sizeof(struct hello_prms_tlv);
	switch (af) {
	case AF_INET:
		size += sizeof(struct hello_prms_opt4_tlv);
		break;
	case AF_INET6:
		size += sizeof(struct hello_prms_opt16_tlv);
		break;
	default:
		fatalx("send_hello: unknown af");
	}
	size += sizeof(struct hello_prms_opt4_tlv);
	if (ldp_is_dual_stack(leconf))
		size += sizeof(struct hello_prms_opt4_tlv);

	/* generate message */
	if ((buf = ibuf_open(size)) == NULL)
		fatal(__func__);

	err |= gen_ldp_hdr(buf, size);
	size -= LDP_HDR_SIZE;
	err |= gen_msg_hdr(buf, MSG_TYPE_HELLO, size);
	err |= gen_hello_prms_tlv(buf, holdtime, flags);

	/*
	 * RFC 7552 - Section 6.1:
	 * "An LSR MUST include only the transport address whose address
	 * family is the same as that of the IP packet carrying the Hello
	 * message".
	 */
	switch (af) {
	case AF_INET:
		err |= gen_opt4_hello_prms_tlv(buf, TLV_TYPE_IPV4TRANSADDR,
		    leconf->ipv4.trans_addr.v4.s_addr);
		break;
	case AF_INET6:
		err |= gen_opt16_hello_prms_tlv(buf, TLV_TYPE_IPV6TRANSADDR,
		    leconf->ipv6.trans_addr.v6.s6_addr);
		break;
	default:
		fatalx("send_hello: unknown af");
	}

	err |= gen_opt4_hello_prms_tlv(buf, TLV_TYPE_CONFIG,
	    htonl(global.conf_seqnum));

   	/*
	 * RFC 7552 - Section 6.1.1:
	 * "A Dual-stack LSR (i.e., an LSR supporting Dual-stack LDP for a peer)
	 * MUST include the Dual-Stack capability TLV in all of its LDP Hellos".
	 */
	if (ldp_is_dual_stack(leconf))
		err |= gen_ds_hello_prms_tlv(buf, leconf->trans_pref);

	if (err) {
		ibuf_free(buf);
		return (-1);
	}

	send_packet(fd, af, &dst, ia, ibuf_data(buf), ibuf_size(buf));
	ibuf_free(buf);

	return (0);
}

void
recv_hello(struct in_addr lsr_id, struct ldp_msg *msg, int af,
    union ldpd_addr *src, struct iface *iface, int multicast, char *buf,
    uint16_t len)
{
	struct adj		*adj = NULL;
	struct nbr		*nbr, *nbrt;
	uint16_t		 holdtime, flags;
	int			 tlvs_rcvd;
	int			 ds_tlv;
	union ldpd_addr		 trans_addr;
	uint32_t		 scope_id = 0;
	uint32_t		 conf_seqnum;
	uint16_t		 trans_pref;
	int			 r;
	struct hello_source	 source;
	struct iface_af		*ia = NULL;
	struct tnbr		*tnbr = NULL;

	r = tlv_decode_hello_prms(buf, len, &holdtime, &flags);
	if (r == -1) {
		log_debug("%s: lsr-id %s: failed to decode params", __func__,
		    inet_ntoa(lsr_id));
		return;
	}
	/* safety checks */
	if (holdtime != 0 && holdtime < MIN_HOLDTIME) {
		log_debug("%s: lsr-id %s: invalid hello holdtime (%u)",
		    __func__, inet_ntoa(lsr_id), holdtime);
		return;
	}
	if (multicast && (flags & F_HELLO_TARGETED)) {
		log_debug("%s: lsr-id %s: multicast targeted hello", __func__,
		    inet_ntoa(lsr_id));
		return;
	}
	if (!multicast && !((flags & F_HELLO_TARGETED))) {
		log_debug("%s: lsr-id %s: unicast link hello", __func__,
		    inet_ntoa(lsr_id));
		return;
	}
	buf += r;
	len -= r;

	r = tlv_decode_opt_hello_prms(buf, len, &tlvs_rcvd, af, &trans_addr,
	    &conf_seqnum, &trans_pref);
	if (r == -1) {
		log_debug("%s: lsr-id %s: failed to decode optional params",
		    __func__, inet_ntoa(lsr_id));
		return;
	}
	if (r != len) {
		log_debug("%s: lsr-id %s: unexpected data in message",
		    __func__, inet_ntoa(lsr_id));
		return;
	}

	/* implicit transport address */
	if (!(tlvs_rcvd & F_HELLO_TLV_RCVD_ADDR))
		trans_addr = *src;
	if (bad_addr(af, &trans_addr)) {
		log_debug("%s: lsr-id %s: invalid transport address %s",
		    __func__, inet_ntoa(lsr_id), log_addr(af, &trans_addr));
		return;
	}
	if (af == AF_INET6 && IN6_IS_SCOPE_EMBED(&trans_addr.v6)) {
		/*
	 	 * RFC 7552 - Section 6.1:
		 * "An LSR MUST use a global unicast IPv6 address in an IPv6
		 * Transport Address optional object of outgoing targeted
		 * Hellos and check for the same in incoming targeted Hellos
		 * (i.e., MUST discard the targeted Hello if it failed the
		 * check)".
		 */
		if (flags & F_HELLO_TARGETED) {
			log_debug("%s: lsr-id %s: invalid targeted hello "
			    "transport address %s", __func__, inet_ntoa(lsr_id),
			     log_addr(af, &trans_addr));
			return;
		}
		scope_id = iface->ifindex;
	}

	memset(&source, 0, sizeof(source));
	source.lsr_id = lsr_id;
	if (flags & F_HELLO_TARGETED) {
		/*
	 	 * RFC 7552 - Section 5.2:
		* "The link-local IPv6 addresses MUST NOT be used as the
		* targeted LDP Hello packet's source or destination addresses".
		*/
		if (af == AF_INET6 && IN6_IS_SCOPE_EMBED(&src->v6)) {
			log_debug("%s: lsr-id %s: targeted hello with "
			    "link-local source address", __func__,
			    inet_ntoa(lsr_id));
			return;
		}

		tnbr = tnbr_find(leconf, af, src);

		/* remove the dynamic tnbr if the 'R' bit was cleared */
		if (tnbr && (tnbr->flags & F_TNBR_DYNAMIC) &&
		    !((flags & F_HELLO_REQ_TARG))) {
			tnbr->flags &= ~F_TNBR_DYNAMIC;
			tnbr = tnbr_check(tnbr);
		}

		if (!tnbr) {
			if (!((flags & F_HELLO_REQ_TARG) &&
			    ((ldp_af_conf_get(leconf, af))->flags &
			    F_LDPD_AF_THELLO_ACCEPT)))
				return;

			tnbr = tnbr_new(leconf, af, src);
			tnbr->flags |= F_TNBR_DYNAMIC;
			tnbr_update(tnbr);
			LIST_INSERT_HEAD(&leconf->tnbr_list, tnbr, entry);
		}

		source.type = HELLO_TARGETED;
		source.target = tnbr;
	} else {
		ia = iface_af_get(iface, af);
		source.type = HELLO_LINK;
		source.link.ia = ia;
		source.link.src_addr = *src;
	}

	adj = adj_find(&source);
	nbr = nbr_find_ldpid(lsr_id.s_addr);

	/* check dual-stack tlv */
	ds_tlv = (tlvs_rcvd & F_HELLO_TLV_RCVD_DS) ? 1 : 0;
	if (ds_tlv && trans_pref != leconf->trans_pref) {
		/*
	 	 * RFC 7552 - Section 6.1.1:
		 * "If the Dual-Stack capability TLV is present and the remote
		 * preference does not match the local preference (or does not
		 * get recognized), then the LSR MUST discard the Hello message
		 * and log an error.
		 * If an LDP session was already in place, then the LSR MUST
		 * send a fatal Notification message with status code of
		 * 'Transport Connection Mismatch' and reset the session".
		 */
		log_debug("%s: lsr-id %s: remote transport preference does not "
		    "match the local preference", __func__, inet_ntoa(lsr_id));
		if (nbr)
			session_shutdown(nbr, S_TRANS_MISMTCH, msg->id,
			    msg->type);
		if (adj)
			adj_del(adj, S_SHUTDOWN);
		return;
	}

	/*
	 * Check for noncompliant dual-stack neighbor according to
	 * RFC 7552 section 6.1.1.
	 */
	if (nbr && !ds_tlv) {
		switch (af) {
		case AF_INET:
			if (nbr_adj_count(nbr, AF_INET6) > 0) {
				session_shutdown(nbr, S_DS_NONCMPLNCE,
				    msg->id, msg->type);
				return;
			}
			break;
		case AF_INET6:
			if (nbr_adj_count(nbr, AF_INET) > 0) {
				session_shutdown(nbr, S_DS_NONCMPLNCE,
				    msg->id, msg->type);
				return;
			}
			break;
		default:
			fatalx("recv_hello: unknown af");
		}
	}

	/*
	 * Protections against misconfigured networks and buggy implementations.
	 */
	if (nbr && nbr->af == af &&
	    (ldp_addrcmp(af, &nbr->raddr, &trans_addr) ||
	    nbr->raddr_scope != scope_id)) {
		log_warnx("%s: lsr-id %s: hello packet advertising a different "
		    "transport address", __func__, inet_ntoa(lsr_id));
		if (adj)
			adj_del(adj, S_SHUTDOWN);
		return;
	}
	if (nbr == NULL) {
		nbrt = nbr_find_addr(af, &trans_addr);
		if (nbrt) {
			log_debug("%s: transport address %s is already being "
			    "used by lsr-id %s", __func__, log_addr(af,
			    &trans_addr), inet_ntoa(nbrt->id));
			if (adj)
				adj_del(adj, S_SHUTDOWN);
			return;
		}
	}

	if (adj == NULL) {
		adj = adj_new(lsr_id, &source, &trans_addr);
		if (nbr) {
			adj->nbr = nbr;
			LIST_INSERT_HEAD(&nbr->adj_list, adj, nbr_entry);
		}
	}

	/*
	 * If the hello adjacency's address-family doesn't match the local
	 * preference, then an adjacency is still created but we don't attempt
	 * to start an LDP session.
	 */
	if (nbr == NULL && (!ds_tlv ||
	    ((trans_pref == DUAL_STACK_LDPOV4 && af == AF_INET) ||
	    (trans_pref == DUAL_STACK_LDPOV6 && af == AF_INET6))))
		nbr = nbr_new(lsr_id, af, ds_tlv, &trans_addr, scope_id);

	/* dynamic LDPv4 GTSM negotiation as per RFC 6720 */
	if (nbr) {
		if (flags & F_HELLO_GTSM)
			nbr->flags |= F_NBR_GTSM_NEGOTIATED;
		else
			nbr->flags &= ~F_NBR_GTSM_NEGOTIATED;
	}

	/* update neighbor's configuration sequence number */
	if (nbr && (tlvs_rcvd & F_HELLO_TLV_RCVD_CONF)) {
		if (conf_seqnum > nbr->conf_seqnum &&
		    nbr_pending_idtimer(nbr))
			nbr_stop_idtimer(nbr);
		nbr->conf_seqnum = conf_seqnum;
	}

	/* always update the holdtime to properly handle runtime changes */
	switch (source.type) {
	case HELLO_LINK:
		if (holdtime == 0)
			holdtime = LINK_DFLT_HOLDTIME;

		adj->holdtime = min(ia->hello_holdtime, holdtime);
		break;
	case HELLO_TARGETED:
		if (holdtime == 0)
			holdtime = TARGETED_DFLT_HOLDTIME;

		adj->holdtime = min(tnbr->hello_holdtime, holdtime);
	}
	if (adj->holdtime != INFINITE_HOLDTIME)
		adj_start_itimer(adj);
	else
		adj_stop_itimer(adj);

	if (nbr && nbr->state == NBR_STA_PRESENT && !nbr_pending_idtimer(nbr) &&
	    nbr_session_active_role(nbr) && !nbr_pending_connect(nbr))
		nbr_establish_connection(nbr);
}

static int
gen_hello_prms_tlv(struct ibuf *buf, uint16_t holdtime, uint16_t flags)
{
	struct hello_prms_tlv	parms;

	memset(&parms, 0, sizeof(parms));
	parms.type = htons(TLV_TYPE_COMMONHELLO);
	parms.length = htons(sizeof(parms.holdtime) + sizeof(parms.flags));
	parms.holdtime = htons(holdtime);
	parms.flags = htons(flags);

	return (ibuf_add(buf, &parms, sizeof(parms)));
}

static int
gen_opt4_hello_prms_tlv(struct ibuf *buf, uint16_t type, uint32_t value)
{
	struct hello_prms_opt4_tlv	parms;

	memset(&parms, 0, sizeof(parms));
	parms.type = htons(type);
	parms.length = htons(sizeof(parms.value));
	parms.value = value;

	return (ibuf_add(buf, &parms, sizeof(parms)));
}

static int
gen_opt16_hello_prms_tlv(struct ibuf *buf, uint16_t type, uint8_t *value)
{
	struct hello_prms_opt16_tlv	parms;

	memset(&parms, 0, sizeof(parms));
	parms.type = htons(type);
	parms.length = htons(sizeof(parms.value));
	memcpy(&parms.value, value, sizeof(parms.value));

	return (ibuf_add(buf, &parms, sizeof(parms)));
}

static int
gen_ds_hello_prms_tlv(struct ibuf *buf, uint32_t value)
{
	if (leconf->flags & F_LDPD_DS_CISCO_INTEROP)
		value = htonl(value);
	else
		value = htonl(value << 28);

	return (gen_opt4_hello_prms_tlv(buf, TLV_TYPE_DUALSTACK, value));
}

static int
tlv_decode_hello_prms(char *buf, uint16_t len, uint16_t *holdtime,
    uint16_t *flags)
{
	struct hello_prms_tlv	tlv;

	if (len < sizeof(tlv))
		return (-1);
	memcpy(&tlv, buf, sizeof(tlv));

	if (tlv.type != htons(TLV_TYPE_COMMONHELLO))
		return (-1);
	if (ntohs(tlv.length) != sizeof(tlv) - TLV_HDR_SIZE)
		return (-1);

	*holdtime = ntohs(tlv.holdtime);
	*flags = ntohs(tlv.flags);

	return (sizeof(tlv));
}

static int
tlv_decode_opt_hello_prms(char *buf, uint16_t len, int *tlvs_rcvd, int af,
    union ldpd_addr *addr, uint32_t *conf_number, uint16_t *trans_pref)
{
	struct tlv	tlv;
	uint16_t	tlv_len;
	int		total = 0;

	*tlvs_rcvd = 0;
	memset(addr, 0, sizeof(*addr));
	*conf_number = 0;
	*trans_pref = 0;

	/*
	 * RFC 7552 - Section 6.1:
	 * "An LSR SHOULD accept the Hello message that contains both IPv4 and
	 * IPv6 Transport Address optional objects but MUST use only the
	 * transport address whose address family is the same as that of the
	 * IP packet carrying the Hello message.  An LSR SHOULD accept only
	 * the first Transport Address optional object for a given address
	 * family in the received Hello message and ignore the rest if the
	 * LSR receives more than one Transport Address optional object for a
	 * given address family".
	 */
	while (len >= sizeof(tlv)) {
		memcpy(&tlv, buf, TLV_HDR_SIZE);
		tlv_len = ntohs(tlv.length);
		if (tlv_len + TLV_HDR_SIZE > len)
			return (-1);
		buf += TLV_HDR_SIZE;
		len -= TLV_HDR_SIZE;
		total += TLV_HDR_SIZE;

		switch (ntohs(tlv.type)) {
		case TLV_TYPE_IPV4TRANSADDR:
			if (tlv_len != sizeof(addr->v4))
				return (-1);
			if (af != AF_INET)
				return (-1);
			if (*tlvs_rcvd & F_HELLO_TLV_RCVD_ADDR)
				break;
			memcpy(&addr->v4, buf, sizeof(addr->v4));
			*tlvs_rcvd |= F_HELLO_TLV_RCVD_ADDR;
			break;
		case TLV_TYPE_IPV6TRANSADDR:
			if (tlv_len != sizeof(addr->v6))
				return (-1);
			if (af != AF_INET6)
				return (-1);
			if (*tlvs_rcvd & F_HELLO_TLV_RCVD_ADDR)
				break;
			memcpy(&addr->v6, buf, sizeof(addr->v6));
			*tlvs_rcvd |= F_HELLO_TLV_RCVD_ADDR;
			break;
		case TLV_TYPE_CONFIG:
			if (tlv_len != sizeof(uint32_t))
				return (-1);
			memcpy(conf_number, buf, sizeof(uint32_t));
			*tlvs_rcvd |= F_HELLO_TLV_RCVD_CONF;
			break;
		case TLV_TYPE_DUALSTACK:
			if (tlv_len != sizeof(uint32_t))
				return (-1);
   			/*
	 		 * RFC 7552 - Section 6.1:
			 * "A Single-stack LSR does not need to use the
			 * Dual-Stack capability in Hello messages and SHOULD
			 * ignore this capability if received".
			 */
			if (!ldp_is_dual_stack(leconf))
				break;
			/* Shame on you, Cisco! */
			if (leconf->flags & F_LDPD_DS_CISCO_INTEROP) {
				memcpy(trans_pref, buf + sizeof(uint16_t),
				    sizeof(uint16_t));
				*trans_pref = ntohs(*trans_pref);
			} else {
				memcpy(trans_pref, buf , sizeof(uint16_t));
				*trans_pref = ntohs(*trans_pref) >> 12;
			}
			*tlvs_rcvd |= F_HELLO_TLV_RCVD_DS;
			break;
		default:
			/* if unknown flag set, ignore TLV */
			if (!(ntohs(tlv.type) & UNKNOWN_FLAG))
				return (-1);
			break;
		}
		buf += tlv_len;
		len -= tlv_len;
		total += tlv_len;
	}

	return (total);
}
