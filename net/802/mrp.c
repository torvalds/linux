// SPDX-License-Identifier: GPL-2.0-only
/*
 *	IEEE 802.1Q Multiple Registration Protocol (MRP)
 *
 *	Copyright (c) 2012 Massachusetts Institute of Technology
 *
 *	Adapted from code in net/802/garp.c
 *	Copyright (c) 2008 Patrick McHardy <kaber@trash.net>
 */
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <net/mrp.h>
#include <linux/unaligned.h>

static unsigned int mrp_join_time __read_mostly = 200;
module_param(mrp_join_time, uint, 0644);
MODULE_PARM_DESC(mrp_join_time, "Join time in ms (default 200ms)");

static unsigned int mrp_periodic_time __read_mostly = 1000;
module_param(mrp_periodic_time, uint, 0644);
MODULE_PARM_DESC(mrp_periodic_time, "Periodic time in ms (default 1s)");

MODULE_DESCRIPTION("IEEE 802.1Q Multiple Registration Protocol (MRP)");
MODULE_LICENSE("GPL");

static const u8
mrp_applicant_state_table[MRP_APPLICANT_MAX + 1][MRP_EVENT_MAX + 1] = {
	[MRP_APPLICANT_VO] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_VP,
		[MRP_EVENT_LV]		= MRP_APPLICANT_VO,
		[MRP_EVENT_TX]		= MRP_APPLICANT_VO,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VO,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VO,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_VO,
	},
	[MRP_APPLICANT_VP] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_VP,
		[MRP_EVENT_LV]		= MRP_APPLICANT_VO,
		[MRP_EVENT_TX]		= MRP_APPLICANT_AA,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VP,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VP,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_VP,
	},
	[MRP_APPLICANT_VN] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_VN,
		[MRP_EVENT_LV]		= MRP_APPLICANT_LA,
		[MRP_EVENT_TX]		= MRP_APPLICANT_AN,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VN,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VN,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_VN,
	},
	[MRP_APPLICANT_AN] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_AN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_AN,
		[MRP_EVENT_LV]		= MRP_APPLICANT_LA,
		[MRP_EVENT_TX]		= MRP_APPLICANT_QA,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_AN,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_AN,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_AN,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AN,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AN,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VN,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VN,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VN,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_AN,
	},
	[MRP_APPLICANT_AA] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_AA,
		[MRP_EVENT_LV]		= MRP_APPLICANT_LA,
		[MRP_EVENT_TX]		= MRP_APPLICANT_QA,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_AA,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_QA,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_AA,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AA,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AA,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VP,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VP,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_AA,
	},
	[MRP_APPLICANT_QA] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_QA,
		[MRP_EVENT_LV]		= MRP_APPLICANT_LA,
		[MRP_EVENT_TX]		= MRP_APPLICANT_QA,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_QA,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_QA,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_QA,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AA,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AA,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VP,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VP,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_AA,
	},
	[MRP_APPLICANT_LA] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_AA,
		[MRP_EVENT_LV]		= MRP_APPLICANT_LA,
		[MRP_EVENT_TX]		= MRP_APPLICANT_VO,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_LA,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_LA,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_LA,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_LA,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_LA,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_LA,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_LA,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_LA,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_LA,
	},
	[MRP_APPLICANT_AO] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_AP,
		[MRP_EVENT_LV]		= MRP_APPLICANT_AO,
		[MRP_EVENT_TX]		= MRP_APPLICANT_AO,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_QO,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VO,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VO,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_AO,
	},
	[MRP_APPLICANT_QO] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_QP,
		[MRP_EVENT_LV]		= MRP_APPLICANT_QO,
		[MRP_EVENT_TX]		= MRP_APPLICANT_QO,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_QO,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_QO,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_QO,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AO,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VO,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VO,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VO,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_QO,
	},
	[MRP_APPLICANT_AP] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_AP,
		[MRP_EVENT_LV]		= MRP_APPLICANT_AO,
		[MRP_EVENT_TX]		= MRP_APPLICANT_QA,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_QP,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VP,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VP,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_AP,
	},
	[MRP_APPLICANT_QP] = {
		[MRP_EVENT_NEW]		= MRP_APPLICANT_VN,
		[MRP_EVENT_JOIN]	= MRP_APPLICANT_QP,
		[MRP_EVENT_LV]		= MRP_APPLICANT_QO,
		[MRP_EVENT_TX]		= MRP_APPLICANT_QP,
		[MRP_EVENT_R_NEW]	= MRP_APPLICANT_QP,
		[MRP_EVENT_R_JOIN_IN]	= MRP_APPLICANT_QP,
		[MRP_EVENT_R_IN]	= MRP_APPLICANT_QP,
		[MRP_EVENT_R_JOIN_MT]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_MT]	= MRP_APPLICANT_AP,
		[MRP_EVENT_R_LV]	= MRP_APPLICANT_VP,
		[MRP_EVENT_R_LA]	= MRP_APPLICANT_VP,
		[MRP_EVENT_REDECLARE]	= MRP_APPLICANT_VP,
		[MRP_EVENT_PERIODIC]	= MRP_APPLICANT_AP,
	},
};

