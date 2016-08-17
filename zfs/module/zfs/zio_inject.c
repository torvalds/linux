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
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2017, Intel Corporation.
 */

/*
 * ZFS fault injection
 *
 * To handle fault injection, we keep track of a series of zinject_record_t
 * structures which describe which logical block(s) should be injected with a
 * fault.  These are kept in a global list.  Each record corresponds to a given
 * spa_t and maintains a special hold on the spa_t so that it cannot be deleted
 * or exported while the injection record exists.
 *
 * Device level injection is done using the 'zi_guid' field.  If this is set, it
 * means that the error is destined for a particular device, not a piece of
 * data.
 *
 * This is a rather poor data structure and algorithm, but we don't expect more
 * than a few faults at any one time, so it should be sufficient for our needs.
 */

#include <sys/arc.h>
#include <sys/zio.h>
#include <sys/zfs_ioctl.h>
#include <sys/vdev_impl.h>
#include <sys/dmu_objset.h>
#include <sys/fs/zfs.h>

uint32_t zio_injection_enabled = 0;

/*
 * Data describing each zinject handler registered on the system, and
 * contains the list node linking the handler in the global zinject
 * handler list.
 */
typedef struct inject_handler {
	int			zi_id;
	spa_t			*zi_spa;
	zinject_record_t	zi_record;
	uint64_t		*zi_lanes;
	int			zi_next_lane;
	list_node_t		zi_link;
} inject_handler_t;

/*
 * List of all zinject handlers registered on the system, protected by
 * the inject_lock defined below.
 */
static list_t inject_handlers;

/*
 * This protects insertion into, and traversal of, the inject handler
 * list defined above; as well as the inject_delay_count. Any time a
 * handler is inserted or removed from the list, this lock should be
 * taken as a RW_WRITER; and any time traversal is done over the list
 * (without modification to it) this lock should be taken as a RW_READER.
 */
static krwlock_t inject_lock;

/*
 * This holds the number of zinject delay handlers that have been
 * registered on the system. It is protected by the inject_lock defined
 * above. Thus modifications to this count must be a RW_WRITER of the
 * inject_lock, and reads of this count must be (at least) a RW_READER
 * of the lock.
 */
static int inject_delay_count = 0;

/*
 * This lock is used only in zio_handle_io_delay(), refer to the comment
 * in that function for more details.
 */
static kmutex_t inject_delay_mtx;

/*
 * Used to assign unique identifying numbers to each new zinject handler.
 */
static int inject_next_id = 1;

/*
 * Test if the requested frequency was triggered
 */
static boolean_t
freq_triggered(uint32_t frequency)
{
	/*
	 * zero implies always (100%)
	 */
	if (frequency == 0)
		return (B_TRUE);

	/*
	 * Note: we still handle legacy (unscaled) frequecy values
	 */
	uint32_t maximum = (frequency <= 100) ? 100 : ZI_PERCENTAGE_MAX;

	return (spa_get_random(maximum) < frequency);
}

/*
 * Returns true if the given record matches the I/O in progress.
 */
static boolean_t
zio_match_handler(zbookmark_phys_t *zb, uint64_t type,
    zinject_record_t *record, int error)
{
	/*
	 * Check for a match against the MOS, which is based on type
	 */
	if (zb->zb_objset == DMU_META_OBJSET &&
	    record->zi_objset == DMU_META_OBJSET &&
	    record->zi_object == DMU_META_DNODE_OBJECT) {
		if (record->zi_type == DMU_OT_NONE ||
		    type == record->zi_type)
			return (freq_triggered(record->zi_freq));
		else
			return (B_FALSE);
	}

	/*
	 * Check for an exact match.
	 */
	if (zb->zb_objset == record->zi_objset &&
	    zb->zb_object == record->zi_object &&
	    zb->zb_level == record->zi_level &&
	    zb->zb_blkid >= record->zi_start &&
	    zb->zb_blkid <= record->zi_end &&
	    error == record->zi_error)
		return (freq_triggered(record->zi_freq));

	return (B_FALSE);
}

