/* $OpenBSD: ike_phase_1.c,v 1.78 2018/09/20 11:49:55 jsg Exp $	 */
/* $EOM: ike_phase_1.c,v 1.31 2000/12/11 23:47:56 niklas Exp $	 */

/*
 * Copyright (c) 1999, 2000 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2001, 2004 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
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

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

#include "attribute.h"
#include "conf.h"
#include "constants.h"
#include "crypto.h"
#include "dh.h"
#include "doi.h"
#include "dpd.h"
#include "exchange.h"
#include "hash.h"
#include "ike_auth.h"
#include "ike_phase_1.h"
#include "ipsec.h"
#include "ipsec_doi.h"
#include "isakmp.h"
#include "log.h"
#include "message.h"
#include "nat_traversal.h"
#include "prf.h"
#include "sa.h"
#include "transport.h"
#include "util.h"
#include "vendor.h"

static int      attribute_unacceptable(u_int16_t, u_int8_t *, u_int16_t,
		    void *);
static int	ike_phase_1_validate_prop(struct exchange *, struct sa *,
		    struct sa *);

/* Offer a set of transforms to the responder in the MSG message.  */
int
ike_phase_1_initiator_send_SA(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	u_int8_t       *proposal = 0, *sa_buf = 0, *saved_nextp, *attr;
	u_int8_t      **transform = 0;
	size_t          transforms_len = 0, proposal_len, sa_len;
	size_t         *transform_len = 0;
	struct conf_list *conf, *life_conf;
	struct conf_list_node *xf, *life;
	int             value, update_nextp;
	size_t          i;
	struct payload *p;
	struct proto   *proto;
	struct proto_attr *pa;
	int             group_desc = -1, new_group_desc;

	/* Get the list of transforms.  */
	conf = conf_get_list(exchange->policy, "Transforms");
	if (!conf)
		return -1;

	transform = calloc(conf->cnt, sizeof *transform);
	if (!transform) {
		log_error("ike_phase_1_initiator_send_SA: calloc (%lu, %lu) "
		    "failed", (u_long)conf->cnt, (u_long)sizeof *transform);
		goto bail_out;
	}
	transform_len = calloc(conf->cnt, sizeof *transform_len);
	if (!transform_len) {
		log_error("ike_phase_1_initiator_send_SA: calloc (%lu, %lu) "
		    "failed", (u_long)conf->cnt,
		    (u_long)sizeof *transform_len);
		goto bail_out;
	}
	for (xf = TAILQ_FIRST(&conf->fields), i = 0; i < conf->cnt;
	    i++, xf = TAILQ_NEXT(xf, link)) {
		/* XXX The sizing needs to be dynamic.  */
		transform[i] = malloc(ISAKMP_TRANSFORM_SA_ATTRS_OFF +
		    16 * ISAKMP_ATTR_VALUE_OFF);
		if (!transform[i]) {
			log_error("ike_phase_1_initiator_send_SA: malloc (%d) "
			    "failed", ISAKMP_TRANSFORM_SA_ATTRS_OFF +
			    16 * ISAKMP_ATTR_VALUE_OFF);
			goto bail_out;
		}
		SET_ISAKMP_TRANSFORM_NO(transform[i], i);
		SET_ISAKMP_TRANSFORM_ID(transform[i], IPSEC_TRANSFORM_KEY_IKE);
		SET_ISAKMP_TRANSFORM_RESERVED(transform[i], 0);

		attr = transform[i] + ISAKMP_TRANSFORM_SA_ATTRS_OFF;

		if (attribute_set_constant(xf->field, "ENCRYPTION_ALGORITHM",
		    ike_encrypt_cst, IKE_ATTR_ENCRYPTION_ALGORITHM, &attr))
			goto bail_out;

		if (attribute_set_constant(xf->field, "HASH_ALGORITHM",
		    ike_hash_cst, IKE_ATTR_HASH_ALGORITHM, &attr))
			goto bail_out;

		if (attribute_set_constant(xf->field, "AUTHENTICATION_METHOD",
		    ike_auth_cst, IKE_ATTR_AUTHENTICATION_METHOD, &attr))
			goto bail_out;

		if (attribute_set_constant(xf->field, "GROUP_DESCRIPTION",
		    ike_group_desc_cst, IKE_ATTR_GROUP_DESCRIPTION, &attr)) {
			/*
			 * If no group description exists, try looking for
			 * a user-defined one.
			 */
			if (attribute_set_constant(xf->field, "GROUP_TYPE",
			    ike_group_cst, IKE_ATTR_GROUP_TYPE, &attr))
				goto bail_out;

#if 0
			if (attribute_set_bignum(xf->field, "GROUP_PRIME",
			    IKE_ATTR_GROUP_PRIME, &attr))
				goto bail_out;

			if (attribute_set_bignum(xf->field,
			    "GROUP_GENERATOR_2", IKE_ATTR_GROUP_GENERATOR_2,
			    &attr))
				goto bail_out;

			if (attribute_set_bignum(xf->field,
			    "GROUP_GENERATOR_2", IKE_ATTR_GROUP_GENERATOR_2,
			    &attr))
				goto bail_out;

			if (attribute_set_bignum(xf->field, "GROUP_CURVE_A",
			    IKE_ATTR_GROUP_CURVE_A, &attr))
				goto bail_out;

			if (attribute_set_bignum(xf->field, "GROUP_CURVE_B",
			    IKE_ATTR_GROUP_CURVE_B, &attr))
				goto bail_out;
#endif
		}
		/*
		 * Life durations are special, we should be able to specify
		 * several, one per type.
		 */
		life_conf = conf_get_list(xf->field, "Life");
		if (life_conf) {
			for (life = TAILQ_FIRST(&life_conf->fields); life;
			    life = TAILQ_NEXT(life, link)) {
				attribute_set_constant(life->field,
				    "LIFE_TYPE", ike_duration_cst,
				    IKE_ATTR_LIFE_TYPE, &attr);

				/*
				 * XXX Deals with 16 and 32 bit lifetimes
				 * only
				 */
				value = conf_get_num(life->field,
				    "LIFE_DURATION", 0);
				if (value) {
					if (value <= 0xffff)
						attr = attribute_set_basic(
						    attr,
						    IKE_ATTR_LIFE_DURATION,
						    value);
					else {
						value = htonl(value);
						attr = attribute_set_var(attr,
						    IKE_ATTR_LIFE_DURATION,
						    (u_int8_t *)&value,
						    sizeof value);
					}
				}
			}
			conf_free_list(life_conf);
		}
		attribute_set_constant(xf->field, "PRF", ike_prf_cst,
		    IKE_ATTR_PRF, &attr);

		value = conf_get_num(xf->field, "KEY_LENGTH", 0);
		if (value)
			attr = attribute_set_basic(attr, IKE_ATTR_KEY_LENGTH,
			    value);

		value = conf_get_num(xf->field, "FIELD_SIZE", 0);
		if (value)
			attr = attribute_set_basic(attr, IKE_ATTR_FIELD_SIZE,
			    value);

		value = conf_get_num(xf->field, "GROUP_ORDER", 0);
		if (value)
			attr = attribute_set_basic(attr, IKE_ATTR_GROUP_ORDER,
			    value);

		/* Record the real transform size.  */
		transforms_len += transform_len[i] = attr - transform[i];

		/* XXX I don't like exchange-specific stuff in here.  */
		if (exchange->type == ISAKMP_EXCH_AGGRESSIVE) {
			/*
			 * Make sure that if a group description is specified,
			 * it is specified for all transforms equally.
			 */
			attr = (u_int8_t *)conf_get_str(xf->field,
			    "GROUP_DESCRIPTION");
			new_group_desc =
			    attr ? constant_value(ike_group_desc_cst,
				(char *)attr) : 0;
			if (group_desc == -1)
				group_desc = new_group_desc;
			else if (group_desc != new_group_desc) {
				log_print("ike_phase_1_initiator_send_SA: "
				    "differing group descriptions in a "
				    "proposal");
				goto bail_out;
			}
		}
		/*
		 * We need to check that we actually support our
		 * configuration.
		 */
		if (attribute_map(transform[i] + ISAKMP_TRANSFORM_SA_ATTRS_OFF,
		    transform_len[i] - ISAKMP_TRANSFORM_SA_ATTRS_OFF,
		    exchange->doi->is_attribute_incompatible, msg)) {
			log_print("ike_phase_1_initiator_send_SA: "
			    "section [%s] has unsupported attribute(s)",
			    xf->field);
			goto bail_out;
		}
	}

	/* XXX I don't like exchange-specific stuff in here.  */
	if (exchange->type == ISAKMP_EXCH_AGGRESSIVE)
		ie->group = group_get(group_desc);

	proposal_len = ISAKMP_PROP_SPI_OFF;
	proposal = malloc(proposal_len);
	if (!proposal) {
		log_error("ike_phase_1_initiator_send_SA: malloc (%lu) failed",
		    (unsigned long)proposal_len);
		goto bail_out;
	}
	SET_ISAKMP_PROP_NO(proposal, 1);
	SET_ISAKMP_PROP_PROTO(proposal, ISAKMP_PROTO_ISAKMP);
	SET_ISAKMP_PROP_SPI_SZ(proposal, 0);
	SET_ISAKMP_PROP_NTRANSFORMS(proposal, conf->cnt);

	/* XXX I would like to see this factored out.  */
	proto = calloc(1, sizeof *proto);
	if (!proto) {
		log_error("ike_phase_1_initiator_send_SA: "
		    "calloc (1, %lu) failed", (unsigned long)sizeof *proto);
		goto bail_out;
	}
	proto->no = 1;
	proto->proto = ISAKMP_PROTO_ISAKMP;
	proto->sa = TAILQ_FIRST(&exchange->sa_list);
	proto->xf_cnt = conf->cnt;
	TAILQ_INIT(&proto->xfs);
	for (i = 0; i < proto->xf_cnt; i++) {
		pa = calloc(1, sizeof *pa);
		if (!pa)
			goto bail_out;
		pa->len = transform_len[i];
		pa->attrs = malloc(pa->len);
		if (!pa->attrs) {
			free(pa);
			goto bail_out;
		}
		memcpy(pa->attrs, transform[i], pa->len);
		TAILQ_INSERT_TAIL(&proto->xfs, pa, next);
	}
	TAILQ_INSERT_TAIL(&TAILQ_FIRST(&exchange->sa_list)->protos, proto,
	    link);

	sa_len = ISAKMP_SA_SIT_OFF + IPSEC_SIT_SIT_LEN;
	sa_buf = malloc(sa_len);
	if (!sa_buf) {
		log_error("ike_phase_1_initiator_send_SA: malloc (%lu) failed",
		    (unsigned long)sa_len);
		goto bail_out;
	}
	SET_ISAKMP_SA_DOI(sa_buf, IPSEC_DOI_IPSEC);
	SET_IPSEC_SIT_SIT(sa_buf + ISAKMP_SA_SIT_OFF, IPSEC_SIT_IDENTITY_ONLY);

	/*
	 * Add the payloads.  As this is a SA, we need to recompute the
	 * lengths of the payloads containing others.
	 */
	if (message_add_payload(msg, ISAKMP_PAYLOAD_SA, sa_buf, sa_len, 1))
		goto bail_out;
	SET_ISAKMP_GEN_LENGTH(sa_buf,
	    sa_len + proposal_len + transforms_len);
	sa_buf = 0;

	saved_nextp = msg->nextp;
	if (message_add_payload(msg, ISAKMP_PAYLOAD_PROPOSAL, proposal,
	    proposal_len, 0))
		goto bail_out;
	SET_ISAKMP_GEN_LENGTH(proposal, proposal_len + transforms_len);
	proposal = 0;

	update_nextp = 0;
	for (i = 0; i < conf->cnt; i++) {
		if (message_add_payload(msg, ISAKMP_PAYLOAD_TRANSFORM,
		    transform[i], transform_len[i], update_nextp))
			goto bail_out;
		update_nextp = 1;
		transform[i] = 0;
	}
	msg->nextp = saved_nextp;

	/* Save SA payload body in ie->sa_i_b, length ie->sa_i_b_len.  */
	ie->sa_i_b_len = sa_len + proposal_len + transforms_len -
	    ISAKMP_GEN_SZ;
	ie->sa_i_b = malloc(ie->sa_i_b_len);
	if (!ie->sa_i_b) {
		log_error("ike_phase_1_initiator_send_SA: malloc (%lu) failed",
		    (unsigned long)ie->sa_i_b_len);
		goto bail_out;
	}
	memcpy(ie->sa_i_b,
	    payload_first(msg, ISAKMP_PAYLOAD_SA)->p + ISAKMP_GEN_SZ,
	    sa_len - ISAKMP_GEN_SZ);
	memcpy(ie->sa_i_b + sa_len - ISAKMP_GEN_SZ,
	    payload_first(msg, ISAKMP_PAYLOAD_PROPOSAL)->p, proposal_len);
	transforms_len = 0;
	for (i = 0, p = TAILQ_FIRST(&msg->payload[ISAKMP_PAYLOAD_TRANSFORM]);
	    i < conf->cnt; i++, p = TAILQ_NEXT(p, link)) {
		memcpy(ie->sa_i_b + sa_len + proposal_len + transforms_len -
		    ISAKMP_GEN_SZ, p->p, transform_len[i]);
		transforms_len += transform_len[i];
	}

	/* Advertise OpenBSD isakmpd. */
	if (add_vendor_openbsd(msg))
		goto bail_out;

	/* Advertise NAT-T capability.  */
	if (nat_t_add_vendor_payloads(msg))
		goto bail_out;

	/* Advertise DPD capability.  */
	if (dpd_add_vendor_payload(msg))
		goto bail_out;

	conf_free_list(conf);
	free(transform);
	free(transform_len);
	return 0;

bail_out:
	free(sa_buf);
	free(proposal);
	if (transform) {
		for (i = 0; i < conf->cnt; i++)
			free(transform[i]);
		free(transform);
	}
	free(transform_len);
	conf_free_list(conf);
	return -1;
}

