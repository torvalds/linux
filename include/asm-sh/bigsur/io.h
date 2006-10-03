/*
 * include/asm-sh/bigsur/io.h
 *
 * By Dustin McIntire (dustin@sensoria.com) (c)2001
 * Derived from io_hd64465.h, which bore the message:
 * By Greg Banks <gbanks@pocketpenguins.com>
 * (c) 2000 PocketPenguins Inc. 
 * and from io_hd64461.h, which bore the message:
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for a Hitachi Big Sur Evaluation Board.
 */

#ifndef _ASM_SH_IO_BIGSUR_H
#define _ASM_SH_IO_BIGSUR_H

#include <linux/types.h>

extern unsigned long bigsur_isa_port2addr(unsigned long offset);
extern int bigsur_irq_demux(int irq);
/* Provision for generic secondary demux step -- used by PCMCIA code */
extern void bigsur_register_irq_demux(int irq,
		int (*demux)(int irq, void *dev), void *dev);
extern void bigsur_unregister_irq_demux(int irq);
/* Set this variable to 1 to see port traffic */
extern int bigsur_io_debug;
/* Map a range of ports to a range of kernel virtual memory. */
extern void bigsur_port_map(u32 baseport, u32 nports, u32 addr, u8 shift);
extern void bigsur_port_unmap(u32 baseport, u32 nports);

#endif /* _ASM_SH_IO_BIGSUR_H */

