/*	$OpenBSD: ikev2.h,v 1.36 2025/04/30 03:51:42 tb Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
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

#ifndef IKED_IKEV2_H
#define IKED_IKEV2_H

#define IKEV2_VERSION		0x20	/* IKE version 2.0 */
#define IKEV1_VERSION		0x10	/* IKE version 1.0 */

#define IKEV2_KEYPAD		"Key Pad for IKEv2"	/* don't change! */

/*
 * IKEv2 pseudo states
 */

#define IKEV2_STATE_INIT		0	/* new IKE SA */
#define IKEV2_STATE_COOKIE		1	/* cookie requested */
#define IKEV2_STATE_SA_INIT		2	/* init IKE SA */
#define IKEV2_STATE_EAP			3	/* EAP requested */
#define IKEV2_STATE_EAP_SUCCESS		4	/* EAP succeeded */
#define IKEV2_STATE_AUTH_REQUEST	5	/* auth received */
#define IKEV2_STATE_AUTH_SUCCESS	6	/* authenticated */
#define IKEV2_STATE_VALID		7	/* authenticated AND validated certs */
#define IKEV2_STATE_EAP_VALID		8	/* EAP validated */
#define IKEV2_STATE_ESTABLISHED		9	/* active IKE SA */
#define IKEV2_STATE_CLOSING		10	/* expect delete for this SA */
#define IKEV2_STATE_CLOSED		11	/* delete this SA */

extern struct iked_constmap ikev2_state_map[];

/*
 * "IKEv2 Parameters" based on the official RFC-based assignments by IANA
 * (http://www.iana.org/assignments/ikev2-parameters/ikev2-parameters.txt)
 */

/*
 * IKEv2 definitions of the IKE header
 */

/* IKEv2 exchange types */
#define IKEV2_EXCHANGE_IKE_SA_INIT		34	/* Initial Exchange */
#define IKEV2_EXCHANGE_IKE_AUTH			35	/* Authentication */
#define IKEV2_EXCHANGE_CREATE_CHILD_SA		36	/* Create Child SA */
#define IKEV2_EXCHANGE_INFORMATIONAL		37	/* Informational */
#define IKEV2_EXCHANGE_IKE_SESSION_RESUME	38	/* RFC5723 */

extern struct iked_constmap ikev2_exchange_map[];

/* IKEv2 message flags */
#define IKEV2_FLAG_INITIATOR		0x08	/* Sent by the initiator */
#define IKEV2_FLAG_OLDVERSION		0x10	/* Supports a higher IKE version */
#define IKEV2_FLAG_RESPONSE		0x20	/* Message is a response */

extern struct iked_constmap ikev2_flag_map[];

/*
 * IKEv2 payloads
 */

struct ikev2_payload {
	uint8_t		 pld_nextpayload;	/* Next payload type */
	uint8_t		 pld_reserved;		/* Contains the critical bit */
	uint16_t	 pld_length;		/* Payload length with header */
} __packed;

struct ikev2_frag_payload {
	uint16_t	 frag_num;		/* current fragment message number */
	uint16_t	 frag_total;		/* total number of fragment messages */
} __packed;

#define IKEV2_CRITICAL_PAYLOAD	0x01	/* First bit in the reserved field */

