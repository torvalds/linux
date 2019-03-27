/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2006 by Juniper Networks.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_QUICC_BFE_H_
#define _DEV_QUICC_BFE_H_

struct quicc_device;

struct quicc_softc {
	device_t	sc_dev;

	struct resource	*sc_rres;	/* Register resource. */
	int		sc_rrid;
	int		sc_rtype;	/* SYS_RES_{IOPORT|MEMORY}. */

	struct resource *sc_ires;	/* Interrupt resource. */
	void		*sc_icookie;
	int		sc_irid;

	struct rman	sc_rman;
	struct quicc_device *sc_device;

	u_int		sc_clock;

	int		sc_fastintr:1;
	int		sc_polled:1;
};

extern devclass_t quicc_devclass;
extern char quicc_driver_name[];

int quicc_bfe_attach(device_t);
int quicc_bfe_detach(device_t);
int quicc_bfe_probe(device_t, u_int);

struct resource *quicc_bus_alloc_resource(device_t, device_t, int, int *,
    rman_res_t, rman_res_t, rman_res_t, u_int);
int quicc_bus_get_resource(device_t, device_t, int, int,
    rman_res_t *, rman_res_t *);
int quicc_bus_read_ivar(device_t, device_t, int, uintptr_t *);
int quicc_bus_release_resource(device_t, device_t, int, int, struct resource *);
int quicc_bus_setup_intr(device_t, device_t, struct resource *, int,
    driver_filter_t *, void (*)(void *), void *, void **);
int quicc_bus_teardown_intr(device_t, device_t, struct resource *, void *);

#endif /* _DEV_QUICC_BFE_H_ */
