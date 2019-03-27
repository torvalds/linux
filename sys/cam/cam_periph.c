/*-
 * Common functions for CAM "type" (peripheral) drivers.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998 Justin T. Gibbs.
 * Copyright (c) 1997, 1998, 1999, 2000 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/devicestat.h>
#include <sys/bus.h>
#include <sys/sbuf.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_queue.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_periph.h>
#include <cam/cam_debug.h>
#include <cam/cam_sim.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>

static	u_int		camperiphnextunit(struct periph_driver *p_drv,
					  u_int newunit, int wired,
					  path_id_t pathid, target_id_t target,
					  lun_id_t lun);
static	u_int		camperiphunit(struct periph_driver *p_drv,
				      path_id_t pathid, target_id_t target,
				      lun_id_t lun); 
static	void		camperiphdone(struct cam_periph *periph, 
					union ccb *done_ccb);
static  void		camperiphfree(struct cam_periph *periph);
static int		camperiphscsistatuserror(union ccb *ccb,
					        union ccb **orig_ccb,
						 cam_flags camflags,
						 u_int32_t sense_flags,
						 int *openings,
						 u_int32_t *relsim_flags,
						 u_int32_t *timeout,
						 u_int32_t  *action,
						 const char **action_string);
static	int		camperiphscsisenseerror(union ccb *ccb,
					        union ccb **orig_ccb,
					        cam_flags camflags,
					        u_int32_t sense_flags,
					        int *openings,
					        u_int32_t *relsim_flags,
					        u_int32_t *timeout,
					        u_int32_t *action,
					        const char **action_string);
static void		cam_periph_devctl_notify(union ccb *ccb);

static int nperiph_drivers;
static int initialized = 0;
struct periph_driver **periph_drivers;

static MALLOC_DEFINE(M_CAMPERIPH, "CAM periph", "CAM peripheral buffers");

static int periph_selto_delay = 1000;
TUNABLE_INT("kern.cam.periph_selto_delay", &periph_selto_delay);
static int periph_noresrc_delay = 500;
TUNABLE_INT("kern.cam.periph_noresrc_delay", &periph_noresrc_delay);
static int periph_busy_delay = 500;
TUNABLE_INT("kern.cam.periph_busy_delay", &periph_busy_delay);


void
periphdriver_register(void *data)
{
	struct periph_driver *drv = (struct periph_driver *)data;
	struct periph_driver **newdrivers, **old;
	int ndrivers;

again:
	ndrivers = nperiph_drivers + 2;
	newdrivers = malloc(sizeof(*newdrivers) * ndrivers, M_CAMPERIPH,
			    M_WAITOK);
	xpt_lock_buses();
	if (ndrivers != nperiph_drivers + 2) {
		/*
		 * Lost race against itself; go around.
		 */
		xpt_unlock_buses();
		free(newdrivers, M_CAMPERIPH);
		goto again;
	}
	if (periph_drivers)
		bcopy(periph_drivers, newdrivers,
		      sizeof(*newdrivers) * nperiph_drivers);
	newdrivers[nperiph_drivers] = drv;
	newdrivers[nperiph_drivers + 1] = NULL;
	old = periph_drivers;
	periph_drivers = newdrivers;
	nperiph_drivers++;
	xpt_unlock_buses();
	if (old)
		free(old, M_CAMPERIPH);
	/* If driver marked as early or it is late now, initialize it. */
	if (((drv->flags & CAM_PERIPH_DRV_EARLY) != 0 && initialized > 0) ||
	    initialized > 1)
		(*drv->init)();
}

int
periphdriver_unregister(void *data)
{
	struct periph_driver *drv = (struct periph_driver *)data;
	int error, n;

	/* If driver marked as early or it is late now, deinitialize it. */
	if (((drv->flags & CAM_PERIPH_DRV_EARLY) != 0 && initialized > 0) ||
	    initialized > 1) {
		if (drv->deinit == NULL) {
			printf("CAM periph driver '%s' doesn't have deinit.\n",
			    drv->driver_name);
			return (EOPNOTSUPP);
		}
		error = drv->deinit();
		if (error != 0)
			return (error);
	}

	xpt_lock_buses();
	for (n = 0; n < nperiph_drivers && periph_drivers[n] != drv; n++)
		;
	KASSERT(n < nperiph_drivers,
	    ("Periph driver '%s' was not registered", drv->driver_name));
	for (; n + 1 < nperiph_drivers; n++)
		periph_drivers[n] = periph_drivers[n + 1];
	periph_drivers[n + 1] = NULL;
	nperiph_drivers--;
	xpt_unlock_buses();
	return (0);
}

void
periphdriver_init(int level)
{
	int	i, early;

	initialized = max(initialized, level);
	for (i = 0; periph_drivers[i] != NULL; i++) {
		early = (periph_drivers[i]->flags & CAM_PERIPH_DRV_EARLY) ? 1 : 2;
		if (early == initialized)
			(*periph_drivers[i]->init)();
	}
}

cam_status
cam_periph_alloc(periph_ctor_t *periph_ctor,
		 periph_oninv_t *periph_oninvalidate,
		 periph_dtor_t *periph_dtor, periph_start_t *periph_start,
		 char *name, cam_periph_type type, struct cam_path *path,
		 ac_callback_t *ac_callback, ac_code code, void *arg)
{
	struct		periph_driver **p_drv;
	struct		cam_sim *sim;
	struct		cam_periph *periph;
	struct		cam_periph *cur_periph;
	path_id_t	path_id;
	target_id_t	target_id;
	lun_id_t	lun_id;
	cam_status	status;
	u_int		init_level;

	init_level = 0;
	/*
	 * Handle Hot-Plug scenarios.  If there is already a peripheral
	 * of our type assigned to this path, we are likely waiting for
	 * final close on an old, invalidated, peripheral.  If this is
	 * the case, queue up a deferred call to the peripheral's async
	 * handler.  If it looks like a mistaken re-allocation, complain.
	 */
	if ((periph = cam_periph_find(path, name)) != NULL) {

		if ((periph->flags & CAM_PERIPH_INVALID) != 0
		 && (periph->flags & CAM_PERIPH_NEW_DEV_FOUND) == 0) {
			periph->flags |= CAM_PERIPH_NEW_DEV_FOUND;
			periph->deferred_callback = ac_callback;
			periph->deferred_ac = code;
			return (CAM_REQ_INPROG);
		} else {
			printf("cam_periph_alloc: attempt to re-allocate "
			       "valid device %s%d rejected flags %#x "
			       "refcount %d\n", periph->periph_name,
			       periph->unit_number, periph->flags,
			       periph->refcount);
		}
		return (CAM_REQ_INVALID);
	}
	
	periph = (struct cam_periph *)malloc(sizeof(*periph), M_CAMPERIPH,
					     M_NOWAIT|M_ZERO);

	if (periph == NULL)
		return (CAM_RESRC_UNAVAIL);
	
	init_level++;


	sim = xpt_path_sim(path);
	path_id = xpt_path_path_id(path);
	target_id = xpt_path_target_id(path);
	lun_id = xpt_path_lun_id(path);
	periph->periph_start = periph_start;
	periph->periph_dtor = periph_dtor;
	periph->periph_oninval = periph_oninvalidate;
	periph->type = type;
	periph->periph_name = name;
	periph->scheduled_priority = CAM_PRIORITY_NONE;
	periph->immediate_priority = CAM_PRIORITY_NONE;
	periph->refcount = 1;		/* Dropped by invalidation. */
	periph->sim = sim;
	SLIST_INIT(&periph->ccb_list);
	status = xpt_create_path(&path, periph, path_id, target_id, lun_id);
	if (status != CAM_REQ_CMP)
		goto failure;
	periph->path = path;

	xpt_lock_buses();
	for (p_drv = periph_drivers; *p_drv != NULL; p_drv++) {
		if (strcmp((*p_drv)->driver_name, name) == 0)
			break;
	}
	if (*p_drv == NULL) {
		printf("cam_periph_alloc: invalid periph name '%s'\n", name);
		xpt_unlock_buses();
		xpt_free_path(periph->path);
		free(periph, M_CAMPERIPH);
		return (CAM_REQ_INVALID);
	}
	periph->unit_number = camperiphunit(*p_drv, path_id, target_id, lun_id);
	cur_periph = TAILQ_FIRST(&(*p_drv)->units);
	while (cur_periph != NULL
	    && cur_periph->unit_number < periph->unit_number)
		cur_periph = TAILQ_NEXT(cur_periph, unit_links);
	if (cur_periph != NULL) {
		KASSERT(cur_periph->unit_number != periph->unit_number, ("duplicate units on periph list"));
		TAILQ_INSERT_BEFORE(cur_periph, periph, unit_links);
	} else {
		TAILQ_INSERT_TAIL(&(*p_drv)->units, periph, unit_links);
		(*p_drv)->generation++;
	}
	xpt_unlock_buses();

	init_level++;

	status = xpt_add_periph(periph);
	if (status != CAM_REQ_CMP)
		goto failure;

	init_level++;
	CAM_DEBUG(periph->path, CAM_DEBUG_INFO, ("Periph created\n"));

	status = periph_ctor(periph, arg);

	if (status == CAM_REQ_CMP)
		init_level++;

failure:
	switch (init_level) {
	case 4:
		/* Initialized successfully */
		break;
	case 3:
		CAM_DEBUG(periph->path, CAM_DEBUG_INFO, ("Periph destroyed\n"));
		xpt_remove_periph(periph);
		/* FALLTHROUGH */
	case 2:
		xpt_lock_buses();
		TAILQ_REMOVE(&(*p_drv)->units, periph, unit_links);
		xpt_unlock_buses();
		xpt_free_path(periph->path);
		/* FALLTHROUGH */
	case 1:
		free(periph, M_CAMPERIPH);
		/* FALLTHROUGH */
	case 0:
		/* No cleanup to perform. */
		break;
	default:
		panic("%s: Unknown init level", __func__);
	}
	return(status);
}

