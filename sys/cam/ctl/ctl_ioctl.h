/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Silicon Graphics International Corp.
 * Copyright (c) 2011 Spectra Logic Corporation
 * Copyright (c) 2014-2017 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * $Id: //depot/users/kenm/FreeBSD-test2/sys/cam/ctl/ctl_ioctl.h#4 $
 * $FreeBSD$
 */
/*
 * CAM Target Layer ioctl interface.
 *
 * Author: Ken Merry <ken@FreeBSD.org>
 */

#ifndef	_CTL_IOCTL_H_
#define	_CTL_IOCTL_H_

#ifdef ICL_KERNEL_PROXY
#include <sys/socket.h>
#endif

#include <sys/ioccom.h>
#include <sys/nv.h>

#define	CTL_DEFAULT_DEV		"/dev/cam/ctl"
/*
 * Maximum number of targets we support.
 */
#define	CTL_MAX_TARGETS		1

/*
 * Maximum target ID we support.
 */
#define	CTL_MAX_TARGID		15

/*
 * Maximum number of initiators per port.
 */
#define	CTL_MAX_INIT_PER_PORT	2048

/* Hopefully this won't conflict with new misc devices that pop up */
#define	CTL_MINOR	225

typedef enum {
	CTL_DELAY_TYPE_NONE,
	CTL_DELAY_TYPE_CONT,
	CTL_DELAY_TYPE_ONESHOT
} ctl_delay_type;

typedef enum {
	CTL_DELAY_LOC_NONE,
	CTL_DELAY_LOC_DATAMOVE,
	CTL_DELAY_LOC_DONE,
} ctl_delay_location;

typedef enum {
	CTL_DELAY_STATUS_NONE,
	CTL_DELAY_STATUS_OK,
	CTL_DELAY_STATUS_INVALID_LUN,
	CTL_DELAY_STATUS_INVALID_TYPE,
	CTL_DELAY_STATUS_INVALID_LOC,
	CTL_DELAY_STATUS_NOT_IMPLEMENTED
} ctl_delay_status;

struct ctl_io_delay_info {
	uint32_t		lun_id;
	ctl_delay_type		delay_type;
	ctl_delay_location	delay_loc;
	uint32_t		delay_secs;
	ctl_delay_status	status;
};

typedef enum {
	CTL_STATS_NO_IO,
	CTL_STATS_READ,
	CTL_STATS_WRITE
} ctl_stat_types;
#define	CTL_STATS_NUM_TYPES	3

typedef enum {
	CTL_SS_OK,
	CTL_SS_NEED_MORE_SPACE,
	CTL_SS_ERROR
} ctl_stats_status;

typedef enum {
	CTL_STATS_FLAG_NONE		= 0x00,
	CTL_STATS_FLAG_TIME_VALID	= 0x01
} ctl_stats_flags;

struct ctl_io_stats {
	uint32_t			item;
	uint64_t			bytes[CTL_STATS_NUM_TYPES];
	uint64_t			operations[CTL_STATS_NUM_TYPES];
	uint64_t			dmas[CTL_STATS_NUM_TYPES];
	struct bintime			time[CTL_STATS_NUM_TYPES];
	struct bintime			dma_time[CTL_STATS_NUM_TYPES];
};

struct ctl_get_io_stats {
	struct ctl_io_stats	*stats;		/* passed to/from kernel */
	size_t			alloc_len;	/* passed to kernel */
	size_t			fill_len;	/* passed to userland */
	int			first_item;	/* passed to kernel */
	int			num_items;	/* passed to userland */
	ctl_stats_status	status;		/* passed to userland */
	ctl_stats_flags		flags;		/* passed to userland */
	struct timespec		timestamp;	/* passed to userland */
};

/*
 * The types of errors that can be injected:
 *
 * NONE:	No error specified.
 * ABORTED:	SSD_KEY_ABORTED_COMMAND, 0x45, 0x00
 * MEDIUM_ERR:	Medium error, different asc/ascq depending on read/write.
 * UA:		Unit attention.
 * CUSTOM:	User specifies the sense data.
 * TYPE:	Mask to use with error types.
 *
 * Flags that affect injection behavior:
 * CONTINUOUS:	This error will stay around until explicitly cleared.
 * DESCRIPTOR:	Use descriptor sense instead of fixed sense.
 */
