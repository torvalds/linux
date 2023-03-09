// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Facebook Inc.

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/udp_tunnel.h>
#include <net/vxlan.h>

enum udp_tunnel_nic_table_entry_flags {
	UDP_TUNNEL_NIC_ENTRY_ADD	= BIT(0),
	UDP_TUNNEL_NIC_ENTRY_DEL	= BIT(1),
	UDP_TUNNEL_NIC_ENTRY_OP_FAIL	= BIT(2),
	UDP_TUNNEL_NIC_ENTRY_FROZEN	= BIT(3),
};

struct udp_tunnel_nic_table_entry {
	__be16 port;
	u8 type;
	u8 flags;
	u16 use_cnt;
#define UDP_TUNNEL_NIC_USE_CNT_MAX	U16_MAX
	u8 hw_priv;
};

/**
 * struct udp_tunnel_nic - UDP tunnel port offload state
 * @work:	async work for talking to hardware from process context
 * @dev:	netdev pointer
 * @need_sync:	at least one port start changed
 * @need_replay: space was freed, we need a replay of all ports
 * @work_pending: @work is currently scheduled
 * @n_tables:	number of tables under @entries
 * @missed:	bitmap of tables which overflown
 * @entries:	table of tables of ports currently offloaded
 */
struct udp_tunnel_nic {
	struct work_struct work;

	struct net_device *dev;

	u8 need_sync:1;
	u8 need_replay:1;
	u8 work_pending:1;

	unsigned int n_tables;
	unsigned long missed;
	struct udp_tunnel_nic_table_entry **entries;
};

/* We ensure all work structs are done using driver state, but not the code.
 * We need a workqueue we can flush before module gets removed.
 */
static struct workqueue_struct *udp_tunnel_nic_workqueue;

static const char *udp_tunnel_nic_tunnel_type_name(unsigned int type)
{
	switch (type) {
	case UDP_TUNNEL_TYPE_VXLAN:
		return "vxlan";
	case UDP_TUNNEL_TYPE_GENEVE:
		return "geneve";
	case UDP_TUNNEL_TYPE_VXLAN_GPE:
		return "vxlan-gpe";
	default:
		return "unknown";
	}
}

static bool
udp_tunnel_nic_entry_is_free(struct udp_tunnel_nic_table_entry *entry)
{
	return entry->use_cnt == 0 && !entry->flags;
}

static bool
udp_tunnel_nic_entry_is_present(struct udp_tunnel_nic_table_entry *entry)
{
	return entry->use_cnt && !(entry->flags & ~UDP_TUNNEL_NIC_ENTRY_FROZEN);
}

static bool
udp_tunnel_nic_entry_is_frozen(struct udp_tunnel_nic_table_entry *entry)
{
	return entry->flags & UDP_TUNNEL_NIC_ENTRY_FROZEN;
}

static void
udp_tunnel_nic_entry_freeze_used(struct udp_tunnel_nic_table_entry *entry)
{
	if (!udp_tunnel_nic_entry_is_free(entry))
		entry->flags |= UDP_TUNNEL_NIC_ENTRY_FROZEN;
}

static void
udp_tunnel_nic_entry_unfreeze(struct udp_tunnel_nic_table_entry *entry)
{
	entry->flags &= ~UDP_TUNNEL_NIC_ENTRY_FROZEN;
}

static bool
udp_tunnel_nic_entry_is_queued(struct udp_tunnel_nic_table_entry *entry)
{
	return entry->flags & (UDP_TUNNEL_NIC_ENTRY_ADD |
			       UDP_TUNNEL_NIC_ENTRY_DEL);
}

static void
udp_tunnel_nic_entry_queue(struct udp_tunnel_nic *utn,
			   struct udp_tunnel_nic_table_entry *entry,
			   unsigned int flag)
{
	entry->flags |= flag;
	utn->need_sync = 1;
}

static void
udp_tunnel_nic_ti_from_entry(struct udp_tunnel_nic_table_entry *entry,
			     struct udp_tunnel_info *ti)
{
	memset(ti, 0, sizeof(*ti));
	ti->port = entry->port;
	ti->type = entry->type;
	ti->hw_priv = entry->hw_priv;
}

static bool
udp_tunnel_nic_is_empty(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	unsigned int i, j;

	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++)
			if (!udp_tunnel_nic_entry_is_free(&utn->entries[i][j]))
				return false;
	return true;
}

