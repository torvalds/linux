/*-
 * SPDX-License-Identifier: (ISC AND BSD-3-Clause)
 *
 * Portions Copyright (C) 2004, 2005, 2008, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (C) 1996-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 1983, 1989, 1993
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 *	$Id: nameser.h,v 1.16 2009/03/03 01:52:48 each Exp $
 * $FreeBSD$
 */

#ifndef _ARPA_NAMESER_H_
#define _ARPA_NAMESER_H_

/*! \file */

#define BIND_4_COMPAT

#include <sys/param.h>
#include <sys/types.h>
#include <sys/cdefs.h>

/*%
 * Revision information.  This is the release date in YYYYMMDD format.
 * It can change every day so the right thing to do with it is use it
 * in preprocessor commands such as "#if (__NAMESER > 19931104)".  Do not
 * compare for equality; rather, use it to determine whether your libbind.a
 * contains a new enough lib/nameser/ to support the feature you need.
 */

#define __NAMESER	20090302	/*%< New interface version stamp. */
/*
 * Define constants based on RFC0883, RFC1034, RFC 1035
 */
#define NS_PACKETSZ	512	/*%< default UDP packet size */
#define NS_MAXDNAME	1025	/*%< maximum domain name (presentation format)*/
#define NS_MAXMSG	65535	/*%< maximum message size */
#define NS_MAXCDNAME	255	/*%< maximum compressed domain name */
#define NS_MAXLABEL	63	/*%< maximum length of domain label */
#define NS_MAXLABELS	128	/*%< theoretical max #/labels per domain name */
#define NS_MAXNNAME	256	/*%< maximum uncompressed (binary) domain name*/
#define	NS_MAXPADDR	(sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
#define NS_HFIXEDSZ	12	/*%< #/bytes of fixed data in header */
#define NS_QFIXEDSZ	4	/*%< #/bytes of fixed data in query */
#define NS_RRFIXEDSZ	10	/*%< #/bytes of fixed data in r record */
#define NS_INT32SZ	4	/*%< #/bytes of data in a u_int32_t */
#define NS_INT16SZ	2	/*%< #/bytes of data in a u_int16_t */
#define NS_INT8SZ	1	/*%< #/bytes of data in a u_int8_t */
#define NS_INADDRSZ	4	/*%< IPv4 T_A */
#define NS_IN6ADDRSZ	16	/*%< IPv6 T_AAAA */
#define NS_CMPRSFLGS	0xc0	/*%< Flag bits indicating name compression. */
#define NS_DEFAULTPORT	53	/*%< For both TCP and UDP. */
/*
 * These can be expanded with synonyms, just keep ns_parse.c:ns_parserecord()
 * in synch with it.
 */
typedef enum __ns_sect {
	ns_s_qd = 0,		/*%< Query: Question. */
	ns_s_zn = 0,		/*%< Update: Zone. */
	ns_s_an = 1,		/*%< Query: Answer. */
	ns_s_pr = 1,		/*%< Update: Prerequisites. */
	ns_s_ns = 2,		/*%< Query: Name servers. */
	ns_s_ud = 2,		/*%< Update: Update. */
	ns_s_ar = 3,		/*%< Query|Update: Additional records. */
	ns_s_max = 4
} ns_sect;

/*%
 * Network name (compressed or not) type.  Equivalent to a pointer when used
 * in a function prototype.  Can be const'd.
 */
typedef u_char ns_nname[NS_MAXNNAME];
typedef const u_char *ns_nname_ct;
typedef u_char *ns_nname_t;

struct ns_namemap { ns_nname_ct base; int len; };
typedef struct ns_namemap *ns_namemap_t;
typedef const struct ns_namemap *ns_namemap_ct;

/*%
 * This is a message handle.  It is caller allocated and has no dynamic data.
 * This structure is intended to be opaque to all but ns_parse.c, thus the
 * leading _'s on the member names.  Use the accessor functions, not the _'s.
 */