typedef enum {
	CTL_LUN_INJ_NONE		= 0x000,
	CTL_LUN_INJ_ABORTED		= 0x001,
	CTL_LUN_INJ_MEDIUM_ERR		= 0x002,
	CTL_LUN_INJ_UA			= 0x003,
	CTL_LUN_INJ_CUSTOM		= 0x004,
	CTL_LUN_INJ_TYPE		= 0x0ff,
	CTL_LUN_INJ_CONTINUOUS		= 0x100,
	CTL_LUN_INJ_DESCRIPTOR		= 0x200
} ctl_lun_error;

/*
 * Flags to specify what type of command the given error pattern will
 * execute on.  The first group of types can be ORed together.
 *
 * READ:	Any read command.
 * WRITE:	Any write command.
 * READWRITE:	Any read or write command.
 * READCAP:	Any read capacity command.
 * TUR:		Test Unit Ready.
 * ANY:		Any command.
 * MASK:	Mask for basic command patterns.
 *
 * Special types:
 *
 * CMD:		The CDB to act on is specified in struct ctl_error_desc_cmd.
 * RANGE:	For read/write commands, act when the LBA is in the
 *		specified range.
 */
typedef enum {
	CTL_LUN_PAT_NONE	= 0x000,
	CTL_LUN_PAT_READ	= 0x001,
	CTL_LUN_PAT_WRITE	= 0x002,
	CTL_LUN_PAT_READWRITE	= CTL_LUN_PAT_READ | CTL_LUN_PAT_WRITE,
	CTL_LUN_PAT_READCAP	= 0x004,
	CTL_LUN_PAT_TUR		= 0x008,
	CTL_LUN_PAT_ANY		= 0x0ff,
	CTL_LUN_PAT_MASK	= 0x0ff,
	CTL_LUN_PAT_CMD		= 0x100,
	CTL_LUN_PAT_RANGE	= 0x200
} ctl_lun_error_pattern;

/*
 * This structure allows the user to specify a particular CDB pattern to
 * look for.
 *
 * cdb_pattern:		Fill in the relevant bytes to look for in the CDB.
 * cdb_valid_bytes:	Bitmask specifying valid bytes in the cdb_pattern.
 * flags:		Specify any command flags (see ctl_io_flags) that
 *			should be set.
 */
struct ctl_error_desc_cmd {
	uint8_t		cdb_pattern[CTL_MAX_CDBLEN];
	uint32_t	cdb_valid_bytes;
	uint32_t	flags;
};

/*
 * Error injection descriptor.
 *
 * lun_id	   LUN to act on.
 * lun_error:	   The type of error to inject.  See above for descriptions.
 * error_pattern:  What kind of command to act on.  See above.
 * cmd_desc:	   For CTL_LUN_PAT_CMD only.
 * lba_range:	   For CTL_LUN_PAT_RANGE only.
 * custom_sense:   Specify sense.  For CTL_LUN_INJ_CUSTOM only.
 * serial:	   Serial number returned by the kernel.  Use for deletion.
 * links:	   Kernel use only.
 */
struct ctl_error_desc {
	uint32_t			lun_id;		/* To kernel */
	ctl_lun_error			lun_error;	/* To kernel */
	ctl_lun_error_pattern		error_pattern;	/* To kernel */
	struct ctl_error_desc_cmd	cmd_desc;	/* To kernel */
	struct ctl_lba_len		lba_range;	/* To kernel */
	struct scsi_sense_data		custom_sense;	/* To kernel */
	uint64_t			serial;		/* From kernel */
	STAILQ_ENTRY(ctl_error_desc)	links;		/* Kernel use only */
};

typedef enum {
	CTL_OOA_FLAG_NONE	= 0x00,
	CTL_OOA_FLAG_ALL_LUNS	= 0x01
} ctl_ooa_flags;

