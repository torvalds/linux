/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_BUFFER_POOL_H__
#define __KSMBD_BUFFER_POOL_H__

void *ksmbd_find_buffer(size_t size);
void ksmbd_release_buffer(void *buffer);

void *ksmbd_realloc_response(void *ptr, size_t old_sz, size_t new_sz);

void ksmbd_free_file_struct(void *filp);
void *ksmbd_alloc_file_struct(void);

void ksmbd_destroy_buffer_pools(void);
int ksmbd_init_buffer_pools(void);

#endif /* __KSMBD_BUFFER_POOL_H__ */