typedef struct __ns_msg {
	const u_char	*_msg, *_eom;
	u_int16_t	_id, _flags, _counts[ns_s_max];
	const u_char	*_sections[ns_s_max];
	ns_sect		_sect;
	int		_rrnum;
	const u_char	*_msg_ptr;
} ns_msg;

/*
 * This is a newmsg handle, used when constructing new messages with
 * ns_newmsg_init, et al.
 */
struct ns_newmsg {
	ns_msg		msg;
	const u_char	*dnptrs[25];
	const u_char	**lastdnptr;
};
typedef struct ns_newmsg ns_newmsg;

/* Private data structure - do not use from outside library. */
struct _ns_flagdata {  int mask, shift;  };
extern struct _ns_flagdata _ns_flagdata[];

/* Accessor macros - this is part of the public interface. */

#define ns_msg_id(handle) ((handle)._id + 0)
#define ns_msg_base(handle) ((handle)._msg + 0)
#define ns_msg_end(handle) ((handle)._eom + 0)
#define ns_msg_size(handle) ((handle)._eom - (handle)._msg)
#define ns_msg_count(handle, section) ((handle)._counts[section] + 0)

/*%
 * This is a parsed record.  It is caller allocated and has no dynamic data.
 */
typedef	struct __ns_rr {
	char		name[NS_MAXDNAME];
	u_int16_t	type;
	u_int16_t	rr_class;
	u_int32_t	ttl;
	u_int16_t	rdlength;
	const u_char *	rdata;
} ns_rr;

/*
 * Same thing, but using uncompressed network binary names, and real C types.
 */
typedef	struct __ns_rr2 {
	ns_nname	nname;
	size_t		nnamel;
	int		type;
	int		rr_class;
	u_int		ttl;
	int		rdlength;
	const u_char *	rdata;
} ns_rr2;

/* Accessor macros - this is part of the public interface. */
#define ns_rr_name(rr)	(((rr).name[0] != '\0') ? (rr).name : ".")
#define ns_rr_nname(rr)	((const ns_nname_t)(rr).nname)
#define ns_rr_nnamel(rr) ((rr).nnamel + 0)
#define ns_rr_type(rr)	((ns_type)((rr).type + 0))
#define ns_rr_class(rr)	((ns_class)((rr).rr_class + 0))
#define ns_rr_ttl(rr)	((rr).ttl + 0)
#define ns_rr_rdlen(rr)	((rr).rdlength + 0)
#define ns_rr_rdata(rr)	((rr).rdata + 0)

/*%
 * These don't have to be in the same order as in the packet flags word,
 * and they can even overlap in some cases, but they will need to be kept
 * in synch with ns_parse.c:ns_flagdata[].
 */
typedef enum __ns_flag {
	ns_f_qr,		/*%< Question/Response. */
	ns_f_opcode,		/*%< Operation code. */
	ns_f_aa,		/*%< Authoritative Answer. */
	ns_f_tc,		/*%< Truncation occurred. */
	ns_f_rd,		/*%< Recursion Desired. */
	ns_f_ra,		/*%< Recursion Available. */
	ns_f_z,			/*%< MBZ. */
	ns_f_ad,		/*%< Authentic Data (DNSSEC). */
	ns_f_cd,		/*%< Checking Disabled (DNSSEC). */
	ns_f_rcode,		/*%< Response code. */
	ns_f_max
} ns_flag;

/*%
 * Currently defined opcodes.
 */
typedef enum __ns_opcode {
	ns_o_query = 0,		/*%< Standard query. */
	ns_o_iquery = 1,	/*%< Inverse query (deprecated/unsupported). */
	ns_o_status = 2,	/*%< Name server status query (unsupported). */
				/* Opcode 3 is undefined/reserved. */
	ns_o_notify = 4,	/*%< Zone change notification. */
	ns_o_update = 5,	/*%< Zone update message. */
	ns_o_max = 6
} ns_opcode;

/*%
 * Currently defined response codes.
 */
