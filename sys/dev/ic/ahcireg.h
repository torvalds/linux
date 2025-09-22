/*	$OpenBSD: ahcireg.h,v 1.6 2024/04/23 13:09:21 jsg Exp $ */

/*
 * Copyright (c) 2006 David Gwynne <dlg@openbsd.org>
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

#define AHCI_REG_CAP		0x000 /* HBA Capabilities */
#define  AHCI_REG_CAP_NP(_r)		(((_r) & 0x1f)+1) /* Number of Ports */
#define  AHCI_REG_CAP_SXS		(1<<5) /* External SATA */
#define  AHCI_REG_CAP_EMS		(1<<6) /* Enclosure Mgmt */
#define  AHCI_REG_CAP_CCCS		(1<<7) /* Cmd Coalescing */
#define  AHCI_REG_CAP_NCS(_r)		((((_r) & 0x1f00)>>8)+1) /* NCmds*/
#define  AHCI_REG_CAP_PSC		(1<<13) /* Partial State Capable */
#define  AHCI_REG_CAP_SSC		(1<<14) /* Slumber State Capable */
#define  AHCI_REG_CAP_PMD		(1<<15) /* PIO Multiple DRQ Block */
#define  AHCI_REG_CAP_FBSS		(1<<16) /* FIS-Based Switching */
#define  AHCI_REG_CAP_SPM		(1<<17) /* Port Multiplier */
#define  AHCI_REG_CAP_SAM		(1<<18) /* AHCI Only mode */
#define  AHCI_REG_CAP_SNZO		(1<<19) /* Non Zero DMA Offsets */
#define  AHCI_REG_CAP_ISS		(0xf<<20) /* Interface Speed Support */
#define  AHCI_REG_CAP_ISS_G1		(0x1<<20) /* Gen 1 (1.5 Gbps) */
#define  AHCI_REG_CAP_ISS_G2		(0x2<<20) /* Gen 2 (3 Gbps) */
#define  AHCI_REG_CAP_ISS_G3		(0x3<<20) /* Gen 3 (6 Gbps) */
#define  AHCI_REG_CAP_SCLO		(1<<24) /* Cmd List Override */
#define  AHCI_REG_CAP_SAL		(1<<25) /* Activity LED */
#define  AHCI_REG_CAP_SALP		(1<<26) /* Aggressive Link Pwr Mgmt */
#define  AHCI_REG_CAP_SSS		(1<<27) /* Staggered Spinup */
#define  AHCI_REG_CAP_SMPS		(1<<28) /* Mech Presence Switch */
#define  AHCI_REG_CAP_SSNTF		(1<<29) /* SNotification Register */
#define  AHCI_REG_CAP_SNCQ		(1<<30) /* Native Cmd Queuing */
#define  AHCI_REG_CAP_S64A		(1U<<31) /* 64bit Addressing */
#define  AHCI_FMT_CAP		"\020" "\040S64A" "\037NCQ" "\036SSNTF" \
				    "\035SMPS" "\034SSS" "\033SALP" "\032SAL" \
				    "\031SCLO" "\024SNZO" "\023SAM" "\022SPM" \
				    "\021FBSS" "\020PMD" "\017SSC" "\016PSC" \
				    "\010CCCS" "\007EMS" "\006SXS"
#define AHCI_REG_GHC		0x004 /* Global HBA Control */
#define  AHCI_REG_GHC_HR		(1<<0) /* HBA Reset */
#define  AHCI_REG_GHC_IE		(1<<1) /* Interrupt Enable */
#define  AHCI_REG_GHC_MRSM		(1<<2) /* MSI Revert to Single Msg */
#define  AHCI_REG_GHC_AE		(1U<<31) /* AHCI Enable */
#define AHCI_FMT_GHC		"\020" "\040AE" "\003MRSM" "\002IE" "\001HR"
#define AHCI_REG_IS		0x008 /* Interrupt Status */
#define AHCI_REG_PI		0x00c /* Ports Implemented */
#define AHCI_REG_VS		0x010 /* AHCI Version */
#define  AHCI_REG_VS_0_95		0x00000905 /* 0.95 */
#define  AHCI_REG_VS_1_0		0x00010000 /* 1.0 */
#define  AHCI_REG_VS_1_1		0x00010100 /* 1.1 */
#define  AHCI_REG_VS_1_2		0x00010200 /* 1.2 */
#define  AHCI_REG_VS_1_3		0x00010300 /* 1.3 */
#define  AHCI_REG_VS_1_3_1		0x00010301 /* 1.3.1 */
#define AHCI_REG_CCC_CTL	0x014 /* Coalescing Control */
#define  AHCI_REG_CCC_CTL_INT(_r)	(((_r) & 0xf8) >> 3) /* CCC INT slot */
#define AHCI_REG_CCC_PORTS	0x018 /* Coalescing Ports */
#define AHCI_REG_EM_LOC		0x01c /* Enclosure Mgmt Location */
#define AHCI_REG_EM_CTL		0x020 /* Enclosure Mgmt Control */

