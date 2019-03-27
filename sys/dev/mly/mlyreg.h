/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
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

/*
 * Section numbers in this document refer to the Mylex "Firmware Software Interface"
 * document ('FSI'), revision 0.11 04/11/00 unless otherwise qualified.
 *
 * Reference is made to the Mylex "Programming Guide for 6.x Controllers" document
 * ('PG6'), document #771242 revision 0.02, 04/11/00
 *
 * Note that fields marked N/A are not supported by the PCI controllers, but are
 * defined here to hold place in datastructures that are shared with the SCSI
 * controllers.  Items not relevant to PCI controllers are not described here.
 *
 * Ordering of items in this file is a little odd due to the constraints of
 * nested declarations.
 */

/*
 * 2.1 (Scatter Gather List Format)
 */
struct mly_sg_entry {
    u_int64_t	physaddr;
    u_int64_t	length;
} __packed;

/*
 * 5.2 System Device Access
 *
 * This is corroborated by the layout of the MDACIOCTL_GETCONTROLLERINFO data
 * in 21.8
 */
#define MLY_MAX_CHANNELS	6
#define MLY_MAX_TARGETS		16
#define MLY_MAX_LUNS		1

/*
 * 8.1 Different Device States
 */
#define MLY_DEVICE_STATE_OFFLINE	0x08	/* DEAD/OFFLINE */
#define MLY_DEVICE_STATE_UNCONFIGURED	0x00
#define MLY_DEVICE_STATE_ONLINE		0x01
#define MLY_DEVICE_STATE_CRITICAL	0x09
#define MLY_DEVICE_STATE_WRITEONLY	0x03
#define MLY_DEVICE_STATE_STANDBY	0x21
#define MLY_DEVICE_STATE_MISSING	0x04	/* or-ed with (ONLINE or WRITEONLY or STANDBY) */

/*
 * 8.2 Device Type Field definitions
 */
#define MLY_DEVICE_TYPE_RAID0		0x0	/* RAID 0 */
#define MLY_DEVICE_TYPE_RAID1		0x1	/* RAID 1 */
#define MLY_DEVICE_TYPE_RAID3		0x3	/* RAID 3 right asymmetric parity */
#define MLY_DEVICE_TYPE_RAID5		0x5	/* RAID 5 right asymmetric parity */
#define MLY_DEVICE_TYPE_RAID6		0x6	/* RAID 6 (Mylex RAID 6) */
#define MLY_DEVICE_TYPE_RAID7		0x7	/* RAID 7 (JBOD) */
#define MLY_DEVICE_TYPE_NEWSPAN		0x8	/* New Mylex SPAN */
#define MLY_DEVICE_TYPE_RAID3F		0x9	/* RAID 3 fixed parity */
#define MLY_DEVICE_TYPE_RAID3L		0xb	/* RAID 3 left symmetric parity */
#define MLY_DEVICE_TYPE_SPAN		0xc	/* current spanning implementation */
#define MLY_DEVICE_TYPE_RAID5L		0xd	/* RAID 5 left symmetric parity */
#define MLY_DEVICE_TYPE_RAIDE		0xe	/* RAID E (concatenation) */
#define MLY_DEVICE_TYPE_PHYSICAL	0xf	/* physical device */

/*
 * 8.3 Stripe Size
 */
#define MLY_STRIPE_ZERO		0x0	/* no stripe (RAID 1, RAID 7, etc) */
#define MLY_STRIPE_512b		0x1
#define MLY_STRIPE_1k		0x2
#define MLY_STRIPE_2k		0x3
#define MLY_STRIPE_4k		0x4
#define MLY_STRIPE_8k		0x5
#define MLY_STRIPE_16k		0x6
#define MLY_STRIPE_32k		0x7
#define MLY_STRIPE_64k		0x8
#define MLY_STRIPE_128k		0x9
#define MLY_STRIPE_256k		0xa
#define MLY_STRIPE_512k		0xb
#define MLY_STRIPE_1m		0xc

/*
 * 8.4 Cacheline Size
 */
#define MLY_CACHELINE_ZERO	0x0	/* caching cannot be enabled */
#define MLY_CACHELINE_512b	0x1
#define MLY_CACHELINE_1k	0x2
#define MLY_CACHELINE_2k	0x3
#define MLY_CACHELINE_4k	0x4
#define MLY_CACHELINE_8k	0x5
#define MLY_CACHELINE_16k	0x6
#define MLY_CACHELINE_32k	0x7
#define MLY_CACHELINE_64k	0x8

/*
 * 8.5 Read/Write control
 */
#define MLY_RWCtl_INITTED	(1<<7)	/* if set, the logical device is initialised */
			/* write control */
#define MLY_RWCtl_WCD		(0)	/* write cache disabled */
#define MLY_RWCtl_WDISABLE	(1<<3)	/* writing disabled */
#define MLY_RWCtl_WCE		(2<<3)	/* write cache enabled */
#define MLY_RWCtl_IWCE		(3<<3)	/* intelligent write cache enabled */
			/* read control */
#define MLY_RWCtl_RCD		(0)	/* read cache is disabled */
#define MLY_RWCtl_RCE		(1)	/* read cache enabled */
#define MLY_RWCtl_RAHEAD	(2)	/* readahead enabled */
#define MLY_RWCtl_IRAHEAD	(3)	/* intelligent readahead enabled */

/*
 * 9.0 LUN Map Format
 */
struct mly_lun_map {
    u_int8_t	res1:4;
    u_int8_t	host_port_mapped:1;	/* this system drive visible to host on this controller/port combination */
    u_int8_t	tid_valid:1;		/* target ID valid */
    u_int8_t	hid_valid:1;		/* host ID valid */
    u_int8_t	lun_valid:1;		/* LUN valid */
    u_int8_t	res2;
    u_int8_t	lun;			/* LUN */
    u_int8_t	tid;			/* TID */
    u_int8_t	hid[32];		/* HID (one bit for each host) */
} __packed;

/*
 * 10.1 Controller Parameters
 */
struct mly_param_controller {
    u_int8_t	rdahen:1;					/* N/A */
    u_int8_t	bilodly:1;					/* N/A */
    u_int8_t   	fua_disable:1;
    u_int8_t	reass1s:1;					/* N/A */
    u_int8_t	truvrfy:1;					/* N/A */
    u_int8_t	dwtvrfy:1;					/* N/A */
    u_int8_t	background_initialisation:1;
    u_int8_t	clustering:1;					/* N/A */

