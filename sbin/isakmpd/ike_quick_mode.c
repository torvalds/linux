/* $OpenBSD: ike_quick_mode.c,v 1.115 2023/03/31 20:16:55 tb Exp $	 */
/* $EOM: ike_quick_mode.c,v 1.139 2001/01/26 10:43:17 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2000, 2001, 2004 Håkan Olsson.  All rights reserved.
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

#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <regex.h>
#include <keynote.h>

#include "attribute.h"
#include "conf.h"
#include "connection.h"
#include "dh.h"
#include "doi.h"
#include "exchange.h"
#include "hash.h"
#include "ike_quick_mode.h"
#include "ipsec.h"
#include "log.h"
#include "message.h"
#include "policy.h"
#include "prf.h"
#include "sa.h"
#include "transport.h"
#include "util.h"
#include "key.h"
#include "x509.h"

static void     gen_g_xy(struct message *);
static int      initiator_send_HASH_SA_NONCE(struct message *);
static int      initiator_recv_HASH_SA_NONCE(struct message *);
static int      initiator_send_HASH(struct message *);
static void     post_quick_mode(struct message *);
static int      responder_recv_HASH_SA_NONCE(struct message *);
static int      responder_send_HASH_SA_NONCE(struct message *);
static int      responder_recv_HASH(struct message *);

static int      check_policy(struct exchange *, struct sa *, struct sa *);

int	(*ike_quick_mode_initiator[])(struct message *) = {
	initiator_send_HASH_SA_NONCE,
	initiator_recv_HASH_SA_NONCE,
	initiator_send_HASH
};

int	(*ike_quick_mode_responder[])(struct message *) = {
	responder_recv_HASH_SA_NONCE,
	responder_send_HASH_SA_NONCE,
	responder_recv_HASH
};

/* How many return values will policy handle -- true/false for now */
#define RETVALUES_NUM 2

/*
 * Given an exchange and our policy, check whether the SA and IDs are
 * acceptable.
 */
static int
check_policy(struct exchange *exchange, struct sa *sa, struct sa *isakmp_sa)
{
	char           *return_values[RETVALUES_NUM];
	char          **principal = 0;
	int             i, len, result = 0, nprinc = 0;
	int            *x509_ids = 0, *keynote_ids = 0;
	unsigned char   hashbuf[20];	/* Set to the largest digest result */
	struct keynote_deckey dc;
	X509_NAME      *subject;

	/* Do we want to use keynote policies? */
	if (ignore_policy ||
	    strncmp("yes", conf_get_str("General", "Use-Keynote"), 3))
		return 1;

	/* Initialize if necessary -- e.g., if pre-shared key auth was used */
	if (isakmp_sa->policy_id < 0) {
		if ((isakmp_sa->policy_id = kn_init()) == -1) {
			log_print("check_policy: "
			    "failed to initialize policy session");
			return 0;
		}
	}
	/* Add the callback that will handle attributes.  */
	if (kn_add_action(isakmp_sa->policy_id, ".*", (char *)policy_callback,
	    ENVIRONMENT_FLAG_FUNC | ENVIRONMENT_FLAG_REGEX) == -1) {
		log_print("check_policy: "
		    "kn_add_action (%d, \".*\", %p, FUNC | REGEX) failed",
		    isakmp_sa->policy_id, policy_callback);
		kn_close(isakmp_sa->policy_id);
		isakmp_sa->policy_id = -1;
		return 0;
	}
	if (policy_asserts_num) {
		keynote_ids = calloc(policy_asserts_num, sizeof *keynote_ids);
		if (!keynote_ids) {
			log_error("check_policy: calloc (%d, %lu) failed",
			    policy_asserts_num,
			    (unsigned long)sizeof *keynote_ids);
			kn_close(isakmp_sa->policy_id);
			isakmp_sa->policy_id = -1;
			return 0;
		}
	}
	/* Add the policy assertions */
	for (i = 0; i < policy_asserts_num; i++)
		keynote_ids[i] = kn_add_assertion(isakmp_sa->policy_id,
		    policy_asserts[i],
		    strlen(policy_asserts[i]), ASSERT_FLAG_LOCAL);

	/* Initialize -- we'll let the callback do all the work.  */
	policy_exchange = exchange;
	policy_sa = sa;
	policy_isakmp_sa = isakmp_sa;

	/* Set the return values; true/false for now at least.  */
	return_values[0] = "false";	/* Order of values in array is
					 * important.  */
	return_values[1] = "true";

	/* Create a principal (authorizer) for the SA/ID request.  */
	switch (isakmp_sa->recv_certtype) {
	case ISAKMP_CERTENC_NONE:
		/*
		 * For shared keys, just duplicate the passphrase with the
		 * appropriate prefix tag.
		 */
		nprinc = 3;
		principal = calloc(nprinc, sizeof *principal);
		if (!principal) {
			log_error("check_policy: calloc (%d, %lu) failed",
			    nprinc, (unsigned long)sizeof *principal);
			goto policydone;
		}
		len = strlen(isakmp_sa->recv_key) + sizeof "passphrase:";
		principal[0] = calloc(len, sizeof(char));
		if (!principal[0]) {
			log_error("check_policy: calloc (%d, %lu) failed", len,
			    (unsigned long)sizeof(char));
			goto policydone;
		}
		/*
		 * XXX Consider changing the magic hash lengths with
		 * constants.
		 */
		strlcpy(principal[0], "passphrase:", len);
		memcpy(principal[0] + sizeof "passphrase:" - 1,
		    isakmp_sa->recv_key, strlen(isakmp_sa->recv_key));

		len = sizeof "passphrase-md5-hex:" + 2 * 16;
		principal[1] = calloc(len, sizeof(char));
		if (!principal[1]) {
			log_error("check_policy: calloc (%d, %lu) failed", len,
			    (unsigned long)sizeof(char));
			goto policydone;
		}
		strlcpy(principal[1], "passphrase-md5-hex:", len);
		MD5(isakmp_sa->recv_key, strlen(isakmp_sa->recv_key), hashbuf);
		for (i = 0; i < 16; i++)
			snprintf(principal[1] + 2 * i +
			    sizeof "passphrase-md5-hex:" - 1, 3, "%02x",
			    hashbuf[i]);

		len = sizeof "passphrase-sha1-hex:" + 2 * 20;
		principal[2] = calloc(len, sizeof(char));
		if (!principal[2]) {
			log_error("check_policy: calloc (%d, %lu) failed", len,
			    (unsigned long)sizeof(char));
			goto policydone;
		}
		strlcpy(principal[2], "passphrase-sha1-hex:", len);
		SHA1(isakmp_sa->recv_key, strlen(isakmp_sa->recv_key),
		    hashbuf);
		for (i = 0; i < 20; i++)
			snprintf(principal[2] + 2 * i +
			    sizeof "passphrase-sha1-hex:" - 1, 3, "%02x",
			    hashbuf[i]);
		break;

	case ISAKMP_CERTENC_KEYNOTE:
		nprinc = 1;

		principal = calloc(nprinc, sizeof *principal);
		if (!principal) {
			log_error("check_policy: calloc (%d, %lu) failed",
			    nprinc, (unsigned long)sizeof *principal);
			goto policydone;
		}
		/* Dup the keys */
		principal[0] = strdup(isakmp_sa->keynote_key);
		if (!principal[0]) {
			log_error("check_policy: calloc (%lu, %lu) failed",
			    (unsigned long)strlen(isakmp_sa->keynote_key),
			    (unsigned long)sizeof(char));
			goto policydone;
		}
		break;

	case ISAKMP_CERTENC_X509_SIG:
		principal = calloc(2, sizeof *principal);
		if (!principal) {
			log_error("check_policy: calloc (2, %lu) failed",
			    (unsigned long)sizeof *principal);
			goto policydone;
		}
		if (isakmp_sa->recv_keytype == ISAKMP_KEY_RSA)
			dc.dec_algorithm = KEYNOTE_ALGORITHM_RSA;
		else {
			log_error("check_policy: "
			    "unknown/unsupported public key algorithm %d",
			    isakmp_sa->recv_keytype);
			goto policydone;
		}

		dc.dec_key = isakmp_sa->recv_key;
		principal[0] = kn_encode_key(&dc, INTERNAL_ENC_PKCS1,
		    ENCODING_HEX, KEYNOTE_PUBLIC_KEY);
		if (keynote_errno == ERROR_MEMORY) {
			log_print("check_policy: "
			    "failed to get memory for public key");
			goto policydone;
		}
		if (!principal[0]) {
			log_print("check_policy: "
			    "failed to allocate memory for principal");
			goto policydone;
		}
		if (asprintf(&principal[1], "rsa-hex:%s", principal[0]) == -1) {
			log_error("check_policy: asprintf() failed");
			goto policydone;
		}
		free(principal[0]);
		principal[0] = principal[1];
		principal[1] = 0;

		/* Generate a "DN:" principal.  */
		subject = X509_get_subject_name(isakmp_sa->recv_cert);
		if (subject) {
			principal[1] = calloc(259, sizeof(char));
			if (!principal[1]) {
				log_error("check_policy: "
				    "calloc (259, %lu) failed",
				    (unsigned long)sizeof(char));
				goto policydone;
			}
			strlcpy(principal[1], "DN:", 259);
			X509_NAME_oneline(subject, principal[1] + 3, 256);
			nprinc = 2;
		} else {
			nprinc = 1;
		}
		break;

		/* XXX Eventually handle these.  */
	case ISAKMP_CERTENC_PKCS:
	case ISAKMP_CERTENC_PGP:
	case ISAKMP_CERTENC_DNS:
	case ISAKMP_CERTENC_X509_KE:
	case ISAKMP_CERTENC_KERBEROS:
	case ISAKMP_CERTENC_CRL:
	case ISAKMP_CERTENC_ARL:
	case ISAKMP_CERTENC_SPKI:
	case ISAKMP_CERTENC_X509_ATTR:
	default:
		log_print("check_policy: "
		    "unknown/unsupported certificate/authentication method %d",
		    isakmp_sa->recv_certtype);
		goto policydone;
	}

	/*
	 * Add the authorizer (who is requesting the SA/ID);
	 * this may be a public or a secret key, depending on
	 * what mode of authentication we used in Phase 1.
	 */
	for (i = 0; i < nprinc; i++) {
		LOG_DBG((LOG_POLICY, 40, "check_policy: "
		    "adding authorizer [%s]", principal[i]));

		if (kn_add_authorizer(isakmp_sa->policy_id, principal[i])
		    == -1) {
			int	j;

			for (j = 0; j < i; j++)
				kn_remove_authorizer(isakmp_sa->policy_id,
				    principal[j]);
			log_print("check_policy: kn_add_authorizer failed");
			goto policydone;
		}
	}

	/* Ask policy */
	result = kn_do_query(isakmp_sa->policy_id, return_values,
	    RETVALUES_NUM);
	LOG_DBG((LOG_POLICY, 40, "check_policy: kn_do_query returned %d",
	    result));

	/* Cleanup environment */
	kn_cleanup_action_environment(isakmp_sa->policy_id);

	/* Remove authorizers from the session */
	for (i = 0; i < nprinc; i++) {
		kn_remove_authorizer(isakmp_sa->policy_id, principal[i]);
		free(principal[i]);
	}

	free(principal);
	principal = 0;
	nprinc = 0;

	/* Check what policy said.  */
	if (result < 0) {
		LOG_DBG((LOG_POLICY, 40, "check_policy: proposal refused"));
		result = 0;
		goto policydone;
	}
policydone:
	for (i = 0; i < nprinc; i++)
		if (principal && principal[i])
			free(principal[i]);

	free(principal);

	/* Remove the policies */
	for (i = 0; i < policy_asserts_num; i++) {
		if (keynote_ids[i] != -1)
			kn_remove_assertion(isakmp_sa->policy_id,
			    keynote_ids[i]);
	}

	free(keynote_ids);

	free(x509_ids);

	/*
	 * XXX Currently, check_policy() is only called from
	 * message_negotiate_sa(), and so this log message reflects this.
	 * Change to something better?
	 */
	if (result == 0)
		log_print("check_policy: negotiated SA failed policy check");

	/*
	 * Given that we have only 2 return values from policy (true/false)
	 * we can just return the query result directly (no pre-processing
	 * needed).
	 */
	return result;
}

