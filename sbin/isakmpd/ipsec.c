/* $OpenBSD: ipsec.c,v 1.155 2025/04/30 03:53:21 tb Exp $	 */
/* $EOM: ipsec.c,v 1.143 2000/12/11 23:57:42 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
 * Copyright (c) 2001 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 2001 Håkan Olsson.  All rights reserved.
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
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <net/if.h>
#include <net/pfvar.h>

#include "attribute.h"
#include "conf.h"
#include "connection.h"
#include "constants.h"
#include "crypto.h"
#include "dh.h"
#include "doi.h"
#include "dpd.h"
#include "exchange.h"
#include "hash.h"
#include "ike_aggressive.h"
#include "ike_auth.h"
#include "ike_main_mode.h"
#include "ike_quick_mode.h"
#include "ipsec.h"
#include "ipsec_doi.h"
#include "isakmp.h"
#include "isakmp_cfg.h"
#include "isakmp_fld.h"
#include "isakmp_num.h"
#include "log.h"
#include "message.h"
#include "nat_traversal.h"
#include "pf_key_v2.h"
#include "prf.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "util.h"
#include "x509.h"

extern int acquire_only;

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

/* The replay window size used for all IPsec protocols if not overridden.  */
#define DEFAULT_REPLAY_WINDOW 16

struct ipsec_decode_arg {
	struct message *msg;
	struct sa      *sa;
	struct proto   *proto;
};

/* These variables hold the contacted peers ADT state.  */
struct contact {
	struct sockaddr *addr;
	socklen_t       len;
}              *contacts = 0;
int             contact_cnt = 0, contact_limit = 0;

static int      addr_cmp(const void *, const void *);
static int      ipsec_add_contact(struct message *);
static int      ipsec_contacted(struct message *);
static int      ipsec_debug_attribute(u_int16_t, u_int8_t *, u_int16_t,
    void *);
static void     ipsec_delete_spi(struct sa *, struct proto *, int);
static int16_t *ipsec_exchange_script(u_int8_t);
static void     ipsec_finalize_exchange(struct message *);
static void     ipsec_free_exchange_data(void *);
static void     ipsec_free_proto_data(void *);
static void     ipsec_free_sa_data(void *);
static struct keystate *ipsec_get_keystate(struct message *);
static u_int8_t *ipsec_get_spi(size_t *, u_int8_t, struct message *);
static int	ipsec_handle_leftover_payload(struct message *, u_int8_t,
    struct payload *);
static int      ipsec_informational_post_hook(struct message *);
static int      ipsec_informational_pre_hook(struct message *);
static int      ipsec_initiator(struct message *);
static void     ipsec_proto_init(struct proto *, char *);
static int      ipsec_responder(struct message *);
static void     ipsec_setup_situation(u_int8_t *);
static int      ipsec_set_network(u_int8_t *, u_int8_t *, struct sa *);
static size_t   ipsec_situation_size(void);
static u_int8_t ipsec_spi_size(u_int8_t);
static int      ipsec_validate_attribute(u_int16_t, u_int8_t *, u_int16_t,
    void *);
static int      ipsec_validate_exchange(u_int8_t);
static int	ipsec_validate_id_information(u_int8_t, u_int8_t *, u_int8_t *,
    size_t, struct exchange *);
static int      ipsec_validate_key_information(u_int8_t *, size_t);
static int      ipsec_validate_notification(u_int16_t);
static int      ipsec_validate_proto(u_int8_t);
static int      ipsec_validate_situation(u_int8_t *, size_t *, size_t);
static int      ipsec_validate_transform_id(u_int8_t, u_int8_t);
static int      ipsec_sa_check_flow(struct sa *, void *);
static int      ipsec_sa_check_flow_any(struct sa *, void *);
static int      ipsec_sa_tag(struct exchange *, struct sa *, struct sa *);
static int      ipsec_sa_iface(struct exchange *, struct sa *, struct sa *);

static struct doi ipsec_doi = {
	{0}, IPSEC_DOI_IPSEC,
	sizeof(struct ipsec_exch), sizeof(struct ipsec_sa),
	sizeof(struct ipsec_proto),
	ipsec_debug_attribute,
	ipsec_delete_spi,
	ipsec_exchange_script,
	ipsec_finalize_exchange,
	ipsec_free_exchange_data,
	ipsec_free_proto_data,
	ipsec_free_sa_data,
	ipsec_get_keystate,
	ipsec_get_spi,
	ipsec_handle_leftover_payload,
	ipsec_informational_post_hook,
	ipsec_informational_pre_hook,
	ipsec_is_attribute_incompatible,
	ipsec_proto_init,
	ipsec_setup_situation,
	ipsec_situation_size,
	ipsec_spi_size,
	ipsec_validate_attribute,
	ipsec_validate_exchange,
	ipsec_validate_id_information,
	ipsec_validate_key_information,
	ipsec_validate_notification,
	ipsec_validate_proto,
	ipsec_validate_situation,
	ipsec_validate_transform_id,
	ipsec_initiator,
	ipsec_responder,
	ipsec_decode_ids
};

int16_t script_quick_mode[] = {
	ISAKMP_PAYLOAD_HASH,	/* Initiator -> responder.  */
	ISAKMP_PAYLOAD_SA,
	ISAKMP_PAYLOAD_NONCE,
	EXCHANGE_SCRIPT_SWITCH,
	ISAKMP_PAYLOAD_HASH,	/* Responder -> initiator.  */
	ISAKMP_PAYLOAD_SA,
	ISAKMP_PAYLOAD_NONCE,
	EXCHANGE_SCRIPT_SWITCH,
	ISAKMP_PAYLOAD_HASH,	/* Initiator -> responder.  */
	EXCHANGE_SCRIPT_END
};

int16_t script_new_group_mode[] = {
	ISAKMP_PAYLOAD_HASH,	/* Initiator -> responder.  */
	ISAKMP_PAYLOAD_SA,
	EXCHANGE_SCRIPT_SWITCH,
	ISAKMP_PAYLOAD_HASH,	/* Responder -> initiator.  */
	ISAKMP_PAYLOAD_SA,
	EXCHANGE_SCRIPT_END
};

struct dst_spi_proto_arg {
	struct sockaddr *dst;
	u_int32_t       spi;
	u_int8_t        proto;
};

/*
 * Check if SA matches what we are asking for through V_ARG.  It has to
 * be a finished phase 2 SA.
 * if "proto" arg is 0, match any proto
 */
static int
ipsec_sa_check(struct sa *sa, void *v_arg)
{
	struct dst_spi_proto_arg *arg = v_arg;
	struct proto   *proto;
	struct sockaddr *dst, *src;
	int             incoming;

	if (sa->phase != 2 || !(sa->flags & SA_FLAG_READY))
		return 0;

	sa->transport->vtbl->get_dst(sa->transport, &dst);
	if (memcmp(sockaddr_addrdata(dst), sockaddr_addrdata(arg->dst),
	    sockaddr_addrlen(dst)) == 0)
		incoming = 0;
	else {
		sa->transport->vtbl->get_src(sa->transport, &src);
		if (memcmp(sockaddr_addrdata(src), sockaddr_addrdata(arg->dst),
		    sockaddr_addrlen(src)) == 0)
			incoming = 1;
		else
			return 0;
	}

	for (proto = TAILQ_FIRST(&sa->protos); proto;
	    proto = TAILQ_NEXT(proto, link))
		if ((arg->proto == 0 || proto->proto == arg->proto) &&
		    memcmp(proto->spi[incoming], &arg->spi, sizeof arg->spi)
		    == 0)
			return 1;
	return 0;
}

/* Find an SA with a "name" of DST, SPI & PROTO.  */
struct sa *
ipsec_sa_lookup(struct sockaddr *dst, u_int32_t spi, u_int8_t proto)
{
	struct dst_spi_proto_arg arg;

	arg.dst = dst;
	arg.spi = spi;
	arg.proto = proto;
	return sa_find(ipsec_sa_check, &arg);
}

/*
 * Check if SA matches the flow of another SA in V_ARG.  It has to
 * be a finished non-replaced phase 2 SA.
 * XXX At some point other selectors will matter here too.
 */
static int
ipsec_sa_check_flow(struct sa *sa, void *v_arg)
{
	if ((sa->flags & (SA_FLAG_READY | SA_FLAG_REPLACED)) != SA_FLAG_READY)
		return 0;

	return ipsec_sa_check_flow_any(sa, v_arg);
}

static int
ipsec_sa_check_flow_any(struct sa *sa, void *v_arg)
{
	struct sa      *sa2 = v_arg;
	struct ipsec_sa *isa = sa->data, *isa2 = sa2->data;

	if (sa == sa2 || sa->phase != 2 ||
	    (sa->flags & SA_FLAG_READY) != SA_FLAG_READY)
		return 0;

	if (isa->tproto != isa2->tproto || isa->sport != isa2->sport ||
	    isa->dport != isa2->dport)
		return 0;

	if ((sa->flags & SA_FLAG_IFACE) != (sa2->flags & SA_FLAG_IFACE))
		return 0;

	if (sa->flags & SA_FLAG_IFACE)
		return sa->iface == sa2->iface;

	/*
	 * If at least one of the IPsec SAs is incomplete, we're done.
	 */
	if (isa->src_net == NULL || isa2->src_net == NULL ||
	    isa->dst_net == NULL || isa2->dst_net == NULL ||
	    isa->src_mask == NULL || isa2->src_mask == NULL ||
	    isa->dst_mask == NULL || isa2->dst_mask == NULL)
		return 0;

	return isa->src_net->sa_family == isa2->src_net->sa_family &&
	    memcmp(sockaddr_addrdata(isa->src_net),
	    sockaddr_addrdata(isa2->src_net),
	    sockaddr_addrlen(isa->src_net)) == 0 &&
	    memcmp(sockaddr_addrdata(isa->src_mask),
	    sockaddr_addrdata(isa2->src_mask),
	    sockaddr_addrlen(isa->src_mask)) == 0 &&
	    memcmp(sockaddr_addrdata(isa->dst_net),
	    sockaddr_addrdata(isa2->dst_net),
	    sockaddr_addrlen(isa->dst_net)) == 0 &&
	    memcmp(sockaddr_addrdata(isa->dst_mask),
	    sockaddr_addrdata(isa2->dst_mask),
	    sockaddr_addrlen(isa->dst_mask)) == 0;
}

/*
 * Construct a PF tag if specified in the configuration.
 * It is possible to use variables to expand the tag:
 * $id		The string representation of the remote ID
 * $domain	The stripped domain part of the ID (for FQDN and UFQDN)
 */
