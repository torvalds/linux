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
#include <sys/rman.h>
#include <sys/selinfo.h>
#include <machine/bus.h>

#ifdef LOCAL_MODULE
#include <ipmi.h>
#include <ipmivars.h>
#else
#include <sys/ipmi.h>
#include <dev/ipmi/ipmivars.h>
#endif

static void	kcs_clear_obf(struct ipmi_softc *, int);
static void	kcs_error(struct ipmi_softc *);
static int	kcs_wait_for_ibf(struct ipmi_softc *, int);
static int	kcs_wait_for_obf(struct ipmi_softc *, int);

static int
kcs_wait_for_ibf(struct ipmi_softc *sc, int state)
{
	int status, start = ticks;

	status = INB(sc, KCS_CTL_STS);
	if (state == 0) {
		/* WAIT FOR IBF = 0 */
		while (ticks - start < MAX_TIMEOUT && status & KCS_STATUS_IBF) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}
	} else {
		/* WAIT FOR IBF = 1 */
		while (ticks - start < MAX_TIMEOUT &&
		    !(status & KCS_STATUS_IBF)) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}
	}
	return (status);
}

static int
kcs_wait_for_obf(struct ipmi_softc *sc, int state)
{
	int status, start = ticks;

	status = INB(sc, KCS_CTL_STS);
	if (state == 0) {
		/* WAIT FOR OBF = 0 */
		while (ticks - start < MAX_TIMEOUT && status & KCS_STATUS_OBF) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}
	} else {
		/* WAIT FOR OBF = 1 */
		while (ticks - start < MAX_TIMEOUT &&
		    !(status & KCS_STATUS_OBF)) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}
	}
	return (status);
}

static void
kcs_clear_obf(struct ipmi_softc *sc, int status)
{
	int data;

	/* Clear OBF */
	if (status & KCS_STATUS_OBF) {
		data = INB(sc, KCS_DATA);
	}
}

static void
kcs_error(struct ipmi_softc *sc)
{
	int retry, status;
	u_char data;

	for (retry = 0; retry < 2; retry++) {

		/* Wait for IBF = 0 */
		status = kcs_wait_for_ibf(sc, 0);

		/* ABORT */
		OUTB(sc, KCS_CTL_STS, KCS_CONTROL_GET_STATUS_ABORT);

		/* Wait for IBF = 0 */
		status = kcs_wait_for_ibf(sc, 0);

		/* Clear OBF */
		kcs_clear_obf(sc, status);

		if (status & KCS_STATUS_OBF) {
			data = INB(sc, KCS_DATA);
			if (data != 0)
				device_printf(sc->ipmi_dev,
				    "KCS Error Data %02x\n", data);
		}

		/* 0x00 to DATA_IN */
		OUTB(sc, KCS_DATA, 0x00);

		/* Wait for IBF = 0 */
		status = kcs_wait_for_ibf(sc, 0);

		if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_READ) {

			/* Wait for OBF = 1 */
			status = kcs_wait_for_obf(sc, 1);

			/* Read error status */
			data = INB(sc, KCS_DATA);
			if (data != 0 && (data != 0xff || bootverbose))
				device_printf(sc->ipmi_dev, "KCS error: %02x\n",
				    data);

			/* Write READ into Data_in */
			OUTB(sc, KCS_DATA, KCS_DATA_IN_READ);

			/* Wait for IBF = 0 */
			status = kcs_wait_for_ibf(sc, 0);
		}

		/* IDLE STATE */
		if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_IDLE) {
			/* Wait for OBF = 1 */
			status = kcs_wait_for_obf(sc, 1);

			/* Clear OBF */
			kcs_clear_obf(sc, status);
			return;
		}
	}
	device_printf(sc->ipmi_dev, "KCS: Error retry exhausted\n");
}

/*
 * Start to write a request.  Waits for IBF to clear and then sends the
 * WR_START command.
 */
