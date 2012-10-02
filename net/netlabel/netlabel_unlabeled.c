/*
 * NetLabel Unlabeled Support
 *
 * This file defines functions for dealing with unlabeled packets for the
 * NetLabel system.  The NetLabel system manages static and dynamic label
 * mappings for network protocols such as CIPSO and RIPSO.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006 - 2008
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/audit.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/net_namespace.h>
#include <net/netlabel.h>
#include <asm/bug.h>
#include <linux/atomic.h>

#include "netlabel_user.h"
#include "netlabel_addrlist.h"
#include "netlabel_domainhash.h"
#include "netlabel_unlabeled.h"
#include "netlabel_mgmt.h"

/* NOTE: at present we always use init's network namespace since we don't
 *       presently support different namespaces even though the majority of
 *       the functions in this file are "namespace safe" */

/* The unlabeled connection hash table which we use to map network interfaces
 * and addresses of unlabeled packets to a user specified secid value for the
 * LSM.  The hash table is used to lookup the network interface entry
 * (struct netlbl_unlhsh_iface) and then the interface entry is used to
 * lookup an IP address match from an ordered list.  If a network interface
 * match can not be found in the hash table then the default entry
 * (netlbl_unlhsh_def) is used.  The IP address entry list
 * (struct netlbl_unlhsh_addr) is ordered such that the entries with a
 * larger netmask come first.
 */
struct netlbl_unlhsh_tbl {
	struct list_head *tbl;
	u32 size;
};
#define netlbl_unlhsh_addr4_entry(iter) \
	container_of(iter, struct netlbl_unlhsh_addr4, list)
struct netlbl_unlhsh_addr4 {
	u32 secid;

	struct netlbl_af4list list;
	struct rcu_head rcu;
};
#define netlbl_unlhsh_addr6_entry(iter) \
	container_of(iter, struct netlbl_unlhsh_addr6, list)
struct netlbl_unlhsh_addr6 {
	u32 secid;

	struct netlbl_af6list list;
	struct rcu_head rcu;
};
struct netlbl_unlhsh_iface {
	int ifindex;
	struct list_head addr4_list;
	struct list_head addr6_list;

	u32 valid;
	struct list_head list;
	struct rcu_head rcu;
};

/* Argument struct for netlbl_unlhsh_walk() */
struct netlbl_unlhsh_walk_arg {
	struct netlink_callback *nl_cb;
	struct sk_buff *skb;
	u32 seq;
};

/* Unlabeled connection hash table */
/* updates should be so rare that having one spinlock for the entire
 * hash table should be okay */
static DEFINE_SPINLOCK(netlbl_unlhsh_lock);
#define netlbl_unlhsh_rcu_deref(p) \
	rcu_dereference_check(p, lockdep_is_held(&netlbl_unlhsh_lock))
static struct netlbl_unlhsh_tbl *netlbl_unlhsh = NULL;
static struct netlbl_unlhsh_iface *netlbl_unlhsh_def = NULL;

/* Accept unlabeled packets flag */
static u8 netlabel_unlabel_acceptflg = 0;

/* NetLabel Generic NETLINK unlabeled family */
static struct genl_family netlbl_unlabel_gnl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = NETLBL_NLTYPE_UNLABELED_NAME,
	.version = NETLBL_PROTO_VERSION,
	.maxattr = NLBL_UNLABEL_A_MAX,
};

/* NetLabel Netlink attribute policy */
static const struct nla_policy netlbl_unlabel_genl_policy[NLBL_UNLABEL_A_MAX + 1] = {
	[NLBL_UNLABEL_A_ACPTFLG] = { .type = NLA_U8 },
	[NLBL_UNLABEL_A_IPV6ADDR] = { .type = NLA_BINARY,
				      .len = sizeof(struct in6_addr) },
	[NLBL_UNLABEL_A_IPV6MASK] = { .type = NLA_BINARY,
				      .len = sizeof(struct in6_addr) },
	[NLBL_UNLABEL_A_IPV4ADDR] = { .type = NLA_BINARY,
				      .len = sizeof(struct in_addr) },
	[NLBL_UNLABEL_A_IPV4MASK] = { .type = NLA_BINARY,
				      .len = sizeof(struct in_addr) },
	[NLBL_UNLABEL_A_IFACE] = { .type = NLA_NUL_STRING,
				   .len = IFNAMSIZ - 1 },
	[NLBL_UNLABEL_A_SECCTX] = { .type = NLA_BINARY }
};

/*
 * Unlabeled Connection Hash Table Functions
 */

/**
 * netlbl_unlhsh_free_iface - Frees an interface entry from the hash table
 * @entry: the entry's RCU field
 *
 * Description:
 * This function is designed to be used as a callback to the call_rcu()
 * function so that memory allocated to a hash table interface entry can be
 * released safely.  It is important to note that this function does not free
 * the IPv4 and IPv6 address lists contained as part of an interface entry.  It
 * is up to the rest of the code to make sure an interface entry is only freed
 * once it's address lists are empty.
 *
 */
static void netlbl_unlhsh_free_iface(struct rcu_head *entry)
{
	struct netlbl_unlhsh_iface *iface;
	struct netlbl_af4list *iter4;
	struct netlbl_af4list *tmp4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *iter6;
	struct netlbl_af6list *tmp6;
#endif /* IPv6 */

	iface = container_of(entry, struct netlbl_unlhsh_iface, rcu);

	/* no need for locks here since we are the only one with access to this
	 * structure */

	netlbl_af4list_foreach_safe(iter4, tmp4, &iface->addr4_list) {
		netlbl_af4list_remove_entry(iter4);
		kfree(netlbl_unlhsh_addr4_entry(iter4));
	}
#if IS_ENABLED(CONFIG_IPV6)
	netlbl_af6list_foreach_safe(iter6, tmp6, &iface->addr6_list) {
		netlbl_af6list_remove_entry(iter6);
		kfree(netlbl_unlhsh_addr6_entry(iter6));
	}
#endif /* IPv6 */
	kfree(iface);
}

/**
 * netlbl_unlhsh_hash - Hashing function for the hash table
 * @ifindex: the network interface/device to hash
 *
 * Description:
 * This is the hashing function for the unlabeled hash table, it returns the
 * bucket number for the given device/interface.  The caller is responsible for
 * ensuring that the hash table is protected with either a RCU read lock or
 * the hash table lock.
 *
 */
