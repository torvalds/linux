/*	$OpenBSD: eap.c,v 1.28 2024/11/21 13:26:49 claudio Exp $	*/

/*
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
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

int	 eap_message_send(struct iked *, struct iked_sa *, int, int);
ssize_t	 eap_add_id_request(struct ibuf *);
char	*eap_validate_id_response(struct eap_message *);
int	 eap_mschap(struct iked *, const struct iked_sa *,
	    struct iked_message *, struct eap_message *);

ssize_t
eap_add_id_request(struct ibuf *e)
{
	struct eap_message		*eap;

	if ((eap = ibuf_reserve(e, sizeof(*eap))) == NULL)
		return (-1);
	eap->eap_code = EAP_CODE_REQUEST;
	eap->eap_id = 0;
	eap->eap_length = htobe16(sizeof(*eap));
	eap->eap_type = EAP_TYPE_IDENTITY;

	return (sizeof(*eap));
}

char *
eap_validate_id_response(struct eap_message *eap)
{
	size_t			 len;
	char			*str;
	uint8_t			*ptr = (uint8_t *)eap;

	len = betoh16(eap->eap_length) - sizeof(*eap);
	ptr += sizeof(*eap);

	if (len == 0) {
		if ((str = strdup("")) == NULL) {
			log_warn("%s: strdup failed", __func__);
			return (NULL);
		}
	} else if ((str = get_string(ptr, len)) == NULL) {
		log_info("%s: invalid identity response, length %zu",
		    __func__, len);
		return (NULL);
	}
	log_debug("%s: identity '%s' length %zd", __func__, str, len);
	return (str);
}

int
eap_identity_request(struct iked *env, struct iked_sa *sa)
{
	struct ikev2_payload		*pld;
	struct ikev2_cert		*cert;
	struct ikev2_auth		*auth;
	struct iked_id			*id, *certid;
	struct ibuf			*e = NULL;
	uint8_t				 firstpayload;
	int				 ret = -1;
	ssize_t				 len = 0;
	int				 i;

	/* Responder only */
	if (sa->sa_hdr.sh_initiator)
		return (-1);

	/* Check if "ca" has done its job yet */
	if (!sa->sa_localauth.id_type)
		return (0);

	/* New encrypted message buffer */
	if ((e = ibuf_static()) == NULL)
		goto done;

	id = &sa->sa_rid;
	certid = &sa->sa_rcert;

	/* ID payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	firstpayload = IKEV2_PAYLOAD_IDr;
	if (ibuf_add_ibuf(e, id->id_buf) != 0)
		goto done;
	len = ibuf_size(id->id_buf);

	if ((sa->sa_statevalid & IKED_REQ_CERT) &&
	    (certid->id_type != IKEV2_CERT_NONE)) {
		if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_CERT) == -1)
			goto done;

		/* CERT payload */
		if ((pld = ikev2_add_payload(e)) == NULL)
			goto done;
		if ((cert = ibuf_reserve(e, sizeof(*cert))) == NULL)
			goto done;
		cert->cert_type = certid->id_type;
		if (ibuf_add_ibuf(e, certid->id_buf) != 0)
			goto done;
		len = ibuf_size(certid->id_buf) + sizeof(*cert);

		for (i = 0; i < IKED_SCERT_MAX; i++) {
			if (sa->sa_scert[i].id_type == IKEV2_CERT_NONE)
				break;
			if (ikev2_next_payload(pld, len,
			    IKEV2_PAYLOAD_CERT) == -1)
				goto done;
			if ((pld = ikev2_add_payload(e)) == NULL)
				goto done;
			if ((cert = ibuf_reserve(e, sizeof(*cert))) == NULL)
				goto done;
			cert->cert_type = sa->sa_scert[i].id_type;
			if (ibuf_add_ibuf(e, sa->sa_scert[i].id_buf) != 0)
				goto done;
			len = ibuf_size(sa->sa_scert[i].id_buf) + sizeof(*cert);
		}
	}

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_AUTH) == -1)
		goto done;

	/* AUTH payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((auth = ibuf_reserve(e, sizeof(*auth))) == NULL)
		goto done;
	auth->auth_method = sa->sa_localauth.id_type;
	if (ibuf_add_ibuf(e, sa->sa_localauth.id_buf) != 0)
		goto done;
	len = ibuf_size(sa->sa_localauth.id_buf) + sizeof(*auth);

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_EAP) == -1)
		goto done;

	/* EAP payload */
	if ((pld = ikev2_add_payload(e)) == NULL)
		goto done;
	if ((len = eap_add_id_request(e)) == -1)
		goto done;

	if (ikev2_next_payload(pld, len, IKEV2_PAYLOAD_NONE) == -1)
		goto done;

	ret = ikev2_msg_send_encrypt(env, sa, &e,
	    IKEV2_EXCHANGE_IKE_AUTH, firstpayload, 1);
 done:
	ibuf_free(e);
	return (ret);
}

