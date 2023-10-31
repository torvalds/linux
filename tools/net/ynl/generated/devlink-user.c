// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN user source */

#include <stdlib.h>
#include <string.h>
#include "devlink-user.h"
#include "ynl.h"
#include <linux/devlink.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

/* Enums */
static const char * const devlink_op_strmap[] = {
	[3] = "get",
	[7] = "port-get",
	[13] = "sb-get",
	[17] = "sb-pool-get",
	[21] = "sb-port-pool-get",
	[25] = "sb-tc-pool-bind-get",
	[DEVLINK_CMD_PARAM_GET] = "param-get",
	[DEVLINK_CMD_REGION_GET] = "region-get",
	[DEVLINK_CMD_INFO_GET] = "info-get",
	[DEVLINK_CMD_HEALTH_REPORTER_GET] = "health-reporter-get",
	[63] = "trap-get",
	[67] = "trap-group-get",
	[71] = "trap-policer-get",
	[76] = "rate-get",
	[80] = "linecard-get",
	[DEVLINK_CMD_SELFTESTS_GET] = "selftests-get",
};

const char *devlink_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(devlink_op_strmap))
		return NULL;
	return devlink_op_strmap[op];
}

static const char * const devlink_sb_pool_type_strmap[] = {
	[0] = "ingress",
	[1] = "egress",
};

const char *devlink_sb_pool_type_str(enum devlink_sb_pool_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(devlink_sb_pool_type_strmap))
		return NULL;
	return devlink_sb_pool_type_strmap[value];
}

/* Policies */
struct ynl_policy_attr devlink_dl_info_version_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_INFO_VERSION_NAME] = { .name = "info-version-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_VALUE] = { .name = "info-version-value", .type = YNL_PT_NUL_STR, },
};

struct ynl_policy_nest devlink_dl_info_version_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_info_version_policy,
};

struct ynl_policy_attr devlink_dl_reload_stats_entry_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_STATS_LIMIT] = { .name = "reload-stats-limit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_STATS_VALUE] = { .name = "reload-stats-value", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_dl_reload_stats_entry_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_stats_entry_policy,
};

struct ynl_policy_attr devlink_dl_reload_act_stats_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_STATS_ENTRY] = { .name = "reload-stats-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_entry_nest, },
};

struct ynl_policy_nest devlink_dl_reload_act_stats_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_act_stats_policy,
};

struct ynl_policy_attr devlink_dl_reload_act_info_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_ACTION] = { .name = "reload-action", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_ACTION_STATS] = { .name = "reload-action-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_stats_nest, },
};

struct ynl_policy_nest devlink_dl_reload_act_info_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_act_info_policy,
};

struct ynl_policy_attr devlink_dl_reload_stats_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_ACTION_INFO] = { .name = "reload-action-info", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_info_nest, },
};

struct ynl_policy_nest devlink_dl_reload_stats_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_reload_stats_policy,
};

struct ynl_policy_attr devlink_dl_dev_stats_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_RELOAD_STATS] = { .name = "reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_REMOTE_RELOAD_STATS] = { .name = "remote-reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
};

struct ynl_policy_nest devlink_dl_dev_stats_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_dl_dev_stats_policy,
};

