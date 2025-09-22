/*	$OpenBSD: atareg.h,v 1.14 2010/07/23 07:47:13 jsg Exp $	*/
/*	$NetBSD: atareg.h,v 1.5 1999/01/18 20:06:24 bouyer Exp $	*/

/*
 * Copyright (c) 1998, 2001 Manuel Bouyer.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,     
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_ATA_ATAREG_H_
#define _DEV_ATA_ATAREG_H_

/*
 * Drive parameter structure for ATA/ATAPI.
 * Bit fields: WDC_* : common to ATA/ATAPI
 *             ATA_* : ATA only
 *             ATAPI_* : ATAPI only.
 */
struct ataparams {
    /* drive info */
    u_int16_t	atap_config;		/* 0: general configuration */
#define WDC_CFG_ATAPI_MASK		0xc000
#define WDC_CFG_ATAPI			0x8000
#define ATA_CFG_REMOVABLE		0x0080
#define ATA_CFG_FIXED			0x0040
#define ATAPI_CFG_TYPE_MASK		0x1f00
#define ATAPI_CFG_TYPE(x)		(((x) & ATAPI_CFG_TYPE_MASK) >> 8)
#define ATAPI_CFG_TYPE_DIRECT		0x00
#define ATAPI_CFG_TYPE_SEQUENTIAL	0x01
#define ATAPI_CFG_TYPE_CDROM		0x05
#define ATAPI_CFG_TYPE_OPTICAL		0x07
#define ATAPI_CFG_TYPE_NODEVICE		0x1F
#define ATAPI_CFG_REMOV			0x0080
#define ATAPI_CFG_DRQ_MASK		0x0060
#define ATAPI_CFG_STD_DRQ		0x0000
#define ATAPI_CFG_IRQ_DRQ		0x0020
#define ATAPI_CFG_ACCEL_DRQ		0x0040
#define ATAPI_CFG_CMD_MASK		0x0003
#define ATAPI_CFG_CMD_12		0x0000
#define ATAPI_CFG_CMD_16		0x0001
/* words 1-9 are ATA only */
    u_int16_t	atap_cylinders;		/* 1: # of non-removable cylinders */
    u_int16_t	__reserved1;
    u_int16_t	atap_heads;		/* 3: # of heads */
    u_int16_t	__retired1[2];		/* 4-5: # of unform. bytes/track */
    u_int16_t	atap_sectors;		/* 6: # of sectors */
    u_int16_t	__retired2[3];

