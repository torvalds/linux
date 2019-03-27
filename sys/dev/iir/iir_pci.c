/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *       Copyright (c) 2000-03 ICP vortex GmbH
 *       Copyright (c) 2002-03 Intel Corporation
 *       Copyright (c) 2003    Adaptec Inc.
 *       All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *  iir_pci.c:  PCI Bus Attachment for Intel Integrated RAID Controller driver
 *
 *  Written by: Achim Leubner <achim.leubner@intel.com>
 *  Written by: Achim Leubner <achim_leubner@adaptec.com>
 *  Fixes/Additions: Boji Tony Kannanthanam <boji.t.kannanthanam@intel.com>
 *
 *  TODO:
 */

/* #include "opt_iir.h" */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/bus.h> 

#include <machine/bus.h> 
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <cam/scsi/scsi_all.h>

#include <dev/iir/iir.h>

/* Mapping registers for various areas */
#define PCI_DPMEM       PCIR_BAR(0)

/* Product numbers for Fibre-Channel are greater than or equal to 0x200 */
#define GDT_PCI_PRODUCT_FC      0x200

/* PCI SRAM structure */
#define GDT_MAGIC       0x00    /* u_int32_t, controller ID from BIOS */
#define GDT_NEED_DEINIT 0x04    /* u_int16_t, switch between BIOS/driver */
#define GDT_SWITCH_SUPPORT 0x06 /* u_int8_t, see GDT_NEED_DEINIT */
#define GDT_OS_USED     0x10    /* u_int8_t [16], OS code per service */
#define GDT_FW_MAGIC    0x3c    /* u_int8_t, controller ID from firmware */
#define GDT_SRAM_SZ     0x40

/* DPRAM PCI controllers */
#define GDT_DPR_IF      0x00    /* interface area */
#define GDT_6SR         (0xff0 - GDT_SRAM_SZ)
#define GDT_SEMA1       0xff1   /* volatile u_int8_t, command semaphore */
#define GDT_IRQEN       0xff5   /* u_int8_t, board interrupts enable */
#define GDT_EVENT       0xff8   /* u_int8_t, release event */
#define GDT_IRQDEL      0xffc   /* u_int8_t, acknowledge board interrupt */
#define GDT_DPRAM_SZ    0x1000

/* PLX register structure (new PCI controllers) */
#define GDT_CFG_REG     0x00    /* u_int8_t, DPRAM cfg. (2: < 1MB, 0: any) */
#define GDT_SEMA0_REG   0x40    /* volatile u_int8_t, command semaphore */
#define GDT_SEMA1_REG   0x41    /* volatile u_int8_t, status semaphore */
#define GDT_PLX_STATUS  0x44    /* volatile u_int16_t, command status */
#define GDT_PLX_SERVICE 0x46    /* u_int16_t, service */
#define GDT_PLX_INFO    0x48    /* u_int32_t [2], additional info */
#define GDT_LDOOR_REG   0x60    /* u_int8_t, PCI to local doorbell */
#define GDT_EDOOR_REG   0x64    /* volatile u_int8_t, local to PCI doorbell */
#define GDT_CONTROL0    0x68    /* u_int8_t, control0 register (unused) */
#define GDT_CONTROL1    0x69    /* u_int8_t, board interrupts enable */
#define GDT_PLX_SZ      0x80

/* DPRAM new PCI controllers */
#define GDT_IC          0x00    /* interface */
#define GDT_PCINEW_6SR  (0x4000 - GDT_SRAM_SZ)
                                /* SRAM structure */
#define GDT_PCINEW_SZ   0x4000

/* i960 register structure (PCI MPR controllers) */
#define GDT_MPR_SEMA0   0x10    /* volatile u_int8_t, command semaphore */
#define GDT_MPR_SEMA1   0x12    /* volatile u_int8_t, status semaphore */
#define GDT_MPR_STATUS  0x14    /* volatile u_int16_t, command status */
#define GDT_MPR_SERVICE 0x16    /* u_int16_t, service */
#define GDT_MPR_INFO    0x18    /* u_int32_t [2], additional info */
#define GDT_MPR_LDOOR   0x20    /* u_int8_t, PCI to local doorbell */
#define GDT_MPR_EDOOR   0x2c    /* volatile u_int8_t, locl to PCI doorbell */
#define GDT_EDOOR_EN    0x34    /* u_int8_t, board interrupts enable */
#define GDT_SEVERITY    0xefc   /* u_int8_t, event severity */
#define GDT_EVT_BUF     0xf00   /* u_int8_t [256], event buffer */
#define GDT_I960_SZ     0x1000