/*
 * Find a peripheral structure with the specified path, target, lun, 
 * and (optionally) type.  If the name is NULL, this function will return
 * the first peripheral driver that matches the specified path.
 */
struct cam_periph *
cam_periph_find(struct cam_path *path, char *name)
{
	struct periph_driver **p_drv;
	struct cam_periph *periph;

	xpt_lock_buses();
	for (p_drv = periph_drivers; *p_drv != NULL; p_drv++) {

		if (name != NULL && (strcmp((*p_drv)->driver_name, name) != 0))
			continue;

		TAILQ_FOREACH(periph, &(*p_drv)->units, unit_links) {
			if (xpt_path_comp(periph->path, path) == 0) {
				xpt_unlock_buses();
				cam_periph_assert(periph, MA_OWNED);
				return(periph);
			}
		}
		if (name != NULL) {
			xpt_unlock_buses();
			return(NULL);
		}
	}
	xpt_unlock_buses();
	return(NULL);
}

/*
 * Find peripheral driver instances attached to the specified path.
 */
int
cam_periph_list(struct cam_path *path, struct sbuf *sb)
{
	struct sbuf local_sb;
	struct periph_driver **p_drv;
	struct cam_periph *periph;
	int count;
	int sbuf_alloc_len;

	sbuf_alloc_len = 16;
retry:
	sbuf_new(&local_sb, NULL, sbuf_alloc_len, SBUF_FIXEDLEN);
	count = 0;
	xpt_lock_buses();
	for (p_drv = periph_drivers; *p_drv != NULL; p_drv++) {

		TAILQ_FOREACH(periph, &(*p_drv)->units, unit_links) {
			if (xpt_path_comp(periph->path, path) != 0)
				continue;

			if (sbuf_len(&local_sb) != 0)
				sbuf_cat(&local_sb, ",");

			sbuf_printf(&local_sb, "%s%d", periph->periph_name,
				    periph->unit_number);

			if (sbuf_error(&local_sb) == ENOMEM) {
				sbuf_alloc_len *= 2;
				xpt_unlock_buses();
				sbuf_delete(&local_sb);
				goto retry;
			}
			count++;
		}
	}
	xpt_unlock_buses();
	sbuf_finish(&local_sb);
	sbuf_cpy(sb, sbuf_data(&local_sb));
	sbuf_delete(&local_sb);
	return (count);
}

int
cam_periph_acquire(struct cam_periph *periph)
{
	int status;

	if (periph == NULL)
		return (EINVAL);

	status = ENOENT;
	xpt_lock_buses();
	if ((periph->flags & CAM_PERIPH_INVALID) == 0) {
		periph->refcount++;
		status = 0;
	}
	xpt_unlock_buses();

	return (status);
}

void
cam_periph_doacquire(struct cam_periph *periph)
{

	xpt_lock_buses();
	KASSERT(periph->refcount >= 1,
	    ("cam_periph_doacquire() with refcount == %d", periph->refcount));
	periph->refcount++;
	xpt_unlock_buses();
}

void
cam_periph_release_locked_buses(struct cam_periph *periph)
{

	cam_periph_assert(periph, MA_OWNED);
	KASSERT(periph->refcount >= 1, ("periph->refcount >= 1"));
	if (--periph->refcount == 0)
		camperiphfree(periph);
}

void
cam_periph_release_locked(struct cam_periph *periph)
{

	if (periph == NULL)
		return;

	xpt_lock_buses();
	cam_periph_release_locked_buses(periph);
	xpt_unlock_buses();
}

void
cam_periph_release(struct cam_periph *periph)
{
	struct mtx *mtx;

	if (periph == NULL)
		return;
	
	cam_periph_assert(periph, MA_NOTOWNED);
	mtx = cam_periph_mtx(periph);
	mtx_lock(mtx);
	cam_periph_release_locked(periph);
	mtx_unlock(mtx);
}

/*
 * hold/unhold act as mutual exclusion for sections of the code that
 * need to sleep and want to make sure that other sections that
 * will interfere are held off. This only protects exclusive sections
 * from each other.
 */
int
cam_periph_hold(struct cam_periph *periph, int priority)
{
	int error;

	/*
	 * Increment the reference count on the peripheral
	 * while we wait for our lock attempt to succeed
	 * to ensure the peripheral doesn't disappear out
	 * from user us while we sleep.
	 */

	if (cam_periph_acquire(periph) != 0)
		return (ENXIO);

	cam_periph_assert(periph, MA_OWNED);
	while ((periph->flags & CAM_PERIPH_LOCKED) != 0) {
		periph->flags |= CAM_PERIPH_LOCK_WANTED;
		if ((error = cam_periph_sleep(periph, periph, priority,
		    "caplck", 0)) != 0) {
			cam_periph_release_locked(periph);
			return (error);
		}
		if (periph->flags & CAM_PERIPH_INVALID) {
			cam_periph_release_locked(periph);
			return (ENXIO);
		}
	}

	periph->flags |= CAM_PERIPH_LOCKED;
	return (0);
}

void
cam_periph_unhold(struct cam_periph *periph)
{

	cam_periph_assert(periph, MA_OWNED);

	periph->flags &= ~CAM_PERIPH_LOCKED;
	if ((periph->flags & CAM_PERIPH_LOCK_WANTED) != 0) {
		periph->flags &= ~CAM_PERIPH_LOCK_WANTED;
		wakeup(periph);
	}

	cam_periph_release_locked(periph);
}

/*
 * Look for the next unit number that is not currently in use for this
 * peripheral type starting at "newunit".  Also exclude unit numbers that
 * are reserved by for future "hardwiring" unless we already know that this
 * is a potential wired device.  Only assume that the device is "wired" the
 * first time through the loop since after that we'll be looking at unit
 * numbers that did not match a wiring entry.
 */
static u_int
camperiphnextunit(struct periph_driver *p_drv, u_int newunit, int wired,
		  path_id_t pathid, target_id_t target, lun_id_t lun)
{
	struct	cam_periph *periph;
	char	*periph_name;
	int	i, val, dunit, r;
	const char *dname, *strval;

	periph_name = p_drv->driver_name;
	for (;;newunit++) {

		for (periph = TAILQ_FIRST(&p_drv->units);
		     periph != NULL && periph->unit_number != newunit;
		     periph = TAILQ_NEXT(periph, unit_links))
			;

		if (periph != NULL && periph->unit_number == newunit) {
			if (wired != 0) {
				xpt_print(periph->path, "Duplicate Wired "
				    "Device entry!\n");
				xpt_print(periph->path, "Second device (%s "
				    "device at scbus%d target %d lun %d) will "
				    "not be wired\n", periph_name, pathid,
				    target, lun);
				wired = 0;
			}
			continue;
		}
		if (wired)
			break;

		/*
		 * Don't match entries like "da 4" as a wired down
		 * device, but do match entries like "da 4 target 5"
		 * or even "da 4 scbus 1". 
		 */
		i = 0;
		dname = periph_name;
		for (;;) {
			r = resource_find_dev(&i, dname, &dunit, NULL, NULL);
			if (r != 0)
				break;
			/* if no "target" and no specific scbus, skip */
			if (resource_int_value(dname, dunit, "target", &val) &&
			    (resource_string_value(dname, dunit, "at",&strval)||
			     strcmp(strval, "scbus") == 0))
				continue;
			if (newunit == dunit)
				break;
		}
		if (r != 0)
			break;
	}
	return (newunit);
}

