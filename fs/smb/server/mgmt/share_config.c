// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/parser.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include "share_config.h"
#include "user_config.h"
#include "user_session.h"
#include "../connection.h"
#include "../transport_ipc.h"
#include "../misc.h"

#define SHARE_HASH_BITS		12
static DEFINE_HASHTABLE(shares_table, SHARE_HASH_BITS);
static DECLARE_RWSEM(shares_table_lock);

struct ksmbd_veto_pattern {
	char			*pattern;
	struct list_head	list;
};

#ifdef CONFIG_PROC_FS
static const struct ksmbd_const_name ksmbd_share_flag_names[] = {
	{KSMBD_SHARE_FLAG_AVAILABLE, "available"},
	{KSMBD_SHARE_FLAG_BROWSEABLE, "browseable"},
	{KSMBD_SHARE_FLAG_WRITEABLE, "writeable"},
	{KSMBD_SHARE_FLAG_READONLY, "read-only"},
	{KSMBD_SHARE_FLAG_GUEST_OK, "guest-ok"},
	{KSMBD_SHARE_FLAG_GUEST_ONLY, "guest-only"},
	{KSMBD_SHARE_FLAG_STORE_DOS_ATTRS, "store-dos-attrs"},
	{KSMBD_SHARE_FLAG_OPLOCKS, "oplocks"},
	{KSMBD_SHARE_FLAG_PIPE, "pipe"},
	{KSMBD_SHARE_FLAG_HIDE_DOT_FILES, "hide-dot-files"},
	{KSMBD_SHARE_FLAG_INHERIT_OWNER, "inherit-owner"},
	{KSMBD_SHARE_FLAG_STREAMS, "streams"},
	{KSMBD_SHARE_FLAG_FOLLOW_SYMLINKS, "follow-symlinks"},
	{KSMBD_SHARE_FLAG_ACL_XATTR, "acl-xattr"},
	{KSMBD_SHARE_FLAG_UPDATE, "update"},
	{KSMBD_SHARE_FLAG_CROSSMNT, "crossmnt"},
	{KSMBD_SHARE_FLAG_CONTINUOUS_AVAILABILITY, "continuous-availability"},
};

static int proc_show_shares(struct seq_file *m, void *v)
{
	struct ksmbd_share_config *share;
	int i;

	down_read(&shares_table_lock);
	hash_for_each(shares_table, i, share, hlist) {
		seq_printf(m, "name:\t%s\n", share->name);
		seq_printf(m, "type:\t%s\n",
			   test_share_config_flag(share, KSMBD_SHARE_FLAG_PIPE) ?
			   "pipe" : "disk");
		seq_printf(m, "tree_connects:\t%d\n",
			   atomic_read(&share->tree_connections));
		seq_printf(m, "file_mask:\t0%07o\n", share->create_mask);
		seq_printf(m, "directory_mask:\t0%07o\n", share->directory_mask);
		seq_puts(m, "flags:\t");
		ksmbd_proc_show_flag_names(m, ksmbd_share_flag_names,
					   ARRAY_SIZE(ksmbd_share_flag_names),
					   share->flags);
		seq_puts(m, "\n\n");
	}
	up_read(&shares_table_lock);
	return 0;
}

int create_proc_shares(void)
{
	if (!ksmbd_proc_create("shares", proc_show_shares, NULL))
		return -ENOMEM;
	return 0;
}
#else
int create_proc_shares(void) { return 0; }
#endif

static unsigned int share_name_hash(const char *name)
{
	return jhash(name, strlen(name), 0);
}

static void kill_share(struct ksmbd_share_config *share)
{
	while (!list_empty(&share->veto_list)) {
		struct ksmbd_veto_pattern *p;

		p = list_entry(share->veto_list.next,
			       struct ksmbd_veto_pattern,
			       list);
		list_del(&p->list);
		kfree(p->pattern);
		kfree(p);
	}

	if (share->path)
		path_put(&share->vfs_path);
	kfree(share->name);
	kfree(share->path);
	kfree(share);
}

void ksmbd_share_config_del(struct ksmbd_share_config *share)
{
	down_write(&shares_table_lock);
	hash_del(&share->hlist);
	up_write(&shares_table_lock);
}

void __ksmbd_share_config_put(struct ksmbd_share_config *share)
{
	ksmbd_share_config_del(share);
	kill_share(share);
}

static struct ksmbd_share_config *
__get_share_config(struct ksmbd_share_config *share)
{
	if (!atomic_inc_not_zero(&share->refcount))
		return NULL;
	return share;
}

static struct ksmbd_share_config *__share_lookup(const char *name)
{
	struct ksmbd_share_config *share;
	unsigned int key = share_name_hash(name);

	hash_for_each_possible(shares_table, share, hlist, key) {
		if (!strcmp(name, share->name))
			return share;
	}
	return NULL;
}

