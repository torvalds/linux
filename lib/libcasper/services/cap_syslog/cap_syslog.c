/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#include <sys/dnv.h>
#include <sys/nv.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "cap_syslog.h"

#define	CAP_SYSLOG_LIMIT	2048

void
cap_syslog(cap_channel_t *chan, int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	cap_vsyslog(chan, pri, fmt, ap);
	va_end(ap);
}

void
cap_vsyslog(cap_channel_t *chan, int priority, const char *fmt, va_list ap)
{
	nvlist_t *nvl;
	char message[CAP_SYSLOG_LIMIT];

	(void)vsnprintf(message, sizeof(message), fmt, ap);

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "vsyslog");
	nvlist_add_number(nvl, "priority", priority);
	nvlist_add_string(nvl, "message", message);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL) {
		return;
	}
	nvlist_destroy(nvl);
}

void
cap_openlog(cap_channel_t *chan, const char *ident, int logopt, int facility)
{
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "openlog");
	if (ident != NULL) {
		nvlist_add_string(nvl, "ident", ident);
	}
	nvlist_add_number(nvl, "logopt", logopt);
	nvlist_add_number(nvl, "facility", facility);
	if (logopt & LOG_PERROR) {
		nvlist_add_descriptor(nvl, "stderr", STDERR_FILENO);
	}
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL) {
		return;
	}
	nvlist_destroy(nvl);
}

void
cap_closelog(cap_channel_t *chan)
{
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "closelog");
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL) {
		return;
	}
	nvlist_destroy(nvl);
}

int
cap_setlogmask(cap_channel_t *chan, int maskpri)
{
	nvlist_t *nvl;
	int omask;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "setlogmask");
	nvlist_add_number(nvl, "maskpri", maskpri);
	nvl = cap_xfer_nvlist(chan, nvl);
	omask = nvlist_get_number(nvl, "omask");

	nvlist_destroy(nvl);

	return (omask);
}

/*
 * Service functions.
 */

static char *LogTag;
static int prev_stderr = -1;

static void
slog_vsyslog(const nvlist_t *limits __unused, const nvlist_t *nvlin,
    nvlist_t *nvlout __unused)
{

	syslog(nvlist_get_number(nvlin, "priority"), "%s",
	    nvlist_get_string(nvlin, "message"));
}

static void
slog_openlog(const nvlist_t *limits __unused, const nvlist_t *nvlin,
    nvlist_t *nvlout __unused)
{
	const char *ident;
	uint64_t logopt;
	int stderr_fd;

	ident = dnvlist_get_string(nvlin, "ident", NULL);
	if (ident != NULL) {
		free(LogTag);
		LogTag = strdup(ident);
	}

	logopt = nvlist_get_number(nvlin, "logopt");
	if (logopt & LOG_PERROR) {
		stderr_fd = dnvlist_get_descriptor(nvlin, "stderr", -1);
		if (prev_stderr == -1)
			prev_stderr = dup(STDERR_FILENO);
		if (prev_stderr != -1)
			(void)dup2(stderr_fd, STDERR_FILENO);
	} else if (prev_stderr != -1) {
		(void)dup2(prev_stderr, STDERR_FILENO);
		close(prev_stderr);
		prev_stderr = -1;
	}
	openlog(LogTag, logopt, nvlist_get_number(nvlin, "facility"));
}

static void
slog_closelog(const nvlist_t *limits __unused, const nvlist_t *nvlin __unused,
    nvlist_t *nvlout __unused)
{

	closelog();

	free(LogTag);
	LogTag = NULL;

	if (prev_stderr != -1) {
		(void)dup2(prev_stderr, STDERR_FILENO);
		close(prev_stderr);
		prev_stderr = -1;
	}
}

static void
slog_setlogmask(const nvlist_t *limits __unused, const nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	int omask;

	omask = setlogmask(nvlist_get_number(nvlin, "maskpri"));
	nvlist_add_number(nvlout, "omask", omask);
}

static int
syslog_command(const char *cmd, const nvlist_t *limits, nvlist_t *nvlin,
    nvlist_t *nvlout)
{

	if (strcmp(cmd, "vsyslog") == 0) {
		slog_vsyslog(limits, nvlin, nvlout);
	} else if (strcmp(cmd, "openlog") == 0) {
		slog_openlog(limits, nvlin, nvlout);
	} else if (strcmp(cmd, "closelog") == 0) {
		slog_closelog(limits, nvlin, nvlout);
	} else if (strcmp(cmd, "setlogmask") == 0) {
		slog_setlogmask(limits, nvlin, nvlout);
	} else {
		return (EINVAL);
	}

	return (0);
}

CREATE_SERVICE("system.syslog", NULL, syslog_command, 0);