/*
 * Panic the system when a config change happens in the function
 * specified by tag.
 */
void
zio_handle_panic_injection(spa_t *spa, char *tag, uint64_t type)
{
	inject_handler_t *handler;

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		if (spa != handler->zi_spa)
			continue;

		if (handler->zi_record.zi_type == type &&
		    strcmp(tag, handler->zi_record.zi_func) == 0)
			panic("Panic requested in function %s\n", tag);
	}

	rw_exit(&inject_lock);
}

/*
 * Determine if the I/O in question should return failure.  Returns the errno
 * to be returned to the caller.
 */
int
zio_handle_fault_injection(zio_t *zio, int error)
{
	int ret = 0;
	inject_handler_t *handler;

	/*
	 * Ignore I/O not associated with any logical data.
	 */
	if (zio->io_logical == NULL)
		return (0);

	/*
	 * Currently, we only support fault injection on reads.
	 */
	if (zio->io_type != ZIO_TYPE_READ)
		return (0);

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		if (zio->io_spa != handler->zi_spa ||
		    handler->zi_record.zi_cmd != ZINJECT_DATA_FAULT)
			continue;

		/* If this handler matches, return EIO */
		if (zio_match_handler(&zio->io_logical->io_bookmark,
		    zio->io_bp ? BP_GET_TYPE(zio->io_bp) : DMU_OT_NONE,
		    &handler->zi_record, error)) {
			ret = error;
			break;
		}
	}

	rw_exit(&inject_lock);

	return (ret);
}

/*
 * Determine if the zio is part of a label update and has an injection
 * handler associated with that portion of the label. Currently, we
 * allow error injection in either the nvlist or the uberblock region of
 * of the vdev label.
 */
int
zio_handle_label_injection(zio_t *zio, int error)
{
	inject_handler_t *handler;
	vdev_t *vd = zio->io_vd;
	uint64_t offset = zio->io_offset;
	int label;
	int ret = 0;

	if (offset >= VDEV_LABEL_START_SIZE &&
	    offset < vd->vdev_psize - VDEV_LABEL_END_SIZE)
		return (0);

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {
		uint64_t start = handler->zi_record.zi_start;
		uint64_t end = handler->zi_record.zi_end;

		if (handler->zi_record.zi_cmd != ZINJECT_LABEL_FAULT)
			continue;

		/*
		 * The injection region is the relative offsets within a
		 * vdev label. We must determine the label which is being
		 * updated and adjust our region accordingly.
		 */
		label = vdev_label_number(vd->vdev_psize, offset);
		start = vdev_label_offset(vd->vdev_psize, label, start);
		end = vdev_label_offset(vd->vdev_psize, label, end);

		if (zio->io_vd->vdev_guid == handler->zi_record.zi_guid &&
		    (offset >= start && offset <= end)) {
			ret = error;
			break;
		}
	}
	rw_exit(&inject_lock);
	return (ret);
}


