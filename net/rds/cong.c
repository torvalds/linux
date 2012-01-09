/*
 * Copyright (c) 2007 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/bitops.h>
#include <linux/export.h>

#include "rds.h"

/*
 * This file implements the receive side of the unconventional congestion
 * management in RDS.
 *
 * Messages waiting in the receive queue on the receiving socket are accounted
 * against the sockets SO_RCVBUF option value.  Only the payload bytes in the
 * message are accounted for.  If the number of bytes queued equals or exceeds
 * rcvbuf then the socket is congested.  All sends attempted to this socket's
 * address should return block or return -EWOULDBLOCK.
 *
 * Applications are expected to be reasonably tuned such that this situation
 * very rarely occurs.  An application encountering this "back-pressure" is
 * considered a bug.
 *
 * This is implemented by having each node maintain bitmaps which indicate
 * which ports on bound addresses are congested.  As the bitmap changes it is
 * sent through all the connections which terminate in the local address of the
 * bitmap which changed.
 *
 * The bitmaps are allocated as connections are brought up.  This avoids
 * allocation in the interrupt handling path which queues messages on sockets.
 * The dense bitmaps let transports send the entire bitmap on any bitmap change
 * reasonably efficiently.  This is much easier to implement than some
 * finer-grained communication of per-port congestion.  The sender does a very
 * inexpensive bit test to test if the port it's about to send to is congested
 * or not.
 */

/*
 * Interaction with poll is a tad tricky. We want all processes stuck in
 * poll to wake up and check whether a congested destination became uncongested.
 * The really sad thing is we have no idea which destinations the application
 * wants to send to - we don't even know which rds_connections are involved.
 * So until we implement a more flexible rds poll interface, we have to make
 * do with this:
 * We maintain a global counter that is incremented each time a congestion map
 * update is received. Each rds socket tracks this value, and if rds_poll
 * finds that the saved generation number is smaller than the global generation
 * number, it wakes up the process.
 */
static atomic_t		rds_cong_generation = ATOMIC_INIT(0);

/*
 * Congestion monitoring
 */
static LIST_HEAD(rds_cong_monitor);
static DEFINE_RWLOCK(rds_cong_monitor_lock);

/*
 * Yes, a global lock.  It's used so infrequently that it's worth keeping it
 * global to simplify the locking.  It's only used in the following
 * circumstances:
 *
 *  - on connection buildup to associate a conn with its maps
 *  - on map changes to inform conns of a new map to send
 *
 *  It's sadly ordered under the socket callback lock and the connection lock.
 *  Receive paths can mark ports congested from interrupt context so the
 *  lock masks interrupts.
 */
static DEFINE_SPINLOCK(rds_cong_lock);
static struct rb_root rds_cong_tree = RB_ROOT;

static struct rds_cong_map *rds_cong_tree_walk(__be32 addr,
					       struct rds_cong_map *insert)
{
	struct rb_node **p = &rds_cong_tree.rb_node;
	struct rb_node *parent = NULL;
	struct rds_cong_map *map;

	while (*p) {
		parent = *p;
		map = rb_entry(parent, struct rds_cong_map, m_rb_node);

		if (addr < map->m_addr)
			p = &(*p)->rb_left;
		else if (addr > map->m_addr)
			p = &(*p)->rb_right;
		else
			return map;
	}

	if (insert) {
		rb_link_node(&insert->m_rb_node, parent, p);
		rb_insert_color(&insert->m_rb_node, &rds_cong_tree);
	}
	return NULL;
}

/*
 * There is only ever one bitmap for any address.  Connections try and allocate
 * these bitmaps in the process getting pointers to them.  The bitmaps are only
 * ever freed as the module is removed after all connections have been freed.
 */
static struct rds_cong_map *rds_cong_from_addr(__be32 addr)
{
	struct rds_cong_map *map;
	struct rds_cong_map *ret = NULL;
	unsigned long zp;
	unsigned long i;
	unsigned long flags;

	map = kzalloc(sizeof(struct rds_cong_map), GFP_KERNEL);
	if (!map)
		return NULL;

	map->m_addr = addr;
	init_waitqueue_head(&map->m_waitq);
	INIT_LIST_HEAD(&map->m_conn_list);

	for (i = 0; i < RDS_CONG_MAP_PAGES; i++) {
		zp = get_zeroed_page(GFP_KERNEL);
		if (zp == 0)
			goto out;
		map->m_page_addrs[i] = zp;
	}

