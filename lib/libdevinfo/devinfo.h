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
 *
 *	$FreeBSD$
 */

#ifndef _DEVINFO_H_INCLUDED
#define _DEVINFO_H_INCLUDED

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/bus.h>

typedef __uintptr_t	devinfo_handle_t;
#define DEVINFO_ROOT_DEVICE	((devinfo_handle_t)0)

typedef enum device_state devinfo_state_t;

struct devinfo_dev {
	devinfo_handle_t	dd_handle;	/* device handle */
	devinfo_handle_t	dd_parent;	/* parent handle */

	char			*dd_name;	/* name of device */
	char			*dd_desc;	/* device description */
	char			*dd_drivername;	/* name of attached driver*/
	char			*dd_pnpinfo;	/* pnp info from parent bus */
	char			*dd_location;	/* Where bus thinks dev at */
	uint32_t		dd_devflags;	/* API flags */
	uint16_t		dd_flags;	/* internal dev flags */
	devinfo_state_t		dd_state;	/* attachment state of dev */
};

struct devinfo_rman {
	devinfo_handle_t	dm_handle;	/* resource manager handle */

	rman_res_t		dm_start;	/* resource start */
	rman_res_t		dm_size;	/* resource size */
    
	char			*dm_desc;	/* resource description */
};

struct devinfo_res {
	devinfo_handle_t	dr_handle;	/* resource handle */
	devinfo_handle_t	dr_rman;	/* resource manager handle */
	devinfo_handle_t	dr_device;	/* owning device */

	rman_res_t		dr_start;	/* region start */
	rman_res_t		dr_size;	/* region size */
	/* XXX add flags */
};

__BEGIN_DECLS

/*
 * Acquire a coherent copy of the kernel's device and resource tables.
 * This must return success (zero) before any other interfaces will
 * function.  Sets errno on failure.
 */
extern int	devinfo_init(void);

/*
 * Release the storage associated with the internal copy of the device
 * and resource tables. devinfo_init must be called before any attempt
 * is made to use any other interfaces.
 */
extern void	devinfo_free(void);

/*
 * Find a device/resource/resource manager by its handle.
 */
extern struct devinfo_dev
	*devinfo_handle_to_device(devinfo_handle_t handle);
extern struct devinfo_res
	*devinfo_handle_to_resource(devinfo_handle_t handle);
extern struct devinfo_rman
	*devinfo_handle_to_rman(devinfo_handle_t handle);

/*
 * Iterate over the children of a device, calling (fn) on each.  If
 * (fn) returns nonzero, abort the scan and return.
 */
extern int
	devinfo_foreach_device_child(struct devinfo_dev *parent, 
	    int (* fn)(struct devinfo_dev *child, void *arg), 
	    void *arg);

/*
 * Iterate over all the resources owned by a device, calling (fn) on each.
 * If (fn) returns nonzero, abort the scan and return.
 */
extern int
	devinfo_foreach_device_resource(struct devinfo_dev *dev,
	    int (* fn)(struct devinfo_dev *dev, 
	    struct devinfo_res *res, void *arg),
	    void *arg);

/*
 * Iterate over all the resources owned by a resource manager, calling (fn)
 * on each.  If (fn) returns nonzero, abort the scan and return.
 */
extern int
	devinfo_foreach_rman_resource(struct devinfo_rman *rman,
	    int (* fn)(struct devinfo_res *res, void *arg),
	    void *arg);

/*
 * Iterate over all the resource managers, calling (fn) on each.  If (fn)
 * returns nonzero, abort the scan and return.
 */
extern int
	devinfo_foreach_rman(int (* fn)(struct devinfo_rman *rman, void *arg),
	    void *arg);

__END_DECLS

#endif /* ! _DEVINFO_H_INCLUDED */
