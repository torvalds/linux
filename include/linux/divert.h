/*
 * Frame Diversion, Benoit Locher <Benoit.Locher@skf.com>
 *
 * Changes:
 * 		06/09/2000	BL:	initial version
 * 
 */
 
#ifndef _LINUX_DIVERT_H
#define _LINUX_DIVERT_H

#include <asm/types.h>

#define	MAX_DIVERT_PORTS	8	/* Max number of ports to divert (tcp, udp) */

/* Divertable protocols */
#define	DIVERT_PROTO_NONE	0x0000
#define	DIVERT_PROTO_IP		0x0001
#define	DIVERT_PROTO_ICMP	0x0002
#define	DIVERT_PROTO_TCP	0x0004
#define	DIVERT_PROTO_UDP	0x0008

/*
 *	This is an Ethernet Frame Diverter option block
 */
struct divert_blk
{
	int		divert;  /* are we active */
	unsigned int protos;	/* protocols */
	u16		tcp_dst[MAX_DIVERT_PORTS]; /* specific tcp dst ports to divert */
	u16		tcp_src[MAX_DIVERT_PORTS]; /* specific tcp src ports to divert */
	u16		udp_dst[MAX_DIVERT_PORTS]; /* specific udp dst ports to divert */
	u16		udp_src[MAX_DIVERT_PORTS]; /* specific udp src ports to divert */
};

/*
 * Diversion control block, for configuration with the userspace tool
 * divert
 */

typedef union _divert_cf_arg
{
	s16		int16;
	u16		uint16;
	s32		int32;
	u32		uint32;
	s64		int64;
	u64		uint64;
	void	__user *ptr;
} divert_cf_arg;


struct divert_cf
{
	int	cmd;				/* Command */
	divert_cf_arg 	arg1,
					arg2,
					arg3;
	int	dev_index;	/* device index (eth0=0, etc...) */
};


/* Diversion commands */
#define	DIVCMD_DIVERT			1 /* ENABLE/DISABLE diversion */
#define	DIVCMD_IP				2 /* ENABLE/DISABLE whold IP diversion */
#define	DIVCMD_TCP				3 /* ENABLE/DISABLE whold TCP diversion */
#define	DIVCMD_TCPDST			4 /* ADD/REMOVE TCP DST port for diversion */
#define	DIVCMD_TCPSRC			5 /* ADD/REMOVE TCP SRC port for diversion */
#define	DIVCMD_UDP				6 /* ENABLE/DISABLE whole UDP diversion */
#define	DIVCMD_UDPDST			7 /* ADD/REMOVE UDP DST port for diversion */
#define	DIVCMD_UDPSRC			8 /* ADD/REMOVE UDP SRC port for diversion */
#define	DIVCMD_ICMP				9 /* ENABLE/DISABLE whole ICMP diversion */
#define	DIVCMD_GETSTATUS		10 /* GET the status of the diverter */
#define	DIVCMD_RESET			11 /* Reset the diverter on the specified dev */
#define DIVCMD_GETVERSION		12 /* Retrieve the diverter code version (char[32]) */

/* General syntax of the commands:
 * 
 * DIVCMD_xxxxxx(arg1, arg2, arg3, dev_index)
 * 
 * SIOCSIFDIVERT:
 *   DIVCMD_DIVERT(DIVARG1_ENABLE|DIVARG1_DISABLE, , ,ifindex)
 *   DIVCMD_IP(DIVARG1_ENABLE|DIVARG1_DISABLE, , , ifindex)
 *   DIVCMD_TCP(DIVARG1_ENABLE|DIVARG1_DISABLE, , , ifindex)
 *   DIVCMD_TCPDST(DIVARG1_ADD|DIVARG1_REMOVE, port, , ifindex)
 *   DIVCMD_TCPSRC(DIVARG1_ADD|DIVARG1_REMOVE, port, , ifindex)
 *   DIVCMD_UDP(DIVARG1_ENABLE|DIVARG1_DISABLE, , , ifindex)
 *   DIVCMD_UDPDST(DIVARG1_ADD|DIVARG1_REMOVE, port, , ifindex)
 *   DIVCMD_UDPSRC(DIVARG1_ADD|DIVARG1_REMOVE, port, , ifindex)
 *   DIVCMD_ICMP(DIVARG1_ENABLE|DIVARG1_DISABLE, , , ifindex)
 *   DIVCMD_RESET(, , , ifindex)
 *   
 * SIOGIFDIVERT:
 *   DIVCMD_GETSTATUS(divert_blk, , , ifindex)
 *   DIVCMD_GETVERSION(string[3])
 */


/* Possible values for arg1 */
#define	DIVARG1_ENABLE			0 /* ENABLE something */
#define	DIVARG1_DISABLE			1 /* DISABLE something */
#define DIVARG1_ADD				2 /* ADD something */
#define DIVARG1_REMOVE			3 /* REMOVE something */


#ifdef __KERNEL__

/* diverter functions */
#include <linux/skbuff.h>

#ifdef CONFIG_NET_DIVERT
#include <linux/netdevice.h>

int alloc_divert_blk(struct net_device *);
void free_divert_blk(struct net_device *);
int divert_ioctl(unsigned int cmd, struct divert_cf __user *arg);
void divert_frame(struct sk_buff *skb);
static inline void handle_diverter(struct sk_buff *skb)
{
	/* if diversion is supported on device, then divert */
	if (skb->dev->divert && skb->dev->divert->divert)
		divert_frame(skb);
}

#else
# define alloc_divert_blk(dev)		(0)
# define free_divert_blk(dev)		do {} while (0)
# define divert_ioctl(cmd, arg)		(-ENOPKG)
# define handle_diverter(skb)		do {} while (0)
#endif
#endif 
#endif	/* _LINUX_DIVERT_H */
