#ifndef __GENWQE_CARD_H__
#define __GENWQE_CARD_H__

/**
 * IBM Accelerator Family 'GenWQE'
 *
 * (C) Copyright IBM Corp. 2013
 *
 * Author: Frank Haverkamp <haver@linux.vnet.ibm.com>
 * Author: Joerg-Stephan Vogt <jsvogt@de.ibm.com>
 * Author: Michael Jung <mijung@gmx.net>
 * Author: Michael Ruettger <michael@ibmra.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/*
 * User-space API for the GenWQE card. For debugging and test purposes
 * the register addresses are included here too.
 */

#include <linux/types.h>
#include <linux/ioctl.h>

/* Basename of sysfs, debugfs and /dev interfaces */
#define GENWQE_DEVNAME			"genwqe"

#define GENWQE_TYPE_ALTERA_230		0x00 /* GenWQE4 Stratix-IV-230 */
#define GENWQE_TYPE_ALTERA_530		0x01 /* GenWQE4 Stratix-IV-530 */
#define GENWQE_TYPE_ALTERA_A4		0x02 /* GenWQE5 A4 Stratix-V-A4 */
#define GENWQE_TYPE_ALTERA_A7		0x03 /* GenWQE5 A7 Stratix-V-A7 */

/* MMIO Unit offsets: Each UnitID occupies a defined address range */
#define GENWQE_UID_OFFS(uid)		((uid) << 24)
#define GENWQE_SLU_OFFS			GENWQE_UID_OFFS(0)
#define GENWQE_HSU_OFFS			GENWQE_UID_OFFS(1)
#define GENWQE_APP_OFFS			GENWQE_UID_OFFS(2)
#define GENWQE_MAX_UNITS		3

/* Common offsets per UnitID */
#define IO_EXTENDED_ERROR_POINTER	0x00000048
#define IO_ERROR_INJECT_SELECTOR	0x00000060
#define IO_EXTENDED_DIAG_SELECTOR	0x00000070
#define IO_EXTENDED_DIAG_READ_MBX	0x00000078
#define IO_EXTENDED_DIAG_MAP(ring)	(0x00000500 | ((ring) << 3))

#define GENWQE_EXTENDED_DIAG_SELECTOR(ring, trace) (((ring) << 8) | (trace))

/* UnitID 0: Service Layer Unit (SLU) */

/* SLU: Unit Configuration Register */
#define IO_SLU_UNITCFG			0x00000000
#define IO_SLU_UNITCFG_TYPE_MASK	0x000000000ff00000 /* 27:20 */

/* SLU: Fault Isolation Register (FIR) (ac_slu_fir) */
#define IO_SLU_FIR			0x00000008 /* read only, wr direct */
#define IO_SLU_FIR_CLR			0x00000010 /* read and clear */

/* SLU: First Error Capture Register (FEC/WOF) */
#define IO_SLU_FEC			0x00000018

#define IO_SLU_ERR_ACT_MASK		0x00000020
#define IO_SLU_ERR_ATTN_MASK		0x00000028
#define IO_SLU_FIRX1_ACT_MASK		0x00000030
#define IO_SLU_FIRX0_ACT_MASK		0x00000038
#define IO_SLU_SEC_LEM_DEBUG_OVR	0x00000040
#define IO_SLU_EXTENDED_ERR_PTR		0x00000048
#define IO_SLU_COMMON_CONFIG		0x00000060

#define IO_SLU_FLASH_FIR		0x00000108
#define IO_SLU_SLC_FIR			0x00000110
#define IO_SLU_RIU_TRAP			0x00000280
#define IO_SLU_FLASH_FEC		0x00000308
#define IO_SLU_SLC_FEC			0x00000310

/*
 * The  Virtual Function's Access is from offset 0x00010000
 * The Physical Function's Access is from offset 0x00050000
 * Single Shared Registers exists only at offset 0x00060000
 *
 * SLC: Queue Virtual Window Window for accessing into a specific VF
 * queue. When accessing the 0x10000 space using the 0x50000 address
 * segment, the value indicated here is used to specify which VF
 * register is decoded. This register, and the 0x50000 register space
 * can only be accessed by the PF. Example, if this register is set to
 * 0x2, then a read from 0x50000 is the same as a read from 0x10000
 * from VF=2.
 */