typedef enum {
	CTL_OOA_OK,
	CTL_OOA_NEED_MORE_SPACE,
	CTL_OOA_ERROR
} ctl_get_ooa_status;

typedef enum {
	CTL_OOACMD_FLAG_NONE		= 0x00,
	CTL_OOACMD_FLAG_DMA		= 0x01,
	CTL_OOACMD_FLAG_BLOCKED		= 0x02,
	CTL_OOACMD_FLAG_ABORT		= 0x04,
	CTL_OOACMD_FLAG_RTR		= 0x08,
	CTL_OOACMD_FLAG_DMA_QUEUED	= 0x10
} ctl_ooa_cmd_flags;

struct ctl_ooa_entry {
	ctl_ooa_cmd_flags	cmd_flags;
	uint8_t			cdb[CTL_MAX_CDBLEN];
	uint8_t			cdb_len;
	uint32_t		tag_num;
	uint32_t		lun_num;
	struct bintime		start_bt;
};

struct ctl_ooa {
	ctl_ooa_flags		flags;		/* passed to kernel */
	uint64_t		lun_num;	/* passed to kernel */
	uint32_t		alloc_len;	/* passed to kernel */
	uint32_t		alloc_num;	/* passed to kernel */
	struct ctl_ooa_entry	*entries;	/* filled in kernel */
	uint32_t		fill_len;	/* passed to userland */
	uint32_t		fill_num;	/* passed to userland */
	uint32_t		dropped_num;	/* passed to userland */
	struct bintime		cur_bt;		/* passed to userland */
	ctl_get_ooa_status	status;		/* passed to userland */
};

typedef enum {
	CTL_LUN_NOSTATUS,
	CTL_LUN_OK,
	CTL_LUN_ERROR,
	CTL_LUN_WARNING
} ctl_lun_status;

#define	CTL_ERROR_STR_LEN	160

typedef enum {
	CTL_LUNREQ_CREATE,
	CTL_LUNREQ_RM,
	CTL_LUNREQ_MODIFY,
} ctl_lunreq_type;

/*
 * The ID_REQ flag is used to say that the caller has requested a
 * particular LUN ID in the req_lun_id field.  If we cannot allocate that
 * LUN ID, the ctl_add_lun() call will fail.
 *
 * The STOPPED flag tells us that the LUN should default to the powered
 * off state.  It will return 0x04,0x02 until it is powered up.  ("Logical
 * unit not ready, initializing command required.")
 *
 * The NO_MEDIA flag tells us that the LUN has no media inserted.
 *
 * The PRIMARY flag tells us that this LUN is registered as a Primary LUN
 * which is accessible via the Master shelf controller in an HA. This flag
 * being set indicates a Primary LUN. This flag being reset represents a
 * Secondary LUN controlled by the Secondary controller in an HA
 * configuration. Flag is applicable at this time to T_DIRECT types. 
 *
 * The SERIAL_NUM flag tells us that the serial_num field is filled in and
 * valid for use in SCSI INQUIRY VPD page 0x80.
 *
 * The DEVID flag tells us that the device_id field is filled in and
 * valid for use in SCSI INQUIRY VPD page 0x83.
 *
 * The DEV_TYPE flag tells us that the device_type field is filled in.
 *
 * The EJECTED flag tells us that the removable LUN has tray open.
 *
 * The UNMAP flag tells us that this LUN supports UNMAP.
 *
 * The OFFLINE flag tells us that this LUN can not access backing store.
 */
typedef enum {
	CTL_LUN_FLAG_ID_REQ		= 0x01,
	CTL_LUN_FLAG_STOPPED		= 0x02,
	CTL_LUN_FLAG_NO_MEDIA		= 0x04,
	CTL_LUN_FLAG_PRIMARY		= 0x08,
	CTL_LUN_FLAG_SERIAL_NUM		= 0x10,
	CTL_LUN_FLAG_DEVID		= 0x20,
	CTL_LUN_FLAG_DEV_TYPE		= 0x40,
	CTL_LUN_FLAG_UNMAP		= 0x80,
	CTL_LUN_FLAG_EJECTED		= 0x100,
	CTL_LUN_FLAG_READONLY		= 0x200
} ctl_backend_lun_flags;

