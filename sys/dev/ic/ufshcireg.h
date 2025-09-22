/*	$OpenBSD: ufshcireg.h,v 1.14 2024/06/15 18:26:25 mglocker Exp $ */

/*
 * Copyright (c) 2022 Marcus Glocker <mglocker@openbsd.org>
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

/*
 * Generic parameters.
 */
#define UFSHCI_UCD_PRDT_MAX_SEGS	64
#define UFSHCI_UCD_PRDT_MAX_XFER	(UFSHCI_UCD_PRDT_MAX_SEGS * PAGE_SIZE)
#define UFSHCI_INTR_AGGR_TIMEOUT	0x08 /* 320us (1 unit = 40us) */
#define UFSHCI_INTR_AGGR_COUNT_MAX	31
#define UFSHCI_SLOTS_MIN		1
#define UFSHCI_SLOTS_MAX		32
#define UFSHCI_TARGETS_MAX		1
#define UFSHCI_LBS			4096 /* UFS Logical Block Size:
						For UFS minimum size shall be
					        4096 bytes */

/*
 * Controller Capabilities Registers
 */

/* Controller Capabilities */
#define UFSHCI_REG_CAP			0x00
#define  UFSHCI_REG_CAP_CS		(1 << 28) /* RO */
#define  UFSHCI_REG_CAP_UICDMETMS	(1 << 26) /* RO */
#define  UFSHCI_REG_CAP_OODDS		(1 << 25) /* RO */
#define  UFSHCI_REG_CAP_64AS		(1 << 24) /* RO */
#define  UFSHCI_REG_AUTOH8		(1 << 23) /* RO */
#define  UFSHCI_REG_CAP_NUTMRS(x)	((x >> 16) & 0x00000007) /* RO */
#define  UFSHCI_REG_CAP_RTT(x)		((x >>  8) & 0x000000ff) /* RO */
#define  UFSHCI_REG_CAP_NUTRS(x)	((x >>  0) & 0x0000001f) /* RO */
/* UFS Version in BCD format */
#define UFSHCI_REG_VER			0x08
#define  UFSHCI_REG_VER_MAJOR(x)	((x >> 8) & 0x0000000f) /* RO */
#define  UFSHCI_REG_VER_MINOR(x)	((x >> 4) & 0x0000000f) /* RO */
#define  UFSHCI_REG_VER_SUFFIX(x)	((x >> 0) & 0x0000000f) /* RO */
/* Product ID */
#define UFSHCI_REG_HCPID		0x10
/* Manufacturer ID */
#define UFSHCI_REG_HCMID		0x14
#define  UFSHCI_REG_HCMID_BI(x)		((x >> 8) & 0x000000ff) /* RO */
#define  UFSHCI_REG_HCMID_MIC(x)	((x >> 0) & 0x000000ff) /* RO */
/* Auto-Hibernate Idle Timer */
#define UFSHCI_REG_AHIT			0x18
#define UFSHCI_REG_AHIT_TS(x)		(x << 10)
#define  UFSHCI_REG_AHIT_TS_1US		0x00
#define  UFSHCI_REG_AHIT_TS_10US	0x01
#define  UFSHCI_REG_AHIT_TS_100US	0x02
#define  UFSHCI_REG_AHIT_TS_1MS		0x03
#define  UFSHCI_REG_AHIT_TS_10MS	0x04
#define  UFSHCI_REG_AHIT_TS_100MS	0x05

/*
 * Operation and Runtime Registers
 */