typedef	enum __ns_rcode {
	ns_r_noerror = 0,	/*%< No error occurred. */
	ns_r_formerr = 1,	/*%< Format error. */
	ns_r_servfail = 2,	/*%< Server failure. */
	ns_r_nxdomain = 3,	/*%< Name error. */
	ns_r_notimpl = 4,	/*%< Unimplemented. */
	ns_r_refused = 5,	/*%< Operation refused. */
	/* these are for BIND_UPDATE */
	ns_r_yxdomain = 6,	/*%< Name exists */
	ns_r_yxrrset = 7,	/*%< RRset exists */
	ns_r_nxrrset = 8,	/*%< RRset does not exist */
	ns_r_notauth = 9,	/*%< Not authoritative for zone */
	ns_r_notzone = 10,	/*%< Zone of record different from zone section */
	ns_r_max = 11,
	/* The following are EDNS extended rcodes */
	ns_r_badvers = 16,
	/* The following are TSIG errors */
	ns_r_badsig = 16,
	ns_r_badkey = 17,
	ns_r_badtime = 18
} ns_rcode;

/* BIND_UPDATE */
typedef enum __ns_update_operation {
	ns_uop_delete = 0,
	ns_uop_add = 1,
	ns_uop_max = 2
} ns_update_operation;

/*%
 * This structure is used for TSIG authenticated messages
 */
struct ns_tsig_key {
	char name[NS_MAXDNAME], alg[NS_MAXDNAME];
	unsigned char *data;
	int len;
};
typedef struct ns_tsig_key ns_tsig_key;

/*%
 * This structure is used for TSIG authenticated TCP messages
 */
struct ns_tcp_tsig_state {
	int counter;
	struct dst_key *key;
	void *ctx;
	unsigned char sig[NS_PACKETSZ];
	int siglen;
};
typedef struct ns_tcp_tsig_state ns_tcp_tsig_state;

#define NS_TSIG_FUDGE 300
#define NS_TSIG_TCP_COUNT 100
#define NS_TSIG_ALG_HMAC_MD5 "HMAC-MD5.SIG-ALG.REG.INT"

#define NS_TSIG_ERROR_NO_TSIG -10
#define NS_TSIG_ERROR_NO_SPACE -11
#define NS_TSIG_ERROR_FORMERR -12

/*%
 * Currently defined type values for resources and queries.
 */
