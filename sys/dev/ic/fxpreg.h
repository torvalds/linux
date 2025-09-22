/*	$OpenBSD: fxpreg.h,v 1.15 2024/09/04 07:54:52 mglocker Exp $	*/

/*
 * Copyright (c) 1995, David Greenman
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
 *	$FreeBSD: if_fxpreg.h,v 1.13 1998/06/08 09:47:46 bde Exp $
 */

#define FXP_VENDORID_INTEL	0x8086
#define FXP_DEVICEID_i82557	0x1229

#define FXP_PCI_MMBA	0x10
#define FXP_PCI_IOBA	0x14

/*
 * Control/status registers.
 */
#define	FXP_CSR_SCB_STATUS	0	/* scb_status (2 byte) */
#define	FXP_CSR_SCB_COMMAND	2	/* scb_command (2 byte) */
#define	FXP_CSR_SCB_GENERAL	4	/* scb_general (4 bytes) */
#define	FXP_CSR_PORT		8	/* port (4 bytes) */
#define	FXP_CSR_FLASHCONTROL	12	/* flash control (2 bytes) */
#define	FXP_CSR_EEPROMCONTROL	14	/* eeprom control (2 bytes) */
#define	FXP_CSR_MDICONTROL	16	/* mdi control (4 bytes) */

/*
 * FOR REFERENCE ONLY, the old definition of FXP_CSR_SCB_RUSCUS:
 *
 *	volatile u_int8_t	:2,
 *				scb_rus:4,
 *				scb_cus:2;
 */

#define FXP_PORT_SOFTWARE_RESET		0
#define FXP_PORT_SELFTEST		1
#define FXP_PORT_SELECTIVE_RESET	2
#define FXP_PORT_DUMP			3

#define FXP_SCB_RUS_IDLE		0x0000
#define FXP_SCB_RUS_SUSPENDED		0x0001
#define FXP_SCB_RUS_NORESOURCES		0x0002
#define FXP_SCB_RUS_READY		0x0004
#define FXP_SCB_RUS_SUSP_NORBDS		0x0009
#define FXP_SCB_RUS_NORES_NORBDS	0x000a
#define FXP_SCB_RUS_READY_NORBDS	0x000c

#define FXP_SCB_CUS_IDLE		0x0000
#define FXP_SCB_CUS_SUSPENDED		0x0040
#define FXP_SCB_CUS_ACTIVE		0x0080
#define FXP_SCB_CUS_MASK		0x00c0

#define FXP_SCB_STATACK_SWI		0x0400
#define FXP_SCB_STATACK_MDI		0x0800
#define FXP_SCB_STATACK_RNR		0x1000
#define FXP_SCB_STATACK_CNA		0x2000
#define FXP_SCB_STATACK_FR		0x4000
#define FXP_SCB_STATACK_CXTNO		0x8000
#define FXP_SCB_STATACK_MASK		0xfc00

#define FXP_SCB_COMMAND_CU_NOP		0x0000
#define FXP_SCB_COMMAND_CU_START	0x0010
#define FXP_SCB_COMMAND_CU_RESUME	0x0020
#define FXP_SCB_COMMAND_CU_DUMP_ADR	0x0040
#define FXP_SCB_COMMAND_CU_DUMP		0x0050
#define FXP_SCB_COMMAND_CU_BASE		0x0060
#define FXP_SCB_COMMAND_CU_DUMPRESET	0x0070

#define FXP_SCB_COMMAND_RU_NOP		0x0000
#define FXP_SCB_COMMAND_RU_START	0x0001
#define FXP_SCB_COMMAND_RU_RESUME	0x0002
#define FXP_SCB_COMMAND_RU_ABORT	0x0004
#define FXP_SCB_COMMAND_RU_LOADHDS	0x0005
#define FXP_SCB_COMMAND_RU_BASE		0x0006
#define FXP_SCB_COMMAND_RU_RBDRESUME	0x0007

#define	FXP_SCB_INTRCNTL_REQUEST_SWI	0x0200

#define	FXP_CMD_TMO	(10000)

/*
 * Command block definitions
 */
struct fxp_cb_nop {
	void *fill[2];
	volatile u_int16_t cb_status;
	volatile u_int16_t cb_command;
	volatile u_int32_t link_addr;
};
struct fxp_cb_ias {
	volatile u_int16_t cb_status;
	volatile u_int16_t cb_command;
	volatile u_int32_t link_addr;
	volatile u_int8_t macaddr[6];
};
/* I hate bit-fields :-( */ /* SO WHY USE IT, EH? */

