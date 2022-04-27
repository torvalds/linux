// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <thermal.h>
#include "thermal_nl.h"

static struct nla_policy thermal_genl_policy[THERMAL_GENL_ATTR_MAX + 1] = {
	/* Thermal zone */
	[THERMAL_GENL_ATTR_TZ]                  = { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_TZ_ID]               = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TEMP]             = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP]             = { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_TZ_TRIP_ID]          = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP_TEMP]        = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP_TYPE]        = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_TRIP_HYST]        = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_MODE]             = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_CDEV_WEIGHT]      = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_TZ_NAME]             = { .type = NLA_STRING },

	/* Governor(s) */
	[THERMAL_GENL_ATTR_TZ_GOV]              = { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_TZ_GOV_NAME]         = { .type = NLA_STRING },

	/* Cooling devices */
	[THERMAL_GENL_ATTR_CDEV]                = { .type = NLA_NESTED },
	[THERMAL_GENL_ATTR_CDEV_ID]             = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CDEV_CUR_STATE]      = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CDEV_MAX_STATE]      = { .type = NLA_U32 },
	[THERMAL_GENL_ATTR_CDEV_NAME]           = { .type = NLA_STRING },
};

static int parse_tz_get(struct genl_info *info, struct thermal_zone **tz)
{
	struct nlattr *attr;
	struct thermal_zone *__tz = NULL;
	size_t size = 0;
	int rem;

	nla_for_each_nested(attr, info->attrs[THERMAL_GENL_ATTR_TZ], rem) {

		if (nla_type(attr) == THERMAL_GENL_ATTR_TZ_ID) {

			size++;

			__tz = realloc(__tz, sizeof(*__tz) * (size + 2));
			if (!__tz)
				return THERMAL_ERROR;

			__tz[size - 1].id = nla_get_u32(attr);
		}


		if (nla_type(attr) == THERMAL_GENL_ATTR_TZ_NAME)
			nla_strlcpy(__tz[size - 1].name, attr,
				    THERMAL_NAME_LENGTH);
	}

	if (__tz)
		__tz[size].id = -1;

	*tz = __tz;

	return THERMAL_SUCCESS;
}

static int parse_cdev_get(struct genl_info *info, struct thermal_cdev **cdev)
{
	struct nlattr *attr;
	struct thermal_cdev *__cdev = NULL;
	size_t size = 0;
	int rem;

	nla_for_each_nested(attr, info->attrs[THERMAL_GENL_ATTR_CDEV], rem) {

		if (nla_type(attr) == THERMAL_GENL_ATTR_CDEV_ID) {

			size++;

			__cdev = realloc(__cdev, sizeof(*__cdev) * (size + 2));
			if (!__cdev)
				return THERMAL_ERROR;

			__cdev[size - 1].id = nla_get_u32(attr);
		}

		if (nla_type(attr) == THERMAL_GENL_ATTR_CDEV_NAME) {
			nla_strlcpy(__cdev[size - 1].name, attr,
				    THERMAL_NAME_LENGTH);
		}

		if (nla_type(attr) == THERMAL_GENL_ATTR_CDEV_CUR_STATE)
			__cdev[size - 1].cur_state = nla_get_u32(attr);

		if (nla_type(attr) == THERMAL_GENL_ATTR_CDEV_MAX_STATE)
			__cdev[size - 1].max_state = nla_get_u32(attr);
	}

	if (__cdev)
		__cdev[size].id = -1;

	*cdev = __cdev;

	return THERMAL_SUCCESS;
}

