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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zio.h>
#include <sys/ddt.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/arc.h>
#include <sys/dsl_pool.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/dsl_scan.h>
#include <sys/abd.h>

/*
 * Enable/disable prefetching of dedup-ed blocks which are going to be freed.
 */
int zfs_dedup_prefetch = 1;

SYSCTL_DECL(_vfs_zfs);
SYSCTL_NODE(_vfs_zfs, OID_AUTO, dedup, CTLFLAG_RW, 0, "ZFS DEDUP");
SYSCTL_INT(_vfs_zfs_dedup, OID_AUTO, prefetch, CTLFLAG_RWTUN, &zfs_dedup_prefetch,
    0, "Enable/disable prefetching of dedup-ed blocks which are going to be freed");

static const ddt_ops_t *ddt_ops[DDT_TYPES] = {
	&ddt_zap_ops,
};

static const char *ddt_class_name[DDT_CLASSES] = {
	"ditto",
	"duplicate",
	"unique",
};

static void
ddt_object_create(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    dmu_tx_t *tx)
{
	spa_t *spa = ddt->ddt_spa;
	objset_t *os = ddt->ddt_os;
	uint64_t *objectp = &ddt->ddt_object[type][class];
	boolean_t prehash = zio_checksum_table[ddt->ddt_checksum].ci_flags &
	    ZCHECKSUM_FLAG_DEDUP;
	char name[DDT_NAMELEN];

	ddt_object_name(ddt, type, class, name);

	ASSERT(*objectp == 0);
	VERIFY(ddt_ops[type]->ddt_op_create(os, objectp, tx, prehash) == 0);
	ASSERT(*objectp != 0);

	VERIFY(zap_add(os, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, objectp, tx) == 0);

	VERIFY(zap_add(os, spa->spa_ddt_stat_object, name,
	    sizeof (uint64_t), sizeof (ddt_histogram_t) / sizeof (uint64_t),
	    &ddt->ddt_histogram[type][class], tx) == 0);
}

static void
ddt_object_destroy(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    dmu_tx_t *tx)
{
	spa_t *spa = ddt->ddt_spa;
	objset_t *os = ddt->ddt_os;
	uint64_t *objectp = &ddt->ddt_object[type][class];
	uint64_t count;
	char name[DDT_NAMELEN];

	ddt_object_name(ddt, type, class, name);

	ASSERT(*objectp != 0);
	VERIFY(ddt_object_count(ddt, type, class, &count) == 0 && count == 0);
	ASSERT(ddt_histogram_empty(&ddt->ddt_histogram[type][class]));
	VERIFY(zap_remove(os, DMU_POOL_DIRECTORY_OBJECT, name, tx) == 0);
	VERIFY(zap_remove(os, spa->spa_ddt_stat_object, name, tx) == 0);
	VERIFY(ddt_ops[type]->ddt_op_destroy(os, *objectp, tx) == 0);
	bzero(&ddt->ddt_object_stats[type][class], sizeof (ddt_object_t));

	*objectp = 0;
}

static int
ddt_object_load(ddt_t *ddt, enum ddt_type type, enum ddt_class class)
{
	ddt_object_t *ddo = &ddt->ddt_object_stats[type][class];
	dmu_object_info_t doi;
	uint64_t count;
	char name[DDT_NAMELEN];
	int error;

	ddt_object_name(ddt, type, class, name);

	error = zap_lookup(ddt->ddt_os, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), 1, &ddt->ddt_object[type][class]);

	if (error != 0)
		return (error);

	VERIFY0(zap_lookup(ddt->ddt_os, ddt->ddt_spa->spa_ddt_stat_object, name,
	    sizeof (uint64_t), sizeof (ddt_histogram_t) / sizeof (uint64_t),
	    &ddt->ddt_histogram[type][class]));

	/*
	 * Seed the cached statistics.
	 */
	VERIFY(ddt_object_info(ddt, type, class, &doi) == 0);

	error = ddt_object_count(ddt, type, class, &count);
	if (error)
		return error;

	ddo->ddo_count = count;
	ddo->ddo_dspace = doi.doi_physical_blocks_512 << 9;
	ddo->ddo_mspace = doi.doi_fill_count * doi.doi_data_block_size;

	return (0);
}