static const u8
mrp_tx_action_table[MRP_APPLICANT_MAX + 1] = {
	[MRP_APPLICANT_VO] = MRP_TX_ACTION_S_IN_OPTIONAL,
	[MRP_APPLICANT_VP] = MRP_TX_ACTION_S_JOIN_IN,
	[MRP_APPLICANT_VN] = MRP_TX_ACTION_S_NEW,
	[MRP_APPLICANT_AN] = MRP_TX_ACTION_S_NEW,
	[MRP_APPLICANT_AA] = MRP_TX_ACTION_S_JOIN_IN,
	[MRP_APPLICANT_QA] = MRP_TX_ACTION_S_JOIN_IN_OPTIONAL,
	[MRP_APPLICANT_LA] = MRP_TX_ACTION_S_LV,
	[MRP_APPLICANT_AO] = MRP_TX_ACTION_S_IN_OPTIONAL,
	[MRP_APPLICANT_QO] = MRP_TX_ACTION_S_IN_OPTIONAL,
	[MRP_APPLICANT_AP] = MRP_TX_ACTION_S_JOIN_IN,
	[MRP_APPLICANT_QP] = MRP_TX_ACTION_S_IN_OPTIONAL,
};

static void mrp_attrvalue_inc(void *value, u8 len)
{
	u8 *v = (u8 *)value;

	/* Add 1 to the last byte. If it becomes zero,
	 * go to the previous byte and repeat.
	 */
	while (len > 0 && !++v[--len])
		;
}

static int mrp_attr_cmp(const struct mrp_attr *attr,
			 const void *value, u8 len, u8 type)
{
	if (attr->type != type)
		return attr->type - type;
	if (attr->len != len)
		return attr->len - len;
	return memcmp(attr->value, value, len);
}

static struct mrp_attr *mrp_attr_lookup(const struct mrp_applicant *app,
					const void *value, u8 len, u8 type)
{
	struct rb_node *parent = app->mad.rb_node;
	struct mrp_attr *attr;
	int d;

	while (parent) {
		attr = rb_entry(parent, struct mrp_attr, node);
		d = mrp_attr_cmp(attr, value, len, type);
		if (d > 0)
			parent = parent->rb_left;
		else if (d < 0)
			parent = parent->rb_right;
		else
			return attr;
	}
	return NULL;
}

static struct mrp_attr *mrp_attr_create(struct mrp_applicant *app,
					const void *value, u8 len, u8 type)
{
	struct rb_node *parent = NULL, **p = &app->mad.rb_node;
	struct mrp_attr *attr;
	int d;

	while (*p) {
		parent = *p;
		attr = rb_entry(parent, struct mrp_attr, node);
		d = mrp_attr_cmp(attr, value, len, type);
		if (d > 0)
			p = &parent->rb_left;
		else if (d < 0)
			p = &parent->rb_right;
		else {
			/* The attribute already exists; re-use it. */
			return attr;
		}
	}
	attr = kmalloc(sizeof(*attr) + len, GFP_ATOMIC);
	if (!attr)
		return attr;
	attr->state = MRP_APPLICANT_VO;
	attr->type  = type;
	attr->len   = len;
	memcpy(attr->value, value, len);

	rb_link_node(&attr->node, parent, p);
	rb_insert_color(&attr->node, &app->mad);
	return attr;
}

static void mrp_attr_destroy(struct mrp_applicant *app, struct mrp_attr *attr)
{
	rb_erase(&attr->node, &app->mad);
	kfree(attr);
}

