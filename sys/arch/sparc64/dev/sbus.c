/*	$OpenBSD: sbus.c,v 1.47 2024/03/29 21:29:33 miod Exp $	*/
/*	$NetBSD: sbus.c,v 1.46 2001/10/07 20:30:41 eeh Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sbus.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Copyright (c) 1999 Eduardo Horvath
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


/*
 * SBus stuff.
 */

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/extent.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/reboot.h>

#include <machine/bus.h>
#include <sparc64/sparc64/cache.h>
#include <sparc64/dev/iommureg.h>
#include <sparc64/dev/iommuvar.h>
#include <sparc64/dev/sbusreg.h>
#include <sparc64/dev/starfire.h>
#include <dev/sbus/sbusvar.h>
#include <dev/sbus/xboxvar.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/openfirm.h>
#include <machine/sparc64.h>

#ifdef DEBUG
#define SDB_DVMA	0x1
#define SDB_INTR	0x2
#define SDB_CHILD	0x4
int sbus_debug = 0;
#define DPRINTF(l, s)   do { if (sbus_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)
#endif

bus_space_tag_t sbus_alloc_bustag(struct sbus_softc *, int);
bus_dma_tag_t sbus_alloc_dma_tag(struct sbus_softc *, bus_dma_tag_t);
int sbus_get_intr(struct sbus_softc *, int,
    struct sbus_intr **, int *, int);
int sbus_overtemp(void *);
int _sbus_bus_map(bus_space_tag_t, bus_space_tag_t,
    bus_addr_t,		/*offset*/
    bus_size_t,		/*size*/
    int,		/*flags*/
    bus_space_handle_t *);
void *sbus_intr_establish(bus_space_tag_t, bus_space_tag_t,
    int,		/*SBus interrupt level*/
    int,		/*`device class' priority*/
    int,		/*flags*/
    int (*)(void *),	/*handler*/
    void *,		/*handler arg*/
    const char *);	/*what*/
void	sbus_attach_common(struct sbus_softc *, int, int);

/* autoconfiguration driver */
void	sbus_mb_attach(struct device *, struct device *, void *);
void	sbus_xbox_attach(struct device *, struct device *, void *);
int	sbus_mb_match(struct device *, void *, void *);
int	sbus_xbox_match(struct device *, void *, void *);

const struct cfattach sbus_mb_ca = {
	sizeof(struct sbus_softc), sbus_mb_match, sbus_mb_attach
};

const struct cfattach sbus_xbox_ca = {
	sizeof(struct sbus_softc), sbus_xbox_match, sbus_xbox_attach
};

struct cfdriver sbus_cd = {
	NULL, "sbus", DV_DULL
};

/*
 * DVMA routines
 */
int sbus_dmamap_create(bus_dma_tag_t, bus_dma_tag_t, bus_size_t, int,
    bus_size_t, bus_size_t, int, bus_dmamap_t *);

/*
 * Child devices receive the SBus interrupt level in their attach
 * arguments. We translate these to CPU IPLs using the following
 * tables. Note: obio bus interrupt levels are identical to the
 * processor IPL.
 *
 * The second set of tables is used when the SBus interrupt level
 * cannot be had from the PROM as an `interrupt' property. We then
 * fall back on the `intr' property which contains the CPU IPL.
 */

/* Translate SBus interrupt level to processor IPL */
static int intr_sbus2ipl_4u[] = {
	0, 2, 3, 5, 7, 9, 11, 13
};

/*
 * This value is or'ed into the attach args' interrupt level cookie
 * if the interrupt level comes from an `intr' property, i.e. it is
 * not an SBus interrupt level.
 */
#define SBUS_INTR_COMPAT	0x80000000


/*
 * Print the location of some sbus-attached device (called just
 * before attaching that device).  If `sbus' is not NULL, the
 * device was found but not configured; print the sbus as well.
 * Return UNCONF (config_find ignores this if the device was configured).
 */
