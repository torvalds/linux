/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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
 * This header file defines the registers of the Mentor Graphics USB OnTheGo
 * Inventra chip.
 */

#ifndef _MUSB2_OTG_H_
#define	_MUSB2_OTG_H_

#define	MUSB2_MAX_DEVICES USB_MAX_DEVICES

/* Common registers */

#define	MUSB2_REG_FADDR 0x0000		/* function address register */
#define	MUSB2_MASK_FADDR 0x7F

#define	MUSB2_REG_POWER 0x0001		/* power register */
#define	MUSB2_MASK_SUSPM_ENA 0x01
#define	MUSB2_MASK_SUSPMODE 0x02
#define	MUSB2_MASK_RESUME 0x04
#define	MUSB2_MASK_RESET 0x08
#define	MUSB2_MASK_HSMODE 0x10
#define	MUSB2_MASK_HSENAB 0x20
#define	MUSB2_MASK_SOFTC 0x40
#define	MUSB2_MASK_ISOUPD 0x80

/* Endpoint interrupt handling */

#define	MUSB2_REG_INTTX 0x0002		/* transmit interrupt register */
#define	MUSB2_REG_INTRX 0x0004		/* receive interrupt register */
#define	MUSB2_REG_INTTXE 0x0006		/* transmit interrupt enable register */
#define	MUSB2_REG_INTRXE 0x0008		/* receive interrupt enable register */
#define	MUSB2_MASK_EPINT(epn) (1 << (epn))	/* epn = [0..15] */

/* Common interrupt handling */

#define	MUSB2_REG_INTUSB 0x000A		/* USB interrupt register */
#define	MUSB2_MASK_ISUSP 0x01
#define	MUSB2_MASK_IRESUME 0x02
#define	MUSB2_MASK_IRESET 0x04
#define	MUSB2_MASK_IBABBLE 0x04
#define	MUSB2_MASK_ISOF 0x08
#define	MUSB2_MASK_ICONN 0x10
#define	MUSB2_MASK_IDISC 0x20
#define	MUSB2_MASK_ISESSRQ 0x40
#define	MUSB2_MASK_IVBUSERR 0x80

#define	MUSB2_REG_INTUSBE 0x000B	/* USB interrupt enable register */
#define	MUSB2_REG_FRAME 0x000C		/* USB frame register */
#define	MUSB2_MASK_FRAME 0x3FF		/* 0..1023 */

#define	MUSB2_REG_EPINDEX 0x000E	/* endpoint index register */
#define	MUSB2_MASK_EPINDEX 0x0F

#define	MUSB2_REG_TESTMODE 0x000F	/* test mode register */
#define	MUSB2_MASK_TSE0_NAK 0x01
#define	MUSB2_MASK_TJ 0x02
#define	MUSB2_MASK_TK 0x04
#define	MUSB2_MASK_TPACKET 0x08
#define	MUSB2_MASK_TFORCE_HS 0x10
#define	MUSB2_MASK_TFORCE_LS 0x20
#define	MUSB2_MASK_TFIFO_ACC 0x40
#define	MUSB2_MASK_TFORCE_HC 0x80

#define	MUSB2_REG_INDEXED_CSR 0x0010	/* EP control status register offset */

#define	MUSB2_REG_TXMAXP (0x0000 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_REG_RXMAXP (0x0004 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_PKTSIZE 0x03FF	/* in bytes, should be even */
#define	MUSB2_MASK_PKTMULT 0xFC00	/* HS packet multiplier: 0..2 */

#define	MUSB2_REG_TXCSRL (0x0002 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_CSRL_TXPKTRDY 0x01
#define	MUSB2_MASK_CSRL_TXFIFONEMPTY 0x02
#define	MUSB2_MASK_CSRL_TXUNDERRUN 0x04	/* Device Mode */
#define	MUSB2_MASK_CSRL_TXERROR 0x04	/* Host Mode */
#define	MUSB2_MASK_CSRL_TXFFLUSH 0x08
#define	MUSB2_MASK_CSRL_TXSENDSTALL 0x10/* Device Mode */
#define	MUSB2_MASK_CSRL_TXSETUPPKT 0x10	/* Host Mode */
#define	MUSB2_MASK_CSRL_TXSENTSTALL 0x20/* Device Mode */
#define	MUSB2_MASK_CSRL_TXSTALLED 0x20	/* Host Mode */
#define	MUSB2_MASK_CSRL_TXDT_CLR 0x40
#define	MUSB2_MASK_CSRL_TXINCOMP 0x80 /* Device mode */
#define	MUSB2_MASK_CSRL_TXNAKTO 0x80 /* Host mode */

