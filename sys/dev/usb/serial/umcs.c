/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Lev Serebryakov <lev@FreeBSD.org>.
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

/*
 * This driver supports several multiport USB-to-RS232 serial adapters driven
 * by MosChip mos7820 and mos7840, bridge chips.
 * The adapters are sold under many different brand names.
 *
 * Datasheets are available at MosChip www site at
 * http://www.moschip.com.  The datasheets don't contain full
 * programming information for the chip.
 *
 * It is nornal to have only two enabled ports in devices, based on
 * quad-port mos7840.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/linker_set.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_cdc.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR umcs_debug
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>

#include <dev/usb/serial/usb_serial.h>

#include <dev/usb/serial/umcs.h>

#define	UMCS7840_MODVER	1

#ifdef USB_DEBUG
static int umcs_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, umcs, CTLFLAG_RW, 0, "USB umcs quadport serial adapter");
SYSCTL_INT(_hw_usb_umcs, OID_AUTO, debug, CTLFLAG_RWTUN, &umcs_debug, 0, "Debug level");
#endif					/* USB_DEBUG */


/*
 * Two-port devices (both with 7820 chip and 7840 chip configured as two-port)
 * have ports 0 and 2, with ports 1 and 3 omitted.
 * So,PHYSICAL port numbers (indexes) on two-port device will be 0 and 2.
 * This driver trys to use physical numbers as much as possible.
 */

/*
 * Indexed by PHYSICAL port number.
 * Pack non-regular registers to array to easier if-less access.
 */
struct umcs7840_port_registers {
	uint8_t	reg_sp;			/* SP register. */
	uint8_t	reg_control;		/* CONTROL register. */
	uint8_t	reg_dcr;		/* DCR0 register. DCR1 & DCR2 can be
					 * calculated */
};

static const struct umcs7840_port_registers umcs7840_port_registers[UMCS7840_MAX_PORTS] = {
	{.reg_sp = MCS7840_DEV_REG_SP1,.reg_control = MCS7840_DEV_REG_CONTROL1,.reg_dcr = MCS7840_DEV_REG_DCR0_1},
	{.reg_sp = MCS7840_DEV_REG_SP2,.reg_control = MCS7840_DEV_REG_CONTROL2,.reg_dcr = MCS7840_DEV_REG_DCR0_2},
	{.reg_sp = MCS7840_DEV_REG_SP3,.reg_control = MCS7840_DEV_REG_CONTROL3,.reg_dcr = MCS7840_DEV_REG_DCR0_3},
	{.reg_sp = MCS7840_DEV_REG_SP4,.reg_control = MCS7840_DEV_REG_CONTROL4,.reg_dcr = MCS7840_DEV_REG_DCR0_4},
};

enum {
	UMCS7840_BULK_RD_EP,
	UMCS7840_BULK_WR_EP,
	UMCS7840_N_TRANSFERS
};

struct umcs7840_softc_oneport {
	struct usb_xfer *sc_xfer[UMCS7840_N_TRANSFERS];	/* Control structures
							 * for two transfers */

	uint8_t	sc_lcr;			/* local line control register */
	uint8_t	sc_mcr;			/* local modem control register */
};

struct umcs7840_softc {
	struct ucom_super_softc sc_super_ucom;
	struct ucom_softc sc_ucom[UMCS7840_MAX_PORTS];	/* Need to be continuous
							 * array, so indexed by
							 * LOGICAL port
							 * (subunit) number */

	struct usb_xfer *sc_intr_xfer;	/* Interrupt endpoint */

	device_t sc_dev;		/* Device for error prints */
	struct usb_device *sc_udev;	/* USB Device for all operations */
	struct mtx sc_mtx;		/* ucom requires this */

	uint8_t	sc_driver_done;		/* Flag when enumeration is finished */

	uint8_t	sc_numports;		/* Number of ports (subunits) */
	struct umcs7840_softc_oneport sc_ports[UMCS7840_MAX_PORTS];	/* Indexed by PHYSICAL
									 * port number. */
};

/* prototypes */
static usb_error_t umcs7840_get_reg_sync(struct umcs7840_softc *, uint8_t, uint8_t *);
static usb_error_t umcs7840_set_reg_sync(struct umcs7840_softc *, uint8_t, uint8_t);
static usb_error_t umcs7840_get_UART_reg_sync(struct umcs7840_softc *, uint8_t, uint8_t, uint8_t *);
static usb_error_t umcs7840_set_UART_reg_sync(struct umcs7840_softc *, uint8_t, uint8_t, uint8_t);

static usb_error_t umcs7840_set_baudrate(struct umcs7840_softc *, uint8_t, uint32_t);
static usb_error_t umcs7840_calc_baudrate(uint32_t rate, uint16_t *, uint8_t *);

