/*	$OpenBSD: adwmcode.h,v 1.4 2008/06/26 05:42:16 ray Exp $ */
/*      $NetBSD: adwmcode.h,v 1.5 2000/05/27 18:24:51 dante Exp $        */

/*
 * Generic driver definitions and exported functions for the Advanced
 * Systems Inc. SCSI controllers
 * 
 * Copyright (c) 1998, 1999, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Author: Baldassare Dante Profeta <dante@mclink.it>
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ADW_MCODE_H
#define ADW_MCODE_H

/******************************************************************************/

#define ADW_MAX_CARRIER	253	/* Max. number of host commands (253) */

/*
 * ADW_CARRIER must be exactly 16 BYTES
 * Every adw_carrier structure _MUST_ always be aligned on a 16 bytes boundary
 */
struct adw_carrier {
/* ---------- the microcode wants the field below ---------- */
	u_int32_t	carr_id;  /* Carrier ID */
	u_int32_t	carr_ba;  /* Carrier Bus Address */
	u_int32_t	areq_ba;  /* ADW_SCSI_REQ_Q Bus Address */
	/*
	 * next_ba [31:4]	Carrier Physical Next Pointer
	 *
	 * next_ba [3:1]	Reserved Bits
	 * next_ba [0]		Done Flag set in Response Queue.
	 */
	u_int32_t	next_ba;  /* see next_ba flags below */
/* ----------                                     ---------- */
};

typedef struct adw_carrier ADW_CARRIER;

/*
 * next_ba flags
 */
#define ADW_RQ_DONE		0x00000001
#define ADW_RQ_GOOD		0x00000002
#define ADW_CQ_STOPPER		0x00000000

/*
 * Mask used to eliminate low 4 bits of carrier 'next_ba' field.
 */
#define ADW_NEXT_BA_MASK	0xFFFFFFF0
#define ADW_GET_CARRP(carrp)	((carrp) & ADW_NEXT_BA_MASK)

/*
 * Bus Address of a Carrier.
 * ba = base_ba + v_address - base_va
 */
#define	ADW_CARRIER_BADDR(dmamap, carriers, x)	((dmamap)->dm_segs[0].ds_addr +\
			(((u_long)x) - ((u_long)(carriers))))
/*
 * Virtual Address of a Carrier.
 * va = base_va + bus_address - base_ba
 */
#define	ADW_CARRIER_VADDR(sc, x)	((ADW_CARRIER *) \
			(((u_int8_t *)(sc)->sc_control->carriers) + \
			((u_long)x) - \
			(sc)->sc_dmamap_carrier->dm_segs[0].ds_addr))

/******************************************************************************/

struct adw_mcode {
	const u_int8_t	*mcode_data;
	const u_int32_t	 mcode_chksum;
	const u_int16_t	 mcode_size;
};


/******************************************************************************/

/*
 * Fixed locations of microcode operating variables.
 */
#define ADW_MC_CODE_BEGIN_ADDR		0x0028 /* microcode start address */
#define ADW_MC_CODE_END_ADDR		0x002A /* microcode end address */
#define ADW_MC_CODE_CHK_SUM		0x002C /* microcode code checksum */
#define ADW_MC_VERSION_DATE		0x0038 /* microcode version */
#define ADW_MC_VERSION_NUM		0x003A /* microcode number */
#define ADW_MC_BIOSMEM			0x0040 /* BIOS RISC Memory Start */
#define ADW_MC_BIOSLEN			0x0050 /* BIOS RISC Memory Length */
#define ADW_MC_BIOS_SIGNATURE		0x0058 /* BIOS Signature 0x55AA */
#define ADW_MC_BIOS_VERSION		0x005A /* BIOS Version (2 bytes) */

#define ADW_MC_SDTR_SPEED1		0x0090 /* SDTR Speed for TID 0-3 */
#define ADW_MC_SDTR_SPEED2		0x0092 /* SDTR Speed for TID 4-7 */
#define ADW_MC_SDTR_SPEED3		0x0094 /* SDTR Speed for TID 8-11 */
#define ADW_MC_SDTR_SPEED4		0x0096 /* SDTR Speed for TID 12-15 */
					/*
					 * 4-bit speed  SDTR speed name
					 * ===========  ===============
					 * 0000b (0x0)  SDTR disabled
					 * 0001b (0x1)  5 MHz
					 * 0010b (0x2)  10 MHz
					 * 0011b (0x3)  20 MHz (Ultra)
					 * 0100b (0x4)  40 MHz (LVD/Ultra2)
					 * 0101b (0x5)  80 MHz (LVD2/Ultra3)
					 * 0110b (0x6)  Undefined
					 * ...
					 * 1111b (0xF)  Undefined
					 */