static bool
udp_tunnel_nic_should_replay(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_table_info *table;
	unsigned int i, j;

	if (!utn->missed)
		return false;

	for (i = 0; i < utn->n_tables; i++) {
		table = &dev->udp_tunnel_nic_info->tables[i];
		if (!test_bit(i, &utn->missed))
			continue;

		for (j = 0; j < table->n_entries; j++)
			if (udp_tunnel_nic_entry_is_free(&utn->entries[i][j]))
				return true;
	}

	return false;
}

static void
__udp_tunnel_nic_get_port(struct net_device *dev, unsigned int table,
			  unsigned int idx, struct udp_tunnel_info *ti)
{
	struct udp_tunnel_nic_table_entry *entry;
	struct udp_tunnel_nic *utn;

	utn = dev->udp_tunnel_nic;
	entry = &utn->entries[table][idx];

	if (entry->use_cnt)
		udp_tunnel_nic_ti_from_entry(entry, ti);
}

static void
__udp_tunnel_nic_set_port_priv(struct net_device *dev, unsigned int table,
			       unsigned int idx, u8 priv)
{
	dev->udp_tunnel_nic->entries[table][idx].hw_priv = priv;
}

static void
udp_tunnel_nic_entry_update_done(struct udp_tunnel_nic_table_entry *entry,
				 int err)
{
	bool dodgy = entry->flags & UDP_TUNNEL_NIC_ENTRY_OP_FAIL;

	WARN_ON_ONCE(entry->flags & UDP_TUNNEL_NIC_ENTRY_ADD &&
		     entry->flags & UDP_TUNNEL_NIC_ENTRY_DEL);

	if (entry->flags & UDP_TUNNEL_NIC_ENTRY_ADD &&
	    (!err || (err == -EEXIST && dodgy)))
		entry->flags &= ~UDP_TUNNEL_NIC_ENTRY_ADD;

	if (entry->flags & UDP_TUNNEL_NIC_ENTRY_DEL &&
	    (!err || (err == -ENOENT && dodgy)))
		entry->flags &= ~UDP_TUNNEL_NIC_ENTRY_DEL;

	if (!err)
		entry->flags &= ~UDP_TUNNEL_NIC_ENTRY_OP_FAIL;
	else
		entry->flags |= UDP_TUNNEL_NIC_ENTRY_OP_FAIL;
}

static void
udp_tunnel_nic_device_sync_one(struct net_device *dev,
			       struct udp_tunnel_nic *utn,
			       unsigned int table, unsigned int idx)
{
	struct udp_tunnel_nic_table_entry *entry;
	struct udp_tunnel_info ti;
	int err;

	entry = &utn->entries[table][idx];
	if (!udp_tunnel_nic_entry_is_queued(entry))
		return;

	udp_tunnel_nic_ti_from_entry(entry, &ti);
	if (entry->flags & UDP_TUNNEL_NIC_ENTRY_ADD)
		err = dev->udp_tunnel_nic_info->set_port(dev, table, idx, &ti);
	else
		err = dev->udp_tunnel_nic_info->unset_port(dev, table, idx,
							   &ti);
	udp_tunnel_nic_entry_update_done(entry, err);

	if (err)
		netdev_warn(dev,
			    "UDP tunnel port sync failed port %d type %s: %d\n",
			    be16_to_cpu(entry->port),
			    udp_tunnel_nic_tunnel_type_name(entry->type),
			    err);
}

static void
udp_tunnel_nic_device_sync_by_port(struct net_device *dev,
				   struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	unsigned int i, j;

	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++)
			udp_tunnel_nic_device_sync_one(dev, utn, i, j);
}

static void
udp_tunnel_nic_device_sync_by_table(struct net_device *dev,
				    struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	unsigned int i, j;
	int err;

	for (i = 0; i < utn->n_tables; i++) {
		/* Find something that needs sync in this table */
		for (j = 0; j < info->tables[i].n_entries; j++)
			if (udp_tunnel_nic_entry_is_queued(&utn->entries[i][j]))
				break;
		if (j == info->tables[i].n_entries)
			continue;

		err = info->sync_table(dev, i);
		if (err)
			netdev_warn(dev, "UDP tunnel port sync failed for table %d: %d\n",
				    i, err);

		for (j = 0; j < info->tables[i].n_entries; j++) {
			struct udp_tunnel_nic_table_entry *entry;

			entry = &utn->entries[i][j];
			if (udp_tunnel_nic_entry_is_queued(entry))
				udp_tunnel_nic_entry_update_done(entry, err);
		}
	}
}

