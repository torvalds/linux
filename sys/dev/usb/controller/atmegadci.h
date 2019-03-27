/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Hans Petter Selasky. All rights reserved.
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
 * USB Device Port register definitions, copied from ATMEGA documentation
 * provided by ATMEL.
 */

#ifndef _ATMEGADCI_H_
#define	_ATMEGADCI_H_

#define	ATMEGA_MAX_DEVICES (USB_MIN_DEVICES + 1)

#define	ATMEGA_OTGTCON 0xF9
#define	ATMEGA_OTGTCON_VALUE(x) ((x) << 0)
#define	ATMEGA_OTGTCON_PAGE(x) ((x) << 5)

#define	ATMEGA_UEINT 0xF4
#define	ATMEGA_UEINT_MASK(n) (1 << (n))	/* endpoint interrupt mask */

#define	ATMEGA_UEBCHX 0xF3		/* FIFO byte count high */
#define	ATMEGA_UEBCLX 0xF2		/* FIFO byte count low */
#define	ATMEGA_UEDATX 0xF1		/* FIFO data */

#define	ATMEGA_UEIENX 0xF0		/* interrupt enable register */
#define	ATMEGA_UEIENX_TXINE (1 << 0)
#define	ATMEGA_UEIENX_STALLEDE (1 << 1)
#define	ATMEGA_UEIENX_RXOUTE (1 << 2)
#define	ATMEGA_UEIENX_RXSTPE (1 << 3)	/* received SETUP packet */
#define	ATMEGA_UEIENX_NAKOUTE (1 << 4)
#define	ATMEGA_UEIENX_NAKINE (1 << 6)
#define	ATMEGA_UEIENX_FLERRE (1 << 7)

#define	ATMEGA_UESTA1X 0xEF
#define	ATMEGA_UESTA1X_CURRBK (3 << 0)	/* current bank */
#define	ATMEGA_UESTA1X_CTRLDIR (1 << 2)	/* control endpoint direction */

#define	ATMEGA_UESTA0X 0xEE
#define	ATMEGA_UESTA0X_NBUSYBK (3 << 0)
#define	ATMEGA_UESTA0X_DTSEQ (3 << 2)
#define	ATMEGA_UESTA0X_UNDERFI (1 << 5)	/* underflow */
#define	ATMEGA_UESTA0X_OVERFI (1 << 6)	/* overflow */
#define	ATMEGA_UESTA0X_CFGOK (1 << 7)

#define	ATMEGA_UECFG1X 0xED		/* endpoint config register */
#define	ATMEGA_UECFG1X_ALLOC (1 << 1)
#define	ATMEGA_UECFG1X_EPBK0 (0 << 2)
#define	ATMEGA_UECFG1X_EPBK1 (1 << 2)
#define	ATMEGA_UECFG1X_EPBK2 (2 << 2)
#define	ATMEGA_UECFG1X_EPBK3 (3 << 2)
#define	ATMEGA_UECFG1X_EPSIZE(n) ((n) << 4)

#define	ATMEGA_UECFG0X 0xEC
#define	ATMEGA_UECFG0X_EPDIR (1 << 0)	/* endpoint direction */
#define	ATMEGA_UECFG0X_EPTYPE0 (0 << 6)
#define	ATMEGA_UECFG0X_EPTYPE1 (1 << 6)
#define	ATMEGA_UECFG0X_EPTYPE2 (2 << 6)
#define	ATMEGA_UECFG0X_EPTYPE3 (3 << 6)

#define	ATMEGA_UECONX 0xEB
#define	ATMEGA_UECONX_EPEN (1 << 0)
#define	ATMEGA_UECONX_RSTDT (1 << 3)
#define	ATMEGA_UECONX_STALLRQC (1 << 4)	/* stall request clear */
#define	ATMEGA_UECONX_STALLRQ (1 << 5)	/* stall request set */

#define	ATMEGA_UERST 0xEA		/* endpoint reset register */
#define	ATMEGA_UERST_MASK(n) (1 << (n))

#define	ATMEGA_UENUM 0xE9		/* endpoint number */

#define	ATMEGA_UEINTX 0xE8		/* interrupt register */
#define	ATMEGA_UEINTX_TXINI (1 << 0)
#define	ATMEGA_UEINTX_STALLEDI (1 << 1)
#define	ATMEGA_UEINTX_RXOUTI (1 << 2)
#define	ATMEGA_UEINTX_RXSTPI (1 << 3)	/* received setup packet */
#define	ATMEGA_UEINTX_NAKOUTI (1 << 4)
#define	ATMEGA_UEINTX_RWAL (1 << 5)
#define	ATMEGA_UEINTX_NAKINI (1 << 6)
#define	ATMEGA_UEINTX_FIFOCON (1 << 7)

#define	ATMEGA_UDMFN 0xE6
#define	ATMEGA_UDMFN_FNCERR (1 << 4)

#define	ATMEGA_UDFNUMH 0xE5		/* frame number high */
#define	ATMEGA_UDFNUMH_MASK 7

#define	ATMEGA_UDFNUML 0xE4		/* frame number low */
#define	ATMEGA_UDFNUML_MASK 0xFF

#define	ATMEGA_FRAME_MASK 0x7FF

#define	ATMEGA_UDADDR 0xE3		/* USB address */
#define	ATMEGA_UDADDR_MASK 0x7F
#define	ATMEGA_UDADDR_ADDEN (1 << 7)

#define	ATMEGA_UDIEN 0xE2		/* USB device interrupt enable */
#define	ATMEGA_UDINT_SUSPE (1 << 0)
#define	ATMEGA_UDINT_MSOFE (1 << 1)
#define	ATMEGA_UDINT_SOFE (1 << 2)
#define	ATMEGA_UDINT_EORSTE (1 << 3)
#define	ATMEGA_UDINT_WAKEUPE (1 << 4)
#define	ATMEGA_UDINT_EORSME (1 << 5)
#define	ATMEGA_UDINT_UPRSME (1 << 6)

