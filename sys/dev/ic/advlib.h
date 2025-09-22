/*	$OpenBSD: advlib.h,v 1.12 2020/07/22 13:16:04 krw Exp $	*/
/*      $NetBSD: advlib.h,v 1.5 1998/10/28 20:39:46 dante Exp $        */

/*
 * Definitions for low level routines and data structures
 * for the Advanced Systems Inc. SCSI controllers chips.
 *
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
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
/*
 * Ported from:
 */
/*
 * advansys.c - Linux Host Driver for AdvanSys SCSI Adapters
 *     
 * Copyright (c) 1995-1996 Advanced System Products, Inc.
 * All Rights Reserved.
 *   
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 */

#ifndef	_ADVANSYS_NARROW_LIBRARY_H_
#define	_ADVANSYS_NARROW_LIBRARY_H_

#include <dev/ic/adv.h>

/******************************************************************************/

#define ADV_VERSION	"3.1E"		/* AdvanSys Driver Version */

#define ASC_LIB_VERSION_MAJOR  1
#define ASC_LIB_VERSION_MINOR  22
#define ASC_LIB_SERIAL_NUMBER  113


#define ASC_NOERROR	1
#define ASC_BUSY	0
#define ASC_ERROR	-1


#if BYTE_ORDER == BIG_ENDIAN
#define LO_BYTE(x)	(*((u_int8_t *)(&(x))+1))
#define HI_BYTE(x)	(*((u_int8_t *)&(x)))
#define LO_WORD(x)	(*((u_int16_t *)(&(x))+1))
#define HI_WORD(x)	(*((u_int16_t *)&(x)))
#else
#define HI_BYTE(x)	(*((u_int8_t *)(&(x))+1))
#define LO_BYTE(x)	(*((u_int8_t *)&(x)))
#define HI_WORD(x)	(*((u_int16_t *)(&(x))+1))
#define LO_WORD(x)	(*((u_int16_t *)&(x)))
#endif

#define MAKEWORD(lo, hi)	((u_int16_t) (((u_int16_t) (lo)) | \
				((u_int16_t) (hi) << 8)))

#define MAKELONG(lo, hi)	((u_int32_t) (((u_int32_t) (lo)) | \
				((u_int32_t) (hi) << 16)))

#define SWAPWORDS(dWord)	((u_int32_t) ((dWord) >> 16) | ((dWord) << 16))
#define SWAPBYTES(word)		((u_int16_t) ((word) >> 8) | ((word) << 8))
#define	BIGTOLITTLE(dWord)	(u_int32_t)(SWAPBYTES(SWAPWORDS(dWord) >> 16 ) << 16) | \
				SWAPBYTES(SWAPWORDS(dWord) & 0xFFFF)
#define LITTLETOBIG(dWord)	BIGTOLITTLE(dWord)


#define ASC_PCI_ID2BUS(id)	((id) & 0xFF)
#define ASC_PCI_ID2DEV(id)	(((id) >> 11) & 0x1F)
#define ASC_PCI_ID2FUNC(id)	(((id) >> 8) & 0x7)
#define ASC_PCI_MKID(bus, dev, func)	((((dev) & 0x1F) << 11) | \
				(((func) & 0x7) << 8) | ((bus) & 0xFF))
#define ASC_PCI_REVISION_3150	0x02
#define ASC_PCI_REVISION_3050	0x03


#define ASC_MAX_SG_QUEUE	7
#define ASC_SG_LIST_PER_Q 	ASC_MAX_SG_QUEUE
#define ASC_MAX_SG_LIST		(1 + ((ASC_SG_LIST_PER_Q) * \
				(ASC_MAX_SG_QUEUE)))		/* SG_ALL */


#define ASC_IS_ISA		0x0001
#define ASC_IS_ISAPNP		0x0081
#define ASC_IS_EISA		0x0002
#define ASC_IS_PCI		0x0004
#define ASC_IS_PCI_ULTRA	0x0104
#define ASC_IS_PCMCIA		0x0008
#define ASC_IS_MCA		0x0020
#define ASC_IS_VL		0x0040


#define ASC_ISA_PNP_PORT_ADDR	0x279
#define ASC_ISA_PNP_PORT_WRITE	(ASC_ISA_PNP_PORT_ADDR+0x800)

#define ASC_IS_WIDESCSI_16	0x0100
#define ASC_IS_WIDESCSI_32	0x0200
#define ASC_IS_BIG_ENDIAN	0x8000


#define ASC_CHIP_MIN_VER_VL		0x01
#define ASC_CHIP_MAX_VER_VL		0x07
#define ASC_CHIP_MIN_VER_PCI		0x09
#define ASC_CHIP_MAX_VER_PCI		0x0F
#define ASC_CHIP_VER_PCI_BIT		0x08
#define ASC_CHIP_MIN_VER_ISA		0x11
#define ASC_CHIP_MIN_VER_ISA_PNP	0x21
#define ASC_CHIP_MAX_VER_ISA		0x27
#define ASC_CHIP_VER_ISA_BIT		0x30
#define ASC_CHIP_VER_ISAPNP_BIT		0x20
#define ASC_CHIP_VER_ASYN_BUG		0x21
#define ASC_CHIP_VER_PCI		0x08
#define ASC_CHIP_VER_PCI_ULTRA_3150	(ASC_CHIP_VER_PCI | 0x02)
#define ASC_CHIP_VER_PCI_ULTRA_3050	(ASC_CHIP_VER_PCI | 0x03)
#define ASC_CHIP_MIN_VER_EISA		0x41
#define ASC_CHIP_MAX_VER_EISA		0x47
#define ASC_CHIP_VER_EISA_BIT		0x40
#define ASC_CHIP_LATEST_VER_EISA	((ASC_CHIP_MIN_VER_EISA - 1) + 3)


#define ASC_MAX_VL_DMA_ADDR	0x07FFFFFFL
#define ASC_MAX_VL_DMA_COUNT	0x07FFFFFFL
#define ASC_MAX_PCI_DMA_ADDR	0xFFFFFFFFL
#define ASC_MAX_PCI_DMA_COUNT	0xFFFFFFFFL
#define ASC_MAX_ISA_DMA_ADDR	0x00FFFFFFL
#define ASC_MAX_ISA_DMA_COUNT	0x00FFFFFFL
#define ASC_MAX_EISA_DMA_ADDR	0x07FFFFFFL
#define ASC_MAX_EISA_DMA_COUNT	0x07FFFFFFL


#define ASC_SCSI_ID_BITS	3
#define ASC_SCSI_TIX_TYPE	u_int8_t

#define ASC_ALL_DEVICE_BIT_SET	0xFF

#ifdef ASC_WIDESCSI_16
#undef  ASC_SCSI_ID_BITS
#define ASC_SCSI_ID_BITS	4
#define ASC_ALL_DEVICE_BIT_SET	0xFFFF
#endif

#ifdef ASC_WIDESCSI_32
#undef  ASC_SCSI_ID_BITS
#define ASC_SCSI_ID_BITS	5
#define ASC_ALL_DEVICE_BIT_SET	0xFFFFFFFFL
#endif

#if ASC_SCSI_ID_BITS == 3
#define ASC_SCSI_BIT_ID_TYPE	u_int8_t
#define ASC_MAX_TID		7
#define ASC_MAX_LUN		7
#define ASC_SCSI_WIDTH_BIT_SET	0xFF
#elif ASC_SCSI_ID_BITS == 4
#define ASC_SCSI_BIT_ID_TYPE	u_int16_t
#define ASC_MAX_TID		15
#define ASC_MAX_LUN		7
#define ASC_SCSI_WIDTH_BIT_SET	0xFFFF
#elif ASC_SCSI_ID_BITS == 5
#define ASC_SCSI_BIT_ID_TYPE	u_int32_t
#define ASC_MAX_TID		31
#define ASC_MAX_LUN		7
#define ASC_SCSI_WIDTH_BIT_SET	0xFFFFFFFF
#else
#error  ASC_SCSI_ID_BITS definition is wrong
#endif


#define ASC_MAX_SENSE_LEN	32
#define ASC_MIN_SENSE_LEN	14
#define ASC_MAX_CDB_LEN		12

#define ASC_SCSI_RESET_HOLD_TIME_US  60


