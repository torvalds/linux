/* $OpenBSD: umcs.c,v 1.12 2024/05/23 03:21:09 jsg Exp $ */
/* $NetBSD: umcs.c,v 1.8 2014/08/23 21:37:56 martin Exp $ */
/* $FreeBSD: head/sys/dev/usb/serial/umcs.c 260559 2014-01-12 11:44:28Z hselasky $ */

/*-
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
 * by MosChip mos7820 and mos7840, bridge chips.  The adapters are sold under
 * many different brand names.
 *
 * Datasheets are available at MosChip www site at http://www.moschip.com.
 * The datasheets don't contain full programming information for the chip.
 *
 * It is normal to have only two enabled ports in devices, based on quad-port
 * mos7840.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/device.h>
#include <sys/task.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/ucomvar.h>

#include "umcs.h"

#ifdef UMCS_DEBUG
#define	DPRINTF(x...)	printf(x)
#else
#define	DPRINTF(x...)
#endif

#define DEVNAME(_sc) ((_sc)->sc_dev.dv_xname)

/*
 * Two-port devices (both with 7820 chip and 7840 chip configured as two-port)
 * have ports 0 and 2, with ports 1 and 3 omitted.
 * So, PHYSICAL port numbers on two-port device will be 0 and 2.
 *
 * We use an array of the following struct, indexed by ucom port index,
 * and include the physical port number in it.
 */
struct umcs_port {
	struct ucom_softc 	*ucom;		/* ucom subdevice */
	unsigned int		 pn;		/* physical port number */
	int			 flags;
#define	UMCS_STATCHG		 0x01

	uint8_t			 lcr;		/* local line control reg. */
	uint8_t			 mcr;		/* local modem control reg. */
};

struct umcs_softc {
	struct device		 sc_dev;
	struct usbd_device	*sc_udev;	/* the usb device */
	struct usbd_pipe	*sc_ipipe;	/* interrupt pipe */
	uint8_t			*sc_ibuf;	/* buffer for interrupt xfer */
	unsigned int		 sc_isize;	/* size of buffer */

	struct umcs_port	 sc_subdevs[UMCS_MAX_PORTS];
	uint8_t			 sc_numports;	/* number of ports */

	int			 sc_init_done;
	struct task		 sc_status_task;
};

int	umcs_get_reg(struct umcs_softc *, uint8_t, uint8_t *);
int	umcs_set_reg(struct umcs_softc *, uint8_t, uint8_t);
int	umcs_get_uart_reg(struct umcs_softc *, uint8_t, uint8_t, uint8_t *);
int	umcs_set_uart_reg(struct umcs_softc *, uint8_t, uint8_t, uint8_t);
int	umcs_calc_baudrate(uint32_t, uint16_t *, uint8_t *);
int	umcs_set_baudrate(struct umcs_softc *, uint8_t, uint32_t);
void	umcs_dtr(struct umcs_softc *, int, int);
void	umcs_rts(struct umcs_softc *, int, int);
void	umcs_break(struct umcs_softc *, int, int);

int	umcs_match(struct device *, void *, void *);
void	umcs_attach(struct device *, struct device *, void *);
int	umcs_detach(struct device *, int);
void	umcs_intr(struct usbd_xfer *, void *, usbd_status);
void	umcs_status_task(void *);

void	umcs_get_status(void *, int, uint8_t *, uint8_t *);
void	umcs_set(void *, int, int, int);
int	umcs_param(void *, int, struct termios *);
int	umcs_open(void *, int);
void	umcs_close(void *, int);

const struct ucom_methods umcs_methods = {
	umcs_get_status,
	umcs_set,
	umcs_param,
	NULL,
	umcs_open,
	umcs_close,
	NULL,
	NULL,
};

