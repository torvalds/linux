/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * I2O user space accessible structures/APIs
 *
 * (c) Copyright 1999, 2000 Red Hat Software
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *************************************************************************
 *
 * This header file defines the I2O APIs that are available to both
 * the kernel and user level applications.  Kernel specific structures
 * are defined in i2o_osm. OSMs should include _only_ i2o_osm.h which
 * automatically includes this file.
 *
 */

#ifndef _I2O_DEV_H
#define _I2O_DEV_H

/* How many controllers are we allowing */
#define MAX_I2O_CONTROLLERS	32

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * I2O Control IOCTLs and structures
 */
#define I2O_MAGIC_NUMBER	'i'
#define I2OGETIOPS		_IOR(I2O_MAGIC_NUMBER,0,__u8[MAX_I2O_CONTROLLERS])
#define I2OHRTGET		_IOWR(I2O_MAGIC_NUMBER,1,struct i2o_cmd_hrtlct)
#define I2OLCTGET		_IOWR(I2O_MAGIC_NUMBER,2,struct i2o_cmd_hrtlct)
#define I2OPARMSET		_IOWR(I2O_MAGIC_NUMBER,3,struct i2o_cmd_psetget)
#define I2OPARMGET		_IOWR(I2O_MAGIC_NUMBER,4,struct i2o_cmd_psetget)
#define I2OSWDL 		_IOWR(I2O_MAGIC_NUMBER,5,struct i2o_sw_xfer)
#define I2OSWUL 		_IOWR(I2O_MAGIC_NUMBER,6,struct i2o_sw_xfer)
#define I2OSWDEL		_IOWR(I2O_MAGIC_NUMBER,7,struct i2o_sw_xfer)
#define I2OVALIDATE		_IOR(I2O_MAGIC_NUMBER,8,__u32)
#define I2OHTML 		_IOWR(I2O_MAGIC_NUMBER,9,struct i2o_html)
#define I2OEVTREG		_IOW(I2O_MAGIC_NUMBER,10,struct i2o_evt_id)
#define I2OEVTGET		_IOR(I2O_MAGIC_NUMBER,11,struct i2o_evt_info)
#define I2OPASSTHRU		_IOR(I2O_MAGIC_NUMBER,12,struct i2o_cmd_passthru)
#define I2OPASSTHRU32		_IOR(I2O_MAGIC_NUMBER,12,struct i2o_cmd_passthru32)

struct i2o_cmd_passthru32 {
	unsigned int iop;	/* IOP unit number */
	__u32 msg;		/* message */
};

struct i2o_cmd_passthru {
	unsigned int iop;	/* IOP unit number */
	void __user *msg;	/* message */
};

struct i2o_cmd_hrtlct {
	unsigned int iop;	/* IOP unit number */
	void __user *resbuf;	/* Buffer for result */
	unsigned int __user *reslen;	/* Buffer length in bytes */
};

struct i2o_cmd_psetget {
	unsigned int iop;	/* IOP unit number */
	unsigned int tid;	/* Target device TID */
	void __user *opbuf;	/* Operation List buffer */
	unsigned int oplen;	/* Operation List buffer length in bytes */
	void __user *resbuf;	/* Result List buffer */
	unsigned int __user *reslen;	/* Result List buffer length in bytes */
};

struct i2o_sw_xfer {
	unsigned int iop;	/* IOP unit number */
	unsigned char flags;	/* Flags field */
	unsigned char sw_type;	/* Software type */
	unsigned int sw_id;	/* Software ID */
	void __user *buf;	/* Pointer to software buffer */
	unsigned int __user *swlen;	/* Length of software data */
	unsigned int __user *maxfrag;	/* Maximum fragment count */
	unsigned int __user *curfrag;	/* Current fragment count */
};

struct i2o_html {
	unsigned int iop;	/* IOP unit number */
	unsigned int tid;	/* Target device ID */
	unsigned int page;	/* HTML page */
	void __user *resbuf;	/* Buffer for reply HTML page */
	unsigned int __user *reslen;	/* Length in bytes of reply buffer */
	void __user *qbuf;	/* Pointer to HTTP query string */
	unsigned int qlen;	/* Length in bytes of query string buffer */
};

#define I2O_EVT_Q_LEN 32

struct i2o_evt_id {
	unsigned int iop;
	unsigned int tid;
	unsigned int evt_mask;
};

