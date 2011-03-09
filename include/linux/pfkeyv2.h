/* PF_KEY user interface, this is defined by rfc2367 so
 * do not make arbitrary modifications or else this header
 * file will not be compliant.
 */

#ifndef _LINUX_PFKEY2_H
#define _LINUX_PFKEY2_H

#include <linux/types.h>

#define PF_KEY_V2		2
#define PFKEYV2_REVISION	199806L

struct sadb_msg {
	__u8		sadb_msg_version;
	__u8		sadb_msg_type;
	__u8		sadb_msg_errno;
	__u8		sadb_msg_satype;
	__u16	sadb_msg_len;
	__u16	sadb_msg_reserved;
	__u32	sadb_msg_seq;
	__u32	sadb_msg_pid;
} __attribute__((packed));
/* sizeof(struct sadb_msg) == 16 */

struct sadb_ext {
	__u16	sadb_ext_len;
	__u16	sadb_ext_type;
} __attribute__((packed));
/* sizeof(struct sadb_ext) == 4 */

struct sadb_sa {
	__u16	sadb_sa_len;
	__u16	sadb_sa_exttype;
	__be32		sadb_sa_spi;
	__u8		sadb_sa_replay;
	__u8		sadb_sa_state;
	__u8		sadb_sa_auth;
	__u8		sadb_sa_encrypt;
	__u32	sadb_sa_flags;
} __attribute__((packed));
/* sizeof(struct sadb_sa) == 16 */

struct sadb_lifetime {
	__u16	sadb_lifetime_len;
	__u16	sadb_lifetime_exttype;
	__u32	sadb_lifetime_allocations;
	__u64	sadb_lifetime_bytes;
	__u64	sadb_lifetime_addtime;
	__u64	sadb_lifetime_usetime;
} __attribute__((packed));
/* sizeof(struct sadb_lifetime) == 32 */

struct sadb_address {
	__u16	sadb_address_len;
	__u16	sadb_address_exttype;
	__u8		sadb_address_proto;
	__u8		sadb_address_prefixlen;
	__u16	sadb_address_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_address) == 8 */

struct sadb_key {
	__u16	sadb_key_len;
	__u16	sadb_key_exttype;
	__u16	sadb_key_bits;
	__u16	sadb_key_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_key) == 8 */

struct sadb_ident {
	__u16	sadb_ident_len;
	__u16	sadb_ident_exttype;
	__u16	sadb_ident_type;
	__u16	sadb_ident_reserved;
	__u64	sadb_ident_id;
} __attribute__((packed));
/* sizeof(struct sadb_ident) == 16 */

struct sadb_sens {
	__u16	sadb_sens_len;
	__u16	sadb_sens_exttype;
	__u32	sadb_sens_dpd;
	__u8		sadb_sens_sens_level;
	__u8		sadb_sens_sens_len;
	__u8		sadb_sens_integ_level;
	__u8		sadb_sens_integ_len;
	__u32	sadb_sens_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_sens) == 16 */

/* followed by:
	__u64	sadb_sens_bitmap[sens_len];
	__u64	sadb_integ_bitmap[integ_len];  */

struct sadb_prop {
	__u16	sadb_prop_len;
	__u16	sadb_prop_exttype;
	__u8		sadb_prop_replay;
	__u8		sadb_prop_reserved[3];
} __attribute__((packed));
/* sizeof(struct sadb_prop) == 8 */

/* followed by:
	struct sadb_comb sadb_combs[(sadb_prop_len +
		sizeof(__u64) - sizeof(struct sadb_prop)) /
		sizeof(struct sadb_comb)]; */

struct sadb_comb {
	__u8		sadb_comb_auth;
	__u8		sadb_comb_encrypt;
	__u16	sadb_comb_flags;
	__u16	sadb_comb_auth_minbits;
	__u16	sadb_comb_auth_maxbits;
	__u16	sadb_comb_encrypt_minbits;
	__u16	sadb_comb_encrypt_maxbits;
	__u32	sadb_comb_reserved;
	__u32	sadb_comb_soft_allocations;
	__u32	sadb_comb_hard_allocations;
	__u64	sadb_comb_soft_bytes;
	__u64	sadb_comb_hard_bytes;
	__u64	sadb_comb_soft_addtime;
	__u64	sadb_comb_hard_addtime;
	__u64	sadb_comb_soft_usetime;
	__u64	sadb_comb_hard_usetime;
} __attribute__((packed));
/* sizeof(struct sadb_comb) == 72 */

struct sadb_supported {
	__u16	sadb_supported_len;
	__u16	sadb_supported_exttype;
	__u32	sadb_supported_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_supported) == 8 */

/* followed by:
	struct sadb_alg sadb_algs[(sadb_supported_len +
		sizeof(__u64) - sizeof(struct sadb_supported)) /
		sizeof(struct sadb_alg)]; */

struct sadb_alg {
	__u8		sadb_alg_id;
	__u8		sadb_alg_ivlen;
	__u16	sadb_alg_minbits;
	__u16	sadb_alg_maxbits;
	__u16	sadb_alg_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_alg) == 8 */