/* Figure out what transform the responder chose.  */
int
ike_phase_1_initiator_recv_SA(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct sa      *sa = TAILQ_FIRST(&exchange->sa_list);
	struct ipsec_exch *ie = exchange->data;
	struct ipsec_sa *isa = sa->data;
	struct payload *sa_p = payload_first(msg, ISAKMP_PAYLOAD_SA);
	struct payload *prop = payload_first(msg, ISAKMP_PAYLOAD_PROPOSAL);
	struct payload *xf = payload_first(msg, ISAKMP_PAYLOAD_TRANSFORM);

	/*
	 * IKE requires that only one SA with only one proposal exists and
	 * since we are getting an answer on our transform offer, only one
	 * transform.
	 */
	if (TAILQ_NEXT(sa_p, link) || TAILQ_NEXT(prop, link) ||
	    TAILQ_NEXT(xf, link)) {
		log_print("ike_phase_1_initiator_recv_SA: "
		    "multiple SA, proposal or transform payloads in phase 1");
		/* XXX Is there a better notification type?  */
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		return -1;
	}
	/* Check that the chosen transform matches an offer.  */
	if (message_negotiate_sa(msg, ike_phase_1_validate_prop) ||
	    !TAILQ_FIRST(&sa->protos))
		return -1;

	ipsec_decode_transform(msg, sa, TAILQ_FIRST(&sa->protos), xf->p);

	/* XXX I don't like exchange-specific stuff in here.  */
	if (exchange->type != ISAKMP_EXCH_AGGRESSIVE)
		ie->group = group_get(isa->group_desc);

	/* Mark the SA as handled.  */
	sa_p->flags |= PL_MARK;

	return 0;
}