int
sbus_print(void *args, const char *busname)
{
	struct sbus_attach_args *sa = args;
	char *class;
	int i;

	if (busname != NULL) {
		printf("\"%s\" at %s", sa->sa_name, busname);
		class = getpropstring(sa->sa_node, "device_type");
		if (*class != '\0')
			printf(" class %s", class);
	}
	printf(" slot %ld offset 0x%lx", (long)sa->sa_slot, 
	       (u_long)sa->sa_offset);
	for (i = 0; i < sa->sa_nintr; i++) {
		struct sbus_intr *sbi = &sa->sa_intr[i];

		printf(" vector %lx ipl %ld", 
		       (u_long)sbi->sbi_vec, 
		       (long)INTLEV(sbi->sbi_pri));
	}
	return (UNCONF);
}

int
sbus_mb_match(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct mainbus_attach_args *ma = aux;

	return (strcmp(cf->cf_driver->cd_name, ma->ma_name) == 0);
}

int
sbus_xbox_match(struct device *parent, void *vcf, void *aux)
{
	struct xbox_softc *xsc = (struct xbox_softc *)parent;

	/* Prevent multiple attachments */
	if (xsc->sc_attached == 0) {
		xsc->sc_attached = 1;
		return (1);
	}

	return (0);
}

void
sbus_xbox_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_softc *sc = (struct sbus_softc *)self;
	struct xbox_softc *xsc = (struct xbox_softc *)parent;
	struct sbus_softc *sbus = (struct sbus_softc *)parent->dv_parent;
	struct xbox_attach_args *xa = aux;
	int node = xa->xa_node;

	sc->sc_master = sbus->sc_master;

	sc->sc_bustag = xa->xa_bustag;
	sc->sc_dmatag = sbus_alloc_dma_tag(sc, xa->xa_dmatag);

	/*
	 * Parent has already done the address translation computations.
	 */
	sc->sc_nrange = xsc->sc_nrange;
	sc->sc_range = xsc->sc_range;

	/*
	 * Record clock frequency for synchronous SCSI.
	 * IS THIS THE CORRECT DEFAULT??
	 */
	sc->sc_clockfreq = getpropint(node, "clock-frequency", 25*1000*1000);
	printf(": %s MHz\n", clockfreq(sc->sc_clockfreq));

	sbus_attach_common(sc, node, 1);
}

void
sbus_mb_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_softc *sc = (struct sbus_softc *)self;
	struct mainbus_attach_args *ma = aux;
	int node = ma->ma_node;
	struct intrhand *ih;
	int error;
	struct sysioreg *sysio;
	char buf[32];
	char *name;

	sc->sc_master = sc;

	sc->sc_bustag = ma->ma_bustag;

	/* Find interrupt group no */
	sc->sc_ign = ma->ma_interrupts[0] & INTMAP_IGN;

	/*
	 * Collect address translations from the OBP.
	 */
	error = getprop(node, "ranges", sizeof(struct sbus_range),
			 &sc->sc_nrange, (void **)&sc->sc_range);
	if (error)
		panic("%s: error getting ranges property", sc->sc_dev.dv_xname);

	/*
	 * Record clock frequency for synchronous SCSI.
	 * IS THIS THE CORRECT DEFAULT??
	 */
	sc->sc_clockfreq = getpropint(node, "clock-frequency", 25*1000*1000);
	printf(": clock = %s MHz\n", clockfreq(sc->sc_clockfreq));

	bus_space_map(sc->sc_bustag,
	    ma->ma_address[0], sizeof(struct sysioreg),
	    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_bh);
	sysio = bus_space_vaddr(sc->sc_bustag, sc->sc_bh);

	/* initialize the IOMMU */

	/* punch in our copies */
	sc->sc_is.is_bustag = sc->sc_bustag;
	bus_space_subregion(sc->sc_bustag, sc->sc_bh,
	    offsetof(struct sysioreg, sys_iommu),
	    sizeof(struct iommureg), &sc->sc_is.is_iommu);

	/* initialize our strbuf_ctl */
	sc->sc_is.is_sb[0] = &sc->sc_sb;
	if (bus_space_subregion(sc->sc_bustag, sc->sc_bh,
	    offsetof(struct sysioreg, sys_strbuf),
	    sizeof(struct iommu_strbuf), &sc->sc_sb.sb_sb) == 0) {
		/* point sb_flush to our flush buffer */
		sc->sc_sb.sb_flush = &sc->sc_flush;
		sc->sc_sb.sb_bustag = sc->sc_bustag;
	} else
		sc->sc_sb.sb_flush = NULL;

	/* give us a nice name.. */
	name = (char *)malloc(32, M_DEVBUF, M_NOWAIT);
	if (name == 0)
		panic("couldn't malloc iommu name");
	snprintf(name, 32, "%s dvma", sc->sc_dev.dv_xname);

	printf("%s: ", sc->sc_dev.dv_xname);
	iommu_init(name, &iommu_hw_default, &sc->sc_is, 0, -1);

	/* Initialize Starfire PC interrupt translation. */
	if (OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,Ultra-Enterprise-10000") == 0)
		starfire_pc_ittrans_init(ma->ma_upaid);

	/* Enable the over temp intr */
	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ih == NULL)
		panic("couldn't malloc intrhand");
	ih->ih_map = &sysio->therm_int_map;
	ih->ih_clr = NULL; /* &sysio->therm_clr_int; */
	ih->ih_fun = sbus_overtemp;
	ih->ih_pil = 1;
	ih->ih_number = INTVEC(*(ih->ih_map));
	strlcpy(ih->ih_name, sc->sc_dev.dv_xname, sizeof(ih->ih_name));
	intr_establish(ih);
	*(ih->ih_map) |= INTMAP_V;
	
	/*
	 * Note: the stupid SBus IOMMU ignores the high bits of an address, so a
	 * NULL DMA pointer will be translated by the first page of the IOTSB.
	 * To avoid bugs we'll alloc and ignore the first entry in the IOTSB.
	 */
	{
		u_long dummy;

		if (extent_alloc_subregion(sc->sc_is.is_dvmamap,
		    sc->sc_is.is_dvmabase, sc->sc_is.is_dvmabase + NBPG, NBPG,
		    NBPG, 0, 0, EX_NOWAIT | EX_BOUNDZERO,
		    (u_long *)&dummy) != 0)
			panic("sbus iommu: can't toss first dvma page");
	}

	sc->sc_dmatag = sbus_alloc_dma_tag(sc, ma->ma_dmatag);

	sbus_attach_common(sc, node, 0);
}

