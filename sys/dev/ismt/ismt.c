/*-
 * Copyright (C) 2014 Intel Corporation
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
 * 3. Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/priority.h>
#include <sys/proc.h>
#include <sys/syslog.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/smbus/smbconf.h>

#include "smbus_if.h"

#define ISMT_DESC_ENTRIES	32

/* Hardware Descriptor Constants - Control Field */
#define ISMT_DESC_CWRL	0x01	/* Command/Write Length */
#define ISMT_DESC_BLK	0X04	/* Perform Block Transaction */
#define ISMT_DESC_FAIR	0x08	/* Set fairness flag upon successful arbit. */
#define ISMT_DESC_PEC	0x10	/* Packet Error Code */
#define ISMT_DESC_I2C	0x20	/* I2C Enable */
#define ISMT_DESC_INT	0x40	/* Interrupt */
#define ISMT_DESC_SOE	0x80	/* Stop On Error */

/* Hardware Descriptor Constants - Status Field */
#define ISMT_DESC_SCS	0x01	/* Success */
#define ISMT_DESC_DLTO	0x04	/* Data Low Time Out */
#define ISMT_DESC_NAK	0x08	/* NAK Received */
#define ISMT_DESC_CRC	0x10	/* CRC Error */
#define ISMT_DESC_CLTO	0x20	/* Clock Low Time Out */
#define ISMT_DESC_COL	0x40	/* Collisions */
#define ISMT_DESC_LPR	0x80	/* Large Packet Received */

/* Macros */
#define ISMT_DESC_ADDR_RW(addr, is_read) ((addr << 1) | (is_read))

/* iSMT General Register address offsets (SMBBAR + <addr>) */
#define ISMT_GR_GCTRL		0x000	/* General Control */
#define ISMT_GR_SMTICL		0x008	/* SMT Interrupt Cause Location */
#define ISMT_GR_ERRINTMSK	0x010	/* Error Interrupt Mask */
#define ISMT_GR_ERRAERMSK	0x014	/* Error AER Mask */
#define ISMT_GR_ERRSTS		0x018	/* Error Status */
#define ISMT_GR_ERRINFO		0x01c	/* Error Information */

/* iSMT Master Registers */
#define ISMT_MSTR_MDBA		0x100	/* Master Descriptor Base Address */
#define ISMT_MSTR_MCTRL		0x108	/* Master Control */
#define ISMT_MSTR_MSTS		0x10c	/* Master Status */
#define ISMT_MSTR_MDS		0x110	/* Master Descriptor Size */
#define ISMT_MSTR_RPOLICY	0x114	/* Retry Policy */

/* iSMT Miscellaneous Registers */
#define ISMT_SPGT	0x300	/* SMBus PHY Global Timing */

/* General Control Register (GCTRL) bit definitions */
#define ISMT_GCTRL_TRST	0x04	/* Target Reset */
#define ISMT_GCTRL_KILL	0x08	/* Kill */
#define ISMT_GCTRL_SRST	0x40	/* Soft Reset */

/* Master Control Register (MCTRL) bit definitions */
#define ISMT_MCTRL_SS	0x01		/* Start/Stop */
#define ISMT_MCTRL_MEIE	0x10		/* Master Error Interrupt Enable */
#define ISMT_MCTRL_FMHP	0x00ff0000	/* Firmware Master Head Ptr (FMHP) */

/* Master Status Register (MSTS) bit definitions */
#define ISMT_MSTS_HMTP	0xff0000	/* HW Master Tail Pointer (HMTP) */
#define ISMT_MSTS_MIS	0x20		/* Master Interrupt Status (MIS) */
#define ISMT_MSTS_MEIS	0x10		/* Master Error Int Status (MEIS) */
#define ISMT_MSTS_IP	0x01		/* In Progress */

/* Master Descriptor Size (MDS) bit definitions */
#define ISMT_MDS_MASK	0xff	/* Master Descriptor Size mask (MDS) */

