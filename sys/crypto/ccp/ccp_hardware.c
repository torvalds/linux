/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
 * All rights reserved.
 * Largely borrowed from ccr(4), Written by: John Baldwin <jhb@FreeBSD.org>
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

#include "opt_ddb.h"

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/vmparam.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "cryptodev_if.h"

#include "ccp.h"
#include "ccp_hardware.h"
#include "ccp_lsb.h"

CTASSERT(sizeof(struct ccp_desc) == 32);

static struct ccp_xts_unitsize_map_entry {
	enum ccp_xts_unitsize cxu_id;
	unsigned cxu_size;
} ccp_xts_unitsize_map[] = {
	{ CCP_XTS_AES_UNIT_SIZE_16, 16 },
	{ CCP_XTS_AES_UNIT_SIZE_512, 512 },
	{ CCP_XTS_AES_UNIT_SIZE_1024, 1024 },
	{ CCP_XTS_AES_UNIT_SIZE_2048, 2048 },
	{ CCP_XTS_AES_UNIT_SIZE_4096, 4096 },
};

SYSCTL_NODE(_hw, OID_AUTO, ccp, CTLFLAG_RD, 0, "ccp node");

unsigned g_ccp_ring_order = 11;
SYSCTL_UINT(_hw_ccp, OID_AUTO, ring_order, CTLFLAG_RDTUN, &g_ccp_ring_order,
    0, "Set CCP ring order.  (1 << this) == ring size.  Min: 6, Max: 16");

/*
 * Zero buffer, sufficient for padding LSB entries, that does not span a page
 * boundary
 */
static const char g_zeroes[32] __aligned(32);

static inline uint32_t
ccp_read_4(struct ccp_softc *sc, uint32_t offset)
{
	return (bus_space_read_4(sc->pci_bus_tag, sc->pci_bus_handle, offset));
}

static inline void
ccp_write_4(struct ccp_softc *sc, uint32_t offset, uint32_t value)
{
	bus_space_write_4(sc->pci_bus_tag, sc->pci_bus_handle, offset, value);
}

static inline uint32_t
ccp_read_queue_4(struct ccp_softc *sc, unsigned queue, uint32_t offset)
{
	/*
	 * Each queue gets its own 4kB register space.  Queue 0 is at 0x1000.
	 */
	return (ccp_read_4(sc, (CMD_Q_STATUS_INCR * (1 + queue)) + offset));
}

static inline void
ccp_write_queue_4(struct ccp_softc *sc, unsigned queue, uint32_t offset,
    uint32_t value)
{
	ccp_write_4(sc, (CMD_Q_STATUS_INCR * (1 + queue)) + offset, value);
}

void
ccp_queue_write_tail(struct ccp_queue *qp)
{
	ccp_write_queue_4(qp->cq_softc, qp->cq_qindex, CMD_Q_TAIL_LO_BASE,
	    ((uint32_t)qp->desc_ring_bus_addr) + (Q_DESC_SIZE * qp->cq_tail));
}

/*
 * Given a queue and a reserved LSB entry index, compute the LSB *entry id* of
 * that entry for the queue's private LSB region.
 */
static inline uint8_t
ccp_queue_lsb_entry(struct ccp_queue *qp, unsigned lsb_entry)
{
	return ((qp->private_lsb * LSB_REGION_LENGTH + lsb_entry));
}

/*
 * Given a queue and a reserved LSB entry index, compute the LSB *address* of
 * that entry for the queue's private LSB region.
 */
static inline uint32_t
ccp_queue_lsb_address(struct ccp_queue *qp, unsigned lsb_entry)
{
	return (ccp_queue_lsb_entry(qp, lsb_entry) * LSB_ENTRY_SIZE);
}

/*
 * Some terminology:
 *
 * LSB - Local Storage Block
 * =========================
 *
 * 8 segments/regions, each containing 16 entries.
 *
 * Each entry contains 256 bits (32 bytes).
 *
 * Segments are virtually addressed in commands, but accesses cannot cross
 * segment boundaries.  Virtual map uses an identity mapping by default
 * (virtual segment N corresponds to physical segment N).
 *
 * Access to a physical region can be restricted to any subset of all five
 * queues.
 *
 * "Pass-through" mode
 * ===================
 *
 * Pass-through is a generic DMA engine, much like ioat(4).  Some nice
 * features:
 *
 * - Supports byte-swapping for endian conversion (32- or 256-bit words)
 * - AND, OR, XOR with fixed 256-bit mask
 * - CRC32 of data (may be used in tandem with bswap, but not bit operations)
 * - Read/write of LSB
 * - Memset
 *
 * If bit manipulation mode is enabled, input must be a multiple of 256 bits
 * (32 bytes).
 *
 * If byte-swapping is enabled, input must be a multiple of the word size.
 *
 * Zlib mode -- only usable from one queue at a time, single job at a time.
 * ========================================================================
 *
 * Only usable from private host, aka PSP?  Not host processor?
 *
 * RNG.
 * ====
 *
 * Raw bits are conditioned with AES and fed through CTR_DRBG.  Output goes in
 * a ring buffer readable by software.
 *
 * NIST SP 800-90B Repetition Count and Adaptive Proportion health checks are
 * implemented on the raw input stream and may be enabled to verify min-entropy
 * of 0.5 bits per bit.
 */

static void
ccp_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *baddr;

	KASSERT(error == 0, ("%s: error:%d", __func__, error));
	baddr = arg;
	*baddr = segs->ds_addr;
}

static int
ccp_hw_attach_queue(device_t dev, uint64_t lsbmask, unsigned queue)
{
	struct ccp_softc *sc;
	struct ccp_queue *qp;
	void *desc;
	size_t ringsz, num_descriptors;
	int error;

	desc = NULL;
	sc = device_get_softc(dev);
	qp = &sc->queues[queue];

	/*
	 * Don't bother allocating a ring for queues the host isn't allowed to
	 * drive.
	 */
	if ((sc->valid_queues & (1 << queue)) == 0)
		return (0);

	ccp_queue_decode_lsb_regions(sc, lsbmask, queue);

	/* Ignore queues that do not have any LSB access. */
	if (qp->lsb_mask == 0) {
		device_printf(dev, "Ignoring queue %u with no LSB access\n",
		    queue);
		sc->valid_queues &= ~(1 << queue);
		return (0);
	}

	num_descriptors = 1 << sc->ring_size_order;
	ringsz = sizeof(struct ccp_desc) * num_descriptors;

	/*
	 * "Queue_Size" is order - 1.
	 *
	 * Queue must be aligned to 5+Queue_Size+1 == 5 + order bits.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(dev),
	    1 << (5 + sc->ring_size_order),
#if defined(__i386__) && !defined(PAE)
	    0, BUS_SPACE_MAXADDR,
#else
	    (bus_addr_t)1 << 32, BUS_SPACE_MAXADDR_48BIT,
#endif
	    BUS_SPACE_MAXADDR, NULL, NULL, ringsz, 1,
	    ringsz, 0, NULL, NULL, &qp->ring_desc_tag);
	if (error != 0)
		goto out;

	error = bus_dmamem_alloc(qp->ring_desc_tag, &desc,
	    BUS_DMA_ZERO | BUS_DMA_WAITOK, &qp->ring_desc_map);
	if (error != 0)
		goto out;

	error = bus_dmamap_load(qp->ring_desc_tag, qp->ring_desc_map, desc,
	    ringsz, ccp_dmamap_cb, &qp->desc_ring_bus_addr, BUS_DMA_WAITOK);
	if (error != 0)
		goto out;

	qp->desc_ring = desc;
	qp->completions_ring = malloc(num_descriptors *
	    sizeof(*qp->completions_ring), M_CCP, M_ZERO | M_WAITOK);

	/* Zero control register; among other things, clears the RUN flag. */
	qp->qcontrol = 0;
	ccp_write_queue_4(sc, queue, CMD_Q_CONTROL_BASE, qp->qcontrol);
	ccp_write_queue_4(sc, queue, CMD_Q_INT_ENABLE_BASE, 0);

	/* Clear any leftover interrupt status flags */
	ccp_write_queue_4(sc, queue, CMD_Q_INTERRUPT_STATUS_BASE,
	    ALL_INTERRUPTS);

	qp->qcontrol |= (sc->ring_size_order - 1) << CMD_Q_SIZE_SHIFT;

	ccp_write_queue_4(sc, queue, CMD_Q_TAIL_LO_BASE,
	    (uint32_t)qp->desc_ring_bus_addr);
	ccp_write_queue_4(sc, queue, CMD_Q_HEAD_LO_BASE,
	    (uint32_t)qp->desc_ring_bus_addr);

	/*
	 * Enable completion interrupts, as well as error or administrative
	 * halt interrupts.  We don't use administrative halts, but they
	 * shouldn't trip unless we do, so it ought to be harmless.
	 */
	ccp_write_queue_4(sc, queue, CMD_Q_INT_ENABLE_BASE,
	    INT_COMPLETION | INT_ERROR | INT_QUEUE_STOPPED);

	qp->qcontrol |= (qp->desc_ring_bus_addr >> 32) << CMD_Q_PTR_HI_SHIFT;
	qp->qcontrol |= CMD_Q_RUN;
	ccp_write_queue_4(sc, queue, CMD_Q_CONTROL_BASE, qp->qcontrol);

out:
	if (error != 0) {
		if (qp->desc_ring != NULL)
			bus_dmamap_unload(qp->ring_desc_tag,
			    qp->ring_desc_map);
		if (desc != NULL)
			bus_dmamem_free(qp->ring_desc_tag, desc,
			    qp->ring_desc_map);
		if (qp->ring_desc_tag != NULL)
			bus_dma_tag_destroy(qp->ring_desc_tag);
	}
	return (error);
}

