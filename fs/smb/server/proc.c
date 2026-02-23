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
#include "server.h"
#include "stats.h"
#include "smb_common.h"
#include "smb2pdu.h"

static struct proc_dir_entry *ksmbd_proc_fs;
struct ksmbd_counters ksmbd_counters;

struct proc_dir_entry *ksmbd_proc_create(const char *name,
					 int (*show)(struct seq_file *m, void *v),
						 void *v)
{
	return proc_create_single_data(name, 0400, ksmbd_proc_fs,
			   show, v);
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

static int proc_show_ksmbd_stats(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "Server\n");
	seq_printf(m, "name: %s\n", ksmbd_server_string());
	seq_printf(m, "netbios: %s\n", ksmbd_netbios_name());
	seq_printf(m, "work group: %s\n", ksmbd_work_group());
	seq_printf(m, "min protocol: %s\n", ksmbd_get_protocol_string(server_conf.min_protocol));
	seq_printf(m, "max protocol: %s\n", ksmbd_get_protocol_string(server_conf.max_protocol));
	seq_printf(m, "flags: 0x%08x\n", server_conf.flags);
	seq_printf(m, "share_fake_fscaps: 0x%08x\n",
		   server_conf.share_fake_fscaps);
	seq_printf(m, "sessions: %lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_SESSIONS));
	seq_printf(m, "tree connects: %lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_TREE_CONNS));
	seq_printf(m, "read bytes: %lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_READ_BYTES));
	seq_printf(m, "written bytes: %lld\n",
		   ksmbd_counter_sum(KSMBD_COUNTER_WRITE_BYTES));

	seq_puts(m, "\nSMB2\n");
	for (i = 0; i < KSMBD_COUNTER_MAX_REQS; i++)
		seq_printf(m, "%-20s:\t%lld\n", smb2_process_req[i].name,
			   ksmbd_counter_sum(KSMBD_COUNTER_FIRST_REQ + i));
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

void ksmbd_proc_init(void)
{
	int i;
	int retval;

	ksmbd_proc_fs = proc_mkdir("fs/ksmbd", NULL);
	if (!ksmbd_proc_fs)
		return;

	if (!proc_mkdir_mode("sessions", 0400, ksmbd_proc_fs))
		goto err_out;

	for (i = 0; i < ARRAY_SIZE(ksmbd_counters.counters); i++) {
		retval = percpu_counter_init(&ksmbd_counters.counters[i], 0, GFP_KERNEL);
		if (retval)
			goto err_out;
	}

	if (!ksmbd_proc_create("server", proc_show_ksmbd_stats, NULL))
		goto err_out;

	ksmbd_proc_reset();
	return;
err_out:
	ksmbd_proc_cleanup();
}