/* SMBus PHY Global Timing Register (SPGT) bit definitions */
#define ISMT_SPGT_SPD_MASK	0xc0000000	/* SMBus Speed mask */
#define ISMT_SPGT_SPD_80K	0x00		/* 80 kHz */
#define ISMT_SPGT_SPD_100K	(0x1 << 30)	/* 100 kHz */
#define ISMT_SPGT_SPD_400K	(0x2 << 30)	/* 400 kHz */
#define ISMT_SPGT_SPD_1M	(0x3 << 30)	/* 1 MHz */

/* MSI Control Register (MSICTL) bit definitions */
#define ISMT_MSICTL_MSIE	0x01	/* MSI Enable */

#define ISMT_MAX_BLOCK_SIZE	32 /* per SMBus spec */

//#define ISMT_DEBUG	device_printf
#ifndef ISMT_DEBUG
#define ISMT_DEBUG(...)
#endif

/* iSMT Hardware Descriptor */
struct ismt_desc {
	uint8_t tgtaddr_rw;	/* target address & r/w bit */
	uint8_t wr_len_cmd;	/* write length in bytes or a command */
	uint8_t rd_len;		/* read length */
	uint8_t control;	/* control bits */
	uint8_t status;		/* status bits */
	uint8_t retry;		/* collision retry and retry count */
	uint8_t rxbytes;	/* received bytes */
	uint8_t txbytes;	/* transmitted bytes */
	uint32_t dptr_low;	/* lower 32 bit of the data pointer */
	uint32_t dptr_high;	/* upper 32 bit of the data pointer */
} __packed;

#define DESC_SIZE	(ISMT_DESC_ENTRIES * sizeof(struct ismt_desc))

#define DMA_BUFFER_SIZE	64

struct ismt_softc {
	device_t		pcidev;
	device_t		smbdev;

	struct thread		*bus_reserved;

	int			intr_rid;
	struct resource		*intr_res;
	void			*intr_handle;

	bus_space_tag_t		mmio_tag;
	bus_space_handle_t	mmio_handle;
	int			mmio_rid;
	struct resource		*mmio_res;

	uint8_t			head;

	struct ismt_desc	*desc;
	bus_dma_tag_t		desc_dma_tag;
	bus_dmamap_t		desc_dma_map;
	uint64_t		desc_bus_addr;

	uint8_t			*dma_buffer;
	bus_dma_tag_t		dma_buffer_dma_tag;
	bus_dmamap_t		dma_buffer_dma_map;
	uint64_t		dma_buffer_bus_addr;

	uint8_t			using_msi;
};

static void
ismt_intr(void *arg)
{
	struct ismt_softc *sc = arg;
	uint32_t val;

	val = bus_read_4(sc->mmio_res, ISMT_MSTR_MSTS);
	ISMT_DEBUG(sc->pcidev, "%s MSTS=0x%x\n", __func__, val);

	val |= (ISMT_MSTS_MIS | ISMT_MSTS_MEIS);
	bus_write_4(sc->mmio_res, ISMT_MSTR_MSTS, val);

	wakeup(sc);
}

static int 
ismt_callback(device_t dev, int index, void *data)
{
	struct ismt_softc	*sc;
	int			acquired, err;

	sc = device_get_softc(dev);

	switch (index) {
	case SMB_REQUEST_BUS:
		acquired = atomic_cmpset_ptr(
		    (uintptr_t *)&sc->bus_reserved,
		    (uintptr_t)NULL, (uintptr_t)curthread);
		ISMT_DEBUG(dev, "SMB_REQUEST_BUS acquired=%d\n", acquired);
		if (acquired)
			err = 0;
		else
			err = EWOULDBLOCK;
		break;
	case SMB_RELEASE_BUS:
		KASSERT(sc->bus_reserved == curthread,
		    ("SMB_RELEASE_BUS called by wrong thread\n"));
		ISMT_DEBUG(dev, "SMB_RELEASE_BUS\n");
		atomic_store_rel_ptr((uintptr_t *)&sc->bus_reserved,
		    (uintptr_t)NULL);
		err = 0;
		break;
	default:
		err = SMB_EABORT;
		break;
	}

	return (err);
}