static void
ccp_hw_detach_queue(device_t dev, unsigned queue)
{
	struct ccp_softc *sc;
	struct ccp_queue *qp;

	sc = device_get_softc(dev);
	qp = &sc->queues[queue];

	/*
	 * Don't bother allocating a ring for queues the host isn't allowed to
	 * drive.
	 */
	if ((sc->valid_queues & (1 << queue)) == 0)
		return;

	free(qp->completions_ring, M_CCP);
	bus_dmamap_unload(qp->ring_desc_tag, qp->ring_desc_map);
	bus_dmamem_free(qp->ring_desc_tag, qp->desc_ring, qp->ring_desc_map);
	bus_dma_tag_destroy(qp->ring_desc_tag);
}

static int
ccp_map_pci_bar(device_t dev)
{
	struct ccp_softc *sc;

	sc = device_get_softc(dev);

	sc->pci_resource_id = PCIR_BAR(2);
	sc->pci_resource = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->pci_resource_id, RF_ACTIVE);
	if (sc->pci_resource == NULL) {
		device_printf(dev, "unable to allocate pci resource\n");
		return (ENODEV);
	}

	sc->pci_resource_id_msix = PCIR_BAR(5);
	sc->pci_resource_msix = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->pci_resource_id_msix, RF_ACTIVE);
	if (sc->pci_resource_msix == NULL) {
		device_printf(dev, "unable to allocate pci resource msix\n");
		bus_release_resource(dev, SYS_RES_MEMORY, sc->pci_resource_id,
		    sc->pci_resource);
		return (ENODEV);
	}

	sc->pci_bus_tag = rman_get_bustag(sc->pci_resource);
	sc->pci_bus_handle = rman_get_bushandle(sc->pci_resource);
	return (0);
}

static void
ccp_unmap_pci_bar(device_t dev)
{
	struct ccp_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resource(dev, SYS_RES_MEMORY, sc->pci_resource_id_msix,
	    sc->pci_resource_msix);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->pci_resource_id,
	    sc->pci_resource);
}

const static struct ccp_error_code {
	uint8_t		ce_code;
	const char	*ce_name;
	int		ce_errno;
	const char	*ce_desc;
} ccp_error_codes[] = {
	{ 0x01, "ILLEGAL_ENGINE", EIO, "Requested engine was invalid" },
	{ 0x03, "ILLEGAL_FUNCTION_TYPE", EIO,
	    "A non-supported function type was specified" },
	{ 0x04, "ILLEGAL_FUNCTION_MODE", EIO,
	    "A non-supported function mode was specified" },
	{ 0x05, "ILLEGAL_FUNCTION_ENCRYPT", EIO,
	    "A CMAC type was specified when ENCRYPT was not specified" },
	{ 0x06, "ILLEGAL_FUNCTION_SIZE", EIO,
	    "A non-supported function size was specified.\n"
	    "AES-CFB: Size was not 127 or 7;\n"
	    "3DES-CFB: Size was not 7;\n"
	    "RSA: See supported size table (7.4.2);\n"
	    "ECC: Size was greater than 576 bits." },
	{ 0x07, "Zlib_MISSING_INIT_EOM", EIO,
	    "Zlib command does not have INIT and EOM set" },
	{ 0x08, "ILLEGAL_FUNCTION_RSVD", EIO,
	    "Reserved bits in a function specification were not 0" },
	{ 0x09, "ILLEGAL_BUFFER_LENGTH", EIO,
	    "The buffer length specified was not correct for the selected engine"
	},
	{ 0x0A, "VLSB_FAULT", EIO, "Illegal VLSB segment mapping:\n"
	    "Undefined VLSB segment mapping or\n"
	    "mapping to unsupported LSB segment id" },
	{ 0x0B, "ILLEGAL_MEM_ADDR", EFAULT,
	    "The specified source/destination buffer access was illegal:\n"
	    "Data buffer located in a LSB location disallowed by the LSB protection masks; or\n"
	    "Data buffer not completely contained within a single segment; or\n"
	    "Pointer with Fixed=1 is not 32-bit aligned; or\n"
	    "Pointer with Fixed=1 attempted to reference non-AXI1 (local) memory."
	},
	{ 0x0C, "ILLEGAL_MEM_SEL", EIO,
	    "A src_mem, dst_mem, or key_mem field was illegal:\n"
	    "A field was set to a reserved value; or\n"
	    "A public command attempted to reference AXI1 (local) or GART memory; or\n"
	    "A Zlib command attmpted to use the LSB." },
	{ 0x0D, "ILLEGAL_CONTEXT_ADDR", EIO,
	    "The specified context location was illegal:\n"
	    "Context located in a LSB location disallowed by the LSB protection masks; or\n"
	    "Context not completely contained within a single segment." },
	{ 0x0E, "ILLEGAL_KEY_ADDR", EIO,
	    "The specified key location was illegal:\n"
	    "Key located in a LSB location disallowed by the LSB protection masks; or\n"
	    "Key not completely contained within a single segment." },
	{ 0x12, "CMD_TIMEOUT", EIO, "A command timeout violation occurred" },
	/* XXX Could fill out these descriptions too */
	{ 0x13, "IDMA0_AXI_SLVERR", EIO, "" },
	{ 0x14, "IDMA0_AXI_DECERR", EIO, "" },
	{ 0x16, "IDMA1_AXI_SLVERR", EIO, "" },
	{ 0x17, "IDMA1_AXI_DECERR", EIO, "" },
	{ 0x19, "ZLIBVHB_AXI_SLVERR", EIO, "" },
	{ 0x1A, "ZLIBVHB_AXI_DECERR", EIO, "" },
	{ 0x1C, "ZLIB_UNEXPECTED_EOM", EIO, "" },
	{ 0x1D, "ZLIB_EXTRA_DATA", EIO, "" },
	{ 0x1E, "ZLIB_BTYPE", EIO, "" },
	{ 0x20, "ZLIB_UNDEFINED_DISTANCE_SYMBOL", EIO, "" },
	{ 0x21, "ZLIB_CODE_LENGTH_SYMBOL", EIO, "" },
	{ 0x22, "ZLIB_VHB_ILLEGAL_FETCH", EIO, "" },
	{ 0x23, "ZLIB_UNCOMPRESSED_LEN", EIO, "" },
	{ 0x24, "ZLIB_LIMIT_REACHED", EIO, "" },
	{ 0x25, "ZLIB_CHECKSUM_MISMATCH", EIO, "" },
	{ 0x26, "ODMA0_AXI_SLVERR", EIO, "" },
	{ 0x27, "ODMA0_AXI_DECERR", EIO, "" },
	{ 0x29, "ODMA1_AXI_SLVERR", EIO, "" },
	{ 0x2A, "ODMA1_AXI_DECERR", EIO, "" },
	{ 0x2B, "LSB_PARITY_ERR", EIO,
	    "A read from the LSB encountered a parity error" },
};

static void
ccp_intr_handle_error(struct ccp_queue *qp, const struct ccp_desc *desc)
{
	struct ccp_completion_ctx *cctx;
	const struct ccp_error_code *ec;
	struct ccp_softc *sc;
	uint32_t status, error, esource, faultblock;
	unsigned q, idx;
	int errno;

	sc = qp->cq_softc;
	q = qp->cq_qindex;

	status = ccp_read_queue_4(sc, q, CMD_Q_STATUS_BASE);

	error = status & STATUS_ERROR_MASK;

	/* Decode error status */
	ec = NULL;
	for (idx = 0; idx < nitems(ccp_error_codes); idx++)
		if (ccp_error_codes[idx].ce_code == error) {
			ec = &ccp_error_codes[idx];
			break;
		}

	esource = (status >> STATUS_ERRORSOURCE_SHIFT) &
	    STATUS_ERRORSOURCE_MASK;
	faultblock = (status >> STATUS_VLSB_FAULTBLOCK_SHIFT) &
	    STATUS_VLSB_FAULTBLOCK_MASK;
	device_printf(sc->dev, "Error: %s (%u) Source: %u Faulting LSB block: %u\n",
	    (ec != NULL) ? ec->ce_name : "(reserved)", error, esource,
	    faultblock);
	if (ec != NULL)
		device_printf(sc->dev, "Error description: %s\n", ec->ce_desc);

	/* TODO Could format the desc nicely here */
	idx = desc - qp->desc_ring;
	DPRINTF(sc->dev, "Bad descriptor index: %u contents: %32D\n", idx,
	    (const void *)desc, " ");

	/*
	 * TODO Per § 14.4 "Error Handling," DMA_Status, DMA_Read/Write_Status,
	 * Zlib Decompress status may be interesting.
	 */

	while (true) {
		/* Keep unused descriptors zero for next use. */
		memset(&qp->desc_ring[idx], 0, sizeof(qp->desc_ring[idx]));

		cctx = &qp->completions_ring[idx];

		/*
		 * Restart procedure described in § 14.2.5.  Could be used by HoC if we
		 * used that.
		 *
		 * Advance HEAD_LO past bad descriptor + any remaining in
		 * transaction manually, then restart queue.
		 */
		idx = (idx + 1) % (1 << sc->ring_size_order);

		/* Callback function signals end of transaction */
		if (cctx->callback_fn != NULL) {
			if (ec == NULL)
				errno = EIO;
			else
				errno = ec->ce_errno;
			/* TODO More specific error code */
			cctx->callback_fn(qp, cctx->session, cctx->callback_arg, errno);
			cctx->callback_fn = NULL;
			break;
		}
	}

	qp->cq_head = idx;
	qp->cq_waiting = false;
	wakeup(&qp->cq_tail);
	DPRINTF(sc->dev, "%s: wrote sw head:%u\n", __func__, qp->cq_head);
	ccp_write_queue_4(sc, q, CMD_Q_HEAD_LO_BASE,
	    (uint32_t)qp->desc_ring_bus_addr + (idx * Q_DESC_SIZE));
	ccp_write_queue_4(sc, q, CMD_Q_CONTROL_BASE, qp->qcontrol);
	DPRINTF(sc->dev, "%s: Restarted queue\n", __func__);
}