typedef enum __ns_type {
	ns_t_invalid = 0,	/*%< Cookie. */
	ns_t_a = 1,		/*%< Host address. */
	ns_t_ns = 2,		/*%< Authoritative server. */
	ns_t_md = 3,		/*%< Mail destination. */
	ns_t_mf = 4,		/*%< Mail forwarder. */
	ns_t_cname = 5,		/*%< Canonical name. */
	ns_t_soa = 6,		/*%< Start of authority zone. */
	ns_t_mb = 7,		/*%< Mailbox domain name. */
	ns_t_mg = 8,		/*%< Mail group member. */
	ns_t_mr = 9,		/*%< Mail rename name. */
	ns_t_null = 10,		/*%< Null resource record. */
	ns_t_wks = 11,		/*%< Well known service. */
	ns_t_ptr = 12,		/*%< Domain name pointer. */
	ns_t_hinfo = 13,	/*%< Host information. */
	ns_t_minfo = 14,	/*%< Mailbox information. */
	ns_t_mx = 15,		/*%< Mail routing information. */
	ns_t_txt = 16,		/*%< Text strings. */
	ns_t_rp = 17,		/*%< Responsible person. */
	ns_t_afsdb = 18,	/*%< AFS cell database. */
	ns_t_x25 = 19,		/*%< X_25 calling address. */
	ns_t_isdn = 20,		/*%< ISDN calling address. */
	ns_t_rt = 21,		/*%< Router. */
	ns_t_nsap = 22,		/*%< NSAP address. */
	ns_t_nsap_ptr = 23,	/*%< Reverse NSAP lookup (deprecated). */
	ns_t_sig = 24,		/*%< Security signature. */
	ns_t_key = 25,		/*%< Security key. */
	ns_t_px = 26,		/*%< X.400 mail mapping. */
	ns_t_gpos = 27,		/*%< Geographical position (withdrawn). */
	ns_t_aaaa = 28,		/*%< IPv6 Address. */
	ns_t_loc = 29,		/*%< Location Information. */
	ns_t_nxt = 30,		/*%< Next domain (security). */
	ns_t_eid = 31,		/*%< Endpoint identifier. */
	ns_t_nimloc = 32,	/*%< Nimrod Locator. */
	ns_t_srv = 33,		/*%< Server Selection. */
	ns_t_atma = 34,		/*%< ATM Address */
	ns_t_naptr = 35,	/*%< Naming Authority PoinTeR */
	ns_t_kx = 36,		/*%< Key Exchange */
	ns_t_cert = 37,		/*%< Certification record */
	ns_t_a6 = 38,		/*%< IPv6 address (experimental) */
	ns_t_dname = 39,	/*%< Non-terminal DNAME */
	ns_t_sink = 40,		/*%< Kitchen sink (experimentatl) */
	ns_t_opt = 41,		/*%< EDNS0 option (meta-RR) */
	ns_t_apl = 42,		/*%< Address prefix list (RFC3123) */
	ns_t_ds = 43,		/*%< Delegation Signer */
	ns_t_sshfp = 44,	/*%< SSH Fingerprint */
	ns_t_ipseckey = 45,	/*%< IPSEC Key */
	ns_t_rrsig = 46,	/*%< RRset Signature */
	ns_t_nsec = 47,		/*%< Negative security */
	ns_t_dnskey = 48,	/*%< DNS Key */
	ns_t_dhcid = 49,	/*%< Dynamic host configuratin identifier */
	ns_t_nsec3 = 50,	/*%< Negative security type 3 */
	ns_t_nsec3param = 51,	/*%< Negative security type 3 parameters */
	ns_t_hip = 55,		/*%< Host Identity Protocol */
	ns_t_spf = 99,		/*%< Sender Policy Framework */
	ns_t_tkey = 249,	/*%< Transaction key */
	ns_t_tsig = 250,	/*%< Transaction signature. */
	ns_t_ixfr = 251,	/*%< Incremental zone transfer. */
	ns_t_axfr = 252,	/*%< Transfer zone of authority. */
	ns_t_mailb = 253,	/*%< Transfer mailbox records. */
	ns_t_maila = 254,	/*%< Transfer mail agent records. */
	ns_t_any = 255,		/*%< Wildcard match. */
	ns_t_zxfr = 256,	/*%< BIND-specific, nonstandard. */
	ns_t_dlv = 32769,	/*%< DNSSEC look-aside validatation. */
	ns_t_max = 65536
} ns_type;

/* Exclusively a QTYPE? (not also an RTYPE) */
#define	ns_t_qt_p(t) (ns_t_xfr_p(t) || (t) == ns_t_any || \
		      (t) == ns_t_mailb || (t) == ns_t_maila)
/* Some kind of meta-RR? (not a QTYPE, but also not an RTYPE) */
#define	ns_t_mrr_p(t) ((t) == ns_t_tsig || (t) == ns_t_opt)
/* Exclusively an RTYPE? (not also a QTYPE or a meta-RR) */
#define ns_t_rr_p(t) (!ns_t_qt_p(t) && !ns_t_mrr_p(t))
#define ns_t_udp_p(t) ((t) != ns_t_axfr && (t) != ns_t_zxfr)
#define ns_t_xfr_p(t) ((t) == ns_t_axfr || (t) == ns_t_ixfr || \
		       (t) == ns_t_zxfr)

/*%
 * Values for class field
 */
typedef enum __ns_class {
	ns_c_invalid = 0,	/*%< Cookie. */
	ns_c_in = 1,		/*%< Internet. */
	ns_c_2 = 2,		/*%< unallocated/unsupported. */
	ns_c_chaos = 3,		/*%< MIT Chaos-net. */
	ns_c_hs = 4,		/*%< MIT Hesiod. */
	/* Query class values which do not appear in resource records */
	ns_c_none = 254,	/*%< for prereq. sections in update requests */
	ns_c_any = 255,		/*%< Wildcard match. */
	ns_c_max = 65536
} ns_class;