/*
 * LUN creation parameters:
 *
 * flags:		Various LUN flags, see above.
 *
 * device_type:		The SCSI device type.  e.g. 0 for Direct Access,
 *			3 for Processor, etc.  Only certain backends may
 *			support setting this field.  The CTL_LUN_FLAG_DEV_TYPE
 *			flag should be set in the flags field if the device
 *			type is set.
 *
 * lun_size_bytes:	The size of the LUN in bytes.  For some backends
 *			this is relevant (e.g. ramdisk), for others, it may
 *			be ignored in favor of using the properties of the
 *			backing store.  If specified, this should be a
 *			multiple of the blocksize.
 *
 *			The actual size of the LUN is returned in this
 *			field.
 *
 * blocksize_bytes:	The LUN blocksize in bytes.  For some backends this
 *			is relevant, for others it may be ignored in
 *			favor of using the properties of the backing store. 
 *
 *			The actual blocksize of the LUN is returned in this
 *			field.
 *
 * req_lun_id:		The requested LUN ID.  The CTL_LUN_FLAG_ID_REQ flag
 *			should be set if this is set.  The request will be
 *			granted if the LUN number is available, otherwise
 * 			the LUN addition request will fail.
 *
 *			The allocated LUN number is returned in this field.
 *
 * serial_num:		This is the value returned in SCSI INQUIRY VPD page
 *			0x80.  If it is specified, the CTL_LUN_FLAG_SERIAL_NUM
 *			flag should be set.
 *
 *			The serial number value used is returned in this
 *			field.
 *
 * device_id:		This is the value returned in the T10 vendor ID
 *			based DESIGNATOR field in the SCSI INQUIRY VPD page
 *			0x83 data.  If it is specified, the CTL_LUN_FLAG_DEVID
 *			flag should be set.
 *
 *			The device id value used is returned in this field.
 */
struct ctl_lun_create_params {
	ctl_backend_lun_flags	flags;
	uint8_t			device_type;
	uint64_t		lun_size_bytes;
	uint32_t		blocksize_bytes;
	uint32_t		req_lun_id;
	uint8_t			serial_num[CTL_SN_LEN];
	uint8_t			device_id[CTL_DEVID_LEN];
};

/*
 * LUN removal parameters:
 *
 * lun_id:		The number of the LUN to delete.  This must be set.
 *			The LUN must be backed by the given backend.
 */
struct ctl_lun_rm_params {
	uint32_t		lun_id;
};

/*
 * LUN modification parameters:
 *
 * lun_id:		The number of the LUN to modify.  This must be set.
 *			The LUN must be backed by the given backend.
 *
 * lun_size_bytes:	The size of the LUN in bytes.  If zero, update
 * 			the size using the backing file size, if possible.
 */
struct ctl_lun_modify_params {
	uint32_t		lun_id;
	uint64_t		lun_size_bytes;
};

/*
 * Union of request type data.  Fill in the appropriate union member for
 * the request type.
 */
union ctl_lunreq_data {
	struct ctl_lun_create_params	create;
	struct ctl_lun_rm_params	rm;
	struct ctl_lun_modify_params	modify;
};

/*
 * LUN request interface:
 *
 * backend:		This is required, and is NUL-terminated a string
 *			that is the name of the backend, like "ramdisk" or
 *			"block".
 *
 * reqtype:		The type of request, CTL_LUNREQ_CREATE to create a
 *			LUN, CTL_LUNREQ_RM to delete a LUN.
 *
 * reqdata:		Request type-specific information.  See the
 *			description of individual the union members above
 *			for more information.
 *
 * num_be_args:		This is the number of backend-specific arguments
 *			in the be_args array.
 *
 * be_args:		This is an array of backend-specific arguments.
 *			See above for a description of the fields in this
 *			structure.
 *
 * status:		Status of the LUN request.
 *
 * error_str:		If the status is CTL_LUN_ERROR, this will
 *			contain a string describing the error.
 *
 * kern_be_args:	For kernel use only.
 */
