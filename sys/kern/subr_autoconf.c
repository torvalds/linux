/*	$OpenBSD: subr_autoconf.c,v 1.98 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: subr_autoconf.c,v 1.21 1996/04/04 06:06:18 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Header: subr_autoconf.c,v 1.12 93/02/01 19:31:48 torek Exp  (LBL)
 *
 *	@(#)subr_autoconf.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/hotplug.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/mutex.h>
#include <sys/atomic.h>
#include <sys/reboot.h>

#include "hotplug.h"
#include "mpath.h"

/*
 * Autoconfiguration subroutines.
 */

/*
 * ioconf.c exports exactly two names: cfdata and cfroots.  All system
 * devices and drivers are found via these tables.
 */
extern short cfroots[];

#define	ROOT ((struct device *)NULL)

struct matchinfo {
	cfmatch_t fn;
	struct	device *parent;
	void	*match, *aux;
	int	indirect, pri;
};

#ifndef AUTOCONF_VERBOSE
#define AUTOCONF_VERBOSE 0
#endif /* AUTOCONF_VERBOSE */
int autoconf_verbose = AUTOCONF_VERBOSE;	/* trace probe calls */

static void mapply(struct matchinfo *, struct cfdata *);

struct deferred_config {
	TAILQ_ENTRY(deferred_config) dc_queue;
	struct device *dc_dev;
	void (*dc_func)(struct device *);
};

TAILQ_HEAD(, deferred_config) deferred_config_queue;
TAILQ_HEAD(, deferred_config) mountroot_config_queue;

void *config_rootsearch(cfmatch_t, char *, void *);
void config_process_deferred_children(struct device *);

struct devicelist alldevs;		/* list of all devices */

volatile int config_pending;		/* semaphore for mountroot */

struct mutex autoconf_attdet_mtx = MUTEX_INITIALIZER(IPL_HIGH);
/*
 * If > 0, devices are being attached and any thread which tries to
 * detach will sleep; if < 0 devices are being detached and any
 * thread which tries to attach will sleep.
 */
int	autoconf_attdet;

/*
 * Versioned state of the devices tree so that changes can be detected.
 */
unsigned int autoconf_serial = 0;

/*
 * Initialize autoconfiguration data structures.  This occurs before console
 * initialization as that might require use of this subsystem.  Furthermore
 * this means that malloc et al. isn't yet available.
 */
void
config_init(void)
{
	TAILQ_INIT(&deferred_config_queue);
	TAILQ_INIT(&mountroot_config_queue);
	TAILQ_INIT(&alldevs);
}

/*
 * Apply the matching function and choose the best.  This is used
 * a few times and we want to keep the code small.
 */
void
mapply(struct matchinfo *m, struct cfdata *cf)
{
	int pri;
	void *match;

	if (m->indirect)
		match = config_make_softc(m->parent, cf);
	else
		match = cf;

	if (autoconf_verbose) {
		printf(">>> probing for %s", cf->cf_driver->cd_name);
		if (cf->cf_fstate == FSTATE_STAR)
			printf("*\n");
		else
			printf("%d\n", cf->cf_unit);
	}
	if (m->fn != NULL)
		pri = (*m->fn)(m->parent, match, m->aux);
	else {
	        if (cf->cf_attach->ca_match == NULL) {
			panic("mapply: no match function for '%s' device",
			    cf->cf_driver->cd_name);
		}
		pri = (*cf->cf_attach->ca_match)(m->parent, match, m->aux);
	}
	if (autoconf_verbose)
		printf(">>> %s probe returned %d\n", cf->cf_driver->cd_name,
		    pri);

	if (pri > m->pri) {
		if (m->indirect && m->match) {
			cf = ((struct device *)m->match)->dv_cfdata;
			free(m->match, M_DEVBUF, cf->cf_attach->ca_devsize);
		}
		m->match = match;
		m->pri = pri;
	} else {
		if (m->indirect)
			free(match, M_DEVBUF, cf->cf_attach->ca_devsize);
	}
}