static int
ipsec_sa_tag(struct exchange *exchange, struct sa *sa, struct sa *isakmp_sa)
{
	char *format, *section;
	char *id_string = NULL, *domain = NULL;
	int error = -1;
	size_t len;

	sa->tag = NULL;

	if (exchange->name == NULL ||
	    (section = exchange->name) == NULL ||
	    (format = conf_get_str(section, "PF-Tag")) == NULL)
		return (0);	/* ignore if not present */

	len = PF_TAG_NAME_SIZE;
	if ((sa->tag = calloc(1, len)) == NULL) {
		log_error("ipsec_sa_tag: calloc");
		goto fail;
	}
	if (strlcpy(sa->tag, format, len) >= len) {
		log_print("ipsec_sa_tag: tag too long");
		goto fail;
	}
	if (isakmp_sa->initiator)
		id_string = ipsec_id_string(isakmp_sa->id_r,
		    isakmp_sa->id_r_len);
	else
		id_string = ipsec_id_string(isakmp_sa->id_i,
		    isakmp_sa->id_i_len);

	if (strstr(format, "$id") != NULL) {
		if (id_string == NULL) {
			log_print("ipsec_sa_tag: cannot get ID");
			goto fail;
		}
		if (expand_string(sa->tag, len, "$id", id_string) != 0) {
			log_print("ipsec_sa_tag: failed to expand tag");
			goto fail;
		}
	}

	if (strstr(format, "$domain") != NULL) {
		if (id_string == NULL) {
			log_print("ipsec_sa_tag: cannot get ID");
			goto fail;
		}
		if (strncmp(id_string, "fqdn/", strlen("fqdn/")) == 0)
			domain = strchr(id_string, '.');
		else if (strncmp(id_string, "ufqdn/", strlen("ufqdn/")) == 0)
			domain = strchr(id_string, '@');
		if (domain == NULL || strlen(domain) < 2) {
			log_print("ipsec_sa_tag: no valid domain in ID %s",
			    id_string);
			goto fail;
		}
		domain++;
		if (expand_string(sa->tag, len, "$domain", domain) != 0) {
			log_print("ipsec_sa_tag: failed to expand tag");
			goto fail;
		}
	}

	LOG_DBG((LOG_SA, 10, "ipsec_sa_tag: tag_len %ld tag \"%s\"",
	    strlen(sa->tag), sa->tag));

	error = 0;
 fail:
	free(id_string);
	if (error != 0) {
		free(sa->tag);
		sa->tag = NULL;
	}

	return (error);
}

static int
ipsec_sa_iface(struct exchange *exchange, struct sa *sa, struct sa *isakmp_sa)
{
	char *section, *value;
	const char *errstr = NULL;

	if (exchange->name == NULL ||
	    (section = exchange->name) == NULL ||
	    (value = conf_get_str(section, "Interface")) == NULL)
		return (0);	/* ignore if not present */

	sa->iface = strtonum(value, 0, UINT_MAX, &errstr);
	if (errstr != NULL) {
		log_error("[%s]:Interface %s", section, errstr);
		return (-1);
	}

	sa->flags |= SA_FLAG_IFACE;

	return (0);
}

/*
 * Do IPsec DOI specific finalizations task for the exchange where MSG was
 * the final message.
 */
static void
ipsec_finalize_exchange(struct message *msg)
{
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa;
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct sa      *sa = 0, *old_sa;
	struct proto   *proto, *last_proto = 0;
	char           *addr1, *addr2, *mask1, *mask2;

	switch (exchange->phase) {
	case 1:
		switch (exchange->type) {
		case ISAKMP_EXCH_ID_PROT:
		case ISAKMP_EXCH_AGGRESSIVE:
			isa = isakmp_sa->data;
			isa->hash = ie->hash->type;
			isa->prf_type = ie->prf_type;
			isa->skeyid_len = ie->skeyid_len;
			isa->skeyid_d = ie->skeyid_d;
			isa->skeyid_a = ie->skeyid_a;
			/* Prevents early free of SKEYID_*.  */
			ie->skeyid_a = ie->skeyid_d = 0;

			/*
			 * If a lifetime was negotiated setup the expiration
			 * timers.
			 */
			if (isakmp_sa->seconds)
				sa_setup_expirations(isakmp_sa);

			if (isakmp_sa->flags & SA_FLAG_NAT_T_KEEPALIVE)
				nat_t_setup_keepalive(isakmp_sa);
			break;
		}
		break;

	case 2:
		switch (exchange->type) {
		case IKE_EXCH_QUICK_MODE:
			/*
			 * Tell the application(s) about the SPIs and key
			 * material.
			 */
			for (sa = TAILQ_FIRST(&exchange->sa_list); sa;
			    sa = TAILQ_NEXT(sa, next)) {
				isa = sa->data;

				if (exchange->initiator) {
					/*
					 * Initiator is source, responder is
					 * destination.
					 */
					if (ipsec_set_network(ie->id_ci,
					    ie->id_cr, sa)) {
						log_print(
						    "ipsec_finalize_exchange: "
						    "ipsec_set_network "
						    "failed");
						return;
					}
				} else {
					/*
					 * Responder is source, initiator is
					 * destination.
					 */
					if (ipsec_set_network(ie->id_cr,
					    ie->id_ci, sa)) {
						log_print(
						    "ipsec_finalize_exchange: "
						    "ipsec_set_network "
						    "failed");
						return;
					}
				}

				if (ipsec_sa_tag(exchange, sa, isakmp_sa) == -1)
					return;

				if (ipsec_sa_iface(exchange, sa, isakmp_sa) == -1)
					return;

				for (proto = TAILQ_FIRST(&sa->protos),
				    last_proto = 0; proto;
				    proto = TAILQ_NEXT(proto, link)) {
					if (pf_key_v2_set_spi(sa, proto,
					    0, isakmp_sa) ||
					    (last_proto &&
					    pf_key_v2_group_spis(sa,
						last_proto, proto, 0)) ||
					    pf_key_v2_set_spi(sa, proto,
						1, isakmp_sa) ||
					    (last_proto &&
						pf_key_v2_group_spis(sa,
						last_proto, proto, 1)))
						/*
						 * XXX Tear down this
						 * exchange.
						 */
						return;
					last_proto = proto;
				}

				if (sockaddr2text(isa->src_net, &addr1, 0))
					addr1 = 0;
				if (sockaddr2text(isa->src_mask, &mask1, 0))
					mask1 = 0;
				if (sockaddr2text(isa->dst_net, &addr2, 0))
					addr2 = 0;
				if (sockaddr2text(isa->dst_mask, &mask2, 0))
					mask2 = 0;

				LOG_DBG((LOG_EXCHANGE, 50,
				    "ipsec_finalize_exchange: src %s %s "
				    "dst %s %s tproto %u sport %u dport %u",
				    addr1 ? addr1 : "<??\?>",
				    mask1 ?  mask1 : "<??\?>",
				    addr2 ? addr2 : "<??\?>",
				    mask2 ? mask2 : "<??\?>",
				    isa->tproto, ntohs(isa->sport),
				    ntohs(isa->dport)));

				free(addr1);
				free(mask1);
				free(addr2);
				free(mask2);

				/*
				 * If this is not an SA acquired by the
				 * kernel, it needs to have a SPD entry
				 * (a.k.a. flow) set up.
				 */
				if (!(sa->flags & SA_FLAG_ONDEMAND ||
				    sa->flags & SA_FLAG_IFACE ||
				    conf_get_str("General", "Acquire-Only") ||
				    acquire_only) &&
				    pf_key_v2_enable_sa(sa, isakmp_sa))
					/* XXX Tear down this exchange.  */
					return;

				/*
				 * Mark elder SAs with the same flow
				 * information as replaced.
				 */
				while ((old_sa = sa_find(ipsec_sa_check_flow,
				    sa)) != 0)
					sa_mark_replaced(old_sa);
			}
			break;
		}
	}
}

/* Set the client addresses in ISA from SRC_ID and DST_ID.  */
static int
ipsec_set_network(u_int8_t *src_id, u_int8_t *dst_id, struct sa *sa)
{
	void               *src_net, *dst_net;
	void               *src_mask = NULL, *dst_mask = NULL;
	struct sockaddr    *addr;
	struct proto       *proto;
	struct ipsec_proto *iproto;
	struct ipsec_sa    *isa = sa->data;
	int                 src_af, dst_af;
	int                 id;
	char               *name, *nat = NULL;
	u_int8_t           *nat_id = NULL;
	size_t              nat_sz;

	if ((name = connection_passive_lookup_by_ids(src_id, dst_id)))
		nat = conf_get_str(name, "NAT-ID");

	if (nat) {
		if ((nat_id = ipsec_build_id(nat, &nat_sz))) {
			LOG_DBG((LOG_EXCHANGE, 50, "ipsec_set_network: SRC-NAT:"
			    " src: %s -> %s", name, nat));
			src_id = nat_id;
		} else
			log_print("ipsec_set_network: ipsec_build_id"
			    " failed for NAT-ID: %s", nat);
	}

	if (((proto = TAILQ_FIRST(&sa->protos)) != NULL) &&
	    ((iproto = proto->data) != NULL) && 
	    (iproto->encap_mode == IPSEC_ENCAP_UDP_ENCAP_TRANSPORT ||
	    iproto->encap_mode == IPSEC_ENCAP_UDP_ENCAP_TRANSPORT_DRAFT)) {
		/*
		 * For NAT-T with transport mode, we need to use the ISAKMP's
		 * SA addresses for the flow.
		 */
		sa->transport->vtbl->get_src(sa->transport, &addr);
		src_af = addr->sa_family;
		src_net = sockaddr_addrdata(addr);

		sa->transport->vtbl->get_dst(sa->transport, &addr);
		dst_af = addr->sa_family;
		dst_net = sockaddr_addrdata(addr);
	} else {
		id = GET_ISAKMP_ID_TYPE(src_id);
		src_net = src_id + ISAKMP_ID_DATA_OFF;
		switch (id) {
		case IPSEC_ID_IPV4_ADDR_SUBNET:
			src_mask = (u_int8_t *)src_net + sizeof(struct in_addr);
			/* FALLTHROUGH */
		case IPSEC_ID_IPV4_ADDR:
			src_af = AF_INET;
			break;

		case IPSEC_ID_IPV6_ADDR_SUBNET:
			src_mask = (u_int8_t *)src_net +
			    sizeof(struct in6_addr);
			/* FALLTHROUGH */
		case IPSEC_ID_IPV6_ADDR:
			src_af = AF_INET6;
			break;

		default:
			log_print(
			    "ipsec_set_network: ID type %d (%s) not supported",
			    id, constant_name(ipsec_id_cst, id));
			return -1;
		}

		id = GET_ISAKMP_ID_TYPE(dst_id);
		dst_net = dst_id + ISAKMP_ID_DATA_OFF;
		switch (id) {
		case IPSEC_ID_IPV4_ADDR_SUBNET:
			dst_mask = (u_int8_t *)dst_net + sizeof(struct in_addr);
			/* FALLTHROUGH */
		case IPSEC_ID_IPV4_ADDR:
			dst_af = AF_INET;
			break;

		case IPSEC_ID_IPV6_ADDR_SUBNET:
			dst_mask = (u_int8_t *)dst_net +
			    sizeof(struct in6_addr);
			/* FALLTHROUGH */
		case IPSEC_ID_IPV6_ADDR:
			dst_af = AF_INET6;
			break;

		default:
			log_print(
			    "ipsec_set_network: ID type %d (%s) not supported",
			    id, constant_name(ipsec_id_cst, id));
			return -1;
		}
	}

	/* Set source address/mask.  */
	switch (src_af) {
	case AF_INET:
		isa->src_net = calloc(1, sizeof(struct sockaddr_in));
		if (!isa->src_net)
			goto memfail;
		isa->src_net->sa_family = AF_INET;
		isa->src_net->sa_len = sizeof(struct sockaddr_in);

		isa->src_mask = calloc(1, sizeof(struct sockaddr_in));
		if (!isa->src_mask)
			goto memfail;
		isa->src_mask->sa_family = AF_INET;
		isa->src_mask->sa_len = sizeof(struct sockaddr_in);
		break;

	case AF_INET6:
		isa->src_net = calloc(1, sizeof(struct sockaddr_in6));
		if (!isa->src_net)
			goto memfail;
		isa->src_net->sa_family = AF_INET6;
		isa->src_net->sa_len = sizeof(struct sockaddr_in6);

		isa->src_mask = calloc(1, sizeof(struct sockaddr_in6));
		if (!isa->src_mask)
			goto memfail;
		isa->src_mask->sa_family = AF_INET6;
		isa->src_mask->sa_len = sizeof(struct sockaddr_in6);
		break;
	}

	/* Net */
	memcpy(sockaddr_addrdata(isa->src_net), src_net,
	    sockaddr_addrlen(isa->src_net));

	/* Mask */
	if (src_mask == NULL)
		memset(sockaddr_addrdata(isa->src_mask), 0xff,
		    sockaddr_addrlen(isa->src_mask));
	else
		memcpy(sockaddr_addrdata(isa->src_mask), src_mask,
		    sockaddr_addrlen(isa->src_mask));

	memcpy(&isa->sport,
	    src_id + ISAKMP_ID_DOI_DATA_OFF + IPSEC_ID_PORT_OFF,
	    IPSEC_ID_PORT_LEN);

	free(nat_id);

	/* Set destination address.  */
	switch (dst_af) {
	case AF_INET:
		isa->dst_net = calloc(1, sizeof(struct sockaddr_in));
		if (!isa->dst_net)
			goto memfail;
		isa->dst_net->sa_family = AF_INET;
		isa->dst_net->sa_len = sizeof(struct sockaddr_in);

		isa->dst_mask = calloc(1, sizeof(struct sockaddr_in));
		if (!isa->dst_mask)
			goto memfail;
		isa->dst_mask->sa_family = AF_INET;
		isa->dst_mask->sa_len = sizeof(struct sockaddr_in);
		break;

	case AF_INET6:
		isa->dst_net = calloc(1, sizeof(struct sockaddr_in6));
		if (!isa->dst_net)
			goto memfail;
		isa->dst_net->sa_family = AF_INET6;
		isa->dst_net->sa_len = sizeof(struct sockaddr_in6);

		isa->dst_mask = calloc(1, sizeof(struct sockaddr_in6));
		if (!isa->dst_mask)
			goto memfail;
		isa->dst_mask->sa_family = AF_INET6;
		isa->dst_mask->sa_len = sizeof(struct sockaddr_in6);
		break;
	}

	/* Net */
	memcpy(sockaddr_addrdata(isa->dst_net), dst_net,
	    sockaddr_addrlen(isa->dst_net));

	/* Mask */
	if (dst_mask == NULL)
		memset(sockaddr_addrdata(isa->dst_mask), 0xff,
		    sockaddr_addrlen(isa->dst_mask));
	else
		memcpy(sockaddr_addrdata(isa->dst_mask), dst_mask,
		    sockaddr_addrlen(isa->dst_mask));

	memcpy(&isa->tproto, dst_id + ISAKMP_ID_DOI_DATA_OFF +
	    IPSEC_ID_PROTO_OFF, IPSEC_ID_PROTO_LEN);
	memcpy(&isa->dport,
	    dst_id + ISAKMP_ID_DOI_DATA_OFF + IPSEC_ID_PORT_OFF,
	    IPSEC_ID_PORT_LEN);
	return 0;

memfail:
	log_error("ipsec_set_network: calloc () failed");
	return -1;
}