static int
kcs_start_write(struct ipmi_softc *sc)
{
	int retry, status;

	for (retry = 0; retry < 10; retry++) {
		/* Wait for IBF = 0 */
		status = kcs_wait_for_ibf(sc, 0);
		if (status & KCS_STATUS_IBF)
			return (0);

		/* Clear OBF */
		kcs_clear_obf(sc, status);

		/* Write start to command */
		OUTB(sc, KCS_CTL_STS, KCS_CONTROL_WRITE_START);

		/* Wait for IBF = 0 */
		status = kcs_wait_for_ibf(sc, 0);
		if (status & KCS_STATUS_IBF)
			return (0);

		if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_WRITE)
			break;
		DELAY(1000000);
	}

	if (KCS_STATUS_STATE(status) != KCS_STATUS_STATE_WRITE)
		/* error state */
		return (0);

	/* Clear OBF */
	kcs_clear_obf(sc, status);

	return (1);
}

/*
 * Write a byte of the request message, excluding the last byte of the
 * message which requires special handling.
 */
static int
kcs_write_byte(struct ipmi_softc *sc, u_char data)
{
	int status;

	/* Data to Data */
	OUTB(sc, KCS_DATA, data);

	/* Wait for IBF = 0 */
	status = kcs_wait_for_ibf(sc, 0);
	if (status & KCS_STATUS_IBF)
		return (0);

	if (KCS_STATUS_STATE(status) != KCS_STATUS_STATE_WRITE)
		return (0);

	/* Clear OBF */
	kcs_clear_obf(sc, status);
	return (1);
}

/*
 * Write the last byte of a request message.
 */
static int
kcs_write_last_byte(struct ipmi_softc *sc, u_char data)
{
	int status;

	/* Write end to command */
	OUTB(sc, KCS_CTL_STS, KCS_CONTROL_WRITE_END);

	/* Wait for IBF = 0 */
	status = kcs_wait_for_ibf(sc, 0);
	if (status & KCS_STATUS_IBF)
		return (0);

	if (KCS_STATUS_STATE(status) != KCS_STATUS_STATE_WRITE)
		/* error state */
		return (0);

	/* Clear OBF */
	kcs_clear_obf(sc, status);

	/* Send data byte to DATA. */
	OUTB(sc, KCS_DATA, data);
	return (1);
}

/*
 * Read one byte of the reply message.
 */
static int
kcs_read_byte(struct ipmi_softc *sc, u_char *data)
{
	int status;
	u_char dummy;

	/* Wait for IBF = 0 */
	status = kcs_wait_for_ibf(sc, 0);

	/* Read State */
	if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_READ) {

		/* Wait for OBF = 1 */
		status = kcs_wait_for_obf(sc, 1);
		if ((status & KCS_STATUS_OBF) == 0)
			return (0);

		/* Read Data_out */
		*data = INB(sc, KCS_DATA);

		/* Write READ into Data_in */
		OUTB(sc, KCS_DATA, KCS_DATA_IN_READ);
		return (1);
	}

	/* Idle State */
	if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_IDLE) {

		/* Wait for OBF = 1*/
		status = kcs_wait_for_obf(sc, 1);
		if ((status & KCS_STATUS_OBF) == 0)
			return (0);

		/* Read Dummy */
		dummy = INB(sc, KCS_DATA);
		return (2);
	}

	/* Error State */
	return (0);
}

/*
 * Send a request message and collect the reply.  Returns true if we
 * succeed.
 */
