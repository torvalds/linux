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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/space_map.h>
#include <sys/metaslab_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/spa_impl.h>
#include <sys/zfeature.h>

#define	WITH_DF_BLOCK_ALLOCATOR

/*
 * Allow allocations to switch to gang blocks quickly. We do this to
 * avoid having to load lots of space_maps in a given txg. There are,
 * however, some cases where we want to avoid "fast" ganging and instead
 * we want to do an exhaustive search of all metaslabs on this device.
 * Currently we don't allow any gang, slog, or dump device related allocations
 * to "fast" gang.
 */
#define	CAN_FASTGANG(flags) \
	(!((flags) & (METASLAB_GANG_CHILD | METASLAB_GANG_HEADER | \
	METASLAB_GANG_AVOID)))

#define	METASLAB_WEIGHT_PRIMARY		(1ULL << 63)
#define	METASLAB_WEIGHT_SECONDARY	(1ULL << 62)
#define	METASLAB_ACTIVE_MASK		\
	(METASLAB_WEIGHT_PRIMARY | METASLAB_WEIGHT_SECONDARY)

/*
 * Metaslab granularity, in bytes. This is roughly similar to what would be
 * referred to as the "stripe size" in traditional RAID arrays. In normal
 * operation, we will try to write this amount of data to a top-level vdev
 * before moving on to the next one.
 */
unsigned long metaslab_aliquot = 512 << 10;

uint64_t metaslab_gang_bang = SPA_MAXBLOCKSIZE + 1;	/* force gang blocks */

/*
 * The in-core space map representation is more compact than its on-disk form.
 * The zfs_condense_pct determines how much more compact the in-core
 * space_map representation must be before we compact it on-disk.
 * Values should be greater than or equal to 100.
 */
int zfs_condense_pct = 200;

/*
 * Condensing a metaslab is not guaranteed to actually reduce the amount of
 * space used on disk. In particular, a space map uses data in increments of
 * MAX(1 << ashift, space_map_blksz), so a metaslab might use the
 * same number of blocks after condensing. Since the goal of condensing is to
 * reduce the number of IOPs required to read the space map, we only want to
 * condense when we can be sure we will reduce the number of blocks used by the
 * space map. Unfortunately, we cannot precisely compute whether or not this is
 * the case in metaslab_should_condense since we are holding ms_lock. Instead,
 * we apply the following heuristic: do not condense a spacemap unless the
 * uncondensed size consumes greater than zfs_metaslab_condense_block_threshold
 * blocks.
 */
int zfs_metaslab_condense_block_threshold = 4;

/*
 * The zfs_mg_noalloc_threshold defines which metaslab groups should
 * be eligible for allocation. The value is defined as a percentage of
 * free space. Metaslab groups that have more free space than
 * zfs_mg_noalloc_threshold are always eligible for allocations. Once
 * a metaslab group's free space is less than or equal to the
 * zfs_mg_noalloc_threshold the allocator will avoid allocating to that
 * group unless all groups in the pool have reached zfs_mg_noalloc_threshold.
 * Once all groups in the pool reach zfs_mg_noalloc_threshold then all
 * groups are allowed to accept allocations. Gang blocks are always
 * eligible to allocate on any metaslab group. The default value of 0 means
 * no metaslab group will be excluded based on this criterion.
 */
int zfs_mg_noalloc_threshold = 0;

/*
 * Metaslab groups are considered eligible for allocations if their
 * fragmenation metric (measured as a percentage) is less than or equal to
 * zfs_mg_fragmentation_threshold. If a metaslab group exceeds this threshold
 * then it will be skipped unless all metaslab groups within the metaslab
 * class have also crossed this threshold.
 */
int zfs_mg_fragmentation_threshold = 85;

/*
 * Allow metaslabs to keep their active state as long as their fragmentation
 * percentage is less than or equal to zfs_metaslab_fragmentation_threshold. An
 * active metaslab that exceeds this threshold will no longer keep its active
 * status allowing better metaslabs to be selected.
 */
int zfs_metaslab_fragmentation_threshold = 70;

/*
 * When set will load all metaslabs when pool is first opened.
 */
int metaslab_debug_load = 0;

/*
 * When set will prevent metaslabs from being unloaded.
 */
int metaslab_debug_unload = 0;

/*
 * Minimum size which forces the dynamic allocator to change
 * it's allocation strategy.  Once the space map cannot satisfy
 * an allocation of this size then it switches to using more
 * aggressive strategy (i.e search by size rather than offset).
 */
uint64_t metaslab_df_alloc_threshold = SPA_MAXBLOCKSIZE;

/*
 * The minimum free space, in percent, which must be available
 * in a space map to continue allocations in a first-fit fashion.
 * Once the space_map's free space drops below this level we dynamically
 * switch to using best-fit allocations.
 */
int metaslab_df_free_pct = 4;

/*
 * Percentage of all cpus that can be used by the metaslab taskq.
 */
int metaslab_load_pct = 50;

/*
 * Determines how many txgs a metaslab may remain loaded without having any
 * allocations from it. As long as a metaslab continues to be used we will
 * keep it loaded.
 */
int metaslab_unload_delay = TXG_SIZE * 2;

/*
 * Max number of metaslabs per group to preload.
 */
int metaslab_preload_limit = SPA_DVAS_PER_BP;

/*
 * Enable/disable preloading of metaslab.
 */
int metaslab_preload_enabled = B_TRUE;

/*
 * Enable/disable fragmentation weighting on metaslabs.
 */
int metaslab_fragmentation_factor_enabled = B_TRUE;

/*
 * Enable/disable lba weighting (i.e. outer tracks are given preference).
 */
int metaslab_lba_weighting_enabled = B_TRUE;

/*
 * Enable/disable metaslab group biasing.
 */
int metaslab_bias_enabled = B_TRUE;

static uint64_t metaslab_fragmentation(metaslab_t *);

/*
 * ==========================================================================
 * Metaslab classes
 * ==========================================================================
 */
metaslab_class_t *
metaslab_class_create(spa_t *spa, metaslab_ops_t *ops)
{
	metaslab_class_t *mc;

	mc = kmem_zalloc(sizeof (metaslab_class_t), KM_SLEEP);

	mc->mc_spa = spa;
	mc->mc_rotor = NULL;
	mc->mc_ops = ops;
	mutex_init(&mc->mc_fastwrite_lock, NULL, MUTEX_DEFAULT, NULL);

	return (mc);
}

void
metaslab_class_destroy(metaslab_class_t *mc)
{
	ASSERT(mc->mc_rotor == NULL);
	ASSERT(mc->mc_alloc == 0);
	ASSERT(mc->mc_deferred == 0);
	ASSERT(mc->mc_space == 0);
	ASSERT(mc->mc_dspace == 0);

	mutex_destroy(&mc->mc_fastwrite_lock);
	kmem_free(mc, sizeof (metaslab_class_t));
}

int
metaslab_class_validate(metaslab_class_t *mc)
{
	metaslab_group_t *mg;
	vdev_t *vd;

	/*
	 * Must hold one of the spa_config locks.
	 */
	ASSERT(spa_config_held(mc->mc_spa, SCL_ALL, RW_READER) ||
	    spa_config_held(mc->mc_spa, SCL_ALL, RW_WRITER));

	if ((mg = mc->mc_rotor) == NULL)
		return (0);

	do {
		vd = mg->mg_vd;
		ASSERT(vd->vdev_mg != NULL);
		ASSERT3P(vd->vdev_top, ==, vd);
		ASSERT3P(mg->mg_class, ==, mc);
		ASSERT3P(vd->vdev_ops, !=, &vdev_hole_ops);
	} while ((mg = mg->mg_next) != mc->mc_rotor);

	return (0);
}

void
metaslab_class_space_update(metaslab_class_t *mc, int64_t alloc_delta,
    int64_t defer_delta, int64_t space_delta, int64_t dspace_delta)
{
	atomic_add_64(&mc->mc_alloc, alloc_delta);
	atomic_add_64(&mc->mc_deferred, defer_delta);
	atomic_add_64(&mc->mc_space, space_delta);
	atomic_add_64(&mc->mc_dspace, dspace_delta);
}

uint64_t
metaslab_class_get_alloc(metaslab_class_t *mc)
{
	return (mc->mc_alloc);
}

uint64_t
metaslab_class_get_deferred(metaslab_class_t *mc)
{
	return (mc->mc_deferred);
}

uint64_t
metaslab_class_get_space(metaslab_class_t *mc)
{
	return (mc->mc_space);
}

uint64_t
metaslab_class_get_dspace(metaslab_class_t *mc)
{
	return (spa_deflate(mc->mc_spa) ? mc->mc_dspace : mc->mc_space);
}

void
metaslab_class_histogram_verify(metaslab_class_t *mc)
{
	vdev_t *rvd = mc->mc_spa->spa_root_vdev;
	uint64_t *mc_hist;
	int i, c;

	if ((zfs_flags & ZFS_DEBUG_HISTOGRAM_VERIFY) == 0)
		return;

	mc_hist = kmem_zalloc(sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE,
	    KM_SLEEP);

	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		/*
		 * Skip any holes, uninitialized top-levels, or
		 * vdevs that are not in this metalab class.
		 */
		if (tvd->vdev_ishole || tvd->vdev_ms_shift == 0 ||
		    mg->mg_class != mc) {
			continue;
		}

		for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++)
			mc_hist[i] += mg->mg_histogram[i];
	}

	for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i++)
		VERIFY3U(mc_hist[i], ==, mc->mc_histogram[i]);

	kmem_free(mc_hist, sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE);
}

/*
 * Calculate the metaslab class's fragmentation metric. The metric
 * is weighted based on the space contribution of each metaslab group.
 * The return value will be a number between 0 and 100 (inclusive), or
 * ZFS_FRAG_INVALID if the metric has not been set. See comment above the
 * zfs_frag_table for more information about the metric.
 */
uint64_t
metaslab_class_fragmentation(metaslab_class_t *mc)
{
	vdev_t *rvd = mc->mc_spa->spa_root_vdev;
	uint64_t fragmentation = 0;
	int c;

	spa_config_enter(mc->mc_spa, SCL_VDEV, FTAG, RW_READER);

	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		/*
		 * Skip any holes, uninitialized top-levels, or
		 * vdevs that are not in this metalab class.
		 */
		if (tvd->vdev_ishole || tvd->vdev_ms_shift == 0 ||
		    mg->mg_class != mc) {
			continue;
		}

		/*
		 * If a metaslab group does not contain a fragmentation
		 * metric then just bail out.
		 */
		if (mg->mg_fragmentation == ZFS_FRAG_INVALID) {
			spa_config_exit(mc->mc_spa, SCL_VDEV, FTAG);
			return (ZFS_FRAG_INVALID);
		}

		/*
		 * Determine how much this metaslab_group is contributing
		 * to the overall pool fragmentation metric.
		 */
		fragmentation += mg->mg_fragmentation *
		    metaslab_group_get_space(mg);
	}
	fragmentation /= metaslab_class_get_space(mc);

	ASSERT3U(fragmentation, <=, 100);
	spa_config_exit(mc->mc_spa, SCL_VDEV, FTAG);
	return (fragmentation);
}

/*
 * Calculate the amount of expandable space that is available in
 * this metaslab class. If a device is expanded then its expandable
 * space will be the amount of allocatable space that is currently not
 * part of this metaslab class.
 */