#define AHCI_REG_CAP2		0x024 /* HBA Capabilities Extended */
#define  AHCI_REG_CAP2_DESO	(1<<5)  /* DevSlp from slumber only */
#define  AHCI_REG_CAP2_SADM	(1<<4)  /* Aggro DevSlp mgmt */
#define  AHCI_REG_CAP2_SDS	(1<<3)  /* Supports DevSlp */
#define  AHCI_REG_CAP2_APST	(1<<2)  /* Auto partial->slumber */
#define  AHCI_REG_CAP2_NVMP	(1<<1)  /* NVMHCI present */
#define  AHCI_REG_CAP2_BOH	(1<<0)  /* BIOS/OS handoff */
#define  AHCI_FMT_CAP2		"\020" "\006DESO" "\005SADM" "\004SDS" \
				    "\003APST" "\002NVMP" "\001BOH"

#define AHCI_PORT_REGION(_p)	(0x100 + ((_p) * 0x80))
#define AHCI_PORT_SIZE		0x80

#define AHCI_PREG_CLB		0x00 /* Cmd List Base Addr */
#define AHCI_PREG_CLBU		0x04 /* Cmd List Base Hi Addr */
#define AHCI_PREG_FB		0x08 /* FIS Base Addr */
#define AHCI_PREG_FBU		0x0c /* FIS Base Hi Addr */
#define AHCI_PREG_IS		0x10 /* Interrupt Status */
#define  AHCI_PREG_IS_DHRS		(1<<0) /* Device to Host FIS */
#define  AHCI_PREG_IS_PSS		(1<<1) /* PIO Setup FIS */
#define  AHCI_PREG_IS_DSS		(1<<2) /* DMA Setup FIS */
#define  AHCI_PREG_IS_SDBS		(1<<3) /* Set Device Bits FIS */
#define  AHCI_PREG_IS_UFS		(1<<4) /* Unknown FIS */
#define  AHCI_PREG_IS_DPS		(1<<5) /* Descriptor Processed */
#define  AHCI_PREG_IS_PCS		(1<<6) /* Port Change */
#define  AHCI_PREG_IS_DMPS		(1<<7) /* Device Mechanical Presence */
#define  AHCI_PREG_IS_PRCS		(1<<22) /* PhyRdy Change */
#define  AHCI_PREG_IS_IPMS		(1<<23) /* Incorrect Port Multiplier */
#define  AHCI_PREG_IS_OFS		(1<<24) /* Overflow */
#define  AHCI_PREG_IS_INFS		(1<<26) /* Interface Non-fatal Error */
#define  AHCI_PREG_IS_IFS		(1<<27) /* Interface Fatal Error */
#define  AHCI_PREG_IS_HBDS		(1<<28) /* Host Bus Data Error */
#define  AHCI_PREG_IS_HBFS		(1<<29) /* Host Bus Fatal Error */
#define  AHCI_PREG_IS_TFES		(1<<30) /* Task File Error */
#define  AHCI_PREG_IS_CPDS		(1U<<31) /* Cold Presence Detect */
#define AHCI_PFMT_IS		"\20" "\040CPDS" "\037TFES" "\036HBFS" \
				    "\035HBDS" "\034IFS" "\033INFS" "\031OFS" \
				    "\030IPMS" "\027PRCS" "\010DMPS" "\006DPS" \
				    "\007PCS" "\005UFS" "\004SDBS" "\003DSS" \
				    "\002PSS" "\001DHRS"
