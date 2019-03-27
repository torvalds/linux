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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>

#include <sys/fm/fs/zfs.h>
#include <sys/fm/protocol.h>
#include <sys/fm/util.h>
#include <sys/sysevent.h>

/*
 * This general routine is responsible for generating all the different ZFS
 * ereports.  The payload is dependent on the class, and which arguments are
 * supplied to the function:
 *
 * 	EREPORT			POOL	VDEV	IO
 * 	block			X	X	X
 * 	data			X		X
 * 	device			X	X
 * 	pool			X
 *
 * If we are in a loading state, all errors are chained together by the same
 * SPA-wide ENA (Error Numeric Association).
 *
 * For isolated I/O requests, we get the ENA from the zio_t. The propagation
 * gets very complicated due to RAID-Z, gang blocks, and vdev caching.  We want
 * to chain together all ereports associated with a logical piece of data.  For
 * read I/Os, there  are basically three 'types' of I/O, which form a roughly
 * layered diagram:
 *
 *      +---------------+
 * 	| Aggregate I/O |	No associated logical data or device
 * 	+---------------+
 *              |
 *              V
 * 	+---------------+	Reads associated with a piece of logical data.
 * 	|   Read I/O    |	This includes reads on behalf of RAID-Z,
 * 	+---------------+       mirrors, gang blocks, retries, etc.
 *              |
 *              V
 * 	+---------------+	Reads associated with a particular device, but
 * 	| Physical I/O  |	no logical data.  Issued as part of vdev caching
 * 	+---------------+	and I/O aggregation.
 *
 * Note that 'physical I/O' here is not the same terminology as used in the rest
 * of ZIO.  Typically, 'physical I/O' simply means that there is no attached
 * blockpointer.  But I/O with no associated block pointer can still be related
 * to a logical piece of data (i.e. RAID-Z requests).
 *
 * Purely physical I/O always have unique ENAs.  They are not related to a
 * particular piece of logical data, and therefore cannot be chained together.
 * We still generate an ereport, but the DE doesn't correlate it with any
 * logical piece of data.  When such an I/O fails, the delegated I/O requests
 * will issue a retry, which will trigger the 'real' ereport with the correct
 * ENA.
 *
 * We keep track of the ENA for a ZIO chain through the 'io_logical' member.
 * When a new logical I/O is issued, we set this to point to itself.  Child I/Os
 * then inherit this pointer, so that when it is first set subsequent failures
 * will use the same ENA.  For vdev cache fill and queue aggregation I/O,
 * this pointer is set to NULL, and no ereport will be generated (since it
 * doesn't actually correspond to any particular device or piece of data,
 * and the caller will always retry without caching or queueing anyway).
 *
 * For checksum errors, we want to include more information about the actual
 * error which occurs.  Accordingly, we build an ereport when the error is
 * noticed, but instead of sending it in immediately, we hang it off of the
 * io_cksum_report field of the logical IO.  When the logical IO completes
 * (successfully or not), zfs_ereport_finish_checksum() is called with the
 * good and bad versions of the buffer (if available), and we annotate the
 * ereport with information about the differences.
 */