    u_int8_t	bios_disable:1;
    u_int8_t   	boot_from_cdrom:1;
    u_int8_t	drive_coercion:1;
    u_int8_t	write_same_disable:1;
    u_int8_t	hba_mode:1;					/* N/A */
    u_int8_t	bios_geometry:2;
#define MLY_BIOSGEOM_2G	0x0
#define MLY_BIOSGEOM_8G	0x1
    u_int8_t	res1:1;						/* N/A */

    u_int8_t	res2[2];					/* N/A */

    u_int8_t	v_dec:1;
    u_int8_t	safte:1;					/* N/A */
    u_int8_t	ses:1;						/* N/A */
    u_int8_t	res3:2;						/* N/A */
    u_int8_t	v_arm:1;
    u_int8_t	v_ofm:1;
    u_int8_t	res4:1;						/* N/A */

    u_int8_t	rebuild_check_rate;
    u_int8_t	cache_line_size;	/* see 8.4 */
    u_int8_t	oem_code;
#define MLY_OEM_MYLEX	0x00
#define MLY_OEM_IBM	0x08
#define MLY_OEM_HP	0x0a
#define MLY_OEM_DEC	0x0c
#define MLY_OEM_SIEMENS	0x10
#define MLY_OEM_INTEL	0x12
    u_int8_t	spinup_mode;
#define MLY_SPIN_AUTO		0
#define MLY_SPIN_PWRSPIN	1
#define MLY_SPIN_WSSUSPIN	2
    u_int8_t	spinup_devices;
    u_int8_t	spinup_interval;
    u_int8_t	spinup_wait_time;

    u_int8_t	res5:3;						/* N/A */
    u_int8_t	vutursns:1;					/* N/A */
    u_int8_t	dccfil:1;					/* N/A */
    u_int8_t	nopause:1;					/* N/A */
    u_int8_t	disqfull:1;					/* N/A */
    u_int8_t	disbusy:1;					/* N/A */

    u_int8_t	res6:2;						/* N/A */
    u_int8_t	failover_node_name;				/* N/A */
    u_int8_t	res7:1;						/* N/A */
    u_int8_t	ftopo:3;					/* N/A */
    u_int8_t	disable_ups:1;					/* N/A */

    u_int8_t	res8:1;						/* N/A */
    u_int8_t	propagate_reset:1;				/* N/A */
    u_int8_t	nonstd_mp_reset:1;				/* N/A */
    u_int8_t	res9:5;						/* N/A */

    u_int8_t	res10;						/* N/A */
    u_int8_t	serial_port_baud_rate;				/* N/A */
    u_int8_t	serial_port_control;				/* N/A */
    u_int8_t	change_stripe_ok_developer_flag_only;		/* N/A */

    u_int8_t	small_large_host_transfers:2;			/* N/A */
    u_int8_t	frame_control:2;				/* N/A */
    u_int8_t	pci_latency_control:2;				/* N/A */
    u_int8_t	treat_lip_as_reset:1;				/* N/A */
    u_int8_t	res11:1;					/* N/A */

    u_int8_t	ms_autorest:1;					/* N/A */
    u_int8_t	res12:7;					/* N/A */

    u_int8_t	ms_aa_fsim:1;					/* N/A */
    u_int8_t	ms_aa_ccach:1;					/* N/A */
    u_int8_t	ms_aa_fault_signals:1;				/* N/A */
    u_int8_t	ms_aa_c4_faults:1;				/* N/A */
    u_int8_t	ms_aa_host_reset_delay_mask:4;			/* N/A */

    u_int8_t	ms_flg_simplex_no_rstcom:1;			/* N/A */
    u_int8_t	res13:7;					/* N/A */

    u_int8_t	res14;						/* N/A */
    u_int8_t	hardloopid[2][2];				/* N/A */
    u_int8_t	ctrlname[2][16+1];				/* N/A */
    u_int8_t	initiator_id;
    u_int8_t	startup_option;
#define MLY_STARTUP_IF_NO_CHANGE	0x0
#define MLY_STARTUP_IF_NO_LUN_CHANGE	0x1
#define MLY_STARTUP_IF_NO_LUN_OFFLINE	0x2
#define MLY_STARTUP_IF_LUN0_NO_CHANGE	0x3
#define MLY_STARTUP_IF_LUN0_NOT_OFFLINE	0x4
#define MLY_STARTUP_ALWAYS		0x5

    u_int8_t	res15[62];
} __packed;

/*
 * 10.2 Physical Device Parameters
 */
struct mly_param_physical_device {
    u_int16_t	tags;
    u_int16_t	speed;
    u_int8_t	width;
    u_int8_t	combing:1;
    u_int8_t	res1:7;
    u_int8_t	res2[3];
} __packed;

/*
 * 10.3 Logical Device Parameters
 */
struct mly_param_logical_device {
    u_int8_t	type;			/* see 8.2 */
    u_int8_t	state;			/* see 8.1 */
    u_int16_t	raid_device;
    u_int8_t	res1;
    u_int8_t	bios_geometry;		/* BIOS control word? */
    u_int8_t	stripe_size;		/* see 8.3 */
    u_int8_t	read_write_control;	/* see 8.5 */
    u_int8_t	res2[8];
} __packed;

/*
 * 12.3 Health Status Buffer
 *
 * Pad to 128 bytes.
 */
struct mly_health_status {
    u_int32_t	uptime_us;				/* N/A */
    u_int32_t	uptime_ms;				/* N/A */
    u_int32_t	realtime;				/* N/A */
    u_int32_t	res1;					/* N/A */
    u_int32_t	change_counter;
    u_int32_t	res2;					/* N/A */
    u_int32_t	debug_message_index;			/* N/A */
    u_int32_t	bios_message_index;			/* N/A */
    u_int32_t	trace_page;				/* N/A */
    u_int32_t	profiler_page;				/* N/A */
    u_int32_t	next_event;
    u_int8_t	res3[4 + 16 + 64];			/* N/A */
} __packed;

/*
 * 14.2 Timeout Bit Format
 */
struct mly_timeout {
    u_int8_t	value:6;
    u_int8_t	scale:2;
#define MLY_TIMEOUT_SECONDS	0x0
#define MLY_TIMEOUT_MINUTES	0x1
#define MLY_TIMEOUT_HOURS	0x2
} __packed;

/*
 * 14.3 Operation Device
 */
#define MLY_OPDEVICE_PHYSICAL_DEVICE		0x0
#define MLY_OPDEVICE_RAID_DEVICE		0x1
#define MLY_OPDEVICE_PHYSICAL_CHANNEL		0x2
#define MLY_OPDEVICE_RAID_CHANNEL		0x3
#define MLY_OPDEVICE_PHYSICAL_CONTROLLER	0x4
#define MLY_OPDEVICE_RAID_CONTROLLER		0x5
#define MLY_OPDEVICE_CONFIGURATION_GROUP	0x10