/* Event data size = frame size - message header + evt indicator */
#define I2O_EVT_DATA_SIZE 88

struct i2o_evt_info {
	struct i2o_evt_id id;
	unsigned char evt_data[I2O_EVT_DATA_SIZE];
	unsigned int data_size;
};

struct i2o_evt_get {
	struct i2o_evt_info info;
	int pending;
	int lost;
};

typedef struct i2o_sg_io_hdr {
	unsigned int flags;	/* see I2O_DPT_SG_IO_FLAGS */
} i2o_sg_io_hdr_t;

/**************************************************************************
 * HRT related constants and structures
 **************************************************************************/
#define I2O_BUS_LOCAL	0
#define I2O_BUS_ISA	1
#define I2O_BUS_EISA	2
/* was  I2O_BUS_MCA	3 */
#define I2O_BUS_PCI	4
#define I2O_BUS_PCMCIA	5
#define I2O_BUS_NUBUS	6
#define I2O_BUS_CARDBUS 7
#define I2O_BUS_UNKNOWN 0x80

typedef struct _i2o_pci_bus {
	__u8 PciFunctionNumber;
	__u8 PciDeviceNumber;
	__u8 PciBusNumber;
	__u8 reserved;
	__u16 PciVendorID;
	__u16 PciDeviceID;
} i2o_pci_bus;

typedef struct _i2o_local_bus {
	__u16 LbBaseIOPort;
	__u16 reserved;
	__u32 LbBaseMemoryAddress;
} i2o_local_bus;

typedef struct _i2o_isa_bus {
	__u16 IsaBaseIOPort;
	__u8 CSN;
	__u8 reserved;
	__u32 IsaBaseMemoryAddress;
} i2o_isa_bus;

typedef struct _i2o_eisa_bus_info {
	__u16 EisaBaseIOPort;
	__u8 reserved;
	__u8 EisaSlotNumber;
	__u32 EisaBaseMemoryAddress;
} i2o_eisa_bus;

typedef struct _i2o_mca_bus {
	__u16 McaBaseIOPort;
	__u8 reserved;
	__u8 McaSlotNumber;
	__u32 McaBaseMemoryAddress;
} i2o_mca_bus;

typedef struct _i2o_other_bus {
	__u16 BaseIOPort;
	__u16 reserved;
	__u32 BaseMemoryAddress;
} i2o_other_bus;

typedef struct _i2o_hrt_entry {
	__u32 adapter_id;
	__u32 parent_tid:12;
	__u32 state:4;
	__u32 bus_num:8;
	__u32 bus_type:8;
	union {
		i2o_pci_bus pci_bus;
		i2o_local_bus local_bus;
		i2o_isa_bus isa_bus;
		i2o_eisa_bus eisa_bus;
		i2o_mca_bus mca_bus;
		i2o_other_bus other_bus;
	} bus;
} i2o_hrt_entry;

typedef struct _i2o_hrt {
	__u16 num_entries;
	__u8 entry_len;
	__u8 hrt_version;
	__u32 change_ind;
	i2o_hrt_entry hrt_entry[1];
} i2o_hrt;

typedef struct _i2o_lct_entry {
	__u32 entry_size:16;
	__u32 tid:12;
	__u32 reserved:4;
	__u32 change_ind;
	__u32 device_flags;
	__u32 class_id:12;
	__u32 version:4;
	__u32 vendor_id:16;
	__u32 sub_class;
	__u32 user_tid:12;
	__u32 parent_tid:12;
	__u32 bios_info:8;
	__u8 identity_tag[8];
	__u32 event_capabilities;
} i2o_lct_entry;

typedef struct _i2o_lct {
	__u32 table_size:16;
	__u32 boot_tid:12;
	__u32 lct_ver:4;
	__u32 iop_flags;
	__u32 change_ind;
	i2o_lct_entry lct_entry[1];
} i2o_lct;