int
eap_challenge_request(struct iked *env, struct iked_sa *sa,
    int eap_id)
{
	struct eap_message		*eap;
	struct eap_mschap_challenge	*ms;
	const char			*name;
	int				 ret = -1;
	struct ibuf			*e;

	if ((e = ibuf_static()) == NULL)
		return (-1);

	if ((eap = ibuf_reserve(e, sizeof(*eap))) == NULL)
		goto done;
	eap->eap_code = EAP_CODE_REQUEST;
	eap->eap_id = eap_id + 1;
	eap->eap_type = sa->sa_policy->pol_auth.auth_eap;

	switch (sa->sa_policy->pol_auth.auth_eap) {
	case EAP_TYPE_MSCHAP_V2:
		name = IKED_USER;	/* XXX should be user-configurable */
		eap->eap_length = htobe16(sizeof(*eap) +
		    sizeof(*ms) + strlen(name));

		if ((ms = ibuf_reserve(e, sizeof(*ms))) == NULL)
			return (-1);
		ms->msc_opcode = EAP_MSOPCODE_CHALLENGE;
		ms->msc_id = eap->eap_id;
		ms->msc_length = htobe16(sizeof(*ms) + strlen(name));
		ms->msc_valuesize = sizeof(ms->msc_challenge);
		arc4random_buf(ms->msc_challenge, sizeof(ms->msc_challenge));
		if (ibuf_add(e, name, strlen(name)) == -1)
			goto done;

		/* Store the EAP challenge value */
		sa->sa_eap.id_type = eap->eap_type;
		if ((sa->sa_eap.id_buf = ibuf_new(ms->msc_challenge,
		    sizeof(ms->msc_challenge))) == NULL)
			goto done;
		break;
	default:
		log_debug("%s: unsupported EAP type %s", __func__,
		    print_map(eap->eap_type, eap_type_map));
		goto done;
	}

	ret = ikev2_send_ike_e(env, sa, e,
	    IKEV2_PAYLOAD_EAP, IKEV2_EXCHANGE_IKE_AUTH, 1);
 done:
	ibuf_free(e);
	return (ret);
}

int
eap_message_send(struct iked *env, struct iked_sa *sa, int eap_code, int eap_id)
{
	struct eap_header		*resp;
	int				 ret = -1;
	struct ibuf			*e;

	if ((e = ibuf_static()) == NULL)
		return (-1);

	if ((resp = ibuf_reserve(e, sizeof(*resp))) == NULL)
		goto done;
	resp->eap_code = eap_code;
	resp->eap_id = eap_id;
	resp->eap_length = htobe16(sizeof(*resp));

	ret = ikev2_send_ike_e(env, sa, e,
	    IKEV2_PAYLOAD_EAP, IKEV2_EXCHANGE_IKE_AUTH, 1);
 done:
	ibuf_free(e);
	return (ret);
}

int
eap_success(struct iked *env, struct iked_sa *sa, int eap_id)
{
	return (eap_message_send(env, sa, EAP_CODE_SUCCESS, eap_id));
}