static void
ccp_intr_run_completions(struct ccp_queue *qp, uint32_t ints)
{
	struct ccp_completion_ctx *cctx;
	struct ccp_softc *sc;
	const struct ccp_desc *desc;
	uint32_t headlo, idx;
	unsigned q, completed;

	sc = qp->cq_softc;
	q = qp->cq_qindex;

	mtx_lock(&qp->cq_lock);

	/*
	 * Hardware HEAD_LO points to the first incomplete descriptor.  Process
	 * any submitted and completed descriptors, up to but not including
	 * HEAD_LO.
	 */
	headlo = ccp_read_queue_4(sc, q, CMD_Q_HEAD_LO_BASE);
	idx = (headlo - (uint32_t)qp->desc_ring_bus_addr) / Q_DESC_SIZE;

	DPRINTF(sc->dev, "%s: hw head:%u sw head:%u\n", __func__, idx,
	    qp->cq_head);
	completed = 0;
	while (qp->cq_head != idx) {
		DPRINTF(sc->dev, "%s: completing:%u\n", __func__, qp->cq_head);

		cctx = &qp->completions_ring[qp->cq_head];
		if (cctx->callback_fn != NULL) {
			cctx->callback_fn(qp, cctx->session,
			    cctx->callback_arg, 0);
			cctx->callback_fn = NULL;
		}

		/* Keep unused descriptors zero for next use. */
		memset(&qp->desc_ring[qp->cq_head], 0,
		    sizeof(qp->desc_ring[qp->cq_head]));

		qp->cq_head = (qp->cq_head + 1) % (1 << sc->ring_size_order);
		completed++;
	}
	if (completed > 0) {
		qp->cq_waiting = false;
		wakeup(&qp->cq_tail);
	}

	DPRINTF(sc->dev, "%s: wrote sw head:%u\n", __func__, qp->cq_head);

	/*
	 * Desc points to the first incomplete descriptor, at the time we read
	 * HEAD_LO.  If there was an error flagged in interrupt status, the HW
	 * will not proceed past the erroneous descriptor by itself.
	 */
	desc = &qp->desc_ring[idx];
	if ((ints & INT_ERROR) != 0)
		ccp_intr_handle_error(qp, desc);

	mtx_unlock(&qp->cq_lock);
}

static void
ccp_intr_handler(void *arg)
{
	struct ccp_softc *sc = arg;
	size_t i;
	uint32_t ints;

	DPRINTF(sc->dev, "%s: interrupt\n", __func__);

	/*
	 * We get one global interrupt per PCI device, shared over all of
	 * its queues.  Scan each valid queue on interrupt for flags indicating
	 * activity.
	 */
	for (i = 0; i < nitems(sc->queues); i++) {
		if ((sc->valid_queues & (1 << i)) == 0)
			continue;

		ints = ccp_read_queue_4(sc, i, CMD_Q_INTERRUPT_STATUS_BASE);
		if (ints == 0)
			continue;

#if 0
		DPRINTF(sc->dev, "%s: %x interrupts on queue %zu\n", __func__,
		    (unsigned)ints, i);
#endif
		/* Write back 1s to clear interrupt status bits. */
		ccp_write_queue_4(sc, i, CMD_Q_INTERRUPT_STATUS_BASE, ints);

		/*
		 * If there was an error, we still need to run completions on
		 * any descriptors prior to the error.  The completions handler
		 * invoked below will also handle the error descriptor.
		 */
		if ((ints & (INT_COMPLETION | INT_ERROR)) != 0)
			ccp_intr_run_completions(&sc->queues[i], ints);

		if ((ints & INT_QUEUE_STOPPED) != 0)
			device_printf(sc->dev, "%s: queue %zu stopped\n",
			    __func__, i);
	}

	/* Re-enable interrupts after processing */
	for (i = 0; i < nitems(sc->queues); i++) {
		if ((sc->valid_queues & (1 << i)) == 0)
			continue;
		ccp_write_queue_4(sc, i, CMD_Q_INT_ENABLE_BASE,
		    INT_COMPLETION | INT_ERROR | INT_QUEUE_STOPPED);
	}
}

static int
ccp_intr_filter(void *arg)
{
	struct ccp_softc *sc = arg;
	size_t i;

	/* TODO: Split individual queues into separate taskqueues? */
	for (i = 0; i < nitems(sc->queues); i++) {
		if ((sc->valid_queues & (1 << i)) == 0)
			continue;

		/* Mask interrupt until task completes */
		ccp_write_queue_4(sc, i, CMD_Q_INT_ENABLE_BASE, 0);
	}

	return (FILTER_SCHEDULE_THREAD);
}

static int
ccp_setup_interrupts(struct ccp_softc *sc)
{
	uint32_t nvec;
	int rid, error, n, ridcopy;

	n = pci_msix_count(sc->dev);
	if (n < 1) {
		device_printf(sc->dev, "%s: msix_count: %d\n", __func__, n);
		return (ENXIO);
	}

	nvec = n;
	error = pci_alloc_msix(sc->dev, &nvec);
	if (error != 0) {
		device_printf(sc->dev, "%s: alloc_msix error: %d\n", __func__,
		    error);
		return (error);
	}
	if (nvec < 1) {
		device_printf(sc->dev, "%s: alloc_msix: 0 vectors\n",
		    __func__);
		return (ENXIO);
	}
	if (nvec > nitems(sc->intr_res)) {
		device_printf(sc->dev, "%s: too many vectors: %u\n", __func__,
		    nvec);
		nvec = nitems(sc->intr_res);
	}

	for (rid = 1; rid < 1 + nvec; rid++) {
		ridcopy = rid;
		sc->intr_res[rid - 1] = bus_alloc_resource_any(sc->dev,
		    SYS_RES_IRQ, &ridcopy, RF_ACTIVE);
		if (sc->intr_res[rid - 1] == NULL) {
			device_printf(sc->dev, "%s: Failed to alloc IRQ resource\n",
			    __func__);
			return (ENXIO);
		}

		sc->intr_tag[rid - 1] = NULL;
		error = bus_setup_intr(sc->dev, sc->intr_res[rid - 1],
		    INTR_MPSAFE | INTR_TYPE_MISC, ccp_intr_filter,
		    ccp_intr_handler, sc, &sc->intr_tag[rid - 1]);
		if (error != 0)
			device_printf(sc->dev, "%s: setup_intr: %d\n",
			    __func__, error);
	}
	sc->intr_count = nvec;

	return (error);
}

static void
ccp_release_interrupts(struct ccp_softc *sc)
{
	unsigned i;

	for (i = 0; i < sc->intr_count; i++) {
		if (sc->intr_tag[i] != NULL)
			bus_teardown_intr(sc->dev, sc->intr_res[i],
			    sc->intr_tag[i]);
		if (sc->intr_res[i] != NULL)
			bus_release_resource(sc->dev, SYS_RES_IRQ,
			    rman_get_rid(sc->intr_res[i]), sc->intr_res[i]);
	}

	pci_release_msi(sc->dev);
}

int
ccp_hw_attach(device_t dev)
{
	struct ccp_softc *sc;
	uint64_t lsbmask;
	uint32_t version, lsbmasklo, lsbmaskhi;
	unsigned queue_idx, j;
	int error;
	bool bars_mapped, interrupts_setup;

	queue_idx = 0;
	bars_mapped = interrupts_setup = false;
	sc = device_get_softc(dev);

	error = ccp_map_pci_bar(dev);
	if (error != 0) {
		device_printf(dev, "%s: couldn't map BAR(s)\n", __func__);
		goto out;
	}
	bars_mapped = true;

	error = pci_enable_busmaster(dev);
	if (error != 0) {
		device_printf(dev, "%s: couldn't enable busmaster\n",
		    __func__);
		goto out;
	}

	sc->ring_size_order = g_ccp_ring_order;
	if (sc->ring_size_order < 6 || sc->ring_size_order > 16) {
		device_printf(dev, "bogus hw.ccp.ring_order\n");
		error = EINVAL;
		goto out;
	}
	sc->valid_queues = ccp_read_4(sc, CMD_QUEUE_MASK_OFFSET);

	version = ccp_read_4(sc, VERSION_REG);
	if ((version & VERSION_NUM_MASK) < 5) {
		device_printf(dev,
		    "driver supports version 5 and later hardware\n");
		error = ENXIO;
		goto out;
	}

	error = ccp_setup_interrupts(sc);
	if (error != 0)
		goto out;
	interrupts_setup = true;

	sc->hw_version = version & VERSION_NUM_MASK;
	sc->num_queues = (version >> VERSION_NUMVQM_SHIFT) &
	    VERSION_NUMVQM_MASK;
	sc->num_lsb_entries = (version >> VERSION_LSBSIZE_SHIFT) &
	    VERSION_LSBSIZE_MASK;
	sc->hw_features = version & VERSION_CAP_MASK;

	/*
	 * Copy private LSB mask to public registers to enable access to LSB
	 * from all queues allowed by BIOS.
	 */
	lsbmasklo = ccp_read_4(sc, LSB_PRIVATE_MASK_LO_OFFSET);
	lsbmaskhi = ccp_read_4(sc, LSB_PRIVATE_MASK_HI_OFFSET);
	ccp_write_4(sc, LSB_PUBLIC_MASK_LO_OFFSET, lsbmasklo);
	ccp_write_4(sc, LSB_PUBLIC_MASK_HI_OFFSET, lsbmaskhi);

	lsbmask = ((uint64_t)lsbmaskhi << 30) | lsbmasklo;

	for (; queue_idx < nitems(sc->queues); queue_idx++) {
		error = ccp_hw_attach_queue(dev, lsbmask, queue_idx);
		if (error != 0) {
			device_printf(dev, "%s: couldn't attach queue %u\n",
			    __func__, queue_idx);
			goto out;
		}
	}
	ccp_assign_lsb_regions(sc, lsbmask);

out:
	if (error != 0) {
		if (interrupts_setup)
			ccp_release_interrupts(sc);
		for (j = 0; j < queue_idx; j++)
			ccp_hw_detach_queue(dev, j);
		if (sc->ring_size_order != 0)
			pci_disable_busmaster(dev);
		if (bars_mapped)
			ccp_unmap_pci_bar(dev);
	}
	return (error);
}

