/*
 *	IEEE 802.1D Generic Attribute Registration Protocol (GARP)
 *
 *	Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/llc.h>
#include <linux/slab.h>
#include <net/llc.h>
#include <net/llc_pdu.h>
#include <net/garp.h>
#include <asm/unaligned.h>

static unsigned int garp_join_time __read_mostly = 200;
module_param(garp_join_time, uint, 0644);
MODULE_PARM_DESC(garp_join_time, "Join time in ms (default 200ms)");
MODULE_LICENSE("GPL");

static const struct garp_state_trans {
	u8	state;
	u8	action;
} garp_applicant_state_table[GARP_APPLICANT_MAX + 1][GARP_EVENT_MAX + 1] = {
	[GARP_APPLICANT_VA] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_AA,
						    .action = GARP_ACTION_S_JOIN_IN },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_AA },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_LA },
	},
	[GARP_APPLICANT_AA] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_QA,
						    .action = GARP_ACTION_S_JOIN_IN },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_QA },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_LA },
	},
	[GARP_APPLICANT_QA] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_QA },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_LA },
	},
	[GARP_APPLICANT_LA] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_VO,
						    .action = GARP_ACTION_S_LEAVE_EMPTY },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_LA },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_LA },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_LA },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_VA },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_INVALID },
	},
	[GARP_APPLICANT_VP] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_AA,
						    .action = GARP_ACTION_S_JOIN_IN },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_AP },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_VO },
	},
	[GARP_APPLICANT_AP] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_QA,
						    .action = GARP_ACTION_S_JOIN_IN },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_QP },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_AO },
	},
	[GARP_APPLICANT_QP] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_QP },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_QO },
	},
	[GARP_APPLICANT_VO] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_AO },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_VP },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_INVALID },
	},
	[GARP_APPLICANT_AO] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_QO },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_AP },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_INVALID },
	},
	[GARP_APPLICANT_QO] = {
		[GARP_EVENT_TRANSMIT_PDU]	= { .state = GARP_APPLICANT_INVALID },
		[GARP_EVENT_R_JOIN_IN]		= { .state = GARP_APPLICANT_QO },
		[GARP_EVENT_R_JOIN_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_EMPTY]		= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_LEAVE_IN]		= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_R_LEAVE_EMPTY]	= { .state = GARP_APPLICANT_VO },
		[GARP_EVENT_REQ_JOIN]		= { .state = GARP_APPLICANT_QP },
		[GARP_EVENT_REQ_LEAVE]		= { .state = GARP_APPLICANT_INVALID },
	},
};

static int garp_attr_cmp(const struct garp_attr *attr,
			 const void *data, u8 len, u8 type)
{
	if (attr->type != type)
		return attr->type - type;
	if (attr->dlen != len)
		return attr->dlen - len;
	return memcmp(attr->data, data, len);
}

static struct garp_attr *garp_attr_lookup(const struct garp_applicant *app,
					  const void *data, u8 len, u8 type)
{
	struct rb_node *parent = app->gid.rb_node;
	struct garp_attr *attr;
	int d;

	while (parent) {
		attr = rb_entry(parent, struct garp_attr, node);
		d = garp_attr_cmp(attr, data, len, type);
		if (d < 0)
			parent = parent->rb_left;
		else if (d > 0)
			parent = parent->rb_right;
		else
			return attr;
	}
	return NULL;
}

static void garp_attr_insert(struct garp_applicant *app, struct garp_attr *new)
{
	struct rb_node *parent = NULL, **p = &app->gid.rb_node;
	struct garp_attr *attr;
	int d;

	while (*p) {
		parent = *p;
		attr = rb_entry(parent, struct garp_attr, node);
		d = garp_attr_cmp(attr, new->data, new->dlen, new->type);
		if (d < 0)
			p = &parent->rb_left;
		else if (d > 0)
			p = &parent->rb_right;
	}
	rb_link_node(&new->node, parent, p);
	rb_insert_color(&new->node, &app->gid);
}

static struct garp_attr *garp_attr_create(struct garp_applicant *app,
					  const void *data, u8 len, u8 type)
{
	struct garp_attr *attr;

	attr = kmalloc(sizeof(*attr) + len, GFP_ATOMIC);
	if (!attr)
		return attr;
	attr->state = GARP_APPLICANT_VO;
	attr->type  = type;
	attr->dlen  = len;
	memcpy(attr->data, data, len);
	garp_attr_insert(app, attr);
	return attr;
}

static void garp_attr_destroy(struct garp_applicant *app, struct garp_attr *attr)
{
	rb_erase(&attr->node, &app->gid);
	kfree(attr);
}

static int garp_pdu_init(struct garp_applicant *app)
{
	struct sk_buff *skb;
	struct garp_pdu_hdr *gp;

#define LLC_RESERVE	sizeof(struct llc_pdu_un)
	skb = alloc_skb(app->dev->mtu + LL_RESERVED_SPACE(app->dev),
			GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb->dev = app->dev;
	skb->protocol = htons(ETH_P_802_2);
	skb_reserve(skb, LL_RESERVED_SPACE(app->dev) + LLC_RESERVE);

	gp = (struct garp_pdu_hdr *)__skb_put(skb, sizeof(*gp));
	put_unaligned(htons(GARP_PROTOCOL_ID), &gp->protocol);

	app->pdu = skb;
	return 0;
}

static int garp_pdu_append_end_mark(struct garp_applicant *app)
{
	if (skb_tailroom(app->pdu) < sizeof(u8))
		return -1;
	*(u8 *)__skb_put(app->pdu, sizeof(u8)) = GARP_END_MARK;
	return 0;
}

static void garp_pdu_queue(struct garp_applicant *app)
{
	if (!app->pdu)
		return;

	garp_pdu_append_end_mark(app);
	garp_pdu_append_end_mark(app);

	llc_pdu_header_init(app->pdu, LLC_PDU_TYPE_U, LLC_SAP_BSPAN,
			    LLC_SAP_BSPAN, LLC_PDU_CMD);
	llc_pdu_init_as_ui_cmd(app->pdu);
	llc_mac_hdr_init(app->pdu, app->dev->dev_addr,
			 app->app->proto.group_address);

	skb_queue_tail(&app->queue, app->pdu);
	app->pdu = NULL;
}

static void garp_queue_xmit(struct garp_applicant *app)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&app->queue)))
		dev_queue_xmit(skb);
}

static int garp_pdu_append_msg(struct garp_applicant *app, u8 attrtype)
{
	struct garp_msg_hdr *gm;

	if (skb_tailroom(app->pdu) < sizeof(*gm))
		return -1;
	gm = (struct garp_msg_hdr *)__skb_put(app->pdu, sizeof(*gm));
	gm->attrtype = attrtype;
	garp_cb(app->pdu)->cur_type = attrtype;
	return 0;
}

static int garp_pdu_append_attr(struct garp_applicant *app,
				const struct garp_attr *attr,
				enum garp_attr_event event)
{
	struct garp_attr_hdr *ga;
	unsigned int len;
	int err;
again:
	if (!app->pdu) {
		err = garp_pdu_init(app);
		if (err < 0)
			return err;
	}

	if (garp_cb(app->pdu)->cur_type != attr->type) {
		if (garp_cb(app->pdu)->cur_type &&
		    garp_pdu_append_end_mark(app) < 0)
			goto queue;
		if (garp_pdu_append_msg(app, attr->type) < 0)
			goto queue;
	}

	len = sizeof(*ga) + attr->dlen;
	if (skb_tailroom(app->pdu) < len)
		goto queue;
	ga = (struct garp_attr_hdr *)__skb_put(app->pdu, len);
	ga->len   = len;
	ga->event = event;
	memcpy(ga->data, attr->data, attr->dlen);
	return 0;

queue:
	garp_pdu_queue(app);
	goto again;
}

static void garp_attr_event(struct garp_applicant *app,
			    struct garp_attr *attr, enum garp_event event)
{
	enum garp_applicant_state state;

	state = garp_applicant_state_table[attr->state][event].state;
	if (state == GARP_APPLICANT_INVALID)
		return;

	switch (garp_applicant_state_table[attr->state][event].action) {
	case GARP_ACTION_NONE:
		break;
	case GARP_ACTION_S_JOIN_IN:
		/* When appending the attribute fails, don't update state in
		 * order to retry on next TRANSMIT_PDU event. */
		if (garp_pdu_append_attr(app, attr, GARP_JOIN_IN) < 0)
			return;
		break;
	case GARP_ACTION_S_LEAVE_EMPTY:
		garp_pdu_append_attr(app, attr, GARP_LEAVE_EMPTY);
		/* As a pure applicant, sending a leave message implies that
		 * the attribute was unregistered and can be destroyed. */
		garp_attr_destroy(app, attr);
		return;
	default:
		WARN_ON(1);
	}

	attr->state = state;
}

