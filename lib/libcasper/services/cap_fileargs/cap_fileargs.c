/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Mariusz Zaborski <oshogbo@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/capsicum.h>
#include <sys/sysctl.h>
#include <sys/cnv.h>
#include <sys/dnv.h>
#include <sys/nv.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "cap_fileargs.h"

#define CACHE_SIZE	128

#define FILEARGS_MAGIC	0xFA00FA00

struct fileargs {
	uint32_t	 fa_magic;
	nvlist_t	*fa_cache;
	cap_channel_t	*fa_chann;
	int		 fa_fdflags;
};

static int
fileargs_get_cache(fileargs_t *fa, const char *name)
{
	int fd;
	const nvlist_t *nvl;
	nvlist_t *tnvl;

	assert(fa != NULL);
	assert(fa->fa_magic == FILEARGS_MAGIC);
	assert(name != NULL);

	if (fa->fa_cache == NULL)
		return (-1);

	if ((fa->fa_fdflags & O_CREAT) != 0)
		return (-1);

	nvl = dnvlist_get_nvlist(fa->fa_cache, name, NULL);
	if (nvl == NULL)
		return (-1);

	tnvl = nvlist_take_nvlist(fa->fa_cache, name);
	fd = nvlist_take_descriptor(tnvl, "fd");
	nvlist_destroy(tnvl);

	if ((fa->fa_fdflags & O_CLOEXEC) != O_CLOEXEC) {
		if (fcntl(fd, F_SETFD, fa->fa_fdflags) == -1) {
			close(fd);
			return (-1);
		}
	}

	return (fd);
}

static void
fileargs_set_cache(fileargs_t *fa, nvlist_t *nvl)
{

	nvlist_destroy(fa->fa_cache);
	fa->fa_cache = nvl;
}

static nvlist_t*
fileargs_fetch(fileargs_t *fa, const char *name)
{
	nvlist_t *nvl;
	int serrno;

	assert(fa != NULL);
	assert(name != NULL);

	nvl = nvlist_create(NV_FLAG_NO_UNIQUE);
	nvlist_add_string(nvl, "cmd", "open");
	nvlist_add_string(nvl, "name", name);

	nvl = cap_xfer_nvlist(fa->fa_chann, nvl);
	if (nvl == NULL)
		return (NULL);

	if (nvlist_get_number(nvl, "error") != 0) {
		serrno = (int)nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		errno = serrno;
		return (NULL);
	}

	return (nvl);
}

static nvlist_t *
fileargs_create_limit(int argc, const char * const *argv, int flags,
    mode_t mode, cap_rights_t *rightsp)
{
	nvlist_t *limits;
	int i;

	limits = nvlist_create(NV_FLAG_NO_UNIQUE);
	if (limits == NULL)
		return (NULL);

	nvlist_add_number(limits, "flags", flags);
	if (rightsp != NULL) {
		nvlist_add_binary(limits, "cap_rights", rightsp,
		    sizeof(*rightsp));
	}
	if ((flags & O_CREAT) != 0)
		nvlist_add_number(limits, "mode", (uint64_t)mode);

	for (i = 0; i < argc; i++) {
		nvlist_add_null(limits, argv[i]);
	}

	return (limits);
}

static fileargs_t *
fileargs_create(cap_channel_t *chan, int fdflags)
{
	fileargs_t *fa;

	fa = malloc(sizeof(*fa));
	if (fa != NULL) {
		fa->fa_cache = NULL;
		fa->fa_chann = chan;
		fa->fa_fdflags = fdflags;
		fa->fa_magic = FILEARGS_MAGIC;
	}

	return (fa);
}

fileargs_t *
fileargs_init(int argc, char *argv[], int flags, mode_t mode,
    cap_rights_t *rightsp)
{
	nvlist_t *limits;

	if (argc <= 0 || argv == NULL) {
		return (fileargs_create(NULL, 0));
	}

	limits = fileargs_create_limit(argc, (const char * const *)argv, flags,
	   mode, rightsp);
	if (limits == NULL)
		return (NULL);

	return (fileargs_initnv(limits));
}

