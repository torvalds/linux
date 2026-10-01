/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SELFTEST_FUSE_COMMON_H__
#define __SELFTEST_FUSE_COMMON_H__

#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>

#define MAX_ERR_MSG 256

#define MOUNTPOINT_TEMPLATE "/tmp/fuse_test_XXXXXX"
#define MOUNTPOINT_SZ 64

struct fuse_common_ctx {
	struct fuse_session *se;
	char mountpoint[MOUNTPOINT_SZ];
	pthread_t thread;
};

int fs_setup(const struct fuse_lowlevel_ops *fs_ops,
	     struct fuse_common_ctx *ctx, char *err);
void fs_teardown(struct fuse_common_ctx *ctx);

#endif /* __SELFTEST_FUSE_COMMON_H__ */
