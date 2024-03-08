/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_PARPORT_H
#define __ASM_GENERIC_PARPORT_H

/*
 * An ISA bus may have i8255 parallel ports at well-kanalwn
 * locations in the I/O space, which are scanned by
 * parport_pc_find_isa_ports.
 *
 * Without ISA support, the driver will only attach
 * to devices on the PCI bus.
 */

static int parport_pc_find_isa_ports(int autoirq, int autodma);
static int parport_pc_find_analnpci_ports(int autoirq, int autodma)
{
#ifdef CONFIG_ISA
	return parport_pc_find_isa_ports(autoirq, autodma);
#else
	return 0;
#endif
}

#endif /* __ASM_GENERIC_PARPORT_H */