uint64_t
metaslab_class_expandable_space(metaslab_class_t *mc)
{
	vdev_t *rvd = mc->mc_spa->spa_root_vdev;
	uint64_t space = 0;
	int c;

	spa_config_enter(mc->mc_spa, SCL_VDEV, FTAG, RW_READER);
	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		if (tvd->vdev_ishole || tvd->vdev_ms_shift == 0 ||
		    mg->mg_class != mc) {
			continue;
		}

		space += tvd->vdev_max_asize - tvd->vdev_asize;
	}
	spa_config_exit(mc->mc_spa, SCL_VDEV, FTAG);
	return (space);
}

/*
 * ==========================================================================
 * Metaslab groups
 * ==========================================================================
 */
static int
metaslab_compare(const void *x1, const void *x2)
{
	const metaslab_t *m1 = x1;
	const metaslab_t *m2 = x2;

	if (m1->ms_weight < m2->ms_weight)
		return (1);
	if (m1->ms_weight > m2->ms_weight)
		return (-1);

	/*
	 * If the weights are identical, use the offset to force uniqueness.
	 */
	if (m1->ms_start < m2->ms_start)
		return (-1);
	if (m1->ms_start > m2->ms_start)
		return (1);

	ASSERT3P(m1, ==, m2);

	return (0);
}

/*
 * Update the allocatable flag and the metaslab group's capacity.
 * The allocatable flag is set to true if the capacity is below
 * the zfs_mg_noalloc_threshold. If a metaslab group transitions
 * from allocatable to non-allocatable or vice versa then the metaslab
 * group's class is updated to reflect the transition.
 */
static void
metaslab_group_alloc_update(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	metaslab_class_t *mc = mg->mg_class;
	vdev_stat_t *vs = &vd->vdev_stat;
	boolean_t was_allocatable;

	ASSERT(vd == vd->vdev_top);

	mutex_enter(&mg->mg_lock);
	was_allocatable = mg->mg_allocatable;

	mg->mg_free_capacity = ((vs->vs_space - vs->vs_alloc) * 100) /
	    (vs->vs_space + 1);

	/*
	 * A metaslab group is considered allocatable if it has plenty
	 * of free space or is not heavily fragmented. We only take
	 * fragmentation into account if the metaslab group has a valid
	 * fragmentation metric (i.e. a value between 0 and 100).
	 */
	mg->mg_allocatable = (mg->mg_free_capacity > zfs_mg_noalloc_threshold &&
	    (mg->mg_fragmentation == ZFS_FRAG_INVALID ||
	    mg->mg_fragmentation <= zfs_mg_fragmentation_threshold));

	/*
	 * The mc_alloc_groups maintains a count of the number of
	 * groups in this metaslab class that are still above the
	 * zfs_mg_noalloc_threshold. This is used by the allocating
	 * threads to determine if they should avoid allocations to
	 * a given group. The allocator will avoid allocations to a group
	 * if that group has reached or is below the zfs_mg_noalloc_threshold
	 * and there are still other groups that are above the threshold.
	 * When a group transitions from allocatable to non-allocatable or
	 * vice versa we update the metaslab class to reflect that change.
	 * When the mc_alloc_groups value drops to 0 that means that all
	 * groups have reached the zfs_mg_noalloc_threshold making all groups
	 * eligible for allocations. This effectively means that all devices
	 * are balanced again.
	 */
	if (was_allocatable && !mg->mg_allocatable)
		mc->mc_alloc_groups--;
	else if (!was_allocatable && mg->mg_allocatable)
		mc->mc_alloc_groups++;

	mutex_exit(&mg->mg_lock);
}

metaslab_group_t *
metaslab_group_create(metaslab_class_t *mc, vdev_t *vd)
{
	metaslab_group_t *mg;

	mg = kmem_zalloc(sizeof (metaslab_group_t), KM_SLEEP);
	mutex_init(&mg->mg_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&mg->mg_metaslab_tree, metaslab_compare,
	    sizeof (metaslab_t), offsetof(struct metaslab, ms_group_node));
	mg->mg_vd = vd;
	mg->mg_class = mc;
	mg->mg_activation_count = 0;

	mg->mg_taskq = taskq_create("metaslab_group_taskq", metaslab_load_pct,
	    maxclsyspri, 10, INT_MAX, TASKQ_THREADS_CPU_PCT | TASKQ_DYNAMIC);

	return (mg);
}

void
metaslab_group_destroy(metaslab_group_t *mg)
{
	ASSERT(mg->mg_prev == NULL);
	ASSERT(mg->mg_next == NULL);
	/*
	 * We may have gone below zero with the activation count
	 * either because we never activated in the first place or
	 * because we're done, and possibly removing the vdev.
	 */
	ASSERT(mg->mg_activation_count <= 0);

	taskq_destroy(mg->mg_taskq);
	avl_destroy(&mg->mg_metaslab_tree);
	mutex_destroy(&mg->mg_lock);
	kmem_free(mg, sizeof (metaslab_group_t));
}

void
metaslab_group_activate(metaslab_group_t *mg)
{
	metaslab_class_t *mc = mg->mg_class;
	metaslab_group_t *mgprev, *mgnext;

	ASSERT(spa_config_held(mc->mc_spa, SCL_ALLOC, RW_WRITER));

	ASSERT(mc->mc_rotor != mg);
	ASSERT(mg->mg_prev == NULL);
	ASSERT(mg->mg_next == NULL);
	ASSERT(mg->mg_activation_count <= 0);

	if (++mg->mg_activation_count <= 0)
		return;

	mg->mg_aliquot = metaslab_aliquot * MAX(1, mg->mg_vd->vdev_children);
	metaslab_group_alloc_update(mg);

	if ((mgprev = mc->mc_rotor) == NULL) {
		mg->mg_prev = mg;
		mg->mg_next = mg;
	} else {
		mgnext = mgprev->mg_next;
		mg->mg_prev = mgprev;
		mg->mg_next = mgnext;
		mgprev->mg_next = mg;
		mgnext->mg_prev = mg;
	}
	mc->mc_rotor = mg;
}

void
metaslab_group_passivate(metaslab_group_t *mg)
{
	metaslab_class_t *mc = mg->mg_class;
	metaslab_group_t *mgprev, *mgnext;

	ASSERT(spa_config_held(mc->mc_spa, SCL_ALLOC, RW_WRITER));

	if (--mg->mg_activation_count != 0) {
		ASSERT(mc->mc_rotor != mg);
		ASSERT(mg->mg_prev == NULL);
		ASSERT(mg->mg_next == NULL);
		ASSERT(mg->mg_activation_count < 0);
		return;
	}

	taskq_wait_outstanding(mg->mg_taskq, 0);
	metaslab_group_alloc_update(mg);

	mgprev = mg->mg_prev;
	mgnext = mg->mg_next;

	if (mg == mgnext) {
		mc->mc_rotor = NULL;
	} else {
		mc->mc_rotor = mgnext;
		mgprev->mg_next = mgnext;
		mgnext->mg_prev = mgprev;
	}

	mg->mg_prev = NULL;
	mg->mg_next = NULL;
}

uint64_t
metaslab_group_get_space(metaslab_group_t *mg)
{
	return ((1ULL << mg->mg_vd->vdev_ms_shift) * mg->mg_vd->vdev_ms_count);
}

void
metaslab_group_histogram_verify(metaslab_group_t *mg)
{
	uint64_t *mg_hist;
	vdev_t *vd = mg->mg_vd;
	uint64_t ashift = vd->vdev_ashift;
	int i, m;

	if ((zfs_flags & ZFS_DEBUG_HISTOGRAM_VERIFY) == 0)
		return;

	mg_hist = kmem_zalloc(sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE,
	    KM_SLEEP);

	ASSERT3U(RANGE_TREE_HISTOGRAM_SIZE, >=,
	    SPACE_MAP_HISTOGRAM_SIZE + ashift);

	for (m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *msp = vd->vdev_ms[m];

		if (msp->ms_sm == NULL)
			continue;

		for (i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++)
			mg_hist[i + ashift] +=
			    msp->ms_sm->sm_phys->smp_histogram[i];
	}

	for (i = 0; i < RANGE_TREE_HISTOGRAM_SIZE; i ++)
		VERIFY3U(mg_hist[i], ==, mg->mg_histogram[i]);

	kmem_free(mg_hist, sizeof (uint64_t) * RANGE_TREE_HISTOGRAM_SIZE);
}

static void
metaslab_group_histogram_add(metaslab_group_t *mg, metaslab_t *msp)
{
	metaslab_class_t *mc = mg->mg_class;
	uint64_t ashift = mg->mg_vd->vdev_ashift;
	int i;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	if (msp->ms_sm == NULL)
		return;

	mutex_enter(&mg->mg_lock);
	for (i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
		mg->mg_histogram[i + ashift] +=
		    msp->ms_sm->sm_phys->smp_histogram[i];
		mc->mc_histogram[i + ashift] +=
		    msp->ms_sm->sm_phys->smp_histogram[i];
	}
	mutex_exit(&mg->mg_lock);
}