struct ynl_policy_attr devlink_policy[DEVLINK_ATTR_MAX + 1] = {
	[DEVLINK_ATTR_BUS_NAME] = { .name = "bus-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_DEV_NAME] = { .name = "dev-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_PORT_INDEX] = { .name = "port-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_SB_INDEX] = { .name = "sb-index", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_SB_POOL_INDEX] = { .name = "sb-pool-index", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_SB_POOL_TYPE] = { .name = "sb-pool-type", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_SB_TC_INDEX] = { .name = "sb-tc-index", .type = YNL_PT_U16, },
	[DEVLINK_ATTR_PARAM_NAME] = { .name = "param-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_REGION_NAME] = { .name = "region-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_DRIVER_NAME] = { .name = "info-driver-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_SERIAL_NUMBER] = { .name = "info-serial-number", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_FIXED] = { .name = "info-version-fixed", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_RUNNING] = { .name = "info-version-running", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_STORED] = { .name = "info-version-stored", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_NAME] = { .name = "info-version-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_VALUE] = { .name = "info-version-value", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_HEALTH_REPORTER_NAME] = { .name = "health-reporter-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_TRAP_NAME] = { .name = "trap-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_TRAP_GROUP_NAME] = { .name = "trap-group-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_RELOAD_FAILED] = { .name = "reload-failed", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_TRAP_POLICER_ID] = { .name = "trap-policer-id", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_RELOAD_ACTION] = { .name = "reload-action", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DEV_STATS] = { .name = "dev-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_dev_stats_nest, },
	[DEVLINK_ATTR_RELOAD_STATS] = { .name = "reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_RELOAD_STATS_ENTRY] = { .name = "reload-stats-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_entry_nest, },
	[DEVLINK_ATTR_RELOAD_STATS_LIMIT] = { .name = "reload-stats-limit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_STATS_VALUE] = { .name = "reload-stats-value", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_REMOTE_RELOAD_STATS] = { .name = "remote-reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_RELOAD_ACTION_INFO] = { .name = "reload-action-info", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_info_nest, },
	[DEVLINK_ATTR_RELOAD_ACTION_STATS] = { .name = "reload-action-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_stats_nest, },
	[DEVLINK_ATTR_RATE_NODE_NAME] = { .name = "rate-node-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_LINECARD_INDEX] = { .name = "linecard-index", .type = YNL_PT_U32, },
};

struct ynl_policy_nest devlink_nest = {
	.max_attr = DEVLINK_ATTR_MAX,
	.table = devlink_policy,
};

/* Common nested types */
void devlink_dl_info_version_free(struct devlink_dl_info_version *obj)
{
	free(obj->info_version_name);
	free(obj->info_version_value);
}

int devlink_dl_info_version_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_info_version *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_INFO_VERSION_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_version_name_len = len;
			dst->info_version_name = malloc(len + 1);
			memcpy(dst->info_version_name, mnl_attr_get_str(attr), len);
			dst->info_version_name[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_VALUE) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_version_value_len = len;
			dst->info_version_value = malloc(len + 1);
			memcpy(dst->info_version_value, mnl_attr_get_str(attr), len);
			dst->info_version_value[len] = 0;
		}
	}

	return 0;
}

void
devlink_dl_reload_stats_entry_free(struct devlink_dl_reload_stats_entry *obj)
{
}

int devlink_dl_reload_stats_entry_parse(struct ynl_parse_arg *yarg,
					const struct nlattr *nested)
{
	struct devlink_dl_reload_stats_entry *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_STATS_LIMIT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_stats_limit = 1;
			dst->reload_stats_limit = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RELOAD_STATS_VALUE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_stats_value = 1;
			dst->reload_stats_value = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

void devlink_dl_reload_act_stats_free(struct devlink_dl_reload_act_stats *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_reload_stats_entry; i++)
		devlink_dl_reload_stats_entry_free(&obj->reload_stats_entry[i]);
	free(obj->reload_stats_entry);
}

