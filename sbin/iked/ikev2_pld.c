/*	$OpenBSD: ikev2_pld.c,v 1.136 2024/07/13 12:22:46 yasuoka Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2014 Hans-Joerg Hoexer
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

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <endian.h>
#include <errno.h>
#include <err.h>
#include <event.h>

#include <openssl/sha.h>
#include <openssl/evp.h>

#include "iked.h"
#include "ikev2.h"
#include "eap.h"
#include "dh.h"

int	 ikev2_validate_pld(struct iked_message *, size_t, size_t,
	    struct ikev2_payload *);
int	 ikev2_pld_payloads(struct iked *, struct iked_message *,
	    size_t, size_t, unsigned int);
int	 ikev2_validate_sa(struct iked_message *, size_t, size_t,
	    struct ikev2_sa_proposal *);
int	 ikev2_pld_sa(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_xform(struct iked_message *, size_t, size_t,
	    struct ikev2_transform *);
int	 ikev2_pld_xform(struct iked *, struct iked_message *,
	    size_t, size_t);
int	 ikev2_validate_attr(struct iked_message *, size_t, size_t,
	    struct ikev2_attribute *);
int	 ikev2_pld_attr(struct iked *, struct ikev2_transform *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_ke(struct iked_message *, size_t, size_t,
	    struct ikev2_keyexchange *);
int	 ikev2_pld_ke(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_id(struct iked_message *, size_t, size_t,
	    struct ikev2_id *);
int	 ikev2_pld_id(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t, unsigned int);
int	 ikev2_validate_cert(struct iked_message *, size_t, size_t,
	    struct ikev2_cert *);
int	 ikev2_pld_cert(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_certreq(struct iked_message *, size_t, size_t,
	    struct ikev2_cert *);
int	 ikev2_pld_certreq(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_pld_nonce(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_notify(struct iked_message *, size_t, size_t,
	    struct ikev2_notify *);
int	 ikev2_pld_notify(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_delete(struct iked_message *, size_t, size_t,
	    struct ikev2_delete *);
int	 ikev2_pld_delete(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_tss(struct iked_message *, size_t, size_t,
	    struct ikev2_tsp *);
int	 ikev2_pld_tss(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_ts(struct iked_message *, size_t, size_t,
	    struct ikev2_ts *);
int	 ikev2_pld_ts(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t, unsigned int);
int	 ikev2_validate_auth(struct iked_message *, size_t, size_t,
	    struct ikev2_auth *);
int	 ikev2_pld_auth(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_pld_e(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_pld_ef(struct iked *env, struct ikev2_payload *pld,
	    struct iked_message *msg, size_t offset, size_t left);
int	 ikev2_frags_reassemble(struct iked *env,
	    struct ikev2_payload *pld, struct iked_message *msg);
int	 ikev2_validate_cp(struct iked_message *, size_t, size_t,
	    struct ikev2_cp *);
int	 ikev2_pld_cp(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);
int	 ikev2_validate_eap(struct iked_message *, size_t, size_t,
	    struct eap_header *);
int	 ikev2_pld_eap(struct iked *, struct ikev2_payload *,
	    struct iked_message *, size_t, size_t);

int
ikev2_pld_parse(struct iked *env, struct ike_header *hdr,
    struct iked_message *msg, size_t offset)
{
	log_debug("%s: header ispi %s rspi %s"
	    " nextpayload %s version 0x%02x exchange %s flags 0x%02x"
	    " msgid %d length %u response %d", __func__,
	    print_spi(betoh64(hdr->ike_ispi), 8),
	    print_spi(betoh64(hdr->ike_rspi), 8),
	    print_map(hdr->ike_nextpayload, ikev2_payload_map),
	    hdr->ike_version,
	    print_map(hdr->ike_exchange, ikev2_exchange_map),
	    hdr->ike_flags,
	    betoh32(hdr->ike_msgid),
	    betoh32(hdr->ike_length),
	    msg->msg_response);

	if (ibuf_size(msg->msg_data) < betoh32(hdr->ike_length)) {
		log_debug("%s: short message", __func__);
		return (-1);
	}

	offset += sizeof(*hdr);

	return (ikev2_pld_payloads(env, msg, offset,
	    betoh32(hdr->ike_length), hdr->ike_nextpayload));
}

int
ikev2_validate_pld(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_payload *pld)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);
	size_t		 pld_length;

	/* We need at least the generic header. */
	if (left < sizeof(*pld)) {
		log_debug("%s: malformed payload: too short for generic "
		    "header (%zu < %zu)", __func__, left, sizeof(*pld));
		return (-1);
	}
	memcpy(pld, msgbuf + offset, sizeof(*pld));

	/*
	 * We need at least the specified number of bytes.
	 * pld_length is the full size of the payload including
	 * the generic payload header.
	 */
	pld_length = betoh16(pld->pld_length);
	if (left < pld_length) {
		log_debug("%s: malformed payload: shorter than specified "
		    "(%zu < %zu)", __func__, left, pld_length);
		return (-1);
	}
	/*
	 * Sanity check the specified payload size, it must
	 * be at least the size of the generic payload header.
	 */
	if (pld_length < sizeof(*pld)) {
		log_debug("%s: malformed payload: shorter than minimum "
		    "header size (%zu < %zu)", __func__, pld_length,
		    sizeof(*pld));
		return (-1);
	}

	return (0);
}

