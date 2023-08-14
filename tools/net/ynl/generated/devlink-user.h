/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/devlink.yaml */
/* YNL-GEN user header */

#ifndef _LINUX_DEVLINK_GEN_H
#define _LINUX_DEVLINK_GEN_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/devlink.h>

struct ynl_sock;

extern const struct ynl_family ynl_devlink_family;

/* Enums */
const char *devlink_op_str(int op);
const char *devlink_sb_pool_type_str(enum devlink_sb_pool_type value);

/* Common nested types */
struct devlink_dl_info_version {
	struct {
		__u32 info_version_name_len;
		__u32 info_version_value_len;
	} _present;

	char *info_version_name;
	char *info_version_value;
};

struct devlink_dl_reload_stats_entry {
	struct {
		__u32 reload_stats_limit:1;
		__u32 reload_stats_value:1;
	} _present;

	__u8 reload_stats_limit;
	__u32 reload_stats_value;
};

struct devlink_dl_reload_act_stats {
	unsigned int n_reload_stats_entry;
	struct devlink_dl_reload_stats_entry *reload_stats_entry;
};

struct devlink_dl_reload_act_info {
	struct {
		__u32 reload_action:1;
	} _present;

	__u8 reload_action;
	unsigned int n_reload_action_stats;
	struct devlink_dl_reload_act_stats *reload_action_stats;
};

struct devlink_dl_reload_stats {
	unsigned int n_reload_action_info;
	struct devlink_dl_reload_act_info *reload_action_info;
};

struct devlink_dl_dev_stats {
	struct {
		__u32 reload_stats:1;
		__u32 remote_reload_stats:1;
	} _present;

	struct devlink_dl_reload_stats reload_stats;
	struct devlink_dl_reload_stats remote_reload_stats;
};

/* ============== DEVLINK_CMD_GET ============== */
/* DEVLINK_CMD_GET - do */
struct devlink_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_get_req *devlink_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_get_req));
}
void devlink_get_req_free(struct devlink_get_req *req);

static inline void
devlink_get_req_set_bus_name(struct devlink_get_req *req, const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_get_req_set_dev_name(struct devlink_get_req *req, const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 reload_failed:1;
		__u32 reload_action:1;
		__u32 dev_stats:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u8 reload_failed;
	__u8 reload_action;
	struct devlink_dl_dev_stats dev_stats;
};

void devlink_get_rsp_free(struct devlink_get_rsp *rsp);

/*
 * Get devlink instances.
 */
struct devlink_get_rsp *
devlink_get(struct ynl_sock *ys, struct devlink_get_req *req);

/* DEVLINK_CMD_GET - dump */
struct devlink_get_list {
	struct devlink_get_list *next;
	struct devlink_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_get_list_free(struct devlink_get_list *rsp);

struct devlink_get_list *devlink_get_dump(struct ynl_sock *ys);

/* ============== DEVLINK_CMD_PORT_GET ============== */
/* DEVLINK_CMD_PORT_GET - do */
struct devlink_port_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_port_get_req *devlink_port_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_get_req));
}
void devlink_port_get_req_free(struct devlink_port_get_req *req);

static inline void
devlink_port_get_req_set_bus_name(struct devlink_port_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_get_req_set_dev_name(struct devlink_port_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_port_get_req_set_port_index(struct devlink_port_get_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

struct devlink_port_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

void devlink_port_get_rsp_free(struct devlink_port_get_rsp *rsp);

/*
 * Get devlink port instances.
 */
struct devlink_port_get_rsp *
devlink_port_get(struct ynl_sock *ys, struct devlink_port_get_req *req);

/* DEVLINK_CMD_PORT_GET - dump */
struct devlink_port_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_port_get_req_dump *
devlink_port_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_port_get_req_dump));
}
void devlink_port_get_req_dump_free(struct devlink_port_get_req_dump *req);

static inline void
devlink_port_get_req_dump_set_bus_name(struct devlink_port_get_req_dump *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_port_get_req_dump_set_dev_name(struct devlink_port_get_req_dump *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_port_get_rsp_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

struct devlink_port_get_rsp_list {
	struct devlink_port_get_rsp_list *next;
	struct devlink_port_get_rsp_dump obj __attribute__ ((aligned (8)));
};

void devlink_port_get_rsp_list_free(struct devlink_port_get_rsp_list *rsp);

struct devlink_port_get_rsp_list *
devlink_port_get_dump(struct ynl_sock *ys,
		      struct devlink_port_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_GET ============== */
/* DEVLINK_CMD_SB_GET - do */
struct devlink_sb_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
};

static inline struct devlink_sb_get_req *devlink_sb_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_get_req));
}
void devlink_sb_get_req_free(struct devlink_sb_get_req *req);