static void mrp_attr_destroy_all(struct mrp_applicant *app)
{
	struct rb_node *node, *next;
	struct mrp_attr *attr;

	for (node = rb_first(&app->mad);
	     next = node ? rb_next(node) : NULL, node != NULL;
	     node = next) {
		attr = rb_entry(node, struct mrp_attr, node);
		mrp_attr_destroy(app, attr);
	}
}

static int mrp_pdu_init(struct mrp_applicant *app)
{
	struct sk_buff *skb;
	struct mrp_pdu_hdr *ph;

	skb = alloc_skb(app->dev->mtu + LL_RESERVED_SPACE(app->dev),
			GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	skb->dev = app->dev;
	skb->protocol = app->app->pkttype.type;
	skb_reserve(skb, LL_RESERVED_SPACE(app->dev));
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	ph = __skb_put(skb, sizeof(*ph));
	ph->version = app->app->version;

	app->pdu = skb;
	return 0;
}

static int mrp_pdu_append_end_mark(struct mrp_applicant *app)
{
	__be16 *endmark;

	if (skb_tailroom(app->pdu) < sizeof(*endmark))
		return -1;
	endmark = __skb_put(app->pdu, sizeof(*endmark));
	put_unaligned(MRP_END_MARK, endmark);
	return 0;
}

static void mrp_pdu_queue(struct mrp_applicant *app)
{
	if (!app->pdu)
		return;

	if (mrp_cb(app->pdu)->mh)
		mrp_pdu_append_end_mark(app);
	mrp_pdu_append_end_mark(app);

	dev_hard_header(app->pdu, app->dev, ntohs(app->app->pkttype.type),
			app->app->group_address, app->dev->dev_addr,
			app->pdu->len);

	skb_queue_tail(&app->queue, app->pdu);
	app->pdu = NULL;
}

static void mrp_queue_xmit(struct mrp_applicant *app)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&app->queue)))
		dev_queue_xmit(skb);
}

static int mrp_pdu_append_msg_hdr(struct mrp_applicant *app,
				  u8 attrtype, u8 attrlen)
{
	struct mrp_msg_hdr *mh;

	if (mrp_cb(app->pdu)->mh) {
		if (mrp_pdu_append_end_mark(app) < 0)
			return -1;
		mrp_cb(app->pdu)->mh = NULL;
		mrp_cb(app->pdu)->vah = NULL;
	}

	if (skb_tailroom(app->pdu) < sizeof(*mh))
		return -1;
	mh = __skb_put(app->pdu, sizeof(*mh));
	mh->attrtype = attrtype;
	mh->attrlen = attrlen;
	mrp_cb(app->pdu)->mh = mh;
	return 0;
}

static int mrp_pdu_append_vecattr_hdr(struct mrp_applicant *app,
				      const void *firstattrvalue, u8 attrlen)
{
	struct mrp_vecattr_hdr *vah;

	if (skb_tailroom(app->pdu) < sizeof(*vah) + attrlen)
		return -1;
	vah = __skb_put(app->pdu, sizeof(*vah) + attrlen);
	put_unaligned(0, &vah->lenflags);
	memcpy(vah->firstattrvalue, firstattrvalue, attrlen);
	mrp_cb(app->pdu)->vah = vah;
	memcpy(mrp_cb(app->pdu)->attrvalue, firstattrvalue, attrlen);
	return 0;
}

static int mrp_pdu_append_vecattr_event(struct mrp_applicant *app,
					const struct mrp_attr *attr,
					enum mrp_vecattr_event vaevent)
{
	u16 len, pos;
	u8 *vaevents;
	int err;
again:
	if (!app->pdu) {
		err = mrp_pdu_init(app);
		if (err < 0)
			return err;
	}

	/* If there is no Message header in the PDU, or the Message header is
	 * for a different attribute type, add an EndMark (if necessary) and a
	 * new Message header to the PDU.
	 */
	if (!mrp_cb(app->pdu)->mh ||
	    mrp_cb(app->pdu)->mh->attrtype != attr->type ||
	    mrp_cb(app->pdu)->mh->attrlen != attr->len) {
		if (mrp_pdu_append_msg_hdr(app, attr->type, attr->len) < 0)
			goto queue;
	}

