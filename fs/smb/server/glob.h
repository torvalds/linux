/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_GLOB_H
#define __KSMBD_GLOB_H

#include <linux/ctype.h>

#include "unicode.h"
#include "vfs_cache.h"

#define KSMBD_VERSION	"3.4.2"

extern int ksmbd_debug_types;

#define KSMBD_DEBUG_SMB		BIT(0)
#define KSMBD_DEBUG_AUTH	BIT(1)
#define KSMBD_DEBUG_VFS		BIT(2)
#define KSMBD_DEBUG_OPLOCK      BIT(3)
#define KSMBD_DEBUG_IPC         BIT(4)
#define KSMBD_DEBUG_CONN        BIT(5)
#define KSMBD_DEBUG_RDMA        BIT(6)
#define KSMBD_DEBUG_ALL         (KSMBD_DEBUG_SMB | KSMBD_DEBUG_AUTH |	\
				KSMBD_DEBUG_VFS | KSMBD_DEBUG_OPLOCK |	\
				KSMBD_DEBUG_IPC | KSMBD_DEBUG_CONN |	\
				KSMBD_DEBUG_RDMA)

#ifdef pr_fmt
#undef pr_fmt
#endif

#ifdef SUBMOD_NAME
#define pr_fmt(fmt)	"ksmbd: " SUBMOD_NAME ": " fmt
#else
#define pr_fmt(fmt)	"ksmbd: " fmt
#endif

#define ksmbd_debug(type, fmt, ...)				\
	do {							\
		if (ksmbd_debug_types & KSMBD_DEBUG_##type)	\
			pr_info(fmt, ##__VA_ARGS__);		\
	} while (0)

#define UNICODE_LEN(x)		((x) * 2)

#endif /* __KSMBD_GLOB_H */