static u_int
camperiphunit(struct periph_driver *p_drv, path_id_t pathid,
	      target_id_t target, lun_id_t lun)
{
	u_int	unit;
	int	wired, i, val, dunit;
	const char *dname, *strval;
	char	pathbuf[32], *periph_name;

	periph_name = p_drv->driver_name;
	snprintf(pathbuf, sizeof(pathbuf), "scbus%d", pathid);
	unit = 0;
	i = 0;
	dname = periph_name;
	for (wired = 0; resource_find_dev(&i, dname, &dunit, NULL, NULL) == 0;
	     wired = 0) {
		if (resource_string_value(dname, dunit, "at", &strval) == 0) {
			if (strcmp(strval, pathbuf) != 0)
				continue;
			wired++;
		}
		if (resource_int_value(dname, dunit, "target", &val) == 0) {
			if (val != target)
				continue;
			wired++;
		}
		if (resource_int_value(dname, dunit, "lun", &val) == 0) {
			if (val != lun)
				continue;
			wired++;
		}
		if (wired != 0) {
			unit = dunit;
			break;
		}
	}

	/*
	 * Either start from 0 looking for the next unit or from
	 * the unit number given in the resource config.  This way,
	 * if we have wildcard matches, we don't return the same
	 * unit number twice.
	 */
	unit = camperiphnextunit(p_drv, unit, wired, pathid, target, lun);

	return (unit);
}

void
cam_periph_invalidate(struct cam_periph *periph)
{

	cam_periph_assert(periph, MA_OWNED);
	/*
	 * We only call this routine the first time a peripheral is
	 * invalidated.
	 */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0)
		return;

	CAM_DEBUG(periph->path, CAM_DEBUG_INFO, ("Periph invalidated\n"));
	if ((periph->flags & CAM_PERIPH_ANNOUNCED) && !rebooting) {
		struct sbuf sb;
		char buffer[160];

		sbuf_new(&sb, buffer, 160, SBUF_FIXEDLEN);
		xpt_denounce_periph_sbuf(periph, &sb);
		sbuf_finish(&sb);
		sbuf_putbuf(&sb);
	}
	periph->flags |= CAM_PERIPH_INVALID;
	periph->flags &= ~CAM_PERIPH_NEW_DEV_FOUND;
	if (periph->periph_oninval != NULL)
		periph->periph_oninval(periph);
	cam_periph_release_locked(periph);
}

static void
camperiphfree(struct cam_periph *periph)
{
	struct periph_driver **p_drv;
	struct periph_driver *drv;

	cam_periph_assert(periph, MA_OWNED);
	KASSERT(periph->periph_allocating == 0, ("%s%d: freed while allocating",
	    periph->periph_name, periph->unit_number));
	for (p_drv = periph_drivers; *p_drv != NULL; p_drv++) {
		if (strcmp((*p_drv)->driver_name, periph->periph_name) == 0)
			break;
	}
	if (*p_drv == NULL) {
		printf("camperiphfree: attempt to free non-existant periph\n");
		return;
	}
	/*
	 * Cache a pointer to the periph_driver structure.  If a
	 * periph_driver is added or removed from the array (see
	 * periphdriver_register()) while we drop the toplogy lock
	 * below, p_drv may change.  This doesn't protect against this
	 * particular periph_driver going away.  That will require full
	 * reference counting in the periph_driver infrastructure.
	 */
	drv = *p_drv;

	/*
	 * We need to set this flag before dropping the topology lock, to
	 * let anyone who is traversing the list that this peripheral is
	 * about to be freed, and there will be no more reference count
	 * checks.
	 */
	periph->flags |= CAM_PERIPH_FREE;

	/*
	 * The peripheral destructor semantics dictate calling with only the
	 * SIM mutex held.  Since it might sleep, it should not be called
	 * with the topology lock held.
	 */
	xpt_unlock_buses();

	/*
	 * We need to call the peripheral destructor prior to removing the
	 * peripheral from the list.  Otherwise, we risk running into a
	 * scenario where the peripheral unit number may get reused
	 * (because it has been removed from the list), but some resources
	 * used by the peripheral are still hanging around.  In particular,
	 * the devfs nodes used by some peripherals like the pass(4) driver
	 * aren't fully cleaned up until the destructor is run.  If the
	 * unit number is reused before the devfs instance is fully gone,
	 * devfs will panic.
	 */
	if (periph->periph_dtor != NULL)
		periph->periph_dtor(periph);

	/*
	 * The peripheral list is protected by the topology lock.
	 */
	xpt_lock_buses();

	TAILQ_REMOVE(&drv->units, periph, unit_links);
	drv->generation++;

	xpt_remove_periph(periph);

	xpt_unlock_buses();
	if ((periph->flags & CAM_PERIPH_ANNOUNCED) && !rebooting)
		xpt_print(periph->path, "Periph destroyed\n");
	else
		CAM_DEBUG(periph->path, CAM_DEBUG_INFO, ("Periph destroyed\n"));

	if (periph->flags & CAM_PERIPH_NEW_DEV_FOUND) {
		union ccb ccb;
		void *arg;

		switch (periph->deferred_ac) {
		case AC_FOUND_DEVICE:
			ccb.ccb_h.func_code = XPT_GDEV_TYPE;
			xpt_setup_ccb(&ccb.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
			xpt_action(&ccb);
			arg = &ccb;
			break;
		case AC_PATH_REGISTERED:
			xpt_path_inq(&ccb.cpi, periph->path);
			arg = &ccb;
			break;
		default:
			arg = NULL;
			break;
		}
		periph->deferred_callback(NULL, periph->deferred_ac,
					  periph->path, arg);
	}
	xpt_free_path(periph->path);
	free(periph, M_CAMPERIPH);
	xpt_lock_buses();
}

/*
 * Map user virtual pointers into kernel virtual address space, so we can
 * access the memory.  This is now a generic function that centralizes most
 * of the sanity checks on the data flags, if any.
 * This also only works for up to MAXPHYS memory.  Since we use
 * buffers to map stuff in and out, we're limited to the buffer size.
 */
int
cam_periph_mapmem(union ccb *ccb, struct cam_periph_map_info *mapinfo,
    u_int maxmap)
{
	int numbufs, i, j;
	int flags[CAM_PERIPH_MAXMAPS];
	u_int8_t **data_ptrs[CAM_PERIPH_MAXMAPS];
	u_int32_t lengths[CAM_PERIPH_MAXMAPS];
	u_int32_t dirs[CAM_PERIPH_MAXMAPS];

	if (maxmap == 0)
		maxmap = DFLTPHYS;	/* traditional default */
	else if (maxmap > MAXPHYS)
		maxmap = MAXPHYS;	/* for safety */
	switch(ccb->ccb_h.func_code) {
	case XPT_DEV_MATCH:
		if (ccb->cdm.match_buf_len == 0) {
			printf("cam_periph_mapmem: invalid match buffer "
			       "length 0\n");
			return(EINVAL);
		}
		if (ccb->cdm.pattern_buf_len > 0) {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.patterns;
			lengths[0] = ccb->cdm.pattern_buf_len;
			dirs[0] = CAM_DIR_OUT;
			data_ptrs[1] = (u_int8_t **)&ccb->cdm.matches;
			lengths[1] = ccb->cdm.match_buf_len;
			dirs[1] = CAM_DIR_IN;
			numbufs = 2;
		} else {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.matches;
			lengths[0] = ccb->cdm.match_buf_len;
			dirs[0] = CAM_DIR_IN;
			numbufs = 1;
		}
		/*
		 * This request will not go to the hardware, no reason
		 * to be so strict. vmapbuf() is able to map up to MAXPHYS.
		 */
		maxmap = MAXPHYS;
		break;
	case XPT_SCSI_IO:
	case XPT_CONT_TARGET_IO:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return(0);
		if ((ccb->ccb_h.flags & CAM_DATA_MASK) != CAM_DATA_VADDR)
			return (EINVAL);
		data_ptrs[0] = &ccb->csio.data_ptr;
		lengths[0] = ccb->csio.dxfer_len;
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		numbufs = 1;
		break;
	case XPT_ATA_IO:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return(0);
		if ((ccb->ccb_h.flags & CAM_DATA_MASK) != CAM_DATA_VADDR)
			return (EINVAL);
		data_ptrs[0] = &ccb->ataio.data_ptr;
		lengths[0] = ccb->ataio.dxfer_len;
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		numbufs = 1;
		break;
	case XPT_MMC_IO:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return(0);
		/* Two mappings: one for cmd->data and one for cmd->data->data */
		data_ptrs[0] = (unsigned char **)&ccb->mmcio.cmd.data;
		lengths[0] = sizeof(struct mmc_data *);
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		data_ptrs[1] = (unsigned char **)&ccb->mmcio.cmd.data->data;
		lengths[1] = ccb->mmcio.cmd.data->len;
		dirs[1] = ccb->ccb_h.flags & CAM_DIR_MASK;
		numbufs = 2;
		break;
	case XPT_SMP_IO:
		data_ptrs[0] = &ccb->smpio.smp_request;
		lengths[0] = ccb->smpio.smp_request_len;
		dirs[0] = CAM_DIR_OUT;
		data_ptrs[1] = &ccb->smpio.smp_response;
		lengths[1] = ccb->smpio.smp_response_len;
		dirs[1] = CAM_DIR_IN;
		numbufs = 2;
		break;
	case XPT_NVME_IO:
	case XPT_NVME_ADMIN:
		if ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE)
			return (0);
		if ((ccb->ccb_h.flags & CAM_DATA_MASK) != CAM_DATA_VADDR)
			return (EINVAL);
		data_ptrs[0] = &ccb->nvmeio.data_ptr;
		lengths[0] = ccb->nvmeio.dxfer_len;
		dirs[0] = ccb->ccb_h.flags & CAM_DIR_MASK;
		numbufs = 1;
		break;
	case XPT_DEV_ADVINFO:
		if (ccb->cdai.bufsiz == 0)
			return (0);

		data_ptrs[0] = (uint8_t **)&ccb->cdai.buf;
		lengths[0] = ccb->cdai.bufsiz;
		dirs[0] = CAM_DIR_IN;
		numbufs = 1;

		/*
		 * This request will not go to the hardware, no reason
		 * to be so strict. vmapbuf() is able to map up to MAXPHYS.
		 */
		maxmap = MAXPHYS;
		break;
	default:
		return(EINVAL);
		break; /* NOTREACHED */
	}

	/*
	 * Check the transfer length and permissions first, so we don't
	 * have to unmap any previously mapped buffers.
	 */
	for (i = 0; i < numbufs; i++) {

		flags[i] = 0;

		/*
		 * The userland data pointer passed in may not be page
		 * aligned.  vmapbuf() truncates the address to a page
		 * boundary, so if the address isn't page aligned, we'll
		 * need enough space for the given transfer length, plus
		 * whatever extra space is necessary to make it to the page
		 * boundary.
		 */
		if ((lengths[i] +
		    (((vm_offset_t)(*data_ptrs[i])) & PAGE_MASK)) > maxmap){
			printf("cam_periph_mapmem: attempt to map %lu bytes, "
			       "which is greater than %lu\n",
			       (long)(lengths[i] +
			       (((vm_offset_t)(*data_ptrs[i])) & PAGE_MASK)),
			       (u_long)maxmap);
			return(E2BIG);
		}

		if (dirs[i] & CAM_DIR_OUT) {
			flags[i] = BIO_WRITE;
		}

		if (dirs[i] & CAM_DIR_IN) {
			flags[i] = BIO_READ;
		}

	}

	/*
	 * This keeps the kernel stack of current thread from getting
	 * swapped.  In low-memory situations where the kernel stack might
	 * otherwise get swapped out, this holds it and allows the thread
	 * to make progress and release the kernel mapped pages sooner.
	 *
	 * XXX KDM should I use P_NOSWAP instead?
	 */
	PHOLD(curproc);

	for (i = 0; i < numbufs; i++) {
		/*
		 * Get the buffer.
		 */
		mapinfo->bp[i] = uma_zalloc(pbuf_zone, M_WAITOK);

		/* put our pointer in the data slot */
		mapinfo->bp[i]->b_data = *data_ptrs[i];

		/* save the user's data address */
		mapinfo->bp[i]->b_caller1 = *data_ptrs[i];

		/* set the transfer length, we know it's < MAXPHYS */
		mapinfo->bp[i]->b_bufsize = lengths[i];

		/* set the direction */
		mapinfo->bp[i]->b_iocmd = flags[i];

		/*
		 * Map the buffer into kernel memory.
		 *
		 * Note that useracc() alone is not a  sufficient test.
		 * vmapbuf() can still fail due to a smaller file mapped
		 * into a larger area of VM, or if userland races against
		 * vmapbuf() after the useracc() check.
		 */
		if (vmapbuf(mapinfo->bp[i], 1) < 0) {
			for (j = 0; j < i; ++j) {
				*data_ptrs[j] = mapinfo->bp[j]->b_caller1;
				vunmapbuf(mapinfo->bp[j]);
				uma_zfree(pbuf_zone, mapinfo->bp[j]);
			}
			uma_zfree(pbuf_zone, mapinfo->bp[i]);
			PRELE(curproc);
			return(EACCES);
		}

		/* set our pointer to the new mapped area */
		*data_ptrs[i] = mapinfo->bp[i]->b_data;

		mapinfo->num_bufs_used++;
	}

	/*
	 * Now that we've gotten this far, change ownership to the kernel
	 * of the buffers so that we don't run afoul of returning to user
	 * space with locks (on the buffer) held.
	 */
	for (i = 0; i < numbufs; i++) {
		BUF_KERNPROC(mapinfo->bp[i]);
	}


	return(0);
}

