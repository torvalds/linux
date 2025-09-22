/*	$OpenBSD: silireg.h,v 1.22 2013/11/26 20:33:16 deraadt Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
 * Copyright (c) 2010 Conformal Systems LLC <info@conformal.com>
 * Copyright (c) 2010 Jonathan Matthew <jonathan@d14n.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* PCI Registers */
#define SILI_PCI_BAR_GLOBAL	0x10 /* Global Registers address */
#define SILI_PCI_BAR_PORT	0x18 /* Port Registers address */
#define SILI_PCI_BAR_INDIRECT	0x20 /* Indirect IO Registers address */

/* Global Registers */
#define SILI_REG_PORT0_STATUS	0x00 /* Port 0 Slot Status */
#define SILI_REG_PORT1_STATUS	0x04 /* Port 1 Slot Status */
#define SILI_REG_PORT2_STATUS	0x08 /* Port 2 Slot Status */
#define SILI_REG_PORT3_STATUS	0x0c /* Port 3 Slot Status */
#define SILI_REG_GC		0x40 /* Global Control */
#define  SILI_REG_GC_GR			(1<<31) /* Global Reset */
#define  SILI_REG_GC_MSIACK		(1<<30) /* MSI Acknowledge */
#define  SILI_REG_GC_I2CINT		(1<<29) /* I2C Interrupt Enable */
#define  SILI_REG_GC_PERRDIS		(1<<28) /* PCI Error Report Disable */
#define  SILI_REG_GC_REQ64		(1<<20) /* latched PCI REQ64 */
#define  SILI_REG_GC_DEVSEL		(1<<19) /* latched PCI DEVSEL */
#define  SILI_REG_GC_STOP		(1<<18) /* latched PCI STOP */
#define  SILI_REG_GC_TRDY		(1<<17) /* latched PCI TRDY */
#define  SILI_REG_GC_M66EN		(1<<16) /* M66EN PCI bus signal */
#define  SILI_REG_GC_PIE_MASK		0x0f
#define SILI_FMT_GC		"\020" "\040GR" "\037MSIACK" "\036I2CINT" \
				    "\035PERRDIS" "\025REQ64" "\024DEVSEL" \
				    "\023STOP" "\022TRDY" "\021M66EN" \
				    "\004P3IE" "\003P2IE" "\002P1IE" "\001P0IE"
#define SILI_REG_GIS		0x44 /* Global Interrupt Status */
#define  SILI_REG_GIS_I2C		(1 << 29)
#define  SILI_REG_GIS_PIS_MASK		0x0f
#define SILI_REG_PHYCONF	0x48 /* PHY Configuration */
#define SILI_REG_BISTCTL	0x50 /* BIST Control */
#define SILI_REG_BISTPATTERN	0x54 /* BIST Pattern */
#define SILI_REG_BISTSTAT	0x58 /* BIST Status */
#define SILI_REG_FLASHADDR	0x70 /* Flash Address */
#define SILI_REG_FLASHDATA	0x74 /* Flash Memory Data / GPIO Control */
#define SILI_REG_GPIOCTL	SILI_REG_FLASHDATA
#define SILI_REG_IICADDR	0x78 /* I2C Address */
#define SILI_REG_IIC		0x7c /* I2C Data / Control */

#define SILI_PORT_SIZE		0x2000
#define SILI_PORT_OFFSET(_p)	((_p) * SILI_PORT_SIZE)

/* Port Registers */
#define SILI_PREG_LRAM		0x0000 /* Port LRAM */
#define SILI_PREG_SLOT_WIDTH	0x80
#define SILI_PREG_SLOT(_s)	(SILI_PREG_LRAM + (_s) * SILI_PREG_SLOT_WIDTH)
#define SILI_PREG_RX_COUNT(_s)	(SILI_PREG_SLOT(_s) + 0x04)
#define SILI_PREG_SIG_HI(_s)	(SILI_PREG_SLOT(_s) + 0x0c)
#define SILI_PREG_SIG_HI_SHIFT	8
#define SILI_PREG_SIG_LO(_s)	(SILI_PREG_SLOT(_s) + 0x14)
#define SILI_PREG_SIG_LO_MASK	0xff