static inline void
devlink_sb_get_req_set_bus_name(struct devlink_sb_get_req *req,
				const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_get_req_set_dev_name(struct devlink_sb_get_req *req,
				const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_get_req_set_sb_index(struct devlink_sb_get_req *req, __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}

struct devlink_sb_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
};

void devlink_sb_get_rsp_free(struct devlink_sb_get_rsp *rsp);

/*
 * Get shared buffer instances.
 */
struct devlink_sb_get_rsp *
devlink_sb_get(struct ynl_sock *ys, struct devlink_sb_get_req *req);

/* DEVLINK_CMD_SB_GET - dump */
struct devlink_sb_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_get_req_dump *
devlink_sb_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_get_req_dump));
}
void devlink_sb_get_req_dump_free(struct devlink_sb_get_req_dump *req);

static inline void
devlink_sb_get_req_dump_set_bus_name(struct devlink_sb_get_req_dump *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_get_req_dump_set_dev_name(struct devlink_sb_get_req_dump *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_get_list {
	struct devlink_sb_get_list *next;
	struct devlink_sb_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_sb_get_list_free(struct devlink_sb_get_list *rsp);

struct devlink_sb_get_list *
devlink_sb_get_dump(struct ynl_sock *ys, struct devlink_sb_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_POOL_GET ============== */
/* DEVLINK_CMD_SB_POOL_GET - do */
struct devlink_sb_pool_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
	__u16 sb_pool_index;
};

static inline struct devlink_sb_pool_get_req *
devlink_sb_pool_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_pool_get_req));
}
void devlink_sb_pool_get_req_free(struct devlink_sb_pool_get_req *req);

static inline void
devlink_sb_pool_get_req_set_bus_name(struct devlink_sb_pool_get_req *req,
				     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_pool_get_req_set_dev_name(struct devlink_sb_pool_get_req *req,
				     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_pool_get_req_set_sb_index(struct devlink_sb_pool_get_req *req,
				     __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_pool_get_req_set_sb_pool_index(struct devlink_sb_pool_get_req *req,
					  __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}

struct devlink_sb_pool_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 sb_index;
	__u16 sb_pool_index;
};

void devlink_sb_pool_get_rsp_free(struct devlink_sb_pool_get_rsp *rsp);

/*
 * Get shared buffer pool instances.
 */
struct devlink_sb_pool_get_rsp *
devlink_sb_pool_get(struct ynl_sock *ys, struct devlink_sb_pool_get_req *req);

/* DEVLINK_CMD_SB_POOL_GET - dump */
struct devlink_sb_pool_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_pool_get_req_dump *
devlink_sb_pool_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_pool_get_req_dump));
}
void
devlink_sb_pool_get_req_dump_free(struct devlink_sb_pool_get_req_dump *req);

static inline void
devlink_sb_pool_get_req_dump_set_bus_name(struct devlink_sb_pool_get_req_dump *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_pool_get_req_dump_set_dev_name(struct devlink_sb_pool_get_req_dump *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_pool_get_list {
	struct devlink_sb_pool_get_list *next;
	struct devlink_sb_pool_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_sb_pool_get_list_free(struct devlink_sb_pool_get_list *rsp);

struct devlink_sb_pool_get_list *
devlink_sb_pool_get_dump(struct ynl_sock *ys,
			 struct devlink_sb_pool_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_PORT_POOL_GET ============== */
/* DEVLINK_CMD_SB_PORT_POOL_GET - do */
struct devlink_sb_port_pool_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	__u16 sb_pool_index;
};

static inline struct devlink_sb_port_pool_get_req *
devlink_sb_port_pool_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_port_pool_get_req));
}
void
devlink_sb_port_pool_get_req_free(struct devlink_sb_port_pool_get_req *req);

