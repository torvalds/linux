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
	[NETDEV_CMD_PAGE_POOL_GET] = "page-pool-get",
	[NETDEV_CMD_PAGE_POOL_ADD_NTF] = "page-pool-add-ntf",
	[NETDEV_CMD_PAGE_POOL_DEL_NTF] = "page-pool-del-ntf",
	[NETDEV_CMD_PAGE_POOL_CHANGE_NTF] = "page-pool-change-ntf",
	[NETDEV_CMD_PAGE_POOL_STATS_GET] = "page-pool-stats-get",
	[NETDEV_CMD_QUEUE_GET] = "queue-get",
	[NETDEV_CMD_NAPI_GET] = "napi-get",
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

static const char * const netdev_xsk_flags_strmap[] = {
	[0] = "tx-timestamp",
	[1] = "tx-checksum",
};

const char *netdev_xsk_flags_str(enum netdev_xsk_flags value)
{
	value = ffs(value) - 1;
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(netdev_xsk_flags_strmap))
		return NULL;
	return netdev_xsk_flags_strmap[value];
}

static const char * const netdev_queue_type_strmap[] = {
	[0] = "rx",
	[1] = "tx",
};

const char *netdev_queue_type_str(enum netdev_queue_type value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(netdev_queue_type_strmap))
		return NULL;
	return netdev_queue_type_strmap[value];
}

