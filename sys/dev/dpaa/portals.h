/*-
 * Copyright (c) 2012 Semihalf.
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
 * $FreeBSD$
 */

typedef struct dpaa_portal {
	int		dp_irid;		/* interrupt rid */
	struct resource	*dp_ires;		/* interrupt resource */

	bool		dp_regs_mapped;		/* register mapping status */

	t_Handle	dp_ph;			/* portal's handle */
	vm_paddr_t	dp_ce_pa;		/* portal's CE area PA */
	vm_paddr_t	dp_ci_pa;		/* portal's CI area PA */
	uint32_t	dp_ce_size;		/* portal's CE area size */
	uint32_t	dp_ci_size;		/* portal's CI area size */
	uintptr_t	dp_intr_num;		/* portal's intr. number */
} dpaa_portal_t;

struct dpaa_portals_softc {
	device_t	sc_dev;			/* device handle */
	vm_paddr_t	sc_dp_pa;		/* portal's PA */
	uint32_t	sc_dp_size;		/* portal's size */
	int		sc_rrid[2];		/* memory rid */
	struct resource	*sc_rres[2];		/* memory resource */
	dpaa_portal_t	sc_dp[MAXCPU];
};

struct dpaa_portals_devinfo {
	struct resource_list	di_res;
	int			di_intr_rid;
};

int bman_portals_attach(device_t);
int bman_portals_detach(device_t);

int qman_portals_attach(device_t);
int qman_portals_detach(device_t);

int dpaa_portal_alloc_res(device_t, struct dpaa_portals_devinfo *, int);
void dpaa_portal_map_registers(struct dpaa_portals_softc *);
