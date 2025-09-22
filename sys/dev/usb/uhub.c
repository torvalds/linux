/*	$OpenBSD: uhub.c,v 1.98 2024/05/23 03:21:09 jsg Exp $ */
/*	$NetBSD: uhub.c,v 1.64 2003/02/08 03:32:51 ichiro Exp $	*/
/*	$FreeBSD: src/sys/dev/usb/uhub.c,v 1.18 1999/11/17 22:33:43 n_hibma Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdivar.h>

#define UHUB_INTR_INTERVAL 255	/* ms */

#ifdef UHUB_DEBUG
#define DPRINTF(x...)	do { printf(x); } while (0)
#else
#define DPRINTF(x...)
#endif

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

struct uhub_softc {
	struct device		sc_dev;		/* base device */
	struct usbd_device	*sc_hub;	/* USB device */
	struct usbd_pipe	*sc_ipipe;	/* interrupt pipe */

	uint32_t		 sc_status;	/* status from last interrupt */
	uint8_t			*sc_statusbuf;	/* per port status buffer */
	size_t			 sc_statuslen;	/* status bufferlen */

	u_char			sc_running;
};
#define UHUB_PROTO(sc) ((sc)->sc_hub->ddesc.bDeviceProtocol)
#define UHUB_IS_HIGH_SPEED(sc) (UHUB_PROTO(sc) != UDPROTO_FSHUB)
#define UHUB_IS_SINGLE_TT(sc) (UHUB_PROTO(sc) == UDPROTO_HSHUBSTT)

int uhub_explore(struct usbd_device *hub);
void uhub_intr(struct usbd_xfer *, void *, usbd_status);
int uhub_port_connect(struct uhub_softc *, int, int);

/*
 * We need two attachment points:
 * hub to usb and hub to hub
 * Every other driver only connects to hubs
 */

int uhub_match(struct device *, void *, void *);
void uhub_attach(struct device *, struct device *, void *);
int uhub_detach(struct device *, int);

struct cfdriver uhub_cd = {
	NULL, "uhub", DV_DULL
};

const struct cfattach uhub_ca = {
	sizeof(struct uhub_softc), uhub_match, uhub_attach,  uhub_detach
};

const struct cfattach uhub_uhub_ca = {
	sizeof(struct uhub_softc), uhub_match, uhub_attach,  uhub_detach
};

int
uhub_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_device_descriptor_t *dd = usbd_get_device_descriptor(uaa->device);

	if (uaa->iface == NULL)
		return (UMATCH_NONE);

	/*
	 * The subclass for hubs seems to be 0 for some and 1 for others,
	 * so we just ignore the subclass.
	 */
	if (dd->bDeviceClass == UDCLASS_HUB)
		return (UMATCH_DEVCLASS_DEVSUBCLASS);
	return (UMATCH_NONE);
}