static int parse_tz_get_trip(struct genl_info *info, struct thermal_zone *tz)
{
	struct nlattr *attr;
	struct thermal_trip *__tt = NULL;
	size_t size = 0;
	int rem;

	nla_for_each_nested(attr, info->attrs[THERMAL_GENL_ATTR_TZ_TRIP], rem) {

		if (nla_type(attr) == THERMAL_GENL_ATTR_TZ_TRIP_ID) {

			size++;

			__tt = realloc(__tt, sizeof(*__tt) * (size + 2));
			if (!__tt)
				return THERMAL_ERROR;

			__tt[size - 1].id = nla_get_u32(attr);
		}

		if (nla_type(attr) == THERMAL_GENL_ATTR_TZ_TRIP_TYPE)
			__tt[size - 1].type = nla_get_u32(attr);

		if (nla_type(attr) == THERMAL_GENL_ATTR_TZ_TRIP_TEMP)
			__tt[size - 1].temp = nla_get_u32(attr);

		if (nla_type(attr) == THERMAL_GENL_ATTR_TZ_TRIP_HYST)
			__tt[size - 1].hyst = nla_get_u32(attr);
	}

	if (__tt)
		__tt[size].id = -1;

	tz->trip = __tt;

	return THERMAL_SUCCESS;
}

static int parse_tz_get_temp(struct genl_info *info, struct thermal_zone *tz)
{
	int id = -1;

	if (info->attrs[THERMAL_GENL_ATTR_TZ_ID])
		id = nla_get_u32(info->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	if (tz->id != id)
		return THERMAL_ERROR;

	if (info->attrs[THERMAL_GENL_ATTR_TZ_TEMP])
		tz->temp = nla_get_u32(info->attrs[THERMAL_GENL_ATTR_TZ_TEMP]);

	return THERMAL_SUCCESS;
}

static int parse_tz_get_gov(struct genl_info *info, struct thermal_zone *tz)
{
	int id = -1;

	if (info->attrs[THERMAL_GENL_ATTR_TZ_ID])
		id = nla_get_u32(info->attrs[THERMAL_GENL_ATTR_TZ_ID]);

	if (tz->id != id)
		return THERMAL_ERROR;

	if (info->attrs[THERMAL_GENL_ATTR_TZ_GOV_NAME]) {
		nla_strlcpy(tz->governor,
			    info->attrs[THERMAL_GENL_ATTR_TZ_GOV_NAME],
			    THERMAL_NAME_LENGTH);
	}

	return THERMAL_SUCCESS;
}

static int handle_netlink(struct nl_cache_ops *unused,
			  struct genl_cmd *cmd,
			  struct genl_info *info, void *arg)
{
	int ret;

	switch (cmd->c_id) {

	case THERMAL_GENL_CMD_TZ_GET_ID:
		ret = parse_tz_get(info, arg);
		break;

	case THERMAL_GENL_CMD_CDEV_GET:
		ret = parse_cdev_get(info, arg);
		break;

	case THERMAL_GENL_CMD_TZ_GET_TEMP:
		ret = parse_tz_get_temp(info, arg);
		break;

	case THERMAL_GENL_CMD_TZ_GET_TRIP:
		ret = parse_tz_get_trip(info, arg);
		break;

	case THERMAL_GENL_CMD_TZ_GET_GOV:
		ret = parse_tz_get_gov(info, arg);
		break;

	default:
		return THERMAL_ERROR;
	}

	return ret;
}

static struct genl_cmd thermal_cmds[] = {
	{
		.c_id		= THERMAL_GENL_CMD_TZ_GET_ID,
		.c_name		= (char *)"List thermal zones",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= THERMAL_GENL_ATTR_MAX,
		.c_attr_policy	= thermal_genl_policy,
	},
	{
		.c_id		= THERMAL_GENL_CMD_TZ_GET_GOV,
		.c_name		= (char *)"Get governor",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= THERMAL_GENL_ATTR_MAX,
		.c_attr_policy	= thermal_genl_policy,
	},
	{
		.c_id		= THERMAL_GENL_CMD_TZ_GET_TEMP,
		.c_name		= (char *)"Get thermal zone temperature",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= THERMAL_GENL_ATTR_MAX,
		.c_attr_policy	= thermal_genl_policy,
	},
	{
		.c_id		= THERMAL_GENL_CMD_TZ_GET_TRIP,
		.c_name		= (char *)"Get thermal zone trip points",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= THERMAL_GENL_ATTR_MAX,
		.c_attr_policy	= thermal_genl_policy,
	},
	{
		.c_id		= THERMAL_GENL_CMD_CDEV_GET,
		.c_name		= (char *)"Get cooling devices",
		.c_msg_parser	= handle_netlink,
		.c_maxattr	= THERMAL_GENL_ATTR_MAX,
		.c_attr_policy	= thermal_genl_policy,
	},
};

static struct genl_ops thermal_cmd_ops = {
	.o_name		= (char *)"thermal",
	.o_cmds		= thermal_cmds,
	.o_ncmds	= ARRAY_SIZE(thermal_cmds),
};

static thermal_error_t thermal_genl_auto(struct thermal_handler *th, int id, int cmd,
					 int flags, void *arg)
{
	struct nl_msg *msg;
	void *hdr;

	msg = nlmsg_alloc();
	if (!msg)
		return THERMAL_ERROR;

	hdr = genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, thermal_cmd_ops.o_id,
			  0, flags, cmd, THERMAL_GENL_VERSION);
	if (!hdr)
		return THERMAL_ERROR;

	if (id >= 0 && nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_ID, id))
		return THERMAL_ERROR;

	if (nl_send_msg(th->sk_cmd, th->cb_cmd, msg, genl_handle_msg, arg))
		return THERMAL_ERROR;

	nlmsg_free(msg);

	return THERMAL_SUCCESS;
}

