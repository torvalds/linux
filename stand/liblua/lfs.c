/*-
 * Copyright (c) 2018 Conrad Meyer <cem@FreeBSD.org>
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
 * Portions derived from https://github.com/keplerproject/luafilesystem under
 * the terms of the MIT license:
 *
 * Copyright (c) 2003-2014 Kepler Project.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <lua.h>
#include "lauxlib.h"
#include "lfs.h"
#include "lstd.h"
#include "lutils.h"
#include "bootstrap.h"

#ifndef nitems
#define	nitems(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

/*
 * The goal is to emulate a subset of the upstream Lua FileSystem library, as
 * faithfully as possible in the boot environment.  Only APIs that seem useful
 * need to emulated.
 *
 * Example usage:
 *
 *     for file in lfs.dir("/boot") do
 *         print("\t"..file)
 *     end
 *
 * Prints:
 *     .
 *     ..
 * (etc.)
 *
 * The other available API is lfs.attributes(), which functions somewhat like
 * stat(2) and returns a table of values.  Example code:
 *
 *     attrs, errormsg, errorcode = lfs.attributes("/boot")
 *     if attrs == nil then
 *         print(errormsg)
 *         return errorcode
 *     end
 *
 *     for k, v in pairs(attrs) do
 *         print(k .. ":\t" .. v)
 *     end
 *     return 0
 *
 * Prints (on success):
 *     gid:    0
 *     change: 140737488342640
 *     mode:   directory
 *     rdev:   0
 *     ino:    4199275
 *     dev:    140737488342544
 *     modification:   140737488342576
 *     size:   512
 *     access: 140737488342560
 *     permissions:    755
 *     nlink:  58283552
 *     uid:    1001
 */

#define DIR_METATABLE "directory iterator metatable"

static int
lua_dir_iter_next(lua_State *L)
{
	struct dirent *entry;
	DIR *dp, **dpp;

	dpp = (DIR **)luaL_checkudata(L, 1, DIR_METATABLE);
	dp = *dpp;
	luaL_argcheck(L, dp != NULL, 1, "closed directory");

	entry = readdirfd(dp->fd);
	if (entry == NULL) {
		closedir(dp);
		*dpp = NULL;
		return 0;
	}

	lua_pushstring(L, entry->d_name);
	return 1;
}

static int
lua_dir_iter_close(lua_State *L)
{
	DIR *dp, **dpp;

	dpp = (DIR **)lua_touserdata(L, 1);
	dp = *dpp;
	if (dp == NULL)
		return 0;

	closedir(dp);
	*dpp = NULL;
	return 0;
}

static int
lua_dir(lua_State *L)
{
	const char *path;
	DIR *dp;

	if (lua_gettop(L) != 1) {
		lua_pushnil(L);
		return 1;
	}

	path = luaL_checkstring(L, 1);
	dp = opendir(path);
	if (dp == NULL) {
		lua_pushnil(L);
		return 1;
	}

	lua_pushcfunction(L, lua_dir_iter_next);
	*(DIR **)lua_newuserdata(L, sizeof(DIR **)) = dp;
	luaL_getmetatable(L, DIR_METATABLE);
	lua_setmetatable(L, -2);
	return 2;
}

static void
register_metatable(lua_State *L)
{
	/*
	 * Create so-called metatable for iterator object returned by
	 * lfs.dir().
	 */
	luaL_newmetatable(L, DIR_METATABLE);

	lua_newtable(L);
	lua_pushcfunction(L, lua_dir_iter_next);
	lua_setfield(L, -2, "next");
	lua_pushcfunction(L, lua_dir_iter_close);
	lua_setfield(L, -2, "close");

	/* Magically associate anonymous method table with metatable. */
	lua_setfield(L, -2, "__index");
	/* Implement magic destructor method */
	lua_pushcfunction(L, lua_dir_iter_close);
	lua_setfield(L, -2, "__gc");

	lua_pop(L, 1);
}

