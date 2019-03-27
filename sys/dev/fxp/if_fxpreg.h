/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1995, David Greenman
 * Copyright (c) 2001 Jonathan Lemon <jlemon@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 *
 * $FreeBSD$
 */

#define FXP_PCI_MMBA	0x10
#define FXP_PCI_IOBA	0x14

/*
 * Control/status registers.
 */
#define	FXP_CSR_SCB_RUSCUS	0	/* scb_rus/scb_cus (1 byte) */
#define	FXP_CSR_SCB_STATACK	1	/* scb_statack (1 byte) */
#define	FXP_CSR_SCB_COMMAND	2	/* scb_command (1 byte) */
#define	FXP_CSR_SCB_INTRCNTL	3	/* scb_intrcntl (1 byte) */
#define	FXP_CSR_SCB_GENERAL	4	/* scb_general (4 bytes) */
#define	FXP_CSR_PORT		8	/* port (4 bytes) */
#define	FXP_CSR_FLASHCONTROL	12	/* flash control (2 bytes) */
#define	FXP_CSR_EEPROMCONTROL	14	/* eeprom control (2 bytes) */
#define	FXP_CSR_MDICONTROL	16	/* mdi control (4 bytes) */
#define	FXP_CSR_FC_THRESH	0x19	/* flow control (1 byte) */
#define	FXP_CSR_FC_STATUS	0x1A	/* flow control status (1 byte) */
#define	FXP_CSR_PMDR		0x1B	/* power management driver (1 byte) */
#define	FXP_CSR_GENCONTROL	0x1C	/* general control (1 byte) */

/*
 * FOR REFERENCE ONLY, the old definition of FXP_CSR_SCB_RUSCUS:
 *
 *	volatile uint8_t	:2,
 *				scb_rus:4,
 *				scb_cus:2;
 */

#define FXP_PORT_SOFTWARE_RESET		0
#define FXP_PORT_SELFTEST		1
#define FXP_PORT_SELECTIVE_RESET	2
#define FXP_PORT_DUMP			3

#define FXP_SCB_RUS_IDLE		0
#define FXP_SCB_RUS_SUSPENDED		1
#define FXP_SCB_RUS_NORESOURCES		2
#define FXP_SCB_RUS_READY		4
#define FXP_SCB_RUS_SUSP_NORBDS		9
#define FXP_SCB_RUS_NORES_NORBDS	10
#define FXP_SCB_RUS_READY_NORBDS	12

#define FXP_SCB_CUS_IDLE		0
#define FXP_SCB_CUS_SUSPENDED		1
#define FXP_SCB_CUS_ACTIVE		2

#define FXP_SCB_INTR_DISABLE		0x01	/* Disable all interrupts */
#define FXP_SCB_INTR_SWI		0x02	/* Generate SWI */
#define FXP_SCB_INTMASK_FCP		0x04
#define FXP_SCB_INTMASK_ER		0x08
#define FXP_SCB_INTMASK_RNR		0x10
#define FXP_SCB_INTMASK_CNA		0x20
#define FXP_SCB_INTMASK_FR		0x40
#define FXP_SCB_INTMASK_CXTNO		0x80

#define FXP_SCB_STATACK_FCP		0x01	/* Flow Control Pause */
#define FXP_SCB_STATACK_ER		0x02	/* Early Receive */
#define FXP_SCB_STATACK_SWI		0x04
#define FXP_SCB_STATACK_MDI		0x08
#define FXP_SCB_STATACK_RNR		0x10
#define FXP_SCB_STATACK_CNA		0x20
#define FXP_SCB_STATACK_FR		0x40
#define FXP_SCB_STATACK_CXTNO		0x80

#define FXP_SCB_COMMAND_CU_NOP		0x00
#define FXP_SCB_COMMAND_CU_START	0x10
#define FXP_SCB_COMMAND_CU_RESUME	0x20
#define FXP_SCB_COMMAND_CU_DUMP_ADR	0x40
#define FXP_SCB_COMMAND_CU_DUMP		0x50
#define FXP_SCB_COMMAND_CU_BASE		0x60
#define FXP_SCB_COMMAND_CU_DUMPRESET	0x70

#define FXP_SCB_COMMAND_RU_NOP		0
#define FXP_SCB_COMMAND_RU_START	1
#define FXP_SCB_COMMAND_RU_RESUME	2
#define FXP_SCB_COMMAND_RU_ABORT	4
#define FXP_SCB_COMMAND_RU_LOADHDS	5
#define FXP_SCB_COMMAND_RU_BASE		6
#define FXP_SCB_COMMAND_RU_RBDRESUME	7

