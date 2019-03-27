/*-
 * Copyright (c) 2014 Pedro Souza <pedrosouza@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include "lua.h"
#include "lauxlib.h"
#include "lstd.h"
#include "lutils.h"
#include "bootstrap.h"

/*
 * Like loader.perform, except args are passed already parsed
 * on the stack.
 */
static int
lua_command(lua_State *L)
{
	int	i;
	int	res = 1;
	int 	argc = lua_gettop(L);
	char	**argv;

	argv = malloc(sizeof(char *) * (argc + 1));
	if (argv == NULL)
		return 0;
	for (i = 0; i < argc; i++)
		argv[i] = (char *)(intptr_t)luaL_checkstring(L, i + 1);
	argv[argc] = NULL;
	res = interp_builtin_cmd(argc, argv);
	free(argv);
	lua_pushinteger(L, res);

	return 1;
}

static int
lua_perform(lua_State *L)
{
	int	argc;
	char	**argv;
	int	res = 1;

	if (parse(&argc, &argv, luaL_checkstring(L, 1)) == 0) {
		res = interp_builtin_cmd(argc, argv);
		free(argv);
	}
	lua_pushinteger(L, res);

	return 1;
}

static int
lua_command_error(lua_State *L)
{

	lua_pushstring(L, command_errbuf);
	return 1;
}

/*
 * Accepts a space-delimited loader command and runs it through the standard
 * loader parsing, as if it were executed at the loader prompt by the user.
 */
static int
lua_interpret(lua_State *L)
{
	const char	*interp_string;

	if (lua_gettop(L) != 1) {
		lua_pushnil(L);
		return 1;
	}

	interp_string = luaL_checkstring(L, 1);
	lua_pushinteger(L, interp_run(interp_string));
	return 1;
}

static int
lua_parse(lua_State *L)
{
	int	argc, nargc;
	char	**argv;

	if (parse(&argc, &argv, luaL_checkstring(L, 1)) == 0) {
		for (nargc = 0; nargc < argc; ++nargc) {
			lua_pushstring(L, argv[nargc]);
		}
		free(argv);
		return nargc;
	}

	lua_pushnil(L);
	return 1;
}

static int
lua_getchar(lua_State *L)
{

	lua_pushinteger(L, getchar());
	return 1;
}

static int
lua_ischar(lua_State *L)
{

	lua_pushboolean(L, ischar());
	return 1;
}

static int
lua_gets(lua_State *L)
{
	char	buf[129];

	ngets(buf, 128);
	lua_pushstring(L, buf);
	return 1;
}

static int
lua_time(lua_State *L)
{

	lua_pushinteger(L, time(NULL));
	return 1;
}

static int
lua_delay(lua_State *L)
{

	delay((int)luaL_checknumber(L, 1));
	return 0;
}

static int
lua_getenv(lua_State *L)
{
	lua_pushstring(L, getenv(luaL_checkstring(L, 1)));

	return 1;
}

static int
lua_setenv(lua_State *L)
{
	const char *key, *val;

	key = luaL_checkstring(L, 1);
	val = luaL_checkstring(L, 2);
	lua_pushinteger(L, setenv(key, val, 1));

	return 1;
}

static int
lua_unsetenv(lua_State *L)
{
	const char	*ev;

	ev = luaL_checkstring(L, 1);
	lua_pushinteger(L, unsetenv(ev));

	return 1;
}

static int
lua_printc(lua_State *L)
{
	ssize_t cur, l;
	const char *s = luaL_checklstring(L, 1, &l);

	for (cur = 0; cur < l; ++cur)
		putchar((unsigned char)*(s++));

	return 1;
}

static int
lua_openfile(lua_State *L)
{
	const char	*mode, *str;
	int	nargs;

	nargs = lua_gettop(L);
	if (nargs < 1 || nargs > 2) {
		lua_pushnil(L);
		return 1;
	}
	str = lua_tostring(L, 1);
	mode = "r";
	if (nargs > 1) {
		mode = lua_tostring(L, 2);
		if (mode == NULL) {
			lua_pushnil(L);
			return 1;
		}
	}
	FILE * f = fopen(str, mode);
	if (f != NULL) {
		FILE ** ptr = (FILE**)lua_newuserdata(L, sizeof(FILE**));
		*ptr = f;
	} else
		lua_pushnil(L);
	return 1;
}

