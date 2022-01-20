/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __USER_CONFIG_MANAGEMENT_H__
#define __USER_CONFIG_MANAGEMENT_H__

#include "../glob.h"

struct ksmbd_user {
	unsigned short		flags;

	unsigned int		uid;
	unsigned int		gid;

	char			*name;

	size_t			passkey_sz;
	char			*passkey;
	unsigned int		failed_login_count;
};

static inline bool user_guest(struct ksmbd_user *user)
{
	return user->flags & KSMBD_USER_FLAG_GUEST_ACCOUNT;
}

static inline void set_user_flag(struct ksmbd_user *user, int flag)
{
	user->flags |= flag;
}

static inline int test_user_flag(struct ksmbd_user *user, int flag)
{
	return user->flags & flag;
}

static inline void set_user_guest(struct ksmbd_user *user)
{
}

static inline char *user_passkey(struct ksmbd_user *user)
{
	return user->passkey;
}

static inline char *user_name(struct ksmbd_user *user)
{
	return user->name;
}

static inline unsigned int user_uid(struct ksmbd_user *user)
{
	return user->uid;
}

static inline unsigned int user_gid(struct ksmbd_user *user)
{
	return user->gid;
}

struct ksmbd_user *ksmbd_login_user(const char *account);
struct ksmbd_user *ksmbd_alloc_user(struct ksmbd_login_response *resp);
void ksmbd_free_user(struct ksmbd_user *user);
int ksmbd_anonymous_user(struct ksmbd_user *user);
bool ksmbd_compare_user(struct ksmbd_user *u1, struct ksmbd_user *u2);
#endif /* __USER_CONFIG_MANAGEMENT_H__ */