void
metaslab_group_histogram_remove(metaslab_group_t *mg, metaslab_t *msp)
{
	metaslab_class_t *mc = mg->mg_class;
	uint64_t ashift = mg->mg_vd->vdev_ashift;
	int i;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	if (msp->ms_sm == NULL)
		return;

	mutex_enter(&mg->mg_lock);
	for (i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
		ASSERT3U(mg->mg_histogram[i + ashift], >=,
		    msp->ms_sm->sm_phys->smp_histogram[i]);
		ASSERT3U(mc->mc_histogram[i + ashift], >=,
		    msp->ms_sm->sm_phys->smp_histogram[i]);

		mg->mg_histogram[i + ashift] -=
		    msp->ms_sm->sm_phys->smp_histogram[i];
		mc->mc_histogram[i + ashift] -=
		    msp->ms_sm->sm_phys->smp_histogram[i];
	}
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_group_add(metaslab_group_t *mg, metaslab_t *msp)
{
	ASSERT(msp->ms_group == NULL);
	mutex_enter(&mg->mg_lock);
	msp->ms_group = mg;
	msp->ms_weight = 0;
	avl_add(&mg->mg_metaslab_tree, msp);
	mutex_exit(&mg->mg_lock);

	mutex_enter(&msp->ms_lock);
	metaslab_group_histogram_add(mg, msp);
	mutex_exit(&msp->ms_lock);
}

static void
metaslab_group_remove(metaslab_group_t *mg, metaslab_t *msp)
{
	mutex_enter(&msp->ms_lock);
	metaslab_group_histogram_remove(mg, msp);
	mutex_exit(&msp->ms_lock);

	mutex_enter(&mg->mg_lock);
	ASSERT(msp->ms_group == mg);
	avl_remove(&mg->mg_metaslab_tree, msp);
	msp->ms_group = NULL;
	mutex_exit(&mg->mg_lock);
}

static void
metaslab_group_sort(metaslab_group_t *mg, metaslab_t *msp, uint64_t weight)
{
	/*
	 * Although in principle the weight can be any value, in
	 * practice we do not use values in the range [1, 511].
	 */
	ASSERT(weight >= SPA_MINBLOCKSIZE || weight == 0);
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	mutex_enter(&mg->mg_lock);
	ASSERT(msp->ms_group == mg);
	avl_remove(&mg->mg_metaslab_tree, msp);
	msp->ms_weight = weight;
	avl_add(&mg->mg_metaslab_tree, msp);
	mutex_exit(&mg->mg_lock);
}

/*
 * Calculate the fragmentation for a given metaslab group. We can use
 * a simple average here since all metaslabs within the group must have
 * the same size. The return value will be a value between 0 and 100
 * (inclusive), or ZFS_FRAG_INVALID if less than half of the metaslab in this
 * group have a fragmentation metric.
 */
uint64_t
metaslab_group_fragmentation(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	uint64_t fragmentation = 0;
	uint64_t valid_ms = 0;
	int m;

	for (m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *msp = vd->vdev_ms[m];

		if (msp->ms_fragmentation == ZFS_FRAG_INVALID)
			continue;

		valid_ms++;
		fragmentation += msp->ms_fragmentation;
	}

	if (valid_ms <= vd->vdev_ms_count / 2)
		return (ZFS_FRAG_INVALID);

	fragmentation /= valid_ms;
	ASSERT3U(fragmentation, <=, 100);
	return (fragmentation);
}

/*
 * Determine if a given metaslab group should skip allocations. A metaslab
 * group should avoid allocations if its free capacity is less than the
 * zfs_mg_noalloc_threshold or its fragmentation metric is greater than
 * zfs_mg_fragmentation_threshold and there is at least one metaslab group
 * that can still handle allocations.
 */
static boolean_t
metaslab_group_allocatable(metaslab_group_t *mg)
{
	vdev_t *vd = mg->mg_vd;
	spa_t *spa = vd->vdev_spa;
	metaslab_class_t *mc = mg->mg_class;

	/*
	 * We use two key metrics to determine if a metaslab group is
	 * considered allocatable -- free space and fragmentation. If
	 * the free space is greater than the free space threshold and
	 * the fragmentation is less than the fragmentation threshold then
	 * consider the group allocatable. There are two case when we will
	 * not consider these key metrics. The first is if the group is
	 * associated with a slog device and the second is if all groups
	 * in this metaslab class have already been consider ineligible
	 * for allocations.
	 */
	return ((mg->mg_free_capacity > zfs_mg_noalloc_threshold &&
	    (mg->mg_fragmentation == ZFS_FRAG_INVALID ||
	    mg->mg_fragmentation <= zfs_mg_fragmentation_threshold)) ||
	    mc != spa_normal_class(spa) || mc->mc_alloc_groups == 0);
}

/*
 * ==========================================================================
 * Range tree callbacks
 * ==========================================================================
 */

/*
 * Comparison function for the private size-ordered tree. Tree is sorted
 * by size, larger sizes at the end of the tree.
 */
static int
metaslab_rangesize_compare(const void *x1, const void *x2)
{
	const range_seg_t *r1 = x1;
	const range_seg_t *r2 = x2;
	uint64_t rs_size1 = r1->rs_end - r1->rs_start;
	uint64_t rs_size2 = r2->rs_end - r2->rs_start;

	if (rs_size1 < rs_size2)
		return (-1);
	if (rs_size1 > rs_size2)
		return (1);

	if (r1->rs_start < r2->rs_start)
		return (-1);

	if (r1->rs_start > r2->rs_start)
		return (1);

	return (0);
}

/*
 * Create any block allocator specific components. The current allocators
 * rely on using both a size-ordered range_tree_t and an array of uint64_t's.
 */
static void
metaslab_rt_create(range_tree_t *rt, void *arg)
{
	metaslab_t *msp = arg;

	ASSERT3P(rt->rt_arg, ==, msp);
	ASSERT(msp->ms_tree == NULL);

	avl_create(&msp->ms_size_tree, metaslab_rangesize_compare,
	    sizeof (range_seg_t), offsetof(range_seg_t, rs_pp_node));
}

/*
 * Destroy the block allocator specific components.
 */
static void
metaslab_rt_destroy(range_tree_t *rt, void *arg)
{
	metaslab_t *msp = arg;

	ASSERT3P(rt->rt_arg, ==, msp);
	ASSERT3P(msp->ms_tree, ==, rt);
	ASSERT0(avl_numnodes(&msp->ms_size_tree));

	avl_destroy(&msp->ms_size_tree);
}

static void
metaslab_rt_add(range_tree_t *rt, range_seg_t *rs, void *arg)
{
	metaslab_t *msp = arg;

	ASSERT3P(rt->rt_arg, ==, msp);
	ASSERT3P(msp->ms_tree, ==, rt);
	VERIFY(!msp->ms_condensing);
	avl_add(&msp->ms_size_tree, rs);
}

static void
metaslab_rt_remove(range_tree_t *rt, range_seg_t *rs, void *arg)
{
	metaslab_t *msp = arg;

	ASSERT3P(rt->rt_arg, ==, msp);
	ASSERT3P(msp->ms_tree, ==, rt);
	VERIFY(!msp->ms_condensing);
	avl_remove(&msp->ms_size_tree, rs);
}

static void
metaslab_rt_vacate(range_tree_t *rt, void *arg)
{
	metaslab_t *msp = arg;

	ASSERT3P(rt->rt_arg, ==, msp);
	ASSERT3P(msp->ms_tree, ==, rt);

	/*
	 * Normally one would walk the tree freeing nodes along the way.
	 * Since the nodes are shared with the range trees we can avoid
	 * walking all nodes and just reinitialize the avl tree. The nodes
	 * will be freed by the range tree, so we don't want to free them here.
	 */
	avl_create(&msp->ms_size_tree, metaslab_rangesize_compare,
	    sizeof (range_seg_t), offsetof(range_seg_t, rs_pp_node));
}

static range_tree_ops_t metaslab_rt_ops = {
	metaslab_rt_create,
	metaslab_rt_destroy,
	metaslab_rt_add,
	metaslab_rt_remove,
	metaslab_rt_vacate
};

/*
 * ==========================================================================
 * Metaslab block operations
 * ==========================================================================
 */

/*
 * Return the maximum contiguous segment within the metaslab.
 */
uint64_t
metaslab_block_maxsize(metaslab_t *msp)
{
	avl_tree_t *t = &msp->ms_size_tree;
	range_seg_t *rs;

	if (t == NULL || (rs = avl_last(t)) == NULL)
		return (0ULL);

	return (rs->rs_end - rs->rs_start);
}

uint64_t
metaslab_block_alloc(metaslab_t *msp, uint64_t size)
{
	uint64_t start;
	range_tree_t *rt = msp->ms_tree;

	VERIFY(!msp->ms_condensing);

	start = msp->ms_ops->msop_alloc(msp, size);
	if (start != -1ULL) {
		vdev_t *vd = msp->ms_group->mg_vd;

		VERIFY0(P2PHASE(start, 1ULL << vd->vdev_ashift));
		VERIFY0(P2PHASE(size, 1ULL << vd->vdev_ashift));
		VERIFY3U(range_tree_space(rt) - size, <=, msp->ms_size);
		range_tree_remove(rt, start, size);
	}
	return (start);
}

/*
 * ==========================================================================
 * Common allocator routines
 * ==========================================================================
 */

#if defined(WITH_FF_BLOCK_ALLOCATOR) || \
    defined(WITH_DF_BLOCK_ALLOCATOR) || \
    defined(WITH_CF_BLOCK_ALLOCATOR)
/*
 * This is a helper function that can be used by the allocator to find
 * a suitable block to allocate. This will search the specified AVL
 * tree looking for a block that matches the specified criteria.
 */
static uint64_t
metaslab_block_picker(avl_tree_t *t, uint64_t *cursor, uint64_t size,
    uint64_t align)
{
	range_seg_t *rs, rsearch;
	avl_index_t where;

	rsearch.rs_start = *cursor;
	rsearch.rs_end = *cursor + size;

	rs = avl_find(t, &rsearch, &where);
	if (rs == NULL)
		rs = avl_nearest(t, where, AVL_AFTER);

	while (rs != NULL) {
		uint64_t offset = P2ROUNDUP(rs->rs_start, align);

		if (offset + size <= rs->rs_end) {
			*cursor = offset + size;
			return (offset);
		}
		rs = AVL_NEXT(t, rs);
	}

	/*
	 * If we know we've searched the whole map (*cursor == 0), give up.
	 * Otherwise, reset the cursor to the beginning and try again.
	 */
	if (*cursor == 0)
		return (-1ULL);

	*cursor = 0;
	return (metaslab_block_picker(t, cursor, size, align));
}
#endif /* WITH_FF/DF/CF_BLOCK_ALLOCATOR */

#if defined(WITH_FF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * The first-fit block allocator
 * ==========================================================================
 */
static uint64_t
metaslab_ff_alloc(metaslab_t *msp, uint64_t size)
{
	/*
	 * Find the largest power of 2 block size that evenly divides the
	 * requested size. This is used to try to allocate blocks with similar
	 * alignment from the same area of the metaslab (i.e. same cursor
	 * bucket) but it does not guarantee that other allocations sizes
	 * may exist in the same region.
	 */
	uint64_t align = size & -size;
	uint64_t *cursor = &msp->ms_lbas[highbit64(align) - 1];
	avl_tree_t *t = &msp->ms_tree->rt_root;

	return (metaslab_block_picker(t, cursor, size, align));
}

static metaslab_ops_t metaslab_ff_ops = {
	metaslab_ff_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_ff_ops;
#endif /* WITH_FF_BLOCK_ALLOCATOR */

#if defined(WITH_DF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * Dynamic block allocator -
 * Uses the first fit allocation scheme until space get low and then
 * adjusts to a best fit allocation method. Uses metaslab_df_alloc_threshold
 * and metaslab_df_free_pct to determine when to switch the allocation scheme.
 * ==========================================================================
 */
static uint64_t
metaslab_df_alloc(metaslab_t *msp, uint64_t size)
{
	/*
	 * Find the largest power of 2 block size that evenly divides the
	 * requested size. This is used to try to allocate blocks with similar
	 * alignment from the same area of the metaslab (i.e. same cursor
	 * bucket) but it does not guarantee that other allocations sizes
	 * may exist in the same region.
	 */
	uint64_t align = size & -size;
	uint64_t *cursor = &msp->ms_lbas[highbit64(align) - 1];
	range_tree_t *rt = msp->ms_tree;
	avl_tree_t *t = &rt->rt_root;
	uint64_t max_size = metaslab_block_maxsize(msp);
	int free_pct = range_tree_space(rt) * 100 / msp->ms_size;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(avl_numnodes(t), ==, avl_numnodes(&msp->ms_size_tree));

	if (max_size < size)
		return (-1ULL);

	/*
	 * If we're running low on space switch to using the size
	 * sorted AVL tree (best-fit).
	 */
	if (max_size < metaslab_df_alloc_threshold ||
	    free_pct < metaslab_df_free_pct) {
		t = &msp->ms_size_tree;
		*cursor = 0;
	}

	return (metaslab_block_picker(t, cursor, size, 1ULL));
}

static metaslab_ops_t metaslab_df_ops = {
	metaslab_df_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_df_ops;
#endif /* WITH_DF_BLOCK_ALLOCATOR */

#if defined(WITH_CF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * Cursor fit block allocator -
 * Select the largest region in the metaslab, set the cursor to the beginning
 * of the range and the cursor_end to the end of the range. As allocations
 * are made advance the cursor. Continue allocating from the cursor until
 * the range is exhausted and then find a new range.
 * ==========================================================================
 */
static uint64_t
metaslab_cf_alloc(metaslab_t *msp, uint64_t size)
{
	range_tree_t *rt = msp->ms_tree;
	avl_tree_t *t = &msp->ms_size_tree;
	uint64_t *cursor = &msp->ms_lbas[0];
	uint64_t *cursor_end = &msp->ms_lbas[1];
	uint64_t offset = 0;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(avl_numnodes(t), ==, avl_numnodes(&rt->rt_root));

	ASSERT3U(*cursor_end, >=, *cursor);

	if ((*cursor + size) > *cursor_end) {
		range_seg_t *rs;

		rs = avl_last(&msp->ms_size_tree);
		if (rs == NULL || (rs->rs_end - rs->rs_start) < size)
			return (-1ULL);

		*cursor = rs->rs_start;
		*cursor_end = rs->rs_end;
	}

	offset = *cursor;
	*cursor += size;

	return (offset);
}

static metaslab_ops_t metaslab_cf_ops = {
	metaslab_cf_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_cf_ops;
#endif /* WITH_CF_BLOCK_ALLOCATOR */

#if defined(WITH_NDF_BLOCK_ALLOCATOR)
/*
 * ==========================================================================
 * New dynamic fit allocator -
 * Select a region that is large enough to allocate 2^metaslab_ndf_clump_shift
 * contiguous blocks. If no region is found then just use the largest segment
 * that remains.
 * ==========================================================================
 */

/*
 * Determines desired number of contiguous blocks (2^metaslab_ndf_clump_shift)
 * to request from the allocator.
 */
uint64_t metaslab_ndf_clump_shift = 4;

static uint64_t
metaslab_ndf_alloc(metaslab_t *msp, uint64_t size)
{
	avl_tree_t *t = &msp->ms_tree->rt_root;
	avl_index_t where;
	range_seg_t *rs, rsearch;
	uint64_t hbit = highbit64(size);
	uint64_t *cursor = &msp->ms_lbas[hbit - 1];
	uint64_t max_size = metaslab_block_maxsize(msp);

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(avl_numnodes(t), ==, avl_numnodes(&msp->ms_size_tree));

	if (max_size < size)
		return (-1ULL);

	rsearch.rs_start = *cursor;
	rsearch.rs_end = *cursor + size;

	rs = avl_find(t, &rsearch, &where);
	if (rs == NULL || (rs->rs_end - rs->rs_start) < size) {
		t = &msp->ms_size_tree;

		rsearch.rs_start = 0;
		rsearch.rs_end = MIN(max_size,
		    1ULL << (hbit + metaslab_ndf_clump_shift));
		rs = avl_find(t, &rsearch, &where);
		if (rs == NULL)
			rs = avl_nearest(t, where, AVL_AFTER);
		ASSERT(rs != NULL);
	}

	if ((rs->rs_end - rs->rs_start) >= size) {
		*cursor = rs->rs_start + size;
		return (rs->rs_start);
	}
	return (-1ULL);
}

static metaslab_ops_t metaslab_ndf_ops = {
	metaslab_ndf_alloc
};

metaslab_ops_t *zfs_metaslab_ops = &metaslab_ndf_ops;
#endif /* WITH_NDF_BLOCK_ALLOCATOR */


/*
 * ==========================================================================
 * Metaslabs
 * ==========================================================================
 */

/*
 * Wait for any in-progress metaslab loads to complete.
 */
void
metaslab_load_wait(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	while (msp->ms_loading) {
		ASSERT(!msp->ms_loaded);
		cv_wait(&msp->ms_load_cv, &msp->ms_lock);
	}
}

int
metaslab_load(metaslab_t *msp)
{
	int error = 0;
	int t;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(!msp->ms_loaded);
	ASSERT(!msp->ms_loading);

	msp->ms_loading = B_TRUE;

	/*
	 * If the space map has not been allocated yet, then treat
	 * all the space in the metaslab as free and add it to the
	 * ms_tree.
	 */
	if (msp->ms_sm != NULL)
		error = space_map_load(msp->ms_sm, msp->ms_tree, SM_FREE);
	else
		range_tree_add(msp->ms_tree, msp->ms_start, msp->ms_size);

	msp->ms_loaded = (error == 0);
	msp->ms_loading = B_FALSE;

	if (msp->ms_loaded) {
		for (t = 0; t < TXG_DEFER_SIZE; t++) {
			range_tree_walk(msp->ms_defertree[t],
			    range_tree_remove, msp->ms_tree);
		}
	}
	cv_broadcast(&msp->ms_load_cv);
	return (error);
}

void
metaslab_unload(metaslab_t *msp)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));
	range_tree_vacate(msp->ms_tree, NULL, NULL);
	msp->ms_loaded = B_FALSE;
	msp->ms_weight &= ~METASLAB_ACTIVE_MASK;
}

int
metaslab_init(metaslab_group_t *mg, uint64_t id, uint64_t object, uint64_t txg,
    metaslab_t **msp)
{
	vdev_t *vd = mg->mg_vd;
	objset_t *mos = vd->vdev_spa->spa_meta_objset;
	metaslab_t *ms;
	int error;

	ms = kmem_zalloc(sizeof (metaslab_t), KM_SLEEP);
	mutex_init(&ms->ms_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ms->ms_load_cv, NULL, CV_DEFAULT, NULL);
	ms->ms_id = id;
	ms->ms_start = id << vd->vdev_ms_shift;
	ms->ms_size = 1ULL << vd->vdev_ms_shift;

	/*
	 * We only open space map objects that already exist. All others
	 * will be opened when we finally allocate an object for it.
	 */
	if (object != 0) {
		error = space_map_open(&ms->ms_sm, mos, object, ms->ms_start,
		    ms->ms_size, vd->vdev_ashift, &ms->ms_lock);

		if (error != 0) {
			kmem_free(ms, sizeof (metaslab_t));
			return (error);
		}

		ASSERT(ms->ms_sm != NULL);
	}

	/*
	 * We create the main range tree here, but we don't create the
	 * alloctree and freetree until metaslab_sync_done().  This serves
	 * two purposes: it allows metaslab_sync_done() to detect the
	 * addition of new space; and for debugging, it ensures that we'd
	 * data fault on any attempt to use this metaslab before it's ready.
	 */
	ms->ms_tree = range_tree_create(&metaslab_rt_ops, ms, &ms->ms_lock);
	metaslab_group_add(mg, ms);

	ms->ms_fragmentation = metaslab_fragmentation(ms);
	ms->ms_ops = mg->mg_class->mc_ops;

	/*
	 * If we're opening an existing pool (txg == 0) or creating
	 * a new one (txg == TXG_INITIAL), all space is available now.
	 * If we're adding space to an existing pool, the new space
	 * does not become available until after this txg has synced.
	 */
	if (txg <= TXG_INITIAL)
		metaslab_sync_done(ms, 0);

	/*
	 * If metaslab_debug_load is set and we're initializing a metaslab
	 * that has an allocated space_map object then load the its space
	 * map so that can verify frees.
	 */
	if (metaslab_debug_load && ms->ms_sm != NULL) {
		mutex_enter(&ms->ms_lock);
		VERIFY0(metaslab_load(ms));
		mutex_exit(&ms->ms_lock);
	}

	if (txg != 0) {
		vdev_dirty(vd, 0, NULL, txg);
		vdev_dirty(vd, VDD_METASLAB, ms, txg);
	}

	*msp = ms;

	return (0);
}

void
metaslab_fini(metaslab_t *msp)
{
	int t;

	metaslab_group_t *mg = msp->ms_group;

	metaslab_group_remove(mg, msp);

	mutex_enter(&msp->ms_lock);

	VERIFY(msp->ms_group == NULL);
	vdev_space_update(mg->mg_vd, -space_map_allocated(msp->ms_sm),
	    0, -msp->ms_size);
	space_map_close(msp->ms_sm);

	metaslab_unload(msp);
	range_tree_destroy(msp->ms_tree);

	for (t = 0; t < TXG_SIZE; t++) {
		range_tree_destroy(msp->ms_alloctree[t]);
		range_tree_destroy(msp->ms_freetree[t]);
	}

	for (t = 0; t < TXG_DEFER_SIZE; t++) {
		range_tree_destroy(msp->ms_defertree[t]);
	}

	ASSERT0(msp->ms_deferspace);

	mutex_exit(&msp->ms_lock);
	cv_destroy(&msp->ms_load_cv);
	mutex_destroy(&msp->ms_lock);

	kmem_free(msp, sizeof (metaslab_t));
}

#define	FRAGMENTATION_TABLE_SIZE	17

/*
 * This table defines a segment size based fragmentation metric that will
 * allow each metaslab to derive its own fragmentation value. This is done
 * by calculating the space in each bucket of the spacemap histogram and
 * multiplying that by the fragmetation metric in this table. Doing
 * this for all buckets and dividing it by the total amount of free
 * space in this metaslab (i.e. the total free space in all buckets) gives
 * us the fragmentation metric. This means that a high fragmentation metric
 * equates to most of the free space being comprised of small segments.
 * Conversely, if the metric is low, then most of the free space is in
 * large segments. A 10% change in fragmentation equates to approximately
 * double the number of segments.
 *
 * This table defines 0% fragmented space using 16MB segments. Testing has
 * shown that segments that are greater than or equal to 16MB do not suffer
 * from drastic performance problems. Using this value, we derive the rest
 * of the table. Since the fragmentation value is never stored on disk, it
 * is possible to change these calculations in the future.
 */
int zfs_frag_table[FRAGMENTATION_TABLE_SIZE] = {
	100,	/* 512B	*/
	100,	/* 1K	*/
	98,	/* 2K	*/
	95,	/* 4K	*/
	90,	/* 8K	*/
	80,	/* 16K	*/
	70,	/* 32K	*/
	60,	/* 64K	*/
	50,	/* 128K	*/
	40,	/* 256K	*/
	30,	/* 512K	*/
	20,	/* 1M	*/
	15,	/* 2M	*/
	10,	/* 4M	*/
	5,	/* 8M	*/
	0	/* 16M	*/
};

/*
 * Calclate the metaslab's fragmentation metric. A return value
 * of ZFS_FRAG_INVALID means that the metaslab has not been upgraded and does
 * not support this metric. Otherwise, the return value should be in the
 * range [0, 100].
 */
static uint64_t
metaslab_fragmentation(metaslab_t *msp)
{
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	uint64_t fragmentation = 0;
	uint64_t total = 0;
	boolean_t feature_enabled = spa_feature_is_enabled(spa,
	    SPA_FEATURE_SPACEMAP_HISTOGRAM);
	int i;

	if (!feature_enabled)
		return (ZFS_FRAG_INVALID);

	/*
	 * A null space map means that the entire metaslab is free
	 * and thus is not fragmented.
	 */
	if (msp->ms_sm == NULL)
		return (0);

	/*
	 * If this metaslab's space_map has not been upgraded, flag it
	 * so that we upgrade next time we encounter it.
	 */
	if (msp->ms_sm->sm_dbuf->db_size != sizeof (space_map_phys_t)) {
		vdev_t *vd = msp->ms_group->mg_vd;

		if (spa_writeable(vd->vdev_spa)) {
			uint64_t txg = spa_syncing_txg(spa);

			msp->ms_condense_wanted = B_TRUE;
			vdev_dirty(vd, VDD_METASLAB, msp, txg + 1);
			spa_dbgmsg(spa, "txg %llu, requesting force condense: "
			    "msp %p, vd %p", txg, msp, vd);
		}
		return (ZFS_FRAG_INVALID);
	}

	for (i = 0; i < SPACE_MAP_HISTOGRAM_SIZE; i++) {
		uint64_t space = 0;
		uint8_t shift = msp->ms_sm->sm_shift;
		int idx = MIN(shift - SPA_MINBLOCKSHIFT + i,
		    FRAGMENTATION_TABLE_SIZE - 1);

		if (msp->ms_sm->sm_phys->smp_histogram[i] == 0)
			continue;

		space = msp->ms_sm->sm_phys->smp_histogram[i] << (i + shift);
		total += space;

		ASSERT3U(idx, <, FRAGMENTATION_TABLE_SIZE);
		fragmentation += space * zfs_frag_table[idx];
	}

	if (total > 0)
		fragmentation /= total;
	ASSERT3U(fragmentation, <=, 100);
	return (fragmentation);
}

/*
 * Compute a weight -- a selection preference value -- for the given metaslab.
 * This is based on the amount of free space, the level of fragmentation,
 * the LBA range, and whether the metaslab is loaded.
 */
static uint64_t
metaslab_weight(metaslab_t *msp)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	uint64_t weight, space;

	ASSERT(MUTEX_HELD(&msp->ms_lock));

	/*
	 * This vdev is in the process of being removed so there is nothing
	 * for us to do here.
	 */
	if (vd->vdev_removing) {
		ASSERT0(space_map_allocated(msp->ms_sm));
		ASSERT0(vd->vdev_ms_shift);
		return (0);
	}

	/*
	 * The baseline weight is the metaslab's free space.
	 */
	space = msp->ms_size - space_map_allocated(msp->ms_sm);

	msp->ms_fragmentation = metaslab_fragmentation(msp);
	if (metaslab_fragmentation_factor_enabled &&
	    msp->ms_fragmentation != ZFS_FRAG_INVALID) {
		/*
		 * Use the fragmentation information to inversely scale
		 * down the baseline weight. We need to ensure that we
		 * don't exclude this metaslab completely when it's 100%
		 * fragmented. To avoid this we reduce the fragmented value
		 * by 1.
		 */
		space = (space * (100 - (msp->ms_fragmentation - 1))) / 100;

		/*
		 * If space < SPA_MINBLOCKSIZE, then we will not allocate from
		 * this metaslab again. The fragmentation metric may have
		 * decreased the space to something smaller than
		 * SPA_MINBLOCKSIZE, so reset the space to SPA_MINBLOCKSIZE
		 * so that we can consume any remaining space.
		 */
		if (space > 0 && space < SPA_MINBLOCKSIZE)
			space = SPA_MINBLOCKSIZE;
	}
	weight = space;

	/*
	 * Modern disks have uniform bit density and constant angular velocity.
	 * Therefore, the outer recording zones are faster (higher bandwidth)
	 * than the inner zones by the ratio of outer to inner track diameter,
	 * which is typically around 2:1.  We account for this by assigning
	 * higher weight to lower metaslabs (multiplier ranging from 2x to 1x).
	 * In effect, this means that we'll select the metaslab with the most
	 * free bandwidth rather than simply the one with the most free space.
	 */
	if (!vd->vdev_nonrot && metaslab_lba_weighting_enabled) {
		weight = 2 * weight - (msp->ms_id * weight) / vd->vdev_ms_count;
		ASSERT(weight >= space && weight <= 2 * space);
	}

	/*
	 * If this metaslab is one we're actively using, adjust its
	 * weight to make it preferable to any inactive metaslab so
	 * we'll polish it off. If the fragmentation on this metaslab
	 * has exceed our threshold, then don't mark it active.
	 */
	if (msp->ms_loaded && msp->ms_fragmentation != ZFS_FRAG_INVALID &&
	    msp->ms_fragmentation <= zfs_metaslab_fragmentation_threshold) {
		weight |= (msp->ms_weight & METASLAB_ACTIVE_MASK);
	}

	return (weight);
}