/*
 * Offer several sets of transforms to the responder.
 * XXX Split this huge function up and look for common code with main mode.
 */
static int
initiator_send_HASH_SA_NONCE(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct doi     *doi = exchange->doi;
	struct ipsec_exch *ie = exchange->data;
	u_int8_t     ***transform = 0, ***new_transform;
	u_int8_t      **proposal = 0, **new_proposal;
	u_int8_t       *sa_buf = 0, *attr, *saved_nextp_sa, *saved_nextp_prop,
	               *id, *spi;
	size_t          spi_sz, sz;
	size_t          proposal_len = 0, proposals_len = 0, sa_len;
	size_t        **transform_len = 0, **new_transform_len;
	size_t         *transforms_len = 0, *new_transforms_len;
	u_int32_t      *transform_cnt = 0, *new_transform_cnt;
	u_int32_t       suite_no, prop_no, prot_no, xf_no, prop_cnt = 0;
	u_int32_t       i;
	int             value, update_nextp, protocol_num, proto_id;
	struct proto   *proto;
	struct conf_list *suite_conf, *prot_conf = 0, *xf_conf = 0, *life_conf;
	struct conf_list_node *suite, *prot, *xf, *life;
	struct constant_map *id_map;
	char           *protocol_id, *transform_id;
	char           *local_id, *remote_id;
	char           *name;
	int             group_desc = -1, new_group_desc;
	struct ipsec_sa *isa = msg->isakmp_sa->data;
	struct hash    *hash = hash_get(isa->hash);
	struct sockaddr *src;
	struct proto_attr *pa;

	if (!ipsec_add_hash_payload(msg, hash->hashsize))
		return -1;

	/* Get the list of protocol suites.  */
	suite_conf = conf_get_list(exchange->policy, "Suites");
	if (!suite_conf)
		return -1;

	for (suite = TAILQ_FIRST(&suite_conf->fields), suite_no = prop_no = 0;
	    suite_no < suite_conf->cnt;
	    suite_no++, suite = TAILQ_NEXT(suite, link)) {
		/* Now get each protocol in this specific protocol suite.  */
		prot_conf = conf_get_list(suite->field, "Protocols");
		if (!prot_conf)
			goto bail_out;

		for (prot = TAILQ_FIRST(&prot_conf->fields), prot_no = 0;
		    prot_no < prot_conf->cnt;
		    prot_no++, prot = TAILQ_NEXT(prot, link)) {
			/* Make sure we have a proposal/transform vectors.  */
			if (prop_no >= prop_cnt) {
				/*
				 * This resize algorithm is completely
				 * arbitrary.
				 */
				prop_cnt = 2 * prop_cnt + 10;
				new_proposal = reallocarray(proposal,
				    prop_cnt, sizeof *proposal);
				if (!new_proposal) {
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "realloc (%p, %lu) failed",
					    proposal,
					    prop_cnt * (unsigned long)sizeof *proposal);
					goto bail_out;
				}
				proposal = new_proposal;

				new_transforms_len = reallocarray(transforms_len,
				    prop_cnt, sizeof *transforms_len);
				if (!new_transforms_len) {
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "realloc (%p, %lu) failed",
					    transforms_len,
					    prop_cnt * (unsigned long)sizeof *transforms_len);
					goto bail_out;
				}
				transforms_len = new_transforms_len;

				new_transform = reallocarray(transform,
				    prop_cnt, sizeof *transform);
				if (!new_transform) {
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "realloc (%p, %lu) failed",
					    transform,
					    prop_cnt * (unsigned long)sizeof *transform);
					goto bail_out;
				}
				transform = new_transform;

				new_transform_cnt = reallocarray(transform_cnt,
				    prop_cnt, sizeof *transform_cnt);
				if (!new_transform_cnt) {
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "realloc (%p, %lu) failed",
					    transform_cnt,
					    prop_cnt * (unsigned long)sizeof *transform_cnt);
					goto bail_out;
				}
				transform_cnt = new_transform_cnt;

				new_transform_len = reallocarray(transform_len,
				    prop_cnt, sizeof *transform_len);
				if (!new_transform_len) {
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "realloc (%p, %lu) failed",
					    transform_len,
					    prop_cnt * (unsigned long)sizeof *transform_len);
					goto bail_out;
				}
				transform_len = new_transform_len;
			}
			protocol_id = conf_get_str(prot->field, "PROTOCOL_ID");
			if (!protocol_id)
				goto bail_out;

			proto_id = constant_value(ipsec_proto_cst,
			    protocol_id);
			switch (proto_id) {
			case IPSEC_PROTO_IPSEC_AH:
				id_map = ipsec_ah_cst;
				break;

			case IPSEC_PROTO_IPSEC_ESP:
				id_map = ipsec_esp_cst;
				break;

			case IPSEC_PROTO_IPCOMP:
				id_map = ipsec_ipcomp_cst;
				break;

			default:
			    {
				log_print("initiator_send_HASH_SA_NONCE: "
				    "invalid PROTCOL_ID: %s", protocol_id);
				goto bail_out;
			    }
			}

			/* Now get each transform we offer for this protocol.*/
			xf_conf = conf_get_list(prot->field, "Transforms");
			if (!xf_conf)
				goto bail_out;
			transform_cnt[prop_no] = xf_conf->cnt;

			transform[prop_no] = calloc(transform_cnt[prop_no],
			    sizeof **transform);
			if (!transform[prop_no]) {
				log_error("initiator_send_HASH_SA_NONCE: "
				    "calloc (%d, %lu) failed",
				    transform_cnt[prop_no],
				    (unsigned long)sizeof **transform);
				goto bail_out;
			}
			transform_len[prop_no] = calloc(transform_cnt[prop_no],
			    sizeof **transform_len);
			if (!transform_len[prop_no]) {
				log_error("initiator_send_HASH_SA_NONCE: "
				    "calloc (%d, %lu) failed",
				    transform_cnt[prop_no],
				    (unsigned long)sizeof **transform_len);
				goto bail_out;
			}
			transforms_len[prop_no] = 0;
			for (xf = TAILQ_FIRST(&xf_conf->fields), xf_no = 0;
			    xf_no < transform_cnt[prop_no];
			    xf_no++, xf = TAILQ_NEXT(xf, link)) {

				/* XXX The sizing needs to be dynamic.  */
				transform[prop_no][xf_no] =
				    calloc(ISAKMP_TRANSFORM_SA_ATTRS_OFF +
				    9 * ISAKMP_ATTR_VALUE_OFF, 1);
				if (!transform[prop_no][xf_no]) {
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "calloc (%d, 1) failed",
					    ISAKMP_TRANSFORM_SA_ATTRS_OFF +
					    9 * ISAKMP_ATTR_VALUE_OFF);
					goto bail_out;
				}
				SET_ISAKMP_TRANSFORM_NO(transform[prop_no][xf_no],
				    xf_no + 1);

				transform_id = conf_get_str(xf->field,
				    "TRANSFORM_ID");
				if (!transform_id)
					goto bail_out;
				SET_ISAKMP_TRANSFORM_ID(transform[prop_no][xf_no],
				    constant_value(id_map, transform_id));
				SET_ISAKMP_TRANSFORM_RESERVED(transform[prop_no][xf_no], 0);

				attr = transform[prop_no][xf_no] +
				    ISAKMP_TRANSFORM_SA_ATTRS_OFF;

				/*
				 * Life durations are special, we should be
				 * able to specify several, one per type.
				 */
				life_conf = conf_get_list(xf->field, "Life");
				if (life_conf) {
					for (life = TAILQ_FIRST(&life_conf->fields);
					    life; life = TAILQ_NEXT(life, link)) {
						attribute_set_constant(
						    life->field, "LIFE_TYPE",
						    ipsec_duration_cst,
						    IPSEC_ATTR_SA_LIFE_TYPE,
						    &attr);

						/*
						 * XXX Deals with 16 and 32
						 * bit lifetimes only
						 */
						value =
						    conf_get_num(life->field,
							"LIFE_DURATION", 0);
						if (value) {
							if (value <= 0xffff)
								attr =
								    attribute_set_basic(
									attr,
									IPSEC_ATTR_SA_LIFE_DURATION,
									value);
							else {
								value = htonl(value);
								attr =
								    attribute_set_var(
									attr,
									IPSEC_ATTR_SA_LIFE_DURATION,
									(u_int8_t *)&value,
									sizeof value);
							}
						}
					}
					conf_free_list(life_conf);
				}

				if (proto_id == IPSEC_PROTO_IPSEC_ESP &&
				    (exchange->flags &
				    EXCHANGE_FLAG_NAT_T_ENABLE)) {
					name = conf_get_str(xf->field,
					    "ENCAPSULATION_MODE");
					if (name) {
						value = constant_value(
						    ipsec_encap_cst,
						    name);
						switch (value) {
						case IPSEC_ENCAP_TUNNEL:
							value = exchange->flags & EXCHANGE_FLAG_NAT_T_DRAFT ?
							    IPSEC_ENCAP_UDP_ENCAP_TUNNEL_DRAFT :
							    IPSEC_ENCAP_UDP_ENCAP_TUNNEL;
							break;
						case IPSEC_ENCAP_TRANSPORT:
							value = exchange->flags & EXCHANGE_FLAG_NAT_T_DRAFT ?
							    IPSEC_ENCAP_UDP_ENCAP_TRANSPORT_DRAFT :
							    IPSEC_ENCAP_UDP_ENCAP_TRANSPORT;
							break;
						}
						attr = attribute_set_basic(
						    attr,
						    IPSEC_ATTR_ENCAPSULATION_MODE,
						    value);
					}
				} else {
					attribute_set_constant(xf->field,
					    "ENCAPSULATION_MODE",
					    ipsec_encap_cst,
					    IPSEC_ATTR_ENCAPSULATION_MODE,
					    &attr);
				}

				if (proto_id != IPSEC_PROTO_IPCOMP) {
					attribute_set_constant(xf->field,
					    "AUTHENTICATION_ALGORITHM",
					    ipsec_auth_cst,
					    IPSEC_ATTR_AUTHENTICATION_ALGORITHM,
					    &attr);

					attribute_set_constant(xf->field,
					    "GROUP_DESCRIPTION",
					    ike_group_desc_cst,
					    IPSEC_ATTR_GROUP_DESCRIPTION, &attr);

					value = conf_get_num(xf->field,
					    "KEY_LENGTH", 0);
					if (value)
						attr = attribute_set_basic(
						    attr,
						    IPSEC_ATTR_KEY_LENGTH,
						    value);

					value = conf_get_num(xf->field,
					    "KEY_ROUNDS", 0);
					if (value)
						attr = attribute_set_basic(
						    attr,
						    IPSEC_ATTR_KEY_ROUNDS,
						    value);
				} else {
					value = conf_get_num(xf->field,
					    "COMPRESS_DICTIONARY_SIZE", 0);
					if (value)
						attr = attribute_set_basic(
						    attr,
						    IPSEC_ATTR_COMPRESS_DICTIONARY_SIZE,
						    value);

					value = conf_get_num(xf->field,
					   "COMPRESS_PRIVATE_ALGORITHM", 0);
					if (value)
						attr = attribute_set_basic(
						    attr,
						    IPSEC_ATTR_COMPRESS_PRIVATE_ALGORITHM,
						    value);
				}

				value = conf_get_num(xf->field, "ECN_TUNNEL",
				    0);
				if (value)
					attr = attribute_set_basic(attr,
					    IPSEC_ATTR_ECN_TUNNEL, value);

				/* Record the real transform size.  */
				transforms_len[prop_no] +=
				    (transform_len[prop_no][xf_no]
					= attr - transform[prop_no][xf_no]);

				if (proto_id != IPSEC_PROTO_IPCOMP) {
					/*
					 * Make sure that if a group
					 * description is specified, it is
					 * specified for all transforms
					 * equally.
					 */
					attr =
					    (u_int8_t *)conf_get_str(xf->field,
						"GROUP_DESCRIPTION");
					new_group_desc
					    = attr ? constant_value(ike_group_desc_cst,
						(char *)attr) : 0;
					if (group_desc == -1)
						group_desc = new_group_desc;
					else if (group_desc != new_group_desc) {
						log_print("initiator_send_HASH_SA_NONCE: "
						    "differing group descriptions in a proposal");
						goto bail_out;
					}
				}
			}
			conf_free_list(xf_conf);
			xf_conf = 0;

			/*
			 * Get SPI from application.
			 * XXX Should we care about unknown constants?
			 */
			protocol_num = constant_value(ipsec_proto_cst,
			    protocol_id);
			spi = doi->get_spi(&spi_sz, protocol_num, msg);
			if (spi_sz && !spi) {
				log_print("initiator_send_HASH_SA_NONCE: "
				    "doi->get_spi failed");
				goto bail_out;
			}
			proposal_len = ISAKMP_PROP_SPI_OFF + spi_sz;
			proposals_len +=
			    proposal_len + transforms_len[prop_no];
			proposal[prop_no] = malloc(proposal_len);
			if (!proposal[prop_no]) {
				log_error("initiator_send_HASH_SA_NONCE: "
				    "malloc (%lu) failed",
				    (unsigned long)proposal_len);
				goto bail_out;
			}
			SET_ISAKMP_PROP_NO(proposal[prop_no], suite_no + 1);
			SET_ISAKMP_PROP_PROTO(proposal[prop_no], protocol_num);

			/* XXX I would like to see this factored out.  */
			proto = calloc(1, sizeof *proto);
			if (!proto) {
				log_error("initiator_send_HASH_SA_NONCE: "
				    "calloc (1, %lu) failed",
				    (unsigned long)sizeof *proto);
				goto bail_out;
			}
			if (doi->proto_size) {
				proto->data = calloc(1, doi->proto_size);
				if (!proto->data) {
					free(proto);
					log_error(
					    "initiator_send_HASH_SA_NONCE: "
					    "calloc (1, %lu) failed",
					    (unsigned long)doi->proto_size);
					goto bail_out;
				}
			}
			proto->no = suite_no + 1;
			proto->proto = protocol_num;
			proto->sa = TAILQ_FIRST(&exchange->sa_list);
			proto->xf_cnt = transform_cnt[prop_no];
			TAILQ_INIT(&proto->xfs);
			for (xf_no = 0; xf_no < proto->xf_cnt; xf_no++) {
				pa = calloc(1, sizeof *pa);
				if (!pa) {
					free(proto->data);
					free(proto);
					goto bail_out;
				}
				pa->len = transform_len[prop_no][xf_no];
				pa->attrs = malloc(pa->len);
				if (!pa->attrs) {
					free(proto->data);
					free(proto);
					free(pa);
					goto bail_out;
				}
				memcpy(pa->attrs, transform[prop_no][xf_no],
				    pa->len);
				TAILQ_INSERT_TAIL(&proto->xfs, pa, next);
			}
			TAILQ_INSERT_TAIL(&TAILQ_FIRST(&exchange->sa_list)->protos,
			    proto, link);

			/* Setup the incoming SPI.  */
			SET_ISAKMP_PROP_SPI_SZ(proposal[prop_no], spi_sz);
			memcpy(proposal[prop_no] + ISAKMP_PROP_SPI_OFF, spi,
			    spi_sz);
			proto->spi_sz[1] = spi_sz;
			proto->spi[1] = spi;

			/*
			 * Let the DOI get at proto for initializing its own
			 * data.
			 */
			if (doi->proto_init)
				doi->proto_init(proto, prot->field);

			SET_ISAKMP_PROP_NTRANSFORMS(proposal[prop_no],
						    transform_cnt[prop_no]);
			prop_no++;
		}
		conf_free_list(prot_conf);
		prot_conf = 0;
	}

	sa_len = ISAKMP_SA_SIT_OFF + IPSEC_SIT_SIT_LEN;
	sa_buf = malloc(sa_len);
	if (!sa_buf) {
		log_error("initiator_send_HASH_SA_NONCE: malloc (%lu) failed",
			  (unsigned long)sa_len);
		goto bail_out;
	}
	SET_ISAKMP_SA_DOI(sa_buf, IPSEC_DOI_IPSEC);
	SET_IPSEC_SIT_SIT(sa_buf + ISAKMP_SA_SIT_OFF, IPSEC_SIT_IDENTITY_ONLY);

	/*
	 * Add the payloads.  As this is a SA, we need to recompute the
	 * lengths of the payloads containing others.  We also need to
	 * reset these payload's "next payload type" field.
	 */
	if (message_add_payload(msg, ISAKMP_PAYLOAD_SA, sa_buf, sa_len, 1))
		goto bail_out;
	SET_ISAKMP_GEN_LENGTH(sa_buf, sa_len + proposals_len);
	sa_buf = 0;

	update_nextp = 0;
	saved_nextp_sa = msg->nextp;
	for (i = 0; i < prop_no; i++) {
		if (message_add_payload(msg, ISAKMP_PAYLOAD_PROPOSAL,
		    proposal[i], proposal_len, update_nextp))
			goto bail_out;
		SET_ISAKMP_GEN_LENGTH(proposal[i],
		    proposal_len + transforms_len[i]);
		proposal[i] = 0;

		update_nextp = 0;
		saved_nextp_prop = msg->nextp;
		for (xf_no = 0; xf_no < transform_cnt[i]; xf_no++) {
			if (message_add_payload(msg, ISAKMP_PAYLOAD_TRANSFORM,
			    transform[i][xf_no],
			    transform_len[i][xf_no], update_nextp))
				goto bail_out;
			update_nextp = 1;
			transform[i][xf_no] = 0;
		}
		msg->nextp = saved_nextp_prop;
		update_nextp = 1;
	}
	msg->nextp = saved_nextp_sa;

	/*
	 * Save SA payload body in ie->sa_i_b, length ie->sa_i_b_len.
	 */
	ie->sa_i_b = message_copy(msg, ISAKMP_GEN_SZ, &ie->sa_i_b_len);
	if (!ie->sa_i_b)
		goto bail_out;

	/*
	 * Generate a nonce, and add it to the message.
	 * XXX I want a better way to specify the nonce's size.
	 */
	if (exchange_gen_nonce(msg, 16))
		return -1;

	/* Generate optional KEY_EXCH payload.  */
	if (group_desc > 0) {
		ie->group = group_get(group_desc);
		if (!ie->group)
			return -1;
		ie->g_x_len = dh_getlen(ie->group);

		if (ipsec_gen_g_x(msg)) {
			group_free(ie->group);
			ie->group = 0;
			return -1;
		}
	}
	/* Generate optional client ID payloads.  XXX Share with responder.  */
	local_id = conf_get_str(exchange->name, "Local-ID");
	remote_id = conf_get_str(exchange->name, "Remote-ID");
	if (local_id && remote_id) {
		id = ipsec_build_id(local_id, &sz);
		if (!id)
			return -1;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "initiator_send_HASH_SA_NONCE: IDic", id, sz));
		if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, id, sz, 1)) {
			free(id);
			return -1;
		}
		id = ipsec_build_id(remote_id, &sz);
		if (!id)
			return -1;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "initiator_send_HASH_SA_NONCE: IDrc", id, sz));
		if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, id, sz, 1)) {
			free(id);
			return -1;
		}
	}
	/* XXX I do not judge these as errors, are they?  */
	else if (local_id)
		log_print("initiator_send_HASH_SA_NONCE: "
			  "Local-ID given without Remote-ID for \"%s\"",
			  exchange->name);
	else if (remote_id)
		/*
		 * This code supports the "road warrior" case, where the
		 * initiator doesn't have a fixed IP address, but wants to
		 * specify a particular remote network to talk to. -- Adrian
		 * Close <adrian@esec.com.au>
		 */
	{
		log_print("initiator_send_HASH_SA_NONCE: "
			  "Remote-ID given without Local-ID for \"%s\"",
			  exchange->name);

		/*
		 * If we're here, then we are the initiator, so use initiator
		 * address for local ID
		 */
		msg->transport->vtbl->get_src(msg->transport, &src);
		sz = ISAKMP_ID_SZ + sockaddr_addrlen(src);

		id = calloc(sz, sizeof(char));
		if (!id) {
			log_error("initiator_send_HASH_SA_NONCE: "
			    "calloc (%lu, %lu) failed", (unsigned long)sz,
			    (unsigned long)sizeof(char));
			return -1;
		}
		switch (src->sa_family) {
		case AF_INET6:
			SET_ISAKMP_ID_TYPE(id, IPSEC_ID_IPV6_ADDR);
			break;
		case AF_INET:
			SET_ISAKMP_ID_TYPE(id, IPSEC_ID_IPV4_ADDR);
			break;
		default:
			log_error("initiator_send_HASH_SA_NONCE: "
			    "unknown sa_family %d", src->sa_family);
			free(id);
			return -1;
		}
		memcpy(id + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(src),
		    sockaddr_addrlen(src));

		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "initiator_send_HASH_SA_NONCE: IDic", id, sz));
		if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, id, sz, 1)) {
			free(id);
			return -1;
		}
		/* Send supplied remote_id */
		id = ipsec_build_id(remote_id, &sz);
		if (!id)
			return -1;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "initiator_send_HASH_SA_NONCE: IDrc", id, sz));
		if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, id, sz, 1)) {
			free(id);
			return -1;
		}
	}
	if (ipsec_fill_in_hash(msg))
		goto bail_out;

	conf_free_list(suite_conf);
	for (i = 0; i < prop_no; i++) {
		free(transform[i]);
		free(transform_len[i]);
	}
	free(proposal);
	free(transform);
	free(transforms_len);
	free(transform_len);
	free(transform_cnt);
	return 0;