static void
__udp_tunnel_nic_device_sync(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	if (!utn->need_sync)
		return;

	if (dev->udp_tunnel_nic_info->sync_table)
		udp_tunnel_nic_device_sync_by_table(dev, utn);
	else
		udp_tunnel_nic_device_sync_by_port(dev, utn);

	utn->need_sync = 0;
	/* Can't replay directly here, in case we come from the tunnel driver's
	 * notification - trying to replay may deadlock inside tunnel driver.
	 */
	utn->need_replay = udp_tunnel_nic_should_replay(dev, utn);
}

static void
udp_tunnel_nic_device_sync(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	bool may_sleep;

	if (!utn->need_sync)
		return;

	/* Drivers which sleep in the callback need to update from
	 * the workqueue, if we come from the tunnel driver's notification.
	 */
	may_sleep = info->flags & UDP_TUNNEL_NIC_INFO_MAY_SLEEP;
	if (!may_sleep)
		__udp_tunnel_nic_device_sync(dev, utn);
	if (may_sleep || utn->need_replay) {
		queue_work(udp_tunnel_nic_workqueue, &utn->work);
		utn->work_pending = 1;
	}
}

static bool
udp_tunnel_nic_table_is_capable(const struct udp_tunnel_nic_table_info *table,
				struct udp_tunnel_info *ti)
{
	return table->tunnel_types & ti->type;
}

static bool
udp_tunnel_nic_is_capable(struct net_device *dev, struct udp_tunnel_nic *utn,
			  struct udp_tunnel_info *ti)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	unsigned int i;

	/* Special case IPv4-only NICs */
	if (info->flags & UDP_TUNNEL_NIC_INFO_IPV4_ONLY &&
	    ti->sa_family != AF_INET)
		return false;

	for (i = 0; i < utn->n_tables; i++)
		if (udp_tunnel_nic_table_is_capable(&info->tables[i], ti))
			return true;
	return false;
}

static int
udp_tunnel_nic_has_collision(struct net_device *dev, struct udp_tunnel_nic *utn,
			     struct udp_tunnel_info *ti)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic_table_entry *entry;
	unsigned int i, j;

	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++) {
			entry =	&utn->entries[i][j];

			if (!udp_tunnel_nic_entry_is_free(entry) &&
			    entry->port == ti->port &&
			    entry->type != ti->type) {
				__set_bit(i, &utn->missed);
				return true;
			}
		}
	return false;
}

static void
udp_tunnel_nic_entry_adj(struct udp_tunnel_nic *utn,
			 unsigned int table, unsigned int idx, int use_cnt_adj)
{
	struct udp_tunnel_nic_table_entry *entry =  &utn->entries[table][idx];
	bool dodgy = entry->flags & UDP_TUNNEL_NIC_ENTRY_OP_FAIL;
	unsigned int from, to;

	WARN_ON(entry->use_cnt + (u32)use_cnt_adj > U16_MAX);

	/* If not going from used to unused or vice versa - all done.
	 * For dodgy entries make sure we try to sync again (queue the entry).
	 */
	entry->use_cnt += use_cnt_adj;
	if (!dodgy && !entry->use_cnt == !(entry->use_cnt - use_cnt_adj))
		return;

	/* Cancel the op before it was sent to the device, if possible,
	 * otherwise we'd need to take special care to issue commands
	 * in the same order the ports arrived.
	 */
	if (use_cnt_adj < 0) {
		from = UDP_TUNNEL_NIC_ENTRY_ADD;
		to = UDP_TUNNEL_NIC_ENTRY_DEL;
	} else {
		from = UDP_TUNNEL_NIC_ENTRY_DEL;
		to = UDP_TUNNEL_NIC_ENTRY_ADD;
	}

	if (entry->flags & from) {
		entry->flags &= ~from;
		if (!dodgy)
			return;
	}

	udp_tunnel_nic_entry_queue(utn, entry, to);
}

static bool
udp_tunnel_nic_entry_try_adj(struct udp_tunnel_nic *utn,
			     unsigned int table, unsigned int idx,
			     struct udp_tunnel_info *ti, int use_cnt_adj)
{
	struct udp_tunnel_nic_table_entry *entry =  &utn->entries[table][idx];