/* Send our public DH value and a nonce to the responder.  */
int
ike_phase_1_initiator_send_KE_NONCE(struct message *msg)
{
	struct ipsec_exch *ie = msg->exchange->data;

	ie->g_x_len = dh_getlen(ie->group);

	/* XXX I want a better way to specify the nonce's size.  */
	return ike_phase_1_send_KE_NONCE(msg, 16);
}

/* Accept responder's public DH value and nonce.  */
int
ike_phase_1_initiator_recv_KE_NONCE(struct message *msg)
{
	if (ike_phase_1_recv_KE_NONCE(msg))
		return -1;

	return ike_phase_1_post_exchange_KE_NONCE(msg);
}

/*
 * Accept a set of transforms offered by the initiator and chose one we can
 * handle.
 */
int
ike_phase_1_responder_recv_SA(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct sa      *sa = TAILQ_FIRST(&exchange->sa_list);
	struct ipsec_sa *isa = sa->data;
	struct payload *sa_p = payload_first(msg, ISAKMP_PAYLOAD_SA);
	struct payload *prop = payload_first(msg, ISAKMP_PAYLOAD_PROPOSAL);
	struct ipsec_exch *ie = exchange->data;

	/* Mark the SA as handled.  */
	sa_p->flags |= PL_MARK;

	/* IKE requires that only one SA with only one proposal exists.  */
	if (TAILQ_NEXT(sa_p, link) || TAILQ_NEXT(prop, link)) {
		log_print("ike_phase_1_responder_recv_SA: "
		    "multiple SA or proposal payloads in phase 1");
		/* XXX Is there a better notification type?  */
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		return -1;
	}
	/* Chose a transform from the SA.  */
	if (message_negotiate_sa(msg, ike_phase_1_validate_prop) ||
	    !TAILQ_FIRST(&sa->protos))
		return -1;

	/* XXX Move into message_negotiate_sa?  */
	ipsec_decode_transform(msg, sa, TAILQ_FIRST(&sa->protos),
	    TAILQ_FIRST(&sa->protos)->chosen->p);

	ie->group = group_get(isa->group_desc);

	/*
	 * Check that the mandatory attributes: encryption, hash,
	 * authentication method and Diffie-Hellman group description, has
	 * been supplied.
	 */
	if (!exchange->crypto || !ie->hash || !ie->ike_auth || !ie->group) {
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		return -1;
	}
	/* Save the body for later hash computation.  */
	ie->sa_i_b_len = GET_ISAKMP_GEN_LENGTH(sa_p->p) - ISAKMP_GEN_SZ;
	ie->sa_i_b = malloc(ie->sa_i_b_len);
	if (!ie->sa_i_b) {
		/* XXX How to notify peer?  */
		log_error("ike_phase_1_responder_recv_SA: malloc (%lu) failed",
		    (unsigned long)ie->sa_i_b_len);
		return -1;
	}
	memcpy(ie->sa_i_b, sa_p->p + ISAKMP_GEN_SZ, ie->sa_i_b_len);
	return 0;
}

