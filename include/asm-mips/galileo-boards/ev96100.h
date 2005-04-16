/*
 *
 */
#ifndef _MIPS_EV96100_H
#define _MIPS_EV96100_H

#include <asm/addrspace.h>

/*
 *   GT64120 config space base address
 */
#define GT64120_BASE	(KSEG1ADDR(0x14000000))
#define MIPS_GT_BASE	GT64120_BASE

/*
 *   PCI Bus allocation
 */
#define GT_PCI_MEM_BASE    0x12000000UL
#define GT_PCI_MEM_SIZE    0x02000000UL
#define GT_PCI_IO_BASE     0x10000000UL
#define GT_PCI_IO_SIZE     0x02000000UL
#define GT_ISA_IO_BASE     PCI_IO_BASE

/*
 *   Duart I/O ports.
 */
#define EV96100_COM1_BASE_ADDR 	(0xBD000000 + 0x20)
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


/*
 * Because of an error/peculiarity in the Galileo chip, we need to swap the
 * bytes when running bigendian.
 */
#define __GT_READ(ofs)							\
	(*(volatile u32 *)(GT64120_BASE+(ofs)))
#define __GT_WRITE(ofs, data)						\
	do { *(volatile u32 *)(GT64120_BASE+(ofs)) = (data); } while (0)
#define GT_READ(ofs)		le32_to_cpu(__GT_READ(ofs))
#define GT_WRITE(ofs, data)	__GT_WRITE(ofs, cpu_to_le32(data))

#endif /* !(_MIPS_EV96100_H) */
