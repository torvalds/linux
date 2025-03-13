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
	struct ksmbd_login_response_ext *resp_ext = NULL;
	struct ksmbd_user *user = NULL;

	resp = ksmbd_ipc_login_request(account);
	if (!resp)
		return NULL;

	if (!(resp->status & KSMBD_USER_FLAG_OK))
		goto out;

	if (resp->status & KSMBD_USER_FLAG_EXTENSION)
		resp_ext = ksmbd_ipc_login_request_ext(account);

	user = ksmbd_alloc_user(resp, resp_ext);
out:
	kvfree(resp);
	return user;
}

struct ksmbd_user *ksmbd_alloc_user(struct ksmbd_login_response *resp,
		struct ksmbd_login_response_ext *resp_ext)
{
	struct ksmbd_user *user;

	user = kmalloc(sizeof(struct ksmbd_user), KSMBD_DEFAULT_GFP);
	if (!user)
		return NULL;

	user->name = kstrdup(resp->account, KSMBD_DEFAULT_GFP);
	user->flags = resp->status;
	user->gid = resp->gid;
	user->uid = resp->uid;
	user->passkey_sz = resp->hash_sz;
	user->passkey = kmalloc(resp->hash_sz, KSMBD_DEFAULT_GFP);
	if (user->passkey)
		memcpy(user->passkey, resp->hash, resp->hash_sz);

	user->ngroups = 0;
	user->sgid = NULL;

	if (!user->name || !user->passkey)
		goto err_free;

	if (resp_ext) {
		if (resp_ext->ngroups > NGROUPS_MAX) {
			pr_err("ngroups(%u) from login response exceeds max groups(%d)\n",
					resp_ext->ngroups, NGROUPS_MAX);
			goto err_free;
		}

		user->sgid = kmemdup(resp_ext->____payload,
				     resp_ext->ngroups * sizeof(gid_t),
				     KSMBD_DEFAULT_GFP);
		if (!user->sgid)
			goto err_free;

		user->ngroups = resp_ext->ngroups;
		ksmbd_debug(SMB, "supplementary groups : %d\n", user->ngroups);
	}

	return user;

err_free:
	kfree(user->name);
	kfree(user->passkey);
	kfree(user);
	return NULL;
}

void ksmbd_free_user(struct ksmbd_user *user)
{
	ksmbd_ipc_logout_request(user->name, user->flags);
	kfree(user->sgid);
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
