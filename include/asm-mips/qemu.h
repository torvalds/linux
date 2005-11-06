/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 by Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_QEMU_H
#define __ASM_QEMU_H

/*
 * Interrupt numbers
 */
#define Q_PIC_IRQ_BASE		0
#define Q_COUNT_COMPARE_IRQ	16

/*
 * Qemu clock rate.  Unlike on real MIPS this has no relation to the
 * instruction issue rate, so the choosen value is pure fiction, just needs
 * to match the value in Qemu itself.
 */
#define QEMU_C0_COUNTER_CLOCK	100000000

#endif /* __ASM_QEMU_H */
