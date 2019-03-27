/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Landon Fuller under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BHND_BHND_PRIVATE_H_
#define _BHND_BHND_PRIVATE_H_

#include <sys/param.h>
#include <sys/queue.h>

#include "bhnd_types.h"

/*
 * Private bhnd(4) driver definitions.
 */

/**
 * A bhnd(4) service registry entry.
 */
struct bhnd_service_entry {
	device_t	provider;	/**< service provider */
	bhnd_service_t	service;	/**< service implemented */
	uint32_t	flags;		/**< entry flags (see BHND_SPF_*) */
	volatile u_int	refs;		/**< reference count; updated atomically
					     with only a shared lock held */

	STAILQ_ENTRY(bhnd_service_entry) link;
};

/**
 * bhnd(4) per-core PMU clkctl quirks.
 */
enum {
	/** On BCM4328-derived chipsets, the CLK_CTL_ST register CCS_HTAVAIL
	 *  and CCS_ALPAVAIL bits are swapped in the ChipCommon and PCMCIA
	 *  cores; the BHND_CCS0_* constants should be used. */
	BHND_CLKCTL_QUIRK_CCS0	= 1
};

/**
 * Per-core bhnd(4) PMU clkctl registers.
 */
struct bhnd_core_clkctl {
	device_t		 cc_dev;		/**< core device */
	device_t		 cc_pmu_dev;		/**< pmu device */
	uint32_t		 cc_quirks;		/**< core-specific clkctl quirks */
	struct bhnd_resource	*cc_res;		/**< resource mapping core's clkctl register */
	bus_size_t		 cc_res_offset;		/**< offset to clkctl register */
	u_int			 cc_max_latency;	/**< maximum PMU transition latency, in microseconds */
	struct mtx		 cc_mtx;		/**< register read/modify/write lock */
};

#define	BHND_ASSERT_CLKCTL_AVAIL(_clkctl)			\
	KASSERT(!bhnd_is_hw_suspended((_clkctl)->cc_dev),	\
	    ("reading clkctl on suspended core will trigger system livelock"))

#define	BHND_CLKCTL_LOCK_INIT(_clkctl)		mtx_init(&(_clkctl)->cc_mtx, \
    device_get_nameunit((_clkctl)->cc_dev), NULL, MTX_DEF)
#define	BHND_CLKCTL_LOCK(_clkctl)		mtx_lock(&(_clkctl)->cc_mtx)
#define	BHND_CLKCTL_UNLOCK(_clkctl)		mtx_unlock(&(_clkctl)->cc_mtx)
#define	BHND_CLKCTL_LOCK_ASSERT(_clkctl, what)	\
    mtx_assert(&(_clkctl)->cc_mtx, what)
#define	BHND_CLKCTL_LOCK_DESTROY(_clkctl)	mtx_destroy(&(_clkctl->cc_mtx))

#define	BHND_CLKCTL_READ_4(_clkctl)		\
	bhnd_bus_read_4((_clkctl)->cc_res, (_clkctl)->cc_res_offset)

#define	BHND_CLKCTL_WRITE_4(_clkctl, _val)	\
	bhnd_bus_write_4((_clkctl)->cc_res, (_clkctl)->cc_res_offset, (_val))
	
#define	BHND_CLKCTL_SET_4(_clkctl, _val, _mask)	\
	BHND_CLKCTL_WRITE_4((_clkctl),		\
	    ((_val) & (_mask)) | (BHND_CLKCTL_READ_4(_clkctl) & ~(_mask)))

#endif /* _BHND_BHND_PRIVATE_H_ */
