/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * An interface to the FreeBSD kernel's bus/device information interface.
 *
 * This interface is implemented with the
 *
 * hw.bus
 * hw.bus.devices
 * hw.bus.rman
 * 
 * sysctls.  The interface is not meant for general user application
 * consumption.
 *
 * Device information is obtained by scanning a linear list of all devices
 * maintained by the kernel.  The actual device structure pointers are
 * handed out as opaque handles in order to allow reconstruction of the
 * logical toplogy in user space.
 *
 * Resource information is obtained by scanning the kernel's resource
 * managers and fetching their contents.  Ownership of resources is
 * tracked using the device's structure pointer again as a handle.
 *
 * In order to ensure coherency of the library's picture of the kernel,
 * a generation count is maintained by the kernel.  The initial generation
 * count is obtained (along with the interface version) from the hw.bus
 * sysctl, and must be passed in with every request.  If the generation
 * number supplied by the library does not match the kernel's current
 * generation number, the request is failed and the library must discard
 * the data it has received and rescan.
 *
 * The information obtained from the kernel is exported to consumers of
 * this library through a variety of interfaces.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devinfo.h"
#include "devinfo_var.h"

static int	devinfo_init_devices(int generation);
static int	devinfo_init_resources(int generation);

TAILQ_HEAD(,devinfo_i_dev)	devinfo_dev;
TAILQ_HEAD(,devinfo_i_rman)	devinfo_rman;
TAILQ_HEAD(,devinfo_i_res)	devinfo_res;

static int	devinfo_initted = 0;
static int	devinfo_generation = 0;

#if 0
# define debug(...)	do { \
	fprintf(stderr, "%s:", __func__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
} while (0)
#else
# define debug(...)
#endif

/*
 * Initialise our local database with the contents of the kernel's
 * tables.
 */
int
devinfo_init(void)
{
	struct u_businfo	ubus;
	size_t		ub_size;
	int			error, retries;

	if (!devinfo_initted) {
		TAILQ_INIT(&devinfo_dev);
		TAILQ_INIT(&devinfo_rman);
		TAILQ_INIT(&devinfo_res);
	}

	/*
	 * Get the generation count and interface version, verify that we 
	 * are compatible with the kernel.
	 */
	for (retries = 0; retries < 10; retries++) {
		debug("get interface version");
		ub_size = sizeof(ubus);
		if (sysctlbyname("hw.bus.info", &ubus,
		    &ub_size, NULL, 0) != 0) {
			warn("sysctlbyname(\"hw.bus.info\", ...) failed");
			return(EINVAL);
		}
		if ((ub_size != sizeof(ubus)) ||
		    (ubus.ub_version != BUS_USER_VERSION)) {
			warnx("kernel bus interface version mismatch: kernel %d expected %d",
			    ubus.ub_version, BUS_USER_VERSION);
			return(EINVAL);
		}
		debug("generation count is %d", ubus.ub_generation);

		/*
		 * Don't rescan if the generation count hasn't changed.
		 */
		if (ubus.ub_generation == devinfo_generation)
			return(0);

		/*
		 * Generation count changed, rescan
		 */
		devinfo_free();
		devinfo_initted = 0;
		devinfo_generation = 0;

		if ((error = devinfo_init_devices(ubus.ub_generation)) != 0) {
			devinfo_free();
			if (error == EINVAL)
				continue;
			break;
		}
		if ((error = devinfo_init_resources(ubus.ub_generation)) != 0) {
			devinfo_free();
			if (error == EINVAL)
				continue;
			break;
		}
		devinfo_initted = 1;
		devinfo_generation = ubus.ub_generation;
		return(0);
	}
	debug("scan failed after %d retries", retries);
	errno = error;
	return(1);
}