static void
ddt_object_sync(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    dmu_tx_t *tx)
{
	ddt_object_t *ddo = &ddt->ddt_object_stats[type][class];
	dmu_object_info_t doi;
	uint64_t count;
	char name[DDT_NAMELEN];

	ddt_object_name(ddt, type, class, name);

	VERIFY(zap_update(ddt->ddt_os, ddt->ddt_spa->spa_ddt_stat_object, name,
	    sizeof (uint64_t), sizeof (ddt_histogram_t) / sizeof (uint64_t),
	    &ddt->ddt_histogram[type][class], tx) == 0);

	/*
	 * Cache DDT statistics; this is the only time they'll change.
	 */
	VERIFY(ddt_object_info(ddt, type, class, &doi) == 0);
	VERIFY(ddt_object_count(ddt, type, class, &count) == 0);

	ddo->ddo_count = count;
	ddo->ddo_dspace = doi.doi_physical_blocks_512 << 9;
	ddo->ddo_mspace = doi.doi_fill_count * doi.doi_data_block_size;
}

static int
ddt_object_lookup(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    ddt_entry_t *dde)
{
	if (!ddt_object_exists(ddt, type, class))
		return (SET_ERROR(ENOENT));

	return (ddt_ops[type]->ddt_op_lookup(ddt->ddt_os,
	    ddt->ddt_object[type][class], dde));
}

static void
ddt_object_prefetch(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    ddt_entry_t *dde)
{
	if (!ddt_object_exists(ddt, type, class))
		return;

	ddt_ops[type]->ddt_op_prefetch(ddt->ddt_os,
	    ddt->ddt_object[type][class], dde);
}

int
ddt_object_update(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    ddt_entry_t *dde, dmu_tx_t *tx)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_update(ddt->ddt_os,
	    ddt->ddt_object[type][class], dde, tx));
}

static int
ddt_object_remove(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    ddt_entry_t *dde, dmu_tx_t *tx)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_remove(ddt->ddt_os,
	    ddt->ddt_object[type][class], dde, tx));
}

int
ddt_object_walk(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    uint64_t *walk, ddt_entry_t *dde)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_walk(ddt->ddt_os,
	    ddt->ddt_object[type][class], dde, walk));
}

int
ddt_object_count(ddt_t *ddt, enum ddt_type type, enum ddt_class class, uint64_t *count)
{
	ASSERT(ddt_object_exists(ddt, type, class));

	return (ddt_ops[type]->ddt_op_count(ddt->ddt_os,
	    ddt->ddt_object[type][class], count));
}

int
ddt_object_info(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    dmu_object_info_t *doi)
{
	if (!ddt_object_exists(ddt, type, class))
		return (SET_ERROR(ENOENT));

	return (dmu_object_info(ddt->ddt_os, ddt->ddt_object[type][class],
	    doi));
}

boolean_t
ddt_object_exists(ddt_t *ddt, enum ddt_type type, enum ddt_class class)
{
	return (!!ddt->ddt_object[type][class]);
}

void
ddt_object_name(ddt_t *ddt, enum ddt_type type, enum ddt_class class,
    char *name)
{
	(void) sprintf(name, DMU_POOL_DDT,
	    zio_checksum_table[ddt->ddt_checksum].ci_name,
	    ddt_ops[type]->ddt_op_name, ddt_class_name[class]);
}

void
ddt_bp_fill(const ddt_phys_t *ddp, blkptr_t *bp, uint64_t txg)
{
	ASSERT(txg != 0);

	for (int d = 0; d < SPA_DVAS_PER_BP; d++)
		bp->blk_dva[d] = ddp->ddp_dva[d];
	BP_SET_BIRTH(bp, txg, ddp->ddp_phys_birth);
}

void
ddt_bp_create(enum zio_checksum checksum,
    const ddt_key_t *ddk, const ddt_phys_t *ddp, blkptr_t *bp)
{
	BP_ZERO(bp);

	if (ddp != NULL)
		ddt_bp_fill(ddp, bp, ddp->ddp_phys_birth);

	bp->blk_cksum = ddk->ddk_cksum;
	bp->blk_fill = 1;

	BP_SET_LSIZE(bp, DDK_GET_LSIZE(ddk));
	BP_SET_PSIZE(bp, DDK_GET_PSIZE(ddk));
	BP_SET_COMPRESS(bp, DDK_GET_COMPRESS(ddk));
	BP_SET_CHECKSUM(bp, checksum);
	BP_SET_TYPE(bp, DMU_OT_DEDUP);
	BP_SET_LEVEL(bp, 0);
	BP_SET_DEDUP(bp, 0);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);
}

void
ddt_key_fill(ddt_key_t *ddk, const blkptr_t *bp)
{
	ddk->ddk_cksum = bp->blk_cksum;
	ddk->ddk_prop = 0;

	DDK_SET_LSIZE(ddk, BP_GET_LSIZE(bp));
	DDK_SET_PSIZE(ddk, BP_GET_PSIZE(bp));
	DDK_SET_COMPRESS(ddk, BP_GET_COMPRESS(bp));
}

