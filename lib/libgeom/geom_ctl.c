/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Poul-Henning Kamp
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <fcntl.h>
#include <errno.h>
#include <paths.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GCTL_TABLE 1
#include <libgeom.h>

/*
 * Global pointer to a string that is used to avoid an errorneous free in
 * gctl_free.
 */
static char nomemmsg[] = "Could not allocate memory";

void
gctl_dump(struct gctl_req *req, FILE *f)
{
	unsigned int i;
	int j;
	struct gctl_req_arg *ap;

	if (req == NULL) {
		fprintf(f, "Dump of gctl request at NULL\n");
		return;
	}
	fprintf(f, "Dump of gctl request at %p:\n", req);
	if (req->error != NULL)
		fprintf(f, "  error:\t\"%s\"\n", req->error);
	else
		fprintf(f, "  error:\tNULL\n");
	for (i = 0; i < req->narg; i++) {
		ap = &req->arg[i];
		fprintf(f, "  param:\t\"%s\" (%d)", ap->name, ap->nlen);
		fprintf(f, " [%s%s",
		    ap->flag & GCTL_PARAM_RD ? "R" : "",
		    ap->flag & GCTL_PARAM_WR ? "W" : "");
		fflush(f);
		if (ap->flag & GCTL_PARAM_ASCII)
			fprintf(f, "%d] = \"%s\"", ap->len, (char *)ap->value);
		else if (ap->len > 0) {
			fprintf(f, "%d] = ", ap->len);
			fflush(f);
			for (j = 0; j < ap->len; j++) {
				fprintf(f, " %02x", ((u_char *)ap->value)[j]);
			}
		} else {
			fprintf(f, "0] = %p", ap->value);
		}
		fprintf(f, "\n");
	}
}

/*
 * Set an error message, if one does not already exist.
 */
static void
gctl_set_error(struct gctl_req *req, const char *error, ...)
{
	va_list ap;

	if (req->error != NULL)
		return;
	va_start(ap, error);
	vasprintf(&req->error, error, ap);
	va_end(ap);
}

/*
 * Check that a malloc operation succeeded, and set a consistent error
 * message if not.
 */
static void
gctl_check_alloc(struct gctl_req *req, void *ptr)
{

	if (ptr != NULL)
		return;
	gctl_set_error(req, nomemmsg);
	if (req->error == NULL)
		req->error = nomemmsg;
}

/*
 * Allocate a new request handle of the specified type.
 * XXX: Why bother checking the type ?
 */
struct gctl_req *
gctl_get_handle(void)
{

	return (calloc(1, sizeof(struct gctl_req)));
}

/*
 * Allocate space for another argument.
 */
static struct gctl_req_arg *
gctl_new_arg(struct gctl_req *req)
{
	struct gctl_req_arg *ap;

	req->narg++;
	req->arg = reallocf(req->arg, sizeof *ap * req->narg);
	gctl_check_alloc(req, req->arg);
	if (req->arg == NULL) {
		req->narg = 0;
		return (NULL);
	}
	ap = req->arg + (req->narg - 1);
	memset(ap, 0, sizeof *ap);
	return (ap);
}

static void
gctl_param_add(struct gctl_req *req, const char *name, int len, void *value,
    int flag)
{
	struct gctl_req_arg *ap;

	if (req == NULL || req->error != NULL)
		return;
	ap = gctl_new_arg(req);
	if (ap == NULL)
		return;
	ap->name = strdup(name);
	gctl_check_alloc(req, ap->name);
	if (ap->name == NULL)
		return;
	ap->nlen = strlen(ap->name) + 1;
	ap->value = value;
	ap->flag = flag;
	if (len >= 0)
		ap->len = len;
	else if (len < 0) {
		ap->flag |= GCTL_PARAM_ASCII;
		ap->len = strlen(value) + 1;
	}
}

void
gctl_ro_param(struct gctl_req *req, const char *name, int len, const void* value)
{

	gctl_param_add(req, name, len, __DECONST(void *, value), GCTL_PARAM_RD);
}

void
gctl_rw_param(struct gctl_req *req, const char *name, int len, void *value)
{

	gctl_param_add(req, name, len, value, GCTL_PARAM_RW);
}

const char *
gctl_issue(struct gctl_req *req)
{
	int fd, error;

	if (req == NULL)
		return ("NULL request pointer");
	if (req->error != NULL)
		return (req->error);

	req->version = GCTL_VERSION;
	req->lerror = BUFSIZ;		/* XXX: arbitrary number */
	req->error = calloc(1, req->lerror);
	if (req->error == NULL) {
		gctl_check_alloc(req, req->error);
		return (req->error);
	}
	req->lerror--;
	fd = open(_PATH_DEV PATH_GEOM_CTL, O_RDONLY);
	if (fd < 0)
		return(strerror(errno));
	error = ioctl(fd, GEOM_CTL, req);
	close(fd);
	if (req->error[0] != '\0')
		return (req->error);
	if (error != 0)
		return(strerror(errno));
	return (NULL);
}

void
gctl_free(struct gctl_req *req)
{
	unsigned int i;

	if (req == NULL)
		return;
	for (i = 0; i < req->narg; i++) {
		if (req->arg[i].name != NULL)
			free(req->arg[i].name);
	}
	free(req->arg);
	if (req->error != NULL && req->error != nomemmsg)
		free(req->error);
	free(req);
}
