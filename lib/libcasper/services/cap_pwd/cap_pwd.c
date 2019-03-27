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
#include <sys/nv.h>

#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcasper.h>
#include <libcasper_service.h>

#include "cap_pwd.h"

static struct passwd gpwd;
static char *gbuffer;
static size_t gbufsize;

static int
passwd_resize(void)
{
	char *buf;

	if (gbufsize == 0)
		gbufsize = 1024;
	else
		gbufsize *= 2;

	buf = gbuffer;
	gbuffer = realloc(buf, gbufsize);
	if (gbuffer == NULL) {
		free(buf);
		gbufsize = 0;
		return (ENOMEM);
	}
	memset(gbuffer, 0, gbufsize);

	return (0);
}

static int
passwd_unpack_string(const nvlist_t *nvl, const char *fieldname, char **fieldp,
    char **bufferp, size_t *bufsizep)
{
	const char *str;
	size_t len;

	str = nvlist_get_string(nvl, fieldname);
	len = strlcpy(*bufferp, str, *bufsizep);
	if (len >= *bufsizep)
		return (ERANGE);
	*fieldp = *bufferp;
	*bufferp += len + 1;
	*bufsizep -= len + 1;

	return (0);
}

static int
passwd_unpack(const nvlist_t *nvl, struct passwd *pwd, char *buffer,
    size_t bufsize)
{
	int error;

	if (!nvlist_exists_string(nvl, "pw_name"))
		return (EINVAL);

	explicit_bzero(pwd, sizeof(*pwd));

	error = passwd_unpack_string(nvl, "pw_name", &pwd->pw_name, &buffer,
	    &bufsize);
	if (error != 0)
		return (error);
	pwd->pw_uid = (uid_t)nvlist_get_number(nvl, "pw_uid");
	pwd->pw_gid = (gid_t)nvlist_get_number(nvl, "pw_gid");
	pwd->pw_change = (time_t)nvlist_get_number(nvl, "pw_change");
	error = passwd_unpack_string(nvl, "pw_passwd", &pwd->pw_passwd, &buffer,
	    &bufsize);
	if (error != 0)
		return (error);
	error = passwd_unpack_string(nvl, "pw_class", &pwd->pw_class, &buffer,
	    &bufsize);
	if (error != 0)
		return (error);
	error = passwd_unpack_string(nvl, "pw_gecos", &pwd->pw_gecos, &buffer,
	    &bufsize);
	if (error != 0)
		return (error);
	error = passwd_unpack_string(nvl, "pw_dir", &pwd->pw_dir, &buffer,
	    &bufsize);
	if (error != 0)
		return (error);
	error = passwd_unpack_string(nvl, "pw_shell", &pwd->pw_shell, &buffer,
	    &bufsize);
	if (error != 0)
		return (error);
	pwd->pw_expire = (time_t)nvlist_get_number(nvl, "pw_expire");
	pwd->pw_fields = (int)nvlist_get_number(nvl, "pw_fields");

	return (0);
}

static int
cap_getpwcommon_r(cap_channel_t *chan, const char *cmd, const char *login,
    uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize,
    struct passwd **result)
{
	nvlist_t *nvl;
	bool getpw_r;
	int error;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", cmd);
	if (strcmp(cmd, "getpwent") == 0 || strcmp(cmd, "getpwent_r") == 0) {
		/* Add nothing. */
	} else if (strcmp(cmd, "getpwnam") == 0 ||
	    strcmp(cmd, "getpwnam_r") == 0) {
		nvlist_add_string(nvl, "name", login);
	} else if (strcmp(cmd, "getpwuid") == 0 ||
	    strcmp(cmd, "getpwuid_r") == 0) {
		nvlist_add_number(nvl, "uid", (uint64_t)uid);
	} else {
		abort();
	}
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL) {
		assert(errno != 0);
		*result = NULL;
		return (errno);
	}
	error = (int)nvlist_get_number(nvl, "error");
	if (error != 0) {
		nvlist_destroy(nvl);
		*result = NULL;
		return (error);
	}

	if (!nvlist_exists_string(nvl, "pw_name")) {
		/* Not found. */
		nvlist_destroy(nvl);
		*result = NULL;
		return (0);
	}

	getpw_r = (strcmp(cmd, "getpwent_r") == 0 ||
	    strcmp(cmd, "getpwnam_r") == 0 || strcmp(cmd, "getpwuid_r") == 0);

	for (;;) {
		error = passwd_unpack(nvl, pwd, buffer, bufsize);
		if (getpw_r || error != ERANGE)
			break;
		assert(buffer == gbuffer);
		assert(bufsize == gbufsize);
		error = passwd_resize();
		if (error != 0)
			break;
		/* Update pointers after resize. */
		buffer = gbuffer;
		bufsize = gbufsize;
	}

	nvlist_destroy(nvl);

	if (error == 0)
		*result = pwd;
	else
		*result = NULL;

	return (error);
}

