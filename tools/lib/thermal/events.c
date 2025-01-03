// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <linux/netlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#include <thermal.h>
#include "thermal_nl.h"

/*
 * Optimization: fill this array to tell which event we do want to pay
 * attention to. That happens at init time with the ops
 * structure. Each ops will enable the event and the general handler
 * will be able to discard the event if there is not ops associated
 * with it.
 */
static int enabled_ops[__THERMAL_GENL_EVENT_MAX];

static int handle_thermal_event(struct nl_msg *n, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(n);
	struct genlmsghdr *genlhdr = genlmsg_hdr(nlh);
	struct nlattr *attrs[THERMAL_GENL_ATTR_MAX + 1];
	struct thermal_handler_param *thp = arg;
	struct thermal_events_ops *ops = &thp->th->ops->events;

	genlmsg_parse(nlh, 0, attrs, THERMAL_GENL_ATTR_MAX, NULL);

	arg = thp->arg;

	/*
	 * This is an event we don't care of, bail out.
	 */
	if (!enabled_ops[genlhdr->cmd])
		return THERMAL_SUCCESS;

	switch (genlhdr->cmd) {

	case THERMAL_GENL_EVENT_TZ_CREATE:
		return ops->tz_create(nla_get_string(attrs[THERMAL_GENL_ATTR_TZ_NAME]),
				      nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]), arg);

	case THERMAL_GENL_EVENT_TZ_DELETE:
		return ops->tz_delete(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]), arg);

	case THERMAL_GENL_EVENT_TZ_ENABLE:
		return ops->tz_enable(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]), arg);

	case THERMAL_GENL_EVENT_TZ_DISABLE:
		return ops->tz_disable(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]), arg);

	case THERMAL_GENL_EVENT_TZ_TRIP_CHANGE:
		return ops->trip_change(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
					nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID]),
					nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_TYPE]),
					nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_TEMP]),
					nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_HYST]), arg);

	case THERMAL_GENL_EVENT_TZ_TRIP_ADD:
		return ops->trip_add(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_TYPE]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_TEMP]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_HYST]), arg);

	case THERMAL_GENL_EVENT_TZ_TRIP_DELETE:
		return ops->trip_delete(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
					nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID]), arg);

	case THERMAL_GENL_EVENT_TZ_TRIP_UP:
		return ops->trip_high(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
				      nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID]),
				      nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TEMP]), arg);

	case THERMAL_GENL_EVENT_TZ_TRIP_DOWN:
		return ops->trip_low(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TRIP_ID]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TEMP]), arg);

	case THERMAL_GENL_EVENT_CDEV_ADD:
		return ops->cdev_add(nla_get_string(attrs[THERMAL_GENL_ATTR_CDEV_NAME]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_CDEV_ID]),
				     nla_get_u32(attrs[THERMAL_GENL_ATTR_CDEV_MAX_STATE]), arg);

	case THERMAL_GENL_EVENT_CDEV_DELETE:
		return ops->cdev_delete(nla_get_u32(attrs[THERMAL_GENL_ATTR_CDEV_ID]), arg);

	case THERMAL_GENL_EVENT_CDEV_STATE_UPDATE:
		return ops->cdev_update(nla_get_u32(attrs[THERMAL_GENL_ATTR_CDEV_ID]),
					nla_get_u32(attrs[THERMAL_GENL_ATTR_CDEV_CUR_STATE]), arg);

	case THERMAL_GENL_EVENT_TZ_GOV_CHANGE:
		return ops->gov_change(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
				       nla_get_string(attrs[THERMAL_GENL_ATTR_GOV_NAME]), arg);

	case THERMAL_GENL_EVENT_THRESHOLD_ADD:
		return ops->threshold_add(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
					  nla_get_u32(attrs[THERMAL_GENL_ATTR_THRESHOLD_TEMP]),
					  nla_get_u32(attrs[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION]), arg);

	case THERMAL_GENL_EVENT_THRESHOLD_DELETE:
		return ops->threshold_delete(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
					     nla_get_u32(attrs[THERMAL_GENL_ATTR_THRESHOLD_TEMP]),
					     nla_get_u32(attrs[THERMAL_GENL_ATTR_THRESHOLD_DIRECTION]), arg);

	case THERMAL_GENL_EVENT_THRESHOLD_FLUSH:
		return ops->threshold_flush(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]), arg);

	case THERMAL_GENL_EVENT_THRESHOLD_UP:
		return ops->threshold_up(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
					 nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TEMP]),
					 nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_PREV_TEMP]), arg);

	case THERMAL_GENL_EVENT_THRESHOLD_DOWN:
		return ops->threshold_down(nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_ID]),
					   nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_TEMP]),
					   nla_get_u32(attrs[THERMAL_GENL_ATTR_TZ_PREV_TEMP]), arg);

	default:
		return -1;
	}
}