static void	umcs7840_free(struct ucom_softc *);
static void umcs7840_cfg_get_status(struct ucom_softc *, uint8_t *, uint8_t *);
static void umcs7840_cfg_set_dtr(struct ucom_softc *, uint8_t);
static void umcs7840_cfg_set_rts(struct ucom_softc *, uint8_t);
static void umcs7840_cfg_set_break(struct ucom_softc *, uint8_t);
static void umcs7840_cfg_param(struct ucom_softc *, struct termios *);
static void umcs7840_cfg_open(struct ucom_softc *);
static void umcs7840_cfg_close(struct ucom_softc *);

static int umcs7840_pre_param(struct ucom_softc *, struct termios *);

static void umcs7840_start_read(struct ucom_softc *);
static void umcs7840_stop_read(struct ucom_softc *);

static void umcs7840_start_write(struct ucom_softc *);
static void umcs7840_stop_write(struct ucom_softc *);

static void umcs7840_poll(struct ucom_softc *ucom);

static device_probe_t umcs7840_probe;
static device_attach_t umcs7840_attach;
static device_detach_t umcs7840_detach;
static void umcs7840_free_softc(struct umcs7840_softc *);

static usb_callback_t umcs7840_intr_callback;
static usb_callback_t umcs7840_read_callback1;
static usb_callback_t umcs7840_read_callback2;
static usb_callback_t umcs7840_read_callback3;
static usb_callback_t umcs7840_read_callback4;
static usb_callback_t umcs7840_write_callback1;
static usb_callback_t umcs7840_write_callback2;
static usb_callback_t umcs7840_write_callback3;
static usb_callback_t umcs7840_write_callback4;

static void umcs7840_read_callbackN(struct usb_xfer *, usb_error_t, uint8_t);
static void umcs7840_write_callbackN(struct usb_xfer *, usb_error_t, uint8_t);

/* Indexed by LOGICAL port number (subunit), so two-port device uses 0 & 1 */
static usb_callback_t *umcs7840_rw_callbacks[UMCS7840_MAX_PORTS][UMCS7840_N_TRANSFERS] = {
	{&umcs7840_read_callback1, &umcs7840_write_callback1},
	{&umcs7840_read_callback2, &umcs7840_write_callback2},
	{&umcs7840_read_callback3, &umcs7840_write_callback3},
	{&umcs7840_read_callback4, &umcs7840_write_callback4},
};

static const struct usb_config umcs7840_bulk_config_data[UMCS7840_N_TRANSFERS] = {
	[UMCS7840_BULK_RD_EP] = {
		.type = UE_BULK,
		.endpoint = 0x01,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,		/* use wMaxPacketSize */
		.callback = &umcs7840_read_callback1,
		.if_index = 0,
	},

	[UMCS7840_BULK_WR_EP] = {
		.type = UE_BULK,
		.endpoint = 0x02,
		.direction = UE_DIR_OUT,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,		/* use wMaxPacketSize */
		.callback = &umcs7840_write_callback1,
		.if_index = 0,
	},
};

static const struct usb_config umcs7840_intr_config_data[1] = {
	[0] = {
		.type = UE_INTERRUPT,
		.endpoint = 0x09,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,		/* use wMaxPacketSize */
		.callback = &umcs7840_intr_callback,
		.if_index = 0,
	},
};

static struct ucom_callback umcs7840_callback = {
	.ucom_cfg_get_status = &umcs7840_cfg_get_status,

	.ucom_cfg_set_dtr = &umcs7840_cfg_set_dtr,
	.ucom_cfg_set_rts = &umcs7840_cfg_set_rts,
	.ucom_cfg_set_break = &umcs7840_cfg_set_break,

	.ucom_cfg_param = &umcs7840_cfg_param,
	.ucom_cfg_open = &umcs7840_cfg_open,
	.ucom_cfg_close = &umcs7840_cfg_close,

	.ucom_pre_param = &umcs7840_pre_param,

	.ucom_start_read = &umcs7840_start_read,
	.ucom_stop_read = &umcs7840_stop_read,

	.ucom_start_write = &umcs7840_start_write,
	.ucom_stop_write = &umcs7840_stop_write,

	.ucom_poll = &umcs7840_poll,
	.ucom_free = &umcs7840_free,
};

static const STRUCT_USB_HOST_ID umcs7840_devs[] = {
	{USB_VPI(USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7820, 0)},
	{USB_VPI(USB_VENDOR_MOSCHIP, USB_PRODUCT_MOSCHIP_MCS7840, 0)},
};

static device_method_t umcs7840_methods[] = {
	DEVMETHOD(device_probe, umcs7840_probe),
	DEVMETHOD(device_attach, umcs7840_attach),
	DEVMETHOD(device_detach, umcs7840_detach),
	DEVMETHOD_END
};

static devclass_t umcs7840_devclass;

static driver_t umcs7840_driver = {
	.name = "umcs7840",
	.methods = umcs7840_methods,
	.size = sizeof(struct umcs7840_softc),
};