int
eap_mschap_challenge(struct iked *env, struct iked_sa *sa, int eap_id,
    int msr_id, uint8_t *successmsg, size_t success_size)
{
	struct ibuf			*eapmsg = NULL;
	struct eap_message		*resp;
	struct eap_mschap_success	*mss;
	char				*msg;
	int				 ret = -1;

	if ((eapmsg = ibuf_static()) == NULL)
		return (-1);

	msg = " M=Welcome";

	if ((resp = ibuf_reserve(eapmsg, sizeof(*resp))) == NULL)
		goto done;
	resp->eap_code = EAP_CODE_REQUEST;
	resp->eap_id = eap_id + 1;
	resp->eap_length = htobe16(sizeof(*resp) + sizeof(*mss) +
	    success_size + strlen(msg));
	resp->eap_type = EAP_TYPE_MSCHAP_V2;

	if ((mss = ibuf_reserve(eapmsg, sizeof(*mss))) == NULL)
		goto done;
	mss->mss_opcode = EAP_MSOPCODE_SUCCESS;
	mss->mss_id = msr_id;
	mss->mss_length = htobe16(sizeof(*mss) +
	    success_size + strlen(msg));
	if (ibuf_add(eapmsg, successmsg, success_size) != 0)
		goto done;
	if (ibuf_add(eapmsg, msg, strlen(msg)) != 0)
		goto done;

	ret = ikev2_send_ike_e(env, sa, eapmsg,
	    IKEV2_PAYLOAD_EAP, IKEV2_EXCHANGE_IKE_AUTH, 1);
 done:
	ibuf_free(eapmsg);
	return (ret);
}

int
eap_mschap_success(struct iked *env, struct iked_sa *sa, int eap_id)
{
	struct ibuf			*eapmsg = NULL;
	struct eap_message		*resp;
	struct eap_mschap		*ms;
	int				 ret = -1;

	if ((eapmsg = ibuf_static()) == NULL)
		return (-1);
	if ((resp = ibuf_reserve(eapmsg, sizeof(*resp))) == NULL)
		goto done;
	resp->eap_code = EAP_CODE_RESPONSE;
	resp->eap_id = eap_id;
	resp->eap_length = htobe16(sizeof(*resp) + sizeof(*ms));
	resp->eap_type = EAP_TYPE_MSCHAP_V2;
	if ((ms = ibuf_reserve(eapmsg, sizeof(*ms))) == NULL)
		goto done;
	ms->ms_opcode = EAP_MSOPCODE_SUCCESS;

	ret = ikev2_send_ike_e(env, sa, eapmsg,
	    IKEV2_PAYLOAD_EAP, IKEV2_EXCHANGE_IKE_AUTH, 1);
 done:
	ibuf_free(eapmsg);
	return (ret);
}

int
eap_mschap(struct iked *env, const struct iked_sa *sa,
    struct iked_message *msg, struct eap_message *eap)
{
	struct eap_mschap_response	*msr;
	struct eap_mschap_peer		*msp;
	struct eap_mschap		*ms;
	uint8_t				*ptr;
	size_t				 len;
	int				 ret = -1;

	if (!sa_stateok(sa, IKEV2_STATE_EAP)) {
		log_debug("%s: unexpected EAP", __func__);
		return (0);	/* ignore */
	}

	if (sa->sa_hdr.sh_initiator) {
		log_debug("%s: initiator EAP not supported", __func__);
		return (-1);
	}

	/* Only MSCHAP-V2 */
	if (eap->eap_type != EAP_TYPE_MSCHAP_V2) {
		log_debug("%s: unsupported type EAP-%s", __func__,
		    print_map(eap->eap_type, eap_type_map));
		return (-1);
	}

	if (betoh16(eap->eap_length) < (sizeof(*eap) + sizeof(*ms))) {
		log_debug("%s: short message", __func__);
		return (-1);
	}

	ms = (struct eap_mschap *)(eap + 1);
	ptr = (uint8_t *)(eap + 1);

