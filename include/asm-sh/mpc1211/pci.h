/*
 *	Low-Level PCI Support for MPC-1211
 *
 *      (c) 2002 Saito.K & Jeanne
 *
 */

#ifndef _PCI_MPC1211_H_
#define _PCI_MPC1211_H_

#include <linux/pci.h>

/* set debug level 4=verbose...1=terse */
//#define DEBUG_PCI 3
#undef DEBUG_PCI

#ifdef DEBUG_PCI
#define PCIDBG(n, x...) { if(DEBUG_PCI>=n) printk(x); }
#else
#define PCIDBG(n, x...)
#endif

/* startup values */
#define PCI_PROBE_BIOS    1
#define PCI_PROBE_CONF1   2
#define PCI_PROBE_CONF2   4
#define PCI_NO_SORT       0x100
#define PCI_BIOS_SORT     0x200
#define PCI_NO_CHECKS     0x400
#define PCI_ASSIGN_ROMS   0x1000
#define PCI_BIOS_IRQ_SCAN 0x2000

/* MPC-1211 Specific Values */
#define PCIPAR            (0xa4000cf8)    /* PCI Config address */
#define PCIPDR            (0xa4000cfc)    /* PCI Config data    */

#define PA_PCI_IO         (0xa4000000)    /* PCI I/O space */
#define PA_PCI_MEM        (0xb0000000)    /* PCI MEM space */

#endif /* _PCI_MPC1211_H_ */