#ifdef _KERNEL
static void
zfs_ereport_start(nvlist_t **ereport_out, nvlist_t **detector_out,
    const char *subclass, spa_t *spa, vdev_t *vd, zio_t *zio,
    uint64_t stateoroffset, uint64_t size)
{
	nvlist_t *ereport, *detector;

	uint64_t ena;
	char class[64];

	/*
	 * If we are doing a spa_tryimport() or in recovery mode,
	 * ignore errors.
	 */
	if (spa_load_state(spa) == SPA_LOAD_TRYIMPORT ||
	    spa_load_state(spa) == SPA_LOAD_RECOVER)
		return;

	/*
	 * If we are in the middle of opening a pool, and the previous attempt
	 * failed, don't bother logging any new ereports - we're just going to
	 * get the same diagnosis anyway.
	 */
	if (spa_load_state(spa) != SPA_LOAD_NONE &&
	    spa->spa_last_open_failed)
		return;

	if (zio != NULL) {
		/*
		 * If this is not a read or write zio, ignore the error.  This
		 * can occur if the DKIOCFLUSHWRITECACHE ioctl fails.
		 */
		if (zio->io_type != ZIO_TYPE_READ &&
		    zio->io_type != ZIO_TYPE_WRITE)
			return;

		/*
		 * Ignore any errors from speculative I/Os, as failure is an
		 * expected result.
		 */
		if (zio->io_flags & ZIO_FLAG_SPECULATIVE)
			return;

		/*
		 * If this I/O is not a retry I/O, don't post an ereport.
		 * Otherwise, we risk making bad diagnoses based on B_FAILFAST
		 * I/Os.
		 */
		if (zio->io_error == EIO &&
		    !(zio->io_flags & ZIO_FLAG_IO_RETRY))
			return;

		if (vd != NULL) {
			/*
			 * If the vdev has already been marked as failing due
			 * to a failed probe, then ignore any subsequent I/O
			 * errors, as the DE will automatically fault the vdev
			 * on the first such failure.  This also catches cases
			 * where vdev_remove_wanted is set and the device has
			 * not yet been asynchronously placed into the REMOVED
			 * state.
			 */
			if (zio->io_vd == vd && !vdev_accessible(vd, zio))
				return;

			/*
			 * Ignore checksum errors for reads from DTL regions of
			 * leaf vdevs.
			 */
			if (zio->io_type == ZIO_TYPE_READ &&
			    zio->io_error == ECKSUM &&
			    vd->vdev_ops->vdev_op_leaf &&
			    vdev_dtl_contains(vd, DTL_MISSING, zio->io_txg, 1))
				return;
		}
	}

	/*
	 * For probe failure, we want to avoid posting ereports if we've
	 * already removed the device in the meantime.
	 */
	if (vd != NULL &&
	    strcmp(subclass, FM_EREPORT_ZFS_PROBE_FAILURE) == 0 &&
	    (vd->vdev_remove_wanted || vd->vdev_state == VDEV_STATE_REMOVED))
		return;

	if ((ereport = fm_nvlist_create(NULL)) == NULL)
		return;

	if ((detector = fm_nvlist_create(NULL)) == NULL) {
		fm_nvlist_destroy(ereport, FM_NVA_FREE);
		return;
	}

	/*
	 * Serialize ereport generation
	 */
	mutex_enter(&spa->spa_errlist_lock);

	/*
	 * Determine the ENA to use for this event.  If we are in a loading
	 * state, use a SPA-wide ENA.  Otherwise, if we are in an I/O state, use
	 * a root zio-wide ENA.  Otherwise, simply use a unique ENA.
	 */
	if (spa_load_state(spa) != SPA_LOAD_NONE) {
		if (spa->spa_ena == 0)
			spa->spa_ena = fm_ena_generate(0, FM_ENA_FMT1);
		ena = spa->spa_ena;
	} else if (zio != NULL && zio->io_logical != NULL) {
		if (zio->io_logical->io_ena == 0)
			zio->io_logical->io_ena =
			    fm_ena_generate(0, FM_ENA_FMT1);
		ena = zio->io_logical->io_ena;
	} else {
		ena = fm_ena_generate(0, FM_ENA_FMT1);
	}

	/*
	 * Construct the full class, detector, and other standard FMA fields.
	 */
	(void) snprintf(class, sizeof (class), "%s.%s",
	    ZFS_ERROR_CLASS, subclass);

	fm_fmri_zfs_set(detector, FM_ZFS_SCHEME_VERSION, spa_guid(spa),
	    vd != NULL ? vd->vdev_guid : 0);

	fm_ereport_set(ereport, FM_EREPORT_VERSION, class, ena, detector, NULL);

	/*
	 * Construct the per-ereport payload, depending on which parameters are
	 * passed in.
	 */

	/*
	 * Generic payload members common to all ereports.
	 */
	fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_POOL,
	    DATA_TYPE_STRING, spa_name(spa), FM_EREPORT_PAYLOAD_ZFS_POOL_GUID,
	    DATA_TYPE_UINT64, spa_guid(spa),
	    FM_EREPORT_PAYLOAD_ZFS_POOL_CONTEXT, DATA_TYPE_INT32,
	    spa_load_state(spa), NULL);

	if (spa != NULL) {
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_POOL_FAILMODE,
		    DATA_TYPE_STRING,
		    spa_get_failmode(spa) == ZIO_FAILURE_MODE_WAIT ?
		    FM_EREPORT_FAILMODE_WAIT :
		    spa_get_failmode(spa) == ZIO_FAILURE_MODE_CONTINUE ?
		    FM_EREPORT_FAILMODE_CONTINUE : FM_EREPORT_FAILMODE_PANIC,
		    NULL);
	}

	if (vd != NULL) {
		vdev_t *pvd = vd->vdev_parent;

		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID,
		    DATA_TYPE_UINT64, vd->vdev_guid,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_TYPE,
		    DATA_TYPE_STRING, vd->vdev_ops->vdev_op_type, NULL);
		if (vd->vdev_path != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_PATH,
			    DATA_TYPE_STRING, vd->vdev_path, NULL);
		if (vd->vdev_devid != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_DEVID,
			    DATA_TYPE_STRING, vd->vdev_devid, NULL);
		if (vd->vdev_fru != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_VDEV_FRU,
			    DATA_TYPE_STRING, vd->vdev_fru, NULL);

		if (pvd != NULL) {
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_PARENT_GUID,
			    DATA_TYPE_UINT64, pvd->vdev_guid,
			    FM_EREPORT_PAYLOAD_ZFS_PARENT_TYPE,
			    DATA_TYPE_STRING, pvd->vdev_ops->vdev_op_type,
			    NULL);
			if (pvd->vdev_path)
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_PARENT_PATH,
				    DATA_TYPE_STRING, pvd->vdev_path, NULL);
			if (pvd->vdev_devid)
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_PARENT_DEVID,
				    DATA_TYPE_STRING, pvd->vdev_devid, NULL);
		}
	}

	if (zio != NULL) {
		/*
		 * Payload common to all I/Os.
		 */
		fm_payload_set(ereport, FM_EREPORT_PAYLOAD_ZFS_ZIO_ERR,
		    DATA_TYPE_INT32, zio->io_error, NULL);

		/*
		 * If the 'size' parameter is non-zero, it indicates this is a
		 * RAID-Z or other I/O where the physical offset and length are
		 * provided for us, instead of within the zio_t.
		 */
		if (vd != NULL) {
			if (size)
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET,
				    DATA_TYPE_UINT64, stateoroffset,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_SIZE,
				    DATA_TYPE_UINT64, size, NULL);
			else
				fm_payload_set(ereport,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET,
				    DATA_TYPE_UINT64, zio->io_offset,
				    FM_EREPORT_PAYLOAD_ZFS_ZIO_SIZE,
				    DATA_TYPE_UINT64, zio->io_size, NULL);
		}

		/*
		 * Payload for I/Os with corresponding logical information.
		 */
		if (zio->io_logical != NULL)
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJSET,
			    DATA_TYPE_UINT64,
			    zio->io_logical->io_bookmark.zb_objset,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJECT,
			    DATA_TYPE_UINT64,
			    zio->io_logical->io_bookmark.zb_object,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_LEVEL,
			    DATA_TYPE_INT64,
			    zio->io_logical->io_bookmark.zb_level,
			    FM_EREPORT_PAYLOAD_ZFS_ZIO_BLKID,
			    DATA_TYPE_UINT64,
			    zio->io_logical->io_bookmark.zb_blkid, NULL);
	} else if (vd != NULL) {
		/*
		 * If we have a vdev but no zio, this is a device fault, and the
		 * 'stateoroffset' parameter indicates the previous state of the
		 * vdev.
		 */
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_PREV_STATE,
		    DATA_TYPE_UINT64, stateoroffset, NULL);
	}

	mutex_exit(&spa->spa_errlist_lock);

	*ereport_out = ereport;
	*detector_out = detector;
}

