/* $FreeBSD$ */
/*-
 * Copyright (c) 2007-2012 Hans Petter Selasky. All rights reserved.
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/sysctl.h>
#include <sys/time.h>

#include <libusb20.h>
#include <libusb20_desc.h>

#include <dev/usb/usb_endian.h>

#include "usbtest.h"

#include "usb_msc_test.h"

/* Command Block Wrapper */
typedef struct {
	uDWord	dCBWSignature;
#define	CBWSIGNATURE	0x43425355
	uDWord	dCBWTag;
	uDWord	dCBWDataTransferLength;
	uByte	bCBWFlags;
#define	CBWFLAGS_OUT	0x00
#define	CBWFLAGS_IN	0x80
	uByte	bCBWLUN;
	uByte	bCDBLength;
#define	CBWCDBLENGTH	16
	uByte	CBWCDB[CBWCDBLENGTH];
} umass_bbb_cbw_t;

#define	UMASS_BBB_CBW_SIZE	31

/* Command Status Wrapper */
typedef struct {
	uDWord	dCSWSignature;
#define	CSWSIGNATURE	0x53425355
#define	CSWSIGNATURE_IMAGINATION_DBX1	0x43425355
#define	CSWSIGNATURE_OLYMPUS_C1	0x55425355
	uDWord	dCSWTag;
	uDWord	dCSWDataResidue;
	uByte	bCSWStatus;
#define	CSWSTATUS_GOOD	0x0
#define	CSWSTATUS_FAILED	0x1
#define	CSWSTATUS_PHASE	0x2
} umass_bbb_csw_t;

#define	UMASS_BBB_CSW_SIZE	13

#define	SC_READ_6			0x08
#define	SC_READ_10			0x28
#define	SC_READ_12			0xa8
#define	SC_WRITE_6			0x0a
#define	SC_WRITE_10			0x2a
#define	SC_WRITE_12			0xaa

static struct stats {
	uint64_t xfer_error;
	uint64_t xfer_success;
	uint64_t xfer_reset;
	uint64_t xfer_rx_bytes;
	uint64_t xfer_tx_bytes;
	uint64_t data_error;
}	stats;

static uint32_t xfer_current_id;
static uint32_t xfer_wrapper_sig;
static uint32_t block_size = 512;

static struct libusb20_transfer *xfer_in;
static struct libusb20_transfer *xfer_out;
static struct libusb20_device *usb_pdev;
static uint8_t usb_iface;
static int sense_recurse;

/*
 * SCSI commands sniffed off the wire - LUN maybe needs to be
 * adjusted!  Refer to "dev/usb/storage/ustorage_fs.c" for more
 * information.
 */
static uint8_t mode_sense_6[0x6] = {0x1a, 0, 0x3f, 0, 0x0c};
static uint8_t read_capacity[0xA] = {0x25,};
static uint8_t request_sense[0xC] = {0x03, 0, 0, 0, 0x12};
static uint8_t test_unit_ready[0x6] = {0};
static uint8_t mode_page_inquiry[0x6] = {0x12, 1, 0x80, 0, 0xff, 0};
static uint8_t request_invalid[0xC] = {0xEA, 0, 0, 0, 0};
static uint8_t prevent_removal[0x6] = {0x1E, 0, 0, 0, 1};
static uint8_t read_toc[0xA] = {0x43, 0x02, 0, 0, 0, 0xAA, 0, 0x0C};

#define	TIMEOUT_FILTER(x) (x)

static void usb_request_sense(uint8_t lun);

static void
do_msc_reset(uint8_t lun)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED setup;

	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &setup);

	setup.bmRequestType = LIBUSB20_REQUEST_TYPE_CLASS |
	    LIBUSB20_RECIPIENT_INTERFACE;
	setup.bRequest = 0xFF;		/* BBB reset */
	setup.wValue = 0;
	setup.wIndex = usb_iface;
	setup.wLength = 0;

	if (libusb20_dev_request_sync(usb_pdev, &setup, NULL, NULL, 5000, 0)) {
		printf("ERROR: %s\n", __FUNCTION__);
		stats.xfer_error++;
	}
	libusb20_tr_clear_stall_sync(xfer_in);
	libusb20_tr_clear_stall_sync(xfer_out);

	stats.xfer_reset++;

	usb_request_sense(lun);
}

