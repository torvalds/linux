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

/*
 * ZFS Channel Programs (ZCP)
 *
 * The ZCP interface allows various ZFS commands and operations ZFS
 * administrative operations (e.g. creating and destroying snapshots, typically
 * performed via an ioctl to /dev/zfs by the zfs(1M) command and
 * libzfs/libzfs_core) to be run * programmatically as a Lua script.  A ZCP
 * script is run as a dsl_sync_task and fully executed during one transaction
 * group sync.  This ensures that no other changes can be written concurrently
 * with a running Lua script.  Combining multiple calls to the exposed ZFS
 * functions into one script gives a number of benefits:
 *
 * 1. Atomicity.  For some compound or iterative operations, it's useful to be
 * able to guarantee that the state of a pool has not changed between calls to
 * ZFS.
 *
 * 2. Performance.  If a large number of changes need to be made (e.g. deleting
 * many filesystems), there can be a significant performance penalty as a
 * result of the need to wait for a transaction group sync to pass for every
 * single operation.  When expressed as a single ZCP script, all these changes
 * can be performed at once in one txg sync.
 *
 * A modified version of the Lua 5.2 interpreter is used to run channel program
 * scripts. The Lua 5.2 manual can be found at:
 *
 *      http://www.lua.org/manual/5.2/
 *
 * If being run by a user (via an ioctl syscall), executing a ZCP script
 * requires root privileges in the global zone.
 *
 * Scripts are passed to zcp_eval() as a string, then run in a synctask by
 * zcp_eval_sync().  Arguments can be passed into the Lua script as an nvlist,
 * which will be converted to a Lua table.  Similarly, values returned from
 * a ZCP script will be converted to an nvlist.  See zcp_lua_to_nvlist_impl()
 * for details on exact allowed types and conversion.
 *
 * ZFS functionality is exposed to a ZCP script as a library of function calls.
 * These calls are sorted into submodules, such as zfs.list and zfs.sync, for
 * iterators and synctasks, respectively.  Each of these submodules resides in
 * its own source file, with a zcp_*_info structure describing each library
 * call in the submodule.
 *
 * Error handling in ZCP scripts is handled by a number of different methods
 * based on severity:
 *
 * 1. Memory and time limits are in place to prevent a channel program from
 * consuming excessive system or running forever.  If one of these limits is
 * hit, the channel program will be stopped immediately and return from
 * zcp_eval() with an error code. No attempt will be made to roll back or undo
 * any changes made by the channel program before the error occured.
 * Consumers invoking zcp_eval() from elsewhere in the kernel may pass a time
 * limit of 0, disabling the time limit.
 *
 * 2. Internal Lua errors can occur as a result of a syntax error, calling a
 * library function with incorrect arguments, invoking the error() function,
 * failing an assert(), or other runtime errors.  In these cases the channel
 * program will stop executing and return from zcp_eval() with an error code.
 * In place of a return value, an error message will also be returned in the
 * 'result' nvlist containing information about the error. No attempt will be
 * made to roll back or undo any changes made by the channel program before the
 * error occured.
 *
 * 3. If an error occurs inside a ZFS library call which returns an error code,
 * the error is returned to the Lua script to be handled as desired.
 *
 * In the first two cases, Lua's error-throwing mechanism is used, which
 * longjumps out of the script execution with luaL_error() and returns with the
 * error.
 *
 * See zfs-program(1M) for more information on high level usage.
 */

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_dataset.h>
#include <sys/zcp.h>
#include <sys/zcp_iter.h>
#include <sys/zcp_prop.h>
#include <sys/zcp_global.h>
#ifdef illumos
#include <util/sscanf.h>
#endif

#ifdef __FreeBSD__
#define	ECHRNG	EDOM
#define	ETIME	ETIMEDOUT
#endif

#define	ZCP_NVLIST_MAX_DEPTH 20

uint64_t zfs_lua_check_instrlimit_interval = 100;
uint64_t zfs_lua_max_instrlimit = ZCP_MAX_INSTRLIMIT;
uint64_t zfs_lua_max_memlimit = ZCP_MAX_MEMLIMIT;

/*
 * Forward declarations for mutually recursive functions
 */
static int zcp_nvpair_value_to_lua(lua_State *, nvpair_t *, char *, int);
static int zcp_lua_to_nvlist_impl(lua_State *, int, nvlist_t *, const char *,
    int);

typedef struct zcp_alloc_arg {
	boolean_t	aa_must_succeed;
	int64_t		aa_alloc_remaining;
	int64_t		aa_alloc_limit;
} zcp_alloc_arg_t;

typedef struct zcp_eval_arg {
	lua_State	*ea_state;
	zcp_alloc_arg_t	*ea_allocargs;
	cred_t		*ea_cred;
	nvlist_t	*ea_outnvl;
	int		ea_result;
	uint64_t	ea_instrlimit;
} zcp_eval_arg_t;

/*
 * The outer-most error callback handler for use with lua_pcall(). On
 * error Lua will call this callback with a single argument that
 * represents the error value. In most cases this will be a string
 * containing an error message, but channel programs can use Lua's
 * error() function to return arbitrary objects as errors. This callback
 * returns (on the Lua stack) the original error object along with a traceback.
 *
 * Fatal Lua errors can occur while resources are held, so we also call any
 * registered cleanup function here.
 */
static int
zcp_error_handler(lua_State *state)
{
	const char *msg;

	zcp_cleanup(state);

	VERIFY3U(1, ==, lua_gettop(state));
	msg = lua_tostring(state, 1);
	luaL_traceback(state, state, msg, 1);
	return (1);
}

int
zcp_argerror(lua_State *state, int narg, const char *msg, ...)
{
	va_list alist;

	va_start(alist, msg);
	const char *buf = lua_pushvfstring(state, msg, alist);
	va_end(alist);

	return (luaL_argerror(state, narg, buf));
}

/*
 * Install a new cleanup function, which will be invoked with the given
 * opaque argument if a fatal error causes the Lua interpreter to longjump out
 * of a function call.
 *
 * If an error occurs, the cleanup function will be invoked exactly once and
 * then unreigstered.
 *
 * Returns the registered cleanup handler so the caller can deregister it
 * if no error occurs.
 */