DRIVER_MODULE(umcs7840, uhub, umcs7840_driver, umcs7840_devclass, 0, 0);
MODULE_DEPEND(umcs7840, ucom, 1, 1, 1);
MODULE_DEPEND(umcs7840, usb, 1, 1, 1);
MODULE_VERSION(umcs7840, UMCS7840_MODVER);
USB_PNP_HOST_INFO(umcs7840_devs);

static int
umcs7840_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != MCS7840_CONFIG_INDEX)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != MCS7840_IFACE_INDEX)
		return (ENXIO);
	return (usbd_lookup_id_by_uaa(umcs7840_devs, sizeof(umcs7840_devs), uaa));
}

static int
umcs7840_attach(device_t dev)
{
	struct usb_config umcs7840_config_tmp[UMCS7840_N_TRANSFERS];
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct umcs7840_softc *sc = device_get_softc(dev);

	uint8_t iface_index = MCS7840_IFACE_INDEX;
	int error;
	int subunit;
	int n;
	uint8_t data;

	for (n = 0; n < UMCS7840_N_TRANSFERS; ++n)
		umcs7840_config_tmp[n] = umcs7840_bulk_config_data[n];

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "umcs7840", NULL, MTX_DEF);
	ucom_ref(&sc->sc_super_ucom);

	sc->sc_dev = dev;
	sc->sc_udev = uaa->device;

	/*
	 * Get number of ports
	 * Documentation (full datasheet) says, that number of ports is
	 * set as MCS7840_DEV_MODE_SELECT24S bit in MODE R/Only
	 * register. But vendor driver uses these undocumented
	 * register & bit.
	 *
	 * Experiments show, that MODE register can have `0'
	 * (4 ports) bit on 2-port device, so use vendor driver's way.
	 *
	 * Also, see notes in header file for these constants.
	 */
	umcs7840_get_reg_sync(sc, MCS7840_DEV_REG_GPIO, &data);
	if (data & MCS7840_DEV_GPIO_4PORTS) {
		sc->sc_numports = 4;
		/* Store physical port numbers in sc_portno */
		sc->sc_ucom[0].sc_portno = 0;
		sc->sc_ucom[1].sc_portno = 1;
		sc->sc_ucom[2].sc_portno = 2;
		sc->sc_ucom[3].sc_portno = 3;
	} else {
		sc->sc_numports = 2;
		/* Store physical port numbers in sc_portno */
		sc->sc_ucom[0].sc_portno = 0;
		sc->sc_ucom[1].sc_portno = 2;	/* '1' is skipped */
	}
	device_printf(dev, "Chip mcs%04x, found %d active ports\n", uaa->info.idProduct, sc->sc_numports);
	if (!umcs7840_get_reg_sync(sc, MCS7840_DEV_REG_MODE, &data)) {
		device_printf(dev, "On-die confguration: RST: active %s, HRD: %s, PLL: %s, POR: %s, Ports: %s, EEPROM write %s, IrDA is %savailable\n",
		    (data & MCS7840_DEV_MODE_RESET) ? "low" : "high",
		    (data & MCS7840_DEV_MODE_SER_PRSNT) ? "yes" : "no",
		    (data & MCS7840_DEV_MODE_PLLBYPASS) ? "bypassed" : "avail",
		    (data & MCS7840_DEV_MODE_PORBYPASS) ? "bypassed" : "avail",
		    (data & MCS7840_DEV_MODE_SELECT24S) ? "2" : "4",
		    (data & MCS7840_DEV_MODE_EEPROMWR) ? "enabled" : "disabled",
		    (data & MCS7840_DEV_MODE_IRDA) ? "" : "not ");
	}
	/* Setup all transfers */
	for (subunit = 0; subunit < sc->sc_numports; ++subunit) {
		for (n = 0; n < UMCS7840_N_TRANSFERS; ++n) {
			/* Set endpoint address */
			umcs7840_config_tmp[n].endpoint = umcs7840_bulk_config_data[n].endpoint + 2 * sc->sc_ucom[subunit].sc_portno;
			umcs7840_config_tmp[n].callback = umcs7840_rw_callbacks[subunit][n];
		}
		error = usbd_transfer_setup(uaa->device,
		    &iface_index, sc->sc_ports[sc->sc_ucom[subunit].sc_portno].sc_xfer, umcs7840_config_tmp,
		    UMCS7840_N_TRANSFERS, sc, &sc->sc_mtx);
		if (error) {
			device_printf(dev, "allocating USB transfers failed for subunit %d of %d\n",
			    subunit + 1, sc->sc_numports);
			goto detach;
		}
	}
	error = usbd_transfer_setup(uaa->device,
	    &iface_index, &sc->sc_intr_xfer, umcs7840_intr_config_data,
	    1, sc, &sc->sc_mtx);
	if (error) {
		device_printf(dev, "allocating USB transfers failed for interrupt\n");
		goto detach;
	}
	/* clear stall at first run */
	mtx_lock(&sc->sc_mtx);
	for (subunit = 0; subunit < sc->sc_numports; ++subunit) {
		usbd_xfer_set_stall(sc->sc_ports[sc->sc_ucom[subunit].sc_portno].sc_xfer[UMCS7840_BULK_RD_EP]);
		usbd_xfer_set_stall(sc->sc_ports[sc->sc_ucom[subunit].sc_portno].sc_xfer[UMCS7840_BULK_WR_EP]);
	}
	mtx_unlock(&sc->sc_mtx);

	error = ucom_attach(&sc->sc_super_ucom, sc->sc_ucom, sc->sc_numports, sc,
	    &umcs7840_callback, &sc->sc_mtx);
	if (error)
		goto detach;

	ucom_set_pnpinfo_usb(&sc->sc_super_ucom, dev);

	return (0);

