/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006 Michael Lorenz
 * Copyright 2008 by Nathan Whitehorn
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/clock.h>
#include <sys/reboot.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/rman.h>

#include <dev/adb/adb.h>

#include "clock_if.h"
#include "cudavar.h"
#include "viareg.h"

/*
 * MacIO interface
 */
static int	cuda_probe(device_t);
static int	cuda_attach(device_t);
static int	cuda_detach(device_t);

static u_int	cuda_adb_send(device_t dev, u_char command_byte, int len, 
    u_char *data, u_char poll);
static u_int	cuda_adb_autopoll(device_t dev, uint16_t mask);
static u_int	cuda_poll(device_t dev);
static void	cuda_send_inbound(struct cuda_softc *sc);
static void	cuda_send_outbound(struct cuda_softc *sc);
static void	cuda_shutdown(void *xsc, int howto);

/*
 * Clock interface
 */
static int cuda_gettime(device_t dev, struct timespec *ts);
static int cuda_settime(device_t dev, struct timespec *ts);

static device_method_t  cuda_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cuda_probe),
	DEVMETHOD(device_attach,	cuda_attach),
        DEVMETHOD(device_detach,        cuda_detach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),
        DEVMETHOD(device_suspend,       bus_generic_suspend),
        DEVMETHOD(device_resume,        bus_generic_resume),

	/* ADB bus interface */
	DEVMETHOD(adb_hb_send_raw_packet,	cuda_adb_send),
	DEVMETHOD(adb_hb_controller_poll,	cuda_poll),
	DEVMETHOD(adb_hb_set_autopoll_mask,	cuda_adb_autopoll),

	/* Clock interface */
	DEVMETHOD(clock_gettime,	cuda_gettime),
	DEVMETHOD(clock_settime,	cuda_settime),

	DEVMETHOD_END
};

static driver_t cuda_driver = {
	"cuda",
	cuda_methods,
	sizeof(struct cuda_softc),
};

static devclass_t cuda_devclass;

DRIVER_MODULE(cuda, macio, cuda_driver, cuda_devclass, 0, 0);
DRIVER_MODULE(adb, cuda, adb_driver, adb_devclass, 0, 0);

static void cuda_intr(void *arg);
static uint8_t cuda_read_reg(struct cuda_softc *sc, u_int offset);
static void cuda_write_reg(struct cuda_softc *sc, u_int offset, uint8_t value);
static void cuda_idle(struct cuda_softc *);
static void cuda_tip(struct cuda_softc *);
static void cuda_clear_tip(struct cuda_softc *);
static void cuda_in(struct cuda_softc *);
static void cuda_out(struct cuda_softc *);
static void cuda_toggle_ack(struct cuda_softc *);
static void cuda_ack_off(struct cuda_softc *);
static int cuda_intr_state(struct cuda_softc *);

static int
cuda_probe(device_t dev)
{
	const char *type = ofw_bus_get_type(dev);

	if (strcmp(type, "via-cuda") != 0)
                return (ENXIO);

	device_set_desc(dev, CUDA_DEVSTR);
	return (0);
}