static inline void
devlink_sb_port_pool_get_req_set_bus_name(struct devlink_sb_port_pool_get_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_port_pool_get_req_set_dev_name(struct devlink_sb_port_pool_get_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_port_pool_get_req_set_port_index(struct devlink_sb_port_pool_get_req *req,
					    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_sb_port_pool_get_req_set_sb_index(struct devlink_sb_port_pool_get_req *req,
					  __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_port_pool_get_req_set_sb_pool_index(struct devlink_sb_port_pool_get_req *req,
					       __u16 sb_pool_index)
{
	req->_present.sb_pool_index = 1;
	req->sb_pool_index = sb_pool_index;
}

struct devlink_sb_port_pool_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	__u16 sb_pool_index;
};

void
devlink_sb_port_pool_get_rsp_free(struct devlink_sb_port_pool_get_rsp *rsp);

/*
 * Get shared buffer port-pool combinations and threshold.
 */
struct devlink_sb_port_pool_get_rsp *
devlink_sb_port_pool_get(struct ynl_sock *ys,
			 struct devlink_sb_port_pool_get_req *req);

/* DEVLINK_CMD_SB_PORT_POOL_GET - dump */
struct devlink_sb_port_pool_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_port_pool_get_req_dump *
devlink_sb_port_pool_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_port_pool_get_req_dump));
}
void
devlink_sb_port_pool_get_req_dump_free(struct devlink_sb_port_pool_get_req_dump *req);

static inline void
devlink_sb_port_pool_get_req_dump_set_bus_name(struct devlink_sb_port_pool_get_req_dump *req,
					       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_port_pool_get_req_dump_set_dev_name(struct devlink_sb_port_pool_get_req_dump *req,
					       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_port_pool_get_list {
	struct devlink_sb_port_pool_get_list *next;
	struct devlink_sb_port_pool_get_rsp obj __attribute__ ((aligned (8)));
};

void
devlink_sb_port_pool_get_list_free(struct devlink_sb_port_pool_get_list *rsp);

struct devlink_sb_port_pool_get_list *
devlink_sb_port_pool_get_dump(struct ynl_sock *ys,
			      struct devlink_sb_port_pool_get_req_dump *req);

/* ============== DEVLINK_CMD_SB_TC_POOL_BIND_GET ============== */
/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - do */
struct devlink_sb_tc_pool_bind_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_type:1;
		__u32 sb_tc_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	enum devlink_sb_pool_type sb_pool_type;
	__u16 sb_tc_index;
};

static inline struct devlink_sb_tc_pool_bind_get_req *
devlink_sb_tc_pool_bind_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_tc_pool_bind_get_req));
}
void
devlink_sb_tc_pool_bind_get_req_free(struct devlink_sb_tc_pool_bind_get_req *req);

static inline void
devlink_sb_tc_pool_bind_get_req_set_bus_name(struct devlink_sb_tc_pool_bind_get_req *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_dev_name(struct devlink_sb_tc_pool_bind_get_req *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_port_index(struct devlink_sb_tc_pool_bind_get_req *req,
					       __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_sb_index(struct devlink_sb_tc_pool_bind_get_req *req,
					     __u32 sb_index)
{
	req->_present.sb_index = 1;
	req->sb_index = sb_index;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_sb_pool_type(struct devlink_sb_tc_pool_bind_get_req *req,
						 enum devlink_sb_pool_type sb_pool_type)
{
	req->_present.sb_pool_type = 1;
	req->sb_pool_type = sb_pool_type;
}
static inline void
devlink_sb_tc_pool_bind_get_req_set_sb_tc_index(struct devlink_sb_tc_pool_bind_get_req *req,
						__u16 sb_tc_index)
{
	req->_present.sb_tc_index = 1;
	req->sb_tc_index = sb_tc_index;
}

struct devlink_sb_tc_pool_bind_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 sb_index:1;
		__u32 sb_pool_type:1;
		__u32 sb_tc_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	__u32 sb_index;
	enum devlink_sb_pool_type sb_pool_type;
	__u16 sb_tc_index;
};

void
devlink_sb_tc_pool_bind_get_rsp_free(struct devlink_sb_tc_pool_bind_get_rsp *rsp);

/*
 * Get shared buffer port-TC to pool bindings and threshold.
 */
struct devlink_sb_tc_pool_bind_get_rsp *
devlink_sb_tc_pool_bind_get(struct ynl_sock *ys,
			    struct devlink_sb_tc_pool_bind_get_req *req);

/* DEVLINK_CMD_SB_TC_POOL_BIND_GET - dump */
struct devlink_sb_tc_pool_bind_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_sb_tc_pool_bind_get_req_dump *
devlink_sb_tc_pool_bind_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_sb_tc_pool_bind_get_req_dump));
}
void
devlink_sb_tc_pool_bind_get_req_dump_free(struct devlink_sb_tc_pool_bind_get_req_dump *req);