/* Interrupt Status */
#define UFSHCI_REG_IS			0x20
#define  UFSHCI_REG_IS_CEFES		(1 << 18) /* RWC */
#define  UFSHCI_REG_IS_SBFES		(1 << 17) /* RWC */
#define  UFSHCI_REG_IS_HCFES		(1 << 16) /* RWC */
#define  UFSHCI_REG_IS_UTPES		(1 << 12) /* RWC */
#define  UFSHCI_REG_IS_DFES		(1 << 11) /* RWC */
#define  UFSHCI_REG_IS_UCCS		(1 << 10) /* RWC */
#define  UFSHCI_REG_IS_UTMRCS		(1 <<  9) /* RWC */
#define  UFSHCI_REG_IS_ULSS		(1 <<  8) /* RWC */
#define  UFSHCI_REG_IS_ULLS		(1 <<  7) /* RWC */
#define  UFSHCI_REG_IS_UHES		(1 <<  6) /* RWC */
#define  UFSHCI_REG_IS_UHXS		(1 <<  5) /* RWC */
#define  UFSHCI_REG_IS_UPMS		(1 <<  4) /* RWC */
#define  UFSHCI_REG_IS_UTMS		(1 <<  3) /* RWC */
#define  UFSHCI_REG_IS_UE		(1 <<  2) /* RWC */
#define  UFSHCI_REG_IS_UDEPRI		(1 <<  1) /* RWC */
#define  UFSHCI_REG_IS_UTRCS		(1 <<  0) /* RWC */
/* Interrupt Enable */
#define UFSHCI_REG_IE			0x24
#define  UFSHCI_REG_IE_CEFFE		(1 << 18) /* RW */
#define  UFSHCI_REG_IE_SBFEE		(1 << 17) /* RW */
#define  UFSHCI_REG_IE_HCFEE		(1 << 16) /* RW */
#define  UFSHCI_REG_IE_UTPEE		(1 << 12) /* RW */
#define  UFSHCI_REG_IE_DFEE		(1 << 11) /* RW */
#define  UFSHCI_REG_IE_UCCE		(1 << 10) /* RW */
#define  UFSHCI_REG_IE_UTMRCE		(1 <<  9) /* RW */
#define  UFSHCI_REG_IE_ULSSE		(1 <<  8) /* RW */
#define  UFSHCI_REG_IE_ULLSE		(1 <<  7) /* RW */
#define  UFSHCI_REG_IE_UHESE		(1 <<  6) /* RW */
#define  UFSHCI_REG_IE_UHXSE		(1 <<  5) /* RW */
#define  UFSHCI_REG_IE_UPMSE		(1 <<  4) /* RW */
#define  UFSHCI_REG_IE_UTMSE		(1 <<  3) /* RW */
#define  UFSHCI_REG_IE_UEE		(1 <<  2) /* RW */
#define  UFSHCI_REG_IE_UDEPRIE		(1 <<  1) /* RW */
#define  UFSHCI_REG_IE_UTRCE		(1 <<  0) /* RW */
/* Host Controller Status */
#define UFSHCI_REG_HCS			0x30
#define  UFSHCI_REG_HCS_TLUNUTPE(x)	((x >> 24) & 0x000000ff) /* RO */
#define  UFSHCI_REG_HCS_TTAGUTPE(x)	((x >> 16) & 0x000000ff) /* RO */
#define  UFSHCI_REG_HCS_UTPEC(x)	((x >> 12) & 0x0000000f) /* RO */
#define  UFSHCI_REG_HCS_UPMCRS(x)	((x >>  8) & 0x00000007) /* RO */
#define   UFSHCI_REG_HCS_UPMCRS_PWR_OK		0x00
#define   UFSHCI_REG_HCS_UPMCRS_PWR_LOCAL	0x01
#define   UFSHCI_REG_HCS_UPMCRS_PWR_REMTOTE	0x02
#define   UFSHCI_REG_HCS_UMPCRS_PWR_BUSY	0x03
#define   UFSHCI_REG_HCS_UMPCRS_PWR_ERROR_CAP	0x04
#define   UFSHCI_REG_HCS_UMPCRS_PWR_FATAL_ERROR	0x05
#define  UFSHCI_REG_HCS_UCRDY		(1 << 3) /* RO */
#define  UFSHCI_REG_HCS_UTMRLRDY	(1 << 2) /* RO */
#define  UFSHCI_REG_HCS_UTRLRDY		(1 << 1) /* RO */
#define  UFSHCI_REG_HCS_DP		(1 << 0) /* RO */
/* Host Controller Enable */
#define UFSHCI_REG_HCE			0x34
#define  UFSHCI_REG_HCE_CGE		(1 << 1) /* RW */
#define  UFSHCI_REG_HCE_HCE		(1 << 0) /* RW */
/* Host UIC Error Code PHY Adapter Layer */
#define UFSHCI_REG_UECPA		0x38
/* Host UIC Error Code Data Link Layer */
#define UFSHCI_REG_UECDL		0x3C
/* Host UIC Error Code Network Layer */
#define UFSHCI_REG_UECN			0x40
/* Host UIC Error Code Transport Layer */
#define UFSHCI_REG_UECT			0x44
/* Host UIC Error Code */
#define UFSHCI_REG_UECDME		0x48
/* UTP Transfer Request Interrupt Aggregation Control Register */
#define UFSHCI_REG_UTRIACR		0x4C
#define  UFSHCI_REG_UTRIACR_IAEN	(1U << 31) /* RW */
#define  UFSHCI_REG_UTRIACR_IAPWEN	(1 << 24) /* WO */
#define  UFSHCI_REG_UTRIACR_IASB	(1 << 20) /* RO */
#define  UFSHCI_REG_UTRIACR_CTR		(1 << 16) /* WO */
#define  UFSHCI_REG_UTRIACR_IACTH(x)	(x <<  8) /* RW, max. val = 31 */
#define  UFSHCI_REG_UTRIACR_IATOVAL(x)	(x <<  0) /* RW, 40us units (1=40us) */