#define SCSICMD_TestUnitReady		0x00
#define SCSICMD_Rewind			0x01
#define SCSICMD_Rezero			0x01
#define SCSICMD_RequestSense		0x03
#define SCSICMD_Format			0x04
#define SCSICMD_FormatUnit		0x04
#define SCSICMD_Read6			0x08
#define SCSICMD_Write6			0x0A
#define SCSICMD_Seek6			0x0B
#define SCSICMD_Inquiry			0x12
#define SCSICMD_Verify6			0x13
#define SCSICMD_ModeSelect6		0x15
#define SCSICMD_ModeSense6		0x1A
#define SCSICMD_StartStopUnit		0x1B
#define SCSICMD_LoadUnloadTape		0x1B
#define SCSICMD_ReadCapacity		0x25
#define SCSICMD_Read10			0x28
#define SCSICMD_Write10			0x2A
#define SCSICMD_Seek10			0x2B
#define SCSICMD_Erase10			0x2C
#define SCSICMD_WriteAndVerify10	0x2E
#define SCSICMD_Verify10		0x2F
#define SCSICMD_WriteBuffer		0x3B
#define SCSICMD_ReadBuffer		0x3C
#define SCSICMD_ReadLong		0x3E
#define SCSICMD_WriteLong		0x3F
#define SCSICMD_ReadTOC			0x43
#define SCSICMD_ReadHeader		0x44
#define SCSICMD_ModeSelect10		0x55
#define SCSICMD_ModeSense10		0x5A


#define SCSI_TYPE_DASD		0x00
#define SCSI_TYPE_SASD		0x01
#define SCSI_TYPE_PRN		0x02
#define SCSI_TYPE_PROC		0x03
#define SCSI_TYPE_WORM		0x04
#define SCSI_TYPE_CDROM		0x05
#define SCSI_TYPE_SCANNER	0x06
#define SCSI_TYPE_OPTMEM	0x07
#define SCSI_TYPE_MED_CHG	0x08
#define SCSI_TYPE_COMM		0x09
#define SCSI_TYPE_UNKNOWN	0x1F
#define SCSI_TYPE_NO_DVC	0xFF


#define ASC_SCSIDIR_NOCHK	0x00
#define ASC_SCSIDIR_T2H		0x08
#define ASC_SCSIDIR_H2T		0x10
#define ASC_SCSIDIR_NODATA	0x18


#define SCSI_SENKEY_NO_SENSE		0x00
#define SCSI_SENKEY_UNDEFINED		0x01
#define SCSI_SENKEY_NOT_READY		0x02
#define SCSI_SENKEY_MEDIUM_ERR		0x03
#define SCSI_SENKEY_HW_ERR		0x04
#define SCSI_SENKEY_ILLEGAL		0x05
#define SCSI_SENKEY_ATTENTION		0x06
#define SCSI_SENKEY_PROTECTED		0x07
#define SCSI_SENKEY_BLANK		0x08
#define SCSI_SENKEY_V_UNIQUE		0x09
#define SCSI_SENKEY_CPY_ABORT		0x0A
#define SCSI_SENKEY_ABORT		0x0B
#define SCSI_SENKEY_EQUAL		0x0C
#define SCSI_SENKEY_VOL_OVERFLOW	0x0D
#define SCSI_SENKEY_MISCOMP		0x0E
#define SCSI_SENKEY_RESERVED		0x0F
#define SCSI_ASC_NOMEDIA		0x3A


#define ASC_CCB_HOST(x)  ((u_int8_t)((u_int8_t)(x) >> 4))
#define ASC_CCB_TID(x)   ((u_int8_t)((u_int8_t)(x) & (u_int8_t)0x0F))
#define ASC_CCB_LUN(x)   ((u_int8_t)((uint)(x) >> 13))


#define SS_GOOD				0x00
#define SS_CHK_CONDITION		0x02
#define SS_CONDITION_MET		0x04
#define SS_TARGET_BUSY			0x08
#define SS_INTERMID			0x10
#define SS_INTERMID_COND_MET		0x14
#define SS_RSERV_CONFLICT		0x18
#define SS_CMD_TERMINATED		0x22
#define SS_QUEUE_FULL			0x28


#define MS_CMD_DONE			0x00
#define MS_EXTEND			0x01
#define MS_SDTR_LEN			0x03
#define MS_SDTR_CODE			0x01
#define MS_WDTR_LEN			0x02
#define MS_WDTR_CODE			0x03
#define MS_MDP_LEN			0x05
#define MS_MDP_CODE			0x00


#define M1_SAVE_DATA_PTR		0x02
#define M1_RESTORE_PTRS			0x03
#define M1_DISCONNECT			0x04
#define M1_INIT_DETECTED_ERR		0x05
#define M1_ABORT			0x06
#define M1_MSG_REJECT			0x07
#define M1_NO_OP			0x08
#define M1_MSG_PARITY_ERR		0x09
#define M1_LINK_CMD_DONE		0x0A
#define M1_LINK_CMD_DONE_WFLAG		0x0B
#define M1_BUS_DVC_RESET		0x0C
#define M1_ABORT_TAG			0x0D
#define M1_CLR_QUEUE			0x0E
#define M1_INIT_RECOVERY		0x0F
#define M1_RELEASE_RECOVERY		0x10
#define M1_KILL_IO_PROC			0x11
#define M2_QTAG_MSG_SIMPLE		0x20
#define M2_QTAG_MSG_HEAD		0x21
#define M2_QTAG_MSG_ORDERED		0x22
#define M2_IGNORE_WIDE_RESIDUE		0x23


/*
 * SCSI Inquiry structure
 */

typedef struct
{
	u_int8_t	peri_dvc_type:5;
	u_int8_t	peri_qualifier:3;
} ASC_SCSI_INQ0;

typedef struct
{
	u_int8_t	dvc_type_modifier:7;
	u_int8_t	rmb:1;
} ASC_SCSI_INQ1;

typedef struct
{
	u_int8_t	ansi_apr_ver:3;
	u_int8_t	ecma_ver:3;
	u_int8_t	iso_ver:2;
} ASC_SCSI_INQ2;

typedef struct
{
	u_int8_t	rsp_data_fmt:4;
	u_int8_t	res:2;
	u_int8_t	TemIOP:1;
	u_int8_t	aenc:1;
} ASC_SCSI_INQ3;

typedef struct
{
	u_int8_t	StfRe:1;
	u_int8_t	CmdQue:1;
	u_int8_t	Reserved:1;
	u_int8_t	Linked:1;
	u_int8_t	Sync:1;
	u_int8_t	WBus16:1;
	u_int8_t	WBus32:1;
	u_int8_t	RelAdr:1;
} ASC_SCSI_INQ7;

typedef struct
{
	ASC_SCSI_INQ0	byte0;
	ASC_SCSI_INQ1	byte1;
	ASC_SCSI_INQ2	byte2;
	ASC_SCSI_INQ3	byte3;
	u_int8_t	add_len;
	u_int8_t	res1;
	u_int8_t	res2;
	ASC_SCSI_INQ7	byte7;
	u_int8_t	vendor_id[8];
	u_int8_t	product_id[16];
	u_int8_t	product_rev_level[4];
} ASC_SCSI_INQUIRY;


/*
 * SCSIQ Microcode offsets
 */
#define ASC_SCSIQ_CPY_BEG		 4
#define ASC_SCSIQ_SGHD_CPY_BEG		 2
#define ASC_SCSIQ_B_FWD			 0
#define ASC_SCSIQ_B_BWD			 1
#define ASC_SCSIQ_B_STATUS		 2
#define ASC_SCSIQ_B_QNO			 3
#define ASC_SCSIQ_B_CNTL		 4
#define ASC_SCSIQ_B_SG_QUEUE_CNT	 5
#define ASC_SCSIQ_D_DATA_ADDR		 8
#define ASC_SCSIQ_D_DATA_CNT		12
#define ASC_SCSIQ_B_SENSE_LEN		20
#define ASC_SCSIQ_DONE_INFO_BEG		22
#define ASC_SCSIQ_D_CCBPTR		22
#define ASC_SCSIQ_B_TARGET_IX		26
#define ASC_SCSIQ_B_CDB_LEN		28
#define ASC_SCSIQ_B_TAG_CODE		29
#define ASC_SCSIQ_W_VM_ID		30
#define ASC_SCSIQ_DONE_STATUS		32
#define ASC_SCSIQ_HOST_STATUS		33
#define ASC_SCSIQ_SCSI_STATUS		34
#define ASC_SCSIQ_CDB_BEG		36
#define ASC_SCSIQ_DW_REMAIN_XFER_ADDR	56
#define ASC_SCSIQ_DW_REMAIN_XFER_CNT 	60
#define ASC_SCSIQ_B_SG_WK_QP		49
#define ASC_SCSIQ_B_SG_WK_IX		50
#define ASC_SCSIQ_W_REQ_COUNT		52
#define ASC_SCSIQ_B_LIST_CNT		 6
#define ASC_SCSIQ_B_CUR_LIST_CNT	 7


