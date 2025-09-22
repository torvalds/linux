/* $OpenBSD: isakmp_cfg.c,v 1.41 2018/01/15 09:54:48 mpi Exp $	 */

/*
 * Copyright (c) 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2002 Håkan Olsson.  All rights reserved.
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
 * This code was written under funding by Gatespace
 * (http://www.gatespace.com/).
 */

#include <sys/types.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <bitstring.h>

#include "attribute.h"
#include "conf.h"
#include "exchange.h"
#include "hash.h"
#include "ipsec.h"
#include "isakmp_fld.h"
#include "isakmp_num.h"
#include "log.h"
#include "message.h"
#include "prf.h"
#include "sa.h"
#include "transport.h"
#include "util.h"

/*
 * Validation script used to test messages for correct content of
 * payloads depending on the exchange type.
 */
int16_t script_transaction[] = {
	ISAKMP_PAYLOAD_ATTRIBUTE,	/* Initiator -> responder.  */
	EXCHANGE_SCRIPT_SWITCH,
	ISAKMP_PAYLOAD_ATTRIBUTE,	/* Responder -> initiator.  */
	EXCHANGE_SCRIPT_END
};

static int      cfg_decode_attribute(u_int16_t, u_int8_t *, u_int16_t, void *);
static int	cfg_encode_attributes(struct isakmp_cfg_attr_head *, u_int32_t,
    u_int32_t, char *, u_int8_t **, u_int16_t *);
static int      cfg_initiator_send_ATTR(struct message *);
static int      cfg_initiator_recv_ATTR(struct message *);
static int      cfg_responder_recv_ATTR(struct message *);
static int      cfg_responder_send_ATTR(struct message *);

u_int8_t       *cfg_add_hash(struct message *);
int		cfg_finalize_hash(struct message *, u_int8_t *, u_int8_t *,
    u_int16_t);
int             cfg_verify_hash(struct message *);

/* Server: SET/ACK    Client; REQ/REPLY */
int (*isakmp_cfg_initiator[]) (struct message *) = {
	cfg_initiator_send_ATTR,
	cfg_initiator_recv_ATTR
};

/* Server: REQ/REPLY  Client: SET/ACK */
int (*isakmp_cfg_responder[]) (struct message *) = {
	cfg_responder_recv_ATTR,
	cfg_responder_send_ATTR
};

/*
 * When we are "the server", this starts SET/ACK mode
 * When we are "the client", this starts REQ/REPLY mode
 */