/*
 * Attach an SBus (main part).
 */
void
sbus_attach_common(struct sbus_softc *sc, int node, int indirect)
{
	bus_space_tag_t sbt;
	struct sbus_attach_args sa;
	int node0;

	/* Setup interrupt translation tables */
	sc->sc_intr2ipl = intr_sbus2ipl_4u;

	sbt = sbus_alloc_bustag(sc, indirect);

	/*
	 * Get the SBus burst transfer size if burst transfers are supported
	 */
	sc->sc_burst = getpropint(node, "burst-sizes", 0);

	/*
	 * Loop through ROM children, fixing any relative addresses
	 * and then configuring each device.
	 * `specials' is an array of device names that are treated
	 * specially:
	 */
	node0 = firstchild(node);
	for (node = node0; node; node = nextsibling(node)) {
		if (!checkstatus(node))
			continue;

		if (sbus_setup_attach_args(sc, sbt, sc->sc_dmatag,
					   node, &sa) != 0) {
			DPRINTF(SDB_CHILD,
			    ("sbus_attach: %s: incomplete\n",
			    getpropstring(node, "name")));
			continue;
		}
		(void) config_found(&sc->sc_dev, (void *)&sa, sbus_print);
		sbus_destroy_attach_args(&sa);
	}
}

int
sbus_setup_attach_args(struct sbus_softc *sc, bus_space_tag_t bustag,
    bus_dma_tag_t dmatag, int node, struct sbus_attach_args *sa)
{
	int	error;
	int	n;

	bzero(sa, sizeof(struct sbus_attach_args));
	error = getprop(node, "name", 1, &n, (void **)&sa->sa_name);
	if (error != 0)
		return (error);
	sa->sa_name[n] = '\0';

	sa->sa_bustag = bustag;
	sa->sa_dmatag = dmatag;
	sa->sa_node = node;
	sa->sa_frequency = sc->sc_clockfreq;

	error = getprop(node, "reg", sizeof(struct sbus_reg),
			 &sa->sa_nreg, (void **)&sa->sa_reg);
	if (error != 0) {
		char buf[32];
		if (error != ENOENT ||
		    !node_has_property(node, "device_type") ||
		    strcmp(getpropstringA(node, "device_type", buf),
			   "hierarchical") != 0)
			return (error);
	}
	for (n = 0; n < sa->sa_nreg; n++) {
		/* Convert to relative addressing, if necessary */
		u_int32_t base = sa->sa_reg[n].sbr_offset;
		if (SBUS_ABS(base)) {
			sa->sa_reg[n].sbr_slot = SBUS_ABS_TO_SLOT(base);
			sa->sa_reg[n].sbr_offset = SBUS_ABS_TO_OFFSET(base);
		}
	}

	if ((error = sbus_get_intr(sc, node, &sa->sa_intr, &sa->sa_nintr,
	    sa->sa_slot)) != 0)
		return (error);

	error = getprop(node, "address", sizeof(u_int32_t),
			 &sa->sa_npromvaddrs, (void **)&sa->sa_promvaddrs);
	if (error != 0 && error != ENOENT)
		return (error);

	return (0);
}