/* IKEv2 payload types */
#define IKEV2_PAYLOAD_NONE	0	/* No payload */
#define IKEV2_PAYLOAD_SA	33	/* Security Association */
#define IKEV2_PAYLOAD_KE	34	/* Key Exchange */
#define IKEV2_PAYLOAD_IDi	35	/* Identification - Initiator */
#define IKEV2_PAYLOAD_IDr	36	/* Identification - Responder */
#define IKEV2_PAYLOAD_CERT	37	/* Certificate */
#define IKEV2_PAYLOAD_CERTREQ	38	/* Certificate Request */
#define IKEV2_PAYLOAD_AUTH	39	/* Authentication */
#define IKEV2_PAYLOAD_NONCE	40	/* Nonce */
#define IKEV2_PAYLOAD_NOTIFY	41	/* Notify */
#define IKEV2_PAYLOAD_DELETE	42	/* Delete */
#define IKEV2_PAYLOAD_VENDOR	43	/* Vendor ID */
#define IKEV2_PAYLOAD_TSi	44	/* Traffic Selector - Initiator */
#define IKEV2_PAYLOAD_TSr	45	/* Traffic Selector - Responder */
#define IKEV2_PAYLOAD_SK	46	/* Encrypted */
#define IKEV2_PAYLOAD_CP	47	/* Configuration Payload */
#define IKEV2_PAYLOAD_EAP	48	/* Extensible Authentication */
#define IKEV2_PAYLOAD_GSPM	49	/* RFC6467 Generic Secure Password */
#define IKEV2_PAYLOAD_SKF	53	/* RFC7383 Encrypted Fragment Payload */

extern struct iked_constmap ikev2_payload_map[];

/*
 * SA payload
 */

struct ikev2_sa_proposal {
	uint8_t		 sap_more;		/* Last proposal or more */
	uint8_t		 sap_reserved;		/* Must be set to zero */
	uint16_t	 sap_length;		/* Proposal length */
	uint8_t		 sap_proposalnr;	/* Proposal number */
	uint8_t		 sap_protoid;		/* Protocol Id */
	uint8_t		 sap_spisize;		/* SPI size */
	uint8_t		 sap_transforms;	/* Number of transforms */
	/* Followed by variable-length SPI */
	/* Followed by variable-length transforms */
} __packed;

#define IKEV2_SAP_LAST	0
#define IKEV2_SAP_MORE	2

#define IKEV2_SAPROTO_NONE		0	/* None */
#define IKEV2_SAPROTO_IKE		1	/* IKEv2 */
#define IKEV2_SAPROTO_AH		2	/* AH */
#define IKEV2_SAPROTO_ESP		3	/* ESP */
#define IKEV2_SAPROTO_FC_ESP_HEADER	4	/* RFC4595 */
#define IKEV2_SAPROTO_FC_CT_AUTH	5	/* RFC4595 */
#define IKEV2_SAPROTO_IPCOMP		204	/* private, should be 4 */

extern struct iked_constmap ikev2_saproto_map[];

struct ikev2_transform {
	uint8_t		xfrm_more;		/* Last transform or more */
	uint8_t		xfrm_reserved;		/* Must be set to zero */
	uint16_t	xfrm_length;		/* Transform length */
	uint8_t		xfrm_type;		/* Transform type */
	uint8_t		xfrm_reserved1;		/* Must be set to zero */
	uint16_t	xfrm_id;		/* Transform Id */
	/* Followed by variable-length transform attributes */
} __packed;

#define IKEV2_XFORM_LAST		0
#define IKEV2_XFORM_MORE		3

#define IKEV2_XFORMTYPE_ENCR		1	/* Encryption */
#define IKEV2_XFORMTYPE_PRF		2	/* Pseudo-Random Function */
#define IKEV2_XFORMTYPE_INTEGR		3	/* Integrity Algorithm */
#define IKEV2_XFORMTYPE_DH		4	/* Diffie-Hellman Group */
#define IKEV2_XFORMTYPE_ESN		5	/* Extended Sequence Numbers */
#define IKEV2_XFORMTYPE_MAX		6

extern struct iked_constmap ikev2_xformtype_map[];

