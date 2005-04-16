/*
 *  This is a direct copy of the ev96100.h file, with a global
 * search and replace.  The numbers are the same.
 *
 *  The reason I'm duplicating this is so that the 64120/96100
 * defines won't be confusing in the source code.
 */
#ifndef _ASM_GT64120_EV96100_GT64120_DEP_H
#define _ASM_GT64120_EV96100_GT64120_DEP_H

/*
 *   GT96100 config space base address
 */
#define GT64120_BASE	(KSEG1ADDR(0x14000000))

/*
 *   PCI Bus allocation
 *
 *   (Guessing ...)
 */
#define GT_PCI_MEM_BASE	0x12000000UL
#define GT_PCI_MEM_SIZE	0x02000000UL
#define GT_PCI_IO_BASE	0x10000000UL
#define GT_PCI_IO_SIZE	0x02000000UL
#define GT_ISA_IO_BASE	PCI_IO_BASE

/*
 *   Duart I/O ports.
 */
#define EV96100_COM1_BASE_ADDR	(0xBD000000 + 0x20)
#define EV96100_COM2_BASE_ADDR	(0xBD000000 + 0x00)


/*
 *   EV96100 interrupt controller register base.
 */
#define EV96100_ICTRL_REGS_BASE	(KSEG1ADDR(0x1f000000))

/*
 *   EV96100 UART register base.
 */
#define EV96100_UART0_REGS_BASE	EV96100_COM1_BASE_ADDR
#define EV96100_UART1_REGS_BASE	EV96100_COM2_BASE_ADDR
#define EV96100_BASE_BAUD	( 3686400 / 16 )

#endif /* _ASM_GT64120_EV96100_GT64120_DEP_H */