int devlink_dl_reload_act_stats_parse(struct ynl_parse_arg *yarg,
				      const struct nlattr *nested)
{
	struct devlink_dl_reload_act_stats *dst = yarg->data;
	unsigned int n_reload_stats_entry = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->reload_stats_entry)
		return ynl_error_parse(yarg, "attribute already present (dl-reload-act-stats.reload-stats-entry)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_STATS_ENTRY) {
			n_reload_stats_entry++;
		}
	}

	if (n_reload_stats_entry) {
		dst->reload_stats_entry = calloc(n_reload_stats_entry, sizeof(*dst->reload_stats_entry));
		dst->n_reload_stats_entry = n_reload_stats_entry;
		i = 0;
		parg.rsp_policy = &devlink_dl_reload_stats_entry_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RELOAD_STATS_ENTRY) {
				parg.data = &dst->reload_stats_entry[i];
				if (devlink_dl_reload_stats_entry_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_reload_act_info_free(struct devlink_dl_reload_act_info *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_reload_action_stats; i++)
		devlink_dl_reload_act_stats_free(&obj->reload_action_stats[i]);
	free(obj->reload_action_stats);
}

int devlink_dl_reload_act_info_parse(struct ynl_parse_arg *yarg,
				     const struct nlattr *nested)
{
	struct devlink_dl_reload_act_info *dst = yarg->data;
	unsigned int n_reload_action_stats = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->reload_action_stats)
		return ynl_error_parse(yarg, "attribute already present (dl-reload-act-info.reload-action-stats)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_ACTION) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_action = 1;
			dst->reload_action = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RELOAD_ACTION_STATS) {
			n_reload_action_stats++;
		}
	}

	if (n_reload_action_stats) {
		dst->reload_action_stats = calloc(n_reload_action_stats, sizeof(*dst->reload_action_stats));
		dst->n_reload_action_stats = n_reload_action_stats;
		i = 0;
		parg.rsp_policy = &devlink_dl_reload_act_stats_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RELOAD_ACTION_STATS) {
				parg.data = &dst->reload_action_stats[i];
				if (devlink_dl_reload_act_stats_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_reload_stats_free(struct devlink_dl_reload_stats *obj)
{
	unsigned int i;

	for (i = 0; i < obj->n_reload_action_info; i++)
		devlink_dl_reload_act_info_free(&obj->reload_action_info[i]);
	free(obj->reload_action_info);
}

int devlink_dl_reload_stats_parse(struct ynl_parse_arg *yarg,
				  const struct nlattr *nested)
{
	struct devlink_dl_reload_stats *dst = yarg->data;
	unsigned int n_reload_action_info = 0;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	parg.ys = yarg->ys;

	if (dst->reload_action_info)
		return ynl_error_parse(yarg, "attribute already present (dl-reload-stats.reload-action-info)");

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_ACTION_INFO) {
			n_reload_action_info++;
		}
	}

	if (n_reload_action_info) {
		dst->reload_action_info = calloc(n_reload_action_info, sizeof(*dst->reload_action_info));
		dst->n_reload_action_info = n_reload_action_info;
		i = 0;
		parg.rsp_policy = &devlink_dl_reload_act_info_nest;
		mnl_attr_for_each_nested(attr, nested) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_RELOAD_ACTION_INFO) {
				parg.data = &dst->reload_action_info[i];
				if (devlink_dl_reload_act_info_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return 0;
}

void devlink_dl_dev_stats_free(struct devlink_dl_dev_stats *obj)
{
	devlink_dl_reload_stats_free(&obj->reload_stats);
	devlink_dl_reload_stats_free(&obj->remote_reload_stats);
}

int devlink_dl_dev_stats_parse(struct ynl_parse_arg *yarg,
			       const struct nlattr *nested)
{
	struct devlink_dl_dev_stats *dst = yarg->data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	parg.ys = yarg->ys;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_RELOAD_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_stats = 1;

			parg.rsp_policy = &devlink_dl_reload_stats_nest;
			parg.data = &dst->reload_stats;
			if (devlink_dl_reload_stats_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == DEVLINK_ATTR_REMOTE_RELOAD_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.remote_reload_stats = 1;

			parg.rsp_policy = &devlink_dl_reload_stats_nest;
			parg.data = &dst->remote_reload_stats;
			if (devlink_dl_reload_stats_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return 0;
}

/* ============== DEVLINK_CMD_GET ============== */
/* DEVLINK_CMD_GET - do */
void devlink_get_req_free(struct devlink_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_get_rsp_free(struct devlink_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	devlink_dl_dev_stats_free(&rsp->dev_stats);
	free(rsp);
}

int devlink_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_RELOAD_FAILED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_failed = 1;
			dst->reload_failed = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_RELOAD_ACTION) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.reload_action = 1;
			dst->reload_action = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_DEV_STATS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dev_stats = 1;

			parg.rsp_policy = &devlink_dl_dev_stats_nest;
			parg.data = &dst->dev_stats;
			if (devlink_dl_dev_stats_parse(&parg, attr))
				return MNL_CB_ERROR;
		}
	}

	return MNL_CB_OK;
}

struct devlink_get_rsp *
devlink_get(struct ynl_sock *ys, struct devlink_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_get_rsp_parse;
	yrs.rsp_cmd = 3;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_GET - dump */
void devlink_get_list_free(struct devlink_get_list *rsp)
{
	struct devlink_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		devlink_dl_dev_stats_free(&rsp->obj.dev_stats);
		free(rsp);
	}
}

struct devlink_get_list *devlink_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_get_list);
	yds.cb = devlink_get_rsp_parse;
	yds.rsp_cmd = 3;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PORT_GET ============== */
