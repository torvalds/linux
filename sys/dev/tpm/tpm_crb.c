/*-
 * Copyright (c) 2018 Stormshield.
 * Copyright (c) 2018 Semihalf.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "tpm20.h"

/*
 * CRB register space as defined in
 * TCG_PC_Client_Platform_TPM_Profile_PTP_2.0_r1.03_v22
 */
#define TPM_LOC_STATE			0x0
#define TPM_LOC_CTRL			0x8
#define TPM_LOC_STS				0xC
#define TPM_CRB_INTF_ID			0x30
#define TPM_CRB_CTRL_EXT		0x38
#define TPM_CRB_CTRL_REQ		0x40
#define TPM_CRB_CTRL_STS		0x44
#define TPM_CRB_CTRL_CANCEL 	0x48
#define TPM_CRB_CTRL_START		0x4C
#define TPM_CRB_INT_ENABLE		0x50
#define TPM_CRB_INT_STS			0x54
#define TPM_CRB_CTRL_CMD_SIZE	0x58
#define TPM_CRB_CTRL_CMD_LADDR	0x5C
#define TPM_CRB_CTRL_CMD_HADDR	0x60
#define TPM_CRB_CTRL_RSP_SIZE	0x64
#define TPM_CRB_CTRL_RSP_ADDR	0x68
#define TPM_CRB_CTRL_RSP_HADDR	0x6c
#define TPM_CRB_DATA_BUFFER		0x80

#define TPM_LOC_STATE_ESTB			BIT(0)
#define TPM_LOC_STATE_ASSIGNED		BIT(1)
#define TPM_LOC_STATE_ACTIVE_MASK	0x9C
#define TPM_LOC_STATE_VALID			BIT(7)

#define TPM_CRB_INTF_ID_TYPE_CRB	0x1
#define TPM_CRB_INTF_ID_TYPE		0x7

#define TPM_LOC_CTRL_REQUEST		BIT(0)
#define TPM_LOC_CTRL_RELINQUISH		BIT(1)

#define TPM_CRB_CTRL_REQ_GO_READY	BIT(0)
#define TPM_CRB_CTRL_REQ_GO_IDLE	BIT(1)

#define TPM_CRB_CTRL_STS_ERR_BIT	BIT(0)
#define TPM_CRB_CTRL_STS_IDLE_BIT	BIT(1)

#define TPM_CRB_CTRL_CANCEL_CMD		BIT(0)

#define TPM_CRB_CTRL_START_CMD		BIT(0)

#define TPM_CRB_INT_ENABLE_BIT		BIT(31)

struct tpmcrb_sc {
	struct tpm_sc	base;
	bus_size_t	cmd_off;
	bus_size_t	rsp_off;
	size_t		cmd_buf_size;
	size_t		rsp_buf_size;
};


int tpmcrb_transmit(struct tpm_sc *sc, size_t size);

static int tpmcrb_acpi_probe(device_t dev);
static int tpmcrb_attach(device_t dev);
static int tpmcrb_detach(device_t dev);

static ACPI_STATUS tpmcrb_fix_buff_offsets(ACPI_RESOURCE *res, void *arg);

static bool tpm_wait_for_u32(struct tpm_sc *sc, bus_size_t off,
    uint32_t mask, uint32_t val, int32_t timeout);
static bool tpmcrb_request_locality(struct tpm_sc *sc, int locality);
static void tpmcrb_relinquish_locality(struct tpm_sc *sc);
static bool tpmcrb_cancel_cmd(struct tpm_sc *sc);

char *tpmcrb_ids[] = {"MSFT0101", NULL};

static int
tpmcrb_acpi_probe(device_t dev)
{
	int err;
	ACPI_TABLE_TPM23 *tbl;
	ACPI_STATUS status;
	err = ACPI_ID_PROBE(device_get_parent(dev), dev, tpmcrb_ids, NULL);
	if (err > 0)
		return (err);
	/*Find TPM2 Header*/
	status = AcpiGetTable(ACPI_SIG_TPM2, 1, (ACPI_TABLE_HEADER **) &tbl);
	if(ACPI_FAILURE(status) ||
	   tbl->StartMethod != TPM2_START_METHOD_CRB)
		err = ENXIO;

	device_set_desc(dev, "Trusted Platform Module 2.0, CRB mode");
	return (err);
}

static ACPI_STATUS
tpmcrb_fix_buff_offsets(ACPI_RESOURCE *res, void *arg)
{
	struct tpmcrb_sc *crb_sc;
	size_t length;
	uint32_t base_addr;

	crb_sc = (struct tpmcrb_sc *)arg;

	if (res->Type != ACPI_RESOURCE_TYPE_FIXED_MEMORY32)
		return (AE_OK);

	base_addr = res->Data.FixedMemory32.Address;
	length = res->Data.FixedMemory32.AddressLength;

	if (crb_sc->cmd_off > base_addr && crb_sc->cmd_off < base_addr + length)
		crb_sc->cmd_off -= base_addr;
	if (crb_sc->rsp_off > base_addr && crb_sc->rsp_off < base_addr + length)
		crb_sc->rsp_off -= base_addr;

	return (AE_OK);
}