bail_out:
	free(sa_buf);
	if (proposal) {
		for (i = 0; i < prop_no; i++) {
			free(proposal[i]);
			if (transform[i]) {
				for (xf_no = 0; xf_no < transform_cnt[i];
				    xf_no++)
					free(transform[i][xf_no]);
				free(transform[i]);
			}
			free(transform_len[i]);
		}
		free(proposal);
		free(transforms_len);
		free(transform);
		free(transform_len);
		free(transform_cnt);
	}
	if (xf_conf)
		conf_free_list(xf_conf);
	if (prot_conf)
		conf_free_list(prot_conf);
	conf_free_list(suite_conf);
	return -1;
}

/* Figure out what transform the responder chose.  */
static int
initiator_recv_HASH_SA_NONCE(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct sa      *sa;
	struct proto   *proto, *next_proto;
	struct payload *sa_p = payload_first(msg, ISAKMP_PAYLOAD_SA);
	struct payload *xf, *idp;
	struct payload *hashp = payload_first(msg, ISAKMP_PAYLOAD_HASH);
	struct payload *kep = payload_first(msg, ISAKMP_PAYLOAD_KEY_EXCH);
	struct prf     *prf;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct hash    *hash = hash_get(isa->hash);
	u_int8_t       *rest;
	size_t          rest_len;
	struct sockaddr *src, *dst;

	/* Allocate the prf and start calculating our HASH(1).  XXX Share?  */
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_recv_HASH_SA_NONCE: "
	    "SKEYID_a", (u_int8_t *)isa->skeyid_a, isa->skeyid_len));
	prf = prf_alloc(isa->prf_type, hash->type, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		return -1;

	prf->Init(prf->prfctx);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "initiator_recv_HASH_SA_NONCE: message_id",
	    exchange->message_id, ISAKMP_HDR_MESSAGE_ID_LEN));
	prf->Update(prf->prfctx, exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_recv_HASH_SA_NONCE: "
	    "NONCE_I_b", exchange->nonce_i, exchange->nonce_i_len));
	prf->Update(prf->prfctx, exchange->nonce_i, exchange->nonce_i_len);
	rest = hashp->p + GET_ISAKMP_GEN_LENGTH(hashp->p);
	rest_len = (GET_ISAKMP_HDR_LENGTH(msg->iov[0].iov_base)
	    - (rest - (u_int8_t *)msg->iov[0].iov_base));
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "initiator_recv_HASH_SA_NONCE: payloads after HASH(2)", rest,
	    rest_len));
	prf->Update(prf->prfctx, rest, rest_len);
	prf->Final(hash->digest, prf->prfctx);
	prf_free(prf);
	LOG_DBG_BUF((LOG_NEGOTIATION, 80,
	    "initiator_recv_HASH_SA_NONCE: computed HASH(2)", hash->digest,
	    hash->hashsize));
	if (memcmp(hashp->p + ISAKMP_HASH_DATA_OFF, hash->digest,
	    hash->hashsize) != 0) {
		message_drop(msg, ISAKMP_NOTIFY_INVALID_HASH_INFORMATION, 0, 1,
		    0);
		return -1;
	}
	/* Mark the HASH as handled.  */
	hashp->flags |= PL_MARK;

	/* Mark message as authenticated. */
	msg->flags |= MSG_AUTHENTICATED;

	/*
	 * As we are getting an answer on our transform offer, only one
	 * transform should be given.
	 *
	 * XXX Currently we only support negotiating one SA per quick mode run.
	 */
	if (TAILQ_NEXT(sa_p, link)) {
		log_print("initiator_recv_HASH_SA_NONCE: "
		    "multiple SA payloads in quick mode not supported yet");
		return -1;
	}
	sa = TAILQ_FIRST(&exchange->sa_list);

	/* This is here for the policy check */
	if (kep)
		ie->pfs = 1;

	/* Drop message when it contains ID types we do not implement yet.  */
	TAILQ_FOREACH(idp, &msg->payload[ISAKMP_PAYLOAD_ID], link) {
		switch (GET_ISAKMP_ID_TYPE(idp->p)) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV4_ADDR_SUBNET:
		case IPSEC_ID_IPV6_ADDR:
		case IPSEC_ID_IPV6_ADDR_SUBNET:
			break;

		case IPSEC_ID_FQDN:
			/*
			 * FQDN may be used for in NAT-T with transport mode.
			 * We can handle the message in this case.  In the
			 * other cases we'll drop the message later.
			 */
			break;

		default:
			message_drop(msg, ISAKMP_NOTIFY_INVALID_ID_INFORMATION,
			    0, 1, 0);
			return -1;
		}
	}

	/* Handle optional client ID payloads.  */
	idp = payload_first(msg, ISAKMP_PAYLOAD_ID);
	if (idp) {
		/* If IDci is there, IDcr must be too.  */
		if (!TAILQ_NEXT(idp, link)) {
			/* XXX Is this a good notify type?  */
			message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0,
			    1, 0);
			return -1;
		}
		/* XXX We should really compare, not override.  */
		ie->id_ci_sz = GET_ISAKMP_GEN_LENGTH(idp->p);
		ie->id_ci = malloc(ie->id_ci_sz);
		if (!ie->id_ci) {
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "malloc (%lu) failed",
			    (unsigned long)ie->id_ci_sz);
			return -1;
		}
		memcpy(ie->id_ci, idp->p, ie->id_ci_sz);
		idp->flags |= PL_MARK;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "initiator_recv_HASH_SA_NONCE: IDci",
		    ie->id_ci + ISAKMP_GEN_SZ, ie->id_ci_sz - ISAKMP_GEN_SZ));

		idp = TAILQ_NEXT(idp, link);
		ie->id_cr_sz = GET_ISAKMP_GEN_LENGTH(idp->p);
		ie->id_cr = malloc(ie->id_cr_sz);
		if (!ie->id_cr) {
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "malloc (%lu) failed",
			    (unsigned long)ie->id_cr_sz);
			return -1;
		}
		memcpy(ie->id_cr, idp->p, ie->id_cr_sz);
		idp->flags |= PL_MARK;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "initiator_recv_HASH_SA_NONCE: IDcr",
		    ie->id_cr + ISAKMP_GEN_SZ, ie->id_cr_sz - ISAKMP_GEN_SZ));
	} else {
		/*
		 * If client identifiers are not present in the exchange,
		 * we fake them. RFC 2409 states:
		 *    The identities of the SAs negotiated in Quick Mode are
		 *    implicitly assumed to be the IP addresses of the ISAKMP
		 *    peers, without any constraints on the protocol or port
		 *    numbers allowed, unless client identifiers are specified
		 *    in Quick Mode.
		 *
		 * -- Michael Paddon (mwp@aba.net.au)
		 */

		ie->flags = IPSEC_EXCH_FLAG_NO_ID;

		/* Get initiator and responder addresses.  */
		msg->transport->vtbl->get_src(msg->transport, &src);
		msg->transport->vtbl->get_dst(msg->transport, &dst);
		ie->id_ci_sz = ISAKMP_ID_DATA_OFF + sockaddr_addrlen(src);
		ie->id_cr_sz = ISAKMP_ID_DATA_OFF + sockaddr_addrlen(dst);
		ie->id_ci = calloc(ie->id_ci_sz, sizeof(char));
		ie->id_cr = calloc(ie->id_cr_sz, sizeof(char));

		if (!ie->id_ci || !ie->id_cr) {
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "calloc (%lu, %lu) failed",
			    (unsigned long)ie->id_cr_sz,
			    (unsigned long)sizeof(char));
			free(ie->id_ci);
			ie->id_ci = 0;
			free(ie->id_cr);
			ie->id_cr = 0;
			return -1;
		}
		if (src->sa_family != dst->sa_family) {
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "sa_family mismatch");
			free(ie->id_ci);
			ie->id_ci = 0;
			free(ie->id_cr);
			ie->id_cr = 0;
			return -1;
		}
		switch (src->sa_family) {
		case AF_INET:
			SET_ISAKMP_ID_TYPE(ie->id_ci, IPSEC_ID_IPV4_ADDR);
			SET_ISAKMP_ID_TYPE(ie->id_cr, IPSEC_ID_IPV4_ADDR);
			break;

		case AF_INET6:
			SET_ISAKMP_ID_TYPE(ie->id_ci, IPSEC_ID_IPV6_ADDR);
			SET_ISAKMP_ID_TYPE(ie->id_cr, IPSEC_ID_IPV6_ADDR);
			break;

		default:
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "unknown sa_family %d", src->sa_family);
			free(ie->id_ci);
			ie->id_ci = 0;
			free(ie->id_cr);
			ie->id_cr = 0;
			return -1;
		}
		memcpy(ie->id_ci + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(src),
		    sockaddr_addrlen(src));
		memcpy(ie->id_cr + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(dst),
		    sockaddr_addrlen(dst));
	}

	/* Build the protection suite in our SA.  */
	TAILQ_FOREACH(xf, &msg->payload[ISAKMP_PAYLOAD_TRANSFORM], link) {
		/*
		 * XXX We could check that the proposal each transform
		 * belongs to is unique.
		 */

		if (sa_add_transform(sa, xf, exchange->initiator, &proto))
			return -1;

		/* XXX Check that the chosen transform matches an offer.  */

		ipsec_decode_transform(msg, sa, proto, xf->p);
	}

	/* Now remove offers that we don't need anymore.  */
	for (proto = TAILQ_FIRST(&sa->protos); proto; proto = next_proto) {
		next_proto = TAILQ_NEXT(proto, link);
		if (!proto->chosen)
			proto_free(proto);
	}

	if (!check_policy(exchange, sa, msg->isakmp_sa)) {
		message_drop(msg, ISAKMP_NOTIFY_NO_PROPOSAL_CHOSEN, 0, 1, 0);
		log_print("initiator_recv_HASH_SA_NONCE: policy check failed");
		return -1;
	}

	/* Mark the SA as handled.  */
	sa_p->flags |= PL_MARK;

	isa = sa->data;
	if ((isa->group_desc &&
	    (!ie->group || ie->group->id != isa->group_desc)) ||
	    (!isa->group_desc && ie->group)) {
		log_print("initiator_recv_HASH_SA_NONCE: disagreement on PFS");
		return -1;
	}
	/* Copy out the initiator's nonce.  */
	if (exchange_save_nonce(msg))
		return -1;

	/* Handle the optional KEY_EXCH payload.  */
	if (kep && ipsec_save_g_x(msg))
		return -1;

	return 0;
}