/* if it's <= 128 bytes, save the corruption directly */
#define	ZFM_MAX_INLINE		(128 / sizeof (uint64_t))

#define	MAX_RANGES		16

typedef struct zfs_ecksum_info {
	/* histograms of set and cleared bits by bit number in a 64-bit word */
	uint32_t zei_histogram_set[sizeof (uint64_t) * NBBY];
	uint32_t zei_histogram_cleared[sizeof (uint64_t) * NBBY];

	/* inline arrays of bits set and cleared. */
	uint64_t zei_bits_set[ZFM_MAX_INLINE];
	uint64_t zei_bits_cleared[ZFM_MAX_INLINE];

	/*
	 * for each range, the number of bits set and cleared.  The Hamming
	 * distance between the good and bad buffers is the sum of them all.
	 */
	uint32_t zei_range_sets[MAX_RANGES];
	uint32_t zei_range_clears[MAX_RANGES];

	struct zei_ranges {
		uint32_t	zr_start;
		uint32_t	zr_end;
	} zei_ranges[MAX_RANGES];

	size_t	zei_range_count;
	uint32_t zei_mingap;
	uint32_t zei_allowed_mingap;

} zfs_ecksum_info_t;

static void
update_histogram(uint64_t value_arg, uint32_t *hist, uint32_t *count)
{
	size_t i;
	size_t bits = 0;
	uint64_t value = BE_64(value_arg);

	/* We store the bits in big-endian (largest-first) order */
	for (i = 0; i < 64; i++) {
		if (value & (1ull << i)) {
			hist[63 - i]++;
			++bits;
		}
	}
	/* update the count of bits changed */
	*count += bits;
}