int
ikev2_pld_payloads(struct iked *env, struct iked_message *msg,
    size_t offset, size_t length, unsigned int payload)
{
	struct ikev2_payload	 pld;
	unsigned int		 e;
	int			 ret;
	uint8_t			*msgbuf = ibuf_data(msg->msg_data);
	size_t			 total, left;

	/* Check if message was decrypted in an E payload */
	e = msg->msg_e ? IKED_E : 0;

	/* Bytes left in datagram. */
	total = length - offset;

	while (payload != 0 && offset < length) {
		if (ikev2_validate_pld(msg, offset, total, &pld))
			return (-1);

		log_debug("%s: %spayload %s"
		    " nextpayload %s critical 0x%02x length %d",
		    __func__, e ? "decrypted " : "",
		    print_map(payload, ikev2_payload_map),
		    print_map(pld.pld_nextpayload, ikev2_payload_map),
		    pld.pld_reserved & IKEV2_CRITICAL_PAYLOAD,
		    betoh16(pld.pld_length));

		/* Skip over generic payload header. */
		offset += sizeof(pld);
		total -= sizeof(pld);
		left = betoh16(pld.pld_length) - sizeof(pld);
		ret = 0;

		switch (payload | e) {
		case IKEV2_PAYLOAD_SA:
		case IKEV2_PAYLOAD_SA | IKED_E:
			ret = ikev2_pld_sa(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_KE:
		case IKEV2_PAYLOAD_KE | IKED_E:
			ret = ikev2_pld_ke(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_IDi | IKED_E:
		case IKEV2_PAYLOAD_IDr | IKED_E:
			ret = ikev2_pld_id(env, &pld, msg, offset, left,
			    payload);
			break;
		case IKEV2_PAYLOAD_CERT | IKED_E:
			ret = ikev2_pld_cert(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_CERTREQ:
		case IKEV2_PAYLOAD_CERTREQ | IKED_E:
			ret = ikev2_pld_certreq(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_AUTH | IKED_E:
			ret = ikev2_pld_auth(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_NONCE:
		case IKEV2_PAYLOAD_NONCE | IKED_E:
			ret = ikev2_pld_nonce(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_NOTIFY:
		case IKEV2_PAYLOAD_NOTIFY | IKED_E:
			ret = ikev2_pld_notify(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_DELETE | IKED_E:
			ret = ikev2_pld_delete(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_TSi | IKED_E:
		case IKEV2_PAYLOAD_TSr | IKED_E:
			ret = ikev2_pld_tss(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_SK:
			ret = ikev2_pld_e(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_SKF:
			ret = ikev2_pld_ef(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_CP | IKED_E:
			ret = ikev2_pld_cp(env, &pld, msg, offset, left);
			break;
		case IKEV2_PAYLOAD_EAP | IKED_E:
			ret = ikev2_pld_eap(env, &pld, msg, offset, left);
			break;
		default:
			print_hex(msgbuf, offset,
			    betoh16(pld.pld_length) - sizeof(pld));
			break;
		}

		if (ret != 0 && ikev2_msg_frompeer(msg)) {
			(void)ikev2_send_informational(env, msg);
			return (-1);
		}

		/* Encrypted payloads must appear last */
		if ((payload == IKEV2_PAYLOAD_SK) ||
		    (payload == IKEV2_PAYLOAD_SKF))
			return (0);

		payload = pld.pld_nextpayload;
		offset += left;
		total -= left;
	}

	return (0);
}

int
ikev2_validate_sa(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_sa_proposal *sap)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);
	size_t		 sap_length;

	if (left < sizeof(*sap)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*sap));
		return (-1);
	}
	memcpy(sap, msgbuf + offset, sizeof(*sap));

	sap_length = betoh16(sap->sap_length);
	if (sap_length < sizeof(*sap)) {
		log_debug("%s: malformed payload: shorter than minimum header "
		    "size (%zu < %zu)", __func__, sap_length, sizeof(*sap));
		return (-1);
	}
	if (left < sap_length) {
		log_debug("%s: malformed payload: too long for actual payload "
		    "size (%zu < %zu)", __func__, left, sap_length);
		return (-1);
	}
	/*
	 * If there is only one proposal, sap_length must be the
	 * total payload size.
	 */
	if (!sap->sap_more && left != sap_length) {
		log_debug("%s: malformed payload: SA payload length mismatches "
		    "single proposal substructure length (%zu != %zu)",
		    __func__, left, sap_length);
		return (-1);
	}
	/*
	 * If there are more than one proposal, there must be bytes
	 * left in the payload.
	 */
	if (sap->sap_more && left <= sap_length) {
		log_debug("%s: malformed payload: SA payload too small for "
		    "further proposals (%zu <= %zu)", __func__,
		    left, sap_length);
		return (-1);
	}
	return (0);
}

int
ikev2_pld_sa(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_sa_proposal	 sap;
	struct iked_proposal		*prop = NULL;
	uint32_t			 spi32;
	uint64_t			 spi = 0, spi64;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);
	int				 r;
	struct iked_proposals		*props;
	size_t				 total;

	do {
		if (ikev2_validate_sa(msg, offset, left, &sap))
			return (-1);

		/* Assumed size of the first proposals, including SPI if present. */
		total = (betoh16(sap.sap_length) - sizeof(sap));

		props = &msg->msg_parent->msg_proposals;

		offset += sizeof(sap);
		left -= sizeof(sap);

		if (sap.sap_spisize) {
			if (left < sap.sap_spisize) {
				log_debug("%s: malformed payload: SPI larger than "
				    "actual payload (%zu < %d)", __func__, left,
				    sap.sap_spisize);
				return (-1);
			}
			if (total < sap.sap_spisize) {
				log_debug("%s: malformed payload: SPI larger than "
				    "proposal (%zu < %d)", __func__, total,
				    sap.sap_spisize);
				return (-1);
			}
			switch (sap.sap_spisize) {
			case 4:
				memcpy(&spi32, msgbuf + offset, 4);
				spi = betoh32(spi32);
				break;
			case 8:
				memcpy(&spi64, msgbuf + offset, 8);
				spi = betoh64(spi64);
				break;
			default:
				log_debug("%s: unsupported SPI size %d",
				    __func__, sap.sap_spisize);
				return (-1);
			}

			offset += sap.sap_spisize;
			left -= sap.sap_spisize;

			/* Assumed size of the proposal, now without SPI. */
			total -= sap.sap_spisize;
		}

		/*
		 * As we verified sanity of packet headers, this check will
		 * be always false, but just to be sure we keep it.
		 */
		if (left < total) {
			log_debug("%s: malformed payload: too long for payload "
			    "(%zu < %zu)", __func__, left, total);
			return (-1);
		}

		log_debug("%s: more %d reserved %d length %d"
		    " proposal #%d protoid %s spisize %d xforms %d spi %s",
		    __func__, sap.sap_more, sap.sap_reserved,
		    betoh16(sap.sap_length), sap.sap_proposalnr,
		    print_map(sap.sap_protoid, ikev2_saproto_map), sap.sap_spisize,
		    sap.sap_transforms, print_spi(spi, sap.sap_spisize));

		if (ikev2_msg_frompeer(msg)) {
			if ((msg->msg_parent->msg_prop = config_add_proposal(props,
			    sap.sap_proposalnr, sap.sap_protoid)) == NULL) {
				log_debug("%s: invalid proposal", __func__);
				return (-1);
			}
			prop = msg->msg_parent->msg_prop;
			prop->prop_peerspi.spi = spi;
			prop->prop_peerspi.spi_protoid = sap.sap_protoid;
			prop->prop_peerspi.spi_size = sap.sap_spisize;

			prop->prop_localspi.spi_protoid = sap.sap_protoid;
			prop->prop_localspi.spi_size = sap.sap_spisize;
		}

		/*
		 * Parse the attached transforms
		 */
		if (sap.sap_transforms) {
			r = ikev2_pld_xform(env, msg, offset, total);
			if ((r == -2) && ikev2_msg_frompeer(msg)) {
				log_debug("%s: invalid proposal transform",
				    __func__);

				/* cleanup and ignore proposal */
				config_free_proposal(props, prop);
				prop = msg->msg_parent->msg_prop = NULL;
			} else if (r != 0) {
				log_debug("%s: invalid proposal transforms",
				    __func__);
				return (-1);
			}
		}

		offset += total;
		left -= total;
	} while (sap.sap_more);

	return (0);
}

int
ikev2_validate_xform(struct iked_message *msg, size_t offset, size_t total,
    struct ikev2_transform *xfrm)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);
	size_t		 xfrm_length;

	if (total < sizeof(*xfrm)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, total, sizeof(*xfrm));
		return (-1);
	}
	memcpy(xfrm, msgbuf + offset, sizeof(*xfrm));

	xfrm_length = betoh16(xfrm->xfrm_length);
	if (xfrm_length < sizeof(*xfrm)) {
		log_debug("%s: malformed payload: shorter than minimum header "
		    "size (%zu < %zu)", __func__, xfrm_length, sizeof(*xfrm));
		return (-1);
	}
	if (total < xfrm_length) {
		log_debug("%s: malformed payload: too long for payload size "
		    "(%zu < %zu)", __func__, total, xfrm_length);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_xform(struct iked *env, struct iked_message *msg,
    size_t offset, size_t total)
{
	struct ikev2_transform		 xfrm;
	char				 id[BUFSIZ];
	int				 ret = 0;
	int				 r;
	size_t				 xfrm_length;

	if (ikev2_validate_xform(msg, offset, total, &xfrm))
		return (-1);

	xfrm_length = betoh16(xfrm.xfrm_length);

	switch (xfrm.xfrm_type) {
	case IKEV2_XFORMTYPE_ENCR:
		strlcpy(id, print_map(betoh16(xfrm.xfrm_id),
		    ikev2_xformencr_map), sizeof(id));
		break;
	case IKEV2_XFORMTYPE_PRF:
		strlcpy(id, print_map(betoh16(xfrm.xfrm_id),
		    ikev2_xformprf_map), sizeof(id));
		break;
	case IKEV2_XFORMTYPE_INTEGR:
		strlcpy(id, print_map(betoh16(xfrm.xfrm_id),
		    ikev2_xformauth_map), sizeof(id));
		break;
	case IKEV2_XFORMTYPE_DH:
		strlcpy(id, print_map(betoh16(xfrm.xfrm_id),
		    ikev2_xformdh_map), sizeof(id));
		break;
	case IKEV2_XFORMTYPE_ESN:
		strlcpy(id, print_map(betoh16(xfrm.xfrm_id),
		    ikev2_xformesn_map), sizeof(id));
		break;
	default:
		snprintf(id, sizeof(id), "<%d>", betoh16(xfrm.xfrm_id));
		break;
	}

	log_debug("%s: more %d reserved %d length %zu"
	    " type %s id %s",
	    __func__, xfrm.xfrm_more, xfrm.xfrm_reserved, xfrm_length,
	    print_map(xfrm.xfrm_type, ikev2_xformtype_map), id);

	/*
	 * Parse transform attributes, if available
	 */
	msg->msg_attrlength = 0;
	if (xfrm_length > sizeof(xfrm)) {
		if (ikev2_pld_attr(env, &xfrm, msg, offset + sizeof(xfrm),
		    xfrm_length - sizeof(xfrm)) != 0) {
			return (-1);
		}
	}

	if (ikev2_msg_frompeer(msg)) {
		r = config_add_transform(msg->msg_parent->msg_prop,
		    xfrm.xfrm_type, betoh16(xfrm.xfrm_id),
		    msg->msg_attrlength, msg->msg_attrlength);
		if (r == -1) {
			log_debug("%s: failed to add transform: alloc error",
			    __func__);
			return (r);
		} else if (r == -2) {
			log_debug("%s: failed to add transform: unknown type",
			    __func__);
			return (r);
		}
	}

	/* Next transform */
	offset += xfrm_length;
	total -= xfrm_length;
	if (xfrm.xfrm_more == IKEV2_XFORM_MORE)
		ret = ikev2_pld_xform(env, msg, offset, total);
	else if (total != 0) {
		/* No more transforms but still some data left. */
		log_debug("%s: less data than specified, %zu bytes left",
		    __func__, total);
		ret = -1;
	}

	return (ret);
}

int
ikev2_validate_attr(struct iked_message *msg, size_t offset, size_t total,
    struct ikev2_attribute *attr)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (total < sizeof(*attr)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, total, sizeof(*attr));
		return (-1);
	}
	memcpy(attr, msgbuf + offset, sizeof(*attr));

	return (0);
}

int
ikev2_pld_attr(struct iked *env, struct ikev2_transform *xfrm,
    struct iked_message *msg, size_t offset, size_t total)
{
	struct ikev2_attribute		 attr;
	unsigned int			 type;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);
	int				 ret = 0;
	size_t				 attr_length;

	if (ikev2_validate_attr(msg, offset, total, &attr))
		return (-1);

	type = betoh16(attr.attr_type) & ~IKEV2_ATTRAF_TV;

	log_debug("%s: attribute type %s length %d total %zu",
	    __func__, print_map(type, ikev2_attrtype_map),
	    betoh16(attr.attr_length), total);

	if (betoh16(attr.attr_type) & IKEV2_ATTRAF_TV) {
		/* Type-Value attribute */
		offset += sizeof(attr);
		total -= sizeof(attr);

		if (type == IKEV2_ATTRTYPE_KEY_LENGTH)
			msg->msg_attrlength = betoh16(attr.attr_length);
	} else {
		/* Type-Length-Value attribute */
		attr_length = betoh16(attr.attr_length);
		if (attr_length < sizeof(attr)) {
			log_debug("%s: malformed payload: shorter than "
			    "minimum header size (%zu < %zu)", __func__,
			    attr_length, sizeof(attr));
			return (-1);
		}
		if (total < attr_length) {
			log_debug("%s: malformed payload: attribute larger "
			    "than actual payload (%zu < %zu)", __func__,
			    total, attr_length);
			return (-1);
		}
		print_hex(msgbuf, offset + sizeof(attr),
		    attr_length - sizeof(attr));
		offset += attr_length;
		total -= attr_length;
	}

	if (total > 0) {
		/* Next attribute */
		ret = ikev2_pld_attr(env, xfrm, msg, offset, total);
	}

	return (ret);
}

int
ikev2_validate_ke(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_keyexchange *kex)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*kex)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*kex));
		return (-1);
	}
	memcpy(kex, msgbuf + offset, sizeof(*kex));

	return (0);
}