	spin_lock_irqsave(&rds_cong_lock, flags);
	ret = rds_cong_tree_walk(addr, map);
	spin_unlock_irqrestore(&rds_cong_lock, flags);

	if (!ret) {
		ret = map;
		map = NULL;
	}

out:
	if (map) {
		for (i = 0; i < RDS_CONG_MAP_PAGES && map->m_page_addrs[i]; i++)
			free_page(map->m_page_addrs[i]);
		kfree(map);
	}

	rdsdebug("map %p for addr %x\n", ret, be32_to_cpu(addr));

	return ret;
}

/*
 * Put the conn on its local map's list.  This is called when the conn is
 * really added to the hash.  It's nested under the rds_conn_lock, sadly.
 */
void rds_cong_add_conn(struct rds_connection *conn)
{
	unsigned long flags;

	rdsdebug("conn %p now on map %p\n", conn, conn->c_lcong);
	spin_lock_irqsave(&rds_cong_lock, flags);
	list_add_tail(&conn->c_map_item, &conn->c_lcong->m_conn_list);
	spin_unlock_irqrestore(&rds_cong_lock, flags);
}

void rds_cong_remove_conn(struct rds_connection *conn)
{
	unsigned long flags;

	rdsdebug("removing conn %p from map %p\n", conn, conn->c_lcong);
	spin_lock_irqsave(&rds_cong_lock, flags);
	list_del_init(&conn->c_map_item);
	spin_unlock_irqrestore(&rds_cong_lock, flags);
}

int rds_cong_get_maps(struct rds_connection *conn)
{
	conn->c_lcong = rds_cong_from_addr(conn->c_laddr);
	conn->c_fcong = rds_cong_from_addr(conn->c_faddr);

	if (!(conn->c_lcong && conn->c_fcong))
		return -ENOMEM;

	return 0;
}

void rds_cong_queue_updates(struct rds_cong_map *map)
{
	struct rds_connection *conn;
	unsigned long flags;

	spin_lock_irqsave(&rds_cong_lock, flags);

	list_for_each_entry(conn, &map->m_conn_list, c_map_item) {
		if (!test_and_set_bit(0, &conn->c_map_queued)) {
			rds_stats_inc(s_cong_update_queued);
			rds_send_xmit(conn);
		}
	}

	spin_unlock_irqrestore(&rds_cong_lock, flags);
}

void rds_cong_map_updated(struct rds_cong_map *map, uint64_t portmask)
{
	rdsdebug("waking map %p for %pI4\n",
	  map, &map->m_addr);
	rds_stats_inc(s_cong_update_received);
	atomic_inc(&rds_cong_generation);
	if (waitqueue_active(&map->m_waitq))
		wake_up(&map->m_waitq);
	if (waitqueue_active(&rds_poll_waitq))
		wake_up_all(&rds_poll_waitq);

	if (portmask && !list_empty(&rds_cong_monitor)) {
		unsigned long flags;
		struct rds_sock *rs;

		read_lock_irqsave(&rds_cong_monitor_lock, flags);
		list_for_each_entry(rs, &rds_cong_monitor, rs_cong_list) {
			spin_lock(&rs->rs_lock);
			rs->rs_cong_notify |= (rs->rs_cong_mask & portmask);
			rs->rs_cong_mask &= ~portmask;
			spin_unlock(&rs->rs_lock);
			if (rs->rs_cong_notify)
				rds_wake_sk_sleep(rs);
		}
		read_unlock_irqrestore(&rds_cong_monitor_lock, flags);
	}
}
EXPORT_SYMBOL_GPL(rds_cong_map_updated);

int rds_cong_updated_since(unsigned long *recent)
{
	unsigned long gen = atomic_read(&rds_cong_generation);

	if (likely(*recent == gen))
		return 0;
	*recent = gen;
	return 1;
}

/*
 * We're called under the locking that protects the sockets receive buffer
 * consumption.  This makes it a lot easier for the caller to only call us
 * when it knows that an existing set bit needs to be cleared, and vice versa.
 * We can't block and we need to deal with concurrent sockets working against
 * the same per-address map.
 */
void rds_cong_set_bit(struct rds_cong_map *map, __be16 port)
{
	unsigned long i;
	unsigned long off;

	rdsdebug("setting congestion for %pI4:%u in map %p\n",
	  &map->m_addr, ntohs(port), map);

	i = be16_to_cpu(port) / RDS_CONG_MAP_PAGE_BITS;
	off = be16_to_cpu(port) % RDS_CONG_MAP_PAGE_BITS;

	__set_bit_le(off, (void *)map->m_page_addrs[i]);
}