#define PUSH_INTEGER(lname, stname)				\
static void							\
push_st_ ## lname (lua_State *L, struct stat *sb)		\
{								\
	lua_pushinteger(L, (lua_Integer)sb->st_ ## stname);	\
}
PUSH_INTEGER(dev, dev)
PUSH_INTEGER(ino, ino)
PUSH_INTEGER(nlink, nlink)
PUSH_INTEGER(uid, uid)
PUSH_INTEGER(gid, gid)
PUSH_INTEGER(rdev, rdev)
PUSH_INTEGER(access, atime)
PUSH_INTEGER(modification, mtime)
PUSH_INTEGER(change, ctime)
PUSH_INTEGER(size, size)
#undef PUSH_INTEGER

static void
push_st_mode(lua_State *L, struct stat *sb)
{
	const char *mode_s;
	mode_t mode;

	mode = (sb->st_mode & S_IFMT);
	if (S_ISREG(mode))
		mode_s = "file";
	else if (S_ISDIR(mode))
		mode_s = "directory";
	else if (S_ISLNK(mode))
		mode_s = "link";
	else if (S_ISSOCK(mode))
		mode_s = "socket";
	else if (S_ISFIFO(mode))
		mode_s = "fifo";
	else if (S_ISCHR(mode))
		mode_s = "char device";
	else if (S_ISBLK(mode))
		mode_s = "block device";
	else
		mode_s = "other";

	lua_pushstring(L, mode_s);
}

static void
push_st_permissions(lua_State *L, struct stat *sb)
{
	char buf[20];

	/*
	 * XXX
	 * Could actually format as "-rwxrwxrwx" -- do we care?
	 */
	snprintf(buf, sizeof(buf), "%o", sb->st_mode & ~S_IFMT);
	lua_pushstring(L, buf);
}

#define PUSH_ENTRY(n)	{ #n, push_st_ ## n }
struct stat_members {
	const char *name;
	void (*push)(lua_State *, struct stat *);
} members[] = {
	PUSH_ENTRY(mode),
	PUSH_ENTRY(dev),
	PUSH_ENTRY(ino),
	PUSH_ENTRY(nlink),
	PUSH_ENTRY(uid),
	PUSH_ENTRY(gid),
	PUSH_ENTRY(rdev),
	PUSH_ENTRY(access),
	PUSH_ENTRY(modification),
	PUSH_ENTRY(change),
	PUSH_ENTRY(size),
	PUSH_ENTRY(permissions),
};
#undef PUSH_ENTRY

static int
lua_attributes(lua_State *L)
{
	struct stat sb;
	const char *path, *member;
	size_t i;
	int rc;

	path = luaL_checkstring(L, 1);
	if (path == NULL) {
		lua_pushnil(L);
		lua_pushfstring(L, "cannot convert first argument to string");
		lua_pushinteger(L, EINVAL);
		return 3;
	}

	rc = stat(path, &sb);
	if (rc != 0) {
		lua_pushnil(L);
		lua_pushfstring(L,
		    "cannot obtain information from file '%s': %s", path,
		    strerror(errno));
		lua_pushinteger(L, errno);
		return 3;
	}

	if (lua_isstring(L, 2)) {
		member = lua_tostring(L, 2);
		for (i = 0; i < nitems(members); i++) {
			if (strcmp(members[i].name, member) != 0)
				continue;

			members[i].push(L, &sb);
			return 1;
		}
		return luaL_error(L, "invalid attribute name '%s'", member);
	}

	/* Create or reuse existing table */
	lua_settop(L, 2);
	if (!lua_istable(L, 2))
		lua_newtable(L);

	/* Export all stat data to caller */
	for (i = 0; i < nitems(members); i++) {
		lua_pushstring(L, members[i].name);
		members[i].push(L, &sb);
		lua_rawset(L, -3);
	}
	return 1;
}

#define REG_SIMPLE(n)	{ #n, lua_ ## n }
static const struct luaL_Reg fslib[] = {
	REG_SIMPLE(attributes),
	REG_SIMPLE(dir),
	{ NULL, NULL },
};
#undef REG_SIMPLE

int
luaopen_lfs(lua_State *L)
{
	register_metatable(L);
	luaL_newlib(L, fslib);
	return 1;
}