detach:
	umcs7840_detach(dev);
	return (ENXIO);
}

static int
umcs7840_detach(device_t dev)
{
	struct umcs7840_softc *sc = device_get_softc(dev);
	int subunit;

	ucom_detach(&sc->sc_super_ucom, sc->sc_ucom);

	for (subunit = 0; subunit < sc->sc_numports; ++subunit)
		usbd_transfer_unsetup(sc->sc_ports[sc->sc_ucom[subunit].sc_portno].sc_xfer, UMCS7840_N_TRANSFERS);
	usbd_transfer_unsetup(&sc->sc_intr_xfer, 1);

	device_claim_softc(dev);

	umcs7840_free_softc(sc);

	return (0);
}

UCOM_UNLOAD_DRAIN(umcs7840);

static void
umcs7840_free_softc(struct umcs7840_softc *sc)
{
	if (ucom_unref(&sc->sc_super_ucom)) {
		mtx_destroy(&sc->sc_mtx);
		device_free_softc(sc);
	}
}

static void
umcs7840_free(struct ucom_softc *ucom)
{
	umcs7840_free_softc(ucom->sc_parent);
}

static void
umcs7840_cfg_open(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint16_t pn = ucom->sc_portno;
	uint8_t data;

	/* If it very first open, finish global configuration */
	if (!sc->sc_driver_done) {
		/*
		 * USB enumeration is finished, pass internal memory to FIFOs
		 * If it is done in the end of "attach", kernel panics.
		 */
		if (umcs7840_get_reg_sync(sc, MCS7840_DEV_REG_CONTROL1, &data))
			return;
		data |= MCS7840_DEV_CONTROL1_DRIVER_DONE;
		if (umcs7840_set_reg_sync(sc, MCS7840_DEV_REG_CONTROL1, data))
			return;
		sc->sc_driver_done = 1;
	}
	/* Toggle reset bit on-off */
	if (umcs7840_get_reg_sync(sc, umcs7840_port_registers[pn].reg_sp, &data))
		return;
	data |= MCS7840_DEV_SPx_UART_RESET;
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_sp, data))
		return;
	data &= ~MCS7840_DEV_SPx_UART_RESET;
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_sp, data))
		return;

	/* Set RS-232 mode */
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_SCRATCHPAD, MCS7840_UART_SCRATCHPAD_RS232))
		return;

	/* Disable RX on time of initialization */
	if (umcs7840_get_reg_sync(sc, umcs7840_port_registers[pn].reg_control, &data))
		return;
	data |= MCS7840_DEV_CONTROLx_RX_DISABLE;
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_control, data))
		return;

	/* Disable all interrupts */
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_IER, 0))
		return;

	/* Reset FIFO -- documented */
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_FCR, 0))
		return;
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_FCR,
	    MCS7840_UART_FCR_ENABLE | MCS7840_UART_FCR_FLUSHRHR |
	    MCS7840_UART_FCR_FLUSHTHR | MCS7840_UART_FCR_RTL_1_14))
		return;

	/* Set 8 bit, no parity, 1 stop bit -- documented */
	sc->sc_ports[pn].sc_lcr = MCS7840_UART_LCR_DATALEN8 | MCS7840_UART_LCR_STOPB1;
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_LCR, sc->sc_ports[pn].sc_lcr))
		return;

	/*
	 * Enable DTR/RTS on modem control, enable modem interrupts --
	 * documented
	 */
	sc->sc_ports[pn].sc_mcr = MCS7840_UART_MCR_DTR | MCS7840_UART_MCR_RTS | MCS7840_UART_MCR_IE;
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_MCR, sc->sc_ports[pn].sc_mcr))
		return;

	/* Clearing Bulkin and Bulkout FIFO */
	if (umcs7840_get_reg_sync(sc, umcs7840_port_registers[pn].reg_sp, &data))
		return;
	data |= MCS7840_DEV_SPx_RESET_OUT_FIFO | MCS7840_DEV_SPx_RESET_IN_FIFO;
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_sp, data))
		return;
	data &= ~(MCS7840_DEV_SPx_RESET_OUT_FIFO | MCS7840_DEV_SPx_RESET_IN_FIFO);
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_sp, data))
		return;

	/* Set speed 9600 */
	if (umcs7840_set_baudrate(sc, pn, 9600))
		return;


	/* Finally enable all interrupts -- documented */
	/*
	 * Copied from vendor driver, I don't know why we should read LCR
	 * here
	 */
	if (umcs7840_get_UART_reg_sync(sc, pn, MCS7840_UART_REG_LCR, &sc->sc_ports[pn].sc_lcr))
		return;
	if (umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_IER,
	    MCS7840_UART_IER_RXSTAT | MCS7840_UART_IER_MODEM))
		return;

	/* Enable RX */
	if (umcs7840_get_reg_sync(sc, umcs7840_port_registers[pn].reg_control, &data))
		return;
	data &= ~MCS7840_DEV_CONTROLx_RX_DISABLE;
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_control, data))
		return;

	DPRINTF("Port %d has been opened\n", pn);
}