void
sbus_destroy_attach_args(struct sbus_attach_args *sa)
{
	free(sa->sa_name, M_DEVBUF, 0);
	free(sa->sa_reg, M_DEVBUF, 0);
	free(sa->sa_intr, M_DEVBUF, 0);
	free((void *)sa->sa_promvaddrs, M_DEVBUF, 0);

	bzero(sa, sizeof(struct sbus_attach_args)); /*DEBUG*/
}


int
_sbus_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t addr,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	struct sbus_softc *sc = t->cookie;
	int64_t slot = BUS_ADDR_IOSPACE(addr);
	int64_t offset = BUS_ADDR_PADDR(addr);
	int i;

	if (t->parent == NULL || t->parent->sparc_bus_map == NULL) {
		printf("\n_psycho_bus_map: invalid parent");
		return (EINVAL);
	}

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		return ((*t->parent->sparc_bus_map)(t, t0, addr,
					size, flags, hp));
	}

	for (i = 0; i < sc->sc_nrange; i++) {
		bus_addr_t paddr;

		if (sc->sc_range[i].cspace != slot)
			continue;

		/* We've found the connection to the parent bus */
		paddr = sc->sc_range[i].poffset + offset;
		paddr |= ((bus_addr_t)sc->sc_range[i].pspace << 32);
		DPRINTF(SDB_DVMA, ("_sbus_bus_map: mapping paddr "
			"slot %lx offset %lx poffset %lx paddr %lx\n",
		    (long)slot, (long)offset, (long)sc->sc_range[i].poffset,
		    (long)paddr));
		return ((*t->parent->sparc_bus_map)(t, t0, paddr,
					size, flags, hp));
	}

	return (EINVAL);
}

bus_addr_t
sbus_bus_addr(bus_space_tag_t t, u_int btype, u_int offset)
{
	bus_addr_t baddr = ~(bus_addr_t)0;
	int slot = btype;
	struct sbus_softc *sc = t->cookie;
	int i;

	for (i = 0; i < sc->sc_nrange; i++) {
		if (sc->sc_range[i].cspace != slot)
			continue;

		baddr = sc->sc_range[i].poffset + offset;
		baddr |= (bus_addr_t)sc->sc_range[i].pspace << 32;
	}

	return (baddr);
}

/*
 * Handle an overtemp situation.
 *
 * SPARCs have temperature sensors which generate interrupts
 * if the machine's temperature exceeds a certain threshold.
 * This handles the interrupt and powers off the machine.
 * The same needs to be done to PCI controller drivers.
 */
int
sbus_overtemp(void *arg)
{
	/* Should try a clean shutdown first */
	printf("DANGER: OVER TEMPERATURE detected\nShutting down...\n");
	delay(20);
	prsignal(initprocess, SIGUSR2);
	return (1);
}

/*
 * Get interrupt attributes for an SBus device.
 */