struct sadb_spirange {
	__u16	sadb_spirange_len;
	__u16	sadb_spirange_exttype;
	__u32	sadb_spirange_min;
	__u32	sadb_spirange_max;
	__u32	sadb_spirange_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_spirange) == 16 */

struct sadb_x_kmprivate {
	__u16	sadb_x_kmprivate_len;
	__u16	sadb_x_kmprivate_exttype;
	__u32	sadb_x_kmprivate_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_x_kmprivate) == 8 */

struct sadb_x_sa2 {
	__u16	sadb_x_sa2_len;
	__u16	sadb_x_sa2_exttype;
	__u8		sadb_x_sa2_mode;
	__u8		sadb_x_sa2_reserved1;
	__u16	sadb_x_sa2_reserved2;
	__u32	sadb_x_sa2_sequence;
	__u32	sadb_x_sa2_reqid;
} __attribute__((packed));
/* sizeof(struct sadb_x_sa2) == 16 */

struct sadb_x_policy {
	__u16	sadb_x_policy_len;
	__u16	sadb_x_policy_exttype;
	__u16	sadb_x_policy_type;
	__u8		sadb_x_policy_dir;
	__u8		sadb_x_policy_reserved;
	__u32	sadb_x_policy_id;
	__u32	sadb_x_policy_priority;
} __attribute__((packed));
/* sizeof(struct sadb_x_policy) == 16 */

struct sadb_x_ipsecrequest {
	__u16	sadb_x_ipsecrequest_len;
	__u16	sadb_x_ipsecrequest_proto;
	__u8		sadb_x_ipsecrequest_mode;
	__u8		sadb_x_ipsecrequest_level;
	__u16	sadb_x_ipsecrequest_reserved1;
	__u32	sadb_x_ipsecrequest_reqid;
	__u32	sadb_x_ipsecrequest_reserved2;
} __attribute__((packed));
/* sizeof(struct sadb_x_ipsecrequest) == 16 */

/* This defines the TYPE of Nat Traversal in use.  Currently only one
 * type of NAT-T is supported, draft-ietf-ipsec-udp-encaps-06
 */
struct sadb_x_nat_t_type {
	__u16	sadb_x_nat_t_type_len;
	__u16	sadb_x_nat_t_type_exttype;
	__u8		sadb_x_nat_t_type_type;
	__u8		sadb_x_nat_t_type_reserved[3];
} __attribute__((packed));
/* sizeof(struct sadb_x_nat_t_type) == 8 */

/* Pass a NAT Traversal port (Source or Dest port) */
struct sadb_x_nat_t_port {
	__u16	sadb_x_nat_t_port_len;
	__u16	sadb_x_nat_t_port_exttype;
	__be16		sadb_x_nat_t_port_port;
	__u16	sadb_x_nat_t_port_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_x_nat_t_port) == 8 */

/* Generic LSM security context */
struct sadb_x_sec_ctx {
	__u16	sadb_x_sec_len;
	__u16	sadb_x_sec_exttype;
	__u8		sadb_x_ctx_alg;  /* LSMs: e.g., selinux == 1 */
	__u8		sadb_x_ctx_doi;
	__u16	sadb_x_ctx_len;
} __attribute__((packed));
/* sizeof(struct sadb_sec_ctx) = 8 */

/* Used by MIGRATE to pass addresses IKE will use to perform
 * negotiation with the peer */
struct sadb_x_kmaddress {
	__u16	sadb_x_kmaddress_len;
	__u16	sadb_x_kmaddress_exttype;
	__u32	sadb_x_kmaddress_reserved;
} __attribute__((packed));
/* sizeof(struct sadb_x_kmaddress) == 8 */

/* Message types */
#define SADB_RESERVED		0
#define SADB_GETSPI		1
#define SADB_UPDATE		2
#define SADB_ADD		3
#define SADB_DELETE		4
#define SADB_GET		5
#define SADB_ACQUIRE		6
#define SADB_REGISTER		7
#define SADB_EXPIRE		8
#define SADB_FLUSH		9
#define SADB_DUMP		10
#define SADB_X_PROMISC		11
#define SADB_X_PCHANGE		12
#define SADB_X_SPDUPDATE	13
#define SADB_X_SPDADD		14
#define SADB_X_SPDDELETE	15
#define SADB_X_SPDGET		16
#define SADB_X_SPDACQUIRE	17
#define SADB_X_SPDDUMP		18
#define SADB_X_SPDFLUSH		19
#define SADB_X_SPDSETIDX	20
#define SADB_X_SPDEXPIRE	21
#define SADB_X_SPDDELETE2	22
#define SADB_X_NAT_T_NEW_MAPPING	23
#define SADB_X_MIGRATE		24
#define SADB_MAX		24

/* Security Association flags */
#define SADB_SAFLAGS_PFS	1
#define SADB_SAFLAGS_NOPMTUDISC	0x20000000
#define SADB_SAFLAGS_DECAP_DSCP	0x40000000
#define SADB_SAFLAGS_NOECN	0x80000000