/* Free the DOI-specific exchange data pointed to by VIE.  */
static void
ipsec_free_exchange_data(void *vie)
{
	struct ipsec_exch *ie = vie;
	struct isakmp_cfg_attr *attr;

	free(ie->sa_i_b);
	free(ie->id_ci);
	free(ie->id_cr);
	free(ie->g_xi);
	free(ie->g_xr);
	free(ie->g_xy);
	free(ie->skeyid);
	free(ie->skeyid_d);
	free(ie->skeyid_a);
	free(ie->skeyid_e);
	free(ie->hash_i);
	free(ie->hash_r);
	if (ie->group)
		group_free(ie->group);
	for (attr = LIST_FIRST(&ie->attrs); attr;
	    attr = LIST_FIRST(&ie->attrs)) {
		LIST_REMOVE(attr, link);
		if (attr->length)
			free(attr->value);
		free(attr);
	}
}

/* Free the DOI-specific SA data pointed to by VISA.  */
static void
ipsec_free_sa_data(void *visa)
{
	struct ipsec_sa *isa = visa;

	free(isa->src_net);
	free(isa->src_mask);
	free(isa->dst_net);
	free(isa->dst_mask);
	free(isa->skeyid_a);
	free(isa->skeyid_d);
}

/* Free the DOI-specific protocol data of an SA pointed to by VIPROTO.  */
static void
ipsec_free_proto_data(void *viproto)
{
	struct ipsec_proto *iproto = viproto;
	int             i;

	for (i = 0; i < 2; i++)
		free(iproto->keymat[i]);
}

/* Return exchange script based on TYPE.  */
static int16_t *
ipsec_exchange_script(u_int8_t type)
{
	switch (type) {
	case ISAKMP_EXCH_TRANSACTION:
		return script_transaction;
	case IKE_EXCH_QUICK_MODE:
		return script_quick_mode;
	case IKE_EXCH_NEW_GROUP_MODE:
		return script_new_group_mode;
	}
	return 0;
}

/* Initialize this DOI, requires doi_init to already have been called.  */
void
ipsec_init(void)
{
	doi_register(&ipsec_doi);
}

/* Given a message MSG, return a suitable IV (or rather keystate).  */
static struct keystate *
ipsec_get_keystate(struct message *msg)
{
	struct keystate *ks;
	struct hash    *hash;

	/* If we have already have an IV, use it.  */
	if (msg->exchange && msg->exchange->keystate) {
		ks = malloc(sizeof *ks);
		if (!ks) {
			log_error("ipsec_get_keystate: malloc (%lu) failed",
			    (unsigned long) sizeof *ks);
			return 0;
		}
		memcpy(ks, msg->exchange->keystate, sizeof *ks);
		return ks;
	}
	/*
	 * For phase 2 when no SA yet is setup we need to hash the IV used by
	 * the ISAKMP SA concatenated with the message ID, and use that as an
	 * IV for further cryptographic operations.
	 */
	if (!msg->isakmp_sa->keystate) {
		log_print("ipsec_get_keystate: no keystate in ISAKMP SA %p",
		    msg->isakmp_sa);
		return 0;
	}
	ks = crypto_clone_keystate(msg->isakmp_sa->keystate);
	if (!ks)
		return 0;

	hash = hash_get(((struct ipsec_sa *)msg->isakmp_sa->data)->hash);
	hash->Init(hash->ctx);
	LOG_DBG_BUF((LOG_CRYPTO, 80, "ipsec_get_keystate: final phase 1 IV",
	    ks->riv, ks->xf->blocksize));
	hash->Update(hash->ctx, ks->riv, ks->xf->blocksize);
	LOG_DBG_BUF((LOG_CRYPTO, 80, "ipsec_get_keystate: message ID",
	    ((u_int8_t *) msg->iov[0].iov_base) + ISAKMP_HDR_MESSAGE_ID_OFF,
	    ISAKMP_HDR_MESSAGE_ID_LEN));
	hash->Update(hash->ctx, ((u_int8_t *) msg->iov[0].iov_base) +
	    ISAKMP_HDR_MESSAGE_ID_OFF, ISAKMP_HDR_MESSAGE_ID_LEN);
	hash->Final(hash->digest, hash->ctx);
	crypto_init_iv(ks, hash->digest, ks->xf->blocksize);
	LOG_DBG_BUF((LOG_CRYPTO, 80, "ipsec_get_keystate: phase 2 IV",
	    hash->digest, ks->xf->blocksize));
	return ks;
}

static void
ipsec_setup_situation(u_int8_t *buf)
{
	SET_IPSEC_SIT_SIT(buf + ISAKMP_SA_SIT_OFF, IPSEC_SIT_IDENTITY_ONLY);
}

static size_t
ipsec_situation_size(void)
{
	return IPSEC_SIT_SIT_LEN;
}

static u_int8_t
ipsec_spi_size(u_int8_t proto)
{
	return IPSEC_SPI_SIZE;
}

static int
ipsec_validate_attribute(u_int16_t type, u_int8_t * value, u_int16_t len,
    void *vmsg)
{
	struct message *msg = vmsg;

	if (msg->exchange->phase == 1 &&
	    (type < IKE_ATTR_ENCRYPTION_ALGORITHM || type > IKE_ATTR_GROUP_ORDER))
		return -1;
	if (msg->exchange->phase == 2 &&
	    (type < IPSEC_ATTR_SA_LIFE_TYPE || type > IPSEC_ATTR_ECN_TUNNEL))
		return -1;
	return 0;
}

static int
ipsec_validate_exchange(u_int8_t exch)
{
	return exch != IKE_EXCH_QUICK_MODE && exch != IKE_EXCH_NEW_GROUP_MODE;
}

static int
ipsec_validate_id_information(u_int8_t type, u_int8_t *extra, u_int8_t *buf,
    size_t sz, struct exchange *exchange)
{
	u_int8_t        proto = GET_IPSEC_ID_PROTO(extra);
	u_int16_t       port = GET_IPSEC_ID_PORT(extra);

	LOG_DBG((LOG_MESSAGE, 40,
	    "ipsec_validate_id_information: proto %d port %d type %d",
	    proto, port, type));
	if (type < IPSEC_ID_IPV4_ADDR || type > IPSEC_ID_KEY_ID)
		return -1;

	switch (type) {
	case IPSEC_ID_IPV4_ADDR:
		LOG_DBG_BUF((LOG_MESSAGE, 40,
		    "ipsec_validate_id_information: IPv4", buf,
		    sizeof(struct in_addr)));
		break;

	case IPSEC_ID_IPV6_ADDR:
		LOG_DBG_BUF((LOG_MESSAGE, 40,
		    "ipsec_validate_id_information: IPv6", buf,
		    sizeof(struct in6_addr)));
		break;

	case IPSEC_ID_IPV4_ADDR_SUBNET:
		LOG_DBG_BUF((LOG_MESSAGE, 40,
		    "ipsec_validate_id_information: IPv4 network/netmask",
		    buf, 2 * sizeof(struct in_addr)));
		break;

	case IPSEC_ID_IPV6_ADDR_SUBNET:
		LOG_DBG_BUF((LOG_MESSAGE, 40,
		    "ipsec_validate_id_information: IPv6 network/netmask",
		    buf, 2 * sizeof(struct in6_addr)));
		break;

	default:
		break;
	}

	if (exchange->phase == 1 &&
	    (proto != IPPROTO_UDP || port != UDP_DEFAULT_PORT) &&
	    (proto != 0 || port != 0)) {
		/*
		 * XXX SSH's ISAKMP tester fails this test (proto 17 - port
		 * 0).
		 */
#ifdef notyet
		return -1;
#else
		log_print("ipsec_validate_id_information: dubious ID "
		    "information accepted");
#endif
	}
	/* XXX More checks?  */

	return 0;
}

