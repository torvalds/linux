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

static void	smic_wait_for_tx_okay(struct ipmi_softc *);
static void	smic_wait_for_rx_okay(struct ipmi_softc *);
static void	smic_wait_for_not_busy(struct ipmi_softc *);
static void	smic_set_busy(struct ipmi_softc *);

static void
smic_wait_for_tx_okay(struct ipmi_softc *sc)
{
	int flags;

	do {
		flags = INB(sc, SMIC_FLAGS);
	} while (!(flags & SMIC_STATUS_TX_RDY));
}

static void
smic_wait_for_rx_okay(struct ipmi_softc *sc)
{
	int flags;

	do {
		flags = INB(sc, SMIC_FLAGS);
	} while (!(flags & SMIC_STATUS_RX_RDY));
}

static void
smic_wait_for_not_busy(struct ipmi_softc *sc)
{
	int flags;

	do {
		flags = INB(sc, SMIC_FLAGS);
	} while (flags & SMIC_STATUS_BUSY);
}

static void
smic_set_busy(struct ipmi_softc *sc)
{
	int flags;

	flags = INB(sc, SMIC_FLAGS);
	flags |= SMIC_STATUS_BUSY;
	flags &= ~SMIC_STATUS_RESERVED;
	OUTB(sc, SMIC_FLAGS, flags);
}

/*
 * Start a transfer with a WR_START transaction that sends the NetFn/LUN
 * address.
 */
static int
smic_start_write(struct ipmi_softc *sc, u_char data)
{
	u_char error, status;

	smic_wait_for_not_busy(sc);

	OUTB(sc, SMIC_CTL_STS, SMIC_CC_SMS_WR_START);
	OUTB(sc, SMIC_DATA, data);
	smic_set_busy(sc);
	smic_wait_for_not_busy(sc);
	status = INB(sc, SMIC_CTL_STS);
	if (status != SMIC_SC_SMS_WR_START) {
		error = INB(sc, SMIC_DATA);
		device_printf(sc->ipmi_dev, "SMIC: Write did not start %02x\n",
		    error);
		return (0);
	}
	return (1);
}

/*
 * Write a byte in the middle of the message (either the command or one of
 * the data bytes) using a WR_NEXT transaction.
 */
static int
smic_write_next(struct ipmi_softc *sc, u_char data)
{
	u_char error, status;

	smic_wait_for_tx_okay(sc);
	OUTB(sc, SMIC_CTL_STS, SMIC_CC_SMS_WR_NEXT);
	OUTB(sc, SMIC_DATA, data);
	smic_set_busy(sc);
	smic_wait_for_not_busy(sc);
	status = INB(sc, SMIC_CTL_STS);
	if (status != SMIC_SC_SMS_WR_NEXT) {
		error = INB(sc, SMIC_DATA);
		device_printf(sc->ipmi_dev, "SMIC: Write did not next %02x\n",
		    error);
		return (0);
	}
	return (1);
}

/*
 * Write the last byte of a transfer to end the write phase via a WR_END
 * transaction.
 */
static int
smic_write_last(struct ipmi_softc *sc, u_char data)
{
	u_char error, status;

	smic_wait_for_tx_okay(sc);
	OUTB(sc, SMIC_CTL_STS, SMIC_CC_SMS_WR_END);
	OUTB(sc, SMIC_DATA, data);
	smic_set_busy(sc);
	smic_wait_for_not_busy(sc);
	status = INB(sc, SMIC_CTL_STS);
	if (status != SMIC_SC_SMS_WR_END) {
		error = INB(sc, SMIC_DATA);
		device_printf(sc->ipmi_dev, "SMIC: Write did not end %02x\n",
		    error);
		return (0);
	}
	return (1);
}

/*
 * Start the read phase of a transfer with a RD_START transaction.
 */
