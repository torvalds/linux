/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SMB2 compression support for ksmbd.
 *
 * Copyright (C) 2026 Namjae Jeon <linkinjeon@kernel.org>
 */
#ifndef __KSMBD_COMPRESS_H__
#define __KSMBD_COMPRESS_H__

#include "connection.h"
#include "../common/compress/compress.h"

int ksmbd_decompress_request(struct ksmbd_conn *conn);
int ksmbd_compress_response(struct ksmbd_work *work);

#endif /* __KSMBD_COMPRESS_H__ */
