/*	$OpenBSD: pci_bus_fixup.c,v 1.10 2023/01/30 10:49:05 jsg Exp $	*/
/*	$NetBSD: pci_bus_fixup.c,v 1.1 1999/11/17 07:32:58 thorpej Exp $  */

/*
 * Copyright (c) 1999, by UCHIYAMA Yasushi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * PCI bus renumbering support.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/ppbreg.h>

#include <i386/pci/pcibiosvar.h>

int	pci_bus_check(pci_chipset_tag_t, int);
int	pci_bus_assign(pci_chipset_tag_t, int);
void	pcibus_print_devid(pci_chipset_tag_t, pcitag_t);

int
pci_bus_check(pci_chipset_tag_t pc, int bus)
{
	int device, maxdevs, function, nfuncs, bus_max, bus_sub;
	const struct pci_quirkdata *qd;
	pcireg_t reg;
	pcitag_t tag;

	bus_max = bus;

	maxdevs = pci_bus_maxdevs(pc, bus);
	for (device = 0; device < maxdevs; device++) {
		tag = pci_make_tag(pc, bus, device, 0);
		reg = pci_conf_read(pc, tag, PCI_ID_REG);

		/* can't be that many */
		if (bus_max == 255)
			break;

		/* Invalid vendor ID value? */
		if (PCI_VENDOR(reg) == PCI_VENDOR_INVALID)
			continue;
		/* XXX Not invalid, but we've done this ~forever. */
		if (PCI_VENDOR(reg) == 0)
			continue;

		qd = pci_lookup_quirkdata(PCI_VENDOR(reg), PCI_PRODUCT(reg));

		reg = pci_conf_read(pc, tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_MULTIFN(reg) ||
		    (qd != NULL &&
		     (qd->quirks & PCI_QUIRK_MULTIFUNCTION) != 0))
			nfuncs = 8;
		else
			nfuncs = 1;

		for (function = 0; function < nfuncs; function++) {
			tag = pci_make_tag(pc, bus, device, function);
			reg = pci_conf_read(pc, tag, PCI_ID_REG);

			/* Invalid vendor ID value? */
			if (PCI_VENDOR(reg) == PCI_VENDOR_INVALID)
				continue;
			/* XXX Not invalid, but we've done this ~forever. */
			if (PCI_VENDOR(reg) == 0)
				continue;

			reg = pci_conf_read(pc, tag, PCI_CLASS_REG);
			if (PCI_CLASS(reg) == PCI_CLASS_BRIDGE &&
			    (PCI_SUBCLASS(reg) == PCI_SUBCLASS_BRIDGE_PCI ||
			     PCI_SUBCLASS(reg) == PCI_SUBCLASS_BRIDGE_CARDBUS)) {

				reg = pci_conf_read(pc, tag, PPB_REG_BUSINFO);
				if (PPB_BUSINFO_PRIMARY(reg) != bus) {
					if (pcibios_flags & PCIBIOS_VERBOSE) {
						pcibus_print_devid(pc, tag);
						printf("Mismatched primary bus: "
						    "primary %d, secondary %d, "
						    "subordinate %d\n",
						    PPB_BUSINFO_PRIMARY(reg),
						    PPB_BUSINFO_SECONDARY(reg),
						    PPB_BUSINFO_SUBORDINATE(reg));
					}
					return (-1);
				}
				if (PPB_BUSINFO_SECONDARY(reg) <= bus) {
					if (pcibios_flags & PCIBIOS_VERBOSE) {
						pcibus_print_devid(pc, tag);
						printf("Incorrect secondary bus: "
						    "primary %d, secondary %d, "
						    "subordinate %d\n",
						    PPB_BUSINFO_PRIMARY(reg),
						    PPB_BUSINFO_SECONDARY(reg),
						    PPB_BUSINFO_SUBORDINATE(reg));
					}
					return (-1);
				}

				/* Scan subordinate bus. */
				bus_sub = pci_bus_check(pc,
				    PPB_BUSINFO_SECONDARY(reg));
				if (bus_sub == -1)
					return (-1);

				if (PPB_BUSINFO_SUBORDINATE(reg) < bus_sub) {
					if (pcibios_flags & PCIBIOS_VERBOSE) {
						pcibus_print_devid(pc, tag);
						printf("Incorrect subordinate bus %d: "
						    "primary %d, secondary %d, "
						    "subordinate %d\n", bus_sub,
						    PPB_BUSINFO_PRIMARY(reg),
						    PPB_BUSINFO_SECONDARY(reg),
						    PPB_BUSINFO_SUBORDINATE(reg));
					}
					return (-1);
				}

				bus_max = (bus_sub > bus_max) ?
				    bus_sub : bus_max;
			}
		}
	}

	return (bus_max);	/* last # of subordinate bus */
}