int
sbus_get_intr(struct sbus_softc *sc, int node, struct sbus_intr **ipp, int *np,
    int slot)
{
	int *ipl;
	int n, i;
	char buf[32];

	/*
	 * The `interrupts' property contains the SBus interrupt level.
	 */
	ipl = NULL;
	if (getprop(node, "interrupts", sizeof(int), np, (void **)&ipl) == 0) {
		struct sbus_intr *ip;
		int pri;

		/* Default to interrupt level 2 -- otherwise unused */
		pri = INTLEVENCODE(2);

		/* Change format to an `struct sbus_intr' array */
		ip = mallocarray(*np, sizeof(struct sbus_intr), M_DEVBUF,
		    M_NOWAIT);
		if (ip == NULL)
			return (ENOMEM);

		/*
		 * Now things get ugly.  We need to take this value which is
		 * the interrupt vector number and encode the IPL into it
		 * somehow. Luckily, the interrupt vector has lots of free
		 * space and we can easily stuff the IPL in there for a while.
		 */
		getpropstringA(node, "device_type", buf);
		if (!buf[0])
			getpropstringA(node, "name", buf);

		for (i = 0; intrmap[i].in_class; i++) 
			if (strcmp(intrmap[i].in_class, buf) == 0) {
				pri = INTLEVENCODE(intrmap[i].in_lev);
				break;
			}

		/*
		 * SBus card devices need the slot number encoded into
		 * the vector as this is generally not done.
		 */
		if ((ipl[0] & INTMAP_OBIO) == 0)
			pri |= slot << 3;

		for (n = 0; n < *np; n++) {
			/* 
			 * We encode vector and priority into sbi_pri so we 
			 * can pass them as a unit.  This will go away if 
			 * sbus_establish ever takes an sbus_intr instead 
			 * of an integer level.
			 * Stuff the real vector in sbi_vec.
			 */

			ip[n].sbi_pri = pri | ipl[n];
			ip[n].sbi_vec = ipl[n];
		}
		free(ipl, M_DEVBUF, 0);
		*ipp = ip;
	}
	
	return (0);
}


/*
 * Install an interrupt handler for an SBus device.
 */
void *
sbus_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int pri, int level,
    int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct sbus_softc *sc = t->cookie;
	struct sysioreg *sysio;
	struct intrhand *ih;
	volatile u_int64_t *map = NULL;
	volatile u_int64_t *clr = NULL;
	int ipl;
	long vec = pri; 

	/* Pick the master SBus as all do not have IOMMU registers */
	sc = sc->sc_master;

	sysio = bus_space_vaddr(sc->sc_bustag, sc->sc_bh);

	if ((flags & BUS_INTR_ESTABLISH_SOFTINTR) != 0)
		ipl = 1 << vec;
	else if ((vec & SBUS_INTR_COMPAT) != 0)
		ipl = 1 << (vec & ~SBUS_INTR_COMPAT);
	else {
		/* Decode and remove IPL */
		ipl = level;
		if (ipl == IPL_NONE)
			ipl = 1 << INTLEV(vec);
		if (ipl == IPL_NONE) {
			printf("ERROR: no IPL, setting IPL 2.\n");
			ipl = 2;
		}
		vec = INTVEC(vec);
		DPRINTF(SDB_INTR,
		    ("\nsbus: intr[%ld]%lx: %lx\nHunting for IRQ...\n",
		    (long)ipl, (long)vec, (u_long)intrlev[vec]));
		if ((vec & INTMAP_OBIO) == 0) {
			/* We're in an SBus slot */
			/* Register the map and clear intr registers */
			bus_space_handle_t maph;
			int slot = INTSLOT(pri);

			map = &(&sysio->sbus_slot0_int)[slot];
			clr = &sysio->sbus0_clr_int[vec];
#ifdef DEBUG
			if (sbus_debug & SDB_INTR) {
				int64_t intrmap = *map;
				
				printf("SBus %lx IRQ as %llx in slot %d\n", 
				       (long)vec, (long long)intrmap, slot);
				printf("\tmap addr %p clr addr %p\n",
				    map, clr);
			}
#endif
			/* Enable the interrupt */
			vec |= INTMAP_V;
			/* Insert IGN */
			vec |= sc->sc_ign;
			/*
			 * This would be cleaner if the underlying interrupt
			 * infrastructure took a bus tag/handle pair.  Even
			 * if not, the following could be done with a write
			 * to the appropriate offset from sc->sc_bustag and
			 * sc->sc_bh.
			 */
			bus_space_map(sc->sc_bustag, (bus_addr_t)map, 8,
			    BUS_SPACE_MAP_PROMADDRESS, &maph);
			bus_space_write_8(sc->sc_bustag, maph, 0, vec);
		} else {
			bus_space_handle_t maph;
			volatile int64_t *intrptr = &sysio->scsi_int_map;
			int64_t intrmap = 0;
			int i;

			/* Insert IGN */
			vec |= sc->sc_ign;
			for (i = 0; &intrptr[i] <=
			    (int64_t *)&sysio->reserved_int_map &&
			    INTVEC(intrmap = intrptr[i]) != INTVEC(vec); i++)
				;
			if (INTVEC(intrmap) == INTVEC(vec)) {
				DPRINTF(SDB_INTR,
				    ("OBIO %lx IRQ as %lx in slot %d\n", 
				    vec, (long)intrmap, i));
				/* Register the map and clear intr registers */
				map = &intrptr[i];
				intrptr = (int64_t *)&sysio->scsi_clr_int;
				clr = &intrptr[i];
				/* Enable the interrupt */
				intrmap |= INTMAP_V;
				/*
				 * This would be cleaner if the underlying
				 * interrupt infrastructure took a bus tag/
				 * handle pair.  Even if not, the following
				 * could be done with a write to the
				 * appropriate offset from sc->sc_bustag and
				 * sc->sc_bh.
				 */
				bus_space_map(sc->sc_bustag,
				    (bus_addr_t)map, 8,
				    BUS_SPACE_MAP_PROMADDRESS, &maph);
				bus_space_write_8(sc->sc_bustag, maph, 0,
				    (u_long)intrmap);
			} else
				panic("IRQ not found!");
		}
	}