/*
 * We've now filled up the range array, and need to increase "mingap" and
 * shrink the range list accordingly.  zei_mingap is always the smallest
 * distance between array entries, so we set the new_allowed_gap to be
 * one greater than that.  We then go through the list, joining together
 * any ranges which are closer than the new_allowed_gap.
 *
 * By construction, there will be at least one.  We also update zei_mingap
 * to the new smallest gap, to prepare for our next invocation.
 */
static void
shrink_ranges(zfs_ecksum_info_t *eip)
{
	uint32_t mingap = UINT32_MAX;
	uint32_t new_allowed_gap = eip->zei_mingap + 1;

	size_t idx, output;
	size_t max = eip->zei_range_count;

	struct zei_ranges *r = eip->zei_ranges;

	ASSERT3U(eip->zei_range_count, >, 0);
	ASSERT3U(eip->zei_range_count, <=, MAX_RANGES);

	output = idx = 0;
	while (idx < max - 1) {
		uint32_t start = r[idx].zr_start;
		uint32_t end = r[idx].zr_end;

		while (idx < max - 1) {
			idx++;

			uint32_t nstart = r[idx].zr_start;
			uint32_t nend = r[idx].zr_end;

			uint32_t gap = nstart - end;
			if (gap < new_allowed_gap) {
				end = nend;
				continue;
			}
			if (gap < mingap)
				mingap = gap;
			break;
		}
		r[output].zr_start = start;
		r[output].zr_end = end;
		output++;
	}
	ASSERT3U(output, <, eip->zei_range_count);
	eip->zei_range_count = output;
	eip->zei_mingap = mingap;
	eip->zei_allowed_mingap = new_allowed_gap;
}

static void
add_range(zfs_ecksum_info_t *eip, int start, int end)
{
	struct zei_ranges *r = eip->zei_ranges;
	size_t count = eip->zei_range_count;

	if (count >= MAX_RANGES) {
		shrink_ranges(eip);
		count = eip->zei_range_count;
	}
	if (count == 0) {
		eip->zei_mingap = UINT32_MAX;
		eip->zei_allowed_mingap = 1;
	} else {
		int gap = start - r[count - 1].zr_end;

		if (gap < eip->zei_allowed_mingap) {
			r[count - 1].zr_end = end;
			return;
		}
		if (gap < eip->zei_mingap)
			eip->zei_mingap = gap;
	}
	r[count].zr_start = start;
	r[count].zr_end = end;
	eip->zei_range_count++;
}

static size_t
range_total_size(zfs_ecksum_info_t *eip)
{
	struct zei_ranges *r = eip->zei_ranges;
	size_t count = eip->zei_range_count;
	size_t result = 0;
	size_t idx;

	for (idx = 0; idx < count; idx++)
		result += (r[idx].zr_end - r[idx].zr_start);

	return (result);
}

