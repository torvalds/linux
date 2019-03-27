/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
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
 *	$FreeBSD$
 */

#define MLX_BLKSIZE	512		/* fixed feature */
#define MLX_PAGE_SIZE	4096		/* controller, not cpu, attribute */

/*
 * Selected command codes.
 */
#define MLX_CMD_ENQUIRY_OLD	0x05
#define MLX_CMD_ENQUIRY		0x53
#define MLX_CMD_ENQUIRY2	0x1c
#define MLX_CMD_ENQSYSDRIVE	0x19
#define MLX_CMD_READSG		0xb6
#define MLX_CMD_WRITESG		0xb7
#define MLX_CMD_READSG_OLD	0x82
#define MLX_CMD_WRITESG_OLD	0x83
#define MLX_CMD_FLUSH		0x0a
#define MLX_CMD_LOGOP		0x72
#define MLX_CMD_REBUILDASYNC	0x16
#define MLX_CMD_CHECKASYNC	0x1e
#define MLX_CMD_REBUILDSTAT	0x0c
#define MLX_CMD_STOPCHANNEL	0x13
#define MLX_CMD_STARTCHANNEL	0x12
#define MLX_CMD_READ_CONFIG	0x4e
#define MLX_CMD_DIRECT_CDB	0x04
#define MLX_CMD_DEVICE_STATE	0x50

#ifdef _KERNEL

#define MLX_CFG_BASE0   0x10		/* first region */
#define MLX_CFG_BASE1   0x14		/* second region (type 3 only) */

/*
 * Status values.
 */
#define MLX_STATUS_OK		0x0000
#define MLX_STATUS_RDWROFFLINE	0x0002	/* read/write claims drive is offline */
#define MLX_STATUS_WEDGED	0xdead	/* controller not listening */
#define MLX_STATUS_LOST		0xbeef	/* never came back */
#define MLX_STATUS_BUSY		0xffff	/* command is in controller */

/*
 * Accessor defines for the V3 interface.
 */
#define MLX_V3_MAILBOX		0x00
#define	MLX_V3_STATUS_IDENT	0x0d
#define MLX_V3_STATUS		0x0e
#define MLX_V3_IDBR		0x40
#define MLX_V3_ODBR		0x41
#define MLX_V3_IER		0x43
#define MLX_V3_FWERROR		0x3f
#define MLX_V3_FWERROR_PARAM1	0x00
#define MLX_V3_FWERROR_PARAM2	0x01

#define MLX_V3_PUT_MAILBOX(sc, idx, val) bus_write_1(sc->mlx_mem, MLX_V3_MAILBOX + idx, val)
#define MLX_V3_GET_STATUS_IDENT(sc)	 bus_read_1 (sc->mlx_mem, MLX_V3_STATUS_IDENT)
#define MLX_V3_GET_STATUS(sc)		 bus_read_2 (sc->mlx_mem, MLX_V3_STATUS)
#define MLX_V3_GET_IDBR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V3_IDBR)
#define MLX_V3_PUT_IDBR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V3_IDBR, val)
#define MLX_V3_GET_ODBR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V3_ODBR)
#define MLX_V3_PUT_ODBR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V3_ODBR, val)
#define MLX_V3_PUT_IER(sc, val)		 bus_write_1(sc->mlx_mem, MLX_V3_IER, val)
#define MLX_V3_GET_FWERROR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V3_FWERROR)
#define MLX_V3_PUT_FWERROR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V3_FWERROR, val)
#define MLX_V3_GET_FWERROR_PARAM1(sc)	 bus_read_1 (sc->mlx_mem, MLX_V3_FWERROR_PARAM1)
#define MLX_V3_GET_FWERROR_PARAM2(sc)	 bus_read_1 (sc->mlx_mem, MLX_V3_FWERROR_PARAM2)

#define MLX_V3_IDB_FULL		(1<<0)		/* mailbox is full */
#define MLX_V3_IDB_INIT_BUSY	(1<<1)		/* initialisation in progress */

#define MLX_V3_IDB_SACK		(1<<1)		/* acknowledge status read */

#define MLX_V3_ODB_SAVAIL	(1<<0)		/* status is available */