/* Security Association states */
#define SADB_SASTATE_LARVAL	0
#define SADB_SASTATE_MATURE	1
#define SADB_SASTATE_DYING	2
#define SADB_SASTATE_DEAD	3
#define SADB_SASTATE_MAX	3

/* Security Association types */
#define SADB_SATYPE_UNSPEC	0
#define SADB_SATYPE_AH		2
#define SADB_SATYPE_ESP		3
#define SADB_SATYPE_RSVP	5
#define SADB_SATYPE_OSPFV2	6
#define SADB_SATYPE_RIPV2	7
#define SADB_SATYPE_MIP		8
#define SADB_X_SATYPE_IPCOMP	9
#define SADB_SATYPE_MAX		9

/* Authentication algorithms */
#define SADB_AALG_NONE			0
#define SADB_AALG_MD5HMAC		2
#define SADB_AALG_SHA1HMAC		3
#define SADB_X_AALG_SHA2_256HMAC	5
#define SADB_X_AALG_SHA2_384HMAC	6
#define SADB_X_AALG_SHA2_512HMAC	7
#define SADB_X_AALG_RIPEMD160HMAC	8
#define SADB_X_AALG_AES_XCBC_MAC	9
#define SADB_X_AALG_NULL		251	/* kame */
#define SADB_AALG_MAX			251

/* Encryption algorithms */
#define SADB_EALG_NONE			0
#define SADB_EALG_DESCBC		2
#define SADB_EALG_3DESCBC		3
#define SADB_X_EALG_CASTCBC		6
#define SADB_X_EALG_BLOWFISHCBC		7
#define SADB_EALG_NULL			11
#define SADB_X_EALG_AESCBC		12
#define SADB_X_EALG_AESCTR		13
#define SADB_X_EALG_AES_CCM_ICV8	14
#define SADB_X_EALG_AES_CCM_ICV12	15
#define SADB_X_EALG_AES_CCM_ICV16	16
#define SADB_X_EALG_AES_GCM_ICV8	18
#define SADB_X_EALG_AES_GCM_ICV12	19
#define SADB_X_EALG_AES_GCM_ICV16	20
#define SADB_X_EALG_CAMELLIACBC		22
#define SADB_X_EALG_NULL_AES_GMAC	23
#define SADB_EALG_MAX                   253 /* last EALG */
/* private allocations should use 249-255 (RFC2407) */
#define SADB_X_EALG_SERPENTCBC  252     /* draft-ietf-ipsec-ciph-aes-cbc-00 */
#define SADB_X_EALG_TWOFISHCBC  253     /* draft-ietf-ipsec-ciph-aes-cbc-00 */

/* Compression algorithms */
#define SADB_X_CALG_NONE		0
#define SADB_X_CALG_OUI			1
#define SADB_X_CALG_DEFLATE		2
#define SADB_X_CALG_LZS			3
#define SADB_X_CALG_LZJH		4
#define SADB_X_CALG_MAX			4

/* Extension Header values */
#define SADB_EXT_RESERVED		0
#define SADB_EXT_SA			1
#define SADB_EXT_LIFETIME_CURRENT	2
#define SADB_EXT_LIFETIME_HARD		3
#define SADB_EXT_LIFETIME_SOFT		4
#define SADB_EXT_ADDRESS_SRC		5
#define SADB_EXT_ADDRESS_DST		6
#define SADB_EXT_ADDRESS_PROXY		7
#define SADB_EXT_KEY_AUTH		8
#define SADB_EXT_KEY_ENCRYPT		9
#define SADB_EXT_IDENTITY_SRC		10
#define SADB_EXT_IDENTITY_DST		11
#define SADB_EXT_SENSITIVITY		12
#define SADB_EXT_PROPOSAL		13
#define SADB_EXT_SUPPORTED_AUTH		14
#define SADB_EXT_SUPPORTED_ENCRYPT	15
#define SADB_EXT_SPIRANGE		16
#define SADB_X_EXT_KMPRIVATE		17
#define SADB_X_EXT_POLICY		18
#define SADB_X_EXT_SA2			19
/* The next four entries are for setting up NAT Traversal */
#define SADB_X_EXT_NAT_T_TYPE		20
#define SADB_X_EXT_NAT_T_SPORT		21
#define SADB_X_EXT_NAT_T_DPORT		22
#define SADB_X_EXT_NAT_T_OA		23
#define SADB_X_EXT_SEC_CTX		24
/* Used with MIGRATE to pass @ to IKE for negotiation */
#define SADB_X_EXT_KMADDRESS		25
#define SADB_EXT_MAX			25

/* Identity Extension values */
#define SADB_IDENTTYPE_RESERVED	0
#define SADB_IDENTTYPE_PREFIX	1
#define SADB_IDENTTYPE_FQDN	2
#define SADB_IDENTTYPE_USERFQDN	3
#define SADB_IDENTTYPE_MAX	3

#endif /* !(_LINUX_PFKEY2_H) */