static int
kcs_polled_request(struct ipmi_softc *sc, struct ipmi_request *req)
{
	u_char *cp, data;
	int i, state;

	IPMI_IO_LOCK(sc);

	/* Send the request. */
	if (!kcs_start_write(sc)) {
		device_printf(sc->ipmi_dev, "KCS: Failed to start write\n");
		goto fail;
	}
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: WRITE_START... ok\n");
#endif

	if (!kcs_write_byte(sc, req->ir_addr)) {
		device_printf(sc->ipmi_dev, "KCS: Failed to write address\n");
		goto fail;
	}
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: Wrote address: %02x\n", req->ir_addr);
#endif

	if (req->ir_requestlen == 0) {
		if (!kcs_write_last_byte(sc, req->ir_command)) {
			device_printf(sc->ipmi_dev,
			    "KCS: Failed to write command\n");
			goto fail;
		}
#ifdef KCS_DEBUG
		device_printf(sc->ipmi_dev, "KCS: Wrote command: %02x\n",
		    req->ir_command);
#endif
	} else {
		if (!kcs_write_byte(sc, req->ir_command)) {
			device_printf(sc->ipmi_dev,
			    "KCS: Failed to write command\n");
			goto fail;
		}
#ifdef KCS_DEBUG
		device_printf(sc->ipmi_dev, "KCS: Wrote command: %02x\n",
		    req->ir_command);
#endif

		cp = req->ir_request;
		for (i = 0; i < req->ir_requestlen - 1; i++) {
			if (!kcs_write_byte(sc, *cp++)) {
				device_printf(sc->ipmi_dev,
				    "KCS: Failed to write data byte %d\n",
				    i + 1);
				goto fail;
			}
#ifdef KCS_DEBUG
			device_printf(sc->ipmi_dev, "KCS: Wrote data: %02x\n",
			    cp[-1]);
#endif
		}

		if (!kcs_write_last_byte(sc, *cp)) {
			device_printf(sc->ipmi_dev,
			    "KCS: Failed to write last dta byte\n");
			goto fail;
		}
#ifdef KCS_DEBUG
		device_printf(sc->ipmi_dev, "KCS: Wrote last data: %02x\n",
		    *cp);
#endif
	}

	/* Read the reply.  First, read the NetFn/LUN. */
	if (kcs_read_byte(sc, &data) != 1) {
		device_printf(sc->ipmi_dev, "KCS: Failed to read address\n");
		goto fail;
	}
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: Read address: %02x\n", data);
#endif
	if (data != IPMI_REPLY_ADDR(req->ir_addr)) {
		device_printf(sc->ipmi_dev, "KCS: Reply address mismatch\n");
		goto fail;
	}

	/* Next we read the command. */
	if (kcs_read_byte(sc, &data) != 1) {
		device_printf(sc->ipmi_dev, "KCS: Failed to read command\n");
		goto fail;
	}
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: Read command: %02x\n", data);
#endif
	if (data != req->ir_command) {
		device_printf(sc->ipmi_dev, "KCS: Command mismatch\n");
		goto fail;
	}

	/* Next we read the completion code. */
	if (kcs_read_byte(sc, &req->ir_compcode) != 1) {
		if (bootverbose) {
			device_printf(sc->ipmi_dev,
			    "KCS: Failed to read completion code\n");
		}
		goto fail;
	}
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: Read completion code: %02x\n",
	    req->ir_compcode);
#endif

	/* Finally, read the reply from the BMC. */
	i = 0;
	for (;;) {
		state = kcs_read_byte(sc, &data);
		if (state == 0) {
			device_printf(sc->ipmi_dev,
			    "KCS: Read failed on byte %d\n", i + 1);
			goto fail;
		}
		if (state == 2)
			break;
		if (i < req->ir_replybuflen) {
			req->ir_reply[i] = data;
#ifdef KCS_DEBUG
			device_printf(sc->ipmi_dev, "KCS: Read data %02x\n",
			    data);
		} else {
			device_printf(sc->ipmi_dev,
			    "KCS: Read short %02x byte %d\n", data, i + 1);
#endif
		}
		i++;
	}
	IPMI_IO_UNLOCK(sc);
	req->ir_replylen = i;
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: READ finished (%d bytes)\n", i);
	if (req->ir_replybuflen < i)
#else
	if (req->ir_replybuflen < i && req->ir_replybuflen != 0)
#endif
		device_printf(sc->ipmi_dev,
		    "KCS: Read short: %zd buffer, %d actual\n",
		    req->ir_replybuflen, i);
	return (1);
fail:
	kcs_error(sc);
	IPMI_IO_UNLOCK(sc);
	return (0);
}

static void
kcs_loop(void *arg)
{
	struct ipmi_softc *sc = arg;
	struct ipmi_request *req;
	int i, ok;

	IPMI_LOCK(sc);
	while ((req = ipmi_dequeue_request(sc)) != NULL) {
		IPMI_UNLOCK(sc);
		ok = 0;
		for (i = 0; i < 3 && !ok; i++)
			ok = kcs_polled_request(sc, req);
		if (ok)
			req->ir_error = 0;
		else
			req->ir_error = EIO;
		IPMI_LOCK(sc);
		ipmi_complete_request(sc, req);
	}
	IPMI_UNLOCK(sc);
	kproc_exit(0);
}

static int
kcs_startup(struct ipmi_softc *sc)
{

	return (kproc_create(kcs_loop, sc, &sc->ipmi_kthread, 0, 0, "%s: kcs",
	    device_get_nameunit(sc->ipmi_dev)));
}