void rds_cong_clear_bit(struct rds_cong_map *map, __be16 port)
{
	unsigned long i;
	unsigned long off;

	rdsdebug("clearing congestion for %pI4:%u in map %p\n",
	  &map->m_addr, ntohs(port), map);

	i = be16_to_cpu(port) / RDS_CONG_MAP_PAGE_BITS;
	off = be16_to_cpu(port) % RDS_CONG_MAP_PAGE_BITS;

	__clear_bit_le(off, (void *)map->m_page_addrs[i]);
}

static int rds_cong_test_bit(struct rds_cong_map *map, __be16 port)
{
	unsigned long i;
	unsigned long off;

	i = be16_to_cpu(port) / RDS_CONG_MAP_PAGE_BITS;
	off = be16_to_cpu(port) % RDS_CONG_MAP_PAGE_BITS;

	return test_bit_le(off, (void *)map->m_page_addrs[i]);
}

void rds_cong_add_socket(struct rds_sock *rs)
{
	unsigned long flags;

	write_lock_irqsave(&rds_cong_monitor_lock, flags);
	if (list_empty(&rs->rs_cong_list))
		list_add(&rs->rs_cong_list, &rds_cong_monitor);
	write_unlock_irqrestore(&rds_cong_monitor_lock, flags);
}

void rds_cong_remove_socket(struct rds_sock *rs)
{
	unsigned long flags;
	struct rds_cong_map *map;

	write_lock_irqsave(&rds_cong_monitor_lock, flags);
	list_del_init(&rs->rs_cong_list);
	write_unlock_irqrestore(&rds_cong_monitor_lock, flags);

	/* update congestion map for now-closed port */
	spin_lock_irqsave(&rds_cong_lock, flags);
	map = rds_cong_tree_walk(rs->rs_bound_addr, NULL);
	spin_unlock_irqrestore(&rds_cong_lock, flags);

	if (map && rds_cong_test_bit(map, rs->rs_bound_port)) {
		rds_cong_clear_bit(map, rs->rs_bound_port);
		rds_cong_queue_updates(map);
	}
}

int rds_cong_wait(struct rds_cong_map *map, __be16 port, int nonblock,
		  struct rds_sock *rs)
{
	if (!rds_cong_test_bit(map, port))
		return 0;
	if (nonblock) {
		if (rs && rs->rs_cong_monitor) {
			unsigned long flags;

			/* It would have been nice to have an atomic set_bit on
			 * a uint64_t. */
			spin_lock_irqsave(&rs->rs_lock, flags);
			rs->rs_cong_mask |= RDS_CONG_MONITOR_MASK(ntohs(port));
			spin_unlock_irqrestore(&rs->rs_lock, flags);

			/* Test again - a congestion update may have arrived in
			 * the meantime. */
			if (!rds_cong_test_bit(map, port))
				return 0;
		}
		rds_stats_inc(s_cong_send_error);
		return -ENOBUFS;
	}

	rds_stats_inc(s_cong_send_blocked);
	rdsdebug("waiting on map %p for port %u\n", map, be16_to_cpu(port));

	return wait_event_interruptible(map->m_waitq,
					!rds_cong_test_bit(map, port));
}

void rds_cong_exit(void)
{
	struct rb_node *node;
	struct rds_cong_map *map;
	unsigned long i;

	while ((node = rb_first(&rds_cong_tree))) {
		map = rb_entry(node, struct rds_cong_map, m_rb_node);
		rdsdebug("freeing map %p\n", map);
		rb_erase(&map->m_rb_node, &rds_cong_tree);
		for (i = 0; i < RDS_CONG_MAP_PAGES && map->m_page_addrs[i]; i++)
			free_page(map->m_page_addrs[i]);
		kfree(map);
	}
}

/*
 * Allocate a RDS message containing a congestion update.
 */
struct rds_message *rds_cong_update_alloc(struct rds_connection *conn)
{
	struct rds_cong_map *map = conn->c_lcong;
	struct rds_message *rm;

	rm = rds_message_map_pages(map->m_page_addrs, RDS_CONG_MAP_BYTES);
	if (!IS_ERR(rm))
		rm->m_inc.i_hdr.h_flags = RDS_FLAG_CONG_BITMAP;

	return rm;
}
