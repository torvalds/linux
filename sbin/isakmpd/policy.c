/* $OpenBSD: policy.c,v 1.103 2024/04/28 16:43:42 florian Exp $	 */
/* $EOM: policy.c,v 1.49 2000/10/24 13:33:39 niklas Exp $ */

/*
 * Copyright (c) 1999, 2000, 2001 Angelos D. Keromytis.  All rights reserved.
 * Copyright (c) 1999, 2000, 2001 Niklas Hallqvist.  All rights reserved.
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
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <regex.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <keynote.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <netdb.h>

#include "conf.h"
#include "exchange.h"
#include "ipsec.h"
#include "isakmp_doi.h"
#include "sa.h"
#include "transport.h"
#include "log.h"
#include "message.h"
#include "monitor.h"
#include "util.h"
#include "policy.h"
#include "x509.h"

char          **policy_asserts = NULL;
int		ignore_policy = 0;
int             policy_asserts_num = 0;
struct exchange *policy_exchange = 0;
struct sa      *policy_sa = 0;
struct sa      *policy_isakmp_sa = 0;

static const char hextab[] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

/*
 * Adaptation of Vixie's inet_ntop4 ()
 */
static const char *
my_inet_ntop4(const in_addr_t *src, char *dst, size_t size, int normalize)
{
	static const char fmt[] = "%03u.%03u.%03u.%03u";
	char            tmp[sizeof "255.255.255.255"];
	in_addr_t       src2;
	int		len;

	if (normalize)
		src2 = ntohl(*src);
	else
		src2 = *src;

	len = snprintf(tmp, sizeof tmp, fmt, ((u_int8_t *)&src2)[0],
	    ((u_int8_t *)&src2)[1], ((u_int8_t *)&src2)[2],
	    ((u_int8_t *)&src2)[3]);
	if (len < 0 || len > (int)size) {
		errno = ENOSPC;
		return 0;
	}
	strlcpy(dst, tmp, size);
	return dst;
}