static struct ismt_desc *
ismt_alloc_desc(struct ismt_softc *sc)
{
	struct ismt_desc *desc;

	KASSERT(sc->bus_reserved == curthread,
	    ("curthread %p did not request bus (%p has reserved)\n",
	    curthread, sc->bus_reserved));

	desc = &sc->desc[sc->head++];
	if (sc->head == ISMT_DESC_ENTRIES)
		sc->head = 0;

	memset(desc, 0, sizeof(*desc));

	return (desc);
}

static int
ismt_submit(struct ismt_softc *sc, struct ismt_desc *desc, uint8_t slave,
    uint8_t is_read)
{
	uint32_t err, fmhp, val;

	desc->control |= ISMT_DESC_FAIR;
	if (sc->using_msi)
		desc->control |= ISMT_DESC_INT;

	desc->tgtaddr_rw = ISMT_DESC_ADDR_RW(slave, is_read);
	desc->dptr_low = (sc->dma_buffer_bus_addr & 0xFFFFFFFFLL);
	desc->dptr_high = (sc->dma_buffer_bus_addr >> 32);

	wmb();

	fmhp = sc->head << 16;
	val = bus_read_4(sc->mmio_res, ISMT_MSTR_MCTRL);
	val &= ~ISMT_MCTRL_FMHP;
	val |= fmhp;
	bus_write_4(sc->mmio_res, ISMT_MSTR_MCTRL, val);

	/* set the start bit */
	val = bus_read_4(sc->mmio_res, ISMT_MSTR_MCTRL);
	val |= ISMT_MCTRL_SS;
	bus_write_4(sc->mmio_res, ISMT_MSTR_MCTRL, val);

	err = tsleep(sc, PWAIT, "ismt_wait", 5 * hz);

	if (err != 0) {
		ISMT_DEBUG(sc->pcidev, "%s timeout\n", __func__);
		return (SMB_ETIMEOUT);
	}

	ISMT_DEBUG(sc->pcidev, "%s status=0x%x\n", __func__, desc->status);

	if (desc->status & ISMT_DESC_SCS)
		return (SMB_ENOERR);

	if (desc->status & ISMT_DESC_NAK)
		return (SMB_ENOACK);

	if (desc->status & ISMT_DESC_CRC)
		return (SMB_EBUSERR);

	if (desc->status & ISMT_DESC_COL)
		return (SMB_ECOLLI);

	if (desc->status & ISMT_DESC_LPR)
		return (SMB_EINVAL);

	if (desc->status & (ISMT_DESC_DLTO | ISMT_DESC_CLTO))
		return (SMB_ETIMEOUT);

	return (SMB_EBUSERR);
}


static int
ismt_quick(device_t dev, u_char slave, int how)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;
	int			is_read;

	ISMT_DEBUG(dev, "%s\n", __func__);

	if (how != SMB_QREAD && how != SMB_QWRITE) {
		return (SMB_ENOTSUPP);
	}

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	is_read = (how == SMB_QREAD ? 1 : 0);
	return (ismt_submit(sc, desc, slave, is_read));
}

static int
ismt_sendb(device_t dev, u_char slave, char byte)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->control = ISMT_DESC_CWRL;
	desc->wr_len_cmd = byte;

	return (ismt_submit(sc, desc, slave, 0));
}

static int
ismt_recvb(device_t dev, u_char slave, char *byte)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;
	int			err;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->rd_len = 1;

	err = ismt_submit(sc, desc, slave, 1);

	if (err != SMB_ENOERR)
		return (err);

	*byte = sc->dma_buffer[0];

	return (err);
}

static int
ismt_writeb(device_t dev, u_char slave, char cmd, char byte)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->wr_len_cmd = 2;
	sc->dma_buffer[0] = cmd;
	sc->dma_buffer[1] = byte;

	return (ismt_submit(sc, desc, slave, 0));
}

static int
ismt_writew(device_t dev, u_char slave, char cmd, short word)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->wr_len_cmd = 3;
	sc->dma_buffer[0] = cmd;
	sc->dma_buffer[1] = word & 0xFF;
	sc->dma_buffer[2] = word >> 8;

	return (ismt_submit(sc, desc, slave, 0));
}