static u32 netlbl_unlhsh_hash(int ifindex)
{
	return ifindex & (netlbl_unlhsh_rcu_deref(netlbl_unlhsh)->size - 1);
}

/**
 * netlbl_unlhsh_search_iface - Search for a matching interface entry
 * @ifindex: the network interface
 *
 * Description:
 * Searches the unlabeled connection hash table and returns a pointer to the
 * interface entry which matches @ifindex, otherwise NULL is returned.  The
 * caller is responsible for ensuring that the hash table is protected with
 * either a RCU read lock or the hash table lock.
 *
 */
static struct netlbl_unlhsh_iface *netlbl_unlhsh_search_iface(int ifindex)
{
	u32 bkt;
	struct list_head *bkt_list;
	struct netlbl_unlhsh_iface *iter;

	bkt = netlbl_unlhsh_hash(ifindex);
	bkt_list = &netlbl_unlhsh_rcu_deref(netlbl_unlhsh)->tbl[bkt];
	list_for_each_entry_rcu(iter, bkt_list, list)
		if (iter->valid && iter->ifindex == ifindex)
			return iter;

	return NULL;
}

/**
 * netlbl_unlhsh_add_addr4 - Add a new IPv4 address entry to the hash table
 * @iface: the associated interface entry
 * @addr: IPv4 address in network byte order
 * @mask: IPv4 address mask in network byte order
 * @secid: LSM secid value for entry
 *
 * Description:
 * Add a new address entry into the unlabeled connection hash table using the
 * interface entry specified by @iface.  On success zero is returned, otherwise
 * a negative value is returned.
 *
 */
static int netlbl_unlhsh_add_addr4(struct netlbl_unlhsh_iface *iface,
				   const struct in_addr *addr,
				   const struct in_addr *mask,
				   u32 secid)
{
	int ret_val;
	struct netlbl_unlhsh_addr4 *entry;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL)
		return -ENOMEM;

	entry->list.addr = addr->s_addr & mask->s_addr;
	entry->list.mask = mask->s_addr;
	entry->list.valid = 1;
	entry->secid = secid;

	spin_lock(&netlbl_unlhsh_lock);
	ret_val = netlbl_af4list_add(&entry->list, &iface->addr4_list);
	spin_unlock(&netlbl_unlhsh_lock);

	if (ret_val != 0)
		kfree(entry);
	return ret_val;
}

#if IS_ENABLED(CONFIG_IPV6)
/**
 * netlbl_unlhsh_add_addr6 - Add a new IPv6 address entry to the hash table
 * @iface: the associated interface entry
 * @addr: IPv6 address in network byte order
 * @mask: IPv6 address mask in network byte order
 * @secid: LSM secid value for entry
 *
 * Description:
 * Add a new address entry into the unlabeled connection hash table using the
 * interface entry specified by @iface.  On success zero is returned, otherwise
 * a negative value is returned.
 *
 */
static int netlbl_unlhsh_add_addr6(struct netlbl_unlhsh_iface *iface,
				   const struct in6_addr *addr,
				   const struct in6_addr *mask,
				   u32 secid)
{
	int ret_val;
	struct netlbl_unlhsh_addr6 *entry;

	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL)
		return -ENOMEM;

	entry->list.addr = *addr;
	entry->list.addr.s6_addr32[0] &= mask->s6_addr32[0];
	entry->list.addr.s6_addr32[1] &= mask->s6_addr32[1];
	entry->list.addr.s6_addr32[2] &= mask->s6_addr32[2];
	entry->list.addr.s6_addr32[3] &= mask->s6_addr32[3];
	entry->list.mask = *mask;
	entry->list.valid = 1;
	entry->secid = secid;

	spin_lock(&netlbl_unlhsh_lock);
	ret_val = netlbl_af6list_add(&entry->list, &iface->addr6_list);
	spin_unlock(&netlbl_unlhsh_lock);

	if (ret_val != 0)
		kfree(entry);
	return 0;
}
#endif /* IPv6 */

/**
 * netlbl_unlhsh_add_iface - Adds a new interface entry to the hash table
 * @ifindex: network interface
 *
 * Description:
 * Add a new, empty, interface entry into the unlabeled connection hash table.
 * On success a pointer to the new interface entry is returned, on failure NULL
 * is returned.
 *
 */
static struct netlbl_unlhsh_iface *netlbl_unlhsh_add_iface(int ifindex)
{
	u32 bkt;
	struct netlbl_unlhsh_iface *iface;

	iface = kzalloc(sizeof(*iface), GFP_ATOMIC);
	if (iface == NULL)
		return NULL;

	iface->ifindex = ifindex;
	INIT_LIST_HEAD(&iface->addr4_list);
	INIT_LIST_HEAD(&iface->addr6_list);
	iface->valid = 1;

	spin_lock(&netlbl_unlhsh_lock);
	if (ifindex > 0) {
		bkt = netlbl_unlhsh_hash(ifindex);
		if (netlbl_unlhsh_search_iface(ifindex) != NULL)
			goto add_iface_failure;
		list_add_tail_rcu(&iface->list,
			     &netlbl_unlhsh_rcu_deref(netlbl_unlhsh)->tbl[bkt]);
	} else {
		INIT_LIST_HEAD(&iface->list);
		if (netlbl_unlhsh_rcu_deref(netlbl_unlhsh_def) != NULL)
			goto add_iface_failure;
		rcu_assign_pointer(netlbl_unlhsh_def, iface);
	}
	spin_unlock(&netlbl_unlhsh_lock);

	return iface;

add_iface_failure:
	spin_unlock(&netlbl_unlhsh_lock);
	kfree(iface);
	return NULL;
}

/**
 * netlbl_unlhsh_add - Adds a new entry to the unlabeled connection hash table
 * @net: network namespace
 * @dev_name: interface name
 * @addr: IP address in network byte order
 * @mask: address mask in network byte order
 * @addr_len: length of address/mask (4 for IPv4, 16 for IPv6)
 * @secid: LSM secid value for the entry
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Adds a new entry to the unlabeled connection hash table.  Returns zero on
 * success, negative values on failure.
 *
 */
