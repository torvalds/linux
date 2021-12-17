// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *      NET3    Protocol independent device support routines.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 *              Authors:	Ross Biro
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dahinds@users.sourceforge.net>
 *		Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *		Adam Sulmicki <adam@cfar.umd.edu>
 *              Pekka Riikonen <priikone@poesidon.pspt.fi>
 *
 *	Changes:
 *              D.J. Barrow     :       Fixed bug where dev->refcnt gets set
 *                                      to 2 if register_netdev gets called
 *                                      before net_dev_init & also removed a
 *                                      few lines of code in the process.
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant
 *					stunts to keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into
 *					drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *              Alan Cox        :       Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before
 *					calling netif_rx. Saves a function
 *					call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *              Alan Cox        :       Fixed nasty side effect of device close
 *					changes.
 *		Rudi Cilibrasi	:	Pass the right thing to
 *					set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to
 *					make it work out on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *		Andi Kleen	:	Fix error reporting for SIOCGIFCONF
 *	    Michael Chastain	:	Fix signed/unsigned for SIOCGIFCONF
 *		Cyrus Durgin	:	Cleaned for KMOD
 *		Adam Sulmicki   :	Bug Fix : Network Device Unload
 *					A network device unload needs to purge
 *					the backlog queue.
 *	Paul Rusty Russell	:	SIOCSIFNAME
 *              Pekka Riikonen  :	Netdev boot-time settings code
 *              Andrew Morton   :       Make unregister_netdevice wait
 *                                      indefinitely on dev->refcnt
 *              J Hadi Salim    :       - Backlog queue sampling
 *				        - netif_rx() feedback
 */

#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/cpu.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/hash.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/busy_poll.h>
#include <linux/rtnetlink.h>
#include <linux/stat.h>
#include <net/dsa.h>
#include <net/dst.h>
#include <net/dst_metadata.h>
#include <net/gro.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/checksum.h>
#include <net/xfrm.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netpoll.h>
#include <linux/rcupdate.h>
#include <linux/delay.h>
#include <net/iw_handler.h>
#include <asm/current.h>
#include <linux/audit.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <net/mpls.h>
#include <linux/ipv6.h>
#include <linux/in.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <trace/events/napi.h>
#include <trace/events/net.h>
#include <trace/events/skb.h>
#include <trace/events/qdisc.h>
#include <linux/inetdevice.h>
#include <linux/cpu_rmap.h>
#include <linux/static_key.h>
#include <linux/hashtable.h>
#include <linux/vmalloc.h>
#include <linux/if_macvlan.h>
#include <linux/errqueue.h>
#include <linux/hrtimer.h>
#include <linux/netfilter_netdev.h>
#include <linux/crash_dump.h>
#include <linux/sctp.h>
#include <net/udp_tunnel.h>
#include <linux/net_namespace.h>
#include <linux/indirect_call_wrapper.h>
#include <net/devlink.h>
#include <linux/pm_runtime.h>
#include <linux/prandom.h>
#include <linux/once_lite.h>

#include "net-sysfs.h"

#define MAX_GRO_SKBS 8

/* This should be increased if a protocol with a bigger head is added. */
#define GRO_MAX_HEAD (MAX_HEADER + 128)

static DEFINE_SPINLOCK(ptype_lock);
static DEFINE_SPINLOCK(offload_lock);
struct list_head ptype_base[PTYPE_HASH_SIZE] __read_mostly;
struct list_head ptype_all __read_mostly;	/* Taps */
static struct list_head offload_base __read_mostly;

static int netif_rx_internal(struct sk_buff *skb);
static int call_netdevice_notifiers_info(unsigned long val,
					 struct netdev_notifier_info *info);
static int call_netdevice_notifiers_extack(unsigned long val,
					   struct net_device *dev,
					   struct netlink_ext_ack *extack);
static struct napi_struct *napi_by_id(unsigned int napi_id);

/*
 * The @dev_base_head list is protected by @dev_base_lock and the rtnl
 * semaphore.
 *
 * Pure readers hold dev_base_lock for reading, or rcu_read_lock()
 *
 * Writers must hold the rtnl semaphore while they loop through the
 * dev_base_head list, and hold dev_base_lock for writing when they do the
 * actual updates.  This allows pure readers to access the list even
 * while a writer is preparing to update it.
 *
 * To put it another way, dev_base_lock is held for writing only to
 * protect against pure readers; the rtnl semaphore provides the
 * protection against other writers.
 *
 * See, for example usages, register_netdevice() and
 * unregister_netdevice(), which must be called with the rtnl
 * semaphore held.
 */
DEFINE_RWLOCK(dev_base_lock);
EXPORT_SYMBOL(dev_base_lock);

static DEFINE_MUTEX(ifalias_mutex);

/* protects napi_hash addition/deletion and napi_gen_id */
static DEFINE_SPINLOCK(napi_hash_lock);

static unsigned int napi_gen_id = NR_CPUS;
static DEFINE_READ_MOSTLY_HASHTABLE(napi_hash, 8);

static DECLARE_RWSEM(devnet_rename_sem);

static inline void dev_base_seq_inc(struct net *net)
{
	while (++net->dev_base_seq == 0)
		;
}

static inline struct hlist_head *dev_name_hash(struct net *net, const char *name)
{
	unsigned int hash = full_name_hash(net, name, strnlen(name, IFNAMSIZ));

	return &net->dev_name_head[hash_32(hash, NETDEV_HASHBITS)];
}

static inline struct hlist_head *dev_index_hash(struct net *net, int ifindex)
{
	return &net->dev_index_head[ifindex & (NETDEV_HASHENTRIES - 1)];
}

static inline void rps_lock(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	spin_lock(&sd->input_pkt_queue.lock);
#endif
}

static inline void rps_unlock(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	spin_unlock(&sd->input_pkt_queue.lock);
#endif
}

static struct netdev_name_node *netdev_name_node_alloc(struct net_device *dev,
						       const char *name)
{
	struct netdev_name_node *name_node;

	name_node = kmalloc(sizeof(*name_node), GFP_KERNEL);
	if (!name_node)
		return NULL;
	INIT_HLIST_NODE(&name_node->hlist);
	name_node->dev = dev;
	name_node->name = name;
	return name_node;
}

static struct netdev_name_node *
netdev_name_node_head_alloc(struct net_device *dev)
{
	struct netdev_name_node *name_node;

	name_node = netdev_name_node_alloc(dev, dev->name);
	if (!name_node)
		return NULL;
	INIT_LIST_HEAD(&name_node->list);
	return name_node;
}

static void netdev_name_node_free(struct netdev_name_node *name_node)
{
	kfree(name_node);
}

static void netdev_name_node_add(struct net *net,
				 struct netdev_name_node *name_node)
{
	hlist_add_head_rcu(&name_node->hlist,
			   dev_name_hash(net, name_node->name));
}

static void netdev_name_node_del(struct netdev_name_node *name_node)
{
	hlist_del_rcu(&name_node->hlist);
}

static struct netdev_name_node *netdev_name_node_lookup(struct net *net,
							const char *name)
{
	struct hlist_head *head = dev_name_hash(net, name);
	struct netdev_name_node *name_node;

	hlist_for_each_entry(name_node, head, hlist)
		if (!strcmp(name_node->name, name))
			return name_node;
	return NULL;
}

static struct netdev_name_node *netdev_name_node_lookup_rcu(struct net *net,
							    const char *name)
{
	struct hlist_head *head = dev_name_hash(net, name);
	struct netdev_name_node *name_node;

	hlist_for_each_entry_rcu(name_node, head, hlist)
		if (!strcmp(name_node->name, name))
			return name_node;
	return NULL;
}

bool netdev_name_in_use(struct net *net, const char *name)
{
	return netdev_name_node_lookup(net, name);
}
EXPORT_SYMBOL(netdev_name_in_use);

int netdev_name_node_alt_create(struct net_device *dev, const char *name)
{
	struct netdev_name_node *name_node;
	struct net *net = dev_net(dev);

	name_node = netdev_name_node_lookup(net, name);
	if (name_node)
		return -EEXIST;
	name_node = netdev_name_node_alloc(dev, name);
	if (!name_node)
		return -ENOMEM;
	netdev_name_node_add(net, name_node);
	/* The node that holds dev->name acts as a head of per-device list. */
	list_add_tail(&name_node->list, &dev->name_node->list);

	return 0;
}
EXPORT_SYMBOL(netdev_name_node_alt_create);

static void __netdev_name_node_alt_destroy(struct netdev_name_node *name_node)
{
	list_del(&name_node->list);
	netdev_name_node_del(name_node);
	kfree(name_node->name);
	netdev_name_node_free(name_node);
}

int netdev_name_node_alt_destroy(struct net_device *dev, const char *name)
{
	struct netdev_name_node *name_node;
	struct net *net = dev_net(dev);

	name_node = netdev_name_node_lookup(net, name);
	if (!name_node)
		return -ENOENT;
	/* lookup might have found our primary name or a name belonging
	 * to another device.
	 */
	if (name_node == dev->name_node || name_node->dev != dev)
		return -EINVAL;

	__netdev_name_node_alt_destroy(name_node);

	return 0;
}
EXPORT_SYMBOL(netdev_name_node_alt_destroy);

static void netdev_name_node_alt_flush(struct net_device *dev)
{
	struct netdev_name_node *name_node, *tmp;

	list_for_each_entry_safe(name_node, tmp, &dev->name_node->list, list)
		__netdev_name_node_alt_destroy(name_node);
}

/* Device list insertion */
static void list_netdevice(struct net_device *dev)
{
	struct net *net = dev_net(dev);

	ASSERT_RTNL();

	write_lock_bh(&dev_base_lock);
	list_add_tail_rcu(&dev->dev_list, &net->dev_base_head);
	netdev_name_node_add(net, dev->name_node);
	hlist_add_head_rcu(&dev->index_hlist,
			   dev_index_hash(net, dev->ifindex));
	write_unlock_bh(&dev_base_lock);

	dev_base_seq_inc(net);
}

/* Device list removal
 * caller must respect a RCU grace period before freeing/reusing dev
 */
static void unlist_netdevice(struct net_device *dev)
{
	ASSERT_RTNL();

	/* Unlink dev from the device chain */
	write_lock_bh(&dev_base_lock);
	list_del_rcu(&dev->dev_list);
	netdev_name_node_del(dev->name_node);
	hlist_del_rcu(&dev->index_hlist);
	write_unlock_bh(&dev_base_lock);

	dev_base_seq_inc(dev_net(dev));
}

/*
 *	Our notifier list
 */

static RAW_NOTIFIER_HEAD(netdev_chain);

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the local softnet handler.
 */

DEFINE_PER_CPU_ALIGNED(struct softnet_data, softnet_data);
EXPORT_PER_CPU_SYMBOL(softnet_data);

#ifdef CONFIG_LOCKDEP
/*
 * register_netdevice() inits txq->_xmit_lock and sets lockdep class
 * according to dev->type
 */
static const unsigned short netdev_lock_type[] = {
	 ARPHRD_NETROM, ARPHRD_ETHER, ARPHRD_EETHER, ARPHRD_AX25,
	 ARPHRD_PRONET, ARPHRD_CHAOS, ARPHRD_IEEE802, ARPHRD_ARCNET,
	 ARPHRD_APPLETLK, ARPHRD_DLCI, ARPHRD_ATM, ARPHRD_METRICOM,
	 ARPHRD_IEEE1394, ARPHRD_EUI64, ARPHRD_INFINIBAND, ARPHRD_SLIP,
	 ARPHRD_CSLIP, ARPHRD_SLIP6, ARPHRD_CSLIP6, ARPHRD_RSRVD,
	 ARPHRD_ADAPT, ARPHRD_ROSE, ARPHRD_X25, ARPHRD_HWX25,
	 ARPHRD_PPP, ARPHRD_CISCO, ARPHRD_LAPB, ARPHRD_DDCMP,
	 ARPHRD_RAWHDLC, ARPHRD_TUNNEL, ARPHRD_TUNNEL6, ARPHRD_FRAD,
	 ARPHRD_SKIP, ARPHRD_LOOPBACK, ARPHRD_LOCALTLK, ARPHRD_FDDI,
	 ARPHRD_BIF, ARPHRD_SIT, ARPHRD_IPDDP, ARPHRD_IPGRE,
	 ARPHRD_PIMREG, ARPHRD_HIPPI, ARPHRD_ASH, ARPHRD_ECONET,
	 ARPHRD_IRDA, ARPHRD_FCPP, ARPHRD_FCAL, ARPHRD_FCPL,
	 ARPHRD_FCFABRIC, ARPHRD_IEEE80211, ARPHRD_IEEE80211_PRISM,
	 ARPHRD_IEEE80211_RADIOTAP, ARPHRD_PHONET, ARPHRD_PHONET_PIPE,
	 ARPHRD_IEEE802154, ARPHRD_VOID, ARPHRD_NONE};

static const char *const netdev_lock_name[] = {
	"_xmit_NETROM", "_xmit_ETHER", "_xmit_EETHER", "_xmit_AX25",
	"_xmit_PRONET", "_xmit_CHAOS", "_xmit_IEEE802", "_xmit_ARCNET",
	"_xmit_APPLETLK", "_xmit_DLCI", "_xmit_ATM", "_xmit_METRICOM",
	"_xmit_IEEE1394", "_xmit_EUI64", "_xmit_INFINIBAND", "_xmit_SLIP",
	"_xmit_CSLIP", "_xmit_SLIP6", "_xmit_CSLIP6", "_xmit_RSRVD",
	"_xmit_ADAPT", "_xmit_ROSE", "_xmit_X25", "_xmit_HWX25",
	"_xmit_PPP", "_xmit_CISCO", "_xmit_LAPB", "_xmit_DDCMP",
	"_xmit_RAWHDLC", "_xmit_TUNNEL", "_xmit_TUNNEL6", "_xmit_FRAD",
	"_xmit_SKIP", "_xmit_LOOPBACK", "_xmit_LOCALTLK", "_xmit_FDDI",
	"_xmit_BIF", "_xmit_SIT", "_xmit_IPDDP", "_xmit_IPGRE",
	"_xmit_PIMREG", "_xmit_HIPPI", "_xmit_ASH", "_xmit_ECONET",
	"_xmit_IRDA", "_xmit_FCPP", "_xmit_FCAL", "_xmit_FCPL",
	"_xmit_FCFABRIC", "_xmit_IEEE80211", "_xmit_IEEE80211_PRISM",
	"_xmit_IEEE80211_RADIOTAP", "_xmit_PHONET", "_xmit_PHONET_PIPE",
	"_xmit_IEEE802154", "_xmit_VOID", "_xmit_NONE"};

static struct lock_class_key netdev_xmit_lock_key[ARRAY_SIZE(netdev_lock_type)];
static struct lock_class_key netdev_addr_lock_key[ARRAY_SIZE(netdev_lock_type)];

static inline unsigned short netdev_lock_pos(unsigned short dev_type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(netdev_lock_type); i++)
		if (netdev_lock_type[i] == dev_type)
			return i;
	/* the last key is used by default */
	return ARRAY_SIZE(netdev_lock_type) - 1;
}

static inline void netdev_set_xmit_lockdep_class(spinlock_t *lock,
						 unsigned short dev_type)
{
	int i;

	i = netdev_lock_pos(dev_type);
	lockdep_set_class_and_name(lock, &netdev_xmit_lock_key[i],
				   netdev_lock_name[i]);
}

static inline void netdev_set_addr_lockdep_class(struct net_device *dev)
{
	int i;

	i = netdev_lock_pos(dev->type);
	lockdep_set_class_and_name(&dev->addr_list_lock,
				   &netdev_addr_lock_key[i],
				   netdev_lock_name[i]);
}
#else
static inline void netdev_set_xmit_lockdep_class(spinlock_t *lock,
						 unsigned short dev_type)
{
}

static inline void netdev_set_addr_lockdep_class(struct net_device *dev)
{
}
#endif

/*******************************************************************************
 *
 *		Protocol management and registration routines
 *
 *******************************************************************************/


/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 *
 *	BEWARE!!! Protocol handlers, mangling input packets,
 *	MUST BE last in hash buckets and checking protocol handlers
 *	MUST start from promiscuous ptype_all chain in net_bh.
 *	It is true now, do not change it.
 *	Explanation follows: if protocol handler, mangling packet, will
 *	be the first on list, it is not able to sense, that packet
 *	is cloned and should be copied-on-write, so that it will
 *	change it and subsequent readers will get broken packet.
 *							--ANK (980803)
 */

static inline struct list_head *ptype_head(const struct packet_type *pt)
{
	if (pt->type == htons(ETH_P_ALL))
		return pt->dev ? &pt->dev->ptype_all : &ptype_all;
	else
		return pt->dev ? &pt->dev->ptype_specific :
				 &ptype_base[ntohs(pt->type) & PTYPE_HASH_MASK];
}

/**
 *	dev_add_pack - add packet handler
 *	@pt: packet type declaration
 *
 *	Add a protocol handler to the networking stack. The passed &packet_type
 *	is linked into kernel lists and may not be freed until it has been
 *	removed from the kernel lists.
 *
 *	This call does not sleep therefore it can not
 *	guarantee all CPU's that are in middle of receiving packets
 *	will see the new packet type (until the next received packet).
 */

void dev_add_pack(struct packet_type *pt)
{
	struct list_head *head = ptype_head(pt);

	spin_lock(&ptype_lock);
	list_add_rcu(&pt->list, head);
	spin_unlock(&ptype_lock);
}
EXPORT_SYMBOL(dev_add_pack);

/**
 *	__dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *      The packet type might still be in use by receivers
 *	and must not be freed until after all the CPU's have gone
 *	through a quiescent state.
 */
void __dev_remove_pack(struct packet_type *pt)
{
	struct list_head *head = ptype_head(pt);
	struct packet_type *pt1;

	spin_lock(&ptype_lock);

	list_for_each_entry(pt1, head, list) {
		if (pt == pt1) {
			list_del_rcu(&pt->list);
			goto out;
		}
	}

	pr_warn("dev_remove_pack: %p not found\n", pt);
out:
	spin_unlock(&ptype_lock);
}
EXPORT_SYMBOL(__dev_remove_pack);

/**
 *	dev_remove_pack	 - remove packet handler
 *	@pt: packet type declaration
 *
 *	Remove a protocol handler that was previously added to the kernel
 *	protocol handlers by dev_add_pack(). The passed &packet_type is removed
 *	from the kernel lists and can be freed or reused once this function
 *	returns.
 *
 *	This call sleeps to guarantee that no CPU is looking at the packet
 *	type after return.
 */
void dev_remove_pack(struct packet_type *pt)
{
	__dev_remove_pack(pt);

	synchronize_net();
}
EXPORT_SYMBOL(dev_remove_pack);


/**
 *	dev_add_offload - register offload handlers
 *	@po: protocol offload declaration
 *
 *	Add protocol offload handlers to the networking stack. The passed
 *	&proto_offload is linked into kernel lists and may not be freed until
 *	it has been removed from the kernel lists.
 *
 *	This call does not sleep therefore it can not
 *	guarantee all CPU's that are in middle of receiving packets
 *	will see the new offload handlers (until the next received packet).
 */
void dev_add_offload(struct packet_offload *po)
{
	struct packet_offload *elem;

	spin_lock(&offload_lock);
	list_for_each_entry(elem, &offload_base, list) {
		if (po->priority < elem->priority)
			break;
	}
	list_add_rcu(&po->list, elem->list.prev);
	spin_unlock(&offload_lock);
}
EXPORT_SYMBOL(dev_add_offload);

/**
 *	__dev_remove_offload	 - remove offload handler
 *	@po: packet offload declaration
 *
 *	Remove a protocol offload handler that was previously added to the
 *	kernel offload handlers by dev_add_offload(). The passed &offload_type
 *	is removed from the kernel lists and can be freed or reused once this
 *	function returns.
 *
 *      The packet type might still be in use by receivers
 *	and must not be freed until after all the CPU's have gone
 *	through a quiescent state.
 */
static void __dev_remove_offload(struct packet_offload *po)
{
	struct list_head *head = &offload_base;
	struct packet_offload *po1;

	spin_lock(&offload_lock);

	list_for_each_entry(po1, head, list) {
		if (po == po1) {
			list_del_rcu(&po->list);
			goto out;
		}
	}

	pr_warn("dev_remove_offload: %p not found\n", po);
out:
	spin_unlock(&offload_lock);
}

/**
 *	dev_remove_offload	 - remove packet offload handler
 *	@po: packet offload declaration
 *
 *	Remove a packet offload handler that was previously added to the kernel
 *	offload handlers by dev_add_offload(). The passed &offload_type is
 *	removed from the kernel lists and can be freed or reused once this
 *	function returns.
 *
 *	This call sleeps to guarantee that no CPU is looking at the packet
 *	type after return.
 */
void dev_remove_offload(struct packet_offload *po)
{
	__dev_remove_offload(po);

	synchronize_net();
}
EXPORT_SYMBOL(dev_remove_offload);

/*******************************************************************************
 *
 *			    Device Interface Subroutines
 *
 *******************************************************************************/

/**
 *	dev_get_iflink	- get 'iflink' value of a interface
 *	@dev: targeted interface
 *
 *	Indicates the ifindex the interface is linked to.
 *	Physical interfaces have the same 'ifindex' and 'iflink' values.
 */

int dev_get_iflink(const struct net_device *dev)
{
	if (dev->netdev_ops && dev->netdev_ops->ndo_get_iflink)
		return dev->netdev_ops->ndo_get_iflink(dev);

	return dev->ifindex;
}
EXPORT_SYMBOL(dev_get_iflink);

/**
 *	dev_fill_metadata_dst - Retrieve tunnel egress information.
 *	@dev: targeted interface
 *	@skb: The packet.
 *
 *	For better visibility of tunnel traffic OVS needs to retrieve
 *	egress tunnel information for a packet. Following API allows
 *	user to get this info.
 */
int dev_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb)
{
	struct ip_tunnel_info *info;

	if (!dev->netdev_ops  || !dev->netdev_ops->ndo_fill_metadata_dst)
		return -EINVAL;

	info = skb_tunnel_info_unclone(skb);
	if (!info)
		return -ENOMEM;
	if (unlikely(!(info->mode & IP_TUNNEL_INFO_TX)))
		return -EINVAL;

	return dev->netdev_ops->ndo_fill_metadata_dst(dev, skb);
}
EXPORT_SYMBOL_GPL(dev_fill_metadata_dst);

static struct net_device_path *dev_fwd_path(struct net_device_path_stack *stack)
{
	int k = stack->num_paths++;

	if (WARN_ON_ONCE(k >= NET_DEVICE_PATH_STACK_MAX))
		return NULL;

	return &stack->path[k];
}

int dev_fill_forward_path(const struct net_device *dev, const u8 *daddr,
			  struct net_device_path_stack *stack)
{
	const struct net_device *last_dev;
	struct net_device_path_ctx ctx = {
		.dev	= dev,
		.daddr	= daddr,
	};
	struct net_device_path *path;
	int ret = 0;

	stack->num_paths = 0;
	while (ctx.dev && ctx.dev->netdev_ops->ndo_fill_forward_path) {
		last_dev = ctx.dev;
		path = dev_fwd_path(stack);
		if (!path)
			return -1;

		memset(path, 0, sizeof(struct net_device_path));
		ret = ctx.dev->netdev_ops->ndo_fill_forward_path(&ctx, path);
		if (ret < 0)
			return -1;

		if (WARN_ON_ONCE(last_dev == ctx.dev))
			return -1;
	}
	path = dev_fwd_path(stack);
	if (!path)
		return -1;
	path->type = DEV_PATH_ETHERNET;
	path->dev = ctx.dev;

	return ret;
}
EXPORT_SYMBOL_GPL(dev_fill_forward_path);

/**
 *	__dev_get_by_name	- find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *
 *	Find an interface by name. Must be called under RTNL semaphore
 *	or @dev_base_lock. If the name is found a pointer to the device
 *	is returned. If the name is not found then %NULL is returned. The
 *	reference counters are not incremented so the caller must be
 *	careful with locks.
 */

struct net_device *__dev_get_by_name(struct net *net, const char *name)
{
	struct netdev_name_node *node_name;

	node_name = netdev_name_node_lookup(net, name);
	return node_name ? node_name->dev : NULL;
}
EXPORT_SYMBOL(__dev_get_by_name);

/**
 * dev_get_by_name_rcu	- find a device by its name
 * @net: the applicable net namespace
 * @name: name to find
 *
 * Find an interface by name.
 * If the name is found a pointer to the device is returned.
 * If the name is not found then %NULL is returned.
 * The reference counters are not incremented so the caller must be
 * careful with locks. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_name_rcu(struct net *net, const char *name)
{
	struct netdev_name_node *node_name;

	node_name = netdev_name_node_lookup_rcu(net, name);
	return node_name ? node_name->dev : NULL;
}
EXPORT_SYMBOL(dev_get_by_name_rcu);

/**
 *	dev_get_by_name		- find a device by its name
 *	@net: the applicable net namespace
 *	@name: name to find
 *
 *	Find an interface by name. This can be called from any
 *	context and does its own locking. The returned handle has
 *	the usage count incremented and the caller must use dev_put() to
 *	release it when it is no longer needed. %NULL is returned if no
 *	matching device is found.
 */

struct net_device *dev_get_by_name(struct net *net, const char *name)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_name_rcu(net, name);
	dev_hold(dev);
	rcu_read_unlock();
	return dev;
}
EXPORT_SYMBOL(dev_get_by_name);

/**
 *	__dev_get_by_index - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold either the RTNL semaphore
 *	or @dev_base_lock.
 */

struct net_device *__dev_get_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;
	struct hlist_head *head = dev_index_hash(net, ifindex);

	hlist_for_each_entry(dev, head, index_hlist)
		if (dev->ifindex == ifindex)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(__dev_get_by_index);

/**
 *	dev_get_by_index_rcu - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not
 *	had its reference counter increased so the caller must be careful
 *	about locking. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_index_rcu(struct net *net, int ifindex)
{
	struct net_device *dev;
	struct hlist_head *head = dev_index_hash(net, ifindex);

	hlist_for_each_entry_rcu(dev, head, index_hlist)
		if (dev->ifindex == ifindex)
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_get_by_index_rcu);


/**
 *	dev_get_by_index - find a device by its ifindex
 *	@net: the applicable net namespace
 *	@ifindex: index of device
 *
 *	Search for an interface by index. Returns NULL if the device
 *	is not found or a pointer to the device. The device returned has
 *	had a reference added and the pointer is safe until the user calls
 *	dev_put to indicate they have finished with it.
 */

struct net_device *dev_get_by_index(struct net *net, int ifindex)
{
	struct net_device *dev;

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, ifindex);
	dev_hold(dev);
	rcu_read_unlock();
	return dev;
}
EXPORT_SYMBOL(dev_get_by_index);

/**
 *	dev_get_by_napi_id - find a device by napi_id
 *	@napi_id: ID of the NAPI struct
 *
 *	Search for an interface by NAPI ID. Returns %NULL if the device
 *	is not found or a pointer to the device. The device has not had
 *	its reference counter increased so the caller must be careful
 *	about locking. The caller must hold RCU lock.
 */

struct net_device *dev_get_by_napi_id(unsigned int napi_id)
{
	struct napi_struct *napi;

	WARN_ON_ONCE(!rcu_read_lock_held());

	if (napi_id < MIN_NAPI_ID)
		return NULL;

	napi = napi_by_id(napi_id);

	return napi ? napi->dev : NULL;
}
EXPORT_SYMBOL(dev_get_by_napi_id);

/**
 *	netdev_get_name - get a netdevice name, knowing its ifindex.
 *	@net: network namespace
 *	@name: a pointer to the buffer where the name will be stored.
 *	@ifindex: the ifindex of the interface to get the name from.
 */
int netdev_get_name(struct net *net, char *name, int ifindex)
{
	struct net_device *dev;
	int ret;

	down_read(&devnet_rename_sem);
	rcu_read_lock();

	dev = dev_get_by_index_rcu(net, ifindex);
	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	strcpy(name, dev->name);

	ret = 0;
out:
	rcu_read_unlock();
	up_read(&devnet_rename_sem);
	return ret;
}

/**
 *	dev_getbyhwaddr_rcu - find a device by its hardware address
 *	@net: the applicable net namespace
 *	@type: media type of device
 *	@ha: hardware address
 *
 *	Search for an interface by MAC address. Returns NULL if the device
 *	is not found or a pointer to the device.
 *	The caller must hold RCU or RTNL.
 *	The returned device has not had its ref count increased
 *	and the caller must therefore be careful about locking
 *
 */

struct net_device *dev_getbyhwaddr_rcu(struct net *net, unsigned short type,
				       const char *ha)
{
	struct net_device *dev;

	for_each_netdev_rcu(net, dev)
		if (dev->type == type &&
		    !memcmp(dev->dev_addr, ha, dev->addr_len))
			return dev;

	return NULL;
}
EXPORT_SYMBOL(dev_getbyhwaddr_rcu);

struct net_device *dev_getfirstbyhwtype(struct net *net, unsigned short type)
{
	struct net_device *dev, *ret = NULL;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev)
		if (dev->type == type) {
			dev_hold(dev);
			ret = dev;
			break;
		}
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL(dev_getfirstbyhwtype);

/**
 *	__dev_get_by_flags - find any device with given flags
 *	@net: the applicable net namespace
 *	@if_flags: IFF_* values
 *	@mask: bitmask of bits in if_flags to check
 *
 *	Search for any interface with the given flags. Returns NULL if a device
 *	is not found or a pointer to the device. Must be called inside
 *	rtnl_lock(), and result refcount is unchanged.
 */

struct net_device *__dev_get_by_flags(struct net *net, unsigned short if_flags,
				      unsigned short mask)
{
	struct net_device *dev, *ret;

	ASSERT_RTNL();

	ret = NULL;
	for_each_netdev(net, dev) {
		if (((dev->flags ^ if_flags) & mask) == 0) {
			ret = dev;
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL(__dev_get_by_flags);

/**
 *	dev_valid_name - check if name is okay for network device
 *	@name: name string
 *
 *	Network device names need to be valid file names to
 *	allow sysfs to work.  We also disallow any kind of
 *	whitespace.
 */
bool dev_valid_name(const char *name)
{
	if (*name == '\0')
		return false;
	if (strnlen(name, IFNAMSIZ) == IFNAMSIZ)
		return false;
	if (!strcmp(name, ".") || !strcmp(name, ".."))
		return false;

	while (*name) {
		if (*name == '/' || *name == ':' || isspace(*name))
			return false;
		name++;
	}
	return true;
}
EXPORT_SYMBOL(dev_valid_name);

/**
 *	__dev_alloc_name - allocate a name for a device
 *	@net: network namespace to allocate the device name in
 *	@name: name format string
 *	@buf:  scratch buffer and result name string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. It scans list of devices to build up a free map, then chooses
 *	the first empty slot. The caller must hold the dev_base or rtnl lock
 *	while allocating the name and adding the device in order to avoid
 *	duplicates.
 *	Limited to bits_per_byte * page size devices (ie 32K on most platforms).
 *	Returns the number of the unit assigned or a negative errno code.
 */

static int __dev_alloc_name(struct net *net, const char *name, char *buf)
{
	int i = 0;
	const char *p;
	const int max_netdevices = 8*PAGE_SIZE;
	unsigned long *inuse;
	struct net_device *d;

	if (!dev_valid_name(name))
		return -EINVAL;

	p = strchr(name, '%');
	if (p) {
		/*
		 * Verify the string as this thing may have come from
		 * the user.  There must be either one "%d" and no other "%"
		 * characters.
		 */
		if (p[1] != 'd' || strchr(p + 2, '%'))
			return -EINVAL;

		/* Use one page as a bit array of possible slots */
		inuse = (unsigned long *) get_zeroed_page(GFP_ATOMIC);
		if (!inuse)
			return -ENOMEM;

		for_each_netdev(net, d) {
			struct netdev_name_node *name_node;
			list_for_each_entry(name_node, &d->name_node->list, list) {
				if (!sscanf(name_node->name, name, &i))
					continue;
				if (i < 0 || i >= max_netdevices)
					continue;

				/*  avoid cases where sscanf is not exact inverse of printf */
				snprintf(buf, IFNAMSIZ, name, i);
				if (!strncmp(buf, name_node->name, IFNAMSIZ))
					set_bit(i, inuse);
			}
			if (!sscanf(d->name, name, &i))
				continue;
			if (i < 0 || i >= max_netdevices)
				continue;

			/*  avoid cases where sscanf is not exact inverse of printf */
			snprintf(buf, IFNAMSIZ, name, i);
			if (!strncmp(buf, d->name, IFNAMSIZ))
				set_bit(i, inuse);
		}

		i = find_first_zero_bit(inuse, max_netdevices);
		free_page((unsigned long) inuse);
	}

	snprintf(buf, IFNAMSIZ, name, i);
	if (!netdev_name_in_use(net, buf))
		return i;

	/* It is possible to run out of possible slots
	 * when the name is long and there isn't enough space left
	 * for the digits, or if all bits are used.
	 */
	return -ENFILE;
}

static int dev_alloc_name_ns(struct net *net,
			     struct net_device *dev,
			     const char *name)
{
	char buf[IFNAMSIZ];
	int ret;

	BUG_ON(!net);
	ret = __dev_alloc_name(net, name, buf);
	if (ret >= 0)
		strlcpy(dev->name, buf, IFNAMSIZ);
	return ret;
}

/**
 *	dev_alloc_name - allocate a name for a device
 *	@dev: device
 *	@name: name format string
 *
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. It scans list of devices to build up a free map, then chooses
 *	the first empty slot. The caller must hold the dev_base or rtnl lock
 *	while allocating the name and adding the device in order to avoid
 *	duplicates.
 *	Limited to bits_per_byte * page size devices (ie 32K on most platforms).
 *	Returns the number of the unit assigned or a negative errno code.
 */

int dev_alloc_name(struct net_device *dev, const char *name)
{
	return dev_alloc_name_ns(dev_net(dev), dev, name);
}
EXPORT_SYMBOL(dev_alloc_name);

static int dev_get_valid_name(struct net *net, struct net_device *dev,
			      const char *name)
{
	BUG_ON(!net);

	if (!dev_valid_name(name))
		return -EINVAL;

	if (strchr(name, '%'))
		return dev_alloc_name_ns(net, dev, name);
	else if (netdev_name_in_use(net, name))
		return -EEXIST;
	else if (dev->name != name)
		strlcpy(dev->name, name, IFNAMSIZ);

	return 0;
}

/**
 *	dev_change_name - change name of a device
 *	@dev: device
 *	@newname: name (or format string) must be at least IFNAMSIZ
 *
 *	Change name of a device, can pass format strings "eth%d".
 *	for wildcarding.
 */
int dev_change_name(struct net_device *dev, const char *newname)
{
	unsigned char old_assign_type;
	char oldname[IFNAMSIZ];
	int err = 0;
	int ret;
	struct net *net;

	ASSERT_RTNL();
	BUG_ON(!dev_net(dev));

	net = dev_net(dev);

	/* Some auto-enslaved devices e.g. failover slaves are
	 * special, as userspace might rename the device after
	 * the interface had been brought up and running since
	 * the point kernel initiated auto-enslavement. Allow
	 * live name change even when these slave devices are
	 * up and running.
	 *
	 * Typically, users of these auto-enslaving devices
	 * don't actually care about slave name change, as
	 * they are supposed to operate on master interface
	 * directly.
	 */
	if (dev->flags & IFF_UP &&
	    likely(!(dev->priv_flags & IFF_LIVE_RENAME_OK)))
		return -EBUSY;

	down_write(&devnet_rename_sem);

	if (strncmp(newname, dev->name, IFNAMSIZ) == 0) {
		up_write(&devnet_rename_sem);
		return 0;
	}

	memcpy(oldname, dev->name, IFNAMSIZ);

	err = dev_get_valid_name(net, dev, newname);
	if (err < 0) {
		up_write(&devnet_rename_sem);
		return err;
	}

	if (oldname[0] && !strchr(oldname, '%'))
		netdev_info(dev, "renamed from %s\n", oldname);

	old_assign_type = dev->name_assign_type;
	dev->name_assign_type = NET_NAME_RENAMED;

rollback:
	ret = device_rename(&dev->dev, dev->name);
	if (ret) {
		memcpy(dev->name, oldname, IFNAMSIZ);
		dev->name_assign_type = old_assign_type;
		up_write(&devnet_rename_sem);
		return ret;
	}

	up_write(&devnet_rename_sem);

	netdev_adjacent_rename_links(dev, oldname);

	write_lock_bh(&dev_base_lock);
	netdev_name_node_del(dev->name_node);
	write_unlock_bh(&dev_base_lock);

	synchronize_rcu();

	write_lock_bh(&dev_base_lock);
	netdev_name_node_add(net, dev->name_node);
	write_unlock_bh(&dev_base_lock);

	ret = call_netdevice_notifiers(NETDEV_CHANGENAME, dev);
	ret = notifier_to_errno(ret);

	if (ret) {
		/* err >= 0 after dev_alloc_name() or stores the first errno */
		if (err >= 0) {
			err = ret;
			down_write(&devnet_rename_sem);
			memcpy(dev->name, oldname, IFNAMSIZ);
			memcpy(oldname, newname, IFNAMSIZ);
			dev->name_assign_type = old_assign_type;
			old_assign_type = NET_NAME_RENAMED;
			goto rollback;
		} else {
			netdev_err(dev, "name change rollback failed: %d\n",
				   ret);
		}
	}

	return err;
}

/**
 *	dev_set_alias - change ifalias of a device
 *	@dev: device
 *	@alias: name up to IFALIASZ
 *	@len: limit of bytes to copy from info
 *
 *	Set ifalias for a device,
 */
int dev_set_alias(struct net_device *dev, const char *alias, size_t len)
{
	struct dev_ifalias *new_alias = NULL;

	if (len >= IFALIASZ)
		return -EINVAL;

	if (len) {
		new_alias = kmalloc(sizeof(*new_alias) + len + 1, GFP_KERNEL);
		if (!new_alias)
			return -ENOMEM;

		memcpy(new_alias->ifalias, alias, len);
		new_alias->ifalias[len] = 0;
	}

	mutex_lock(&ifalias_mutex);
	new_alias = rcu_replace_pointer(dev->ifalias, new_alias,
					mutex_is_locked(&ifalias_mutex));
	mutex_unlock(&ifalias_mutex);

	if (new_alias)
		kfree_rcu(new_alias, rcuhead);

	return len;
}
EXPORT_SYMBOL(dev_set_alias);

/**
 *	dev_get_alias - get ifalias of a device
 *	@dev: device
 *	@name: buffer to store name of ifalias
 *	@len: size of buffer
 *
 *	get ifalias for a device.  Caller must make sure dev cannot go
 *	away,  e.g. rcu read lock or own a reference count to device.
 */
int dev_get_alias(const struct net_device *dev, char *name, size_t len)
{
	const struct dev_ifalias *alias;
	int ret = 0;

	rcu_read_lock();
	alias = rcu_dereference(dev->ifalias);
	if (alias)
		ret = snprintf(name, len, "%s", alias->ifalias);
	rcu_read_unlock();

	return ret;
}

/**
 *	netdev_features_change - device changes features
 *	@dev: device to cause notification
 *
 *	Called to indicate a device has changed features.
 */
void netdev_features_change(struct net_device *dev)
{
	call_netdevice_notifiers(NETDEV_FEAT_CHANGE, dev);
}
EXPORT_SYMBOL(netdev_features_change);

/**
 *	netdev_state_change - device changes state
 *	@dev: device to cause notification
 *
 *	Called to indicate a device has changed state. This function calls
 *	the notifier chains for netdev_chain and sends a NEWLINK message
 *	to the routing socket.
 */
void netdev_state_change(struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		struct netdev_notifier_change_info change_info = {
			.info.dev = dev,
		};

		call_netdevice_notifiers_info(NETDEV_CHANGE,
					      &change_info.info);
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0, GFP_KERNEL);
	}
}
EXPORT_SYMBOL(netdev_state_change);

/**
 * __netdev_notify_peers - notify network peers about existence of @dev,
 * to be called when rtnl lock is already held.
 * @dev: network device
 *
 * Generate traffic such that interested network peers are aware of
 * @dev, such as by generating a gratuitous ARP. This may be used when
 * a device wants to inform the rest of the network about some sort of
 * reconfiguration such as a failover event or virtual machine
 * migration.
 */
void __netdev_notify_peers(struct net_device *dev)
{
	ASSERT_RTNL();
	call_netdevice_notifiers(NETDEV_NOTIFY_PEERS, dev);
	call_netdevice_notifiers(NETDEV_RESEND_IGMP, dev);
}
EXPORT_SYMBOL(__netdev_notify_peers);

/**
 * netdev_notify_peers - notify network peers about existence of @dev
 * @dev: network device
 *
 * Generate traffic such that interested network peers are aware of
 * @dev, such as by generating a gratuitous ARP. This may be used when
 * a device wants to inform the rest of the network about some sort of
 * reconfiguration such as a failover event or virtual machine
 * migration.
 */
void netdev_notify_peers(struct net_device *dev)
{
	rtnl_lock();
	__netdev_notify_peers(dev);
	rtnl_unlock();
}
EXPORT_SYMBOL(netdev_notify_peers);

static int napi_threaded_poll(void *data);

static int napi_kthread_create(struct napi_struct *n)
{
	int err = 0;

	/* Create and wake up the kthread once to put it in
	 * TASK_INTERRUPTIBLE mode to avoid the blocked task
	 * warning and work with loadavg.
	 */
	n->thread = kthread_run(napi_threaded_poll, n, "napi/%s-%d",
				n->dev->name, n->napi_id);
	if (IS_ERR(n->thread)) {
		err = PTR_ERR(n->thread);
		pr_err("kthread_run failed with err %d\n", err);
		n->thread = NULL;
	}

	return err;
}

static int __dev_open(struct net_device *dev, struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int ret;

	ASSERT_RTNL();

	if (!netif_device_present(dev)) {
		/* may be detached because parent is runtime-suspended */
		if (dev->dev.parent)
			pm_runtime_resume(dev->dev.parent);
		if (!netif_device_present(dev))
			return -ENODEV;
	}

	/* Block netpoll from trying to do any rx path servicing.
	 * If we don't do this there is a chance ndo_poll_controller
	 * or ndo_poll may be running while we open the device
	 */
	netpoll_poll_disable(dev);

	ret = call_netdevice_notifiers_extack(NETDEV_PRE_UP, dev, extack);
	ret = notifier_to_errno(ret);
	if (ret)
		return ret;

	set_bit(__LINK_STATE_START, &dev->state);

	if (ops->ndo_validate_addr)
		ret = ops->ndo_validate_addr(dev);

	if (!ret && ops->ndo_open)
		ret = ops->ndo_open(dev);

	netpoll_poll_enable(dev);

	if (ret)
		clear_bit(__LINK_STATE_START, &dev->state);
	else {
		dev->flags |= IFF_UP;
		dev_set_rx_mode(dev);
		dev_activate(dev);
		add_device_randomness(dev->dev_addr, dev->addr_len);
	}

	return ret;
}

/**
 *	dev_open	- prepare an interface for use.
 *	@dev: device to open
 *	@extack: netlink extended ack
 *
 *	Takes a device from down to up state. The device's private open
 *	function is invoked and then the multicast lists are loaded. Finally
 *	the device is moved into the up state and a %NETDEV_UP message is
 *	sent to the netdev notifier chain.
 *
 *	Calling this function on an active interface is a nop. On a failure
 *	a negative errno code is returned.
 */
int dev_open(struct net_device *dev, struct netlink_ext_ack *extack)
{
	int ret;

	if (dev->flags & IFF_UP)
		return 0;

	ret = __dev_open(dev, extack);
	if (ret < 0)
		return ret;

	rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP|IFF_RUNNING, GFP_KERNEL);
	call_netdevice_notifiers(NETDEV_UP, dev);

	return ret;
}
EXPORT_SYMBOL(dev_open);

static void __dev_close_many(struct list_head *head)
{
	struct net_device *dev;

	ASSERT_RTNL();
	might_sleep();

	list_for_each_entry(dev, head, close_list) {
		/* Temporarily disable netpoll until the interface is down */
		netpoll_poll_disable(dev);

		call_netdevice_notifiers(NETDEV_GOING_DOWN, dev);

		clear_bit(__LINK_STATE_START, &dev->state);

		/* Synchronize to scheduled poll. We cannot touch poll list, it
		 * can be even on different cpu. So just clear netif_running().
		 *
		 * dev->stop() will invoke napi_disable() on all of it's
		 * napi_struct instances on this device.
		 */
		smp_mb__after_atomic(); /* Commit netif_running(). */
	}

	dev_deactivate_many(head);

	list_for_each_entry(dev, head, close_list) {
		const struct net_device_ops *ops = dev->netdev_ops;

		/*
		 *	Call the device specific close. This cannot fail.
		 *	Only if device is UP
		 *
		 *	We allow it to be called even after a DETACH hot-plug
		 *	event.
		 */
		if (ops->ndo_stop)
			ops->ndo_stop(dev);

		dev->flags &= ~IFF_UP;
		netpoll_poll_enable(dev);
	}
}

static void __dev_close(struct net_device *dev)
{
	LIST_HEAD(single);

	list_add(&dev->close_list, &single);
	__dev_close_many(&single);
	list_del(&single);
}

void dev_close_many(struct list_head *head, bool unlink)
{
	struct net_device *dev, *tmp;

	/* Remove the devices that don't need to be closed */
	list_for_each_entry_safe(dev, tmp, head, close_list)
		if (!(dev->flags & IFF_UP))
			list_del_init(&dev->close_list);

	__dev_close_many(head);

	list_for_each_entry_safe(dev, tmp, head, close_list) {
		rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP|IFF_RUNNING, GFP_KERNEL);
		call_netdevice_notifiers(NETDEV_DOWN, dev);
		if (unlink)
			list_del_init(&dev->close_list);
	}
}
EXPORT_SYMBOL(dev_close_many);

/**
 *	dev_close - shutdown an interface.
 *	@dev: device to shutdown
 *
 *	This function moves an active device into down state. A
 *	%NETDEV_GOING_DOWN is sent to the netdev notifier chain. The device
 *	is then deactivated and finally a %NETDEV_DOWN is sent to the notifier
 *	chain.
 */
void dev_close(struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		LIST_HEAD(single);

		list_add(&dev->close_list, &single);
		dev_close_many(&single, true);
		list_del(&single);
	}
}
EXPORT_SYMBOL(dev_close);


/**
 *	dev_disable_lro - disable Large Receive Offload on a device
 *	@dev: device
 *
 *	Disable Large Receive Offload (LRO) on a net device.  Must be
 *	called under RTNL.  This is needed if received packets may be
 *	forwarded to another interface.
 */
void dev_disable_lro(struct net_device *dev)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	dev->wanted_features &= ~NETIF_F_LRO;
	netdev_update_features(dev);

	if (unlikely(dev->features & NETIF_F_LRO))
		netdev_WARN(dev, "failed to disable LRO!\n");

	netdev_for_each_lower_dev(dev, lower_dev, iter)
		dev_disable_lro(lower_dev);
}
EXPORT_SYMBOL(dev_disable_lro);

/**
 *	dev_disable_gro_hw - disable HW Generic Receive Offload on a device
 *	@dev: device
 *
 *	Disable HW Generic Receive Offload (GRO_HW) on a net device.  Must be
 *	called under RTNL.  This is needed if Generic XDP is installed on
 *	the device.
 */
static void dev_disable_gro_hw(struct net_device *dev)
{
	dev->wanted_features &= ~NETIF_F_GRO_HW;
	netdev_update_features(dev);

	if (unlikely(dev->features & NETIF_F_GRO_HW))
		netdev_WARN(dev, "failed to disable GRO_HW!\n");
}

const char *netdev_cmd_to_name(enum netdev_cmd cmd)
{
#define N(val) 						\
	case NETDEV_##val:				\
		return "NETDEV_" __stringify(val);
	switch (cmd) {
	N(UP) N(DOWN) N(REBOOT) N(CHANGE) N(REGISTER) N(UNREGISTER)
	N(CHANGEMTU) N(CHANGEADDR) N(GOING_DOWN) N(CHANGENAME) N(FEAT_CHANGE)
	N(BONDING_FAILOVER) N(PRE_UP) N(PRE_TYPE_CHANGE) N(POST_TYPE_CHANGE)
	N(POST_INIT) N(RELEASE) N(NOTIFY_PEERS) N(JOIN) N(CHANGEUPPER)
	N(RESEND_IGMP) N(PRECHANGEMTU) N(CHANGEINFODATA) N(BONDING_INFO)
	N(PRECHANGEUPPER) N(CHANGELOWERSTATE) N(UDP_TUNNEL_PUSH_INFO)
	N(UDP_TUNNEL_DROP_INFO) N(CHANGE_TX_QUEUE_LEN)
	N(CVLAN_FILTER_PUSH_INFO) N(CVLAN_FILTER_DROP_INFO)
	N(SVLAN_FILTER_PUSH_INFO) N(SVLAN_FILTER_DROP_INFO)
	N(PRE_CHANGEADDR)
	}
#undef N
	return "UNKNOWN_NETDEV_EVENT";
}
EXPORT_SYMBOL_GPL(netdev_cmd_to_name);

static int call_netdevice_notifier(struct notifier_block *nb, unsigned long val,
				   struct net_device *dev)
{
	struct netdev_notifier_info info = {
		.dev = dev,
	};

	return nb->notifier_call(nb, val, &info);
}

static int call_netdevice_register_notifiers(struct notifier_block *nb,
					     struct net_device *dev)
{
	int err;

	err = call_netdevice_notifier(nb, NETDEV_REGISTER, dev);
	err = notifier_to_errno(err);
	if (err)
		return err;

	if (!(dev->flags & IFF_UP))
		return 0;

	call_netdevice_notifier(nb, NETDEV_UP, dev);
	return 0;
}

static void call_netdevice_unregister_notifiers(struct notifier_block *nb,
						struct net_device *dev)
{
	if (dev->flags & IFF_UP) {
		call_netdevice_notifier(nb, NETDEV_GOING_DOWN,
					dev);
		call_netdevice_notifier(nb, NETDEV_DOWN, dev);
	}
	call_netdevice_notifier(nb, NETDEV_UNREGISTER, dev);
}

static int call_netdevice_register_net_notifiers(struct notifier_block *nb,
						 struct net *net)
{
	struct net_device *dev;
	int err;

	for_each_netdev(net, dev) {
		err = call_netdevice_register_notifiers(nb, dev);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	for_each_netdev_continue_reverse(net, dev)
		call_netdevice_unregister_notifiers(nb, dev);
	return err;
}

static void call_netdevice_unregister_net_notifiers(struct notifier_block *nb,
						    struct net *net)
{
	struct net_device *dev;

	for_each_netdev(net, dev)
		call_netdevice_unregister_notifiers(nb, dev);
}

static int dev_boot_phase = 1;

/**
 * register_netdevice_notifier - register a network notifier block
 * @nb: notifier
 *
 * Register a notifier to be called when network device events occur.
 * The notifier passed is linked into the kernel structures and must
 * not be reused until it has been unregistered. A negative errno code
 * is returned on a failure.
 *
 * When registered all registration and up events are replayed
 * to the new notifier to allow device to have a race free
 * view of the network device list.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	struct net *net;
	int err;

	/* Close race with setup_net() and cleanup_net() */
	down_write(&pernet_ops_rwsem);
	rtnl_lock();
	err = raw_notifier_chain_register(&netdev_chain, nb);
	if (err)
		goto unlock;
	if (dev_boot_phase)
		goto unlock;
	for_each_net(net) {
		err = call_netdevice_register_net_notifiers(nb, net);
		if (err)
			goto rollback;
	}

unlock:
	rtnl_unlock();
	up_write(&pernet_ops_rwsem);
	return err;

rollback:
	for_each_net_continue_reverse(net)
		call_netdevice_unregister_net_notifiers(nb, net);

	raw_notifier_chain_unregister(&netdev_chain, nb);
	goto unlock;
}
EXPORT_SYMBOL(register_netdevice_notifier);

/**
 * unregister_netdevice_notifier - unregister a network notifier block
 * @nb: notifier
 *
 * Unregister a notifier previously registered by
 * register_netdevice_notifier(). The notifier is unlinked into the
 * kernel structures and may then be reused. A negative errno code
 * is returned on a failure.
 *
 * After unregistering unregister and down device events are synthesized
 * for all devices on the device list to the removed notifier to remove
 * the need for special case cleanup code.
 */

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	struct net *net;
	int err;

	/* Close race with setup_net() and cleanup_net() */
	down_write(&pernet_ops_rwsem);
	rtnl_lock();
	err = raw_notifier_chain_unregister(&netdev_chain, nb);
	if (err)
		goto unlock;

	for_each_net(net)
		call_netdevice_unregister_net_notifiers(nb, net);

unlock:
	rtnl_unlock();
	up_write(&pernet_ops_rwsem);
	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier);

static int __register_netdevice_notifier_net(struct net *net,
					     struct notifier_block *nb,
					     bool ignore_call_fail)
{
	int err;

	err = raw_notifier_chain_register(&net->netdev_chain, nb);
	if (err)
		return err;
	if (dev_boot_phase)
		return 0;

	err = call_netdevice_register_net_notifiers(nb, net);
	if (err && !ignore_call_fail)
		goto chain_unregister;

	return 0;

chain_unregister:
	raw_notifier_chain_unregister(&net->netdev_chain, nb);
	return err;
}

static int __unregister_netdevice_notifier_net(struct net *net,
					       struct notifier_block *nb)
{
	int err;

	err = raw_notifier_chain_unregister(&net->netdev_chain, nb);
	if (err)
		return err;

	call_netdevice_unregister_net_notifiers(nb, net);
	return 0;
}

/**
 * register_netdevice_notifier_net - register a per-netns network notifier block
 * @net: network namespace
 * @nb: notifier
 *
 * Register a notifier to be called when network device events occur.
 * The notifier passed is linked into the kernel structures and must
 * not be reused until it has been unregistered. A negative errno code
 * is returned on a failure.
 *
 * When registered all registration and up events are replayed
 * to the new notifier to allow device to have a race free
 * view of the network device list.
 */

int register_netdevice_notifier_net(struct net *net, struct notifier_block *nb)
{
	int err;

	rtnl_lock();
	err = __register_netdevice_notifier_net(net, nb, false);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(register_netdevice_notifier_net);

/**
 * unregister_netdevice_notifier_net - unregister a per-netns
 *                                     network notifier block
 * @net: network namespace
 * @nb: notifier
 *
 * Unregister a notifier previously registered by
 * register_netdevice_notifier(). The notifier is unlinked into the
 * kernel structures and may then be reused. A negative errno code
 * is returned on a failure.
 *
 * After unregistering unregister and down device events are synthesized
 * for all devices on the device list to the removed notifier to remove
 * the need for special case cleanup code.
 */

int unregister_netdevice_notifier_net(struct net *net,
				      struct notifier_block *nb)
{
	int err;

	rtnl_lock();
	err = __unregister_netdevice_notifier_net(net, nb);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier_net);

int register_netdevice_notifier_dev_net(struct net_device *dev,
					struct notifier_block *nb,
					struct netdev_net_notifier *nn)
{
	int err;

	rtnl_lock();
	err = __register_netdevice_notifier_net(dev_net(dev), nb, false);
	if (!err) {
		nn->nb = nb;
		list_add(&nn->list, &dev->net_notifier_list);
	}
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(register_netdevice_notifier_dev_net);

int unregister_netdevice_notifier_dev_net(struct net_device *dev,
					  struct notifier_block *nb,
					  struct netdev_net_notifier *nn)
{
	int err;

	rtnl_lock();
	list_del(&nn->list);
	err = __unregister_netdevice_notifier_net(dev_net(dev), nb);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(unregister_netdevice_notifier_dev_net);

static void move_netdevice_notifiers_dev_net(struct net_device *dev,
					     struct net *net)
{
	struct netdev_net_notifier *nn;

	list_for_each_entry(nn, &dev->net_notifier_list, list) {
		__unregister_netdevice_notifier_net(dev_net(dev), nn->nb);
		__register_netdevice_notifier_net(net, nn->nb, true);
	}
}

/**
 *	call_netdevice_notifiers_info - call all network notifier blocks
 *	@val: value passed unmodified to notifier function
 *	@info: notifier information data
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 */

static int call_netdevice_notifiers_info(unsigned long val,
					 struct netdev_notifier_info *info)
{
	struct net *net = dev_net(info->dev);
	int ret;

	ASSERT_RTNL();

	/* Run per-netns notifier block chain first, then run the global one.
	 * Hopefully, one day, the global one is going to be removed after
	 * all notifier block registrators get converted to be per-netns.
	 */
	ret = raw_notifier_call_chain(&net->netdev_chain, val, info);
	if (ret & NOTIFY_STOP_MASK)
		return ret;
	return raw_notifier_call_chain(&netdev_chain, val, info);
}

static int call_netdevice_notifiers_extack(unsigned long val,
					   struct net_device *dev,
					   struct netlink_ext_ack *extack)
{
	struct netdev_notifier_info info = {
		.dev = dev,
		.extack = extack,
	};

	return call_netdevice_notifiers_info(val, &info);
}

/**
 *	call_netdevice_notifiers - call all network notifier blocks
 *      @val: value passed unmodified to notifier function
 *      @dev: net_device pointer passed unmodified to notifier function
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 */

int call_netdevice_notifiers(unsigned long val, struct net_device *dev)
{
	return call_netdevice_notifiers_extack(val, dev, NULL);
}
EXPORT_SYMBOL(call_netdevice_notifiers);

/**
 *	call_netdevice_notifiers_mtu - call all network notifier blocks
 *	@val: value passed unmodified to notifier function
 *	@dev: net_device pointer passed unmodified to notifier function
 *	@arg: additional u32 argument passed to the notifier function
 *
 *	Call all network notifier blocks.  Parameters and return value
 *	are as for raw_notifier_call_chain().
 */
static int call_netdevice_notifiers_mtu(unsigned long val,
					struct net_device *dev, u32 arg)
{
	struct netdev_notifier_info_ext info = {
		.info.dev = dev,
		.ext.mtu = arg,
	};

	BUILD_BUG_ON(offsetof(struct netdev_notifier_info_ext, info) != 0);

	return call_netdevice_notifiers_info(val, &info.info);
}

#ifdef CONFIG_NET_INGRESS
static DEFINE_STATIC_KEY_FALSE(ingress_needed_key);

void net_inc_ingress_queue(void)
{
	static_branch_inc(&ingress_needed_key);
}
EXPORT_SYMBOL_GPL(net_inc_ingress_queue);

void net_dec_ingress_queue(void)
{
	static_branch_dec(&ingress_needed_key);
}
EXPORT_SYMBOL_GPL(net_dec_ingress_queue);
#endif

#ifdef CONFIG_NET_EGRESS
static DEFINE_STATIC_KEY_FALSE(egress_needed_key);

void net_inc_egress_queue(void)
{
	static_branch_inc(&egress_needed_key);
}
EXPORT_SYMBOL_GPL(net_inc_egress_queue);

void net_dec_egress_queue(void)
{
	static_branch_dec(&egress_needed_key);
}
EXPORT_SYMBOL_GPL(net_dec_egress_queue);
#endif

static DEFINE_STATIC_KEY_FALSE(netstamp_needed_key);
#ifdef CONFIG_JUMP_LABEL
static atomic_t netstamp_needed_deferred;
static atomic_t netstamp_wanted;
static void netstamp_clear(struct work_struct *work)
{
	int deferred = atomic_xchg(&netstamp_needed_deferred, 0);
	int wanted;

	wanted = atomic_add_return(deferred, &netstamp_wanted);
	if (wanted > 0)
		static_branch_enable(&netstamp_needed_key);
	else
		static_branch_disable(&netstamp_needed_key);
}
static DECLARE_WORK(netstamp_work, netstamp_clear);
#endif

void net_enable_timestamp(void)
{
#ifdef CONFIG_JUMP_LABEL
	int wanted;

	while (1) {
		wanted = atomic_read(&netstamp_wanted);
		if (wanted <= 0)
			break;
		if (atomic_cmpxchg(&netstamp_wanted, wanted, wanted + 1) == wanted)
			return;
	}
	atomic_inc(&netstamp_needed_deferred);
	schedule_work(&netstamp_work);
#else
	static_branch_inc(&netstamp_needed_key);
#endif
}
EXPORT_SYMBOL(net_enable_timestamp);

void net_disable_timestamp(void)
{
#ifdef CONFIG_JUMP_LABEL
	int wanted;

	while (1) {
		wanted = atomic_read(&netstamp_wanted);
		if (wanted <= 1)
			break;
		if (atomic_cmpxchg(&netstamp_wanted, wanted, wanted - 1) == wanted)
			return;
	}
	atomic_dec(&netstamp_needed_deferred);
	schedule_work(&netstamp_work);
#else
	static_branch_dec(&netstamp_needed_key);
#endif
}
EXPORT_SYMBOL(net_disable_timestamp);

static inline void net_timestamp_set(struct sk_buff *skb)
{
	skb->tstamp = 0;
	if (static_branch_unlikely(&netstamp_needed_key))
		__net_timestamp(skb);
}

#define net_timestamp_check(COND, SKB)				\
	if (static_branch_unlikely(&netstamp_needed_key)) {	\
		if ((COND) && !(SKB)->tstamp)			\
			__net_timestamp(SKB);			\
	}							\

bool is_skb_forwardable(const struct net_device *dev, const struct sk_buff *skb)
{
	return __is_skb_forwardable(dev, skb, true);
}
EXPORT_SYMBOL_GPL(is_skb_forwardable);

static int __dev_forward_skb2(struct net_device *dev, struct sk_buff *skb,
			      bool check_mtu)
{
	int ret = ____dev_forward_skb(dev, skb, check_mtu);

	if (likely(!ret)) {
		skb->protocol = eth_type_trans(skb, dev);
		skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);
	}

	return ret;
}

int __dev_forward_skb(struct net_device *dev, struct sk_buff *skb)
{
	return __dev_forward_skb2(dev, skb, true);
}
EXPORT_SYMBOL_GPL(__dev_forward_skb);

/**
 * dev_forward_skb - loopback an skb to another netif
 *
 * @dev: destination network device
 * @skb: buffer to forward
 *
 * return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped, but freed)
 *
 * dev_forward_skb can be used for injecting an skb from the
 * start_xmit function of one device into the receive queue
 * of another device.
 *
 * The receiving device may be in another namespace, so
 * we have to clear all information in the skb that could
 * impact namespace isolation.
 */
int dev_forward_skb(struct net_device *dev, struct sk_buff *skb)
{
	return __dev_forward_skb(dev, skb) ?: netif_rx_internal(skb);
}
EXPORT_SYMBOL_GPL(dev_forward_skb);

int dev_forward_skb_nomtu(struct net_device *dev, struct sk_buff *skb)
{
	return __dev_forward_skb2(dev, skb, false) ?: netif_rx_internal(skb);
}

static inline int deliver_skb(struct sk_buff *skb,
			      struct packet_type *pt_prev,
			      struct net_device *orig_dev)
{
	if (unlikely(skb_orphan_frags_rx(skb, GFP_ATOMIC)))
		return -ENOMEM;
	refcount_inc(&skb->users);
	return pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
}

static inline void deliver_ptype_list_skb(struct sk_buff *skb,
					  struct packet_type **pt,
					  struct net_device *orig_dev,
					  __be16 type,
					  struct list_head *ptype_list)
{
	struct packet_type *ptype, *pt_prev = *pt;

	list_for_each_entry_rcu(ptype, ptype_list, list) {
		if (ptype->type != type)
			continue;
		if (pt_prev)
			deliver_skb(skb, pt_prev, orig_dev);
		pt_prev = ptype;
	}
	*pt = pt_prev;
}

static inline bool skb_loop_sk(struct packet_type *ptype, struct sk_buff *skb)
{
	if (!ptype->af_packet_priv || !skb->sk)
		return false;

	if (ptype->id_match)
		return ptype->id_match(ptype, skb->sk);
	else if ((struct sock *)ptype->af_packet_priv == skb->sk)
		return true;

	return false;
}

/**
 * dev_nit_active - return true if any network interface taps are in use
 *
 * @dev: network device to check for the presence of taps
 */
bool dev_nit_active(struct net_device *dev)
{
	return !list_empty(&ptype_all) || !list_empty(&dev->ptype_all);
}
EXPORT_SYMBOL_GPL(dev_nit_active);

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */

void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev)
{
	struct packet_type *ptype;
	struct sk_buff *skb2 = NULL;
	struct packet_type *pt_prev = NULL;
	struct list_head *ptype_list = &ptype_all;

	rcu_read_lock();
again:
	list_for_each_entry_rcu(ptype, ptype_list, list) {
		if (ptype->ignore_outgoing)
			continue;

		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		if (skb_loop_sk(ptype, skb))
			continue;

		if (pt_prev) {
			deliver_skb(skb2, pt_prev, skb->dev);
			pt_prev = ptype;
			continue;
		}

		/* need to clone skb, done only once */
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (!skb2)
			goto out_unlock;

		net_timestamp_set(skb2);

		/* skb->nh should be correctly
		 * set by sender, so that the second statement is
		 * just protection against buggy protocols.
		 */
		skb_reset_mac_header(skb2);

		if (skb_network_header(skb2) < skb2->data ||
		    skb_network_header(skb2) > skb_tail_pointer(skb2)) {
			net_crit_ratelimited("protocol %04x is buggy, dev %s\n",
					     ntohs(skb2->protocol),
					     dev->name);
			skb_reset_network_header(skb2);
		}

		skb2->transport_header = skb2->network_header;
		skb2->pkt_type = PACKET_OUTGOING;
		pt_prev = ptype;
	}

	if (ptype_list == &ptype_all) {
		ptype_list = &dev->ptype_all;
		goto again;
	}
out_unlock:
	if (pt_prev) {
		if (!skb_orphan_frags_rx(skb2, GFP_ATOMIC))
			pt_prev->func(skb2, skb->dev, pt_prev, skb->dev);
		else
			kfree_skb(skb2);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(dev_queue_xmit_nit);

/**
 * netif_setup_tc - Handle tc mappings on real_num_tx_queues change
 * @dev: Network device
 * @txq: number of queues available
 *
 * If real_num_tx_queues is changed the tc mappings may no longer be
 * valid. To resolve this verify the tc mapping remains valid and if
 * not NULL the mapping. With no priorities mapping to this
 * offset/count pair it will no longer be used. In the worst case TC0
 * is invalid nothing can be done so disable priority mappings. If is
 * expected that drivers will fix this mapping if they can before
 * calling netif_set_real_num_tx_queues.
 */
static void netif_setup_tc(struct net_device *dev, unsigned int txq)
{
	int i;
	struct netdev_tc_txq *tc = &dev->tc_to_txq[0];

	/* If TC0 is invalidated disable TC mapping */
	if (tc->offset + tc->count > txq) {
		netdev_warn(dev, "Number of in use tx queues changed invalidating tc mappings. Priority traffic classification disabled!\n");
		dev->num_tc = 0;
		return;
	}

	/* Invalidated prio to tc mappings set to TC0 */
	for (i = 1; i < TC_BITMASK + 1; i++) {
		int q = netdev_get_prio_tc_map(dev, i);

		tc = &dev->tc_to_txq[q];
		if (tc->offset + tc->count > txq) {
			netdev_warn(dev, "Number of in use tx queues changed. Priority %i to tc mapping %i is no longer valid. Setting map to 0\n",
				    i, q);
			netdev_set_prio_tc_map(dev, i, 0);
		}
	}
}

int netdev_txq_to_tc(struct net_device *dev, unsigned int txq)
{
	if (dev->num_tc) {
		struct netdev_tc_txq *tc = &dev->tc_to_txq[0];
		int i;

		/* walk through the TCs and see if it falls into any of them */
		for (i = 0; i < TC_MAX_QUEUE; i++, tc++) {
			if ((txq - tc->offset) < tc->count)
				return i;
		}

		/* didn't find it, just return -1 to indicate no match */
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL(netdev_txq_to_tc);

#ifdef CONFIG_XPS
static struct static_key xps_needed __read_mostly;
static struct static_key xps_rxqs_needed __read_mostly;
static DEFINE_MUTEX(xps_map_mutex);
#define xmap_dereference(P)		\
	rcu_dereference_protected((P), lockdep_is_held(&xps_map_mutex))

static bool remove_xps_queue(struct xps_dev_maps *dev_maps,
			     struct xps_dev_maps *old_maps, int tci, u16 index)
{
	struct xps_map *map = NULL;
	int pos;

	if (dev_maps)
		map = xmap_dereference(dev_maps->attr_map[tci]);
	if (!map)
		return false;

	for (pos = map->len; pos--;) {
		if (map->queues[pos] != index)
			continue;

		if (map->len > 1) {
			map->queues[pos] = map->queues[--map->len];
			break;
		}

		if (old_maps)
			RCU_INIT_POINTER(old_maps->attr_map[tci], NULL);
		RCU_INIT_POINTER(dev_maps->attr_map[tci], NULL);
		kfree_rcu(map, rcu);
		return false;
	}

	return true;
}

static bool remove_xps_queue_cpu(struct net_device *dev,
				 struct xps_dev_maps *dev_maps,
				 int cpu, u16 offset, u16 count)
{
	int num_tc = dev_maps->num_tc;
	bool active = false;
	int tci;

	for (tci = cpu * num_tc; num_tc--; tci++) {
		int i, j;

		for (i = count, j = offset; i--; j++) {
			if (!remove_xps_queue(dev_maps, NULL, tci, j))
				break;
		}

		active |= i < 0;
	}

	return active;
}

static void reset_xps_maps(struct net_device *dev,
			   struct xps_dev_maps *dev_maps,
			   enum xps_map_type type)
{
	static_key_slow_dec_cpuslocked(&xps_needed);
	if (type == XPS_RXQS)
		static_key_slow_dec_cpuslocked(&xps_rxqs_needed);

	RCU_INIT_POINTER(dev->xps_maps[type], NULL);

	kfree_rcu(dev_maps, rcu);
}

static void clean_xps_maps(struct net_device *dev, enum xps_map_type type,
			   u16 offset, u16 count)
{
	struct xps_dev_maps *dev_maps;
	bool active = false;
	int i, j;

	dev_maps = xmap_dereference(dev->xps_maps[type]);
	if (!dev_maps)
		return;

	for (j = 0; j < dev_maps->nr_ids; j++)
		active |= remove_xps_queue_cpu(dev, dev_maps, j, offset, count);
	if (!active)
		reset_xps_maps(dev, dev_maps, type);

	if (type == XPS_CPUS) {
		for (i = offset + (count - 1); count--; i--)
			netdev_queue_numa_node_write(
				netdev_get_tx_queue(dev, i), NUMA_NO_NODE);
	}
}

static void netif_reset_xps_queues(struct net_device *dev, u16 offset,
				   u16 count)
{
	if (!static_key_false(&xps_needed))
		return;

	cpus_read_lock();
	mutex_lock(&xps_map_mutex);

	if (static_key_false(&xps_rxqs_needed))
		clean_xps_maps(dev, XPS_RXQS, offset, count);

	clean_xps_maps(dev, XPS_CPUS, offset, count);

	mutex_unlock(&xps_map_mutex);
	cpus_read_unlock();
}

static void netif_reset_xps_queues_gt(struct net_device *dev, u16 index)
{
	netif_reset_xps_queues(dev, index, dev->num_tx_queues - index);
}

static struct xps_map *expand_xps_map(struct xps_map *map, int attr_index,
				      u16 index, bool is_rxqs_map)
{
	struct xps_map *new_map;
	int alloc_len = XPS_MIN_MAP_ALLOC;
	int i, pos;

	for (pos = 0; map && pos < map->len; pos++) {
		if (map->queues[pos] != index)
			continue;
		return map;
	}

	/* Need to add tx-queue to this CPU's/rx-queue's existing map */
	if (map) {
		if (pos < map->alloc_len)
			return map;

		alloc_len = map->alloc_len * 2;
	}

	/* Need to allocate new map to store tx-queue on this CPU's/rx-queue's
	 *  map
	 */
	if (is_rxqs_map)
		new_map = kzalloc(XPS_MAP_SIZE(alloc_len), GFP_KERNEL);
	else
		new_map = kzalloc_node(XPS_MAP_SIZE(alloc_len), GFP_KERNEL,
				       cpu_to_node(attr_index));
	if (!new_map)
		return NULL;

	for (i = 0; i < pos; i++)
		new_map->queues[i] = map->queues[i];
	new_map->alloc_len = alloc_len;
	new_map->len = pos;

	return new_map;
}

/* Copy xps maps at a given index */
static void xps_copy_dev_maps(struct xps_dev_maps *dev_maps,
			      struct xps_dev_maps *new_dev_maps, int index,
			      int tc, bool skip_tc)
{
	int i, tci = index * dev_maps->num_tc;
	struct xps_map *map;

	/* copy maps belonging to foreign traffic classes */
	for (i = 0; i < dev_maps->num_tc; i++, tci++) {
		if (i == tc && skip_tc)
			continue;

		/* fill in the new device map from the old device map */
		map = xmap_dereference(dev_maps->attr_map[tci]);
		RCU_INIT_POINTER(new_dev_maps->attr_map[tci], map);
	}
}

/* Must be called under cpus_read_lock */
int __netif_set_xps_queue(struct net_device *dev, const unsigned long *mask,
			  u16 index, enum xps_map_type type)
{
	struct xps_dev_maps *dev_maps, *new_dev_maps = NULL, *old_dev_maps = NULL;
	const unsigned long *online_mask = NULL;
	bool active = false, copy = false;
	int i, j, tci, numa_node_id = -2;
	int maps_sz, num_tc = 1, tc = 0;
	struct xps_map *map, *new_map;
	unsigned int nr_ids;

	if (dev->num_tc) {
		/* Do not allow XPS on subordinate device directly */
		num_tc = dev->num_tc;
		if (num_tc < 0)
			return -EINVAL;

		/* If queue belongs to subordinate dev use its map */
		dev = netdev_get_tx_queue(dev, index)->sb_dev ? : dev;

		tc = netdev_txq_to_tc(dev, index);
		if (tc < 0)
			return -EINVAL;
	}

	mutex_lock(&xps_map_mutex);

	dev_maps = xmap_dereference(dev->xps_maps[type]);
	if (type == XPS_RXQS) {
		maps_sz = XPS_RXQ_DEV_MAPS_SIZE(num_tc, dev->num_rx_queues);
		nr_ids = dev->num_rx_queues;
	} else {
		maps_sz = XPS_CPU_DEV_MAPS_SIZE(num_tc);
		if (num_possible_cpus() > 1)
			online_mask = cpumask_bits(cpu_online_mask);
		nr_ids = nr_cpu_ids;
	}

	if (maps_sz < L1_CACHE_BYTES)
		maps_sz = L1_CACHE_BYTES;

	/* The old dev_maps could be larger or smaller than the one we're
	 * setting up now, as dev->num_tc or nr_ids could have been updated in
	 * between. We could try to be smart, but let's be safe instead and only
	 * copy foreign traffic classes if the two map sizes match.
	 */
	if (dev_maps &&
	    dev_maps->num_tc == num_tc && dev_maps->nr_ids == nr_ids)
		copy = true;

	/* allocate memory for queue storage */
	for (j = -1; j = netif_attrmask_next_and(j, online_mask, mask, nr_ids),
	     j < nr_ids;) {
		if (!new_dev_maps) {
			new_dev_maps = kzalloc(maps_sz, GFP_KERNEL);
			if (!new_dev_maps) {
				mutex_unlock(&xps_map_mutex);
				return -ENOMEM;
			}

			new_dev_maps->nr_ids = nr_ids;
			new_dev_maps->num_tc = num_tc;
		}

		tci = j * num_tc + tc;
		map = copy ? xmap_dereference(dev_maps->attr_map[tci]) : NULL;

		map = expand_xps_map(map, j, index, type == XPS_RXQS);
		if (!map)
			goto error;

		RCU_INIT_POINTER(new_dev_maps->attr_map[tci], map);
	}

	if (!new_dev_maps)
		goto out_no_new_maps;

	if (!dev_maps) {
		/* Increment static keys at most once per type */
		static_key_slow_inc_cpuslocked(&xps_needed);
		if (type == XPS_RXQS)
			static_key_slow_inc_cpuslocked(&xps_rxqs_needed);
	}

	for (j = 0; j < nr_ids; j++) {
		bool skip_tc = false;

		tci = j * num_tc + tc;
		if (netif_attr_test_mask(j, mask, nr_ids) &&
		    netif_attr_test_online(j, online_mask, nr_ids)) {
			/* add tx-queue to CPU/rx-queue maps */
			int pos = 0;

			skip_tc = true;

			map = xmap_dereference(new_dev_maps->attr_map[tci]);
			while ((pos < map->len) && (map->queues[pos] != index))
				pos++;

			if (pos == map->len)
				map->queues[map->len++] = index;
#ifdef CONFIG_NUMA
			if (type == XPS_CPUS) {
				if (numa_node_id == -2)
					numa_node_id = cpu_to_node(j);
				else if (numa_node_id != cpu_to_node(j))
					numa_node_id = -1;
			}
#endif
		}

		if (copy)
			xps_copy_dev_maps(dev_maps, new_dev_maps, j, tc,
					  skip_tc);
	}

	rcu_assign_pointer(dev->xps_maps[type], new_dev_maps);

	/* Cleanup old maps */
	if (!dev_maps)
		goto out_no_old_maps;

	for (j = 0; j < dev_maps->nr_ids; j++) {
		for (i = num_tc, tci = j * dev_maps->num_tc; i--; tci++) {
			map = xmap_dereference(dev_maps->attr_map[tci]);
			if (!map)
				continue;

			if (copy) {
				new_map = xmap_dereference(new_dev_maps->attr_map[tci]);
				if (map == new_map)
					continue;
			}

			RCU_INIT_POINTER(dev_maps->attr_map[tci], NULL);
			kfree_rcu(map, rcu);
		}
	}

	old_dev_maps = dev_maps;

out_no_old_maps:
	dev_maps = new_dev_maps;
	active = true;

out_no_new_maps:
	if (type == XPS_CPUS)
		/* update Tx queue numa node */
		netdev_queue_numa_node_write(netdev_get_tx_queue(dev, index),
					     (numa_node_id >= 0) ?
					     numa_node_id : NUMA_NO_NODE);

	if (!dev_maps)
		goto out_no_maps;

	/* removes tx-queue from unused CPUs/rx-queues */
	for (j = 0; j < dev_maps->nr_ids; j++) {
		tci = j * dev_maps->num_tc;

		for (i = 0; i < dev_maps->num_tc; i++, tci++) {
			if (i == tc &&
			    netif_attr_test_mask(j, mask, dev_maps->nr_ids) &&
			    netif_attr_test_online(j, online_mask, dev_maps->nr_ids))
				continue;

			active |= remove_xps_queue(dev_maps,
						   copy ? old_dev_maps : NULL,
						   tci, index);
		}
	}

	if (old_dev_maps)
		kfree_rcu(old_dev_maps, rcu);

	/* free map if not active */
	if (!active)
		reset_xps_maps(dev, dev_maps, type);

out_no_maps:
	mutex_unlock(&xps_map_mutex);

	return 0;
error:
	/* remove any maps that we added */
	for (j = 0; j < nr_ids; j++) {
		for (i = num_tc, tci = j * num_tc; i--; tci++) {
			new_map = xmap_dereference(new_dev_maps->attr_map[tci]);
			map = copy ?
			      xmap_dereference(dev_maps->attr_map[tci]) :
			      NULL;
			if (new_map && new_map != map)
				kfree(new_map);
		}
	}

	mutex_unlock(&xps_map_mutex);

	kfree(new_dev_maps);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(__netif_set_xps_queue);

int netif_set_xps_queue(struct net_device *dev, const struct cpumask *mask,
			u16 index)
{
	int ret;

	cpus_read_lock();
	ret =  __netif_set_xps_queue(dev, cpumask_bits(mask), index, XPS_CPUS);
	cpus_read_unlock();

	return ret;
}
EXPORT_SYMBOL(netif_set_xps_queue);

#endif
static void netdev_unbind_all_sb_channels(struct net_device *dev)
{
	struct netdev_queue *txq = &dev->_tx[dev->num_tx_queues];

	/* Unbind any subordinate channels */
	while (txq-- != &dev->_tx[0]) {
		if (txq->sb_dev)
			netdev_unbind_sb_channel(dev, txq->sb_dev);
	}
}

void netdev_reset_tc(struct net_device *dev)
{
#ifdef CONFIG_XPS
	netif_reset_xps_queues_gt(dev, 0);
#endif
	netdev_unbind_all_sb_channels(dev);

	/* Reset TC configuration of device */
	dev->num_tc = 0;
	memset(dev->tc_to_txq, 0, sizeof(dev->tc_to_txq));
	memset(dev->prio_tc_map, 0, sizeof(dev->prio_tc_map));
}
EXPORT_SYMBOL(netdev_reset_tc);

int netdev_set_tc_queue(struct net_device *dev, u8 tc, u16 count, u16 offset)
{
	if (tc >= dev->num_tc)
		return -EINVAL;

#ifdef CONFIG_XPS
	netif_reset_xps_queues(dev, offset, count);
#endif
	dev->tc_to_txq[tc].count = count;
	dev->tc_to_txq[tc].offset = offset;
	return 0;
}
EXPORT_SYMBOL(netdev_set_tc_queue);

int netdev_set_num_tc(struct net_device *dev, u8 num_tc)
{
	if (num_tc > TC_MAX_QUEUE)
		return -EINVAL;

#ifdef CONFIG_XPS
	netif_reset_xps_queues_gt(dev, 0);
#endif
	netdev_unbind_all_sb_channels(dev);

	dev->num_tc = num_tc;
	return 0;
}
EXPORT_SYMBOL(netdev_set_num_tc);

void netdev_unbind_sb_channel(struct net_device *dev,
			      struct net_device *sb_dev)
{
	struct netdev_queue *txq = &dev->_tx[dev->num_tx_queues];

#ifdef CONFIG_XPS
	netif_reset_xps_queues_gt(sb_dev, 0);
#endif
	memset(sb_dev->tc_to_txq, 0, sizeof(sb_dev->tc_to_txq));
	memset(sb_dev->prio_tc_map, 0, sizeof(sb_dev->prio_tc_map));

	while (txq-- != &dev->_tx[0]) {
		if (txq->sb_dev == sb_dev)
			txq->sb_dev = NULL;
	}
}
EXPORT_SYMBOL(netdev_unbind_sb_channel);

int netdev_bind_sb_channel_queue(struct net_device *dev,
				 struct net_device *sb_dev,
				 u8 tc, u16 count, u16 offset)
{
	/* Make certain the sb_dev and dev are already configured */
	if (sb_dev->num_tc >= 0 || tc >= dev->num_tc)
		return -EINVAL;

	/* We cannot hand out queues we don't have */
	if ((offset + count) > dev->real_num_tx_queues)
		return -EINVAL;

	/* Record the mapping */
	sb_dev->tc_to_txq[tc].count = count;
	sb_dev->tc_to_txq[tc].offset = offset;

	/* Provide a way for Tx queue to find the tc_to_txq map or
	 * XPS map for itself.
	 */
	while (count--)
		netdev_get_tx_queue(dev, count + offset)->sb_dev = sb_dev;

	return 0;
}
EXPORT_SYMBOL(netdev_bind_sb_channel_queue);

int netdev_set_sb_channel(struct net_device *dev, u16 channel)
{
	/* Do not use a multiqueue device to represent a subordinate channel */
	if (netif_is_multiqueue(dev))
		return -ENODEV;

	/* We allow channels 1 - 32767 to be used for subordinate channels.
	 * Channel 0 is meant to be "native" mode and used only to represent
	 * the main root device. We allow writing 0 to reset the device back
	 * to normal mode after being used as a subordinate channel.
	 */
	if (channel > S16_MAX)
		return -EINVAL;

	dev->num_tc = -channel;

	return 0;
}
EXPORT_SYMBOL(netdev_set_sb_channel);

/*
 * Routine to help set real_num_tx_queues. To avoid skbs mapped to queues
 * greater than real_num_tx_queues stale skbs on the qdisc must be flushed.
 */
int netif_set_real_num_tx_queues(struct net_device *dev, unsigned int txq)
{
	bool disabling;
	int rc;

	disabling = txq < dev->real_num_tx_queues;

	if (txq < 1 || txq > dev->num_tx_queues)
		return -EINVAL;

	if (dev->reg_state == NETREG_REGISTERED ||
	    dev->reg_state == NETREG_UNREGISTERING) {
		ASSERT_RTNL();

		rc = netdev_queue_update_kobjects(dev, dev->real_num_tx_queues,
						  txq);
		if (rc)
			return rc;

		if (dev->num_tc)
			netif_setup_tc(dev, txq);

		dev_qdisc_change_real_num_tx(dev, txq);

		dev->real_num_tx_queues = txq;

		if (disabling) {
			synchronize_net();
			qdisc_reset_all_tx_gt(dev, txq);
#ifdef CONFIG_XPS
			netif_reset_xps_queues_gt(dev, txq);
#endif
		}
	} else {
		dev->real_num_tx_queues = txq;
	}

	return 0;
}
EXPORT_SYMBOL(netif_set_real_num_tx_queues);

#ifdef CONFIG_SYSFS
/**
 *	netif_set_real_num_rx_queues - set actual number of RX queues used
 *	@dev: Network device
 *	@rxq: Actual number of RX queues
 *
 *	This must be called either with the rtnl_lock held or before
 *	registration of the net device.  Returns 0 on success, or a
 *	negative error code.  If called before registration, it always
 *	succeeds.
 */
int netif_set_real_num_rx_queues(struct net_device *dev, unsigned int rxq)
{
	int rc;

	if (rxq < 1 || rxq > dev->num_rx_queues)
		return -EINVAL;

	if (dev->reg_state == NETREG_REGISTERED) {
		ASSERT_RTNL();

		rc = net_rx_queue_update_kobjects(dev, dev->real_num_rx_queues,
						  rxq);
		if (rc)
			return rc;
	}

	dev->real_num_rx_queues = rxq;
	return 0;
}
EXPORT_SYMBOL(netif_set_real_num_rx_queues);
#endif

/**
 *	netif_set_real_num_queues - set actual number of RX and TX queues used
 *	@dev: Network device
 *	@txq: Actual number of TX queues
 *	@rxq: Actual number of RX queues
 *
 *	Set the real number of both TX and RX queues.
 *	Does nothing if the number of queues is already correct.
 */
int netif_set_real_num_queues(struct net_device *dev,
			      unsigned int txq, unsigned int rxq)
{
	unsigned int old_rxq = dev->real_num_rx_queues;
	int err;

	if (txq < 1 || txq > dev->num_tx_queues ||
	    rxq < 1 || rxq > dev->num_rx_queues)
		return -EINVAL;

	/* Start from increases, so the error path only does decreases -
	 * decreases can't fail.
	 */
	if (rxq > dev->real_num_rx_queues) {
		err = netif_set_real_num_rx_queues(dev, rxq);
		if (err)
			return err;
	}
	if (txq > dev->real_num_tx_queues) {
		err = netif_set_real_num_tx_queues(dev, txq);
		if (err)
			goto undo_rx;
	}
	if (rxq < dev->real_num_rx_queues)
		WARN_ON(netif_set_real_num_rx_queues(dev, rxq));
	if (txq < dev->real_num_tx_queues)
		WARN_ON(netif_set_real_num_tx_queues(dev, txq));

	return 0;
undo_rx:
	WARN_ON(netif_set_real_num_rx_queues(dev, old_rxq));
	return err;
}
EXPORT_SYMBOL(netif_set_real_num_queues);

/**
 * netif_get_num_default_rss_queues - default number of RSS queues
 *
 * This routine should set an upper limit on the number of RSS queues
 * used by default by multiqueue devices.
 */
int netif_get_num_default_rss_queues(void)
{
	return is_kdump_kernel() ?
		1 : min_t(int, DEFAULT_MAX_NUM_RSS_QUEUES, num_online_cpus());
}
EXPORT_SYMBOL(netif_get_num_default_rss_queues);

static void __netif_reschedule(struct Qdisc *q)
{
	struct softnet_data *sd;
	unsigned long flags;

	local_irq_save(flags);
	sd = this_cpu_ptr(&softnet_data);
	q->next_sched = NULL;
	*sd->output_queue_tailp = q;
	sd->output_queue_tailp = &q->next_sched;
	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_restore(flags);
}

void __netif_schedule(struct Qdisc *q)
{
	if (!test_and_set_bit(__QDISC_STATE_SCHED, &q->state))
		__netif_reschedule(q);
}
EXPORT_SYMBOL(__netif_schedule);

struct dev_kfree_skb_cb {
	enum skb_free_reason reason;
};

static struct dev_kfree_skb_cb *get_kfree_skb_cb(const struct sk_buff *skb)
{
	return (struct dev_kfree_skb_cb *)skb->cb;
}

void netif_schedule_queue(struct netdev_queue *txq)
{
	rcu_read_lock();
	if (!netif_xmit_stopped(txq)) {
		struct Qdisc *q = rcu_dereference(txq->qdisc);

		__netif_schedule(q);
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(netif_schedule_queue);

void netif_tx_wake_queue(struct netdev_queue *dev_queue)
{
	if (test_and_clear_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state)) {
		struct Qdisc *q;

		rcu_read_lock();
		q = rcu_dereference(dev_queue->qdisc);
		__netif_schedule(q);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL(netif_tx_wake_queue);

void __dev_kfree_skb_irq(struct sk_buff *skb, enum skb_free_reason reason)
{
	unsigned long flags;

	if (unlikely(!skb))
		return;

	if (likely(refcount_read(&skb->users) == 1)) {
		smp_rmb();
		refcount_set(&skb->users, 0);
	} else if (likely(!refcount_dec_and_test(&skb->users))) {
		return;
	}
	get_kfree_skb_cb(skb)->reason = reason;
	local_irq_save(flags);
	skb->next = __this_cpu_read(softnet_data.completion_queue);
	__this_cpu_write(softnet_data.completion_queue, skb);
	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(__dev_kfree_skb_irq);

void __dev_kfree_skb_any(struct sk_buff *skb, enum skb_free_reason reason)
{
	if (in_hardirq() || irqs_disabled())
		__dev_kfree_skb_irq(skb, reason);
	else
		dev_kfree_skb(skb);
}
EXPORT_SYMBOL(__dev_kfree_skb_any);


/**
 * netif_device_detach - mark device as removed
 * @dev: network device
 *
 * Mark device as removed from system and therefore no longer available.
 */
void netif_device_detach(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
		netif_tx_stop_all_queues(dev);
	}
}
EXPORT_SYMBOL(netif_device_detach);

/**
 * netif_device_attach - mark device as attached
 * @dev: network device
 *
 * Mark device as attached from system and restart if needed.
 */
void netif_device_attach(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_PRESENT, &dev->state) &&
	    netif_running(dev)) {
		netif_tx_wake_all_queues(dev);
		__netdev_watchdog_up(dev);
	}
}
EXPORT_SYMBOL(netif_device_attach);

/*
 * Returns a Tx hash based on the given packet descriptor a Tx queues' number
 * to be used as a distribution range.
 */
static u16 skb_tx_hash(const struct net_device *dev,
		       const struct net_device *sb_dev,
		       struct sk_buff *skb)
{
	u32 hash;
	u16 qoffset = 0;
	u16 qcount = dev->real_num_tx_queues;

	if (dev->num_tc) {
		u8 tc = netdev_get_prio_tc_map(dev, skb->priority);

		qoffset = sb_dev->tc_to_txq[tc].offset;
		qcount = sb_dev->tc_to_txq[tc].count;
		if (unlikely(!qcount)) {
			net_warn_ratelimited("%s: invalid qcount, qoffset %u for tc %u\n",
					     sb_dev->name, qoffset, tc);
			qoffset = 0;
			qcount = dev->real_num_tx_queues;
		}
	}

	if (skb_rx_queue_recorded(skb)) {
		hash = skb_get_rx_queue(skb);
		if (hash >= qoffset)
			hash -= qoffset;
		while (unlikely(hash >= qcount))
			hash -= qcount;
		return hash + qoffset;
	}

	return (u16) reciprocal_scale(skb_get_hash(skb), qcount) + qoffset;
}

static void skb_warn_bad_offload(const struct sk_buff *skb)
{
	static const netdev_features_t null_features;
	struct net_device *dev = skb->dev;
	const char *name = "";

	if (!net_ratelimit())
		return;

	if (dev) {
		if (dev->dev.parent)
			name = dev_driver_string(dev->dev.parent);
		else
			name = netdev_name(dev);
	}
	skb_dump(KERN_WARNING, skb, false);
	WARN(1, "%s: caps=(%pNF, %pNF)\n",
	     name, dev ? &dev->features : &null_features,
	     skb->sk ? &skb->sk->sk_route_caps : &null_features);
}

/*
 * Invalidate hardware checksum when packet is to be mangled, and
 * complete checksum manually on outgoing path.
 */
int skb_checksum_help(struct sk_buff *skb)
{
	__wsum csum;
	int ret = 0, offset;

	if (skb->ip_summed == CHECKSUM_COMPLETE)
		goto out_set_summed;

	if (unlikely(skb_is_gso(skb))) {
		skb_warn_bad_offload(skb);
		return -EINVAL;
	}

	/* Before computing a checksum, we should make sure no frag could
	 * be modified by an external entity : checksum could be wrong.
	 */
	if (skb_has_shared_frag(skb)) {
		ret = __skb_linearize(skb);
		if (ret)
			goto out;
	}

	offset = skb_checksum_start_offset(skb);
	BUG_ON(offset >= skb_headlen(skb));
	csum = skb_checksum(skb, offset, skb->len - offset, 0);

	offset += skb->csum_offset;
	BUG_ON(offset + sizeof(__sum16) > skb_headlen(skb));

	ret = skb_ensure_writable(skb, offset + sizeof(__sum16));
	if (ret)
		goto out;

	*(__sum16 *)(skb->data + offset) = csum_fold(csum) ?: CSUM_MANGLED_0;
out_set_summed:
	skb->ip_summed = CHECKSUM_NONE;
out:
	return ret;
}
EXPORT_SYMBOL(skb_checksum_help);

int skb_crc32c_csum_help(struct sk_buff *skb)
{
	__le32 crc32c_csum;
	int ret = 0, offset, start;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		goto out;

	if (unlikely(skb_is_gso(skb)))
		goto out;

	/* Before computing a checksum, we should make sure no frag could
	 * be modified by an external entity : checksum could be wrong.
	 */
	if (unlikely(skb_has_shared_frag(skb))) {
		ret = __skb_linearize(skb);
		if (ret)
			goto out;
	}
	start = skb_checksum_start_offset(skb);
	offset = start + offsetof(struct sctphdr, checksum);
	if (WARN_ON_ONCE(offset >= skb_headlen(skb))) {
		ret = -EINVAL;
		goto out;
	}

	ret = skb_ensure_writable(skb, offset + sizeof(__le32));
	if (ret)
		goto out;

	crc32c_csum = cpu_to_le32(~__skb_checksum(skb, start,
						  skb->len - start, ~(__u32)0,
						  crc32c_csum_stub));
	*(__le32 *)(skb->data + offset) = crc32c_csum;
	skb->ip_summed = CHECKSUM_NONE;
	skb->csum_not_inet = 0;
out:
	return ret;
}

__be16 skb_network_protocol(struct sk_buff *skb, int *depth)
{
	__be16 type = skb->protocol;

	/* Tunnel gso handlers can set protocol to ethernet. */
	if (type == htons(ETH_P_TEB)) {
		struct ethhdr *eth;

		if (unlikely(!pskb_may_pull(skb, sizeof(struct ethhdr))))
			return 0;

		eth = (struct ethhdr *)skb->data;
		type = eth->h_proto;
	}

	return __vlan_get_protocol(skb, type, depth);
}

/**
 *	skb_mac_gso_segment - mac layer segmentation handler.
 *	@skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 */
struct sk_buff *skb_mac_gso_segment(struct sk_buff *skb,
				    netdev_features_t features)
{
	struct sk_buff *segs = ERR_PTR(-EPROTONOSUPPORT);
	struct packet_offload *ptype;
	int vlan_depth = skb->mac_len;
	__be16 type = skb_network_protocol(skb, &vlan_depth);

	if (unlikely(!type))
		return ERR_PTR(-EINVAL);

	__skb_pull(skb, vlan_depth);

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, &offload_base, list) {
		if (ptype->type == type && ptype->callbacks.gso_segment) {
			segs = ptype->callbacks.gso_segment(skb, features);
			break;
		}
	}
	rcu_read_unlock();

	__skb_push(skb, skb->data - skb_mac_header(skb));

	return segs;
}
EXPORT_SYMBOL(skb_mac_gso_segment);


/* openvswitch calls this on rx path, so we need a different check.
 */
static inline bool skb_needs_check(struct sk_buff *skb, bool tx_path)
{
	if (tx_path)
		return skb->ip_summed != CHECKSUM_PARTIAL &&
		       skb->ip_summed != CHECKSUM_UNNECESSARY;

	return skb->ip_summed == CHECKSUM_NONE;
}

/**
 *	__skb_gso_segment - Perform segmentation on skb.
 *	@skb: buffer to segment
 *	@features: features for the output path (see dev->features)
 *	@tx_path: whether it is called in TX path
 *
 *	This function segments the given skb and returns a list of segments.
 *
 *	It may return NULL if the skb requires no segmentation.  This is
 *	only possible when GSO is used for verifying header integrity.
 *
 *	Segmentation preserves SKB_GSO_CB_OFFSET bytes of previous skb cb.
 */
struct sk_buff *__skb_gso_segment(struct sk_buff *skb,
				  netdev_features_t features, bool tx_path)
{
	struct sk_buff *segs;

	if (unlikely(skb_needs_check(skb, tx_path))) {
		int err;

		/* We're going to init ->check field in TCP or UDP header */
		err = skb_cow_head(skb, 0);
		if (err < 0)
			return ERR_PTR(err);
	}

	/* Only report GSO partial support if it will enable us to
	 * support segmentation on this frame without needing additional
	 * work.
	 */
	if (features & NETIF_F_GSO_PARTIAL) {
		netdev_features_t partial_features = NETIF_F_GSO_ROBUST;
		struct net_device *dev = skb->dev;

		partial_features |= dev->features & dev->gso_partial_features;
		if (!skb_gso_ok(skb, features | partial_features))
			features &= ~NETIF_F_GSO_PARTIAL;
	}

	BUILD_BUG_ON(SKB_GSO_CB_OFFSET +
		     sizeof(*SKB_GSO_CB(skb)) > sizeof(skb->cb));

	SKB_GSO_CB(skb)->mac_offset = skb_headroom(skb);
	SKB_GSO_CB(skb)->encap_level = 0;

	skb_reset_mac_header(skb);
	skb_reset_mac_len(skb);

	segs = skb_mac_gso_segment(skb, features);

	if (segs != skb && unlikely(skb_needs_check(skb, tx_path) && !IS_ERR(segs)))
		skb_warn_bad_offload(skb);

	return segs;
}
EXPORT_SYMBOL(__skb_gso_segment);

/* Take action when hardware reception checksum errors are detected. */
#ifdef CONFIG_BUG
static void do_netdev_rx_csum_fault(struct net_device *dev, struct sk_buff *skb)
{
	netdev_err(dev, "hw csum failure\n");
	skb_dump(KERN_ERR, skb, true);
	dump_stack();
}

void netdev_rx_csum_fault(struct net_device *dev, struct sk_buff *skb)
{
	DO_ONCE_LITE(do_netdev_rx_csum_fault, dev, skb);
}
EXPORT_SYMBOL(netdev_rx_csum_fault);
#endif

/* XXX: check that highmem exists at all on the given machine. */
static int illegal_highdma(struct net_device *dev, struct sk_buff *skb)
{
#ifdef CONFIG_HIGHMEM
	int i;

	if (!(dev->features & NETIF_F_HIGHDMA)) {
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

			if (PageHighMem(skb_frag_page(frag)))
				return 1;
		}
	}
#endif
	return 0;
}

/* If MPLS offload request, verify we are testing hardware MPLS features
 * instead of standard features for the netdev.
 */
#if IS_ENABLED(CONFIG_NET_MPLS_GSO)
static netdev_features_t net_mpls_features(struct sk_buff *skb,
					   netdev_features_t features,
					   __be16 type)
{
	if (eth_p_mpls(type))
		features &= skb->dev->mpls_features;

	return features;
}
#else
static netdev_features_t net_mpls_features(struct sk_buff *skb,
					   netdev_features_t features,
					   __be16 type)
{
	return features;
}
#endif

static netdev_features_t harmonize_features(struct sk_buff *skb,
	netdev_features_t features)
{
	__be16 type;

	type = skb_network_protocol(skb, NULL);
	features = net_mpls_features(skb, features, type);

	if (skb->ip_summed != CHECKSUM_NONE &&
	    !can_checksum_protocol(features, type)) {
		features &= ~(NETIF_F_CSUM_MASK | NETIF_F_GSO_MASK);
	}
	if (illegal_highdma(skb->dev, skb))
		features &= ~NETIF_F_SG;

	return features;
}

netdev_features_t passthru_features_check(struct sk_buff *skb,
					  struct net_device *dev,
					  netdev_features_t features)
{
	return features;
}
EXPORT_SYMBOL(passthru_features_check);

static netdev_features_t dflt_features_check(struct sk_buff *skb,
					     struct net_device *dev,
					     netdev_features_t features)
{
	return vlan_features_check(skb, features);
}

static netdev_features_t gso_features_check(const struct sk_buff *skb,
					    struct net_device *dev,
					    netdev_features_t features)
{
	u16 gso_segs = skb_shinfo(skb)->gso_segs;

	if (gso_segs > dev->gso_max_segs)
		return features & ~NETIF_F_GSO_MASK;

	if (!skb_shinfo(skb)->gso_type) {
		skb_warn_bad_offload(skb);
		return features & ~NETIF_F_GSO_MASK;
	}

	/* Support for GSO partial features requires software
	 * intervention before we can actually process the packets
	 * so we need to strip support for any partial features now
	 * and we can pull them back in after we have partially
	 * segmented the frame.
	 */
	if (!(skb_shinfo(skb)->gso_type & SKB_GSO_PARTIAL))
		features &= ~dev->gso_partial_features;

	/* Make sure to clear the IPv4 ID mangling feature if the
	 * IPv4 header has the potential to be fragmented.
	 */
	if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV4) {
		struct iphdr *iph = skb->encapsulation ?
				    inner_ip_hdr(skb) : ip_hdr(skb);

		if (!(iph->frag_off & htons(IP_DF)))
			features &= ~NETIF_F_TSO_MANGLEID;
	}

	return features;
}

netdev_features_t netif_skb_features(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	netdev_features_t features = dev->features;

	if (skb_is_gso(skb))
		features = gso_features_check(skb, dev, features);

	/* If encapsulation offload request, verify we are testing
	 * hardware encapsulation features instead of standard
	 * features for the netdev
	 */
	if (skb->encapsulation)
		features &= dev->hw_enc_features;

	if (skb_vlan_tagged(skb))
		features = netdev_intersect_features(features,
						     dev->vlan_features |
						     NETIF_F_HW_VLAN_CTAG_TX |
						     NETIF_F_HW_VLAN_STAG_TX);

	if (dev->netdev_ops->ndo_features_check)
		features &= dev->netdev_ops->ndo_features_check(skb, dev,
								features);
	else
		features &= dflt_features_check(skb, dev, features);

	return harmonize_features(skb, features);
}
EXPORT_SYMBOL(netif_skb_features);

static int xmit_one(struct sk_buff *skb, struct net_device *dev,
		    struct netdev_queue *txq, bool more)
{
	unsigned int len;
	int rc;

	if (dev_nit_active(dev))
		dev_queue_xmit_nit(skb, dev);

	len = skb->len;
	PRANDOM_ADD_NOISE(skb, dev, txq, len + jiffies);
	trace_net_dev_start_xmit(skb, dev);
	rc = netdev_start_xmit(skb, dev, txq, more);
	trace_net_dev_xmit(skb, rc, dev, len);

	return rc;
}

struct sk_buff *dev_hard_start_xmit(struct sk_buff *first, struct net_device *dev,
				    struct netdev_queue *txq, int *ret)
{
	struct sk_buff *skb = first;
	int rc = NETDEV_TX_OK;

	while (skb) {
		struct sk_buff *next = skb->next;

		skb_mark_not_on_list(skb);
		rc = xmit_one(skb, dev, txq, next != NULL);
		if (unlikely(!dev_xmit_complete(rc))) {
			skb->next = next;
			goto out;
		}

		skb = next;
		if (netif_tx_queue_stopped(txq) && skb) {
			rc = NETDEV_TX_BUSY;
			break;
		}
	}

out:
	*ret = rc;
	return skb;
}

static struct sk_buff *validate_xmit_vlan(struct sk_buff *skb,
					  netdev_features_t features)
{
	if (skb_vlan_tag_present(skb) &&
	    !vlan_hw_offload_capable(features, skb->vlan_proto))
		skb = __vlan_hwaccel_push_inside(skb);
	return skb;
}

int skb_csum_hwoffload_help(struct sk_buff *skb,
			    const netdev_features_t features)
{
	if (unlikely(skb_csum_is_sctp(skb)))
		return !!(features & NETIF_F_SCTP_CRC) ? 0 :
			skb_crc32c_csum_help(skb);

	if (features & NETIF_F_HW_CSUM)
		return 0;

	if (features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) {
		switch (skb->csum_offset) {
		case offsetof(struct tcphdr, check):
		case offsetof(struct udphdr, check):
			return 0;
		}
	}

	return skb_checksum_help(skb);
}
EXPORT_SYMBOL(skb_csum_hwoffload_help);

static struct sk_buff *validate_xmit_skb(struct sk_buff *skb, struct net_device *dev, bool *again)
{
	netdev_features_t features;

	features = netif_skb_features(skb);
	skb = validate_xmit_vlan(skb, features);
	if (unlikely(!skb))
		goto out_null;

	skb = sk_validate_xmit_skb(skb, dev);
	if (unlikely(!skb))
		goto out_null;

	if (netif_needs_gso(skb, features)) {
		struct sk_buff *segs;

		segs = skb_gso_segment(skb, features);
		if (IS_ERR(segs)) {
			goto out_kfree_skb;
		} else if (segs) {
			consume_skb(skb);
			skb = segs;
		}
	} else {
		if (skb_needs_linearize(skb, features) &&
		    __skb_linearize(skb))
			goto out_kfree_skb;

		/* If packet is not checksummed and device does not
		 * support checksumming for this protocol, complete
		 * checksumming here.
		 */
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			if (skb->encapsulation)
				skb_set_inner_transport_header(skb,
							       skb_checksum_start_offset(skb));
			else
				skb_set_transport_header(skb,
							 skb_checksum_start_offset(skb));
			if (skb_csum_hwoffload_help(skb, features))
				goto out_kfree_skb;
		}
	}

	skb = validate_xmit_xfrm(skb, features, again);

	return skb;

out_kfree_skb:
	kfree_skb(skb);
out_null:
	atomic_long_inc(&dev->tx_dropped);
	return NULL;
}

struct sk_buff *validate_xmit_skb_list(struct sk_buff *skb, struct net_device *dev, bool *again)
{
	struct sk_buff *next, *head = NULL, *tail;

	for (; skb != NULL; skb = next) {
		next = skb->next;
		skb_mark_not_on_list(skb);

		/* in case skb wont be segmented, point to itself */
		skb->prev = skb;

		skb = validate_xmit_skb(skb, dev, again);
		if (!skb)
			continue;

		if (!head)
			head = skb;
		else
			tail->next = skb;
		/* If skb was segmented, skb->prev points to
		 * the last segment. If not, it still contains skb.
		 */
		tail = skb->prev;
	}
	return head;
}
EXPORT_SYMBOL_GPL(validate_xmit_skb_list);

static void qdisc_pkt_len_init(struct sk_buff *skb)
{
	const struct skb_shared_info *shinfo = skb_shinfo(skb);

	qdisc_skb_cb(skb)->pkt_len = skb->len;

	/* To get more precise estimation of bytes sent on wire,
	 * we add to pkt_len the headers size of all segments
	 */
	if (shinfo->gso_size && skb_transport_header_was_set(skb)) {
		unsigned int hdr_len;
		u16 gso_segs = shinfo->gso_segs;

		/* mac layer + network layer */
		hdr_len = skb_transport_header(skb) - skb_mac_header(skb);

		/* + transport layer */
		if (likely(shinfo->gso_type & (SKB_GSO_TCPV4 | SKB_GSO_TCPV6))) {
			const struct tcphdr *th;
			struct tcphdr _tcphdr;

			th = skb_header_pointer(skb, skb_transport_offset(skb),
						sizeof(_tcphdr), &_tcphdr);
			if (likely(th))
				hdr_len += __tcp_hdrlen(th);
		} else {
			struct udphdr _udphdr;

			if (skb_header_pointer(skb, skb_transport_offset(skb),
					       sizeof(_udphdr), &_udphdr))
				hdr_len += sizeof(struct udphdr);
		}

		if (shinfo->gso_type & SKB_GSO_DODGY)
			gso_segs = DIV_ROUND_UP(skb->len - hdr_len,
						shinfo->gso_size);

		qdisc_skb_cb(skb)->pkt_len += (gso_segs - 1) * hdr_len;
	}
}

static int dev_qdisc_enqueue(struct sk_buff *skb, struct Qdisc *q,
			     struct sk_buff **to_free,
			     struct netdev_queue *txq)
{
	int rc;

	rc = q->enqueue(skb, q, to_free) & NET_XMIT_MASK;
	if (rc == NET_XMIT_SUCCESS)
		trace_qdisc_enqueue(q, txq, skb);
	return rc;
}

static inline int __dev_xmit_skb(struct sk_buff *skb, struct Qdisc *q,
				 struct net_device *dev,
				 struct netdev_queue *txq)
{
	spinlock_t *root_lock = qdisc_lock(q);
	struct sk_buff *to_free = NULL;
	bool contended;
	int rc;

	qdisc_calculate_pkt_len(skb, q);

	if (q->flags & TCQ_F_NOLOCK) {
		if (q->flags & TCQ_F_CAN_BYPASS && nolock_qdisc_is_empty(q) &&
		    qdisc_run_begin(q)) {
			/* Retest nolock_qdisc_is_empty() within the protection
			 * of q->seqlock to protect from racing with requeuing.
			 */
			if (unlikely(!nolock_qdisc_is_empty(q))) {
				rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
				__qdisc_run(q);
				qdisc_run_end(q);

				goto no_lock_out;
			}

			qdisc_bstats_cpu_update(q, skb);
			if (sch_direct_xmit(skb, q, dev, txq, NULL, true) &&
			    !nolock_qdisc_is_empty(q))
				__qdisc_run(q);

			qdisc_run_end(q);
			return NET_XMIT_SUCCESS;
		}

		rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
		qdisc_run(q);

no_lock_out:
		if (unlikely(to_free))
			kfree_skb_list(to_free);
		return rc;
	}

	/*
	 * Heuristic to force contended enqueues to serialize on a
	 * separate lock before trying to get qdisc main lock.
	 * This permits qdisc->running owner to get the lock more
	 * often and dequeue packets faster.
	 */
	contended = qdisc_is_running(q);
	if (unlikely(contended))
		spin_lock(&q->busylock);

	spin_lock(root_lock);
	if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED, &q->state))) {
		__qdisc_drop(skb, &to_free);
		rc = NET_XMIT_DROP;
	} else if ((q->flags & TCQ_F_CAN_BYPASS) && !qdisc_qlen(q) &&
		   qdisc_run_begin(q)) {
		/*
		 * This is a work-conserving queue; there are no old skbs
		 * waiting to be sent out; and the qdisc is not running -
		 * xmit the skb directly.
		 */

		qdisc_bstats_update(q, skb);

		if (sch_direct_xmit(skb, q, dev, txq, root_lock, true)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
		}

		qdisc_run_end(q);
		rc = NET_XMIT_SUCCESS;
	} else {
		rc = dev_qdisc_enqueue(skb, q, &to_free, txq);
		if (qdisc_run_begin(q)) {
			if (unlikely(contended)) {
				spin_unlock(&q->busylock);
				contended = false;
			}
			__qdisc_run(q);
			qdisc_run_end(q);
		}
	}
	spin_unlock(root_lock);
	if (unlikely(to_free))
		kfree_skb_list(to_free);
	if (unlikely(contended))
		spin_unlock(&q->busylock);
	return rc;
}

#if IS_ENABLED(CONFIG_CGROUP_NET_PRIO)
static void skb_update_prio(struct sk_buff *skb)
{
	const struct netprio_map *map;
	const struct sock *sk;
	unsigned int prioidx;

	if (skb->priority)
		return;
	map = rcu_dereference_bh(skb->dev->priomap);
	if (!map)
		return;
	sk = skb_to_full_sk(skb);
	if (!sk)
		return;

	prioidx = sock_cgroup_prioidx(&sk->sk_cgrp_data);

	if (prioidx < map->priomap_len)
		skb->priority = map->priomap[prioidx];
}
#else
#define skb_update_prio(skb)
#endif

/**
 *	dev_loopback_xmit - loop back @skb
 *	@net: network namespace this loopback is happening in
 *	@sk:  sk needed to be a netfilter okfn
 *	@skb: buffer to transmit
 */
int dev_loopback_xmit(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb_reset_mac_header(skb);
	__skb_pull(skb, skb_network_offset(skb));
	skb->pkt_type = PACKET_LOOPBACK;
	if (skb->ip_summed == CHECKSUM_NONE)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	WARN_ON(!skb_dst(skb));
	skb_dst_force(skb);
	netif_rx_ni(skb);
	return 0;
}
EXPORT_SYMBOL(dev_loopback_xmit);

#ifdef CONFIG_NET_EGRESS
static struct sk_buff *
sch_handle_egress(struct sk_buff *skb, int *ret, struct net_device *dev)
{
#ifdef CONFIG_NET_CLS_ACT
	struct mini_Qdisc *miniq = rcu_dereference_bh(dev->miniq_egress);
	struct tcf_result cl_res;

	if (!miniq)
		return skb;

	/* qdisc_skb_cb(skb)->pkt_len was already set by the caller. */
	qdisc_skb_cb(skb)->mru = 0;
	qdisc_skb_cb(skb)->post_ct = false;
	mini_qdisc_bstats_cpu_update(miniq, skb);

	switch (tcf_classify(skb, miniq->block, miniq->filter_list, &cl_res, false)) {
	case TC_ACT_OK:
	case TC_ACT_RECLASSIFY:
		skb->tc_index = TC_H_MIN(cl_res.classid);
		break;
	case TC_ACT_SHOT:
		mini_qdisc_qstats_cpu_drop(miniq);
		*ret = NET_XMIT_DROP;
		kfree_skb(skb);
		return NULL;
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
	case TC_ACT_TRAP:
		*ret = NET_XMIT_SUCCESS;
		consume_skb(skb);
		return NULL;
	case TC_ACT_REDIRECT:
		/* No need to push/pop skb's mac_header here on egress! */
		skb_do_redirect(skb);
		*ret = NET_XMIT_SUCCESS;
		return NULL;
	default:
		break;
	}
#endif /* CONFIG_NET_CLS_ACT */

	return skb;
}
#endif /* CONFIG_NET_EGRESS */

#ifdef CONFIG_XPS
static int __get_xps_queue_idx(struct net_device *dev, struct sk_buff *skb,
			       struct xps_dev_maps *dev_maps, unsigned int tci)
{
	int tc = netdev_get_prio_tc_map(dev, skb->priority);
	struct xps_map *map;
	int queue_index = -1;

	if (tc >= dev_maps->num_tc || tci >= dev_maps->nr_ids)
		return queue_index;

	tci *= dev_maps->num_tc;
	tci += tc;

	map = rcu_dereference(dev_maps->attr_map[tci]);
	if (map) {
		if (map->len == 1)
			queue_index = map->queues[0];
		else
			queue_index = map->queues[reciprocal_scale(
						skb_get_hash(skb), map->len)];
		if (unlikely(queue_index >= dev->real_num_tx_queues))
			queue_index = -1;
	}
	return queue_index;
}
#endif

static int get_xps_queue(struct net_device *dev, struct net_device *sb_dev,
			 struct sk_buff *skb)
{
#ifdef CONFIG_XPS
	struct xps_dev_maps *dev_maps;
	struct sock *sk = skb->sk;
	int queue_index = -1;

	if (!static_key_false(&xps_needed))
		return -1;

	rcu_read_lock();
	if (!static_key_false(&xps_rxqs_needed))
		goto get_cpus_map;

	dev_maps = rcu_dereference(sb_dev->xps_maps[XPS_RXQS]);
	if (dev_maps) {
		int tci = sk_rx_queue_get(sk);

		if (tci >= 0)
			queue_index = __get_xps_queue_idx(dev, skb, dev_maps,
							  tci);
	}

get_cpus_map:
	if (queue_index < 0) {
		dev_maps = rcu_dereference(sb_dev->xps_maps[XPS_CPUS]);
		if (dev_maps) {
			unsigned int tci = skb->sender_cpu - 1;

			queue_index = __get_xps_queue_idx(dev, skb, dev_maps,
							  tci);
		}
	}
	rcu_read_unlock();

	return queue_index;
#else
	return -1;
#endif
}

u16 dev_pick_tx_zero(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev)
{
	return 0;
}
EXPORT_SYMBOL(dev_pick_tx_zero);

u16 dev_pick_tx_cpu_id(struct net_device *dev, struct sk_buff *skb,
		       struct net_device *sb_dev)
{
	return (u16)raw_smp_processor_id() % dev->real_num_tx_queues;
}
EXPORT_SYMBOL(dev_pick_tx_cpu_id);

u16 netdev_pick_tx(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev)
{
	struct sock *sk = skb->sk;
	int queue_index = sk_tx_queue_get(sk);

	sb_dev = sb_dev ? : dev;

	if (queue_index < 0 || skb->ooo_okay ||
	    queue_index >= dev->real_num_tx_queues) {
		int new_index = get_xps_queue(dev, sb_dev, skb);

		if (new_index < 0)
			new_index = skb_tx_hash(dev, sb_dev, skb);

		if (queue_index != new_index && sk &&
		    sk_fullsock(sk) &&
		    rcu_access_pointer(sk->sk_dst_cache))
			sk_tx_queue_set(sk, new_index);

		queue_index = new_index;
	}

	return queue_index;
}
EXPORT_SYMBOL(netdev_pick_tx);

struct netdev_queue *netdev_core_pick_tx(struct net_device *dev,
					 struct sk_buff *skb,
					 struct net_device *sb_dev)
{
	int queue_index = 0;

#ifdef CONFIG_XPS
	u32 sender_cpu = skb->sender_cpu - 1;

	if (sender_cpu >= (u32)NR_CPUS)
		skb->sender_cpu = raw_smp_processor_id() + 1;
#endif

	if (dev->real_num_tx_queues != 1) {
		const struct net_device_ops *ops = dev->netdev_ops;

		if (ops->ndo_select_queue)
			queue_index = ops->ndo_select_queue(dev, skb, sb_dev);
		else
			queue_index = netdev_pick_tx(dev, skb, sb_dev);

		queue_index = netdev_cap_txqueue(dev, queue_index);
	}

	skb_set_queue_mapping(skb, queue_index);
	return netdev_get_tx_queue(dev, queue_index);
}

/**
 *	__dev_queue_xmit - transmit a buffer
 *	@skb: buffer to transmit
 *	@sb_dev: suboordinate device used for L2 forwarding offload
 *
 *	Queue a buffer for transmission to a network device. The caller must
 *	have set the device and priority and built the buffer before calling
 *	this function. The function can be called from an interrupt.
 *
 *	A negative errno code is returned on a failure. A success does not
 *	guarantee the frame will be transmitted as it may be dropped due
 *	to congestion or traffic shaping.
 *
 * -----------------------------------------------------------------------------------
 *      I notice this method can also return errors from the queue disciplines,
 *      including NET_XMIT_DROP, which is a positive value.  So, errors can also
 *      be positive.
 *
 *      Regardless of the return value, the skb is consumed, so it is currently
 *      difficult to retry a send to this method.  (You can bump the ref count
 *      before sending to hold a reference for retry if you are careful.)
 *
 *      When calling this method, interrupts MUST be enabled.  This is because
 *      the BH enable code must have IRQs enabled so that it will not deadlock.
 *          --BLG
 */
static int __dev_queue_xmit(struct sk_buff *skb, struct net_device *sb_dev)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	struct Qdisc *q;
	int rc = -ENOMEM;
	bool again = false;

	skb_reset_mac_header(skb);

	if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_SCHED_TSTAMP))
		__skb_tstamp_tx(skb, NULL, NULL, skb->sk, SCM_TSTAMP_SCHED);

	/* Disable soft irqs for various locks below. Also
	 * stops preemption for RCU.
	 */
	rcu_read_lock_bh();

	skb_update_prio(skb);

	qdisc_pkt_len_init(skb);
#ifdef CONFIG_NET_CLS_ACT
	skb->tc_at_ingress = 0;
#endif
#ifdef CONFIG_NET_EGRESS
	if (static_branch_unlikely(&egress_needed_key)) {
		if (nf_hook_egress_active()) {
			skb = nf_hook_egress(skb, &rc, dev);
			if (!skb)
				goto out;
		}
		nf_skip_egress(skb, true);
		skb = sch_handle_egress(skb, &rc, dev);
		if (!skb)
			goto out;
		nf_skip_egress(skb, false);
	}
#endif
	/* If device/qdisc don't need skb->dst, release it right now while
	 * its hot in this cpu cache.
	 */
	if (dev->priv_flags & IFF_XMIT_DST_RELEASE)
		skb_dst_drop(skb);
	else
		skb_dst_force(skb);

	txq = netdev_core_pick_tx(dev, skb, sb_dev);
	q = rcu_dereference_bh(txq->qdisc);

	trace_net_dev_queue(skb);
	if (q->enqueue) {
		rc = __dev_xmit_skb(skb, q, dev, txq);
		goto out;
	}

	/* The device has no queue. Common case for software devices:
	 * loopback, all the sorts of tunnels...

	 * Really, it is unlikely that netif_tx_lock protection is necessary
	 * here.  (f.e. loopback and IP tunnels are clean ignoring statistics
	 * counters.)
	 * However, it is possible, that they rely on protection
	 * made by us here.

	 * Check this and shot the lock. It is not prone from deadlocks.
	 *Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags & IFF_UP) {
		int cpu = smp_processor_id(); /* ok because BHs are off */

		/* Other cpus might concurrently change txq->xmit_lock_owner
		 * to -1 or to their cpu id, but not to our id.
		 */
		if (READ_ONCE(txq->xmit_lock_owner) != cpu) {
			if (dev_xmit_recursion())
				goto recursion_alert;

			skb = validate_xmit_skb(skb, dev, &again);
			if (!skb)
				goto out;

			PRANDOM_ADD_NOISE(skb, dev, txq, jiffies);
			HARD_TX_LOCK(dev, txq, cpu);

			if (!netif_xmit_stopped(txq)) {
				dev_xmit_recursion_inc();
				skb = dev_hard_start_xmit(skb, dev, txq, &rc);
				dev_xmit_recursion_dec();
				if (dev_xmit_complete(rc)) {
					HARD_TX_UNLOCK(dev, txq);
					goto out;
				}
			}
			HARD_TX_UNLOCK(dev, txq);
			net_crit_ratelimited("Virtual device %s asks to queue packet!\n",
					     dev->name);
		} else {
			/* Recursion is detected! It is possible,
			 * unfortunately
			 */
recursion_alert:
			net_crit_ratelimited("Dead loop on virtual device %s, fix it urgently!\n",
					     dev->name);
		}
	}

	rc = -ENETDOWN;
	rcu_read_unlock_bh();

	atomic_long_inc(&dev->tx_dropped);
	kfree_skb_list(skb);
	return rc;
out:
	rcu_read_unlock_bh();
	return rc;
}

int dev_queue_xmit(struct sk_buff *skb)
{
	return __dev_queue_xmit(skb, NULL);
}
EXPORT_SYMBOL(dev_queue_xmit);

int dev_queue_xmit_accel(struct sk_buff *skb, struct net_device *sb_dev)
{
	return __dev_queue_xmit(skb, sb_dev);
}
EXPORT_SYMBOL(dev_queue_xmit_accel);

int __dev_direct_xmit(struct sk_buff *skb, u16 queue_id)
{
	struct net_device *dev = skb->dev;
	struct sk_buff *orig_skb = skb;
	struct netdev_queue *txq;
	int ret = NETDEV_TX_BUSY;
	bool again = false;

	if (unlikely(!netif_running(dev) ||
		     !netif_carrier_ok(dev)))
		goto drop;

	skb = validate_xmit_skb_list(skb, dev, &again);
	if (skb != orig_skb)
		goto drop;

	skb_set_queue_mapping(skb, queue_id);
	txq = skb_get_tx_queue(dev, skb);
	PRANDOM_ADD_NOISE(skb, dev, txq, jiffies);

	local_bh_disable();

	dev_xmit_recursion_inc();
	HARD_TX_LOCK(dev, txq, smp_processor_id());
	if (!netif_xmit_frozen_or_drv_stopped(txq))
		ret = netdev_start_xmit(skb, dev, txq, false);
	HARD_TX_UNLOCK(dev, txq);
	dev_xmit_recursion_dec();

	local_bh_enable();
	return ret;
drop:
	atomic_long_inc(&dev->tx_dropped);
	kfree_skb_list(skb);
	return NET_XMIT_DROP;
}
EXPORT_SYMBOL(__dev_direct_xmit);

/*************************************************************************
 *			Receiver routines
 *************************************************************************/

int netdev_max_backlog __read_mostly = 1000;
EXPORT_SYMBOL(netdev_max_backlog);

int netdev_tstamp_prequeue __read_mostly = 1;
int netdev_budget __read_mostly = 300;
/* Must be at least 2 jiffes to guarantee 1 jiffy timeout */
unsigned int __read_mostly netdev_budget_usecs = 2 * USEC_PER_SEC / HZ;
int weight_p __read_mostly = 64;           /* old backlog weight */
int dev_weight_rx_bias __read_mostly = 1;  /* bias for backlog weight */
int dev_weight_tx_bias __read_mostly = 1;  /* bias for output_queue quota */
int dev_rx_weight __read_mostly = 64;
int dev_tx_weight __read_mostly = 64;
/* Maximum number of GRO_NORMAL skbs to batch up for list-RX */
int gro_normal_batch __read_mostly = 8;

/* Called with irq disabled */
static inline void ____napi_schedule(struct softnet_data *sd,
				     struct napi_struct *napi)
{
	struct task_struct *thread;

	if (test_bit(NAPI_STATE_THREADED, &napi->state)) {
		/* Paired with smp_mb__before_atomic() in
		 * napi_enable()/dev_set_threaded().
		 * Use READ_ONCE() to guarantee a complete
		 * read on napi->thread. Only call
		 * wake_up_process() when it's not NULL.
		 */
		thread = READ_ONCE(napi->thread);
		if (thread) {
			/* Avoid doing set_bit() if the thread is in
			 * INTERRUPTIBLE state, cause napi_thread_wait()
			 * makes sure to proceed with napi polling
			 * if the thread is explicitly woken from here.
			 */
			if (READ_ONCE(thread->__state) != TASK_INTERRUPTIBLE)
				set_bit(NAPI_STATE_SCHED_THREADED, &napi->state);
			wake_up_process(thread);
			return;
		}
	}

	list_add_tail(&napi->poll_list, &sd->poll_list);
	__raise_softirq_irqoff(NET_RX_SOFTIRQ);
}

#ifdef CONFIG_RPS

/* One global table that all flow-based protocols share. */
struct rps_sock_flow_table __rcu *rps_sock_flow_table __read_mostly;
EXPORT_SYMBOL(rps_sock_flow_table);
u32 rps_cpu_mask __read_mostly;
EXPORT_SYMBOL(rps_cpu_mask);

struct static_key_false rps_needed __read_mostly;
EXPORT_SYMBOL(rps_needed);
struct static_key_false rfs_needed __read_mostly;
EXPORT_SYMBOL(rfs_needed);

static struct rps_dev_flow *
set_rps_cpu(struct net_device *dev, struct sk_buff *skb,
	    struct rps_dev_flow *rflow, u16 next_cpu)
{
	if (next_cpu < nr_cpu_ids) {
#ifdef CONFIG_RFS_ACCEL
		struct netdev_rx_queue *rxqueue;
		struct rps_dev_flow_table *flow_table;
		struct rps_dev_flow *old_rflow;
		u32 flow_id;
		u16 rxq_index;
		int rc;

		/* Should we steer this flow to a different hardware queue? */
		if (!skb_rx_queue_recorded(skb) || !dev->rx_cpu_rmap ||
		    !(dev->features & NETIF_F_NTUPLE))
			goto out;
		rxq_index = cpu_rmap_lookup_index(dev->rx_cpu_rmap, next_cpu);
		if (rxq_index == skb_get_rx_queue(skb))
			goto out;

		rxqueue = dev->_rx + rxq_index;
		flow_table = rcu_dereference(rxqueue->rps_flow_table);
		if (!flow_table)
			goto out;
		flow_id = skb_get_hash(skb) & flow_table->mask;
		rc = dev->netdev_ops->ndo_rx_flow_steer(dev, skb,
							rxq_index, flow_id);
		if (rc < 0)
			goto out;
		old_rflow = rflow;
		rflow = &flow_table->flows[flow_id];
		rflow->filter = rc;
		if (old_rflow->filter == rflow->filter)
			old_rflow->filter = RPS_NO_FILTER;
	out:
#endif
		rflow->last_qtail =
			per_cpu(softnet_data, next_cpu).input_queue_head;
	}

	rflow->cpu = next_cpu;
	return rflow;
}

/*
 * get_rps_cpu is called from netif_receive_skb and returns the target
 * CPU from the RPS map of the receiving queue for a given skb.
 * rcu_read_lock must be held on entry.
 */
static int get_rps_cpu(struct net_device *dev, struct sk_buff *skb,
		       struct rps_dev_flow **rflowp)
{
	const struct rps_sock_flow_table *sock_flow_table;
	struct netdev_rx_queue *rxqueue = dev->_rx;
	struct rps_dev_flow_table *flow_table;
	struct rps_map *map;
	int cpu = -1;
	u32 tcpu;
	u32 hash;

	if (skb_rx_queue_recorded(skb)) {
		u16 index = skb_get_rx_queue(skb);

		if (unlikely(index >= dev->real_num_rx_queues)) {
			WARN_ONCE(dev->real_num_rx_queues > 1,
				  "%s received packet on queue %u, but number "
				  "of RX queues is %u\n",
				  dev->name, index, dev->real_num_rx_queues);
			goto done;
		}
		rxqueue += index;
	}

	/* Avoid computing hash if RFS/RPS is not active for this rxqueue */

	flow_table = rcu_dereference(rxqueue->rps_flow_table);
	map = rcu_dereference(rxqueue->rps_map);
	if (!flow_table && !map)
		goto done;

	skb_reset_network_header(skb);
	hash = skb_get_hash(skb);
	if (!hash)
		goto done;

	sock_flow_table = rcu_dereference(rps_sock_flow_table);
	if (flow_table && sock_flow_table) {
		struct rps_dev_flow *rflow;
		u32 next_cpu;
		u32 ident;

		/* First check into global flow table if there is a match */
		ident = sock_flow_table->ents[hash & sock_flow_table->mask];
		if ((ident ^ hash) & ~rps_cpu_mask)
			goto try_rps;

		next_cpu = ident & rps_cpu_mask;

		/* OK, now we know there is a match,
		 * we can look at the local (per receive queue) flow table
		 */
		rflow = &flow_table->flows[hash & flow_table->mask];
		tcpu = rflow->cpu;

		/*
		 * If the desired CPU (where last recvmsg was done) is
		 * different from current CPU (one in the rx-queue flow
		 * table entry), switch if one of the following holds:
		 *   - Current CPU is unset (>= nr_cpu_ids).
		 *   - Current CPU is offline.
		 *   - The current CPU's queue tail has advanced beyond the
		 *     last packet that was enqueued using this table entry.
		 *     This guarantees that all previous packets for the flow
		 *     have been dequeued, thus preserving in order delivery.
		 */
		if (unlikely(tcpu != next_cpu) &&
		    (tcpu >= nr_cpu_ids || !cpu_online(tcpu) ||
		     ((int)(per_cpu(softnet_data, tcpu).input_queue_head -
		      rflow->last_qtail)) >= 0)) {
			tcpu = next_cpu;
			rflow = set_rps_cpu(dev, skb, rflow, next_cpu);
		}

		if (tcpu < nr_cpu_ids && cpu_online(tcpu)) {
			*rflowp = rflow;
			cpu = tcpu;
			goto done;
		}
	}

try_rps:

	if (map) {
		tcpu = map->cpus[reciprocal_scale(hash, map->len)];
		if (cpu_online(tcpu)) {
			cpu = tcpu;
			goto done;
		}
	}

done:
	return cpu;
}

#ifdef CONFIG_RFS_ACCEL

/**
 * rps_may_expire_flow - check whether an RFS hardware filter may be removed
 * @dev: Device on which the filter was set
 * @rxq_index: RX queue index
 * @flow_id: Flow ID passed to ndo_rx_flow_steer()
 * @filter_id: Filter ID returned by ndo_rx_flow_steer()
 *
 * Drivers that implement ndo_rx_flow_steer() should periodically call
 * this function for each installed filter and remove the filters for
 * which it returns %true.
 */
bool rps_may_expire_flow(struct net_device *dev, u16 rxq_index,
			 u32 flow_id, u16 filter_id)
{
	struct netdev_rx_queue *rxqueue = dev->_rx + rxq_index;
	struct rps_dev_flow_table *flow_table;
	struct rps_dev_flow *rflow;
	bool expire = true;
	unsigned int cpu;

	rcu_read_lock();
	flow_table = rcu_dereference(rxqueue->rps_flow_table);
	if (flow_table && flow_id <= flow_table->mask) {
		rflow = &flow_table->flows[flow_id];
		cpu = READ_ONCE(rflow->cpu);
		if (rflow->filter == filter_id && cpu < nr_cpu_ids &&
		    ((int)(per_cpu(softnet_data, cpu).input_queue_head -
			   rflow->last_qtail) <
		     (int)(10 * flow_table->mask)))
			expire = false;
	}
	rcu_read_unlock();
	return expire;
}
EXPORT_SYMBOL(rps_may_expire_flow);

#endif /* CONFIG_RFS_ACCEL */

/* Called from hardirq (IPI) context */
static void rps_trigger_softirq(void *data)
{
	struct softnet_data *sd = data;

	____napi_schedule(sd, &sd->backlog);
	sd->received_rps++;
}

#endif /* CONFIG_RPS */

/*
 * Check if this softnet_data structure is another cpu one
 * If yes, queue it to our IPI list and return 1
 * If no, return 0
 */
static int rps_ipi_queued(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *mysd = this_cpu_ptr(&softnet_data);

	if (sd != mysd) {
		sd->rps_ipi_next = mysd->rps_ipi_list;
		mysd->rps_ipi_list = sd;

		__raise_softirq_irqoff(NET_RX_SOFTIRQ);
		return 1;
	}
#endif /* CONFIG_RPS */
	return 0;
}

#ifdef CONFIG_NET_FLOW_LIMIT
int netdev_flow_limit_table_len __read_mostly = (1 << 12);
#endif

static bool skb_flow_limit(struct sk_buff *skb, unsigned int qlen)
{
#ifdef CONFIG_NET_FLOW_LIMIT
	struct sd_flow_limit *fl;
	struct softnet_data *sd;
	unsigned int old_flow, new_flow;

	if (qlen < (netdev_max_backlog >> 1))
		return false;

	sd = this_cpu_ptr(&softnet_data);

	rcu_read_lock();
	fl = rcu_dereference(sd->flow_limit);
	if (fl) {
		new_flow = skb_get_hash(skb) & (fl->num_buckets - 1);
		old_flow = fl->history[fl->history_head];
		fl->history[fl->history_head] = new_flow;

		fl->history_head++;
		fl->history_head &= FLOW_LIMIT_HISTORY - 1;

		if (likely(fl->buckets[old_flow]))
			fl->buckets[old_flow]--;

		if (++fl->buckets[new_flow] > (FLOW_LIMIT_HISTORY >> 1)) {
			fl->count++;
			rcu_read_unlock();
			return true;
		}
	}
	rcu_read_unlock();
#endif
	return false;
}

/*
 * enqueue_to_backlog is called to queue an skb to a per CPU backlog
 * queue (may be a remote CPU queue).
 */
static int enqueue_to_backlog(struct sk_buff *skb, int cpu,
			      unsigned int *qtail)
{
	struct softnet_data *sd;
	unsigned long flags;
	unsigned int qlen;

	sd = &per_cpu(softnet_data, cpu);

	local_irq_save(flags);

	rps_lock(sd);
	if (!netif_running(skb->dev))
		goto drop;
	qlen = skb_queue_len(&sd->input_pkt_queue);
	if (qlen <= netdev_max_backlog && !skb_flow_limit(skb, qlen)) {
		if (qlen) {
enqueue:
			__skb_queue_tail(&sd->input_pkt_queue, skb);
			input_queue_tail_incr_save(sd, qtail);
			rps_unlock(sd);
			local_irq_restore(flags);
			return NET_RX_SUCCESS;
		}

		/* Schedule NAPI for backlog device
		 * We can use non atomic operation since we own the queue lock
		 */
		if (!__test_and_set_bit(NAPI_STATE_SCHED, &sd->backlog.state)) {
			if (!rps_ipi_queued(sd))
				____napi_schedule(sd, &sd->backlog);
		}
		goto enqueue;
	}

drop:
	sd->dropped++;
	rps_unlock(sd);

	local_irq_restore(flags);

	atomic_long_inc(&skb->dev->rx_dropped);
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct netdev_rx_queue *netif_get_rxqueue(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct netdev_rx_queue *rxqueue;

	rxqueue = dev->_rx;

	if (skb_rx_queue_recorded(skb)) {
		u16 index = skb_get_rx_queue(skb);

		if (unlikely(index >= dev->real_num_rx_queues)) {
			WARN_ONCE(dev->real_num_rx_queues > 1,
				  "%s received packet on queue %u, but number "
				  "of RX queues is %u\n",
				  dev->name, index, dev->real_num_rx_queues);

			return rxqueue; /* Return first rxqueue */
		}
		rxqueue += index;
	}
	return rxqueue;
}

u32 bpf_prog_run_generic_xdp(struct sk_buff *skb, struct xdp_buff *xdp,
			     struct bpf_prog *xdp_prog)
{
	void *orig_data, *orig_data_end, *hard_start;
	struct netdev_rx_queue *rxqueue;
	bool orig_bcast, orig_host;
	u32 mac_len, frame_sz;
	__be16 orig_eth_type;
	struct ethhdr *eth;
	u32 metalen, act;
	int off;

	/* The XDP program wants to see the packet starting at the MAC
	 * header.
	 */
	mac_len = skb->data - skb_mac_header(skb);
	hard_start = skb->data - skb_headroom(skb);

	/* SKB "head" area always have tailroom for skb_shared_info */
	frame_sz = (void *)skb_end_pointer(skb) - hard_start;
	frame_sz += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));

	rxqueue = netif_get_rxqueue(skb);
	xdp_init_buff(xdp, frame_sz, &rxqueue->xdp_rxq);
	xdp_prepare_buff(xdp, hard_start, skb_headroom(skb) - mac_len,
			 skb_headlen(skb) + mac_len, true);

	orig_data_end = xdp->data_end;
	orig_data = xdp->data;
	eth = (struct ethhdr *)xdp->data;
	orig_host = ether_addr_equal_64bits(eth->h_dest, skb->dev->dev_addr);
	orig_bcast = is_multicast_ether_addr_64bits(eth->h_dest);
	orig_eth_type = eth->h_proto;

	act = bpf_prog_run_xdp(xdp_prog, xdp);

	/* check if bpf_xdp_adjust_head was used */
	off = xdp->data - orig_data;
	if (off) {
		if (off > 0)
			__skb_pull(skb, off);
		else if (off < 0)
			__skb_push(skb, -off);

		skb->mac_header += off;
		skb_reset_network_header(skb);
	}

	/* check if bpf_xdp_adjust_tail was used */
	off = xdp->data_end - orig_data_end;
	if (off != 0) {
		skb_set_tail_pointer(skb, xdp->data_end - xdp->data);
		skb->len += off; /* positive on grow, negative on shrink */
	}

	/* check if XDP changed eth hdr such SKB needs update */
	eth = (struct ethhdr *)xdp->data;
	if ((orig_eth_type != eth->h_proto) ||
	    (orig_host != ether_addr_equal_64bits(eth->h_dest,
						  skb->dev->dev_addr)) ||
	    (orig_bcast != is_multicast_ether_addr_64bits(eth->h_dest))) {
		__skb_push(skb, ETH_HLEN);
		skb->pkt_type = PACKET_HOST;
		skb->protocol = eth_type_trans(skb, skb->dev);
	}

	/* Redirect/Tx gives L2 packet, code that will reuse skb must __skb_pull
	 * before calling us again on redirect path. We do not call do_redirect
	 * as we leave that up to the caller.
	 *
	 * Caller is responsible for managing lifetime of skb (i.e. calling
	 * kfree_skb in response to actions it cannot handle/XDP_DROP).
	 */
	switch (act) {
	case XDP_REDIRECT:
	case XDP_TX:
		__skb_push(skb, mac_len);
		break;
	case XDP_PASS:
		metalen = xdp->data - xdp->data_meta;
		if (metalen)
			skb_metadata_set(skb, metalen);
		break;
	}

	return act;
}

static u32 netif_receive_generic_xdp(struct sk_buff *skb,
				     struct xdp_buff *xdp,
				     struct bpf_prog *xdp_prog)
{
	u32 act = XDP_DROP;

	/* Reinjected packets coming from act_mirred or similar should
	 * not get XDP generic processing.
	 */
	if (skb_is_redirected(skb))
		return XDP_PASS;

	/* XDP packets must be linear and must have sufficient headroom
	 * of XDP_PACKET_HEADROOM bytes. This is the guarantee that also
	 * native XDP provides, thus we need to do it here as well.
	 */
	if (skb_cloned(skb) || skb_is_nonlinear(skb) ||
	    skb_headroom(skb) < XDP_PACKET_HEADROOM) {
		int hroom = XDP_PACKET_HEADROOM - skb_headroom(skb);
		int troom = skb->tail + skb->data_len - skb->end;

		/* In case we have to go down the path and also linearize,
		 * then lets do the pskb_expand_head() work just once here.
		 */
		if (pskb_expand_head(skb,
				     hroom > 0 ? ALIGN(hroom, NET_SKB_PAD) : 0,
				     troom > 0 ? troom + 128 : 0, GFP_ATOMIC))
			goto do_drop;
		if (skb_linearize(skb))
			goto do_drop;
	}

	act = bpf_prog_run_generic_xdp(skb, xdp, xdp_prog);
	switch (act) {
	case XDP_REDIRECT:
	case XDP_TX:
	case XDP_PASS:
		break;
	default:
		bpf_warn_invalid_xdp_action(act);
		fallthrough;
	case XDP_ABORTED:
		trace_xdp_exception(skb->dev, xdp_prog, act);
		fallthrough;
	case XDP_DROP:
	do_drop:
		kfree_skb(skb);
		break;
	}

	return act;
}

/* When doing generic XDP we have to bypass the qdisc layer and the
 * network taps in order to match in-driver-XDP behavior.
 */
void generic_xdp_tx(struct sk_buff *skb, struct bpf_prog *xdp_prog)
{
	struct net_device *dev = skb->dev;
	struct netdev_queue *txq;
	bool free_skb = true;
	int cpu, rc;

	txq = netdev_core_pick_tx(dev, skb, NULL);
	cpu = smp_processor_id();
	HARD_TX_LOCK(dev, txq, cpu);
	if (!netif_xmit_stopped(txq)) {
		rc = netdev_start_xmit(skb, dev, txq, 0);
		if (dev_xmit_complete(rc))
			free_skb = false;
	}
	HARD_TX_UNLOCK(dev, txq);
	if (free_skb) {
		trace_xdp_exception(dev, xdp_prog, XDP_TX);
		kfree_skb(skb);
	}
}

static DEFINE_STATIC_KEY_FALSE(generic_xdp_needed_key);

int do_xdp_generic(struct bpf_prog *xdp_prog, struct sk_buff *skb)
{
	if (xdp_prog) {
		struct xdp_buff xdp;
		u32 act;
		int err;

		act = netif_receive_generic_xdp(skb, &xdp, xdp_prog);
		if (act != XDP_PASS) {
			switch (act) {
			case XDP_REDIRECT:
				err = xdp_do_generic_redirect(skb->dev, skb,
							      &xdp, xdp_prog);
				if (err)
					goto out_redir;
				break;
			case XDP_TX:
				generic_xdp_tx(skb, xdp_prog);
				break;
			}
			return XDP_DROP;
		}
	}
	return XDP_PASS;
out_redir:
	kfree_skb(skb);
	return XDP_DROP;
}
EXPORT_SYMBOL_GPL(do_xdp_generic);

static int netif_rx_internal(struct sk_buff *skb)
{
	int ret;

	net_timestamp_check(netdev_tstamp_prequeue, skb);

	trace_netif_rx(skb);

#ifdef CONFIG_RPS
	if (static_branch_unlikely(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu;

		preempt_disable();
		rcu_read_lock();

		cpu = get_rps_cpu(skb->dev, skb, &rflow);
		if (cpu < 0)
			cpu = smp_processor_id();

		ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);

		rcu_read_unlock();
		preempt_enable();
	} else
#endif
	{
		unsigned int qtail;

		ret = enqueue_to_backlog(skb, get_cpu(), &qtail);
		put_cpu();
	}
	return ret;
}

/**
 *	netif_rx	-	post buffer to the network code
 *	@skb: buffer to post
 *
 *	This function receives a packet from a device driver and queues it for
 *	the upper (protocol) levels to process.  It always succeeds. The buffer
 *	may be dropped during processing for congestion control or by the
 *	protocol layers.
 *
 *	return values:
 *	NET_RX_SUCCESS	(no congestion)
 *	NET_RX_DROP     (packet was dropped)
 *
 */

int netif_rx(struct sk_buff *skb)
{
	int ret;

	trace_netif_rx_entry(skb);

	ret = netif_rx_internal(skb);
	trace_netif_rx_exit(ret);

	return ret;
}
EXPORT_SYMBOL(netif_rx);

int netif_rx_ni(struct sk_buff *skb)
{
	int err;

	trace_netif_rx_ni_entry(skb);

	preempt_disable();
	err = netif_rx_internal(skb);
	if (local_softirq_pending())
		do_softirq();
	preempt_enable();
	trace_netif_rx_ni_exit(err);

	return err;
}
EXPORT_SYMBOL(netif_rx_ni);

int netif_rx_any_context(struct sk_buff *skb)
{
	/*
	 * If invoked from contexts which do not invoke bottom half
	 * processing either at return from interrupt or when softrqs are
	 * reenabled, use netif_rx_ni() which invokes bottomhalf processing
	 * directly.
	 */
	if (in_interrupt())
		return netif_rx(skb);
	else
		return netif_rx_ni(skb);
}
EXPORT_SYMBOL(netif_rx_any_context);

static __latent_entropy void net_tx_action(struct softirq_action *h)
{
	struct softnet_data *sd = this_cpu_ptr(&softnet_data);

	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_disable();
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_enable();

		while (clist) {
			struct sk_buff *skb = clist;

			clist = clist->next;

			WARN_ON(refcount_read(&skb->users));
			if (likely(get_kfree_skb_cb(skb)->reason == SKB_REASON_CONSUMED))
				trace_consume_skb(skb);
			else
				trace_kfree_skb(skb, net_tx_action);

			if (skb->fclone != SKB_FCLONE_UNAVAILABLE)
				__kfree_skb(skb);
			else
				__kfree_skb_defer(skb);
		}
	}

	if (sd->output_queue) {
		struct Qdisc *head;

		local_irq_disable();
		head = sd->output_queue;
		sd->output_queue = NULL;
		sd->output_queue_tailp = &sd->output_queue;
		local_irq_enable();

		rcu_read_lock();

		while (head) {
			struct Qdisc *q = head;
			spinlock_t *root_lock = NULL;

			head = head->next_sched;

			/* We need to make sure head->next_sched is read
			 * before clearing __QDISC_STATE_SCHED
			 */
			smp_mb__before_atomic();

			if (!(q->flags & TCQ_F_NOLOCK)) {
				root_lock = qdisc_lock(q);
				spin_lock(root_lock);
			} else if (unlikely(test_bit(__QDISC_STATE_DEACTIVATED,
						     &q->state))) {
				/* There is a synchronize_net() between
				 * STATE_DEACTIVATED flag being set and
				 * qdisc_reset()/some_qdisc_is_busy() in
				 * dev_deactivate(), so we can safely bail out
				 * early here to avoid data race between
				 * qdisc_deactivate() and some_qdisc_is_busy()
				 * for lockless qdisc.
				 */
				clear_bit(__QDISC_STATE_SCHED, &q->state);
				continue;
			}

			clear_bit(__QDISC_STATE_SCHED, &q->state);
			qdisc_run(q);
			if (root_lock)
				spin_unlock(root_lock);
		}

		rcu_read_unlock();
	}

	xfrm_dev_backlog(sd);
}

#if IS_ENABLED(CONFIG_BRIDGE) && IS_ENABLED(CONFIG_ATM_LANE)
/* This hook is defined here for ATM LANE */
int (*br_fdb_test_addr_hook)(struct net_device *dev,
			     unsigned char *addr) __read_mostly;
EXPORT_SYMBOL_GPL(br_fdb_test_addr_hook);
#endif

static inline struct sk_buff *
sch_handle_ingress(struct sk_buff *skb, struct packet_type **pt_prev, int *ret,
		   struct net_device *orig_dev, bool *another)
{
#ifdef CONFIG_NET_CLS_ACT
	struct mini_Qdisc *miniq = rcu_dereference_bh(skb->dev->miniq_ingress);
	struct tcf_result cl_res;

	/* If there's at least one ingress present somewhere (so
	 * we get here via enabled static key), remaining devices
	 * that are not configured with an ingress qdisc will bail
	 * out here.
	 */
	if (!miniq)
		return skb;

	if (*pt_prev) {
		*ret = deliver_skb(skb, *pt_prev, orig_dev);
		*pt_prev = NULL;
	}

	qdisc_skb_cb(skb)->pkt_len = skb->len;
	qdisc_skb_cb(skb)->mru = 0;
	qdisc_skb_cb(skb)->post_ct = false;
	skb->tc_at_ingress = 1;
	mini_qdisc_bstats_cpu_update(miniq, skb);

	switch (tcf_classify(skb, miniq->block, miniq->filter_list, &cl_res, false)) {
	case TC_ACT_OK:
	case TC_ACT_RECLASSIFY:
		skb->tc_index = TC_H_MIN(cl_res.classid);
		break;
	case TC_ACT_SHOT:
		mini_qdisc_qstats_cpu_drop(miniq);
		kfree_skb(skb);
		return NULL;
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
	case TC_ACT_TRAP:
		consume_skb(skb);
		return NULL;
	case TC_ACT_REDIRECT:
		/* skb_mac_header check was done by cls/act_bpf, so
		 * we can safely push the L2 header back before
		 * redirecting to another netdev
		 */
		__skb_push(skb, skb->mac_len);
		if (skb_do_redirect(skb) == -EAGAIN) {
			__skb_pull(skb, skb->mac_len);
			*another = true;
			break;
		}
		return NULL;
	case TC_ACT_CONSUMED:
		return NULL;
	default:
		break;
	}
#endif /* CONFIG_NET_CLS_ACT */
	return skb;
}

/**
 *	netdev_is_rx_handler_busy - check if receive handler is registered
 *	@dev: device to check
 *
 *	Check if a receive handler is already registered for a given device.
 *	Return true if there one.
 *
 *	The caller must hold the rtnl_mutex.
 */
bool netdev_is_rx_handler_busy(struct net_device *dev)
{
	ASSERT_RTNL();
	return dev && rtnl_dereference(dev->rx_handler);
}
EXPORT_SYMBOL_GPL(netdev_is_rx_handler_busy);

/**
 *	netdev_rx_handler_register - register receive handler
 *	@dev: device to register a handler for
 *	@rx_handler: receive handler to register
 *	@rx_handler_data: data pointer that is used by rx handler
 *
 *	Register a receive handler for a device. This handler will then be
 *	called from __netif_receive_skb. A negative errno code is returned
 *	on a failure.
 *
 *	The caller must hold the rtnl_mutex.
 *
 *	For a general description of rx_handler, see enum rx_handler_result.
 */
int netdev_rx_handler_register(struct net_device *dev,
			       rx_handler_func_t *rx_handler,
			       void *rx_handler_data)
{
	if (netdev_is_rx_handler_busy(dev))
		return -EBUSY;

	if (dev->priv_flags & IFF_NO_RX_HANDLER)
		return -EINVAL;

	/* Note: rx_handler_data must be set before rx_handler */
	rcu_assign_pointer(dev->rx_handler_data, rx_handler_data);
	rcu_assign_pointer(dev->rx_handler, rx_handler);

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_rx_handler_register);

/**
 *	netdev_rx_handler_unregister - unregister receive handler
 *	@dev: device to unregister a handler from
 *
 *	Unregister a receive handler from a device.
 *
 *	The caller must hold the rtnl_mutex.
 */
void netdev_rx_handler_unregister(struct net_device *dev)
{

	ASSERT_RTNL();
	RCU_INIT_POINTER(dev->rx_handler, NULL);
	/* a reader seeing a non NULL rx_handler in a rcu_read_lock()
	 * section has a guarantee to see a non NULL rx_handler_data
	 * as well.
	 */
	synchronize_net();
	RCU_INIT_POINTER(dev->rx_handler_data, NULL);
}
EXPORT_SYMBOL_GPL(netdev_rx_handler_unregister);

/*
 * Limit the use of PFMEMALLOC reserves to those protocols that implement
 * the special handling of PFMEMALLOC skbs.
 */
static bool skb_pfmemalloc_protocol(struct sk_buff *skb)
{
	switch (skb->protocol) {
	case htons(ETH_P_ARP):
	case htons(ETH_P_IP):
	case htons(ETH_P_IPV6):
	case htons(ETH_P_8021Q):
	case htons(ETH_P_8021AD):
		return true;
	default:
		return false;
	}
}

static inline int nf_ingress(struct sk_buff *skb, struct packet_type **pt_prev,
			     int *ret, struct net_device *orig_dev)
{
	if (nf_hook_ingress_active(skb)) {
		int ingress_retval;

		if (*pt_prev) {
			*ret = deliver_skb(skb, *pt_prev, orig_dev);
			*pt_prev = NULL;
		}

		rcu_read_lock();
		ingress_retval = nf_hook_ingress(skb);
		rcu_read_unlock();
		return ingress_retval;
	}
	return 0;
}

static int __netif_receive_skb_core(struct sk_buff **pskb, bool pfmemalloc,
				    struct packet_type **ppt_prev)
{
	struct packet_type *ptype, *pt_prev;
	rx_handler_func_t *rx_handler;
	struct sk_buff *skb = *pskb;
	struct net_device *orig_dev;
	bool deliver_exact = false;
	int ret = NET_RX_DROP;
	__be16 type;

	net_timestamp_check(!netdev_tstamp_prequeue, skb);

	trace_netif_receive_skb(skb);

	orig_dev = skb->dev;

	skb_reset_network_header(skb);
	if (!skb_transport_header_was_set(skb))
		skb_reset_transport_header(skb);
	skb_reset_mac_len(skb);

	pt_prev = NULL;

another_round:
	skb->skb_iif = skb->dev->ifindex;

	__this_cpu_inc(softnet_data.processed);

	if (static_branch_unlikely(&generic_xdp_needed_key)) {
		int ret2;

		migrate_disable();
		ret2 = do_xdp_generic(rcu_dereference(skb->dev->xdp_prog), skb);
		migrate_enable();

		if (ret2 != XDP_PASS) {
			ret = NET_RX_DROP;
			goto out;
		}
	}

	if (eth_type_vlan(skb->protocol)) {
		skb = skb_vlan_untag(skb);
		if (unlikely(!skb))
			goto out;
	}

	if (skb_skip_tc_classify(skb))
		goto skip_classify;

	if (pfmemalloc)
		goto skip_taps;

	list_for_each_entry_rcu(ptype, &ptype_all, list) {
		if (pt_prev)
			ret = deliver_skb(skb, pt_prev, orig_dev);
		pt_prev = ptype;
	}

	list_for_each_entry_rcu(ptype, &skb->dev->ptype_all, list) {
		if (pt_prev)
			ret = deliver_skb(skb, pt_prev, orig_dev);
		pt_prev = ptype;
	}

skip_taps:
#ifdef CONFIG_NET_INGRESS
	if (static_branch_unlikely(&ingress_needed_key)) {
		bool another = false;

		nf_skip_egress(skb, true);
		skb = sch_handle_ingress(skb, &pt_prev, &ret, orig_dev,
					 &another);
		if (another)
			goto another_round;
		if (!skb)
			goto out;

		nf_skip_egress(skb, false);
		if (nf_ingress(skb, &pt_prev, &ret, orig_dev) < 0)
			goto out;
	}
#endif
	skb_reset_redirect(skb);
skip_classify:
	if (pfmemalloc && !skb_pfmemalloc_protocol(skb))
		goto drop;

	if (skb_vlan_tag_present(skb)) {
		if (pt_prev) {
			ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = NULL;
		}
		if (vlan_do_receive(&skb))
			goto another_round;
		else if (unlikely(!skb))
			goto out;
	}

	rx_handler = rcu_dereference(skb->dev->rx_handler);
	if (rx_handler) {
		if (pt_prev) {
			ret = deliver_skb(skb, pt_prev, orig_dev);
			pt_prev = NULL;
		}
		switch (rx_handler(&skb)) {
		case RX_HANDLER_CONSUMED:
			ret = NET_RX_SUCCESS;
			goto out;
		case RX_HANDLER_ANOTHER:
			goto another_round;
		case RX_HANDLER_EXACT:
			deliver_exact = true;
			break;
		case RX_HANDLER_PASS:
			break;
		default:
			BUG();
		}
	}

	if (unlikely(skb_vlan_tag_present(skb)) && !netdev_uses_dsa(skb->dev)) {
check_vlan_id:
		if (skb_vlan_tag_get_id(skb)) {
			/* Vlan id is non 0 and vlan_do_receive() above couldn't
			 * find vlan device.
			 */
			skb->pkt_type = PACKET_OTHERHOST;
		} else if (eth_type_vlan(skb->protocol)) {
			/* Outer header is 802.1P with vlan 0, inner header is
			 * 802.1Q or 802.1AD and vlan_do_receive() above could
			 * not find vlan dev for vlan id 0.
			 */
			__vlan_hwaccel_clear_tag(skb);
			skb = skb_vlan_untag(skb);
			if (unlikely(!skb))
				goto out;
			if (vlan_do_receive(&skb))
				/* After stripping off 802.1P header with vlan 0
				 * vlan dev is found for inner header.
				 */
				goto another_round;
			else if (unlikely(!skb))
				goto out;
			else
				/* We have stripped outer 802.1P vlan 0 header.
				 * But could not find vlan dev.
				 * check again for vlan id to set OTHERHOST.
				 */
				goto check_vlan_id;
		}
		/* Note: we might in the future use prio bits
		 * and set skb->priority like in vlan_do_receive()
		 * For the time being, just ignore Priority Code Point
		 */
		__vlan_hwaccel_clear_tag(skb);
	}

	type = skb->protocol;

	/* deliver only exact match when indicated */
	if (likely(!deliver_exact)) {
		deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
				       &ptype_base[ntohs(type) &
						   PTYPE_HASH_MASK]);
	}

	deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
			       &orig_dev->ptype_specific);

	if (unlikely(skb->dev != orig_dev)) {
		deliver_ptype_list_skb(skb, &pt_prev, orig_dev, type,
				       &skb->dev->ptype_specific);
	}

	if (pt_prev) {
		if (unlikely(skb_orphan_frags_rx(skb, GFP_ATOMIC)))
			goto drop;
		*ppt_prev = pt_prev;
	} else {
drop:
		if (!deliver_exact)
			atomic_long_inc(&skb->dev->rx_dropped);
		else
			atomic_long_inc(&skb->dev->rx_nohandler);
		kfree_skb(skb);
		/* Jamal, now you will not able to escape explaining
		 * me how you were going to use this. :-)
		 */
		ret = NET_RX_DROP;
	}

out:
	/* The invariant here is that if *ppt_prev is not NULL
	 * then skb should also be non-NULL.
	 *
	 * Apparently *ppt_prev assignment above holds this invariant due to
	 * skb dereferencing near it.
	 */
	*pskb = skb;
	return ret;
}

static int __netif_receive_skb_one_core(struct sk_buff *skb, bool pfmemalloc)
{
	struct net_device *orig_dev = skb->dev;
	struct packet_type *pt_prev = NULL;
	int ret;

	ret = __netif_receive_skb_core(&skb, pfmemalloc, &pt_prev);
	if (pt_prev)
		ret = INDIRECT_CALL_INET(pt_prev->func, ipv6_rcv, ip_rcv, skb,
					 skb->dev, pt_prev, orig_dev);
	return ret;
}

/**
 *	netif_receive_skb_core - special purpose version of netif_receive_skb
 *	@skb: buffer to process
 *
 *	More direct receive version of netif_receive_skb().  It should
 *	only be used by callers that have a need to skip RPS and Generic XDP.
 *	Caller must also take care of handling if ``(page_is_)pfmemalloc``.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 *
 *	Return values (usually ignored):
 *	NET_RX_SUCCESS: no congestion
 *	NET_RX_DROP: packet was dropped
 */
int netif_receive_skb_core(struct sk_buff *skb)
{
	int ret;

	rcu_read_lock();
	ret = __netif_receive_skb_one_core(skb, false);
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(netif_receive_skb_core);

static inline void __netif_receive_skb_list_ptype(struct list_head *head,
						  struct packet_type *pt_prev,
						  struct net_device *orig_dev)
{
	struct sk_buff *skb, *next;

	if (!pt_prev)
		return;
	if (list_empty(head))
		return;
	if (pt_prev->list_func != NULL)
		INDIRECT_CALL_INET(pt_prev->list_func, ipv6_list_rcv,
				   ip_list_rcv, head, pt_prev, orig_dev);
	else
		list_for_each_entry_safe(skb, next, head, list) {
			skb_list_del_init(skb);
			pt_prev->func(skb, skb->dev, pt_prev, orig_dev);
		}
}

static void __netif_receive_skb_list_core(struct list_head *head, bool pfmemalloc)
{
	/* Fast-path assumptions:
	 * - There is no RX handler.
	 * - Only one packet_type matches.
	 * If either of these fails, we will end up doing some per-packet
	 * processing in-line, then handling the 'last ptype' for the whole
	 * sublist.  This can't cause out-of-order delivery to any single ptype,
	 * because the 'last ptype' must be constant across the sublist, and all
	 * other ptypes are handled per-packet.
	 */
	/* Current (common) ptype of sublist */
	struct packet_type *pt_curr = NULL;
	/* Current (common) orig_dev of sublist */
	struct net_device *od_curr = NULL;
	struct list_head sublist;
	struct sk_buff *skb, *next;

	INIT_LIST_HEAD(&sublist);
	list_for_each_entry_safe(skb, next, head, list) {
		struct net_device *orig_dev = skb->dev;
		struct packet_type *pt_prev = NULL;

		skb_list_del_init(skb);
		__netif_receive_skb_core(&skb, pfmemalloc, &pt_prev);
		if (!pt_prev)
			continue;
		if (pt_curr != pt_prev || od_curr != orig_dev) {
			/* dispatch old sublist */
			__netif_receive_skb_list_ptype(&sublist, pt_curr, od_curr);
			/* start new sublist */
			INIT_LIST_HEAD(&sublist);
			pt_curr = pt_prev;
			od_curr = orig_dev;
		}
		list_add_tail(&skb->list, &sublist);
	}

	/* dispatch final sublist */
	__netif_receive_skb_list_ptype(&sublist, pt_curr, od_curr);
}

static int __netif_receive_skb(struct sk_buff *skb)
{
	int ret;

	if (sk_memalloc_socks() && skb_pfmemalloc(skb)) {
		unsigned int noreclaim_flag;

		/*
		 * PFMEMALLOC skbs are special, they should
		 * - be delivered to SOCK_MEMALLOC sockets only
		 * - stay away from userspace
		 * - have bounded memory usage
		 *
		 * Use PF_MEMALLOC as this saves us from propagating the allocation
		 * context down to all allocation sites.
		 */
		noreclaim_flag = memalloc_noreclaim_save();
		ret = __netif_receive_skb_one_core(skb, true);
		memalloc_noreclaim_restore(noreclaim_flag);
	} else
		ret = __netif_receive_skb_one_core(skb, false);

	return ret;
}

static void __netif_receive_skb_list(struct list_head *head)
{
	unsigned long noreclaim_flag = 0;
	struct sk_buff *skb, *next;
	bool pfmemalloc = false; /* Is current sublist PF_MEMALLOC? */

	list_for_each_entry_safe(skb, next, head, list) {
		if ((sk_memalloc_socks() && skb_pfmemalloc(skb)) != pfmemalloc) {
			struct list_head sublist;

			/* Handle the previous sublist */
			list_cut_before(&sublist, head, &skb->list);
			if (!list_empty(&sublist))
				__netif_receive_skb_list_core(&sublist, pfmemalloc);
			pfmemalloc = !pfmemalloc;
			/* See comments in __netif_receive_skb */
			if (pfmemalloc)
				noreclaim_flag = memalloc_noreclaim_save();
			else
				memalloc_noreclaim_restore(noreclaim_flag);
		}
	}
	/* Handle the remaining sublist */
	if (!list_empty(head))
		__netif_receive_skb_list_core(head, pfmemalloc);
	/* Restore pflags */
	if (pfmemalloc)
		memalloc_noreclaim_restore(noreclaim_flag);
}

static int generic_xdp_install(struct net_device *dev, struct netdev_bpf *xdp)
{
	struct bpf_prog *old = rtnl_dereference(dev->xdp_prog);
	struct bpf_prog *new = xdp->prog;
	int ret = 0;

	switch (xdp->command) {
	case XDP_SETUP_PROG:
		rcu_assign_pointer(dev->xdp_prog, new);
		if (old)
			bpf_prog_put(old);

		if (old && !new) {
			static_branch_dec(&generic_xdp_needed_key);
		} else if (new && !old) {
			static_branch_inc(&generic_xdp_needed_key);
			dev_disable_lro(dev);
			dev_disable_gro_hw(dev);
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int netif_receive_skb_internal(struct sk_buff *skb)
{
	int ret;

	net_timestamp_check(netdev_tstamp_prequeue, skb);

	if (skb_defer_rx_timestamp(skb))
		return NET_RX_SUCCESS;

	rcu_read_lock();
#ifdef CONFIG_RPS
	if (static_branch_unlikely(&rps_needed)) {
		struct rps_dev_flow voidflow, *rflow = &voidflow;
		int cpu = get_rps_cpu(skb->dev, skb, &rflow);

		if (cpu >= 0) {
			ret = enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			rcu_read_unlock();
			return ret;
		}
	}
#endif
	ret = __netif_receive_skb(skb);
	rcu_read_unlock();
	return ret;
}

static void netif_receive_skb_list_internal(struct list_head *head)
{
	struct sk_buff *skb, *next;
	struct list_head sublist;

	INIT_LIST_HEAD(&sublist);
	list_for_each_entry_safe(skb, next, head, list) {
		net_timestamp_check(netdev_tstamp_prequeue, skb);
		skb_list_del_init(skb);
		if (!skb_defer_rx_timestamp(skb))
			list_add_tail(&skb->list, &sublist);
	}
	list_splice_init(&sublist, head);

	rcu_read_lock();
#ifdef CONFIG_RPS
	if (static_branch_unlikely(&rps_needed)) {
		list_for_each_entry_safe(skb, next, head, list) {
			struct rps_dev_flow voidflow, *rflow = &voidflow;
			int cpu = get_rps_cpu(skb->dev, skb, &rflow);

			if (cpu >= 0) {
				/* Will be handled, remove from list */
				skb_list_del_init(skb);
				enqueue_to_backlog(skb, cpu, &rflow->last_qtail);
			}
		}
	}
#endif
	__netif_receive_skb_list(head);
	rcu_read_unlock();
}

/**
 *	netif_receive_skb - process receive buffer from network
 *	@skb: buffer to process
 *
 *	netif_receive_skb() is the main receive data processing function.
 *	It always succeeds. The buffer may be dropped during processing
 *	for congestion control or by the protocol layers.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 *
 *	Return values (usually ignored):
 *	NET_RX_SUCCESS: no congestion
 *	NET_RX_DROP: packet was dropped
 */
int netif_receive_skb(struct sk_buff *skb)
{
	int ret;

	trace_netif_receive_skb_entry(skb);

	ret = netif_receive_skb_internal(skb);
	trace_netif_receive_skb_exit(ret);

	return ret;
}
EXPORT_SYMBOL(netif_receive_skb);

/**
 *	netif_receive_skb_list - process many receive buffers from network
 *	@head: list of skbs to process.
 *
 *	Since return value of netif_receive_skb() is normally ignored, and
 *	wouldn't be meaningful for a list, this function returns void.
 *
 *	This function may only be called from softirq context and interrupts
 *	should be enabled.
 */
void netif_receive_skb_list(struct list_head *head)
{
	struct sk_buff *skb;

	if (list_empty(head))
		return;
	if (trace_netif_receive_skb_list_entry_enabled()) {
		list_for_each_entry(skb, head, list)
			trace_netif_receive_skb_list_entry(skb);
	}
	netif_receive_skb_list_internal(head);
	trace_netif_receive_skb_list_exit(0);
}
EXPORT_SYMBOL(netif_receive_skb_list);

static DEFINE_PER_CPU(struct work_struct, flush_works);

/* Network device is going away, flush any packets still pending */
static void flush_backlog(struct work_struct *work)
{
	struct sk_buff *skb, *tmp;
	struct softnet_data *sd;

	local_bh_disable();
	sd = this_cpu_ptr(&softnet_data);

	local_irq_disable();
	rps_lock(sd);
	skb_queue_walk_safe(&sd->input_pkt_queue, skb, tmp) {
		if (skb->dev->reg_state == NETREG_UNREGISTERING) {
			__skb_unlink(skb, &sd->input_pkt_queue);
			dev_kfree_skb_irq(skb);
			input_queue_head_incr(sd);
		}
	}
	rps_unlock(sd);
	local_irq_enable();

	skb_queue_walk_safe(&sd->process_queue, skb, tmp) {
		if (skb->dev->reg_state == NETREG_UNREGISTERING) {
			__skb_unlink(skb, &sd->process_queue);
			kfree_skb(skb);
			input_queue_head_incr(sd);
		}
	}
	local_bh_enable();
}

static bool flush_required(int cpu)
{
#if IS_ENABLED(CONFIG_RPS)
	struct softnet_data *sd = &per_cpu(softnet_data, cpu);
	bool do_flush;

	local_irq_disable();
	rps_lock(sd);

	/* as insertion into process_queue happens with the rps lock held,
	 * process_queue access may race only with dequeue
	 */
	do_flush = !skb_queue_empty(&sd->input_pkt_queue) ||
		   !skb_queue_empty_lockless(&sd->process_queue);
	rps_unlock(sd);
	local_irq_enable();

	return do_flush;
#endif
	/* without RPS we can't safely check input_pkt_queue: during a
	 * concurrent remote skb_queue_splice() we can detect as empty both
	 * input_pkt_queue and process_queue even if the latter could end-up
	 * containing a lot of packets.
	 */
	return true;
}

static void flush_all_backlogs(void)
{
	static cpumask_t flush_cpus;
	unsigned int cpu;

	/* since we are under rtnl lock protection we can use static data
	 * for the cpumask and avoid allocating on stack the possibly
	 * large mask
	 */
	ASSERT_RTNL();

	cpus_read_lock();

	cpumask_clear(&flush_cpus);
	for_each_online_cpu(cpu) {
		if (flush_required(cpu)) {
			queue_work_on(cpu, system_highpri_wq,
				      per_cpu_ptr(&flush_works, cpu));
			cpumask_set_cpu(cpu, &flush_cpus);
		}
	}

	/* we can have in flight packet[s] on the cpus we are not flushing,
	 * synchronize_net() in unregister_netdevice_many() will take care of
	 * them
	 */
	for_each_cpu(cpu, &flush_cpus)
		flush_work(per_cpu_ptr(&flush_works, cpu));

	cpus_read_unlock();
}

/* Pass the currently batched GRO_NORMAL SKBs up to the stack. */
static void gro_normal_list(struct napi_struct *napi)
{
	if (!napi->rx_count)
		return;
	netif_receive_skb_list_internal(&napi->rx_list);
	INIT_LIST_HEAD(&napi->rx_list);
	napi->rx_count = 0;
}

/* Queue one GRO_NORMAL SKB up for list processing. If batch size exceeded,
 * pass the whole batch up to the stack.
 */
static void gro_normal_one(struct napi_struct *napi, struct sk_buff *skb, int segs)
{
	list_add_tail(&skb->list, &napi->rx_list);
	napi->rx_count += segs;
	if (napi->rx_count >= gro_normal_batch)
		gro_normal_list(napi);
}

static void napi_gro_complete(struct napi_struct *napi, struct sk_buff *skb)
{
	struct packet_offload *ptype;
	__be16 type = skb->protocol;
	struct list_head *head = &offload_base;
	int err = -ENOENT;

	BUILD_BUG_ON(sizeof(struct napi_gro_cb) > sizeof(skb->cb));

	if (NAPI_GRO_CB(skb)->count == 1) {
		skb_shinfo(skb)->gso_size = 0;
		goto out;
	}

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_complete)
			continue;

		err = INDIRECT_CALL_INET(ptype->callbacks.gro_complete,
					 ipv6_gro_complete, inet_gro_complete,
					 skb, 0);
		break;
	}
	rcu_read_unlock();

	if (err) {
		WARN_ON(&ptype->list == head);
		kfree_skb(skb);
		return;
	}

out:
	gro_normal_one(napi, skb, NAPI_GRO_CB(skb)->count);
}

static void __napi_gro_flush_chain(struct napi_struct *napi, u32 index,
				   bool flush_old)
{
	struct list_head *head = &napi->gro_hash[index].list;
	struct sk_buff *skb, *p;

	list_for_each_entry_safe_reverse(skb, p, head, list) {
		if (flush_old && NAPI_GRO_CB(skb)->age == jiffies)
			return;
		skb_list_del_init(skb);
		napi_gro_complete(napi, skb);
		napi->gro_hash[index].count--;
	}

	if (!napi->gro_hash[index].count)
		__clear_bit(index, &napi->gro_bitmask);
}

/* napi->gro_hash[].list contains packets ordered by age.
 * youngest packets at the head of it.
 * Complete skbs in reverse order to reduce latencies.
 */
void napi_gro_flush(struct napi_struct *napi, bool flush_old)
{
	unsigned long bitmask = napi->gro_bitmask;
	unsigned int i, base = ~0U;

	while ((i = ffs(bitmask)) != 0) {
		bitmask >>= i;
		base += i;
		__napi_gro_flush_chain(napi, base, flush_old);
	}
}
EXPORT_SYMBOL(napi_gro_flush);

static void gro_list_prepare(const struct list_head *head,
			     const struct sk_buff *skb)
{
	unsigned int maclen = skb->dev->hard_header_len;
	u32 hash = skb_get_hash_raw(skb);
	struct sk_buff *p;

	list_for_each_entry(p, head, list) {
		unsigned long diffs;

		NAPI_GRO_CB(p)->flush = 0;

		if (hash != skb_get_hash_raw(p)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}

		diffs = (unsigned long)p->dev ^ (unsigned long)skb->dev;
		diffs |= skb_vlan_tag_present(p) ^ skb_vlan_tag_present(skb);
		if (skb_vlan_tag_present(p))
			diffs |= skb_vlan_tag_get(p) ^ skb_vlan_tag_get(skb);
		diffs |= skb_metadata_differs(p, skb);
		if (maclen == ETH_HLEN)
			diffs |= compare_ether_header(skb_mac_header(p),
						      skb_mac_header(skb));
		else if (!diffs)
			diffs = memcmp(skb_mac_header(p),
				       skb_mac_header(skb),
				       maclen);

		/* in most common scenarions 'slow_gro' is 0
		 * otherwise we are already on some slower paths
		 * either skip all the infrequent tests altogether or
		 * avoid trying too hard to skip each of them individually
		 */
		if (!diffs && unlikely(skb->slow_gro | p->slow_gro)) {
#if IS_ENABLED(CONFIG_SKB_EXTENSIONS) && IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
			struct tc_skb_ext *skb_ext;
			struct tc_skb_ext *p_ext;
#endif

			diffs |= p->sk != skb->sk;
			diffs |= skb_metadata_dst_cmp(p, skb);
			diffs |= skb_get_nfct(p) ^ skb_get_nfct(skb);

#if IS_ENABLED(CONFIG_SKB_EXTENSIONS) && IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
			skb_ext = skb_ext_find(skb, TC_SKB_EXT);
			p_ext = skb_ext_find(p, TC_SKB_EXT);

			diffs |= (!!p_ext) ^ (!!skb_ext);
			if (!diffs && unlikely(skb_ext))
				diffs |= p_ext->chain ^ skb_ext->chain;
#endif
		}

		NAPI_GRO_CB(p)->same_flow = !diffs;
	}
}

static inline void skb_gro_reset_offset(struct sk_buff *skb, u32 nhoff)
{
	const struct skb_shared_info *pinfo = skb_shinfo(skb);
	const skb_frag_t *frag0 = &pinfo->frags[0];

	NAPI_GRO_CB(skb)->data_offset = 0;
	NAPI_GRO_CB(skb)->frag0 = NULL;
	NAPI_GRO_CB(skb)->frag0_len = 0;

	if (!skb_headlen(skb) && pinfo->nr_frags &&
	    !PageHighMem(skb_frag_page(frag0)) &&
	    (!NET_IP_ALIGN || !((skb_frag_off(frag0) + nhoff) & 3))) {
		NAPI_GRO_CB(skb)->frag0 = skb_frag_address(frag0);
		NAPI_GRO_CB(skb)->frag0_len = min_t(unsigned int,
						    skb_frag_size(frag0),
						    skb->end - skb->tail);
	}
}

static void gro_pull_from_frag0(struct sk_buff *skb, int grow)
{
	struct skb_shared_info *pinfo = skb_shinfo(skb);

	BUG_ON(skb->end - skb->tail < grow);

	memcpy(skb_tail_pointer(skb), NAPI_GRO_CB(skb)->frag0, grow);

	skb->data_len -= grow;
	skb->tail += grow;

	skb_frag_off_add(&pinfo->frags[0], grow);
	skb_frag_size_sub(&pinfo->frags[0], grow);

	if (unlikely(!skb_frag_size(&pinfo->frags[0]))) {
		skb_frag_unref(skb, 0);
		memmove(pinfo->frags, pinfo->frags + 1,
			--pinfo->nr_frags * sizeof(pinfo->frags[0]));
	}
}

static void gro_flush_oldest(struct napi_struct *napi, struct list_head *head)
{
	struct sk_buff *oldest;

	oldest = list_last_entry(head, struct sk_buff, list);

	/* We are called with head length >= MAX_GRO_SKBS, so this is
	 * impossible.
	 */
	if (WARN_ON_ONCE(!oldest))
		return;

	/* Do not adjust napi->gro_hash[].count, caller is adding a new
	 * SKB to the chain.
	 */
	skb_list_del_init(oldest);
	napi_gro_complete(napi, oldest);
}

static enum gro_result dev_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	u32 bucket = skb_get_hash_raw(skb) & (GRO_HASH_BUCKETS - 1);
	struct gro_list *gro_list = &napi->gro_hash[bucket];
	struct list_head *head = &offload_base;
	struct packet_offload *ptype;
	__be16 type = skb->protocol;
	struct sk_buff *pp = NULL;
	enum gro_result ret;
	int same_flow;
	int grow;

	if (netif_elide_gro(skb->dev))
		goto normal;

	gro_list_prepare(&gro_list->list, skb);

	rcu_read_lock();
	list_for_each_entry_rcu(ptype, head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_receive)
			continue;

		skb_set_network_header(skb, skb_gro_offset(skb));
		skb_reset_mac_len(skb);
		NAPI_GRO_CB(skb)->same_flow = 0;
		NAPI_GRO_CB(skb)->flush = skb_is_gso(skb) || skb_has_frag_list(skb);
		NAPI_GRO_CB(skb)->free = 0;
		NAPI_GRO_CB(skb)->encap_mark = 0;
		NAPI_GRO_CB(skb)->recursion_counter = 0;
		NAPI_GRO_CB(skb)->is_fou = 0;
		NAPI_GRO_CB(skb)->is_atomic = 1;
		NAPI_GRO_CB(skb)->gro_remcsum_start = 0;

		/* Setup for GRO checksum validation */
		switch (skb->ip_summed) {
		case CHECKSUM_COMPLETE:
			NAPI_GRO_CB(skb)->csum = skb->csum;
			NAPI_GRO_CB(skb)->csum_valid = 1;
			NAPI_GRO_CB(skb)->csum_cnt = 0;
			break;
		case CHECKSUM_UNNECESSARY:
			NAPI_GRO_CB(skb)->csum_cnt = skb->csum_level + 1;
			NAPI_GRO_CB(skb)->csum_valid = 0;
			break;
		default:
			NAPI_GRO_CB(skb)->csum_cnt = 0;
			NAPI_GRO_CB(skb)->csum_valid = 0;
		}

		pp = INDIRECT_CALL_INET(ptype->callbacks.gro_receive,
					ipv6_gro_receive, inet_gro_receive,
					&gro_list->list, skb);
		break;
	}
	rcu_read_unlock();

	if (&ptype->list == head)
		goto normal;

	if (PTR_ERR(pp) == -EINPROGRESS) {
		ret = GRO_CONSUMED;
		goto ok;
	}

	same_flow = NAPI_GRO_CB(skb)->same_flow;
	ret = NAPI_GRO_CB(skb)->free ? GRO_MERGED_FREE : GRO_MERGED;

	if (pp) {
		skb_list_del_init(pp);
		napi_gro_complete(napi, pp);
		gro_list->count--;
	}

	if (same_flow)
		goto ok;

	if (NAPI_GRO_CB(skb)->flush)
		goto normal;

	if (unlikely(gro_list->count >= MAX_GRO_SKBS))
		gro_flush_oldest(napi, &gro_list->list);
	else
		gro_list->count++;

	NAPI_GRO_CB(skb)->count = 1;
	NAPI_GRO_CB(skb)->age = jiffies;
	NAPI_GRO_CB(skb)->last = skb;
	skb_shinfo(skb)->gso_size = skb_gro_len(skb);
	list_add(&skb->list, &gro_list->list);
	ret = GRO_HELD;

pull:
	grow = skb_gro_offset(skb) - skb_headlen(skb);
	if (grow > 0)
		gro_pull_from_frag0(skb, grow);
ok:
	if (gro_list->count) {
		if (!test_bit(bucket, &napi->gro_bitmask))
			__set_bit(bucket, &napi->gro_bitmask);
	} else if (test_bit(bucket, &napi->gro_bitmask)) {
		__clear_bit(bucket, &napi->gro_bitmask);
	}

	return ret;

normal:
	ret = GRO_NORMAL;
	goto pull;
}

struct packet_offload *gro_find_receive_by_type(__be16 type)
{
	struct list_head *offload_head = &offload_base;
	struct packet_offload *ptype;

	list_for_each_entry_rcu(ptype, offload_head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_receive)
			continue;
		return ptype;
	}
	return NULL;
}
EXPORT_SYMBOL(gro_find_receive_by_type);

struct packet_offload *gro_find_complete_by_type(__be16 type)
{
	struct list_head *offload_head = &offload_base;
	struct packet_offload *ptype;

	list_for_each_entry_rcu(ptype, offload_head, list) {
		if (ptype->type != type || !ptype->callbacks.gro_complete)
			continue;
		return ptype;
	}
	return NULL;
}
EXPORT_SYMBOL(gro_find_complete_by_type);

static gro_result_t napi_skb_finish(struct napi_struct *napi,
				    struct sk_buff *skb,
				    gro_result_t ret)
{
	switch (ret) {
	case GRO_NORMAL:
		gro_normal_one(napi, skb, 1);
		break;

	case GRO_MERGED_FREE:
		if (NAPI_GRO_CB(skb)->free == NAPI_GRO_FREE_STOLEN_HEAD)
			napi_skb_free_stolen_head(skb);
		else if (skb->fclone != SKB_FCLONE_UNAVAILABLE)
			__kfree_skb(skb);
		else
			__kfree_skb_defer(skb);
		break;

	case GRO_HELD:
	case GRO_MERGED:
	case GRO_CONSUMED:
		break;
	}

	return ret;
}

gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb)
{
	gro_result_t ret;

	skb_mark_napi_id(skb, napi);
	trace_napi_gro_receive_entry(skb);

	skb_gro_reset_offset(skb, 0);

	ret = napi_skb_finish(napi, skb, dev_gro_receive(napi, skb));
	trace_napi_gro_receive_exit(ret);

	return ret;
}
EXPORT_SYMBOL(napi_gro_receive);

static void napi_reuse_skb(struct napi_struct *napi, struct sk_buff *skb)
{
	if (unlikely(skb->pfmemalloc)) {
		consume_skb(skb);
		return;
	}
	__skb_pull(skb, skb_headlen(skb));
	/* restore the reserve we had after netdev_alloc_skb_ip_align() */
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN - skb_headroom(skb));
	__vlan_hwaccel_clear_tag(skb);
	skb->dev = napi->dev;
	skb->skb_iif = 0;

	/* eth_type_trans() assumes pkt_type is PACKET_HOST */
	skb->pkt_type = PACKET_HOST;

	skb->encapsulation = 0;
	skb_shinfo(skb)->gso_type = 0;
	skb->truesize = SKB_TRUESIZE(skb_end_offset(skb));
	if (unlikely(skb->slow_gro)) {
		skb_orphan(skb);
		skb_ext_reset(skb);
		nf_reset_ct(skb);
		skb->slow_gro = 0;
	}

	napi->skb = skb;
}

struct sk_buff *napi_get_frags(struct napi_struct *napi)
{
	struct sk_buff *skb = napi->skb;

	if (!skb) {
		skb = napi_alloc_skb(napi, GRO_MAX_HEAD);
		if (skb) {
			napi->skb = skb;
			skb_mark_napi_id(skb, napi);
		}
	}
	return skb;
}
EXPORT_SYMBOL(napi_get_frags);

static gro_result_t napi_frags_finish(struct napi_struct *napi,
				      struct sk_buff *skb,
				      gro_result_t ret)
{
	switch (ret) {
	case GRO_NORMAL:
	case GRO_HELD:
		__skb_push(skb, ETH_HLEN);
		skb->protocol = eth_type_trans(skb, skb->dev);
		if (ret == GRO_NORMAL)
			gro_normal_one(napi, skb, 1);
		break;

	case GRO_MERGED_FREE:
		if (NAPI_GRO_CB(skb)->free == NAPI_GRO_FREE_STOLEN_HEAD)
			napi_skb_free_stolen_head(skb);
		else
			napi_reuse_skb(napi, skb);
		break;

	case GRO_MERGED:
	case GRO_CONSUMED:
		break;
	}

	return ret;
}

/* Upper GRO stack assumes network header starts at gro_offset=0
 * Drivers could call both napi_gro_frags() and napi_gro_receive()
 * We copy ethernet header into skb->data to have a common layout.
 */
static struct sk_buff *napi_frags_skb(struct napi_struct *napi)
{
	struct sk_buff *skb = napi->skb;
	const struct ethhdr *eth;
	unsigned int hlen = sizeof(*eth);

	napi->skb = NULL;

	skb_reset_mac_header(skb);
	skb_gro_reset_offset(skb, hlen);

	if (unlikely(skb_gro_header_hard(skb, hlen))) {
		eth = skb_gro_header_slow(skb, hlen, 0);
		if (unlikely(!eth)) {
			net_warn_ratelimited("%s: dropping impossible skb from %s\n",
					     __func__, napi->dev->name);
			napi_reuse_skb(napi, skb);
			return NULL;
		}
	} else {
		eth = (const struct ethhdr *)skb->data;
		gro_pull_from_frag0(skb, hlen);
		NAPI_GRO_CB(skb)->frag0 += hlen;
		NAPI_GRO_CB(skb)->frag0_len -= hlen;
	}
	__skb_pull(skb, hlen);

	/*
	 * This works because the only protocols we care about don't require
	 * special handling.
	 * We'll fix it up properly in napi_frags_finish()
	 */
	skb->protocol = eth->h_proto;

	return skb;
}

gro_result_t napi_gro_frags(struct napi_struct *napi)
{
	gro_result_t ret;
	struct sk_buff *skb = napi_frags_skb(napi);

	trace_napi_gro_frags_entry(skb);

	ret = napi_frags_finish(napi, skb, dev_gro_receive(napi, skb));
	trace_napi_gro_frags_exit(ret);

	return ret;
}
EXPORT_SYMBOL(napi_gro_frags);

/* Compute the checksum from gro_offset and return the folded value
 * after adding in any pseudo checksum.
 */
__sum16 __skb_gro_checksum_complete(struct sk_buff *skb)
{
	__wsum wsum;
	__sum16 sum;

	wsum = skb_checksum(skb, skb_gro_offset(skb), skb_gro_len(skb), 0);

	/* NAPI_GRO_CB(skb)->csum holds pseudo checksum */
	sum = csum_fold(csum_add(NAPI_GRO_CB(skb)->csum, wsum));
	/* See comments in __skb_checksum_complete(). */
	if (likely(!sum)) {
		if (unlikely(skb->ip_summed == CHECKSUM_COMPLETE) &&
		    !skb->csum_complete_sw)
			netdev_rx_csum_fault(skb->dev, skb);
	}

	NAPI_GRO_CB(skb)->csum = wsum;
	NAPI_GRO_CB(skb)->csum_valid = 1;

	return sum;
}
EXPORT_SYMBOL(__skb_gro_checksum_complete);

static void net_rps_send_ipi(struct softnet_data *remsd)
{
#ifdef CONFIG_RPS
	while (remsd) {
		struct softnet_data *next = remsd->rps_ipi_next;

		if (cpu_online(remsd->cpu))
			smp_call_function_single_async(remsd->cpu, &remsd->csd);
		remsd = next;
	}
#endif
}

/*
 * net_rps_action_and_irq_enable sends any pending IPI's for rps.
 * Note: called with local irq disabled, but exits with local irq enabled.
 */
static void net_rps_action_and_irq_enable(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	struct softnet_data *remsd = sd->rps_ipi_list;

	if (remsd) {
		sd->rps_ipi_list = NULL;

		local_irq_enable();

		/* Send pending IPI's to kick RPS processing on remote cpus. */
		net_rps_send_ipi(remsd);
	} else
#endif
		local_irq_enable();
}

static bool sd_has_rps_ipi_waiting(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	return sd->rps_ipi_list != NULL;
#else
	return false;
#endif
}

static int process_backlog(struct napi_struct *napi, int quota)
{
	struct softnet_data *sd = container_of(napi, struct softnet_data, backlog);
	bool again = true;
	int work = 0;

	/* Check if we have pending ipi, its better to send them now,
	 * not waiting net_rx_action() end.
	 */
	if (sd_has_rps_ipi_waiting(sd)) {
		local_irq_disable();
		net_rps_action_and_irq_enable(sd);
	}

	napi->weight = dev_rx_weight;
	while (again) {
		struct sk_buff *skb;

		while ((skb = __skb_dequeue(&sd->process_queue))) {
			rcu_read_lock();
			__netif_receive_skb(skb);
			rcu_read_unlock();
			input_queue_head_incr(sd);
			if (++work >= quota)
				return work;

		}

		local_irq_disable();
		rps_lock(sd);
		if (skb_queue_empty(&sd->input_pkt_queue)) {
			/*
			 * Inline a custom version of __napi_complete().
			 * only current cpu owns and manipulates this napi,
			 * and NAPI_STATE_SCHED is the only possible flag set
			 * on backlog.
			 * We can use a plain write instead of clear_bit(),
			 * and we dont need an smp_mb() memory barrier.
			 */
			napi->state = 0;
			again = false;
		} else {
			skb_queue_splice_tail_init(&sd->input_pkt_queue,
						   &sd->process_queue);
		}
		rps_unlock(sd);
		local_irq_enable();
	}

	return work;
}

/**
 * __napi_schedule - schedule for receive
 * @n: entry to schedule
 *
 * The entry's receive function will be scheduled to run.
 * Consider using __napi_schedule_irqoff() if hard irqs are masked.
 */
void __napi_schedule(struct napi_struct *n)
{
	unsigned long flags;

	local_irq_save(flags);
	____napi_schedule(this_cpu_ptr(&softnet_data), n);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(__napi_schedule);

/**
 *	napi_schedule_prep - check if napi can be scheduled
 *	@n: napi context
 *
 * Test if NAPI routine is already running, and if not mark
 * it as running.  This is used as a condition variable to
 * insure only one NAPI poll instance runs.  We also make
 * sure there is no pending NAPI disable.
 */
bool napi_schedule_prep(struct napi_struct *n)
{
	unsigned long val, new;

	do {
		val = READ_ONCE(n->state);
		if (unlikely(val & NAPIF_STATE_DISABLE))
			return false;
		new = val | NAPIF_STATE_SCHED;

		/* Sets STATE_MISSED bit if STATE_SCHED was already set
		 * This was suggested by Alexander Duyck, as compiler
		 * emits better code than :
		 * if (val & NAPIF_STATE_SCHED)
		 *     new |= NAPIF_STATE_MISSED;
		 */
		new |= (val & NAPIF_STATE_SCHED) / NAPIF_STATE_SCHED *
						   NAPIF_STATE_MISSED;
	} while (cmpxchg(&n->state, val, new) != val);

	return !(val & NAPIF_STATE_SCHED);
}
EXPORT_SYMBOL(napi_schedule_prep);

/**
 * __napi_schedule_irqoff - schedule for receive
 * @n: entry to schedule
 *
 * Variant of __napi_schedule() assuming hard irqs are masked.
 *
 * On PREEMPT_RT enabled kernels this maps to __napi_schedule()
 * because the interrupt disabled assumption might not be true
 * due to force-threaded interrupts and spinlock substitution.
 */
void __napi_schedule_irqoff(struct napi_struct *n)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		____napi_schedule(this_cpu_ptr(&softnet_data), n);
	else
		__napi_schedule(n);
}
EXPORT_SYMBOL(__napi_schedule_irqoff);

bool napi_complete_done(struct napi_struct *n, int work_done)
{
	unsigned long flags, val, new, timeout = 0;
	bool ret = true;

	/*
	 * 1) Don't let napi dequeue from the cpu poll list
	 *    just in case its running on a different cpu.
	 * 2) If we are busy polling, do nothing here, we have
	 *    the guarantee we will be called later.
	 */
	if (unlikely(n->state & (NAPIF_STATE_NPSVC |
				 NAPIF_STATE_IN_BUSY_POLL)))
		return false;

	if (work_done) {
		if (n->gro_bitmask)
			timeout = READ_ONCE(n->dev->gro_flush_timeout);
		n->defer_hard_irqs_count = READ_ONCE(n->dev->napi_defer_hard_irqs);
	}
	if (n->defer_hard_irqs_count > 0) {
		n->defer_hard_irqs_count--;
		timeout = READ_ONCE(n->dev->gro_flush_timeout);
		if (timeout)
			ret = false;
	}
	if (n->gro_bitmask) {
		/* When the NAPI instance uses a timeout and keeps postponing
		 * it, we need to bound somehow the time packets are kept in
		 * the GRO layer
		 */
		napi_gro_flush(n, !!timeout);
	}

	gro_normal_list(n);

	if (unlikely(!list_empty(&n->poll_list))) {
		/* If n->poll_list is not empty, we need to mask irqs */
		local_irq_save(flags);
		list_del_init(&n->poll_list);
		local_irq_restore(flags);
	}

	do {
		val = READ_ONCE(n->state);

		WARN_ON_ONCE(!(val & NAPIF_STATE_SCHED));

		new = val & ~(NAPIF_STATE_MISSED | NAPIF_STATE_SCHED |
			      NAPIF_STATE_SCHED_THREADED |
			      NAPIF_STATE_PREFER_BUSY_POLL);

		/* If STATE_MISSED was set, leave STATE_SCHED set,
		 * because we will call napi->poll() one more time.
		 * This C code was suggested by Alexander Duyck to help gcc.
		 */
		new |= (val & NAPIF_STATE_MISSED) / NAPIF_STATE_MISSED *
						    NAPIF_STATE_SCHED;
	} while (cmpxchg(&n->state, val, new) != val);

	if (unlikely(val & NAPIF_STATE_MISSED)) {
		__napi_schedule(n);
		return false;
	}

	if (timeout)
		hrtimer_start(&n->timer, ns_to_ktime(timeout),
			      HRTIMER_MODE_REL_PINNED);
	return ret;
}
EXPORT_SYMBOL(napi_complete_done);

/* must be called under rcu_read_lock(), as we dont take a reference */
static struct napi_struct *napi_by_id(unsigned int napi_id)
{
	unsigned int hash = napi_id % HASH_SIZE(napi_hash);
	struct napi_struct *napi;

	hlist_for_each_entry_rcu(napi, &napi_hash[hash], napi_hash_node)
		if (napi->napi_id == napi_id)
			return napi;

	return NULL;
}

#if defined(CONFIG_NET_RX_BUSY_POLL)

static void __busy_poll_stop(struct napi_struct *napi, bool skip_schedule)
{
	if (!skip_schedule) {
		gro_normal_list(napi);
		__napi_schedule(napi);
		return;
	}

	if (napi->gro_bitmask) {
		/* flush too old packets
		 * If HZ < 1000, flush all packets.
		 */
		napi_gro_flush(napi, HZ >= 1000);
	}

	gro_normal_list(napi);
	clear_bit(NAPI_STATE_SCHED, &napi->state);
}

static void busy_poll_stop(struct napi_struct *napi, void *have_poll_lock, bool prefer_busy_poll,
			   u16 budget)
{
	bool skip_schedule = false;
	unsigned long timeout;
	int rc;

	/* Busy polling means there is a high chance device driver hard irq
	 * could not grab NAPI_STATE_SCHED, and that NAPI_STATE_MISSED was
	 * set in napi_schedule_prep().
	 * Since we are about to call napi->poll() once more, we can safely
	 * clear NAPI_STATE_MISSED.
	 *
	 * Note: x86 could use a single "lock and ..." instruction
	 * to perform these two clear_bit()
	 */
	clear_bit(NAPI_STATE_MISSED, &napi->state);
	clear_bit(NAPI_STATE_IN_BUSY_POLL, &napi->state);

	local_bh_disable();

	if (prefer_busy_poll) {
		napi->defer_hard_irqs_count = READ_ONCE(napi->dev->napi_defer_hard_irqs);
		timeout = READ_ONCE(napi->dev->gro_flush_timeout);
		if (napi->defer_hard_irqs_count && timeout) {
			hrtimer_start(&napi->timer, ns_to_ktime(timeout), HRTIMER_MODE_REL_PINNED);
			skip_schedule = true;
		}
	}

	/* All we really want here is to re-enable device interrupts.
	 * Ideally, a new ndo_busy_poll_stop() could avoid another round.
	 */
	rc = napi->poll(napi, budget);
	/* We can't gro_normal_list() here, because napi->poll() might have
	 * rearmed the napi (napi_complete_done()) in which case it could
	 * already be running on another CPU.
	 */
	trace_napi_poll(napi, rc, budget);
	netpoll_poll_unlock(have_poll_lock);
	if (rc == budget)
		__busy_poll_stop(napi, skip_schedule);
	local_bh_enable();
}

void napi_busy_loop(unsigned int napi_id,
		    bool (*loop_end)(void *, unsigned long),
		    void *loop_end_arg, bool prefer_busy_poll, u16 budget)
{
	unsigned long start_time = loop_end ? busy_loop_current_time() : 0;
	int (*napi_poll)(struct napi_struct *napi, int budget);
	void *have_poll_lock = NULL;
	struct napi_struct *napi;

restart:
	napi_poll = NULL;

	rcu_read_lock();

	napi = napi_by_id(napi_id);
	if (!napi)
		goto out;

	preempt_disable();
	for (;;) {
		int work = 0;

		local_bh_disable();
		if (!napi_poll) {
			unsigned long val = READ_ONCE(napi->state);

			/* If multiple threads are competing for this napi,
			 * we avoid dirtying napi->state as much as we can.
			 */
			if (val & (NAPIF_STATE_DISABLE | NAPIF_STATE_SCHED |
				   NAPIF_STATE_IN_BUSY_POLL)) {
				if (prefer_busy_poll)
					set_bit(NAPI_STATE_PREFER_BUSY_POLL, &napi->state);
				goto count;
			}
			if (cmpxchg(&napi->state, val,
				    val | NAPIF_STATE_IN_BUSY_POLL |
					  NAPIF_STATE_SCHED) != val) {
				if (prefer_busy_poll)
					set_bit(NAPI_STATE_PREFER_BUSY_POLL, &napi->state);
				goto count;
			}
			have_poll_lock = netpoll_poll_lock(napi);
			napi_poll = napi->poll;
		}
		work = napi_poll(napi, budget);
		trace_napi_poll(napi, work, budget);
		gro_normal_list(napi);
count:
		if (work > 0)
			__NET_ADD_STATS(dev_net(napi->dev),
					LINUX_MIB_BUSYPOLLRXPACKETS, work);
		local_bh_enable();

		if (!loop_end || loop_end(loop_end_arg, start_time))
			break;

		if (unlikely(need_resched())) {
			if (napi_poll)
				busy_poll_stop(napi, have_poll_lock, prefer_busy_poll, budget);
			preempt_enable();
			rcu_read_unlock();
			cond_resched();
			if (loop_end(loop_end_arg, start_time))
				return;
			goto restart;
		}
		cpu_relax();
	}
	if (napi_poll)
		busy_poll_stop(napi, have_poll_lock, prefer_busy_poll, budget);
	preempt_enable();
out:
	rcu_read_unlock();
}
EXPORT_SYMBOL(napi_busy_loop);

#endif /* CONFIG_NET_RX_BUSY_POLL */

static void napi_hash_add(struct napi_struct *napi)
{
	if (test_bit(NAPI_STATE_NO_BUSY_POLL, &napi->state))
		return;

	spin_lock(&napi_hash_lock);

	/* 0..NR_CPUS range is reserved for sender_cpu use */
	do {
		if (unlikely(++napi_gen_id < MIN_NAPI_ID))
			napi_gen_id = MIN_NAPI_ID;
	} while (napi_by_id(napi_gen_id));
	napi->napi_id = napi_gen_id;

	hlist_add_head_rcu(&napi->napi_hash_node,
			   &napi_hash[napi->napi_id % HASH_SIZE(napi_hash)]);

	spin_unlock(&napi_hash_lock);
}

/* Warning : caller is responsible to make sure rcu grace period
 * is respected before freeing memory containing @napi
 */
static void napi_hash_del(struct napi_struct *napi)
{
	spin_lock(&napi_hash_lock);

	hlist_del_init_rcu(&napi->napi_hash_node);

	spin_unlock(&napi_hash_lock);
}

static enum hrtimer_restart napi_watchdog(struct hrtimer *timer)
{
	struct napi_struct *napi;

	napi = container_of(timer, struct napi_struct, timer);

	/* Note : we use a relaxed variant of napi_schedule_prep() not setting
	 * NAPI_STATE_MISSED, since we do not react to a device IRQ.
	 */
	if (!napi_disable_pending(napi) &&
	    !test_and_set_bit(NAPI_STATE_SCHED, &napi->state)) {
		clear_bit(NAPI_STATE_PREFER_BUSY_POLL, &napi->state);
		__napi_schedule_irqoff(napi);
	}

	return HRTIMER_NORESTART;
}

static void init_gro_hash(struct napi_struct *napi)
{
	int i;

	for (i = 0; i < GRO_HASH_BUCKETS; i++) {
		INIT_LIST_HEAD(&napi->gro_hash[i].list);
		napi->gro_hash[i].count = 0;
	}
	napi->gro_bitmask = 0;
}

int dev_set_threaded(struct net_device *dev, bool threaded)
{
	struct napi_struct *napi;
	int err = 0;

	if (dev->threaded == threaded)
		return 0;

	if (threaded) {
		list_for_each_entry(napi, &dev->napi_list, dev_list) {
			if (!napi->thread) {
				err = napi_kthread_create(napi);
				if (err) {
					threaded = false;
					break;
				}
			}
		}
	}

	dev->threaded = threaded;

	/* Make sure kthread is created before THREADED bit
	 * is set.
	 */
	smp_mb__before_atomic();

	/* Setting/unsetting threaded mode on a napi might not immediately
	 * take effect, if the current napi instance is actively being
	 * polled. In this case, the switch between threaded mode and
	 * softirq mode will happen in the next round of napi_schedule().
	 * This should not cause hiccups/stalls to the live traffic.
	 */
	list_for_each_entry(napi, &dev->napi_list, dev_list) {
		if (threaded)
			set_bit(NAPI_STATE_THREADED, &napi->state);
		else
			clear_bit(NAPI_STATE_THREADED, &napi->state);
	}

	return err;
}
EXPORT_SYMBOL(dev_set_threaded);

void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
		    int (*poll)(struct napi_struct *, int), int weight)
{
	if (WARN_ON(test_and_set_bit(NAPI_STATE_LISTED, &napi->state)))
		return;

	INIT_LIST_HEAD(&napi->poll_list);
	INIT_HLIST_NODE(&napi->napi_hash_node);
	hrtimer_init(&napi->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
	napi->timer.function = napi_watchdog;
	init_gro_hash(napi);
	napi->skb = NULL;
	INIT_LIST_HEAD(&napi->rx_list);
	napi->rx_count = 0;
	napi->poll = poll;
	if (weight > NAPI_POLL_WEIGHT)
		netdev_err_once(dev, "%s() called with weight %d\n", __func__,
				weight);
	napi->weight = weight;
	napi->dev = dev;
#ifdef CONFIG_NETPOLL
	napi->poll_owner = -1;
#endif
	set_bit(NAPI_STATE_SCHED, &napi->state);
	set_bit(NAPI_STATE_NPSVC, &napi->state);
	list_add_rcu(&napi->dev_list, &dev->napi_list);
	napi_hash_add(napi);
	/* Create kthread for this napi if dev->threaded is set.
	 * Clear dev->threaded if kthread creation failed so that
	 * threaded mode will not be enabled in napi_enable().
	 */
	if (dev->threaded && napi_kthread_create(napi))
		dev->threaded = 0;
}
EXPORT_SYMBOL(netif_napi_add);

void napi_disable(struct napi_struct *n)
{
	unsigned long val, new;

	might_sleep();
	set_bit(NAPI_STATE_DISABLE, &n->state);

	for ( ; ; ) {
		val = READ_ONCE(n->state);
		if (val & (NAPIF_STATE_SCHED | NAPIF_STATE_NPSVC)) {
			usleep_range(20, 200);
			continue;
		}

		new = val | NAPIF_STATE_SCHED | NAPIF_STATE_NPSVC;
		new &= ~(NAPIF_STATE_THREADED | NAPIF_STATE_PREFER_BUSY_POLL);

		if (cmpxchg(&n->state, val, new) == val)
			break;
	}

	hrtimer_cancel(&n->timer);

	clear_bit(NAPI_STATE_DISABLE, &n->state);
}
EXPORT_SYMBOL(napi_disable);

/**
 *	napi_enable - enable NAPI scheduling
 *	@n: NAPI context
 *
 * Resume NAPI from being scheduled on this context.
 * Must be paired with napi_disable.
 */
void napi_enable(struct napi_struct *n)
{
	unsigned long val, new;

	do {
		val = READ_ONCE(n->state);
		BUG_ON(!test_bit(NAPI_STATE_SCHED, &val));

		new = val & ~(NAPIF_STATE_SCHED | NAPIF_STATE_NPSVC);
		if (n->dev->threaded && n->thread)
			new |= NAPIF_STATE_THREADED;
	} while (cmpxchg(&n->state, val, new) != val);
}
EXPORT_SYMBOL(napi_enable);

static void flush_gro_hash(struct napi_struct *napi)
{
	int i;

	for (i = 0; i < GRO_HASH_BUCKETS; i++) {
		struct sk_buff *skb, *n;

		list_for_each_entry_safe(skb, n, &napi->gro_hash[i].list, list)
			kfree_skb(skb);
		napi->gro_hash[i].count = 0;
	}
}

/* Must be called in process context */
void __netif_napi_del(struct napi_struct *napi)
{
	if (!test_and_clear_bit(NAPI_STATE_LISTED, &napi->state))
		return;

	napi_hash_del(napi);
	list_del_rcu(&napi->dev_list);
	napi_free_frags(napi);

	flush_gro_hash(napi);
	napi->gro_bitmask = 0;

	if (napi->thread) {
		kthread_stop(napi->thread);
		napi->thread = NULL;
	}
}
EXPORT_SYMBOL(__netif_napi_del);

static int __napi_poll(struct napi_struct *n, bool *repoll)
{
	int work, weight;

	weight = n->weight;

	/* This NAPI_STATE_SCHED test is for avoiding a race
	 * with netpoll's poll_napi().  Only the entity which
	 * obtains the lock and sees NAPI_STATE_SCHED set will
	 * actually make the ->poll() call.  Therefore we avoid
	 * accidentally calling ->poll() when NAPI is not scheduled.
	 */
	work = 0;
	if (test_bit(NAPI_STATE_SCHED, &n->state)) {
		work = n->poll(n, weight);
		trace_napi_poll(n, work, weight);
	}

	if (unlikely(work > weight))
		netdev_err_once(n->dev, "NAPI poll function %pS returned %d, exceeding its budget of %d.\n",
				n->poll, work, weight);

	if (likely(work < weight))
		return work;

	/* Drivers must not modify the NAPI state if they
	 * consume the entire weight.  In such cases this code
	 * still "owns" the NAPI instance and therefore can
	 * move the instance around on the list at-will.
	 */
	if (unlikely(napi_disable_pending(n))) {
		napi_complete(n);
		return work;
	}

	/* The NAPI context has more processing work, but busy-polling
	 * is preferred. Exit early.
	 */
	if (napi_prefer_busy_poll(n)) {
		if (napi_complete_done(n, work)) {
			/* If timeout is not set, we need to make sure
			 * that the NAPI is re-scheduled.
			 */
			napi_schedule(n);
		}
		return work;
	}

	if (n->gro_bitmask) {
		/* flush too old packets
		 * If HZ < 1000, flush all packets.
		 */
		napi_gro_flush(n, HZ >= 1000);
	}

	gro_normal_list(n);

	/* Some drivers may have called napi_schedule
	 * prior to exhausting their budget.
	 */
	if (unlikely(!list_empty(&n->poll_list))) {
		pr_warn_once("%s: Budget exhausted after napi rescheduled\n",
			     n->dev ? n->dev->name : "backlog");
		return work;
	}

	*repoll = true;

	return work;
}

static int napi_poll(struct napi_struct *n, struct list_head *repoll)
{
	bool do_repoll = false;
	void *have;
	int work;

	list_del_init(&n->poll_list);

	have = netpoll_poll_lock(n);

	work = __napi_poll(n, &do_repoll);

	if (do_repoll)
		list_add_tail(&n->poll_list, repoll);

	netpoll_poll_unlock(have);

	return work;
}

static int napi_thread_wait(struct napi_struct *napi)
{
	bool woken = false;

	set_current_state(TASK_INTERRUPTIBLE);

	while (!kthread_should_stop()) {
		/* Testing SCHED_THREADED bit here to make sure the current
		 * kthread owns this napi and could poll on this napi.
		 * Testing SCHED bit is not enough because SCHED bit might be
		 * set by some other busy poll thread or by napi_disable().
		 */
		if (test_bit(NAPI_STATE_SCHED_THREADED, &napi->state) || woken) {
			WARN_ON(!list_empty(&napi->poll_list));
			__set_current_state(TASK_RUNNING);
			return 0;
		}

		schedule();
		/* woken being true indicates this thread owns this napi. */
		woken = true;
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);

	return -1;
}

static int napi_threaded_poll(void *data)
{
	struct napi_struct *napi = data;
	void *have;

	while (!napi_thread_wait(napi)) {
		for (;;) {
			bool repoll = false;

			local_bh_disable();

			have = netpoll_poll_lock(napi);
			__napi_poll(napi, &repoll);
			netpoll_poll_unlock(have);

			local_bh_enable();

			if (!repoll)
				break;

			cond_resched();
		}
	}
	return 0;
}

static __latent_entropy void net_rx_action(struct softirq_action *h)
{
	struct softnet_data *sd = this_cpu_ptr(&softnet_data);
	unsigned long time_limit = jiffies +
		usecs_to_jiffies(netdev_budget_usecs);
	int budget = netdev_budget;
	LIST_HEAD(list);
	LIST_HEAD(repoll);

	local_irq_disable();
	list_splice_init(&sd->poll_list, &list);
	local_irq_enable();

	for (;;) {
		struct napi_struct *n;

		if (list_empty(&list)) {
			if (!sd_has_rps_ipi_waiting(sd) && list_empty(&repoll))
				return;
			break;
		}

		n = list_first_entry(&list, struct napi_struct, poll_list);
		budget -= napi_poll(n, &repoll);

		/* If softirq window is exhausted then punt.
		 * Allow this to run for 2 jiffies since which will allow
		 * an average latency of 1.5/HZ.
		 */
		if (unlikely(budget <= 0 ||
			     time_after_eq(jiffies, time_limit))) {
			sd->time_squeeze++;
			break;
		}
	}

	local_irq_disable();

	list_splice_tail_init(&sd->poll_list, &list);
	list_splice_tail(&repoll, &list);
	list_splice(&list, &sd->poll_list);
	if (!list_empty(&sd->poll_list))
		__raise_softirq_irqoff(NET_RX_SOFTIRQ);

	net_rps_action_and_irq_enable(sd);
}

struct netdev_adjacent {
	struct net_device *dev;

	/* upper master flag, there can only be one master device per list */
	bool master;

	/* lookup ignore flag */
	bool ignore;

	/* counter for the number of times this device was added to us */
	u16 ref_nr;

	/* private field for the users */
	void *private;

	struct list_head list;
	struct rcu_head rcu;
};

static struct netdev_adjacent *__netdev_find_adj(struct net_device *adj_dev,
						 struct list_head *adj_list)
{
	struct netdev_adjacent *adj;

	list_for_each_entry(adj, adj_list, list) {
		if (adj->dev == adj_dev)
			return adj;
	}
	return NULL;
}

static int ____netdev_has_upper_dev(struct net_device *upper_dev,
				    struct netdev_nested_priv *priv)
{
	struct net_device *dev = (struct net_device *)priv->data;

	return upper_dev == dev;
}

/**
 * netdev_has_upper_dev - Check if device is linked to an upper device
 * @dev: device
 * @upper_dev: upper device to check
 *
 * Find out if a device is linked to specified upper device and return true
 * in case it is. Note that this checks only immediate upper device,
 * not through a complete stack of devices. The caller must hold the RTNL lock.
 */
bool netdev_has_upper_dev(struct net_device *dev,
			  struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.data = (void *)upper_dev,
	};

	ASSERT_RTNL();

	return netdev_walk_all_upper_dev_rcu(dev, ____netdev_has_upper_dev,
					     &priv);
}
EXPORT_SYMBOL(netdev_has_upper_dev);

/**
 * netdev_has_upper_dev_all_rcu - Check if device is linked to an upper device
 * @dev: device
 * @upper_dev: upper device to check
 *
 * Find out if a device is linked to specified upper device and return true
 * in case it is. Note that this checks the entire upper device chain.
 * The caller must hold rcu lock.
 */

bool netdev_has_upper_dev_all_rcu(struct net_device *dev,
				  struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.data = (void *)upper_dev,
	};

	return !!netdev_walk_all_upper_dev_rcu(dev, ____netdev_has_upper_dev,
					       &priv);
}
EXPORT_SYMBOL(netdev_has_upper_dev_all_rcu);

/**
 * netdev_has_any_upper_dev - Check if device is linked to some device
 * @dev: device
 *
 * Find out if a device is linked to an upper device and return true in case
 * it is. The caller must hold the RTNL lock.
 */
bool netdev_has_any_upper_dev(struct net_device *dev)
{
	ASSERT_RTNL();

	return !list_empty(&dev->adj_list.upper);
}
EXPORT_SYMBOL(netdev_has_any_upper_dev);

/**
 * netdev_master_upper_dev_get - Get master upper device
 * @dev: device
 *
 * Find a master upper device and return pointer to it or NULL in case
 * it's not there. The caller must hold the RTNL lock.
 */
struct net_device *netdev_master_upper_dev_get(struct net_device *dev)
{
	struct netdev_adjacent *upper;

	ASSERT_RTNL();

	if (list_empty(&dev->adj_list.upper))
		return NULL;

	upper = list_first_entry(&dev->adj_list.upper,
				 struct netdev_adjacent, list);
	if (likely(upper->master))
		return upper->dev;
	return NULL;
}
EXPORT_SYMBOL(netdev_master_upper_dev_get);

static struct net_device *__netdev_master_upper_dev_get(struct net_device *dev)
{
	struct netdev_adjacent *upper;

	ASSERT_RTNL();

	if (list_empty(&dev->adj_list.upper))
		return NULL;

	upper = list_first_entry(&dev->adj_list.upper,
				 struct netdev_adjacent, list);
	if (likely(upper->master) && !upper->ignore)
		return upper->dev;
	return NULL;
}

/**
 * netdev_has_any_lower_dev - Check if device is linked to some device
 * @dev: device
 *
 * Find out if a device is linked to a lower device and return true in case
 * it is. The caller must hold the RTNL lock.
 */
static bool netdev_has_any_lower_dev(struct net_device *dev)
{
	ASSERT_RTNL();

	return !list_empty(&dev->adj_list.lower);
}

void *netdev_adjacent_get_private(struct list_head *adj_list)
{
	struct netdev_adjacent *adj;

	adj = list_entry(adj_list, struct netdev_adjacent, list);

	return adj->private;
}
EXPORT_SYMBOL(netdev_adjacent_get_private);

/**
 * netdev_upper_get_next_dev_rcu - Get the next dev from upper list
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next device from the dev's upper list, starting from iter
 * position. The caller must hold RCU read lock.
 */
struct net_device *netdev_upper_get_next_dev_rcu(struct net_device *dev,
						 struct list_head **iter)
{
	struct netdev_adjacent *upper;

	WARN_ON_ONCE(!rcu_read_lock_held() && !lockdep_rtnl_is_held());

	upper = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);

	if (&upper->list == &dev->adj_list.upper)
		return NULL;

	*iter = &upper->list;

	return upper->dev;
}
EXPORT_SYMBOL(netdev_upper_get_next_dev_rcu);

static struct net_device *__netdev_next_upper_dev(struct net_device *dev,
						  struct list_head **iter,
						  bool *ignore)
{
	struct netdev_adjacent *upper;

	upper = list_entry((*iter)->next, struct netdev_adjacent, list);

	if (&upper->list == &dev->adj_list.upper)
		return NULL;

	*iter = &upper->list;
	*ignore = upper->ignore;

	return upper->dev;
}

static struct net_device *netdev_next_upper_dev_rcu(struct net_device *dev,
						    struct list_head **iter)
{
	struct netdev_adjacent *upper;

	WARN_ON_ONCE(!rcu_read_lock_held() && !lockdep_rtnl_is_held());

	upper = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);

	if (&upper->list == &dev->adj_list.upper)
		return NULL;

	*iter = &upper->list;

	return upper->dev;
}

static int __netdev_walk_all_upper_dev(struct net_device *dev,
				       int (*fn)(struct net_device *dev,
					 struct netdev_nested_priv *priv),
				       struct netdev_nested_priv *priv)
{
	struct net_device *udev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;
	bool ignore;

	now = dev;
	iter = &dev->adj_list.upper;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			udev = __netdev_next_upper_dev(now, &iter, &ignore);
			if (!udev)
				break;
			if (ignore)
				continue;

			next = udev;
			niter = &udev->adj_list.upper;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}

int netdev_walk_all_upper_dev_rcu(struct net_device *dev,
				  int (*fn)(struct net_device *dev,
					    struct netdev_nested_priv *priv),
				  struct netdev_nested_priv *priv)
{
	struct net_device *udev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;

	now = dev;
	iter = &dev->adj_list.upper;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			udev = netdev_next_upper_dev_rcu(now, &iter);
			if (!udev)
				break;

			next = udev;
			niter = &udev->adj_list.upper;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_walk_all_upper_dev_rcu);

static bool __netdev_has_upper_dev(struct net_device *dev,
				   struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.flags = 0,
		.data = (void *)upper_dev,
	};

	ASSERT_RTNL();

	return __netdev_walk_all_upper_dev(dev, ____netdev_has_upper_dev,
					   &priv);
}

/**
 * netdev_lower_get_next_private - Get the next ->private from the
 *				   lower neighbour list
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next netdev_adjacent->private from the dev's lower neighbour
 * list, starting from iter position. The caller must hold either hold the
 * RTNL lock or its own locking that guarantees that the neighbour lower
 * list will remain unchanged.
 */
void *netdev_lower_get_next_private(struct net_device *dev,
				    struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry(*iter, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = lower->list.next;

	return lower->private;
}
EXPORT_SYMBOL(netdev_lower_get_next_private);

/**
 * netdev_lower_get_next_private_rcu - Get the next ->private from the
 *				       lower neighbour list, RCU
 *				       variant
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next netdev_adjacent->private from the dev's lower neighbour
 * list, starting from iter position. The caller must hold RCU read lock.
 */
void *netdev_lower_get_next_private_rcu(struct net_device *dev,
					struct list_head **iter)
{
	struct netdev_adjacent *lower;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_bh_held());

	lower = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;

	return lower->private;
}
EXPORT_SYMBOL(netdev_lower_get_next_private_rcu);

/**
 * netdev_lower_get_next - Get the next device from the lower neighbour
 *                         list
 * @dev: device
 * @iter: list_head ** of the current position
 *
 * Gets the next netdev_adjacent from the dev's lower neighbour
 * list, starting from iter position. The caller must hold RTNL lock or
 * its own locking that guarantees that the neighbour lower
 * list will remain unchanged.
 */
void *netdev_lower_get_next(struct net_device *dev, struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry(*iter, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = lower->list.next;

	return lower->dev;
}
EXPORT_SYMBOL(netdev_lower_get_next);

static struct net_device *netdev_next_lower_dev(struct net_device *dev,
						struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry((*iter)->next, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;

	return lower->dev;
}

static struct net_device *__netdev_next_lower_dev(struct net_device *dev,
						  struct list_head **iter,
						  bool *ignore)
{
	struct netdev_adjacent *lower;

	lower = list_entry((*iter)->next, struct netdev_adjacent, list);

	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;
	*ignore = lower->ignore;

	return lower->dev;
}

int netdev_walk_all_lower_dev(struct net_device *dev,
			      int (*fn)(struct net_device *dev,
					struct netdev_nested_priv *priv),
			      struct netdev_nested_priv *priv)
{
	struct net_device *ldev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;

	now = dev;
	iter = &dev->adj_list.lower;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			ldev = netdev_next_lower_dev(now, &iter);
			if (!ldev)
				break;

			next = ldev;
			niter = &ldev->adj_list.lower;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_walk_all_lower_dev);

static int __netdev_walk_all_lower_dev(struct net_device *dev,
				       int (*fn)(struct net_device *dev,
					 struct netdev_nested_priv *priv),
				       struct netdev_nested_priv *priv)
{
	struct net_device *ldev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;
	bool ignore;

	now = dev;
	iter = &dev->adj_list.lower;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			ldev = __netdev_next_lower_dev(now, &iter, &ignore);
			if (!ldev)
				break;
			if (ignore)
				continue;

			next = ldev;
			niter = &ldev->adj_list.lower;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}

struct net_device *netdev_next_lower_dev_rcu(struct net_device *dev,
					     struct list_head **iter)
{
	struct netdev_adjacent *lower;

	lower = list_entry_rcu((*iter)->next, struct netdev_adjacent, list);
	if (&lower->list == &dev->adj_list.lower)
		return NULL;

	*iter = &lower->list;

	return lower->dev;
}
EXPORT_SYMBOL(netdev_next_lower_dev_rcu);

static u8 __netdev_upper_depth(struct net_device *dev)
{
	struct net_device *udev;
	struct list_head *iter;
	u8 max_depth = 0;
	bool ignore;

	for (iter = &dev->adj_list.upper,
	     udev = __netdev_next_upper_dev(dev, &iter, &ignore);
	     udev;
	     udev = __netdev_next_upper_dev(dev, &iter, &ignore)) {
		if (ignore)
			continue;
		if (max_depth < udev->upper_level)
			max_depth = udev->upper_level;
	}

	return max_depth;
}

static u8 __netdev_lower_depth(struct net_device *dev)
{
	struct net_device *ldev;
	struct list_head *iter;
	u8 max_depth = 0;
	bool ignore;

	for (iter = &dev->adj_list.lower,
	     ldev = __netdev_next_lower_dev(dev, &iter, &ignore);
	     ldev;
	     ldev = __netdev_next_lower_dev(dev, &iter, &ignore)) {
		if (ignore)
			continue;
		if (max_depth < ldev->lower_level)
			max_depth = ldev->lower_level;
	}

	return max_depth;
}

static int __netdev_update_upper_level(struct net_device *dev,
				       struct netdev_nested_priv *__unused)
{
	dev->upper_level = __netdev_upper_depth(dev) + 1;
	return 0;
}

static int __netdev_update_lower_level(struct net_device *dev,
				       struct netdev_nested_priv *priv)
{
	dev->lower_level = __netdev_lower_depth(dev) + 1;

#ifdef CONFIG_LOCKDEP
	if (!priv)
		return 0;

	if (priv->flags & NESTED_SYNC_IMM)
		dev->nested_level = dev->lower_level - 1;
	if (priv->flags & NESTED_SYNC_TODO)
		net_unlink_todo(dev);
#endif
	return 0;
}

int netdev_walk_all_lower_dev_rcu(struct net_device *dev,
				  int (*fn)(struct net_device *dev,
					    struct netdev_nested_priv *priv),
				  struct netdev_nested_priv *priv)
{
	struct net_device *ldev, *next, *now, *dev_stack[MAX_NEST_DEV + 1];
	struct list_head *niter, *iter, *iter_stack[MAX_NEST_DEV + 1];
	int ret, cur = 0;

	now = dev;
	iter = &dev->adj_list.lower;

	while (1) {
		if (now != dev) {
			ret = fn(now, priv);
			if (ret)
				return ret;
		}

		next = NULL;
		while (1) {
			ldev = netdev_next_lower_dev_rcu(now, &iter);
			if (!ldev)
				break;

			next = ldev;
			niter = &ldev->adj_list.lower;
			dev_stack[cur] = now;
			iter_stack[cur++] = iter;
			break;
		}

		if (!next) {
			if (!cur)
				return 0;
			next = dev_stack[--cur];
			niter = iter_stack[cur];
		}

		now = next;
		iter = niter;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(netdev_walk_all_lower_dev_rcu);

/**
 * netdev_lower_get_first_private_rcu - Get the first ->private from the
 *				       lower neighbour list, RCU
 *				       variant
 * @dev: device
 *
 * Gets the first netdev_adjacent->private from the dev's lower neighbour
 * list. The caller must hold RCU read lock.
 */
void *netdev_lower_get_first_private_rcu(struct net_device *dev)
{
	struct netdev_adjacent *lower;

	lower = list_first_or_null_rcu(&dev->adj_list.lower,
			struct netdev_adjacent, list);
	if (lower)
		return lower->private;
	return NULL;
}
EXPORT_SYMBOL(netdev_lower_get_first_private_rcu);

/**
 * netdev_master_upper_dev_get_rcu - Get master upper device
 * @dev: device
 *
 * Find a master upper device and return pointer to it or NULL in case
 * it's not there. The caller must hold the RCU read lock.
 */
struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *dev)
{
	struct netdev_adjacent *upper;

	upper = list_first_or_null_rcu(&dev->adj_list.upper,
				       struct netdev_adjacent, list);
	if (upper && likely(upper->master))
		return upper->dev;
	return NULL;
}
EXPORT_SYMBOL(netdev_master_upper_dev_get_rcu);

static int netdev_adjacent_sysfs_add(struct net_device *dev,
			      struct net_device *adj_dev,
			      struct list_head *dev_list)
{
	char linkname[IFNAMSIZ+7];

	sprintf(linkname, dev_list == &dev->adj_list.upper ?
		"upper_%s" : "lower_%s", adj_dev->name);
	return sysfs_create_link(&(dev->dev.kobj), &(adj_dev->dev.kobj),
				 linkname);
}
static void netdev_adjacent_sysfs_del(struct net_device *dev,
			       char *name,
			       struct list_head *dev_list)
{
	char linkname[IFNAMSIZ+7];

	sprintf(linkname, dev_list == &dev->adj_list.upper ?
		"upper_%s" : "lower_%s", name);
	sysfs_remove_link(&(dev->dev.kobj), linkname);
}

static inline bool netdev_adjacent_is_neigh_list(struct net_device *dev,
						 struct net_device *adj_dev,
						 struct list_head *dev_list)
{
	return (dev_list == &dev->adj_list.upper ||
		dev_list == &dev->adj_list.lower) &&
		net_eq(dev_net(dev), dev_net(adj_dev));
}

static int __netdev_adjacent_dev_insert(struct net_device *dev,
					struct net_device *adj_dev,
					struct list_head *dev_list,
					void *private, bool master)
{
	struct netdev_adjacent *adj;
	int ret;

	adj = __netdev_find_adj(adj_dev, dev_list);

	if (adj) {
		adj->ref_nr += 1;
		pr_debug("Insert adjacency: dev %s adj_dev %s adj->ref_nr %d\n",
			 dev->name, adj_dev->name, adj->ref_nr);

		return 0;
	}

	adj = kmalloc(sizeof(*adj), GFP_KERNEL);
	if (!adj)
		return -ENOMEM;

	adj->dev = adj_dev;
	adj->master = master;
	adj->ref_nr = 1;
	adj->private = private;
	adj->ignore = false;
	dev_hold(adj_dev);

	pr_debug("Insert adjacency: dev %s adj_dev %s adj->ref_nr %d; dev_hold on %s\n",
		 dev->name, adj_dev->name, adj->ref_nr, adj_dev->name);

	if (netdev_adjacent_is_neigh_list(dev, adj_dev, dev_list)) {
		ret = netdev_adjacent_sysfs_add(dev, adj_dev, dev_list);
		if (ret)
			goto free_adj;
	}

	/* Ensure that master link is always the first item in list. */
	if (master) {
		ret = sysfs_create_link(&(dev->dev.kobj),
					&(adj_dev->dev.kobj), "master");
		if (ret)
			goto remove_symlinks;

		list_add_rcu(&adj->list, dev_list);
	} else {
		list_add_tail_rcu(&adj->list, dev_list);
	}

	return 0;

remove_symlinks:
	if (netdev_adjacent_is_neigh_list(dev, adj_dev, dev_list))
		netdev_adjacent_sysfs_del(dev, adj_dev->name, dev_list);
free_adj:
	kfree(adj);
	dev_put(adj_dev);

	return ret;
}

static void __netdev_adjacent_dev_remove(struct net_device *dev,
					 struct net_device *adj_dev,
					 u16 ref_nr,
					 struct list_head *dev_list)
{
	struct netdev_adjacent *adj;

	pr_debug("Remove adjacency: dev %s adj_dev %s ref_nr %d\n",
		 dev->name, adj_dev->name, ref_nr);

	adj = __netdev_find_adj(adj_dev, dev_list);

	if (!adj) {
		pr_err("Adjacency does not exist for device %s from %s\n",
		       dev->name, adj_dev->name);
		WARN_ON(1);
		return;
	}

	if (adj->ref_nr > ref_nr) {
		pr_debug("adjacency: %s to %s ref_nr - %d = %d\n",
			 dev->name, adj_dev->name, ref_nr,
			 adj->ref_nr - ref_nr);
		adj->ref_nr -= ref_nr;
		return;
	}

	if (adj->master)
		sysfs_remove_link(&(dev->dev.kobj), "master");

	if (netdev_adjacent_is_neigh_list(dev, adj_dev, dev_list))
		netdev_adjacent_sysfs_del(dev, adj_dev->name, dev_list);

	list_del_rcu(&adj->list);
	pr_debug("adjacency: dev_put for %s, because link removed from %s to %s\n",
		 adj_dev->name, dev->name, adj_dev->name);
	dev_put(adj_dev);
	kfree_rcu(adj, rcu);
}

static int __netdev_adjacent_dev_link_lists(struct net_device *dev,
					    struct net_device *upper_dev,
					    struct list_head *up_list,
					    struct list_head *down_list,
					    void *private, bool master)
{
	int ret;

	ret = __netdev_adjacent_dev_insert(dev, upper_dev, up_list,
					   private, master);
	if (ret)
		return ret;

	ret = __netdev_adjacent_dev_insert(upper_dev, dev, down_list,
					   private, false);
	if (ret) {
		__netdev_adjacent_dev_remove(dev, upper_dev, 1, up_list);
		return ret;
	}

	return 0;
}

static void __netdev_adjacent_dev_unlink_lists(struct net_device *dev,
					       struct net_device *upper_dev,
					       u16 ref_nr,
					       struct list_head *up_list,
					       struct list_head *down_list)
{
	__netdev_adjacent_dev_remove(dev, upper_dev, ref_nr, up_list);
	__netdev_adjacent_dev_remove(upper_dev, dev, ref_nr, down_list);
}

static int __netdev_adjacent_dev_link_neighbour(struct net_device *dev,
						struct net_device *upper_dev,
						void *private, bool master)
{
	return __netdev_adjacent_dev_link_lists(dev, upper_dev,
						&dev->adj_list.upper,
						&upper_dev->adj_list.lower,
						private, master);
}

static void __netdev_adjacent_dev_unlink_neighbour(struct net_device *dev,
						   struct net_device *upper_dev)
{
	__netdev_adjacent_dev_unlink_lists(dev, upper_dev, 1,
					   &dev->adj_list.upper,
					   &upper_dev->adj_list.lower);
}

static int __netdev_upper_dev_link(struct net_device *dev,
				   struct net_device *upper_dev, bool master,
				   void *upper_priv, void *upper_info,
				   struct netdev_nested_priv *priv,
				   struct netlink_ext_ack *extack)
{
	struct netdev_notifier_changeupper_info changeupper_info = {
		.info = {
			.dev = dev,
			.extack = extack,
		},
		.upper_dev = upper_dev,
		.master = master,
		.linking = true,
		.upper_info = upper_info,
	};
	struct net_device *master_dev;
	int ret = 0;

	ASSERT_RTNL();

	if (dev == upper_dev)
		return -EBUSY;

	/* To prevent loops, check if dev is not upper device to upper_dev. */
	if (__netdev_has_upper_dev(upper_dev, dev))
		return -EBUSY;

	if ((dev->lower_level + upper_dev->upper_level) > MAX_NEST_DEV)
		return -EMLINK;

	if (!master) {
		if (__netdev_has_upper_dev(dev, upper_dev))
			return -EEXIST;
	} else {
		master_dev = __netdev_master_upper_dev_get(dev);
		if (master_dev)
			return master_dev == upper_dev ? -EEXIST : -EBUSY;
	}

	ret = call_netdevice_notifiers_info(NETDEV_PRECHANGEUPPER,
					    &changeupper_info.info);
	ret = notifier_to_errno(ret);
	if (ret)
		return ret;

	ret = __netdev_adjacent_dev_link_neighbour(dev, upper_dev, upper_priv,
						   master);
	if (ret)
		return ret;

	ret = call_netdevice_notifiers_info(NETDEV_CHANGEUPPER,
					    &changeupper_info.info);
	ret = notifier_to_errno(ret);
	if (ret)
		goto rollback;

	__netdev_update_upper_level(dev, NULL);
	__netdev_walk_all_lower_dev(dev, __netdev_update_upper_level, NULL);

	__netdev_update_lower_level(upper_dev, priv);
	__netdev_walk_all_upper_dev(upper_dev, __netdev_update_lower_level,
				    priv);

	return 0;

rollback:
	__netdev_adjacent_dev_unlink_neighbour(dev, upper_dev);

	return ret;
}

/**
 * netdev_upper_dev_link - Add a link to the upper device
 * @dev: device
 * @upper_dev: new upper device
 * @extack: netlink extended ack
 *
 * Adds a link to device which is upper to this one. The caller must hold
 * the RTNL lock. On a failure a negative errno code is returned.
 * On success the reference counts are adjusted and the function
 * returns zero.
 */
int netdev_upper_dev_link(struct net_device *dev,
			  struct net_device *upper_dev,
			  struct netlink_ext_ack *extack)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_IMM | NESTED_SYNC_TODO,
		.data = NULL,
	};

	return __netdev_upper_dev_link(dev, upper_dev, false,
				       NULL, NULL, &priv, extack);
}
EXPORT_SYMBOL(netdev_upper_dev_link);

/**
 * netdev_master_upper_dev_link - Add a master link to the upper device
 * @dev: device
 * @upper_dev: new upper device
 * @upper_priv: upper device private
 * @upper_info: upper info to be passed down via notifier
 * @extack: netlink extended ack
 *
 * Adds a link to device which is upper to this one. In this case, only
 * one master upper device can be linked, although other non-master devices
 * might be linked as well. The caller must hold the RTNL lock.
 * On a failure a negative errno code is returned. On success the reference
 * counts are adjusted and the function returns zero.
 */
int netdev_master_upper_dev_link(struct net_device *dev,
				 struct net_device *upper_dev,
				 void *upper_priv, void *upper_info,
				 struct netlink_ext_ack *extack)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_IMM | NESTED_SYNC_TODO,
		.data = NULL,
	};

	return __netdev_upper_dev_link(dev, upper_dev, true,
				       upper_priv, upper_info, &priv, extack);
}
EXPORT_SYMBOL(netdev_master_upper_dev_link);

static void __netdev_upper_dev_unlink(struct net_device *dev,
				      struct net_device *upper_dev,
				      struct netdev_nested_priv *priv)
{
	struct netdev_notifier_changeupper_info changeupper_info = {
		.info = {
			.dev = dev,
		},
		.upper_dev = upper_dev,
		.linking = false,
	};

	ASSERT_RTNL();

	changeupper_info.master = netdev_master_upper_dev_get(dev) == upper_dev;

	call_netdevice_notifiers_info(NETDEV_PRECHANGEUPPER,
				      &changeupper_info.info);

	__netdev_adjacent_dev_unlink_neighbour(dev, upper_dev);

	call_netdevice_notifiers_info(NETDEV_CHANGEUPPER,
				      &changeupper_info.info);

	__netdev_update_upper_level(dev, NULL);
	__netdev_walk_all_lower_dev(dev, __netdev_update_upper_level, NULL);

	__netdev_update_lower_level(upper_dev, priv);
	__netdev_walk_all_upper_dev(upper_dev, __netdev_update_lower_level,
				    priv);
}

/**
 * netdev_upper_dev_unlink - Removes a link to upper device
 * @dev: device
 * @upper_dev: new upper device
 *
 * Removes a link to device which is upper to this one. The caller must hold
 * the RTNL lock.
 */
void netdev_upper_dev_unlink(struct net_device *dev,
			     struct net_device *upper_dev)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_TODO,
		.data = NULL,
	};

	__netdev_upper_dev_unlink(dev, upper_dev, &priv);
}
EXPORT_SYMBOL(netdev_upper_dev_unlink);

static void __netdev_adjacent_dev_set(struct net_device *upper_dev,
				      struct net_device *lower_dev,
				      bool val)
{
	struct netdev_adjacent *adj;

	adj = __netdev_find_adj(lower_dev, &upper_dev->adj_list.lower);
	if (adj)
		adj->ignore = val;

	adj = __netdev_find_adj(upper_dev, &lower_dev->adj_list.upper);
	if (adj)
		adj->ignore = val;
}

static void netdev_adjacent_dev_disable(struct net_device *upper_dev,
					struct net_device *lower_dev)
{
	__netdev_adjacent_dev_set(upper_dev, lower_dev, true);
}

static void netdev_adjacent_dev_enable(struct net_device *upper_dev,
				       struct net_device *lower_dev)
{
	__netdev_adjacent_dev_set(upper_dev, lower_dev, false);
}

int netdev_adjacent_change_prepare(struct net_device *old_dev,
				   struct net_device *new_dev,
				   struct net_device *dev,
				   struct netlink_ext_ack *extack)
{
	struct netdev_nested_priv priv = {
		.flags = 0,
		.data = NULL,
	};
	int err;

	if (!new_dev)
		return 0;

	if (old_dev && new_dev != old_dev)
		netdev_adjacent_dev_disable(dev, old_dev);
	err = __netdev_upper_dev_link(new_dev, dev, false, NULL, NULL, &priv,
				      extack);
	if (err) {
		if (old_dev && new_dev != old_dev)
			netdev_adjacent_dev_enable(dev, old_dev);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(netdev_adjacent_change_prepare);

void netdev_adjacent_change_commit(struct net_device *old_dev,
				   struct net_device *new_dev,
				   struct net_device *dev)
{
	struct netdev_nested_priv priv = {
		.flags = NESTED_SYNC_IMM | NESTED_SYNC_TODO,
		.data = NULL,
	};

	if (!new_dev || !old_dev)
		return;

	if (new_dev == old_dev)
		return;

	netdev_adjacent_dev_enable(dev, old_dev);
	__netdev_upper_dev_unlink(old_dev, dev, &priv);
}
EXPORT_SYMBOL(netdev_adjacent_change_commit);

void netdev_adjacent_change_abort(struct net_device *old_dev,
				  struct net_device *new_dev,
				  struct net_device *dev)
{
	struct netdev_nested_priv priv = {
		.flags = 0,
		.data = NULL,
	};

	if (!new_dev)
		return;

	if (old_dev && new_dev != old_dev)
		netdev_adjacent_dev_enable(dev, old_dev);

	__netdev_upper_dev_unlink(new_dev, dev, &priv);
}
EXPORT_SYMBOL(netdev_adjacent_change_abort);

/**
 * netdev_bonding_info_change - Dispatch event about slave change
 * @dev: device
 * @bonding_info: info to dispatch
 *
 * Send NETDEV_BONDING_INFO to netdev notifiers with info.
 * The caller must hold the RTNL lock.
 */
void netdev_bonding_info_change(struct net_device *dev,
				struct netdev_bonding_info *bonding_info)
{
	struct netdev_notifier_bonding_info info = {
		.info.dev = dev,
	};

	memcpy(&info.bonding_info, bonding_info,
	       sizeof(struct netdev_bonding_info));
	call_netdevice_notifiers_info(NETDEV_BONDING_INFO,
				      &info.info);
}
EXPORT_SYMBOL(netdev_bonding_info_change);

/**
 * netdev_get_xmit_slave - Get the xmit slave of master device
 * @dev: device
 * @skb: The packet
 * @all_slaves: assume all the slaves are active
 *
 * The reference counters are not incremented so the caller must be
 * careful with locks. The caller must hold RCU lock.
 * %NULL is returned if no slave is found.
 */

struct net_device *netdev_get_xmit_slave(struct net_device *dev,
					 struct sk_buff *skb,
					 bool all_slaves)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_get_xmit_slave)
		return NULL;
	return ops->ndo_get_xmit_slave(dev, skb, all_slaves);
}
EXPORT_SYMBOL(netdev_get_xmit_slave);

static struct net_device *netdev_sk_get_lower_dev(struct net_device *dev,
						  struct sock *sk)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_sk_get_lower_dev)
		return NULL;
	return ops->ndo_sk_get_lower_dev(dev, sk);
}

/**
 * netdev_sk_get_lowest_dev - Get the lowest device in chain given device and socket
 * @dev: device
 * @sk: the socket
 *
 * %NULL is returned if no lower device is found.
 */

struct net_device *netdev_sk_get_lowest_dev(struct net_device *dev,
					    struct sock *sk)
{
	struct net_device *lower;

	lower = netdev_sk_get_lower_dev(dev, sk);
	while (lower) {
		dev = lower;
		lower = netdev_sk_get_lower_dev(dev, sk);
	}

	return dev;
}
EXPORT_SYMBOL(netdev_sk_get_lowest_dev);

static void netdev_adjacent_add_links(struct net_device *dev)
{
	struct netdev_adjacent *iter;

	struct net *net = dev_net(dev);

	list_for_each_entry(iter, &dev->adj_list.upper, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.lower);
		netdev_adjacent_sysfs_add(dev, iter->dev,
					  &dev->adj_list.upper);
	}

	list_for_each_entry(iter, &dev->adj_list.lower, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.upper);
		netdev_adjacent_sysfs_add(dev, iter->dev,
					  &dev->adj_list.lower);
	}
}

static void netdev_adjacent_del_links(struct net_device *dev)
{
	struct netdev_adjacent *iter;

	struct net *net = dev_net(dev);

	list_for_each_entry(iter, &dev->adj_list.upper, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, dev->name,
					  &iter->dev->adj_list.lower);
		netdev_adjacent_sysfs_del(dev, iter->dev->name,
					  &dev->adj_list.upper);
	}

	list_for_each_entry(iter, &dev->adj_list.lower, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, dev->name,
					  &iter->dev->adj_list.upper);
		netdev_adjacent_sysfs_del(dev, iter->dev->name,
					  &dev->adj_list.lower);
	}
}

void netdev_adjacent_rename_links(struct net_device *dev, char *oldname)
{
	struct netdev_adjacent *iter;

	struct net *net = dev_net(dev);

	list_for_each_entry(iter, &dev->adj_list.upper, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, oldname,
					  &iter->dev->adj_list.lower);
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.lower);
	}

	list_for_each_entry(iter, &dev->adj_list.lower, list) {
		if (!net_eq(net, dev_net(iter->dev)))
			continue;
		netdev_adjacent_sysfs_del(iter->dev, oldname,
					  &iter->dev->adj_list.upper);
		netdev_adjacent_sysfs_add(iter->dev, dev,
					  &iter->dev->adj_list.upper);
	}
}

void *netdev_lower_dev_get_private(struct net_device *dev,
				   struct net_device *lower_dev)
{
	struct netdev_adjacent *lower;

	if (!lower_dev)
		return NULL;
	lower = __netdev_find_adj(lower_dev, &dev->adj_list.lower);
	if (!lower)
		return NULL;

	return lower->private;
}
EXPORT_SYMBOL(netdev_lower_dev_get_private);


/**
 * netdev_lower_state_changed - Dispatch event about lower device state change
 * @lower_dev: device
 * @lower_state_info: state to dispatch
 *
 * Send NETDEV_CHANGELOWERSTATE to netdev notifiers with info.
 * The caller must hold the RTNL lock.
 */
void netdev_lower_state_changed(struct net_device *lower_dev,
				void *lower_state_info)
{
	struct netdev_notifier_changelowerstate_info changelowerstate_info = {
		.info.dev = lower_dev,
	};

	ASSERT_RTNL();
	changelowerstate_info.lower_state_info = lower_state_info;
	call_netdevice_notifiers_info(NETDEV_CHANGELOWERSTATE,
				      &changelowerstate_info.info);
}
EXPORT_SYMBOL(netdev_lower_state_changed);

static void dev_change_rx_flags(struct net_device *dev, int flags)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (ops->ndo_change_rx_flags)
		ops->ndo_change_rx_flags(dev, flags);
}

static int __dev_set_promiscuity(struct net_device *dev, int inc, bool notify)
{
	unsigned int old_flags = dev->flags;
	kuid_t uid;
	kgid_t gid;

	ASSERT_RTNL();

	dev->flags |= IFF_PROMISC;
	dev->promiscuity += inc;
	if (dev->promiscuity == 0) {
		/*
		 * Avoid overflow.
		 * If inc causes overflow, untouch promisc and return error.
		 */
		if (inc < 0)
			dev->flags &= ~IFF_PROMISC;
		else {
			dev->promiscuity -= inc;
			netdev_warn(dev, "promiscuity touches roof, set promiscuity failed. promiscuity feature of device might be broken.\n");
			return -EOVERFLOW;
		}
	}
	if (dev->flags != old_flags) {
		pr_info("device %s %s promiscuous mode\n",
			dev->name,
			dev->flags & IFF_PROMISC ? "entered" : "left");
		if (audit_enabled) {
			current_uid_gid(&uid, &gid);
			audit_log(audit_context(), GFP_ATOMIC,
				  AUDIT_ANOM_PROMISCUOUS,
				  "dev=%s prom=%d old_prom=%d auid=%u uid=%u gid=%u ses=%u",
				  dev->name, (dev->flags & IFF_PROMISC),
				  (old_flags & IFF_PROMISC),
				  from_kuid(&init_user_ns, audit_get_loginuid(current)),
				  from_kuid(&init_user_ns, uid),
				  from_kgid(&init_user_ns, gid),
				  audit_get_sessionid(current));
		}

		dev_change_rx_flags(dev, IFF_PROMISC);
	}
	if (notify)
		__dev_notify_flags(dev, old_flags, IFF_PROMISC);
	return 0;
}

/**
 *	dev_set_promiscuity	- update promiscuity count on a device
 *	@dev: device
 *	@inc: modifier
 *
 *	Add or remove promiscuity from a device. While the count in the device
 *	remains above zero the interface remains promiscuous. Once it hits zero
 *	the device reverts back to normal filtering operation. A negative inc
 *	value is used to drop promiscuity on the device.
 *	Return 0 if successful or a negative errno code on error.
 */
int dev_set_promiscuity(struct net_device *dev, int inc)
{
	unsigned int old_flags = dev->flags;
	int err;

	err = __dev_set_promiscuity(dev, inc, true);
	if (err < 0)
		return err;
	if (dev->flags != old_flags)
		dev_set_rx_mode(dev);
	return err;
}
EXPORT_SYMBOL(dev_set_promiscuity);

static int __dev_set_allmulti(struct net_device *dev, int inc, bool notify)
{
	unsigned int old_flags = dev->flags, old_gflags = dev->gflags;

	ASSERT_RTNL();

	dev->flags |= IFF_ALLMULTI;
	dev->allmulti += inc;
	if (dev->allmulti == 0) {
		/*
		 * Avoid overflow.
		 * If inc causes overflow, untouch allmulti and return error.
		 */
		if (inc < 0)
			dev->flags &= ~IFF_ALLMULTI;
		else {
			dev->allmulti -= inc;
			netdev_warn(dev, "allmulti touches roof, set allmulti failed. allmulti feature of device might be broken.\n");
			return -EOVERFLOW;
		}
	}
	if (dev->flags ^ old_flags) {
		dev_change_rx_flags(dev, IFF_ALLMULTI);
		dev_set_rx_mode(dev);
		if (notify)
			__dev_notify_flags(dev, old_flags,
					   dev->gflags ^ old_gflags);
	}
	return 0;
}

/**
 *	dev_set_allmulti	- update allmulti count on a device
 *	@dev: device
 *	@inc: modifier
 *
 *	Add or remove reception of all multicast frames to a device. While the
 *	count in the device remains above zero the interface remains listening
 *	to all interfaces. Once it hits zero the device reverts back to normal
 *	filtering operation. A negative @inc value is used to drop the counter
 *	when releasing a resource needing all multicasts.
 *	Return 0 if successful or a negative errno code on error.
 */

int dev_set_allmulti(struct net_device *dev, int inc)
{
	return __dev_set_allmulti(dev, inc, true);
}
EXPORT_SYMBOL(dev_set_allmulti);

/*
 *	Upload unicast and multicast address lists to device and
 *	configure RX filtering. When the device doesn't support unicast
 *	filtering it is put in promiscuous mode while unicast addresses
 *	are present.
 */
void __dev_set_rx_mode(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	/* dev_open will call this function so the list will stay sane. */
	if (!(dev->flags&IFF_UP))
		return;

	if (!netif_device_present(dev))
		return;

	if (!(dev->priv_flags & IFF_UNICAST_FLT)) {
		/* Unicast addresses changes may only happen under the rtnl,
		 * therefore calling __dev_set_promiscuity here is safe.
		 */
		if (!netdev_uc_empty(dev) && !dev->uc_promisc) {
			__dev_set_promiscuity(dev, 1, false);
			dev->uc_promisc = true;
		} else if (netdev_uc_empty(dev) && dev->uc_promisc) {
			__dev_set_promiscuity(dev, -1, false);
			dev->uc_promisc = false;
		}
	}

	if (ops->ndo_set_rx_mode)
		ops->ndo_set_rx_mode(dev);
}

void dev_set_rx_mode(struct net_device *dev)
{
	netif_addr_lock_bh(dev);
	__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
}

/**
 *	dev_get_flags - get flags reported to userspace
 *	@dev: device
 *
 *	Get the combination of flag bits exported through APIs to userspace.
 */
unsigned int dev_get_flags(const struct net_device *dev)
{
	unsigned int flags;

	flags = (dev->flags & ~(IFF_PROMISC |
				IFF_ALLMULTI |
				IFF_RUNNING |
				IFF_LOWER_UP |
				IFF_DORMANT)) |
		(dev->gflags & (IFF_PROMISC |
				IFF_ALLMULTI));

	if (netif_running(dev)) {
		if (netif_oper_up(dev))
			flags |= IFF_RUNNING;
		if (netif_carrier_ok(dev))
			flags |= IFF_LOWER_UP;
		if (netif_dormant(dev))
			flags |= IFF_DORMANT;
	}

	return flags;
}
EXPORT_SYMBOL(dev_get_flags);

int __dev_change_flags(struct net_device *dev, unsigned int flags,
		       struct netlink_ext_ack *extack)
{
	unsigned int old_flags = dev->flags;
	int ret;

	ASSERT_RTNL();

	/*
	 *	Set the flags on our device.
	 */

	dev->flags = (flags & (IFF_DEBUG | IFF_NOTRAILERS | IFF_NOARP |
			       IFF_DYNAMIC | IFF_MULTICAST | IFF_PORTSEL |
			       IFF_AUTOMEDIA)) |
		     (dev->flags & (IFF_UP | IFF_VOLATILE | IFF_PROMISC |
				    IFF_ALLMULTI));

	/*
	 *	Load in the correct multicast list now the flags have changed.
	 */

	if ((old_flags ^ flags) & IFF_MULTICAST)
		dev_change_rx_flags(dev, IFF_MULTICAST);

	dev_set_rx_mode(dev);

	/*
	 *	Have we downed the interface. We handle IFF_UP ourselves
	 *	according to user attempts to set it, rather than blindly
	 *	setting it.
	 */

	ret = 0;
	if ((old_flags ^ flags) & IFF_UP) {
		if (old_flags & IFF_UP)
			__dev_close(dev);
		else
			ret = __dev_open(dev, extack);
	}

	if ((flags ^ dev->gflags) & IFF_PROMISC) {
		int inc = (flags & IFF_PROMISC) ? 1 : -1;
		unsigned int old_flags = dev->flags;

		dev->gflags ^= IFF_PROMISC;

		if (__dev_set_promiscuity(dev, inc, false) >= 0)
			if (dev->flags != old_flags)
				dev_set_rx_mode(dev);
	}

	/* NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
	 * is important. Some (broken) drivers set IFF_PROMISC, when
	 * IFF_ALLMULTI is requested not asking us and not reporting.
	 */
	if ((flags ^ dev->gflags) & IFF_ALLMULTI) {
		int inc = (flags & IFF_ALLMULTI) ? 1 : -1;

		dev->gflags ^= IFF_ALLMULTI;
		__dev_set_allmulti(dev, inc, false);
	}

	return ret;
}

void __dev_notify_flags(struct net_device *dev, unsigned int old_flags,
			unsigned int gchanges)
{
	unsigned int changes = dev->flags ^ old_flags;

	if (gchanges)
		rtmsg_ifinfo(RTM_NEWLINK, dev, gchanges, GFP_ATOMIC);

	if (changes & IFF_UP) {
		if (dev->flags & IFF_UP)
			call_netdevice_notifiers(NETDEV_UP, dev);
		else
			call_netdevice_notifiers(NETDEV_DOWN, dev);
	}

	if (dev->flags & IFF_UP &&
	    (changes & ~(IFF_UP | IFF_PROMISC | IFF_ALLMULTI | IFF_VOLATILE))) {
		struct netdev_notifier_change_info change_info = {
			.info = {
				.dev = dev,
			},
			.flags_changed = changes,
		};

		call_netdevice_notifiers_info(NETDEV_CHANGE, &change_info.info);
	}
}

/**
 *	dev_change_flags - change device settings
 *	@dev: device
 *	@flags: device state flags
 *	@extack: netlink extended ack
 *
 *	Change settings on device based state flags. The flags are
 *	in the userspace exported format.
 */
int dev_change_flags(struct net_device *dev, unsigned int flags,
		     struct netlink_ext_ack *extack)
{
	int ret;
	unsigned int changes, old_flags = dev->flags, old_gflags = dev->gflags;

	ret = __dev_change_flags(dev, flags, extack);
	if (ret < 0)
		return ret;

	changes = (old_flags ^ dev->flags) | (old_gflags ^ dev->gflags);
	__dev_notify_flags(dev, old_flags, changes);
	return ret;
}
EXPORT_SYMBOL(dev_change_flags);

int __dev_set_mtu(struct net_device *dev, int new_mtu)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (ops->ndo_change_mtu)
		return ops->ndo_change_mtu(dev, new_mtu);

	/* Pairs with all the lockless reads of dev->mtu in the stack */
	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}
EXPORT_SYMBOL(__dev_set_mtu);

int dev_validate_mtu(struct net_device *dev, int new_mtu,
		     struct netlink_ext_ack *extack)
{
	/* MTU must be positive, and in range */
	if (new_mtu < 0 || new_mtu < dev->min_mtu) {
		NL_SET_ERR_MSG(extack, "mtu less than device minimum");
		return -EINVAL;
	}

	if (dev->max_mtu > 0 && new_mtu > dev->max_mtu) {
		NL_SET_ERR_MSG(extack, "mtu greater than device maximum");
		return -EINVAL;
	}
	return 0;
}

/**
 *	dev_set_mtu_ext - Change maximum transfer unit
 *	@dev: device
 *	@new_mtu: new transfer unit
 *	@extack: netlink extended ack
 *
 *	Change the maximum transfer size of the network device.
 */
int dev_set_mtu_ext(struct net_device *dev, int new_mtu,
		    struct netlink_ext_ack *extack)
{
	int err, orig_mtu;

	if (new_mtu == dev->mtu)
		return 0;

	err = dev_validate_mtu(dev, new_mtu, extack);
	if (err)
		return err;

	if (!netif_device_present(dev))
		return -ENODEV;

	err = call_netdevice_notifiers(NETDEV_PRECHANGEMTU, dev);
	err = notifier_to_errno(err);
	if (err)
		return err;

	orig_mtu = dev->mtu;
	err = __dev_set_mtu(dev, new_mtu);

	if (!err) {
		err = call_netdevice_notifiers_mtu(NETDEV_CHANGEMTU, dev,
						   orig_mtu);
		err = notifier_to_errno(err);
		if (err) {
			/* setting mtu back and notifying everyone again,
			 * so that they have a chance to revert changes.
			 */
			__dev_set_mtu(dev, orig_mtu);
			call_netdevice_notifiers_mtu(NETDEV_CHANGEMTU, dev,
						     new_mtu);
		}
	}
	return err;
}

int dev_set_mtu(struct net_device *dev, int new_mtu)
{
	struct netlink_ext_ack extack;
	int err;

	memset(&extack, 0, sizeof(extack));
	err = dev_set_mtu_ext(dev, new_mtu, &extack);
	if (err && extack._msg)
		net_err_ratelimited("%s: %s\n", dev->name, extack._msg);
	return err;
}
EXPORT_SYMBOL(dev_set_mtu);

/**
 *	dev_change_tx_queue_len - Change TX queue length of a netdevice
 *	@dev: device
 *	@new_len: new tx queue length
 */
int dev_change_tx_queue_len(struct net_device *dev, unsigned long new_len)
{
	unsigned int orig_len = dev->tx_queue_len;
	int res;

	if (new_len != (unsigned int)new_len)
		return -ERANGE;

	if (new_len != orig_len) {
		dev->tx_queue_len = new_len;
		res = call_netdevice_notifiers(NETDEV_CHANGE_TX_QUEUE_LEN, dev);
		res = notifier_to_errno(res);
		if (res)
			goto err_rollback;
		res = dev_qdisc_change_tx_queue_len(dev);
		if (res)
			goto err_rollback;
	}

	return 0;

err_rollback:
	netdev_err(dev, "refused to change device tx_queue_len\n");
	dev->tx_queue_len = orig_len;
	return res;
}

/**
 *	dev_set_group - Change group this device belongs to
 *	@dev: device
 *	@new_group: group this device should belong to
 */
void dev_set_group(struct net_device *dev, int new_group)
{
	dev->group = new_group;
}
EXPORT_SYMBOL(dev_set_group);

/**
 *	dev_pre_changeaddr_notify - Call NETDEV_PRE_CHANGEADDR.
 *	@dev: device
 *	@addr: new address
 *	@extack: netlink extended ack
 */
int dev_pre_changeaddr_notify(struct net_device *dev, const char *addr,
			      struct netlink_ext_ack *extack)
{
	struct netdev_notifier_pre_changeaddr_info info = {
		.info.dev = dev,
		.info.extack = extack,
		.dev_addr = addr,
	};
	int rc;

	rc = call_netdevice_notifiers_info(NETDEV_PRE_CHANGEADDR, &info.info);
	return notifier_to_errno(rc);
}
EXPORT_SYMBOL(dev_pre_changeaddr_notify);

/**
 *	dev_set_mac_address - Change Media Access Control Address
 *	@dev: device
 *	@sa: new address
 *	@extack: netlink extended ack
 *
 *	Change the hardware (MAC) address of the device
 */
int dev_set_mac_address(struct net_device *dev, struct sockaddr *sa,
			struct netlink_ext_ack *extack)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (!ops->ndo_set_mac_address)
		return -EOPNOTSUPP;
	if (sa->sa_family != dev->type)
		return -EINVAL;
	if (!netif_device_present(dev))
		return -ENODEV;
	err = dev_pre_changeaddr_notify(dev, sa->sa_data, extack);
	if (err)
		return err;
	err = ops->ndo_set_mac_address(dev, sa);
	if (err)
		return err;
	dev->addr_assign_type = NET_ADDR_SET;
	call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	add_device_randomness(dev->dev_addr, dev->addr_len);
	return 0;
}
EXPORT_SYMBOL(dev_set_mac_address);

static DECLARE_RWSEM(dev_addr_sem);

int dev_set_mac_address_user(struct net_device *dev, struct sockaddr *sa,
			     struct netlink_ext_ack *extack)
{
	int ret;

	down_write(&dev_addr_sem);
	ret = dev_set_mac_address(dev, sa, extack);
	up_write(&dev_addr_sem);
	return ret;
}
EXPORT_SYMBOL(dev_set_mac_address_user);

int dev_get_mac_address(struct sockaddr *sa, struct net *net, char *dev_name)
{
	size_t size = sizeof(sa->sa_data);
	struct net_device *dev;
	int ret = 0;

	down_read(&dev_addr_sem);
	rcu_read_lock();

	dev = dev_get_by_name_rcu(net, dev_name);
	if (!dev) {
		ret = -ENODEV;
		goto unlock;
	}
	if (!dev->addr_len)
		memset(sa->sa_data, 0, size);
	else
		memcpy(sa->sa_data, dev->dev_addr,
		       min_t(size_t, size, dev->addr_len));
	sa->sa_family = dev->type;

unlock:
	rcu_read_unlock();
	up_read(&dev_addr_sem);
	return ret;
}
EXPORT_SYMBOL(dev_get_mac_address);

/**
 *	dev_change_carrier - Change device carrier
 *	@dev: device
 *	@new_carrier: new value
 *
 *	Change device carrier
 */
int dev_change_carrier(struct net_device *dev, bool new_carrier)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_change_carrier)
		return -EOPNOTSUPP;
	if (!netif_device_present(dev))
		return -ENODEV;
	return ops->ndo_change_carrier(dev, new_carrier);
}
EXPORT_SYMBOL(dev_change_carrier);

/**
 *	dev_get_phys_port_id - Get device physical port ID
 *	@dev: device
 *	@ppid: port ID
 *
 *	Get device physical port ID
 */
int dev_get_phys_port_id(struct net_device *dev,
			 struct netdev_phys_item_id *ppid)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_get_phys_port_id)
		return -EOPNOTSUPP;
	return ops->ndo_get_phys_port_id(dev, ppid);
}
EXPORT_SYMBOL(dev_get_phys_port_id);

/**
 *	dev_get_phys_port_name - Get device physical port name
 *	@dev: device
 *	@name: port name
 *	@len: limit of bytes to copy to name
 *
 *	Get device physical port name
 */
int dev_get_phys_port_name(struct net_device *dev,
			   char *name, size_t len)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (ops->ndo_get_phys_port_name) {
		err = ops->ndo_get_phys_port_name(dev, name, len);
		if (err != -EOPNOTSUPP)
			return err;
	}
	return devlink_compat_phys_port_name_get(dev, name, len);
}
EXPORT_SYMBOL(dev_get_phys_port_name);

/**
 *	dev_get_port_parent_id - Get the device's port parent identifier
 *	@dev: network device
 *	@ppid: pointer to a storage for the port's parent identifier
 *	@recurse: allow/disallow recursion to lower devices
 *
 *	Get the devices's port parent identifier
 */
int dev_get_port_parent_id(struct net_device *dev,
			   struct netdev_phys_item_id *ppid,
			   bool recurse)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	struct netdev_phys_item_id first = { };
	struct net_device *lower_dev;
	struct list_head *iter;
	int err;

	if (ops->ndo_get_port_parent_id) {
		err = ops->ndo_get_port_parent_id(dev, ppid);
		if (err != -EOPNOTSUPP)
			return err;
	}

	err = devlink_compat_switch_id_get(dev, ppid);
	if (!recurse || err != -EOPNOTSUPP)
		return err;

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = dev_get_port_parent_id(lower_dev, ppid, true);
		if (err)
			break;
		if (!first.id_len)
			first = *ppid;
		else if (memcmp(&first, ppid, sizeof(*ppid)))
			return -EOPNOTSUPP;
	}

	return err;
}
EXPORT_SYMBOL(dev_get_port_parent_id);

/**
 *	netdev_port_same_parent_id - Indicate if two network devices have
 *	the same port parent identifier
 *	@a: first network device
 *	@b: second network device
 */
bool netdev_port_same_parent_id(struct net_device *a, struct net_device *b)
{
	struct netdev_phys_item_id a_id = { };
	struct netdev_phys_item_id b_id = { };

	if (dev_get_port_parent_id(a, &a_id, true) ||
	    dev_get_port_parent_id(b, &b_id, true))
		return false;

	return netdev_phys_item_id_same(&a_id, &b_id);
}
EXPORT_SYMBOL(netdev_port_same_parent_id);

/**
 *	dev_change_proto_down - update protocol port state information
 *	@dev: device
 *	@proto_down: new value
 *
 *	This info can be used by switch drivers to set the phys state of the
 *	port.
 */
int dev_change_proto_down(struct net_device *dev, bool proto_down)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (!ops->ndo_change_proto_down)
		return -EOPNOTSUPP;
	if (!netif_device_present(dev))
		return -ENODEV;
	return ops->ndo_change_proto_down(dev, proto_down);
}
EXPORT_SYMBOL(dev_change_proto_down);

/**
 *	dev_change_proto_down_generic - generic implementation for
 * 	ndo_change_proto_down that sets carrier according to
 * 	proto_down.
 *
 *	@dev: device
 *	@proto_down: new value
 */
int dev_change_proto_down_generic(struct net_device *dev, bool proto_down)
{
	if (proto_down)
		netif_carrier_off(dev);
	else
		netif_carrier_on(dev);
	dev->proto_down = proto_down;
	return 0;
}
EXPORT_SYMBOL(dev_change_proto_down_generic);

/**
 *	dev_change_proto_down_reason - proto down reason
 *
 *	@dev: device
 *	@mask: proto down mask
 *	@value: proto down value
 */
void dev_change_proto_down_reason(struct net_device *dev, unsigned long mask,
				  u32 value)
{
	int b;

	if (!mask) {
		dev->proto_down_reason = value;
	} else {
		for_each_set_bit(b, &mask, 32) {
			if (value & (1 << b))
				dev->proto_down_reason |= BIT(b);
			else
				dev->proto_down_reason &= ~BIT(b);
		}
	}
}
EXPORT_SYMBOL(dev_change_proto_down_reason);

struct bpf_xdp_link {
	struct bpf_link link;
	struct net_device *dev; /* protected by rtnl_lock, no refcnt held */
	int flags;
};

static enum bpf_xdp_mode dev_xdp_mode(struct net_device *dev, u32 flags)
{
	if (flags & XDP_FLAGS_HW_MODE)
		return XDP_MODE_HW;
	if (flags & XDP_FLAGS_DRV_MODE)
		return XDP_MODE_DRV;
	if (flags & XDP_FLAGS_SKB_MODE)
		return XDP_MODE_SKB;
	return dev->netdev_ops->ndo_bpf ? XDP_MODE_DRV : XDP_MODE_SKB;
}

static bpf_op_t dev_xdp_bpf_op(struct net_device *dev, enum bpf_xdp_mode mode)
{
	switch (mode) {
	case XDP_MODE_SKB:
		return generic_xdp_install;
	case XDP_MODE_DRV:
	case XDP_MODE_HW:
		return dev->netdev_ops->ndo_bpf;
	default:
		return NULL;
	}
}

static struct bpf_xdp_link *dev_xdp_link(struct net_device *dev,
					 enum bpf_xdp_mode mode)
{
	return dev->xdp_state[mode].link;
}

static struct bpf_prog *dev_xdp_prog(struct net_device *dev,
				     enum bpf_xdp_mode mode)
{
	struct bpf_xdp_link *link = dev_xdp_link(dev, mode);

	if (link)
		return link->link.prog;
	return dev->xdp_state[mode].prog;
}

u8 dev_xdp_prog_count(struct net_device *dev)
{
	u8 count = 0;
	int i;

	for (i = 0; i < __MAX_XDP_MODE; i++)
		if (dev->xdp_state[i].prog || dev->xdp_state[i].link)
			count++;
	return count;
}
EXPORT_SYMBOL_GPL(dev_xdp_prog_count);

u32 dev_xdp_prog_id(struct net_device *dev, enum bpf_xdp_mode mode)
{
	struct bpf_prog *prog = dev_xdp_prog(dev, mode);

	return prog ? prog->aux->id : 0;
}

static void dev_xdp_set_link(struct net_device *dev, enum bpf_xdp_mode mode,
			     struct bpf_xdp_link *link)
{
	dev->xdp_state[mode].link = link;
	dev->xdp_state[mode].prog = NULL;
}

static void dev_xdp_set_prog(struct net_device *dev, enum bpf_xdp_mode mode,
			     struct bpf_prog *prog)
{
	dev->xdp_state[mode].link = NULL;
	dev->xdp_state[mode].prog = prog;
}

static int dev_xdp_install(struct net_device *dev, enum bpf_xdp_mode mode,
			   bpf_op_t bpf_op, struct netlink_ext_ack *extack,
			   u32 flags, struct bpf_prog *prog)
{
	struct netdev_bpf xdp;
	int err;

	memset(&xdp, 0, sizeof(xdp));
	xdp.command = mode == XDP_MODE_HW ? XDP_SETUP_PROG_HW : XDP_SETUP_PROG;
	xdp.extack = extack;
	xdp.flags = flags;
	xdp.prog = prog;

	/* Drivers assume refcnt is already incremented (i.e, prog pointer is
	 * "moved" into driver), so they don't increment it on their own, but
	 * they do decrement refcnt when program is detached or replaced.
	 * Given net_device also owns link/prog, we need to bump refcnt here
	 * to prevent drivers from underflowing it.
	 */
	if (prog)
		bpf_prog_inc(prog);
	err = bpf_op(dev, &xdp);
	if (err) {
		if (prog)
			bpf_prog_put(prog);
		return err;
	}

	if (mode != XDP_MODE_HW)
		bpf_prog_change_xdp(dev_xdp_prog(dev, mode), prog);

	return 0;
}

static void dev_xdp_uninstall(struct net_device *dev)
{
	struct bpf_xdp_link *link;
	struct bpf_prog *prog;
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;

	ASSERT_RTNL();

	for (mode = XDP_MODE_SKB; mode < __MAX_XDP_MODE; mode++) {
		prog = dev_xdp_prog(dev, mode);
		if (!prog)
			continue;

		bpf_op = dev_xdp_bpf_op(dev, mode);
		if (!bpf_op)
			continue;

		WARN_ON(dev_xdp_install(dev, mode, bpf_op, NULL, 0, NULL));

		/* auto-detach link from net device */
		link = dev_xdp_link(dev, mode);
		if (link)
			link->dev = NULL;
		else
			bpf_prog_put(prog);

		dev_xdp_set_link(dev, mode, NULL);
	}
}

static int dev_xdp_attach(struct net_device *dev, struct netlink_ext_ack *extack,
			  struct bpf_xdp_link *link, struct bpf_prog *new_prog,
			  struct bpf_prog *old_prog, u32 flags)
{
	unsigned int num_modes = hweight32(flags & XDP_FLAGS_MODES);
	struct bpf_prog *cur_prog;
	struct net_device *upper;
	struct list_head *iter;
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;
	int err;

	ASSERT_RTNL();

	/* either link or prog attachment, never both */
	if (link && (new_prog || old_prog))
		return -EINVAL;
	/* link supports only XDP mode flags */
	if (link && (flags & ~XDP_FLAGS_MODES)) {
		NL_SET_ERR_MSG(extack, "Invalid XDP flags for BPF link attachment");
		return -EINVAL;
	}
	/* just one XDP mode bit should be set, zero defaults to drv/skb mode */
	if (num_modes > 1) {
		NL_SET_ERR_MSG(extack, "Only one XDP mode flag can be set");
		return -EINVAL;
	}
	/* avoid ambiguity if offload + drv/skb mode progs are both loaded */
	if (!num_modes && dev_xdp_prog_count(dev) > 1) {
		NL_SET_ERR_MSG(extack,
			       "More than one program loaded, unset mode is ambiguous");
		return -EINVAL;
	}
	/* old_prog != NULL implies XDP_FLAGS_REPLACE is set */
	if (old_prog && !(flags & XDP_FLAGS_REPLACE)) {
		NL_SET_ERR_MSG(extack, "XDP_FLAGS_REPLACE is not specified");
		return -EINVAL;
	}

	mode = dev_xdp_mode(dev, flags);
	/* can't replace attached link */
	if (dev_xdp_link(dev, mode)) {
		NL_SET_ERR_MSG(extack, "Can't replace active BPF XDP link");
		return -EBUSY;
	}

	/* don't allow if an upper device already has a program */
	netdev_for_each_upper_dev_rcu(dev, upper, iter) {
		if (dev_xdp_prog_count(upper) > 0) {
			NL_SET_ERR_MSG(extack, "Cannot attach when an upper device already has a program");
			return -EEXIST;
		}
	}

	cur_prog = dev_xdp_prog(dev, mode);
	/* can't replace attached prog with link */
	if (link && cur_prog) {
		NL_SET_ERR_MSG(extack, "Can't replace active XDP program with BPF link");
		return -EBUSY;
	}
	if ((flags & XDP_FLAGS_REPLACE) && cur_prog != old_prog) {
		NL_SET_ERR_MSG(extack, "Active program does not match expected");
		return -EEXIST;
	}

	/* put effective new program into new_prog */
	if (link)
		new_prog = link->link.prog;

	if (new_prog) {
		bool offload = mode == XDP_MODE_HW;
		enum bpf_xdp_mode other_mode = mode == XDP_MODE_SKB
					       ? XDP_MODE_DRV : XDP_MODE_SKB;

		if ((flags & XDP_FLAGS_UPDATE_IF_NOEXIST) && cur_prog) {
			NL_SET_ERR_MSG(extack, "XDP program already attached");
			return -EBUSY;
		}
		if (!offload && dev_xdp_prog(dev, other_mode)) {
			NL_SET_ERR_MSG(extack, "Native and generic XDP can't be active at the same time");
			return -EEXIST;
		}
		if (!offload && bpf_prog_is_dev_bound(new_prog->aux)) {
			NL_SET_ERR_MSG(extack, "Using device-bound program without HW_MODE flag is not supported");
			return -EINVAL;
		}
		if (new_prog->expected_attach_type == BPF_XDP_DEVMAP) {
			NL_SET_ERR_MSG(extack, "BPF_XDP_DEVMAP programs can not be attached to a device");
			return -EINVAL;
		}
		if (new_prog->expected_attach_type == BPF_XDP_CPUMAP) {
			NL_SET_ERR_MSG(extack, "BPF_XDP_CPUMAP programs can not be attached to a device");
			return -EINVAL;
		}
	}

	/* don't call drivers if the effective program didn't change */
	if (new_prog != cur_prog) {
		bpf_op = dev_xdp_bpf_op(dev, mode);
		if (!bpf_op) {
			NL_SET_ERR_MSG(extack, "Underlying driver does not support XDP in native mode");
			return -EOPNOTSUPP;
		}

		err = dev_xdp_install(dev, mode, bpf_op, extack, flags, new_prog);
		if (err)
			return err;
	}

	if (link)
		dev_xdp_set_link(dev, mode, link);
	else
		dev_xdp_set_prog(dev, mode, new_prog);
	if (cur_prog)
		bpf_prog_put(cur_prog);

	return 0;
}

static int dev_xdp_attach_link(struct net_device *dev,
			       struct netlink_ext_ack *extack,
			       struct bpf_xdp_link *link)
{
	return dev_xdp_attach(dev, extack, link, NULL, NULL, link->flags);
}

static int dev_xdp_detach_link(struct net_device *dev,
			       struct netlink_ext_ack *extack,
			       struct bpf_xdp_link *link)
{
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;

	ASSERT_RTNL();

	mode = dev_xdp_mode(dev, link->flags);
	if (dev_xdp_link(dev, mode) != link)
		return -EINVAL;

	bpf_op = dev_xdp_bpf_op(dev, mode);
	WARN_ON(dev_xdp_install(dev, mode, bpf_op, NULL, 0, NULL));
	dev_xdp_set_link(dev, mode, NULL);
	return 0;
}

static void bpf_xdp_link_release(struct bpf_link *link)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);

	rtnl_lock();

	/* if racing with net_device's tear down, xdp_link->dev might be
	 * already NULL, in which case link was already auto-detached
	 */
	if (xdp_link->dev) {
		WARN_ON(dev_xdp_detach_link(xdp_link->dev, NULL, xdp_link));
		xdp_link->dev = NULL;
	}

	rtnl_unlock();
}

static int bpf_xdp_link_detach(struct bpf_link *link)
{
	bpf_xdp_link_release(link);
	return 0;
}

static void bpf_xdp_link_dealloc(struct bpf_link *link)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);

	kfree(xdp_link);
}

static void bpf_xdp_link_show_fdinfo(const struct bpf_link *link,
				     struct seq_file *seq)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);
	u32 ifindex = 0;

	rtnl_lock();
	if (xdp_link->dev)
		ifindex = xdp_link->dev->ifindex;
	rtnl_unlock();

	seq_printf(seq, "ifindex:\t%u\n", ifindex);
}

static int bpf_xdp_link_fill_link_info(const struct bpf_link *link,
				       struct bpf_link_info *info)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);
	u32 ifindex = 0;

	rtnl_lock();
	if (xdp_link->dev)
		ifindex = xdp_link->dev->ifindex;
	rtnl_unlock();

	info->xdp.ifindex = ifindex;
	return 0;
}

static int bpf_xdp_link_update(struct bpf_link *link, struct bpf_prog *new_prog,
			       struct bpf_prog *old_prog)
{
	struct bpf_xdp_link *xdp_link = container_of(link, struct bpf_xdp_link, link);
	enum bpf_xdp_mode mode;
	bpf_op_t bpf_op;
	int err = 0;

	rtnl_lock();

	/* link might have been auto-released already, so fail */
	if (!xdp_link->dev) {
		err = -ENOLINK;
		goto out_unlock;
	}

	if (old_prog && link->prog != old_prog) {
		err = -EPERM;
		goto out_unlock;
	}
	old_prog = link->prog;
	if (old_prog == new_prog) {
		/* no-op, don't disturb drivers */
		bpf_prog_put(new_prog);
		goto out_unlock;
	}

	mode = dev_xdp_mode(xdp_link->dev, xdp_link->flags);
	bpf_op = dev_xdp_bpf_op(xdp_link->dev, mode);
	err = dev_xdp_install(xdp_link->dev, mode, bpf_op, NULL,
			      xdp_link->flags, new_prog);
	if (err)
		goto out_unlock;

	old_prog = xchg(&link->prog, new_prog);
	bpf_prog_put(old_prog);

out_unlock:
	rtnl_unlock();
	return err;
}

static const struct bpf_link_ops bpf_xdp_link_lops = {
	.release = bpf_xdp_link_release,
	.dealloc = bpf_xdp_link_dealloc,
	.detach = bpf_xdp_link_detach,
	.show_fdinfo = bpf_xdp_link_show_fdinfo,
	.fill_link_info = bpf_xdp_link_fill_link_info,
	.update_prog = bpf_xdp_link_update,
};

int bpf_xdp_link_attach(const union bpf_attr *attr, struct bpf_prog *prog)
{
	struct net *net = current->nsproxy->net_ns;
	struct bpf_link_primer link_primer;
	struct bpf_xdp_link *link;
	struct net_device *dev;
	int err, fd;

	rtnl_lock();
	dev = dev_get_by_index(net, attr->link_create.target_ifindex);
	if (!dev) {
		rtnl_unlock();
		return -EINVAL;
	}

	link = kzalloc(sizeof(*link), GFP_USER);
	if (!link) {
		err = -ENOMEM;
		goto unlock;
	}

	bpf_link_init(&link->link, BPF_LINK_TYPE_XDP, &bpf_xdp_link_lops, prog);
	link->dev = dev;
	link->flags = attr->link_create.flags;

	err = bpf_link_prime(&link->link, &link_primer);
	if (err) {
		kfree(link);
		goto unlock;
	}

	err = dev_xdp_attach_link(dev, NULL, link);
	rtnl_unlock();

	if (err) {
		link->dev = NULL;
		bpf_link_cleanup(&link_primer);
		goto out_put_dev;
	}

	fd = bpf_link_settle(&link_primer);
	/* link itself doesn't hold dev's refcnt to not complicate shutdown */
	dev_put(dev);
	return fd;

unlock:
	rtnl_unlock();

out_put_dev:
	dev_put(dev);
	return err;
}

/**
 *	dev_change_xdp_fd - set or clear a bpf program for a device rx path
 *	@dev: device
 *	@extack: netlink extended ack
 *	@fd: new program fd or negative value to clear
 *	@expected_fd: old program fd that userspace expects to replace or clear
 *	@flags: xdp-related flags
 *
 *	Set or clear a bpf program for a device
 */
int dev_change_xdp_fd(struct net_device *dev, struct netlink_ext_ack *extack,
		      int fd, int expected_fd, u32 flags)
{
	enum bpf_xdp_mode mode = dev_xdp_mode(dev, flags);
	struct bpf_prog *new_prog = NULL, *old_prog = NULL;
	int err;

	ASSERT_RTNL();

	if (fd >= 0) {
		new_prog = bpf_prog_get_type_dev(fd, BPF_PROG_TYPE_XDP,
						 mode != XDP_MODE_SKB);
		if (IS_ERR(new_prog))
			return PTR_ERR(new_prog);
	}

	if (expected_fd >= 0) {
		old_prog = bpf_prog_get_type_dev(expected_fd, BPF_PROG_TYPE_XDP,
						 mode != XDP_MODE_SKB);
		if (IS_ERR(old_prog)) {
			err = PTR_ERR(old_prog);
			old_prog = NULL;
			goto err_out;
		}
	}

	err = dev_xdp_attach(dev, extack, NULL, new_prog, old_prog, flags);

err_out:
	if (err && new_prog)
		bpf_prog_put(new_prog);
	if (old_prog)
		bpf_prog_put(old_prog);
	return err;
}

/**
 *	dev_new_index	-	allocate an ifindex
 *	@net: the applicable net namespace
 *
 *	Returns a suitable unique value for a new device interface
 *	number.  The caller must hold the rtnl semaphore or the
 *	dev_base_lock to be sure it remains unique.
 */
static int dev_new_index(struct net *net)
{
	int ifindex = net->ifindex;

	for (;;) {
		if (++ifindex <= 0)
			ifindex = 1;
		if (!__dev_get_by_index(net, ifindex))
			return net->ifindex = ifindex;
	}
}

/* Delayed registration/unregisteration */
static LIST_HEAD(net_todo_list);
DECLARE_WAIT_QUEUE_HEAD(netdev_unregistering_wq);

static void net_set_todo(struct net_device *dev)
{
	list_add_tail(&dev->todo_list, &net_todo_list);
	dev_net(dev)->dev_unreg_count++;
}

static netdev_features_t netdev_sync_upper_features(struct net_device *lower,
	struct net_device *upper, netdev_features_t features)
{
	netdev_features_t upper_disables = NETIF_F_UPPER_DISABLES;
	netdev_features_t feature;
	int feature_bit;

	for_each_netdev_feature(upper_disables, feature_bit) {
		feature = __NETIF_F_BIT(feature_bit);
		if (!(upper->wanted_features & feature)
		    && (features & feature)) {
			netdev_dbg(lower, "Dropping feature %pNF, upper dev %s has it off.\n",
				   &feature, upper->name);
			features &= ~feature;
		}
	}

	return features;
}

static void netdev_sync_lower_features(struct net_device *upper,
	struct net_device *lower, netdev_features_t features)
{
	netdev_features_t upper_disables = NETIF_F_UPPER_DISABLES;
	netdev_features_t feature;
	int feature_bit;

	for_each_netdev_feature(upper_disables, feature_bit) {
		feature = __NETIF_F_BIT(feature_bit);
		if (!(features & feature) && (lower->features & feature)) {
			netdev_dbg(upper, "Disabling feature %pNF on lower dev %s.\n",
				   &feature, lower->name);
			lower->wanted_features &= ~feature;
			__netdev_update_features(lower);

			if (unlikely(lower->features & feature))
				netdev_WARN(upper, "failed to disable %pNF on %s!\n",
					    &feature, lower->name);
			else
				netdev_features_change(lower);
		}
	}
}

static netdev_features_t netdev_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	/* Fix illegal checksum combinations */
	if ((features & NETIF_F_HW_CSUM) &&
	    (features & (NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM))) {
		netdev_warn(dev, "mixed HW and IP checksum settings.\n");
		features &= ~(NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM);
	}

	/* TSO requires that SG is present as well. */
	if ((features & NETIF_F_ALL_TSO) && !(features & NETIF_F_SG)) {
		netdev_dbg(dev, "Dropping TSO features since no SG feature.\n");
		features &= ~NETIF_F_ALL_TSO;
	}

	if ((features & NETIF_F_TSO) && !(features & NETIF_F_HW_CSUM) &&
					!(features & NETIF_F_IP_CSUM)) {
		netdev_dbg(dev, "Dropping TSO features since no CSUM feature.\n");
		features &= ~NETIF_F_TSO;
		features &= ~NETIF_F_TSO_ECN;
	}

	if ((features & NETIF_F_TSO6) && !(features & NETIF_F_HW_CSUM) &&
					 !(features & NETIF_F_IPV6_CSUM)) {
		netdev_dbg(dev, "Dropping TSO6 features since no CSUM feature.\n");
		features &= ~NETIF_F_TSO6;
	}

	/* TSO with IPv4 ID mangling requires IPv4 TSO be enabled */
	if ((features & NETIF_F_TSO_MANGLEID) && !(features & NETIF_F_TSO))
		features &= ~NETIF_F_TSO_MANGLEID;

	/* TSO ECN requires that TSO is present as well. */
	if ((features & NETIF_F_ALL_TSO) == NETIF_F_TSO_ECN)
		features &= ~NETIF_F_TSO_ECN;

	/* Software GSO depends on SG. */
	if ((features & NETIF_F_GSO) && !(features & NETIF_F_SG)) {
		netdev_dbg(dev, "Dropping NETIF_F_GSO since no SG feature.\n");
		features &= ~NETIF_F_GSO;
	}

	/* GSO partial features require GSO partial be set */
	if ((features & dev->gso_partial_features) &&
	    !(features & NETIF_F_GSO_PARTIAL)) {
		netdev_dbg(dev,
			   "Dropping partially supported GSO features since no GSO partial.\n");
		features &= ~dev->gso_partial_features;
	}

	if (!(features & NETIF_F_RXCSUM)) {
		/* NETIF_F_GRO_HW implies doing RXCSUM since every packet
		 * successfully merged by hardware must also have the
		 * checksum verified by hardware.  If the user does not
		 * want to enable RXCSUM, logically, we should disable GRO_HW.
		 */
		if (features & NETIF_F_GRO_HW) {
			netdev_dbg(dev, "Dropping NETIF_F_GRO_HW since no RXCSUM feature.\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	/* LRO/HW-GRO features cannot be combined with RX-FCS */
	if (features & NETIF_F_RXFCS) {
		if (features & NETIF_F_LRO) {
			netdev_dbg(dev, "Dropping LRO feature since RX-FCS is requested.\n");
			features &= ~NETIF_F_LRO;
		}

		if (features & NETIF_F_GRO_HW) {
			netdev_dbg(dev, "Dropping HW-GRO feature since RX-FCS is requested.\n");
			features &= ~NETIF_F_GRO_HW;
		}
	}

	if ((features & NETIF_F_GRO_HW) && (features & NETIF_F_LRO)) {
		netdev_dbg(dev, "Dropping LRO feature since HW-GRO is requested.\n");
		features &= ~NETIF_F_LRO;
	}

	if (features & NETIF_F_HW_TLS_TX) {
		bool ip_csum = (features & (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM)) ==
			(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);
		bool hw_csum = features & NETIF_F_HW_CSUM;

		if (!ip_csum && !hw_csum) {
			netdev_dbg(dev, "Dropping TLS TX HW offload feature since no CSUM feature.\n");
			features &= ~NETIF_F_HW_TLS_TX;
		}
	}

	if ((features & NETIF_F_HW_TLS_RX) && !(features & NETIF_F_RXCSUM)) {
		netdev_dbg(dev, "Dropping TLS RX HW offload feature since no RXCSUM feature.\n");
		features &= ~NETIF_F_HW_TLS_RX;
	}

	return features;
}

int __netdev_update_features(struct net_device *dev)
{
	struct net_device *upper, *lower;
	netdev_features_t features;
	struct list_head *iter;
	int err = -1;

	ASSERT_RTNL();

	features = netdev_get_wanted_features(dev);

	if (dev->netdev_ops->ndo_fix_features)
		features = dev->netdev_ops->ndo_fix_features(dev, features);

	/* driver might be less strict about feature dependencies */
	features = netdev_fix_features(dev, features);

	/* some features can't be enabled if they're off on an upper device */
	netdev_for_each_upper_dev_rcu(dev, upper, iter)
		features = netdev_sync_upper_features(dev, upper, features);

	if (dev->features == features)
		goto sync_lower;

	netdev_dbg(dev, "Features changed: %pNF -> %pNF\n",
		&dev->features, &features);

	if (dev->netdev_ops->ndo_set_features)
		err = dev->netdev_ops->ndo_set_features(dev, features);
	else
		err = 0;

	if (unlikely(err < 0)) {
		netdev_err(dev,
			"set_features() failed (%d); wanted %pNF, left %pNF\n",
			err, &features, &dev->features);
		/* return non-0 since some features might have changed and
		 * it's better to fire a spurious notification than miss it
		 */
		return -1;
	}

sync_lower:
	/* some features must be disabled on lower devices when disabled
	 * on an upper device (think: bonding master or bridge)
	 */
	netdev_for_each_lower_dev(dev, lower, iter)
		netdev_sync_lower_features(dev, lower, features);

	if (!err) {
		netdev_features_t diff = features ^ dev->features;

		if (diff & NETIF_F_RX_UDP_TUNNEL_PORT) {
			/* udp_tunnel_{get,drop}_rx_info both need
			 * NETIF_F_RX_UDP_TUNNEL_PORT enabled on the
			 * device, or they won't do anything.
			 * Thus we need to update dev->features
			 * *before* calling udp_tunnel_get_rx_info,
			 * but *after* calling udp_tunnel_drop_rx_info.
			 */
			if (features & NETIF_F_RX_UDP_TUNNEL_PORT) {
				dev->features = features;
				udp_tunnel_get_rx_info(dev);
			} else {
				udp_tunnel_drop_rx_info(dev);
			}
		}

		if (diff & NETIF_F_HW_VLAN_CTAG_FILTER) {
			if (features & NETIF_F_HW_VLAN_CTAG_FILTER) {
				dev->features = features;
				err |= vlan_get_rx_ctag_filter_info(dev);
			} else {
				vlan_drop_rx_ctag_filter_info(dev);
			}
		}

		if (diff & NETIF_F_HW_VLAN_STAG_FILTER) {
			if (features & NETIF_F_HW_VLAN_STAG_FILTER) {
				dev->features = features;
				err |= vlan_get_rx_stag_filter_info(dev);
			} else {
				vlan_drop_rx_stag_filter_info(dev);
			}
		}

		dev->features = features;
	}

	return err < 0 ? 0 : 1;
}

/**
 *	netdev_update_features - recalculate device features
 *	@dev: the device to check
 *
 *	Recalculate dev->features set and send notifications if it
 *	has changed. Should be called after driver or hardware dependent
 *	conditions might have changed that influence the features.
 */
void netdev_update_features(struct net_device *dev)
{
	if (__netdev_update_features(dev))
		netdev_features_change(dev);
}
EXPORT_SYMBOL(netdev_update_features);

/**
 *	netdev_change_features - recalculate device features
 *	@dev: the device to check
 *
 *	Recalculate dev->features set and send notifications even
 *	if they have not changed. Should be called instead of
 *	netdev_update_features() if also dev->vlan_features might
 *	have changed to allow the changes to be propagated to stacked
 *	VLAN devices.
 */
void netdev_change_features(struct net_device *dev)
{
	__netdev_update_features(dev);
	netdev_features_change(dev);
}
EXPORT_SYMBOL(netdev_change_features);

/**
 *	netif_stacked_transfer_operstate -	transfer operstate
 *	@rootdev: the root or lower level device to transfer state from
 *	@dev: the device to transfer operstate to
 *
 *	Transfer operational state from root to device. This is normally
 *	called when a stacking relationship exists between the root
 *	device and the device(a leaf device).
 */
void netif_stacked_transfer_operstate(const struct net_device *rootdev,
					struct net_device *dev)
{
	if (rootdev->operstate == IF_OPER_DORMANT)
		netif_dormant_on(dev);
	else
		netif_dormant_off(dev);

	if (rootdev->operstate == IF_OPER_TESTING)
		netif_testing_on(dev);
	else
		netif_testing_off(dev);

	if (netif_carrier_ok(rootdev))
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);
}
EXPORT_SYMBOL(netif_stacked_transfer_operstate);

static int netif_alloc_rx_queues(struct net_device *dev)
{
	unsigned int i, count = dev->num_rx_queues;
	struct netdev_rx_queue *rx;
	size_t sz = count * sizeof(*rx);
	int err = 0;

	BUG_ON(count < 1);

	rx = kvzalloc(sz, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!rx)
		return -ENOMEM;

	dev->_rx = rx;

	for (i = 0; i < count; i++) {
		rx[i].dev = dev;

		/* XDP RX-queue setup */
		err = xdp_rxq_info_reg(&rx[i].xdp_rxq, dev, i, 0);
		if (err < 0)
			goto err_rxq_info;
	}
	return 0;

err_rxq_info:
	/* Rollback successful reg's and free other resources */
	while (i--)
		xdp_rxq_info_unreg(&rx[i].xdp_rxq);
	kvfree(dev->_rx);
	dev->_rx = NULL;
	return err;
}

static void netif_free_rx_queues(struct net_device *dev)
{
	unsigned int i, count = dev->num_rx_queues;

	/* netif_alloc_rx_queues alloc failed, resources have been unreg'ed */
	if (!dev->_rx)
		return;

	for (i = 0; i < count; i++)
		xdp_rxq_info_unreg(&dev->_rx[i].xdp_rxq);

	kvfree(dev->_rx);
}

static void netdev_init_one_queue(struct net_device *dev,
				  struct netdev_queue *queue, void *_unused)
{
	/* Initialize queue lock */
	spin_lock_init(&queue->_xmit_lock);
	netdev_set_xmit_lockdep_class(&queue->_xmit_lock, dev->type);
	queue->xmit_lock_owner = -1;
	netdev_queue_numa_node_write(queue, NUMA_NO_NODE);
	queue->dev = dev;
#ifdef CONFIG_BQL
	dql_init(&queue->dql, HZ);
#endif
}

static void netif_free_tx_queues(struct net_device *dev)
{
	kvfree(dev->_tx);
}

static int netif_alloc_netdev_queues(struct net_device *dev)
{
	unsigned int count = dev->num_tx_queues;
	struct netdev_queue *tx;
	size_t sz = count * sizeof(*tx);

	if (count < 1 || count > 0xffff)
		return -EINVAL;

	tx = kvzalloc(sz, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!tx)
		return -ENOMEM;

	dev->_tx = tx;

	netdev_for_each_tx_queue(dev, netdev_init_one_queue, NULL);
	spin_lock_init(&dev->tx_global_lock);

	return 0;
}

void netif_tx_stop_all_queues(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		netif_tx_stop_queue(txq);
	}
}
EXPORT_SYMBOL(netif_tx_stop_all_queues);

/**
 *	register_netdevice	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	Callers must hold the rtnl semaphore. You may want
 *	register_netdev() instead of this.
 *
 *	BUGS:
 *	The locking appears insufficient to guarantee two parallel registers
 *	will not get the same name.
 */

int register_netdevice(struct net_device *dev)
{
	int ret;
	struct net *net = dev_net(dev);

	BUILD_BUG_ON(sizeof(netdev_features_t) * BITS_PER_BYTE <
		     NETDEV_FEATURE_COUNT);
	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	might_sleep();

	/* When net_device's are persistent, this will be fatal. */
	BUG_ON(dev->reg_state != NETREG_UNINITIALIZED);
	BUG_ON(!net);

	ret = ethtool_check_ops(dev->ethtool_ops);
	if (ret)
		return ret;

	spin_lock_init(&dev->addr_list_lock);
	netdev_set_addr_lockdep_class(dev);

	ret = dev_get_valid_name(net, dev, dev->name);
	if (ret < 0)
		goto out;

	ret = -ENOMEM;
	dev->name_node = netdev_name_node_head_alloc(dev);
	if (!dev->name_node)
		goto out;

	/* Init, if this function is available */
	if (dev->netdev_ops->ndo_init) {
		ret = dev->netdev_ops->ndo_init(dev);
		if (ret) {
			if (ret > 0)
				ret = -EIO;
			goto err_free_name;
		}
	}

	if (((dev->hw_features | dev->features) &
	     NETIF_F_HW_VLAN_CTAG_FILTER) &&
	    (!dev->netdev_ops->ndo_vlan_rx_add_vid ||
	     !dev->netdev_ops->ndo_vlan_rx_kill_vid)) {
		netdev_WARN(dev, "Buggy VLAN acceleration in driver!\n");
		ret = -EINVAL;
		goto err_uninit;
	}

	ret = -EBUSY;
	if (!dev->ifindex)
		dev->ifindex = dev_new_index(net);
	else if (__dev_get_by_index(net, dev->ifindex))
		goto err_uninit;

	/* Transfer changeable features to wanted_features and enable
	 * software offloads (GSO and GRO).
	 */
	dev->hw_features |= (NETIF_F_SOFT_FEATURES | NETIF_F_SOFT_FEATURES_OFF);
	dev->features |= NETIF_F_SOFT_FEATURES;

	if (dev->udp_tunnel_nic_info) {
		dev->features |= NETIF_F_RX_UDP_TUNNEL_PORT;
		dev->hw_features |= NETIF_F_RX_UDP_TUNNEL_PORT;
	}

	dev->wanted_features = dev->features & dev->hw_features;

	if (!(dev->flags & IFF_LOOPBACK))
		dev->hw_features |= NETIF_F_NOCACHE_COPY;

	/* If IPv4 TCP segmentation offload is supported we should also
	 * allow the device to enable segmenting the frame with the option
	 * of ignoring a static IP ID value.  This doesn't enable the
	 * feature itself but allows the user to enable it later.
	 */
	if (dev->hw_features & NETIF_F_TSO)
		dev->hw_features |= NETIF_F_TSO_MANGLEID;
	if (dev->vlan_features & NETIF_F_TSO)
		dev->vlan_features |= NETIF_F_TSO_MANGLEID;
	if (dev->mpls_features & NETIF_F_TSO)
		dev->mpls_features |= NETIF_F_TSO_MANGLEID;
	if (dev->hw_enc_features & NETIF_F_TSO)
		dev->hw_enc_features |= NETIF_F_TSO_MANGLEID;

	/* Make NETIF_F_HIGHDMA inheritable to VLAN devices.
	 */
	dev->vlan_features |= NETIF_F_HIGHDMA;

	/* Make NETIF_F_SG inheritable to tunnel devices.
	 */
	dev->hw_enc_features |= NETIF_F_SG | NETIF_F_GSO_PARTIAL;

	/* Make NETIF_F_SG inheritable to MPLS.
	 */
	dev->mpls_features |= NETIF_F_SG;

	ret = call_netdevice_notifiers(NETDEV_POST_INIT, dev);
	ret = notifier_to_errno(ret);
	if (ret)
		goto err_uninit;

	ret = netdev_register_kobject(dev);
	if (ret) {
		dev->reg_state = NETREG_UNREGISTERED;
		goto err_uninit;
	}
	dev->reg_state = NETREG_REGISTERED;

	__netdev_update_features(dev);

	/*
	 *	Default initial state at registry is that the
	 *	device is present.
	 */

	set_bit(__LINK_STATE_PRESENT, &dev->state);

	linkwatch_init_dev(dev);

	dev_init_scheduler(dev);
	dev_hold(dev);
	list_netdevice(dev);
	add_device_randomness(dev->dev_addr, dev->addr_len);

	/* If the device has permanent device address, driver should
	 * set dev_addr and also addr_assign_type should be set to
	 * NET_ADDR_PERM (default value).
	 */
	if (dev->addr_assign_type == NET_ADDR_PERM)
		memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	/* Notify protocols, that a new device appeared. */
	ret = call_netdevice_notifiers(NETDEV_REGISTER, dev);
	ret = notifier_to_errno(ret);
	if (ret) {
		/* Expect explicit free_netdev() on failure */
		dev->needs_free_netdev = false;
		unregister_netdevice_queue(dev, NULL);
		goto out;
	}
	/*
	 *	Prevent userspace races by waiting until the network
	 *	device is fully setup before sending notifications.
	 */
	if (!dev->rtnl_link_ops ||
	    dev->rtnl_link_state == RTNL_LINK_INITIALIZED)
		rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U, GFP_KERNEL);

out:
	return ret;

err_uninit:
	if (dev->netdev_ops->ndo_uninit)
		dev->netdev_ops->ndo_uninit(dev);
	if (dev->priv_destructor)
		dev->priv_destructor(dev);
err_free_name:
	netdev_name_node_free(dev->name_node);
	goto out;
}
EXPORT_SYMBOL(register_netdevice);

/**
 *	init_dummy_netdev	- init a dummy network device for NAPI
 *	@dev: device to init
 *
 *	This takes a network device structure and initialize the minimum
 *	amount of fields so it can be used to schedule NAPI polls without
 *	registering a full blown interface. This is to be used by drivers
 *	that need to tie several hardware interfaces to a single NAPI
 *	poll scheduler due to HW limitations.
 */
int init_dummy_netdev(struct net_device *dev)
{
	/* Clear everything. Note we don't initialize spinlocks
	 * are they aren't supposed to be taken by any of the
	 * NAPI code and this dummy netdev is supposed to be
	 * only ever used for NAPI polls
	 */
	memset(dev, 0, sizeof(struct net_device));

	/* make sure we BUG if trying to hit standard
	 * register/unregister code path
	 */
	dev->reg_state = NETREG_DUMMY;

	/* NAPI wants this */
	INIT_LIST_HEAD(&dev->napi_list);

	/* a dummy interface is started by default */
	set_bit(__LINK_STATE_PRESENT, &dev->state);
	set_bit(__LINK_STATE_START, &dev->state);

	/* napi_busy_loop stats accounting wants this */
	dev_net_set(dev, &init_net);

	/* Note : We dont allocate pcpu_refcnt for dummy devices,
	 * because users of this 'device' dont need to change
	 * its refcount.
	 */

	return 0;
}
EXPORT_SYMBOL_GPL(init_dummy_netdev);


/**
 *	register_netdev	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	This is a wrapper around register_netdevice that takes the rtnl semaphore
 *	and expands the device name if you passed a format string to
 *	alloc_netdev.
 */
int register_netdev(struct net_device *dev)
{
	int err;

	if (rtnl_lock_killable())
		return -EINTR;
	err = register_netdevice(dev);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(register_netdev);

int netdev_refcnt_read(const struct net_device *dev)
{
#ifdef CONFIG_PCPU_DEV_REFCNT
	int i, refcnt = 0;

	for_each_possible_cpu(i)
		refcnt += *per_cpu_ptr(dev->pcpu_refcnt, i);
	return refcnt;
#else
	return refcount_read(&dev->dev_refcnt);
#endif
}
EXPORT_SYMBOL(netdev_refcnt_read);

int netdev_unregister_timeout_secs __read_mostly = 10;

#define WAIT_REFS_MIN_MSECS 1
#define WAIT_REFS_MAX_MSECS 250
/**
 * netdev_wait_allrefs - wait until all references are gone.
 * @dev: target net_device
 *
 * This is called when unregistering network devices.
 *
 * Any protocol or device that holds a reference should register
 * for netdevice notification, and cleanup and put back the
 * reference if they receive an UNREGISTER event.
 * We can get stuck here if buggy protocols don't correctly
 * call dev_put.
 */
static void netdev_wait_allrefs(struct net_device *dev)
{
	unsigned long rebroadcast_time, warning_time;
	int wait = 0, refcnt;

	linkwatch_forget_dev(dev);

	rebroadcast_time = warning_time = jiffies;
	refcnt = netdev_refcnt_read(dev);

	while (refcnt != 1) {
		if (time_after(jiffies, rebroadcast_time + 1 * HZ)) {
			rtnl_lock();

			/* Rebroadcast unregister notification */
			call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

			__rtnl_unlock();
			rcu_barrier();
			rtnl_lock();

			if (test_bit(__LINK_STATE_LINKWATCH_PENDING,
				     &dev->state)) {
				/* We must not have linkwatch events
				 * pending on unregister. If this
				 * happens, we simply run the queue
				 * unscheduled, resulting in a noop
				 * for this device.
				 */
				linkwatch_run_queue();
			}

			__rtnl_unlock();

			rebroadcast_time = jiffies;
		}

		if (!wait) {
			rcu_barrier();
			wait = WAIT_REFS_MIN_MSECS;
		} else {
			msleep(wait);
			wait = min(wait << 1, WAIT_REFS_MAX_MSECS);
		}

		refcnt = netdev_refcnt_read(dev);

		if (refcnt != 1 &&
		    time_after(jiffies, warning_time +
			       netdev_unregister_timeout_secs * HZ)) {
			pr_emerg("unregister_netdevice: waiting for %s to become free. Usage count = %d\n",
				 dev->name, refcnt);
			warning_time = jiffies;
		}
	}
}

/* The sequence is:
 *
 *	rtnl_lock();
 *	...
 *	register_netdevice(x1);
 *	register_netdevice(x2);
 *	...
 *	unregister_netdevice(y1);
 *	unregister_netdevice(y2);
 *      ...
 *	rtnl_unlock();
 *	free_netdev(y1);
 *	free_netdev(y2);
 *
 * We are invoked by rtnl_unlock().
 * This allows us to deal with problems:
 * 1) We can delete sysfs objects which invoke hotplug
 *    without deadlocking with linkwatch via keventd.
 * 2) Since we run with the RTNL semaphore not held, we can sleep
 *    safely in order to wait for the netdev refcnt to drop to zero.
 *
 * We must not return until all unregister events added during
 * the interval the lock was held have been completed.
 */
void netdev_run_todo(void)
{
	struct list_head list;
#ifdef CONFIG_LOCKDEP
	struct list_head unlink_list;

	list_replace_init(&net_unlink_list, &unlink_list);

	while (!list_empty(&unlink_list)) {
		struct net_device *dev = list_first_entry(&unlink_list,
							  struct net_device,
							  unlink_list);
		list_del_init(&dev->unlink_list);
		dev->nested_level = dev->lower_level - 1;
	}
#endif

	/* Snapshot list, allow later requests */
	list_replace_init(&net_todo_list, &list);

	__rtnl_unlock();


	/* Wait for rcu callbacks to finish before next phase */
	if (!list_empty(&list))
		rcu_barrier();

	while (!list_empty(&list)) {
		struct net_device *dev
			= list_first_entry(&list, struct net_device, todo_list);
		list_del(&dev->todo_list);

		if (unlikely(dev->reg_state != NETREG_UNREGISTERING)) {
			pr_err("network todo '%s' but state %d\n",
			       dev->name, dev->reg_state);
			dump_stack();
			continue;
		}

		dev->reg_state = NETREG_UNREGISTERED;

		netdev_wait_allrefs(dev);

		/* paranoia */
		BUG_ON(netdev_refcnt_read(dev) != 1);
		BUG_ON(!list_empty(&dev->ptype_all));
		BUG_ON(!list_empty(&dev->ptype_specific));
		WARN_ON(rcu_access_pointer(dev->ip_ptr));
		WARN_ON(rcu_access_pointer(dev->ip6_ptr));
#if IS_ENABLED(CONFIG_DECNET)
		WARN_ON(dev->dn_ptr);
#endif
		if (dev->priv_destructor)
			dev->priv_destructor(dev);
		if (dev->needs_free_netdev)
			free_netdev(dev);

		/* Report a network device has been unregistered */
		rtnl_lock();
		dev_net(dev)->dev_unreg_count--;
		__rtnl_unlock();
		wake_up(&netdev_unregistering_wq);

		/* Free network device */
		kobject_put(&dev->dev.kobj);
	}
}

/* Convert net_device_stats to rtnl_link_stats64. rtnl_link_stats64 has
 * all the same fields in the same order as net_device_stats, with only
 * the type differing, but rtnl_link_stats64 may have additional fields
 * at the end for newer counters.
 */
void netdev_stats_to_stats64(struct rtnl_link_stats64 *stats64,
			     const struct net_device_stats *netdev_stats)
{
#if BITS_PER_LONG == 64
	BUILD_BUG_ON(sizeof(*stats64) < sizeof(*netdev_stats));
	memcpy(stats64, netdev_stats, sizeof(*netdev_stats));
	/* zero out counters that only exist in rtnl_link_stats64 */
	memset((char *)stats64 + sizeof(*netdev_stats), 0,
	       sizeof(*stats64) - sizeof(*netdev_stats));
#else
	size_t i, n = sizeof(*netdev_stats) / sizeof(unsigned long);
	const unsigned long *src = (const unsigned long *)netdev_stats;
	u64 *dst = (u64 *)stats64;

	BUILD_BUG_ON(n > sizeof(*stats64) / sizeof(u64));
	for (i = 0; i < n; i++)
		dst[i] = src[i];
	/* zero out counters that only exist in rtnl_link_stats64 */
	memset((char *)stats64 + n * sizeof(u64), 0,
	       sizeof(*stats64) - n * sizeof(u64));
#endif
}
EXPORT_SYMBOL(netdev_stats_to_stats64);

/**
 *	dev_get_stats	- get network device statistics
 *	@dev: device to get statistics from
 *	@storage: place to store stats
 *
 *	Get network statistics from device. Return @storage.
 *	The device driver may provide its own method by setting
 *	dev->netdev_ops->get_stats64 or dev->netdev_ops->get_stats;
 *	otherwise the internal statistics structure is used.
 */
struct rtnl_link_stats64 *dev_get_stats(struct net_device *dev,
					struct rtnl_link_stats64 *storage)
{
	const struct net_device_ops *ops = dev->netdev_ops;

	if (ops->ndo_get_stats64) {
		memset(storage, 0, sizeof(*storage));
		ops->ndo_get_stats64(dev, storage);
	} else if (ops->ndo_get_stats) {
		netdev_stats_to_stats64(storage, ops->ndo_get_stats(dev));
	} else {
		netdev_stats_to_stats64(storage, &dev->stats);
	}
	storage->rx_dropped += (unsigned long)atomic_long_read(&dev->rx_dropped);
	storage->tx_dropped += (unsigned long)atomic_long_read(&dev->tx_dropped);
	storage->rx_nohandler += (unsigned long)atomic_long_read(&dev->rx_nohandler);
	return storage;
}
EXPORT_SYMBOL(dev_get_stats);

/**
 *	dev_fetch_sw_netstats - get per-cpu network device statistics
 *	@s: place to store stats
 *	@netstats: per-cpu network stats to read from
 *
 *	Read per-cpu network statistics and populate the related fields in @s.
 */
void dev_fetch_sw_netstats(struct rtnl_link_stats64 *s,
			   const struct pcpu_sw_netstats __percpu *netstats)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		const struct pcpu_sw_netstats *stats;
		struct pcpu_sw_netstats tmp;
		unsigned int start;

		stats = per_cpu_ptr(netstats, cpu);
		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			tmp.rx_packets = stats->rx_packets;
			tmp.rx_bytes   = stats->rx_bytes;
			tmp.tx_packets = stats->tx_packets;
			tmp.tx_bytes   = stats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		s->rx_packets += tmp.rx_packets;
		s->rx_bytes   += tmp.rx_bytes;
		s->tx_packets += tmp.tx_packets;
		s->tx_bytes   += tmp.tx_bytes;
	}
}
EXPORT_SYMBOL_GPL(dev_fetch_sw_netstats);

/**
 *	dev_get_tstats64 - ndo_get_stats64 implementation
 *	@dev: device to get statistics from
 *	@s: place to store stats
 *
 *	Populate @s from dev->stats and dev->tstats. Can be used as
 *	ndo_get_stats64() callback.
 */
void dev_get_tstats64(struct net_device *dev, struct rtnl_link_stats64 *s)
{
	netdev_stats_to_stats64(s, &dev->stats);
	dev_fetch_sw_netstats(s, dev->tstats);
}
EXPORT_SYMBOL_GPL(dev_get_tstats64);

struct netdev_queue *dev_ingress_queue_create(struct net_device *dev)
{
	struct netdev_queue *queue = dev_ingress_queue(dev);

#ifdef CONFIG_NET_CLS_ACT
	if (queue)
		return queue;
	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return NULL;
	netdev_init_one_queue(dev, queue, NULL);
	RCU_INIT_POINTER(queue->qdisc, &noop_qdisc);
	queue->qdisc_sleeping = &noop_qdisc;
	rcu_assign_pointer(dev->ingress_queue, queue);
#endif
	return queue;
}

static const struct ethtool_ops default_ethtool_ops;

void netdev_set_default_ethtool_ops(struct net_device *dev,
				    const struct ethtool_ops *ops)
{
	if (dev->ethtool_ops == &default_ethtool_ops)
		dev->ethtool_ops = ops;
}
EXPORT_SYMBOL_GPL(netdev_set_default_ethtool_ops);

void netdev_freemem(struct net_device *dev)
{
	char *addr = (char *)dev - dev->padded;

	kvfree(addr);
}

/**
 * alloc_netdev_mqs - allocate network device
 * @sizeof_priv: size of private data to allocate space for
 * @name: device name format string
 * @name_assign_type: origin of device name
 * @setup: callback to initialize device
 * @txqs: the number of TX subqueues to allocate
 * @rxqs: the number of RX subqueues to allocate
 *
 * Allocates a struct net_device with private data area for driver use
 * and performs basic initialization.  Also allocates subqueue structs
 * for each queue on the device.
 */
struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name,
		unsigned char name_assign_type,
		void (*setup)(struct net_device *),
		unsigned int txqs, unsigned int rxqs)
{
	struct net_device *dev;
	unsigned int alloc_size;
	struct net_device *p;

	BUG_ON(strlen(name) >= sizeof(dev->name));

	if (txqs < 1) {
		pr_err("alloc_netdev: Unable to allocate device with zero queues\n");
		return NULL;
	}

	if (rxqs < 1) {
		pr_err("alloc_netdev: Unable to allocate device with zero RX queues\n");
		return NULL;
	}

	alloc_size = sizeof(struct net_device);
	if (sizeof_priv) {
		/* ensure 32-byte alignment of private area */
		alloc_size = ALIGN(alloc_size, NETDEV_ALIGN);
		alloc_size += sizeof_priv;
	}
	/* ensure 32-byte alignment of whole construct */
	alloc_size += NETDEV_ALIGN - 1;

	p = kvzalloc(alloc_size, GFP_KERNEL_ACCOUNT | __GFP_RETRY_MAYFAIL);
	if (!p)
		return NULL;

	dev = PTR_ALIGN(p, NETDEV_ALIGN);
	dev->padded = (char *)dev - (char *)p;

#ifdef CONFIG_PCPU_DEV_REFCNT
	dev->pcpu_refcnt = alloc_percpu(int);
	if (!dev->pcpu_refcnt)
		goto free_dev;
	dev_hold(dev);
#else
	refcount_set(&dev->dev_refcnt, 1);
#endif

	if (dev_addr_init(dev))
		goto free_pcpu;

	dev_mc_init(dev);
	dev_uc_init(dev);

	dev_net_set(dev, &init_net);

	dev->gso_max_size = GSO_MAX_SIZE;
	dev->gso_max_segs = GSO_MAX_SEGS;
	dev->upper_level = 1;
	dev->lower_level = 1;
#ifdef CONFIG_LOCKDEP
	dev->nested_level = 0;
	INIT_LIST_HEAD(&dev->unlink_list);
#endif

	INIT_LIST_HEAD(&dev->napi_list);
	INIT_LIST_HEAD(&dev->unreg_list);
	INIT_LIST_HEAD(&dev->close_list);
	INIT_LIST_HEAD(&dev->link_watch_list);
	INIT_LIST_HEAD(&dev->adj_list.upper);
	INIT_LIST_HEAD(&dev->adj_list.lower);
	INIT_LIST_HEAD(&dev->ptype_all);
	INIT_LIST_HEAD(&dev->ptype_specific);
	INIT_LIST_HEAD(&dev->net_notifier_list);
#ifdef CONFIG_NET_SCHED
	hash_init(dev->qdisc_hash);
#endif
	dev->priv_flags = IFF_XMIT_DST_RELEASE | IFF_XMIT_DST_RELEASE_PERM;
	setup(dev);

	if (!dev->tx_queue_len) {
		dev->priv_flags |= IFF_NO_QUEUE;
		dev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;
	}

	dev->num_tx_queues = txqs;
	dev->real_num_tx_queues = txqs;
	if (netif_alloc_netdev_queues(dev))
		goto free_all;

	dev->num_rx_queues = rxqs;
	dev->real_num_rx_queues = rxqs;
	if (netif_alloc_rx_queues(dev))
		goto free_all;

	strcpy(dev->name, name);
	dev->name_assign_type = name_assign_type;
	dev->group = INIT_NETDEV_GROUP;
	if (!dev->ethtool_ops)
		dev->ethtool_ops = &default_ethtool_ops;

	nf_hook_netdev_init(dev);

	return dev;

free_all:
	free_netdev(dev);
	return NULL;

free_pcpu:
#ifdef CONFIG_PCPU_DEV_REFCNT
	free_percpu(dev->pcpu_refcnt);
free_dev:
#endif
	netdev_freemem(dev);
	return NULL;
}
EXPORT_SYMBOL(alloc_netdev_mqs);

/**
 * free_netdev - free network device
 * @dev: device
 *
 * This function does the last stage of destroying an allocated device
 * interface. The reference to the device object is released. If this
 * is the last reference then it will be freed.Must be called in process
 * context.
 */
void free_netdev(struct net_device *dev)
{
	struct napi_struct *p, *n;

	might_sleep();

	/* When called immediately after register_netdevice() failed the unwind
	 * handling may still be dismantling the device. Handle that case by
	 * deferring the free.
	 */
	if (dev->reg_state == NETREG_UNREGISTERING) {
		ASSERT_RTNL();
		dev->needs_free_netdev = true;
		return;
	}

	netif_free_tx_queues(dev);
	netif_free_rx_queues(dev);

	kfree(rcu_dereference_protected(dev->ingress_queue, 1));

	/* Flush device addresses */
	dev_addr_flush(dev);

	list_for_each_entry_safe(p, n, &dev->napi_list, dev_list)
		netif_napi_del(p);

#ifdef CONFIG_PCPU_DEV_REFCNT
	free_percpu(dev->pcpu_refcnt);
	dev->pcpu_refcnt = NULL;
#endif
	free_percpu(dev->xdp_bulkq);
	dev->xdp_bulkq = NULL;

	/*  Compatibility with error handling in drivers */
	if (dev->reg_state == NETREG_UNINITIALIZED) {
		netdev_freemem(dev);
		return;
	}

	BUG_ON(dev->reg_state != NETREG_UNREGISTERED);
	dev->reg_state = NETREG_RELEASED;

	/* will free via device release */
	put_device(&dev->dev);
}
EXPORT_SYMBOL(free_netdev);

/**
 *	synchronize_net -  Synchronize with packet receive processing
 *
 *	Wait for packets currently being received to be done.
 *	Does not block later packets from starting.
 */
void synchronize_net(void)
{
	might_sleep();
	if (rtnl_is_locked())
		synchronize_rcu_expedited();
	else
		synchronize_rcu();
}
EXPORT_SYMBOL(synchronize_net);

/**
 *	unregister_netdevice_queue - remove device from the kernel
 *	@dev: device
 *	@head: list
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables.
 *	If head not NULL, device is queued to be unregistered later.
 *
 *	Callers must hold the rtnl semaphore.  You may want
 *	unregister_netdev() instead of this.
 */

void unregister_netdevice_queue(struct net_device *dev, struct list_head *head)
{
	ASSERT_RTNL();

	if (head) {
		list_move_tail(&dev->unreg_list, head);
	} else {
		LIST_HEAD(single);

		list_add(&dev->unreg_list, &single);
		unregister_netdevice_many(&single);
	}
}
EXPORT_SYMBOL(unregister_netdevice_queue);

/**
 *	unregister_netdevice_many - unregister many devices
 *	@head: list of devices
 *
 *  Note: As most callers use a stack allocated list_head,
 *  we force a list_del() to make sure stack wont be corrupted later.
 */
void unregister_netdevice_many(struct list_head *head)
{
	struct net_device *dev, *tmp;
	LIST_HEAD(close_head);

	BUG_ON(dev_boot_phase);
	ASSERT_RTNL();

	if (list_empty(head))
		return;

	list_for_each_entry_safe(dev, tmp, head, unreg_list) {
		/* Some devices call without registering
		 * for initialization unwind. Remove those
		 * devices and proceed with the remaining.
		 */
		if (dev->reg_state == NETREG_UNINITIALIZED) {
			pr_debug("unregister_netdevice: device %s/%p never was registered\n",
				 dev->name, dev);

			WARN_ON(1);
			list_del(&dev->unreg_list);
			continue;
		}
		dev->dismantle = true;
		BUG_ON(dev->reg_state != NETREG_REGISTERED);
	}

	/* If device is running, close it first. */
	list_for_each_entry(dev, head, unreg_list)
		list_add_tail(&dev->close_list, &close_head);
	dev_close_many(&close_head, true);

	list_for_each_entry(dev, head, unreg_list) {
		/* And unlink it from device chain. */
		unlist_netdevice(dev);

		dev->reg_state = NETREG_UNREGISTERING;
	}
	flush_all_backlogs();

	synchronize_net();

	list_for_each_entry(dev, head, unreg_list) {
		struct sk_buff *skb = NULL;

		/* Shutdown queueing discipline. */
		dev_shutdown(dev);

		dev_xdp_uninstall(dev);

		/* Notify protocols, that we are about to destroy
		 * this device. They should clean all the things.
		 */
		call_netdevice_notifiers(NETDEV_UNREGISTER, dev);

		if (!dev->rtnl_link_ops ||
		    dev->rtnl_link_state == RTNL_LINK_INITIALIZED)
			skb = rtmsg_ifinfo_build_skb(RTM_DELLINK, dev, ~0U, 0,
						     GFP_KERNEL, NULL, 0);

		/*
		 *	Flush the unicast and multicast chains
		 */
		dev_uc_flush(dev);
		dev_mc_flush(dev);

		netdev_name_node_alt_flush(dev);
		netdev_name_node_free(dev->name_node);

		if (dev->netdev_ops->ndo_uninit)
			dev->netdev_ops->ndo_uninit(dev);

		if (skb)
			rtmsg_ifinfo_send(skb, dev, GFP_KERNEL);

		/* Notifier chain MUST detach us all upper devices. */
		WARN_ON(netdev_has_any_upper_dev(dev));
		WARN_ON(netdev_has_any_lower_dev(dev));

		/* Remove entries from kobject tree */
		netdev_unregister_kobject(dev);
#ifdef CONFIG_XPS
		/* Remove XPS queueing entries */
		netif_reset_xps_queues_gt(dev, 0);
#endif
	}

	synchronize_net();

	list_for_each_entry(dev, head, unreg_list) {
		dev_put(dev);
		net_set_todo(dev);
	}

	list_del(head);
}
EXPORT_SYMBOL(unregister_netdevice_many);

/**
 *	unregister_netdev - remove device from the kernel
 *	@dev: device
 *
 *	This function shuts down a device interface and removes it
 *	from the kernel tables.
 *
 *	This is just a wrapper for unregister_netdevice that takes
 *	the rtnl semaphore.  In general you want to use this and not
 *	unregister_netdevice.
 */
void unregister_netdev(struct net_device *dev)
{
	rtnl_lock();
	unregister_netdevice(dev);
	rtnl_unlock();
}
EXPORT_SYMBOL(unregister_netdev);

/**
 *	__dev_change_net_namespace - move device to different nethost namespace
 *	@dev: device
 *	@net: network namespace
 *	@pat: If not NULL name pattern to try if the current device name
 *	      is already taken in the destination network namespace.
 *	@new_ifindex: If not zero, specifies device index in the target
 *	              namespace.
 *
 *	This function shuts down a device interface and moves it
 *	to a new network namespace. On success 0 is returned, on
 *	a failure a netagive errno code is returned.
 *
 *	Callers must hold the rtnl semaphore.
 */

int __dev_change_net_namespace(struct net_device *dev, struct net *net,
			       const char *pat, int new_ifindex)
{
	struct net *net_old = dev_net(dev);
	int err, new_nsid;

	ASSERT_RTNL();

	/* Don't allow namespace local devices to be moved. */
	err = -EINVAL;
	if (dev->features & NETIF_F_NETNS_LOCAL)
		goto out;

	/* Ensure the device has been registrered */
	if (dev->reg_state != NETREG_REGISTERED)
		goto out;

	/* Get out if there is nothing todo */
	err = 0;
	if (net_eq(net_old, net))
		goto out;

	/* Pick the destination device name, and ensure
	 * we can use it in the destination network namespace.
	 */
	err = -EEXIST;
	if (netdev_name_in_use(net, dev->name)) {
		/* We get here if we can't use the current device name */
		if (!pat)
			goto out;
		err = dev_get_valid_name(net, dev, pat);
		if (err < 0)
			goto out;
	}

	/* Check that new_ifindex isn't used yet. */
	err = -EBUSY;
	if (new_ifindex && __dev_get_by_index(net, new_ifindex))
		goto out;

	/*
	 * And now a mini version of register_netdevice unregister_netdevice.
	 */

	/* If device is running close it first. */
	dev_close(dev);

	/* And unlink it from device chain */
	unlist_netdevice(dev);

	synchronize_net();

	/* Shutdown queueing discipline. */
	dev_shutdown(dev);

	/* Notify protocols, that we are about to destroy
	 * this device. They should clean all the things.
	 *
	 * Note that dev->reg_state stays at NETREG_REGISTERED.
	 * This is wanted because this way 8021q and macvlan know
	 * the device is just moving and can keep their slaves up.
	 */
	call_netdevice_notifiers(NETDEV_UNREGISTER, dev);
	rcu_barrier();

	new_nsid = peernet2id_alloc(dev_net(dev), net, GFP_KERNEL);
	/* If there is an ifindex conflict assign a new one */
	if (!new_ifindex) {
		if (__dev_get_by_index(net, dev->ifindex))
			new_ifindex = dev_new_index(net);
		else
			new_ifindex = dev->ifindex;
	}

	rtmsg_ifinfo_newnet(RTM_DELLINK, dev, ~0U, GFP_KERNEL, &new_nsid,
			    new_ifindex);

	/*
	 *	Flush the unicast and multicast chains
	 */
	dev_uc_flush(dev);
	dev_mc_flush(dev);

	/* Send a netdev-removed uevent to the old namespace */
	kobject_uevent(&dev->dev.kobj, KOBJ_REMOVE);
	netdev_adjacent_del_links(dev);

	/* Move per-net netdevice notifiers that are following the netdevice */
	move_netdevice_notifiers_dev_net(dev, net);

	/* Actually switch the network namespace */
	dev_net_set(dev, net);
	dev->ifindex = new_ifindex;

	/* Send a netdev-add uevent to the new namespace */
	kobject_uevent(&dev->dev.kobj, KOBJ_ADD);
	netdev_adjacent_add_links(dev);

	/* Fixup kobjects */
	err = device_rename(&dev->dev, dev->name);
	WARN_ON(err);

	/* Adapt owner in case owning user namespace of target network
	 * namespace is different from the original one.
	 */
	err = netdev_change_owner(dev, net_old, net);
	WARN_ON(err);

	/* Add the device back in the hashes */
	list_netdevice(dev);

	/* Notify protocols, that a new device appeared. */
	call_netdevice_notifiers(NETDEV_REGISTER, dev);

	/*
	 *	Prevent userspace races by waiting until the network
	 *	device is fully setup before sending notifications.
	 */
	rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U, GFP_KERNEL);

	synchronize_net();
	err = 0;
out:
	return err;
}
EXPORT_SYMBOL_GPL(__dev_change_net_namespace);

static int dev_cpu_dead(unsigned int oldcpu)
{
	struct sk_buff **list_skb;
	struct sk_buff *skb;
	unsigned int cpu;
	struct softnet_data *sd, *oldsd, *remsd = NULL;

	local_irq_disable();
	cpu = smp_processor_id();
	sd = &per_cpu(softnet_data, cpu);
	oldsd = &per_cpu(softnet_data, oldcpu);

	/* Find end of our completion_queue. */
	list_skb = &sd->completion_queue;
	while (*list_skb)
		list_skb = &(*list_skb)->next;
	/* Append completion queue from offline CPU. */
	*list_skb = oldsd->completion_queue;
	oldsd->completion_queue = NULL;

	/* Append output queue from offline CPU. */
	if (oldsd->output_queue) {
		*sd->output_queue_tailp = oldsd->output_queue;
		sd->output_queue_tailp = oldsd->output_queue_tailp;
		oldsd->output_queue = NULL;
		oldsd->output_queue_tailp = &oldsd->output_queue;
	}
	/* Append NAPI poll list from offline CPU, with one exception :
	 * process_backlog() must be called by cpu owning percpu backlog.
	 * We properly handle process_queue & input_pkt_queue later.
	 */
	while (!list_empty(&oldsd->poll_list)) {
		struct napi_struct *napi = list_first_entry(&oldsd->poll_list,
							    struct napi_struct,
							    poll_list);

		list_del_init(&napi->poll_list);
		if (napi->poll == process_backlog)
			napi->state = 0;
		else
			____napi_schedule(sd, napi);
	}

	raise_softirq_irqoff(NET_TX_SOFTIRQ);
	local_irq_enable();

#ifdef CONFIG_RPS
	remsd = oldsd->rps_ipi_list;
	oldsd->rps_ipi_list = NULL;
#endif
	/* send out pending IPI's on offline CPU */
	net_rps_send_ipi(remsd);

	/* Process offline CPU's input_pkt_queue */
	while ((skb = __skb_dequeue(&oldsd->process_queue))) {
		netif_rx_ni(skb);
		input_queue_head_incr(oldsd);
	}
	while ((skb = skb_dequeue(&oldsd->input_pkt_queue))) {
		netif_rx_ni(skb);
		input_queue_head_incr(oldsd);
	}

	return 0;
}

/**
 *	netdev_increment_features - increment feature set by one
 *	@all: current feature set
 *	@one: new feature set
 *	@mask: mask feature set
 *
 *	Computes a new feature set after adding a device with feature set
 *	@one to the master device with current feature set @all.  Will not
 *	enable anything that is off in @mask. Returns the new feature set.
 */
netdev_features_t netdev_increment_features(netdev_features_t all,
	netdev_features_t one, netdev_features_t mask)
{
	if (mask & NETIF_F_HW_CSUM)
		mask |= NETIF_F_CSUM_MASK;
	mask |= NETIF_F_VLAN_CHALLENGED;

	all |= one & (NETIF_F_ONE_FOR_ALL | NETIF_F_CSUM_MASK) & mask;
	all &= one | ~NETIF_F_ALL_FOR_ALL;

	/* If one device supports hw checksumming, set for all. */
	if (all & NETIF_F_HW_CSUM)
		all &= ~(NETIF_F_CSUM_MASK & ~NETIF_F_HW_CSUM);

	return all;
}
EXPORT_SYMBOL(netdev_increment_features);

static struct hlist_head * __net_init netdev_create_hash(void)
{
	int i;
	struct hlist_head *hash;

	hash = kmalloc_array(NETDEV_HASHENTRIES, sizeof(*hash), GFP_KERNEL);
	if (hash != NULL)
		for (i = 0; i < NETDEV_HASHENTRIES; i++)
			INIT_HLIST_HEAD(&hash[i]);

	return hash;
}

/* Initialize per network namespace state */
static int __net_init netdev_init(struct net *net)
{
	BUILD_BUG_ON(GRO_HASH_BUCKETS >
		     8 * sizeof_field(struct napi_struct, gro_bitmask));

	if (net != &init_net)
		INIT_LIST_HEAD(&net->dev_base_head);

	net->dev_name_head = netdev_create_hash();
	if (net->dev_name_head == NULL)
		goto err_name;

	net->dev_index_head = netdev_create_hash();
	if (net->dev_index_head == NULL)
		goto err_idx;

	RAW_INIT_NOTIFIER_HEAD(&net->netdev_chain);

	return 0;

err_idx:
	kfree(net->dev_name_head);
err_name:
	return -ENOMEM;
}

/**
 *	netdev_drivername - network driver for the device
 *	@dev: network device
 *
 *	Determine network driver for device.
 */
const char *netdev_drivername(const struct net_device *dev)
{
	const struct device_driver *driver;
	const struct device *parent;
	const char *empty = "";

	parent = dev->dev.parent;
	if (!parent)
		return empty;

	driver = parent->driver;
	if (driver && driver->name)
		return driver->name;
	return empty;
}

static void __netdev_printk(const char *level, const struct net_device *dev,
			    struct va_format *vaf)
{
	if (dev && dev->dev.parent) {
		dev_printk_emit(level[1] - '0',
				dev->dev.parent,
				"%s %s %s%s: %pV",
				dev_driver_string(dev->dev.parent),
				dev_name(dev->dev.parent),
				netdev_name(dev), netdev_reg_state(dev),
				vaf);
	} else if (dev) {
		printk("%s%s%s: %pV",
		       level, netdev_name(dev), netdev_reg_state(dev), vaf);
	} else {
		printk("%s(NULL net_device): %pV", level, vaf);
	}
}

void netdev_printk(const char *level, const struct net_device *dev,
		   const char *format, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, format);

	vaf.fmt = format;
	vaf.va = &args;

	__netdev_printk(level, dev, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(netdev_printk);

#define define_netdev_printk_level(func, level)			\
void func(const struct net_device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf;					\
	va_list args;						\
								\
	va_start(args, fmt);					\
								\
	vaf.fmt = fmt;						\
	vaf.va = &args;						\
								\
	__netdev_printk(level, dev, &vaf);			\
								\
	va_end(args);						\
}								\
EXPORT_SYMBOL(func);

define_netdev_printk_level(netdev_emerg, KERN_EMERG);
define_netdev_printk_level(netdev_alert, KERN_ALERT);
define_netdev_printk_level(netdev_crit, KERN_CRIT);
define_netdev_printk_level(netdev_err, KERN_ERR);
define_netdev_printk_level(netdev_warn, KERN_WARNING);
define_netdev_printk_level(netdev_notice, KERN_NOTICE);
define_netdev_printk_level(netdev_info, KERN_INFO);

static void __net_exit netdev_exit(struct net *net)
{
	kfree(net->dev_name_head);
	kfree(net->dev_index_head);
	if (net != &init_net)
		WARN_ON_ONCE(!list_empty(&net->dev_base_head));
}

static struct pernet_operations __net_initdata netdev_net_ops = {
	.init = netdev_init,
	.exit = netdev_exit,
};

static void __net_exit default_device_exit(struct net *net)
{
	struct net_device *dev, *aux;
	/*
	 * Push all migratable network devices back to the
	 * initial network namespace
	 */
	rtnl_lock();
	for_each_netdev_safe(net, dev, aux) {
		int err;
		char fb_name[IFNAMSIZ];

		/* Ignore unmoveable devices (i.e. loopback) */
		if (dev->features & NETIF_F_NETNS_LOCAL)
			continue;

		/* Leave virtual devices for the generic cleanup */
		if (dev->rtnl_link_ops && !dev->rtnl_link_ops->netns_refund)
			continue;

		/* Push remaining network devices to init_net */
		snprintf(fb_name, IFNAMSIZ, "dev%d", dev->ifindex);
		if (netdev_name_in_use(&init_net, fb_name))
			snprintf(fb_name, IFNAMSIZ, "dev%%d");
		err = dev_change_net_namespace(dev, &init_net, fb_name);
		if (err) {
			pr_emerg("%s: failed to move %s to init_net: %d\n",
				 __func__, dev->name, err);
			BUG();
		}
	}
	rtnl_unlock();
}

static void __net_exit rtnl_lock_unregistering(struct list_head *net_list)
{
	/* Return with the rtnl_lock held when there are no network
	 * devices unregistering in any network namespace in net_list.
	 */
	struct net *net;
	bool unregistering;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	add_wait_queue(&netdev_unregistering_wq, &wait);
	for (;;) {
		unregistering = false;
		rtnl_lock();
		list_for_each_entry(net, net_list, exit_list) {
			if (net->dev_unreg_count > 0) {
				unregistering = true;
				break;
			}
		}
		if (!unregistering)
			break;
		__rtnl_unlock();

		wait_woken(&wait, TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&netdev_unregistering_wq, &wait);
}

static void __net_exit default_device_exit_batch(struct list_head *net_list)
{
	/* At exit all network devices most be removed from a network
	 * namespace.  Do this in the reverse order of registration.
	 * Do this across as many network namespaces as possible to
	 * improve batching efficiency.
	 */
	struct net_device *dev;
	struct net *net;
	LIST_HEAD(dev_kill_list);

	/* To prevent network device cleanup code from dereferencing
	 * loopback devices or network devices that have been freed
	 * wait here for all pending unregistrations to complete,
	 * before unregistring the loopback device and allowing the
	 * network namespace be freed.
	 *
	 * The netdev todo list containing all network devices
	 * unregistrations that happen in default_device_exit_batch
	 * will run in the rtnl_unlock() at the end of
	 * default_device_exit_batch.
	 */
	rtnl_lock_unregistering(net_list);
	list_for_each_entry(net, net_list, exit_list) {
		for_each_netdev_reverse(net, dev) {
			if (dev->rtnl_link_ops && dev->rtnl_link_ops->dellink)
				dev->rtnl_link_ops->dellink(dev, &dev_kill_list);
			else
				unregister_netdevice_queue(dev, &dev_kill_list);
		}
	}
	unregister_netdevice_many(&dev_kill_list);
	rtnl_unlock();
}

static struct pernet_operations __net_initdata default_device_ops = {
	.exit = default_device_exit,
	.exit_batch = default_device_exit_batch,
};

/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not
 *	present) and leaves us with a valid list of present and active devices.
 *
 */

/*
 *       This is called single threaded during boot, so no need
 *       to take the rtnl semaphore.
 */
static int __init net_dev_init(void)
{
	int i, rc = -ENOMEM;

	BUG_ON(!dev_boot_phase);

	if (dev_proc_init())
		goto out;

	if (netdev_kobject_init())
		goto out;

	INIT_LIST_HEAD(&ptype_all);
	for (i = 0; i < PTYPE_HASH_SIZE; i++)
		INIT_LIST_HEAD(&ptype_base[i]);

	INIT_LIST_HEAD(&offload_base);

	if (register_pernet_subsys(&netdev_net_ops))
		goto out;

	/*
	 *	Initialise the packet receive queues.
	 */

	for_each_possible_cpu(i) {
		struct work_struct *flush = per_cpu_ptr(&flush_works, i);
		struct softnet_data *sd = &per_cpu(softnet_data, i);

		INIT_WORK(flush, flush_backlog);

		skb_queue_head_init(&sd->input_pkt_queue);
		skb_queue_head_init(&sd->process_queue);
#ifdef CONFIG_XFRM_OFFLOAD
		skb_queue_head_init(&sd->xfrm_backlog);
#endif
		INIT_LIST_HEAD(&sd->poll_list);
		sd->output_queue_tailp = &sd->output_queue;
#ifdef CONFIG_RPS
		INIT_CSD(&sd->csd, rps_trigger_softirq, sd);
		sd->cpu = i;
#endif

		init_gro_hash(&sd->backlog);
		sd->backlog.poll = process_backlog;
		sd->backlog.weight = weight_p;
	}

	dev_boot_phase = 0;

	/* The loopback device is special if any other network devices
	 * is present in a network namespace the loopback device must
	 * be present. Since we now dynamically allocate and free the
	 * loopback device ensure this invariant is maintained by
	 * keeping the loopback device as the first device on the
	 * list of network devices.  Ensuring the loopback devices
	 * is the first device that appears and the last network device
	 * that disappears.
	 */
	if (register_pernet_device(&loopback_net_ops))
		goto out;

	if (register_pernet_device(&default_device_ops))
		goto out;

	open_softirq(NET_TX_SOFTIRQ, net_tx_action);
	open_softirq(NET_RX_SOFTIRQ, net_rx_action);

	rc = cpuhp_setup_state_nocalls(CPUHP_NET_DEV_DEAD, "net/dev:dead",
				       NULL, dev_cpu_dead);
	WARN_ON(rc < 0);
	rc = 0;
out:
	return rc;
}

subsys_initcall(net_dev_init);