/* DPRAM PCI MPR controllers */
#define GDT_I960R       0x00    /* 4KB i960 registers */
#define GDT_MPR_IC      GDT_I960_SZ
                                /* i960 register area */
#define GDT_MPR_6SR     (GDT_I960_SZ + 0x3000 - GDT_SRAM_SZ)
                                /* DPRAM struct. */
#define GDT_MPR_SZ      (0x3000 - GDT_SRAM_SZ)

static int      iir_pci_probe(device_t dev);
static int      iir_pci_attach(device_t dev);

void            gdt_pci_enable_intr(struct gdt_softc *);

void            gdt_mpr_copy_cmd(struct gdt_softc *, struct gdt_ccb *);
u_int8_t        gdt_mpr_get_status(struct gdt_softc *);
void            gdt_mpr_intr(struct gdt_softc *, struct gdt_intr_ctx *);
void            gdt_mpr_release_event(struct gdt_softc *);
void            gdt_mpr_set_sema0(struct gdt_softc *);
int             gdt_mpr_test_busy(struct gdt_softc *);

static device_method_t iir_pci_methods[] = {
        /* Device interface */
        DEVMETHOD(device_probe,         iir_pci_probe),
        DEVMETHOD(device_attach,        iir_pci_attach),
        { 0, 0}
};


static  driver_t iir_pci_driver =
{
        "iir",
        iir_pci_methods,
        sizeof(struct gdt_softc)
};

static devclass_t iir_devclass;

DRIVER_MODULE(iir, pci, iir_pci_driver, iir_devclass, 0, 0);
MODULE_DEPEND(iir, pci, 1, 1, 1);
MODULE_DEPEND(iir, cam, 1, 1, 1);

static int
iir_pci_probe(device_t dev)
{
    if (pci_get_vendor(dev) == INTEL_VENDOR_ID_IIR &&
        pci_get_device(dev) == INTEL_DEVICE_ID_IIR) {
        device_set_desc(dev, "Intel Integrated RAID Controller");
        return (BUS_PROBE_DEFAULT);
    }
    if (pci_get_vendor(dev) == GDT_VENDOR_ID &&
        ((pci_get_device(dev) >= GDT_DEVICE_ID_MIN &&
        pci_get_device(dev) <= GDT_DEVICE_ID_MAX) ||
        pci_get_device(dev) == GDT_DEVICE_ID_NEWRX)) {
        device_set_desc(dev, "ICP Disk Array Controller");
        return (BUS_PROBE_DEFAULT);
    }
    return (ENXIO);
}


static int
iir_pci_attach(device_t dev)
{
    struct gdt_softc    *gdt;
    struct resource     *irq = NULL;
    int                 retries, rid, error = 0;
    void                *ih;
    u_int8_t            protocol;  

    gdt = device_get_softc(dev);
    mtx_init(&gdt->sc_lock, "iir", NULL, MTX_DEF);

    /* map DPMEM */
    rid = PCI_DPMEM;
    gdt->sc_dpmem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
    if (gdt->sc_dpmem == NULL) {
        device_printf(dev, "can't allocate register resources\n");
        error = ENOMEM;
        goto err;
    }

    /* get IRQ */
    rid = 0;
    irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
                                 RF_ACTIVE | RF_SHAREABLE);
    if (irq == NULL) {
        device_printf(dev, "can't find IRQ value\n");
        error = ENOMEM;
        goto err;
    }

    gdt->sc_devnode = dev;
    gdt->sc_init_level = 0;
    gdt->sc_hanum = device_get_unit(dev);
    gdt->sc_bus = pci_get_bus(dev);
    gdt->sc_slot = pci_get_slot(dev);
    gdt->sc_vendor = pci_get_vendor(dev);
    gdt->sc_device = pci_get_device(dev);
    gdt->sc_subdevice = pci_get_subdevice(dev);
    gdt->sc_class = GDT_MPR;