/* Policies */
struct ynl_policy_attr netdev_page_pool_info_policy[NETDEV_A_PAGE_POOL_MAX + 1] = {
	[NETDEV_A_PAGE_POOL_ID] = { .name = "id", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
};

struct ynl_policy_nest netdev_page_pool_info_nest = {
	.max_attr = NETDEV_A_PAGE_POOL_MAX,
	.table = netdev_page_pool_info_policy,
};

struct ynl_policy_attr netdev_dev_policy[NETDEV_A_DEV_MAX + 1] = {
	[NETDEV_A_DEV_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
	[NETDEV_A_DEV_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[NETDEV_A_DEV_XDP_FEATURES] = { .name = "xdp-features", .type = YNL_PT_U64, },
	[NETDEV_A_DEV_XDP_ZC_MAX_SEGS] = { .name = "xdp-zc-max-segs", .type = YNL_PT_U32, },
	[NETDEV_A_DEV_XDP_RX_METADATA_FEATURES] = { .name = "xdp-rx-metadata-features", .type = YNL_PT_U64, },
	[NETDEV_A_DEV_XSK_FEATURES] = { .name = "xsk-features", .type = YNL_PT_U64, },
};

struct ynl_policy_nest netdev_dev_nest = {
	.max_attr = NETDEV_A_DEV_MAX,
	.table = netdev_dev_policy,
};

struct ynl_policy_attr netdev_page_pool_policy[NETDEV_A_PAGE_POOL_MAX + 1] = {
	[NETDEV_A_PAGE_POOL_ID] = { .name = "id", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
	[NETDEV_A_PAGE_POOL_NAPI_ID] = { .name = "napi-id", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_INFLIGHT] = { .name = "inflight", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_INFLIGHT_MEM] = { .name = "inflight-mem", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_DETACH_TIME] = { .name = "detach-time", .type = YNL_PT_UINT, },
};

struct ynl_policy_nest netdev_page_pool_nest = {
	.max_attr = NETDEV_A_PAGE_POOL_MAX,
	.table = netdev_page_pool_policy,
};

struct ynl_policy_attr netdev_page_pool_stats_policy[NETDEV_A_PAGE_POOL_STATS_MAX + 1] = {
	[NETDEV_A_PAGE_POOL_STATS_INFO] = { .name = "info", .type = YNL_PT_NEST, .nest = &netdev_page_pool_info_nest, },
	[NETDEV_A_PAGE_POOL_STATS_ALLOC_FAST] = { .name = "alloc-fast", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_ALLOC_SLOW] = { .name = "alloc-slow", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_ALLOC_SLOW_HIGH_ORDER] = { .name = "alloc-slow-high-order", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_ALLOC_EMPTY] = { .name = "alloc-empty", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_ALLOC_REFILL] = { .name = "alloc-refill", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_ALLOC_WAIVE] = { .name = "alloc-waive", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_RECYCLE_CACHED] = { .name = "recycle-cached", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_RECYCLE_CACHE_FULL] = { .name = "recycle-cache-full", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_RECYCLE_RING] = { .name = "recycle-ring", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_RECYCLE_RING_FULL] = { .name = "recycle-ring-full", .type = YNL_PT_UINT, },
	[NETDEV_A_PAGE_POOL_STATS_RECYCLE_RELEASED_REFCNT] = { .name = "recycle-released-refcnt", .type = YNL_PT_UINT, },
};

struct ynl_policy_nest netdev_page_pool_stats_nest = {
	.max_attr = NETDEV_A_PAGE_POOL_STATS_MAX,
	.table = netdev_page_pool_stats_policy,
};

struct ynl_policy_attr netdev_queue_policy[NETDEV_A_QUEUE_MAX + 1] = {
	[NETDEV_A_QUEUE_ID] = { .name = "id", .type = YNL_PT_U32, },
	[NETDEV_A_QUEUE_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
	[NETDEV_A_QUEUE_TYPE] = { .name = "type", .type = YNL_PT_U32, },
	[NETDEV_A_QUEUE_NAPI_ID] = { .name = "napi-id", .type = YNL_PT_U32, },
};

struct ynl_policy_nest netdev_queue_nest = {
	.max_attr = NETDEV_A_QUEUE_MAX,
	.table = netdev_queue_policy,
};

struct ynl_policy_attr netdev_napi_policy[NETDEV_A_NAPI_MAX + 1] = {
	[NETDEV_A_NAPI_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
	[NETDEV_A_NAPI_ID] = { .name = "id", .type = YNL_PT_U32, },
	[NETDEV_A_NAPI_IRQ] = { .name = "irq", .type = YNL_PT_U32, },
	[NETDEV_A_NAPI_PID] = { .name = "pid", .type = YNL_PT_U32, },
};

struct ynl_policy_nest netdev_napi_nest = {
	.max_attr = NETDEV_A_NAPI_MAX,
	.table = netdev_napi_policy,
};

/* Common nested types */
void netdev_page_pool_info_free(struct netdev_page_pool_info *obj)
{
}

int netdev_page_pool_info_put(struct nlmsghdr *nlh, unsigned int attr_type,
			      struct netdev_page_pool_info *obj)
{
	struct nlattr *nest;

	nest = mnl_attr_nest_start(nlh, attr_type);
	if (obj->_present.id)
		mnl_attr_put_uint(nlh, NETDEV_A_PAGE_POOL_ID, obj->id);
	if (obj->_present.ifindex)
		mnl_attr_put_u32(nlh, NETDEV_A_PAGE_POOL_IFINDEX, obj->ifindex);
	mnl_attr_nest_end(nlh, nest);

	return 0;
}

int netdev_page_pool_info_parse(struct ynl_parse_arg *yarg,
				const struct nlattr *nested)
{
	struct netdev_page_pool_info *dst = yarg->data;
	const struct nlattr *attr;

	mnl_attr_for_each_nested(attr, nested) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NETDEV_A_PAGE_POOL_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.id = 1;
			dst->id = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_IFINDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ifindex = 1;
			dst->ifindex = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_NAPI_IRQ) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.irq = 1;
			dst->irq = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_NAPI_PID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.pid = 1;
			dst->pid = mnl_attr_get_u32(attr);
		}
	}

	return 0;
}

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
		} else if (type == NETDEV_A_DEV_XSK_FEATURES) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.xsk_features = 1;
			dst->xsk_features = mnl_attr_get_u64(attr);
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

/* ============== NETDEV_CMD_PAGE_POOL_GET ============== */
/* NETDEV_CMD_PAGE_POOL_GET - do */
void netdev_page_pool_get_req_free(struct netdev_page_pool_get_req *req)
{
	free(req);
}

void netdev_page_pool_get_rsp_free(struct netdev_page_pool_get_rsp *rsp)
{
	free(rsp);
}

int netdev_page_pool_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct netdev_page_pool_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NETDEV_A_PAGE_POOL_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.id = 1;
			dst->id = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_IFINDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ifindex = 1;
			dst->ifindex = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_PAGE_POOL_NAPI_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.napi_id = 1;
			dst->napi_id = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_INFLIGHT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.inflight = 1;
			dst->inflight = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_INFLIGHT_MEM) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.inflight_mem = 1;
			dst->inflight_mem = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_DETACH_TIME) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.detach_time = 1;
			dst->detach_time = mnl_attr_get_uint(attr);
		}
	}

	return MNL_CB_OK;
}