static int
initiator_send_HASH(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct prf     *prf;
	u_int8_t       *buf;
	struct hash    *hash = hash_get(isa->hash);

	/*
	 * We want a HASH payload to start with.  XXX Share with
	 * ike_main_mode.c?
	 */
	buf = malloc(ISAKMP_HASH_SZ + hash->hashsize);
	if (!buf) {
		log_error("initiator_send_HASH: malloc (%lu) failed",
		    ISAKMP_HASH_SZ + (unsigned long)hash->hashsize);
		return -1;
	}
	if (message_add_payload(msg, ISAKMP_PAYLOAD_HASH, buf,
	    ISAKMP_HASH_SZ + hash->hashsize, 1)) {
		free(buf);
		return -1;
	}
	/* Allocate the prf and start calculating our HASH(3).  XXX Share?  */
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_send_HASH: SKEYID_a",
	    isa->skeyid_a, isa->skeyid_len));
	prf = prf_alloc(isa->prf_type, isa->hash, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		return -1;
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, (unsigned char *)"\0", 1);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_send_HASH: message_id",
	    exchange->message_id, ISAKMP_HDR_MESSAGE_ID_LEN));
	prf->Update(prf->prfctx, exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_send_HASH: NONCE_I_b",
	    exchange->nonce_i, exchange->nonce_i_len));
	prf->Update(prf->prfctx, exchange->nonce_i, exchange->nonce_i_len);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_send_HASH: NONCE_R_b",
	    exchange->nonce_r, exchange->nonce_r_len));
	prf->Update(prf->prfctx, exchange->nonce_r, exchange->nonce_r_len);
	prf->Final(buf + ISAKMP_GEN_SZ, prf->prfctx);
	prf_free(prf);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "initiator_send_HASH: HASH(3)",
	    buf + ISAKMP_GEN_SZ, hash->hashsize));

	if (ie->group)
		message_register_post_send(msg, gen_g_xy);

	message_register_post_send(msg, post_quick_mode);

	return 0;
}

