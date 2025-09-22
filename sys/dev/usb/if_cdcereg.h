/*	$OpenBSD: if_cdcereg.h,v 1.9 2024/01/04 08:41:59 kevlo Exp $ */

/*
 * Copyright (c) 1997, 1998, 1999, 2000-2003 Bill Paul <wpaul@windriver.com>
 * Copyright (c) 2003 Craig Boston
 * Copyright (c) 2004 Daniel Hartmeier
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul, THE VOICES IN HIS HEAD OR
 * THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define CDCE_RX_LIST_CNT	1
#define CDCE_TX_LIST_CNT	1
#define CDCE_BUFSZ		1542

struct cdce_type {
	struct usb_devno	 cdce_dev;
	u_int16_t		 cdce_flags;
#define CDCE_CRC32	1
#define CDCE_SWAPUNION	2
};

struct cdce_softc;

struct cdce_chain {
	struct cdce_softc	*cdce_sc;
	struct usbd_xfer	*cdce_xfer;
	char			*cdce_buf;
	struct mbuf		*cdce_mbuf;
	int			 cdce_accum;
	int			 cdce_idx;
};

struct cdce_cdata {
	struct cdce_chain	 cdce_rx_chain[CDCE_RX_LIST_CNT];
	struct cdce_chain	 cdce_tx_chain[CDCE_TX_LIST_CNT];
	int			 cdce_tx_prod;
	int			 cdce_tx_cons;
	int			 cdce_tx_cnt;
	int			 cdce_rx_prod;
};

struct cdce_softc {
	struct device		 cdce_dev;
	struct arpcom		 cdce_arpcom;
#define GET_IFP(sc) (&(sc)->cdce_arpcom.ac_if)
	struct usbd_device	*cdce_udev;
	struct usbd_interface	*cdce_ctl_iface;
	int			 cdce_intr_no;
	struct usbd_pipe	*cdce_intr_pipe;
	struct usb_cdc_notification cdce_intr_buf;
	int			 cdce_intr_size;
	struct usbd_interface	*cdce_data_iface;
	int			 cdce_bulkin_no;
	struct usbd_pipe	*cdce_bulkin_pipe;
	int			 cdce_bulkout_no;
	struct usbd_pipe	*cdce_bulkout_pipe;
	struct cdce_cdata	 cdce_cdata;
	int			 cdce_rxeof_errors;
	u_int16_t		 cdce_flags;
	char			 cdce_attached;
};
