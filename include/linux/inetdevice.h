#ifndef _LINUX_INETDEVICE_H
#define _LINUX_INETDEVICE_H

#ifdef __KERNEL__

#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/timer.h>

struct ipv4_devconf
{
	int	accept_redirects;
	int	send_redirects;
	int	secure_redirects;
	int	shared_media;
	int	accept_source_route;
	int	rp_filter;
	int	proxy_arp;
	int	bootp_relay;
	int	log_martians;
	int	forwarding;
	int	mc_forwarding;
	int	tag;
	int     arp_filter;
	int	arp_announce;
	int	arp_ignore;
	int	arp_accept;
	int	medium_id;
	int	no_xfrm;
	int	no_policy;
	int	force_igmp_version;
	int	promote_secondaries;
	void	*sysctl;
};

extern struct ipv4_devconf ipv4_devconf;

struct in_device
{
	struct net_device	*dev;
	atomic_t		refcnt;
	int			dead;
	struct in_ifaddr	*ifa_list;	/* IP ifaddr chain		*/
	rwlock_t		mc_list_lock;
	struct ip_mc_list	*mc_list;	/* IP multicast filter chain    */
	spinlock_t		mc_tomb_lock;
	struct ip_mc_list	*mc_tomb;
	unsigned long		mr_v1_seen;
	unsigned long		mr_v2_seen;
	unsigned long		mr_maxdelay;
	unsigned char		mr_qrv;
	unsigned char		mr_gq_running;
	unsigned char		mr_ifc_count;
	struct timer_list	mr_gq_timer;	/* general query timer */
	struct timer_list	mr_ifc_timer;	/* interface change timer */

	struct neigh_parms	*arp_parms;
	struct ipv4_devconf	cnf;
	struct rcu_head		rcu_head;
};

#define IN_DEV_FORWARD(in_dev)		((in_dev)->cnf.forwarding)
#define IN_DEV_MFORWARD(in_dev)		(ipv4_devconf.mc_forwarding && (in_dev)->cnf.mc_forwarding)
#define IN_DEV_RPFILTER(in_dev)		(ipv4_devconf.rp_filter && (in_dev)->cnf.rp_filter)
#define IN_DEV_SOURCE_ROUTE(in_dev)	(ipv4_devconf.accept_source_route && (in_dev)->cnf.accept_source_route)
#define IN_DEV_BOOTP_RELAY(in_dev)	(ipv4_devconf.bootp_relay && (in_dev)->cnf.bootp_relay)

#define IN_DEV_LOG_MARTIANS(in_dev)	(ipv4_devconf.log_martians || (in_dev)->cnf.log_martians)
#define IN_DEV_PROXY_ARP(in_dev)	(ipv4_devconf.proxy_arp || (in_dev)->cnf.proxy_arp)
#define IN_DEV_SHARED_MEDIA(in_dev)	(ipv4_devconf.shared_media || (in_dev)->cnf.shared_media)
#define IN_DEV_TX_REDIRECTS(in_dev)	(ipv4_devconf.send_redirects || (in_dev)->cnf.send_redirects)
#define IN_DEV_SEC_REDIRECTS(in_dev)	(ipv4_devconf.secure_redirects || (in_dev)->cnf.secure_redirects)
#define IN_DEV_IDTAG(in_dev)		((in_dev)->cnf.tag)
#define IN_DEV_MEDIUM_ID(in_dev)	((in_dev)->cnf.medium_id)
#define IN_DEV_PROMOTE_SECONDARIES(in_dev)	(ipv4_devconf.promote_secondaries || (in_dev)->cnf.promote_secondaries)

#define IN_DEV_RX_REDIRECTS(in_dev) \
	((IN_DEV_FORWARD(in_dev) && \
	  (ipv4_devconf.accept_redirects && (in_dev)->cnf.accept_redirects)) \
	 || (!IN_DEV_FORWARD(in_dev) && \
	  (ipv4_devconf.accept_redirects || (in_dev)->cnf.accept_redirects)))

