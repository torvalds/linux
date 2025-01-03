// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>

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

        /* Thresholds */
        [THERMAL_GENL_ATTR_THRESHOLD]      	= { .type = NLA_NESTED },
        [THERMAL_GENL_ATTR_THRESHOLD_TEMP]      = { .type = NLA_U32 },
        [THERMAL_GENL_ATTR_THRESHOLD_DIRECTION] = { .type = NLA_U32 },
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

static int parse_threshold_get(struct genl_info *info, struct thermal_zone *tz)
{
	struct nlattr *attr;
	struct thermal_threshold *__tt = NULL;
	size_t size = 0;
	int rem;

	/*
	 * The size contains the size of the array and we want to
	 * access the last element, size - 1.
	 *
	 * The variable size is initialized to zero but it will be
	 * then incremented by the first if() statement. The message
	 * attributes are ordered, so the first if() statement will be
	 * always called before the second one. If it happens that is
	 * not the case, then it is a kernel bug.
	 */
	nla_for_each_nested(attr, info->attrs[THERMAL_GENL_ATTR_THRESHOLD], rem) {

		if (nla_type(attr) == THERMAL_GENL_ATTR_THRESHOLD_TEMP) {

			size++;

			__tt = realloc(__tt, sizeof(*__tt) * (size + 2));
			if (!__tt)
				return THERMAL_ERROR;

			__tt[size - 1].temperature = nla_get_u32(attr);
		}

		if (nla_type(attr) == THERMAL_GENL_ATTR_THRESHOLD_DIRECTION)
			__tt[size - 1].direction = nla_get_u32(attr);
	}

	if (__tt)
		__tt[size].temperature = INT_MAX;

	tz->thresholds = __tt;

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

	case THERMAL_GENL_CMD_THRESHOLD_GET:
		ret = parse_threshold_get(info, arg);
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
        {
                .c_id           = THERMAL_GENL_CMD_THRESHOLD_GET,
                .c_name         = (char *)"Get thresholds list",
                .c_msg_parser   = handle_netlink,
                .c_maxattr      = THERMAL_GENL_ATTR_MAX,
                .c_attr_policy  = thermal_genl_policy,
        },
        {
                .c_id           = THERMAL_GENL_CMD_THRESHOLD_ADD,
                .c_name         = (char *)"Add a threshold",
                .c_msg_parser   = handle_netlink,
                .c_maxattr      = THERMAL_GENL_ATTR_MAX,
                .c_attr_policy  = thermal_genl_policy,
        },
        {
                .c_id           = THERMAL_GENL_CMD_THRESHOLD_DELETE,
                .c_name         = (char *)"Delete a threshold",
                .c_msg_parser   = handle_netlink,
                .c_maxattr      = THERMAL_GENL_ATTR_MAX,
                .c_attr_policy  = thermal_genl_policy,
        },
        {
                .c_id           = THERMAL_GENL_CMD_THRESHOLD_FLUSH,
                .c_name         = (char *)"Flush the thresholds",
                .c_msg_parser   = handle_netlink,
                .c_maxattr      = THERMAL_GENL_ATTR_MAX,
                .c_attr_policy  = thermal_genl_policy,
        },
};

static struct genl_ops thermal_cmd_ops = {
	.o_name		= (char *)"thermal",
	.o_cmds		= thermal_cmds,
	.o_ncmds	= ARRAY_SIZE(thermal_cmds),
};

struct cmd_param {
	int tz_id;
	int temp;
	int direction;
};

typedef int (*cmd_cb_t)(struct nl_msg *, struct cmd_param *);

static int thermal_genl_tz_id_encode(struct nl_msg *msg, struct cmd_param *p)
{
	if (nla_put_u32(msg, THERMAL_GENL_ATTR_TZ_ID, p->tz_id))
		return -1;

	return 0;
}

static int thermal_genl_threshold_encode(struct nl_msg *msg, struct cmd_param *p)
{
	if (thermal_genl_tz_id_encode(msg, p))
		return -1;

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_THRESHOLD_TEMP, p->temp))
		return -1;

	if (nla_put_u32(msg, THERMAL_GENL_ATTR_THRESHOLD_DIRECTION, p->direction))
		return -1;

	return 0;
}