static int
cfg_initiator_send_ATTR(struct message *msg)
{
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_exch *ie = msg->exchange->data;
	u_int8_t       *hashp = 0, *attrp, *attr;
	size_t          attrlen, off;
	char           *id_string, *cfg_mode, *field;
	struct sockaddr *sa;
#define CFG_ATTR_BIT_MAX ISAKMP_CFG_ATTR_FUTURE_MIN	/* XXX */
	bitstr_t        bit_decl(attrbits, CFG_ATTR_BIT_MAX);
	u_int16_t       bit, length;
	u_int32_t       life;

	if (msg->exchange->phase == 2) {
		hashp = cfg_add_hash(msg);
		if (!hashp)
			return -1;
	}
	/* We initiated this exchange, check isakmp_sa for other side.  */
	if (isakmp_sa->initiator)
		id_string = ipsec_id_string(isakmp_sa->id_r,
		    isakmp_sa->id_r_len);
	else
		id_string = ipsec_id_string(isakmp_sa->id_i,
		    isakmp_sa->id_i_len);
	if (!id_string) {
		log_print("cfg_initiator_send_ATTR: cannot parse ID");
		goto fail;
	}
	/* Check for attribute list to send to the other side */
	attrlen = 0;
	bit_nclear(attrbits, 0, CFG_ATTR_BIT_MAX - 1);

	cfg_mode = conf_get_str(id_string, "Mode");
	if (!cfg_mode || strcmp(cfg_mode, "SET") == 0) {
		/* SET/ACK mode */
		ie->cfg_type = ISAKMP_CFG_SET;

		LOG_DBG((LOG_NEGOTIATION, 10,
		    "cfg_initiator_send_ATTR: SET/ACK mode"));

#define ATTRFIND(STR,ATTR4,LEN4,ATTR6,LEN6) do				\
	{								\
		if ((sa = conf_get_address (id_string, STR)) != NULL)	\
			switch (sa->sa_family) {			\
			case AF_INET:					\
				bit_set (attrbits, ATTR4);		\
				attrlen += ISAKMP_ATTR_SZ + LEN4;	\
				break;					\
			case AF_INET6:					\
				bit_set (attrbits, ATTR6);		\
				attrlen += ISAKMP_ATTR_SZ + LEN6;	\
				break;					\
			default:					\
				break;					\
			}						\
		free (sa);						\
	} while (0)

		/*
		 * XXX We don't simultaneously support IPv4 and IPv6
		 * addresses.
		 */
		ATTRFIND("Address", ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS, 4,
		    ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS, 16);
		ATTRFIND("Netmask", ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK, 4,
		    ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK, 16);
		ATTRFIND("Nameserver", ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS, 4,
		    ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS, 16);
		ATTRFIND("WINS-server", ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS, 4,
		    ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS, 16);
		ATTRFIND("DHCP-server", ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP, 4,
		    ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP, 16);
#ifdef notyet
		ATTRFIND("Network", ISAKMP_CFG_ATTR_INTERNAL_IP4_SUBNET, 8,
		    ISAKMP_CFG_ATTR_INTERNAL_IP6_SUBNET, 17);
#endif
#undef ATTRFIND

		if (conf_get_str(id_string, "Lifetime")) {
			bit_set(attrbits,
			    ISAKMP_CFG_ATTR_INTERNAL_ADDRESS_EXPIRY);
			attrlen += ISAKMP_ATTR_SZ + 4;
		}
	} else {
		struct conf_list *alist;
		struct conf_list_node *anode;

		ie->cfg_type = ISAKMP_CFG_REQUEST;

		LOG_DBG((LOG_NEGOTIATION, 10,
		    "cfg_initiator_send_ATTR: REQ/REPLY mode"));

		alist = conf_get_list(id_string, "Attributes");
		if (alist) {
			for (anode = TAILQ_FIRST(&alist->fields); anode;
			    anode = TAILQ_NEXT(anode, link)) {
				if (strcasecmp(anode->field, "Address") == 0) {
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS);
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS);
					attrlen += ISAKMP_ATTR_SZ * 2;
				} else if (strcasecmp(anode->field, "Netmask")
				    == 0) {
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK);
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK);
					attrlen += ISAKMP_ATTR_SZ * 2;
				} else if (strcasecmp(anode->field,
				    "Nameserver") == 0) {
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS);
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS);
					attrlen += ISAKMP_ATTR_SZ * 2;
				} else if (strcasecmp(anode->field,
				    "WINS-server") == 0) {
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS);
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS);
					attrlen += ISAKMP_ATTR_SZ * 2;
				} else if (strcasecmp(anode->field,
				    "DHCP-server") == 0) {
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP);
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP);
					attrlen += ISAKMP_ATTR_SZ * 2;
				} else if (strcasecmp(anode->field,
				    "Lifetime") == 0) {
					bit_set(attrbits, ISAKMP_CFG_ATTR_INTERNAL_ADDRESS_EXPIRY);
					attrlen += ISAKMP_ATTR_SZ;
				} else {
					log_print("cfg_initiator_send_ATTR: "
					    "unknown attribute %.20s in "
					    "section [%s]", anode->field,
					    id_string);
				}
			}

			conf_free_list(alist);
		}
	}

	if (attrlen == 0) {
		/* No data found.  */
		log_print("cfg_initiator_send_ATTR: no IKECFG attributes "
		    "found for [%s]", id_string);

		/*
		 * We can continue, but this indicates a configuration error
		 * that the user probably will want to correct.
		 */
		free(id_string);
		return 0;
	}
	attrlen += ISAKMP_ATTRIBUTE_SZ;
	attrp = calloc(1, attrlen);
	if (!attrp) {
		log_error("cfg_initiator_send_ATTR: calloc (1, %lu) failed",
		    (unsigned long)attrlen);
		goto fail;
	}
	if (message_add_payload(msg, ISAKMP_PAYLOAD_ATTRIBUTE, attrp, attrlen,
	    1)) {
		free(attrp);
		goto fail;
	}
	SET_ISAKMP_ATTRIBUTE_TYPE(attrp, ie->cfg_type);
	arc4random_buf((u_int8_t *) & ie->cfg_id, sizeof ie->cfg_id);
	SET_ISAKMP_ATTRIBUTE_ID(attrp, ie->cfg_id);

	off = ISAKMP_ATTRIBUTE_SZ;

	/*
	 * Use the bitstring built previously to collect the right
	 * parameters for attrp.
	 */
	for (bit = 0; bit < CFG_ATTR_BIT_MAX; bit++)
		if (bit_test(attrbits, bit)) {
			attr = attrp + off;
			SET_ISAKMP_ATTR_TYPE(attr, bit);

			if (ie->cfg_type == ISAKMP_CFG_REQUEST) {
				off += ISAKMP_ATTR_SZ;
				continue;
			}
			/* All the other are similar, this is the odd one.  */
			if (bit == ISAKMP_CFG_ATTR_INTERNAL_ADDRESS_EXPIRY) {
				life = conf_get_num(id_string, "Lifetime",
				    1200);
				SET_ISAKMP_ATTR_LENGTH_VALUE(attr, 4);
				encode_32(attr + ISAKMP_ATTR_VALUE_OFF, life);
				off += ISAKMP_ATTR_SZ + 4;
				continue;
			}
			switch (bit) {
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK:
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP:
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS:
				length = 4;
				break;

			case ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS:
				length = 16;
				break;

			default:
				length = 0;	/* Silence gcc.  */
			}

			switch (bit) {
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS:
				field = "Address";
				break;
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK:
				field = "Netmask";
				break;
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS:
				field = "Nameserver";
				break;
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP:
				field = "DHCP-server";
				break;
			case ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS:
			case ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS:
				field = "WINS-server";
				break;
			default:
				field = 0;	/* Silence gcc.  */
			}

			sa = conf_get_address(id_string, field);

			SET_ISAKMP_ATTR_LENGTH_VALUE(attr, length);
			memcpy(attr + ISAKMP_ATTR_VALUE_OFF,
			    sockaddr_addrdata(sa), length);

			free(sa);

			off += ISAKMP_ATTR_SZ + length;
		}
	if (msg->exchange->phase == 2)
		if (cfg_finalize_hash(msg, hashp, attrp, attrlen))
			goto fail;

	return 0;