void
ddt_phys_fill(ddt_phys_t *ddp, const blkptr_t *bp)
{
	ASSERT(ddp->ddp_phys_birth == 0);

	for (int d = 0; d < SPA_DVAS_PER_BP; d++)
		ddp->ddp_dva[d] = bp->blk_dva[d];
	ddp->ddp_phys_birth = BP_PHYSICAL_BIRTH(bp);
}

void
ddt_phys_clear(ddt_phys_t *ddp)
{
	bzero(ddp, sizeof (*ddp));
}

void
ddt_phys_addref(ddt_phys_t *ddp)
{
	ddp->ddp_refcnt++;
}

void
ddt_phys_decref(ddt_phys_t *ddp)
{
	ASSERT((int64_t)ddp->ddp_refcnt > 0);
	ddp->ddp_refcnt--;
}

void
ddt_phys_free(ddt_t *ddt, ddt_key_t *ddk, ddt_phys_t *ddp, uint64_t txg)
{
	blkptr_t blk;

	ddt_bp_create(ddt->ddt_checksum, ddk, ddp, &blk);
	ddt_phys_clear(ddp);
	zio_free(ddt->ddt_spa, txg, &blk);
}

ddt_phys_t *
ddt_phys_select(const ddt_entry_t *dde, const blkptr_t *bp)
{
	ddt_phys_t *ddp = (ddt_phys_t *)dde->dde_phys;

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		if (DVA_EQUAL(BP_IDENTITY(bp), &ddp->ddp_dva[0]) &&
		    BP_PHYSICAL_BIRTH(bp) == ddp->ddp_phys_birth)
			return (ddp);
	}
	return (NULL);
}

uint64_t
ddt_phys_total_refcnt(const ddt_entry_t *dde)
{
	uint64_t refcnt = 0;

	for (int p = DDT_PHYS_SINGLE; p <= DDT_PHYS_TRIPLE; p++)
		refcnt += dde->dde_phys[p].ddp_refcnt;

	return (refcnt);
}

static void
ddt_stat_generate(ddt_t *ddt, ddt_entry_t *dde, ddt_stat_t *dds)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_phys_t *ddp = dde->dde_phys;
	ddt_key_t *ddk = &dde->dde_key;
	uint64_t lsize = DDK_GET_LSIZE(ddk);
	uint64_t psize = DDK_GET_PSIZE(ddk);

	bzero(dds, sizeof (*dds));

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		uint64_t dsize = 0;
		uint64_t refcnt = ddp->ddp_refcnt;

		if (ddp->ddp_phys_birth == 0)
			continue;

		for (int d = 0; d < SPA_DVAS_PER_BP; d++)
			dsize += dva_get_dsize_sync(spa, &ddp->ddp_dva[d]);

		dds->dds_blocks += 1;
		dds->dds_lsize += lsize;
		dds->dds_psize += psize;
		dds->dds_dsize += dsize;

		dds->dds_ref_blocks += refcnt;
		dds->dds_ref_lsize += lsize * refcnt;
		dds->dds_ref_psize += psize * refcnt;
		dds->dds_ref_dsize += dsize * refcnt;
	}
}

void
ddt_stat_add(ddt_stat_t *dst, const ddt_stat_t *src, uint64_t neg)
{
	const uint64_t *s = (const uint64_t *)src;
	uint64_t *d = (uint64_t *)dst;
	uint64_t *d_end = (uint64_t *)(dst + 1);

	ASSERT(neg == 0 || neg == -1ULL);	/* add or subtract */

	while (d < d_end)
		*d++ += (*s++ ^ neg) - neg;
}

static void
ddt_stat_update(ddt_t *ddt, ddt_entry_t *dde, uint64_t neg)
{
	ddt_stat_t dds;
	ddt_histogram_t *ddh;
	int bucket;

	ddt_stat_generate(ddt, dde, &dds);

	bucket = highbit64(dds.dds_ref_blocks) - 1;
	ASSERT(bucket >= 0);

	ddh = &ddt->ddt_histogram[dde->dde_type][dde->dde_class];

	ddt_stat_add(&ddh->ddh_stat[bucket], &dds, neg);
}

void
ddt_histogram_add(ddt_histogram_t *dst, const ddt_histogram_t *src)
{
	for (int h = 0; h < 64; h++)
		ddt_stat_add(&dst->ddh_stat[h], &src->ddh_stat[h], 0);
}

void
ddt_histogram_stat(ddt_stat_t *dds, const ddt_histogram_t *ddh)
{
	bzero(dds, sizeof (*dds));

	for (int h = 0; h < 64; h++)
		ddt_stat_add(dds, &ddh->ddh_stat[h], 0);
}

boolean_t
ddt_histogram_empty(const ddt_histogram_t *ddh)
{
	const uint64_t *s = (const uint64_t *)ddh;
	const uint64_t *s_end = (const uint64_t *)(ddh + 1);

	while (s < s_end)
		if (*s++ != 0)
			return (B_FALSE);

	return (B_TRUE);
}

