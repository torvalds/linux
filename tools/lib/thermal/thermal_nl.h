/* SPDX-License-Identifier: LGPL-2.1+ */
/* Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org> */
#ifndef __THERMAL_H
#define __THERMAL_H

#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/mngt.h>
#include <netlink/genl/ctrl.h>

struct thermal_handler {
	int done;
	int error;
	struct thermal_ops *ops;
	struct nl_msg *msg;
	struct nl_sock *sk_event;
	struct nl_sock *sk_sampling;
	struct nl_sock *sk_cmd;
	struct nl_cb *cb_cmd;
	struct nl_cb *cb_event;
	struct nl_cb *cb_sampling;
};

struct thermal_handler_param {
	struct thermal_handler *th;
	void *arg;
};

/*
 * Low level netlink
 */
extern int nl_subscribe_thermal(struct nl_sock *nl_sock, struct nl_cb *nl_cb,
				const char *group);

extern int nl_unsubscribe_thermal(struct nl_sock *nl_sock, struct nl_cb *nl_cb,
				  const char *group);

extern int nl_thermal_connect(struct nl_sock **nl_sock, struct nl_cb **nl_cb);

extern void nl_thermal_disconnect(struct nl_sock *nl_sock, struct nl_cb *nl_cb);

extern int nl_send_msg(struct nl_sock *sock, struct nl_cb *nl_cb, struct nl_msg *msg,
		       int (*rx_handler)(struct nl_msg *, void *),
		       void *data);

#endif /* __THERMAL_H */