/* Reply with the transform we chose.  */
int
ike_phase_1_responder_send_SA(struct message *msg)
{
	/* Add the SA payload with the transform that was chosen.  */
	if (message_add_sa_payload(msg))
		return -1;

	/* Advertise OpenBSD isakmpd. */
	if (add_vendor_openbsd(msg))
		return -1;

	/* Advertise NAT-T capability.  */
	if (nat_t_add_vendor_payloads(msg))
		return -1;

	/* Advertise DPD capability.  */
	if (dpd_add_vendor_payload(msg))
		return -1;
	return 0;
}

/* Send our public DH value and a nonce to the peer.  */
int
ike_phase_1_send_KE_NONCE(struct message *msg, size_t nonce_sz)
{
	/* Public DH key.  */
	if (ipsec_gen_g_x(msg)) {
		/* XXX How to log and notify peer?  */
		return -1;
	}
	/* Generate a nonce, and add it to the message.  */
	if (exchange_gen_nonce(msg, nonce_sz)) {
		/* XXX Log?  */
		return -1;
	}
	/* Are there any CERTREQs to send? */
	if (exchange_add_certreqs(msg)) {
		/* XXX Log? */
		return -1;
	}
	/* Try to add certificates which are acceptable for the CERTREQs */
	if (exchange_add_certs(msg)) {
		/* XXX Log? */
		return -1;
	}
	/* If this exchange uses NAT-Traversal, add NAT-D payloads now.  */
	if (msg->exchange->flags & EXCHANGE_FLAG_NAT_T_CAP_PEER)
		if (nat_t_exchange_add_nat_d(msg)) {
			/* XXX Log? */
			return -1;
		}
	return 0;
}

/* Receive our peer's public DH value and nonce.  */
int
ike_phase_1_recv_KE_NONCE(struct message *msg)
{
	/* Copy out the initiator's DH public value.  */
	if (ipsec_save_g_x(msg)) {
		/* XXX How to log and notify peer?  */
		return -1;
	}
	/* Copy out the initiator's nonce.  */
	if (exchange_save_nonce(msg)) {
		/* XXX How to log and notify peer?  */
		return -1;
	}
	/* Copy out the initiator's cert requests.  */
	if (exchange_save_certreq(msg)) {
		/* XXX How to log and notify peer?  */
		return -1;
	}
	/* MainMode: Check for NAT-D payloads and contents.  */
	if (msg->exchange->type == ISAKMP_EXCH_ID_PROT &&
	    msg->exchange->flags & EXCHANGE_FLAG_NAT_T_CAP_PEER)
		(void)nat_t_exchange_check_nat_d(msg);
	return 0;
}

/*
 * Compute DH values and key material.  This is done in a post-send function
 * as that means we can do parallel work in both the initiator and responder
 * thus speeding up exchanges.
 */
int
ike_phase_1_post_exchange_KE_NONCE(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct prf     *prf;
	struct hash    *hash = ie->hash;
	enum cryptoerr  err;

	/* Compute Diffie-Hellman shared value.  */
	ie->g_xy_len = dh_secretlen(ie->group);
	ie->g_xy = malloc(ie->g_xy_len);
	if (!ie->g_xy) {
		/* XXX How to notify peer?  */
		log_error("ike_phase_1_post_exchange_KE_NONCE: "
		    "malloc (%lu) failed", (unsigned long)ie->g_xy_len);
		return -1;
	}
	if (dh_create_shared(ie->group, ie->g_xy,
	    exchange->initiator ? ie->g_xr : ie->g_xi)) {
		log_print("ike_phase_1_post_exchange_KE_NONCE: "
		    "dh_create_shared failed");
		return -1;
	}
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "ike_phase_1_post_exchange_KE_NONCE: g^xy", ie->g_xy,
	    ie->g_xy_len));

	/* Compute the SKEYID depending on the authentication method.  */
	ie->skeyid = ie->ike_auth->gen_skeyid(exchange, &ie->skeyid_len);
	if (!ie->skeyid) {
		/* XXX Log and teardown?  */
		return -1;
	}
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "ike_phase_1_post_exchange_KE_NONCE: SKEYID", ie->skeyid,
	    ie->skeyid_len));

	/* SKEYID_d.  */
	ie->skeyid_d = malloc(ie->skeyid_len);
	if (!ie->skeyid_d) {
		/* XXX How to notify peer?  */
		log_error("ike_phase_1_post_exchange_KE_NONCE: "
		    "malloc (%lu) failed", (unsigned long)ie->skeyid_len);
		return -1;
	}
	prf = prf_alloc(ie->prf_type, hash->type, ie->skeyid, ie->skeyid_len);
	if (!prf) {
		/* XXX Log and teardown?  */
		return -1;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, ie->g_xy, ie->g_xy_len);
	prf->Update(prf->prfctx, exchange->cookies, ISAKMP_HDR_COOKIES_LEN);
	prf->Update(prf->prfctx, (unsigned char *)"\0", 1);
	prf->Final(ie->skeyid_d, prf->prfctx);
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "ike_phase_1_post_exchange_KE_NONCE: SKEYID_d", ie->skeyid_d,
	    ie->skeyid_len));

	/* SKEYID_a.  */
	ie->skeyid_a = malloc(ie->skeyid_len);
	if (!ie->skeyid_a) {
		log_error("ike_phase_1_post_exchange_KE_NONCE: "
		    "malloc (%lu) failed", (unsigned long)ie->skeyid_len);
		prf_free(prf);
		return -1;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, ie->skeyid_d, ie->skeyid_len);
	prf->Update(prf->prfctx, ie->g_xy, ie->g_xy_len);
	prf->Update(prf->prfctx, exchange->cookies, ISAKMP_HDR_COOKIES_LEN);
	prf->Update(prf->prfctx, (unsigned char *)"\1", 1);
	prf->Final(ie->skeyid_a, prf->prfctx);
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "ike_phase_1_post_exchange_KE_NONCE: SKEYID_a", ie->skeyid_a,
	    ie->skeyid_len));

	/* SKEYID_e.  */
	ie->skeyid_e = malloc(ie->skeyid_len);
	if (!ie->skeyid_e) {
		/* XXX How to notify peer?  */
		log_error("ike_phase_1_post_exchange_KE_NONCE: "
		    "malloc (%lu) failed", (unsigned long)ie->skeyid_len);
		prf_free(prf);
		return -1;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, ie->skeyid_a, ie->skeyid_len);
	prf->Update(prf->prfctx, ie->g_xy, ie->g_xy_len);
	prf->Update(prf->prfctx, exchange->cookies, ISAKMP_HDR_COOKIES_LEN);
	prf->Update(prf->prfctx, (unsigned char *)"\2", 1);
	prf->Final(ie->skeyid_e, prf->prfctx);
	prf_free(prf);
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "ike_phase_1_post_exchange_KE_NONCE: SKEYID_e", ie->skeyid_e,
	    ie->skeyid_len));

	/* Key length determination.  */
	if (!exchange->key_length)
		exchange->key_length = exchange->crypto->keymax;

	/* Derive a longer key from skeyid_e */
	if (ie->skeyid_len < exchange->key_length) {
		u_int16_t       len, keylen;
		u_int8_t       *key, *p;

		prf = prf_alloc(ie->prf_type, hash->type, ie->skeyid_e,
		    ie->skeyid_len);
		if (!prf) {
			/* XXX - notify peer */
			return -1;
		}
		/* Make keylen a multiple of prf->blocksize */
		keylen = exchange->key_length;
		if (keylen % prf->blocksize)
			keylen += prf->blocksize - (keylen % prf->blocksize);

		key = malloc(keylen);
		if (!key) {
			/* XXX - Notify peer.  */
			prf_free(prf);
			log_error("ike_phase_1_post_exchange_KE_NONCE: "
			    "malloc (%d) failed", keylen);
			return -1;
		}
		prf->Init(prf->prfctx);
		prf->Update(prf->prfctx, (unsigned char *)"\0", 1);
		prf->Final(key, prf->prfctx);

		for (len = prf->blocksize, p = key; len < exchange->key_length;
		    len += prf->blocksize, p += prf->blocksize) {
			prf->Init(prf->prfctx);
			prf->Update(prf->prfctx, p, prf->blocksize);
			prf->Final(p + prf->blocksize, prf->prfctx);
		}
		prf_free(prf);

		/* Setup our keystate using the derived encryption key.  */
		exchange->keystate = crypto_init(exchange->crypto, key,
		    exchange->key_length, &err);

		free(key);
	} else
		/* Setup our keystate using the raw skeyid_e.  */
		exchange->keystate = crypto_init(exchange->crypto,
		    ie->skeyid_e, exchange->key_length, &err);

	/* Special handling for DES weak keys.  */
	if (!exchange->keystate && err == EWEAKKEY &&
	    (exchange->key_length << 1) <= ie->skeyid_len) {
		log_print("ike_phase_1_post_exchange_KE_NONCE: "
		    "weak key, trying subseq. skeyid_e");
		exchange->keystate = crypto_init(exchange->crypto,
		    ie->skeyid_e + exchange->key_length,
		    exchange->key_length, &err);
	}
	if (!exchange->keystate) {
		log_print("ike_phase_1_post_exchange_KE_NONCE: "
		    "exchange->crypto->init () failed: %d", err);

		/*
		 * XXX We really need to know if problems are of transient
		 * nature or fatal (like failed assertions etc.)
		 */
		return -1;
	}
	/* Setup IV.  XXX Only for CBC transforms, no?  */
	hash->Init(hash->ctx);
	hash->Update(hash->ctx, ie->g_xi, ie->g_x_len);
	hash->Update(hash->ctx, ie->g_xr, ie->g_x_len);
	hash->Final(hash->digest, hash->ctx);
	crypto_init_iv(exchange->keystate, hash->digest,
	    exchange->crypto->blocksize);
	return 0;
}

