/*	$OpenBSD: umidi.c,v 1.57 2024/05/23 03:21:09 jsg Exp $	*/
/*	$NetBSD: umidi.c,v 1.16 2002/07/11 21:14:32 augustss Exp $	*/
/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Takuya SHIOZAKI (tshiozak@netbsd.org).
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
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/usb/umidireg.h>
#include <dev/usb/umidivar.h>
#include <dev/usb/umidi_quirks.h>

#include <dev/audio_if.h>
#include <dev/midi_if.h>

#ifdef UMIDI_DEBUG
#define DPRINTF(x)	if (umididebug) printf x
#define DPRINTFN(n,x)	if (umididebug >= (n)) printf x
int	umididebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif


static int umidi_open(void *, int,
		      void (*)(void *, int), void (*)(void *), void *);
static void umidi_close(void *);
static int umidi_output(void *, int);
static void umidi_flush(void *);
static void umidi_getinfo(void *, struct midi_info *);

static usbd_status alloc_pipe(struct umidi_endpoint *);
static void free_pipe(struct umidi_endpoint *);

static usbd_status alloc_all_endpoints(struct umidi_softc *);
static void free_all_endpoints(struct umidi_softc *);

static usbd_status alloc_all_jacks(struct umidi_softc *);
static void free_all_jacks(struct umidi_softc *);
static usbd_status bind_jacks_to_mididev(struct umidi_softc *,
					 struct umidi_jack *,
					 struct umidi_jack *,
					 struct umidi_mididev *);
static void unbind_jacks_from_mididev(struct umidi_mididev *);
static void unbind_all_jacks(struct umidi_softc *);
static usbd_status assign_all_jacks_automatically(struct umidi_softc *);
static usbd_status open_out_jack(struct umidi_jack *, void *,
				 void (*)(void *));
static usbd_status open_in_jack(struct umidi_jack *, void *,
				void (*)(void *, int));
static void close_jack(struct umidi_jack *);

static usbd_status attach_mididev(struct umidi_softc *,
				  struct umidi_mididev *);
static usbd_status detach_mididev(struct umidi_mididev *, int);
static usbd_status deactivate_mididev(struct umidi_mididev *);
static usbd_status alloc_all_mididevs(struct umidi_softc *, int);
static void free_all_mididevs(struct umidi_softc *);
static usbd_status attach_all_mididevs(struct umidi_softc *);
static usbd_status detach_all_mididevs(struct umidi_softc *, int);
static usbd_status deactivate_all_mididevs(struct umidi_softc *);

#ifdef UMIDI_DEBUG
static void dump_sc(struct umidi_softc *);
static void dump_ep(struct umidi_endpoint *);
static void dump_jack(struct umidi_jack *);
#endif

static void init_packet(struct umidi_packet *);

static usbd_status start_input_transfer(struct umidi_endpoint *);
static usbd_status start_output_transfer(struct umidi_endpoint *);
static int out_jack_output(struct umidi_jack *, int);
static void out_jack_flush(struct umidi_jack *);
static void in_intr(struct usbd_xfer *, void *, usbd_status);
static void out_intr(struct usbd_xfer *, void *, usbd_status);
static int out_build_packet(int, struct umidi_packet *, uByte, u_char *);


const struct midi_hw_if umidi_hw_if = {
	umidi_open,
	umidi_close,
	umidi_output,
	umidi_flush,		/* flush */
	umidi_getinfo,
	0,		/* ioctl */
};

int umidi_match(struct device *, void *, void *); 
void umidi_attach(struct device *, struct device *, void *); 
int umidi_detach(struct device *, int); 
int umidi_activate(struct device *, int); 

struct cfdriver umidi_cd = { 
	NULL, "umidi", DV_DULL 
}; 

const struct cfattach umidi_ca = { 
	sizeof(struct umidi_softc), 
	umidi_match, 
	umidi_attach, 
	umidi_detach, 
	umidi_activate, 
};

int
umidi_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg *uaa = aux;
	usb_interface_descriptor_t *id;

	DPRINTFN(1,("%s\n", __func__));

	if (uaa->iface == NULL)
		return UMATCH_NONE;

	if (umidi_search_quirk(uaa->vendor, uaa->product, uaa->ifaceno))
		return UMATCH_IFACECLASS_IFACESUBCLASS;

	id = usbd_get_interface_descriptor(uaa->iface);
	if (id!=NULL &&
	    id->bInterfaceClass==UICLASS_AUDIO &&
	    id->bInterfaceSubClass==UISUBCLASS_MIDISTREAM)
		return UMATCH_IFACECLASS_IFACESUBCLASS;

	return UMATCH_NONE;
}

void
umidi_attach(struct device *parent, struct device *self, void *aux)
{
	usbd_status err;
	struct umidi_softc *sc = (struct umidi_softc *)self;
	struct usb_attach_arg *uaa = aux;
	int i;

	DPRINTFN(1,("%s\n", __func__));

	sc->sc_iface = uaa->iface;
	sc->sc_udev = uaa->device;

	sc->sc_quirk =
	    umidi_search_quirk(uaa->vendor, uaa->product, uaa->ifaceno);
	printf("%s: ", sc->sc_dev.dv_xname);
	umidi_print_quirk(sc->sc_quirk);

	err = alloc_all_endpoints(sc);
	if (err!=USBD_NORMAL_COMPLETION)
		goto error;
	err = alloc_all_jacks(sc);
	if (err!=USBD_NORMAL_COMPLETION) {
		free_all_endpoints(sc);
		goto error;
	}
	printf("%s: out=%d, in=%d\n",
	       sc->sc_dev.dv_xname,
	       sc->sc_out_num_jacks, sc->sc_in_num_jacks);

	err = assign_all_jacks_automatically(sc);
	if (err!=USBD_NORMAL_COMPLETION) {
		unbind_all_jacks(sc);
		free_all_jacks(sc);
		free_all_endpoints(sc);
		goto error;
	}
	err = attach_all_mididevs(sc);
	if (err!=USBD_NORMAL_COMPLETION) {
		unbind_all_jacks(sc);
		free_all_jacks(sc);
		free_all_endpoints(sc);
		goto error;
	}

#ifdef UMIDI_DEBUG
	dump_sc(sc);
#endif

	for (i = 0; i < sc->sc_in_num_endpoints; i++)
		(void)start_input_transfer(&sc->sc_in_ep[i]);
	return;
error:
	printf("%s: disabled.\n", sc->sc_dev.dv_xname);
	usbd_deactivate(sc->sc_udev);
}

