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

#ifndef _AVR32DCI_H_
#define	_AVR32DCI_H_

#define	AVR32_MAX_DEVICES (USB_MIN_DEVICES + 1)

/* Register definitions */

#define	AVR32_CTRL 0x00			/* Control */
#define	AVR32_CTRL_DEV_ADDR 0x7F
#define	AVR32_CTRL_DEV_FADDR_EN 0x80
#define	AVR32_CTRL_DEV_EN_USBA 0x100
#define	AVR32_CTRL_DEV_DETACH 0x200
#define	AVR32_CTRL_DEV_REWAKEUP 0x400

#define	AVR32_FNUM 0x04			/* Frame Number */
#define	AVR32_FNUM_MASK 0x3FFF
#define	AVR32_FRAME_MASK 0x7FF

/* 0x08 - 0x0C Reserved */
#define	AVR32_IEN 0x10			/* Interrupt Enable */
#define	AVR32_INTSTA 0x14		/* Interrupt Status */
#define	AVR32_CLRINT 0x18		/* Clear Interrupt */

#define	AVR32_INT_SPEED 0x00000001	/* set if High Speed else Full Speed */
#define	AVR32_INT_DET_SUSPD 0x00000002
#define	AVR32_INT_MICRO_SOF 0x00000004
#define	AVR32_INT_INT_SOF 0x00000008
#define	AVR32_INT_ENDRESET 0x00000010
#define	AVR32_INT_WAKE_UP 0x00000020
#define	AVR32_INT_ENDOFRSM 0x00000040
#define	AVR32_INT_UPSTR_RES 0x00000080
#define	AVR32_INT_EPT_INT(n) (0x00000100 << (n))
#define	AVR32_INT_DMA_INT(n) (0x01000000 << (n))

#define	AVR32_EPTRST 0x1C		/* Endpoints Reset */
#define	AVR32_EPTRST_MASK(n) (0x00000001 << (n))

/* 0x20 - 0xCC Reserved */
#define	AVR32_TSTSOFCNT 0xD0		/* Test SOF Counter */
#define	AVR32_TSTCNTA 0xD4		/* Test A Counter */
#define	AVR32_TSTCNTB 0xD8		/* Test B Counter */
#define	AVR32_TSTMODEREG 0xDC		/* Test Mode */
#define	AVR32_TST 0xE0			/* Test */
#define	AVR32_TST_NORMAL 0x00000000
#define	AVR32_TST_HS_ONLY 0x00000002
#define	AVR32_TST_FS_ONLY 0x00000003

/* 0xE4 - 0xE8 Reserved */
#define	AVR32_IPPADDRSIZE 0xEC		/* PADDRSIZE */
#define	AVR32_IPNAME1 0xF0		/* Name1 */
#define	AVR32_IPNAME2 0xF4		/* Name2 */
#define	AVR32_IPFEATURES 0xF8		/* Features */
#define	AVR32_IPFEATURES_NEP(x) (((x) & 0xF) ? ((x) & 0xF) : 0x10)

#define	AVR32_IPVERSION 0xFC		/* IP Version */

#define	_A(base,n) ((base) + (0x20*(n)))
#define	AVR32_EPTCFG(n) _A(0x100, n)	/* Endpoint Configuration */
#define	AVR32_EPTCFG_EPSIZE(n) ((n)-3)	/* power of two */
#define	AVR32_EPTCFG_EPDIR_OUT 0x00000000
#define	AVR32_EPTCFG_EPDIR_IN 0x00000008
#define	AVR32_EPTCFG_TYPE_CTRL 0x00000000
#define	AVR32_EPTCFG_TYPE_ISOC 0x00000100
#define	AVR32_EPTCFG_TYPE_BULK 0x00000200
#define	AVR32_EPTCFG_TYPE_INTR 0x00000300
#define	AVR32_EPTCFG_NBANK(n) (0x00000400*(n))
#define	AVR32_EPTCFG_NB_TRANS(n) (0x00001000*(n))
#define	AVR32_EPTCFG_EPT_MAPD 0x80000000

#define	AVR32_EPTCTLENB(n) _A(0x104, n)	/* Endpoint Control Enable */
#define	AVR32_EPTCTLDIS(n) _A(0x108, n)	/* Endpoint Control Disable */
#define	AVR32_EPTCTL(n) _A(0x10C, n)	/* Endpoint Control */
#define	AVR32_EPTCTL_EPT_ENABL 0x00000001
#define	AVR32_EPTCTL_AUTO_VALID 0x00000002
#define	AVR32_EPTCTL_INTDIS_DMA 0x00000008
#define	AVR32_EPTCTL_NYET_DIS 0x00000010
#define	AVR32_EPTCTL_DATAX_RX 0x00000040
#define	AVR32_EPTCTL_MDATA_RX 0x00000080
#define	AVR32_EPTCTL_ERR_OVFLW 0x00000100
#define	AVR32_EPTCTL_RX_BK_RDY 0x00000200
#define	AVR32_EPTCTL_TX_COMPLT 0x00000400
#define	AVR32_EPTCTL_TX_PK_RDY 0x00000800
#define	AVR32_EPTCTL_RX_SETUP 0x00001000
#define	AVR32_EPTCTL_STALL_SNT 0x00002000
#define	AVR32_EPTCTL_NAK_IN 0x00004000
#define	AVR32_EPTCTL_NAK_OUT 0x00008000
#define	AVR32_EPTCTL_BUSY_BANK 0x00040000
#define	AVR32_EPTCTL_SHORT_PCKT 0x80000000

