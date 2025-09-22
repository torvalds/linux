/* $OpenBSD: dnssec.c,v 1.28 2021/10/09 18:43:50 deraadt Exp $	 */

/*
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

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include <openssl/rsa.h>

#ifdef LWRES
#include <lwres/netdb.h>
#include <dns/keyvalues.h>
#else
#include <netdb.h>
#endif

#include "dnssec.h"
#include "exchange.h"
#include "ipsec_num.h"
#include "libcrypto.h"
#include "log.h"
#include "message.h"
#include "transport.h"
#include "util.h"

#ifndef DNS_UFQDN_SEPARATOR
#define DNS_UFQDN_SEPARATOR "._ipsec."
#endif

/* adapted from <dns/rdatastruct.h> / RFC 2535  */
struct dns_rdata_key {
	u_int16_t       flags;
	u_int8_t        protocol;
	u_int8_t        algorithm;
	u_int16_t       datalen;
	unsigned char  *data;
};

void *
dns_get_key(int type, struct message *msg, int *keylen)
{
	struct exchange *exchange = msg->exchange;
	struct rrsetinfo *rr;
	struct dns_rdata_key key_rr;
	char            name[HOST_NAME_MAX+1];
	in_addr_t       ip4;
	u_int8_t        algorithm, *id, *umark;
	size_t          id_len;
	int             ret, i;

	switch (type) {
	case IKE_AUTH_RSA_SIG:
		algorithm = DNS_KEYALG_RSA;
		break;

	case IKE_AUTH_RSA_ENC:
	case IKE_AUTH_RSA_ENC_REV:
		/* XXX Not yet. */
		/* algorithm = DNS_KEYALG_RSA; */
		return 0;

	case IKE_AUTH_DSS:
		/* XXX Not yet. */
		/* algorithm = DNS_KEYALG_DSS; */
		return 0;

	case IKE_AUTH_PRE_SHARED:
	default:
		return 0;
	}

	id = exchange->initiator ? exchange->id_r : exchange->id_i;
	id_len = exchange->initiator ? exchange->id_r_len : exchange->id_i_len;
	bzero(name, sizeof name);

	if (!id || id_len == 0) {
		log_print("dns_get_key: ID is missing");
		return 0;
	}
	/* Exchanges (and SAs) don't carry the ID in ISAKMP form */
	id -= ISAKMP_GEN_SZ;
	id_len += ISAKMP_GEN_SZ - ISAKMP_ID_DATA_OFF;

	switch (GET_ISAKMP_ID_TYPE(id)) {
	case IPSEC_ID_IPV4_ADDR:
		/* We want to lookup a KEY RR in the reverse zone.  */
		if (id_len < sizeof ip4)
			return 0;
		memcpy(&ip4, id + ISAKMP_ID_DATA_OFF, sizeof ip4);
		snprintf(name, sizeof name, "%d.%d.%d.%d.in-addr.arpa.", ip4
		    >> 24, (ip4 >> 16) & 0xFF, (ip4 >> 8) & 0xFF, ip4 & 0xFF);
		break;

	case IPSEC_ID_IPV6_ADDR:
		/* XXX Not yet. */
		return 0;
		break;

	case IPSEC_ID_FQDN:
		if ((id_len + 1) >= sizeof name)
			return 0;
		/* ID is not NULL-terminated. Add trailing dot and NULL.  */
		memcpy(name, id + ISAKMP_ID_DATA_OFF, id_len);
		*(name + id_len) = '.';
		*(name + id_len + 1) = '\0';
		break;

	case IPSEC_ID_USER_FQDN:
		/*
		 * Some special handling here. We want to convert the ID
		 * 'user@host.domain' string into 'user._ipsec.host.domain.'.
		 */
		if ((id_len + sizeof(DNS_UFQDN_SEPARATOR)) >= sizeof name)
			return 0;
		/* Look for the '@' separator.  */
		for (umark = id + ISAKMP_ID_DATA_OFF; (umark - id) < id_len;
		    umark++)
			if (*umark == '@')
				break;
		if (*umark != '@') {
			LOG_DBG((LOG_MISC, 50, "dns_get_key: bad UFQDN ID"));
			return 0;
		}
		*umark++ = '\0';
		/* id is now terminated. 'umark', however, is not.  */
		snprintf(name, sizeof name, "%s%s", id + ISAKMP_ID_DATA_OFF,
		    DNS_UFQDN_SEPARATOR);
		memcpy(name + strlen(name), umark, id_len - strlen(id) - 1);
		*(name + id_len + sizeof(DNS_UFQDN_SEPARATOR) - 2) = '.';
		*(name + id_len + sizeof(DNS_UFQDN_SEPARATOR) - 1) = '\0';
		break;

	default:
		return 0;
	}

	LOG_DBG((LOG_MISC, 50, "dns_get_key: trying KEY RR for %s", name));
	ret = getrrsetbyname(name, C_IN, T_KEY, 0, &rr);

	if (ret) {
		LOG_DBG((LOG_MISC, 30, "dns_get_key: no DNS responses "
		    "(error %d)", ret));
		return 0;
	}
	LOG_DBG((LOG_MISC, 80,
	    "dns_get_key: rrset class %d type %d ttl %d nrdatas %d nrsigs %d",
	    rr->rri_rdclass, rr->rri_rdtype, rr->rri_ttl, rr->rri_nrdatas,
	    rr->rri_nsigs));

	/* We don't accept unvalidated data. */
	if (!(rr->rri_flags & RRSET_VALIDATED)) {
		LOG_DBG((LOG_MISC, 10, "dns_get_key: "
		    "got unvalidated response"));
		freerrset(rr);
		return 0;
	}
	/* Sanity. */
	if (rr->rri_nrdatas == 0 || rr->rri_rdtype != T_KEY) {
		LOG_DBG((LOG_MISC, 30, "dns_get_key: no KEY RRs received"));
		freerrset(rr);
		return 0;
	}
	bzero(&key_rr, sizeof key_rr);

	/*
	 * Find a key with the wanted algorithm, if any.
	 * XXX If there are several keys present, we currently only find the
	 * first.
	 */
	for (i = 0; i < rr->rri_nrdatas && key_rr.datalen == 0; i++) {
		key_rr.flags = ntohs((u_int16_t) * rr->rri_rdatas[i].rdi_data);
		key_rr.protocol = *(rr->rri_rdatas[i].rdi_data + 2);
		key_rr.algorithm = *(rr->rri_rdatas[i].rdi_data + 3);

		if (key_rr.protocol != DNS_KEYPROTO_IPSEC) {
			LOG_DBG((LOG_MISC, 50, "dns_get_key: ignored "
			    "non-IPsec key"));
			continue;
		}
		if (key_rr.algorithm != algorithm) {
			LOG_DBG((LOG_MISC, 50, "dns_get_key: ignored "
			    "key with other alg"));
			continue;
		}
		key_rr.datalen = rr->rri_rdatas[i].rdi_length - 4;
		if (key_rr.datalen <= 0) {
			LOG_DBG((LOG_MISC, 50, "dns_get_key: "
			    "ignored bad key"));
			key_rr.datalen = 0;
			continue;
		}
		/* This key seems to fit our requirements... */
		key_rr.data = malloc(key_rr.datalen);
		if (!key_rr.data) {
			log_error("dns_get_key: malloc (%d) failed",
			    key_rr.datalen);
			freerrset(rr);
			return 0;
		}
		memcpy(key_rr.data, rr->rri_rdatas[i].rdi_data + 4,
		    key_rr.datalen);
		*keylen = key_rr.datalen;
	}

	freerrset(rr);

	if (key_rr.datalen)
		return key_rr.data;
	return 0;
}