/*
 *  Bitfields cleaned out since it is not endian compatible. OK
 *  you can define a big endian structure but can never be 100% safe...
 *
 *  ANY PROGRAMMER TRYING THE STUNT WITH BITFIELDS IN A DEVICE DRIVER
 *  SHOULD BE PUT UP AGAINST THE WALL, BLINDFOLDED AND SHOT!
 */
struct fxp_cb_config {
        volatile u_int16_t      cb_status;
        volatile u_int16_t      cb_command;
        volatile u_int32_t      link_addr;
        volatile u_int8_t       byte_count;
        volatile u_int8_t       fifo_limit;
        volatile u_int8_t       adaptive_ifs;
        volatile u_int8_t       ctrl0;
        volatile u_int8_t       rx_dma_bytecount;
        volatile u_int8_t       tx_dma_bytecount;
        volatile u_int8_t       ctrl1;
        volatile u_int8_t       ctrl2;
        volatile u_int8_t       mediatype;
        volatile u_int8_t       void2;
        volatile u_int8_t       ctrl3;
        volatile u_int8_t       linear_priority;
        volatile u_int8_t       interfrm_spacing;
        volatile u_int8_t       void3;
        volatile u_int8_t       void4;
        volatile u_int8_t       promiscuous;
        volatile u_int8_t       void5;
        volatile u_int8_t       void6;
        volatile u_int8_t       stripping;
        volatile u_int8_t       fdx_pin;
        volatile u_int8_t       multi_ia;
        volatile u_int8_t       mc_all;
};

#define MAXMCADDR 80
struct fxp_cb_mcs {
	volatile u_int16_t cb_status;
	volatile u_int16_t cb_command;
	volatile u_int32_t link_addr;
	volatile u_int16_t mc_cnt;
	volatile u_int8_t mc_addr[MAXMCADDR][6];
};

/*
 * Number of DMA segments in a TxCB. Note that this is carefully
 * chosen to make the total struct size an even power of two. It's
 * critical that no TxCB be split across a page boundary since
 * no attempt is made to allocate physically contiguous memory.
 */
#define	SZ_TXCB		16	/* TX control block head size = 4 32 bit words */
#define	SZ_TBD		8	/* Fragment ptr/size block size */
#define FXP_NTXSEG      ((256 - SZ_TXCB) / SZ_TBD)

struct fxp_tbd {
	volatile u_int32_t tb_addr;
	volatile u_int32_t tb_size;
};
struct fxp_cb_tx {
	volatile u_int16_t cb_status;
	volatile u_int16_t cb_command;
	volatile u_int32_t link_addr;
	volatile u_int32_t tbd_array_addr;
	volatile u_int16_t byte_count;
	volatile u_int8_t tx_threshold;
	volatile u_int8_t tbd_number;
	/*
	 * The following isn't actually part of the TxCB.
	 */
	volatile struct fxp_tbd tbd[FXP_NTXSEG];
};

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
/* command flags */
#define FXP_CB_COMMAND_SF	0x0008	/* simple/flexible mode */
#define FXP_CB_COMMAND_I	0x2000	/* generate interrupt on completion */
#define FXP_CB_COMMAND_S	0x4000	/* suspend on completion */
#define FXP_CB_COMMAND_EL	0x8000	/* end of list */

/*
 * RFA definitions
 */

struct fxp_rfa {
	volatile u_int16_t rfa_status;
	volatile u_int16_t rfa_control;
	volatile u_int32_t link_addr;
	volatile u_int32_t rbd_addr;
	volatile u_int16_t actual_size;
	volatile u_int16_t size;
};
#define FXP_RFA_STATUS_RCOL	0x0001	/* receive collision */
#define FXP_RFA_STATUS_IAMATCH	0x0002	/* 0 = matches station address */
#define FXP_RFA_STATUS_S4	0x0010	/* receive error from PHY */
#define FXP_RFA_STATUS_TL	0x0020	/* type/length */
#define FXP_RFA_STATUS_FTS	0x0080	/* frame too short */
#define FXP_RFA_STATUS_OVERRUN	0x0100	/* DMA overrun */
#define FXP_RFA_STATUS_RNR	0x0200	/* RU not ready */
#define FXP_RFA_STATUS_ALIGN	0x0400	/* alignment error */
#define FXP_RFA_STATUS_CRC	0x0800	/* CRC error */
#define FXP_RFA_STATUS_OK	0x2000	/* packet received okay */
#define FXP_RFA_STATUS_C	0x8000	/* packet reception complete */
#define FXP_RFA_CONTROL_SF	0x08	/* simple/flexible memory mode */
#define FXP_RFA_CONTROL_H	0x10	/* header RFD */
#define FXP_RFA_CONTROL_S	0x4000	/* suspend after reception */
#define FXP_RFA_CONTROL_EL	0x8000	/* end of list */

