/*	$OpenBSD: pci_machdep.h,v 1.33 2025/01/23 11:24:34 kettenis Exp $	*/
/*	$NetBSD: pci_machdep.h,v 1.7 1997/06/06 23:29:18 thorpej Exp $	*/

/*
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1994 Charles Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles Hannum.
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
 * i386-specific PCI structure and type definitions.
 * NOT TO BE USED DIRECTLY BY MACHINE INDEPENDENT CODE.
 *
 * Configuration tag; created from a {bus,device,function} triplet by
 * pci_make_tag(), and passed to pci_conf_read() and pci_conf_write().
 * We could instead always pass the {bus,device,function} triplet to
 * the read and write routines, but this would cause extra overhead.
 *
 * Mode 2 is historical and deprecated by the Revision 2.0 specification.
 */
union i386_pci_tag_u {
	u_int32_t mode1;
	struct {
		u_int16_t port;
		u_int8_t enable;
		u_int8_t forward;
	} mode2;
};

extern struct bus_dma_tag pci_bus_dma_tag;

/*
 * Types provided to machine-independent PCI code
 */
typedef void *pci_chipset_tag_t;
typedef union i386_pci_tag_u pcitag_t;

typedef
struct {
	pcitag_t tag;
	int line, pin;
	void *link;
} pci_intr_handle_t;

/*
 * i386-specific PCI variables and functions.
 * NOT TO BE USED DIRECTLY BY MACHINE INDEPENDENT CODE.
 */
extern int pci_mode;
int		pci_mode_detect(void);

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
int		pci_conf_size(pci_chipset_tag_t, pcitag_t);
pcireg_t	pci_conf_read(pci_chipset_tag_t, pcitag_t, int);
void		pci_conf_write(pci_chipset_tag_t, pcitag_t, int,
		    pcireg_t);
struct pci_attach_args;
int		pci_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
int		pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
#define		pci_intr_line(c, ih)	((ih).line)
const char	*pci_intr_string(pci_chipset_tag_t, pci_intr_handle_t);
void		*pci_intr_establish(pci_chipset_tag_t, pci_intr_handle_t,
		    int, int (*)(void *), void *, const char *);
void		*pci_intr_establish_cpu(pci_chipset_tag_t, pci_intr_handle_t,
		    int, struct cpu_info *,
		    int (*)(void *), void *, const char *);
void		pci_intr_disestablish(pci_chipset_tag_t, void *);
void		pci_decompose_tag(pci_chipset_tag_t, pcitag_t,
		    int *, int *, int *);
#define	pci_probe_device_hook(c, a)	(0)

void 		pci_dev_postattach(struct device *, struct pci_attach_args *);

pcireg_t	pci_min_powerstate(pci_chipset_tag_t, pcitag_t);
void		pci_set_powerstate_md(pci_chipset_tag_t, pcitag_t, int, int);

void		pci_mcfg_init(bus_space_tag_t, bus_addr_t, int, int, int);
pci_chipset_tag_t pci_lookup_segment(int, int);

static inline int
pci_intr_map_msix(struct pci_attach_args *pa, int vec, pci_intr_handle_t *ihp)
{
	return -1;
}

/*
 * Section 6.2.4, `Miscellaneous Functions' of the PIC Specification,
 * says that 255 means `unknown' or `no connection' to the interrupt
 * controller on a PC.
 */
#define	I386_PCI_INTERRUPT_LINE_NO_CONNECTION	0xff

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