/*
 * Iterate over all potential children of some device, calling the given
 * function (default being the child's match function) for each one.
 * Nonzero returns are matches; the highest value returned is considered
 * the best match.  Return the `found child' if we got a match, or NULL
 * otherwise.  The `aux' pointer is simply passed on through.
 *
 * Note that this function is designed so that it can be used to apply
 * an arbitrary function to all potential children (its return value
 * can be ignored).
 */
void *
config_search(cfmatch_t fn, struct device *parent, void *aux)
{
	struct cfdata *cf;
	short *p;
	struct matchinfo m;

	m.fn = fn;
	m.parent = parent;
	m.match = NULL;
	m.aux = aux;
	m.indirect = parent && (parent->dv_cfdata->cf_driver->cd_mode & CD_INDIRECT);
	m.pri = 0;

	for (cf = cfdata; cf->cf_driver; cf++) {
		/*
		 * Skip cf if no longer eligible, otherwise scan
		 * through parents for one matching `parent',
		 * and try match function.
		 */
		if (cf->cf_fstate == FSTATE_FOUND)
			continue;
		if (cf->cf_fstate == FSTATE_DNOTFOUND ||
		    cf->cf_fstate == FSTATE_DSTAR)
			continue;
		if (boothowto & RB_UNHIBERNATE) {
			if (cf->cf_driver->cd_mode & CD_SKIPHIBERNATE)
				continue;
			if (cf->cf_driver->cd_class == DV_IFNET)
				continue;
			if (cf->cf_driver->cd_class == DV_TAPE)
				continue;
		}
		if (ISSET(boothowto, RB_COCOVM) &&
		    !ISSET(cf->cf_driver->cd_mode, CD_COCOVM))
			continue;
		for (p = cf->cf_parents; *p >= 0; p++)
			if (parent->dv_cfdata == &cfdata[*p])
				mapply(&m, cf);
	}

	if (autoconf_verbose) {
		if (m.match) {
			if (m.indirect)
				cf = ((struct device *)m.match)->dv_cfdata;
			else
				cf = (struct cfdata *)m.match;
			printf(">>> %s probe won\n",
			    cf->cf_driver->cd_name);
		} else
			printf(">>> no winning probe\n");
	}
	return (m.match);
}

/*
 * Iterate over all potential children of some device, calling the given
 * function for each one.
 *
 * Note that this function is designed so that it can be used to apply
 * an arbitrary function to all potential children (its return value
 * can be ignored).
 */
void
config_scan(cfscan_t fn, struct device *parent)
{
	struct cfdata *cf;
	short *p;
	void *match;
	int indirect;

	indirect = parent && (parent->dv_cfdata->cf_driver->cd_mode & CD_INDIRECT);

	for (cf = cfdata; cf->cf_driver; cf++) {
		/*
		 * Skip cf if no longer eligible, otherwise scan
		 * through parents for one matching `parent',
		 * and try match function.
		 */
		if (cf->cf_fstate == FSTATE_FOUND)
			continue;
		if (cf->cf_fstate == FSTATE_DNOTFOUND ||
		    cf->cf_fstate == FSTATE_DSTAR)
			continue;
		for (p = cf->cf_parents; *p >= 0; p++)
			if (parent->dv_cfdata == &cfdata[*p]) {
				match = indirect?
				    config_make_softc(parent, cf) :
				    (void *)cf;
				(*fn)(parent, match);
			}
	}
}

/*
 * Find the given root device.
 * This is much like config_search, but there is no parent.
 */
void *
config_rootsearch(cfmatch_t fn, char *rootname, void *aux)
{
	struct cfdata *cf;
	short *p;
	struct matchinfo m;

	m.fn = fn;
	m.parent = ROOT;
	m.match = NULL;
	m.aux = aux;
	m.indirect = 0;
	m.pri = 0;
	/*
	 * Look at root entries for matching name.  We do not bother
	 * with found-state here since only one instance of each possible
	 * root child should ever be searched.
	 */
	for (p = cfroots; *p >= 0; p++) {
		cf = &cfdata[*p];
		if (cf->cf_fstate == FSTATE_DNOTFOUND ||
		    cf->cf_fstate == FSTATE_DSTAR)
			continue;
		if (strcmp(cf->cf_driver->cd_name, rootname) == 0)
			mapply(&m, cf);
	}
	return (m.match);
}

