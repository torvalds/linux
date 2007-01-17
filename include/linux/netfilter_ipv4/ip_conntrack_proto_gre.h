#ifndef _CONNTRACK_PROTO_GRE_H
#define _CONNTRACK_PROTO_GRE_H
#include <asm/byteorder.h>

/* GRE PROTOCOL HEADER */

/* GRE Version field */
#define GRE_VERSION_1701	0x0
#define GRE_VERSION_PPTP	0x1

/* GRE Protocol field */
#define GRE_PROTOCOL_PPTP	0x880B

/* GRE Flags */
#define GRE_FLAG_C		0x80
#define GRE_FLAG_R		0x40
#define GRE_FLAG_K		0x20
#define GRE_FLAG_S		0x10
#define GRE_FLAG_A		0x80

#define GRE_IS_C(f)	((f)&GRE_FLAG_C)
#define GRE_IS_R(f)	((f)&GRE_FLAG_R)
#define GRE_IS_K(f)	((f)&GRE_FLAG_K)
#define GRE_IS_S(f)	((f)&GRE_FLAG_S)
#define GRE_IS_A(f)	((f)&GRE_FLAG_A)

/* GRE is a mess: Four different standards */
struct gre_hdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u16	rec:3,
		srr:1,
		seq:1,
		key:1,
		routing:1,
		csum:1,
		version:3,
		reserved:4,
		ack:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u16	csum:1,
		routing:1,
		key:1,
		seq:1,
		srr:1,
		rec:3,
		ack:1,
		reserved:4,
		version:3;
#else
#error "Adjust your <asm/byteorder.h> defines"
#endif
	__be16	protocol;
};

/* modified GRE header for PPTP */
struct gre_hdr_pptp {
	__u8   flags;		/* bitfield */
	__u8   version;		/* should be GRE_VERSION_PPTP */
	__be16 protocol;	/* should be GRE_PROTOCOL_PPTP */
	__be16 payload_len;	/* size of ppp payload, not inc. gre header */
	__be16 call_id;		/* peer's call_id for this session */
	__be32 seq;		/* sequence number.  Present if S==1 */
	__be32 ack;		/* seq number of highest packet recieved by */
				/*  sender in this session */
};


/* this is part of ip_conntrack */
struct ip_ct_gre {
	unsigned int stream_timeout;
	unsigned int timeout;
};

#ifdef __KERNEL__
struct ip_conntrack_expect;
struct ip_conntrack;

/* structure for original <-> reply keymap */
struct ip_ct_gre_keymap {
	struct list_head list;

	struct ip_conntrack_tuple tuple;
};

/* add new tuple->key_reply pair to keymap */
int ip_ct_gre_keymap_add(struct ip_conntrack *ct,
			 struct ip_conntrack_tuple *t,
			 int reply);

/* delete keymap entries */
void ip_ct_gre_keymap_destroy(struct ip_conntrack *ct);


/* get pointer to gre key, if present */
static inline __be32 *gre_key(struct gre_hdr *greh)
{
	if (!greh->key)
		return NULL;
	if (greh->csum || greh->routing)
		return (__be32 *) (greh+sizeof(*greh)+4);
	return (__be32 *) (greh+sizeof(*greh));
}

/* get pointer ot gre csum, if present */
static inline __sum16 *gre_csum(struct gre_hdr *greh)
{
	if (!greh->csum)
		return NULL;
	return (__sum16 *) (greh+sizeof(*greh));
}

#endif /* __KERNEL__ */

#endif /* _CONNTRACK_PROTO_GRE_H */
