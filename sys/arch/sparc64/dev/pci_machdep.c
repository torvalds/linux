/*	$OpenBSD: pci_machdep.c,v 1.53 2025/06/28 11:34:21 miod Exp $	*/
/*	$NetBSD: pci_machdep.c,v 1.22 2001/07/20 00:07:13 eeh Exp $	*/

/*
 * Copyright (c) 1999, 2000 Matthew R. Green
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
 */

/*
 * functions expected by the MI PCI code.
 */

#ifdef DEBUG
#define SPDB_CONF	0x01
#define SPDB_INTR	0x04
#define SPDB_INTMAP	0x08
#define SPDB_PROBE	0x20
int sparc_pci_debug = 0x0;
#define DPRINTF(l, s)	do { if (sparc_pci_debug & l) printf s; } while (0)
#else
#define DPRINTF(l, s)	do { } while (0)
#endif

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/malloc.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/ofw_pci.h>

#include <sparc64/dev/iommuvar.h>
#include <sparc64/dev/psychoreg.h>
#include <sparc64/dev/psychovar.h>
#include <sparc64/sparc64/cache.h>

/* this is a base to be copied */
struct sparc_pci_chipset _sparc_pci_chipset = {
	NULL,
};

static int pci_bus_frequency(int node);

/*
 * functions provided to the MI code.
 */

void
pci_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
	/* Don't do anything */
}

int
pci_32bit_dmamap_create(bus_dma_tag_t dt, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamp)
{
	bus_dma_tag_t pdt = dt->_parent;

	CLR(flags, BUS_DMA_64BIT);

	return ((*pdt->_dmamap_create)(pdt, t0, size, nsegments, maxsegsz,
	    boundary, flags, dmamp));
}

int
pci_probe_device_hook(pci_chipset_tag_t pc, struct pci_attach_args *pa)
{
	bus_dma_tag_t dt, pdt;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_RCC &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_RCC_PCIE_PCIX) {
		/*
		 * These PCI bridges only support 40bit DVA, so intercept
		 * bus_dmamap_create so we can clear BUS_DMA_64BIT.
		 */

		dt = malloc(sizeof(*dt), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (dt == NULL)
			panic("%s: could not alloc dma tag", __func__);

		pdt = pa->pa_dmat;

		dt->_parent = pdt;
		dt->_dmamap_create = pci_32bit_dmamap_create;

		pa->pa_dmat = dt;
	}

	return (0);
}

int
pci_bus_maxdevs(pci_chipset_tag_t pc, int busno)
{

	return 32;
}

pcitag_t
pci_make_tag(pci_chipset_tag_t pc, int b, int d, int f)
{
	struct ofw_pci_register reg;
	pcitag_t tag;
	int busrange[2];
	int node, len;
#ifdef DEBUG
	char name[80];
	bzero(name, sizeof(name));
#endif

	if (pc->busnode[b])
		return PCITAG_CREATE(0, b, d, f);

	/* 
	 * Hunt for the node that corresponds to this device 
	 *
	 * We could cache this info in an array in the parent
	 * device... except then we have problems with devices
	 * attached below pci-pci bridges, and we would need to
	 * add special code to the pci-pci bridge to cache this
	 * info.
	 */

	tag = PCITAG_CREATE(-1, b, d, f);

	/*
	 * Traverse all peers until we find the node or we find
	 * the right bridge. 
	 */
	for (node = pc->rootnode; node; node = OF_peer(node)) {

#ifdef DEBUG
		if (sparc_pci_debug & SPDB_PROBE) {
			OF_getprop(node, "name", &name, sizeof(name));
			printf("checking node %x %s\n", node, name);
		}
#endif

		/*
		 * Check for PCI-PCI bridges.  If the device we want is
		 * in the bus-range for that bridge, work our way down.
		 */
		while ((OF_getprop(node, "bus-range", (void *)&busrange,
			sizeof(busrange)) == sizeof(busrange)) &&
			(b >= busrange[0] && b <= busrange[1])) {
			/* Go down 1 level */
			node = OF_child(node);
#ifdef DEBUG
			if (sparc_pci_debug & SPDB_PROBE) {
				OF_getprop(node, "name", &name, sizeof(name));
				printf("going down to node %x %s\n",
					node, name);
			}
#endif
		}

		/* 
		 * We only really need the first `reg' property. 
		 *
		 * For simplicity, we'll query the `reg' when we
		 * need it.  Otherwise we could malloc() it, but
		 * that gets more complicated.
		 */
		len = OF_getproplen(node, "reg");
		if (len < sizeof(reg))
			continue;
		if (OF_getprop(node, "reg", (void *)&reg, sizeof(reg)) != len)
			panic("pci_probe_bus: OF_getprop len botch");

		if (b != OFW_PCI_PHYS_HI_BUS(reg.phys_hi))
			continue;
		if (d != OFW_PCI_PHYS_HI_DEVICE(reg.phys_hi))
			continue;
		if (f != OFW_PCI_PHYS_HI_FUNCTION(reg.phys_hi))
			continue;

		/* Got a match */
		tag = PCITAG_CREATE(node, b, d, f);

		return (tag);
	}
	/* No device found -- return a dead tag */
	return (tag);
}

