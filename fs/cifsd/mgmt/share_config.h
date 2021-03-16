/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __SHARE_CONFIG_MANAGEMENT_H__
#define __SHARE_CONFIG_MANAGEMENT_H__

#include <linux/workqueue.h>
#include <linux/hashtable.h>
#include <linux/path.h>

#include "../glob.h"  /* FIXME */

struct ksmbd_share_config {
	char			*name;
	char			*path;

	unsigned int		path_sz;
	unsigned int		flags;
	struct list_head	veto_list;

	struct path		vfs_path;

	atomic_t		refcount;
	struct hlist_node	hlist;
	unsigned short		create_mask;
	unsigned short		directory_mask;
	unsigned short		force_create_mode;
	unsigned short		force_directory_mode;
	unsigned short		force_uid;
	unsigned short		force_gid;
};

#define KSMBD_SHARE_INVALID_UID	((__u16)-1)
#define KSMBD_SHARE_INVALID_GID	((__u16)-1)

static inline int share_config_create_mode(struct ksmbd_share_config *share,
	umode_t posix_mode)
{
	if (!share->force_create_mode) {
		if (!posix_mode)
			return share->create_mask;
		else
			return posix_mode & share->create_mask;
	}
	return share->force_create_mode & share->create_mask;
}

static inline int share_config_directory_mode(struct ksmbd_share_config *share,
	umode_t posix_mode)
{
	if (!share->force_directory_mode) {
		if (!posix_mode)
			return share->directory_mask;
		else
			return posix_mode & share->directory_mask;
	}

	return share->force_directory_mode & share->directory_mask;
}

static inline int test_share_config_flag(struct ksmbd_share_config *share,
					 int flag)
{
	return share->flags & flag;
}

extern void __ksmbd_share_config_put(struct ksmbd_share_config *share);

static inline void ksmbd_share_config_put(struct ksmbd_share_config *share)
{
	if (!atomic_dec_and_test(&share->refcount))
		return;
	__ksmbd_share_config_put(share);
}

struct ksmbd_share_config *ksmbd_share_config_get(char *name);
bool ksmbd_share_veto_filename(struct ksmbd_share_config *share,
			       const char *filename);
void ksmbd_share_configs_cleanup(void);

#endif /* __SHARE_CONFIG_MANAGEMENT_H__ */
