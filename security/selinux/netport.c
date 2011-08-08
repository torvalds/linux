/*
 * Network port table
 *
 * SELinux must keep a mapping of network ports to labels/SIDs.  This
 * mapping is maintained as part of the normal policy but a fast cache is
 * needed to reduce the lookup overhead.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 *
 * This code is heavily based on the "netif" concept originally developed by
 * James Morris <jmorris@redhat.com>
 *   (see security/selinux/netif.c for more information)
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2008
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include "netport.h"
#include "objsec.h"

#define SEL_NETPORT_HASH_SIZE       256
#define SEL_NETPORT_HASH_BKT_LIMIT   16

struct sel_netport_bkt {
	int size;
	struct list_head list;
};

struct sel_netport {
	struct netport_security_struct psec;

	struct list_head list;
	struct rcu_head rcu;
};

/* NOTE: we are using a combined hash table for both IPv4 and IPv6, the reason
 * for this is that I suspect most users will not make heavy use of both
 * address families at the same time so one table will usually end up wasted,
 * if this becomes a problem we can always add a hash table for each address
 * family later */

static LIST_HEAD(sel_netport_list);
static DEFINE_SPINLOCK(sel_netport_lock);
static struct sel_netport_bkt sel_netport_hash[SEL_NETPORT_HASH_SIZE];

/**
 * sel_netport_hashfn - Hashing function for the port table
 * @pnum: port number
 *
 * Description:
 * This is the hashing function for the port table, it returns the bucket
 * number for the given port.
 *
 */
static unsigned int sel_netport_hashfn(u16 pnum)
{
	return (pnum & (SEL_NETPORT_HASH_SIZE - 1));
}

/**
 * sel_netport_find - Search for a port record
 * @protocol: protocol
 * @port: pnum
 *
 * Description:
 * Search the network port table and return the matching record.  If an entry
 * can not be found in the table return NULL.
 *
 */
static struct sel_netport *sel_netport_find(u8 protocol, u16 pnum)
{
	unsigned int idx;
	struct sel_netport *port;

	idx = sel_netport_hashfn(pnum);
	list_for_each_entry_rcu(port, &sel_netport_hash[idx].list, list)
		if (port->psec.port == pnum && port->psec.protocol == protocol)
			return port;

	return NULL;
}

/**
 * sel_netport_insert - Insert a new port into the table
 * @port: the new port record
 *
 * Description:
 * Add a new port record to the network address hash table.
 *
 */
static void sel_netport_insert(struct sel_netport *port)
{
	unsigned int idx;

	/* we need to impose a limit on the growth of the hash table so check
	 * this bucket to make sure it is within the specified bounds */
	idx = sel_netport_hashfn(port->psec.port);
	list_add_rcu(&port->list, &sel_netport_hash[idx].list);
	if (sel_netport_hash[idx].size == SEL_NETPORT_HASH_BKT_LIMIT) {
		struct sel_netport *tail;
		tail = list_entry(
			rcu_dereference(sel_netport_hash[idx].list.prev),
			struct sel_netport, list);
		list_del_rcu(&tail->list);
		kfree_rcu(tail, rcu);
	} else
		sel_netport_hash[idx].size++;
}

/**
 * sel_netport_sid_slow - Lookup the SID of a network address using the policy
 * @protocol: protocol
 * @pnum: port
 * @sid: port SID
 *
 * Description:
 * This function determines the SID of a network port by quering the security
 * policy.  The result is added to the network port table to speedup future
 * queries.  Returns zero on success, negative values on failure.
 *
 */
static int sel_netport_sid_slow(u8 protocol, u16 pnum, u32 *sid)
{
	int ret = -ENOMEM;
	struct sel_netport *port;
	struct sel_netport *new = NULL;

	spin_lock_bh(&sel_netport_lock);
	port = sel_netport_find(protocol, pnum);
	if (port != NULL) {
		*sid = port->psec.sid;
		spin_unlock_bh(&sel_netport_lock);
		return 0;
	}
	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (new == NULL)
		goto out;
	ret = security_port_sid(protocol, pnum, sid);
	if (ret != 0)
		goto out;

	new->psec.port = pnum;
	new->psec.protocol = protocol;
	new->psec.sid = *sid;
	sel_netport_insert(new);

out:
	spin_unlock_bh(&sel_netport_lock);
	if (unlikely(ret)) {
		printk(KERN_WARNING
		       "SELinux: failure in sel_netport_sid_slow(),"
		       " unable to determine network port label\n");
		kfree(new);
	}
	return ret;
}

/**
 * sel_netport_sid - Lookup the SID of a network port
 * @protocol: protocol
 * @pnum: port
 * @sid: port SID
 *
 * Description:
 * This function determines the SID of a network port using the fastest method
 * possible.  First the port table is queried, but if an entry can't be found
 * then the policy is queried and the result is added to the table to speedup
 * future queries.  Returns zero on success, negative values on failure.
 *
 */
int sel_netport_sid(u8 protocol, u16 pnum, u32 *sid)
{
	struct sel_netport *port;

	rcu_read_lock();
	port = sel_netport_find(protocol, pnum);
	if (port != NULL) {
		*sid = port->psec.sid;
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	return sel_netport_sid_slow(protocol, pnum, sid);
}

/**
 * sel_netport_flush - Flush the entire network port table
 *
 * Description:
 * Remove all entries from the network address table.
 *
 */
static void sel_netport_flush(void)
{
	unsigned int idx;
	struct sel_netport *port, *port_tmp;

	spin_lock_bh(&sel_netport_lock);
	for (idx = 0; idx < SEL_NETPORT_HASH_SIZE; idx++) {
		list_for_each_entry_safe(port, port_tmp,
					 &sel_netport_hash[idx].list, list) {
			list_del_rcu(&port->list);
			kfree_rcu(port, rcu);
		}
		sel_netport_hash[idx].size = 0;
	}
	spin_unlock_bh(&sel_netport_lock);
}

static int sel_netport_avc_callback(u32 event, u32 ssid, u32 tsid,
				    u16 class, u32 perms, u32 *retained)
{
	if (event == AVC_CALLBACK_RESET) {
		sel_netport_flush();
		synchronize_net();
	}
	return 0;
}

static __init int sel_netport_init(void)
{
	int iter;
	int ret;

	if (!selinux_enabled)
		return 0;

	for (iter = 0; iter < SEL_NETPORT_HASH_SIZE; iter++) {
		INIT_LIST_HEAD(&sel_netport_hash[iter].list);
		sel_netport_hash[iter].size = 0;
	}

	ret = avc_add_callback(sel_netport_avc_callback, AVC_CALLBACK_RESET,
			       SECSID_NULL, SECSID_NULL, SECCLASS_NULL, 0);
	if (ret != 0)
		panic("avc_add_callback() failed, error %d\n", ret);

	return ret;
}

__initcall(sel_netport_init);