static int parse_veto_list(struct ksmbd_share_config *share,
			   char *veto_list,
			   int veto_list_sz)
{
	int sz = 0;

	if (!veto_list_sz)
		return 0;

	while (veto_list_sz > 0) {
		struct ksmbd_veto_pattern *p;

		sz = strlen(veto_list);
		if (!sz)
			break;

		p = kzalloc_obj(struct ksmbd_veto_pattern, KSMBD_DEFAULT_GFP);
		if (!p)
			return -ENOMEM;

		p->pattern = kstrdup(veto_list, KSMBD_DEFAULT_GFP);
		if (!p->pattern) {
			kfree(p);
			return -ENOMEM;
		}

		list_add(&p->list, &share->veto_list);

		veto_list += sz + 1;
		veto_list_sz -= (sz + 1);
	}

	return 0;
}

static struct ksmbd_share_config *share_config_request(struct ksmbd_work *work,
						       const char *name)
{
	struct ksmbd_share_config_response *resp;
	struct ksmbd_share_config *share = NULL;
	struct ksmbd_share_config *lookup;
	struct unicode_map *um = work->conn->um;
	int ret;

	resp = ksmbd_ipc_share_config_request(name);
	if (!resp)
		return NULL;

	if (resp->flags == KSMBD_SHARE_FLAG_INVALID)
		goto out;

	if (*resp->share_name) {
		char *cf_resp_name;
		bool equal;

		cf_resp_name = ksmbd_casefold_sharename(um, resp->share_name);
		if (IS_ERR(cf_resp_name))
			goto out;
		equal = !strcmp(cf_resp_name, name);
		kfree(cf_resp_name);
		if (!equal)
			goto out;
	}

	share = kzalloc_obj(struct ksmbd_share_config, KSMBD_DEFAULT_GFP);
	if (!share)
		goto out;

	share->flags = resp->flags;
	atomic_set(&share->refcount, 1);
	ksmbd_share_tree_conn_init(share);
	INIT_LIST_HEAD(&share->veto_list);
	share->name = kstrdup(name, KSMBD_DEFAULT_GFP);

	if (!test_share_config_flag(share, KSMBD_SHARE_FLAG_PIPE)) {
		int path_len = PATH_MAX;

		if (resp->payload_sz)
			path_len = resp->payload_sz - resp->veto_list_sz;

		share->path = kstrndup(ksmbd_share_config_path(resp), path_len,
				      KSMBD_DEFAULT_GFP);
		if (!share->path) {
			ret = -ENOMEM;
		} else {
			ret = 0;
			share->path_sz = strlen(share->path);
			while (share->path_sz > 1 &&
			       share->path[share->path_sz - 1] == '/')
				share->path[--share->path_sz] = '\0';
		}
		share->create_mask = resp->create_mask;
		share->directory_mask = resp->directory_mask;
		share->force_create_mode = resp->force_create_mode;
		share->force_directory_mode = resp->force_directory_mode;
		share->force_uid = resp->force_uid;
		share->force_gid = resp->force_gid;
		if (!ret)
			ret = parse_veto_list(share,
					      KSMBD_SHARE_CONFIG_VETO_LIST(resp),
					      resp->veto_list_sz);
		if (!ret && share->path) {
			if (__ksmbd_override_fsids(work, share)) {
				kill_share(share);
				share = NULL;
				goto out;
			}

			ret = kern_path(share->path, 0, &share->vfs_path);
			ksmbd_revert_fsids(work);
			if (ret) {
				ksmbd_debug(SMB, "failed to access '%s'\n",
					    share->path);
				/* Avoid put_path() */
				kfree(share->path);
				share->path = NULL;
			}
		}
		if (ret || !share->name) {
			kill_share(share);
			share = NULL;
			goto out;
		}
	}

	down_write(&shares_table_lock);
	lookup = __share_lookup(name);
	if (lookup)
		lookup = __get_share_config(lookup);
	if (!lookup) {
		hash_add(shares_table, &share->hlist, share_name_hash(name));
	} else {
		kill_share(share);
		share = lookup;
	}
	up_write(&shares_table_lock);

out:
	kvfree(resp);
	return share;
}

struct ksmbd_share_config *ksmbd_share_config_get(struct ksmbd_work *work,
						  const char *name)
{
	struct ksmbd_share_config *share;

	down_read(&shares_table_lock);
	share = __share_lookup(name);
	if (share)
		share = __get_share_config(share);
	up_read(&shares_table_lock);

	if (share)
		return share;
	return share_config_request(work, name);
}

bool ksmbd_share_veto_filename(struct ksmbd_share_config *share,
			       const char *filename)
{
	struct ksmbd_veto_pattern *p;

	list_for_each_entry(p, &share->veto_list, list) {
		if (match_wildcard(p->pattern, filename))
			return true;
	}
	return false;
}