    u_int8_t	atap_serial[20];	/* 10-19: serial number */
    u_int16_t	__retired3[2];
    u_int16_t	__obsolete1;
    u_int8_t	atap_revision[8];	/* 23-26: firmware revision */
    u_int8_t	atap_model[40];		/* 27-46: model number */
    u_int16_t	atap_multi;		/* 47: maximum sectors per irq (ATA) */
    u_int16_t	__reserved2;
    u_int16_t	atap_capabilities1;	/* 49: capability flags */
#define WDC_CAP_IORDY	0x0800
#define WDC_CAP_IORDY_DSBL 0x0400
#define WDC_CAP_LBA	0x0200
#define WDC_CAP_DMA	0x0100
#define ATA_CAP_STBY	0x2000
#define ATAPI_CAP_INTERL_DMA	0x8000
#define ATAPI_CAP_CMD_QUEUE	0x4000
#define ATAPI_CAP_OVERLP	0x2000
#define ATAPI_CAP_ATA_RST	0x1000
    u_int16_t	atap_capabilities2;	/* 50: capability flags (ATA) */
#if BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t	__junk2;
    u_int8_t	atap_oldpiotiming;	/* 51: old PIO timing mode */
    u_int8_t	__junk3;
    u_int8_t	atap_olddmatiming;	/* 52: old DMA timing mode (ATA) */
#else
    u_int8_t	atap_oldpiotiming;	/* 51: old PIO timing mode */
    u_int8_t	__junk2;
    u_int8_t	atap_olddmatiming;	/* 52: old DMA timing mode (ATA) */
    u_int8_t	__junk3;
#endif
    u_int16_t	atap_extensions;	/* 53: extensions supported */
#define WDC_EXT_UDMA_MODES	0x0004
#define WDC_EXT_MODES		0x0002
#define WDC_EXT_GEOM		0x0001
/* words 54-62 are ATA only */
    u_int16_t	atap_curcylinders;	/* 54: current logical cylinders */
    u_int16_t	atap_curheads;		/* 55: current logical heads */
    u_int16_t	atap_cursectors;	/* 56: current logical sectors/tracks */
    u_int16_t	atap_curcapacity[2];	/* 57-58: current capacity */
    u_int16_t	atap_curmulti;		/* 59: current multi-sector setting */
#define WDC_MULTI_VALID 0x0100
#define WDC_MULTI_MASK  0x00ff
    u_int16_t	atap_capacity[2];	/* 60-61: total capacity (LBA only) */
    u_int16_t	__retired4;
#if BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t	atap_dmamode_supp;	/* 63: multiword DMA mode supported */
    u_int8_t	atap_dmamode_act;	/*     multiword DMA mode active */
    u_int8_t	atap_piomode_supp;	/* 64: PIO mode supported */
    u_int8_t	__junk4;
#else
    u_int8_t	atap_dmamode_act;	/*     multiword DMA mode active */
    u_int8_t	atap_dmamode_supp;	/* 63: multiword DMA mode supported */
    u_int8_t	__junk4;
    u_int8_t	atap_piomode_supp;	/* 64: PIO mode supported */
#endif
    u_int16_t	atap_dmatiming_mimi;	/* 65: minimum DMA cycle time */
    u_int16_t	atap_dmatiming_recom;	/* 66: recommended DMA cycle time */
    u_int16_t	atap_piotiming;		/* 67: mini PIO cycle time without FC */
    u_int16_t	atap_piotiming_iordy;	/* 68: mini PIO cycle time with IORDY FC */
    u_int16_t	__reserved3[2];
/* words 71-72 are ATAPI only */
    u_int16_t	atap_pkt_br;		/* 71: time (ns) to bus release */
    u_int16_t	atap_pkt_bsyclr;	/* 72: tme to clear BSY after service */
    u_int16_t	__reserved4[2];
    u_int16_t	atap_queuedepth;	/* 75: */
#define WDC_QUEUE_DEPTH_MASK 0x1f
    u_int16_t	atap_sata_caps;		/* 76: SATA capabilities */
#define SATA_SIGNAL_GEN1	0x0002	/* SATA Gen-1 signaling speed */
#define SATA_SIGNAL_GEN2	0x0004	/* SATA Gen-2 signaling speed */
#define SATA_NATIVE_CMDQ	0x0100	/* native command queuing */
#define SATA_HOST_PWR_MGMT	0x0200	/* power management (host) */
    u_int16_t	atap_sata_reserved;	/* 77: reserved */
    u_int16_t	atap_sata_features_supp;/* 78: SATA features supported */
#define SATA_NONZERO_OFFSETS	0x0002	/* non-zero buffer offsets */
#define SATA_DMA_SETUP_AUTO	0x0004	/* DMA setup auto-activate */
#define SATA_DRIVE_PWR_MGMT	0x0008	/* power management (device) */
    u_int16_t	atap_sata_features_en;	/* 79: SATA features enabled */
    u_int16_t	atap_ata_major;		/* 80: Major version number */
#define WDC_VER_ATA1	0x0002
#define WDC_VER_ATA2	0x0004
#define WDC_VER_ATA3	0x0008
#define WDC_VER_ATA4	0x0010
#define WDC_VER_ATA5	0x0020
#define WDC_VER_ATA6	0x0040
#define WDC_VER_ATA7	0x0080
#define WDC_VER_ATA8	0x0100
#define WDC_VER_ATA9	0x0200
#define WDC_VER_ATA10	0x0400
#define WDC_VER_ATA11	0x0800
#define WDC_VER_ATA12	0x1000
#define WDC_VER_ATA13	0x2000
#define WDC_VER_ATA14	0x4000
    u_int16_t	atap_ata_minor;		/* 81: Minor version number */
    u_int16_t	atap_cmd_set1;		/* 82: command set supported */
#define WDC_CMD1_NOP	0x4000
#define WDC_CMD1_RB	0x2000
#define WDC_CMD1_WB	0x1000
#define WDC_CMD1_HPA	0x0400
#define WDC_CMD1_DVRST	0x0200
#define WDC_CMD1_SRV	0x0100
#define WDC_CMD1_RLSE	0x0080
#define WDC_CMD1_AHEAD	0x0040
#define WDC_CMD1_CACHE	0x0020
#define WDC_CMD1_PKT	0x0010
#define WDC_CMD1_PM	0x0008
#define WDC_CMD1_REMOV	0x0004
#define WDC_CMD1_SEC	0x0002
#define WDC_CMD1_SMART	0x0001
    u_int16_t	atap_cmd_set2;		/* 83: command set supported */
#define ATAPI_CMD2_FCE	0x2000 /* Flush Cache Ext supported */
#define ATAPI_CMD2_FC	0x1000 /* Flush Cache supported */
#define ATAPI_CMD2_DCO	0x0800 /* Device Configuration Overlay supported */
#define ATAPI_CMD2_48AD	0x0400 /* 48bit address supported */
#define ATAPI_CMD2_AAM	0x0200 /* Automatic Acoustic Management supported */
#define ATAPI_CMD2_SM	0x0100 /* Set Max security extension supported */
#define ATAPI_CMD2_SF	0x0040 /* Set Features subcommand required */
#define ATAPI_CMD2_PUIS	0x0020 /* Power up in standby supported */
#define WDC_CMD2_RMSN	0x0010
#define ATA_CMD2_APM	0x0008
#define ATA_CMD2_CFA	0x0004
#define ATA_CMD2_RWQ	0x0002
#define WDC_CMD2_DM	0x0001 /* Download Microcode supported */
    u_int16_t	atap_cmd_ext;		/* 84: command/features supp. ext. */
#define ATAPI_CMDE_IIUF	0x2000 /* IDLE IMMEDIATE with UNLOAD FEATURE */	
#define ATAPI_CMDE_MSER	0x0004 /* Media serial number supported */
#define ATAPI_CMDE_TEST	0x0002 /* SMART self-test supported */
#define ATAPI_CMDE_SLOG	0x0001 /* SMART error logging supported */
    u_int16_t	atap_cmd1_en;		/* 85: cmd/features enabled */
/* bits are the same as atap_cmd_set1 */
    u_int16_t	atap_cmd2_en;		/* 86: cmd/features enabled */
/* bits are the same as atap_cmd_set2 */
    u_int16_t	atap_cmd_def;		/* 87: cmd/features default */
/* bits are NOT the same as atap_cmd_ext */
#if BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t	atap_udmamode_supp;	/* 88: Ultra-DMA mode supported */
    u_int8_t	atap_udmamode_act;	/*     Ultra-DMA mode active */
#else
    u_int8_t	atap_udmamode_act;	/*     Ultra-DMA mode active */
    u_int8_t	atap_udmamode_supp;	/* 88: Ultra-DMA mode supported */
#endif
/* 89-92 are ATA-only */
    u_int16_t	atap_seu_time;		/* 89: Sec. Erase Unit compl. time */
    u_int16_t	atap_eseu_time;		/* 90: Enhanced SEU compl. time */
    u_int16_t	atap_apm_val;		/* 91: current APM value */
    u_int16_t	atap_mpasswd_rev;	/* 92: Master Password revision */
    u_int16_t	atap_hwreset_res;	/* 93: Hardware reset value */
#define ATA_HWRES_CBLID    0x2000  /* CBLID above Vih */
#define ATA_HWRES_D1_PDIAG 0x0800  /* Device 1 PDIAG detect OK */
#define ATA_HWRES_D1_CSEL  0x0400  /* Device 1 used CSEL for address */
#define ATA_HWRES_D1_JUMP  0x0200  /* Device 1 jumpered to address */
#define ATA_HWRES_D0_SEL   0x0040  /* Device 0 responds when Dev 1 selected */
#define ATA_HWRES_D0_DASP  0x0020  /* Device 0 DASP detect OK */
#define ATA_HWRES_D0_PDIAG 0x0010  /* Device 0 PDIAG detect OK */
#define ATA_HWRES_D0_DIAG  0x0008  /* Device 0 diag OK */
#define ATA_HWRES_D0_CSEL  0x0004  /* Device 0 used CSEL for address */
#define ATA_HWRES_D0_JUMP  0x0002  /* Device 0 jumpered to address */
#if BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t	atap_acoustic_val;	/* 94: Current acoustic level */
    u_int8_t	atap_acoustic_def;	/*     recommended level */
#else
    u_int8_t	atap_acoustic_def;	/*     recommended level */
    u_int8_t	atap_acoustic_val;	/* 94: Current acoustic level */
#endif
    u_int16_t	__reserved6[5];		/* 95-99: reserved */
    u_int16_t	atap_max_lba[4];	/* 100-103: Max. user LBA add */
    u_int16_t	__reserved7[23];	/* 104-126: reserved */
    u_int16_t	atap_rmsn_supp;		/* 127: remov. media status notif. */
#define WDC_RMSN_SUPP_MASK 0x0003
#define WDC_RMSN_SUPP 0x0001
    u_int16_t	atap_sec_st;		/* 128: security status */
#define WDC_SEC_LEV_MAX	0x0100
#define WDC_SEC_ESE_SUPP 0x0020
#define WDC_SEC_EXP	0x0010
#define WDC_SEC_FROZEN	0x0008
#define WDC_SEC_LOCKED	0x0004
#define WDC_SEC_EN	0x0002
#define WDC_SEC_SUPP	0x0001
    u_int16_t	__reserved8[31];	/* 129-159: vendor specific */
    u_int16_t	atap_cfa_power;		/* 160: CFA powermode */
#define ATAPI_CFA_MAX_MASK  0x0FFF
#define ATAPI_CFA_MODE1_DIS 0x1000 /* CFA Mode 1 Disabled */
#define ATAPI_CFA_MODE1_REQ 0x2000 /* CFA Mode 1 Required */
#define ATAPI_CFA_WORD160   0x8000 /* Word 160 supported */
    u_int16_t	__reserved9[15];	/* 161-175: reserved for CFA */
    u_int8_t	atap_media_serial[60];	/* 176-205: media serial number */
    u_int16_t	__reserved10[49];	/* 206-254: reserved */
#if BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t	atap_signature;		/* 255: Signature */
    u_int8_t	atap_checksum;		/*      Checksum */
#else
    u_int8_t	atap_checksum;		/*      Checksum */
    u_int8_t	atap_signature;		/* 255: Signature */
#endif
};

#endif	/* !_DEV_ATA_ATAREG_H_ */