int
ike_phase_1_responder_send_ID_AUTH(struct message *msg)
{
	if (ike_phase_1_send_ID(msg))
		return -1;

	return ike_phase_1_send_AUTH(msg);
}

int
ike_phase_1_send_ID(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	u_int8_t       *buf;
	char            header[80];
	ssize_t         sz;
	struct sockaddr *src;
	int             initiator = exchange->initiator;
	u_int8_t      **id;
	size_t         *id_len;
	char           *my_id = 0, *data;
	u_int8_t        id_type;
	sa_family_t	af = 0;

	/* Choose the right fields to fill-in.  */
	id = initiator ? &exchange->id_i : &exchange->id_r;
	id_len = initiator ? &exchange->id_i_len : &exchange->id_r_len;

	if (exchange->name)
		my_id = conf_get_str(exchange->name, "ID");

	if (!my_id)
		my_id = conf_get_str("General", "Default-phase-1-ID");

	msg->transport->vtbl->get_src(msg->transport, &src);
	sz = my_id ? ipsec_id_size(my_id, &id_type) : sockaddr_addrlen(src);
	if (sz == -1)
		return -1;

	sz += ISAKMP_ID_DATA_OFF;
	buf = malloc(sz);
	if (!buf) {
		log_error("ike_phase_1_send_ID: malloc (%lu) failed",
			  (unsigned long)sz);
		return -1;
	}
	SET_IPSEC_ID_PROTO(buf + ISAKMP_ID_DOI_DATA_OFF, 0);
	SET_IPSEC_ID_PORT(buf + ISAKMP_ID_DOI_DATA_OFF, 0);
	if (my_id) {
		SET_ISAKMP_ID_TYPE(buf, id_type);
		switch (id_type) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV4_ADDR_SUBNET:
			af = AF_INET;
			break;
		case IPSEC_ID_IPV6_ADDR:
		case IPSEC_ID_IPV6_ADDR_SUBNET:
			af = AF_INET6;
			break;
		}
		switch (id_type) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV6_ADDR:
			data = conf_get_str(my_id, "Address");
			if (!data) {
				log_print("ike_phase_1_send_ID: section %s "
				    "has no \"Address\" tag", my_id);
				free(buf);
				return -1;
			}
			if (text2sockaddr(data, NULL, &src, af, 0)) {
				log_error("ike_phase_1_send_ID: "
				    "text2sockaddr() failed");
				free(buf);
				return -1;
			}
			memcpy(buf + ISAKMP_ID_DATA_OFF,
			    sockaddr_addrdata(src), sockaddr_addrlen(src));
			free(src);
			break;

		case IPSEC_ID_IPV4_ADDR_SUBNET:
		case IPSEC_ID_IPV6_ADDR_SUBNET:
			/* Network */
			data = conf_get_str(my_id, "Network");
			if (!data) {
				log_print("ike_phase_1_send_ID: section %s "
				    "has no \"Network\" tag", my_id);
				free(buf);
				return -1;
			}
			if (text2sockaddr(data, NULL, &src, af, 0)) {
				log_error("ike_phase_1_send_ID: "
				    "text2sockaddr() failed");
				free(buf);
				return -1;
			}
			memcpy(buf + ISAKMP_ID_DATA_OFF,
			    sockaddr_addrdata(src), sockaddr_addrlen(src));
			free(src);
			/* Netmask */
			data = conf_get_str(my_id, "Netmask");
			if (!data) {
				log_print("ike_phase_1_send_ID: section %s "
				    "has no \"Netmask\" tag", my_id);
				free(buf);
				return -1;
			}
			if (text2sockaddr(data, NULL, &src, af, 1)) {
				log_error("ike_phase_1_send_ID: "
				    "text2sockaddr() failed");
				free(buf);
				return -1;
			}
			memcpy(buf + ISAKMP_ID_DATA_OFF +
			    sockaddr_addrlen(src), sockaddr_addrdata(src),
			    sockaddr_addrlen(src));
			free(src);
			break;

		case IPSEC_ID_FQDN:
		case IPSEC_ID_USER_FQDN:
		case IPSEC_ID_KEY_ID:
			data = conf_get_str(my_id, "Name");
			if (!data) {
				log_print("ike_phase_1_send_ID: section %s "
				    "has no \"Name\" tag", my_id);
				free(buf);
				return -1;
			}
			memcpy(buf + ISAKMP_ID_DATA_OFF, data,
			    sz - ISAKMP_ID_DATA_OFF);
			break;

		default:
			log_print("ike_phase_1_send_ID: "
			    "unsupported ID type %d", id_type);
			free(buf);
			return -1;
		}
	} else {
		switch (src->sa_family) {
		case AF_INET:
			SET_ISAKMP_ID_TYPE(buf, IPSEC_ID_IPV4_ADDR);
			break;
		case AF_INET6:
			SET_ISAKMP_ID_TYPE(buf, IPSEC_ID_IPV6_ADDR);
			break;
		}
		/* Already in network byteorder.  */
		memcpy(buf + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(src),
		    sockaddr_addrlen(src));
	}

	if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, buf, sz, 1)) {
		free(buf);
		return -1;
	}
	*id_len = sz - ISAKMP_GEN_SZ;
	*id = malloc(*id_len);
	if (!*id) {
		log_error("ike_phase_1_send_ID: malloc (%lu) failed",
		    (unsigned long)*id_len);
		return -1;
	}
	memcpy(*id, buf + ISAKMP_GEN_SZ, *id_len);
	snprintf(header, sizeof header, "ike_phase_1_send_ID: %s",
	    constant_name(ipsec_id_cst, GET_ISAKMP_ID_TYPE(buf)));
	LOG_DBG_BUF((LOG_NEGOTIATION, 40, header, buf + ISAKMP_ID_DATA_OFF,
	    sz - ISAKMP_ID_DATA_OFF));
	return 0;
}

