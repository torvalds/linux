/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/module.h>
#include <sys/selinfo.h>

#include <dev/smbus/smbconf.h>
#include <dev/smbus/smb.h>

#include "smbus_if.h"

#ifdef LOCAL_MODULE
#include <ipmivars.h>
#else
#include <dev/ipmi/ipmivars.h>
#endif

#define SMBUS_WRITE_SINGLE	0x02
#define SMBUS_WRITE_START	0x06
#define SMBUS_WRITE_CONT	0x07
#define SMBUS_READ_START	0x03
#define SMBUS_READ_CONT		0x09
#define SMBUS_DATA_SIZE		32

#ifdef SSIF_DEBUG
static void
dump_buffer(device_t dev, const char *msg, u_char *bytes, int len)
{
	int i;

	device_printf(dev, "%s:", msg);
	for (i = 0; i < len; i++)
		printf(" %02x", bytes[i]);
	printf("\n");
}
#endif

static int
ssif_polled_request(struct ipmi_softc *sc, struct ipmi_request *req)
{
	u_char ssif_buf[SMBUS_DATA_SIZE];
	device_t dev = sc->ipmi_dev;
	device_t smbus = sc->ipmi_ssif_smbus;
	u_char *cp, block, count, offset;
	size_t len;
	int error;

	/* Acquire the bus while we send the request. */
	if (smbus_request_bus(smbus, dev, SMB_WAIT) != 0)
		return (0);

	/*
	 * First, send out the request.  Begin by filling out the first
	 * packet which includes the NetFn/LUN and command.
	 */
	ssif_buf[0] = req->ir_addr;
	ssif_buf[1] = req->ir_command;
	if (req->ir_requestlen > 0)
		bcopy(req->ir_request, &ssif_buf[2],
		    min(req->ir_requestlen, SMBUS_DATA_SIZE - 2));

	/* Small requests are sent with a single command. */
	if (req->ir_requestlen <= 30) {
#ifdef SSIF_DEBUG
		dump_buffer(dev, "WRITE_SINGLE", ssif_buf,
		    req->ir_requestlen + 2);
#endif
		error = smbus_error(smbus_bwrite(smbus,
			sc->ipmi_ssif_smbus_address, SMBUS_WRITE_SINGLE,
			req->ir_requestlen + 2, ssif_buf));
		if (error) {
#ifdef SSIF_ERROR_DEBUG
			device_printf(dev, "SSIF: WRITE_SINGLE error %d\n",
			    error);
#endif
			goto fail;
		}
	} else {
		/* Longer requests are sent out in 32-byte messages. */
#ifdef SSIF_DEBUG
		dump_buffer(dev, "WRITE_START", ssif_buf, SMBUS_DATA_SIZE);
#endif
		error = smbus_error(smbus_bwrite(smbus,
			sc->ipmi_ssif_smbus_address, SMBUS_WRITE_START,
			SMBUS_DATA_SIZE, ssif_buf));
		if (error) {
#ifdef SSIF_ERROR_DEBUG
			device_printf(dev, "SSIF: WRITE_START error %d\n",
			    error);
#endif
			goto fail;
		}

		len = req->ir_requestlen - (SMBUS_DATA_SIZE - 2);
		cp = req->ir_request + (SMBUS_DATA_SIZE - 2);
		while (len > 0) {
#ifdef SSIF_DEBUG
			dump_buffer(dev, "WRITE_CONT", cp,
			    min(len, SMBUS_DATA_SIZE));
#endif
			error = smbus_error(smbus_bwrite(smbus,
			    sc->ipmi_ssif_smbus_address, SMBUS_WRITE_CONT,
			    min(len, SMBUS_DATA_SIZE), cp));
			if (error) {
#ifdef SSIF_ERROR_DEBUG
				device_printf(dev, "SSIF: WRITE_CONT error %d\n",
				    error);
#endif
				goto fail;
			}
			cp += SMBUS_DATA_SIZE;
			len -= SMBUS_DATA_SIZE;
		}

		/*
		 * The final WRITE_CONT transaction has to have a non-zero
		 * length that is also not SMBUS_DATA_SIZE.  If our last
		 * WRITE_CONT transaction in the loop sent SMBUS_DATA_SIZE
		 * bytes, then len will be 0, and we send an extra 0x00 byte
		 * to terminate the transaction.
		 */
		if (len == 0) {
			char c = 0;

#ifdef SSIF_DEBUG
			dump_buffer(dev, "WRITE_CONT", &c, 1);
#endif
			error = smbus_error(smbus_bwrite(smbus,
				sc->ipmi_ssif_smbus_address, SMBUS_WRITE_CONT,
				1, &c));
			if (error) {
#ifdef SSIF_ERROR_DEBUG
				device_printf(dev, "SSIF: WRITE_CONT error %d\n",
				    error);
#endif
				goto fail;
			}
		}
	}

	/* Release the bus. */
	smbus_release_bus(smbus, dev);

	/* Give the BMC 100ms to chew on the request. */
	pause("ssifwt", hz / 10);

	/* Try to read the first packet. */
read_start:
	if (smbus_request_bus(smbus, dev, SMB_WAIT) != 0)
		return (0);
	count = SMBUS_DATA_SIZE;
	error = smbus_error(smbus_bread(smbus,
	    sc->ipmi_ssif_smbus_address, SMBUS_READ_START, &count, ssif_buf));
	if (error == ENXIO || error == EBUSY) {
		smbus_release_bus(smbus, dev);
#ifdef SSIF_DEBUG
		device_printf(dev, "SSIF: READ_START retry\n");
#endif
		/* Give the BMC another 10ms. */
		pause("ssifwt", hz / 100);
		goto read_start;
	}
	if (error) {
#ifdef SSIF_ERROR_DEBUG
		device_printf(dev, "SSIF: READ_START failed: %d\n", error);
#endif
		goto fail;
	}
#ifdef SSIF_DEBUG
	device_printf("SSIF: READ_START: ok\n");
#endif

	/*
	 * If this is the first part of a multi-part read, then we need to
	 * skip the first two bytes.
	 */
	if (count == SMBUS_DATA_SIZE && ssif_buf[0] == 0 && ssif_buf[1] == 1)
		offset = 2;
	else
		offset = 0;

	/* We had better get the reply header. */
	if (count < 3) {
		device_printf(dev, "SSIF: Short reply packet\n");
		goto fail;
	}

	/* Verify the NetFn/LUN. */
	if (ssif_buf[offset] != IPMI_REPLY_ADDR(req->ir_addr)) {
		device_printf(dev, "SSIF: Reply address mismatch\n");
		goto fail;
	}

	/* Verify the command. */
	if (ssif_buf[offset + 1] != req->ir_command) {
		device_printf(dev, "SMIC: Command mismatch\n");
		goto fail;
	}

	/* Read the completion code. */
	req->ir_compcode = ssif_buf[offset + 2];

	/* If this is a single read, just copy the data and return. */
	if (offset == 0) {
#ifdef SSIF_DEBUG
		dump_buffer(dev, "READ_SINGLE", ssif_buf, count);
#endif
		len = count - 3;
		bcopy(&ssif_buf[3], req->ir_reply,
		    min(req->ir_replybuflen, len));
		goto done;
	}

	/*
	 * This is the first part of a multi-read transaction, so copy
	 * out the payload and start looping.
	 */
#ifdef SSIF_DEBUG
	dump_buffer(dev, "READ_START", ssif_buf + 2, count - 2);
#endif
	bcopy(&ssif_buf[5], req->ir_reply, min(req->ir_replybuflen, count - 5));
	len = count - 5;
	block = 1;

	for (;;) {
		/* Read another packet via READ_CONT. */
		count = SMBUS_DATA_SIZE;
		error = smbus_error(smbus_bread(smbus,
		    sc->ipmi_ssif_smbus_address, SMBUS_READ_CONT, &count,
		    ssif_buf));
		if (error) {
#ifdef SSIF_ERROR_DEBUG
			printf("SSIF: READ_CONT failed: %d\n", error);
#endif
			goto fail;
		}
#ifdef SSIF_DEBUG
		device_printf(dev, "SSIF: READ_CONT... ok\n");
#endif

		/* Verify the block number.  0xff marks the last block. */
		if (ssif_buf[0] != 0xff && ssif_buf[0] != block) {
			device_printf(dev, "SSIF: Read wrong block %d %d\n",
			    ssif_buf[0], block);
			goto fail;
		}
		if (ssif_buf[0] != 0xff && count < SMBUS_DATA_SIZE) {
			device_printf(dev,
			    "SSIF: Read short middle block, length %d\n",
			    count);
			goto fail;
		}
#ifdef SSIF_DEBUG
		if (ssif_buf[0] == 0xff)
			dump_buffer(dev, "READ_END", ssif_buf + 1, count - 1);
		else
			dump_buffer(dev, "READ_CONT", ssif_buf + 1, count - 1);
#endif
		if (len < req->ir_replybuflen)
			bcopy(&ssif_buf[1], &req->ir_reply[len],
			    min(req->ir_replybuflen - len, count - 1));
		len += count - 1;

		/* If this was the last block we are done. */
		if (ssif_buf[0] != 0xff)
			break;
		block++;
	}

done:
	/* Save the total length and return success. */
	req->ir_replylen = len;
	smbus_release_bus(smbus, dev);
	return (1);

fail:
	smbus_release_bus(smbus, dev);
	return (0);
}

