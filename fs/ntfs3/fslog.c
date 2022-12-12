// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/random.h>
#include <linux/slab.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * LOG FILE structs
 */

// clang-format off

#define MaxLogFileSize     0x100000000ull
#define DefaultLogPageSize 4096
#define MinLogRecordPages  0x30

struct RESTART_HDR {
	struct NTFS_RECORD_HEADER rhdr; // 'RSTR'
	__le32 sys_page_size; // 0x10: Page size of the system which initialized the log.
	__le32 page_size;     // 0x14: Log page size used for this log file.
	__le16 ra_off;        // 0x18:
	__le16 minor_ver;     // 0x1A:
	__le16 major_ver;     // 0x1C:
	__le16 fixups[];
};

#define LFS_NO_CLIENT 0xffff
#define LFS_NO_CLIENT_LE cpu_to_le16(0xffff)

struct CLIENT_REC {
	__le64 oldest_lsn;
	__le64 restart_lsn; // 0x08:
	__le16 prev_client; // 0x10:
	__le16 next_client; // 0x12:
	__le16 seq_num;     // 0x14:
	u8 align[6];        // 0x16:
	__le32 name_bytes;  // 0x1C: In bytes.
	__le16 name[32];    // 0x20: Name of client.
};

static_assert(sizeof(struct CLIENT_REC) == 0x60);

/* Two copies of these will exist at the beginning of the log file */
struct RESTART_AREA {
	__le64 current_lsn;    // 0x00: Current logical end of log file.
	__le16 log_clients;    // 0x08: Maximum number of clients.
	__le16 client_idx[2];  // 0x0A: Free/use index into the client record arrays.
	__le16 flags;          // 0x0E: See RESTART_SINGLE_PAGE_IO.
	__le32 seq_num_bits;   // 0x10: The number of bits in sequence number.
	__le16 ra_len;         // 0x14:
	__le16 client_off;     // 0x16:
	__le64 l_size;         // 0x18: Usable log file size.
	__le32 last_lsn_data_len; // 0x20:
	__le16 rec_hdr_len;    // 0x24: Log page data offset.
	__le16 data_off;       // 0x26: Log page data length.
	__le32 open_log_count; // 0x28:
	__le32 align[5];       // 0x2C:
	struct CLIENT_REC clients[]; // 0x40:
};

struct LOG_REC_HDR {
	__le16 redo_op;      // 0x00:  NTFS_LOG_OPERATION
	__le16 undo_op;      // 0x02:  NTFS_LOG_OPERATION
	__le16 redo_off;     // 0x04:  Offset to Redo record.
	__le16 redo_len;     // 0x06:  Redo length.
	__le16 undo_off;     // 0x08:  Offset to Undo record.
	__le16 undo_len;     // 0x0A:  Undo length.
	__le16 target_attr;  // 0x0C:
	__le16 lcns_follow;  // 0x0E:
	__le16 record_off;   // 0x10:
	__le16 attr_off;     // 0x12:
	__le16 cluster_off;  // 0x14:
	__le16 reserved;     // 0x16:
	__le64 target_vcn;   // 0x18:
	__le64 page_lcns[];  // 0x20:
};

static_assert(sizeof(struct LOG_REC_HDR) == 0x20);

#define RESTART_ENTRY_ALLOCATED    0xFFFFFFFF
#define RESTART_ENTRY_ALLOCATED_LE cpu_to_le32(0xFFFFFFFF)

struct RESTART_TABLE {
	__le16 size;       // 0x00: In bytes
	__le16 used;       // 0x02: Entries
	__le16 total;      // 0x04: Entries
	__le16 res[3];     // 0x06:
	__le32 free_goal;  // 0x0C:
	__le32 first_free; // 0x10:
	__le32 last_free;  // 0x14:

};

static_assert(sizeof(struct RESTART_TABLE) == 0x18);

struct ATTR_NAME_ENTRY {
	__le16 off; // Offset in the Open attribute Table.
	__le16 name_bytes;
	__le16 name[];
};

struct OPEN_ATTR_ENRTY {
	__le32 next;            // 0x00: RESTART_ENTRY_ALLOCATED if allocated
	__le32 bytes_per_index; // 0x04:
	enum ATTR_TYPE type;    // 0x08:
	u8 is_dirty_pages;      // 0x0C:
	u8 is_attr_name;        // 0x0B: Faked field to manage 'ptr'
	u8 name_len;            // 0x0C: Faked field to manage 'ptr'
	u8 res;
	struct MFT_REF ref;     // 0x10: File Reference of file containing attribute
	__le64 open_record_lsn; // 0x18:
	void *ptr;              // 0x20:
};

/* 32 bit version of 'struct OPEN_ATTR_ENRTY' */
struct OPEN_ATTR_ENRTY_32 {
	__le32 next;            // 0x00: RESTART_ENTRY_ALLOCATED if allocated
	__le32 ptr;             // 0x04:
	struct MFT_REF ref;     // 0x08:
	__le64 open_record_lsn; // 0x10:
	u8 is_dirty_pages;      // 0x18:
	u8 is_attr_name;        // 0x19:
	u8 res1[2];
	enum ATTR_TYPE type;    // 0x1C:
	u8 name_len;            // 0x20: In wchar
	u8 res2[3];
	__le32 AttributeName;   // 0x24:
	__le32 bytes_per_index; // 0x28:
};

#define SIZEOF_OPENATTRIBUTEENTRY0 0x2c
// static_assert( 0x2C == sizeof(struct OPEN_ATTR_ENRTY_32) );
static_assert(sizeof(struct OPEN_ATTR_ENRTY) < SIZEOF_OPENATTRIBUTEENTRY0);

/*
 * One entry exists in the Dirty Pages Table for each page which is dirty at
 * the time the Restart Area is written.
 */
struct DIR_PAGE_ENTRY {
	__le32 next;         // 0x00: RESTART_ENTRY_ALLOCATED if allocated
	__le32 target_attr;  // 0x04: Index into the Open attribute Table
	__le32 transfer_len; // 0x08:
	__le32 lcns_follow;  // 0x0C:
	__le64 vcn;          // 0x10: Vcn of dirty page
	__le64 oldest_lsn;   // 0x18:
	__le64 page_lcns[];  // 0x20:
};

static_assert(sizeof(struct DIR_PAGE_ENTRY) == 0x20);

/* 32 bit version of 'struct DIR_PAGE_ENTRY' */
struct DIR_PAGE_ENTRY_32 {
	__le32 next;		// 0x00: RESTART_ENTRY_ALLOCATED if allocated
	__le32 target_attr;	// 0x04: Index into the Open attribute Table
	__le32 transfer_len;	// 0x08:
	__le32 lcns_follow;	// 0x0C:
	__le32 reserved;	// 0x10:
	__le32 vcn_low;		// 0x14: Vcn of dirty page
	__le32 vcn_hi;		// 0x18: Vcn of dirty page
	__le32 oldest_lsn_low;	// 0x1C:
	__le32 oldest_lsn_hi;	// 0x1C:
	__le32 page_lcns_low;	// 0x24:
	__le32 page_lcns_hi;	// 0x24:
};

static_assert(offsetof(struct DIR_PAGE_ENTRY_32, vcn_low) == 0x14);
static_assert(sizeof(struct DIR_PAGE_ENTRY_32) == 0x2c);

enum transact_state {
	TransactionUninitialized = 0,
	TransactionActive,
	TransactionPrepared,
	TransactionCommitted
};

struct TRANSACTION_ENTRY {
	__le32 next;          // 0x00: RESTART_ENTRY_ALLOCATED if allocated
	u8 transact_state;    // 0x04:
	u8 reserved[3];       // 0x05:
	__le64 first_lsn;     // 0x08:
	__le64 prev_lsn;      // 0x10:
	__le64 undo_next_lsn; // 0x18:
	__le32 undo_records;  // 0x20: Number of undo log records pending abort
	__le32 undo_len;      // 0x24: Total undo size
};

static_assert(sizeof(struct TRANSACTION_ENTRY) == 0x28);

struct NTFS_RESTART {
	__le32 major_ver;             // 0x00:
	__le32 minor_ver;             // 0x04:
	__le64 check_point_start;     // 0x08:
	__le64 open_attr_table_lsn;   // 0x10:
	__le64 attr_names_lsn;        // 0x18:
	__le64 dirty_pages_table_lsn; // 0x20:
	__le64 transact_table_lsn;    // 0x28:
	__le32 open_attr_len;         // 0x30: In bytes
	__le32 attr_names_len;        // 0x34: In bytes
	__le32 dirty_pages_len;       // 0x38: In bytes
	__le32 transact_table_len;    // 0x3C: In bytes
};

static_assert(sizeof(struct NTFS_RESTART) == 0x40);

struct NEW_ATTRIBUTE_SIZES {
	__le64 alloc_size;
	__le64 valid_size;
	__le64 data_size;
	__le64 total_size;
};

struct BITMAP_RANGE {
	__le32 bitmap_off;
	__le32 bits;
};

struct LCN_RANGE {
	__le64 lcn;
	__le64 len;
};

/* The following type defines the different log record types. */
#define LfsClientRecord  cpu_to_le32(1)
#define LfsClientRestart cpu_to_le32(2)

/* This is used to uniquely identify a client for a particular log file. */
struct CLIENT_ID {
	__le16 seq_num;
	__le16 client_idx;
};

/* This is the header that begins every Log Record in the log file. */
struct LFS_RECORD_HDR {
	__le64 this_lsn;		// 0x00:
	__le64 client_prev_lsn;		// 0x08:
	__le64 client_undo_next_lsn;	// 0x10:
	__le32 client_data_len;		// 0x18:
	struct CLIENT_ID client;	// 0x1C: Owner of this log record.
	__le32 record_type;		// 0x20: LfsClientRecord or LfsClientRestart.
	__le32 transact_id;		// 0x24:
	__le16 flags;			// 0x28: LOG_RECORD_MULTI_PAGE
	u8 align[6];			// 0x2A:
};

#define LOG_RECORD_MULTI_PAGE cpu_to_le16(1)

static_assert(sizeof(struct LFS_RECORD_HDR) == 0x30);

struct LFS_RECORD {
	__le16 next_record_off;	// 0x00: Offset of the free space in the page,
	u8 align[6];		// 0x02:
	__le64 last_end_lsn;	// 0x08: lsn for the last log record which ends on the page,
};

static_assert(sizeof(struct LFS_RECORD) == 0x10);

struct RECORD_PAGE_HDR {
	struct NTFS_RECORD_HEADER rhdr;	// 'RCRD'
	__le32 rflags;			// 0x10: See LOG_PAGE_LOG_RECORD_END
	__le16 page_count;		// 0x14:
	__le16 page_pos;		// 0x16:
	struct LFS_RECORD record_hdr;	// 0x18:
	__le16 fixups[10];		// 0x28:
	__le32 file_off;		// 0x3c: Used when major version >= 2
};

// clang-format on

// Page contains the end of a log record.
#define LOG_PAGE_LOG_RECORD_END cpu_to_le32(0x00000001)

static inline bool is_log_record_end(const struct RECORD_PAGE_HDR *hdr)
{
	return hdr->rflags & LOG_PAGE_LOG_RECORD_END;
}

static_assert(offsetof(struct RECORD_PAGE_HDR, file_off) == 0x3c);

/*
 * END of NTFS LOG structures
 */

/* Define some tuning parameters to keep the restart tables a reasonable size. */
#define INITIAL_NUMBER_TRANSACTIONS 5

enum NTFS_LOG_OPERATION {

	Noop = 0x00,
	CompensationLogRecord = 0x01,
	InitializeFileRecordSegment = 0x02,
	DeallocateFileRecordSegment = 0x03,
	WriteEndOfFileRecordSegment = 0x04,
	CreateAttribute = 0x05,
	DeleteAttribute = 0x06,
	UpdateResidentValue = 0x07,
	UpdateNonresidentValue = 0x08,
	UpdateMappingPairs = 0x09,
	DeleteDirtyClusters = 0x0A,
	SetNewAttributeSizes = 0x0B,
	AddIndexEntryRoot = 0x0C,
	DeleteIndexEntryRoot = 0x0D,
	AddIndexEntryAllocation = 0x0E,
	DeleteIndexEntryAllocation = 0x0F,
	WriteEndOfIndexBuffer = 0x10,
	SetIndexEntryVcnRoot = 0x11,
	SetIndexEntryVcnAllocation = 0x12,
	UpdateFileNameRoot = 0x13,
	UpdateFileNameAllocation = 0x14,
	SetBitsInNonresidentBitMap = 0x15,
	ClearBitsInNonresidentBitMap = 0x16,
	HotFix = 0x17,
	EndTopLevelAction = 0x18,
	PrepareTransaction = 0x19,
	CommitTransaction = 0x1A,
	ForgetTransaction = 0x1B,
	OpenNonresidentAttribute = 0x1C,
	OpenAttributeTableDump = 0x1D,
	AttributeNamesDump = 0x1E,
	DirtyPageTableDump = 0x1F,
	TransactionTableDump = 0x20,
	UpdateRecordDataRoot = 0x21,
	UpdateRecordDataAllocation = 0x22,

	UpdateRelativeDataInIndex =
		0x23, // NtOfsRestartUpdateRelativeDataInIndex
	UpdateRelativeDataInIndex2 = 0x24,
	ZeroEndOfFileRecord = 0x25,
};

/*
 * Array for log records which require a target attribute.
 * A true indicates that the corresponding restart operation
 * requires a target attribute.
 */
static const u8 AttributeRequired[] = {
	0xFC, 0xFB, 0xFF, 0x10, 0x06,
};

static inline bool is_target_required(u16 op)
{
	bool ret = op <= UpdateRecordDataAllocation &&
		   (AttributeRequired[op >> 3] >> (op & 7) & 1);
	return ret;
}

static inline bool can_skip_action(enum NTFS_LOG_OPERATION op)
{
	switch (op) {
	case Noop:
	case DeleteDirtyClusters:
	case HotFix:
	case EndTopLevelAction:
	case PrepareTransaction:
	case CommitTransaction:
	case ForgetTransaction:
	case CompensationLogRecord:
	case OpenNonresidentAttribute:
	case OpenAttributeTableDump:
	case AttributeNamesDump:
	case DirtyPageTableDump:
	case TransactionTableDump:
		return true;
	default:
		return false;
	}
}

enum { lcb_ctx_undo_next, lcb_ctx_prev, lcb_ctx_next };

/* Bytes per restart table. */
static inline u32 bytes_per_rt(const struct RESTART_TABLE *rt)
{
	return le16_to_cpu(rt->used) * le16_to_cpu(rt->size) +
	       sizeof(struct RESTART_TABLE);
}

/* Log record length. */
static inline u32 lrh_length(const struct LOG_REC_HDR *lr)
{
	u16 t16 = le16_to_cpu(lr->lcns_follow);

	return struct_size(lr, page_lcns, max_t(u16, 1, t16));
}

struct lcb {
	struct LFS_RECORD_HDR *lrh; // Log record header of the current lsn.
	struct LOG_REC_HDR *log_rec;
	u32 ctx_mode; // lcb_ctx_undo_next/lcb_ctx_prev/lcb_ctx_next
	struct CLIENT_ID client;
	bool alloc; // If true the we should deallocate 'log_rec'.
};

static void lcb_put(struct lcb *lcb)
{
	if (lcb->alloc)
		kfree(lcb->log_rec);
	kfree(lcb->lrh);
	kfree(lcb);
}

/* Find the oldest lsn from active clients. */
static inline void oldest_client_lsn(const struct CLIENT_REC *ca,
				     __le16 next_client, u64 *oldest_lsn)
{
	while (next_client != LFS_NO_CLIENT_LE) {
		const struct CLIENT_REC *cr = ca + le16_to_cpu(next_client);
		u64 lsn = le64_to_cpu(cr->oldest_lsn);

		/* Ignore this block if it's oldest lsn is 0. */
		if (lsn && lsn < *oldest_lsn)
			*oldest_lsn = lsn;

		next_client = cr->next_client;
	}
}

static inline bool is_rst_page_hdr_valid(u32 file_off,
					 const struct RESTART_HDR *rhdr)
{
	u32 sys_page = le32_to_cpu(rhdr->sys_page_size);
	u32 page_size = le32_to_cpu(rhdr->page_size);
	u32 end_usa;
	u16 ro;

	if (sys_page < SECTOR_SIZE || page_size < SECTOR_SIZE ||
	    sys_page & (sys_page - 1) || page_size & (page_size - 1)) {
		return false;
	}

	/* Check that if the file offset isn't 0, it is the system page size. */
	if (file_off && file_off != sys_page)
		return false;

	/* Check support version 1.1+. */
	if (le16_to_cpu(rhdr->major_ver) <= 1 && !rhdr->minor_ver)
		return false;

	if (le16_to_cpu(rhdr->major_ver) > 2)
		return false;

	ro = le16_to_cpu(rhdr->ra_off);
	if (!IS_ALIGNED(ro, 8) || ro > sys_page)
		return false;

	end_usa = ((sys_page >> SECTOR_SHIFT) + 1) * sizeof(short);
	end_usa += le16_to_cpu(rhdr->rhdr.fix_off);

	if (ro < end_usa)
		return false;

	return true;
}

static inline bool is_rst_area_valid(const struct RESTART_HDR *rhdr)
{
	const struct RESTART_AREA *ra;
	u16 cl, fl, ul;
	u32 off, l_size, file_dat_bits, file_size_round;
	u16 ro = le16_to_cpu(rhdr->ra_off);
	u32 sys_page = le32_to_cpu(rhdr->sys_page_size);

	if (ro + offsetof(struct RESTART_AREA, l_size) >
	    SECTOR_SIZE - sizeof(short))
		return false;

	ra = Add2Ptr(rhdr, ro);
	cl = le16_to_cpu(ra->log_clients);

	if (cl > 1)
		return false;

	off = le16_to_cpu(ra->client_off);

	if (!IS_ALIGNED(off, 8) || ro + off > SECTOR_SIZE - sizeof(short))
		return false;

	off += cl * sizeof(struct CLIENT_REC);

	if (off > sys_page)
		return false;

	/*
	 * Check the restart length field and whether the entire
	 * restart area is contained that length.
	 */
	if (le16_to_cpu(rhdr->ra_off) + le16_to_cpu(ra->ra_len) > sys_page ||
	    off > le16_to_cpu(ra->ra_len)) {
		return false;
	}

	/*
	 * As a final check make sure that the use list and the free list
	 * are either empty or point to a valid client.
	 */
	fl = le16_to_cpu(ra->client_idx[0]);
	ul = le16_to_cpu(ra->client_idx[1]);
	if ((fl != LFS_NO_CLIENT && fl >= cl) ||
	    (ul != LFS_NO_CLIENT && ul >= cl))
		return false;

	/* Make sure the sequence number bits match the log file size. */
	l_size = le64_to_cpu(ra->l_size);

	file_dat_bits = sizeof(u64) * 8 - le32_to_cpu(ra->seq_num_bits);
	file_size_round = 1u << (file_dat_bits + 3);
	if (file_size_round != l_size &&
	    (file_size_round < l_size || (file_size_round / 2) > l_size)) {
		return false;
	}

	/* The log page data offset and record header length must be quad-aligned. */
	if (!IS_ALIGNED(le16_to_cpu(ra->data_off), 8) ||
	    !IS_ALIGNED(le16_to_cpu(ra->rec_hdr_len), 8))
		return false;

	return true;
}

static inline bool is_client_area_valid(const struct RESTART_HDR *rhdr,
					bool usa_error)
{
	u16 ro = le16_to_cpu(rhdr->ra_off);
	const struct RESTART_AREA *ra = Add2Ptr(rhdr, ro);
	u16 ra_len = le16_to_cpu(ra->ra_len);
	const struct CLIENT_REC *ca;
	u32 i;

	if (usa_error && ra_len + ro > SECTOR_SIZE - sizeof(short))
		return false;

	/* Find the start of the client array. */
	ca = Add2Ptr(ra, le16_to_cpu(ra->client_off));

	/*
	 * Start with the free list.
	 * Check that all the clients are valid and that there isn't a cycle.
	 * Do the in-use list on the second pass.
	 */
	for (i = 0; i < 2; i++) {
		u16 client_idx = le16_to_cpu(ra->client_idx[i]);
		bool first_client = true;
		u16 clients = le16_to_cpu(ra->log_clients);

		while (client_idx != LFS_NO_CLIENT) {
			const struct CLIENT_REC *cr;

			if (!clients ||
			    client_idx >= le16_to_cpu(ra->log_clients))
				return false;

			clients -= 1;
			cr = ca + client_idx;

			client_idx = le16_to_cpu(cr->next_client);

			if (first_client) {
				first_client = false;
				if (cr->prev_client != LFS_NO_CLIENT_LE)
					return false;
			}
		}
	}

	return true;
}