static void
post_quick_mode(struct message *msg)
{
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct prf     *prf;
	struct sa      *sa;
	struct proto   *proto;
	struct ipsec_proto *iproto;
	u_int8_t       *keymat;
	int             i;

	/*
	 * Loop over all SA negotiations and do both an in- and an outgoing SA
	 * per protocol.
	 */
	for (sa = TAILQ_FIRST(&exchange->sa_list); sa;
	    sa = TAILQ_NEXT(sa, next)) {
		for (proto = TAILQ_FIRST(&sa->protos); proto;
		    proto = TAILQ_NEXT(proto, link)) {
			if (proto->proto == IPSEC_PROTO_IPCOMP)
				continue;

			iproto = proto->data;

			/*
			 * There are two SAs for each SA negotiation,
			 * incoming and outgoing.
			 */
			for (i = 0; i < 2; i++) {
				prf = prf_alloc(isa->prf_type, isa->hash,
				    isa->skeyid_d, isa->skeyid_len);
				if (!prf) {
					/* XXX What to do?  */
					continue;
				}
				ie->keymat_len = ipsec_keymat_length(proto);

				/*
				 * We need to roundup the length of the key
				 * material buffer to a multiple of the PRF's
				 * blocksize as it is generated in chunks of
				 * that blocksize.
				 */
				iproto->keymat[i]
					= malloc(((ie->keymat_len + prf->blocksize - 1)
					/ prf->blocksize) * prf->blocksize);
				if (!iproto->keymat[i]) {
					log_error("post_quick_mode: "
					    "malloc (%lu) failed",
					    (((unsigned long)ie->keymat_len +
						prf->blocksize - 1) / prf->blocksize) *
					    prf->blocksize);
					/* XXX What more to do?  */
					free(prf);
					continue;
				}
				for (keymat = iproto->keymat[i];
				keymat < iproto->keymat[i] + ie->keymat_len;
				    keymat += prf->blocksize) {
					prf->Init(prf->prfctx);

					if (keymat != iproto->keymat[i]) {
						/*
						 * Hash in last round's
						 * KEYMAT.
						 */
						LOG_DBG_BUF((LOG_NEGOTIATION,
						    90, "post_quick_mode: "
						    "last KEYMAT",
						    keymat - prf->blocksize,
						    prf->blocksize));
						prf->Update(prf->prfctx,
						    keymat - prf->blocksize,
						    prf->blocksize);
					}
					/* If PFS is used hash in g^xy.  */
					if (ie->g_xy) {
						LOG_DBG_BUF((LOG_NEGOTIATION,
						    90, "post_quick_mode: "
						    "g^xy", ie->g_xy,
						    ie->g_xy_len));
						prf->Update(prf->prfctx,
						    ie->g_xy, ie->g_xy_len);
					}
					LOG_DBG((LOG_NEGOTIATION, 90,
					    "post_quick_mode: "
					    "suite %d proto %d", proto->no,
					    proto->proto));
					prf->Update(prf->prfctx, &proto->proto,
					    1);
					LOG_DBG_BUF((LOG_NEGOTIATION, 90,
					    "post_quick_mode: SPI",
					    proto->spi[i], proto->spi_sz[i]));
					prf->Update(prf->prfctx,
					    proto->spi[i], proto->spi_sz[i]);
					LOG_DBG_BUF((LOG_NEGOTIATION, 90,
					    "post_quick_mode: Ni_b",
					    exchange->nonce_i,
					    exchange->nonce_i_len));
					prf->Update(prf->prfctx,
					    exchange->nonce_i,
					    exchange->nonce_i_len);
					LOG_DBG_BUF((LOG_NEGOTIATION, 90,
					    "post_quick_mode: Nr_b",
					    exchange->nonce_r,
					    exchange->nonce_r_len));
					prf->Update(prf->prfctx,
					    exchange->nonce_r,
					    exchange->nonce_r_len);
					prf->Final(keymat, prf->prfctx);
				}
				prf_free(prf);
				LOG_DBG_BUF((LOG_NEGOTIATION, 90,
				    "post_quick_mode: KEYMAT",
				    iproto->keymat[i], ie->keymat_len));
			}
		}
	}

	log_verbose("isakmpd: quick mode done%s: %s",
	    (exchange->initiator == 0) ? " (as responder)" : "",
	    !msg->isakmp_sa || !msg->isakmp_sa->transport ? "<no transport>"
	    : msg->isakmp_sa->transport->vtbl->decode_ids
	    (msg->isakmp_sa->transport));
}