const struct usb_devno umcs_devs[] = {
	{ USB_VENDOR_MOSCHIP,		USB_PRODUCT_MOSCHIP_MCS7810 },
	{ USB_VENDOR_MOSCHIP,		USB_PRODUCT_MOSCHIP_MCS7820 },
	{ USB_VENDOR_MOSCHIP,		USB_PRODUCT_MOSCHIP_MCS7840 },
	{ USB_VENDOR_ATEN,		USB_PRODUCT_ATEN_UC2324 }
};

struct cfdriver umcs_cd = {
	NULL, "umcs", DV_DULL
};

const struct cfattach umcs_ca = {
	sizeof(struct umcs_softc), umcs_match, umcs_attach, umcs_detach
};


static inline int
umcs_reg_sp(int pn)
{
	KASSERT(pn >= 0 && pn < 4);
	switch (pn) {
	default:
	case 0:	return UMCS_SP1;
	case 1:	return UMCS_SP2;
	case 2:	return UMCS_SP3;
	case 3:	return UMCS_SP4;
	}
}

static inline int
umcs_reg_ctrl(int pn)
{
	KASSERT(pn >= 0 && pn < 4);
	switch (pn) {
	default:
	case 0:	return UMCS_CTRL1;
	case 1:	return UMCS_CTRL2;
	case 2:	return UMCS_CTRL3;
	case 3:	return UMCS_CTRL4;
	}
}

