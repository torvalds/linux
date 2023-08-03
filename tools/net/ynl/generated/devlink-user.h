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

#endif /* _LINUX_DEVLINK_GEN_H */
