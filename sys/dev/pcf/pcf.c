/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998 Nicolas Souchu, Marc Bouget
 * Copyright (c) 2004 Joerg Wunsch
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
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#include <dev/pcf/pcfvar.h>
#include "iicbus_if.h"

/* Not so official debugging option. */
/* #define PCFDEBUG */

static int pcf_wait_byte(struct pcf_softc *pcf);
static int pcf_noack(struct pcf_softc *pcf, int timeout);
static void pcf_stop_locked(struct pcf_softc *pcf);

/*
 * Polling mode for master operations wait for a new
 * byte incoming or outgoing
 */
static int
pcf_wait_byte(struct pcf_softc *sc)
{
	int counter = TIMEOUT;

	PCF_ASSERT_LOCKED(sc);
	while (counter--) {

		if ((pcf_get_S1(sc) & PIN) == 0)
			return (0);
	}

#ifdef PCFDEBUG
	printf("pcf: timeout!\n");
#endif

	return (IIC_ETIMEOUT);
}

static void
pcf_stop_locked(struct pcf_softc *sc)
{

	PCF_ASSERT_LOCKED(sc);
#ifdef PCFDEBUG
	device_printf(dev, " >> stop\n");
#endif
	/*
	 * Send STOP condition iff the START condition was previously sent.
	 * STOP is sent only once even if an iicbus_stop() is called after
	 * an iicbus_read()... see pcf_read(): the PCF needs to send the stop
	 * before the last char is read.
	 */
	if (sc->pcf_started) {
		/* set stop condition and enable IT */
		pcf_set_S1(sc, PIN|ESO|ENI|STO|ACK);

		sc->pcf_started = 0;
	}
}

static int
pcf_noack(struct pcf_softc *sc, int timeout)
{
	int noack;
	int k = timeout/10;

	PCF_ASSERT_LOCKED(sc);
	do {
		noack = pcf_get_S1(sc) & LRB;
		if (!noack)
			break;
		DELAY(10);				/* XXX wait 10 us */
	} while (k--);

	return (noack);
}

int
pcf_repeated_start(device_t dev, u_char slave, int timeout)
{
	struct pcf_softc *sc = DEVTOSOFTC(dev);
	int error = 0;

	PCF_LOCK(sc);
#ifdef PCFDEBUG
	device_printf(dev, " >> repeated start for slave %#x\n",
		      (unsigned)slave);
#endif
	/* repeated start */
	pcf_set_S1(sc, ESO|STA|STO|ACK);

	/* set slave address to PCF. Last bit (LSB) must be set correctly
	 * according to transfer direction */
	pcf_set_S0(sc, slave);

	/* wait for address sent, polling */
	if ((error = pcf_wait_byte(sc)))
		goto error;

	/* check for ack */
	if (pcf_noack(sc, timeout)) {
		error = IIC_ENOACK;
#ifdef PCFDEBUG
		printf("pcf: no ack on repeated_start!\n");
#endif
		goto error;
	}

	PCF_UNLOCK(sc);
	return (0);

error:
	pcf_stop_locked(sc);
	PCF_UNLOCK(sc);
	return (error);
}

int
pcf_start(device_t dev, u_char slave, int timeout)
{
	struct pcf_softc *sc = DEVTOSOFTC(dev);
	int error = 0;

	PCF_LOCK(sc);
#ifdef PCFDEBUG
	device_printf(dev, " >> start for slave %#x\n", (unsigned)slave);
#endif
	if ((pcf_get_S1(sc) & nBB) == 0) {
#ifdef PCFDEBUG
		printf("pcf: busy!\n");
#endif
		PCF_UNLOCK(sc);
		return (IIC_EBUSERR);
	}

	/* set slave address to PCF. Last bit (LSB) must be set correctly
	 * according to transfer direction */
	pcf_set_S0(sc, slave);

	/* START only */
	pcf_set_S1(sc, PIN|ESO|STA|ACK);

	sc->pcf_started = 1;

	/* wait for address sent, polling */
	if ((error = pcf_wait_byte(sc)))
		goto error;

	/* check for ACK */
	if (pcf_noack(sc, timeout)) {
		error = IIC_ENOACK;
#ifdef PCFDEBUG
		printf("pcf: no ack on start!\n");
#endif
		goto error;
	}

	PCF_UNLOCK(sc);
	return (0);

error:
	pcf_stop_locked(sc);
	PCF_UNLOCK(sc);
	return (error);
}