static inline void
devlink_sb_tc_pool_bind_get_req_dump_set_bus_name(struct devlink_sb_tc_pool_bind_get_req_dump *req,
						  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_sb_tc_pool_bind_get_req_dump_set_dev_name(struct devlink_sb_tc_pool_bind_get_req_dump *req,
						  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_sb_tc_pool_bind_get_list {
	struct devlink_sb_tc_pool_bind_get_list *next;
	struct devlink_sb_tc_pool_bind_get_rsp obj __attribute__ ((aligned (8)));
};

void
devlink_sb_tc_pool_bind_get_list_free(struct devlink_sb_tc_pool_bind_get_list *rsp);

struct devlink_sb_tc_pool_bind_get_list *
devlink_sb_tc_pool_bind_get_dump(struct ynl_sock *ys,
				 struct devlink_sb_tc_pool_bind_get_req_dump *req);

/* ============== DEVLINK_CMD_PARAM_GET ============== */
/* DEVLINK_CMD_PARAM_GET - do */
struct devlink_param_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 param_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *param_name;
};

static inline struct devlink_param_get_req *devlink_param_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_param_get_req));
}
void devlink_param_get_req_free(struct devlink_param_get_req *req);

static inline void
devlink_param_get_req_set_bus_name(struct devlink_param_get_req *req,
				   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_param_get_req_set_dev_name(struct devlink_param_get_req *req,
				   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_param_get_req_set_param_name(struct devlink_param_get_req *req,
				     const char *param_name)
{
	free(req->param_name);
	req->_present.param_name_len = strlen(param_name);
	req->param_name = malloc(req->_present.param_name_len + 1);
	memcpy(req->param_name, param_name, req->_present.param_name_len);
	req->param_name[req->_present.param_name_len] = 0;
}

struct devlink_param_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 param_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *param_name;
};

void devlink_param_get_rsp_free(struct devlink_param_get_rsp *rsp);

/*
 * Get param instances.
 */
struct devlink_param_get_rsp *
devlink_param_get(struct ynl_sock *ys, struct devlink_param_get_req *req);

/* DEVLINK_CMD_PARAM_GET - dump */
struct devlink_param_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_param_get_req_dump *
devlink_param_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_param_get_req_dump));
}
void devlink_param_get_req_dump_free(struct devlink_param_get_req_dump *req);

static inline void
devlink_param_get_req_dump_set_bus_name(struct devlink_param_get_req_dump *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_param_get_req_dump_set_dev_name(struct devlink_param_get_req_dump *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_param_get_list {
	struct devlink_param_get_list *next;
	struct devlink_param_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_param_get_list_free(struct devlink_param_get_list *rsp);

struct devlink_param_get_list *
devlink_param_get_dump(struct ynl_sock *ys,
		       struct devlink_param_get_req_dump *req);

/* ============== DEVLINK_CMD_REGION_GET ============== */
/* DEVLINK_CMD_REGION_GET - do */
struct devlink_region_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
};

static inline struct devlink_region_get_req *devlink_region_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_get_req));
}
void devlink_region_get_req_free(struct devlink_region_get_req *req);

static inline void
devlink_region_get_req_set_bus_name(struct devlink_region_get_req *req,
				    const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_get_req_set_dev_name(struct devlink_region_get_req *req,
				    const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_region_get_req_set_port_index(struct devlink_region_get_req *req,
				      __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_region_get_req_set_region_name(struct devlink_region_get_req *req,
				       const char *region_name)
{
	free(req->region_name);
	req->_present.region_name_len = strlen(region_name);
	req->region_name = malloc(req->_present.region_name_len + 1);
	memcpy(req->region_name, region_name, req->_present.region_name_len);
	req->region_name[req->_present.region_name_len] = 0;
}

struct devlink_region_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 region_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *region_name;
};

void devlink_region_get_rsp_free(struct devlink_region_get_rsp *rsp);

/*
 * Get region instances.
 */
struct devlink_region_get_rsp *
devlink_region_get(struct ynl_sock *ys, struct devlink_region_get_req *req);

/* DEVLINK_CMD_REGION_GET - dump */
struct devlink_region_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_region_get_req_dump *
devlink_region_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_region_get_req_dump));
}
void devlink_region_get_req_dump_free(struct devlink_region_get_req_dump *req);