static int
lua_closefile(lua_State *L)
{
	FILE ** f;
	if (lua_gettop(L) != 1) {
		lua_pushboolean(L, 0);
		return 1;
	}

	f = (FILE**)lua_touserdata(L, 1);
	if (f != NULL && *f != NULL) {
		lua_pushboolean(L, fclose(*f) == 0 ? 1 : 0);
		*f = NULL;
	} else
		lua_pushboolean(L, 0);

	return 1;
}

static int
lua_readfile(lua_State *L)
{
	FILE	**f;
	size_t	size, r;
	char * buf;

	if (lua_gettop(L) < 1 || lua_gettop(L) > 2) {
		lua_pushnil(L);
		lua_pushinteger(L, 0);
		return 2;
	}

	f = (FILE**)lua_touserdata(L, 1);

	if (f == NULL || *f == NULL) {
		lua_pushnil(L);
		lua_pushinteger(L, 0);
		return 2;
	}

	if (lua_gettop(L) == 2)
		size = (size_t)lua_tonumber(L, 2);
	else
		size = (*f)->size;


	buf = (char*)malloc(size);
	r = fread(buf, 1, size, *f);
	lua_pushlstring(L, buf, r);
	free(buf);
	lua_pushinteger(L, r);

	return 2;
}

/*
 * Implements io.write(file, ...)
 * Any number of string and number arguments may be passed to it,
 * and it will return the number of bytes written, or nil, an error string, and
 * the errno.
 */
static int
lua_writefile(lua_State *L)
{
	FILE	**f;
	const char	*buf;
	int	i, nargs;
	size_t	bufsz, w, wrsz;

	buf = NULL;
	bufsz = 0;
	w = 0;
	wrsz = 0;
	nargs = lua_gettop(L);
	if (nargs < 2) {
		errno = EINVAL;
		return luaL_fileresult(L, 0, NULL);
	}

	f = (FILE**)lua_touserdata(L, 1);

	if (f == NULL || *f == NULL) {
		errno = EINVAL;
		return luaL_fileresult(L, 0, NULL);
	}

	/* Do a validation pass first */
	for (i = 0; i < nargs - 1; i++) {
		/*
		 * With Lua's API, lua_isstring really checks if the argument
		 * is a string or a number.  The latter will be implicitly
		 * converted to a string by our later call to lua_tolstring.
		 */
		if (!lua_isstring(L, i + 2)) {
			errno = EINVAL;
			return luaL_fileresult(L, 0, NULL);
		}
	}
	for (i = 0; i < nargs - 1; i++) {
		/* We've already validated; there's no chance of failure */
		buf = lua_tolstring(L, i + 2, &bufsz);
		wrsz = fwrite(buf, 1, bufsz, *f);
		if (wrsz < bufsz)
			return luaL_fileresult(L, 0, NULL);
		w += wrsz;
	}
	lua_pushinteger(L, w);
	return 1;
}

#define REG_SIMPLE(n)	{ #n, lua_ ## n }
static const struct luaL_Reg loaderlib[] = {
	REG_SIMPLE(delay),
	REG_SIMPLE(command_error),
	REG_SIMPLE(command),
	REG_SIMPLE(interpret),
	REG_SIMPLE(parse),
	REG_SIMPLE(getenv),
	REG_SIMPLE(perform),
	/* Also registered as the global 'printc' */
	REG_SIMPLE(printc),
	REG_SIMPLE(setenv),
	REG_SIMPLE(time),
	REG_SIMPLE(unsetenv),
	{ NULL, NULL },
};

static const struct luaL_Reg iolib[] = {
	{ "close", lua_closefile },
	REG_SIMPLE(getchar),
	REG_SIMPLE(gets),
	REG_SIMPLE(ischar),
	{ "open", lua_openfile },
	{ "read", lua_readfile },
	{ "write", lua_writefile },
	{ NULL, NULL },
};
#undef REG_SIMPLE

int
luaopen_loader(lua_State *L)
{
	luaL_newlib(L, loaderlib);
	/* Add loader.machine and loader.machine_arch properties */
	lua_pushstring(L, MACHINE);
	lua_setfield(L, -2, "machine");
	lua_pushstring(L, MACHINE_ARCH);
	lua_setfield(L, -2, "machine_arch");
	/* Set global printc to loader.printc */
	lua_register(L, "printc", lua_printc);
	return 1;
}

int
luaopen_io(lua_State *L)
{
	luaL_newlib(L, iolib);
	return 1;
}