void
ccp_hw_detach(device_t dev)
{
	struct ccp_softc *sc;
	unsigned i;

	sc = device_get_softc(dev);

	for (i = 0; i < nitems(sc->queues); i++)
		ccp_hw_detach_queue(dev, i);

	ccp_release_interrupts(sc);
	pci_disable_busmaster(dev);
	ccp_unmap_pci_bar(dev);
}

static int __must_check
ccp_passthrough(struct ccp_queue *qp, bus_addr_t dst,
    enum ccp_memtype dst_type, bus_addr_t src, enum ccp_memtype src_type,
    bus_size_t len, enum ccp_passthru_byteswap swapmode,
    enum ccp_passthru_bitwise bitmode, bool interrupt,
    const struct ccp_completion_ctx *cctx)
{
	struct ccp_desc *desc;

	if (ccp_queue_get_ring_space(qp) == 0)
		return (EAGAIN);

	desc = &qp->desc_ring[qp->cq_tail];

	memset(desc, 0, sizeof(*desc));
	desc->engine = CCP_ENGINE_PASSTHRU;

	desc->pt.ioc = interrupt;
	desc->pt.byteswap = swapmode;
	desc->pt.bitwise = bitmode;
	desc->length = len;

	desc->src_lo = (uint32_t)src;
	desc->src_hi = src >> 32;
	desc->src_mem = src_type;

	desc->dst_lo = (uint32_t)dst;
	desc->dst_hi = dst >> 32;
	desc->dst_mem = dst_type;

	if (bitmode != CCP_PASSTHRU_BITWISE_NOOP)
		desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_KEY);

	if (cctx != NULL)
		memcpy(&qp->completions_ring[qp->cq_tail], cctx, sizeof(*cctx));

	qp->cq_tail = (qp->cq_tail + 1) % (1 << qp->cq_softc->ring_size_order);
	return (0);
}

static int __must_check
ccp_passthrough_sgl(struct ccp_queue *qp, bus_addr_t lsb_addr, bool tolsb,
    struct sglist *sgl, bus_size_t len, bool interrupt,
    const struct ccp_completion_ctx *cctx)
{
	struct sglist_seg *seg;
	size_t i, remain, nb;
	int error;

	remain = len;
	for (i = 0; i < sgl->sg_nseg && remain != 0; i++) {
		seg = &sgl->sg_segs[i];
		/* crd_len is int, so 32-bit min() is ok. */
		nb = min(remain, seg->ss_len);

		if (tolsb)
			error = ccp_passthrough(qp, lsb_addr, CCP_MEMTYPE_SB,
			    seg->ss_paddr, CCP_MEMTYPE_SYSTEM, nb,
			    CCP_PASSTHRU_BYTESWAP_NOOP,
			    CCP_PASSTHRU_BITWISE_NOOP,
			    (nb == remain) && interrupt, cctx);
		else
			error = ccp_passthrough(qp, seg->ss_paddr,
			    CCP_MEMTYPE_SYSTEM, lsb_addr, CCP_MEMTYPE_SB, nb,
			    CCP_PASSTHRU_BYTESWAP_NOOP,
			    CCP_PASSTHRU_BITWISE_NOOP,
			    (nb == remain) && interrupt, cctx);
		if (error != 0)
			return (error);

		remain -= nb;
	}
	return (0);
}

/*
 * Note that these vectors are in reverse of the usual order.
 */
const struct SHA_vectors {
	uint32_t SHA1[8];
	uint32_t SHA224[8];
	uint32_t SHA256[8];
	uint64_t SHA384[8];
	uint64_t SHA512[8];
} SHA_H __aligned(PAGE_SIZE) = {
	.SHA1 = {
		0xc3d2e1f0ul,
		0x10325476ul,
		0x98badcfeul,
		0xefcdab89ul,
		0x67452301ul,
		0,
		0,
		0,
	},
	.SHA224 = {
		0xbefa4fa4ul,
		0x64f98fa7ul,
		0x68581511ul,
		0xffc00b31ul,
		0xf70e5939ul,
		0x3070dd17ul,
		0x367cd507ul,
		0xc1059ed8ul,
	},
	.SHA256 = {
		0x5be0cd19ul,
		0x1f83d9abul,
		0x9b05688cul,
		0x510e527ful,
		0xa54ff53aul,
		0x3c6ef372ul,
		0xbb67ae85ul,
		0x6a09e667ul,
	},
	.SHA384 = {
		0x47b5481dbefa4fa4ull,
		0xdb0c2e0d64f98fa7ull,
		0x8eb44a8768581511ull,
		0x67332667ffc00b31ull,
		0x152fecd8f70e5939ull,
		0x9159015a3070dd17ull,
		0x629a292a367cd507ull,
		0xcbbb9d5dc1059ed8ull,
	},
	.SHA512 = {
		0x5be0cd19137e2179ull,
		0x1f83d9abfb41bd6bull,
		0x9b05688c2b3e6c1full,
		0x510e527fade682d1ull,
		0xa54ff53a5f1d36f1ull,
		0x3c6ef372fe94f82bull,
		0xbb67ae8584caa73bull,
		0x6a09e667f3bcc908ull,
	},
};
/*
 * Ensure vectors do not cross a page boundary.
 *
 * Disabled due to a new Clang error:  "expression is not an integral constant
 * expression."  GCC (cross toolchain) seems to handle this assertion with
 * _Static_assert just fine.
 */
#if 0
CTASSERT(PAGE_SIZE - ((uintptr_t)&SHA_H % PAGE_SIZE) >= sizeof(SHA_H));
#endif

const struct SHA_Defn {
	enum sha_version version;
	const void *H_vectors;
	size_t H_size;
	struct auth_hash *axf;
	enum ccp_sha_type engine_type;
} SHA_definitions[] = {
	{
		.version = SHA1,
		.H_vectors = SHA_H.SHA1,
		.H_size = sizeof(SHA_H.SHA1),
		.axf = &auth_hash_hmac_sha1,
		.engine_type = CCP_SHA_TYPE_1,
	},
#if 0
	{
		.version = SHA2_224,
		.H_vectors = SHA_H.SHA224,
		.H_size = sizeof(SHA_H.SHA224),
		.axf = &auth_hash_hmac_sha2_224,
		.engine_type = CCP_SHA_TYPE_224,
	},
#endif
	{
		.version = SHA2_256,
		.H_vectors = SHA_H.SHA256,
		.H_size = sizeof(SHA_H.SHA256),
		.axf = &auth_hash_hmac_sha2_256,
		.engine_type = CCP_SHA_TYPE_256,
	},
	{
		.version = SHA2_384,
		.H_vectors = SHA_H.SHA384,
		.H_size = sizeof(SHA_H.SHA384),
		.axf = &auth_hash_hmac_sha2_384,
		.engine_type = CCP_SHA_TYPE_384,
	},
	{
		.version = SHA2_512,
		.H_vectors = SHA_H.SHA512,
		.H_size = sizeof(SHA_H.SHA512),
		.axf = &auth_hash_hmac_sha2_512,
		.engine_type = CCP_SHA_TYPE_512,
	},
};

static int __must_check
ccp_sha_single_desc(struct ccp_queue *qp, const struct SHA_Defn *defn,
    vm_paddr_t addr, size_t len, bool start, bool end, uint64_t msgbits)
{
	struct ccp_desc *desc;

	if (ccp_queue_get_ring_space(qp) == 0)
		return (EAGAIN);

	desc = &qp->desc_ring[qp->cq_tail];

	memset(desc, 0, sizeof(*desc));
	desc->engine = CCP_ENGINE_SHA;
	desc->som = start;
	desc->eom = end;

	desc->sha.type = defn->engine_type;
	desc->length = len;

	if (end) {
		desc->sha_len_lo = (uint32_t)msgbits;
		desc->sha_len_hi = msgbits >> 32;
	}

	desc->src_lo = (uint32_t)addr;
	desc->src_hi = addr >> 32;
	desc->src_mem = CCP_MEMTYPE_SYSTEM;

	desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_SHA);

	qp->cq_tail = (qp->cq_tail + 1) % (1 << qp->cq_softc->ring_size_order);
	return (0);
}

static int __must_check
ccp_sha(struct ccp_queue *qp, enum sha_version version, struct sglist *sgl_src,
    struct sglist *sgl_dst, const struct ccp_completion_ctx *cctx)
{
	const struct SHA_Defn *defn;
	struct sglist_seg *seg;
	size_t i, msgsize, remaining, nb;
	uint32_t lsbaddr;
	int error;

	for (i = 0; i < nitems(SHA_definitions); i++)
		if (SHA_definitions[i].version == version)
			break;
	if (i == nitems(SHA_definitions))
		return (EINVAL);
	defn = &SHA_definitions[i];

	/* XXX validate input ??? */

	/* Load initial SHA state into LSB */
	/* XXX ensure H_vectors don't span page boundaries */
	error = ccp_passthrough(qp, ccp_queue_lsb_address(qp, LSB_ENTRY_SHA),
	    CCP_MEMTYPE_SB, pmap_kextract((vm_offset_t)defn->H_vectors),
	    CCP_MEMTYPE_SYSTEM, roundup2(defn->H_size, LSB_ENTRY_SIZE),
	    CCP_PASSTHRU_BYTESWAP_NOOP, CCP_PASSTHRU_BITWISE_NOOP, false,
	    NULL);
	if (error != 0)
		return (error);