static int
smic_start_read(struct ipmi_softc *sc, u_char *data)
{
	u_char error, status;

	smic_wait_for_not_busy(sc);

	smic_wait_for_rx_okay(sc);
	OUTB(sc, SMIC_CTL_STS, SMIC_CC_SMS_RD_START);
	smic_set_busy(sc);
	smic_wait_for_not_busy(sc);
	status = INB(sc, SMIC_CTL_STS);
	if (status != SMIC_SC_SMS_RD_START) {
		error = INB(sc, SMIC_DATA);
		device_printf(sc->ipmi_dev, "SMIC: Read did not start %02x\n",
		    error);
		return (0);
	}
	*data = INB(sc, SMIC_DATA);
	return (1);
}

/*
 * Read a byte via a RD_NEXT transaction.  If this was the last byte, return
 * 2 rather than 1.
 */
static int
smic_read_byte(struct ipmi_softc *sc, u_char *data)
{
	u_char error, status;

	smic_wait_for_rx_okay(sc);
	OUTB(sc, SMIC_CTL_STS, SMIC_SC_SMS_RD_NEXT);
	smic_set_busy(sc);
	smic_wait_for_not_busy(sc);
	status = INB(sc, SMIC_CTL_STS);
	if (status != SMIC_SC_SMS_RD_NEXT &&
	    status != SMIC_SC_SMS_RD_END) {
		error = INB(sc, SMIC_DATA);
		device_printf(sc->ipmi_dev, "SMIC: Read did not next %02x\n",
		    error);
		return (0);
	}
	*data = INB(sc, SMIC_DATA);
	if (status == SMIC_SC_SMS_RD_NEXT)
		return (1);
	else
		return (2);
}

/* Complete a transfer via a RD_END transaction after reading the last byte. */
static int
smic_read_end(struct ipmi_softc *sc)
{
	u_char error, status;

	OUTB(sc, SMIC_CTL_STS, SMIC_CC_SMS_RD_END);
	smic_set_busy(sc);
	smic_wait_for_not_busy(sc);
	status = INB(sc, SMIC_CTL_STS);
	if (status != SMIC_SC_SMS_RDY) {
		error = INB(sc, SMIC_DATA);
		device_printf(sc->ipmi_dev, "SMIC: Read did not end %02x\n",
		    error);
		return (0);
	}
	return (1);
}

