/*	$OpenBSD: jmb.c,v 1.12 2025/07/15 13:40:02 jsg Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/* JMicron registers */
#define JM_PCI_CTL0		0x40 /* control register 0 */
#define  JM_PCI_CTL0_ROM_EN		(1<<31)	/* External Option ROM */
#define  JM_PCI_CTL0_IDWR_EN		(1<<30) /* Device ID Write */
#define  JM_PCI_CTL0_MSI64_EN		(1<<25) /* 64bit MSI Addr Mode */
#define  JM_PCI_CTL0_MSI_EN		(1<<24) /* MSI Addr Mode */
#define  JM_PCI_CTL0_IDEDMA_CFG		(1<<23) /* PCIIDE DMA Chan Cfg */
#define  JM_PCI_CTL0_PCIIDE_CS		(1<<22) /* PCIIDE channels Swap */
#define  JM_PCI_CTL0_SATA_PS		(1<<21) /* SATA channel M/S swap */
#define  JM_PCI_CTL0_AHCI_PS		(1<<20) /* SATA AHCI ports swap */
#define  JM_PCI_CTL0_F1_SUBCLASS_M	0xc0000 /* subclass for func 1 */
#define  JM_PCI_CTL0_F0_SUBCLASS_M	0x30000 /* subclass for func 0 */
#define  JM_PCI_CTL0_SUBCLASS_IDE	0x0 /* IDE Controller */
#define  JM_PCI_CTL0_SUBCLASS_RAID	0x1 /* RAID Controller */
#define  JM_PCI_CTL0_SUBCLASS_AHCI	0x2 /* AHCI Controller */
#define  JM_PCI_CTL0_SUBCLASS_OTHER	0x3 /* Other Mass Storage */
#define  JM_PCI_CTL0_F1_SUBCLASS(_m)	((_m)<<18) /* subclass for func 1 */
#define  JM_PCI_CTL0_F0_SUBCLASS(_m)	((_m)<<16) /* subclass for func 0 */
#define  JM_PCI_CTL0_SATA1_AHCI		(1<<15) /* SATA port 1 AHCI enable */
#define  JM_PCI_CTL0_SATA1_IDE		(1<<14) /* SATA port 1 IDE enable */
#define  JM_PCI_CTL0_SATA0_AHCI		(1<<13) /* SATA port 0 AHCI enable */
#define  JM_PCI_CTL0_SATA0_IDE		(1<<12) /* SATA port 0 PCIIDE enable */
#define  JM_PCI_CTL0_AHCI_F1		(1<<9) /* AHCI on function 1 */
#define  JM_PCI_CTL0_AHCI_EN		(1<<8) /* AHCI enable */
#define  JM_PCI_CTL0_PATA0_RST		(1<<6) /* PATA port 0 reset */
#define  JM_PCI_CTL0_PATA0_EN		(1<<5) /* PATA port 0 enable */
#define  JM_PCI_CTL0_PATA0_SEC		(1<<4) /* PATA 0 enable on 2nd chan */
#define  JM_PCI_CTL0_PATA0_40P		(1<<3) /* PATA 0 40pin cable */
#define  JM_PCI_CTL0_PCIIDE_F1		(1<<1) /* PCIIDE on function 1 */
#define  JM_PCI_CTL0_PATA0_PRI		(1<<0) /* PATA 0 enable on 1st chan */

#define JM_PCI_CTL5		0x80 /* control register 8 */
#define  JM_PCI_CTL5_PATA1_PRI		(1<<24) /* force PATA 1 on chan0 */

int		jmb_match(struct device *, void *, void *);
void		jmb_attach(struct device *, struct device *, void *);
int		jmb_print(void *, const char *);

struct jmb_softc {
	struct device		sc_dev;
};

const struct cfattach jmb_ca = {
	sizeof(struct jmb_softc),
	jmb_match,
	jmb_attach,
	config_detach_children
};

struct cfdriver jmb_cd = {
	NULL, "jmb", DV_DULL
};

static const struct pci_matchid jmb_devices[] = {
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB360 },
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB361 },
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB362 },
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB363 },
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB365 },
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB366 },
	{ PCI_VENDOR_JMICRON,	PCI_PRODUCT_JMICRON_JMB368 }
};