#define IN_DEV_ARPFILTER(in_dev)	(ipv4_devconf.arp_filter || (in_dev)->cnf.arp_filter)
#define IN_DEV_ARP_ANNOUNCE(in_dev)	(max(ipv4_devconf.arp_announce, (in_dev)->cnf.arp_announce))
#define IN_DEV_ARP_IGNORE(in_dev)	(max(ipv4_devconf.arp_ignore, (in_dev)->cnf.arp_ignore))

struct in_ifaddr
{
	struct in_ifaddr	*ifa_next;
	struct in_device	*ifa_dev;
	struct rcu_head		rcu_head;
	u32			ifa_local;
	u32			ifa_address;
	u32			ifa_mask;
	u32			ifa_broadcast;
	u32			ifa_anycast;
	unsigned char		ifa_scope;
	unsigned char		ifa_flags;
	unsigned char		ifa_prefixlen;
	char			ifa_label[IFNAMSIZ];
};

extern int register_inetaddr_notifier(struct notifier_block *nb);
extern int unregister_inetaddr_notifier(struct notifier_block *nb);

extern struct net_device 	*ip_dev_find(u32 addr);
extern int		inet_addr_onlink(struct in_device *in_dev, u32 a, u32 b);
extern int		devinet_ioctl(unsigned int cmd, void __user *);
extern void		devinet_init(void);
extern struct in_device *inetdev_init(struct net_device *dev);
extern struct in_device	*inetdev_by_index(int);
extern u32		inet_select_addr(const struct net_device *dev, u32 dst, int scope);
extern u32		inet_confirm_addr(const struct net_device *dev, u32 dst, u32 local, int scope);
extern struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, u32 prefix, u32 mask);
extern void		inet_forward_change(void);

static __inline__ int inet_ifa_match(u32 addr, struct in_ifaddr *ifa)
{
	return !((addr^ifa->ifa_address)&ifa->ifa_mask);
}

/*
 *	Check if a mask is acceptable.
 */
 
static __inline__ int bad_mask(u32 mask, u32 addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

#define for_primary_ifa(in_dev)	{ struct in_ifaddr *ifa; \
  for (ifa = (in_dev)->ifa_list; ifa && !(ifa->ifa_flags&IFA_F_SECONDARY); ifa = ifa->ifa_next)

#define for_ifa(in_dev)	{ struct in_ifaddr *ifa; \
  for (ifa = (in_dev)->ifa_list; ifa; ifa = ifa->ifa_next)


#define endfor_ifa(in_dev) }

static inline struct in_device *__in_dev_get_rcu(const struct net_device *dev)
{
	struct in_device *in_dev = dev->ip_ptr;
	if (in_dev)
		in_dev = rcu_dereference(in_dev);
	return in_dev;
}

static __inline__ struct in_device *
in_dev_get(const struct net_device *dev)
{
	struct in_device *in_dev;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev)
		atomic_inc(&in_dev->refcnt);
	rcu_read_unlock();
	return in_dev;
}

static __inline__ struct in_device *
__in_dev_get_rtnl(const struct net_device *dev)
{
	return (struct in_device*)dev->ip_ptr;
}

extern void in_dev_finish_destroy(struct in_device *idev);

static inline void in_dev_put(struct in_device *idev)
{
	if (atomic_dec_and_test(&idev->refcnt))
		in_dev_finish_destroy(idev);
}

#define __in_dev_put(idev)  atomic_dec(&(idev)->refcnt)
#define in_dev_hold(idev)   atomic_inc(&(idev)->refcnt)

#endif /* __KERNEL__ */

static __inline__ __u32 inet_make_mask(int logmask)
{
	if (logmask)
		return htonl(~((1<<(32-logmask))-1));
	return 0;
}

static __inline__ int inet_mask_len(__u32 mask)
{
	if (!(mask = ntohl(mask)))
		return 0;
	return 32 - ffz(~mask);
}


#endif /* _LINUX_INETDEVICE_H */
