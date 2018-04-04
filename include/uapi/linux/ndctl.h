/*
 * Copyright (c) 2014-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 */
#ifndef __NDCTL_H__
#define __NDCTL_H__

#include <linux/types.h>

struct nd_cmd_dimm_flags {
	__u32 status;
	__u32 flags;
} __packed;

struct nd_cmd_get_config_size {
	__u32 status;
	__u32 config_size;
	__u32 max_xfer;
} __packed;

struct nd_cmd_get_config_data_hdr {
	__u32 in_offset;
	__u32 in_length;
	__u32 status;
	__u8 out_buf[0];
} __packed;

struct nd_cmd_set_config_hdr {
	__u32 in_offset;
	__u32 in_length;
	__u8 in_buf[0];
} __packed;

struct nd_cmd_vendor_hdr {
	__u32 opcode;
	__u32 in_length;
	__u8 in_buf[0];
} __packed;

struct nd_cmd_vendor_tail {
	__u32 status;
	__u32 out_length;
	__u8 out_buf[0];
} __packed;

struct nd_cmd_ars_cap {
	__u64 address;
	__u64 length;
	__u32 status;
	__u32 max_ars_out;
	__u32 clear_err_unit;
	__u16 flags;
	__u16 reserved;
} __packed;

struct nd_cmd_ars_start {
	__u64 address;
	__u64 length;
	__u16 type;
	__u8 flags;
	__u8 reserved[5];
	__u32 status;
	__u32 scrub_time;
} __packed;

struct nd_cmd_ars_status {
	__u32 status;
	__u32 out_length;
	__u64 address;
	__u64 length;
	__u64 restart_address;
	__u64 restart_length;
	__u16 type;
	__u16 flags;
	__u32 num_records;
	struct nd_ars_record {
		__u32 handle;
		__u32 reserved;
		__u64 err_address;
		__u64 length;
	} __packed records[0];
} __packed;

struct nd_cmd_clear_error {
	__u64 address;
	__u64 length;
	__u32 status;
	__u8 reserved[4];
	__u64 cleared;
} __packed;

enum {
	ND_CMD_IMPLEMENTED = 0,

	/* bus commands */
	ND_CMD_ARS_CAP = 1,
	ND_CMD_ARS_START = 2,
	ND_CMD_ARS_STATUS = 3,
	ND_CMD_CLEAR_ERROR = 4,

	/* per-dimm commands */
	ND_CMD_SMART = 1,
	ND_CMD_SMART_THRESHOLD = 2,
	ND_CMD_DIMM_FLAGS = 3,
	ND_CMD_GET_CONFIG_SIZE = 4,
	ND_CMD_GET_CONFIG_DATA = 5,
	ND_CMD_SET_CONFIG_DATA = 6,
	ND_CMD_VENDOR_EFFECT_LOG_SIZE = 7,
	ND_CMD_VENDOR_EFFECT_LOG = 8,
	ND_CMD_VENDOR = 9,
	ND_CMD_CALL = 10,
};

enum {
	ND_ARS_VOLATILE = 1,
	ND_ARS_PERSISTENT = 2,
	ND_ARS_RETURN_PREV_DATA = 1 << 1,
	ND_CONFIG_LOCKED = 1,
};

static inline const char *nvdimm_bus_cmd_name(unsigned cmd)
{
	static const char * const names[] = {
		[ND_CMD_ARS_CAP] = "ars_cap",
		[ND_CMD_ARS_START] = "ars_start",
		[ND_CMD_ARS_STATUS] = "ars_status",
		[ND_CMD_CLEAR_ERROR] = "clear_error",
		[ND_CMD_CALL] = "cmd_call",
	};

	if (cmd < ARRAY_SIZE(names) && names[cmd])
		return names[cmd];
	return "unknown";
}

static inline const char *nvdimm_cmd_name(unsigned cmd)
{
	static const char * const names[] = {
		[ND_CMD_SMART] = "smart",
		[ND_CMD_SMART_THRESHOLD] = "smart_thresh",
		[ND_CMD_DIMM_FLAGS] = "flags",
		[ND_CMD_GET_CONFIG_SIZE] = "get_size",
		[ND_CMD_GET_CONFIG_DATA] = "get_data",
		[ND_CMD_SET_CONFIG_DATA] = "set_data",
		[ND_CMD_VENDOR_EFFECT_LOG_SIZE] = "effect_size",
		[ND_CMD_VENDOR_EFFECT_LOG] = "effect_log",
		[ND_CMD_VENDOR] = "vendor",
		[ND_CMD_CALL] = "cmd_call",
	};

	if (cmd < ARRAY_SIZE(names) && names[cmd])
		return names[cmd];
	return "unknown";
}

#define ND_IOCTL 'N'

#define ND_IOCTL_DIMM_FLAGS		_IOWR(ND_IOCTL, ND_CMD_DIMM_FLAGS,\
					struct nd_cmd_dimm_flags)