static void thermal_events_ops_init(struct thermal_events_ops *ops)
{
	enabled_ops[THERMAL_GENL_EVENT_TZ_CREATE]		= !!ops->tz_create;
	enabled_ops[THERMAL_GENL_EVENT_TZ_DELETE]		= !!ops->tz_delete;
	enabled_ops[THERMAL_GENL_EVENT_TZ_DISABLE]		= !!ops->tz_disable;
	enabled_ops[THERMAL_GENL_EVENT_TZ_ENABLE]		= !!ops->tz_enable;
	enabled_ops[THERMAL_GENL_EVENT_TZ_TRIP_UP]		= !!ops->trip_high;
	enabled_ops[THERMAL_GENL_EVENT_TZ_TRIP_DOWN]		= !!ops->trip_low;
	enabled_ops[THERMAL_GENL_EVENT_TZ_TRIP_CHANGE]		= !!ops->trip_change;
	enabled_ops[THERMAL_GENL_EVENT_TZ_TRIP_ADD]		= !!ops->trip_add;
	enabled_ops[THERMAL_GENL_EVENT_TZ_TRIP_DELETE]		= !!ops->trip_delete;
	enabled_ops[THERMAL_GENL_EVENT_CDEV_ADD]		= !!ops->cdev_add;
	enabled_ops[THERMAL_GENL_EVENT_CDEV_DELETE]		= !!ops->cdev_delete;
	enabled_ops[THERMAL_GENL_EVENT_CDEV_STATE_UPDATE] 	= !!ops->cdev_update;
	enabled_ops[THERMAL_GENL_EVENT_TZ_GOV_CHANGE]		= !!ops->gov_change;
	enabled_ops[THERMAL_GENL_EVENT_THRESHOLD_ADD]		= !!ops->threshold_add;
	enabled_ops[THERMAL_GENL_EVENT_THRESHOLD_DELETE]	= !!ops->threshold_delete;
	enabled_ops[THERMAL_GENL_EVENT_THRESHOLD_FLUSH]		= !!ops->threshold_flush;
	enabled_ops[THERMAL_GENL_EVENT_THRESHOLD_UP]		= !!ops->threshold_up;
	enabled_ops[THERMAL_GENL_EVENT_THRESHOLD_DOWN]		= !!ops->threshold_down;
}

thermal_error_t thermal_events_handle(struct thermal_handler *th, void *arg)
{
	struct thermal_handler_param thp = { .th = th, .arg = arg };

	if (!th)
		return THERMAL_ERROR;

	if (nl_cb_set(th->cb_event, NL_CB_VALID, NL_CB_CUSTOM,
		      handle_thermal_event, &thp))
		return THERMAL_ERROR;

	return nl_recvmsgs(th->sk_event, th->cb_event);
}

int thermal_events_fd(struct thermal_handler *th)
{
	if (!th)
		return -1;

	return nl_socket_get_fd(th->sk_event);
}

thermal_error_t thermal_events_exit(struct thermal_handler *th)
{
	if (nl_unsubscribe_thermal(th->sk_event, th->cb_event,
				   THERMAL_GENL_EVENT_GROUP_NAME))
		return THERMAL_ERROR;

	nl_thermal_disconnect(th->sk_event, th->cb_event);

	return THERMAL_SUCCESS;
}

thermal_error_t thermal_events_init(struct thermal_handler *th)
{
	thermal_events_ops_init(&th->ops->events);

	if (nl_thermal_connect(&th->sk_event, &th->cb_event))
		return THERMAL_ERROR;

	if (nl_subscribe_thermal(th->sk_event, th->cb_event,
				 THERMAL_GENL_EVENT_GROUP_NAME))
		return THERMAL_ERROR;

	return THERMAL_SUCCESS;
}