static int
cuda_attach(device_t dev)
{
	struct cuda_softc *sc;

	volatile int i;
	uint8_t reg;
	phandle_t node,child;
	
	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	
	sc->sc_memrid = 0;
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
	    &sc->sc_memrid, RF_ACTIVE);

	if (sc->sc_memr == NULL) {
		device_printf(dev, "Could not alloc mem resource!\n");
		return (ENXIO);
	}

	sc->sc_irqrid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irqrid,
            	RF_ACTIVE);
        if (sc->sc_irq == NULL) {
                device_printf(dev, "could not allocate interrupt\n");
                bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_memrid,
                    sc->sc_memr);
                return (ENXIO);
        }

	if (bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC | INTR_MPSAFE 
	    | INTR_ENTROPY, NULL, cuda_intr, dev, &sc->sc_ih) != 0) {
                device_printf(dev, "could not setup interrupt\n");
                bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_memrid,
                    sc->sc_memr);
                bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqrid,
                    sc->sc_irq);
                return (ENXIO);
        }

	mtx_init(&sc->sc_mutex,"cuda",NULL,MTX_DEF | MTX_RECURSE);

	sc->sc_sent = 0;
	sc->sc_received = 0;
	sc->sc_waiting = 0;
	sc->sc_polling = 0;
	sc->sc_state = CUDA_NOTREADY;
	sc->sc_autopoll = 0;
	sc->sc_rtc = -1;

	STAILQ_INIT(&sc->sc_inq);
	STAILQ_INIT(&sc->sc_outq);
	STAILQ_INIT(&sc->sc_freeq);

	for (i = 0; i < CUDA_MAXPACKETS; i++)
		STAILQ_INSERT_TAIL(&sc->sc_freeq, &sc->sc_pkts[i], pkt_q);

	/* Init CUDA */

	reg = cuda_read_reg(sc, vDirB);
	reg |= 0x30;	/* register B bits 4 and 5: outputs */
	cuda_write_reg(sc, vDirB, reg);

	reg = cuda_read_reg(sc, vDirB);
	reg &= 0xf7;	/* register B bit 3: input */
	cuda_write_reg(sc, vDirB, reg);

	reg = cuda_read_reg(sc, vACR);
	reg &= ~vSR_OUT;	/* make sure SR is set to IN */
	cuda_write_reg(sc, vACR, reg);

	cuda_write_reg(sc, vACR, (cuda_read_reg(sc, vACR) | 0x0c) & ~0x10);

	sc->sc_state = CUDA_IDLE;	/* used by all types of hardware */

	cuda_write_reg(sc, vIER, 0x84); /* make sure VIA interrupts are on */

	cuda_idle(sc);	/* reset ADB */

	/* Reset CUDA */

	i = cuda_read_reg(sc, vSR);	/* clear interrupt */
	cuda_write_reg(sc, vIER, 0x04); /* no interrupts while clearing */
	cuda_idle(sc);	/* reset state to idle */
	DELAY(150);
	cuda_tip(sc);	/* signal start of frame */
	DELAY(150);
	cuda_toggle_ack(sc);
	DELAY(150);
	cuda_clear_tip(sc);
	DELAY(150);
	cuda_idle(sc);	/* back to idle state */
	i = cuda_read_reg(sc, vSR);	/* clear interrupt */
	cuda_write_reg(sc, vIER, 0x84);	/* ints ok now */

	/* Initialize child buses (ADB) */
	node = ofw_bus_get_node(dev);

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		char name[32];

		memset(name, 0, sizeof(name));
		OF_getprop(child, "name", name, sizeof(name));

		if (bootverbose)
			device_printf(dev, "CUDA child <%s>\n",name);

		if (strncmp(name, "adb", 4) == 0) {
			sc->adb_bus = device_add_child(dev,"adb",-1);
		}
	}

	clock_register(dev, 1000);
	EVENTHANDLER_REGISTER(shutdown_final, cuda_shutdown, sc,
	    SHUTDOWN_PRI_LAST);

	return (bus_generic_attach(dev));
}

static int cuda_detach(device_t dev) {
	struct cuda_softc *sc;

	sc = device_get_softc(dev);

	bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqrid, sc->sc_irq);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_memrid, sc->sc_memr);
	mtx_destroy(&sc->sc_mutex);

	return (bus_generic_detach(dev));
}

static uint8_t
cuda_read_reg(struct cuda_softc *sc, u_int offset) {
	return (bus_read_1(sc->sc_memr, offset));
}

static void
cuda_write_reg(struct cuda_softc *sc, u_int offset, uint8_t value) {
	bus_write_1(sc->sc_memr, offset, value);
}

static void
cuda_idle(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vBufB);
	reg |= (vPB4 | vPB5);
	cuda_write_reg(sc, vBufB, reg);
}

static void
cuda_tip(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vBufB);
	reg &= ~vPB5;
	cuda_write_reg(sc, vBufB, reg);
}

static void
cuda_clear_tip(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vBufB);
	reg |= vPB5;
	cuda_write_reg(sc, vBufB, reg);
}

static void
cuda_in(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vACR);
	reg &= ~vSR_OUT;
	cuda_write_reg(sc, vACR, reg);
}

static void
cuda_out(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vACR);
	reg |= vSR_OUT;
	cuda_write_reg(sc, vACR, reg);
}

static void
cuda_toggle_ack(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vBufB);
	reg ^= vPB4;
	cuda_write_reg(sc, vBufB, reg);
}

static void
cuda_ack_off(struct cuda_softc *sc)
{
	uint8_t reg;

	reg = cuda_read_reg(sc, vBufB);
	reg |= vPB4;
	cuda_write_reg(sc, vBufB, reg);
}