const char *msgs[3] = { "", " not configured\n", " unsupported\n" };

/*
 * The given `aux' argument describes a device that has been found
 * on the given parent, but not necessarily configured.  Locate the
 * configuration data for that device (using the submatch function
 * provided, or using candidates' cd_match configuration driver
 * functions) and attach it, and return true.  If the device was
 * not configured, call the given `print' function and return 0.
 */
struct device *
config_found_sm(struct device *parent, void *aux, cfprint_t print,
    cfmatch_t submatch)
{
	void *match;

	if ((match = config_search(submatch, parent, aux)) != NULL)
		return (config_attach(parent, match, aux, print));
	if (print)
		printf("%s", msgs[(*print)(aux, parent->dv_xname)]);
	return (NULL);
}

/*
 * As above, but for root devices.
 */
struct device *
config_rootfound(char *rootname, void *aux)
{
	void *match;

	if ((match = config_rootsearch((cfmatch_t)NULL, rootname, aux)) != NULL)
		return (config_attach(ROOT, match, aux, (cfprint_t)NULL));
	printf("root device %s not configured\n", rootname);
	return (NULL);
}

/*
 * Attach a found device.  Allocates memory for device variables.
 */
struct device *
config_attach(struct device *parent, void *match, void *aux, cfprint_t print)
{
	struct cfdata *cf;
	struct device *dev;
	struct cfdriver *cd;
	const struct cfattach *ca;

	mtx_enter(&autoconf_attdet_mtx);
	while (autoconf_attdet < 0)
		msleep_nsec(&autoconf_attdet, &autoconf_attdet_mtx,
		    PWAIT, "autoconf", INFSLP);
	autoconf_attdet++;
	mtx_leave(&autoconf_attdet_mtx);

	if (parent && (parent->dv_cfdata->cf_driver->cd_mode & CD_INDIRECT)) {
		dev = match;
		cf = dev->dv_cfdata;
	} else {
		cf = match;
		dev = config_make_softc(parent, cf);
	}

	cd = cf->cf_driver;
	ca = cf->cf_attach;

	KASSERT(cd->cd_devs != NULL);
	KASSERT(dev->dv_unit < cd->cd_ndevs);
	KASSERT(cd->cd_devs[dev->dv_unit] == NULL);
	cd->cd_devs[dev->dv_unit] = dev;

	/*
	 * If this is a "STAR" device and we used the last unit, prepare for
	 * another one.
	 */
	if (cf->cf_fstate == FSTATE_STAR) {
		if (dev->dv_unit == cf->cf_unit)
			cf->cf_unit++;
	} else
		cf->cf_fstate = FSTATE_FOUND;

	TAILQ_INSERT_TAIL(&alldevs, dev, dv_list);
	device_ref(dev);

	if (parent == ROOT)
		printf("%s at root", dev->dv_xname);
	else {
		printf("%s at %s", dev->dv_xname, parent->dv_xname);
		if (print)
			(void) (*print)(aux, NULL);
	}

	/*
	 * Before attaching, clobber any unfound devices that are
	 * otherwise identical, or bump the unit number on all starred
	 * cfdata for this device.
	 */
	for (cf = cfdata; cf->cf_driver; cf++) {
		if (cf->cf_driver == cd &&
		    cf->cf_unit == dev->dv_unit) {
			if (cf->cf_fstate == FSTATE_NOTFOUND)
				cf->cf_fstate = FSTATE_FOUND;
			if (cf->cf_fstate == FSTATE_STAR)
				cf->cf_unit++;
		}
	}
	device_register(dev, aux);
	(*ca->ca_attach)(parent, dev, aux);
	config_process_deferred_children(dev);
#if NHOTPLUG > 0
	if (!cold)
		hotplug_device_attach(cd->cd_class, dev->dv_xname);
#endif

	mtx_enter(&autoconf_attdet_mtx);
	if (--autoconf_attdet == 0)
		wakeup(&autoconf_attdet);
	autoconf_serial++;
	mtx_leave(&autoconf_attdet_mtx);
	return (dev);
}