static inline void
devlink_region_get_req_dump_set_bus_name(struct devlink_region_get_req_dump *req,
					 const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_region_get_req_dump_set_dev_name(struct devlink_region_get_req_dump *req,
					 const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_region_get_list {
	struct devlink_region_get_list *next;
	struct devlink_region_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_region_get_list_free(struct devlink_region_get_list *rsp);

struct devlink_region_get_list *
devlink_region_get_dump(struct ynl_sock *ys,
			struct devlink_region_get_req_dump *req);

/* ============== DEVLINK_CMD_INFO_GET ============== */
/* DEVLINK_CMD_INFO_GET - do */
struct devlink_info_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_info_get_req *devlink_info_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_info_get_req));
}
void devlink_info_get_req_free(struct devlink_info_get_req *req);

static inline void
devlink_info_get_req_set_bus_name(struct devlink_info_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_info_get_req_set_dev_name(struct devlink_info_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_info_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 info_driver_name_len;
		__u32 info_serial_number_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *info_driver_name;
	char *info_serial_number;
	unsigned int n_info_version_fixed;
	struct devlink_dl_info_version *info_version_fixed;
	unsigned int n_info_version_running;
	struct devlink_dl_info_version *info_version_running;
	unsigned int n_info_version_stored;
	struct devlink_dl_info_version *info_version_stored;
};

void devlink_info_get_rsp_free(struct devlink_info_get_rsp *rsp);

/*
 * Get device information, like driver name, hardware and firmware versions etc.
 */
struct devlink_info_get_rsp *
devlink_info_get(struct ynl_sock *ys, struct devlink_info_get_req *req);

/* DEVLINK_CMD_INFO_GET - dump */
struct devlink_info_get_list {
	struct devlink_info_get_list *next;
	struct devlink_info_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_info_get_list_free(struct devlink_info_get_list *rsp);

struct devlink_info_get_list *devlink_info_get_dump(struct ynl_sock *ys);

/* ============== DEVLINK_CMD_HEALTH_REPORTER_GET ============== */
/* DEVLINK_CMD_HEALTH_REPORTER_GET - do */
struct devlink_health_reporter_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

static inline struct devlink_health_reporter_get_req *
devlink_health_reporter_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_get_req));
}
void
devlink_health_reporter_get_req_free(struct devlink_health_reporter_get_req *req);

static inline void
devlink_health_reporter_get_req_set_bus_name(struct devlink_health_reporter_get_req *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_set_dev_name(struct devlink_health_reporter_get_req *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_set_port_index(struct devlink_health_reporter_get_req *req,
					       __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_health_reporter_get_req_set_health_reporter_name(struct devlink_health_reporter_get_req *req,
							 const char *health_reporter_name)
{
	free(req->health_reporter_name);
	req->_present.health_reporter_name_len = strlen(health_reporter_name);
	req->health_reporter_name = malloc(req->_present.health_reporter_name_len + 1);
	memcpy(req->health_reporter_name, health_reporter_name, req->_present.health_reporter_name_len);
	req->health_reporter_name[req->_present.health_reporter_name_len] = 0;
}

struct devlink_health_reporter_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 health_reporter_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *health_reporter_name;
};

void
devlink_health_reporter_get_rsp_free(struct devlink_health_reporter_get_rsp *rsp);

/*
 * Get health reporter instances.
 */
struct devlink_health_reporter_get_rsp *
devlink_health_reporter_get(struct ynl_sock *ys,
			    struct devlink_health_reporter_get_req *req);

/* DEVLINK_CMD_HEALTH_REPORTER_GET - dump */
struct devlink_health_reporter_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
};

static inline struct devlink_health_reporter_get_req_dump *
devlink_health_reporter_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_health_reporter_get_req_dump));
}
void
devlink_health_reporter_get_req_dump_free(struct devlink_health_reporter_get_req_dump *req);

static inline void
devlink_health_reporter_get_req_dump_set_bus_name(struct devlink_health_reporter_get_req_dump *req,
						  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_dump_set_dev_name(struct devlink_health_reporter_get_req_dump *req,
						  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_health_reporter_get_req_dump_set_port_index(struct devlink_health_reporter_get_req_dump *req,
						    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}

struct devlink_health_reporter_get_list {
	struct devlink_health_reporter_get_list *next;
	struct devlink_health_reporter_get_rsp obj __attribute__ ((aligned (8)));
};

void
devlink_health_reporter_get_list_free(struct devlink_health_reporter_get_list *rsp);

struct devlink_health_reporter_get_list *
devlink_health_reporter_get_dump(struct ynl_sock *ys,
				 struct devlink_health_reporter_get_req_dump *req);

/* ============== DEVLINK_CMD_TRAP_GET ============== */
/* DEVLINK_CMD_TRAP_GET - do */
struct devlink_trap_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_name;
};

static inline struct devlink_trap_get_req *devlink_trap_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_get_req));
}
void devlink_trap_get_req_free(struct devlink_trap_get_req *req);