#define IKEV2_XFORMENCR_NONE		0	/* None */
#define IKEV2_XFORMENCR_DES_IV64	1	/* RFC1827 */
#define IKEV2_XFORMENCR_DES		2	/* RFC2405 */
#define IKEV2_XFORMENCR_3DES		3	/* RFC2451 */
#define IKEV2_XFORMENCR_RC5		4	/* RFC2451 */
#define IKEV2_XFORMENCR_IDEA		5	/* RFC2451 */
#define IKEV2_XFORMENCR_CAST		6	/* RFC2451 */
#define IKEV2_XFORMENCR_BLOWFISH	7	/* RFC2451 */
#define IKEV2_XFORMENCR_3IDEA		8	/* RFC2451 */
#define IKEV2_XFORMENCR_DES_IV32	9	/* DESIV32 */
#define IKEV2_XFORMENCR_RC4		10	/* RFC2451 */
#define IKEV2_XFORMENCR_NULL		11	/* RFC2410 */
#define IKEV2_XFORMENCR_AES_CBC		12	/* RFC3602 */
#define IKEV2_XFORMENCR_AES_CTR		13	/* RFC3664 */
#define IKEV2_XFORMENCR_AES_CCM_8	14	/* RFC5282 */
#define IKEV2_XFORMENCR_AES_CCM_12	15	/* RFC5282 */
#define IKEV2_XFORMENCR_AES_CCM_16	16	/* RFC5282 */
#define IKEV2_XFORMENCR_AES_GCM_8	18	/* RFC5282 */
#define IKEV2_XFORMENCR_AES_GCM_12	19	/* RFC5282 */
#define IKEV2_XFORMENCR_AES_GCM_16	20	/* RFC5282 */
#define IKEV2_XFORMENCR_NULL_AES_GMAC	21	/* RFC4543 */
#define IKEV2_XFORMENCR_XTS_AES		22	/* IEEE P1619 */
#define IKEV2_XFORMENCR_CAMELLIA_CBC	23	/* RFC5529 */
#define IKEV2_XFORMENCR_CAMELLIA_CTR	24	/* RFC5529 */
#define IKEV2_XFORMENCR_CAMELLIA_CCM_8	25	/* RFC5529 */
#define IKEV2_XFORMENCR_CAMELLIA_CCM_12	26	/* RFC5529 */
#define IKEV2_XFORMENCR_CAMELLIA_CCM_16	27	/* RFC5529 */
#define IKEV2_XFORMENCR_CHACHA20_POLY1305 28	/* RFC7634 */

extern struct iked_constmap ikev2_xformencr_map[];

#define IKEV2_IPCOMP_OUI		1	/* UNSPECIFIED */
#define IKEV2_IPCOMP_DEFLATE		2	/* RFC2394 */
#define IKEV2_IPCOMP_LZS		3	/* RFC2395 */
#define IKEV2_IPCOMP_LZJH		4	/* RFC3051 */

extern struct iked_constmap ikev2_ipcomp_map[];

#define IKEV2_XFORMPRF_HMAC_MD5		1	/* RFC2104 */
#define IKEV2_XFORMPRF_HMAC_SHA1	2	/* RFC2104 */
#define IKEV2_XFORMPRF_HMAC_TIGER	3	/* RFC2104 */
#define IKEV2_XFORMPRF_AES128_XCBC	4	/* RFC3664 */
#define IKEV2_XFORMPRF_HMAC_SHA2_256	5	/* RFC4868 */
#define IKEV2_XFORMPRF_HMAC_SHA2_384	6	/* RFC4868 */
#define IKEV2_XFORMPRF_HMAC_SHA2_512	7	/* RFC4868 */
#define IKEV2_XFORMPRF_AES128_CMAC	8	/* RFC4615 */

extern struct iked_constmap ikev2_xformprf_map[];

