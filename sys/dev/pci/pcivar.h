/*	$OpenBSD: pcivar.h,v 1.81 2025/06/29 19:32:08 miod Exp $	*/
/*	$NetBSD: pcivar.h,v 1.23 1997/06/06 23:48:05 thorpej Exp $	*/

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

#ifndef _DEV_PCI_PCIVAR_H_
#define	_DEV_PCI_PCIVAR_H_

/*
 * Definitions for PCI autoconfiguration.
 *
 * This file describes types and functions which are used for PCI
 * configuration.  Some of this information is machine-specific, and is
 * provided by pci_machdep.h.
 */

#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/extent.h>
#include <machine/bus.h>
#include <dev/pci/pcireg.h>

/*
 * Structures and definitions needed by the machine-dependent header.
 */
typedef u_int32_t pcireg_t;		/* configuration space register XXX */

/*
 * Power Management (PCI 2.2)
 */
#define PCI_PWR_D0	0
#define PCI_PWR_D1	1
#define PCI_PWR_D2	2
#define PCI_PWR_D3	3

struct pci_matchid {
	pci_vendor_id_t		pm_vid;
	pci_product_id_t	pm_pid;
};

#ifdef _KERNEL

struct pcibus_attach_args;
struct pci_softc;

/*
 * Machine-dependent definitions.
 */
#if defined(__alpha__)
#include <alpha/pci/pci_machdep.h>
#elif defined(__i386__)
#include <i386/pci/pci_machdep.h>
#else
#include <machine/pci_machdep.h>
#endif

/*
 * PCI bus attach arguments.
 */
struct pcibus_attach_args {
	char	*pba_busname;		/* XXX should be common */
	bus_space_tag_t pba_iot;	/* pci i/o space tag */
	bus_space_tag_t pba_memt;	/* pci mem space tag */
	bus_dma_tag_t pba_dmat;		/* DMA tag */
	pci_chipset_tag_t pba_pc;
	int		pba_flags;	/* flags; see below */

	struct extent	*pba_ioex;
	struct extent	*pba_memex;
	struct extent	*pba_pmemex;
	struct extent	*pba_busex;

	int		pba_domain;	/* PCI domain */
	int		pba_bus;	/* PCI bus number */

	/*
	 * Pointer to the pcitag of our parent bridge.  If there is no
	 * parent bridge, then we assume we are a root bus.
	 */
	pcitag_t	*pba_bridgetag;
	pci_intr_handle_t *pba_bridgeih;

	/*
	 * Interrupt swizzling information.  These fields
	 * are only used by secondary busses.
	 */
	u_int		pba_intrswiz;	/* how to swizzle pins */
	pcitag_t	pba_intrtag;	/* intr. appears to come from here */
};

/*
 * PCI device attach arguments.
 */
struct pci_attach_args {
	bus_space_tag_t pa_iot;		/* pci i/o space tag */
	bus_space_tag_t pa_memt;	/* pci mem space tag */
	bus_dma_tag_t pa_dmat;		/* DMA tag */
	pci_chipset_tag_t pa_pc;
	int		pa_flags;	/* flags; see below */

	struct extent	*pa_ioex;
	struct extent	*pa_memex;
	struct extent	*pa_pmemex;
	struct extent	*pa_busex;

	u_int           pa_domain;
	u_int           pa_bus;
	u_int		pa_device;
	u_int		pa_function;
	pcitag_t	pa_tag;
	pcireg_t	pa_id, pa_class;

	pcitag_t	*pa_bridgetag;
	pci_intr_handle_t *pa_bridgeih;

	/*
	 * Interrupt information.
	 *
	 * "Intrline" is used on systems whose firmware puts
	 * the right routing data into the line register in
	 * configuration space.  The rest are used on systems
	 * that do not.
	 */
	u_int		pa_intrswiz;	/* how to swizzle pins if ppb */
	pcitag_t	pa_intrtag;	/* intr. appears to come from here */
	pci_intr_pin_t	pa_intrpin;	/* intr. appears on this pin */
	pci_intr_line_t	pa_intrline;	/* intr. routing information */
	pci_intr_pin_t	pa_rawintrpin;	/* unswizzled pin */
};

/*
 * Flags given in the bus and device attachment args.
 *
 * OpenBSD doesn't actually use them yet -- csapuntz@cvs.openbsd.org
 */
#define	PCI_FLAGS_IO_ENABLED		0x01	/* I/O space is enabled */
#define	PCI_FLAGS_MEM_ENABLED		0x02	/* memory space is enabled */
#define	PCI_FLAGS_MRL_OKAY		0x04	/* Memory Read Line okay */
#define	PCI_FLAGS_MRM_OKAY		0x08	/* Memory Read Multiple okay */
#define	PCI_FLAGS_MWI_OKAY		0x10	/* Memory Write and Invalidate
						   okay */