static int
tpmcrb_attach(device_t dev)
{
	struct tpmcrb_sc *crb_sc;
	struct tpm_sc *sc;
	ACPI_HANDLE handle;
	ACPI_STATUS status;
	int result;

	crb_sc = device_get_softc(dev);
	sc = &crb_sc->base;
	handle = acpi_get_handle(dev);

	sc->dev = dev;

	sc->mem_rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
					     RF_ACTIVE);
	if (sc->mem_res == NULL)
		return (ENXIO);

	if(!tpmcrb_request_locality(sc, 0)) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->mem_rid, sc->mem_res);
		return (ENXIO);
	}

	/*
	 * Disable all interrupts for now, since I don't have a device that
	 * works in CRB mode and supports them.
	 */
	AND4(sc, TPM_CRB_INT_ENABLE, ~TPM_CRB_INT_ENABLE_BIT);
	sc->interrupts = false;

	/*
	 * Read addresses of Tx/Rx buffers and their sizes. Note that they
	 * can be implemented by a single buffer. Also for some reason CMD
	 * addr is stored in two 4 byte neighboring registers, whereas RSP is
	 * stored in a single 8 byte one.
	 */
#ifdef __amd64__
	crb_sc->rsp_off = RD8(sc, TPM_CRB_CTRL_RSP_ADDR);
#else
	crb_sc->rsp_off = RD4(sc, TPM_CRB_CTRL_RSP_ADDR);
	crb_sc->rsp_off |= ((uint64_t) RD4(sc, TPM_CRB_CTRL_RSP_HADDR) << 32);
#endif
	crb_sc->cmd_off = RD4(sc, TPM_CRB_CTRL_CMD_LADDR);
	crb_sc->cmd_off |= ((uint64_t) RD4(sc, TPM_CRB_CTRL_CMD_HADDR) << 32);
	crb_sc->cmd_buf_size = RD4(sc, TPM_CRB_CTRL_CMD_SIZE);
	crb_sc->rsp_buf_size = RD4(sc, TPM_CRB_CTRL_RSP_SIZE);

	tpmcrb_relinquish_locality(sc);

	/* Emulator returns address in acpi space instead of an offset */
	status = AcpiWalkResources(handle, "_CRS", tpmcrb_fix_buff_offsets,
		    (void *)crb_sc);
	if (ACPI_FAILURE(status)) {
		tpmcrb_detach(dev);
		return (ENXIO);
	}

	if (crb_sc->rsp_off == crb_sc->cmd_off) {
		/*
		 * If Tx/Rx buffers are implemented as one they have to be of
		 * same size
		 */
		if (crb_sc->cmd_buf_size != crb_sc->rsp_buf_size) {
			device_printf(sc->dev,
			    "Overlapping Tx/Rx buffers have different sizes\n");
			tpmcrb_detach(dev);
			return (ENXIO);
		}
	}

	sc->transmit = tpmcrb_transmit;

	result = tpm20_init(sc);
	if (result != 0)
		tpmcrb_detach(dev);

	return (result);
}

static int
tpmcrb_detach(device_t dev)
{
	struct tpm_sc *sc;

	sc = device_get_softc(dev);
	tpm20_release(sc);

	if (sc->mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->mem_rid, sc->mem_res);

	return (0);
}

static bool
tpm_wait_for_u32(struct tpm_sc *sc, bus_size_t off, uint32_t mask, uint32_t val,
    int32_t timeout)
{

	/* Check for condition */
	if ((RD4(sc, off) & mask) == val)
		return (true);

	while (timeout > 0) {
		if ((RD4(sc, off) & mask) == val)
			return (true);

		pause("TPM in polling mode", 1);
		timeout -= tick;
	}
	return (false);
}

static bool
tpmcrb_request_locality(struct tpm_sc *sc, int locality)
{
	uint32_t mask;

	/* Currently we only support Locality 0 */
	if (locality != 0)
		return (false);

	mask = TPM_LOC_STATE_VALID | TPM_LOC_STATE_ASSIGNED;

	OR4(sc, TPM_LOC_CTRL, TPM_LOC_CTRL_REQUEST);
	if (!tpm_wait_for_u32(sc, TPM_LOC_STATE, mask, mask, TPM_TIMEOUT_C))
		return (false);

	return (true);
}

static void
tpmcrb_relinquish_locality(struct tpm_sc *sc)
{

	OR4(sc, TPM_LOC_CTRL, TPM_LOC_CTRL_RELINQUISH);
}

static bool
tpmcrb_cancel_cmd(struct tpm_sc *sc)
{
	uint32_t mask = ~0;

	WR4(sc, TPM_CRB_CTRL_CANCEL, TPM_CRB_CTRL_CANCEL_CMD);
	if (!tpm_wait_for_u32(sc, TPM_CRB_CTRL_START,
		    mask, ~mask, TPM_TIMEOUT_B)) {
		device_printf(sc->dev,
		    "Device failed to cancel command\n");
		return (false);
	}

	WR4(sc, TPM_CRB_CTRL_CANCEL, !TPM_CRB_CTRL_CANCEL_CMD);
	return (true);
}

