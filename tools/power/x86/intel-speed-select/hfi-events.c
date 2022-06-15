// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Speed Select -- Read HFI events for OOB
 * Copyright (c) 2022 Intel Corporation.
 */

/*
 * This file incorporates work covered by the following copyright and
 * permission notice:

 * WPA Supplicant - driver interaction with Linux nl80211/cfg80211
 * Copyright (c) 2003-2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of
 * BSD license.
 *
 * Requires
 * libnl-genl-3-dev
 *
 * For Fedora/CenOS
 * dnf install libnl3-devel
 * For Ubuntu
 * apt install libnl-3-dev libnl-genl-3-dev
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

#include <linux/thermal.h>
#include "isst.h"

struct hfi_event_data {
	struct nl_sock *nl_handle;
	struct nl_cb *nl_cb;
};

struct hfi_event_data drv;

static int ack_handler(struct nl_msg *msg, void *arg)
{
	int *err = arg;
	*err = 0;
	return NL_STOP;
}

static int finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;
	*ret = 0;
	return NL_SKIP;
}

static int error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err,
			 void *arg)
{
	int *ret = arg;
	*ret = err->error;
	return NL_SKIP;
}

static int seq_check_handler(struct nl_msg *msg, void *arg)
{
	return NL_OK;
}

static int send_and_recv_msgs(struct hfi_event_data *drv,
			      struct nl_msg *msg,
			      int (*valid_handler)(struct nl_msg *, void *),
			      void *valid_data)
{
	struct nl_cb *cb;
	int err = -ENOMEM;

	cb = nl_cb_clone(drv->nl_cb);
	if (!cb)
		goto out;

	err = nl_send_auto_complete(drv->nl_handle, msg);
	if (err < 0)
		goto out;

	err = 1;

	nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
	nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
	nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);

	if (valid_handler)
		nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM,
			  valid_handler, valid_data);

	while (err > 0)
		nl_recvmsgs(drv->nl_handle, cb);
 out:
	nl_cb_put(cb);
	nlmsg_free(msg);
	return err;
}

struct family_data {
	const char *group;
	int id;
};

static int family_handler(struct nl_msg *msg, void *arg)
{
	struct family_data *res = arg;
	struct nlattr *tb[CTRL_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *mcgrp;
	int i;

	nla_parse(tb, CTRL_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);
	if (!tb[CTRL_ATTR_MCAST_GROUPS])
		return NL_SKIP;

	nla_for_each_nested(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS], i) {
		struct nlattr *tb2[CTRL_ATTR_MCAST_GRP_MAX + 1];
		nla_parse(tb2, CTRL_ATTR_MCAST_GRP_MAX, nla_data(mcgrp),
			  nla_len(mcgrp), NULL);
		if (!tb2[CTRL_ATTR_MCAST_GRP_NAME] ||
		    !tb2[CTRL_ATTR_MCAST_GRP_ID] ||
		    strncmp(nla_data(tb2[CTRL_ATTR_MCAST_GRP_NAME]),
				res->group,
				nla_len(tb2[CTRL_ATTR_MCAST_GRP_NAME])) != 0)
			continue;
		res->id = nla_get_u32(tb2[CTRL_ATTR_MCAST_GRP_ID]);
		break;
	};

	return 0;
}

static int nl_get_multicast_id(struct hfi_event_data *drv,
			       const char *family, const char *group)
{
	struct nl_msg *msg;
	int ret = -1;
	struct family_data res = { group, -ENOENT };

	msg = nlmsg_alloc();
	if (!msg)
		return -ENOMEM;
	genlmsg_put(msg, 0, 0, genl_ctrl_resolve(drv->nl_handle, "nlctrl"),
		    0, 0, CTRL_CMD_GETFAMILY, 0);
	NLA_PUT_STRING(msg, CTRL_ATTR_FAMILY_NAME, family);

	ret = send_and_recv_msgs(drv, msg, family_handler, &res);
	msg = NULL;
	if (ret == 0)
		ret = res.id;

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}

struct perf_cap {
	int cpu;
	int perf;
	int eff;
};

static void process_hfi_event(struct perf_cap *perf_cap)
{
	process_level_change(perf_cap->cpu);
}

static int handle_event(struct nl_msg *n, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(n);
	struct genlmsghdr *genlhdr = genlmsg_hdr(nlh);
	struct nlattr *attrs[THERMAL_GENL_ATTR_MAX + 1];
	int ret;
	struct perf_cap perf_cap = {0};

	ret = genlmsg_parse(nlh, 0, attrs, THERMAL_GENL_ATTR_MAX, NULL);

	debug_printf("Received event %d parse_rer:%d\n", genlhdr->cmd, ret);
	if (genlhdr->cmd == THERMAL_GENL_EVENT_CPU_CAPABILITY_CHANGE) {
		struct nlattr *cap;
		int j, index = 0;

		debug_printf("THERMAL_GENL_EVENT_CPU_CAPABILITY_CHANGE\n");
		nla_for_each_nested(cap, attrs[THERMAL_GENL_ATTR_CPU_CAPABILITY], j) {
			switch (index) {
			case 0:
				perf_cap.cpu = nla_get_u32(cap);
				break;
			case 1:
				perf_cap.perf = nla_get_u32(cap);
				break;
			case 2:
				perf_cap.eff = nla_get_u32(cap);
				break;
			default:
				break;
			}
			++index;
			if (index == 3) {
				index = 0;
				process_hfi_event(&perf_cap);
			}
		}
	}

	return 0;
}

static int _hfi_exit;

static int check_hf_suport(void)
{
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;

	__cpuid(6, eax, ebx, ecx, edx);
	if (eax & BIT(19))
		return 1;

	return 0;
}

int hfi_main(void)
{
	struct nl_sock *sock;
	struct nl_cb *cb;
	int err = 0;
	int mcast_id;
	int no_block = 0;

	if (!check_hf_suport()) {
		fprintf(stderr, "CPU Doesn't support HFI\n");
		return -1;
	}

	sock = nl_socket_alloc();
	if (!sock) {
		fprintf(stderr, "nl_socket_alloc failed\n");
		return -1;
	}

	if (genl_connect(sock)) {
		fprintf(stderr, "genl_connect(sk_event) failed\n");
		goto free_sock;
	}

	drv.nl_handle = sock;
	drv.nl_cb = cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (drv.nl_cb == NULL) {
		printf("Failed to allocate netlink callbacks");
		goto free_sock;
	}

	mcast_id = nl_get_multicast_id(&drv, THERMAL_GENL_FAMILY_NAME,
				   THERMAL_GENL_EVENT_GROUP_NAME);
	if (mcast_id < 0) {
		fprintf(stderr, "nl_get_multicast_id failed\n");
		goto free_sock;
	}

	if (nl_socket_add_membership(sock, mcast_id)) {
		fprintf(stderr, "nl_socket_add_membership failed");
		goto free_sock;
	}

	nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, seq_check_handler, 0);
	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, handle_event, NULL);

	if (no_block)
		nl_socket_set_nonblocking(sock);

	debug_printf("hfi is initialized\n");

	while (!_hfi_exit && !err) {
		err = nl_recvmsgs(sock, cb);
		debug_printf("nl_recv_message err:%d\n", err);
	}

	return 0;

	/* Netlink library doesn't have calls to dealloc cb or disconnect */
free_sock:
	nl_socket_free(sock);

	return -1;
}

void hfi_exit(void)
{
	_hfi_exit = 1;
}