/*
 * Accept a set of transforms offered by the initiator and chose one we can
 * handle.
 * XXX Describe in more detail.
 */
static int
responder_recv_HASH_SA_NONCE(struct message *msg)
{
	struct payload *hashp, *kep, *idp;
	struct sa      *sa;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct prf     *prf;
	u_int8_t       *hash, *my_hash = 0;
	size_t          hash_len;
	u_int8_t       *pkt = msg->iov[0].iov_base;
	u_int8_t        group_desc = 0;
	int             retval = -1;
	struct proto   *proto;
	struct sockaddr *src, *dst;
	char           *name;

	hashp = payload_first(msg, ISAKMP_PAYLOAD_HASH);
	hash = hashp->p;
	hashp->flags |= PL_MARK;

	/* The HASH payload should be the first one.  */
	if (hash != pkt + ISAKMP_HDR_SZ) {
		/* XXX Is there a better notification type?  */
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		goto cleanup;
	}
	hash_len = GET_ISAKMP_GEN_LENGTH(hash);
	my_hash = malloc(hash_len - ISAKMP_GEN_SZ);
	if (!my_hash) {
		log_error("responder_recv_HASH_SA_NONCE: malloc (%lu) failed",
		    (unsigned long)hash_len - ISAKMP_GEN_SZ);
		goto cleanup;
	}
	/*
	 * Check the payload's integrity.
	 * XXX Share with ipsec_fill_in_hash?
	 */
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_recv_HASH_SA_NONCE: "
	    "SKEYID_a", isa->skeyid_a, isa->skeyid_len));
	prf = prf_alloc(isa->prf_type, isa->hash, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		goto cleanup;
	prf->Init(prf->prfctx);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "responder_recv_HASH_SA_NONCE: message_id",
	    exchange->message_id, ISAKMP_HDR_MESSAGE_ID_LEN));
	prf->Update(prf->prfctx, exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "responder_recv_HASH_SA_NONCE: message after HASH",
	    hash + hash_len,
	    msg->iov[0].iov_len - ISAKMP_HDR_SZ - hash_len));
	prf->Update(prf->prfctx, hash + hash_len,
	    msg->iov[0].iov_len - ISAKMP_HDR_SZ - hash_len);
	prf->Final(my_hash, prf->prfctx);
	prf_free(prf);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "responder_recv_HASH_SA_NONCE: computed HASH(1)", my_hash,
	    hash_len - ISAKMP_GEN_SZ));
	if (memcmp(hash + ISAKMP_GEN_SZ, my_hash, hash_len - ISAKMP_GEN_SZ)
	    != 0) {
		message_drop(msg, ISAKMP_NOTIFY_INVALID_HASH_INFORMATION, 0,
		    1, 0);
		goto cleanup;
	}
	free(my_hash);
	my_hash = 0;

	/* Mark message as authenticated. */
	msg->flags |= MSG_AUTHENTICATED;

	kep = payload_first(msg, ISAKMP_PAYLOAD_KEY_EXCH);
	if (kep)
		ie->pfs = 1;

	/* Drop message when it contains ID types we do not implement yet.  */
	TAILQ_FOREACH(idp, &msg->payload[ISAKMP_PAYLOAD_ID], link) {
		switch (GET_ISAKMP_ID_TYPE(idp->p)) {
		case IPSEC_ID_IPV4_ADDR:
		case IPSEC_ID_IPV4_ADDR_SUBNET:
		case IPSEC_ID_IPV6_ADDR:
		case IPSEC_ID_IPV6_ADDR_SUBNET:
			break;

		case IPSEC_ID_FQDN:
			/*
			 * FQDN may be used for in NAT-T with transport mode.
			 * We can handle the message in this case.  In the
			 * other cases we'll drop the message later.
			 */
			break;

		default:
			message_drop(msg, ISAKMP_NOTIFY_INVALID_ID_INFORMATION,
			    0, 1, 0);
			goto cleanup;
		}
	}

	/* Handle optional client ID payloads.  */
	idp = payload_first(msg, ISAKMP_PAYLOAD_ID);
	if (idp) {
		/* If IDci is there, IDcr must be too.  */
		if (!TAILQ_NEXT(idp, link)) {
			/* XXX Is this a good notify type?  */
			message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0,
			    1, 0);
			goto cleanup;
		}
		ie->id_ci_sz = GET_ISAKMP_GEN_LENGTH(idp->p);
		ie->id_ci = malloc(ie->id_ci_sz);
		if (!ie->id_ci) {
			log_error("responder_recv_HASH_SA_NONCE: "
			    "malloc (%lu) failed",
			    (unsigned long)ie->id_ci_sz);
			goto cleanup;
		}
		memcpy(ie->id_ci, idp->p, ie->id_ci_sz);
		idp->flags |= PL_MARK;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "responder_recv_HASH_SA_NONCE: IDci",
		    ie->id_ci + ISAKMP_GEN_SZ, ie->id_ci_sz - ISAKMP_GEN_SZ));

		idp = TAILQ_NEXT(idp, link);
		ie->id_cr_sz = GET_ISAKMP_GEN_LENGTH(idp->p);
		ie->id_cr = malloc(ie->id_cr_sz);
		if (!ie->id_cr) {
			log_error("responder_recv_HASH_SA_NONCE: "
			    "malloc (%lu) failed",
			    (unsigned long)ie->id_cr_sz);
			goto cleanup;
		}
		memcpy(ie->id_cr, idp->p, ie->id_cr_sz);
		idp->flags |= PL_MARK;
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "responder_recv_HASH_SA_NONCE: IDcr",
		    ie->id_cr + ISAKMP_GEN_SZ, ie->id_cr_sz - ISAKMP_GEN_SZ));
	} else {
		/*
		 * If client identifiers are not present in the exchange,
		 * we fake them. RFC 2409 states:
		 *    The identities of the SAs negotiated in Quick Mode are
		 *    implicitly assumed to be the IP addresses of the ISAKMP
		 *    peers, without any constraints on the protocol or port
		 *    numbers allowed, unless client identifiers are specified
		 *    in Quick Mode.
		 *
		 * -- Michael Paddon (mwp@aba.net.au)
		 */

		ie->flags = IPSEC_EXCH_FLAG_NO_ID;

		/* Get initiator and responder addresses.  */
		msg->transport->vtbl->get_src(msg->transport, &src);
		msg->transport->vtbl->get_dst(msg->transport, &dst);
		ie->id_ci_sz = ISAKMP_ID_DATA_OFF + sockaddr_addrlen(src);
		ie->id_cr_sz = ISAKMP_ID_DATA_OFF + sockaddr_addrlen(dst);
		ie->id_ci = calloc(ie->id_ci_sz, sizeof(char));
		ie->id_cr = calloc(ie->id_cr_sz, sizeof(char));

		if (!ie->id_ci || !ie->id_cr) {
			log_error("responder_recv_HASH_SA_NONCE: "
			    "calloc (%lu, %lu) failed",
			    (unsigned long)ie->id_ci_sz,
			    (unsigned long)sizeof(char));
			goto cleanup;
		}
		if (src->sa_family != dst->sa_family) {
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "sa_family mismatch");
			goto cleanup;
		}
		switch (src->sa_family) {
		case AF_INET:
			SET_ISAKMP_ID_TYPE(ie->id_ci, IPSEC_ID_IPV4_ADDR);
			SET_ISAKMP_ID_TYPE(ie->id_cr, IPSEC_ID_IPV4_ADDR);
			break;

		case AF_INET6:
			SET_ISAKMP_ID_TYPE(ie->id_ci, IPSEC_ID_IPV6_ADDR);
			SET_ISAKMP_ID_TYPE(ie->id_cr, IPSEC_ID_IPV6_ADDR);
			break;

		default:
			log_error("initiator_recv_HASH_SA_NONCE: "
			    "unknown sa_family %d", src->sa_family);
			goto cleanup;
		}

		memcpy(ie->id_cr + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(src),
		    sockaddr_addrlen(src));
		memcpy(ie->id_ci + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(dst),
		    sockaddr_addrlen(dst));
	}

	if (message_negotiate_sa(msg, check_policy))
		goto cleanup;

	for (sa = TAILQ_FIRST(&exchange->sa_list); sa;
	    sa = TAILQ_NEXT(sa, next)) {
		for (proto = TAILQ_FIRST(&sa->protos); proto;
		    proto = TAILQ_NEXT(proto, link)) {
			/*
			 * XXX we need to have some attributes per proto, not
			 * all per SA.
			 */
			ipsec_decode_transform(msg, sa, proto,
			    proto->chosen->p);
			if (proto->proto == IPSEC_PROTO_IPSEC_AH &&
			    !((struct ipsec_proto *)proto->data)->auth) {
				log_print("responder_recv_HASH_SA_NONCE: "
				    "AH proposed without an algorithm "
				    "attribute");
				message_drop(msg,
				    ISAKMP_NOTIFY_NO_PROPOSAL_CHOSEN, 0, 1, 0);
				goto next_sa;
			}
		}

		isa = sa->data;

		/*
		 * The group description is mandatory if we got a KEY_EXCH
		 * payload.
		 */
		if (kep) {
			if (!isa->group_desc) {
				log_print("responder_recv_HASH_SA_NONCE: "
				    "KEY_EXCH payload without a group "
				    "desc. attribute");
				message_drop(msg,
				    ISAKMP_NOTIFY_NO_PROPOSAL_CHOSEN, 0, 1, 0);
				continue;
			}
			/* Also, all SAs must have equal groups.  */
			if (!group_desc)
				group_desc = isa->group_desc;
			else if (group_desc != isa->group_desc) {
				log_print("responder_recv_HASH_SA_NONCE: "
				  "differing group descriptions in one QM");
				message_drop(msg,
				    ISAKMP_NOTIFY_NO_PROPOSAL_CHOSEN, 0, 1, 0);
				continue;
			}
		}
		/* At least one SA was accepted.  */
		retval = 0;

next_sa:
		;	/* XXX gcc3 wants this. */
	}

	if (kep) {
		ie->group = group_get(group_desc);
		if (!ie->group) {
			/*
			 * XXX If the error was due to an out-of-range group
			 * description we should notify our peer, but this
			 * should probably be done by the attribute
			 * validation.  Is it?
			 */
			goto cleanup;
		}
	}
	/* Copy out the initiator's nonce.  */
	if (exchange_save_nonce(msg))
		goto cleanup;

	/* Handle the optional KEY_EXCH payload.  */
	if (kep && ipsec_save_g_x(msg))
		goto cleanup;

	/*
	 * Try to find and set the connection name on the exchange.
	 */

	/*
	 * Check for accepted identities as well as lookup the connection
	 * name and set it on the exchange.
	 *
	 * When not using policies make sure the peer proposes sane IDs.
	 * Otherwise this is done by KeyNote.
	 */
	name = connection_passive_lookup_by_ids(ie->id_ci, ie->id_cr);
	if (name) {
		exchange->name = strdup(name);
		if (!exchange->name) {
			log_error("responder_recv_HASH_SA_NONCE: "
			    "strdup (\"%s\") failed", name);
			goto cleanup;
		}
	} else if (
	    ignore_policy ||
	    strncmp("yes", conf_get_str("General", "Use-Keynote"), 3)) {
		log_print("responder_recv_HASH_SA_NONCE: peer proposed "
		    "invalid phase 2 IDs: %s",
		    (exchange->doi->decode_ids("initiator id %s, responder"
		    " id %s", ie->id_ci, ie->id_ci_sz, ie->id_cr,
		    ie->id_cr_sz, 1)));
		message_drop(msg, ISAKMP_NOTIFY_INVALID_ID_INFORMATION, 0, 1,
		    0);
		goto cleanup;
	}

	return retval;

