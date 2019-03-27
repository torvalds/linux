/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999,2000 Jonathan Lemon
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
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <geom/geom_disk.h>

#include <dev/ida/idavar.h>
#include <dev/ida/idareg.h>

#define	IDA_PCI_MAX_DMA_ADDR	0xFFFFFFFF
#define	IDA_PCI_MAX_DMA_COUNT	0xFFFFFFFF

#define	IDA_PCI_MEMADDR		PCIR_BAR(1)		/* Mem I/O Address */

#define	IDA_DEVICEID_SMART		0xAE100E11
#define	IDA_DEVICEID_DEC_SMART		0x00461011
#define	IDA_DEVICEID_NCR_53C1510	0x00101000

static int
ida_v3_fifo_full(struct ida_softc *ida)
{
	return (ida_inl(ida, R_CMD_FIFO) == 0);
}

static void
ida_v3_submit(struct ida_softc *ida, struct ida_qcb *qcb)
{
	ida_outl(ida, R_CMD_FIFO, qcb->hwqcb_busaddr);
}

static bus_addr_t
ida_v3_done(struct ida_softc *ida)
{
	bus_addr_t completed;

	completed = ida_inl(ida, R_DONE_FIFO);
	if (completed == -1) {
		return (0);			/* fifo is empty */
	}
	return (completed);
}

static int
ida_v3_int_pending(struct ida_softc *ida)
{
	return (ida_inl(ida, R_INT_PENDING));
}

static void
ida_v3_int_enable(struct ida_softc *ida, int enable)
{
	if (enable)
		ida->flags |= IDA_INTERRUPTS;
	else
		ida->flags &= ~IDA_INTERRUPTS;
	ida_outl(ida, R_INT_MASK, enable ? INT_ENABLE : INT_DISABLE);
}

static int
ida_v4_fifo_full(struct ida_softc *ida)
{
	return (ida_inl(ida, R_42XX_REQUEST) != 0);
}

static void
ida_v4_submit(struct ida_softc *ida, struct ida_qcb *qcb)
{
	ida_outl(ida, R_42XX_REQUEST, qcb->hwqcb_busaddr);
}

static bus_addr_t
ida_v4_done(struct ida_softc *ida)
{
	bus_addr_t completed;

	completed = ida_inl(ida, R_42XX_REPLY);
	if (completed == -1)
		return (0);			/* fifo is empty */
	ida_outl(ida, R_42XX_REPLY, 0);		/* confirm read */
	return (completed);
}

static int
ida_v4_int_pending(struct ida_softc *ida)
{
	return (ida_inl(ida, R_42XX_STATUS) & STATUS_42XX_INT_PENDING);
}

static void
ida_v4_int_enable(struct ida_softc *ida, int enable)
{
	if (enable)
		ida->flags |= IDA_INTERRUPTS;
	else
		ida->flags &= ~IDA_INTERRUPTS;
	ida_outl(ida, R_42XX_INT_MASK,
	    enable ? INT_ENABLE_42XX : INT_DISABLE_42XX);
}

static struct ida_access ida_v3_access = {
	ida_v3_fifo_full,
	ida_v3_submit,
	ida_v3_done,
	ida_v3_int_pending,
	ida_v3_int_enable,
};

static struct ida_access ida_v4_access = {
	ida_v4_fifo_full,
	ida_v4_submit,
	ida_v4_done,
	ida_v4_int_pending,
	ida_v4_int_enable,
};

static struct ida_board board_id[] = {
	{ 0x40300E11, "Compaq SMART-2/P array controller",
	    &ida_v3_access, 0 },
	{ 0x40310E11, "Compaq SMART-2SL array controller",
	    &ida_v3_access, 0 },
	{ 0x40320E11, "Compaq Smart Array 3200 controller",
	    &ida_v3_access, 0 },
	{ 0x40330E11, "Compaq Smart Array 3100ES controller",
	    &ida_v3_access, 0 },
	{ 0x40340E11, "Compaq Smart Array 221 controller",
	    &ida_v3_access, 0 },

	{ 0x40400E11, "Compaq Integrated Array controller",
	    &ida_v4_access, IDA_FIRMWARE },
	{ 0x40480E11, "Compaq RAID LC2 controller",
	    &ida_v4_access, IDA_FIRMWARE },
	{ 0x40500E11, "Compaq Smart Array 4200 controller",
	    &ida_v4_access, 0 },
	{ 0x40510E11, "Compaq Smart Array 4250ES controller",
	    &ida_v4_access, 0 },
	{ 0x40580E11, "Compaq Smart Array 431 controller",
	    &ida_v4_access, 0 },