static void
ssif_loop(void *arg)
{
	struct ipmi_softc *sc = arg;
	struct ipmi_request *req;
	int i, ok;

	IPMI_LOCK(sc);
	while ((req = ipmi_dequeue_request(sc)) != NULL) {
		IPMI_UNLOCK(sc);
		ok = 0;
		for (i = 0; i < 5; i++) {
			ok = ssif_polled_request(sc, req);
			if (ok)
				break;

			/* Wait 60 ms between retries. */
			pause("retry", 60 * hz / 1000);
#ifdef SSIF_RETRY_DEBUG
			device_printf(sc->ipmi_dev,
			    "SSIF: Retrying request (%d)\n", i + 1);
#endif
		}
		if (ok)
			req->ir_error = 0;
		else
			req->ir_error = EIO;
		IPMI_LOCK(sc);
		ipmi_complete_request(sc, req);
		IPMI_UNLOCK(sc);

		/* Enforce 10ms between requests. */
		pause("delay", hz / 100);

		IPMI_LOCK(sc);
	}
	IPMI_UNLOCK(sc);
	kproc_exit(0);
}

static int
ssif_startup(struct ipmi_softc *sc)
{

	return (kproc_create(ssif_loop, sc, &sc->ipmi_kthread, 0, 0,
	    "%s: ssif", device_get_nameunit(sc->ipmi_dev)));
}

static int
ssif_driver_request(struct ipmi_softc *sc, struct ipmi_request *req, int timo)
{
	int error;

	IPMI_LOCK(sc);
	error = ipmi_polled_enqueue_request(sc, req);
	if (error == 0)
		error = msleep(req, &sc->ipmi_requests_lock, 0, "ipmireq",
		    timo);
	if (error == 0)
		error = req->ir_error;
	IPMI_UNLOCK(sc);
	return (error);
}

int
ipmi_ssif_attach(struct ipmi_softc *sc, device_t smbus, int smbus_address)
{

	/* Setup smbus address. */
	sc->ipmi_ssif_smbus = smbus;
	sc->ipmi_ssif_smbus_address = smbus_address;

	/* Setup function pointers. */
	sc->ipmi_startup = ssif_startup;
	sc->ipmi_enqueue_request = ipmi_polled_enqueue_request;
	sc->ipmi_driver_request = ssif_driver_request;

	return (0);
}