/*
 * remove_client
 *
 * Remove a client record from a client record list an restart area.
 */
static inline void remove_client(struct CLIENT_REC *ca,
				 const struct CLIENT_REC *cr, __le16 *head)
{
	if (cr->prev_client == LFS_NO_CLIENT_LE)
		*head = cr->next_client;
	else
		ca[le16_to_cpu(cr->prev_client)].next_client = cr->next_client;

	if (cr->next_client != LFS_NO_CLIENT_LE)
		ca[le16_to_cpu(cr->next_client)].prev_client = cr->prev_client;
}

/*
 * add_client - Add a client record to the start of a list.
 */
static inline void add_client(struct CLIENT_REC *ca, u16 index, __le16 *head)
{
	struct CLIENT_REC *cr = ca + index;

	cr->prev_client = LFS_NO_CLIENT_LE;
	cr->next_client = *head;

	if (*head != LFS_NO_CLIENT_LE)
		ca[le16_to_cpu(*head)].prev_client = cpu_to_le16(index);

	*head = cpu_to_le16(index);
}

static inline void *enum_rstbl(struct RESTART_TABLE *t, void *c)
{
	__le32 *e;
	u32 bprt;
	u16 rsize = t ? le16_to_cpu(t->size) : 0;

	if (!c) {
		if (!t || !t->total)
			return NULL;
		e = Add2Ptr(t, sizeof(struct RESTART_TABLE));
	} else {
		e = Add2Ptr(c, rsize);
	}

	/* Loop until we hit the first one allocated, or the end of the list. */
	for (bprt = bytes_per_rt(t); PtrOffset(t, e) < bprt;
	     e = Add2Ptr(e, rsize)) {
		if (*e == RESTART_ENTRY_ALLOCATED_LE)
			return e;
	}
	return NULL;
}

/*
 * find_dp - Search for a @vcn in Dirty Page Table.
 */
static inline struct DIR_PAGE_ENTRY *find_dp(struct RESTART_TABLE *dptbl,
					     u32 target_attr, u64 vcn)
{
	__le32 ta = cpu_to_le32(target_attr);
	struct DIR_PAGE_ENTRY *dp = NULL;

	while ((dp = enum_rstbl(dptbl, dp))) {
		u64 dp_vcn = le64_to_cpu(dp->vcn);

		if (dp->target_attr == ta && vcn >= dp_vcn &&
		    vcn < dp_vcn + le32_to_cpu(dp->lcns_follow)) {
			return dp;
		}
	}
	return NULL;
}

static inline u32 norm_file_page(u32 page_size, u32 *l_size, bool use_default)
{
	if (use_default)
		page_size = DefaultLogPageSize;

	/* Round the file size down to a system page boundary. */
	*l_size &= ~(page_size - 1);

	/* File should contain at least 2 restart pages and MinLogRecordPages pages. */
	if (*l_size < (MinLogRecordPages + 2) * page_size)
		return 0;

	return page_size;
}

static bool check_log_rec(const struct LOG_REC_HDR *lr, u32 bytes, u32 tr,
			  u32 bytes_per_attr_entry)
{
	u16 t16;

	if (bytes < sizeof(struct LOG_REC_HDR))
		return false;
	if (!tr)
		return false;

	if ((tr - sizeof(struct RESTART_TABLE)) %
	    sizeof(struct TRANSACTION_ENTRY))
		return false;

	if (le16_to_cpu(lr->redo_off) & 7)
		return false;

	if (le16_to_cpu(lr->undo_off) & 7)
		return false;

	if (lr->target_attr)
		goto check_lcns;

	if (is_target_required(le16_to_cpu(lr->redo_op)))
		return false;

	if (is_target_required(le16_to_cpu(lr->undo_op)))
		return false;

check_lcns:
	if (!lr->lcns_follow)
		goto check_length;

	t16 = le16_to_cpu(lr->target_attr);
	if ((t16 - sizeof(struct RESTART_TABLE)) % bytes_per_attr_entry)
		return false;

check_length:
	if (bytes < lrh_length(lr))
		return false;

	return true;
}

static bool check_rstbl(const struct RESTART_TABLE *rt, size_t bytes)
{
	u32 ts;
	u32 i, off;
	u16 rsize = le16_to_cpu(rt->size);
	u16 ne = le16_to_cpu(rt->used);
	u32 ff = le32_to_cpu(rt->first_free);
	u32 lf = le32_to_cpu(rt->last_free);

	ts = rsize * ne + sizeof(struct RESTART_TABLE);

	if (!rsize || rsize > bytes ||
	    rsize + sizeof(struct RESTART_TABLE) > bytes || bytes < ts ||
	    le16_to_cpu(rt->total) > ne || ff > ts || lf > ts ||
	    (ff && ff < sizeof(struct RESTART_TABLE)) ||
	    (lf && lf < sizeof(struct RESTART_TABLE))) {
		return false;
	}

	/*
	 * Verify each entry is either allocated or points
	 * to a valid offset the table.
	 */
	for (i = 0; i < ne; i++) {
		off = le32_to_cpu(*(__le32 *)Add2Ptr(
			rt, i * rsize + sizeof(struct RESTART_TABLE)));

		if (off != RESTART_ENTRY_ALLOCATED && off &&
		    (off < sizeof(struct RESTART_TABLE) ||
		     ((off - sizeof(struct RESTART_TABLE)) % rsize))) {
			return false;
		}
	}

	/*
	 * Walk through the list headed by the first entry to make
	 * sure none of the entries are currently being used.
	 */
	for (off = ff; off;) {
		if (off == RESTART_ENTRY_ALLOCATED)
			return false;

		off = le32_to_cpu(*(__le32 *)Add2Ptr(rt, off));
	}

	return true;
}

/*
 * free_rsttbl_idx - Free a previously allocated index a Restart Table.
 */
static inline void free_rsttbl_idx(struct RESTART_TABLE *rt, u32 off)
{
	__le32 *e;
	u32 lf = le32_to_cpu(rt->last_free);
	__le32 off_le = cpu_to_le32(off);

	e = Add2Ptr(rt, off);

	if (off < le32_to_cpu(rt->free_goal)) {
		*e = rt->first_free;
		rt->first_free = off_le;
		if (!lf)
			rt->last_free = off_le;
	} else {
		if (lf)
			*(__le32 *)Add2Ptr(rt, lf) = off_le;
		else
			rt->first_free = off_le;

		rt->last_free = off_le;
		*e = 0;
	}

	le16_sub_cpu(&rt->total, 1);
}

static inline struct RESTART_TABLE *init_rsttbl(u16 esize, u16 used)
{
	__le32 *e, *last_free;
	u32 off;
	u32 bytes = esize * used + sizeof(struct RESTART_TABLE);
	u32 lf = sizeof(struct RESTART_TABLE) + (used - 1) * esize;
	struct RESTART_TABLE *t = kzalloc(bytes, GFP_NOFS);

	if (!t)
		return NULL;

	t->size = cpu_to_le16(esize);
	t->used = cpu_to_le16(used);
	t->free_goal = cpu_to_le32(~0u);
	t->first_free = cpu_to_le32(sizeof(struct RESTART_TABLE));
	t->last_free = cpu_to_le32(lf);

	e = (__le32 *)(t + 1);
	last_free = Add2Ptr(t, lf);

	for (off = sizeof(struct RESTART_TABLE) + esize; e < last_free;
	     e = Add2Ptr(e, esize), off += esize) {
		*e = cpu_to_le32(off);
	}
	return t;
}

static inline struct RESTART_TABLE *extend_rsttbl(struct RESTART_TABLE *tbl,
						  u32 add, u32 free_goal)
{
	u16 esize = le16_to_cpu(tbl->size);
	__le32 osize = cpu_to_le32(bytes_per_rt(tbl));
	u32 used = le16_to_cpu(tbl->used);
	struct RESTART_TABLE *rt;

	rt = init_rsttbl(esize, used + add);
	if (!rt)
		return NULL;

	memcpy(rt + 1, tbl + 1, esize * used);

	rt->free_goal = free_goal == ~0u
				? cpu_to_le32(~0u)
				: cpu_to_le32(sizeof(struct RESTART_TABLE) +
					      free_goal * esize);

	if (tbl->first_free) {
		rt->first_free = tbl->first_free;
		*(__le32 *)Add2Ptr(rt, le32_to_cpu(tbl->last_free)) = osize;
	} else {
		rt->first_free = osize;
	}

	rt->total = tbl->total;

	kfree(tbl);
	return rt;
}

/*
 * alloc_rsttbl_idx
 *
 * Allocate an index from within a previously initialized Restart Table.
 */
static inline void *alloc_rsttbl_idx(struct RESTART_TABLE **tbl)
{
	u32 off;
	__le32 *e;
	struct RESTART_TABLE *t = *tbl;

	if (!t->first_free) {
		*tbl = t = extend_rsttbl(t, 16, ~0u);
		if (!t)
			return NULL;
	}

	off = le32_to_cpu(t->first_free);

	/* Dequeue this entry and zero it. */
	e = Add2Ptr(t, off);

	t->first_free = *e;

	memset(e, 0, le16_to_cpu(t->size));

	*e = RESTART_ENTRY_ALLOCATED_LE;

	/* If list is going empty, then we fix the last_free as well. */
	if (!t->first_free)
		t->last_free = 0;

	le16_add_cpu(&t->total, 1);

	return Add2Ptr(t, off);
}

/*
 * alloc_rsttbl_from_idx
 *
 * Allocate a specific index from within a previously initialized Restart Table.
 */
static inline void *alloc_rsttbl_from_idx(struct RESTART_TABLE **tbl, u32 vbo)
{
	u32 off;
	__le32 *e;
	struct RESTART_TABLE *rt = *tbl;
	u32 bytes = bytes_per_rt(rt);
	u16 esize = le16_to_cpu(rt->size);

	/* If the entry is not the table, we will have to extend the table. */
	if (vbo >= bytes) {
		/*
		 * Extend the size by computing the number of entries between
		 * the existing size and the desired index and adding 1 to that.
		 */
		u32 bytes2idx = vbo - bytes;

		/*
		 * There should always be an integral number of entries
		 * being added. Now extend the table.
		 */
		*tbl = rt = extend_rsttbl(rt, bytes2idx / esize + 1, bytes);
		if (!rt)
			return NULL;
	}

	/* See if the entry is already allocated, and just return if it is. */
	e = Add2Ptr(rt, vbo);

	if (*e == RESTART_ENTRY_ALLOCATED_LE)
		return e;

	/*
	 * Walk through the table, looking for the entry we're
	 * interested and the previous entry.
	 */
	off = le32_to_cpu(rt->first_free);
	e = Add2Ptr(rt, off);

	if (off == vbo) {
		/* this is a match */
		rt->first_free = *e;
		goto skip_looking;
	}

	/*
	 * Need to walk through the list looking for the predecessor
	 * of our entry.
	 */
	for (;;) {
		/* Remember the entry just found */
		u32 last_off = off;
		__le32 *last_e = e;

		/* Should never run of entries. */

		/* Lookup up the next entry the list. */
		off = le32_to_cpu(*last_e);
		e = Add2Ptr(rt, off);

		/* If this is our match we are done. */
		if (off == vbo) {
			*last_e = *e;

			/*
			 * If this was the last entry, we update that
			 * table as well.
			 */
			if (le32_to_cpu(rt->last_free) == off)
				rt->last_free = cpu_to_le32(last_off);
			break;
		}
	}

skip_looking:
	/* If the list is now empty, we fix the last_free as well. */
	if (!rt->first_free)
		rt->last_free = 0;

	/* Zero this entry. */
	memset(e, 0, esize);
	*e = RESTART_ENTRY_ALLOCATED_LE;

	le16_add_cpu(&rt->total, 1);

	return e;
}

#define RESTART_SINGLE_PAGE_IO cpu_to_le16(0x0001)

#define NTFSLOG_WRAPPED 0x00000001
#define NTFSLOG_MULTIPLE_PAGE_IO 0x00000002
#define NTFSLOG_NO_LAST_LSN 0x00000004
#define NTFSLOG_REUSE_TAIL 0x00000010
#define NTFSLOG_NO_OLDEST_LSN 0x00000020

/* Helper struct to work with NTFS $LogFile. */
struct ntfs_log {
	struct ntfs_inode *ni;

	u32 l_size;
	u32 sys_page_size;
	u32 sys_page_mask;
	u32 page_size;
	u32 page_mask; // page_size - 1
	u8 page_bits;
	struct RECORD_PAGE_HDR *one_page_buf;

	struct RESTART_TABLE *open_attr_tbl;
	u32 transaction_id;
	u32 clst_per_page;

	u32 first_page;
	u32 next_page;
	u32 ra_off;
	u32 data_off;
	u32 restart_size;
	u32 data_size;
	u16 record_header_len;
	u64 seq_num;
	u32 seq_num_bits;
	u32 file_data_bits;
	u32 seq_num_mask; /* (1 << file_data_bits) - 1 */

	struct RESTART_AREA *ra; /* In-memory image of the next restart area. */
	u32 ra_size; /* The usable size of the restart area. */

	/*
	 * If true, then the in-memory restart area is to be written
	 * to the first position on the disk.
	 */
	bool init_ra;
	bool set_dirty; /* True if we need to set dirty flag. */

	u64 oldest_lsn;

	u32 oldest_lsn_off;
	u64 last_lsn;

	u32 total_avail;
	u32 total_avail_pages;
	u32 total_undo_commit;
	u32 max_current_avail;
	u32 current_avail;
	u32 reserved;

	short major_ver;
	short minor_ver;

	u32 l_flags; /* See NTFSLOG_XXX */
	u32 current_openlog_count; /* On-disk value for open_log_count. */

	struct CLIENT_ID client_id;
	u32 client_undo_commit;
};

static inline u32 lsn_to_vbo(struct ntfs_log *log, const u64 lsn)
{
	u32 vbo = (lsn << log->seq_num_bits) >> (log->seq_num_bits - 3);

	return vbo;
}

/* Compute the offset in the log file of the next log page. */
static inline u32 next_page_off(struct ntfs_log *log, u32 off)
{
	off = (off & ~log->sys_page_mask) + log->page_size;
	return off >= log->l_size ? log->first_page : off;
}

static inline u32 lsn_to_page_off(struct ntfs_log *log, u64 lsn)
{
	return (((u32)lsn) << 3) & log->page_mask;
}

static inline u64 vbo_to_lsn(struct ntfs_log *log, u32 off, u64 Seq)
{
	return (off >> 3) + (Seq << log->file_data_bits);
}

static inline bool is_lsn_in_file(struct ntfs_log *log, u64 lsn)
{
	return lsn >= log->oldest_lsn &&
	       lsn <= le64_to_cpu(log->ra->current_lsn);
}

static inline u32 hdr_file_off(struct ntfs_log *log,
			       struct RECORD_PAGE_HDR *hdr)
{
	if (log->major_ver < 2)
		return le64_to_cpu(hdr->rhdr.lsn);

	return le32_to_cpu(hdr->file_off);
}

static inline u64 base_lsn(struct ntfs_log *log,
			   const struct RECORD_PAGE_HDR *hdr, u64 lsn)
{
	u64 h_lsn = le64_to_cpu(hdr->rhdr.lsn);
	u64 ret = (((h_lsn >> log->file_data_bits) +
		    (lsn < (lsn_to_vbo(log, h_lsn) & ~log->page_mask) ? 1 : 0))
		   << log->file_data_bits) +
		  ((((is_log_record_end(hdr) &&
		      h_lsn <= le64_to_cpu(hdr->record_hdr.last_end_lsn))
			     ? le16_to_cpu(hdr->record_hdr.next_record_off)
			     : log->page_size) +
		    lsn) >>
		   3);

	return ret;
}

static inline bool verify_client_lsn(struct ntfs_log *log,
				     const struct CLIENT_REC *client, u64 lsn)
{
	return lsn >= le64_to_cpu(client->oldest_lsn) &&
	       lsn <= le64_to_cpu(log->ra->current_lsn) && lsn;
}

struct restart_info {
	u64 last_lsn;
	struct RESTART_HDR *r_page;
	u32 vbo;
	bool chkdsk_was_run;
	bool valid_page;
	bool initialized;
	bool restart;
};

static int read_log_page(struct ntfs_log *log, u32 vbo,
			 struct RECORD_PAGE_HDR **buffer, bool *usa_error)
{
	int err = 0;
	u32 page_idx = vbo >> log->page_bits;
	u32 page_off = vbo & log->page_mask;
	u32 bytes = log->page_size - page_off;
	void *to_free = NULL;
	u32 page_vbo = page_idx << log->page_bits;
	struct RECORD_PAGE_HDR *page_buf;
	struct ntfs_inode *ni = log->ni;
	bool bBAAD;

	if (vbo >= log->l_size)
		return -EINVAL;

	if (!*buffer) {
		to_free = kmalloc(log->page_size, GFP_NOFS);
		if (!to_free)
			return -ENOMEM;
		*buffer = to_free;
	}

	page_buf = page_off ? log->one_page_buf : *buffer;

	err = ntfs_read_run_nb(ni->mi.sbi, &ni->file.run, page_vbo, page_buf,
			       log->page_size, NULL);
	if (err)
		goto out;

	if (page_buf->rhdr.sign != NTFS_FFFF_SIGNATURE)
		ntfs_fix_post_read(&page_buf->rhdr, PAGE_SIZE, false);

	if (page_buf != *buffer)
		memcpy(*buffer, Add2Ptr(page_buf, page_off), bytes);

	bBAAD = page_buf->rhdr.sign == NTFS_BAAD_SIGNATURE;

	if (usa_error)
		*usa_error = bBAAD;
	/* Check that the update sequence array for this page is valid */
	/* If we don't allow errors, raise an error status */
	else if (bBAAD)
		err = -EINVAL;

out:
	if (err && to_free) {
		kfree(to_free);
		*buffer = NULL;
	}

	return err;
}

/*
 * log_read_rst
 *
 * It walks through 512 blocks of the file looking for a valid
 * restart page header. It will stop the first time we find a
 * valid page header.
 */
static int log_read_rst(struct ntfs_log *log, u32 l_size, bool first,
			struct restart_info *info)
{
	u32 skip, vbo;
	struct RESTART_HDR *r_page = NULL;

	/* Determine which restart area we are looking for. */
	if (first) {
		vbo = 0;
		skip = 512;
	} else {
		vbo = 512;
		skip = 0;
	}

	/* Loop continuously until we succeed. */
	for (; vbo < l_size; vbo = 2 * vbo + skip, skip = 0) {
		bool usa_error;
		bool brst, bchk;
		struct RESTART_AREA *ra;

		/* Read a page header at the current offset. */
		if (read_log_page(log, vbo, (struct RECORD_PAGE_HDR **)&r_page,
				  &usa_error)) {
			/* Ignore any errors. */
			continue;
		}

		/* Exit if the signature is a log record page. */
		if (r_page->rhdr.sign == NTFS_RCRD_SIGNATURE) {
			info->initialized = true;
			break;
		}

		brst = r_page->rhdr.sign == NTFS_RSTR_SIGNATURE;
		bchk = r_page->rhdr.sign == NTFS_CHKD_SIGNATURE;

		if (!bchk && !brst) {
			if (r_page->rhdr.sign != NTFS_FFFF_SIGNATURE) {
				/*
				 * Remember if the signature does not
				 * indicate uninitialized file.
				 */
				info->initialized = true;
			}
			continue;
		}

		ra = NULL;
		info->valid_page = false;
		info->initialized = true;
		info->vbo = vbo;

		/* Let's check the restart area if this is a valid page. */
		if (!is_rst_page_hdr_valid(vbo, r_page))
			goto check_result;
		ra = Add2Ptr(r_page, le16_to_cpu(r_page->ra_off));

		if (!is_rst_area_valid(r_page))
			goto check_result;

		/*
		 * We have a valid restart page header and restart area.
		 * If chkdsk was run or we have no clients then we have
		 * no more checking to do.
		 */
		if (bchk || ra->client_idx[1] == LFS_NO_CLIENT_LE) {
			info->valid_page = true;
			goto check_result;
		}

		if (is_client_area_valid(r_page, usa_error)) {
			info->valid_page = true;
			ra = Add2Ptr(r_page, le16_to_cpu(r_page->ra_off));
		}

check_result:
		/*
		 * If chkdsk was run then update the caller's
		 * values and return.
		 */
		if (r_page->rhdr.sign == NTFS_CHKD_SIGNATURE) {
			info->chkdsk_was_run = true;
			info->last_lsn = le64_to_cpu(r_page->rhdr.lsn);
			info->restart = true;
			info->r_page = r_page;
			return 0;
		}

		/*
		 * If we have a valid page then copy the values
		 * we need from it.
		 */
		if (info->valid_page) {
			info->last_lsn = le64_to_cpu(ra->current_lsn);
			info->restart = true;
			info->r_page = r_page;
			return 0;
		}
	}