/* SLC: Queue Segment */
#define IO_SLC_QUEUE_SEGMENT		0x00010000
#define IO_SLC_VF_QUEUE_SEGMENT		0x00050000

/* SLC: Queue Offset */
#define IO_SLC_QUEUE_OFFSET		0x00010008
#define IO_SLC_VF_QUEUE_OFFSET		0x00050008

/* SLC: Queue Configuration */
#define IO_SLC_QUEUE_CONFIG		0x00010010
#define IO_SLC_VF_QUEUE_CONFIG		0x00050010

/* SLC: Job Timout/Only accessible for the PF */
#define IO_SLC_APPJOB_TIMEOUT		0x00010018
#define IO_SLC_VF_APPJOB_TIMEOUT	0x00050018
#define TIMEOUT_250MS			0x0000000f
#define HEARTBEAT_DISABLE		0x0000ff00

/* SLC: Queue InitSequence Register */
#define	IO_SLC_QUEUE_INITSQN		0x00010020
#define	IO_SLC_VF_QUEUE_INITSQN		0x00050020

/* SLC: Queue Wrap */
#define IO_SLC_QUEUE_WRAP		0x00010028
#define IO_SLC_VF_QUEUE_WRAP		0x00050028

/* SLC: Queue Status */
#define IO_SLC_QUEUE_STATUS		0x00010100
#define IO_SLC_VF_QUEUE_STATUS		0x00050100

/* SLC: Queue Working Time */
#define IO_SLC_QUEUE_WTIME		0x00010030
#define IO_SLC_VF_QUEUE_WTIME		0x00050030

/* SLC: Queue Error Counts */
#define IO_SLC_QUEUE_ERRCNTS		0x00010038
#define IO_SLC_VF_QUEUE_ERRCNTS		0x00050038

/* SLC: Queue Loast Response Word */
#define IO_SLC_QUEUE_LRW		0x00010040
#define IO_SLC_VF_QUEUE_LRW		0x00050040

/* SLC: Freerunning Timer */
#define IO_SLC_FREE_RUNNING_TIMER	0x00010108
#define IO_SLC_VF_FREE_RUNNING_TIMER	0x00050108

/* SLC: Queue Virtual Access Region */
#define IO_PF_SLC_VIRTUAL_REGION	0x00050000

/* SLC: Queue Virtual Window */
#define IO_PF_SLC_VIRTUAL_WINDOW	0x00060000

/* SLC: DDCB Application Job Pending [n] (n=0:63) */
#define IO_PF_SLC_JOBPEND(n)		(0x00061000 + 8*(n))
#define IO_SLC_JOBPEND(n)		IO_PF_SLC_JOBPEND(n)

/* SLC: Parser Trap RAM [n] (n=0:31) */
#define IO_SLU_SLC_PARSE_TRAP(n)	(0x00011000 + 8*(n))

/* SLC: Dispatcher Trap RAM [n] (n=0:31) */
#define IO_SLU_SLC_DISP_TRAP(n)	(0x00011200 + 8*(n))

/* Global Fault Isolation Register (GFIR) */
#define IO_SLC_CFGREG_GFIR		0x00020000
#define GFIR_ERR_TRIGGER		0x0000ffff

/* SLU: Soft Reset Register */
#define IO_SLC_CFGREG_SOFTRESET		0x00020018

/* SLU: Misc Debug Register */
#define IO_SLC_MISC_DEBUG		0x00020060
#define IO_SLC_MISC_DEBUG_CLR		0x00020068
#define IO_SLC_MISC_DEBUG_SET		0x00020070

/* Temperature Sensor Reading */
#define IO_SLU_TEMPERATURE_SENSOR	0x00030000
#define IO_SLU_TEMPERATURE_CONFIG	0x00030008

