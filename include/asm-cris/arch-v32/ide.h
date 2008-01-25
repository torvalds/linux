/*
 *  linux/include/asm-cris/ide.h
 *
 *  Copyright (C) 2000-2004  Axis Communications AB
 *
 *  Authors:    Bjorn Wesen, Mikael Starvik
 *
 */

/*
 *  This file contains the ETRAX FS specific IDE code.
 */

#ifndef __ASMCRIS_IDE_H
#define __ASMCRIS_IDE_H

#ifdef __KERNEL__

#include <asm/arch/hwregs/intr_vect.h>
#include <asm/arch/hwregs/ata_defs.h>
#include <asm/io.h>
#include <asm-generic/ide_iops.h>


/* ETRAX FS can support 4 IDE busses on the same pins (serialized) */

#define MAX_HWIFS	4

static inline int ide_default_irq(unsigned long base)
{
	/* all IDE busses share the same IRQ,
	 * this has the side-effect that ide-probe.c will cluster our 4 interfaces
	 * together in a hwgroup, and will serialize accesses. this is good, because
	 * we can't access more than one interface at the same time on ETRAX100.
	 */
	return ATA_INTR_VECT;
}

static inline unsigned long ide_default_io_base(int index)
{
	reg_ata_rw_ctrl2 ctrl2 = {.sel = index};
	/* we have no real I/O base address per interface, since all go through the
	 * same register. but in a bitfield in that register, we have the i/f number.
	 * so we can use the io_base to remember that bitfield.
	 */
        ctrl2.sel = index;

	return REG_TYPE_CONV(unsigned long, reg_ata_rw_ctrl2, ctrl2);
}

#define IDE_ARCH_ACK_INTR
#define ide_ack_intr(hwif)	((hwif)->ack_intr(hwif))

#endif /* __KERNEL__ */

#endif /* __ASMCRIS_IDE_H */