	switch (ms->ms_opcode) {
	case EAP_MSOPCODE_RESPONSE:
		msr = (struct eap_mschap_response *)ms;
		if (betoh16(eap->eap_length) < (sizeof(*eap) + sizeof(*msr))) {
			log_debug("%s: short response", __func__);
			return (-1);
		}
		ptr += sizeof(*msr);
		len = betoh16(eap->eap_length) -
		    sizeof(*eap) - sizeof(*msr);
		if (len != 0)
			msg->msg_parent->msg_eap.eam_user = get_string(ptr, len);

		msg->msg_parent->msg_eap.eam_msrid = msr->msr_id;
		msp = &msr->msr_response.resp_peer;
		memcpy(msg->msg_parent->msg_eap.eam_challenge,
		    msp->msp_challenge, EAP_MSCHAP_CHALLENGE_SZ);
		memcpy(msg->msg_parent->msg_eap.eam_ntresponse,
		    msp->msp_ntresponse, EAP_MSCHAP_NTRESPONSE_SZ);
		msg->msg_parent->msg_eap.eam_state =
		    EAP_STATE_MSCHAPV2_CHALLENGE;
		return (0);
	case EAP_MSOPCODE_SUCCESS:
		msg->msg_parent->msg_eap.eam_state = EAP_STATE_MSCHAPV2_SUCCESS;
		return (0);
	case EAP_MSOPCODE_FAILURE:
	case EAP_MSOPCODE_CHANGE_PASSWORD:
	case EAP_MSOPCODE_CHALLENGE:
	default:
		log_debug("%s: EAP-%s unsupported "
		    "responder operation %s", __func__,
		    print_map(eap->eap_type, eap_type_map),
		    print_map(ms->ms_opcode, eap_msopcode_map));
		return (-1);
	}
	return (ret);
}

