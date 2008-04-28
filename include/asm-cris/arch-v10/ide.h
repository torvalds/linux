/*
 *  linux/include/asm-cris/ide.h
 *
 *  Copyright (C) 2000, 2001, 2002  Axis Communications AB
 *
 *  Authors:    Bjorn Wesen
 *
 */

/*
 *  This file contains the ETRAX 100LX specific IDE code.
 */

#ifndef __ASMCRIS_IDE_H
#define __ASMCRIS_IDE_H

#ifdef __KERNEL__

#include <asm/arch/svinto.h>
#include <asm/io.h>
#include <asm-generic/ide_iops.h>


/* ETRAX 100 can support 4 IDE busses on the same pins (serialized) */

#define MAX_HWIFS	4

static inline int ide_default_irq(unsigned long base)
{
	/* all IDE busses share the same IRQ, number 4.
	 * this has the side-effect that ide-probe.c will cluster our 4 interfaces
	 * together in a hwgroup, and will serialize accesses. this is good, because
	 * we can't access more than one interface at the same time on ETRAX100.
	 */
	return 4;
}

static inline unsigned long ide_default_io_base(int index)
{
	/* we have no real I/O base address per interface, since all go through the
	 * same register. but in a bitfield in that register, we have the i/f number.
	 * so we can use the io_base to remember that bitfield.
	 */
	static const unsigned long io_bases[MAX_HWIFS] = {
		IO_FIELD(R_ATA_CTRL_DATA, sel, 0),
		IO_FIELD(R_ATA_CTRL_DATA, sel, 1),
		IO_FIELD(R_ATA_CTRL_DATA, sel, 2),
		IO_FIELD(R_ATA_CTRL_DATA, sel, 3)
	};
	return io_bases[index];
}

/* this is called once for each interface, to setup the port addresses. data_port is the result
 * of the ide_default_io_base call above. ctrl_port will be 0, but that is don't care for us.
 */

static inline void ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port, unsigned long ctrl_port, int *irq)
{
	int i;

	/* fill in ports for ATA addresses 0 to 7 */
	for (i = 0; i <= 7; i++) {
		hw->io_ports_array[i] = data_port |
			IO_FIELD(R_ATA_CTRL_DATA, addr, i) |
			IO_STATE(R_ATA_CTRL_DATA, cs0, active);
	}

	/* the IDE control register is at ATA address 6, with CS1 active instead of CS0 */
	hw->io_ports.ctl_addr = data_port |
			IO_FIELD(R_ATA_CTRL_DATA, addr, 6) |
			IO_STATE(R_ATA_CTRL_DATA, cs1, active);

	/* whats this for ? */
	hw->io_ports.irq_addr = 0;
}

static inline void ide_init_default_hwifs(void)
{
	hw_regs_t hw;
	int index;

	for(index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
}

#endif /* __KERNEL__ */

#endif /* __ASMCRIS_IDE_H */