/*
 * UTP Transfer Request List Registers
 */

/* Base Address */
#define UFSHCI_REG_UTRLBA			0x50 /* RW */
/* Base Address Upper 32-bits */
#define UFSHCI_REG_UTRLBAU			0x54 /* RW */
/* Door Bell Register */
#define UFSHCI_REG_UTRLDBR			0x58 /* RWS */
/* Clear Register */
#define UFSHCI_REG_UTRLCLR			0x5C /* WO */
/* Run-Stop Register */
#define UFSHCI_REG_UTRLRSR			0x60 /* RW */
#define  UFSHCI_REG_UTRLRSR_STOP		0x00
#define  UFSHCI_REG_UTRLRSR_START		0x01
/* Completion Notification Register */
#define UFSHCI_REG_UTRLCNR			0x64 /* RWC */

/*
 * UTP Task Management Request List Registers
 */

/* Base Address */
#define UFSHCI_REG_UTMRLBA			0x70 /* RW */
/* Base Address Upper 32-bits */
#define UFSHCI_REG_UTMRLBAU			0x74 /* RW */
/* Door Bell Register */
#define UFSHCI_REG_UTMRLDBR			0x78 /* RWS */
/* Clear Register */
#define UFSHCI_REG_UTMRLCLR			0x7C /* WO */
/* Run-Stop Register */
#define UFSHCI_REG_UTMRLRSR			0x80 /* RW */
#define  UFSHCI_REG_UTMRLRSR_STOP		0x00
#define  UFSHCI_REG_UTMRLRSR_START		0x01

/*
 * UIC Command Registers
 */

/* UIC Command Register */
#define UFSHCI_REG_UICCMD				0x90
#define  UFSHCI_REG_UICCMD_CMDOP_DME_GET		0x01 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_SET		0x02 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_PEER_GET		0x03 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_PEER_SET		0x04 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_POWERON		0x10 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_POWEROFF		0x11 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_ENABLE		0x12 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_RESET		0x14 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_ENDPOINTRESET	0x15 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_LINKSTARTUP	0x16 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_HIBERNATE_ENTER	0x17 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_HIBERNATE_EXIT	0x18 /* RW */
#define  UFSHCI_REG_UICCMD_CMDOP_DME_TEST_MODE		0x1A /* RW */
/* UIC Command Argument 1 */
#define UFSHCI_REG_UICCMDARG1				0x94
/* UIC Command Argument 2 */
#define UFSHCI_REG_UICCMDARG2				0x98
/* UIC Command Argument 3 */
#define UFSHCI_REG_UICCMDARG3				0x9C

/*
 * Vendor Specific Registers (0xC0 - 0xFF)
 */

/*
 * UTP Transfer Request Descriptor Structure
 */

/* Command Type (CT) */
#define UFSHCI_UTRD_DW0_CT_UFS		(1 << 28) /* UFS Storage */
/* Data Direction (DD) */
#define UFSHCI_UTRD_DW0_DD_NO		(0 << 25) /* No transfer */
#define UFSHCI_UTRD_DW0_DD_I2T		(1 << 25) /* From Initiator to Target */
#define UFSHCI_UTRD_DW0_DD_T2I		(2 << 25) /* From Target to Initiator */
/* Interrupt (I) */
#define UFSHCI_UTRD_DW0_I_REG		(0 << 24) /* Regular Command */
#define UFSHCI_UTRD_DW0_I_INT		(1 << 24) /* Interrupt Command */
/* Crypto Enable (CE) */
#define UFSHCI_UTRD_DW0_CE_DISABLE	(0 << 23) /* Disable Crypto */
#define UFSHCI_UTRD_DW0_CE_ENABLE	(1 << 23) /* Enable Crypto */
/* Crypto Configuration Index (CCI) */
#define UFSHCI_UTRD_DW0_CCI(x)		(x & 0x000000ff)