int
jmb_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args		*pa = aux;

	return (pci_matchbyid(pa, jmb_devices,
	    sizeof(jmb_devices) / sizeof(jmb_devices[0])) * 3);
}

void
jmb_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args		*pa = aux, jpa;
	u_int32_t			ctl0, ctl5;
	int				sata = 0, pata = 0;

	ctl0 = pci_conf_read(pa->pa_pc, pa->pa_tag, JM_PCI_CTL0);
	ctl5 = pci_conf_read(pa->pa_pc, pa->pa_tag, JM_PCI_CTL5);

	/* configure sata bits if it is on this function */
	if (pa->pa_function == (ISSET(ctl0, JM_PCI_CTL0_AHCI_F1) ? 1 : 0)) {
		ctl0 &= ~(JM_PCI_CTL0_AHCI_EN | JM_PCI_CTL0_SATA0_IDE |
		    JM_PCI_CTL0_SATA0_AHCI | JM_PCI_CTL0_SATA1_IDE |
		    JM_PCI_CTL0_SATA1_AHCI);

		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_JMICRON_JMB360:
		case PCI_PRODUCT_JMICRON_JMB361:
		case PCI_PRODUCT_JMICRON_JMB362:
		case PCI_PRODUCT_JMICRON_JMB363:
		case PCI_PRODUCT_JMICRON_JMB365:
		case PCI_PRODUCT_JMICRON_JMB366:
			/* enable AHCI */
			ctl0 |= JM_PCI_CTL0_AHCI_EN | JM_PCI_CTL0_SATA0_AHCI |
			    JM_PCI_CTL0_SATA1_AHCI;
			sata = 1;
			break;
		}
	}

	/* configure pata bits if it is on this function */
	if (pa->pa_function == (ISSET(ctl0, JM_PCI_CTL0_PCIIDE_F1) ? 1 : 0)) {
		ctl0 &= ~(JM_PCI_CTL0_PCIIDE_CS | JM_PCI_CTL0_IDEDMA_CFG);
		ctl5 &= ~JM_PCI_CTL5_PATA1_PRI;

		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_JMICRON_JMB366:
		case PCI_PRODUCT_JMICRON_JMB365:
			/* wire the second PATA port in the right place */
			ctl5 |= JM_PCI_CTL5_PATA1_PRI;
			/* FALLTHROUGH */
		case PCI_PRODUCT_JMICRON_JMB363:
		case PCI_PRODUCT_JMICRON_JMB361:
		case PCI_PRODUCT_JMICRON_JMB368:
			ctl0 |= JM_PCI_CTL0_PCIIDE_CS | JM_PCI_CTL0_IDEDMA_CFG;
			pata = 1;
			break;
		}
	}

	pci_conf_write(pa->pa_pc, pa->pa_tag, JM_PCI_CTL0, ctl0);
	pci_conf_write(pa->pa_pc, pa->pa_tag, JM_PCI_CTL5, ctl5);

	printf("\n");

	jpa = *pa;

	if (sata) {
		/* tweak the class to look like ahci, then try to attach it */
		jpa.pa_class = (PCI_CLASS_MASS_STORAGE << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_MASS_STORAGE_SATA << PCI_SUBCLASS_SHIFT) |
		    (0x01 << PCI_INTERFACE_SHIFT); /* AHCI_PCI_INTERFACE */
		config_found(self, &jpa, jmb_print);
	}

	if (pata) {
		/* set things up for pciide */
		jpa.pa_class = (PCI_CLASS_MASS_STORAGE << PCI_CLASS_SHIFT) |
		    (PCI_SUBCLASS_MASS_STORAGE_IDE << PCI_SUBCLASS_SHIFT) |
		    (0x85 << PCI_INTERFACE_SHIFT);
		config_found(self, &jpa, jmb_print);
	}
}

int
jmb_print(void *aux, const char *pnp)
{
	struct pci_attach_args		*pa = aux;
	char				devinfo[256];

	if (pnp != NULL) {
		pci_devinfo(pa->pa_id, pa->pa_class, 1, devinfo,
		    sizeof(devinfo));
		printf("%s at %s", devinfo, pnp);
	}

	return (UNCONF);
}