static int
ipsec_validate_key_information(u_int8_t *buf, size_t sz)
{
	/* XXX Not implemented yet.  */
	return 0;
}

static int
ipsec_validate_notification(u_int16_t type)
{
	return type < IPSEC_NOTIFY_RESPONDER_LIFETIME ||
	    type > IPSEC_NOTIFY_INITIAL_CONTACT ? -1 : 0;
}

static int
ipsec_validate_proto(u_int8_t proto)
{
	return proto < IPSEC_PROTO_IPSEC_AH ||
	    proto > IPSEC_PROTO_IPCOMP ? -1 : 0;
}

static int
ipsec_validate_situation(u_int8_t *buf, size_t *sz, size_t len)
{
	if (len < IPSEC_SIT_SIT_OFF + IPSEC_SIT_SIT_LEN) {
		log_print("ipsec_validate_situation: payload too short: %u",
		    (unsigned int) len);
		return -1;
	}
	/* Currently only "identity only" situations are supported.  */
	if (GET_IPSEC_SIT_SIT(buf) != IPSEC_SIT_IDENTITY_ONLY)
		return 1;

	*sz = IPSEC_SIT_SIT_LEN;

	return 0;
}

static int
ipsec_validate_transform_id(u_int8_t proto, u_int8_t transform_id)
{
	switch (proto) {
	/*
	 * As no unexpected protocols can occur, we just tie the
	 * default case to the first case, in order to silence a GCC
	 * warning.
	 */
	default:
	case ISAKMP_PROTO_ISAKMP:
		return transform_id != IPSEC_TRANSFORM_KEY_IKE;
	case IPSEC_PROTO_IPSEC_AH:
		return transform_id < IPSEC_AH_MD5 ||
		    transform_id > IPSEC_AH_RIPEMD ? -1 : 0;
	case IPSEC_PROTO_IPSEC_ESP:
		return transform_id < IPSEC_ESP_DES_IV64 ||
		    (transform_id > IPSEC_ESP_AES_GMAC &&
		    transform_id < IPSEC_ESP_AES_MARS) ||
		    transform_id > IPSEC_ESP_AES_TWOFISH ? -1 : 0;
	case IPSEC_PROTO_IPCOMP:
		return transform_id < IPSEC_IPCOMP_OUI ||
		    transform_id > IPSEC_IPCOMP_DEFLATE ? -1 : 0;
	}
}

static int
ipsec_initiator(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	int             (**script)(struct message *) = 0;

	/* Check that the SA is coherent with the IKE rules.  */
	if (exchange->type != ISAKMP_EXCH_TRANSACTION &&
	    ((exchange->phase == 1 && exchange->type != ISAKMP_EXCH_ID_PROT &&
	    exchange->type != ISAKMP_EXCH_AGGRESSIVE &&
	    exchange->type != ISAKMP_EXCH_INFO) ||
	    (exchange->phase == 2 && exchange->type != IKE_EXCH_QUICK_MODE &&
	    exchange->type != ISAKMP_EXCH_INFO))) {
		log_print("ipsec_initiator: unsupported exchange type %d "
		    "in phase %d", exchange->type, exchange->phase);
		return -1;
	}
	switch (exchange->type) {
	case ISAKMP_EXCH_ID_PROT:
		script = ike_main_mode_initiator;
		break;
	case ISAKMP_EXCH_AGGRESSIVE:
		script = ike_aggressive_initiator;
		break;
	case ISAKMP_EXCH_TRANSACTION:
		script = isakmp_cfg_initiator;
		break;
	case ISAKMP_EXCH_INFO:
		return message_send_info(msg);
	case IKE_EXCH_QUICK_MODE:
		script = ike_quick_mode_initiator;
		break;
	default:
		log_print("ipsec_initiator: unsupported exchange type %d",
			  exchange->type);
		return -1;
	}

	/* Run the script code for this step.  */
	if (script)
		return script[exchange->step] (msg);

	return 0;
}

/*
 * delete all SA's from addr with the associated proto and SPI's
 *
 * spis[] is an array of SPIs of size 16-octet for proto ISAKMP
 * or 4-octet otherwise.
 */
static void
ipsec_delete_spi_list(struct sockaddr *addr, u_int8_t proto, u_int8_t *spis,
    int nspis, char *type)
{
	struct sa	*sa;
	char		*peer;
	char		 ids[1024];
	int		 i;

	for (i = 0; i < nspis; i++) {
		if (proto == ISAKMP_PROTO_ISAKMP) {
			u_int8_t *spi = spis + i * ISAKMP_HDR_COOKIES_LEN;

			/*
			 * This really shouldn't happen in IPSEC DOI
			 * code, but Cisco VPN 3000 sends ISAKMP DELETE's
			 * this way.
			 */
			sa = sa_lookup_isakmp_sa(addr, spi);
		} else {
			u_int32_t spi = ((u_int32_t *)spis)[i];

			sa = ipsec_sa_lookup(addr, spi, proto);
		}

		if (sa == NULL) {
			LOG_DBG((LOG_SA, 30, "ipsec_delete_spi_list: could "
			    "not locate SA (SPI %08x, proto %u)",
			    ((u_int32_t *)spis)[i], proto));
			continue;
		}

		strlcpy(ids,
		    sa->doi->decode_ids("initiator id: %s, responder id: %s",
		    sa->id_i, sa->id_i_len, sa->id_r, sa->id_r_len, 0),
		    sizeof ids);
		if (sockaddr2text(addr, &peer, 0))
			peer = NULL;

		/* only log deletion of SAs which are not soft expired yet */
		if (sa->soft_death != NULL)
			log_verbose("isakmpd: Peer %s made us delete live SA "
			    "%s for proto %d, %s", peer ? peer : "<unknown>",
			    sa->name ? sa->name : "<unnamed>", proto, ids);

		LOG_DBG((LOG_SA, 30, "ipsec_delete_spi_list: "
		    "%s made us delete SA %p (%d references) for proto %d (%s)",
		    type, sa, sa->refcnt, proto, ids));
		free(peer);

		/* Delete the SA and search for the next */
		sa_free(sa);
	}
}

static int
ipsec_responder(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	int             (**script)(struct message *) = 0;
	struct payload *p;
	u_int16_t       type;

	/* Check that a new exchange is coherent with the IKE rules.  */
	if (exchange->step == 0 && exchange->type != ISAKMP_EXCH_TRANSACTION &&
	    ((exchange->phase == 1 && exchange->type != ISAKMP_EXCH_ID_PROT &&
	    exchange->type != ISAKMP_EXCH_AGGRESSIVE &&
	    exchange->type != ISAKMP_EXCH_INFO) ||
	    (exchange->phase == 2 && exchange->type != IKE_EXCH_QUICK_MODE &&
	    exchange->type != ISAKMP_EXCH_INFO))) {
		message_drop(msg, ISAKMP_NOTIFY_UNSUPPORTED_EXCHANGE_TYPE,
		    0, 1, 0);
		return -1;
	}
	LOG_DBG((LOG_MISC, 30, "ipsec_responder: phase %d exchange %d step %d",
	    exchange->phase, exchange->type, exchange->step));
	switch (exchange->type) {
	case ISAKMP_EXCH_ID_PROT:
		script = ike_main_mode_responder;
		break;
	case ISAKMP_EXCH_AGGRESSIVE:
		script = ike_aggressive_responder;
		break;
	case ISAKMP_EXCH_TRANSACTION:
		script = isakmp_cfg_responder;
		break;
	case ISAKMP_EXCH_INFO:
		TAILQ_FOREACH(p, &msg->payload[ISAKMP_PAYLOAD_NOTIFY], link) {
			type = GET_ISAKMP_NOTIFY_MSG_TYPE(p->p);
			LOG_DBG((LOG_EXCHANGE, 10,
			    "ipsec_responder: got NOTIFY of type %s",
			    constant_name(isakmp_notify_cst, type)));

			switch (type) {
			case IPSEC_NOTIFY_INITIAL_CONTACT:
				/* Handled by leftover logic. */
				break;

			case ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE:
			case ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE_ACK:
				dpd_handle_notify(msg, p);
				break;

			default:
				p->flags |= PL_MARK;
				break;
			}
		}

		/*
		 * If any DELETEs are in here, let the logic of leftover
		 * payloads deal with them.
		 */
		return 0;

	case IKE_EXCH_QUICK_MODE:
		script = ike_quick_mode_responder;
		break;

	default:
		message_drop(msg, ISAKMP_NOTIFY_UNSUPPORTED_EXCHANGE_TYPE,
		    0, 1, 0);
		return -1;
	}

	/* Run the script code for this step.  */
	if (script)
		return script[exchange->step] (msg);

	/*
	 * XXX So far we don't accept any proposals for exchanges we don't
	 * support.
	 */
	if (payload_first(msg, ISAKMP_PAYLOAD_SA)) {
		message_drop(msg, ISAKMP_NOTIFY_NO_PROPOSAL_CHOSEN, 0, 1, 0);
		return -1;
	}
	return 0;
}

static enum hashes
from_ike_hash(u_int16_t hash)
{
	switch (hash) {
		case IKE_HASH_MD5:
		return HASH_MD5;
	case IKE_HASH_SHA:
		return HASH_SHA1;
	case IKE_HASH_SHA2_256:
		return HASH_SHA2_256;
	case IKE_HASH_SHA2_384:
		return HASH_SHA2_384;
	case IKE_HASH_SHA2_512:
		return HASH_SHA2_512;
	}
	return -1;
}

static enum transform
from_ike_crypto(u_int16_t crypto)
{
	/* Coincidentally this is the null operation :-)  */
	return crypto;
}

/*
 * Find out whether the attribute of type TYPE with a LEN length value
 * pointed to by VALUE is incompatible with what we can handle.
 * VMSG is a pointer to the current message.
 */