	kfree(r_page);

	return 0;
}

/*
 * Ilog_init_pg_hdr - Init @log from restart page header.
 */
static void log_init_pg_hdr(struct ntfs_log *log, u32 sys_page_size,
			    u32 page_size, u16 major_ver, u16 minor_ver)
{
	log->sys_page_size = sys_page_size;
	log->sys_page_mask = sys_page_size - 1;
	log->page_size = page_size;
	log->page_mask = page_size - 1;
	log->page_bits = blksize_bits(page_size);

	log->clst_per_page = log->page_size >> log->ni->mi.sbi->cluster_bits;
	if (!log->clst_per_page)
		log->clst_per_page = 1;

	log->first_page = major_ver >= 2
				  ? 0x22 * page_size
				  : ((sys_page_size << 1) + (page_size << 1));
	log->major_ver = major_ver;
	log->minor_ver = minor_ver;
}

/*
 * log_create - Init @log in cases when we don't have a restart area to use.
 */
static void log_create(struct ntfs_log *log, u32 l_size, const u64 last_lsn,
		       u32 open_log_count, bool wrapped, bool use_multi_page)
{
	log->l_size = l_size;
	/* All file offsets must be quadword aligned. */
	log->file_data_bits = blksize_bits(l_size) - 3;
	log->seq_num_mask = (8 << log->file_data_bits) - 1;
	log->seq_num_bits = sizeof(u64) * 8 - log->file_data_bits;
	log->seq_num = (last_lsn >> log->file_data_bits) + 2;
	log->next_page = log->first_page;
	log->oldest_lsn = log->seq_num << log->file_data_bits;
	log->oldest_lsn_off = 0;
	log->last_lsn = log->oldest_lsn;

	log->l_flags |= NTFSLOG_NO_LAST_LSN | NTFSLOG_NO_OLDEST_LSN;

	/* Set the correct flags for the I/O and indicate if we have wrapped. */
	if (wrapped)
		log->l_flags |= NTFSLOG_WRAPPED;

	if (use_multi_page)
		log->l_flags |= NTFSLOG_MULTIPLE_PAGE_IO;

	/* Compute the log page values. */
	log->data_off = ALIGN(
		offsetof(struct RECORD_PAGE_HDR, fixups) +
			sizeof(short) * ((log->page_size >> SECTOR_SHIFT) + 1),
		8);
	log->data_size = log->page_size - log->data_off;
	log->record_header_len = sizeof(struct LFS_RECORD_HDR);

	/* Remember the different page sizes for reservation. */
	log->reserved = log->data_size - log->record_header_len;

	/* Compute the restart page values. */
	log->ra_off = ALIGN(
		offsetof(struct RESTART_HDR, fixups) +
			sizeof(short) *
				((log->sys_page_size >> SECTOR_SHIFT) + 1),
		8);
	log->restart_size = log->sys_page_size - log->ra_off;
	log->ra_size = struct_size(log->ra, clients, 1);
	log->current_openlog_count = open_log_count;

	/*
	 * The total available log file space is the number of
	 * log file pages times the space available on each page.
	 */
	log->total_avail_pages = log->l_size - log->first_page;
	log->total_avail = log->total_avail_pages >> log->page_bits;

	/*
	 * We assume that we can't use the end of the page less than
	 * the file record size.
	 * Then we won't need to reserve more than the caller asks for.
	 */
	log->max_current_avail = log->total_avail * log->reserved;
	log->total_avail = log->total_avail * log->data_size;
	log->current_avail = log->max_current_avail;
}

/*
 * log_create_ra - Fill a restart area from the values stored in @log.
 */
static struct RESTART_AREA *log_create_ra(struct ntfs_log *log)
{
	struct CLIENT_REC *cr;
	struct RESTART_AREA *ra = kzalloc(log->restart_size, GFP_NOFS);

	if (!ra)
		return NULL;

	ra->current_lsn = cpu_to_le64(log->last_lsn);
	ra->log_clients = cpu_to_le16(1);
	ra->client_idx[1] = LFS_NO_CLIENT_LE;
	if (log->l_flags & NTFSLOG_MULTIPLE_PAGE_IO)
		ra->flags = RESTART_SINGLE_PAGE_IO;
	ra->seq_num_bits = cpu_to_le32(log->seq_num_bits);
	ra->ra_len = cpu_to_le16(log->ra_size);
	ra->client_off = cpu_to_le16(offsetof(struct RESTART_AREA, clients));
	ra->l_size = cpu_to_le64(log->l_size);
	ra->rec_hdr_len = cpu_to_le16(log->record_header_len);
	ra->data_off = cpu_to_le16(log->data_off);
	ra->open_log_count = cpu_to_le32(log->current_openlog_count + 1);

	cr = ra->clients;

	cr->prev_client = LFS_NO_CLIENT_LE;
	cr->next_client = LFS_NO_CLIENT_LE;

	return ra;
}

static u32 final_log_off(struct ntfs_log *log, u64 lsn, u32 data_len)
{
	u32 base_vbo = lsn << 3;
	u32 final_log_off = (base_vbo & log->seq_num_mask) & ~log->page_mask;
	u32 page_off = base_vbo & log->page_mask;
	u32 tail = log->page_size - page_off;

	page_off -= 1;

	/* Add the length of the header. */
	data_len += log->record_header_len;

	/*
	 * If this lsn is contained this log page we are done.
	 * Otherwise we need to walk through several log pages.
	 */
	if (data_len > tail) {
		data_len -= tail;
		tail = log->data_size;
		page_off = log->data_off - 1;

		for (;;) {
			final_log_off = next_page_off(log, final_log_off);

			/*
			 * We are done if the remaining bytes
			 * fit on this page.
			 */
			if (data_len <= tail)
				break;
			data_len -= tail;
		}
	}

	/*
	 * We add the remaining bytes to our starting position on this page
	 * and then add that value to the file offset of this log page.
	 */
	return final_log_off + data_len + page_off;
}

static int next_log_lsn(struct ntfs_log *log, const struct LFS_RECORD_HDR *rh,
			u64 *lsn)
{
	int err;
	u64 this_lsn = le64_to_cpu(rh->this_lsn);
	u32 vbo = lsn_to_vbo(log, this_lsn);
	u32 end =
		final_log_off(log, this_lsn, le32_to_cpu(rh->client_data_len));
	u32 hdr_off = end & ~log->sys_page_mask;
	u64 seq = this_lsn >> log->file_data_bits;
	struct RECORD_PAGE_HDR *page = NULL;

	/* Remember if we wrapped. */
	if (end <= vbo)
		seq += 1;

	/* Log page header for this page. */
	err = read_log_page(log, hdr_off, &page, NULL);
	if (err)
		return err;

	/*
	 * If the lsn we were given was not the last lsn on this page,
	 * then the starting offset for the next lsn is on a quad word
	 * boundary following the last file offset for the current lsn.
	 * Otherwise the file offset is the start of the data on the next page.
	 */
	if (this_lsn == le64_to_cpu(page->rhdr.lsn)) {
		/* If we wrapped, we need to increment the sequence number. */
		hdr_off = next_page_off(log, hdr_off);
		if (hdr_off == log->first_page)
			seq += 1;

		vbo = hdr_off + log->data_off;
	} else {
		vbo = ALIGN(end, 8);
	}

	/* Compute the lsn based on the file offset and the sequence count. */
	*lsn = vbo_to_lsn(log, vbo, seq);

	/*
	 * If this lsn is within the legal range for the file, we return true.
	 * Otherwise false indicates that there are no more lsn's.
	 */
	if (!is_lsn_in_file(log, *lsn))
		*lsn = 0;

	kfree(page);

	return 0;
}

/*
 * current_log_avail - Calculate the number of bytes available for log records.
 */
static u32 current_log_avail(struct ntfs_log *log)
{
	u32 oldest_off, next_free_off, free_bytes;

	if (log->l_flags & NTFSLOG_NO_LAST_LSN) {
		/* The entire file is available. */
		return log->max_current_avail;
	}

	/*
	 * If there is a last lsn the restart area then we know that we will
	 * have to compute the free range.
	 * If there is no oldest lsn then start at the first page of the file.
	 */
	oldest_off = (log->l_flags & NTFSLOG_NO_OLDEST_LSN)
			     ? log->first_page
			     : (log->oldest_lsn_off & ~log->sys_page_mask);

	/*
	 * We will use the next log page offset to compute the next free page.
	 * If we are going to reuse this page go to the next page.
	 * If we are at the first page then use the end of the file.
	 */
	next_free_off = (log->l_flags & NTFSLOG_REUSE_TAIL)
				? log->next_page + log->page_size
				: log->next_page == log->first_page
					  ? log->l_size
					  : log->next_page;

	/* If the two offsets are the same then there is no available space. */
	if (oldest_off == next_free_off)
		return 0;
	/*
	 * If the free offset follows the oldest offset then subtract
	 * this range from the total available pages.
	 */
	free_bytes =
		oldest_off < next_free_off
			? log->total_avail_pages - (next_free_off - oldest_off)
			: oldest_off - next_free_off;

	free_bytes >>= log->page_bits;
	return free_bytes * log->reserved;
}

static bool check_subseq_log_page(struct ntfs_log *log,
				  const struct RECORD_PAGE_HDR *rp, u32 vbo,
				  u64 seq)
{
	u64 lsn_seq;
	const struct NTFS_RECORD_HEADER *rhdr = &rp->rhdr;
	u64 lsn = le64_to_cpu(rhdr->lsn);

	if (rhdr->sign == NTFS_FFFF_SIGNATURE || !rhdr->sign)
		return false;

	/*
	 * If the last lsn on the page occurs was written after the page
	 * that caused the original error then we have a fatal error.
	 */
	lsn_seq = lsn >> log->file_data_bits;

	/*
	 * If the sequence number for the lsn the page is equal or greater
	 * than lsn we expect, then this is a subsequent write.
	 */
	return lsn_seq >= seq ||
	       (lsn_seq == seq - 1 && log->first_page == vbo &&
		vbo != (lsn_to_vbo(log, lsn) & ~log->page_mask));
}

/*
 * last_log_lsn
 *
 * Walks through the log pages for a file, searching for the
 * last log page written to the file.
 */