void
uhub_attach(struct device *parent, struct device *self, void *aux)
{
	struct uhub_softc *sc = (struct uhub_softc *)self;
	struct usb_attach_arg *uaa = aux;
	struct usbd_device *dev = uaa->device;
	struct usbd_hub *hub = NULL;
	union {
		usb_hub_descriptor_t	hs;
		usb_hub_ss_descriptor_t	ss;
	} hd;
	int p, port, nports, powerdelay;
	struct usbd_interface *iface = uaa->iface;
	usb_endpoint_descriptor_t *ed;
	struct usbd_tt *tts = NULL;
	uint8_t ttthink = 0;
	usbd_status err;
#ifdef UHUB_DEBUG
	int nremov;
#endif

	sc->sc_hub = dev;

	if (dev->depth > USB_HUB_MAX_DEPTH) {
		printf("%s: hub depth (%d) exceeded, hub ignored\n",
		       sc->sc_dev.dv_xname, USB_HUB_MAX_DEPTH);
		return;
	}

	/*
	 * Super-Speed hubs need to know their depth to be able to
	 * parse the bits of the route-string that correspond to
	 * their downstream port number.
	 *
	 * This does no apply to root hubs.
	 */
	if (dev->depth != 0 && dev->speed == USB_SPEED_SUPER) {
		if (usbd_set_hub_depth(dev, dev->depth - 1)) {
			printf("%s: unable to set HUB depth\n",
			    sc->sc_dev.dv_xname);
			return;
		}
	}

	/* Get hub descriptor. */
	if (dev->speed == USB_SPEED_SUPER) {
		err = usbd_get_hub_ss_descriptor(dev, &hd.ss, 1);
		nports = hd.ss.bNbrPorts;
		powerdelay = (hd.ss.bPwrOn2PwrGood * UHD_PWRON_FACTOR);
		if (!err && nports > 7)
			usbd_get_hub_ss_descriptor(dev, &hd.ss, nports);
	} else {
		err = usbd_get_hub_descriptor(dev, &hd.hs, 1);
		nports = hd.hs.bNbrPorts;
		powerdelay = (hd.hs.bPwrOn2PwrGood * UHD_PWRON_FACTOR);
		ttthink = UGETW(hd.hs.wHubCharacteristics) & UHD_TT_THINK;
		if (!err && nports > 7)
			usbd_get_hub_descriptor(dev, &hd.hs, nports);
	}

	if (err) {
		DPRINTF("%s: getting hub descriptor failed, error=%s\n",
			 sc->sc_dev.dv_xname, usbd_errstr(err));
		return;
	}

#ifdef UHUB_DEBUG
	for (nremov = 0, port = 1; port <= nports; port++) {
		if (dev->speed == USB_SPEED_SUPER) {
			if (!UHD_NOT_REMOV(&hd.ss, port))
				nremov++;
		} else {
			if (!UHD_NOT_REMOV(&hd.hs, port))
				nremov++;
		}
	}

	printf("%s: %d port%s with %d removable, %s powered",
	       sc->sc_dev.dv_xname, nports, nports != 1 ? "s" : "",
	       nremov, dev->self_powered ? "self" : "bus");

	if (dev->depth > 0 && UHUB_IS_HIGH_SPEED(sc)) {
		printf(", %s transaction translator%s",
		    UHUB_IS_SINGLE_TT(sc) ? "single" : "multiple",
		    UHUB_IS_SINGLE_TT(sc) ? "" : "s");
	}

	printf("\n");
#endif

	if (nports == 0) {
		printf("%s: no ports, hub ignored\n", sc->sc_dev.dv_xname);
		goto bad;
	}

	hub = malloc(sizeof(*hub), M_USBDEV, M_NOWAIT);
	if (hub == NULL)
		return;
	hub->ports = mallocarray(nports, sizeof(struct usbd_port),
	    M_USBDEV, M_NOWAIT);
	if (hub->ports == NULL) {
		free(hub, M_USBDEV, sizeof *hub);
		return;
	}
	dev->hub = hub;
	dev->hub->hubsoftc = sc;
	hub->explore = uhub_explore;
	hub->nports = nports;
	hub->powerdelay = powerdelay;
	hub->ttthink = ttthink >> 5;
	hub->multi = UHUB_IS_SINGLE_TT(sc) ? 0 : 1;

	if (!dev->self_powered && dev->powersrc->parent != NULL &&
	    !dev->powersrc->parent->self_powered) {
		printf("%s: bus powered hub connected to bus powered hub, "
		       "ignored\n", sc->sc_dev.dv_xname);
		goto bad;
	}

	/* Set up interrupt pipe. */
	ed = usbd_interface2endpoint_descriptor(iface, 0);
	if (ed == NULL) {
		printf("%s: no endpoint descriptor\n", sc->sc_dev.dv_xname);
		goto bad;
	}
	if (UE_GET_XFERTYPE(ed->bmAttributes) != UE_INTERRUPT) {
		printf("%s: bad interrupt endpoint\n", sc->sc_dev.dv_xname);
		goto bad;
	}

	sc->sc_statuslen = (nports + 1 + 7) / 8;
	sc->sc_statusbuf = malloc(sc->sc_statuslen, M_USBDEV, M_NOWAIT);
	if (!sc->sc_statusbuf)
		goto bad;

	err = usbd_open_pipe_intr(iface, ed->bEndpointAddress,
		  USBD_SHORT_XFER_OK, &sc->sc_ipipe, sc, sc->sc_statusbuf,
		  sc->sc_statuslen, uhub_intr, UHUB_INTR_INTERVAL);
	if (err) {
		printf("%s: cannot open interrupt pipe\n",
		       sc->sc_dev.dv_xname);
		goto bad;
	}

	/* Wait with power off for a while. */
	usbd_delay_ms(dev, USB_POWER_DOWN_TIME);

	/*
	 * To have the best chance of success we do things in the exact same
	 * order as Windoze98.  This should not be necessary, but some
	 * devices do not follow the USB specs to the letter.
	 *
	 * These are the events on the bus when a hub is attached:
	 *  Get device and config descriptors (see attach code)
	 *  Get hub descriptor (see above)
	 *  For all ports
	 *     turn on power
	 *     wait for power to become stable
	 * (all below happens in explore code)
	 *  For all ports
	 *     clear C_PORT_CONNECTION
	 *  For all ports
	 *     get port status
	 *     if device connected
	 *        wait 100 ms
	 *        turn on reset
	 *        wait
	 *        clear C_PORT_RESET
	 *        get port status
	 *        proceed with device attachment
	 */

	if (UHUB_IS_HIGH_SPEED(sc)) {
		tts = mallocarray((UHUB_IS_SINGLE_TT(sc) ? 1 : nports),
		    sizeof(struct usbd_tt), M_USBDEV, M_NOWAIT);
		if (!tts)
			goto bad;
	}
	/* Set up data structures */
	for (p = 0; p < nports; p++) {
		struct usbd_port *up = &hub->ports[p];
		up->device = NULL;
		up->parent = dev;
		up->portno = p + 1;
		if (dev->self_powered)
			/* Self powered hub, give ports maximum current. */
			up->power = USB_MAX_POWER;
		else
			up->power = USB_MIN_POWER;
		up->restartcnt = 0;
		up->reattach = 0;
		if (UHUB_IS_HIGH_SPEED(sc)) {
			up->tt = &tts[UHUB_IS_SINGLE_TT(sc) ? 0 : p];
			up->tt->hub = hub;
			up->tt->hcpriv = NULL;
		} else {
			up->tt = NULL;
		}
	}

	for (port = 1; port <= nports; port++) {
		/* Turn the power on. */
		err = usbd_set_port_feature(dev, port, UHF_PORT_POWER);
		if (err)
			printf("%s: port %d power on failed, %s\n",
			       sc->sc_dev.dv_xname, port,
			       usbd_errstr(err));
		/* Make sure we check the port status at least once. */
		sc->sc_status |= (1 << port);
	}

	/* Wait for stable power. */
        if (dev->powersrc->parent != NULL)
		usbd_delay_ms(dev, powerdelay + USB_EXTRA_POWER_UP_TIME);

	/* The usual exploration will finish the setup. */

	sc->sc_running = 1;

	return;

 bad:
	free(sc->sc_statusbuf, M_USBDEV, sc->sc_statuslen);
	if (hub) {
		free(hub->ports, M_USBDEV, hub->nports * sizeof(*hub->ports));
		free(hub, M_USBDEV, sizeof(*hub));
	}
	dev->hub = NULL;
}