/*
 * Command block definitions
 */
struct fxp_cb_nop {
	uint16_t cb_status;
	uint16_t cb_command;
	uint32_t link_addr;
};
struct fxp_cb_ias {
	uint16_t cb_status;
	uint16_t cb_command;
	uint32_t link_addr;
	uint8_t macaddr[6];
};

/* I hate bit-fields :-( */
#if BYTE_ORDER == LITTLE_ENDIAN
#define __FXP_BITFIELD2(a, b)			a, b
#define __FXP_BITFIELD3(a, b, c)		a, b, c
#define __FXP_BITFIELD4(a, b, c, d)		a, b, c, d
#define __FXP_BITFIELD5(a, b, c, d, e)		a, b, c, d, e
#define __FXP_BITFIELD6(a, b, c, d, e, f)	a, b, c, d, e, f
#define __FXP_BITFIELD7(a, b, c, d, e, f, g)	a, b, c, d, e, f, g
#define __FXP_BITFIELD8(a, b, c, d, e, f, g, h)	a, b, c, d, e, f, g, h
#else
#define __FXP_BITFIELD2(a, b)			b, a
#define __FXP_BITFIELD3(a, b, c)		c, b, a
#define __FXP_BITFIELD4(a, b, c, d)		d, c, b, a
#define __FXP_BITFIELD5(a, b, c, d, e)		e, d, c, b, a
#define __FXP_BITFIELD6(a, b, c, d, e, f)	f, e, d, c, b, a
#define __FXP_BITFIELD7(a, b, c, d, e, f, g)	g, f, e, d, c, b, a
#define __FXP_BITFIELD8(a, b, c, d, e, f, g, h)	h, g, f, e, d, c, b, a
#endif

struct fxp_cb_config {
	uint16_t	cb_status;
	uint16_t	cb_command;
	uint32_t	link_addr;

	/* Bytes 0 - 21 -- common to all i8255x */
	u_int		__FXP_BITFIELD2(byte_count:6, :2);
	u_int		__FXP_BITFIELD3(rx_fifo_limit:4, tx_fifo_limit:3, :1);
	uint8_t		adaptive_ifs;
	u_int		__FXP_BITFIELD5(mwi_enable:1,		/* 8,9 */
			    type_enable:1,			/* 8,9 */
			    read_align_en:1,			/* 8,9 */
			    end_wr_on_cl:1,			/* 8,9 */
			    :4);
	u_int		__FXP_BITFIELD2(rx_dma_bytecount:7, :1);
	u_int		__FXP_BITFIELD2(tx_dma_bytecount:7, dma_mbce:1);
	u_int		__FXP_BITFIELD8(late_scb:1,		/* 7 */
			    direct_dma_dis:1,			/* 8,9 */
			    tno_int_or_tco_en:1,		/* 7,9 */
			    ci_int:1,
			    ext_txcb_dis:1,			/* 8,9 */
			    ext_stats_dis:1,			/* 8,9 */
			    keep_overrun_rx:1,
			    save_bf:1);
	u_int		__FXP_BITFIELD6(disc_short_rx:1,
			    underrun_retry:2,
			    :2,
			    ext_rfa:1,				/* 550 */
			    two_frames:1,			/* 8,9 */
			    dyn_tbd:1);				/* 8,9 */
	u_int		__FXP_BITFIELD3(mediatype:1,		/* 7 */
			    :6,
			    csma_dis:1);			/* 8,9 */
	u_int		__FXP_BITFIELD6(tcp_udp_cksum:1,	/* 9 */
			    :3,
			    vlan_tco:1,				/* 8,9 */
			    link_wake_en:1,			/* 8,9 */
			    arp_wake_en:1,			/* 8 */
			    mc_wake_en:1);			/* 8 */
	u_int		__FXP_BITFIELD4(:3,
			    nsai:1,
			    preamble_length:2,
			    loopback:2);
	u_int		__FXP_BITFIELD2(linear_priority:3,	/* 7 */
			    :5);
	u_int		__FXP_BITFIELD3(linear_pri_mode:1,	/* 7 */
			    :3,
			    interfrm_spacing:4);
	u_int		:8;
	u_int		:8;
	u_int		__FXP_BITFIELD8(promiscuous:1,
			    bcast_disable:1,
			    wait_after_win:1,			/* 8,9 */
			    :1,
			    ignore_ul:1,			/* 8,9 */
			    crc16_en:1,				/* 9 */
			    :1,
			    crscdt:1);
	u_int		fc_delay_lsb:8;				/* 8,9 */
	u_int		fc_delay_msb:8;				/* 8,9 */
	u_int		__FXP_BITFIELD6(stripping:1,
			    padding:1,
			    rcv_crc_xfer:1,
			    long_rx_en:1,			/* 8,9 */
			    pri_fc_thresh:3,			/* 8,9 */
			    :1);
	u_int		__FXP_BITFIELD8(ia_wake_en:1,		/* 8 */
			    magic_pkt_dis:1,			/* 8,9,!9ER */
			    tx_fc_dis:1,			/* 8,9 */
			    rx_fc_restop:1,			/* 8,9 */
			    rx_fc_restart:1,			/* 8,9 */
			    fc_filter:1,			/* 8,9 */
			    force_fdx:1,
			    fdx_pin_en:1);
	u_int		__FXP_BITFIELD4(:5,
			    pri_fc_loc:1,			/* 8,9 */
			    multi_ia:1,
			    :1);
	u_int		__FXP_BITFIELD3(:3, mc_all:1, :4);

