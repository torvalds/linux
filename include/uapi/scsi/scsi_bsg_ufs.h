/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * UFS Transport SGIO v4 BSG Message Support
 *
 * Copyright (C) 2011-2013 Samsung India Software Operations
 * Copyright (C) 2018 Western Digital Corporation
 */
#ifndef SCSI_BSG_UFS_H
#define SCSI_BSG_UFS_H

#include <asm/byteorder.h>
#include <linux/types.h>
/*
 * This file intended to be included by both kernel and user space
 */

#define UFS_CDB_SIZE	16
/* uic commands are 4DW long, per UFSHCI V2.1 paragraph 5.6.1 */
#define UIC_CMD_SIZE (sizeof(__u32) * 4)

enum ufs_bsg_msg_code {
	UPIU_TRANSACTION_UIC_CMD = 0x1F,
	UPIU_TRANSACTION_ARPMB_CMD,
};

/* UFS RPMB Request Message Types */
enum ufs_rpmb_op_type {
	UFS_RPMB_WRITE_KEY		= 0x01,
	UFS_RPMB_READ_CNT		= 0x02,
	UFS_RPMB_WRITE			= 0x03,
	UFS_RPMB_READ			= 0x04,
	UFS_RPMB_READ_RESP		= 0x05,
	UFS_RPMB_SEC_CONF_WRITE		= 0x06,
	UFS_RPMB_SEC_CONF_READ		= 0x07,
	UFS_RPMB_PURGE_ENABLE		= 0x08,
	UFS_RPMB_PURGE_STATUS_READ	= 0x09,
};

/**
 * struct utp_upiu_header - UPIU header structure
 * @dword_0: UPIU header DW-0
 * @dword_1: UPIU header DW-1
 * @dword_2: UPIU header DW-2
 *
 * @transaction_code: Type of request or response. See also enum
 *	upiu_request_transaction and enum upiu_response_transaction.
 * @flags: UPIU flags. The meaning of individual flags depends on the
 *	transaction code.
 * @lun: Logical unit number.
 * @task_tag: Task tag.
 * @iid: Initiator ID.
 * @command_set_type: 0 for SCSI command set; 1 for UFS specific.
 * @tm_function: Task management function in case of a task management request
 *	UPIU.
 * @query_function: Query function in case of a query request UPIU.
 * @response: 0 for success; 1 for failure.
 * @status: SCSI status if this is the header of a response to a SCSI command.
 * @ehs_length: EHS length in units of 32 bytes.
 * @device_information:
 * @data_segment_length: data segment length.
 */
struct utp_upiu_header {
	union {
		struct {
			__be32 dword_0;
			__be32 dword_1;
			__be32 dword_2;
		};
		struct {
			__u8 transaction_code;
			__u8 flags;
			__u8 lun;
			__u8 task_tag;
#if defined(__BIG_ENDIAN)
			__u8 iid: 4;
			__u8 command_set_type: 4;
#elif defined(__LITTLE_ENDIAN)
			__u8 command_set_type: 4;
			__u8 iid: 4;
#else
#error
#endif
			union {
				__u8 tm_function;
				__u8 query_function;
			};
			__u8 response;
			__u8 status;
			__u8 ehs_length;
			__u8 device_information;
			__be16 data_segment_length;
		};
	};
};

/**
 * struct utp_upiu_query - upiu request buffer structure for
 * query request.
 * @opcode: command to perform B-0
 * @idn: a value that indicates the particular type of data B-1
 * @index: Index to further identify data B-2
 * @selector: Index to further identify data B-3
 * @reserved_osf: spec reserved field B-4,5
 * @length: number of descriptor bytes to read/write B-6,7
 * @value: Attribute value to be written DW-5
 * @reserved: spec reserved DW-6,7
 */
struct utp_upiu_query {
	__u8 opcode;
	__u8 idn;
	__u8 index;
	__u8 selector;
	__be16 reserved_osf;
	__be16 length;
	__be32 value;
	__be32 reserved[2];
};

/**
 * struct utp_upiu_query_v4_0 - upiu request buffer structure for
 * query request >= UFS 4.0 spec.
 * @opcode: command to perform B-0
 * @idn: a value that indicates the particular type of data B-1
 * @index: Index to further identify data B-2
 * @selector: Index to further identify data B-3
 * @osf4: spec field B-5
 * @osf5: spec field B 6,7
 * @osf6: spec field DW 8,9
 * @osf7: spec field DW 10,11
 */
struct utp_upiu_query_v4_0 {
	__u8 opcode;
	__u8 idn;
	__u8 index;
	__u8 selector;
	__u8 osf3;
	__u8 osf4;
	__be16 osf5;
	__be32 osf6;
	__be32 osf7;
	__be32 reserved;
};

/**
 * struct utp_upiu_cmd - Command UPIU structure
 * @data_transfer_len: Data Transfer Length DW-3
 * @cdb: Command Descriptor Block CDB DW-4 to DW-7
 */
struct utp_upiu_cmd {
	__be32 exp_data_transfer_len;
	__u8 cdb[UFS_CDB_SIZE];
};

/**
 * struct utp_upiu_req - general upiu request structure
 * @header:UPIU header structure DW-0 to DW-2
 * @sc: fields structure for scsi command DW-3 to DW-7
 * @qr: fields structure for query request DW-3 to DW-7
 * @uc: use utp_upiu_query to host the 4 dwords of uic command
 */
struct utp_upiu_req {
	struct utp_upiu_header header;
	union {
		struct utp_upiu_cmd		sc;
		struct utp_upiu_query		qr;
		struct utp_upiu_query		uc;
	};
};

struct ufs_arpmb_meta {
	__be16	req_resp_type;
	__u8	nonce[16];
	__be32	write_counter;
	__be16	addr_lun;
	__be16	block_count;
	__be16	result;
} __attribute__((__packed__));

struct ufs_ehs {
	__u8	length;
	__u8	ehs_type;
	__be16	ehssub_type;
	struct ufs_arpmb_meta meta;
	__u8	mac_key[32];
} __attribute__((__packed__));

/* request (CDB) structure of the sg_io_v4 */
struct ufs_bsg_request {
	__u32 msgcode;
	struct utp_upiu_req upiu_req;
};

/* response (request sense data) structure of the sg_io_v4 */
struct ufs_bsg_reply {
	/*
	 * The completion result. Result exists in two forms:
	 * if negative, it is an -Exxx system errno value. There will
	 * be no further reply information supplied.
	 * else, it's the 4-byte scsi error result, with driver, host,
	 * msg and status fields. The per-msgcode reply structure
	 * will contain valid data.
	 */
	int result;

	/* If there was reply_payload, how much was received? */
	__u32 reply_payload_rcv_len;

	struct utp_upiu_req upiu_rsp;
};

struct ufs_rpmb_request {
	struct ufs_bsg_request bsg_request;
	struct ufs_ehs ehs_req;
};

struct ufs_rpmb_reply {
	struct ufs_bsg_reply bsg_reply;
	struct ufs_ehs ehs_rsp;
};
#endif /* UFS_BSG_H */
