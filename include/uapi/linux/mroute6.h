#ifndef _UAPI__LINUX_MROUTE6_H
#define _UAPI__LINUX_MROUTE6_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sockios.h>
#include <linux/in6.h>		/* For struct sockaddr_in6. */

/*
 *	Based on the MROUTING 3.5 defines primarily to keep
 *	source compatibility with BSD.
 *
 *	See the pim6sd code for the original history.
 *
 *      Protocol Independent Multicast (PIM) data structures included
 *      Carlos Picoto (cap@di.fc.ul.pt)
 *
 */

#define MRT6_BASE	200
#define MRT6_INIT	(MRT6_BASE)	/* Activate the kernel mroute code 	*/
#define MRT6_DONE	(MRT6_BASE+1)	/* Shutdown the kernel mroute		*/
#define MRT6_ADD_MIF	(MRT6_BASE+2)	/* Add a virtual interface		*/
#define MRT6_DEL_MIF	(MRT6_BASE+3)	/* Delete a virtual interface		*/
#define MRT6_ADD_MFC	(MRT6_BASE+4)	/* Add a multicast forwarding entry	*/
#define MRT6_DEL_MFC	(MRT6_BASE+5)	/* Delete a multicast forwarding entry	*/
#define MRT6_VERSION	(MRT6_BASE+6)	/* Get the kernel multicast version	*/
#define MRT6_ASSERT	(MRT6_BASE+7)	/* Activate PIM assert mode		*/
#define MRT6_PIM	(MRT6_BASE+8)	/* enable PIM code			*/
#define MRT6_TABLE	(MRT6_BASE+9)	/* Specify mroute table ID		*/
#define MRT6_ADD_MFC_PROXY	(MRT6_BASE+10)	/* Add a (*,*|G) mfc entry	*/
#define MRT6_DEL_MFC_PROXY	(MRT6_BASE+11)	/* Del a (*,*|G) mfc entry	*/
#define MRT6_MAX	(MRT6_BASE+11)

#define SIOCGETMIFCNT_IN6	SIOCPROTOPRIVATE	/* IP protocol privates */
#define SIOCGETSGCNT_IN6	(SIOCPROTOPRIVATE+1)
#define SIOCGETRPF	(SIOCPROTOPRIVATE+2)

#define MAXMIFS		32
typedef unsigned long mifbitmap_t;	/* User mode code depends on this lot */
typedef unsigned short mifi_t;
#define ALL_MIFS	((mifi_t)(-1))

#ifndef IF_SETSIZE
#define IF_SETSIZE	256
#endif

typedef	__u32		if_mask;
#define NIFBITS (sizeof(if_mask) * 8)        /* bits per mask */

typedef struct if_set {
	if_mask ifs_bits[__KERNEL_DIV_ROUND_UP(IF_SETSIZE, NIFBITS)];
} if_set;

#define IF_SET(n, p)    ((p)->ifs_bits[(n)/NIFBITS] |= (1 << ((n) % NIFBITS)))
#define IF_CLR(n, p)    ((p)->ifs_bits[(n)/NIFBITS] &= ~(1 << ((n) % NIFBITS)))
#define IF_ISSET(n, p)  ((p)->ifs_bits[(n)/NIFBITS] & (1 << ((n) % NIFBITS)))
#define IF_COPY(f, t)   bcopy(f, t, sizeof(*(f)))
#define IF_ZERO(p)      bzero(p, sizeof(*(p)))

/*
 *	Passed by mrouted for an MRT_ADD_MIF - again we use the
 *	mrouted 3.6 structures for compatibility
 */

struct mif6ctl {
	mifi_t	mif6c_mifi;		/* Index of MIF */
	unsigned char mif6c_flags;	/* MIFF_ flags */
	unsigned char vifc_threshold;	/* ttl limit */
	__u16	 mif6c_pifi;		/* the index of the physical IF */
	unsigned int vifc_rate_limit;	/* Rate limiter values (NI) */
};

#define MIFF_REGISTER	0x1	/* register vif	*/

/*
 *	Cache manipulation structures for mrouted and PIMd
 */

struct mf6cctl {
	struct sockaddr_in6 mf6cc_origin;		/* Origin of mcast	*/
	struct sockaddr_in6 mf6cc_mcastgrp;		/* Group in question	*/
	mifi_t	mf6cc_parent;			/* Where it arrived	*/
	struct if_set mf6cc_ifset;		/* Where it is going */
};

/*
 *	Group count retrieval for pim6sd
 */

struct sioc_sg_req6 {
	struct sockaddr_in6 src;
	struct sockaddr_in6 grp;
	unsigned long pktcnt;
	unsigned long bytecnt;
	unsigned long wrong_if;
};

/*
 *	To get vif packet counts
 */

struct sioc_mif_req6 {
	mifi_t	mifi;		/* Which iface */
	unsigned long icount;	/* In packets */
	unsigned long ocount;	/* Out packets */
	unsigned long ibytes;	/* In bytes */
	unsigned long obytes;	/* Out bytes */
};

/*
 *	That's all usermode folks
 */



/*
 * Structure used to communicate from kernel to multicast router.
 * We'll overlay the structure onto an MLD header (not an IPv6 heder like igmpmsg{}
 * used for IPv4 implementation). This is because this structure will be passed via an
 * IPv6 raw socket, on which an application will only receiver the payload i.e the data after
 * the IPv6 header and all the extension headers. (See section 3 of RFC 3542)
 */

struct mrt6msg {
#define MRT6MSG_NOCACHE		1
#define MRT6MSG_WRONGMIF	2
#define MRT6MSG_WHOLEPKT	3		/* used for use level encap */
	__u8		im6_mbz;		/* must be zero		   */
	__u8		im6_msgtype;		/* what type of message    */
	__u16		im6_mif;		/* mif rec'd on		   */
	__u32		im6_pad;		/* padding for 64 bit arch */
	struct in6_addr	im6_src, im6_dst;
};

/* ip6mr netlink cache report attributes */
enum {
	IP6MRA_CREPORT_UNSPEC,
	IP6MRA_CREPORT_MSGTYPE,
	IP6MRA_CREPORT_MIF_ID,
	IP6MRA_CREPORT_SRC_ADDR,
	IP6MRA_CREPORT_DST_ADDR,
	IP6MRA_CREPORT_PKT,
	__IP6MRA_CREPORT_MAX
};
#define IP6MRA_CREPORT_MAX (__IP6MRA_CREPORT_MAX - 1)

#endif /* _UAPI__LINUX_MROUTE6_H */