void
ddt_get_dedup_object_stats(spa_t *spa, ddt_object_t *ddo_total)
{
	/* Sum the statistics we cached in ddt_object_sync(). */
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
			for (enum ddt_class class = 0; class < DDT_CLASSES;
			    class++) {
				ddt_object_t *ddo =
				    &ddt->ddt_object_stats[type][class];
				ddo_total->ddo_count += ddo->ddo_count;
				ddo_total->ddo_dspace += ddo->ddo_dspace;
				ddo_total->ddo_mspace += ddo->ddo_mspace;
			}
		}
	}

	/* ... and compute the averages. */
	if (ddo_total->ddo_count != 0) {
		ddo_total->ddo_dspace /= ddo_total->ddo_count;
		ddo_total->ddo_mspace /= ddo_total->ddo_count;
	}
}

void
ddt_get_dedup_histogram(spa_t *spa, ddt_histogram_t *ddh)
{
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
			for (enum ddt_class class = 0; class < DDT_CLASSES;
			    class++) {
				ddt_histogram_add(ddh,
				    &ddt->ddt_histogram_cache[type][class]);
			}
		}
	}
}

void
ddt_get_dedup_stats(spa_t *spa, ddt_stat_t *dds_total)
{
	ddt_histogram_t *ddh_total;

	ddh_total = kmem_zalloc(sizeof (ddt_histogram_t), KM_SLEEP);
	ddt_get_dedup_histogram(spa, ddh_total);
	ddt_histogram_stat(dds_total, ddh_total);
	kmem_free(ddh_total, sizeof (ddt_histogram_t));
}

uint64_t
ddt_get_dedup_dspace(spa_t *spa)
{
	ddt_stat_t dds_total = { 0 };

	ddt_get_dedup_stats(spa, &dds_total);
	return (dds_total.dds_ref_dsize - dds_total.dds_dsize);
}

uint64_t
ddt_get_pool_dedup_ratio(spa_t *spa)
{
	ddt_stat_t dds_total = { 0 };

	ddt_get_dedup_stats(spa, &dds_total);
	if (dds_total.dds_dsize == 0)
		return (100);

	return (dds_total.dds_ref_dsize * 100 / dds_total.dds_dsize);
}

int
ddt_ditto_copies_needed(ddt_t *ddt, ddt_entry_t *dde, ddt_phys_t *ddp_willref)
{
	spa_t *spa = ddt->ddt_spa;
	uint64_t total_refcnt = 0;
	uint64_t ditto = spa->spa_dedup_ditto;
	int total_copies = 0;
	int desired_copies = 0;

	for (int p = DDT_PHYS_SINGLE; p <= DDT_PHYS_TRIPLE; p++) {
		ddt_phys_t *ddp = &dde->dde_phys[p];
		zio_t *zio = dde->dde_lead_zio[p];
		uint64_t refcnt = ddp->ddp_refcnt;	/* committed refs */
		if (zio != NULL)
			refcnt += zio->io_parent_count;	/* pending refs */
		if (ddp == ddp_willref)
			refcnt++;			/* caller's ref */
		if (refcnt != 0) {
			total_refcnt += refcnt;
			total_copies += p;
		}
	}

	if (ditto == 0 || ditto > UINT32_MAX)
		ditto = UINT32_MAX;

	if (total_refcnt >= 1)
		desired_copies++;
	if (total_refcnt >= ditto)
		desired_copies++;
	if (total_refcnt >= ditto * ditto)
		desired_copies++;

	return (MAX(desired_copies, total_copies) - total_copies);
}

int
ddt_ditto_copies_present(ddt_entry_t *dde)
{
	ddt_phys_t *ddp = &dde->dde_phys[DDT_PHYS_DITTO];
	dva_t *dva = ddp->ddp_dva;
	int copies = 0 - DVA_GET_GANG(dva);

	for (int d = 0; d < SPA_DVAS_PER_BP; d++, dva++)
		if (DVA_IS_VALID(dva))
			copies++;

	ASSERT(copies >= 0 && copies < SPA_DVAS_PER_BP);

	return (copies);
}

size_t
ddt_compress(void *src, uchar_t *dst, size_t s_len, size_t d_len)
{
	uchar_t *version = dst++;
	int cpfunc = ZIO_COMPRESS_ZLE;
	zio_compress_info_t *ci = &zio_compress_table[cpfunc];
	size_t c_len;

	ASSERT(d_len >= s_len + 1);	/* no compression plus version byte */

	c_len = ci->ci_compress(src, dst, s_len, d_len - 1, ci->ci_level);

	if (c_len == s_len) {
		cpfunc = ZIO_COMPRESS_OFF;
		bcopy(src, dst, s_len);
	}

	*version = cpfunc;
	/* CONSTCOND */
	if (ZFS_HOST_BYTEORDER)
		*version |= DDT_COMPRESS_BYTEORDER_MASK;

	return (c_len + 1);
}