static int last_log_lsn(struct ntfs_log *log)
{
	int err;
	bool usa_error = false;
	bool replace_page = false;
	bool reuse_page = log->l_flags & NTFSLOG_REUSE_TAIL;
	bool wrapped_file, wrapped;

	u32 page_cnt = 1, page_pos = 1;
	u32 page_off = 0, page_off1 = 0, saved_off = 0;
	u32 final_off, second_off, final_off_prev = 0, second_off_prev = 0;
	u32 first_file_off = 0, second_file_off = 0;
	u32 part_io_count = 0;
	u32 tails = 0;
	u32 this_off, curpage_off, nextpage_off, remain_pages;

	u64 expected_seq, seq_base = 0, lsn_base = 0;
	u64 best_lsn, best_lsn1, best_lsn2;
	u64 lsn_cur, lsn1, lsn2;
	u64 last_ok_lsn = reuse_page ? log->last_lsn : 0;

	u16 cur_pos, best_page_pos;

	struct RECORD_PAGE_HDR *page = NULL;
	struct RECORD_PAGE_HDR *tst_page = NULL;
	struct RECORD_PAGE_HDR *first_tail = NULL;
	struct RECORD_PAGE_HDR *second_tail = NULL;
	struct RECORD_PAGE_HDR *tail_page = NULL;
	struct RECORD_PAGE_HDR *second_tail_prev = NULL;
	struct RECORD_PAGE_HDR *first_tail_prev = NULL;
	struct RECORD_PAGE_HDR *page_bufs = NULL;
	struct RECORD_PAGE_HDR *best_page;

	if (log->major_ver >= 2) {
		final_off = 0x02 * log->page_size;
		second_off = 0x12 * log->page_size;

		// 0x10 == 0x12 - 0x2
		page_bufs = kmalloc(log->page_size * 0x10, GFP_NOFS);
		if (!page_bufs)
			return -ENOMEM;
	} else {
		second_off = log->first_page - log->page_size;
		final_off = second_off - log->page_size;
	}

next_tail:
	/* Read second tail page (at pos 3/0x12000). */
	if (read_log_page(log, second_off, &second_tail, &usa_error) ||
	    usa_error || second_tail->rhdr.sign != NTFS_RCRD_SIGNATURE) {
		kfree(second_tail);
		second_tail = NULL;
		second_file_off = 0;
		lsn2 = 0;
	} else {
		second_file_off = hdr_file_off(log, second_tail);
		lsn2 = le64_to_cpu(second_tail->record_hdr.last_end_lsn);
	}

	/* Read first tail page (at pos 2/0x2000). */
	if (read_log_page(log, final_off, &first_tail, &usa_error) ||
	    usa_error || first_tail->rhdr.sign != NTFS_RCRD_SIGNATURE) {
		kfree(first_tail);
		first_tail = NULL;
		first_file_off = 0;
		lsn1 = 0;
	} else {
		first_file_off = hdr_file_off(log, first_tail);
		lsn1 = le64_to_cpu(first_tail->record_hdr.last_end_lsn);
	}

	if (log->major_ver < 2) {
		int best_page;

		first_tail_prev = first_tail;
		final_off_prev = first_file_off;
		second_tail_prev = second_tail;
		second_off_prev = second_file_off;
		tails = 1;

		if (!first_tail && !second_tail)
			goto tail_read;

		if (first_tail && second_tail)
			best_page = lsn1 < lsn2 ? 1 : 0;
		else if (first_tail)
			best_page = 0;
		else
			best_page = 1;

		page_off = best_page ? second_file_off : first_file_off;
		seq_base = (best_page ? lsn2 : lsn1) >> log->file_data_bits;
		goto tail_read;
	}

	best_lsn1 = first_tail ? base_lsn(log, first_tail, first_file_off) : 0;
	best_lsn2 =
		second_tail ? base_lsn(log, second_tail, second_file_off) : 0;

	if (first_tail && second_tail) {
		if (best_lsn1 > best_lsn2) {
			best_lsn = best_lsn1;
			best_page = first_tail;
			this_off = first_file_off;
		} else {
			best_lsn = best_lsn2;
			best_page = second_tail;
			this_off = second_file_off;
		}
	} else if (first_tail) {
		best_lsn = best_lsn1;
		best_page = first_tail;
		this_off = first_file_off;
	} else if (second_tail) {
		best_lsn = best_lsn2;
		best_page = second_tail;
		this_off = second_file_off;
	} else {
		goto tail_read;
	}

	best_page_pos = le16_to_cpu(best_page->page_pos);

	if (!tails) {
		if (best_page_pos == page_pos) {
			seq_base = best_lsn >> log->file_data_bits;
			saved_off = page_off = le32_to_cpu(best_page->file_off);
			lsn_base = best_lsn;

			memmove(page_bufs, best_page, log->page_size);

			page_cnt = le16_to_cpu(best_page->page_count);
			if (page_cnt > 1)
				page_pos += 1;

			tails = 1;
		}
	} else if (seq_base == (best_lsn >> log->file_data_bits) &&
		   saved_off + log->page_size == this_off &&
		   lsn_base < best_lsn &&
		   (page_pos != page_cnt || best_page_pos == page_pos ||
		    best_page_pos == 1) &&
		   (page_pos >= page_cnt || best_page_pos == page_pos)) {
		u16 bppc = le16_to_cpu(best_page->page_count);

		saved_off += log->page_size;
		lsn_base = best_lsn;

		memmove(Add2Ptr(page_bufs, tails * log->page_size), best_page,
			log->page_size);

		tails += 1;

		if (best_page_pos != bppc) {
			page_cnt = bppc;
			page_pos = best_page_pos;

			if (page_cnt > 1)
				page_pos += 1;
		} else {
			page_pos = page_cnt = 1;
		}
	} else {
		kfree(first_tail);
		kfree(second_tail);
		goto tail_read;
	}

	kfree(first_tail_prev);
	first_tail_prev = first_tail;
	final_off_prev = first_file_off;
	first_tail = NULL;

	kfree(second_tail_prev);
	second_tail_prev = second_tail;
	second_off_prev = second_file_off;
	second_tail = NULL;

	final_off += log->page_size;
	second_off += log->page_size;

	if (tails < 0x10)
		goto next_tail;
tail_read:
	first_tail = first_tail_prev;
	final_off = final_off_prev;

	second_tail = second_tail_prev;
	second_off = second_off_prev;

	page_cnt = page_pos = 1;

	curpage_off = seq_base == log->seq_num ? min(log->next_page, page_off)
					       : log->next_page;

	wrapped_file =
		curpage_off == log->first_page &&
		!(log->l_flags & (NTFSLOG_NO_LAST_LSN | NTFSLOG_REUSE_TAIL));

	expected_seq = wrapped_file ? (log->seq_num + 1) : log->seq_num;

	nextpage_off = curpage_off;

next_page:
	tail_page = NULL;
	/* Read the next log page. */
	err = read_log_page(log, curpage_off, &page, &usa_error);

	/* Compute the next log page offset the file. */
	nextpage_off = next_page_off(log, curpage_off);
	wrapped = nextpage_off == log->first_page;

	if (tails > 1) {
		struct RECORD_PAGE_HDR *cur_page =
			Add2Ptr(page_bufs, curpage_off - page_off);

		if (curpage_off == saved_off) {
			tail_page = cur_page;
			goto use_tail_page;
		}

		if (page_off > curpage_off || curpage_off >= saved_off)
			goto use_tail_page;

		if (page_off1)
			goto use_cur_page;

		if (!err && !usa_error &&
		    page->rhdr.sign == NTFS_RCRD_SIGNATURE &&
		    cur_page->rhdr.lsn == page->rhdr.lsn &&
		    cur_page->record_hdr.next_record_off ==
			    page->record_hdr.next_record_off &&
		    ((page_pos == page_cnt &&
		      le16_to_cpu(page->page_pos) == 1) ||
		     (page_pos != page_cnt &&
		      le16_to_cpu(page->page_pos) == page_pos + 1 &&
		      le16_to_cpu(page->page_count) == page_cnt))) {
			cur_page = NULL;
			goto use_tail_page;
		}

		page_off1 = page_off;

use_cur_page:

		lsn_cur = le64_to_cpu(cur_page->rhdr.lsn);

		if (last_ok_lsn !=
			    le64_to_cpu(cur_page->record_hdr.last_end_lsn) &&
		    ((lsn_cur >> log->file_data_bits) +
		     ((curpage_off <
		       (lsn_to_vbo(log, lsn_cur) & ~log->page_mask))
			      ? 1
			      : 0)) != expected_seq) {
			goto check_tail;
		}

		if (!is_log_record_end(cur_page)) {
			tail_page = NULL;
			last_ok_lsn = lsn_cur;
			goto next_page_1;
		}

		log->seq_num = expected_seq;
		log->l_flags &= ~NTFSLOG_NO_LAST_LSN;
		log->last_lsn = le64_to_cpu(cur_page->record_hdr.last_end_lsn);
		log->ra->current_lsn = cur_page->record_hdr.last_end_lsn;

		if (log->record_header_len <=
		    log->page_size -
			    le16_to_cpu(cur_page->record_hdr.next_record_off)) {
			log->l_flags |= NTFSLOG_REUSE_TAIL;
			log->next_page = curpage_off;
		} else {
			log->l_flags &= ~NTFSLOG_REUSE_TAIL;
			log->next_page = nextpage_off;
		}

		if (wrapped_file)
			log->l_flags |= NTFSLOG_WRAPPED;

		last_ok_lsn = le64_to_cpu(cur_page->record_hdr.last_end_lsn);
		goto next_page_1;
	}

	/*
	 * If we are at the expected first page of a transfer check to see
	 * if either tail copy is at this offset.
	 * If this page is the last page of a transfer, check if we wrote
	 * a subsequent tail copy.
	 */
	if (page_cnt == page_pos || page_cnt == page_pos + 1) {
		/*
		 * Check if the offset matches either the first or second
		 * tail copy. It is possible it will match both.
		 */
		if (curpage_off == final_off)
			tail_page = first_tail;

		/*
		 * If we already matched on the first page then
		 * check the ending lsn's.
		 */
		if (curpage_off == second_off) {
			if (!tail_page ||
			    (second_tail &&
			     le64_to_cpu(second_tail->record_hdr.last_end_lsn) >
				     le64_to_cpu(first_tail->record_hdr
							 .last_end_lsn))) {
				tail_page = second_tail;
			}
		}
	}

use_tail_page:
	if (tail_page) {
		/* We have a candidate for a tail copy. */
		lsn_cur = le64_to_cpu(tail_page->record_hdr.last_end_lsn);

		if (last_ok_lsn < lsn_cur) {
			/*
			 * If the sequence number is not expected,
			 * then don't use the tail copy.
			 */
			if (expected_seq != (lsn_cur >> log->file_data_bits))
				tail_page = NULL;
		} else if (last_ok_lsn > lsn_cur) {
			/*
			 * If the last lsn is greater than the one on
			 * this page then forget this tail.
			 */
			tail_page = NULL;
		}
	}

	/*
	 *If we have an error on the current page,
	 * we will break of this loop.
	 */
	if (err || usa_error)
		goto check_tail;

	/*
	 * Done if the last lsn on this page doesn't match the previous known
	 * last lsn or the sequence number is not expected.
	 */
	lsn_cur = le64_to_cpu(page->rhdr.lsn);
	if (last_ok_lsn != lsn_cur &&
	    expected_seq != (lsn_cur >> log->file_data_bits)) {
		goto check_tail;
	}

	/*
	 * Check that the page position and page count values are correct.
	 * If this is the first page of a transfer the position must be 1
	 * and the count will be unknown.
	 */
	if (page_cnt == page_pos) {
		if (page->page_pos != cpu_to_le16(1) &&
		    (!reuse_page || page->page_pos != page->page_count)) {
			/*
			 * If the current page is the first page we are
			 * looking at and we are reusing this page then
			 * it can be either the first or last page of a
			 * transfer. Otherwise it can only be the first.
			 */
			goto check_tail;
		}
	} else if (le16_to_cpu(page->page_count) != page_cnt ||
		   le16_to_cpu(page->page_pos) != page_pos + 1) {
		/*
		 * The page position better be 1 more than the last page
		 * position and the page count better match.
		 */
		goto check_tail;
	}

	/*
	 * We have a valid page the file and may have a valid page
	 * the tail copy area.
	 * If the tail page was written after the page the file then
	 * break of the loop.
	 */
	if (tail_page &&
	    le64_to_cpu(tail_page->record_hdr.last_end_lsn) > lsn_cur) {
		/* Remember if we will replace the page. */
		replace_page = true;
		goto check_tail;
	}

	tail_page = NULL;

	if (is_log_record_end(page)) {
		/*
		 * Since we have read this page we know the sequence number
		 * is the same as our expected value.
		 */
		log->seq_num = expected_seq;
		log->last_lsn = le64_to_cpu(page->record_hdr.last_end_lsn);
		log->ra->current_lsn = page->record_hdr.last_end_lsn;
		log->l_flags &= ~NTFSLOG_NO_LAST_LSN;

		/*
		 * If there is room on this page for another header then
		 * remember we want to reuse the page.
		 */
		if (log->record_header_len <=
		    log->page_size -
			    le16_to_cpu(page->record_hdr.next_record_off)) {
			log->l_flags |= NTFSLOG_REUSE_TAIL;
			log->next_page = curpage_off;
		} else {
			log->l_flags &= ~NTFSLOG_REUSE_TAIL;
			log->next_page = nextpage_off;
		}

		/* Remember if we wrapped the log file. */
		if (wrapped_file)
			log->l_flags |= NTFSLOG_WRAPPED;
	}

	/*
	 * Remember the last page count and position.
	 * Also remember the last known lsn.
	 */
	page_cnt = le16_to_cpu(page->page_count);
	page_pos = le16_to_cpu(page->page_pos);
	last_ok_lsn = le64_to_cpu(page->rhdr.lsn);

next_page_1:

	if (wrapped) {
		expected_seq += 1;
		wrapped_file = 1;
	}

	curpage_off = nextpage_off;
	kfree(page);
	page = NULL;
	reuse_page = 0;
	goto next_page;

check_tail:
	if (tail_page) {
		log->seq_num = expected_seq;
		log->last_lsn = le64_to_cpu(tail_page->record_hdr.last_end_lsn);
		log->ra->current_lsn = tail_page->record_hdr.last_end_lsn;
		log->l_flags &= ~NTFSLOG_NO_LAST_LSN;

		if (log->page_size -
			    le16_to_cpu(
				    tail_page->record_hdr.next_record_off) >=
		    log->record_header_len) {
			log->l_flags |= NTFSLOG_REUSE_TAIL;
			log->next_page = curpage_off;
		} else {
			log->l_flags &= ~NTFSLOG_REUSE_TAIL;
			log->next_page = nextpage_off;
		}

		if (wrapped)
			log->l_flags |= NTFSLOG_WRAPPED;
	}

	/* Remember that the partial IO will start at the next page. */
	second_off = nextpage_off;

	/*
	 * If the next page is the first page of the file then update
	 * the sequence number for log records which begon the next page.
	 */
	if (wrapped)
		expected_seq += 1;

	/*
	 * If we have a tail copy or are performing single page I/O we can
	 * immediately look at the next page.
	 */
	if (replace_page || (log->ra->flags & RESTART_SINGLE_PAGE_IO)) {
		page_cnt = 2;
		page_pos = 1;
		goto check_valid;
	}

	if (page_pos != page_cnt)
		goto check_valid;
	/*
	 * If the next page causes us to wrap to the beginning of the log
	 * file then we know which page to check next.
	 */
	if (wrapped) {
		page_cnt = 2;
		page_pos = 1;
		goto check_valid;
	}

	cur_pos = 2;

next_test_page:
	kfree(tst_page);
	tst_page = NULL;

	/* Walk through the file, reading log pages. */
	err = read_log_page(log, nextpage_off, &tst_page, &usa_error);

	/*
	 * If we get a USA error then assume that we correctly found
	 * the end of the original transfer.
	 */
	if (usa_error)
		goto file_is_valid;

	/*
	 * If we were able to read the page, we examine it to see if it
	 * is the same or different Io block.
	 */
	if (err)
		goto next_test_page_1;

	if (le16_to_cpu(tst_page->page_pos) == cur_pos &&
	    check_subseq_log_page(log, tst_page, nextpage_off, expected_seq)) {
		page_cnt = le16_to_cpu(tst_page->page_count) + 1;
		page_pos = le16_to_cpu(tst_page->page_pos);
		goto check_valid;
	} else {
		goto file_is_valid;
	}

next_test_page_1:

	nextpage_off = next_page_off(log, curpage_off);
	wrapped = nextpage_off == log->first_page;

	if (wrapped) {
		expected_seq += 1;
		page_cnt = 2;
		page_pos = 1;
	}

	cur_pos += 1;
	part_io_count += 1;
	if (!wrapped)
		goto next_test_page;

check_valid:
	/* Skip over the remaining pages this transfer. */
	remain_pages = page_cnt - page_pos - 1;
	part_io_count += remain_pages;

	while (remain_pages--) {
		nextpage_off = next_page_off(log, curpage_off);
		wrapped = nextpage_off == log->first_page;

		if (wrapped)
			expected_seq += 1;
	}

	/* Call our routine to check this log page. */
	kfree(tst_page);
	tst_page = NULL;

	err = read_log_page(log, nextpage_off, &tst_page, &usa_error);
	if (!err && !usa_error &&
	    check_subseq_log_page(log, tst_page, nextpage_off, expected_seq)) {
		err = -EINVAL;
		goto out;
	}

file_is_valid:

	/* We have a valid file. */
	if (page_off1 || tail_page) {
		struct RECORD_PAGE_HDR *tmp_page;

		if (sb_rdonly(log->ni->mi.sbi->sb)) {
			err = -EROFS;
			goto out;
		}

		if (page_off1) {
			tmp_page = Add2Ptr(page_bufs, page_off1 - page_off);
			tails -= (page_off1 - page_off) / log->page_size;
			if (!tail_page)
				tails -= 1;
		} else {
			tmp_page = tail_page;
			tails = 1;
		}

		while (tails--) {
			u64 off = hdr_file_off(log, tmp_page);

			if (!page) {
				page = kmalloc(log->page_size, GFP_NOFS);
				if (!page)
					return -ENOMEM;
			}

			/*
			 * Correct page and copy the data from this page
			 * into it and flush it to disk.
			 */
			memcpy(page, tmp_page, log->page_size);

			/* Fill last flushed lsn value flush the page. */
			if (log->major_ver < 2)
				page->rhdr.lsn = page->record_hdr.last_end_lsn;
			else
				page->file_off = 0;

			page->page_pos = page->page_count = cpu_to_le16(1);

			ntfs_fix_pre_write(&page->rhdr, log->page_size);

			err = ntfs_sb_write_run(log->ni->mi.sbi,
						&log->ni->file.run, off, page,
						log->page_size, 0);

			if (err)
				goto out;

			if (part_io_count && second_off == off) {
				second_off += log->page_size;
				part_io_count -= 1;
			}

			tmp_page = Add2Ptr(tmp_page, log->page_size);
		}
	}

	if (part_io_count) {
		if (sb_rdonly(log->ni->mi.sbi->sb)) {
			err = -EROFS;
			goto out;
		}
	}

out:
	kfree(second_tail);
	kfree(first_tail);
	kfree(page);
	kfree(tst_page);
	kfree(page_bufs);

	return err;
}

/*
 * read_log_rec_buf - Copy a log record from the file to a buffer.
 *
 * The log record may span several log pages and may even wrap the file.
 */
static int read_log_rec_buf(struct ntfs_log *log,
			    const struct LFS_RECORD_HDR *rh, void *buffer)
{
	int err;
	struct RECORD_PAGE_HDR *ph = NULL;
	u64 lsn = le64_to_cpu(rh->this_lsn);
	u32 vbo = lsn_to_vbo(log, lsn) & ~log->page_mask;
	u32 off = lsn_to_page_off(log, lsn) + log->record_header_len;
	u32 data_len = le32_to_cpu(rh->client_data_len);

	/*
	 * While there are more bytes to transfer,
	 * we continue to attempt to perform the read.
	 */
	for (;;) {
		bool usa_error;
		u32 tail = log->page_size - off;

		if (tail >= data_len)
			tail = data_len;

		data_len -= tail;

		err = read_log_page(log, vbo, &ph, &usa_error);
		if (err)
			goto out;

		/*
		 * The last lsn on this page better be greater or equal
		 * to the lsn we are copying.
		 */
		if (lsn > le64_to_cpu(ph->rhdr.lsn)) {
			err = -EINVAL;
			goto out;
		}

		memcpy(buffer, Add2Ptr(ph, off), tail);

		/* If there are no more bytes to transfer, we exit the loop. */
		if (!data_len) {
			if (!is_log_record_end(ph) ||
			    lsn > le64_to_cpu(ph->record_hdr.last_end_lsn)) {
				err = -EINVAL;
				goto out;
			}
			break;
		}

		if (ph->rhdr.lsn == ph->record_hdr.last_end_lsn ||
		    lsn > le64_to_cpu(ph->rhdr.lsn)) {
			err = -EINVAL;
			goto out;
		}

		vbo = next_page_off(log, vbo);
		off = log->data_off;

		/*
		 * Adjust our pointer the user's buffer to transfer
		 * the next block to.
		 */
		buffer = Add2Ptr(buffer, tail);
	}

out:
	kfree(ph);
	return err;
}

static int read_rst_area(struct ntfs_log *log, struct NTFS_RESTART **rst_,
			 u64 *lsn)
{
	int err;
	struct LFS_RECORD_HDR *rh = NULL;
	const struct CLIENT_REC *cr =
		Add2Ptr(log->ra, le16_to_cpu(log->ra->client_off));
	u64 lsnr, lsnc = le64_to_cpu(cr->restart_lsn);
	u32 len;
	struct NTFS_RESTART *rst;

	*lsn = 0;
	*rst_ = NULL;

	/* If the client doesn't have a restart area, go ahead and exit now. */
	if (!lsnc)
		return 0;

	err = read_log_page(log, lsn_to_vbo(log, lsnc),
			    (struct RECORD_PAGE_HDR **)&rh, NULL);
	if (err)
		return err;

	rst = NULL;
	lsnr = le64_to_cpu(rh->this_lsn);

	if (lsnc != lsnr) {
		/* If the lsn values don't match, then the disk is corrupt. */
		err = -EINVAL;
		goto out;
	}

	*lsn = lsnr;
	len = le32_to_cpu(rh->client_data_len);

	if (!len) {
		err = 0;
		goto out;
	}

	if (len < sizeof(struct NTFS_RESTART)) {
		err = -EINVAL;
		goto out;
	}

	rst = kmalloc(len, GFP_NOFS);
	if (!rst) {
		err = -ENOMEM;
		goto out;
	}

	/* Copy the data into the 'rst' buffer. */
	err = read_log_rec_buf(log, rh, rst);
	if (err)
		goto out;

	*rst_ = rst;
	rst = NULL;

out:
	kfree(rh);
	kfree(rst);

	return err;
}

static int find_log_rec(struct ntfs_log *log, u64 lsn, struct lcb *lcb)
{
	int err;
	struct LFS_RECORD_HDR *rh = lcb->lrh;
	u32 rec_len, len;

	/* Read the record header for this lsn. */
	if (!rh) {
		err = read_log_page(log, lsn_to_vbo(log, lsn),
				    (struct RECORD_PAGE_HDR **)&rh, NULL);

		lcb->lrh = rh;
		if (err)
			return err;
	}

	/*
	 * If the lsn the log record doesn't match the desired
	 * lsn then the disk is corrupt.
	 */
	if (lsn != le64_to_cpu(rh->this_lsn))
		return -EINVAL;

	len = le32_to_cpu(rh->client_data_len);

	/*
	 * Check that the length field isn't greater than the total
	 * available space the log file.
	 */
	rec_len = len + log->record_header_len;
	if (rec_len >= log->total_avail)
		return -EINVAL;

	/*
	 * If the entire log record is on this log page,
	 * put a pointer to the log record the context block.
	 */
	if (rh->flags & LOG_RECORD_MULTI_PAGE) {
		void *lr = kmalloc(len, GFP_NOFS);

		if (!lr)
			return -ENOMEM;

		lcb->log_rec = lr;
		lcb->alloc = true;

		/* Copy the data into the buffer returned. */
		err = read_log_rec_buf(log, rh, lr);
		if (err)
			return err;
	} else {
		/* If beyond the end of the current page -> an error. */
		u32 page_off = lsn_to_page_off(log, lsn);

		if (page_off + len + log->record_header_len > log->page_size)
			return -EINVAL;

		lcb->log_rec = Add2Ptr(rh, sizeof(struct LFS_RECORD_HDR));
		lcb->alloc = false;
	}

	return 0;
}

/*
 * read_log_rec_lcb - Init the query operation.
 */
static int read_log_rec_lcb(struct ntfs_log *log, u64 lsn, u32 ctx_mode,
			    struct lcb **lcb_)
{
	int err;
	const struct CLIENT_REC *cr;
	struct lcb *lcb;

	switch (ctx_mode) {
	case lcb_ctx_undo_next:
	case lcb_ctx_prev:
	case lcb_ctx_next:
		break;
	default:
		return -EINVAL;
	}

	/* Check that the given lsn is the legal range for this client. */
	cr = Add2Ptr(log->ra, le16_to_cpu(log->ra->client_off));

	if (!verify_client_lsn(log, cr, lsn))
		return -EINVAL;

	lcb = kzalloc(sizeof(struct lcb), GFP_NOFS);
	if (!lcb)
		return -ENOMEM;
	lcb->client = log->client_id;
	lcb->ctx_mode = ctx_mode;

	/* Find the log record indicated by the given lsn. */
	err = find_log_rec(log, lsn, lcb);
	if (err)
		goto out;

	*lcb_ = lcb;
	return 0;

out:
	lcb_put(lcb);
	*lcb_ = NULL;
	return err;
}

/*
 * find_client_next_lsn
 *
 * Attempt to find the next lsn to return to a client based on the context mode.
 */
static int find_client_next_lsn(struct ntfs_log *log, struct lcb *lcb, u64 *lsn)
{
	int err;
	u64 next_lsn;
	struct LFS_RECORD_HDR *hdr;

	hdr = lcb->lrh;
	*lsn = 0;

	if (lcb_ctx_next != lcb->ctx_mode)
		goto check_undo_next;

	/* Loop as long as another lsn can be found. */
	for (;;) {
		u64 current_lsn;

		err = next_log_lsn(log, hdr, &current_lsn);
		if (err)
			goto out;

		if (!current_lsn)
			break;

		if (hdr != lcb->lrh)
			kfree(hdr);

		hdr = NULL;
		err = read_log_page(log, lsn_to_vbo(log, current_lsn),
				    (struct RECORD_PAGE_HDR **)&hdr, NULL);
		if (err)
			goto out;

		if (memcmp(&hdr->client, &lcb->client,
			   sizeof(struct CLIENT_ID))) {
			/*err = -EINVAL; */
		} else if (LfsClientRecord == hdr->record_type) {
			kfree(lcb->lrh);
			lcb->lrh = hdr;
			*lsn = current_lsn;
			return 0;
		}
	}

out:
	if (hdr != lcb->lrh)
		kfree(hdr);
	return err;

check_undo_next:
	if (lcb_ctx_undo_next == lcb->ctx_mode)
		next_lsn = le64_to_cpu(hdr->client_undo_next_lsn);
	else if (lcb_ctx_prev == lcb->ctx_mode)
		next_lsn = le64_to_cpu(hdr->client_prev_lsn);
	else
		return 0;

	if (!next_lsn)
		return 0;

	if (!verify_client_lsn(
		    log, Add2Ptr(log->ra, le16_to_cpu(log->ra->client_off)),
		    next_lsn))
		return 0;

	hdr = NULL;
	err = read_log_page(log, lsn_to_vbo(log, next_lsn),
			    (struct RECORD_PAGE_HDR **)&hdr, NULL);
	if (err)
		return err;
	kfree(lcb->lrh);
	lcb->lrh = hdr;

	*lsn = next_lsn;

	return 0;
}

static int read_next_log_rec(struct ntfs_log *log, struct lcb *lcb, u64 *lsn)
{
	int err;

	err = find_client_next_lsn(log, lcb, lsn);
	if (err)
		return err;

	if (!*lsn)
		return 0;

	if (lcb->alloc)
		kfree(lcb->log_rec);

	lcb->log_rec = NULL;
	lcb->alloc = false;
	kfree(lcb->lrh);
	lcb->lrh = NULL;

	return find_log_rec(log, *lsn, lcb);
}

bool check_index_header(const struct INDEX_HDR *hdr, size_t bytes)
{
	__le16 mask;
	u32 min_de, de_off, used, total;
	const struct NTFS_DE *e;

	if (hdr_has_subnode(hdr)) {
		min_de = sizeof(struct NTFS_DE) + sizeof(u64);
		mask = NTFS_IE_HAS_SUBNODES;
	} else {
		min_de = sizeof(struct NTFS_DE);
		mask = 0;
	}

	de_off = le32_to_cpu(hdr->de_off);
	used = le32_to_cpu(hdr->used);
	total = le32_to_cpu(hdr->total);

	if (de_off > bytes - min_de || used > bytes || total > bytes ||
	    de_off + min_de > used || used > total) {
		return false;
	}

	e = Add2Ptr(hdr, de_off);
	for (;;) {
		u16 esize = le16_to_cpu(e->size);
		struct NTFS_DE *next = Add2Ptr(e, esize);

		if (esize < min_de || PtrOffset(hdr, next) > used ||
		    (e->flags & NTFS_IE_HAS_SUBNODES) != mask) {
			return false;
		}

		if (de_is_last(e))
			break;

		e = next;
	}

	return true;
}

