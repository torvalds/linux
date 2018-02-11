/*
 * 	connector.h
 * 
 * 2004-2005 Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __CONNECTOR_H
#define __CONNECTOR_H


#include <linux/refcount.h>

#include <linux/list.h>
#include <linux/workqueue.h>

#include <net/sock.h>
#include <uapi/linux/connector.h>

#define CN_CBQ_NAMELEN		32

struct cn_queue_dev {
	atomic_t refcnt;
	unsigned char name[CN_CBQ_NAMELEN];

	struct list_head queue_list;
	spinlock_t queue_lock;

	struct sock *nls;
};

struct cn_callback_id {
	unsigned char name[CN_CBQ_NAMELEN];
	struct cb_id id;
};

struct cn_callback_entry {
	struct list_head callback_entry;
	refcount_t refcnt;
	struct cn_queue_dev *pdev;

	struct cn_callback_id id;
	void (*callback) (struct cn_msg *, struct netlink_skb_parms *);

	u32 seq, group;
};

struct cn_dev {
	struct cb_id id;

	u32 seq, groups;
	struct sock *nls;
	void (*input) (struct sk_buff *skb);

	struct cn_queue_dev *cbdev;
};

int cn_add_callback(struct cb_id *id, const char *name,
		    void (*callback)(struct cn_msg *, struct netlink_skb_parms *));
void cn_del_callback(struct cb_id *);
int cn_netlink_send_mult(struct cn_msg *msg, u16 len, u32 portid, u32 group, gfp_t gfp_mask);
int cn_netlink_send(struct cn_msg *msg, u32 portid, u32 group, gfp_t gfp_mask);

int cn_queue_add_callback(struct cn_queue_dev *dev, const char *name,
			  struct cb_id *id,
			  void (*callback)(struct cn_msg *, struct netlink_skb_parms *));
void cn_queue_del_callback(struct cn_queue_dev *dev, struct cb_id *id);
void cn_queue_release_callback(struct cn_callback_entry *);

struct cn_queue_dev *cn_queue_alloc_dev(const char *name, struct sock *);
void cn_queue_free_dev(struct cn_queue_dev *dev);

int cn_cb_equal(struct cb_id *, struct cb_id *);

#endif				/* __CONNECTOR_H */