static void
umcs7840_cfg_close(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint16_t pn = ucom->sc_portno;
	uint8_t data;

	umcs7840_stop_read(ucom);
	umcs7840_stop_write(ucom);

	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_MCR, 0);
	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_IER, 0);

	/* Disable RX */
	if (umcs7840_get_reg_sync(sc, umcs7840_port_registers[pn].reg_control, &data))
		return;
	data |= MCS7840_DEV_CONTROLx_RX_DISABLE;
	if (umcs7840_set_reg_sync(sc, umcs7840_port_registers[pn].reg_control, data))
		return;
	DPRINTF("Port %d has been closed\n", pn);
}

static void
umcs7840_cfg_set_dtr(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	if (onoff)
		sc->sc_ports[pn].sc_mcr |= MCS7840_UART_MCR_DTR;
	else
		sc->sc_ports[pn].sc_mcr &= ~MCS7840_UART_MCR_DTR;

	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_MCR, sc->sc_ports[pn].sc_mcr);
	DPRINTF("Port %d DTR set to: %s\n", pn, onoff ? "on" : "off");
}

static void
umcs7840_cfg_set_rts(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	if (onoff)
		sc->sc_ports[pn].sc_mcr |= MCS7840_UART_MCR_RTS;
	else
		sc->sc_ports[pn].sc_mcr &= ~MCS7840_UART_MCR_RTS;

	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_MCR, sc->sc_ports[pn].sc_mcr);
	DPRINTF("Port %d RTS set to: %s\n", pn, onoff ? "on" : "off");
}

static void
umcs7840_cfg_set_break(struct ucom_softc *ucom, uint8_t onoff)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	if (onoff)
		sc->sc_ports[pn].sc_lcr |= MCS7840_UART_LCR_BREAK;
	else
		sc->sc_ports[pn].sc_lcr &= ~MCS7840_UART_LCR_BREAK;

	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_LCR, sc->sc_ports[pn].sc_lcr);
	DPRINTF("Port %d BREAK set to: %s\n", pn, onoff ? "on" : "off");
}


static void
umcs7840_cfg_param(struct ucom_softc *ucom, struct termios *t)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;
	uint8_t lcr = sc->sc_ports[pn].sc_lcr;
	uint8_t mcr = sc->sc_ports[pn].sc_mcr;

	DPRINTF("Port %d config:\n", pn);
	if (t->c_cflag & CSTOPB) {
		DPRINTF("  2 stop bits\n");
		lcr |= MCS7840_UART_LCR_STOPB2;
	} else {
		lcr |= MCS7840_UART_LCR_STOPB1;
		DPRINTF("  1 stop bit\n");
	}

	lcr &= ~MCS7840_UART_LCR_PARITYMASK;
	if (t->c_cflag & PARENB) {
		lcr |= MCS7840_UART_LCR_PARITYON;
		if (t->c_cflag & PARODD) {
			lcr = MCS7840_UART_LCR_PARITYODD;
			DPRINTF("  parity on - odd\n");
		} else {
			lcr = MCS7840_UART_LCR_PARITYEVEN;
			DPRINTF("  parity on - even\n");
		}
	} else {
		lcr &= ~MCS7840_UART_LCR_PARITYON;
		DPRINTF("  parity off\n");
	}

	lcr &= ~MCS7840_UART_LCR_DATALENMASK;
	switch (t->c_cflag & CSIZE) {
	case CS5:
		lcr |= MCS7840_UART_LCR_DATALEN5;
		DPRINTF("  5 bit\n");
		break;
	case CS6:
		lcr |= MCS7840_UART_LCR_DATALEN6;
		DPRINTF("  6 bit\n");
		break;
	case CS7:
		lcr |= MCS7840_UART_LCR_DATALEN7;
		DPRINTF("  7 bit\n");
		break;
	case CS8:
		lcr |= MCS7840_UART_LCR_DATALEN8;
		DPRINTF("  8 bit\n");
		break;
	}

	if (t->c_cflag & CRTSCTS) {
		mcr |= MCS7840_UART_MCR_CTSRTS;
		DPRINTF("  CTS/RTS\n");
	} else
		mcr &= ~MCS7840_UART_MCR_CTSRTS;

	if (t->c_cflag & (CDTR_IFLOW | CDSR_OFLOW)) {
		mcr |= MCS7840_UART_MCR_DTRDSR;
		DPRINTF("  DTR/DSR\n");
	} else
		mcr &= ~MCS7840_UART_MCR_DTRDSR;

	sc->sc_ports[pn].sc_lcr = lcr;
	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_LCR, sc->sc_ports[pn].sc_lcr);
	DPRINTF("Port %d LCR=%02x\n", pn, sc->sc_ports[pn].sc_lcr);

	sc->sc_ports[pn].sc_mcr = mcr;
	umcs7840_set_UART_reg_sync(sc, pn, MCS7840_UART_REG_MCR, sc->sc_ports[pn].sc_mcr);
	DPRINTF("Port %d MCR=%02x\n", pn, sc->sc_ports[pn].sc_mcr);

	umcs7840_set_baudrate(sc, pn, t->c_ospeed);
}


