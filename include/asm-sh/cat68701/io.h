/*
 * include/asm-sh/io_cat68701.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *           2001 Yutarou Ebihar (ebihara@si-linux.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * IO functions for an AONE Corp. CAT-68701 SH7708 Borad
 */

#ifndef _ASM_SH_IO_CAT68701_H
#define _ASM_SH_IO_CAT68701_H

extern unsigned long cat68701_isa_port2addr(unsigned long offset);
extern int cat68701_irq_demux(int irq);

extern void init_cat68701_IRQ(void);
extern void heartbeat_cat68701(void);

#endif /* _ASM_SH_IO_CAT68701_H */