static int
metaslab_activate(metaslab_t *msp, uint64_t activation_weight)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));

	if ((msp->ms_weight & METASLAB_ACTIVE_MASK) == 0) {
		metaslab_load_wait(msp);
		if (!msp->ms_loaded) {
			int error = metaslab_load(msp);
			if (error) {
				metaslab_group_sort(msp->ms_group, msp, 0);
				return (error);
			}
		}

		metaslab_group_sort(msp->ms_group, msp,
		    msp->ms_weight | activation_weight);
	}
	ASSERT(msp->ms_loaded);
	ASSERT(msp->ms_weight & METASLAB_ACTIVE_MASK);

	return (0);
}

static void
metaslab_passivate(metaslab_t *msp, uint64_t size)
{
	/*
	 * If size < SPA_MINBLOCKSIZE, then we will not allocate from
	 * this metaslab again.  In that case, it had better be empty,
	 * or we would be leaving space on the table.
	 */
	ASSERT(size >= SPA_MINBLOCKSIZE || range_tree_space(msp->ms_tree) == 0);
	metaslab_group_sort(msp->ms_group, msp, MIN(msp->ms_weight, size));
	ASSERT((msp->ms_weight & METASLAB_ACTIVE_MASK) == 0);
}

static void
metaslab_preload(void *arg)
{
	metaslab_t *msp = arg;
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	fstrans_cookie_t cookie = spl_fstrans_mark();

	ASSERT(!MUTEX_HELD(&msp->ms_group->mg_lock));

	mutex_enter(&msp->ms_lock);
	metaslab_load_wait(msp);
	if (!msp->ms_loaded)
		(void) metaslab_load(msp);

	/*
	 * Set the ms_access_txg value so that we don't unload it right away.
	 */
	msp->ms_access_txg = spa_syncing_txg(spa) + metaslab_unload_delay + 1;
	mutex_exit(&msp->ms_lock);
	spl_fstrans_unmark(cookie);
}