zcp_cleanup_handler_t *
zcp_register_cleanup(lua_State *state, zcp_cleanup_t cleanfunc, void *cleanarg)
{
	zcp_run_info_t *ri = zcp_run_info(state);

	zcp_cleanup_handler_t *zch = kmem_alloc(sizeof (*zch), KM_SLEEP);
	zch->zch_cleanup_func = cleanfunc;
	zch->zch_cleanup_arg = cleanarg;
	list_insert_head(&ri->zri_cleanup_handlers, zch);

	return (zch);
}

void
zcp_deregister_cleanup(lua_State *state, zcp_cleanup_handler_t *zch)
{
	zcp_run_info_t *ri = zcp_run_info(state);
	list_remove(&ri->zri_cleanup_handlers, zch);
	kmem_free(zch, sizeof (*zch));
}

/*
 * Execute the currently registered cleanup handlers then free them and
 * destroy the handler list.
 */
void
zcp_cleanup(lua_State *state)
{
	zcp_run_info_t *ri = zcp_run_info(state);

	for (zcp_cleanup_handler_t *zch =
	    list_remove_head(&ri->zri_cleanup_handlers); zch != NULL;
	    zch = list_remove_head(&ri->zri_cleanup_handlers)) {
		zch->zch_cleanup_func(zch->zch_cleanup_arg);
		kmem_free(zch, sizeof (*zch));
	}
}

/*
 * Convert the lua table at the given index on the Lua stack to an nvlist
 * and return it.
 *
 * If the table can not be converted for any reason, NULL is returned and
 * an error message is pushed onto the Lua stack.
 */
static nvlist_t *
zcp_table_to_nvlist(lua_State *state, int index, int depth)
{
	nvlist_t *nvl;
	/*
	 * Converting a Lua table to an nvlist with key uniqueness checking is
	 * O(n^2) in the number of keys in the nvlist, which can take a long
	 * time when we return a large table from a channel program.
	 * Furthermore, Lua's table interface *almost* guarantees unique keys
	 * on its own (details below). Therefore, we don't use fnvlist_alloc()
	 * here to avoid the built-in uniqueness checking.
	 *
	 * The *almost* is because it's possible to have key collisions between
	 * e.g. the string "1" and the number 1, or the string "true" and the
	 * boolean true, so we explicitly check that when we're looking at a
	 * key which is an integer / boolean or a string that can be parsed as
	 * one of those types. In the worst case this could still devolve into
	 * O(n^2), so we only start doing these checks on boolean/integer keys
	 * once we've seen a string key which fits this weird usage pattern.
	 *
	 * Ultimately, we still want callers to know that the keys in this
	 * nvlist are unique, so before we return this we set the nvlist's
	 * flags to reflect that.
	 */
	VERIFY0(nvlist_alloc(&nvl, 0, KM_SLEEP));

	/*
	 * Push an empty stack slot where lua_next() will store each
	 * table key.
	 */
	lua_pushnil(state);
	boolean_t saw_str_could_collide = B_FALSE;
	while (lua_next(state, index) != 0) {
		/*
		 * The next key-value pair from the table at index is
		 * now on the stack, with the key at stack slot -2 and
		 * the value at slot -1.
		 */
		int err = 0;
		char buf[32];
		const char *key = NULL;
		boolean_t key_could_collide = B_FALSE;

		switch (lua_type(state, -2)) {
		case LUA_TSTRING:
			key = lua_tostring(state, -2);

			/* check if this could collide with a number or bool */
			long long tmp;
			int parselen;
			if ((sscanf(key, "%lld%n", &tmp, &parselen) > 0 &&
			    parselen == strlen(key)) ||
			    strcmp(key, "true") == 0 ||
			    strcmp(key, "false") == 0) {
				key_could_collide = B_TRUE;
				saw_str_could_collide = B_TRUE;
			}
			break;
		case LUA_TBOOLEAN:
			key = (lua_toboolean(state, -2) == B_TRUE ?
			    "true" : "false");
			if (saw_str_could_collide) {
				key_could_collide = B_TRUE;
			}
			break;
		case LUA_TNUMBER:
			VERIFY3U(sizeof (buf), >,
			    snprintf(buf, sizeof (buf), "%lld",
			    (longlong_t)lua_tonumber(state, -2)));
			key = buf;
			if (saw_str_could_collide) {
				key_could_collide = B_TRUE;
			}
			break;
		default:
			fnvlist_free(nvl);
			(void) lua_pushfstring(state, "Invalid key "
			    "type '%s' in table",
			    lua_typename(state, lua_type(state, -2)));
			return (NULL);
		}
		/*
		 * Check for type-mismatched key collisions, and throw an error.
		 */
		if (key_could_collide && nvlist_exists(nvl, key)) {
			fnvlist_free(nvl);
			(void) lua_pushfstring(state, "Collision of "
			    "key '%s' in table", key);
			return (NULL);
		}
		/*
		 * Recursively convert the table value and insert into
		 * the new nvlist with the parsed key.  To prevent
		 * stack overflow on circular or heavily nested tables,
		 * we track the current nvlist depth.
		 */
		if (depth >= ZCP_NVLIST_MAX_DEPTH) {
			fnvlist_free(nvl);
			(void) lua_pushfstring(state, "Maximum table "
			    "depth (%d) exceeded for table",
			    ZCP_NVLIST_MAX_DEPTH);
			return (NULL);
		}
		err = zcp_lua_to_nvlist_impl(state, -1, nvl, key,
		    depth + 1);
		if (err != 0) {
			fnvlist_free(nvl);
			/*
			 * Error message has been pushed to the lua
			 * stack by the recursive call.
			 */
			return (NULL);
		}
		/*
		 * Pop the value pushed by lua_next().
		 */
		lua_pop(state, 1);
	}

	/*
	 * Mark the nvlist as having unique keys. This is a little ugly, but we
	 * ensured above that there are no duplicate keys in the nvlist.
	 */
	nvl->nvl_nvflag |= NV_UNIQUE_NAME;

	return (nvl);
}

