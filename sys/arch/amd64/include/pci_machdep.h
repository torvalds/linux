/*	$OpenBSD: pci_machdep.h,v 1.32 2025/01/23 11:24:34 kettenis Exp $	*/
/*	$NetBSD: pci_machdep.h,v 1.1 2003/02/26 21:26:11 fvdl Exp $	*/

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles M. Hannum.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Machine-specific definitions for PCI autoconfiguration.
 */

/*
 * amd64-specific PCI structure and type definitions.
 * NOT TO BE USED DIRECTLY BY MACHINE INDEPENDENT CODE.
 */

extern struct bus_dma_tag pci_bus_dma_tag;

/*
 * Types provided to machine-independent PCI code
 */
typedef void *pci_chipset_tag_t;
typedef u_int32_t pcitag_t;

typedef struct {
	pcitag_t tag;
	int line, pin;
} pci_intr_handle_t;

#define	pci_intr_line(pc,ih)	((ih.line) & 0xff)

/*
 * amd64-specific PCI variables and functions.
 * NOT TO BE USED DIRECTLY BY MACHINE INDEPENDENT CODE.
 */
struct		pci_attach_args;

extern struct extent *pciio_ex;
extern struct extent *pcimem_ex;
extern struct extent *pcibus_ex;
void		pci_init_extents(void);

/*
 * Functions provided to machine-independent PCI code.
 */
void		pci_attach_hook(struct device *, struct device *,
		    struct pcibus_attach_args *);
int		pci_bus_maxdevs(pci_chipset_tag_t, int);
pcitag_t	pci_make_tag(pci_chipset_tag_t, int, int, int);
void		pci_decompose_tag(pci_chipset_tag_t, pcitag_t,
		    int *, int *, int *);
int		pci_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t	pci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void		pci_conf_write(pci_chipset_tag_t, pcitag_t, int,
		    pcireg_t);
int		pci_intr_enable_msivec(struct pci_attach_args *, int);
int		pci_intr_map_msi(struct pci_attach_args *,
		    pci_intr_handle_t *);
int		pci_intr_map_msivec(struct pci_attach_args *,
		    int, pci_intr_handle_t *);
int		pci_intr_map_msix(struct pci_attach_args *,
		    int, pci_intr_handle_t *);
int		pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char	*pci_intr_string(pci_chipset_tag_t, pci_intr_handle_t);
void		*pci_intr_establish(pci_chipset_tag_t, pci_intr_handle_t,
		    int, int (*)(void *), void *, const char *);
void		*pci_intr_establish_cpu(pci_chipset_tag_t, pci_intr_handle_t,
		    int, struct cpu_info *,
		    int (*)(void *), void *, const char *);
void		pci_intr_disestablish(pci_chipset_tag_t, void *);
int		pci_probe_device_hook(pci_chipset_tag_t,
		    struct pci_attach_args *);

void 		pci_dev_postattach(struct device *, struct pci_attach_args *);

pcireg_t	pci_min_powerstate(pci_chipset_tag_t, pcitag_t);
void		pci_set_powerstate_md(pci_chipset_tag_t, pcitag_t, int, int);

void		pci_mcfg_init(bus_space_tag_t, bus_addr_t, int, int, int);
pci_chipset_tag_t pci_lookup_segment(int, int);

#define __HAVE_PCI_MSIX

int	pci_msix_table_map(pci_chipset_tag_t, pcitag_t,
	    bus_space_tag_t, bus_space_handle_t *);
void	pci_msix_table_unmap(pci_chipset_tag_t, pcitag_t,
	    bus_space_tag_t, bus_space_handle_t);

/*
 * ALL OF THE FOLLOWING ARE MACHINE-DEPENDENT, AND SHOULD NOT BE USED
 * BY PORTABLE CODE.
 */

/*
 * Section 6.2.4, `Miscellaneous Functions' of the PCI Specification,
 * says that 255 means `unknown' or `no connection' to the interrupt
 * controller on a PC.
 */
#define	X86_PCI_INTERRUPT_LINE_NO_CONNECTION	0xff

/*
 * PCI address space is shared with ISA, so avoid legacy ISA I/O
 * registers.
 */
#define PCI_IO_START	0x400
#define PCI_IO_END	0xffff

/*
 * Avoid the DOS Compatibility Memory area.
 */
#define PCI_MEM_START	0x100000