struct ctl_lun_req {
#define	CTL_BE_NAME_LEN		32
	char			backend[CTL_BE_NAME_LEN];
	ctl_lunreq_type		reqtype;
	union ctl_lunreq_data	reqdata;
	void *			args;
	nvlist_t *		args_nvl;
	size_t			args_len;
	void *			result;
	nvlist_t *		result_nvl;
	size_t			result_len;
	ctl_lun_status		status;
	char			error_str[CTL_ERROR_STR_LEN];
};

/*
 * LUN list status:
 *
 * NONE:		No status.
 *
 * OK:			Request completed successfully.
 *
 * NEED_MORE_SPACE:	The allocated length of the entries field is too
 * 			small for the available data.
 *
 * ERROR:		An error occurred, look at the error string for a
 *			description of the error.
 */
typedef enum {
	CTL_LUN_LIST_NONE,
	CTL_LUN_LIST_OK,
	CTL_LUN_LIST_NEED_MORE_SPACE,
	CTL_LUN_LIST_ERROR
} ctl_lun_list_status;

/*
 * LUN list interface
 *
 * backend_name:	This is a NUL-terminated string.  If the string
 *			length is 0, then all LUNs on all backends will
 *			be enumerated.  Otherwise this is the name of the
 *			backend to be enumerated, like "ramdisk" or "block".
 *
 * alloc_len:		The length of the data buffer allocated for entries.
 *			In order to properly size the buffer, make one call
 *			with alloc_len set to 0, and then use the returned
 *			dropped_len as the buffer length to allocate and
 *			pass in on a subsequent call.
 *
 * lun_xml:		XML-formatted information on the requested LUNs.
 *
 * fill_len:		The amount of data filled in the storage for entries.
 *
 * status:		The status of the request.  See above for the 
 *			description of the values of this field.
 *
 * error_str:		If the status indicates an error, this string will
 *			be filled in to describe the error.
 */
struct ctl_lun_list {
	char			backend[CTL_BE_NAME_LEN]; /* passed to kernel*/
	uint32_t		alloc_len;	/* passed to kernel */
	char			*lun_xml;	/* filled in kernel */
	uint32_t		fill_len;	/* passed to userland */
	ctl_lun_list_status	status;		/* passed to userland */
	char			error_str[CTL_ERROR_STR_LEN];
						/* passed to userland */
};

/*
 * Port request interface:
 *
 * driver:		This is required, and is NUL-terminated a string
 *			that is the name of the frontend, like "iscsi" .
 *
 * reqtype:		The type of request, CTL_REQ_CREATE to create a
 *			port, CTL_REQ_REMOVE to delete a port.
 *
 * num_be_args:		This is the number of frontend-specific arguments
 *			in the be_args array.
 *
 * be_args:		This is an array of frontend-specific arguments.
 *			See above for a description of the fields in this
 *			structure.
 *
 * status:		Status of the request.
 *
 * error_str:		If the status is CTL_LUN_ERROR, this will
 *			contain a string describing the error.
 *
 * kern_be_args:	For kernel use only.
 */
typedef enum {
	CTL_REQ_CREATE,
	CTL_REQ_REMOVE,
	CTL_REQ_MODIFY,
} ctl_req_type;

struct ctl_req {
	char			driver[CTL_DRIVER_NAME_LEN];
	ctl_req_type		reqtype;
	void *			args;
	nvlist_t *		args_nvl;
	size_t			args_len;
	void *			result;
	nvlist_t *		result_nvl;
	size_t			result_len;
	ctl_lun_status		status;
	char			error_str[CTL_ERROR_STR_LEN];
};

/*
 * iSCSI status
 *
 * OK:			Request completed successfully.
 *
 * ERROR:		An error occurred, look at the error string for a
 *			description of the error.
 *
 * CTL_ISCSI_LIST_NEED_MORE_SPACE:
 * 			User has to pass larger buffer for CTL_ISCSI_LIST ioctl.
 */
