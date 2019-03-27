/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Hans Petter Selasky. All rights reserved.
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

#ifndef _DWC_OTG_H_
#define	_DWC_OTG_H_

#define	DWC_OTG_MAX_DEVICES MIN(USB_MAX_DEVICES, 32)
#define	DWC_OTG_FRAME_MASK 0x7FF
#define	DWC_OTG_MAX_TXP 4
#define	DWC_OTG_MAX_TXN (0x200 * DWC_OTG_MAX_TXP)
#define	DWC_OTG_MAX_CHANNELS 16
#define	DWC_OTG_MAX_ENDPOINTS 16
#define	DWC_OTG_HOST_TIMER_RATE 10 /* ms */
#define	DWC_OTG_TT_SLOT_MAX 8
#define	DWC_OTG_SLOT_IDLE_MAX 3
#define	DWC_OTG_SLOT_IDLE_MIN 2
#ifndef DWC_OTG_TX_MAX_FIFO_SIZE
#define	DWC_OTG_TX_MAX_FIFO_SIZE DWC_OTG_MAX_TXN
#endif

#define	DWC_OTG_READ_4(sc, reg) \
  bus_space_read_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)

#define	DWC_OTG_WRITE_4(sc, reg, data)	\
  bus_space_write_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)

struct dwc_otg_td;
struct dwc_otg_softc;

typedef uint8_t (dwc_otg_cmd_t)(struct dwc_otg_softc *sc, struct dwc_otg_td *td);

struct dwc_otg_td {
	struct dwc_otg_td *obj_next;
	dwc_otg_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t tx_bytes;
	uint32_t offset;
	uint32_t remainder;
	uint32_t hcchar;		/* HOST CFG */
	uint32_t hcsplt;		/* HOST CFG */
	uint16_t max_packet_size;	/* packet_size */
	uint16_t npkt;
	uint8_t max_packet_count;	/* packet_count */
	uint8_t errcnt;
	uint8_t tmr_res;
	uint8_t tmr_val;
	uint8_t	ep_no;
	uint8_t ep_type;
	uint8_t channel[3];
	uint8_t tt_index;		/* TT data */
	uint8_t tt_start_slot;		/* TT data */
	uint8_t tt_complete_slot;	/* TT data */
	uint8_t tt_xactpos;		/* TT data */
	uint8_t state;
#define	DWC_CHAN_ST_START 0
#define	DWC_CHAN_ST_WAIT_ANE 1
#define	DWC_CHAN_ST_WAIT_S_ANE 2
#define	DWC_CHAN_ST_WAIT_C_ANE 3
#define	DWC_CHAN_ST_WAIT_C_PKT 4
#define	DWC_CHAN_ST_TX_WAIT_ISOC 5
	uint8_t	error_any:1;
	uint8_t	error_stall:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	did_stall:1;
	uint8_t toggle:1;
	uint8_t set_toggle:1;
	uint8_t got_short:1;
	uint8_t tt_scheduled:1;
	uint8_t did_nak:1;
};

struct dwc_otg_tt_info {
	uint8_t slot_index;
};

struct dwc_otg_std_temp {
	dwc_otg_cmd_t *func;
	struct usb_page_cache *pc;
	struct dwc_otg_td *td;
	struct dwc_otg_td *td_next;
	uint32_t len;
	uint32_t offset;
	uint16_t max_frame_size;
	uint8_t	short_pkt;

	/*
	 * short_pkt = 0: transfer should be short terminated
	 * short_pkt = 1: transfer should not be short terminated
	 */
	uint8_t	setup_alt_next;
	uint8_t did_stall;
	uint8_t bulk_or_control;
};

struct dwc_otg_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union dwc_otg_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct dwc_otg_flags {
	uint8_t	change_connect:1;
	uint8_t	change_suspend:1;
	uint8_t change_reset:1;
	uint8_t change_enabled:1;
	uint8_t change_over_current:1;
	uint8_t	status_suspend:1;	/* set if suspended */
	uint8_t	status_vbus:1;		/* set if present */
	uint8_t	status_bus_reset:1;	/* set if reset complete */
	uint8_t	status_high_speed:1;	/* set if High Speed is selected */
	uint8_t	status_low_speed:1;	/* set if Low Speed is selected */
	uint8_t status_device_mode:1;	/* set if device mode */
	uint8_t	self_powered:1;
	uint8_t	clocks_off:1;
	uint8_t	port_powered:1;
	uint8_t	port_enabled:1;
	uint8_t port_over_current:1;
	uint8_t	d_pulled_up:1;
};

struct dwc_otg_profile {
	struct usb_hw_ep_profile usb;
	uint16_t max_buffer;
};

struct dwc_otg_chan_state {
	uint16_t allocated;
	uint16_t wait_halted;
	uint32_t hcint;
};

struct dwc_otg_softc {
	struct usb_bus sc_bus;
	union dwc_otg_hub_temp sc_hub_temp;
	struct dwc_otg_profile sc_hw_ep_profile[DWC_OTG_MAX_ENDPOINTS];
	struct dwc_otg_tt_info sc_tt_info[DWC_OTG_MAX_DEVICES];
	struct usb_callout sc_timer;

	struct usb_device *sc_devices[DWC_OTG_MAX_DEVICES];
	struct resource *sc_io_res;
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	bus_size_t sc_io_size;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	uint32_t sc_bounce_buffer[MAX(512 * DWC_OTG_MAX_TXP, 1024) / 4];

	uint32_t sc_fifo_size;
	uint32_t sc_irq_mask;
	uint32_t sc_last_rx_status;
	uint32_t sc_out_ctl[DWC_OTG_MAX_ENDPOINTS];
	uint32_t sc_in_ctl[DWC_OTG_MAX_ENDPOINTS];
	struct dwc_otg_chan_state sc_chan_state[DWC_OTG_MAX_CHANNELS];
	uint32_t sc_tmr_val;
	uint32_t sc_hprt_val;
	uint32_t sc_xfer_complete;

	uint16_t sc_current_rx_bytes;
	uint16_t sc_current_rx_fifo;

	uint16_t sc_active_rx_ep;
	uint16_t sc_last_frame_num;

	uint8_t sc_phy_type;
	uint8_t sc_phy_bits;
#define	DWC_OTG_PHY_ULPI 1
#define	DWC_OTG_PHY_HSIC 2
#define	DWC_OTG_PHY_INTERNAL 3
#define	DWC_OTG_PHY_UTMI 4

	uint8_t sc_timer_active;
	uint8_t	sc_dev_ep_max;
	uint8_t sc_dev_in_ep_max;
	uint8_t	sc_host_ch_max;
	uint8_t sc_needsof;
	uint8_t	sc_rt_addr;		/* root HUB address */
	uint8_t	sc_conf;		/* root HUB config */
	uint8_t sc_mode;		/* mode of operation */
#define	DWC_MODE_OTG 0		/* both modes */
#define	DWC_MODE_DEVICE 1	/* device only */
#define	DWC_MODE_HOST  2	/* host only */

	uint8_t	sc_hub_idata[1];

	struct dwc_otg_flags sc_flags;
};

/* prototypes */

driver_filter_t dwc_otg_filter_interrupt;
driver_intr_t dwc_otg_interrupt;
int dwc_otg_init(struct dwc_otg_softc *);
void dwc_otg_uninit(struct dwc_otg_softc *);

#endif		/* _DWC_OTG_H_ */
