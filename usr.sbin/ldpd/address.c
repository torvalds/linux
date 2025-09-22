/*	$OpenBSD: address.c,v 1.35 2017/03/04 00:21:48 renato Exp $ */

/*
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
#include <stdlib.h>
#include <string.h>

#include "ldpd.h"
#include "ldpe.h"
#include "lde.h"
#include "log.h"

static void	 send_address(struct nbr *, int, struct if_addr_head *,
		    unsigned int, int);
static int	 gen_address_list_tlv(struct ibuf *, int, struct if_addr_head *,
		    unsigned int);
static int	 gen_mac_list_tlv(struct ibuf *, uint8_t *);
static void	 address_list_add(struct if_addr_head *, struct if_addr *);
static void	 address_list_clr(struct if_addr_head *);
static void	 log_msg_address(int, uint16_t, struct nbr *, int,
		    union ldpd_addr *);
static void	 log_msg_mac_withdrawal(int, struct nbr *, uint8_t *);

static void
send_address(struct nbr *nbr, int af, struct if_addr_head *addr_list,
    unsigned int addr_count, int withdraw)
{
	struct ibuf	*buf;
	uint16_t	 msg_type;
	uint8_t		 addr_size;
	struct if_addr	*if_addr;
	uint16_t	 size;
	unsigned int	 tlv_addr_count = 0;
	int		 err = 0;

	/* nothing to send */
	if (LIST_EMPTY(addr_list))
		return;

	if (!withdraw)
		msg_type = MSG_TYPE_ADDR;
	else
		msg_type = MSG_TYPE_ADDRWITHDRAW;

	switch (af) {
	case AF_INET:
		addr_size = sizeof(struct in_addr);
		break;
	case AF_INET6:
		addr_size = sizeof(struct in6_addr);
		break;
	default:
		fatalx("send_address: unknown af");
	}

	while ((if_addr = LIST_FIRST(addr_list)) != NULL) {
		/*
		 * Send as many addresses as possible - respect the session's
		 * negotiated maximum pdu length.
		 */
		size = LDP_HDR_SIZE + LDP_MSG_SIZE + ADDR_LIST_SIZE;
		if (size + addr_count * addr_size <= nbr->max_pdu_len)
			tlv_addr_count = addr_count;
		else
			tlv_addr_count = (nbr->max_pdu_len - size) / addr_size;
		size += tlv_addr_count * addr_size;
		addr_count -= tlv_addr_count;

		if ((buf = ibuf_open(size)) == NULL)
			fatal(__func__);

		err |= gen_ldp_hdr(buf, size);
		size -= LDP_HDR_SIZE;
		err |= gen_msg_hdr(buf, msg_type, size);
		size -= LDP_MSG_SIZE;
		err |= gen_address_list_tlv(buf, af, addr_list, tlv_addr_count);
		if (err) {
			address_list_clr(addr_list);
			ibuf_free(buf);
			return;
		}

		while ((if_addr = LIST_FIRST(addr_list)) != NULL) {
			log_msg_address(1, msg_type, nbr, af, &if_addr->addr);

			LIST_REMOVE(if_addr, entry);
			free(if_addr);
			if (--tlv_addr_count == 0)
				break;
		}

		evbuf_enqueue(&nbr->tcp->wbuf, buf);
	}

	nbr_fsm(nbr, NBR_EVT_PDU_SENT);
}

void
send_address_single(struct nbr *nbr, struct if_addr *if_addr, int withdraw)
{
	struct if_addr_head	 addr_list;

	LIST_INIT(&addr_list);
	address_list_add(&addr_list, if_addr);
	send_address(nbr, if_addr->af, &addr_list, 1, withdraw);
}

void
send_address_all(struct nbr *nbr, int af)
{
	struct if_addr_head	 addr_list;
	struct if_addr		*if_addr;
	unsigned int		 addr_count = 0;

	LIST_INIT(&addr_list);
	LIST_FOREACH(if_addr, &global.addr_list, entry) {
		if (if_addr->af != af)
			continue;

		address_list_add(&addr_list, if_addr);
		addr_count++;
	}

	send_address(nbr, af, &addr_list, addr_count, 0);
}