static void
metaslab_group_preload(metaslab_group_t *mg)
{
	spa_t *spa = mg->mg_vd->vdev_spa;
	metaslab_t *msp;
	avl_tree_t *t = &mg->mg_metaslab_tree;
	int m = 0;

	if (spa_shutting_down(spa) || !metaslab_preload_enabled) {
		taskq_wait_outstanding(mg->mg_taskq, 0);
		return;
	}

	mutex_enter(&mg->mg_lock);
	/*
	 * Load the next potential metaslabs
	 */
	msp = avl_first(t);
	while (msp != NULL) {
		metaslab_t *msp_next = AVL_NEXT(t, msp);

		/*
		 * We preload only the maximum number of metaslabs specified
		 * by metaslab_preload_limit. If a metaslab is being forced
		 * to condense then we preload it too. This will ensure
		 * that force condensing happens in the next txg.
		 */
		if (++m > metaslab_preload_limit && !msp->ms_condense_wanted) {
			msp = msp_next;
			continue;
		}

		/*
		 * We must drop the metaslab group lock here to preserve
		 * lock ordering with the ms_lock (when grabbing both
		 * the mg_lock and the ms_lock, the ms_lock must be taken
		 * first).  As a result, it is possible that the ordering
		 * of the metaslabs within the avl tree may change before
		 * we reacquire the lock. The metaslab cannot be removed from
		 * the tree while we're in syncing context so it is safe to
		 * drop the mg_lock here. If the metaslabs are reordered
		 * nothing will break -- we just may end up loading a
		 * less than optimal one.
		 */
		mutex_exit(&mg->mg_lock);
		VERIFY(taskq_dispatch(mg->mg_taskq, metaslab_preload,
		    msp, TQ_SLEEP) != 0);
		mutex_enter(&mg->mg_lock);
		msp = msp_next;
	}
	mutex_exit(&mg->mg_lock);
}

/*
 * Determine if the space map's on-disk footprint is past our tolerance
 * for inefficiency. We would like to use the following criteria to make
 * our decision:
 *
 * 1. The size of the space map object should not dramatically increase as a
 * result of writing out the free space range tree.
 *
 * 2. The minimal on-disk space map representation is zfs_condense_pct/100
 * times the size than the free space range tree representation
 * (i.e. zfs_condense_pct = 110 and in-core = 1MB, minimal = 1.1.MB).
 *
 * 3. The on-disk size of the space map should actually decrease.
 *
 * Checking the first condition is tricky since we don't want to walk
 * the entire AVL tree calculating the estimated on-disk size. Instead we
 * use the size-ordered range tree in the metaslab and calculate the
 * size required to write out the largest segment in our free tree. If the
 * size required to represent that segment on disk is larger than the space
 * map object then we avoid condensing this map.
 *
 * To determine the second criterion we use a best-case estimate and assume
 * each segment can be represented on-disk as a single 64-bit entry. We refer
 * to this best-case estimate as the space map's minimal form.
 *
 * Unfortunately, we cannot compute the on-disk size of the space map in this
 * context because we cannot accurately compute the effects of compression, etc.
 * Instead, we apply the heuristic described in the block comment for
 * zfs_metaslab_condense_block_threshold - we only condense if the space used
 * is greater than a threshold number of blocks.
 */
static boolean_t
metaslab_should_condense(metaslab_t *msp)
{
	space_map_t *sm = msp->ms_sm;
	range_seg_t *rs;
	uint64_t size, entries, segsz, object_size, optimal_size, record_size;
	dmu_object_info_t doi;
	uint64_t vdev_blocksize = 1 << msp->ms_group->mg_vd->vdev_ashift;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT(msp->ms_loaded);

	/*
	 * Use the ms_size_tree range tree, which is ordered by size, to
	 * obtain the largest segment in the free tree. We always condense
	 * metaslabs that are empty and metaslabs for which a condense
	 * request has been made.
	 */
	rs = avl_last(&msp->ms_size_tree);
	if (rs == NULL || msp->ms_condense_wanted)
		return (B_TRUE);

	/*
	 * Calculate the number of 64-bit entries this segment would
	 * require when written to disk. If this single segment would be
	 * larger on-disk than the entire current on-disk structure, then
	 * clearly condensing will increase the on-disk structure size.
	 */
	size = (rs->rs_end - rs->rs_start) >> sm->sm_shift;
	entries = size / (MIN(size, SM_RUN_MAX));
	segsz = entries * sizeof (uint64_t);

	optimal_size = sizeof (uint64_t) * avl_numnodes(&msp->ms_tree->rt_root);
	object_size = space_map_length(msp->ms_sm);

	dmu_object_info_from_db(sm->sm_dbuf, &doi);
	record_size = MAX(doi.doi_data_block_size, vdev_blocksize);

	return (segsz <= object_size &&
	    object_size >= (optimal_size * zfs_condense_pct / 100) &&
	    object_size > zfs_metaslab_condense_block_threshold * record_size);
}

/*
 * Condense the on-disk space map representation to its minimized form.
 * The minimized form consists of a small number of allocations followed by
 * the entries of the free range tree.
 */