int
umcs_match(struct device *dev, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	if (uaa->iface == NULL || uaa->ifaceno != UMCS_IFACE_NO)
		return (UMATCH_NONE);

	return (usb_lookup(umcs_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE;
}

void
umcs_attach(struct device *parent, struct device *self, void *aux)
{
	struct umcs_softc *sc = (struct umcs_softc *)self;
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;
	usb_endpoint_descriptor_t *ed;
	struct ucom_attach_args uca;
	int error, i, intr_addr;
	uint8_t data;

	sc->sc_udev = uaa->device;

	/*
	 * Get number of ports
	 * Documentation (full datasheet) says, that number of ports is
	 * set as UMCS_MODE_SELECT24S bit in MODE R/Only
	 * register. But vendor driver uses these undocumented
	 * register & bit.
	 *
	 * Experiments show, that MODE register can have `0'
	 * (4 ports) bit on 2-port device, so use vendor driver's way.
	 *
	 * Also, see notes in header file for these constants.
	 */
	if (umcs_get_reg(sc, UMCS_GPIO, &data)) {
		printf("%s: unable to get number of ports\n", DEVNAME(sc));
		usbd_deactivate(sc->sc_udev);
		return;
	}
	if (data & UMCS_GPIO_4PORTS)
		sc->sc_numports = 4; /* physical port no are : 0, 1, 2, 3 */
	else if (uaa->product == USB_PRODUCT_MOSCHIP_MCS7810)
		sc->sc_numports = 1;
	else
		sc->sc_numports = 2; /* physical port no are: 0 and 2 */

#ifdef UMCS_DEBUG
	if (!umcs_get_reg(sc, UMCS_MODE, &data)) {
		printf("%s: On-die configuration: RST: active %s, "
		    "HRD: %s, PLL: %s, POR: %s, Ports: %s, EEPROM write %s, "
		    "IrDA is %savailable\n", DEVNAME(sc),
		    (data & UMCS_MODE_RESET) ? "low" : "high",
		    (data & UMCS_MODE_SER_PRSNT) ? "yes" : "no",
		    (data & UMCS_MODE_PLLBYPASS) ? "bypassed" : "avail",
		    (data & UMCS_MODE_PORBYPASS) ? "bypassed" : "avail",
		    (data & UMCS_MODE_SELECT24S) ? "2" : "4",
		    (data & UMCS_MODE_EEPROMWR) ? "enabled" : "disabled",
		    (data & UMCS_MODE_IRDA) ? "" : "not ");
	}
#endif

	/* Set up the interrupt pipe */
	id = usbd_get_interface_descriptor(uaa->iface);
	intr_addr = -1;
	for (i = 0 ; i < id->bNumEndpoints ; i++) {
		ed = usbd_interface2endpoint_descriptor(uaa->iface, i);
		if (ed == NULL) {
			printf("%s: no endpoint descriptor found for %d\n",
			    DEVNAME(sc), i);
			usbd_deactivate(sc->sc_udev);
			return;
		}

		if (UE_GET_DIR(ed->bEndpointAddress) != UE_DIR_IN ||
		    UE_GET_XFERTYPE(ed->bmAttributes) != UE_INTERRUPT)
			continue;
		sc->sc_isize = UGETW(ed->wMaxPacketSize);
		intr_addr = ed->bEndpointAddress;
		break;
	}
	if (intr_addr < 0) {
		printf("%s: missing endpoint\n", DEVNAME(sc));
		usbd_deactivate(sc->sc_udev);
		return;
	}
	sc->sc_ibuf = malloc(sc->sc_isize, M_USBDEV, M_WAITOK);

	error = usbd_open_pipe_intr(uaa->iface, intr_addr,
		    USBD_SHORT_XFER_OK, &sc->sc_ipipe, sc, sc->sc_ibuf,
		    sc->sc_isize, umcs_intr, 100 /* XXX */);
	if (error) {
		printf("%s: cannot open interrupt pipe (addr %d)\n",
		    DEVNAME(sc), intr_addr);
		usbd_deactivate(sc->sc_udev);
		return;
	}

	memset(&uca, 0, sizeof uca);
	uca.ibufsize = 256;
	uca.obufsize = 256;
	uca.ibufsizepad = 256;
	uca.opkthdrlen = 0;
	uca.device = sc->sc_udev;
	uca.iface = uaa->iface;
	uca.methods = &umcs_methods;
	uca.arg = sc;

	for (i = 0; i < sc->sc_numports; i++) {
		uca.bulkin = uca.bulkout = -1;

		/*
		 * On 4 port cards, endpoints are 0/1, 2/3, 4/5, and 6/7.
		 * On 2 port cards, they are 0/1 and 4/5.
		 * On single port, just 0/1 will be used.
		 */
		int pn = i * (sc->sc_numports == 2 ? 2 : 1);

		ed = usbd_interface2endpoint_descriptor(uaa->iface, pn*2);
		if (ed == NULL) {
			printf("%s: no bulk in endpoint found for %d\n",
			    DEVNAME(sc), i);
			usbd_deactivate(sc->sc_udev);
			return;
		}
		uca.bulkin = ed->bEndpointAddress;

		ed = usbd_interface2endpoint_descriptor(uaa->iface, pn*2+1);
		if (ed == NULL) {
			printf("%s: no bulk out endpoint found for %d\n",
			    DEVNAME(sc), i);
			usbd_deactivate(sc->sc_udev);
			return;
		}
		uca.bulkout = ed->bEndpointAddress;
		uca.portno = i;

		sc->sc_subdevs[i].pn = pn;
		sc->sc_subdevs[i].ucom = (struct ucom_softc *)
		    config_found_sm(self, &uca, ucomprint, ucomsubmatch);
	}

	task_set(&sc->sc_status_task, umcs_status_task, sc);
}

int
umcs_get_reg(struct umcs_softc *sc, uint8_t reg, uint8_t *data)
{
	usb_device_request_t req;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UMCS_READ;
	USETW(req.wValue, 0);
	USETW(req.wIndex, reg);
	USETW(req.wLength, UMCS_READ_LENGTH);

	if (usbd_do_request(sc->sc_udev, &req, data))
		return (EIO);

	return (0);
}

int
umcs_set_reg(struct umcs_softc *sc, uint8_t reg, uint8_t data)
{
	usb_device_request_t req;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UMCS_WRITE;
	USETW(req.wValue, data);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	if (usbd_do_request(sc->sc_udev, &req, NULL))
		return (EIO);

	return (0);
}

int
umcs_get_uart_reg(struct umcs_softc *sc, uint8_t portno, uint8_t reg,
    uint8_t *data)
{
	usb_device_request_t req;
	uint16_t wVal;

	wVal = ((uint16_t)(sc->sc_subdevs[portno].pn + 1)) << 8;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = UMCS_READ;
	USETW(req.wValue, wVal);
	USETW(req.wIndex, reg);
	USETW(req.wLength, UMCS_READ_LENGTH);

	if (usbd_do_request(sc->sc_udev, &req, data))
		return (EIO);

	return (0);
}

int
umcs_set_uart_reg(struct umcs_softc *sc, uint8_t portno, uint8_t reg,
    uint8_t data)
{
	usb_device_request_t req;
	uint16_t wVal;

	wVal = ((uint16_t)(sc->sc_subdevs[portno].pn + 1)) << 8 | data;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = UMCS_WRITE;
	USETW(req.wValue, wVal);
	USETW(req.wIndex, reg);
	USETW(req.wLength, 0);

	if (usbd_do_request(sc->sc_udev, &req, NULL))
		return (EIO);

	return (0);
}

int
umcs_set_baudrate(struct umcs_softc *sc, uint8_t portno, uint32_t rate)
{
	int pn = sc->sc_subdevs[portno].pn;
	int spreg = umcs_reg_sp(pn);
	uint8_t lcr = sc->sc_subdevs[portno].lcr;
	uint8_t clk, data;
	uint16_t div;

	if (umcs_calc_baudrate(rate, &div, &clk))
		return (EINVAL);

	DPRINTF("%s: portno %d set speed: %d (%02x/%d)\n", DEVNAME(sc), portno,
	    rate, clk, div);

	/* Set clock source for standard BAUD frequencies */
	if (umcs_get_reg(sc, spreg, &data))
		return (EIO);
	data &= UMCS_SPx_CLK_MASK;
	if (umcs_set_reg(sc, spreg, data | clk))
		return (EIO);

	/* Set divider */
	lcr |= UMCS_LCR_DIVISORS;
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_LCR, lcr))
		return (EIO);
	sc->sc_subdevs[portno].lcr = lcr;

	if (umcs_set_uart_reg(sc, portno, UMCS_REG_DLL, div & 0xff) ||
	    umcs_set_uart_reg(sc, portno, UMCS_REG_DLM, (div >> 8) & 0xff))
		return (EIO);

	/* Turn off access to DLL/DLM registers of UART */
	lcr &= ~UMCS_LCR_DIVISORS;
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_LCR, lcr))
		return (EIO);
	sc->sc_subdevs[portno].lcr = lcr;

	return (0);
}