/* Device Side Mode */
#define	MUSB2_MASK_CSR0L_RXPKTRDY 0x01
#define	MUSB2_MASK_CSR0L_TXPKTRDY 0x02
#define	MUSB2_MASK_CSR0L_SENTSTALL 0x04
#define	MUSB2_MASK_CSR0L_DATAEND 0x08
#define	MUSB2_MASK_CSR0L_SETUPEND 0x10
#define	MUSB2_MASK_CSR0L_SENDSTALL 0x20
#define	MUSB2_MASK_CSR0L_RXPKTRDY_CLR 0x40
#define	MUSB2_MASK_CSR0L_SETUPEND_CLR 0x80

/* Host Side Mode */
#define	MUSB2_MASK_CSR0L_TXFIFONEMPTY 0x02
#define	MUSB2_MASK_CSR0L_RXSTALL 0x04
#define	MUSB2_MASK_CSR0L_SETUPPKT 0x08
#define	MUSB2_MASK_CSR0L_ERROR 0x10
#define	MUSB2_MASK_CSR0L_REQPKT 0x20
#define	MUSB2_MASK_CSR0L_STATUSPKT 0x40
#define	MUSB2_MASK_CSR0L_NAKTIMO 0x80

#define	MUSB2_REG_TXCSRH (0x0003 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_CSRH_TXDT_VAL 0x01	/* Host Mode */
#define	MUSB2_MASK_CSRH_TXDT_WREN 0x02	/* Host Mode */
#define	MUSB2_MASK_CSRH_TXDMAREQMODE 0x04
#define	MUSB2_MASK_CSRH_TXDT_SWITCH 0x08
#define	MUSB2_MASK_CSRH_TXDMAREQENA 0x10
#define	MUSB2_MASK_CSRH_RXMODE 0x00
#define	MUSB2_MASK_CSRH_TXMODE 0x20
#define	MUSB2_MASK_CSRH_TXISO 0x40	/* Device Mode */
#define	MUSB2_MASK_CSRH_TXAUTOSET 0x80

#define	MUSB2_MASK_CSR0H_FFLUSH 0x01	/* Device Side flush FIFO */
#define	MUSB2_MASK_CSR0H_DT 0x02	/* Host Side data toggle */
#define	MUSB2_MASK_CSR0H_DT_WREN 0x04	/* Host Side */
#define	MUSB2_MASK_CSR0H_PING_DIS 0x08	/* Host Side */

#define	MUSB2_REG_RXCSRL (0x0006 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_CSRL_RXPKTRDY 0x01
#define	MUSB2_MASK_CSRL_RXFIFOFULL 0x02
#define	MUSB2_MASK_CSRL_RXOVERRUN 0x04 /* Device Mode */
#define	MUSB2_MASK_CSRL_RXERROR 0x04 /* Host Mode */
#define	MUSB2_MASK_CSRL_RXDATAERR 0x08 /* Device Mode */
#define	MUSB2_MASK_CSRL_RXNAKTO 0x08 /* Host Mode */
#define	MUSB2_MASK_CSRL_RXFFLUSH 0x10
#define	MUSB2_MASK_CSRL_RXSENDSTALL 0x20/* Device Mode */
#define	MUSB2_MASK_CSRL_RXREQPKT 0x20	/* Host Mode */
#define	MUSB2_MASK_CSRL_RXSENTSTALL 0x40/* Device Mode */
#define	MUSB2_MASK_CSRL_RXSTALL 0x40	/* Host Mode */
#define	MUSB2_MASK_CSRL_RXDT_CLR 0x80