int
pcf_stop(device_t dev)
{
	struct pcf_softc *sc = DEVTOSOFTC(dev);

#ifdef PCFDEBUG
	device_printf(dev, " >> stop\n");
#endif
	PCF_LOCK(sc);
	pcf_stop_locked(sc);
	PCF_UNLOCK(sc);

	return (0);
}

void
pcf_intr(void *arg)
{
	struct pcf_softc *sc = arg;
	char data, status, addr;
	char error = 0;

	PCF_LOCK(sc);
	status = pcf_get_S1(sc);

	if (status & PIN) {
		printf("pcf: spurious interrupt, status=0x%x\n",
		       status & 0xff);

		goto error;
	}

	if (status & LAB)
		printf("pcf: bus arbitration lost!\n");

	if (status & BER) {
		error = IIC_EBUSERR;
		iicbus_intr(sc->iicbus, INTR_ERROR, &error);

		goto error;
	}

	do {
		status = pcf_get_S1(sc);

		switch(sc->pcf_slave_mode) {

		case SLAVE_TRANSMITTER:
			if (status & LRB) {
				/* ack interrupt line */
				dummy_write(sc);

				/* no ack, don't send anymore */
				sc->pcf_slave_mode = SLAVE_RECEIVER;

				iicbus_intr(sc->iicbus, INTR_NOACK, NULL);
				break;
			}

			/* get data from upper code */
			iicbus_intr(sc->iicbus, INTR_TRANSMIT, &data);

			pcf_set_S0(sc, data);
			break;

		case SLAVE_RECEIVER:
			if (status & AAS) {
				addr = pcf_get_S0(sc);

				if (status & AD0)
					iicbus_intr(sc->iicbus, INTR_GENERAL, &addr);
				else
					iicbus_intr(sc->iicbus, INTR_START, &addr);

				if (addr & LSB) {
					sc->pcf_slave_mode = SLAVE_TRANSMITTER;

					/* get the first char from upper code */
					iicbus_intr(sc->iicbus, INTR_TRANSMIT, &data);

					/* send first data byte */
					pcf_set_S0(sc, data);
				}

				break;
			}

			/* stop condition received? */
			if (status & STS) {
				/* ack interrupt line */
				dummy_read(sc);

				/* emulate intr stop condition */
				iicbus_intr(sc->iicbus, INTR_STOP, NULL);

			} else {
				/* get data, ack interrupt line */
				data = pcf_get_S0(sc);

				/* deliver the character */
				iicbus_intr(sc->iicbus, INTR_RECEIVE, &data);
			}
			break;

		    default:
			panic("%s: unknown slave mode (%d)!", __func__,
				sc->pcf_slave_mode);
		    }

	} while ((pcf_get_S1(sc) & PIN) == 0);
	PCF_UNLOCK(sc);

	return;

error:
	/* unknown event on bus...reset PCF */
	pcf_set_S1(sc, PIN|ESO|ENI|ACK);

	sc->pcf_slave_mode = SLAVE_RECEIVER;
	PCF_UNLOCK(sc);

	return;
}