/* Maximum speeds for standard frequencies, when PLL is not used */
static const uint32_t umcs_baudrate_divisors[] = {
    0, 115200, 230400, 403200, 460800, 806400, 921600, 1572864, 3145728,
};

int
umcs_calc_baudrate(uint32_t rate, uint16_t *divisor, uint8_t *clk)
{
	const uint8_t divisors_len = nitems(umcs_baudrate_divisors);
	uint8_t i = 0;

	if (rate > umcs_baudrate_divisors[divisors_len - 1])
		return (-1);

	for (i = 0; i < divisors_len - 1; i++) {
		if (rate > umcs_baudrate_divisors[i] &&
		    rate <= umcs_baudrate_divisors[i + 1]) {
			*divisor = umcs_baudrate_divisors[i + 1] / rate;
			/* 0x00 .. 0x70 */
			*clk = i << UMCS_SPx_CLK_SHIFT;
			return (0);
		}
	}

	return (-1);
}

int
umcs_detach(struct device *self, int flags)
{
	struct umcs_softc *sc = (struct umcs_softc *)self;

	task_del(systq, &sc->sc_status_task);

	if (sc->sc_ipipe != NULL) {
		usbd_close_pipe(sc->sc_ipipe);
		sc->sc_ipipe = NULL;
	}

	if (sc->sc_ibuf != NULL) {
		free(sc->sc_ibuf, M_USBDEV, sc->sc_isize);
		sc->sc_ibuf = NULL;
	}

	return (config_detach_children(self, flags));
}