/*
 * 14.4 Status Bit Format
 *
 * AKA Status Mailbox Format
 *
 * XXX format conflict between FSI and PG6 over the ordering of the
 * status and sense length fields.
 */
struct mly_status {
    u_int16_t	command_id;
    u_int8_t	status;
    u_int8_t	sense_length;
    int32_t	residue;
} __packed;

/*
 * 14.5 Command Control Bit (CCB) format
 *
 * This byte is unfortunately named.
 */
struct mly_command_control {
    u_int8_t	force_unit_access:1;
    u_int8_t	disable_page_out:1;
    u_int8_t	res1:1;
    u_int8_t	extended_sg_table:1;
    u_int8_t	data_direction:1;
#define MLY_CCB_WRITE	1
#define MLY_CCB_READ	0
    u_int8_t	res2:1;
    u_int8_t	no_auto_sense:1;
    u_int8_t	disable_disconnect:1;
} __packed;

/*
 * 15.0 Commands
 *
 * We use the command names as given by Mylex
 */
#define MDACMD_MEMCOPY		0x1	/* memory to memory copy */
#define MDACMD_SCSIPT		0x2	/* SCSI passthrough (small command) */
#define MDACMD_SCSILCPT		0x3	/* SCSI passthrough (large command) */
#define MDACMD_SCSI		0x4	/* SCSI command for logical/phyiscal device (small command) */
#define MDACMD_SCSILC		0x5	/* SCSI command for logical/phyiscal device (large command) */
#define MDACMD_IOCTL		0x20	/* Management command */
#define MDACMD_IOCTLCHECK	0x23	/* Validate management command (not implemented) */

/*
 * 16.0 IOCTL command
 *
 * We use the IOCTL names as given by Mylex
 * Note that only ioctls supported by the PCI controller family are listed
 */
#define MDACIOCTL_GETCONTROLLERINFO		0x1
#define MDACIOCTL_GETLOGDEVINFOVALID		0x3
#define MDACIOCTL_GETPHYSDEVINFOVALID		0x5
#define MDACIOCTL_GETCONTROLLERSTATISTICS	0xb
#define MDACIOCTL_GETLOGDEVSTATISTICS		0xd
#define MDACIOCTL_GETPHYSDEVSTATISTICS		0xf
#define MDACIOCTL_GETHEALTHSTATUS		0x11
#define MDACIOCTL_GETEVENT			0x15
/* flash update */
#define MDACIOCTL_STOREIMAGE			0x2c
#define MDACIOCTL_READIMAGE			0x2d
#define MDACIOCTL_FLASHIMAGES			0x2e
/* battery backup unit */
#define MDACIOCTL_GET_SUBSYSTEM_DATA		0x70
#define MDACIOCTL_SET_SUBSYSTEM_DATA		0x71
/* non-data commands */
#define MDACIOCTL_STARTDISOCVERY		0x81
#define MDACIOCTL_SETRAIDDEVSTATE		0x82
#define MDACIOCTL_INITPHYSDEVSTART		0x84
#define MDACIOCTL_INITPHYSDEVSTOP		0x85
#define MDACIOCTL_INITRAIDDEVSTART		0x86
#define MDACIOCTL_INITRAIDDEVSTOP		0x87
#define MDACIOCTL_REBUILDRAIDDEVSTART		0x88
#define MDACIOCTL_REBUILDRAIDDEVSTOP		0x89
#define MDACIOCTL_MAKECONSISTENTDATASTART	0x8a
#define MDACIOCTL_MAKECONSISTENTDATASTOP	0x8b
#define MDACIOCTL_CONSISTENCYCHECKSTART		0x8c
#define MDACIOCTL_CONSISTENCYCHECKSTOP		0x8d
#define MDACIOCTL_SETMEMORYMAILBOX		0x8e
#define MDACIOCTL_RESETDEVICE			0x90
#define MDACIOCTL_FLUSHDEVICEDATA		0x91
#define MDACIOCTL_PAUSEDEVICE			0x92
#define MDACIOCTL_UNPAUSEDEVICE			0x93
#define MDACIOCTL_LOCATEDEVICE			0x94
#define MDACIOCTL_SETMASTERSLAVEMODE		0x95
#define MDACIOCTL_SETREALTIMECLOCK		0xac
/* RAID configuration */
#define MDACIOCTL_CREATENEWCONF			0xc0
#define MDACIOCTL_DELETERAIDDEV			0xc1
#define MDACIOCTL_REPLACEINTERNALDEV		0xc2
#define MDACIOCTL_RENAMERAIDDEV			0xc3
#define MDACIOCTL_ADDNEWCONF			0xc4
#define MDACIOCTL_XLATEPHYSDEVTORAIDDEV		0xc5
#define MDACIOCTL_MORE				0xc6
#define MDACIOCTL_SETPHYSDEVPARAMETER		0xc8
#define MDACIOCTL_GETPHYSDEVPARAMETER		0xc9
#define MDACIOCTL_CLEARCONF			0xca
#define MDACIOCTL_GETDEVCONFINFO		0xcb
#define MDACIOCTL_GETGROUPCONFINFO		0xcc
#define MDACIOCTL_GETFREESPACELIST		0xcd
#define MDACIOCTL_GETLOGDEVPARAMETER		0xce
#define MDACIOCTL_SETLOGDEVPARAMETER		0xcf
#define MDACIOCTL_GETCONTROLLERPARAMETER	0xd0
#define MDACIOCTL_SETCONTRLLERPARAMETER		0xd1
#define MDACIOCTL_CLEARCONFSUSPMODE		0xd2
#define MDACIOCTL_GETBDT_FOR_SYSDRIVE		0xe0

/*
 * 17.1.4 Data Transfer Memory Address Without SG List
 */
struct mly_short_transfer {
    struct mly_sg_entry	sg[2];
} __packed;

/*
 * 17.1.5 Data Transfer Memory Address With SG List
 *
 * Note that only the first s/g table is currently used.
 */
struct mly_sg_transfer {
    u_int16_t	entries[3];
    u_int16_t	res1;
    u_int64_t	table_physaddr[3];
} __packed;

/*
 * 17.1.3 Data Transfer Memory Address Format
 */
union mly_command_transfer {
    struct mly_short_transfer	direct;
    struct mly_sg_transfer	indirect;
};