#define IKEV2_XFORMAUTH_NONE		0	/* No Authentication */
#define IKEV2_XFORMAUTH_HMAC_MD5_96	1	/* RFC2403 */
#define IKEV2_XFORMAUTH_HMAC_SHA1_96	2	/* RFC2404 */
#define IKEV2_XFORMAUTH_DES_MAC		3	/* DES-MAC */
#define IKEV2_XFORMAUTH_KPDK_MD5	4	/* RFC1826 */
#define IKEV2_XFORMAUTH_AES_XCBC_96	5	/* RFC3566 */
#define IKEV2_XFORMAUTH_HMAC_MD5_128	6	/* RFC4595 */
#define IKEV2_XFORMAUTH_HMAC_SHA1_160	7	/* RFC4595 */
#define IKEV2_XFORMAUTH_AES_CMAC_96	8	/* RFC4494 */
#define IKEV2_XFORMAUTH_AES_128_GMAC	9	/* RFC4543 */
#define IKEV2_XFORMAUTH_AES_192_GMAC	10	/* RFC4543 */
#define IKEV2_XFORMAUTH_AES_256_GMAC	11	/* RFC4543 */
#define IKEV2_XFORMAUTH_HMAC_SHA2_256_128 12	/* RFC4868 */
#define IKEV2_XFORMAUTH_HMAC_SHA2_384_192 13	/* RFC4868 */
#define IKEV2_XFORMAUTH_HMAC_SHA2_512_256 14	/* RFC4868 */

/* Placeholders for AEAD ciphers (only used internally) */
#define IKEV2_XFORMAUTH_AES_GCM_8	2018	/* internal */
#define IKEV2_XFORMAUTH_AES_GCM_12	2019	/* internal */
#define IKEV2_XFORMAUTH_AES_GCM_16	2020	/* internal */

extern struct iked_constmap ikev2_xformauth_map[];

#define IKEV2_XFORMDH_NONE		0	/* No DH */
#define IKEV2_XFORMDH_MODP_768		1	/* DH Group 1 */
#define IKEV2_XFORMDH_MODP_1024		2	/* DH Group 2 */
#define IKEV2_XFORMDH_MODP_1536		5	/* DH Group 5 */
#define IKEV2_XFORMDH_MODP_2048		14	/* DH Group 14 */
#define IKEV2_XFORMDH_MODP_3072		15	/* DH Group 15 */
#define IKEV2_XFORMDH_MODP_4096		16	/* DH Group 16 */
#define IKEV2_XFORMDH_MODP_6144		17	/* DH Group 17 */
#define IKEV2_XFORMDH_MODP_8192		18	/* DH Group 18 */
#define IKEV2_XFORMDH_ECP_256		19	/* RFC5114 */
#define IKEV2_XFORMDH_ECP_384		20	/* RFC5114 */
#define IKEV2_XFORMDH_ECP_521		21	/* RFC5114 */
#define IKEV2_XFORMDH_ECP_224		26	/* RFC5114 */
#define IKEV2_XFORMDH_BRAINPOOL_P224R1	27	/* RFC6954 */
#define IKEV2_XFORMDH_BRAINPOOL_P256R1	28	/* RFC6954 */
#define IKEV2_XFORMDH_BRAINPOOL_P384R1	29	/* RFC6954 */
#define IKEV2_XFORMDH_BRAINPOOL_P512R1	30	/* RFC6954 */
#define IKEV2_XFORMDH_CURVE25519	31	/* RFC8031 */
#define IKEV2_XFORMDH_X_SNTRUP761X25519	1035	/* private */

extern struct iked_constmap ikev2_xformdh_map[];

#define IKEV2_IPV4_OVERHEAD		(20 + 8 + 28) /* IPv4 + UDP + IKE_HDR*/
#define IKEV2_MAXLEN_IPV4_FRAG		(576 - IKEV2_IPV4_OVERHEAD)
#define IKEV2_IPV6_OVERHEAD		(40 + 8 + 28) /* IPv6 + UDP + IKE_HDR*/
#define IKEV2_MAXLEN_IPV6_FRAG		(1280 - IKEV2_IPV6_OVERHEAD)

#define IKEV2_MAXNUM_TSS		255	/* 8 bit Number of TSs field */

#define IKEV2_XFORMESN_NONE		0	/* No ESN */
#define IKEV2_XFORMESN_ESN		1	/* ESN */

extern struct iked_constmap ikev2_xformesn_map[];

struct ikev2_attribute {
	uint16_t	attr_type;	/* Attribute type */
	uint16_t	attr_length;	/* Attribute length or value */
	/* Followed by variable length (TLV) */
} __packed;