void
pci_decompose_tag(pci_chipset_tag_t pc, pcitag_t tag, int *bp, int *dp, int *fp)
{

	if (bp != NULL)
		*bp = PCITAG_BUS(tag);
	if (dp != NULL)
		*dp = PCITAG_DEV(tag);
	if (fp != NULL)
		*fp = PCITAG_FUN(tag);
}

static int 
pci_bus_frequency(int node)
{
	int len, bus_frequency;

	len = OF_getproplen(node, "clock-frequency");
	if (len < sizeof(bus_frequency)) {
		DPRINTF(SPDB_PROBE,
		    ("pci_bus_frequency: clock-frequency len %d too small\n",
		     len));
		return 33;
	}
	if (OF_getprop(node, "clock-frequency", &bus_frequency,
		       sizeof(bus_frequency)) != len) {
		DPRINTF(SPDB_PROBE,
		    ("pci_bus_frequency: could not read clock-frequency\n"));
		return 33;
	}
	return bus_frequency / 1000000;
}

int
sparc64_pci_enumerate_bus(struct pci_softc *sc,
    int (*match)(struct pci_attach_args *), struct pci_attach_args *pap)
{
	struct ofw_pci_register reg;
	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t tag;
	pcireg_t class, csr, bhlc, ic;
	int node, b, d, f, ret;
	int bus_frequency, lt, cl, cacheline;
	char name[30];

	if (sc->sc_bridgetag)
		node = PCITAG_NODE(*sc->sc_bridgetag);
	else
		node = pc->rootnode;

	bus_frequency = pci_bus_frequency(node);

	/*
	 * Make sure the cache line size is at least as big as the
	 * ecache line and the streaming cache (64 byte).
	 */
	cacheline = max(cacheinfo.ec_linesize, 64);

	for (node = OF_child(node); node != 0 && node != -1;
	     node = OF_peer(node)) {
		if (!checkstatus(node))
			continue;

		name[0] = name[29] = 0;
		OF_getprop(node, "name", name, sizeof(name));

		if (OF_getprop(node, "class-code", &class, sizeof(class)) != 
		    sizeof(class))
			continue;
		if (OF_getprop(node, "reg", &reg, sizeof(reg)) < sizeof(reg))
			panic("pci_enumerate_bus: \"%s\" regs too small", name);

		b = OFW_PCI_PHYS_HI_BUS(reg.phys_hi);
		d = OFW_PCI_PHYS_HI_DEVICE(reg.phys_hi);
		f = OFW_PCI_PHYS_HI_FUNCTION(reg.phys_hi);

		if (sc->sc_bus != b) {
			printf("%s: WARNING: incorrect bus # for \"%s\" "
			"(%d/%d/%d)\n", sc->sc_dev.dv_xname, name, b, d, f);
			continue;
		}

		tag = PCITAG_CREATE(node, b, d, f);

		/*
		 * Turn on parity and fast-back-to-back for the device.
		 */
		csr = pci_conf_read(pc, tag, PCI_COMMAND_STATUS_REG);
		if (csr & PCI_STATUS_BACKTOBACK_SUPPORT)
			csr |= PCI_COMMAND_BACKTOBACK_ENABLE;
		csr |= PCI_COMMAND_PARITY_ENABLE;
		pci_conf_write(pc, tag, PCI_COMMAND_STATUS_REG, csr);

		/*
		 * Initialize the latency timer register for busmaster
		 * devices to work properly.
		 *   latency-timer = min-grant * bus-freq / 4  (from FreeBSD)
		 * Also initialize the cache line size register.
		 * Solaris anytime sets this register to the value 0x10.
		 */
		bhlc = pci_conf_read(pc, tag, PCI_BHLC_REG);
		ic = pci_conf_read(pc, tag, PCI_INTERRUPT_REG);

		lt = min(PCI_MIN_GNT(ic) * bus_frequency / 4, 255);
		if (lt == 0 || lt < PCI_LATTIMER(bhlc))
			lt = PCI_LATTIMER(bhlc);

		cl = PCI_CACHELINE(bhlc);
		if (cl == 0)
			cl = cacheline;

		bhlc &= ~((PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT) |
			  (PCI_CACHELINE_MASK << PCI_CACHELINE_SHIFT));
		bhlc |= (lt << PCI_LATTIMER_SHIFT) |
			(cl << PCI_CACHELINE_SHIFT);
		pci_conf_write(pc, tag, PCI_BHLC_REG, bhlc);

		ret = pci_probe_device(sc, tag, match, pap);
		if (match != NULL && ret != 0)
			return (ret);
	}

	return (0);
}