#define MLX_V3_FWERROR_PEND	(1<<2)		/* firmware error pending */

/*
 * Accessor defines for the V4 interface.
 */
#define MLX_V4_MAILBOX		0x1000
#define MLX_V4_MAILBOX_LENGTH		16
#define MLX_V4_STATUS_IDENT	0x1018
#define MLX_V4_STATUS		0x101a
#define MLX_V4_IDBR		0x0020
#define MLX_V4_ODBR		0x002c
#define MLX_V4_IER		0x0034
#define MLX_V4_FWERROR		0x103f
#define MLX_V4_FWERROR_PARAM1	0x1000
#define MLX_V4_FWERROR_PARAM2	0x1001

/* use longword access? */
#define MLX_V4_PUT_MAILBOX(sc, idx, val) bus_write_1(sc->mlx_mem, MLX_V4_MAILBOX + idx, val)
#define MLX_V4_GET_STATUS_IDENT(sc)	 bus_read_1 (sc->mlx_mem, MLX_V4_STATUS_IDENT)
#define MLX_V4_GET_STATUS(sc)		 bus_read_2 (sc->mlx_mem, MLX_V4_STATUS)
#define MLX_V4_GET_IDBR(sc)		 bus_read_4 (sc->mlx_mem, MLX_V4_IDBR)
#define MLX_V4_PUT_IDBR(sc, val)	 bus_write_4(sc->mlx_mem, MLX_V4_IDBR, val)
#define MLX_V4_GET_ODBR(sc)		 bus_read_4 (sc->mlx_mem, MLX_V4_ODBR)
#define MLX_V4_PUT_ODBR(sc, val)	 bus_write_4(sc->mlx_mem, MLX_V4_ODBR, val)
#define MLX_V4_PUT_IER(sc, val)		 bus_write_4(sc->mlx_mem, MLX_V4_IER, val)
#define MLX_V4_GET_FWERROR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V4_FWERROR)
#define MLX_V4_PUT_FWERROR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V4_FWERROR, val)
#define MLX_V4_GET_FWERROR_PARAM1(sc)	 bus_read_1 (sc->mlx_mem, MLX_V4_FWERROR_PARAM1)
#define MLX_V4_GET_FWERROR_PARAM2(sc)	 bus_read_1 (sc->mlx_mem, MLX_V4_FWERROR_PARAM2)

#define MLX_V4_IDB_FULL		(1<<0)		/* mailbox is full */
#define MLX_V4_IDB_INIT_BUSY	(1<<1)		/* initialisation in progress */

#define MLX_V4_IDB_HWMBOX_CMD	(1<<0)		/* posted hardware mailbox command */
#define MLX_V4_IDB_SACK		(1<<1)		/* acknowledge status read */
#define MLX_V4_IDB_MEMMBOX_CMD	(1<<4)		/* posted memory mailbox command */

#define MLX_V4_ODB_HWSAVAIL	(1<<0)		/* status is available for hardware mailbox */
#define MLX_V4_ODB_MEMSAVAIL	(1<<1)		/* status is available for memory mailbox */

#define MLX_V4_ODB_HWMBOX_ACK	(1<<0)		/* ack status read from hardware mailbox */
#define MLX_V4_ODB_MEMMBOX_ACK	(1<<1)		/* ack status read from memory mailbox */

#define MLX_V4_IER_MASK		0xfb		/* message unit interrupt mask */
#define MLX_V4_IER_DISINT	(1<<2)		/* interrupt disable bit */

#define MLX_V4_FWERROR_PEND	(1<<2)		/* firmware error pending */

/*
 * Accessor defines for the V5 interface
 */
#define MLX_V5_MAILBOX		0x50
#define MLX_V5_MAILBOX_LENGTH		16
#define MLX_V5_STATUS_IDENT	0x5d
#define MLX_V5_STATUS		0x5e
#define MLX_V5_IDBR		0x60
#define MLX_V5_ODBR		0x61
#define MLX_V5_IER		0x34
#define MLX_V5_FWERROR		0x63
#define MLX_V5_FWERROR_PARAM1	0x50
#define MLX_V5_FWERROR_PARAM2	0x51

