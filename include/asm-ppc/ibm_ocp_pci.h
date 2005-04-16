/*
 * Author: Armin Kuster <akuster@mvista.com>
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM_OCP_PCI_H__
#define __ASM_IBM_OCP_PCI_H__

/* PCI 32 */

struct pmm_regs {
	u32 la;
	u32 ma;
	u32 pcila;
	u32 pciha;
};

typedef struct pcil0_regs {
	struct pmm_regs pmm[3];
	u32 ptm1ms;
	u32 ptm1la;
	u32 ptm2ms;
	u32 ptm2la;
} pci0_t;

#endif				/* __ASM_IBM_OCP_PCI_H__ */
#endif				/* __KERNEL__ */