#define	MUSB2_REG_RXCSRH (0x0007 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_CSRH_RXINCOMP 0x01
#define	MUSB2_MASK_CSRH_RXDT_VAL 0x02	/* Host Mode */
#define	MUSB2_MASK_CSRH_RXDT_WREN 0x04	/* Host Mode */
#define	MUSB2_MASK_CSRH_RXDMAREQMODE 0x08
#define	MUSB2_MASK_CSRH_RXNYET 0x10
#define	MUSB2_MASK_CSRH_RXDMAREQENA 0x20
#define	MUSB2_MASK_CSRH_RXISO 0x40	/* Device Mode */
#define	MUSB2_MASK_CSRH_RXAUTOREQ 0x40	/* Host Mode */
#define	MUSB2_MASK_CSRH_RXAUTOCLEAR 0x80

#define	MUSB2_REG_RXCOUNT (0x0008 + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_RXCOUNT 0xFFFF

#define	MUSB2_REG_TXTI (0x000A + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_REG_RXTI (0x000C + MUSB2_REG_INDEXED_CSR)

/* Host Mode */
#define	MUSB2_MASK_TI_SPEED 0xC0
#define	MUSB2_MASK_TI_SPEED_LO 0xC0
#define	MUSB2_MASK_TI_SPEED_FS 0x80
#define	MUSB2_MASK_TI_SPEED_HS 0x40
#define	MUSB2_MASK_TI_PROTO_CTRL 0x00
#define	MUSB2_MASK_TI_PROTO_ISOC 0x10
#define	MUSB2_MASK_TI_PROTO_BULK 0x20
#define	MUSB2_MASK_TI_PROTO_INTR 0x30
#define	MUSB2_MASK_TI_EP_NUM 0x0F

#define	MUSB2_REG_TXNAKLIMIT (0x000B /* EPN=0 */ + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_REG_RXNAKLIMIT (0x000D /* EPN=0 */ + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_NAKLIMIT 0xFF

#define	MUSB2_REG_FSIZE (0x000F + MUSB2_REG_INDEXED_CSR)
#define	MUSB2_MASK_RX_FSIZE 0xF0	/* 3..13, 2**n bytes */
#define	MUSB2_MASK_TX_FSIZE 0x0F	/* 3..13, 2**n bytes */

#define	MUSB2_REG_EPFIFO(n) (0x0020 + (4*(n)))

#define	MUSB2_REG_CONFDATA (0x000F + MUSB2_REG_INDEXED_CSR)	/* EPN=0 */
#define	MUSB2_MASK_CD_UTMI_DW 0x01
#define	MUSB2_MASK_CD_SOFTCONE 0x02
#define	MUSB2_MASK_CD_DYNFIFOSZ 0x04
#define	MUSB2_MASK_CD_HBTXE 0x08
#define	MUSB2_MASK_CD_HBRXE 0x10
#define	MUSB2_MASK_CD_BIGEND 0x20
#define	MUSB2_MASK_CD_MPTXE 0x40
#define	MUSB2_MASK_CD_MPRXE 0x80

/* Various registers */

#define	MUSB2_REG_DEVCTL 0x0060
#define	MUSB2_MASK_SESS 0x01
#define	MUSB2_MASK_HOSTREQ 0x02
#define	MUSB2_MASK_HOSTMD 0x04
#define	MUSB2_MASK_VBUS0 0x08
#define	MUSB2_MASK_VBUS1 0x10
#define	MUSB2_MASK_LSDEV 0x20
#define	MUSB2_MASK_FSDEV 0x40
#define	MUSB2_MASK_BDEV 0x80

#define	MUSB2_REG_MISC 0x0061
#define	MUSB2_MASK_RXEDMA 0x01
#define	MUSB2_MASK_TXEDMA 0x02

