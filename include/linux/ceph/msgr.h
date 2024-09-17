/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CEPH_MSGR_H
#define CEPH_MSGR_H

/*
 * Data types for message passing layer used by Ceph.
 */

#define CEPH_MON_PORT    6789  /* default monitor port */

/*
 * tcp connection banner.  include a protocol version. and adjust
 * whenever the wire protocol changes.  try to keep this string length
 * constant.
 */
#define CEPH_BANNER "ceph v027"
#define CEPH_BANNER_LEN 9
#define CEPH_BANNER_MAX_LEN 30


/*
 * messenger V2 connection banner prefix.
 * The full banner string should have the form: "ceph v2\n<le16>"
 * the 2 bytes are the length of the remaining banner.
 */
#define CEPH_BANNER_V2 "ceph v2\n"
#define CEPH_BANNER_V2_LEN 8
#define CEPH_BANNER_V2_PREFIX_LEN (CEPH_BANNER_V2_LEN + sizeof(__le16))

/*
 * messenger V2 features
 */
#define CEPH_MSGR2_INCARNATION_1 (0ull)

#define DEFINE_MSGR2_FEATURE(bit, incarnation, name)               \
	static const uint64_t __maybe_unused CEPH_MSGR2_FEATURE_##name = (1ULL << bit); \
	static const uint64_t __maybe_unused CEPH_MSGR2_FEATUREMASK_##name =            \
			(1ULL << bit | CEPH_MSGR2_INCARNATION_##incarnation);

#define HAVE_MSGR2_FEATURE(x, name) \
	(((x) & (CEPH_MSGR2_FEATUREMASK_##name)) == (CEPH_MSGR2_FEATUREMASK_##name))

DEFINE_MSGR2_FEATURE( 0, 1, REVISION_1)   // msgr2.1

#define CEPH_MSGR2_SUPPORTED_FEATURES (CEPH_MSGR2_FEATURE_REVISION_1)

#define CEPH_MSGR2_REQUIRED_FEATURES  (CEPH_MSGR2_FEATURE_REVISION_1)


/*
 * Rollover-safe type and comparator for 32-bit sequence numbers.
 * Comparator returns -1, 0, or 1.
 */
typedef __u32 ceph_seq_t;

static inline __s32 ceph_seq_cmp(__u32 a, __u32 b)
{
       return (__s32)a - (__s32)b;
}


/*
 * entity_name -- logical name for a process participating in the
 * network, e.g. 'mds0' or 'osd3'.
 */
struct ceph_entity_name {
	__u8 type;      /* CEPH_ENTITY_TYPE_* */
	__le64 num;
} __attribute__ ((packed));

#define CEPH_ENTITY_TYPE_MON    0x01
#define CEPH_ENTITY_TYPE_MDS    0x02
#define CEPH_ENTITY_TYPE_OSD    0x04
#define CEPH_ENTITY_TYPE_CLIENT 0x08
#define CEPH_ENTITY_TYPE_AUTH   0x20

#define CEPH_ENTITY_TYPE_ANY    0xFF

extern const char *ceph_entity_type_name(int type);

/*
 * entity_addr -- network address
 */
struct ceph_entity_addr {
	__le32 type;  /* CEPH_ENTITY_ADDR_TYPE_* */
	__le32 nonce;  /* unique id for process (e.g. pid) */
	struct sockaddr_storage in_addr;
} __attribute__ ((packed));

static inline bool ceph_addr_equal_no_type(const struct ceph_entity_addr *lhs,
					   const struct ceph_entity_addr *rhs)
{
	return !memcmp(&lhs->in_addr, &rhs->in_addr, sizeof(lhs->in_addr)) &&
	       lhs->nonce == rhs->nonce;
}

struct ceph_entity_inst {
	struct ceph_entity_name name;
	struct ceph_entity_addr addr;
} __attribute__ ((packed));


/* used by message exchange protocol */
#define CEPH_MSGR_TAG_READY         1  /* server->client: ready for messages */
#define CEPH_MSGR_TAG_RESETSESSION  2  /* server->client: reset, try again */
#define CEPH_MSGR_TAG_WAIT          3  /* server->client: wait for racing
					  incoming connection */
#define CEPH_MSGR_TAG_RETRY_SESSION 4  /* server->client + cseq: try again
					  with higher cseq */
#define CEPH_MSGR_TAG_RETRY_GLOBAL  5  /* server->client + gseq: try again
					  with higher gseq */
#define CEPH_MSGR_TAG_CLOSE         6  /* closing pipe */
#define CEPH_MSGR_TAG_MSG           7  /* message */
#define CEPH_MSGR_TAG_ACK           8  /* message ack */
#define CEPH_MSGR_TAG_KEEPALIVE     9  /* just a keepalive byte! */
#define CEPH_MSGR_TAG_BADPROTOVER   10 /* bad protocol version */
#define CEPH_MSGR_TAG_BADAUTHORIZER 11 /* bad authorizer */
#define CEPH_MSGR_TAG_FEATURES      12 /* insufficient features */
#define CEPH_MSGR_TAG_SEQ           13 /* 64-bit int follows with seen seq number */
#define CEPH_MSGR_TAG_KEEPALIVE2    14 /* keepalive2 byte + ceph_timespec */
#define CEPH_MSGR_TAG_KEEPALIVE2_ACK 15 /* keepalive2 reply */
#define CEPH_MSGR_TAG_CHALLENGE_AUTHORIZER 16  /* cephx v2 doing server challenge */

/*
 * connection negotiation
 */
struct ceph_msg_connect {
	__le64 features;     /* supported feature bits */
	__le32 host_type;    /* CEPH_ENTITY_TYPE_* */
	__le32 global_seq;   /* count connections initiated by this host */
	__le32 connect_seq;  /* count connections initiated in this session */
	__le32 protocol_version;
	__le32 authorizer_protocol;
	__le32 authorizer_len;
	__u8  flags;         /* CEPH_MSG_CONNECT_* */
} __attribute__ ((packed));

struct ceph_msg_connect_reply {
	__u8 tag;
	__le64 features;     /* feature bits for this session */
	__le32 global_seq;
	__le32 connect_seq;
	__le32 protocol_version;
	__le32 authorizer_len;
	__u8 flags;
} __attribute__ ((packed));

#define CEPH_MSG_CONNECT_LOSSY  1  /* messages i send may be safely dropped */


/*
 * message header
 */
struct ceph_msg_header_old {
	__le64 seq;       /* message seq# for this session */
	__le64 tid;       /* transaction id */
	__le16 type;      /* message type */
	__le16 priority;  /* priority.  higher value == higher priority */
	__le16 version;   /* version of message encoding */

	__le32 front_len; /* bytes in main payload */
	__le32 middle_len;/* bytes in middle payload */
	__le32 data_len;  /* bytes of data payload */
	__le16 data_off;  /* sender: include full offset;
			     receiver: mask against ~PAGE_MASK */

	struct ceph_entity_inst src, orig_src;
	__le32 reserved;
	__le32 crc;       /* header crc32c */
} __attribute__ ((packed));

struct ceph_msg_header {
	__le64 seq;       /* message seq# for this session */
	__le64 tid;       /* transaction id */
	__le16 type;      /* message type */
	__le16 priority;  /* priority.  higher value == higher priority */
	__le16 version;   /* version of message encoding */

	__le32 front_len; /* bytes in main payload */
	__le32 middle_len;/* bytes in middle payload */
	__le32 data_len;  /* bytes of data payload */
	__le16 data_off;  /* sender: include full offset;
			     receiver: mask against ~PAGE_MASK */

	struct ceph_entity_name src;
	__le16 compat_version;
	__le16 reserved;
	__le32 crc;       /* header crc32c */
} __attribute__ ((packed));

struct ceph_msg_header2 {
	__le64 seq;       /* message seq# for this session */
	__le64 tid;       /* transaction id */
	__le16 type;      /* message type */
	__le16 priority;  /* priority.  higher value == higher priority */
	__le16 version;   /* version of message encoding */

	__le32 data_pre_padding_len;
	__le16 data_off;  /* sender: include full offset;
			     receiver: mask against ~PAGE_MASK */

	__le64 ack_seq;
	__u8 flags;
	/* oldest code we think can decode this.  unknown if zero. */
	__le16 compat_version;
	__le16 reserved;
} __attribute__ ((packed));

#define CEPH_MSG_PRIO_LOW     64
#define CEPH_MSG_PRIO_DEFAULT 127
#define CEPH_MSG_PRIO_HIGH    196
#define CEPH_MSG_PRIO_HIGHEST 255

/*
 * follows data payload
 */
struct ceph_msg_footer_old {
	__le32 front_crc, middle_crc, data_crc;
	__u8 flags;
} __attribute__ ((packed));

struct ceph_msg_footer {
	__le32 front_crc, middle_crc, data_crc;
	// sig holds the 64 bits of the digital signature for the message PLR
	__le64  sig;
	__u8 flags;
} __attribute__ ((packed));

#define CEPH_MSG_FOOTER_COMPLETE  (1<<0)   /* msg wasn't aborted */
#define CEPH_MSG_FOOTER_NOCRC     (1<<1)   /* no data crc */
#define CEPH_MSG_FOOTER_SIGNED	  (1<<2)   /* msg was signed */


#endif