	/* Execute series of SHA updates on correctly sized buffers */
	msgsize = 0;
	for (i = 0; i < sgl_src->sg_nseg; i++) {
		seg = &sgl_src->sg_segs[i];
		msgsize += seg->ss_len;
		error = ccp_sha_single_desc(qp, defn, seg->ss_paddr,
		    seg->ss_len, i == 0, i == sgl_src->sg_nseg - 1,
		    msgsize << 3);
		if (error != 0)
			return (error);
	}

	/* Copy result out to sgl_dst */
	remaining = roundup2(defn->H_size, LSB_ENTRY_SIZE);
	lsbaddr = ccp_queue_lsb_address(qp, LSB_ENTRY_SHA);
	for (i = 0; i < sgl_dst->sg_nseg; i++) {
		seg = &sgl_dst->sg_segs[i];
		/* crd_len is int, so 32-bit min() is ok. */
		nb = min(remaining, seg->ss_len);

		error = ccp_passthrough(qp, seg->ss_paddr, CCP_MEMTYPE_SYSTEM,
		    lsbaddr, CCP_MEMTYPE_SB, nb, CCP_PASSTHRU_BYTESWAP_NOOP,
		    CCP_PASSTHRU_BITWISE_NOOP,
		    (cctx != NULL) ? (nb == remaining) : false,
		    (nb == remaining) ? cctx : NULL);
		if (error != 0)
			return (error);

		remaining -= nb;
		lsbaddr += nb;
		if (remaining == 0)
			break;
	}

	return (0);
}

static void
byteswap256(uint64_t *buffer)
{
	uint64_t t;

	t = bswap64(buffer[3]);
	buffer[3] = bswap64(buffer[0]);
	buffer[0] = t;

	t = bswap64(buffer[2]);
	buffer[2] = bswap64(buffer[1]);
	buffer[1] = t;
}

/*
 * Translate CCP internal LSB hash format into a standard hash ouput.
 *
 * Manipulates input buffer with byteswap256 operation.
 */
static void
ccp_sha_copy_result(char *output, char *buffer, enum sha_version version)
{
	const struct SHA_Defn *defn;
	size_t i;

	for (i = 0; i < nitems(SHA_definitions); i++)
		if (SHA_definitions[i].version == version)
			break;
	if (i == nitems(SHA_definitions))
		panic("bogus sha version auth_mode %u\n", (unsigned)version);

	defn = &SHA_definitions[i];

	/* Swap 256bit manually -- DMA engine can, but with limitations */
	byteswap256((void *)buffer);
	if (defn->axf->hashsize > LSB_ENTRY_SIZE)
		byteswap256((void *)(buffer + LSB_ENTRY_SIZE));

	switch (defn->version) {
	case SHA1:
		memcpy(output, buffer + 12, defn->axf->hashsize);
		break;
#if 0
	case SHA2_224:
		memcpy(output, buffer + XXX, defn->axf->hashsize);
		break;
#endif
	case SHA2_256:
		memcpy(output, buffer, defn->axf->hashsize);
		break;
	case SHA2_384:
		memcpy(output,
		    buffer + LSB_ENTRY_SIZE * 3 - defn->axf->hashsize,
		    defn->axf->hashsize - LSB_ENTRY_SIZE);
		memcpy(output + defn->axf->hashsize - LSB_ENTRY_SIZE, buffer,
		    LSB_ENTRY_SIZE);
		break;
	case SHA2_512:
		memcpy(output, buffer + LSB_ENTRY_SIZE, LSB_ENTRY_SIZE);
		memcpy(output + LSB_ENTRY_SIZE, buffer, LSB_ENTRY_SIZE);
		break;
	}
}

static void
ccp_do_hmac_done(struct ccp_queue *qp, struct ccp_session *s,
    struct cryptop *crp, struct cryptodesc *crd, int error)
{
	char ihash[SHA2_512_HASH_LEN /* max hash len */];
	union authctx auth_ctx;
	struct auth_hash *axf;

	axf = s->hmac.auth_hash;

	s->pending--;

	if (error != 0) {
		crp->crp_etype = error;
		goto out;
	}

	/* Do remaining outer hash over small inner hash in software */
	axf->Init(&auth_ctx);
	axf->Update(&auth_ctx, s->hmac.opad, axf->blocksize);
	ccp_sha_copy_result(ihash, s->hmac.ipad, s->hmac.auth_mode);
#if 0
	INSECURE_DEBUG(dev, "%s sha intermediate=%64D\n", __func__,
	    (u_char *)ihash, " ");
#endif
	axf->Update(&auth_ctx, ihash, axf->hashsize);
	axf->Final(s->hmac.ipad, &auth_ctx);

	crypto_copyback(crp->crp_flags, crp->crp_buf, crd->crd_inject,
	    s->hmac.hash_len, s->hmac.ipad);

	/* Avoid leaking key material */
	explicit_bzero(&auth_ctx, sizeof(auth_ctx));
	explicit_bzero(s->hmac.ipad, sizeof(s->hmac.ipad));
	explicit_bzero(s->hmac.opad, sizeof(s->hmac.opad));

out:
	crypto_done(crp);
}

static void
ccp_hmac_done(struct ccp_queue *qp, struct ccp_session *s, void *vcrp,
    int error)
{
	struct cryptodesc *crd;
	struct cryptop *crp;

	crp = vcrp;
	crd = crp->crp_desc;
	ccp_do_hmac_done(qp, s, crp, crd, error);
}

static int __must_check
ccp_do_hmac(struct ccp_queue *qp, struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crd, const struct ccp_completion_ctx *cctx)
{
	device_t dev;
	struct auth_hash *axf;
	int error;

	dev = qp->cq_softc->dev;
	axf = s->hmac.auth_hash;

	/*
	 * Populate the SGL describing inside hash contents.  We want to hash
	 * the ipad (key XOR fixed bit pattern) concatenated with the user
	 * data.
	 */
	sglist_reset(qp->cq_sg_ulptx);
	error = sglist_append(qp->cq_sg_ulptx, s->hmac.ipad, axf->blocksize);
	if (error != 0)
		return (error);
	error = sglist_append_sglist(qp->cq_sg_ulptx, qp->cq_sg_crp,
	    crd->crd_skip, crd->crd_len);
	if (error != 0) {
		DPRINTF(dev, "%s: sglist too short\n", __func__);
		return (error);
	}
	/* Populate SGL for output -- just reuse hmac.ipad buffer. */
	sglist_reset(qp->cq_sg_dst);
	error = sglist_append(qp->cq_sg_dst, s->hmac.ipad,
	    roundup2(axf->hashsize, LSB_ENTRY_SIZE));
	if (error != 0)
		return (error);

	error = ccp_sha(qp, s->hmac.auth_mode, qp->cq_sg_ulptx, qp->cq_sg_dst,
	    cctx);
	if (error != 0) {
		DPRINTF(dev, "%s: ccp_sha error\n", __func__);
		return (error);
	}
	return (0);
}

int __must_check
ccp_hmac(struct ccp_queue *qp, struct ccp_session *s, struct cryptop *crp)
{
	struct ccp_completion_ctx ctx;
	struct cryptodesc *crd;

	crd = crp->crp_desc;

	ctx.callback_fn = ccp_hmac_done;
	ctx.callback_arg = crp;
	ctx.session = s;

	return (ccp_do_hmac(qp, s, crp, crd, &ctx));
}

static void
ccp_byteswap(char *data, size_t len)
{
	size_t i;
	char t;

	len--;
	for (i = 0; i < len; i++, len--) {
		t = data[i];
		data[i] = data[len];
		data[len] = t;
	}
}

static void
ccp_blkcipher_done(struct ccp_queue *qp, struct ccp_session *s, void *vcrp,
    int error)
{
	struct cryptop *crp;

	explicit_bzero(&s->blkcipher, sizeof(s->blkcipher));

	crp = vcrp;

	s->pending--;

	if (error != 0)
		crp->crp_etype = error;

	DPRINTF(qp->cq_softc->dev, "%s: qp=%p crp=%p\n", __func__, qp, crp);
	crypto_done(crp);
}

static void
ccp_collect_iv(struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crd)
{

	if (crd->crd_flags & CRD_F_ENCRYPT) {
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(s->blkcipher.iv, crd->crd_iv,
			    s->blkcipher.iv_len);
		else
			arc4rand(s->blkcipher.iv, s->blkcipher.iv_len, 0);
		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, s->blkcipher.iv_len,
			    s->blkcipher.iv);
	} else {
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(s->blkcipher.iv, crd->crd_iv,
			    s->blkcipher.iv_len);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, s->blkcipher.iv_len,
			    s->blkcipher.iv);
	}

	/*
	 * If the input IV is 12 bytes, append an explicit counter of 1.
	 */
	if (crd->crd_alg == CRYPTO_AES_NIST_GCM_16 &&
	    s->blkcipher.iv_len == 12) {
		*(uint32_t *)&s->blkcipher.iv[12] = htobe32(1);
		s->blkcipher.iv_len = AES_BLOCK_LEN;
	}

	if (crd->crd_alg == CRYPTO_AES_XTS && s->blkcipher.iv_len != AES_BLOCK_LEN) {
		DPRINTF(NULL, "got ivlen != 16: %u\n", s->blkcipher.iv_len);
		if (s->blkcipher.iv_len < AES_BLOCK_LEN)
			memset(&s->blkcipher.iv[s->blkcipher.iv_len], 0,
			    AES_BLOCK_LEN - s->blkcipher.iv_len);
		s->blkcipher.iv_len = AES_BLOCK_LEN;
	}

	/* Reverse order of IV material for HW */
	INSECURE_DEBUG(NULL, "%s: IV: %16D len: %u\n", __func__,
	    s->blkcipher.iv, " ", s->blkcipher.iv_len);

	/*
	 * For unknown reasons, XTS mode expects the IV in the reverse byte
	 * order to every other AES mode.
	 */
	if (crd->crd_alg != CRYPTO_AES_XTS)
		ccp_byteswap(s->blkcipher.iv, s->blkcipher.iv_len);
}