#define	MUSB2_REG_TXFIFOSZ 0x0062
#define	MUSB2_REG_RXFIFOSZ 0x0063
#define	MUSB2_MASK_FIFODB 0x10		/* set if double buffering, r/w */
#define	MUSB2_MASK_FIFOSZ 0x0F
#define	MUSB2_VAL_FIFOSZ_8 0
#define	MUSB2_VAL_FIFOSZ_16 1
#define	MUSB2_VAL_FIFOSZ_32 2
#define	MUSB2_VAL_FIFOSZ_64 3
#define	MUSB2_VAL_FIFOSZ_128 4
#define	MUSB2_VAL_FIFOSZ_256 5
#define	MUSB2_VAL_FIFOSZ_512 6
#define	MUSB2_VAL_FIFOSZ_1024 7
#define	MUSB2_VAL_FIFOSZ_2048 8
#define	MUSB2_VAL_FIFOSZ_4096 9

#define	MUSB2_REG_TXFIFOADD 0x0064
#define	MUSB2_REG_RXFIFOADD 0x0066
#define	MUSB2_MASK_FIFOADD 0xFFF	/* unit is 8-bytes */

#define	MUSB2_REG_VSTATUS 0x0068
#define	MUSB2_REG_VCONTROL 0x0068
#define	MUSB2_REG_HWVERS 0x006C
#define	MUSB2_REG_ULPI_BASE 0x0070

#define	MUSB2_REG_EPINFO 0x0078
#define	MUSB2_MASK_NRXEP 0xF0
#define	MUSB2_MASK_NTXEP 0x0F

#define	MUSB2_REG_RAMINFO 0x0079
#define	MUSB2_REG_LINKINFO 0x007A

#define	MUSB2_REG_VPLEN 0x007B
#define	MUSB2_MASK_VPLEN 0xFF

#define	MUSB2_REG_HS_EOF1 0x007C
#define	MUSB2_REG_FS_EOF1 0x007D
#define	MUSB2_REG_LS_EOF1 0x007E
#define	MUSB2_REG_SOFT_RST 0x007F
#define	MUSB2_MASK_SRST 0x01
#define	MUSB2_MASK_SRSTX 0x02

#define	MUSB2_REG_RQPKTCOUNT(n) (0x0300 + (4*(n))
#define	MUSB2_REG_RXDBDIS 0x0340
#define	MUSB2_REG_TXDBDIS 0x0342
#define	MUSB2_MASK_DB(n) (1 << (n))	/* disable double buffer, n = [0..15] */

#define	MUSB2_REG_CHIRPTO 0x0344
#define	MUSB2_REG_HSRESUM 0x0346

/* Host Mode only registers */

#define	MUSB2_REG_TXFADDR(n) (0x0080 + (8*(n)))
#define	MUSB2_REG_TXHADDR(n) (0x0082 + (8*(n)))
#define	MUSB2_REG_TXHUBPORT(n) (0x0083 + (8*(n)))
#define	MUSB2_REG_RXFADDR(n) (0x0084 + (8*(n)))
#define	MUSB2_REG_RXHADDR(n) (0x0086 + (8*(n)))
#define	MUSB2_REG_RXHUBPORT(n) (0x0087 + (8*(n)))

#define	MUSB2_EP_MAX 16			/* maximum number of endpoints */

#define	MUSB2_DEVICE_MODE	0
#define	MUSB2_HOST_MODE		1