static int
cuda_intr_state(struct cuda_softc *sc)
{
	return ((cuda_read_reg(sc, vBufB) & vPB3) == 0);
}

static int
cuda_send(void *cookie, int poll, int length, uint8_t *msg)
{
	struct cuda_softc *sc = cookie;
	device_t dev = sc->sc_dev;
	struct cuda_packet *pkt;

	if (sc->sc_state == CUDA_NOTREADY)
		return (-1);

	mtx_lock(&sc->sc_mutex);

	pkt = STAILQ_FIRST(&sc->sc_freeq);
	if (pkt == NULL) {
		mtx_unlock(&sc->sc_mutex);
		return (-1);
	}

	pkt->len = length - 1;
	pkt->type = msg[0];
	memcpy(pkt->data, &msg[1], pkt->len);

	STAILQ_REMOVE_HEAD(&sc->sc_freeq, pkt_q);
	STAILQ_INSERT_TAIL(&sc->sc_outq, pkt, pkt_q);

	/*
	 * If we already are sending a packet, we should bail now that this
	 * one has been added to the queue.
	 */

	if (sc->sc_waiting) {
		mtx_unlock(&sc->sc_mutex);
		return (0);
	}

	cuda_send_outbound(sc);
	mtx_unlock(&sc->sc_mutex);

	if (sc->sc_polling || poll || cold)
		cuda_poll(dev);

	return (0);
}

static void
cuda_send_outbound(struct cuda_softc *sc)
{
	struct cuda_packet *pkt;

	mtx_assert(&sc->sc_mutex, MA_OWNED);

	pkt = STAILQ_FIRST(&sc->sc_outq);
	if (pkt == NULL)
		return;

	sc->sc_out_length = pkt->len + 1;
	memcpy(sc->sc_out, &pkt->type, pkt->len + 1);
	sc->sc_sent = 0;

	STAILQ_REMOVE_HEAD(&sc->sc_outq, pkt_q);
	STAILQ_INSERT_TAIL(&sc->sc_freeq, pkt, pkt_q);

	sc->sc_waiting = 1;

	cuda_poll(sc->sc_dev);

	DELAY(150);

	if (sc->sc_state == CUDA_IDLE && !cuda_intr_state(sc)) {
		sc->sc_state = CUDA_OUT;
		cuda_out(sc);
		cuda_write_reg(sc, vSR, sc->sc_out[0]);
		cuda_ack_off(sc);
		cuda_tip(sc);
	}
}

static void
cuda_send_inbound(struct cuda_softc *sc)
{
	device_t dev;
	struct cuda_packet *pkt;

	dev = sc->sc_dev;
	
	mtx_lock(&sc->sc_mutex);

	while ((pkt = STAILQ_FIRST(&sc->sc_inq)) != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_inq, pkt_q);

		mtx_unlock(&sc->sc_mutex);

		/* check if we have a handler for this message */
		switch (pkt->type) {
		   case CUDA_ADB:
			if (pkt->len > 2) {
				adb_receive_raw_packet(sc->adb_bus,
				    pkt->data[0],pkt->data[1],
				    pkt->len - 2,&pkt->data[2]);
			} else {
				adb_receive_raw_packet(sc->adb_bus,
				    pkt->data[0],pkt->data[1],0,NULL);
			}
			break;
		   case CUDA_PSEUDO:
			mtx_lock(&sc->sc_mutex);
			switch (pkt->data[1]) {
			case CMD_AUTOPOLL:
				sc->sc_autopoll = 1;
				break;
			case CMD_READ_RTC:
				memcpy(&sc->sc_rtc, &pkt->data[2],
				    sizeof(sc->sc_rtc));
				wakeup(&sc->sc_rtc);
				break;
			case CMD_WRITE_RTC:
				break;
			}
			mtx_unlock(&sc->sc_mutex);
			break;
		   case CUDA_ERROR:
			/*
			 * CUDA will throw errors if we miss a race between
			 * sending and receiving packets. This is already
			 * handled when we abort packet output to handle
			 * this packet in cuda_intr(). Thus, we ignore
			 * these messages.
			 */
			break;
		   default:
			device_printf(dev,"unknown CUDA command %d\n",
			    pkt->type);
			break;
		}

		mtx_lock(&sc->sc_mutex);

		STAILQ_INSERT_TAIL(&sc->sc_freeq, pkt, pkt_q);
	}

	mtx_unlock(&sc->sc_mutex);
}

