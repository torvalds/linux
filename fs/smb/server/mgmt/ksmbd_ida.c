// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include "ksmbd_ida.h"

static inline int __acquire_id(struct ida *ida, int from, int to)
{
	return ida_simple_get(ida, from, to, GFP_KERNEL);
}

int ksmbd_acquire_smb2_tid(struct ida *ida)
{
	int id;

	id = __acquire_id(ida, 1, 0xFFFFFFFF);

	return id;
}

int ksmbd_acquire_smb2_uid(struct ida *ida)
{
	int id;

	id = __acquire_id(ida, 1, 0);
	if (id == 0xFFFE)
		id = __acquire_id(ida, 1, 0);

	return id;
}

int ksmbd_acquire_async_msg_id(struct ida *ida)
{
	return __acquire_id(ida, 1, 0);
}

int ksmbd_acquire_id(struct ida *ida)
{
	return __acquire_id(ida, 0, 0);
}

void ksmbd_release_id(struct ida *ida, int id)
{
	ida_simple_remove(ida, id);
}
