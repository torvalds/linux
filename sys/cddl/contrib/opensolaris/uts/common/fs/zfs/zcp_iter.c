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
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#include "lua.h"
#include "lauxlib.h"

#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/zap.h>
#include <sys/dsl_dir.h>
#include <sys/zcp_prop.h>

#include <sys/zcp.h>

typedef int (zcp_list_func_t)(lua_State *);
typedef struct zcp_list_info {
	const char *name;
	zcp_list_func_t *func;
	zcp_list_func_t *gc;
	const zcp_arg_t pargs[4];
	const zcp_arg_t kwargs[2];
} zcp_list_info_t;

static int
zcp_clones_iter(lua_State *state)
{
	int err;
	char clonename[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t dsobj = lua_tonumber(state, lua_upvalueindex(1));
	uint64_t cursor = lua_tonumber(state, lua_upvalueindex(2));
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	dsl_dataset_t *ds, *clone;
	zap_attribute_t za;
	zap_cursor_t zc;

	err = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (err == ENOENT) {
		return (0);
	} else if (err != 0) {
		return (luaL_error(state,
		    "unexpected error %d from dsl_dataset_hold_obj(dsobj)",
		    err));
	}

	if (dsl_dataset_phys(ds)->ds_next_clones_obj == 0) {
		dsl_dataset_rele(ds, FTAG);
		return (0);
	}

	zap_cursor_init_serialized(&zc, dp->dp_meta_objset,
	    dsl_dataset_phys(ds)->ds_next_clones_obj, cursor);
	dsl_dataset_rele(ds, FTAG);

	err = zap_cursor_retrieve(&zc, &za);
	if (err != 0) {
		zap_cursor_fini(&zc);
		if (err != ENOENT) {
			return (luaL_error(state,
			    "unexpected error %d from zap_cursor_retrieve()",
			    err));
		}
		return (0);
	}
	zap_cursor_advance(&zc);
	cursor = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);

	err = dsl_dataset_hold_obj(dp, za.za_first_integer, FTAG, &clone);
	if (err != 0) {
		return (luaL_error(state,
		    "unexpected error %d from "
		    "dsl_dataset_hold_obj(za_first_integer)", err));
	}

	dsl_dir_name(clone->ds_dir, clonename);
	dsl_dataset_rele(clone, FTAG);

	lua_pushnumber(state, cursor);
	lua_replace(state, lua_upvalueindex(2));

	(void) lua_pushstring(state, clonename);
	return (1);
}