int
ikev2_pld_ke(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_keyexchange	 kex;
	uint8_t				*buf;
	size_t				 len;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);

	if (ikev2_validate_ke(msg, offset, left, &kex))
		return (-1);

	log_debug("%s: dh group %s reserved %d", __func__,
	    print_map(betoh16(kex.kex_dhgroup), ikev2_xformdh_map),
	    betoh16(kex.kex_reserved));

	buf = msgbuf + offset + sizeof(kex);
	len = left - sizeof(kex);

	if (len == 0) {
		log_debug("%s: malformed payload: no KE data given", __func__);
		return (-1);
	}

	print_hex(buf, 0, len);

	if (ikev2_msg_frompeer(msg)) {
		if (msg->msg_parent->msg_ke != NULL) {
			log_info("%s: duplicate KE payload", __func__);
			return (-1);
		}
		if ((msg->msg_parent->msg_ke = ibuf_new(buf, len)) == NULL) {
			log_debug("%s: failed to get exchange", __func__);
			return (-1);
		}
		msg->msg_parent->msg_dhgroup = betoh16(kex.kex_dhgroup);
	}

	return (0);
}

int
ikev2_validate_id(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_id *id)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*id)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*id));
		return (-1);
	}
	memcpy(id, msgbuf + offset, sizeof(*id));

	if (id->id_type == IKEV2_ID_NONE) {
		log_debug("%s: malformed payload: invalid ID type.",
		    __func__);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_id(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left, unsigned int payload)
{
	uint8_t				*ptr;
	struct ikev2_id			 id;
	size_t				 len;
	struct iked_id			*idp, idb;
	const struct iked_sa		*sa = msg->msg_sa;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);
	char				 idstr[IKED_ID_SIZE];

	if (ikev2_validate_id(msg, offset, left, &id))
		return (-1);

	bzero(&idb, sizeof(idb));

	/* Don't strip the Id payload header */
	ptr = msgbuf + offset;
	len = left;

	idb.id_type = id.id_type;
	idb.id_offset = sizeof(id);
	if ((idb.id_buf = ibuf_new(ptr, len)) == NULL)
		return (-1);

	if (ikev2_print_id(&idb, idstr, sizeof(idstr)) == -1) {
		ibuf_free(idb.id_buf);
		log_debug("%s: malformed id", __func__);
		return (-1);
	}

	log_debug("%s: id %s length %zu", __func__, idstr, len);

	if (!ikev2_msg_frompeer(msg)) {
		ibuf_free(idb.id_buf);
		return (0);
	}

	if (((sa->sa_hdr.sh_initiator && payload == IKEV2_PAYLOAD_IDr) ||
	    (!sa->sa_hdr.sh_initiator && payload == IKEV2_PAYLOAD_IDi)))
		idp = &msg->msg_parent->msg_peerid;
	else if (!sa->sa_hdr.sh_initiator && payload == IKEV2_PAYLOAD_IDr)
		idp = &msg->msg_parent->msg_localid;
	else {
		ibuf_free(idb.id_buf);
		log_debug("%s: unexpected id payload", __func__);
		return (0);
	}

	if (idp->id_type) {
		ibuf_free(idb.id_buf);
		log_debug("%s: duplicate id payload", __func__);
		return (-1);
	}

	idp->id_buf = idb.id_buf;
	idp->id_offset = idb.id_offset;
	idp->id_type = idb.id_type;

	return (0);
}

int
ikev2_validate_cert(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_cert *cert)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*cert)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*cert));
		return (-1);
	}
	memcpy(cert, msgbuf + offset, sizeof(*cert));
	if (cert->cert_type == IKEV2_CERT_NONE) {
		log_debug("%s: malformed payload: invalid cert type", __func__);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_cert(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_cert		 cert;
	uint8_t				*buf;
	size_t				 len;
	struct iked_id			*certid;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);
	const struct iked_sa		*sa = msg->msg_sa;
	int				 i;

	if (ikev2_validate_cert(msg, offset, left, &cert))
		return (-1);
	offset += sizeof(cert);

	buf = msgbuf + offset;
	len = left - sizeof(cert);

	log_debug("%s: type %s length %zu",
	    __func__, print_map(cert.cert_type, ikev2_cert_map), len);

	print_hex(buf, 0, len);

	if (!ikev2_msg_frompeer(msg))
		return (0);

	/* do not accept internal encoding in the wire */
	if (cert.cert_type == IKEV2_CERT_BUNDLE) {
		log_debug("%s: ignoring IKEV2_CERT_BUNDLE",
		   SPI_SA(sa, __func__));
		return (0);
	}

	certid = &msg->msg_parent->msg_cert;
	if (certid->id_type) {
		/* try to set supplemental certs */
		for (i = 0; i < IKED_SCERT_MAX; i++) {
			certid = &msg->msg_parent->msg_scert[i];
			if (!certid->id_type)
				break;
		}
		if (certid->id_type) {
			log_debug("%s: too many cert payloads, ignoring",
			   SPI_SA(sa, __func__));
			return (0);
		}
	}

	if ((certid->id_buf = ibuf_new(buf, len)) == NULL) {
		log_debug("%s: failed to save cert", __func__);
		return (-1);
	}
	certid->id_type = cert.cert_type;
	certid->id_offset = 0;

	return (0);
}

