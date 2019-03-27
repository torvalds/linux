/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Hans Petter Selasky <hselasky@FreeBSD.org>
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

#ifndef _USS820_DCI_H_
#define	_USS820_DCI_H_

#define	USS820_MAX_DEVICES (USB_MIN_DEVICES + 1)

#define	USS820_EP_MAX 8			/* maximum number of endpoints */

#define	USS820_TXDAT 0x00		/* Transmit FIFO data */

#define	USS820_TXCNTL 0x01		/* Transmit FIFO byte count low */
#define	USS820_TXCNTL_MASK 0xFF

#define	USS820_TXCNTH 0x02		/* Transmit FIFO byte count high */
#define	USS820_TXCNTH_MASK 0x03
#define	USS820_TXCNTH_UNUSED 0xFC

#define	USS820_TXCON 0x03		/* USB transmit FIFO control */
#define	USS820_TXCON_REVRP 0x01
#define	USS820_TXCON_ADVRM 0x02
#define	USS820_TXCON_ATM 0x04		/* Automatic Transmit Management */
#define	USS820_TXCON_TXISO 0x08		/* Transmit Isochronous Data */
#define	USS820_TXCON_UNUSED 0x10
#define	USS820_TXCON_FFSZ_16_64 0x00
#define	USS820_TXCON_FFSZ_64_256 0x20
#define	USS820_TXCON_FFSZ_8_512 0x40
#define	USS820_TXCON_FFSZ_32_1024 0x60
#define	USS820_TXCON_FFSZ_MASK 0x60
#define	USS820_TXCON_TXCLR 0x80		/* Transmit FIFO clear */

#define	USS820_TXFLG 0x04		/* Transmit FIFO flag (Read Only) */
#define	USS820_TXFLG_TXOVF 0x01		/* TX overrun */
#define	USS820_TXFLG_TXURF 0x02		/* TX underrun */
#define	USS820_TXFLG_TXFULL 0x04	/* TX full */
#define	USS820_TXFLG_TXEMP 0x08		/* TX empty */
#define	USS820_TXFLG_UNUSED 0x30
#define	USS820_TXFLG_TXFIF0 0x40
#define	USS820_TXFLG_TXFIF1 0x80

#define	USS820_RXDAT 0x05		/* Receive FIFO data */

#define	USS820_RXCNTL 0x06		/* Receive FIFO byte count low */
#define	USS820_RXCNTL_MASK 0xFF

#define	USS820_RXCNTH 0x07		/* Receive FIFO byte count high */
#define	USS820_RXCNTH_MASK 0x03
#define	USS820_RXCNTH_UNUSED 0xFC

#define	USS820_RXCON 0x08		/* Receive FIFO control */
#define	USS820_RXCON_REVWP 0x01
#define	USS820_RXCON_ADVWM 0x02
#define	USS820_RXCON_ARM 0x04		/* Auto Receive Management */
#define	USS820_RXCON_RXISO 0x08		/* Receive Isochronous Data */
#define	USS820_RXCON_RXFFRC 0x10	/* FIFO Read Complete */
#define	USS820_RXCON_FFSZ_16_64 0x00
#define	USS820_RXCON_FFSZ_64_256 0x20
#define	USS820_RXCON_FFSZ_8_512 0x40
#define	USS820_RXCON_FFSZ_32_1024 0x60
#define	USS820_RXCON_RXCLR 0x80		/* Receive FIFO clear */

#define	USS820_RXFLG 0x09		/* Receive FIFO flag (Read Only) */
#define	USS820_RXFLG_RXOVF 0x01		/* RX overflow */
#define	USS820_RXFLG_RXURF 0x02		/* RX underflow */
#define	USS820_RXFLG_RXFULL 0x04	/* RX full */
#define	USS820_RXFLG_RXEMP 0x08		/* RX empty */
#define	USS820_RXFLG_RXFLUSH 0x10	/* RX flush */
#define	USS820_RXFLG_UNUSED 0x20
#define	USS820_RXFLG_RXFIF0 0x40
#define	USS820_RXFLG_RXFIF1 0x80