#define	ATMEGA_UDINT 0xE1		/* USB device interrupt status */
#define	ATMEGA_UDINT_SUSPI (1 << 0)
#define	ATMEGA_UDINT_MSOFI (1 << 1)
#define	ATMEGA_UDINT_SOFI (1 << 2)
#define	ATMEGA_UDINT_EORSTI (1 << 3)
#define	ATMEGA_UDINT_WAKEUPI (1 << 4)
#define	ATMEGA_UDINT_EORSMI (1 << 5)
#define	ATMEGA_UDINT_UPRSMI (1 << 6)

#define	ATMEGA_UDCON 0xE0		/* USB device connection register */
#define	ATMEGA_UDCON_DETACH (1 << 0)
#define	ATMEGA_UDCON_RMWKUP (1 << 1)
#define	ATMEGA_UDCON_LSM (1 << 2)
#define	ATMEGA_UDCON_RSTCPU (1 << 3)

#define	ATMEGA_OTGINT 0xDF

#define	ATMEGA_OTGCON 0xDD
#define	ATMEGA_OTGCON_VBUSRQC (1 << 0)
#define	ATMEGA_OTGCON_VBUSREQ (1 << 1)
#define	ATMEGA_OTGCON_VBUSHWC (1 << 2)
#define	ATMEGA_OTGCON_SRPSEL (1 << 3)
#define	ATMEGA_OTGCON_SRPREQ (1 << 4)
#define	ATMEGA_OTGCON_HNPREQ (1 << 5)

#define	ATMEGA_USBINT 0xDA
#define	ATMEGA_USBINT_VBUSTI (1 << 0)	/* USB VBUS interrupt */
#define	ATMEGA_USBINT_IDI (1 << 1)	/* USB ID interrupt */

#define	ATMEGA_USBSTA 0xD9
#define	ATMEGA_USBSTA_VBUS (1 << 0)
#define	ATMEGA_USBSTA_ID (1 << 1)

#define	ATMEGA_USBCON 0xD8
#define	ATMEGA_USBCON_VBUSTE (1 << 0)
#define	ATMEGA_USBCON_IDE (1 << 1)
#define	ATMEGA_USBCON_OTGPADE (1 << 4)
#define	ATMEGA_USBCON_FRZCLK (1 << 5)
#define	ATMEGA_USBCON_USBE (1 << 7)

#define	ATMEGA_UHWCON 0xD7
#define	ATMEGA_UHWCON_UVREGE (1 << 0)
#define	ATMEGA_UHWCON_UVCONE (1 << 4)
#define	ATMEGA_UHWCON_UIDE (1 << 6)
#define	ATMEGA_UHWCON_UIMOD (1 << 7)

#define	ATMEGA_READ_1(sc, reg) \
  bus_space_read_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)

#define	ATMEGA_WRITE_1(sc, reg, data) \
  bus_space_write_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)

#define	ATMEGA_WRITE_MULTI_1(sc, reg, ptr, len) \
  bus_space_write_multi_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, ptr, len)

#define	ATMEGA_READ_MULTI_1(sc, reg, ptr, len) \
  bus_space_read_multi_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, ptr, len)

/*
 * Maximum number of endpoints supported:
 */
#define	ATMEGA_EP_MAX 7

struct atmegadci_td;

typedef uint8_t (atmegadci_cmd_t)(struct atmegadci_td *td);
typedef void (atmegadci_clocks_t)(struct usb_bus *);

struct atmegadci_td {
	struct atmegadci_td *obj_next;
	atmegadci_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t remainder;
	uint16_t max_packet_size;
	uint8_t	error:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	support_multi_buffer:1;
	uint8_t	did_stall:1;
	uint8_t	ep_no:3;
};

struct atmegadci_std_temp {
	atmegadci_cmd_t *func;
	struct usb_page_cache *pc;
	struct atmegadci_td *td;
	struct atmegadci_td *td_next;
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
};

struct atmegadci_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union atmegadci_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct atmegadci_flags {
	uint8_t	change_connect:1;
	uint8_t	change_suspend:1;
	uint8_t	status_suspend:1;	/* set if suspended */
	uint8_t	status_vbus:1;		/* set if present */
	uint8_t	status_bus_reset:1;	/* set if reset complete */
	uint8_t	remote_wakeup:1;
	uint8_t	self_powered:1;
	uint8_t	clocks_off:1;
	uint8_t	port_powered:1;
	uint8_t	port_enabled:1;
	uint8_t	d_pulled_up:1;
};

struct atmegadci_softc {
	struct usb_bus sc_bus;
	union atmegadci_hub_temp sc_hub_temp;

	/* must be set by by the bus interface layer */
	atmegadci_clocks_t *sc_clocks_on;
	atmegadci_clocks_t *sc_clocks_off;

	struct usb_device *sc_devices[ATMEGA_MAX_DEVICES];
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	struct resource *sc_io_res;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	uint8_t	sc_rt_addr;		/* root hub address */
	uint8_t	sc_dv_addr;		/* device address */
	uint8_t	sc_conf;		/* root hub config */

	uint8_t	sc_hub_idata[1];

	struct atmegadci_flags sc_flags;
};

/* prototypes */

usb_error_t atmegadci_init(struct atmegadci_softc *sc);
void	atmegadci_uninit(struct atmegadci_softc *sc);
void	atmegadci_interrupt(struct atmegadci_softc *sc);

#endif					/* _ATMEGADCI_H_ */