int
uhub_explore(struct usbd_device *dev)
{
	struct uhub_softc *sc = dev->hub->hubsoftc;
	struct usbd_port *up;
	int status, change;
	int port;

	if (usbd_is_dying(sc->sc_hub))
		return (EIO);

	if (!sc->sc_running)
		return (ENXIO);

	/* Ignore hubs that are too deep. */
	if (sc->sc_hub->depth > USB_HUB_MAX_DEPTH)
		return (EOPNOTSUPP);

	for (port = 1; port <= sc->sc_hub->hub->nports; port++) {
		up = &sc->sc_hub->hub->ports[port-1];

		change = 0;
		status = 0;

		if ((sc->sc_status & (1 << port)) || up->reattach) {
			sc->sc_status &= ~(1 << port);

			if (usbd_get_port_status(dev, port, &up->status))
				continue;

			status = UGETW(up->status.wPortStatus);
			change = UGETW(up->status.wPortChange);
			DPRINTF("%s: port %d status=0x%04x change=0x%04x\n",
			    sc->sc_dev.dv_xname, port, status, change);
		}

		if (up->reattach) {
			change |= UPS_C_CONNECT_STATUS;
			up->reattach = 0;
		}

		if (change & UPS_C_PORT_ENABLED) {
			usbd_clear_port_feature(sc->sc_hub, port,
			    UHF_C_PORT_ENABLE);
			if (change & UPS_C_CONNECT_STATUS) {
				/* Ignore the port error if the device
				   vanished. */
			} else if (status & UPS_PORT_ENABLED) {
				printf("%s: illegal enable change, port %d\n",
				       sc->sc_dev.dv_xname, port);
			} else {
				/* Port error condition. */
				if (up->restartcnt) /* no message first time */
					printf("%s: port error, restarting "
					       "port %d\n",
					       sc->sc_dev.dv_xname, port);

				if (up->restartcnt++ < USBD_RESTART_MAX)
					change |= UPS_C_CONNECT_STATUS;
				else
					printf("%s: port error, giving up "
					       "port %d\n",
					       sc->sc_dev.dv_xname, port);
			}
		}

		if (change & UPS_C_PORT_RESET) {
			usbd_clear_port_feature(sc->sc_hub, port,
			    UHF_C_PORT_RESET);
			change |= UPS_C_CONNECT_STATUS;
		}

		if (change & UPS_C_BH_PORT_RESET &&
		    sc->sc_hub->speed == USB_SPEED_SUPER) {
			usbd_clear_port_feature(sc->sc_hub, port,
			    UHF_C_BH_PORT_RESET);
		}

		if (change & UPS_C_CONNECT_STATUS) {
			if (uhub_port_connect(sc, port, status))
				continue;

			/* The port set up succeeded, reset error count. */
			up->restartcnt = 0;
		}

		if (change & UPS_C_PORT_LINK_STATE) {
			usbd_clear_port_feature(sc->sc_hub, port,
			    UHF_C_PORT_LINK_STATE);
		}

		/* Recursive explore. */
		if (up->device != NULL && up->device->hub != NULL)
			up->device->hub->explore(up->device);
	}

	return (0);
}