int
pci_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	int val = 0;

        if (PCITAG_NODE(tag) != -1)
		val = pc->conf_size(pc, tag);

        return (val);
}

pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
        pcireg_t val = (pcireg_t)~0;

        if (PCITAG_NODE(tag) != -1)
		val = pc->conf_read(pc, tag, reg);

        return (val);
}

void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
        if (PCITAG_NODE(tag) != -1)
		pc->conf_write(pc, tag, reg, data);
}

/*
 * interrupt mapping foo.
 * XXX: how does this deal with multiple interrupts for a device?
 */
int
pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pcitag_t tag = pa->pa_tag;
	int interrupts[4], ninterrupts;
	int len, node = PCITAG_NODE(tag);
	char devtype[30];

	len = OF_getproplen(node, "interrupts");
	if (len < 0 || len < sizeof(interrupts[0])) {
		DPRINTF(SPDB_INTMAP,
			("pci_intr_map: interrupts len %d too small\n", len));
		return (ENODEV);
	}
	if (OF_getprop(node, "interrupts", interrupts,
	    sizeof(interrupts)) != len) {
		DPRINTF(SPDB_INTMAP,
			("pci_intr_map: could not read interrupts\n"));
		return (ENODEV);
	}

	/*
	 * If we have multiple interrupts for a device, choose the one
	 * that corresponds to the PCI function.  This makes the
	 * second PC Card slot on the UltraBook get the right interrupt.
	 */
	ninterrupts = len / sizeof(interrupts[0]);
	if (PCITAG_FUN(pa->pa_tag) < ninterrupts)
		interrupts[0] = interrupts[PCITAG_FUN(pa->pa_tag)];

	if (OF_mapintr(node, &interrupts[0], sizeof(interrupts[0]), 
	    sizeof(interrupts)) < 0) {
		interrupts[0] = -1;
	}
	/* Try to find an IPL for this type of device. */
	if (OF_getprop(node, "device_type", &devtype, sizeof(devtype)) > 0) {
		for (len = 0;  intrmap[len].in_class; len++)
			if (strcmp(intrmap[len].in_class, devtype) == 0) {
				interrupts[0] |= INTLEVENCODE(intrmap[len].in_lev);
				break;
			}
	}

	/* XXXX -- we use the ino.  What if there is a valid IGN? */
	*ihp = interrupts[0];

	if (pa->pa_pc->intr_map) {
		int rv = (*pa->pa_pc->intr_map)(pa, ihp);
		if (rv != 0)
			return (rv);
	}

	KASSERT(PCI_INTR_TYPE(*ihp) == PCI_INTR_INTX);

	return (0);
}

int
pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 ||
	    pci_get_capability(pc, tag, PCI_CAP_MSI, NULL, NULL) == 0)
		return (-1);

	*ihp = PCITAG_OFFSET(pa->pa_tag) | PCI_INTR_MSI;
	return (0);
}

int
pci_intr_map_msix(struct pci_attach_args *pa, int vec, pci_intr_handle_t *ihp)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	pcireg_t reg;

	if (vec & ~PCI_INTR_VEC_MASK)
		return (-1);

	if ((pa->pa_flags & PCI_FLAGS_MSI_ENABLED) == 0 ||
	    pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		return (-1);

	if (vec > PCI_MSIX_MC_TBLSZ(reg))
		return (-1);

	KASSERT(!ISSET(pa->pa_tag, PCI_INTR_TYPE_MASK));
	KASSERT(!ISSET(pa->pa_tag, PCI_INTR_VEC_MASK));

	*ihp = PCI_INTR_MSIX | PCITAG_OFFSET(pa->pa_tag) | vec;
	return (0);
}

int
pci_intr_line(pci_chipset_tag_t pc, pci_intr_handle_t ih)
{
	return (ih);
}

const char *
pci_intr_string(pci_chipset_tag_t pc, pci_intr_handle_t ih)
{
	static char str[16];
	const char *rv = str;

	DPRINTF(SPDB_INTR, ("pci_intr_string: ih %u", ih));
	switch (PCI_INTR_TYPE(ih)) {
	case PCI_INTR_MSIX:
		rv = "msix";
		break;
	case PCI_INTR_MSI:
		rv = "msi";
		break;
	case PCI_INTR_INTX:
		snprintf(str, sizeof str, "ivec 0x%llx", INTVEC(ih));
		break;
	}
	DPRINTF(SPDB_INTR, ("; returning %s\n", rv));

	return (rv);
}

void *
pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *what)
{
	return (pci_intr_establish_cpu(pc, ih, level, NULL, func, arg, what));
}