void
ddt_decompress(uchar_t *src, void *dst, size_t s_len, size_t d_len)
{
	uchar_t version = *src++;
	int cpfunc = version & DDT_COMPRESS_FUNCTION_MASK;
	zio_compress_info_t *ci = &zio_compress_table[cpfunc];

	if (ci->ci_decompress != NULL)
		(void) ci->ci_decompress(src, dst, s_len, d_len, ci->ci_level);
	else
		bcopy(src, dst, d_len);

	if (((version & DDT_COMPRESS_BYTEORDER_MASK) != 0) !=
	    (ZFS_HOST_BYTEORDER != 0))
		byteswap_uint64_array(dst, d_len);
}

ddt_t *
ddt_select_by_checksum(spa_t *spa, enum zio_checksum c)
{
	return (spa->spa_ddt[c]);
}

ddt_t *
ddt_select(spa_t *spa, const blkptr_t *bp)
{
	return (spa->spa_ddt[BP_GET_CHECKSUM(bp)]);
}

void
ddt_enter(ddt_t *ddt)
{
	mutex_enter(&ddt->ddt_lock);
}

void
ddt_exit(ddt_t *ddt)
{
	mutex_exit(&ddt->ddt_lock);
}

static ddt_entry_t *
ddt_alloc(const ddt_key_t *ddk)
{
	ddt_entry_t *dde;

	dde = kmem_zalloc(sizeof (ddt_entry_t), KM_SLEEP);
	cv_init(&dde->dde_cv, NULL, CV_DEFAULT, NULL);

	dde->dde_key = *ddk;

	return (dde);
}

static void
ddt_free(ddt_entry_t *dde)
{
	ASSERT(!dde->dde_loading);

	for (int p = 0; p < DDT_PHYS_TYPES; p++)
		ASSERT(dde->dde_lead_zio[p] == NULL);

	if (dde->dde_repair_abd != NULL)
		abd_free(dde->dde_repair_abd);

	cv_destroy(&dde->dde_cv);
	kmem_free(dde, sizeof (*dde));
}

void
ddt_remove(ddt_t *ddt, ddt_entry_t *dde)
{
	ASSERT(MUTEX_HELD(&ddt->ddt_lock));

	avl_remove(&ddt->ddt_tree, dde);
	ddt_free(dde);
}

ddt_entry_t *
ddt_lookup(ddt_t *ddt, const blkptr_t *bp, boolean_t add)
{
	ddt_entry_t *dde, dde_search;
	enum ddt_type type;
	enum ddt_class class;
	avl_index_t where;
	int error;

	ASSERT(MUTEX_HELD(&ddt->ddt_lock));

	ddt_key_fill(&dde_search.dde_key, bp);

	dde = avl_find(&ddt->ddt_tree, &dde_search, &where);
	if (dde == NULL) {
		if (!add)
			return (NULL);
		dde = ddt_alloc(&dde_search.dde_key);
		avl_insert(&ddt->ddt_tree, dde, where);
	}

	while (dde->dde_loading)
		cv_wait(&dde->dde_cv, &ddt->ddt_lock);

	if (dde->dde_loaded)
		return (dde);

	dde->dde_loading = B_TRUE;

	ddt_exit(ddt);

	error = ENOENT;

	for (type = 0; type < DDT_TYPES; type++) {
		for (class = 0; class < DDT_CLASSES; class++) {
			error = ddt_object_lookup(ddt, type, class, dde);
			if (error != ENOENT) {
				ASSERT0(error);
				break;
			}
		}
		if (error != ENOENT)
			break;
	}

	ddt_enter(ddt);

	ASSERT(dde->dde_loaded == B_FALSE);
	ASSERT(dde->dde_loading == B_TRUE);

	dde->dde_type = type;	/* will be DDT_TYPES if no entry found */
	dde->dde_class = class;	/* will be DDT_CLASSES if no entry found */
	dde->dde_loaded = B_TRUE;
	dde->dde_loading = B_FALSE;

	if (error == 0)
		ddt_stat_update(ddt, dde, -1ULL);

	cv_broadcast(&dde->dde_cv);

	return (dde);
}

void
ddt_prefetch(spa_t *spa, const blkptr_t *bp)
{
	ddt_t *ddt;
	ddt_entry_t dde;

	if (!zfs_dedup_prefetch || bp == NULL || !BP_GET_DEDUP(bp))
		return;

	/*
	 * We only remove the DDT once all tables are empty and only
	 * prefetch dedup blocks when there are entries in the DDT.
	 * Thus no locking is required as the DDT can't disappear on us.
	 */
	ddt = ddt_select(spa, bp);
	ddt_key_fill(&dde.dde_key, bp);

	for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
		for (enum ddt_class class = 0; class < DDT_CLASSES; class++) {
			ddt_object_prefetch(ddt, type, class, &dde);
		}
	}
}