static int
smic_polled_request(struct ipmi_softc *sc, struct ipmi_request *req)
{
	u_char *cp, data;
	int i, state;

	/* First, start the message with the address. */
	if (!smic_start_write(sc, req->ir_addr))
		return (0);
#ifdef SMIC_DEBUG
	device_printf(sc->ipmi_dev, "SMIC: WRITE_START address: %02x\n",
	    req->ir_addr);
#endif

	if (req->ir_requestlen == 0) {
		/* Send the command as the last byte. */
		if (!smic_write_last(sc, req->ir_command))
			return (0);
#ifdef SMIC_DEBUG
		device_printf(sc->ipmi_dev, "SMIC: Wrote command: %02x\n",
		    req->ir_command);
#endif
	} else {
		/* Send the command. */
		if (!smic_write_next(sc, req->ir_command))
			return (0);
#ifdef SMIC_DEBUG
		device_printf(sc->ipmi_dev, "SMIC: Wrote command: %02x\n",
		    req->ir_command);
#endif

		/* Send the payload. */
		cp = req->ir_request;
		for (i = 0; i < req->ir_requestlen - 1; i++) {
			if (!smic_write_next(sc, *cp++))
				return (0);
#ifdef SMIC_DEBUG
			device_printf(sc->ipmi_dev, "SMIC: Wrote data: %02x\n",
			    cp[-1]);
#endif
		}
		if (!smic_write_last(sc, *cp))
			return (0);
#ifdef SMIC_DEBUG
		device_printf(sc->ipmi_dev, "SMIC: Write last data: %02x\n",
		    *cp);
#endif
	}

	/* Start the read phase by reading the NetFn/LUN. */
	if (smic_start_read(sc, &data) != 1)
		return (0);
#ifdef SMIC_DEBUG
	device_printf(sc->ipmi_dev, "SMIC: Read address: %02x\n", data);
#endif
	if (data != IPMI_REPLY_ADDR(req->ir_addr)) {
		device_printf(sc->ipmi_dev, "SMIC: Reply address mismatch\n");
		return (0);
	}

	/* Read the command. */
	if (smic_read_byte(sc, &data) != 1)
		return (0);
#ifdef SMIC_DEBUG
	device_printf(sc->ipmi_dev, "SMIC: Read command: %02x\n", data);
#endif
	if (data != req->ir_command) {
		device_printf(sc->ipmi_dev, "SMIC: Command mismatch\n");
		return (0);
	}

	/* Read the completion code. */
	state = smic_read_byte(sc, &req->ir_compcode);
	if (state == 0)
		return (0);
#ifdef SMIC_DEBUG
	device_printf(sc->ipmi_dev, "SMIC: Read completion code: %02x\n",
	    req->ir_compcode);
#endif

	/* Finally, read the reply from the BMC. */
	i = 0;
	while (state == 1) {
		state = smic_read_byte(sc, &data);
		if (state == 0)
			return (0);
		if (i < req->ir_replybuflen) {
			req->ir_reply[i] = data;
#ifdef SMIC_DEBUG
			device_printf(sc->ipmi_dev, "SMIC: Read data: %02x\n",
			    data);
		} else {
			device_printf(sc->ipmi_dev,
			    "SMIC: Read short %02x byte %d\n", data, i + 1);
#endif
		}
		i++;
	}

	/* Terminate the transfer. */
	if (!smic_read_end(sc))
		return (0);
	req->ir_replylen = i;
#ifdef SMIC_DEBUG
	device_printf(sc->ipmi_dev, "SMIC: READ finished (%d bytes)\n", i);
	if (req->ir_replybuflen < i)
#else
	if (req->ir_replybuflen < i && req->ir_replybuflen != 0)
#endif
		device_printf(sc->ipmi_dev,
		    "SMIC: Read short: %zd buffer, %d actual\n",
		    req->ir_replybuflen, i);
	return (1);
}

static void
smic_loop(void *arg)
{
	struct ipmi_softc *sc = arg;
	struct ipmi_request *req;
	int i, ok;

	IPMI_LOCK(sc);
	while ((req = ipmi_dequeue_request(sc)) != NULL) {
		IPMI_UNLOCK(sc);
		ok = 0;
		for (i = 0; i < 3 && !ok; i++) {
			IPMI_IO_LOCK(sc);
			ok = smic_polled_request(sc, req);
			IPMI_IO_UNLOCK(sc);
		}
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
smic_startup(struct ipmi_softc *sc)
{

	return (kproc_create(smic_loop, sc, &sc->ipmi_kthread, 0, 0,
	    "%s: smic", device_get_nameunit(sc->ipmi_dev)));
}

static int
smic_driver_request(struct ipmi_softc *sc, struct ipmi_request *req, int timo)
{
	int i, ok;

	ok = 0;
	for (i = 0; i < 3 && !ok; i++) {
		IPMI_IO_LOCK(sc);
		ok = smic_polled_request(sc, req);
		IPMI_IO_UNLOCK(sc);
	}
	if (ok)
		req->ir_error = 0;
	else
		req->ir_error = EIO;
	return (req->ir_error);
}

int
ipmi_smic_attach(struct ipmi_softc *sc)
{
	int flags;

	/* Setup function pointers. */
	sc->ipmi_startup = smic_startup;
	sc->ipmi_enqueue_request = ipmi_polled_enqueue_request;
	sc->ipmi_driver_request = smic_driver_request;
	sc->ipmi_driver_requests_polled = 1;

	/* See if we can talk to the controller. */
	flags = INB(sc, SMIC_FLAGS);
	if (flags == 0xff) {
		device_printf(sc->ipmi_dev, "couldn't find it\n");
		return (ENXIO);
	}

#ifdef SMIC_DEBUG
	device_printf(sc->ipmi_dev, "SMIC: initial state: %02x\n", flags);
#endif

	return (0);
}