static uint8_t
do_msc_cmd(uint8_t *pcmd, uint8_t cmdlen, void *pdata, uint32_t datalen,
    uint8_t isread, uint8_t isshort, uint8_t lun, uint8_t flags)
{
	umass_bbb_cbw_t cbw;
	umass_bbb_csw_t csw;
	struct libusb20_transfer *xfer_io;
	uint32_t actlen;
	uint32_t timeout;
	int error;

	memset(&cbw, 0, sizeof(cbw));

	USETDW(cbw.dCBWSignature, xfer_wrapper_sig);
	USETDW(cbw.dCBWTag, xfer_current_id);
	xfer_current_id++;
	USETDW(cbw.dCBWDataTransferLength, datalen);
	cbw.bCBWFlags = (isread ? CBWFLAGS_IN : CBWFLAGS_OUT);
	cbw.bCBWLUN = lun;
	cbw.bCDBLength = cmdlen;
	bcopy(pcmd, cbw.CBWCDB, cmdlen);

	actlen = 0;

	timeout = ((datalen + 299999) / 300000) * 1000;
	timeout += 5000;

	if ((error = libusb20_tr_bulk_intr_sync(xfer_out,
	    &cbw, sizeof(cbw), &actlen, TIMEOUT_FILTER(1000)))) {
		printf("ERROR: CBW reception: %d\n", error);
		do_msc_reset(lun);
		return (1);
	}
	if (actlen != sizeof(cbw)) {
		printf("ERROR: CBW length: %d != %d\n",
		    actlen, (int)sizeof(cbw));
		do_msc_reset(lun);
		return (1);
	}
	if (flags & 1)
		datalen /= 2;

	if (datalen != 0) {
		xfer_io = isread ? xfer_in : xfer_out;

		if ((error = libusb20_tr_bulk_intr_sync(xfer_io,
		    pdata, datalen, &actlen, TIMEOUT_FILTER(timeout)))) {
			printf("ERROR: Data transfer: %d\n", error);
			do_msc_reset(lun);
			return (1);
		}
		if ((actlen != datalen) && (!isshort)) {
			printf("ERROR: Short data: %d of %d bytes\n",
			    actlen, datalen);
			do_msc_reset(lun);
			return (1);
		}
	}
	actlen = 0;
	timeout = 8;

	do {
		error = libusb20_tr_bulk_intr_sync(xfer_in, &csw,
		    sizeof(csw), &actlen, TIMEOUT_FILTER(1000));
		if (error) {
			if (error == LIBUSB20_TRANSFER_TIMED_OUT) {
				printf("TIMEOUT: Trying to get CSW again. "
				    "%d tries left.\n", timeout);
			} else {
				break;
			}
		} else {
			break;
		}
	} while (--timeout);

	if (error) {
		libusb20_tr_clear_stall_sync(xfer_in);
		error = libusb20_tr_bulk_intr_sync(xfer_in, &csw,
		    sizeof(csw), &actlen, TIMEOUT_FILTER(1000));
		if (error) {
			libusb20_tr_clear_stall_sync(xfer_in);
			printf("ERROR: Could not read CSW: Stalled or "
			    "timeout (%d).\n", error);
			do_msc_reset(lun);
			return (1);
		}
	}
	if (UGETDW(csw.dCSWSignature) != CSWSIGNATURE) {
		printf("ERROR: Wrong CSW signature\n");
		do_msc_reset(lun);
		return (1);
	}
	if (actlen != sizeof(csw)) {
		printf("ERROR: Wrong CSW length: %d != %d\n",
		    actlen, (int)sizeof(csw));
		do_msc_reset(lun);
		return (1);
	}
	if (csw.bCSWStatus != 0) {
		printf("ERROR: CSW status: %d\n", (int)csw.bCSWStatus);
		return (1);
	} else {
		stats.xfer_success++;
		return (0);
	}
}

static void
do_msc_shorter_cmd(uint8_t lun)
{
	uint8_t buffer[sizeof(umass_bbb_cbw_t)];
	int actlen;
	int error;
	int x;

	memset(buffer, 0, sizeof(buffer));

	for (x = 0; x != (sizeof(buffer) - 1); x++) {
		error = libusb20_tr_bulk_intr_sync(xfer_out,
		    buffer, x, &actlen, 250);

		printf("Sent short %d of %d bytes wrapper block, "
		    "status = %d\n", x, (int)(sizeof(buffer) - 1),
		    error);

		do_msc_reset(lun);

		if (error != 0) {
			printf("ERROR: Too short command wrapper "
			    "was not accepted\n");
			stats.xfer_error++;
			break;
		}
	}
}

static uint8_t
do_read_10(uint32_t lba, uint32_t len, void *buf, uint8_t lun)
{
	static uint8_t cmd[10];
	uint8_t retval;

	cmd[0] = SC_READ_10;

	len /= block_size;

	cmd[2] = lba >> 24;
	cmd[3] = lba >> 16;
	cmd[4] = lba >> 8;
	cmd[5] = lba >> 0;

	cmd[7] = len >> 8;
	cmd[8] = len;

	retval = do_msc_cmd(cmd, 10, buf, len * block_size, 1, 0, lun, 0);

	if (retval) {
		printf("ERROR: %s\n", __FUNCTION__);
		stats.xfer_error++;
	}
	return (retval);
}