int
ipsec_is_attribute_incompatible(u_int16_t type, u_int8_t *value, u_int16_t len,
    void *vmsg)
{
	struct message *msg = vmsg;
	u_int16_t dv = decode_16(value);

	if (msg->exchange->phase == 1) {
		switch (type) {
		case IKE_ATTR_ENCRYPTION_ALGORITHM:
			return !crypto_get(from_ike_crypto(dv));
		case IKE_ATTR_HASH_ALGORITHM:
			return !hash_get(from_ike_hash(dv));
		case IKE_ATTR_AUTHENTICATION_METHOD:
			return !ike_auth_get(dv);
		case IKE_ATTR_GROUP_DESCRIPTION:
			return (dv < IKE_GROUP_DESC_MODP_768 ||
			    dv > IKE_GROUP_DESC_MODP_1536) &&
			    (dv < IKE_GROUP_DESC_MODP_2048 ||
			    dv > IKE_GROUP_DESC_ECP_521) &&
			    (dv < IKE_GROUP_DESC_ECP_224 ||
			    dv > IKE_GROUP_DESC_BP_512);
		case IKE_ATTR_GROUP_TYPE:
			return 1;
		case IKE_ATTR_GROUP_PRIME:
			return 1;
		case IKE_ATTR_GROUP_GENERATOR_1:
			return 1;
		case IKE_ATTR_GROUP_GENERATOR_2:
			return 1;
		case IKE_ATTR_GROUP_CURVE_A:
			return 1;
		case IKE_ATTR_GROUP_CURVE_B:
			return 1;
		case IKE_ATTR_LIFE_TYPE:
			return dv < IKE_DURATION_SECONDS ||
			    dv > IKE_DURATION_KILOBYTES;
		case IKE_ATTR_LIFE_DURATION:
			return len != 2 && len != 4;
		case IKE_ATTR_PRF:
			return 1;
		case IKE_ATTR_KEY_LENGTH:
			/*
			 * Our crypto routines only allows key-lengths which
			 * are multiples of an octet.
			 */
			return dv % 8 != 0;
		case IKE_ATTR_FIELD_SIZE:
			return 1;
		case IKE_ATTR_GROUP_ORDER:
			return 1;
		}
	} else {
		switch (type) {
		case IPSEC_ATTR_SA_LIFE_TYPE:
			return dv < IPSEC_DURATION_SECONDS ||
			    dv > IPSEC_DURATION_KILOBYTES;
		case IPSEC_ATTR_SA_LIFE_DURATION:
			return len != 2 && len != 4;
		case IPSEC_ATTR_GROUP_DESCRIPTION:
			return (dv < IKE_GROUP_DESC_MODP_768 ||
			    dv > IKE_GROUP_DESC_MODP_1536) &&
			    (dv < IKE_GROUP_DESC_MODP_2048 ||
			    dv > IKE_GROUP_DESC_ECP_521) &&
			    (dv < IKE_GROUP_DESC_ECP_224 ||
			    dv > IKE_GROUP_DESC_BP_512);
		case IPSEC_ATTR_ENCAPSULATION_MODE:
			return dv != IPSEC_ENCAP_TUNNEL &&
			    dv != IPSEC_ENCAP_TRANSPORT &&
			    dv != IPSEC_ENCAP_UDP_ENCAP_TUNNEL &&
			    dv != IPSEC_ENCAP_UDP_ENCAP_TRANSPORT &&
			    dv != IPSEC_ENCAP_UDP_ENCAP_TUNNEL_DRAFT &&
			    dv != IPSEC_ENCAP_UDP_ENCAP_TRANSPORT_DRAFT;
		case IPSEC_ATTR_AUTHENTICATION_ALGORITHM:
			return dv < IPSEC_AUTH_HMAC_MD5 ||
			    dv > IPSEC_AUTH_HMAC_RIPEMD;
		case IPSEC_ATTR_KEY_LENGTH:
			/*
			 * XXX Blowfish needs '0'. Others appear to disregard
			 * this attr?
			 */
			return 0;
		case IPSEC_ATTR_KEY_ROUNDS:
			return 1;
		case IPSEC_ATTR_COMPRESS_DICTIONARY_SIZE:
			return 1;
		case IPSEC_ATTR_COMPRESS_PRIVATE_ALGORITHM:
			return 1;
		case IPSEC_ATTR_ECN_TUNNEL:
			return 1;
		}
	}
	/* XXX Silence gcc.  */
	return 1;
}

/*
 * Log the attribute of TYPE with a LEN length value pointed to by VALUE
 * in human-readable form.  VMSG is a pointer to the current message.
 */
int
ipsec_debug_attribute(u_int16_t type, u_int8_t *value, u_int16_t len,
    void *vmsg)
{
	struct message *msg = vmsg;
	char            val[20];

	/* XXX Transient solution.  */
	if (len == 2)
		snprintf(val, sizeof val, "%d", decode_16(value));
	else if (len == 4)
		snprintf(val, sizeof val, "%d", decode_32(value));
	else
		snprintf(val, sizeof val, "unrepresentable");

	LOG_DBG((LOG_MESSAGE, 50, "Attribute %s value %s",
	    constant_name(msg->exchange->phase == 1 ? ike_attr_cst :
	    ipsec_attr_cst, type), val));
	return 0;
}

/*
 * Decode the attribute of type TYPE with a LEN length value pointed to by
 * VALUE.  VIDA is a pointer to a context structure where we can find the
 * current message, SA and protocol.
 */
int
ipsec_decode_attribute(u_int16_t type, u_int8_t *value, u_int16_t len,
    void *vida)
{
	struct ipsec_decode_arg *ida = vida;
	struct message *msg = ida->msg;
	struct sa      *sa = ida->sa;
	struct ipsec_sa *isa = sa->data;
	struct proto   *proto = ida->proto;
	struct ipsec_proto *iproto = proto->data;
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	static int      lifetype = 0;

	if (exchange->phase == 1) {
		switch (type) {
		case IKE_ATTR_ENCRYPTION_ALGORITHM:
			/* XXX Errors possible?  */
			exchange->crypto = crypto_get(from_ike_crypto(
			    decode_16(value)));
			break;
		case IKE_ATTR_HASH_ALGORITHM:
			/* XXX Errors possible?  */
			ie->hash = hash_get(from_ike_hash(decode_16(value)));
			break;
		case IKE_ATTR_AUTHENTICATION_METHOD:
			/* XXX Errors possible?  */
			ie->ike_auth = ike_auth_get(decode_16(value));
			break;
		case IKE_ATTR_GROUP_DESCRIPTION:
			isa->group_desc = decode_16(value);
			break;
		case IKE_ATTR_GROUP_TYPE:
			break;
		case IKE_ATTR_GROUP_PRIME:
			break;
		case IKE_ATTR_GROUP_GENERATOR_1:
			break;
		case IKE_ATTR_GROUP_GENERATOR_2:
			break;
		case IKE_ATTR_GROUP_CURVE_A:
			break;
		case IKE_ATTR_GROUP_CURVE_B:
			break;
		case IKE_ATTR_LIFE_TYPE:
			lifetype = decode_16(value);
			return 0;
		case IKE_ATTR_LIFE_DURATION:
			switch (lifetype) {
			case IKE_DURATION_SECONDS:
				switch (len) {
				case 2:
					sa->seconds = decode_16(value);
					break;
				case 4:
					sa->seconds = decode_32(value);
					break;
				default:
					log_print("ipsec_decode_attribute: "
					    "unreasonable lifetime");
				}
				break;
			case IKE_DURATION_KILOBYTES:
				switch (len) {
				case 2:
					sa->kilobytes = decode_16(value);
					break;
				case 4:
					sa->kilobytes = decode_32(value);
					break;
				default:
					log_print("ipsec_decode_attribute: "
					    "unreasonable lifetime");
				}
				break;
			default:
				log_print("ipsec_decode_attribute: unknown "
				    "lifetime type");
			}
			break;
		case IKE_ATTR_PRF:
			break;
		case IKE_ATTR_KEY_LENGTH:
			exchange->key_length = decode_16(value) / 8;
			break;
		case IKE_ATTR_FIELD_SIZE:
			break;
		case IKE_ATTR_GROUP_ORDER:
			break;
		}
	} else {
		switch (type) {
		case IPSEC_ATTR_SA_LIFE_TYPE:
			lifetype = decode_16(value);
			return 0;
		case IPSEC_ATTR_SA_LIFE_DURATION:
			switch (lifetype) {
			case IPSEC_DURATION_SECONDS:
				switch (len) {
				case 2:
					sa->seconds = decode_16(value);
					break;
				case 4:
					sa->seconds = decode_32(value);
					break;
				default:
					log_print("ipsec_decode_attribute: "
					    "unreasonable lifetime");
				}
				break;
			case IPSEC_DURATION_KILOBYTES:
				switch (len) {
				case 2:
					sa->kilobytes = decode_16(value);
					break;
				case 4:
					sa->kilobytes = decode_32(value);
					break;
				default:
					log_print("ipsec_decode_attribute: "
					    "unreasonable lifetime");
				}
				break;
			default:
				log_print("ipsec_decode_attribute: unknown "
				    "lifetime type");
			}
			break;
		case IPSEC_ATTR_GROUP_DESCRIPTION:
			isa->group_desc = decode_16(value);
			break;
		case IPSEC_ATTR_ENCAPSULATION_MODE:
			/*
			 * XXX Multiple protocols must have same
			 * encapsulation mode, no?
			 */
			iproto->encap_mode = decode_16(value);
			break;
		case IPSEC_ATTR_AUTHENTICATION_ALGORITHM:
			iproto->auth = decode_16(value);
			break;
		case IPSEC_ATTR_KEY_LENGTH:
			iproto->keylen = decode_16(value);
			break;
		case IPSEC_ATTR_KEY_ROUNDS:
			iproto->keyrounds = decode_16(value);
			break;
		case IPSEC_ATTR_COMPRESS_DICTIONARY_SIZE:
			break;
		case IPSEC_ATTR_COMPRESS_PRIVATE_ALGORITHM:
			break;
		case IPSEC_ATTR_ECN_TUNNEL:
			break;
		}
	}
	lifetype = 0;
	return 0;
}

/*
 * Walk over the attributes of the transform payload found in BUF, and
 * fill out the fields of the SA attached to MSG.  Also mark the SA as
 * processed.
 */
void
ipsec_decode_transform(struct message *msg, struct sa *sa, struct proto *proto,
    u_int8_t *buf)
{
	struct ipsec_exch *ie = msg->exchange->data;
	struct ipsec_decode_arg ida;

	LOG_DBG((LOG_MISC, 20, "ipsec_decode_transform: transform %d chosen",
	    GET_ISAKMP_TRANSFORM_NO(buf)));

	ida.msg = msg;
	ida.sa = sa;
	ida.proto = proto;

	/* The default IKE lifetime is 8 hours.  */
	if (sa->phase == 1)
		sa->seconds = 28800;

	/* Extract the attributes and stuff them into the SA.  */
	attribute_map(buf + ISAKMP_TRANSFORM_SA_ATTRS_OFF,
	    GET_ISAKMP_GEN_LENGTH(buf) - ISAKMP_TRANSFORM_SA_ATTRS_OFF,
	    ipsec_decode_attribute, &ida);

	/*
	 * If no pseudo-random function was negotiated, it's HMAC.
	 * XXX As PRF_HMAC currently is zero, this is a no-op.
	 */
	if (!ie->prf_type)
		ie->prf_type = PRF_HMAC;
}

/*
 * Delete the IPsec SA represented by the INCOMING direction in protocol PROTO
 * of the IKE security association SA.
 */
static void
ipsec_delete_spi(struct sa *sa, struct proto *proto, int incoming)
{
	struct sa *new_sa;
	struct ipsec_proto *iproto;

	if (sa->phase == 1)
		return;

	iproto = proto->data;
	/*
	 * If the SA is using UDP encap and it replaced other SA,
	 * enable the other SA to keep the flow for the other SAs.
	 */
	if ((iproto->encap_mode == IPSEC_ENCAP_UDP_ENCAP_TRANSPORT ||
	    iproto->encap_mode == IPSEC_ENCAP_UDP_ENCAP_TRANSPORT_DRAFT) &&
	    (sa->flags & SA_FLAG_REPLACED) == 0 &&
	    (new_sa = sa_find(ipsec_sa_check_flow_any, sa)) != NULL &&
	    new_sa->flags & SA_FLAG_REPLACED)
		sa_replace(sa, new_sa);

	/*
	 * If the SA was not replaced and was not one acquired through the
	 * kernel (ACQUIRE message), remove the flow associated with it.
	 * We ignore any errors from the disabling of the flow.
	 */
	if (sa->flags & SA_FLAG_READY && !(sa->flags & SA_FLAG_ONDEMAND ||
	    sa->flags & SA_FLAG_REPLACED || sa->flags & SA_FLAG_IFACE ||
	    acquire_only ||
	    conf_get_str("General", "Acquire-Only")))
		pf_key_v2_disable_sa(sa, incoming);

	/* XXX Error handling?  Is it interesting?  */
	pf_key_v2_delete_spi(sa, proto, incoming);
}