int
umidi_activate(struct device *self, int act)
{
	struct umidi_softc *sc = (struct umidi_softc *)self;

	if (act == DVACT_DEACTIVATE) {
		DPRINTFN(1,("%s (deactivate)\n", __func__));
		usbd_deactivate(sc->sc_udev);
		deactivate_all_mididevs(sc);
	}
	return 0;
}

int
umidi_detach(struct device *self, int flags)
{
	struct umidi_softc *sc = (struct umidi_softc *)self;

	DPRINTFN(1,("%s\n", __func__));

	detach_all_mididevs(sc, flags);
	free_all_mididevs(sc);
	free_all_jacks(sc);
	free_all_endpoints(sc);

	return 0;
}


/*
 * midi_if stuffs
 */
int
umidi_open(void *addr,
	   int flags,
	   void (*iintr)(void *, int),
	   void (*ointr)(void *),
	   void *arg)
{
	struct umidi_mididev *mididev = addr;
	struct umidi_softc *sc = mididev->sc;

	DPRINTF(("%s: sc=%p\n", __func__, sc));

	if (!sc)
		return ENXIO;
	if (mididev->opened)
		return EBUSY;
	if (usbd_is_dying(sc->sc_udev))
		return EIO;

	mididev->opened = 1;
	mididev->flags = flags;
	if ((mididev->flags & FWRITE) && mididev->out_jack)
		open_out_jack(mididev->out_jack, arg, ointr);
	if ((mididev->flags & FREAD) && mididev->in_jack)
		open_in_jack(mididev->in_jack, arg, iintr);
	return 0;
}

void
umidi_close(void *addr)
{
	int s;
	struct umidi_mididev *mididev = addr;

	s = splusb();
	if ((mididev->flags & FWRITE) && mididev->out_jack)
		close_jack(mididev->out_jack);
	if ((mididev->flags & FREAD) && mididev->in_jack)
		close_jack(mididev->in_jack);
	mididev->opened = 0;
	splx(s);
}

int
umidi_output(void *addr, int d)
{
	struct umidi_mididev *mididev = addr;

	if (!mididev->out_jack || !mididev->opened)
		return 1;

	return out_jack_output(mididev->out_jack, d);
}

void
umidi_flush(void *addr)
{
	struct umidi_mididev *mididev = addr;

	if (!mididev->out_jack || !mididev->opened)
		return;

	out_jack_flush(mididev->out_jack);
}

void
umidi_getinfo(void *addr, struct midi_info *mi)
{
	struct umidi_mididev *mididev = addr;

	mi->name = "USB MIDI I/F"; /* XXX: model name */
	mi->props = MIDI_PROP_OUT_INTR;
	if (mididev->in_jack)
		mi->props |= MIDI_PROP_CAN_INPUT;
}


/*
 * each endpoint stuffs
 */

/* alloc/free pipe */
static usbd_status
alloc_pipe(struct umidi_endpoint *ep)
{
	struct umidi_softc *sc = ep->sc;
	usbd_status err;

	DPRINTF(("%s: alloc_pipe %p\n", sc->sc_dev.dv_xname, ep));
	SIMPLEQ_INIT(&ep->intrq);
	ep->pending = 0;
	ep->busy = 0;
	ep->used = 0;
	ep->xfer = usbd_alloc_xfer(sc->sc_udev);
	if (ep->xfer == NULL)
		return USBD_NOMEM;
	ep->buffer = usbd_alloc_buffer(ep->xfer, ep->packetsize);
	if (ep->buffer == NULL) {
		usbd_free_xfer(ep->xfer);
		return USBD_NOMEM;
	}
	err = usbd_open_pipe(sc->sc_iface, ep->addr, 0, &ep->pipe);
	if (err != USBD_NORMAL_COMPLETION) {
		usbd_free_xfer(ep->xfer);
		return err;
	}
	return USBD_NORMAL_COMPLETION;
}

static void
free_pipe(struct umidi_endpoint *ep)
{
	DPRINTF(("%s: %s %p\n", ep->sc->sc_dev.dv_xname, __func__, ep));
	usbd_close_pipe(ep->pipe);
	usbd_free_xfer(ep->xfer);
}


/* alloc/free the array of endpoint structures */

static usbd_status alloc_all_endpoints_fixed_ep(struct umidi_softc *);
static usbd_status alloc_all_endpoints_yamaha(struct umidi_softc *);
static usbd_status alloc_all_endpoints_genuine(struct umidi_softc *);

