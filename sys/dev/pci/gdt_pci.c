/*	$OpenBSD: gdt_pci.c,v 1.29 2024/09/01 03:08:56 jsg Exp $	*/

/*
 * Copyright (c) 1999, 2000 Niklas Hallqvist.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This driver would not have written if it was not for the hardware donations
 * from both ICP-Vortex and Öko.neT.  I want to thank them for their support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/queue.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ic/gdtreg.h>
#include <dev/ic/gdtvar.h>

/* Product numbers for Fibre-Channel are greater than or equal to 0x200 */
#define GDT_PCI_PRODUCT_FC	0x200

#define GDT_DEVICE_ID_MIN	0x100
#define GDT_DEVICE_ID_MAX	0x2ff
#define GDT_DEVICE_ID_NEWRX	0x300
#define GDT_DEVICE_ID_NEWRX2	0x301

/* Mapping registers for various areas */
#define GDT_PCI_DPMEM		0x10
#define GDT_PCINEW_IOMEM	0x10
#define GDT_PCINEW_IO		0x14
#define GDT_PCINEW_DPMEM	0x18

/* PCI SRAM structure */
#define GDT_MAGIC	0x00	/* u_int32_t, controller ID from BIOS */
#define GDT_NEED_DEINIT	0x04	/* u_int16_t, switch between BIOS/driver */
#define GDT_SWITCH_SUPPORT 0x06	/* u_int8_t, see GDT_NEED_DEINIT */
#define GDT_OS_USED	0x10	/* u_int8_t [16], OS code per service */
#define GDT_FW_MAGIC	0x3c	/* u_int8_t, controller ID from firmware */
#define GDT_SRAM_SZ	0x40

/* DPRAM PCI controllers */
#define GDT_DPR_IF	0x00	/* interface area */
#define GDT_6SR		(0xff0 - GDT_SRAM_SZ)
#define GDT_SEMA1	0xff1	/* volatile u_int8_t, command semaphore */
#define GDT_IRQEN	0xff5	/* u_int8_t, board interrupts enable */
#define GDT_EVENT	0xff8	/* u_int8_t, release event */
#define GDT_IRQDEL	0xffc	/* u_int8_t, acknowledge board interrupt */
#define GDT_DPRAM_SZ	0x1000

/* PLX register structure (new PCI controllers) */
#define GDT_CFG_REG	0x00	/* u_int8_t, DPRAM cfg. (2: < 1MB, 0: any) */
#define GDT_SEMA0_REG	0x40	/* volatile u_int8_t, command semaphore */
#define GDT_SEMA1_REG	0x41	/* volatile u_int8_t, status semaphore */
#define GDT_PLX_STATUS	0x44	/* volatile u_int16_t, command status */
#define GDT_PLX_SERVICE	0x46	/* u_int16_t, service */
#define GDT_PLX_INFO	0x48	/* u_int32_t [2], additional info */
#define GDT_LDOOR_REG	0x60	/* u_int8_t, PCI to local doorbell */
#define GDT_EDOOR_REG	0x64	/* volatile u_int8_t, local to PCI doorbell */
#define GDT_CONTROL0	0x68	/* u_int8_t, control0 register (unused) */
#define GDT_CONTROL1	0x69	/* u_int8_t, board interrupts enable */
#define GDT_PLX_SZ	0x80

/* DPRAM new PCI controllers */
#define GDT_IC		0x00	/* interface */
#define GDT_PCINEW_6SR	(0x4000 - GDT_SRAM_SZ)
				/* SRAM structure */
#define GDT_PCINEW_SZ	0x4000

/* i960 register structure (PCI MPR controllers) */
#define GDT_MPR_SEMA0	0x10	/* volatile u_int8_t, command semaphore */
#define GDT_MPR_SEMA1	0x12	/* volatile u_int8_t, status semaphore */
#define GDT_MPR_STATUS	0x14	/* volatile u_int16_t, command status */
#define GDT_MPR_SERVICE	0x16	/* u_int16_t, service */
#define GDT_MPR_INFO	0x18	/* u_int32_t [2], additional info */
#define GDT_MPR_LDOOR	0x20	/* u_int8_t, PCI to local doorbell */
#define GDT_MPR_EDOOR	0x2c	/* volatile u_int8_t, locl to PCI doorbell */
#define GDT_EDOOR_EN	0x34	/* u_int8_t, board interrupts enable */
#define GDT_I960_SZ	0x1000