#define	USS820_EPINDEX 0x0a		/* Endpoint index selection */
#define	USS820_EPINDEX_MASK 0x07
#define	USS820_EPINDEX_UNUSED 0xF8

#define	USS820_EPCON 0x0b		/* Endpoint control */
#define	USS820_EPCON_TXEPEN 0x01	/* Transmit Endpoint Enable */
#define	USS820_EPCON_TXOE 0x02		/* Transmit Output Enable */
#define	USS820_EPCON_RXEPEN 0x04	/* Receive Endpoint Enable */
#define	USS820_EPCON_RXIE 0x08		/* Receive Input Enable */
#define	USS820_EPCON_RXSPM 0x10		/* Receive Single-Packet Mode */
#define	USS820_EPCON_CTLEP 0x20		/* Control Endpoint */
#define	USS820_EPCON_TXSTL 0x40		/* Stall Transmit Endpoint */
#define	USS820_EPCON_RXSTL 0x80		/* Stall Receive Endpoint */

#define	USS820_TXSTAT 0x0c		/* Transmit status */
#define	USS820_TXSTAT_TXACK 0x01	/* Transmit Acknowledge */
#define	USS820_TXSTAT_TXERR 0x02	/* Transmit Error */
#define	USS820_TXSTAT_TXVOID 0x04	/* Transmit Void */
#define	USS820_TXSTAT_TXSOVW 0x08	/* Transmit Data Sequence Overwrite
					 * Bit */
#define	USS820_TXSTAT_TXFLUSH 0x10	/* Transmit FIFO Packet Flushed */
#define	USS820_TXSTAT_TXNAKE 0x20	/* Transmit NAK Mode Enable */
#define	USS820_TXSTAT_TXDSAM 0x40	/* Transmit Data-Set-Available Mode */
#define	USS820_TXSTAT_TXSEQ 0x80	/* Transmitter Current Sequence Bit */

#define	USS820_RXSTAT 0x0d		/* Receive status */
#define	USS820_RXSTAT_RXACK 0x01	/* Receive Acknowledge */
#define	USS820_RXSTAT_RXERR 0x02	/* Receive Error */
#define	USS820_RXSTAT_RXVOID 0x04	/* Receive Void */
#define	USS820_RXSTAT_RXSOVW 0x08	/* Receive Data Sequence Overwrite Bit */
#define	USS820_RXSTAT_EDOVW 0x10	/* End Overwrite Flag */
#define	USS820_RXSTAT_STOVW 0x20	/* Start Overwrite Flag */
#define	USS820_RXSTAT_RXSETUP 0x40	/* Received SETUP token */
#define	USS820_RXSTAT_RXSEQ 0x80	/* Receiver Endpoint Sequence Bit */

#define	USS820_SOFL 0x0e		/* Start Of Frame counter low */
#define	USS820_SOFL_MASK 0xFF

#define	USS820_SOFH 0x0f		/* Start Of Frame counter high */
#define	USS820_SOFH_MASK 0x07
#define	USS820_SOFH_SOFDIS 0x08		/* SOF Pin Output Disable */
#define	USS820_SOFH_FTLOCK 0x10		/* Frame Timer Lock */
#define	USS820_SOFH_SOFIE 0x20		/* SOF Interrupt Enable */
#define	USS820_SOFH_ASOF 0x40		/* Any Start of Frame */
#define	USS820_SOFH_SOFACK 0x80		/* SOF Token Received Without Error */

#define	USS820_FADDR 0x10		/* Function Address */
#define	USS820_FADDR_MASK 0x7F
#define	USS820_FADDR_UNUSED 0x80

#define	USS820_SCR 0x11			/* System Control */
#define	USS820_SCR_UNUSED 0x01
#define	USS820_SCR_T_IRQ 0x02		/* Global Interrupt Enable */
#define	USS820_SCR_IRQLVL 0x04		/* Interrupt Mode */
#define	USS820_SCR_SRESET 0x08		/* Software reset */
#define	USS820_SCR_IE_RESET 0x10	/* Enable Reset Interrupt */
#define	USS820_SCR_IE_SUSP 0x20		/* Enable Suspend Interrupt */
#define	USS820_SCR_RWUPE 0x40		/* Enable Remote Wake-Up Feature */
#define	USS820_SCR_IRQPOL 0x80		/* IRQ polarity */