static inline void
devlink_trap_get_req_set_bus_name(struct devlink_trap_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_get_req_set_dev_name(struct devlink_trap_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_get_req_set_trap_name(struct devlink_trap_get_req *req,
				   const char *trap_name)
{
	free(req->trap_name);
	req->_present.trap_name_len = strlen(trap_name);
	req->trap_name = malloc(req->_present.trap_name_len + 1);
	memcpy(req->trap_name, trap_name, req->_present.trap_name_len);
	req->trap_name[req->_present.trap_name_len] = 0;
}

struct devlink_trap_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_name;
};

void devlink_trap_get_rsp_free(struct devlink_trap_get_rsp *rsp);

/*
 * Get trap instances.
 */
struct devlink_trap_get_rsp *
devlink_trap_get(struct ynl_sock *ys, struct devlink_trap_get_req *req);

/* DEVLINK_CMD_TRAP_GET - dump */
struct devlink_trap_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_trap_get_req_dump *
devlink_trap_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_get_req_dump));
}
void devlink_trap_get_req_dump_free(struct devlink_trap_get_req_dump *req);

static inline void
devlink_trap_get_req_dump_set_bus_name(struct devlink_trap_get_req_dump *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_get_req_dump_set_dev_name(struct devlink_trap_get_req_dump *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_trap_get_list {
	struct devlink_trap_get_list *next;
	struct devlink_trap_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_trap_get_list_free(struct devlink_trap_get_list *rsp);

struct devlink_trap_get_list *
devlink_trap_get_dump(struct ynl_sock *ys,
		      struct devlink_trap_get_req_dump *req);

/* ============== DEVLINK_CMD_TRAP_GROUP_GET ============== */
/* DEVLINK_CMD_TRAP_GROUP_GET - do */
struct devlink_trap_group_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_group_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_group_name;
};

static inline struct devlink_trap_group_get_req *
devlink_trap_group_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_group_get_req));
}
void devlink_trap_group_get_req_free(struct devlink_trap_group_get_req *req);

static inline void
devlink_trap_group_get_req_set_bus_name(struct devlink_trap_group_get_req *req,
					const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_group_get_req_set_dev_name(struct devlink_trap_group_get_req *req,
					const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_group_get_req_set_trap_group_name(struct devlink_trap_group_get_req *req,
					       const char *trap_group_name)
{
	free(req->trap_group_name);
	req->_present.trap_group_name_len = strlen(trap_group_name);
	req->trap_group_name = malloc(req->_present.trap_group_name_len + 1);
	memcpy(req->trap_group_name, trap_group_name, req->_present.trap_group_name_len);
	req->trap_group_name[req->_present.trap_group_name_len] = 0;
}

struct devlink_trap_group_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_group_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	char *trap_group_name;
};

void devlink_trap_group_get_rsp_free(struct devlink_trap_group_get_rsp *rsp);

/*
 * Get trap group instances.
 */
struct devlink_trap_group_get_rsp *
devlink_trap_group_get(struct ynl_sock *ys,
		       struct devlink_trap_group_get_req *req);

/* DEVLINK_CMD_TRAP_GROUP_GET - dump */
struct devlink_trap_group_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_trap_group_get_req_dump *
devlink_trap_group_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_group_get_req_dump));
}
void
devlink_trap_group_get_req_dump_free(struct devlink_trap_group_get_req_dump *req);

static inline void
devlink_trap_group_get_req_dump_set_bus_name(struct devlink_trap_group_get_req_dump *req,
					     const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_group_get_req_dump_set_dev_name(struct devlink_trap_group_get_req_dump *req,
					     const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_trap_group_get_list {
	struct devlink_trap_group_get_list *next;
	struct devlink_trap_group_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_trap_group_get_list_free(struct devlink_trap_group_get_list *rsp);

struct devlink_trap_group_get_list *
devlink_trap_group_get_dump(struct ynl_sock *ys,
			    struct devlink_trap_group_get_req_dump *req);

/* ============== DEVLINK_CMD_TRAP_POLICER_GET ============== */
/* DEVLINK_CMD_TRAP_POLICER_GET - do */
struct devlink_trap_policer_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_policer_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 trap_policer_id;
};

static inline struct devlink_trap_policer_get_req *
devlink_trap_policer_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_policer_get_req));
}
void
devlink_trap_policer_get_req_free(struct devlink_trap_policer_get_req *req);