static uint8_t
do_write_10(uint32_t lba, uint32_t len, void *buf, uint8_t lun)
{
	static uint8_t cmd[10];
	uint8_t retval;
	uint8_t abort;

	cmd[0] = SC_WRITE_10;

	abort = len & 1;

	len /= block_size;

	cmd[2] = lba >> 24;
	cmd[3] = lba >> 16;
	cmd[4] = lba >> 8;
	cmd[5] = lba >> 0;

	cmd[7] = len >> 8;
	cmd[8] = len;

	retval = do_msc_cmd(cmd, 10, buf, (len * block_size), 0, 0, lun, abort);

	if (retval) {
		printf("ERROR: %s\n", __FUNCTION__);
		stats.xfer_error++;
	}
	return (retval);
}

static void
do_io_test(struct usb_msc_params *p, uint8_t lun, uint32_t lba_max,
    uint8_t *buffer, uint8_t *reference)
{
	uint32_t io_offset;
	uint32_t io_size;
	uint32_t temp;
	uint8_t do_read;
	uint8_t retval;

	switch (p->io_mode) {
	case USB_MSC_IO_MODE_WRITE_ONLY:
		do_read = 0;
		break;
	case USB_MSC_IO_MODE_READ_WRITE:
		do_read = (usb_ts_rand_noise() & 1);
		break;
	default:
		do_read = 1;
		break;
	}

	switch (p->io_offset) {
	case USB_MSC_IO_OFF_RANDOM:
		io_offset = usb_ts_rand_noise();
		break;
	default:
		io_offset = 0;
		break;
	}

	switch (p->io_delay) {
	case USB_MSC_IO_DELAY_RANDOM_10MS:
		usleep(((uint32_t)usb_ts_rand_noise()) % 10000U);
		break;
	case USB_MSC_IO_DELAY_RANDOM_100MS:
		usleep(((uint32_t)usb_ts_rand_noise()) % 100000U);
		break;
	case USB_MSC_IO_DELAY_FIXED_10MS:
		usleep(10000);
		break;
	case USB_MSC_IO_DELAY_FIXED_100MS:
		usleep(100000);
		break;
	default:
		break;
	}

	switch (p->io_size) {
	case USB_MSC_IO_SIZE_RANDOM:
		io_size = ((uint32_t)usb_ts_rand_noise()) & 65535U;
		break;
	case USB_MSC_IO_SIZE_INCREASING:
		io_size = (xfer_current_id & 65535U);
		break;
	case USB_MSC_IO_SIZE_FIXED_1BLK:
		io_size = 1;
		break;
	case USB_MSC_IO_SIZE_FIXED_2BLK:
		io_size = 2;
		break;
	case USB_MSC_IO_SIZE_FIXED_4BLK:
		io_size = 4;
		break;
	case USB_MSC_IO_SIZE_FIXED_8BLK:
		io_size = 8;
		break;
	case USB_MSC_IO_SIZE_FIXED_16BLK:
		io_size = 16;
		break;
	case USB_MSC_IO_SIZE_FIXED_32BLK:
		io_size = 32;
		break;
	case USB_MSC_IO_SIZE_FIXED_64BLK:
		io_size = 64;
		break;
	case USB_MSC_IO_SIZE_FIXED_128BLK:
		io_size = 128;
		break;
	case USB_MSC_IO_SIZE_FIXED_256BLK:
		io_size = 256;
		break;
	case USB_MSC_IO_SIZE_FIXED_512BLK:
		io_size = 512;
		break;
	case USB_MSC_IO_SIZE_FIXED_1024BLK:
		io_size = 1024;
		break;
	default:
		io_size = 1;
		break;
	}

	if (io_size == 0)
		io_size = 1;

	io_offset %= lba_max;

	temp = (lba_max - io_offset);

	if (io_size > temp)
		io_size = temp;

	if (do_read) {
		retval = do_read_10(io_offset, io_size * block_size,
		    buffer + (io_offset * block_size), lun);

		if (retval == 0) {
			if (bcmp(buffer + (io_offset * block_size),
			    reference + (io_offset * block_size),
			    io_size * block_size)) {
				printf("ERROR: Data comparison failure\n");
				stats.data_error++;
				retval = 1;
			}
		}
		stats.xfer_rx_bytes += (io_size * block_size);

	} else {

		retval = do_write_10(io_offset, io_size * block_size,
		    reference + (io_offset * block_size), lun);

		stats.xfer_tx_bytes += (io_size * block_size);
	}

	if ((stats.xfer_error + stats.data_error +
	    stats.xfer_reset) >= p->max_errors) {
		printf("Maximum number of errors exceeded\n");
		p->done = 1;
	}
}

static void
usb_request_sense(uint8_t lun)
{
	uint8_t dummy_buf[255];

	if (sense_recurse)
		return;

	sense_recurse++;

	do_msc_cmd(request_sense, sizeof(request_sense),
	    dummy_buf, 255, 1, 1, lun, 0);

	sense_recurse--;
}