/* Voltage Margining Control */
#define IO_SLU_VOLTAGE_CONTROL		0x00030080
#define IO_SLU_VOLTAGE_NOMINAL		0x00000000
#define IO_SLU_VOLTAGE_DOWN5		0x00000006
#define IO_SLU_VOLTAGE_UP5		0x00000007

/* Direct LED Control Register */
#define IO_SLU_LEDCONTROL		0x00030100

/* SLU: Flashbus Direct Access -A5 */
#define IO_SLU_FLASH_DIRECTACCESS	0x00040010

/* SLU: Flashbus Direct Access2 -A5 */
#define IO_SLU_FLASH_DIRECTACCESS2	0x00040020

/* SLU: Flashbus Command Interface -A5 */
#define IO_SLU_FLASH_CMDINTF		0x00040030

/* SLU: BitStream Loaded */
#define IO_SLU_BITSTREAM		0x00040040

/* This Register has a switch which will change the CAs to UR */
#define IO_HSU_ERR_BEHAVIOR		0x01001010

#define IO_SLC2_SQB_TRAP		0x00062000
#define IO_SLC2_QUEUE_MANAGER_TRAP	0x00062008
#define IO_SLC2_FLS_MASTER_TRAP		0x00062010

/* UnitID 1: HSU Registers */
#define IO_HSU_UNITCFG			0x01000000
#define IO_HSU_FIR			0x01000008
#define IO_HSU_FIR_CLR			0x01000010
#define IO_HSU_FEC			0x01000018
#define IO_HSU_ERR_ACT_MASK		0x01000020
#define IO_HSU_ERR_ATTN_MASK		0x01000028
#define IO_HSU_FIRX1_ACT_MASK		0x01000030
#define IO_HSU_FIRX0_ACT_MASK		0x01000038
#define IO_HSU_SEC_LEM_DEBUG_OVR	0x01000040
#define IO_HSU_EXTENDED_ERR_PTR		0x01000048
#define IO_HSU_COMMON_CONFIG		0x01000060

/* UnitID 2: Application Unit (APP) */
#define IO_APP_UNITCFG			0x02000000
#define IO_APP_FIR			0x02000008
#define IO_APP_FIR_CLR			0x02000010
#define IO_APP_FEC			0x02000018
#define IO_APP_ERR_ACT_MASK		0x02000020
#define IO_APP_ERR_ATTN_MASK		0x02000028
#define IO_APP_FIRX1_ACT_MASK		0x02000030
#define IO_APP_FIRX0_ACT_MASK		0x02000038
#define IO_APP_SEC_LEM_DEBUG_OVR	0x02000040
#define IO_APP_EXTENDED_ERR_PTR		0x02000048
#define IO_APP_COMMON_CONFIG		0x02000060

#define IO_APP_DEBUG_REG_01		0x02010000
#define IO_APP_DEBUG_REG_02		0x02010008
#define IO_APP_DEBUG_REG_03		0x02010010
#define IO_APP_DEBUG_REG_04		0x02010018
#define IO_APP_DEBUG_REG_05		0x02010020
#define IO_APP_DEBUG_REG_06		0x02010028
#define IO_APP_DEBUG_REG_07		0x02010030
#define IO_APP_DEBUG_REG_08		0x02010038
#define IO_APP_DEBUG_REG_09		0x02010040
#define IO_APP_DEBUG_REG_10		0x02010048
#define IO_APP_DEBUG_REG_11		0x02010050
#define IO_APP_DEBUG_REG_12		0x02010058
#define IO_APP_DEBUG_REG_13		0x02010060
#define IO_APP_DEBUG_REG_14		0x02010068
#define IO_APP_DEBUG_REG_15		0x02010070
#define IO_APP_DEBUG_REG_16		0x02010078
#define IO_APP_DEBUG_REG_17		0x02010080
#define IO_APP_DEBUG_REG_18		0x02010088

/* Read/write from/to registers */
struct genwqe_reg_io {
	__u64 num;		/* register offset/address */
	__u64 val64;
};