/* no FC ctr.
    if (gdt->sc_device >= GDT_PCI_PRODUCT_FC)
        gdt->sc_class |= GDT_FC;
*/

    /* initialize RP controller */
    /* check and reset interface area */
    bus_write_4(gdt->sc_dpmem, GDT_MPR_IC, htole32(GDT_MPR_MAGIC));
    if (bus_read_4(gdt->sc_dpmem, GDT_MPR_IC) != htole32(GDT_MPR_MAGIC)) {
	device_printf(dev, "cannot access DPMEM at 0x%jx (shadowed?)\n",
	    rman_get_start(gdt->sc_dpmem));
        error = ENXIO;
        goto err;
    }
    bus_set_region_4(gdt->sc_dpmem, GDT_I960_SZ, htole32(0), GDT_MPR_SZ >> 2);

    /* Disable everything */
    bus_write_1(gdt->sc_dpmem, GDT_EDOOR_EN,
	bus_read_1(gdt->sc_dpmem, GDT_EDOOR_EN) | 4);
    bus_write_1(gdt->sc_dpmem, GDT_MPR_EDOOR, 0xff);
    bus_write_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_STATUS, 0);
    bus_write_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_CMD_INDEX, 0);

    bus_write_4(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_INFO,
	htole32(rman_get_start(gdt->sc_dpmem)));
    bus_write_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_CMD_INDX, 0xff);
    bus_write_1(gdt->sc_dpmem, GDT_MPR_LDOOR, 1);

    DELAY(20);
    retries = GDT_RETRIES;
    while (bus_read_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_STATUS) != 0xff) {
        if (--retries == 0) {
            device_printf(dev, "DEINIT failed\n");
            error = ENXIO;
            goto err;
        }
        DELAY(1);
    }

    protocol = (uint8_t)le32toh(bus_read_4(gdt->sc_dpmem,
	    GDT_MPR_IC + GDT_S_INFO));
    bus_write_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_STATUS, 0);
    if (protocol != GDT_PROTOCOL_VERSION) {
        device_printf(dev, "unsupported protocol %d\n", protocol);
        error = ENXIO;
        goto err;
    }
    
    /* special command to controller BIOS */
    bus_write_4(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_INFO, htole32(0));
    bus_write_4(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_INFO + sizeof (u_int32_t),
	htole32(0));
    bus_write_4(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_INFO + 2 * sizeof (u_int32_t),
	htole32(1));
    bus_write_4(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_INFO + 3 * sizeof (u_int32_t),
	htole32(0));
    bus_write_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_CMD_INDX, 0xfe);
    bus_write_1(gdt->sc_dpmem, GDT_MPR_LDOOR, 1);

    DELAY(20);
    retries = GDT_RETRIES;
    while (bus_read_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_STATUS) != 0xfe) {
        if (--retries == 0) {
            device_printf(dev, "initialization error\n");
            error = ENXIO;
            goto err;
        }
        DELAY(1);
    }

    bus_write_1(gdt->sc_dpmem, GDT_MPR_IC + GDT_S_STATUS, 0);

    gdt->sc_ic_all_size = GDT_MPR_SZ;
    
    gdt->sc_copy_cmd = gdt_mpr_copy_cmd;
    gdt->sc_get_status = gdt_mpr_get_status;
    gdt->sc_intr = gdt_mpr_intr;
    gdt->sc_release_event = gdt_mpr_release_event;
    gdt->sc_set_sema0 = gdt_mpr_set_sema0;
    gdt->sc_test_busy = gdt_mpr_test_busy;

    /* Allocate a dmatag representing the capabilities of this attachment */
    if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev),
                           /*alignemnt*/1, /*boundary*/0,
                           /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
                           /*highaddr*/BUS_SPACE_MAXADDR,
                           /*filter*/NULL, /*filterarg*/NULL,
                           /*maxsize*/BUS_SPACE_MAXSIZE_32BIT,
			   /*nsegments*/BUS_SPACE_UNRESTRICTED,
                           /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT,
			   /*flags*/0, /*lockfunc*/busdma_lock_mutex,
			   /*lockarg*/&gdt->sc_lock, &gdt->sc_parent_dmat) != 0) {
        error = ENXIO;
        goto err;
    }
    gdt->sc_init_level++;

    if (iir_init(gdt) != 0) {
        iir_free(gdt);
        error = ENXIO;
        goto err;
    }

    /* Register with the XPT */
    iir_attach(gdt);

    /* associate interrupt handler */
    if (bus_setup_intr(dev, irq, INTR_TYPE_CAM | INTR_MPSAFE, 
                        NULL, iir_intr, gdt, &ih )) {
        device_printf(dev, "Unable to register interrupt handler\n");
        error = ENXIO;
        goto err;
    }

    gdt_pci_enable_intr(gdt);
    return (0);
    
err:
    if (irq)
        bus_release_resource( dev, SYS_RES_IRQ, 0, irq );

    if (gdt->sc_dpmem)
        bus_release_resource( dev, SYS_RES_MEMORY, rid, gdt->sc_dpmem );
    mtx_destroy(&gdt->sc_lock);

    return (error);
}