static void
metaslab_condense(metaslab_t *msp, uint64_t txg, dmu_tx_t *tx)
{
	spa_t *spa = msp->ms_group->mg_vd->vdev_spa;
	range_tree_t *freetree = msp->ms_freetree[txg & TXG_MASK];
	range_tree_t *condense_tree;
	space_map_t *sm = msp->ms_sm;
	int t;

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	ASSERT3U(spa_sync_pass(spa), ==, 1);
	ASSERT(msp->ms_loaded);


	spa_dbgmsg(spa, "condensing: txg %llu, msp[%llu] %p, "
	    "smp size %llu, segments %lu, forcing condense=%s", txg,
	    msp->ms_id, msp, space_map_length(msp->ms_sm),
	    avl_numnodes(&msp->ms_tree->rt_root),
	    msp->ms_condense_wanted ? "TRUE" : "FALSE");

	msp->ms_condense_wanted = B_FALSE;

	/*
	 * Create an range tree that is 100% allocated. We remove segments
	 * that have been freed in this txg, any deferred frees that exist,
	 * and any allocation in the future. Removing segments should be
	 * a relatively inexpensive operation since we expect these trees to
	 * have a small number of nodes.
	 */
	condense_tree = range_tree_create(NULL, NULL, &msp->ms_lock);
	range_tree_add(condense_tree, msp->ms_start, msp->ms_size);

	/*
	 * Remove what's been freed in this txg from the condense_tree.
	 * Since we're in sync_pass 1, we know that all the frees from
	 * this txg are in the freetree.
	 */
	range_tree_walk(freetree, range_tree_remove, condense_tree);

	for (t = 0; t < TXG_DEFER_SIZE; t++) {
		range_tree_walk(msp->ms_defertree[t],
		    range_tree_remove, condense_tree);
	}

	for (t = 1; t < TXG_CONCURRENT_STATES; t++) {
		range_tree_walk(msp->ms_alloctree[(txg + t) & TXG_MASK],
		    range_tree_remove, condense_tree);
	}

	/*
	 * We're about to drop the metaslab's lock thus allowing
	 * other consumers to change it's content. Set the
	 * metaslab's ms_condensing flag to ensure that
	 * allocations on this metaslab do not occur while we're
	 * in the middle of committing it to disk. This is only critical
	 * for the ms_tree as all other range trees use per txg
	 * views of their content.
	 */
	msp->ms_condensing = B_TRUE;

	mutex_exit(&msp->ms_lock);
	space_map_truncate(sm, tx);
	mutex_enter(&msp->ms_lock);

	/*
	 * While we would ideally like to create a space_map representation
	 * that consists only of allocation records, doing so can be
	 * prohibitively expensive because the in-core free tree can be
	 * large, and therefore computationally expensive to subtract
	 * from the condense_tree. Instead we sync out two trees, a cheap
	 * allocation only tree followed by the in-core free tree. While not
	 * optimal, this is typically close to optimal, and much cheaper to
	 * compute.
	 */
	space_map_write(sm, condense_tree, SM_ALLOC, tx);
	range_tree_vacate(condense_tree, NULL, NULL);
	range_tree_destroy(condense_tree);

	space_map_write(sm, msp->ms_tree, SM_FREE, tx);
	msp->ms_condensing = B_FALSE;
}

/*
 * Write a metaslab to disk in the context of the specified transaction group.
 */
void
metaslab_sync(metaslab_t *msp, uint64_t txg)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa_meta_objset(spa);
	range_tree_t *alloctree = msp->ms_alloctree[txg & TXG_MASK];
	range_tree_t **freetree = &msp->ms_freetree[txg & TXG_MASK];
	range_tree_t **freed_tree =
	    &msp->ms_freetree[TXG_CLEAN(txg) & TXG_MASK];
	dmu_tx_t *tx;
	uint64_t object = space_map_object(msp->ms_sm);

	ASSERT(!vd->vdev_ishole);

	/*
	 * This metaslab has just been added so there's no work to do now.
	 */
	if (*freetree == NULL) {
		ASSERT3P(alloctree, ==, NULL);
		return;
	}

	ASSERT3P(alloctree, !=, NULL);
	ASSERT3P(*freetree, !=, NULL);
	ASSERT3P(*freed_tree, !=, NULL);

	/*
	 * Normally, we don't want to process a metaslab if there
	 * are no allocations or frees to perform. However, if the metaslab
	 * is being forced to condense we need to let it through.
	 */
	if (range_tree_space(alloctree) == 0 &&
	    range_tree_space(*freetree) == 0 &&
	    !msp->ms_condense_wanted)
		return;

	/*
	 * The only state that can actually be changing concurrently with
	 * metaslab_sync() is the metaslab's ms_tree.  No other thread can
	 * be modifying this txg's alloctree, freetree, freed_tree, or
	 * space_map_phys_t. Therefore, we only hold ms_lock to satify
	 * space_map ASSERTs. We drop it whenever we call into the DMU,
	 * because the DMU can call down to us (e.g. via zio_free()) at
	 * any time.
	 */

	tx = dmu_tx_create_assigned(spa_get_dsl(spa), txg);

	if (msp->ms_sm == NULL) {
		uint64_t new_object;

		new_object = space_map_alloc(mos, tx);
		VERIFY3U(new_object, !=, 0);

		VERIFY0(space_map_open(&msp->ms_sm, mos, new_object,
		    msp->ms_start, msp->ms_size, vd->vdev_ashift,
		    &msp->ms_lock));
		ASSERT(msp->ms_sm != NULL);
	}

	mutex_enter(&msp->ms_lock);

	/*
	 * Note: metaslab_condense() clears the space_map's histogram.
	 * Therefore we muse verify and remove this histogram before
	 * condensing.
	 */
	metaslab_group_histogram_verify(mg);
	metaslab_class_histogram_verify(mg->mg_class);
	metaslab_group_histogram_remove(mg, msp);

	if (msp->ms_loaded && spa_sync_pass(spa) == 1 &&
	    metaslab_should_condense(msp)) {
		metaslab_condense(msp, txg, tx);
	} else {
		space_map_write(msp->ms_sm, alloctree, SM_ALLOC, tx);
		space_map_write(msp->ms_sm, *freetree, SM_FREE, tx);
	}

	if (msp->ms_loaded) {
		/*
		 * When the space map is loaded, we have an accruate
		 * histogram in the range tree. This gives us an opportunity
		 * to bring the space map's histogram up-to-date so we clear
		 * it first before updating it.
		 */
		space_map_histogram_clear(msp->ms_sm);
		space_map_histogram_add(msp->ms_sm, msp->ms_tree, tx);
	} else {
		/*
		 * Since the space map is not loaded we simply update the
		 * exisiting histogram with what was freed in this txg. This
		 * means that the on-disk histogram may not have an accurate
		 * view of the free space but it's close enough to allow
		 * us to make allocation decisions.
		 */
		space_map_histogram_add(msp->ms_sm, *freetree, tx);
	}
	metaslab_group_histogram_add(mg, msp);
	metaslab_group_histogram_verify(mg);
	metaslab_class_histogram_verify(mg->mg_class);

	/*
	 * For sync pass 1, we avoid traversing this txg's free range tree
	 * and instead will just swap the pointers for freetree and
	 * freed_tree. We can safely do this since the freed_tree is
	 * guaranteed to be empty on the initial pass.
	 */
	if (spa_sync_pass(spa) == 1) {
		range_tree_swap(freetree, freed_tree);
	} else {
		range_tree_vacate(*freetree, range_tree_add, *freed_tree);
	}
	range_tree_vacate(alloctree, NULL, NULL);

	ASSERT0(range_tree_space(msp->ms_alloctree[txg & TXG_MASK]));
	ASSERT0(range_tree_space(msp->ms_freetree[txg & TXG_MASK]));

	mutex_exit(&msp->ms_lock);

	if (object != space_map_object(msp->ms_sm)) {
		object = space_map_object(msp->ms_sm);
		dmu_write(mos, vd->vdev_ms_array, sizeof (uint64_t) *
		    msp->ms_id, sizeof (uint64_t), &object, tx);
	}
	dmu_tx_commit(tx);
}

/*
 * Called after a transaction group has completely synced to mark
 * all of the metaslab's free space as usable.
 */
void
metaslab_sync_done(metaslab_t *msp, uint64_t txg)
{
	metaslab_group_t *mg = msp->ms_group;
	vdev_t *vd = mg->mg_vd;
	range_tree_t **freed_tree;
	range_tree_t **defer_tree;
	int64_t alloc_delta, defer_delta;
	int t;

	ASSERT(!vd->vdev_ishole);

	mutex_enter(&msp->ms_lock);

	/*
	 * If this metaslab is just becoming available, initialize its
	 * alloctrees, freetrees, and defertree and add its capacity to
	 * the vdev.
	 */
	if (msp->ms_freetree[TXG_CLEAN(txg) & TXG_MASK] == NULL) {
		for (t = 0; t < TXG_SIZE; t++) {
			ASSERT(msp->ms_alloctree[t] == NULL);
			ASSERT(msp->ms_freetree[t] == NULL);

			msp->ms_alloctree[t] = range_tree_create(NULL, msp,
			    &msp->ms_lock);
			msp->ms_freetree[t] = range_tree_create(NULL, msp,
			    &msp->ms_lock);
		}

		for (t = 0; t < TXG_DEFER_SIZE; t++) {
			ASSERT(msp->ms_defertree[t] == NULL);

			msp->ms_defertree[t] = range_tree_create(NULL, msp,
			    &msp->ms_lock);
		}

		vdev_space_update(vd, 0, 0, msp->ms_size);
	}

	freed_tree = &msp->ms_freetree[TXG_CLEAN(txg) & TXG_MASK];
	defer_tree = &msp->ms_defertree[txg % TXG_DEFER_SIZE];

	alloc_delta = space_map_alloc_delta(msp->ms_sm);
	defer_delta = range_tree_space(*freed_tree) -
	    range_tree_space(*defer_tree);

	vdev_space_update(vd, alloc_delta + defer_delta, defer_delta, 0);

	ASSERT0(range_tree_space(msp->ms_alloctree[txg & TXG_MASK]));
	ASSERT0(range_tree_space(msp->ms_freetree[txg & TXG_MASK]));

	/*
	 * If there's a metaslab_load() in progress, wait for it to complete
	 * so that we have a consistent view of the in-core space map.
	 */
	metaslab_load_wait(msp);

	/*
	 * Move the frees from the defer_tree back to the free
	 * range tree (if it's loaded). Swap the freed_tree and the
	 * defer_tree -- this is safe to do because we've just emptied out
	 * the defer_tree.
	 */
	range_tree_vacate(*defer_tree,
	    msp->ms_loaded ? range_tree_add : NULL, msp->ms_tree);
	range_tree_swap(freed_tree, defer_tree);

	space_map_update(msp->ms_sm);

	msp->ms_deferspace += defer_delta;
	ASSERT3S(msp->ms_deferspace, >=, 0);
	ASSERT3S(msp->ms_deferspace, <=, msp->ms_size);
	if (msp->ms_deferspace != 0) {
		/*
		 * Keep syncing this metaslab until all deferred frees
		 * are back in circulation.
		 */
		vdev_dirty(vd, VDD_METASLAB, msp, txg + 1);
	}

	if (msp->ms_loaded && msp->ms_access_txg < txg) {
		for (t = 1; t < TXG_CONCURRENT_STATES; t++) {
			VERIFY0(range_tree_space(
			    msp->ms_alloctree[(txg + t) & TXG_MASK]));
		}

		if (!metaslab_debug_unload)
			metaslab_unload(msp);
	}

	metaslab_group_sort(mg, msp, metaslab_weight(msp));
	mutex_exit(&msp->ms_lock);
}

void
metaslab_sync_reassess(metaslab_group_t *mg)
{
	metaslab_group_alloc_update(mg);
	mg->mg_fragmentation = metaslab_group_fragmentation(mg);

	/*
	 * Preload the next potential metaslabs
	 */
	metaslab_group_preload(mg);
}

static uint64_t
metaslab_distance(metaslab_t *msp, dva_t *dva)
{
	uint64_t ms_shift = msp->ms_group->mg_vd->vdev_ms_shift;
	uint64_t offset = DVA_GET_OFFSET(dva) >> ms_shift;
	uint64_t start = msp->ms_id;

	if (msp->ms_group->mg_vd->vdev_id != DVA_GET_VDEV(dva))
		return (1ULL << 63);

	if (offset < start)
		return ((start - offset) << ms_shift);
	if (offset > start)
		return ((offset - start) << ms_shift);
	return (0);
}