/* DPRAM PCI MPR controllers */
#define GDT_I960R	0x00	/* 4KB i960 registers */
#define GDT_MPR_IC	GDT_I960_SZ
				/* interface area */
#define GDT_MPR_6SR	(GDT_I960_SZ + 0x3000 - GDT_SRAM_SZ)
				/* SRAM structure */
#define GDT_MPR_SZ	0x4000

int	gdt_pci_probe(struct device *, void *, void *);
void	gdt_pci_attach(struct device *, struct device *, void *);
void	gdt_pci_enable_intr(struct gdt_softc *);

void	gdt_pci_copy_cmd(struct gdt_softc *, struct gdt_ccb *);
u_int8_t gdt_pci_get_status(struct gdt_softc *);
void	gdt_pci_intr(struct gdt_softc *, struct gdt_intr_ctx *);
void	gdt_pci_release_event(struct gdt_softc *, struct gdt_ccb *);
void	gdt_pci_set_sema0(struct gdt_softc *);
int	gdt_pci_test_busy(struct gdt_softc *);

void	gdt_pcinew_copy_cmd(struct gdt_softc *, struct gdt_ccb *);
u_int8_t gdt_pcinew_get_status(struct gdt_softc *);
void	gdt_pcinew_intr(struct gdt_softc *, struct gdt_intr_ctx *);
void	gdt_pcinew_release_event(struct gdt_softc *, struct gdt_ccb *);
void	gdt_pcinew_set_sema0(struct gdt_softc *);
int	gdt_pcinew_test_busy(struct gdt_softc *);

void	gdt_mpr_copy_cmd(struct gdt_softc *, struct gdt_ccb *);
u_int8_t gdt_mpr_get_status(struct gdt_softc *);
void	gdt_mpr_intr(struct gdt_softc *, struct gdt_intr_ctx *);
void	gdt_mpr_release_event(struct gdt_softc *, struct gdt_ccb *);
void	gdt_mpr_set_sema0(struct gdt_softc *);
int	gdt_mpr_test_busy(struct gdt_softc *);

const struct cfattach gdt_pci_ca = {
	sizeof (struct gdt_softc), gdt_pci_probe, gdt_pci_attach
};

int
gdt_pci_probe(struct device *parent, void *match, void *aux)
{
        struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VORTEX &&
	    ((PCI_PRODUCT(pa->pa_id) >= GDT_DEVICE_ID_MIN &&
	    PCI_PRODUCT(pa->pa_id) <= GDT_DEVICE_ID_MAX) ||
	    PCI_PRODUCT(pa->pa_id) == GDT_DEVICE_ID_NEWRX ||
	    PCI_PRODUCT(pa->pa_id) == GDT_DEVICE_ID_NEWRX2))
		return (1);
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL &&
	    (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_GDT_RAID1 ||
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_GDT_RAID2))
		return (1);
	return (0);
}

void
gdt_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct gdt_softc *sc = (void *)self;
	bus_space_tag_t dpmemt, iomemt, iot;
	bus_space_handle_t dpmemh, iomemh, ioh;
	bus_addr_t dpmembase, iomembase, iobase;
	bus_size_t dpmemsize, iomemsize, iosize;
	u_int16_t prod;
	u_int32_t status = 0;