fail:
	free(id_string);
	return -1;
}

/*
 * As "the server", this ends SET/ACK.
 * As "the client", this ends REQ/REPLY.
 */
static int
cfg_initiator_recv_ATTR(struct message *msg)
{
	struct payload *attrp = payload_first(msg, ISAKMP_PAYLOAD_ATTRIBUTE);
	struct ipsec_exch *ie = msg->exchange->data;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct isakmp_cfg_attr *attr;
	struct sockaddr *sa;
	const char     *uk_addr = "<unknown>";
	char           *addr;

	if (msg->exchange->phase == 2)
		if (cfg_verify_hash(msg))
			return -1;

	/* Sanity.  */
	if (ie->cfg_id != GET_ISAKMP_ATTRIBUTE_ID(attrp->p)) {
		log_print("cfg_initiator_recv_ATTR: "
		    "cfg packet ID does not match!");
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		return -1;
	}
	switch (attrp->p[ISAKMP_ATTRIBUTE_TYPE_OFF]) {
	case ISAKMP_CFG_ACK:
		if (ie->cfg_type != ISAKMP_CFG_SET) {
			log_print("cfg_initiator_recv_ATTR: "
			    "bad packet type ACK");
			message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED,
			    0, 1, 0);
			return -1;
		}
		break;
	case ISAKMP_CFG_REPLY:
		if (ie->cfg_type != ISAKMP_CFG_REQUEST) {
			log_print("cfg_initiator_recv_ATTR: "
			    "bad packet type REPLY");
			message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED,
			    0, 1, 0);
			return -1;
		}
		break;

	default:
		log_print("cfg_initiator_recv_ATTR: unexpected configuration "
		    "message type %d", attrp->p[ISAKMP_ATTRIBUTE_TYPE_OFF]);
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		return -1;
	}

	attribute_map(attrp->p + ISAKMP_ATTRIBUTE_ATTRS_OFF,
	    GET_ISAKMP_GEN_LENGTH(attrp->p) - ISAKMP_TRANSFORM_SA_ATTRS_OFF,
	    cfg_decode_attribute, ie);

	switch (ie->cfg_type) {
	case ISAKMP_CFG_ACK: {
			/* SET/ACK -- Server side (ACK from client) */
			msg->transport->vtbl->get_src(isakmp_sa->transport,
			    &sa);
			if (sockaddr2text(sa, &addr, 0) < 0)
				addr = (char *) uk_addr;

			for (attr = LIST_FIRST(&ie->attrs); attr;
			    attr = LIST_NEXT(attr, link))
				LOG_DBG((LOG_NEGOTIATION, 50,
				    "cfg_initiator_recv_ATTR: "
				    "client %s ACKs attribute %s", addr,
				    constant_name(isakmp_cfg_attr_cst,
					attr->type)));

			if (addr != uk_addr)
				free(addr);
		}
		break;

	case ISAKMP_CFG_REPLY: {
			/*
			 * REQ/REPLY: effect attributes we've gotten
			 * responses on.
			 */
			msg->transport->vtbl->get_src(isakmp_sa->transport,
			    &sa);
			if (sockaddr2text(sa, &addr, 0) < 0)
				addr = (char *) uk_addr;

			for (attr = LIST_FIRST(&ie->attrs); attr;
			    attr = LIST_NEXT(attr, link))
				LOG_DBG((LOG_NEGOTIATION, 50,
				    "cfg_initiator_recv_ATTR: "
				    "server %s replied with attribute %s",
				    addr, constant_name(isakmp_cfg_attr_cst,
					attr->type)));

			if (addr != uk_addr)
				free(addr);
		}
		break;

	default:
		break;
	}

	attrp->flags |= PL_MARK;
	return 0;
}