/* DNSSEC constants. */

typedef enum __ns_key_types {
	ns_kt_rsa = 1,		/*%< key type RSA/MD5 */
	ns_kt_dh  = 2,		/*%< Diffie Hellman */
	ns_kt_dsa = 3,		/*%< Digital Signature Standard (MANDATORY) */
	ns_kt_private = 254	/*%< Private key type starts with OID */
} ns_key_types;

typedef enum __ns_cert_types {
	cert_t_pkix = 1,	/*%< PKIX (X.509v3) */
	cert_t_spki = 2,	/*%< SPKI */
	cert_t_pgp  = 3,	/*%< PGP */
	cert_t_url  = 253,	/*%< URL private type */
	cert_t_oid  = 254	/*%< OID private type */
} ns_cert_types;

/* Flags field of the KEY RR rdata. */
#define	NS_KEY_TYPEMASK		0xC000	/*%< Mask for "type" bits */
#define	NS_KEY_TYPE_AUTH_CONF	0x0000	/*%< Key usable for both */
#define	NS_KEY_TYPE_CONF_ONLY	0x8000	/*%< Key usable for confidentiality */
#define	NS_KEY_TYPE_AUTH_ONLY	0x4000	/*%< Key usable for authentication */
#define	NS_KEY_TYPE_NO_KEY	0xC000	/*%< No key usable for either; no key */
/* The type bits can also be interpreted independently, as single bits: */
#define	NS_KEY_NO_AUTH		0x8000	/*%< Key unusable for authentication */
#define	NS_KEY_NO_CONF		0x4000	/*%< Key unusable for confidentiality */
#define	NS_KEY_RESERVED2	0x2000	/* Security is *mandatory* if bit=0 */
#define	NS_KEY_EXTENDED_FLAGS	0x1000	/*%< reserved - must be zero */
#define	NS_KEY_RESERVED4	0x0800  /*%< reserved - must be zero */
#define	NS_KEY_RESERVED5	0x0400  /*%< reserved - must be zero */
#define	NS_KEY_NAME_TYPE	0x0300	/*%< these bits determine the type */
#define	NS_KEY_NAME_USER	0x0000	/*%< key is assoc. with user */
#define	NS_KEY_NAME_ENTITY	0x0200	/*%< key is assoc. with entity eg host */
#define	NS_KEY_NAME_ZONE	0x0100	/*%< key is zone key */
#define	NS_KEY_NAME_RESERVED	0x0300	/*%< reserved meaning */
#define	NS_KEY_RESERVED8	0x0080  /*%< reserved - must be zero */
#define	NS_KEY_RESERVED9	0x0040  /*%< reserved - must be zero */
#define	NS_KEY_RESERVED10	0x0020  /*%< reserved - must be zero */
#define	NS_KEY_RESERVED11	0x0010  /*%< reserved - must be zero */
#define	NS_KEY_SIGNATORYMASK	0x000F	/*%< key can sign RR's of same name */
#define	NS_KEY_RESERVED_BITMASK ( NS_KEY_RESERVED2 | \
				  NS_KEY_RESERVED4 | \
				  NS_KEY_RESERVED5 | \
				  NS_KEY_RESERVED8 | \
				  NS_KEY_RESERVED9 | \
				  NS_KEY_RESERVED10 | \
				  NS_KEY_RESERVED11 )
#define NS_KEY_RESERVED_BITMASK2 0xFFFF /*%< no bits defined here */
/* The Algorithm field of the KEY and SIG RR's is an integer, {1..254} */
#define	NS_ALG_MD5RSA		1	/*%< MD5 with RSA */
#define	NS_ALG_DH               2	/*%< Diffie Hellman KEY */
#define	NS_ALG_DSA              3	/*%< DSA KEY */
#define	NS_ALG_DSS              NS_ALG_DSA
#define	NS_ALG_EXPIRE_ONLY	253	/*%< No alg, no security */
#define	NS_ALG_PRIVATE_OID	254	/*%< Key begins with OID giving alg */
/* Protocol values  */
/* value 0 is reserved */
#define NS_KEY_PROT_TLS         1
#define NS_KEY_PROT_EMAIL       2
#define NS_KEY_PROT_DNSSEC      3
#define NS_KEY_PROT_IPSEC       4
#define NS_KEY_PROT_ANY		255

