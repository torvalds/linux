/*	$OpenBSD: if_wi_usb.c,v 1.79 2025/06/10 13:32:26 claudio Exp $ */

/*
 * Copyright (c) 2003 Dale Rahn. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/kthread.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#define ROUNDUP64(x) (((x)+63) & ~63)

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ioctl.h>

#include <machine/bus.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

#include <dev/usb/if_wi_usb.h>

int wi_usb_do_transmit_sync(struct wi_usb_softc *wsc, struct wi_usb_chain *c, 
    void *ident);
void wi_usb_txeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status);
void wi_usb_txeof_frm(struct usbd_xfer *xfer, void *priv,
    usbd_status status);
void wi_usb_rxeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status);
void wi_usb_intr(struct usbd_xfer *xfer, void *priv,
    usbd_status status);
void wi_usb_stop(struct wi_usb_softc *usc);
int wi_usb_tx_list_init(struct wi_usb_softc *usc);
int wi_usb_rx_list_init(struct wi_usb_softc *usc);
int wi_usb_open_pipes(struct wi_usb_softc *usc);
void wi_usb_cmdresp(struct wi_usb_chain *c);
void wi_usb_rridresp(struct wi_usb_chain *c);
void wi_usb_wridresp(struct wi_usb_chain *c);
void wi_usb_infofrm(struct wi_usb_chain *c, int len);
int wi_send_packet(struct wi_usb_softc *sc, int id);
void wi_usb_rxfrm(struct wi_usb_softc *usc, wi_usb_usbin *uin, int total_len);
void wi_usb_txfrm(struct wi_usb_softc *usc, wi_usb_usbin *uin, int total_len);
void wi_usb_start_thread(void *);

int wi_usb_tx_lock_try(struct wi_usb_softc *sc);
void wi_usb_tx_lock(struct wi_usb_softc *usc);
void wi_usb_tx_unlock(struct wi_usb_softc *usc);
void wi_usb_ctl_lock(struct wi_usb_softc *usc);
void wi_usb_ctl_unlock(struct wi_usb_softc *usc);

void wi_dump_data(void *buffer, int len);

void wi_usb_thread(void *arg);

#ifdef WI_USB_DEBUG
#define DPRINTF(x)      do { if (wi_usbdebug) printf x; } while (0)
#define DPRINTFN(n,x)   do { if (wi_usbdebug >= (n)) printf x; } while (0)
int     wi_usbdebug = 1;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

struct wi_usb_thread_info {
	int status;
	int dying;
	int idle;
};

/* thread status flags */
#define WI_START	0x01
#define WI_DYING	0x02
#define WI_INQUIRE	0x04
#define WI_WATCHDOG	0x08


struct wi_usb_softc {
	struct wi_softc		sc_wi;
#define wi_usb_dev sc_wi.sc_dev

	struct timeout		wi_usb_stat_ch;

	struct usbd_device	*wi_usb_udev;
	struct usbd_interface	*wi_usb_iface;
	u_int16_t		wi_usb_vendor;
	u_int16_t		wi_usb_product;
	int			wi_usb_ed[WI_USB_ENDPT_MAX];
	struct usbd_pipe	*wi_usb_ep[WI_USB_ENDPT_MAX];

	struct wi_usb_chain	wi_usb_tx_chain[WI_USB_TX_LIST_CNT];
	struct wi_usb_chain	wi_usb_rx_chain[WI_USB_RX_LIST_CNT];

	int			wi_usb_refcnt;
	char			wi_usb_attached;
	int			wi_usb_intr_errs;
	struct timeval		wi_usb_rx_notice;

	int			wi_usb_pollpending;

	wi_usb_usbin		wi_usb_ibuf;
	int			wi_usb_tx_prod;
	int			wi_usb_tx_cons;
	int			wi_usb_tx_cnt;
	int			wi_usb_rx_prod;

	struct wi_ltv_gen	*ridltv;
	int			ridresperr;

	int			cmdresp;
	int			cmdresperr;
	int			txresp;
	int			txresperr;

	/* nummem (tx/mgmt) */
	int			wi_usb_nummem;
#define MAX_WI_NMEM 3
	void			*wi_usb_txmem[MAX_WI_NMEM];
	int			wi_usb_txmemsize[MAX_WI_NMEM];
	void			*wi_usb_rxmem;
	int			wi_usb_rxmemsize;

	void			*wi_info;
	void			*wi_rxframe;

	/* prevent multiple outstanding USB requests */
	int			wi_lock;
	int			wi_lockwait;

	/* prevent multiple command requests */
	int			wi_ctllock;
	int			wi_ctllockwait;
	struct proc		*wi_curproc;

	/* kthread */
	struct wi_usb_thread_info	*wi_thread_info;
	int			wi_resetonce;
};

struct wi_funcs wi_func_usb = {
        wi_cmd_usb,
        wi_read_record_usb,
        wi_write_record_usb,
        wi_alloc_nicmem_usb,
        wi_read_data_usb,
        wi_write_data_usb,
        wi_get_fid_usb,
        wi_init_usb,

        wi_start_usb,
        wi_ioctl_usb,
        wi_watchdog_usb,
        wi_inquire_usb,
};

/*
 * Various supported device vendors/products.
 */
const struct wi_usb_type {
	struct usb_devno	wi_usb_device;
	u_int16_t	wi_usb_flags;
	/* XXX */
} wi_usb_devs[] = {
	{{ USB_VENDOR_ACCTON, USB_PRODUCT_ACCTON_111 }, 0 },
	{{ USB_VENDOR_ACERW, USB_PRODUCT_ACERW_WARPLINK }, 0 },
	{{ USB_VENDOR_ACTIONTEC, USB_PRODUCT_ACTIONTEC_FREELAN }, 0 },
	{{ USB_VENDOR_ACTIONTEC, USB_PRODUCT_ACTIONTEC_PRISM_25 }, 0 },
	{{ USB_VENDOR_ACTIONTEC, USB_PRODUCT_ACTIONTEC_PRISM_25A }, 0 },
	{{ USB_VENDOR_ADAPTEC, USB_PRODUCT_ADAPTEC_AWN8020 }, 0 },
	{{ USB_VENDOR_AMBIT, USB_PRODUCT_AMBIT_WLAN }, 0 },
	{{ USB_VENDOR_ASUSTEK, USB_PRODUCT_ASUSTEK_WL140 }, 0 },
	{{ USB_VENDOR_AVERATEC, USB_PRODUCT_AVERATEC_USBWLAN }, 0 },
	{{ USB_VENDOR_COMPAQ, USB_PRODUCT_COMPAQ_W100 }, 0 },
	{{ USB_VENDOR_COMPAQ, USB_PRODUCT_COMPAQ_W200 }, 0 },
	{{ USB_VENDOR_COREGA, USB_PRODUCT_COREGA_WLANUSB }, 0 },
	{{ USB_VENDOR_COREGA, USB_PRODUCT_COREGA_WLUSB_11_KEY }, 0 },
	{{ USB_VENDOR_DELL, USB_PRODUCT_DELL_TM1180 }, 0 },
	{{ USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DWL120F }, 0 },
	{{ USB_VENDOR_DLINK, USB_PRODUCT_DLINK_DWL122 }, 0 },
	{{ USB_VENDOR_INTEL, USB_PRODUCT_INTEL_I2011B }, 0 },
	{{ USB_VENDOR_INTERSIL, USB_PRODUCT_INTERSIL_PRISM_2X }, 0 },
	{{ USB_VENDOR_IODATA, USB_PRODUCT_IODATA_USBWNB11 }, 0 },
	{{ USB_VENDOR_JVC, USB_PRODUCT_JVC_MP_XP7250_WL }, 0 },
	{{ USB_VENDOR_LINKSYS, USB_PRODUCT_LINKSYS_WUSB11_25 }, 0 },
	{{ USB_VENDOR_LINKSYS, USB_PRODUCT_LINKSYS_WUSB12_11 }, 0 },
	{{ USB_VENDOR_LINKSYS3, USB_PRODUCT_LINKSYS3_WUSB11V30 }, 0 },
	{{ USB_VENDOR_MELCO, USB_PRODUCT_MELCO_KB11 }, 0 },
	{{ USB_VENDOR_MELCO, USB_PRODUCT_MELCO_KS11G }, 0 },
	{{ USB_VENDOR_MELCO, USB_PRODUCT_MELCO_S11 }, 0 },
	{{ USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_MN510 }, 0 },
	{{ USB_VENDOR_NETGEAR, USB_PRODUCT_NETGEAR_MA111NA }, 0 },
	{{ USB_VENDOR_PHEENET, USB_PRODUCT_PHEENET_WL503IA }, 0 },
	{{ USB_VENDOR_PHEENET, USB_PRODUCT_PHEENET_WM168B }, 0 },
	{{ USB_VENDOR_PLANEX, USB_PRODUCT_PLANEX_GW_US11H }, 0 },
	{{ USB_VENDOR_SIEMENS, USB_PRODUCT_SIEMENS_SPEEDSTREAM22 }, 0 },
	{{ USB_VENDOR_SITECOM2, USB_PRODUCT_SITECOM2_WL022 }, 0 },
	{{ USB_VENDOR_TEKRAM, USB_PRODUCT_TEKRAM_0193 }, 0 },
	{{ USB_VENDOR_TEKRAM, USB_PRODUCT_TEKRAM_ZYAIR_B200 }, 0 },
	{{ USB_VENDOR_USR, USB_PRODUCT_USR_USR1120 }, 0 },
	{{ USB_VENDOR_VIEWSONIC, USB_PRODUCT_VIEWSONIC_AIRSYNC }, 0 },
	{{ USB_VENDOR_ZCOM, USB_PRODUCT_ZCOM_XI725 }, 0 },
	{{ USB_VENDOR_ZCOM, USB_PRODUCT_ZCOM_XI735 }, 0 }
};
#define wi_usb_lookup(v, p) ((struct wi_usb_type *)usb_lookup(wi_usb_devs, v, p))

