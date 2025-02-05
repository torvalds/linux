/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __SERVER_H__
#define __SERVER_H__

#include "smbacl.h"

/*
 * Server state type
 */
enum {
	SERVER_STATE_STARTING_UP,
	SERVER_STATE_RUNNING,
	SERVER_STATE_RESETTING,
	SERVER_STATE_SHUTTING_DOWN,
};

/*
 * Server global config string index
 */
enum {
	SERVER_CONF_NETBIOS_NAME,
	SERVER_CONF_SERVER_STRING,
	SERVER_CONF_WORK_GROUP,
};

struct ksmbd_server_config {
	unsigned int		flags;
	unsigned int		state;
	short			signing;
	short			enforced_signing;
	short			min_protocol;
	short			max_protocol;
	unsigned short		tcp_port;
	unsigned short		ipc_timeout;
	unsigned long		ipc_last_active;
	unsigned long		deadtime;
	unsigned int		share_fake_fscaps;
	struct smb_sid		domain_sid;
	unsigned int		auth_mechs;
	unsigned int		max_connections;
	unsigned int		max_inflight_req;

	char			*conf[SERVER_CONF_WORK_GROUP + 1];
	struct task_struct	*dh_task;
	bool			bind_interfaces_only;
};

extern struct ksmbd_server_config server_conf;

int ksmbd_set_netbios_name(char *v);
int ksmbd_set_server_string(char *v);
int ksmbd_set_work_group(char *v);

char *ksmbd_netbios_name(void);
char *ksmbd_server_string(void);
char *ksmbd_work_group(void);

static inline int ksmbd_server_running(void)
{
	return READ_ONCE(server_conf.state) == SERVER_STATE_RUNNING;
}

static inline int ksmbd_server_configurable(void)
{
	return READ_ONCE(server_conf.state) < SERVER_STATE_RESETTING;
}

int server_queue_ctrl_init_work(void);
int server_queue_ctrl_reset_work(void);
#endif /* __SERVER_H__ */