/*
 * Convert a value from the given index into the lua stack to an nvpair, adding
 * it to an nvlist with the given key.
 *
 * Values are converted as follows:
 *
 *   string -> string
 *   number -> int64
 *   boolean -> boolean
 *   nil -> boolean (no value)
 *
 * Lua tables are converted to nvlists and then inserted. The table's keys
 * are converted to strings then used as keys in the nvlist to store each table
 * element.  Keys are converted as follows:
 *
 *   string -> no change
 *   number -> "%lld"
 *   boolean -> "true" | "false"
 *   nil -> error
 *
 * In the case of a key collision, an error is thrown.
 *
 * If an error is encountered, a nonzero error code is returned, and an error
 * string will be pushed onto the Lua stack.
 */
static int
zcp_lua_to_nvlist_impl(lua_State *state, int index, nvlist_t *nvl,
    const char *key, int depth)
{
	/*
	 * Verify that we have enough remaining space in the lua stack to parse
	 * a key-value pair and push an error.
	 */
	if (!lua_checkstack(state, 3)) {
		(void) lua_pushstring(state, "Lua stack overflow");
		return (1);
	}

	index = lua_absindex(state, index);

	switch (lua_type(state, index)) {
	case LUA_TNIL:
		fnvlist_add_boolean(nvl, key);
		break;
	case LUA_TBOOLEAN:
		fnvlist_add_boolean_value(nvl, key,
		    lua_toboolean(state, index));
		break;
	case LUA_TNUMBER:
		fnvlist_add_int64(nvl, key, lua_tonumber(state, index));
		break;
	case LUA_TSTRING:
		fnvlist_add_string(nvl, key, lua_tostring(state, index));
		break;
	case LUA_TTABLE: {
		nvlist_t *value_nvl = zcp_table_to_nvlist(state, index, depth);
		if (value_nvl == NULL)
			return (EINVAL);

		fnvlist_add_nvlist(nvl, key, value_nvl);
		fnvlist_free(value_nvl);
		break;
	}
	default:
		(void) lua_pushfstring(state,
		    "Invalid value type '%s' for key '%s'",
		    lua_typename(state, lua_type(state, index)), key);
		return (EINVAL);
	}

	return (0);
}

/*
 * Convert a lua value to an nvpair, adding it to an nvlist with the given key.
 */
static void
zcp_lua_to_nvlist(lua_State *state, int index, nvlist_t *nvl, const char *key)
{
	/*
	 * On error, zcp_lua_to_nvlist_impl pushes an error string onto the Lua
	 * stack before returning with a nonzero error code. If an error is
	 * returned, throw a fatal lua error with the given string.
	 */
	if (zcp_lua_to_nvlist_impl(state, index, nvl, key, 0) != 0)
		(void) lua_error(state);
}

static int
zcp_lua_to_nvlist_helper(lua_State *state)
{
	nvlist_t *nv = (nvlist_t *)lua_touserdata(state, 2);
	const char *key = (const char *)lua_touserdata(state, 1);
	zcp_lua_to_nvlist(state, 3, nv, key);
	return (0);
}

static void
zcp_convert_return_values(lua_State *state, nvlist_t *nvl,
    const char *key, zcp_eval_arg_t *evalargs)
{
	int err;
	VERIFY3U(1, ==, lua_gettop(state));
	lua_pushcfunction(state, zcp_lua_to_nvlist_helper);
	lua_pushlightuserdata(state, (char *)key);
	lua_pushlightuserdata(state, nvl);
	lua_pushvalue(state, 1);
	lua_remove(state, 1);
	err = lua_pcall(state, 3, 0, 0); /* zcp_lua_to_nvlist_helper */
	if (err != 0) {
		zcp_lua_to_nvlist(state, 1, nvl, ZCP_RET_ERROR);
		evalargs->ea_result = SET_ERROR(ECHRNG);
	}
}

/*
 * Push a Lua table representing nvl onto the stack.  If it can't be
 * converted, return EINVAL, fill in errbuf, and push nothing. errbuf may
 * be specified as NULL, in which case no error string will be output.
 *
 * Most nvlists are converted as simple key->value Lua tables, but we make
 * an exception for the case where all nvlist entries are BOOLEANs (a string
 * key without a value). In Lua, a table key pointing to a value of Nil
 * (no value) is equivalent to the key not existing, so a BOOLEAN nvlist
 * entry can't be directly converted to a Lua table entry. Nvlists of entirely
 * BOOLEAN entries are frequently used to pass around lists of datasets, so for
 * convenience we check for this case, and convert it to a simple Lua array of
 * strings.
 */
int
zcp_nvlist_to_lua(lua_State *state, nvlist_t *nvl,
    char *errbuf, int errbuf_len)
{
	nvpair_t *pair;
	lua_newtable(state);
	boolean_t has_values = B_FALSE;
	/*
	 * If the list doesn't have any values, just convert it to a string
	 * array.
	 */
	for (pair = nvlist_next_nvpair(nvl, NULL);
	    pair != NULL; pair = nvlist_next_nvpair(nvl, pair)) {
		if (nvpair_type(pair) != DATA_TYPE_BOOLEAN) {
			has_values = B_TRUE;
			break;
		}
	}
	if (!has_values) {
		int i = 1;
		for (pair = nvlist_next_nvpair(nvl, NULL);
		    pair != NULL; pair = nvlist_next_nvpair(nvl, pair)) {
			(void) lua_pushinteger(state, i);
			(void) lua_pushstring(state, nvpair_name(pair));
			(void) lua_settable(state, -3);
			i++;
		}
	} else {
		for (pair = nvlist_next_nvpair(nvl, NULL);
		    pair != NULL; pair = nvlist_next_nvpair(nvl, pair)) {
			int err = zcp_nvpair_value_to_lua(state, pair,
			    errbuf, errbuf_len);
			if (err != 0) {
				lua_pop(state, 1);
				return (err);
			}
			(void) lua_setfield(state, -2, nvpair_name(pair));
		}
	}
	return (0);
}

/*
 * Push a Lua object representing the value of "pair" onto the stack.
 *
 * Only understands boolean_value, string, int64, nvlist,
 * string_array, and int64_array type values.  For other
 * types, returns EINVAL, fills in errbuf, and pushes nothing.
 */