#define SILI_PREG_PMP_BASE	0xF80	/* PMP Device Status/QActive Registers */
#define SILI_PREG_PMP_STATUS(_p) (SILI_PREG_PMP_BASE + (_p * 8))
#define SILI_PREG_PMP_QACTIVE(_p) (SILI_PREG_PMP_BASE + 4 + (_p * 8))
#define  SILI_PREG_PMP_STATUS_PIO	0x000000FF
#define  SILI_PREG_PMP_STATUS_ACTIVE	0x00000F00
#define  SILI_PREG_PMP_STATUS_BUSY	(1 << 13)
#define  SILI_PREG_PMP_STATUS_NCQ	(1 << 14)
#define  SILI_PREG_PMP_STATUS_LEGACY_Q	(1 << 15)
#define  SILI_PREG_PMP_STATUS_PENDING	(1 << 16)

#define SILI_PREG_PCS		0x1000 /* Port Control Set / Status */
#define  SILI_PREG_PCS_PORTRDY		(1<<31) /* Port Ready */
#define  SILI_PREG_PCS_OOBB		(1<<25) /* OOB Bypass */
#define  SILI_PREG_PCS_ACTIVE(_x)	(((_x)>>16) & 0x1f) /* Active Slot */
#define  SILI_PREG_PCS_LED_ON		(1<<15) /* LED On */
#define  SILI_PREG_PCS_AIA		(1<<14) /* Auto Interlock Accept */
#define  SILI_PREG_PCS_PMEN		(1<<13) /* Port Mult Enable */
#define  SILI_PREG_PCS_IA		(1<<12) /* Interlock Accept */
#define  SILI_PREG_PCS_IR		(1<<11) /* Interlock Reject */
#define  SILI_PREG_PCS_A32B		(1<<10) /* 32-bit Activation */
#define  SILI_PREG_PCS_SD		(1<<9) /* Scrambler Disable */
#define  SILI_PREG_PCS_CD		(1<<8) /* CONT Disable */
#define  SILI_PREG_PCS_TB		(1<<7) /* Transmit BIST */
#define  SILI_PREG_PCS_RESUME		(1<<6) /* Resume */
#define  SILI_PREG_PCS_PLEN		(1<<5) /* Packet Length */
#define  SILI_PREG_PCS_LEDDISABLE	(1<<4) /* LED Disable */
#define  SILI_PREG_PCS_NOINTCLR		(1<<3) /* No Intr Clear on Read */
#define  SILI_PREG_PCS_PORTINIT		(1<<2) /* Port Initialize */
#define  SILI_PREG_PCS_DEVRESET		(1<<1) /* Device Reset */
#define  SILI_PREG_PCS_PORTRESET	(1<<0) /* Port Reset */
#define SILI_PFMT_PCS		"\020" "\032OOBB" "\020LED_ON" "\017AIA" \
				    "\016PMEN" "\015IA" "\014IR" "\013A32B" \
				    "\012SD" "\011CD" "\010TB" "\007RESUME" \
				    "\006PLEN" "\005LEDDISABLE" \
				    "\004NOINTCLR" "\003PORTINIT" \
				    "\002PORTINIT" "\001PORTRESET"