static int __must_check
ccp_do_pst_to_lsb(struct ccp_queue *qp, uint32_t lsbaddr, const void *src,
    size_t len)
{
	int error;

	sglist_reset(qp->cq_sg_ulptx);
	error = sglist_append(qp->cq_sg_ulptx, __DECONST(void *, src), len);
	if (error != 0)
		return (error);

	error = ccp_passthrough_sgl(qp, lsbaddr, true, qp->cq_sg_ulptx, len,
	    false, NULL);
	return (error);
}

static int __must_check
ccp_do_xts(struct ccp_queue *qp, struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crd, enum ccp_cipher_dir dir,
    const struct ccp_completion_ctx *cctx)
{
	struct ccp_desc *desc;
	device_t dev;
	unsigned i;
	enum ccp_xts_unitsize usize;

	/* IV and Key data are already loaded */

	dev = qp->cq_softc->dev;

	for (i = 0; i < nitems(ccp_xts_unitsize_map); i++)
		if (ccp_xts_unitsize_map[i].cxu_size == crd->crd_len) {
			usize = ccp_xts_unitsize_map[i].cxu_id;
			break;
		}
	if (i >= nitems(ccp_xts_unitsize_map))
		return (EINVAL);

	for (i = 0; i < qp->cq_sg_ulptx->sg_nseg; i++) {
		struct sglist_seg *seg;

		seg = &qp->cq_sg_ulptx->sg_segs[i];

		desc = &qp->desc_ring[qp->cq_tail];
		desc->engine = CCP_ENGINE_XTS_AES;
		desc->som = (i == 0);
		desc->eom = (i == qp->cq_sg_ulptx->sg_nseg - 1);
		desc->ioc = (desc->eom && cctx != NULL);
		DPRINTF(dev, "%s: XTS %u: som:%d eom:%d ioc:%d dir:%d\n",
		    __func__, qp->cq_tail, (int)desc->som, (int)desc->eom,
		    (int)desc->ioc, (int)dir);

		if (desc->ioc)
			memcpy(&qp->completions_ring[qp->cq_tail], cctx,
			    sizeof(*cctx));

		desc->aes_xts.encrypt = dir;
		desc->aes_xts.type = s->blkcipher.cipher_type;
		desc->aes_xts.size = usize;

		DPRINTF(dev, "XXX %s: XTS %u: type:%u size:%u\n", __func__,
		    qp->cq_tail, (unsigned)desc->aes_xts.type,
		    (unsigned)desc->aes_xts.size);

		desc->length = seg->ss_len;
		desc->src_lo = (uint32_t)seg->ss_paddr;
		desc->src_hi = (seg->ss_paddr >> 32);
		desc->src_mem = CCP_MEMTYPE_SYSTEM;

		/* Crypt in-place */
		desc->dst_lo = desc->src_lo;
		desc->dst_hi = desc->src_hi;
		desc->dst_mem = desc->src_mem;

		desc->key_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_KEY);
		desc->key_hi = 0;
		desc->key_mem = CCP_MEMTYPE_SB;

		desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_IV);

		qp->cq_tail = (qp->cq_tail + 1) %
		    (1 << qp->cq_softc->ring_size_order);
	}
	return (0);
}

static int __must_check
ccp_do_blkcipher(struct ccp_queue *qp, struct ccp_session *s,
    struct cryptop *crp, struct cryptodesc *crd,
    const struct ccp_completion_ctx *cctx)
{
	struct ccp_desc *desc;
	char *keydata;
	device_t dev;
	enum ccp_cipher_dir dir;
	int error;
	size_t keydata_len;
	unsigned i, j;

	dev = qp->cq_softc->dev;

	if (s->blkcipher.key_len == 0 || crd->crd_len == 0) {
		DPRINTF(dev, "%s: empty\n", __func__);
		return (EINVAL);
	}
	if ((crd->crd_len % AES_BLOCK_LEN) != 0) {
		DPRINTF(dev, "%s: len modulo: %d\n", __func__, crd->crd_len);
		return (EINVAL);
	}

	/*
	 * Individual segments must be multiples of AES block size for the HW
	 * to process it.  Non-compliant inputs aren't bogus, just not doable
	 * on this hardware.
	 */
	for (i = 0; i < qp->cq_sg_crp->sg_nseg; i++)
		if ((qp->cq_sg_crp->sg_segs[i].ss_len % AES_BLOCK_LEN) != 0) {
			DPRINTF(dev, "%s: seg modulo: %zu\n", __func__,
			    qp->cq_sg_crp->sg_segs[i].ss_len);
			return (EINVAL);
		}

	/* Gather IV/nonce data */
	ccp_collect_iv(s, crp, crd);

	if ((crd->crd_flags & CRD_F_ENCRYPT) != 0)
		dir = CCP_CIPHER_DIR_ENCRYPT;
	else
		dir = CCP_CIPHER_DIR_DECRYPT;

	/* Set up passthrough op(s) to copy IV into LSB */
	error = ccp_do_pst_to_lsb(qp, ccp_queue_lsb_address(qp, LSB_ENTRY_IV),
	    s->blkcipher.iv, s->blkcipher.iv_len);
	if (error != 0)
		return (error);

	/*
	 * Initialize keydata and keydata_len for GCC.  The default case of the
	 * following switch is impossible to reach, but GCC doesn't know that.
	 */
	keydata_len = 0;
	keydata = NULL;

	switch (crd->crd_alg) {
	case CRYPTO_AES_XTS:
		for (j = 0; j < nitems(ccp_xts_unitsize_map); j++)
			if (ccp_xts_unitsize_map[j].cxu_size == crd->crd_len)
				break;
		/* Input buffer must be a supported UnitSize */
		if (j >= nitems(ccp_xts_unitsize_map)) {
			device_printf(dev, "%s: rejected block size: %u\n",
			    __func__, crd->crd_len);
			return (EOPNOTSUPP);
		}
		/* FALLTHROUGH */
	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ICM:
		keydata = s->blkcipher.enckey;
		keydata_len = s->blkcipher.key_len;
		break;
	}

	INSECURE_DEBUG(dev, "%s: KEY(%zu): %16D\n", __func__, keydata_len,
	    keydata, " ");
	if (crd->crd_alg == CRYPTO_AES_XTS)
		INSECURE_DEBUG(dev, "%s: KEY(XTS): %64D\n", __func__, keydata, " ");

	/* Reverse order of key material for HW */
	ccp_byteswap(keydata, keydata_len);

	/* Store key material into LSB to avoid page boundaries */
	if (crd->crd_alg == CRYPTO_AES_XTS) {
		/*
		 * XTS mode uses 2 256-bit vectors for the primary key and the
		 * tweak key.  For 128-bit keys, the vectors are zero-padded.
		 *
		 * After byteswapping the combined OCF-provided K1:K2 vector
		 * above, we need to reverse the order again so the hardware
		 * gets the swapped keys in the order K1':K2'.
		 */
		error = ccp_do_pst_to_lsb(qp,
		    ccp_queue_lsb_address(qp, LSB_ENTRY_KEY + 1), keydata,
		    keydata_len / 2);
		if (error != 0)
			return (error);
		error = ccp_do_pst_to_lsb(qp,
		    ccp_queue_lsb_address(qp, LSB_ENTRY_KEY),
		    keydata + (keydata_len / 2), keydata_len / 2);

		/* Zero-pad 128 bit keys */
		if (keydata_len == 32) {
			if (error != 0)
				return (error);
			error = ccp_do_pst_to_lsb(qp,
			    ccp_queue_lsb_address(qp, LSB_ENTRY_KEY) +
			    keydata_len / 2, g_zeroes, keydata_len / 2);
			if (error != 0)
				return (error);
			error = ccp_do_pst_to_lsb(qp,
			    ccp_queue_lsb_address(qp, LSB_ENTRY_KEY + 1) +
			    keydata_len / 2, g_zeroes, keydata_len / 2);
		}
	} else
		error = ccp_do_pst_to_lsb(qp,
		    ccp_queue_lsb_address(qp, LSB_ENTRY_KEY), keydata,
		    keydata_len);
	if (error != 0)
		return (error);

	/*
	 * Point SGLs at the subset of cryptop buffer contents representing the
	 * data.
	 */
	sglist_reset(qp->cq_sg_ulptx);
	error = sglist_append_sglist(qp->cq_sg_ulptx, qp->cq_sg_crp,
	    crd->crd_skip, crd->crd_len);
	if (error != 0)
		return (error);

	INSECURE_DEBUG(dev, "%s: Contents: %16D\n", __func__,
	    (void *)PHYS_TO_DMAP(qp->cq_sg_ulptx->sg_segs[0].ss_paddr), " ");

	DPRINTF(dev, "%s: starting AES ops @ %u\n", __func__, qp->cq_tail);

	if (ccp_queue_get_ring_space(qp) < qp->cq_sg_ulptx->sg_nseg)
		return (EAGAIN);

	if (crd->crd_alg == CRYPTO_AES_XTS)
		return (ccp_do_xts(qp, s, crp, crd, dir, cctx));

	for (i = 0; i < qp->cq_sg_ulptx->sg_nseg; i++) {
		struct sglist_seg *seg;

		seg = &qp->cq_sg_ulptx->sg_segs[i];

		desc = &qp->desc_ring[qp->cq_tail];
		desc->engine = CCP_ENGINE_AES;
		desc->som = (i == 0);
		desc->eom = (i == qp->cq_sg_ulptx->sg_nseg - 1);
		desc->ioc = (desc->eom && cctx != NULL);
		DPRINTF(dev, "%s: AES %u: som:%d eom:%d ioc:%d dir:%d\n",
		    __func__, qp->cq_tail, (int)desc->som, (int)desc->eom,
		    (int)desc->ioc, (int)dir);

		if (desc->ioc)
			memcpy(&qp->completions_ring[qp->cq_tail], cctx,
			    sizeof(*cctx));

		desc->aes.encrypt = dir;
		desc->aes.mode = s->blkcipher.cipher_mode;
		desc->aes.type = s->blkcipher.cipher_type;
		if (crd->crd_alg == CRYPTO_AES_ICM)
			/*
			 * Size of CTR value in bits, - 1.  ICM mode uses all
			 * 128 bits as counter.
			 */
			desc->aes.size = 127;

		DPRINTF(dev, "%s: AES %u: mode:%u type:%u size:%u\n", __func__,
		    qp->cq_tail, (unsigned)desc->aes.mode,
		    (unsigned)desc->aes.type, (unsigned)desc->aes.size);

		desc->length = seg->ss_len;
		desc->src_lo = (uint32_t)seg->ss_paddr;
		desc->src_hi = (seg->ss_paddr >> 32);
		desc->src_mem = CCP_MEMTYPE_SYSTEM;

		/* Crypt in-place */
		desc->dst_lo = desc->src_lo;
		desc->dst_hi = desc->src_hi;
		desc->dst_mem = desc->src_mem;

		desc->key_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_KEY);
		desc->key_hi = 0;
		desc->key_mem = CCP_MEMTYPE_SB;

		desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_IV);

		qp->cq_tail = (qp->cq_tail + 1) %
		    (1 << qp->cq_softc->ring_size_order);
	}
	return (0);
}

