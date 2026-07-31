// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2025, LG Electronics.
 *   Author(s): Hyunchul Lee <hyc.lee@gmail.com>
 *   Copyright (C) 2025, Samsung Electronics.
 *   Author(s): Vedansh Bhardwaj <v.bhardwaj@samsung.com>
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "misc.h"
#include "connection.h"
#include "server.h"
#include "stats.h"
#include "smb_common.h"
#include "smb2pdu.h"
#include "vfs_cache.h"

static struct proc_dir_entry *ksmbd_proc_fs;
struct ksmbd_counters ksmbd_counters;

struct proc_dir_entry *ksmbd_proc_create(const char *name,
					 int (*show)(struct seq_file *m, void *v),
						 void *v)
{
	return proc_create_single_data(name, 0400, ksmbd_proc_fs,
			   show, v);
}

void ksmbd_proc_show_flag_names(struct seq_file *m,
				const struct ksmbd_const_name *table,
				int count, unsigned int flags)
{
	unsigned int remaining = flags;
	bool separator = false;
	int i;

	for (i = 0; i < count; i++) {
		unsigned int flag = table[i].const_value;

		if (!flag || (remaining & flag) != flag)
			continue;
		seq_printf(m, "%s%s", separator ? "," : "", table[i].name);
		separator = true;
		remaining &= ~flag;
	}

	if (remaining)
		seq_printf(m, "%s0x%08x", separator ? "," : "", remaining);
	else if (!separator)
		seq_puts(m, "none");
}

const char *ksmbd_proc_const_name(const struct ksmbd_const_name *table,
				  int count, unsigned int const_value)
{
	int i;

	for (i = 0; i < count; i++) {
		if (table[i].const_value == const_value)
			return table[i].name;
	}
	return NULL;
}

struct ksmbd_const_smb2_process_req {
	unsigned int const_value;
	const char *name;
};

static const struct ksmbd_const_smb2_process_req smb2_process_req[KSMBD_COUNTER_MAX_REQS] = {
	{le16_to_cpu(SMB2_NEGOTIATE), "SMB2_NEGOTIATE"},
	{le16_to_cpu(SMB2_SESSION_SETUP), "SMB2_SESSION_SETUP"},
	{le16_to_cpu(SMB2_LOGOFF), "SMB2_LOGOFF"},
	{le16_to_cpu(SMB2_TREE_CONNECT), "SMB2_TREE_CONNECT"},
	{le16_to_cpu(SMB2_TREE_DISCONNECT), "SMB2_TREE_DISCONNECT"},
	{le16_to_cpu(SMB2_CREATE), "SMB2_CREATE"},
	{le16_to_cpu(SMB2_CLOSE), "SMB2_CLOSE"},
	{le16_to_cpu(SMB2_FLUSH), "SMB2_FLUSH"},
	{le16_to_cpu(SMB2_READ), "SMB2_READ"},
	{le16_to_cpu(SMB2_WRITE), "SMB2_WRITE"},
	{le16_to_cpu(SMB2_LOCK), "SMB2_LOCK"},
	{le16_to_cpu(SMB2_IOCTL), "SMB2_IOCTL"},
	{le16_to_cpu(SMB2_CANCEL), "SMB2_CANCEL"},
	{le16_to_cpu(SMB2_ECHO), "SMB2_ECHO"},
	{le16_to_cpu(SMB2_QUERY_DIRECTORY), "SMB2_QUERY_DIRECTORY"},
	{le16_to_cpu(SMB2_CHANGE_NOTIFY), "SMB2_CHANGE_NOTIFY"},
	{le16_to_cpu(SMB2_QUERY_INFO), "SMB2_QUERY_INFO"},
	{le16_to_cpu(SMB2_SET_INFO), "SMB2_SET_INFO"},
	{le16_to_cpu(SMB2_OPLOCK_BREAK), "SMB2_OPLOCK_BREAK"},
};

static const char *ksmbd_server_state_string(void)
{
	switch (READ_ONCE(server_conf.state)) {
	case SERVER_STATE_STARTING_UP:
		return "starting";
	case SERVER_STATE_RUNNING:
		return "running";
	case SERVER_STATE_RESETTING:
		return "resetting";
	case SERVER_STATE_SHUTTING_DOWN:
		return "shutdown";
	default:
		return "unknown";
	}
}

static const char *ksmbd_signing_mode_string(void)
{
	switch (server_conf.signing) {
	case KSMBD_CONFIG_OPT_DISABLED:
		return "disabled";
	case KSMBD_CONFIG_OPT_MANDATORY:
		return "mandatory";
	case KSMBD_CONFIG_OPT_AUTO:
		return "auto";
	default:
		return "unknown";
	}
}

static void proc_show_runtime_totals(struct seq_file *m)
{
	struct ksmbd_conn *conn;
	unsigned int clients = 0;
	unsigned int open_files = 0;
	int i;

	down_read(&conn_list_lock);
	hash_for_each(conn_list, i, conn, hlist) {
		clients++;
		open_files += atomic_read(&conn->stats.open_files_count);
	}
	up_read(&conn_list_lock);

	seq_printf(m, "clients:\t%u\n", clients);
	seq_printf(m, "open_files:\t%u\n", open_files);
}