int
ikev2_validate_certreq(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_cert *cert)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*cert)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*cert));
		return (-1);
	}
	memcpy(cert, msgbuf + offset, sizeof(*cert));

	return (0);
}

int
ikev2_pld_certreq(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_cert		 cert;
	struct iked_certreq		*cr;
	uint8_t				*buf;
	ssize_t				 len;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);

	if (ikev2_validate_certreq(msg, offset, left, &cert))
		return (-1);
	offset += sizeof(cert);

	buf = msgbuf + offset;
	len = left - sizeof(cert);

	log_debug("%s: type %s length %zd",
	    __func__, print_map(cert.cert_type, ikev2_cert_map), len);

	print_hex(buf, 0, len);

	if (!ikev2_msg_frompeer(msg))
		return (0);

	if (cert.cert_type == IKEV2_CERT_X509_CERT) {
		if (len == 0) {
			log_info("%s: invalid length 0", __func__);
			return (0);
		}
		if ((len % SHA_DIGEST_LENGTH) != 0) {
			log_info("%s: invalid certificate request",
			    __func__);
			return (-1);
		}
	}

	if ((cr = calloc(1, sizeof(struct iked_certreq))) == NULL) {
		log_info("%s: failed to allocate certreq.", __func__);
		return (-1);
	}
	if ((cr->cr_data = ibuf_new(buf, len)) == NULL) {
		log_info("%s: failed to allocate buffer.", __func__);
		free(cr);
		return (-1);
	}
	cr->cr_type = cert.cert_type;
	SIMPLEQ_INSERT_TAIL(&msg->msg_parent->msg_certreqs, cr, cr_entry);

	return (0);
}

int
ikev2_validate_auth(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_auth *auth)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*auth)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*auth));
		return (-1);
	}
	memcpy(auth, msgbuf + offset, sizeof(*auth));

	if (auth->auth_method == 0) {
		log_info("%s: malformed payload: invalid auth method",
		    __func__);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_auth(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_auth		 auth;
	struct iked_id			*idp;
	uint8_t				*buf;
	size_t				 len;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);

	if (ikev2_validate_auth(msg, offset, left, &auth))
		return (-1);
	offset += sizeof(auth);

	buf = msgbuf + offset;
	len = left - sizeof(auth);

	log_debug("%s: method %s length %zu",
	    __func__, print_map(auth.auth_method, ikev2_auth_map), len);

	print_hex(buf, 0, len);

	if (!ikev2_msg_frompeer(msg))
		return (0);

	idp = &msg->msg_parent->msg_auth;
	if (idp->id_type) {
		log_debug("%s: duplicate auth payload", __func__);
		return (-1);
	}

	ibuf_free(idp->id_buf);
	idp->id_type = auth.auth_method;
	idp->id_offset = 0;
	if ((idp->id_buf = ibuf_new(buf, len)) == NULL)
		return (-1);

	return (0);
}

int
ikev2_pld_nonce(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	size_t		 len;
	uint8_t		*buf;
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	buf = msgbuf + offset;
	len = left;

	if (len == 0) {
		log_debug("%s: malformed payload: no NONCE given", __func__);
		return (-1);
	}

	print_hex(buf, 0, len);

	if (ikev2_msg_frompeer(msg)) {
		if (msg->msg_parent->msg_nonce != NULL) {
			log_info("%s: duplicate NONCE payload", __func__);
			return (-1);
		}
		if ((msg->msg_nonce = ibuf_new(buf, len)) == NULL) {
			log_debug("%s: failed to get peer nonce", __func__);
			return (-1);
		}
		msg->msg_parent->msg_nonce = msg->msg_nonce;
	}

	return (0);
}

int
ikev2_validate_notify(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_notify *n)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*n)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*n));
		return (-1);
	}
	memcpy(n, msgbuf + offset, sizeof(*n));

	return (0);
}

