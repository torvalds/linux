#ifndef __BEN_VLAN_802_1Q_INC__
#define __BEN_VLAN_802_1Q_INC__

#include <linux/if_vlan.h>

/*  Uncomment this if you want debug traces to be shown. */
/* #define VLAN_DEBUG */

#define VLAN_ERR KERN_ERR
#define VLAN_INF KERN_INFO
#define VLAN_DBG KERN_ALERT /* change these... to debug, having a hard time
			     * changing the log level at run-time..for some reason.
			     */

/*

These I use for memory debugging.  I feared a leak at one time, but
I never found it..and the problem seems to have dissappeared.  Still,
I'll bet they might prove useful again... --Ben


#define VLAN_MEM_DBG(x, y, z) printk(VLAN_DBG "%s:  "  x, __FUNCTION__, y, z);
#define VLAN_FMEM_DBG(x, y) printk(VLAN_DBG "%s:  " x, __FUNCTION__, y);
*/

/* This way they don't do anything! */
#define VLAN_MEM_DBG(x, y, z)
#define VLAN_FMEM_DBG(x, y)


extern unsigned short vlan_name_type;

#define VLAN_GRP_HASH_SHIFT	5
#define VLAN_GRP_HASH_SIZE	(1 << VLAN_GRP_HASH_SHIFT)
#define VLAN_GRP_HASH_MASK	(VLAN_GRP_HASH_SIZE - 1)

/*  Find a VLAN device by the MAC address of its Ethernet device, and
 *  it's VLAN ID.  The default configuration is to have VLAN's scope
 *  to be box-wide, so the MAC will be ignored.  The mac will only be
 *  looked at if we are configured to have a separate set of VLANs per
 *  each MAC addressable interface.  Note that this latter option does
 *  NOT follow the spec for VLANs, but may be useful for doing very
 *  large quantities of VLAN MUX/DEMUX onto FrameRelay or ATM PVCs.
 *
 *  Must be invoked with rcu_read_lock (ie preempt disabled)
 *  or with RTNL.
 */
struct net_device *__find_vlan_dev(struct net_device* real_dev,
				   unsigned short VID); /* vlan.c */

/* found in vlan_dev.c */
int vlan_dev_rebuild_header(struct sk_buff *skb);
int vlan_skb_recv(struct sk_buff *skb, struct net_device *dev,
		  struct packet_type *ptype, struct net_device *orig_dev);
int vlan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
			 unsigned short type, void *daddr, void *saddr,
			 unsigned len);
int vlan_dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
int vlan_dev_hwaccel_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
int vlan_dev_change_mtu(struct net_device *dev, int new_mtu);
int vlan_dev_open(struct net_device* dev);
int vlan_dev_stop(struct net_device* dev);
int vlan_dev_ioctl(struct net_device* dev, struct ifreq *ifr, int cmd);
void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, short vlan_prio);
int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, short vlan_prio);
int vlan_dev_set_vlan_flag(const struct net_device *dev,
			   u32 flag, short flag_val);
void vlan_dev_get_realdev_name(const struct net_device *dev, char *result);
void vlan_dev_get_vid(const struct net_device *dev, unsigned short *result);
void vlan_dev_set_multicast_list(struct net_device *vlan_dev);

int vlan_check_real_dev(struct net_device *real_dev, unsigned short vlan_id);
void vlan_setup(struct net_device *dev);
int register_vlan_dev(struct net_device *dev);
int unregister_vlan_device(struct net_device *dev);

int vlan_netlink_init(void);
void vlan_netlink_fini(void);

extern struct rtnl_link_ops vlan_link_ops;

#endif /* !(__BEN_VLAN_802_1Q_INC__) */
