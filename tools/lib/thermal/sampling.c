// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <thermal.h>
#include "thermal_nl.h"

static int handle_thermal_sample(struct nl_msg *n, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(n);
	struct genlmsghdr *genlhdr = genlmsg_hdr(nlh);
	struct nlattr *attrs[THERMAL_GENL_ATTR_MAX + 1];
	struct thermal_handler_param *thp = arg;
	struct thermal_handler *th = thp->th;

	genlmsg_parse(nlh, 0, attrs, THERMAL_GENL_ATTR_MAX, NULL);

	switch (genlhdr->cmd) {

	case THERMAL_GENL_SAMPLING_TEMP:
		return th->ops->sampling.tz_temp(
			nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
			nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TEMP]), arg);
	default:
		return THERMAL_ERROR;
	}
}

thermal_error_t thermal_sampling_handle(struct thermal_handler *th, void *arg)
{
	struct thermal_handler_param thp = { .th = th, .arg = arg };

	if (!th)
		return THERMAL_ERROR;

	if (nl_cb_set(th->cb_sampling, NL_CB_VALID, NL_CB_CUSTOM,
		      handle_thermal_sample, &thp))
		return THERMAL_ERROR;

	return nl_recvmsgs(th->sk_sampling, th->cb_sampling);
}

int thermal_sampling_fd(struct thermal_handler *th)
{
	if (!th)
		return -1;

	return nl_socket_get_fd(th->sk_sampling);
}

thermal_error_t thermal_sampling_exit(struct thermal_handler *th)
{
	if (nl_unsubscribe_thermal(th->sk_sampling, th->cb_sampling,
				   THERMAL_GENL_SAMPLING_GROUP_NAME))
		return THERMAL_ERROR;

	nl_thermal_disconnect(th->sk_sampling, th->cb_sampling);

	return THERMAL_SUCCESS;
}

thermal_error_t thermal_sampling_init(struct thermal_handler *th)
{
	if (nl_thermal_connect(&th->sk_sampling, &th->cb_sampling))
		return THERMAL_ERROR;

	if (nl_subscribe_thermal(th->sk_sampling, th->cb_sampling,
				 THERMAL_GENL_SAMPLING_GROUP_NAME))
		return THERMAL_ERROR;

	return THERMAL_SUCCESS;
}
