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
 */

#ifndef _SYS_ZFEATURE_H
#define	_SYS_ZFEATURE_H

#include <sys/nvpair.h>
#include <sys/txg.h>
#include "zfeature_common.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VALID_FEATURE_FID(fid)	((fid) >= 0 && (fid) < SPA_FEATURES)
#define	VALID_FEATURE_OR_NONE(fid)	((fid) == SPA_FEATURE_NONE ||	\
    VALID_FEATURE_FID(fid))

struct spa;
struct dmu_tx;
struct objset;

extern void spa_feature_create_zap_objects(struct spa *, struct dmu_tx *);
extern void spa_feature_enable(struct spa *, spa_feature_t,
    struct dmu_tx *);
extern void spa_feature_incr(struct spa *, spa_feature_t, struct dmu_tx *);
extern void spa_feature_decr(struct spa *, spa_feature_t, struct dmu_tx *);
extern boolean_t spa_feature_is_enabled(struct spa *, spa_feature_t);
extern boolean_t spa_feature_is_active(struct spa *, spa_feature_t);
extern boolean_t spa_feature_enabled_txg(spa_t *spa, spa_feature_t fid,
    uint64_t *txg);
extern uint64_t spa_feature_refcount(spa_t *, spa_feature_t, uint64_t);
extern boolean_t spa_features_check(spa_t *, boolean_t, nvlist_t *, nvlist_t *);

/*
 * These functions are only exported for zhack and zdb; normal callers should
 * use the above interfaces.
 */
extern int feature_get_refcount(struct spa *, zfeature_info_t *, uint64_t *);
extern int feature_get_refcount_from_disk(spa_t *spa, zfeature_info_t *feature,
    uint64_t *res);
extern void feature_enable_sync(struct spa *, zfeature_info_t *,
    struct dmu_tx *);
extern void feature_sync(struct spa *, zfeature_info_t *, uint64_t,
    struct dmu_tx *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZFEATURE_H */