cleanup:
	/* Remove all potential protocols that have been added to the SAs.  */
	for (sa = TAILQ_FIRST(&exchange->sa_list); sa;
	    sa = TAILQ_NEXT(sa, next))
		while ((proto = TAILQ_FIRST(&sa->protos)) != 0)
			proto_free(proto);
	free(my_hash);
	free(ie->id_ci);
	ie->id_ci = 0;
	free(ie->id_cr);
	ie->id_cr = 0;
	return -1;
}

/* Reply with the transform we chose.  */
static int
responder_send_HASH_SA_NONCE(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct prf     *prf;
	struct hash    *hash = hash_get(isa->hash);
	size_t          nonce_sz = exchange->nonce_i_len;
	u_int8_t       *buf;
	int             initiator = exchange->initiator;
	char            header[80];
	u_int32_t       i;
	u_int8_t       *id;
	size_t          sz;

	/*
	 * We want a HASH payload to start with.  XXX Share with
	 * ike_main_mode.c?
	 */
	buf = malloc(ISAKMP_HASH_SZ + hash->hashsize);
	if (!buf) {
		log_error("responder_send_HASH_SA_NONCE: malloc (%lu) failed",
			  ISAKMP_HASH_SZ + (unsigned long)hash->hashsize);
		return -1;
	}
	if (message_add_payload(msg, ISAKMP_PAYLOAD_HASH, buf,
	    ISAKMP_HASH_SZ + hash->hashsize, 1)) {
		free(buf);
		return -1;
	}
	/* Add the SA payload(s) with the transform(s) that was/were chosen. */
	if (message_add_sa_payload(msg))
		return -1;

	/* Generate a nonce, and add it to the message.  */
	if (exchange_gen_nonce(msg, nonce_sz))
		return -1;

	/* Generate optional KEY_EXCH payload.  This is known as PFS.  */
	if (ie->group && ipsec_gen_g_x(msg))
		return -1;

	/*
	 * If the initiator client ID's were acceptable, just mirror them
	 * back.
	 */
	if (!(ie->flags & IPSEC_EXCH_FLAG_NO_ID)) {
		sz = ie->id_ci_sz;
		id = malloc(sz);
		if (!id) {
			log_error("responder_send_HASH_SA_NONCE: "
			    "malloc (%lu) failed", (unsigned long)sz);
			return -1;
		}
		memcpy(id, ie->id_ci, sz);
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "responder_send_HASH_SA_NONCE: IDic", id, sz));
		if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, id, sz, 1)) {
			free(id);
			return -1;
		}
		sz = ie->id_cr_sz;
		id = malloc(sz);
		if (!id) {
			log_error("responder_send_HASH_SA_NONCE: "
			    "malloc (%lu) failed", (unsigned long)sz);
			return -1;
		}
		memcpy(id, ie->id_cr, sz);
		LOG_DBG_BUF((LOG_NEGOTIATION, 90,
		    "responder_send_HASH_SA_NONCE: IDrc", id, sz));
		if (message_add_payload(msg, ISAKMP_PAYLOAD_ID, id, sz, 1)) {
			free(id);
			return -1;
		}
	}
	/* Allocate the prf and start calculating our HASH(2).  XXX Share?  */
	LOG_DBG((LOG_NEGOTIATION, 90, "responder_recv_HASH: "
	    "isakmp_sa %p isa %p", isakmp_sa, isa));
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_send_HASH_SA_NONCE: "
	    "SKEYID_a", isa->skeyid_a, isa->skeyid_len));
	prf = prf_alloc(isa->prf_type, hash->type, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		return -1;
	prf->Init(prf->prfctx);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "responder_send_HASH_SA_NONCE: message_id",
	    exchange->message_id, ISAKMP_HDR_MESSAGE_ID_LEN));
	prf->Update(prf->prfctx, exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_send_HASH_SA_NONCE: "
	    "NONCE_I_b", exchange->nonce_i, exchange->nonce_i_len));
	prf->Update(prf->prfctx, exchange->nonce_i, exchange->nonce_i_len);

	/* Loop over all payloads after HASH(2).  */
	for (i = 2; i < msg->iovlen; i++) {
		/* XXX Misleading payload type printouts.  */
		snprintf(header, sizeof header,
		   "responder_send_HASH_SA_NONCE: payload %d after HASH(2)",
			 i - 1);
		LOG_DBG_BUF((LOG_NEGOTIATION, 90, header, msg->iov[i].iov_base,
		    msg->iov[i].iov_len));
		prf->Update(prf->prfctx, msg->iov[i].iov_base,
		    msg->iov[i].iov_len);
	}
	prf->Final(buf + ISAKMP_HASH_DATA_OFF, prf->prfctx);
	prf_free(prf);
	snprintf(header, sizeof header, "responder_send_HASH_SA_NONCE: "
	    "HASH_%c", initiator ? 'I' : 'R');
	LOG_DBG_BUF((LOG_NEGOTIATION, 80, header, buf + ISAKMP_HASH_DATA_OFF,
	    hash->hashsize));

	if (ie->group)
		message_register_post_send(msg, gen_g_xy);

	return 0;
}

