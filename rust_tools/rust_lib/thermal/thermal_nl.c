// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <thermal.h>
#include "thermal_nl.h"

struct handler_args {
	const char *group;
	int id;
};

static __thread int err;
static __thread int done;

static int nl_seq_check_handler(struct nl_msg *msg, void *arg)
{
	return NL_OK;
}

static int nl_error_handler(struct sockaddr_nl *nla, struct nlmsgerr *nl_err,
			    void *arg)
{
	int *ret = arg;

	if (ret)
		*ret = nl_err->error;

	return NL_STOP;
}

static int nl_finish_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;

	if (ret)
		*ret = 1;

	return NL_OK;
}

static int nl_ack_handler(struct nl_msg *msg, void *arg)
{
	int *ret = arg;

	if (ret)
		*ret = 1;

	return NL_OK;
}

int nl_send_msg(struct nl_sock *sock, struct nl_cb *cb, struct nl_msg *msg,
		int (*rx_handler)(struct nl_msg *, void *), void *data)
{
	if (!rx_handler)
		return THERMAL_ERROR;

	err = nl_send_auto_complete(sock, msg);
	if (err < 0)
		return err;

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, rx_handler, data);

	err = done = 0;

	while (err == 0 && done == 0)
		nl_recvmsgs(sock, cb);

	return err;
}

static int nl_family_handler(struct nl_msg *msg, void *arg)
{
	struct handler_args *grp = arg;
	struct nlattr *tb[CTRL_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *mcgrp;
	int rem_mcgrp;

	nla_parse(tb, CTRL_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[CTRL_ATTR_MCAST_GROUPS])
		return THERMAL_ERROR;

	nla_for_each_nested(mcgrp, tb[CTRL_ATTR_MCAST_GROUPS], rem_mcgrp) {

		struct nlattr *tb_mcgrp[CTRL_ATTR_MCAST_GRP_MAX + 1];

		nla_parse(tb_mcgrp, CTRL_ATTR_MCAST_GRP_MAX,
			  nla_data(mcgrp), nla_len(mcgrp), NULL);

		if (!tb_mcgrp[CTRL_ATTR_MCAST_GRP_NAME] ||
		    !tb_mcgrp[CTRL_ATTR_MCAST_GRP_ID])
			continue;

		if (strncmp(nla_data(tb_mcgrp[CTRL_ATTR_MCAST_GRP_NAME]),
			    grp->group,
			    nla_len(tb_mcgrp[CTRL_ATTR_MCAST_GRP_NAME])))
			continue;

		grp->id = nla_get_u32(tb_mcgrp[CTRL_ATTR_MCAST_GRP_ID]);

		break;
	}

	return THERMAL_SUCCESS;
}

static int nl_get_multicast_id(struct nl_sock *sock, struct nl_cb *cb,
			       const char *family, const char *group)
{
	struct nl_msg *msg;
	int ret = 0, ctrlid;
	struct handler_args grp = {
		.group = group,
		.id = -ENOENT,
	};

	msg = nlmsg_alloc();
	if (!msg)
		return THERMAL_ERROR;

	ctrlid = genl_ctrl_resolve(sock, "nlctrl");

	genlmsg_put(msg, 0, 0, ctrlid, 0, 0, CTRL_CMD_GETFAMILY, 0);

	nla_put_string(msg, CTRL_ATTR_FAMILY_NAME, family);

	ret = nl_send_msg(sock, cb, msg, nl_family_handler, &grp);
	if (ret)
		goto nla_put_failure;

	ret = grp.id;

nla_put_failure:
	nlmsg_free(msg);
	return ret;
}

int nl_thermal_connect(struct nl_sock **nl_sock, struct nl_cb **nl_cb)
{
	struct nl_cb *cb;
	struct nl_sock *sock;

	cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb)
		return THERMAL_ERROR;

	sock = nl_socket_alloc();
	if (!sock)
		goto out_cb_free;

	if (genl_connect(sock))
		goto out_socket_free;

	if (nl_cb_err(cb, NL_CB_CUSTOM, nl_error_handler, &err) ||
	    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, nl_finish_handler, &done) ||
	    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, nl_ack_handler, &done) ||
	    nl_cb_set(cb, NL_CB_SEQ_CHECK, NL_CB_CUSTOM, nl_seq_check_handler, &done))
		return THERMAL_ERROR;

	*nl_sock = sock;
	*nl_cb = cb;

	return THERMAL_SUCCESS;

out_socket_free:
	nl_socket_free(sock);
out_cb_free:
	nl_cb_put(cb);
	return THERMAL_ERROR;
}

void nl_thermal_disconnect(struct nl_sock *nl_sock, struct nl_cb *nl_cb)
{
	nl_close(nl_sock);
	nl_socket_free(nl_sock);
	nl_cb_put(nl_cb);
}

int nl_unsubscribe_thermal(struct nl_sock *nl_sock, struct nl_cb *nl_cb,
			   const char *group)
{
	int mcid;

	mcid = nl_get_multicast_id(nl_sock, nl_cb, THERMAL_GENL_FAMILY_NAME,
				   group);
	if (mcid < 0)
		return THERMAL_ERROR;

	if (nl_socket_drop_membership(nl_sock, mcid))
		return THERMAL_ERROR;

	return THERMAL_SUCCESS;
}

int nl_subscribe_thermal(struct nl_sock *nl_sock, struct nl_cb *nl_cb,
			 const char *group)
{
	int mcid;

	mcid = nl_get_multicast_id(nl_sock, nl_cb, THERMAL_GENL_FAMILY_NAME,
				   group);
	if (mcid < 0)
		return THERMAL_ERROR;

	if (nl_socket_add_membership(nl_sock, mcid))
		return THERMAL_ERROR;

	return THERMAL_SUCCESS;
}