static u_int
cuda_poll(device_t dev)
{
	struct cuda_softc *sc = device_get_softc(dev);

	if (sc->sc_state == CUDA_IDLE && !cuda_intr_state(sc) && 
	    !sc->sc_waiting)
		return (0);

	cuda_intr(dev);
	return (0);
}

static void
cuda_intr(void *arg)
{
	device_t        dev;
	struct cuda_softc *sc;

	int i, ending, restart_send, process_inbound;
	uint8_t reg;

        dev = (device_t)arg;
	sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mutex);

	restart_send = 0;
	process_inbound = 0;
	reg = cuda_read_reg(sc, vIFR);
	if ((reg & vSR_INT) != vSR_INT) {
		mtx_unlock(&sc->sc_mutex);
		return;
	}

	cuda_write_reg(sc, vIFR, 0x7f);	/* Clear interrupt */

switch_start:
	switch (sc->sc_state) {
	case CUDA_IDLE:
		/*
		 * This is an unexpected packet, so grab the first (dummy)
		 * byte, set up the proper vars, and tell the chip we are
		 * starting to receive the packet by setting the TIP bit.
		 */
		sc->sc_in[1] = cuda_read_reg(sc, vSR);

		if (cuda_intr_state(sc) == 0) {
			/* must have been a fake start */

			if (sc->sc_waiting) {
				/* start over */
				DELAY(150);
				sc->sc_state = CUDA_OUT;
				sc->sc_sent = 0;
				cuda_out(sc);
				cuda_write_reg(sc, vSR, sc->sc_out[1]);
				cuda_ack_off(sc);
				cuda_tip(sc);
			}
			break;
		}

		cuda_in(sc);
		cuda_tip(sc);

		sc->sc_received = 1;
		sc->sc_state = CUDA_IN;
		break;

	case CUDA_IN:
		sc->sc_in[sc->sc_received] = cuda_read_reg(sc, vSR);
		ending = 0;

		if (sc->sc_received > 255) {
			/* bitch only once */
			if (sc->sc_received == 256) {
				device_printf(dev,"input overflow\n");
				ending = 1;
			}
		} else
			sc->sc_received++;

		/* intr off means this is the last byte (end of frame) */
		if (cuda_intr_state(sc) == 0) {
			ending = 1;
		} else {
			cuda_toggle_ack(sc);			
		}
		
		if (ending == 1) {	/* end of message? */
			struct cuda_packet *pkt;

			/* reset vars and signal the end of this frame */
			cuda_idle(sc);

			/* Queue up the packet */
			pkt = STAILQ_FIRST(&sc->sc_freeq);
			if (pkt != NULL) {
				/* If we have a free packet, process it */

				pkt->len = sc->sc_received - 2;
				pkt->type = sc->sc_in[1];
				memcpy(pkt->data, &sc->sc_in[2], pkt->len);

				STAILQ_REMOVE_HEAD(&sc->sc_freeq, pkt_q);
				STAILQ_INSERT_TAIL(&sc->sc_inq, pkt, pkt_q);

				process_inbound = 1;
			}

			sc->sc_state = CUDA_IDLE;
			sc->sc_received = 0;

			/*
			 * If there is something waiting to be sent out,
			 * set everything up and send the first byte.
			 */
			if (sc->sc_waiting == 1) {
				DELAY(1500);	/* required */
				sc->sc_sent = 0;
				sc->sc_state = CUDA_OUT;

				/*
				 * If the interrupt is on, we were too slow
				 * and the chip has already started to send
				 * something to us, so back out of the write
				 * and start a read cycle.
				 */
				if (cuda_intr_state(sc)) {
					cuda_in(sc);
					cuda_idle(sc);
					sc->sc_sent = 0;
					sc->sc_state = CUDA_IDLE;
					sc->sc_received = 0;
					DELAY(150);
					goto switch_start;
				}

				/*
				 * If we got here, it's ok to start sending
				 * so load the first byte and tell the chip
				 * we want to send.
				 */
				cuda_out(sc);
				cuda_write_reg(sc, vSR,
				    sc->sc_out[sc->sc_sent]);
				cuda_ack_off(sc);
				cuda_tip(sc);
			}
		}
		break;

	case CUDA_OUT:
		i = cuda_read_reg(sc, vSR);	/* reset SR-intr in IFR */

		sc->sc_sent++;
		if (cuda_intr_state(sc)) {	/* ADB intr low during write */
			cuda_in(sc);	/* make sure SR is set to IN */
			cuda_idle(sc);
			sc->sc_sent = 0;	/* must start all over */
			sc->sc_state = CUDA_IDLE;	/* new state */
			sc->sc_received = 0;
			sc->sc_waiting = 1;	/* must retry when done with
						 * read */
			DELAY(150);
			goto switch_start;	/* process next state right
						 * now */
			break;
		}
		if (sc->sc_out_length == sc->sc_sent) {	/* check for done */
			sc->sc_waiting = 0;	/* done writing */
			sc->sc_state = CUDA_IDLE;	/* signal bus is idle */
			cuda_in(sc);
			cuda_idle(sc);
		} else {
			/* send next byte */
			cuda_write_reg(sc, vSR, sc->sc_out[sc->sc_sent]);
			cuda_toggle_ack(sc);	/* signal byte ready to
							 * shift */
		}
		break;

	case CUDA_NOTREADY:
		break;

	default:
		break;
	}

	mtx_unlock(&sc->sc_mutex);

	if (process_inbound)
		cuda_send_inbound(sc);

	mtx_lock(&sc->sc_mutex);
	/* If we have another packet waiting, set it up */
	if (!sc->sc_waiting && sc->sc_state == CUDA_IDLE)
		cuda_send_outbound(sc);

	mtx_unlock(&sc->sc_mutex);

}

