// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * 	cn_test.c
 * 
 * 2004+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 */

#define pr_fmt(fmt) "cn_test: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include <linux/connector.h>

static struct cb_id cn_test_id = { CN_NETLINK_USERS + 3, 0x456 };
static char cn_test_name[] = "cn_test";
static struct sock *nls;
static struct timer_list cn_test_timer;

static void cn_test_callback(struct cn_msg *msg, struct netlink_skb_parms *nsp)
{
	pr_info("%s: %lu: idx=%x, val=%x, seq=%u, ack=%u, len=%d: %s.\n",
	        __func__, jiffies, msg->id.idx, msg->id.val,
	        msg->seq, msg->ack, msg->len,
	        msg->len ? (char *)msg->data : "");
}

/*
 * Do not remove this function even if no one is using it as
 * this is an example of how to get notifications about new
 * connector user registration
 */
#if 0
static int cn_test_want_notify(void)
{
	struct cn_ctl_msg *ctl;
	struct cn_notify_req *req;
	struct cn_msg *msg = NULL;
	int size, size0;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	u32 group = 1;

	size0 = sizeof(*msg) + sizeof(*ctl) + 3 * sizeof(*req);

	size = NLMSG_SPACE(size0);

	skb = alloc_skb(size, GFP_ATOMIC);
	if (!skb) {
		pr_err("failed to allocate new skb with size=%u\n", size);
		return -ENOMEM;
	}

	nlh = nlmsg_put(skb, 0, 0x123, NLMSG_DONE, size - sizeof(*nlh), 0);
	if (!nlh) {
		kfree_skb(skb);
		return -EMSGSIZE;
	}

	msg = nlmsg_data(nlh);

	memset(msg, 0, size0);

	msg->id.idx = -1;
	msg->id.val = -1;
	msg->seq = 0x123;
	msg->ack = 0x345;
	msg->len = size0 - sizeof(*msg);

	ctl = (struct cn_ctl_msg *)(msg + 1);

	ctl->idx_notify_num = 1;
	ctl->val_notify_num = 2;
	ctl->group = group;
	ctl->len = msg->len - sizeof(*ctl);

	req = (struct cn_notify_req *)(ctl + 1);

	/*
	 * Idx.
	 */
	req->first = cn_test_id.idx;
	req->range = 10;

	/*
	 * Val 0.
	 */
	req++;
	req->first = cn_test_id.val;
	req->range = 10;

	/*
	 * Val 1.
	 */
	req++;
	req->first = cn_test_id.val + 20;
	req->range = 10;

	NETLINK_CB(skb).dst_group = ctl->group;
	//netlink_broadcast(nls, skb, 0, ctl->group, GFP_ATOMIC);
	netlink_unicast(nls, skb, 0, 0);

	pr_info("request was sent: group=0x%x\n", ctl->group);

	return 0;
}
#endif

static u32 cn_test_timer_counter;
static void cn_test_timer_func(struct timer_list *unused)
{
	struct cn_msg *m;
	char data[32];

	pr_debug("%s: timer fired\n", __func__);

	m = kzalloc(sizeof(*m) + sizeof(data), GFP_ATOMIC);
	if (m) {

		memcpy(&m->id, &cn_test_id, sizeof(m->id));
		m->seq = cn_test_timer_counter;
		m->len = sizeof(data);

		m->len =
		    scnprintf(data, sizeof(data), "counter = %u",
			      cn_test_timer_counter) + 1;

		memcpy(m + 1, data, m->len);

		cn_netlink_send(m, 0, 0, GFP_ATOMIC);
		kfree(m);
	}

	cn_test_timer_counter++;

	mod_timer(&cn_test_timer, jiffies + msecs_to_jiffies(1000));
}

static int cn_test_init(void)
{
	int err;

	err = cn_add_callback(&cn_test_id, cn_test_name, cn_test_callback);
	if (err)
		goto err_out;
	cn_test_id.val++;
	err = cn_add_callback(&cn_test_id, cn_test_name, cn_test_callback);
	if (err) {
		cn_del_callback(&cn_test_id);
		goto err_out;
	}

	timer_setup(&cn_test_timer, cn_test_timer_func, 0);
	mod_timer(&cn_test_timer, jiffies + msecs_to_jiffies(1000));

	pr_info("initialized with id={%u.%u}\n",
		cn_test_id.idx, cn_test_id.val);

	return 0;

      err_out:
	if (nls && nls->sk_socket)
		sock_release(nls->sk_socket);

	return err;
}

static void cn_test_fini(void)
{
	timer_delete_sync(&cn_test_timer);
	cn_del_callback(&cn_test_id);
	cn_test_id.val--;
	cn_del_callback(&cn_test_id);
	if (nls && nls->sk_socket)
		sock_release(nls->sk_socket);
}

module_init(cn_test_init);
module_exit(cn_test_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Connector's test module");
