/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
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
#include <sys/sysctl.h>
#include <sys/nv.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "cap_sysctl.h"

int
cap_sysctlbyname(cap_channel_t *chan, const char *name, void *oldp,
    size_t *oldlenp, const void *newp, size_t newlen)
{
	nvlist_t *nvl;
	const uint8_t *retoldp;
	uint8_t operation;
	size_t oldlen;

	operation = 0;
	if (oldp != NULL)
		operation |= CAP_SYSCTL_READ;
	if (newp != NULL)
		operation |= CAP_SYSCTL_WRITE;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "sysctl");
	nvlist_add_string(nvl, "name", name);
	nvlist_add_number(nvl, "operation", (uint64_t)operation);
	if (oldp == NULL && oldlenp != NULL)
		nvlist_add_null(nvl, "justsize");
	else if (oldlenp != NULL)
		nvlist_add_number(nvl, "oldlen", (uint64_t)*oldlenp);
	if (newp != NULL)
		nvlist_add_binary(nvl, "newp", newp, newlen);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL)
		return (-1);
	if (nvlist_get_number(nvl, "error") != 0) {
		errno = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (-1);
	}

	if (oldp == NULL && oldlenp != NULL) {
		*oldlenp = (size_t)nvlist_get_number(nvl, "oldlen");
	} else if (oldp != NULL) {
		retoldp = nvlist_get_binary(nvl, "oldp", &oldlen);
		memcpy(oldp, retoldp, oldlen);
		if (oldlenp != NULL)
			*oldlenp = oldlen;
	}
	nvlist_destroy(nvl);

	return (0);
}

/*
 * Service functions.
 */
static int
sysctl_check_one(const nvlist_t *nvl, bool islimit)
{
	const char *name;
	void *cookie;
	int type;
	unsigned int fields;

	/* NULL nvl is of course invalid. */
	if (nvl == NULL)
		return (EINVAL);
	if (nvlist_error(nvl) != 0)
		return (nvlist_error(nvl));

#define	HAS_NAME	0x01
#define	HAS_OPERATION	0x02

	fields = 0;
	cookie = NULL;
	while ((name = nvlist_next(nvl, &type, &cookie)) != NULL) {
		/* We accept only one 'name' and one 'operation' in nvl. */
		if (strcmp(name, "name") == 0) {
			if (type != NV_TYPE_STRING)
				return (EINVAL);
			/* Only one 'name' can be present. */
			if ((fields & HAS_NAME) != 0)
				return (EINVAL);
			fields |= HAS_NAME;
		} else if (strcmp(name, "operation") == 0) {
			uint64_t operation;

			if (type != NV_TYPE_NUMBER)
				return (EINVAL);
			/*
			 * We accept only CAP_SYSCTL_READ and
			 * CAP_SYSCTL_WRITE flags.
			 */
			operation = nvlist_get_number(nvl, name);
			if ((operation & ~(CAP_SYSCTL_RDWR)) != 0)
				return (EINVAL);
			/* ...but there has to be at least one of them. */
			if ((operation & (CAP_SYSCTL_RDWR)) == 0)
				return (EINVAL);
			/* Only one 'operation' can be present. */
			if ((fields & HAS_OPERATION) != 0)
				return (EINVAL);
			fields |= HAS_OPERATION;
		} else if (islimit) {
			/* If this is limit, there can be no other fields. */
			return (EINVAL);
		}
	}

	/* Both fields has to be there. */
	if (fields != (HAS_NAME | HAS_OPERATION))
		return (EINVAL);

#undef	HAS_OPERATION
#undef	HAS_NAME

	return (0);
}

static bool
sysctl_allowed(const nvlist_t *limits, const char *chname, uint64_t choperation)
{
	uint64_t operation;
	const char *name;
	void *cookie;
	int type;

	if (limits == NULL)
		return (true);

	cookie = NULL;
	while ((name = nvlist_next(limits, &type, &cookie)) != NULL) {
		assert(type == NV_TYPE_NUMBER);

		operation = nvlist_get_number(limits, name);
		if ((operation & choperation) != choperation)
			continue;

		if ((operation & CAP_SYSCTL_RECURSIVE) == 0) {
			if (strcmp(name, chname) != 0)
				continue;
		} else {
			size_t namelen;

			namelen = strlen(name);
			if (strncmp(name, chname, namelen) != 0)
				continue;
			if (chname[namelen] != '.' && chname[namelen] != '\0')
				continue;
		}

		return (true);
	}

	return (false);
}

static int
sysctl_limit(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const char *name;
	void *cookie;
	uint64_t operation;
	int type;

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &type, &cookie)) != NULL) {
		if (type != NV_TYPE_NUMBER)
			return (EINVAL);
		operation = nvlist_get_number(newlimits, name);
		if ((operation & ~(CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE)) != 0)
			return (EINVAL);
		if ((operation & (CAP_SYSCTL_RDWR | CAP_SYSCTL_RECURSIVE)) == 0)
			return (EINVAL);
		if (!sysctl_allowed(oldlimits, name, operation))
			return (ENOTCAPABLE);
	}

	return (0);
}

static int
sysctl_command(const char *cmd, const nvlist_t *limits, nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	const char *name;
	const void *newp;
	void *oldp;
	uint64_t operation;
	size_t oldlen, newlen;
	size_t *oldlenp;
	int error;

	if (strcmp(cmd, "sysctl") != 0)
		return (EINVAL);
	error = sysctl_check_one(nvlin, false);
	if (error != 0)
		return (error);

	name = nvlist_get_string(nvlin, "name");
	operation = nvlist_get_number(nvlin, "operation");
	if (!sysctl_allowed(limits, name, operation))
		return (ENOTCAPABLE);

	if ((operation & CAP_SYSCTL_WRITE) != 0) {
		if (!nvlist_exists_binary(nvlin, "newp"))
			return (EINVAL);
		newp = nvlist_get_binary(nvlin, "newp", &newlen);
		assert(newp != NULL && newlen > 0);
	} else {
		newp = NULL;
		newlen = 0;
	}

	if ((operation & CAP_SYSCTL_READ) != 0) {
		if (nvlist_exists_null(nvlin, "justsize")) {
			oldp = NULL;
			oldlen = 0;
			oldlenp = &oldlen;
		} else {
			if (!nvlist_exists_number(nvlin, "oldlen"))
				return (EINVAL);
			oldlen = (size_t)nvlist_get_number(nvlin, "oldlen");
			if (oldlen == 0)
				return (EINVAL);
			oldp = calloc(1, oldlen);
			if (oldp == NULL)
				return (ENOMEM);
			oldlenp = &oldlen;
		}
	} else {
		oldp = NULL;
		oldlen = 0;
		oldlenp = NULL;
	}

	if (sysctlbyname(name, oldp, oldlenp, newp, newlen) == -1) {
		error = errno;
		free(oldp);
		return (error);
	}

	if ((operation & CAP_SYSCTL_READ) != 0) {
		if (nvlist_exists_null(nvlin, "justsize"))
			nvlist_add_number(nvlout, "oldlen", (uint64_t)oldlen);
		else
			nvlist_move_binary(nvlout, "oldp", oldp, oldlen);
	}

	return (0);
}

CREATE_SERVICE("system.sysctl", sysctl_limit, sysctl_command, 0);
