/*
 * Since there are many different boards and no standard configuration,
 * we have a unique include file for each.  Rather than change every
 * file that has to include MPC8260 configuration, they all include
 * this one and the configuration switching is done here.
 */
#ifdef __KERNEL__
#ifndef __ASM_PPC_MPC8260_H__
#define __ASM_PPC_MPC8260_H__

#include <linux/config.h>

#ifdef CONFIG_8260

#ifdef CONFIG_EST8260
#include <platforms/est8260.h>
#endif

#ifdef CONFIG_SBC82xx
#include <platforms/sbc82xx.h>
#endif

#ifdef CONFIG_SBS8260
#include <platforms/sbs8260.h>
#endif

#ifdef CONFIG_RPX8260
#include <platforms/rpx8260.h>
#endif

#ifdef CONFIG_WILLOW
#include <platforms/willow.h>
#endif

#ifdef CONFIG_TQM8260
#include <platforms/tqm8260.h>
#endif

#if defined(CONFIG_PQ2ADS) || defined (CONFIG_PQ2FADS)
#include <platforms/pq2ads.h>
#endif

#ifdef CONFIG_PCI_8260
#include <syslib/m8260_pci.h>
#endif

/* Make sure the memory translation stuff is there if PCI not used.
 */
#ifndef _IO_BASE
#define _IO_BASE        0
#endif

#ifndef _ISA_MEM_BASE
#define _ISA_MEM_BASE   0
#endif

#ifndef PCI_DRAM_OFFSET
#define PCI_DRAM_OFFSET 0
#endif

/* Map 256MB I/O region
 */
#ifndef IO_PHYS_ADDR
#define IO_PHYS_ADDR	0xe0000000
#endif
#ifndef IO_VIRT_ADDR
#define IO_VIRT_ADDR	IO_PHYS_ADDR
#endif

#ifndef __ASSEMBLY__
/* The "residual" data board information structure the boot loader
 * hands to us.
 */
extern unsigned char __res[];
#endif

#endif /* CONFIG_8260 */
#endif /* !__ASM_PPC_MPC8260_H__ */
#endif /* __KERNEL__ */