#define	USS820_SSR 0x12			/* System Status */
#define	USS820_SSR_RESET 0x01		/* Reset Condition Detected on USB
					 * cable */
#define	USS820_SSR_SUSPEND 0x02		/* Suspend Detected */
#define	USS820_SSR_RESUME 0x04		/* Resume Detected */
#define	USS820_SSR_SUSPDIS 0x08		/* Suspend Disable */
#define	USS820_SSR_SUSPPO 0x10		/* Suspend Power Off */
#define	USS820_SSR_UNUSED 0xE0

#define	USS820_UNK0 0x13		/* Unknown */
#define	USS820_UNK0_UNUSED 0xFF

#define	USS820_SBI 0x14			/* Serial bus interrupt low */
#define	USS820_SBI_FTXD0 0x01		/* Function Transmit Done, EP 0 */
#define	USS820_SBI_FRXD0 0x02		/* Function Receive Done, EP 0 */
#define	USS820_SBI_FTXD1 0x04
#define	USS820_SBI_FRXD1 0x08
#define	USS820_SBI_FTXD2 0x10
#define	USS820_SBI_FRXD2 0x20
#define	USS820_SBI_FTXD3 0x40
#define	USS820_SBI_FRXD3 0x80

#define	USS820_SBI1 0x15		/* Serial bus interrupt high */
#define	USS820_SBI1_FTXD4 0x01
#define	USS820_SBI1_FRXD4 0x02
#define	USS820_SBI1_FTXD5 0x04
#define	USS820_SBI1_FRXD5 0x08
#define	USS820_SBI1_FTXD6 0x10
#define	USS820_SBI1_FRXD6 0x20
#define	USS820_SBI1_FTXD7 0x40
#define	USS820_SBI1_FRXD7 0x80

#define	USS820_SBIE 0x16		/* Serial bus interrupt enable low */
#define	USS820_SBIE_FTXIE0 0x01
#define	USS820_SBIE_FRXIE0 0x02
#define	USS820_SBIE_FTXIE1 0x04
#define	USS820_SBIE_FRXIE1 0x08
#define	USS820_SBIE_FTXIE2 0x10
#define	USS820_SBIE_FRXIE2 0x20
#define	USS820_SBIE_FTXIE3 0x40
#define	USS820_SBIE_FRXIE3 0x80

#define	USS820_SBIE1 0x17		/* Serial bus interrupt enable high */
#define	USS820_SBIE1_FTXIE4 0x01
#define	USS820_SBIE1_FRXIE4 0x02
#define	USS820_SBIE1_FTXIE5 0x04
#define	USS820_SBIE1_FRXIE5 0x08
#define	USS820_SBIE1_FTXIE6 0x10
#define	USS820_SBIE1_FRXIE6 0x20
#define	USS820_SBIE1_FTXIE7 0x40
#define	USS820_SBIE1_FRXIE7 0x80

#define	USS820_REV 0x18			/* Hardware revision */
#define	USS820_REV_MIN 0x0F
#define	USS820_REV_MAJ 0xF0

#define	USS820_LOCK 0x19		/* Suspend power-off locking */
#define	USS820_LOCK_UNLOCKED 0x01
#define	USS820_LOCK_UNUSED 0xFE

#define	USS820_PEND 0x1a		/* Pend hardware status update */
#define	USS820_PEND_PEND 0x01
#define	USS820_PEND_UNUSED 0xFE

#define	USS820_SCRATCH 0x1b		/* Scratch firmware information */
#define	USS820_SCRATCH_MASK 0x7F
#define	USS820_SCRATCH_IE_RESUME 0x80	/* Enable Resume Interrupt */

#define	USS820_MCSR 0x1c		/* Miscellaneous control and status */
#define	USS820_MCSR_DPEN 0x01		/* DPLS Pull-Up Enable */
#define	USS820_MCSR_SUSPLOE 0x02	/* Suspend Lock Out Enable */
#define	USS820_MCSR_BDFEAT 0x04		/* Board Feature Enable */
#define	USS820_MCSR_FEAT 0x08		/* Feature Enable */
#define	USS820_MCSR_PKGID 0x10		/* Package Identification */
#define	USS820_MCSR_SUSPS 0x20		/* Suspend Status */
#define	USS820_MCSR_INIT 0x40		/* Device Initialized */
#define	USS820_MCSR_RWUPR 0x80		/* Remote Wakeup-Up Remember */

