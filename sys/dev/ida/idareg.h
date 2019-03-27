/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999,2000 Jonathan Lemon
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

/*
 * #defines and software structures for the Compaq RAID card
 */

/*
 * board register offsets for SMART-2 controllers
 */
#define	R_CMD_FIFO	0x04
#define	R_DONE_FIFO	0x08
#define	R_INT_MASK	0x0C
#define	R_STATUS	0x10
#define	R_INT_PENDING	0x14

/*
 * interrupt mask values for SMART series
 */
#define	INT_DISABLE	0x00
#define	INT_ENABLE	0x01

/*
 * board offsets for the 42xx series
 */
#define	R_42XX_STATUS	0x30
#define	R_42XX_INT_MASK	0x34
#define	R_42XX_REQUEST	0x40
#define	R_42XX_REPLY	0x44

/*
 * interrupt values for 42xx series
 */
#define	INT_ENABLE_42XX			0x00
#define	INT_DISABLE_42XX		0x08
#define	STATUS_42XX_INT_PENDING		0x08

/*
 * return status codes
 */
#define	SOFT_ERROR	0x02	/* Non-fatal error. */
#define	HARD_ERROR	0x04	/* Fatal error. */
#define	INVALID_ERROR	0x10	/* Invalid Request Block. */
#define	CMD_REJECTED	0x14

/*
 * command types
 */
#define	CMD_GET_LOG_DRV_INFO	0x10	/* Identify controller */
#define	CMD_GET_CTRL_INFO	0x11	/* Identify logical driver */
#define	CMD_SENSE_DRV_STATUS	0x12	/* Sense logical drive status */
#define	CMD_START_RECOVERY	0x13	/* Start recover */
#define	CMD_GET_PHYS_DRV_INFO	0x15	/* Identify physical drive */
#define	CMD_BLINK_DRV_LEDS	0x16	/* Blink drive tray LEDs */
#define	CMD_SENSE_DRV_LEDS	0x17	/* Sense Blinking drive tray LEDs */
#define	CMD_GET_LOG_DRV_EXT	0x18	/* Identify logical drive, Extended */
#define	CMD_READ		0x20	/* Read */
#define	CMD_WRITE		0x30	/* Write */
#define	CMD_WRITE_MEDIA		0x31	/* Write media */
#define	CMD_RESET_CTRL		0x40	/* Reset controller */
#define	CMD_DIAG_PASS_THRU	0x41	/* ??? */
#define	CMD_GET_CONFIG		0x50	/* Sense configuration */
#define	CMD_SET_CONFIG		0x51	/* Set configuration */

#define	CMD_BYPASS_VOL_STATE	0x52	/* ??? */
#define	CMD_SS_CREATE_VOL	0x53	/* ??? */
#define	CMD_CHANGE_CONFIG	0x54	/* ??? */
#define	CMD_SENSE_ORIG_CONF	0x55	/* ??? */
#define	CMD_REORDER_LOG_DRV	0x56	/* ??? */

#define	CMD_LABEL_LOG_DRV	0x57	/* Label logical drive */
#define	CMD_SS_TO_VOL		0x58	/* ??? */
#define	CMD_SET_SURFACE_DELAY	0x60	/* Set surface delay */
#define	CMD_SET_OVERHEAT_DELAY	0x61	/* ??? */
#define	CMD_SENSE_BUS_PARAMS	0x65	/* Sense bus parameters */
#define	CMD_SENSE_SUBSYS_INFO	0x66	/* Sense Subsystem Information */
#define	CMD_SENSE_SURFACE_ATS	0x70	/* Sense surface analysis task status */
#define	CMD_PASSTHROUGH		0x90	/* Pass-through operation */
#define	CMD_PASSTHROUGH_A	0x91	/* ??? */
#define	CMD_RESET_SCSI_DEV	0x94	/* Reset SCSI device */
#define	CMD_PAUSE_BG_ACT	0x98	/* Pause Background Activity */
#define	CMD_RESUME_BG_ACT	0x99	/* Resume Background Activity */
#define	CMD_START_FIRMWARE	0x99	/* for integrated RAID */
#define	CMD_SENSE_DRV_ERR_LOG	0xa6	/* Sense drive error log */
#define	CMD_START_CPM		0xa7	/* Start controller performance monitoring */
#define	CMD_SENSE_CP		0xa8	/* Sense controller performance */
#define	CMD_STOP_CPM		0xa9	/* Stop controller performance monitoring */
#define	CMD_FLUSH_CACHE		0xc2	/* Flush/disable write cache */
#define	CMD_COLLECT_BUFFER	0xd2	/* ??? */
#define	CMD_ACCEPT_MEDIA_EXCH	0xe0	/* Accept Media Exchange */	