/* Enable interrupts */
void
gdt_pci_enable_intr(struct gdt_softc *gdt)
{
    GDT_DPRINTF(GDT_D_INTR, ("gdt_pci_enable_intr(%p) ", gdt));

    switch(GDT_CLASS(gdt)) {
      case GDT_MPR:
        bus_write_1(gdt->sc_dpmem, GDT_MPR_EDOOR, 0xff);
        bus_write_1(gdt->sc_dpmem, GDT_EDOOR_EN,
	    bus_read_1(gdt->sc_dpmem, GDT_EDOOR_EN) & ~4);
        break;
    }
}


/*
 * MPR PCI controller-specific functions
 */

void
gdt_mpr_copy_cmd(struct gdt_softc *gdt, struct gdt_ccb *gccb)
{
    u_int16_t cp_count = roundup(gccb->gc_cmd_len, sizeof (u_int32_t));
    u_int16_t dp_offset = gdt->sc_cmd_off;
    u_int16_t cmd_no = gdt->sc_cmd_cnt++;

    GDT_DPRINTF(GDT_D_CMD, ("gdt_mpr_copy_cmd(%p) ", gdt));

    gdt->sc_cmd_off += cp_count;

    bus_write_region_4(gdt->sc_dpmem, GDT_MPR_IC + GDT_DPR_CMD + dp_offset, 
	(u_int32_t *)gccb->gc_cmd, cp_count >> 2);
    bus_write_2(gdt->sc_dpmem,
	GDT_MPR_IC + GDT_COMM_QUEUE + cmd_no * GDT_COMM_Q_SZ + GDT_OFFSET,
	htole16(GDT_DPMEM_COMMAND_OFFSET + dp_offset));
    bus_write_2(gdt->sc_dpmem,
	GDT_MPR_IC + GDT_COMM_QUEUE + cmd_no * GDT_COMM_Q_SZ + GDT_SERV_ID,
	htole16(gccb->gc_service));
}

u_int8_t
gdt_mpr_get_status(struct gdt_softc *gdt)
{
    GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_get_status(%p) ", gdt));
        
    return bus_read_1(gdt->sc_dpmem, GDT_MPR_EDOOR);
}

void
gdt_mpr_intr(struct gdt_softc *gdt, struct gdt_intr_ctx *ctx)
{
    int i;

    GDT_DPRINTF(GDT_D_INTR, ("gdt_mpr_intr(%p) ", gdt));

    bus_write_1(gdt->sc_dpmem, GDT_MPR_EDOOR, 0xff);

    if (ctx->istatus & 0x80) {          /* error flag */
        ctx->istatus &= ~0x80;
        ctx->cmd_status = bus_read_2(gdt->sc_dpmem, GDT_MPR_STATUS);
    } else                                      /* no error */
        ctx->cmd_status = GDT_S_OK;

    ctx->info = bus_read_4(gdt->sc_dpmem, GDT_MPR_INFO);
    ctx->service = bus_read_2(gdt->sc_dpmem, GDT_MPR_SERVICE);
    ctx->info2 = bus_read_4(gdt->sc_dpmem, GDT_MPR_INFO + sizeof (u_int32_t));

    /* event string */
    if (ctx->istatus == GDT_ASYNCINDEX) {
        if (ctx->service != GDT_SCREENSERVICE && 
            (gdt->sc_fw_vers & 0xff) >= 0x1a) {
            gdt->sc_dvr.severity = bus_read_1(gdt->sc_dpmem, GDT_SEVERITY);
            for (i = 0; i < 256; ++i) {
                gdt->sc_dvr.event_string[i] = bus_read_1(gdt->sc_dpmem,
		    GDT_EVT_BUF + i);
                if (gdt->sc_dvr.event_string[i] == 0)
                    break;
            }
        }
    }
    bus_write_1(gdt->sc_dpmem, GDT_MPR_SEMA1, 0);
}

void
gdt_mpr_release_event(struct gdt_softc *gdt)
{
    GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_release_event(%p) ", gdt));
    
    bus_write_1(gdt->sc_dpmem, GDT_MPR_LDOOR, 1);
}

void
gdt_mpr_set_sema0(struct gdt_softc *gdt)
{
    GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_set_sema0(%p) ", gdt));

    bus_write_1(gdt->sc_dpmem, GDT_MPR_SEMA0, 1);
}

int
gdt_mpr_test_busy(struct gdt_softc *gdt)
{
    GDT_DPRINTF(GDT_D_MISC, ("gdt_mpr_test_busy(%p) ", gdt));

    return (bus_read_1(gdt->sc_dpmem, GDT_MPR_SEMA0) & 1);
}