int
ike_phase_1_send_AUTH(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;

	if (ie->ike_auth->encode_hash(msg)) {
		/* XXX Log? */
		return -1;
	}
	/*
	 * XXX Many people say the COMMIT flag is just junk, especially in
	 * Phase 1.
	 */
#ifdef notyet
	if ((exchange->flags & EXCHANGE_FLAG_COMMITTED) == 0)
		exchange->flags |= EXCHANGE_FLAG_I_COMMITTED;
#endif

	return 0;
}

/* Receive ID and HASH and check that the exchange has been consistent.  */
int
ike_phase_1_recv_ID_AUTH(struct message *msg)
{
	if (ike_phase_1_recv_ID(msg))
		return -1;

	return ike_phase_1_recv_AUTH(msg);
}

/* Receive ID.  */
int
ike_phase_1_recv_ID(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct payload *payload;
	char            header[80], *rs = 0, *rid = 0, *p;
	int             initiator = exchange->initiator;
	u_int8_t      **id, id_type;
	size_t         *id_len;
	ssize_t         sz;
	struct sockaddr *sa;
	sa_family_t	af = 0;

	payload = payload_first(msg, ISAKMP_PAYLOAD_ID);

	if (exchange->name)
		rs = conf_get_str(exchange->name, "Remote-ID");

	if (rs) {
		sz = ipsec_id_size(rs, &id_type);
		if (sz == -1) {
			log_print("ike_phase_1_recv_ID: could not handle "
			    "specified Remote-ID [%s]", rs);
			return -1;
		}
		rid = malloc(sz);
		if (!rid) {
			log_error("ike_phase_1_recv_ID: malloc (%lu) failed",
			    (unsigned long)sz);
			return -1;
		}
		switch (id_type) {
		case IPSEC_ID_IPV4_ADDR:
			af = AF_INET;
			break;
		case IPSEC_ID_IPV6_ADDR:
			af = AF_INET6;
			break;
		}
		switch (id_type) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV6_ADDR:
			p = conf_get_str(rs, "Address");
			if (!p) {
				log_print("ike_phase_1_recv_ID: failed to get "
				    "Address in Remote-ID section [%s]", rs);
				free(rid);
				return -1;
			}
			if (text2sockaddr(p, 0, &sa, af, 0) == -1) {
				log_print("ike_phase_1_recv_ID: "
				    "failed to parse address %s", p);
				free(rid);
				return -1;
			}
			if ((id_type == IPSEC_ID_IPV4_ADDR &&
			    sa->sa_family != AF_INET) ||
			    (id_type == IPSEC_ID_IPV6_ADDR &&
			    sa->sa_family != AF_INET6)) {
				log_print("ike_phase_1_recv_ID: "
				    "address %s not of expected family", p);
				free(rid);
				free(sa);
				return -1;
			}
			memcpy(rid, sockaddr_addrdata(sa),
			    sockaddr_addrlen(sa));
			free(sa);
			break;

		case IPSEC_ID_FQDN:
		case IPSEC_ID_USER_FQDN:
		case IPSEC_ID_KEY_ID:
			p = conf_get_str(rs, "Name");
			if (!p) {
				log_print("ike_phase_1_recv_ID: failed to "
				    "get Name in Remote-ID section [%s]", rs);
				free(rid);
				return -1;
			}
			memcpy(rid, p, sz);
			break;

		default:
			log_print("ike_phase_1_recv_ID: "
			    "unsupported ID type %d", id_type);
			free(rid);
			return -1;
		}

		/* Compare expected/desired and received remote ID */
		if (memcmp(rid, payload->p + ISAKMP_ID_DATA_OFF, sz) != 0) {
			free(rid);
			log_print("ike_phase_1_recv_ID: "
			    "received remote ID other than expected %s", p);
			return -1;
		}
		free(rid);
	}
	/* Choose the right fields to fill in */
	id = initiator ? &exchange->id_r : &exchange->id_i;
	id_len = initiator ? &exchange->id_r_len : &exchange->id_i_len;

	*id_len = GET_ISAKMP_GEN_LENGTH(payload->p) - ISAKMP_GEN_SZ;
	*id = malloc(*id_len);
	if (!*id) {
		log_error("ike_phase_1_recv_ID: malloc (%lu) failed",
		    (unsigned long)*id_len);
		return -1;
	}
	memcpy(*id, payload->p + ISAKMP_GEN_SZ, *id_len);
	snprintf(header, sizeof header, "ike_phase_1_recv_ID: %s",
	    constant_name(ipsec_id_cst, GET_ISAKMP_ID_TYPE(payload->p)));
	LOG_DBG_BUF((LOG_NEGOTIATION, 40, header,
	    payload->p + ISAKMP_ID_DATA_OFF,
	    *id_len + ISAKMP_GEN_SZ - ISAKMP_ID_DATA_OFF));
	payload->flags |= PL_MARK;
	return 0;
}