/*
 * Store BUF into the g^x entry of the exchange that message MSG belongs to.
 * PEER is non-zero when the value is our peer's, and zero when it is ours.
 */
static int
ipsec_g_x(struct message *msg, int peer, u_int8_t *buf)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	u_int8_t      **g_x;
	int             initiator = exchange->initiator ^ peer;
	char            header[32];

	g_x = initiator ? &ie->g_xi : &ie->g_xr;
	*g_x = malloc(ie->g_x_len);
	if (!*g_x) {
		log_error("ipsec_g_x: malloc (%lu) failed",
		    (unsigned long)ie->g_x_len);
		return -1;
	}
	memcpy(*g_x, buf, ie->g_x_len);
	snprintf(header, sizeof header, "ipsec_g_x: g^x%c",
	    initiator ? 'i' : 'r');
	LOG_DBG_BUF((LOG_MISC, 80, header, *g_x, ie->g_x_len));
	return 0;
}

/* Generate our DH value.  */
int
ipsec_gen_g_x(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	u_int8_t       *buf;

	buf = malloc(ISAKMP_KE_SZ + ie->g_x_len);
	if (!buf) {
		log_error("ipsec_gen_g_x: malloc (%lu) failed",
		    ISAKMP_KE_SZ + (unsigned long)ie->g_x_len);
		return -1;
	}
	if (message_add_payload(msg, ISAKMP_PAYLOAD_KEY_EXCH, buf,
	    ISAKMP_KE_SZ + ie->g_x_len, 1)) {
		free(buf);
		return -1;
	}
	if (dh_create_exchange(ie->group, buf + ISAKMP_KE_DATA_OFF)) {
		log_print("ipsec_gen_g_x: dh_create_exchange failed");
		free(buf);
		return -1;
	}
	return ipsec_g_x(msg, 0, buf + ISAKMP_KE_DATA_OFF);
}

/* Save the peer's DH value.  */
int
ipsec_save_g_x(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct ipsec_exch *ie = exchange->data;
	struct payload *kep;

	kep = payload_first(msg, ISAKMP_PAYLOAD_KEY_EXCH);
	kep->flags |= PL_MARK;
	ie->g_x_len = GET_ISAKMP_GEN_LENGTH(kep->p) - ISAKMP_KE_DATA_OFF;

	/* Check that the given length matches the group's expectancy.  */
	if (ie->g_x_len != (size_t) dh_getlen(ie->group)) {
		/* XXX Is this a good notify type?  */
		message_drop(msg, ISAKMP_NOTIFY_PAYLOAD_MALFORMED, 0, 1, 0);
		return -1;
	}
	return ipsec_g_x(msg, 1, kep->p + ISAKMP_KE_DATA_OFF);
}

/*
 * Get a SPI for PROTO and the transport MSG passed over.  Store the
 * size where SZ points.  NB!  A zero return is OK if *SZ is zero.
 */
static u_int8_t *
ipsec_get_spi(size_t *sz, u_int8_t proto, struct message *msg)
{
	struct sockaddr *dst, *src;
	struct transport *transport = msg->transport;

	if (msg->exchange->phase == 1) {
		*sz = 0;
		return 0;
	} else {
		/* We are the destination in the SA we want a SPI for.  */
		transport->vtbl->get_src(transport, &dst);
		/* The peer is the source.  */
		transport->vtbl->get_dst(transport, &src);
		return pf_key_v2_get_spi(sz, proto, src, dst,
		    msg->exchange->seq);
	}
}

/*
 * We have gotten a payload PAYLOAD of type TYPE, which did not get handled
 * by the logic of the exchange MSG takes part in.  Now is the time to deal
 * with such a payload if we know how to, if we don't, return -1, otherwise
 * 0.
 */
int
ipsec_handle_leftover_payload(struct message *msg, u_int8_t type,
    struct payload *payload)
{
	u_int32_t       spisz, nspis;
	struct sockaddr *dst;
	int             reenter = 0;
	u_int8_t       *spis, proto;
	struct sa      *sa;

	switch (type) {
	case ISAKMP_PAYLOAD_DELETE:
		proto = GET_ISAKMP_DELETE_PROTO(payload->p);
		nspis = GET_ISAKMP_DELETE_NSPIS(payload->p);
		spisz = GET_ISAKMP_DELETE_SPI_SZ(payload->p);

		if (nspis == 0) {
			LOG_DBG((LOG_SA, 60, "ipsec_handle_leftover_payload: "
			    "message specified zero SPIs, ignoring"));
			return -1;
		}
		/* verify proper SPI size */
		if ((proto == ISAKMP_PROTO_ISAKMP &&
		    spisz != ISAKMP_HDR_COOKIES_LEN) ||
		    (proto != ISAKMP_PROTO_ISAKMP && spisz != sizeof(u_int32_t))) {
			log_print("ipsec_handle_leftover_payload: invalid SPI "
			    "size %d for proto %d in DELETE payload",
			    spisz, proto);
			return -1;
		}
		spis = calloc(nspis, spisz);
		if (!spis) {
			log_error("ipsec_handle_leftover_payload: malloc "
			    "(%d) failed", nspis * spisz);
			return -1;
		}
		/* extract SPI and get dst address */
		memcpy(spis, payload->p + ISAKMP_DELETE_SPI_OFF, nspis * spisz);
		msg->transport->vtbl->get_dst(msg->transport, &dst);

		ipsec_delete_spi_list(dst, proto, spis, nspis, "DELETE");

		free(spis);
		payload->flags |= PL_MARK;
		return 0;

	case ISAKMP_PAYLOAD_NOTIFY:
		switch (GET_ISAKMP_NOTIFY_MSG_TYPE(payload->p)) {
		case IPSEC_NOTIFY_INITIAL_CONTACT:
			/*
			 * Permit INITIAL-CONTACT if
			 *   - this is not an AGGRESSIVE mode exchange
			 *   - it is protected by an ISAKMP SA
			 *
			 * XXX Instead of the first condition above, we could
			 * XXX permit this only for phase 2. In the last
			 * XXX packet of main-mode, this payload, while
			 * XXX encrypted, is not part of the hash digest.  As
			 * XXX we currently send our own INITIAL-CONTACTs at
			 * XXX this point, this too would need to be changed.
			 */
			if (msg->exchange->type == ISAKMP_EXCH_AGGRESSIVE) {
				log_print("ipsec_handle_leftover_payload: got "
				    "INITIAL-CONTACT in AGGRESSIVE mode");
				return -1;
			}
			if ((msg->exchange->flags & EXCHANGE_FLAG_ENCRYPT)
			    == 0) {
				log_print("ipsec_handle_leftover_payload: got "
				    "INITIAL-CONTACT without ISAKMP SA");
				return -1;
			}

			if ((msg->flags & MSG_AUTHENTICATED) == 0) {
				log_print("ipsec_handle_leftover_payload: "
				    "got unauthenticated INITIAL-CONTACT");
				return -1;
			}
			/*
			 * Find out who is sending this and then delete every
			 * SA that is ready.  Exchanges will timeout
			 * themselves and then the non-ready SAs will
			 * disappear too.
			 */
			msg->transport->vtbl->get_dst(msg->transport, &dst);
			while ((sa = sa_lookup_by_peer(dst, SA_LEN(dst), 0)) != 0) {
				/*
				 * Don't delete the current SA -- we received
				 * the notification over it, so it's obviously
				 * still active. We temporarily need to remove
				 * the SA from the list to avoid an endless
				 * loop, but keep a reference so it won't
				 * disappear meanwhile.
				 */
				if (sa == msg->isakmp_sa) {
					sa_reference(sa);
					sa_remove(sa);
					reenter = 1;
					continue;
				}
				LOG_DBG((LOG_SA, 30,
				    "ipsec_handle_leftover_payload: "
				    "INITIAL-CONTACT made us delete SA %p",
				    sa));
				sa_delete(sa, 0);
			}

			if (reenter) {
				sa_enter(msg->isakmp_sa);
				sa_release(msg->isakmp_sa);
			}
			payload->flags |= PL_MARK;
			return 0;
		}
	}
	return -1;
}

/* Return the encryption keylength in octets of the ESP protocol PROTO.  */
int
ipsec_esp_enckeylength(struct proto *proto)
{
	struct ipsec_proto *iproto = proto->data;

	/* Compute the keylength to use.  */
	switch (proto->id) {
	case IPSEC_ESP_3DES:
		return 24;
	case IPSEC_ESP_CAST:
		if (!iproto->keylen)
			return 16;
		return iproto->keylen / 8;
	case IPSEC_ESP_AES_CTR:
	case IPSEC_ESP_AES_GCM_16:
	case IPSEC_ESP_AES_GMAC:
		if (!iproto->keylen)
			return 20;
		return iproto->keylen / 8 + 4;
	case IPSEC_ESP_AES:
		if (!iproto->keylen)
			return 16;
		/* FALLTHROUGH */
	default:
		return iproto->keylen / 8;
	}
}

/* Return the authentication keylength in octets of the ESP protocol PROTO.  */
int
ipsec_esp_authkeylength(struct proto *proto)
{
	struct ipsec_proto *iproto = proto->data;

	switch (iproto->auth) {
	case IPSEC_AUTH_HMAC_MD5:
		return 16;
	case IPSEC_AUTH_HMAC_SHA:
	case IPSEC_AUTH_HMAC_RIPEMD:
		return 20;
	case IPSEC_AUTH_HMAC_SHA2_256:
		return 32;
	case IPSEC_AUTH_HMAC_SHA2_384:
		return 48;
	case IPSEC_AUTH_HMAC_SHA2_512:
		return 64;
	default:
		return 0;
	}
}

/* Return the authentication keylength in octets of the AH protocol PROTO.  */
int
ipsec_ah_keylength(struct proto *proto)
{
	switch (proto->id) {
		case IPSEC_AH_MD5:
		return 16;
	case IPSEC_AH_SHA:
	case IPSEC_AH_RIPEMD:
		return 20;
	case IPSEC_AH_SHA2_256:
		return 32;
	case IPSEC_AH_SHA2_384:
		return 48;
	case IPSEC_AH_SHA2_512:
		return 64;
	default:
		return -1;
	}
}

/* Return the total keymaterial length of the protocol PROTO.  */
int
ipsec_keymat_length(struct proto *proto)
{
	switch (proto->proto) {
		case IPSEC_PROTO_IPSEC_ESP:
		return ipsec_esp_enckeylength(proto)
		    + ipsec_esp_authkeylength(proto);
	case IPSEC_PROTO_IPSEC_AH:
		return ipsec_ah_keylength(proto);
	default:
		return -1;
	}
}