int
ikev2_pld_notify(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_notify	 n;
	const struct iked_sa	*sa = msg->msg_sa;
	uint8_t			*buf, md[SHA_DIGEST_LENGTH];
	uint32_t		 spi32;
	uint64_t		 spi64;
	struct iked_spi		*rekey;
	uint16_t		 type;
	uint16_t		 signature_hash;

	if (ikev2_validate_notify(msg, offset, left, &n))
		return (-1);
	type = betoh16(n.n_type);

	log_debug("%s: protoid %s spisize %d type %s",
	    __func__,
	    print_map(n.n_protoid, ikev2_saproto_map), n.n_spisize,
	    print_map(type, ikev2_n_map));

	left -= sizeof(n);
	if ((buf = ibuf_seek(msg->msg_data, offset + sizeof(n), left)) == NULL)
		return (-1);

	print_hex(buf, 0, left);

	if (!ikev2_msg_frompeer(msg))
		return (0);

	switch (type) {
	case IKEV2_N_NAT_DETECTION_SOURCE_IP:
	case IKEV2_N_NAT_DETECTION_DESTINATION_IP:
		if (left != sizeof(md)) {
			log_debug("%s: malformed payload: hash size mismatch"
			    " (%zu != %zu)", __func__, left, sizeof(md));
			return (-1);
		}
		if (ikev2_nat_detection(env, msg, md, sizeof(md), type,
		    ikev2_msg_frompeer(msg)) == -1)
			return (-1);
		if (memcmp(buf, md, left) != 0) {
			log_debug("%s: %s detected NAT", __func__,
			    print_map(type, ikev2_n_map));
			if (type == IKEV2_N_NAT_DETECTION_SOURCE_IP)
				msg->msg_parent->msg_nat_detected
				    |= IKED_MSG_NAT_SRC_IP;
			else
				msg->msg_parent->msg_nat_detected
				    |= IKED_MSG_NAT_DST_IP;
		}
		print_hex(md, 0, sizeof(md));
		/* remember for MOBIKE */
		msg->msg_parent->msg_natt_rcvd = 1;
		break;
	case IKEV2_N_AUTHENTICATION_FAILED:
		if (!msg->msg_e) {
			log_debug("%s: AUTHENTICATION_FAILED not encrypted",
			    __func__);
			return (-1);
		}
		/*
		 * If we are the responder, then we only accept
		 * AUTHENTICATION_FAILED from authenticated peers.
		 * If we are the initiator, the peer cannot be authenticated.
		 */
		if (!sa->sa_hdr.sh_initiator) {
			if (!sa_stateok(sa, IKEV2_STATE_VALID)) {
				log_debug("%s: ignoring AUTHENTICATION_FAILED"
				    " from unauthenticated initiator",
				    __func__);
				return (-1);
			}
		} else {
			if (sa_stateok(sa, IKEV2_STATE_VALID)) {
				log_debug("%s: ignoring AUTHENTICATION_FAILED"
				    " from authenticated responder",
				    __func__);
				return (-1);
			}
		}
		msg->msg_parent->msg_flags
		    |= IKED_MSG_FLAGS_AUTHENTICATION_FAILED;
		break;
	case IKEV2_N_INVALID_KE_PAYLOAD:
		if (sa_stateok(sa, IKEV2_STATE_VALID) &&
		    !msg->msg_e) {
			log_debug("%s: INVALID_KE_PAYLOAD not encrypted",
			    __func__);
			return (-1);
		}
		if (left != sizeof(msg->msg_parent->msg_group)) {
			log_debug("%s: malformed payload: group size mismatch"
			    " (%zu != %zu)", __func__, left,
			    sizeof(msg->msg_parent->msg_group));
			return (-1);
		}
		memcpy(&msg->msg_parent->msg_group, buf, left);
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_INVALID_KE;
		break;
	case IKEV2_N_NO_ADDITIONAL_SAS:
		if (!msg->msg_e) {
			log_debug("%s: NO_ADDITIONAL_SAS not encrypted",
			    __func__);
			return (-1);
		}
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_NO_ADDITIONAL_SAS;
		break;
	case IKEV2_N_REKEY_SA:
		if (!msg->msg_e) {
			log_debug("%s: N_REKEY_SA not encrypted", __func__);
			return (-1);
		}
		if (left != n.n_spisize) {
			log_debug("%s: malformed notification", __func__);
			return (-1);
		}
		rekey = &msg->msg_parent->msg_rekey;
		if (rekey->spi != 0) {
			log_debug("%s: rekeying of multiple SAs not supported",
			    __func__);
			return (-1);
		}
		switch (n.n_spisize) {
		case 4:
			memcpy(&spi32, buf, left);
			rekey->spi = betoh32(spi32);
			break;
		case 8:
			memcpy(&spi64, buf, left);
			rekey->spi = betoh64(spi64);
			break;
		default:
			log_debug("%s: invalid spi size %d", __func__,
			    n.n_spisize);
			return (-1);
		}
		rekey->spi_size = n.n_spisize;
		rekey->spi_protoid = n.n_protoid;

		log_debug("%s: rekey %s spi %s", __func__,
		    print_map(n.n_protoid, ikev2_saproto_map),
		    print_spi(rekey->spi, n.n_spisize));
		break;
	case IKEV2_N_TEMPORARY_FAILURE:
		if (!msg->msg_e) {
			log_debug("%s: IKEV2_N_TEMPORARY_FAILURE not encrypted",
			    __func__);
			return (-1);
		}
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_TEMPORARY_FAILURE;
		break;
	case IKEV2_N_IPCOMP_SUPPORTED:
		if (!msg->msg_e) {
			log_debug("%s: N_IPCOMP_SUPPORTED not encrypted",
			    __func__);
			return (-1);
		}
		if (left < sizeof(msg->msg_parent->msg_cpi) +
		    sizeof(msg->msg_parent->msg_transform)) {
			log_debug("%s: ignoring malformed ipcomp notification",
			    __func__);
			return (0);
		}
		memcpy(&msg->msg_parent->msg_cpi, buf,
		    sizeof(msg->msg_parent->msg_cpi));
		memcpy(&msg->msg_parent->msg_transform,
		    buf + sizeof(msg->msg_parent->msg_cpi),
		    sizeof(msg->msg_parent->msg_transform));

		log_debug("%s: %s cpi 0x%x, transform %s, length %zu", __func__,
		    msg->msg_parent->msg_response ? "res" : "req",
		    betoh16(msg->msg_parent->msg_cpi),
		    print_map(msg->msg_parent->msg_transform,
		    ikev2_ipcomp_map), left);

		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_IPCOMP_SUPPORTED;
		break;
	case IKEV2_N_CHILD_SA_NOT_FOUND:
		if (!msg->msg_e) {
			log_debug("%s: N_CHILD_SA_NOT_FOUND not encrypted",
			    __func__);
			return (-1);
		}
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_CHILD_SA_NOT_FOUND;
		break;
	case IKEV2_N_NO_PROPOSAL_CHOSEN:
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_NO_PROPOSAL_CHOSEN;
		break;
	case IKEV2_N_MOBIKE_SUPPORTED:
		if (!msg->msg_e) {
			log_debug("%s: N_MOBIKE_SUPPORTED not encrypted",
			    __func__);
			return (-1);
		}
		if (left != 0) {
			log_debug("%s: ignoring malformed mobike"
			    " notification: %zu", __func__, left);
			return (0);
		}
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_MOBIKE;
		break;
	case IKEV2_N_USE_TRANSPORT_MODE:
		if (!msg->msg_e) {
			log_debug("%s: N_USE_TRANSPORT_MODE not encrypted",
			    __func__);
			return (-1);
		}
		if (left != 0) {
			log_debug("%s: ignoring malformed transport mode"
			    " notification: %zu", __func__, left);
			return (0);
		}
		if (msg->msg_parent->msg_response) {
			if (!(msg->msg_policy->pol_flags & IKED_POLICY_TRANSPORT)) {
				log_debug("%s: ignoring transport mode"
				    " notification (policy)", __func__);
				return (0);
			}
		}
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_USE_TRANSPORT;
		break;
	case IKEV2_N_UPDATE_SA_ADDRESSES:
		if (!msg->msg_e) {
			log_debug("%s: N_UPDATE_SA_ADDRESSES not encrypted",
			    __func__);
			return (-1);
		}
		if (!sa->sa_mobike) {
			log_debug("%s: ignoring update sa addresses"
			    " notification w/o mobike: %zu", __func__, left);
			return (0);
		}
		if (left != 0) {
			log_debug("%s: ignoring malformed update sa addresses"
			    " notification: %zu", __func__, left);
			return (0);
		}
		msg->msg_parent->msg_update_sa_addresses = 1;
		break;
	case IKEV2_N_COOKIE2:
		if (!msg->msg_e) {
			log_debug("%s: N_COOKIE2 not encrypted",
			    __func__);
			return (-1);
		}
		if (!sa->sa_mobike) {
			log_debug("%s: ignoring cookie2 notification"
			    " w/o mobike: %zu", __func__, left);
			return (0);
		}
		if (left < IKED_COOKIE2_MIN || left > IKED_COOKIE2_MAX) {
			log_debug("%s: ignoring malformed cookie2"
			    " notification: %zu", __func__, left);
			return (0);
		}
		ibuf_free(msg->msg_cookie2);	/* should not happen */
		if ((msg->msg_cookie2 = ibuf_new(buf, left)) == NULL) {
			log_debug("%s: failed to get peer cookie2", __func__);
			return (-1);
		}
		msg->msg_parent->msg_cookie2 = msg->msg_cookie2;
		break;
	case IKEV2_N_COOKIE:
		if (msg->msg_e) {
			log_debug("%s: N_COOKIE encrypted",
			    __func__);
			return (-1);
		}
		if (left < IKED_COOKIE_MIN || left > IKED_COOKIE_MAX) {
			log_debug("%s: ignoring malformed cookie"
			    " notification: %zu", __func__, left);
			return (0);
		}
		log_debug("%s: received cookie, len %zu", __func__, left);
		print_hex(buf, 0, left);

		ibuf_free(msg->msg_cookie);
		if ((msg->msg_cookie = ibuf_new(buf, left)) == NULL) {
			log_debug("%s: failed to get peer cookie", __func__);
			return (-1);
		}
		msg->msg_parent->msg_cookie = msg->msg_cookie;
		break;
	case IKEV2_N_FRAGMENTATION_SUPPORTED:
		if (msg->msg_e) {
			log_debug("%s: N_FRAGMENTATION_SUPPORTED encrypted",
			    __func__);
			return (-1);
		}
		if (left != 0) {
			log_debug("%s: ignoring malformed fragmentation"
			    " notification: %zu", __func__, left);
			return (0);
		}
		msg->msg_parent->msg_flags |= IKED_MSG_FLAGS_FRAGMENTATION;
		break;
	case IKEV2_N_SIGNATURE_HASH_ALGORITHMS:
		if (msg->msg_e) {
			log_debug("%s: SIGNATURE_HASH_ALGORITHMS: encrypted",
			    __func__);
			return (-1);
		}
		if (sa == NULL) {
			log_debug("%s: SIGNATURE_HASH_ALGORITHMS: no SA",
			    __func__);
			return (-1);
		}
		if (sa->sa_sigsha2) {
			log_debug("%s: SIGNATURE_HASH_ALGORITHMS: "
			    "duplicate notify", __func__);
			return (0);
		}
		if (left < sizeof(signature_hash) ||
		    left % sizeof(signature_hash)) {
			log_debug("%s: malformed signature hash notification"
			    "(%zu bytes)", __func__, left);
			return (0);
		}
		while (left >= sizeof(signature_hash)) {
			memcpy(&signature_hash, buf, sizeof(signature_hash));
			signature_hash = betoh16(signature_hash);
			log_debug("%s: signature hash %s (%x)", __func__,
			    print_map(signature_hash, ikev2_sighash_map),
			    signature_hash);
			left -= sizeof(signature_hash);
			buf += sizeof(signature_hash);
			if (signature_hash == IKEV2_SIGHASH_SHA2_256)
				msg->msg_parent->msg_flags
				    |= IKED_MSG_FLAGS_SIGSHA2;
		}
		break;
	}

	return (0);
}