/*
 * As "the server", this starts REQ/REPLY (initiated by the client).
 * As "the client", this starts SET/ACK (initiated by the server).
 */
static int
cfg_responder_recv_ATTR(struct message *msg)
{
	struct payload *attrp = payload_first(msg, ISAKMP_PAYLOAD_ATTRIBUTE);
	struct ipsec_exch *ie = msg->exchange->data;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct isakmp_cfg_attr *attr;
	struct sockaddr *sa;
	char           *addr;

	if (msg->exchange->phase == 2)
		if (cfg_verify_hash(msg))
			return -1;

	ie->cfg_id = GET_ISAKMP_ATTRIBUTE_ID(attrp->p);
	ie->cfg_type = attrp->p[ISAKMP_ATTRIBUTE_TYPE_OFF];

	switch (ie->cfg_type) {
	case ISAKMP_CFG_REQUEST:
	case ISAKMP_CFG_SET:
		break;

	default:
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		log_print("cfg_responder_recv_ATTR: "
		    "unexpected configuration message type %d", ie->cfg_type);
		return -1;
	}

	attribute_map(attrp->p + ISAKMP_ATTRIBUTE_ATTRS_OFF,
	    GET_ISAKMP_GEN_LENGTH(attrp->p) - ISAKMP_TRANSFORM_SA_ATTRS_OFF,
	    cfg_decode_attribute, ie);

	switch (ie->cfg_type) {
	case ISAKMP_CFG_REQUEST:
		/* We're done.  */
		break;

	case ISAKMP_CFG_SET: {
			/* SET/ACK -- Client side (SET from server) */
			const char     *uk_addr = "<unknown>";

			msg->transport->vtbl->get_dst(isakmp_sa->transport,
			    &sa);
			if (sockaddr2text(sa, &addr, 0) < 0)
				addr = (char *) uk_addr;

			for (attr = LIST_FIRST(&ie->attrs); attr;
			    attr = LIST_NEXT(attr, link))
				LOG_DBG((LOG_NEGOTIATION, 50,
				    "cfg_responder_recv_ATTR: "
				    "server %s asks us to SET attribute %s",
				    addr, constant_name(isakmp_cfg_attr_cst,
					attr->type)));

			/*
			 * XXX Here's the place to add code to walk through
			 * XXX each attribute and send them along to dhclient
			 * XXX or whatever. Each attribute that we act upon
			 * XXX (such as setting a netmask), should be marked
			 * XXX like this for us to send the proper ACK
			 * XXX response: attr->attr_used++;
			 */

			if (addr != uk_addr)
				free(addr);
		}
		break;

	default:
		break;
	}

	attrp->flags |= PL_MARK;
	return 0;
}

/*
 * As "the server", this ends REQ/REPLY mode.
 * As "the client", this ends SET/ACK mode.
 */