/* Helper function for ipsec_get_id().  */
static int
ipsec_get_proto_port(char *section, u_int8_t *tproto, u_int16_t *port)
{
	struct protoent	*pe = NULL;
	struct servent	*se;
	char	*pstr;

	pstr = conf_get_str(section, "Protocol");
	if (!pstr) {
		*tproto = 0;
		return 0;
	}
	*tproto = (u_int8_t)atoi(pstr);
	if (!*tproto) {
		pe = getprotobyname(pstr);
		if (pe)
			*tproto = pe->p_proto;
	}
	if (!*tproto) {
		log_print("ipsec_get_proto_port: protocol \"%s\" unknown",
		    pstr);
		return -1;
	}

	pstr = conf_get_str(section, "Port");
	if (!pstr)
		return 0;
	*port = (u_int16_t)atoi(pstr);
	if (!*port) {
		se = getservbyname(pstr,
		    pe ? pe->p_name : (pstr ? pstr : NULL));
		if (se)
			*port = ntohs(se->s_port);
	}
	if (!*port) {
		log_print("ipsec_get_proto_port: port \"%s\" unknown",
		    pstr);
		return -1;
	}
	return 0;
}

/*
 * Out of a named section SECTION in the configuration file find out
 * the network address and mask as well as the ID type.  Put the info
 * in the areas pointed to by ADDR, MASK, TPROTO, PORT, and ID respectively.
 * Return 0 on success and -1 on failure.
 */
int
ipsec_get_id(char *section, int *id, struct sockaddr **addr,
    struct sockaddr **mask, u_int8_t *tproto, u_int16_t *port)
{
	char	*type, *address, *netmask;
	sa_family_t	af = 0;

	type = conf_get_str(section, "ID-type");
	if (!type) {
		log_print("ipsec_get_id: section %s has no \"ID-type\" tag",
		    section);
		return -1;
	}
	*id = constant_value(ipsec_id_cst, type);
	switch (*id) {
	case IPSEC_ID_IPV4_ADDR:
	case IPSEC_ID_IPV4_ADDR_SUBNET:
		af = AF_INET;
		break;
	case IPSEC_ID_IPV6_ADDR:
	case IPSEC_ID_IPV6_ADDR_SUBNET:
		af = AF_INET6;
		break;
	}
	switch (*id) {
	case IPSEC_ID_IPV4_ADDR:
	case IPSEC_ID_IPV6_ADDR: {
		int ret;

		address = conf_get_str(section, "Address");
		if (!address) {
			log_print("ipsec_get_id: section %s has no "
			    "\"Address\" tag", section);
			return -1;
		}
		if (text2sockaddr(address, NULL, addr, af, 0)) {
			log_print("ipsec_get_id: invalid address %s in "
			    "section %s", address, section);
			return -1;
		}
		ret = ipsec_get_proto_port(section, tproto, port);
		if (ret < 0)
			free(*addr);

		return ret;
	}

#ifdef notyet
	case IPSEC_ID_FQDN:
		return -1;

	case IPSEC_ID_USER_FQDN:
		return -1;
#endif

	case IPSEC_ID_IPV4_ADDR_SUBNET:
	case IPSEC_ID_IPV6_ADDR_SUBNET: {
		int ret;

		address = conf_get_str(section, "Network");
		if (!address) {
			log_print("ipsec_get_id: section %s has no "
			    "\"Network\" tag", section);
			return -1;
		}
		if (text2sockaddr(address, NULL, addr, af, 0)) {
			log_print("ipsec_get_id: invalid section %s "
			    "network %s", section, address);
			return -1;
		}
		netmask = conf_get_str(section, "Netmask");
		if (!netmask) {
			log_print("ipsec_get_id: section %s has no "
			    "\"Netmask\" tag", section);
			free(*addr);
			return -1;
		}
		if (text2sockaddr(netmask, NULL, mask, af, 1)) {
			log_print("ipsec_get_id: invalid section %s "
			    "network %s", section, netmask);
			free(*addr);
			return -1;
		}
		ret = ipsec_get_proto_port(section, tproto, port);
		if (ret < 0) {
			free(*mask);
			free(*addr);
		}
		return ret;
	}

#ifdef notyet
	case IPSEC_ID_IPV4_RANGE:
		return -1;

	case IPSEC_ID_IPV6_RANGE:
		return -1;

	case IPSEC_ID_DER_ASN1_DN:
		return -1;

	case IPSEC_ID_DER_ASN1_GN:
		return -1;

	case IPSEC_ID_KEY_ID:
		return -1;
#endif

	default:
		log_print("ipsec_get_id: unknown ID type \"%s\" in "
		    "section %s", type, section);
		return -1;
	}

	return 0;
}

/*
 * XXX I rather want this function to return a status code, and fail if
 * we cannot fit the information in the supplied buffer.
 */
static void
ipsec_decode_id(char *buf, size_t size, u_int8_t *id, size_t id_len,
    int isakmpform)
{
	int             id_type;
	char           *addr = 0, *mask = 0;

	if (id) {
		if (!isakmpform) {
			/*
			 * Exchanges and SAs dont carry the IDs in ISAKMP
			 * form.
			 */
			id -= ISAKMP_GEN_SZ;
			id_len += ISAKMP_GEN_SZ;
		}
		id_type = GET_ISAKMP_ID_TYPE(id);
		switch (id_type) {
		case IPSEC_ID_IPV4_ADDR:
			util_ntoa(&addr, AF_INET, id + ISAKMP_ID_DATA_OFF);
			snprintf(buf, size, "%s", addr);
			break;

		case IPSEC_ID_IPV4_ADDR_SUBNET:
			util_ntoa(&addr, AF_INET, id + ISAKMP_ID_DATA_OFF);
			util_ntoa(&mask, AF_INET, id + ISAKMP_ID_DATA_OFF + 4);
			snprintf(buf, size, "%s/%s", addr, mask);
			break;

		case IPSEC_ID_IPV6_ADDR:
			util_ntoa(&addr, AF_INET6, id + ISAKMP_ID_DATA_OFF);
			snprintf(buf, size, "%s", addr);
			break;

		case IPSEC_ID_IPV6_ADDR_SUBNET:
			util_ntoa(&addr, AF_INET6, id + ISAKMP_ID_DATA_OFF);
			util_ntoa(&mask, AF_INET6, id + ISAKMP_ID_DATA_OFF +
			    sizeof(struct in6_addr));
			snprintf(buf, size, "%s/%s", addr, mask);
			break;

		case IPSEC_ID_FQDN:
		case IPSEC_ID_USER_FQDN:
			/* String is not NUL terminated, be careful */
			id_len -= ISAKMP_ID_DATA_OFF;
			id_len = MINIMUM(id_len, size - 1);
			memcpy(buf, id + ISAKMP_ID_DATA_OFF, id_len);
			buf[id_len] = '\0';
			break;

		case IPSEC_ID_DER_ASN1_DN:
			addr = x509_DN_string(id + ISAKMP_ID_DATA_OFF,
			    id_len - ISAKMP_ID_DATA_OFF);
			if (!addr) {
				snprintf(buf, size, "unparsable ASN1 DN ID");
				return;
			}
			strlcpy(buf, addr, size);
			break;

		default:
			snprintf(buf, size, "<id type unknown: %x>", id_type);
			break;
		}
	} else
		snprintf(buf, size, "<no ipsec id>");
	free(addr);
	free(mask);
}

char *
ipsec_decode_ids(char *fmt, u_int8_t *id1, size_t id1_len, u_int8_t *id2,
    size_t id2_len, int isakmpform)
{
	static char     result[1024];
	char            s_id1[256], s_id2[256];

	ipsec_decode_id(s_id1, sizeof s_id1, id1, id1_len, isakmpform);
	ipsec_decode_id(s_id2, sizeof s_id2, id2, id2_len, isakmpform);

	snprintf(result, sizeof result, fmt, s_id1, s_id2);
	return result;
}

/*
 * Out of a named section SECTION in the configuration file build an
 * ISAKMP ID payload.  Ths payload size should be stashed in SZ.
 * The caller is responsible for freeing the payload.
 */
u_int8_t *
ipsec_build_id(char *section, size_t *sz)
{
	struct sockaddr *addr, *mask;
	u_int8_t       *p;
	int             id, subnet = 0;
	u_int8_t        tproto = 0;
	u_int16_t       port = 0;

	if (ipsec_get_id(section, &id, &addr, &mask, &tproto, &port))
		return 0;

	if (id == IPSEC_ID_IPV4_ADDR_SUBNET || id == IPSEC_ID_IPV6_ADDR_SUBNET)
		subnet = 1;

	*sz = ISAKMP_ID_SZ + sockaddr_addrlen(addr);
	if (subnet)
		*sz += sockaddr_addrlen(mask);

	p = malloc(*sz);
	if (!p) {
		log_print("ipsec_build_id: malloc(%lu) failed",
		    (unsigned long)*sz);
		if (subnet)
			free(mask);
		free(addr);
		return 0;
	}
	SET_ISAKMP_ID_TYPE(p, id);
	SET_ISAKMP_ID_DOI_DATA(p, (unsigned char *)"\000\000\000");

	memcpy(p + ISAKMP_ID_DATA_OFF, sockaddr_addrdata(addr),
	    sockaddr_addrlen(addr));
	if (subnet)
		memcpy(p + ISAKMP_ID_DATA_OFF + sockaddr_addrlen(addr),
		    sockaddr_addrdata(mask), sockaddr_addrlen(mask));

	SET_IPSEC_ID_PROTO(p + ISAKMP_ID_DOI_DATA_OFF, tproto);
	SET_IPSEC_ID_PORT(p + ISAKMP_ID_DOI_DATA_OFF, port);

	if (subnet)
		free(mask);
	free(addr);
	return p;
}

/*
 * copy an ISAKMPD id
 */
int
ipsec_clone_id(u_int8_t **did, size_t *did_len, u_int8_t *id, size_t id_len)
{
	free(*did);

	if (!id_len || !id) {
		*did = 0;
		*did_len = 0;
		return 0;
	}
	*did = malloc(id_len);
	if (!*did) {
		*did_len = 0;
		log_error("ipsec_clone_id: malloc(%lu) failed",
		    (unsigned long)id_len);
		return -1;
	}
	*did_len = id_len;
	memcpy(*did, id, id_len);

	return 0;
}

/*
 * IPsec-specific PROTO initializations.  SECTION is only set if we are the
 * initiator thus only usable there.
 * XXX I want to fix this later.
 */
void
ipsec_proto_init(struct proto *proto, char *section)
{
	struct ipsec_proto *iproto = proto->data;

	if (proto->sa->phase == 2)
		iproto->replay_window = section ? conf_get_num(section,
		    "ReplayWindow", DEFAULT_REPLAY_WINDOW) :
		    DEFAULT_REPLAY_WINDOW;
}

/*
 * Add a notification payload of type INITIAL CONTACT to MSG if this is
 * the first contact we have made to our peer.
 */
