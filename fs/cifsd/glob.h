/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_GLOB_H
#define __KSMBD_GLOB_H

#include <linux/ctype.h>
#include <linux/version.h>

#include "unicode.h"
#include "vfs_cache.h"
#include "smberr.h"

#define KSMBD_VERSION	"3.1.9"

/* @FIXME clean up this code */

extern int ksmbd_debug_types;
extern int ksmbd_caseless_search;

#define DATA_STREAM	1
#define DIR_STREAM	2

#define KSMBD_DEBUG_SMB         (1 << 0)
#define KSMBD_DEBUG_AUTH        (1 << 1)
#define KSMBD_DEBUG_VFS         (1 << 2)
#define KSMBD_DEBUG_OPLOCK      (1 << 3)
#define KSMBD_DEBUG_IPC         (1 << 4)
#define KSMBD_DEBUG_CONN        (1 << 5)
#define KSMBD_DEBUG_RDMA        (1 << 6)
#define KSMBD_DEBUG_ALL         (KSMBD_DEBUG_SMB | KSMBD_DEBUG_AUTH |	\
				KSMBD_DEBUG_VFS | KSMBD_DEBUG_OPLOCK |	\
				KSMBD_DEBUG_IPC | KSMBD_DEBUG_CONN |	\
				KSMBD_DEBUG_RDMA)

#ifndef ksmbd_pr_fmt
#ifdef SUBMOD_NAME
#define ksmbd_pr_fmt(fmt)	"ksmbd: " SUBMOD_NAME ": " fmt
#else
#define ksmbd_pr_fmt(fmt)	"ksmbd: " fmt
#endif
#endif

#define ksmbd_debug(type, fmt, ...)				\
	do {							\
		if (ksmbd_debug_types & KSMBD_DEBUG_##type)	\
			pr_info(ksmbd_pr_fmt("%s:%d: " fmt),	\
				__func__,			\
				__LINE__,			\
				##__VA_ARGS__);			\
	} while (0)

#define ksmbd_info(fmt, ...)					\
			pr_info(ksmbd_pr_fmt(fmt), ##__VA_ARGS__)

#define ksmbd_err(fmt, ...)					\
			pr_err(ksmbd_pr_fmt("%s:%d: " fmt),	\
				__func__,			\
				__LINE__,			\
				##__VA_ARGS__)

#define UNICODE_LEN(x)		((x) * 2)

#endif /* __KSMBD_GLOB_H */