	/* Bytes 22 - 31 -- i82550 only */
	u_int		__FXP_BITFIELD3(gamla_rx:1,
			    vlan_strip_en:1,
			    :6);
	uint8_t		pad[9];
};

#define MAXMCADDR 80
struct fxp_cb_mcs {
	uint16_t cb_status;
	uint16_t cb_command;
	uint32_t link_addr;
	uint16_t mc_cnt;
	uint8_t mc_addr[MAXMCADDR][6];
};

#define MAXUCODESIZE 192
struct fxp_cb_ucode {
	uint16_t cb_status;
	uint16_t cb_command;
	uint32_t link_addr;
	uint32_t ucode[MAXUCODESIZE];
};

/*
 * Number of DMA segments in a TxCB.
 */
#define FXP_NTXSEG	35

struct fxp_tbd {
	uint32_t tb_addr;
	uint32_t tb_size;
};

struct fxp_ipcb {
	/*
	 * The following fields are valid only when
	 * using the IPCB command block for TX checksum offload
	 * (and TCP large send, VLANs, and (I think) IPsec). To use
	 * them, you must enable extended TxCBs (available only
	 * on the 82559 and later) and use the IPCBXMIT command.
	 * Note that Intel defines the IPCB to be 32 bytes long,
	 * the last 8 bytes of which comprise the first entry
	 * in the TBD array (see note below). This means we only
	 * have to define 8 extra bytes here.
         */
	uint16_t ipcb_schedule_low;
	uint8_t ipcb_ip_schedule;
	uint8_t ipcb_ip_activation_high;
	uint16_t ipcb_vlan_id;
	uint8_t ipcb_ip_header_offset;
	uint8_t ipcb_tcp_header_offset;
};

struct fxp_cb_tx {
	uint16_t cb_status;
	uint16_t cb_command;
	uint32_t link_addr;
	union {
		struct {
			uint32_t tbd_array_addr;
			uint16_t byte_count;
			uint8_t tx_threshold;
			uint8_t tbd_number;
		};
		struct fxp_tbd tbdtso;
	};

	/*
	 * The following structure isn't actually part of the TxCB,
	 * unless the extended TxCB feature is being used.  In this
	 * case, the first two elements of the structure below are
	 * fetched along with the TxCB.
	 */
	union {
		struct fxp_ipcb ipcb;
		struct fxp_tbd tbd[FXP_NTXSEG + 1];
	} tx_cb_u;
};

#define tbd			tx_cb_u.tbd
#define ipcb_schedule_low	tx_cb_u.ipcb.ipcb_schedule_low
#define ipcb_ip_schedule	tx_cb_u.ipcb.ipcb_ip_schedule
#define ipcb_ip_activation_high tx_cb_u.ipcb.ipcb_ip_activation_high
#define ipcb_vlan_id		tx_cb_u.ipcb.ipcb_vlan_id
#define ipcb_ip_header_offset	tx_cb_u.ipcb.ipcb_ip_header_offset
#define ipcb_tcp_header_offset	tx_cb_u.ipcb.ipcb_tcp_header_offset

/*
 * IPCB field definitions
 */