void *
pci_intr_establish_cpu(pci_chipset_tag_t pc, pci_intr_handle_t ih,
    int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, const char *what)
{
	void *cookie;
	int flags = 0;

	if (level & IPL_MPSAFE) {
		flags |= BUS_INTR_ESTABLISH_MPSAFE;
		level &= ~IPL_MPSAFE;
	}

	DPRINTF(SPDB_INTR, ("pci_intr_establish_cpu: ih %lu; level %d; ci %p",
	    (u_long)ih, level, ci));
	cookie = bus_intr_establish_cpu(pc->bustag, ih, level, flags,
	    ci, func, arg, what);

	DPRINTF(SPDB_INTR, ("; returning handle %p\n", cookie));
	return (cookie);
}

void
pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{

	DPRINTF(SPDB_INTR, ("pci_intr_disestablish: cookie %p\n", cookie));

	/* XXX */
	printf("can't disestablish PCI interrupts yet\n");
}

void
pci_msi_enable(pci_chipset_tag_t pc, pcitag_t tag, bus_addr_t addr, int vec)
{
	pcireg_t reg;
	int off;

	if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
		panic("%s: no msi capability", __func__);

	if (reg & PCI_MSI_MC_C64) {
		pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
		pci_conf_write(pc, tag, off + PCI_MSI_MAU32, 0);
		pci_conf_write(pc, tag, off + PCI_MSI_MD64, vec);
	} else {
		pci_conf_write(pc, tag, off + PCI_MSI_MA, addr);
		pci_conf_write(pc, tag, off + PCI_MSI_MD32, vec);
	}
	pci_conf_write(pc, tag, off, reg | PCI_MSI_MC_MSIE);
}

void
pci_msix_enable(pci_chipset_tag_t pc, pcitag_t tag, bus_space_tag_t memt,
    int vec, bus_addr_t addr, uint32_t data)
{
	bus_space_handle_t memh;
	bus_addr_t base;
	pcireg_t reg, table, type;
	uint32_t ctrl;
	int bir, offset;
	int off, tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		panic("%s: no msix capability", __func__);

	table = pci_conf_read(pc, tag, off + PCI_MSIX_TABLE);
	bir = (table & PCI_MSIX_TABLE_BIR);
	offset = (table & PCI_MSIX_TABLE_OFF);
	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;

	bir = PCI_MAPREG_START + bir * 4;
	type = pci_mapreg_type(pc, tag, bir);
	if (pci_mapreg_info(pc, tag, bir, type, &base, NULL, NULL) ||
	    bus_space_map(memt, base + offset, tblsz * 16, 0, &memh))
		panic("%s: cannot map registers", __func__);

	bus_space_write_4(memt, memh, PCI_MSIX_MA(vec), addr);
	bus_space_write_4(memt, memh, PCI_MSIX_MAU32(vec), addr >> 32);
	bus_space_write_4(memt, memh, PCI_MSIX_MD(vec), data);
	bus_space_barrier(memt, memh, PCI_MSIX_MA(vec), 16,
	    BUS_SPACE_BARRIER_WRITE);
	ctrl = bus_space_read_4(memt, memh, PCI_MSIX_VC(vec));
	bus_space_write_4(memt, memh, PCI_MSIX_VC(vec),
	    ctrl & ~PCI_MSIX_VC_MASK);

	bus_space_unmap(memt, memh, tblsz * 16);

	pci_conf_write(pc, tag, off, reg | PCI_MSIX_MC_MSIXE);
}

int
pci_msix_table_map(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, bus_space_handle_t *memh)
{
	bus_addr_t base;
	pcireg_t reg, table, type;
	int bir, offset;
	int off, tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, &off, &reg) == 0)
		panic("%s: no msix capability", __func__);

	table = pci_conf_read(pc, tag, off + PCI_MSIX_TABLE);
	bir = (table & PCI_MSIX_TABLE_BIR);
	offset = (table & PCI_MSIX_TABLE_OFF);
	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;

	bir = PCI_MAPREG_START + bir * 4;
	type = pci_mapreg_type(pc, tag, bir);
	if (pci_mapreg_info(pc, tag, bir, type, &base, NULL, NULL) ||
	    bus_space_map(memt, base + offset, tblsz * 16, 0, memh))
		return (-1);

	return (0);
}

void
pci_msix_table_unmap(pci_chipset_tag_t pc, pcitag_t tag,
    bus_space_tag_t memt, bus_space_handle_t memh)
{
	pcireg_t reg;
	int tblsz;

	if (pci_get_capability(pc, tag, PCI_CAP_MSIX, NULL, &reg) == 0)
		panic("%s: no msix capability", __func__);

	tblsz = PCI_MSIX_MC_TBLSZ(reg) + 1;

	bus_space_unmap(memt, memh, tblsz * 16);
}