int
zio_handle_device_injection(vdev_t *vd, zio_t *zio, int error)
{
	inject_handler_t *handler;
	int ret = 0;

	/*
	 * We skip over faults in the labels unless it's during
	 * device open (i.e. zio == NULL).
	 */
	if (zio != NULL) {
		uint64_t offset = zio->io_offset;

		if (offset < VDEV_LABEL_START_SIZE ||
		    offset >= vd->vdev_psize - VDEV_LABEL_END_SIZE)
			return (0);
	}

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		if (handler->zi_record.zi_cmd != ZINJECT_DEVICE_FAULT)
			continue;

		if (vd->vdev_guid == handler->zi_record.zi_guid) {
			if (handler->zi_record.zi_failfast &&
			    (zio == NULL || (zio->io_flags &
			    (ZIO_FLAG_IO_RETRY | ZIO_FLAG_TRYHARD)))) {
				continue;
			}

			/* Handle type specific I/O failures */
			if (zio != NULL &&
			    handler->zi_record.zi_iotype != ZIO_TYPES &&
			    handler->zi_record.zi_iotype != zio->io_type)
				continue;

			if (handler->zi_record.zi_error == error) {
				/*
				 * limit error injection if requested
				 */
				if (!freq_triggered(handler->zi_record.zi_freq))
					continue;

				/*
				 * For a failed open, pretend like the device
				 * has gone away.
				 */
				if (error == ENXIO)
					vd->vdev_stat.vs_aux =
					    VDEV_AUX_OPEN_FAILED;

				/*
				 * Treat these errors as if they had been
				 * retried so that all the appropriate stats
				 * and FMA events are generated.
				 */
				if (!handler->zi_record.zi_failfast &&
				    zio != NULL)
					zio->io_flags |= ZIO_FLAG_IO_RETRY;

				ret = error;
				break;
			}
			if (handler->zi_record.zi_error == ENXIO) {
				ret = SET_ERROR(EIO);
				break;
			}
		}
	}

	rw_exit(&inject_lock);

	return (ret);
}

/*
 * Simulate hardware that ignores cache flushes.  For requested number
 * of seconds nix the actual writing to disk.
 */
void
zio_handle_ignored_writes(zio_t *zio)
{
	inject_handler_t *handler;

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		/* Ignore errors not destined for this pool */
		if (zio->io_spa != handler->zi_spa ||
		    handler->zi_record.zi_cmd != ZINJECT_IGNORED_WRITES)
			continue;

		/*
		 * Positive duration implies # of seconds, negative
		 * a number of txgs
		 */
		if (handler->zi_record.zi_timer == 0) {
			if (handler->zi_record.zi_duration > 0)
				handler->zi_record.zi_timer = ddi_get_lbolt64();
			else
				handler->zi_record.zi_timer = zio->io_txg;
		}

		/* Have a "problem" writing 60% of the time */
		if (spa_get_random(100) < 60)
			zio->io_pipeline &= ~ZIO_VDEV_IO_STAGES;
		break;
	}

	rw_exit(&inject_lock);
}

void
spa_handle_ignored_writes(spa_t *spa)
{
	inject_handler_t *handler;

	if (zio_injection_enabled == 0)
		return;

	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler)) {

		if (spa != handler->zi_spa ||
		    handler->zi_record.zi_cmd != ZINJECT_IGNORED_WRITES)
			continue;

		if (handler->zi_record.zi_duration > 0) {
			VERIFY(handler->zi_record.zi_timer == 0 ||
			    ddi_time_after64(
			    (int64_t)handler->zi_record.zi_timer +
			    handler->zi_record.zi_duration * hz,
			    ddi_get_lbolt64()));
		} else {
			/* duration is negative so the subtraction here adds */
			VERIFY(handler->zi_record.zi_timer == 0 ||
			    handler->zi_record.zi_timer -
			    handler->zi_record.zi_duration >=
			    spa_syncing_txg(spa));
		}
	}

	rw_exit(&inject_lock);
}

