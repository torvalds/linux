/*
 * include/asm-sh/machvec.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#ifndef _ASM_SH_MACHVEC_H
#define _ASM_SH_MACHVEC_H 1

#include <linux/config.h>
#include <linux/types.h>
#include <linux/time.h>

#include <asm/machtypes.h>
#include <asm/machvec_init.h>

struct device;
struct timeval;

struct sh_machine_vector
{
	int mv_nr_irqs;

	unsigned char (*mv_inb)(unsigned long);
	unsigned short (*mv_inw)(unsigned long);
	unsigned int (*mv_inl)(unsigned long);
	void (*mv_outb)(unsigned char, unsigned long);
	void (*mv_outw)(unsigned short, unsigned long);
	void (*mv_outl)(unsigned int, unsigned long);

	unsigned char (*mv_inb_p)(unsigned long);
	unsigned short (*mv_inw_p)(unsigned long);
	unsigned int (*mv_inl_p)(unsigned long);
	void (*mv_outb_p)(unsigned char, unsigned long);
	void (*mv_outw_p)(unsigned short, unsigned long);
	void (*mv_outl_p)(unsigned int, unsigned long);

	void (*mv_insb)(unsigned long port, void *addr, unsigned long count);
	void (*mv_insw)(unsigned long port, void *addr, unsigned long count);
	void (*mv_insl)(unsigned long port, void *addr, unsigned long count);
	void (*mv_outsb)(unsigned long port, const void *addr, unsigned long count);
	void (*mv_outsw)(unsigned long port, const void *addr, unsigned long count);
	void (*mv_outsl)(unsigned long port, const void *addr, unsigned long count);

	unsigned char (*mv_readb)(unsigned long);
	unsigned short (*mv_readw)(unsigned long);
	unsigned int (*mv_readl)(unsigned long);
	void (*mv_writeb)(unsigned char, unsigned long);
	void (*mv_writew)(unsigned short, unsigned long);
	void (*mv_writel)(unsigned int, unsigned long);

	void* (*mv_ioremap)(unsigned long offset, unsigned long size);
	void (*mv_iounmap)(void *addr);

	unsigned long (*mv_isa_port2addr)(unsigned long offset);

	int (*mv_irq_demux)(int irq);

	void (*mv_init_irq)(void);
	void (*mv_init_pci)(void);

	void (*mv_heartbeat)(void);

	void *(*mv_consistent_alloc)(struct device *, size_t, dma_addr_t *, int);
	int (*mv_consistent_free)(struct device *, size_t, void *, dma_addr_t);
};

extern struct sh_machine_vector sh_mv;

#endif /* _ASM_SH_MACHVEC_H */