static int
ismt_readb(device_t dev, u_char slave, char cmd, char *byte)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;
	int			err;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->control = ISMT_DESC_CWRL;
	desc->wr_len_cmd = cmd;
	desc->rd_len = 1;

	err = ismt_submit(sc, desc, slave, 1);

	if (err != SMB_ENOERR)
		return (err);

	*byte = sc->dma_buffer[0];

	return (err);
}

static int
ismt_readw(device_t dev, u_char slave, char cmd, short *word)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;
	int			err;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->control = ISMT_DESC_CWRL;
	desc->wr_len_cmd = cmd;
	desc->rd_len = 2;

	err = ismt_submit(sc, desc, slave, 1);

	if (err != SMB_ENOERR)
		return (err);

	*word = sc->dma_buffer[0] | (sc->dma_buffer[1] << 8);

	return (err);
}

static int
ismt_pcall(device_t dev, u_char slave, char cmd, short sdata, short *rdata)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;
	int			err;

	ISMT_DEBUG(dev, "%s\n", __func__);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->wr_len_cmd = 3;
	desc->rd_len = 2;
	sc->dma_buffer[0] = cmd;
	sc->dma_buffer[1] = sdata & 0xff;
	sc->dma_buffer[2] = sdata >> 8;

	err = ismt_submit(sc, desc, slave, 0);

	if (err != SMB_ENOERR)
		return (err);

	*rdata = sc->dma_buffer[0] | (sc->dma_buffer[1] << 8);

	return (err);
}

static int
ismt_bwrite(device_t dev, u_char slave, char cmd, u_char count, char *buf)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;

	ISMT_DEBUG(dev, "%s\n", __func__);

	if (count == 0 || count > ISMT_MAX_BLOCK_SIZE)
		return (SMB_EINVAL);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->control = ISMT_DESC_I2C;
	desc->wr_len_cmd = count + 1;
	sc->dma_buffer[0] = cmd;
	memcpy(&sc->dma_buffer[1], buf, count);

	return (ismt_submit(sc, desc, slave, 0));
}

static int
ismt_bread(device_t dev, u_char slave, char cmd, u_char *count, char *buf)
{
	struct ismt_desc	*desc;
	struct ismt_softc	*sc;
	int			err;

	ISMT_DEBUG(dev, "%s\n", __func__);

	if (*count == 0 || *count > ISMT_MAX_BLOCK_SIZE)
		return (SMB_EINVAL);

	sc = device_get_softc(dev);
	desc = ismt_alloc_desc(sc);
	desc->control = ISMT_DESC_I2C | ISMT_DESC_CWRL;
	desc->wr_len_cmd = cmd;
	desc->rd_len = *count;

	err = ismt_submit(sc, desc, slave, 0);

	if (err != SMB_ENOERR)
		return (err);

	memcpy(buf, sc->dma_buffer, desc->rxbytes);
	*count = desc->rxbytes;

	return (err);
}

static int
ismt_detach(device_t dev)
{
	struct ismt_softc	*sc;
	int			error;

	ISMT_DEBUG(dev, "%s\n", __func__);
	sc = device_get_softc(dev);

	error = bus_generic_detach(dev);
	if (error)
		return (error);

	device_delete_child(dev, sc->smbdev);

	if (sc->intr_handle != NULL) {
		bus_teardown_intr(dev, sc->intr_res, sc->intr_handle);
		sc->intr_handle = NULL;
	}
	if (sc->intr_res != NULL) {
		bus_release_resource(dev,
		    SYS_RES_IRQ, sc->intr_rid, sc->intr_res);
		sc->intr_res = NULL;
	}
	if (sc->using_msi == 1)
		pci_release_msi(dev);

	if (sc->mmio_res != NULL) {
		bus_release_resource(dev,
		    SYS_RES_MEMORY, sc->mmio_rid, sc->mmio_res);
		sc->mmio_res = NULL;
	}

	bus_dmamap_unload(sc->desc_dma_tag, sc->desc_dma_map);
	bus_dmamap_unload(sc->dma_buffer_dma_tag, sc->dma_buffer_dma_map);

	bus_dmamem_free(sc->desc_dma_tag, sc->desc,
	    sc->desc_dma_map);
	bus_dmamem_free(sc->dma_buffer_dma_tag, sc->dma_buffer,
	    sc->dma_buffer_dma_map);

	bus_dma_tag_destroy(sc->desc_dma_tag);
	bus_dma_tag_destroy(sc->dma_buffer_dma_tag);

	pci_disable_busmaster(dev);

	return 0;
}