#ifdef DEBUG
	if (sbus_debug & SDB_INTR) { long i; for (i = 0; i < 400000000; i++); }
#endif

	ih = bus_intr_allocate(t0, handler, arg, vec, ipl, map, clr, what);
	if (ih == NULL)
		return (ih);

	intr_establish(ih);

	return (ih);
}

bus_space_tag_t
sbus_alloc_bustag(struct sbus_softc *sc, int indirect)
{
	struct sparc_bus_space_tag *sbt;

	sbt = malloc(sizeof(*sbt), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sbt == NULL)
		return (NULL);

	strlcpy(sbt->name, sc->sc_dev.dv_xname, sizeof(sbt->name));
	sbt->cookie = sc;
	if (indirect)
		sbt->parent = sc->sc_bustag->parent;
	else
		sbt->parent = sc->sc_bustag;
	sbt->default_type = SBUS_BUS_SPACE;
	sbt->asi = ASI_PRIMARY;
	sbt->sasi = ASI_PRIMARY;
	sbt->sparc_bus_map = _sbus_bus_map;
	sbt->sparc_bus_mmap = sc->sc_bustag->sparc_bus_mmap;
	sbt->sparc_intr_establish = sbus_intr_establish;
	return (sbt);
}


bus_dma_tag_t
sbus_alloc_dma_tag(struct sbus_softc *sc, bus_dma_tag_t psdt)
{
	bus_dma_tag_t sdt;

	sdt = (bus_dma_tag_t)malloc(sizeof(struct sparc_bus_dma_tag),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sdt == NULL)
		/* Panic? */
		return (psdt);

	sdt->_cookie = sc;
	sdt->_parent = psdt;
	sdt->_dmamap_create	= sbus_dmamap_create;
	sdt->_dmamap_destroy	= iommu_dvmamap_destroy;
	sdt->_dmamap_load	= iommu_dvmamap_load;
	sdt->_dmamap_load_raw	= iommu_dvmamap_load_raw;
	sdt->_dmamap_unload	= iommu_dvmamap_unload;
	sdt->_dmamap_sync	= iommu_dvmamap_sync;
	sdt->_dmamem_alloc	= iommu_dvmamem_alloc;
	sdt->_dmamem_free	= iommu_dvmamem_free;
	return (sdt);
}

int
sbus_dmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamp)
{
	struct sbus_softc *sc = t->_cookie;

	/* Disallow DMA on secondary SBuses for now */
	if (sc != sc->sc_master)
		return (EINVAL);

        return (iommu_dvmamap_create(t, t0, &sc->sc_sb, size, nsegments,
	    maxsegsz, boundary, flags, dmamp));
}
