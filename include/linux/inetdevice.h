#ifndef _LINUX_INETDEVICE_H
#define _LINUX_INETDEVICE_H

#ifdef __KERNEL__

#include <linux/bitmap.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/timer.h>
#include <linux/sysctl.h>
#include <linux/rtnetlink.h>

enum
{
	IPV4_DEVCONF_FORWARDING=1,
	IPV4_DEVCONF_MC_FORWARDING,
	IPV4_DEVCONF_PROXY_ARP,
	IPV4_DEVCONF_ACCEPT_REDIRECTS,
	IPV4_DEVCONF_SECURE_REDIRECTS,
	IPV4_DEVCONF_SEND_REDIRECTS,
	IPV4_DEVCONF_SHARED_MEDIA,
	IPV4_DEVCONF_RP_FILTER,
	IPV4_DEVCONF_ACCEPT_SOURCE_ROUTE,
	IPV4_DEVCONF_BOOTP_RELAY,
	IPV4_DEVCONF_LOG_MARTIANS,
	IPV4_DEVCONF_TAG,
	IPV4_DEVCONF_ARPFILTER,
	IPV4_DEVCONF_MEDIUM_ID,
	IPV4_DEVCONF_NOXFRM,
	IPV4_DEVCONF_NOPOLICY,
	IPV4_DEVCONF_FORCE_IGMP_VERSION,
	IPV4_DEVCONF_ARP_ANNOUNCE,
	IPV4_DEVCONF_ARP_IGNORE,
	IPV4_DEVCONF_PROMOTE_SECONDARIES,
	IPV4_DEVCONF_ARP_ACCEPT,
	IPV4_DEVCONF_ARP_NOTIFY,
	IPV4_DEVCONF_ACCEPT_LOCAL,
	IPV4_DEVCONF_SRC_VMARK,
	IPV4_DEVCONF_PROXY_ARP_PVLAN,
	IPV4_DEVCONF_ROUTE_LOCALNET,
	__IPV4_DEVCONF_MAX
};

#define IPV4_DEVCONF_MAX (__IPV4_DEVCONF_MAX - 1)

struct ipv4_devconf {
	void	*sysctl;
	int	data[IPV4_DEVCONF_MAX];
	DECLARE_BITMAP(state, IPV4_DEVCONF_MAX);
};