int
ddt_entry_compare(const void *x1, const void *x2)
{
	const ddt_entry_t *dde1 = x1;
	const ddt_entry_t *dde2 = x2;
	const uint64_t *u1 = (const uint64_t *)&dde1->dde_key;
	const uint64_t *u2 = (const uint64_t *)&dde2->dde_key;

	for (int i = 0; i < DDT_KEY_WORDS; i++) {
		if (u1[i] < u2[i])
			return (-1);
		if (u1[i] > u2[i])
			return (1);
	}

	return (0);
}

static ddt_t *
ddt_table_alloc(spa_t *spa, enum zio_checksum c)
{
	ddt_t *ddt;

	ddt = kmem_zalloc(sizeof (*ddt), KM_SLEEP);

	mutex_init(&ddt->ddt_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&ddt->ddt_tree, ddt_entry_compare,
	    sizeof (ddt_entry_t), offsetof(ddt_entry_t, dde_node));
	avl_create(&ddt->ddt_repair_tree, ddt_entry_compare,
	    sizeof (ddt_entry_t), offsetof(ddt_entry_t, dde_node));
	ddt->ddt_checksum = c;
	ddt->ddt_spa = spa;
	ddt->ddt_os = spa->spa_meta_objset;

	return (ddt);
}

static void
ddt_table_free(ddt_t *ddt)
{
	ASSERT(avl_numnodes(&ddt->ddt_tree) == 0);
	ASSERT(avl_numnodes(&ddt->ddt_repair_tree) == 0);
	avl_destroy(&ddt->ddt_tree);
	avl_destroy(&ddt->ddt_repair_tree);
	mutex_destroy(&ddt->ddt_lock);
	kmem_free(ddt, sizeof (*ddt));
}

void
ddt_create(spa_t *spa)
{
	spa->spa_dedup_checksum = ZIO_DEDUPCHECKSUM;

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++)
		spa->spa_ddt[c] = ddt_table_alloc(spa, c);
}

int
ddt_load(spa_t *spa)
{
	int error;

	ddt_create(spa);

	error = zap_lookup(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_DDT_STATS, sizeof (uint64_t), 1,
	    &spa->spa_ddt_stat_object);

	if (error)
		return (error == ENOENT ? 0 : error);

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
			for (enum ddt_class class = 0; class < DDT_CLASSES;
			    class++) {
				error = ddt_object_load(ddt, type, class);
				if (error != 0 && error != ENOENT)
					return (error);
			}
		}

		/*
		 * Seed the cached histograms.
		 */
		bcopy(ddt->ddt_histogram, &ddt->ddt_histogram_cache,
		    sizeof (ddt->ddt_histogram));
	}

	return (0);
}

void
ddt_unload(spa_t *spa)
{
	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		if (spa->spa_ddt[c]) {
			ddt_table_free(spa->spa_ddt[c]);
			spa->spa_ddt[c] = NULL;
		}
	}
}

boolean_t
ddt_class_contains(spa_t *spa, enum ddt_class max_class, const blkptr_t *bp)
{
	ddt_t *ddt;
	ddt_entry_t dde;

	if (!BP_GET_DEDUP(bp))
		return (B_FALSE);

	if (max_class == DDT_CLASS_UNIQUE)
		return (B_TRUE);

	ddt = spa->spa_ddt[BP_GET_CHECKSUM(bp)];

	ddt_key_fill(&dde.dde_key, bp);

	for (enum ddt_type type = 0; type < DDT_TYPES; type++)
		for (enum ddt_class class = 0; class <= max_class; class++)
			if (ddt_object_lookup(ddt, type, class, &dde) == 0)
				return (B_TRUE);

	return (B_FALSE);
}

ddt_entry_t *
ddt_repair_start(ddt_t *ddt, const blkptr_t *bp)
{
	ddt_key_t ddk;
	ddt_entry_t *dde;

	ddt_key_fill(&ddk, bp);

	dde = ddt_alloc(&ddk);

	for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
		for (enum ddt_class class = 0; class < DDT_CLASSES; class++) {
			/*
			 * We can only do repair if there are multiple copies
			 * of the block.  For anything in the UNIQUE class,
			 * there's definitely only one copy, so don't even try.
			 */
			if (class != DDT_CLASS_UNIQUE &&
			    ddt_object_lookup(ddt, type, class, dde) == 0)
				return (dde);
		}
	}

	bzero(dde->dde_phys, sizeof (dde->dde_phys));

	return (dde);
}