int netlbl_unlhsh_add(struct net *net,
		      const char *dev_name,
		      const void *addr,
		      const void *mask,
		      u32 addr_len,
		      u32 secid,
		      struct netlbl_audit *audit_info)
{
	int ret_val;
	int ifindex;
	struct net_device *dev;
	struct netlbl_unlhsh_iface *iface;
	struct audit_buffer *audit_buf = NULL;
	char *secctx = NULL;
	u32 secctx_len;

	if (addr_len != sizeof(struct in_addr) &&
	    addr_len != sizeof(struct in6_addr))
		return -EINVAL;

	rcu_read_lock();
	if (dev_name != NULL) {
		dev = dev_get_by_name_rcu(net, dev_name);
		if (dev == NULL) {
			ret_val = -ENODEV;
			goto unlhsh_add_return;
		}
		ifindex = dev->ifindex;
		iface = netlbl_unlhsh_search_iface(ifindex);
	} else {
		ifindex = 0;
		iface = rcu_dereference(netlbl_unlhsh_def);
	}
	if (iface == NULL) {
		iface = netlbl_unlhsh_add_iface(ifindex);
		if (iface == NULL) {
			ret_val = -ENOMEM;
			goto unlhsh_add_return;
		}
	}
	audit_buf = netlbl_audit_start_common(AUDIT_MAC_UNLBL_STCADD,
					      audit_info);
	switch (addr_len) {
	case sizeof(struct in_addr): {
		const struct in_addr *addr4 = addr;
		const struct in_addr *mask4 = mask;

		ret_val = netlbl_unlhsh_add_addr4(iface, addr4, mask4, secid);
		if (audit_buf != NULL)
			netlbl_af4list_audit_addr(audit_buf, 1,
						  dev_name,
						  addr4->s_addr,
						  mask4->s_addr);
		break;
	}
#if IS_ENABLED(CONFIG_IPV6)
	case sizeof(struct in6_addr): {
		const struct in6_addr *addr6 = addr;
		const struct in6_addr *mask6 = mask;

		ret_val = netlbl_unlhsh_add_addr6(iface, addr6, mask6, secid);
		if (audit_buf != NULL)
			netlbl_af6list_audit_addr(audit_buf, 1,
						  dev_name,
						  addr6, mask6);
		break;
	}
#endif /* IPv6 */
	default:
		ret_val = -EINVAL;
	}
	if (ret_val == 0)
		atomic_inc(&netlabel_mgmt_protocount);

unlhsh_add_return:
	rcu_read_unlock();
	if (audit_buf != NULL) {
		if (security_secid_to_secctx(secid,
					     &secctx,
					     &secctx_len) == 0) {
			audit_log_format(audit_buf, " sec_obj=%s", secctx);
			security_release_secctx(secctx, secctx_len);
		}
		audit_log_format(audit_buf, " res=%u", ret_val == 0 ? 1 : 0);
		audit_log_end(audit_buf);
	}
	return ret_val;
}

/**
 * netlbl_unlhsh_remove_addr4 - Remove an IPv4 address entry
 * @net: network namespace
 * @iface: interface entry
 * @addr: IP address
 * @mask: IP address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Remove an IP address entry from the unlabeled connection hash table.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlhsh_remove_addr4(struct net *net,
				      struct netlbl_unlhsh_iface *iface,
				      const struct in_addr *addr,
				      const struct in_addr *mask,
				      struct netlbl_audit *audit_info)
{
	struct netlbl_af4list *list_entry;
	struct netlbl_unlhsh_addr4 *entry;
	struct audit_buffer *audit_buf;
	struct net_device *dev;
	char *secctx;
	u32 secctx_len;

	spin_lock(&netlbl_unlhsh_lock);
	list_entry = netlbl_af4list_remove(addr->s_addr, mask->s_addr,
					   &iface->addr4_list);
	spin_unlock(&netlbl_unlhsh_lock);
	if (list_entry != NULL)
		entry = netlbl_unlhsh_addr4_entry(list_entry);
	else
		entry = NULL;

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_UNLBL_STCDEL,
					      audit_info);
	if (audit_buf != NULL) {
		dev = dev_get_by_index(net, iface->ifindex);
		netlbl_af4list_audit_addr(audit_buf, 1,
					  (dev != NULL ? dev->name : NULL),
					  addr->s_addr, mask->s_addr);
		if (dev != NULL)
			dev_put(dev);
		if (entry != NULL &&
		    security_secid_to_secctx(entry->secid,
					     &secctx, &secctx_len) == 0) {
			audit_log_format(audit_buf, " sec_obj=%s", secctx);
			security_release_secctx(secctx, secctx_len);
		}
		audit_log_format(audit_buf, " res=%u", entry != NULL ? 1 : 0);
		audit_log_end(audit_buf);
	}

	if (entry == NULL)
		return -ENOENT;

	kfree_rcu(entry, rcu);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
/**
 * netlbl_unlhsh_remove_addr6 - Remove an IPv6 address entry
 * @net: network namespace
 * @iface: interface entry
 * @addr: IP address
 * @mask: IP address mask
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Remove an IP address entry from the unlabeled connection hash table.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlhsh_remove_addr6(struct net *net,
				      struct netlbl_unlhsh_iface *iface,
				      const struct in6_addr *addr,
				      const struct in6_addr *mask,
				      struct netlbl_audit *audit_info)
{
	struct netlbl_af6list *list_entry;
	struct netlbl_unlhsh_addr6 *entry;
	struct audit_buffer *audit_buf;
	struct net_device *dev;
	char *secctx;
	u32 secctx_len;

	spin_lock(&netlbl_unlhsh_lock);
	list_entry = netlbl_af6list_remove(addr, mask, &iface->addr6_list);
	spin_unlock(&netlbl_unlhsh_lock);
	if (list_entry != NULL)
		entry = netlbl_unlhsh_addr6_entry(list_entry);
	else
		entry = NULL;

	audit_buf = netlbl_audit_start_common(AUDIT_MAC_UNLBL_STCDEL,
					      audit_info);
	if (audit_buf != NULL) {
		dev = dev_get_by_index(net, iface->ifindex);
		netlbl_af6list_audit_addr(audit_buf, 1,
					  (dev != NULL ? dev->name : NULL),
					  addr, mask);
		if (dev != NULL)
			dev_put(dev);
		if (entry != NULL &&
		    security_secid_to_secctx(entry->secid,
					     &secctx, &secctx_len) == 0) {
			audit_log_format(audit_buf, " sec_obj=%s", secctx);
			security_release_secctx(secctx, secctx_len);
		}
		audit_log_format(audit_buf, " res=%u", entry != NULL ? 1 : 0);
		audit_log_end(audit_buf);
	}

	if (entry == NULL)
		return -ENOENT;

	kfree_rcu(entry, rcu);
	return 0;
}
#endif /* IPv6 */

