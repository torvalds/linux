/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/netdev.yaml */
/* YNL-GEN user header */

#ifndef _LINUX_NETDEV_GEN_H
#define _LINUX_NETDEV_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/netdev.h>

struct ynl_sock;

extern const struct ynl_family ynl_netdev_family;

/* Enums */
const char *netdev_op_str(int op);
const char *netdev_xdp_act_str(enum netdev_xdp_act value);
const char *netdev_xdp_rx_metadata_str(enum netdev_xdp_rx_metadata value);
const char *netdev_xsk_flags_str(enum netdev_xsk_flags value);

/* Common nested types */
struct netdev_page_pool_info {
	struct {
		__u32 id:1;
		__u32 ifindex:1;
	} _present;

	__u64 id;
	__u32 ifindex;
};

/* ============== NETDEV_CMD_DEV_GET ============== */
/* NETDEV_CMD_DEV_GET - do */
struct netdev_dev_get_req {
	struct {
		__u32 ifindex:1;
	} _present;

	__u32 ifindex;
};

static inline struct netdev_dev_get_req *netdev_dev_get_req_alloc(void)
{
	return calloc(1, sizeof(struct netdev_dev_get_req));
}
void netdev_dev_get_req_free(struct netdev_dev_get_req *req);

static inline void
netdev_dev_get_req_set_ifindex(struct netdev_dev_get_req *req, __u32 ifindex)
{
	req->_present.ifindex = 1;
	req->ifindex = ifindex;
}

struct netdev_dev_get_rsp {
	struct {
		__u32 ifindex:1;
		__u32 xdp_features:1;
		__u32 xdp_zc_max_segs:1;
		__u32 xdp_rx_metadata_features:1;
		__u32 xsk_features:1;
	} _present;

	__u32 ifindex;
	__u64 xdp_features;
	__u32 xdp_zc_max_segs;
	__u64 xdp_rx_metadata_features;
	__u64 xsk_features;
};

void netdev_dev_get_rsp_free(struct netdev_dev_get_rsp *rsp);

/*
 * Get / dump information about a netdev.
 */
struct netdev_dev_get_rsp *
netdev_dev_get(struct ynl_sock *ys, struct netdev_dev_get_req *req);

/* NETDEV_CMD_DEV_GET - dump */
struct netdev_dev_get_list {
	struct netdev_dev_get_list *next;
	struct netdev_dev_get_rsp obj __attribute__((aligned(8)));
};

void netdev_dev_get_list_free(struct netdev_dev_get_list *rsp);

struct netdev_dev_get_list *netdev_dev_get_dump(struct ynl_sock *ys);

/* NETDEV_CMD_DEV_GET - notify */
struct netdev_dev_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct netdev_dev_get_ntf *ntf);
	struct netdev_dev_get_rsp obj __attribute__((aligned(8)));
};

void netdev_dev_get_ntf_free(struct netdev_dev_get_ntf *rsp);

/* ============== NETDEV_CMD_PAGE_POOL_GET ============== */
/* NETDEV_CMD_PAGE_POOL_GET - do */
struct netdev_page_pool_get_req {
	struct {
		__u32 id:1;
	} _present;

	__u64 id;
};

static inline struct netdev_page_pool_get_req *
netdev_page_pool_get_req_alloc(void)
{
	return calloc(1, sizeof(struct netdev_page_pool_get_req));
}
void netdev_page_pool_get_req_free(struct netdev_page_pool_get_req *req);

static inline void
netdev_page_pool_get_req_set_id(struct netdev_page_pool_get_req *req, __u64 id)
{
	req->_present.id = 1;
	req->id = id;
}

struct netdev_page_pool_get_rsp {
	struct {
		__u32 id:1;
		__u32 ifindex:1;
		__u32 napi_id:1;
		__u32 inflight:1;
		__u32 inflight_mem:1;
		__u32 detach_time:1;
	} _present;

	__u64 id;
	__u32 ifindex;
	__u64 napi_id;
	__u64 inflight;
	__u64 inflight_mem;
	__u64 detach_time;
};

void netdev_page_pool_get_rsp_free(struct netdev_page_pool_get_rsp *rsp);