	/* If there is no VectorAttribute header for this Message in the PDU,
	 * or this attribute's value does not sequentially follow the previous
	 * attribute's value, add a new VectorAttribute header to the PDU.
	 */
	if (!mrp_cb(app->pdu)->vah ||
	    memcmp(mrp_cb(app->pdu)->attrvalue, attr->value, attr->len)) {
		if (mrp_pdu_append_vecattr_hdr(app, attr->value, attr->len) < 0)
			goto queue;
	}

	len = be16_to_cpu(get_unaligned(&mrp_cb(app->pdu)->vah->lenflags));
	pos = len % 3;

	/* Events are packed into Vectors in the PDU, three to a byte. Add a
	 * byte to the end of the Vector if necessary.
	 */
	if (!pos) {
		if (skb_tailroom(app->pdu) < sizeof(u8))
			goto queue;
		vaevents = __skb_put(app->pdu, sizeof(u8));
	} else {
		vaevents = (u8 *)(skb_tail_pointer(app->pdu) - sizeof(u8));
	}

	switch (pos) {
	case 0:
		*vaevents = vaevent * (__MRP_VECATTR_EVENT_MAX *
				       __MRP_VECATTR_EVENT_MAX);
		break;
	case 1:
		*vaevents += vaevent * __MRP_VECATTR_EVENT_MAX;
		break;
	case 2:
		*vaevents += vaevent;
		break;
	default:
		WARN_ON(1);
	}

	/* Increment the length of the VectorAttribute in the PDU, as well as
	 * the value of the next attribute that would continue its Vector.
	 */
	put_unaligned(cpu_to_be16(++len), &mrp_cb(app->pdu)->vah->lenflags);
	mrp_attrvalue_inc(mrp_cb(app->pdu)->attrvalue, attr->len);

	return 0;

queue:
	mrp_pdu_queue(app);
	goto again;
}

static void mrp_attr_event(struct mrp_applicant *app,
			   struct mrp_attr *attr, enum mrp_event event)
{
	enum mrp_applicant_state state;

	state = mrp_applicant_state_table[attr->state][event];
	if (state == MRP_APPLICANT_INVALID) {
		WARN_ON(1);
		return;
	}

	if (event == MRP_EVENT_TX) {
		/* When appending the attribute fails, don't update its state
		 * in order to retry at the next TX event.
		 */

		switch (mrp_tx_action_table[attr->state]) {
		case MRP_TX_ACTION_NONE:
		case MRP_TX_ACTION_S_JOIN_IN_OPTIONAL:
		case MRP_TX_ACTION_S_IN_OPTIONAL:
			break;
		case MRP_TX_ACTION_S_NEW:
			if (mrp_pdu_append_vecattr_event(
				    app, attr, MRP_VECATTR_EVENT_NEW) < 0)
				return;
			break;
		case MRP_TX_ACTION_S_JOIN_IN:
			if (mrp_pdu_append_vecattr_event(
				    app, attr, MRP_VECATTR_EVENT_JOIN_IN) < 0)
				return;
			break;
		case MRP_TX_ACTION_S_LV:
			if (mrp_pdu_append_vecattr_event(
				    app, attr, MRP_VECATTR_EVENT_LV) < 0)
				return;
			/* As a pure applicant, sending a leave message
			 * implies that the attribute was unregistered and
			 * can be destroyed.
			 */
			mrp_attr_destroy(app, attr);
			return;
		default:
			WARN_ON(1);
		}
	}

	attr->state = state;
}

int mrp_request_join(const struct net_device *dev,
		     const struct mrp_application *appl,
		     const void *value, u8 len, u8 type)
{
	struct mrp_port *port = rtnl_dereference(dev->mrp_port);
	struct mrp_applicant *app = rtnl_dereference(
		port->applicants[appl->type]);
	struct mrp_attr *attr;

	if (sizeof(struct mrp_skb_cb) + len >
	    sizeof_field(struct sk_buff, cb))
		return -ENOMEM;

	spin_lock_bh(&app->lock);
	attr = mrp_attr_create(app, value, len, type);
	if (!attr) {
		spin_unlock_bh(&app->lock);
		return -ENOMEM;
	}
	mrp_attr_event(app, attr, MRP_EVENT_JOIN);
	spin_unlock_bh(&app->lock);
	return 0;
}
EXPORT_SYMBOL_GPL(mrp_request_join);

