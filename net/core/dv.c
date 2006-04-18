/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic frame diversion
 *
 * Authors:	
 * 		Benoit LOCHER:	initial integration within the kernel with support for ethernet
 * 		Dave Miller:	improvement on the code (correctness, performance and source files)
 *
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <net/dst.h>
#include <net/arp.h>
#include <net/sock.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/checksum.h>
#include <linux/divert.h>
#include <linux/sockios.h>

const char sysctl_divert_version[32]="0.46";	/* Current version */

static int __init dv_init(void)
{
	return 0;
}
module_init(dv_init);

/*
 * Allocate a divert_blk for a device. This must be an ethernet nic.
 */
int alloc_divert_blk(struct net_device *dev)
{
	int alloc_size = (sizeof(struct divert_blk) + 3) & ~3;

	dev->divert = NULL;
	if (dev->type == ARPHRD_ETHER) {
		dev->divert = kzalloc(alloc_size, GFP_KERNEL);
		if (dev->divert == NULL) {
			printk(KERN_INFO "divert: unable to allocate divert_blk for %s\n",
			       dev->name);
			return -ENOMEM;
		}
		dev_hold(dev);
	}

	return 0;
} 

/*
 * Free a divert_blk allocated by the above function, if it was 
 * allocated on that device.
 */
void free_divert_blk(struct net_device *dev)
{
	if (dev->divert) {
		kfree(dev->divert);
		dev->divert=NULL;
		dev_put(dev);
	}
}

/*
 * Adds a tcp/udp (source or dest) port to an array
 */
static int add_port(u16 ports[], u16 port)
{
	int i;

	if (port == 0)
		return -EINVAL;

	/* Storing directly in network format for performance,
	 * thanks Dave :)
	 */
	port = htons(port);

	for (i = 0; i < MAX_DIVERT_PORTS; i++) {
		if (ports[i] == port)
			return -EALREADY;
	}
	
	for (i = 0; i < MAX_DIVERT_PORTS; i++) {
		if (ports[i] == 0) {
			ports[i] = port;
			return 0;
		}
	}

	return -ENOBUFS;
}

/*
 * Removes a port from an array tcp/udp (source or dest)
 */
static int remove_port(u16 ports[], u16 port)
{
	int i;

	if (port == 0)
		return -EINVAL;
	
	/* Storing directly in network format for performance,
	 * thanks Dave !
	 */
	port = htons(port);

	for (i = 0; i < MAX_DIVERT_PORTS; i++) {
		if (ports[i] == port) {
			ports[i] = 0;
			return 0;
		}
	}

	return -EINVAL;
}

/* Some basic sanity checks on the arguments passed to divert_ioctl() */
static int check_args(struct divert_cf *div_cf, struct net_device **dev)
{
	char devname[32];
	int ret;

	if (dev == NULL)
		return -EFAULT;
	
	/* GETVERSION: all other args are unused */
	if (div_cf->cmd == DIVCMD_GETVERSION)
		return 0;
	
	/* Network device index should reasonably be between 0 and 1000 :) */
	if (div_cf->dev_index < 0 || div_cf->dev_index > 1000) 
		return -EINVAL;
			
	/* Let's try to find the ifname */
	sprintf(devname, "eth%d", div_cf->dev_index);
	*dev = dev_get_by_name(devname);
	
	/* dev should NOT be null */
	if (*dev == NULL)
		return -EINVAL;

	ret = 0;

	/* user issuing the ioctl must be a super one :) */
	if (!capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
		goto out;
	}

	/* Device must have a divert_blk member NOT null */
	if ((*dev)->divert == NULL)
		ret = -EINVAL;
out:
	dev_put(*dev);
	return ret;
}

/*
 * control function of the diverter
 */
#if 0
#define	DVDBG(a)	\
	printk(KERN_DEBUG "divert_ioctl() line %d %s\n", __LINE__, (a))
#else
#define	DVDBG(a)
#endif