/*
 * 21.1  MDACIOCTL_SETREALTIMECLOCK
 * 21.7  MDACIOCTL_GETHEALTHSTATUS
 * 21.8  MDACIOCTL_GETCONTROLLERINFO
 * 21.9  MDACIOCTL_GETLOGDEVINFOVALID
 * 21.10 MDACIOCTL_GETPHYSDEVINFOVALID
 * 21.11 MDACIOCTL_GETPHYSDEVSTATISTICS
 * 21.12 MDACIOCTL_GETLOGDEVSTATISTICS
 * 21.13 MDACIOCTL_GETCONTROLLERSTATISTICS
 * 21.27 MDACIOCTL_GETBDT_FOR_SYSDRIVE
 * 23.4  MDACIOCTL_CREATENEWCONF
 * 23.5  MDACIOCTL_ADDNEWCONF
 * 23.8  MDACIOCTL_GETDEVCONFINFO
 * 23.9  MDACIOCTL_GETFREESPACELIST
 * 24.1  MDACIOCTL_MORE
 * 25.1  MDACIOCTL_GETPHYSDEVPARAMETER
 * 25.2  MDACIOCTL_SETPHYSDEVPARAMETER
 * 25.3  MDACIOCTL_GETLOGDEVPARAMETER
 * 25.4  MDACIOCTL_SETLOGDEVPARAMETER
 * 25.5  MDACIOCTL_GETCONTROLLERPARAMETER
 * 25.6  MDACIOCTL_SETCONTROLLERPARAMETER
 *
 * These commands just transfer data
 */
struct mly_ioctl_param_data {
    u_int8_t			param[10];
    union mly_command_transfer	transfer;
} __packed;

/*
 * 21.2 MDACIOCTL_SETMEMORYMAILBOX
 */
struct mly_ioctl_param_setmemorymailbox {
    u_int8_t	health_buffer_size;
    u_int8_t	res1;
    u_int64_t	health_buffer_physaddr;
    u_int64_t	command_mailbox_physaddr;
    u_int64_t	status_mailbox_physaddr;
    u_int64_t	res2[2];
} __packed;

/*
 * 21.8.2 MDACIOCTL_GETCONTROLLERINFO: Data Format
 */
struct mly_ioctl_getcontrollerinfo {
    u_int8_t	res1;						/* N/A */
    u_int8_t	interface_type;
    u_int8_t	controller_type;
    u_int8_t	res2;						/* N/A */
    u_int16_t	interface_speed;
    u_int8_t	interface_width;
    u_int8_t	res3[9];					/* N/A */
    char	interface_name[16];
    char	controller_name[16];
    u_int8_t	res4[16];					/* N/A */
    /* firmware release information */
    u_int8_t	fw_major;
    u_int8_t	fw_minor;
    u_int8_t	fw_turn;
    u_int8_t	fw_build;
    u_int8_t	fw_day;
    u_int8_t	fw_month;
    u_int8_t	fw_century;
    u_int8_t	fw_year;
    /* hardware release information */
    u_int8_t	hw_revision;					/* N/A */
    u_int8_t	res5[3];					/* N/A */
    u_int8_t	hw_release_day;					/* N/A */
    u_int8_t	hw_release_month;				/* N/A */
    u_int8_t	hw_release_century;				/* N/A */
    u_int8_t	hw_release_year;				/* N/A */
    /* hardware manufacturing information */
    u_int8_t	batch_number;					/* N/A */
    u_int8_t	res6;						/* N/A */
    u_int8_t	plant_number;
    u_int8_t	res7;
    u_int8_t	hw_manuf_day;
    u_int8_t	hw_manuf_month;
    u_int8_t	hw_manuf_century;
    u_int8_t	hw_manuf_year;
    u_int8_t	max_pdd_per_xldd;
    u_int8_t	max_ildd_per_xldd;
    u_int16_t	nvram_size;
    u_int8_t	max_number_of_xld;				/* N/A */
    u_int8_t	res8[3];					/* N/A */
    /* unique information per controller */
    char	serial_number[16];
    u_int8_t	res9[16];					/* N/A */
    /* vendor information */
    u_int8_t	res10[3];					/* N/A */
    u_int8_t	oem_information;
    char	vendor_name[16];				/* N/A */
    /* other physical/controller/operation information */
    u_int8_t	bbu_present:1;
    u_int8_t	active_clustering:1;
    u_int8_t	res11:6;					/* N/A */
    u_int8_t	res12[3];					/* N/A */
    /* physical device scan information */
    u_int8_t	physical_scan_active:1;
    u_int8_t	res13:7;					/* N/A */
    u_int8_t	physical_scan_channel;
    u_int8_t	physical_scan_target;
    u_int8_t	physical_scan_lun;
    /* maximum command data transfer size */
    u_int16_t	maximum_block_count;
    u_int16_t	maximum_sg_entries;
    /* logical/physical device counts */
    u_int16_t	logical_devices_present;
    u_int16_t	logical_devices_critical;
    u_int16_t	logical_devices_offline;
    u_int16_t	physical_devices_present;
    u_int16_t	physical_disks_present;
    u_int16_t	physical_disks_critical;			/* N/A */
    u_int16_t	physical_disks_offline;
    u_int16_t	maximum_parallel_commands;
    /* channel and target ID information */
    u_int8_t	physical_channels_present;
    u_int8_t	virtual_channels_present;
    u_int8_t	physical_channels_possible;
    u_int8_t	virtual_channels_possible;
    u_int8_t	maximum_targets_possible[16];			/* N/A (6 and up) */
    u_int8_t	res14[12];					/* N/A */
    /* memory/cache information */
    u_int16_t	memory_size;
    u_int16_t	cache_size;
    u_int32_t	valid_cache_size;				/* N/A */
    u_int32_t	dirty_cache_size;				/* N/A */
    u_int16_t	memory_speed;
    u_int8_t	memory_width;
    u_int8_t	memory_type:5;
    u_int8_t	res15:1;					/* N/A */
    u_int8_t	memory_parity:1;
    u_int8_t	memory_ecc:1;
    char	memory_information[16];				/* N/A */
    /* execution memory information */
    u_int16_t	exmemory_size;
    u_int16_t	l2cache_size;					/* N/A */
    u_int8_t	res16[8];					/* N/A */
    u_int16_t	exmemory_speed;
    u_int8_t	exmemory_width;
    u_int8_t	exmemory_type:5;
    u_int8_t	res17:1;					/* N/A */
    u_int8_t	exmemory_parity:1;
    u_int8_t	exmemory_ecc:1;
    char	exmemory_name[16];				/* N/A */
    /* CPU information */
    struct {
	u_int16_t	speed;
	u_int8_t	type;
	u_int8_t	number;
	u_int8_t	res1[12];				/* N/A */
	char		name[16];				/* N/A */
    } cpu[2] __packed;
    /* debugging/profiling/command time tracing information */
    u_int16_t	profiling_page;					/* N/A */
    u_int16_t	profiling_programs;				/* N/A */
    u_int16_t	time_trace_page;				/* N/A */
    u_int16_t	time_trace_programs;				/* N/A */
    u_int8_t	res18[8];					/* N/A */
    /* error counters on physical devices */
    u_int16_t	physical_device_bus_resets;			/* N/A */
    u_int16_t	physical_device_parity_errors;			/* N/A */
    u_int16_t	physical_device_soft_errors;			/* N/A */
    u_int16_t	physical_device_commands_failed;		/* N/A */
    u_int16_t	physical_device_miscellaneous_errors;		/* N/A */
    u_int16_t	physical_device_command_timeouts;		/* N/A */
    u_int16_t	physical_device_selection_timeouts;		/* N/A */
    u_int16_t	physical_device_retries;			/* N/A */
    u_int16_t	physical_device_aborts;				/* N/A */
    u_int16_t	physical_device_host_command_aborts;		/* N/A */
    u_int16_t	physical_device_PFAs_detected;			/* N/A */
    u_int16_t	physical_device_host_commands_failed;		/* N/A */
    u_int8_t	res19[8];					/* N/A */
    /* error counters on logical devices */
    u_int16_t	logical_device_soft_errors;			/* N/A */
    u_int16_t	logical_device_commands_failed;			/* N/A */
    u_int16_t	logical_device_host_command_aborts;		/* N/A */
    u_int16_t	res20;						/* N/A */
    /* error counters on controller */
    u_int16_t	controller_parity_ecc_errors;
    u_int16_t	controller_host_command_aborts;			/* N/A */
    u_int8_t	res21[4];					/* N/A */
    /* long duration activity information */
    u_int16_t	background_inits_active;
    u_int16_t	logical_inits_active;
    u_int16_t	physical_inits_active;
    u_int16_t	consistency_checks_active;
    u_int16_t	rebuilds_active;
    u_int16_t	MORE_active;
    u_int16_t	patrol_active;					/* N/A */
    u_int8_t	long_operation_status;				/* N/A */
    u_int8_t	res22;						/* N/A */
    /* flash ROM information */
    u_int8_t	flash_type;					/* N/A */
    u_int8_t	res23;						/* N/A */
    u_int16_t	flash_size;
    u_int32_t	flash_maximum_age;
    u_int32_t	flash_age;
    u_int8_t	res24[4];					/* N/A */
    char	flash_name[16];					/* N/A */
    /* firmware runtime information */
    u_int8_t	rebuild_rate;
    u_int8_t	background_init_rate;
    u_int8_t	init_rate;
    u_int8_t	consistency_check_rate;
    u_int8_t	res25[4];					/* N/A */
    u_int32_t	maximum_dp;
    u_int32_t	free_dp;
    u_int32_t	maximum_iop;
    u_int32_t	free_iop;
    u_int16_t	maximum_comb_length;
    u_int16_t	maximum_configuration_groups;
    u_int8_t	installation_abort:1;
    u_int8_t	maintenance:1;
    u_int8_t	res26:6;					/* N/A */
    u_int8_t	res27[3];					/* N/A */
    u_int8_t	res28[32 + 512];				/* N/A */
} __packed;