typedef struct _i2o_status_block {
	__u16 org_id;
	__u16 reserved;
	__u16 iop_id:12;
	__u16 reserved1:4;
	__u16 host_unit_id;
	__u16 segment_number:12;
	__u16 i2o_version:4;
	__u8 iop_state;
	__u8 msg_type;
	__u16 inbound_frame_size;
	__u8 init_code;
	__u8 reserved2;
	__u32 max_inbound_frames;
	__u32 cur_inbound_frames;
	__u32 max_outbound_frames;
	char product_id[24];
	__u32 expected_lct_size;
	__u32 iop_capabilities;
	__u32 desired_mem_size;
	__u32 current_mem_size;
	__u32 current_mem_base;
	__u32 desired_io_size;
	__u32 current_io_size;
	__u32 current_io_base;
	__u32 reserved3:24;
	__u32 cmd_status:8;
} i2o_status_block;

/* Event indicator mask flags */
#define I2O_EVT_IND_STATE_CHANGE		0x80000000
#define I2O_EVT_IND_GENERAL_WARNING		0x40000000
#define I2O_EVT_IND_CONFIGURATION_FLAG		0x20000000
#define I2O_EVT_IND_LOCK_RELEASE		0x10000000
#define I2O_EVT_IND_CAPABILITY_CHANGE		0x08000000
#define I2O_EVT_IND_DEVICE_RESET		0x04000000
#define I2O_EVT_IND_EVT_MASK_MODIFIED		0x02000000
#define I2O_EVT_IND_FIELD_MODIFIED		0x01000000
#define I2O_EVT_IND_VENDOR_EVT			0x00800000
#define I2O_EVT_IND_DEVICE_STATE		0x00400000

/* Executive event indicitors */
#define I2O_EVT_IND_EXEC_RESOURCE_LIMITS	0x00000001
#define I2O_EVT_IND_EXEC_CONNECTION_FAIL	0x00000002
#define I2O_EVT_IND_EXEC_ADAPTER_FAULT		0x00000004
#define I2O_EVT_IND_EXEC_POWER_FAIL		0x00000008
#define I2O_EVT_IND_EXEC_RESET_PENDING		0x00000010
#define I2O_EVT_IND_EXEC_RESET_IMMINENT 	0x00000020
#define I2O_EVT_IND_EXEC_HW_FAIL		0x00000040
#define I2O_EVT_IND_EXEC_XCT_CHANGE		0x00000080
#define I2O_EVT_IND_EXEC_NEW_LCT_ENTRY		0x00000100
#define I2O_EVT_IND_EXEC_MODIFIED_LCT		0x00000200
#define I2O_EVT_IND_EXEC_DDM_AVAILABILITY	0x00000400

/* Random Block Storage Event Indicators */
#define I2O_EVT_IND_BSA_VOLUME_LOAD		0x00000001
#define I2O_EVT_IND_BSA_VOLUME_UNLOAD		0x00000002
#define I2O_EVT_IND_BSA_VOLUME_UNLOAD_REQ	0x00000004
#define I2O_EVT_IND_BSA_CAPACITY_CHANGE 	0x00000008
#define I2O_EVT_IND_BSA_SCSI_SMART		0x00000010

/* Event data for generic events */
#define I2O_EVT_STATE_CHANGE_NORMAL		0x00
#define I2O_EVT_STATE_CHANGE_SUSPENDED		0x01
#define I2O_EVT_STATE_CHANGE_RESTART		0x02
#define I2O_EVT_STATE_CHANGE_NA_RECOVER 	0x03
#define I2O_EVT_STATE_CHANGE_NA_NO_RECOVER	0x04
#define I2O_EVT_STATE_CHANGE_QUIESCE_REQUEST	0x05
#define I2O_EVT_STATE_CHANGE_FAILED		0x10
#define I2O_EVT_STATE_CHANGE_FAULTED		0x11

#define I2O_EVT_GEN_WARNING_NORMAL		0x00
#define I2O_EVT_GEN_WARNING_ERROR_THRESHOLD	0x01
#define I2O_EVT_GEN_WARNING_MEDIA_FAULT 	0x02

#define I2O_EVT_CAPABILITY_OTHER		0x01
#define I2O_EVT_CAPABILITY_CHANGED		0x02

#define I2O_EVT_SENSOR_STATE_CHANGED		0x01

/*
 *	I2O classes / subclasses
 */

/*  Class ID and Code Assignments
 *  (LCT.ClassID.Version field)
 */
#define I2O_CLASS_VERSION_10			0x00
#define I2O_CLASS_VERSION_11			0x01

/*  Class code names
 *  (from v1.5 Table 6-1 Class Code Assignments.)
 */