void mrp_request_leave(const struct net_device *dev,
		       const struct mrp_application *appl,
		       const void *value, u8 len, u8 type)
{
	struct mrp_port *port = rtnl_dereference(dev->mrp_port);
	struct mrp_applicant *app = rtnl_dereference(
		port->applicants[appl->type]);
	struct mrp_attr *attr;

	if (sizeof(struct mrp_skb_cb) + len >
	    sizeof_field(struct sk_buff, cb))
		return;

	spin_lock_bh(&app->lock);
	attr = mrp_attr_lookup(app, value, len, type);
	if (!attr) {
		spin_unlock_bh(&app->lock);
		return;
	}
	mrp_attr_event(app, attr, MRP_EVENT_LV);
	spin_unlock_bh(&app->lock);
}
EXPORT_SYMBOL_GPL(mrp_request_leave);

static void mrp_mad_event(struct mrp_applicant *app, enum mrp_event event)
{
	struct rb_node *node, *next;
	struct mrp_attr *attr;

	for (node = rb_first(&app->mad);
	     next = node ? rb_next(node) : NULL, node != NULL;
	     node = next) {
		attr = rb_entry(node, struct mrp_attr, node);
		mrp_attr_event(app, attr, event);
	}
}

static void mrp_join_timer_arm(struct mrp_applicant *app)
{
	unsigned long delay;

	delay = get_random_u32_below(msecs_to_jiffies(mrp_join_time));
	mod_timer(&app->join_timer, jiffies + delay);
}

static void mrp_join_timer(struct timer_list *t)
{
	struct mrp_applicant *app = from_timer(app, t, join_timer);

	spin_lock(&app->lock);
	mrp_mad_event(app, MRP_EVENT_TX);
	mrp_pdu_queue(app);
	spin_unlock(&app->lock);

	mrp_queue_xmit(app);
	spin_lock(&app->lock);
	if (likely(app->active))
		mrp_join_timer_arm(app);
	spin_unlock(&app->lock);
}

static void mrp_periodic_timer_arm(struct mrp_applicant *app)
{
	mod_timer(&app->periodic_timer,
		  jiffies + msecs_to_jiffies(mrp_periodic_time));
}

static void mrp_periodic_timer(struct timer_list *t)
{
	struct mrp_applicant *app = from_timer(app, t, periodic_timer);

	spin_lock(&app->lock);
	if (likely(app->active)) {
		mrp_mad_event(app, MRP_EVENT_PERIODIC);
		mrp_pdu_queue(app);
		mrp_periodic_timer_arm(app);
	}
	spin_unlock(&app->lock);
}

static int mrp_pdu_parse_end_mark(struct sk_buff *skb, int *offset)
{
	__be16 endmark;

	if (skb_copy_bits(skb, *offset, &endmark, sizeof(endmark)) < 0)
		return -1;
	if (endmark == MRP_END_MARK) {
		*offset += sizeof(endmark);
		return -1;
	}
	return 0;
}

static void mrp_pdu_parse_vecattr_event(struct mrp_applicant *app,
					struct sk_buff *skb,
					enum mrp_vecattr_event vaevent)
{
	struct mrp_attr *attr;
	enum mrp_event event;

	attr = mrp_attr_lookup(app, mrp_cb(skb)->attrvalue,
			       mrp_cb(skb)->mh->attrlen,
			       mrp_cb(skb)->mh->attrtype);
	if (attr == NULL)
		return;

	switch (vaevent) {
	case MRP_VECATTR_EVENT_NEW:
		event = MRP_EVENT_R_NEW;
		break;
	case MRP_VECATTR_EVENT_JOIN_IN:
		event = MRP_EVENT_R_JOIN_IN;
		break;
	case MRP_VECATTR_EVENT_IN:
		event = MRP_EVENT_R_IN;
		break;
	case MRP_VECATTR_EVENT_JOIN_MT:
		event = MRP_EVENT_R_JOIN_MT;
		break;
	case MRP_VECATTR_EVENT_MT:
		event = MRP_EVENT_R_MT;
		break;
	case MRP_VECATTR_EVENT_LV:
		event = MRP_EVENT_R_LV;
		break;
	default:
		return;
	}

	mrp_attr_event(app, attr, event);
}

static int mrp_pdu_parse_vecattr(struct mrp_applicant *app,
				 struct sk_buff *skb, int *offset)
{
	struct mrp_vecattr_hdr _vah;
	u16 valen;
	u8 vaevents, vaevent;

