/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_RPMSG_NS_H
#define _LINUX_RPMSG_NS_H

#include <linux/mod_devicetable.h>
#include <linux/rpmsg.h>
#include <linux/rpmsg/byteorder.h>
#include <linux/types.h>

/**
 * struct rpmsg_ns_msg - dynamic name service announcement message
 * @name: name of remote service that is published
 * @addr: address of remote service that is published
 * @flags: indicates whether service is created or destroyed
 *
 * This message is sent across to publish a new service, or announce
 * about its removal. When we receive these messages, an appropriate
 * rpmsg channel (i.e device) is created/destroyed. In turn, the ->probe()
 * or ->remove() handler of the appropriate rpmsg driver will be invoked
 * (if/as-soon-as one is registered).
 */
struct rpmsg_ns_msg {
	char name[RPMSG_NAME_SIZE];
	__rpmsg32 addr;
	__rpmsg32 flags;
} __packed;

/**
 * enum rpmsg_ns_flags - dynamic name service announcement flags
 *
 * @RPMSG_NS_CREATE: a new remote service was just created
 * @RPMSG_NS_DESTROY: a known remote service was just destroyed
 */
enum rpmsg_ns_flags {
	RPMSG_NS_CREATE		= 0,
	RPMSG_NS_DESTROY	= 1,
};

/* Address 53 is reserved for advertising remote services */
#define RPMSG_NS_ADDR			(53)

int rpmsg_ns_register_device(struct rpmsg_device *rpdev);

#endif
