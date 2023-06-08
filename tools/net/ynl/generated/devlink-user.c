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
	[DEVLINK_CMD_INFO_GET] = "info-get",
};

const char *devlink_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(devlink_op_strmap))
		return NULL;
	return devlink_op_strmap[op];
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
	[DEVLINK_ATTR_INFO_DRIVER_NAME] = { .name = "info-driver-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_SERIAL_NUMBER] = { .name = "info-serial-number", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_FIXED] = { .name = "info-version-fixed", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_RUNNING] = { .name = "info-version-running", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_STORED] = { .name = "info-version-stored", .type = YNL_PT_NEST, .nest = &devlink_dl_info_version_nest, },
	[DEVLINK_ATTR_INFO_VERSION_NAME] = { .name = "info-version-name", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_INFO_VERSION_VALUE] = { .name = "info-version-value", .type = YNL_PT_NUL_STR, },
	[DEVLINK_ATTR_RELOAD_FAILED] = { .name = "reload-failed", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_ACTION] = { .name = "reload-action", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_DEV_STATS] = { .name = "dev-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_dev_stats_nest, },
	[DEVLINK_ATTR_RELOAD_STATS] = { .name = "reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_RELOAD_STATS_ENTRY] = { .name = "reload-stats-entry", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_entry_nest, },
	[DEVLINK_ATTR_RELOAD_STATS_LIMIT] = { .name = "reload-stats-limit", .type = YNL_PT_U8, },
	[DEVLINK_ATTR_RELOAD_STATS_VALUE] = { .name = "reload-stats-value", .type = YNL_PT_U32, },
	[DEVLINK_ATTR_REMOTE_RELOAD_STATS] = { .name = "remote-reload-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_stats_nest, },
	[DEVLINK_ATTR_RELOAD_ACTION_INFO] = { .name = "reload-action-info", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_info_nest, },
	[DEVLINK_ATTR_RELOAD_ACTION_STATS] = { .name = "reload-action-stats", .type = YNL_PT_NEST, .nest = &devlink_dl_reload_act_stats_nest, },
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

const struct ynl_family ynl_devlink_family =  {
	.name		= "devlink",
};