/**
 * netlbl_unlhsh_condremove_iface - Remove an interface entry
 * @iface: the interface entry
 *
 * Description:
 * Remove an interface entry from the unlabeled connection hash table if it is
 * empty.  An interface entry is considered to be empty if there are no
 * address entries assigned to it.
 *
 */
static void netlbl_unlhsh_condremove_iface(struct netlbl_unlhsh_iface *iface)
{
	struct netlbl_af4list *iter4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *iter6;
#endif /* IPv6 */

	spin_lock(&netlbl_unlhsh_lock);
	netlbl_af4list_foreach_rcu(iter4, &iface->addr4_list)
		goto unlhsh_condremove_failure;
#if IS_ENABLED(CONFIG_IPV6)
	netlbl_af6list_foreach_rcu(iter6, &iface->addr6_list)
		goto unlhsh_condremove_failure;
#endif /* IPv6 */
	iface->valid = 0;
	if (iface->ifindex > 0)
		list_del_rcu(&iface->list);
	else
		RCU_INIT_POINTER(netlbl_unlhsh_def, NULL);
	spin_unlock(&netlbl_unlhsh_lock);

	call_rcu(&iface->rcu, netlbl_unlhsh_free_iface);
	return;

unlhsh_condremove_failure:
	spin_unlock(&netlbl_unlhsh_lock);
}

/**
 * netlbl_unlhsh_remove - Remove an entry from the unlabeled hash table
 * @net: network namespace
 * @dev_name: interface name
 * @addr: IP address in network byte order
 * @mask: address mask in network byte order
 * @addr_len: length of address/mask (4 for IPv4, 16 for IPv6)
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Removes and existing entry from the unlabeled connection hash table.
 * Returns zero on success, negative values on failure.
 *
 */
int netlbl_unlhsh_remove(struct net *net,
			 const char *dev_name,
			 const void *addr,
			 const void *mask,
			 u32 addr_len,
			 struct netlbl_audit *audit_info)
{
	int ret_val;
	struct net_device *dev;
	struct netlbl_unlhsh_iface *iface;

	if (addr_len != sizeof(struct in_addr) &&
	    addr_len != sizeof(struct in6_addr))
		return -EINVAL;

	rcu_read_lock();
	if (dev_name != NULL) {
		dev = dev_get_by_name_rcu(net, dev_name);
		if (dev == NULL) {
			ret_val = -ENODEV;
			goto unlhsh_remove_return;
		}
		iface = netlbl_unlhsh_search_iface(dev->ifindex);
	} else
		iface = rcu_dereference(netlbl_unlhsh_def);
	if (iface == NULL) {
		ret_val = -ENOENT;
		goto unlhsh_remove_return;
	}
	switch (addr_len) {
	case sizeof(struct in_addr):
		ret_val = netlbl_unlhsh_remove_addr4(net,
						     iface, addr, mask,
						     audit_info);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case sizeof(struct in6_addr):
		ret_val = netlbl_unlhsh_remove_addr6(net,
						     iface, addr, mask,
						     audit_info);
		break;
#endif /* IPv6 */
	default:
		ret_val = -EINVAL;
	}
	if (ret_val == 0) {
		netlbl_unlhsh_condremove_iface(iface);
		atomic_dec(&netlabel_mgmt_protocount);
	}

unlhsh_remove_return:
	rcu_read_unlock();
	return ret_val;
}

/*
 * General Helper Functions
 */

/**
 * netlbl_unlhsh_netdev_handler - Network device notification handler
 * @this: notifier block
 * @event: the event
 * @ptr: the network device (cast to void)
 *
 * Description:
 * Handle network device events, although at present all we care about is a
 * network device going away.  In the case of a device going away we clear any
 * related entries from the unlabeled connection hash table.
 *
 */
static int netlbl_unlhsh_netdev_handler(struct notifier_block *this,
					unsigned long event,
					void *ptr)
{
	struct net_device *dev = ptr;
	struct netlbl_unlhsh_iface *iface = NULL;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	/* XXX - should this be a check for NETDEV_DOWN or _UNREGISTER? */
	if (event == NETDEV_DOWN) {
		spin_lock(&netlbl_unlhsh_lock);
		iface = netlbl_unlhsh_search_iface(dev->ifindex);
		if (iface != NULL && iface->valid) {
			iface->valid = 0;
			list_del_rcu(&iface->list);
		} else
			iface = NULL;
		spin_unlock(&netlbl_unlhsh_lock);
	}

	if (iface != NULL)
		call_rcu(&iface->rcu, netlbl_unlhsh_free_iface);

	return NOTIFY_DONE;
}

/**
 * netlbl_unlabel_acceptflg_set - Set the unlabeled accept flag
 * @value: desired value
 * @audit_info: NetLabel audit information
 *
 * Description:
 * Set the value of the unlabeled accept flag to @value.
 *
 */
static void netlbl_unlabel_acceptflg_set(u8 value,
					 struct netlbl_audit *audit_info)
{
	struct audit_buffer *audit_buf;
	u8 old_val;

	old_val = netlabel_unlabel_acceptflg;
	netlabel_unlabel_acceptflg = value;
	audit_buf = netlbl_audit_start_common(AUDIT_MAC_UNLBL_ALLOW,
					      audit_info);
	if (audit_buf != NULL) {
		audit_log_format(audit_buf,
				 " unlbl_accept=%u old=%u", value, old_val);
		audit_log_end(audit_buf);
	}
}

