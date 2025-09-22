/* $OpenBSD: pfkeyv2.h,v 1.95 2024/05/13 01:15:53 jsg Exp $ */
/*
 *	@(#)COPYRIGHT	1.1 (NRL) January 1998
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#ifndef _NET_PFKEY_V2_H_
#define _NET_PFKEY_V2_H_

#define PF_KEY_V2			2
#define PFKEYV2_REVISION		199806L

/* This should be updated whenever the API is altered.  */
#define _OPENBSD_IPSEC_API_VERSION	2

#define SADB_RESERVED      0
#define SADB_GETSPI        1
#define SADB_UPDATE        2
#define SADB_ADD           3
#define SADB_DELETE        4
#define SADB_GET           5
#define SADB_ACQUIRE       6
#define SADB_REGISTER      7
#define SADB_EXPIRE        8
#define SADB_FLUSH         9
#define SADB_DUMP          10
#define SADB_X_PROMISC     11
#define SADB_X_ADDFLOW     12
#define SADB_X_DELFLOW     13
#define SADB_X_GRPSPIS     14
#define SADB_X_ASKPOLICY   15
#define SADB_X_SPDDUMP     16
#define SADB_MAX           16

struct sadb_msg {
	uint8_t sadb_msg_version;
	uint8_t sadb_msg_type;
	uint8_t sadb_msg_errno;
	uint8_t sadb_msg_satype;
	uint16_t sadb_msg_len;
	uint16_t sadb_msg_reserved;
	uint32_t sadb_msg_seq;
	uint32_t sadb_msg_pid;
};

struct sadb_ext {
	uint16_t sadb_ext_len;
	uint16_t sadb_ext_type;
};

struct sadb_sa {
	uint16_t sadb_sa_len;
	uint16_t sadb_sa_exttype;
	uint32_t sadb_sa_spi;
	uint8_t sadb_sa_replay;
	uint8_t sadb_sa_state;
	uint8_t sadb_sa_auth;
	uint8_t sadb_sa_encrypt;
	uint32_t sadb_sa_flags;
};

struct sadb_lifetime {
	uint16_t sadb_lifetime_len;
	uint16_t sadb_lifetime_exttype;
	uint32_t sadb_lifetime_allocations;
	uint64_t sadb_lifetime_bytes;
	uint64_t sadb_lifetime_addtime;
	uint64_t sadb_lifetime_usetime;
};

struct sadb_address {
	uint16_t sadb_address_len;
	uint16_t sadb_address_exttype;
	uint32_t sadb_address_reserved;
};

struct sadb_key {
	uint16_t sadb_key_len;
	uint16_t sadb_key_exttype;
	uint16_t sadb_key_bits;
	uint16_t sadb_key_reserved;
};

struct sadb_ident {
	uint16_t sadb_ident_len;
	uint16_t sadb_ident_exttype;
	uint16_t sadb_ident_type;
	uint16_t sadb_ident_reserved;
	uint64_t sadb_ident_id;
};

struct sadb_sens {
	uint16_t sadb_sens_len;
	uint16_t sadb_sens_exttype;
	uint32_t sadb_sens_dpd;
	uint8_t sadb_sens_sens_level;
	uint8_t sadb_sens_sens_len;
	uint8_t sadb_sens_integ_level;
	uint8_t sadb_sens_integ_len;
	uint32_t sadb_sens_reserved;
};

struct sadb_prop {
	uint16_t sadb_prop_len;
	uint16_t sadb_prop_exttype;
	uint8_t sadb_prop_num;
	uint8_t sadb_prop_replay;
	uint16_t sadb_prop_reserved;
};

struct sadb_comb {
	uint8_t sadb_comb_auth;
	uint8_t sadb_comb_encrypt;
	uint16_t sadb_comb_flags;
	uint16_t sadb_comb_auth_minbits;
	uint16_t sadb_comb_auth_maxbits;
	uint16_t sadb_comb_encrypt_minbits;
	uint16_t sadb_comb_encrypt_maxbits;
	uint32_t sadb_comb_reserved;
	uint32_t sadb_comb_soft_allocations;
	uint32_t sadb_comb_hard_allocations;
	uint64_t sadb_comb_soft_bytes;
	uint64_t sadb_comb_hard_bytes;
	uint64_t sadb_comb_soft_addtime;
	uint64_t sadb_comb_hard_addtime;
	uint64_t sadb_comb_soft_usetime;
	uint64_t sadb_comb_hard_usetime;
};

struct sadb_supported {
	uint16_t sadb_supported_len;
	uint16_t sadb_supported_exttype;
	uint32_t sadb_supported_reserved;
};

struct sadb_alg {
	uint8_t sadb_alg_id;
	uint8_t sadb_alg_ivlen;
	uint16_t sadb_alg_minbits;
	uint16_t sadb_alg_maxbits;
	uint16_t sadb_alg_reserved;
};