static void
gen_g_xy(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;

	/* Compute Diffie-Hellman shared value.  */
	ie->g_xy_len = dh_secretlen(ie->group);
	ie->g_xy = malloc(ie->g_xy_len);
	if (!ie->g_xy) {
		log_error("gen_g_xy: malloc (%lu) failed",
		    (unsigned long)ie->g_xy_len);
		return;
	}
	if (dh_create_shared(ie->group, ie->g_xy,
	    exchange->initiator ? ie->g_xr : ie->g_xi)) {
		log_print("gen_g_xy: dh_create_shared failed");
		return;
	}
	LOG_DBG_BUF((LOG_NEGOTIATION, 80, "gen_g_xy: g^xy", ie->g_xy,
	    ie->g_xy_len));
}

static int
responder_recv_HASH(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct prf     *prf;
	u_int8_t       *hash, *my_hash = 0;
	size_t          hash_len;
	struct payload *hashp;

	/* Find HASH(3) and create our own hash, just as big.  */
	hashp = payload_first(msg, ISAKMP_PAYLOAD_HASH);
	hash = hashp->p;
	hashp->flags |= PL_MARK;
	hash_len = GET_ISAKMP_GEN_LENGTH(hash);
	my_hash = malloc(hash_len - ISAKMP_GEN_SZ);
	if (!my_hash) {
		log_error("responder_recv_HASH: malloc (%lu) failed",
			  (unsigned long)hash_len - ISAKMP_GEN_SZ);
		goto cleanup;
	}
	/* Allocate the prf and start calculating our HASH(3).  XXX Share?  */
	LOG_DBG((LOG_NEGOTIATION, 90, "responder_recv_HASH: "
	    "isakmp_sa %p isa %p", isakmp_sa, isa));
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_recv_HASH: SKEYID_a",
	    isa->skeyid_a, isa->skeyid_len));
	prf = prf_alloc(isa->prf_type, isa->hash, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		goto cleanup;
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, (unsigned char *)"\0", 1);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_recv_HASH: message_id",
	    exchange->message_id, ISAKMP_HDR_MESSAGE_ID_LEN));
	prf->Update(prf->prfctx, exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_recv_HASH: NONCE_I_b",
	    exchange->nonce_i, exchange->nonce_i_len));
	prf->Update(prf->prfctx, exchange->nonce_i, exchange->nonce_i_len);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90, "responder_recv_HASH: NONCE_R_b",
	    exchange->nonce_r, exchange->nonce_r_len));
	prf->Update(prf->prfctx, exchange->nonce_r, exchange->nonce_r_len);
	prf->Final(my_hash, prf->prfctx);
	prf_free(prf);
	LOG_DBG_BUF((LOG_NEGOTIATION, 90,
	    "responder_recv_HASH: computed HASH(3)", my_hash,
	    hash_len - ISAKMP_GEN_SZ));
	if (memcmp(hash + ISAKMP_GEN_SZ, my_hash, hash_len - ISAKMP_GEN_SZ)
	    != 0) {
		message_drop(msg, ISAKMP_NOTIFY_INVALID_HASH_INFORMATION, 0,
		    1, 0);
		goto cleanup;
	}
	free(my_hash);

	/* Mark message as authenticated. */
	msg->flags |= MSG_AUTHENTICATED;

	post_quick_mode(msg);

	return 0;

cleanup:
	free(my_hash);
	return -1;
}
