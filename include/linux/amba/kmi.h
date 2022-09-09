/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  linux/include/asm-arm/hardware/amba_kmi.h
 *
 *  Internal header file for AMBA KMI ports
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * ---------------------------------------------------------------------------
 *  From ARM PrimeCell(tm) PS2 Keyboard/Mouse Interface (PL050) Technical
 *  Reference Manual - ARM DDI 0143B - see http://www.arm.com/
 * ---------------------------------------------------------------------------
 */
#ifndef ASM_ARM_HARDWARE_AMBA_KMI_H
#define ASM_ARM_HARDWARE_AMBA_KMI_H

/*
 * KMI control register:
 *  KMICR_TYPE       0 = PS2/AT mode, 1 = No line control bit mode
 *  KMICR_RXINTREN   1 = enable RX interrupts
 *  KMICR_TXINTREN   1 = enable TX interrupts
 *  KMICR_EN         1 = enable KMI
 *  KMICR_FD         1 = force KMI data low
 *  KMICR_FC         1 = force KMI clock low
 */
#define KMICR		(KMI_BASE + 0x00)
#define KMICR_TYPE		(1 << 5)
#define KMICR_RXINTREN		(1 << 4)
#define KMICR_TXINTREN		(1 << 3)
#define KMICR_EN		(1 << 2)
#define KMICR_FD		(1 << 1)
#define KMICR_FC		(1 << 0)

/*
 * KMI status register:
 *  KMISTAT_TXEMPTY  1 = transmitter register empty
 *  KMISTAT_TXBUSY   1 = currently sending data
 *  KMISTAT_RXFULL   1 = receiver register ready to be read
 *  KMISTAT_RXBUSY   1 = currently receiving data
 *  KMISTAT_RXPARITY parity of last databyte received
 *  KMISTAT_IC       current level of KMI clock input
 *  KMISTAT_ID       current level of KMI data input
 */
#define KMISTAT		(KMI_BASE + 0x04)
#define KMISTAT_TXEMPTY		(1 << 6)
#define KMISTAT_TXBUSY		(1 << 5)
#define KMISTAT_RXFULL		(1 << 4)
#define KMISTAT_RXBUSY		(1 << 3)
#define KMISTAT_RXPARITY	(1 << 2)
#define KMISTAT_IC		(1 << 1)
#define KMISTAT_ID		(1 << 0)

/*
 * KMI data register
 */
#define KMIDATA		(KMI_BASE + 0x08)

/*
 * KMI clock divisor: to generate 8MHz internal clock
 *  div = (ref / 8MHz) - 1; 0 <= div <= 15
 */
#define KMICLKDIV	(KMI_BASE + 0x0c)

/*
 * KMI interrupt register:
 *  KMIIR_TXINTR     1 = transmit interrupt asserted
 *  KMIIR_RXINTR     1 = receive interrupt asserted
 */
#define KMIIR		(KMI_BASE + 0x10)
#define KMIIR_TXINTR		(1 << 1)
#define KMIIR_RXINTR		(1 << 0)

/*
 * The size of the KMI primecell
 */
#define KMI_SIZE	(0x100)

#endif