thermal_error_t thermal_cmd_get_tz(struct thermal_handler *th, struct thermal_zone **tz)
{
	return thermal_genl_auto(th, -1, THERMAL_GENL_CMD_TZ_GET_ID,
				 NLM_F_DUMP | NLM_F_ACK, tz);
}

thermal_error_t thermal_cmd_get_cdev(struct thermal_handler *th, struct thermal_cdev **tc)
{
	return thermal_genl_auto(th, -1, THERMAL_GENL_CMD_CDEV_GET,
				 NLM_F_DUMP | NLM_F_ACK, tc);
}

thermal_error_t thermal_cmd_get_trip(struct thermal_handler *th, struct thermal_zone *tz)
{
	return thermal_genl_auto(th, tz->id, THERMAL_GENL_CMD_TZ_GET_TRIP,
				 0, tz);
}

thermal_error_t thermal_cmd_get_governor(struct thermal_handler *th, struct thermal_zone *tz)
{
	return thermal_genl_auto(th, tz->id, THERMAL_GENL_CMD_TZ_GET_GOV, 0, tz);
}

thermal_error_t thermal_cmd_get_temp(struct thermal_handler *th, struct thermal_zone *tz)
{
	return thermal_genl_auto(th, tz->id, THERMAL_GENL_CMD_TZ_GET_TEMP, 0, tz);
}

thermal_error_t thermal_cmd_exit(struct thermal_handler *th)
{
	if (genl_unregister_family(&thermal_cmd_ops))
		return THERMAL_ERROR;

	nl_thermal_disconnect(th->sk_cmd, th->cb_cmd);

	return THERMAL_SUCCESS;
}

thermal_error_t thermal_cmd_init(struct thermal_handler *th)
{
	int ret;
	int family;

	if (nl_thermal_connect(&th->sk_cmd, &th->cb_cmd))
		return THERMAL_ERROR;

	ret = genl_register_family(&thermal_cmd_ops);
	if (ret)
		return THERMAL_ERROR;

	ret = genl_ops_resolve(th->sk_cmd, &thermal_cmd_ops);
	if (ret)
		return THERMAL_ERROR;

	family = genl_ctrl_resolve(th->sk_cmd, "nlctrl");
	if (family != GENL_ID_CTRL)
		return THERMAL_ERROR;

	return THERMAL_SUCCESS;
}
