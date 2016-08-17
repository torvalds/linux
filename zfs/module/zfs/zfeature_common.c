/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014, Nexenta Systems, Inc. All rights reserved.
 */

#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include <sys/debug.h>
#include <sys/fs/zfs.h>
#include <sys/inttypes.h>
#include <sys/types.h>
#include "zfeature_common.h"

/*
 * Set to disable all feature checks while opening pools, allowing pools with
 * unsupported features to be opened. Set for testing only.
 */
boolean_t zfeature_checks_disable = B_FALSE;

zfeature_info_t spa_feature_table[SPA_FEATURES];

/*
 * Valid characters for feature guids. This list is mainly for aesthetic
 * purposes and could be expanded in the future. There are different allowed
 * characters in the guids reverse dns portion (before the colon) and its
 * short name (after the colon).
 */
static int
valid_char(char c, boolean_t after_colon)
{
	return ((c >= 'a' && c <= 'z') ||
	    (c >= '0' && c <= '9') ||
	    (after_colon && c == '_') ||
	    (!after_colon && (c == '.' || c == '-')));
}

/*
 * Every feature guid must contain exactly one colon which separates a reverse
 * dns organization name from the feature's "short" name (e.g.
 * "com.company:feature_name").
 */
boolean_t
zfeature_is_valid_guid(const char *name)
{
	int i;
	boolean_t has_colon = B_FALSE;

	i = 0;
	while (name[i] != '\0') {
		char c = name[i++];
		if (c == ':') {
			if (has_colon)
				return (B_FALSE);
			has_colon = B_TRUE;
			continue;
		}
		if (!valid_char(c, has_colon))
			return (B_FALSE);
	}

	return (has_colon);
}

boolean_t
zfeature_is_supported(const char *guid)
{
	spa_feature_t i;

	if (zfeature_checks_disable)
		return (B_TRUE);

	for (i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feature = &spa_feature_table[i];
		if (strcmp(guid, feature->fi_guid) == 0)
			return (B_TRUE);
	}

	return (B_FALSE);
}

int
zfeature_lookup_name(const char *name, spa_feature_t *res)
{
	spa_feature_t i;

	for (i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feature = &spa_feature_table[i];
		if (strcmp(name, feature->fi_uname) == 0) {
			if (res != NULL)
				*res = i;
			return (0);
		}
	}

	return (ENOENT);
}

boolean_t
zfeature_depends_on(spa_feature_t fid, spa_feature_t check) {
	zfeature_info_t *feature = &spa_feature_table[fid];
	int i;

	for (i = 0; feature->fi_depends[i] != SPA_FEATURE_NONE; i++) {
		if (feature->fi_depends[i] == check)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static void
zfeature_register(spa_feature_t fid, const char *guid, const char *name,
    const char *desc, boolean_t readonly, boolean_t mos,
    boolean_t activate_on_enable, const spa_feature_t *deps)
{
	zfeature_info_t *feature = &spa_feature_table[fid];
	static spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	ASSERT(name != NULL);
	ASSERT(desc != NULL);
	ASSERT(!readonly || !mos);
	ASSERT3U(fid, <, SPA_FEATURES);
	ASSERT(zfeature_is_valid_guid(guid));

	if (deps == NULL)
		deps = nodeps;

	feature->fi_feature = fid;
	feature->fi_guid = guid;
	feature->fi_uname = name;
	feature->fi_desc = desc;
	feature->fi_can_readonly = readonly;
	feature->fi_mos = mos;
	feature->fi_activate_on_enable = activate_on_enable;
	feature->fi_depends = deps;
}

void
zpool_feature_init(void)
{
	zfeature_register(SPA_FEATURE_ASYNC_DESTROY,
	    "com.delphix:async_destroy", "async_destroy",
	    "Destroy filesystems asynchronously.", B_TRUE, B_FALSE,
	    B_FALSE, NULL);

	zfeature_register(SPA_FEATURE_EMPTY_BPOBJ,
	    "com.delphix:empty_bpobj", "empty_bpobj",
	    "Snapshots use less space.", B_TRUE, B_FALSE,
	    B_FALSE, NULL);

	zfeature_register(SPA_FEATURE_LZ4_COMPRESS,
	    "org.illumos:lz4_compress", "lz4_compress",
	    "LZ4 compression algorithm support.", B_FALSE, B_FALSE,
	    B_TRUE, NULL);

	zfeature_register(SPA_FEATURE_SPACEMAP_HISTOGRAM,
	    "com.delphix:spacemap_histogram", "spacemap_histogram",
	    "Spacemaps maintain space histograms.", B_TRUE, B_FALSE,
	    B_FALSE, NULL);

	zfeature_register(SPA_FEATURE_ENABLED_TXG,
	    "com.delphix:enabled_txg", "enabled_txg",
	    "Record txg at which a feature is enabled", B_TRUE, B_FALSE,
	    B_FALSE, NULL);

	{
	static const spa_feature_t hole_birth_deps[] = {
		SPA_FEATURE_ENABLED_TXG,
		SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_HOLE_BIRTH,
	    "com.delphix:hole_birth", "hole_birth",
	    "Retain hole birth txg for more precise zfs send",
	    B_FALSE, B_TRUE, B_TRUE, hole_birth_deps);
	}

	zfeature_register(SPA_FEATURE_EXTENSIBLE_DATASET,
	    "com.delphix:extensible_dataset", "extensible_dataset",
	    "Enhanced dataset functionality, used by other features.",
	    B_FALSE, B_FALSE, B_FALSE, NULL);

	{
	static const spa_feature_t bookmarks_deps[] = {
		SPA_FEATURE_EXTENSIBLE_DATASET,
		SPA_FEATURE_NONE
	};

	zfeature_register(SPA_FEATURE_BOOKMARKS,
	    "com.delphix:bookmarks", "bookmarks",
	    "\"zfs bookmark\" command",
	    B_TRUE, B_FALSE, B_FALSE, bookmarks_deps);
	}

	{
	static const spa_feature_t filesystem_limits_deps[] = {
	    SPA_FEATURE_EXTENSIBLE_DATASET,
	    SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_FS_SS_LIMIT,
	    "com.joyent:filesystem_limits", "filesystem_limits",
	    "Filesystem and snapshot limits.", B_TRUE, B_FALSE, B_FALSE,
	    filesystem_limits_deps);
	}

	zfeature_register(SPA_FEATURE_EMBEDDED_DATA,
	    "com.delphix:embedded_data", "embedded_data",
	    "Blocks which compress very well use even less space.",
	    B_FALSE, B_TRUE, B_TRUE, NULL);

	{
	static const spa_feature_t large_blocks_deps[] = {
		SPA_FEATURE_EXTENSIBLE_DATASET,
		SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_LARGE_BLOCKS,
	    "org.open-zfs:large_blocks", "large_blocks",
	    "Support for blocks larger than 128KB.", B_FALSE, B_FALSE, B_FALSE,
	    large_blocks_deps);
	}
}