int
eap_parse(struct iked *env, const struct iked_sa *sa, struct iked_message *msg,
    void *data, int response)
{
	struct eap_header		*hdr = data;
	struct eap_message		*eap = data;
	size_t				 len;
	uint8_t				*ptr;
	struct eap_mschap		*ms;
	struct eap_mschap_challenge	*msc;
	struct eap_mschap_response	*msr;
	struct eap_mschap_success	*mss;
	struct eap_mschap_failure	*msf;
	char				*str;

	/* length is already verified by the caller against sizeof(eap) */
	len = betoh16(hdr->eap_length);
	if (len < sizeof(*eap))
		goto fail;
	ptr = (uint8_t *)(eap + 1);
	len -= sizeof(*eap);

	switch (hdr->eap_code) {
	case EAP_CODE_REQUEST:
	case EAP_CODE_RESPONSE:
		break;
	case EAP_CODE_SUCCESS:
		return (0);
	case EAP_CODE_FAILURE:
		if (response)
			return (0);
		return (-1);
	default:
		log_debug("%s: unsupported EAP code %s", __func__,
		    print_map(hdr->eap_code, eap_code_map));
		return (-1);
	}

	msg->msg_parent->msg_eap.eam_id = hdr->eap_id;
	msg->msg_parent->msg_eap.eam_type = eap->eap_type;

	switch (eap->eap_type) {
	case EAP_TYPE_IDENTITY:
		if (eap->eap_code == EAP_CODE_REQUEST)
			break;
		if ((str = eap_validate_id_response(eap)) == NULL)
			return (-1);
		if (response) {
			free(str);
			break;
		}
		if (sa->sa_eapid != NULL) {
			free(str);
			log_debug("%s: EAP identity already known", __func__);
			return (0);
		}
		msg->msg_parent->msg_eap.eam_response = 1;
		msg->msg_parent->msg_eap.eam_identity = str;
		msg->msg_parent->msg_eap.eam_state =
		    EAP_STATE_IDENTITY;
		return (0);
	case EAP_TYPE_MSCHAP_V2:
		if (len < sizeof(*ms))
			goto fail;
		ms = (struct eap_mschap *)ptr;
		switch (ms->ms_opcode) {
		case EAP_MSOPCODE_CHALLENGE:
			if (len < sizeof(*msc))
				goto fail;
			msc = (struct eap_mschap_challenge *)ptr;
			ptr += sizeof(*msc);
			len -= sizeof(*msc);
			if ((str = get_string(ptr, len)) == NULL) {
				log_debug("%s: invalid challenge name",
				    __func__);
				return (-1);
			}
			log_info("%s: %s %s id %d "
			    "length %d valuesize %d name '%s' length %zu",
			    SPI_SA(sa, __func__),
			    print_map(eap->eap_type, eap_type_map),
			    print_map(ms->ms_opcode, eap_msopcode_map),
			    msc->msc_id, betoh16(msc->msc_length),
			    msc->msc_valuesize, str, len);
			free(str);
			print_hex(msc->msc_challenge, 0,
			    sizeof(msc->msc_challenge));
			break;
		case EAP_MSOPCODE_RESPONSE:
			if (len < sizeof(*msr))
				goto fail;
			msr = (struct eap_mschap_response *)ptr;
			ptr += sizeof(*msr);
			len -= sizeof(*msr);
			if ((str = get_string(ptr, len)) == NULL) {
				log_debug("%s: invalid response name",
				    __func__);
				return (-1);
			}
			log_info("%s: %s %s id %d "
			    "length %d valuesize %d name '%s' name-length %zu",
			    __func__,
			    print_map(eap->eap_type, eap_type_map),
			    print_map(ms->ms_opcode, eap_msopcode_map),
			    msr->msr_id, betoh16(msr->msr_length),
			    msr->msr_valuesize, str, len);
			free(str);
			print_hex(msr->msr_response.resp_data, 0,
			    sizeof(msr->msr_response.resp_data));
			break;
		case EAP_MSOPCODE_SUCCESS:
			if (eap->eap_code == EAP_CODE_REQUEST) {
				if (len < sizeof(*mss))
					goto fail;
				mss = (struct eap_mschap_success *)ptr;
				ptr += sizeof(*mss);
				len -= sizeof(*mss);
				if ((str = get_string(ptr, len)) == NULL) {
					log_debug("%s: invalid response name",
					    __func__);
					return (-1);
				}
				log_info("%s: %s %s request id %d "
				    "length %d message '%s' message-len %zu",
				    __func__,
				    print_map(eap->eap_type, eap_type_map),
				    print_map(ms->ms_opcode, eap_msopcode_map),
				    mss->mss_id, betoh16(mss->mss_length),
				    str, len);
				free(str);
			} else {
				if (len < sizeof(*ms))
					goto fail;
				ms = (struct eap_mschap *)ptr;
				log_info("%s: %s %s response", __func__,
				    print_map(eap->eap_type, eap_type_map),
				    print_map(ms->ms_opcode, eap_msopcode_map));
				if (response)
					break;
				msg->msg_parent->msg_eap.eam_success = 1;
				msg->msg_parent->msg_eap.eam_state =
				    EAP_STATE_SUCCESS;
				return (0);
			}
			break;
		case EAP_MSOPCODE_FAILURE:
			if (len < sizeof(*msf))
				goto fail;
			msf = (struct eap_mschap_failure *)ptr;
			ptr += sizeof(*msf);
			len -= sizeof(*msf);
			if ((str = get_string(ptr, len)) == NULL) {
				log_debug("%s: invalid failure message",
				    __func__);
				return (-1);
			}
			log_info("%s: %s %s id %d "
			    "length %d message '%s'", __func__,
			    print_map(eap->eap_type, eap_type_map),
			    print_map(ms->ms_opcode, eap_msopcode_map),
			    msf->msf_id, betoh16(msf->msf_length), str);
			free(str);
			break;
		default:
			log_info("%s: unknown ms opcode %d", __func__,
			    ms->ms_opcode);
			return (-1);
		}
		if (response)
			break;

		return (eap_mschap(env, sa, msg, eap));
	default:
		if (sa->sa_policy->pol_auth.auth_eap != EAP_TYPE_RADIUS) {
			log_debug("%s: unsupported EAP type %s", __func__,
			    print_map(eap->eap_type, eap_type_map));
			return (-1);
		} /* else, when RADIUS, pass it to the client */
		break;
	}

	return (0);

 fail:
	log_debug("%s: short message", __func__);
	return (-1);
}
