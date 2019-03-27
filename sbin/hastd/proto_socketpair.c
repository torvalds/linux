/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pjdlog.h"
#include "proto_impl.h"

#define	SP_CTX_MAGIC	0x50c3741
struct sp_ctx {
	int			sp_magic;
	int			sp_fd[2];
	int			sp_side;
#define	SP_SIDE_UNDEF		0
#define	SP_SIDE_CLIENT		1
#define	SP_SIDE_SERVER		2
};

static void sp_close(void *ctx);

static int
sp_client(const char *srcaddr, const char *dstaddr, void **ctxp)
{
	struct sp_ctx *spctx;
	int ret;

	if (strcmp(dstaddr, "socketpair://") != 0)
		return (-1);

	PJDLOG_ASSERT(srcaddr == NULL);

	spctx = malloc(sizeof(*spctx));
	if (spctx == NULL)
		return (errno);

	if (socketpair(PF_UNIX, SOCK_STREAM, 0, spctx->sp_fd) == -1) {
		ret = errno;
		free(spctx);
		return (ret);
	}

	spctx->sp_side = SP_SIDE_UNDEF;
	spctx->sp_magic = SP_CTX_MAGIC;
	*ctxp = spctx;

	return (0);
}

static int
sp_send(void *ctx, const unsigned char *data, size_t size, int fd)
{
	struct sp_ctx *spctx = ctx;
	int sock;

	PJDLOG_ASSERT(spctx != NULL);
	PJDLOG_ASSERT(spctx->sp_magic == SP_CTX_MAGIC);

	switch (spctx->sp_side) {
	case SP_SIDE_UNDEF:
		/*
		 * If the first operation done by the caller is proto_send(),
		 * we assume this is the client.
		 */
		/* FALLTHROUGH */
		spctx->sp_side = SP_SIDE_CLIENT;
		/* Close other end. */
		close(spctx->sp_fd[1]);
		spctx->sp_fd[1] = -1;
	case SP_SIDE_CLIENT:
		PJDLOG_ASSERT(spctx->sp_fd[0] >= 0);
		sock = spctx->sp_fd[0];
		break;
	case SP_SIDE_SERVER:
		PJDLOG_ASSERT(spctx->sp_fd[1] >= 0);
		sock = spctx->sp_fd[1];
		break;
	default:
		PJDLOG_ABORT("Invalid socket side (%d).", spctx->sp_side);
	}

	/* Someone is just trying to decide about side. */
	if (data == NULL)
		return (0);

	return (proto_common_send(sock, data, size, fd));
}

static int
sp_recv(void *ctx, unsigned char *data, size_t size, int *fdp)
{
	struct sp_ctx *spctx = ctx;
	int fd;

	PJDLOG_ASSERT(spctx != NULL);
	PJDLOG_ASSERT(spctx->sp_magic == SP_CTX_MAGIC);

	switch (spctx->sp_side) {
	case SP_SIDE_UNDEF:
		/*
		 * If the first operation done by the caller is proto_recv(),
		 * we assume this is the server.
		 */
		/* FALLTHROUGH */
		spctx->sp_side = SP_SIDE_SERVER;
		/* Close other end. */
		close(spctx->sp_fd[0]);
		spctx->sp_fd[0] = -1;
	case SP_SIDE_SERVER:
		PJDLOG_ASSERT(spctx->sp_fd[1] >= 0);
		fd = spctx->sp_fd[1];
		break;
	case SP_SIDE_CLIENT:
		PJDLOG_ASSERT(spctx->sp_fd[0] >= 0);
		fd = spctx->sp_fd[0];
		break;
	default:
		PJDLOG_ABORT("Invalid socket side (%d).", spctx->sp_side);
	}

	/* Someone is just trying to decide about side. */
	if (data == NULL)
		return (0);

	return (proto_common_recv(fd, data, size, fdp));
}

static int
sp_descriptor(const void *ctx)
{
	const struct sp_ctx *spctx = ctx;

	PJDLOG_ASSERT(spctx != NULL);
	PJDLOG_ASSERT(spctx->sp_magic == SP_CTX_MAGIC);
	PJDLOG_ASSERT(spctx->sp_side == SP_SIDE_CLIENT ||
	    spctx->sp_side == SP_SIDE_SERVER);

	switch (spctx->sp_side) {
	case SP_SIDE_CLIENT:
		PJDLOG_ASSERT(spctx->sp_fd[0] >= 0);
		return (spctx->sp_fd[0]);
	case SP_SIDE_SERVER:
		PJDLOG_ASSERT(spctx->sp_fd[1] >= 0);
		return (spctx->sp_fd[1]);
	}

	PJDLOG_ABORT("Invalid socket side (%d).", spctx->sp_side);
}

static void
sp_close(void *ctx)
{
	struct sp_ctx *spctx = ctx;

	PJDLOG_ASSERT(spctx != NULL);
	PJDLOG_ASSERT(spctx->sp_magic == SP_CTX_MAGIC);

	switch (spctx->sp_side) {
	case SP_SIDE_UNDEF:
		PJDLOG_ASSERT(spctx->sp_fd[0] >= 0);
		close(spctx->sp_fd[0]);
		spctx->sp_fd[0] = -1;
		PJDLOG_ASSERT(spctx->sp_fd[1] >= 0);
		close(spctx->sp_fd[1]);
		spctx->sp_fd[1] = -1;
		break;
	case SP_SIDE_CLIENT:
		PJDLOG_ASSERT(spctx->sp_fd[0] >= 0);
		close(spctx->sp_fd[0]);
		spctx->sp_fd[0] = -1;
		PJDLOG_ASSERT(spctx->sp_fd[1] == -1);
		break;
	case SP_SIDE_SERVER:
		PJDLOG_ASSERT(spctx->sp_fd[1] >= 0);
		close(spctx->sp_fd[1]);
		spctx->sp_fd[1] = -1;
		PJDLOG_ASSERT(spctx->sp_fd[0] == -1);
		break;
	default:
		PJDLOG_ABORT("Invalid socket side (%d).", spctx->sp_side);
	}

	spctx->sp_magic = 0;
	free(spctx);
}

static struct proto sp_proto = {
	.prt_name = "socketpair",
	.prt_client = sp_client,
	.prt_send = sp_send,
	.prt_recv = sp_recv,
	.prt_descriptor = sp_descriptor,
	.prt_close = sp_close
};

static __constructor void
sp_ctor(void)
{

	proto_register(&sp_proto, false);
}