static usbd_status
alloc_all_endpoints(struct umidi_softc *sc)
{
	usbd_status err;
	struct umidi_endpoint *ep;
	int i;

	sc->sc_out_num_jacks = sc->sc_in_num_jacks = 0;

	if (UMQ_ISTYPE(sc, UMQ_TYPE_FIXED_EP))
		err = alloc_all_endpoints_fixed_ep(sc);
	else if (UMQ_ISTYPE(sc, UMQ_TYPE_YAMAHA))
		err = alloc_all_endpoints_yamaha(sc);
	else
		err = alloc_all_endpoints_genuine(sc);
	if (err!=USBD_NORMAL_COMPLETION)
		return err;

	ep = sc->sc_endpoints;
	for (i=sc->sc_out_num_endpoints+sc->sc_in_num_endpoints; i>0; i--) {
		err = alloc_pipe(ep);
		if (err!=USBD_NORMAL_COMPLETION) {
			while(ep != sc->sc_endpoints) {
				ep--;
				free_pipe(ep);
			}
			free(sc->sc_endpoints, M_USBDEV,
			    (sc->sc_out_num_endpoints + sc->sc_in_num_endpoints)
			    * sizeof(*sc->sc_endpoints));
			sc->sc_endpoints = sc->sc_out_ep = sc->sc_in_ep = NULL;
			break;
		}
		ep++;
	}
	return err;
}

static void
free_all_endpoints(struct umidi_softc *sc)
{
	int i;

	for (i=0; i<sc->sc_in_num_endpoints+sc->sc_out_num_endpoints; i++)
	    free_pipe(&sc->sc_endpoints[i]);
	free(sc->sc_endpoints, M_USBDEV, (sc->sc_out_num_endpoints +
	    sc->sc_in_num_endpoints) * sizeof(*sc->sc_endpoints));
	sc->sc_endpoints = sc->sc_out_ep = sc->sc_in_ep = NULL;
}

static usbd_status
alloc_all_endpoints_fixed_ep(struct umidi_softc *sc)
{
	struct umq_fixed_ep_desc *fp;
	struct umidi_endpoint *ep;
	usb_endpoint_descriptor_t *epd;
	int i;

	fp = umidi_get_quirk_data_from_type(sc->sc_quirk,
					    UMQ_TYPE_FIXED_EP);
	sc->sc_out_num_endpoints = fp->num_out_ep;
	sc->sc_in_num_endpoints = fp->num_in_ep;
	sc->sc_endpoints = mallocarray(sc->sc_out_num_endpoints +
	    sc->sc_in_num_endpoints, sizeof(*sc->sc_endpoints), M_USBDEV,
	    M_WAITOK | M_CANFAIL);
	if (!sc->sc_endpoints)
		return USBD_NOMEM;
	sc->sc_out_ep = sc->sc_out_num_endpoints ? sc->sc_endpoints : NULL;
	sc->sc_in_ep =
	    sc->sc_in_num_endpoints ?
		sc->sc_endpoints+sc->sc_out_num_endpoints : NULL;

	if (sc->sc_in_ep == NULL || sc->sc_out_ep == NULL) {
		printf("%s: cannot get valid endpoints", sc->sc_dev.dv_xname);
		goto error;
	}
	ep = &sc->sc_out_ep[0];
	for (i=0; i<sc->sc_out_num_endpoints; i++) {
		epd = usbd_interface2endpoint_descriptor(
			sc->sc_iface,
			fp->out_ep[i].ep);
		if (!epd) {
			DPRINTF(("%s: cannot get endpoint descriptor(out:%d)\n",
			       sc->sc_dev.dv_xname, fp->out_ep[i].ep));
			goto error;
		}
		if (UE_GET_XFERTYPE(epd->bmAttributes)!=UE_BULK ||
		    UE_GET_DIR(epd->bEndpointAddress)!=UE_DIR_OUT) {
			printf("%s: illegal endpoint(out:%d)\n",
			       sc->sc_dev.dv_xname, fp->out_ep[i].ep);
			goto error;
		}
		ep->sc = sc;
		ep->packetsize = UGETW(epd->wMaxPacketSize);
		ep->addr = epd->bEndpointAddress;
		ep->num_jacks = fp->out_ep[i].num_jacks;
		sc->sc_out_num_jacks += fp->out_ep[i].num_jacks;
		ep->num_open = 0;
		memset(ep->jacks, 0, sizeof(ep->jacks));
		ep++;
	}
	ep = &sc->sc_in_ep[0];
	for (i=0; i<sc->sc_in_num_endpoints; i++) {
		epd = usbd_interface2endpoint_descriptor(
			sc->sc_iface,
			fp->in_ep[i].ep);
		if (!epd) {
			DPRINTF(("%s: cannot get endpoint descriptor(in:%d)\n",
			       sc->sc_dev.dv_xname, fp->in_ep[i].ep));
			goto error;
		}
		if (UE_GET_XFERTYPE(epd->bmAttributes)!=UE_BULK ||
		    UE_GET_DIR(epd->bEndpointAddress)!=UE_DIR_IN) {
			printf("%s: illegal endpoint(in:%d)\n",
			       sc->sc_dev.dv_xname, fp->in_ep[i].ep);
			goto error;
		}
		ep->sc = sc;
		ep->addr = epd->bEndpointAddress;
		ep->packetsize = UGETW(epd->wMaxPacketSize);
		ep->num_jacks = fp->in_ep[i].num_jacks;
		sc->sc_in_num_jacks += fp->in_ep[i].num_jacks;
		ep->num_open = 0;
		memset(ep->jacks, 0, sizeof(ep->jacks));
		ep++;
	}

	return USBD_NORMAL_COMPLETION;
error:
	free(sc->sc_endpoints, M_USBDEV, (sc->sc_out_num_endpoints +
	    sc->sc_in_num_endpoints) * sizeof(*sc->sc_endpoints));
	sc->sc_endpoints = NULL;
	return USBD_INVAL;
}