static int
devinfo_init_devices(int generation)
{
	struct u_device		udev;
	struct devinfo_i_dev	*dd;
	int			dev_idx;
	int			dev_ptr;
	int			name2oid[2];
	int			oid[CTL_MAXNAME + 12];
	size_t			oidlen, rlen;
	char			*name, *walker, *ep;
	int			error;

	/* 
	 * Find the OID for the rman interface node.
	 * This is just the usual evil, undocumented sysctl juju.
	 */
	name2oid[0] = 0;
	name2oid[1] = 3;
	oidlen = sizeof(oid);
	name = "hw.bus.devices";
	error = sysctl(name2oid, 2, oid, &oidlen, name, strlen(name));
	if (error < 0) {
		warnx("can't find hw.bus.devices sysctl node");
		return(ENOENT);
	}
	oidlen /= sizeof(int);
	if (oidlen > CTL_MAXNAME) {
		warnx("hw.bus.devices oid is too large");
		return(EINVAL);
	}
	oid[oidlen++] = generation;
	dev_ptr = oidlen++;

	/*
	 * Scan devices.
	 *
	 * Stop after a fairly insane number to avoid death in the case
	 * of kernel corruption.
	 */
	for (dev_idx = 0; dev_idx < 10000; dev_idx++) {

		/*
		 * Get the device information.
		 */
		oid[dev_ptr] = dev_idx;
		rlen = sizeof(udev);
		error = sysctl(oid, oidlen, &udev, &rlen, NULL, 0);
		if (error < 0) {
			if (errno == ENOENT)	/* end of list */
				break;
			if (errno != EINVAL)	/* gen count skip, restart */
				warn("sysctl hw.bus.devices.%d", dev_idx);
			return(errno);
		}
		if (rlen != sizeof(udev)) {
			warnx("sysctl returned wrong data %zd bytes instead of %zd",
			    rlen, sizeof(udev));
			return (EINVAL);
		}
		if ((dd = malloc(sizeof(*dd))) == NULL)
			return(ENOMEM);
		dd->dd_dev.dd_handle = udev.dv_handle;
		dd->dd_dev.dd_parent = udev.dv_parent;
		dd->dd_dev.dd_devflags = udev.dv_devflags;
		dd->dd_dev.dd_flags = udev.dv_flags;
		dd->dd_dev.dd_state = udev.dv_state;

		walker = udev.dv_fields;
		ep = walker + sizeof(udev.dv_fields);
		dd->dd_name = NULL;
		dd->dd_desc = NULL;
		dd->dd_drivername = NULL;
		dd->dd_pnpinfo = NULL;
		dd->dd_location = NULL;
#define UNPACK(x)							\
		dd->dd_dev.x = dd->x = strdup(walker);			\
		if (dd->x == NULL)					\
			return(ENOMEM);					\
		if (walker + strnlen(walker, ep - walker) >= ep)	\
			return(EINVAL);					\
		walker += strlen(walker) + 1;

		UNPACK(dd_name);
		UNPACK(dd_desc);
		UNPACK(dd_drivername);
		UNPACK(dd_pnpinfo);
		UNPACK(dd_location);
#undef UNPACK
		TAILQ_INSERT_TAIL(&devinfo_dev, dd, dd_link);
	}
	debug("fetched %d devices", dev_idx);
	return(0);
}

