/*
 * This is a module which is used for queueing IPv4 packets and
 * communicating with userspace via netlink.
 *
 * (C) 2000 James Morris, this code is GPL.
 */
#ifndef _IP_QUEUE_H
#define _IP_QUEUE_H

#ifdef __KERNEL__
#ifdef DEBUG_IPQ
#define QDEBUG(x...) printk(KERN_DEBUG ## x)
#else
#define QDEBUG(x...)
#endif  /* DEBUG_IPQ */
#else
#include <net/if.h>
#endif	/* ! __KERNEL__ */

/* Messages sent from kernel */
typedef struct ipq_packet_msg {
	unsigned long packet_id;	/* ID of queued packet */
	unsigned long mark;		/* Netfilter mark value */
	long timestamp_sec;		/* Packet arrival time (seconds) */
	long timestamp_usec;		/* Packet arrvial time (+useconds) */
	unsigned int hook;		/* Netfilter hook we rode in on */
	char indev_name[IFNAMSIZ];	/* Name of incoming interface */
	char outdev_name[IFNAMSIZ];	/* Name of outgoing interface */
	__be16 hw_protocol;		/* Hardware protocol (network order) */
	unsigned short hw_type;		/* Hardware type */
	unsigned char hw_addrlen;	/* Hardware address length */
	unsigned char hw_addr[8];	/* Hardware address */
	size_t data_len;		/* Length of packet data */
	unsigned char payload[0];	/* Optional packet data */
} ipq_packet_msg_t;

/* Messages sent from userspace */
typedef struct ipq_mode_msg {
	unsigned char value;		/* Requested mode */
	size_t range;			/* Optional range of packet requested */
} ipq_mode_msg_t;

typedef struct ipq_verdict_msg {
	unsigned int value;		/* Verdict to hand to netfilter */
	unsigned long id;		/* Packet ID for this verdict */
	size_t data_len;		/* Length of replacement data */
	unsigned char payload[0];	/* Optional replacement packet */
} ipq_verdict_msg_t;

typedef struct ipq_peer_msg {
	union {
		ipq_verdict_msg_t verdict;
		ipq_mode_msg_t mode;
	} msg;
} ipq_peer_msg_t;

/* Packet delivery modes */
enum {
	IPQ_COPY_NONE,		/* Initial mode, packets are dropped */
	IPQ_COPY_META,		/* Copy metadata */
	IPQ_COPY_PACKET		/* Copy metadata + packet (range) */
};
#define IPQ_COPY_MAX IPQ_COPY_PACKET

/* Types of messages */
#define IPQM_BASE	0x10	/* standard netlink messages below this */
#define IPQM_MODE	(IPQM_BASE + 1)		/* Mode request from peer */
#define IPQM_VERDICT	(IPQM_BASE + 2)		/* Verdict from peer */ 
#define IPQM_PACKET	(IPQM_BASE + 3)		/* Packet from kernel */
#define IPQM_MAX	(IPQM_BASE + 4)

#endif /*_IP_QUEUE_H*/
