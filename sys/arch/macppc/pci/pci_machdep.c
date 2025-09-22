/*	$OpenBSD: pci_machdep.c,v 1.7 2016/07/04 09:30:18 mpi Exp $	*/

/*
 * Copyright (c) 2013 Martin Pieuchot
 * Copyright (c) 1997 Per Fogelstrom
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

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>


struct powerpc_bus_dma_tag pci_bus_dma_tag = {
	NULL,
	_dmamap_create,
	_dmamap_destroy,
	_dmamap_load,
	_dmamap_load_mbuf,
	_dmamap_load_uio,
	_dmamap_load_raw,
	_dmamap_unload,
	_dmamap_sync,
	_dmamem_alloc,
	_dmamem_alloc_range,
	_dmamem_free,
	_dmamem_map,
	_dmamem_unmap,
	_dmamem_mmap
};

void
pci_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
pci_bus_maxdevs(pci_chipset_tag_t pc, int busno)
{
	return (32);
}

pcitag_t
pci_make_tag(pci_chipset_tag_t pc, int b, int d, int f)
{
	struct ofw_pci_register reg;
	pcitag_t tag;
	int node, busrange[2];

	if (pc->busnode[b])
		return PCITAG_CREATE(0, b, d, f);

	node = pc->pc_node;

	/*
	 * Because ht(4) controller nodes do not have a "bus-range"
	 * property,  we need to start iterating from one of their
	 * PCI bridge nodes to be able to find our devices.
	 */
	if (OF_getprop(node, "bus-range", &busrange, sizeof(busrange)) < 0)
		node = OF_child(pc->pc_node);

	for (; node; node = OF_peer(node)) {
		/*
		 * Check for PCI-PCI bridges.  If the device we want is
		 * in the bus-range for that bridge, work our way down.
		 */
		while ((OF_getprop(node, "bus-range", &busrange,
			sizeof(busrange)) == sizeof(busrange)) &&
			(b >= busrange[0] && b <= busrange[1])) {
			node = OF_child(node);
		}

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) < sizeof(reg))
			continue;

		if (b != OFW_PCI_PHYS_HI_BUS(reg.phys_hi))
			continue;
		if (d != OFW_PCI_PHYS_HI_DEVICE(reg.phys_hi))
			continue;
		if (f != OFW_PCI_PHYS_HI_FUNCTION(reg.phys_hi))
			continue;

		tag = PCITAG_CREATE(node, b, d, f);

		return (tag);
	}

	return (PCITAG_CREATE(-1, b, d, f));
}

void
pci_decompose_tag(pci_chipset_tag_t pc, pcitag_t tag, int *b, int *d, int *f)
{
	if (b != NULL)
		*b = PCITAG_BUS(tag);
	if (d != NULL)
		*d = PCITAG_DEV(tag);
	if (f != NULL)
		*f = PCITAG_FUN(tag);
}

int
pci_conf_size(pci_chipset_tag_t pc, pcitag_t tag)
{
	return (PCI_CONFIG_SPACE_SIZE);
}

pcireg_t
pci_conf_read(pci_chipset_tag_t pc, pcitag_t tag, int reg)
{
        if (PCITAG_NODE(tag) != -1)
		return (*(pc)->pc_conf_read)(pc->pc_conf_v, tag, reg);

        return ((pcireg_t)~0);
}

void
pci_conf_write(pci_chipset_tag_t pc, pcitag_t tag, int reg, pcireg_t data)
{
        if (PCITAG_NODE(tag) != -1)
		(*(pc)->pc_conf_write)(pc->pc_conf_v, tag, reg, data);
}

int
pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct ofw_pci_register reg;
	int node = PCITAG_NODE(pa->pa_tag);
	int intr[4], len;

	if (OF_getprop(node, "reg", &reg, sizeof(reg)) < sizeof(reg))
		return (ENODEV);

	/* Try to get the old Apple OFW interrupt property first. */
	len = OF_getprop(node, "AAPL,interrupts", &intr, sizeof(intr));
	if (len == sizeof(intr[0]))
		goto found;

	len = OF_getprop(node, "interrupts", intr, sizeof(intr));
	if (len < sizeof(intr[0]))
		return (ENODEV);

	reg.size_hi = intr[0];
	if (ofw_intr_map(OF_parent(node), (uint32_t *)&reg, intr)) {
		/*
		 * This can fail on some machines where the parent's
		 * node doesn't have any "interrupt-map" and friends.
		 *
		 * In this case just trust what we got in "interrupts".
		 */
	}

found:
	*ihp = intr[0];

	return (0);
}

int
pci_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	return (-1);
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

	snprintf(str, sizeof(str), "irq %ld", ih);

	return (str);
}

void *
pci_intr_establish(pci_chipset_tag_t pc, pci_intr_handle_t ih, int lvl,
    int (*func)(void *), void *arg, const char *what)
{
	return (*intr_establish_func)(pc, ih, IST_LEVEL, lvl, func, arg, what);
}

void
pci_intr_disestablish(pci_chipset_tag_t pc, void *cookie)
{
	(*intr_disestablish_func)(pc, cookie);
}

int
pci_ether_hw_addr(pci_chipset_tag_t pc, uint8_t *oaddr)
{
	uint8_t laddr[6];
	int node, len;

	node = OF_finddevice("enet");
	len = OF_getprop(node, "local-mac-address", laddr, sizeof(laddr));
	if (sizeof(laddr) == len) {
		memcpy(oaddr, laddr, sizeof(laddr));
		return (1);
	}

	oaddr[0] = oaddr[1] = oaddr[2] = 0xff;
	oaddr[3] = oaddr[4] = oaddr[5] = 0xff;

	return (0);
}