	if (udp_tunnel_nic_entry_is_free(entry) ||
	    entry->port != ti->port ||
	    entry->type != ti->type)
		return false;

	if (udp_tunnel_nic_entry_is_frozen(entry))
		return true;

	udp_tunnel_nic_entry_adj(utn, table, idx, use_cnt_adj);
	return true;
}

/* Try to find existing matching entry and adjust its use count, instead of
 * adding a new one. Returns true if entry was found. In case of delete the
 * entry may have gotten removed in the process, in which case it will be
 * queued for removal.
 */
static bool
udp_tunnel_nic_try_existing(struct net_device *dev, struct udp_tunnel_nic *utn,
			    struct udp_tunnel_info *ti, int use_cnt_adj)
{
	const struct udp_tunnel_nic_table_info *table;
	unsigned int i, j;

	for (i = 0; i < utn->n_tables; i++) {
		table = &dev->udp_tunnel_nic_info->tables[i];
		if (!udp_tunnel_nic_table_is_capable(table, ti))
			continue;

		for (j = 0; j < table->n_entries; j++)
			if (udp_tunnel_nic_entry_try_adj(utn, i, j, ti,
							 use_cnt_adj))
				return true;
	}

	return false;
}

static bool
udp_tunnel_nic_add_existing(struct net_device *dev, struct udp_tunnel_nic *utn,
			    struct udp_tunnel_info *ti)
{
	return udp_tunnel_nic_try_existing(dev, utn, ti, +1);
}

static bool
udp_tunnel_nic_del_existing(struct net_device *dev, struct udp_tunnel_nic *utn,
			    struct udp_tunnel_info *ti)
{
	return udp_tunnel_nic_try_existing(dev, utn, ti, -1);
}

static bool
udp_tunnel_nic_add_new(struct net_device *dev, struct udp_tunnel_nic *utn,
		       struct udp_tunnel_info *ti)
{
	const struct udp_tunnel_nic_table_info *table;
	unsigned int i, j;

	for (i = 0; i < utn->n_tables; i++) {
		table = &dev->udp_tunnel_nic_info->tables[i];
		if (!udp_tunnel_nic_table_is_capable(table, ti))
			continue;

		for (j = 0; j < table->n_entries; j++) {
			struct udp_tunnel_nic_table_entry *entry;

			entry = &utn->entries[i][j];
			if (!udp_tunnel_nic_entry_is_free(entry))
				continue;

			entry->port = ti->port;
			entry->type = ti->type;
			entry->use_cnt = 1;
			udp_tunnel_nic_entry_queue(utn, entry,
						   UDP_TUNNEL_NIC_ENTRY_ADD);
			return true;
		}

		/* The different table may still fit this port in, but there
		 * are no devices currently which have multiple tables accepting
		 * the same tunnel type, and false positives are okay.
		 */
		__set_bit(i, &utn->missed);
	}

	return false;
}

static void
__udp_tunnel_nic_add_port(struct net_device *dev, struct udp_tunnel_info *ti)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic *utn;

	utn = dev->udp_tunnel_nic;
	if (!utn)
		return;
	if (!netif_running(dev) && info->flags & UDP_TUNNEL_NIC_INFO_OPEN_ONLY)
		return;
	if (info->flags & UDP_TUNNEL_NIC_INFO_STATIC_IANA_VXLAN &&
	    ti->port == htons(IANA_VXLAN_UDP_PORT)) {
		if (ti->type != UDP_TUNNEL_TYPE_VXLAN)
			netdev_warn(dev, "device assumes port 4789 will be used by vxlan tunnels\n");
		return;
	}

	if (!udp_tunnel_nic_is_capable(dev, utn, ti))
		return;

	/* It may happen that a tunnel of one type is removed and different
	 * tunnel type tries to reuse its port before the device was informed.
	 * Rely on utn->missed to re-add this port later.
	 */
	if (udp_tunnel_nic_has_collision(dev, utn, ti))
		return;

	if (!udp_tunnel_nic_add_existing(dev, utn, ti))
		udp_tunnel_nic_add_new(dev, utn, ti);

	udp_tunnel_nic_device_sync(dev, utn);
}

static void
__udp_tunnel_nic_del_port(struct net_device *dev, struct udp_tunnel_info *ti)
{
	struct udp_tunnel_nic *utn;

	utn = dev->udp_tunnel_nic;
	if (!utn)
		return;

	if (!udp_tunnel_nic_is_capable(dev, utn, ti))
		return;

	udp_tunnel_nic_del_existing(dev, utn, ti);

	udp_tunnel_nic_device_sync(dev, utn);
}