/* Receive HASH and check that the exchange has been consistent.  */
int
ike_phase_1_recv_AUTH(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct prf     *prf;
	struct hash    *hash = ie->hash;
	char            header[80];
	size_t          hashsize = hash->hashsize;
	int             initiator = exchange->initiator;
	u_int8_t      **hash_p, *id;
	size_t          id_len;

	/* Choose the right fields to fill in */
	hash_p = initiator ? &ie->hash_r : &ie->hash_i;
	id = initiator ? exchange->id_r : exchange->id_i;
	id_len = initiator ? exchange->id_r_len : exchange->id_i_len;

	/* The decoded hash will be in ie->hash_r or ie->hash_i */
	if (ie->ike_auth->decode_hash(msg)) {
		message_drop(msg, ISAKMP_NOTIFY_INVALID_ID_INFORMATION, 0, 1,
		    0);
		return -1;
	}
	/* Allocate the prf and start calculating his HASH.  */
	prf = prf_alloc(ie->prf_type, hash->type, ie->skeyid, ie->skeyid_len);
	if (!prf) {
		/* XXX Log?  */
		return -1;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, initiator ? ie->g_xr : ie->g_xi, ie->g_x_len);
	prf->Update(prf->prfctx, initiator ? ie->g_xi : ie->g_xr, ie->g_x_len);
	prf->Update(prf->prfctx, exchange->cookies +
	    (initiator ? ISAKMP_HDR_RCOOKIE_OFF : ISAKMP_HDR_ICOOKIE_OFF),
	    ISAKMP_HDR_ICOOKIE_LEN);
	prf->Update(prf->prfctx, exchange->cookies +
	    (initiator ? ISAKMP_HDR_ICOOKIE_OFF : ISAKMP_HDR_RCOOKIE_OFF),
	    ISAKMP_HDR_ICOOKIE_LEN);
	prf->Update(prf->prfctx, ie->sa_i_b, ie->sa_i_b_len);
	prf->Update(prf->prfctx, id, id_len);
	prf->Final(hash->digest, prf->prfctx);
	prf_free(prf);
	snprintf(header, sizeof header, "ike_phase_1_recv_AUTH: "
	    "computed HASH_%c", initiator ? 'R' : 'I');
	LOG_DBG_BUF((LOG_NEGOTIATION, 80, header, hash->digest, hashsize));

	/* Check that the hash we got matches the one we computed.  */
	if (memcmp(*hash_p, hash->digest, hashsize) != 0) {
		/* XXX Log?  */
		return -1;
	}

	/* Mark message as authenticated. */
	msg->flags |= MSG_AUTHENTICATED;

	return 0;
}

struct attr_node {
	LIST_ENTRY(attr_node) link;
	u_int16_t       type;
};

struct validation_state {
	struct conf_list_node *xf;
	LIST_HEAD(attr_head, attr_node) attrs;
	char           *life;
};

/* Validate a proposal inside SA according to EXCHANGE's policy.  */
static int
ike_phase_1_validate_prop(struct exchange *exchange, struct sa *sa,
    struct sa *isakmp_sa)
{
	struct conf_list *conf, *tags;
	struct conf_list_node *xf, *tag;
	struct proto   *proto;
	struct validation_state vs;
	struct attr_node *node, *next_node;

	/* Get the list of transforms.  */
	conf = conf_get_list(exchange->policy, "Transforms");
	if (!conf)
		return 0;

	for (xf = TAILQ_FIRST(&conf->fields); xf; xf = TAILQ_NEXT(xf, link)) {
		for (proto = TAILQ_FIRST(&sa->protos); proto;
		    proto = TAILQ_NEXT(proto, link)) {
			/* Mark all attributes in our policy as unseen.  */
			LIST_INIT(&vs.attrs);
			vs.xf = xf;
			vs.life = 0;
			if (attribute_map(proto->chosen->p +
			    ISAKMP_TRANSFORM_SA_ATTRS_OFF,
			    GET_ISAKMP_GEN_LENGTH(proto->chosen->p) -
			    ISAKMP_TRANSFORM_SA_ATTRS_OFF,
			    attribute_unacceptable, &vs))
				goto try_next;

			/* Sweep over unseen tags in this section.  */
			tags = conf_get_tag_list(xf->field);
			if (tags) {
				for (tag = TAILQ_FIRST(&tags->fields); tag;
				    tag = TAILQ_NEXT(tag, link))
					/*
					 * XXX Should we care about attributes
					 * we have, they do not provide?
					 */
					for (node = LIST_FIRST(&vs.attrs);
					    node; node = next_node) {
						next_node =
						    LIST_NEXT(node, link);
						if (node->type ==
						    constant_value(ike_attr_cst,
						    tag->field)) {
							LIST_REMOVE(node, link);
							free(node);
						}
					}
				conf_free_list(tags);
			}
			/* Are there leftover tags in this section?  */
			node = LIST_FIRST(&vs.attrs);
			if (node)
				goto try_next;
		}

		/* All protocols were OK, we succeeded.  */
		LOG_DBG((LOG_NEGOTIATION, 20, "ike_phase_1_validate_prop: "
		    "success"));
		conf_free_list(conf);
		free(vs.life);
		return 1;

try_next:
		/* Are there leftover tags in this section?  */
		node = LIST_FIRST(&vs.attrs);
		while (node) {
			LIST_REMOVE(node, link);
			free(node);
			node = LIST_FIRST(&vs.attrs);
		}
		free(vs.life);
	}