/*
 * 21.9.2 MDACIOCTL_GETLOGDEVINFOVALID
 */
struct mly_ioctl_getlogdevinfovalid {
    u_int8_t	res1;						/* N/A */
    u_int8_t	channel;
    u_int8_t	target;
    u_int8_t	lun;
    u_int8_t	state;				/* see 8.1 */
    u_int8_t	raid_level;			/* see 8.2 */
    u_int8_t	stripe_size;			/* see 8.3 */
    u_int8_t	cache_line_size;		/* see 8.4 */
    u_int8_t	read_write_control;		/* see 8.5 */
    u_int8_t	consistency_check:1;
    u_int8_t	rebuild:1;
    u_int8_t	make_consistent:1;
    u_int8_t	initialisation:1;
    u_int8_t	migration:1;
    u_int8_t	patrol:1;
    u_int8_t	res2:2;						/* N/A */
    u_int8_t	ar5_limit;
    u_int8_t	ar5_algo;
    u_int16_t	logical_device_number;
    u_int16_t	bios_control;
    /* erorr counters */
    u_int16_t	soft_errors;					/* N/A */
    u_int16_t	commands_failed;				/* N/A */
    u_int16_t	host_command_aborts;				/* N/A */
    u_int16_t	deferred_write_errors;				/* N/A */
    u_int8_t	res3[8];					/* N/A */
    /* device size information */
    u_int8_t	res4[2];					/* N/A */
    u_int16_t	device_block_size;
    u_int32_t	original_device_size;				/* N/A */
    u_int32_t	device_size;			/* XXX "blocks or MB" Huh? */
    u_int8_t	res5[4];					/* N/A */
    char	device_name[32];				/* N/A */
    u_int8_t	inquiry[36];
    u_int8_t	res6[12];					/* N/A */
    u_int64_t	last_read_block;				/* N/A */
    u_int64_t	last_written_block;				/* N/A */
    u_int64_t	consistency_check_block;
    u_int64_t	rebuild_block;
    u_int64_t	make_consistent_block;
    u_int64_t	initialisation_block;
    u_int64_t	migration_block;
    u_int64_t	patrol_block;					/* N/A */
    u_int8_t	res7[64];					/* N/A */
} __packed;

/*
 * 21.10.2 MDACIOCTL_GETPHYSDEVINFOVALID: Data Format
 */