static void
usb_msc_test(struct usb_msc_params *p)
{
	struct stats last_stat;
	struct timeval sub_tv;
	struct timeval ref_tv;
	struct timeval res_tv;
	uint8_t *buffer = NULL;
	uint8_t *reference = NULL;
	uint32_t dummy_buf[65536 / 4];
	uint32_t lba_max;
	uint32_t x;
	uint32_t y;
	uint32_t capacity_lba;
	uint32_t capacity_bs;
	time_t last_sec;
	uint8_t lun;
	int tries;

	memset(&last_stat, 0, sizeof(last_stat));

	switch (p->io_lun) {
	case USB_MSC_IO_LUN_0:
		lun = 0;
		break;
	case USB_MSC_IO_LUN_1:
		lun = 1;
		break;
	case USB_MSC_IO_LUN_2:
		lun = 2;
		break;
	case USB_MSC_IO_LUN_3:
		lun = 3;
		break;
	default:
		lun = 0;
		break;
	}

	p->done = 0;

	sense_recurse = p->try_sense_on_error ? 0 : 1;

	printf("Resetting device ...\n");

	do_msc_reset(lun);

	printf("Testing SCSI commands ...\n");

	if (p->try_all_lun) {
		printf("Requesting sense from LUN 0..255 ... ");
		for (x = y = 0; x != 256; x++) {
			if (do_msc_cmd(mode_sense_6, sizeof(mode_sense_6),
			    dummy_buf, 255, 1, 1, x, 0))
				y++;

			if (libusb20_dev_check_connected(usb_pdev) != 0) {
				printf(" disconnect ");
				break;
			}
		}
		printf("Passed=%d, Failed=%d\n", 256 - y, y);
	}
	do_msc_cmd(mode_sense_6, sizeof(mode_sense_6),
	    dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(request_sense, sizeof(request_sense),
	    dummy_buf, 255, 1, 1, lun, 0);

	for (tries = 0; tries != 4; tries++) {

		memset(dummy_buf, 0, sizeof(dummy_buf));

		if (do_msc_cmd(read_capacity, sizeof(read_capacity),
		    dummy_buf, 255, 1, 1, lun, 0) != 0) {
			printf("Cannot read disk capacity (%u / 4)\n", tries);
			if (tries == 3)
				return;
			usleep(50000);
			continue;
		} else {
			break;
		}
	}

	capacity_lba = be32toh(dummy_buf[0]);
	capacity_bs = be32toh(dummy_buf[1]);

	printf("Disk reports a capacity of LBA=%u and BS=%u\n",
	    capacity_lba, capacity_bs);

	block_size = capacity_bs;

	if (capacity_bs > 65535) {
		printf("Blocksize is too big\n");
		return;
	}
	if (capacity_bs < 1) {
		printf("Blocksize is too small\n");
		return;
	}
	if (capacity_bs != 512)
		printf("INFO: Blocksize is not 512 bytes\n");

	if (p->try_shorter_wrapper_block) {
		printf("Trying too short command wrapper:\n");
		do_msc_shorter_cmd(lun);
	}

	if (p->try_invalid_scsi_command) {
		int status;

		for (tries = 0; tries != 4; tries++) {

			printf("Trying invalid SCSI command: ");

			status = do_msc_cmd(request_invalid,
			    sizeof(request_invalid), dummy_buf,
			    255, 1, 1, lun, 0);

			printf("Result%s as expected\n", status ? "" : " NOT");

			usleep(50000);
		}
	}
	if (p->try_invalid_wrapper_block) {
		int status;

		for (tries = 0; tries != 4; tries++) {

			printf("Trying invalid USB wrapper block signature: ");

			xfer_wrapper_sig = 0x55663322;

			status = do_msc_cmd(read_capacity,
			    sizeof(read_capacity), dummy_buf,
			    255, 1, 1, lun, 0);

			printf("Result%s as expected\n", status ? "" : " NOT");

			xfer_wrapper_sig = CBWSIGNATURE;

			usleep(50000);
		}
	}
	do_msc_cmd(request_sense, sizeof(request_sense), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(read_capacity, sizeof(read_capacity), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(request_sense, sizeof(request_sense), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(request_sense, sizeof(request_sense), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(request_sense, sizeof(request_sense), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(request_sense, sizeof(request_sense), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(test_unit_ready, sizeof(test_unit_ready), 0, 0, 1, 1, lun, 0);
	do_msc_cmd(read_capacity, sizeof(read_capacity), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(mode_page_inquiry, sizeof(mode_page_inquiry), dummy_buf, 255, 1, 1, lun, 0);
	do_msc_cmd(mode_page_inquiry, sizeof(mode_page_inquiry), dummy_buf, 255, 1, 1, lun, 0);

	if (do_msc_cmd(prevent_removal, sizeof(prevent_removal),
	    0, 0, 1, 1, lun, 0)) {
		printf("INFO: Prevent medium removal failed\n");
	}
	if (do_msc_cmd(read_toc, sizeof(read_toc),
	    dummy_buf, 255, 1, 1, lun, 0)) {
		printf("INFO: Read Table Of Content failed\n");
	}
	if (p->try_last_lba) {

		for (y = 0, x = (1UL << 31); x; x >>= 1) {
			if (do_read_10(x | y, block_size, dummy_buf, lun) == 0)
				y |= x;
		}

		printf("Highest readable LBA: %u (%s), "
		    "Capacity is %u MBytes\n", y,
		    (capacity_lba != y) ? "WRONG" : "OK",
		    (int)((((uint64_t)(y) * (uint64_t)block_size) +
		    (uint64_t)block_size) / 1000000ULL));
	} else {

		y = capacity_lba;

		printf("Highest readable LBA: %u (not "
		    "verified), Capacity is %u MBytes\n", y,
		    (int)((((uint64_t)(y) * (uint64_t)block_size) +
		    (uint64_t)block_size) / 1000000ULL));
	}

	if (y != 0xFFFFFFFFU)
		y++;

	lba_max = y;

	switch (p->io_area) {
	case USB_MSC_IO_AREA_1MB:
		lba_max = 1024;
		break;
	case USB_MSC_IO_AREA_16MB:
		lba_max = 1024 * 16;
		break;
	case USB_MSC_IO_AREA_256MB:
		lba_max = 1024 * 256;
		break;
	case USB_MSC_IO_AREA_COMPLETE:
	default:
		break;
	}

	if (lba_max > 65535)
		lba_max = 65535;

	printf("Highest testable LBA: %u\n", (int)lba_max);

	buffer = malloc(block_size * lba_max);
	if (buffer == NULL) {
		printf("ERROR: Could not allocate memory\n");
		goto fail;
	}
	reference = malloc(block_size * lba_max);
	if (reference == NULL) {
		printf("ERROR: Could not allocate memory\n");
		goto fail;
	}
retry_read_init:

	printf("Setting up initial data pattern, "
	    "LBA limit = %u ... ", lba_max);

	switch (p->io_mode) {
	case USB_MSC_IO_MODE_WRITE_ONCE_READ_ONLY:
	case USB_MSC_IO_MODE_WRITE_ONLY:
	case USB_MSC_IO_MODE_READ_WRITE:

		switch (p->io_pattern) {
		case USB_MSC_IO_PATTERN_FIXED:
			for (x = 0; x != (block_size * lba_max); x += 8) {
				reference[x + 0] = x >> 24;
				reference[x + 1] = x >> 16;
				reference[x + 2] = x >> 8;
				reference[x + 3] = x >> 0;
				reference[x + 4] = 0xFF;
				reference[x + 5] = 0x00;
				reference[x + 6] = 0xFF;
				reference[x + 7] = 0x00;
			}
			if (do_write_10(0, lba_max * block_size,
			    reference, lun)) {
				printf("FAILED\n");
				lba_max /= 2;
				if (lba_max)
					goto retry_read_init;
				goto fail;
			}
			printf("SUCCESS\n");
			break;
		case USB_MSC_IO_PATTERN_RANDOM:
			for (x = 0; x != (block_size * lba_max); x++) {
				reference[x] = usb_ts_rand_noise() % 255U;
			}
			if (do_write_10(0, lba_max * block_size,
			    reference, lun)) {
				printf("FAILED\n");
				lba_max /= 2;
				if (lba_max)
					goto retry_read_init;
				goto fail;
			}
			printf("SUCCESS\n");
			break;
		default:
			if (do_read_10(0, lba_max * block_size,
			    reference, lun)) {
				printf("FAILED\n");
				lba_max /= 2;
				if (lba_max)
					goto retry_read_init;
				goto fail;
			}
			printf("SUCCESS\n");
			break;
		}
		break;

	default:
		if (do_read_10(0, lba_max * block_size, reference, lun)) {
			printf("FAILED\n");
			lba_max /= 2;
			if (lba_max)
				goto retry_read_init;
			goto fail;
		}
		printf("SUCCESS\n");
		break;
	}


	if (p->try_abort_data_write) {
		if (do_write_10(0, (2 * block_size) | 1, reference, lun))
			printf("Aborted data write failed (OK)!\n");
		else
			printf("Aborted data write did not fail (ERROR)!\n");

		if (do_read_10(0, (2 * block_size), reference, lun))
			printf("Post-aborted data read failed (ERROR)\n");
		else
			printf("Post-aborted data read success (OK)!\n");
	}
	printf("Starting test ...\n");

	gettimeofday(&ref_tv, 0);

	last_sec = ref_tv.tv_sec;

	printf("\n");

	while (1) {

		gettimeofday(&sub_tv, 0);

		if (last_sec != sub_tv.tv_sec) {

			printf("STATUS: ID=%u, RX=%u bytes/sec, "
			    "TX=%u bytes/sec, ERR=%u, RST=%u, DERR=%u\n",
			    (int)xfer_current_id,
			    (int)(stats.xfer_rx_bytes -
			    last_stat.xfer_rx_bytes),
			    (int)(stats.xfer_tx_bytes -
			    last_stat.xfer_tx_bytes),
			    (int)(stats.xfer_error),
			    (int)(stats.xfer_reset),
			    (int)(stats.data_error));

			fflush(stdout);

			last_sec = sub_tv.tv_sec;
			last_stat = stats;
		}
		timersub(&sub_tv, &ref_tv, &res_tv);

		if ((res_tv.tv_sec < 0) || (res_tv.tv_sec >= (int)p->duration))
			break;

		do_io_test(p, lun, lba_max, buffer, reference);

		if (libusb20_dev_check_connected(usb_pdev) != 0) {
			printf("Device disconnected\n");
			break;
		}
		if (p->done) {
			printf("Maximum number of errors exceeded\n");
			break;
		}
	}

	printf("\nTest done!\n");

fail:
	if (buffer)
		free(buffer);
	if (reference)
		free(reference);
}

void
show_host_device_selection(uint8_t level, uint16_t *pvid, uint16_t *ppid)
{
	struct libusb20_backend *pbe;
	struct libusb20_device *pdev;
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;

	uint16_t vid[USB_DEVICES_MAX];
	uint16_t pid[USB_DEVICES_MAX];

	int index;
	int sel;

	const char *ptr;

top:
	pbe = libusb20_be_alloc_default();
	pdev = NULL;
	index = 0;

	printf("\n[] Select USB device:\n");

	while ((pdev = libusb20_be_device_foreach(pbe, pdev))) {

		if (libusb20_dev_get_mode(pdev) != LIBUSB20_MODE_HOST)
			continue;

		if (index < USB_DEVICES_MAX) {
			ddesc = libusb20_dev_get_device_desc(pdev);
			ptr = libusb20_dev_get_desc(pdev);
			printf("%s%d) %s\n", indent[level], index, ptr);
			vid[index] = ddesc->idVendor;
			pid[index] = ddesc->idProduct;
			index++;
		} else {
			break;
		}
	}

	printf("%sr) Refresh device list\n", indent[level]);
	printf("%sx) Return to previous menu\n", indent[level]);

	/* release data */
	libusb20_be_free(pbe);

	sel = get_integer();

	if (sel == -2)
		goto top;

	if ((sel < 0) || (sel >= index)) {
		*pvid = 0;
		*ppid = 0;
		return;
	}
	*pvid = vid[sel];
	*ppid = pid[sel];
}

struct libusb20_device *
find_usb_device(uint16_t vid, uint16_t pid)
{
	struct libusb20_backend *pbe = libusb20_be_alloc_default();
	struct libusb20_device *pdev = NULL;
	struct LIBUSB20_DEVICE_DESC_DECODED *ddesc;

	while ((pdev = libusb20_be_device_foreach(pbe, pdev))) {

		if (libusb20_dev_get_mode(pdev) != LIBUSB20_MODE_HOST)
			continue;

		ddesc = libusb20_dev_get_device_desc(pdev);

		if ((vid == ddesc->idVendor) &&
		    (pid == ddesc->idProduct)) {
			libusb20_be_dequeue_device(pbe, pdev);
			break;
		}
	}

	/* release data */
	libusb20_be_free(pbe);

	return (pdev);
}

void
find_usb_endpoints(struct libusb20_device *pdev, uint8_t class,
    uint8_t subclass, uint8_t protocol, uint8_t alt_setting,
    uint8_t *pif, uint8_t *in_ep, uint8_t *out_ep, uint8_t next_if)
{
	struct libusb20_config *pcfg;
	struct libusb20_interface *iface;
	struct libusb20_endpoint *ep;
	uint8_t x;
	uint8_t y;
	uint8_t z;

	*in_ep = 0;
	*out_ep = 0;
	*pif = 0;

	pcfg = libusb20_dev_alloc_config(pdev,
	    libusb20_dev_get_config_index(pdev));

	if (pcfg == NULL)
		return;

	for (x = 0; x != pcfg->num_interface; x++) {

		y = alt_setting;

		iface = (pcfg->interface + x);

		if ((iface->desc.bInterfaceClass == class) &&
		    (iface->desc.bInterfaceSubClass == subclass ||
		    subclass == 255) &&
		    (iface->desc.bInterfaceProtocol == protocol ||
		    protocol == 255)) {

			if (next_if) {
				x++;
				if (x == pcfg->num_interface)
					break;
				iface = (pcfg->interface + x);
			}
			*pif = x;

			for (z = 0; z != iface->num_endpoints; z++) {
				ep = iface->endpoints + z;

				/* BULK only */
				if ((ep->desc.bmAttributes & 3) != 2)
					continue;

				if (ep->desc.bEndpointAddress & 0x80)
					*in_ep = ep->desc.bEndpointAddress;
				else
					*out_ep = ep->desc.bEndpointAddress;
			}
			break;
		}
	}

	free(pcfg);
}

static void
exec_host_msc_test(struct usb_msc_params *p, uint16_t vid, uint16_t pid)
{
	struct libusb20_device *pdev;

	uint8_t in_ep;
	uint8_t out_ep;
	uint8_t iface;

	int error;

	memset(&stats, 0, sizeof(stats));

	xfer_current_id = 0;
	xfer_wrapper_sig = CBWSIGNATURE;

	pdev = find_usb_device(vid, pid);
	if (pdev == NULL) {
		printf("USB device not found\n");
		return;
	}
	find_usb_endpoints(pdev, 8, 6, 0x50, 0, &iface, &in_ep, &out_ep, 0);

	if ((in_ep == 0) || (out_ep == 0)) {
		printf("Could not find USB endpoints\n");
		libusb20_dev_free(pdev);
		return;
	}
	printf("Attaching to: %s @ iface %d\n",
	    libusb20_dev_get_desc(pdev), iface);

	if (libusb20_dev_open(pdev, 2)) {
		printf("Could not open USB device\n");
		libusb20_dev_free(pdev);
		return;
	}
	if (libusb20_dev_detach_kernel_driver(pdev, iface)) {
		printf("WARNING: Could not detach kernel driver\n");
	}
	xfer_in = libusb20_tr_get_pointer(pdev, 0);
	error = libusb20_tr_open(xfer_in, 65536, 1, in_ep);
	if (error) {
		printf("Could not open USB endpoint %d\n", in_ep);
		libusb20_dev_free(pdev);
		return;
	}
	xfer_out = libusb20_tr_get_pointer(pdev, 1);
	error = libusb20_tr_open(xfer_out, 65536, 1, out_ep);
	if (error) {
		printf("Could not open USB endpoint %d\n", out_ep);
		libusb20_dev_free(pdev);
		return;
	}
	usb_pdev = pdev;
	usb_iface = iface;

	usb_msc_test(p);

	libusb20_dev_free(pdev);
}

static void
set_defaults(struct usb_msc_params *p)
{
	memset(p, 0, sizeof(*p));

	p->duration = 60;		/* seconds */
	p->try_invalid_scsi_command = 1;
	p->try_invalid_wrapper_block = 1;
	p->try_last_lba = 1;
	p->max_errors = -1;
}

static const char *
get_io_mode(const struct usb_msc_params *p)
{
	;				/* indent fix */
	switch (p->io_mode) {
	case USB_MSC_IO_MODE_READ_ONLY:
		return ("Read Only");
	case USB_MSC_IO_MODE_WRITE_ONCE_READ_ONLY:
		return ("Write Once, Read Only");
	case USB_MSC_IO_MODE_WRITE_ONLY:
		return ("Write Only");
	case USB_MSC_IO_MODE_READ_WRITE:
		return ("Read and Write");
	default:
		return ("Unknown");
	}
}

static const char *
get_io_pattern(const struct usb_msc_params *p)
{
	;				/* indent fix */
	switch (p->io_pattern) {
	case USB_MSC_IO_PATTERN_FIXED:
		return ("Fixed");
	case USB_MSC_IO_PATTERN_RANDOM:
		return ("Random");
	case USB_MSC_IO_PATTERN_PRESERVE:
		return ("Preserve");
	default:
		return ("Unknown");
	}
}

static const char *
get_io_size(const struct usb_msc_params *p)
{
	;				/* indent fix */
	switch (p->io_size) {
	case USB_MSC_IO_SIZE_RANDOM:
		return ("Random");
	case USB_MSC_IO_SIZE_INCREASING:
		return ("Increasing");
	case USB_MSC_IO_SIZE_FIXED_1BLK:
		return ("Single block");
	case USB_MSC_IO_SIZE_FIXED_2BLK:
		return ("2 blocks");
	case USB_MSC_IO_SIZE_FIXED_4BLK:
		return ("4 blocks");
	case USB_MSC_IO_SIZE_FIXED_8BLK:
		return ("8 blocks");
	case USB_MSC_IO_SIZE_FIXED_16BLK:
		return ("16 blocks");
	case USB_MSC_IO_SIZE_FIXED_32BLK:
		return ("32 blocks");
	case USB_MSC_IO_SIZE_FIXED_64BLK:
		return ("64 blocks");
	case USB_MSC_IO_SIZE_FIXED_128BLK:
		return ("128 blocks");
	case USB_MSC_IO_SIZE_FIXED_256BLK:
		return ("256 blocks");
	case USB_MSC_IO_SIZE_FIXED_512BLK:
		return ("512 blocks");
	case USB_MSC_IO_SIZE_FIXED_1024BLK:
		return ("1024 blocks");
	default:
		return ("Unknown");
	}
}

static const char *
get_io_delay(const struct usb_msc_params *p)
{
	;				/* indent fix */
	switch (p->io_delay) {
	case USB_MSC_IO_DELAY_NONE:
		return ("None");
	case USB_MSC_IO_DELAY_RANDOM_10MS:
		return ("Random 10ms");
	case USB_MSC_IO_DELAY_RANDOM_100MS:
		return ("Random 100ms");
	case USB_MSC_IO_DELAY_FIXED_10MS:
		return ("Fixed 10ms");
	case USB_MSC_IO_DELAY_FIXED_100MS:
		return ("Fixed 100ms");
	default:
		return ("Unknown");
	}
}

static const char *
get_io_offset(const struct usb_msc_params *p)
{
	;				/* indent fix */
	switch (p->io_offset) {
	case USB_MSC_IO_OFF_START_OF_DISK:
		return ("Start Of Disk");
	case USB_MSC_IO_OFF_RANDOM:
		return ("Random Offset");
	default:
		return ("Unknown");
	}
}

static const char *
get_io_area(const struct usb_msc_params *p)
{
	;				/* indent fix */
	switch (p->io_area) {
	case USB_MSC_IO_AREA_COMPLETE:
		return ("Complete Disk");
	case USB_MSC_IO_AREA_1MB:
		return ("First MegaByte");
	case USB_MSC_IO_AREA_16MB:
		return ("First 16 MegaBytes");
	case USB_MSC_IO_AREA_256MB:
		return ("First 256 MegaBytes");
	default:
		return ("Unknown");
	}
}

void
show_host_msc_test(uint8_t level, uint16_t vid,
    uint16_t pid, uint32_t duration)
{
	struct usb_msc_params params;
	uint8_t retval;

	set_defaults(&params);

	params.duration = duration;

	while (1) {

		retval = usb_ts_show_menu(level,
		    "Mass Storage Test Parameters",
		    " 1) Toggle I/O mode: <%s>\n"
		    " 2) Toggle I/O size: <%s>\n"
		    " 3) Toggle I/O delay: <%s>\n"
		    " 4) Toggle I/O offset: <%s>\n"
		    " 5) Toggle I/O area: <%s>\n"
		    " 6) Toggle I/O pattern: <%s>\n"
		    " 7) Toggle try invalid SCSI command: <%s>\n"
		    " 8) Toggle try invalid wrapper block: <%s>\n"
		    " 9) Toggle try invalid MaxPacketSize: <%s>\n"
		    "10) Toggle try last Logical Block Address: <%s>\n"
		    "11) Toggle I/O lun: <%d>\n"
		    "12) Set maximum number of errors: <%d>\n"
		    "13) Set test duration: <%d> seconds\n"
		    "14) Toggle try aborted write transfer: <%s>\n"
		    "15) Toggle request sense on error: <%s>\n"
		    "16) Toggle try all LUN: <%s>\n"
		    "17) Toggle try too short wrapper block: <%s>\n"
		    "20) Reset parameters\n"
		    "30) Start test (VID=0x%04x, PID=0x%04x)\n"
		    "40) Select another device\n"
		    " x) Return to previous menu \n",
		    get_io_mode(&params),
		    get_io_size(&params),
		    get_io_delay(&params),
		    get_io_offset(&params),
		    get_io_area(&params),
		    get_io_pattern(&params),
		    (params.try_invalid_scsi_command ? "YES" : "NO"),
		    (params.try_invalid_wrapper_block ? "YES" : "NO"),
		    (params.try_invalid_max_packet_size ? "YES" : "NO"),
		    (params.try_last_lba ? "YES" : "NO"),
		    params.io_lun,
		    (int)params.max_errors,
		    (int)params.duration,
		    (params.try_abort_data_write ? "YES" : "NO"),
		    (params.try_sense_on_error ? "YES" : "NO"),
		    (params.try_all_lun ? "YES" : "NO"),
		    (params.try_shorter_wrapper_block ? "YES" : "NO"),
		    vid, pid);
		switch (retval) {
		case 0:
			break;
		case 1:
			params.io_mode++;
			params.io_mode %= USB_MSC_IO_MODE_MAX;
			break;
		case 2:
			params.io_size++;
			params.io_size %= USB_MSC_IO_SIZE_MAX;
			break;
		case 3:
			params.io_delay++;
			params.io_delay %= USB_MSC_IO_DELAY_MAX;
			break;
		case 4:
			params.io_offset++;
			params.io_offset %= USB_MSC_IO_OFF_MAX;
			break;
		case 5:
			params.io_area++;
			params.io_area %= USB_MSC_IO_AREA_MAX;
			break;
		case 6:
			params.io_pattern++;
			params.io_pattern %= USB_MSC_IO_PATTERN_MAX;
			break;
		case 7:
			params.try_invalid_scsi_command ^= 1;
			break;
		case 8:
			params.try_invalid_wrapper_block ^= 1;
			break;
		case 9:
			params.try_invalid_max_packet_size ^= 1;
			break;
		case 10:
			params.try_last_lba ^= 1;
			break;
		case 11:
			params.io_lun++;
			params.io_lun %= USB_MSC_IO_LUN_MAX;
			break;
		case 12:
			params.max_errors = get_integer();
			break;
		case 13:
			params.duration = get_integer();
			break;
		case 14:
			params.try_abort_data_write ^= 1;
			break;
		case 15:
			params.try_sense_on_error ^= 1;
			break;
		case 16:
			params.try_all_lun ^= 1;
			break;
		case 17:
			params.try_shorter_wrapper_block ^= 1;
			break;
		case 20:
			set_defaults(&params);
			break;
		case 30:
			exec_host_msc_test(&params, vid, pid);
			break;
		case 40:
			show_host_device_selection(level + 1, &vid, &pid);
			break;
		default:
			return;
		}
	}
}