/* 0x110 Reserved */
#define	AVR32_EPTSETSTA(n) _A(0x114, n)	/* Endpoint Set Status */
#define	AVR32_EPTCLRSTA(n) _A(0x118, n)	/* Endpoint Clear Status */
#define	AVR32_EPTSTA(n) _A(0x11C, n)	/* Endpoint Status */
#define	AVR32_EPTSTA_FRCESTALL 0x00000020
#define	AVR32_EPTSTA_TOGGLESQ_STA(x) (((x) & 0xC0) >> 6)
#define	AVR32_EPTSTA_TOGGLESQ 0x00000040
#define	AVR32_EPTSTA_ERR_OVFLW 0x00000100
#define	AVR32_EPTSTA_RX_BK_RDY 0x00000200
#define	AVR32_EPTSTA_TX_COMPLT 0x00000400
#define	AVR32_EPTSTA_TX_PK_RDY 0x00000800
#define	AVR32_EPTSTA_RX_SETUP 0x00001000
#define	AVR32_EPTSTA_STALL_SNT 0x00002000
#define	AVR32_EPTSTA_NAK_IN 0x00004000
#define	AVR32_EPTSTA_NAK_OUT 0x00008000
#define	AVR32_EPTSTA_CURRENT_BANK(x) (((x) & 0x00030000) >> 16)
#define	AVR32_EPTSTA_BUSY_BANK_STA(x) (((x) & 0x000C0000) >> 18)
#define	AVR32_EPTSTA_BYTE_COUNT(x) (((x) & 0x7FF00000) >> 20)
#define	AVR32_EPTSTA_SHRT_PCKT 0x80000000

/* 0x300 - 0x30C Reserved */
#define	AVR32_DMANXTDSC 0x310		/* DMA Next Descriptor Address */
#define	AVR32_DMAADDRESS 0x314		/* DMA Channel Address */

#define	AVR32_READ_4(sc, reg) \
  bus_space_read_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)

#define	AVR32_WRITE_4(sc, reg, data) \
  bus_space_write_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)

#define	AVR32_WRITE_MULTI_4(sc, reg, ptr, len) \
  bus_space_write_multi_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, ptr, len)

#define	AVR32_READ_MULTI_4(sc, reg, ptr, len) \
  bus_space_read_multi_4((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, ptr, len)

/*
 * Maximum number of endpoints supported:
 */
#define	AVR32_EP_MAX 7

struct avr32dci_td;

typedef uint8_t (avr32dci_cmd_t)(struct avr32dci_td *td);
typedef void (avr32dci_clocks_t)(struct usb_bus *);

struct avr32dci_td {
	struct avr32dci_td *obj_next;
	avr32dci_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t remainder;
	uint16_t max_packet_size;
	uint8_t bank_shift;
	uint8_t	error:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	support_multi_buffer:1;
	uint8_t	did_stall:1;
	uint8_t	ep_no:3;
};

struct avr32dci_std_temp {
	avr32dci_cmd_t *func;
	struct usb_page_cache *pc;
	struct avr32dci_td *td;
	struct avr32dci_td *td_next;
	uint32_t len;
	uint32_t offset;
	uint16_t max_frame_size;
	uint8_t	bank_shift;
	uint8_t	short_pkt;
	/*
         * short_pkt = 0: transfer should be short terminated
         * short_pkt = 1: transfer should not be short terminated
         */
	uint8_t	setup_alt_next;
	uint8_t did_stall;
};

struct avr32dci_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union avr32dci_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct avr32dci_flags {
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

struct avr32dci_softc {
	struct usb_bus sc_bus;
	union avr32dci_hub_temp sc_hub_temp;

	/* must be set by by the bus interface layer */
	avr32dci_clocks_t *sc_clocks_on;
	avr32dci_clocks_t *sc_clocks_off;

	struct usb_device *sc_devices[AVR32_MAX_DEVICES];
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	struct resource *sc_io_res;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;
	uint8_t *physdata;

	uint8_t	sc_rt_addr;		/* root hub address */
	uint8_t	sc_dv_addr;		/* device address */
	uint8_t	sc_conf;		/* root hub config */

	uint8_t	sc_hub_idata[1];

	struct avr32dci_flags sc_flags;
};

/* prototypes */

usb_error_t avr32dci_init(struct avr32dci_softc *sc);
void	avr32dci_uninit(struct avr32dci_softc *sc);
void	avr32dci_interrupt(struct avr32dci_softc *sc);
void	avr32dci_vbus_interrupt(struct avr32dci_softc *sc, uint8_t is_on);

#endif					/* _AVR32DCI_H_ */