#define MLX_V5_PUT_MAILBOX(sc, idx, val) bus_write_1(sc->mlx_mem, MLX_V5_MAILBOX + idx, val)
#define MLX_V5_GET_STATUS_IDENT(sc)	 bus_read_1 (sc->mlx_mem, MLX_V5_STATUS_IDENT)
#define MLX_V5_GET_STATUS(sc)		 bus_read_2 (sc->mlx_mem, MLX_V5_STATUS)
#define MLX_V5_GET_IDBR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V5_IDBR)
#define MLX_V5_PUT_IDBR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V5_IDBR, val)
#define MLX_V5_GET_ODBR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V5_ODBR)
#define MLX_V5_PUT_ODBR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V5_ODBR, val)
#define MLX_V5_PUT_IER(sc, val)		 bus_write_1(sc->mlx_mem, MLX_V5_IER, val)
#define MLX_V5_GET_FWERROR(sc)		 bus_read_1 (sc->mlx_mem, MLX_V5_FWERROR)
#define MLX_V5_PUT_FWERROR(sc, val)	 bus_write_1(sc->mlx_mem, MLX_V5_FWERROR, val)
#define MLX_V5_GET_FWERROR_PARAM1(sc)	 bus_read_1 (sc->mlx_mem, MLX_V5_FWERROR_PARAM1)
#define MLX_V5_GET_FWERROR_PARAM2(sc)	 bus_read_1 (sc->mlx_mem, MLX_V5_FWERROR_PARAM2)

#define MLX_V5_IDB_EMPTY	(1<<0)		/* mailbox is empty */
#define MLX_V5_IDB_INIT_DONE	(1<<1)		/* initialisation has completed */

#define MLX_V5_IDB_HWMBOX_CMD	(1<<0)		/* posted hardware mailbox command */
#define MLX_V5_IDB_SACK		(1<<1)		/* acknowledge status read */
#define MLX_V5_IDB_RESET	(1<<3)		/* reset request */
#define MLX_V5_IDB_MEMMBOX_CMD	(1<<4)		/* posted memory mailbox command */

#define MLX_V5_ODB_HWSAVAIL	(1<<0)		/* status is available for hardware mailbox */
#define MLX_V5_ODB_MEMSAVAIL	(1<<1)		/* status is available for memory mailbox */

#define MLX_V5_ODB_HWMBOX_ACK	(1<<0)		/* ack status read from hardware mailbox */
#define MLX_V5_ODB_MEMMBOX_ACK	(1<<1)		/* ack status read from memory mailbox */

#define MLX_V5_IER_DISINT	(1<<2)		/* interrupt disable bit */

#define MLX_V5_FWERROR_PEND	(1<<2)		/* firmware error pending */

#endif /* _KERNEL */

/*
 * Scatter-gather list format, type 1, kind 00.
 */
struct mlx_sgentry 
{
    u_int32_t	sg_addr;
    u_int32_t	sg_count;
} __packed;

/*
 * Command result buffers, as placed in system memory by the controller.
 */

struct mlx_enquiry_old	/* MLX_CMD_ENQUIRY_OLD */
{
    u_int8_t		me_num_sys_drvs;
    u_int8_t		res1[3];
    u_int32_t		me_drvsize[8];
    u_int16_t		me_flash_age;
    u_int8_t		me_status_flags;
    u_int8_t		me_free_state_change_count;
    u_int8_t		me_fwminor;
    u_int8_t		me_fwmajor;
    u_int8_t		me_rebuild_flag;
    u_int8_t		me_max_commands;
    u_int8_t		me_offline_sd_count;
    u_int8_t		res3;
    u_int8_t		me_critical_sd_count;
    u_int8_t		res4[3];
    u_int8_t		me_dead_count;
    u_int8_t		res5;
    u_int8_t		me_rebuild_count;
    u_int8_t		me_misc_flags;
    struct 
    {
	u_int8_t	dd_targ;
	u_int8_t	dd_chan;
    } __packed me_dead[20];
} __packed;

