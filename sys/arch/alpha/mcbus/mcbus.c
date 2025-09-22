/* $OpenBSD: mcbus.c,v 1.7 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: mcbus.c,v 1.19 2007/03/04 05:59:11 christos Exp $ */

/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Autoconfiguration routines for the MCBUS system
 * bus found on AlphaServer 4100 systems.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>
#include <machine/pte.h>

#include <alpha/mcbus/mcbusreg.h>
#include <alpha/mcbus/mcbusvar.h>

#include <alpha/pci/mcpciareg.h>

#define KV(_addr)	((caddr_t)ALPHA_PHYS_TO_K0SEG((_addr)))
#define	MCPCIA_EXISTS(mid, gid)	\
	(!badaddr((void *)KV(MCPCIA_BRIDGE_ADDR(gid, mid)), sizeof (u_int32_t)))

struct mcbus_cpu_busdep mcbus_primary;

int	mcbusmatch (struct device *, void *, void *);
void	mcbusattach (struct device *, struct device *, void *);
int	mcbusprint (void *, const char *);
int	mcbussbm (struct device *, void *, void *);

const	char *mcbus_node_type_str (u_int8_t);

typedef struct {
	struct device	mcbus_dev;
	u_int8_t	mcbus_types[MCBUS_MID_MAX];
} mcbus_softc_t;

const struct cfattach mcbus_ca = {
	sizeof(mcbus_softc_t), mcbusmatch, mcbusattach
};

struct cfdriver mcbus_cd = {
	NULL, "mcbus", DV_DULL,
};

/*
 * Tru64 UNIX (formerly Digital UNIX (formerly DEC OSF/1)) probes for MCPCIAs
 * in the following order:
 *
 *	5, 4, 7, 6
 *
 * This is so that the built-in CD-ROM on the internal 53c810 is always
 * dka500.  We probe them in the same order, for consistency.
 */
const int mcbus_mcpcia_probe_order[] = { 5, 4, 7, 6 };

extern void mcpcia_config_cleanup (void);

int
mcbusprint(void *aux, const char *cp)
{
	struct mcbus_dev_attach_args *tap = aux;
	printf(" mid %d: %s", tap->ma_mid,
	    mcbus_node_type_str(tap->ma_type));
	return (UNCONF);
}

int
mcbussbm(struct device *parent, void *cf, void *aux)
{
	struct mcbus_dev_attach_args *tap = aux;
	struct cfdata *mcf = (struct cfdata *)cf;

	if (mcf->cf_loc[MCBUSCF_MID] != MCBUSCF_MID_DEFAULT &&
	    mcf->cf_loc[MCBUSCF_MID] != tap->ma_mid)
		return (0);


	return ((*mcf->cf_attach->ca_match)(parent, mcf, aux));
}

int
mcbusmatch(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	/* Make sure we're looking for a MCBUS. */
	if (strcmp(ma->ma_name, mcbus_cd.cd_name) != 0)
		return (0);

	/*
	 * Only available on 4100 processor type platforms.
	 */
	if (cputype != ST_DEC_4100)
		return (0);

	return (1);
}

void
mcbusattach(struct device *parent, struct device *self, void *aux)
{
	static const char *bcs[CPU_BCacheMask + 1] = {
		"No", "1MB", "2MB", "4MB",
	};
	struct mcbus_dev_attach_args ta;
	mcbus_softc_t *mbp = (mcbus_softc_t *)self;
	int i, mid;

	printf(": %s BCache\n", mcbus_primary.mcbus_valid ?
	    bcs[mcbus_primary.mcbus_bcache] : "Unknown");

	mbp->mcbus_types[0] = MCBUS_TYPE_RES;
	for (mid = 1; mid < MCBUS_MID_MAX; ++mid)
		mbp->mcbus_types[mid] = MCBUS_TYPE_UNK;

	/*
	 * Find and "configure" memory.
	 */
	ta.ma_name = mcbus_cd.cd_name;
	ta.ma_gid = MCBUS_GID_FROM_INSTANCE(0);
	ta.ma_mid = 1;
	ta.ma_type = MCBUS_TYPE_MEM;
	mbp->mcbus_types[1] = MCBUS_TYPE_MEM;

	(void) config_found_sm(self, &ta, mcbusprint, mcbussbm);

	/*
	 * Now find PCI busses.
	 */
	for (i = 0; i < MCPCIA_PER_MCBUS; i++) {
		mid = mcbus_mcpcia_probe_order[i];
		ta.ma_name = mcbus_cd.cd_name;
		ta.ma_gid = MCBUS_GID_FROM_INSTANCE(0);
		ta.ma_mid = mid;
		ta.ma_type = MCBUS_TYPE_PCI;
		if (MCPCIA_EXISTS(ta.ma_mid, ta.ma_gid))
			(void) config_found_sm(self, &ta, mcbusprint,
					mcbussbm);
	}

	mcpcia_config_cleanup();
}

const char *
mcbus_node_type_str(u_int8_t type)
{
	switch (type) {
	case MCBUS_TYPE_RES:
		panic ("RESERVED TYPE IN MCBUS_NODE_TYPE_STR");
		break;
	case MCBUS_TYPE_UNK:
		panic ("UNKNOWN TYPE IN MCBUS_NODE_TYPE_STR");
		break;
	case MCBUS_TYPE_MEM:
		return ("Memory");
	case MCBUS_TYPE_CPU:
		return ("CPU");
	case MCBUS_TYPE_PCI:
		return ("PCI Bridge");
	default:
		panic("REALLY UNKNOWN (%x) TYPE IN MCBUS_NODE_TYPE_STR", type);
		break;
	}
}