static inline bool check_index_buffer(const struct INDEX_BUFFER *ib, u32 bytes)
{
	u16 fo;
	const struct NTFS_RECORD_HEADER *r = &ib->rhdr;

	if (r->sign != NTFS_INDX_SIGNATURE)
		return false;

	fo = (SECTOR_SIZE - ((bytes >> SECTOR_SHIFT) + 1) * sizeof(short));

	if (le16_to_cpu(r->fix_off) > fo)
		return false;

	if ((le16_to_cpu(r->fix_num) - 1) * SECTOR_SIZE != bytes)
		return false;

	return check_index_header(&ib->ihdr,
				  bytes - offsetof(struct INDEX_BUFFER, ihdr));
}

static inline bool check_index_root(const struct ATTRIB *attr,
				    struct ntfs_sb_info *sbi)
{
	bool ret;
	const struct INDEX_ROOT *root = resident_data(attr);
	u8 index_bits = le32_to_cpu(root->index_block_size) >= sbi->cluster_size
				? sbi->cluster_bits
				: SECTOR_SHIFT;
	u8 block_clst = root->index_block_clst;

	if (le32_to_cpu(attr->res.data_size) < sizeof(struct INDEX_ROOT) ||
	    (root->type != ATTR_NAME && root->type != ATTR_ZERO) ||
	    (root->type == ATTR_NAME &&
	     root->rule != NTFS_COLLATION_TYPE_FILENAME) ||
	    (le32_to_cpu(root->index_block_size) !=
	     (block_clst << index_bits)) ||
	    (block_clst != 1 && block_clst != 2 && block_clst != 4 &&
	     block_clst != 8 && block_clst != 0x10 && block_clst != 0x20 &&
	     block_clst != 0x40 && block_clst != 0x80)) {
		return false;
	}

	ret = check_index_header(&root->ihdr,
				 le32_to_cpu(attr->res.data_size) -
					 offsetof(struct INDEX_ROOT, ihdr));
	return ret;
}

static inline bool check_attr(const struct MFT_REC *rec,
			      const struct ATTRIB *attr,
			      struct ntfs_sb_info *sbi)
{
	u32 asize = le32_to_cpu(attr->size);
	u32 rsize = 0;
	u64 dsize, svcn, evcn;
	u16 run_off;

	/* Check the fixed part of the attribute record header. */
	if (asize >= sbi->record_size ||
	    asize + PtrOffset(rec, attr) >= sbi->record_size ||
	    (attr->name_len &&
	     le16_to_cpu(attr->name_off) + attr->name_len * sizeof(short) >
		     asize)) {
		return false;
	}

	/* Check the attribute fields. */
	switch (attr->non_res) {
	case 0:
		rsize = le32_to_cpu(attr->res.data_size);
		if (rsize >= asize ||
		    le16_to_cpu(attr->res.data_off) + rsize > asize) {
			return false;
		}
		break;

	case 1:
		dsize = le64_to_cpu(attr->nres.data_size);
		svcn = le64_to_cpu(attr->nres.svcn);
		evcn = le64_to_cpu(attr->nres.evcn);
		run_off = le16_to_cpu(attr->nres.run_off);

		if (svcn > evcn + 1 || run_off >= asize ||
		    le64_to_cpu(attr->nres.valid_size) > dsize ||
		    dsize > le64_to_cpu(attr->nres.alloc_size)) {
			return false;
		}

		if (run_off > asize)
			return false;

		if (run_unpack(NULL, sbi, 0, svcn, evcn, svcn,
			       Add2Ptr(attr, run_off), asize - run_off) < 0) {
			return false;
		}

		return true;

	default:
		return false;
	}

	switch (attr->type) {
	case ATTR_NAME:
		if (fname_full_size(Add2Ptr(
			    attr, le16_to_cpu(attr->res.data_off))) > asize) {
			return false;
		}
		break;

	case ATTR_ROOT:
		return check_index_root(attr, sbi);

	case ATTR_STD:
		if (rsize < sizeof(struct ATTR_STD_INFO5) &&
		    rsize != sizeof(struct ATTR_STD_INFO)) {
			return false;
		}
		break;

	case ATTR_LIST:
	case ATTR_ID:
	case ATTR_SECURE:
	case ATTR_LABEL:
	case ATTR_VOL_INFO:
	case ATTR_DATA:
	case ATTR_ALLOC:
	case ATTR_BITMAP:
	case ATTR_REPARSE:
	case ATTR_EA_INFO:
	case ATTR_EA:
	case ATTR_PROPERTYSET:
	case ATTR_LOGGED_UTILITY_STREAM:
		break;

	default:
		return false;
	}

	return true;
}

static inline bool check_file_record(const struct MFT_REC *rec,
				     const struct MFT_REC *rec2,
				     struct ntfs_sb_info *sbi)
{
	const struct ATTRIB *attr;
	u16 fo = le16_to_cpu(rec->rhdr.fix_off);
	u16 fn = le16_to_cpu(rec->rhdr.fix_num);
	u16 ao = le16_to_cpu(rec->attr_off);
	u32 rs = sbi->record_size;

	/* Check the file record header for consistency. */
	if (rec->rhdr.sign != NTFS_FILE_SIGNATURE ||
	    fo > (SECTOR_SIZE - ((rs >> SECTOR_SHIFT) + 1) * sizeof(short)) ||
	    (fn - 1) * SECTOR_SIZE != rs || ao < MFTRECORD_FIXUP_OFFSET_1 ||
	    ao > sbi->record_size - SIZEOF_RESIDENT || !is_rec_inuse(rec) ||
	    le32_to_cpu(rec->total) != rs) {
		return false;
	}

	/* Loop to check all of the attributes. */
	for (attr = Add2Ptr(rec, ao); attr->type != ATTR_END;
	     attr = Add2Ptr(attr, le32_to_cpu(attr->size))) {
		if (check_attr(rec, attr, sbi))
			continue;
		return false;
	}

	return true;
}

static inline int check_lsn(const struct NTFS_RECORD_HEADER *hdr,
			    const u64 *rlsn)
{
	u64 lsn;

	if (!rlsn)
		return true;

	lsn = le64_to_cpu(hdr->lsn);

	if (hdr->sign == NTFS_HOLE_SIGNATURE)
		return false;

	if (*rlsn > lsn)
		return true;

	return false;
}

static inline bool check_if_attr(const struct MFT_REC *rec,
				 const struct LOG_REC_HDR *lrh)
{
	u16 ro = le16_to_cpu(lrh->record_off);
	u16 o = le16_to_cpu(rec->attr_off);
	const struct ATTRIB *attr = Add2Ptr(rec, o);

	while (o < ro) {
		u32 asize;

		if (attr->type == ATTR_END)
			break;

		asize = le32_to_cpu(attr->size);
		if (!asize)
			break;

		o += asize;
		attr = Add2Ptr(attr, asize);
	}

	return o == ro;
}

static inline bool check_if_index_root(const struct MFT_REC *rec,
				       const struct LOG_REC_HDR *lrh)
{
	u16 ro = le16_to_cpu(lrh->record_off);
	u16 o = le16_to_cpu(rec->attr_off);
	const struct ATTRIB *attr = Add2Ptr(rec, o);

	while (o < ro) {
		u32 asize;

		if (attr->type == ATTR_END)
			break;

		asize = le32_to_cpu(attr->size);
		if (!asize)
			break;

		o += asize;
		attr = Add2Ptr(attr, asize);
	}

	return o == ro && attr->type == ATTR_ROOT;
}

static inline bool check_if_root_index(const struct ATTRIB *attr,
				       const struct INDEX_HDR *hdr,
				       const struct LOG_REC_HDR *lrh)
{
	u16 ao = le16_to_cpu(lrh->attr_off);
	u32 de_off = le32_to_cpu(hdr->de_off);
	u32 o = PtrOffset(attr, hdr) + de_off;
	const struct NTFS_DE *e = Add2Ptr(hdr, de_off);
	u32 asize = le32_to_cpu(attr->size);

	while (o < ao) {
		u16 esize;

		if (o >= asize)
			break;

		esize = le16_to_cpu(e->size);
		if (!esize)
			break;

		o += esize;
		e = Add2Ptr(e, esize);
	}

	return o == ao;
}

static inline bool check_if_alloc_index(const struct INDEX_HDR *hdr,
					u32 attr_off)
{
	u32 de_off = le32_to_cpu(hdr->de_off);
	u32 o = offsetof(struct INDEX_BUFFER, ihdr) + de_off;
	const struct NTFS_DE *e = Add2Ptr(hdr, de_off);
	u32 used = le32_to_cpu(hdr->used);

	while (o < attr_off) {
		u16 esize;

		if (de_off >= used)
			break;

		esize = le16_to_cpu(e->size);
		if (!esize)
			break;

		o += esize;
		de_off += esize;
		e = Add2Ptr(e, esize);
	}

	return o == attr_off;
}

static inline void change_attr_size(struct MFT_REC *rec, struct ATTRIB *attr,
				    u32 nsize)
{
	u32 asize = le32_to_cpu(attr->size);
	int dsize = nsize - asize;
	u8 *next = Add2Ptr(attr, asize);
	u32 used = le32_to_cpu(rec->used);

	memmove(Add2Ptr(attr, nsize), next, used - PtrOffset(rec, next));

	rec->used = cpu_to_le32(used + dsize);
	attr->size = cpu_to_le32(nsize);
}

struct OpenAttr {
	struct ATTRIB *attr;
	struct runs_tree *run1;
	struct runs_tree run0;
	struct ntfs_inode *ni;
	// CLST rno;
};

/*
 * cmp_type_and_name
 *
 * Return: 0 if 'attr' has the same type and name.
 */
static inline int cmp_type_and_name(const struct ATTRIB *a1,
				    const struct ATTRIB *a2)
{
	return a1->type != a2->type || a1->name_len != a2->name_len ||
	       (a1->name_len && memcmp(attr_name(a1), attr_name(a2),
				       a1->name_len * sizeof(short)));
}

static struct OpenAttr *find_loaded_attr(struct ntfs_log *log,
					 const struct ATTRIB *attr, CLST rno)
{
	struct OPEN_ATTR_ENRTY *oe = NULL;

	while ((oe = enum_rstbl(log->open_attr_tbl, oe))) {
		struct OpenAttr *op_attr;

		if (ino_get(&oe->ref) != rno)
			continue;

		op_attr = (struct OpenAttr *)oe->ptr;
		if (!cmp_type_and_name(op_attr->attr, attr))
			return op_attr;
	}
	return NULL;
}

static struct ATTRIB *attr_create_nonres_log(struct ntfs_sb_info *sbi,
					     enum ATTR_TYPE type, u64 size,
					     const u16 *name, size_t name_len,
					     __le16 flags)
{
	struct ATTRIB *attr;
	u32 name_size = ALIGN(name_len * sizeof(short), 8);
	bool is_ext = flags & (ATTR_FLAG_COMPRESSED | ATTR_FLAG_SPARSED);
	u32 asize = name_size +
		    (is_ext ? SIZEOF_NONRESIDENT_EX : SIZEOF_NONRESIDENT);

	attr = kzalloc(asize, GFP_NOFS);
	if (!attr)
		return NULL;

	attr->type = type;
	attr->size = cpu_to_le32(asize);
	attr->flags = flags;
	attr->non_res = 1;
	attr->name_len = name_len;

	attr->nres.evcn = cpu_to_le64((u64)bytes_to_cluster(sbi, size) - 1);
	attr->nres.alloc_size = cpu_to_le64(ntfs_up_cluster(sbi, size));
	attr->nres.data_size = cpu_to_le64(size);
	attr->nres.valid_size = attr->nres.data_size;
	if (is_ext) {
		attr->name_off = SIZEOF_NONRESIDENT_EX_LE;
		if (is_attr_compressed(attr))
			attr->nres.c_unit = COMPRESSION_UNIT;

		attr->nres.run_off =
			cpu_to_le16(SIZEOF_NONRESIDENT_EX + name_size);
		memcpy(Add2Ptr(attr, SIZEOF_NONRESIDENT_EX), name,
		       name_len * sizeof(short));
	} else {
		attr->name_off = SIZEOF_NONRESIDENT_LE;
		attr->nres.run_off =
			cpu_to_le16(SIZEOF_NONRESIDENT + name_size);
		memcpy(Add2Ptr(attr, SIZEOF_NONRESIDENT), name,
		       name_len * sizeof(short));
	}

	return attr;
}

/*
 * do_action - Common routine for the Redo and Undo Passes.
 * @rlsn: If it is NULL then undo.
 */
