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

#ifndef _SYS_ZCP_H
#define	_SYS_ZCP_H

#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZCP_RUN_INFO_KEY "runinfo"

extern uint64_t zfs_lua_max_instrlimit;
extern uint64_t zfs_lua_max_memlimit;

int zcp_argerror(lua_State *, int, const char *, ...);

int zcp_eval(const char *, const char *, boolean_t, uint64_t, uint64_t,
    nvpair_t *, nvlist_t *);

int zcp_load_list_lib(lua_State *);

int zcp_load_synctask_lib(lua_State *, boolean_t);

typedef void (zcp_cleanup_t)(void *);
typedef struct zcp_cleanup_handler {
	zcp_cleanup_t *zch_cleanup_func;
	void *zch_cleanup_arg;
	list_node_t zch_node;
} zcp_cleanup_handler_t;

typedef struct zcp_run_info {
	dsl_pool_t	*zri_pool;

	/*
	 * An estimate of the total amount of space consumed by all
	 * synctasks we have successfully performed so far in this
	 * channel program. Used to generate ENOSPC errors for syncfuncs.
	 */
	int		zri_space_used;

	/*
	 * The credentials of the thread which originally invoked the channel
	 * program. Since channel programs are always invoked from the synctask
	 * thread they should always do permissions checks against this cred
	 * rather than the 'current' thread's.
	 */
	cred_t		*zri_cred;

	/*
	 * The tx in which this channel program is running.
	 */
	dmu_tx_t	*zri_tx;

	/*
	 * The maximum number of Lua instructions the channel program is allowed
	 * to execute. If it takes longer than this it will time out. A value
	 * of 0 indicates no instruction limit.
	 */
	uint64_t	zri_maxinstrs;

	/*
	 * The number of Lua instructions the channel program has executed.
	 */
	uint64_t	zri_curinstrs;

	/*
	 * Boolean indicating whether or not the channel program exited
	 * because it timed out.
	 */
	boolean_t	zri_timed_out;

	/*
	 * Boolean indicating whether or not we are running in syncing
	 * context.
	 */
	boolean_t	zri_sync;

	/*
	 * List of currently registered cleanup handlers, which will be
	 * triggered in the event of a fatal error.
	 */
	list_t		zri_cleanup_handlers;
} zcp_run_info_t;

zcp_run_info_t *zcp_run_info(lua_State *);
zcp_cleanup_handler_t *zcp_register_cleanup(lua_State *, zcp_cleanup_t, void *);
void zcp_deregister_cleanup(lua_State *, zcp_cleanup_handler_t *);
void zcp_cleanup(lua_State *);

/*
 * Argument parsing routines for channel program callback functions.
 */
typedef struct zcp_arg {
	/*
	 * The name of this argument. For keyword arguments this is the name
	 * functions will use to set the argument. For positional arguments
	 * the name has no programatic meaning, but will appear in error
	 * messages and help output.
	 */
	const char *za_name;

	/*
	 * The Lua type this argument should have (e.g. LUA_TSTRING,
	 * LUA_TBOOLEAN) see the lua_type() function documentation for a
	 * complete list. Calling a function with an argument that does
	 * not match the expected type will result in the program terminating.
	 */
	const int za_lua_type;
} zcp_arg_t;

void zcp_parse_args(lua_State *, const char *, const zcp_arg_t *,
    const zcp_arg_t *);
int zcp_nvlist_to_lua(lua_State *, nvlist_t *, char *, int);
int zcp_dataset_hold_error(lua_State *, dsl_pool_t *, const char *, int);
struct dsl_dataset *zcp_dataset_hold(lua_State *, dsl_pool_t *,
    const char *, void *);

typedef int (zcp_lib_func_t)(lua_State *);
typedef struct zcp_lib_info {
	const char *name;
	zcp_lib_func_t *func;
	const zcp_arg_t pargs[4];
	const zcp_arg_t kwargs[2];
} zcp_lib_info_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZCP_H */