struct in_device {
	struct net_device	*dev;
	atomic_t		refcnt;
	int			dead;
	struct in_ifaddr	*ifa_list;	/* IP ifaddr chain		*/
	struct ip_mc_list __rcu	*mc_list;	/* IP multicast filter chain    */
	int			mc_count;	/* Number of installed mcasts	*/
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

#define IPV4_DEVCONF(cnf, attr) ((cnf).data[IPV4_DEVCONF_ ## attr - 1])
#define IPV4_DEVCONF_ALL(net, attr) \
	IPV4_DEVCONF((*(net)->ipv4.devconf_all), attr)

static inline int ipv4_devconf_get(struct in_device *in_dev, int index)
{
	index--;
	return in_dev->cnf.data[index];
}

static inline void ipv4_devconf_set(struct in_device *in_dev, int index,
				    int val)
{
	index--;
	set_bit(index, in_dev->cnf.state);
	in_dev->cnf.data[index] = val;
}

static inline void ipv4_devconf_setall(struct in_device *in_dev)
{
	bitmap_fill(in_dev->cnf.state, IPV4_DEVCONF_MAX);
}

#define IN_DEV_CONF_GET(in_dev, attr) \
	ipv4_devconf_get((in_dev), IPV4_DEVCONF_ ## attr)
#define IN_DEV_CONF_SET(in_dev, attr, val) \
	ipv4_devconf_set((in_dev), IPV4_DEVCONF_ ## attr, (val))

#define IN_DEV_ANDCONF(in_dev, attr) \
	(IPV4_DEVCONF_ALL(dev_net(in_dev->dev), attr) && \
	 IN_DEV_CONF_GET((in_dev), attr))

#define IN_DEV_NET_ORCONF(in_dev, net, attr) \
	(IPV4_DEVCONF_ALL(net, attr) || \
	 IN_DEV_CONF_GET((in_dev), attr))

#define IN_DEV_ORCONF(in_dev, attr) \
	IN_DEV_NET_ORCONF(in_dev, dev_net(in_dev->dev), attr)

#define IN_DEV_MAXCONF(in_dev, attr) \
	(max(IPV4_DEVCONF_ALL(dev_net(in_dev->dev), attr), \
	     IN_DEV_CONF_GET((in_dev), attr)))

#define IN_DEV_FORWARD(in_dev)		IN_DEV_CONF_GET((in_dev), FORWARDING)
#define IN_DEV_MFORWARD(in_dev)		IN_DEV_ANDCONF((in_dev), MC_FORWARDING)
#define IN_DEV_RPFILTER(in_dev)		IN_DEV_MAXCONF((in_dev), RP_FILTER)
#define IN_DEV_SRC_VMARK(in_dev)    	IN_DEV_ORCONF((in_dev), SRC_VMARK)
#define IN_DEV_SOURCE_ROUTE(in_dev)	IN_DEV_ANDCONF((in_dev), \
						       ACCEPT_SOURCE_ROUTE)
#define IN_DEV_ACCEPT_LOCAL(in_dev)	IN_DEV_ORCONF((in_dev), ACCEPT_LOCAL)
#define IN_DEV_BOOTP_RELAY(in_dev)	IN_DEV_ANDCONF((in_dev), BOOTP_RELAY)

#define IN_DEV_LOG_MARTIANS(in_dev)	IN_DEV_ORCONF((in_dev), LOG_MARTIANS)
#define IN_DEV_PROXY_ARP(in_dev)	IN_DEV_ORCONF((in_dev), PROXY_ARP)
#define IN_DEV_PROXY_ARP_PVLAN(in_dev)	IN_DEV_CONF_GET(in_dev, PROXY_ARP_PVLAN)
#define IN_DEV_SHARED_MEDIA(in_dev)	IN_DEV_ORCONF((in_dev), SHARED_MEDIA)
#define IN_DEV_TX_REDIRECTS(in_dev)	IN_DEV_ORCONF((in_dev), SEND_REDIRECTS)
#define IN_DEV_SEC_REDIRECTS(in_dev)	IN_DEV_ORCONF((in_dev), \
						      SECURE_REDIRECTS)
#define IN_DEV_IDTAG(in_dev)		IN_DEV_CONF_GET(in_dev, TAG)
#define IN_DEV_MEDIUM_ID(in_dev)	IN_DEV_CONF_GET(in_dev, MEDIUM_ID)
#define IN_DEV_PROMOTE_SECONDARIES(in_dev) \
					IN_DEV_ORCONF((in_dev), \
						      PROMOTE_SECONDARIES)
#define IN_DEV_ROUTE_LOCALNET(in_dev)	IN_DEV_ORCONF(in_dev, ROUTE_LOCALNET)
#define IN_DEV_NET_ROUTE_LOCALNET(in_dev, net)	\
	IN_DEV_NET_ORCONF(in_dev, net, ROUTE_LOCALNET)

#define IN_DEV_RX_REDIRECTS(in_dev) \
	((IN_DEV_FORWARD(in_dev) && \
	  IN_DEV_ANDCONF((in_dev), ACCEPT_REDIRECTS)) \
	 || (!IN_DEV_FORWARD(in_dev) && \
	  IN_DEV_ORCONF((in_dev), ACCEPT_REDIRECTS)))

#define IN_DEV_ARPFILTER(in_dev)	IN_DEV_ORCONF((in_dev), ARPFILTER)
#define IN_DEV_ARP_ACCEPT(in_dev)	IN_DEV_ORCONF((in_dev), ARP_ACCEPT)
#define IN_DEV_ARP_ANNOUNCE(in_dev)	IN_DEV_MAXCONF((in_dev), ARP_ANNOUNCE)
#define IN_DEV_ARP_IGNORE(in_dev)	IN_DEV_MAXCONF((in_dev), ARP_IGNORE)
#define IN_DEV_ARP_NOTIFY(in_dev)	IN_DEV_MAXCONF((in_dev), ARP_NOTIFY)

struct in_ifaddr {
	struct hlist_node	hash;
	struct in_ifaddr	*ifa_next;
	struct in_device	*ifa_dev;
	struct rcu_head		rcu_head;
	__be32			ifa_local;
	__be32			ifa_address;
	__be32			ifa_mask;
	__be32			ifa_broadcast;
	unsigned char		ifa_scope;
	unsigned char		ifa_flags;
	unsigned char		ifa_prefixlen;
	char			ifa_label[IFNAMSIZ];
};

extern int register_inetaddr_notifier(struct notifier_block *nb);
extern int unregister_inetaddr_notifier(struct notifier_block *nb);

extern void inet_netconf_notify_devconf(struct net *net, int type, int ifindex,
					struct ipv4_devconf *devconf);

extern struct net_device *__ip_dev_find(struct net *net, __be32 addr, bool devref);
static inline struct net_device *ip_dev_find(struct net *net, __be32 addr)
{
	return __ip_dev_find(net, addr, true);
}

extern int		inet_addr_onlink(struct in_device *in_dev, __be32 a, __be32 b);
extern int		devinet_ioctl(struct net *net, unsigned int cmd, void __user *);
extern void		devinet_init(void);
extern struct in_device	*inetdev_by_index(struct net *, int);
extern __be32		inet_select_addr(const struct net_device *dev, __be32 dst, int scope);
extern __be32		inet_confirm_addr(struct in_device *in_dev, __be32 dst, __be32 local, int scope);
extern struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, __be32 prefix, __be32 mask);

static __inline__ int inet_ifa_match(__be32 addr, struct in_ifaddr *ifa)
{
	return !((addr^ifa->ifa_address)&ifa->ifa_mask);
}

/*
 *	Check if a mask is acceptable.
 */
 
static __inline__ int bad_mask(__be32 mask, __be32 addr)
{
	__u32 hmask;
	if (addr & (mask = ~mask))
		return 1;
	hmask = ntohl(mask);
	if (hmask & (hmask+1))
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
	return rcu_dereference(dev->ip_ptr);
}

static inline struct in_device *in_dev_get(const struct net_device *dev)
{
	struct in_device *in_dev;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev)
		atomic_inc(&in_dev->refcnt);
	rcu_read_unlock();
	return in_dev;
}

static inline struct in_device *__in_dev_get_rtnl(const struct net_device *dev)
{
	return rtnl_dereference(dev->ip_ptr);
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

static __inline__ __be32 inet_make_mask(int logmask)
{
	if (logmask)
		return htonl(~((1<<(32-logmask))-1));
	return 0;
}

static __inline__ int inet_mask_len(__be32 mask)
{
	__u32 hmask = ntohl(mask);
	if (!hmask)
		return 0;
	return 32 - ffz(~hmask);
}


#endif /* _LINUX_INETDEVICE_H */