#define IKEV2_ATTRAF_TLV		0x0000	/* Type-Length-Value format */
#define IKEV2_ATTRAF_TV			0x8000	/* Type-Value format */

#define IKEV2_ATTRTYPE_KEY_LENGTH	14	/* Key length */

extern struct iked_constmap ikev2_attrtype_map[];

/*
 * KE Payload
 */

struct ikev2_keyexchange {
	uint16_t	 kex_dhgroup;		/* DH Group # */
	uint16_t	 kex_reserved;		/* Reserved */
} __packed;

/*
 * N payload
 */

struct ikev2_notify {
	uint8_t		 n_protoid;		/* Protocol Id */
	uint8_t		 n_spisize;		/* SPI size */
	uint16_t	 n_type;		/* Notify message type */
	/* Followed by variable length SPI */
	/* Followed by variable length notification data */
} __packed;

#define IKEV2_N_UNSUPPORTED_CRITICAL_PAYLOAD	1	/* RFC7296 */
#define IKEV2_N_INVALID_IKE_SPI			4	/* RFC7296 */
#define IKEV2_N_INVALID_MAJOR_VERSION		5	/* RFC7296 */
#define IKEV2_N_INVALID_SYNTAX			7	/* RFC7296 */
#define IKEV2_N_INVALID_MESSAGE_ID		9	/* RFC7296 */
#define IKEV2_N_INVALID_SPI			11	/* RFC7296 */
#define IKEV2_N_NO_PROPOSAL_CHOSEN		14	/* RFC7296 */
#define IKEV2_N_INVALID_KE_PAYLOAD		17	/* RFC7296 */
#define IKEV2_N_AUTHENTICATION_FAILED		24	/* RFC7296 */
#define IKEV2_N_SINGLE_PAIR_REQUIRED		34	/* RFC7296 */
#define IKEV2_N_NO_ADDITIONAL_SAS		35	/* RFC7296 */
#define IKEV2_N_INTERNAL_ADDRESS_FAILURE	36	/* RFC7296 */
#define IKEV2_N_FAILED_CP_REQUIRED		37	/* RFC7296 */
#define IKEV2_N_TS_UNACCEPTABLE			38	/* RFC7296 */
#define IKEV2_N_INVALID_SELECTORS		39	/* RFC7296 */
#define IKEV2_N_UNACCEPTABLE_ADDRESSES		40	/* RFC4555 */
#define IKEV2_N_UNEXPECTED_NAT_DETECTED		41	/* RFC4555 */
#define IKEV2_N_USE_ASSIGNED_HoA		42	/* RFC5026 */
#define IKEV2_N_TEMPORARY_FAILURE		43	/* RFC7296 */
#define IKEV2_N_CHILD_SA_NOT_FOUND		44	/* RFC7296 */
#define IKEV2_N_INITIAL_CONTACT			16384	/* RFC7296 */
#define IKEV2_N_SET_WINDOW_SIZE			16385	/* RFC7296 */
#define IKEV2_N_ADDITIONAL_TS_POSSIBLE		16386	/* RFC7296 */
#define IKEV2_N_IPCOMP_SUPPORTED		16387	/* RFC7296 */
#define IKEV2_N_NAT_DETECTION_SOURCE_IP		16388	/* RFC7296 */
#define IKEV2_N_NAT_DETECTION_DESTINATION_IP	16389	/* RFC7296 */
#define IKEV2_N_COOKIE				16390	/* RFC7296 */
#define IKEV2_N_USE_TRANSPORT_MODE		16391	/* RFC7296 */
#define IKEV2_N_HTTP_CERT_LOOKUP_SUPPORTED	16392	/* RFC7296 */
#define IKEV2_N_REKEY_SA			16393	/* RFC7296 */
#define IKEV2_N_ESP_TFC_PADDING_NOT_SUPPORTED	16394	/* RFC7296 */
#define IKEV2_N_NON_FIRST_FRAGMENTS_ALSO	16395	/* RFC7296 */
#define IKEV2_N_MOBIKE_SUPPORTED		16396	/* RFC4555 */
#define IKEV2_N_ADDITIONAL_IP4_ADDRESS		16397	/* RFC4555 */
#define IKEV2_N_ADDITIONAL_IP6_ADDRESS		16398	/* RFC4555 */
#define IKEV2_N_NO_ADDITIONAL_ADDRESSES		16399	/* RFC4555 */
#define IKEV2_N_UPDATE_SA_ADDRESSES		16400	/* RFC4555 */
#define IKEV2_N_COOKIE2				16401	/* RFC4555 */
#define IKEV2_N_NO_NATS_ALLOWED			16402	/* RFC4555 */
#define IKEV2_N_AUTH_LIFETIME			16403	/* RFC4478 */
#define IKEV2_N_MULTIPLE_AUTH_SUPPORTED		16404	/* RFC4739 */
#define IKEV2_N_ANOTHER_AUTH_FOLLOWS		16405	/* RFC4739 */
#define IKEV2_N_REDIRECT_SUPPORTED		16406	/* RFC5685 */
#define IKEV2_N_REDIRECT			16407	/* RFC5685 */
#define IKEV2_N_REDIRECTED_FROM			16408	/* RFC5685 */
#define IKEV2_N_TICKET_LT_OPAQUE		16409	/* RFC5723 */
#define IKEV2_N_TICKET_REQUEST			16410	/* RFC5723 */
#define IKEV2_N_TICKET_ACK			16411	/* RFC5723 */
#define IKEV2_N_TICKET_NACK			16412	/* RFC5723 */
#define IKEV2_N_TICKET_OPAQUE			16413	/* RFC5723 */
#define IKEV2_N_LINK_ID				16414	/* RFC5739 */
#define IKEV2_N_USE_WESP_MODE			16415	/* RFC5415 */
#define IKEV2_N_ROHC_SUPPORTED			16416	/* RFC5857 */
#define IKEV2_N_EAP_ONLY_AUTHENTICATION		16417	/* RFC5998 */
#define IKEV2_N_CHILDLESS_IKEV2_SUPPORTED	16418	/* RFC6023 */
#define IKEV2_N_QUICK_CRASH_DETECTION		16419	/* RFC6290 */
#define IKEV2_N_IKEV2_MESSAGE_ID_SYNC_SUPPORTED	16420	/* RFC6311 */
#define IKEV2_N_IPSEC_REPLAY_CTR_SYNC_SUPPORTED	16421	/* RFC6311 */
#define IKEV2_N_IKEV2_MESSAGE_ID_SYNC		16422	/* RFC6311 */
#define IKEV2_N_IPSEC_REPLAY_CTR_SYNC		16423	/* RFC6311 */
#define IKEV2_N_SECURE_PASSWORD_METHODS		16424	/* RFC6467 */
#define IKEV2_N_PSK_PERSIST			16425	/* RFC6631 */
#define IKEV2_N_PSK_CONFIRM			16426	/* RFC6631 */
#define IKEV2_N_ERX_SUPPORTED			16427	/* RFC6867 */
#define IKEV2_N_IFOM_CAPABILITY			16428	/* OA3GPP */
#define IKEV2_N_FRAGMENTATION_SUPPORTED		16430	/* RFC7383 */
#define IKEV2_N_SIGNATURE_HASH_ALGORITHMS	16431	/* RFC7427 */