static void __udp_tunnel_nic_reset_ntf(struct net_device *dev)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic *utn;
	unsigned int i, j;

	ASSERT_RTNL();

	utn = dev->udp_tunnel_nic;
	if (!utn)
		return;

	utn->need_sync = false;
	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++) {
			struct udp_tunnel_nic_table_entry *entry;

			entry = &utn->entries[i][j];

			entry->flags &= ~(UDP_TUNNEL_NIC_ENTRY_DEL |
					  UDP_TUNNEL_NIC_ENTRY_OP_FAIL);
			/* We don't release rtnl across ops */
			WARN_ON(entry->flags & UDP_TUNNEL_NIC_ENTRY_FROZEN);
			if (!entry->use_cnt)
				continue;

			udp_tunnel_nic_entry_queue(utn, entry,
						   UDP_TUNNEL_NIC_ENTRY_ADD);
		}

	__udp_tunnel_nic_device_sync(dev, utn);
}

static size_t
__udp_tunnel_nic_dump_size(struct net_device *dev, unsigned int table)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic *utn;
	unsigned int j;
	size_t size;

	utn = dev->udp_tunnel_nic;
	if (!utn)
		return 0;

	size = 0;
	for (j = 0; j < info->tables[table].n_entries; j++) {
		if (!udp_tunnel_nic_entry_is_present(&utn->entries[table][j]))
			continue;

		size += nla_total_size(0) +		 /* _TABLE_ENTRY */
			nla_total_size(sizeof(__be16)) + /* _ENTRY_PORT */
			nla_total_size(sizeof(u32));	 /* _ENTRY_TYPE */
	}

	return size;
}

static int
__udp_tunnel_nic_dump_write(struct net_device *dev, unsigned int table,
			    struct sk_buff *skb)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic *utn;
	struct nlattr *nest;
	unsigned int j;

	utn = dev->udp_tunnel_nic;
	if (!utn)
		return 0;

	for (j = 0; j < info->tables[table].n_entries; j++) {
		if (!udp_tunnel_nic_entry_is_present(&utn->entries[table][j]))
			continue;

		nest = nla_nest_start(skb, ETHTOOL_A_TUNNEL_UDP_TABLE_ENTRY);
		if (!nest)
			return -EMSGSIZE;

		if (nla_put_be16(skb, ETHTOOL_A_TUNNEL_UDP_ENTRY_PORT,
				 utn->entries[table][j].port) ||
		    nla_put_u32(skb, ETHTOOL_A_TUNNEL_UDP_ENTRY_TYPE,
				ilog2(utn->entries[table][j].type)))
			goto err_cancel;

		nla_nest_end(skb, nest);
	}

	return 0;

err_cancel:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static const struct udp_tunnel_nic_ops __udp_tunnel_nic_ops = {
	.get_port	= __udp_tunnel_nic_get_port,
	.set_port_priv	= __udp_tunnel_nic_set_port_priv,
	.add_port	= __udp_tunnel_nic_add_port,
	.del_port	= __udp_tunnel_nic_del_port,
	.reset_ntf	= __udp_tunnel_nic_reset_ntf,
	.dump_size	= __udp_tunnel_nic_dump_size,
	.dump_write	= __udp_tunnel_nic_dump_write,
};

static void
udp_tunnel_nic_flush(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	unsigned int i, j;

	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++) {
			int adj_cnt = -utn->entries[i][j].use_cnt;

			if (adj_cnt)
				udp_tunnel_nic_entry_adj(utn, i, j, adj_cnt);
		}

	__udp_tunnel_nic_device_sync(dev, utn);

	for (i = 0; i < utn->n_tables; i++)
		memset(utn->entries[i], 0, array_size(info->tables[i].n_entries,
						      sizeof(**utn->entries)));
	WARN_ON(utn->need_sync);
	utn->need_replay = 0;
}

