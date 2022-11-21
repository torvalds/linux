// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/slab.h>
#include <linux/mm.h>

#include "user_config.h"
#include "../transport_ipc.h"

struct ksmbd_user *ksmbd_login_user(const char *account)
{
	struct ksmbd_login_response *resp;
	struct ksmbd_user *user = NULL;

	resp = ksmbd_ipc_login_request(account);
	if (!resp)
		return NULL;

	if (!(resp->status & KSMBD_USER_FLAG_OK))
		goto out;

	user = ksmbd_alloc_user(resp);
out:
	kvfree(resp);
	return user;
}

struct ksmbd_user *ksmbd_alloc_user(struct ksmbd_login_response *resp)
{
	struct ksmbd_user *user = NULL;

	user = kmalloc(sizeof(struct ksmbd_user), GFP_KERNEL);
	if (!user)
		return NULL;

	user->name = kstrdup(resp->account, GFP_KERNEL);
	user->flags = resp->status;
	user->gid = resp->gid;
	user->uid = resp->uid;
	user->passkey_sz = resp->hash_sz;
	user->passkey = kmalloc(resp->hash_sz, GFP_KERNEL);
	if (user->passkey)
		memcpy(user->passkey, resp->hash, resp->hash_sz);

	if (!user->name || !user->passkey) {
		kfree(user->name);
		kfree(user->passkey);
		kfree(user);
		user = NULL;
	}
	return user;
}

void ksmbd_free_user(struct ksmbd_user *user)
{
	ksmbd_ipc_logout_request(user->name, user->flags);
	kfree(user->name);
	kfree(user->passkey);
	kfree(user);
}

int ksmbd_anonymous_user(struct ksmbd_user *user)
{
	if (user->name[0] == '\0')
		return 1;
	return 0;
}

bool ksmbd_compare_user(struct ksmbd_user *u1, struct ksmbd_user *u2)
{
	if (strcmp(u1->name, u2->name))
		return false;
	if (memcmp(u1->passkey, u2->passkey, u1->passkey_sz))
		return false;

	return true;
}