void
send_mac_withdrawal(struct nbr *nbr, struct map *fec, uint8_t *mac)
{
	struct ibuf	*buf;
	uint16_t	 size;
	int		 err;

	size = LDP_HDR_SIZE + LDP_MSG_SIZE + ADDR_LIST_SIZE + len_fec_tlv(fec) +
	    TLV_HDR_SIZE;
	if (mac)
		size += ETHER_ADDR_LEN;

	if ((buf = ibuf_open(size)) == NULL)
		fatal(__func__);

	err = gen_ldp_hdr(buf, size);
	size -= LDP_HDR_SIZE;
	err |= gen_msg_hdr(buf, MSG_TYPE_ADDRWITHDRAW, size);
	size -= LDP_MSG_SIZE;
	err |= gen_address_list_tlv(buf, AF_INET, NULL, 0);
	err |= gen_fec_tlv(buf, fec);
	err |= gen_mac_list_tlv(buf, mac);
	if (err) {
		ibuf_free(buf);
		return;
	}

	log_msg_mac_withdrawal(1, nbr, mac);

	evbuf_enqueue(&nbr->tcp->wbuf, buf);

	nbr_fsm(nbr, NBR_EVT_PDU_SENT);
}

int
recv_address(struct nbr *nbr, char *buf, uint16_t len)
{
	struct ldp_msg		msg;
	uint16_t		msg_type;
	enum imsg_type		type;
	struct address_list_tlv	alt;
	uint16_t		alt_len;
	uint16_t		alt_family;
	struct lde_addr		lde_addr;

	memcpy(&msg, buf, sizeof(msg));
	msg_type = ntohs(msg.type);
	switch (msg_type) {
	case MSG_TYPE_ADDR:
		type = IMSG_ADDRESS_ADD;
		break;
	case MSG_TYPE_ADDRWITHDRAW:
		type = IMSG_ADDRESS_DEL;
		break;
	default:
		fatalx("recv_address: unexpected msg type");
	}
	buf += LDP_MSG_SIZE;
	len -= LDP_MSG_SIZE;

	/* Address List TLV */
	if (len < ADDR_LIST_SIZE) {
		session_shutdown(nbr, S_BAD_MSG_LEN, msg.id, msg.type);
		return (-1);
	}
	memcpy(&alt, buf, sizeof(alt));
	alt_len = ntohs(alt.length);
	alt_family = ntohs(alt.family);
	if (alt_len > len - TLV_HDR_SIZE) {
		session_shutdown(nbr, S_BAD_TLV_LEN, msg.id, msg.type);
		return (-1);
	}
	if (ntohs(alt.type) != TLV_TYPE_ADDRLIST) {
		send_notification(nbr->tcp, S_MISS_MSG, msg.id, msg.type);
		return (-1);
	}
	switch (alt_family) {
	case AF_IPV4:
		if (!nbr->v4_enabled)
			/* just ignore the message */
			return (0);
		break;
	case AF_IPV6:
		if (!nbr->v6_enabled)
			/* just ignore the message */
			return (0);
		break;
	default:
		send_notification(nbr->tcp, S_UNSUP_ADDR, msg.id, msg.type);
		return (-1);
	}
	alt_len -= sizeof(alt.family);
	buf += sizeof(alt);
	len -= sizeof(alt);

	/* Process all received addresses */
	while (alt_len > 0) {
		switch (alt_family) {
		case AF_IPV4:
			if (alt_len < sizeof(struct in_addr)) {
				session_shutdown(nbr, S_BAD_TLV_LEN, msg.id,
				    msg.type);
				return (-1);
			}

			memset(&lde_addr, 0, sizeof(lde_addr));
			lde_addr.af = AF_INET;
			memcpy(&lde_addr.addr, buf, sizeof(struct in_addr));

			buf += sizeof(struct in_addr);
			len -= sizeof(struct in_addr);
			alt_len -= sizeof(struct in_addr);
			break;
		case AF_IPV6:
			if (alt_len < sizeof(struct in6_addr)) {
				session_shutdown(nbr, S_BAD_TLV_LEN, msg.id,
				    msg.type);
				return (-1);
			}

			memset(&lde_addr, 0, sizeof(lde_addr));
			lde_addr.af = AF_INET6;
			memcpy(&lde_addr.addr, buf, sizeof(struct in6_addr));

			buf += sizeof(struct in6_addr);
			len -= sizeof(struct in6_addr);
			alt_len -= sizeof(struct in6_addr);
			break;
		default:
			fatalx("recv_address: unknown af");
		}

		log_msg_address(0, msg_type, nbr, lde_addr.af, &lde_addr.addr);

		ldpe_imsg_compose_lde(type, nbr->peerid, 0, &lde_addr,
		    sizeof(lde_addr));
	}

	/* Optional Parameters */
	while (len > 0) {
		struct tlv 	tlv;
		uint16_t	tlv_type;
		uint16_t	tlv_len;

		if (len < sizeof(tlv)) {
			session_shutdown(nbr, S_BAD_TLV_LEN, msg.id, msg.type);
			return (-1);
		}

		memcpy(&tlv, buf, TLV_HDR_SIZE);
		tlv_type = ntohs(tlv.type);
		tlv_len = ntohs(tlv.length);
		if (tlv_len + TLV_HDR_SIZE > len) {
			session_shutdown(nbr, S_BAD_TLV_LEN, msg.id, msg.type);
			return (-1);
		}
		buf += TLV_HDR_SIZE;
		len -= TLV_HDR_SIZE;

		switch (tlv_type) {
		default:
			if (!(ntohs(tlv.type) & UNKNOWN_FLAG))
				send_notification_rtlvs(nbr, S_UNKNOWN_TLV,
				    msg.id, msg.type, tlv_type, tlv_len, buf);
			/* ignore unknown tlv */
			break;
		}
		buf += tlv_len;
		len -= tlv_len;
	}

	return (0);
}