/*
 * Called from process context when the hub is gone.
 * Detach all devices on active ports.
 */
int
uhub_detach(struct device *self, int flags)
{
	struct uhub_softc *sc = (struct uhub_softc *)self;
	struct usbd_hub *hub = sc->sc_hub->hub;
	struct usbd_port *rup;
	int port;

	if (hub == NULL)		/* Must be partially working */
		return (0);

	usbd_close_pipe(sc->sc_ipipe);

	for (port = 0; port < hub->nports; port++) {
		rup = &hub->ports[port];
		if (rup->device != NULL) {
			usbd_detach(rup->device, self);
			rup->device = NULL;
		}
	}

	free(hub->ports[0].tt, M_USBDEV,
	    (UHUB_IS_SINGLE_TT(sc) ? 1 : hub->nports) * sizeof(struct usbd_tt));
	free(sc->sc_statusbuf, M_USBDEV, sc->sc_statuslen);
	free(hub->ports, M_USBDEV, hub->nports * sizeof(*hub->ports));
	free(hub, M_USBDEV, sizeof(*hub));
	sc->sc_hub->hub = NULL;

	return (0);
}

/*
 * This is an indication that some port has changed status.  Remember
 * the ports that need attention and notify the USB task thread that
 * we need to be explored again.
 */
