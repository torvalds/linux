// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include "ksmbd_ida.h"

struct ksmbd_ida *ksmbd_ida_alloc(void)
{
	struct ksmbd_ida *ida;

	ida = kmalloc(sizeof(struct ksmbd_ida), GFP_KERNEL);
	if (!ida)
		return NULL;

	ida_init(&ida->map);
	return ida;
}

void ksmbd_ida_free(struct ksmbd_ida *ida)
{
	if (!ida)
		return;

	ida_destroy(&ida->map);
	kfree(ida);
}

static inline int __acquire_id(struct ksmbd_ida *ida, int from, int to)
{
	return ida_simple_get(&ida->map, from, to, GFP_KERNEL);
}

int ksmbd_acquire_smb2_tid(struct ksmbd_ida *ida)
{
	int id;

	do {
		id = __acquire_id(ida, 0, 0);
	} while (id == 0xFFFF);

	return id;
}

int ksmbd_acquire_smb2_uid(struct ksmbd_ida *ida)
{
	int id;

	do {
		id = __acquire_id(ida, 1, 0);
	} while (id == 0xFFFE);

	return id;
}

int ksmbd_acquire_async_msg_id(struct ksmbd_ida *ida)
{
	return __acquire_id(ida, 1, 0);
}

int ksmbd_acquire_id(struct ksmbd_ida *ida)
{
	return __acquire_id(ida, 0, 0);
}

void ksmbd_release_id(struct ksmbd_ida *ida, int id)
{
	ida_simple_remove(&ida->map, id);
}