struct mly_ioctl_getphysdevinfovalid {
    u_int8_t	res1;
    u_int8_t	channel;
    u_int8_t	target;
    u_int8_t	lun;
    u_int8_t	raid_ft:1;			/* configuration status */
    u_int8_t	res2:1;						/* N/A */
    u_int8_t	local:1;
    u_int8_t	res3:5;
    u_int8_t	host_dead:1;			/* multiple host/controller status *//* N/A */
    u_int8_t	host_connection_dead:1;				/* N/A */
    u_int8_t	res4:6;						/* N/A */
    u_int8_t	state;				/* see 8.1 */
    u_int8_t	width;
    u_int16_t	speed;
    /* multiported physical device information */
    u_int8_t	ports_available;				/* N/A */
    u_int8_t	ports_inuse;					/* N/A */
    u_int8_t	res5[4];
    u_int8_t	ether_address[16];				/* N/A */
    u_int16_t	command_tags;
    u_int8_t	consistency_check:1;				/* N/A */
    u_int8_t	rebuild:1;					/* N/A */
    u_int8_t	make_consistent:1;				/* N/A */
    u_int8_t	initialisation:1;
    u_int8_t	migration:1;					/* N/A */
    u_int8_t	patrol:1;					/* N/A */
    u_int8_t	res6:2;
    u_int8_t	long_operation_status;				/* N/A */
    u_int8_t	parity_errors;
    u_int8_t	soft_errors;
    u_int8_t	hard_errors;
    u_int8_t	miscellaneous_errors;
    u_int8_t	command_timeouts;				/* N/A */
    u_int8_t	retries;					/* N/A */
    u_int8_t	aborts;						/* N/A */
    u_int8_t	PFAs_detected;					/* N/A */
    u_int8_t	res7[6];
    u_int16_t	block_size;
    u_int32_t	original_device_size;		/* XXX "blocks or MB" Huh? */
    u_int32_t	device_size;			/* XXX "blocks or MB" Huh? */
    u_int8_t	res8[4];
    char	name[16];					/* N/A */
    u_int8_t	res9[16 + 32];
    u_int8_t	inquiry[36];
    u_int8_t	res10[12 + 16];
    u_int64_t	last_read_block;				/* N/A */
    u_int64_t	last_written_block;				/* N/A */
    u_int64_t	consistency_check_block;			/* N/A */
    u_int64_t	rebuild_block;					/* N/A */
    u_int64_t	make_consistent_block;				/* N/A */
    u_int64_t	initialisation_block;				/* N/A */
    u_int64_t	migration_block;				/* N/A */
    u_int64_t	patrol_block;					/* N/A */
    u_int8_t	res11[256];
} __packed;

union mly_devinfo {
    struct mly_ioctl_getlogdevinfovalid		logdev;
    struct mly_ioctl_getphysdevinfovalid	physdev;
};

/*
 * 21.11.2 MDACIOCTL_GETPHYSDEVSTATISTICS: Data Format
 * 21.12.2 MDACIOCTL_GETLOGDEVSTATISTICS: Data Format
 */
struct mly_ioctl_getdevstatistics {
    u_int32_t	uptime_ms;			/* getphysedevstatistics only */
    u_int8_t	res1[5];					/* N/A */
    u_int8_t	channel;
    u_int8_t	target;
    u_int8_t	lun;
    u_int16_t	raid_device;			/* getlogdevstatistics only */
    u_int8_t	res2[2];					/* N/A */
    /* total read/write performance including cache data */
    u_int32_t	total_reads;
    u_int32_t	total_writes;
    u_int32_t	total_read_size;
    u_int32_t	total_write_size;
    /* cache read/write performance */
    u_int32_t	cache_reads;					/* N/A */
    u_int32_t	cache_writes;					/* N/A */
    u_int32_t	cache_read_size;				/* N/A */
    u_int32_t	cache_write_size;				/* N/A */
    /* commands active/wait information */
    u_int32_t	command_waits_done;				/* N/A */
    u_int16_t	active_commands;				/* N/A */
    u_int16_t	waiting_commands;				/* N/A */
    u_int8_t	res3[8];					/* N/A */
} __packed;

/*
 * 21.13.2 MDACIOCTL_GETCONTROLLERSTATISTICS: Data Format
 */
struct mly_ioctl_getcontrollerstatistics {
    u_int32_t	uptime_ms;					/* N/A */
    u_int8_t	res1[12];					/* N/A */
    /* target physical device performance data information */
    u_int32_t	target_physical_device_interrupts;		/* N/A */
    u_int32_t	target_physical_device_stray_interrupts;	/* N/A */
    u_int8_t	res2[8];					/* N/A */
    u_int32_t	target_physical_device_reads;			/* N/A */
    u_int32_t	target_physical_device_writes;			/* N/A */
    u_int32_t	target_physical_device_read_size;		/* N/A */
    u_int32_t	target_physical_device_write_size;		/* N/A */
    /* host system performance data information */
    u_int32_t	host_system_interrupts;				/* N/A */
    u_int32_t	host_system_stray_interrupts;			/* N/A */
    u_int32_t	host_system_sent_interrupts;			/* N/A */
    u_int8_t	res3[4];					/* N/A */
    u_int32_t	physical_device_reads;				/* N/A */
    u_int32_t	physical_device_writes;				/* N/A */
    u_int32_t	physical_device_read_size;			/* N/A */
    u_int32_t	physical_device_write_size;			/* N/A */
    u_int32_t	physical_device_cache_reads;			/* N/A */
    u_int32_t	physical_device_cache_writes;			/* N/A */
    u_int32_t	physical_device_cache_read_size;		/* N/A */
    u_int32_t	physical_device_cache_write_size;		/* N/A */
    u_int32_t	logical_device_reads;				/* N/A */
    u_int32_t	logical_device_writes;				/* N/A */
    u_int32_t	logical_device_read_size;			/* N/A */
    u_int32_t	logical_device_write_size;			/* N/A */
    u_int32_t	logical_device_cache_reads;			/* N/A */
    u_int32_t	logical_device_cache_writes;			/* N/A */
    u_int32_t	logical_device_cache_read_size;			/* N/A */
    u_int32_t	logical_device_cache_write_size;		/* N/A */
    u_int16_t	target_physical_device_commands_active;		/* N/A */
    u_int16_t	target_physical_device_commands_waiting;	/* N/A */
    u_int16_t	host_system_commands_active;			/* N/A */
    u_int16_t	host_system_commands_waiting;			/* N/A */
    u_int8_t	res4[48 + 64];					/* N/A */
} __packed;

/*
 * 21.2 MDACIOCTL_SETRAIDDEVSTATE
 */
struct mly_ioctl_param_setraiddevstate {
    u_int8_t	state;
} __packed;

/*
 * 21.27.2 MDACIOCTL_GETBDT_FOR_SYSDRIVE: Data Format
 */
#define MLY_MAX_BDT_ENTRIES	1022
struct mly_ioctl_getbdt_for_sysdrive {
    u_int32_t	num_of_bdt_entries;
    u_int32_t	bad_data_block_address[MLY_MAX_BDT_ENTRIES];
} __packed;

/*
 * 22.1 Physical Device Definition (PDD)
 */
struct mly_pdd {
    u_int8_t	type;				/* see 8.2 */
    u_int8_t	state;				/* see 8.1 */
    u_int16_t	raid_device;
    u_int32_t	device_size;			/* XXX "block or MB" Huh? */
    u_int8_t	controller;
    u_int8_t	channel;
    u_int8_t	target;
    u_int8_t	lun;
    u_int32_t	start_address;
} __packed;

/*
 * 22.2 RAID Device Use Definition (UDD)
 */
struct mly_udd {
    u_int8_t	res1;
    u_int8_t	state;				/* see 8.1 */
    u_int16_t	raid_device;
    u_int32_t	start_address;
} __packed;

/*
 * RAID Device Definition (LDD)
 */