static int
gen_address_list_tlv(struct ibuf *buf, int af, struct if_addr_head *addr_list,
    unsigned int tlv_addr_count)
{
	struct address_list_tlv	 alt;
	uint16_t		 addr_size;
	struct if_addr		*if_addr;
	int			 err = 0;

	memset(&alt, 0, sizeof(alt));
	alt.type = htons(TLV_TYPE_ADDRLIST);

	switch (af) {
	case AF_INET:
		alt.family = htons(AF_IPV4);
		addr_size = sizeof(struct in_addr);
		break;
	case AF_INET6:
		alt.family = htons(AF_IPV6);
		addr_size = sizeof(struct in6_addr);
		break;
	default:
		fatalx("gen_address_list_tlv: unknown af");
	}
	alt.length = htons(sizeof(alt.family) + addr_size * tlv_addr_count);

	err |= ibuf_add(buf, &alt, sizeof(alt));
	if (addr_list == NULL)
		return (err);

	LIST_FOREACH(if_addr, addr_list, entry) {
		err |= ibuf_add(buf, &if_addr->addr, addr_size);
		if (--tlv_addr_count == 0)
			break;
	}

	return (err);
}

static int
gen_mac_list_tlv(struct ibuf *buf, uint8_t *mac)
{
	struct tlv	 tlv;
	int		 err;

	memset(&tlv, 0, sizeof(tlv));
	tlv.type = htons(TLV_TYPE_MAC_LIST);
	if (mac)
		tlv.length = htons(ETHER_ADDR_LEN);
	err = ibuf_add(buf, &tlv, sizeof(tlv));
	if (mac)
		err |= ibuf_add(buf, mac, ETHER_ADDR_LEN);

	return (err);
}

static void
address_list_add(struct if_addr_head *addr_list, struct if_addr *if_addr)
{
	struct if_addr		*new;

	new = malloc(sizeof(*new));
	if (new == NULL)
		fatal(__func__);
	*new = *if_addr;

	LIST_INSERT_HEAD(addr_list, new, entry);
}

static void
address_list_clr(struct if_addr_head *addr_list)
{
	struct if_addr		*if_addr;

	while ((if_addr = LIST_FIRST(addr_list)) != NULL) {
		LIST_REMOVE(if_addr, entry);
		free(if_addr);
	}
}

static void
log_msg_address(int out, uint16_t msg_type, struct nbr *nbr, int af,
    union ldpd_addr *addr)
{
	log_debug("msg-%s: %s: lsr-id %s, address %s", (out) ? "out" : "in",
	    msg_name(msg_type), inet_ntoa(nbr->id), log_addr(af, addr));
}

static void
log_msg_mac_withdrawal(int out, struct nbr *nbr, uint8_t *mac)
{
	log_debug("msg-%s: mac withdrawal: lsr-id %s, mac %s",
	    (out) ? "out" : "in", inet_ntoa(nbr->id),
	    (mac) ? ether_ntoa((struct ether_addr *)mac) : "wildcard");
}