static inline void
devlink_trap_policer_get_req_set_bus_name(struct devlink_trap_policer_get_req *req,
					  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_policer_get_req_set_dev_name(struct devlink_trap_policer_get_req *req,
					  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_trap_policer_get_req_set_trap_policer_id(struct devlink_trap_policer_get_req *req,
						 __u32 trap_policer_id)
{
	req->_present.trap_policer_id = 1;
	req->trap_policer_id = trap_policer_id;
}

struct devlink_trap_policer_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 trap_policer_id:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 trap_policer_id;
};

void
devlink_trap_policer_get_rsp_free(struct devlink_trap_policer_get_rsp *rsp);

/*
 * Get trap policer instances.
 */
struct devlink_trap_policer_get_rsp *
devlink_trap_policer_get(struct ynl_sock *ys,
			 struct devlink_trap_policer_get_req *req);

/* DEVLINK_CMD_TRAP_POLICER_GET - dump */
struct devlink_trap_policer_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_trap_policer_get_req_dump *
devlink_trap_policer_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_trap_policer_get_req_dump));
}
void
devlink_trap_policer_get_req_dump_free(struct devlink_trap_policer_get_req_dump *req);

static inline void
devlink_trap_policer_get_req_dump_set_bus_name(struct devlink_trap_policer_get_req_dump *req,
					       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_trap_policer_get_req_dump_set_dev_name(struct devlink_trap_policer_get_req_dump *req,
					       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_trap_policer_get_list {
	struct devlink_trap_policer_get_list *next;
	struct devlink_trap_policer_get_rsp obj __attribute__ ((aligned (8)));
};

void
devlink_trap_policer_get_list_free(struct devlink_trap_policer_get_list *rsp);

struct devlink_trap_policer_get_list *
devlink_trap_policer_get_dump(struct ynl_sock *ys,
			      struct devlink_trap_policer_get_req_dump *req);

/* ============== DEVLINK_CMD_RATE_GET ============== */
/* DEVLINK_CMD_RATE_GET - do */
struct devlink_rate_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 rate_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *rate_node_name;
};

static inline struct devlink_rate_get_req *devlink_rate_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_get_req));
}
void devlink_rate_get_req_free(struct devlink_rate_get_req *req);

static inline void
devlink_rate_get_req_set_bus_name(struct devlink_rate_get_req *req,
				  const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_get_req_set_dev_name(struct devlink_rate_get_req *req,
				  const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_rate_get_req_set_port_index(struct devlink_rate_get_req *req,
				    __u32 port_index)
{
	req->_present.port_index = 1;
	req->port_index = port_index;
}
static inline void
devlink_rate_get_req_set_rate_node_name(struct devlink_rate_get_req *req,
					const char *rate_node_name)
{
	free(req->rate_node_name);
	req->_present.rate_node_name_len = strlen(rate_node_name);
	req->rate_node_name = malloc(req->_present.rate_node_name_len + 1);
	memcpy(req->rate_node_name, rate_node_name, req->_present.rate_node_name_len);
	req->rate_node_name[req->_present.rate_node_name_len] = 0;
}

struct devlink_rate_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 port_index:1;
		__u32 rate_node_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 port_index;
	char *rate_node_name;
};

void devlink_rate_get_rsp_free(struct devlink_rate_get_rsp *rsp);

/*
 * Get rate instances.
 */
struct devlink_rate_get_rsp *
devlink_rate_get(struct ynl_sock *ys, struct devlink_rate_get_req *req);

/* DEVLINK_CMD_RATE_GET - dump */
struct devlink_rate_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_rate_get_req_dump *
devlink_rate_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_rate_get_req_dump));
}
void devlink_rate_get_req_dump_free(struct devlink_rate_get_req_dump *req);

static inline void
devlink_rate_get_req_dump_set_bus_name(struct devlink_rate_get_req_dump *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_rate_get_req_dump_set_dev_name(struct devlink_rate_get_req_dump *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_rate_get_list {
	struct devlink_rate_get_list *next;
	struct devlink_rate_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_rate_get_list_free(struct devlink_rate_get_list *rsp);

struct devlink_rate_get_list *
devlink_rate_get_dump(struct ynl_sock *ys,
		      struct devlink_rate_get_req_dump *req);

/* ============== DEVLINK_CMD_LINECARD_GET ============== */
/* DEVLINK_CMD_LINECARD_GET - do */
struct devlink_linecard_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 linecard_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 linecard_index;
};

static inline struct devlink_linecard_get_req *
devlink_linecard_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_linecard_get_req));
}
void devlink_linecard_get_req_free(struct devlink_linecard_get_req *req);