static struct passwd *
cap_getpwcommon(cap_channel_t *chan, const char *cmd, const char *login,
    uid_t uid)
{
	struct passwd *result;
	int error, serrno;

	serrno = errno;

	error = cap_getpwcommon_r(chan, cmd, login, uid, &gpwd, gbuffer,
	    gbufsize, &result);
	if (error != 0) {
		errno = error;
		return (NULL);
	}

	errno = serrno;

	return (result);
}

struct passwd *
cap_getpwent(cap_channel_t *chan)
{

	return (cap_getpwcommon(chan, "getpwent", NULL, 0));
}

struct passwd *
cap_getpwnam(cap_channel_t *chan, const char *login)
{

	return (cap_getpwcommon(chan, "getpwnam", login, 0));
}

struct passwd *
cap_getpwuid(cap_channel_t *chan, uid_t uid)
{

	return (cap_getpwcommon(chan, "getpwuid", NULL, uid));
}

int
cap_getpwent_r(cap_channel_t *chan, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **result)
{

	return (cap_getpwcommon_r(chan, "getpwent_r", NULL, 0, pwd, buffer,
	    bufsize, result));
}

int
cap_getpwnam_r(cap_channel_t *chan, const char *name, struct passwd *pwd,
    char *buffer, size_t bufsize, struct passwd **result)
{

	return (cap_getpwcommon_r(chan, "getpwnam_r", name, 0, pwd, buffer,
	    bufsize, result));
}

int
cap_getpwuid_r(cap_channel_t *chan, uid_t uid, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **result)
{

	return (cap_getpwcommon_r(chan, "getpwuid_r", NULL, uid, pwd, buffer,
	    bufsize, result));
}

int
cap_setpassent(cap_channel_t *chan, int stayopen)
{
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", "setpassent");
	nvlist_add_bool(nvl, "stayopen", stayopen != 0);
	nvl = cap_xfer_nvlist(chan, nvl);
	if (nvl == NULL)
		return (0);
	if (nvlist_get_number(nvl, "error") != 0) {
		errno = nvlist_get_number(nvl, "error");
		nvlist_destroy(nvl);
		return (0);
	}
	nvlist_destroy(nvl);

	return (1);
}

static void
cap_set_end_pwent(cap_channel_t *chan, const char *cmd)
{
	nvlist_t *nvl;

	nvl = nvlist_create(0);
	nvlist_add_string(nvl, "cmd", cmd);
	/* Ignore any errors, we have no way to report them. */
	nvlist_destroy(cap_xfer_nvlist(chan, nvl));
}

void
cap_setpwent(cap_channel_t *chan)
{

	cap_set_end_pwent(chan, "setpwent");
}

void
cap_endpwent(cap_channel_t *chan)
{

	cap_set_end_pwent(chan, "endpwent");
}

int
cap_pwd_limit_cmds(cap_channel_t *chan, const char * const *cmds, size_t ncmds)
{
	nvlist_t *limits, *nvl;
	unsigned int i;

	if (cap_limit_get(chan, &limits) < 0)
		return (-1);
	if (limits == NULL) {
		limits = nvlist_create(0);
	} else {
		if (nvlist_exists_nvlist(limits, "cmds"))
			nvlist_free_nvlist(limits, "cmds");
	}
	nvl = nvlist_create(0);
	for (i = 0; i < ncmds; i++)
		nvlist_add_null(nvl, cmds[i]);
	nvlist_move_nvlist(limits, "cmds", nvl);
	return (cap_limit_set(chan, limits));
}