static int
zcp_nvpair_value_to_lua(lua_State *state, nvpair_t *pair,
    char *errbuf, int errbuf_len)
{
	int err = 0;

	if (pair == NULL) {
		lua_pushnil(state);
		return (0);
	}

	switch (nvpair_type(pair)) {
	case DATA_TYPE_BOOLEAN_VALUE:
		(void) lua_pushboolean(state,
		    fnvpair_value_boolean_value(pair));
		break;
	case DATA_TYPE_STRING:
		(void) lua_pushstring(state, fnvpair_value_string(pair));
		break;
	case DATA_TYPE_INT64:
		(void) lua_pushinteger(state, fnvpair_value_int64(pair));
		break;
	case DATA_TYPE_NVLIST:
		err = zcp_nvlist_to_lua(state,
		    fnvpair_value_nvlist(pair), errbuf, errbuf_len);
		break;
	case DATA_TYPE_STRING_ARRAY: {
		char **strarr;
		uint_t nelem;
		(void) nvpair_value_string_array(pair, &strarr, &nelem);
		lua_newtable(state);
		for (int i = 0; i < nelem; i++) {
			(void) lua_pushinteger(state, i + 1);
			(void) lua_pushstring(state, strarr[i]);
			(void) lua_settable(state, -3);
		}
		break;
	}
	case DATA_TYPE_UINT64_ARRAY: {
		uint64_t *intarr;
		uint_t nelem;
		(void) nvpair_value_uint64_array(pair, &intarr, &nelem);
		lua_newtable(state);
		for (int i = 0; i < nelem; i++) {
			(void) lua_pushinteger(state, i + 1);
			(void) lua_pushinteger(state, intarr[i]);
			(void) lua_settable(state, -3);
		}
		break;
	}
	case DATA_TYPE_INT64_ARRAY: {
		int64_t *intarr;
		uint_t nelem;
		(void) nvpair_value_int64_array(pair, &intarr, &nelem);
		lua_newtable(state);
		for (int i = 0; i < nelem; i++) {
			(void) lua_pushinteger(state, i + 1);
			(void) lua_pushinteger(state, intarr[i]);
			(void) lua_settable(state, -3);
		}
		break;
	}
	default: {
		if (errbuf != NULL) {
			(void) snprintf(errbuf, errbuf_len,
			    "Unhandled nvpair type %d for key '%s'",
			    nvpair_type(pair), nvpair_name(pair));
		}
		return (EINVAL);
	}
	}
	return (err);
}

int
zcp_dataset_hold_error(lua_State *state, dsl_pool_t *dp, const char *dsname,
    int error)
{
	if (error == ENOENT) {
		(void) zcp_argerror(state, 1, "no such dataset '%s'", dsname);
		return (0); /* not reached; zcp_argerror will longjmp */
	} else if (error == EXDEV) {
		(void) zcp_argerror(state, 1,
		    "dataset '%s' is not in the target pool '%s'",
		    dsname, spa_name(dp->dp_spa));
		return (0); /* not reached; zcp_argerror will longjmp */
	} else if (error == EIO) {
		(void) luaL_error(state,
		    "I/O error while accessing dataset '%s'", dsname);
		return (0); /* not reached; luaL_error will longjmp */
	} else if (error != 0) {
		(void) luaL_error(state,
		    "unexpected error %d while accessing dataset '%s'",
		    error, dsname);
		return (0); /* not reached; luaL_error will longjmp */
	}
	return (0);
}

/*
 * Note: will longjmp (via lua_error()) on error.
 * Assumes that the dsname is argument #1 (for error reporting purposes).
 */
dsl_dataset_t *
zcp_dataset_hold(lua_State *state, dsl_pool_t *dp, const char *dsname,
    void *tag)
{
	dsl_dataset_t *ds;
	int error = dsl_dataset_hold(dp, dsname, tag, &ds);
	(void) zcp_dataset_hold_error(state, dp, dsname, error);
	return (ds);
}