#define	PCI_FLAGS_MSI_ENABLED		0x20	/* Message Signaled Interrupt
						   enabled */
#define PCI_FLAGS_MSIVEC_ENABLED	0x40	/* Multiple Message Capability
						   enabled */

/*
 *
 */
struct pci_quirkdata {
	pci_vendor_id_t		vendor;		/* Vendor ID */
	pci_product_id_t	product;	/* Product ID */
	int			quirks;		/* quirks; see below */
};
#define	PCI_QUIRK_MULTIFUNCTION		1
#define	PCI_QUIRK_MONOFUNCTION		2

struct pci_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot, sc_memt;
	bus_dma_tag_t sc_dmat;
	pci_chipset_tag_t sc_pc;
	int sc_flags;
	struct extent *sc_ioex;
	struct extent *sc_memex;
	struct extent *sc_pmemex;
	struct extent *sc_busex;
	LIST_HEAD(, pci_dev) sc_devs;
	int sc_domain, sc_bus, sc_maxndevs;
	pcitag_t *sc_bridgetag;
	pci_intr_handle_t *sc_bridgeih;
	u_int sc_intrswiz;
	pcitag_t sc_intrtag;
};

extern int pci_ndomains;
extern int pci_dopm;

/*
 * Locators devices that attach to 'pcibus', as specified to config.
 */
#define	pcibuscf_bus		cf_loc[0]
#define	PCIBUS_UNK_BUS		-1		/* wildcarded 'bus' */

/*
 * Locators for PCI devices, as specified to config.
 */
#define	pcicf_dev		cf_loc[0]
#define	PCI_UNK_DEV		-1		/* wildcarded 'dev' */

#define	pcicf_function		cf_loc[1]
#define	PCI_UNK_FUNCTION	-1		/* wildcarded 'function' */

/*
 * Configuration space access and utility functions.  (Note that most,
 * e.g. make_tag, conf_read, conf_write are declared by pci_machdep.h.)
 */
int	pci_mapreg_probe(pci_chipset_tag_t, pcitag_t, int, pcireg_t *);
pcireg_t pci_mapreg_type(pci_chipset_tag_t, pcitag_t, int);
int	pci_mapreg_info(pci_chipset_tag_t, pcitag_t, int, pcireg_t,
	    bus_addr_t *, bus_size_t *, int *);
int	pci_mapreg_assign(struct pci_attach_args *, int, pcireg_t,
	    bus_addr_t *, bus_size_t *);
int	pci_mapreg_map(struct pci_attach_args *, int, pcireg_t, int,
	    bus_space_tag_t *, bus_space_handle_t *, bus_addr_t *,
	    bus_size_t *, bus_size_t);

int	pci_get_capability(pci_chipset_tag_t, pcitag_t, int,
	    int *, pcireg_t *);
int	pci_get_ht_capability(pci_chipset_tag_t, pcitag_t, int,
	    int *, pcireg_t *);
int	pci_get_ext_capability(pci_chipset_tag_t, pcitag_t, int,
	    int *, pcireg_t *);

struct msix_vector;

struct msix_vector *pci_alloc_msix_table(pci_chipset_tag_t, pcitag_t);
void	pci_free_msix_table(pci_chipset_tag_t, pcitag_t, struct msix_vector *);
void	pci_suspend_msix(pci_chipset_tag_t, pcitag_t, bus_space_tag_t,
	    pcireg_t *, struct msix_vector *);
void	pci_resume_msix(pci_chipset_tag_t, pcitag_t, bus_space_tag_t,
	    pcireg_t, struct msix_vector *);

int	pci_intr_msix_count(struct pci_attach_args *);

uint16_t pci_requester_id(pci_chipset_tag_t, pcitag_t);

int pci_matchbyid(struct pci_attach_args *, const struct pci_matchid *, int);
int pci_get_powerstate(pci_chipset_tag_t, pcitag_t);
int pci_set_powerstate(pci_chipset_tag_t, pcitag_t, int);
void pci_disable_legacy_vga(struct device *);

/*
 * Vital Product Data (PCI 2.2)
 */
int pci_vpd_read(pci_chipset_tag_t, pcitag_t, int, int, pcireg_t *);

/*
 * Helper functions for autoconfiguration.
 */
const char *pci_findvendor(pcireg_t);
const char *pci_findproduct(pcireg_t);
int	pci_find_device(struct pci_attach_args *pa,
	    int (*match)(struct pci_attach_args *));
int	pci_probe_device(struct pci_softc *, pcitag_t tag,
	    int (*)(struct pci_attach_args *), struct pci_attach_args *);
int	pci_detach_devices(struct pci_softc *, int);
void	pci_devinfo(pcireg_t, pcireg_t, int, char *, size_t);
const struct pci_quirkdata *
	pci_lookup_quirkdata(pci_vendor_id_t, pci_product_id_t);

#endif /* _KERNEL */
#endif /* _DEV_PCI_PCIVAR_H_ */