fileargs_t *
fileargs_cinit(cap_channel_t *cas, int argc, char *argv[], int flags,
     mode_t mode, cap_rights_t *rightsp)
{
	nvlist_t *limits;

	if (argc <= 0 || argv == NULL) {
		return (fileargs_create(NULL, 0));
	}

	limits = fileargs_create_limit(argc, (const char * const *)argv, flags,
	   mode, rightsp);
	if (limits == NULL)
		return (NULL);

	return (fileargs_cinitnv(cas, limits));
}

fileargs_t *
fileargs_initnv(nvlist_t *limits)
{
        cap_channel_t *cas;
	fileargs_t *fa;

	if (limits == NULL) {
		return (fileargs_create(NULL, 0));
	}

        cas = cap_init();
        if (cas == NULL) {
		nvlist_destroy(limits);
                return (NULL);
	}

        fa = fileargs_cinitnv(cas, limits);
        cap_close(cas);

	return (fa);
}

fileargs_t *
fileargs_cinitnv(cap_channel_t *cas, nvlist_t *limits)
{
	cap_channel_t *chann;
	fileargs_t *fa;
	int serrno, ret;
	int flags;

	assert(cas != NULL);

	if (limits == NULL) {
		return (fileargs_create(NULL, 0));
	}

	chann = NULL;
	fa = NULL;

	chann = cap_service_open(cas, "system.fileargs");
	if (chann == NULL) {
		nvlist_destroy(limits);
		return (NULL);
	}

	flags = nvlist_get_number(limits, "flags");

	/* Limits are consumed no need to free them. */
	ret = cap_limit_set(chann, limits);
	if (ret < 0)
		goto out;

	fa = fileargs_create(chann, flags);
	if (fa == NULL)
		goto out;

	return (fa);
out:
	serrno = errno;
	if (chann != NULL)
		cap_close(chann);
	errno = serrno;
	return (NULL);
}

int
fileargs_open(fileargs_t *fa, const char *name)
{
	int fd;
	nvlist_t *nvl;
	char *cmd;

	assert(fa != NULL);
	assert(fa->fa_magic == FILEARGS_MAGIC);

	if (name == NULL) {
		errno = EINVAL;
		return (-1);
	}

	if (fa->fa_chann == NULL) {
		errno = ENOTCAPABLE;
		return (-1);
	}

	fd = fileargs_get_cache(fa, name);
	if (fd != -1)
		return (fd);

	nvl = fileargs_fetch(fa, name);
	if (nvl == NULL)
		return (-1);

	fd = nvlist_take_descriptor(nvl, "fd");
	cmd = nvlist_take_string(nvl, "cmd");
	if (strcmp(cmd, "cache") == 0)
		fileargs_set_cache(fa, nvl);
	else
		nvlist_destroy(nvl);
	free(cmd);

	return (fd);
}

FILE *
fileargs_fopen(fileargs_t *fa, const char *name, const char *mode)
{
	int fd;

	if ((fd = fileargs_open(fa, name)) < 0) {
		return (NULL);
	}

	return (fdopen(fd, mode));
}

void
fileargs_free(fileargs_t *fa)
{

	if (fa == NULL)
		return;

	assert(fa->fa_magic == FILEARGS_MAGIC);

	nvlist_destroy(fa->fa_cache);
	if (fa->fa_chann != NULL) {
		cap_close(fa->fa_chann);
	}
	explicit_bzero(&fa->fa_magic, sizeof(fa->fa_magic));
	free(fa);
}

/*
 * Service functions.
 */

static const char *lastname;
static void *cacheposition;
static bool allcached;
static const cap_rights_t *caprightsp;
static int capflags;
static mode_t capmode;