#define AHCI_PREG_IE		0x14 /* Interrupt Enable */
#define  AHCI_PREG_IE_DHRE		(1<<0) /* Device to Host FIS */
#define  AHCI_PREG_IE_PSE		(1<<1) /* PIO Setup FIS */
#define  AHCI_PREG_IE_DSE		(1<<2) /* DMA Setup FIS */
#define  AHCI_PREG_IE_SDBE		(1<<3) /* Set Device Bits FIS */
#define  AHCI_PREG_IE_UFE		(1<<4) /* Unknown FIS */
#define  AHCI_PREG_IE_DPE		(1<<5) /* Descriptor Processed */
#define  AHCI_PREG_IE_PCE		(1<<6) /* Port Change */
#define  AHCI_PREG_IE_DMPE		(1<<7) /* Device Mechanical Presence */
#define  AHCI_PREG_IE_PRCE		(1<<22) /* PhyRdy Change */
#define  AHCI_PREG_IE_IPME		(1<<23) /* Incorrect Port Multiplier */
#define  AHCI_PREG_IE_OFE		(1<<24) /* Overflow */
#define  AHCI_PREG_IE_INFE		(1<<26) /* Interface Non-fatal Error */
#define  AHCI_PREG_IE_IFE		(1<<27) /* Interface Fatal Error */
#define  AHCI_PREG_IE_HBDE		(1<<28) /* Host Bus Data Error */
#define  AHCI_PREG_IE_HBFE		(1<<29) /* Host Bus Fatal Error */
#define  AHCI_PREG_IE_TFEE		(1<<30) /* Task File Error */
#define  AHCI_PREG_IE_CPDE		(1U<<31) /* Cold Presence Detect */
#define AHCI_PFMT_IE		"\20" "\040CPDE" "\037TFEE" "\036HBFE" \
				    "\035HBDE" "\034IFE" "\033INFE" "\031OFE" \
				    "\030IPME" "\027PRCE" "\010DMPE" "\007PCE" \
				    "\006DPE" "\005UFE" "\004SDBE" "\003DSE" \
				    "\002PSE" "\001DHRE"
#define AHCI_PREG_CMD		0x18 /* Command and Status */
#define  AHCI_PREG_CMD_ST		(1<<0) /* Start */
#define  AHCI_PREG_CMD_SUD		(1<<1) /* Spin Up Device */
#define  AHCI_PREG_CMD_POD		(1<<2) /* Power On Device */
#define  AHCI_PREG_CMD_CLO		(1<<3) /* Command List Override */
#define  AHCI_PREG_CMD_FRE		(1<<4) /* FIS Receive Enable */
#define  AHCI_PREG_CMD_CCS(_r)		(((_r) >> 8) & 0x1f) /* Curr CmdSlot# */
#define  AHCI_PREG_CMD_MPSS		(1<<13) /* Mech Presence State */
#define  AHCI_PREG_CMD_FR		(1<<14) /* FIS Receive Running */
#define  AHCI_PREG_CMD_CR		(1<<15) /* Command List Running */
#define  AHCI_PREG_CMD_CPS		(1<<16) /* Cold Presence State */
#define  AHCI_PREG_CMD_PMA		(1<<17) /* Port Multiplier Attached */
#define  AHCI_PREG_CMD_HPCP		(1<<18) /* Hot Plug Capable */
#define  AHCI_PREG_CMD_MPSP		(1<<19) /* Mech Presence Switch */
#define  AHCI_PREG_CMD_CPD		(1<<20) /* Cold Presence Detection */
#define  AHCI_PREG_CMD_ESP		(1<<21) /* External SATA Port */
#define  AHCI_PREG_CMD_ATAPI		(1<<24) /* Device is ATAPI */
#define  AHCI_PREG_CMD_DLAE		(1<<25) /* Drv LED on ATAPI Enable */
#define  AHCI_PREG_CMD_ALPE		(1<<26) /* Aggro Pwr Mgmt Enable */
#define  AHCI_PREG_CMD_ASP		(1<<27) /* Aggro Slumber/Partial */
#define  AHCI_PREG_CMD_ICC		0xf0000000 /* Interface Comm Ctrl */
#define  AHCI_PREG_CMD_ICC_SLUMBER	0x60000000
#define  AHCI_PREG_CMD_ICC_PARTIAL	0x20000000
#define  AHCI_PREG_CMD_ICC_ACTIVE	0x10000000
#define  AHCI_PREG_CMD_ICC_IDLE		0x00000000
#define  AHCI_PFMT_CMD		"\020" "\034ASP" "\033ALPE" "\032DLAE" \
				    "\031ATAPI" "\026ESP" "\025CPD" "\024MPSP" \
				    "\023HPCP" "\022PMA" "\021CPS" "\020CR" \
				    "\017FR" "\016MPSS" "\005FRE" "\004CLO" \
				    "\003POD" "\002SUD" "\001ST"