struct sadb_spirange {
	uint16_t sadb_spirange_len;
	uint16_t sadb_spirange_exttype;
	uint32_t sadb_spirange_min;
	uint32_t sadb_spirange_max;
	uint32_t sadb_spirange_reserved;
};

struct sadb_protocol {
	uint16_t sadb_protocol_len;
	uint16_t sadb_protocol_exttype;
	uint8_t  sadb_protocol_proto;
	uint8_t  sadb_protocol_direction;
	uint8_t  sadb_protocol_flags;
	uint8_t  sadb_protocol_reserved2;
};

struct sadb_x_policy {
	uint16_t  sadb_x_policy_len;
	uint16_t  sadb_x_policy_exttype;
	uint32_t  sadb_x_policy_seq;
};

struct sadb_x_udpencap {
	uint16_t sadb_x_udpencap_len;
	uint16_t sadb_x_udpencap_exttype;
	uint16_t sadb_x_udpencap_port;
	uint16_t sadb_x_udpencap_reserved;
};

struct sadb_x_tag {
	uint16_t  sadb_x_tag_len;
	uint16_t  sadb_x_tag_exttype;
	uint32_t  sadb_x_tag_taglen;
};

struct sadb_x_replay {
	uint16_t  sadb_x_replay_len;
	uint16_t  sadb_x_replay_exttype;
	uint32_t  sadb_x_replay_reserved;
	uint64_t  sadb_x_replay_count;
};

struct sadb_x_rdomain {
	uint16_t  sadb_x_rdomain_len;
	uint16_t  sadb_x_rdomain_exttype;
	uint16_t  sadb_x_rdomain_dom1;
	uint16_t  sadb_x_rdomain_dom2;
};

struct sadb_x_tap {
	uint16_t  sadb_x_tap_len;
	uint16_t  sadb_x_tap_exttype;
	uint32_t  sadb_x_tap_unit;
};

struct sadb_x_counter {
	uint16_t  sadb_x_counter_len;
	uint16_t  sadb_x_counter_exttype;
	uint32_t  sadb_x_counter_pad;
	uint64_t  sadb_x_counter_ipackets;	/* Input IPsec packets */
	uint64_t  sadb_x_counter_opackets;	/* Output IPsec packets */
	uint64_t  sadb_x_counter_ibytes;	/* Input bytes */
	uint64_t  sadb_x_counter_obytes;	/* Output bytes */
	uint64_t  sadb_x_counter_idrops;	/* Dropped on input */
	uint64_t  sadb_x_counter_odrops;	/* Dropped on output */
	uint64_t  sadb_x_counter_idecompbytes;	/* Input bytes, decompressed */
	uint64_t  sadb_x_counter_ouncompbytes;	/* Output bytes, uncompressed */
};

struct sadb_x_mtu {
	uint16_t  sadb_x_mtu_len;
	uint16_t  sadb_x_mtu_exttype;
	uint32_t  sadb_x_mtu_mtu;
};

struct sadb_x_iface {
	uint16_t  sadb_x_iface_len;
	uint16_t  sadb_x_iface_exttype;
	uint32_t  sadb_x_iface_unit;
	uint8_t   sadb_x_iface_direction;
	uint8_t   sadb_x_iface_reserved[7];
};

#ifdef _KERNEL
#define SADB_X_GETSPROTO(x) \
	( (x) == SADB_SATYPE_AH ? IPPROTO_AH :\
	(x) == SADB_SATYPE_ESP ? IPPROTO_ESP :\
	(x) == SADB_X_SATYPE_TCPSIGNATURE ? IPPROTO_TCP :\
	(x) == SADB_X_SATYPE_IPCOMP ? IPPROTO_IPCOMP: IPPROTO_IPIP )
#endif

