/*	$OpenBSD: notification.c,v 1.46 2017/03/04 00:15:35 renato Exp $ */

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
#include <string.h>

#include "ldpd.h"
#include "ldp.h"
#include "log.h"
#include "ldpe.h"

static int	 gen_returned_tlvs(struct ibuf *, uint16_t, uint16_t, char *);
static void	 log_msg_notification(int, struct nbr *, struct notify_msg *);

void
send_notification_full(struct tcp_conn *tcp, struct notify_msg *nm)
{
	struct ibuf	*buf;
	uint16_t	 size;
	int		 err = 0;

	/* calculate size */
	size = LDP_HDR_SIZE + LDP_MSG_SIZE + STATUS_SIZE;
	if (nm->flags & F_NOTIF_PW_STATUS)
		size += PW_STATUS_TLV_SIZE;
	if (nm->flags & F_NOTIF_FEC)
		size += len_fec_tlv(&nm->fec);
	if (nm->flags & F_NOTIF_RETURNED_TLVS)
		size += TLV_HDR_SIZE * 2 + nm->rtlvs.length;

	if ((buf = ibuf_open(size)) == NULL)
		fatal(__func__);

	err |= gen_ldp_hdr(buf, size);
	size -= LDP_HDR_SIZE;
	err |= gen_msg_hdr(buf, MSG_TYPE_NOTIFICATION, size);
	err |= gen_status_tlv(buf, nm->status_code, nm->msg_id, nm->msg_type);
	/* optional tlvs */
	if (nm->flags & F_NOTIF_PW_STATUS)
		err |= gen_pw_status_tlv(buf, nm->pw_status);
	if (nm->flags & F_NOTIF_FEC)
		err |= gen_fec_tlv(buf, &nm->fec);
	if (nm->flags & F_NOTIF_RETURNED_TLVS)
		err |= gen_returned_tlvs(buf, nm->rtlvs.type, nm->rtlvs.length,
		    nm->rtlvs.data);
	if (err) {
		ibuf_free(buf);
		return;
	}

	if (tcp->nbr) {
		log_msg_notification(1, tcp->nbr, nm);
		nbr_fsm(tcp->nbr, NBR_EVT_PDU_SENT);
	}

	evbuf_enqueue(&tcp->wbuf, buf);
}

/* send a notification without optional tlvs */
void
send_notification(struct tcp_conn *tcp, uint32_t status_code, uint32_t msg_id,
    uint16_t msg_type)
{
	struct notify_msg	 nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = status_code;
	nm.msg_id = msg_id;
	nm.msg_type = msg_type;

	send_notification_full(tcp, &nm);
}

void
send_notification_rtlvs(struct nbr *nbr, uint32_t status_code, uint32_t msg_id,
    uint16_t msg_type, uint16_t tlv_type, uint16_t tlv_len, char *tlv_data)
{
	struct notify_msg	 nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = status_code;
	nm.msg_id = msg_id;
	nm.msg_type = msg_type;
	/* do not append the given TLV if it's too big (shouldn't happen) */
	if (tlv_len < 1024) {
		nm.rtlvs.type = tlv_type;
		nm.rtlvs.length = tlv_len;
		nm.rtlvs.data = tlv_data;
		nm.flags |= F_NOTIF_RETURNED_TLVS;
	}

	send_notification_full(nbr->tcp, &nm);
}