/*
 * All registers of our card will return values not equal this values.
 * If we see IO_ILLEGAL_VALUE on any of our MMIO register reads, the
 * card can be considered as unusable. It will need recovery.
 */
#define IO_ILLEGAL_VALUE		0xffffffffffffffffull

/*
 * Generic DDCB execution interface.
 *
 * This interface is a first prototype resulting from discussions we
 * had with other teams which wanted to use the Genwqe card. It allows
 * to issue a DDCB request in a generic way. The request will block
 * until it finishes or time out with error.
 *
 * Some DDCBs require DMA addresses to be specified in the ASIV
 * block. The interface provies the capability to let the kernel
 * driver know where those addresses are by specifying the ATS field,
 * such that it can replace the user-space addresses with appropriate
 * DMA addresses or DMA addresses of a scatter gather list which is
 * dynamically created.
 *
 * Our hardware will refuse DDCB execution if the ATS field is not as
 * expected. That means the DDCB execution engine in the chip knows
 * where it expects DMA addresses within the ASIV part of the DDCB and
 * will check that against the ATS field definition. Any invalid or
 * unknown ATS content will lead to DDCB refusal.
 */

/* Genwqe chip Units */
#define DDCB_ACFUNC_SLU			0x00  /* chip service layer unit */
#define DDCB_ACFUNC_APP			0x01  /* chip application */

/* DDCB return codes (RETC) */
#define DDCB_RETC_IDLE			0x0000 /* Unexecuted/DDCB created */
#define DDCB_RETC_PENDING		0x0101 /* Pending Execution */
#define DDCB_RETC_COMPLETE		0x0102 /* Cmd complete. No error */
#define DDCB_RETC_FAULT			0x0104 /* App Err, recoverable */
#define DDCB_RETC_ERROR			0x0108 /* App Err, non-recoverable */
#define DDCB_RETC_FORCED_ERROR		0x01ff /* overwritten by driver  */

#define DDCB_RETC_UNEXEC		0x0110 /* Unexe/Removed from queue */
#define DDCB_RETC_TERM			0x0120 /* Terminated */
#define DDCB_RETC_RES0			0x0140 /* Reserved */
#define DDCB_RETC_RES1			0x0180 /* Reserved */

/* DDCB Command Options (CMDOPT) */
#define DDCB_OPT_ECHO_FORCE_NO		0x0000 /* ECHO DDCB */
#define DDCB_OPT_ECHO_FORCE_102		0x0001 /* force return code */
#define DDCB_OPT_ECHO_FORCE_104		0x0002
#define DDCB_OPT_ECHO_FORCE_108		0x0003

#define DDCB_OPT_ECHO_FORCE_110		0x0004 /* only on PF ! */
#define DDCB_OPT_ECHO_FORCE_120		0x0005
#define DDCB_OPT_ECHO_FORCE_140		0x0006
#define DDCB_OPT_ECHO_FORCE_180		0x0007

#define DDCB_OPT_ECHO_COPY_NONE		(0 << 5)
#define DDCB_OPT_ECHO_COPY_ALL		(1 << 5)

/* Definitions of Service Layer Commands */
#define SLCMD_ECHO_SYNC			0x00 /* PF/VF */
#define SLCMD_MOVE_FLASH		0x06 /* PF only */
#define SLCMD_MOVE_FLASH_FLAGS_MODE	0x03 /* bit 0 and 1 used for mode */
#define SLCMD_MOVE_FLASH_FLAGS_DLOAD	0	/* mode: download  */
#define SLCMD_MOVE_FLASH_FLAGS_EMUL	1	/* mode: emulation */
#define SLCMD_MOVE_FLASH_FLAGS_UPLOAD	2	/* mode: upload	   */
#define SLCMD_MOVE_FLASH_FLAGS_VERIFY	3	/* mode: verify	   */
#define SLCMD_MOVE_FLASH_FLAG_NOTAP	(1 << 2)/* just dump DDCB and exit */
#define SLCMD_MOVE_FLASH_FLAG_POLL	(1 << 3)/* wait for RETC >= 0102   */
#define SLCMD_MOVE_FLASH_FLAG_PARTITION	(1 << 4)
#define SLCMD_MOVE_FLASH_FLAG_ERASE	(1 << 5)