/**
 * netlbl_unlabel_addrinfo_get - Get the IPv4/6 address information
 * @info: the Generic NETLINK info block
 * @addr: the IP address
 * @mask: the IP address mask
 * @len: the address length
 *
 * Description:
 * Examine the Generic NETLINK message and extract the IP address information.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlabel_addrinfo_get(struct genl_info *info,
				       void **addr,
				       void **mask,
				       u32 *len)
{
	u32 addr_len;

	if (info->attrs[NLBL_UNLABEL_A_IPV4ADDR]) {
		addr_len = nla_len(info->attrs[NLBL_UNLABEL_A_IPV4ADDR]);
		if (addr_len != sizeof(struct in_addr) &&
		    addr_len != nla_len(info->attrs[NLBL_UNLABEL_A_IPV4MASK]))
			return -EINVAL;
		*len = addr_len;
		*addr = nla_data(info->attrs[NLBL_UNLABEL_A_IPV4ADDR]);
		*mask = nla_data(info->attrs[NLBL_UNLABEL_A_IPV4MASK]);
		return 0;
	} else if (info->attrs[NLBL_UNLABEL_A_IPV6ADDR]) {
		addr_len = nla_len(info->attrs[NLBL_UNLABEL_A_IPV6ADDR]);
		if (addr_len != sizeof(struct in6_addr) &&
		    addr_len != nla_len(info->attrs[NLBL_UNLABEL_A_IPV6MASK]))
			return -EINVAL;
		*len = addr_len;
		*addr = nla_data(info->attrs[NLBL_UNLABEL_A_IPV6ADDR]);
		*mask = nla_data(info->attrs[NLBL_UNLABEL_A_IPV6MASK]);
		return 0;
	}

	return -EINVAL;
}

/*
 * NetLabel Command Handlers
 */

/**
 * netlbl_unlabel_accept - Handle an ACCEPT message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated ACCEPT message and set the accept flag accordingly.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlabel_accept(struct sk_buff *skb, struct genl_info *info)
{
	u8 value;
	struct netlbl_audit audit_info;

	if (info->attrs[NLBL_UNLABEL_A_ACPTFLG]) {
		value = nla_get_u8(info->attrs[NLBL_UNLABEL_A_ACPTFLG]);
		if (value == 1 || value == 0) {
			netlbl_netlink_auditinfo(skb, &audit_info);
			netlbl_unlabel_acceptflg_set(value, &audit_info);
			return 0;
		}
	}

	return -EINVAL;
}

/**
 * netlbl_unlabel_list - Handle a LIST message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated LIST message and respond with the current status.
 * Returns zero on success, negative values on failure.
 *
 */
static int netlbl_unlabel_list(struct sk_buff *skb, struct genl_info *info)
{
	int ret_val = -EINVAL;
	struct sk_buff *ans_skb;
	void *data;

	ans_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (ans_skb == NULL)
		goto list_failure;
	data = genlmsg_put_reply(ans_skb, info, &netlbl_unlabel_gnl_family,
				 0, NLBL_UNLABEL_C_LIST);
	if (data == NULL) {
		ret_val = -ENOMEM;
		goto list_failure;
	}

	ret_val = nla_put_u8(ans_skb,
			     NLBL_UNLABEL_A_ACPTFLG,
			     netlabel_unlabel_acceptflg);
	if (ret_val != 0)
		goto list_failure;

	genlmsg_end(ans_skb, data);
	return genlmsg_reply(ans_skb, info);

list_failure:
	kfree_skb(ans_skb);
	return ret_val;
}

/**
 * netlbl_unlabel_staticadd - Handle a STATICADD message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated STATICADD message and add a new unlabeled
 * connection entry to the hash table.  Returns zero on success, negative
 * values on failure.
 *
 */
static int netlbl_unlabel_staticadd(struct sk_buff *skb,
				    struct genl_info *info)
{
	int ret_val;
	char *dev_name;
	void *addr;
	void *mask;
	u32 addr_len;
	u32 secid;
	struct netlbl_audit audit_info;

	/* Don't allow users to add both IPv4 and IPv6 addresses for a
	 * single entry.  However, allow users to create two entries, one each
	 * for IPv4 and IPv4, with the same LSM security context which should
	 * achieve the same result. */
	if (!info->attrs[NLBL_UNLABEL_A_SECCTX] ||
	    !info->attrs[NLBL_UNLABEL_A_IFACE] ||
	    !((!info->attrs[NLBL_UNLABEL_A_IPV4ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV4MASK]) ^
	      (!info->attrs[NLBL_UNLABEL_A_IPV6ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV6MASK])))
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);

	ret_val = netlbl_unlabel_addrinfo_get(info, &addr, &mask, &addr_len);
	if (ret_val != 0)
		return ret_val;
	dev_name = nla_data(info->attrs[NLBL_UNLABEL_A_IFACE]);
	ret_val = security_secctx_to_secid(
		                  nla_data(info->attrs[NLBL_UNLABEL_A_SECCTX]),
				  nla_len(info->attrs[NLBL_UNLABEL_A_SECCTX]),
				  &secid);
	if (ret_val != 0)
		return ret_val;

	return netlbl_unlhsh_add(&init_net,
				 dev_name, addr, mask, addr_len, secid,
				 &audit_info);
}

/**
 * netlbl_unlabel_staticadddef - Handle a STATICADDDEF message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated STATICADDDEF message and add a new default
 * unlabeled connection entry.  Returns zero on success, negative values on
 * failure.
 *
 */
static int netlbl_unlabel_staticadddef(struct sk_buff *skb,
				       struct genl_info *info)
{
	int ret_val;
	void *addr;
	void *mask;
	u32 addr_len;
	u32 secid;
	struct netlbl_audit audit_info;

	/* Don't allow users to add both IPv4 and IPv6 addresses for a
	 * single entry.  However, allow users to create two entries, one each
	 * for IPv4 and IPv6, with the same LSM security context which should
	 * achieve the same result. */
	if (!info->attrs[NLBL_UNLABEL_A_SECCTX] ||
	    !((!info->attrs[NLBL_UNLABEL_A_IPV4ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV4MASK]) ^
	      (!info->attrs[NLBL_UNLABEL_A_IPV6ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV6MASK])))
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);

	ret_val = netlbl_unlabel_addrinfo_get(info, &addr, &mask, &addr_len);
	if (ret_val != 0)
		return ret_val;
	ret_val = security_secctx_to_secid(
		                  nla_data(info->attrs[NLBL_UNLABEL_A_SECCTX]),
				  nla_len(info->attrs[NLBL_UNLABEL_A_SECCTX]),
				  &secid);
	if (ret_val != 0)
		return ret_val;

	return netlbl_unlhsh_add(&init_net,
				 NULL, addr, mask, addr_len, secid,
				 &audit_info);
}