#define	MUSB2_READ_2(sc, reg) \
  bus_space_read_2((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)

#define	MUSB2_WRITE_2(sc, reg, data)	\
  bus_space_write_2((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)

#define	MUSB2_READ_1(sc, reg) \
  bus_space_read_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg)

#define	MUSB2_WRITE_1(sc, reg, data)	\
  bus_space_write_1((sc)->sc_io_tag, (sc)->sc_io_hdl, reg, data)

struct musbotg_td;
struct musbotg_softc;

typedef uint8_t (musbotg_cmd_t)(struct musbotg_td *td);

struct musbotg_dma {
	struct musbotg_softc *sc;
	uint32_t dma_chan;
	uint8_t	busy:1;
	uint8_t	complete:1;
	uint8_t	error:1;
};

struct musbotg_td {
	struct musbotg_td *obj_next;
	musbotg_cmd_t *func;
	struct usb_page_cache *pc;
	uint32_t offset;
	uint32_t remainder;
	uint16_t max_frame_size;	/* packet_size * mult */
	uint16_t reg_max_packet;
	uint8_t	ep_no;
	uint8_t	transfer_type;
	uint8_t	error:1;
	uint8_t	alt_next:1;
	uint8_t	short_pkt:1;
	uint8_t	support_multi_buffer:1;
	uint8_t	did_stall:1;
	uint8_t	dma_enabled:1;
	uint8_t	transaction_started:1;
	uint8_t dev_addr;
	uint8_t toggle;
	int8_t channel;
	uint8_t haddr;
	uint8_t hport;
};

struct musbotg_std_temp {
	musbotg_cmd_t *func;
	struct usb_page_cache *pc;
	struct musbotg_td *td;
	struct musbotg_td *td_next;
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
	uint8_t dev_addr;
	int8_t channel;
	uint8_t haddr;
	uint8_t hport;
	uint8_t	transfer_type;
};

struct musbotg_config_desc {
	struct usb_config_descriptor confd;
	struct usb_interface_descriptor ifcd;
	struct usb_endpoint_descriptor endpd;
} __packed;

union musbotg_hub_temp {
	uWord	wValue;
	struct usb_port_status ps;
};

struct musbotg_flags {
	uint8_t	change_connect:1;
	uint8_t	change_suspend:1;
	uint8_t	change_reset:1;
	uint8_t	change_over_current:1;
	uint8_t	change_enabled:1;
	uint8_t	status_suspend:1;	/* set if suspended */
	uint8_t	status_vbus:1;		/* set if present */
	uint8_t	status_bus_reset:1;	/* set if reset complete */
	uint8_t	status_high_speed:1;	/* set if High Speed is selected */
	uint8_t	remote_wakeup:1;
	uint8_t	self_powered:1;
	uint8_t	clocks_off:1;
	uint8_t	port_powered:1;
	uint8_t	port_enabled:1;
	uint8_t	port_over_current:1;
	uint8_t	d_pulled_up:1;
};

struct musb_otg_ep_cfg {
	int ep_end;
	int ep_fifosz_shift;
	uint8_t ep_fifosz_reg;
};

struct musbotg_softc {
	struct usb_bus sc_bus;
	union musbotg_hub_temp sc_hub_temp;
	struct usb_hw_ep_profile sc_hw_ep_profile[MUSB2_EP_MAX];

	struct usb_device *sc_devices[MUSB2_MAX_DEVICES];
	struct resource *sc_io_res;
	struct resource *sc_irq_res;
	void   *sc_intr_hdl;
	bus_size_t sc_io_size;
	bus_space_tag_t sc_io_tag;
	bus_space_handle_t sc_io_hdl;

	void    (*sc_clocks_on) (void *arg);
	void    (*sc_clocks_off) (void *arg);
	void    (*sc_ep_int_set) (struct musbotg_softc *sc, int ep, int on);
	void   *sc_clocks_arg;

	uint32_t sc_bounce_buf[(1024 * 3) / 4];	/* bounce buffer */

	uint8_t	sc_ep_max;		/* maximum number of RX and TX
					 * endpoints supported */
	uint8_t	sc_rt_addr;		/* root HUB address */
	uint8_t	sc_dv_addr;		/* device address */
	uint8_t	sc_conf;		/* root HUB config */
	uint8_t	sc_ep0_busy;		/* set if ep0 is busy */
	uint8_t	sc_ep0_cmd;		/* pending commands */
	uint8_t	sc_conf_data;		/* copy of hardware register */

	uint8_t	sc_hub_idata[1];
	uint16_t sc_channel_mask;	/* 16 endpoints */

	struct musbotg_flags sc_flags;
	uint8_t	sc_id;
	uint8_t	sc_mode;
	void *sc_platform_data;
	const struct musb_otg_ep_cfg *sc_ep_cfg;
};

/* prototypes */

usb_error_t musbotg_init(struct musbotg_softc *sc);
void	musbotg_uninit(struct musbotg_softc *sc);
void	musbotg_interrupt(struct musbotg_softc *sc,
    uint16_t rxstat, uint16_t txstat, uint8_t stat);
void	musbotg_vbus_interrupt(struct musbotg_softc *sc, uint8_t is_on);
void	musbotg_connect_interrupt(struct musbotg_softc *sc);

#endif					/* _MUSB2_OTG_H_ */
