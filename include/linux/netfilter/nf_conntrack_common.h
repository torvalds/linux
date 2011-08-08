#ifndef _NF_CONNTRACK_COMMON_H
#define _NF_CONNTRACK_COMMON_H
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */
enum ip_conntrack_info {
	/* Part of an established connection (either direction). */
	IP_CT_ESTABLISHED,

	/* Like NEW, but related to an existing connection, or ICMP error
	   (in either direction). */
	IP_CT_RELATED,

	/* Started a new connection to track (only
           IP_CT_DIR_ORIGINAL); may be a retransmission. */
	IP_CT_NEW,

	/* >= this indicates reply direction */
	IP_CT_IS_REPLY,

	IP_CT_ESTABLISHED_REPLY = IP_CT_ESTABLISHED + IP_CT_IS_REPLY,
	IP_CT_RELATED_REPLY = IP_CT_RELATED + IP_CT_IS_REPLY,
	IP_CT_NEW_REPLY = IP_CT_NEW + IP_CT_IS_REPLY,	
	/* Number of distinct IP_CT types (no NEW in reply dirn). */
	IP_CT_NUMBER = IP_CT_IS_REPLY * 2 - 1
};

/* Bitset representing status of connection. */
enum ip_conntrack_status {
	/* It's an expected connection: bit 0 set.  This bit never changed */
	IPS_EXPECTED_BIT = 0,
	IPS_EXPECTED = (1 << IPS_EXPECTED_BIT),

	/* We've seen packets both ways: bit 1 set.  Can be set, not unset. */
	IPS_SEEN_REPLY_BIT = 1,
	IPS_SEEN_REPLY = (1 << IPS_SEEN_REPLY_BIT),

	/* Conntrack should never be early-expired. */
	IPS_ASSURED_BIT = 2,
	IPS_ASSURED = (1 << IPS_ASSURED_BIT),

	/* Connection is confirmed: originating packet has left box */
	IPS_CONFIRMED_BIT = 3,
	IPS_CONFIRMED = (1 << IPS_CONFIRMED_BIT),

	/* Connection needs src nat in orig dir.  This bit never changed. */
	IPS_SRC_NAT_BIT = 4,
	IPS_SRC_NAT = (1 << IPS_SRC_NAT_BIT),

	/* Connection needs dst nat in orig dir.  This bit never changed. */
	IPS_DST_NAT_BIT = 5,
	IPS_DST_NAT = (1 << IPS_DST_NAT_BIT),

	/* Both together. */
	IPS_NAT_MASK = (IPS_DST_NAT | IPS_SRC_NAT),

	/* Connection needs TCP sequence adjusted. */
	IPS_SEQ_ADJUST_BIT = 6,
	IPS_SEQ_ADJUST = (1 << IPS_SEQ_ADJUST_BIT),

	/* NAT initialization bits. */
	IPS_SRC_NAT_DONE_BIT = 7,
	IPS_SRC_NAT_DONE = (1 << IPS_SRC_NAT_DONE_BIT),

	IPS_DST_NAT_DONE_BIT = 8,
	IPS_DST_NAT_DONE = (1 << IPS_DST_NAT_DONE_BIT),

	/* Both together */
	IPS_NAT_DONE_MASK = (IPS_DST_NAT_DONE | IPS_SRC_NAT_DONE),

	/* Connection is dying (removed from lists), can not be unset. */
	IPS_DYING_BIT = 9,
	IPS_DYING = (1 << IPS_DYING_BIT),

	/* Connection has fixed timeout. */
	IPS_FIXED_TIMEOUT_BIT = 10,
	IPS_FIXED_TIMEOUT = (1 << IPS_FIXED_TIMEOUT_BIT),

	/* Conntrack is a template */
	IPS_TEMPLATE_BIT = 11,
	IPS_TEMPLATE = (1 << IPS_TEMPLATE_BIT),

	/* Conntrack is a fake untracked entry */
	IPS_UNTRACKED_BIT = 12,
	IPS_UNTRACKED = (1 << IPS_UNTRACKED_BIT),
};

/* Connection tracking event types */
enum ip_conntrack_events {
	IPCT_NEW,		/* new conntrack */
	IPCT_RELATED,		/* related conntrack */
	IPCT_DESTROY,		/* destroyed conntrack */
	IPCT_REPLY,		/* connection has seen two-way traffic */
	IPCT_ASSURED,		/* connection status has changed to assured */
	IPCT_PROTOINFO,		/* protocol information has changed */
	IPCT_HELPER,		/* new helper has been set */
	IPCT_MARK,		/* new mark has been set */
	IPCT_NATSEQADJ,		/* NAT is doing sequence adjustment */
	IPCT_SECMARK,		/* new security mark has been set */
};

enum ip_conntrack_expect_events {
	IPEXP_NEW,		/* new expectation */
	IPEXP_DESTROY,		/* destroyed expectation */
};

/* expectation flags */
#define NF_CT_EXPECT_PERMANENT		0x1
#define NF_CT_EXPECT_INACTIVE		0x2
#define NF_CT_EXPECT_USERSPACE		0x4

#ifdef __KERNEL__
struct ip_conntrack_stat {
	unsigned int searched;
	unsigned int found;
	unsigned int new;
	unsigned int invalid;
	unsigned int ignore;
	unsigned int delete;
	unsigned int delete_list;
	unsigned int insert;
	unsigned int insert_failed;
	unsigned int drop;
	unsigned int early_drop;
	unsigned int error;
	unsigned int expect_new;
	unsigned int expect_create;
	unsigned int expect_delete;
	unsigned int search_restart;
};

/* call to create an explicit dependency on nf_conntrack. */
extern void need_conntrack(void);

#endif /* __KERNEL__ */

#endif /* _NF_CONNTRACK_COMMON_H */