/**
 * netlbl_unlabel_staticremove - Handle a STATICREMOVE message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated STATICREMOVE message and remove the specified
 * unlabeled connection entry.  Returns zero on success, negative values on
 * failure.
 *
 */
static int netlbl_unlabel_staticremove(struct sk_buff *skb,
				       struct genl_info *info)
{
	int ret_val;
	char *dev_name;
	void *addr;
	void *mask;
	u32 addr_len;
	struct netlbl_audit audit_info;

	/* See the note in netlbl_unlabel_staticadd() about not allowing both
	 * IPv4 and IPv6 in the same entry. */
	if (!info->attrs[NLBL_UNLABEL_A_IFACE] ||
	    !((!info->attrs[NLBL_UNLABEL_A_IPV4ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV4MASK]) ^
	      (!info->attrs[NLBL_UNLABEL_A_IPV6ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV6MASK])))
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);

	ret_val = netlbl_unlabel_addrinfo_get(info, &addr, &mask, &addr_len);
	if (ret_val != 0)
		return ret_val;
	dev_name = nla_data(info->attrs[NLBL_UNLABEL_A_IFACE]);

	return netlbl_unlhsh_remove(&init_net,
				    dev_name, addr, mask, addr_len,
				    &audit_info);
}

/**
 * netlbl_unlabel_staticremovedef - Handle a STATICREMOVEDEF message
 * @skb: the NETLINK buffer
 * @info: the Generic NETLINK info block
 *
 * Description:
 * Process a user generated STATICREMOVEDEF message and remove the default
 * unlabeled connection entry.  Returns zero on success, negative values on
 * failure.
 *
 */
static int netlbl_unlabel_staticremovedef(struct sk_buff *skb,
					  struct genl_info *info)
{
	int ret_val;
	void *addr;
	void *mask;
	u32 addr_len;
	struct netlbl_audit audit_info;

	/* See the note in netlbl_unlabel_staticadd() about not allowing both
	 * IPv4 and IPv6 in the same entry. */
	if (!((!info->attrs[NLBL_UNLABEL_A_IPV4ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV4MASK]) ^
	      (!info->attrs[NLBL_UNLABEL_A_IPV6ADDR] ||
	       !info->attrs[NLBL_UNLABEL_A_IPV6MASK])))
		return -EINVAL;

	netlbl_netlink_auditinfo(skb, &audit_info);

	ret_val = netlbl_unlabel_addrinfo_get(info, &addr, &mask, &addr_len);
	if (ret_val != 0)
		return ret_val;

	return netlbl_unlhsh_remove(&init_net,
				    NULL, addr, mask, addr_len,
				    &audit_info);
}


/**
 * netlbl_unlabel_staticlist_gen - Generate messages for STATICLIST[DEF]
 * @cmd: command/message
 * @iface: the interface entry
 * @addr4: the IPv4 address entry
 * @addr6: the IPv6 address entry
 * @arg: the netlbl_unlhsh_walk_arg structure
 *
 * Description:
 * This function is designed to be used to generate a response for a
 * STATICLIST or STATICLISTDEF message.  When called either @addr4 or @addr6
 * can be specified, not both, the other unspecified entry should be set to
 * NULL by the caller.  Returns the size of the message on success, negative
 * values on failure.
 *
 */
static int netlbl_unlabel_staticlist_gen(u32 cmd,
				       const struct netlbl_unlhsh_iface *iface,
				       const struct netlbl_unlhsh_addr4 *addr4,
				       const struct netlbl_unlhsh_addr6 *addr6,
				       void *arg)
{
	int ret_val = -ENOMEM;
	struct netlbl_unlhsh_walk_arg *cb_arg = arg;
	struct net_device *dev;
	void *data;
	u32 secid;
	char *secctx;
	u32 secctx_len;

	data = genlmsg_put(cb_arg->skb, NETLINK_CB(cb_arg->nl_cb->skb).pid,
			   cb_arg->seq, &netlbl_unlabel_gnl_family,
			   NLM_F_MULTI, cmd);
	if (data == NULL)
		goto list_cb_failure;

	if (iface->ifindex > 0) {
		dev = dev_get_by_index(&init_net, iface->ifindex);
		if (!dev) {
			ret_val = -ENODEV;
			goto list_cb_failure;
		}
		ret_val = nla_put_string(cb_arg->skb,
					 NLBL_UNLABEL_A_IFACE, dev->name);
		dev_put(dev);
		if (ret_val != 0)
			goto list_cb_failure;
	}

	if (addr4) {
		struct in_addr addr_struct;

		addr_struct.s_addr = addr4->list.addr;
		ret_val = nla_put(cb_arg->skb,
				  NLBL_UNLABEL_A_IPV4ADDR,
				  sizeof(struct in_addr),
				  &addr_struct);
		if (ret_val != 0)
			goto list_cb_failure;

		addr_struct.s_addr = addr4->list.mask;
		ret_val = nla_put(cb_arg->skb,
				  NLBL_UNLABEL_A_IPV4MASK,
				  sizeof(struct in_addr),
				  &addr_struct);
		if (ret_val != 0)
			goto list_cb_failure;

		secid = addr4->secid;
	} else {
		ret_val = nla_put(cb_arg->skb,
				  NLBL_UNLABEL_A_IPV6ADDR,
				  sizeof(struct in6_addr),
				  &addr6->list.addr);
		if (ret_val != 0)
			goto list_cb_failure;

		ret_val = nla_put(cb_arg->skb,
				  NLBL_UNLABEL_A_IPV6MASK,
				  sizeof(struct in6_addr),
				  &addr6->list.mask);
		if (ret_val != 0)
			goto list_cb_failure;

		secid = addr6->secid;
	}

	ret_val = security_secid_to_secctx(secid, &secctx, &secctx_len);
	if (ret_val != 0)
		goto list_cb_failure;
	ret_val = nla_put(cb_arg->skb,
			  NLBL_UNLABEL_A_SECCTX,
			  secctx_len,
			  secctx);
	security_release_secctx(secctx, secctx_len);
	if (ret_val != 0)
		goto list_cb_failure;

	cb_arg->seq++;
	return genlmsg_end(cb_arg->skb, data);

list_cb_failure:
	genlmsg_cancel(cb_arg->skb, data);
	return ret_val;
}