	{ 0, "", 0, 0 },
};

static int ida_pci_probe(device_t dev);
static int ida_pci_attach(device_t dev);

static device_method_t ida_pci_methods[] = {
	DEVMETHOD(device_probe,		ida_pci_probe),
	DEVMETHOD(device_attach,	ida_pci_attach),
	DEVMETHOD(device_detach,	ida_detach),

	DEVMETHOD_END
};

static driver_t ida_pci_driver = {
	"ida",
	ida_pci_methods,
	sizeof(struct ida_softc)
};

static devclass_t ida_devclass;

static struct ida_board *
ida_pci_match(device_t dev)
{
	int i;
	u_int32_t id, sub_id;

	id = pci_get_devid(dev);
	sub_id = pci_get_subdevice(dev) << 16 | pci_get_subvendor(dev);

	if (id == IDA_DEVICEID_SMART ||
	    id == IDA_DEVICEID_DEC_SMART ||
	    id == IDA_DEVICEID_NCR_53C1510) {
		for (i = 0; board_id[i].board; i++)
			if (board_id[i].board == sub_id)
				return (&board_id[i]);
	}
	return (NULL);
}

static int
ida_pci_probe(device_t dev)
{
	struct ida_board *board = ida_pci_match(dev);

	if (board != NULL) {
		device_set_desc(dev, board->desc);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
ida_pci_attach(device_t dev)
{
	struct ida_board *board = ida_pci_match(dev);
	u_int32_t id = pci_get_devid(dev);
	struct ida_softc *ida;
	int error, rid;

	ida = (struct ida_softc *)device_get_softc(dev);
	ida->dev = dev;
	ida->cmd = *board->accessor;
	ida->flags = board->flags;
	mtx_init(&ida->lock, "ida", NULL, MTX_DEF);
	callout_init_mtx(&ida->ch, &ida->lock, 0);

	ida->regs_res_type = SYS_RES_MEMORY;
	ida->regs_res_id = IDA_PCI_MEMADDR;
	if (id == IDA_DEVICEID_DEC_SMART)
		ida->regs_res_id = PCIR_BAR(0);

	ida->regs = bus_alloc_resource_any(dev, ida->regs_res_type,
	    &ida->regs_res_id, RF_ACTIVE);
	if (ida->regs == NULL) {
		device_printf(dev, "can't allocate memory resources\n");
		return (ENOMEM);
	}

	error = bus_dma_tag_create(
		/* parent	*/ bus_get_dma_tag(dev),
		/* alignment	*/ 1,
		/* boundary	*/ 0,
		/* lowaddr	*/ BUS_SPACE_MAXADDR_32BIT,
		/* highaddr	*/ BUS_SPACE_MAXADDR,
		/* filter	*/ NULL,
		/* filterarg	*/ NULL,
		/* maxsize	*/ BUS_SPACE_MAXSIZE_32BIT,
		/* nsegments	*/ BUS_SPACE_UNRESTRICTED,
		/* maxsegsize	*/ BUS_SPACE_MAXSIZE_32BIT,
		/* flags	*/ BUS_DMA_ALLOCNOW,
		/* lockfunc	*/ NULL,
		/* lockarg	*/ NULL,
		&ida->parent_dmat);
	if (error != 0) {
		device_printf(dev, "can't allocate DMA tag\n");
		ida_free(ida);
		return (ENOMEM);
	}

	rid = 0;
	ida->irq_res_type = SYS_RES_IRQ;
	ida->irq = bus_alloc_resource_any(dev, ida->irq_res_type, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (ida->irq == NULL) {
	        ida_free(ida);
	        return (ENOMEM);
	}
	error = bus_setup_intr(dev, ida->irq, INTR_TYPE_BIO | INTR_ENTROPY | INTR_MPSAFE,
	    NULL, ida_intr, ida, &ida->ih);
	if (error) {
		device_printf(dev, "can't setup interrupt\n");
		ida_free(ida);
		return (ENOMEM);
	}

	error = ida_setup(ida);
	if (error) {
	        ida_free(ida);
	        return (error);
	}

	return (0);
}

DRIVER_MODULE(ida, pci, ida_pci_driver, ida_devclass, 0, 0);
MODULE_PNP_INFO("W32:vendor/device;D:#", pci, ida, board_id,
    nitems(board_id) - 1);