void
umcs_get_status(void *self, int portno, uint8_t *lsr, uint8_t *msr)
{
	struct umcs_softc *sc = self;
	uint8_t	hw_lsr = 0;	/* local line status register */
	uint8_t	hw_msr = 0;	/* local modem status register */

	if (usbd_is_dying(sc->sc_udev))
		return;

	/* Read LSR & MSR */
	if (umcs_get_uart_reg(sc, portno, UMCS_REG_LSR, &hw_lsr) ||
	    umcs_get_uart_reg(sc, portno, UMCS_REG_MSR, &hw_msr))
	    	return;

	*lsr = hw_lsr;
	*msr = hw_msr;
}

void
umcs_set(void *self, int portno, int reg, int onoff)
{
	struct umcs_softc *sc = self;

	if (usbd_is_dying(sc->sc_udev))
		return;

	switch (reg) {
	case UCOM_SET_DTR:
		umcs_dtr(sc, portno, onoff);
		break;
	case UCOM_SET_RTS:
		umcs_rts(sc, portno, onoff);
		break;
	case UCOM_SET_BREAK:
		umcs_break(sc, portno, onoff);
		break;
	default:
		break;
	}
}

int
umcs_param(void *self, int portno, struct termios *t)
{
	struct umcs_softc *sc = self;
	uint8_t lcr = sc->sc_subdevs[portno].lcr;
	uint8_t mcr = sc->sc_subdevs[portno].mcr;
	int error = 0;

	if (t->c_cflag & CSTOPB)
		lcr |= UMCS_LCR_STOPB2;
	else
		lcr |= UMCS_LCR_STOPB1;

	lcr &= ~UMCS_LCR_PARITYMASK;
	if (t->c_cflag & PARENB) {
		lcr |= UMCS_LCR_PARITYON;
		if (t->c_cflag & PARODD) {
			lcr |= UMCS_LCR_PARITYODD;
		} else {
			lcr |= UMCS_LCR_PARITYEVEN;
		}
	} else {
		lcr &= ~UMCS_LCR_PARITYON;
	}

	lcr &= ~UMCS_LCR_DATALENMASK;
	switch (t->c_cflag & CSIZE) {
	case CS5:
		lcr |= UMCS_LCR_DATALEN5;
		break;
	case CS6:
		lcr |= UMCS_LCR_DATALEN6;
		break;
	case CS7:
		lcr |= UMCS_LCR_DATALEN7;
		break;
	case CS8:
		lcr |= UMCS_LCR_DATALEN8;
		break;
	}

	if (t->c_cflag & CRTSCTS)
		mcr |= UMCS_MCR_CTSRTS;
	else
		mcr &= ~UMCS_MCR_CTSRTS;

	if (t->c_cflag & CLOCAL)
		mcr &= ~UMCS_MCR_DTRDSR;
	else
		mcr |= UMCS_MCR_DTRDSR;

	if (umcs_set_uart_reg(sc, portno, UMCS_REG_LCR, lcr))
		return (EIO);
	sc->sc_subdevs[portno].lcr = lcr;

	if (umcs_set_uart_reg(sc, portno, UMCS_REG_MCR, mcr))
		return (EIO);
	sc->sc_subdevs[portno].mcr = mcr;

	error = umcs_set_baudrate(sc, portno, t->c_ospeed);

	return (error);
}

void
umcs_dtr(struct umcs_softc *sc, int portno, int onoff)
{
	uint8_t mcr = sc->sc_subdevs[portno].mcr;

	if (onoff)
		mcr |= UMCS_MCR_DTR;
	else
		mcr &= ~UMCS_MCR_DTR;

	if (umcs_set_uart_reg(sc, portno, UMCS_REG_MCR, mcr))
		return;
	sc->sc_subdevs[portno].mcr = mcr;
}