/**
 * netlbl_unlabel_staticlist - Handle a STATICLIST message
 * @skb: the NETLINK buffer
 * @cb: the NETLINK callback
 *
 * Description:
 * Process a user generated STATICLIST message and dump the unlabeled
 * connection hash table in a form suitable for use in a kernel generated
 * STATICLIST message.  Returns the length of @skb.
 *
 */
static int netlbl_unlabel_staticlist(struct sk_buff *skb,
				     struct netlink_callback *cb)
{
	struct netlbl_unlhsh_walk_arg cb_arg;
	u32 skip_bkt = cb->args[0];
	u32 skip_chain = cb->args[1];
	u32 skip_addr4 = cb->args[2];
	u32 skip_addr6 = cb->args[3];
	u32 iter_bkt;
	u32 iter_chain = 0, iter_addr4 = 0, iter_addr6 = 0;
	struct netlbl_unlhsh_iface *iface;
	struct list_head *iter_list;
	struct netlbl_af4list *addr4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netlbl_af6list *addr6;
#endif

	cb_arg.nl_cb = cb;
	cb_arg.skb = skb;
	cb_arg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	for (iter_bkt = skip_bkt;
	     iter_bkt < rcu_dereference(netlbl_unlhsh)->size;
	     iter_bkt++, iter_chain = 0, iter_addr4 = 0, iter_addr6 = 0) {
		iter_list = &rcu_dereference(netlbl_unlhsh)->tbl[iter_bkt];
		list_for_each_entry_rcu(iface, iter_list, list) {
			if (!iface->valid ||
			    iter_chain++ < skip_chain)
				continue;
			netlbl_af4list_foreach_rcu(addr4,
						   &iface->addr4_list) {
				if (iter_addr4++ < skip_addr4)
					continue;
				if (netlbl_unlabel_staticlist_gen(
					      NLBL_UNLABEL_C_STATICLIST,
					      iface,
					      netlbl_unlhsh_addr4_entry(addr4),
					      NULL,
					      &cb_arg) < 0) {
					iter_addr4--;
					iter_chain--;
					goto unlabel_staticlist_return;
				}
			}
#if IS_ENABLED(CONFIG_IPV6)
			netlbl_af6list_foreach_rcu(addr6,
						   &iface->addr6_list) {
				if (iter_addr6++ < skip_addr6)
					continue;
				if (netlbl_unlabel_staticlist_gen(
					      NLBL_UNLABEL_C_STATICLIST,
					      iface,
					      NULL,
					      netlbl_unlhsh_addr6_entry(addr6),
					      &cb_arg) < 0) {
					iter_addr6--;
					iter_chain--;
					goto unlabel_staticlist_return;
				}
			}
#endif /* IPv6 */
		}
	}

unlabel_staticlist_return:
	rcu_read_unlock();
	cb->args[0] = skip_bkt;
	cb->args[1] = skip_chain;
	cb->args[2] = skip_addr4;
	cb->args[3] = skip_addr6;
	return skb->len;
}

/**
 * netlbl_unlabel_staticlistdef - Handle a STATICLISTDEF message
 * @skb: the NETLINK buffer
 * @cb: the NETLINK callback
 *
 * Description:
 * Process a user generated STATICLISTDEF message and dump the default
 * unlabeled connection entry in a form suitable for use in a kernel generated
 * STATICLISTDEF message.  Returns the length of @skb.
 *
 */
static int netlbl_unlabel_staticlistdef(struct sk_buff *skb,
					struct netlink_callback *cb)
{
	struct netlbl_unlhsh_walk_arg cb_arg;
	struct netlbl_unlhsh_iface *iface;
	u32 skip_addr4 = cb->args[0];
	u32 skip_addr6 = cb->args[1];
	u32 iter_addr4 = 0;
	struct netlbl_af4list *addr4;
#if IS_ENABLED(CONFIG_IPV6)
	u32 iter_addr6 = 0;
	struct netlbl_af6list *addr6;
#endif

	cb_arg.nl_cb = cb;
	cb_arg.skb = skb;
	cb_arg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	iface = rcu_dereference(netlbl_unlhsh_def);
	if (iface == NULL || !iface->valid)
		goto unlabel_staticlistdef_return;

	netlbl_af4list_foreach_rcu(addr4, &iface->addr4_list) {
		if (iter_addr4++ < skip_addr4)
			continue;
		if (netlbl_unlabel_staticlist_gen(NLBL_UNLABEL_C_STATICLISTDEF,
					      iface,
					      netlbl_unlhsh_addr4_entry(addr4),
					      NULL,
					      &cb_arg) < 0) {
			iter_addr4--;
			goto unlabel_staticlistdef_return;
		}
	}
#if IS_ENABLED(CONFIG_IPV6)
	netlbl_af6list_foreach_rcu(addr6, &iface->addr6_list) {
		if (iter_addr6++ < skip_addr6)
			continue;
		if (netlbl_unlabel_staticlist_gen(NLBL_UNLABEL_C_STATICLISTDEF,
					      iface,
					      NULL,
					      netlbl_unlhsh_addr6_entry(addr6),
					      &cb_arg) < 0) {
			iter_addr6--;
			goto unlabel_staticlistdef_return;
		}
	}
#endif /* IPv6 */

unlabel_staticlistdef_return:
	rcu_read_unlock();
	cb->args[0] = skip_addr4;
	cb->args[1] = skip_addr6;
	return skb->len;
}

/*
 * NetLabel Generic NETLINK Command Definitions
 */

static struct genl_ops netlbl_unlabel_genl_ops[] = {
	{
	.cmd = NLBL_UNLABEL_C_STATICADD,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_unlabel_genl_policy,
	.doit = netlbl_unlabel_staticadd,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_UNLABEL_C_STATICREMOVE,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_unlabel_genl_policy,
	.doit = netlbl_unlabel_staticremove,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_UNLABEL_C_STATICLIST,
	.flags = 0,
	.policy = netlbl_unlabel_genl_policy,
	.doit = NULL,
	.dumpit = netlbl_unlabel_staticlist,
	},
	{
	.cmd = NLBL_UNLABEL_C_STATICADDDEF,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_unlabel_genl_policy,
	.doit = netlbl_unlabel_staticadddef,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_UNLABEL_C_STATICREMOVEDEF,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_unlabel_genl_policy,
	.doit = netlbl_unlabel_staticremovedef,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_UNLABEL_C_STATICLISTDEF,
	.flags = 0,
	.policy = netlbl_unlabel_genl_policy,
	.doit = NULL,
	.dumpit = netlbl_unlabel_staticlistdef,
	},
	{
	.cmd = NLBL_UNLABEL_C_ACCEPT,
	.flags = GENL_ADMIN_PERM,
	.policy = netlbl_unlabel_genl_policy,
	.doit = netlbl_unlabel_accept,
	.dumpit = NULL,
	},
	{
	.cmd = NLBL_UNLABEL_C_LIST,
	.flags = 0,
	.policy = netlbl_unlabel_genl_policy,
	.doit = netlbl_unlabel_list,
	.dumpit = NULL,
	},
};