static int
devinfo_init_resources(int generation)
{
	struct u_rman		urman;
	struct devinfo_i_rman	*dm;
	struct u_resource	ures;
	struct devinfo_i_res	*dr;
	int			rman_idx, res_idx;
	int			rman_ptr, res_ptr;
	int			name2oid[2];
	int			oid[CTL_MAXNAME + 12];
	size_t			oidlen, rlen;
	char			*name;
	int			error;

	/* 
	 * Find the OID for the rman interface node.
	 * This is just the usual evil, undocumented sysctl juju.
	 */
	name2oid[0] = 0;
	name2oid[1] = 3;
	oidlen = sizeof(oid);
	name = "hw.bus.rman";
	error = sysctl(name2oid, 2, oid, &oidlen, name, strlen(name));
	if (error < 0) {
		warnx("can't find hw.bus.rman sysctl node");
		return(ENOENT);
	}
	oidlen /= sizeof(int);
	if (oidlen > CTL_MAXNAME) {
		warnx("hw.bus.rman oid is too large");
		return(EINVAL);
	}
	oid[oidlen++] = generation;
	rman_ptr = oidlen++;
	res_ptr = oidlen++;

	/*
	 * Scan resource managers.
	 *
	 * Stop after a fairly insane number to avoid death in the case
	 * of kernel corruption.
	 */
	for (rman_idx = 0; rman_idx < 255; rman_idx++) {

		/*
		 * Get the resource manager information.
		 */
		oid[rman_ptr] = rman_idx;
		oid[res_ptr] = -1;
		rlen = sizeof(urman);
		error = sysctl(oid, oidlen, &urman, &rlen, NULL, 0);
		if (error < 0) {
			if (errno == ENOENT)	/* end of list */
				break;
			if (errno != EINVAL)	/* gen count skip, restart */
				warn("sysctl hw.bus.rman.%d", rman_idx);
			return(errno);
		}
		if ((dm = malloc(sizeof(*dm))) == NULL)
			return(ENOMEM);
		dm->dm_rman.dm_handle = urman.rm_handle;
		dm->dm_rman.dm_start = urman.rm_start;
		dm->dm_rman.dm_size = urman.rm_size;
		snprintf(dm->dm_desc, DEVINFO_STRLEN, "%s", urman.rm_descr);
		dm->dm_rman.dm_desc = &dm->dm_desc[0];
		TAILQ_INSERT_TAIL(&devinfo_rman, dm, dm_link);

		/*
		 * Scan resources on this resource manager.
		 *
		 * Stop after a fairly insane number to avoid death in the case
		 * of kernel corruption.
		 */
		for (res_idx = 0; res_idx < 1000; res_idx++) {
			/* 
			 * Get the resource information.
			 */
			oid[res_ptr] = res_idx;
			rlen = sizeof(ures);
			error = sysctl(oid, oidlen, &ures, &rlen, NULL, 0);
			if (error < 0) {
				if (errno == ENOENT)	/* end of list */
					break;
				if (errno != EINVAL)	/* gen count skip */
					warn("sysctl hw.bus.rman.%d.%d",
					    rman_idx, res_idx);
				return(errno);
			}
			if ((dr = malloc(sizeof(*dr))) == NULL)
				return(ENOMEM);
			dr->dr_res.dr_handle = ures.r_handle;
			dr->dr_res.dr_rman = ures.r_parent;
			dr->dr_res.dr_device = ures.r_device;
			dr->dr_res.dr_start = ures.r_start;
			dr->dr_res.dr_size = ures.r_size;
			TAILQ_INSERT_TAIL(&devinfo_res, dr, dr_link);
		}
		debug("fetched %d resources", res_idx);
	}
	debug("scanned %d resource managers", rman_idx);
	return(0);
}

/*
 * Free the list contents.
 */
void
devinfo_free(void)
{
	struct devinfo_i_dev	*dd;
	struct devinfo_i_rman	*dm;
	struct devinfo_i_res	*dr;

	while ((dd = TAILQ_FIRST(&devinfo_dev)) != NULL) {
		TAILQ_REMOVE(&devinfo_dev, dd, dd_link);
		free(dd->dd_name);
		free(dd->dd_desc);
		free(dd->dd_drivername);
		free(dd->dd_pnpinfo);
		free(dd->dd_location);
		free(dd);
	}
	while ((dm = TAILQ_FIRST(&devinfo_rman)) != NULL) {
		TAILQ_REMOVE(&devinfo_rman, dm, dm_link);
		free(dm);
	}
	while ((dr = TAILQ_FIRST(&devinfo_res)) != NULL) {
		TAILQ_REMOVE(&devinfo_res, dr, dr_link);
		free(dr);
	}
	devinfo_initted = 0;
	devinfo_generation = 0;
}

/*
 * Find a device by its handle.
 */