typedef enum {
	CTL_ISCSI_OK,
	CTL_ISCSI_ERROR,
	CTL_ISCSI_LIST_NEED_MORE_SPACE,
	CTL_ISCSI_SESSION_NOT_FOUND
} ctl_iscsi_status;

typedef enum {
	CTL_ISCSI_HANDOFF,
	CTL_ISCSI_LIST,
	CTL_ISCSI_LOGOUT,
	CTL_ISCSI_TERMINATE,
	CTL_ISCSI_LIMITS,
#if defined(ICL_KERNEL_PROXY) || 1
	/*
	 * We actually need those in all cases, but leave the ICL_KERNEL_PROXY,
	 * to remember to remove them along with rest of proxy code, eventually.
	 */
	CTL_ISCSI_LISTEN,
	CTL_ISCSI_ACCEPT,
	CTL_ISCSI_SEND,
	CTL_ISCSI_RECEIVE,
#endif
} ctl_iscsi_type;

typedef enum {
	CTL_ISCSI_DIGEST_NONE,
	CTL_ISCSI_DIGEST_CRC32C
} ctl_iscsi_digest;

#define	CTL_ISCSI_NAME_LEN	224	/* 223 bytes, by RFC 3720, + '\0' */
#define	CTL_ISCSI_ADDR_LEN	47	/* INET6_ADDRSTRLEN + '\0' */
#define	CTL_ISCSI_ALIAS_LEN	128	/* Arbitrary. */
#define	CTL_ISCSI_OFFLOAD_LEN	8	/* Arbitrary. */

struct ctl_iscsi_handoff_params {
	char			initiator_name[CTL_ISCSI_NAME_LEN];
	char			initiator_addr[CTL_ISCSI_ADDR_LEN];
	char			initiator_alias[CTL_ISCSI_ALIAS_LEN];
	uint8_t			initiator_isid[6];
	char			target_name[CTL_ISCSI_NAME_LEN];
	int			socket;
	int			portal_group_tag;

	/*
	 * Connection parameters negotiated by ctld(8).
	 */
	ctl_iscsi_digest	header_digest;
	ctl_iscsi_digest	data_digest;
	uint32_t		cmdsn;
	uint32_t		statsn;
	int			max_recv_data_segment_length;
	int			max_burst_length;
	int			first_burst_length;
	uint32_t		immediate_data;
	char			offload[CTL_ISCSI_OFFLOAD_LEN];
#ifdef ICL_KERNEL_PROXY
	int			connection_id;
#else
	int			spare;
#endif
	int			max_send_data_segment_length;
};

struct ctl_iscsi_list_params {
	uint32_t		alloc_len;	/* passed to kernel */
	char                   *conn_xml;	/* filled in kernel */
	uint32_t		fill_len;	/* passed to userland */
	int			spare[4];
};

struct ctl_iscsi_logout_params {
	int			connection_id;	/* passed to kernel */
	char			initiator_name[CTL_ISCSI_NAME_LEN];
						/* passed to kernel */
	char			initiator_addr[CTL_ISCSI_ADDR_LEN];
						/* passed to kernel */
	int			all;		/* passed to kernel */
	int			spare[4];
};

struct ctl_iscsi_terminate_params {
	int			connection_id;	/* passed to kernel */
	char			initiator_name[CTL_ISCSI_NAME_LEN];
						/* passed to kernel */
	char			initiator_addr[CTL_ISCSI_NAME_LEN];
						/* passed to kernel */
	int			all;		/* passed to kernel */
	int			spare[4];
};

struct ctl_iscsi_limits_params {
	/* passed to kernel */
	char			offload[CTL_ISCSI_OFFLOAD_LEN];

	/* passed to userland */
	size_t			spare;
	int			max_recv_data_segment_length;
	int			max_send_data_segment_length;
	int			max_burst_length;
	int			first_burst_length;
};

#ifdef ICL_KERNEL_PROXY
struct ctl_iscsi_listen_params {
	int			iser;
	int			domain;
	int			socktype;
	int			protocol;
	struct sockaddr		*addr;
	socklen_t		addrlen;
	int			portal_id;
	int			spare[4];
};