hrtime_t
zio_handle_io_delay(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	inject_handler_t *min_handler = NULL;
	hrtime_t min_target = 0;
	inject_handler_t *handler;
	hrtime_t idle;
	hrtime_t busy;
	hrtime_t target;

	rw_enter(&inject_lock, RW_READER);

	/*
	 * inject_delay_count is a subset of zio_injection_enabled that
	 * is only incremented for delay handlers. These checks are
	 * mainly added to remind the reader why we're not explicitly
	 * checking zio_injection_enabled like the other functions.
	 */
	IMPLY(inject_delay_count > 0, zio_injection_enabled > 0);
	IMPLY(zio_injection_enabled == 0, inject_delay_count == 0);

	/*
	 * If there aren't any inject delay handlers registered, then we
	 * can short circuit and simply return 0 here. A value of zero
	 * informs zio_delay_interrupt() that this request should not be
	 * delayed. This short circuit keeps us from acquiring the
	 * inject_delay_mutex unnecessarily.
	 */
	if (inject_delay_count == 0) {
		rw_exit(&inject_lock);
		return (0);
	}

	/*
	 * Each inject handler has a number of "lanes" associated with
	 * it. Each lane is able to handle requests independently of one
	 * another, and at a latency defined by the inject handler
	 * record's zi_timer field. Thus if a handler in configured with
	 * a single lane with a 10ms latency, it will delay requests
	 * such that only a single request is completed every 10ms. So,
	 * if more than one request is attempted per each 10ms interval,
	 * the average latency of the requests will be greater than
	 * 10ms; but if only a single request is submitted each 10ms
	 * interval the average latency will be 10ms.
	 *
	 * We need to acquire this mutex to prevent multiple concurrent
	 * threads being assigned to the same lane of a given inject
	 * handler. The mutex allows us to perform the following two
	 * operations atomically:
	 *
	 *	1. determine the minimum handler and minimum target
	 *	   value of all the possible handlers
	 *	2. update that minimum handler's lane array
	 *
	 * Without atomicity, two (or more) threads could pick the same
	 * lane in step (1), and then conflict with each other in step
	 * (2). This could allow a single lane handler to process
	 * multiple requests simultaneously, which shouldn't be possible.
	 */
	mutex_enter(&inject_delay_mtx);

	for (handler = list_head(&inject_handlers);
	    handler != NULL; handler = list_next(&inject_handlers, handler)) {
		if (handler->zi_record.zi_cmd != ZINJECT_DELAY_IO)
			continue;

		if (!freq_triggered(handler->zi_record.zi_freq))
			continue;

		if (vd->vdev_guid != handler->zi_record.zi_guid)
			continue;

		/*
		 * Defensive; should never happen as the array allocation
		 * occurs prior to inserting this handler on the list.
		 */
		ASSERT3P(handler->zi_lanes, !=, NULL);

		/*
		 * This should never happen, the zinject command should
		 * prevent a user from setting an IO delay with zero lanes.
		 */
		ASSERT3U(handler->zi_record.zi_nlanes, !=, 0);

		ASSERT3U(handler->zi_record.zi_nlanes, >,
		    handler->zi_next_lane);

		/*
		 * We want to issue this IO to the lane that will become
		 * idle the soonest, so we compare the soonest this
		 * specific handler can complete the IO with all other
		 * handlers, to find the lowest value of all possible
		 * lanes. We then use this lane to submit the request.
		 *
		 * Since each handler has a constant value for its
		 * delay, we can just use the "next" lane for that
		 * handler; as it will always be the lane with the
		 * lowest value for that particular handler (i.e. the
		 * lane that will become idle the soonest). This saves a
		 * scan of each handler's lanes array.
		 *
		 * There's two cases to consider when determining when
		 * this specific IO request should complete. If this
		 * lane is idle, we want to "submit" the request now so
		 * it will complete after zi_timer milliseconds. Thus,
		 * we set the target to now + zi_timer.
		 *
		 * If the lane is busy, we want this request to complete
		 * zi_timer milliseconds after the lane becomes idle.
		 * Since the 'zi_lanes' array holds the time at which
		 * each lane will become idle, we use that value to
		 * determine when this request should complete.
		 */
		idle = handler->zi_record.zi_timer + gethrtime();
		busy = handler->zi_record.zi_timer +
		    handler->zi_lanes[handler->zi_next_lane];
		target = MAX(idle, busy);

		if (min_handler == NULL) {
			min_handler = handler;
			min_target = target;
			continue;
		}

		ASSERT3P(min_handler, !=, NULL);
		ASSERT3U(min_target, !=, 0);

		/*
		 * We don't yet increment the "next lane" variable since
		 * we still might find a lower value lane in another
		 * handler during any remaining iterations. Once we're
		 * sure we've selected the absolute minimum, we'll claim
		 * the lane and increment the handler's "next lane"
		 * field below.
		 */

		if (target < min_target) {
			min_handler = handler;
			min_target = target;
		}
	}

	/*
	 * 'min_handler' will be NULL if no IO delays are registered for
	 * this vdev, otherwise it will point to the handler containing
	 * the lane that will become idle the soonest.
	 */
	if (min_handler != NULL) {
		ASSERT3U(min_target, !=, 0);
		min_handler->zi_lanes[min_handler->zi_next_lane] = min_target;

		/*
		 * If we've used all possible lanes for this handler,
		 * loop back and start using the first lane again;
		 * otherwise, just increment the lane index.
		 */
		min_handler->zi_next_lane = (min_handler->zi_next_lane + 1) %
		    min_handler->zi_record.zi_nlanes;
	}

	mutex_exit(&inject_delay_mtx);
	rw_exit(&inject_lock);

	return (min_target);
}