#define SILI_PREG_PCC		0x1004 /* Port Control Clear */
#define  SILI_PREG_PCC_OOBB		(1<<25) /* OOB Bypass */
#define  SILI_PREG_PCC_LED_ON		(1<<15) /* LED On */
#define  SILI_PREG_PCC_AIA		(1<<14) /* Auto Interlock Accept */
#define  SILI_PREG_PCC_PMEN		(1<<13) /* Port Mult Enable */
#define  SILI_PREG_PCC_IA		(1<<12) /* Interlock Accept */
#define  SILI_PREG_PCC_IR		(1<<11) /* Interlock Reject */
#define  SILI_PREG_PCC_A32B		(1<<10) /* 32-bit Activation */
#define  SILI_PREG_PCC_SD		(1<<9) /* Scrambler Disable */
#define  SILI_PREG_PCC_CD		(1<<8) /* CONT Disable */
#define  SILI_PREG_PCC_TB		(1<<7) /* Transmit BIST */
#define  SILI_PREG_PCC_RESUME		(1<<6) /* Resume */
#define  SILI_PREG_PCC_PLEN		(1<<5) /* Packet Length */
#define  SILI_PREG_PCC_LEDDISABLE	(1<<4) /* LED Disable */
#define  SILI_PREG_PCC_NOINTCLR		(1<<3) /* No Intr Clear on Read */
#define  SILI_PREG_PCC_PORTINIT		(1<<2) /* Port Initialize */
#define  SILI_PREG_PCC_DEVRESET		(1<<1) /* Device Reset */
#define  SILI_PREG_PCC_PORTRESET	(1<<0) /* Port Reset */
#define SILI_PREG_IS		0x1008 /* Interrupt Status */
#define  SILI_PREG_IS_SDB		(1<<11) /* SDB Notify */
#define  SILI_PREG_IS_HANDSHAKE		(1<<10) /* Handshake error threshold */
#define  SILI_PREG_IS_CRC		(1<<9) /* CRC error threshold */
#define  SILI_PREG_IS_DEC		(1<<8) /* 8b/10b decode error thresh */
#define  SILI_PREG_IS_DEVXCHG		(1<<7) /* Device Exchanged */
#define  SILI_PREG_IS_UNRECFIS		(1<<6) /* Unrecognized FIS Type */
#define  SILI_PREG_IS_COMWAKE		(1<<5) /* ComWake */
#define  SILI_PREG_IS_PHYRDYCHG		(1<<4) /* Phy Ready Change */
#define  SILI_PREG_IS_PMCHG		(1<<3) /* Power Mmgt Change */
#define  SILI_PREG_IS_PORTRDY		(1<<2) /* Port Ready */
#define  SILI_PREG_IS_CMDERR		(1<<1) /* Command Error */
#define  SILI_PREG_IS_CMDCOMP		(1<<0) /* Command Completion */
#define SILI_PFMT_IS		"\020" "\014SDB" "\013HANDSHAKE" \
				    "\012CRC" "\011DECODE" \
				    "\010DEVXCHG" "\007UNRECFIS" \
				    "\006COMWAKE" "\005PHYRDYCHG" \
				    "\004PMCHG" "\003PORTRDY" \
				    "\002CMDERR" "\001CMDCOMP"