int
ipsec_initial_contact(struct message *msg)
{
	u_int8_t *buf;

	if (ipsec_contacted(msg))
		return 0;

	buf = malloc(ISAKMP_NOTIFY_SZ + ISAKMP_HDR_COOKIES_LEN);
	if (!buf) {
		log_error("ike_phase_1_initial_contact: malloc (%d) failed",
		    ISAKMP_NOTIFY_SZ + ISAKMP_HDR_COOKIES_LEN);
		return -1;
	}
	SET_ISAKMP_NOTIFY_DOI(buf, IPSEC_DOI_IPSEC);
	SET_ISAKMP_NOTIFY_PROTO(buf, ISAKMP_PROTO_ISAKMP);
	SET_ISAKMP_NOTIFY_SPI_SZ(buf, ISAKMP_HDR_COOKIES_LEN);
	SET_ISAKMP_NOTIFY_MSG_TYPE(buf, IPSEC_NOTIFY_INITIAL_CONTACT);
	memcpy(buf + ISAKMP_NOTIFY_SPI_OFF, msg->isakmp_sa->cookies,
	    ISAKMP_HDR_COOKIES_LEN);
	if (message_add_payload(msg, ISAKMP_PAYLOAD_NOTIFY, buf,
	    ISAKMP_NOTIFY_SZ + ISAKMP_HDR_COOKIES_LEN, 1)) {
		free(buf);
		return -1;
	}
	return ipsec_add_contact(msg);
}

/*
 * Compare the two contacts pointed to by A and B.  Return negative if
 * *A < *B, 0 if they are equal, and positive if *A is the largest of them.
 */
static int
addr_cmp(const void *a, const void *b)
{
	const struct contact *x = a, *y = b;
	int             minlen = MINIMUM(x->len, y->len);
	int             rv = memcmp(x->addr, y->addr, minlen);

	return rv ? rv : (x->len - y->len);
}

/*
 * Add the peer that MSG is bound to as an address we don't want to send
 * INITIAL CONTACT too from now on.  Do not call this function with a
 * specific address duplicate times. We want fast lookup, speed of insertion
 * is unimportant, if this is to scale.
 */
static int
ipsec_add_contact(struct message *msg)
{
	struct contact *new_contacts;
	struct sockaddr *dst, *addr;
	int             cnt;

	if (contact_cnt == contact_limit) {
		cnt = contact_limit ? 2 * contact_limit : 64;
		new_contacts = reallocarray(contacts, cnt, sizeof contacts[0]);
		if (!new_contacts) {
			log_error("ipsec_add_contact: "
			    "realloc (%p, %lu) failed", contacts,
			    cnt * (unsigned long) sizeof contacts[0]);
			return -1;
		}
		contact_limit = cnt;
		contacts = new_contacts;
	}
	msg->transport->vtbl->get_dst(msg->transport, &dst);
	addr = malloc(SA_LEN(dst));
	if (!addr) {
		log_error("ipsec_add_contact: malloc (%lu) failed",
		    (unsigned long)SA_LEN(dst));
		return -1;
	}
	memcpy(addr, dst, SA_LEN(dst));
	contacts[contact_cnt].addr = addr;
	contacts[contact_cnt++].len = SA_LEN(dst);

	/*
	 * XXX There are better algorithms for already mostly-sorted data like
	 * this, but only qsort is standard.  I will someday do this inline.
	 */
	qsort(contacts, contact_cnt, sizeof *contacts, addr_cmp);
	return 0;
}

/* Return true if the recipient of MSG has already been contacted.  */
static int
ipsec_contacted(struct message *msg)
{
	struct contact  contact;

	msg->transport->vtbl->get_dst(msg->transport, &contact.addr);
	contact.len = SA_LEN(contact.addr);
	return contacts ? (bsearch(&contact, contacts, contact_cnt,
	    sizeof *contacts, addr_cmp) != 0) : 0;
}

/* Add a HASH for to MSG.  */
u_int8_t *
ipsec_add_hash_payload(struct message *msg, size_t hashsize)
{
	u_int8_t *buf;

	buf = malloc(ISAKMP_HASH_SZ + hashsize);
	if (!buf) {
		log_error("ipsec_add_hash_payload: malloc (%lu) failed",
		    ISAKMP_HASH_SZ + (unsigned long) hashsize);
		return 0;
	}
	if (message_add_payload(msg, ISAKMP_PAYLOAD_HASH, buf,
	    ISAKMP_HASH_SZ + hashsize, 1)) {
		free(buf);
		return 0;
	}
	return buf;
}

/* Fill in the HASH payload of MSG.  */
int
ipsec_fill_in_hash(struct message *msg)
{
	struct exchange *exchange = msg->exchange;
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa = isakmp_sa->data;
	struct hash    *hash = hash_get(isa->hash);
	struct prf     *prf;
	struct payload *payload;
	u_int8_t       *buf;
	u_int32_t       i;
	char            header[80];

	/* If no SKEYID_a, we need not do anything.  */
	if (!isa->skeyid_a)
		return 0;

	payload = payload_first(msg, ISAKMP_PAYLOAD_HASH);
	if (!payload) {
		log_print("ipsec_fill_in_hash: no HASH payload found");
		return -1;
	}
	buf = payload->p;

	/* Allocate the prf and start calculating our HASH(1).  */
	LOG_DBG_BUF((LOG_MISC, 90, "ipsec_fill_in_hash: SKEYID_a",
	    isa->skeyid_a, isa->skeyid_len));
	prf = prf_alloc(isa->prf_type, hash->type, isa->skeyid_a,
	    isa->skeyid_len);
	if (!prf)
		return -1;

	prf->Init(prf->prfctx);
	LOG_DBG_BUF((LOG_MISC, 90, "ipsec_fill_in_hash: message_id",
	    exchange->message_id, ISAKMP_HDR_MESSAGE_ID_LEN));
	prf->Update(prf->prfctx, exchange->message_id,
	    ISAKMP_HDR_MESSAGE_ID_LEN);

	/* Loop over all payloads after HASH(1).  */
	for (i = 2; i < msg->iovlen; i++) {
		/* XXX Misleading payload type printouts.  */
		snprintf(header, sizeof header,
		    "ipsec_fill_in_hash: payload %d after HASH(1)", i - 1);
		LOG_DBG_BUF((LOG_MISC, 90, header, msg->iov[i].iov_base,
		    msg->iov[i].iov_len));
		prf->Update(prf->prfctx, msg->iov[i].iov_base,
		    msg->iov[i].iov_len);
	}
	prf->Final(buf + ISAKMP_HASH_DATA_OFF, prf->prfctx);
	prf_free(prf);
	LOG_DBG_BUF((LOG_MISC, 80, "ipsec_fill_in_hash: HASH(1)", buf +
	    ISAKMP_HASH_DATA_OFF, hash->hashsize));

	return 0;
}

/* Add a HASH payload to MSG, if we have an ISAKMP SA we're protected by.  */
static int
ipsec_informational_pre_hook(struct message *msg)
{
	struct sa      *isakmp_sa = msg->isakmp_sa;
	struct ipsec_sa *isa;
	struct hash    *hash;

	if (!isakmp_sa)
		return 0;
	isa = isakmp_sa->data;
	hash = hash_get(isa->hash);
	return ipsec_add_hash_payload(msg, hash->hashsize) == 0;
}

/*
 * Fill in the HASH payload in MSG, if we have an ISAKMP SA we're protected by.
 */
static int
ipsec_informational_post_hook(struct message *msg)
{
	if (!msg->isakmp_sa)
		return 0;
	return ipsec_fill_in_hash(msg);
}

ssize_t
ipsec_id_size(char *section, u_int8_t *id)
{
	char *type, *data;

	type = conf_get_str(section, "ID-type");
	if (!type) {
		log_print("ipsec_id_size: section %s has no \"ID-type\" tag",
		    section);
		return -1;
	}
	*id = constant_value(ipsec_id_cst, type);
	switch (*id) {
	case IPSEC_ID_IPV4_ADDR:
		return sizeof(struct in_addr);
	case IPSEC_ID_IPV4_ADDR_SUBNET:
		return 2 * sizeof(struct in_addr);
	case IPSEC_ID_IPV6_ADDR:
		return sizeof(struct in6_addr);
	case IPSEC_ID_IPV6_ADDR_SUBNET:
		return 2 * sizeof(struct in6_addr);
	case IPSEC_ID_FQDN:
	case IPSEC_ID_USER_FQDN:
	case IPSEC_ID_KEY_ID:
	case IPSEC_ID_DER_ASN1_DN:
	case IPSEC_ID_DER_ASN1_GN:
		data = conf_get_str(section, "Name");
		if (!data) {
			log_print("ipsec_id_size: "
			    "section %s has no \"Name\" tag", section);
			return -1;
		}
		return strlen(data);
	}
	log_print("ipsec_id_size: unrecognized/unsupported ID-type %d (%s)",
	    *id, type);
	return -1;
}

/*
 * Generate a string version of the ID.
 */
char *
ipsec_id_string(u_int8_t *id, size_t id_len)
{
	char           *buf = 0;
	char           *addrstr = 0;
	size_t          len, size;

	/*
	 * XXX Real ugly way of making the offsets correct.  Be aware that id
	 * now will point before the actual buffer and cannot be dereferenced
	 * without an offset larger than or equal to ISAKM_GEN_SZ.
	 */
	id -= ISAKMP_GEN_SZ;

	/* This is the actual length of the ID data field.  */
	id_len += ISAKMP_GEN_SZ - ISAKMP_ID_DATA_OFF;

	/*
	 * Conservative allocation.
	 * XXX I think the ASN1 DN case can be thought through to give a better
	 * estimate.
	 */
	size = MAXIMUM(sizeof "ipv6/ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
	    sizeof "asn1_dn/" + id_len);
	buf = malloc(size);
	if (!buf)
		/* XXX Log?  */
		goto fail;

	switch (GET_ISAKMP_ID_TYPE(id)) {
	case IPSEC_ID_IPV4_ADDR:
		if (id_len < sizeof(struct in_addr))
			goto fail;
		util_ntoa(&addrstr, AF_INET, id + ISAKMP_ID_DATA_OFF);
		if (!addrstr)
			goto fail;
		snprintf(buf, size, "ipv4/%s", addrstr);
		break;

	case IPSEC_ID_IPV6_ADDR:
		if (id_len < sizeof(struct in6_addr))
			goto fail;
		util_ntoa(&addrstr, AF_INET6, id + ISAKMP_ID_DATA_OFF);
		if (!addrstr)
			goto fail;
		snprintf(buf, size, "ipv6/%s", addrstr);
		break;

	case IPSEC_ID_FQDN:
	case IPSEC_ID_USER_FQDN:
		strlcpy(buf, GET_ISAKMP_ID_TYPE(id) == IPSEC_ID_FQDN ?
		    "fqdn/" : "ufqdn/", size);
		len = strlen(buf);

		memcpy(buf + len, id + ISAKMP_ID_DATA_OFF, id_len);
		*(buf + len + id_len) = '\0';
		break;

	case IPSEC_ID_DER_ASN1_DN:
		strlcpy(buf, "asn1_dn/", size);
		len = strlen(buf);
		addrstr = x509_DN_string(id + ISAKMP_ID_DATA_OFF, id_len);
		if (!addrstr)
			goto fail;
		if (size < len + strlen(addrstr) + 1)
			goto fail;
		strlcpy(buf + len, addrstr, size - len);
		break;

	default:
		/* Unknown type.  */
		LOG_DBG((LOG_MISC, 10,
		    "ipsec_id_string: unknown identity type %d\n",
		    GET_ISAKMP_ID_TYPE(id)));
		goto fail;
	}

	free(addrstr);
	return buf;

fail:
	free(buf);
	free(addrstr);
	return 0;
}
