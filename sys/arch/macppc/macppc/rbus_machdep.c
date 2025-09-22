/*	$OpenBSD: rbus_machdep.c,v 1.13 2013/08/07 07:29:19 mpi Exp $ */
/*	$NetBSD: rbus_machdep.c,v 1.2 1999/10/15 06:43:06 haya Exp $	*/

/*
 * Copyright (c) 1999
 *     HAYAKAWA Koichi.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <dev/cardbus/rbus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/ofw/openfirm.h>

void macppc_cardbus_init(pci_chipset_tag_t pc, pcitag_t tag);

rbus_tag_t
rbus_pccbb_parent_mem(struct device *self, struct pci_attach_args *pa)
{
	macppc_cardbus_init(pa->pa_pc, pa->pa_tag);

	return (rbus_new_root_share(pa->pa_memt, pa->pa_memex,
	    0x00000000, 0xffffffff));
}

rbus_tag_t
rbus_pccbb_parent_io(struct device *self, struct pci_attach_args *pa)
{
	return (rbus_new_root_share(pa->pa_iot, pa->pa_ioex,
	    0x0000, 0xffff));
}

/*
 * Big ugly hack to enable bridge/fix interrupts
 */
void
macppc_cardbus_init(pci_chipset_tag_t pc, pcitag_t tag)
{
	u_int x;
	static int initted = 0;

	if (initted)
		return;
	initted = 1;

	/* XXX What about other bridges? */

	x = pci_conf_read(pc, tag, PCI_ID_REG);
	if (PCI_VENDOR(x) == PCI_VENDOR_TI &&
	    PCI_PRODUCT(x) == PCI_PRODUCT_TI_PCI1211) {
		/* For CardBus card. */
		pci_conf_write(pc, tag, 0x18, 0x10010100);

		/* Route INTA to MFUNC0 */
		x = pci_conf_read(pc, tag, 0x8c);
		x |= 0x02;
		pci_conf_write(pc, tag, 0x8c, x);

		tag = pci_make_tag(pc, 0, 0, 0);
		x = pci_conf_read(pc, tag, PCI_ID_REG);
		if (PCI_VENDOR(x) == PCI_VENDOR_MOT &&
		    PCI_PRODUCT(x) == PCI_PRODUCT_MOT_MPC106) {
			/* Set subordinate bus number to 1. */
			x = pci_conf_read(pc, tag, 0x40);
			x |= 1 << 8;
			pci_conf_write(pc, tag, 0x40, x);
		}
	}

	if (PCI_VENDOR(x) == PCI_VENDOR_TI &&
	    (PCI_PRODUCT(x) == PCI_PRODUCT_TI_PCI1410 ||
	    PCI_PRODUCT(x) == PCI_PRODUCT_TI_PCI1510)) {
		/* dont mess with the bus numbers or latency timer */

		/* Route INTA to MFUNC0 */
		x = pci_conf_read(pc, tag, 0x8c);
		x |= 0x02;
		pci_conf_write(pc, tag, 0x8c, x);
	}
}

void
pccbb_attach_hook(struct device *parent, struct device *self,
    struct pci_attach_args *pa)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	int node = PCITAG_NODE(pa->pa_tag);
	int bus, busrange[2];

	if (OF_getprop(OF_parent(node), "bus-range", &busrange,
	    sizeof(busrange)) != sizeof(busrange))
		return;

	bus = busrange[0] + 1;
	while (bus < 256 && pc->busnode[bus])
		bus++;
	if (bus == 256)
		return;
	pc->busnode[bus] = node;
}