static int
cfg_responder_send_ATTR(struct message *msg)
{
	struct ipsec_exch *ie = msg->exchange->data;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	u_int8_t       *hashp = 0, *attrp;
	u_int16_t       attrlen;
	char           *id_string;

	if (msg->exchange->phase == 2) {
		hashp = cfg_add_hash(msg);
		if (!hashp)
			return -1;
	}
	/* We are responder, check isakmp_sa for other side.  */
	if (isakmp_sa->initiator ^ (ie->cfg_type == ISAKMP_CFG_REQUEST))
		id_string = ipsec_id_string(isakmp_sa->id_i,
		    isakmp_sa->id_i_len);
	else
		id_string = ipsec_id_string(isakmp_sa->id_r,
		    isakmp_sa->id_r_len);
	if (!id_string) {
		log_print("cfg_responder_send_ATTR: cannot parse client's ID");
		return -1;
	}
	if (cfg_encode_attributes(&ie->attrs, (ie->cfg_type == ISAKMP_CFG_SET ?
	    ISAKMP_CFG_ACK : ISAKMP_CFG_REPLY), ie->cfg_id, id_string, &attrp,
	    &attrlen)) {
		free(id_string);
		return -1;
	}
	free(id_string);

	if (message_add_payload(msg, ISAKMP_PAYLOAD_ATTRIBUTE, attrp, attrlen,
	    1)) {
		free(attrp);
		return -1;
	}
	if (msg->exchange->phase == 2)
		if (cfg_finalize_hash(msg, hashp, attrp, attrlen))
			return -1;

	return 0;
}

u_int8_t *
cfg_add_hash(struct message *msg)
{
	struct ipsec_sa *isa = msg->isakmp_sa->data;
	struct hash    *hash = hash_get(isa->hash);
	u_int8_t       *hashp;

	hashp = malloc(ISAKMP_HASH_SZ + hash->hashsize);
	if (!hashp) {
		log_error("cfg_add_hash: malloc (%lu) failed",
		    ISAKMP_HASH_SZ + (unsigned long)hash->hashsize);
		return 0;
	}
	if (message_add_payload(msg, ISAKMP_PAYLOAD_HASH, hashp,
	    ISAKMP_HASH_SZ + hash->hashsize, 1)) {
		free(hashp);
		return 0;
	}
	return hashp;
}