int garp_request_join(const struct net_device *dev,
		      const struct garp_application *appl,
		      const void *data, u8 len, u8 type)
{
	struct garp_port *port = dev->garp_port;
	struct garp_applicant *app = port->applicants[appl->type];
	struct garp_attr *attr;

	spin_lock_bh(&app->lock);
	attr = garp_attr_create(app, data, len, type);
	if (!attr) {
		spin_unlock_bh(&app->lock);
		return -ENOMEM;
	}
	garp_attr_event(app, attr, GARP_EVENT_REQ_JOIN);
	spin_unlock_bh(&app->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(garp_request_join);

void garp_request_leave(const struct net_device *dev,
			const struct garp_application *appl,
			const void *data, u8 len, u8 type)
{
	struct garp_port *port = dev->garp_port;
	struct garp_applicant *app = port->applicants[appl->type];
	struct garp_attr *attr;

	spin_lock_bh(&app->lock);
	attr = garp_attr_lookup(app, data, len, type);
	if (!attr) {
		spin_unlock_bh(&app->lock);
		return;
	}
	garp_attr_event(app, attr, GARP_EVENT_REQ_LEAVE);
	spin_unlock_bh(&app->lock);
}
EXPORT_SYMBOL_GPL(garp_request_leave);

static void garp_gid_event(struct garp_applicant *app, enum garp_event event)
{
	struct rb_node *node, *next;
	struct garp_attr *attr;

	for (node = rb_first(&app->gid);
	     next = node ? rb_next(node) : NULL, node != NULL;
	     node = next) {
		attr = rb_entry(node, struct garp_attr, node);
		garp_attr_event(app, attr, event);
	}
}

static void garp_join_timer_arm(struct garp_applicant *app)
{
	unsigned long delay;

	delay = (u64)msecs_to_jiffies(garp_join_time) * net_random() >> 32;
	mod_timer(&app->join_timer, jiffies + delay);
}

static void garp_join_timer(unsigned long data)
{
	struct garp_applicant *app = (struct garp_applicant *)data;

	spin_lock(&app->lock);
	garp_gid_event(app, GARP_EVENT_TRANSMIT_PDU);
	garp_pdu_queue(app);
	spin_unlock(&app->lock);

	garp_queue_xmit(app);
	garp_join_timer_arm(app);
}

static int garp_pdu_parse_end_mark(struct sk_buff *skb)
{
	if (!pskb_may_pull(skb, sizeof(u8)))
		return -1;
	if (*skb->data == GARP_END_MARK) {
		skb_pull(skb, sizeof(u8));
		return -1;
	}
	return 0;
}

static int garp_pdu_parse_attr(struct garp_applicant *app, struct sk_buff *skb,
			       u8 attrtype)
{
	const struct garp_attr_hdr *ga;
	struct garp_attr *attr;
	enum garp_event event;
	unsigned int dlen;

	if (!pskb_may_pull(skb, sizeof(*ga)))
		return -1;
	ga = (struct garp_attr_hdr *)skb->data;
	if (ga->len < sizeof(*ga))
		return -1;

	if (!pskb_may_pull(skb, ga->len))
		return -1;
	skb_pull(skb, ga->len);
	dlen = sizeof(*ga) - ga->len;

	if (attrtype > app->app->maxattr)
		return 0;

	switch (ga->event) {
	case GARP_LEAVE_ALL:
		if (dlen != 0)
			return -1;
		garp_gid_event(app, GARP_EVENT_R_LEAVE_EMPTY);
		return 0;
	case GARP_JOIN_EMPTY:
		event = GARP_EVENT_R_JOIN_EMPTY;
		break;
	case GARP_JOIN_IN:
		event = GARP_EVENT_R_JOIN_IN;
		break;
	case GARP_LEAVE_EMPTY:
		event = GARP_EVENT_R_LEAVE_EMPTY;
		break;
	case GARP_EMPTY:
		event = GARP_EVENT_R_EMPTY;
		break;
	default:
		return 0;
	}

	if (dlen == 0)
		return -1;
	attr = garp_attr_lookup(app, ga->data, dlen, attrtype);
	if (attr == NULL)
		return 0;
	garp_attr_event(app, attr, event);
	return 0;
}

static int garp_pdu_parse_msg(struct garp_applicant *app, struct sk_buff *skb)
{
	const struct garp_msg_hdr *gm;

	if (!pskb_may_pull(skb, sizeof(*gm)))
		return -1;
	gm = (struct garp_msg_hdr *)skb->data;
	if (gm->attrtype == 0)
		return -1;
	skb_pull(skb, sizeof(*gm));

	while (skb->len > 0) {
		if (garp_pdu_parse_attr(app, skb, gm->attrtype) < 0)
			return -1;
		if (garp_pdu_parse_end_mark(skb) < 0)
			break;
	}
	return 0;
}

static void garp_pdu_rcv(const struct stp_proto *proto, struct sk_buff *skb,
			 struct net_device *dev)
{
	struct garp_application *appl = proto->data;
	struct garp_port *port;
	struct garp_applicant *app;
	const struct garp_pdu_hdr *gp;

	port = rcu_dereference(dev->garp_port);
	if (!port)
		goto err;
	app = rcu_dereference(port->applicants[appl->type]);
	if (!app)
		goto err;

	if (!pskb_may_pull(skb, sizeof(*gp)))
		goto err;
	gp = (struct garp_pdu_hdr *)skb->data;
	if (get_unaligned(&gp->protocol) != htons(GARP_PROTOCOL_ID))
		goto err;
	skb_pull(skb, sizeof(*gp));

	spin_lock(&app->lock);
	while (skb->len > 0) {
		if (garp_pdu_parse_msg(app, skb) < 0)
			break;
		if (garp_pdu_parse_end_mark(skb) < 0)
			break;
	}
	spin_unlock(&app->lock);
err:
	kfree_skb(skb);
}

static int garp_init_port(struct net_device *dev)
{
	struct garp_port *port;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;
	rcu_assign_pointer(dev->garp_port, port);
	return 0;
}

static void garp_release_port(struct net_device *dev)
{
	struct garp_port *port = dev->garp_port;
	unsigned int i;

	for (i = 0; i <= GARP_APPLICATION_MAX; i++) {
		if (port->applicants[i])
			return;
	}
	rcu_assign_pointer(dev->garp_port, NULL);
	synchronize_rcu();
	kfree(port);
}

int garp_init_applicant(struct net_device *dev, struct garp_application *appl)
{
	struct garp_applicant *app;
	int err;

	ASSERT_RTNL();

	if (!dev->garp_port) {
		err = garp_init_port(dev);
		if (err < 0)
			goto err1;
	}

	err = -ENOMEM;
	app = kzalloc(sizeof(*app), GFP_KERNEL);
	if (!app)
		goto err2;

	err = dev_mc_add(dev, appl->proto.group_address);
	if (err < 0)
		goto err3;

	app->dev = dev;
	app->app = appl;
	app->gid = RB_ROOT;
	spin_lock_init(&app->lock);
	skb_queue_head_init(&app->queue);
	rcu_assign_pointer(dev->garp_port->applicants[appl->type], app);
	setup_timer(&app->join_timer, garp_join_timer, (unsigned long)app);
	garp_join_timer_arm(app);
	return 0;

err3:
	kfree(app);
err2:
	garp_release_port(dev);
err1:
	return err;
}
EXPORT_SYMBOL_GPL(garp_init_applicant);

void garp_uninit_applicant(struct net_device *dev, struct garp_application *appl)
{
	struct garp_port *port = dev->garp_port;
	struct garp_applicant *app = port->applicants[appl->type];

	ASSERT_RTNL();

	rcu_assign_pointer(port->applicants[appl->type], NULL);
	synchronize_rcu();

	/* Delete timer and generate a final TRANSMIT_PDU event to flush out
	 * all pending messages before the applicant is gone. */
	del_timer_sync(&app->join_timer);
	garp_gid_event(app, GARP_EVENT_TRANSMIT_PDU);
	garp_pdu_queue(app);
	garp_queue_xmit(app);

	dev_mc_del(dev, appl->proto.group_address);
	kfree(app);
	garp_release_port(dev);
}
EXPORT_SYMBOL_GPL(garp_uninit_applicant);

int garp_register_application(struct garp_application *appl)
{
	appl->proto.rcv = garp_pdu_rcv;
	appl->proto.data = appl;
	return stp_proto_register(&appl->proto);
}
EXPORT_SYMBOL_GPL(garp_register_application);

void garp_unregister_application(struct garp_application *appl)
{
	stp_proto_unregister(&appl->proto);
}
EXPORT_SYMBOL_GPL(garp_unregister_application);