int
pcf_rst_card(device_t dev, u_char speed, u_char addr, u_char *oldaddr)
{
	struct pcf_softc *sc = DEVTOSOFTC(dev);

	PCF_LOCK(sc);
	if (oldaddr)
		*oldaddr = sc->pcf_addr;

	/* retrieve own address from bus level */
	if (!addr)
		sc->pcf_addr = PCF_DEFAULT_ADDR;
	else
		sc->pcf_addr = addr;

	pcf_set_S1(sc, PIN);				/* initialize S1 */

	/* own address S'O<>0 */
	pcf_set_S0(sc, sc->pcf_addr >> 1);

	/* select clock register */
	pcf_set_S1(sc, PIN|ES1);

	/* select bus speed : 18=90kb, 19=45kb, 1A=11kb, 1B=1.5kb */
	switch (speed) {
	case IIC_SLOW:
		pcf_set_S0(sc,  0x1b); /* XXX Sun uses 0x1f */
		break;

	case IIC_FAST:
		pcf_set_S0(sc,  0x19); /* XXX Sun: 0x1d */
		break;

	case IIC_UNKNOWN:
	case IIC_FASTEST:
	default:
		pcf_set_S0(sc,  0x18); /* XXX Sun: 0x1c */
		break;
	}

	/* set bus on, ack=yes, INT=yes */
	pcf_set_S1(sc, PIN|ESO|ENI|ACK);

	sc->pcf_slave_mode = SLAVE_RECEIVER;
	PCF_UNLOCK(sc);

	return (0);
}

int
pcf_write(device_t dev, const char *buf, int len, int *sent, int timeout /* us */)
{
	struct pcf_softc *sc = DEVTOSOFTC(dev);
	int bytes, error = 0;

#ifdef PCFDEBUG
	device_printf(dev, " >> writing %d bytes: %#x%s\n", len,
		      (unsigned)buf[0], len > 1? "...": "");
#endif

	bytes = 0;
	PCF_LOCK(sc);
	while (len) {

		pcf_set_S0(sc, *buf++);

		/* wait for the byte to be send */
		if ((error = pcf_wait_byte(sc)))
			goto error;

		/* check if ack received */
		if (pcf_noack(sc, timeout)) {
			error = IIC_ENOACK;
			goto error;
		}

		len --;
		bytes ++;
	}

error:
	*sent = bytes;
	PCF_UNLOCK(sc);

#ifdef PCFDEBUG
	device_printf(dev, " >> %d bytes written (%d)\n", bytes, error);
#endif

	return (error);
}

int
pcf_read(device_t dev, char *buf, int len, int *read, int last,
	 int delay /* us */)
{
	struct pcf_softc *sc = DEVTOSOFTC(dev);
	int bytes, error = 0;
#ifdef PCFDEBUG
	char *obuf = buf;

	device_printf(dev, " << reading %d bytes\n", len);
#endif

	PCF_LOCK(sc);
	/* trig the bus to get the first data byte in S0 */
	if (len) {
		if (len == 1 && last)
			/* just one byte to read */
			pcf_set_S1(sc, ESO);		/* no ack */

		dummy_read(sc);
	}

	bytes = 0;
	while (len) {

		/* XXX delay needed here */

		/* wait for trigged byte */
		if ((error = pcf_wait_byte(sc))) {
			pcf_stop_locked(sc);
			goto error;
		}

		if (len == 1 && last)
			/* ok, last data byte already in S0, no I2C activity
			 * on next pcf_get_S0() */
			pcf_stop_locked(sc);

		else if (len == 2 && last)
			/* next trigged byte with no ack */
			pcf_set_S1(sc, ESO);

		/* receive byte, trig next byte */
		*buf++ = pcf_get_S0(sc);

		len --;
		bytes ++;
	}

error:
	*read = bytes;
	PCF_UNLOCK(sc);

#ifdef PCFDEBUG
	device_printf(dev, " << %d bytes read (%d): %#x%s\n", bytes, error,
		      (unsigned)obuf[0], bytes > 1? "...": "");
#endif

	return (error);
}

DRIVER_MODULE(iicbus, pcf, iicbus_driver, iicbus_devclass, 0, 0);
MODULE_DEPEND(pcf, iicbus, PCF_MINVER, PCF_PREFVER, PCF_MAXVER);
MODULE_VERSION(pcf, PCF_MODVER);