	mrp_cb(skb)->vah = skb_header_pointer(skb, *offset, sizeof(_vah),
					      &_vah);
	if (!mrp_cb(skb)->vah)
		return -1;
	*offset += sizeof(_vah);

	if (get_unaligned(&mrp_cb(skb)->vah->lenflags) &
	    MRP_VECATTR_HDR_FLAG_LA)
		mrp_mad_event(app, MRP_EVENT_R_LA);
	valen = be16_to_cpu(get_unaligned(&mrp_cb(skb)->vah->lenflags) &
			    MRP_VECATTR_HDR_LEN_MASK);

	/* The VectorAttribute structure in a PDU carries event information
	 * about one or more attributes having consecutive values. Only the
	 * value for the first attribute is contained in the structure. So
	 * we make a copy of that value, and then increment it each time we
	 * advance to the next event in its Vector.
	 */
	if (sizeof(struct mrp_skb_cb) + mrp_cb(skb)->mh->attrlen >
	    sizeof_field(struct sk_buff, cb))
		return -1;
	if (skb_copy_bits(skb, *offset, mrp_cb(skb)->attrvalue,
			  mrp_cb(skb)->mh->attrlen) < 0)
		return -1;
	*offset += mrp_cb(skb)->mh->attrlen;

	/* In a VectorAttribute, the Vector contains events which are packed
	 * three to a byte. We process one byte of the Vector at a time.
	 */
	while (valen > 0) {
		if (skb_copy_bits(skb, *offset, &vaevents,
				  sizeof(vaevents)) < 0)
			return -1;
		*offset += sizeof(vaevents);

		/* Extract and process the first event. */
		vaevent = vaevents / (__MRP_VECATTR_EVENT_MAX *
				      __MRP_VECATTR_EVENT_MAX);
		if (vaevent >= __MRP_VECATTR_EVENT_MAX) {
			/* The byte is malformed; stop processing. */
			return -1;
		}
		mrp_pdu_parse_vecattr_event(app, skb, vaevent);

		/* If present, extract and process the second event. */
		if (!--valen)
			break;
		mrp_attrvalue_inc(mrp_cb(skb)->attrvalue,
				  mrp_cb(skb)->mh->attrlen);
		vaevents %= (__MRP_VECATTR_EVENT_MAX *
			     __MRP_VECATTR_EVENT_MAX);
		vaevent = vaevents / __MRP_VECATTR_EVENT_MAX;
		mrp_pdu_parse_vecattr_event(app, skb, vaevent);

		/* If present, extract and process the third event. */
		if (!--valen)
			break;
		mrp_attrvalue_inc(mrp_cb(skb)->attrvalue,
				  mrp_cb(skb)->mh->attrlen);
		vaevents %= __MRP_VECATTR_EVENT_MAX;
		vaevent = vaevents;
		mrp_pdu_parse_vecattr_event(app, skb, vaevent);
	}
	return 0;
}

static int mrp_pdu_parse_msg(struct mrp_applicant *app, struct sk_buff *skb,
			     int *offset)
{
	struct mrp_msg_hdr _mh;

	mrp_cb(skb)->mh = skb_header_pointer(skb, *offset, sizeof(_mh), &_mh);
	if (!mrp_cb(skb)->mh)
		return -1;
	*offset += sizeof(_mh);

	if (mrp_cb(skb)->mh->attrtype == 0 ||
	    mrp_cb(skb)->mh->attrtype > app->app->maxattr ||
	    mrp_cb(skb)->mh->attrlen == 0)
		return -1;

	while (skb->len > *offset) {
		if (mrp_pdu_parse_end_mark(skb, offset) < 0)
			break;
		if (mrp_pdu_parse_vecattr(app, skb, offset) < 0)
			return -1;
	}
	return 0;
}