/* Data Unit Number Upper 32-bits (DUNL) */
#define UFSHCI_UTRD_DW1_DUNL(x)		(x << 0)

/* Overall Command Status (OCS) */
#define UFSHCI_UTRD_DW2_OCS(x)		(x & 0x000000ff)
#define UFSHCI_UTRD_DW2_OCS_SUCCESS	0x00 /* Success */
#define UFSHCI_UTRD_DW2_OCS_ICTA	0x01 /* Invalid Command Table Attr. */
#define UFSHCI_UTRD_DW2_OCS_IPA		0x02 /* Invalid PRDT Attr. */
#define UFSHCI_UTRD_DW2_OCS_MDBS	0x03 /* Mismatch Data Buffer Size */
#define UFSHCI_UTRD_DW2_OCS_MRUS	0x04 /* Mismatch Response UPIU Size */
#define UFSHCI_UTRD_DW2_OCS_CF		0x05 /* Communication Failure */
#define UFSHCI_UTRD_DW2_OCS_ABRT	0x06 /* Aborted */
#define UFSHCI_UTRD_DW2_OCS_FE		0x07 /* Fatal Error */
#define UFSHCI_UTRD_DW2_OCS_DFE		0x08 /* Device Fatal Error */
#define UFSHCI_UTRD_DW2_OCS_ICC		0x09 /* Invalid Crypto Configuration */
#define UFSHCI_UTRD_DW2_OCS_GCE		0x0A /* General Crypto Error */
#define UFSHCI_UTRD_DW2_OCS_IOV		0x0F /* Invalid OCS Value */

/* Data Unit Number Upper 32-bits Upper 32-bits (DUNU) */
#define UFSHCI_UTRD_DW3_DUNU(x)		(x << 0)

/* UTP Command Descriptor Base Address (UCDBA) */
#define UFSHCI_UTRD_DW4_UCDBA(x)	(x << 7)

/* UTP Command Descriptor Base Address Upper 32-bits (UCDBAU) */
#define UFSHCI_UTRD_DW5_UCDBAU(x)	(x << 0)

/* Response UPIU Offset (RUO) */
#define UFSHCI_UTRD_DW6_RUO(x)		(x << 16)
/* Response UPIU Length (RUL) */
#define UFSHCI_UTRD_DW6_RUL(x)		(x & 0x0000ffff)

/* PRDT Offset (PRDTO) */
#define UFSHCI_UTRD_DW7_PRDTO(x)	(x << 16)
/* PRDT Length (PRDTL) */
#define UFSHCI_UTRD_DW7_PRDTL(x)	(x & 0x0000ffff)

struct ufshci_utrd {
	uint32_t dw0; /* CT, DD, I, CE, CCI */
	uint32_t dw1; /* Data Unit Number Lower 32-bits (DUNL) */
	uint32_t dw2; /* OCS */
	uint32_t dw3; /* Data Unit Number Upper 32-bits (DUNU) */
	uint32_t dw4; /* UTP Cmd. Desc. Base Addr. Lower 32-bits (UCDBA) */
	uint32_t dw5; /* UTP Cmd. Desc. Base Addr. Upper 32-bits (UCDBAU) */
	uint32_t dw6; /* RUO, RUL */
	uint32_t dw7; /* PRDTO, PRDTL */
} __packed;

/*
 * UTP Command Descriptor, PRDT (Physical Region Description Table) Structure
 */

/* Data Base Address (DBA) */
#define UFSHCI_UCD_DW0_DBA(x)	(x & 0xfffffffc)

/* Data Byte Count (DBC) */
#define UFSHCI_UCD_DW3_DBC(x)	(x & 0x0003ffff)

struct ufshci_ucd_prdt {
	uint32_t dw0; /* Data base Address Lower 32-bits (DBA) */
	uint32_t dw1; /* Data base Address Upper 32-bits (DBAU) */
	uint32_t dw2; /* Reserved */
	uint32_t dw3; /* Data Byte Count (DBC) */
} __packed;