struct mlx_enquiry	/* MLX_CMD_ENQUIRY */
{
    u_int8_t		me_num_sys_drvs;
    u_int8_t		res1[3];
    u_int32_t		me_drvsize[32];
    u_int16_t		me_flash_age;
    u_int8_t		me_status_flags;
#define MLX_ENQ_SFLAG_DEFWRERR	(1<<0)	/* deferred write error indicator */
#define MLX_ENQ_SFLAG_BATTLOW	(1<<1)	/* battery low */
    u_int8_t		res2;
    u_int8_t		me_fwminor;
    u_int8_t		me_fwmajor;
    u_int8_t		me_rebuild_flag;
    u_int8_t		me_max_commands;
    u_int8_t		me_offline_sd_count;
    u_int8_t		res3;
    u_int16_t		me_event_log_seq_num;
    u_int8_t		me_critical_sd_count;
    u_int8_t		res4[3];
    u_int8_t		me_dead_count;
    u_int8_t		res5;
    u_int8_t		me_rebuild_count;
    u_int8_t		me_misc_flags;
#define MLX_ENQ_MISC_BBU	(1<<3)	/* battery backup present */
    struct 
    {
	u_int8_t	dd_targ;
	u_int8_t	dd_chan;
    } __packed me_dead[20];
} __packed;

struct mlx_enquiry2	/* MLX_CMD_ENQUIRY2 */
{
    u_int32_t		me_hardware_id;
    u_int32_t		me_firmware_id;
    u_int32_t		res1;
    u_int8_t		me_configured_channels;
    u_int8_t		me_actual_channels;
    u_int8_t		me_max_targets;
    u_int8_t		me_max_tags;
    u_int8_t		me_max_sys_drives;
    u_int8_t		me_max_arms;
    u_int8_t		me_max_spans;
    u_int8_t		res2;
    u_int32_t		res3;
    u_int32_t		me_mem_size;
    u_int32_t		me_cache_size;
    u_int32_t		me_flash_size;
    u_int32_t		me_nvram_size;
    u_int16_t		me_mem_type;
    u_int16_t		me_clock_speed;
    u_int16_t		me_mem_speed;
    u_int16_t		me_hardware_speed;
    u_int8_t		res4[12];
    u_int16_t		me_max_commands;
    u_int16_t		me_max_sg;
    u_int16_t		me_max_dp;
    u_int16_t		me_max_iod;
    u_int16_t		me_max_comb;
    u_int8_t		me_latency;
    u_int8_t		res5;
    u_int8_t		me_scsi_timeout;
    u_int8_t		res6;
    u_int16_t		me_min_freelines;
    u_int8_t		res7[8];
    u_int8_t		me_rate_const;
    u_int8_t		res8[11];
    u_int16_t		me_physblk;
    u_int16_t		me_logblk;
    u_int16_t		me_maxblk;
    u_int16_t		me_blocking_factor;
    u_int16_t		me_cacheline;
    u_int8_t		me_scsi_cap;
    u_int8_t		res9[5];
    u_int16_t		me_firmware_build;
    u_int8_t		me_fault_mgmt_type;
    u_int8_t		res10;
    u_int32_t		me_firmware_features;
    u_int8_t		res11[8];
} __packed;

struct mlx_enq_sys_drive /* MLX_CMD_ENQSYSDRIVE returns an array of 32 of these */
{
    u_int32_t		sd_size;
    u_int8_t		sd_state;
    u_int8_t		sd_raidlevel;
    u_int16_t		res1;
} __packed;

struct mlx_eventlog_entry	/* MLX_CMD_LOGOP/MLX_LOGOP_GET */
{
    u_int8_t		el_type;
    u_int8_t		el_length;
    u_char		el_target:5;
    u_char		el_channel:3;
    u_char		el_lun:6;
    u_char		res1:2;
    u_int16_t		el_seqno;
    u_char		el_errorcode:7;
    u_char		el_valid:1;
    u_int8_t		el_segment;
    u_char		el_sensekey:4;
    u_char		res2:1;
    u_char		el_ILI:1;
    u_char		el_EOM:1;
    u_char		el_filemark:1;
    u_int8_t		el_information[4];
    u_int8_t		el_addsense;
    u_int8_t		el_csi[4];
    u_int8_t		el_asc;
    u_int8_t		el_asq;
    u_int8_t		res3[12];
} __packed;