void
uhub_intr(struct usbd_xfer *xfer, void *addr, usbd_status status)
{
	struct uhub_softc *sc = addr;
	uint32_t stats = 0;
	int i;

	if (usbd_is_dying(sc->sc_hub))
		return;

	DPRINTF("%s: intr status=%d\n", sc->sc_dev.dv_xname, status);

	if (status == USBD_STALLED)
		usbd_clear_endpoint_stall_async(sc->sc_ipipe);
	else if (status == USBD_NORMAL_COMPLETION) {
		for (i = 0; i < xfer->actlen; i++)
			stats |= (uint32_t)(xfer->buffer[i]) << (i * 8);
		sc->sc_status |= stats;

		usb_needs_explore(sc->sc_hub, 0);
	}
}

int
uhub_port_connect(struct uhub_softc *sc, int port, int status)
{
	struct usbd_port *up = &sc->sc_hub->hub->ports[port-1];
	int speed, change;

	/* We have a connect status change, handle it. */
	usbd_clear_port_feature(sc->sc_hub, port, UHF_C_PORT_CONNECTION);

	/*
	 * If there is already a device on the port the change status
	 * must mean that is has disconnected.  Looking at the
	 * current connect status is not enough to figure this out
	 * since a new unit may have been connected before we handle
	 * the disconnect.
	 */
	if (up->device != NULL) {
		/* Disconnected */
		usbd_detach(up->device, &sc->sc_dev);
		up->device = NULL;
	}

	/* Nothing connected, just ignore it. */
	if ((status & UPS_CURRENT_CONNECT_STATUS) == 0)
		return (0);

	/* Connected */
	if ((status & (UPS_PORT_POWER|UPS_PORT_POWER_SS)) == 0) {
		printf("%s: connected port %d has no power\n", DEVNAME(sc),
		    port);
		return (-1);
	}

	/* Wait for maximum device power up time. */
	usbd_delay_ms(sc->sc_hub, USB_PORT_POWERUP_DELAY);

	/* Reset port, which implies enabling it. */
	if (usbd_reset_port(sc->sc_hub, port)) {
		printf("%s: port %d reset failed\n", DEVNAME(sc), port);
		return (-1);
	}
	/* Get port status again, it might have changed during reset */
	if (usbd_get_port_status(sc->sc_hub, port, &up->status))
		return (-1);

	status = UGETW(up->status.wPortStatus);
	change = UGETW(up->status.wPortChange);
	DPRINTF("%s: port %d status=0x%04x change=0x%04x\n", DEVNAME(sc),
	    port, status, change);

	/* Nothing connected, just ignore it. */
	if ((status & UPS_CURRENT_CONNECT_STATUS) == 0) {
		DPRINTF("%s: port %d, device disappeared after reset\n",
		    DEVNAME(sc), port);
		return (-1);
	}

	/*
	 * Figure out device speed.  This is a bit tricky because
	 * UPS_PORT_POWER_SS and UPS_LOW_SPEED share the same bit.
	 */
	if ((status & UPS_PORT_POWER) == 0)
		status &= ~UPS_PORT_POWER_SS;

	if (status & UPS_HIGH_SPEED)
		speed = USB_SPEED_HIGH;
	else if (status & UPS_LOW_SPEED)
		speed = USB_SPEED_LOW;
	else {
		/*
		 * If there is no power bit set, it is certainly
		 * a Super Speed device, so use the speed of its
		 * parent hub.
		 */
		if (status & UPS_PORT_POWER)
			speed = USB_SPEED_FULL;
		else
			speed = sc->sc_hub->speed;
	}

	/*
	 * Reduce the speed, otherwise we won't setup the proper
	 * transfer methods.
	 */
	if (speed > sc->sc_hub->speed)
		speed = sc->sc_hub->speed;

	/* Get device info and set its address. */
	if (usbd_new_device(&sc->sc_dev, sc->sc_hub->bus, sc->sc_hub->depth + 1,
	    speed, port, up)) {
		/*
		 * The unit refused to accept a new address, or had
		 * some other serious problem.  Since we cannot leave
		 * at 0 we have to disable the port instead.
		 */
		printf("%s: device problem, disabling port %d\n", DEVNAME(sc),
		    port);
		usbd_clear_port_feature(sc->sc_hub, port, UHF_PORT_ENABLE);

		return (-1);
	}

	return (0);
}