int
cfg_finalize_hash(struct message *msg, u_int8_t *hashp, u_int8_t *data,
    u_int16_t length)
{
	struct ipsec_sa *isa = msg->isakmp_sa->data;
	struct prf     *prf;

	prf = prf_alloc(isa->prf_type, isa->hash, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		return -1;

	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, msg->exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	prf->Update(prf->prfctx, data, length);
	prf->Final(hashp + ISAKMP_GEN_SZ, prf->prfctx);
	prf_free(prf);
	return 0;
}

int
cfg_verify_hash(struct message *msg)
{
	struct payload *hashp = payload_first(msg, ISAKMP_PAYLOAD_HASH);
	struct ipsec_sa *isa = msg->isakmp_sa->data;
	struct prf     *prf;
	u_int8_t       *hash, *comp_hash;
	size_t          hash_len;

	if (!hashp) {
		log_print("cfg_verify_hash: phase 2 message missing HASH");
		message_drop(msg, ISAKMP_NOTIFY_INVALID_HASH_INFORMATION,
		    0, 1, 0);
		return -1;
	}
	hash = hashp->p;
	hash_len = GET_ISAKMP_GEN_LENGTH(hash);
	comp_hash = malloc(hash_len - ISAKMP_GEN_SZ);
	if (!comp_hash) {
		log_error("cfg_verify_hash: malloc (%lu) failed",
		    (unsigned long)hash_len - ISAKMP_GEN_SZ);
		return -1;
	}
	/* Verify hash.  */
	prf = prf_alloc(isa->prf_type, isa->hash, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf) {
		free(comp_hash);
		return -1;
	}
	prf->Init(prf->prfctx);
	prf->Update(prf->prfctx, msg->exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);
	prf->Update(prf->prfctx, hash + hash_len,
	    msg->iov[0].iov_len - ISAKMP_HDR_SZ - hash_len);
	prf->Final(comp_hash, prf->prfctx);
	prf_free(prf);

	if (memcmp(hash + ISAKMP_GEN_SZ, comp_hash, hash_len - ISAKMP_GEN_SZ)
	    != 0) {
		message_drop(msg, ISAKMP_NOTIFY_INVALID_HASH_INFORMATION,
		    0, 1, 0);
		free(comp_hash);
		return -1;
	}
	free(comp_hash);

	/* Mark the HASH as handled.  */
	hashp->flags |= PL_MARK;

	/* Mark message authenticated. */
	msg->flags |= MSG_AUTHENTICATED;

	return 0;
}

/*
 * Decode the attribute of type TYPE with a LEN length value pointed to by
 * VALUE.  VIE is a pointer to the IPsec exchange context holding the
 * attributes indexed by type for easy retrieval.
 */
static int
cfg_decode_attribute(u_int16_t type, u_int8_t * value, u_int16_t len,
    void *vie)
{
	struct ipsec_exch *ie = vie;
	struct isakmp_cfg_attr *attr;

	if (type >= ISAKMP_CFG_ATTR_PRIVATE_MIN &&
	    type <= ISAKMP_CFG_ATTR_PRIVATE_MAX)
		return 0;
	if (type == 0 || type >= ISAKMP_CFG_ATTR_FUTURE_MIN) {
		LOG_DBG((LOG_NEGOTIATION, 30,
		    "cfg_decode_attribute: invalid attr type %u", type));
		return -1;
	}
	attr = calloc(1, sizeof *attr);
	if (!attr) {
		log_error("cfg_decode_attribute: calloc (1, %lu) failed",
		    (unsigned long)sizeof *attr);
		return -1;
	}
	attr->type = type;
	attr->length = len;
	if (len) {
		attr->value = malloc(len);
		if (!attr->value) {
			log_error("cfg_decode_attribute: malloc (%d) failed",
			    len);
			free(attr);
			/* Should we also deallocate all other values?  */
			return -1;
		}
		memcpy(attr->value, value, len);
	}
	LIST_INSERT_HEAD(&ie->attrs, attr, link);
	return 0;
}

/*
 * Encode list of attributes from ie->attrs into a attribute payload.
 */
static int
cfg_encode_attributes(struct isakmp_cfg_attr_head *attrs, u_int32_t type,
    u_int32_t cfg_id, char *id_string, u_int8_t **attrp, u_int16_t *len)
{
	struct isakmp_cfg_attr *attr;
	struct sockaddr *sa;
	sa_family_t     family;
	u_int32_t       value;
	u_int16_t       off;
	char           *field;

	/* Compute length */
	*len = ISAKMP_ATTRIBUTE_SZ;
	for (attr = LIST_FIRST(attrs); attr; attr = LIST_NEXT(attr, link)) {
		/* With ACK we only include the attrs we've actually used.  */
		if (type == ISAKMP_CFG_ACK && attr->attr_used == 0)
			continue;

		switch (attr->type) {
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS:
		case ISAKMP_CFG_ATTR_INTERNAL_ADDRESS_EXPIRY:
			attr->length = 4;
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP4_SUBNET:
			attr->length = 8;
			break;

		case ISAKMP_CFG_ATTR_APPLICATION_VERSION:
			/* XXX So far no version identifier of isakmpd here. */
			attr->length = 0;
			break;

		case ISAKMP_CFG_ATTR_SUPPORTED_ATTRIBUTES:
			attr->length = 2 * 15;
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS:
			attr->length = 16;
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP6_SUBNET:
			attr->length = 17;
			break;

		default:
			attr->ignore++;
			/* XXX Log!  */
		}
		*len += ISAKMP_ATTR_SZ + attr->length;
	}

	/* Allocate enough space for the payload */
	*attrp = calloc(1, *len);
	if (!*attrp) {
		log_error("cfg_encode_attributes: calloc (1, %lu) failed",
		    (unsigned long)*len);
		return -1;
	}
	SET_ISAKMP_ATTRIBUTE_TYPE(*attrp, type);
	SET_ISAKMP_ATTRIBUTE_ID(*attrp, cfg_id);

	off = ISAKMP_ATTRIBUTE_SZ;
	for (attr = LIST_FIRST(attrs); attr; attr = LIST_NEXT(attr, link)) {
		/* With ACK we only include the attrs we've actually used.  */
		if (type == ISAKMP_CFG_ACK && attr->attr_used == 0)
			continue;

		switch (attr->type) {
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_SUBNET:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS:
			family = AF_INET;
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_SUBNET:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS:
			family = AF_INET6;
			break;

		default:
			family = 0;
			break;
		}

		switch (attr->type) {
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS:
			field = "Address";
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP4_SUBNET:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_SUBNET:
			field = "Network";	/* XXX or just "Address" */
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK:
			field = "Netmask";
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP:
			field = "DHCP-server";
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS:
			field = "Nameserver";
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS:
			field = "WINS-server";
			break;

		default:
			field = 0;
		}

		switch (attr->type) {
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_ADDRESS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NETMASK:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DHCP:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_DNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP4_NBNS:
		case ISAKMP_CFG_ATTR_INTERNAL_IP6_NBNS:
			sa = conf_get_address(id_string, field);
			if (!sa) {
				LOG_DBG((LOG_NEGOTIATION, 10,
				    "cfg_responder_send_ATTR: "
				    "attribute not found: %s", field));
				attr->length = 0;
				break;
			}
			if (sa->sa_family != family) {
				log_print("cfg_responder_send_ATTR: "
				    "attribute %s - expected %s got %s data",
				    field,
				    (family == AF_INET ? "IPv4" : "IPv6"),
				    (sa->sa_family ==
					AF_INET ? "IPv4" : "IPv6"));
				free(sa);
				attr->length = 0;
				break;
			}
			/* Temporary limit length for the _SUBNET types. */
			if (attr->type == ISAKMP_CFG_ATTR_INTERNAL_IP4_SUBNET)
				attr->length = 4;
			else if (attr->type ==
			    ISAKMP_CFG_ATTR_INTERNAL_IP6_SUBNET)
				attr->length = 16;

			memcpy(*attrp + off + ISAKMP_ATTR_VALUE_OFF,
			    sockaddr_addrdata(sa), attr->length);
			free(sa);

			/* _SUBNET types need some extra work. */
			if (attr->type ==
			    ISAKMP_CFG_ATTR_INTERNAL_IP4_SUBNET) {
				sa = conf_get_address(id_string, "Netmask");
				if (!sa) {
					LOG_DBG((LOG_NEGOTIATION, 10,
					    "cfg_responder_send_ATTR: "
					   "attribute not found: Netmask"));
					attr->length = 0;
					break;
				}
				if (sa->sa_family != AF_INET) {
					log_print("cfg_responder_send_ATTR: "
					    "attribute Netmask - expected "
					    "IPv4 got IPv6 data");
					free(sa);
					attr->length = 0;
					break;
				}
				memcpy(*attrp + off + ISAKMP_ATTR_VALUE_OFF +
				    attr->length, sockaddr_addrdata(sa),
				    attr->length);
				attr->length = 8;
				free(sa);
			} else if (attr->type ==
			    ISAKMP_CFG_ATTR_INTERNAL_IP6_SUBNET) {
				int prefix = conf_get_num(id_string, "Prefix",
				    -1);

				if (prefix == -1) {
					log_print("cfg_responder_send_ATTR: "
					    "attribute not found: Prefix");
					attr->length = 0;
					break;
				} else if (prefix < -1 || prefix > 128) {
					log_print("cfg_responder_send_ATTR: "
					    "attribute Prefix - invalid "
					    "value %d", prefix);
					attr->length = 0;
					break;
				}
				*(*attrp + off + ISAKMP_ATTR_VALUE_OFF + 16) =
				    (u_int8_t)prefix;
				attr->length = 17;
			}
			break;

		case ISAKMP_CFG_ATTR_INTERNAL_ADDRESS_EXPIRY:
			value = conf_get_num(id_string, "Lifetime", 1200);
			encode_32(*attrp + off + ISAKMP_ATTR_VALUE_OFF, value);
			break;

		case ISAKMP_CFG_ATTR_APPLICATION_VERSION:
			/* XXX So far no version identifier of isakmpd here. */
			break;

		case ISAKMP_CFG_ATTR_SUPPORTED_ATTRIBUTES:
			break;

		default:
			break;
		}

		SET_ISAKMP_ATTR_TYPE(*attrp + off, attr->type);
		SET_ISAKMP_ATTR_LENGTH_VALUE(*attrp + off, attr->length);
		off += ISAKMP_ATTR_VALUE_OFF + attr->length;
	}

	return 0;
}
