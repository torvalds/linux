/*	$OpenBSD: bgplgd.c,v 1.4 2024/12/03 10:38:06 claudio Exp $ */
/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bgplgd.h"

#define NCMDARGS	5
#define OMETRIC_TYPE	\
	    "application/openmetrics-text; version=1.0.0; charset=utf-8"

const struct cmd {
	const char	*path;
	char		*args[NCMDARGS];
	unsigned int	qs_mask;
	int		barenbr;
	const char	*content_type;
} cmds[] = {
	{ "/interfaces", { "show", "interfaces", NULL }, 0 },
	{ "/memory", { "show", "rib", "memory", NULL }, 0 },
	{ "/neighbors", { "show", "neighbor", NULL }, QS_MASK_NEIGHBOR, 1 },
	{ "/nexthops", { "show", "nexthop", NULL }, 0 },
	{ "/rib", { "show", "rib", "detail", NULL }, QS_MASK_RIB },
	{ "/rib/in", { "show", "rib", "in", "detail", NULL }, QS_MASK_ADJRIB },
	{ "/rib/out", { "show", "rib", "out", "detail", NULL }, QS_MASK_ADJRIB },
	{ "/rtr", { "show", "rtr", NULL }, 0 },
	{ "/sets", { "show", "sets", NULL }, 0 },
	{ "/summary", { "show", NULL }, 0 },
	{ "/metrics", { "show", "metrics", NULL }, 0, 0, OMETRIC_TYPE },
	{ NULL }
};

static int
command_from_path(const char *path, struct lg_ctx *ctx)
{
	size_t i;

	for (i = 0; cmds[i].path != NULL; i++) {
		if (strcmp(cmds[i].path, path) == 0) {
			ctx->command = &cmds[i];
			ctx->qs_mask = cmds[i].qs_mask;
			return 0;
		}
	}
	return 404;
}

/*
 * Prepare a request into a context to call bgpctl.
 * Parse method, path and querystring. On failure return the correct
 * HTTP error code. On success 0 is returned.
 */
int
prep_request(struct lg_ctx *ctx, const char *meth, const char *path,
    const char *qs)
{
	if (meth == NULL || path == NULL)
		return 500;
	if (strcmp(meth, "GET") != 0)
		return 405;
	if (command_from_path(path, ctx) != 0)
		return 404;
	if (parse_querystring(qs, ctx) != 0)
		return 400;

	return 0;
}

/*
 * Entry point from the FastCGI handler.
 * This runs as an own process and must use STDOUT and STDERR.
 * The log functions should no longer be used here.
 */
void
bgpctl_call(struct lg_ctx *ctx)
{
	char *argv[64];
	size_t i, argc = 0;

	argv[argc++] = bgpctlpath;
	argv[argc++] = "-j";
	argv[argc++] = "-s";
	argv[argc++] = bgpctlsock;

	for (i = 0; ctx->command->args[i] != NULL; i++)
		argv[argc++] = ctx->command->args[i];

	argc = qs_argv(argv, argc, sizeof(argv) / sizeof(argv[0]), ctx,
	    ctx->command->barenbr);

	argv[argc++] = NULL;

	signal(SIGPIPE, SIG_DFL);

	/* Write server header first */
	if (ctx->command->content_type == NULL)
		printf("Content-type: application/json\r\n\r\n");
	else
		printf("Content-type: %s\r\n\r\n", ctx->command->content_type);
	fflush(stdout);

	execvp(bgpctlpath, argv);

	err(1, "failed to execute %s", bgpctlpath);
}