#define MLX_LOGOP_GET		0x00	/* operation codes for MLX_CMD_LOGOP */
#define MLX_LOGMSG_SENSE	0x00	/* log message contents codes */

struct mlx_rebuild_stat	/* MLX_CMD_REBUILDSTAT */
{
    u_int32_t	rb_drive;
    u_int32_t	rb_size;
    u_int32_t	rb_remaining;
} __packed;

struct mlx_config2
{
    u_int16_t	cf_flags1;
#define MLX_CF2_ACTV_NEG	(1<<1)
#define MLX_CF2_NORSTRTRY	(1<<7)
#define MLX_CF2_STRGWRK		(1<<8)
#define MLX_CF2_HPSUPP		(1<<9)
#define MLX_CF2_NODISCN		(1<<10)
#define MLX_CF2_ARM    		(1<<13)
#define MLX_CF2_OFM		(1<<15)
#define MLX_CF2_AEMI (MLX_CF2_ARM | MLX_CF2_OFM)
    u_int8_t	cf_oemid;
    u_int8_t	cf_oem_model;
    u_int8_t	cf_physical_sector;
    u_int8_t	cf_logical_sector;
    u_int8_t	cf_blockfactor;
    u_int8_t	cf_flags2;
#define MLX_CF2_READAH		(1<<0)
#define MLX_CF2_BIOSDLY		(1<<1)
#define MLX_CF2_REASS1S		(1<<4)
#define MLX_CF2_FUAENABL	(1<<6)
#define MLX_CF2_R5ALLS		(1<<7)
    u_int8_t	cf_rcrate;
    u_int8_t	cf_res1;
    u_int8_t	cf_blocks_per_cache_line;
    u_int8_t	cf_blocks_per_stripe;
    u_int8_t	cf_scsi_param_0;
    u_int8_t	cf_scsi_param_1;
    u_int8_t	cf_scsi_param_2;
    u_int8_t	cf_scsi_param_3;
    u_int8_t	cf_scsi_param_4;
    u_int8_t	cf_scsi_param_5;
    u_int8_t	cf_scsi_initiator_id;    
    u_int8_t	cf_res2;
    u_int8_t	cf_startup_mode;
    u_int8_t	cf_simultaneous_spinup_devices;
    u_int8_t	cf_delay_between_spinups;
    u_int8_t	cf_res3;
    u_int16_t	cf_checksum;
} __packed;

struct mlx_sys_drv_span
{
    u_int32_t	sp_start_lba;
    u_int32_t	sp_nblks;
    u_int8_t	sp_arm[8];
} __packed;

struct mlx_sys_drv
{
    u_int8_t	sd_status;
    u_int8_t	sd_ext_status;
    u_int8_t	sd_mod1;
    u_int8_t	sd_mod2;
    u_int8_t	sd_raidlevel;
#define MLX_SYS_DRV_WRITEBACK	(1<<7)
#define MLX_SYS_DRV_RAID0	0
#define MLX_SYS_DRV_RAID1	1
#define MLX_SYS_DRV_RAID3	3
#define MLX_SYS_DRV_RAID5	5
#define MLX_SYS_DRV_RAID6	6
#define MLX_SYS_DRV_JBOD	7
    u_int8_t	sd_valid_arms;
    u_int8_t	sd_valid_spans;
    u_int8_t	sd_init_state;
#define MLX_SYS_DRV_INITTED	0x81;
    struct mlx_sys_drv_span sd_span[4];
} __packed;