int wi_usb_match(struct device *, void *, void *);
void wi_usb_attach(struct device *, struct device *, void *);
int wi_usb_detach(struct device *, int);

const struct cfattach wi_usb_ca = {
	sizeof(struct wi_usb_softc), wi_usb_match, wi_usb_attach, wi_usb_detach
};

int
wi_usb_match(struct device *parent, void *match, void *aux)
{
	struct usb_attach_arg	*uaa = aux;

	if (uaa->iface == NULL || uaa->configno != 1)
		return (UMATCH_NONE);

	return (wi_usb_lookup(uaa->vendor, uaa->product) != NULL ?
		UMATCH_VENDOR_PRODUCT_CONF_IFACE : UMATCH_NONE);
}


/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
wi_usb_attach(struct device *parent, struct device *self, void *aux)
{
	struct wi_usb_softc	*sc = (struct wi_usb_softc *)self;
	struct usb_attach_arg	*uaa = aux;
/*	int			s; */
	struct usbd_device	*dev = uaa->device;
	struct usbd_interface	*iface = uaa->iface;
	usb_interface_descriptor_t	*id;
	usb_endpoint_descriptor_t	*ed;
	int			 i;

	DPRINTFN(5,(" : wi_usb_attach: sc=%p", sc));

	/* XXX - any tasks? */

	/* XXX - flags? */

	sc->wi_usb_udev = dev;
	sc->wi_usb_iface = iface;
	sc->wi_usb_product = uaa->product;
	sc->wi_usb_vendor = uaa->vendor;

	sc->sc_wi.wi_usb_cdata = sc;
	sc->sc_wi.wi_flags |= WI_FLAGS_BUS_USB;

	sc->wi_lock = 0;
	sc->wi_lockwait = 0;
	sc->wi_resetonce = 0;

	id = usbd_get_interface_descriptor(iface);

	/* Find endpoints. */
	for (i = 0; i < id->bNumEndpoints; i++) {
		ed = usbd_interface2endpoint_descriptor(iface, i);
		if (ed == NULL) {
			printf("%s: couldn't get endpoint descriptor %d\n",
			    sc->wi_usb_dev.dv_xname, i);
			return;
		}
		if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
		    UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->wi_usb_ed[WI_USB_ENDPT_RX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
			sc->wi_usb_ed[WI_USB_ENDPT_TX] = ed->bEndpointAddress;
		} else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
			   UE_GET_XFERTYPE(ed->bmAttributes) == UE_INTERRUPT) {
			sc->wi_usb_ed[WI_USB_ENDPT_INTR] = ed->bEndpointAddress;
		}
	}

	sc->wi_usb_nummem = 0;

	/* attach wi device */

	if (wi_usb_rx_list_init(sc)) {
		printf("%s: rx list init failed\n",
		    sc->wi_usb_dev.dv_xname);
		return;
	}
	if (wi_usb_tx_list_init(sc)) {
		printf("%s: tx list init failed\n",
		    sc->wi_usb_dev.dv_xname);
		return;
	}

	if (wi_usb_open_pipes(sc)){
		printf("%s: open pipes failed\n",
		    sc->wi_usb_dev.dv_xname);
		return;
	}

	sc->wi_usb_attached = 1;

	kthread_create_deferred(wi_usb_start_thread, sc);
}

int
wi_usb_detach(struct device *self, int flags)
{
	struct wi_usb_softc	*sc = (struct wi_usb_softc *)self;
	struct ifnet		*ifp = WI_GET_IFP(sc);
	struct wi_softc		*wsc = &sc->sc_wi;
	int s;
	int err;

	/* Detached before attach finished, so just bail out. */
	if (!sc->wi_usb_attached)
		return (0);

	if (sc->wi_thread_info != NULL) {
		sc->wi_thread_info->dying = 1;

		sc->wi_thread_info->status |= WI_DYING;
		if (sc->wi_thread_info->idle)
			wakeup(sc->wi_thread_info);
	}

	/* tasks? */

	s = splusb();
	/* detach wi */

	if (!(wsc->wi_flags & WI_FLAGS_ATTACHED)) {
		printf("%s: already detached\n", sc->wi_usb_dev.dv_xname);
		splx(s);
		return (0);
	}

	wi_detach(&sc->sc_wi);

	wsc->wi_flags = 0;

	if (ifp->if_softc != NULL) {
		ether_ifdetach(ifp);
		if_detach(ifp);
	}

	sc->wi_usb_attached = 0;

	if (--sc->wi_usb_refcnt >= 0) {
		/* Wait for processes to go away. */
		usb_detach_wait(&sc->wi_usb_dev);
	}

	while (sc->wi_usb_nummem) {
		sc->wi_usb_nummem--;
		free(sc->wi_usb_txmem[sc->wi_usb_nummem], M_USBDEV,
		  sc->wi_usb_txmemsize[sc->wi_usb_nummem]);
		sc->wi_usb_txmem[sc->wi_usb_nummem] = NULL;
		sc->wi_usb_txmemsize[sc->wi_usb_nummem] = 0;
	}

	if (sc->wi_usb_ep[WI_USB_ENDPT_INTR] != NULL) {
		err = usbd_close_pipe(sc->wi_usb_ep[WI_USB_ENDPT_INTR]);
		if (err) {
			printf("%s: close intr pipe failed: %s\n",
			    sc->wi_usb_dev.dv_xname, usbd_errstr(err));
		}
		sc->wi_usb_ep[WI_USB_ENDPT_INTR] = NULL;
	}
	if (sc->wi_usb_ep[WI_USB_ENDPT_TX] != NULL) {
		err = usbd_close_pipe(sc->wi_usb_ep[WI_USB_ENDPT_TX]);
		if (err) {
			printf("%s: close tx pipe failed: %s\n",
			    sc->wi_usb_dev.dv_xname, usbd_errstr(err));
		}
		sc->wi_usb_ep[WI_USB_ENDPT_TX] = NULL;
	}
	if (sc->wi_usb_ep[WI_USB_ENDPT_RX] != NULL) {
		err = usbd_close_pipe(sc->wi_usb_ep[WI_USB_ENDPT_RX]);
		if (err) {
			printf("%s: close rx pipe failed: %s\n",
			    sc->wi_usb_dev.dv_xname, usbd_errstr(err));
		}
		sc->wi_usb_ep[WI_USB_ENDPT_RX] = NULL;
	}

	splx(s);

	return (0);
}