static inline void
devlink_linecard_get_req_set_bus_name(struct devlink_linecard_get_req *req,
				      const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_linecard_get_req_set_dev_name(struct devlink_linecard_get_req *req,
				      const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}
static inline void
devlink_linecard_get_req_set_linecard_index(struct devlink_linecard_get_req *req,
					    __u32 linecard_index)
{
	req->_present.linecard_index = 1;
	req->linecard_index = linecard_index;
}

struct devlink_linecard_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
		__u32 linecard_index:1;
	} _present;

	char *bus_name;
	char *dev_name;
	__u32 linecard_index;
};

void devlink_linecard_get_rsp_free(struct devlink_linecard_get_rsp *rsp);

/*
 * Get line card instances.
 */
struct devlink_linecard_get_rsp *
devlink_linecard_get(struct ynl_sock *ys, struct devlink_linecard_get_req *req);

/* DEVLINK_CMD_LINECARD_GET - dump */
struct devlink_linecard_get_req_dump {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_linecard_get_req_dump *
devlink_linecard_get_req_dump_alloc(void)
{
	return calloc(1, sizeof(struct devlink_linecard_get_req_dump));
}
void
devlink_linecard_get_req_dump_free(struct devlink_linecard_get_req_dump *req);

static inline void
devlink_linecard_get_req_dump_set_bus_name(struct devlink_linecard_get_req_dump *req,
					   const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_linecard_get_req_dump_set_dev_name(struct devlink_linecard_get_req_dump *req,
					   const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_linecard_get_list {
	struct devlink_linecard_get_list *next;
	struct devlink_linecard_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_linecard_get_list_free(struct devlink_linecard_get_list *rsp);

struct devlink_linecard_get_list *
devlink_linecard_get_dump(struct ynl_sock *ys,
			  struct devlink_linecard_get_req_dump *req);

/* ============== DEVLINK_CMD_SELFTESTS_GET ============== */
/* DEVLINK_CMD_SELFTESTS_GET - do */
struct devlink_selftests_get_req {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

static inline struct devlink_selftests_get_req *
devlink_selftests_get_req_alloc(void)
{
	return calloc(1, sizeof(struct devlink_selftests_get_req));
}
void devlink_selftests_get_req_free(struct devlink_selftests_get_req *req);

static inline void
devlink_selftests_get_req_set_bus_name(struct devlink_selftests_get_req *req,
				       const char *bus_name)
{
	free(req->bus_name);
	req->_present.bus_name_len = strlen(bus_name);
	req->bus_name = malloc(req->_present.bus_name_len + 1);
	memcpy(req->bus_name, bus_name, req->_present.bus_name_len);
	req->bus_name[req->_present.bus_name_len] = 0;
}
static inline void
devlink_selftests_get_req_set_dev_name(struct devlink_selftests_get_req *req,
				       const char *dev_name)
{
	free(req->dev_name);
	req->_present.dev_name_len = strlen(dev_name);
	req->dev_name = malloc(req->_present.dev_name_len + 1);
	memcpy(req->dev_name, dev_name, req->_present.dev_name_len);
	req->dev_name[req->_present.dev_name_len] = 0;
}

struct devlink_selftests_get_rsp {
	struct {
		__u32 bus_name_len;
		__u32 dev_name_len;
	} _present;

	char *bus_name;
	char *dev_name;
};

void devlink_selftests_get_rsp_free(struct devlink_selftests_get_rsp *rsp);

/*
 * Get device selftest instances.
 */
struct devlink_selftests_get_rsp *
devlink_selftests_get(struct ynl_sock *ys,
		      struct devlink_selftests_get_req *req);

/* DEVLINK_CMD_SELFTESTS_GET - dump */
struct devlink_selftests_get_list {
	struct devlink_selftests_get_list *next;
	struct devlink_selftests_get_rsp obj __attribute__ ((aligned (8)));
};

void devlink_selftests_get_list_free(struct devlink_selftests_get_list *rsp);

struct devlink_selftests_get_list *
devlink_selftests_get_dump(struct ynl_sock *ys);

#endif /* _LINUX_DEVLINK_GEN_H */