#define	CMD_FLASH_READ		0xf6	/* Read FLASH */
#define	CMD_FLASH_WRITE		0xf7	/* Write FLASH */

/* logical drive parameter table */
struct ida_drive_param {
	u_int16_t	ncylinders;
	u_int8_t	nheads;
	u_int8_t	signature;
	u_int8_t	psectors;
	u_int16_t	wprecomp;
	u_int8_t	max_acc;
	u_int8_t	control;
	u_int16_t	pcylinders;
	u_int8_t	ptracks;
	u_int16_t	landing_zone;
	u_int8_t	nsectors;
	u_int8_t	checksum;
} __packed;

#define	IDA_RAID0	0	/* No fault tolerance. */
#define	IDA_RAID4	1	/* Data Guard */
#define	IDA_RAID1	2	/* Mirroring */
#define	IDA_RAID5	3	/* Distributed Data Guard */

/*
 * CMD_GET_LOG_DRV_INFO (0x10)
 * Identify Logical Drive
 */
struct ida_drive_info {
	u_int16_t	secsize;	/* block size in bytes */
	u_int32_t	secperunit;	/* blocks available */
	struct ida_drive_param	dp;	/* logical drive parameter table */
	u_int8_t	mirror;		/* fault tolerance */
	u_int8_t	reserved;
	u_int8_t	bios_disable;
} __packed;

/*
 * CMD_GET_LOG_DRV_EXT (0x18)
 * Identify Logical Drive, Extended
 */
struct ida_drive_info_ext {
	u_int16_t	secsize;	/* block size in bytes */
	u_int32_t	secperunit;	/* blocks available */
	struct ida_drive_param	dp;	/* logical drive parameter table */
	u_int8_t	mirror;		/* fault tolerance */
	u_int8_t	reserved;
	u_int8_t	bios_disable;
	u_int32_t	ld_id;		/* Logical drive identifier */
	u_int8_t	ld_label[64];	/* Logical drive label */
} __packed;

/*
 * CMD_GET_CTRL_INFO (0x11)
 * Identify Controller
 */
struct ida_controller_info {
	u_int8_t	num_drvs;	/* Number of configured logical drives */
	u_int32_t	signature;	/* Configuration signature */
	u_int8_t	firm_rev[4];	/* ASCII firmware revision */
	u_int8_t	rom_rev[4];	/* ROM firmware revision */
	u_int8_t	hw_rev;		/* Revision level of the hardware */
	u_int32_t	bb_rev;
	u_int32_t	dp_map;		/* Drive present bit map */
	u_int32_t	ed_map;		/* External drive bit map */
	u_int32_t	board_id;
	u_int8_t	cfg_error;
	u_int32_t	nd_map;		/* Non-disk map */
	u_int8_t	bad_ram_addr;
	u_int8_t	cpu_rev;
	u_int8_t	pdpi_rev;
	u_int8_t	epic_rev;
	u_int8_t	wcxc_rev;
	u_int8_t	mkt_rev;	/* Marketing revision */
	u_int8_t	cflag;		/* Controller flags */
#define	IDA_CI_CFLAG_7DPB	(1<<3)
#define	IDA_CI_CFLAG_BIGMAP	(1<<7)
	u_int8_t	hflag;
	u_int8_t	expand_dis;
	u_int8_t	scsi_cc;	/* SCSI chip count */
	u_int32_t	max_req_blocks;
	u_int32_t	cclock;		/* Controller Clock */
	u_int8_t	dp_scsi;	/* Drives per SCSI bus */
	u_int16_t	big_dp_map[8];	/* Big drive present bit map */
	u_int16_t	big_ed_map[8];	/* Big external drive bit map */
	u_int16_t	big_nd_map[8];	/* Big non-disk map */
	u_int16_t	task_flags;
	u_int8_t	icl_bus;
	u_int8_t	red_modes;
	u_int8_t	cur_red_mode;
	u_int8_t	red_ctlr_stat;
	u_int8_t	red_fail_reason;
	u_int8_t	reserved[403];
} __packed;

/*
 * CMD_SENSE_DRV_STATUS (0x12)
 * Sense logical drive status
 */