int
wi_send_packet(struct wi_usb_softc *sc, int id)
{
	struct wi_usb_chain	*c;
	struct wi_frame		*wibuf;
	int			total_len, rnd_len;
	int			err;

	c = &sc->wi_usb_tx_chain[0];

	DPRINTFN(10,("%s: %s: id=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, id));

	/* assemble packet from write_data buffer */
	if (id == 0 || id == 1) {
		/* tx_lock acquired before wi_start() */
		wibuf = sc->wi_usb_txmem[id];

		total_len = sizeof (struct wi_frame) +
		    letoh16(wibuf->wi_dat_len);
		rnd_len = ROUNDUP64(total_len);
		if ((total_len > sc->wi_usb_txmemsize[id]) ||
		   (rnd_len > WI_USB_BUFSZ )){
			printf("invalid packet len: %x memsz %x max %x\n",
			    total_len, sc->wi_usb_txmemsize[id], WI_USB_BUFSZ);

			err = EIO;
			goto err_ret;
		}

		sc->txresp = WI_CMD_TX;
		sc->txresperr = 0;

		bcopy(wibuf, c->wi_usb_buf, total_len);

		bzero(((char *)c->wi_usb_buf)+total_len,
		    rnd_len - total_len);

		/* zero old packet for next TX */
		bzero(wibuf, total_len);

		total_len = rnd_len;

		DPRINTFN(5,("%s: %s: id=%x len=%x\n",
		    sc->wi_usb_dev.dv_xname, __func__, id, total_len));

		usbd_setup_xfer(c->wi_usb_xfer, sc->wi_usb_ep[WI_USB_ENDPT_TX],
		    c, c->wi_usb_buf, rnd_len,
		    USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
		    WI_USB_TX_TIMEOUT, wi_usb_txeof_frm);

		err = usbd_transfer(c->wi_usb_xfer);
		if (err != USBD_IN_PROGRESS && err != USBD_NORMAL_COMPLETION) {
			printf("%s: %s: error=%s\n",
			    sc->wi_usb_dev.dv_xname, __func__,
			    usbd_errstr(err));
			/* Stop the interface from process context. */
			wi_usb_stop(sc);
			err = EIO;
		} else {
			err = 0;
		}

		DPRINTFN(5,("%s: %s: exit err=%x\n",
		    sc->wi_usb_dev.dv_xname, __func__, err));
err_ret:
		return err;
	}
	printf("%s:%s: invalid packet id sent %x\n",
	    sc->wi_usb_dev.dv_xname, __func__, id);
	return 0;
}

int
wi_cmd_usb(struct wi_softc *wsc, int cmd, int val0, int val1, int val2)
{
	struct wi_usb_chain	*c;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;
	struct wi_cmdreq	*pcmd;
	int			total_len, rnd_len;
	int			err;

	DPRINTFN(5,("%s: %s: enter cmd=%x %x %x %x\n",
	    sc->wi_usb_dev.dv_xname, __func__, cmd, val0, val1, val2));

	if ((cmd & WI_CMD_CODE_MASK) == WI_CMD_TX) {
		return wi_send_packet(sc, val0);
	}


	if ((cmd & WI_CMD_CODE_MASK) == WI_CMD_INI) {
		/* free alloc_nicmem regions */
		while (sc->wi_usb_nummem) {
			sc->wi_usb_nummem--;
			free(sc->wi_usb_txmem[sc->wi_usb_nummem], M_USBDEV,
			  sc->wi_usb_txmemsize[sc->wi_usb_nummem]);
			sc->wi_usb_txmem[sc->wi_usb_nummem] = NULL;
			sc->wi_usb_txmemsize[sc->wi_usb_nummem] = 0;
		}

#if 0
		/* if this is the first time, init, otherwise do not?? */
		if (sc->wi_resetonce) {
			return 0;
		} else
			sc->wi_resetonce = 1;
#endif
	}

	wi_usb_ctl_lock(sc);

	wi_usb_tx_lock(sc);

	c = &sc->wi_usb_tx_chain[0];
	pcmd = c->wi_usb_buf;


	total_len = sizeof (struct wi_cmdreq);
	rnd_len = ROUNDUP64(total_len);
	if (rnd_len > WI_USB_BUFSZ) {
		printf("read_record buf size err %x %x\n", 
		    rnd_len, WI_USB_BUFSZ);
		err = EIO;
		goto err_ret;
	}

	sc->cmdresp = cmd;
	sc->cmdresperr = 0;

	pcmd->type = htole16(WI_USB_CMDREQ);
	pcmd->cmd  = htole16(cmd);
	pcmd->param0  = htole16(val0);
	pcmd->param1  = htole16(val1);
	pcmd->param2  = htole16(val2);

	bzero(((char*)pcmd)+total_len, rnd_len - total_len);

	usbd_setup_xfer(c->wi_usb_xfer, sc->wi_usb_ep[WI_USB_ENDPT_TX],
	    c, c->wi_usb_buf, rnd_len, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    WI_USB_TX_TIMEOUT, wi_usb_txeof);

	err = wi_usb_do_transmit_sync(sc, c, &sc->cmdresperr);

	if (err == 0)
		err = sc->cmdresperr;

	sc->cmdresperr = 0;

err_ret:
	wi_usb_tx_unlock(sc);

	wi_usb_ctl_unlock(sc);

	DPRINTFN(5,("%s: %s: exit err=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, err));
	return err;
}


int
wi_read_record_usb(struct wi_softc *wsc, struct wi_ltv_gen *ltv)
{
	struct wi_usb_chain	*c;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;
	struct wi_rridreq	*prid;
	int			total_len, rnd_len;
	int			err;
	struct wi_ltv_gen	*oltv = NULL, p2ltv;

	DPRINTFN(5,("%s: %s: enter rid=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, ltv->wi_type));

	/* Do we need to deal with these here, as in _io version?
	 * WI_RID_ENCRYPTION -> WI_RID_P2_ENCRYPTION
	 * WI_RID_TX_CRYPT_KEY -> WI_RID_P2_TX_CRYPT_KEY
	 */
	if (wsc->sc_firmware_type != WI_LUCENT) {
		oltv = ltv;
		switch (ltv->wi_type) {
		case WI_RID_ENCRYPTION:
			p2ltv.wi_type = WI_RID_P2_ENCRYPTION;
			p2ltv.wi_len = 2;
			ltv = &p2ltv;
			break;
		case WI_RID_TX_CRYPT_KEY:
			if (ltv->wi_val > WI_NLTV_KEYS)
				return (EINVAL);
			p2ltv.wi_type = WI_RID_P2_TX_CRYPT_KEY;
			p2ltv.wi_len = 2;
			ltv = &p2ltv;
			break;
		}
	}

	wi_usb_tx_lock(sc);

	c = &sc->wi_usb_tx_chain[0];
	prid = c->wi_usb_buf;

	total_len = sizeof(struct wi_rridreq);
	rnd_len = ROUNDUP64(total_len);

	if (rnd_len > WI_USB_BUFSZ) {
		printf("read_record buf size err %x %x\n", 
		    rnd_len, WI_USB_BUFSZ);
		wi_usb_tx_unlock(sc);
		return EIO;
	}

	sc->ridltv = ltv;
	sc->ridresperr = 0;

	prid->type = htole16(WI_USB_RRIDREQ);
	prid->frmlen = htole16(2);	/* variable size? */
	prid->rid  = htole16(ltv->wi_type);

	bzero(((char*)prid)+total_len, rnd_len - total_len);

	usbd_setup_xfer(c->wi_usb_xfer, sc->wi_usb_ep[WI_USB_ENDPT_TX],
	    c, c->wi_usb_buf, rnd_len, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    WI_USB_TX_TIMEOUT, wi_usb_txeof);

	DPRINTFN(10,("%s: %s: total_len=%x, wilen %d\n",
	    sc->wi_usb_dev.dv_xname, __func__, total_len, ltv->wi_len));

	err = wi_usb_do_transmit_sync(sc, c, &sc->ridresperr);

	/* Do we need to deal with these here, as in _io version?
	 *
	 * WI_RID_TX_RATE
	 * WI_RID_CUR_TX_RATE
	 * WI_RID_ENCRYPTION
	 * WI_RID_TX_CRYPT_KEY
	 * WI_RID_CNFAUTHMODE
	 */
	if (ltv->wi_type == WI_RID_PORTTYPE && wsc->wi_ptype == WI_PORTTYPE_IBSS
	    && ltv->wi_val == wsc->wi_ibss_port) {
		/*
		 * Convert vendor IBSS port type to WI_PORTTYPE_IBSS.
		 * Since Lucent uses port type 1 for BSS *and* IBSS we
		 * have to rely on wi_ptype to distinguish this for us.
		 */
		ltv->wi_val = htole16(WI_PORTTYPE_IBSS);
	} else if (wsc->sc_firmware_type != WI_LUCENT) {
		int v;

		switch (oltv->wi_type) {
		case WI_RID_TX_RATE:
		case WI_RID_CUR_TX_RATE:
			switch (letoh16(ltv->wi_val)) {
			case 1: v = 1; break;
			case 2: v = 2; break;
			case 3:	v = 6; break;
			case 4: v = 5; break;
			case 7: v = 7; break;
			case 8: v = 11; break;
			case 15: v = 3; break;
			default: v = 0x100 + letoh16(ltv->wi_val); break;
			}
			oltv->wi_val = htole16(v);
			break;
		case WI_RID_ENCRYPTION:
			oltv->wi_len = 2;
			if (ltv->wi_val & htole16(0x01))
				oltv->wi_val = htole16(1);
			else
				oltv->wi_val = htole16(0);
			break;
		case WI_RID_TX_CRYPT_KEY:
		case WI_RID_CNFAUTHMODE:
			oltv->wi_len = 2;
			oltv->wi_val = ltv->wi_val;
			break;
		}
	}

	if (err == 0)
		err = sc->ridresperr;

	sc->ridresperr = 0;

	wi_usb_tx_unlock(sc);

	DPRINTFN(5,("%s: %s: exit err=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, err));
	return err;
}

int
wi_write_record_usb(struct wi_softc *wsc, struct wi_ltv_gen *ltv)
{
	struct wi_usb_chain	*c;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;
	struct wi_wridreq	*prid;
	int			total_len, rnd_len;
	int			err;
	struct wi_ltv_gen	p2ltv;
	u_int16_t		val = 0;
	int			i;

	DPRINTFN(5,("%s: %s: enter rid=%x wi_len %d copying %x\n",
	    sc->wi_usb_dev.dv_xname, __func__, ltv->wi_type, ltv->wi_len,
	    (ltv->wi_len-1)*2 ));

	/* Do we need to deal with these here, as in _io version?
	 * WI_PORTTYPE_IBSS -> WI_RID_PORTTYPE
	 * RID_TX_RATE munging
	 * RID_ENCRYPTION
	 * WI_RID_TX_CRYPT_KEY
	 * WI_RID_DEFLT_CRYPT_KEYS
	 */
	if (ltv->wi_type == WI_RID_PORTTYPE &&
	    letoh16(ltv->wi_val) == WI_PORTTYPE_IBSS) {
		/* Convert WI_PORTTYPE_IBSS to vendor IBSS port type. */
		p2ltv.wi_type = WI_RID_PORTTYPE;
		p2ltv.wi_len = 2;
		p2ltv.wi_val = wsc->wi_ibss_port;
		ltv = &p2ltv;
	} else if (wsc->sc_firmware_type != WI_LUCENT) {
		int v;

		switch (ltv->wi_type) {
		case WI_RID_TX_RATE:
			p2ltv.wi_type = WI_RID_TX_RATE;
			p2ltv.wi_len = 2;
			switch (letoh16(ltv->wi_val)) {
			case 1: v = 1; break;
			case 2: v = 2; break;
			case 3:	v = 15; break;
			case 5: v = 4; break;
			case 6: v = 3; break;
			case 7: v = 7; break;
			case 11: v = 8; break;
			default: return EINVAL;
			}
			p2ltv.wi_val = htole16(v);
			ltv = &p2ltv;
			break;
		case WI_RID_ENCRYPTION:
			p2ltv.wi_type = WI_RID_P2_ENCRYPTION;
			p2ltv.wi_len = 2;
			if (ltv->wi_val & htole16(0x01)) {
				val = PRIVACY_INVOKED;
				/*
				 * If using shared key WEP we must set the
				 * EXCLUDE_UNENCRYPTED bit.  Symbol cards
				 * need this bit set even when not using
				 * shared key. We can't just test for
				 * IEEE80211_AUTH_SHARED since Symbol cards
				 * have 2 shared key modes.
				 */
				if (wsc->wi_authtype != IEEE80211_AUTH_OPEN ||
				    wsc->sc_firmware_type == WI_SYMBOL)
					val |= EXCLUDE_UNENCRYPTED;

				switch (wsc->wi_crypto_algorithm) {
				case WI_CRYPTO_FIRMWARE_WEP:
					/*
					 * TX encryption is broken in
					 * Host AP mode.
					 */
					if (wsc->wi_ptype == WI_PORTTYPE_HOSTAP)
						val |= HOST_ENCRYPT;
					break;
				case WI_CRYPTO_SOFTWARE_WEP:
					val |= HOST_ENCRYPT|HOST_DECRYPT;
					break;
				}
				p2ltv.wi_val = htole16(val);
			} else
				p2ltv.wi_val = htole16(HOST_ENCRYPT | HOST_DECRYPT);
			ltv = &p2ltv;
			break;
		case WI_RID_TX_CRYPT_KEY:
			if (ltv->wi_val > WI_NLTV_KEYS)
				return (EINVAL);
			p2ltv.wi_type = WI_RID_P2_TX_CRYPT_KEY;
			p2ltv.wi_len = 2;
			p2ltv.wi_val = ltv->wi_val;
			ltv = &p2ltv;
			break;
		case WI_RID_DEFLT_CRYPT_KEYS: {
				int error;
				int keylen;
				struct wi_ltv_str ws;
				struct wi_ltv_keys *wk;

				wk = (struct wi_ltv_keys *)ltv;
				keylen = wk->wi_keys[wsc->wi_tx_key].wi_keylen;
				keylen = letoh16(keylen);

				for (i = 0; i < 4; i++) {
					bzero(&ws, sizeof(ws));
					ws.wi_len = (keylen > 5) ? 8 : 4;
					ws.wi_type = WI_RID_P2_CRYPT_KEY0 + i;
					bcopy(&wk->wi_keys[i].wi_keydat,
					    ws.wi_str, keylen);
					error = wi_write_record_usb(wsc,
					    (struct wi_ltv_gen *)&ws);
					if (error)
						return (error);
				}
			}
			return (0);
		}
	}

	wi_usb_tx_lock(sc);

	c = &sc->wi_usb_tx_chain[0];

	prid = c->wi_usb_buf;

	total_len = sizeof(prid->type) + sizeof(prid->frmlen) +
	    sizeof(prid->rid) + (ltv->wi_len-1)*2;
	rnd_len = ROUNDUP64(total_len);
	if (rnd_len > WI_USB_BUFSZ) {
		printf("write_record buf size err %x %x\n", 
		    rnd_len, WI_USB_BUFSZ);
		wi_usb_tx_unlock(sc);
		return EIO;
	}

	prid->type = htole16(WI_USB_WRIDREQ);
	prid->frmlen = htole16(ltv->wi_len);
	prid->rid  = htole16(ltv->wi_type);
	if (ltv->wi_len > 1)
		bcopy(&ltv->wi_val, &prid->data[0], (ltv->wi_len-1)*2);

	bzero(((char*)prid)+total_len, rnd_len - total_len);

	usbd_setup_xfer(c->wi_usb_xfer, sc->wi_usb_ep[WI_USB_ENDPT_TX],
	    c, c->wi_usb_buf, rnd_len, USBD_FORCE_SHORT_XFER | USBD_NO_COPY,
	    WI_USB_TX_TIMEOUT, wi_usb_txeof);

	err = wi_usb_do_transmit_sync(sc, c, &sc->ridresperr);

	if (err == 0)
		err = sc->ridresperr;

	sc->ridresperr = 0;

	wi_usb_tx_unlock(sc);

	DPRINTFN(5,("%s: %s: exit err=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, err));
	return err;
}

/*
 * This is an ugly compat portion to emulate the I/O which writes
 * a packet or management information
 * The data is copied into local memory for the requested
 * 'id' then on the wi_cmd WI_CMD_TX, the id argument
 * will identify which buffer to use
 */
int
wi_alloc_nicmem_usb(struct wi_softc *wsc, int len, int *id)
{
	int nmem;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;

	DPRINTFN(10,("%s: %s: enter len=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, len));

	/*
	 * NOTE THIS IS A USB DEVICE WHICH WILL LIKELY HAVE MANY
	 * CONNECTS/DISCONNECTS, FREE THIS MEMORY XXX XXX XXX !!! !!!
	 */
	nmem = sc->wi_usb_nummem++;

	if (nmem >= MAX_WI_NMEM) {
		sc->wi_usb_nummem--;
		return ENOMEM;
	}

	sc->wi_usb_txmem[nmem] = malloc(len, M_USBDEV, M_WAITOK | M_CANFAIL);
	if (sc->wi_usb_txmem[nmem] == NULL) {
		sc->wi_usb_nummem--;
		return ENOMEM;
	}
	sc->wi_usb_txmemsize[nmem] = len;

	*id = nmem;
	return 0;
}

/*
 * this is crazy, we skip the first 16 bits of the buf so that it
 * can be used as the 'type' of the usb transfer.
 */


int
wi_write_data_usb(struct wi_softc *wsc, int id, int off, caddr_t buf, int len)
{
	u_int8_t	*ptr;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;

	DPRINTFN(10,("%s: %s: id %x off %x len %d\n",
	    sc->wi_usb_dev.dv_xname, __func__, id, off, len));

	if (id < 0 && id >= sc->wi_usb_nummem)
		return EIO;

	ptr = (u_int8_t *)(sc->wi_usb_txmem[id]) + off;

	if (len + off > sc->wi_usb_txmemsize[id])
		return EIO;
	DPRINTFN(10,("%s: %s: completed \n",
	    sc->wi_usb_dev.dv_xname, __func__));

	bcopy(buf, ptr, len);
	return 0;
}

/*
 * On the prism I/O, this read_data points to the hardware buffer
 * which contains the
 */
int
wi_read_data_usb(struct wi_softc *wsc, int id, int off, caddr_t buf, int len)
{
	u_int8_t	*ptr;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;

	DPRINTFN(10,("%s: %s: id %x off %x len %d\n",
	    sc->wi_usb_dev.dv_xname, __func__, id, off, len));

	if (id == 0x1001 && sc->wi_info != NULL)
		ptr = (u_int8_t *)sc->wi_info + off;
	else if (id == 0x1000 && sc->wi_rxframe != NULL)
		ptr = (u_int8_t *)sc->wi_rxframe + off;
	else if (id >= 0 && id < sc->wi_usb_nummem) {

		if (sc->wi_usb_txmem[id] == NULL)
			return EIO;
		if (len + off > sc->wi_usb_txmemsize[id])
			return EIO;

		ptr = (u_int8_t *)(sc->wi_usb_txmem[id]) + off;
	} else
		return EIO;

	if (id < sc->wi_usb_nummem) {
		ptr = (u_int8_t *)(sc->wi_usb_txmem[id]) + off;

		if (len + off > sc->wi_usb_txmemsize[id])
			return EIO;
	}

	bcopy(ptr, buf, len);
	return 0;
}

void
wi_usb_stop(struct wi_usb_softc *sc)
{
	DPRINTFN(1,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname,__func__));
	/* XXX */

	/* Stop transfers */
}

int
wi_usb_do_transmit_sync(struct wi_usb_softc *sc, struct wi_usb_chain *c,
    void *ident)
{
	usbd_status		err;

	DPRINTFN(10,("%s: %s:\n",
	    sc->wi_usb_dev.dv_xname, __func__));

	sc->wi_usb_refcnt++;
	err = usbd_transfer(c->wi_usb_xfer);
	if (err != USBD_IN_PROGRESS && err != USBD_NORMAL_COMPLETION) {
		printf("%s: %s error=%s\n",
		    sc->wi_usb_dev.dv_xname, __func__,
		    usbd_errstr(err));
		/* Stop the interface from process context. */
		wi_usb_stop(sc);
		err = EIO;
		goto done;
	}
	err = tsleep_nsec(ident, PRIBIO, "wiTXsync", SEC_TO_NSEC(1));
	if (err) {
		DPRINTFN(1,("%s: %s: err %x\n",
		    sc->wi_usb_dev.dv_xname, __func__, err));
		err = ETIMEDOUT;
	}
done:
	if (--sc->wi_usb_refcnt < 0)
		usb_detach_wakeup(&sc->wi_usb_dev);
	return err;
}


/*
 * A command/rrid/wrid  was sent to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
wi_usb_txeof(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct wi_usb_chain	*c = priv;
	struct wi_usb_softc	*sc = c->wi_usb_sc;

	int			s;

	if (usbd_is_dying(sc->wi_usb_udev))
		return;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->wi_usb_dev.dv_xname,
		    __func__, status));

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		printf("%s: usb error on tx: %s\n", sc->wi_usb_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED) {
			sc->wi_usb_refcnt++;
			usbd_clear_endpoint_stall_async(
			    sc->wi_usb_ep[WI_USB_ENDPT_TX]);
			if (--sc->wi_usb_refcnt < 0)
				usb_detach_wakeup(&sc->wi_usb_dev);
		}
		splx(s);
		return;
	}

	splx(s);
}

/*
 * A packet was sent to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
wi_usb_txeof_frm(struct usbd_xfer *xfer, void *priv,
    usbd_status status)
{
	struct wi_usb_chain	*c = priv;
	struct wi_usb_softc	*sc = c->wi_usb_sc;
	struct wi_softc		*wsc = &sc->sc_wi;
	struct ifnet		*ifp = &wsc->sc_ic.ic_if;

	int			s;
	int			err = 0;

	if (usbd_is_dying(sc->wi_usb_udev))
		return;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->wi_usb_dev.dv_xname,
		    __func__, status));

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED) {
			splx(s);
			return;
		}
		printf("%s: usb error on tx: %s\n", sc->wi_usb_dev.dv_xname,
		    usbd_errstr(status));
		if (status == USBD_STALLED) {
			sc->wi_usb_refcnt++;
			usbd_clear_endpoint_stall_async(
			    sc->wi_usb_ep[WI_USB_ENDPT_TX]);
			if (--sc->wi_usb_refcnt < 0)
				usb_detach_wakeup(&sc->wi_usb_dev);
		}
		splx(s);
		return;
	}

	if (status)
		err = WI_EV_TX_EXC;

	wi_txeof(wsc, err);

	wi_usb_tx_unlock(sc);

	if (!ifq_empty(&ifp->if_snd))
		wi_start_usb(ifp);

	splx(s);
}

int
wi_usb_rx_list_init(struct wi_usb_softc *sc)
{
	struct wi_usb_chain	*c;
	int			i;

	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	for (i = 0; i < WI_USB_RX_LIST_CNT; i++) {
		c = &sc->wi_usb_rx_chain[i];
		c->wi_usb_sc = sc;
		c->wi_usb_idx = i;
		if (c->wi_usb_xfer != NULL) {
			printf("UGH RX\n");
		}
		if (c->wi_usb_xfer == NULL) {
			c->wi_usb_xfer = usbd_alloc_xfer(sc->wi_usb_udev);
			if (c->wi_usb_xfer == NULL)
				return (ENOBUFS);
			c->wi_usb_buf = usbd_alloc_buffer(c->wi_usb_xfer,
			    WI_USB_BUFSZ);
			if (c->wi_usb_buf == NULL)
				return (ENOBUFS); /* XXX free xfer */
		}
	}

	return (0);
}

int
wi_usb_tx_list_init(struct wi_usb_softc *sc)
{
	struct wi_usb_chain	*c;
	int			i;

	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	for (i = 0; i < WI_USB_TX_LIST_CNT; i++) {
		c = &sc->wi_usb_tx_chain[i];
		c->wi_usb_sc = sc;
		c->wi_usb_idx = i;
		c->wi_usb_mbuf = NULL;
		if (c->wi_usb_xfer != NULL) {
			printf("UGH TX\n");
		}
		if (c->wi_usb_xfer == NULL) {
			c->wi_usb_xfer = usbd_alloc_xfer(sc->wi_usb_udev);
			if (c->wi_usb_xfer == NULL)
				return (ENOBUFS);
			c->wi_usb_buf = usbd_alloc_buffer(c->wi_usb_xfer,
			    WI_USB_BUFSZ);
			if (c->wi_usb_buf == NULL)
				return (ENOBUFS);
		}
	}

	return (0);
}

int
wi_usb_open_pipes(struct wi_usb_softc *sc)
{
	usbd_status		err;
	int			error = 0;
	struct wi_usb_chain	*c;
	int			i;

	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname,__func__));

	sc->wi_usb_refcnt++;

	/* Open RX and TX pipes. */
	err = usbd_open_pipe(sc->wi_usb_iface, sc->wi_usb_ed[WI_USB_ENDPT_RX],
	    USBD_EXCLUSIVE_USE, &sc->wi_usb_ep[WI_USB_ENDPT_RX]);
	if (err) {
		printf("%s: open rx pipe failed: %s\n",
		    sc->wi_usb_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}

	err = usbd_open_pipe(sc->wi_usb_iface, sc->wi_usb_ed[WI_USB_ENDPT_TX],
	    USBD_EXCLUSIVE_USE, &sc->wi_usb_ep[WI_USB_ENDPT_TX]);
	if (err) {
		printf("%s: open tx pipe failed: %s\n",
		    sc->wi_usb_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}

	/* is this used? */
	err = usbd_open_pipe_intr(sc->wi_usb_iface,
	    sc->wi_usb_ed[WI_USB_ENDPT_INTR], 0,
	    &sc->wi_usb_ep[WI_USB_ENDPT_INTR], sc, &sc->wi_usb_ibuf,
	    WI_USB_INTR_PKTLEN, wi_usb_intr, WI_USB_INTR_INTERVAL);
	if (err) {
		printf("%s: open intr pipe failed: %s\n",
		    sc->wi_usb_dev.dv_xname, usbd_errstr(err));
		error = EIO;
		goto done;
	}

	/* Start up the receive pipe. */
	for (i = 0; i < WI_USB_RX_LIST_CNT; i++) {
		c = &sc->wi_usb_rx_chain[i];
		usbd_setup_xfer(c->wi_usb_xfer, sc->wi_usb_ep[WI_USB_ENDPT_RX],
		    c, c->wi_usb_buf, WI_USB_BUFSZ,
		    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT,
		    wi_usb_rxeof);
		DPRINTFN(10,("%s: %s: start read\n", sc->wi_usb_dev.dv_xname,
			    __func__));
		usbd_transfer(c->wi_usb_xfer);
	}

done:
	if (--sc->wi_usb_refcnt < 0)
		usb_detach_wakeup(&sc->wi_usb_dev);

	return (error);
}

/*
 * This is a bit of a kludge, however wi_rxeof and wi_update_stats
 * call wi_get_fid to determine where the data associated with
 * the transaction is located, the returned id is then used to
 * wi_read_data the information out.
 *
 * This code returns which 'fid' should be used. The results are only valid
 * during a wi_usb_rxeof because the data is received packet is 'held'
 * an a variable for reading by wi_read_data_usb for that period.
 *
 * for magic numbers this uses  0x1000, 0x1001 for rx/info
 */

int
wi_get_fid_usb(struct wi_softc *sc, int fid)
{
	switch (fid) {
	case WI_RX_FID:
		return 0x1000;
	case WI_INFO_FID:
		return 0x1001;
	default:
		return 0x1111;
	}

}

#if 0
void
wi_dump_data(void *buffer, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if (((i) % 16) == 0)
			printf("\n %02x:", i);
		printf(" %02x",
		    ((uint8_t *)(buffer))[i]);

	}
	printf("\n");

}
#endif

/*
 * A frame has been received.
 */
void
wi_usb_rxeof(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct wi_usb_chain	*c = priv;
	struct wi_usb_softc	*sc = c->wi_usb_sc;
	wi_usb_usbin		*uin;
	int			total_len = 0;
	u_int16_t		rtype;

	if (usbd_is_dying(sc->wi_usb_udev))
		return;

	DPRINTFN(10,("%s: %s: enter status=%d\n", sc->wi_usb_dev.dv_xname,
		    __func__, status));


	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_IOERROR
		    || status == USBD_CANCELLED) {
			printf("%s: %u usb errors on rx: %s\n",
			    sc->wi_usb_dev.dv_xname, 1,
			    /* sc->wi_usb_rx_errs, */
			    usbd_errstr(status));
			return;
		}
#if 0
		sc->wi_usb_rx_errs++;
		if (usbd_ratecheck(&sc->wi_usb_rx_notice)) {
			printf("%s: %u usb errors on rx: %s\n",
			    sc->wi_usb_dev.dv_xname, sc->wi_usb_rx_errs,
			    usbd_errstr(status));
			sc->wi_usb_rx_errs = 0;
		}
#endif
		if (status == USBD_STALLED) {
			sc->wi_usb_refcnt++;
			usbd_clear_endpoint_stall_async(
			    sc->wi_usb_ep[WI_USB_ENDPT_RX]);
			if (--sc->wi_usb_refcnt < 0)
				usb_detach_wakeup(&sc->wi_usb_dev);
		}
		goto done;
	}

	usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

	if (total_len < 6) /* short XXX */
		goto done;

	uin = (wi_usb_usbin *)(c->wi_usb_buf);

	rtype = letoh16(uin->type);


#if 0
	wi_dump_data(c->wi_usb_buf, total_len);
#endif

	if (WI_USB_ISRXFRM(rtype)) {
		wi_usb_rxfrm(sc, uin, total_len);
		goto done;
	}
	if (WI_USB_ISTXFRM(rtype)) {
		DPRINTFN(2,("%s: %s: txfrm type %x\n",
		    sc->wi_usb_dev.dv_xname, __func__, rtype));
		wi_usb_txfrm(sc, uin, total_len);
		goto done;
	}

	switch (rtype) {
	case WI_USB_INFOFRM:
		/* info packet, INFO_FID hmm */
		DPRINTFN(10,("%s: %s: infofrm type %x\n",
		    sc->wi_usb_dev.dv_xname, __func__, rtype));
		wi_usb_infofrm(c, total_len);
		break;
	case WI_USB_CMDRESP:
		wi_usb_cmdresp(c);
		break;
	case WI_USB_WRIDRESP:
		wi_usb_wridresp(c);
		break;
	case WI_USB_RRIDRESP:
		wi_usb_rridresp(c);
		break;
	case WI_USB_WMEMRESP:
		/* Not currently used */
		DPRINTFN(2,("%s: %s: wmemresp type %x\n",
		    sc->wi_usb_dev.dv_xname, __func__, rtype));
		break;
	case WI_USB_RMEMRESP:
		/* Not currently used */
		DPRINTFN(2,("%s: %s: rmemresp type %x\n",
		    sc->wi_usb_dev.dv_xname, __func__, rtype));
		break;
	case WI_USB_BUFAVAIL:
		printf("wi_usb: received USB_BUFAVAIL packet\n"); /* XXX */
		break;
	case WI_USB_ERROR:
		printf("wi_usb: received USB_ERROR packet\n"); /* XXX */
		break;
#if 0
	default:
		printf("wi_usb: received Unknown packet 0x%x len %x\n",
		    rtype, total_len);
		wi_dump_data(c->wi_usb_buf, total_len);
#endif
	}

 done:
	/* Setup new transfer. */
	usbd_setup_xfer(c->wi_usb_xfer, sc->wi_usb_ep[WI_USB_ENDPT_RX],
	    c, c->wi_usb_buf, WI_USB_BUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
	    USBD_NO_TIMEOUT, wi_usb_rxeof);
	sc->wi_usb_refcnt++;
	usbd_transfer(c->wi_usb_xfer);
	if (--sc->wi_usb_refcnt < 0)
		usb_detach_wakeup(&sc->wi_usb_dev);

	DPRINTFN(10,("%s: %s: start rx\n", sc->wi_usb_dev.dv_xname,
		    __func__));
}

void
wi_usb_intr(struct usbd_xfer *xfer, void *priv, usbd_status status)
{
	struct wi_usb_softc	*sc = priv;

	DPRINTFN(2,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	if (usbd_is_dying(sc->wi_usb_udev))
		return;

	if (status != USBD_NORMAL_COMPLETION) {
		if (status == USBD_NOT_STARTED || status == USBD_CANCELLED)
			return;

		if (status == USBD_STALLED) {
			sc->wi_usb_refcnt++;
			usbd_clear_endpoint_stall_async(
			    sc->wi_usb_ep[WI_USB_ENDPT_RX]);
			if (--sc->wi_usb_refcnt < 0)
				usb_detach_wakeup(&sc->wi_usb_dev);
		}
		return;
	}
	/* XXX oerrors or collisions? */
}
void
wi_usb_cmdresp(struct wi_usb_chain *c)
{
	struct wi_cmdresp *presp = (struct wi_cmdresp *)(c->wi_usb_buf);
	u_int16_t status = letoh16(presp->status);
	struct wi_usb_softc	*sc = c->wi_usb_sc;
	uint16_t type;
	uint16_t cmdresperr;

	type = htole16(presp->type);
	cmdresperr = letoh16(presp->resp0);
	DPRINTFN(10,("%s: %s: enter type=%x, status=%x, cmdresp=%x, "
	    "resp=%x,%x,%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, type, status, sc->cmdresp,
	    cmdresperr, letoh16(presp->resp1),
	    letoh16(presp->resp2)));

	/* XXX */
	if (sc->cmdresp != (status & WI_STAT_CMD_CODE)) {
		DPRINTFN(1,("%s: cmd ty %x st %x cmd %x failed %x\n",
		    sc->wi_usb_dev.dv_xname,
			type, status, sc->cmdresp, cmdresperr));
		return;
	}

	sc->cmdresperr = (status & WI_STAT_CMD_RESULT) >> 8;

	sc->cmdresp = 0; /* good value for idle == INI ?? XXX  */

	wakeup(&sc->cmdresperr);
}
void
wi_usb_rridresp(struct wi_usb_chain *c)
{
	struct wi_rridresp *presp = (struct wi_rridresp *)(c->wi_usb_buf);
	u_int16_t frmlen = letoh16(presp->frmlen);
	struct wi_usb_softc	*sc = c->wi_usb_sc;
	struct wi_ltv_gen *ltv;
	uint16_t rid;

	rid = letoh16(presp->rid);
	ltv =  sc->ridltv;

	if (ltv == 0) {
		DPRINTFN(5,("%s: %s: enter ltv = 0 rid=%x len %d\n",
		    sc->wi_usb_dev.dv_xname, __func__, rid,
		    frmlen));
		return;
	}

	DPRINTFN(5,("%s: %s: enter rid=%x expecting %x len %d exptlen %d\n",
	    sc->wi_usb_dev.dv_xname, __func__, rid, ltv->wi_type,
	    frmlen, ltv->wi_len));

	rid = letoh16(presp->rid);

	if (rid != ltv->wi_type) {
		sc->ridresperr = EIO;
		return;
	}

	if (frmlen > ltv->wi_len) {
		sc->ridresperr = ENOSPC;
		sc->ridltv = 0;
		wakeup(&sc->ridresperr);
		return;
	}

	ltv->wi_len = frmlen;

	DPRINTFN(10,("%s: %s: copying %d frmlen %d\n",
	    sc->wi_usb_dev.dv_xname, __func__, (ltv->wi_len-1)*2,
	    frmlen));

	if (ltv->wi_len > 1)
		bcopy(&presp->data[0], &ltv->wi_val,
		    (ltv->wi_len-1)*2);

	sc->ridresperr = 0;
	sc->ridltv = 0;
	wakeup(&sc->ridresperr);

}

void
wi_usb_wridresp(struct wi_usb_chain *c)
{
	struct wi_wridresp *presp = (struct wi_wridresp *)(c->wi_usb_buf);
	struct wi_usb_softc	*sc = c->wi_usb_sc;
	uint16_t status;

	status = letoh16(presp->status);

	DPRINTFN(10,("%s: %s: enter status=%x\n",
	    sc->wi_usb_dev.dv_xname, __func__, status));

	sc->ridresperr = (status & WI_STAT_CMD_RESULT) >> 8;
	sc->ridltv = 0;
	wakeup(&sc->ridresperr);
}

void
wi_usb_infofrm(struct wi_usb_chain *c, int len)
{
	struct wi_usb_softc	*sc = c->wi_usb_sc;

	DPRINTFN(10,("%s: %s: enter\n",
	    sc->wi_usb_dev.dv_xname, __func__));

	sc->wi_info = ((char *)c->wi_usb_buf) + 2;
	wi_update_stats(&sc->sc_wi);
	sc->wi_info = NULL;
}

void
wi_usb_txfrm(struct wi_usb_softc *sc, wi_usb_usbin *uin, int total_len)
{
	u_int16_t		status;
	int 			s;
	struct wi_softc		*wsc = &sc->sc_wi;
	struct ifnet		*ifp = &wsc->sc_ic.ic_if;

	s = splnet();
	status = letoh16(uin->type); /* XXX -- type == status */


	DPRINTFN(2,("%s: %s: enter status=%d\n",
	    sc->wi_usb_dev.dv_xname, __func__, status));

	if (sc->txresp == WI_CMD_TX) {
		sc->txresperr=status;
		sc->txresp = 0;
		wakeup(&sc->txresperr);
	} else {
		if (status != 0) /* XXX */
			wi_watchdog_usb(ifp);
	DPRINTFN(1,("%s: %s: txresp not expected status=%d \n",
	    sc->wi_usb_dev.dv_xname, __func__, status));
	}

	splx(s);
}
void
wi_usb_rxfrm(struct wi_usb_softc *sc, wi_usb_usbin *uin, int total_len)
{
	int s;

	DPRINTFN(5,("%s: %s: enter len=%d\n",
	    sc->wi_usb_dev.dv_xname, __func__, total_len));

	s = splnet();

	sc->wi_rxframe = (void *)uin;

	wi_rxeof(&sc->sc_wi);

	sc->wi_rxframe = NULL;

	splx(s);

}


void
wi_usb_start_thread(void *arg)
{
	struct wi_usb_softc	*sc = arg;
	kthread_create(wi_usb_thread, arg, NULL, sc->wi_usb_dev.dv_xname);
}

void
wi_start_usb(struct ifnet *ifp)
{
	struct wi_softc		*wsc;
	struct wi_usb_softc	*sc;
	int s;

	wsc = ifp->if_softc;
	sc  = wsc->wi_usb_cdata;

	s = splnet();

	DPRINTFN(5,("%s: %s:\n",
	    sc->wi_usb_dev.dv_xname, __func__));

	if (wi_usb_tx_lock_try(sc)) {
		/* lock acquired do start now */
		wi_func_io.f_start(ifp);
	} else {
		sc->wi_thread_info->status |= WI_START;
		if (sc->wi_thread_info->idle)
			wakeup(sc->wi_thread_info);
	}

	splx(s);
}

/* 
 * inquire is called from interrupt context (timeout)
 * It is not possible to sleep in interrupt context so it is necessary
 * to signal the kernel thread to perform the action.
 */
void
wi_init_usb(struct wi_softc *wsc)
{
	DPRINTFN(5,("%s: %s:\n", WI_PRT_ARG(wsc), __func__));

	wi_usb_ctl_lock(wsc->wi_usb_cdata);
	wi_func_io.f_init(wsc);
	wi_usb_ctl_unlock(wsc->wi_usb_cdata);
}


/* 
 * inquire is called from interrupt context (timeout)
 * It is not possible to sleep in interrupt context so it is necessary
 * to signal the kernel thread to perform the action.
 */
void
wi_inquire_usb(void *xsc)
{
	struct wi_softc		*wsc = xsc;
	struct wi_usb_softc	*sc = wsc->wi_usb_cdata;
	int s;


	s = splnet();

	DPRINTFN(2,("%s: %s:\n",
	    sc->wi_usb_dev.dv_xname, __func__));

	sc->wi_thread_info->status |= WI_INQUIRE;

	if (sc->wi_thread_info->idle)
		wakeup(sc->wi_thread_info);
	splx(s);
}

/* 
 * Watchdog is normally called from interrupt context (timeout)
 * It is not possible to sleep in interrupt context so it is necessary
 * to signal the kernel thread to perform the action.
 */
void
wi_watchdog_usb(struct ifnet *ifp)
{
	struct wi_softc		*wsc;
	struct wi_usb_softc	*sc;
	int s;

	wsc = ifp->if_softc;
	sc = wsc->wi_usb_cdata;

	s = splnet();

	DPRINTFN(5,("%s: %s: ifp %x\n",
	    sc->wi_usb_dev.dv_xname, __func__, ifp));

	sc->wi_thread_info->status |= WI_WATCHDOG;

	if (sc->wi_thread_info->idle)
		wakeup(sc->wi_thread_info);
	splx(s);
}

/*
 * ioctl will always be called from a user context, 
 * therefore it is possible to sleep in the calling context
 * acquire the lock and call the real ioctl function directly 
 */
int
wi_ioctl_usb(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct wi_softc		*wsc;
	int err;

	wsc = ifp->if_softc;

	wi_usb_ctl_lock(wsc->wi_usb_cdata);
	err = wi_func_io.f_ioctl(ifp, command, data);
	wi_usb_ctl_unlock(wsc->wi_usb_cdata);
	return err;
}

void
wi_usb_thread(void *arg)
{
	struct wi_usb_softc *sc = arg;
	struct wi_usb_thread_info *wi_thread_info;
	int s;

	wi_thread_info = malloc(sizeof(*wi_thread_info), M_USBDEV, M_WAITOK);

	/*
	 * is there a remote possibility that the device could
	 * be removed before the kernel thread starts up?
	 */

	sc->wi_usb_refcnt++;

	sc->wi_thread_info = wi_thread_info;
	wi_thread_info->dying = 0;
	wi_thread_info->status = 0;

	wi_usb_ctl_lock(sc);

	wi_attach(&sc->sc_wi, &wi_func_usb);

	wi_usb_ctl_unlock(sc);

	for(;;) {
		if (wi_thread_info->dying) { 
			if (--sc->wi_usb_refcnt < 0)
				usb_detach_wakeup(&sc->wi_usb_dev);
			kthread_exit(0);
		}

		DPRINTFN(5,("%s: %s: dying %x status %x\n",
		    sc->wi_usb_dev.dv_xname, __func__,
			wi_thread_info->dying, wi_thread_info->status));

		wi_usb_ctl_lock(sc);

		DPRINTFN(5,("%s: %s: starting %x\n",
		    sc->wi_usb_dev.dv_xname, __func__,
		    wi_thread_info->status));

		s = splusb();
		if (wi_thread_info->status & WI_START) {
			wi_thread_info->status &= ~WI_START;
			wi_usb_tx_lock(sc);
			wi_func_io.f_start(&sc->sc_wi.sc_ic.ic_if);
			/*
			 * tx_unlock is explicitly missing here
			 * it is done in txeof_frm
			 */
		} else if (wi_thread_info->status & WI_INQUIRE) {
			wi_thread_info->status &= ~WI_INQUIRE;
			wi_func_io.f_inquire(&sc->sc_wi);
		} else if (wi_thread_info->status & WI_WATCHDOG) {
			wi_thread_info->status &= ~WI_WATCHDOG;
			wi_func_io.f_watchdog( &sc->sc_wi.sc_ic.ic_if);
		}
		splx(s);

		DPRINTFN(5,("%s: %s: ending %x\n",
		    sc->wi_usb_dev.dv_xname, __func__,
		    wi_thread_info->status));
		wi_usb_ctl_unlock(sc);

		if (wi_thread_info->status == 0) {
			s = splnet();
			wi_thread_info->idle = 1;
			tsleep_nsec(wi_thread_info, PRIBIO, "wiIDL", INFSLP);
			wi_thread_info->idle = 0;
			splx(s);
		}
	}
}

int
wi_usb_tx_lock_try(struct wi_usb_softc *sc)
{
	int s;

	s = splnet();

	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	if (sc->wi_lock != 0) {
		splx(s);
		return 0; /* failed to acquire lock */
	}

	sc->wi_lock = 1;

	splx(s);

	return 1;
}
void
wi_usb_tx_lock(struct wi_usb_softc *sc)
{
	int s;

	s = splnet();

	again:
	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	if (sc->wi_lock != 0) {
		sc->wi_lockwait++;
		DPRINTFN(10,("%s: %s: busy %d\n", sc->wi_usb_dev.dv_xname,
		__func__, sc->wi_lockwait ));
		tsleep_nsec(&sc->wi_lock, PRIBIO, "witxl", INFSLP);
	}

	if (sc->wi_lock != 0)
		goto again;
	sc->wi_lock = 1;

	splx(s);

	return;

}

void
wi_usb_tx_unlock(struct wi_usb_softc *sc)
{
	int s;
	s = splnet();

	sc->wi_lock = 0;

	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	if (sc->wi_lockwait) {
		DPRINTFN(10,("%s: %s: waking\n",
		    sc->wi_usb_dev.dv_xname, __func__));
		sc->wi_lockwait = 0;
		wakeup(&sc->wi_lock);
	}

	splx(s);
}

void
wi_usb_ctl_lock(struct wi_usb_softc *sc)
{
	int s;

	s = splnet();

	again:
	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname,
	    __func__));

	if (sc->wi_ctllock != 0) {
		if (curproc == sc->wi_curproc) {
			/* allow recursion */
			sc->wi_ctllock++;
			splx(s);
			return;
		}
		sc->wi_ctllockwait++;
		DPRINTFN(10,("%s: %s: busy %d\n", sc->wi_usb_dev.dv_xname,
		__func__, sc->wi_ctllockwait ));
		tsleep_nsec(&sc->wi_ctllock, PRIBIO, "wiusbthr", INFSLP);
	}

	if (sc->wi_ctllock != 0)
		goto again;
	sc->wi_ctllock++;
	sc->wi_curproc = curproc;

	splx(s);

	return;

}

void
wi_usb_ctl_unlock(struct wi_usb_softc *sc)
{
	int s;

	s = splnet();

	sc->wi_ctllock--;

	DPRINTFN(10,("%s: %s: enter\n", sc->wi_usb_dev.dv_xname, __func__));

	if (sc->wi_ctllock == 0 && sc->wi_ctllockwait) {
		DPRINTFN(10,("%s: %s: waking\n",
		    sc->wi_usb_dev.dv_xname, __func__));
		sc->wi_ctllockwait = 0;
		sc->wi_curproc = 0;
		wakeup(&sc->wi_ctllock);
	}

	splx(s);
}