static uint64_t
metaslab_group_alloc(metaslab_group_t *mg, uint64_t psize, uint64_t asize,
    uint64_t txg, uint64_t min_distance, dva_t *dva, int d)
{
	spa_t *spa = mg->mg_vd->vdev_spa;
	metaslab_t *msp = NULL;
	uint64_t offset = -1ULL;
	avl_tree_t *t = &mg->mg_metaslab_tree;
	uint64_t activation_weight;
	uint64_t target_distance;
	int i;

	activation_weight = METASLAB_WEIGHT_PRIMARY;
	for (i = 0; i < d; i++) {
		if (DVA_GET_VDEV(&dva[i]) == mg->mg_vd->vdev_id) {
			activation_weight = METASLAB_WEIGHT_SECONDARY;
			break;
		}
	}

	for (;;) {
		boolean_t was_active;

		mutex_enter(&mg->mg_lock);
		for (msp = avl_first(t); msp; msp = AVL_NEXT(t, msp)) {
			if (msp->ms_weight < asize) {
				spa_dbgmsg(spa, "%s: failed to meet weight "
				    "requirement: vdev %llu, txg %llu, mg %p, "
				    "msp %p, psize %llu, asize %llu, "
				    "weight %llu", spa_name(spa),
				    mg->mg_vd->vdev_id, txg,
				    mg, msp, psize, asize, msp->ms_weight);
				mutex_exit(&mg->mg_lock);
				return (-1ULL);
			}

			/*
			 * If the selected metaslab is condensing, skip it.
			 */
			if (msp->ms_condensing)
				continue;

			was_active = msp->ms_weight & METASLAB_ACTIVE_MASK;
			if (activation_weight == METASLAB_WEIGHT_PRIMARY)
				break;

			target_distance = min_distance +
			    (space_map_allocated(msp->ms_sm) != 0 ? 0 :
			    min_distance >> 1);

			for (i = 0; i < d; i++)
				if (metaslab_distance(msp, &dva[i]) <
				    target_distance)
					break;
			if (i == d)
				break;
		}
		mutex_exit(&mg->mg_lock);
		if (msp == NULL)
			return (-1ULL);

		mutex_enter(&msp->ms_lock);

		/*
		 * Ensure that the metaslab we have selected is still
		 * capable of handling our request. It's possible that
		 * another thread may have changed the weight while we
		 * were blocked on the metaslab lock.
		 */
		if (msp->ms_weight < asize || (was_active &&
		    !(msp->ms_weight & METASLAB_ACTIVE_MASK) &&
		    activation_weight == METASLAB_WEIGHT_PRIMARY)) {
			mutex_exit(&msp->ms_lock);
			continue;
		}

		if ((msp->ms_weight & METASLAB_WEIGHT_SECONDARY) &&
		    activation_weight == METASLAB_WEIGHT_PRIMARY) {
			metaslab_passivate(msp,
			    msp->ms_weight & ~METASLAB_ACTIVE_MASK);
			mutex_exit(&msp->ms_lock);
			continue;
		}

		if (metaslab_activate(msp, activation_weight) != 0) {
			mutex_exit(&msp->ms_lock);
			continue;
		}

		/*
		 * If this metaslab is currently condensing then pick again as
		 * we can't manipulate this metaslab until it's committed
		 * to disk.
		 */
		if (msp->ms_condensing) {
			mutex_exit(&msp->ms_lock);
			continue;
		}

		if ((offset = metaslab_block_alloc(msp, asize)) != -1ULL)
			break;

		metaslab_passivate(msp, metaslab_block_maxsize(msp));
		mutex_exit(&msp->ms_lock);
	}

	if (range_tree_space(msp->ms_alloctree[txg & TXG_MASK]) == 0)
		vdev_dirty(mg->mg_vd, VDD_METASLAB, msp, txg);

	range_tree_add(msp->ms_alloctree[txg & TXG_MASK], offset, asize);
	msp->ms_access_txg = txg + metaslab_unload_delay;

	mutex_exit(&msp->ms_lock);

	return (offset);
}

/*
 * Allocate a block for the specified i/o.
 */
static int
metaslab_alloc_dva(spa_t *spa, metaslab_class_t *mc, uint64_t psize,
    dva_t *dva, int d, dva_t *hintdva, uint64_t txg, int flags)
{
	metaslab_group_t *mg, *fast_mg, *rotor;
	vdev_t *vd;
	int dshift = 3;
	int all_zero;
	int zio_lock = B_FALSE;
	boolean_t allocatable;
	uint64_t offset = -1ULL;
	uint64_t asize;
	uint64_t distance;

	ASSERT(!DVA_IS_VALID(&dva[d]));

	/*
	 * For testing, make some blocks above a certain size be gang blocks.
	 */
	if (psize >= metaslab_gang_bang && (ddi_get_lbolt() & 3) == 0)
		return (SET_ERROR(ENOSPC));

	if (flags & METASLAB_FASTWRITE)
		mutex_enter(&mc->mc_fastwrite_lock);

	/*
	 * Start at the rotor and loop through all mgs until we find something.
	 * Note that there's no locking on mc_rotor or mc_aliquot because
	 * nothing actually breaks if we miss a few updates -- we just won't
	 * allocate quite as evenly.  It all balances out over time.
	 *
	 * If we are doing ditto or log blocks, try to spread them across
	 * consecutive vdevs.  If we're forced to reuse a vdev before we've
	 * allocated all of our ditto blocks, then try and spread them out on
	 * that vdev as much as possible.  If it turns out to not be possible,
	 * gradually lower our standards until anything becomes acceptable.
	 * Also, allocating on consecutive vdevs (as opposed to random vdevs)
	 * gives us hope of containing our fault domains to something we're
	 * able to reason about.  Otherwise, any two top-level vdev failures
	 * will guarantee the loss of data.  With consecutive allocation,
	 * only two adjacent top-level vdev failures will result in data loss.
	 *
	 * If we are doing gang blocks (hintdva is non-NULL), try to keep
	 * ourselves on the same vdev as our gang block header.  That
	 * way, we can hope for locality in vdev_cache, plus it makes our
	 * fault domains something tractable.
	 */
	if (hintdva) {
		vd = vdev_lookup_top(spa, DVA_GET_VDEV(&hintdva[d]));

		/*
		 * It's possible the vdev we're using as the hint no
		 * longer exists (i.e. removed). Consult the rotor when
		 * all else fails.
		 */
		if (vd != NULL) {
			mg = vd->vdev_mg;

			if (flags & METASLAB_HINTBP_AVOID &&
			    mg->mg_next != NULL)
				mg = mg->mg_next;
		} else {
			mg = mc->mc_rotor;
		}
	} else if (d != 0) {
		vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[d - 1]));
		mg = vd->vdev_mg->mg_next;
	} else if (flags & METASLAB_FASTWRITE) {
		mg = fast_mg = mc->mc_rotor;

		do {
			if (fast_mg->mg_vd->vdev_pending_fastwrite <
			    mg->mg_vd->vdev_pending_fastwrite)
				mg = fast_mg;
		} while ((fast_mg = fast_mg->mg_next) != mc->mc_rotor);

	} else {
		mg = mc->mc_rotor;
	}

	/*
	 * If the hint put us into the wrong metaslab class, or into a
	 * metaslab group that has been passivated, just follow the rotor.
	 */
	if (mg->mg_class != mc || mg->mg_activation_count <= 0)
		mg = mc->mc_rotor;

	rotor = mg;
top:
	all_zero = B_TRUE;
	do {
		ASSERT(mg->mg_activation_count == 1);

		vd = mg->mg_vd;

		/*
		 * Don't allocate from faulted devices.
		 */
		if (zio_lock) {
			spa_config_enter(spa, SCL_ZIO, FTAG, RW_READER);
			allocatable = vdev_allocatable(vd);
			spa_config_exit(spa, SCL_ZIO, FTAG);
		} else {
			allocatable = vdev_allocatable(vd);
		}

		/*
		 * Determine if the selected metaslab group is eligible
		 * for allocations. If we're ganging or have requested
		 * an allocation for the smallest gang block size
		 * then we don't want to avoid allocating to the this
		 * metaslab group. If we're in this condition we should
		 * try to allocate from any device possible so that we
		 * don't inadvertently return ENOSPC and suspend the pool
		 * even though space is still available.
		 */
		if (allocatable && CAN_FASTGANG(flags) &&
		    psize > SPA_GANGBLOCKSIZE)
			allocatable = metaslab_group_allocatable(mg);

		if (!allocatable)
			goto next;

		/*
		 * Avoid writing single-copy data to a failing vdev
		 * unless the user instructs us that it is okay.
		 */
		if ((vd->vdev_stat.vs_write_errors > 0 ||
		    vd->vdev_state < VDEV_STATE_HEALTHY) &&
		    d == 0 && dshift == 3 && vd->vdev_children == 0) {
			all_zero = B_FALSE;
			goto next;
		}

		ASSERT(mg->mg_class == mc);

		distance = vd->vdev_asize >> dshift;
		if (distance <= (1ULL << vd->vdev_ms_shift))
			distance = 0;
		else
			all_zero = B_FALSE;

		asize = vdev_psize_to_asize(vd, psize);
		ASSERT(P2PHASE(asize, 1ULL << vd->vdev_ashift) == 0);

		offset = metaslab_group_alloc(mg, psize, asize, txg, distance,
		    dva, d);
		if (offset != -1ULL) {
			/*
			 * If we've just selected this metaslab group,
			 * figure out whether the corresponding vdev is
			 * over- or under-used relative to the pool,
			 * and set an allocation bias to even it out.
			 *
			 * Bias is also used to compensate for unequally
			 * sized vdevs so that space is allocated fairly.
			 */
			if (mc->mc_aliquot == 0 && metaslab_bias_enabled) {
				vdev_stat_t *vs = &vd->vdev_stat;
				int64_t vs_free = vs->vs_space - vs->vs_alloc;
				int64_t mc_free = mc->mc_space - mc->mc_alloc;
				int64_t ratio;

				/*
				 * Calculate how much more or less we should
				 * try to allocate from this device during
				 * this iteration around the rotor.
				 *
				 * This basically introduces a zero-centered
				 * bias towards the devices with the most
				 * free space, while compensating for vdev
				 * size differences.
				 *
				 * Examples:
				 *  vdev V1 = 16M/128M
				 *  vdev V2 = 16M/128M
				 *  ratio(V1) = 100% ratio(V2) = 100%
				 *
				 *  vdev V1 = 16M/128M
				 *  vdev V2 = 64M/128M
				 *  ratio(V1) = 127% ratio(V2) =  72%
				 *
				 *  vdev V1 = 16M/128M
				 *  vdev V2 = 64M/512M
				 *  ratio(V1) =  40% ratio(V2) = 160%
				 */
				ratio = (vs_free * mc->mc_alloc_groups * 100) /
				    (mc_free + 1);
				mg->mg_bias = ((ratio - 100) *
				    (int64_t)mg->mg_aliquot) / 100;
			} else if (!metaslab_bias_enabled) {
				mg->mg_bias = 0;
			}

			if ((flags & METASLAB_FASTWRITE) ||
			    atomic_add_64_nv(&mc->mc_aliquot, asize) >=
			    mg->mg_aliquot + mg->mg_bias) {
				mc->mc_rotor = mg->mg_next;
				mc->mc_aliquot = 0;
			}

			DVA_SET_VDEV(&dva[d], vd->vdev_id);
			DVA_SET_OFFSET(&dva[d], offset);
			DVA_SET_GANG(&dva[d], !!(flags & METASLAB_GANG_HEADER));
			DVA_SET_ASIZE(&dva[d], asize);

			if (flags & METASLAB_FASTWRITE) {
				atomic_add_64(&vd->vdev_pending_fastwrite,
				    psize);
				mutex_exit(&mc->mc_fastwrite_lock);
			}

			return (0);
		}
next:
		mc->mc_rotor = mg->mg_next;
		mc->mc_aliquot = 0;
	} while ((mg = mg->mg_next) != rotor);

	if (!all_zero) {
		dshift++;
		ASSERT(dshift < 64);
		goto top;
	}

	if (!allocatable && !zio_lock) {
		dshift = 3;
		zio_lock = B_TRUE;
		goto top;
	}

	bzero(&dva[d], sizeof (dva_t));

	if (flags & METASLAB_FASTWRITE)
		mutex_exit(&mc->mc_fastwrite_lock);

	return (SET_ERROR(ENOSPC));
}