static int proc_show_ksmbd_stats(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "Server\n");
	seq_printf(m, "state:\t%s\n", ksmbd_server_state_string());
	seq_printf(m, "name:\t%s\n", ksmbd_server_string());
	seq_printf(m, "netbios:\t%s\n", ksmbd_netbios_name());
	seq_printf(m, "work_group:\t%s\n", ksmbd_work_group());
	seq_printf(m, "min_protocol:\t%s\n", ksmbd_get_protocol_string(server_conf.min_protocol));
	seq_printf(m, "max_protocol:\t%s\n", ksmbd_get_protocol_string(server_conf.max_protocol));
	seq_printf(m, "flags:\t0x%08x\n", server_conf.flags);
	seq_printf(m, "tcp_port:\t%u\n", server_conf.tcp_port);
	seq_printf(m, "signing:\t%s\n", ksmbd_signing_mode_string());
	seq_printf(m, "signing_enforced:\t%s\n",
		   server_conf.enforced_signing ? "yes" : "no");
	seq_printf(m, "bind_interfaces_only:\t%s\n",
		   server_conf.bind_interfaces_only ? "yes" : "no");
	seq_printf(m, "max_connections:\t%u\n", server_conf.max_connections);
	seq_printf(m, "max_connections_per_ip:\t%u\n",
		   server_conf.max_ip_connections);
	seq_printf(m, "max_inflight_requests:\t%u\n",
		   server_conf.max_inflight_req);
	seq_printf(m, "deadtime_seconds:\t%lu\n", server_conf.deadtime / HZ);
	seq_printf(m, "ipc_timeout_seconds:\t%u\n", server_conf.ipc_timeout / HZ);
	if (server_conf.ipc_last_active)
		seq_printf(m, "ipc_last_active_seconds:\t%lu\n",
			   jiffies_to_msecs(jiffies - server_conf.ipc_last_active) /
			   MSEC_PER_SEC);
	else
		seq_puts(m, "ipc_last_active_seconds:\tnever\n");
	seq_printf(m, "durable_scavenger:\t%s\n",
		   ksmbd_durable_scavenger_active() ? "running" : "stopped");
	seq_printf(m, "share_fake_fscaps:\t0x%08x\n",
		   server_conf.share_fake_fscaps);
	proc_show_runtime_totals(m);
	seq_printf(m, "sessions:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_SESSIONS));
	seq_printf(m, "tree_connects:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_TREE_CONNS));
	seq_printf(m, "requests:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_REQUESTS));
	seq_printf(m, "read_bytes:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_READ_BYTES));
	seq_printf(m, "written_bytes:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_WRITE_BYTES));

	seq_puts(m, "\nSMB2\n");
	for (i = 0; i < KSMBD_COUNTER_MAX_REQS; i++)
		seq_printf(m, "%s:\t%lld\n", smb2_process_req[i].name,
			   ksmbd_counter_sum(KSMBD_COUNTER_FIRST_REQ + i));

	seq_puts(m, "\nSMB2 status\n");
	seq_printf(m, "success:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_STATUS_SUCCESS));
	seq_printf(m, "informational:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_STATUS_INFORMATIONAL));
	seq_printf(m, "warning:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_STATUS_WARNING));
	seq_printf(m, "error:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_STATUS_ERROR));
	seq_printf(m, "access_denied:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_ERROR_ACCESS_DENIED));
	seq_printf(m, "not_found:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_ERROR_NOT_FOUND));
	seq_printf(m, "invalid_parameter:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_ERROR_INVALID_PARAMETER));
	seq_printf(m, "sharing_violation:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_ERROR_SHARING_VIOLATION));
	seq_printf(m, "not_supported:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_ERROR_NOT_SUPPORTED));
	seq_printf(m, "other:\t%lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_ERROR_OTHER));
	return 0;
}

void ksmbd_proc_cleanup(void)
{
	int i;

	if (!ksmbd_proc_fs)
		return;

	proc_remove(ksmbd_proc_fs);

	for (i = 0; i < ARRAY_SIZE(ksmbd_counters.counters); i++)
		percpu_counter_destroy(&ksmbd_counters.counters[i]);

	ksmbd_proc_fs = NULL;
}

void ksmbd_proc_reset(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ksmbd_counters.counters); i++)
		percpu_counter_set(&ksmbd_counters.counters[i], 0);
}

int ksmbd_proc_init(void)
{
	int i;
	int retval = -ENOMEM;

	ksmbd_proc_fs = proc_mkdir("fs/ksmbd", NULL);
	if (!ksmbd_proc_fs)
		return retval;

	if (!proc_mkdir_mode("sessions", 0400, ksmbd_proc_fs))
		goto err_out;

	for (i = 0; i < ARRAY_SIZE(ksmbd_counters.counters); i++) {
		retval = percpu_counter_init(&ksmbd_counters.counters[i], 0, GFP_KERNEL);
		if (retval)
			goto err_out;
	}

	if (!ksmbd_proc_create("server", proc_show_ksmbd_stats, NULL)) {
		retval = -ENOMEM;
		goto err_out;
	}

	ksmbd_proc_reset();
	return 0;
err_out:
	ksmbd_proc_cleanup();
	return retval;
}