void
ddt_repair_done(ddt_t *ddt, ddt_entry_t *dde)
{
	avl_index_t where;

	ddt_enter(ddt);

	if (dde->dde_repair_abd != NULL && spa_writeable(ddt->ddt_spa) &&
	    avl_find(&ddt->ddt_repair_tree, dde, &where) == NULL)
		avl_insert(&ddt->ddt_repair_tree, dde, where);
	else
		ddt_free(dde);

	ddt_exit(ddt);
}

static void
ddt_repair_entry_done(zio_t *zio)
{
	ddt_entry_t *rdde = zio->io_private;

	ddt_free(rdde);
}

static void
ddt_repair_entry(ddt_t *ddt, ddt_entry_t *dde, ddt_entry_t *rdde, zio_t *rio)
{
	ddt_phys_t *ddp = dde->dde_phys;
	ddt_phys_t *rddp = rdde->dde_phys;
	ddt_key_t *ddk = &dde->dde_key;
	ddt_key_t *rddk = &rdde->dde_key;
	zio_t *zio;
	blkptr_t blk;

	zio = zio_null(rio, rio->io_spa, NULL,
	    ddt_repair_entry_done, rdde, rio->io_flags);

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++, rddp++) {
		if (ddp->ddp_phys_birth == 0 ||
		    ddp->ddp_phys_birth != rddp->ddp_phys_birth ||
		    bcmp(ddp->ddp_dva, rddp->ddp_dva, sizeof (ddp->ddp_dva)))
			continue;
		ddt_bp_create(ddt->ddt_checksum, ddk, ddp, &blk);
		zio_nowait(zio_rewrite(zio, zio->io_spa, 0, &blk,
		    rdde->dde_repair_abd, DDK_GET_PSIZE(rddk), NULL, NULL,
		    ZIO_PRIORITY_SYNC_WRITE, ZIO_DDT_CHILD_FLAGS(zio), NULL));
	}

	zio_nowait(zio);
}

static void
ddt_repair_table(ddt_t *ddt, zio_t *rio)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_entry_t *dde, *rdde_next, *rdde;
	avl_tree_t *t = &ddt->ddt_repair_tree;
	blkptr_t blk;

	if (spa_sync_pass(spa) > 1)
		return;

	ddt_enter(ddt);
	for (rdde = avl_first(t); rdde != NULL; rdde = rdde_next) {
		rdde_next = AVL_NEXT(t, rdde);
		avl_remove(&ddt->ddt_repair_tree, rdde);
		ddt_exit(ddt);
		ddt_bp_create(ddt->ddt_checksum, &rdde->dde_key, NULL, &blk);
		dde = ddt_repair_start(ddt, &blk);
		ddt_repair_entry(ddt, dde, rdde, rio);
		ddt_repair_done(ddt, dde);
		ddt_enter(ddt);
	}
	ddt_exit(ddt);
}

static void
ddt_sync_entry(ddt_t *ddt, ddt_entry_t *dde, dmu_tx_t *tx, uint64_t txg)
{
	dsl_pool_t *dp = ddt->ddt_spa->spa_dsl_pool;
	ddt_phys_t *ddp = dde->dde_phys;
	ddt_key_t *ddk = &dde->dde_key;
	enum ddt_type otype = dde->dde_type;
	enum ddt_type ntype = DDT_TYPE_CURRENT;
	enum ddt_class oclass = dde->dde_class;
	enum ddt_class nclass;
	uint64_t total_refcnt = 0;

	ASSERT(dde->dde_loaded);
	ASSERT(!dde->dde_loading);

	for (int p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		ASSERT(dde->dde_lead_zio[p] == NULL);
		ASSERT((int64_t)ddp->ddp_refcnt >= 0);
		if (ddp->ddp_phys_birth == 0) {
			ASSERT(ddp->ddp_refcnt == 0);
			continue;
		}
		if (p == DDT_PHYS_DITTO) {
			if (ddt_ditto_copies_needed(ddt, dde, NULL) == 0)
				ddt_phys_free(ddt, ddk, ddp, txg);
			continue;
		}
		if (ddp->ddp_refcnt == 0)
			ddt_phys_free(ddt, ddk, ddp, txg);
		total_refcnt += ddp->ddp_refcnt;
	}

	if (dde->dde_phys[DDT_PHYS_DITTO].ddp_phys_birth != 0)
		nclass = DDT_CLASS_DITTO;
	else if (total_refcnt > 1)
		nclass = DDT_CLASS_DUPLICATE;
	else
		nclass = DDT_CLASS_UNIQUE;

	if (otype != DDT_TYPES &&
	    (otype != ntype || oclass != nclass || total_refcnt == 0)) {
		VERIFY(ddt_object_remove(ddt, otype, oclass, dde, tx) == 0);
		ASSERT(ddt_object_lookup(ddt, otype, oclass, dde) == ENOENT);
	}

	if (total_refcnt != 0) {
		dde->dde_type = ntype;
		dde->dde_class = nclass;
		ddt_stat_update(ddt, dde, 0);
		if (!ddt_object_exists(ddt, ntype, nclass))
			ddt_object_create(ddt, ntype, nclass, tx);
		VERIFY(ddt_object_update(ddt, ntype, nclass, dde, tx) == 0);

		/*
		 * If the class changes, the order that we scan this bp
		 * changes.  If it decreases, we could miss it, so
		 * scan it right now.  (This covers both class changing
		 * while we are doing ddt_walk(), and when we are
		 * traversing.)
		 */
		if (nclass < oclass) {
			dsl_scan_ddt_entry(dp->dp_scan,
			    ddt->ddt_checksum, dde, tx);
		}
	}
}

