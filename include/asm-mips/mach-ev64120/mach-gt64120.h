/*
 *  This is a direct copy of the ev96100.h file, with a global
 * search and replace.  The numbers are the same.
 *
 *  The reason I'm duplicating this is so that the 64120/96100
 * defines won't be confusing in the source code.
 */
#ifndef __ASM_GALILEO_BOARDS_MIPS_EV64120_H
#define __ASM_GALILEO_BOARDS_MIPS_EV64120_H

/*
 *   GT64120 config space base address
 */
extern unsigned long gt64120_base;

#define GT64120_BASE	(gt64120_base)

/*
 *   PCI Bus allocation
 */
#define GT_PCI_MEM_BASE	0x12000000UL
#define GT_PCI_MEM_SIZE	0x02000000UL
#define GT_PCI_IO_BASE	0x10000000UL
#define GT_PCI_IO_SIZE	0x02000000UL
#define GT_ISA_IO_BASE	PCI_IO_BASE

/*
 *   Duart I/O ports.
 */
#define EV64120_COM1_BASE_ADDR	(0x1d000000 + 0x20)
#define EV64120_COM2_BASE_ADDR	(0x1d000000 + 0x00)


/*
 *   EV64120 interrupt controller register base.
 */
#define EV64120_ICTRL_REGS_BASE	(KSEG1ADDR(0x1f000000))

/*
 *   EV64120 UART register base.
 */
#define EV64120_UART0_REGS_BASE	(KSEG1ADDR(EV64120_COM1_BASE_ADDR))
#define EV64120_UART1_REGS_BASE	(KSEG1ADDR(EV64120_COM2_BASE_ADDR))
#define EV64120_BASE_BAUD ( 3686400 / 16 )

/*
 * PCI interrupts will come in on either the INTA or INTD interrups lines,
 * which are mapped to the #2 and #5 interrupt pins of the MIPS.  On our
 * boards, they all either come in on IntD or they all come in on IntA, they
 * aren't mixed. There can be numerous PCI interrupts, so we keep a list of the
 * "requested" interrupt numbers and go through the list whenever we get an
 * IntA/D.
 *
 * Interrupts < 8 are directly wired to the processor; PCI INTA is 8 and
 * INTD is 11.
 */
#define GT_TIMER	4
#define GT_INTA		2
#define GT_INTD		5

#endif /* __ASM_GALILEO_BOARDS_MIPS_EV64120_H */