struct netdev_page_pool_get_rsp *
netdev_page_pool_get(struct ynl_sock *ys, struct netdev_page_pool_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct netdev_page_pool_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, NETDEV_CMD_PAGE_POOL_GET, 1);
	ys->req_policy = &netdev_page_pool_nest;
	yrs.yarg.rsp_policy = &netdev_page_pool_nest;

	if (req->_present.id)
		mnl_attr_put_uint(nlh, NETDEV_A_PAGE_POOL_ID, req->id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = netdev_page_pool_get_rsp_parse;
	yrs.rsp_cmd = NETDEV_CMD_PAGE_POOL_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	netdev_page_pool_get_rsp_free(rsp);
	return NULL;
}

/* NETDEV_CMD_PAGE_POOL_GET - dump */
void netdev_page_pool_get_list_free(struct netdev_page_pool_get_list *rsp)
{
	struct netdev_page_pool_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp);
	}
}

struct netdev_page_pool_get_list *
netdev_page_pool_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct netdev_page_pool_get_list);
	yds.cb = netdev_page_pool_get_rsp_parse;
	yds.rsp_cmd = NETDEV_CMD_PAGE_POOL_GET;
	yds.rsp_policy = &netdev_page_pool_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, NETDEV_CMD_PAGE_POOL_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	netdev_page_pool_get_list_free(yds.first);
	return NULL;
}

/* NETDEV_CMD_PAGE_POOL_GET - notify */
void netdev_page_pool_get_ntf_free(struct netdev_page_pool_get_ntf *rsp)
{
	free(rsp);
}

/* ============== NETDEV_CMD_PAGE_POOL_STATS_GET ============== */
/* NETDEV_CMD_PAGE_POOL_STATS_GET - do */
void
netdev_page_pool_stats_get_req_free(struct netdev_page_pool_stats_get_req *req)
{
	netdev_page_pool_info_free(&req->info);
	free(req);
}

void
netdev_page_pool_stats_get_rsp_free(struct netdev_page_pool_stats_get_rsp *rsp)
{
	netdev_page_pool_info_free(&rsp->info);
	free(rsp);
}

int netdev_page_pool_stats_get_rsp_parse(const struct nlmsghdr *nlh,
					 void *data)
{
	struct netdev_page_pool_stats_get_rsp *dst;
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct ynl_parse_arg parg;

	dst = yarg->data;
	parg.ys = yarg->ys;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NETDEV_A_PAGE_POOL_STATS_INFO) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.info = 1;

			parg.rsp_policy = &netdev_page_pool_info_nest;
			parg.data = &dst->info;
			if (netdev_page_pool_info_parse(&parg, attr))
				return MNL_CB_ERROR;
		} else if (type == NETDEV_A_PAGE_POOL_STATS_ALLOC_FAST) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.alloc_fast = 1;
			dst->alloc_fast = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_ALLOC_SLOW) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.alloc_slow = 1;
			dst->alloc_slow = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_ALLOC_SLOW_HIGH_ORDER) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.alloc_slow_high_order = 1;
			dst->alloc_slow_high_order = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_ALLOC_EMPTY) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.alloc_empty = 1;
			dst->alloc_empty = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_ALLOC_REFILL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.alloc_refill = 1;
			dst->alloc_refill = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_ALLOC_WAIVE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.alloc_waive = 1;
			dst->alloc_waive = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_RECYCLE_CACHED) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.recycle_cached = 1;
			dst->recycle_cached = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_RECYCLE_CACHE_FULL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.recycle_cache_full = 1;
			dst->recycle_cache_full = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_RECYCLE_RING) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.recycle_ring = 1;
			dst->recycle_ring = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_RECYCLE_RING_FULL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.recycle_ring_full = 1;
			dst->recycle_ring_full = mnl_attr_get_uint(attr);
		} else if (type == NETDEV_A_PAGE_POOL_STATS_RECYCLE_RELEASED_REFCNT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.recycle_released_refcnt = 1;
			dst->recycle_released_refcnt = mnl_attr_get_uint(attr);
		}
	}

	return MNL_CB_OK;
}