#define DPMEM_MAPPED		1
#define IOMEM_MAPPED		2
#define IO_MAPPED		4
#define INTR_ESTABLISHED	8
	int retries;
	u_int8_t protocol;
	pci_intr_handle_t ih;
	const char *intrstr;

	printf(": ");

	sc->sc_class = 0;
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VORTEX) {
		prod = PCI_PRODUCT(pa->pa_id);
		switch (prod) {
		case PCI_PRODUCT_VORTEX_GDT_60X0:
		case PCI_PRODUCT_VORTEX_GDT_6000B:
			sc->sc_class = GDT_PCI;
			break;

		case PCI_PRODUCT_VORTEX_GDT_6X10:
		case PCI_PRODUCT_VORTEX_GDT_6X20:
		case PCI_PRODUCT_VORTEX_GDT_6530:
		case PCI_PRODUCT_VORTEX_GDT_6550:
		case PCI_PRODUCT_VORTEX_GDT_6X17:
		case PCI_PRODUCT_VORTEX_GDT_6X27:
		case PCI_PRODUCT_VORTEX_GDT_6537:
		case PCI_PRODUCT_VORTEX_GDT_6557:
		case PCI_PRODUCT_VORTEX_GDT_6X15:
		case PCI_PRODUCT_VORTEX_GDT_6X25:
		case PCI_PRODUCT_VORTEX_GDT_6535:
		case PCI_PRODUCT_VORTEX_GDT_6555:
			sc->sc_class = GDT_PCINEW;
			break;

		case PCI_PRODUCT_VORTEX_GDT_6X17RP:
		case PCI_PRODUCT_VORTEX_GDT_6X27RP:
		case PCI_PRODUCT_VORTEX_GDT_6537RP:
		case PCI_PRODUCT_VORTEX_GDT_6557RP:
		case PCI_PRODUCT_VORTEX_GDT_6X11RP:
		case PCI_PRODUCT_VORTEX_GDT_6X21RP:
		case PCI_PRODUCT_VORTEX_GDT_6X17RD:
		case PCI_PRODUCT_VORTEX_GDT_6X27RD:
		case PCI_PRODUCT_VORTEX_GDT_6537RD:
		case PCI_PRODUCT_VORTEX_GDT_6557RD:
		case PCI_PRODUCT_VORTEX_GDT_6X11RD:
		case PCI_PRODUCT_VORTEX_GDT_6X21RD:
		case PCI_PRODUCT_VORTEX_GDT_6X18RD:
		case PCI_PRODUCT_VORTEX_GDT_6X28RD:
		case PCI_PRODUCT_VORTEX_GDT_6X38RD:
		case PCI_PRODUCT_VORTEX_GDT_6X58RD:
		case PCI_PRODUCT_VORTEX_GDT_6518RS:
		case PCI_PRODUCT_VORTEX_GDT_7X18RN:
		case PCI_PRODUCT_VORTEX_GDT_7X28RN:
		case PCI_PRODUCT_VORTEX_GDT_7X38RN:
		case PCI_PRODUCT_VORTEX_GDT_7X58RN:
		case PCI_PRODUCT_VORTEX_GDT_6X19RD:
		case PCI_PRODUCT_VORTEX_GDT_6X29RD:
		case PCI_PRODUCT_VORTEX_GDT_7X19RN:
		case PCI_PRODUCT_VORTEX_GDT_7X29RN:
		case PCI_PRODUCT_VORTEX_GDT_7X43RN:
			sc->sc_class = GDT_MPR;
		}

		/* If we don't recognize it, determine class heuristically.  */
		if (sc->sc_class == 0)
			sc->sc_class = prod < 0x100 ? GDT_PCINEW : GDT_MPR;

		if (prod >= GDT_PCI_PRODUCT_FC)
			sc->sc_class |= GDT_FC;

	} else if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_INTEL) {
		sc->sc_class = GDT_MPR;
	}

	if (pci_mapreg_map(pa,
	    GDT_CLASS(sc) == GDT_PCINEW ? GDT_PCINEW_DPMEM : GDT_PCI_DPMEM,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0, &dpmemt,
	    &dpmemh, &dpmembase, &dpmemsize, 0)) {
		if (pci_mapreg_map(pa,
		    GDT_CLASS(sc) == GDT_PCINEW ? GDT_PCINEW_DPMEM :
		    GDT_PCI_DPMEM,
		    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT_1M, 0,
		    &dpmemt,&dpmemh, &dpmembase, &dpmemsize, 0)) {
			printf("cannot map DPMEM\n");
			goto bail_out;
		}
	}
	status |= DPMEM_MAPPED;
	sc->sc_dpmemt = dpmemt;
	sc->sc_dpmemh = dpmemh;
	sc->sc_dpmembase = dpmembase;
	sc->sc_dmat = pa->pa_dmat;

	/*
	 * The GDT_PCINEW series also has two other regions to map.
	 */
	if (GDT_CLASS(sc) == GDT_PCINEW) {
		if (pci_mapreg_map(pa, GDT_PCINEW_IOMEM, PCI_MAPREG_TYPE_MEM,
		    0, &iomemt, &iomemh, &iomembase, &iomemsize, 0)) {
			printf("can't map memory mapped i/o ports\n");
			goto bail_out;
		}
		status |= IOMEM_MAPPED;

		if (pci_mapreg_map(pa, GDT_PCINEW_IO, PCI_MAPREG_TYPE_IO, 0,
		    &iot, &ioh, &iobase, &iosize, 0)) {
			printf("can't map i/o space\n");
			goto bail_out;
		}
		status |= IO_MAPPED;
		sc->sc_iot = iot;
		sc->sc_ioh = ioh;
		sc->sc_iobase = iobase;
	}

	switch (GDT_CLASS(sc)) {
	case GDT_PCI:
		bus_space_set_region_4(dpmemt, dpmemh, 0, 0,
		    GDT_DPR_IF_SZ >> 2);
		if (bus_space_read_1(dpmemt, dpmemh, 0) != 0) {
			printf("can't write to DPMEM\n");
			goto bail_out;
		}

#if 0
		/* disable board interrupts, deinit services */
		gdth_writeb(0xff, &dp6_ptr->io.irqdel);
		gdth_writeb(0x00, &dp6_ptr->io.irqen);
		gdth_writeb(0x00, &dp6_ptr->u.ic.S_Status);
		gdth_writeb(0x00, &dp6_ptr->u.ic.Cmd_Index);

		gdth_writel(pcistr->dpmem, &dp6_ptr->u.ic.S_Info[0]);
		gdth_writeb(0xff, &dp6_ptr->u.ic.S_Cmd_Indx);
		gdth_writeb(0, &dp6_ptr->io.event);
		retries = INIT_RETRIES;
		gdth_delay(20);
		while (gdth_readb(&dp6_ptr->u.ic.S_Status) != 0xff) {
		  if (--retries == 0) {
		    printk("initialization error (DEINIT failed)\n");
		    gdth_munmap(ha->brd);
		    return 0;
		  }
		  gdth_delay(1);
		}
		prot_ver = (unchar)gdth_readl(&dp6_ptr->u.ic.S_Info[0]);
		gdth_writeb(0, &dp6_ptr->u.ic.S_Status);
		gdth_writeb(0xff, &dp6_ptr->io.irqdel);
		if (prot_ver != PROTOCOL_VERSION) {
		  printk("illegal protocol version\n");
		  gdth_munmap(ha->brd);
		  return 0;
		}

		ha->type = GDT_PCI;
		ha->ic_all_size = sizeof(dp6_ptr->u);

		/* special command to controller BIOS */
		gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[0]);
		gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[1]);
		gdth_writel(0x01, &dp6_ptr->u.ic.S_Info[2]);
		gdth_writel(0x00, &dp6_ptr->u.ic.S_Info[3]);
		gdth_writeb(0xfe, &dp6_ptr->u.ic.S_Cmd_Indx);
		gdth_writeb(0, &dp6_ptr->io.event);
		retries = INIT_RETRIES;
		gdth_delay(20);
		while (gdth_readb(&dp6_ptr->u.ic.S_Status) != 0xfe) {
		  if (--retries == 0) {
		    printk("initialization error\n");
		    gdth_munmap(ha->brd);
		    return 0;
		  }
		  gdth_delay(1);
		}
		gdth_writeb(0, &dp6_ptr->u.ic.S_Status);
		gdth_writeb(0xff, &dp6_ptr->io.irqdel);