extern struct iked_constmap ikev2_n_map[];

/*
 * DELETE payload
 */

struct ikev2_delete {
	uint8_t		 del_protoid;		/* Protocol Id */
	uint8_t		 del_spisize;		/* SPI size */
	uint16_t	 del_nspi;		/* Number of SPIs */
	/* Followed by variable length SPIs */
} __packed;

/*
 * ID payload
 */

struct ikev2_id {
	uint8_t		 id_type;		/* Id type */
	uint8_t		 id_reserved[3];	/* Reserved */
	/* Followed by the identification data */
} __packed;

#define IKEV2_ID_NONE		0	/* No ID */
#define IKEV2_ID_IPV4		1	/* RFC7296 (ID_IPV4_ADDR) */
#define IKEV2_ID_FQDN		2	/* RFC7296 */
#define IKEV2_ID_UFQDN		3	/* RFC7296 (ID_RFC822_ADDR) */
#define IKEV2_ID_IPV6		5	/* RFC7296 (ID_IPV6_ADDR) */
#define IKEV2_ID_ASN1_DN	9	/* RFC7296 */
#define IKEV2_ID_ASN1_GN	10	/* RFC7296 */
#define IKEV2_ID_KEY_ID		11	/* RFC7296 */
#define IKEV2_ID_FC_NAME	12	/* RFC4595 */