int
cap_pwd_limit_fields(cap_channel_t *chan, const char * const *fields,
    size_t nfields)
{
	nvlist_t *limits, *nvl;
	unsigned int i;

	if (cap_limit_get(chan, &limits) < 0)
		return (-1);
	if (limits == NULL) {
		limits = nvlist_create(0);
	} else {
		if (nvlist_exists_nvlist(limits, "fields"))
			nvlist_free_nvlist(limits, "fields");
	}
	nvl = nvlist_create(0);
	for (i = 0; i < nfields; i++)
		nvlist_add_null(nvl, fields[i]);
	nvlist_move_nvlist(limits, "fields", nvl);
	return (cap_limit_set(chan, limits));
}

int
cap_pwd_limit_users(cap_channel_t *chan, const char * const *names,
    size_t nnames, uid_t *uids, size_t nuids)
{
	nvlist_t *limits, *users;
	char nvlname[64];
	unsigned int i;
	int n;

	if (cap_limit_get(chan, &limits) < 0)
		return (-1);
	if (limits == NULL) {
		limits = nvlist_create(0);
	} else {
		if (nvlist_exists_nvlist(limits, "users"))
			nvlist_free_nvlist(limits, "users");
	}
	users = nvlist_create(0);
	for (i = 0; i < nuids; i++) {
		n = snprintf(nvlname, sizeof(nvlname), "uid%u", i);
		assert(n > 0 && n < (int)sizeof(nvlname));
		nvlist_add_number(users, nvlname, (uint64_t)uids[i]);
	}
	for (i = 0; i < nnames; i++) {
		n = snprintf(nvlname, sizeof(nvlname), "name%u", i);
		assert(n > 0 && n < (int)sizeof(nvlname));
		nvlist_add_string(users, nvlname, names[i]);
	}
	nvlist_move_nvlist(limits, "users", users);
	return (cap_limit_set(chan, limits));
}


/*
 * Service functions.
 */
static bool
pwd_allowed_cmd(const nvlist_t *limits, const char *cmd)
{

	if (limits == NULL)
		return (true);

	/*
	 * If no limit was set on allowed commands, then all commands
	 * are allowed.
	 */
	if (!nvlist_exists_nvlist(limits, "cmds"))
		return (true);

	limits = nvlist_get_nvlist(limits, "cmds");
	return (nvlist_exists_null(limits, cmd));
}

static int
pwd_allowed_cmds(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const char *name;
	void *cookie;
	int type;

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &type, &cookie)) != NULL) {
		if (type != NV_TYPE_NULL)
			return (EINVAL);
		if (!pwd_allowed_cmd(oldlimits, name))
			return (ENOTCAPABLE);
	}

	return (0);
}

static bool
pwd_allowed_user(const nvlist_t *limits, const char *uname, uid_t uid)
{
	const char *name;
	void *cookie;
	int type;

	if (limits == NULL)
		return (true);

	/*
	 * If no limit was set on allowed users, then all users are allowed.
	 */
	if (!nvlist_exists_nvlist(limits, "users"))
		return (true);

	limits = nvlist_get_nvlist(limits, "users");
	cookie = NULL;
	while ((name = nvlist_next(limits, &type, &cookie)) != NULL) {
		switch (type) {
		case NV_TYPE_NUMBER:
			if (uid != (uid_t)-1 &&
			    nvlist_get_number(limits, name) == (uint64_t)uid) {
				return (true);
			}
			break;
		case NV_TYPE_STRING:
			if (uname != NULL &&
			    strcmp(nvlist_get_string(limits, name),
			    uname) == 0) {
				return (true);
			}
			break;
		default:
			abort();
		}
	}

	return (false);
}

static int
pwd_allowed_users(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const char *name, *uname;
	void *cookie;
	uid_t uid;
	int type;

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &type, &cookie)) != NULL) {
		switch (type) {
		case NV_TYPE_NUMBER:
			uid = (uid_t)nvlist_get_number(newlimits, name);
			uname = NULL;
			break;
		case NV_TYPE_STRING:
			uid = (uid_t)-1;
			uname = nvlist_get_string(newlimits, name);
			break;
		default:
			return (EINVAL);
		}
		if (!pwd_allowed_user(oldlimits, uname, uid))
			return (ENOTCAPABLE);
	}

	return (0);
}

static bool
pwd_allowed_field(const nvlist_t *limits, const char *field)
{

	if (limits == NULL)
		return (true);

	/*
	 * If no limit was set on allowed fields, then all fields are allowed.
	 */
	if (!nvlist_exists_nvlist(limits, "fields"))
		return (true);

	limits = nvlist_get_nvlist(limits, "fields");
	return (nvlist_exists_null(limits, field));
}