int __must_check
ccp_blkcipher(struct ccp_queue *qp, struct ccp_session *s, struct cryptop *crp)
{
	struct ccp_completion_ctx ctx;
	struct cryptodesc *crd;

	crd = crp->crp_desc;

	ctx.callback_fn = ccp_blkcipher_done;
	ctx.session = s;
	ctx.callback_arg = crp;

	return (ccp_do_blkcipher(qp, s, crp, crd, &ctx));
}

static void
ccp_authenc_done(struct ccp_queue *qp, struct ccp_session *s, void *vcrp,
    int error)
{
	struct cryptodesc *crda;
	struct cryptop *crp;

	explicit_bzero(&s->blkcipher, sizeof(s->blkcipher));

	crp = vcrp;
	if (s->cipher_first)
		crda = crp->crp_desc->crd_next;
	else
		crda = crp->crp_desc;

	ccp_do_hmac_done(qp, s, crp, crda, error);
}

int __must_check
ccp_authenc(struct ccp_queue *qp, struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde)
{
	struct ccp_completion_ctx ctx;
	int error;

	ctx.callback_fn = ccp_authenc_done;
	ctx.session = s;
	ctx.callback_arg = crp;

	/* Perform first operation */
	if (s->cipher_first)
		error = ccp_do_blkcipher(qp, s, crp, crde, NULL);
	else
		error = ccp_do_hmac(qp, s, crp, crda, NULL);
	if (error != 0)
		return (error);

	/* Perform second operation */
	if (s->cipher_first)
		error = ccp_do_hmac(qp, s, crp, crda, &ctx);
	else
		error = ccp_do_blkcipher(qp, s, crp, crde, &ctx);
	return (error);
}

static int __must_check
ccp_do_ghash_aad(struct ccp_queue *qp, struct ccp_session *s)
{
	struct ccp_desc *desc;
	struct sglist_seg *seg;
	unsigned i;

	if (ccp_queue_get_ring_space(qp) < qp->cq_sg_ulptx->sg_nseg)
		return (EAGAIN);

	for (i = 0; i < qp->cq_sg_ulptx->sg_nseg; i++) {
		seg = &qp->cq_sg_ulptx->sg_segs[i];

		desc = &qp->desc_ring[qp->cq_tail];

		desc->engine = CCP_ENGINE_AES;
		desc->aes.mode = CCP_AES_MODE_GHASH;
		desc->aes.type = s->blkcipher.cipher_type;
		desc->aes.encrypt = CCP_AES_MODE_GHASH_AAD;

		desc->som = (i == 0);
		desc->length = seg->ss_len;

		desc->src_lo = (uint32_t)seg->ss_paddr;
		desc->src_hi = (seg->ss_paddr >> 32);
		desc->src_mem = CCP_MEMTYPE_SYSTEM;

		desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_IV);

		desc->key_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_KEY);
		desc->key_mem = CCP_MEMTYPE_SB;

		qp->cq_tail = (qp->cq_tail + 1) %
		    (1 << qp->cq_softc->ring_size_order);
	}
	return (0);
}

static int __must_check
ccp_do_gctr(struct ccp_queue *qp, struct ccp_session *s,
    enum ccp_cipher_dir dir, struct sglist_seg *seg, bool som, bool eom)
{
	struct ccp_desc *desc;

	if (ccp_queue_get_ring_space(qp) == 0)
		return (EAGAIN);

	desc = &qp->desc_ring[qp->cq_tail];

	desc->engine = CCP_ENGINE_AES;
	desc->aes.mode = CCP_AES_MODE_GCTR;
	desc->aes.type = s->blkcipher.cipher_type;
	desc->aes.encrypt = dir;
	desc->aes.size = 8 * (seg->ss_len % GMAC_BLOCK_LEN) - 1;

	desc->som = som;
	desc->eom = eom;

	/* Trailing bytes will be masked off by aes.size above. */
	desc->length = roundup2(seg->ss_len, GMAC_BLOCK_LEN);

	desc->dst_lo = desc->src_lo = (uint32_t)seg->ss_paddr;
	desc->dst_hi = desc->src_hi = seg->ss_paddr >> 32;
	desc->dst_mem = desc->src_mem = CCP_MEMTYPE_SYSTEM;

	desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_IV);

	desc->key_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_KEY);
	desc->key_mem = CCP_MEMTYPE_SB;

	qp->cq_tail = (qp->cq_tail + 1) %
	    (1 << qp->cq_softc->ring_size_order);
	return (0);
}

static int __must_check
ccp_do_ghash_final(struct ccp_queue *qp, struct ccp_session *s)
{
	struct ccp_desc *desc;

	if (ccp_queue_get_ring_space(qp) == 0)
		return (EAGAIN);

	desc = &qp->desc_ring[qp->cq_tail];

	desc->engine = CCP_ENGINE_AES;
	desc->aes.mode = CCP_AES_MODE_GHASH;
	desc->aes.type = s->blkcipher.cipher_type;
	desc->aes.encrypt = CCP_AES_MODE_GHASH_FINAL;

	desc->length = GMAC_BLOCK_LEN;

	desc->src_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_GHASH_IN);
	desc->src_mem = CCP_MEMTYPE_SB;

	desc->lsb_ctx_id = ccp_queue_lsb_entry(qp, LSB_ENTRY_IV);

	desc->key_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_KEY);
	desc->key_mem = CCP_MEMTYPE_SB;

	desc->dst_lo = ccp_queue_lsb_address(qp, LSB_ENTRY_GHASH);
	desc->dst_mem = CCP_MEMTYPE_SB;

	qp->cq_tail = (qp->cq_tail + 1) %
	    (1 << qp->cq_softc->ring_size_order);
	return (0);
}

static void
ccp_gcm_done(struct ccp_queue *qp, struct ccp_session *s, void *vcrp,
    int error)
{
	char tag[GMAC_DIGEST_LEN];
	struct cryptodesc *crde, *crda;
	struct cryptop *crp;

	crp = vcrp;
	if (s->cipher_first) {
		crde = crp->crp_desc;
		crda = crp->crp_desc->crd_next;
	} else {
		crde = crp->crp_desc->crd_next;
		crda = crp->crp_desc;
	}

	s->pending--;

	if (error != 0) {
		crp->crp_etype = error;
		goto out;
	}

	/* Encrypt is done.  Decrypt needs to verify tag. */
	if ((crde->crd_flags & CRD_F_ENCRYPT) != 0)
		goto out;

	/* Copy in message tag. */
	crypto_copydata(crp->crp_flags, crp->crp_buf, crda->crd_inject,
	    sizeof(tag), tag);

	/* Verify tag against computed GMAC */
	if (timingsafe_bcmp(tag, s->gmac.final_block, s->gmac.hash_len) != 0)
		crp->crp_etype = EBADMSG;

out:
	explicit_bzero(&s->blkcipher, sizeof(s->blkcipher));
	explicit_bzero(&s->gmac, sizeof(s->gmac));
	crypto_done(crp);
}