static int
open_file(const char *name)
{
	int fd, serrno;

	if ((capflags & O_CREAT) == 0)
		fd = open(name, capflags);
	else
		fd = open(name, capflags, capmode);
	if (fd < 0)
		return (-1);

	if (caprightsp != NULL) {
		if (cap_rights_limit(fd, caprightsp) < 0 && errno != ENOSYS) {
			serrno = errno;
			close(fd);
			errno = serrno;
			return (-1);
		}
	}

	return (fd);
}

static void
fileargs_add_cache(nvlist_t *nvlout, const nvlist_t *limits,
    const char *curent_name)
{
	int type, i, fd;
	void *cookie;
	nvlist_t *new;
	const char *fname;

	if ((capflags & O_CREAT) != 0) {
		allcached = true;
		return;
	}

	cookie = cacheposition;
	for (i = 0; i < CACHE_SIZE + 1; i++) {
		fname = nvlist_next(limits, &type, &cookie);
		if (fname == NULL) {
			cacheposition = NULL;
			lastname = NULL;
			allcached = true;
			return;
		}
		/* We doing that to catch next element name. */
		if (i == CACHE_SIZE) {
			break;
		}

		if (type != NV_TYPE_NULL ||
		    (curent_name != NULL && strcmp(fname, curent_name) == 0)) {
			curent_name = NULL;
			i--;
			continue;
		}

		fd = open_file(fname);
		if (fd < 0) {
			i--;
			continue;
		}

		new = nvlist_create(NV_FLAG_NO_UNIQUE);
		nvlist_move_descriptor(new, "fd", fd);
		nvlist_add_nvlist(nvlout, fname, new);
	}
	cacheposition = cookie;
	lastname = fname;
}

static bool
fileargs_allowed(const nvlist_t *limits, const nvlist_t *request)
{
	const char *name;

	name = dnvlist_get_string(request, "name", NULL);
	if (name == NULL)
		return (false);

	/* Fast path. */
	if (lastname != NULL && strcmp(name, lastname) == 0)
		return (true);

	if (!nvlist_exists_null(limits, name))
		return (false);

	return (true);
}

static int
fileargs_limit(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{

	if (oldlimits != NULL)
		return (ENOTCAPABLE);

	capflags = (int)dnvlist_get_number(newlimits, "flags", 0);
	if ((capflags & O_CREAT) != 0)
		capmode = (mode_t)nvlist_get_number(newlimits, "mode");
	else
		capmode = 0;

	caprightsp = dnvlist_get_binary(newlimits, "cap_rights", NULL, NULL, 0);

	return (0);
}

static int
fileargs_command_open(const nvlist_t *limits, nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	int fd;
	const char *name;

	if (limits == NULL)
		return (ENOTCAPABLE);

	if (!fileargs_allowed(limits, nvlin))
		return (ENOTCAPABLE);

	name = nvlist_get_string(nvlin, "name");

	fd = open_file(name);
	if (fd < 0)
		return (errno);

	if (!allcached && (lastname == NULL ||
	    strcmp(name, lastname) == 0)) {
		nvlist_add_string(nvlout, "cmd", "cache");
		fileargs_add_cache(nvlout, limits, name);
	} else {
		nvlist_add_string(nvlout, "cmd", "open");
	}
	nvlist_move_descriptor(nvlout, "fd", fd);
	return (0);
}

static int
fileargs_command(const char *cmd, const nvlist_t *limits,
    nvlist_t *nvlin, nvlist_t *nvlout)
{

	if (strcmp(cmd, "open") == 0)
		return (fileargs_command_open(limits, nvlin, nvlout));

	return (EINVAL);
}

CREATE_SERVICE("system.fileargs", fileargs_limit, fileargs_command,
    CASPER_SERVICE_FD | CASPER_SERVICE_STDIO | CASPER_SERVICE_NO_UNIQ_LIMITS);