/* Signatures */
#define	NS_MD5RSA_MIN_BITS	 512	/*%< Size of a mod or exp in bits */
#define	NS_MD5RSA_MAX_BITS	4096
	/* Total of binary mod and exp */
#define	NS_MD5RSA_MAX_BYTES	((NS_MD5RSA_MAX_BITS+7/8)*2+3)
	/* Max length of text sig block */
#define	NS_MD5RSA_MAX_BASE64	(((NS_MD5RSA_MAX_BYTES+2)/3)*4)
#define NS_MD5RSA_MIN_SIZE	((NS_MD5RSA_MIN_BITS+7)/8)
#define NS_MD5RSA_MAX_SIZE	((NS_MD5RSA_MAX_BITS+7)/8)

#define NS_DSA_SIG_SIZE         41
#define NS_DSA_MIN_SIZE         213
#define NS_DSA_MAX_BYTES        405

/* Offsets into SIG record rdata to find various values */
#define	NS_SIG_TYPE	0	/*%< Type flags */
#define	NS_SIG_ALG	2	/*%< Algorithm */
#define	NS_SIG_LABELS	3	/*%< How many labels in name */
#define	NS_SIG_OTTL	4	/*%< Original TTL */
#define	NS_SIG_EXPIR	8	/*%< Expiration time */
#define	NS_SIG_SIGNED	12	/*%< Signature time */
#define	NS_SIG_FOOT	16	/*%< Key footprint */
#define	NS_SIG_SIGNER	18	/*%< Domain name of who signed it */
/* How RR types are represented as bit-flags in NXT records */
#define	NS_NXT_BITS 8
#define	NS_NXT_BIT_SET(  n,p) (p[(n)/NS_NXT_BITS] |=  (0x80>>((n)%NS_NXT_BITS)))
#define	NS_NXT_BIT_CLEAR(n,p) (p[(n)/NS_NXT_BITS] &= ~(0x80>>((n)%NS_NXT_BITS)))
#define	NS_NXT_BIT_ISSET(n,p) (p[(n)/NS_NXT_BITS] &   (0x80>>((n)%NS_NXT_BITS)))
#define NS_NXT_MAX 127

/*%
 * EDNS0 extended flags and option codes, host order.
 */
#define NS_OPT_DNSSEC_OK	0x8000U
#define NS_OPT_NSID             3

/*%
 * Inline versions of get/put short/long.  Pointer is advanced.
 */
#define NS_GET16(s, cp) do { \
	register const u_char *t_cp = (const u_char *)(cp); \
	(s) = ((u_int16_t)t_cp[0] << 8) \
	    | ((u_int16_t)t_cp[1]) \
	    ; \
	(cp) += NS_INT16SZ; \
} while (0)

#define NS_GET32(l, cp) do { \
	register const u_char *t_cp = (const u_char *)(cp); \
	(l) = ((u_int32_t)t_cp[0] << 24) \
	    | ((u_int32_t)t_cp[1] << 16) \
	    | ((u_int32_t)t_cp[2] << 8) \
	    | ((u_int32_t)t_cp[3]) \
	    ; \
	(cp) += NS_INT32SZ; \
} while (0)

#define NS_PUT16(s, cp) do { \
	register u_int16_t t_s = (u_int16_t)(s); \
	register u_char *t_cp = (u_char *)(cp); \
	*t_cp++ = t_s >> 8; \
	*t_cp   = t_s; \
	(cp) += NS_INT16SZ; \
} while (0)

#define NS_PUT32(l, cp) do { \
	register u_int32_t t_l = (u_int32_t)(l); \
	register u_char *t_cp = (u_char *)(cp); \
	*t_cp++ = t_l >> 24; \
	*t_cp++ = t_l >> 16; \
	*t_cp++ = t_l >> 8; \
	*t_cp   = t_l; \
	(cp) += NS_INT32SZ; \
} while (0)

