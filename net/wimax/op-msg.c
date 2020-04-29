// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux WiMAX
 * Generic messaging interface between userspace and driver/device
 *
 * Copyright (C) 2007-2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This implements a direct communication channel between user space and
 * the driver/device, by which free form messages can be sent back and
 * forth.
 *
 * This is intended for device-specific features, vendor quirks, etc.
 *
 * See include/net/wimax.h
 *
 * GENERIC NETLINK ENCODING AND CAPACITY
 *
 * A destination "pipe name" is added to each message; it is up to the
 * drivers to assign or use those names (if using them at all).
 *
 * Messages are encoded as a binary netlink attribute using nla_put()
 * using type NLA_UNSPEC (as some versions of libnl still in
 * deployment don't yet understand NLA_BINARY).
 *
 * The maximum capacity of this transport is PAGESIZE per message (so
 * the actual payload will be bit smaller depending on the
 * netlink/generic netlink attributes and headers).
 *
 * RECEPTION OF MESSAGES
 *
 * When a message is received from user space, it is passed verbatim
 * to the driver calling wimax_dev->op_msg_from_user(). The return
 * value from this function is passed back to user space as an ack
 * over the generic netlink protocol.
 *
 * The stack doesn't do any processing or interpretation of these
 * messages.
 *
 * SENDING MESSAGES
 *
 * Messages can be sent with wimax_msg().
 *
 * If the message delivery needs to happen on a different context to
 * that of its creation, wimax_msg_alloc() can be used to get a
 * pointer to the message that can be delivered later on with
 * wimax_msg_send().
 *
 * ROADMAP
 *
 * wimax_gnl_doit_msg_from_user()    Process a message from user space
 *   wimax_dev_get_by_genl_info()
 *   wimax_dev->op_msg_from_user()   Delivery of message to the driver
 *
 * wimax_msg()                       Send a message to user space
 *   wimax_msg_alloc()
 *   wimax_msg_send()
 */
#include <linux/device.h>
#include <linux/slab.h>
#include <net/genetlink.h>
#include <linux/netdevice.h>
#include <linux/wimax.h>
#include <linux/security.h>
#include <linux/export.h>
#include "wimax-internal.h"


#define D_SUBMODULE op_msg
#include "debug-levels.h"


/**
 * wimax_msg_alloc - Create a new skb for sending a message to userspace
 *
 * @wimax_dev: WiMAX device descriptor
 * @pipe_name: "named pipe" the message will be sent to
 * @msg: pointer to the message data to send
 * @size: size of the message to send (in bytes), including the header.
 * @gfp_flags: flags for memory allocation.
 *
 * Returns: %0 if ok, negative errno code on error
 *
 * Description:
 *
 * Allocates an skb that will contain the message to send to user
 * space over the messaging pipe and initializes it, copying the
 * payload.
 *
 * Once this call is done, you can deliver it with
 * wimax_msg_send().
 *
 * IMPORTANT:
 *
 * Don't use skb_push()/skb_pull()/skb_reserve() on the skb, as
 * wimax_msg_send() depends on skb->data being placed at the
 * beginning of the user message.
 *
 * Unlike other WiMAX stack calls, this call can be used way early,
 * even before wimax_dev_add() is called, as long as the
 * wimax_dev->net_dev pointer is set to point to a proper
 * net_dev. This is so that drivers can use it early in case they need
 * to send stuff around or communicate with user space.
 */
struct sk_buff *wimax_msg_alloc(struct wimax_dev *wimax_dev,
				const char *pipe_name,
				const void *msg, size_t size,
				gfp_t gfp_flags)
{
	int result;
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	size_t msg_size;
	void *genl_msg;
	struct sk_buff *skb;

	msg_size = nla_total_size(size)
		+ nla_total_size(sizeof(u32))
		+ (pipe_name ? nla_total_size(strlen(pipe_name)) : 0);
	result = -ENOMEM;
	skb = genlmsg_new(msg_size, gfp_flags);
	if (skb == NULL)
		goto error_new;
	genl_msg = genlmsg_put(skb, 0, 0, &wimax_gnl_family,
			       0, WIMAX_GNL_OP_MSG_TO_USER);
	if (genl_msg == NULL) {
		dev_err(dev, "no memory to create generic netlink message\n");
		goto error_genlmsg_put;
	}
	result = nla_put_u32(skb, WIMAX_GNL_MSG_IFIDX,
			     wimax_dev->net_dev->ifindex);
	if (result < 0) {
		dev_err(dev, "no memory to add ifindex attribute\n");
		goto error_nla_put;
	}
	if (pipe_name) {
		result = nla_put_string(skb, WIMAX_GNL_MSG_PIPE_NAME,
					pipe_name);
		if (result < 0) {
			dev_err(dev, "no memory to add pipe_name attribute\n");
			goto error_nla_put;
		}
	}
	result = nla_put(skb, WIMAX_GNL_MSG_DATA, size, msg);
	if (result < 0) {
		dev_err(dev, "no memory to add payload (msg %p size %zu) in "
			"attribute: %d\n", msg, size, result);
		goto error_nla_put;
	}
	genlmsg_end(skb, genl_msg);
	return skb;

error_nla_put:
error_genlmsg_put:
error_new:
	nlmsg_free(skb);
	return ERR_PTR(result);
}
EXPORT_SYMBOL_GPL(wimax_msg_alloc);


/**
 * wimax_msg_data_len - Return a pointer and size of a message's payload
 *
 * @msg: Pointer to a message created with wimax_msg_alloc()
 * @size: Pointer to where to store the message's size
 *
 * Returns the pointer to the message data.
 */
const void *wimax_msg_data_len(struct sk_buff *msg, size_t *size)
{
	struct nlmsghdr *nlh = (void *) msg->head;
	struct nlattr *nla;

	nla = nlmsg_find_attr(nlh, sizeof(struct genlmsghdr),
			      WIMAX_GNL_MSG_DATA);
	if (nla == NULL) {
		pr_err("Cannot find attribute WIMAX_GNL_MSG_DATA\n");
		return NULL;
	}
	*size = nla_len(nla);
	return nla_data(nla);
}
EXPORT_SYMBOL_GPL(wimax_msg_data_len);


/**
 * wimax_msg_data - Return a pointer to a message's payload
 *
 * @msg: Pointer to a message created with wimax_msg_alloc()
 */
const void *wimax_msg_data(struct sk_buff *msg)
{
	struct nlmsghdr *nlh = (void *) msg->head;
	struct nlattr *nla;

	nla = nlmsg_find_attr(nlh, sizeof(struct genlmsghdr),
			      WIMAX_GNL_MSG_DATA);
	if (nla == NULL) {
		pr_err("Cannot find attribute WIMAX_GNL_MSG_DATA\n");
		return NULL;
	}
	return nla_data(nla);
}
EXPORT_SYMBOL_GPL(wimax_msg_data);


/**
 * wimax_msg_len - Return a message's payload length
 *
 * @msg: Pointer to a message created with wimax_msg_alloc()
 */
ssize_t wimax_msg_len(struct sk_buff *msg)
{
	struct nlmsghdr *nlh = (void *) msg->head;
	struct nlattr *nla;

	nla = nlmsg_find_attr(nlh, sizeof(struct genlmsghdr),
			      WIMAX_GNL_MSG_DATA);
	if (nla == NULL) {
		pr_err("Cannot find attribute WIMAX_GNL_MSG_DATA\n");
		return -EINVAL;
	}
	return nla_len(nla);
}
EXPORT_SYMBOL_GPL(wimax_msg_len);


/**
 * wimax_msg_send - Send a pre-allocated message to user space
 *
 * @wimax_dev: WiMAX device descriptor
 *
 * @skb: &struct sk_buff returned by wimax_msg_alloc(). Note the
 *     ownership of @skb is transferred to this function.
 *
 * Returns: 0 if ok, < 0 errno code on error
 *
 * Description:
 *
 * Sends a free-form message that was preallocated with
 * wimax_msg_alloc() and filled up.
 *
 * Assumes that once you pass an skb to this function for sending, it
 * owns it and will release it when done (on success).
 *
 * IMPORTANT:
 *
 * Don't use skb_push()/skb_pull()/skb_reserve() on the skb, as
 * wimax_msg_send() depends on skb->data being placed at the
 * beginning of the user message.
 *
 * Unlike other WiMAX stack calls, this call can be used way early,
 * even before wimax_dev_add() is called, as long as the
 * wimax_dev->net_dev pointer is set to point to a proper
 * net_dev. This is so that drivers can use it early in case they need
 * to send stuff around or communicate with user space.
 */
int wimax_msg_send(struct wimax_dev *wimax_dev, struct sk_buff *skb)
{
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	void *msg = skb->data;
	size_t size = skb->len;
	might_sleep();

	d_printf(1, dev, "CTX: wimax msg, %zu bytes\n", size);
	d_dump(2, dev, msg, size);
	genlmsg_multicast(&wimax_gnl_family, skb, 0, 0, GFP_KERNEL);
	d_printf(1, dev, "CTX: genl multicast done\n");
	return 0;
}
EXPORT_SYMBOL_GPL(wimax_msg_send);


/**
 * wimax_msg - Send a message to user space
 *
 * @wimax_dev: WiMAX device descriptor (properly referenced)
 * @pipe_name: "named pipe" the message will be sent to
 * @buf: pointer to the message to send.
 * @size: size of the buffer pointed to by @buf (in bytes).
 * @gfp_flags: flags for memory allocation.
 *
 * Returns: %0 if ok, negative errno code on error.
 *
 * Description:
 *
 * Sends a free-form message to user space on the device @wimax_dev.
 *
 * NOTES:
 *
 * Once the @skb is given to this function, who will own it and will
 * release it when done (unless it returns error).
 */
int wimax_msg(struct wimax_dev *wimax_dev, const char *pipe_name,
	      const void *buf, size_t size, gfp_t gfp_flags)
{
	int result = -ENOMEM;
	struct sk_buff *skb;

	skb = wimax_msg_alloc(wimax_dev, pipe_name, buf, size, gfp_flags);
	if (IS_ERR(skb))
		result = PTR_ERR(skb);
	else
		result = wimax_msg_send(wimax_dev, skb);
	return result;
}
EXPORT_SYMBOL_GPL(wimax_msg);

/*
 * Relays a message from user space to the driver
 *
 * The skb is passed to the driver-specific function with the netlink
 * and generic netlink headers already stripped.
 *
 * This call will block while handling/relaying the message.
 */
int wimax_gnl_doit_msg_from_user(struct sk_buff *skb, struct genl_info *info)
{
	int result, ifindex;
	struct wimax_dev *wimax_dev;
	struct device *dev;
	struct nlmsghdr *nlh = info->nlhdr;
	char *pipe_name;
	void *msg_buf;
	size_t msg_len;

	might_sleep();
	d_fnstart(3, NULL, "(skb %p info %p)\n", skb, info);
	result = -ENODEV;
	if (info->attrs[WIMAX_GNL_MSG_IFIDX] == NULL) {
		pr_err("WIMAX_GNL_MSG_FROM_USER: can't find IFIDX attribute\n");
		goto error_no_wimax_dev;
	}
	ifindex = nla_get_u32(info->attrs[WIMAX_GNL_MSG_IFIDX]);
	wimax_dev = wimax_dev_get_by_genl_info(info, ifindex);
	if (wimax_dev == NULL)
		goto error_no_wimax_dev;
	dev = wimax_dev_to_dev(wimax_dev);

	/* Unpack arguments */
	result = -EINVAL;
	if (info->attrs[WIMAX_GNL_MSG_DATA] == NULL) {
		dev_err(dev, "WIMAX_GNL_MSG_FROM_USER: can't find MSG_DATA "
			"attribute\n");
		goto error_no_data;
	}
	msg_buf = nla_data(info->attrs[WIMAX_GNL_MSG_DATA]);
	msg_len = nla_len(info->attrs[WIMAX_GNL_MSG_DATA]);

	if (info->attrs[WIMAX_GNL_MSG_PIPE_NAME] == NULL)
		pipe_name = NULL;
	else {
		struct nlattr *attr = info->attrs[WIMAX_GNL_MSG_PIPE_NAME];
		size_t attr_len = nla_len(attr);
		/* libnl-1.1 does not yet support NLA_NUL_STRING */
		result = -ENOMEM;
		pipe_name = kstrndup(nla_data(attr), attr_len + 1, GFP_KERNEL);
		if (pipe_name == NULL)
			goto error_alloc;
		pipe_name[attr_len] = 0;
	}
	mutex_lock(&wimax_dev->mutex);
	result = wimax_dev_is_ready(wimax_dev);
	if (result == -ENOMEDIUM)
		result = 0;
	if (result < 0)
		goto error_not_ready;
	result = -ENOSYS;
	if (wimax_dev->op_msg_from_user == NULL)
		goto error_noop;

	d_printf(1, dev,
		 "CRX: nlmsghdr len %u type %u flags 0x%04x seq 0x%x pid %u\n",
		 nlh->nlmsg_len, nlh->nlmsg_type, nlh->nlmsg_flags,
		 nlh->nlmsg_seq, nlh->nlmsg_pid);
	d_printf(1, dev, "CRX: wimax message %zu bytes\n", msg_len);
	d_dump(2, dev, msg_buf, msg_len);

	result = wimax_dev->op_msg_from_user(wimax_dev, pipe_name,
					     msg_buf, msg_len, info);
error_noop:
error_not_ready:
	mutex_unlock(&wimax_dev->mutex);
error_alloc:
	kfree(pipe_name);
error_no_data:
	dev_put(wimax_dev->net_dev);
error_no_wimax_dev:
	d_fnend(3, NULL, "(skb %p info %p) = %d\n", skb, info, result);
	return result;
}
