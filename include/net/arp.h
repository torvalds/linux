/* linux/net/inet/arp.h */
#ifndef _ARP_H
#define _ARP_H

#include <linux/if_arp.h>
#include <net/neighbour.h>

#define HAVE_ARP_CREATE

extern struct neigh_table arp_tbl;

extern void	arp_init(void);
extern int	arp_find(unsigned char *haddr, struct sk_buff *skb);
extern int	arp_ioctl(unsigned int cmd, void __user *arg);
extern void     arp_send(int type, int ptype, u32 dest_ip, 
			 struct net_device *dev, u32 src_ip, 
			 unsigned char *dest_hw, unsigned char *src_hw, unsigned char *th);
extern int	arp_bind_neighbour(struct dst_entry *dst);
extern int	arp_mc_map(u32 addr, u8 *haddr, struct net_device *dev, int dir);
extern void	arp_ifdown(struct net_device *dev);

extern struct sk_buff *arp_create(int type, int ptype, u32 dest_ip,
				  struct net_device *dev, u32 src_ip,
				  unsigned char *dest_hw, unsigned char *src_hw,
				  unsigned char *target_hw);
extern void arp_xmit(struct sk_buff *skb);

extern struct neigh_ops arp_broken_ops;

#endif	/* _ARP_H */