struct netdev_page_pool_stats_get_rsp *
netdev_page_pool_stats_get(struct ynl_sock *ys,
			   struct netdev_page_pool_stats_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct netdev_page_pool_stats_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, NETDEV_CMD_PAGE_POOL_STATS_GET, 1);
	ys->req_policy = &netdev_page_pool_stats_nest;
	yrs.yarg.rsp_policy = &netdev_page_pool_stats_nest;

	if (req->_present.info)
		netdev_page_pool_info_put(nlh, NETDEV_A_PAGE_POOL_STATS_INFO, &req->info);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = netdev_page_pool_stats_get_rsp_parse;
	yrs.rsp_cmd = NETDEV_CMD_PAGE_POOL_STATS_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	netdev_page_pool_stats_get_rsp_free(rsp);
	return NULL;
}

/* NETDEV_CMD_PAGE_POOL_STATS_GET - dump */
void
netdev_page_pool_stats_get_list_free(struct netdev_page_pool_stats_get_list *rsp)
{
	struct netdev_page_pool_stats_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		netdev_page_pool_info_free(&rsp->obj.info);
		free(rsp);
	}
}

struct netdev_page_pool_stats_get_list *
netdev_page_pool_stats_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct netdev_page_pool_stats_get_list);
	yds.cb = netdev_page_pool_stats_get_rsp_parse;
	yds.rsp_cmd = NETDEV_CMD_PAGE_POOL_STATS_GET;
	yds.rsp_policy = &netdev_page_pool_stats_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, NETDEV_CMD_PAGE_POOL_STATS_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	netdev_page_pool_stats_get_list_free(yds.first);
	return NULL;
}

/* ============== NETDEV_CMD_QUEUE_GET ============== */
/* NETDEV_CMD_QUEUE_GET - do */
void netdev_queue_get_req_free(struct netdev_queue_get_req *req)
{
	free(req);
}

void netdev_queue_get_rsp_free(struct netdev_queue_get_rsp *rsp)
{
	free(rsp);
}

int netdev_queue_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct netdev_queue_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NETDEV_A_QUEUE_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.id = 1;
			dst->id = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_QUEUE_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.type = 1;
			dst->type = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_QUEUE_NAPI_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.napi_id = 1;
			dst->napi_id = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_QUEUE_IFINDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ifindex = 1;
			dst->ifindex = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct netdev_queue_get_rsp *
netdev_queue_get(struct ynl_sock *ys, struct netdev_queue_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct netdev_queue_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, NETDEV_CMD_QUEUE_GET, 1);
	ys->req_policy = &netdev_queue_nest;
	yrs.yarg.rsp_policy = &netdev_queue_nest;

	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, NETDEV_A_QUEUE_IFINDEX, req->ifindex);
	if (req->_present.type)
		mnl_attr_put_u32(nlh, NETDEV_A_QUEUE_TYPE, req->type);
	if (req->_present.id)
		mnl_attr_put_u32(nlh, NETDEV_A_QUEUE_ID, req->id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = netdev_queue_get_rsp_parse;
	yrs.rsp_cmd = NETDEV_CMD_QUEUE_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	netdev_queue_get_rsp_free(rsp);
	return NULL;
}

/* NETDEV_CMD_QUEUE_GET - dump */
void netdev_queue_get_list_free(struct netdev_queue_get_list *rsp)
{
	struct netdev_queue_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp);
	}
}