struct mlx_phys_drv
{
    u_int8_t	pd_flags1;
#define	MLX_PHYS_DRV_PRESENT	(1<<0)
    u_int8_t	pd_flags2;
#define MLX_PHYS_DRV_OTHER	0x00
#define MLX_PHYS_DRV_DISK	0x01
#define MLX_PHYS_DRV_SEQUENTIAL	0x02
#define MLX_PHYS_DRV_CDROM	0x03
#define MLX_PHYS_DRV_FAST20	(1<<3)
#define MLX_PHYS_DRV_SYNC	(1<<4)
#define MLX_PHYS_DRV_FAST	(1<<5)
#define MLX_PHYS_DRV_WIDE	(1<<6)
#define MLX_PHYS_DRV_TAG	(1<<7)
    u_int8_t	pd_status;
#define MLX_PHYS_DRV_DEAD	0x00
#define MLX_PHYS_DRV_WRONLY	0x02
#define MLX_PHYS_DRV_ONLINE	0x03
#define MLX_PHYS_DRV_STANDBY	0x10
    u_int8_t	pd_res1;
    u_int8_t	pd_period;
    u_int8_t	pd_offset;
    u_int32_t	pd_config_size;
} __packed;

struct mlx_core_cfg
{
    u_int8_t	cc_num_sys_drives;
    u_int8_t	cc_res1[3];
    struct mlx_sys_drv	cc_sys_drives[32];
    struct mlx_phys_drv cc_phys_drives[5 * 16];
} __packed;

struct mlx_dcdb
{
    u_int8_t	dcdb_target:4;
    u_int8_t	dcdb_channel:4;
    u_int8_t	dcdb_flags;
#define MLX_DCDB_NO_DATA	0x00
#define MLX_DCDB_DATA_IN	0x01
#define MLX_DCDB_DATA_OUT	0x02
#define MLX_DCDB_EARLY_STATUS		(1<<2)
#define MLX_DCDB_TIMEOUT_10S	0x10
#define MLX_DCDB_TIMEOUT_60S	0x20
#define MLX_DCDB_TIMEOUT_20M	0x30
#define MLX_DCDB_TIMEOUT_24H	0x40
#define MLX_DCDB_NO_AUTO_SENSE	(1<<6)
#define MLX_DCDB_DISCONNECT	(1<<7)
    u_int16_t	dcdb_datasize;
    u_int32_t	dcdb_physaddr;
    u_int8_t	dcdb_cdb_length:4;
    u_int8_t	dcdb_datasize_high:4;
    u_int8_t	dcdb_sense_length;
    u_int8_t	dcdb_cdb[12];
    u_int8_t	dcdb_sense[64];
    u_int8_t	dcdb_status;
    u_int8_t	res1;
} __packed;

struct mlx_bbtable_entry 
{
    u_int32_t	bbt_block_number;
    u_int8_t	bbt_extent;
    u_int8_t	res1;
    u_int8_t	bbt_entry_type;
    u_int8_t	bbt_system_drive:5;
    u_int8_t	res2:3;
} __packed;

#ifdef _KERNEL
/*
 * Inlines to build various command structures
 */
static __inline void
mlx_make_type1(struct mlx_command *mc,
	       u_int8_t code, 
	       u_int16_t f1,
	       u_int32_t f2,
	       u_int8_t f3,
	       u_int32_t f4,
	       u_int8_t f5) 
{
    mc->mc_mailbox[0x0] = code;
    mc->mc_mailbox[0x2] = f1 & 0xff;
    mc->mc_mailbox[0x3] = (((f2 >> 24) & 0x3) << 6) | ((f1 >> 8) & 0x3f);
    mc->mc_mailbox[0x4] = f2 & 0xff;
    mc->mc_mailbox[0x5] = (f2 >> 8) & 0xff;
    mc->mc_mailbox[0x6] = (f2 >> 16) & 0xff;
    mc->mc_mailbox[0x7] = f3;
    mc->mc_mailbox[0x8] = f4 & 0xff;
    mc->mc_mailbox[0x9] = (f4 >> 8) & 0xff;
    mc->mc_mailbox[0xa] = (f4 >> 16) & 0xff;
    mc->mc_mailbox[0xb] = (f4 >> 24) & 0xff;
    mc->mc_mailbox[0xc] = f5;
}