extern struct iked_constmap ikev2_id_map[];

/*
 * CERT/CERTREQ payloads
 */

struct ikev2_cert {
	uint8_t		cert_type;	/* Encoding */
	/* Followed by the certificate data */
} __packed;

#define IKEV2_CERT_NONE			0	/* None */
#define IKEV2_CERT_X509_PKCS7		1	/* UNSPECIFIED */
#define IKEV2_CERT_PGP			2	/* UNSPECIFIED */
#define IKEV2_CERT_DNS_SIGNED_KEY	3	/* UNSPECIFIED */
#define IKEV2_CERT_X509_CERT		4	/* RFC7296 */
#define IKEV2_CERT_KERBEROS_TOKEN	6	/* UNSPECIFIED */
#define IKEV2_CERT_CRL			7	/* RFC7296 */
#define IKEV2_CERT_ARL			8	/* UNSPECIFIED */
#define IKEV2_CERT_SPKI			9	/* UNSPECIFIED */
#define IKEV2_CERT_X509_ATTR		10	/* UNSPECIFIED */
#define IKEV2_CERT_RSA_KEY		11	/* RFC7296 */
#define IKEV2_CERT_HASHURL_X509		12	/* RFC7296 */
#define IKEV2_CERT_HASHURL_X509_BUNDLE	13	/* RFC7296 */
#define IKEV2_CERT_OCSP			14	/* RFC4806 */
/*
 * As of November 2014, work was still in progress to add a more generic
 * format for raw public keys (RFC7296), so we use a number in IANA's private
 * use range (201-255, same RFC) for ECDSA.
 */
#define IKEV2_CERT_ECDSA		201	/* Private */
#define IKEV2_CERT_BUNDLE		254	/* Private */

extern struct iked_constmap ikev2_cert_map[];

/*
 * TSi/TSr payloads
 */

struct ikev2_tsp {
	uint8_t		tsp_count;		/* Number of TSs */
	uint8_t		tsp_reserved[3];	/* Reserved */
	/* Followed by the traffic selectors */
} __packed;

struct ikev2_ts {
	uint8_t		ts_type;		/* TS type */
	uint8_t		ts_protoid;		/* Protocol Id */
	uint16_t	ts_length;		/* Length */
	uint16_t	ts_startport;		/* Start port */
	uint16_t	ts_endport;		/* End port */
} __packed;

#define IKEV2_TS_IPV4_ADDR_RANGE	7	/* RFC7296 */
#define IKEV2_TS_IPV6_ADDR_RANGE	8	/* RFC7296 */
#define IKEV2_TS_FC_ADDR_RANGE		9	/* RFC4595 */

extern struct iked_constmap ikev2_ts_map[];

/*
 * AUTH payload
 */

struct ikev2_auth {
	uint8_t		auth_method;		/* Signature type */
	uint8_t		auth_reserved[3];	/* Reserved */
	/* Followed by the signature */
} __packed;