#define AHCI_PREG_TFD		0x20 /* Task File Data*/
#define  AHCI_PREG_TFD_STS		0xff
#define  AHCI_PREG_TFD_STS_ERR		(1<<0)
#define  AHCI_PREG_TFD_STS_DRQ		(1<<3)
#define  AHCI_PREG_TFD_STS_BSY		(1<<7)
#define  AHCI_PREG_TFD_ERR		0xff00
#define AHCI_PFMT_TFD_STS	"\20" "\010BSY" "\004DRQ" "\001ERR"
#define AHCI_PREG_SIG		0x24 /* Signature */
#define AHCI_PREG_SSTS		0x28 /* SATA Status */
#define  AHCI_PREG_SSTS_DET		0xf /* Device Detection */
#define  AHCI_PREG_SSTS_DET_NONE	0x0
#define  AHCI_PREG_SSTS_DET_DEV_NE	0x1
#define  AHCI_PREG_SSTS_DET_DEV		0x3
#define  AHCI_PREG_SSTS_DET_PHYOFFLINE	0x4
#define  AHCI_PREG_SSTS_SPD		0xf0 /* Current Interface Speed */
#define  AHCI_PREG_SSTS_SPD_NONE	0x00
#define  AHCI_PREG_SSTS_SPD_GEN1	0x10
#define  AHCI_PREG_SSTS_SPD_GEN2	0x20
#define  AHCI_PREG_SSTS_SPD_GEN3	0x30
#define  AHCI_PREG_SSTS_IPM		0xf00 /* Interface Power Management */
#define  AHCI_PREG_SSTS_IPM_NONE	0x000
#define  AHCI_PREG_SSTS_IPM_ACTIVE	0x100
#define  AHCI_PREG_SSTS_IPM_PARTIAL	0x200
#define  AHCI_PREG_SSTS_IPM_SLUMBER	0x600
#define AHCI_PREG_SCTL		0x2c /* SATA Control */
#define  AHCI_PREG_SCTL_DET		0xf /* Device Detection */
#define  AHCI_PREG_SCTL_DET_NONE	0x0
#define  AHCI_PREG_SCTL_DET_INIT	0x1
#define  AHCI_PREG_SCTL_DET_DISABLE	0x4
#define  AHCI_PREG_SCTL_SPD		0xf0 /* Speed Allowed */
#define  AHCI_PREG_SCTL_SPD_ANY		0x00
#define  AHCI_PREG_SCTL_SPD_GEN1	0x10
#define  AHCI_PREG_SCTL_SPD_GEN2	0x20
#define  AHCI_PREG_SCTL_SPD_GEN3	0x30
#define  AHCI_PREG_SCTL_IPM		0xf00 /* Interface Power Management */
#define  AHCI_PREG_SCTL_IPM_NONE	0x000
#define  AHCI_PREG_SCTL_IPM_NOPARTIAL	0x100
#define  AHCI_PREG_SCTL_IPM_NOSLUMBER	0x200
#define  AHCI_PREG_SCTL_IPM_DISABLED	0x300
#define AHCI_PREG_SERR		0x30 /* SATA Error */
#define  AHCI_PREG_SERR_ERR(_r)		((_r) & 0xffff)
#define  AHCI_PREG_SERR_ERR_I		(1<<0) /* Recovered Data Integrity */
#define  AHCI_PREG_SERR_ERR_M		(1<<1) /* Recovered Communications */
#define  AHCI_PREG_SERR_ERR_T		(1<<8) /* Transient Data Integrity */
#define  AHCI_PREG_SERR_ERR_C		(1<<9) /* Persistent Comm/Data */
#define  AHCI_PREG_SERR_ERR_P		(1<<10) /* Protocol */
#define  AHCI_PREG_SERR_ERR_E		(1<<11) /* Internal */
#define  AHCI_PFMT_SERR_ERR	"\020" "\014E" "\013P" "\012C" "\011T" "\002M" \
				    "\001I"