static void
udp_tunnel_nic_replay(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic_shared_node *node;
	unsigned int i, j;

	/* Freeze all the ports we are already tracking so that the replay
	 * does not double up the refcount.
	 */
	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++)
			udp_tunnel_nic_entry_freeze_used(&utn->entries[i][j]);
	utn->missed = 0;
	utn->need_replay = 0;

	if (!info->shared) {
		udp_tunnel_get_rx_info(dev);
	} else {
		list_for_each_entry(node, &info->shared->devices, list)
			udp_tunnel_get_rx_info(node->dev);
	}

	for (i = 0; i < utn->n_tables; i++)
		for (j = 0; j < info->tables[i].n_entries; j++)
			udp_tunnel_nic_entry_unfreeze(&utn->entries[i][j]);
}

static void udp_tunnel_nic_device_sync_work(struct work_struct *work)
{
	struct udp_tunnel_nic *utn =
		container_of(work, struct udp_tunnel_nic, work);

	rtnl_lock();
	utn->work_pending = 0;
	__udp_tunnel_nic_device_sync(utn->dev, utn);

	if (utn->need_replay)
		udp_tunnel_nic_replay(utn->dev, utn);
	rtnl_unlock();
}

static struct udp_tunnel_nic *
udp_tunnel_nic_alloc(const struct udp_tunnel_nic_info *info,
		     unsigned int n_tables)
{
	struct udp_tunnel_nic *utn;
	unsigned int i;

	utn = kzalloc(sizeof(*utn), GFP_KERNEL);
	if (!utn)
		return NULL;
	utn->n_tables = n_tables;
	INIT_WORK(&utn->work, udp_tunnel_nic_device_sync_work);

	utn->entries = kmalloc_array(n_tables, sizeof(void *), GFP_KERNEL);
	if (!utn->entries)
		goto err_free_utn;

	for (i = 0; i < n_tables; i++) {
		utn->entries[i] = kcalloc(info->tables[i].n_entries,
					  sizeof(*utn->entries[i]), GFP_KERNEL);
		if (!utn->entries[i])
			goto err_free_prev_entries;
	}

	return utn;

err_free_prev_entries:
	while (i--)
		kfree(utn->entries[i]);
	kfree(utn->entries);
err_free_utn:
	kfree(utn);
	return NULL;
}

static void udp_tunnel_nic_free(struct udp_tunnel_nic *utn)
{
	unsigned int i;

	for (i = 0; i < utn->n_tables; i++)
		kfree(utn->entries[i]);
	kfree(utn->entries);
	kfree(utn);
}

static int udp_tunnel_nic_register(struct net_device *dev)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;
	struct udp_tunnel_nic_shared_node *node = NULL;
	struct udp_tunnel_nic *utn;
	unsigned int n_tables, i;

	BUILD_BUG_ON(sizeof(utn->missed) * BITS_PER_BYTE <
		     UDP_TUNNEL_NIC_MAX_TABLES);
	/* Expect use count of at most 2 (IPv4, IPv6) per device */
	BUILD_BUG_ON(UDP_TUNNEL_NIC_USE_CNT_MAX <
		     UDP_TUNNEL_NIC_MAX_SHARING_DEVICES * 2);

	/* Check that the driver info is sane */
	if (WARN_ON(!info->set_port != !info->unset_port) ||
	    WARN_ON(!info->set_port == !info->sync_table) ||
	    WARN_ON(!info->tables[0].n_entries))
		return -EINVAL;

	if (WARN_ON(info->shared &&
		    info->flags & UDP_TUNNEL_NIC_INFO_OPEN_ONLY))
		return -EINVAL;

	n_tables = 1;
	for (i = 1; i < UDP_TUNNEL_NIC_MAX_TABLES; i++) {
		if (!info->tables[i].n_entries)
			continue;

		n_tables++;
		if (WARN_ON(!info->tables[i - 1].n_entries))
			return -EINVAL;
	}

	/* Create UDP tunnel state structures */
	if (info->shared) {
		node = kzalloc(sizeof(*node), GFP_KERNEL);
		if (!node)
			return -ENOMEM;

		node->dev = dev;
	}

	if (info->shared && info->shared->udp_tunnel_nic_info) {
		utn = info->shared->udp_tunnel_nic_info;
	} else {
		utn = udp_tunnel_nic_alloc(info, n_tables);
		if (!utn) {
			kfree(node);
			return -ENOMEM;
		}
	}

	if (info->shared) {
		if (!info->shared->udp_tunnel_nic_info) {
			INIT_LIST_HEAD(&info->shared->devices);
			info->shared->udp_tunnel_nic_info = utn;
		}

		list_add_tail(&node->list, &info->shared->devices);
	}

	utn->dev = dev;
	dev_hold(dev);
	dev->udp_tunnel_nic = utn;

	if (!(info->flags & UDP_TUNNEL_NIC_INFO_OPEN_ONLY))
		udp_tunnel_get_rx_info(dev);

	return 0;
}