static int mrp_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt, struct net_device *orig_dev)
{
	struct mrp_application *appl = container_of(pt, struct mrp_application,
						    pkttype);
	struct mrp_port *port;
	struct mrp_applicant *app;
	struct mrp_pdu_hdr _ph;
	const struct mrp_pdu_hdr *ph;
	int offset = skb_network_offset(skb);

	/* If the interface is in promiscuous mode, drop the packet if
	 * it was unicast to another host.
	 */
	if (unlikely(skb->pkt_type == PACKET_OTHERHOST))
		goto out;
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (unlikely(!skb))
		goto out;
	port = rcu_dereference(dev->mrp_port);
	if (unlikely(!port))
		goto out;
	app = rcu_dereference(port->applicants[appl->type]);
	if (unlikely(!app))
		goto out;

	ph = skb_header_pointer(skb, offset, sizeof(_ph), &_ph);
	if (!ph)
		goto out;
	offset += sizeof(_ph);

	if (ph->version != app->app->version)
		goto out;

	spin_lock(&app->lock);
	while (skb->len > offset) {
		if (mrp_pdu_parse_end_mark(skb, &offset) < 0)
			break;
		if (mrp_pdu_parse_msg(app, skb, &offset) < 0)
			break;
	}
	spin_unlock(&app->lock);
out:
	kfree_skb(skb);
	return 0;
}

static int mrp_init_port(struct net_device *dev)
{
	struct mrp_port *port;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;
	rcu_assign_pointer(dev->mrp_port, port);
	return 0;
}

static void mrp_release_port(struct net_device *dev)
{
	struct mrp_port *port = rtnl_dereference(dev->mrp_port);
	unsigned int i;

	for (i = 0; i <= MRP_APPLICATION_MAX; i++) {
		if (rtnl_dereference(port->applicants[i]))
			return;
	}
	RCU_INIT_POINTER(dev->mrp_port, NULL);
	kfree_rcu(port, rcu);
}

int mrp_init_applicant(struct net_device *dev, struct mrp_application *appl)
{
	struct mrp_applicant *app;
	int err;

	ASSERT_RTNL();

	if (!rtnl_dereference(dev->mrp_port)) {
		err = mrp_init_port(dev);
		if (err < 0)
			goto err1;
	}

	err = -ENOMEM;
	app = kzalloc(sizeof(*app), GFP_KERNEL);
	if (!app)
		goto err2;

	err = dev_mc_add(dev, appl->group_address);
	if (err < 0)
		goto err3;

	app->dev = dev;
	app->app = appl;
	app->mad = RB_ROOT;
	app->active = true;
	spin_lock_init(&app->lock);
	skb_queue_head_init(&app->queue);
	rcu_assign_pointer(dev->mrp_port->applicants[appl->type], app);
	timer_setup(&app->join_timer, mrp_join_timer, 0);
	mrp_join_timer_arm(app);
	timer_setup(&app->periodic_timer, mrp_periodic_timer, 0);
	mrp_periodic_timer_arm(app);
	return 0;

err3:
	kfree(app);
err2:
	mrp_release_port(dev);
err1:
	return err;
}
EXPORT_SYMBOL_GPL(mrp_init_applicant);

void mrp_uninit_applicant(struct net_device *dev, struct mrp_application *appl)
{
	struct mrp_port *port = rtnl_dereference(dev->mrp_port);
	struct mrp_applicant *app = rtnl_dereference(
		port->applicants[appl->type]);

	ASSERT_RTNL();

	RCU_INIT_POINTER(port->applicants[appl->type], NULL);

	spin_lock_bh(&app->lock);
	app->active = false;
	spin_unlock_bh(&app->lock);
	/* Delete timer and generate a final TX event to flush out
	 * all pending messages before the applicant is gone.
	 */
	timer_shutdown_sync(&app->join_timer);
	timer_shutdown_sync(&app->periodic_timer);

	spin_lock_bh(&app->lock);
	mrp_mad_event(app, MRP_EVENT_TX);
	mrp_attr_destroy_all(app);
	mrp_pdu_queue(app);
	spin_unlock_bh(&app->lock);

	mrp_queue_xmit(app);

	dev_mc_del(dev, appl->group_address);
	kfree_rcu(app, rcu);
	mrp_release_port(dev);
}
EXPORT_SYMBOL_GPL(mrp_uninit_applicant);

int mrp_register_application(struct mrp_application *appl)
{
	appl->pkttype.func = mrp_rcv;
	dev_add_pack(&appl->pkttype);
	return 0;
}
EXPORT_SYMBOL_GPL(mrp_register_application);

void mrp_unregister_application(struct mrp_application *appl)
{
	dev_remove_pack(&appl->pkttype);
}
EXPORT_SYMBOL_GPL(mrp_unregister_application);