/*
 * Get / dump information about Page Pools.
(Only Page Pools associated with a net_device can be listed.)

 */
struct netdev_page_pool_get_rsp *
netdev_page_pool_get(struct ynl_sock *ys, struct netdev_page_pool_get_req *req);

/* NETDEV_CMD_PAGE_POOL_GET - dump */
struct netdev_page_pool_get_list {
	struct netdev_page_pool_get_list *next;
	struct netdev_page_pool_get_rsp obj __attribute__((aligned(8)));
};

void netdev_page_pool_get_list_free(struct netdev_page_pool_get_list *rsp);

struct netdev_page_pool_get_list *
netdev_page_pool_get_dump(struct ynl_sock *ys);

/* NETDEV_CMD_PAGE_POOL_GET - notify */
struct netdev_page_pool_get_ntf {
	__u16 family;
	__u8 cmd;
	struct ynl_ntf_base_type *next;
	void (*free)(struct netdev_page_pool_get_ntf *ntf);
	struct netdev_page_pool_get_rsp obj __attribute__((aligned(8)));
};

void netdev_page_pool_get_ntf_free(struct netdev_page_pool_get_ntf *rsp);

/* ============== NETDEV_CMD_PAGE_POOL_STATS_GET ============== */
/* NETDEV_CMD_PAGE_POOL_STATS_GET - do */
struct netdev_page_pool_stats_get_req {
	struct {
		__u32 info:1;
	} _present;

	struct netdev_page_pool_info info;
};

static inline struct netdev_page_pool_stats_get_req *
netdev_page_pool_stats_get_req_alloc(void)
{
	return calloc(1, sizeof(struct netdev_page_pool_stats_get_req));
}
void
netdev_page_pool_stats_get_req_free(struct netdev_page_pool_stats_get_req *req);

static inline void
netdev_page_pool_stats_get_req_set_info_id(struct netdev_page_pool_stats_get_req *req,
					   __u64 id)
{
	req->_present.info = 1;
	req->info._present.id = 1;
	req->info.id = id;
}
static inline void
netdev_page_pool_stats_get_req_set_info_ifindex(struct netdev_page_pool_stats_get_req *req,
						__u32 ifindex)
{
	req->_present.info = 1;
	req->info._present.ifindex = 1;
	req->info.ifindex = ifindex;
}

struct netdev_page_pool_stats_get_rsp {
	struct {
		__u32 info:1;
		__u32 alloc_fast:1;
		__u32 alloc_slow:1;
		__u32 alloc_slow_high_order:1;
		__u32 alloc_empty:1;
		__u32 alloc_refill:1;
		__u32 alloc_waive:1;
		__u32 recycle_cached:1;
		__u32 recycle_cache_full:1;
		__u32 recycle_ring:1;
		__u32 recycle_ring_full:1;
		__u32 recycle_released_refcnt:1;
	} _present;

	struct netdev_page_pool_info info;
	__u64 alloc_fast;
	__u64 alloc_slow;
	__u64 alloc_slow_high_order;
	__u64 alloc_empty;
	__u64 alloc_refill;
	__u64 alloc_waive;
	__u64 recycle_cached;
	__u64 recycle_cache_full;
	__u64 recycle_ring;
	__u64 recycle_ring_full;
	__u64 recycle_released_refcnt;
};

void
netdev_page_pool_stats_get_rsp_free(struct netdev_page_pool_stats_get_rsp *rsp);

/*
 * Get page pool statistics.
 */
struct netdev_page_pool_stats_get_rsp *
netdev_page_pool_stats_get(struct ynl_sock *ys,
			   struct netdev_page_pool_stats_get_req *req);

/* NETDEV_CMD_PAGE_POOL_STATS_GET - dump */
struct netdev_page_pool_stats_get_list {
	struct netdev_page_pool_stats_get_list *next;
	struct netdev_page_pool_stats_get_rsp obj __attribute__((aligned(8)));
};

void
netdev_page_pool_stats_get_list_free(struct netdev_page_pool_stats_get_list *rsp);

struct netdev_page_pool_stats_get_list *
netdev_page_pool_stats_get_dump(struct ynl_sock *ys);

#endif /* _LINUX_NETDEV_GEN_H */