#define	USS820_DSAV 0x1d		/* Data set available low (Read Only) */
#define	USS820_DSAV_TXAV0 0x01
#define	USS820_DSAV_RXAV0 0x02
#define	USS820_DSAV_TXAV1 0x04
#define	USS820_DSAV_RXAV1 0x08
#define	USS820_DSAV_TXAV2 0x10
#define	USS820_DSAV_RXAV2 0x20
#define	USS820_DSAV_TXAV3 0x40
#define	USS820_DSAV_RXAV3 0x80

#define	USS820_DSAV1 0x1e		/* Data set available high */
#define	USS820_DSAV1_TXAV4 0x01
#define	USS820_DSAV1_RXAV4 0x02
#define	USS820_DSAV1_TXAV5 0x04
#define	USS820_DSAV1_RXAV5 0x08
#define	USS820_DSAV1_TXAV6 0x10
#define	USS820_DSAV1_RXAV6 0x20
#define	USS820_DSAV1_TXAV7 0x40
#define	USS820_DSAV1_RXAV7 0x80

#define	USS820_UNK1 0x1f		/* Unknown */
#define	USS820_UNK1_UNKNOWN 0xFF

#ifndef USS820_REG_STRIDE
#define	USS820_REG_STRIDE 1
#endif

#define	USS820_READ_1(sc, reg) \
  bus_space_read_1((sc)->sc_io_tag, (sc)->sc_io_hdl, (reg) * USS820_REG_STRIDE)

#define	USS820_WRITE_1(sc, reg, data)	\
  bus_space_write_1((sc)->sc_io_tag, (sc)->sc_io_hdl, (reg) * USS820_REG_STRIDE, (data))

struct uss820dci_td;
struct uss820dci_softc;

typedef uint8_t (uss820dci_cmd_t)(struct uss820dci_softc *, struct uss820dci_td *td);

struct uss820dci_td {
	struct uss820dci_td *obj_next;
	uss820dci_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t remainder;
	uint16_t max_packet_size;
	uint8_t	ep_index;
	uint8_t	error:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	support_multi_buffer:1;
	uint8_t	did_stall:1;
	uint8_t	did_enable:1;
};

struct uss820_std_temp {
	uss820dci_cmd_t *func;
	struct usb_page_cache *pc;
	struct uss820dci_td *td;
	struct uss820dci_td *td_next;
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

struct uss820dci_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union uss820_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct uss820_flags {
	uint8_t	change_connect:1;
	uint8_t	change_suspend:1;
	uint8_t	status_suspend:1;	/* set if suspended */
	uint8_t	status_vbus:1;		/* set if present */
	uint8_t	status_bus_reset:1;	/* set if reset complete */
	uint8_t	clocks_off:1;
	uint8_t	port_powered:1;
	uint8_t	port_enabled:1;
	uint8_t	d_pulled_up:1;
	uint8_t	mcsr_feat:1;
};

struct uss820dci_softc {
	struct usb_bus sc_bus;
	union uss820_hub_temp sc_hub_temp;

	struct usb_device *sc_devices[USS820_MAX_DEVICES];
	struct resource *sc_io_res;
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	bus_size_t sc_io_size;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	uint32_t sc_xfer_complete;

	uint8_t	sc_rt_addr;		/* root HUB address */
	uint8_t	sc_dv_addr;		/* device address */
	uint8_t	sc_conf;		/* root HUB config */

	uint8_t	sc_hub_idata[1];

	struct uss820_flags sc_flags;
};

/* prototypes */

usb_error_t uss820dci_init(struct uss820dci_softc *sc);
void	uss820dci_uninit(struct uss820dci_softc *sc);
driver_filter_t uss820dci_filter_interrupt;
driver_intr_t uss820dci_interrupt;

#endif					/* _USS820_DCI_H_ */