#define ASC_DEF_SCSI1_QNG	4
#define ASC_MAX_SCSI1_QNG	4
#define ASC_DEF_SCSI2_QNG	16
#define ASC_MAX_SCSI2_QNG	32

#define ASC_TAG_CODE_MASK	0x23

#define ASC_STOP_REQ_RISC_STOP		0x01
#define ASC_STOP_ACK_RISC_STOP		0x03
#define ASC_STOP_CLEAN_UP_BUSY_Q	0x10
#define ASC_STOP_CLEAN_UP_DISC_Q	0x20
#define ASC_STOP_HOST_REQ_RISC_HALT	0x40

#define ASC_TIDLUN_TO_IX(tid, lun)	(ASC_SCSI_TIX_TYPE)((tid) + ((lun)<<ASC_SCSI_ID_BITS))
#define ASC_TID_TO_TARGET_ID(tid)	(ASC_SCSI_BIT_ID_TYPE)(0x01 << (tid))
#define ASC_TIX_TO_TARGET_ID(tix)	(0x01 << ((tix) & ASC_MAX_TID))
#define ASC_TIX_TO_TID(tix)		((tix) & ASC_MAX_TID)
#define ASC_TID_TO_TIX(tid)		((tid) & ASC_MAX_TID)
#define ASC_TIX_TO_LUN(tix)		(((tix) >> ASC_SCSI_ID_BITS) & ASC_MAX_LUN)
#define ASC_QNO_TO_QADDR(q_no)		((ASC_QADR_BEG)+((int)(q_no) << 6))


/*
 * Structures used to dialog with the RISC engine
 */

typedef struct asc_scisq_1
{
	u_int8_t	status;	/* see below status values */
	u_int8_t	q_no;	/* Queue ID of the first queue for this transaction */
	u_int8_t	cntl;	/* see below cntl values */
	u_int8_t	sg_queue_cnt;	/* number of SG entries */
	u_int8_t	target_id;
	u_int8_t	target_lun;
	u_int32_t	data_addr; /* physical address of first segment to transfer */
	u_int32_t	data_cnt;  /* byte count of first segment to transfer */
	u_int32_t	sense_addr; /* physical address of the sense buffer */
	u_int8_t	sense_len; /* length of sense buffer */
	u_int8_t	extra_bytes;
} ASC_SCSIQ_1;

/* status values */
#define ASC_QS_FREE		0x00
#define ASC_QS_READY		0x01
#define ASC_QS_DISC1		0x02
#define ASC_QS_DISC2		0x04
#define ASC_QS_BUSY		0x08
#define ASC_QS_ABORTED		0x40
#define ASC_QS_DONE		0x80

/* cntl values */
#define ASC_QC_NO_CALLBACK	0x01
#define ASC_QC_SG_SWAP_QUEUE	0x02
#define ASC_QC_SG_HEAD		0x04
#define ASC_QC_DATA_IN		0x08
#define ASC_QC_DATA_OUT		0x10
#define ASC_QC_URGENT		0x20
#define ASC_QC_MSG_OUT		0x40
#define ASC_QC_REQ_SENSE	0x80


typedef struct asc_scisq_2
{
	u_int32_t	ccb_ptr;	/* pointer to our CCB */
	u_int8_t	target_ix;	/* combined TID and LUN */
	u_int8_t	flag;
	u_int8_t	cdb_len;	/* bytes of Command Descriptor Block */
	u_int8_t	tag_code;	/* type of this transaction. see below */
	u_int16_t	vm_id;
} ASC_SCSIQ_2;

/* tag_code values */
#define ASC_TAG_FLAG_EXTRA_BYTES		0x10
#define ASC_TAG_FLAG_DISABLE_DISCONNECT		0x04
#define ASC_TAG_FLAG_DISABLE_ASYN_USE_SYN_FIX	0x08
#define ASC_TAG_FLAG_DISABLE_CHK_COND_INT_HOST	0x40


typedef struct asc_scsiq_3
{
	u_int8_t	done_stat;	/* see below done_stat values */
	u_int8_t	host_stat;	/* see below host_stat values */
	u_int8_t	scsi_stat;
	u_int8_t	scsi_msg;
} ASC_SCSIQ_3;

/* done_stat values */
#define ASC_QD_IN_PROGRESS		0x00
#define ASC_QD_NO_ERROR			0x01
#define ASC_QD_ABORTED_BY_HOST		0x02
#define ASC_QD_WITH_ERROR		0x04
#define ASC_QD_INVALID_REQUEST		0x80
#define ASC_QD_INVALID_HOST_NUM		0x81
#define ASC_QD_INVALID_DEVICE		0x82
#define ASC_QD_ERR_INTERNAL		0xFF

/* host_stat values */
#define ASC_QHSTA_NO_ERROR			0x00
#define ASC_QHSTA_M_SEL_TIMEOUT			0x11
#define ASC_QHSTA_M_DATA_OVER_RUN		0x12
#define ASC_QHSTA_M_DATA_UNDER_RUN		0x12
#define ASC_QHSTA_M_UNEXPECTED_BUS_FREE		0x13
#define ASC_QHSTA_M_BAD_BUS_PHASE_SEQ		0x14
#define ASC_QHSTA_D_QDONE_SG_LIST_CORRUPTED	0x21
#define ASC_QHSTA_D_ASC_DVC_ERROR_CODE_SET	0x22
#define ASC_QHSTA_D_HOST_ABORT_FAILED		0x23
#define ASC_QHSTA_D_EXE_SCSI_Q_FAILED		0x24
#define ASC_QHSTA_D_EXE_SCSI_Q_BUSY_TIMEOUT	0x25
#define ASC_QHSTA_D_ASPI_NO_BUF_POOL		0x26
#define ASC_QHSTA_M_WTM_TIMEOUT			0x41
#define ASC_QHSTA_M_BAD_CMPL_STATUS_IN		0x42
#define ASC_QHSTA_M_NO_AUTO_REQ_SENSE		0x43
#define ASC_QHSTA_M_AUTO_REQ_SENSE_FAIL		0x44
#define ASC_QHSTA_M_TARGET_STATUS_BUSY		0x45
#define ASC_QHSTA_M_BAD_TAG_CODE		0x46
#define ASC_QHSTA_M_BAD_QUEUE_FULL_OR_BUSY	0x47
#define ASC_QHSTA_M_HUNG_REQ_SCSI_BUS_RESET	0x48
#define ASC_QHSTA_D_LRAM_CMP_ERROR		0x81
#define ASC_QHSTA_M_MICRO_CODE_ERROR_HALT	0xA1


typedef struct asc_scsiq_4
{
	u_int8_t	cdb[ASC_MAX_CDB_LEN];
	u_int8_t	y_first_sg_list_qp;
	u_int8_t	y_working_sg_qp;
	u_int8_t	y_working_sg_ix;
	u_int8_t	y_res;
	u_int16_t	x_req_count;
	u_int16_t	x_reconnect_rtn;
	u_int32_t	x_saved_data_addr;
	u_int32_t	x_saved_data_cnt;
} ASC_SCSIQ_4;

typedef struct asc_q_done_info
{
	ASC_SCSIQ_2	d2;
	ASC_SCSIQ_3	d3;
	u_int8_t	q_status;
	u_int8_t	q_no;
	u_int8_t	cntl;
	u_int8_t	sense_len;
	u_int8_t	extra_bytes;
	u_int8_t	res;
	u_int32_t	remain_bytes;
} ASC_QDONE_INFO;

typedef struct asc_sg_list
{
	u_int32_t	addr;
	u_int32_t	bytes;
} ASC_SG_LIST;

typedef struct asc_sg_head
{
	u_int16_t	entry_cnt;	/* number of SG entries */
	u_int16_t	queue_cnt;	/* number of queues required to store SG entries */
	u_int16_t	entry_to_copy;	/* number of SG entries to copy to the board */
	u_int16_t	res;
	ASC_SG_LIST	sg_list[ASC_MAX_SG_LIST];
} ASC_SG_HEAD;

#define ASC_MIN_SG_LIST   2

