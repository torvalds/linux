/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016, 2017 by Delphix. All rights reserved.
 */

#include <sys/zcp_global.h>

#include "lua.h"
#include "lauxlib.h"

typedef struct zcp_errno_global {
	const char *zeg_name;
	int zeg_errno;
} zcp_errno_global_t;

static const zcp_errno_global_t errno_globals[] = {
	{"EPERM", EPERM},
	{"ENOENT", ENOENT},
	{"ESRCH", ESRCH},
	{"EINTR", EINTR},
	{"EIO", EIO},
	{"ENXIO", ENXIO},
	{"E2BIG", E2BIG},
	{"ENOEXEC", ENOEXEC},
	{"EBADF", EBADF},
	{"ECHILD", ECHILD},
	{"EAGAIN", EAGAIN},
	{"ENOMEM", ENOMEM},
	{"EACCES", EACCES},
	{"EFAULT", EFAULT},
	{"ENOTBLK", ENOTBLK},
	{"EBUSY", EBUSY},
	{"EEXIST", EEXIST},
	{"EXDEV", EXDEV},
	{"ENODEV", ENODEV},
	{"ENOTDIR", ENOTDIR},
	{"EISDIR", EISDIR},
	{"EINVAL", EINVAL},
	{"ENFILE", ENFILE},
	{"EMFILE", EMFILE},
	{"ENOTTY", ENOTTY},
	{"ETXTBSY", ETXTBSY},
	{"EFBIG", EFBIG},
	{"ENOSPC", ENOSPC},
	{"ESPIPE", ESPIPE},
	{"EROFS", EROFS},
	{"EMLINK", EMLINK},
	{"EPIPE", EPIPE},
	{"EDOM", EDOM},
	{"ERANGE", ERANGE},
	{"EDEADLK", EDEADLK},
	{"ENOLCK", ENOLCK},
	{"ECANCELED", ECANCELED},
	{"ENOTSUP", ENOTSUP},
	{"EDQUOT", EDQUOT},
	{"ENAMETOOLONG", ENAMETOOLONG},
	{NULL, 0}
};

static void
zcp_load_errno_globals(lua_State *state)
{
	const zcp_errno_global_t *global = errno_globals;
	while (global->zeg_name != NULL) {
		lua_pushnumber(state, (lua_Number)global->zeg_errno);
		lua_setglobal(state, global->zeg_name);
		global++;
	}
}

void
zcp_load_globals(lua_State *state)
{
	zcp_load_errno_globals(state);
}