enum genwqe_card_state {
	GENWQE_CARD_UNUSED = 0,
	GENWQE_CARD_USED = 1,
	GENWQE_CARD_FATAL_ERROR = 2,
	GENWQE_CARD_RELOAD_BITSTREAM = 3,
	GENWQE_CARD_STATE_MAX,
};

/* common struct for chip image exchange */
struct genwqe_bitstream {
	__u64 data_addr;		/* pointer to image data */
	__u32 size;			/* size of image file */
	__u32 crc;			/* crc of this image */
	__u64 target_addr;		/* starting address in Flash */
	__u32 partition;		/* '0', '1', or 'v' */
	__u32 uid;			/* 1=host/x=dram */

	__u64 slu_id;			/* informational/sim: SluID */
	__u64 app_id;			/* informational/sim: AppID */

	__u16 retc;			/* returned from processing */
	__u16 attn;			/* attention code from processing */
	__u32 progress;			/* progress code from processing */
};

/* Issuing a specific DDCB command */
#define DDCB_LENGTH			256 /* for debug data */
#define DDCB_ASIV_LENGTH		104 /* len of the DDCB ASIV array */
#define DDCB_ASIV_LENGTH_ATS		96  /* ASIV in ATS architecture */
#define DDCB_ASV_LENGTH			64  /* len of the DDCB ASV array  */
#define DDCB_FIXUPS			12  /* maximum number of fixups */

struct genwqe_debug_data {
	char driver_version[64];
	__u64 slu_unitcfg;
	__u64 app_unitcfg;

	__u8  ddcb_before[DDCB_LENGTH];
	__u8  ddcb_prev[DDCB_LENGTH];
	__u8  ddcb_finished[DDCB_LENGTH];
};

/*
 * Address Translation Specification (ATS) definitions
 *
 * Each 4 bit within the ATS 64-bit word specify the required address
 * translation at the defined offset.
 *
 * 63 LSB
 *         6666.5555.5555.5544.4444.4443.3333.3333 ... 11
 *         3210.9876.5432.1098.7654.3210.9876.5432 ... 1098.7654.3210
 *
 * offset: 0x00 0x08 0x10 0x18 0x20 0x28 0x30 0x38 ... 0x68 0x70 0x78
 *         res  res  res  res  ASIV ...
 * The first 4 entries in the ATS word are reserved. The following nibbles
 * each describe at an 8 byte offset the format of the required data.
 */
#define ATS_TYPE_DATA			0x0ull /* data  */
#define ATS_TYPE_FLAT_RD		0x4ull /* flat buffer read only */
#define ATS_TYPE_FLAT_RDWR		0x5ull /* flat buffer read/write */
#define ATS_TYPE_SGL_RD			0x6ull /* sgl read only */
#define ATS_TYPE_SGL_RDWR		0x7ull /* sgl read/write */

#define ATS_SET_FLAGS(_struct, _field, _flags)				\
	(((_flags) & 0xf) << (44 - (4 * (offsetof(_struct, _field) / 8))))

#define ATS_GET_FLAGS(_ats, _byte_offs)					\
	(((_ats)	  >> (44 - (4 * ((_byte_offs) / 8)))) & 0xf)

/**
 * struct genwqe_ddcb_cmd - User parameter for generic DDCB commands
 *
 * On the way into the kernel the driver will read the whole data
 * structure. On the way out the driver will not copy the ASIV data
 * back to user-space.
 */
struct genwqe_ddcb_cmd {
	/* START of data copied to/from driver */
	__u64 next_addr;		/* chaining genwqe_ddcb_cmd */
	__u64 flags;			/* reserved */

	__u8  acfunc;			/* accelerators functional unit */
	__u8  cmd;			/* command to execute */
	__u8  asiv_length;		/* used parameter length */
	__u8  asv_length;		/* length of valid return values  */
	__u16 cmdopts;			/* command options */
	__u16 retc;			/* return code from processing    */