#endif

		sc->sc_ic_all_size = GDT_DPRAM_SZ;

		sc->sc_copy_cmd = gdt_pci_copy_cmd;
		sc->sc_get_status = gdt_pci_get_status;
		sc->sc_intr = gdt_pci_intr;
		sc->sc_release_event = gdt_pci_release_event;
		sc->sc_set_sema0 = gdt_pci_set_sema0;
		sc->sc_test_busy = gdt_pci_test_busy;

		break;

	case GDT_PCINEW:
		bus_space_set_region_4(dpmemt, dpmemh, 0, 0,
		    GDT_DPR_IF_SZ >> 2);
		if (bus_space_read_1(dpmemt, dpmemh, 0) != 0) {
			printf("cannot write to DPMEM\n");
			goto bail_out;
		}

#if 0
		/* disable board interrupts, deinit services */
		outb(0x00,PTR2USHORT(&ha->plx->control1));
		outb(0xff,PTR2USHORT(&ha->plx->edoor_reg));

		gdth_writeb(0x00, &dp6c_ptr->u.ic.S_Status);
		gdth_writeb(0x00, &dp6c_ptr->u.ic.Cmd_Index);

		gdth_writel(pcistr->dpmem, &dp6c_ptr->u.ic.S_Info[0]);
		gdth_writeb(0xff, &dp6c_ptr->u.ic.S_Cmd_Indx);

		outb(1,PTR2USHORT(&ha->plx->ldoor_reg));

		retries = INIT_RETRIES;
		gdth_delay(20);
		while (gdth_readb(&dp6c_ptr->u.ic.S_Status) != 0xff) {
		  if (--retries == 0) {
		    printk("initialization error (DEINIT failed)\n");
		    gdth_munmap(ha->brd);
		    return 0;
		  }
		  gdth_delay(1);
		}
		prot_ver = (unchar)gdth_readl(&dp6c_ptr->u.ic.S_Info[0]);
		gdth_writeb(0, &dp6c_ptr->u.ic.Status);
		if (prot_ver != PROTOCOL_VERSION) {
		  printk("illegal protocol version\n");
		  gdth_munmap(ha->brd);
		  return 0;
		}

		ha->type = GDT_PCINEW;
		ha->ic_all_size = sizeof(dp6c_ptr->u);

		/* special command to controller BIOS */
		gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[0]);
		gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[1]);
		gdth_writel(0x01, &dp6c_ptr->u.ic.S_Info[2]);
		gdth_writel(0x00, &dp6c_ptr->u.ic.S_Info[3]);
		gdth_writeb(0xfe, &dp6c_ptr->u.ic.S_Cmd_Indx);

		outb(1,PTR2USHORT(&ha->plx->ldoor_reg));

		retries = INIT_RETRIES;
		gdth_delay(20);
		while (gdth_readb(&dp6c_ptr->u.ic.S_Status) != 0xfe) {
		  if (--retries == 0) {
		    printk("initialization error\n");
		    gdth_munmap(ha->brd);
		    return 0;
		  }
		  gdth_delay(1);
		}
		gdth_writeb(0, &dp6c_ptr->u.ic.S_Status);