static int
pwd_allowed_fields(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const char *name;
	void *cookie;
	int type;

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &type, &cookie)) != NULL) {
		if (type != NV_TYPE_NULL)
			return (EINVAL);
		if (!pwd_allowed_field(oldlimits, name))
			return (ENOTCAPABLE);
	}

	return (0);
}

static bool
pwd_pack(const nvlist_t *limits, const struct passwd *pwd, nvlist_t *nvl)
{
	int fields;

	if (pwd == NULL)
		return (true);

	/*
	 * If either name or UID is allowed, we allow it.
	 */
	if (!pwd_allowed_user(limits, pwd->pw_name, pwd->pw_uid))
		return (false);

	fields = pwd->pw_fields;

	if (pwd_allowed_field(limits, "pw_name")) {
		nvlist_add_string(nvl, "pw_name", pwd->pw_name);
	} else {
		nvlist_add_string(nvl, "pw_name", "");
		fields &= ~_PWF_NAME;
	}
	if (pwd_allowed_field(limits, "pw_uid")) {
		nvlist_add_number(nvl, "pw_uid", (uint64_t)pwd->pw_uid);
	} else {
		nvlist_add_number(nvl, "pw_uid", (uint64_t)-1);
		fields &= ~_PWF_UID;
	}
	if (pwd_allowed_field(limits, "pw_gid")) {
		nvlist_add_number(nvl, "pw_gid", (uint64_t)pwd->pw_gid);
	} else {
		nvlist_add_number(nvl, "pw_gid", (uint64_t)-1);
		fields &= ~_PWF_GID;
	}
	if (pwd_allowed_field(limits, "pw_change")) {
		nvlist_add_number(nvl, "pw_change", (uint64_t)pwd->pw_change);
	} else {
		nvlist_add_number(nvl, "pw_change", (uint64_t)0);
		fields &= ~_PWF_CHANGE;
	}
	if (pwd_allowed_field(limits, "pw_passwd")) {
		nvlist_add_string(nvl, "pw_passwd", pwd->pw_passwd);
	} else {
		nvlist_add_string(nvl, "pw_passwd", "");
		fields &= ~_PWF_PASSWD;
	}
	if (pwd_allowed_field(limits, "pw_class")) {
		nvlist_add_string(nvl, "pw_class", pwd->pw_class);
	} else {
		nvlist_add_string(nvl, "pw_class", "");
		fields &= ~_PWF_CLASS;
	}
	if (pwd_allowed_field(limits, "pw_gecos")) {
		nvlist_add_string(nvl, "pw_gecos", pwd->pw_gecos);
	} else {
		nvlist_add_string(nvl, "pw_gecos", "");
		fields &= ~_PWF_GECOS;
	}
	if (pwd_allowed_field(limits, "pw_dir")) {
		nvlist_add_string(nvl, "pw_dir", pwd->pw_dir);
	} else {
		nvlist_add_string(nvl, "pw_dir", "");
		fields &= ~_PWF_DIR;
	}
	if (pwd_allowed_field(limits, "pw_shell")) {
		nvlist_add_string(nvl, "pw_shell", pwd->pw_shell);
	} else {
		nvlist_add_string(nvl, "pw_shell", "");
		fields &= ~_PWF_SHELL;
	}
	if (pwd_allowed_field(limits, "pw_expire")) {
		nvlist_add_number(nvl, "pw_expire", (uint64_t)pwd->pw_expire);
	} else {
		nvlist_add_number(nvl, "pw_expire", (uint64_t)0);
		fields &= ~_PWF_EXPIRE;
	}
	nvlist_add_number(nvl, "pw_fields", (uint64_t)fields);

	return (true);
}

static int
pwd_getpwent(const nvlist_t *limits, const nvlist_t *nvlin __unused,
    nvlist_t *nvlout)
{
	struct passwd *pwd;

	for (;;) {
		errno = 0;
		pwd = getpwent();
		if (errno != 0)
			return (errno);
		if (pwd_pack(limits, pwd, nvlout))
			return (0);
	}

	/* NOTREACHED */
}

static int
pwd_getpwnam(const nvlist_t *limits, const nvlist_t *nvlin, nvlist_t *nvlout)
{
	struct passwd *pwd;
	const char *name;

	if (!nvlist_exists_string(nvlin, "name"))
		return (EINVAL);
	name = nvlist_get_string(nvlin, "name");
	assert(name != NULL);

	errno = 0;
	pwd = getpwnam(name);
	if (errno != 0)
		return (errno);

	(void)pwd_pack(limits, pwd, nvlout);

	return (0);
}