static zfs_ecksum_info_t *
annotate_ecksum(nvlist_t *ereport, zio_bad_cksum_t *info,
    const uint8_t *goodbuf, const uint8_t *badbuf, size_t size,
    boolean_t drop_if_identical)
{
	const uint64_t *good = (const uint64_t *)goodbuf;
	const uint64_t *bad = (const uint64_t *)badbuf;

	uint64_t allset = 0;
	uint64_t allcleared = 0;

	size_t nui64s = size / sizeof (uint64_t);

	size_t inline_size;
	int no_inline = 0;
	size_t idx;
	size_t range;

	size_t offset = 0;
	ssize_t start = -1;

	zfs_ecksum_info_t *eip = kmem_zalloc(sizeof (*eip), KM_SLEEP);

	/* don't do any annotation for injected checksum errors */
	if (info != NULL && info->zbc_injected)
		return (eip);

	if (info != NULL && info->zbc_has_cksum) {
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_CKSUM_EXPECTED,
		    DATA_TYPE_UINT64_ARRAY,
		    sizeof (info->zbc_expected) / sizeof (uint64_t),
		    (uint64_t *)&info->zbc_expected,
		    FM_EREPORT_PAYLOAD_ZFS_CKSUM_ACTUAL,
		    DATA_TYPE_UINT64_ARRAY,
		    sizeof (info->zbc_actual) / sizeof (uint64_t),
		    (uint64_t *)&info->zbc_actual,
		    FM_EREPORT_PAYLOAD_ZFS_CKSUM_ALGO,
		    DATA_TYPE_STRING,
		    info->zbc_checksum_name,
		    NULL);

		if (info->zbc_byteswapped) {
			fm_payload_set(ereport,
			    FM_EREPORT_PAYLOAD_ZFS_CKSUM_BYTESWAP,
			    DATA_TYPE_BOOLEAN, 1,
			    NULL);
		}
	}

	if (badbuf == NULL || goodbuf == NULL)
		return (eip);

	ASSERT3U(nui64s, <=, UINT32_MAX);
	ASSERT3U(size, ==, nui64s * sizeof (uint64_t));
	ASSERT3U(size, <=, SPA_MAXBLOCKSIZE);
	ASSERT3U(size, <=, UINT32_MAX);

	/* build up the range list by comparing the two buffers. */
	for (idx = 0; idx < nui64s; idx++) {
		if (good[idx] == bad[idx]) {
			if (start == -1)
				continue;

			add_range(eip, start, idx);
			start = -1;
		} else {
			if (start != -1)
				continue;

			start = idx;
		}
	}
	if (start != -1)
		add_range(eip, start, idx);

	/* See if it will fit in our inline buffers */
	inline_size = range_total_size(eip);
	if (inline_size > ZFM_MAX_INLINE)
		no_inline = 1;

	/*
	 * If there is no change and we want to drop if the buffers are
	 * identical, do so.
	 */
	if (inline_size == 0 && drop_if_identical) {
		kmem_free(eip, sizeof (*eip));
		return (NULL);
	}

	/*
	 * Now walk through the ranges, filling in the details of the
	 * differences.  Also convert our uint64_t-array offsets to byte
	 * offsets.
	 */
	for (range = 0; range < eip->zei_range_count; range++) {
		size_t start = eip->zei_ranges[range].zr_start;
		size_t end = eip->zei_ranges[range].zr_end;

		for (idx = start; idx < end; idx++) {
			uint64_t set, cleared;

			// bits set in bad, but not in good
			set = ((~good[idx]) & bad[idx]);
			// bits set in good, but not in bad
			cleared = (good[idx] & (~bad[idx]));

			allset |= set;
			allcleared |= cleared;

			if (!no_inline) {
				ASSERT3U(offset, <, inline_size);
				eip->zei_bits_set[offset] = set;
				eip->zei_bits_cleared[offset] = cleared;
				offset++;
			}

			update_histogram(set, eip->zei_histogram_set,
			    &eip->zei_range_sets[range]);
			update_histogram(cleared, eip->zei_histogram_cleared,
			    &eip->zei_range_clears[range]);
		}

		/* convert to byte offsets */
		eip->zei_ranges[range].zr_start	*= sizeof (uint64_t);
		eip->zei_ranges[range].zr_end	*= sizeof (uint64_t);
	}
	eip->zei_allowed_mingap	*= sizeof (uint64_t);
	inline_size		*= sizeof (uint64_t);

	/* fill in ereport */
	fm_payload_set(ereport,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_OFFSET_RANGES,
	    DATA_TYPE_UINT32_ARRAY, 2 * eip->zei_range_count,
	    (uint32_t *)eip->zei_ranges,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_MIN_GAP,
	    DATA_TYPE_UINT32, eip->zei_allowed_mingap,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_SETS,
	    DATA_TYPE_UINT32_ARRAY, eip->zei_range_count, eip->zei_range_sets,
	    FM_EREPORT_PAYLOAD_ZFS_BAD_RANGE_CLEARS,
	    DATA_TYPE_UINT32_ARRAY, eip->zei_range_count, eip->zei_range_clears,
	    NULL);

	if (!no_inline) {
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_BAD_SET_BITS,
		    DATA_TYPE_UINT8_ARRAY,
		    inline_size, (uint8_t *)eip->zei_bits_set,
		    FM_EREPORT_PAYLOAD_ZFS_BAD_CLEARED_BITS,
		    DATA_TYPE_UINT8_ARRAY,
		    inline_size, (uint8_t *)eip->zei_bits_cleared,
		    NULL);
	} else {
		fm_payload_set(ereport,
		    FM_EREPORT_PAYLOAD_ZFS_BAD_SET_HISTOGRAM,
		    DATA_TYPE_UINT32_ARRAY,
		    NBBY * sizeof (uint64_t), eip->zei_histogram_set,
		    FM_EREPORT_PAYLOAD_ZFS_BAD_CLEARED_HISTOGRAM,
		    DATA_TYPE_UINT32_ARRAY,
		    NBBY * sizeof (uint64_t), eip->zei_histogram_cleared,
		    NULL);
	}
	return (eip);
}
#endif