int
ikev2_validate_delete(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_delete *del)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*del)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*del));
		return (-1);
	}
	memcpy(del, msgbuf + offset, sizeof(*del));

	if (del->del_protoid == 0) {
		log_info("%s: malformed payload: invalid protoid", __func__);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_delete(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_delete	 del;
	uint8_t			*buf, *msgbuf = ibuf_data(msg->msg_data);
	size_t			 cnt, sz, len;

	if (ikev2_validate_delete(msg, offset, left, &del))
		return (-1);

	/* Skip if it's a response, then we don't have to deal with it */
	if (ikev2_msg_frompeer(msg) &&
	    msg->msg_parent->msg_response)
		return (0);

	cnt = betoh16(del.del_nspi);
	sz = del.del_spisize;

	log_debug("%s: proto %s spisize %zu nspi %zu",
	    __func__, print_map(del.del_protoid, ikev2_saproto_map),
	    sz, cnt);

	if (msg->msg_parent->msg_del_protoid) {
		log_debug("%s: duplicate delete payload", __func__);
		return (0);
	}

	msg->msg_parent->msg_del_protoid = del.del_protoid;
	msg->msg_parent->msg_del_cnt = cnt;
	msg->msg_parent->msg_del_spisize = sz;

	buf = msgbuf + offset + sizeof(del);
	len = left - sizeof(del);
	if (len == 0 || sz == 0 || cnt == 0)
		return (0);

	if ((len / sz) != cnt) {
		log_debug("%s: invalid payload length %zu/%zu != %zu",
		    __func__, len, sz, cnt);
		return (-1);
	}

	print_hex(buf, 0, len);

	msg->msg_parent->msg_del_buf = ibuf_new(buf, len);

	return (0);
}

int
ikev2_validate_tss(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_tsp *tsp)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*tsp)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*tsp));
		return (-1);
	}
	memcpy(tsp, msgbuf + offset, sizeof(*tsp));

	return (0);
}

int
ikev2_pld_tss(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_tsp		 tsp;
	struct ikev2_ts			 ts;
	size_t				 ts_len, i;

	if (ikev2_validate_tss(msg, offset, left, &tsp))
		return (-1);

	offset += sizeof(tsp);
	left -= sizeof(tsp);

	log_debug("%s: count %d length %zu", __func__,
	    tsp.tsp_count, left);

	for (i = 0; i < tsp.tsp_count; i++) {
		if (ikev2_validate_ts(msg, offset, left, &ts))
			return (-1);

		log_debug("%s: type %s protoid %u length %d "
		    "startport %u endport %u", __func__,
		    print_map(ts.ts_type, ikev2_ts_map),
		    ts.ts_protoid, betoh16(ts.ts_length),
		    betoh16(ts.ts_startport),
		    betoh16(ts.ts_endport));

		offset += sizeof(ts);
		left -= sizeof(ts);

		ts_len = betoh16(ts.ts_length) - sizeof(ts);
		if (ikev2_pld_ts(env, pld, msg, offset, ts_len, ts.ts_type))
			return (-1);

		offset += ts_len;
		left -= ts_len;
	}

	return (0);
}

int
ikev2_validate_ts(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_ts *ts)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);
	size_t		 ts_length;

	if (left < sizeof(*ts)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*ts));
		return (-1);
	}
	memcpy(ts, msgbuf + offset, sizeof(*ts));

	ts_length = betoh16(ts->ts_length);
	if (ts_length < sizeof(*ts)) {
		log_debug("%s: malformed payload: shorter than minimum header "
		    "size (%zu < %zu)", __func__, ts_length, sizeof(*ts));
		return (-1);
	}
	if (left < ts_length) {
		log_debug("%s: malformed payload: too long for payload size "
		    "(%zu < %zu)", __func__, left, ts_length);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_ts(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left, unsigned int type)
{
	struct sockaddr_in		 start4, end4;
	struct sockaddr_in6		 start6, end6;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);
	uint8_t				*ptr;

	ptr = msgbuf + offset;

	switch (type) {
	case IKEV2_TS_IPV4_ADDR_RANGE:
		if (left < 2 * 4) {
			log_debug("%s: malformed payload: too short "
			    "for ipv4 addr range (%zu < %u)",
			    __func__, left, 2 * 4);
			return (-1);
		}

		bzero(&start4, sizeof(start4));
		start4.sin_family = AF_INET;
		start4.sin_len = sizeof(start4);
		memcpy(&start4.sin_addr.s_addr, ptr, 4);
		ptr += 4;
		left -= 4;

		bzero(&end4, sizeof(end4));
		end4.sin_family = AF_INET;
		end4.sin_len = sizeof(end4);
		memcpy(&end4.sin_addr.s_addr, ptr, 4);
		left -= 4;

		log_debug("%s: start %s end %s", __func__,
		    print_addr(&start4), print_addr(&end4));
		break;
	case IKEV2_TS_IPV6_ADDR_RANGE:
		if (left < 2 * 16) {
			log_debug("%s: malformed payload: too short "
			    "for ipv6 addr range (%zu < %u)",
			    __func__, left, 2 * 16);
			return (-1);
		}
		bzero(&start6, sizeof(start6));
		start6.sin6_family = AF_INET6;
		start6.sin6_len = sizeof(start6);
		memcpy(&start6.sin6_addr, ptr, 16);
		ptr += 16;
		left -= 16;

		bzero(&end6, sizeof(end6));
		end6.sin6_family = AF_INET6;
		end6.sin6_len = sizeof(end6);
		memcpy(&end6.sin6_addr, ptr, 16);
		left -= 16;

		log_debug("%s: start %s end %s", __func__,
		    print_addr(&start6), print_addr(&end6));
		break;
	default:
		log_debug("%s: ignoring unknown TS type %u", __func__, type);
		return (0);
	}

	if (left > 0) {
		log_debug("%s: malformed payload: left (%zu) > 0",
		    __func__, left);
		return (-1);
	}

	return (0);
}

int
ikev2_pld_ef(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct iked_sa			*sa = msg->msg_sa;
	struct iked_frag		*sa_frag = &sa->sa_fragments;
	struct iked_frag_entry		*el;
	struct ikev2_frag_payload	 frag;
	uint8_t				*msgbuf = ibuf_data(msg->msg_data);
	uint8_t				*buf;
	struct ibuf			*e = NULL;
	size_t				 frag_num, frag_total;
	size_t				 len;
	int				 ret = -1;
	int				 processed = 0;
	ssize_t				 elen;

	buf = msgbuf + offset;
	memcpy(&frag, buf, sizeof(frag));
	frag_num = betoh16(frag.frag_num);
	frag_total = betoh16(frag.frag_total);

	offset += sizeof(frag);
	buf = msgbuf + offset;
	len = left - sizeof(frag);

	ikestat_inc(env, ikes_frag_rcvd);

	/* Limit number of total fragments to avoid DOS */
	if (frag_total > IKED_FRAG_TOTAL_MAX ) {
		log_debug("%s: Total Fragments too big  %zu",
		    __func__, frag_total);
		goto dropall;
	}

	/* Check sanity of fragment header */
	if (frag_num == 0 || frag_total == 0) {
		log_debug("%s: Malformed fragment received: %zu of %zu",
		    __func__, frag_num, frag_total);
		goto done;
	}
	log_debug("%s: Received fragment: %zu of %zu",
	    __func__, frag_num, frag_total);

	/* Drop fragment if frag_num and frag_total don't match */
	if (frag_num > frag_total)
		goto done;

	/* Decrypt fragment */
	if ((e = ibuf_new(buf, len)) == NULL)
		goto done;

	if ((e = ikev2_msg_decrypt(env, msg->msg_sa, msg->msg_data, e))
	    == NULL ) {
		log_debug("%s: Failed to decrypt fragment: %zu of %zu",
		    __func__, frag_num, frag_total);
		goto done;
	}
	elen = ibuf_size(e);

	/* Check new fragmented message */
	if (sa_frag->frag_arr == NULL) {
		sa_frag->frag_arr = recallocarray(NULL, 0, frag_total,
		    sizeof(struct iked_frag_entry*));
		if (sa_frag->frag_arr == NULL) {
			log_info("%s: recallocarray sa_frag->frag_arr.", __func__);
			goto done;
		}
		sa_frag->frag_total = frag_total;
	} else {
		/* Drop all fragments if frag_total doesn't match previous */
		if (frag_total != sa_frag->frag_total)
			goto dropall;

		/* Silent drop if fragment already stored */
		if (sa_frag->frag_arr[frag_num-1] != NULL)
			goto done;
	}

	/* The first fragments IKE header determines pld_nextpayload */
	if (frag_num == 1)
		sa_frag->frag_nextpayload = pld->pld_nextpayload;

	/* Insert new list element */
	el = calloc(1, sizeof(struct iked_frag_entry));
	if (el == NULL) {
		log_info("%s: Failed allocating new fragment: %zu of %zu",
		    __func__, frag_num, frag_total);
		goto done;
	}

	sa_frag->frag_arr[frag_num-1] = el;
	el->frag_size = elen;
	el->frag_data = calloc(1, elen);
	if (el->frag_data == NULL) {
		log_debug("%s: Failed allocating new fragment data: %zu of %zu",
		    __func__, frag_num, frag_total);
		goto done;
	}

	/* Copy plaintext to fragment */
	memcpy(el->frag_data, ibuf_seek(e, 0, 0), elen);
	sa_frag->frag_total_size += elen;
	sa_frag->frag_count++;

	/* If all frags are received start reassembly */
	if (sa_frag->frag_count == sa_frag->frag_total) {
		log_debug("%s: All fragments received: %zu of %zu",
		    __func__, frag_num, frag_total);
		ret = ikev2_frags_reassemble(env, pld, msg);
	} else {
		ret = 0;
	}
	processed = 1;

done:
	if (!processed)
		ikestat_inc(env, ikes_frag_rcvd_drop);
	ibuf_free(e);
	return (ret);
dropall:
	ikestat_add(env, ikes_frag_rcvd_drop, sa_frag->frag_count + 1);
	config_free_fragments(sa_frag);
	ibuf_free(e);
	return -1;
}