int
dns_RSA_dns_to_x509(u_int8_t *key, int keylen, RSA **rsa_key)
{
	RSA	*rsa;
	int	 key_offset;
	u_int8_t e_len;

	if (!key || keylen <= 0) {
		log_print("dns_RSA_dns_to_x509: invalid public key");
		return -1;
	}
	rsa = RSA_new();
	if (rsa == NULL) {
		log_error("dns_RSA_dns_to_x509: "
		    "failed to allocate new RSA struct");
		return -1;
	}
	e_len = *key;
	key_offset = 1;

	if (e_len == 0) {
		if (keylen < 3) {
			log_print("dns_RSA_dns_to_x509: invalid public key");
			RSA_free(rsa);
			return -1;
		}
		e_len = *(key + key_offset++) << 8;
		e_len += *(key + key_offset++);
	}
	if (e_len > (keylen - key_offset)) {
		log_print("dns_RSA_dns_to_x509: invalid public key");
		RSA_free(rsa);
		return -1;
	}
	rsa->e = BN_bin2bn(key + key_offset, e_len, NULL);
	key_offset += e_len;

	/* XXX if (keylen <= key_offset) -> "invalid public key" ? */

	rsa->n = BN_bin2bn(key + key_offset, keylen - key_offset, NULL);

	*rsa_key = rsa;

	LOG_DBG((LOG_MISC, 30, "dns_RSA_dns_to_x509: got %d bits RSA key",
	    BN_num_bits(rsa->n)));
	return 0;
}

#if notyet
int
dns_RSA_x509_to_dns(RSA *rsa_key, u_int8_t *key, int *keylen)
{
	return 0;
}
#endif