static void
ddt_sync_table(ddt_t *ddt, dmu_tx_t *tx, uint64_t txg)
{
	spa_t *spa = ddt->ddt_spa;
	ddt_entry_t *dde;
	void *cookie = NULL;

	if (avl_numnodes(&ddt->ddt_tree) == 0)
		return;

	ASSERT(spa->spa_uberblock.ub_version >= SPA_VERSION_DEDUP);

	if (spa->spa_ddt_stat_object == 0) {
		spa->spa_ddt_stat_object = zap_create_link(ddt->ddt_os,
		    DMU_OT_DDT_STATS, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_DDT_STATS, tx);
	}

	while ((dde = avl_destroy_nodes(&ddt->ddt_tree, &cookie)) != NULL) {
		ddt_sync_entry(ddt, dde, tx, txg);
		ddt_free(dde);
	}

	for (enum ddt_type type = 0; type < DDT_TYPES; type++) {
		uint64_t add, count = 0;
		for (enum ddt_class class = 0; class < DDT_CLASSES; class++) {
			if (ddt_object_exists(ddt, type, class)) {
				ddt_object_sync(ddt, type, class, tx);
				VERIFY(ddt_object_count(ddt, type, class,
				    &add) == 0);
				count += add;
			}
		}
		for (enum ddt_class class = 0; class < DDT_CLASSES; class++) {
			if (count == 0 && ddt_object_exists(ddt, type, class))
				ddt_object_destroy(ddt, type, class, tx);
		}
	}

	bcopy(ddt->ddt_histogram, &ddt->ddt_histogram_cache,
	    sizeof (ddt->ddt_histogram));
}

void
ddt_sync(spa_t *spa, uint64_t txg)
{
	dsl_scan_t *scn = spa->spa_dsl_pool->dp_scan;
	dmu_tx_t *tx;
	zio_t *rio;

	ASSERT(spa_syncing_txg(spa) == txg);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	rio = zio_root(spa, NULL, NULL,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SELF_HEAL);

	/*
	 * This function may cause an immediate scan of ddt blocks (see
	 * the comment above dsl_scan_ddt() for details). We set the
	 * scan's root zio here so that we can wait for any scan IOs in
	 * addition to the regular ddt IOs.
	 */
	ASSERT3P(scn->scn_zio_root, ==, NULL);
	scn->scn_zio_root = rio;

	for (enum zio_checksum c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		if (ddt == NULL)
			continue;
		ddt_sync_table(ddt, tx, txg);
		ddt_repair_table(ddt, rio);
	}

	(void) zio_wait(rio);
	scn->scn_zio_root = NULL;

	dmu_tx_commit(tx);
}

int
ddt_walk(spa_t *spa, ddt_bookmark_t *ddb, ddt_entry_t *dde)
{
	do {
		do {
			do {
				ddt_t *ddt = spa->spa_ddt[ddb->ddb_checksum];
				int error = ENOENT;
				if (ddt_object_exists(ddt, ddb->ddb_type,
				    ddb->ddb_class)) {
					error = ddt_object_walk(ddt,
					    ddb->ddb_type, ddb->ddb_class,
					    &ddb->ddb_cursor, dde);
				}
				dde->dde_type = ddb->ddb_type;
				dde->dde_class = ddb->ddb_class;
				if (error == 0)
					return (0);
				if (error != ENOENT)
					return (error);
				ddb->ddb_cursor = 0;
			} while (++ddb->ddb_checksum < ZIO_CHECKSUM_FUNCTIONS);
			ddb->ddb_checksum = 0;
		} while (++ddb->ddb_type < DDT_TYPES);
		ddb->ddb_type = 0;
	} while (++ddb->ddb_class < DDT_CLASSES);

	return (SET_ERROR(ENOENT));
}