/*
 * NetLabel Generic NETLINK Protocol Functions
 */

/**
 * netlbl_unlabel_genl_init - Register the Unlabeled NetLabel component
 *
 * Description:
 * Register the unlabeled packet NetLabel component with the Generic NETLINK
 * mechanism.  Returns zero on success, negative values on failure.
 *
 */
int __init netlbl_unlabel_genl_init(void)
{
	return genl_register_family_with_ops(&netlbl_unlabel_gnl_family,
		netlbl_unlabel_genl_ops, ARRAY_SIZE(netlbl_unlabel_genl_ops));
}

/*
 * NetLabel KAPI Hooks
 */

static struct notifier_block netlbl_unlhsh_netdev_notifier = {
	.notifier_call = netlbl_unlhsh_netdev_handler,
};

/**
 * netlbl_unlabel_init - Initialize the unlabeled connection hash table
 * @size: the number of bits to use for the hash buckets
 *
 * Description:
 * Initializes the unlabeled connection hash table and registers a network
 * device notification handler.  This function should only be called by the
 * NetLabel subsystem itself during initialization.  Returns zero on success,
 * non-zero values on error.
 *
 */
int __init netlbl_unlabel_init(u32 size)
{
	u32 iter;
	struct netlbl_unlhsh_tbl *hsh_tbl;

	if (size == 0)
		return -EINVAL;

	hsh_tbl = kmalloc(sizeof(*hsh_tbl), GFP_KERNEL);
	if (hsh_tbl == NULL)
		return -ENOMEM;
	hsh_tbl->size = 1 << size;
	hsh_tbl->tbl = kcalloc(hsh_tbl->size,
			       sizeof(struct list_head),
			       GFP_KERNEL);
	if (hsh_tbl->tbl == NULL) {
		kfree(hsh_tbl);
		return -ENOMEM;
	}
	for (iter = 0; iter < hsh_tbl->size; iter++)
		INIT_LIST_HEAD(&hsh_tbl->tbl[iter]);

	spin_lock(&netlbl_unlhsh_lock);
	rcu_assign_pointer(netlbl_unlhsh, hsh_tbl);
	spin_unlock(&netlbl_unlhsh_lock);

	register_netdevice_notifier(&netlbl_unlhsh_netdev_notifier);

	return 0;
}

/**
 * netlbl_unlabel_getattr - Get the security attributes for an unlabled packet
 * @skb: the packet
 * @family: protocol family
 * @secattr: the security attributes
 *
 * Description:
 * Determine the security attributes, if any, for an unlabled packet and return
 * them in @secattr.  Returns zero on success and negative values on failure.
 *
 */
int netlbl_unlabel_getattr(const struct sk_buff *skb,
			   u16 family,
			   struct netlbl_lsm_secattr *secattr)
{
	struct netlbl_unlhsh_iface *iface;

	rcu_read_lock();
	iface = netlbl_unlhsh_search_iface(skb->skb_iif);
	if (iface == NULL)
		iface = rcu_dereference(netlbl_unlhsh_def);
	if (iface == NULL || !iface->valid)
		goto unlabel_getattr_nolabel;
	switch (family) {
	case PF_INET: {
		struct iphdr *hdr4;
		struct netlbl_af4list *addr4;

		hdr4 = ip_hdr(skb);
		addr4 = netlbl_af4list_search(hdr4->saddr,
					      &iface->addr4_list);
		if (addr4 == NULL)
			goto unlabel_getattr_nolabel;
		secattr->attr.secid = netlbl_unlhsh_addr4_entry(addr4)->secid;
		break;
	}
#if IS_ENABLED(CONFIG_IPV6)
	case PF_INET6: {
		struct ipv6hdr *hdr6;
		struct netlbl_af6list *addr6;

		hdr6 = ipv6_hdr(skb);
		addr6 = netlbl_af6list_search(&hdr6->saddr,
					      &iface->addr6_list);
		if (addr6 == NULL)
			goto unlabel_getattr_nolabel;
		secattr->attr.secid = netlbl_unlhsh_addr6_entry(addr6)->secid;
		break;
	}
#endif /* IPv6 */
	default:
		goto unlabel_getattr_nolabel;
	}
	rcu_read_unlock();

	secattr->flags |= NETLBL_SECATTR_SECID;
	secattr->type = NETLBL_NLTYPE_UNLABELED;
	return 0;

unlabel_getattr_nolabel:
	rcu_read_unlock();
	if (netlabel_unlabel_acceptflg == 0)
		return -ENOMSG;
	secattr->type = NETLBL_NLTYPE_UNLABELED;
	return 0;
}

/**
 * netlbl_unlabel_defconf - Set the default config to allow unlabeled packets
 *
 * Description:
 * Set the default NetLabel configuration to allow incoming unlabeled packets
 * and to send unlabeled network traffic by default.
 *
 */
int __init netlbl_unlabel_defconf(void)
{
	int ret_val;
	struct netlbl_dom_map *entry;
	struct netlbl_audit audit_info;

	/* Only the kernel is allowed to call this function and the only time
	 * it is called is at bootup before the audit subsystem is reporting
	 * messages so don't worry to much about these values. */
	security_task_getsecid(current, &audit_info.secid);
	audit_info.loginuid = GLOBAL_ROOT_UID;
	audit_info.sessionid = 0;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (entry == NULL)
		return -ENOMEM;
	entry->type = NETLBL_NLTYPE_UNLABELED;
	ret_val = netlbl_domhsh_add_default(entry, &audit_info);
	if (ret_val != 0)
		return ret_val;

	netlbl_unlabel_acceptflg_set(1, &audit_info);

	return 0;
}
