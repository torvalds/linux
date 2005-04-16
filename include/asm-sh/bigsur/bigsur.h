/*
 *
 * Hitachi Big Sur Eval Board support
 *
 * Dustin McIntire (dustin@sensoria.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Derived from Hitachi SH7751 reference manual
 * 
 */

#ifndef _ASM_BIGSUR_H_
#define _ASM_BIGSUR_H_

#include <asm/irq.h>
#include <asm/hd64465/hd64465.h>

/* 7751 Internal IRQ's used by external CPLD controller */
#define BIGSUR_IRQ_LOW	0
#define BIGSUR_IRQ_NUM  14         /* External CPLD level 1 IRQs */
#define BIGSUR_IRQ_HIGH (BIGSUR_IRQ_LOW + BIGSUR_IRQ_NUM)
#define BIGSUR_2NDLVL_IRQ_LOW   (HD64465_IRQ_BASE+HD64465_IRQ_NUM)  
#define BIGSUR_2NDLVL_IRQ_NUM   32 /* Level 2 IRQs = 4 regs * 8 bits */
#define BIGSUR_2NDLVL_IRQ_HIGH  (BIGSUR_2NDLVL_IRQ_LOW + \
                                 BIGSUR_2NDLVL_IRQ_NUM)

/* PCI interrupt base number (A_INTA-A_INTD) */
#define BIGSUR_SH7751_PCI_IRQ_BASE  (BIGSUR_2NDLVL_IRQ_LOW+10)  

/* CPLD registers and external chip addresses */
#define BIGSUR_HD64464_ADDR	0xB2000000
#define BIGSUR_DGDR	0xB1FFFE00
#define BIGSUR_BIDR	0xB1FFFD00
#define BIGSUR_CSLR	0xB1FFFC00
#define BIGSUR_SW1R	0xB1FFFB00
#define BIGSUR_DBGR	0xB1FFFA00
#define BIGSUR_BDTR	0xB1FFF900
#define BIGSUR_BDRR	0xB1FFF800
#define BIGSUR_PPR1	0xB1FFF700
#define BIGSUR_PPR2	0xB1FFF600
#define BIGSUR_IDE2	0xB1FFF500
#define BIGSUR_IDE3	0xB1FFF400
#define BIGSUR_SPCR	0xB1FFF300
#define BIGSUR_ETHR	0xB1FE0000
#define BIGSUR_PPDR	0xB1FDFF00
#define BIGSUR_ICTL	0xB1FDFE00
#define BIGSUR_ICMD	0xB1FDFD00
#define BIGSUR_DMA0	0xB1FDFC00
#define BIGSUR_DMA1	0xB1FDFB00
#define BIGSUR_IRQ0	0xB1FDFA00
#define BIGSUR_IRQ1	0xB1FDF900
#define BIGSUR_IRQ2	0xB1FDF800
#define BIGSUR_IRQ3	0xB1FDF700
#define BIGSUR_IMR0	0xB1FDF600
#define BIGSUR_IMR1	0xB1FDF500
#define BIGSUR_IMR2	0xB1FDF400
#define BIGSUR_IMR3	0xB1FDF300
#define BIGSUR_IRLMR0	0xB1FDF200
#define BIGSUR_IRLMR1	0xB1FDF100
#define BIGSUR_V320USC_ADDR  0xB1000000
#define BIGSUR_HD64465_ADDR  0xB0000000
#define BIGSUR_INTERNAL_BASE 0xB0000000

/* SMC ethernet card parameters */
#define BIGSUR_ETHER_IOPORT		0x220

/* IDE register paramters */
#define BIGSUR_IDECMD_IOPORT	0x1f0
#define BIGSUR_IDECTL_IOPORT	0x1f8

/* LED bit position in BIGSUR_CSLR */
#define BIGSUR_LED  (1<<4)

/* PCI: default LOCAL memory window sizes (seen from PCI bus) */
#define BIGSUR_LSR0_SIZE    (64*(1<<20)) //64MB
#define BIGSUR_LSR1_SIZE    (64*(1<<20)) //64MB

#endif /* _ASM_BIGSUR_H_ */