#define SADB_EXT_RESERVED             0
#define SADB_EXT_SA                   1
#define SADB_EXT_LIFETIME_CURRENT     2
#define SADB_EXT_LIFETIME_HARD        3
#define SADB_EXT_LIFETIME_SOFT        4
#define SADB_EXT_ADDRESS_SRC          5
#define SADB_EXT_ADDRESS_DST          6
#define SADB_EXT_ADDRESS_PROXY        7
#define SADB_EXT_KEY_AUTH             8
#define SADB_EXT_KEY_ENCRYPT          9
#define SADB_EXT_IDENTITY_SRC         10
#define SADB_EXT_IDENTITY_DST         11
#define SADB_EXT_SENSITIVITY          12
#define SADB_EXT_PROPOSAL             13
#define SADB_EXT_SUPPORTED_AUTH	      14
#define SADB_EXT_SUPPORTED_ENCRYPT    15
#define SADB_EXT_SPIRANGE             16
#define SADB_X_EXT_SRC_MASK           17
#define SADB_X_EXT_DST_MASK           18
#define SADB_X_EXT_PROTOCOL           19
#define SADB_X_EXT_FLOW_TYPE          20
#define SADB_X_EXT_SRC_FLOW           21
#define SADB_X_EXT_DST_FLOW           22
#define SADB_X_EXT_SA2                23
#define SADB_X_EXT_DST2               24
#define SADB_X_EXT_POLICY             25
#define SADB_X_EXT_LOCAL_CREDENTIALS  26
#define SADB_X_EXT_REMOTE_CREDENTIALS 27
#define SADB_X_EXT_LOCAL_AUTH         28
#define SADB_X_EXT_REMOTE_AUTH        29
#define SADB_X_EXT_SUPPORTED_COMP     30
#define SADB_X_EXT_UDPENCAP           31
#define SADB_X_EXT_LIFETIME_LASTUSE   32
#define SADB_X_EXT_TAG                33
#define SADB_X_EXT_TAP                34
#define SADB_X_EXT_SATYPE2            35
#define SADB_X_EXT_COUNTER            36
#define SADB_X_EXT_RDOMAIN            37
#define SADB_X_EXT_MTU                38
#define SADB_X_EXT_REPLAY             39
#define SADB_X_EXT_IFACE              40
#define SADB_EXT_MAX                  40

/* Fix pfkeyv2.c struct pfkeyv2_socket if SATYPE_MAX > 31 */
#define SADB_SATYPE_UNSPEC		 0
#define SADB_SATYPE_AH			 1
#define SADB_SATYPE_ESP			 2
#define SADB_SATYPE_RSVP		 3
#define SADB_SATYPE_OSPFV2		 4
#define SADB_SATYPE_RIPV2		 5
#define SADB_SATYPE_MIP			 6
#define SADB_X_SATYPE_IPIP		 7
#define SADB_X_SATYPE_TCPSIGNATURE	 8
#define SADB_X_SATYPE_IPCOMP		 9
#define SADB_SATYPE_MAX			 9

#define SADB_SASTATE_LARVAL   0
#define SADB_SASTATE_MATURE   1
#define SADB_SASTATE_DYING    2
#define SADB_SASTATE_DEAD     3
#define SADB_SASTATE_MAX      3

#define SADB_AALG_NONE               0
#define SADB_AALG_MD5HMAC            2
#define SADB_AALG_SHA1HMAC           3
#define SADB_X_AALG_SHA2_256         5
#define SADB_X_AALG_SHA2_384         6
#define SADB_X_AALG_SHA2_512         7
#define SADB_X_AALG_RIPEMD160HMAC    8
#define SADB_X_AALG_AES128GMAC       9
#define SADB_X_AALG_AES192GMAC       10
#define SADB_X_AALG_AES256GMAC       11
#define SADB_X_AALG_CHACHA20POLY1305 12
#define SADB_AALG_MAX                12

#define SADB_EALG_NONE        0
#define SADB_EALG_3DESCBC     3
#define SADB_X_EALG_CAST      6
#define SADB_X_EALG_BLF       7
#define SADB_EALG_NULL        11
#define SADB_X_EALG_AES       12
#define SADB_X_EALG_AESCTR    13
#define SADB_X_EALG_AESGCM8   18
#define SADB_X_EALG_AESGCM12  19
#define SADB_X_EALG_AESGCM16  20
#define SADB_X_EALG_AESGMAC   21
#define SADB_X_EALG_CHACHA20POLY1305 22
#define SADB_EALG_MAX         22

#define SADB_X_CALG_NONE	0
#define SADB_X_CALG_OUI		1
#define SADB_X_CALG_DEFLATE	2
#define SADB_X_CALG_MAX		2

#define SADB_SAFLAGS_PFS		0x001	/* perfect forward secrecy */
#define SADB_X_SAFLAGS_TUNNEL		0x004	/* Force tunneling */
#define SADB_X_SAFLAGS_CHAINDEL		0x008	/* Delete whole SA chain */
#define SADB_X_SAFLAGS_UDPENCAP		0x200	/* ESP in UDP  */
#define SADB_X_SAFLAGS_ESN		0x400	/* Extended Sequence Number */

#define SADB_X_POLICYFLAGS_POLICY	0x0001	/* This is a static policy */

#define SADB_IDENTTYPE_RESERVED     0
#define SADB_IDENTTYPE_PREFIX       1
#define SADB_IDENTTYPE_FQDN         2
#define SADB_IDENTTYPE_USERFQDN     3
#define SADB_IDENTTYPE_ASN1_DN      4
#define SADB_IDENTTYPE_MAX          4

#define SADB_KEY_FLAGS_MAX 0