typedef struct asc_min_sg_head
{
	u_int16_t	entry_cnt;
	u_int16_t	queue_cnt;
	u_int16_t	entry_to_copy;
	u_int16_t	res;
	ASC_SG_LIST	sg_list[ASC_MIN_SG_LIST];
} ASC_MIN_SG_HEAD;

#define ASC_QCX_SORT		0x0001
#define ASC_QCX_COALEASE	0x0002

typedef struct asc_scsi_q
{
	ASC_SCSIQ_1	q1;
	ASC_SCSIQ_2	q2;
	u_int8_t	*cdbptr;	/* pointer to CDB to execute */
	ASC_SG_HEAD	*sg_head;	/* pointer to SG list */
} ASC_SCSI_Q;

typedef struct asc_scsi_req_q
{
	ASC_SCSIQ_1	q1;
	ASC_SCSIQ_2	q2;
	u_int8_t	*cdbptr;
	ASC_SG_HEAD	*sg_head;
	u_int8_t	*sense_ptr;
	ASC_SCSIQ_3	q3;
	u_int8_t	cdb[ASC_MAX_CDB_LEN];
	u_int8_t	sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_REQ_Q;

typedef struct asc_scsi_bios_req_q
{
	ASC_SCSIQ_1	q1;
	ASC_SCSIQ_2	q2;
	u_int8_t	*cdbptr;
	ASC_SG_HEAD	*sg_head;
	u_int8_t	*sense_ptr;
	ASC_SCSIQ_3	q3;
	u_int8_t	cdb[ASC_MAX_CDB_LEN];
	u_int8_t	sense[ASC_MIN_SENSE_LEN];
} ASC_SCSI_BIOS_REQ_Q;

typedef struct asc_risc_q
{
	u_int8_t	fwd;
	u_int8_t	bwd;
	ASC_SCSIQ_1	i1;
	ASC_SCSIQ_2	i2;
	ASC_SCSIQ_3	i3;
	ASC_SCSIQ_4	i4;
} ASC_RISC_Q;

typedef struct asc_sg_list_q
{
	u_int8_t	seq_no;
	u_int8_t	q_no;
	u_int8_t	cntl;		/* see below cntl values */
	u_int8_t	sg_head_qp;
	u_int8_t	sg_list_cnt;
	u_int8_t	sg_cur_list_cnt;
} ASC_SG_LIST_Q;

/* cntl values */
#define ASC_QCSG_SG_XFER_LIST	0x02
#define ASC_QCSG_SG_XFER_MORE	0x04
#define ASC_QCSG_SG_XFER_END	0x08

#define ASC_SGQ_B_SG_CNTL		4
#define ASC_SGQ_B_SG_HEAD_QP		5
#define ASC_SGQ_B_SG_LIST_CNT		6
#define ASC_SGQ_B_SG_CUR_LIST_CNT	7
#define ASC_SGQ_LIST_BEG		8


typedef struct asc_risc_sg_list_q
{
	u_int8_t	fwd;
	u_int8_t	bwd;
	ASC_SG_LIST_Q	sg;
	ASC_SG_LIST	sg_list[7];
} ASC_RISC_SG_LIST_Q;


#define ASC_EXE_SCSI_IO_MAX_IDLE_LOOP  0x1000000UL
#define ASC_EXE_SCSI_IO_MAX_WAIT_LOOP  1024

#define ASCQ_ERR_NO_ERROR		0x00
#define ASCQ_ERR_IO_NOT_FOUND		0x01
#define ASCQ_ERR_LOCAL_MEM		0x02
#define ASCQ_ERR_CHKSUM			0x03
#define ASCQ_ERR_START_CHIP		0x04
#define ASCQ_ERR_INT_TARGET_ID		0x05
#define ASCQ_ERR_INT_LOCAL_MEM		0x06
#define ASCQ_ERR_HALT_RISC		0x07
#define ASCQ_ERR_GET_ASPI_ENTRY		0x08
#define ASCQ_ERR_CLOSE_ASPI		0x09
#define ASCQ_ERR_HOST_INQUIRY		0x0A
#define ASCQ_ERR_SAVED_CCB_BAD		0x0B
#define ASCQ_ERR_QCNTL_SG_LIST		0x0C
#define ASCQ_ERR_Q_STATUS		0x0D
#define ASCQ_ERR_WR_SCSIQ		0x0E
#define ASCQ_ERR_PC_ADDR		0x0F
#define ASCQ_ERR_SYN_OFFSET		0x10
#define ASCQ_ERR_SYN_XFER_TIME		0x11
#define ASCQ_ERR_LOCK_DMA		0x12
#define ASCQ_ERR_UNLOCK_DMA		0x13
#define ASCQ_ERR_VDS_CHK_INSTALL	0x14
#define ASCQ_ERR_MICRO_CODE_HALT	0x15
#define ASCQ_ERR_SET_LRAM_ADDR		0x16
#define ASCQ_ERR_CUR_QNG		0x17
#define ASCQ_ERR_SG_Q_LINKS		0x18
#define ASCQ_ERR_SCSIQ_PTR		0x19
#define ASCQ_ERR_ISR_RE_ENTRY		0x1A
#define ASCQ_ERR_CRITICAL_RE_ENTRY	0x1B
#define ASCQ_ERR_ISR_ON_CRITICAL	0x1C
#define ASCQ_ERR_SG_LIST_ODD_ADDRESS	0x1D
#define ASCQ_ERR_XFER_ADDRESS_TOO_BIG	0x1E
#define ASCQ_ERR_SCSIQ_NULL_PTR		0x1F
#define ASCQ_ERR_SCSIQ_BAD_NEXT_PTR 	0x20
#define ASCQ_ERR_GET_NUM_OF_FREE_Q	0x21
#define ASCQ_ERR_SEND_SCSI_Q		0x22
#define ASCQ_ERR_HOST_REQ_RISC_HALT 	0x23
#define ASCQ_ERR_RESET_SDTR		0x24

#define ASC_WARN_NO_ERROR		0x0000
#define ASC_WARN_IO_PORT_ROTATE		0x0001
#define ASC_WARN_EEPROM_CHKSUM		0x0002
#define ASC_WARN_IRQ_MODIFIED		0x0004
#define ASC_WARN_AUTO_CONFIG		0x0008
#define ASC_WARN_CMD_QNG_CONFLICT	0x0010
#define ASC_WARN_EEPROM_RECOVER		0x0020
#define ASC_WARN_CFG_MSW_RECOVER	0x0040
#define ASC_WARN_SET_PCI_CONFIG_SPACE	0x0080

#define ASC_IERR_WRITE_EEPROM		0x0001
#define ASC_IERR_MCODE_CHKSUM		0x0002
#define ASC_IERR_SET_PC_ADDR		0x0004
#define ASC_IERR_START_STOP_CHIP	0x0008
#define ASC_IERR_IRQ_NO			0x0010
#define ASC_IERR_SET_IRQ_NO		0x0020
#define ASC_IERR_CHIP_VERSION		0x0040
#define ASC_IERR_SET_SCSI_ID		0x0080
#define ASC_IERR_GET_PHY_ADDR		0x0100
#define ASC_IERR_BAD_SIGNATURE		0x0200
#define ASC_IERR_NO_BUS_TYPE		0x0400
#define ASC_IERR_SCAM			0x0800
#define ASC_IERR_SET_SDTR		0x1000
#define ASC_IERR_RW_LRAM		0x8000

#define ASC_DEF_IRQ_NO			10
#define ASC_MAX_IRQ_NO			15
#define ASC_MIN_IRQ_NO			10
#define ASC_MIN_REMAIN_Q		0x02
#define ASC_DEF_MAX_TOTAL_QNG   0xF0
#define ASC_MIN_TAG_Q_PER_DVC   0x04
#define ASC_DEF_TAG_Q_PER_DVC   0x04
#define ASC_MIN_FREE_Q		ASC_MIN_REMAIN_Q
#define ASC_MIN_TOTAL_QNG	((ASC_MAX_SG_QUEUE)+(ASC_MIN_FREE_Q))
#define ASC_MAX_TOTAL_QNG	240
#define ASC_MAX_PCI_ULTRA_INRAM_TOTAL_QNG	16
#define ASC_MAX_PCI_ULTRA_INRAM_TAG_QNG		8
#define ASC_MAX_PCI_INRAM_TOTAL_QNG		20
#define ASC_MAX_INRAM_TAG_QNG	16
#define ASC_IOADR_TABLE_MAX_IX	11
#define ASC_IOADR_GAP			0x10
#define ASC_SEARCH_IOP_GAP		0x10
#define ASC_MIN_IOP_ADDR		0x0100
#define ASC_MAX_IOP_ADDR		0x03F0

#define ASC_IOADR_1			0x0110
#define ASC_IOADR_2			0x0130
#define ASC_IOADR_3			0x0150
#define ASC_IOADR_4			0x0190
#define ASC_IOADR_5			0x0210
#define ASC_IOADR_6			0x0230
#define ASC_IOADR_7			0x0250
#define ASC_IOADR_8			0x0330

#define ASC_IOADR_DEF			ASC_IOADR_8
#define ASC_LIB_SCSIQ_WK_SP		256
#define ASC_MAX_SYN_XFER_NO		16
#define ASC_SYN_MAX_OFFSET		0x0F
#define ASC_DEF_SDTR_OFFSET		0x0F
#define ASC_DEF_SDTR_INDEX		0x00
#define ASC_SDTR_ULTRA_PCI_10MB_INDEX	0x02


/*
 * This structure is used to handle internal messages
 * during interrupt handling routine
 */
typedef struct ext_msg
{
	u_int8_t	msg_type;
	u_int8_t	msg_len;
	u_int8_t	msg_req;

	union
	{
		struct
		{
			u_int8_t	sdtr_xfer_period;
			u_int8_t	sdtr_req_ack_offset;
		} sdtr;

		struct
		{
			u_int8_t	wdtr_width;
		} wdtr;

		struct
		{
			u_int8_t	mdp_b3;
			u_int8_t	mdp_b2;
			u_int8_t	mdp_b1;
			u_int8_t	mdp_b0;
		} mdp;
	} u_ext_msg;

	u_int8_t	res;
} EXT_MSG;

#define xfer_period	u_ext_msg.sdtr.sdtr_xfer_period
#define req_ack_offset	u_ext_msg.sdtr.sdtr_req_ack_offset
#define wdtr_width	u_ext_msg.wdtr.wdtr_width
#define mdp_b3		u_ext_msg.mdp_b3
#define mdp_b2		u_ext_msg.mdp_b2
#define mdp_b1		u_ext_msg.mdp_b1
#define mdp_b0		u_ext_msg.mdp_b0


#define ASC_DEF_DVC_CNTL		0xFFFF
#define ASC_DEF_CHIP_SCSI_ID		7
#define ASC_DEF_ISA_DMA_SPEED		4

#define ASC_PCI_DEVICE_ID_REV_A		0x1100
#define ASC_PCI_DEVICE_ID_REV_B		0x1200

#define ASC_BUG_FIX_IF_NOT_DWB		0x0001
#define ASC_BUG_FIX_ASYN_USE_SYN	0x0002

#define ASYN_SDTR_DATA_FIX_PCI_REV_AB	0x41

#define ASC_MIN_TAGGED_CMD	7

#define ASC_MAX_SCSI_RESET_WAIT	30


typedef struct asc_softc
{
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	bus_dmamap_t		sc_dmamap_control; /* maps the control structures */
	void			*sc_ih;

	struct adv_control	*sc_control; /* control structures */
	TAILQ_HEAD(, adv_ccb)	sc_free_ccb, sc_waiting_ccb;
	struct mutex		sc_ccb_mtx;
	struct scsi_iopool	sc_iopool;

	u_int8_t		*overrun_buf;

	u_int16_t		sc_flags;	/* see below sc_flags values */

	u_int16_t		dvc_cntl;
	u_int16_t		bug_fix_cntl;
	u_int16_t		bus_type;

	ulong			isr_callback;

	ASC_SCSI_BIT_ID_TYPE	init_sdtr;
	ASC_SCSI_BIT_ID_TYPE	sdtr_done;
	ASC_SCSI_BIT_ID_TYPE	use_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE	unit_not_ready;
	ASC_SCSI_BIT_ID_TYPE	queue_full_or_busy;
	ASC_SCSI_BIT_ID_TYPE	start_motor;

	ASC_SCSI_BIT_ID_TYPE	can_tagged_qng;
	ASC_SCSI_BIT_ID_TYPE	cmd_qng_enabled;
	ASC_SCSI_BIT_ID_TYPE	disc_enable;
	ASC_SCSI_BIT_ID_TYPE	sdtr_enable;
	u_int8_t		chip_scsi_id;
	u_int8_t		isa_dma_speed;
	u_int8_t		isa_dma_channel;
	u_int8_t		chip_version;
	u_int16_t		pci_device_id;
	u_int16_t		lib_serial_no;
	u_int16_t		lib_version;
	u_int16_t		mcode_date;
	u_int16_t		mcode_version;
	u_int8_t		max_tag_qng[ASC_MAX_TID + 1];
	u_int8_t		sdtr_period_offset[ASC_MAX_TID + 1];
	u_int8_t		adapter_info[6];

	u_int8_t		scsi_reset_wait;
	u_int8_t		max_total_qng;
	u_int8_t		cur_total_qng;
	u_int8_t		irq_no;
	u_int8_t		last_q_shortage;

	u_int8_t		cur_dvc_qng[ASC_MAX_TID + 1];
	u_int8_t		max_dvc_qng[ASC_MAX_TID + 1];
	u_int8_t		sdtr_period_tbl[ASC_MAX_SYN_XFER_NO];
	u_int8_t		sdtr_period_tbl_size;	/* see below */
	u_int8_t		sdtr_data[ASC_MAX_TID+1];

	u_int16_t		reqcnt[ASC_MAX_TID+1]; /* Starvation request count */

	u_int32_t		max_dma_count;
	ASC_SCSI_BIT_ID_TYPE	pci_fix_asyn_xfer;
	ASC_SCSI_BIT_ID_TYPE	pci_fix_asyn_xfer_always;
	u_int8_t		max_sdtr_index;
	u_int8_t		host_init_sdtr_index;
} ASC_SOFTC;

/* sc_flags values */
#define ASC_HOST_IN_RESET		0x01
#define ASC_HOST_IN_ABORT		0x02
#define ASC_WIDE_BOARD			0x04
#define ASC_SELECT_QUEUE_DEPTHS		0x08

/* sdtr_period_tbl_size values */
#define SYN_XFER_NS_0		 25
#define SYN_XFER_NS_1		 30
#define SYN_XFER_NS_2		 35
#define SYN_XFER_NS_3		 40
#define SYN_XFER_NS_4		 50
#define SYN_XFER_NS_5		 60
#define SYN_XFER_NS_6		 70
#define SYN_XFER_NS_7		 85

#define SYN_ULTRA_XFER_NS_0	 12
#define SYN_ULTRA_XFER_NS_1	 19
#define SYN_ULTRA_XFER_NS_2	 25
#define SYN_ULTRA_XFER_NS_3	 32
#define SYN_ULTRA_XFER_NS_4	 38
#define SYN_ULTRA_XFER_NS_5	 44
#define SYN_ULTRA_XFER_NS_6	 50
#define SYN_ULTRA_XFER_NS_7	 57
#define SYN_ULTRA_XFER_NS_8	 63
#define SYN_ULTRA_XFER_NS_9	 69
#define SYN_ULTRA_XFER_NS_10	 75
#define SYN_ULTRA_XFER_NS_11	 82
#define SYN_ULTRA_XFER_NS_12	 88
#define SYN_ULTRA_XFER_NS_13	 94
#define SYN_ULTRA_XFER_NS_14	100
#define SYN_ULTRA_XFER_NS_15	107


/* second level interrupt callback type definition */
typedef int (* ASC_ISR_CALLBACK) (ASC_SOFTC *, ASC_QDONE_INFO *);


#define ASC_MCNTL_NO_SEL_TIMEOUT	0x0001
#define ASC_MCNTL_NULL_TARGET		0x0002

#define ASC_CNTL_INITIATOR		0x0001
#define ASC_CNTL_BIOS_GT_1GB		0x0002
#define ASC_CNTL_BIOS_GT_2_DISK		0x0004
#define ASC_CNTL_BIOS_REMOVABLE		0x0008
#define ASC_CNTL_NO_SCAM		0x0010
#define ASC_CNTL_INT_MULTI_Q		0x0080
#define ASC_CNTL_NO_LUN_SUPPORT		0x0040
#define ASC_CNTL_NO_VERIFY_COPY		0x0100
#define ASC_CNTL_RESET_SCSI		0x0200
#define ASC_CNTL_INIT_INQUIRY	 	0x0400
#define ASC_CNTL_INIT_VERBOSE		0x0800
#define ASC_CNTL_SCSI_PARITY		0x1000
#define ASC_CNTL_BURST_MODE		0x2000
#define ASC_CNTL_SDTR_ENABLE_ULTRA	0x4000

#define ASC_EEP_DVC_CFG_BEG_VL		 2
#define ASC_EEP_MAX_DVC_ADDR_VL		15
#define ASC_EEP_DVC_CFG_BEG		32
#define ASC_EEP_MAX_DVC_ADDR		45
#define ASC_EEP_DEFINED_WORDS		10
#define ASC_EEP_MAX_ADDR		63
#define ASC_EEP_RES_WORDS		 0
#define ASC_EEP_MAX_RETRY		20
#define ASC_MAX_INIT_BUSY_RETRY		 8
#define ASC_EEP_ISA_PNP_WSIZE		16


/*
 * This structure is used to read/write EEProm configuration
 */
typedef struct asceep_config
{
	u_int16_t	cfg_lsw;
	u_int16_t	cfg_msw;
#if BYTE_ORDER == BIG_ENDIAN
	u_int8_t	disc_enable;
	u_int8_t	init_sdtr;
	u_int8_t	start_motor;
	u_int8_t	use_cmd_qng;
	u_int8_t	max_tag_qng;
	u_int8_t	max_total_qng;
	u_int8_t	power_up_wait;
	u_int8_t	bios_scan;
	u_int8_t	isa_dma_speed:4;
	u_int8_t	chip_scsi_id:4;
	u_int8_t	no_scam;
#else
	u_int8_t	init_sdtr;
	u_int8_t	disc_enable;
	u_int8_t	use_cmd_qng;
	u_int8_t	start_motor;
	u_int8_t	max_total_qng;
	u_int8_t	max_tag_qng;
	u_int8_t	bios_scan;
	u_int8_t	power_up_wait;
	u_int8_t	no_scam;
	u_int8_t	chip_scsi_id:4;
	u_int8_t	isa_dma_speed:4;
#endif
	u_int8_t	dos_int13_table[ASC_MAX_TID + 1];
	u_int8_t	adapter_info[6];
	u_int16_t	cntl;
	u_int16_t	chksum;
} ASCEEP_CONFIG;

#define ASC_PCI_CFG_LSW_SCSI_PARITY	0x0800
#define ASC_PCI_CFG_LSW_BURST_MODE	0x0080
#define ASC_PCI_CFG_LSW_INTR_ABLE	0x0020

#define ASC_EEP_CMD_READ		0x80
#define ASC_EEP_CMD_WRITE		0x40
#define ASC_EEP_CMD_WRITE_ABLE		0x30
#define ASC_EEP_CMD_WRITE_DISABLE	0x00

#define ASC_OVERRUN_BSIZE		0x00000048UL

#define ASC_CTRL_BREAK_ONCE		0x0001
#define ASC_CTRL_BREAK_STAY_IDLE	0x0002

#define ASCV_MSGOUT_BEG			0x0000
#define ASCV_MSGOUT_SDTR_PERIOD		(ASCV_MSGOUT_BEG+3)
#define ASCV_MSGOUT_SDTR_OFFSET		(ASCV_MSGOUT_BEG+4)
#define ASCV_BREAK_SAVED_CODE		0x0006
#define ASCV_MSGIN_BEG			(ASCV_MSGOUT_BEG+8)
#define ASCV_MSGIN_SDTR_PERIOD		(ASCV_MSGIN_BEG+3)
#define ASCV_MSGIN_SDTR_OFFSET		(ASCV_MSGIN_BEG+4)
#define ASCV_SDTR_DATA_BEG		(ASCV_MSGIN_BEG+8)
#define ASCV_SDTR_DONE_BEG		(ASCV_SDTR_DATA_BEG+8)
#define ASCV_MAX_DVC_QNG_BEG		0x0020
#define ASCV_BREAK_ADDR		   	0x0028
#define ASCV_BREAK_NOTIFY_COUNT 	0x002A
#define ASCV_BREAK_CONTROL		0x002C
#define ASCV_BREAK_HIT_COUNT		0x002E

#define ASCV_ASCDVC_ERR_CODE_W		0x0030
#define ASCV_MCODE_CHKSUM_W		0x0032
#define ASCV_MCODE_SIZE_W		0x0034
#define ASCV_STOP_CODE_B		0x0036
#define ASCV_DVC_ERR_CODE_B		0x0037
#define ASCV_OVERRUN_PADDR_D		0x0038
#define ASCV_OVERRUN_BSIZE_D		0x003C
#define ASCV_HALTCODE_W			0x0040
#define ASCV_CHKSUM_W			0x0042
#define ASCV_MC_DATE_W			0x0044
#define ASCV_MC_VER_W			0x0046
#define ASCV_NEXTRDY_B			0x0048
#define ASCV_DONENEXT_B	  		0x0049
#define ASCV_USE_TAGGED_QNG_B		0x004A
#define ASCV_SCSIBUSY_B	 		0x004B
#define ASCV_Q_DONE_IN_PROGRESS_B	0x004C
#define ASCV_CURCDB_B			0x004D
#define ASCV_RCLUN_B			0x004E
#define ASCV_BUSY_QHEAD_B		0x004F
#define ASCV_DISC1_QHEAD_B		0x0050
#define ASCV_DISC_ENABLE_B		0x0052
#define ASCV_CAN_TAGGED_QNG_B 		0x0053
#define ASCV_HOSTSCSI_ID_B		0x0055
#define ASCV_MCODE_CNTL_B		0x0056
#define ASCV_NULL_TARGET_B		0x0057
#define ASCV_FREE_Q_HEAD_W		0x0058
#define ASCV_DONE_Q_TAIL_W		0x005A
#define ASCV_FREE_Q_HEAD_B		(ASCV_FREE_Q_HEAD_W+1)
#define ASCV_DONE_Q_TAIL_B		(ASCV_DONE_Q_TAIL_W+1)
#define ASCV_HOST_FLAG_B		0x005D
#define ASCV_TOTAL_READY_Q_B  		0x0064
#define ASCV_VER_SERIAL_B	 	0x0065
#define ASCV_HALTCODE_SAVED_W		0x0066
#define ASCV_WTM_FLAG_B			0x0068
#define ASCV_RISC_FLAG_B		0x006A
#define ASCV_REQ_SG_LIST_QP		0x006B

#define ASC_HOST_FLAG_IN_ISR		0x01
#define ASC_HOST_FLAG_ACK_INT		0x02
#define ASC_RISC_FLAG_GEN_INT		0x01
#define ASC_RISC_FLAG_REQ_SG_LIST	0x02

#define ASC_IOP_CTRL			0x0F
#define ASC_IOP_STATUS			0x0E
#define ASC_IOP_INT_ACK			ASC_IOP_STATUS
#define ASC_IOP_REG_IFC			0x0D
#define ASC_IOP_SYN_OFFSET		0x0B
#define ASC_IOP_EXTRA_CONTROL	0x0D
#define ASC_IOP_REG_PC			0x0C
#define ASC_IOP_RAM_ADDR		0x0A
#define ASC_IOP_RAM_DATA		0x08
#define ASC_IOP_EEP_DATA		0x06
#define ASC_IOP_EEP_CMD			0x07
#define ASC_IOP_VERSION			0x03
#define ASC_IOP_CONFIG_HIGH		0x04
#define ASC_IOP_CONFIG_LOW		0x02
#define ASC_IOP_SIG_BYTE		0x01
#define ASC_IOP_SIG_WORD		0x00
#define ASC_IOP_REG_DC1			0x0E
#define ASC_IOP_REG_DC0			0x0C
#define ASC_IOP_REG_SB			0x0B
#define ASC_IOP_REG_DA1			0x0A
#define ASC_IOP_REG_DA0			0x08
#define ASC_IOP_REG_SC			0x09
#define ASC_IOP_DMA_SPEED		0x07
#define ASC_IOP_REG_FLAG		0x07
#define ASC_IOP_FIFO_H			0x06
#define ASC_IOP_FIFO_L			0x04
#define ASC_IOP_REG_ID			0x05
#define ASC_IOP_REG_QP			0x03
#define ASC_IOP_REG_IH	 		0x02
#define ASC_IOP_REG_IX	 		0x01
#define ASC_IOP_REG_AX			0x00

#define ASC_IFC_REG_LOCK		0x00
#define ASC_IFC_REG_UNLOCK		0x09
#define ASC_IFC_WR_EN_FILTER		0x10
#define ASC_IFC_RD_NO_EEPROM		0x10
#define ASC_IFC_SLEW_RATE		0x20
#define ASC_IFC_ACT_NEG			0x40
#define ASC_IFC_INP_FILTER		0x80
#define ASC_IFC_INIT_DEFAULT	(ASC_IFC_ACT_NEG | ASC_IFC_REG_UNLOCK)

#define SC_SEL	0x80
#define SC_BSY	0x40
#define SC_ACK	0x20
#define SC_REQ	0x10
#define SC_ATN	0x08
#define SC_IO	0x04
#define SC_CD	0x02
#define SC_MSG	0x01

#define SEC_SCSI_CTL		0x80
#define SEC_ACTIVE_NEGATE	0x40
#define SEC_SLEW_RATE		0x20
#define SEC_ENABLE_FILTER	0x10

#define ASC_HALT_EXTMSG_IN			0x8000
#define ASC_HALT_CHK_CONDITION			0x8100
#define ASC_HALT_SS_QUEUE_FULL			0x8200
#define ASC_HALT_DISABLE_ASYN_USE_SYN_FIX	0x8300
#define ASC_HALT_ENABLE_ASYN_USE_SYN_FIX	0x8400
#define ASC_HALT_SDTR_REJECTED			0x4000

#define ASC_MAX_QNO		0xF8

#define ASC_DATA_SEC_BEG	0x0080
#define ASC_DATA_SEC_END	0x0080
#define ASC_CODE_SEC_BEG	0x0080
#define ASC_CODE_SEC_END	0x0080
#define ASC_QADR_BEG		(0x4000)
#define ASC_QADR_USED		(ASC_MAX_QNO * 64)
#define ASC_QADR_END		0x7FFF
#define ASC_QLAST_ADR		0x7FC0
#define ASC_QBLK_SIZE		0x40
#define ASC_BIOS_DATA_QBEG	0xF8
#define ASC_MIN_ACTIVE_QNO	0x01
#define ASC_QLINK_END		0xFF
#define ASC_EEPROM_WORDS	0x10
#define ASC_MAX_MGS_LEN		0x10

#define ASC_BIOS_ADDR_DEF	0xDC00
#define ASC_BIOS_SIZE		0x3800
#define ASC_BIOS_RAM_OFF	0x3800
#define ASC_BIOS_RAM_SIZE 	0x800
#define ASC_BIOS_MIN_ADDR	0xC000
#define ASC_BIOS_MAX_ADDR	0xEC00
#define ASC_BIOS_BANK_SIZE	0x0400

#define ASC_MCODE_START_ADDR	0x0080

#define ASC_CFG0_HOST_INT_ON	0x0020
#define ASC_CFG0_BIOS_ON	0x0040
#define ASC_CFG0_VERA_BURST_ON	0x0080
#define ASC_CFG0_SCSI_PARITY_ON	0x0800
#define ASC_CFG1_SCSI_TARGET_ON	0x0080
#define ASC_CFG1_LRAM_8BITS_ON	0x0800
#define ASC_CFG_MSW_CLR_MASK	0x3080

#define ASC_CSW_TEST1			0x8000
#define ASC_CSW_AUTO_CONFIG		0x4000
#define ASC_CSW_RESERVED1		0x2000
#define ASC_CSW_IRQ_WRITTEN		0x1000
#define ASC_CSW_33MHZ_SELECTED		0x0800
#define ASC_CSW_TEST2			0x0400
#define ASC_CSW_TEST3			0x0200
#define ASC_CSW_RESERVED2		0x0100
#define ASC_CSW_DMA_DONE		0x0080
#define ASC_CSW_FIFO_RDY		0x0040
#define ASC_CSW_EEP_READ_DONE		0x0020
#define ASC_CSW_HALTED			0x0010
#define ASC_CSW_SCSI_RESET_ACTIVE	0x0008
#define ASC_CSW_PARITY_ERR		0x0004
#define ASC_CSW_SCSI_RESET_LATCH  	0x0002
#define ASC_CSW_INT_PENDING		0x0001

#define ASC_CIW_CLR_SCSI_RESET_INT	0x1000
#define ASC_CIW_INT_ACK			0x0100
#define ASC_CIW_TEST1			0x0200
#define ASC_CIW_TEST2			0x0400
#define ASC_CIW_SEL_33MHZ		0x0800
#define ASC_CIW_IRQ_ACT			0x1000

#define ASC_CC_CHIP_RESET	0x80
#define ASC_CC_SCSI_RESET	0x40
#define ASC_CC_HALT		0x20
#define ASC_CC_SINGLE_STEP	0x10
#define ASC_CC_DMA_ABLE		0x08
#define ASC_CC_TEST		0x04
#define ASC_CC_BANK_ONE		0x02
#define ASC_CC_DIAG		0x01

#define ASC_1000_ID0W		0x04C1
#define ASC_1000_ID0W_FIX	0x00C1
#define ASC_1000_ID1B		0x25

#define ASC_EISA_BIG_IOP_GAP	(0x1C30-0x0C50)
#define ASC_EISA_SMALL_IOP_GAP	(0x0020)
#define ASC_EISA_MIN_IOP_ADDR	(0x0C30)
#define ASC_EISA_MAX_IOP_ADDR	(0xFC50)
#define ASC_EISA_REV_IOP_MASK	(0x0C83)
#define ASC_EISA_PID_IOP_MASK	(0x0C80)
#define ASC_EISA_CFG_IOP_MASK	(0x0C86)

#define ASC_GET_EISA_SLOT(iop)	((iop) & 0xF000)

#define ASC_EISA_ID_740	0x01745004UL
#define ASC_EISA_ID_750	0x01755004UL

#define ASC_INS_HALTINT		0x6281
#define ASC_INS_HALT		0x6280
#define ASC_INS_SINT		0x6200
#define ASC_INS_RFLAG_WTM	0x7380


/******************************************************************************/
/*                                      Macro                                 */
/******************************************************************************/

/*
 * These Macros are used to deal with board CPU Registers and LRAM
 */

#define ASC_GET_QDONE_IN_PROGRESS(iot, ioh)			AscReadLramByte((iot), (ioh), ASCV_Q_DONE_IN_PROGRESS_B)
#define ASC_PUT_QDONE_IN_PROGRESS(iot, ioh, val)		AscWriteLramByte((iot), (ioh), ASCV_Q_DONE_IN_PROGRESS_B, val)
#define ASC_GET_VAR_FREE_QHEAD(iot, ioh)			AscReadLramWord((iot), (ioh), ASCV_FREE_Q_HEAD_W)
#define ASC_GET_VAR_DONE_QTAIL(iot, ioh)			AscReadLramWord((iot), (ioh), ASCV_DONE_Q_TAIL_W)
#define ASC_PUT_VAR_FREE_QHEAD(iot, ioh, val)			AscWriteLramWord((iot), (ioh), ASCV_FREE_Q_HEAD_W, val)
#define ASC_PUT_VAR_DONE_QTAIL(iot, ioh, val)			AscWriteLramWord((iot), (ioh), ASCV_DONE_Q_TAIL_W, val)
#define ASC_GET_RISC_VAR_FREE_QHEAD(iot, ioh)			AscReadLramByte((iot), (ioh), ASCV_NEXTRDY_B)
#define ASC_GET_RISC_VAR_DONE_QTAIL(iot, ioh)			AscReadLramByte((iot), (ioh), ASCV_DONENEXT_B)
#define ASC_PUT_RISC_VAR_FREE_QHEAD(iot, ioh, val)   		AscWriteLramByte((iot), (ioh), ASCV_NEXTRDY_B, val)
#define ASC_PUT_RISC_VAR_DONE_QTAIL(iot, ioh, val)   		AscWriteLramByte((iot), (ioh), ASCV_DONENEXT_B, val)
#define ASC_PUT_MCODE_SDTR_DONE_AT_ID(iot, ioh, id, data)	AscWriteLramByte((iot), (ioh), (u_int16_t)((u_int16_t)ASCV_SDTR_DONE_BEG+(u_int16_t)id), (data)) ;
#define ASC_GET_MCODE_SDTR_DONE_AT_ID(iot, ioh, id)		AscReadLramByte((iot), (ioh), (u_int16_t)((u_int16_t)ASCV_SDTR_DONE_BEG+(u_int16_t)id)) ;
#define ASC_PUT_MCODE_INIT_SDTR_AT_ID(iot, ioh, id, data)	AscWriteLramByte((iot), (ioh), (u_int16_t)((u_int16_t)ASCV_SDTR_DATA_BEG+(u_int16_t)id), data) ;
#define ASC_GET_MCODE_INIT_SDTR_AT_ID(iot, ioh, id)		AscReadLramByte((iot), (ioh), (u_int16_t)((u_int16_t)ASCV_SDTR_DATA_BEG+(u_int16_t)id)) ;
#define ASC_SYN_INDEX_TO_PERIOD(sc, index)			(u_int8_t)((sc)->sdtr_period_tbl[ (index) ])
#define ASC_GET_CHIP_SIGNATURE_BYTE(iot, ioh)			bus_space_read_1((iot), (ioh), ASC_IOP_SIG_BYTE)
#define ASC_GET_CHIP_SIGNATURE_WORD(iot, ioh)			bus_space_read_2((iot), (ioh), ASC_IOP_SIG_WORD)
#define ASC_GET_CHIP_VER_NO(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_VERSION)
#define ASC_GET_CHIP_CFG_LSW(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_CONFIG_LOW)
#define ASC_GET_CHIP_CFG_MSW(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_CONFIG_HIGH)
#define ASC_SET_CHIP_CFG_LSW(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_CONFIG_LOW, data)
#define ASC_SET_CHIP_CFG_MSW(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_CONFIG_HIGH, data)
#define ASC_GET_CHIP_EEP_CMD(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_EEP_CMD)
#define ASC_SET_CHIP_EEP_CMD(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_EEP_CMD, data)
#define ASC_GET_CHIP_EEP_DATA(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_EEP_DATA)
#define ASC_SET_CHIP_EEP_DATA(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_EEP_DATA, data)
#define ASC_GET_CHIP_LRAM_ADDR(iot, ioh)			bus_space_read_2((iot), (ioh), ASC_IOP_RAM_ADDR)
#define ASC_SET_CHIP_LRAM_ADDR(iot, ioh, addr)			bus_space_write_2((iot), (ioh), ASC_IOP_RAM_ADDR, addr)
#define ASC_GET_CHIP_LRAM_DATA(iot, ioh)			bus_space_read_2((iot), (ioh), ASC_IOP_RAM_DATA)
#define ASC_SET_CHIP_LRAM_DATA(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_RAM_DATA, data)
#if BYTE_ORDER == BIG_ENDIAN
#define ASC_GET_CHIP_LRAM_DATA_NO_SWAP(iot, ioh)		SWAPBYTES(bus_space_read_2((iot), (ioh), ASC_IOP_RAM_DATA))
#define ASC_SET_CHIP_LRAM_DATA_NO_SWAP(iot, ioh, data)		bus_space_write_2((iot), (ioh), ASC_IOP_RAM_DATA, SWAPBYTES(data))
#else
#define ASC_GET_CHIP_LRAM_DATA_NO_SWAP(iot, ioh)		bus_space_read_2((iot), (ioh), ASC_IOP_RAM_DATA)
#define ASC_SET_CHIP_LRAM_DATA_NO_SWAP(iot, ioh, data)		bus_space_write_2((iot), (ioh), ASC_IOP_RAM_DATA, data)
#endif
#define ASC_GET_CHIP_IFC(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_REG_IFC)
#define ASC_SET_CHIP_IFC(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_REG_IFC, data)
#define ASC_GET_CHIP_STATUS(iot, ioh)				(u_int16_t)bus_space_read_2((iot), (ioh), ASC_IOP_STATUS)
#define ASC_SET_CHIP_STATUS(iot, ioh, cs_val)			bus_space_write_2((iot), (ioh), ASC_IOP_STATUS, cs_val)
#define ASC_GET_CHIP_CONTROL(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_CTRL)
#define ASC_SET_CHIP_CONTROL(iot, ioh, cc_val)			bus_space_write_1((iot), (ioh), ASC_IOP_CTRL, cc_val)
#define ASC_GET_CHIP_SYN(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_SYN_OFFSET)
#define ASC_SET_CHIP_SYN(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_SYN_OFFSET, data)
#define ASC_SET_PC_ADDR(iot, ioh, data)				bus_space_write_2((iot), (ioh), ASC_IOP_REG_PC, data)
#define ASC_GET_PC_ADDR(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_PC)
#define ASC_IS_INT_PENDING(iot, ioh)				(ASC_GET_CHIP_STATUS((iot), (ioh)) & (ASC_CSW_INT_PENDING | ASC_CSW_SCSI_RESET_LATCH))
#define ASC_GET_CHIP_SCSI_ID(iot, ioh)				((ASC_GET_CHIP_CFG_LSW((iot), (ioh)) >> 8) & ASC_MAX_TID)
#define ASC_GET_EXTRA_CONTROL(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_EXTRA_CONTROL)
#define ASC_SET_EXTRA_CONTROL(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_EXTRA_CONTROL, data)
#define ASC_READ_CHIP_AX(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_AX)
#define ASC_WRITE_CHIP_AX(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_REG_AX, data)
#define ASC_READ_CHIP_IX(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_REG_IX)
#define ASC_WRITE_CHIP_IX(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_REG_IX, data)
#define ASC_READ_CHIP_IH(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_IH)
#define ASC_WRITE_CHIP_IH(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_REG_IH, data)
#define ASC_READ_CHIP_QP(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_REG_QP)
#define ASC_WRITE_CHIP_QP(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_REG_QP, data)
#define ASC_READ_CHIP_FIFO_L(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_FIFO_L)
#define ASC_WRITE_CHIP_FIFO_L(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_REG_FIFO_L, data)
#define ASC_READ_CHIP_FIFO_H(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_FIFO_H)
#define ASC_WRITE_CHIP_FIFO_H(iot, ioh, data)			bus_space_write_2((iot), (ioh), ASC_IOP_REG_FIFO_H, data)
#define ASC_READ_CHIP_DMA_SPEED(iot, ioh)			bus_space_read_1((iot), (ioh), ASC_IOP_DMA_SPEED)
#define ASC_WRITE_CHIP_DMA_SPEED(iot, ioh, data)		bus_space_write_1((iot), (ioh), ASC_IOP_DMA_SPEED, data)
#define ASC_READ_CHIP_DA0(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_DA0)
#define ASC_WRITE_CHIP_DA0(iot, ioh)				bus_space_write_2((iot), (ioh), ASC_IOP_REG_DA0, data)
#define ASC_READ_CHIP_DA1(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_DA1)
#define ASC_WRITE_CHIP_DA1(iot, ioh)				bus_space_write_2((iot), (ioh), ASC_IOP_REG_DA1, data)
#define ASC_READ_CHIP_DC0(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_DC0)
#define ASC_WRITE_CHIP_DC0(iot, ioh)				bus_space_write_2((iot), (ioh), ASC_IOP_REG_DC0, data)
#define ASC_READ_CHIP_DC1(iot, ioh)				bus_space_read_2((iot), (ioh), ASC_IOP_REG_DC1)
#define ASC_WRITE_CHIP_DC1(iot, ioh)				bus_space_write_2((iot), (ioh), ASC_IOP_REG_DC1, data)
#define ASC_READ_CHIP_DVC_ID(iot, ioh)				bus_space_read_1((iot), (ioh), ASC_IOP_REG_ID)
#define ASC_WRITE_CHIP_DVC_ID(iot, ioh, data)			bus_space_write_1((iot), (ioh), ASC_IOP_REG_ID, data)


/******************************************************************************/
/*                                Exported functions                          */
/******************************************************************************/


void AscInitASC_SOFTC(ASC_SOFTC *);
u_int16_t AscInitFromEEP(ASC_SOFTC *);
u_int16_t AscInitFromASC_SOFTC(ASC_SOFTC *);
int AscInitDriver(ASC_SOFTC *);
void AscReInitLram(ASC_SOFTC *);
int AscFindSignature(bus_space_tag_t, bus_space_handle_t);
int AscISR(ASC_SOFTC *);
int AscExeScsiQueue(ASC_SOFTC *, ASC_SCSI_Q *);
void AscInquiryHandling(ASC_SOFTC *, u_int8_t, ASC_SCSI_INQUIRY *);
int AscAbortCCB(ASC_SOFTC *, u_int32_t);
int AscResetBus(ASC_SOFTC *);
int AscResetDevice(ASC_SOFTC *, u_char);


/******************************************************************************/

#endif	/* _ADVANSYS_NARROW_LIBRARY_H_ */