static int
pwd_getpwuid(const nvlist_t *limits, const nvlist_t *nvlin, nvlist_t *nvlout)
{
	struct passwd *pwd;
	uid_t uid;

	if (!nvlist_exists_number(nvlin, "uid"))
		return (EINVAL);

	uid = (uid_t)nvlist_get_number(nvlin, "uid");

	errno = 0;
	pwd = getpwuid(uid);
	if (errno != 0)
		return (errno);

	(void)pwd_pack(limits, pwd, nvlout);

	return (0);
}

static int
pwd_setpassent(const nvlist_t *limits __unused, const nvlist_t *nvlin,
    nvlist_t *nvlout __unused)
{
	int stayopen;

	if (!nvlist_exists_bool(nvlin, "stayopen"))
		return (EINVAL);

	stayopen = nvlist_get_bool(nvlin, "stayopen") ? 1 : 0;

	return (setpassent(stayopen) == 0 ? EFAULT : 0);
}

static int
pwd_setpwent(const nvlist_t *limits __unused, const nvlist_t *nvlin __unused,
    nvlist_t *nvlout __unused)
{

	setpwent();

	return (0);
}

static int
pwd_endpwent(const nvlist_t *limits __unused, const nvlist_t *nvlin __unused,
    nvlist_t *nvlout __unused)
{

	endpwent();

	return (0);
}

static int
pwd_limit(const nvlist_t *oldlimits, const nvlist_t *newlimits)
{
	const nvlist_t *limits;
	const char *name;
	void *cookie;
	int error, type;

	if (oldlimits != NULL && nvlist_exists_nvlist(oldlimits, "cmds") &&
	    !nvlist_exists_nvlist(newlimits, "cmds")) {
		return (ENOTCAPABLE);
	}
	if (oldlimits != NULL && nvlist_exists_nvlist(oldlimits, "fields") &&
	    !nvlist_exists_nvlist(newlimits, "fields")) {
		return (ENOTCAPABLE);
	}
	if (oldlimits != NULL && nvlist_exists_nvlist(oldlimits, "users") &&
	    !nvlist_exists_nvlist(newlimits, "users")) {
		return (ENOTCAPABLE);
	}

	cookie = NULL;
	while ((name = nvlist_next(newlimits, &type, &cookie)) != NULL) {
		if (type != NV_TYPE_NVLIST)
			return (EINVAL);
		limits = nvlist_get_nvlist(newlimits, name);
		if (strcmp(name, "cmds") == 0)
			error = pwd_allowed_cmds(oldlimits, limits);
		else if (strcmp(name, "fields") == 0)
			error = pwd_allowed_fields(oldlimits, limits);
		else if (strcmp(name, "users") == 0)
			error = pwd_allowed_users(oldlimits, limits);
		else
			error = EINVAL;
		if (error != 0)
			return (error);
	}

	return (0);
}

static int
pwd_command(const char *cmd, const nvlist_t *limits, nvlist_t *nvlin,
    nvlist_t *nvlout)
{
	int error;

	if (!pwd_allowed_cmd(limits, cmd))
		return (ENOTCAPABLE);

	if (strcmp(cmd, "getpwent") == 0 || strcmp(cmd, "getpwent_r") == 0)
		error = pwd_getpwent(limits, nvlin, nvlout);
	else if (strcmp(cmd, "getpwnam") == 0 || strcmp(cmd, "getpwnam_r") == 0)
		error = pwd_getpwnam(limits, nvlin, nvlout);
	else if (strcmp(cmd, "getpwuid") == 0 || strcmp(cmd, "getpwuid_r") == 0)
		error = pwd_getpwuid(limits, nvlin, nvlout);
	else if (strcmp(cmd, "setpassent") == 0)
		error = pwd_setpassent(limits, nvlin, nvlout);
	else if (strcmp(cmd, "setpwent") == 0)
		error = pwd_setpwent(limits, nvlin, nvlout);
	else if (strcmp(cmd, "endpwent") == 0)
		error = pwd_endpwent(limits, nvlin, nvlout);
	else
		error = EINVAL;

	return (error);
}

CREATE_SERVICE("system.pwd", pwd_limit, pwd_command, 0);