/*
 * Create a new handler for the given record.  We add it to the list, adding
 * a reference to the spa_t in the process.  We increment zio_injection_enabled,
 * which is the switch to trigger all fault injection.
 */
int
zio_inject_fault(char *name, int flags, int *id, zinject_record_t *record)
{
	inject_handler_t *handler;
	int error;
	spa_t *spa;

	/*
	 * If this is pool-wide metadata, make sure we unload the corresponding
	 * spa_t, so that the next attempt to load it will trigger the fault.
	 * We call spa_reset() to unload the pool appropriately.
	 */
	if (flags & ZINJECT_UNLOAD_SPA)
		if ((error = spa_reset(name)) != 0)
			return (error);

	if (record->zi_cmd == ZINJECT_DELAY_IO) {
		/*
		 * A value of zero for the number of lanes or for the
		 * delay time doesn't make sense.
		 */
		if (record->zi_timer == 0 || record->zi_nlanes == 0)
			return (SET_ERROR(EINVAL));

		/*
		 * The number of lanes is directly mapped to the size of
		 * an array used by the handler. Thus, to ensure the
		 * user doesn't trigger an allocation that's "too large"
		 * we cap the number of lanes here.
		 */
		if (record->zi_nlanes >= UINT16_MAX)
			return (SET_ERROR(EINVAL));
	}

	if (!(flags & ZINJECT_NULL)) {
		/*
		 * spa_inject_ref() will add an injection reference, which will
		 * prevent the pool from being removed from the namespace while
		 * still allowing it to be unloaded.
		 */
		if ((spa = spa_inject_addref(name)) == NULL)
			return (SET_ERROR(ENOENT));

		handler = kmem_alloc(sizeof (inject_handler_t), KM_SLEEP);

		handler->zi_spa = spa;
		handler->zi_record = *record;

		if (handler->zi_record.zi_cmd == ZINJECT_DELAY_IO) {
			handler->zi_lanes = kmem_zalloc(
			    sizeof (*handler->zi_lanes) *
			    handler->zi_record.zi_nlanes, KM_SLEEP);
			handler->zi_next_lane = 0;
		} else {
			handler->zi_lanes = NULL;
			handler->zi_next_lane = 0;
		}

		rw_enter(&inject_lock, RW_WRITER);

		/*
		 * We can't move this increment into the conditional
		 * above because we need to hold the RW_WRITER lock of
		 * inject_lock, and we don't want to hold that while
		 * allocating the handler's zi_lanes array.
		 */
		if (handler->zi_record.zi_cmd == ZINJECT_DELAY_IO) {
			ASSERT3S(inject_delay_count, >=, 0);
			inject_delay_count++;
			ASSERT3S(inject_delay_count, >, 0);
		}

		*id = handler->zi_id = inject_next_id++;
		list_insert_tail(&inject_handlers, handler);
		atomic_inc_32(&zio_injection_enabled);

		rw_exit(&inject_lock);
	}

	/*
	 * Flush the ARC, so that any attempts to read this data will end up
	 * going to the ZIO layer.  Note that this is a little overkill, but
	 * we don't have the necessary ARC interfaces to do anything else, and
	 * fault injection isn't a performance critical path.
	 */
	if (flags & ZINJECT_FLUSH_ARC)
		/*
		 * We must use FALSE to ensure arc_flush returns, since
		 * we're not preventing concurrent ARC insertions.
		 */
		arc_flush(NULL, FALSE);

	return (0);
}