void
zfs_ereport_post(const char *subclass, spa_t *spa, vdev_t *vd, zio_t *zio,
    uint64_t stateoroffset, uint64_t size)
{
#ifdef _KERNEL
	nvlist_t *ereport = NULL;
	nvlist_t *detector = NULL;

	zfs_ereport_start(&ereport, &detector,
	    subclass, spa, vd, zio, stateoroffset, size);

	if (ereport == NULL)
		return;

	fm_ereport_post(ereport, EVCH_SLEEP);

	fm_nvlist_destroy(ereport, FM_NVA_FREE);
	fm_nvlist_destroy(detector, FM_NVA_FREE);
#endif
}

void
zfs_ereport_start_checksum(spa_t *spa, vdev_t *vd,
    struct zio *zio, uint64_t offset, uint64_t length, void *arg,
    zio_bad_cksum_t *info)
{
	zio_cksum_report_t *report = kmem_zalloc(sizeof (*report), KM_SLEEP);

	if (zio->io_vsd != NULL)
		zio->io_vsd_ops->vsd_cksum_report(zio, report, arg);
	else
		zio_vsd_default_cksum_report(zio, report, arg);

	/* copy the checksum failure information if it was provided */
	if (info != NULL) {
		report->zcr_ckinfo = kmem_zalloc(sizeof (*info), KM_SLEEP);
		bcopy(info, report->zcr_ckinfo, sizeof (*info));
	}

	report->zcr_align = 1ULL << vd->vdev_top->vdev_ashift;
	report->zcr_length = length;

#ifdef _KERNEL
	zfs_ereport_start(&report->zcr_ereport, &report->zcr_detector,
	    FM_EREPORT_ZFS_CHECKSUM, spa, vd, zio, offset, length);

	if (report->zcr_ereport == NULL) {
		report->zcr_free(report->zcr_cbdata, report->zcr_cbinfo);
		if (report->zcr_ckinfo != NULL) {
			kmem_free(report->zcr_ckinfo,
			    sizeof (*report->zcr_ckinfo));
		}
		kmem_free(report, sizeof (*report));
		return;
	}
#endif

	mutex_enter(&spa->spa_errlist_lock);
	report->zcr_next = zio->io_logical->io_cksum_report;
	zio->io_logical->io_cksum_report = report;
	mutex_exit(&spa->spa_errlist_lock);
}

void
zfs_ereport_finish_checksum(zio_cksum_report_t *report,
    const void *good_data, const void *bad_data, boolean_t drop_if_identical)
{
#ifdef _KERNEL
	zfs_ecksum_info_t *info = NULL;
	info = annotate_ecksum(report->zcr_ereport, report->zcr_ckinfo,
	    good_data, bad_data, report->zcr_length, drop_if_identical);

	if (info != NULL)
		fm_ereport_post(report->zcr_ereport, EVCH_SLEEP);

	fm_nvlist_destroy(report->zcr_ereport, FM_NVA_FREE);
	fm_nvlist_destroy(report->zcr_detector, FM_NVA_FREE);
	report->zcr_ereport = report->zcr_detector = NULL;

	if (info != NULL)
		kmem_free(info, sizeof (*info));
#endif
}

