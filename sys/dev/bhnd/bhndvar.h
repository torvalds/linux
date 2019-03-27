/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_BHNDVAR_H_
#define _BHND_BHNDVAR_H_

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/malloc.h>

#include "bhnd.h"

/*
 * Definitions shared by bhnd(4) bus and bhndb(4) bridge driver implementations.
 */

MALLOC_DECLARE(M_BHND);
DECLARE_CLASS(bhnd_driver);

struct bhnd_core_clkctl;

struct bhnd_core_clkctl		*bhnd_alloc_core_clkctl(device_t dev,
				     device_t pmu_dev, struct bhnd_resource *r,
				     bus_size_t offset, u_int max_latency);
void				 bhnd_free_core_clkctl(
				     struct bhnd_core_clkctl *clkctl);
int				 bhnd_core_clkctl_wait(
				     struct bhnd_core_clkctl *clkctl,
				     uint32_t value, uint32_t mask);

int				 bhnd_generic_attach(device_t dev);
int				 bhnd_generic_detach(device_t dev);
int				 bhnd_generic_shutdown(device_t dev);
int				 bhnd_generic_resume(device_t dev);
int				 bhnd_generic_suspend(device_t dev);

int				 bhnd_generic_get_probe_order(device_t dev,
				     device_t child);

int				 bhnd_generic_alloc_pmu(device_t dev,
				     device_t child);
int				 bhnd_generic_release_pmu(device_t dev,
				     device_t child);
int				 bhnd_generic_get_clock_latency(device_t dev,
				     device_t child, bhnd_clock clock,
				     u_int *latency);
int				 bhnd_generic_get_clock_freq(device_t dev,
				     device_t child, bhnd_clock clock,
				     u_int *freq);
int				 bhnd_generic_request_clock(device_t dev,
				     device_t child, bhnd_clock clock);
int				 bhnd_generic_enable_clocks(device_t dev,
				     device_t child, uint32_t clocks);
int				 bhnd_generic_request_ext_rsrc(device_t dev,
				     device_t child, u_int rsrc);
int				 bhnd_generic_release_ext_rsrc(device_t dev,
				     device_t child, u_int rsrc);

int				 bhnd_generic_print_child(device_t dev,
				     device_t child);
void				 bhnd_generic_probe_nomatch(device_t dev,
				     device_t child);

void				 bhnd_generic_child_deleted(device_t dev,
				     device_t child);
int				 bhnd_generic_suspend_child(device_t dev,
				     device_t child);
int				 bhnd_generic_resume_child(device_t dev,
				     device_t child);

int				 bhnd_generic_setup_intr(device_t dev,
				     device_t child, struct resource *irq,
				     int flags, driver_filter_t *filter,
				     driver_intr_t *intr, void *arg,
				     void **cookiep);

int				 bhnd_generic_get_nvram_var(device_t dev,
				     device_t child, const char *name,
				     void *buf, size_t *size,
				     bhnd_nvram_type type);

/**
 * bhnd driver instance state. Must be first member of all subclass
 * softc structures.
 */
struct bhnd_softc {
	device_t dev;	/**< bus device */
};

#endif /* _BHND_BHNDVAR_H_ */