void
umcs_rts(struct umcs_softc *sc, int portno, int onoff)
{
	uint8_t mcr = sc->sc_subdevs[portno].mcr;

	if (onoff)
		mcr |= UMCS_MCR_RTS;
	else
		mcr &= ~UMCS_MCR_RTS;

	if (umcs_set_uart_reg(sc, portno, UMCS_REG_MCR, mcr))
		return;
	sc->sc_subdevs[portno].mcr = mcr;
}

void
umcs_break(struct umcs_softc *sc, int portno, int onoff)
{
	uint8_t lcr = sc->sc_subdevs[portno].lcr;

	if (onoff)
		lcr |= UMCS_LCR_BREAK;
	else
		lcr &= ~UMCS_LCR_BREAK;

	if (umcs_set_uart_reg(sc, portno, UMCS_REG_LCR, lcr))
		return;
	sc->sc_subdevs[portno].lcr = lcr;
}

int
umcs_open(void *self, int portno)
{
	struct umcs_softc *sc = self;
	int pn = sc->sc_subdevs[portno].pn;
	int spreg = umcs_reg_sp(pn);
	int ctrlreg = umcs_reg_ctrl(pn);
	uint8_t mcr = sc->sc_subdevs[portno].mcr;
	uint8_t lcr = sc->sc_subdevs[portno].lcr;
	uint8_t data;
	int error;

	if (usbd_is_dying(sc->sc_udev))
		return (EIO);

	/* If it very first open, finish global configuration */
	if (!sc->sc_init_done) {
		if (umcs_get_reg(sc, UMCS_CTRL1, &data) ||
		    umcs_set_reg(sc, UMCS_CTRL1, data | UMCS_CTRL1_DRIVER_DONE))
			return (EIO);
		sc->sc_init_done = 1;
	}

	/* Toggle reset bit on-off */
	if (umcs_get_reg(sc, spreg, &data) ||
	    umcs_set_reg(sc, spreg, data | UMCS_SPx_UART_RESET) ||
	    umcs_set_reg(sc, spreg, data & ~UMCS_SPx_UART_RESET))
		return (EIO);

	/* Set RS-232 mode */
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_SCRATCHPAD,
	    UMCS_SCRATCHPAD_RS232))
		return (EIO);

	/* Disable RX on time of initialization */
	if (umcs_get_reg(sc, ctrlreg, &data) ||
	    umcs_set_reg(sc, ctrlreg, data | UMCS_CTRL_RX_DISABLE))
		return (EIO);

	/* Disable all interrupts */
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_IER, 0))
		return (EIO);

	/* Reset FIFO -- documented */
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_FCR, 0) ||
	    umcs_set_uart_reg(sc, portno, UMCS_REG_FCR,
	    UMCS_FCR_ENABLE | UMCS_FCR_FLUSHRHR |
	    UMCS_FCR_FLUSHTHR | UMCS_FCR_RTL_1_14))
		return (EIO);

	/* Set 8 bit, no parity, 1 stop bit -- documented */
	lcr = UMCS_LCR_DATALEN8 | UMCS_LCR_STOPB1;
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_LCR, lcr))
		return (EIO);
	sc->sc_subdevs[portno].lcr = lcr;

	/*
	 * Enable DTR/RTS on modem control, enable modem interrupts --
	 * documented
	 */
	mcr = UMCS_MCR_DTR | UMCS_MCR_RTS | UMCS_MCR_IE;
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_MCR, mcr))
		return (EIO);
	sc->sc_subdevs[portno].mcr = mcr;

	/* Clearing Bulkin and Bulkout FIFO */
	if (umcs_get_reg(sc, spreg, &data))
		return (EIO);
	data |= UMCS_SPx_RESET_OUT_FIFO|UMCS_SPx_RESET_IN_FIFO;
	if (umcs_set_reg(sc, spreg, data))
		return (EIO);
	data &= ~(UMCS_SPx_RESET_OUT_FIFO|UMCS_SPx_RESET_IN_FIFO);
	if (umcs_set_reg(sc, spreg, data))
		return (EIO);

	/* Set speed 9600 */
	if ((error = umcs_set_baudrate(sc, portno, 9600)) != 0)
		return (error);

	/* Finally enable all interrupts -- documented */
	/*
	 * Copied from vendor driver, I don't know why we should read LCR
	 * here
	 */
	if (umcs_get_uart_reg(sc, portno, UMCS_REG_LCR,
	    &sc->sc_subdevs[portno].lcr))
		return (EIO);
	if (umcs_set_uart_reg(sc, portno, UMCS_REG_IER,
	    UMCS_IER_RXSTAT | UMCS_IER_MODEM))
		return (EIO);

	/* Enable RX */
	if (umcs_get_reg(sc, ctrlreg, &data) ||
	    umcs_set_reg(sc, ctrlreg, data & ~UMCS_CTRL_RX_DISABLE))
		return (EIO);

	return (0);
}

