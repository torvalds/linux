#ifndef __ASM_SH_MPC1211_H
#define __ASM_SH_MPC1211_H

/*
 * linux/include/asm-sh/mpc1211.h
 *
 * Copyright (C) 2001  Saito.K & Jeanne
 *
 * Interface MPC-1211 support
 */

#define PA_PCI_IO       (0xa4000000)    /* PCI I/O space */
#define PA_PCI_MEM      (0xb0000000)    /* PCI MEM space */

#define PCIPAR          (0xa4000cf8)    /* PCI Config address */
#define PCIPDR          (0xa4000cfc)    /* PCI Config data    */

#endif  /* __ASM_SH_MPC1211_H */