int
ikev2_frags_reassemble(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg)
{
	struct iked_frag		*sa_frag = &msg->msg_sa->sa_fragments;
	struct ibuf			*e = NULL;
	struct iked_frag_entry		*el;
	uint8_t				*ptr;
	size_t				 offset;
	size_t				 i;
	struct iked_message		 emsg;
	int				 ret = -1;
	int				 processed = 0;

	/* Reassemble fragments to single buffer */
	if ((e = ibuf_new(NULL, sa_frag->frag_total_size)) == NULL) {
		log_debug("%s: Failed allocating SK buffer.", __func__);
		goto done;
	}

	/* Empty queue to new buffer */
	offset = 0;
	for (i = 0; i < sa_frag->frag_total; i++) {
		if ((el = sa_frag->frag_arr[i]) == NULL)
			fatalx("Tried to reassemble shallow frag_arr");
		ptr = ibuf_seek(e, offset, el->frag_size);
		if (ptr == NULL) {
			log_info("%s: failed to reassemble fragments", __func__);
			goto done;
		}
		memcpy(ptr, el->frag_data, el->frag_size);
		offset += el->frag_size;
	}

	log_debug("%s: Defragmented length %zd", __func__,
	    sa_frag->frag_total_size);
	print_hex(ibuf_data(e), 0,  sa_frag->frag_total_size);

	/* Drop the original request's packets from the retransmit queue */
	if (msg->msg_response)
		ikev2_msg_dispose(env, &msg->msg_sa->sa_requests,
		    ikev2_msg_lookup(env, &msg->msg_sa->sa_requests, msg,
		    msg->msg_exchange));

	/*
	 * Parse decrypted payload
	 */
	bzero(&emsg, sizeof(emsg));
	memcpy(&emsg, msg, sizeof(*msg));
	emsg.msg_data = e;
	emsg.msg_e = 1;
	emsg.msg_parent = msg;
	TAILQ_INIT(&emsg.msg_proposals);

	ret = ikev2_pld_payloads(env, &emsg, 0, ibuf_size(e),
	    sa_frag->frag_nextpayload);
	processed = 1;
done:
	if (processed)
		ikestat_add(env, ikes_frag_reass_ok, sa_frag->frag_total);
	else
		ikestat_add(env, ikes_frag_reass_drop, sa_frag->frag_total);
	config_free_fragments(sa_frag);
	ibuf_free(e);

	return (ret);
}

int
ikev2_pld_e(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct iked_sa		*sa = msg->msg_sa;
	struct ibuf		*e = NULL;
	uint8_t			*msgbuf = ibuf_data(msg->msg_data);
	struct iked_message	 emsg;
	uint8_t			*buf;
	size_t			 len;
	int			 ret = -1;

	if (sa->sa_fragments.frag_arr != NULL) {
		log_warn("%s: Received SK payload when SKFs are in queue.",
		    __func__);
		config_free_fragments(&sa->sa_fragments);
		return (ret);
	}

	buf = msgbuf + offset;
	len = left;

	if ((e = ibuf_new(buf, len)) == NULL)
		goto done;

	if (ikev2_msg_frompeer(msg)) {
		e = ikev2_msg_decrypt(env, msg->msg_sa, msg->msg_data, e);
	} else {
		sa->sa_hdr.sh_initiator = sa->sa_hdr.sh_initiator ? 0 : 1;
		e = ikev2_msg_decrypt(env, msg->msg_sa, msg->msg_data, e);
		sa->sa_hdr.sh_initiator = sa->sa_hdr.sh_initiator ? 0 : 1;
	}

	if (e == NULL)
		goto done;

	/*
	 * Parse decrypted payload
	 */
	bzero(&emsg, sizeof(emsg));
	memcpy(&emsg, msg, sizeof(*msg));
	emsg.msg_data = e;
	emsg.msg_e = 1;
	emsg.msg_parent = msg;
	TAILQ_INIT(&emsg.msg_proposals);

	ret = ikev2_pld_payloads(env, &emsg, 0, ibuf_size(e),
	    pld->pld_nextpayload);

 done:
	ibuf_free(e);

	return (ret);
}

int
ikev2_validate_cp(struct iked_message *msg, size_t offset, size_t left,
    struct ikev2_cp *cp)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*cp)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*cp));
		return (-1);
	}
	memcpy(cp, msgbuf + offset, sizeof(*cp));

	return (0);
}