#define ND_IOCTL_GET_CONFIG_SIZE	_IOWR(ND_IOCTL, ND_CMD_GET_CONFIG_SIZE,\
					struct nd_cmd_get_config_size)

#define ND_IOCTL_GET_CONFIG_DATA	_IOWR(ND_IOCTL, ND_CMD_GET_CONFIG_DATA,\
					struct nd_cmd_get_config_data_hdr)

#define ND_IOCTL_SET_CONFIG_DATA	_IOWR(ND_IOCTL, ND_CMD_SET_CONFIG_DATA,\
					struct nd_cmd_set_config_hdr)

#define ND_IOCTL_VENDOR			_IOWR(ND_IOCTL, ND_CMD_VENDOR,\
					struct nd_cmd_vendor_hdr)

#define ND_IOCTL_ARS_CAP		_IOWR(ND_IOCTL, ND_CMD_ARS_CAP,\
					struct nd_cmd_ars_cap)

#define ND_IOCTL_ARS_START		_IOWR(ND_IOCTL, ND_CMD_ARS_START,\
					struct nd_cmd_ars_start)

#define ND_IOCTL_ARS_STATUS		_IOWR(ND_IOCTL, ND_CMD_ARS_STATUS,\
					struct nd_cmd_ars_status)

#define ND_IOCTL_CLEAR_ERROR		_IOWR(ND_IOCTL, ND_CMD_CLEAR_ERROR,\
					struct nd_cmd_clear_error)

#define ND_DEVICE_DIMM 1            /* nd_dimm: container for "config data" */
#define ND_DEVICE_REGION_PMEM 2     /* nd_region: (parent of PMEM namespaces) */
#define ND_DEVICE_REGION_BLK 3      /* nd_region: (parent of BLK namespaces) */
#define ND_DEVICE_NAMESPACE_IO 4    /* legacy persistent memory */
#define ND_DEVICE_NAMESPACE_PMEM 5  /* PMEM namespace (may alias with BLK) */
#define ND_DEVICE_NAMESPACE_BLK 6   /* BLK namespace (may alias with PMEM) */
#define ND_DEVICE_DAX_PMEM 7        /* Device DAX interface to pmem */

enum nd_driver_flags {
	ND_DRIVER_DIMM            = 1 << ND_DEVICE_DIMM,
	ND_DRIVER_REGION_PMEM     = 1 << ND_DEVICE_REGION_PMEM,
	ND_DRIVER_REGION_BLK      = 1 << ND_DEVICE_REGION_BLK,
	ND_DRIVER_NAMESPACE_IO    = 1 << ND_DEVICE_NAMESPACE_IO,
	ND_DRIVER_NAMESPACE_PMEM  = 1 << ND_DEVICE_NAMESPACE_PMEM,
	ND_DRIVER_NAMESPACE_BLK   = 1 << ND_DEVICE_NAMESPACE_BLK,
	ND_DRIVER_DAX_PMEM	  = 1 << ND_DEVICE_DAX_PMEM,
};

enum {
	ND_MIN_NAMESPACE_SIZE = PAGE_SIZE,
};

enum ars_masks {
	ARS_STATUS_MASK = 0x0000FFFF,
	ARS_EXT_STATUS_SHIFT = 16,
};

/*
 * struct nd_cmd_pkg
 *
 * is a wrapper to a quasi pass thru interface for invoking firmware
 * associated with nvdimms.
 *
 * INPUT PARAMETERS
 *
 * nd_family corresponds to the firmware (e.g. DSM) interface.
 *
 * nd_command are the function index advertised by the firmware.
 *
 * nd_size_in is the size of the input parameters being passed to firmware
 *
 * OUTPUT PARAMETERS
 *
 * nd_fw_size is the size of the data firmware wants to return for
 * the call.  If nd_fw_size is greater than size of nd_size_out, only
 * the first nd_size_out bytes are returned.
 */

struct nd_cmd_pkg {
	__u64   nd_family;		/* family of commands */
	__u64   nd_command;
	__u32   nd_size_in;		/* INPUT: size of input args */
	__u32   nd_size_out;		/* INPUT: size of payload */
	__u32   nd_reserved2[9];	/* reserved must be zero */
	__u32   nd_fw_size;		/* OUTPUT: size fw wants to return */
	unsigned char nd_payload[];	/* Contents of call      */
};

/* These NVDIMM families represent pre-standardization command sets */
#define NVDIMM_FAMILY_INTEL 0
#define NVDIMM_FAMILY_HPE1 1
#define NVDIMM_FAMILY_HPE2 2
#define NVDIMM_FAMILY_MSFT 3

#define ND_IOCTL_CALL			_IOWR(ND_IOCTL, ND_CMD_CALL,\
					struct nd_cmd_pkg)

#endif /* __NDCTL_H__ */