#endif

		sc->sc_ic_all_size = GDT_PCINEW_SZ;

		sc->sc_copy_cmd = gdt_pcinew_copy_cmd;
		sc->sc_get_status = gdt_pcinew_get_status;
		sc->sc_intr = gdt_pcinew_intr;
		sc->sc_release_event = gdt_pcinew_release_event;
		sc->sc_set_sema0 = gdt_pcinew_set_sema0;
		sc->sc_test_busy = gdt_pcinew_test_busy;

		break;

	case GDT_MPR:
		bus_space_write_4(dpmemt, dpmemh, GDT_MPR_IC, GDT_MPR_MAGIC);
		if (bus_space_read_4(dpmemt, dpmemh, GDT_MPR_IC) !=
		    GDT_MPR_MAGIC) {
			printf("cannot access DPMEM at 0x%lx (shadowed?)\n",
			    dpmembase);
			goto bail_out;
		}

		/*
		 * XXX Here the Linux driver has a weird remapping logic I
		 * don't understand.  My controller does not need it, and I
		 * cannot see what purpose it serves, therefore I did not
		 * do anything similar.
		 */

		bus_space_set_region_4(dpmemt, dpmemh, GDT_I960_SZ, 0,
		    GDT_DPR_IF_SZ >> 2);

		/* Disable everything */
		bus_space_write_1(dpmemt, dpmemh, GDT_EDOOR_EN,
		    bus_space_read_1(dpmemt, dpmemh, GDT_EDOOR_EN) | 4);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_EDOOR, 0xff);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_STATUS,
		    0);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_IC + GDT_CMD_INDEX,
		    0);

		bus_space_write_4(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_INFO,
		    dpmembase);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_CMD_INDX,
		    0xff);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_LDOOR, 1);

		DELAY(20);
		retries = GDT_RETRIES;
		while (bus_space_read_1(dpmemt, dpmemh,
		    GDT_MPR_IC + GDT_S_STATUS) != 0xff) {
			if (--retries == 0) {
				printf("DEINIT failed (status 0x%x)\n",
				    bus_space_read_1(dpmemt, dpmemh,
				    GDT_MPR_IC + GDT_S_STATUS));
				goto bail_out;
			}
			DELAY(1);
		}

		protocol = (u_int8_t)bus_space_read_4(dpmemt, dpmemh,
		    GDT_MPR_IC + GDT_S_INFO);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_STATUS,
		    0);
		if (protocol != GDT_PROTOCOL_VERSION) {
		 	printf("unsupported protocol %d\n", protocol);
			goto bail_out;
		}

		/* special command to controller BIOS */
		bus_space_write_4(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_INFO, 0);
		bus_space_write_4(dpmemt, dpmemh,
		    GDT_MPR_IC + GDT_S_INFO + sizeof (u_int32_t), 0);
		bus_space_write_4(dpmemt, dpmemh,
		    GDT_MPR_IC + GDT_S_INFO + 2 * sizeof (u_int32_t), 1);
		bus_space_write_4(dpmemt, dpmemh,
		    GDT_MPR_IC + GDT_S_INFO + 3 * sizeof (u_int32_t), 0);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_CMD_INDX,
		    0xfe);
		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_LDOOR, 1);

		DELAY(20);
		retries = GDT_RETRIES;
		while (bus_space_read_1(dpmemt, dpmemh,
		    GDT_MPR_IC + GDT_S_STATUS) != 0xfe) {
			if (--retries == 0) {
				printf("initialization error\n");
				goto bail_out;
			}
			DELAY(1);
		}

		bus_space_write_1(dpmemt, dpmemh, GDT_MPR_IC + GDT_S_STATUS,
		    0);

		sc->sc_ic_all_size = GDT_MPR_SZ;

		sc->sc_copy_cmd = gdt_mpr_copy_cmd;
		sc->sc_get_status = gdt_mpr_get_status;
		sc->sc_intr = gdt_mpr_intr;
		sc->sc_release_event = gdt_mpr_release_event;
		sc->sc_set_sema0 = gdt_mpr_set_sema0;
		sc->sc_test_busy = gdt_mpr_test_busy;
	}

	if (pci_intr_map(pa, &ih)) {
		printf("couldn't map interrupt\n");
		goto bail_out;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO, gdt_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto bail_out;
	}
	status |= INTR_ESTABLISHED;
	if (intrstr != NULL)
		printf("%s ", intrstr);

	if (gdt_attach(sc))
		goto bail_out;

	gdt_pci_enable_intr(sc);

	return;

 bail_out:
	if (status & DPMEM_MAPPED)
		bus_space_unmap(dpmemt, dpmemh, dpmemsize);
	if (status & IOMEM_MAPPED)
		bus_space_unmap(iomemt, iomemh, iomembase);
	if (status & IO_MAPPED)
		bus_space_unmap(iot, ioh, iosize);
	if (status & INTR_ESTABLISHED)
		pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
	return;
}

