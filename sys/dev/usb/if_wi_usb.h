/*	$OpenBSD: if_wi_usb.h,v 1.3 2015/06/12 15:47:31 mpi Exp $ */

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

#define WI_USB_ENDPT_TX		1
#define WI_USB_ENDPT_RX		2
#define WI_USB_ENDPT_INTR	3
#define WI_USB_ENDPT_MAX	4


/* XXX */
#define WI_USB_DATA_MAXLEN	WI_DEFAULT_DATALEN
#define WI_USB_BUFSZ		2368	/* MAX PACKET LEN ???  n%64 == 0 */
#define WI_USB_INTR_INTERVAL	100	/* ms */

struct wi_usb_softc;

struct wi_usb_chain {
	struct wi_usb_softc	*wi_usb_sc;
	struct usbd_xfer	*wi_usb_xfer;
	void			*wi_usb_buf;
	struct mbuf		*wi_usb_mbuf;
	int			wi_usb_idx;
};
#define WI_USB_TX_LIST_CNT	1
#define WI_USB_RX_LIST_CNT	1

struct wi_rridreq {
	u_int16_t		type;		/* 0x00 */
	u_int16_t		frmlen;		/* 0x02 */
	u_int16_t		rid;		/* 0x04 */
	u_int8_t		pad[58]; 	/* 0x06	+ sizeof(.) == 64 */	
};
struct wi_rridresp {
	u_int16_t		type;		/* 0x00 */
	u_int16_t		frmlen;		/* 0x02 */
	u_int16_t		rid;		/* 0x04 */
	u_int8_t		data[1658];	/* 0x06 */
	/* sizeof(struct wi_rridresp) == WI_USB_BUFSZ */
};
struct wi_wridreq {
	u_int16_t		type;		/* 0x00 */
	u_int16_t		frmlen;		/* 0x02 */
	u_int16_t		rid;		/* 0x04 */
	u_int8_t		data[2048];	/* 0x06 */
};
struct wi_wridresp {
	u_int16_t		type;
	u_int16_t		status;
	u_int16_t		resp0;
	u_int16_t		resp1;
	u_int16_t		resp2;
};
struct wi_info {
	u_int16_t		type;
	u_int16_t		info;
};


#define WI_USB_CMD_INIT		0x0
#define WI_USB_CMD_ENABLE	0x1
#define WI_USB_CMD_DISABLE	0x2
#define WI_USB_CMD_DIAG		0x3

struct wi_cmdreq {
	u_int16_t		type;
	u_int16_t		cmd;
	u_int16_t		param0;
	u_int16_t		param1;
	u_int16_t		param2;
	u_int8_t		pad[54];
};
struct wi_cmdresp {
	u_int16_t		type;
	u_int16_t		status;
	u_int16_t		resp0;
	u_int16_t		resp1;
	u_int16_t		resp2;
};

typedef union {
	u_int16_t		type;
	struct wi_rridreq	rridreq;
	struct wi_rridresp	rridresp;
	struct wi_cmdreq	cmdreq;
	struct wi_cmdresp	cmdresp;
} wi_usb_usbin;
#define WI_USB_INTR_PKTLEN	8

#define WI_USB_TX_TIMEOUT	10000 /* ms */


/* Should be sent to the bulkout endpoint */
#define WI_USB_TXFRM		0
#define WI_USB_CMDREQ		1
#define WI_USB_WRIDREQ		2
#define WI_USB_RRIDREQ		3
#define WI_USB_WMEMREQ		4
#define WI_USB_RMEMREQ		5

/* Received from the bulkin endpoint */
#define WI_USB_ISTXFRM(a)	(((a) & 0xf000) == 0x0000)
#define WI_USB_ISRXFRM(a)	(((a) & 0xf000) == 0x2000)

#define WI_USB_INFOFRM		0x8000
#define WI_USB_CMDRESP		0x8001
#define WI_USB_WRIDRESP		0x8002
#define WI_USB_RRIDRESP		0x8003
#define WI_USB_WMEMRESP		0x8004
#define WI_USB_RMEMRESP		0x8005
#define WI_USB_BUFAVAIL		0x8006
#define WI_USB_ERROR		0x8007

#define WI_GET_IFP(sc)		&(sc)->sc_wi.sc_ic.ic_if

/* USB */
int wi_cmd_usb(struct wi_softc *sc, int cmd, int val0, int val1, int val2);
int wi_read_record_usb(struct wi_softc *sc, struct wi_ltv_gen *ltv);
int wi_write_record_usb(struct wi_softc *sc, struct wi_ltv_gen *ltv);
int wi_read_data_usb(struct wi_softc *sc, int id, int off, caddr_t buf,
    int len);
int wi_write_data_usb(struct wi_softc *sc, int id, int off, caddr_t buf,
    int len);
int wi_alloc_nicmem_usb(struct wi_softc *sc, int len, int *id);
int wi_get_fid_usb(struct wi_softc *sc, int fid);
void wi_init_usb(struct wi_softc *sc);

void wi_start_usb(struct ifnet *ifp);
int wi_ioctl_usb(struct ifnet *, u_long, caddr_t);
void wi_inquire_usb(void *xsc);
void wi_watchdog_usb(struct ifnet *ifp);