/*%
 * ANSI C identifier hiding for bind's lib/nameser.
 */
#define	ns_msg_getflag		__ns_msg_getflag
#define ns_get16		__ns_get16
#define ns_get32		__ns_get32
#define ns_put16		__ns_put16
#define ns_put32		__ns_put32
#define ns_initparse		__ns_initparse
#define ns_skiprr		__ns_skiprr
#define ns_parserr		__ns_parserr
#define ns_parserr2		__ns_parserr2
#define	ns_sprintrr		__ns_sprintrr
#define	ns_sprintrrf		__ns_sprintrrf
#define	ns_format_ttl		__ns_format_ttl
#define	ns_parse_ttl		__ns_parse_ttl
#if 0
#define ns_datetosecs		__ns_datetosecs
#endif
#define	ns_name_ntol		__ns_name_ntol
#define	ns_name_ntop		__ns_name_ntop
#define	ns_name_pton		__ns_name_pton
#define	ns_name_pton2		__ns_name_pton2
#define	ns_name_unpack		__ns_name_unpack
#define	ns_name_unpack2		__ns_name_unpack2
#define	ns_name_pack		__ns_name_pack
#define	ns_name_compress	__ns_name_compress
#define	ns_name_uncompress	__ns_name_uncompress
#define	ns_name_skip		__ns_name_skip
#define	ns_name_rollback	__ns_name_rollback
#define	ns_name_length		__ns_name_length
#define	ns_name_eq		__ns_name_eq
#define	ns_name_owned		__ns_name_owned
#define	ns_name_map		__ns_name_map
#define	ns_name_labels		__ns_name_labels
#if 0
#define	ns_sign			__ns_sign
#define	ns_sign2		__ns_sign2
#define	ns_sign_tcp		__ns_sign_tcp
#define	ns_sign_tcp2		__ns_sign_tcp2
#define	ns_sign_tcp_init	__ns_sign_tcp_init
#define ns_find_tsig		__ns_find_tsig
#define	ns_verify		__ns_verify
#define	ns_verify_tcp		__ns_verify_tcp
#define	ns_verify_tcp_init	__ns_verify_tcp_init
#endif
#define	ns_samedomain		__ns_samedomain
#if 0
#define	ns_subdomain		__ns_subdomain
#endif
#define	ns_makecanon		__ns_makecanon
#define	ns_samename		__ns_samename
#define	ns_newmsg_init		__ns_newmsg_init
#define	ns_newmsg_copy		__ns_newmsg_copy
#define	ns_newmsg_id		__ns_newmsg_id
#define	ns_newmsg_flag		__ns_newmsg_flag
#define	ns_newmsg_q		__ns_newmsg_q
#define	ns_newmsg_rr		__ns_newmsg_rr
#define	ns_newmsg_done		__ns_newmsg_done
#define	ns_rdata_unpack		__ns_rdata_unpack
#define	ns_rdata_equal		__ns_rdata_equal
#define	ns_rdata_refers		__ns_rdata_refers

__BEGIN_DECLS
int		ns_msg_getflag(ns_msg, int);
u_int		ns_get16(const u_char *);
u_long		ns_get32(const u_char *);
void		ns_put16(u_int, u_char *);
void		ns_put32(u_long, u_char *);
int		ns_initparse(const u_char *, int, ns_msg *);
int		ns_skiprr(const u_char *, const u_char *, ns_sect, int);
int		ns_parserr(ns_msg *, ns_sect, int, ns_rr *);
int		ns_parserr2(ns_msg *, ns_sect, int, ns_rr2 *);
int		ns_sprintrr(const ns_msg *, const ns_rr *,
			    const char *, const char *, char *, size_t);
int		ns_sprintrrf(const u_char *, size_t, const char *,
			     ns_class, ns_type, u_long, const u_char *,
			     size_t, const char *, const char *,
			     char *, size_t);