void
zfs_ereport_free_checksum(zio_cksum_report_t *rpt)
{
#ifdef _KERNEL
	if (rpt->zcr_ereport != NULL) {
		fm_nvlist_destroy(rpt->zcr_ereport,
		    FM_NVA_FREE);
		fm_nvlist_destroy(rpt->zcr_detector,
		    FM_NVA_FREE);
	}
#endif
	rpt->zcr_free(rpt->zcr_cbdata, rpt->zcr_cbinfo);

	if (rpt->zcr_ckinfo != NULL)
		kmem_free(rpt->zcr_ckinfo, sizeof (*rpt->zcr_ckinfo));

	kmem_free(rpt, sizeof (*rpt));
}

void
zfs_ereport_send_interim_checksum(zio_cksum_report_t *report)
{
#ifdef _KERNEL
	fm_ereport_post(report->zcr_ereport, EVCH_SLEEP);
#endif
}

void
zfs_ereport_post_checksum(spa_t *spa, vdev_t *vd,
    struct zio *zio, uint64_t offset, uint64_t length,
    const void *good_data, const void *bad_data, zio_bad_cksum_t *zbc)
{
#ifdef _KERNEL
	nvlist_t *ereport = NULL;
	nvlist_t *detector = NULL;
	zfs_ecksum_info_t *info;

	zfs_ereport_start(&ereport, &detector,
	    FM_EREPORT_ZFS_CHECKSUM, spa, vd, zio, offset, length);

	if (ereport == NULL)
		return;

	info = annotate_ecksum(ereport, zbc, good_data, bad_data, length,
	    B_FALSE);

	if (info != NULL)
		fm_ereport_post(ereport, EVCH_SLEEP);

	fm_nvlist_destroy(ereport, FM_NVA_FREE);
	fm_nvlist_destroy(detector, FM_NVA_FREE);

	if (info != NULL)
		kmem_free(info, sizeof (*info));
#endif
}

static void
zfs_post_common(spa_t *spa, vdev_t *vd, const char *name)
{
#ifdef _KERNEL
	nvlist_t *resource;
	char class[64];

	if (spa_load_state(spa) == SPA_LOAD_TRYIMPORT)
		return;

	if ((resource = fm_nvlist_create(NULL)) == NULL)
		return;

	(void) snprintf(class, sizeof (class), "%s.%s.%s", FM_RSRC_RESOURCE,
	    ZFS_ERROR_CLASS, name);
	VERIFY(nvlist_add_uint8(resource, FM_VERSION, FM_RSRC_VERSION) == 0);
	VERIFY(nvlist_add_string(resource, FM_CLASS, class) == 0);
	VERIFY(nvlist_add_uint64(resource,
	    FM_EREPORT_PAYLOAD_ZFS_POOL_GUID, spa_guid(spa)) == 0);
	if (vd)
		VERIFY(nvlist_add_uint64(resource,
		    FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID, vd->vdev_guid) == 0);

	fm_ereport_post(resource, EVCH_SLEEP);

	fm_nvlist_destroy(resource, FM_NVA_FREE);
#endif
}

/*
 * The 'resource.fs.zfs.removed' event is an internal signal that the given vdev
 * has been removed from the system.  This will cause the DE to ignore any
 * recent I/O errors, inferring that they are due to the asynchronous device
 * removal.
 */
void
zfs_post_remove(spa_t *spa, vdev_t *vd)
{
	zfs_post_common(spa, vd, FM_RESOURCE_REMOVED);
}

/*
 * The 'resource.fs.zfs.autoreplace' event is an internal signal that the pool
 * has the 'autoreplace' property set, and therefore any broken vdevs will be
 * handled by higher level logic, and no vdev fault should be generated.
 */
void
zfs_post_autoreplace(spa_t *spa, vdev_t *vd)
{
	zfs_post_common(spa, vd, FM_RESOURCE_AUTOREPLACE);
}

/*
 * The 'resource.fs.zfs.statechange' event is an internal signal that the
 * given vdev has transitioned its state to DEGRADED or HEALTHY.  This will
 * cause the retire agent to repair any outstanding fault management cases
 * open because the device was not found (fault.fs.zfs.device).
 */
void
zfs_post_state_change(spa_t *spa, vdev_t *vd)
{
	zfs_post_common(spa, vd, FM_RESOURCE_STATECHANGE);
}