static void
ismt_single_map(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	uint64_t *bus_addr = (uint64_t *)arg;

	KASSERT(error == 0, ("%s: error=%d\n", __func__, error));
	KASSERT(nseg == 1, ("%s: nseg=%d\n", __func__, nseg));

	*bus_addr = seg[0].ds_addr;
}

static int
ismt_attach(device_t dev)
{
	struct ismt_softc *sc = device_get_softc(dev);
	int err, num_vectors, val;

	sc->pcidev = dev;
	pci_enable_busmaster(dev);

	if ((sc->smbdev = device_add_child(dev, "smbus", -1)) == NULL) {
		device_printf(dev, "no smbus child found\n");
		err = ENXIO;
		goto fail;
	}

	sc->mmio_rid = PCIR_BAR(0);
	sc->mmio_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->mmio_rid, RF_ACTIVE);
	if (sc->mmio_res == NULL) {
		device_printf(dev, "cannot allocate mmio region\n");
		err = ENOMEM;
		goto fail;
	}

	sc->mmio_tag = rman_get_bustag(sc->mmio_res);
	sc->mmio_handle = rman_get_bushandle(sc->mmio_res);

	/* Attach "smbus" child */
	if ((err = bus_generic_attach(dev)) != 0) {
		device_printf(dev, "failed to attach child: %d\n", err);
		err = ENXIO;
		goto fail;
	}

	bus_dma_tag_create(bus_get_dma_tag(dev), 4, PAGE_SIZE,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    DESC_SIZE, 1, DESC_SIZE,
	    0, NULL, NULL, &sc->desc_dma_tag);

	bus_dma_tag_create(bus_get_dma_tag(dev), 4, PAGE_SIZE,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    DMA_BUFFER_SIZE, 1, DMA_BUFFER_SIZE,
	    0, NULL, NULL, &sc->dma_buffer_dma_tag);

	bus_dmamap_create(sc->desc_dma_tag, 0,
	    &sc->desc_dma_map);
	bus_dmamap_create(sc->dma_buffer_dma_tag, 0,
	    &sc->dma_buffer_dma_map);

	bus_dmamem_alloc(sc->desc_dma_tag,
	    (void **)&sc->desc, BUS_DMA_WAITOK,
	    &sc->desc_dma_map);
	bus_dmamem_alloc(sc->dma_buffer_dma_tag,
	    (void **)&sc->dma_buffer, BUS_DMA_WAITOK,
	    &sc->dma_buffer_dma_map);

	bus_dmamap_load(sc->desc_dma_tag,
	    sc->desc_dma_map, sc->desc, DESC_SIZE,
	    ismt_single_map, &sc->desc_bus_addr, 0);
	bus_dmamap_load(sc->dma_buffer_dma_tag,
	    sc->dma_buffer_dma_map, sc->dma_buffer, DMA_BUFFER_SIZE,
	    ismt_single_map, &sc->dma_buffer_bus_addr, 0);

	bus_write_4(sc->mmio_res, ISMT_MSTR_MDBA,
	    (sc->desc_bus_addr & 0xFFFFFFFFLL));
	bus_write_4(sc->mmio_res, ISMT_MSTR_MDBA + 4,
	    (sc->desc_bus_addr >> 32));

	/* initialize the Master Control Register (MCTRL) */
	bus_write_4(sc->mmio_res, ISMT_MSTR_MCTRL, ISMT_MCTRL_MEIE);

	/* initialize the Master Status Register (MSTS) */
	bus_write_4(sc->mmio_res, ISMT_MSTR_MSTS, 0);

	/* initialize the Master Descriptor Size (MDS) */
	val = bus_read_4(sc->mmio_res, ISMT_MSTR_MDS);
	val &= ~ISMT_MDS_MASK;
	val |= (ISMT_DESC_ENTRIES - 1);
	bus_write_4(sc->mmio_res, ISMT_MSTR_MDS, val);

	sc->using_msi = 1;

	if (pci_msi_count(dev) == 0) {
		sc->using_msi = 0;
		goto intx;
	}

	num_vectors = 1;
	if (pci_alloc_msi(dev, &num_vectors) != 0) {
		sc->using_msi = 0;
		goto intx;
	}

	sc->intr_rid = 1;
	sc->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->intr_rid, RF_ACTIVE);

	if (sc->intr_res == NULL) {
		sc->using_msi = 0;
		pci_release_msi(dev);
	}