#ifdef _KERNEL
#define PFKEYV2_LIFETIME_HARD      0
#define PFKEYV2_LIFETIME_SOFT      1
#define PFKEYV2_LIFETIME_CURRENT   2
#define PFKEYV2_LIFETIME_LASTUSE   3

#define PFKEYV2_IDENTITY_SRC       0
#define PFKEYV2_IDENTITY_DST       1

#define PFKEYV2_ENCRYPTION_KEY     0
#define PFKEYV2_AUTHENTICATION_KEY 1

#define PFKEYV2_SOCKETFLAGS_REGISTERED 1
#define PFKEYV2_SOCKETFLAGS_PROMISC    2

#define PFKEYV2_SENDMESSAGE_UNICAST    1
#define PFKEYV2_SENDMESSAGE_REGISTERED 2
#define PFKEYV2_SENDMESSAGE_BROADCAST  3
#endif /* _KERNEL */

#define SADB_X_FLOW_TYPE_USE           1
#define SADB_X_FLOW_TYPE_ACQUIRE       2
#define SADB_X_FLOW_TYPE_REQUIRE       3
#define SADB_X_FLOW_TYPE_BYPASS        4
#define SADB_X_FLOW_TYPE_DENY          5
#define SADB_X_FLOW_TYPE_DONTACQ       6

#ifdef _KERNEL
struct tdb;
struct socket;
struct mbuf;

#define EXTLEN(x) (((struct sadb_ext *)(x))->sadb_ext_len * sizeof(uint64_t))
#define PADUP(x) (((x) + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1))

int pfkeyv2_parsemessage(void *, int, void **);
int pfkeyv2_expire(struct tdb *, u_int16_t);
int pfkeyv2_acquire(struct ipsec_policy *, union sockaddr_union *,
    union sockaddr_union *, u_int32_t *, struct sockaddr_encap *);

int pfkeyv2_get(struct tdb *, void **, void **, int *, int *);
int pfkeyv2_policy(struct ipsec_acquire *, void **, void **, int *);
int pfkeyv2_dosend(struct socket *, void *, int);
int pfkeyv2_sendmessage(void **, int, struct socket *, u_int8_t, int, u_int);
int pfkeyv2_dump_policy(struct ipsec_policy *, void **, void **, int *);
int pfkeyv2_dump_walker(struct tdb *, void *, int);
int pfkeyv2_get_proto_alg(u_int8_t, u_int8_t *, int *);
int pfkeyv2_sysctl(int *, u_int, void *, size_t *, void *, size_t);
int pfkeyv2_sysctl_walker(struct tdb *, void *, int);

int pfdatatopacket(void *, int, struct mbuf **);

void export_address(void **, struct sockaddr *);
void export_identities(void **, struct ipsec_ids *, int, void **);
void export_lifetime(void **, struct tdb *, int);
void export_sa(void **, struct tdb *);
void export_flow(void **, u_int8_t, struct sockaddr_encap *,
    struct sockaddr_encap *, void **);
void export_key(void **, struct tdb *, int);
void export_udpencap(void **, struct tdb *);
void export_tag(void **, struct tdb *);
void export_replay(void **, struct tdb *);
void export_rdomain(void **, struct tdb *);
void export_mtu(void **, struct tdb *);
void export_tap(void **, struct tdb *);
void export_satype(void **, struct tdb *);
void export_counter(void **, struct tdb *);
void export_iface(void **, struct tdb *);

void import_address(struct sockaddr *, struct sadb_address *);
void import_identities(struct ipsec_ids **, int, struct sadb_ident *,
    struct sadb_ident *);
void import_key(struct ipsecinit *, struct sadb_key *, int);
void import_lifetime(struct tdb *, struct sadb_lifetime *, int);
void import_sa(struct tdb *, struct sadb_sa *, struct ipsecinit *);
int import_flow(struct sockaddr_encap *, struct sockaddr_encap *,
    struct sadb_address *, struct sadb_address *, struct sadb_address *,
    struct sadb_address *, struct sadb_protocol *, struct sadb_protocol *);
void import_udpencap(struct tdb *, struct sadb_x_udpencap *);
void import_tag(struct tdb *, struct sadb_x_tag *);
void import_rdomain(struct tdb *, struct sadb_x_rdomain *);
void import_tap(struct tdb *, struct sadb_x_tap *);
void import_iface(struct tdb *, struct sadb_x_iface *);

extern const uint64_t sadb_exts_allowed_out[SADB_MAX+1];
extern const uint64_t sadb_exts_required_out[SADB_MAX+1];

extern struct pool ipsec_policy_pool;
extern struct pool ipsec_acquire_pool;
#endif /* _KERNEL */

#endif /* _NET_PFKEY_V2_H_ */