	LOG_DBG((LOG_NEGOTIATION, 20, "ike_phase_1_validate_prop: failure"));
	conf_free_list(conf);
	return 0;
}

/*
 * Look at the attribute of type TYPE, located at VALUE for LEN bytes forward.
 * The VVS argument holds a validation state kept across invocations.
 * If the attribute is unacceptable to use, return non-zero, otherwise zero.
 */
static int
attribute_unacceptable(u_int16_t type, u_int8_t *value, u_int16_t len,
    void *vvs)
{
	struct validation_state *vs = vvs;
	struct conf_list *life_conf;
	struct conf_list_node *xf = vs->xf, *life;
	char           *tag = constant_lookup(ike_attr_cst, type);
	char           *str;
	struct constant_map *map;
	struct attr_node *node;
	int             rv, dur = 0;

	if (!tag) {
		log_print("attribute_unacceptable: "
		    "attribute type %d not known", type);
		return 1;
	}
	switch (type) {
	case IKE_ATTR_ENCRYPTION_ALGORITHM:
	case IKE_ATTR_HASH_ALGORITHM:
	case IKE_ATTR_AUTHENTICATION_METHOD:
	case IKE_ATTR_GROUP_DESCRIPTION:
	case IKE_ATTR_GROUP_TYPE:
	case IKE_ATTR_PRF:
		str = conf_get_str(xf->field, tag);
		if (!str) {
			/* This attribute does not exist in this policy.  */
			log_print("attribute_unacceptable: "
			    "attr %s does not exist in %s", tag, xf->field);
			return 1;
		}
		map = constant_link_lookup(ike_attr_cst, type);
		if (!map)
			return 1;

		if ((constant_value(map, str) == decode_16(value)) ||
		    (!strcmp(str, "ANY"))) {
			/* Mark this attribute as seen.  */
			node = malloc(sizeof *node);
			if (!node) {
				log_error("attribute_unacceptable: "
				    "malloc (%lu) failed",
				    (unsigned long)sizeof *node);
				return 1;
			}
			node->type = type;
			LIST_INSERT_HEAD(&vs->attrs, node, link);
			return 0;
		}
		log_print("attribute_unacceptable: %s: got %s, expected %s",
		    tag, constant_name(map, decode_16(value)), str);
		return 1;

	case IKE_ATTR_GROUP_PRIME:
	case IKE_ATTR_GROUP_GENERATOR_1:
	case IKE_ATTR_GROUP_GENERATOR_2:
	case IKE_ATTR_GROUP_CURVE_A:
	case IKE_ATTR_GROUP_CURVE_B:
		/* XXX Bignums not handled yet.  */
		log_print("attribute_unacceptable: "
		    "bignum type %d not supported", type);
		return 1;

	case IKE_ATTR_LIFE_TYPE:
	case IKE_ATTR_LIFE_DURATION:
		life_conf = conf_get_list(xf->field, "Life");
		if (life_conf &&
		    !strcmp(conf_get_str(xf->field, "Life"), "ANY")) {
			conf_free_list(life_conf);
			return 0;
		}

		rv = 1;
		if (!life_conf) {
			/* Life attributes given, but not in our policy.  */
			log_print("attribute_unacceptable: "
			    "life attribute received, none in policy");
			return 1;
		}
		/*
		 * Each lifetime type must match, otherwise we turn the
		 * proposal down. In order to do this we need to find the
		 * specific section of our policy's "Life" list and match
		 * its duration.
		 */
		switch (type) {
		case IKE_ATTR_LIFE_TYPE:
			for (life = TAILQ_FIRST(&life_conf->fields); life;
			    life = TAILQ_NEXT(life, link)) {
				str = conf_get_str(life->field, "LIFE_TYPE");
				if (!str) {
					log_print("attribute_unacceptable: "
					    "section [%s] has no LIFE_TYPE",
					    life->field);
					continue;
				}

				/*
				 * If this is the type we are looking at,
				 * save a pointer to this section in vs->life.
				 */
				if (constant_value(ike_duration_cst, str) ==
				    decode_16(value)) {
					vs->life = strdup(life->field);
					rv = 0;
					goto bail_out;
				}
			}
			log_print("attribute_unacceptable: "
			    "unrecognized LIFE_TYPE %d", decode_16(value));
			vs->life = 0;
			break;

		case IKE_ATTR_LIFE_DURATION:
			if (!vs->life) {
				log_print("attribute_unacceptable: "
				    "LIFE_DURATION without LIFE_TYPE");
				rv = 1;
				goto bail_out;
			}
			str = conf_get_str(vs->life, "LIFE_DURATION");
			if (str) {
				if (!strcmp(str, "ANY"))
					rv = 0;
				else {
					dur = (len == 4) ? decode_32(value) :
					    decode_16(value);
					if ((rv = !conf_match_num(vs->life,
					    "LIFE_DURATION", dur))) {
						log_print(
						    "attribute_unacceptable: "
						    "LIFE_DURATION: got %d, "
						    " expected %s", dur, str);
					}
				}
			} else {
				log_print("attribute_unacceptable: "
				    "section [%s] has no LIFE_DURATION",
				    vs->life);
				rv = 1;
			}

			free(vs->life);
			vs->life = 0;
			break;
		}

bail_out:
		conf_free_list(life_conf);
		return rv;

	case IKE_ATTR_KEY_LENGTH:
	case IKE_ATTR_FIELD_SIZE:
	case IKE_ATTR_GROUP_ORDER:
		if (conf_match_num(xf->field, tag, decode_16(value))) {
			/* Mark this attribute as seen.  */
			node = malloc(sizeof *node);
			if (!node) {
				log_error("attribute_unacceptable: "
				    "malloc (%lu) failed",
				    (unsigned long)sizeof *node);
				return 1;
			}
			node->type = type;
			LIST_INSERT_HEAD(&vs->attrs, node, link);
			return 0;
		}
		return 1;
	default:
		log_print("attribute_unacceptable: unexpected type %d",
		    type);
	}
	return 1;
}