int
pci_bus_assign(pci_chipset_tag_t pc, int bus)
{
	static int bridge_cnt;
	int bridge, device, maxdevs, function, nfuncs, bus_max, bus_sub;
	const struct pci_quirkdata *qd;
	pcireg_t reg;
	pcitag_t tag;

	bus_max = bus;

	maxdevs = pci_bus_maxdevs(pc, bus);
	for (device = 0; device < maxdevs; device++) {
		tag = pci_make_tag(pc, bus, device, 0);
		reg = pci_conf_read(pc, tag, PCI_ID_REG);

		/* can't be that many */
		if (bus_max == 255)
			break;

		/* Invalid vendor ID value? */
		if (PCI_VENDOR(reg) == PCI_VENDOR_INVALID)
			continue;
		/* XXX Not invalid, but we've done this ~forever. */
		if (PCI_VENDOR(reg) == 0)
			continue;

		qd = pci_lookup_quirkdata(PCI_VENDOR(reg), PCI_PRODUCT(reg));

		reg = pci_conf_read(pc, tag, PCI_BHLC_REG);
		if (PCI_HDRTYPE_MULTIFN(reg) ||
		    (qd != NULL &&
		     (qd->quirks & PCI_QUIRK_MULTIFUNCTION) != 0))
			nfuncs = 8;
		else
			nfuncs = 1;

		for (function = 0; function < nfuncs; function++) {
			tag = pci_make_tag(pc, bus, device, function);
			reg = pci_conf_read(pc, tag, PCI_ID_REG);

			/* Invalid vendor ID value? */
			if (PCI_VENDOR(reg) == PCI_VENDOR_INVALID)
				continue;
			/* XXX Not invalid, but we've done this ~forever. */
			if (PCI_VENDOR(reg) == 0)
				continue;

			reg = pci_conf_read(pc, tag, PCI_CLASS_REG);
			if (PCI_CLASS(reg) == PCI_CLASS_BRIDGE &&
			    (PCI_SUBCLASS(reg) == PCI_SUBCLASS_BRIDGE_PCI ||
			     PCI_SUBCLASS(reg) == PCI_SUBCLASS_BRIDGE_CARDBUS)) {
				/* Assign the bridge's secondary bus #. */
				bus_max++;

				reg = pci_conf_read(pc, tag, PPB_REG_BUSINFO);
				reg &= 0xff000000;
				reg |= bus | (bus_max << 8) | (0xff << 16);
				pci_conf_write(pc, tag, PPB_REG_BUSINFO, reg);

				/* Scan subordinate bus. */
				bus_sub = pci_bus_assign(pc, bus_max);

				/* Configure the bridge. */
				reg &= 0xff000000;
				reg |= bus | (bus_max << 8) | (bus_sub << 16);
				pci_conf_write(pc, tag, PPB_REG_BUSINFO, reg);

				if (pcibios_flags & PCIBIOS_VERBOSE) {
					/* Assign the bridge #. */
					bridge = bridge_cnt++;

					printf("PCI bridge %d: primary %d, "
					    "secondary %d, subordinate %d\n",
					    bridge, bus, bus_max, bus_sub);
				}

				/* Next bridge's secondary bus #. */
				bus_max = (bus_sub > bus_max) ?
				    bus_sub : bus_max;
			}
		}
	}

	return (bus_max);	/* last # of subordinate bus */
}

int
pci_bus_fixup(pci_chipset_tag_t pc, int bus)
{
	int bus_max;

	bus_max = pci_bus_check(pc, bus);
	if (bus_max != -1)
		return (bus_max);

	if (pcibios_flags & PCIBIOS_VERBOSE)
		printf("PCI bus renumbering needed\n");
	return pci_bus_assign(pc, bus);
}

void
pcibus_print_devid(pci_chipset_tag_t pc, pcitag_t tag)
{
	int bus, device, function;	
	pcireg_t id;

	id = pci_conf_read(pc, tag, PCI_ID_REG);
	pci_decompose_tag(pc, tag, &bus, &device, &function);
	printf("%03d:%02d:%d %04x:%04x\n", bus, device, function, 
	       PCI_VENDOR(id), PCI_PRODUCT(id));
}
