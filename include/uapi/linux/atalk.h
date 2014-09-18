#ifndef _UAPI__LINUX_ATALK_H__
#define _UAPI__LINUX_ATALK_H__

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/socket.h>

/*
 * AppleTalk networking structures
 *
 * The following are directly referenced from the University Of Michigan
 * netatalk for compatibility reasons.
 */
#define ATPORT_FIRST	1
#define ATPORT_RESERVED	128
#define ATPORT_LAST	254		/* 254 is only legal on localtalk */ 
#define ATADDR_ANYNET	(__u16)0
#define ATADDR_ANYNODE	(__u8)0
#define ATADDR_ANYPORT  (__u8)0
#define ATADDR_BCAST	(__u8)255
#define DDP_MAXSZ	587
#define DDP_MAXHOPS     15		/* 4 bits of hop counter */

#define SIOCATALKDIFADDR       (SIOCPROTOPRIVATE + 0)

struct atalk_addr {
	__be16	s_net;
	__u8	s_node;
};

struct sockaddr_at {
	__kernel_sa_family_t sat_family;
	__u8		  sat_port;
	struct atalk_addr sat_addr;
	char		  sat_zero[8];
};

struct atalk_netrange {
	__u8	nr_phase;
	__be16	nr_firstnet;
	__be16	nr_lastnet;
};

#endif /* _UAPI__LINUX_ATALK_H__ */