int
recv_notification(struct nbr *nbr, char *buf, uint16_t len)
{
	struct ldp_msg		msg;
	struct status_tlv	st;
	struct notify_msg	nm;
	int			tlen;

	memcpy(&msg, buf, sizeof(msg));
	buf += LDP_MSG_SIZE;
	len -= LDP_MSG_SIZE;

	if (len < STATUS_SIZE) {
		session_shutdown(nbr, S_BAD_MSG_LEN, msg.id, msg.type);
		return (-1);
	}
	memcpy(&st, buf, sizeof(st));

	if (ntohs(st.length) > STATUS_SIZE - TLV_HDR_SIZE ||
	    ntohs(st.length) > len - TLV_HDR_SIZE) {
		session_shutdown(nbr, S_BAD_TLV_LEN, msg.id, msg.type);
		return (-1);
	}
	buf += STATUS_SIZE;
	len -= STATUS_SIZE;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = ntohl(st.status_code);

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
		case TLV_TYPE_EXTSTATUS:
		case TLV_TYPE_RETURNEDPDU:
		case TLV_TYPE_RETURNEDMSG:
			/* TODO is there any use for this? */
			break;
		case TLV_TYPE_PW_STATUS:
			if (tlv_len != 4) {
				session_shutdown(nbr, S_BAD_TLV_LEN,
				    msg.id, msg.type);
				return (-1);
			}

			nm.pw_status = ntohl(*(uint32_t *)buf);
			nm.flags |= F_NOTIF_PW_STATUS;
			break;
		case TLV_TYPE_FEC:
			if ((tlen = tlv_decode_fec_elm(nbr, &msg, buf,
			    tlv_len, &nm.fec)) == -1)
				return (-1);
			/* allow only one fec element */
			if (tlen != tlv_len) {
				session_shutdown(nbr, S_BAD_TLV_VAL,
				    msg.id, msg.type);
				return (-1);
			}
			nm.flags |= F_NOTIF_FEC;
			break;
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

	/* sanity checks */
	switch (nm.status_code) {
	case S_PW_STATUS:
		if (!(nm.flags & (F_NOTIF_PW_STATUS|F_NOTIF_FEC))) {
			send_notification(nbr->tcp, S_MISS_MSG,
			    msg.id, msg.type);
			return (-1);
		}

		switch (nm.fec.type) {
		case MAP_TYPE_PWID:
			break;
		default:
			send_notification(nbr->tcp, S_BAD_TLV_VAL,
			    msg.id, msg.type);
			return (-1);
		}
		break;
	case S_ENDOFLIB:
		if (!(nm.flags & F_NOTIF_FEC)) {
			send_notification(nbr->tcp, S_MISS_MSG,
			    msg.id, msg.type);
			return (-1);
		}
		if (nm.fec.type != MAP_TYPE_TYPED_WCARD) {
			send_notification(nbr->tcp, S_BAD_TLV_VAL,
			    msg.id, msg.type);
			return (-1);
		}
		break;
	default:
		break;
	}

	log_msg_notification(0, nbr, &nm);

	if (st.status_code & htonl(STATUS_FATAL)) {
		if (nbr->state == NBR_STA_OPENSENT)
			nbr_start_idtimer(nbr);

		nbr_fsm(nbr, NBR_EVT_CLOSE_SESSION);
		return (-1);
	}

	/* lde needs to know about a few notification messages */
	switch (nm.status_code) {
	case S_PW_STATUS:
	case S_ENDOFLIB:
		ldpe_imsg_compose_lde(IMSG_NOTIFICATION, nbr->peerid, 0,
		    &nm, sizeof(nm));
		break;
	default:
		break;
	}

	return (0);
}

int
gen_status_tlv(struct ibuf *buf, uint32_t status_code, uint32_t msg_id,
    uint16_t msg_type)
{
	struct status_tlv	st;

	memset(&st, 0, sizeof(st));
	st.type = htons(TLV_TYPE_STATUS);
	st.length = htons(STATUS_TLV_LEN);
	st.status_code = htonl(status_code);
	/*
	 * For convenience, msg_id and msg_type are already in network
	 * byte order.
	 */
	st.msg_id = msg_id;
	st.msg_type = msg_type;

	return (ibuf_add(buf, &st, STATUS_SIZE));
}

static int
gen_returned_tlvs(struct ibuf *buf, uint16_t type, uint16_t length,
    char *tlv_data)
{
	struct tlv	 rtlvs;
	struct tlv	 tlv;
	int		 err;

	rtlvs.type = htons(TLV_TYPE_RETURNED_TLVS);
	rtlvs.length = htons(length + TLV_HDR_SIZE);
	tlv.type = htons(type);
	tlv.length = htons(length);

	err = ibuf_add(buf, &rtlvs, sizeof(rtlvs));
	err |= ibuf_add(buf, &tlv, sizeof(tlv));
	err |= ibuf_add(buf, tlv_data, length);

	return (err);
}

void
log_msg_notification(int out, struct nbr *nbr, struct notify_msg *nm)
{
	const char	*dir = (out) ? "out" : "in";

	if (nm->status_code & STATUS_FATAL) {
		log_warnx("msg-%s: notification: lsr-id %s, status %s "
		    "(fatal error)", dir, inet_ntoa(nbr->id),
		    status_code_name(nm->status_code));
		return;
	}

	log_debug("msg-%s: notification: lsr-id %s, status %s", dir,
	    inet_ntoa(nbr->id), status_code_name(nm->status_code));
	if (nm->flags & F_NOTIF_FEC)
		log_debug("msg-%s: notification:   fec %s", dir,
		    log_map(&nm->fec));
	if (nm->flags & F_NOTIF_PW_STATUS)
		log_debug("msg-%s: notification:   pw-status %s", dir,
		    (nm->pw_status) ? "not forwarding" : "forwarding");
}
