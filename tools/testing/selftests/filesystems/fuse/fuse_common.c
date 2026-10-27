// SPDX-License-Identifier: GPL-2.0

#include "fuse_common.h"

static void *run_daemon(void *arg)
{
	fuse_session_loop((struct fuse_session *)arg);
	return NULL;
}

int fs_setup(const struct fuse_lowlevel_ops *fs_ops,
	     struct fuse_common_ctx *ctx, char *err)
{
	char *fuse_argv[] = { "fuse_test", NULL };
	struct fuse_args args = FUSE_ARGS_INIT(1, fuse_argv);

	strcpy(ctx->mountpoint, MOUNTPOINT_TEMPLATE);
	if (!mkdtemp(ctx->mountpoint)) {
		snprintf(err, MAX_ERR_MSG, "mkdtemp: %s", strerror(errno));
		return -1;
	}

	ctx->se = fuse_session_new(&args, fs_ops, sizeof(*fs_ops), NULL);
	if (!ctx->se) {
		rmdir(ctx->mountpoint);
		snprintf(err, MAX_ERR_MSG, "fuse_session_new failed");
		return -1;
	}

	if (fuse_session_mount(ctx->se, ctx->mountpoint)) {
		fuse_session_destroy(ctx->se);
		rmdir(ctx->mountpoint);
		snprintf(err, MAX_ERR_MSG, "fuse_session_mount failed "
			"(missing fusermount3 or insufficient privileges)");
		return -1;
	}

	if (pthread_create(&ctx->thread, NULL, run_daemon, ctx->se)) {
		fuse_session_unmount(ctx->se);
		fuse_session_destroy(ctx->se);
		rmdir(ctx->mountpoint);
		snprintf(err, MAX_ERR_MSG, "pthread_create: %s", strerror(errno));
		return -1;
	}

	fuse_opt_free_args(&args);

	return 0;
}

void fs_teardown(struct fuse_common_ctx *ctx)
{
	fuse_session_exit(ctx->se);
	fuse_session_unmount(ctx->se);
	pthread_join(ctx->thread, NULL);
	fuse_session_destroy(ctx->se);
	rmdir(ctx->mountpoint);
}
