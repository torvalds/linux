// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN user source */

#include <stdlib.h>
#include <string.h>
#include "netdev-user.h"
#include "ynl.h"
#include <linux/netdev.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

/* Enums */
static const char * const netdev_op_strmap[] = {
	[NETDEV_CMD_DEV_GET] = "dev-get",
	[NETDEV_CMD_DEV_ADD_NTF] = "dev-add-ntf",
	[NETDEV_CMD_DEV_DEL_NTF] = "dev-del-ntf",
	[NETDEV_CMD_DEV_CHANGE_NTF] = "dev-change-ntf",
};

const char *netdev_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(netdev_op_strmap))
		return NULL;
	return netdev_op_strmap[op];
}

static const char * const netdev_xdp_act_strmap[] = {
	[0] = "basic",
	[1] = "redirect",
	[2] = "ndo-xmit",
	[3] = "xsk-zerocopy",
	[4] = "hw-offload",
	[5] = "rx-sg",
	[6] = "ndo-xmit-sg",
};

const char *netdev_xdp_act_str(enum netdev_xdp_act value)
{
	value = ffs(value) - 1;
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(netdev_xdp_act_strmap))
		return NULL;
	return netdev_xdp_act_strmap[value];
}

static const char * const netdev_xdp_rx_metadata_strmap[] = {
	[0] = "timestamp",
	[1] = "hash",
};

const char *netdev_xdp_rx_metadata_str(enum netdev_xdp_rx_metadata value)
{
	value = ffs(value) - 1;
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(netdev_xdp_rx_metadata_strmap))
		return NULL;
	return netdev_xdp_rx_metadata_strmap[value];
}

/* Policies */
struct ynl_policy_attr netdev_dev_policy[NETDEV_A_DEV_MAX + 1] = {
	[NETDEV_A_DEV_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
	[NETDEV_A_DEV_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[NETDEV_A_DEV_XDP_FEATURES] = { .name = "xdp-features", .type = YNL_PT_U64, },
	[NETDEV_A_DEV_XDP_ZC_MAX_SEGS] = { .name = "xdp-zc-max-segs", .type = YNL_PT_U32, },
	[NETDEV_A_DEV_XDP_RX_METADATA_FEATURES] = { .name = "xdp-rx-metadata-features", .type = YNL_PT_U64, },
};

struct ynl_policy_nest netdev_dev_nest = {
	.max_attr = NETDEV_A_DEV_MAX,
	.table = netdev_dev_policy,
};

/* Common nested types */
/* ============== NETDEV_CMD_DEV_GET ============== */
/* NETDEV_CMD_DEV_GET - do */
void netdev_dev_get_req_free(struct netdev_dev_get_req *req)
{
	free(req);
}

void netdev_dev_get_rsp_free(struct netdev_dev_get_rsp *rsp)
{
	free(rsp);
}

int netdev_dev_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct netdev_dev_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NETDEV_A_DEV_IFINDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ifindex = 1;
			dst->ifindex = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_DEV_XDP_FEATURES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.xdp_features = 1;
			dst->xdp_features = mnl_attr_get_u64(attr);
		} else if (type == NETDEV_A_DEV_XDP_ZC_MAX_SEGS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.xdp_zc_max_segs = 1;
			dst->xdp_zc_max_segs = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_DEV_XDP_RX_METADATA_FEATURES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.xdp_rx_metadata_features = 1;
			dst->xdp_rx_metadata_features = mnl_attr_get_u64(attr);
		}
	}

	return MNL_CB_OK;
}

struct netdev_dev_get_rsp *
netdev_dev_get(struct ynl_sock *ys, struct netdev_dev_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct netdev_dev_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, NETDEV_CMD_DEV_GET, 1);
	ys->req_policy = &netdev_dev_nest;
	yrs.yarg.rsp_policy = &netdev_dev_nest;

	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, NETDEV_A_DEV_IFINDEX, req->ifindex);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = netdev_dev_get_rsp_parse;
	yrs.rsp_cmd = NETDEV_CMD_DEV_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	netdev_dev_get_rsp_free(rsp);
	return NULL;
}

/* NETDEV_CMD_DEV_GET - dump */
void netdev_dev_get_list_free(struct netdev_dev_get_list *rsp)
{
	struct netdev_dev_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp);
	}
}

struct netdev_dev_get_list *netdev_dev_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct netdev_dev_get_list);
	yds.cb = netdev_dev_get_rsp_parse;
	yds.rsp_cmd = NETDEV_CMD_DEV_GET;
	yds.rsp_policy = &netdev_dev_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, NETDEV_CMD_DEV_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	netdev_dev_get_list_free(yds.first);
	return NULL;
}

/* NETDEV_CMD_DEV_GET - notify */
void netdev_dev_get_ntf_free(struct netdev_dev_get_ntf *rsp)
{
	free(rsp);
}

static const struct ynl_ntf_info netdev_ntf_info[] =  {
	[NETDEV_CMD_DEV_ADD_NTF] =  {
		.alloc_sz	= sizeof(struct netdev_dev_get_ntf),
		.cb		= netdev_dev_get_rsp_parse,
		.policy		= &netdev_dev_nest,
		.free		= (void *)netdev_dev_get_ntf_free,
	},
	[NETDEV_CMD_DEV_DEL_NTF] =  {
		.alloc_sz	= sizeof(struct netdev_dev_get_ntf),
		.cb		= netdev_dev_get_rsp_parse,
		.policy		= &netdev_dev_nest,
		.free		= (void *)netdev_dev_get_ntf_free,
	},
	[NETDEV_CMD_DEV_CHANGE_NTF] =  {
		.alloc_sz	= sizeof(struct netdev_dev_get_ntf),
		.cb		= netdev_dev_get_rsp_parse,
		.policy		= &netdev_dev_nest,
		.free		= (void *)netdev_dev_get_ntf_free,
	},
};

const struct ynl_family ynl_netdev_family =  {
	.name		= "netdev",
	.ntf_info	= netdev_ntf_info,
	.ntf_info_size	= MNL_ARRAY_SIZE(netdev_ntf_info),
};