static __inline void
mlx_make_type2(struct mlx_command *mc,
	       u_int8_t code, 
	       u_int8_t f1,
	       u_int8_t f2,
	       u_int8_t f3,
	       u_int8_t f4,
	       u_int8_t f5,
	       u_int8_t f6,
	       u_int32_t f7,
	       u_int8_t f8)
{
    mc->mc_mailbox[0x0] = code;
    mc->mc_mailbox[0x2] = f1;
    mc->mc_mailbox[0x3] = f2;
    mc->mc_mailbox[0x4] = f3;
    mc->mc_mailbox[0x5] = f4;
    mc->mc_mailbox[0x6] = f5;
    mc->mc_mailbox[0x7] = f6;
    mc->mc_mailbox[0x8] = f7 & 0xff;
    mc->mc_mailbox[0x9] = (f7 >> 8) & 0xff;
    mc->mc_mailbox[0xa] = (f7 >> 16) & 0xff;
    mc->mc_mailbox[0xb] = (f7 >> 24) & 0xff;
    mc->mc_mailbox[0xc] = f8;
}

static __inline void
mlx_make_type3(struct mlx_command *mc,
	       u_int8_t code, 
	       u_int8_t f1,
	       u_int8_t f2,
	       u_int16_t f3,
	       u_int8_t f4,
	       u_int8_t f5,
	       u_int32_t f6,
	       u_int8_t f7)
{
    mc->mc_mailbox[0x0] = code;
    mc->mc_mailbox[0x2] = f1;
    mc->mc_mailbox[0x3] = f2;
    mc->mc_mailbox[0x4] = f3 & 0xff;
    mc->mc_mailbox[0x5] = (f3 >> 8) & 0xff;
    mc->mc_mailbox[0x6] = f4;
    mc->mc_mailbox[0x7] = f5;
    mc->mc_mailbox[0x8] = f6 & 0xff;
    mc->mc_mailbox[0x9] = (f6 >> 8) & 0xff;
    mc->mc_mailbox[0xa] = (f6 >> 16) & 0xff;
    mc->mc_mailbox[0xb] = (f6 >> 24) & 0xff;
    mc->mc_mailbox[0xc] = f7;
}

static __inline void
mlx_make_type4(struct mlx_command *mc,
	       u_int8_t code, 
	       u_int16_t f1,
	       u_int32_t f2,
	       u_int32_t f3,
	       u_int8_t f4)
{
    mc->mc_mailbox[0x0] = code;
    mc->mc_mailbox[0x2] = f1 & 0xff;
    mc->mc_mailbox[0x3] = (f1 >> 8) & 0xff;
    mc->mc_mailbox[0x4] = f2 & 0xff;
    mc->mc_mailbox[0x5] = (f2 >> 8) & 0xff;
    mc->mc_mailbox[0x6] = (f2 >> 16) & 0xff;
    mc->mc_mailbox[0x7] = (f2 >> 24) & 0xff;
    mc->mc_mailbox[0x8] = f3 & 0xff;
    mc->mc_mailbox[0x9] = (f3 >> 8) & 0xff;
    mc->mc_mailbox[0xa] = (f3 >> 16) & 0xff;
    mc->mc_mailbox[0xb] = (f3 >> 24) & 0xff;
    mc->mc_mailbox[0xc] = f4;
}

static __inline void
mlx_make_type5(struct mlx_command *mc,
	       u_int8_t code, 
	       u_int8_t f1,
	       u_int8_t f2,
	       u_int32_t f3,
	       u_int32_t f4,
	       u_int8_t f5)
{
    mc->mc_mailbox[0x0] = code;
    mc->mc_mailbox[0x2] = f1;
    mc->mc_mailbox[0x3] = f2;
    mc->mc_mailbox[0x4] = f3 & 0xff;
    mc->mc_mailbox[0x5] = (f3 >> 8) & 0xff;
    mc->mc_mailbox[0x6] = (f3 >> 16) & 0xff;
    mc->mc_mailbox[0x7] = (f3 >> 24) & 0xff;
    mc->mc_mailbox[0x8] = f4 & 0xff;
    mc->mc_mailbox[0x9] = (f4 >> 8) & 0xff;
    mc->mc_mailbox[0xa] = (f4 >> 16) & 0xff;
    mc->mc_mailbox[0xb] = (f4 >> 24) & 0xff;
    mc->mc_mailbox[0xc] = f5;
}

#endif /* _KERNEL */