int __must_check
ccp_gcm(struct ccp_queue *qp, struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde)
{
	struct ccp_completion_ctx ctx;
	enum ccp_cipher_dir dir;
	device_t dev;
	unsigned i;
	int error;

	if (s->blkcipher.key_len == 0)
		return (EINVAL);

	/*
	 * AAD is only permitted before the cipher/plain text, not
	 * after.
	 */
	if (crda->crd_len + crda->crd_skip > crde->crd_len + crde->crd_skip)
		return (EINVAL);

	dev = qp->cq_softc->dev;

	if ((crde->crd_flags & CRD_F_ENCRYPT) != 0)
		dir = CCP_CIPHER_DIR_ENCRYPT;
	else
		dir = CCP_CIPHER_DIR_DECRYPT;

	/* Zero initial GHASH portion of context */
	memset(s->blkcipher.iv, 0, sizeof(s->blkcipher.iv));

	/* Gather IV data */
	ccp_collect_iv(s, crp, crde);

	/* Reverse order of key material for HW */
	ccp_byteswap(s->blkcipher.enckey, s->blkcipher.key_len);

	/* Prepare input buffer of concatenated lengths for final GHASH */
	be64enc(s->gmac.final_block, (uint64_t)crda->crd_len * 8);
	be64enc(&s->gmac.final_block[8], (uint64_t)crde->crd_len * 8);

	/* Send IV + initial zero GHASH, key data, and lengths buffer to LSB */
	error = ccp_do_pst_to_lsb(qp, ccp_queue_lsb_address(qp, LSB_ENTRY_IV),
	    s->blkcipher.iv, 32);
	if (error != 0)
		return (error);
	error = ccp_do_pst_to_lsb(qp, ccp_queue_lsb_address(qp, LSB_ENTRY_KEY),
	    s->blkcipher.enckey, s->blkcipher.key_len);
	if (error != 0)
		return (error);
	error = ccp_do_pst_to_lsb(qp,
	    ccp_queue_lsb_address(qp, LSB_ENTRY_GHASH_IN), s->gmac.final_block,
	    GMAC_BLOCK_LEN);
	if (error != 0)
		return (error);

	/* First step - compute GHASH over AAD */
	if (crda->crd_len != 0) {
		sglist_reset(qp->cq_sg_ulptx);
		error = sglist_append_sglist(qp->cq_sg_ulptx, qp->cq_sg_crp,
		    crda->crd_skip, crda->crd_len);
		if (error != 0)
			return (error);

		/* This engine cannot process non-block multiple AAD data. */
		for (i = 0; i < qp->cq_sg_ulptx->sg_nseg; i++)
			if ((qp->cq_sg_ulptx->sg_segs[i].ss_len %
			    GMAC_BLOCK_LEN) != 0) {
				DPRINTF(dev, "%s: AD seg modulo: %zu\n",
				    __func__,
				    qp->cq_sg_ulptx->sg_segs[i].ss_len);
				return (EINVAL);
			}

		error = ccp_do_ghash_aad(qp, s);
		if (error != 0)
			return (error);
	}

	/* Feed data piece by piece into GCTR */
	sglist_reset(qp->cq_sg_ulptx);
	error = sglist_append_sglist(qp->cq_sg_ulptx, qp->cq_sg_crp,
	    crde->crd_skip, crde->crd_len);
	if (error != 0)
		return (error);

	/*
	 * All segments except the last must be even multiples of AES block
	 * size for the HW to process it.  Non-compliant inputs aren't bogus,
	 * just not doable on this hardware.
	 *
	 * XXX: Well, the hardware will produce a valid tag for shorter final
	 * segment inputs, but it will still write out a block-sized plaintext
	 * or ciphertext chunk.  For a typical CRP this tramples trailing data,
	 * including the provided message tag.  So, reject such inputs for now.
	 */
	for (i = 0; i < qp->cq_sg_ulptx->sg_nseg; i++)
		if ((qp->cq_sg_ulptx->sg_segs[i].ss_len % AES_BLOCK_LEN) != 0) {
			DPRINTF(dev, "%s: seg modulo: %zu\n", __func__,
			    qp->cq_sg_ulptx->sg_segs[i].ss_len);
			return (EINVAL);
		}

	for (i = 0; i < qp->cq_sg_ulptx->sg_nseg; i++) {
		struct sglist_seg *seg;

		seg = &qp->cq_sg_ulptx->sg_segs[i];
		error = ccp_do_gctr(qp, s, dir, seg,
		    (i == 0 && crda->crd_len == 0),
		    i == (qp->cq_sg_ulptx->sg_nseg - 1));
		if (error != 0)
			return (error);
	}

	/* Send just initial IV (not GHASH!) to LSB again */
	error = ccp_do_pst_to_lsb(qp, ccp_queue_lsb_address(qp, LSB_ENTRY_IV),
	    s->blkcipher.iv, s->blkcipher.iv_len);
	if (error != 0)
		return (error);

	ctx.callback_fn = ccp_gcm_done;
	ctx.session = s;
	ctx.callback_arg = crp;

	/* Compute final hash and copy result back */
	error = ccp_do_ghash_final(qp, s);
	if (error != 0)
		return (error);

	/* When encrypting, copy computed tag out to caller buffer. */
	sglist_reset(qp->cq_sg_ulptx);
	if (dir == CCP_CIPHER_DIR_ENCRYPT)
		error = sglist_append_sglist(qp->cq_sg_ulptx, qp->cq_sg_crp,
		    crda->crd_inject, s->gmac.hash_len);
	else
		/*
		 * For decrypting, copy the computed tag out to our session
		 * buffer to verify in our callback.
		 */
		error = sglist_append(qp->cq_sg_ulptx, s->gmac.final_block,
		    s->gmac.hash_len);
	if (error != 0)
		return (error);
	error = ccp_passthrough_sgl(qp,
	    ccp_queue_lsb_address(qp, LSB_ENTRY_GHASH), false, qp->cq_sg_ulptx,
	    s->gmac.hash_len, true, &ctx);
	return (error);
}

#define MAX_TRNG_RETRIES	10
u_int
random_ccp_read(void *v, u_int c)
{
	uint32_t *buf;
	u_int i, j;

	KASSERT(c % sizeof(*buf) == 0, ("%u not multiple of u_long", c));

	buf = v;
	for (i = c; i > 0; i -= sizeof(*buf)) {
		for (j = 0; j < MAX_TRNG_RETRIES; j++) {
			*buf = ccp_read_4(g_ccp_softc, TRNG_OUT_OFFSET);
			if (*buf != 0)
				break;
		}
		if (j == MAX_TRNG_RETRIES)
			return (0);
		buf++;
	}
	return (c);

}

#ifdef DDB
void
db_ccp_show_hw(struct ccp_softc *sc)
{

	db_printf("  queue mask: 0x%x\n",
	    ccp_read_4(sc, CMD_QUEUE_MASK_OFFSET));
	db_printf("  queue prio: 0x%x\n",
	    ccp_read_4(sc, CMD_QUEUE_PRIO_OFFSET));
	db_printf("  reqid: 0x%x\n", ccp_read_4(sc, CMD_REQID_CONFIG_OFFSET));
	db_printf("  trng output: 0x%x\n", ccp_read_4(sc, TRNG_OUT_OFFSET));
	db_printf("  cmd timeout: 0x%x\n",
	    ccp_read_4(sc, CMD_CMD_TIMEOUT_OFFSET));
	db_printf("  lsb public mask lo: 0x%x\n",
	    ccp_read_4(sc, LSB_PUBLIC_MASK_LO_OFFSET));
	db_printf("  lsb public mask hi: 0x%x\n",
	    ccp_read_4(sc, LSB_PUBLIC_MASK_HI_OFFSET));
	db_printf("  lsb private mask lo: 0x%x\n",
	    ccp_read_4(sc, LSB_PRIVATE_MASK_LO_OFFSET));
	db_printf("  lsb private mask hi: 0x%x\n",
	    ccp_read_4(sc, LSB_PRIVATE_MASK_HI_OFFSET));
	db_printf("  version: 0x%x\n", ccp_read_4(sc, VERSION_REG));
}

void
db_ccp_show_queue_hw(struct ccp_queue *qp)
{
	const struct ccp_error_code *ec;
	struct ccp_softc *sc;
	uint32_t status, error, esource, faultblock, headlo, qcontrol;
	unsigned q, i;

	sc = qp->cq_softc;
	q = qp->cq_qindex;

	qcontrol = ccp_read_queue_4(sc, q, CMD_Q_CONTROL_BASE);
	db_printf("  qcontrol: 0x%x%s%s\n", qcontrol,
	    (qcontrol & CMD_Q_RUN) ? " RUN" : "",
	    (qcontrol & CMD_Q_HALTED) ? " HALTED" : "");
	db_printf("  tail_lo: 0x%x\n",
	    ccp_read_queue_4(sc, q, CMD_Q_TAIL_LO_BASE));
	headlo = ccp_read_queue_4(sc, q, CMD_Q_HEAD_LO_BASE);
	db_printf("  head_lo: 0x%x\n", headlo);
	db_printf("  int enable: 0x%x\n",
	    ccp_read_queue_4(sc, q, CMD_Q_INT_ENABLE_BASE));
	db_printf("  interrupt status: 0x%x\n",
	    ccp_read_queue_4(sc, q, CMD_Q_INTERRUPT_STATUS_BASE));
	status = ccp_read_queue_4(sc, q, CMD_Q_STATUS_BASE);
	db_printf("  status: 0x%x\n", status);
	db_printf("  int stats: 0x%x\n",
	    ccp_read_queue_4(sc, q, CMD_Q_INT_STATUS_BASE));

	error = status & STATUS_ERROR_MASK;
	if (error == 0)
		return;

	esource = (status >> STATUS_ERRORSOURCE_SHIFT) &
	    STATUS_ERRORSOURCE_MASK;
	faultblock = (status >> STATUS_VLSB_FAULTBLOCK_SHIFT) &
	    STATUS_VLSB_FAULTBLOCK_MASK;

	ec = NULL;
	for (i = 0; i < nitems(ccp_error_codes); i++)
		if (ccp_error_codes[i].ce_code == error)
			break;
	if (i < nitems(ccp_error_codes))
		ec = &ccp_error_codes[i];

	db_printf("  Error: %s (%u) Source: %u Faulting LSB block: %u\n",
	    (ec != NULL) ? ec->ce_name : "(reserved)", error, esource,
	    faultblock);
	if (ec != NULL)
		db_printf("  Error description: %s\n", ec->ce_desc);

	i = (headlo - (uint32_t)qp->desc_ring_bus_addr) / Q_DESC_SIZE;
	db_printf("  Bad descriptor idx: %u contents:\n  %32D\n", i,
	    (void *)&qp->desc_ring[i], " ");
}
#endif