static int do_action(struct ntfs_log *log, struct OPEN_ATTR_ENRTY *oe,
		     const struct LOG_REC_HDR *lrh, u32 op, void *data,
		     u32 dlen, u32 rec_len, const u64 *rlsn)
{
	int err = 0;
	struct ntfs_sb_info *sbi = log->ni->mi.sbi;
	struct inode *inode = NULL, *inode_parent;
	struct mft_inode *mi = NULL, *mi2_child = NULL;
	CLST rno = 0, rno_base = 0;
	struct INDEX_BUFFER *ib = NULL;
	struct MFT_REC *rec = NULL;
	struct ATTRIB *attr = NULL, *attr2;
	struct INDEX_HDR *hdr;
	struct INDEX_ROOT *root;
	struct NTFS_DE *e, *e1, *e2;
	struct NEW_ATTRIBUTE_SIZES *new_sz;
	struct ATTR_FILE_NAME *fname;
	struct OpenAttr *oa, *oa2;
	u32 nsize, t32, asize, used, esize, off, bits;
	u16 id, id2;
	u32 record_size = sbi->record_size;
	u64 t64;
	u16 roff = le16_to_cpu(lrh->record_off);
	u16 aoff = le16_to_cpu(lrh->attr_off);
	u64 lco = 0;
	u64 cbo = (u64)le16_to_cpu(lrh->cluster_off) << SECTOR_SHIFT;
	u64 tvo = le64_to_cpu(lrh->target_vcn) << sbi->cluster_bits;
	u64 vbo = cbo + tvo;
	void *buffer_le = NULL;
	u32 bytes = 0;
	bool a_dirty = false;
	u16 data_off;

	oa = oe->ptr;

	/* Big switch to prepare. */
	switch (op) {
	/* ============================================================
	 * Process MFT records, as described by the current log record.
	 * ============================================================
	 */
	case InitializeFileRecordSegment:
	case DeallocateFileRecordSegment:
	case WriteEndOfFileRecordSegment:
	case CreateAttribute:
	case DeleteAttribute:
	case UpdateResidentValue:
	case UpdateMappingPairs:
	case SetNewAttributeSizes:
	case AddIndexEntryRoot:
	case DeleteIndexEntryRoot:
	case SetIndexEntryVcnRoot:
	case UpdateFileNameRoot:
	case UpdateRecordDataRoot:
	case ZeroEndOfFileRecord:
		rno = vbo >> sbi->record_bits;
		inode = ilookup(sbi->sb, rno);
		if (inode) {
			mi = &ntfs_i(inode)->mi;
		} else if (op == InitializeFileRecordSegment) {
			mi = kzalloc(sizeof(struct mft_inode), GFP_NOFS);
			if (!mi)
				return -ENOMEM;
			err = mi_format_new(mi, sbi, rno, 0, false);
			if (err)
				goto out;
		} else {
			/* Read from disk. */
			err = mi_get(sbi, rno, &mi);
			if (err)
				return err;
		}
		rec = mi->mrec;

		if (op == DeallocateFileRecordSegment)
			goto skip_load_parent;

		if (InitializeFileRecordSegment != op) {
			if (rec->rhdr.sign == NTFS_BAAD_SIGNATURE)
				goto dirty_vol;
			if (!check_lsn(&rec->rhdr, rlsn))
				goto out;
			if (!check_file_record(rec, NULL, sbi))
				goto dirty_vol;
			attr = Add2Ptr(rec, roff);
		}

		if (is_rec_base(rec) || InitializeFileRecordSegment == op) {
			rno_base = rno;
			goto skip_load_parent;
		}

		rno_base = ino_get(&rec->parent_ref);
		inode_parent = ntfs_iget5(sbi->sb, &rec->parent_ref, NULL);
		if (IS_ERR(inode_parent))
			goto skip_load_parent;

		if (is_bad_inode(inode_parent)) {
			iput(inode_parent);
			goto skip_load_parent;
		}

		if (ni_load_mi_ex(ntfs_i(inode_parent), rno, &mi2_child)) {
			iput(inode_parent);
		} else {
			if (mi2_child->mrec != mi->mrec)
				memcpy(mi2_child->mrec, mi->mrec,
				       sbi->record_size);

			if (inode)
				iput(inode);
			else if (mi)
				mi_put(mi);

			inode = inode_parent;
			mi = mi2_child;
			rec = mi2_child->mrec;
			attr = Add2Ptr(rec, roff);
		}

skip_load_parent:
		inode_parent = NULL;
		break;

	/*
	 * Process attributes, as described by the current log record.
	 */
	case UpdateNonresidentValue:
	case AddIndexEntryAllocation:
	case DeleteIndexEntryAllocation:
	case WriteEndOfIndexBuffer:
	case SetIndexEntryVcnAllocation:
	case UpdateFileNameAllocation:
	case SetBitsInNonresidentBitMap:
	case ClearBitsInNonresidentBitMap:
	case UpdateRecordDataAllocation:
		attr = oa->attr;
		bytes = UpdateNonresidentValue == op ? dlen : 0;
		lco = (u64)le16_to_cpu(lrh->lcns_follow) << sbi->cluster_bits;

		if (attr->type == ATTR_ALLOC) {
			t32 = le32_to_cpu(oe->bytes_per_index);
			if (bytes < t32)
				bytes = t32;
		}

		if (!bytes)
			bytes = lco - cbo;

		bytes += roff;
		if (attr->type == ATTR_ALLOC)
			bytes = (bytes + 511) & ~511; // align

		buffer_le = kmalloc(bytes, GFP_NOFS);
		if (!buffer_le)
			return -ENOMEM;

		err = ntfs_read_run_nb(sbi, oa->run1, vbo, buffer_le, bytes,
				       NULL);
		if (err)
			goto out;

		if (attr->type == ATTR_ALLOC && *(int *)buffer_le)
			ntfs_fix_post_read(buffer_le, bytes, false);
		break;

	default:
		WARN_ON(1);
	}

	/* Big switch to do operation. */
	switch (op) {
	case InitializeFileRecordSegment:
		if (roff + dlen > record_size)
			goto dirty_vol;

		memcpy(Add2Ptr(rec, roff), data, dlen);
		mi->dirty = true;
		break;

	case DeallocateFileRecordSegment:
		clear_rec_inuse(rec);
		le16_add_cpu(&rec->seq, 1);
		mi->dirty = true;
		break;

	case WriteEndOfFileRecordSegment:
		attr2 = (struct ATTRIB *)data;
		if (!check_if_attr(rec, lrh) || roff + dlen > record_size)
			goto dirty_vol;

		memmove(attr, attr2, dlen);
		rec->used = cpu_to_le32(ALIGN(roff + dlen, 8));

		mi->dirty = true;
		break;

	case CreateAttribute:
		attr2 = (struct ATTRIB *)data;
		asize = le32_to_cpu(attr2->size);
		used = le32_to_cpu(rec->used);

		if (!check_if_attr(rec, lrh) || dlen < SIZEOF_RESIDENT ||
		    !IS_ALIGNED(asize, 8) ||
		    Add2Ptr(attr2, asize) > Add2Ptr(lrh, rec_len) ||
		    dlen > record_size - used) {
			goto dirty_vol;
		}

		memmove(Add2Ptr(attr, asize), attr, used - roff);
		memcpy(attr, attr2, asize);

		rec->used = cpu_to_le32(used + asize);
		id = le16_to_cpu(rec->next_attr_id);
		id2 = le16_to_cpu(attr2->id);
		if (id <= id2)
			rec->next_attr_id = cpu_to_le16(id2 + 1);
		if (is_attr_indexed(attr))
			le16_add_cpu(&rec->hard_links, 1);

		oa2 = find_loaded_attr(log, attr, rno_base);
		if (oa2) {
			void *p2 = kmemdup(attr, le32_to_cpu(attr->size),
					   GFP_NOFS);
			if (p2) {
				// run_close(oa2->run1);
				kfree(oa2->attr);
				oa2->attr = p2;
			}
		}

		mi->dirty = true;
		break;

	case DeleteAttribute:
		asize = le32_to_cpu(attr->size);
		used = le32_to_cpu(rec->used);

		if (!check_if_attr(rec, lrh))
			goto dirty_vol;

		rec->used = cpu_to_le32(used - asize);
		if (is_attr_indexed(attr))
			le16_add_cpu(&rec->hard_links, -1);

		memmove(attr, Add2Ptr(attr, asize), used - asize - roff);

		mi->dirty = true;
		break;

	case UpdateResidentValue:
		nsize = aoff + dlen;

		if (!check_if_attr(rec, lrh))
			goto dirty_vol;

		asize = le32_to_cpu(attr->size);
		used = le32_to_cpu(rec->used);

		if (lrh->redo_len == lrh->undo_len) {
			if (nsize > asize)
				goto dirty_vol;
			goto move_data;
		}

		if (nsize > asize && nsize - asize > record_size - used)
			goto dirty_vol;

		nsize = ALIGN(nsize, 8);
		data_off = le16_to_cpu(attr->res.data_off);

		if (nsize < asize) {
			memmove(Add2Ptr(attr, aoff), data, dlen);
			data = NULL; // To skip below memmove().
		}

		memmove(Add2Ptr(attr, nsize), Add2Ptr(attr, asize),
			used - le16_to_cpu(lrh->record_off) - asize);

		rec->used = cpu_to_le32(used + nsize - asize);
		attr->size = cpu_to_le32(nsize);
		attr->res.data_size = cpu_to_le32(aoff + dlen - data_off);

move_data:
		if (data)
			memmove(Add2Ptr(attr, aoff), data, dlen);

		oa2 = find_loaded_attr(log, attr, rno_base);
		if (oa2) {
			void *p2 = kmemdup(attr, le32_to_cpu(attr->size),
					   GFP_NOFS);
			if (p2) {
				// run_close(&oa2->run0);
				oa2->run1 = &oa2->run0;
				kfree(oa2->attr);
				oa2->attr = p2;
			}
		}

		mi->dirty = true;
		break;

	case UpdateMappingPairs:
		nsize = aoff + dlen;
		asize = le32_to_cpu(attr->size);
		used = le32_to_cpu(rec->used);

		if (!check_if_attr(rec, lrh) || !attr->non_res ||
		    aoff < le16_to_cpu(attr->nres.run_off) || aoff > asize ||
		    (nsize > asize && nsize - asize > record_size - used)) {
			goto dirty_vol;
		}

		nsize = ALIGN(nsize, 8);

		memmove(Add2Ptr(attr, nsize), Add2Ptr(attr, asize),
			used - le16_to_cpu(lrh->record_off) - asize);
		rec->used = cpu_to_le32(used + nsize - asize);
		attr->size = cpu_to_le32(nsize);
		memmove(Add2Ptr(attr, aoff), data, dlen);

		if (run_get_highest_vcn(le64_to_cpu(attr->nres.svcn),
					attr_run(attr), &t64)) {
			goto dirty_vol;
		}

		attr->nres.evcn = cpu_to_le64(t64);
		oa2 = find_loaded_attr(log, attr, rno_base);
		if (oa2 && oa2->attr->non_res)
			oa2->attr->nres.evcn = attr->nres.evcn;

		mi->dirty = true;
		break;

	case SetNewAttributeSizes:
		new_sz = data;
		if (!check_if_attr(rec, lrh) || !attr->non_res)
			goto dirty_vol;

		attr->nres.alloc_size = new_sz->alloc_size;
		attr->nres.data_size = new_sz->data_size;
		attr->nres.valid_size = new_sz->valid_size;

		if (dlen >= sizeof(struct NEW_ATTRIBUTE_SIZES))
			attr->nres.total_size = new_sz->total_size;

		oa2 = find_loaded_attr(log, attr, rno_base);
		if (oa2) {
			void *p2 = kmemdup(attr, le32_to_cpu(attr->size),
					   GFP_NOFS);
			if (p2) {
				kfree(oa2->attr);
				oa2->attr = p2;
			}
		}
		mi->dirty = true;
		break;

	case AddIndexEntryRoot:
		e = (struct NTFS_DE *)data;
		esize = le16_to_cpu(e->size);
		root = resident_data(attr);
		hdr = &root->ihdr;
		used = le32_to_cpu(hdr->used);

		if (!check_if_index_root(rec, lrh) ||
		    !check_if_root_index(attr, hdr, lrh) ||
		    Add2Ptr(data, esize) > Add2Ptr(lrh, rec_len) ||
		    esize > le32_to_cpu(rec->total) - le32_to_cpu(rec->used)) {
			goto dirty_vol;
		}

		e1 = Add2Ptr(attr, le16_to_cpu(lrh->attr_off));

		change_attr_size(rec, attr, le32_to_cpu(attr->size) + esize);

		memmove(Add2Ptr(e1, esize), e1,
			PtrOffset(e1, Add2Ptr(hdr, used)));
		memmove(e1, e, esize);

		le32_add_cpu(&attr->res.data_size, esize);
		hdr->used = cpu_to_le32(used + esize);
		le32_add_cpu(&hdr->total, esize);

		mi->dirty = true;
		break;

	case DeleteIndexEntryRoot:
		root = resident_data(attr);
		hdr = &root->ihdr;
		used = le32_to_cpu(hdr->used);

		if (!check_if_index_root(rec, lrh) ||
		    !check_if_root_index(attr, hdr, lrh)) {
			goto dirty_vol;
		}

		e1 = Add2Ptr(attr, le16_to_cpu(lrh->attr_off));
		esize = le16_to_cpu(e1->size);
		e2 = Add2Ptr(e1, esize);

		memmove(e1, e2, PtrOffset(e2, Add2Ptr(hdr, used)));

		le32_sub_cpu(&attr->res.data_size, esize);
		hdr->used = cpu_to_le32(used - esize);
		le32_sub_cpu(&hdr->total, esize);

		change_attr_size(rec, attr, le32_to_cpu(attr->size) - esize);

		mi->dirty = true;
		break;

	case SetIndexEntryVcnRoot:
		root = resident_data(attr);
		hdr = &root->ihdr;

		if (!check_if_index_root(rec, lrh) ||
		    !check_if_root_index(attr, hdr, lrh)) {
			goto dirty_vol;
		}

		e = Add2Ptr(attr, le16_to_cpu(lrh->attr_off));

		de_set_vbn_le(e, *(__le64 *)data);
		mi->dirty = true;
		break;

	case UpdateFileNameRoot:
		root = resident_data(attr);
		hdr = &root->ihdr;

		if (!check_if_index_root(rec, lrh) ||
		    !check_if_root_index(attr, hdr, lrh)) {
			goto dirty_vol;
		}

		e = Add2Ptr(attr, le16_to_cpu(lrh->attr_off));
		fname = (struct ATTR_FILE_NAME *)(e + 1);
		memmove(&fname->dup, data, sizeof(fname->dup)); //
		mi->dirty = true;
		break;

	case UpdateRecordDataRoot:
		root = resident_data(attr);
		hdr = &root->ihdr;

		if (!check_if_index_root(rec, lrh) ||
		    !check_if_root_index(attr, hdr, lrh)) {
			goto dirty_vol;
		}

		e = Add2Ptr(attr, le16_to_cpu(lrh->attr_off));

		memmove(Add2Ptr(e, le16_to_cpu(e->view.data_off)), data, dlen);

		mi->dirty = true;
		break;

	case ZeroEndOfFileRecord:
		if (roff + dlen > record_size)
			goto dirty_vol;

		memset(attr, 0, dlen);
		mi->dirty = true;
		break;

	case UpdateNonresidentValue:
		if (lco < cbo + roff + dlen)
			goto dirty_vol;

		memcpy(Add2Ptr(buffer_le, roff), data, dlen);

		a_dirty = true;
		if (attr->type == ATTR_ALLOC)
			ntfs_fix_pre_write(buffer_le, bytes);
		break;

	case AddIndexEntryAllocation:
		ib = Add2Ptr(buffer_le, roff);
		hdr = &ib->ihdr;
		e = data;
		esize = le16_to_cpu(e->size);
		e1 = Add2Ptr(ib, aoff);

		if (is_baad(&ib->rhdr))
			goto dirty_vol;
		if (!check_lsn(&ib->rhdr, rlsn))
			goto out;

		used = le32_to_cpu(hdr->used);

		if (!check_index_buffer(ib, bytes) ||
		    !check_if_alloc_index(hdr, aoff) ||
		    Add2Ptr(e, esize) > Add2Ptr(lrh, rec_len) ||
		    used + esize > le32_to_cpu(hdr->total)) {
			goto dirty_vol;
		}

		memmove(Add2Ptr(e1, esize), e1,
			PtrOffset(e1, Add2Ptr(hdr, used)));
		memcpy(e1, e, esize);

		hdr->used = cpu_to_le32(used + esize);

		a_dirty = true;

		ntfs_fix_pre_write(&ib->rhdr, bytes);
		break;

	case DeleteIndexEntryAllocation:
		ib = Add2Ptr(buffer_le, roff);
		hdr = &ib->ihdr;
		e = Add2Ptr(ib, aoff);
		esize = le16_to_cpu(e->size);

		if (is_baad(&ib->rhdr))
			goto dirty_vol;
		if (!check_lsn(&ib->rhdr, rlsn))
			goto out;

		if (!check_index_buffer(ib, bytes) ||
		    !check_if_alloc_index(hdr, aoff)) {
			goto dirty_vol;
		}

		e1 = Add2Ptr(e, esize);
		nsize = esize;
		used = le32_to_cpu(hdr->used);

		memmove(e, e1, PtrOffset(e1, Add2Ptr(hdr, used)));

		hdr->used = cpu_to_le32(used - nsize);

		a_dirty = true;

		ntfs_fix_pre_write(&ib->rhdr, bytes);
		break;

	case WriteEndOfIndexBuffer:
		ib = Add2Ptr(buffer_le, roff);
		hdr = &ib->ihdr;
		e = Add2Ptr(ib, aoff);

		if (is_baad(&ib->rhdr))
			goto dirty_vol;
		if (!check_lsn(&ib->rhdr, rlsn))
			goto out;
		if (!check_index_buffer(ib, bytes) ||
		    !check_if_alloc_index(hdr, aoff) ||
		    aoff + dlen > offsetof(struct INDEX_BUFFER, ihdr) +
					  le32_to_cpu(hdr->total)) {
			goto dirty_vol;
		}

		hdr->used = cpu_to_le32(dlen + PtrOffset(hdr, e));
		memmove(e, data, dlen);

		a_dirty = true;
		ntfs_fix_pre_write(&ib->rhdr, bytes);
		break;

	case SetIndexEntryVcnAllocation:
		ib = Add2Ptr(buffer_le, roff);
		hdr = &ib->ihdr;
		e = Add2Ptr(ib, aoff);

		if (is_baad(&ib->rhdr))
			goto dirty_vol;

		if (!check_lsn(&ib->rhdr, rlsn))
			goto out;
		if (!check_index_buffer(ib, bytes) ||
		    !check_if_alloc_index(hdr, aoff)) {
			goto dirty_vol;
		}

		de_set_vbn_le(e, *(__le64 *)data);

		a_dirty = true;
		ntfs_fix_pre_write(&ib->rhdr, bytes);
		break;

	case UpdateFileNameAllocation:
		ib = Add2Ptr(buffer_le, roff);
		hdr = &ib->ihdr;
		e = Add2Ptr(ib, aoff);

		if (is_baad(&ib->rhdr))
			goto dirty_vol;

		if (!check_lsn(&ib->rhdr, rlsn))
			goto out;
		if (!check_index_buffer(ib, bytes) ||
		    !check_if_alloc_index(hdr, aoff)) {
			goto dirty_vol;
		}

		fname = (struct ATTR_FILE_NAME *)(e + 1);
		memmove(&fname->dup, data, sizeof(fname->dup));

		a_dirty = true;
		ntfs_fix_pre_write(&ib->rhdr, bytes);
		break;

	case SetBitsInNonresidentBitMap:
		off = le32_to_cpu(((struct BITMAP_RANGE *)data)->bitmap_off);
		bits = le32_to_cpu(((struct BITMAP_RANGE *)data)->bits);

		if (cbo + (off + 7) / 8 > lco ||
		    cbo + ((off + bits + 7) / 8) > lco) {
			goto dirty_vol;
		}

		ntfs_bitmap_set_le(Add2Ptr(buffer_le, roff), off, bits);
		a_dirty = true;
		break;

	case ClearBitsInNonresidentBitMap:
		off = le32_to_cpu(((struct BITMAP_RANGE *)data)->bitmap_off);
		bits = le32_to_cpu(((struct BITMAP_RANGE *)data)->bits);

		if (cbo + (off + 7) / 8 > lco ||
		    cbo + ((off + bits + 7) / 8) > lco) {
			goto dirty_vol;
		}

		ntfs_bitmap_clear_le(Add2Ptr(buffer_le, roff), off, bits);
		a_dirty = true;
		break;

	case UpdateRecordDataAllocation:
		ib = Add2Ptr(buffer_le, roff);
		hdr = &ib->ihdr;
		e = Add2Ptr(ib, aoff);

		if (is_baad(&ib->rhdr))
			goto dirty_vol;

		if (!check_lsn(&ib->rhdr, rlsn))
			goto out;
		if (!check_index_buffer(ib, bytes) ||
		    !check_if_alloc_index(hdr, aoff)) {
			goto dirty_vol;
		}

		memmove(Add2Ptr(e, le16_to_cpu(e->view.data_off)), data, dlen);

		a_dirty = true;
		ntfs_fix_pre_write(&ib->rhdr, bytes);
		break;

	default:
		WARN_ON(1);
	}

	if (rlsn) {
		__le64 t64 = cpu_to_le64(*rlsn);

		if (rec)
			rec->rhdr.lsn = t64;
		if (ib)
			ib->rhdr.lsn = t64;
	}

	if (mi && mi->dirty) {
		err = mi_write(mi, 0);
		if (err)
			goto out;
	}

	if (a_dirty) {
		attr = oa->attr;
		err = ntfs_sb_write_run(sbi, oa->run1, vbo, buffer_le, bytes, 0);
		if (err)
			goto out;
	}

out:

	if (inode)
		iput(inode);
	else if (mi != mi2_child)
		mi_put(mi);

	kfree(buffer_le);

	return err;

dirty_vol:
	log->set_dirty = true;
	goto out;
}

/*
 * log_replay - Replays log and empties it.
 *
 * This function is called during mount operation.
 * It replays log and empties it.
 * Initialized is set false if logfile contains '-1'.
 */
