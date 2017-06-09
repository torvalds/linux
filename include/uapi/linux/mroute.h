#ifndef _UAPI__LINUX_MROUTE_H
#define _UAPI__LINUX_MROUTE_H

#include <linux/sockios.h>
#include <linux/types.h>
#include <linux/in.h>		/* For struct in_addr. */

/* Based on the MROUTING 3.5 defines primarily to keep
 * source compatibility with BSD.
 *
 * See the mrouted code for the original history.
 *
 * Protocol Independent Multicast (PIM) data structures included
 * Carlos Picoto (cap@di.fc.ul.pt)
 */

#define MRT_BASE	200
#define MRT_INIT	(MRT_BASE)	/* Activate the kernel mroute code 	*/
#define MRT_DONE	(MRT_BASE+1)	/* Shutdown the kernel mroute		*/
#define MRT_ADD_VIF	(MRT_BASE+2)	/* Add a virtual interface		*/
#define MRT_DEL_VIF	(MRT_BASE+3)	/* Delete a virtual interface		*/
#define MRT_ADD_MFC	(MRT_BASE+4)	/* Add a multicast forwarding entry	*/
#define MRT_DEL_MFC	(MRT_BASE+5)	/* Delete a multicast forwarding entry	*/
#define MRT_VERSION	(MRT_BASE+6)	/* Get the kernel multicast version	*/
#define MRT_ASSERT	(MRT_BASE+7)	/* Activate PIM assert mode		*/
#define MRT_PIM		(MRT_BASE+8)	/* enable PIM code			*/
#define MRT_TABLE	(MRT_BASE+9)	/* Specify mroute table ID		*/
#define MRT_ADD_MFC_PROXY	(MRT_BASE+10)	/* Add a (*,*|G) mfc entry	*/
#define MRT_DEL_MFC_PROXY	(MRT_BASE+11)	/* Del a (*,*|G) mfc entry	*/
#define MRT_MAX		(MRT_BASE+11)

#define SIOCGETVIFCNT	SIOCPROTOPRIVATE	/* IP protocol privates */
#define SIOCGETSGCNT	(SIOCPROTOPRIVATE+1)
#define SIOCGETRPF	(SIOCPROTOPRIVATE+2)

#define MAXVIFS		32
typedef unsigned long vifbitmap_t;	/* User mode code depends on this lot */
typedef unsigned short vifi_t;
#define ALL_VIFS	((vifi_t)(-1))

/* Same idea as select */

#define VIFM_SET(n,m)	((m)|=(1<<(n)))
#define VIFM_CLR(n,m)	((m)&=~(1<<(n)))
#define VIFM_ISSET(n,m)	((m)&(1<<(n)))
#define VIFM_CLRALL(m)	((m)=0)
#define VIFM_COPY(mfrom,mto)	((mto)=(mfrom))
#define VIFM_SAME(m1,m2)	((m1)==(m2))

/* Passed by mrouted for an MRT_ADD_VIF - again we use the
 * mrouted 3.6 structures for compatibility
 */
struct vifctl {
	vifi_t	vifc_vifi;		/* Index of VIF */
	unsigned char vifc_flags;	/* VIFF_ flags */
	unsigned char vifc_threshold;	/* ttl limit */
	unsigned int vifc_rate_limit;	/* Rate limiter values (NI) */
	union {
		struct in_addr vifc_lcl_addr;     /* Local interface address */
		int            vifc_lcl_ifindex;  /* Local interface index   */
	};
	struct in_addr vifc_rmt_addr;	/* IPIP tunnel addr */
};

#define VIFF_TUNNEL		0x1	/* IPIP tunnel */
#define VIFF_SRCRT		0x2	/* NI */
#define VIFF_REGISTER		0x4	/* register vif	*/
#define VIFF_USE_IFINDEX	0x8	/* use vifc_lcl_ifindex instead of
					   vifc_lcl_addr to find an interface */

/* Cache manipulation structures for mrouted and PIMd */
struct mfcctl {
	struct in_addr mfcc_origin;		/* Origin of mcast	*/
	struct in_addr mfcc_mcastgrp;		/* Group in question	*/
	vifi_t	mfcc_parent;			/* Where it arrived	*/
	unsigned char mfcc_ttls[MAXVIFS];	/* Where it is going	*/
	unsigned int mfcc_pkt_cnt;		/* pkt count for src-grp */
	unsigned int mfcc_byte_cnt;
	unsigned int mfcc_wrong_if;
	int	     mfcc_expire;
};

/*  Group count retrieval for mrouted */
struct sioc_sg_req {
	struct in_addr src;
	struct in_addr grp;
	unsigned long pktcnt;
	unsigned long bytecnt;
	unsigned long wrong_if;
};

/* To get vif packet counts */
struct sioc_vif_req {
	vifi_t	vifi;		/* Which iface */
	unsigned long icount;	/* In packets */
	unsigned long ocount;	/* Out packets */
	unsigned long ibytes;	/* In bytes */
	unsigned long obytes;	/* Out bytes */
};

/* This is the format the mroute daemon expects to see IGMP control
 * data. Magically happens to be like an IP packet as per the original
 */
struct igmpmsg {
	__u32 unused1,unused2;
	unsigned char im_msgtype;		/* What is this */
	unsigned char im_mbz;			/* Must be zero */
	unsigned char im_vif;			/* Interface (this ought to be a vifi_t!) */
	unsigned char unused3;
	struct in_addr im_src,im_dst;
};

/* That's all usermode folks */

#define MFC_ASSERT_THRESH (3*HZ)		/* Maximal freq. of asserts */

/* Pseudo messages used by mrouted */
#define IGMPMSG_NOCACHE		1		/* Kern cache fill request to mrouted */
#define IGMPMSG_WRONGVIF	2		/* For PIM assert processing (unused) */
#define IGMPMSG_WHOLEPKT	3		/* For PIM Register processing */

#endif /* _UAPI__LINUX_MROUTE_H */