/* DEVLINK_CMD_PORT_GET - do */
void devlink_port_get_req_free(struct devlink_port_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_port_get_rsp_free(struct devlink_port_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_port_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_port_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_port_get_rsp *
devlink_port_get(struct ynl_sock *ys, struct devlink_port_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_port_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PORT_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_port_get_rsp_parse;
	yrs.rsp_cmd = 7;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_port_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_PORT_GET - dump */
int devlink_port_get_rsp_dump_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_port_get_rsp_dump *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

void devlink_port_get_rsp_list_free(struct devlink_port_get_rsp_list *rsp)
{
	struct devlink_port_get_rsp_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_port_get_rsp_list *
devlink_port_get_dump(struct ynl_sock *ys,
		      struct devlink_port_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_port_get_rsp_list);
	yds.cb = devlink_port_get_rsp_dump_parse;
	yds.rsp_cmd = 7;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_PORT_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_port_get_rsp_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_GET ============== */
/* DEVLINK_CMD_SB_GET - do */
void devlink_sb_get_req_free(struct devlink_sb_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_sb_get_rsp_free(struct devlink_sb_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_sb_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_get_rsp *
devlink_sb_get(struct ynl_sock *ys, struct devlink_sb_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_get_rsp_parse;
	yrs.rsp_cmd = 13;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_GET - dump */
void devlink_sb_get_list_free(struct devlink_sb_get_list *rsp)
{
	struct devlink_sb_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_get_list *
devlink_sb_get_dump(struct ynl_sock *ys, struct devlink_sb_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_get_list);
	yds.cb = devlink_sb_get_rsp_parse;
	yds.rsp_cmd = 13;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_POOL_GET ============== */
/* DEVLINK_CMD_SB_POOL_GET - do */
void devlink_sb_pool_get_req_free(struct devlink_sb_pool_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_sb_pool_get_rsp_free(struct devlink_sb_pool_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_pool_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_sb_pool_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_POOL_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_pool_index = 1;
			dst->sb_pool_index = mnl_attr_get_u16(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_pool_get_rsp *
devlink_sb_pool_get(struct ynl_sock *ys, struct devlink_sb_pool_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_pool_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_POOL_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_pool_get_rsp_parse;
	yrs.rsp_cmd = 17;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_pool_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_POOL_GET - dump */
void devlink_sb_pool_get_list_free(struct devlink_sb_pool_get_list *rsp)
{
	struct devlink_sb_pool_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_pool_get_list *
devlink_sb_pool_get_dump(struct ynl_sock *ys,
			 struct devlink_sb_pool_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_pool_get_list);
	yds.cb = devlink_sb_pool_get_rsp_parse;
	yds.rsp_cmd = 17;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_POOL_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_pool_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_PORT_POOL_GET ============== */
/* DEVLINK_CMD_SB_PORT_POOL_GET - do */
void
devlink_sb_port_pool_get_req_free(struct devlink_sb_port_pool_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_sb_port_pool_get_rsp_free(struct devlink_sb_port_pool_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_port_pool_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_sb_port_pool_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_POOL_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_pool_index = 1;
			dst->sb_pool_index = mnl_attr_get_u16(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_port_pool_get_rsp *
devlink_sb_port_pool_get(struct ynl_sock *ys,
			 struct devlink_sb_port_pool_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_port_pool_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_PORT_POOL_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_POOL_INDEX, req->sb_pool_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_port_pool_get_rsp_parse;
	yrs.rsp_cmd = 21;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_port_pool_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_PORT_POOL_GET - dump */
void
devlink_sb_port_pool_get_list_free(struct devlink_sb_port_pool_get_list *rsp)
{
	struct devlink_sb_port_pool_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_port_pool_get_list *
devlink_sb_port_pool_get_dump(struct ynl_sock *ys,
			      struct devlink_sb_port_pool_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_port_pool_get_list);
	yds.cb = devlink_sb_port_pool_get_rsp_parse;
	yds.rsp_cmd = 21;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_PORT_POOL_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_port_pool_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SB_TC_POOL_BIND_GET ============== */
/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - do */
void
devlink_sb_tc_pool_bind_get_req_free(struct devlink_sb_tc_pool_bind_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_sb_tc_pool_bind_get_rsp_free(struct devlink_sb_tc_pool_bind_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_sb_tc_pool_bind_get_rsp_parse(const struct nlmsghdr *nlh,
					  void *data)
{
	struct devlink_sb_tc_pool_bind_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_index = 1;
			dst->sb_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_SB_POOL_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_pool_type = 1;
			dst->sb_pool_type = mnl_attr_get_u8(attr);
		} else if (type == DEVLINK_ATTR_SB_TC_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sb_tc_index = 1;
			dst->sb_tc_index = mnl_attr_get_u16(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_sb_tc_pool_bind_get_rsp *
devlink_sb_tc_pool_bind_get(struct ynl_sock *ys,
			    struct devlink_sb_tc_pool_bind_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_sb_tc_pool_bind_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SB_TC_POOL_BIND_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.sb_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_SB_INDEX, req->sb_index);
	if (req->_present.sb_pool_type)
		mnl_attr_put_u8(nlh, DEVLINK_ATTR_SB_POOL_TYPE, req->sb_pool_type);
	if (req->_present.sb_tc_index)
		mnl_attr_put_u16(nlh, DEVLINK_ATTR_SB_TC_INDEX, req->sb_tc_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_sb_tc_pool_bind_get_rsp_parse;
	yrs.rsp_cmd = 25;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_sb_tc_pool_bind_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - dump */
void
devlink_sb_tc_pool_bind_get_list_free(struct devlink_sb_tc_pool_bind_get_list *rsp)
{
	struct devlink_sb_tc_pool_bind_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_sb_tc_pool_bind_get_list *
devlink_sb_tc_pool_bind_get_dump(struct ynl_sock *ys,
				 struct devlink_sb_tc_pool_bind_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_sb_tc_pool_bind_get_list);
	yds.cb = devlink_sb_tc_pool_bind_get_rsp_parse;
	yds.rsp_cmd = 25;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SB_TC_POOL_BIND_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_sb_tc_pool_bind_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_PARAM_GET ============== */
/* DEVLINK_CMD_PARAM_GET - do */
void devlink_param_get_req_free(struct devlink_param_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->param_name);
	free(req);
}

void devlink_param_get_rsp_free(struct devlink_param_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->param_name);
	free(rsp);
}

int devlink_param_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_param_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PARAM_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.param_name_len = len;
			dst->param_name = malloc(len + 1);
			memcpy(dst->param_name, mnl_attr_get_str(attr), len);
			dst->param_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_param_get_rsp *
devlink_param_get(struct ynl_sock *ys, struct devlink_param_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_param_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_PARAM_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.param_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_PARAM_NAME, req->param_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_param_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_PARAM_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_param_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_PARAM_GET - dump */
void devlink_param_get_list_free(struct devlink_param_get_list *rsp)
{
	struct devlink_param_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.param_name);
		free(rsp);
	}
}

struct devlink_param_get_list *
devlink_param_get_dump(struct ynl_sock *ys,
		       struct devlink_param_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_param_get_list);
	yds.cb = devlink_param_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_PARAM_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_PARAM_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_param_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_REGION_GET ============== */
/* DEVLINK_CMD_REGION_GET - do */
void devlink_region_get_req_free(struct devlink_region_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->region_name);
	free(req);
}

void devlink_region_get_rsp_free(struct devlink_region_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->region_name);
	free(rsp);
}

int devlink_region_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_region_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_REGION_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.region_name_len = len;
			dst->region_name = malloc(len + 1);
			memcpy(dst->region_name, mnl_attr_get_str(attr), len);
			dst->region_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_region_get_rsp *
devlink_region_get(struct ynl_sock *ys, struct devlink_region_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_region_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_REGION_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.region_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_REGION_NAME, req->region_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_region_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_REGION_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_region_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_REGION_GET - dump */
void devlink_region_get_list_free(struct devlink_region_get_list *rsp)
{
	struct devlink_region_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.region_name);
		free(rsp);
	}
}

struct devlink_region_get_list *
devlink_region_get_dump(struct ynl_sock *ys,
			struct devlink_region_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_region_get_list);
	yds.cb = devlink_region_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_REGION_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_REGION_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_region_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_INFO_GET ============== */
/* DEVLINK_CMD_INFO_GET - do */
void devlink_info_get_req_free(struct devlink_info_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_info_get_rsp_free(struct devlink_info_get_rsp *rsp)
{
	unsigned int i;

	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->info_driver_name);
	free(rsp->info_serial_number);
	for (i = 0; i < rsp->n_info_version_fixed; i++)
		devlink_dl_info_version_free(&rsp->info_version_fixed[i]);
	free(rsp->info_version_fixed);
	for (i = 0; i < rsp->n_info_version_running; i++)
		devlink_dl_info_version_free(&rsp->info_version_running[i]);
	free(rsp->info_version_running);
	for (i = 0; i < rsp->n_info_version_stored; i++)
		devlink_dl_info_version_free(&rsp->info_version_stored[i]);
	free(rsp->info_version_stored);
	free(rsp);
}

int devlink_info_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	unsigned int n_info_version_running = 0;
	unsigned int n_info_version_stored = 0;
	unsigned int n_info_version_fixed = 0;
	struct ynl_parse_arg *yarg = data;
	struct devlink_info_get_rsp *dst;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;
	int i;

	dst = yarg->data;
	parg.ys = yarg->ys;

	if (dst->info_version_fixed)
		return ynl_error_parse(yarg, "attribute already present (devlink.info-version-fixed)");
	if (dst->info_version_running)
		return ynl_error_parse(yarg, "attribute already present (devlink.info-version-running)");
	if (dst->info_version_stored)
		return ynl_error_parse(yarg, "attribute already present (devlink.info-version-stored)");

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_DRIVER_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_driver_name_len = len;
			dst->info_driver_name = malloc(len + 1);
			memcpy(dst->info_driver_name, mnl_attr_get_str(attr), len);
			dst->info_driver_name[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_SERIAL_NUMBER) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.info_serial_number_len = len;
			dst->info_serial_number = malloc(len + 1);
			memcpy(dst->info_serial_number, mnl_attr_get_str(attr), len);
			dst->info_serial_number[len] = 0;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_FIXED) {
			n_info_version_fixed++;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_RUNNING) {
			n_info_version_running++;
		} else if (type == DEVLINK_ATTR_INFO_VERSION_STORED) {
			n_info_version_stored++;
		}
	}

	if (n_info_version_fixed) {
		dst->info_version_fixed = calloc(n_info_version_fixed, sizeof(*dst->info_version_fixed));
		dst->n_info_version_fixed = n_info_version_fixed;
		i = 0;
		parg.rsp_policy = &devlink_dl_info_version_nest;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_INFO_VERSION_FIXED) {
				parg.data = &dst->info_version_fixed[i];
				if (devlink_dl_info_version_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}
	if (n_info_version_running) {
		dst->info_version_running = calloc(n_info_version_running, sizeof(*dst->info_version_running));
		dst->n_info_version_running = n_info_version_running;
		i = 0;
		parg.rsp_policy = &devlink_dl_info_version_nest;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_INFO_VERSION_RUNNING) {
				parg.data = &dst->info_version_running[i];
				if (devlink_dl_info_version_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}
	if (n_info_version_stored) {
		dst->info_version_stored = calloc(n_info_version_stored, sizeof(*dst->info_version_stored));
		dst->n_info_version_stored = n_info_version_stored;
		i = 0;
		parg.rsp_policy = &devlink_dl_info_version_nest;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == DEVLINK_ATTR_INFO_VERSION_STORED) {
				parg.data = &dst->info_version_stored[i];
				if (devlink_dl_info_version_parse(&parg, attr))
					return MNL_CB_ERROR;
				i++;
			}
		}
	}

	return MNL_CB_OK;
}

struct devlink_info_get_rsp *
devlink_info_get(struct ynl_sock *ys, struct devlink_info_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_info_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_INFO_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_info_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_INFO_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_info_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_INFO_GET - dump */
void devlink_info_get_list_free(struct devlink_info_get_list *rsp)
{
	struct devlink_info_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		unsigned int i;

		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.info_driver_name);
		free(rsp->obj.info_serial_number);
		for (i = 0; i < rsp->obj.n_info_version_fixed; i++)
			devlink_dl_info_version_free(&rsp->obj.info_version_fixed[i]);
		free(rsp->obj.info_version_fixed);
		for (i = 0; i < rsp->obj.n_info_version_running; i++)
			devlink_dl_info_version_free(&rsp->obj.info_version_running[i]);
		free(rsp->obj.info_version_running);
		for (i = 0; i < rsp->obj.n_info_version_stored; i++)
			devlink_dl_info_version_free(&rsp->obj.info_version_stored[i]);
		free(rsp->obj.info_version_stored);
		free(rsp);
	}
}

struct devlink_info_get_list *devlink_info_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_info_get_list);
	yds.cb = devlink_info_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_INFO_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_INFO_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_info_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_HEALTH_REPORTER_GET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_GET - do */
void
devlink_health_reporter_get_req_free(struct devlink_health_reporter_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->health_reporter_name);
	free(req);
}

void
devlink_health_reporter_get_rsp_free(struct devlink_health_reporter_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->health_reporter_name);
	free(rsp);
}

int devlink_health_reporter_get_rsp_parse(const struct nlmsghdr *nlh,
					  void *data)
{
	struct devlink_health_reporter_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_HEALTH_REPORTER_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.health_reporter_name_len = len;
			dst->health_reporter_name = malloc(len + 1);
			memcpy(dst->health_reporter_name, mnl_attr_get_str(attr), len);
			dst->health_reporter_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_health_reporter_get_rsp *
devlink_health_reporter_get(struct ynl_sock *ys,
			    struct devlink_health_reporter_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_health_reporter_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.health_reporter_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_HEALTH_REPORTER_NAME, req->health_reporter_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_health_reporter_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_HEALTH_REPORTER_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_health_reporter_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_HEALTH_REPORTER_GET - dump */
void
devlink_health_reporter_get_list_free(struct devlink_health_reporter_get_list *rsp)
{
	struct devlink_health_reporter_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.health_reporter_name);
		free(rsp);
	}
}

struct devlink_health_reporter_get_list *
devlink_health_reporter_get_dump(struct ynl_sock *ys,
				 struct devlink_health_reporter_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_health_reporter_get_list);
	yds.cb = devlink_health_reporter_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_HEALTH_REPORTER_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_HEALTH_REPORTER_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_health_reporter_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_TRAP_GET ============== */
/* DEVLINK_CMD_TRAP_GET - do */
void devlink_trap_get_req_free(struct devlink_trap_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->trap_name);
	free(req);
}

void devlink_trap_get_rsp_free(struct devlink_trap_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->trap_name);
	free(rsp);
}

int devlink_trap_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_trap_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_TRAP_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.trap_name_len = len;
			dst->trap_name = malloc(len + 1);
			memcpy(dst->trap_name, mnl_attr_get_str(attr), len);
			dst->trap_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_trap_get_rsp *
devlink_trap_get(struct ynl_sock *ys, struct devlink_trap_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_trap_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_TRAP_NAME, req->trap_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_trap_get_rsp_parse;
	yrs.rsp_cmd = 63;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_trap_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_TRAP_GET - dump */
void devlink_trap_get_list_free(struct devlink_trap_get_list *rsp)
{
	struct devlink_trap_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.trap_name);
		free(rsp);
	}
}

struct devlink_trap_get_list *
devlink_trap_get_dump(struct ynl_sock *ys,
		      struct devlink_trap_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_trap_get_list);
	yds.cb = devlink_trap_get_rsp_parse;
	yds.rsp_cmd = 63;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_TRAP_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_trap_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_TRAP_GROUP_GET ============== */
/* DEVLINK_CMD_TRAP_GROUP_GET - do */
void devlink_trap_group_get_req_free(struct devlink_trap_group_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->trap_group_name);
	free(req);
}

void devlink_trap_group_get_rsp_free(struct devlink_trap_group_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->trap_group_name);
	free(rsp);
}

int devlink_trap_group_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_trap_group_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_TRAP_GROUP_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.trap_group_name_len = len;
			dst->trap_group_name = malloc(len + 1);
			memcpy(dst->trap_group_name, mnl_attr_get_str(attr), len);
			dst->trap_group_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_trap_group_get_rsp *
devlink_trap_group_get(struct ynl_sock *ys,
		       struct devlink_trap_group_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_trap_group_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_GROUP_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_group_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_TRAP_GROUP_NAME, req->trap_group_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_trap_group_get_rsp_parse;
	yrs.rsp_cmd = 67;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_trap_group_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_TRAP_GROUP_GET - dump */
void devlink_trap_group_get_list_free(struct devlink_trap_group_get_list *rsp)
{
	struct devlink_trap_group_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.trap_group_name);
		free(rsp);
	}
}

struct devlink_trap_group_get_list *
devlink_trap_group_get_dump(struct ynl_sock *ys,
			    struct devlink_trap_group_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_trap_group_get_list);
	yds.cb = devlink_trap_group_get_rsp_parse;
	yds.rsp_cmd = 67;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_TRAP_GROUP_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_trap_group_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_TRAP_POLICER_GET ============== */
/* DEVLINK_CMD_TRAP_POLICER_GET - do */
void
devlink_trap_policer_get_req_free(struct devlink_trap_policer_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void
devlink_trap_policer_get_rsp_free(struct devlink_trap_policer_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_trap_policer_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_trap_policer_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_TRAP_POLICER_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.trap_policer_id = 1;
			dst->trap_policer_id = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_trap_policer_get_rsp *
devlink_trap_policer_get(struct ynl_sock *ys,
			 struct devlink_trap_policer_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_trap_policer_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_TRAP_POLICER_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.trap_policer_id)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_TRAP_POLICER_ID, req->trap_policer_id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_trap_policer_get_rsp_parse;
	yrs.rsp_cmd = 71;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_trap_policer_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_TRAP_POLICER_GET - dump */
void
devlink_trap_policer_get_list_free(struct devlink_trap_policer_get_list *rsp)
{
	struct devlink_trap_policer_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_trap_policer_get_list *
devlink_trap_policer_get_dump(struct ynl_sock *ys,
			      struct devlink_trap_policer_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_trap_policer_get_list);
	yds.cb = devlink_trap_policer_get_rsp_parse;
	yds.rsp_cmd = 71;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_TRAP_POLICER_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_trap_policer_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_RATE_GET ============== */
/* DEVLINK_CMD_RATE_GET - do */
void devlink_rate_get_req_free(struct devlink_rate_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req->rate_node_name);
	free(req);
}

void devlink_rate_get_rsp_free(struct devlink_rate_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp->rate_node_name);
	free(rsp);
}

int devlink_rate_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct devlink_rate_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_PORT_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port_index = 1;
			dst->port_index = mnl_attr_get_u32(attr);
		} else if (type == DEVLINK_ATTR_RATE_NODE_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.rate_node_name_len = len;
			dst->rate_node_name = malloc(len + 1);
			memcpy(dst->rate_node_name, mnl_attr_get_str(attr), len);
			dst->rate_node_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_rate_get_rsp *
devlink_rate_get(struct ynl_sock *ys, struct devlink_rate_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_rate_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_RATE_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.port_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_PORT_INDEX, req->port_index);
	if (req->_present.rate_node_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_RATE_NODE_NAME, req->rate_node_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_rate_get_rsp_parse;
	yrs.rsp_cmd = 76;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_rate_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_RATE_GET - dump */
void devlink_rate_get_list_free(struct devlink_rate_get_list *rsp)
{
	struct devlink_rate_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp->obj.rate_node_name);
		free(rsp);
	}
}

struct devlink_rate_get_list *
devlink_rate_get_dump(struct ynl_sock *ys,
		      struct devlink_rate_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_rate_get_list);
	yds.cb = devlink_rate_get_rsp_parse;
	yds.rsp_cmd = 76;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_RATE_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_rate_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_LINECARD_GET ============== */
/* DEVLINK_CMD_LINECARD_GET - do */
void devlink_linecard_get_req_free(struct devlink_linecard_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_linecard_get_rsp_free(struct devlink_linecard_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_linecard_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_linecard_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		} else if (type == DEVLINK_ATTR_LINECARD_INDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.linecard_index = 1;
			dst->linecard_index = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct devlink_linecard_get_rsp *
devlink_linecard_get(struct ynl_sock *ys, struct devlink_linecard_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_linecard_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_LINECARD_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);
	if (req->_present.linecard_index)
		mnl_attr_put_u32(nlh, DEVLINK_ATTR_LINECARD_INDEX, req->linecard_index);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_linecard_get_rsp_parse;
	yrs.rsp_cmd = 80;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_linecard_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_LINECARD_GET - dump */
void devlink_linecard_get_list_free(struct devlink_linecard_get_list *rsp)
{
	struct devlink_linecard_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_linecard_get_list *
devlink_linecard_get_dump(struct ynl_sock *ys,
			  struct devlink_linecard_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_linecard_get_list);
	yds.cb = devlink_linecard_get_rsp_parse;
	yds.rsp_cmd = 80;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_LINECARD_GET, 1);
	ys->req_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_linecard_get_list_free(yds.first);
	return NULL;
}

/* ============== DEVLINK_CMD_SELFTESTS_GET ============== */
/* DEVLINK_CMD_SELFTESTS_GET - do */
void devlink_selftests_get_req_free(struct devlink_selftests_get_req *req)
{
	free(req->bus_name);
	free(req->dev_name);
	free(req);
}

void devlink_selftests_get_rsp_free(struct devlink_selftests_get_rsp *rsp)
{
	free(rsp->bus_name);
	free(rsp->dev_name);
	free(rsp);
}

int devlink_selftests_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct devlink_selftests_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == DEVLINK_ATTR_BUS_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.bus_name_len = len;
			dst->bus_name = malloc(len + 1);
			memcpy(dst->bus_name, mnl_attr_get_str(attr), len);
			dst->bus_name[len] = 0;
		} else if (type == DEVLINK_ATTR_DEV_NAME) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = strnlen(mnl_attr_get_str(attr), mnl_attr_get_payload_len(attr));
			dst->_present.dev_name_len = len;
			dst->dev_name = malloc(len + 1);
			memcpy(dst->dev_name, mnl_attr_get_str(attr), len);
			dst->dev_name[len] = 0;
		}
	}

	return MNL_CB_OK;
}

struct devlink_selftests_get_rsp *
devlink_selftests_get(struct ynl_sock *ys,
		      struct devlink_selftests_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct devlink_selftests_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, DEVLINK_CMD_SELFTESTS_GET, 1);
	ys->req_policy = &devlink_nest;
	yrs.yarg.rsp_policy = &devlink_nest;

	if (req->_present.bus_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_BUS_NAME, req->bus_name);
	if (req->_present.dev_name_len)
		mnl_attr_put_strz(nlh, DEVLINK_ATTR_DEV_NAME, req->dev_name);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = devlink_selftests_get_rsp_parse;
	yrs.rsp_cmd = DEVLINK_CMD_SELFTESTS_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	devlink_selftests_get_rsp_free(rsp);
	return NULL;
}

/* DEVLINK_CMD_SELFTESTS_GET - dump */
void devlink_selftests_get_list_free(struct devlink_selftests_get_list *rsp)
{
	struct devlink_selftests_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.bus_name);
		free(rsp->obj.dev_name);
		free(rsp);
	}
}

struct devlink_selftests_get_list *
devlink_selftests_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct devlink_selftests_get_list);
	yds.cb = devlink_selftests_get_rsp_parse;
	yds.rsp_cmd = DEVLINK_CMD_SELFTESTS_GET;
	yds.rsp_policy = &devlink_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, DEVLINK_CMD_SELFTESTS_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	devlink_selftests_get_list_free(yds.first);
	return NULL;
}

const struct ynl_family ynl_devlink_family =  {
	.name		= "devlink",
};