/*
 * Unmap memory segments mapped into kernel virtual address space by
 * cam_periph_mapmem().
 */
void
cam_periph_unmapmem(union ccb *ccb, struct cam_periph_map_info *mapinfo)
{
	int numbufs, i;
	u_int8_t **data_ptrs[CAM_PERIPH_MAXMAPS];

	if (mapinfo->num_bufs_used <= 0) {
		/* nothing to free and the process wasn't held. */
		return;
	}

	switch (ccb->ccb_h.func_code) {
	case XPT_DEV_MATCH:
		numbufs = min(mapinfo->num_bufs_used, 2);

		if (numbufs == 1) {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.matches;
		} else {
			data_ptrs[0] = (u_int8_t **)&ccb->cdm.patterns;
			data_ptrs[1] = (u_int8_t **)&ccb->cdm.matches;
		}
		break;
	case XPT_SCSI_IO:
	case XPT_CONT_TARGET_IO:
		data_ptrs[0] = &ccb->csio.data_ptr;
		numbufs = min(mapinfo->num_bufs_used, 1);
		break;
	case XPT_ATA_IO:
		data_ptrs[0] = &ccb->ataio.data_ptr;
		numbufs = min(mapinfo->num_bufs_used, 1);
		break;
	case XPT_SMP_IO:
		numbufs = min(mapinfo->num_bufs_used, 2);
		data_ptrs[0] = &ccb->smpio.smp_request;
		data_ptrs[1] = &ccb->smpio.smp_response;
		break;
	case XPT_DEV_ADVINFO:
		numbufs = min(mapinfo->num_bufs_used, 1);
		data_ptrs[0] = (uint8_t **)&ccb->cdai.buf;
		break;
	case XPT_NVME_IO:
	case XPT_NVME_ADMIN:
		data_ptrs[0] = &ccb->nvmeio.data_ptr;
		numbufs = min(mapinfo->num_bufs_used, 1);
		break;
	default:
		/* allow ourselves to be swapped once again */
		PRELE(curproc);
		return;
		break; /* NOTREACHED */ 
	}

	for (i = 0; i < numbufs; i++) {
		/* Set the user's pointer back to the original value */
		*data_ptrs[i] = mapinfo->bp[i]->b_caller1;

		/* unmap the buffer */
		vunmapbuf(mapinfo->bp[i]);

		/* release the buffer */
		uma_zfree(pbuf_zone, mapinfo->bp[i]);
	}

	/* allow ourselves to be swapped once again */
	PRELE(curproc);
}

int
cam_periph_ioctl(struct cam_periph *periph, u_long cmd, caddr_t addr,
		 int (*error_routine)(union ccb *ccb, 
				      cam_flags camflags,
				      u_int32_t sense_flags))
{
	union ccb 	     *ccb;
	int 		     error;
	int		     found;

	error = found = 0;

	switch(cmd){
	case CAMGETPASSTHRU:
		ccb = cam_periph_getccb(periph, CAM_PRIORITY_NORMAL);
		xpt_setup_ccb(&ccb->ccb_h,
			      ccb->ccb_h.path,
			      CAM_PRIORITY_NORMAL);
		ccb->ccb_h.func_code = XPT_GDEVLIST;

		/*
		 * Basically, the point of this is that we go through
		 * getting the list of devices, until we find a passthrough
		 * device.  In the current version of the CAM code, the
		 * only way to determine what type of device we're dealing
		 * with is by its name.
		 */
		while (found == 0) {
			ccb->cgdl.index = 0;
			ccb->cgdl.status = CAM_GDEVLIST_MORE_DEVS;
			while (ccb->cgdl.status == CAM_GDEVLIST_MORE_DEVS) {

				/* we want the next device in the list */
				xpt_action(ccb);
				if (strncmp(ccb->cgdl.periph_name, 
				    "pass", 4) == 0){
					found = 1;
					break;
				}
			}
			if ((ccb->cgdl.status == CAM_GDEVLIST_LAST_DEVICE) &&
			    (found == 0)) {
				ccb->cgdl.periph_name[0] = '\0';
				ccb->cgdl.unit_number = 0;
				break;
			}
		}

		/* copy the result back out */	
		bcopy(ccb, addr, sizeof(union ccb));

		/* and release the ccb */
		xpt_release_ccb(ccb);

		break;
	default:
		error = ENOTTY;
		break;
	}
	return(error);
}