struct ida_drive_status {
	u_int8_t	status;
	u_int32_t	failure_map;
	u_int16_t	read_err[32];
	u_int16_t	write_error[32];
	u_int8_t	reserved0[288];
	u_int32_t	secrecover;
	u_int8_t	rebuilding;
	u_int16_t	remap_cnt[32];
	u_int32_t	repl_map;
	u_int32_t	spare_map;
	u_int8_t	spare_status;
	u_int8_t	spare_repl_map[32];
	u_int32_t	repl_ok_map;
	u_int8_t	media_exchange;
	u_int8_t	cache_failure;
	u_int8_t	expand_failure;
	u_int8_t	unit_flags;
	u_int16_t	big_failure_map[8];
	u_int16_t	big_remap_cnt[128];
	u_int16_t	big_repl_map[8];
	u_int16_t	big_act_spare_map[8];
	u_int8_t	big_spare_repl_map[128];
	u_int16_t	big_repl_ok_map[8];
	u_int8_t	big_rebuilding;
	u_int8_t	reserved1[36];
} __packed;

/*
 * CMD_GET_PHYS_DRV_INFO (0x15)
 * Identify Physical Drive
 */
struct ida_phys_drv_info {
	u_int8_t	scsi_bus;	/* SCSI Bus */
	u_int8_t	scsi_id;	/* SCSI ID */
	u_int16_t	blksize;	/* block size in bytes */
	u_int32_t	blkcount;	/* total blocks */
	u_int32_t	blkreserved;	/* reserved blocks */
	u_int8_t	drv_model[40];	/* drive model */
	u_int8_t	drv_serial[40];	/* drive serial number */
	u_int8_t	drv_fwrev[8];	/* drive firmware revision */
	u_int8_t	scsi_inq;	/* SCSI inquiry bits */
	u_int8_t	cpq_drv_stmp;
	u_int8_t	last_fail;
	u_int8_t	pd_flags;	/* physical drive flags */
#define	PDF_DISK_PRESENT	0x01
#define	PDF_NONDISK_PRESENT	0x02
#define	PDF_WIDE_ENABLED	0x04
#define	PDF_SYNC		0x08
#define	PDF_NARROW_TRAY		0x10
#define	PDF_WIDEFAIL		0x20
#define	PDF_ULTRA		0x40
#define	PDF_ULTRA2		0x80
	u_int8_t	mpd_flags;	/* more physical drive flags */
#define	MPDF_SMART_SUPPORT	0x01	/* S.M.A.R.T supported */
#define	MPDF_SMART_ERRORS	0x02	/* S.M.A.R.T errors recorded */
#define	MPDF_SMART_ENABLED	0x04	/* S.M.A.R.T predictive failure is enabled */
#define	MPDF_SMART_ERR_RESET	0x08	/* S.M.A.R.T errors recorded since last reset */
#define	MPDF_DRIVE_EXTERNAL	0x10	/* Connected to external connector. */
#define	MPDF_DRIVE_CONF_LVOL	0x20	/* Configured as part of a logical volume */
#define	MPDF_DRIVE_CONF_SPARE	0x40	/* Configured as a spare */
#define	MPDF_DRIVE_WCE		0x80	/* Drive WCE set on spinup */
	u_int8_t	scsi_lun;
	u_int8_t	ympd_flags;	/* yet more physical drive flags */
#define	YMPDF_DRIVE_WCE_SET	0x40	/* WCE currently set */
#define	YMPDF_DRIVE_WCE_CHNG	0x80	/* WCE changeable */
	u_int8_t	reserved;
	u_int32_t	spi_speed_rules;
	u_int8_t	phys_con[2];	/* Physical Connector */
	u_int8_t	phys_box;	/* Physical Box on Bus */
	u_int8_t	phys_bay;	/* Physical Bay in Box */
} __packed;

/*
 * CMD_BLINK_DRV_LEDS (0x16)
 * Blink Drive Tray LEDs
 *
 * CMD_SENSE_DRV_LEDS (0x17)
 * Sense Blinking Drive Tray LEDs
 */
struct ida_blink_drv_leds {
	u_int32_t	bd;		/* Blink duration (in 10ths sec) */
	u_int32_t	bte;		/* Blink time elapsed (sense only) */
	u_int8_t	bse[256];	/* Blink/seek enable */
	u_int8_t	reserved1[248];
} __packed;

/*
 * CMD_LABEL_LOG_DRV (0x57)
 * Label Logical Drive
 */
struct ida_label_logical {
	u_int32_t	ld_id;		/* Logical drive identifier */
	u_int8_t	ld_label[64];	/* Logical drive label */
} __packed;