static int
kcs_driver_request(struct ipmi_softc *sc, struct ipmi_request *req, int timo)
{
	int i, ok;

	ok = 0;
	for (i = 0; i < 3 && !ok; i++)
		ok = kcs_polled_request(sc, req);
	if (ok)
		req->ir_error = 0;
	else
		req->ir_error = EIO;
	return (req->ir_error);
}

int
ipmi_kcs_attach(struct ipmi_softc *sc)
{
	int status;

	/* Setup function pointers. */
	sc->ipmi_startup = kcs_startup;
	sc->ipmi_enqueue_request = ipmi_polled_enqueue_request;
	sc->ipmi_driver_request = kcs_driver_request;
	sc->ipmi_driver_requests_polled = 1;

	/* See if we can talk to the controller. */
	status = INB(sc, KCS_CTL_STS);
	if (status == 0xff) {
		device_printf(sc->ipmi_dev, "couldn't find it\n");
		return (ENXIO);
	}

#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "KCS: initial state: %02x\n", status);
#endif
	if (status & KCS_STATUS_OBF ||
	    KCS_STATUS_STATE(status) != KCS_STATUS_STATE_IDLE)
		kcs_error(sc);

	return (0);
}

/*
 * Determine the alignment automatically for a PCI attachment.  In this case,
 * any unused bytes will return 0x00 when read.  We make use of the C/D bit
 * in the CTL_STS register to try to start a GET_STATUS transaction.  When
 * we write the command, that bit should be set, so we should get a non-zero
 * value back when we read CTL_STS if the offset we are testing is the CTL_STS
 * register.
 */
int
ipmi_kcs_probe_align(struct ipmi_softc *sc)
{
	int data, status;

	sc->ipmi_io_spacing = 1;
retry:
#ifdef KCS_DEBUG
	device_printf(sc->ipmi_dev, "Trying KCS align %d... ", sc->ipmi_io_spacing);
#endif

	/* Wait for IBF = 0 */
	status = INB(sc, KCS_CTL_STS);
	while (status & KCS_STATUS_IBF) {
		DELAY(100);
		status = INB(sc, KCS_CTL_STS);
	}

	OUTB(sc, KCS_CTL_STS, KCS_CONTROL_GET_STATUS_ABORT);

	/* Wait for IBF = 0 */
	status = INB(sc, KCS_CTL_STS);
	while (status & KCS_STATUS_IBF) {
		DELAY(100);
		status = INB(sc, KCS_CTL_STS);
	}

	/* If we got 0x00 back, then this must not be the CTL_STS register. */
	if (status == 0) {
#ifdef KCS_DEBUG
		printf("failed\n");
#endif
		sc->ipmi_io_spacing <<= 1;
		if (sc->ipmi_io_spacing > 4)
			return (0);
		goto retry;
	}
#ifdef KCS_DEBUG
	printf("ok\n");
#endif

	/* Finish out the transaction. */

	/* Clear OBF */
	if (status & KCS_STATUS_OBF)
		data = INB(sc, KCS_DATA);

	/* 0x00 to DATA_IN */
	OUTB(sc, KCS_DATA, 0);

	/* Wait for IBF = 0 */
	status = INB(sc, KCS_CTL_STS);
	while (status & KCS_STATUS_IBF) {
		DELAY(100);
		status = INB(sc, KCS_CTL_STS);
	}

	if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_READ) {
		/* Wait for IBF = 1 */
		while (!(status & KCS_STATUS_OBF)) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}

		/* Read error status. */
		data = INB(sc, KCS_DATA);

		/* Write dummy READ to DATA_IN. */
		OUTB(sc, KCS_DATA, KCS_DATA_IN_READ);

		/* Wait for IBF = 0 */
		status = INB(sc, KCS_CTL_STS);
		while (status & KCS_STATUS_IBF) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}
	}

	if (KCS_STATUS_STATE(status) == KCS_STATUS_STATE_IDLE) {
		/* Wait for IBF = 1 */
		while (!(status & KCS_STATUS_OBF)) {
			DELAY(100);
			status = INB(sc, KCS_CTL_STS);
		}

		/* Clear OBF */
		if (status & KCS_STATUS_OBF)
			data = INB(sc, KCS_DATA);
	} else
		device_printf(sc->ipmi_dev, "KCS probe: end state %x\n",
		    KCS_STATUS_STATE(status));

	return (1);
}