int
ofw_enumerate_pcibus(struct pci_softc *sc,
    int (*match)(struct pci_attach_args *), struct pci_attach_args *pap)
{
	pci_chipset_tag_t pc = sc->sc_pc;
	struct ofw_pci_register reg;
	int len, node, b, d, f, ret;
	uint32_t val = 0;
	char compat[32];
	pcireg_t bhlcr;
	pcitag_t tag;

	if (sc->sc_bridgetag)
		node = PCITAG_NODE(*sc->sc_bridgetag);
	else
		node = pc->pc_node;

	/* The AGP bridge is not in the device-tree. */
	len = OF_getprop(node, "compatible", compat, sizeof(compat));
	if (len > 0 && strcmp(compat, "u3-agp") == 0) {
		tag = PCITAG_CREATE(0, sc->sc_bus, 11, 0);
		ret = pci_probe_device(sc, tag, match, pap);
		if (match != NULL && ret != 0)
			return (ret);
	}

	/*
	 * An HT-PCI bridge is needed for interrupt mapping, attach it first
	 */
	if (len > 0 && strcmp(compat, "u3-ht") == 0) {
		int snode;

		for (snode = OF_child(node); snode; snode = OF_peer(snode)) {
			val = 0;
			if ((OF_getprop(snode, "shasta-interrupt-sequencer",
			    &val, sizeof(val)) < sizeof(val)) || val != 1)
				continue;

			if (OF_getprop(snode, "reg", &reg, sizeof(reg))
			    < sizeof(reg))
				continue;

			b = OFW_PCI_PHYS_HI_BUS(reg.phys_hi);
			d = OFW_PCI_PHYS_HI_DEVICE(reg.phys_hi);
			f = OFW_PCI_PHYS_HI_FUNCTION(reg.phys_hi);

			tag = PCITAG_CREATE(snode, b, d, f);

			bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
			if (PCI_HDRTYPE_TYPE(bhlcr) > 2)
				continue;

			ret = pci_probe_device(sc, tag, match, pap);
			if (match != NULL && ret != 0)
				return (ret);
		}
	}

	for (node = OF_child(node); node; node = OF_peer(node)) {
		if (OF_getprop(node, "reg", &reg, sizeof(reg)) < sizeof(reg))
			continue;

		/*
		 * Skip HT-PCI bridge, it has been attached before.
		 */
		val = 0;
		if ((OF_getprop(node, "shasta-interrupt-sequencer", &val,
		    sizeof(val)) >= sizeof(val)) && val == 1)
			continue;

		b = OFW_PCI_PHYS_HI_BUS(reg.phys_hi);
		d = OFW_PCI_PHYS_HI_DEVICE(reg.phys_hi);
		f = OFW_PCI_PHYS_HI_FUNCTION(reg.phys_hi);

		tag = PCITAG_CREATE(node, b, d, f);

		bhlcr = pci_conf_read(pc, tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_TYPE(bhlcr) > 2)
			continue;

		ret = pci_probe_device(sc, tag, match, pap);
		if (match != NULL && ret != 0)
			return (ret);
	}

	return (0);
}

int
ofw_intr_map(int node, uint32_t *addr, uint32_t *intr)
{
	uint32_t imap[144], mmask[8], *mp, *mp1;
	uint32_t acells, icells, mcells;
	int ilen, mlen, i, step = 0;
	int parent;

	ilen = OF_getprop(node, "interrupt-map", imap, sizeof(imap));
	mlen = OF_getprop(node, "interrupt-map-mask", mmask, sizeof(mmask));
	if (ilen < 0 || mlen < 0)
		return (-1);

	if ((OF_getprop(node, "#address-cells", &acells, 4) < 0) ||
	    (OF_getprop(node, "#interrupt-cells", &icells, 4) < 0))
		return (-1);

	mcells = acells + icells;
	if (mcells != (mlen / sizeof(mmask[0])))
		return (-1);

	for (i = 0; i < mcells; i++)
		addr[i] &= mmask[i];

	/* interrupt-map is formatted as follows
	 * int * #address-cells, int * #interrupt-cells, int, int, int
	 * eg
	 * address-cells = 3
	 * interrupt-cells = 1
	 * 00001000 00000000 00000000 00000000 ff911258 00000034 00000001
	 * 00001800 00000000 00000000 00000000 ff911258 00000035 00000001
	 * 00002000 00000000 00000000 00000000 ff911258 00000036 00000001
	 * | address cells          | | intr | |node| | irq  | |edge/level|
	 *                            | cells|        | interrupt cells   |
	 *                                            | of node           |
	 * or at least something close to that.
	 */
	for (mp = imap; ilen > mlen; mp += step) {
		mp1 = mp + mcells;
		parent = *mp1;

		if (bcmp(mp, addr, mlen) == 0) {
			char ic[20];

			/*
			 * If we have a match and the parent is not an
			 * interrupt controller continue recursively.
			 */
			if (OF_getprop(parent, "interrupt-controller", ic, 20))
				return ofw_intr_map(parent, &mp1[1], intr);

			*intr = mp1[1];
			return (0);
		}

		if (OF_getprop(parent, "#address-cells", &acells, 4) < 0)
			acells = 0;
		if (OF_getprop(parent, "#interrupt-cells", &icells, 4) < 0)
			break;

		step = mcells + 1 + acells + icells;
		ilen -= step * sizeof(imap[0]);
	}

	return (-1);
}
