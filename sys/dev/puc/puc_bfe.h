/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_PUC_BFE_H_
#define	_DEV_PUC_BFE_H_

#define	PUC_PCI_BARS	6

struct puc_cfg;
struct puc_port;

extern const struct puc_cfg puc_pci_devices[];

extern devclass_t puc_devclass;
extern const char puc_driver_name[];

struct puc_bar {
	struct resource *b_res;
	int		b_rid;
	int		b_type;
};

struct puc_softc {
	device_t	sc_dev;

	const struct puc_cfg *sc_cfg;
	intptr_t	sc_cfg_data;

	struct puc_bar	sc_bar[PUC_PCI_BARS];
	struct rman	sc_ioport;
	struct rman	sc_iomem;
	struct rman	sc_irq;

	struct resource *sc_ires;
	void		*sc_icookie;
	int		sc_irid;

	int		sc_nports;
	struct puc_port *sc_port;

	int		sc_fastintr:1;
	int		sc_leaving:1;
	int		sc_polled:1;
	int		sc_msi:1;

	int		sc_ilr;

	/*
	 * Bitmask of ports that use the serdev I/F. This allows for
	 * 32 ports on ILP32 machines and 64 ports on LP64 machines.
	 */
	u_long		sc_serdevs;
};

struct puc_bar *puc_get_bar(struct puc_softc *sc, int rid);

int puc_bfe_attach(device_t);
int puc_bfe_detach(device_t);
int puc_bfe_probe(device_t, const struct puc_cfg *);

int puc_bus_child_location_str(device_t, device_t, char *, size_t);
int puc_bus_child_pnpinfo_str(device_t, device_t, char *, size_t);
struct resource *puc_bus_alloc_resource(device_t, device_t, int, int *,
    rman_res_t, rman_res_t, rman_res_t, u_int);
int puc_bus_get_resource(device_t, device_t, int, int, rman_res_t *, rman_res_t *);
int puc_bus_print_child(device_t, device_t);
int puc_bus_read_ivar(device_t, device_t, int, uintptr_t *);
int puc_bus_release_resource(device_t, device_t, int, int, struct resource *);
int puc_bus_setup_intr(device_t, device_t, struct resource *, int,
    driver_filter_t *, driver_intr_t *, void *, void **);
int puc_bus_teardown_intr(device_t, device_t, struct resource *, void *);

SYSCTL_DECL(_hw_puc);

#endif /* _DEV_PUC_BFE_H_ */