struct netdev_queue_get_list *
netdev_queue_get_dump(struct ynl_sock *ys,
		      struct netdev_queue_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct netdev_queue_get_list);
	yds.cb = netdev_queue_get_rsp_parse;
	yds.rsp_cmd = NETDEV_CMD_QUEUE_GET;
	yds.rsp_policy = &netdev_queue_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, NETDEV_CMD_QUEUE_GET, 1);
	ys->req_policy = &netdev_queue_nest;

	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, NETDEV_A_QUEUE_IFINDEX, req->ifindex);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	netdev_queue_get_list_free(yds.first);
	return NULL;
}

/* ============== NETDEV_CMD_NAPI_GET ============== */
/* NETDEV_CMD_NAPI_GET - do */
void netdev_napi_get_req_free(struct netdev_napi_get_req *req)
{
	free(req);
}

void netdev_napi_get_rsp_free(struct netdev_napi_get_rsp *rsp)
{
	free(rsp);
}

int netdev_napi_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct netdev_napi_get_rsp *dst;
	const struct nlattr *attr;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NETDEV_A_NAPI_ID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.id = 1;
			dst->id = mnl_attr_get_u32(attr);
		} else if (type == NETDEV_A_NAPI_IFINDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ifindex = 1;
			dst->ifindex = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct netdev_napi_get_rsp *
netdev_napi_get(struct ynl_sock *ys, struct netdev_napi_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct netdev_napi_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, NETDEV_CMD_NAPI_GET, 1);
	ys->req_policy = &netdev_napi_nest;
	yrs.yarg.rsp_policy = &netdev_napi_nest;

	if (req->_present.id)
		mnl_attr_put_u32(nlh, NETDEV_A_NAPI_ID, req->id);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = netdev_napi_get_rsp_parse;
	yrs.rsp_cmd = NETDEV_CMD_NAPI_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	netdev_napi_get_rsp_free(rsp);
	return NULL;
}

/* NETDEV_CMD_NAPI_GET - dump */
void netdev_napi_get_list_free(struct netdev_napi_get_list *rsp)
{
	struct netdev_napi_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp);
	}
}

struct netdev_napi_get_list *
netdev_napi_get_dump(struct ynl_sock *ys, struct netdev_napi_get_req_dump *req)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct netdev_napi_get_list);
	yds.cb = netdev_napi_get_rsp_parse;
	yds.rsp_cmd = NETDEV_CMD_NAPI_GET;
	yds.rsp_policy = &netdev_napi_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, NETDEV_CMD_NAPI_GET, 1);
	ys->req_policy = &netdev_napi_nest;

	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, NETDEV_A_NAPI_IFINDEX, req->ifindex);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	netdev_napi_get_list_free(yds.first);
	return NULL;
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
	[NETDEV_CMD_PAGE_POOL_ADD_NTF] =  {
		.alloc_sz	= sizeof(struct netdev_page_pool_get_ntf),
		.cb		= netdev_page_pool_get_rsp_parse,
		.policy		= &netdev_page_pool_nest,
		.free		= (void *)netdev_page_pool_get_ntf_free,
	},
	[NETDEV_CMD_PAGE_POOL_DEL_NTF] =  {
		.alloc_sz	= sizeof(struct netdev_page_pool_get_ntf),
		.cb		= netdev_page_pool_get_rsp_parse,
		.policy		= &netdev_page_pool_nest,
		.free		= (void *)netdev_page_pool_get_ntf_free,
	},
	[NETDEV_CMD_PAGE_POOL_CHANGE_NTF] =  {
		.alloc_sz	= sizeof(struct netdev_page_pool_get_ntf),
		.cb		= netdev_page_pool_get_rsp_parse,
		.policy		= &netdev_page_pool_nest,
		.free		= (void *)netdev_page_pool_get_ntf_free,
	},
};

const struct ynl_family ynl_netdev_family =  {
	.name		= "netdev",
	.ntf_info	= netdev_ntf_info,
	.ntf_info_size	= MNL_ARRAY_SIZE(netdev_ntf_info),
};