static u_int
cuda_adb_send(device_t dev, u_char command_byte, int len, u_char *data, 
    u_char poll)
{
	struct cuda_softc *sc = device_get_softc(dev);
	uint8_t packet[16];
	int i;

	/* construct an ADB command packet and send it */
	packet[0] = CUDA_ADB;
	packet[1] = command_byte;
	for (i = 0; i < len; i++)
		packet[i + 2] = data[i];

	cuda_send(sc, poll, len + 2, packet);

	return (0);
}

static u_int 
cuda_adb_autopoll(device_t dev, uint16_t mask) {
	struct cuda_softc *sc = device_get_softc(dev);

	uint8_t cmd[] = {CUDA_PSEUDO, CMD_AUTOPOLL, mask != 0};

	mtx_lock(&sc->sc_mutex);

	if (cmd[2] == sc->sc_autopoll) {
		mtx_unlock(&sc->sc_mutex);
		return (0);
	}

	sc->sc_autopoll = -1;
	cuda_send(sc, 1, 3, cmd);

	mtx_unlock(&sc->sc_mutex);

	return (0);
}

static void
cuda_shutdown(void *xsc, int howto)
{
	struct cuda_softc *sc = xsc;
	uint8_t cmd[] = {CUDA_PSEUDO, 0};

	cmd[1] = (howto & RB_HALT) ? CMD_POWEROFF : CMD_RESET;
	cuda_poll(sc->sc_dev);
	cuda_send(sc, 1, 2, cmd);

	while (1)
		cuda_poll(sc->sc_dev);
}

#define DIFF19041970	2082844800

static int
cuda_gettime(device_t dev, struct timespec *ts)
{
	struct cuda_softc *sc = device_get_softc(dev);
	uint8_t cmd[] = {CUDA_PSEUDO, CMD_READ_RTC};

	mtx_lock(&sc->sc_mutex);
	sc->sc_rtc = -1;
	cuda_send(sc, 1, 2, cmd);
	if (sc->sc_rtc == -1)
		mtx_sleep(&sc->sc_rtc, &sc->sc_mutex, 0, "rtc", 100);

	ts->tv_sec = sc->sc_rtc - DIFF19041970;
	ts->tv_nsec = 0;
	mtx_unlock(&sc->sc_mutex);

	return (0);
}

static int
cuda_settime(device_t dev, struct timespec *ts)
{
	struct cuda_softc *sc = device_get_softc(dev);
	uint8_t cmd[] = {CUDA_PSEUDO, CMD_WRITE_RTC, 0, 0, 0, 0};
	uint32_t sec;

	sec = ts->tv_sec + DIFF19041970;
	memcpy(&cmd[2], &sec, sizeof(sec));

	mtx_lock(&sc->sc_mutex);
	cuda_send(sc, 0, 6, cmd);
	mtx_unlock(&sc->sc_mutex);

	return (0);
}