static int
umcs7840_pre_param(struct ucom_softc *ucom, struct termios *t)
{
	uint8_t clk;
	uint16_t divisor;

	if (umcs7840_calc_baudrate(t->c_ospeed, &divisor, &clk) || !divisor)
		return (EINVAL);
	return (0);
}

static void
umcs7840_start_read(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	/* Start interrupt transfer */
	usbd_transfer_start(sc->sc_intr_xfer);

	/* Start read transfer */
	usbd_transfer_start(sc->sc_ports[pn].sc_xfer[UMCS7840_BULK_RD_EP]);
}

static void
umcs7840_stop_read(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	/* Stop read transfer */
	usbd_transfer_stop(sc->sc_ports[pn].sc_xfer[UMCS7840_BULK_RD_EP]);
}

static void
umcs7840_start_write(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	/* Start interrupt transfer */
	usbd_transfer_start(sc->sc_intr_xfer);

	/* Start write transfer */
	usbd_transfer_start(sc->sc_ports[pn].sc_xfer[UMCS7840_BULK_WR_EP]);
}

static void
umcs7840_stop_write(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;

	/* Stop write transfer */
	usbd_transfer_stop(sc->sc_ports[pn].sc_xfer[UMCS7840_BULK_WR_EP]);
}

static void
umcs7840_cfg_get_status(struct ucom_softc *ucom, uint8_t *lsr, uint8_t *msr)
{
	struct umcs7840_softc *sc = ucom->sc_parent;
	uint8_t pn = ucom->sc_portno;
	uint8_t	hw_msr = 0;	/* local modem status register */

	/*
	 * Read status registers.  MSR bits need translation from ns16550 to
	 * SER_* values.  LSR bits are ns16550 in hardware and ucom.
	 */
	umcs7840_get_UART_reg_sync(sc, pn, MCS7840_UART_REG_LSR, lsr);
	umcs7840_get_UART_reg_sync(sc, pn, MCS7840_UART_REG_MSR, &hw_msr);

	if (hw_msr & MCS7840_UART_MSR_NEGCTS)
		*msr |= SER_CTS;

	if (hw_msr & MCS7840_UART_MSR_NEGDCD)
		*msr |= SER_DCD;

	if (hw_msr & MCS7840_UART_MSR_NEGRI)
		*msr |= SER_RI;

	if (hw_msr & MCS7840_UART_MSR_NEGDSR)
		*msr |= SER_DSR;

	DPRINTF("Port %d status: LSR=%02x MSR=%02x\n", ucom->sc_portno, *lsr, *msr);
}

static void
umcs7840_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct umcs7840_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[13];
	int actlen;
	int subunit;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		if (actlen == 5 || actlen == 13) {
			pc = usbd_xfer_get_frame(xfer, 0);
			usbd_copy_out(pc, 0, buf, actlen);
			/* Check status of all ports */
			for (subunit = 0; subunit < sc->sc_numports; ++subunit) {
				uint8_t pn = sc->sc_ucom[subunit].sc_portno;

				if (buf[pn] & MCS7840_UART_ISR_NOPENDING)
					continue;
				DPRINTF("Port %d has pending interrupt: %02x (FIFO: %02x)\n", pn, buf[pn] & MCS7840_UART_ISR_INTMASK, buf[pn] & (~MCS7840_UART_ISR_INTMASK));
				switch (buf[pn] & MCS7840_UART_ISR_INTMASK) {
				case MCS7840_UART_ISR_RXERR:
				case MCS7840_UART_ISR_RXHASDATA:
				case MCS7840_UART_ISR_RXTIMEOUT:
				case MCS7840_UART_ISR_MSCHANGE:
					ucom_status_change(&sc->sc_ucom[subunit]);
					break;
				default:
					/* Do nothing */
					break;
				}
			}
		} else
			device_printf(sc->sc_dev, "Invalid interrupt data length %d", actlen);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