#define FXP_IPCB_IP_CHECKSUM_ENABLE	0x10
#define FXP_IPCB_TCPUDP_CHECKSUM_ENABLE	0x20
#define FXP_IPCB_TCP_PACKET		0x40
#define FXP_IPCB_LARGESEND_ENABLE	0x80
#define FXP_IPCB_HARDWAREPARSING_ENABLE	0x01
#define FXP_IPCB_INSERTVLAN_ENABLE	0x02

/*
 * Control Block (CB) definitions
 */

/* status */
#define FXP_CB_STATUS_OK	0x2000
#define FXP_CB_STATUS_C		0x8000
/* commands */
#define FXP_CB_COMMAND_NOP	0x0
#define FXP_CB_COMMAND_IAS	0x1
#define FXP_CB_COMMAND_CONFIG	0x2
#define FXP_CB_COMMAND_MCAS	0x3
#define FXP_CB_COMMAND_XMIT	0x4
#define FXP_CB_COMMAND_UCODE	0x5
#define FXP_CB_COMMAND_DUMP	0x6
#define FXP_CB_COMMAND_DIAG	0x7
#define FXP_CB_COMMAND_LOADFILT	0x8
#define FXP_CB_COMMAND_IPCBXMIT 0x9

/* command flags */
#define FXP_CB_COMMAND_SF	0x0008	/* simple/flexible mode */
#define FXP_CB_COMMAND_I	0x2000	/* generate interrupt on completion */
#define FXP_CB_COMMAND_S	0x4000	/* suspend on completion */
#define FXP_CB_COMMAND_EL	0x8000	/* end of list */

/*
 * RFA definitions
 */

struct fxp_rfa {
	uint16_t rfa_status;
	uint16_t rfa_control;
	uint32_t link_addr;
	uint32_t rbd_addr;
	uint16_t actual_size;
	uint16_t size;

	/*
	 * The following fields are only available when using
	 * extended receive mode on an 82550/82551 chipset.
	 */
	uint16_t rfax_vlan_id;
	uint8_t rfax_rx_parser_sts;
	uint8_t rfax_rsvd0;
	uint16_t rfax_security_sts;
	uint8_t rfax_csum_sts;
	uint8_t rfax_zerocopy_sts;
	uint8_t rfax_pad[8];
} __packed;
#define FXP_RFAX_LEN 16

#define FXP_RFA_STATUS_RCOL	0x0001	/* receive collision */
#define FXP_RFA_STATUS_IAMATCH	0x0002	/* 0 = matches station address */
#define FXP_RFA_STATUS_NOAMATCH	0x0004	/* 1 = doesn't match anything */
#define FXP_RFA_STATUS_PARSE	0x0008	/* pkt parse ok (82550/1 only) */
#define FXP_RFA_STATUS_S4	0x0010	/* receive error from PHY */
#define FXP_RFA_STATUS_TL	0x0020	/* type/length */
#define FXP_RFA_STATUS_FTS	0x0080	/* frame too short */
#define FXP_RFA_STATUS_OVERRUN	0x0100	/* DMA overrun */
#define FXP_RFA_STATUS_RNR	0x0200	/* no resources */
#define FXP_RFA_STATUS_ALIGN	0x0400	/* alignment error */
#define FXP_RFA_STATUS_CRC	0x0800	/* CRC error */
#define FXP_RFA_STATUS_VLAN	0x1000	/* VLAN tagged frame */
#define FXP_RFA_STATUS_OK	0x2000	/* packet received okay */
#define FXP_RFA_STATUS_C	0x8000	/* packet reception complete */
#define FXP_RFA_CONTROL_SF	0x08	/* simple/flexible memory mode */
#define FXP_RFA_CONTROL_H	0x10	/* header RFD */
#define FXP_RFA_CONTROL_S	0x4000	/* suspend after reception */
#define FXP_RFA_CONTROL_EL	0x8000	/* end of list */

/* Bits in the 'csum_sts' byte */
#define FXP_RFDX_CS_TCPUDP_CSUM_BIT_VALID	0x10
#define FXP_RFDX_CS_TCPUDP_CSUM_VALID		0x20
#define FXP_RFDX_CS_IP_CSUM_BIT_VALID		0x01
#define FXP_RFDX_CS_IP_CSUM_VALID		0x02

/* Bits in the 'packet parser' byte */
#define FXP_RFDX_P_PARSE_BIT			0x08
#define FXP_RFDX_P_CSUM_PROTOCOL_MASK		0x03
#define FXP_RFDX_P_TCP_PACKET			0x00
#define FXP_RFDX_P_UDP_PACKET			0x01
#define FXP_RFDX_P_IP_PACKET			0x03