#define I2O_CLASS_EXECUTIVE			0x000
#define I2O_CLASS_DDM				0x001
#define I2O_CLASS_RANDOM_BLOCK_STORAGE		0x010
#define I2O_CLASS_SEQUENTIAL_STORAGE		0x011
#define I2O_CLASS_LAN				0x020
#define I2O_CLASS_WAN				0x030
#define I2O_CLASS_FIBRE_CHANNEL_PORT		0x040
#define I2O_CLASS_FIBRE_CHANNEL_PERIPHERAL	0x041
#define I2O_CLASS_SCSI_PERIPHERAL		0x051
#define I2O_CLASS_ATE_PORT			0x060
#define I2O_CLASS_ATE_PERIPHERAL		0x061
#define I2O_CLASS_FLOPPY_CONTROLLER		0x070
#define I2O_CLASS_FLOPPY_DEVICE 		0x071
#define I2O_CLASS_BUS_ADAPTER			0x080
#define I2O_CLASS_PEER_TRANSPORT_AGENT		0x090
#define I2O_CLASS_PEER_TRANSPORT		0x091
#define	I2O_CLASS_END				0xfff

/*
 *  Rest of 0x092 - 0x09f reserved for peer-to-peer classes
 */

#define I2O_CLASS_MATCH_ANYCLASS		0xffffffff

/*
 *  Subclasses
 */

#define I2O_SUBCLASS_i960			0x001
#define I2O_SUBCLASS_HDM			0x020
#define I2O_SUBCLASS_ISM			0x021

/* Operation functions */

#define I2O_PARAMS_FIELD_GET			0x0001
#define I2O_PARAMS_LIST_GET			0x0002
#define I2O_PARAMS_MORE_GET			0x0003
#define I2O_PARAMS_SIZE_GET			0x0004
#define I2O_PARAMS_TABLE_GET			0x0005
#define I2O_PARAMS_FIELD_SET			0x0006
#define I2O_PARAMS_LIST_SET			0x0007
#define I2O_PARAMS_ROW_ADD			0x0008
#define I2O_PARAMS_ROW_DELETE			0x0009
#define I2O_PARAMS_TABLE_CLEAR			0x000A

/*
 * I2O serial number conventions / formats
 * (circa v1.5)
 */

#define I2O_SNFORMAT_UNKNOWN			0
#define I2O_SNFORMAT_BINARY			1
#define I2O_SNFORMAT_ASCII			2
#define I2O_SNFORMAT_UNICODE			3
#define I2O_SNFORMAT_LAN48_MAC			4
#define I2O_SNFORMAT_WAN			5

/*
 * Plus new in v2.0 (Yellowstone pdf doc)
 */

#define I2O_SNFORMAT_LAN64_MAC			6
#define I2O_SNFORMAT_DDM			7
#define I2O_SNFORMAT_IEEE_REG64 		8
#define I2O_SNFORMAT_IEEE_REG128		9
#define I2O_SNFORMAT_UNKNOWN2			0xff

/*
 *	I2O Get Status State values
 */

#define ADAPTER_STATE_INITIALIZING		0x01
#define ADAPTER_STATE_RESET			0x02
#define ADAPTER_STATE_HOLD			0x04
#define ADAPTER_STATE_READY			0x05
#define ADAPTER_STATE_OPERATIONAL		0x08
#define ADAPTER_STATE_FAILED			0x10
#define ADAPTER_STATE_FAULTED			0x11

/*
 *	Software module types
 */
#define I2O_SOFTWARE_MODULE_IRTOS		0x11
#define I2O_SOFTWARE_MODULE_IOP_PRIVATE		0x22
#define I2O_SOFTWARE_MODULE_IOP_CONFIG		0x23

/*
 *	Vendors
 */
#define I2O_VENDOR_DPT				0x001b

/*
 * DPT / Adaptec specific values for i2o_sg_io_hdr flags.
 */
#define I2O_DPT_SG_FLAG_INTERPRET		0x00010000
#define I2O_DPT_SG_FLAG_PHYSICAL		0x00020000

#define I2O_DPT_FLASH_FRAG_SIZE			0x10000
#define I2O_DPT_FLASH_READ			0x0101
#define I2O_DPT_FLASH_WRITE			0x0102

#endif				/* _I2O_DEV_H */