int divert_ioctl(unsigned int cmd, struct divert_cf __user *arg)
{
	struct divert_cf	div_cf;
	struct divert_blk	*div_blk;
	struct net_device	*dev;
	int			ret;

	switch (cmd) {
	case SIOCGIFDIVERT:
		DVDBG("SIOCGIFDIVERT, copy_from_user");
		if (copy_from_user(&div_cf, arg, sizeof(struct divert_cf)))
			return -EFAULT;
		DVDBG("before check_args");
		ret = check_args(&div_cf, &dev);
		if (ret)
			return ret;
		DVDBG("after checkargs");
		div_blk = dev->divert;
			
		DVDBG("befre switch()");
		switch (div_cf.cmd) {
		case DIVCMD_GETSTATUS:
			/* Now, just give the user the raw divert block
			 * for him to play with :)
			 */
			if (copy_to_user(div_cf.arg1.ptr, dev->divert,
					 sizeof(struct divert_blk)))
				return -EFAULT;
			break;

		case DIVCMD_GETVERSION:
			DVDBG("GETVERSION: checking ptr");
			if (div_cf.arg1.ptr == NULL)
				return -EINVAL;
			DVDBG("GETVERSION: copying data to userland");
			if (copy_to_user(div_cf.arg1.ptr,
					 sysctl_divert_version, 32))
				return -EFAULT;
			DVDBG("GETVERSION: data copied");
			break;

		default:
			return -EINVAL;
		}

		break;

	case SIOCSIFDIVERT:
		if (copy_from_user(&div_cf, arg, sizeof(struct divert_cf)))
			return -EFAULT;

		ret = check_args(&div_cf, &dev);
		if (ret)
			return ret;

		div_blk = dev->divert;

		switch(div_cf.cmd) {
		case DIVCMD_RESET:
			div_blk->divert = 0;
			div_blk->protos = DIVERT_PROTO_NONE;
			memset(div_blk->tcp_dst, 0,
			       MAX_DIVERT_PORTS * sizeof(u16));
			memset(div_blk->tcp_src, 0,
			       MAX_DIVERT_PORTS * sizeof(u16));
			memset(div_blk->udp_dst, 0,
			       MAX_DIVERT_PORTS * sizeof(u16));
			memset(div_blk->udp_src, 0,
			       MAX_DIVERT_PORTS * sizeof(u16));
			return 0;
				
		case DIVCMD_DIVERT:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ENABLE:
				if (div_blk->divert)
					return -EALREADY;
				div_blk->divert = 1;
				break;

			case DIVARG1_DISABLE:
				if (!div_blk->divert)
					return -EALREADY;
				div_blk->divert = 0;
				break;

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_IP:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ENABLE:
				if (div_blk->protos & DIVERT_PROTO_IP)
					return -EALREADY;
				div_blk->protos |= DIVERT_PROTO_IP;
				break;

			case DIVARG1_DISABLE:
				if (!(div_blk->protos & DIVERT_PROTO_IP))
					return -EALREADY;
				div_blk->protos &= ~DIVERT_PROTO_IP;
				break;

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_TCP:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ENABLE:
				if (div_blk->protos & DIVERT_PROTO_TCP)
					return -EALREADY;
				div_blk->protos |= DIVERT_PROTO_TCP;
				break;

			case DIVARG1_DISABLE:
				if (!(div_blk->protos & DIVERT_PROTO_TCP))
					return -EALREADY;
				div_blk->protos &= ~DIVERT_PROTO_TCP;
				break;

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_TCPDST:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ADD:
				return add_port(div_blk->tcp_dst,
						div_cf.arg2.uint16);
				
			case DIVARG1_REMOVE:
				return remove_port(div_blk->tcp_dst,
						   div_cf.arg2.uint16);

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_TCPSRC:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ADD:
				return add_port(div_blk->tcp_src,
						div_cf.arg2.uint16);

			case DIVARG1_REMOVE:
				return remove_port(div_blk->tcp_src,
						   div_cf.arg2.uint16);

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_UDP:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ENABLE:
				if (div_blk->protos & DIVERT_PROTO_UDP)
					return -EALREADY;
				div_blk->protos |= DIVERT_PROTO_UDP;
				break;

			case DIVARG1_DISABLE:
				if (!(div_blk->protos & DIVERT_PROTO_UDP))
					return -EALREADY;
				div_blk->protos &= ~DIVERT_PROTO_UDP;
				break;

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_UDPDST:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ADD:
				return add_port(div_blk->udp_dst,
						div_cf.arg2.uint16);

			case DIVARG1_REMOVE:
				return remove_port(div_blk->udp_dst,
						   div_cf.arg2.uint16);

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_UDPSRC:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ADD:
				return add_port(div_blk->udp_src,
						div_cf.arg2.uint16);

			case DIVARG1_REMOVE:
				return remove_port(div_blk->udp_src,
						   div_cf.arg2.uint16);

			default:
				return -EINVAL;
			}

			break;

		case DIVCMD_ICMP:
			switch(div_cf.arg1.int32) {
			case DIVARG1_ENABLE:
				if (div_blk->protos & DIVERT_PROTO_ICMP)
					return -EALREADY;
				div_blk->protos |= DIVERT_PROTO_ICMP;
				break;

			case DIVARG1_DISABLE:
				if (!(div_blk->protos & DIVERT_PROTO_ICMP))
					return -EALREADY;
				div_blk->protos &= ~DIVERT_PROTO_ICMP;
				break;

			default:
				return -EINVAL;
			}

			break;

		default:
			return -EINVAL;
		}

		break;

	default:
		return -EINVAL;
	}

	return 0;
}


