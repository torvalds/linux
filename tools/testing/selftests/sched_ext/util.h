/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <void@manifault.com>
 */

#ifndef __SCX_TEST_UTIL_H__
#define __SCX_TEST_UTIL_H__

long file_read_long(const char *path);
int file_write_long(const char *path, long val);

#endif // __SCX_TEST_H__