static thermal_error_t thermal_genl_auto(struct thermal_handler *th, cmd_cb_t cmd_cb,
					 struct cmd_param *param,
					 int cmd, int flags, void *arg)
{
	thermal_error_t ret = THERMAL_ERROR;
	struct nl_msg *msg;
	void *hdr;

	msg = nlmsg_alloc();
	if (!msg)
		return THERMAL_ERROR;

	hdr = genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, thermal_cmd_ops.o_id,
			  0, flags, cmd, THERMAL_GENL_VERSION);
	if (!hdr)
		goto out;

	if (cmd_cb && cmd_cb(msg, param))
		goto out;

	if (nl_send_msg(th->sk_cmd, th->cb_cmd, msg, genl_handle_msg, arg))
		goto out;

	ret = THERMAL_SUCCESS;
out:
	nlmsg_free(msg);

	return ret;
}

thermal_error_t thermal_cmd_get_tz(struct thermal_handler *th, struct thermal_zone **tz)
{
	return thermal_genl_auto(th, NULL, NULL, THERMAL_GENL_CMD_TZ_GET_ID,
				 NLM_F_DUMP | NLM_F_ACK, tz);
}

thermal_error_t thermal_cmd_get_cdev(struct thermal_handler *th, struct thermal_cdev **tc)
{
	return thermal_genl_auto(th, NULL, NULL, THERMAL_GENL_CMD_CDEV_GET,
				 NLM_F_DUMP | NLM_F_ACK, tc);
}

thermal_error_t thermal_cmd_get_trip(struct thermal_handler *th, struct thermal_zone *tz)
{
	struct cmd_param p = { .tz_id = tz->id };

	return thermal_genl_auto(th, thermal_genl_tz_id_encode, &p,
				 THERMAL_GENL_CMD_TZ_GET_TRIP, 0, tz);
}

thermal_error_t thermal_cmd_get_governor(struct thermal_handler *th, struct thermal_zone *tz)
{
	struct cmd_param p = { .tz_id = tz->id };

	return thermal_genl_auto(th, thermal_genl_tz_id_encode, &p,
				 THERMAL_GENL_CMD_TZ_GET_GOV, 0, tz);
}

thermal_error_t thermal_cmd_get_temp(struct thermal_handler *th, struct thermal_zone *tz)
{
	struct cmd_param p = { .tz_id = tz->id };

	return thermal_genl_auto(th, thermal_genl_tz_id_encode, &p,
				 THERMAL_GENL_CMD_TZ_GET_TEMP, 0, tz);
}

thermal_error_t thermal_cmd_threshold_get(struct thermal_handler *th,
                                          struct thermal_zone *tz)
{
	struct cmd_param p = { .tz_id = tz->id };

        return thermal_genl_auto(th, thermal_genl_tz_id_encode, &p,
				 THERMAL_GENL_CMD_THRESHOLD_GET, 0, tz);
}

thermal_error_t thermal_cmd_threshold_add(struct thermal_handler *th,
                                          struct thermal_zone *tz,
                                          int temperature,
                                          int direction)
{
	struct cmd_param p = { .tz_id = tz->id, .temp = temperature, .direction = direction };

        return thermal_genl_auto(th, thermal_genl_threshold_encode, &p,
				 THERMAL_GENL_CMD_THRESHOLD_ADD, 0, tz);
}

thermal_error_t thermal_cmd_threshold_delete(struct thermal_handler *th,
                                             struct thermal_zone *tz,
                                             int temperature,
                                             int direction)
{
	struct cmd_param p = { .tz_id = tz->id, .temp = temperature, .direction = direction };

        return thermal_genl_auto(th, thermal_genl_threshold_encode, &p,
				 THERMAL_GENL_CMD_THRESHOLD_DELETE, 0, tz);
}

thermal_error_t thermal_cmd_threshold_flush(struct thermal_handler *th,
                                            struct thermal_zone *tz)
{
	struct cmd_param p = { .tz_id = tz->id };

        return thermal_genl_auto(th, thermal_genl_tz_id_encode, &p,
				 THERMAL_GENL_CMD_THRESHOLD_FLUSH, 0, tz);
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
