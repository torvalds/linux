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

#include <sys/types.h>
#include <sys/rman.h>
#include <sys/bus.h>

/*
 * This is defined by the version 1 interface.
 */
#define DEVINFO_STRLEN	32

/*
 * Devices.  
 *
 * Internal structure contains string buffers and list linkage;
 */
struct devinfo_i_dev {
	struct devinfo_dev		dd_dev;
	char				*dd_name;
	char				*dd_desc;
	char				*dd_drivername;
	char				*dd_pnpinfo;
	char				*dd_location;
	uint32_t			dd_devflags;
	uint16_t			dd_flags;
	device_state_t			dd_state;
	TAILQ_ENTRY(devinfo_i_dev)	dd_link;
};

/*
 * Resources.
 *
 * Internal structures contain string buffers and list linkage;
 */
struct devinfo_i_rman {
	struct devinfo_rman		dm_rman;
	char				dm_desc[32];
	TAILQ_ENTRY(devinfo_i_rman)	dm_link;
};

struct devinfo_i_res {
	struct devinfo_res		dr_res;
	TAILQ_ENTRY(devinfo_i_res)	dr_link;
};
