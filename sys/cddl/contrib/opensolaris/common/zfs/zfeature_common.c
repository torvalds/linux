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
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2014, Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include <sys/debug.h>
#include <sys/fs/zfs.h>
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
	if (zfeature_checks_disable)
		return (B_TRUE);

	for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
		zfeature_info_t *feature = &spa_feature_table[i];
		if (strcmp(guid, feature->fi_guid) == 0)
			return (B_TRUE);
	}
	return (B_FALSE);
}

int
zfeature_lookup_name(const char *name, spa_feature_t *res)
{
	for (spa_feature_t i = 0; i < SPA_FEATURES; i++) {
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
zfeature_depends_on(spa_feature_t fid, spa_feature_t check)
{
	zfeature_info_t *feature = &spa_feature_table[fid];

	for (int i = 0; feature->fi_depends[i] != SPA_FEATURE_NONE; i++) {
		if (feature->fi_depends[i] == check)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static void
zfeature_register(spa_feature_t fid, const char *guid, const char *name,
    const char *desc, zfeature_flags_t flags, const spa_feature_t *deps)
{
	zfeature_info_t *feature = &spa_feature_table[fid];
	static spa_feature_t nodeps[] = { SPA_FEATURE_NONE };

	ASSERT(name != NULL);
	ASSERT(desc != NULL);
	ASSERT((flags & ZFEATURE_FLAG_READONLY_COMPAT) == 0 ||
	    (flags & ZFEATURE_FLAG_MOS) == 0);
	ASSERT3U(fid, <, SPA_FEATURES);
	ASSERT(zfeature_is_valid_guid(guid));

	if (deps == NULL)
		deps = nodeps;

	feature->fi_feature = fid;
	feature->fi_guid = guid;
	feature->fi_uname = name;
	feature->fi_desc = desc;
	feature->fi_flags = flags;
	feature->fi_depends = deps;
}

void
zpool_feature_init(void)
{
	zfeature_register(SPA_FEATURE_ASYNC_DESTROY,
	    "com.delphix:async_destroy", "async_destroy",
	    "Destroy filesystems asynchronously.",
	    ZFEATURE_FLAG_READONLY_COMPAT, NULL);

	zfeature_register(SPA_FEATURE_EMPTY_BPOBJ,
	    "com.delphix:empty_bpobj", "empty_bpobj",
	    "Snapshots use less space.",
	    ZFEATURE_FLAG_READONLY_COMPAT, NULL);

	zfeature_register(SPA_FEATURE_LZ4_COMPRESS,
	    "org.illumos:lz4_compress", "lz4_compress",
	    "LZ4 compression algorithm support.",
	    ZFEATURE_FLAG_ACTIVATE_ON_ENABLE, NULL);

	zfeature_register(SPA_FEATURE_MULTI_VDEV_CRASH_DUMP,
	    "com.joyent:multi_vdev_crash_dump", "multi_vdev_crash_dump",
	    "Crash dumps to multiple vdev pools.",
	    0, NULL);

	zfeature_register(SPA_FEATURE_SPACEMAP_HISTOGRAM,
	    "com.delphix:spacemap_histogram", "spacemap_histogram",
	    "Spacemaps maintain space histograms.",
	    ZFEATURE_FLAG_READONLY_COMPAT, NULL);

	zfeature_register(SPA_FEATURE_ENABLED_TXG,
	    "com.delphix:enabled_txg", "enabled_txg",
	    "Record txg at which a feature is enabled",
	    ZFEATURE_FLAG_READONLY_COMPAT, NULL);

	static spa_feature_t hole_birth_deps[] = { SPA_FEATURE_ENABLED_TXG,
	    SPA_FEATURE_NONE };
	zfeature_register(SPA_FEATURE_HOLE_BIRTH,
	    "com.delphix:hole_birth", "hole_birth",
	    "Retain hole birth txg for more precise zfs send",
	    ZFEATURE_FLAG_MOS | ZFEATURE_FLAG_ACTIVATE_ON_ENABLE,
	    hole_birth_deps);

	zfeature_register(SPA_FEATURE_EXTENSIBLE_DATASET,
	    "com.delphix:extensible_dataset", "extensible_dataset",
	    "Enhanced dataset functionality, used by other features.",
	    0, NULL);

	static const spa_feature_t bookmarks_deps[] = {
		SPA_FEATURE_EXTENSIBLE_DATASET,
		SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_BOOKMARKS,
	    "com.delphix:bookmarks", "bookmarks",
	    "\"zfs bookmark\" command",
	    ZFEATURE_FLAG_READONLY_COMPAT, bookmarks_deps);

	static const spa_feature_t filesystem_limits_deps[] = {
	    SPA_FEATURE_EXTENSIBLE_DATASET,
	    SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_FS_SS_LIMIT,
	    "com.joyent:filesystem_limits", "filesystem_limits",
	    "Filesystem and snapshot limits.",
	    ZFEATURE_FLAG_READONLY_COMPAT, filesystem_limits_deps);

	zfeature_register(SPA_FEATURE_EMBEDDED_DATA,
	    "com.delphix:embedded_data", "embedded_data",
	    "Blocks which compress very well use even less space.",
	    ZFEATURE_FLAG_MOS | ZFEATURE_FLAG_ACTIVATE_ON_ENABLE,
	    NULL);

	zfeature_register(SPA_FEATURE_POOL_CHECKPOINT,
	    "com.delphix:zpool_checkpoint", "zpool_checkpoint",
	    "Pool state can be checkpointed, allowing rewind later.",
	    ZFEATURE_FLAG_READONLY_COMPAT, NULL);

	zfeature_register(SPA_FEATURE_SPACEMAP_V2,
	    "com.delphix:spacemap_v2", "spacemap_v2",
	    "Space maps representing large segments are more efficient.",
	    ZFEATURE_FLAG_READONLY_COMPAT | ZFEATURE_FLAG_ACTIVATE_ON_ENABLE,
	    NULL);

	static const spa_feature_t large_blocks_deps[] = {
		SPA_FEATURE_EXTENSIBLE_DATASET,
		SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_LARGE_BLOCKS,
	    "org.open-zfs:large_blocks", "large_blocks",
	    "Support for blocks larger than 128KB.",
	    ZFEATURE_FLAG_PER_DATASET, large_blocks_deps);

	{
	static const spa_feature_t large_dnode_deps[] = {
		SPA_FEATURE_EXTENSIBLE_DATASET,
		SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_LARGE_DNODE,
	    "org.zfsonlinux:large_dnode", "large_dnode",
	    "Variable on-disk size of dnodes.",
	    ZFEATURE_FLAG_PER_DATASET, large_dnode_deps);
	}

	zfeature_register(SPA_FEATURE_SHA512,
	    "org.illumos:sha512", "sha512",
	    "SHA-512/256 hash algorithm.",
	    ZFEATURE_FLAG_PER_DATASET, NULL);
	zfeature_register(SPA_FEATURE_SKEIN,
	    "org.illumos:skein", "skein",
	    "Skein hash algorithm.",
	    ZFEATURE_FLAG_PER_DATASET, NULL);

#ifdef illumos
	zfeature_register(SPA_FEATURE_EDONR,
	    "org.illumos:edonr", "edonr",
	    "Edon-R hash algorithm.",
	    ZFEATURE_FLAG_PER_DATASET, NULL);
#endif

	zfeature_register(SPA_FEATURE_DEVICE_REMOVAL,
	    "com.delphix:device_removal", "device_removal",
	    "Top-level vdevs can be removed, reducing logical pool size.",
	    ZFEATURE_FLAG_MOS, NULL);

	static const spa_feature_t obsolete_counts_deps[] = {
		SPA_FEATURE_EXTENSIBLE_DATASET,
		SPA_FEATURE_DEVICE_REMOVAL,
		SPA_FEATURE_NONE
	};
	zfeature_register(SPA_FEATURE_OBSOLETE_COUNTS,
	    "com.delphix:obsolete_counts", "obsolete_counts",
	    "Reduce memory used by removed devices when their blocks are "
	    "freed or remapped.",
	    ZFEATURE_FLAG_READONLY_COMPAT, obsolete_counts_deps);
}
