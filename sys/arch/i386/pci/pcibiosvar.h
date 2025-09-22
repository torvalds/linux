/*	$OpenBSD: pcibiosvar.h,v 1.19 2024/05/13 01:15:50 jsg Exp $	*/
/*	$NetBSD: pcibios.h,v 1.2 2000/04/28 17:15:16 uch Exp $	*/

/*
 * Copyright (c) 1999, by UCHIYAMA Yasushi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Data structure definitions for the PCI BIOS interface.
 */

#define	PCIBIOS_ADDR_FIXUP	0x001
#define	PCIBIOS_BUS_FIXUP	0x002
#define	PCIBIOS_INTR_FIXUP	0x004
#define	PCIBIOS_INTR_GUESS	0x008
#define	PCIBIOS_VERBOSE		0x010
#define	PCIBIOS_INTRDEBUG	0x020
#define	PCIBIOS_FIXUP_FORCE	0x040

/*
 * PCI BIOS return codes.
 */
#define	PCIBIOS_SUCCESS			0x00
#define	PCIBIOS_SERVICE_NOT_PRESENT	0x80
#define	PCIBIOS_FUNCTION_NOT_SUPPORTED	0x81
#define	PCIBIOS_BAD_VENDOR_ID		0x83
#define	PCIBIOS_DEVICE_NOT_FOUND	0x86
#define	PCIBIOS_BAD_REGISTER_NUMBER	0x87
#define	PCIBIOS_SET_FAILED		0x88
#define	PCIBIOS_BUFFER_TOO_SMALL	0x89

struct pcibios_softc {
	struct device sc_dev;

	int max_bus;

	/* address fixup guts */
	struct extent *extent_mem;
	struct extent *extent_port;
	bus_addr_t mem_alloc_start;
	bus_addr_t port_alloc_start;
	int nbogus;
};

/*
 * PCI IRQ Routing Table definitions.
 */

/*
 * Slot entry (per PCI 2.1)
 */
struct pcibios_linkmap {
	u_int8_t	link;
	u_int16_t	bitmap;
} __packed;

struct pcibios_intr_routing {
	u_int8_t	bus;
	u_int8_t	device;
	struct pcibios_linkmap linkmap[4];	/* INT[A:D]# */
	u_int8_t	slot;
	u_int8_t	reserved;
} __packed;

/*
 * $PIR header.  Reference:
 *
 *	http://www.microsoft.com/whdc/archive/pciirq.mspx
 */
struct pcibios_pir_header {
	u_int32_t	signature;		/* $PIR */
	u_int16_t	version;
	u_int16_t	tablesize;
	u_int8_t	router_bus;
	u_int8_t	router_devfunc;
	u_int16_t	exclusive_irq;
	u_int32_t	compat_router;		/* PCI vendor/product */
	u_int32_t	miniport;
	u_int8_t	reserved[11];
	u_int8_t	checksum;
} __packed;

#define	PIR_DEVFUNC_DEVICE(devfunc)	(((devfunc) >> 3) & 0x1f)
#define	PIR_DEVFUNC_FUNCTION(devfunc)	((devfunc) & 7)
#define	PIR_DEVFUNC_COMPOSE(dev,func)	((((dev) &0x1f) << 3) | ((func) & 7))

extern struct pcibios_pir_header pcibios_pir_header;
extern struct pcibios_intr_routing *pcibios_pir_table;
extern int pcibios_pir_table_nentries;

extern int pcibios_flags;

typedef void *pciintr_icu_handle_t;

struct pciintr_icu {
	int	(*pi_getclink)(pciintr_icu_handle_t, int, int *);
	int	(*pi_get_intr)(pciintr_icu_handle_t, int, int *);
	int	(*pi_set_intr)(pciintr_icu_handle_t, int, int);
	int	(*pi_get_trigger)(pciintr_icu_handle_t, int, int *);
	int	(*pi_set_trigger)(pciintr_icu_handle_t, int, int);
};

typedef const struct pciintr_icu *pciintr_icu_tag_t;

#define	pciintr_icu_getclink(t, h, link, pirqp)				\
	(*(t)->pi_getclink)((h), (link), (pirqp))
#define	pciintr_icu_get_intr(t, h, pirq, irqp)				\
	(*(t)->pi_get_intr)((h), (pirq), (irqp))
#define	pciintr_icu_set_intr(t, h, pirq, irq)				\
	(*(t)->pi_set_intr)((h), (pirq), (irq))
#define	pciintr_icu_get_trigger(t, h, irq, triggerp)			\
	(*(t)->pi_get_trigger)((h), (irq), (triggerp))
#define	pciintr_icu_set_trigger(t, h, irq, trigger)			\
	(*(t)->pi_set_trigger)((h), (irq), (trigger))

#define	PCIBIOS_PRINTV(arg) \
	do { \
		if (pcibios_flags & PCIBIOS_VERBOSE) \
			printf arg; \
	} while (0)

#define	PCIADDR_SEARCH_IO	0
#define	PCIADDR_SEARCH_MEM	1

#define	PCI_INT_VIA_ISA		0x20000000 /* XXX see APIC_INT_VIA_APIC */

int  pci_intr_fixup(struct pcibios_softc *, pci_chipset_tag_t, bus_space_tag_t);
int  pci_bus_fixup(pci_chipset_tag_t, int);
void pci_addr_fixup(struct pcibios_softc *, pci_chipset_tag_t, int);
void pci_device_foreach(struct pcibios_softc *, pci_chipset_tag_t, int,
    void (*)(struct pcibios_softc *, pci_chipset_tag_t, pcitag_t));
int  pci_intr_header_fixup(pci_chipset_tag_t, pcitag_t, pci_intr_handle_t *);
int  pci_intr_route_link(pci_chipset_tag_t, pci_intr_handle_t *);
int  pci_intr_post_fixup(void);

/*
 * Init functions for our known PCI ICUs.
 */
int	piix_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	opti82c558_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	opti82c700_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	via82c586_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	via8231_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	sis85c503_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	amd756_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	ali1543_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);
int	osb4_init(pci_chipset_tag_t, bus_space_tag_t, pcitag_t,
	    pciintr_icu_tag_t *, pciintr_icu_handle_t *);