/*
 * UTP Task Management Request Descriptor Structure
 */

/* Interrupt (I) */
#define UFSHCI_UTMRD_DW0_I_DISABLE	(0 << 24)
#define UFSHCI_UTMRD_DW0_I_ENABLE	(1 << 24)

/* Overall Command Status (OCS) */
#define UFSHCI_UTMRD_DW2_OCS(x)		(x & 0x000000ff)
#define UFSHCI_UTMRD_DW2_OCS_SUCCESS	0x00 /* Success */
#define UFSHCI_UTMRD_DW2_OCS_ICTA	0x01 /* Invalid Command Table Attr. */
#define UFSHCI_UTMRD_DW2_OCS_IPA	0x02 /* Invalid PRDT Attr. */
#define UFSHCI_UTMRD_DW2_OCS_MDBS	0x03 /* Mismatch Data Buffer Size */
#define UFSHCI_UTMRD_DW2_OCS_MRUS	0x04 /* Mismatch Response UPIU Size */
#define UFSHCI_UTMRD_DW2_OCS_CF		0x05 /* Communication Failure */
#define UFSHCI_UTMRD_DW2_OCS_ABRT	0x06 /* Aborted */
#define UFSHCI_UTMRD_DW2_OCS_FE		0x07 /* Fatal Error */
#define UFSHCI_UTMRD_DW2_OCS_DFE	0x08 /* Device Fatal Error */
#define UFSHCI_UTMRD_DW2_OCS_ICC	0x09 /* Invalid Crypto Configuration */
#define UFSHCI_UTMRD_DW2_OCS_GCE	0x0A /* General Crypto Error */
#define UFSHCI_UTMRD_DW2_OCS_IOV	0x0F /* Invalid OCS Value */

struct ufshci_utmrd {
	uint32_t dw0; /* I */
	uint32_t dw1; /* Reserved */
	uint32_t dw2; /* OCS */
	uint32_t dw3; /* Reserved */
	uint8_t dw4_w11[32]; /* Task Management Request UPIU */
	uint8_t dw12_dw19[32]; /* Task Management Response UPIU */
} __packed;

/*
 * ****************************************************************************
 * Universal Flash Storage (UFS) Version 2.1 Specs from JESD220C
 * ****************************************************************************
 */

/* UPIU structures are in Big Endian! */

#define UPIU_TC_I2T_NOP_OUT		0x00
#define UPIU_TC_I2T_COMMAND		0x01
#define UPIU_TC_I2T_DATA_OUT		0x02
#define UPIU_TC_I2T_TMR			0x04
#define UPIU_TC_I2T_QUERY_REQUEST	0x16
#define UPIU_TC_T2I_NOP_IN		0x20
#define UPIU_TC_T2I_RESPONSE		0x21
#define UPIU_TC_T2I_DATA_IN		0x22
#define UPIU_TC_T2I_TMR			0x24
#define UPIU_TC_T2I_QUERY_RESPONSE	0x36
#define UPIU_TC_T2I_REJECT		0x3f

#define UPIU_SCSI_RSP_INQUIRY_SIZE	36
#define UPIU_SCSI_RSP_CAPACITY16_SIZE	32
#define UPIU_SCSI_RSP_CAPACITY_SIZE	8

struct upiu_hdr {
	uint8_t tc;			/* Transaction Code */
	uint8_t flags;
	uint8_t lun;
	uint8_t task_tag;
	uint8_t cmd_set_type;
	uint8_t query;
	uint8_t response;
	uint8_t status;
	uint8_t ehs_len;
	uint8_t device_info;
	uint16_t ds_len;		/* Data Segment Length */
} __packed;

struct upiu_command {
	struct upiu_hdr hdr;
	uint32_t expected_xfer_len;
	uint8_t cdb[16];
} __packed;

struct upiu_response {
	struct upiu_hdr hdr;
	uint32_t residual_xfer_len;
	uint8_t cdb[16];
} __packed;

struct ufshci_ucd {
	struct upiu_command cmd;
	struct upiu_response rsp;
	struct ufshci_ucd_prdt prdt[UFSHCI_UCD_PRDT_MAX_SEGS];
} __packed __aligned(128);
