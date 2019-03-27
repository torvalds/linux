/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Marcel Moolenaar
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

#ifndef _DEV_SCC_BFE_H_
#define _DEV_SCC_BFE_H_

#include <sys/serial.h>

/*
 * Bus access structure. This structure holds the minimum information needed
 * to access the SCC. The rclk field, although not important to actually
 * access the SCC, is important for baudrate programming, delay loops and
 * other timing related computations.
 */
struct scc_bas {
	bus_space_tag_t	bst;
	bus_space_handle_t bsh;
	u_int		range;
	u_int		rclk;
	u_int		regshft;
};

#define	scc_regofs(bas, reg)		((reg) << (bas)->regshft)

#define	scc_getreg(bas, reg)		\
	bus_space_read_1((bas)->bst, (bas)->bsh, scc_regofs(bas, reg))
#define	scc_setreg(bas, reg, value)	\
	bus_space_write_1((bas)->bst, (bas)->bsh, scc_regofs(bas, reg), value)

#define	scc_barrier(bas)		\
	bus_space_barrier((bas)->bst, (bas)->bsh, 0, (bas)->range,	\
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE)

/*
 * SCC mode (child) and channel control structures.
 */

#define	SCC_NMODES	3
#define	SCC_ISRCCNT	5

struct scc_chan;

struct scc_mode {
	struct scc_chan	*m_chan;
	device_t	m_dev;

	u_int		m_mode;
	int		m_attached:1;
	int		m_fastintr:1;
	int		m_hasintr:1;
	int		m_probed:1;
	int		m_sysdev:1;

	driver_filter_t	*ih;
	serdev_intr_t	*ih_src[SCC_ISRCCNT];
	void		*ih_arg;
};

struct scc_chan {
	struct resource ch_rres;
	struct resource_list ch_rlist;

	struct resource *ch_ires;	/* Interrupt resource. */
	void		*ch_icookie;
	int		ch_irid;

	struct scc_mode	ch_mode[SCC_NMODES];

	u_int		ch_nr;
	int		ch_enabled:1;
	int		ch_sysdev:1;

	uint32_t	ch_ipend;
	uint32_t	ch_hwsig;
};

/*
 * SCC class & instance (=softc)
 */
struct scc_class {
	KOBJ_CLASS_FIELDS;
	u_int		cl_channels;	/* Number of independent channels. */
	u_int		cl_class;	/* SCC bus class ID. */
	u_int		cl_modes;	/* Supported modes (bitset). */
	int		cl_range;
};

extern struct scc_class scc_quicc_class;
extern struct scc_class scc_sab82532_class;
extern struct scc_class scc_z8530_class;

struct scc_softc {
	KOBJ_FIELDS;
	struct scc_class *sc_class;
	struct scc_bas	sc_bas;
	device_t	sc_dev;

	struct mtx	sc_hwmtx;	/* Spinlock protecting hardware. */

	struct resource	*sc_rres;	/* Register resource. */
	int		sc_rrid;
	int		sc_rtype;	/* SYS_RES_{IOPORT|MEMORY}. */

	struct scc_chan	*sc_chan;

	int		sc_fastintr:1;
	int		sc_leaving:1;
	int		sc_polled:1;

	uint32_t        sc_hwsig;       /* Signal state. Used by HW driver. */
};

extern devclass_t scc_devclass;
extern const char scc_driver_name[];

int scc_bfe_attach(device_t dev, u_int ipc);
int scc_bfe_detach(device_t dev);
int scc_bfe_probe(device_t dev, u_int regshft, u_int rclk, u_int rid);

struct resource *scc_bus_alloc_resource(device_t, device_t, int, int *,
    rman_res_t, rman_res_t, rman_res_t, u_int);
int scc_bus_get_resource(device_t, device_t, int, int, rman_res_t *, rman_res_t *);
int scc_bus_read_ivar(device_t, device_t, int, uintptr_t *);
int scc_bus_release_resource(device_t, device_t, int, int, struct resource *);
int scc_bus_setup_intr(device_t, device_t, struct resource *, int,
    driver_filter_t *, void (*)(void *), void *, void **);
int scc_bus_teardown_intr(device_t, device_t, struct resource *, void *);

#endif /* _DEV_SCC_BFE_H_ */
