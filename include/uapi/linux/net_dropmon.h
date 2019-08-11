/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __NET_DROPMON_H
#define __NET_DROPMON_H

#include <linux/types.h>
#include <linux/netlink.h>

struct net_dm_drop_point {
	__u8 pc[8];
	__u32 count;
};

#define is_drop_point_hw(x) do {\
	int ____i, ____j;\
	for (____i = 0; ____i < 8; i ____i++)\
		____j |= x[____i];\
	____j;\
} while (0)

#define NET_DM_CFG_VERSION  0
#define NET_DM_CFG_ALERT_COUNT  1
#define NET_DM_CFG_ALERT_DELAY 2
#define NET_DM_CFG_MAX 3

struct net_dm_config_entry {
	__u32 type;
	__u64 data __attribute__((aligned(8)));
};

struct net_dm_config_msg {
	__u32 entries;
	struct net_dm_config_entry options[0];
};

struct net_dm_alert_msg {
	__u32 entries;
	struct net_dm_drop_point points[0];
};

struct net_dm_user_msg {
	union {
		struct net_dm_config_msg user;
		struct net_dm_alert_msg alert;
	} u;
};


/* These are the netlink message types for this protocol */

enum {
	NET_DM_CMD_UNSPEC = 0,
	NET_DM_CMD_ALERT,
	NET_DM_CMD_CONFIG,
	NET_DM_CMD_START,
	NET_DM_CMD_STOP,
	_NET_DM_CMD_MAX,
};

#define NET_DM_CMD_MAX (_NET_DM_CMD_MAX - 1)

/*
 * Our group identifiers
 */
#define NET_DM_GRP_ALERT 1

/**
 * enum net_dm_alert_mode - Alert mode.
 * @NET_DM_ALERT_MODE_SUMMARY: A summary of recent drops is sent to user space.
 */
enum net_dm_alert_mode {
	NET_DM_ALERT_MODE_SUMMARY,
};

#endif
