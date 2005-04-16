/*
 *  include/asm-mips/ddb5074.h -- NEC DDB Vrc-5074 definitions
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */

#ifndef _ASM_DDB5XXX_DDB5074_H
#define _ASM_DDB5XXX_DDB5074_H

#include <asm/nile4.h>

#define DDB_SDRAM_SIZE      0x04000000      /* 64MB */

#define DDB_PCI_IO_BASE     0x06000000
#define DDB_PCI_IO_SIZE     0x02000000      /* 32 MB */

#define DDB_PCI_MEM_BASE    0x08000000
#define DDB_PCI_MEM_SIZE    0x08000000  /* 128 MB */

#define DDB_PCI_CONFIG_BASE DDB_PCI_MEM_BASE
#define DDB_PCI_CONFIG_SIZE DDB_PCI_MEM_SIZE

#define NILE4_PCI_IO_BASE   0xa6000000
#define NILE4_PCI_MEM_BASE  0xa8000000
#define NILE4_PCI_CFG_BASE  NILE4_PCI_MEM_BASE
#define DDB_PCI_IACK_BASE NILE4_PCI_IO_BASE

#define NILE4_IRQ_BASE NUM_I8259_INTERRUPTS
#define CPU_IRQ_BASE (NUM_NILE4_INTERRUPTS + NILE4_IRQ_BASE)
#define CPU_NILE4_CASCADE 2

extern void ddb5074_led_hex(int hex);
extern void ddb5074_led_d2(int on);
extern void ddb5074_led_d3(int on);

extern void nile4_irq_setup(u32 base);
#endif