static void
cam_periph_done_panic(struct cam_periph *periph, union ccb *done_ccb)
{

	panic("%s: already done with ccb %p", __func__, done_ccb);
}

static void
cam_periph_done(struct cam_periph *periph, union ccb *done_ccb)
{

	/* Caller will release the CCB */
	xpt_path_assert(done_ccb->ccb_h.path, MA_OWNED);
	done_ccb->ccb_h.cbfcnp = cam_periph_done_panic;
	wakeup(&done_ccb->ccb_h.cbfcnp);
}

static void
cam_periph_ccbwait(union ccb *ccb)
{

	if ((ccb->ccb_h.func_code & XPT_FC_QUEUED) != 0) {
		while (ccb->ccb_h.cbfcnp != cam_periph_done_panic)
			xpt_path_sleep(ccb->ccb_h.path, &ccb->ccb_h.cbfcnp,
			    PRIBIO, "cbwait", 0);
	}
	KASSERT(ccb->ccb_h.pinfo.index == CAM_UNQUEUED_INDEX &&
	    (ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_INPROG,
	    ("%s: proceeding with incomplete ccb: ccb=%p, func_code=%#x, "
	     "status=%#x, index=%d", __func__, ccb, ccb->ccb_h.func_code,
	     ccb->ccb_h.status, ccb->ccb_h.pinfo.index));
}

/*
 * Dispatch a CCB and wait for it to complete.  If the CCB has set a
 * callback function (ccb->ccb_h.cbfcnp), it will be overwritten and lost.
 */
int
cam_periph_runccb(union ccb *ccb,
		  int (*error_routine)(union ccb *ccb,
				       cam_flags camflags,
				       u_int32_t sense_flags),
		  cam_flags camflags, u_int32_t sense_flags,
		  struct devstat *ds)
{
	struct bintime *starttime;
	struct bintime ltime;
	int error;
	bool must_poll;
	uint32_t timeout = 1;

	starttime = NULL;
	xpt_path_assert(ccb->ccb_h.path, MA_OWNED);
	KASSERT((ccb->ccb_h.flags & CAM_UNLOCKED) == 0,
	    ("%s: ccb=%p, func_code=%#x, flags=%#x", __func__, ccb,
	     ccb->ccb_h.func_code, ccb->ccb_h.flags));

	/*
	 * If the user has supplied a stats structure, and if we understand
	 * this particular type of ccb, record the transaction start.
	 */
	if (ds != NULL &&
	    (ccb->ccb_h.func_code == XPT_SCSI_IO ||
	    ccb->ccb_h.func_code == XPT_ATA_IO ||
	    ccb->ccb_h.func_code == XPT_NVME_IO)) {
		starttime = &ltime;
		binuptime(starttime);
		devstat_start_transaction(ds, starttime);
	}

	/*
	 * We must poll the I/O while we're dumping. The scheduler is normally
	 * stopped for dumping, except when we call doadump from ddb. While the
	 * scheduler is running in this case, we still need to poll the I/O to
	 * avoid sleeping waiting for the ccb to complete.
	 *
	 * A panic triggered dump stops the scheduler, any callback from the
	 * shutdown_post_sync event will run with the scheduler stopped, but
	 * before we're officially dumping. To avoid hanging in adashutdown
	 * initiated commands (or other similar situations), we have to test for
	 * either SCHEDULER_STOPPED() here as well.
	 *
	 * To avoid locking problems, dumping/polling callers must call
	 * without a periph lock held.
	 */
	must_poll = dumping || SCHEDULER_STOPPED();
	ccb->ccb_h.cbfcnp = cam_periph_done;

	/*
	 * If we're polling, then we need to ensure that we have ample resources
	 * in the periph.  cam_periph_error can reschedule the ccb by calling
	 * xpt_action and returning ERESTART, so we have to effect the polling
	 * in the do loop below.
	 */
	if (must_poll) {
		timeout = xpt_poll_setup(ccb);
	}

	if (timeout == 0) {
		ccb->ccb_h.status = CAM_RESRC_UNAVAIL;
		error = EBUSY;
	} else {
		xpt_action(ccb);
		do {
			if (must_poll) {
				xpt_pollwait(ccb, timeout);
				timeout = ccb->ccb_h.timeout * 10;
			} else {
				cam_periph_ccbwait(ccb);
			}
			if ((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)
				error = 0;
			else if (error_routine != NULL) {
				ccb->ccb_h.cbfcnp = cam_periph_done;
				error = (*error_routine)(ccb, camflags, sense_flags);
			} else
				error = 0;
		} while (error == ERESTART);
	}

	if ((ccb->ccb_h.status & CAM_DEV_QFRZN) != 0) {
		cam_release_devq(ccb->ccb_h.path,
				 /* relsim_flags */0,
				 /* openings */0,
				 /* timeout */0,
				 /* getcount_only */ FALSE);
		ccb->ccb_h.status &= ~CAM_DEV_QFRZN;
	}

	if (ds != NULL) {
		uint32_t bytes;
		devstat_tag_type tag;
		bool valid = true;

		if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
			bytes = ccb->csio.dxfer_len - ccb->csio.resid;
			tag = (devstat_tag_type)(ccb->csio.tag_action & 0x3);
		} else if (ccb->ccb_h.func_code == XPT_ATA_IO) {
			bytes = ccb->ataio.dxfer_len - ccb->ataio.resid;
			tag = (devstat_tag_type)0;
		} else if (ccb->ccb_h.func_code == XPT_NVME_IO) {
			bytes = ccb->nvmeio.dxfer_len; /* NB: resid no possible */
			tag = (devstat_tag_type)0;
		} else {
			valid = false;
		}
		if (valid)
			devstat_end_transaction(ds, bytes, tag,
			    ((ccb->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_NONE) ?
			    DEVSTAT_NO_DATA : (ccb->ccb_h.flags & CAM_DIR_OUT) ?
			    DEVSTAT_WRITE : DEVSTAT_READ, NULL, starttime);
	}

	return(error);
}

void
cam_freeze_devq(struct cam_path *path)
{
	struct ccb_hdr ccb_h;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("cam_freeze_devq\n"));
	xpt_setup_ccb(&ccb_h, path, /*priority*/1);
	ccb_h.func_code = XPT_NOOP;
	ccb_h.flags = CAM_DEV_QFREEZE;
	xpt_action((union ccb *)&ccb_h);
}

u_int32_t
cam_release_devq(struct cam_path *path, u_int32_t relsim_flags,
		 u_int32_t openings, u_int32_t arg,
		 int getcount_only)
{
	struct ccb_relsim crs;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("cam_release_devq(%u, %u, %u, %d)\n",
	    relsim_flags, openings, arg, getcount_only));
	xpt_setup_ccb(&crs.ccb_h, path, CAM_PRIORITY_NORMAL);
	crs.ccb_h.func_code = XPT_REL_SIMQ;
	crs.ccb_h.flags = getcount_only ? CAM_DEV_QFREEZE : 0;
	crs.release_flags = relsim_flags;
	crs.openings = openings;
	crs.release_timeout = arg;
	xpt_action((union ccb *)&crs);
	return (crs.qfrozen_cnt);
}