static int zcp_debug(lua_State *);
static zcp_lib_info_t zcp_debug_info = {
	.name = "debug",
	.func = zcp_debug,
	.pargs = {
	    { .za_name = "debug string", .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_debug(lua_State *state)
{
	const char *dbgstring;
	zcp_run_info_t *ri = zcp_run_info(state);
	zcp_lib_info_t *libinfo = &zcp_debug_info;

	zcp_parse_args(state, libinfo->name, libinfo->pargs, libinfo->kwargs);

	dbgstring = lua_tostring(state, 1);

	zfs_dbgmsg("txg %lld ZCP: %s", ri->zri_tx->tx_txg, dbgstring);

	return (0);
}

static int zcp_exists(lua_State *);
static zcp_lib_info_t zcp_exists_info = {
	.name = "exists",
	.func = zcp_exists,
	.pargs = {
	    { .za_name = "dataset", .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_exists(lua_State *state)
{
	zcp_run_info_t *ri = zcp_run_info(state);
	dsl_pool_t *dp = ri->zri_pool;
	zcp_lib_info_t *libinfo = &zcp_exists_info;

	zcp_parse_args(state, libinfo->name, libinfo->pargs, libinfo->kwargs);

	const char *dsname = lua_tostring(state, 1);

	dsl_dataset_t *ds;
	int error = dsl_dataset_hold(dp, dsname, FTAG, &ds);
	if (error == 0) {
		dsl_dataset_rele(ds, FTAG);
		lua_pushboolean(state, B_TRUE);
	} else if (error == ENOENT) {
		lua_pushboolean(state, B_FALSE);
	} else if (error == EXDEV) {
		return (luaL_error(state, "dataset '%s' is not in the "
		    "target pool", dsname));
	} else if (error == EIO) {
		return (luaL_error(state, "I/O error opening dataset '%s'",
		    dsname));
	} else if (error != 0) {
		return (luaL_error(state, "unexpected error %d", error));
	}

	return (1);
}

/*
 * Allocate/realloc/free a buffer for the lua interpreter.
 *
 * When nsize is 0, behaves as free() and returns NULL.
 *
 * If ptr is NULL, behaves as malloc() and returns an allocated buffer of size
 * at least nsize.
 *
 * Otherwise, behaves as realloc(), changing the allocation from osize to nsize.
 * Shrinking the buffer size never fails.
 *
 * The original allocated buffer size is stored as a uint64 at the beginning of
 * the buffer to avoid actually reallocating when shrinking a buffer, since lua
 * requires that this operation never fail.
 */
static void *
zcp_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
	zcp_alloc_arg_t *allocargs = ud;
	int flags = (allocargs->aa_must_succeed) ?
	    KM_SLEEP : (KM_NOSLEEP | KM_NORMALPRI);

	if (nsize == 0) {
		if (ptr != NULL) {
			int64_t *allocbuf = (int64_t *)ptr - 1;
			int64_t allocsize = *allocbuf;
			ASSERT3S(allocsize, >, 0);
			ASSERT3S(allocargs->aa_alloc_remaining + allocsize, <=,
			    allocargs->aa_alloc_limit);
			allocargs->aa_alloc_remaining += allocsize;
			kmem_free(allocbuf, allocsize);
		}
		return (NULL);
	} else if (ptr == NULL) {
		int64_t *allocbuf;
		int64_t allocsize = nsize + sizeof (int64_t);

		if (!allocargs->aa_must_succeed &&
		    (allocsize <= 0 ||
		    allocsize > allocargs->aa_alloc_remaining)) {
			return (NULL);
		}

		allocbuf = kmem_alloc(allocsize, flags);
		if (allocbuf == NULL) {
			return (NULL);
		}
		allocargs->aa_alloc_remaining -= allocsize;

		*allocbuf = allocsize;
		return (allocbuf + 1);
	} else if (nsize <= osize) {
		/*
		 * If shrinking the buffer, lua requires that the reallocation
		 * never fail.
		 */
		return (ptr);
	} else {
		ASSERT3U(nsize, >, osize);

		uint64_t *luabuf = zcp_lua_alloc(ud, NULL, 0, nsize);
		if (luabuf == NULL) {
			return (NULL);
		}
		(void) memcpy(luabuf, ptr, osize);
		VERIFY3P(zcp_lua_alloc(ud, ptr, osize, 0), ==, NULL);
		return (luabuf);
	}
}

/* ARGSUSED */
static void
zcp_lua_counthook(lua_State *state, lua_Debug *ar)
{
	/*
	 * If we're called, check how many instructions the channel program has
	 * executed so far, and compare against the limit.
	 */
	lua_getfield(state, LUA_REGISTRYINDEX, ZCP_RUN_INFO_KEY);
	zcp_run_info_t *ri = lua_touserdata(state, -1);

	ri->zri_curinstrs += zfs_lua_check_instrlimit_interval;
	if (ri->zri_maxinstrs != 0 && ri->zri_curinstrs > ri->zri_maxinstrs) {
		ri->zri_timed_out = B_TRUE;
		(void) lua_pushstring(state,
		    "Channel program timed out.");
		(void) lua_error(state);
	}
}

static int
zcp_panic_cb(lua_State *state)
{
	panic("unprotected error in call to Lua API (%s)\n",
	    lua_tostring(state, -1));
	return (0);
}

static void
zcp_eval_impl(dmu_tx_t *tx, boolean_t sync, zcp_eval_arg_t *evalargs)
{
	int err;
	zcp_run_info_t ri;
	lua_State *state = evalargs->ea_state;

	VERIFY3U(3, ==, lua_gettop(state));

	/*
	 * Store the zcp_run_info_t struct for this run in the Lua registry.
	 * Registry entries are not directly accessible by the Lua scripts but
	 * can be accessed by our callbacks.
	 */
	ri.zri_space_used = 0;
	ri.zri_pool = dmu_tx_pool(tx);
	ri.zri_cred = evalargs->ea_cred;
	ri.zri_tx = tx;
	ri.zri_timed_out = B_FALSE;
	ri.zri_sync = sync;
	list_create(&ri.zri_cleanup_handlers, sizeof (zcp_cleanup_handler_t),
	    offsetof(zcp_cleanup_handler_t, zch_node));
	ri.zri_curinstrs = 0;
	ri.zri_maxinstrs = evalargs->ea_instrlimit;

	lua_pushlightuserdata(state, &ri);
	lua_setfield(state, LUA_REGISTRYINDEX, ZCP_RUN_INFO_KEY);
	VERIFY3U(3, ==, lua_gettop(state));

	/*
	 * Tell the Lua interpreter to call our handler every count
	 * instructions. Channel programs that execute too many instructions
	 * should die with ETIMEDOUT.
	 */
	(void) lua_sethook(state, zcp_lua_counthook, LUA_MASKCOUNT,
	    zfs_lua_check_instrlimit_interval);

	/*
	 * Tell the Lua memory allocator to stop using KM_SLEEP before handing
	 * off control to the channel program. Channel programs that use too
	 * much memory should die with ENOSPC.
	 */
	evalargs->ea_allocargs->aa_must_succeed = B_FALSE;

	/*
	 * Call the Lua function that open-context passed us. This pops the
	 * function and its input from the stack and pushes any return
	 * or error values.
	 */
	err = lua_pcall(state, 1, LUA_MULTRET, 1);

	/*
	 * Let Lua use KM_SLEEP while we interpret the return values.
	 */
	evalargs->ea_allocargs->aa_must_succeed = B_TRUE;

	/*
	 * Remove the error handler callback from the stack. At this point,
	 * there shouldn't be any cleanup handler registered in the handler
	 * list (zri_cleanup_handlers), regardless of whether it ran or not.
	 */
	list_destroy(&ri.zri_cleanup_handlers);
	lua_remove(state, 1);

	switch (err) {
	case LUA_OK: {
		/*
		 * Lua supports returning multiple values in a single return
		 * statement.  Return values will have been pushed onto the
		 * stack:
		 * 1: Return value 1
		 * 2: Return value 2
		 * 3: etc...
		 * To simplify the process of retrieving a return value from a
		 * channel program, we disallow returning more than one value
		 * to ZFS from the Lua script, yielding a singleton return
		 * nvlist of the form { "return": Return value 1 }.
		 */
		int return_count = lua_gettop(state);

		if (return_count == 1) {
			evalargs->ea_result = 0;
			zcp_convert_return_values(state, evalargs->ea_outnvl,
			    ZCP_RET_RETURN, evalargs);
		} else if (return_count > 1) {
			evalargs->ea_result = SET_ERROR(ECHRNG);
			lua_settop(state, 0);
			(void) lua_pushfstring(state, "Multiple return "
			    "values not supported");
			zcp_convert_return_values(state, evalargs->ea_outnvl,
			    ZCP_RET_ERROR, evalargs);
		}
		break;
	}
	case LUA_ERRRUN:
	case LUA_ERRGCMM: {
		/*
		 * The channel program encountered a fatal error within the
		 * script, such as failing an assertion, or calling a function
		 * with incompatible arguments. The error value and the
		 * traceback generated by zcp_error_handler() should be on the
		 * stack.
		 */
		VERIFY3U(1, ==, lua_gettop(state));
		if (ri.zri_timed_out) {
			evalargs->ea_result = SET_ERROR(ETIME);
		} else {
			evalargs->ea_result = SET_ERROR(ECHRNG);
		}

		zcp_convert_return_values(state, evalargs->ea_outnvl,
		    ZCP_RET_ERROR, evalargs);
		break;
	}
	case LUA_ERRERR: {
		/*
		 * The channel program encountered a fatal error within the
		 * script, and we encountered another error while trying to
		 * compute the traceback in zcp_error_handler(). We can only
		 * return the error message.
		 */
		VERIFY3U(1, ==, lua_gettop(state));
		if (ri.zri_timed_out) {
			evalargs->ea_result = SET_ERROR(ETIME);
		} else {
			evalargs->ea_result = SET_ERROR(ECHRNG);
		}

		zcp_convert_return_values(state, evalargs->ea_outnvl,
		    ZCP_RET_ERROR, evalargs);
		break;
	}
	case LUA_ERRMEM:
		/*
		 * Lua ran out of memory while running the channel program.
		 * There's not much we can do.
		 */
		evalargs->ea_result = SET_ERROR(ENOSPC);
		break;
	default:
		VERIFY0(err);
	}
}

static void
zcp_pool_error(zcp_eval_arg_t *evalargs, const char *poolname)
{
	evalargs->ea_result = SET_ERROR(ECHRNG);
	lua_settop(evalargs->ea_state, 0);
	(void) lua_pushfstring(evalargs->ea_state, "Could not open pool: %s",
	    poolname);
	zcp_convert_return_values(evalargs->ea_state, evalargs->ea_outnvl,
	    ZCP_RET_ERROR, evalargs);

}

static void
zcp_eval_sync(void *arg, dmu_tx_t *tx)
{
	zcp_eval_arg_t *evalargs = arg;

	/*
	 * Open context should have setup the stack to contain:
	 * 1: Error handler callback
	 * 2: Script to run (converted to a Lua function)
	 * 3: nvlist input to function (converted to Lua table or nil)
	 */
	VERIFY3U(3, ==, lua_gettop(evalargs->ea_state));

	zcp_eval_impl(tx, B_TRUE, evalargs);
}

static void
zcp_eval_open(zcp_eval_arg_t *evalargs, const char *poolname)
{

	int error;
	dsl_pool_t *dp;
	dmu_tx_t *tx;

	/*
	 * See comment from the same assertion in zcp_eval_sync().
	 */
	VERIFY3U(3, ==, lua_gettop(evalargs->ea_state));

	error = dsl_pool_hold(poolname, FTAG, &dp);
	if (error != 0) {
		zcp_pool_error(evalargs, poolname);
		return;
	}

	/*
	 * As we are running in open-context, we have no transaction associated
	 * with the channel program. At the same time, functions from the
	 * zfs.check submodule need to be associated with a transaction as
	 * they are basically dry-runs of their counterparts in the zfs.sync
	 * submodule. These functions should be able to run in open-context.
	 * Therefore we create a new transaction that we later abort once
	 * the channel program has been evaluated.
	 */
	tx = dmu_tx_create_dd(dp->dp_mos_dir);

	zcp_eval_impl(tx, B_FALSE, evalargs);

	dmu_tx_abort(tx);

	dsl_pool_rele(dp, FTAG);
}

int
zcp_eval(const char *poolname, const char *program, boolean_t sync,
    uint64_t instrlimit, uint64_t memlimit, nvpair_t *nvarg, nvlist_t *outnvl)
{
	int err;
	lua_State *state;
	zcp_eval_arg_t evalargs;

	if (instrlimit > zfs_lua_max_instrlimit)
		return (SET_ERROR(EINVAL));
	if (memlimit == 0 || memlimit > zfs_lua_max_memlimit)
		return (SET_ERROR(EINVAL));

	zcp_alloc_arg_t allocargs = {
		.aa_must_succeed = B_TRUE,
		.aa_alloc_remaining = (int64_t)memlimit,
		.aa_alloc_limit = (int64_t)memlimit,
	};

	/*
	 * Creates a Lua state with a memory allocator that uses KM_SLEEP.
	 * This should never fail.
	 */
	state = lua_newstate(zcp_lua_alloc, &allocargs);
	VERIFY(state != NULL);
	(void) lua_atpanic(state, zcp_panic_cb);

	/*
	 * Load core Lua libraries we want access to.
	 */
	VERIFY3U(1, ==, luaopen_base(state));
	lua_pop(state, 1);
	VERIFY3U(1, ==, luaopen_coroutine(state));
	lua_setglobal(state, LUA_COLIBNAME);
	VERIFY0(lua_gettop(state));
	VERIFY3U(1, ==, luaopen_string(state));
	lua_setglobal(state, LUA_STRLIBNAME);
	VERIFY0(lua_gettop(state));
	VERIFY3U(1, ==, luaopen_table(state));
	lua_setglobal(state, LUA_TABLIBNAME);
	VERIFY0(lua_gettop(state));

	/*
	 * Load globally visible variables such as errno aliases.
	 */
	zcp_load_globals(state);
	VERIFY0(lua_gettop(state));

	/*
	 * Load ZFS-specific modules.
	 */
	lua_newtable(state);
	VERIFY3U(1, ==, zcp_load_list_lib(state));
	lua_setfield(state, -2, "list");
	VERIFY3U(1, ==, zcp_load_synctask_lib(state, B_FALSE));
	lua_setfield(state, -2, "check");
	VERIFY3U(1, ==, zcp_load_synctask_lib(state, B_TRUE));
	lua_setfield(state, -2, "sync");
	VERIFY3U(1, ==, zcp_load_get_lib(state));
	lua_pushcclosure(state, zcp_debug_info.func, 0);
	lua_setfield(state, -2, zcp_debug_info.name);
	lua_pushcclosure(state, zcp_exists_info.func, 0);
	lua_setfield(state, -2, zcp_exists_info.name);
	lua_setglobal(state, "zfs");
	VERIFY0(lua_gettop(state));

	/*
	 * Push the error-callback that calculates Lua stack traces on
	 * unexpected failures.
	 */
	lua_pushcfunction(state, zcp_error_handler);
	VERIFY3U(1, ==, lua_gettop(state));

	/*
	 * Load the actual script as a function onto the stack as text ("t").
	 * The only valid error condition is a syntax error in the script.
	 * ERRMEM should not be possible because our allocator is using
	 * KM_SLEEP.  ERRGCMM should not be possible because we have not added
	 * any objects with __gc metamethods to the interpreter that could
	 * fail.
	 */
	err = luaL_loadbufferx(state, program, strlen(program),
	    "channel program", "t");
	if (err == LUA_ERRSYNTAX) {
		fnvlist_add_string(outnvl, ZCP_RET_ERROR,
		    lua_tostring(state, -1));
		lua_close(state);
		return (SET_ERROR(EINVAL));
	}
	VERIFY0(err);
	VERIFY3U(2, ==, lua_gettop(state));

	/*
	 * Convert the input nvlist to a Lua object and put it on top of the
	 * stack.
	 */
	char errmsg[128];
	err = zcp_nvpair_value_to_lua(state, nvarg,
	    errmsg, sizeof (errmsg));
	if (err != 0) {
		fnvlist_add_string(outnvl, ZCP_RET_ERROR, errmsg);
		lua_close(state);
		return (SET_ERROR(EINVAL));
	}
	VERIFY3U(3, ==, lua_gettop(state));

	evalargs.ea_state = state;
	evalargs.ea_allocargs = &allocargs;
	evalargs.ea_instrlimit = instrlimit;
	evalargs.ea_cred = CRED();
	evalargs.ea_outnvl = outnvl;
	evalargs.ea_result = 0;

	if (sync) {
		err = dsl_sync_task(poolname, NULL,
		    zcp_eval_sync, &evalargs, 0, ZFS_SPACE_CHECK_ZCP_EVAL);
		if (err != 0)
			zcp_pool_error(&evalargs, poolname);
	} else {
		zcp_eval_open(&evalargs, poolname);
	}
	lua_close(state);

	return (evalargs.ea_result);
}

/*
 * Retrieve metadata about the currently running channel program.
 */
zcp_run_info_t *
zcp_run_info(lua_State *state)
{
	zcp_run_info_t *ri;

	lua_getfield(state, LUA_REGISTRYINDEX, ZCP_RUN_INFO_KEY);
	ri = lua_touserdata(state, -1);
	lua_pop(state, 1);
	return (ri);
}

/*
 * Argument Parsing
 * ================
 *
 * The Lua language allows methods to be called with any number
 * of arguments of any type. When calling back into ZFS we need to sanitize
 * arguments from channel programs to make sure unexpected arguments or
 * arguments of the wrong type result in clear error messages. To do this
 * in a uniform way all callbacks from channel programs should use the
 * zcp_parse_args() function to interpret inputs.
 *
 * Positional vs Keyword Arguments
 * ===============================
 *
 * Every callback function takes a fixed set of required positional arguments
 * and optional keyword arguments. For example, the destroy function takes
 * a single positional string argument (the name of the dataset to destroy)
 * and an optional "defer" keyword boolean argument. When calling lua functions
 * with parentheses, only positional arguments can be used:
 *
 *     zfs.sync.snapshot("rpool@snap")
 *
 * To use keyword arguments functions should be called with a single argument
 * that is a lua table containing mappings of integer -> positional arguments
 * and string -> keyword arguments:
 *
 *     zfs.sync.snapshot({1="rpool@snap", defer=true})
 *
 * The lua language allows curly braces to be used in place of parenthesis as
 * syntactic sugar for this calling convention:
 *
 *     zfs.sync.snapshot{"rpool@snap", defer=true}
 */

/*
 * Throw an error and print the given arguments.  If there are too many
 * arguments to fit in the output buffer, only the error format string is
 * output.
 */
static void
zcp_args_error(lua_State *state, const char *fname, const zcp_arg_t *pargs,
    const zcp_arg_t *kwargs, const char *fmt, ...)
{
	int i;
	char errmsg[512];
	size_t len = sizeof (errmsg);
	size_t msglen = 0;
	va_list argp;

	va_start(argp, fmt);
	VERIFY3U(len, >, vsnprintf(errmsg, len, fmt, argp));
	va_end(argp);

	/*
	 * Calculate the total length of the final string, including extra
	 * formatting characters. If the argument dump would be too large,
	 * only print the error string.
	 */
	msglen = strlen(errmsg);
	msglen += strlen(fname) + 4; /* : + {} + null terminator */
	for (i = 0; pargs[i].za_name != NULL; i++) {
		msglen += strlen(pargs[i].za_name);
		msglen += strlen(lua_typename(state, pargs[i].za_lua_type));
		if (pargs[i + 1].za_name != NULL || kwargs[0].za_name != NULL)
			msglen += 5; /* < + ( + )> + , */
		else
			msglen += 4; /* < + ( + )> */
	}
	for (i = 0; kwargs[i].za_name != NULL; i++) {
		msglen += strlen(kwargs[i].za_name);
		msglen += strlen(lua_typename(state, kwargs[i].za_lua_type));
		if (kwargs[i + 1].za_name != NULL)
			msglen += 4; /* =( + ) + , */
		else
			msglen += 3; /* =( + ) */
	}

	if (msglen >= len)
		(void) luaL_error(state, errmsg);

	VERIFY3U(len, >, strlcat(errmsg, ": ", len));
	VERIFY3U(len, >, strlcat(errmsg, fname, len));
	VERIFY3U(len, >, strlcat(errmsg, "{", len));
	for (i = 0; pargs[i].za_name != NULL; i++) {
		VERIFY3U(len, >, strlcat(errmsg, "<", len));
		VERIFY3U(len, >, strlcat(errmsg, pargs[i].za_name, len));
		VERIFY3U(len, >, strlcat(errmsg, "(", len));
		VERIFY3U(len, >, strlcat(errmsg,
		    lua_typename(state, pargs[i].za_lua_type), len));
		VERIFY3U(len, >, strlcat(errmsg, ")>", len));
		if (pargs[i + 1].za_name != NULL || kwargs[0].za_name != NULL) {
			VERIFY3U(len, >, strlcat(errmsg, ", ", len));
		}
	}
	for (i = 0; kwargs[i].za_name != NULL; i++) {
		VERIFY3U(len, >, strlcat(errmsg, kwargs[i].za_name, len));
		VERIFY3U(len, >, strlcat(errmsg, "=(", len));
		VERIFY3U(len, >, strlcat(errmsg,
		    lua_typename(state, kwargs[i].za_lua_type), len));
		VERIFY3U(len, >, strlcat(errmsg, ")", len));
		if (kwargs[i + 1].za_name != NULL) {
			VERIFY3U(len, >, strlcat(errmsg, ", ", len));
		}
	}
	VERIFY3U(len, >, strlcat(errmsg, "}", len));

	(void) luaL_error(state, errmsg);
	panic("unreachable code");
}

static void
zcp_parse_table_args(lua_State *state, const char *fname,
    const zcp_arg_t *pargs, const zcp_arg_t *kwargs)
{
	int i;
	int type;

	for (i = 0; pargs[i].za_name != NULL; i++) {
		/*
		 * Check the table for this positional argument, leaving it
		 * on the top of the stack once we finish validating it.
		 */
		lua_pushinteger(state, i + 1);
		lua_gettable(state, 1);

		type = lua_type(state, -1);
		if (type == LUA_TNIL) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "too few arguments");
			panic("unreachable code");
		} else if (type != pargs[i].za_lua_type) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "arg %d wrong type (is '%s', expected '%s')",
			    i + 1, lua_typename(state, type),
			    lua_typename(state, pargs[i].za_lua_type));
			panic("unreachable code");
		}

		/*
		 * Remove the positional argument from the table.
		 */
		lua_pushinteger(state, i + 1);
		lua_pushnil(state);
		lua_settable(state, 1);
	}

	for (i = 0; kwargs[i].za_name != NULL; i++) {
		/*
		 * Check the table for this keyword argument, which may be
		 * nil if it was omitted. Leave the value on the top of
		 * the stack after validating it.
		 */
		lua_getfield(state, 1, kwargs[i].za_name);

		type = lua_type(state, -1);
		if (type != LUA_TNIL && type != kwargs[i].za_lua_type) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "kwarg '%s' wrong type (is '%s', expected '%s')",
			    kwargs[i].za_name, lua_typename(state, type),
			    lua_typename(state, kwargs[i].za_lua_type));
			panic("unreachable code");
		}

		/*
		 * Remove the keyword argument from the table.
		 */
		lua_pushnil(state);
		lua_setfield(state, 1, kwargs[i].za_name);
	}

	/*
	 * Any entries remaining in the table are invalid inputs, print
	 * an error message based on what the entry is.
	 */
	lua_pushnil(state);
	if (lua_next(state, 1)) {
		if (lua_isnumber(state, -2) && lua_tointeger(state, -2) > 0) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "too many positional arguments");
		} else if (lua_isstring(state, -2)) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "invalid kwarg '%s'", lua_tostring(state, -2));
		} else {
			zcp_args_error(state, fname, pargs, kwargs,
			    "kwarg keys must be strings");
		}
		panic("unreachable code");
	}

	lua_remove(state, 1);
}

