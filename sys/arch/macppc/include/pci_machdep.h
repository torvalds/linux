/*	$OpenBSD: pci_machdep.h,v 1.4 2019/12/05 12:46:54 mpi Exp $	*/
/*	$NetBSD: pci_machdep.h,v 1.7 2001/07/20 00:07:14 eeh Exp $ */

/*
 * Copyright (c) 1999 Matthew R. Green
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MACHINE_PCI_MACHDEP_H_
#define _MACHINE_PCI_MACHDEP_H_

extern struct powerpc_bus_dma_tag pci_bus_dma_tag;

/*
 * Forward declarations.
 */
struct pci_attach_args;

/*
 * define some bits used to glue into the common PCI code.
 */

typedef struct ppc_pci_chipset *pci_chipset_tag_t;

typedef unsigned long pci_intr_handle_t;

/* 
 * The stuuuuuuupid allegedly MI PCI code expects pcitag_t to be a
 * scalar type.  But we really need to store both the OFW node and
 * the bus/device/function info in it.  (We'd like to store more, 
 * like all the ofw properties, but we don't need to.)  Luckily,
 * both are 32-bit values, so we can squeeze them into a u_int64_t
 * with a little help from some macros.
 */

#define PCITAG_NODE(x)		(int)(((x)>>32)&0xffffffff)
#define PCITAG_OFFSET(x)	((x)&0xffffffff)
#define PCITAG_BUS(t)		((PCITAG_OFFSET(t)>>16)&0xff)
#define PCITAG_DEV(t)		((PCITAG_OFFSET(t)>>11)&0x1f)
#define PCITAG_FUN(t)		((PCITAG_OFFSET(t)>>8)&0x7)
#define PCITAG_CREATE(n,b,d,f)	(((uint64_t)(n)<<32)|((b)<<16)|((d)<<11)|((f)<<8))
typedef uint64_t pcitag_t; 

struct ppc_pci_chipset {
	void		*pc_conf_v;
	int		 pc_node;
	int		 busnode[256];

	pcireg_t	(*pc_conf_read)(void *, pcitag_t, int);
	void		(*pc_conf_write)(void *, pcitag_t, int, pcireg_t);
};


void		pci_attach_hook(struct device *, struct device *,
		    struct pcibus_attach_args *);
int		pci_bus_maxdevs(pci_chipset_tag_t, int);
pcitag_t	pci_make_tag(pci_chipset_tag_t, int, int, int);
void		pci_decompose_tag(pci_chipset_tag_t, pcitag_t, int *, int *,
		    int *);
int		pci_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t	pci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void		pci_conf_write(pci_chipset_tag_t, pcitag_t, int, pcireg_t);
int		pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int		pci_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
#define		pci_intr_map_msix(p, vec, ihp)	(-1)
int		pci_intr_line(pci_chipset_tag_t, pci_intr_handle_t);
const char	*pci_intr_string(pci_chipset_tag_t, pci_intr_handle_t);
void		*pci_intr_establish(pci_chipset_tag_t, pci_intr_handle_t,
		    int, int (*)(void *), void *, const char *);
void		pci_intr_disestablish(pci_chipset_tag_t, void *);
int		pci_ether_hw_addr(pci_chipset_tag_t, uint8_t *);

#define pci_probe_device_hook(c, a)	(0)

#define pci_min_powerstate(c, t)	(PCI_PMCSR_STATE_D3)
#define pci_set_powerstate_md(c, t, s, p)

#define pci_dev_postattach(a, b)

int		ofw_intr_map(int, uint32_t *, uint32_t *);
int		ofw_enumerate_pcibus(struct pci_softc *,
		    int (*match)(struct pci_attach_args *),
		    struct pci_attach_args *);

#define PCI_MACHDEP_ENUMERATE_BUS	ofw_enumerate_pcibus

#endif /* _MACHINE_PCI_MACHDEP_H_ */