struct ctl_iscsi_accept_params {
	int			connection_id;
	int			portal_id;
	struct sockaddr		*initiator_addr;
	socklen_t		initiator_addrlen;
	int			spare[4];
};

struct ctl_iscsi_send_params {
	int			connection_id;
	void			*bhs;
	size_t			spare;
	void			*spare2;
	size_t			data_segment_len;
	void			*data_segment;
	int			spare3[4];
};

struct ctl_iscsi_receive_params {
	int			connection_id;
	void			*bhs;
	size_t			spare;
	void			*spare2;
	size_t			data_segment_len;
	void			*data_segment;
	int			spare3[4];
};

#endif /* ICL_KERNEL_PROXY */

union ctl_iscsi_data {
	struct ctl_iscsi_handoff_params		handoff;
	struct ctl_iscsi_list_params		list;
	struct ctl_iscsi_logout_params		logout;
	struct ctl_iscsi_terminate_params	terminate;
	struct ctl_iscsi_limits_params		limits;
#ifdef ICL_KERNEL_PROXY
	struct ctl_iscsi_listen_params		listen;
	struct ctl_iscsi_accept_params		accept;
	struct ctl_iscsi_send_params		send;
	struct ctl_iscsi_receive_params		receive;
#endif
};

/*
 * iSCSI interface
 *
 * status:		The status of the request.  See above for the 
 *			description of the values of this field.
 *
 * error_str:		If the status indicates an error, this string will
 *			be filled in to describe the error.
 */
struct ctl_iscsi {
	ctl_iscsi_type		type;		/* passed to kernel */
	union ctl_iscsi_data	data;		/* passed to kernel */
	ctl_iscsi_status	status;		/* passed to userland */
	char			error_str[CTL_ERROR_STR_LEN];
						/* passed to userland */
};

struct ctl_lun_map {
	uint32_t		port;
	uint32_t		plun;
	uint32_t		lun;
};

#define	CTL_IO			_IOWR(CTL_MINOR, 0x00, union ctl_io)
#define	CTL_ENABLE_PORT		_IOW(CTL_MINOR, 0x04, struct ctl_port_entry)
#define	CTL_DISABLE_PORT	_IOW(CTL_MINOR, 0x05, struct ctl_port_entry)
#define	CTL_DELAY_IO		_IOWR(CTL_MINOR, 0x10, struct ctl_io_delay_info)
#define	CTL_ERROR_INJECT	_IOWR(CTL_MINOR, 0x16, struct ctl_error_desc)
#define	CTL_GET_OOA		_IOWR(CTL_MINOR, 0x18, struct ctl_ooa)
#define	CTL_DUMP_STRUCTS	_IO(CTL_MINOR, 0x19)
#define	CTL_LUN_REQ		_IOWR(CTL_MINOR, 0x21, struct ctl_lun_req)
#define	CTL_LUN_LIST		_IOWR(CTL_MINOR, 0x22, struct ctl_lun_list)
#define	CTL_ERROR_INJECT_DELETE	_IOW(CTL_MINOR, 0x23, struct ctl_error_desc)
#define	CTL_SET_PORT_WWNS	_IOW(CTL_MINOR, 0x24, struct ctl_port_entry)
#define	CTL_ISCSI		_IOWR(CTL_MINOR, 0x25, struct ctl_iscsi)
#define	CTL_PORT_REQ		_IOWR(CTL_MINOR, 0x26, struct ctl_req)
#define	CTL_PORT_LIST		_IOWR(CTL_MINOR, 0x27, struct ctl_lun_list)
#define	CTL_LUN_MAP		_IOW(CTL_MINOR, 0x28, struct ctl_lun_map)
#define	CTL_GET_LUN_STATS	_IOWR(CTL_MINOR, 0x29, struct ctl_get_io_stats)
#define	CTL_GET_PORT_STATS	_IOWR(CTL_MINOR, 0x2a, struct ctl_get_io_stats)

#endif /* _CTL_IOCTL_H_ */

/*
 * vim: ts=8
 */