	__u16 attn;			/* attention code from processing */
	__u16 vcrc;			/* variant crc16 */
	__u32 progress;			/* progress code from processing  */

	__u64 deque_ts;			/* dequeue time stamp */
	__u64 cmplt_ts;			/* completion time stamp */
	__u64 disp_ts;			/* SW processing start */

	/* move to end and avoid copy-back */
	__u64 ddata_addr;		/* collect debug data */

	/* command specific values */
	__u8  asv[DDCB_ASV_LENGTH];

	/* END of data copied from driver */
	union {
		struct {
			__u64 ats;
			__u8  asiv[DDCB_ASIV_LENGTH_ATS];
		};
		/* used for flash update to keep it backward compatible */
		__u8 __asiv[DDCB_ASIV_LENGTH];
	};
	/* END of data copied to driver */
};

#define GENWQE_IOC_CODE	    0xa5

/* Access functions */
#define GENWQE_READ_REG64   _IOR(GENWQE_IOC_CODE, 30, struct genwqe_reg_io)
#define GENWQE_WRITE_REG64  _IOW(GENWQE_IOC_CODE, 31, struct genwqe_reg_io)
#define GENWQE_READ_REG32   _IOR(GENWQE_IOC_CODE, 32, struct genwqe_reg_io)
#define GENWQE_WRITE_REG32  _IOW(GENWQE_IOC_CODE, 33, struct genwqe_reg_io)
#define GENWQE_READ_REG16   _IOR(GENWQE_IOC_CODE, 34, struct genwqe_reg_io)
#define GENWQE_WRITE_REG16  _IOW(GENWQE_IOC_CODE, 35, struct genwqe_reg_io)

#define GENWQE_GET_CARD_STATE _IOR(GENWQE_IOC_CODE, 36,	enum genwqe_card_state)

/**
 * struct genwqe_mem - Memory pinning/unpinning information
 * @addr:          virtual user space address
 * @size:          size of the area pin/dma-map/unmap
 * direction:      0: read/1: read and write
 *
 * Avoid pinning and unpinning of memory pages dynamically. Instead
 * the idea is to pin the whole buffer space required for DDCB
 * opertionas in advance. The driver will reuse this pinning and the
 * memory associated with it to setup the sglists for the DDCB
 * requests without the need to allocate and free memory or map and
 * unmap to get the DMA addresses.
 *
 * The inverse operation needs to be called after the pinning is not
 * needed anymore. The pinnings else the pinnings will get removed
 * after the device is closed. Note that pinnings will required
 * memory.
 */
struct genwqe_mem {
	__u64 addr;
	__u64 size;
	__u64 direction;
	__u64 flags;
};

#define GENWQE_PIN_MEM	      _IOWR(GENWQE_IOC_CODE, 40, struct genwqe_mem)
#define GENWQE_UNPIN_MEM      _IOWR(GENWQE_IOC_CODE, 41, struct genwqe_mem)

/*
 * Generic synchronous DDCB execution interface.
 * Synchronously execute a DDCB.
 *
 * Return: 0 on success or negative error code.
 *         -EINVAL: Invalid parameters (ASIV_LEN, ASV_LEN, illegal fixups
 *                  no mappings found/could not create mappings
 *         -EFAULT: illegal addresses in fixups, purging failed
 *         -EBADMSG: enqueing failed, retc != DDCB_RETC_COMPLETE
 */
#define GENWQE_EXECUTE_DDCB					\
	_IOWR(GENWQE_IOC_CODE, 50, struct genwqe_ddcb_cmd)

#define GENWQE_EXECUTE_RAW_DDCB					\
	_IOWR(GENWQE_IOC_CODE, 51, struct genwqe_ddcb_cmd)

/* Service Layer functions (PF only) */
#define GENWQE_SLU_UPDATE  _IOWR(GENWQE_IOC_CODE, 80, struct genwqe_bitstream)
#define GENWQE_SLU_READ	   _IOWR(GENWQE_IOC_CODE, 81, struct genwqe_bitstream)

#endif	/* __GENWQE_CARD_H__ */