#define  AHCI_PREG_SERR_DIAG(_r)	(((_r) >> 16) & 0xffff)
#define  AHCI_PREG_SERR_DIAG_N		(1<<0) /* PhyRdy Change */
#define  AHCI_PREG_SERR_DIAG_I		(1<<1) /* Phy Internal Error */
#define  AHCI_PREG_SERR_DIAG_W		(1<<2) /* Comm Wake */
#define  AHCI_PREG_SERR_DIAG_B		(1<<3) /* 10B to 8B Decode Error */
#define  AHCI_PREG_SERR_DIAG_D		(1<<4) /* Disparity Error */
#define  AHCI_PREG_SERR_DIAG_C		(1<<5) /* CRC Error */
#define  AHCI_PREG_SERR_DIAG_H		(1<<6) /* Handshake Error */
#define  AHCI_PREG_SERR_DIAG_S		(1<<7) /* Link Sequence Error */
#define  AHCI_PREG_SERR_DIAG_T		(1<<8) /* Transport State Trans Err */
#define  AHCI_PREG_SERR_DIAG_F		(1<<9) /* Unknown FIS Type */
#define  AHCI_PREG_SERR_DIAG_X		(1<<10) /* Exchanged */
#define  AHCI_PFMT_SERR_DIAG	"\020" "\013X" "\012F" "\011T" "\010S" "\007H" \
				    "\006C" "\005D" "\004B" "\003W" "\002I" \
				    "\001N"
#define AHCI_PREG_SACT		0x34 /* SATA Active */
#define AHCI_PREG_CI		0x38 /* Command Issue */
#define  AHCI_PREG_CI_ALL_SLOTS	0xffffffff
#define AHCI_PREG_SNTF		0x3c /* SNotification */

#define AHCI_PREG_FBS		0x40 /* FIS-based Switching Control */
#define AHCI_PREG_FBS_DWE	0xf0000	/* Device With Error */
#define AHCI_PREG_FBS_ADO	0xf000	/* Active Device Optimization */
#define AHCI_PREG_FBS_DEV	0xf00	/* Device To Issue */
#define AHCI_PREG_FBS_SDE	(1<<2)	/* Single Device Error */
#define AHCI_PREG_FBS_DEC	(1<<1)	/* Device Error Clear */
#define AHCI_PREG_FBS_EN	(1<<0)	/* Enable */

struct ahci_cmd_hdr {
	u_int16_t		flags;
#define AHCI_CMD_LIST_FLAG_CFL		0x001f /* Command FIS Length */
#define AHCI_CMD_LIST_FLAG_A		(1<<5) /* ATAPI */
#define AHCI_CMD_LIST_FLAG_W		(1<<6) /* Write */
#define AHCI_CMD_LIST_FLAG_P		(1<<7) /* Prefetchable */
#define AHCI_CMD_LIST_FLAG_R		(1<<8) /* Reset */
#define AHCI_CMD_LIST_FLAG_B		(1<<9) /* BIST */
#define AHCI_CMD_LIST_FLAG_C		(1<<10) /* Clear Busy upon R_OK */
#define AHCI_CMD_LIST_FLAG_PMP		0xf000 /* Port Multiplier Port */
#define AHCI_CMD_LIST_FLAG_PMP_SHIFT	12
	u_int16_t		prdtl; /* sgl len */

	u_int32_t		prdbc; /* transferred byte count */

	u_int64_t		ctba;

	u_int32_t		reserved[4];
} __packed __aligned(8);

struct ahci_rfis {
	u_int8_t		dsfis[28];
	u_int8_t		reserved1[4];
	u_int8_t		psfis[24];
	u_int8_t		reserved2[8];
	u_int8_t		rfis[24];
	u_int8_t		reserved3[4];
	u_int8_t		sdbfis[4];
	u_int8_t		ufis[64];
	u_int8_t		reserved4[96];
} __packed;

struct ahci_prdt {
	u_int64_t		dba;
	u_int32_t		reserved;
	u_int32_t		flags;
#define AHCI_PRDT_FLAG_INTR		(1U<<31) /* interrupt on completion */
} __packed __aligned(8);

/* this makes ahci_cmd_table 512 bytes, supporting 128-byte alignment */
#define AHCI_MAX_PRDT		24

struct ahci_cmd_table {
	u_int8_t		cfis[64];	/* Command FIS */
	u_int8_t		acmd[16];	/* ATAPI Command */
	u_int8_t		reserved[48];

	struct ahci_prdt	prdt[AHCI_MAX_PRDT];
} __packed __aligned(128);

#define AHCI_MAX_PORTS		32