umcs7840_read_callback1(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_read_callbackN(xfer, error, 0);
}

static void
umcs7840_read_callback2(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_read_callbackN(xfer, error, 1);
}
static void
umcs7840_read_callback3(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_read_callbackN(xfer, error, 2);
}

static void
umcs7840_read_callback4(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_read_callbackN(xfer, error, 3);
}

static void
umcs7840_read_callbackN(struct usb_xfer *xfer, usb_error_t error, uint8_t subunit)
{
	struct umcs7840_softc *sc = usbd_xfer_softc(xfer);
	struct ucom_softc *ucom = &sc->sc_ucom[subunit];
	struct usb_page_cache *pc;
	int actlen;

	usbd_xfer_status(xfer, &actlen, NULL, NULL, NULL);

	DPRINTF("Port %d read, state = %d, data length = %d\n", ucom->sc_portno, USB_GET_STATE(xfer), actlen);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		ucom_put_data(ucom, pc, 0, actlen);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
umcs7840_write_callback1(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_write_callbackN(xfer, error, 0);
}

static void
umcs7840_write_callback2(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_write_callbackN(xfer, error, 1);
}

static void
umcs7840_write_callback3(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_write_callbackN(xfer, error, 2);
}

static void
umcs7840_write_callback4(struct usb_xfer *xfer, usb_error_t error)
{
	umcs7840_write_callbackN(xfer, error, 3);
}

static void
umcs7840_write_callbackN(struct usb_xfer *xfer, usb_error_t error, uint8_t subunit)
{
	struct umcs7840_softc *sc = usbd_xfer_softc(xfer);
	struct ucom_softc *ucom = &sc->sc_ucom[subunit];
	struct usb_page_cache *pc;
	uint32_t actlen;

	DPRINTF("Port %d write, state = %d\n", ucom->sc_portno, USB_GET_STATE(xfer));

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
	case USB_ST_TRANSFERRED:
tr_setup:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (ucom_get_data(ucom, pc, 0, usbd_xfer_max_len(xfer), &actlen)) {
			DPRINTF("Port %d write, has %d bytes\n", ucom->sc_portno, actlen);
			usbd_xfer_set_frame_len(xfer, 0, actlen);
			usbd_transfer_submit(xfer);
		}
		return;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try to clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		return;
	}
}

static void
umcs7840_poll(struct ucom_softc *ucom)
{
	struct umcs7840_softc *sc = ucom->sc_parent;

	DPRINTF("Port %d poll\n", ucom->sc_portno);
	usbd_transfer_poll(sc->sc_ports[ucom->sc_portno].sc_xfer, UMCS7840_N_TRANSFERS);
	usbd_transfer_poll(&sc->sc_intr_xfer, 1);
}

static usb_error_t
umcs7840_get_reg_sync(struct umcs7840_softc *sc, uint8_t reg, uint8_t *data)
{
	struct usb_device_request req;
	usb_error_t err;
	uint16_t len;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MCS7840_RDREQ;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, UMCS7840_READ_LENGTH);

	err = usbd_do_request_proc(sc->sc_udev, &sc->sc_super_ucom.sc_tq, &req, (void *)data, 0, &len, UMCS7840_CTRL_TIMEOUT);
	if (err == USB_ERR_NORMAL_COMPLETION && len != 1) {
		device_printf(sc->sc_dev, "Reading register %d failed: invalid length %d\n", reg, len);
		return (USB_ERR_INVAL);
	} else if (err)
		device_printf(sc->sc_dev, "Reading register %d failed: %s\n", reg, usbd_errstr(err));
	return (err);
}

static usb_error_t
umcs7840_set_reg_sync(struct umcs7840_softc *sc, uint8_t reg, uint8_t data)
{
	struct usb_device_request req;
	usb_error_t err;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MCS7840_WRREQ;
	USETW(req.wValue, data);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	err = usbd_do_request_proc(sc->sc_udev, &sc->sc_super_ucom.sc_tq, &req, NULL, 0, NULL, UMCS7840_CTRL_TIMEOUT);
	if (err)
		device_printf(sc->sc_dev, "Writing register %d failed: %s\n", reg, usbd_errstr(err));

	return (err);
}