static int zcp_clones_list(lua_State *);
static zcp_list_info_t zcp_clones_list_info = {
	.name = "clones",
	.func = zcp_clones_list,
	.gc = NULL,
	.pargs = {
	    { .za_name = "snapshot", .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_clones_list(lua_State *state)
{
	const char *snapname = lua_tostring(state, 1);
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	boolean_t issnap;
	uint64_t dsobj, cursor;

	/*
	 * zcp_dataset_hold will either successfully return the requested
	 * dataset or throw a lua error and longjmp out of the zfs.list.clones
	 * call without returning.
	 */
	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, snapname, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */
	cursor = 0;
	issnap = ds->ds_is_snapshot;
	dsobj = ds->ds_object;
	dsl_dataset_rele(ds, FTAG);

	if (!issnap) {
		return (zcp_argerror(state, 1, "%s is not a snapshot",
		    snapname));
	}

	lua_pushnumber(state, dsobj);
	lua_pushnumber(state, cursor);
	lua_pushcclosure(state, &zcp_clones_iter, 2);
	return (1);
}

static int
zcp_snapshots_iter(lua_State *state)
{
	int err;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t dsobj = lua_tonumber(state, lua_upvalueindex(1));
	uint64_t cursor = lua_tonumber(state, lua_upvalueindex(2));
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	dsl_dataset_t *ds;
	objset_t *os;
	char *p;

	err = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (err != 0) {
		return (luaL_error(state,
		    "unexpected error %d from dsl_dataset_hold_obj(dsobj)",
		    err));
	}

	dsl_dataset_name(ds, snapname);
	VERIFY3U(sizeof (snapname), >,
	    strlcat(snapname, "@", sizeof (snapname)));

	p = strchr(snapname, '\0');
	VERIFY0(dmu_objset_from_ds(ds, &os));
	err = dmu_snapshot_list_next(os,
	    sizeof (snapname) - (p - snapname), p, NULL, &cursor, NULL);
	dsl_dataset_rele(ds, FTAG);

	if (err == ENOENT) {
		return (0);
	} else if (err != 0) {
		return (luaL_error(state,
		    "unexpected error %d from dmu_snapshot_list_next()", err));
	}

	lua_pushnumber(state, cursor);
	lua_replace(state, lua_upvalueindex(2));

	(void) lua_pushstring(state, snapname);
	return (1);
}

static int zcp_snapshots_list(lua_State *);
static zcp_list_info_t zcp_snapshots_list_info = {
	.name = "snapshots",
	.func = zcp_snapshots_list,
	.gc = NULL,
	.pargs = {
	    { .za_name = "filesystem | volume", .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_snapshots_list(lua_State *state)
{
	const char *fsname = lua_tostring(state, 1);
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	boolean_t issnap;
	uint64_t dsobj;

	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, fsname, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */
	issnap = ds->ds_is_snapshot;
	dsobj = ds->ds_object;
	dsl_dataset_rele(ds, FTAG);

	if (issnap) {
		return (zcp_argerror(state, 1,
		    "argument %s cannot be a snapshot", fsname));
	}

	lua_pushnumber(state, dsobj);
	lua_pushnumber(state, 0);
	lua_pushcclosure(state, &zcp_snapshots_iter, 2);
	return (1);
}

/*
 * Note: channel programs only run in the global zone, so all datasets
 * are visible to this zone.
 */
static boolean_t
dataset_name_hidden(const char *name)
{
	if (strchr(name, '$') != NULL)
		return (B_TRUE);
	if (strchr(name, '%') != NULL)
		return (B_TRUE);
	return (B_FALSE);
}

static int
zcp_children_iter(lua_State *state)
{
	int err;
	char childname[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t dsobj = lua_tonumber(state, lua_upvalueindex(1));
	uint64_t cursor = lua_tonumber(state, lua_upvalueindex(2));
	zcp_run_info_t *ri = zcp_run_info(state);
	dsl_pool_t *dp = ri->zri_pool;
	dsl_dataset_t *ds;
	objset_t *os;
	char *p;

	err = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (err != 0) {
		return (luaL_error(state,
		    "unexpected error %d from dsl_dataset_hold_obj(dsobj)",
		    err));
	}

	dsl_dataset_name(ds, childname);
	VERIFY3U(sizeof (childname), >,
	    strlcat(childname, "/", sizeof (childname)));
	p = strchr(childname, '\0');

	VERIFY0(dmu_objset_from_ds(ds, &os));
	do {
		err = dmu_dir_list_next(os,
		    sizeof (childname) - (p - childname), p, NULL, &cursor);
	} while (err == 0 && dataset_name_hidden(childname));
	dsl_dataset_rele(ds, FTAG);

	if (err == ENOENT) {
		return (0);
	} else if (err != 0) {
		return (luaL_error(state,
		    "unexpected error %d from dmu_dir_list_next()",
		    err));
	}

	lua_pushnumber(state, cursor);
	lua_replace(state, lua_upvalueindex(2));

	(void) lua_pushstring(state, childname);
	return (1);
}

static int zcp_children_list(lua_State *);
static zcp_list_info_t zcp_children_list_info = {
	.name = "children",
	.func = zcp_children_list,
	.gc = NULL,
	.pargs = {
	    { .za_name = "filesystem | volume", .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_children_list(lua_State *state)
{
	const char *fsname = lua_tostring(state, 1);
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	boolean_t issnap;
	uint64_t dsobj;

	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, fsname, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */

	issnap = ds->ds_is_snapshot;
	dsobj = ds->ds_object;
	dsl_dataset_rele(ds, FTAG);

	if (issnap) {
		return (zcp_argerror(state, 1,
		    "argument %s cannot be a snapshot", fsname));
	}

	lua_pushnumber(state, dsobj);
	lua_pushnumber(state, 0);
	lua_pushcclosure(state, &zcp_children_iter, 2);
	return (1);
}

static int
zcp_props_list_gc(lua_State *state)
{
	nvlist_t **props = lua_touserdata(state, 1);
	if (*props != NULL)
		fnvlist_free(*props);
	return (0);
}

static int
zcp_props_iter(lua_State *state)
{
	char *source, *val;
	nvlist_t *nvprop;
	nvlist_t **props = lua_touserdata(state, lua_upvalueindex(1));
	nvpair_t *pair = lua_touserdata(state, lua_upvalueindex(2));

	do {
		pair = nvlist_next_nvpair(*props, pair);
		if (pair == NULL) {
			fnvlist_free(*props);
			*props = NULL;
			return (0);
		}
	} while (!zfs_prop_user(nvpair_name(pair)));

	lua_pushlightuserdata(state, pair);
	lua_replace(state, lua_upvalueindex(2));

	nvprop = fnvpair_value_nvlist(pair);
	val = fnvlist_lookup_string(nvprop, ZPROP_VALUE);
	source = fnvlist_lookup_string(nvprop, ZPROP_SOURCE);

	(void) lua_pushstring(state, nvpair_name(pair));
	(void) lua_pushstring(state, val);
	(void) lua_pushstring(state, source);
	return (3);
}

static int zcp_props_list(lua_State *);
static zcp_list_info_t zcp_props_list_info = {
	.name = "properties",
	.func = zcp_props_list,
	.gc = zcp_props_list_gc,
	.pargs = {
	    { .za_name = "filesystem | snapshot | volume",
	    .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

static int
zcp_props_list(lua_State *state)
{
	const char *dsname = lua_tostring(state, 1);
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	objset_t *os;
	nvlist_t **props = lua_newuserdata(state, sizeof (nvlist_t *));

	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, dsname, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */
	VERIFY0(dmu_objset_from_ds(ds, &os));
	VERIFY0(dsl_prop_get_all(os, props));
	dsl_dataset_rele(ds, FTAG);

	/*
	 * Set the metatable for the properties list to free it on completion.
	 */
	luaL_getmetatable(state, zcp_props_list_info.name);
	(void) lua_setmetatable(state, -2);

	lua_pushlightuserdata(state, NULL);
	lua_pushcclosure(state, &zcp_props_iter, 2);
	return (1);
}


/*
 * Populate nv with all valid properties and their values for the given
 * dataset.
 */
static void
zcp_dataset_props(dsl_dataset_t *ds, nvlist_t *nv)
{
	for (int prop = ZFS_PROP_TYPE; prop < ZFS_NUM_PROPS; prop++) {
		/* Do not display hidden props */
		if (!zfs_prop_visible(prop))
			continue;
		/* Do not display props not valid for this dataset */
		if (!prop_valid_for_ds(ds, prop))
			continue;
		fnvlist_add_boolean(nv, zfs_prop_to_name(prop));
	}
}

static int zcp_system_props_list(lua_State *);
static zcp_list_info_t zcp_system_props_list_info = {
	.name = "system_properties",
	.func = zcp_system_props_list,
	.pargs = {
	    { .za_name = "dataset", .za_lua_type = LUA_TSTRING},
	    {NULL, 0}
	},
	.kwargs = {
	    {NULL, 0}
	}
};

/*
 * Get a list of all visble properties and their values for a given dataset.
 * Returned on the stack as a Lua table.
 */
static int
zcp_system_props_list(lua_State *state)
{
	int error;
	char errbuf[128];
	const char *dataset_name;
	dsl_pool_t *dp = zcp_run_info(state)->zri_pool;
	zcp_list_info_t *libinfo = &zcp_system_props_list_info;
	zcp_parse_args(state, libinfo->name, libinfo->pargs, libinfo->kwargs);
	dataset_name = lua_tostring(state, 1);
	nvlist_t *nv = fnvlist_alloc();

	dsl_dataset_t *ds = zcp_dataset_hold(state, dp, dataset_name, FTAG);
	if (ds == NULL)
		return (1); /* not reached; zcp_dataset_hold() longjmp'd */

	/* Get the names of all valid properties for this dataset */
	zcp_dataset_props(ds, nv);
	dsl_dataset_rele(ds, FTAG);

	/* push list as lua table */
	error = zcp_nvlist_to_lua(state, nv, errbuf, sizeof (errbuf));
	nvlist_free(nv);
	if (error != 0) {
		return (luaL_error(state,
		    "Error returning nvlist: %s", errbuf));
	}
	return (1);
}

static int
zcp_list_func(lua_State *state)
{
	zcp_list_info_t *info = lua_touserdata(state, lua_upvalueindex(1));

	zcp_parse_args(state, info->name, info->pargs, info->kwargs);

	return (info->func(state));
}

int
zcp_load_list_lib(lua_State *state)
{
	int i;
	zcp_list_info_t *zcp_list_funcs[] = {
		&zcp_children_list_info,
		&zcp_snapshots_list_info,
		&zcp_props_list_info,
		&zcp_clones_list_info,
		&zcp_system_props_list_info,
		NULL
	};

	lua_newtable(state);

	for (i = 0; zcp_list_funcs[i] != NULL; i++) {
		zcp_list_info_t *info = zcp_list_funcs[i];

		if (info->gc != NULL) {
			/*
			 * If the function requires garbage collection, create
			 * a metatable with its name and register the __gc
			 * function.
			 */
			(void) luaL_newmetatable(state, info->name);
			(void) lua_pushstring(state, "__gc");
			lua_pushcfunction(state, info->gc);
			lua_settable(state, -3);
			lua_pop(state, 1);
		}

		lua_pushlightuserdata(state, info);
		lua_pushcclosure(state, &zcp_list_func, 1);
		lua_setfield(state, -2, info->name);
		info++;
	}

	return (1);
}