int log_replay(struct ntfs_inode *ni, bool *initialized)
{
	int err;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ntfs_log *log;

	struct restart_info rst_info, rst_info2;
	u64 rec_lsn, ra_lsn, checkpt_lsn = 0, rlsn = 0;
	struct ATTR_NAME_ENTRY *attr_names = NULL;
	struct ATTR_NAME_ENTRY *ane;
	struct RESTART_TABLE *dptbl = NULL;
	struct RESTART_TABLE *trtbl = NULL;
	const struct RESTART_TABLE *rt;
	struct RESTART_TABLE *oatbl = NULL;
	struct inode *inode;
	struct OpenAttr *oa;
	struct ntfs_inode *ni_oe;
	struct ATTRIB *attr = NULL;
	u64 size, vcn, undo_next_lsn;
	CLST rno, lcn, lcn0, len0, clen;
	void *data;
	struct NTFS_RESTART *rst = NULL;
	struct lcb *lcb = NULL;
	struct OPEN_ATTR_ENRTY *oe;
	struct TRANSACTION_ENTRY *tr;
	struct DIR_PAGE_ENTRY *dp;
	u32 i, bytes_per_attr_entry;
	u32 l_size = ni->vfs_inode.i_size;
	u32 orig_file_size = l_size;
	u32 page_size, vbo, tail, off, dlen;
	u32 saved_len, rec_len, transact_id;
	bool use_second_page;
	struct RESTART_AREA *ra2, *ra = NULL;
	struct CLIENT_REC *ca, *cr;
	__le16 client;
	struct RESTART_HDR *rh;
	const struct LFS_RECORD_HDR *frh;
	const struct LOG_REC_HDR *lrh;
	bool is_mapped;
	bool is_ro = sb_rdonly(sbi->sb);
	u64 t64;
	u16 t16;
	u32 t32;

	/* Get the size of page. NOTE: To replay we can use default page. */
#if PAGE_SIZE >= DefaultLogPageSize && PAGE_SIZE <= DefaultLogPageSize * 2
	page_size = norm_file_page(PAGE_SIZE, &l_size, true);
#else
	page_size = norm_file_page(PAGE_SIZE, &l_size, false);
#endif
	if (!page_size)
		return -EINVAL;

	log = kzalloc(sizeof(struct ntfs_log), GFP_NOFS);
	if (!log)
		return -ENOMEM;

	memset(&rst_info, 0, sizeof(struct restart_info));

	log->ni = ni;
	log->l_size = l_size;
	log->one_page_buf = kmalloc(page_size, GFP_NOFS);
	if (!log->one_page_buf) {
		err = -ENOMEM;
		goto out;
	}

	log->page_size = page_size;
	log->page_mask = page_size - 1;
	log->page_bits = blksize_bits(page_size);

	/* Look for a restart area on the disk. */
	err = log_read_rst(log, l_size, true, &rst_info);
	if (err)
		goto out;

	/* remember 'initialized' */
	*initialized = rst_info.initialized;

	if (!rst_info.restart) {
		if (rst_info.initialized) {
			/* No restart area but the file is not initialized. */
			err = -EINVAL;
			goto out;
		}

		log_init_pg_hdr(log, page_size, page_size, 1, 1);
		log_create(log, l_size, 0, get_random_u32(), false, false);

		log->ra = ra;

		ra = log_create_ra(log);
		if (!ra) {
			err = -ENOMEM;
			goto out;
		}
		log->ra = ra;
		log->init_ra = true;

		goto process_log;
	}

	/*
	 * If the restart offset above wasn't zero then we won't
	 * look for a second restart.
	 */
	if (rst_info.vbo)
		goto check_restart_area;

	memset(&rst_info2, 0, sizeof(struct restart_info));
	err = log_read_rst(log, l_size, false, &rst_info2);
	if (err)
		goto out;

	/* Determine which restart area to use. */
	if (!rst_info2.restart || rst_info2.last_lsn <= rst_info.last_lsn)
		goto use_first_page;

	use_second_page = true;

	if (rst_info.chkdsk_was_run && page_size != rst_info.vbo) {
		struct RECORD_PAGE_HDR *sp = NULL;
		bool usa_error;

		if (!read_log_page(log, page_size, &sp, &usa_error) &&
		    sp->rhdr.sign == NTFS_CHKD_SIGNATURE) {
			use_second_page = false;
		}
		kfree(sp);
	}

	if (use_second_page) {
		kfree(rst_info.r_page);
		memcpy(&rst_info, &rst_info2, sizeof(struct restart_info));
		rst_info2.r_page = NULL;
	}

use_first_page:
	kfree(rst_info2.r_page);

check_restart_area:
	/*
	 * If the restart area is at offset 0, we want
	 * to write the second restart area first.
	 */
	log->init_ra = !!rst_info.vbo;

	/* If we have a valid page then grab a pointer to the restart area. */
	ra2 = rst_info.valid_page
		      ? Add2Ptr(rst_info.r_page,
				le16_to_cpu(rst_info.r_page->ra_off))
		      : NULL;

	if (rst_info.chkdsk_was_run ||
	    (ra2 && ra2->client_idx[1] == LFS_NO_CLIENT_LE)) {
		bool wrapped = false;
		bool use_multi_page = false;
		u32 open_log_count;

		/* Do some checks based on whether we have a valid log page. */
		if (!rst_info.valid_page) {
			open_log_count = get_random_u32();
			goto init_log_instance;
		}
		open_log_count = le32_to_cpu(ra2->open_log_count);

		/*
		 * If the restart page size isn't changing then we want to
		 * check how much work we need to do.
		 */
		if (page_size != le32_to_cpu(rst_info.r_page->sys_page_size))
			goto init_log_instance;

init_log_instance:
		log_init_pg_hdr(log, page_size, page_size, 1, 1);

		log_create(log, l_size, rst_info.last_lsn, open_log_count,
			   wrapped, use_multi_page);

		ra = log_create_ra(log);
		if (!ra) {
			err = -ENOMEM;
			goto out;
		}
		log->ra = ra;

		/* Put the restart areas and initialize
		 * the log file as required.
		 */
		goto process_log;
	}

	if (!ra2) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * If the log page or the system page sizes have changed, we can't
	 * use the log file. We must use the system page size instead of the
	 * default size if there is not a clean shutdown.
	 */
	t32 = le32_to_cpu(rst_info.r_page->sys_page_size);
	if (page_size != t32) {
		l_size = orig_file_size;
		page_size =
			norm_file_page(t32, &l_size, t32 == DefaultLogPageSize);
	}

	if (page_size != t32 ||
	    page_size != le32_to_cpu(rst_info.r_page->page_size)) {
		err = -EINVAL;
		goto out;
	}

	/* If the file size has shrunk then we won't mount it. */
	if (l_size < le64_to_cpu(ra2->l_size)) {
		err = -EINVAL;
		goto out;
	}

	log_init_pg_hdr(log, page_size, page_size,
			le16_to_cpu(rst_info.r_page->major_ver),
			le16_to_cpu(rst_info.r_page->minor_ver));

	log->l_size = le64_to_cpu(ra2->l_size);
	log->seq_num_bits = le32_to_cpu(ra2->seq_num_bits);
	log->file_data_bits = sizeof(u64) * 8 - log->seq_num_bits;
	log->seq_num_mask = (8 << log->file_data_bits) - 1;
	log->last_lsn = le64_to_cpu(ra2->current_lsn);
	log->seq_num = log->last_lsn >> log->file_data_bits;
	log->ra_off = le16_to_cpu(rst_info.r_page->ra_off);
	log->restart_size = log->sys_page_size - log->ra_off;
	log->record_header_len = le16_to_cpu(ra2->rec_hdr_len);
	log->ra_size = le16_to_cpu(ra2->ra_len);
	log->data_off = le16_to_cpu(ra2->data_off);
	log->data_size = log->page_size - log->data_off;
	log->reserved = log->data_size - log->record_header_len;

	vbo = lsn_to_vbo(log, log->last_lsn);

	if (vbo < log->first_page) {
		/* This is a pseudo lsn. */
		log->l_flags |= NTFSLOG_NO_LAST_LSN;
		log->next_page = log->first_page;
		goto find_oldest;
	}

	/* Find the end of this log record. */
	off = final_log_off(log, log->last_lsn,
			    le32_to_cpu(ra2->last_lsn_data_len));

	/* If we wrapped the file then increment the sequence number. */
	if (off <= vbo) {
		log->seq_num += 1;
		log->l_flags |= NTFSLOG_WRAPPED;
	}

	/* Now compute the next log page to use. */
	vbo &= ~log->sys_page_mask;
	tail = log->page_size - (off & log->page_mask) - 1;

	/*
	 *If we can fit another log record on the page,
	 * move back a page the log file.
	 */
	if (tail >= log->record_header_len) {
		log->l_flags |= NTFSLOG_REUSE_TAIL;
		log->next_page = vbo;
	} else {
		log->next_page = next_page_off(log, vbo);
	}

find_oldest:
	/*
	 * Find the oldest client lsn. Use the last
	 * flushed lsn as a starting point.
	 */
	log->oldest_lsn = log->last_lsn;
	oldest_client_lsn(Add2Ptr(ra2, le16_to_cpu(ra2->client_off)),
			  ra2->client_idx[1], &log->oldest_lsn);
	log->oldest_lsn_off = lsn_to_vbo(log, log->oldest_lsn);

	if (log->oldest_lsn_off < log->first_page)
		log->l_flags |= NTFSLOG_NO_OLDEST_LSN;

	if (!(ra2->flags & RESTART_SINGLE_PAGE_IO))
		log->l_flags |= NTFSLOG_WRAPPED | NTFSLOG_MULTIPLE_PAGE_IO;

	log->current_openlog_count = le32_to_cpu(ra2->open_log_count);
	log->total_avail_pages = log->l_size - log->first_page;
	log->total_avail = log->total_avail_pages >> log->page_bits;
	log->max_current_avail = log->total_avail * log->reserved;
	log->total_avail = log->total_avail * log->data_size;

	log->current_avail = current_log_avail(log);

	ra = kzalloc(log->restart_size, GFP_NOFS);
	if (!ra) {
		err = -ENOMEM;
		goto out;
	}
	log->ra = ra;

	t16 = le16_to_cpu(ra2->client_off);
	if (t16 == offsetof(struct RESTART_AREA, clients)) {
		memcpy(ra, ra2, log->ra_size);
	} else {
		memcpy(ra, ra2, offsetof(struct RESTART_AREA, clients));
		memcpy(ra->clients, Add2Ptr(ra2, t16),
		       le16_to_cpu(ra2->ra_len) - t16);

		log->current_openlog_count = get_random_u32();
		ra->open_log_count = cpu_to_le32(log->current_openlog_count);
		log->ra_size = offsetof(struct RESTART_AREA, clients) +
			       sizeof(struct CLIENT_REC);
		ra->client_off =
			cpu_to_le16(offsetof(struct RESTART_AREA, clients));
		ra->ra_len = cpu_to_le16(log->ra_size);
	}

	le32_add_cpu(&ra->open_log_count, 1);

	/* Now we need to walk through looking for the last lsn. */
	err = last_log_lsn(log);
	if (err)
		goto out;

	log->current_avail = current_log_avail(log);

	/* Remember which restart area to write first. */
	log->init_ra = rst_info.vbo;

process_log:
	/* 1.0, 1.1, 2.0 log->major_ver/minor_ver - short values. */
	switch ((log->major_ver << 16) + log->minor_ver) {
	case 0x10000:
	case 0x10001:
	case 0x20000:
		break;
	default:
		ntfs_warn(sbi->sb, "\x24LogFile version %d.%d is not supported",
			  log->major_ver, log->minor_ver);
		err = -EOPNOTSUPP;
		log->set_dirty = true;
		goto out;
	}

	/* One client "NTFS" per logfile. */
	ca = Add2Ptr(ra, le16_to_cpu(ra->client_off));

	for (client = ra->client_idx[1];; client = cr->next_client) {
		if (client == LFS_NO_CLIENT_LE) {
			/* Insert "NTFS" client LogFile. */
			client = ra->client_idx[0];
			if (client == LFS_NO_CLIENT_LE) {
				err = -EINVAL;
				goto out;
			}

			t16 = le16_to_cpu(client);
			cr = ca + t16;

			remove_client(ca, cr, &ra->client_idx[0]);

			cr->restart_lsn = 0;
			cr->oldest_lsn = cpu_to_le64(log->oldest_lsn);
			cr->name_bytes = cpu_to_le32(8);
			cr->name[0] = cpu_to_le16('N');
			cr->name[1] = cpu_to_le16('T');
			cr->name[2] = cpu_to_le16('F');
			cr->name[3] = cpu_to_le16('S');

			add_client(ca, t16, &ra->client_idx[1]);
			break;
		}

		cr = ca + le16_to_cpu(client);

		if (cpu_to_le32(8) == cr->name_bytes &&
		    cpu_to_le16('N') == cr->name[0] &&
		    cpu_to_le16('T') == cr->name[1] &&
		    cpu_to_le16('F') == cr->name[2] &&
		    cpu_to_le16('S') == cr->name[3])
			break;
	}

	/* Update the client handle with the client block information. */
	log->client_id.seq_num = cr->seq_num;
	log->client_id.client_idx = client;

	err = read_rst_area(log, &rst, &ra_lsn);
	if (err)
		goto out;

	if (!rst)
		goto out;

	bytes_per_attr_entry = !rst->major_ver ? 0x2C : 0x28;

	checkpt_lsn = le64_to_cpu(rst->check_point_start);
	if (!checkpt_lsn)
		checkpt_lsn = ra_lsn;

	/* Allocate and Read the Transaction Table. */
	if (!rst->transact_table_len)
		goto check_dirty_page_table;

	t64 = le64_to_cpu(rst->transact_table_lsn);
	err = read_log_rec_lcb(log, t64, lcb_ctx_prev, &lcb);
	if (err)
		goto out;

	lrh = lcb->log_rec;
	frh = lcb->lrh;
	rec_len = le32_to_cpu(frh->client_data_len);

	if (!check_log_rec(lrh, rec_len, le32_to_cpu(frh->transact_id),
			   bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	t16 = le16_to_cpu(lrh->redo_off);

	rt = Add2Ptr(lrh, t16);
	t32 = rec_len - t16;

	/* Now check that this is a valid restart table. */
	if (!check_rstbl(rt, t32)) {
		err = -EINVAL;
		goto out;
	}

	trtbl = kmemdup(rt, t32, GFP_NOFS);
	if (!trtbl) {
		err = -ENOMEM;
		goto out;
	}

	lcb_put(lcb);
	lcb = NULL;

check_dirty_page_table:
	/* The next record back should be the Dirty Pages Table. */
	if (!rst->dirty_pages_len)
		goto check_attribute_names;

	t64 = le64_to_cpu(rst->dirty_pages_table_lsn);
	err = read_log_rec_lcb(log, t64, lcb_ctx_prev, &lcb);
	if (err)
		goto out;

	lrh = lcb->log_rec;
	frh = lcb->lrh;
	rec_len = le32_to_cpu(frh->client_data_len);

	if (!check_log_rec(lrh, rec_len, le32_to_cpu(frh->transact_id),
			   bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	t16 = le16_to_cpu(lrh->redo_off);

	rt = Add2Ptr(lrh, t16);
	t32 = rec_len - t16;

	/* Now check that this is a valid restart table. */
	if (!check_rstbl(rt, t32)) {
		err = -EINVAL;
		goto out;
	}

	dptbl = kmemdup(rt, t32, GFP_NOFS);
	if (!dptbl) {
		err = -ENOMEM;
		goto out;
	}

	/* Convert Ra version '0' into version '1'. */
	if (rst->major_ver)
		goto end_conv_1;

	dp = NULL;
	while ((dp = enum_rstbl(dptbl, dp))) {
		struct DIR_PAGE_ENTRY_32 *dp0 = (struct DIR_PAGE_ENTRY_32 *)dp;
		// NOTE: Danger. Check for of boundary.
		memmove(&dp->vcn, &dp0->vcn_low,
			2 * sizeof(u64) +
				le32_to_cpu(dp->lcns_follow) * sizeof(u64));
	}

end_conv_1:
	lcb_put(lcb);
	lcb = NULL;

	/*
	 * Go through the table and remove the duplicates,
	 * remembering the oldest lsn values.
	 */
	if (sbi->cluster_size <= log->page_size)
		goto trace_dp_table;

	dp = NULL;
	while ((dp = enum_rstbl(dptbl, dp))) {
		struct DIR_PAGE_ENTRY *next = dp;

		while ((next = enum_rstbl(dptbl, next))) {
			if (next->target_attr == dp->target_attr &&
			    next->vcn == dp->vcn) {
				if (le64_to_cpu(next->oldest_lsn) <
				    le64_to_cpu(dp->oldest_lsn)) {
					dp->oldest_lsn = next->oldest_lsn;
				}

				free_rsttbl_idx(dptbl, PtrOffset(dptbl, next));
			}
		}
	}
trace_dp_table:
check_attribute_names:
	/* The next record should be the Attribute Names. */
	if (!rst->attr_names_len)
		goto check_attr_table;

	t64 = le64_to_cpu(rst->attr_names_lsn);
	err = read_log_rec_lcb(log, t64, lcb_ctx_prev, &lcb);
	if (err)
		goto out;

	lrh = lcb->log_rec;
	frh = lcb->lrh;
	rec_len = le32_to_cpu(frh->client_data_len);

	if (!check_log_rec(lrh, rec_len, le32_to_cpu(frh->transact_id),
			   bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	t32 = lrh_length(lrh);
	rec_len -= t32;

	attr_names = kmemdup(Add2Ptr(lrh, t32), rec_len, GFP_NOFS);
	if (!attr_names) {
		err = -ENOMEM;
		goto out;
	}

	lcb_put(lcb);
	lcb = NULL;

check_attr_table:
	/* The next record should be the attribute Table. */
	if (!rst->open_attr_len)
		goto check_attribute_names2;

	t64 = le64_to_cpu(rst->open_attr_table_lsn);
	err = read_log_rec_lcb(log, t64, lcb_ctx_prev, &lcb);
	if (err)
		goto out;

	lrh = lcb->log_rec;
	frh = lcb->lrh;
	rec_len = le32_to_cpu(frh->client_data_len);

	if (!check_log_rec(lrh, rec_len, le32_to_cpu(frh->transact_id),
			   bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	t16 = le16_to_cpu(lrh->redo_off);

	rt = Add2Ptr(lrh, t16);
	t32 = rec_len - t16;

	if (!check_rstbl(rt, t32)) {
		err = -EINVAL;
		goto out;
	}

	oatbl = kmemdup(rt, t32, GFP_NOFS);
	if (!oatbl) {
		err = -ENOMEM;
		goto out;
	}

	log->open_attr_tbl = oatbl;

	/* Clear all of the Attr pointers. */
	oe = NULL;
	while ((oe = enum_rstbl(oatbl, oe))) {
		if (!rst->major_ver) {
			struct OPEN_ATTR_ENRTY_32 oe0;

			/* Really 'oe' points to OPEN_ATTR_ENRTY_32. */
			memcpy(&oe0, oe, SIZEOF_OPENATTRIBUTEENTRY0);

			oe->bytes_per_index = oe0.bytes_per_index;
			oe->type = oe0.type;
			oe->is_dirty_pages = oe0.is_dirty_pages;
			oe->name_len = 0;
			oe->ref = oe0.ref;
			oe->open_record_lsn = oe0.open_record_lsn;
		}

		oe->is_attr_name = 0;
		oe->ptr = NULL;
	}

	lcb_put(lcb);
	lcb = NULL;

check_attribute_names2:
	if (!rst->attr_names_len)
		goto trace_attribute_table;

	ane = attr_names;
	if (!oatbl)
		goto trace_attribute_table;
	while (ane->off) {
		/* TODO: Clear table on exit! */
		oe = Add2Ptr(oatbl, le16_to_cpu(ane->off));
		t16 = le16_to_cpu(ane->name_bytes);
		oe->name_len = t16 / sizeof(short);
		oe->ptr = ane->name;
		oe->is_attr_name = 2;
		ane = Add2Ptr(ane, sizeof(struct ATTR_NAME_ENTRY) + t16);
	}

trace_attribute_table:
	/*
	 * If the checkpt_lsn is zero, then this is a freshly
	 * formatted disk and we have no work to do.
	 */
	if (!checkpt_lsn) {
		err = 0;
		goto out;
	}

	if (!oatbl) {
		oatbl = init_rsttbl(bytes_per_attr_entry, 8);
		if (!oatbl) {
			err = -ENOMEM;
			goto out;
		}
	}

	log->open_attr_tbl = oatbl;

	/* Start the analysis pass from the Checkpoint lsn. */
	rec_lsn = checkpt_lsn;

	/* Read the first lsn. */
	err = read_log_rec_lcb(log, checkpt_lsn, lcb_ctx_next, &lcb);
	if (err)
		goto out;

	/* Loop to read all subsequent records to the end of the log file. */
next_log_record_analyze:
	err = read_next_log_rec(log, lcb, &rec_lsn);
	if (err)
		goto out;

	if (!rec_lsn)
		goto end_log_records_enumerate;

	frh = lcb->lrh;
	transact_id = le32_to_cpu(frh->transact_id);
	rec_len = le32_to_cpu(frh->client_data_len);
	lrh = lcb->log_rec;

	if (!check_log_rec(lrh, rec_len, transact_id, bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * The first lsn after the previous lsn remembered
	 * the checkpoint is the first candidate for the rlsn.
	 */
	if (!rlsn)
		rlsn = rec_lsn;

	if (LfsClientRecord != frh->record_type)
		goto next_log_record_analyze;

	/*
	 * Now update the Transaction Table for this transaction. If there
	 * is no entry present or it is unallocated we allocate the entry.
	 */
	if (!trtbl) {
		trtbl = init_rsttbl(sizeof(struct TRANSACTION_ENTRY),
				    INITIAL_NUMBER_TRANSACTIONS);
		if (!trtbl) {
			err = -ENOMEM;
			goto out;
		}
	}

	tr = Add2Ptr(trtbl, transact_id);

	if (transact_id >= bytes_per_rt(trtbl) ||
	    tr->next != RESTART_ENTRY_ALLOCATED_LE) {
		tr = alloc_rsttbl_from_idx(&trtbl, transact_id);
		if (!tr) {
			err = -ENOMEM;
			goto out;
		}
		tr->transact_state = TransactionActive;
		tr->first_lsn = cpu_to_le64(rec_lsn);
	}

	tr->prev_lsn = tr->undo_next_lsn = cpu_to_le64(rec_lsn);

	/*
	 * If this is a compensation log record, then change
	 * the undo_next_lsn to be the undo_next_lsn of this record.
	 */
	if (lrh->undo_op == cpu_to_le16(CompensationLogRecord))
		tr->undo_next_lsn = frh->client_undo_next_lsn;

	/* Dispatch to handle log record depending on type. */
	switch (le16_to_cpu(lrh->redo_op)) {
	case InitializeFileRecordSegment:
	case DeallocateFileRecordSegment:
	case WriteEndOfFileRecordSegment:
	case CreateAttribute:
	case DeleteAttribute:
	case UpdateResidentValue:
	case UpdateNonresidentValue:
	case UpdateMappingPairs:
	case SetNewAttributeSizes:
	case AddIndexEntryRoot:
	case DeleteIndexEntryRoot:
	case AddIndexEntryAllocation:
	case DeleteIndexEntryAllocation:
	case WriteEndOfIndexBuffer:
	case SetIndexEntryVcnRoot:
	case SetIndexEntryVcnAllocation:
	case UpdateFileNameRoot:
	case UpdateFileNameAllocation:
	case SetBitsInNonresidentBitMap:
	case ClearBitsInNonresidentBitMap:
	case UpdateRecordDataRoot:
	case UpdateRecordDataAllocation:
	case ZeroEndOfFileRecord:
		t16 = le16_to_cpu(lrh->target_attr);
		t64 = le64_to_cpu(lrh->target_vcn);
		dp = find_dp(dptbl, t16, t64);

		if (dp)
			goto copy_lcns;

		/*
		 * Calculate the number of clusters per page the system
		 * which wrote the checkpoint, possibly creating the table.
		 */
		if (dptbl) {
			t32 = (le16_to_cpu(dptbl->size) -
			       sizeof(struct DIR_PAGE_ENTRY)) /
			      sizeof(u64);
		} else {
			t32 = log->clst_per_page;
			kfree(dptbl);
			dptbl = init_rsttbl(struct_size(dp, page_lcns, t32),
					    32);
			if (!dptbl) {
				err = -ENOMEM;
				goto out;
			}
		}

		dp = alloc_rsttbl_idx(&dptbl);
		if (!dp) {
			err = -ENOMEM;
			goto out;
		}
		dp->target_attr = cpu_to_le32(t16);
		dp->transfer_len = cpu_to_le32(t32 << sbi->cluster_bits);
		dp->lcns_follow = cpu_to_le32(t32);
		dp->vcn = cpu_to_le64(t64 & ~((u64)t32 - 1));
		dp->oldest_lsn = cpu_to_le64(rec_lsn);

copy_lcns:
		/*
		 * Copy the Lcns from the log record into the Dirty Page Entry.
		 * TODO: For different page size support, must somehow make
		 * whole routine a loop, case Lcns do not fit below.
		 */
		t16 = le16_to_cpu(lrh->lcns_follow);
		for (i = 0; i < t16; i++) {
			size_t j = (size_t)(le64_to_cpu(lrh->target_vcn) -
					    le64_to_cpu(dp->vcn));
			dp->page_lcns[j + i] = lrh->page_lcns[i];
		}

		goto next_log_record_analyze;

	case DeleteDirtyClusters: {
		u32 range_count =
			le16_to_cpu(lrh->redo_len) / sizeof(struct LCN_RANGE);
		const struct LCN_RANGE *r =
			Add2Ptr(lrh, le16_to_cpu(lrh->redo_off));

		/* Loop through all of the Lcn ranges this log record. */
		for (i = 0; i < range_count; i++, r++) {
			u64 lcn0 = le64_to_cpu(r->lcn);
			u64 lcn_e = lcn0 + le64_to_cpu(r->len) - 1;

			dp = NULL;
			while ((dp = enum_rstbl(dptbl, dp))) {
				u32 j;

				t32 = le32_to_cpu(dp->lcns_follow);
				for (j = 0; j < t32; j++) {
					t64 = le64_to_cpu(dp->page_lcns[j]);
					if (t64 >= lcn0 && t64 <= lcn_e)
						dp->page_lcns[j] = 0;
				}
			}
		}
		goto next_log_record_analyze;
		;
	}

	case OpenNonresidentAttribute:
		t16 = le16_to_cpu(lrh->target_attr);
		if (t16 >= bytes_per_rt(oatbl)) {
			/*
			 * Compute how big the table needs to be.
			 * Add 10 extra entries for some cushion.
			 */
			u32 new_e = t16 / le16_to_cpu(oatbl->size);

			new_e += 10 - le16_to_cpu(oatbl->used);

			oatbl = extend_rsttbl(oatbl, new_e, ~0u);
			log->open_attr_tbl = oatbl;
			if (!oatbl) {
				err = -ENOMEM;
				goto out;
			}
		}

		/* Point to the entry being opened. */
		oe = alloc_rsttbl_from_idx(&oatbl, t16);
		log->open_attr_tbl = oatbl;
		if (!oe) {
			err = -ENOMEM;
			goto out;
		}

		/* Initialize this entry from the log record. */
		t16 = le16_to_cpu(lrh->redo_off);
		if (!rst->major_ver) {
			/* Convert version '0' into version '1'. */
			struct OPEN_ATTR_ENRTY_32 *oe0 = Add2Ptr(lrh, t16);

			oe->bytes_per_index = oe0->bytes_per_index;
			oe->type = oe0->type;
			oe->is_dirty_pages = oe0->is_dirty_pages;
			oe->name_len = 0; //oe0.name_len;
			oe->ref = oe0->ref;
			oe->open_record_lsn = oe0->open_record_lsn;
		} else {
			memcpy(oe, Add2Ptr(lrh, t16), bytes_per_attr_entry);
		}

		t16 = le16_to_cpu(lrh->undo_len);
		if (t16) {
			oe->ptr = kmalloc(t16, GFP_NOFS);
			if (!oe->ptr) {
				err = -ENOMEM;
				goto out;
			}
			oe->name_len = t16 / sizeof(short);
			memcpy(oe->ptr,
			       Add2Ptr(lrh, le16_to_cpu(lrh->undo_off)), t16);
			oe->is_attr_name = 1;
		} else {
			oe->ptr = NULL;
			oe->is_attr_name = 0;
		}

		goto next_log_record_analyze;

	case HotFix:
		t16 = le16_to_cpu(lrh->target_attr);
		t64 = le64_to_cpu(lrh->target_vcn);
		dp = find_dp(dptbl, t16, t64);
		if (dp) {
			size_t j = le64_to_cpu(lrh->target_vcn) -
				   le64_to_cpu(dp->vcn);
			if (dp->page_lcns[j])
				dp->page_lcns[j] = lrh->page_lcns[0];
		}
		goto next_log_record_analyze;

	case EndTopLevelAction:
		tr = Add2Ptr(trtbl, transact_id);
		tr->prev_lsn = cpu_to_le64(rec_lsn);
		tr->undo_next_lsn = frh->client_undo_next_lsn;
		goto next_log_record_analyze;

	case PrepareTransaction:
		tr = Add2Ptr(trtbl, transact_id);
		tr->transact_state = TransactionPrepared;
		goto next_log_record_analyze;

	case CommitTransaction:
		tr = Add2Ptr(trtbl, transact_id);
		tr->transact_state = TransactionCommitted;
		goto next_log_record_analyze;

	case ForgetTransaction:
		free_rsttbl_idx(trtbl, transact_id);
		goto next_log_record_analyze;

	case Noop:
	case OpenAttributeTableDump:
	case AttributeNamesDump:
	case DirtyPageTableDump:
	case TransactionTableDump:
		/* The following cases require no action the Analysis Pass. */
		goto next_log_record_analyze;

	default:
		/*
		 * All codes will be explicitly handled.
		 * If we see a code we do not expect, then we are trouble.
		 */
		goto next_log_record_analyze;
	}

end_log_records_enumerate:
	lcb_put(lcb);
	lcb = NULL;

	/*
	 * Scan the Dirty Page Table and Transaction Table for
	 * the lowest lsn, and return it as the Redo lsn.
	 */
	dp = NULL;
	while ((dp = enum_rstbl(dptbl, dp))) {
		t64 = le64_to_cpu(dp->oldest_lsn);
		if (t64 && t64 < rlsn)
			rlsn = t64;
	}

	tr = NULL;
	while ((tr = enum_rstbl(trtbl, tr))) {
		t64 = le64_to_cpu(tr->first_lsn);
		if (t64 && t64 < rlsn)
			rlsn = t64;
	}

	/*
	 * Only proceed if the Dirty Page Table or Transaction
	 * table are not empty.
	 */
	if ((!dptbl || !dptbl->total) && (!trtbl || !trtbl->total))
		goto end_reply;

	sbi->flags |= NTFS_FLAGS_NEED_REPLAY;
	if (is_ro)
		goto out;

	/* Reopen all of the attributes with dirty pages. */
	oe = NULL;
next_open_attribute:

	oe = enum_rstbl(oatbl, oe);
	if (!oe) {
		err = 0;
		dp = NULL;
		goto next_dirty_page;
	}

	oa = kzalloc(sizeof(struct OpenAttr), GFP_NOFS);
	if (!oa) {
		err = -ENOMEM;
		goto out;
	}

	inode = ntfs_iget5(sbi->sb, &oe->ref, NULL);
	if (IS_ERR(inode))
		goto fake_attr;

	if (is_bad_inode(inode)) {
		iput(inode);
fake_attr:
		if (oa->ni) {
			iput(&oa->ni->vfs_inode);
			oa->ni = NULL;
		}

		attr = attr_create_nonres_log(sbi, oe->type, 0, oe->ptr,
					      oe->name_len, 0);
		if (!attr) {
			kfree(oa);
			err = -ENOMEM;
			goto out;
		}
		oa->attr = attr;
		oa->run1 = &oa->run0;
		goto final_oe;
	}

	ni_oe = ntfs_i(inode);
	oa->ni = ni_oe;

	attr = ni_find_attr(ni_oe, NULL, NULL, oe->type, oe->ptr, oe->name_len,
			    NULL, NULL);

	if (!attr)
		goto fake_attr;

	t32 = le32_to_cpu(attr->size);
	oa->attr = kmemdup(attr, t32, GFP_NOFS);
	if (!oa->attr)
		goto fake_attr;

	if (!S_ISDIR(inode->i_mode)) {
		if (attr->type == ATTR_DATA && !attr->name_len) {
			oa->run1 = &ni_oe->file.run;
			goto final_oe;
		}
	} else {
		if (attr->type == ATTR_ALLOC &&
		    attr->name_len == ARRAY_SIZE(I30_NAME) &&
		    !memcmp(attr_name(attr), I30_NAME, sizeof(I30_NAME))) {
			oa->run1 = &ni_oe->dir.alloc_run;
			goto final_oe;
		}
	}

	if (attr->non_res) {
		u16 roff = le16_to_cpu(attr->nres.run_off);
		CLST svcn = le64_to_cpu(attr->nres.svcn);

		if (roff > t32) {
			kfree(oa->attr);
			oa->attr = NULL;
			goto fake_attr;
		}

		err = run_unpack(&oa->run0, sbi, inode->i_ino, svcn,
				 le64_to_cpu(attr->nres.evcn), svcn,
				 Add2Ptr(attr, roff), t32 - roff);
		if (err < 0) {
			kfree(oa->attr);
			oa->attr = NULL;
			goto fake_attr;
		}
		err = 0;
	}
	oa->run1 = &oa->run0;
	attr = oa->attr;

final_oe:
	if (oe->is_attr_name == 1)
		kfree(oe->ptr);
	oe->is_attr_name = 0;
	oe->ptr = oa;
	oe->name_len = attr->name_len;

	goto next_open_attribute;

	/*
	 * Now loop through the dirty page table to extract all of the Vcn/Lcn.
	 * Mapping that we have, and insert it into the appropriate run.
	 */
next_dirty_page:
	dp = enum_rstbl(dptbl, dp);
	if (!dp)
		goto do_redo_1;

	oe = Add2Ptr(oatbl, le32_to_cpu(dp->target_attr));

	if (oe->next != RESTART_ENTRY_ALLOCATED_LE)
		goto next_dirty_page;

	oa = oe->ptr;
	if (!oa)
		goto next_dirty_page;

	i = -1;
next_dirty_page_vcn:
	i += 1;
	if (i >= le32_to_cpu(dp->lcns_follow))
		goto next_dirty_page;

	vcn = le64_to_cpu(dp->vcn) + i;
	size = (vcn + 1) << sbi->cluster_bits;

	if (!dp->page_lcns[i])
		goto next_dirty_page_vcn;

	rno = ino_get(&oe->ref);
	if (rno <= MFT_REC_MIRR &&
	    size < (MFT_REC_VOL + 1) * sbi->record_size &&
	    oe->type == ATTR_DATA) {
		goto next_dirty_page_vcn;
	}

	lcn = le64_to_cpu(dp->page_lcns[i]);

	if ((!run_lookup_entry(oa->run1, vcn, &lcn0, &len0, NULL) ||
	     lcn0 != lcn) &&
	    !run_add_entry(oa->run1, vcn, lcn, 1, false)) {
		err = -ENOMEM;
		goto out;
	}
	attr = oa->attr;
	if (size > le64_to_cpu(attr->nres.alloc_size)) {
		attr->nres.valid_size = attr->nres.data_size =
			attr->nres.alloc_size = cpu_to_le64(size);
	}
	goto next_dirty_page_vcn;

do_redo_1:
	/*
	 * Perform the Redo Pass, to restore all of the dirty pages to the same
	 * contents that they had immediately before the crash. If the dirty
	 * page table is empty, then we can skip the entire Redo Pass.
	 */
	if (!dptbl || !dptbl->total)
		goto do_undo_action;

	rec_lsn = rlsn;

	/*
	 * Read the record at the Redo lsn, before falling
	 * into common code to handle each record.
	 */
	err = read_log_rec_lcb(log, rlsn, lcb_ctx_next, &lcb);
	if (err)
		goto out;

	/*
	 * Now loop to read all of our log records forwards, until
	 * we hit the end of the file, cleaning up at the end.
	 */
do_action_next:
	frh = lcb->lrh;

	if (LfsClientRecord != frh->record_type)
		goto read_next_log_do_action;

	transact_id = le32_to_cpu(frh->transact_id);
	rec_len = le32_to_cpu(frh->client_data_len);
	lrh = lcb->log_rec;

	if (!check_log_rec(lrh, rec_len, transact_id, bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	/* Ignore log records that do not update pages. */
	if (lrh->lcns_follow)
		goto find_dirty_page;

	goto read_next_log_do_action;

find_dirty_page:
	t16 = le16_to_cpu(lrh->target_attr);
	t64 = le64_to_cpu(lrh->target_vcn);
	dp = find_dp(dptbl, t16, t64);

	if (!dp)
		goto read_next_log_do_action;

	if (rec_lsn < le64_to_cpu(dp->oldest_lsn))
		goto read_next_log_do_action;

	t16 = le16_to_cpu(lrh->target_attr);
	if (t16 >= bytes_per_rt(oatbl)) {
		err = -EINVAL;
		goto out;
	}

	oe = Add2Ptr(oatbl, t16);

	if (oe->next != RESTART_ENTRY_ALLOCATED_LE) {
		err = -EINVAL;
		goto out;
	}

	oa = oe->ptr;

	if (!oa) {
		err = -EINVAL;
		goto out;
	}
	attr = oa->attr;

	vcn = le64_to_cpu(lrh->target_vcn);

	if (!run_lookup_entry(oa->run1, vcn, &lcn, NULL, NULL) ||
	    lcn == SPARSE_LCN) {
		goto read_next_log_do_action;
	}

	/* Point to the Redo data and get its length. */
	data = Add2Ptr(lrh, le16_to_cpu(lrh->redo_off));
	dlen = le16_to_cpu(lrh->redo_len);

	/* Shorten length by any Lcns which were deleted. */
	saved_len = dlen;

	for (i = le16_to_cpu(lrh->lcns_follow); i; i--) {
		size_t j;
		u32 alen, voff;

		voff = le16_to_cpu(lrh->record_off) +
		       le16_to_cpu(lrh->attr_off);
		voff += le16_to_cpu(lrh->cluster_off) << SECTOR_SHIFT;

		/* If the Vcn question is allocated, we can just get out. */
		j = le64_to_cpu(lrh->target_vcn) - le64_to_cpu(dp->vcn);
		if (dp->page_lcns[j + i - 1])
			break;

		if (!saved_len)
			saved_len = 1;

		/*
		 * Calculate the allocated space left relative to the
		 * log record Vcn, after removing this unallocated Vcn.
		 */
		alen = (i - 1) << sbi->cluster_bits;

		/*
		 * If the update described this log record goes beyond
		 * the allocated space, then we will have to reduce the length.
		 */
		if (voff >= alen)
			dlen = 0;
		else if (voff + dlen > alen)
			dlen = alen - voff;
	}

	/*
	 * If the resulting dlen from above is now zero,
	 * we can skip this log record.
	 */
	if (!dlen && saved_len)
		goto read_next_log_do_action;

	t16 = le16_to_cpu(lrh->redo_op);
	if (can_skip_action(t16))
		goto read_next_log_do_action;

	/* Apply the Redo operation a common routine. */
	err = do_action(log, oe, lrh, t16, data, dlen, rec_len, &rec_lsn);
	if (err)
		goto out;

	/* Keep reading and looping back until end of file. */
read_next_log_do_action:
	err = read_next_log_rec(log, lcb, &rec_lsn);
	if (!err && rec_lsn)
		goto do_action_next;

	lcb_put(lcb);
	lcb = NULL;

do_undo_action:
	/* Scan Transaction Table. */
	tr = NULL;
transaction_table_next:
	tr = enum_rstbl(trtbl, tr);
	if (!tr)
		goto undo_action_done;

	if (TransactionActive != tr->transact_state || !tr->undo_next_lsn) {
		free_rsttbl_idx(trtbl, PtrOffset(trtbl, tr));
		goto transaction_table_next;
	}

	log->transaction_id = PtrOffset(trtbl, tr);
	undo_next_lsn = le64_to_cpu(tr->undo_next_lsn);

	/*
	 * We only have to do anything if the transaction has
	 * something its undo_next_lsn field.
	 */
	if (!undo_next_lsn)
		goto commit_undo;

	/* Read the first record to be undone by this transaction. */
	err = read_log_rec_lcb(log, undo_next_lsn, lcb_ctx_undo_next, &lcb);
	if (err)
		goto out;

	/*
	 * Now loop to read all of our log records forwards,
	 * until we hit the end of the file, cleaning up at the end.
	 */
undo_action_next:

	lrh = lcb->log_rec;
	frh = lcb->lrh;
	transact_id = le32_to_cpu(frh->transact_id);
	rec_len = le32_to_cpu(frh->client_data_len);

	if (!check_log_rec(lrh, rec_len, transact_id, bytes_per_attr_entry)) {
		err = -EINVAL;
		goto out;
	}

	if (lrh->undo_op == cpu_to_le16(Noop))
		goto read_next_log_undo_action;

	oe = Add2Ptr(oatbl, le16_to_cpu(lrh->target_attr));
	oa = oe->ptr;

	t16 = le16_to_cpu(lrh->lcns_follow);
	if (!t16)
		goto add_allocated_vcns;

	is_mapped = run_lookup_entry(oa->run1, le64_to_cpu(lrh->target_vcn),
				     &lcn, &clen, NULL);

	/*
	 * If the mapping isn't already the table or the  mapping
	 * corresponds to a hole the mapping, we need to make sure
	 * there is no partial page already memory.
	 */
	if (is_mapped && lcn != SPARSE_LCN && clen >= t16)
		goto add_allocated_vcns;

	vcn = le64_to_cpu(lrh->target_vcn);
	vcn &= ~(u64)(log->clst_per_page - 1);

add_allocated_vcns:
	for (i = 0, vcn = le64_to_cpu(lrh->target_vcn),
	    size = (vcn + 1) << sbi->cluster_bits;
	     i < t16; i++, vcn += 1, size += sbi->cluster_size) {
		attr = oa->attr;
		if (!attr->non_res) {
			if (size > le32_to_cpu(attr->res.data_size))
				attr->res.data_size = cpu_to_le32(size);
		} else {
			if (size > le64_to_cpu(attr->nres.data_size))
				attr->nres.valid_size = attr->nres.data_size =
					attr->nres.alloc_size =
						cpu_to_le64(size);
		}
	}

	t16 = le16_to_cpu(lrh->undo_op);
	if (can_skip_action(t16))
		goto read_next_log_undo_action;

	/* Point to the Redo data and get its length. */
	data = Add2Ptr(lrh, le16_to_cpu(lrh->undo_off));
	dlen = le16_to_cpu(lrh->undo_len);

	/* It is time to apply the undo action. */
	err = do_action(log, oe, lrh, t16, data, dlen, rec_len, NULL);

read_next_log_undo_action:
	/*
	 * Keep reading and looping back until we have read the
	 * last record for this transaction.
	 */
	err = read_next_log_rec(log, lcb, &rec_lsn);
	if (err)
		goto out;

	if (rec_lsn)
		goto undo_action_next;

	lcb_put(lcb);
	lcb = NULL;

commit_undo:
	free_rsttbl_idx(trtbl, log->transaction_id);

	log->transaction_id = 0;

	goto transaction_table_next;

undo_action_done:

	ntfs_update_mftmirr(sbi, 0);

	sbi->flags &= ~NTFS_FLAGS_NEED_REPLAY;

end_reply:

	err = 0;
	if (is_ro)
		goto out;

	rh = kzalloc(log->page_size, GFP_NOFS);
	if (!rh) {
		err = -ENOMEM;
		goto out;
	}

	rh->rhdr.sign = NTFS_RSTR_SIGNATURE;
	rh->rhdr.fix_off = cpu_to_le16(offsetof(struct RESTART_HDR, fixups));
	t16 = (log->page_size >> SECTOR_SHIFT) + 1;
	rh->rhdr.fix_num = cpu_to_le16(t16);
	rh->sys_page_size = cpu_to_le32(log->page_size);
	rh->page_size = cpu_to_le32(log->page_size);

	t16 = ALIGN(offsetof(struct RESTART_HDR, fixups) + sizeof(short) * t16,
		    8);
	rh->ra_off = cpu_to_le16(t16);
	rh->minor_ver = cpu_to_le16(1); // 0x1A:
	rh->major_ver = cpu_to_le16(1); // 0x1C:

	ra2 = Add2Ptr(rh, t16);
	memcpy(ra2, ra, sizeof(struct RESTART_AREA));

	ra2->client_idx[0] = 0;
	ra2->client_idx[1] = LFS_NO_CLIENT_LE;
	ra2->flags = cpu_to_le16(2);

	le32_add_cpu(&ra2->open_log_count, 1);

	ntfs_fix_pre_write(&rh->rhdr, log->page_size);

	err = ntfs_sb_write_run(sbi, &ni->file.run, 0, rh, log->page_size, 0);
	if (!err)
		err = ntfs_sb_write_run(sbi, &log->ni->file.run, log->page_size,
					rh, log->page_size, 0);

	kfree(rh);
	if (err)
		goto out;

out:
	kfree(rst);
	if (lcb)
		lcb_put(lcb);

	/*
	 * Scan the Open Attribute Table to close all of
	 * the open attributes.
	 */
	oe = NULL;
	while ((oe = enum_rstbl(oatbl, oe))) {
		rno = ino_get(&oe->ref);

		if (oe->is_attr_name == 1) {
			kfree(oe->ptr);
			oe->ptr = NULL;
			continue;
		}

		if (oe->is_attr_name)
			continue;

		oa = oe->ptr;
		if (!oa)
			continue;

		run_close(&oa->run0);
		kfree(oa->attr);
		if (oa->ni)
			iput(&oa->ni->vfs_inode);
		kfree(oa);
	}

	kfree(trtbl);
	kfree(oatbl);
	kfree(dptbl);
	kfree(attr_names);
	kfree(rst_info.r_page);

	kfree(ra);
	kfree(log->one_page_buf);

	if (err)
		sbi->flags |= NTFS_FLAGS_NEED_REPLAY;

	if (err == -EROFS)
		err = 0;
	else if (log->set_dirty)
		ntfs_set_state(sbi, NTFS_DIRTY_ERROR);

	kfree(log);

	return err;
}