void
umcs_close(void *self, int portno)
{
	struct umcs_softc *sc = self;
	int pn = sc->sc_subdevs[portno].pn;
	int ctrlreg = umcs_reg_ctrl(pn);
	uint8_t data;

	if (usbd_is_dying(sc->sc_udev))
		return;

	umcs_set_uart_reg(sc, portno, UMCS_REG_MCR, 0);
	umcs_set_uart_reg(sc, portno, UMCS_REG_IER, 0);

	/* Disable RX */
	if (umcs_get_reg(sc, ctrlreg, &data) ||
	    umcs_set_reg(sc, ctrlreg, data | UMCS_CTRL_RX_DISABLE))
		return;
}

void
umcs_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct umcs_softc *sc = priv;
	uint8_t *buf = sc->sc_ibuf;
	int actlen, i;

	if (usbd_is_dying(sc->sc_udev))
		return;

	if (status == USBD_CANCELLED || status == USBD_IOERROR)
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF("%s: interrupt status=%d\n", DEVNAME(sc), status);
		usbd_clear_endpoint_stall_async(sc->sc_ipipe);
		return;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &actlen, NULL);
	if (actlen != 5 && actlen != 13) {
		printf("%s: invalid interrupt data length %d\n", DEVNAME(sc),
		    actlen);
		return;
	}

	/* Check status of all ports */
	for (i = 0; i < sc->sc_numports; i++) {
		uint8_t pn = sc->sc_subdevs[i].pn;

		if (buf[pn] & UMCS_ISR_NOPENDING)
			continue;

		DPRINTF("%s: port %d has pending interrupt: %02x, FIFO=%02x\n",
		    DEVNAME(sc), i, buf[pn] & UMCS_ISR_INTMASK,
		    buf[pn] & (~UMCS_ISR_INTMASK));

		switch (buf[pn] & UMCS_ISR_INTMASK) {
		case UMCS_ISR_RXERR:
		case UMCS_ISR_RXHASDATA:
		case UMCS_ISR_RXTIMEOUT:
		case UMCS_ISR_MSCHANGE:
			sc->sc_subdevs[i].flags |= UMCS_STATCHG;
			task_add(systq, &sc->sc_status_task);
			break;
		default:
			/* Do nothing */
			break;
		}
	}
}

void
umcs_status_task(void *arg)
{
	struct umcs_softc *sc = arg;
	int i;

	for (i = 0; i < sc->sc_numports; i++) {
		if ((sc->sc_subdevs[i].flags & UMCS_STATCHG) == 0)
			continue;

		sc->sc_subdevs[i].flags &= ~UMCS_STATCHG;
		ucom_status_change(sc->sc_subdevs[i].ucom);
	}
}