static usbd_status
alloc_all_endpoints_yamaha(struct umidi_softc *sc)
{
	/* This driver currently supports max 1in/1out bulk endpoints */
	usb_descriptor_t *desc;
	usb_endpoint_descriptor_t *epd;
	int out_addr, in_addr, in_packetsize, i, dir;
	size_t remain, descsize;

	out_addr = in_addr = 0;

	/* detect endpoints */
	desc = TO_D(usbd_get_interface_descriptor(sc->sc_iface));
	for (i=(int)TO_IFD(desc)->bNumEndpoints-1; i>=0; i--) {
		epd = usbd_interface2endpoint_descriptor(sc->sc_iface, i);
		if (epd == NULL)
			continue;

		if (UE_GET_XFERTYPE(epd->bmAttributes) == UE_BULK) {
			dir = UE_GET_DIR(epd->bEndpointAddress);
			if (dir==UE_DIR_OUT && !out_addr)
				out_addr = epd->bEndpointAddress;
			else if (dir==UE_DIR_IN && !in_addr) {
				in_addr = epd->bEndpointAddress;
				in_packetsize = UGETW(epd->wMaxPacketSize);
			}
		}
	}
	desc = NEXT_D(desc);

	/* count jacks */
	if (!(desc->bDescriptorType==UDESC_CS_INTERFACE &&
	      desc->bDescriptorSubtype==UMIDI_MS_HEADER))
		return USBD_INVAL;
	remain = (size_t)UGETW(TO_CSIFD(desc)->wTotalLength) -
		(size_t)desc->bLength;
	desc = NEXT_D(desc);

	while (remain>=sizeof(usb_descriptor_t)) {
		descsize = desc->bLength;
		if (descsize>remain || descsize==0)
			break;
		if (desc->bDescriptorType==UDESC_CS_INTERFACE &&
		    remain>=UMIDI_JACK_DESCRIPTOR_SIZE) {
			if (desc->bDescriptorSubtype==UMIDI_OUT_JACK)
				sc->sc_out_num_jacks++;
			else if (desc->bDescriptorSubtype==UMIDI_IN_JACK)
				sc->sc_in_num_jacks++;
		}
		desc = NEXT_D(desc);
		remain-=descsize;
	}

	/* validate some parameters */
	if (sc->sc_out_num_jacks>UMIDI_MAX_EPJACKS)
		sc->sc_out_num_jacks = UMIDI_MAX_EPJACKS;
	if (sc->sc_in_num_jacks>UMIDI_MAX_EPJACKS)
		sc->sc_in_num_jacks = UMIDI_MAX_EPJACKS;
	if (sc->sc_out_num_jacks && out_addr)
		sc->sc_out_num_endpoints = 1;
	else {
		sc->sc_out_num_endpoints = 0;
		sc->sc_out_num_jacks = 0;
	}
	if (sc->sc_in_num_jacks && in_addr)
		sc->sc_in_num_endpoints = 1;
	else {
		sc->sc_in_num_endpoints = 0;
		sc->sc_in_num_jacks = 0;
	}
	sc->sc_endpoints = mallocarray(sc->sc_out_num_endpoints +
	    sc->sc_in_num_endpoints, sizeof(struct umidi_endpoint),
	    M_USBDEV, M_WAITOK | M_CANFAIL);
	if (!sc->sc_endpoints)
		return USBD_NOMEM;
	if (sc->sc_out_num_endpoints) {
		sc->sc_out_ep = sc->sc_endpoints;
		sc->sc_out_ep->sc = sc;
		sc->sc_out_ep->addr = out_addr;
		sc->sc_out_ep->packetsize = UGETW(epd->wMaxPacketSize);
		sc->sc_out_ep->num_jacks = sc->sc_out_num_jacks;
		sc->sc_out_ep->num_open = 0;
		memset(sc->sc_out_ep->jacks, 0, sizeof(sc->sc_out_ep->jacks));
	} else
		sc->sc_out_ep = NULL;

	if (sc->sc_in_num_endpoints) {
		sc->sc_in_ep = sc->sc_endpoints+sc->sc_out_num_endpoints;
		sc->sc_in_ep->sc = sc;
		sc->sc_in_ep->addr = in_addr;
		sc->sc_in_ep->packetsize = in_packetsize;
		sc->sc_in_ep->num_jacks = sc->sc_in_num_jacks;
		sc->sc_in_ep->num_open = 0;
		memset(sc->sc_in_ep->jacks, 0, sizeof(sc->sc_in_ep->jacks));
	} else
		sc->sc_in_ep = NULL;

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
alloc_all_endpoints_genuine(struct umidi_softc *sc)
{
	usb_interface_descriptor_t *interface_desc;
	usb_config_descriptor_t *config_desc;
	usb_descriptor_t *desc;
	size_t remain, descsize;
	struct umidi_endpoint *p, *q, *lowest, *endep, tmpep;
	int epaddr, eppacketsize, num_ep;

	interface_desc = usbd_get_interface_descriptor(sc->sc_iface);
	num_ep = interface_desc->bNumEndpoints;
	sc->sc_endpoints = p = mallocarray(num_ep,
	    sizeof(struct umidi_endpoint), M_USBDEV, M_WAITOK | M_CANFAIL);
	if (!p)
		return USBD_NOMEM;

	sc->sc_out_num_endpoints = sc->sc_in_num_endpoints = 0;
	epaddr = -1;

	/* get the list of endpoints for midi stream */
	config_desc = usbd_get_config_descriptor(sc->sc_udev);
	desc = (usb_descriptor_t *) config_desc;
	remain = (size_t)UGETW(config_desc->wTotalLength);
	while (remain>=sizeof(usb_descriptor_t)) {
		descsize = desc->bLength;
		if (descsize>remain || descsize==0)
			break;
		if (desc->bDescriptorType==UDESC_ENDPOINT &&
		    remain>=USB_ENDPOINT_DESCRIPTOR_SIZE &&
		    UE_GET_XFERTYPE(TO_EPD(desc)->bmAttributes) == UE_BULK) {
			epaddr = TO_EPD(desc)->bEndpointAddress;
			eppacketsize = UGETW(TO_EPD(desc)->wMaxPacketSize);
		} else if (desc->bDescriptorType==UDESC_CS_ENDPOINT &&
			   remain>=UMIDI_CS_ENDPOINT_DESCRIPTOR_SIZE &&
			   epaddr!=-1) {
			if (num_ep>0) {
				num_ep--;
				p->sc = sc;
				p->addr = epaddr;
				p->packetsize = eppacketsize;
				p->num_jacks = TO_CSEPD(desc)->bNumEmbMIDIJack;
				if (UE_GET_DIR(epaddr)==UE_DIR_OUT) {
					sc->sc_out_num_endpoints++;
					sc->sc_out_num_jacks += p->num_jacks;
				} else {
					sc->sc_in_num_endpoints++;
					sc->sc_in_num_jacks += p->num_jacks;
				}
				p++;
			}
		} else
			epaddr = -1;
		desc = NEXT_D(desc);
		remain-=descsize;
	}

	/* sort endpoints */
	num_ep = sc->sc_out_num_endpoints + sc->sc_in_num_endpoints;
	p = sc->sc_endpoints;
	endep = p + num_ep;
	while (p<endep) {
		lowest = p;
		for (q=p+1; q<endep; q++) {
			if ((UE_GET_DIR(lowest->addr)==UE_DIR_IN &&
			     UE_GET_DIR(q->addr)==UE_DIR_OUT) ||
			    ((UE_GET_DIR(lowest->addr)==
			      UE_GET_DIR(q->addr)) &&
			     (UE_GET_ADDR(lowest->addr)>
			      UE_GET_ADDR(q->addr))))
				lowest = q;
		}
		if (lowest != p) {
			memcpy((void *)&tmpep, (void *)p, sizeof(tmpep));
			memcpy((void *)p, (void *)lowest, sizeof(tmpep));
			memcpy((void *)lowest, (void *)&tmpep, sizeof(tmpep));
		}
		p->num_open = 0;
		p++;
	}

	sc->sc_out_ep = sc->sc_out_num_endpoints ? sc->sc_endpoints : NULL;
	sc->sc_in_ep =
	    sc->sc_in_num_endpoints ?
		sc->sc_endpoints+sc->sc_out_num_endpoints : NULL;

	return USBD_NORMAL_COMPLETION;
}


/*
 * jack stuffs
 */

static usbd_status
alloc_all_jacks(struct umidi_softc *sc)
{
	int i, j;
	struct umidi_endpoint *ep;
	struct umidi_jack *jack, **rjack;

	/* allocate/initialize structures */
	sc->sc_jacks = mallocarray(sc->sc_in_num_jacks + sc->sc_out_num_jacks,
	    sizeof(*sc->sc_out_jacks), M_USBDEV, M_WAITOK | M_CANFAIL);
	if (!sc->sc_jacks)
		return USBD_NOMEM;
	sc->sc_out_jacks =
	    sc->sc_out_num_jacks ? sc->sc_jacks : NULL;
	sc->sc_in_jacks =
	    sc->sc_in_num_jacks ? sc->sc_jacks+sc->sc_out_num_jacks : NULL;

	jack = &sc->sc_out_jacks[0];
	for (i=0; i<sc->sc_out_num_jacks; i++) {
		jack->opened = 0;
		jack->binded = 0;
		jack->arg = NULL;
		jack->u.out.intr = NULL;
		jack->intr = 0;
		jack->cable_number = i;
		jack++;
	}
	jack = &sc->sc_in_jacks[0];
	for (i=0; i<sc->sc_in_num_jacks; i++) {
		jack->opened = 0;
		jack->binded = 0;
		jack->arg = NULL;
		jack->u.in.intr = NULL;
		jack->cable_number = i;
		jack++;
	}

	/* assign each jacks to each endpoints */
	jack = &sc->sc_out_jacks[0];
	ep = &sc->sc_out_ep[0];
	for (i=0; i<sc->sc_out_num_endpoints; i++) {
		rjack = &ep->jacks[0];
		for (j=0; j<ep->num_jacks; j++) {
			*rjack = jack;
			jack->endpoint = ep;
			jack++;
			rjack++;
		}
		ep++;
	}
	jack = &sc->sc_in_jacks[0];
	ep = &sc->sc_in_ep[0];
	for (i=0; i<sc->sc_in_num_endpoints; i++) {
		rjack = &ep->jacks[0];
		for (j=0; j<ep->num_jacks; j++) {
			*rjack = jack;
			jack->endpoint = ep;
			jack++;
			rjack++;
		}
		ep++;
	}

	return USBD_NORMAL_COMPLETION;
}

static void
free_all_jacks(struct umidi_softc *sc)
{
	int s, jacks = sc->sc_in_num_jacks + sc->sc_out_num_jacks;

	s = splusb();
	if (sc->sc_out_jacks) {
		free(sc->sc_jacks, M_USBDEV, jacks * sizeof(*sc->sc_out_jacks));
		sc->sc_jacks = sc->sc_in_jacks = sc->sc_out_jacks = NULL;
		sc->sc_out_num_jacks = sc->sc_in_num_jacks = 0;
	}
	splx(s);
}

static usbd_status
bind_jacks_to_mididev(struct umidi_softc *sc,
		      struct umidi_jack *out_jack,
		      struct umidi_jack *in_jack,
		      struct umidi_mididev *mididev)
{
	if ((out_jack && out_jack->binded) || (in_jack && in_jack->binded))
		return USBD_IN_USE;
	if (mididev->out_jack || mididev->in_jack)
		return USBD_IN_USE;

	if (out_jack)
		out_jack->binded = 1;
	if (in_jack)
		in_jack->binded = 1;
	mididev->in_jack = in_jack;
	mididev->out_jack = out_jack;

	return USBD_NORMAL_COMPLETION;
}

static void
unbind_jacks_from_mididev(struct umidi_mididev *mididev)
{
	if ((mididev->flags & FWRITE) && mididev->out_jack)
		close_jack(mididev->out_jack);
	if ((mididev->flags & FREAD) && mididev->in_jack)
		close_jack(mididev->in_jack);

	if (mididev->out_jack)
		mididev->out_jack->binded = 0;
	if (mididev->in_jack)
		mididev->in_jack->binded = 0;
	mididev->out_jack = mididev->in_jack = NULL;
}

static void
unbind_all_jacks(struct umidi_softc *sc)
{
	int i;

	if (sc->sc_mididevs)
		for (i=0; i<sc->sc_num_mididevs; i++) {
			unbind_jacks_from_mididev(&sc->sc_mididevs[i]);
		}
}

static usbd_status
assign_all_jacks_automatically(struct umidi_softc *sc)
{
	usbd_status err;
	int i;
	struct umidi_jack *out, *in;

	err =
	    alloc_all_mididevs(sc,
			       max(sc->sc_out_num_jacks, sc->sc_in_num_jacks));
	if (err!=USBD_NORMAL_COMPLETION)
		return err;

	for (i=0; i<sc->sc_num_mididevs; i++) {
		out = (i<sc->sc_out_num_jacks) ? &sc->sc_out_jacks[i]:NULL;
		in = (i<sc->sc_in_num_jacks) ? &sc->sc_in_jacks[i]:NULL;
		err = bind_jacks_to_mididev(sc, out, in, &sc->sc_mididevs[i]);
		if (err!=USBD_NORMAL_COMPLETION) {
			free_all_mididevs(sc);
			return err;
		}
	}

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
open_out_jack(struct umidi_jack *jack, void *arg, void (*intr)(void *))
{
	if (jack->opened)
		return USBD_IN_USE;

	jack->arg = arg;
	jack->u.out.intr = intr;
	init_packet(&jack->packet);
	jack->opened = 1;
	jack->endpoint->num_open++;
	
	return USBD_NORMAL_COMPLETION;
}

static usbd_status
open_in_jack(struct umidi_jack *jack, void *arg, void (*intr)(void *, int))
{
	if (jack->opened)
		return USBD_IN_USE;

	jack->arg = arg;
	jack->u.in.intr = intr;
	jack->opened = 1;
	jack->endpoint->num_open++;	
	
	return USBD_NORMAL_COMPLETION;
}

static void
close_jack(struct umidi_jack *jack)
{
	if (jack->opened) {
		jack->opened = 0;
		jack->endpoint->num_open--;
	}
}

static usbd_status
attach_mididev(struct umidi_softc *sc, struct umidi_mididev *mididev)
{
	if (mididev->sc)
		return USBD_IN_USE;

	mididev->sc = sc;

	mididev->mdev = midi_attach_mi(&umidi_hw_if, mididev, &sc->sc_dev);

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
detach_mididev(struct umidi_mididev *mididev, int flags)
{
	if (!mididev->sc)
		return USBD_NO_ADDR;

	if (mididev->opened)
		umidi_close(mididev);
	unbind_jacks_from_mididev(mididev);

	if (mididev->mdev)
		config_detach(mididev->mdev, flags);

	mididev->sc = NULL;

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
deactivate_mididev(struct umidi_mididev *mididev)
{
	if (mididev->out_jack)
		mididev->out_jack->binded = 0;
	if (mididev->in_jack)
		mididev->in_jack->binded = 0;
	config_deactivate(mididev->mdev);

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
alloc_all_mididevs(struct umidi_softc *sc, int nmidi)
{
	sc->sc_num_mididevs = nmidi;
	sc->sc_mididevs = mallocarray(nmidi, sizeof(*sc->sc_mididevs),
	    M_USBDEV, M_WAITOK | M_CANFAIL | M_ZERO);
	if (!sc->sc_mididevs)
		return USBD_NOMEM;

	return USBD_NORMAL_COMPLETION;
}

static void
free_all_mididevs(struct umidi_softc *sc)
{
	if (sc->sc_mididevs)
		free(sc->sc_mididevs, M_USBDEV,
		    sc->sc_num_mididevs * sizeof(*sc->sc_mididevs));
	sc->sc_mididevs = NULL;
	sc->sc_num_mididevs = 0;
}

static usbd_status
attach_all_mididevs(struct umidi_softc *sc)
{
	usbd_status err;
	int i;

	if (sc->sc_mididevs)
		for (i=0; i<sc->sc_num_mididevs; i++) {
			err = attach_mididev(sc, &sc->sc_mididevs[i]);
			if (err!=USBD_NORMAL_COMPLETION)
				return err;
		}

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
detach_all_mididevs(struct umidi_softc *sc, int flags)
{
	usbd_status err;
	int i;

	if (sc->sc_mididevs)
		for (i=0; i<sc->sc_num_mididevs; i++) {
			err = detach_mididev(&sc->sc_mididevs[i], flags);
			if (err!=USBD_NORMAL_COMPLETION)
				return err;
		}

	return USBD_NORMAL_COMPLETION;
}

static usbd_status
deactivate_all_mididevs(struct umidi_softc *sc)
{
	usbd_status err;
	int i;

	if (sc->sc_mididevs)
		for (i=0; i<sc->sc_num_mididevs; i++) {
			err = deactivate_mididev(&sc->sc_mididevs[i]);
			if (err!=USBD_NORMAL_COMPLETION)
				return err;
		}

	return USBD_NORMAL_COMPLETION;
}

#ifdef UMIDI_DEBUG
static void
dump_sc(struct umidi_softc *sc)
{
	int i;

	DPRINTFN(10, ("%s: %s\n", sc->sc_dev.dv_xname, __func__));
	for (i=0; i<sc->sc_out_num_endpoints; i++) {
		DPRINTFN(10, ("\tout_ep(%p):\n", &sc->sc_out_ep[i]));
		dump_ep(&sc->sc_out_ep[i]);
	}
	for (i=0; i<sc->sc_in_num_endpoints; i++) {
		DPRINTFN(10, ("\tin_ep(%p):\n", &sc->sc_in_ep[i]));
		dump_ep(&sc->sc_in_ep[i]);
	}
}

static void
dump_ep(struct umidi_endpoint *ep)
{
	int i;
	for (i=0; i<ep->num_jacks; i++) {
		DPRINTFN(10, ("\t\tjack(%p):\n", ep->jacks[i]));
		dump_jack(ep->jacks[i]);
	}
}
static void
dump_jack(struct umidi_jack *jack)
{
	DPRINTFN(10, ("\t\t\tep=%p\n",
		      jack->endpoint));
}

#endif /* UMIDI_DEBUG */



/*
 * MUX MIDI PACKET
 */

static const int packet_length[16] = {
	/*0*/	-1,
	/*1*/	-1,
	/*2*/	2,
	/*3*/	3,
	/*4*/	3,
	/*5*/	1,
	/*6*/	2,
	/*7*/	3,
	/*8*/	3,
	/*9*/	3,
	/*A*/	3,
	/*B*/	3,
	/*C*/	2,
	/*D*/	2,
	/*E*/	3,
	/*F*/	1,
};

#define	GET_CN(p)		(((unsigned char)(p)>>4)&0x0F)
#define GET_CIN(p)		((unsigned char)(p)&0x0F)

static void
init_packet(struct umidi_packet *packet)
{
	packet->status = 0;
	packet->index = 0;
}

static usbd_status
start_input_transfer(struct umidi_endpoint *ep)
{
	usbd_status err;
	usbd_setup_xfer(ep->xfer, ep->pipe,
			(void *)ep,
			ep->buffer, ep->packetsize,
			USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT, in_intr);
	err = usbd_transfer(ep->xfer);
	if (err != USBD_NORMAL_COMPLETION && err != USBD_IN_PROGRESS) {
		DPRINTF(("%s: %s: usbd_transfer() failed err=%s\n", 
			ep->sc->sc_dev.dv_xname, __func__, usbd_errstr(err)));
		return err;
	}
	return USBD_NORMAL_COMPLETION;
}

static usbd_status
start_output_transfer(struct umidi_endpoint *ep)
{
	usbd_status err;
	usbd_setup_xfer(ep->xfer, ep->pipe,
			(void *)ep,
			ep->buffer, ep->used,
			USBD_NO_COPY, USBD_NO_TIMEOUT, out_intr);
	err = usbd_transfer(ep->xfer);
	if (err != USBD_NORMAL_COMPLETION && err != USBD_IN_PROGRESS) {
		DPRINTF(("%s: %s: usbd_transfer() failed err=%s\n", 
			ep->sc->sc_dev.dv_xname, __func__, usbd_errstr(err)));
		return err;
	}
	ep->used = ep->packetsize;
	return USBD_NORMAL_COMPLETION;
}


#ifdef UMIDI_DEBUG
#define DPR_PACKET(dir, sc, p)						\
	DPRINTFN(500,							\
		 ("%s: umidi packet(" #dir "): %02X %02X %02X %02X\n",	\
		  sc->sc_dev.dv_xname,				\
		  (unsigned char)(p)->buffer[0],			\
		  (unsigned char)(p)->buffer[1],			\
		  (unsigned char)(p)->buffer[2],			\
		  (unsigned char)(p)->buffer[3]));
#else
#define DPR_PACKET(dir, sc, p)
#endif

static int
out_jack_output(struct umidi_jack *j, int d)
{
	struct umidi_endpoint *ep = j->endpoint;
	struct umidi_softc *sc = ep->sc;
	int s;

	if (usbd_is_dying(sc->sc_udev))
		return 1;
	if (!j->opened)
		return 1;
	s = splusb();
	if (ep->busy) {
		if (!j->intr) {
			SIMPLEQ_INSERT_TAIL(&ep->intrq, j, intrq_entry);
			ep->pending++;
			j->intr = 1;
		}		
		splx(s);
		return 0;
	}
	if (!out_build_packet(j->cable_number, &j->packet, d,
		ep->buffer + ep->used)) {
		splx(s);
		return 1;
	}
	ep->used += UMIDI_PACKET_SIZE;
	if (ep->used == ep->packetsize) {
		ep->busy = 1;
		start_output_transfer(ep);
	}
	splx(s);
	return 1;
}

static void
out_jack_flush(struct umidi_jack *j)
{
	struct umidi_endpoint *ep = j->endpoint;
	int s;

	if (usbd_is_dying(ep->sc->sc_udev) || !j->opened)
		return;
		
	s = splusb();	
	if (ep->used != 0 && !ep->busy) {
		ep->busy = 1;
		start_output_transfer(ep);
	}
	splx(s);
}


static void
in_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	int cn, evlen, remain, i;
	unsigned char *buf;
	struct umidi_endpoint *ep = (struct umidi_endpoint *)priv;
	struct umidi_jack *jack;

	if (usbd_is_dying(ep->sc->sc_udev))
		return;

	usbd_get_xfer_status(xfer, NULL, NULL, &remain, NULL);
	if (status != USBD_NORMAL_COMPLETION) {
		DPRINTF(("%s: abnormal status: %s\n", __func__, usbd_errstr(status)));
		return;
	}
	buf = ep->buffer;
	while (remain >= UMIDI_PACKET_SIZE) {
		cn = GET_CN(buf[0]);
		if (cn < ep->num_jacks && (jack = ep->jacks[cn]) &&
		    jack->binded && jack->opened &&  jack->u.in.intr) {
		    	evlen = packet_length[GET_CIN(buf[0])];
			mtx_enter(&audio_lock);
			for (i=0; i<evlen; i++)
				(*jack->u.in.intr)(jack->arg, buf[i+1]);
			mtx_leave(&audio_lock);
		}
		buf += UMIDI_PACKET_SIZE;
		remain -= UMIDI_PACKET_SIZE;
	}
	(void)start_input_transfer(ep);
}

static void
out_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct umidi_endpoint *ep = (struct umidi_endpoint *)priv;
	struct umidi_softc *sc = ep->sc;
	struct umidi_jack *j;
	unsigned pending;
	
	if (usbd_is_dying(sc->sc_udev))
		return;

	ep->used = 0;
	ep->busy = 0;
	for (pending = ep->pending; pending > 0; pending--) {
		j = SIMPLEQ_FIRST(&ep->intrq);
#ifdef DIAGNOSTIC
		if (j == NULL) {
			printf("umidi: missing intr entry\n");
			break;
		}
#endif
		SIMPLEQ_REMOVE_HEAD(&ep->intrq, intrq_entry);
		ep->pending--;
		j->intr = 0;
		mtx_enter(&audio_lock);
		if (j->opened && j->u.out.intr)
			(*j->u.out.intr)(j->arg);
		mtx_leave(&audio_lock);
	}
}

#define UMIDI_VOICELEN(status) 	(umidi_evlen[((status) >> 4) & 7])
static const unsigned int umidi_evlen[] = { 4, 4, 4, 4, 3, 3, 4 };

#define EV_SYSEX	0xf0
#define EV_MTC		0xf1
#define EV_SPP		0xf2
#define EV_SONGSEL	0xf3
#define EV_TUNE_REQ	0xf6
#define EV_SYSEX_STOP	0xf7

static int
out_build_packet(int cable_number, struct umidi_packet *packet, 
    uByte data, u_char *obuf)
{
	if (data >= 0xf8) {		/* is it a realtime message ? */
		obuf[0] = data >> 4 | cable_number << 4;
		obuf[1] = data;
		obuf[2] = 0;
		obuf[3] = 0;
		return 1;
	}
	if (data >= 0xf0) {		/* is it a common message ? */
		switch(data) {
		case EV_SYSEX:
			packet->buf[1] = packet->status = data;
			packet->index = 2;
			break;
		case EV_SYSEX_STOP:
			if (packet->status != EV_SYSEX) break;
			if (packet->index == 0)
				packet->index = 1; 
			packet->status = data;
			packet->buf[packet->index++] = data;
			packet->buf[0] = (0x4 - 1 + packet->index) | cable_number << 4;
			goto packetready;
		case EV_TUNE_REQ: 
			packet->status = data;
			packet->buf[0] = 0x5 | cable_number << 4;
			packet->index = 1;
			goto packetready;
		default:
			packet->status = data;
			break;
		}
		return 0;
	}	
	if (data >= 0x80) {		/* is it a voice message ? */
		packet->status = data;
		packet->index = 0;
		return 0;
	} 

	/* else it is a data byte */	
	if (packet->status >= 0xf0) {
		switch(packet->status) {
		case EV_SYSEX:		/* sysex starts or continues */
			if (packet->index == 0)
				packet->index = 1; 

			packet->buf[packet->index++] = data;
			if (packet->index >= UMIDI_PACKET_SIZE) {
				packet->buf[0] = 0x4 | cable_number << 4;
				goto packetready;
			}
			break;
		case EV_MTC:		/* messages with 1 data byte */
		case EV_SONGSEL:	
			packet->buf[0] = 0x2 | cable_number << 4;
			packet->buf[1] = packet->status;
			packet->buf[2] = data;
			packet->index = 3;
			goto packetready;
		case EV_SPP:		/* messages with 2 data bytes */
			if (packet->index == 0) {
				packet->buf[0] = 0x3 | cable_number << 4;
				packet->index = 1;
			}
			packet->buf[packet->index++] = data;
			if (packet->index >= UMIDI_PACKET_SIZE) {
				packet->buf[1] = packet->status;
				goto packetready;
			}
			break;
		default:		/* ignore data with unknown status */
			break;
		}
		return 0;
	}
	if (packet->status >= 0x80) {	/* is it a voice message ? */
		if (packet->index == 0) {
			packet->buf[0] = packet->status >> 4 | cable_number << 4;
			packet->buf[1] = packet->status;
			packet->index = 2;
		}
		packet->buf[packet->index++] = data;
		if (packet->index >= UMIDI_VOICELEN(packet->status))
			goto packetready;
	}
	/* ignore data with unknown status */
	return 0;
	
packetready:
	while (packet->index < UMIDI_PACKET_SIZE)
		packet->buf[packet->index++] = 0;
	packet->index = 0;
	memcpy(obuf, packet->buf, UMIDI_PACKET_SIZE);
	return 1;
}