static usb_error_t
umcs7840_get_UART_reg_sync(struct umcs7840_softc *sc, uint8_t portno, uint8_t reg, uint8_t *data)
{
	struct usb_device_request req;
	uint16_t wVal;
	usb_error_t err;
	uint16_t len;

	/* portno is port number */
	wVal = ((uint16_t)(portno + 1)) << 8;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = MCS7840_RDREQ;
	USETW(req.wValue, wVal);
	USETW(req.wIndex, reg);
	USETW(req.wLength, UMCS7840_READ_LENGTH);

	err = usbd_do_request_proc(sc->sc_udev, &sc->sc_super_ucom.sc_tq, &req, (void *)data, 0, &len, UMCS7840_CTRL_TIMEOUT);
	if (err == USB_ERR_NORMAL_COMPLETION && len != 1) {
		device_printf(sc->sc_dev, "Reading UART%d register %d failed: invalid length %d\n", portno, reg, len);
		return (USB_ERR_INVAL);
	} else if (err)
		device_printf(sc->sc_dev, "Reading UART%d register %d failed: %s\n", portno, reg, usbd_errstr(err));
	return (err);
}

static usb_error_t
umcs7840_set_UART_reg_sync(struct umcs7840_softc *sc, uint8_t portno, uint8_t reg, uint8_t data)
{
	struct usb_device_request req;
	usb_error_t err;
	uint16_t wVal;

	/* portno is port number */
	wVal = ((uint16_t)(portno + 1)) << 8 | data;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = MCS7840_WRREQ;
	USETW(req.wValue, wVal);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	err = usbd_do_request_proc(sc->sc_udev, &sc->sc_super_ucom.sc_tq, &req, NULL, 0, NULL, UMCS7840_CTRL_TIMEOUT);
	if (err)
		device_printf(sc->sc_dev, "Writing UART%d register %d failed: %s\n", portno, reg, usbd_errstr(err));
	return (err);
}

static usb_error_t
umcs7840_set_baudrate(struct umcs7840_softc *sc, uint8_t portno, uint32_t rate)
{
	usb_error_t err;
	uint16_t divisor;
	uint8_t clk;
	uint8_t data;

	if (umcs7840_calc_baudrate(rate, &divisor, &clk)) {
		DPRINTF("Port %d bad speed: %d\n", portno, rate);
		return (-1);
	}
	if (divisor == 0 || (clk & MCS7840_DEV_SPx_CLOCK_MASK) != clk) {
		DPRINTF("Port %d bad speed calculation: %d\n", portno, rate);
		return (-1);
	}
	DPRINTF("Port %d set speed: %d (%02x / %d)\n", portno, rate, clk, divisor);

	/* Set clock source for standard BAUD frequences */
	err = umcs7840_get_reg_sync(sc, umcs7840_port_registers[portno].reg_sp, &data);
	if (err)
		return (err);
	data &= MCS7840_DEV_SPx_CLOCK_MASK;
	data |= clk;
	err = umcs7840_set_reg_sync(sc, umcs7840_port_registers[portno].reg_sp, data);
	if (err)
		return (err);

	/* Set divider */
	sc->sc_ports[portno].sc_lcr |= MCS7840_UART_LCR_DIVISORS;
	err = umcs7840_set_UART_reg_sync(sc, portno, MCS7840_UART_REG_LCR, sc->sc_ports[portno].sc_lcr);
	if (err)
		return (err);

	err = umcs7840_set_UART_reg_sync(sc, portno, MCS7840_UART_REG_DLL, (uint8_t)(divisor & 0xff));
	if (err)
		return (err);
	err = umcs7840_set_UART_reg_sync(sc, portno, MCS7840_UART_REG_DLM, (uint8_t)((divisor >> 8) & 0xff));
	if (err)
		return (err);

	/* Turn off access to DLL/DLM registers of UART */
	sc->sc_ports[portno].sc_lcr &= ~MCS7840_UART_LCR_DIVISORS;
	err = umcs7840_set_UART_reg_sync(sc, portno, MCS7840_UART_REG_LCR, sc->sc_ports[portno].sc_lcr);
	if (err)
		return (err);
	return (0);
}

/* Maximum speeds for standard frequences, when PLL is not used */
static const uint32_t umcs7840_baudrate_divisors[] = {0, 115200, 230400, 403200, 460800, 806400, 921600, 1572864, 3145728,};
static const uint8_t umcs7840_baudrate_divisors_len = nitems(umcs7840_baudrate_divisors);

static usb_error_t
umcs7840_calc_baudrate(uint32_t rate, uint16_t *divisor, uint8_t *clk)
{
	uint8_t i = 0;

	if (rate > umcs7840_baudrate_divisors[umcs7840_baudrate_divisors_len - 1])
		return (-1);

	for (i = 0; i < umcs7840_baudrate_divisors_len - 1 &&
	    !(rate > umcs7840_baudrate_divisors[i] && rate <= umcs7840_baudrate_divisors[i + 1]); ++i);
	if (rate == 0)
		*divisor = 1;	/* XXX */
	else
		*divisor = umcs7840_baudrate_divisors[i + 1] / rate;
	/* 0x00 .. 0x70 */
	*clk = i << MCS7840_DEV_SPx_CLOCK_SHIFT;
	return (0);
}