int
ikev2_pld_cp(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct ikev2_cp		 cp;
	struct ikev2_cfg	*cfg;
	struct iked_addr	*addr;
	struct sockaddr_in	*in4;
	struct sockaddr_in6	*in6;
	uint8_t			*msgbuf = ibuf_data(msg->msg_data);
	uint8_t			*ptr;
	size_t			 len;
	int			 cfg_type;

	if (ikev2_validate_cp(msg, offset, left, &cp))
		return (-1);

	ptr = msgbuf + offset + sizeof(cp);
	len = left - sizeof(cp);

	log_debug("%s: type %s length %zu",
	    __func__, print_map(cp.cp_type, ikev2_cp_map), len);
	print_hex(ptr, 0, len);

	while (len > 0) {
		if (len < sizeof(*cfg)) {
			log_debug("%s: malformed payload: too short for cfg "
			    "(%zu < %zu)", __func__, len, sizeof(*cfg));
			return (-1);
		}
		cfg = (struct ikev2_cfg *)ptr;

		log_debug("%s: %s 0x%04x length %d", __func__,
		    print_map(betoh16(cfg->cfg_type), ikev2_cfg_map),
		    betoh16(cfg->cfg_type),
		    betoh16(cfg->cfg_length));

		ptr += sizeof(*cfg);
		len -= sizeof(*cfg);

		if (len < betoh16(cfg->cfg_length)) {
			log_debug("%s: malformed payload: too short for "
			    "cfg_length (%zu < %u)", __func__, len,
			    betoh16(cfg->cfg_length));
			return (-1);
		}

		print_hex(ptr, sizeof(*cfg), betoh16(cfg->cfg_length));

		cfg_type = betoh16(cfg->cfg_type);
		switch (cfg_type) {
		case IKEV2_CFG_INTERNAL_IP4_ADDRESS:
		case IKEV2_CFG_INTERNAL_IP4_DNS:
			if (!ikev2_msg_frompeer(msg))
				break;
			if (betoh16(cfg->cfg_length) == 0)
				break;
			/* XXX multiple-valued */
			if (betoh16(cfg->cfg_length) < 4) {
				log_debug("%s: malformed payload: too short "
				    "for ipv4 addr (%u < %u)",
				    __func__, betoh16(cfg->cfg_length), 4);
				return (-1);
			}
			switch(cfg_type) {
			case IKEV2_CFG_INTERNAL_IP4_ADDRESS:
				if (msg->msg_parent->msg_cp_addr != NULL) {
					log_debug("%s: address already set", __func__);
					goto skip;
				}
				break;
			case IKEV2_CFG_INTERNAL_IP4_DNS:
				if (msg->msg_parent->msg_cp_dns != NULL) {
					log_debug("%s: dns already set", __func__);
					goto skip;
				}
				break;
			default:
				break;
			}
			if ((addr = calloc(1, sizeof(*addr))) == NULL) {
				log_debug("%s: malloc failed", __func__);
				break;
			}
			addr->addr_af = AF_INET;
			in4 = (struct sockaddr_in *)&addr->addr;
			in4->sin_family = AF_INET;
			in4->sin_len = sizeof(*in4);
			memcpy(&in4->sin_addr.s_addr, ptr, 4);
			switch(cfg_type) {
			case IKEV2_CFG_INTERNAL_IP4_ADDRESS:
				msg->msg_parent->msg_cp_addr = addr;
				log_debug("%s: IP4_ADDRESS %s", __func__,
				    print_addr(&addr->addr));
				break;
			case IKEV2_CFG_INTERNAL_IP4_DNS:
				msg->msg_parent->msg_cp_dns = addr;
				log_debug("%s: IP4_DNS %s", __func__,
				    print_addr(&addr->addr));
				break;
			default:
				log_debug("%s: cfg %s", __func__,
				    print_addr(&addr->addr));
				break;
			}
			break;
		case IKEV2_CFG_INTERNAL_IP6_ADDRESS:
		case IKEV2_CFG_INTERNAL_IP6_DNS:
			if (!ikev2_msg_frompeer(msg))
				break;
			if (betoh16(cfg->cfg_length) == 0)
				break;
			/* XXX multiple-valued */
			if (betoh16(cfg->cfg_length) < 16) {
				log_debug("%s: malformed payload: too short "
				    "for ipv6 addr w/prefixlen (%u < %u)",
				    __func__, betoh16(cfg->cfg_length), 16);
				return (-1);
			}
			switch(cfg_type) {
			case IKEV2_CFG_INTERNAL_IP6_ADDRESS:
				if (msg->msg_parent->msg_cp_addr6 != NULL) {
					log_debug("%s: address6 already set", __func__);
					goto skip;
				}
				break;
			case IKEV2_CFG_INTERNAL_IP6_DNS:
				if (msg->msg_parent->msg_cp_dns != NULL) {
					log_debug("%s: dns already set", __func__);
					goto skip;
				}
				break;
			}
			if ((addr = calloc(1, sizeof(*addr))) == NULL) {
				log_debug("%s: malloc failed", __func__);
				break;
			}
			addr->addr_af = AF_INET6;
			in6 = (struct sockaddr_in6 *)&addr->addr;
			in6->sin6_family = AF_INET6;
			in6->sin6_len = sizeof(*in6);
			memcpy(&in6->sin6_addr, ptr, 16);
			switch(cfg_type) {
			case IKEV2_CFG_INTERNAL_IP6_ADDRESS:
				msg->msg_parent->msg_cp_addr6 = addr;
				log_debug("%s: IP6_ADDRESS %s", __func__,
				    print_addr(&addr->addr));
				break;
			case IKEV2_CFG_INTERNAL_IP6_DNS:
				msg->msg_parent->msg_cp_dns = addr;
				log_debug("%s: IP6_DNS %s", __func__,
				    print_addr(&addr->addr));
				break;
			default:
				log_debug("%s: cfg %s/%d", __func__,
				    print_addr(&addr->addr), ptr[16]);
				break;
			}
			break;
		}

 skip:
		ptr += betoh16(cfg->cfg_length);
		len -= betoh16(cfg->cfg_length);
	}

	if (!ikev2_msg_frompeer(msg))
		return (0);

	msg->msg_parent->msg_cp = cp.cp_type;

	return (0);
}

int
ikev2_validate_eap(struct iked_message *msg, size_t offset, size_t left,
    struct eap_header *hdr)
{
	uint8_t		*msgbuf = ibuf_data(msg->msg_data);

	if (left < sizeof(*hdr)) {
		log_debug("%s: malformed payload: too short for header "
		    "(%zu < %zu)", __func__, left, sizeof(*hdr));
		return (-1);
	}
	memcpy(hdr, msgbuf + offset, sizeof(*hdr));

	return (0);
}

int
ikev2_pld_eap(struct iked *env, struct ikev2_payload *pld,
    struct iked_message *msg, size_t offset, size_t left)
{
	struct eap_header		 hdr;
	struct eap_message		*eap = NULL;
	const struct iked_sa		*sa = msg->msg_sa;
	size_t				 eap_len;

	if (ikev2_validate_eap(msg, offset, left, &hdr))
		return (-1);

	eap_len = betoh16(hdr.eap_length);
	if (left != eap_len) {
		log_info("%s: malformed payload: EAP length does not match"
		    " payload length (%zu != %zu)", __func__, left, eap_len);
		return (-1);
	}

	if (eap_len < sizeof(*eap)) {
		log_info("%s: %s id %d length %d", SPI_SA(sa, __func__),
		    print_map(hdr.eap_code, eap_code_map),
		    hdr.eap_id, betoh16(hdr.eap_length));
	} else {
		/* Now try to get the indicated length */
		if ((eap = ibuf_seek(msg->msg_data, offset, eap_len)) == NULL) {
			log_debug("%s: invalid EAP length", __func__);
			return (-1);
		}

		log_info("%s: %s id %d length %d EAP-%s", SPI_SA(sa, __func__),
		    print_map(eap->eap_code, eap_code_map),
		    eap->eap_id, betoh16(eap->eap_length),
		    print_map(eap->eap_type, eap_type_map));

		if (eap_parse(env, sa, msg, eap, msg->msg_response) == -1)
			return (-1);
		if (msg->msg_parent->msg_eapmsg != NULL) {
			log_info("%s: duplicate EAP in payload", __func__);
			return (-1);
		}
		if ((msg->msg_parent->msg_eapmsg = ibuf_new(eap, eap_len))
		    == NULL) {
			log_debug("%s: failed to save eap", __func__);
			return (-1);
		}
		msg->msg_parent->msg_eap.eam_found = 1;
	}

	return (0);
}

/* parser for the initial IKE_AUTH payload, does not require msg_sa */
int
ikev2_pld_parse_quick(struct iked *env, struct ike_header *hdr,
    struct iked_message *msg, size_t offset)
{
	struct ikev2_payload	 pld;
	struct ikev2_frag_payload frag;
	uint8_t			*msgbuf = ibuf_data(msg->msg_data);
	uint8_t			*buf;
	size_t			 len, total, left;
	size_t			 length;
	unsigned int		 payload;

	log_debug("%s: header ispi %s rspi %s"
	    " nextpayload %s version 0x%02x exchange %s flags 0x%02x"
	    " msgid %d length %u response %d", __func__,
	    print_spi(betoh64(hdr->ike_ispi), 8),
	    print_spi(betoh64(hdr->ike_rspi), 8),
	    print_map(hdr->ike_nextpayload, ikev2_payload_map),
	    hdr->ike_version,
	    print_map(hdr->ike_exchange, ikev2_exchange_map),
	    hdr->ike_flags,
	    betoh32(hdr->ike_msgid),
	    betoh32(hdr->ike_length),
	    msg->msg_response);

	length = betoh32(hdr->ike_length);

	if (ibuf_size(msg->msg_data) < length) {
		log_debug("%s: short message", __func__);
		return (-1);
	}

	offset += sizeof(*hdr);

	/* Bytes left in datagram. */
	total = length - offset;

	payload = hdr->ike_nextpayload;

	while (payload != 0 && offset < length) {
		if (ikev2_validate_pld(msg, offset, total, &pld))
			return (-1);

		log_debug("%s: %spayload %s"
		    " nextpayload %s critical 0x%02x length %d",
		    __func__, msg->msg_e ? "decrypted " : "",
		    print_map(payload, ikev2_payload_map),
		    print_map(pld.pld_nextpayload, ikev2_payload_map),
		    pld.pld_reserved & IKEV2_CRITICAL_PAYLOAD,
		    betoh16(pld.pld_length));

		/* Skip over generic payload header. */
		offset += sizeof(pld);
		total -= sizeof(pld);
		left = betoh16(pld.pld_length) - sizeof(pld);

		switch (payload) {
		case IKEV2_PAYLOAD_SKF:
			len = left;
			buf = msgbuf + offset;
			if (len < sizeof(frag))
				return (-1);
			memcpy(&frag, buf, sizeof(frag));
			msg->msg_frag_num = betoh16(frag.frag_num);
			break;
		}

		payload = pld.pld_nextpayload;
		offset += left;
		total -= left;
	}

	return (0);
}