/*
 * Check if packet should have its dest mac address set to the box itself
 * for diversion
 */

#define	ETH_DIVERT_FRAME(skb) \
	memcpy(eth_hdr(skb), skb->dev->dev_addr, ETH_ALEN); \
	skb->pkt_type=PACKET_HOST
		
void divert_frame(struct sk_buff *skb)
{
	struct ethhdr			*eth = eth_hdr(skb);
	struct iphdr			*iph;
	struct tcphdr			*tcph;
	struct udphdr			*udph;
	struct divert_blk		*divert = skb->dev->divert;
	int				i, src, dst;
	unsigned char			*skb_data_end = skb->data + skb->len;

	/* Packet is already aimed at us, return */
	if (!compare_ether_addr(eth->h_dest, skb->dev->dev_addr))
		return;
	
	/* proto is not IP, do nothing */
	if (eth->h_proto != htons(ETH_P_IP))
		return;
	
	/* Divert all IP frames ? */
	if (divert->protos & DIVERT_PROTO_IP) {
		ETH_DIVERT_FRAME(skb);
		return;
	}
	
	/* Check for possible (maliciously) malformed IP frame (thanks Dave) */
	iph = (struct iphdr *) skb->data;
	if (((iph->ihl<<2)+(unsigned char*)(iph)) >= skb_data_end) {
		printk(KERN_INFO "divert: malformed IP packet !\n");
		return;
	}

	switch (iph->protocol) {
	/* Divert all ICMP frames ? */
	case IPPROTO_ICMP:
		if (divert->protos & DIVERT_PROTO_ICMP) {
			ETH_DIVERT_FRAME(skb);
			return;
		}
		break;

	/* Divert all TCP frames ? */
	case IPPROTO_TCP:
		if (divert->protos & DIVERT_PROTO_TCP) {
			ETH_DIVERT_FRAME(skb);
			return;
		}

		/* Check for possible (maliciously) malformed IP
		 * frame (thanx Dave)
		 */
		tcph = (struct tcphdr *)
			(((unsigned char *)iph) + (iph->ihl<<2));
		if (((unsigned char *)(tcph+1)) >= skb_data_end) {
			printk(KERN_INFO "divert: malformed TCP packet !\n");
			return;
		}

		/* Divert some tcp dst/src ports only ?*/
		for (i = 0; i < MAX_DIVERT_PORTS; i++) {
			dst = divert->tcp_dst[i];
			src = divert->tcp_src[i];
			if ((dst && dst == tcph->dest) ||
			    (src && src == tcph->source)) {
				ETH_DIVERT_FRAME(skb);
				return;
			}
		}
		break;

	/* Divert all UDP frames ? */
	case IPPROTO_UDP:
		if (divert->protos & DIVERT_PROTO_UDP) {
			ETH_DIVERT_FRAME(skb);
			return;
		}

		/* Check for possible (maliciously) malformed IP
		 * packet (thanks Dave)
		 */
		udph = (struct udphdr *)
			(((unsigned char *)iph) + (iph->ihl<<2));
		if (((unsigned char *)(udph+1)) >= skb_data_end) {
			printk(KERN_INFO
			       "divert: malformed UDP packet !\n");
			return;
		}

		/* Divert some udp dst/src ports only ? */
		for (i = 0; i < MAX_DIVERT_PORTS; i++) {
			dst = divert->udp_dst[i];
			src = divert->udp_src[i];
			if ((dst && dst == udph->dest) ||
			    (src && src == udph->source)) {
				ETH_DIVERT_FRAME(skb);
				return;
			}
		}
		break;
	}
}