struct mly_ldd {
    u_int8_t	type;				/* see 8.2 */
    u_int8_t	state;				/* see 8.1 */
    u_int16_t	raid_device;
    u_int32_t	device_size;			/* XXX "block or MB" Huh? */
    u_int8_t	devices_used_count;
    u_int8_t	stripe_size;			/* see 8.3 */
    u_int8_t	cache_line_size;		/* see 8.4 */
    u_int8_t	read_write_control;		/* see 8.5 */
    u_int32_t	devices_used_size;		/* XXX "block or MB" Huh? */
    u_int16_t	devices_used[32];		/* XXX actual size of this field unknown! */
} __packed;

/*
 * Define a datastructure giving the smallest allocation that will hold
 * a PDD, UDD or LDD for MDACIOCTL_GETDEVCONFINFO.
 */
struct mly_devconf_hdr {
    u_int8_t	type;				/* see 8.2 */
    u_int8_t	state;				/* see 8.1 */
    u_int16_t	raid_device;
};

union mly_ioctl_devconfinfo {
    struct mly_pdd		pdd;
    struct mly_udd		udd;
    struct mly_ldd		ldd;
    struct mly_devconf_hdr	hdr;
};

/*
 * 22.3 MDACIOCTL_RENAMERAIDDEV
 *
 * XXX this command is listed as transferring data, but does not define the data.
 */
struct mly_ioctl_param_renameraiddev {
    u_int8_t	new_raid_device;
} __packed;

/*
 * 23.6.2 MDACIOCTL_XLATEPHYSDEVTORAIDDEV
 *
 * XXX documentation suggests this format will change
 */
struct mly_ioctl_param_xlatephysdevtoraiddev {
    u_int16_t	raid_device;
    u_int8_t	res1[2];
    u_int8_t	controller;
    u_int8_t	channel;
    u_int8_t	target;
    u_int8_t	lun;
} __packed;

/*
 * 23.7 MDACIOCTL_GETGROUPCONFINFO
 */
struct mly_ioctl_param_getgroupconfinfo {
    u_int16_t			group;
    u_int8_t			res1[8];
    union mly_command_transfer	transfer;
} __packed;

/*
 * 23.9.2 MDACIOCTL_GETFREESPACELIST: Data Format
 *
 * The controller will populate as much of this structure as is provided,
 * or as is required to fully list the free space available.
 */
struct mly_ioctl_getfreespacelist_entry {
    u_int16_t	raid_device;
    u_int8_t	res1[6];
    u_int32_t	address;		/* XXX "blocks or MB" Huh? */
    u_int32_t	size;			/* XXX "blocks or MB" Huh? */
} __packed;

struct mly_ioctl_getfrespacelist {
    u_int16_t	returned_entries;
    u_int16_t	total_entries;
    u_int8_t	res1[12];
    struct mly_ioctl_getfreespacelist_entry space[0];	/* expand to suit */
} __packed;

/*
 * 27.1 MDACIOCTL_GETSUBSYSTEMDATA
 * 27.2 MDACIOCTL_SETSUBSYSTEMDATA
 *
 * PCI controller only supports a limited subset of the possible operations.
 *
 * XXX where does the status end up? (the command transfers no data)
 */
struct mly_ioctl_param_subsystemdata {
    u_int8_t	operation:4;
#define MLY_BBU_GETSTATUS	0x00
#define MLY_BBU_SET_THRESHOLD	0x00	/* minutes in param[0,1] */
    u_int8_t	subsystem:4;
#define MLY_SUBSYSTEM_BBU	0x01
    u_int	parameter[3];		/* only for SETSUBSYSTEMDATA */
} __packed;

struct mly_ioctl_getsubsystemdata_bbustatus {
    u_int16_t	current_power;
    u_int16_t	maximum_power;
    u_int16_t	power_threshold;
    u_int8_t	charge_level;
    u_int8_t	hardware_version;
    u_int8_t	battery_type;
#define MLY_BBU_TYPE_UNKNOWN	0x00
#define MLY_BBU_TYPE_NICAD	0x01
#define MLY_BBU_TYPE_MISSING	0xfe
    u_int8_t	res1;
    u_int8_t	operation_status;
#define MLY_BBU_STATUS_NO_SYNC		0x01
#define MLY_BBU_STATUS_OUT_OF_SYNC	0x02
#define MLY_BBU_STATUS_FIRST_WARNING	0x04
#define MLY_BBU_STATUS_SECOND_WARNING	0x08
#define MLY_BBU_STATUS_RECONDITIONING	0x10
#define MLY_BBU_STATUS_DISCHARGING	0x20
#define MLY_BBU_STATUS_FASTCHARGING	0x40
    u_int8_t	res2;
} __packed;

/*
 * 28.9  MDACIOCTL_RESETDEVICE
 * 28.10 MDACIOCTL_FLUSHDEVICEDATA
 * 28.11 MDACIOCTL_PAUSEDEVICE
 * 28.12 MDACIOCTL_UNPAUSEDEVICE
 */
struct mly_ioctl_param_deviceoperation {
    u_int8_t	operation_device;		/* see 14.3 */
} __packed;

/*
 * 31.1 Event Data Format
 */
struct mly_event {
    u_int32_t	sequence_number;
    u_int32_t	timestamp;
    u_int32_t	code;
    u_int8_t	controller;
    u_int8_t	channel;
    u_int8_t	target;				/* also enclosure */
    u_int8_t	lun;				/* also enclosure unit */
    u_int8_t   	res1[4];
    u_int32_t	param;
    u_int8_t	sense[40];
} __packed;

/*
 * 31.2 MDACIOCTL_GETEVENT
 */
struct mly_ioctl_param_getevent {
    u_int16_t			sequence_number_low;
    u_int8_t			res1[8];
    union mly_command_transfer	transfer;
} __packed;

union mly_ioctl_param {
    struct mly_ioctl_param_data				data;
    struct mly_ioctl_param_setmemorymailbox		setmemorymailbox;
    struct mly_ioctl_param_setraiddevstate		setraiddevstate;
    struct mly_ioctl_param_renameraiddev		renameraiddev;
    struct mly_ioctl_param_xlatephysdevtoraiddev	xlatephysdevtoraiddev;
    struct mly_ioctl_param_getgroupconfinfo		getgroupconfinfo;
    struct mly_ioctl_param_subsystemdata		subsystemdata;
    struct mly_ioctl_param_deviceoperation		deviceoperation;
    struct mly_ioctl_param_getevent			getevent;
};

/*
 * 19 SCSI Command Format
 */
struct mly_command_address_physical {
    u_int8_t			lun;
    u_int8_t			target;
    u_int8_t			channel:3;
    u_int8_t			controller:5;
} __packed;