/*
 * Free the block represented by DVA in the context of the specified
 * transaction group.
 */
static void
metaslab_free_dva(spa_t *spa, const dva_t *dva, uint64_t txg, boolean_t now)
{
	uint64_t vdev = DVA_GET_VDEV(dva);
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);
	vdev_t *vd;
	metaslab_t *msp;

	if (txg > spa_freeze_txg(spa))
		return;

	if ((vd = vdev_lookup_top(spa, vdev)) == NULL || !DVA_IS_VALID(dva) ||
	    (offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count) {
		zfs_panic_recover("metaslab_free_dva(): bad DVA %llu:%llu:%llu",
		    (u_longlong_t)vdev, (u_longlong_t)offset,
		    (u_longlong_t)size);
		return;
	}

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	if (DVA_GET_GANG(dva))
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

	mutex_enter(&msp->ms_lock);

	if (now) {
		range_tree_remove(msp->ms_alloctree[txg & TXG_MASK],
		    offset, size);

		VERIFY(!msp->ms_condensing);
		VERIFY3U(offset, >=, msp->ms_start);
		VERIFY3U(offset + size, <=, msp->ms_start + msp->ms_size);
		VERIFY3U(range_tree_space(msp->ms_tree) + size, <=,
		    msp->ms_size);
		VERIFY0(P2PHASE(offset, 1ULL << vd->vdev_ashift));
		VERIFY0(P2PHASE(size, 1ULL << vd->vdev_ashift));
		range_tree_add(msp->ms_tree, offset, size);
	} else {
		if (range_tree_space(msp->ms_freetree[txg & TXG_MASK]) == 0)
			vdev_dirty(vd, VDD_METASLAB, msp, txg);
		range_tree_add(msp->ms_freetree[txg & TXG_MASK],
		    offset, size);
	}

	mutex_exit(&msp->ms_lock);
}

/*
 * Intent log support: upon opening the pool after a crash, notify the SPA
 * of blocks that the intent log has allocated for immediate write, but
 * which are still considered free by the SPA because the last transaction
 * group didn't commit yet.
 */
static int
metaslab_claim_dva(spa_t *spa, const dva_t *dva, uint64_t txg)
{
	uint64_t vdev = DVA_GET_VDEV(dva);
	uint64_t offset = DVA_GET_OFFSET(dva);
	uint64_t size = DVA_GET_ASIZE(dva);
	vdev_t *vd;
	metaslab_t *msp;
	int error = 0;

	ASSERT(DVA_IS_VALID(dva));

	if ((vd = vdev_lookup_top(spa, vdev)) == NULL ||
	    (offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count)
		return (SET_ERROR(ENXIO));

	msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	if (DVA_GET_GANG(dva))
		size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);

	mutex_enter(&msp->ms_lock);

	if ((txg != 0 && spa_writeable(spa)) || !msp->ms_loaded)
		error = metaslab_activate(msp, METASLAB_WEIGHT_SECONDARY);

	if (error == 0 && !range_tree_contains(msp->ms_tree, offset, size))
		error = SET_ERROR(ENOENT);

	if (error || txg == 0) {	/* txg == 0 indicates dry run */
		mutex_exit(&msp->ms_lock);
		return (error);
	}

	VERIFY(!msp->ms_condensing);
	VERIFY0(P2PHASE(offset, 1ULL << vd->vdev_ashift));
	VERIFY0(P2PHASE(size, 1ULL << vd->vdev_ashift));
	VERIFY3U(range_tree_space(msp->ms_tree) - size, <=, msp->ms_size);
	range_tree_remove(msp->ms_tree, offset, size);

	if (spa_writeable(spa)) {	/* don't dirty if we're zdb(1M) */
		if (range_tree_space(msp->ms_alloctree[txg & TXG_MASK]) == 0)
			vdev_dirty(vd, VDD_METASLAB, msp, txg);
		range_tree_add(msp->ms_alloctree[txg & TXG_MASK], offset, size);
	}

	mutex_exit(&msp->ms_lock);

	return (0);
}

int
metaslab_alloc(spa_t *spa, metaslab_class_t *mc, uint64_t psize, blkptr_t *bp,
    int ndvas, uint64_t txg, blkptr_t *hintbp, int flags)
{
	dva_t *dva = bp->blk_dva;
	dva_t *hintdva = hintbp->blk_dva;
	int d, error = 0;

	ASSERT(bp->blk_birth == 0);
	ASSERT(BP_PHYSICAL_BIRTH(bp) == 0);

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);

	if (mc->mc_rotor == NULL) {	/* no vdevs in this class */
		spa_config_exit(spa, SCL_ALLOC, FTAG);
		return (SET_ERROR(ENOSPC));
	}

	ASSERT(ndvas > 0 && ndvas <= spa_max_replication(spa));
	ASSERT(BP_GET_NDVAS(bp) == 0);
	ASSERT(hintbp == NULL || ndvas <= BP_GET_NDVAS(hintbp));

	for (d = 0; d < ndvas; d++) {
		error = metaslab_alloc_dva(spa, mc, psize, dva, d, hintdva,
		    txg, flags);
		if (error != 0) {
			for (d--; d >= 0; d--) {
				metaslab_free_dva(spa, &dva[d], txg, B_TRUE);
				bzero(&dva[d], sizeof (dva_t));
			}
			spa_config_exit(spa, SCL_ALLOC, FTAG);
			return (error);
		}
	}
	ASSERT(error == 0);
	ASSERT(BP_GET_NDVAS(bp) == ndvas);

	spa_config_exit(spa, SCL_ALLOC, FTAG);

	BP_SET_BIRTH(bp, txg, txg);

	return (0);
}

void
metaslab_free(spa_t *spa, const blkptr_t *bp, uint64_t txg, boolean_t now)
{
	const dva_t *dva = bp->blk_dva;
	int d, ndvas = BP_GET_NDVAS(bp);

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!now || bp->blk_birth >= spa_syncing_txg(spa));

	spa_config_enter(spa, SCL_FREE, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++)
		metaslab_free_dva(spa, &dva[d], txg, now);

	spa_config_exit(spa, SCL_FREE, FTAG);
}

int
metaslab_claim(spa_t *spa, const blkptr_t *bp, uint64_t txg)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	int d, error = 0;

	ASSERT(!BP_IS_HOLE(bp));

	if (txg != 0) {
		/*
		 * First do a dry run to make sure all DVAs are claimable,
		 * so we don't have to unwind from partial failures below.
		 */
		if ((error = metaslab_claim(spa, bp, 0)) != 0)
			return (error);
	}

	spa_config_enter(spa, SCL_ALLOC, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++)
		if ((error = metaslab_claim_dva(spa, &dva[d], txg)) != 0)
			break;

	spa_config_exit(spa, SCL_ALLOC, FTAG);

	ASSERT(error == 0 || txg == 0);

	return (error);
}

void
metaslab_fastwrite_mark(spa_t *spa, const blkptr_t *bp)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	uint64_t psize = BP_GET_PSIZE(bp);
	int d;
	vdev_t *vd;

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!BP_IS_EMBEDDED(bp));
	ASSERT(psize > 0);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++) {
		if ((vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[d]))) == NULL)
			continue;
		atomic_add_64(&vd->vdev_pending_fastwrite, psize);
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);
}

void
metaslab_fastwrite_unmark(spa_t *spa, const blkptr_t *bp)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = BP_GET_NDVAS(bp);
	uint64_t psize = BP_GET_PSIZE(bp);
	int d;
	vdev_t *vd;

	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(!BP_IS_EMBEDDED(bp));
	ASSERT(psize > 0);

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	for (d = 0; d < ndvas; d++) {
		if ((vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[d]))) == NULL)
			continue;
		ASSERT3U(vd->vdev_pending_fastwrite, >=, psize);
		atomic_sub_64(&vd->vdev_pending_fastwrite, psize);
	}

	spa_config_exit(spa, SCL_VDEV, FTAG);
}

void
metaslab_check_free(spa_t *spa, const blkptr_t *bp)
{
	int i, j;

	if ((zfs_flags & ZFS_DEBUG_ZIO_FREE) == 0)
		return;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	for (i = 0; i < BP_GET_NDVAS(bp); i++) {
		uint64_t vdev = DVA_GET_VDEV(&bp->blk_dva[i]);
		vdev_t *vd = vdev_lookup_top(spa, vdev);
		uint64_t offset = DVA_GET_OFFSET(&bp->blk_dva[i]);
		uint64_t size = DVA_GET_ASIZE(&bp->blk_dva[i]);
		metaslab_t *msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

		if (msp->ms_loaded)
			range_tree_verify(msp->ms_tree, offset, size);

		for (j = 0; j < TXG_SIZE; j++)
			range_tree_verify(msp->ms_freetree[j], offset, size);
		for (j = 0; j < TXG_DEFER_SIZE; j++)
			range_tree_verify(msp->ms_defertree[j], offset, size);
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(metaslab_aliquot, ulong, 0644);
module_param(metaslab_debug_load, int, 0644);
module_param(metaslab_debug_unload, int, 0644);
module_param(metaslab_preload_enabled, int, 0644);
module_param(zfs_mg_noalloc_threshold, int, 0644);
module_param(zfs_mg_fragmentation_threshold, int, 0644);
module_param(zfs_metaslab_fragmentation_threshold, int, 0644);
module_param(metaslab_fragmentation_factor_enabled, int, 0644);
module_param(metaslab_lba_weighting_enabled, int, 0644);
module_param(metaslab_bias_enabled, int, 0644);

MODULE_PARM_DESC(metaslab_aliquot,
	"allocation granularity (a.k.a. stripe size)");
MODULE_PARM_DESC(metaslab_debug_load,
	"load all metaslabs when pool is first opened");
MODULE_PARM_DESC(metaslab_debug_unload,
	"prevent metaslabs from being unloaded");
MODULE_PARM_DESC(metaslab_preload_enabled,
	"preload potential metaslabs during reassessment");

MODULE_PARM_DESC(zfs_mg_noalloc_threshold,
	"percentage of free space for metaslab group to allow allocation");
MODULE_PARM_DESC(zfs_mg_fragmentation_threshold,
	"fragmentation for metaslab group to allow allocation");

MODULE_PARM_DESC(zfs_metaslab_fragmentation_threshold,
	"fragmentation for metaslab to allow allocation");
MODULE_PARM_DESC(metaslab_fragmentation_factor_enabled,
	"use the fragmentation metric to prefer less fragmented metaslabs");
MODULE_PARM_DESC(metaslab_lba_weighting_enabled,
	"prefer metaslabs with lower LBAs");
MODULE_PARM_DESC(metaslab_bias_enabled,
	"enable metaslab group biasing");
#endif /* _KERNEL && HAVE_SPL */
