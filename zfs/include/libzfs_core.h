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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 * Copyright 2017 RackTop Systems.
 */

#ifndef	_LIBZFS_CORE_H
#define	_LIBZFS_CORE_H

#include <libnvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

int libzfs_core_init(void);
void libzfs_core_fini(void);

/*
 * NB: this type should be kept binary compatible with dmu_objset_type_t.
 */
enum lzc_dataset_type {
	LZC_DATSET_TYPE_ZFS = 2,
	LZC_DATSET_TYPE_ZVOL
};

int lzc_snapshot(nvlist_t *, nvlist_t *, nvlist_t **);
int lzc_create(const char *, enum lzc_dataset_type, nvlist_t *);
int lzc_clone(const char *, const char *, nvlist_t *);
int lzc_promote(const char *, char *, int);
int lzc_destroy_snaps(nvlist_t *, boolean_t, nvlist_t **);
int lzc_bookmark(nvlist_t *, nvlist_t **);
int lzc_get_bookmarks(const char *, nvlist_t *, nvlist_t **);
int lzc_destroy_bookmarks(nvlist_t *, nvlist_t **);

int lzc_snaprange_space(const char *, const char *, uint64_t *);

int lzc_hold(nvlist_t *, int, nvlist_t **);
int lzc_release(nvlist_t *, nvlist_t **);
int lzc_get_holds(const char *, nvlist_t **);

enum lzc_send_flags {
	LZC_SEND_FLAG_EMBED_DATA = 1 << 0,
	LZC_SEND_FLAG_LARGE_BLOCK = 1 << 1,
	LZC_SEND_FLAG_COMPRESS = 1 << 2
};

int lzc_send(const char *, const char *, int, enum lzc_send_flags);
int lzc_send_resume(const char *, const char *, int,
    enum lzc_send_flags, uint64_t, uint64_t);
int lzc_send_space(const char *, const char *, enum lzc_send_flags, uint64_t *);

struct dmu_replay_record;

int lzc_receive(const char *, nvlist_t *, const char *, boolean_t, int);
int lzc_receive_resumable(const char *, nvlist_t *, const char *,
    boolean_t, int);
int lzc_receive_with_header(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, int, const struct dmu_replay_record *);
int lzc_receive_one(const char *, nvlist_t *, const char *, boolean_t,
    boolean_t, int, const struct dmu_replay_record *, int, uint64_t *,
    uint64_t *, uint64_t *, nvlist_t **);
int lzc_receive_with_cmdprops(const char *, nvlist_t *, nvlist_t *,
    const char *, boolean_t, boolean_t, int, const struct dmu_replay_record *,
    int, uint64_t *, uint64_t *, uint64_t *, nvlist_t **);

boolean_t lzc_exists(const char *);

int lzc_rollback(const char *, char *, int);
int lzc_rollback_to(const char *, const char *);

int lzc_sync(const char *, nvlist_t *, nvlist_t **);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_CORE_H */