#define ADW_MC_CHIP_TYPE		0x009A
#define ADW_MC_INTRB_CODE		0x009B
#define ADW_MC_WDTR_ABLE		0x009C
#define ADW_MC_SDTR_ABLE		0x009E
#define ADW_MC_TAGQNG_ABLE		0x00A0
#define ADW_MC_DISC_ENABLE		0x00A2
#define ADW_MC_IDLE_CMD_STATUS		0x00A4
#define ADW_MC_IDLE_CMD			0x00A6
#define ADW_MC_IDLE_CMD_PARAMETER	0x00A8
#define ADW_MC_DEFAULT_SCSI_CFG0	0x00AC
#define ADW_MC_DEFAULT_SCSI_CFG1	0x00AE
#define ADW_MC_DEFAULT_MEM_CFG		0x00B0
#define ADW_MC_DEFAULT_SEL_MASK		0x00B2
#define ADW_MC_SDTR_DONE 		0x00B6
#define ADW_MC_NUMBER_OF_QUEUED_CMD	0x00C0
#define ADW_MC_NUMBER_OF_MAX_CMD	0x00D0
#define ADW_MC_DEVICE_HSHK_CFG_TABLE	0x0100
#define ADW_MC_CONTROL_FLAG 		0x0122 /* Microcode control flag. */
#define ADW_MC_WDTR_DONE 		0x0124
#define ADW_MC_CAM_MODE_MASK		0x015E /* CAM mode TID bitmask. */
#define ADW_MC_ICQ			0x0160
#define ADW_MC_IRQ			0x0164
#define ADW_MC_PPR_ABLE			0x017A


/*
 * Microcode Control Flags
 *
 * Flags set by the Adw Library in RISC variable 'control_flag' (0x122)
 * and handled by the microcode.
 */
#define CONTROL_FLAG_IGNORE_PERR	0x0001 /* Ignore DMA Parity Errors */
#define CONTROL_FLAG_ENABLE_AIPP	0x0002 /* Enabled AIPP checking. */


/*
 * ADW_MC_DEVICE_HSHK_CFG_TABLE microcode table or HSHK_CFG register format
 */
#define HSHK_CFG_WIDE_XFR	0x8000
#define HSHK_CFG_RATE		0x0F00
#define HSHK_CFG_OFFSET		0x001F

#define ADW_DEF_MAX_HOST_QNG	0xFD /* Max. number of host commands (253) */
#define ADW_DEF_MIN_HOST_QNG	0x10 /* Min. number of host commands (16) */
#define ADW_DEF_MAX_DVC_QNG	0x3F /* Max. number commands per device (63) */
#define ADW_DEF_MIN_DVC_QNG	0x04 /* Min. number commands per device (4) */

#define ADW_QC_DATA_CHECK	0x01 /* Require ADW_QC_DATA_OUT set or clear. */
#define ADW_QC_DATA_OUT		0x02 /* Data out DMA transfer. */
#define ADW_QC_START_MOTOR	0x04 /* Send auto-start motor before request. */
#define ADW_QC_NO_OVERRUN	0x08 /* Don't report overrun. */
#define ADW_QC_FREEZE_TIDQ	0x10 /* Freeze TID queue after request.XXX TBD*/

#define ADW_QSC_NO_DISC		0x01 /* Don't allow disconnect for request. */
#define ADW_QSC_NO_TAGMSG	0x02 /* Don't allow tag queuing for request. */
#define ADW_QSC_NO_SYNC		0x04 /* Don't use Synch. transfer on request. */
#define ADW_QSC_NO_WIDE		0x08 /* Don't use Wide transfer on request. */
#define ADW_QSC_REDO_DTR	0x10 /* Renegotiate WDTR/SDTR before request. */
/*
 * Note: If a Tag Message is to be sent and neither ADW_QSC_HEAD_TAG or
 * ADW_QSC_ORDERED_TAG is set, then a Simple Tag Message (0x20) is used.
 */
#define ADW_QSC_HEAD_TAG	0x40 /* Use Head Tag Message (0x21). */
#define ADW_QSC_ORDERED_TAG	0x80 /* Use Ordered Tag Message (0x22). */


/******************************************************************************/

ADW_CARRIER *AdwInitCarriers(bus_dmamap_t, ADW_CARRIER *);

extern const struct adw_mcode adw_asc3550_mcode_data;
extern const struct adw_mcode adw_asc38C0800_mcode_data;
extern const struct adw_mcode adw_asc38C1600_mcode_data;
/******************************************************************************/

#endif	/* ADW_MCODE_H */