/*
 * Returns the next record with an ID greater than that supplied to the
 * function.  Used to iterate over all handlers in the system.
 */
int
zio_inject_list_next(int *id, char *name, size_t buflen,
    zinject_record_t *record)
{
	inject_handler_t *handler;
	int ret;

	mutex_enter(&spa_namespace_lock);
	rw_enter(&inject_lock, RW_READER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler))
		if (handler->zi_id > *id)
			break;

	if (handler) {
		*record = handler->zi_record;
		*id = handler->zi_id;
		(void) strncpy(name, spa_name(handler->zi_spa), buflen);
		ret = 0;
	} else {
		ret = SET_ERROR(ENOENT);
	}

	rw_exit(&inject_lock);
	mutex_exit(&spa_namespace_lock);

	return (ret);
}

/*
 * Clear the fault handler with the given identifier, or return ENOENT if none
 * exists.
 */
int
zio_clear_fault(int id)
{
	inject_handler_t *handler;

	rw_enter(&inject_lock, RW_WRITER);

	for (handler = list_head(&inject_handlers); handler != NULL;
	    handler = list_next(&inject_handlers, handler))
		if (handler->zi_id == id)
			break;

	if (handler == NULL) {
		rw_exit(&inject_lock);
		return (SET_ERROR(ENOENT));
	}

	if (handler->zi_record.zi_cmd == ZINJECT_DELAY_IO) {
		ASSERT3S(inject_delay_count, >, 0);
		inject_delay_count--;
		ASSERT3S(inject_delay_count, >=, 0);
	}

	list_remove(&inject_handlers, handler);
	rw_exit(&inject_lock);

	if (handler->zi_record.zi_cmd == ZINJECT_DELAY_IO) {
		ASSERT3P(handler->zi_lanes, !=, NULL);
		kmem_free(handler->zi_lanes, sizeof (*handler->zi_lanes) *
		    handler->zi_record.zi_nlanes);
	} else {
		ASSERT3P(handler->zi_lanes, ==, NULL);
	}

	spa_inject_delref(handler->zi_spa);
	kmem_free(handler, sizeof (inject_handler_t));
	atomic_dec_32(&zio_injection_enabled);

	return (0);
}

void
zio_inject_init(void)
{
	rw_init(&inject_lock, NULL, RW_DEFAULT, NULL);
	mutex_init(&inject_delay_mtx, NULL, MUTEX_DEFAULT, NULL);
	list_create(&inject_handlers, sizeof (inject_handler_t),
	    offsetof(inject_handler_t, zi_link));
}

void
zio_inject_fini(void)
{
	list_destroy(&inject_handlers);
	mutex_destroy(&inject_delay_mtx);
	rw_destroy(&inject_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(zio_injection_enabled);
EXPORT_SYMBOL(zio_inject_fault);
EXPORT_SYMBOL(zio_inject_list_next);
EXPORT_SYMBOL(zio_clear_fault);
EXPORT_SYMBOL(zio_handle_fault_injection);
EXPORT_SYMBOL(zio_handle_device_injection);
EXPORT_SYMBOL(zio_handle_label_injection);
#endif
