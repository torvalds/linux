/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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

#ifndef _OCTUSB_H_
#define	_OCTUSB_H_

#define	OCTUSB_MAX_DEVICES MIN(USB_MAX_DEVICES, 64)
#define	OCTUSB_MAX_PORTS	2	/* hardcoded */
#define	OCTUSB_MAX_FIXUP	4096	/* bytes */
#define	OCTUSB_INTR_ENDPT	0x01

struct octusb_qh;
struct octusb_td;
struct octusb_softc;

typedef uint8_t (octusb_cmd_t)(struct octusb_td *td);

struct octusb_td {
	struct octusb_qh *qh;
	struct octusb_td *obj_next;
	struct usb_page_cache *pc;
	octusb_cmd_t *func;

	uint32_t remainder;
	uint32_t offset;

	uint8_t	error_any:1;
	uint8_t	error_stall:1;
	uint8_t	short_pkt:1;
	uint8_t	alt_next:1;
	uint8_t	reserved:4;
};

struct octusb_qh {

	uint64_t fixup_phys;

	struct octusb_softc *sc;
	struct usb_page_cache *fixup_pc;
	uint8_t *fixup_buf;

	cvmx_usb_iso_packet_t iso_pkt;

	uint32_t fixup_off;

	uint16_t max_frame_size;
	uint16_t max_packet_size;
	uint16_t fixup_actlen;
	uint16_t fixup_len;
	uint16_t ep_interval;

	uint8_t	dev_addr;
	uint8_t	dev_speed;
	uint8_t	ep_allocated;
	uint8_t	ep_mult;
	uint8_t	ep_num;
	uint8_t	ep_type;
	uint8_t	ep_toggle_next;
	uint8_t	root_port_index;
	uint8_t	fixup_complete;
	uint8_t	fixup_pending;
	uint8_t	hs_hub_addr;
	uint8_t	hs_hub_port;

	int	fixup_handle;
	int	ep_handle;
};

struct octusb_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union octusb_hub_desc {
	struct usb_status stat;
	struct usb_port_status ps;
	uint8_t	temp[128];
};

struct octusb_port {
	cvmx_usb_state_t state;
	uint8_t	disabled;
};

struct octusb_softc {

	struct usb_bus sc_bus;		/* base device */
	union octusb_hub_desc sc_hub_desc;

	struct usb_device *sc_devices[OCTUSB_MAX_DEVICES];

	struct resource *sc_irq_res[OCTUSB_MAX_PORTS];
	void   *sc_intr_hdl[OCTUSB_MAX_PORTS];

	struct octusb_port sc_port[OCTUSB_MAX_PORTS];
	device_t sc_dev;

	struct usb_hub_descriptor_min sc_hubd;

	uint8_t	sc_noport;		/* number of ports */
	uint8_t	sc_addr;		/* device address */
	uint8_t	sc_conf;		/* device configuration */
	uint8_t	sc_isreset;		/* set if current port is reset */

	uint8_t	sc_hub_idata[1];
};

usb_bus_mem_cb_t octusb_iterate_hw_softc;
usb_error_t octusb_init(struct octusb_softc *);
usb_error_t octusb_uninit(struct octusb_softc *);
void	octusb_interrupt(struct octusb_softc *);

#endif					/* _OCTUSB_H_ */