static void
udp_tunnel_nic_unregister(struct net_device *dev, struct udp_tunnel_nic *utn)
{
	const struct udp_tunnel_nic_info *info = dev->udp_tunnel_nic_info;

	/* For a shared table remove this dev from the list of sharing devices
	 * and if there are other devices just detach.
	 */
	if (info->shared) {
		struct udp_tunnel_nic_shared_node *node, *first;

		list_for_each_entry(node, &info->shared->devices, list)
			if (node->dev == dev)
				break;
		if (list_entry_is_head(node, &info->shared->devices, list))
			return;

		list_del(&node->list);
		kfree(node);

		first = list_first_entry_or_null(&info->shared->devices,
						 typeof(*first), list);
		if (first) {
			udp_tunnel_drop_rx_info(dev);
			utn->dev = first->dev;
			goto release_dev;
		}

		info->shared->udp_tunnel_nic_info = NULL;
	}

	/* Flush before we check work, so we don't waste time adding entries
	 * from the work which we will boot immediately.
	 */
	udp_tunnel_nic_flush(dev, utn);

	/* Wait for the work to be done using the state, netdev core will
	 * retry unregister until we give up our reference on this device.
	 */
	if (utn->work_pending)
		return;

	udp_tunnel_nic_free(utn);
release_dev:
	dev->udp_tunnel_nic = NULL;
	dev_put(dev);
}

static int
udp_tunnel_nic_netdevice_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	const struct udp_tunnel_nic_info *info;
	struct udp_tunnel_nic *utn;

	info = dev->udp_tunnel_nic_info;
	if (!info)
		return NOTIFY_DONE;

	if (event == NETDEV_REGISTER) {
		int err;

		err = udp_tunnel_nic_register(dev);
		if (err)
			netdev_WARN(dev, "failed to register for UDP tunnel offloads: %d", err);
		return notifier_from_errno(err);
	}
	/* All other events will need the udp_tunnel_nic state */
	utn = dev->udp_tunnel_nic;
	if (!utn)
		return NOTIFY_DONE;

	if (event == NETDEV_UNREGISTER) {
		udp_tunnel_nic_unregister(dev, utn);
		return NOTIFY_OK;
	}

	/* All other events only matter if NIC has to be programmed open */
	if (!(info->flags & UDP_TUNNEL_NIC_INFO_OPEN_ONLY))
		return NOTIFY_DONE;

	if (event == NETDEV_UP) {
		WARN_ON(!udp_tunnel_nic_is_empty(dev, utn));
		udp_tunnel_get_rx_info(dev);
		return NOTIFY_OK;
	}
	if (event == NETDEV_GOING_DOWN) {
		udp_tunnel_nic_flush(dev, utn);
		return NOTIFY_OK;
	}

	return NOTIFY_DONE;
}

static struct notifier_block udp_tunnel_nic_notifier_block __read_mostly = {
	.notifier_call = udp_tunnel_nic_netdevice_event,
};

static int __init udp_tunnel_nic_init_module(void)
{
	int err;

	udp_tunnel_nic_workqueue = alloc_ordered_workqueue("udp_tunnel_nic", 0);
	if (!udp_tunnel_nic_workqueue)
		return -ENOMEM;

	rtnl_lock();
	udp_tunnel_nic_ops = &__udp_tunnel_nic_ops;
	rtnl_unlock();

	err = register_netdevice_notifier(&udp_tunnel_nic_notifier_block);
	if (err)
		goto err_unset_ops;

	return 0;

err_unset_ops:
	rtnl_lock();
	udp_tunnel_nic_ops = NULL;
	rtnl_unlock();
	destroy_workqueue(udp_tunnel_nic_workqueue);
	return err;
}
late_initcall(udp_tunnel_nic_init_module);

static void __exit udp_tunnel_nic_cleanup_module(void)
{
	unregister_netdevice_notifier(&udp_tunnel_nic_notifier_block);

	rtnl_lock();
	udp_tunnel_nic_ops = NULL;
	rtnl_unlock();

	destroy_workqueue(udp_tunnel_nic_workqueue);
}
module_exit(udp_tunnel_nic_cleanup_module);

MODULE_LICENSE("GPL");