/* Enable interrupts */
void
gdt_pci_enable_intr(struct gdt_softc *sc)
{
	GDT_DPRINTF(GDT_D_INTR, ("gdt_pci_enable_intr(%p) ", sc));

	switch(GDT_CLASS(sc)) {
	case GDT_PCI:
		bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_IRQDEL,
		    1);
		bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh,
		    GDT_CMD_INDEX, 0);
		bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_IRQEN,
		    1);
		break;

	case GDT_PCINEW:
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, GDT_EDOOR_REG,
		    0xff);
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, GDT_CONTROL1, 3);
		break;

	case GDT_MPR:
		bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh,
		    GDT_MPR_EDOOR, 0xff);
		bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_EDOOR_EN,
		    bus_space_read_1(sc->sc_dpmemt, sc->sc_dpmemh,
		    GDT_EDOOR_EN) & ~4);
		break;
	}
}

/*
 * "old" PCI controller-specific functions
 */

void
gdt_pci_copy_cmd(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	/* XXX Not yet implemented */
}

u_int8_t
gdt_pci_get_status(struct gdt_softc *sc)
{
	/* XXX Not yet implemented */
	return (0);
}

void
gdt_pci_intr(struct gdt_softc *sc, struct gdt_intr_ctx *ctx)
{
	/* XXX Not yet implemented */
}

void
gdt_pci_release_event(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	/* XXX Not yet implemented */
}

void
gdt_pci_set_sema0(struct gdt_softc *sc)
{
	bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_SEMA0, 1);
}

int
gdt_pci_test_busy(struct gdt_softc *sc)
{
	/* XXX Not yet implemented */
	return (0);
}

/*
 * "new" PCI controller-specific functions
 */

void
gdt_pcinew_copy_cmd(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	/* XXX Not yet implemented */
}

u_int8_t
gdt_pcinew_get_status(struct gdt_softc *sc)
{
	/* XXX Not yet implemented */
	return (0);
}

