#ifndef _NF_CONNTRACK_COMMON_H
#define _NF_CONNTRACK_COMMON_H
/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */
enum ip_conntrack_info
{
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
};

/* Connection tracking event bits */
enum ip_conntrack_events
{
	/* New conntrack */
	IPCT_NEW_BIT = 0,
	IPCT_NEW = (1 << IPCT_NEW_BIT),

	/* Expected connection */
	IPCT_RELATED_BIT = 1,
	IPCT_RELATED = (1 << IPCT_RELATED_BIT),

	/* Destroyed conntrack */
	IPCT_DESTROY_BIT = 2,
	IPCT_DESTROY = (1 << IPCT_DESTROY_BIT),

	/* Timer has been refreshed */
	IPCT_REFRESH_BIT = 3,
	IPCT_REFRESH = (1 << IPCT_REFRESH_BIT),

	/* Status has changed */
	IPCT_STATUS_BIT = 4,
	IPCT_STATUS = (1 << IPCT_STATUS_BIT),

	/* Update of protocol info */
	IPCT_PROTOINFO_BIT = 5,
	IPCT_PROTOINFO = (1 << IPCT_PROTOINFO_BIT),

	/* Volatile protocol info */
	IPCT_PROTOINFO_VOLATILE_BIT = 6,
	IPCT_PROTOINFO_VOLATILE = (1 << IPCT_PROTOINFO_VOLATILE_BIT),

	/* New helper for conntrack */
	IPCT_HELPER_BIT = 7,
	IPCT_HELPER = (1 << IPCT_HELPER_BIT),

	/* Update of helper info */
	IPCT_HELPINFO_BIT = 8,
	IPCT_HELPINFO = (1 << IPCT_HELPINFO_BIT),

	/* Volatile helper info */
	IPCT_HELPINFO_VOLATILE_BIT = 9,
	IPCT_HELPINFO_VOLATILE = (1 << IPCT_HELPINFO_VOLATILE_BIT),

	/* NAT info */
	IPCT_NATINFO_BIT = 10,
	IPCT_NATINFO = (1 << IPCT_NATINFO_BIT),

	/* Counter highest bit has been set */
	IPCT_COUNTER_FILLING_BIT = 11,
	IPCT_COUNTER_FILLING = (1 << IPCT_COUNTER_FILLING_BIT),
};

enum ip_conntrack_expect_events {
	IPEXP_NEW_BIT = 0,
	IPEXP_NEW = (1 << IPEXP_NEW_BIT),
};

#ifdef __KERNEL__
struct ip_conntrack_counter
{
	u_int32_t packets;
	u_int32_t bytes;
};

struct ip_conntrack_stat
{
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
};

/* call to create an explicit dependency on nf_conntrack. */
extern void need_conntrack(void);

#endif /* __KERNEL__ */

#endif /* _NF_CONNTRACK_COMMON_H */
