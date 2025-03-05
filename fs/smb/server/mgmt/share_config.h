/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __SHARE_CONFIG_MANAGEMENT_H__
#define __SHARE_CONFIG_MANAGEMENT_H__

#include <linux/workqueue.h>
#include <linux/hashtable.h>
#include <linux/path.h>
#include <linux/unicode.h>

struct ksmbd_work;

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

static inline umode_t
share_config_create_mode(struct ksmbd_share_config *share,
			 umode_t posix_mode)
{
	umode_t mode = (posix_mode ?: (umode_t)-1) & share->create_mask;

	return mode | share->force_create_mode;
}

static inline umode_t
share_config_directory_mode(struct ksmbd_share_config *share,
			    umode_t posix_mode)
{
	umode_t mode = (posix_mode ?: (umode_t)-1) & share->directory_mask;

	return mode | share->force_directory_mode;
}

static inline int test_share_config_flag(struct ksmbd_share_config *share,
					 int flag)
{
	return share->flags & flag;
}

void ksmbd_share_config_del(struct ksmbd_share_config *share);
void __ksmbd_share_config_put(struct ksmbd_share_config *share);

static inline void ksmbd_share_config_put(struct ksmbd_share_config *share)
{
	if (!atomic_dec_and_test(&share->refcount))
		return;
	__ksmbd_share_config_put(share);
}

struct ksmbd_share_config *ksmbd_share_config_get(struct ksmbd_work *work,
						  const char *name);
bool ksmbd_share_veto_filename(struct ksmbd_share_config *share,
			       const char *filename);
#endif /* __SHARE_CONFIG_MANAGEMENT_H__ */