#define saved_ccb_ptr ppriv_ptr0
static void
camperiphdone(struct cam_periph *periph, union ccb *done_ccb)
{
	union ccb      *saved_ccb;
	cam_status	status;
	struct scsi_start_stop_unit *scsi_cmd;
	int		error = 0, error_code, sense_key, asc, ascq;

	scsi_cmd = (struct scsi_start_stop_unit *)
	    &done_ccb->csio.cdb_io.cdb_bytes;
	status = done_ccb->ccb_h.status;

	if ((status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		if (scsi_extract_sense_ccb(done_ccb,
		    &error_code, &sense_key, &asc, &ascq)) {
			/*
			 * If the error is "invalid field in CDB",
			 * and the load/eject flag is set, turn the
			 * flag off and try again.  This is just in
			 * case the drive in question barfs on the
			 * load eject flag.  The CAM code should set
			 * the load/eject flag by default for
			 * removable media.
			 */
			if ((scsi_cmd->opcode == START_STOP_UNIT) &&
			    ((scsi_cmd->how & SSS_LOEJ) != 0) &&
			     (asc == 0x24) && (ascq == 0x00)) {
				scsi_cmd->how &= ~SSS_LOEJ;
				if (status & CAM_DEV_QFRZN) {
					cam_release_devq(done_ccb->ccb_h.path,
					    0, 0, 0, 0);
					done_ccb->ccb_h.status &=
					    ~CAM_DEV_QFRZN;
				}
				xpt_action(done_ccb);
				goto out;
			}
		}
		error = cam_periph_error(done_ccb, 0,
		    SF_RETRY_UA | SF_NO_PRINT);
		if (error == ERESTART)
			goto out;
		if (done_ccb->ccb_h.status & CAM_DEV_QFRZN) {
			cam_release_devq(done_ccb->ccb_h.path, 0, 0, 0, 0);
			done_ccb->ccb_h.status &= ~CAM_DEV_QFRZN;
		}
	} else {
		/*
		 * If we have successfully taken a device from the not
		 * ready to ready state, re-scan the device and re-get
		 * the inquiry information.  Many devices (mostly disks)
		 * don't properly report their inquiry information unless
		 * they are spun up.
		 */
		if (scsi_cmd->opcode == START_STOP_UNIT)
			xpt_async(AC_INQ_CHANGED, done_ccb->ccb_h.path, NULL);
	}

	/*
	 * After recovery action(s) completed, return to the original CCB.
	 * If the recovery CCB has failed, considering its own possible
	 * retries and recovery, assume we are back in state where we have
	 * been originally, but without recovery hopes left.  In such case,
	 * after the final attempt below, we cancel any further retries,
	 * blocking by that also any new recovery attempts for this CCB,
	 * and the result will be the final one returned to the CCB owher.
	 */
	saved_ccb = (union ccb *)done_ccb->ccb_h.saved_ccb_ptr;
	bcopy(saved_ccb, done_ccb, sizeof(*done_ccb));
	xpt_free_ccb(saved_ccb);
	if (done_ccb->ccb_h.cbfcnp != camperiphdone)
		periph->flags &= ~CAM_PERIPH_RECOVERY_INPROG;
	if (error != 0)
		done_ccb->ccb_h.retry_count = 0;
	xpt_action(done_ccb);

out:
	/* Drop freeze taken due to CAM_DEV_QFREEZE flag set. */
	cam_release_devq(done_ccb->ccb_h.path, 0, 0, 0, 0);
}

/*
 * Generic Async Event handler.  Peripheral drivers usually
 * filter out the events that require personal attention,
 * and leave the rest to this function.
 */
void
cam_periph_async(struct cam_periph *periph, u_int32_t code,
		 struct cam_path *path, void *arg)
{
	switch (code) {
	case AC_LOST_DEVICE:
		cam_periph_invalidate(periph);
		break; 
	default:
		break;
	}
}

void
cam_periph_bus_settle(struct cam_periph *periph, u_int bus_settle)
{
	struct ccb_getdevstats cgds;

	xpt_setup_ccb(&cgds.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
	cgds.ccb_h.func_code = XPT_GDEV_STATS;
	xpt_action((union ccb *)&cgds);
	cam_periph_freeze_after_event(periph, &cgds.last_reset, bus_settle);
}

void
cam_periph_freeze_after_event(struct cam_periph *periph,
			      struct timeval* event_time, u_int duration_ms)
{
	struct timeval delta;
	struct timeval duration_tv;

	if (!timevalisset(event_time))
		return;

	microtime(&delta);
	timevalsub(&delta, event_time);
	duration_tv.tv_sec = duration_ms / 1000;
	duration_tv.tv_usec = (duration_ms % 1000) * 1000;
	if (timevalcmp(&delta, &duration_tv, <)) {
		timevalsub(&duration_tv, &delta);

		duration_ms = duration_tv.tv_sec * 1000;
		duration_ms += duration_tv.tv_usec / 1000;
		cam_freeze_devq(periph->path); 
		cam_release_devq(periph->path,
				RELSIM_RELEASE_AFTER_TIMEOUT,
				/*reduction*/0,
				/*timeout*/duration_ms,
				/*getcount_only*/0);
	}

}

static int
camperiphscsistatuserror(union ccb *ccb, union ccb **orig_ccb,
    cam_flags camflags, u_int32_t sense_flags,
    int *openings, u_int32_t *relsim_flags,
    u_int32_t *timeout, u_int32_t *action, const char **action_string)
{
	int error;

	switch (ccb->csio.scsi_status) {
	case SCSI_STATUS_OK:
	case SCSI_STATUS_COND_MET:
	case SCSI_STATUS_INTERMED:
	case SCSI_STATUS_INTERMED_COND_MET:
		error = 0;
		break;
	case SCSI_STATUS_CMD_TERMINATED:
	case SCSI_STATUS_CHECK_COND:
		error = camperiphscsisenseerror(ccb, orig_ccb,
					        camflags,
					        sense_flags,
					        openings,
					        relsim_flags,
					        timeout,
					        action,
					        action_string);
		break;
	case SCSI_STATUS_QUEUE_FULL:
	{
		/* no decrement */
		struct ccb_getdevstats cgds;

		/*
		 * First off, find out what the current
		 * transaction counts are.
		 */
		xpt_setup_ccb(&cgds.ccb_h,
			      ccb->ccb_h.path,
			      CAM_PRIORITY_NORMAL);
		cgds.ccb_h.func_code = XPT_GDEV_STATS;
		xpt_action((union ccb *)&cgds);

		/*
		 * If we were the only transaction active, treat
		 * the QUEUE FULL as if it were a BUSY condition.
		 */
		if (cgds.dev_active != 0) {
			int total_openings;

			/*
		 	 * Reduce the number of openings to
			 * be 1 less than the amount it took
			 * to get a queue full bounded by the
			 * minimum allowed tag count for this
			 * device.
		 	 */
			total_openings = cgds.dev_active + cgds.dev_openings;
			*openings = cgds.dev_active;
			if (*openings < cgds.mintags)
				*openings = cgds.mintags;
			if (*openings < total_openings)
				*relsim_flags = RELSIM_ADJUST_OPENINGS;
			else {
				/*
				 * Some devices report queue full for
				 * temporary resource shortages.  For
				 * this reason, we allow a minimum
				 * tag count to be entered via a
				 * quirk entry to prevent the queue
				 * count on these devices from falling
				 * to a pessimisticly low value.  We
				 * still wait for the next successful
				 * completion, however, before queueing
				 * more transactions to the device.
				 */
				*relsim_flags = RELSIM_RELEASE_AFTER_CMDCMPLT;
			}
			*timeout = 0;
			error = ERESTART;
			*action &= ~SSQ_PRINT_SENSE;
			break;
		}
		/* FALLTHROUGH */
	}
	case SCSI_STATUS_BUSY:
		/*
		 * Restart the queue after either another
		 * command completes or a 1 second timeout.
		 */
		if ((sense_flags & SF_RETRY_BUSY) != 0 ||
		    (ccb->ccb_h.retry_count--) > 0) {
			error = ERESTART;
			*relsim_flags = RELSIM_RELEASE_AFTER_TIMEOUT
				      | RELSIM_RELEASE_AFTER_CMDCMPLT;
			*timeout = 1000;
		} else {
			error = EIO;
		}
		break;
	case SCSI_STATUS_RESERV_CONFLICT:
	default:
		error = EIO;
		break;
	}
	return (error);
}

static int
camperiphscsisenseerror(union ccb *ccb, union ccb **orig,
    cam_flags camflags, u_int32_t sense_flags,
    int *openings, u_int32_t *relsim_flags,
    u_int32_t *timeout, u_int32_t *action, const char **action_string)
{
	struct cam_periph *periph;
	union ccb *orig_ccb = ccb;
	int error, recoveryccb;

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
	if (ccb->ccb_h.func_code == XPT_SCSI_IO && ccb->csio.bio != NULL)
		biotrack(ccb->csio.bio, __func__);
#endif

	periph = xpt_path_periph(ccb->ccb_h.path);
	recoveryccb = (ccb->ccb_h.cbfcnp == camperiphdone);
	if ((periph->flags & CAM_PERIPH_RECOVERY_INPROG) && !recoveryccb) {
		/*
		 * If error recovery is already in progress, don't attempt
		 * to process this error, but requeue it unconditionally
		 * and attempt to process it once error recovery has
		 * completed.  This failed command is probably related to
		 * the error that caused the currently active error recovery
		 * action so our  current recovery efforts should also
		 * address this command.  Be aware that the error recovery
		 * code assumes that only one recovery action is in progress
		 * on a particular peripheral instance at any given time
		 * (e.g. only one saved CCB for error recovery) so it is
		 * imperitive that we don't violate this assumption.
		 */
		error = ERESTART;
		*action &= ~SSQ_PRINT_SENSE;
	} else {
		scsi_sense_action err_action;
		struct ccb_getdev cgd;

		/*
		 * Grab the inquiry data for this device.
		 */
		xpt_setup_ccb(&cgd.ccb_h, ccb->ccb_h.path, CAM_PRIORITY_NORMAL);
		cgd.ccb_h.func_code = XPT_GDEV_TYPE;
		xpt_action((union ccb *)&cgd);

		err_action = scsi_error_action(&ccb->csio, &cgd.inq_data,
		    sense_flags);
		error = err_action & SS_ERRMASK;

		/*
		 * Do not autostart sequential access devices
		 * to avoid unexpected tape loading.
		 */
		if ((err_action & SS_MASK) == SS_START &&
		    SID_TYPE(&cgd.inq_data) == T_SEQUENTIAL) {
			*action_string = "Will not autostart a "
			    "sequential access device";
			goto sense_error_done;
		}

		/*
		 * Avoid recovery recursion if recovery action is the same.
		 */
		if ((err_action & SS_MASK) >= SS_START && recoveryccb) {
			if (((err_action & SS_MASK) == SS_START &&
			     ccb->csio.cdb_io.cdb_bytes[0] == START_STOP_UNIT) ||
			    ((err_action & SS_MASK) == SS_TUR &&
			     (ccb->csio.cdb_io.cdb_bytes[0] == TEST_UNIT_READY))) {
				err_action = SS_RETRY|SSQ_DECREMENT_COUNT|EIO;
				*relsim_flags = RELSIM_RELEASE_AFTER_TIMEOUT;
				*timeout = 500;
			}
		}

		/*
		 * If the recovery action will consume a retry,
		 * make sure we actually have retries available.
		 */
		if ((err_action & SSQ_DECREMENT_COUNT) != 0) {
		 	if (ccb->ccb_h.retry_count > 0 &&
			    (periph->flags & CAM_PERIPH_INVALID) == 0)
		 		ccb->ccb_h.retry_count--;
			else {
				*action_string = "Retries exhausted";
				goto sense_error_done;
			}
		}

		if ((err_action & SS_MASK) >= SS_START) {
			/*
			 * Do common portions of commands that
			 * use recovery CCBs.
			 */
			orig_ccb = xpt_alloc_ccb_nowait();
			if (orig_ccb == NULL) {
				*action_string = "Can't allocate recovery CCB";
				goto sense_error_done;
			}
			/*
			 * Clear freeze flag for original request here, as
			 * this freeze will be dropped as part of ERESTART.
			 */
			ccb->ccb_h.status &= ~CAM_DEV_QFRZN;
			bcopy(ccb, orig_ccb, sizeof(*orig_ccb));
		}

		switch (err_action & SS_MASK) {
		case SS_NOP:
			*action_string = "No recovery action needed";
			error = 0;
			break;
		case SS_RETRY:
			*action_string = "Retrying command (per sense data)";
			error = ERESTART;
			break;
		case SS_FAIL:
			*action_string = "Unretryable error";
			break;
		case SS_START:
		{
			int le;

			/*
			 * Send a start unit command to the device, and
			 * then retry the command.
			 */
			*action_string = "Attempting to start unit";
			periph->flags |= CAM_PERIPH_RECOVERY_INPROG;

			/*
			 * Check for removable media and set
			 * load/eject flag appropriately.
			 */
			if (SID_IS_REMOVABLE(&cgd.inq_data))
				le = TRUE;
			else
				le = FALSE;

			scsi_start_stop(&ccb->csio,
					/*retries*/1,
					camperiphdone,
					MSG_SIMPLE_Q_TAG,
					/*start*/TRUE,
					/*load/eject*/le,
					/*immediate*/FALSE,
					SSD_FULL_SIZE,
					/*timeout*/50000);
			break;
		}
		case SS_TUR:
		{
			/*
			 * Send a Test Unit Ready to the device.
			 * If the 'many' flag is set, we send 120
			 * test unit ready commands, one every half 
			 * second.  Otherwise, we just send one TUR.
			 * We only want to do this if the retry 
			 * count has not been exhausted.
			 */
			int retries;

			if ((err_action & SSQ_MANY) != 0) {
				*action_string = "Polling device for readiness";
				retries = 120;
			} else {
				*action_string = "Testing device for readiness";
				retries = 1;
			}
			periph->flags |= CAM_PERIPH_RECOVERY_INPROG;
			scsi_test_unit_ready(&ccb->csio,
					     retries,
					     camperiphdone,
					     MSG_SIMPLE_Q_TAG,
					     SSD_FULL_SIZE,
					     /*timeout*/5000);

			/*
			 * Accomplish our 500ms delay by deferring
			 * the release of our device queue appropriately.
			 */
			*relsim_flags = RELSIM_RELEASE_AFTER_TIMEOUT;
			*timeout = 500;
			break;
		}
		default:
			panic("Unhandled error action %x", err_action);
		}
		
		if ((err_action & SS_MASK) >= SS_START) {
			/*
			 * Drop the priority, so that the recovery
			 * CCB is the first to execute.  Freeze the queue
			 * after this command is sent so that we can
			 * restore the old csio and have it queued in
			 * the proper order before we release normal 
			 * transactions to the device.
			 */
			ccb->ccb_h.pinfo.priority--;
			ccb->ccb_h.flags |= CAM_DEV_QFREEZE;
			ccb->ccb_h.saved_ccb_ptr = orig_ccb;
			error = ERESTART;
			*orig = orig_ccb;
		}

sense_error_done:
		*action = err_action;
	}
	return (error);
}

/*
 * Generic error handler.  Peripheral drivers usually filter
 * out the errors that they handle in a unique manner, then
 * call this function.
 */
int
cam_periph_error(union ccb *ccb, cam_flags camflags,
		 u_int32_t sense_flags)
{
	struct cam_path *newpath;
	union ccb  *orig_ccb, *scan_ccb;
	struct cam_periph *periph;
	const char *action_string;
	cam_status  status;
	int	    frozen, error, openings, devctl_err;
	u_int32_t   action, relsim_flags, timeout;

	action = SSQ_PRINT_SENSE;
	periph = xpt_path_periph(ccb->ccb_h.path);
	action_string = NULL;
	status = ccb->ccb_h.status;
	frozen = (status & CAM_DEV_QFRZN) != 0;
	status &= CAM_STATUS_MASK;
	devctl_err = openings = relsim_flags = timeout = 0;
	orig_ccb = ccb;

	/* Filter the errors that should be reported via devctl */
	switch (ccb->ccb_h.status & CAM_STATUS_MASK) {
	case CAM_CMD_TIMEOUT:
	case CAM_REQ_ABORTED:
	case CAM_REQ_CMP_ERR:
	case CAM_REQ_TERMIO:
	case CAM_UNREC_HBA_ERROR:
	case CAM_DATA_RUN_ERR:
	case CAM_SCSI_STATUS_ERROR:
	case CAM_ATA_STATUS_ERROR:
	case CAM_SMP_STATUS_ERROR:
		devctl_err++;
		break;
	default:
		break;
	}

	switch (status) {
	case CAM_REQ_CMP:
		error = 0;
		action &= ~SSQ_PRINT_SENSE;
		break;
	case CAM_SCSI_STATUS_ERROR:
		error = camperiphscsistatuserror(ccb, &orig_ccb,
		    camflags, sense_flags, &openings, &relsim_flags,
		    &timeout, &action, &action_string);
		break;
	case CAM_AUTOSENSE_FAIL:
		error = EIO;	/* we have to kill the command */
		break;
	case CAM_UA_ABORT:
	case CAM_UA_TERMIO:
	case CAM_MSG_REJECT_REC:
		/* XXX Don't know that these are correct */
		error = EIO;
		break;
	case CAM_SEL_TIMEOUT:
		if ((camflags & CAM_RETRY_SELTO) != 0) {
			if (ccb->ccb_h.retry_count > 0 &&
			    (periph->flags & CAM_PERIPH_INVALID) == 0) {
				ccb->ccb_h.retry_count--;
				error = ERESTART;

				/*
				 * Wait a bit to give the device
				 * time to recover before we try again.
				 */
				relsim_flags = RELSIM_RELEASE_AFTER_TIMEOUT;
				timeout = periph_selto_delay;
				break;
			}
			action_string = "Retries exhausted";
		}
		/* FALLTHROUGH */
	case CAM_DEV_NOT_THERE:
		error = ENXIO;
		action = SSQ_LOST;
		break;
	case CAM_REQ_INVALID:
	case CAM_PATH_INVALID:
	case CAM_NO_HBA:
	case CAM_PROVIDE_FAIL:
	case CAM_REQ_TOO_BIG:
	case CAM_LUN_INVALID:
	case CAM_TID_INVALID:
	case CAM_FUNC_NOTAVAIL:
		error = EINVAL;
		break;
	case CAM_SCSI_BUS_RESET:
	case CAM_BDR_SENT:
		/*
		 * Commands that repeatedly timeout and cause these
		 * kinds of error recovery actions, should return
		 * CAM_CMD_TIMEOUT, which allows us to safely assume
		 * that this command was an innocent bystander to
		 * these events and should be unconditionally
		 * retried.
		 */
	case CAM_REQUEUE_REQ:
		/* Unconditional requeue if device is still there */
		if (periph->flags & CAM_PERIPH_INVALID) {
			action_string = "Periph was invalidated";
			error = EIO;
		} else if (sense_flags & SF_NO_RETRY) {
			error = EIO;
			action_string = "Retry was blocked";
		} else {
			error = ERESTART;
			action &= ~SSQ_PRINT_SENSE;
		}
		break;
	case CAM_RESRC_UNAVAIL:
		/* Wait a bit for the resource shortage to abate. */
		timeout = periph_noresrc_delay;
		/* FALLTHROUGH */
	case CAM_BUSY:
		if (timeout == 0) {
			/* Wait a bit for the busy condition to abate. */
			timeout = periph_busy_delay;
		}
		relsim_flags = RELSIM_RELEASE_AFTER_TIMEOUT;
		/* FALLTHROUGH */
	case CAM_ATA_STATUS_ERROR:
	case CAM_REQ_CMP_ERR:
	case CAM_CMD_TIMEOUT:
	case CAM_UNEXP_BUSFREE:
	case CAM_UNCOR_PARITY:
	case CAM_DATA_RUN_ERR:
	default:
		if (periph->flags & CAM_PERIPH_INVALID) {
			error = EIO;
			action_string = "Periph was invalidated";
		} else if (ccb->ccb_h.retry_count == 0) {
			error = EIO;
			action_string = "Retries exhausted";
		} else if (sense_flags & SF_NO_RETRY) {
			error = EIO;
			action_string = "Retry was blocked";
		} else {
			ccb->ccb_h.retry_count--;
			error = ERESTART;
		}
		break;
	}

	if ((sense_flags & SF_PRINT_ALWAYS) ||
	    CAM_DEBUGGED(ccb->ccb_h.path, CAM_DEBUG_INFO))
		action |= SSQ_PRINT_SENSE;
	else if (sense_flags & SF_NO_PRINT)
		action &= ~SSQ_PRINT_SENSE;
	if ((action & SSQ_PRINT_SENSE) != 0)
		cam_error_print(orig_ccb, CAM_ESF_ALL, CAM_EPF_ALL);
	if (error != 0 && (action & SSQ_PRINT_SENSE) != 0) {
		if (error != ERESTART) {
			if (action_string == NULL)
				action_string = "Unretryable error";
			xpt_print(ccb->ccb_h.path, "Error %d, %s\n",
			    error, action_string);
		} else if (action_string != NULL)
			xpt_print(ccb->ccb_h.path, "%s\n", action_string);
		else {
			xpt_print(ccb->ccb_h.path,
			    "Retrying command, %d more tries remain\n",
			    ccb->ccb_h.retry_count);
		}
	}

	if (devctl_err && (error != 0 || (action & SSQ_PRINT_SENSE) != 0))
		cam_periph_devctl_notify(orig_ccb);

	if ((action & SSQ_LOST) != 0) {
		lun_id_t lun_id;

		/*
		 * For a selection timeout, we consider all of the LUNs on
		 * the target to be gone.  If the status is CAM_DEV_NOT_THERE,
		 * then we only get rid of the device(s) specified by the
		 * path in the original CCB.
		 */
		if (status == CAM_SEL_TIMEOUT)
			lun_id = CAM_LUN_WILDCARD;
		else
			lun_id = xpt_path_lun_id(ccb->ccb_h.path);

		/* Should we do more if we can't create the path?? */
		if (xpt_create_path(&newpath, periph,
				    xpt_path_path_id(ccb->ccb_h.path),
				    xpt_path_target_id(ccb->ccb_h.path),
				    lun_id) == CAM_REQ_CMP) {

			/*
			 * Let peripheral drivers know that this
			 * device has gone away.
			 */
			xpt_async(AC_LOST_DEVICE, newpath, NULL);
			xpt_free_path(newpath);
		}
	}

	/* Broadcast UNIT ATTENTIONs to all periphs. */
	if ((action & SSQ_UA) != 0)
		xpt_async(AC_UNIT_ATTENTION, orig_ccb->ccb_h.path, orig_ccb);

	/* Rescan target on "Reported LUNs data has changed" */
	if ((action & SSQ_RESCAN) != 0) {
		if (xpt_create_path(&newpath, NULL,
				    xpt_path_path_id(ccb->ccb_h.path),
				    xpt_path_target_id(ccb->ccb_h.path),
				    CAM_LUN_WILDCARD) == CAM_REQ_CMP) {

			scan_ccb = xpt_alloc_ccb_nowait();
			if (scan_ccb != NULL) {
				scan_ccb->ccb_h.path = newpath;
				scan_ccb->ccb_h.func_code = XPT_SCAN_TGT;
				scan_ccb->crcn.flags = 0;
				xpt_rescan(scan_ccb);
			} else {
				xpt_print(newpath,
				    "Can't allocate CCB to rescan target\n");
				xpt_free_path(newpath);
			}
		}
	}

	/* Attempt a retry */
	if (error == ERESTART || error == 0) {
		if (frozen != 0)
			ccb->ccb_h.status &= ~CAM_DEV_QFRZN;
		if (error == ERESTART)
			xpt_action(ccb);
		if (frozen != 0)
			cam_release_devq(ccb->ccb_h.path,
					 relsim_flags,
					 openings,
					 timeout,
					 /*getcount_only*/0);
	}

	return (error);
}

#define CAM_PERIPH_DEVD_MSG_SIZE	256

static void
cam_periph_devctl_notify(union ccb *ccb)
{
	struct cam_periph *periph;
	struct ccb_getdev *cgd;
	struct sbuf sb;
	int serr, sk, asc, ascq;
	char *sbmsg, *type;

	sbmsg = malloc(CAM_PERIPH_DEVD_MSG_SIZE, M_CAMPERIPH, M_NOWAIT);
	if (sbmsg == NULL)
		return;

	sbuf_new(&sb, sbmsg, CAM_PERIPH_DEVD_MSG_SIZE, SBUF_FIXEDLEN);

	periph = xpt_path_periph(ccb->ccb_h.path);
	sbuf_printf(&sb, "device=%s%d ", periph->periph_name,
	    periph->unit_number);

	sbuf_printf(&sb, "serial=\"");
	if ((cgd = (struct ccb_getdev *)xpt_alloc_ccb_nowait()) != NULL) {
		xpt_setup_ccb(&cgd->ccb_h, ccb->ccb_h.path,
		    CAM_PRIORITY_NORMAL);
		cgd->ccb_h.func_code = XPT_GDEV_TYPE;
		xpt_action((union ccb *)cgd);

		if (cgd->ccb_h.status == CAM_REQ_CMP)
			sbuf_bcat(&sb, cgd->serial_num, cgd->serial_num_len);
		xpt_free_ccb((union ccb *)cgd);
	}
	sbuf_printf(&sb, "\" ");
	sbuf_printf(&sb, "cam_status=\"0x%x\" ", ccb->ccb_h.status);

	switch (ccb->ccb_h.status & CAM_STATUS_MASK) {
	case CAM_CMD_TIMEOUT:
		sbuf_printf(&sb, "timeout=%d ", ccb->ccb_h.timeout);
		type = "timeout";
		break;
	case CAM_SCSI_STATUS_ERROR:
		sbuf_printf(&sb, "scsi_status=%d ", ccb->csio.scsi_status);
		if (scsi_extract_sense_ccb(ccb, &serr, &sk, &asc, &ascq))
			sbuf_printf(&sb, "scsi_sense=\"%02x %02x %02x %02x\" ",
			    serr, sk, asc, ascq);
		type = "error";
		break;
	case CAM_ATA_STATUS_ERROR:
		sbuf_printf(&sb, "RES=\"");
		ata_res_sbuf(&ccb->ataio.res, &sb);
		sbuf_printf(&sb, "\" ");
		type = "error";
		break;
	default:
		type = "error";
		break;
	}

	if (ccb->ccb_h.func_code == XPT_SCSI_IO) {
		sbuf_printf(&sb, "CDB=\"");
		scsi_cdb_sbuf(scsiio_cdb_ptr(&ccb->csio), &sb);
		sbuf_printf(&sb, "\" ");
	} else if (ccb->ccb_h.func_code == XPT_ATA_IO) {
		sbuf_printf(&sb, "ACB=\"");
		ata_cmd_sbuf(&ccb->ataio.cmd, &sb);
		sbuf_printf(&sb, "\" ");
	}

	if (sbuf_finish(&sb) == 0)
		devctl_notify("CAM", "periph", type, sbuf_data(&sb));
	sbuf_delete(&sb);
	free(sbmsg, M_CAMPERIPH);
}

/*
 * Sysctl to force an invalidation of the drive right now. Can be
 * called with CTLFLAG_MPSAFE since we take periph lock.
 */
int
cam_periph_invalidate_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct cam_periph *periph;
	int error, value;

	periph = arg1;
	value = 0;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == NULL || value != 1)
		return (error);

	cam_periph_lock(periph);
	cam_periph_invalidate(periph);
	cam_periph_unlock(periph);

	return (0);
}