int		ns_format_ttl(u_long, char *, size_t);
int		ns_parse_ttl(const char *, u_long *);
#if 0
u_int32_t	ns_datetosecs(const char *cp, int *errp);
#endif
int		ns_name_ntol(const u_char *, u_char *, size_t);
int		ns_name_ntop(const u_char *, char *, size_t);
int		ns_name_pton(const char *, u_char *, size_t);
int		ns_name_pton2(const char *, u_char *, size_t, size_t *);
int		ns_name_unpack(const u_char *, const u_char *,
			       const u_char *, u_char *, size_t);
int		ns_name_unpack2(const u_char *, const u_char *,
				const u_char *, u_char *, size_t,
				size_t *);
int		ns_name_pack(const u_char *, u_char *, int,
			     const u_char **, const u_char **);
int		ns_name_uncompress(const u_char *, const u_char *,
				   const u_char *, char *, size_t);
int		ns_name_compress(const char *, u_char *, size_t,
				 const u_char **, const u_char **);
int		ns_name_skip(const u_char **, const u_char *);
void		ns_name_rollback(const u_char *, const u_char **,
				 const u_char **);
ssize_t		ns_name_length(ns_nname_ct, size_t);
int		ns_name_eq(ns_nname_ct, size_t, ns_nname_ct, size_t);
int		ns_name_owned(ns_namemap_ct, int, ns_namemap_ct, int);
int		ns_name_map(ns_nname_ct, size_t, ns_namemap_t, int);
int		ns_name_labels(ns_nname_ct, size_t);
#if 0
int		ns_sign(u_char *, int *, int, int, void *,
			const u_char *, int, u_char *, int *, time_t);
int		ns_sign2(u_char *, int *, int, int, void *,
			 const u_char *, int, u_char *, int *, time_t,
			 u_char **, u_char **);
int		ns_sign_tcp(u_char *, int *, int, int,
			    ns_tcp_tsig_state *, int);
int		ns_sign_tcp2(u_char *, int *, int, int,
			     ns_tcp_tsig_state *, int,
			     u_char **, u_char **);
int		ns_sign_tcp_init(void *, const u_char *, int,
				 ns_tcp_tsig_state *);
u_char		*ns_find_tsig(u_char *, u_char *);
int		ns_verify(u_char *, int *, void *,
			  const u_char *, int, u_char *, int *,
			  time_t *, int);
int		ns_verify_tcp(u_char *, int *, ns_tcp_tsig_state *, int);
int		ns_verify_tcp_init(void *, const u_char *, int,
				   ns_tcp_tsig_state *);
#endif
int		ns_samedomain(const char *, const char *);
#if 0
int		ns_subdomain(const char *, const char *);
#endif
int		ns_makecanon(const char *, char *, size_t);
int		ns_samename(const char *, const char *);
int		ns_newmsg_init(u_char *buffer, size_t bufsiz, ns_newmsg *);
int		ns_newmsg_copy(ns_newmsg *, ns_msg *);
void		ns_newmsg_id(ns_newmsg *handle, u_int16_t id);
void		ns_newmsg_flag(ns_newmsg *handle, ns_flag flag, u_int value);
int		ns_newmsg_q(ns_newmsg *handle, ns_nname_ct qname,
			    ns_type qtype, ns_class qclass);
int		ns_newmsg_rr(ns_newmsg *handle, ns_sect sect,
			     ns_nname_ct name, ns_type type,
			     ns_class rr_class, u_int32_t ttl,
			     u_int16_t rdlen, const u_char *rdata);
size_t		ns_newmsg_done(ns_newmsg *handle);
ssize_t		ns_rdata_unpack(const u_char *, const u_char *, ns_type,
				const u_char *, size_t, u_char *, size_t);
int		ns_rdata_equal(ns_type,
			       const u_char *, size_t,
			       const u_char *, size_t);
int		ns_rdata_refers(ns_type,
				const u_char *, size_t,
				const u_char *);
__END_DECLS

#ifdef BIND_4_COMPAT
#include <arpa/nameser_compat.h>
#endif

#endif /* !_ARPA_NAMESER_H_ */
/*! \file */