intx:
	if (sc->using_msi == 0) {
		sc->intr_rid = 0;
		sc->intr_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
		    &sc->intr_rid, RF_SHAREABLE | RF_ACTIVE);
		if (sc->intr_res == NULL) {
			device_printf(dev, "cannot allocate irq\n");
			err = ENXIO;
			goto fail;
		}
	}

	ISMT_DEBUG(dev, "using_msi = %d\n", sc->using_msi);

	err = bus_setup_intr(dev, sc->intr_res,
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, ismt_intr, sc,
	    &sc->intr_handle);
	if (err != 0) {
		device_printf(dev, "cannot setup interrupt\n");
		err = ENXIO;
		goto fail;
	}

	return (0);

fail:
	ismt_detach(dev);
	return (err);
}

#define ID_INTEL_S1200_SMT0		0x0c598086
#define ID_INTEL_S1200_SMT1		0x0c5a8086
#define ID_INTEL_C2000_SMT		0x1f158086

static int
ismt_probe(device_t dev)
{
	const char *desc;

	switch (pci_get_devid(dev)) {
	case ID_INTEL_S1200_SMT0:
		desc = "Atom Processor S1200 SMBus 2.0 Controller 0";
		break;
	case ID_INTEL_S1200_SMT1:
		desc = "Atom Processor S1200 SMBus 2.0 Controller 1";
		break;
	case ID_INTEL_C2000_SMT:
		desc = "Atom Processor C2000 SMBus 2.0";
		break;
	default:
		return (ENXIO);
	}

	device_set_desc(dev, desc);
	return (BUS_PROBE_DEFAULT);
}

/* Device methods */
static device_method_t ismt_pci_methods[] = {
        DEVMETHOD(device_probe,		ismt_probe),
        DEVMETHOD(device_attach,	ismt_attach),
        DEVMETHOD(device_detach,	ismt_detach),

        DEVMETHOD(smbus_callback,	ismt_callback),
        DEVMETHOD(smbus_quick,		ismt_quick),
        DEVMETHOD(smbus_sendb,		ismt_sendb),
        DEVMETHOD(smbus_recvb,		ismt_recvb),
        DEVMETHOD(smbus_writeb,		ismt_writeb),
        DEVMETHOD(smbus_writew,		ismt_writew),
        DEVMETHOD(smbus_readb,		ismt_readb),
        DEVMETHOD(smbus_readw,		ismt_readw),
        DEVMETHOD(smbus_pcall,		ismt_pcall),
        DEVMETHOD(smbus_bwrite,		ismt_bwrite),
        DEVMETHOD(smbus_bread,		ismt_bread),

	DEVMETHOD_END
};

static driver_t ismt_pci_driver = {
	"ismt",
	ismt_pci_methods,
	sizeof(struct ismt_softc)
};

static devclass_t ismt_pci_devclass;

DRIVER_MODULE(ismt, pci, ismt_pci_driver, ismt_pci_devclass, 0, 0);
DRIVER_MODULE(smbus, ismt, smbus_driver, smbus_devclass, 0, 0);

MODULE_DEPEND(ismt, pci, 1, 1, 1);
MODULE_DEPEND(ismt, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(ismt, 1);
