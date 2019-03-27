/* $FreeBSD$ */
/*-
 * Copyright (c) 2014 Hans Petter Selasky <hselasky@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#ifndef _SAF1761_OTG_H_
#define	_SAF1761_OTG_H_

#define	SOTG_MAX_DEVICES MIN(USB_MAX_DEVICES, 32)
#define	SOTG_FS_MAX_PACKET_SIZE 64
#define	SOTG_HS_MAX_PACKET_SIZE 512
#define	SOTG_NUM_PORTS 2	/* one Device and one Host port */
#define	SOTG_HOST_PORT_NUM 1
#define	SOTG_DEVICE_PORT_NUM 2
#define	SOTG_HOST_CHANNEL_MAX (3 * 32)

/* Macros used for reading and writing little endian registers */

#define	SAF1761_READ_LE_4(sc, reg) ({ uint32_t _temp; \
  _temp = bus_space_read_4((sc)->sc_io_tag, (sc)->sc_io_hdl, (reg)); \
  le32toh(_temp); })

#define	SAF1761_WRITE_LE_4(sc, reg, data) do { \
  uint32_t _temp = (data); \
  bus_space_write_4((sc)->sc_io_tag, (sc)->sc_io_hdl, (reg), htole32(_temp)); \
} while (0)

/* 90ns delay macro */

#define	SAF1761_90NS_DELAY(sc) do { \
	(void) SAF1761_READ_LE_4(sc, SOTG_VEND_PROD_ID); \
	(void) SAF1761_READ_LE_4(sc, SOTG_VEND_PROD_ID); \
	(void) SAF1761_READ_LE_4(sc, SOTG_VEND_PROD_ID); \
	(void) SAF1761_READ_LE_4(sc, SOTG_VEND_PROD_ID); \
} while (0)

struct saf1761_otg_softc;
struct saf1761_otg_td;

typedef uint8_t (saf1761_otg_cmd_t)(struct saf1761_otg_softc *, struct saf1761_otg_td *td);

struct saf1761_otg_td {
	struct saf1761_otg_td *obj_next;
	saf1761_otg_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t remainder;
	uint32_t dw1_value;
	uint16_t max_packet_size;
	uint8_t	ep_index;
	uint8_t ep_type;
	uint8_t channel;
	uint8_t uframe;
	uint8_t interval;
	uint8_t	error_any:1;
	uint8_t	error_stall:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	did_stall:1;
	uint8_t	toggle:1;
	uint8_t	set_toggle:1;
};

struct saf1761_otg_std_temp {
	saf1761_otg_cmd_t *func;
	struct usb_page_cache *pc;
	struct saf1761_otg_td *td;
	struct saf1761_otg_td *td_next;
	uint32_t len;
	uint32_t offset;
	uint16_t max_frame_size;
	uint8_t	short_pkt;
	/*
         * short_pkt = 0: transfer should be short terminated
         * short_pkt = 1: transfer should not be short terminated
         */
	uint8_t	setup_alt_next;
	uint8_t	did_stall;
};

struct saf1761_otg_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union saf1761_otg_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct saf1761_otg_flags {
	uint8_t	change_connect:1;
	uint8_t	change_suspend:1;
	uint8_t	status_suspend:1;	/* set if suspended */
	uint8_t	status_vbus:1;		/* set if present */
	uint8_t	status_bus_reset:1;	/* set if reset complete */
	uint8_t	clocks_off:1;
	uint8_t	port_powered:1;
	uint8_t	port_enabled:1;
	uint8_t	d_pulled_up:1;
};

struct saf1761_otg_softc {
	struct usb_bus sc_bus;
	union saf1761_otg_hub_temp sc_hub_temp;

	struct usb_device *sc_devices[SOTG_MAX_DEVICES];
	struct resource *sc_io_res;
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	bus_size_t sc_io_size;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	uint32_t sc_host_async_busy_map[2];
	uint32_t sc_host_async_map;
	uint32_t sc_host_async_suspend_map;
	uint32_t sc_host_intr_busy_map[2];
	uint32_t sc_host_intr_map;
	uint32_t sc_host_intr_suspend_map;
	uint32_t sc_host_isoc_busy_map[2];
	uint32_t sc_host_isoc_map;
	uint32_t sc_host_isoc_suspend_map;
	uint32_t sc_intr_enable;	/* enabled interrupts */
	uint32_t sc_hw_mode;		/* hardware mode */
	uint32_t sc_interrupt_cfg;	/* interrupt configuration */
	uint32_t sc_xfer_complete;

	uint32_t sc_bounce_buffer[1024 / 4];

	uint8_t	sc_rt_addr;		/* root HUB address */
	uint8_t	sc_dv_addr;		/* device address */
	uint8_t	sc_conf;		/* root HUB config */
	uint8_t sc_isreset;		/* host mode */

	uint8_t	sc_hub_idata[1];

	struct saf1761_otg_flags sc_flags;
};

/* prototypes */

usb_error_t saf1761_otg_init(struct saf1761_otg_softc *sc);
void	saf1761_otg_uninit(struct saf1761_otg_softc *sc);
driver_filter_t saf1761_otg_filter_interrupt;
driver_intr_t saf1761_otg_interrupt;

#endif					/* _SAF1761_OTG_H_ */