struct mly_command_address_logical {
    u_int16_t			logdev;
    u_int8_t			res1:3;
    u_int8_t			controller:5;
} __packed;

union mly_command_address {
    struct mly_command_address_physical	phys;
    struct mly_command_address_logical	log;
};

struct mly_command_generic {
    u_int16_t			command_id;
    u_int8_t			opcode;
    struct mly_command_control	command_control;
    u_int32_t			data_size;
    u_int64_t			sense_buffer_address;
    union mly_command_address	addr;
    struct mly_timeout		timeout;
    u_int8_t			maximum_sense_size;
    u_int8_t			res1[11];
    union mly_command_transfer	transfer;
} __packed;
    

/*
 * 19.1 MDACMD_SCSI & MDACMD_SCSIPT
 */
#define MLY_CMD_SCSI_SMALL_CDB	10
struct mly_command_scsi_small {
    u_int16_t			command_id;
    u_int8_t			opcode;
    struct mly_command_control	command_control;
    u_int32_t			data_size;
    u_int64_t			sense_buffer_address;
    union mly_command_address	addr;
    struct mly_timeout		timeout;
    u_int8_t			maximum_sense_size;
    u_int8_t			cdb_length;
    u_int8_t			cdb[MLY_CMD_SCSI_SMALL_CDB];
    union mly_command_transfer	transfer;
} __packed;
    
/*
 * 19.2 MDACMD_SCSILC & MDACMD_SCSILCPT
 */
struct mly_command_scsi_large {
    u_int16_t			command_id;
    u_int8_t			opcode;
    struct mly_command_control	command_control;
    u_int32_t			data_size;
    u_int64_t			sense_buffer_address;
    union mly_command_address	addr;
    struct mly_timeout		timeout;
    u_int8_t			maximum_sense_size;
    u_int8_t			cdb_length;
    u_int16_t			res1;
    u_int64_t			cdb_physaddr;
    union mly_command_transfer	transfer;
} __packed;
    
/*
 * 20.1 IOCTL Command Format: Internal Bus
 */
struct mly_command_ioctl {
    u_int16_t			command_id;
    u_int8_t			opcode;
    struct mly_command_control	command_control;
    u_int32_t			data_size;
    u_int64_t			sense_buffer_address;
    union mly_command_address	addr;
    struct mly_timeout		timeout;
    u_int8_t			maximum_sense_size;
    u_int8_t			sub_ioctl;
    union mly_ioctl_param	param;
} __packed;

/*
 * PG6: 8.2.2
 */
struct mly_command_mmbox {
    u_int32_t			flag;
    u_int8_t			data[60];
} __packed;

union mly_command_packet {
    struct mly_command_generic		generic;
    struct mly_command_scsi_small	scsi_small;
    struct mly_command_scsi_large	scsi_large;
    struct mly_command_ioctl		ioctl;
    struct mly_command_mmbox		mmbox;
};

/*
 * PG6: 5.3
 */
#define MLY_I960RX_COMMAND_MAILBOX	0x10
#define MLY_I960RX_STATUS_MAILBOX	0x18
#define MLY_I960RX_IDBR			0x20
#define MLY_I960RX_ODBR			0x2c
#define MLY_I960RX_ERROR_STATUS		0x2e
#define MLY_I960RX_INTERRUPT_STATUS	0x30
#define MLY_I960RX_INTERRUPT_MASK	0x34

#define MLY_STRONGARM_COMMAND_MAILBOX	0x50
#define MLY_STRONGARM_STATUS_MAILBOX	0x58
#define MLY_STRONGARM_IDBR		0x60
#define MLY_STRONGARM_ODBR		0x61
#define MLY_STRONGARM_ERROR_STATUS	0x63
#define MLY_STRONGARM_INTERRUPT_STATUS	0x30
#define MLY_STRONGARM_INTERRUPT_MASK	0x34

/*
 * PG6: 5.4.3 Doorbell 0
 */
#define MLY_HM_CMDSENT			(1<<0)
#define MLY_HM_STSACK			(1<<1)
#define MLY_SOFT_RST			(1<<3)
#define MLY_AM_CMDSENT			(1<<4)

/*
 * PG6: 5.4.4 Doorbell 1
 *
 * Note that the documentation claims that these bits are set when the
 * status queue(s) are empty, whereas the Linux driver and experience 
 * suggest they are set when there is status available.
 */
#define MLY_HM_STSREADY			(1<<0)
#define MLY_AM_STSREADY			(1<<1)

/*
 * PG6: 5.4.6 Doorbell 3
 */
#define MLY_MSG_EMPTY			(1<<3)
#define MLY_MSG_SPINUP			0x08
#define MLY_MSG_RACE_RECOVERY_FAIL	0x60
#define MLY_MSG_RACE_IN_PROGRESS	0x70
#define MLY_MSG_RACE_ON_CRITICAL	0xb0
#define MLY_MSG_PARITY_ERROR		0xf0

/*
 * PG6: 5.4.8 Outbound Interrupt Mask
 */
#define MLY_INTERRUPT_MASK_DISABLE	0xff
#define MLY_INTERRUPT_MASK_ENABLE	(0xff & ~(1<<2))

/*
 * PG6: 8.2 Advanced Mailbox Scheme
 *
 * Note that this must be allocated on a 4k boundary, and all internal
 * fields must also reside on a 4k boundary.
 * We could dynamically size this structure, but the extra effort
 * is probably unjustified.  Note that these buffers do not need to be
 * adjacent - we just group them to simplify allocation of the bus-visible
 * buffer.
 *
 * XXX Note that for some reason, if MLY_MMBOX_COMMANDS is > 64, the controller
 * fails to respond to the command at (MLY_MMBOX_COMMANDS - 64).  It's not
 * wrapping to 0 at this point (determined by experimentation).  This is not
 * consistent with the Linux driver's implementation.
 * Whilst it's handy to have lots of room for status returns in case we end up
 * being slow getting back to completed commands, it seems unlikely that we 
 * would get 64 commands ahead of the controller on the submissions side, so
 * the current workaround is to simply limit the command ring to 64 entries.
 */
union mly_status_packet {
     struct mly_status		status;
     struct {
	 u_int32_t		flag;
	 u_int8_t		data[4];
     } __packed mmbox;
};
union mly_health_region {
    struct mly_health_status	status;
    u_int8_t			pad[1024];
};

#define MLY_MMBOX_COMMANDS		64
#define MLY_MMBOX_STATUS		512
struct mly_mmbox {
    union mly_command_packet	mmm_command[MLY_MMBOX_COMMANDS];
    union mly_status_packet	mmm_status[MLY_MMBOX_STATUS];
    union mly_health_region	mmm_health;
} __packed;