/*
 * Statistics dump area definitions
 */
struct fxp_stats {
	uint32_t tx_good;
	uint32_t tx_maxcols;
	uint32_t tx_latecols;
	uint32_t tx_underruns;
	uint32_t tx_lostcrs;
	uint32_t tx_deffered;
	uint32_t tx_single_collisions;
	uint32_t tx_multiple_collisions;
	uint32_t tx_total_collisions;
	uint32_t rx_good;
	uint32_t rx_crc_errors;
	uint32_t rx_alignment_errors;
	uint32_t rx_rnr_errors;
	uint32_t rx_overrun_errors;
	uint32_t rx_cdt_errors;
	uint32_t rx_shortframes;
	uint32_t tx_pause;
	uint32_t rx_pause;
	uint32_t rx_controls;
	uint16_t tx_tco;
	uint16_t rx_tco;
	uint32_t completion_status;
	uint32_t reserved0;
	uint32_t reserved1;
	uint32_t reserved2;
};
#define FXP_STATS_DUMP_COMPLETE	0xa005
#define FXP_STATS_DR_COMPLETE	0xa007

/*
 * Serial EEPROM control register bits
 */
#define FXP_EEPROM_EESK		0x01 		/* shift clock */
#define FXP_EEPROM_EECS		0x02 		/* chip select */
#define FXP_EEPROM_EEDI		0x04 		/* data in */
#define FXP_EEPROM_EEDO		0x08 		/* data out */

/*
 * Serial EEPROM opcodes, including start bit
 */
#define FXP_EEPROM_OPC_ERASE	0x4
#define FXP_EEPROM_OPC_WRITE	0x5
#define FXP_EEPROM_OPC_READ	0x6

/*
 * EEPROM map
 */
#define	FXP_EEPROM_MAP_IA0	0x00		/* Station address */
#define	FXP_EEPROM_MAP_IA1	0x01
#define	FXP_EEPROM_MAP_IA2	0x02
#define	FXP_EEPROM_MAP_COMPAT	0x03		/* Compatibility */
#define	FXP_EEPROM_MAP_CNTR	0x05		/* Controller/connector type */
#define	FXP_EEPROM_MAP_PRI_PHY	0x06		/* Primary PHY record */
#define	FXP_EEPROM_MAP_SEC_PHY	0x07		/* Secondary PHY record */
#define	FXP_EEPROM_MAP_PWA0	0x08		/* Printed wire assembly num. */
#define	FXP_EEPROM_MAP_PWA1	0x09		/* Printed wire assembly num. */
#define	FXP_EEPROM_MAP_ID	0x0A		/* EEPROM ID */
#define	FXP_EEPROM_MAP_SUBSYS	0x0B		/* Subsystem ID */
#define	FXP_EEPROM_MAP_SUBVEN	0x0C		/* Subsystem vendor ID */
#define	FXP_EEPROM_MAP_CKSUM64	0x3F		/* 64-word EEPROM checksum */
#define	FXP_EEPROM_MAP_CKSUM256	0xFF		/* 256-word EEPROM checksum */

/*
 * Management Data Interface opcodes
 */
#define FXP_MDI_WRITE		0x1
#define FXP_MDI_READ		0x2

/*
 * PHY device types
 */
#define FXP_PHY_DEVICE_MASK	0x3f00
#define FXP_PHY_SERIAL_ONLY	0x8000
#define FXP_PHY_NONE		0
#define FXP_PHY_82553A		1
#define FXP_PHY_82553C		2
#define FXP_PHY_82503		3
#define FXP_PHY_DP83840		4
#define FXP_PHY_80C240		5
#define FXP_PHY_80C24		6
#define FXP_PHY_82555		7
#define FXP_PHY_DP83840A	10
#define FXP_PHY_82555B		11

/*
 * Chip revision values.
 */
#define FXP_REV_82557		1       /* catchall 82557 chip type */
#define FXP_REV_82558_A4	4	/* 82558 A4 stepping */
#define FXP_REV_82558_B0	5	/* 82558 B0 stepping */
#define FXP_REV_82559_A0	8	/* 82559 A0 stepping */
#define FXP_REV_82559S_A	9	/* 82559S A stepping */
#define FXP_REV_82550		12
#define FXP_REV_82550_C		13	/* 82550 C stepping */
#define FXP_REV_82551_E		14	/* 82551 */
#define FXP_REV_82551_F		15	/* 82551 */
#define FXP_REV_82551_10	16	/* 82551 */