int
tpmcrb_transmit(struct tpm_sc *sc, size_t length)
{
	struct tpmcrb_sc *crb_sc;
	uint32_t mask, curr_cmd;
	int timeout, bytes_available;

	crb_sc = (struct tpmcrb_sc *)sc;

	sx_assert(&sc->dev_lock, SA_XLOCKED);

	if (length > crb_sc->cmd_buf_size) {
		device_printf(sc->dev,
		    "Requested transfer is bigger than buffer size\n");
		return (E2BIG);
	}

	if (RD4(sc, TPM_CRB_CTRL_STS) & TPM_CRB_CTRL_STS_ERR_BIT) {
		device_printf(sc->dev,
		    "Device has Error bit set\n");
		return (EIO);
	}
	if (!tpmcrb_request_locality(sc, 0)) {
		device_printf(sc->dev,
		    "Failed to obtain locality\n");
		return (EIO);
	}
	/* Clear cancellation bit */
	WR4(sc, TPM_CRB_CTRL_CANCEL, !TPM_CRB_CTRL_CANCEL_CMD);

	/* Switch device to idle state if necessary */
	if (!(RD4(sc, TPM_CRB_CTRL_STS) & TPM_CRB_CTRL_STS_IDLE_BIT)) {
		OR4(sc, TPM_CRB_CTRL_REQ, TPM_CRB_CTRL_REQ_GO_IDLE);

		mask = TPM_CRB_CTRL_STS_IDLE_BIT;
		if (!tpm_wait_for_u32(sc, TPM_CRB_CTRL_STS,
			    mask, mask, TPM_TIMEOUT_C)) {
			device_printf(sc->dev,
			    "Failed to transition to idle state\n");
			return (EIO);
		}
	}
	/* Switch to ready state */
	OR4(sc, TPM_CRB_CTRL_REQ, TPM_CRB_CTRL_REQ_GO_READY);

	mask = TPM_CRB_CTRL_REQ_GO_READY;
	if (!tpm_wait_for_u32(sc, TPM_CRB_CTRL_STS,
		    mask, !mask, TPM_TIMEOUT_C)) {
		device_printf(sc->dev,
		    "Failed to transition to ready state\n");
		return (EIO);
	}

	/*
	 * Calculate timeout for current command.
	 * Command code is passed in bytes 6-10.
	 */
	curr_cmd = be32toh(*(uint32_t *) (&sc->buf[6]));
	timeout = tpm20_get_timeout(curr_cmd);

	/* Send command and tell device to process it. */
	bus_write_region_stream_1(sc->mem_res, crb_sc->cmd_off,
	    sc->buf, length);
	bus_barrier(sc->mem_res, crb_sc->cmd_off,
	    length, BUS_SPACE_BARRIER_WRITE);

	WR4(sc, TPM_CRB_CTRL_START, TPM_CRB_CTRL_START_CMD);
	bus_barrier(sc->mem_res, TPM_CRB_CTRL_START,
	    4, BUS_SPACE_BARRIER_WRITE);

	mask = ~0;
	if (!tpm_wait_for_u32(sc, TPM_CRB_CTRL_START, mask, ~mask, timeout)) {
		device_printf(sc->dev,
		    "Timeout while waiting for device to process cmd\n");
		if (!tpmcrb_cancel_cmd(sc))
			return (EIO);
	}

	/* Read response header. Length is passed in bytes 2 - 6. */
	bus_read_region_stream_1(sc->mem_res, crb_sc->rsp_off,
	    sc->buf, TPM_HEADER_SIZE);
	bytes_available = be32toh(*(uint32_t *) (&sc->buf[2]));

	if (bytes_available > TPM_BUFSIZE || bytes_available < TPM_HEADER_SIZE) {
		device_printf(sc->dev,
		    "Incorrect response size: %d\n",
		    bytes_available);
		return (EIO);
	}

	bus_read_region_stream_1(sc->mem_res, crb_sc->rsp_off + TPM_HEADER_SIZE,
	      &sc->buf[TPM_HEADER_SIZE], bytes_available - TPM_HEADER_SIZE);

	OR4(sc, TPM_CRB_CTRL_REQ, TPM_CRB_CTRL_REQ_GO_IDLE);

	tpmcrb_relinquish_locality(sc);
	sc->pending_data_length = bytes_available;

	return (0);
}

/* ACPI Driver */
static device_method_t	tpmcrb_methods[] = {
	DEVMETHOD(device_probe,		tpmcrb_acpi_probe),
	DEVMETHOD(device_attach,	tpmcrb_attach),
	DEVMETHOD(device_detach,	tpmcrb_detach),
	DEVMETHOD(device_shutdown,	tpm20_shutdown),
	DEVMETHOD(device_suspend,	tpm20_suspend),
	{0, 0}
};
static driver_t	tpmcrb_driver = {
	"tpmcrb", tpmcrb_methods, sizeof(struct tpmcrb_sc),
};

devclass_t tpmcrb_devclass;
DRIVER_MODULE(tpmcrb, acpi, tpmcrb_driver, tpmcrb_devclass, 0, 0);
