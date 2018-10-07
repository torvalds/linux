/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UFS Transport SGIO v4 BSG Message Support
 *
 * Copyright (C) 2011-2013 Samsung India Software Operations
 */
#ifndef SCSI_BSG_UFS_H
#define SCSI_BSG_UFS_H

/*
 * This file intended to be included by both kernel and user space
 */

#define UFS_CDB_SIZE	16

/**
 * struct utp_upiu_header - UPIU header structure
 * @dword_0: UPIU header DW-0
 * @dword_1: UPIU header DW-1
 * @dword_2: UPIU header DW-2
 */
struct utp_upiu_header {
	__be32 dword_0;
	__be32 dword_1;
	__be32 dword_2;
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
 * struct utp_upiu_cmd - Command UPIU structure
 * @data_transfer_len: Data Transfer Length DW-3
 * @cdb: Command Descriptor Block CDB DW-4 to DW-7
 */
struct utp_upiu_cmd {
	__be32 exp_data_transfer_len;
	u8 cdb[UFS_CDB_SIZE];
};

/**
 * struct utp_upiu_req - general upiu request structure
 * @header:UPIU header structure DW-0 to DW-2
 * @sc: fields structure for scsi command DW-3 to DW-7
 * @qr: fields structure for query request DW-3 to DW-7
 */
struct utp_upiu_req {
	struct utp_upiu_header header;
	union {
		struct utp_upiu_cmd		sc;
		struct utp_upiu_query		qr;
	};
};
#endif /* UFS_BSG_H */