#define IKEV2_AUTH_NONE			0	/* None */
#define IKEV2_AUTH_RSA_SIG		1	/* RFC7296 */
#define IKEV2_AUTH_SHARED_KEY_MIC	2	/* RFC7296 */
#define IKEV2_AUTH_DSS_SIG		3	/* RFC7296 */
#define IKEV2_AUTH_ECDSA_256		9	/* RFC4754 */
#define IKEV2_AUTH_ECDSA_384		10	/* RFC4754 */
#define IKEV2_AUTH_ECDSA_521		11	/* RFC4754 */
#define IKEV2_AUTH_GSPM			12	/* RFC6467 */
#define IKEV2_AUTH_NULL			13	/* RFC7619 */
#define IKEV2_AUTH_SIG			14	/* RFC7427 */
#define IKEV2_AUTH_SIG_ANY		255	/* Internal (any signature) */
/*
 * AUTH_SIG also serves as an indication that a given policy has
 * been configured to accept RSA or ECDSA payloads, as long as it
 * successfully authenticates against a configured CA.
 */

extern struct iked_constmap ikev2_auth_map[];

/* Notifications used together with IKEV2_AUTH_SIG */

#define IKEV2_SIGHASH_RESERVED		0	/* RFC7427 */
#define IKEV2_SIGHASH_SHA1		1	/* RFC7427 */
#define IKEV2_SIGHASH_SHA2_256		2	/* RFC7427 */
#define IKEV2_SIGHASH_SHA2_384		3	/* RFC7427 */
#define IKEV2_SIGHASH_SHA2_512		4	/* RFC7427 */

extern struct iked_constmap ikev2_sighash_map[];

/*
 * CP payload
 */

struct ikev2_cp {
	uint8_t		cp_type;
	uint8_t		cp_reserved[3];
	/* Followed by the attributes */
} __packed;

#define IKEV2_CP_REQUEST	1	/* CFG-Request */
#define IKEV2_CP_REPLY		2	/* CFG-Reply */
#define IKEV2_CP_SET		3	/* CFG-SET */
#define IKEV2_CP_ACK		4	/* CFG-ACK */

extern struct iked_constmap ikev2_cp_map[];

struct ikev2_cfg {
	uint16_t	cfg_type;	/* first bit must be set to zero */
	uint16_t	cfg_length;
	/* Followed by variable-length data */
} __packed;

#define IKEV2_CFG_INTERNAL_IP4_ADDRESS		1	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP4_NETMASK		2	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP4_DNS		3	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP4_NBNS		4	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_ADDRESS_EXPIRY	5	/* RFC4306 */
#define IKEV2_CFG_INTERNAL_IP4_DHCP		6	/* RFC7296 */
#define IKEV2_CFG_APPLICATION_VERSION		7	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP6_ADDRESS		8	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP6_DNS		10	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP6_NBNS		11	/* RFC4306 */
#define IKEV2_CFG_INTERNAL_IP6_DHCP		12	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP4_SUBNET		13	/* RFC7296 */
#define IKEV2_CFG_SUPPORTED_ATTRIBUTES		14	/* RFC7296 */
#define IKEV2_CFG_INTERNAL_IP6_SUBNET		15	/* RFC7296 */
#define IKEV2_CFG_MIP6_HOME_PREFIX		16	/* RFC5026 */
#define IKEV2_CFG_INTERNAL_IP6_LINK		17	/* RFC5739 */
#define IKEV2_CFG_INTERNAL_IP6_PREFIX		18	/* RFC5739 */
#define IKEV2_CFG_HOME_AGENT_ADDRESS		19	/* http://www.3gpp.org/ftp/Specs/html-info/24302.htm */
#define IKEV2_CFG_INTERNAL_IP4_SERVER		23456	/* MS-IKEE */
#define IKEV2_CFG_INTERNAL_IP6_SERVER		23457	/* MS-IKEE */

extern struct iked_constmap ikev2_cfg_map[];

/* IKEv1 payload types */
#define IKEV1_PAYLOAD_NONE	0	/* No payload */
#define IKEV1_PAYLOAD_PROPOSAL	2	/* Proposal */

#endif /* IKED_IKEV2_H */