static void
zcp_parse_pos_args(lua_State *state, const char *fname, const zcp_arg_t *pargs,
    const zcp_arg_t *kwargs)
{
	int i;
	int type;

	for (i = 0; pargs[i].za_name != NULL; i++) {
		type = lua_type(state, i + 1);
		if (type == LUA_TNONE) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "too few arguments");
			panic("unreachable code");
		} else if (type != pargs[i].za_lua_type) {
			zcp_args_error(state, fname, pargs, kwargs,
			    "arg %d wrong type (is '%s', expected '%s')",
			    i + 1, lua_typename(state, type),
			    lua_typename(state, pargs[i].za_lua_type));
			panic("unreachable code");
		}
	}
	if (lua_gettop(state) != i) {
		zcp_args_error(state, fname, pargs, kwargs,
		    "too many positional arguments");
		panic("unreachable code");
	}

	for (i = 0; kwargs[i].za_name != NULL; i++) {
		lua_pushnil(state);
	}
}

/*
 * Checks the current Lua stack against an expected set of positional and
 * keyword arguments. If the stack does not match the expected arguments
 * aborts the current channel program with a useful error message, otherwise
 * it re-arranges the stack so that it contains the positional arguments
 * followed by the keyword argument values in declaration order. Any missing
 * keyword argument will be represented by a nil value on the stack.
 *
 * If the stack contains exactly one argument of type LUA_TTABLE the curly
 * braces calling convention is assumed, otherwise the stack is parsed for
 * positional arguments only.
 *
 * This function should be used by every function callback. It should be called
 * before the callback manipulates the Lua stack as it assumes the stack
 * represents the function arguments.
 */
void
zcp_parse_args(lua_State *state, const char *fname, const zcp_arg_t *pargs,
    const zcp_arg_t *kwargs)
{
	if (lua_gettop(state) == 1 && lua_istable(state, 1)) {
		zcp_parse_table_args(state, fname, pargs, kwargs);
	} else {
		zcp_parse_pos_args(state, fname, pargs, kwargs);
	}
}