#define SILI_PREG_IES		0x1010 /* Interrupt Enable Set */
#define SILI_PREG_IEC		0x1014 /* Interrupt Enable Clear */
#define  SILI_PREG_IE_SDB		(1<<11) /* SDB Notify */
#define  SILI_PREG_IE_DEVXCHG		(1<<7) /* Device Exchange */
#define  SILI_PREG_IE_UNRECFIS		(1<<6) /* Unrecognized FIS Type */
#define  SILI_PREG_IE_COMWAKE		(1<<5) /* ComWake */
#define  SILI_PREG_IE_PHYRDYCHG		(1<<4) /* Phy Ready Change */
#define  SILI_PREG_IE_PMCHG		(1<<3) /* Power Mmgt Change */
#define  SILI_PREG_IE_PORTRDY		(1<<2) /* Port Ready */
#define  SILI_PREG_IE_CMDERR		(1<<1) /* Command Error */
#define  SILI_PREG_IE_CMDCOMP		(1<<0) /* Command Completion */
#define  SILI_PREG_IE_ALL		0x08ff
#define SILI_PREG_AUA		0x101c /* Activation Upper Address */
#define SILI_PREG_FIFO		0x1020 /* Command Execution FIFO */
#define SILI_PREG_CE		0x1024 /* Command Error */
#define  SILI_PREG_CE_DEVICEERROR		1
#define  SILI_PREG_CE_SDBERROR			2
#define  SILI_PREG_CE_DATAFISERROR		3
#define SILI_PREG_FC		0x1028 /* FIS Configuration */
#define SILI_PREG_RFT		0x102c /* Request FIFO Threshold */
#define SILI_PREG_DEC		0x1040 /* 8b/10b Decode Error Counter */
#define SILI_PREG_CEC		0x1044 /* CRC Error Counter */
#define SILI_PREG_HEC		0x1048 /* Handshake Error Counter */
#define SILI_PREG_PHYCONF	0x1050 /* Port PHY Configuration */
#define SILI_PREG_PSS		0x1800 /* Port Slot Status */
#define SILI_PREG_PSS_ATTENTION		(1U << 31)
#define SILI_PREG_PSS_ALL_SLOTS		0x7fffffff
#define SILI_PREG_CAR_LO(_s)	(0x1c00 + ((_s) * 0x8)) /* Cmd Activate Reg */
#define SILI_PREG_CAR_HI(_s)	(0x1c00 + ((_s) * 0x8) + 0x4)
#define SILI_PREG_CONTEXT	0x1e04 /* Port Context Register */
#define  SILI_PREG_CONTEXT_SLOT_MASK	0x1F
#define  SILI_PREG_CONTEXT_PMPORT_MASK	0x0F
#define  SILI_PREG_CONTEXT_SLOT_SHIFT	0
#define  SILI_PREG_CONTEXT_PMPORT_SHIFT	5
#define SILI_PREG_SCTL		0x1f00 /* SControl */
#define  SILI_PREG_SCTL_PMP		0x000F0000
#define  SILI_PREG_SCTL_PMP_SHIFT	16
#define SILI_PREG_SSTS		0x1f04 /* SStatus */
#define SILI_PREG_SERR		0x1f08 /* SError */
#define SILI_PREG_SACT		0x1f0c /* SActive */
#define SILI_PREG_SNOT		0x1f10 /* SNotification */




struct sili_sge {
	u_int32_t		addr_lo;
	u_int32_t		addr_hi;
	u_int32_t		data_count;
	u_int32_t		flags;
#define SILI_SGE_TRM			(1<<31)
#define SILI_SGE_LNK			(1<<30)
#define SILI_SGE_DRD			(1<<29)
#define SILI_SGE_XCF			(1<<28)
} __packed;

#define SILI_SGT_SGLLEN		4

struct sili_sgt {
	struct sili_sge		sgl[SILI_SGT_SGLLEN];
} __packed;

#define SILI_PRB_PROTOCOL_OVERRIDE	(1<<0)
#define SILI_PRB_RETRANSMIT		(1<<1)
#define SILI_PRB_EXTERNAL_COMMAND	(1<<2)
#define SILI_PRB_RECEIVE		(1<<3)
#define SILI_PRB_PACKET_READ		(1<<4)
#define SILI_PRB_PACKET_WRITE		(1<<5)
#define SILI_PRB_INTERRUPT_MASK		(1<<6)
#define SILI_PRB_SOFT_RESET		(1<<7)

struct sili_prb { /* this is just a useful template */
	u_int16_t		control;
	u_int16_t		reserved1;
	u_int32_t		reserved2;

	u_int8_t		fis[ATA_FIS_LENGTH];

	u_int32_t		reserved3[9];
} __packed;

struct sili_prb_ata {
	u_int16_t		control;
	u_int16_t		protocol_override;
	u_int32_t		rx_count;

	u_int8_t		fis[ATA_FIS_LENGTH];

	u_int32_t		reserved;

	struct sili_sge		sgl[2];
} __packed;

struct sili_prb_packet {
	u_int16_t		control;
	u_int16_t		protocol_override;
	u_int32_t		rx_count;

	u_int8_t		fis[ATA_FIS_LENGTH];

	u_int32_t		reserved;

	u_int8_t		cdb[16];

	struct sili_sge		sgl[1];
} __packed;

struct sili_prb_softreset {
	u_int16_t		control;
	u_int16_t		reserved1;
	u_int32_t		reserved2;

	u_int8_t		fis[ATA_FIS_LENGTH];

	u_int32_t		reserved3[9];
} __packed;

#define SILI_MAX_CMDS		31
#define SILI_PRB_LENGTH		64
#define SILI_PRB_ALIGN		8
#define SILI_SGT_LENGTH		64