struct devinfo_dev *
devinfo_handle_to_device(devinfo_handle_t handle)
{
	struct devinfo_i_dev	*dd;

	/*
	 * Find the root device, whose parent is NULL
	 */
	if (handle == DEVINFO_ROOT_DEVICE) {
		TAILQ_FOREACH(dd, &devinfo_dev, dd_link)
		    if (dd->dd_dev.dd_parent == DEVINFO_ROOT_DEVICE)
			    return(&dd->dd_dev);
		return(NULL);
	}

	/*
	 * Scan for the device
	 */
	TAILQ_FOREACH(dd, &devinfo_dev, dd_link)
	    if (dd->dd_dev.dd_handle == handle)
		    return(&dd->dd_dev);
	return(NULL);
}

/*
 * Find a resource by its handle.
 */
struct devinfo_res *
devinfo_handle_to_resource(devinfo_handle_t handle)
{
	struct devinfo_i_res	*dr;

	TAILQ_FOREACH(dr, &devinfo_res, dr_link)
	    if (dr->dr_res.dr_handle == handle)
		    return(&dr->dr_res);
	return(NULL);
}

/*
 * Find a resource manager by its handle.
 */
struct devinfo_rman *
devinfo_handle_to_rman(devinfo_handle_t handle)
{
	struct devinfo_i_rman	*dm;

	TAILQ_FOREACH(dm, &devinfo_rman, dm_link)
	    if (dm->dm_rman.dm_handle == handle)
		    return(&dm->dm_rman);
	return(NULL);
}

/*
 * Iterate over the children of a device, calling (fn) on each.  If
 * (fn) returns nonzero, abort the scan and return.
 */
int
devinfo_foreach_device_child(struct devinfo_dev *parent, 
    int (* fn)(struct devinfo_dev *child, void *arg), 
    void *arg)
{
	struct devinfo_i_dev	*dd;
	int				error;

	TAILQ_FOREACH(dd, &devinfo_dev, dd_link)
	    if (dd->dd_dev.dd_parent == parent->dd_handle)
		    if ((error = fn(&dd->dd_dev, arg)) != 0)
			    return(error);
	return(0);
}

/*
 * Iterate over all the resources owned by a device, calling (fn) on each.
 * If (fn) returns nonzero, abort the scan and return.
 */
int
devinfo_foreach_device_resource(struct devinfo_dev *dev,
    int (* fn)(struct devinfo_dev *dev, struct devinfo_res *res, void *arg),
    void *arg)
{
	struct devinfo_i_res	*dr;
	int				error;

	TAILQ_FOREACH(dr, &devinfo_res, dr_link)
	    if (dr->dr_res.dr_device == dev->dd_handle)
		    if ((error = fn(dev, &dr->dr_res, arg)) != 0)
			    return(error);
	return(0);
}

/*
 * Iterate over all the resources owned by a resource manager, calling (fn)
 * on each.  If (fn) returns nonzero, abort the scan and return.
 */
extern int
devinfo_foreach_rman_resource(struct devinfo_rman *rman,
    int (* fn)(struct devinfo_res *res, void *arg),
    void *arg)
{
	struct devinfo_i_res	*dr;
	int				error;

	TAILQ_FOREACH(dr, &devinfo_res, dr_link)
	    if (dr->dr_res.dr_rman == rman->dm_handle)
		    if ((error = fn(&dr->dr_res, arg)) != 0)
			    return(error);
	return(0);
}

/*
 * Iterate over all the resource managers, calling (fn) on each.  If (fn)
 * returns nonzero, abort the scan and return.
 */
extern int
devinfo_foreach_rman(int (* fn)(struct devinfo_rman *rman, void *arg),
    void *arg)
{
    struct devinfo_i_rman	*dm;
    int				error;

    TAILQ_FOREACH(dm, &devinfo_rman, dm_link)
	if ((error = fn(&dm->dm_rman, arg)) != 0)
	    return(error);
    return(0);
}