void
gdt_pcinew_intr(struct gdt_softc *sc, struct gdt_intr_ctx *ctx)
{
	/* XXX Not yet implemented */
}

void
gdt_pcinew_release_event(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	/* XXX Not yet implemented */
}

void
gdt_pcinew_set_sema0(struct gdt_softc *sc)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, GDT_SEMA0_REG, 1);
}

int
gdt_pcinew_test_busy(struct gdt_softc *sc)
{
	/* XXX Not yet implemented */
	return (0);
}

/*
 * MPR PCI controller-specific functions
 */

void
gdt_mpr_copy_cmd(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	u_int16_t cp_count = roundup(sc->sc_cmd_len, sizeof (u_int32_t));
	u_int16_t dp_offset = sc->sc_cmd_off;
	u_int16_t cmd_no = sc->sc_cmd_cnt++;

	GDT_DPRINTF(GDT_D_CMD, ("gdt_mpr_copy_cmd(%p) ", sc));

	sc->sc_cmd_off += cp_count;

	bus_space_write_2(sc->sc_dpmemt, sc->sc_dpmemh,
	    GDT_MPR_IC + GDT_COMM_QUEUE + cmd_no * GDT_COMM_Q_SZ + GDT_OFFSET,
	    GDT_DPMEM_COMMAND_OFFSET + dp_offset);
	bus_space_write_2(sc->sc_dpmemt, sc->sc_dpmemh,
	    GDT_MPR_IC + GDT_COMM_QUEUE + cmd_no * GDT_COMM_Q_SZ + GDT_SERV_ID,
	    ccb->gc_service);
	bus_space_write_raw_region_4(sc->sc_dpmemt, sc->sc_dpmemh,
	    GDT_MPR_IC + GDT_DPR_CMD + dp_offset, sc->sc_cmd, cp_count);
}

u_int8_t
gdt_mpr_get_status(struct gdt_softc *sc)
{
	GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_get_status(%p) ", sc));

	return bus_space_read_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_MPR_EDOOR);
}

void
gdt_mpr_intr(struct gdt_softc *sc, struct gdt_intr_ctx *ctx)
{
	GDT_DPRINTF(GDT_D_INTR, ("gdt_mpr_intr(%p) ", sc));

	if (ctx->istatus & 0x80) {		/* error flag */
		ctx->istatus &= ~0x80;
		ctx->cmd_status = bus_space_read_2(sc->sc_dpmemt,
		    sc->sc_dpmemh, GDT_MPR_STATUS);
		if (ctx->istatus == GDT_ASYNCINDEX) {
			ctx->service = bus_space_read_2(sc->sc_dpmemt,
			    sc->sc_dpmemh, GDT_MPR_SERVICE);
			ctx->info2 = bus_space_read_4(sc->sc_dpmemt,
			    sc->sc_dpmemh, GDT_MPR_INFO + sizeof (u_int32_t));
		}
	} else					/* no error */
		ctx->cmd_status = GDT_S_OK;

	ctx->info =
	    bus_space_read_4(sc->sc_dpmemt, sc->sc_dpmemh, GDT_MPR_INFO);

	if (gdt_polling)			/* init. -> more info */
		ctx->info2 = bus_space_read_4(sc->sc_dpmemt, sc->sc_dpmemh,
		    GDT_MPR_INFO + sizeof (u_int32_t));
	bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_MPR_EDOOR, 0xff);
	bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_MPR_SEMA1, 0);
}

void
gdt_mpr_release_event(struct gdt_softc *sc, struct gdt_ccb *ccb)
{
	GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_release_event(%p) ", sc));

	if (gdt_dec16(sc->sc_cmd + GDT_CMD_OPCODE) == GDT_INIT)
		ccb->gc_service |= 0x80;
	bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_MPR_LDOOR, 1);
}

void
gdt_mpr_set_sema0(struct gdt_softc *sc)
{
	GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_set_sema0(%p) ", sc));

	bus_space_write_1(sc->sc_dpmemt, sc->sc_dpmemh, GDT_MPR_SEMA0, 1);
}

int
gdt_mpr_test_busy(struct gdt_softc *sc)
{
	GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_test_busy(%p) ", sc));

	return (bus_space_read_1(sc->sc_dpmemt, sc->sc_dpmemh,
	    GDT_MPR_SEMA0) & 1);
}