/*
 * Statistics dump area definitions
 */
struct fxp_stats {
	volatile u_int32_t tx_good;
	volatile u_int32_t tx_maxcols;
	volatile u_int32_t tx_latecols;
	volatile u_int32_t tx_underruns;
	volatile u_int32_t tx_lostcrs;
	volatile u_int32_t tx_deffered;
	volatile u_int32_t tx_single_collisions;
	volatile u_int32_t tx_multiple_collisions;
	volatile u_int32_t tx_total_collisions;
	volatile u_int32_t rx_good;
	volatile u_int32_t rx_crc_errors;
	volatile u_int32_t rx_alignment_errors;
	volatile u_int32_t rx_rnr_errors;
	volatile u_int32_t rx_overrun_errors;
	volatile u_int32_t rx_cdt_errors;
	volatile u_int32_t rx_shortframes;
	volatile u_int32_t completion_status;
};
#define FXP_STATS_DUMP_COMPLETE	0xa005
#define FXP_STATS_DR_COMPLETE	0xa007
	
/*
 * Serial EEPROM control register bits
 */
/* shift clock */
#define FXP_EEPROM_EESK		0x01
/* chip select */
#define FXP_EEPROM_EECS		0x02
/* data in */
#define FXP_EEPROM_EEDI		0x04
/* data out */
#define FXP_EEPROM_EEDO		0x08

/*
 * Serial EEPROM opcodes, including start bit
 */
#define FXP_EEPROM_OPC_ERASE	0x4
#define FXP_EEPROM_OPC_WRITE	0x5
#define FXP_EEPROM_OPC_READ	0x6

/*
 * Serial EEPROM registers.  A subset of them from Intel's
 * "82559 EEPROM Map and Programming Information" document.
 */
#define FXP_EEPROM_REG_MAC		0x00
#define FXP_EEPROM_REG_COMPAT		0x03
#define  FXP_EEPROM_REG_COMPAT_MC10	0x0001
#define  FXP_EEPROM_REG_COMPAT_MC100	0x0002
#define  FXP_EEPROM_REG_COMPAT_SRV	0x0400
#define FXP_EEPROM_REG_PHY		0x06
#define FXP_EEPROM_REG_ID		0x0a
#define  FXP_EEPROM_REG_ID_STB		0x0002

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
 * PHY BMCR Basic Mode Control Register
 */
#define FXP_PHY_BMCR			0x0
#define FXP_PHY_BMCR_FULLDUPLEX		0x0100
#define FXP_PHY_BMCR_AUTOEN		0x1000
#define FXP_PHY_BMCR_SPEED_100M		0x2000

/*
 * DP84830 PHY, PCS Configuration Register
 */
#define FXP_DP83840_PCR			0x17
#define FXP_DP83840_PCR_LED4_MODE	0x0002	/* 1 = LED4 always indicates full duplex */
#define FXP_DP83840_PCR_F_CONNECT	0x0020	/* 1 = force link disconnect function bypass */
#define FXP_DP83840_PCR_BIT8		0x0100
#define FXP_DP83840_PCR_BIT10		0x0400

#define	MAXUCODESIZE 192
struct fxp_cb_ucode {
	volatile u_int16_t cb_status;
	volatile u_int16_t cb_command;
	volatile u_int32_t link_addr;
	volatile u_int32_t ucode[MAXUCODESIZE];
};

/* 
 * Chip revision values.
 */
#define FXP_REV_82557_A		0	/* 82557 A */
#define FXP_REV_82557_B		1	/* 82557 B */
#define FXP_REV_82557_C		2	/* 82557 C */
#define FXP_REV_82558_A4	4	/* 82558 A4 stepping */
#define FXP_REV_82558_B0	5	/* 82558 B0 stepping */
#define FXP_REV_82559_A0	8	/* 82559 A0 stepping */
#define FXP_REV_82559S_A	9	/* 82559S A stepping */
#define FXP_REV_82550		12
#define FXP_REV_82550_C		13	/* 82550 C stepping */
#define FXP_REV_82551_E		14	/* 82551 */
#define FXP_REV_82551_F		15	/* 82551 */
#define FXP_REV_82551_10	16	/* 82551 */