static const char *
my_inet_ntop6(const unsigned char *src, char *dst, size_t size)
{
	static const char fmt[] =
	    "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x";
	char	tmp[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"];
	int	len;

	len = snprintf(tmp, sizeof tmp, fmt, src[0], src[1], src[2], src[3],
	    src[4], src[5], src[6], src[7], src[8], src[9], src[10], src[11],
	    src[12], src[13], src[14], src[15]);
	if (len < 0 || len > (int)size) {
		errno = ENOSPC;
		return 0;
	}
	strlcpy(dst, tmp, size);
	return dst;
}

char *
policy_callback(char *name)
{
	struct proto   *proto;

	u_int8_t       *attr, *value, *id, *idlocal, *idremote;
	size_t          id_sz, idlocalsz, idremotesz;
	struct sockaddr *sin;
	struct ipsec_exch *ie;
	struct ipsec_sa *is;
	size_t          i;
	int             fmt, lifetype = 0;
	in_addr_t       net, subnet;
	u_int16_t       len, type;
	time_t          tt;
	char           *addr;
	static char     mytimeofday[15];

	/* We use all these as a cache.  */
#define PMAX 32
	static char    *esp_present, *ah_present, *comp_present;
	static char    *ah_hash_alg, *ah_auth_alg, *esp_auth_alg, *esp_enc_alg;
	static char    *comp_alg, ah_life_kbytes[PMAX], ah_life_seconds[PMAX];
	static char     esp_life_kbytes[PMAX], esp_life_seconds[PMAX];
	static char     comp_life_kbytes[PMAX];
	static char    *ah_ecn, *esp_ecn, *comp_ecn;
	static char     comp_life_seconds[PMAX], *ah_encapsulation;
	static char    *esp_encapsulation, *comp_encapsulation;
	static char     ah_key_length[PMAX], esp_key_length[PMAX];
	static char     ah_key_rounds[PMAX], esp_key_rounds[PMAX];
	static char	comp_dict_size[PMAX], comp_private_alg[PMAX];
	static char    *remote_filter_type, *local_filter_type;
	static char     remote_filter_addr_upper[NI_MAXHOST];
	static char     remote_filter_addr_lower[NI_MAXHOST];
	static char     local_filter_addr_upper[NI_MAXHOST];
	static char     local_filter_addr_lower[NI_MAXHOST];
	static char     ah_group_desc[PMAX], esp_group_desc[PMAX];
	static char	comp_group_desc[PMAX], remote_ike_address[NI_MAXHOST];
	static char     local_ike_address[NI_MAXHOST];
	static char    *remote_id_type, remote_id_addr_upper[NI_MAXHOST];
	static char    *phase_1, remote_id_addr_lower[NI_MAXHOST];
	static char    *remote_id_proto, remote_id_port[PMAX];
	static char     remote_filter_port[PMAX], local_filter_port[PMAX];
	static char    *remote_filter_proto, *local_filter_proto, *pfs;
	static char    *initiator, remote_filter_proto_num[3];
	static char	local_filter_proto_num[3], remote_id_proto_num[3];
	static char     phase1_group[PMAX];

	/* Allocated.  */
	static char    *remote_filter = 0, *local_filter = 0, *remote_id = 0;

	static int      dirty = 1;

	/* We only need to set dirty at initialization time really.  */
	if (strcmp(name, KEYNOTE_CALLBACK_CLEANUP) == 0 ||
	    strcmp(name, KEYNOTE_CALLBACK_INITIALIZE) == 0) {
		esp_present = ah_present = comp_present = pfs = "no";
		ah_hash_alg = ah_auth_alg = phase_1 = "";
		esp_auth_alg = esp_enc_alg = comp_alg = ah_encapsulation = "";
		ah_ecn = esp_ecn = comp_ecn = "no";
		esp_encapsulation = comp_encapsulation = "";
		remote_filter_type = "";
		local_filter_type = remote_id_type = initiator = "";
		remote_filter_proto = local_filter_proto = "";
		remote_id_proto = "";

		free(remote_filter);
		remote_filter = 0;
		free(local_filter);
		local_filter = 0;
		free(remote_id);
		remote_id = 0;

		bzero(remote_ike_address, sizeof remote_ike_address);
		bzero(local_ike_address, sizeof local_ike_address);
		bzero(ah_life_kbytes, sizeof ah_life_kbytes);
		bzero(ah_life_seconds, sizeof ah_life_seconds);
		bzero(esp_life_kbytes, sizeof esp_life_kbytes);
		bzero(esp_life_seconds, sizeof esp_life_seconds);
		bzero(comp_life_kbytes, sizeof comp_life_kbytes);
		bzero(comp_life_seconds, sizeof comp_life_seconds);
		bzero(ah_key_length, sizeof ah_key_length);
		bzero(ah_key_rounds, sizeof ah_key_rounds);
		bzero(esp_key_length, sizeof esp_key_length);
		bzero(esp_key_rounds, sizeof esp_key_rounds);
		bzero(comp_dict_size, sizeof comp_dict_size);
		bzero(comp_private_alg, sizeof comp_private_alg);
		bzero(remote_filter_addr_upper,
		    sizeof remote_filter_addr_upper);
		bzero(remote_filter_addr_lower,
		    sizeof remote_filter_addr_lower);
		bzero(local_filter_addr_upper,
		    sizeof local_filter_addr_upper);
		bzero(local_filter_addr_lower,
		    sizeof local_filter_addr_lower);
		bzero(remote_id_addr_upper, sizeof remote_id_addr_upper);
		bzero(remote_id_addr_lower, sizeof remote_id_addr_lower);
		bzero(ah_group_desc, sizeof ah_group_desc);
		bzero(esp_group_desc, sizeof esp_group_desc);
		bzero(remote_id_port, sizeof remote_id_port);
		bzero(remote_filter_port, sizeof remote_filter_port);
		bzero(local_filter_port, sizeof local_filter_port);
		bzero(phase1_group, sizeof phase1_group);

		dirty = 1;
		return "";
	}
	/*
	 * If dirty is set, this is the first request for an attribute, so
	 * populate our value cache.
	 */
	if (dirty) {
		ie = policy_exchange->data;

		if (ie->pfs)
			pfs = "yes";

		is = policy_isakmp_sa->data;
		snprintf(phase1_group, sizeof phase1_group, "%u",
		    is->group_desc);

		for (proto = TAILQ_FIRST(&policy_sa->protos); proto;
		    proto = TAILQ_NEXT(proto, link)) {
			switch (proto->proto) {
			case IPSEC_PROTO_IPSEC_AH:
				ah_present = "yes";
				switch (proto->id) {
				case IPSEC_AH_MD5:
					ah_hash_alg = "md5";
					break;

				case IPSEC_AH_SHA:
					ah_hash_alg = "sha";
					break;

				case IPSEC_AH_RIPEMD:
					ah_hash_alg = "ripemd";
					break;

				case IPSEC_AH_SHA2_256:
					ah_auth_alg = "sha2-256";
					break;

				case IPSEC_AH_SHA2_384:
					ah_auth_alg = "sha2-384";
					break;

				case IPSEC_AH_SHA2_512:
					ah_auth_alg = "sha2-512";
					break;

				case IPSEC_AH_DES:
					ah_hash_alg = "des";
					break;
				}

				break;

			case IPSEC_PROTO_IPSEC_ESP:
				esp_present = "yes";
				switch (proto->id) {
				case IPSEC_ESP_DES_IV64:
					esp_enc_alg = "des-iv64";
					break;

				case IPSEC_ESP_DES:
					esp_enc_alg = "des";
					break;

				case IPSEC_ESP_3DES:
					esp_enc_alg = "3des";
					break;

				case IPSEC_ESP_AES:
				case IPSEC_ESP_AES_CTR:
				case IPSEC_ESP_AES_GCM_16:
				case IPSEC_ESP_AES_GMAC:
					esp_enc_alg = "aes";
					break;

				case IPSEC_ESP_RC5:
					esp_enc_alg = "rc5";
					break;

				case IPSEC_ESP_IDEA:
					esp_enc_alg = "idea";
					break;

				case IPSEC_ESP_CAST:
					esp_enc_alg = "cast";
					break;

				case IPSEC_ESP_BLOWFISH:
					esp_enc_alg = "blowfish";
					break;

				case IPSEC_ESP_3IDEA:
					esp_enc_alg = "3idea";
					break;

				case IPSEC_ESP_DES_IV32:
					esp_enc_alg = "des-iv32";
					break;

				case IPSEC_ESP_RC4:
					esp_enc_alg = "rc4";
					break;

				case IPSEC_ESP_NULL:
					esp_enc_alg = "null";
					break;
				}

				break;

			case IPSEC_PROTO_IPCOMP:
				comp_present = "yes";
				switch (proto->id) {
				case IPSEC_IPCOMP_OUI:
					comp_alg = "oui";
					break;

				case IPSEC_IPCOMP_DEFLATE:
					comp_alg = "deflate";
					break;
				}

				break;
			}

			for (attr = proto->chosen->p +
			    ISAKMP_TRANSFORM_SA_ATTRS_OFF;
			    attr < proto->chosen->p +
			    GET_ISAKMP_GEN_LENGTH(proto->chosen->p);
			    attr = value + len) {
				if (attr + ISAKMP_ATTR_VALUE_OFF >
				    (proto->chosen->p +
				    GET_ISAKMP_GEN_LENGTH(proto->chosen->p)))
					return "";

				type = GET_ISAKMP_ATTR_TYPE(attr);
				fmt = ISAKMP_ATTR_FORMAT(type);
				type = ISAKMP_ATTR_TYPE(type);
				value = attr + (fmt ?
				    ISAKMP_ATTR_LENGTH_VALUE_OFF :
				    ISAKMP_ATTR_VALUE_OFF);
				len = (fmt ? ISAKMP_ATTR_LENGTH_VALUE_LEN :
				    GET_ISAKMP_ATTR_LENGTH_VALUE(attr));

				if (value + len > proto->chosen->p +
				    GET_ISAKMP_GEN_LENGTH(proto->chosen->p))
					return "";

				switch (type) {
				case IPSEC_ATTR_SA_LIFE_TYPE:
					lifetype = decode_16(value);
					break;

				case IPSEC_ATTR_SA_LIFE_DURATION:
					switch (proto->proto) {
					case IPSEC_PROTO_IPSEC_AH:
						if (lifetype == IPSEC_DURATION_SECONDS) {
							if (len == 2)
								snprintf(ah_life_seconds, sizeof ah_life_seconds,
								    "%u", decode_16(value));
							else
								snprintf(ah_life_seconds, sizeof ah_life_seconds,
								    "%u", decode_32(value));
						} else {
							if (len == 2)
								snprintf(ah_life_kbytes, sizeof ah_life_kbytes,
								    "%u", decode_16(value));
							else
								snprintf(ah_life_kbytes, sizeof ah_life_kbytes,
								    "%u", decode_32(value));
						}

						break;

					case IPSEC_PROTO_IPSEC_ESP:
						if (lifetype == IPSEC_DURATION_SECONDS) {
							if (len == 2)
								snprintf(esp_life_seconds,
								    sizeof esp_life_seconds, "%u",
								    decode_16(value));
							else
								snprintf(esp_life_seconds,
								    sizeof esp_life_seconds, "%u",
								    decode_32(value));
						} else {
							if (len == 2)
								snprintf(esp_life_kbytes,
								    sizeof esp_life_kbytes, "%u",
								    decode_16(value));
							else
								snprintf(esp_life_kbytes,
								    sizeof esp_life_kbytes, "%u",
								    decode_32(value));
						}

						break;

					case IPSEC_PROTO_IPCOMP:
						if (lifetype == IPSEC_DURATION_SECONDS) {
							if (len == 2)
								snprintf(comp_life_seconds,
								    sizeof comp_life_seconds, "%u",
								    decode_16(value));
							else
								snprintf(comp_life_seconds,
								    sizeof comp_life_seconds, "%u",
								    decode_32(value));
						} else {
							if (len == 2)
								snprintf(comp_life_kbytes,
								    sizeof comp_life_kbytes, "%u",
								    decode_16(value));
							else
								snprintf(comp_life_kbytes,
								    sizeof comp_life_kbytes, "%u",
								    decode_32(value));
						}
						break;
					}
					break;

				case IPSEC_ATTR_GROUP_DESCRIPTION:
					switch (proto->proto) {
					case IPSEC_PROTO_IPSEC_AH:
						snprintf(ah_group_desc,
						    sizeof ah_group_desc, "%u",
						    decode_16(value));
						break;

					case IPSEC_PROTO_IPSEC_ESP:
						snprintf(esp_group_desc,
						    sizeof esp_group_desc, "%u",
						    decode_16(value));
						break;

					case IPSEC_PROTO_IPCOMP:
						snprintf(comp_group_desc,
						    sizeof comp_group_desc, "%u",
						    decode_16(value));
						break;
					}
					break;

				case IPSEC_ATTR_ECN_TUNNEL:
					if (decode_16(value))
						switch (proto->proto) {
						case IPSEC_PROTO_IPSEC_AH:
							ah_ecn = "yes";
							break;

						case IPSEC_PROTO_IPSEC_ESP:
							esp_ecn = "yes";
							break;

						case IPSEC_PROTO_IPCOMP:
							comp_ecn = "yes";
							break;
						}

				case IPSEC_ATTR_ENCAPSULATION_MODE:
					if (decode_16(value) == IPSEC_ENCAP_TUNNEL)
						switch (proto->proto) {
						case IPSEC_PROTO_IPSEC_AH:
							ah_encapsulation = "tunnel";
							break;

						case IPSEC_PROTO_IPSEC_ESP:
							esp_encapsulation = "tunnel";
							break;

						case IPSEC_PROTO_IPCOMP:
							comp_encapsulation = "tunnel";
							break;
						}
					else if (decode_16(value) ==
					    IPSEC_ENCAP_UDP_ENCAP_TUNNEL ||
					    decode_16(value) ==
					    IPSEC_ENCAP_UDP_ENCAP_TUNNEL_DRAFT)
						switch (proto->proto) {
						case IPSEC_PROTO_IPSEC_AH:
							ah_encapsulation = "udp-encap-tunnel";
							break;

						case IPSEC_PROTO_IPSEC_ESP:
							esp_encapsulation = "udp-encap-tunnel";
							break;

						case IPSEC_PROTO_IPCOMP:
							comp_encapsulation = "udp-encap-tunnel";
							break;
						}
					/* XXX IPSEC_ENCAP_UDP_ENCAP_TRANSPORT */
					else
						switch (proto->proto) {
						case IPSEC_PROTO_IPSEC_AH:
							ah_encapsulation = "transport";
							break;

						case IPSEC_PROTO_IPSEC_ESP:
							esp_encapsulation = "transport";
							break;

						case IPSEC_PROTO_IPCOMP:
							comp_encapsulation = "transport";
							break;
						}
					break;

				case IPSEC_ATTR_AUTHENTICATION_ALGORITHM:
					switch (proto->proto) {
					case IPSEC_PROTO_IPSEC_AH:
						switch (decode_16(value)) {
						case IPSEC_AUTH_HMAC_MD5:
							ah_auth_alg = "hmac-md5";
							break;

						case IPSEC_AUTH_HMAC_SHA:
							ah_auth_alg = "hmac-sha";
							break;

						case IPSEC_AUTH_HMAC_RIPEMD:
							ah_auth_alg = "hmac-ripemd";
							break;

						case IPSEC_AUTH_HMAC_SHA2_256:
							ah_auth_alg = "hmac-sha2-256";
							break;

						case IPSEC_AUTH_HMAC_SHA2_384:
							ah_auth_alg = "hmac-sha2-384";
							break;

						case IPSEC_AUTH_HMAC_SHA2_512:
							ah_auth_alg = "hmac-sha2-512";
							break;

						case IPSEC_AUTH_DES_MAC:
							ah_auth_alg = "des-mac";
							break;

						case IPSEC_AUTH_KPDK:
							ah_auth_alg = "kpdk";
							break;
						}
						break;

					case IPSEC_PROTO_IPSEC_ESP:
						switch (decode_16(value)) {
						case IPSEC_AUTH_HMAC_MD5:
							esp_auth_alg = "hmac-md5";
							break;

						case IPSEC_AUTH_HMAC_SHA:
							esp_auth_alg = "hmac-sha";
							break;

						case IPSEC_AUTH_HMAC_RIPEMD:
							esp_auth_alg = "hmac-ripemd";
							break;

						case IPSEC_AUTH_HMAC_SHA2_256:
							esp_auth_alg = "hmac-sha2-256";
							break;

						case IPSEC_AUTH_HMAC_SHA2_384:
							esp_auth_alg = "hmac-sha2-384";
							break;

						case IPSEC_AUTH_HMAC_SHA2_512:
							esp_auth_alg = "hmac-sha2-512";
							break;

						case IPSEC_AUTH_DES_MAC:
							esp_auth_alg = "des-mac";
							break;

						case IPSEC_AUTH_KPDK:
							esp_auth_alg = "kpdk";
							break;
						}
						break;
					}
					break;

				case IPSEC_ATTR_KEY_LENGTH:
					switch (proto->proto) {
					case IPSEC_PROTO_IPSEC_AH:
						snprintf(ah_key_length,
						    sizeof ah_key_length, "%u",
						    decode_16(value));
						break;

					case IPSEC_PROTO_IPSEC_ESP:
						snprintf(esp_key_length,
						    sizeof esp_key_length, "%u",
						    decode_16(value));
						break;
					}
					break;

				case IPSEC_ATTR_KEY_ROUNDS:
					switch (proto->proto) {
					case IPSEC_PROTO_IPSEC_AH:
						snprintf(ah_key_rounds,
						    sizeof ah_key_rounds, "%u",
						    decode_16(value));
						break;

					case IPSEC_PROTO_IPSEC_ESP:
						snprintf(esp_key_rounds,
						    sizeof esp_key_rounds, "%u",
						    decode_16(value));
						break;
					}
					break;

				case IPSEC_ATTR_COMPRESS_DICTIONARY_SIZE:
					snprintf(comp_dict_size,
					    sizeof comp_dict_size, "%u",
					    decode_16(value));
					break;

				case IPSEC_ATTR_COMPRESS_PRIVATE_ALGORITHM:
					snprintf(comp_private_alg,
					    sizeof comp_private_alg, "%u",
					    decode_16(value));
					break;
				}
			}
		}

		policy_sa->transport->vtbl->get_src(policy_sa->transport,
		    &sin);
		if (sockaddr2text(sin, &addr, 1)) {
			log_error("policy_callback: sockaddr2text failed");
			goto bad;
		}
		strlcpy(local_ike_address, addr, sizeof local_ike_address);
		free(addr);

		policy_sa->transport->vtbl->get_dst(policy_sa->transport,
		    &sin);
		if (sockaddr2text(sin, &addr, 1)) {
			log_error("policy_callback: sockaddr2text failed");
			goto bad;
		}
		strlcpy(remote_ike_address, addr, sizeof remote_ike_address);
		free(addr);

		switch (policy_isakmp_sa->exch_type) {
		case ISAKMP_EXCH_AGGRESSIVE:
			phase_1 = "aggressive";
			break;

		case ISAKMP_EXCH_ID_PROT:
			phase_1 = "main";
			break;
		}

		if (policy_isakmp_sa->initiator) {
			id = policy_isakmp_sa->id_r;
			id_sz = policy_isakmp_sa->id_r_len;
		} else {
			id = policy_isakmp_sa->id_i;
			id_sz = policy_isakmp_sa->id_i_len;
		}

		switch (id[0]) {
		case IPSEC_ID_IPV4_ADDR:
			remote_id_type = "IPv4 address";

			net = decode_32(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ);
			my_inet_ntop4(&net, remote_id_addr_upper,
			    sizeof remote_id_addr_upper - 1, 1);
			my_inet_ntop4(&net, remote_id_addr_lower,
			    sizeof remote_id_addr_lower - 1, 1);
			remote_id = strdup(remote_id_addr_upper);
			if (!remote_id) {
				log_error("policy_callback: "
				    "strdup (\"%s\") failed",
				    remote_id_addr_upper);
				goto bad;
			}
			break;

		case IPSEC_ID_IPV4_RANGE:
			remote_id_type = "IPv4 range";

			net = decode_32(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ);
			my_inet_ntop4(&net, remote_id_addr_lower,
			    sizeof remote_id_addr_lower - 1, 1);
			net = decode_32(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ + 4);
			my_inet_ntop4(&net, remote_id_addr_upper,
			    sizeof remote_id_addr_upper - 1, 1);
			len = strlen(remote_id_addr_upper) +
			    strlen(remote_id_addr_lower) + 2;
			remote_id = calloc(len, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%d, %lu) failed", len,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			strlcpy(remote_id, remote_id_addr_lower, len);
			strlcat(remote_id, "-", len);
			strlcat(remote_id, remote_id_addr_upper, len);
			break;

		case IPSEC_ID_IPV4_ADDR_SUBNET:
			remote_id_type = "IPv4 subnet";

			net = decode_32(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ);
			subnet = decode_32(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ + 4);
			net &= subnet;
			my_inet_ntop4(&net, remote_id_addr_lower,
			    sizeof remote_id_addr_lower - 1, 1);
			net |= ~subnet;
			my_inet_ntop4(&net, remote_id_addr_upper,
			    sizeof remote_id_addr_upper - 1, 1);
			len = strlen(remote_id_addr_upper) +
				 strlen(remote_id_addr_lower) + 2;
			remote_id = calloc(len, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%d, %lu) failed", len,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			strlcpy(remote_id, remote_id_addr_lower, len);
			strlcat(remote_id, "-", len);
			strlcat(remote_id, remote_id_addr_upper, len);
			break;

		case IPSEC_ID_IPV6_ADDR:
			remote_id_type = "IPv6 address";
			my_inet_ntop6(id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
			    remote_id_addr_upper, sizeof remote_id_addr_upper);
			strlcpy(remote_id_addr_lower, remote_id_addr_upper,
			    sizeof remote_id_addr_lower);
			remote_id = strdup(remote_id_addr_upper);
			if (!remote_id) {
				log_error("policy_callback: "
				    "strdup (\"%s\") failed",
				    remote_id_addr_upper);
				goto bad;
			}
			break;

		case IPSEC_ID_IPV6_RANGE:
			remote_id_type = "IPv6 range";

			my_inet_ntop6(id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
			    remote_id_addr_lower,
			    sizeof remote_id_addr_lower - 1);

			my_inet_ntop6(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ + 16, remote_id_addr_upper,
			    sizeof remote_id_addr_upper - 1);

			len = strlen(remote_id_addr_upper) +
			    strlen(remote_id_addr_lower) + 2;
			remote_id = calloc(len, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%d, %lu) failed", len,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			strlcpy(remote_id, remote_id_addr_lower, len);
			strlcat(remote_id, "-", len);
			strlcat(remote_id, remote_id_addr_upper, len);
			break;

		case IPSEC_ID_IPV6_ADDR_SUBNET:
		    {
			struct in6_addr net, mask;

			remote_id_type = "IPv6 subnet";

			bcopy(id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ, &net,
			    sizeof(net));
			bcopy(id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ + 16,
			    &mask, sizeof(mask));

			for (i = 0; i < 16; i++)
				net.s6_addr[i] &= mask.s6_addr[i];

			my_inet_ntop6((unsigned char *)&net,
			    remote_id_addr_lower,
			    sizeof remote_id_addr_lower - 1);

			for (i = 0; i < 16; i++)
				net.s6_addr[i] |= ~mask.s6_addr[i];

			my_inet_ntop6((unsigned char *)&net,
			    remote_id_addr_upper,
			    sizeof remote_id_addr_upper - 1);

			len = strlen(remote_id_addr_upper) +
			    strlen(remote_id_addr_lower) + 2;
			remote_id = calloc(len, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%d, %lu) failed", len,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			strlcpy(remote_id, remote_id_addr_lower, len);
			strlcat(remote_id, "-", len);
			strlcat(remote_id, remote_id_addr_upper, len);
			break;
		    }

		case IPSEC_ID_FQDN:
			remote_id_type = "FQDN";
			remote_id = calloc(id_sz - ISAKMP_ID_DATA_OFF +
			    ISAKMP_GEN_SZ + 1, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%lu, %lu) failed",
				    (unsigned long)id_sz - ISAKMP_ID_DATA_OFF +
				    ISAKMP_GEN_SZ + 1,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			memcpy(remote_id,
			    id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
			    id_sz - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ);
			break;

		case IPSEC_ID_USER_FQDN:
			remote_id_type = "User FQDN";
			remote_id = calloc(id_sz - ISAKMP_ID_DATA_OFF +
			    ISAKMP_GEN_SZ + 1, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%lu, %lu) failed",
				    (unsigned long)id_sz - ISAKMP_ID_DATA_OFF +
				    ISAKMP_GEN_SZ + 1,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			memcpy(remote_id,
			    id + ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ,
			    id_sz - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ);
			break;

		case IPSEC_ID_DER_ASN1_DN:
			remote_id_type = "ASN1 DN";

			remote_id = x509_DN_string(id + ISAKMP_ID_DATA_OFF -
			    ISAKMP_GEN_SZ,
			    id_sz - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ);
			if (!remote_id) {
				LOG_DBG((LOG_POLICY, 50,
				    "policy_callback: failed to decode name"));
				goto bad;
			}
			break;

		case IPSEC_ID_DER_ASN1_GN:	/* XXX */
			remote_id_type = "ASN1 GN";
			break;

		case IPSEC_ID_KEY_ID:
			remote_id_type = "Key ID";
			remote_id = calloc(2 * (id_sz - ISAKMP_ID_DATA_OFF +
			    ISAKMP_GEN_SZ) + 1, sizeof(char));
			if (!remote_id) {
				log_error("policy_callback: "
				    "calloc (%lu, %lu) failed",
				    2 * ((unsigned long)id_sz -
					ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ) + 1,
				    (unsigned long)sizeof(char));
				goto bad;
			}
			/* Does it contain any non-printable characters ? */
			for (i = 0;
			    i < id_sz - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ;
			    i++)
				if (!isprint((unsigned char)*(id + ISAKMP_ID_DATA_OFF -
				    ISAKMP_GEN_SZ + i)))
					break;
			if (i >= id_sz - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ) {
				memcpy(remote_id, id + ISAKMP_ID_DATA_OFF -
				    ISAKMP_GEN_SZ,
				    id_sz - ISAKMP_ID_DATA_OFF +
				    ISAKMP_GEN_SZ);
				break;
			}
			/* Non-printable characters, convert to hex */
			for (i = 0;
			    i < id_sz - ISAKMP_ID_DATA_OFF + ISAKMP_GEN_SZ;
			    i++) {
				remote_id[2 * i] = hextab[*(id +
				    ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ) >> 4];
				remote_id[2 * i + 1] = hextab[*(id +
				    ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ) & 0xF];
			}
			break;

		default:
			log_print("policy_callback: "
			    "unknown remote ID type %u", id[0]);
			goto bad;
		}

		switch (id[1]) {
		case IPPROTO_TCP:
			remote_id_proto = "tcp";
			break;

		case IPPROTO_UDP:
			remote_id_proto = "udp";
			break;

		case IPPROTO_ETHERIP:
			remote_id_proto = "etherip";
			break;

		default:
			snprintf(remote_id_proto_num,
			    sizeof remote_id_proto_num, "%d",
			    id[1]);
			remote_id_proto = remote_id_proto_num;
			break;
		}

		snprintf(remote_id_port, sizeof remote_id_port, "%u",
		    decode_16(id + 2));

		if (policy_exchange->initiator) {
			initiator = "yes";
			idlocal = ie->id_ci;
			idremote = ie->id_cr;
			idlocalsz = ie->id_ci_sz;
			idremotesz = ie->id_cr_sz;
		} else {
			initiator = "no";
			idlocal = ie->id_cr;
			idremote = ie->id_ci;
			idlocalsz = ie->id_cr_sz;
			idremotesz = ie->id_ci_sz;
		}

		/* Initialize the ID variables.  */
		if (idremote) {
			switch (GET_ISAKMP_ID_TYPE(idremote)) {
			case IPSEC_ID_IPV4_ADDR:
				remote_filter_type = "IPv4 address";

				net = decode_32(idremote + ISAKMP_ID_DATA_OFF);
				my_inet_ntop4(&net, remote_filter_addr_upper,
				    sizeof remote_filter_addr_upper - 1, 1);
				my_inet_ntop4(&net, remote_filter_addr_lower,
				    sizeof remote_filter_addr_lower - 1, 1);
				remote_filter =
				    strdup(remote_filter_addr_upper);
				if (!remote_filter) {
					log_error("policy_callback: strdup "
					    "(\"%s\") failed",
					    remote_filter_addr_upper);
					goto bad;
				}
				break;

			case IPSEC_ID_IPV4_RANGE:
				remote_filter_type = "IPv4 range";

				net = decode_32(idremote + ISAKMP_ID_DATA_OFF);
				my_inet_ntop4(&net, remote_filter_addr_lower,
				    sizeof remote_filter_addr_lower - 1, 1);
				net = decode_32(idremote + ISAKMP_ID_DATA_OFF +
				    4);
				my_inet_ntop4(&net, remote_filter_addr_upper,
				    sizeof remote_filter_addr_upper - 1, 1);
				len = strlen(remote_filter_addr_upper) +
				    strlen(remote_filter_addr_lower) + 2;
				remote_filter = calloc(len, sizeof(char));
				if (!remote_filter) {
					log_error("policy_callback: calloc "
					    "(%d, %lu) failed", len,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				strlcpy(remote_filter,
				    remote_filter_addr_lower, len);
				strlcat(remote_filter, "-", len);
				strlcat(remote_filter,
				    remote_filter_addr_upper, len);
				break;

			case IPSEC_ID_IPV4_ADDR_SUBNET:
				remote_filter_type = "IPv4 subnet";

				net = decode_32(idremote + ISAKMP_ID_DATA_OFF);
				subnet = decode_32(idremote +
				    ISAKMP_ID_DATA_OFF + 4);
				net &= subnet;
				my_inet_ntop4(&net, remote_filter_addr_lower,
				    sizeof remote_filter_addr_lower - 1, 1);
				net |= ~subnet;
				my_inet_ntop4(&net, remote_filter_addr_upper,
				    sizeof remote_filter_addr_upper - 1, 1);
				len = strlen(remote_filter_addr_upper) +
				    strlen(remote_filter_addr_lower) + 2;
				remote_filter = calloc(len, sizeof(char));
				if (!remote_filter) {
					log_error("policy_callback: calloc "
					    "(%d, %lu) failed", len,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				strlcpy(remote_filter,
				    remote_filter_addr_lower, len);
				strlcat(remote_filter, "-", len);
				strlcat(remote_filter,
				    remote_filter_addr_upper, len);
				break;

			case IPSEC_ID_IPV6_ADDR:
				remote_filter_type = "IPv6 address";
				my_inet_ntop6(idremote + ISAKMP_ID_DATA_OFF,
				    remote_filter_addr_upper,
				    sizeof remote_filter_addr_upper - 1);
				strlcpy(remote_filter_addr_lower,
				    remote_filter_addr_upper,
				    sizeof remote_filter_addr_lower);
				remote_filter =
				    strdup(remote_filter_addr_upper);
				if (!remote_filter) {
					log_error("policy_callback: strdup "
					    "(\"%s\") failed",
					    remote_filter_addr_upper);
					goto bad;
				}
				break;

			case IPSEC_ID_IPV6_RANGE:
				remote_filter_type = "IPv6 range";

				my_inet_ntop6(idremote + ISAKMP_ID_DATA_OFF,
				    remote_filter_addr_lower,
				    sizeof remote_filter_addr_lower - 1);

				my_inet_ntop6(idremote + ISAKMP_ID_DATA_OFF +
				    16, remote_filter_addr_upper,
				    sizeof remote_filter_addr_upper - 1);

				len = strlen(remote_filter_addr_upper) +
				    strlen(remote_filter_addr_lower) + 2;
				remote_filter = calloc(len, sizeof(char));
				if (!remote_filter) {
					log_error("policy_callback: calloc "
					    "(%d, %lu) failed", len,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				strlcpy(remote_filter,
				    remote_filter_addr_lower, len);
				strlcat(remote_filter, "-", len);
				strlcat(remote_filter,
				    remote_filter_addr_upper, len);
				break;

			case IPSEC_ID_IPV6_ADDR_SUBNET:
				{
					struct in6_addr net, mask;

					remote_filter_type = "IPv6 subnet";

					bcopy(idremote + ISAKMP_ID_DATA_OFF,
					    &net, sizeof(net));
					bcopy(idremote + ISAKMP_ID_DATA_OFF +
					    16, &mask, sizeof(mask));

					for (i = 0; i < 16; i++)
						net.s6_addr[i] &=
						    mask.s6_addr[i];

					my_inet_ntop6((unsigned char *)&net,
					    remote_filter_addr_lower,
					sizeof remote_filter_addr_lower - 1);

					for (i = 0; i < 16; i++)
						net.s6_addr[i] |=
						    ~mask.s6_addr[i];

					my_inet_ntop6((unsigned char *)&net,
					    remote_filter_addr_upper,
					sizeof remote_filter_addr_upper - 1);

					len = strlen(remote_filter_addr_upper)
						+ strlen(remote_filter_addr_lower) + 2;
					remote_filter = calloc(len,
					    sizeof(char));
					if (!remote_filter) {
						log_error("policy_callback: "
						    "calloc (%d, %lu) failed",
						    len,
						    (unsigned long)sizeof(char));
						goto bad;
					}
					strlcpy(remote_filter,
					    remote_filter_addr_lower, len);
					strlcat(remote_filter, "-", len);
					strlcat(remote_filter,
					    remote_filter_addr_upper, len);
					break;
				}

			case IPSEC_ID_FQDN:
				remote_filter_type = "FQDN";
				remote_filter = malloc(idremotesz -
				    ISAKMP_ID_DATA_OFF + 1);
				if (!remote_filter) {
					log_error("policy_callback: "
					    "malloc (%lu) failed",
					    (unsigned long)idremotesz -
					    ISAKMP_ID_DATA_OFF + 1);
					goto bad;
				}
				memcpy(remote_filter,
				    idremote + ISAKMP_ID_DATA_OFF,
				    idremotesz - ISAKMP_ID_DATA_OFF);
				remote_filter[idremotesz - ISAKMP_ID_DATA_OFF]
				    = '\0';
				break;

			case IPSEC_ID_USER_FQDN:
				remote_filter_type = "User FQDN";
				remote_filter = malloc(idremotesz -
				    ISAKMP_ID_DATA_OFF + 1);
				if (!remote_filter) {
					log_error("policy_callback: "
					    "malloc (%lu) failed",
					    (unsigned long)idremotesz -
					    ISAKMP_ID_DATA_OFF + 1);
					goto bad;
				}
				memcpy(remote_filter,
				    idremote + ISAKMP_ID_DATA_OFF,
				    idremotesz - ISAKMP_ID_DATA_OFF);
				remote_filter[idremotesz - ISAKMP_ID_DATA_OFF]
				    = '\0';
				break;

			case IPSEC_ID_DER_ASN1_DN:
				remote_filter_type = "ASN1 DN";

				remote_filter = x509_DN_string(idremote +
				    ISAKMP_ID_DATA_OFF,
				    idremotesz - ISAKMP_ID_DATA_OFF);
				if (!remote_filter) {
					LOG_DBG((LOG_POLICY, 50,
					    "policy_callback: "
					    "failed to decode name"));
					goto bad;
				}
				break;

			case IPSEC_ID_DER_ASN1_GN:	/* XXX -- not sure
							 * what's in this.  */
				remote_filter_type = "ASN1 GN";
				break;

			case IPSEC_ID_KEY_ID:
				remote_filter_type = "Key ID";
				remote_filter
					= calloc(2 * (idremotesz -
					    ISAKMP_ID_DATA_OFF) + 1,
					    sizeof(char));
				if (!remote_filter) {
					log_error("policy_callback: "
					    "calloc (%lu, %lu) failed",
					    2 * ((unsigned long)idremotesz -
						ISAKMP_ID_DATA_OFF) + 1,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				/*
				 * Does it contain any non-printable
				 * characters ?
				 */
				for (i = 0;
				    i < idremotesz - ISAKMP_ID_DATA_OFF; i++)
					if (!isprint((unsigned char)*(idremote +
					    ISAKMP_ID_DATA_OFF + i)))
						break;
				if (i >= idremotesz - ISAKMP_ID_DATA_OFF) {
					memcpy(remote_filter,
					    idremote + ISAKMP_ID_DATA_OFF,
					    idremotesz - ISAKMP_ID_DATA_OFF);
					break;
				}
				/* Non-printable characters, convert to hex */
				for (i = 0;
				    i < idremotesz - ISAKMP_ID_DATA_OFF;
				    i++) {
					remote_filter[2 * i]
					    = hextab[*(idremote +
						ISAKMP_ID_DATA_OFF) >> 4];
					remote_filter[2 * i + 1]
					    = hextab[*(idremote +
						ISAKMP_ID_DATA_OFF) & 0xF];
				}
				break;

			default:
				log_print("policy_callback: "
				    "unknown Remote ID type %u",
				    GET_ISAKMP_ID_TYPE(idremote));
				goto bad;
			}

			switch (idremote[ISAKMP_GEN_SZ + 1]) {
			case IPPROTO_TCP:
				remote_filter_proto = "tcp";
				break;

			case IPPROTO_UDP:
				remote_filter_proto = "udp";
				break;

			case IPPROTO_ETHERIP:
				remote_filter_proto = "etherip";
				break;

			default:
				snprintf(remote_filter_proto_num,
				    sizeof remote_filter_proto_num, "%d",
				    idremote[ISAKMP_GEN_SZ + 1]);
				remote_filter_proto = remote_filter_proto_num;
				break;
			}

			snprintf(remote_filter_port, sizeof remote_filter_port,
			    "%u", decode_16(idremote + ISAKMP_GEN_SZ + 2));
		} else {
			policy_sa->transport->vtbl->get_dst(policy_sa->transport, &sin);
			switch (sin->sa_family) {
			case AF_INET:
				remote_filter_type = "IPv4 address";
				break;
			case AF_INET6:
				remote_filter_type = "IPv6 address";
				break;
			default:
				log_print("policy_callback: "
				    "unsupported protocol family %d",
				    sin->sa_family);
				goto bad;
			}
			if (sockaddr2text(sin, &addr, 1)) {
				log_error("policy_callback: "
				    "sockaddr2text failed");
				goto bad;
			}
			memcpy(remote_filter_addr_upper, addr,
			    sizeof remote_filter_addr_upper);
			memcpy(remote_filter_addr_lower, addr,
			    sizeof remote_filter_addr_lower);
			free(addr);
			remote_filter = strdup(remote_filter_addr_upper);
			if (!remote_filter) {
				log_error("policy_callback: "
				    "strdup (\"%s\") failed",
				    remote_filter_addr_upper);
				goto bad;
			}
		}

		if (idlocal) {
			switch (GET_ISAKMP_ID_TYPE(idlocal)) {
			case IPSEC_ID_IPV4_ADDR:
				local_filter_type = "IPv4 address";

				net = decode_32(idlocal + ISAKMP_ID_DATA_OFF);
				my_inet_ntop4(&net, local_filter_addr_upper,
				    sizeof local_filter_addr_upper - 1, 1);
				my_inet_ntop4(&net, local_filter_addr_lower,
				    sizeof local_filter_addr_upper - 1, 1);
				local_filter = strdup(local_filter_addr_upper);
				if (!local_filter) {
					log_error("policy_callback: "
					    "strdup (\"%s\") failed",
					    local_filter_addr_upper);
					goto bad;
				}
				break;

			case IPSEC_ID_IPV4_RANGE:
				local_filter_type = "IPv4 range";

				net = decode_32(idlocal + ISAKMP_ID_DATA_OFF);
				my_inet_ntop4(&net, local_filter_addr_lower,
				    sizeof local_filter_addr_lower - 1, 1);
				net = decode_32(idlocal + ISAKMP_ID_DATA_OFF +
				    4);
				my_inet_ntop4(&net, local_filter_addr_upper,
				    sizeof local_filter_addr_upper - 1, 1);
				len = strlen(local_filter_addr_upper)
					+ strlen(local_filter_addr_lower) + 2;
				local_filter = calloc(len, sizeof(char));
				if (!local_filter) {
					log_error("policy_callback: "
					    "calloc (%d, %lu) failed", len,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				strlcpy(local_filter, local_filter_addr_lower,
				    len);
				strlcat(local_filter, "-", len);
				strlcat(local_filter, local_filter_addr_upper,
				    len);
				break;

			case IPSEC_ID_IPV4_ADDR_SUBNET:
				local_filter_type = "IPv4 subnet";

				net = decode_32(idlocal + ISAKMP_ID_DATA_OFF);
				subnet = decode_32(idlocal +
				    ISAKMP_ID_DATA_OFF + 4);
				net &= subnet;
				my_inet_ntop4(&net, local_filter_addr_lower,
				    sizeof local_filter_addr_lower - 1, 1);
				net |= ~subnet;
				my_inet_ntop4(&net, local_filter_addr_upper,
				    sizeof local_filter_addr_upper - 1, 1);
				len = strlen(local_filter_addr_upper) +
				    strlen(local_filter_addr_lower) + 2;
				local_filter = calloc(len, sizeof(char));
				if (!local_filter) {
					log_error("policy_callback: "
					    "calloc (%d, %lu) failed", len,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				strlcpy(local_filter, local_filter_addr_lower,
				    len);
				strlcat(local_filter, "-", len);
				strlcat(local_filter, local_filter_addr_upper,
				    len);
				break;

			case IPSEC_ID_IPV6_ADDR:
				local_filter_type = "IPv6 address";
				my_inet_ntop6(idlocal + ISAKMP_ID_DATA_OFF,
				    local_filter_addr_upper,
				    sizeof local_filter_addr_upper - 1);
				strlcpy(local_filter_addr_lower,
				    local_filter_addr_upper,
				    sizeof local_filter_addr_lower);
				local_filter = strdup(local_filter_addr_upper);
				if (!local_filter) {
					log_error("policy_callback: "
					    "strdup (\"%s\") failed",
					    local_filter_addr_upper);
					goto bad;
				}
				break;

			case IPSEC_ID_IPV6_RANGE:
				local_filter_type = "IPv6 range";

				my_inet_ntop6(idlocal + ISAKMP_ID_DATA_OFF,
				    local_filter_addr_lower,
				    sizeof local_filter_addr_lower - 1);

				my_inet_ntop6(idlocal + ISAKMP_ID_DATA_OFF +
				    16, local_filter_addr_upper,
				    sizeof local_filter_addr_upper - 1);

				len = strlen(local_filter_addr_upper)
					+ strlen(local_filter_addr_lower) + 2;
				local_filter = calloc(len, sizeof(char));
				if (!local_filter) {
					log_error("policy_callback: "
					    "calloc (%d, %lu) failed", len,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				strlcpy(local_filter, local_filter_addr_lower,
				    len);
				strlcat(local_filter, "-", len);
				strlcat(local_filter, local_filter_addr_upper,
				    len);
				break;

			case IPSEC_ID_IPV6_ADDR_SUBNET:
				{
					struct in6_addr net, mask;

					local_filter_type = "IPv6 subnet";

					bcopy(idlocal + ISAKMP_ID_DATA_OFF,
					    &net, sizeof(net));
					bcopy(idlocal + ISAKMP_ID_DATA_OFF +
					    16, &mask, sizeof(mask));

					for (i = 0; i < 16; i++)
						net.s6_addr[i] &=
						    mask.s6_addr[i];

					my_inet_ntop6((unsigned char *)&net,
					    local_filter_addr_lower,
					sizeof local_filter_addr_lower - 1);

					for (i = 0; i < 16; i++)
						net.s6_addr[i] |=
						    ~mask.s6_addr[i];

					my_inet_ntop6((unsigned char *)&net,
					    local_filter_addr_upper,
					    sizeof local_filter_addr_upper -
					    1);

					len = strlen(local_filter_addr_upper)
					    + strlen(local_filter_addr_lower)
					    + 2;
					local_filter = calloc(len,
					    sizeof(char));
					if (!local_filter) {
						log_error("policy_callback: "
						    "calloc (%d, %lu) failed",
						    len,
						    (unsigned long)sizeof(char));
						goto bad;
					}
					strlcpy(local_filter,
					    local_filter_addr_lower, len);
					strlcat(local_filter, "-", len);
					strlcat(local_filter,
					    local_filter_addr_upper, len);
					break;
				}

			case IPSEC_ID_FQDN:
				local_filter_type = "FQDN";
				local_filter = malloc(idlocalsz -
				    ISAKMP_ID_DATA_OFF + 1);
				if (!local_filter) {
					log_error("policy_callback: "
					    "malloc (%lu) failed",
					    (unsigned long)idlocalsz -
					    ISAKMP_ID_DATA_OFF + 1);
					goto bad;
				}
				memcpy(local_filter,
				    idlocal + ISAKMP_ID_DATA_OFF,
				    idlocalsz - ISAKMP_ID_DATA_OFF);
				local_filter[idlocalsz - ISAKMP_ID_DATA_OFF] = '\0';
				break;

			case IPSEC_ID_USER_FQDN:
				local_filter_type = "User FQDN";
				local_filter = malloc(idlocalsz -
				    ISAKMP_ID_DATA_OFF + 1);
				if (!local_filter) {
					log_error("policy_callback: "
					    "malloc (%lu) failed",
					    (unsigned long)idlocalsz -
					    ISAKMP_ID_DATA_OFF + 1);
					goto bad;
				}
				memcpy(local_filter,
				    idlocal + ISAKMP_ID_DATA_OFF,
				    idlocalsz - ISAKMP_ID_DATA_OFF);
				local_filter[idlocalsz - ISAKMP_ID_DATA_OFF] = '\0';
				break;

			case IPSEC_ID_DER_ASN1_DN:
				local_filter_type = "ASN1 DN";

				local_filter = x509_DN_string(idlocal +
				    ISAKMP_ID_DATA_OFF,
				    idlocalsz - ISAKMP_ID_DATA_OFF);
				if (!local_filter) {
					LOG_DBG((LOG_POLICY, 50,
					    "policy_callback: failed to decode"
					    " name"));
					goto bad;
				}
				break;

			case IPSEC_ID_DER_ASN1_GN:
				/* XXX -- not sure what's in this.  */
				local_filter_type = "ASN1 GN";
				break;

			case IPSEC_ID_KEY_ID:
				local_filter_type = "Key ID";
				local_filter = calloc(2 * (idlocalsz -
				    ISAKMP_ID_DATA_OFF) + 1,
				    sizeof(char));
				if (!local_filter) {
					log_error("policy_callback: "
					    "calloc (%lu, %lu) failed",
					    2 * ((unsigned long)idlocalsz -
					    ISAKMP_ID_DATA_OFF) + 1,
					    (unsigned long)sizeof(char));
					goto bad;
				}
				/*
				 * Does it contain any non-printable
				 * characters ?
				 */
				for (i = 0;
				    i < idlocalsz - ISAKMP_ID_DATA_OFF; i++)
					if (!isprint((unsigned char)*(idlocal +
					    ISAKMP_ID_DATA_OFF + i)))
						break;
				if (i >= idlocalsz - ISAKMP_ID_DATA_OFF) {
					memcpy(local_filter, idlocal +
					    ISAKMP_ID_DATA_OFF,
					    idlocalsz - ISAKMP_ID_DATA_OFF);
					break;
				}
				/* Non-printable characters, convert to hex */
				for (i = 0;
				    i < idlocalsz - ISAKMP_ID_DATA_OFF; i++) {
					local_filter[2 * i] =
					    hextab[*(idlocal +
					    ISAKMP_ID_DATA_OFF) >> 4];
					local_filter[2 * i + 1] =
					    hextab[*(idlocal +
					    ISAKMP_ID_DATA_OFF) & 0xF];
				}
				break;

			default:
				log_print("policy_callback: "
				    "unknown Local ID type %u",
				    GET_ISAKMP_ID_TYPE(idlocal));
				goto bad;
			}

			switch (idlocal[ISAKMP_GEN_SZ + 1]) {
			case IPPROTO_TCP:
				local_filter_proto = "tcp";
				break;

			case IPPROTO_UDP:
				local_filter_proto = "udp";
				break;

			case IPPROTO_ETHERIP:
				local_filter_proto = "etherip";
				break;

			default:
				snprintf(local_filter_proto_num,
				    sizeof local_filter_proto_num,
				    "%d", idlocal[ISAKMP_GEN_SZ + 1]);
				local_filter_proto = local_filter_proto_num;
				break;
			}

			snprintf(local_filter_port, sizeof local_filter_port,
			    "%u", decode_16(idlocal + ISAKMP_GEN_SZ + 2));
		} else {
			policy_sa->transport->vtbl->get_src(policy_sa->transport,
			    (struct sockaddr **)&sin);
			switch (sin->sa_family) {
			case AF_INET:
				local_filter_type = "IPv4 address";
				break;
			case AF_INET6:
				local_filter_type = "IPv6 address";
				break;
			default:
				log_print("policy_callback: "
				    "unsupported protocol family %d",
				    sin->sa_family);
				goto bad;
			}

			if (sockaddr2text(sin, &addr, 1)) {
				log_error("policy_callback: "
				    "sockaddr2text failed");
				goto bad;
			}
			memcpy(local_filter_addr_upper, addr,
			    sizeof local_filter_addr_upper);
			memcpy(local_filter_addr_lower, addr,
			    sizeof local_filter_addr_lower);
			free(addr);
			local_filter = strdup(local_filter_addr_upper);
			if (!local_filter) {
				log_error("policy_callback: "
				    "strdup (\"%s\") failed",
				    local_filter_addr_upper);
				goto bad;
			}
		}

		LOG_DBG((LOG_POLICY, 80,
		    "Policy context (action attributes):"));
		LOG_DBG((LOG_POLICY, 80, "esp_present == %s", esp_present));
		LOG_DBG((LOG_POLICY, 80, "ah_present == %s", ah_present));
		LOG_DBG((LOG_POLICY, 80, "comp_present == %s", comp_present));
		LOG_DBG((LOG_POLICY, 80, "ah_hash_alg == %s", ah_hash_alg));
		LOG_DBG((LOG_POLICY, 80, "esp_enc_alg == %s", esp_enc_alg));
		LOG_DBG((LOG_POLICY, 80, "comp_alg == %s", comp_alg));
		LOG_DBG((LOG_POLICY, 80, "ah_auth_alg == %s", ah_auth_alg));
		LOG_DBG((LOG_POLICY, 80, "esp_auth_alg == %s", esp_auth_alg));
		LOG_DBG((LOG_POLICY, 80, "ah_life_seconds == %s",
		    ah_life_seconds));
		LOG_DBG((LOG_POLICY, 80, "ah_life_kbytes == %s",
		    ah_life_kbytes));
		LOG_DBG((LOG_POLICY, 80, "esp_life_seconds == %s",
		    esp_life_seconds));
		LOG_DBG((LOG_POLICY, 80, "esp_life_kbytes == %s",
		    esp_life_kbytes));
		LOG_DBG((LOG_POLICY, 80, "comp_life_seconds == %s",
		    comp_life_seconds));
		LOG_DBG((LOG_POLICY, 80, "comp_life_kbytes == %s",
		    comp_life_kbytes));
		LOG_DBG((LOG_POLICY, 80, "ah_encapsulation == %s",
		    ah_encapsulation));
		LOG_DBG((LOG_POLICY, 80, "esp_encapsulation == %s",
		    esp_encapsulation));
		LOG_DBG((LOG_POLICY, 80, "comp_encapsulation == %s",
		    comp_encapsulation));
		LOG_DBG((LOG_POLICY, 80, "comp_dict_size == %s",
		    comp_dict_size));
		LOG_DBG((LOG_POLICY, 80, "comp_private_alg == %s",
		    comp_private_alg));
		LOG_DBG((LOG_POLICY, 80, "ah_key_length == %s",
		    ah_key_length));
		LOG_DBG((LOG_POLICY, 80, "ah_key_rounds == %s",
		    ah_key_rounds));
		LOG_DBG((LOG_POLICY, 80, "esp_key_length == %s",
		    esp_key_length));
		LOG_DBG((LOG_POLICY, 80, "esp_key_rounds == %s",
		    esp_key_rounds));
		LOG_DBG((LOG_POLICY, 80, "ah_group_desc == %s",
		    ah_group_desc));
		LOG_DBG((LOG_POLICY, 80, "esp_group_desc == %s",
		    esp_group_desc));
		LOG_DBG((LOG_POLICY, 80, "comp_group_desc == %s",
		    comp_group_desc));
		LOG_DBG((LOG_POLICY, 80, "ah_ecn == %s", ah_ecn));
		LOG_DBG((LOG_POLICY, 80, "esp_ecn == %s", esp_ecn));
		LOG_DBG((LOG_POLICY, 80, "comp_ecn == %s", comp_ecn));
		LOG_DBG((LOG_POLICY, 80, "remote_filter_type == %s",
		    remote_filter_type));
		LOG_DBG((LOG_POLICY, 80, "remote_filter_addr_upper == %s",
		    remote_filter_addr_upper));
		LOG_DBG((LOG_POLICY, 80, "remote_filter_addr_lower == %s",
		    remote_filter_addr_lower));
		LOG_DBG((LOG_POLICY, 80, "remote_filter == %s",
		    (remote_filter ? remote_filter : "")));
		LOG_DBG((LOG_POLICY, 80, "remote_filter_port == %s",
		    remote_filter_port));
		LOG_DBG((LOG_POLICY, 80, "remote_filter_proto == %s",
		    remote_filter_proto));
		LOG_DBG((LOG_POLICY, 80, "local_filter_type == %s",
		    local_filter_type));
		LOG_DBG((LOG_POLICY, 80, "local_filter_addr_upper == %s",
		    local_filter_addr_upper));
		LOG_DBG((LOG_POLICY, 80, "local_filter_addr_lower == %s",
		    local_filter_addr_lower));
		LOG_DBG((LOG_POLICY, 80, "local_filter == %s",
		    (local_filter ? local_filter : "")));
		LOG_DBG((LOG_POLICY, 80, "local_filter_port == %s",
		    local_filter_port));
		LOG_DBG((LOG_POLICY, 80, "local_filter_proto == %s",
		    local_filter_proto));
		LOG_DBG((LOG_POLICY, 80, "remote_id_type == %s",
		    remote_id_type));
		LOG_DBG((LOG_POLICY, 80, "remote_id_addr_upper == %s",
		    remote_id_addr_upper));
		LOG_DBG((LOG_POLICY, 80, "remote_id_addr_lower == %s",
		    remote_id_addr_lower));
		LOG_DBG((LOG_POLICY, 80, "remote_id == %s",
		    (remote_id ? remote_id : "")));
		LOG_DBG((LOG_POLICY, 80, "remote_id_port == %s",
		    remote_id_port));
		LOG_DBG((LOG_POLICY, 80, "remote_id_proto == %s",
		    remote_id_proto));
		LOG_DBG((LOG_POLICY, 80, "remote_negotiation_address == %s",
		    remote_ike_address));
		LOG_DBG((LOG_POLICY, 80, "local_negotiation_address == %s",
		    local_ike_address));
		LOG_DBG((LOG_POLICY, 80, "pfs == %s", pfs));
		LOG_DBG((LOG_POLICY, 80, "initiator == %s", initiator));
		LOG_DBG((LOG_POLICY, 80, "phase1_group_desc == %s",
		    phase1_group));

		/* Unset dirty now.  */
		dirty = 0;
	}
	if (strcmp(name, "phase_1") == 0)
		return phase_1;

	if (strcmp(name, "GMTTimeOfDay") == 0) {
		struct tm *tm;
		tt = time(NULL);
		if ((tm = gmtime(&tt)) == NULL) {
			log_error("policy_callback: invalid time %lld", tt);
			goto bad;
		}
		strftime(mytimeofday, 14, "%Y%m%d%H%M%S", tm);
		return mytimeofday;
	}
	if (strcmp(name, "LocalTimeOfDay") == 0) {
		struct tm *tm;
		tt = time(NULL);
		if ((tm = localtime(&tt)) == NULL) {
			log_error("policy_callback: invalid time %lld", tt);
			goto bad;
		}
		strftime(mytimeofday, 14, "%Y%m%d%H%M%S", tm);
		return mytimeofday;
	}
	if (strcmp(name, "initiator") == 0)
		return initiator;

	if (strcmp(name, "pfs") == 0)
		return pfs;

	if (strcmp(name, "app_domain") == 0)
		return "IPsec policy";

	if (strcmp(name, "doi") == 0)
		return "ipsec";

	if (strcmp(name, "esp_present") == 0)
		return esp_present;

	if (strcmp(name, "ah_present") == 0)
		return ah_present;

	if (strcmp(name, "comp_present") == 0)
		return comp_present;

	if (strcmp(name, "ah_hash_alg") == 0)
		return ah_hash_alg;

	if (strcmp(name, "ah_auth_alg") == 0)
		return ah_auth_alg;

	if (strcmp(name, "esp_auth_alg") == 0)
		return esp_auth_alg;

	if (strcmp(name, "esp_enc_alg") == 0)
		return esp_enc_alg;

	if (strcmp(name, "comp_alg") == 0)
		return comp_alg;

	if (strcmp(name, "ah_life_kbytes") == 0)
		return ah_life_kbytes;

	if (strcmp(name, "ah_life_seconds") == 0)
		return ah_life_seconds;

	if (strcmp(name, "esp_life_kbytes") == 0)
		return esp_life_kbytes;

	if (strcmp(name, "esp_life_seconds") == 0)
		return esp_life_seconds;

	if (strcmp(name, "comp_life_kbytes") == 0)
		return comp_life_kbytes;

	if (strcmp(name, "comp_life_seconds") == 0)
		return comp_life_seconds;

	if (strcmp(name, "ah_encapsulation") == 0)
		return ah_encapsulation;

	if (strcmp(name, "esp_encapsulation") == 0)
		return esp_encapsulation;

	if (strcmp(name, "comp_encapsulation") == 0)
		return comp_encapsulation;

	if (strcmp(name, "ah_key_length") == 0)
		return ah_key_length;

	if (strcmp(name, "ah_key_rounds") == 0)
		return ah_key_rounds;

	if (strcmp(name, "esp_key_length") == 0)
		return esp_key_length;

	if (strcmp(name, "esp_key_rounds") == 0)
		return esp_key_rounds;

	if (strcmp(name, "comp_dict_size") == 0)
		return comp_dict_size;

	if (strcmp(name, "comp_private_alg") == 0)
		return comp_private_alg;

	if (strcmp(name, "remote_filter_type") == 0)
		return remote_filter_type;

	if (strcmp(name, "remote_filter") == 0)
		return (remote_filter ? remote_filter : "");

	if (strcmp(name, "remote_filter_addr_upper") == 0)
		return remote_filter_addr_upper;

	if (strcmp(name, "remote_filter_addr_lower") == 0)
		return remote_filter_addr_lower;

	if (strcmp(name, "remote_filter_port") == 0)
		return remote_filter_port;

	if (strcmp(name, "remote_filter_proto") == 0)
		return remote_filter_proto;

	if (strcmp(name, "local_filter_type") == 0)
		return local_filter_type;

	if (strcmp(name, "local_filter") == 0)
		return (local_filter ? local_filter : "");

	if (strcmp(name, "local_filter_addr_upper") == 0)
		return local_filter_addr_upper;

	if (strcmp(name, "local_filter_addr_lower") == 0)
		return local_filter_addr_lower;

	if (strcmp(name, "local_filter_port") == 0)
		return local_filter_port;

	if (strcmp(name, "local_filter_proto") == 0)
		return local_filter_proto;

	if (strcmp(name, "remote_ike_address") == 0)
		return remote_ike_address;

	if (strcmp(name, "remote_negotiation_address") == 0)
		return remote_ike_address;

	if (strcmp(name, "local_ike_address") == 0)
		return local_ike_address;

	if (strcmp(name, "local_negotiation_address") == 0)
		return local_ike_address;

	if (strcmp(name, "remote_id_type") == 0)
		return remote_id_type;

	if (strcmp(name, "remote_id") == 0)
		return (remote_id ? remote_id : "");

	if (strcmp(name, "remote_id_addr_upper") == 0)
		return remote_id_addr_upper;

	if (strcmp(name, "remote_id_addr_lower") == 0)
		return remote_id_addr_lower;

	if (strcmp(name, "remote_id_port") == 0)
		return remote_id_port;

	if (strcmp(name, "remote_id_proto") == 0)
		return remote_id_proto;

	if (strcmp(name, "phase1_group_desc") == 0)
		return phase1_group;

	if (strcmp(name, "esp_group_desc") == 0)
		return esp_group_desc;

	if (strcmp(name, "ah_group_desc") == 0)
		return ah_group_desc;

	if (strcmp(name, "comp_group_desc") == 0)
		return comp_group_desc;

	if (strcmp(name, "comp_ecn") == 0)
		return comp_ecn;

	if (strcmp(name, "ah_ecn") == 0)
		return ah_ecn;

	if (strcmp(name, "esp_ecn") == 0)
		return esp_ecn;

	return "";

bad:
	policy_callback(KEYNOTE_CALLBACK_INITIALIZE);
	return "";
}

void
policy_init(void)
{
	char           *ptr, *policy_file;
	char          **asserts;
	size_t          sz, len;
	int             fd, i;

	LOG_DBG((LOG_POLICY, 30, "policy_init: initializing"));

	/* Do we want to use the policy modules?  */
	if (ignore_policy ||
	    strncmp("yes", conf_get_str("General", "Use-Keynote"), 3))
		return;

	/* Get policy file from configuration.  */
	policy_file = conf_get_str("General", "Policy-file");
	if (!policy_file)
		policy_file = CONF_DFLT_POLICY_FILE;

	/* Open policy file.  */
	fd = monitor_open(policy_file, O_RDONLY, 0);
	if (fd == -1)
		log_fatal("policy_init: open (\"%s\", O_RDONLY) failed",
		    policy_file);

	/* Check file modes and collect file size */
	if (check_file_secrecy_fd(fd, policy_file, &sz)) {
		close(fd);
		log_fatal("policy_init: cannot read %s", policy_file);
	}

	/* Allocate memory to keep policies.  */
	ptr = calloc(sz + 1, sizeof(char));
	if (!ptr)
		log_fatal("policy_init: calloc (%lu, %lu) failed",
		    (unsigned long)sz + 1, (unsigned long)sizeof(char));

	/* Just in case there are short reads...  */
	for (len = 0; len < sz; len += i) {
		i = read(fd, ptr + len, sz - len);
		if (i == -1)
			log_fatal("policy_init: read (%d, %p, %lu) failed", fd,
			    ptr + len, (unsigned long)(sz - len));
	}

	/* We're done with this.  */
	close(fd);

	/* Parse buffer, break up into individual policies.  */
	asserts = kn_read_asserts(ptr, sz, &i);

	/* Begone!  */
	free(ptr);

	if (asserts == (char **)NULL)
		log_print("policy_init: all policies flushed");

	/* Cleanup */
	if (policy_asserts) {
		for (fd = 0; fd < policy_asserts_num; fd++)
			if (policy_asserts)
				free(policy_asserts[fd]);

		free(policy_asserts);
	}
	policy_asserts = asserts;
	policy_asserts_num = i;
}

/* Nothing needed for initialization */
int
keynote_cert_init(void)
{
	return 1;
}

/* Just copy and return.  */
void           *
keynote_cert_get(u_int8_t *data, u_int32_t len)
{
	char	*foo = malloc(len + 1);

	if (foo == NULL)
		return NULL;

	memcpy(foo, data, len);
	foo[len] = '\0';
	return foo;
}

/*
 * We just verify the signature on the credentials.
 * On signature failure, just drop the whole payload.
 */
int
keynote_cert_validate(void *scert)
{
	char	**foo;
	int	  num, i;

	if (scert == NULL)
		return 0;

	foo = kn_read_asserts((char *)scert, strlen((char *)scert), &num);
	if (foo == NULL)
		return 0;

	for (i = 0; i < num; i++) {
		if (kn_verify_assertion(scert, strlen((char *)scert))
		    != SIGRESULT_TRUE) {
			for (; i < num; i++)
				free(foo[i]);
			free(foo);
			return 0;
		}
		free(foo[i]);
	}

	free(foo);
	return 1;
}

/* Add received credentials.  */
int
keynote_cert_insert(int sid, void *scert)
{
	char	**foo;
	int	  num;

	if (scert == NULL)
		return 0;

	foo = kn_read_asserts((char *)scert, strlen((char *)scert), &num);
	if (foo == NULL)
		return 0;

	while (num--)
		kn_add_assertion(sid, foo[num], strlen(foo[num]), 0);

	return 1;
}

/* Just regular memory free.  */
void
keynote_cert_free(void *cert)
{
	free(cert);
}

/* Verify that the key given to us is valid.  */
int
keynote_certreq_validate(u_int8_t *data, u_int32_t len)
{
	struct keynote_deckey dc;
	int	 err = 1;
	char	*dat;

	dat = calloc(len + 1, sizeof(char));
	if (!dat) {
		log_error("keynote_certreq_validate: calloc (%d, %lu) failed",
		    len + 1, (unsigned long)sizeof(char));
		return 0;
	}
	memcpy(dat, data, len);

	if (kn_decode_key(&dc, dat, KEYNOTE_PUBLIC_KEY) != 0)
		err = 0;
	else
		kn_free_key(&dc);

	free(dat);

	return err;
}

/* Beats me what we should be doing with this.  */
int
keynote_certreq_decode(void **pdata, u_int8_t *data, u_int32_t len)
{
	/* XXX */
	return 0;
}

void
keynote_free_aca(void *blob)
{
	/* XXX */
}

int
keynote_cert_obtain(u_int8_t *id, size_t id_len, void *data, u_int8_t **cert,
    u_int32_t *certlen)
{
	char           *dirname, *file, *addr_str;
	struct stat     sb;
	size_t          size;
	int             idtype, fd, len;

	if (!id) {
		log_print("keynote_cert_obtain: ID is missing");
		return 0;
	}
	/* Get type of ID.  */
	idtype = id[0];
	id += ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ;
	id_len -= ISAKMP_ID_DATA_OFF - ISAKMP_GEN_SZ;

	dirname = conf_get_str("KeyNote", "Credential-directory");
	if (!dirname) {
		LOG_DBG((LOG_POLICY, 30,
			 "keynote_cert_obtain: no Credential-directory"));
		return 0;
	}
	len = strlen(dirname) + strlen(CREDENTIAL_FILE) + 3;

	switch (idtype) {
	case IPSEC_ID_IPV4_ADDR:
	case IPSEC_ID_IPV6_ADDR:
		util_ntoa(&addr_str, idtype == IPSEC_ID_IPV4_ADDR ?
		    AF_INET : AF_INET6, id);
		if (addr_str == 0)
			return 0;

		if (asprintf(&file, "%s/%s/%s", dirname,
		    addr_str, CREDENTIAL_FILE) == -1) {
			log_error("keynote_cert_obtain: failed to allocate "
			    "%lu bytes", (unsigned long)len +
			    strlen(addr_str));
			free(addr_str);
			return 0;
		}
		free(addr_str);
		break;

	case IPSEC_ID_FQDN:
	case IPSEC_ID_USER_FQDN:
		file = calloc(len + id_len, sizeof(char));
		if (file == NULL) {
			log_error("keynote_cert_obtain: "
			    "failed to allocate %lu bytes",
			    (unsigned long)len + id_len);
			return 0;
		}
		snprintf(file, len + id_len, "%s/", dirname);
		memcpy(file + strlen(dirname) + 1, id, id_len);
		snprintf(file + strlen(dirname) + 1 + id_len,
		    len - strlen(dirname) - 1, "/%s", CREDENTIAL_FILE);
		break;

	default:
		return 0;
	}

	fd = monitor_open(file, O_RDONLY, 0);
	if (fd < 0) {
		LOG_DBG((LOG_POLICY, 30, "keynote_cert_obtain: "
		    "failed to open \"%s\"", file));
		free(file);
		return 0;
	}

	if (fstat(fd, &sb) == -1) {
		LOG_DBG((LOG_POLICY, 30, "keynote_cert_obtain: "
		    "failed to stat \"%s\"", file));
		free(file);
		close(fd);
		return 0;
	}
	size = (size_t)sb.st_size;

	*cert = calloc(size + 1, sizeof(char));
	if (*cert == NULL) {
		log_error("keynote_cert_obtain: failed to allocate %lu bytes",
		    (unsigned long)size);
		free(file);
		close(fd);
		return 0;
	}

	if (read(fd, *cert, size) != (int)size) {
		LOG_DBG((LOG_POLICY, 30, "keynote_cert_obtain: "
		    "failed to read %lu bytes from \"%s\"",
		    (unsigned long)size, file));
		free(cert);
		cert = NULL;
		free(file);
		close(fd);
		return 0;
	}
	close(fd);
	free(file);
	*certlen = size;
	return 1;
}

/* This should never be called.  */
int
keynote_cert_get_subjects(void *scert, int *n, u_int8_t ***id,
    u_int32_t **id_len)
{
	return 0;
}

/* Get the authorizer key.  */
int
keynote_cert_get_key(void *scert, void *keyp)
{
	struct keynote_keylist *kl;
	int             sid, kid, num;
	char          **foo;

	foo = kn_read_asserts((char *)scert, strlen((char *)scert), &num);
	if (foo == NULL || num == 0) {
		log_print("keynote_cert_get_key: "
		    "failed to decompose credentials");
		return 0;
	}
	kid = kn_init();
	if (kid == -1) {
		log_print("keynote_cert_get_key: "
		    "failed to initialize new policy session");
		while (num--)
			free(foo[num]);
		free(foo);
		return 0;
	}
	sid = kn_add_assertion(kid, foo[num - 1], strlen(foo[num - 1]), 0);
	while (num--)
		free(foo[num]);
	free(foo);

	if (sid == -1) {
		log_print("keynote_cert_get_key: failed to add assertion");
		kn_close(kid);
		return 0;
	}
	*(RSA **)keyp = NULL;

	kl = kn_get_licensees(kid, sid);
	while (kl) {
		if (kl->key_alg == KEYNOTE_ALGORITHM_RSA ||
		    kl->key_alg == KEYNOTE_ALGORITHM_X509) {
			*(RSA **)keyp = RSAPublicKey_dup(kl->key_key);
			break;
		}
		kl = kl->key_next;
	}

	kn_remove_assertion(kid, sid);
	kn_close(kid);
	return *(RSA **)keyp == NULL ? 0 : 1;
}

void *
keynote_cert_dup(void *cert)
{
	return strdup((char *)cert);
}

void
keynote_serialize(void *cert, u_int8_t **data, u_int32_t *datalen)
{
	*datalen = strlen((char *)cert) + 1;
	*data = (u_int8_t *)strdup(cert);	/* i.e an extra character at
						 * the end... */
	if (*data == NULL)
		log_error("keynote_serialize: malloc (%d) failed", *datalen);
}

/* From cert to printable */
char *
keynote_printable(void *cert)
{
	return strdup((char *)cert);
}

/* From printable to cert */
void *
keynote_from_printable(char *cert)
{
	return strdup(cert);
}

/* Number of CAs we trust (currently this is x509 only) */
int
keynote_ca_count(void)
{
	return 0;
}