struct device *
config_make_softc(struct device *parent, struct cfdata *cf)
{
	struct device *dev;
	struct cfdriver *cd;
	const struct cfattach *ca;

	cd = cf->cf_driver;
	ca = cf->cf_attach;
	if (ca->ca_devsize < sizeof(struct device))
		panic("config_make_softc");

	/* get memory for all device vars */
	dev = malloc(ca->ca_devsize, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (dev == NULL)
		panic("config_make_softc: allocation for device softc failed");

	dev->dv_class = cd->cd_class;
	dev->dv_cfdata = cf;
	dev->dv_flags = DVF_ACTIVE;	/* always initially active */

	/* If this is a STAR device, search for a free unit number */
	if (cf->cf_fstate == FSTATE_STAR) {
		for (dev->dv_unit = cf->cf_starunit1;
		    dev->dv_unit < cf->cf_unit; dev->dv_unit++)
			if (cd->cd_ndevs == 0 ||
			    dev->dv_unit >= cd->cd_ndevs ||
			    cd->cd_devs[dev->dv_unit] == NULL)
				break;
	} else
		dev->dv_unit = cf->cf_unit;

	/* Build the device name into dv_xname. */
	if (snprintf(dev->dv_xname, sizeof(dev->dv_xname), "%s%d",
	    cd->cd_name, dev->dv_unit) >= sizeof(dev->dv_xname))
		panic("config_make_softc: device name too long");
	dev->dv_parent = parent;

	/* put this device in the devices array */
	if (dev->dv_unit >= cd->cd_ndevs) {
		/*
		 * Need to expand the array.
		 */
		int old = cd->cd_ndevs, new;
		void **nsp;

		if (old == 0)
			new = MINALLOCSIZE / sizeof(void *);
		else
			new = old * 2;
		while (new <= dev->dv_unit)
			new *= 2;
		cd->cd_ndevs = new;
		nsp = mallocarray(new, sizeof(void *), M_DEVBUF, M_NOWAIT|M_ZERO);
		if (nsp == NULL)
			panic("config_make_softc: %sing dev array",
			    old != 0 ? "expand" : "creat");
		if (old != 0) {
			bcopy(cd->cd_devs, nsp, old * sizeof(void *));
			free(cd->cd_devs, M_DEVBUF, old * sizeof(void *));
		}
		cd->cd_devs = nsp;
	}
	if (cd->cd_devs[dev->dv_unit])
		panic("config_make_softc: duplicate %s", dev->dv_xname);

	dev->dv_ref = 1;

	return (dev);
}

/*
 * Detach a device.  Optionally forced (e.g. because of hardware
 * removal) and quiet.  Returns zero if successful, non-zero
 * (an error code) otherwise.
 *
 * Note that this code wants to be run from a process context, so
 * that the detach can sleep to allow processes which have a device
 * open to run and unwind their stacks.
 */
int
config_detach(struct device *dev, int flags)
{
	struct cfdata *cf;
	const struct cfattach *ca;
	struct cfdriver *cd;
	int rv = 0, i;
#ifdef DIAGNOSTIC
	struct device *d;
#endif
#if NHOTPLUG > 0
	char devname[16];
#endif

	mtx_enter(&autoconf_attdet_mtx);
	while (autoconf_attdet > 0)
		msleep_nsec(&autoconf_attdet, &autoconf_attdet_mtx,
		    PWAIT, "autoconf", INFSLP);
	autoconf_attdet--;
	mtx_leave(&autoconf_attdet_mtx);

#if NHOTPLUG > 0
	strlcpy(devname, dev->dv_xname, sizeof(devname));
#endif

	cf = dev->dv_cfdata;
#ifdef DIAGNOSTIC
	if (cf->cf_fstate != FSTATE_FOUND && cf->cf_fstate != FSTATE_STAR)
		panic("config_detach: bad device fstate");
#endif
	ca = cf->cf_attach;
	cd = cf->cf_driver;

	/*
	 * Ensure the device is deactivated.  If the device has an
	 * activation entry point and DVF_ACTIVE is still set, the
	 * device is busy, and the detach fails.
	 */
	rv = config_deactivate(dev);

	/*
	 * Try to detach the device.  If that's not possible, then
	 * we either panic() (for the forced but failed case), or
	 * return an error.
	 */
	if (rv == 0) {
		if (ca->ca_detach != NULL)
			rv = (*ca->ca_detach)(dev, flags);
		else
			rv = EOPNOTSUPP;
	}
	if (rv != 0) {
		if ((flags & DETACH_FORCE) == 0)
			goto done;
		else
			panic("config_detach: forced detach of %s failed (%d)",
			    dev->dv_xname, rv);
	}

	/*
	 * The device has now been successfully detached.
	 */

#ifdef DIAGNOSTIC
	/*
	 * Sanity: If you're successfully detached, you should have no
	 * children.  (Note that because children must be attached
	 * after parents, we only need to search the latter part of
	 * the list.)
	 */
	i = 0;
	for (d = TAILQ_NEXT(dev, dv_list); d != NULL;
	     d = TAILQ_NEXT(d, dv_list)) {
		if (d->dv_parent == dev) {
			printf("config_detach: %s attached at %s\n",
			    d->dv_xname, dev->dv_xname);
			i = 1;
		}
	}
	if (i != 0)
		panic("config_detach: detached device (%s) has children",
		    dev->dv_xname);
#endif

	/*
	 * Mark cfdata to show that the unit can be reused, if possible.
	 * Note that we can only re-use a starred unit number if the unit
	 * being detached had the last assigned unit number.
	 */
	for (cf = cfdata; cf->cf_driver; cf++) {
		if (cf->cf_driver == cd) {
			if (cf->cf_fstate == FSTATE_FOUND &&
			    cf->cf_unit == dev->dv_unit)
				cf->cf_fstate = FSTATE_NOTFOUND;
			if (cf->cf_fstate == FSTATE_STAR &&
			    cf->cf_unit == dev->dv_unit + 1)
				cf->cf_unit--;
		}
	}

	/*
	 * Unlink from device list.
	 */
	TAILQ_REMOVE(&alldevs, dev, dv_list);
	device_unref(dev);

	/*
	 * Remove from cfdriver's array, tell the world, and free softc.
	 */
	cd->cd_devs[dev->dv_unit] = NULL;
	if ((flags & DETACH_QUIET) == 0)
		printf("%s detached\n", dev->dv_xname);

	device_unref(dev);
	/*
	 * If the device now has no units in use, deallocate its softc array.
	 */
	for (i = 0; i < cd->cd_ndevs; i++)
		if (cd->cd_devs[i] != NULL)
			break;
	if (i == cd->cd_ndevs) {		/* nothing found; deallocate */
		free(cd->cd_devs, M_DEVBUF, cd->cd_ndevs * sizeof(void *));
		cd->cd_devs = NULL;
		cd->cd_ndevs = 0;
		cf->cf_unit = 0;
	}

#if NHOTPLUG > 0
	if (!cold)
		hotplug_device_detach(cd->cd_class, devname);
#endif

	/*
	 * Return success.
	 */
done:
	mtx_enter(&autoconf_attdet_mtx);
	if (++autoconf_attdet == 0)
		wakeup(&autoconf_attdet);
	autoconf_serial++;
	mtx_leave(&autoconf_attdet_mtx);
	return (rv);
}

int
config_deactivate(struct device *dev)
{
	int rv = 0, oflags = dev->dv_flags;

	if (dev->dv_flags & DVF_ACTIVE) {
		dev->dv_flags &= ~DVF_ACTIVE;
		rv = config_suspend(dev, DVACT_DEACTIVATE);
		if (rv)
			dev->dv_flags = oflags;
	}
	return (rv);
}

/*
 * Defer the configuration of the specified device until all
 * of its parent's devices have been attached.
 */
void
config_defer(struct device *dev, void (*func)(struct device *))
{
	struct deferred_config *dc;

	if (dev->dv_parent == NULL)
		panic("config_defer: can't defer config of a root device");

#ifdef DIAGNOSTIC
	for (dc = TAILQ_FIRST(&deferred_config_queue); dc != NULL;
	     dc = TAILQ_NEXT(dc, dc_queue)) {
		if (dc->dc_dev == dev)
			panic("config_defer: deferred twice");
	}
#endif

	if ((dc = malloc(sizeof(*dc), M_DEVBUF, M_NOWAIT)) == NULL)
		panic("config_defer: can't allocate defer structure");

	dc->dc_dev = dev;
	dc->dc_func = func;
	TAILQ_INSERT_TAIL(&deferred_config_queue, dc, dc_queue);
	config_pending_incr();
}

/*
 * Defer the configuration of the specified device until after
 * root file system is mounted.
 */
void
config_mountroot(struct device *dev, void (*func)(struct device *))
{
	struct deferred_config *dc;

	/*
	 * No need to defer if root file system is already mounted.
	 */
	if (rootvp != NULL) {
		(*func)(dev);
		return;
	}

#ifdef DIAGNOSTIC
	for (dc = TAILQ_FIRST(&mountroot_config_queue); dc != NULL;
	     dc = TAILQ_NEXT(dc, dc_queue)) {
		if (dc->dc_dev == dev)
			panic("config_mountroot: deferred twice");
	}
#endif

	if ((dc = malloc(sizeof(*dc), M_DEVBUF, M_NOWAIT)) == NULL)
		panic("config_mountroot: can't allocate defer structure");

	dc->dc_dev = dev;
	dc->dc_func = func;
	TAILQ_INSERT_TAIL(&mountroot_config_queue, dc, dc_queue);
}

/*
 * Process the deferred configuration queue for a device.
 */
void
config_process_deferred_children(struct device *parent)
{
	struct deferred_config *dc, *ndc;

	for (dc = TAILQ_FIRST(&deferred_config_queue);
	     dc != NULL; dc = ndc) {
		ndc = TAILQ_NEXT(dc, dc_queue);
		if (dc->dc_dev->dv_parent == parent) {
			TAILQ_REMOVE(&deferred_config_queue, dc, dc_queue);
			(*dc->dc_func)(dc->dc_dev);
			free(dc, M_DEVBUF, sizeof(*dc));
			config_pending_decr();
		}
	}
}

/*
 * Process the deferred configuration queue after the root file
 * system is mounted .
 */
void
config_process_deferred_mountroot(void)
{
	struct deferred_config *dc;

	while ((dc = TAILQ_FIRST(&mountroot_config_queue)) != NULL) {
		TAILQ_REMOVE(&mountroot_config_queue, dc, dc_queue);
		(*dc->dc_func)(dc->dc_dev);
		free(dc, M_DEVBUF, sizeof(*dc));
	}
}

/*
 * Manipulate the config_pending semaphore.
 */
void
config_pending_incr(void)
{

	config_pending++;
}

void
config_pending_decr(void)
{

#ifdef DIAGNOSTIC
	if (config_pending == 0)
		panic("config_pending_decr: config_pending == 0");
#endif
	config_pending--;
	if (config_pending == 0)
		wakeup((void *)&config_pending);
}

int
config_detach_children(struct device *parent, int flags)
{
	struct device *dev, *next_dev;
	int rv = 0;

	/*
	 * The config_detach routine may sleep, meaning devices
	 * may be added to the queue. However, all devices will
	 * be added to the tail of the queue, the queue won't
	 * be re-organized, and the subtree of parent here should be locked
	 * for purposes of adding/removing children.
	 *
	 * Note that we can not afford trying to walk the device list
	 * once - our ``next'' device might be a child of the device
	 * we are about to detach, so it would disappear.
	 * Just play it safe and restart from the parent.
	 */
	for (dev = TAILQ_LAST(&alldevs, devicelist);
	    dev != NULL; dev = next_dev) {
		if (dev->dv_parent == parent) {
			if ((rv = config_detach(dev, flags)) != 0)
				return (rv);
			next_dev = TAILQ_LAST(&alldevs, devicelist);
		} else {
			next_dev = TAILQ_PREV(dev, devicelist, dv_list);
		}
	}

	return (0);
}

int
config_suspend(struct device *dev, int act)
{
	const struct cfattach *ca = dev->dv_cfdata->cf_attach;
	int r;

	device_ref(dev);
	if (ca->ca_activate)
		r = (*ca->ca_activate)(dev, act);
	else
		r = config_activate_children(dev, act);
	device_unref(dev);
	return (r);
}

int
config_suspend_all(int act)
{
	struct device *mainbus = device_mainbus();
	struct device *mpath = device_mpath();
	int rv = 0;

	switch (act) {
	case DVACT_QUIESCE:
	case DVACT_SUSPEND:
	case DVACT_POWERDOWN:
		if (mpath) {
			rv = config_suspend(mpath, act);
			if (rv)
				return rv;
		}
		if (mainbus)
			rv = config_suspend(mainbus, act);
		break;
	case DVACT_RESUME:
	case DVACT_WAKEUP:
		if (mainbus) {
			rv = config_suspend(mainbus, act);
			if (rv)
				return rv;
		}
		if (mpath)
			rv = config_suspend(mpath, act);
		break;
	}

	return (rv);
}

/*
 * Call the ca_activate for each of our children, letting each
 * decide whether they wish to do the same for their children
 * and more.
 */
int
config_activate_children(struct device *parent, int act)
{
	struct device *d;
	int rv = 0;

	for (d = TAILQ_NEXT(parent, dv_list); d != NULL;
	    d = TAILQ_NEXT(d, dv_list)) {
		if (d->dv_parent != parent)
			continue;
		switch (act) {
		case DVACT_QUIESCE:
		case DVACT_SUSPEND:
		case DVACT_RESUME:
		case DVACT_WAKEUP:
		case DVACT_POWERDOWN:
			rv = config_suspend(d, act);
			break;
		case DVACT_DEACTIVATE:
			rv = config_deactivate(d);
			break;
		}
		if (rv == 0)
			continue;

		/*
		 * Found a device that refuses the action.
		 * If we were being asked to suspend, we can
		 * try to resume all previous devices.
		 */
#ifdef DIAGNOSTIC
		printf("config_activate_children: device %s failed %d\n",
		    d->dv_xname, act);
#endif
		if (act == DVACT_RESUME)
			printf("failing resume cannot be handled\n");
		if (act == DVACT_POWERDOWN)
			return (rv);
		if (act != DVACT_SUSPEND)
			return (rv);

		d = TAILQ_PREV(d, devicelist, dv_list);
		for (; d != NULL && d != parent;
		    d = TAILQ_PREV(d, devicelist, dv_list)) {
			if (d->dv_parent != parent)
				continue;
			printf("resume %s\n", d->dv_xname);
			config_suspend(d, DVACT_RESUME);
		}
		return (rv);
	}
	return (rv);
}

/* 
 * Lookup a device in the cfdriver device array.  Does not return a
 * device if it is not active.
 *
 * Increments ref count on the device by one, reflecting the
 * new reference created on the stack.
 *
 * Context: process only 
 */
struct device *
device_lookup(struct cfdriver *cd, int unit)
{
	struct device *dv = NULL;

	if (unit >= 0 && unit < cd->cd_ndevs)
		dv = (struct device *)(cd->cd_devs[unit]);

	if (!dv)
		return (NULL);

	if (!(dv->dv_flags & DVF_ACTIVE))
		dv = NULL;

	if (dv != NULL)
		device_ref(dv);

	return (dv);
}

struct device *
device_mainbus(void)
{
	extern struct cfdriver mainbus_cd;

	if (mainbus_cd.cd_ndevs < 1)
		return (NULL);

	return (mainbus_cd.cd_devs[0]);
}

struct device *
device_mpath(void)
{
#if NMPATH > 0
	extern struct cfdriver mpath_cd;

	if (mpath_cd.cd_ndevs < 1)
		return (NULL);

	return (mpath_cd.cd_devs[0]);
#else
	return (NULL);
#endif
}

/*
 * Increments the ref count on the device structure. The device
 * structure is freed when the ref count hits 0.
 *
 * Context: process or interrupt
 */
void
device_ref(struct device *dv)
{
	atomic_inc_int(&dv->dv_ref);
}

/*
 * Decrement the ref count on the device structure.
 *
 * free's the structure when the ref count hits zero.
 *
 * Context: process or interrupt
 */
void
device_unref(struct device *dv)
{
	const struct cfattach *ca;

	if (atomic_dec_int_nv(&dv->dv_ref) == 0) {
		ca = dv->dv_cfdata->cf_attach;
		free(dv, M_DEVBUF, ca->ca_devsize);
	}
}
