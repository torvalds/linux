/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM btrfs

#if !defined(_TRACE_BTRFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BTRFS_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>

struct btrfs_root;
struct btrfs_fs_info;
struct btrfs_inode;
struct extent_map;
struct btrfs_file_extent_item;
struct btrfs_ordered_extent;
struct btrfs_delayed_ref_node;
struct btrfs_delayed_ref_head;
struct btrfs_block_group;
struct btrfs_free_cluster;
struct btrfs_chunk_map;
struct extent_buffer;
struct btrfs_work;
struct btrfs_workqueue;
struct btrfs_qgroup_extent_record;
struct btrfs_qgroup;
struct extent_io_tree;
struct prelim_ref;
struct btrfs_space_info;
struct btrfs_raid_bio;
struct raid56_bio_trace_info;
struct find_free_extent_ctl;

#define show_ref_type(type)						\
	__print_symbolic(type,						\
		{ BTRFS_TREE_BLOCK_REF_KEY, 	"TREE_BLOCK_REF" },	\
		{ BTRFS_EXTENT_DATA_REF_KEY, 	"EXTENT_DATA_REF" },	\
		{ BTRFS_SHARED_BLOCK_REF_KEY, 	"SHARED_BLOCK_REF" },	\
		{ BTRFS_SHARED_DATA_REF_KEY, 	"SHARED_DATA_REF" })

#define __show_root_type(obj)						\
	__print_symbolic_u64(obj,					\
		{ BTRFS_ROOT_TREE_OBJECTID, 	"ROOT_TREE"	},	\
		{ BTRFS_EXTENT_TREE_OBJECTID, 	"EXTENT_TREE"	},	\
		{ BTRFS_CHUNK_TREE_OBJECTID, 	"CHUNK_TREE"	},	\
		{ BTRFS_DEV_TREE_OBJECTID, 	"DEV_TREE"	},	\
		{ BTRFS_FS_TREE_OBJECTID, 	"FS_TREE"	},	\
		{ BTRFS_ROOT_TREE_DIR_OBJECTID, "ROOT_TREE_DIR"	},	\
		{ BTRFS_CSUM_TREE_OBJECTID, 	"CSUM_TREE"	},	\
		{ BTRFS_TREE_LOG_OBJECTID,	"TREE_LOG"	},	\
		{ BTRFS_QUOTA_TREE_OBJECTID,	"QUOTA_TREE"	},	\
		{ BTRFS_TREE_RELOC_OBJECTID,	"TREE_RELOC"	},	\
		{ BTRFS_UUID_TREE_OBJECTID,	"UUID_TREE"	},	\
		{ BTRFS_FREE_SPACE_TREE_OBJECTID, "FREE_SPACE_TREE" },	\
		{ BTRFS_BLOCK_GROUP_TREE_OBJECTID, "BLOCK_GROUP_TREE" },\
		{ BTRFS_DATA_RELOC_TREE_OBJECTID, "DATA_RELOC_TREE" })

#define show_root_type(obj)						\
	obj, ((obj >= BTRFS_DATA_RELOC_TREE_OBJECTID) ||		\
	      (obj >= BTRFS_ROOT_TREE_OBJECTID &&			\
	       obj <= BTRFS_QUOTA_TREE_OBJECTID)) ? __show_root_type(obj) : "-"

#define FLUSH_ACTIONS								\
	EM( BTRFS_RESERVE_NO_FLUSH,		"BTRFS_RESERVE_NO_FLUSH")	\
	EM( BTRFS_RESERVE_FLUSH_LIMIT,		"BTRFS_RESERVE_FLUSH_LIMIT")	\
	EM( BTRFS_RESERVE_FLUSH_ALL,		"BTRFS_RESERVE_FLUSH_ALL")	\
	EMe(BTRFS_RESERVE_FLUSH_ALL_STEAL,	"BTRFS_RESERVE_FLUSH_ALL_STEAL")

#define FI_TYPES							\
	EM( BTRFS_FILE_EXTENT_INLINE,		"INLINE")		\
	EM( BTRFS_FILE_EXTENT_REG,		"REG")			\
	EMe(BTRFS_FILE_EXTENT_PREALLOC,		"PREALLOC")

#define QGROUP_RSV_TYPES						\
	EM( BTRFS_QGROUP_RSV_DATA,		"DATA")			\
	EM( BTRFS_QGROUP_RSV_META_PERTRANS,	"META_PERTRANS")	\
	EMe(BTRFS_QGROUP_RSV_META_PREALLOC,	"META_PREALLOC")

#define IO_TREE_OWNER						    \
	EM( IO_TREE_FS_PINNED_EXTENTS, 	  "PINNED_EXTENTS")	    \
	EM( IO_TREE_FS_EXCLUDED_EXTENTS,  "EXCLUDED_EXTENTS")	    \
	EM( IO_TREE_BTREE_INODE_IO,	  "BTREE_INODE_IO")	    \
	EM( IO_TREE_INODE_IO,		  "INODE_IO")		    \
	EM( IO_TREE_RELOC_BLOCKS,	  "RELOC_BLOCKS")	    \
	EM( IO_TREE_TRANS_DIRTY_PAGES,	  "TRANS_DIRTY_PAGES")      \
	EM( IO_TREE_ROOT_DIRTY_LOG_PAGES, "ROOT_DIRTY_LOG_PAGES")   \
	EM( IO_TREE_INODE_FILE_EXTENT,	  "INODE_FILE_EXTENT")      \
	EM( IO_TREE_LOG_CSUM_RANGE,	  "LOG_CSUM_RANGE")         \
	EMe(IO_TREE_SELFTEST,		  "SELFTEST")

#define FLUSH_STATES							\
	EM( FLUSH_DELAYED_ITEMS_NR,	"FLUSH_DELAYED_ITEMS_NR")	\
	EM( FLUSH_DELAYED_ITEMS,	"FLUSH_DELAYED_ITEMS")		\
	EM( FLUSH_DELALLOC,		"FLUSH_DELALLOC")		\
	EM( FLUSH_DELALLOC_WAIT,	"FLUSH_DELALLOC_WAIT")		\
	EM( FLUSH_DELALLOC_FULL,	"FLUSH_DELALLOC_FULL")		\
	EM( FLUSH_DELAYED_REFS_NR,	"FLUSH_DELAYED_REFS_NR")	\
	EM( FLUSH_DELAYED_REFS,		"FLUSH_DELAYED_REFS")		\
	EM( ALLOC_CHUNK,		"ALLOC_CHUNK")			\
	EM( ALLOC_CHUNK_FORCE,		"ALLOC_CHUNK_FORCE")		\
	EM( RUN_DELAYED_IPUTS,		"RUN_DELAYED_IPUTS")		\
	EM( COMMIT_TRANS,		"COMMIT_TRANS")			\
	EMe(RESET_ZONES,		"RESET_ZONES")

/*
 * First define the enums in the above macros to be exported to userspace via
 * TRACE_DEFINE_ENUM().
 */

#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

FLUSH_ACTIONS
FI_TYPES
QGROUP_RSV_TYPES
IO_TREE_OWNER
FLUSH_STATES

/*
 * Now redefine the EM and EMe macros to map the enums to the strings that will
 * be printed in the output
 */

#undef EM
#undef EMe
#define EM(a, b)        {a, b},
#define EMe(a, b)       {a, b}


#define BTRFS_GROUP_FLAGS	\
	{ BTRFS_BLOCK_GROUP_DATA,	"DATA"},	\
	{ BTRFS_BLOCK_GROUP_SYSTEM,	"SYSTEM"},	\
	{ BTRFS_BLOCK_GROUP_METADATA,	"METADATA"},	\
	{ BTRFS_BLOCK_GROUP_RAID0,	"RAID0"}, 	\
	{ BTRFS_BLOCK_GROUP_RAID1,	"RAID1"}, 	\
	{ BTRFS_BLOCK_GROUP_DUP,	"DUP"}, 	\
	{ BTRFS_BLOCK_GROUP_RAID10,	"RAID10"}, 	\
	{ BTRFS_BLOCK_GROUP_RAID5,	"RAID5"},	\
	{ BTRFS_BLOCK_GROUP_RAID6,	"RAID6"}

#define EXTENT_FLAGS						\
	{ EXTENT_DIRTY,			"DIRTY"},		\
	{ EXTENT_UPTODATE,		"UPTODATE"},		\
	{ EXTENT_LOCKED,		"LOCKED"},		\
	{ EXTENT_NEW,			"NEW"},			\
	{ EXTENT_DELALLOC,		"DELALLOC"},		\
	{ EXTENT_DEFRAG,		"DEFRAG"},		\
	{ EXTENT_BOUNDARY,		"BOUNDARY"},		\
	{ EXTENT_NODATASUM,		"NODATASUM"},		\
	{ EXTENT_CLEAR_META_RESV,	"CLEAR_META_RESV"},	\
	{ EXTENT_NEED_WAIT,		"NEED_WAIT"},		\
	{ EXTENT_NORESERVE,		"NORESERVE"},		\
	{ EXTENT_QGROUP_RESERVED,	"QGROUP_RESERVED"},	\
	{ EXTENT_CLEAR_DATA_RESV,	"CLEAR_DATA_RESV"},	\
	{ EXTENT_DELALLOC_NEW,		"DELALLOC_NEW"}

#define BTRFS_FSID_SIZE 16
#define TP_STRUCT__entry_fsid __array(u8, fsid, BTRFS_FSID_SIZE)

#define TP_fast_assign_fsid(fs_info)					\
({									\
	if (fs_info)							\
		memcpy(__entry->fsid, fs_info->fs_devices->fsid,	\
		       BTRFS_FSID_SIZE);				\
	else								\
		memset(__entry->fsid, 0, BTRFS_FSID_SIZE);		\
})

#define TP_STRUCT__entry_btrfs(args...)					\
	TP_STRUCT__entry(						\
		TP_STRUCT__entry_fsid					\
		args)
#define TP_fast_assign_btrfs(fs_info, args...)				\
	TP_fast_assign(							\
		TP_fast_assign_fsid(fs_info);				\
		args)
#define TP_printk_btrfs(fmt, args...) \
	TP_printk("%pU: " fmt, __entry->fsid, args)

TRACE_EVENT(btrfs_transaction_commit,

	TP_PROTO(const struct btrfs_fs_info *fs_info),

	TP_ARGS(fs_info),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  generation		)
		__field(	u64,  root_objectid		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->generation	= fs_info->generation;
		__entry->root_objectid	= BTRFS_ROOT_TREE_OBJECTID;
	),

	TP_printk_btrfs("root=%llu(%s) gen=%llu",
		  show_root_type(__entry->root_objectid),
		  __entry->generation)
);

DECLARE_EVENT_CLASS(btrfs__inode,

	TP_PROTO(const struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  ino			)
		__field(	u64,  blocks			)
		__field(	u64,  disk_i_size		)
		__field(	u64,  generation		)
		__field(	u64,  last_trans		)
		__field(	u64,  logged_trans		)
		__field(	u64,  root_objectid		)
	),

	TP_fast_assign_btrfs(btrfs_sb(inode->i_sb),
		__entry->ino	= btrfs_ino(BTRFS_I(inode));
		__entry->blocks	= inode->i_blocks;
		__entry->disk_i_size  = BTRFS_I(inode)->disk_i_size;
		__entry->generation = BTRFS_I(inode)->generation;
		__entry->last_trans = BTRFS_I(inode)->last_trans;
		__entry->logged_trans = BTRFS_I(inode)->logged_trans;
		__entry->root_objectid =
				BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk_btrfs("root=%llu(%s) gen=%llu ino=%llu blocks=%llu "
		  "disk_i_size=%llu last_trans=%llu logged_trans=%llu",
		  show_root_type(__entry->root_objectid),
		  __entry->generation,
		  __entry->ino,
		  __entry->blocks,
		  __entry->disk_i_size,
		  __entry->last_trans,
		  __entry->logged_trans)
);

DEFINE_EVENT(btrfs__inode, btrfs_inode_new,

	TP_PROTO(const struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(btrfs__inode, btrfs_inode_request,

	TP_PROTO(const struct inode *inode),

	TP_ARGS(inode)
);

DEFINE_EVENT(btrfs__inode, btrfs_inode_evict,

	TP_PROTO(const struct inode *inode),

	TP_ARGS(inode)
);

#define __show_map_type(type)						\
	__print_symbolic_u64(type,					\
		{ EXTENT_MAP_LAST_BYTE, "LAST_BYTE" 	},		\
		{ EXTENT_MAP_HOLE, 	"HOLE" 		},		\
		{ EXTENT_MAP_INLINE,	"INLINE"	})

#define show_map_type(type)			\
	type, (type >= EXTENT_MAP_LAST_BYTE) ? "-" :  __show_map_type(type)

#define show_map_flags(flag)						\
	__print_flags(flag, "|",					\
		{ EXTENT_FLAG_PINNED,		"PINNED"	},\
		{ EXTENT_FLAG_COMPRESS_ZLIB,	"COMPRESS_ZLIB"	},\
		{ EXTENT_FLAG_COMPRESS_LZO,	"COMPRESS_LZO"	},\
		{ EXTENT_FLAG_COMPRESS_ZSTD,	"COMPRESS_ZSTD"	},\
		{ EXTENT_FLAG_PREALLOC,		"PREALLOC"	},\
		{ EXTENT_FLAG_LOGGING,		"LOGGING"	})

TRACE_EVENT_CONDITION(btrfs_get_extent,

	TP_PROTO(const struct btrfs_root *root, const struct btrfs_inode *inode,
		 const struct extent_map *map),

	TP_ARGS(root, inode, map),

	TP_CONDITION(map),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  root_objectid	)
		__field(	u64,  ino		)
		__field(	u64,  start		)
		__field(	u64,  len		)
		__field(	u32,  flags		)
		__field(	int,  refs		)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->root_objectid	= root->root_key.objectid;
		__entry->ino		= btrfs_ino(inode);
		__entry->start		= map->start;
		__entry->len		= map->len;
		__entry->flags		= map->flags;
		__entry->refs		= refcount_read(&map->refs);
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu start=%llu len=%llu flags=%s refs=%u",
		  show_root_type(__entry->root_objectid),
		  __entry->ino,
		  __entry->start,
		  __entry->len,
		  show_map_flags(__entry->flags),
		  __entry->refs)
);

TRACE_EVENT(btrfs_handle_em_exist,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		const struct extent_map *existing, const struct extent_map *map,
		u64 start, u64 len),

	TP_ARGS(fs_info, existing, map, start, len),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  e_start		)
		__field(	u64,  e_len		)
		__field(	u64,  map_start		)
		__field(	u64,  map_len		)
		__field(	u64,  start		)
		__field(	u64,  len		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->e_start	= existing->start;
		__entry->e_len		= existing->len;
		__entry->map_start	= map->start;
		__entry->map_len	= map->len;
		__entry->start		= start;
		__entry->len		= len;
	),

	TP_printk_btrfs("start=%llu len=%llu "
		  "existing(start=%llu len=%llu) "
		  "em(start=%llu len=%llu)",
		  __entry->start,
		  __entry->len,
		  __entry->e_start,
		  __entry->e_len,
		  __entry->map_start,
		  __entry->map_len)
);

/* file extent item */
DECLARE_EVENT_CLASS(btrfs__file_extent_item_regular,

	TP_PROTO(const struct btrfs_inode *bi, const struct extent_buffer *l,
		 const struct btrfs_file_extent_item *fi, u64 start),

	TP_ARGS(bi, l, fi, start),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	root_obj	)
		__field(	u64,	ino		)
		__field(	loff_t,	isize		)
		__field(	u64,	disk_isize	)
		__field(	u64,	num_bytes	)
		__field(	u64,	ram_bytes	)
		__field(	u64,	disk_bytenr	)
		__field(	u64,	disk_num_bytes	)
		__field(	u64,	extent_offset	)
		__field(	u8,	extent_type	)
		__field(	u8,	compression	)
		__field(	u64,	extent_start	)
		__field(	u64,	extent_end	)
	),

	TP_fast_assign_btrfs(bi->root->fs_info,
		__entry->root_obj	= bi->root->root_key.objectid;
		__entry->ino		= btrfs_ino(bi);
		__entry->isize		= bi->vfs_inode.i_size;
		__entry->disk_isize	= bi->disk_i_size;
		__entry->num_bytes	= btrfs_file_extent_num_bytes(l, fi);
		__entry->ram_bytes	= btrfs_file_extent_ram_bytes(l, fi);
		__entry->disk_bytenr	= btrfs_file_extent_disk_bytenr(l, fi);
		__entry->disk_num_bytes	= btrfs_file_extent_disk_num_bytes(l, fi);
		__entry->extent_offset	= btrfs_file_extent_offset(l, fi);
		__entry->extent_type	= btrfs_file_extent_type(l, fi);
		__entry->compression	= btrfs_file_extent_compression(l, fi);
		__entry->extent_start	= start;
		__entry->extent_end	= (start + __entry->num_bytes);
	),

	TP_printk_btrfs(
		"root=%llu(%s) inode=%llu size=%llu disk_isize=%llu "
		"file extent range=[%llu %llu] "
		"(num_bytes=%llu ram_bytes=%llu disk_bytenr=%llu "
		"disk_num_bytes=%llu extent_offset=%llu type=%s "
		"compression=%u",
		show_root_type(__entry->root_obj), __entry->ino,
		__entry->isize,
		__entry->disk_isize, __entry->extent_start,
		__entry->extent_end, __entry->num_bytes, __entry->ram_bytes,
		__entry->disk_bytenr, __entry->disk_num_bytes,
		__entry->extent_offset, __print_symbolic(__entry->extent_type, FI_TYPES),
		__entry->compression)
);

DECLARE_EVENT_CLASS(
	btrfs__file_extent_item_inline,

	TP_PROTO(const struct btrfs_inode *bi, const struct extent_buffer *l,
		 const struct btrfs_file_extent_item *fi, int slot, u64 start),

	TP_ARGS(bi, l, fi, slot,  start),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	root_obj	)
		__field(	u64,	ino		)
		__field(	loff_t,	isize		)
		__field(	u64,	disk_isize	)
		__field(	u8,	extent_type	)
		__field(	u8,	compression	)
		__field(	u64,	extent_start	)
		__field(	u64,	extent_end	)
	),

	TP_fast_assign_btrfs(
		bi->root->fs_info,
		__entry->root_obj	= bi->root->root_key.objectid;
		__entry->ino		= btrfs_ino(bi);
		__entry->isize		= bi->vfs_inode.i_size;
		__entry->disk_isize	= bi->disk_i_size;
		__entry->extent_type	= btrfs_file_extent_type(l, fi);
		__entry->compression	= btrfs_file_extent_compression(l, fi);
		__entry->extent_start	= start;
		__entry->extent_end	= (start + btrfs_file_extent_ram_bytes(l, fi));
	),

	TP_printk_btrfs(
		"root=%llu(%s) inode=%llu size=%llu disk_isize=%llu "
		"file extent range=[%llu %llu] "
		"extent_type=%s compression=%u",
		show_root_type(__entry->root_obj), __entry->ino, __entry->isize,
		__entry->disk_isize, __entry->extent_start,
		__entry->extent_end, __print_symbolic(__entry->extent_type, FI_TYPES),
		__entry->compression)
);

DEFINE_EVENT(
	btrfs__file_extent_item_regular, btrfs_get_extent_show_fi_regular,

	TP_PROTO(const struct btrfs_inode *bi, const struct extent_buffer *l,
		 const struct btrfs_file_extent_item *fi, u64 start),

	TP_ARGS(bi, l, fi, start)
);

DEFINE_EVENT(
	btrfs__file_extent_item_regular, btrfs_truncate_show_fi_regular,

	TP_PROTO(const struct btrfs_inode *bi, const struct extent_buffer *l,
		 const struct btrfs_file_extent_item *fi, u64 start),

	TP_ARGS(bi, l, fi, start)
);

DEFINE_EVENT(
	btrfs__file_extent_item_inline, btrfs_get_extent_show_fi_inline,

	TP_PROTO(const struct btrfs_inode *bi, const struct extent_buffer *l,
		 const struct btrfs_file_extent_item *fi, int slot, u64 start),

	TP_ARGS(bi, l, fi, slot, start)
);

DEFINE_EVENT(
	btrfs__file_extent_item_inline, btrfs_truncate_show_fi_inline,

	TP_PROTO(const struct btrfs_inode *bi, const struct extent_buffer *l,
		 const struct btrfs_file_extent_item *fi, int slot, u64 start),

	TP_ARGS(bi, l, fi, slot, start)
);

#define show_ordered_flags(flags)					   \
	__print_flags(flags, "|",					   \
		{ (1 << BTRFS_ORDERED_REGULAR), 	"REGULAR" 	}, \
		{ (1 << BTRFS_ORDERED_NOCOW), 		"NOCOW" 	}, \
		{ (1 << BTRFS_ORDERED_PREALLOC), 	"PREALLOC" 	}, \
		{ (1 << BTRFS_ORDERED_COMPRESSED), 	"COMPRESSED" 	}, \
		{ (1 << BTRFS_ORDERED_DIRECT),	 	"DIRECT" 	}, \
		{ (1 << BTRFS_ORDERED_IO_DONE), 	"IO_DONE" 	}, \
		{ (1 << BTRFS_ORDERED_COMPLETE), 	"COMPLETE" 	}, \
		{ (1 << BTRFS_ORDERED_IOERR), 		"IOERR" 	}, \
		{ (1 << BTRFS_ORDERED_TRUNCATED), 	"TRUNCATED"	})


DECLARE_EVENT_CLASS(btrfs__ordered_extent,

	TP_PROTO(const struct btrfs_inode *inode,
		 const struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  ino		)
		__field(	u64,  file_offset	)
		__field(	u64,  start		)
		__field(	u64,  len		)
		__field(	u64,  disk_len		)
		__field(	u64,  bytes_left	)
		__field(	unsigned long,  flags	)
		__field(	int,  compress_type	)
		__field(	int,  refs		)
		__field(	u64,  root_objectid	)
		__field(	u64,  truncated_len	)
	),

	TP_fast_assign_btrfs(inode->root->fs_info,
		__entry->ino 		= btrfs_ino(inode);
		__entry->file_offset	= ordered->file_offset;
		__entry->start		= ordered->disk_bytenr;
		__entry->len		= ordered->num_bytes;
		__entry->disk_len	= ordered->disk_num_bytes;
		__entry->bytes_left	= ordered->bytes_left;
		__entry->flags		= ordered->flags;
		__entry->compress_type	= ordered->compress_type;
		__entry->refs		= refcount_read(&ordered->refs);
		__entry->root_objectid	= inode->root->root_key.objectid;
		__entry->truncated_len	= ordered->truncated_len;
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu file_offset=%llu "
		  "start=%llu len=%llu disk_len=%llu "
		  "truncated_len=%llu "
		  "bytes_left=%llu flags=%s compress_type=%d "
		  "refs=%d",
		  show_root_type(__entry->root_objectid),
		  __entry->ino,
		  __entry->file_offset,
		  __entry->start,
		  __entry->len,
		  __entry->disk_len,
		  __entry->truncated_len,
		  __entry->bytes_left,
		  show_ordered_flags(__entry->flags),
		  __entry->compress_type, __entry->refs)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_add,

	TP_PROTO(const struct btrfs_inode *inode,
		 const struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_remove,

	TP_PROTO(const struct btrfs_inode *inode,
		 const struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_start,

	TP_PROTO(const struct btrfs_inode *inode,
		 const struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_put,

	TP_PROTO(const struct btrfs_inode *inode,
		 const struct btrfs_ordered_extent *ordered),

	TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_lookup,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_lookup_range,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_lookup_first_range,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_lookup_for_logging,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_lookup_first,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_split,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_dec_test_pending,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

DEFINE_EVENT(btrfs__ordered_extent, btrfs_ordered_extent_mark_finished,

	     TP_PROTO(const struct btrfs_inode *inode,
		      const struct btrfs_ordered_extent *ordered),

	     TP_ARGS(inode, ordered)
);

TRACE_EVENT(btrfs_finish_ordered_extent,

	TP_PROTO(const struct btrfs_inode *inode, u64 start, u64 len,
		 bool uptodate),

	TP_ARGS(inode, start, len, uptodate),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	 ino		)
		__field(	u64,	 start		)
		__field(	u64,	 len		)
		__field(	bool,	 uptodate	)
		__field(	u64,	 root_objectid	)
	),

	TP_fast_assign_btrfs(inode->root->fs_info,
		__entry->ino	= btrfs_ino(inode);
		__entry->start	= start;
		__entry->len	= len;
		__entry->uptodate = uptodate;
		__entry->root_objectid = inode->root->root_key.objectid;
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu start=%llu len=%llu uptodate=%d",
		  show_root_type(__entry->root_objectid),
		  __entry->ino, __entry->start,
		  __entry->len, !!__entry->uptodate)
);

DECLARE_EVENT_CLASS(btrfs__writepage,

	TP_PROTO(const struct folio *folio, const struct inode *inode,
		 const struct writeback_control *wbc),

	TP_ARGS(folio, inode, wbc),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	ino			)
		__field(	pgoff_t,  index			)
		__field(	long,   nr_to_write		)
		__field(	long,   pages_skipped		)
		__field(	loff_t, range_start		)
		__field(	loff_t, range_end		)
		__field(	char,   for_kupdate		)
		__field(	char,   for_reclaim		)
		__field(	char,   range_cyclic		)
		__field(	unsigned long,  writeback_index	)
		__field(	u64,    root_objectid		)
	),

	TP_fast_assign_btrfs(btrfs_sb(inode->i_sb),
		__entry->ino		= btrfs_ino(BTRFS_I(inode));
		__entry->index		= folio->index;
		__entry->nr_to_write	= wbc->nr_to_write;
		__entry->pages_skipped	= wbc->pages_skipped;
		__entry->range_start	= wbc->range_start;
		__entry->range_end	= wbc->range_end;
		__entry->for_kupdate	= wbc->for_kupdate;
		__entry->for_reclaim	= wbc->for_reclaim;
		__entry->range_cyclic	= wbc->range_cyclic;
		__entry->writeback_index = inode->i_mapping->writeback_index;
		__entry->root_objectid	=
				 BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu page_index=%lu "
		  "nr_to_write=%ld pages_skipped=%ld range_start=%llu "
		  "range_end=%llu for_kupdate=%d "
		  "for_reclaim=%d range_cyclic=%d writeback_index=%lu",
		  show_root_type(__entry->root_objectid),
		  __entry->ino, __entry->index,
		  __entry->nr_to_write, __entry->pages_skipped,
		  __entry->range_start, __entry->range_end,
		  __entry->for_kupdate,
		  __entry->for_reclaim, __entry->range_cyclic,
		  __entry->writeback_index)
);

DEFINE_EVENT(btrfs__writepage, extent_writepage,

	TP_PROTO(const struct folio *folio, const struct inode *inode,
		 const struct writeback_control *wbc),

	TP_ARGS(folio, inode, wbc)
);

TRACE_EVENT(btrfs_writepage_end_io_hook,

	TP_PROTO(const struct btrfs_inode *inode, u64 start, u64 end,
		 int uptodate),

	TP_ARGS(inode, start, end, uptodate),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	 ino		)
		__field(	u64,	 start		)
		__field(	u64,	 end		)
		__field(	int,	 uptodate	)
		__field(	u64,    root_objectid	)
	),

	TP_fast_assign_btrfs(inode->root->fs_info,
		__entry->ino	= btrfs_ino(inode);
		__entry->start	= start;
		__entry->end	= end;
		__entry->uptodate = uptodate;
		__entry->root_objectid = inode->root->root_key.objectid;
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu start=%llu end=%llu uptodate=%d",
		  show_root_type(__entry->root_objectid),
		  __entry->ino, __entry->start,
		  __entry->end, __entry->uptodate)
);

TRACE_EVENT(btrfs_sync_file,

	TP_PROTO(const struct file *file, int datasync),

	TP_ARGS(file, datasync),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	ino		)
		__field(	u64,	parent		)
		__field(	int,    datasync	)
		__field(	u64,    root_objectid	)
	),

	TP_fast_assign(
		const struct dentry *dentry = file->f_path.dentry;
		const struct inode *inode = d_inode(dentry);

		TP_fast_assign_fsid(btrfs_sb(file->f_path.dentry->d_sb));
		__entry->ino		= btrfs_ino(BTRFS_I(inode));
		__entry->parent		= btrfs_ino(BTRFS_I(d_inode(dentry->d_parent)));
		__entry->datasync	= datasync;
		__entry->root_objectid	=
				 BTRFS_I(inode)->root->root_key.objectid;
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu parent=%llu datasync=%d",
		  show_root_type(__entry->root_objectid),
		  __entry->ino,
		  __entry->parent,
		  __entry->datasync)
);

TRACE_EVENT(btrfs_sync_fs,

	TP_PROTO(const struct btrfs_fs_info *fs_info, int wait),

	TP_ARGS(fs_info, wait),

	TP_STRUCT__entry_btrfs(
		__field(	int,  wait		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->wait	= wait;
	),

	TP_printk_btrfs("wait=%d", __entry->wait)
);

TRACE_EVENT(btrfs_add_block_group,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_block_group *block_group, int create),

	TP_ARGS(fs_info, block_group, create),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	offset			)
		__field(	u64,	size			)
		__field(	u64,	flags			)
		__field(	u64,	bytes_used		)
		__field(	u64,	bytes_super		)
		__field(	int,	create			)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->offset		= block_group->start;
		__entry->size		= block_group->length;
		__entry->flags		= block_group->flags;
		__entry->bytes_used	= block_group->used;
		__entry->bytes_super	= block_group->bytes_super;
		__entry->create		= create;
	),

	TP_printk_btrfs("block_group offset=%llu size=%llu "
		  "flags=%llu(%s) bytes_used=%llu bytes_super=%llu "
		  "create=%d",
		  __entry->offset,
		  __entry->size,
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS),
		  __entry->bytes_used,
		  __entry->bytes_super, __entry->create)
);

#define show_ref_action(action)						\
	__print_symbolic(action,					\
		{ BTRFS_ADD_DELAYED_REF,    "ADD_DELAYED_REF" },	\
		{ BTRFS_DROP_DELAYED_REF,   "DROP_DELAYED_REF" },	\
		{ BTRFS_ADD_DELAYED_EXTENT, "ADD_DELAYED_EXTENT" }, 	\
		{ BTRFS_UPDATE_DELAYED_HEAD, "UPDATE_DELAYED_HEAD" })
			

DECLARE_EVENT_CLASS(btrfs_delayed_tree_ref,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_node *ref),

	TP_ARGS(fs_info, ref),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
		__field(	int,  action		) 
		__field(	u64,  parent		)
		__field(	u64,  ref_root		)
		__field(	int,  level		)
		__field(	int,  type		)
		__field(	u64,  seq		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->bytenr		= ref->bytenr;
		__entry->num_bytes	= ref->num_bytes;
		__entry->action		= ref->action;
		__entry->parent		= ref->parent;
		__entry->ref_root	= ref->ref_root;
		__entry->level		= ref->tree_ref.level;
		__entry->type		= ref->type;
		__entry->seq		= ref->seq;
	),

	TP_printk_btrfs("bytenr=%llu num_bytes=%llu action=%s "
		  "parent=%llu(%s) ref_root=%llu(%s) level=%d "
		  "type=%s seq=%llu",
		  __entry->bytenr,
		  __entry->num_bytes,
		  show_ref_action(__entry->action),
		  show_root_type(__entry->parent),
		  show_root_type(__entry->ref_root),
		  __entry->level, show_ref_type(__entry->type),
		  __entry->seq)
);

DEFINE_EVENT(btrfs_delayed_tree_ref,  add_delayed_tree_ref,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_node *ref),

	TP_ARGS(fs_info, ref)
);

DEFINE_EVENT(btrfs_delayed_tree_ref,  run_delayed_tree_ref,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_node *ref),

	TP_ARGS(fs_info, ref)
);

DECLARE_EVENT_CLASS(btrfs_delayed_data_ref,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_node *ref),

	TP_ARGS(fs_info, ref),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
		__field(	int,  action		) 
		__field(	u64,  parent		)
		__field(	u64,  ref_root		)
		__field(	u64,  owner		)
		__field(	u64,  offset		)
		__field(	int,  type		)
		__field(	u64,  seq		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->bytenr		= ref->bytenr;
		__entry->num_bytes	= ref->num_bytes;
		__entry->action		= ref->action;
		__entry->parent		= ref->parent;
		__entry->ref_root	= ref->ref_root;
		__entry->owner		= ref->data_ref.objectid;
		__entry->offset		= ref->data_ref.offset;
		__entry->type		= ref->type;
		__entry->seq		= ref->seq;
	),

	TP_printk_btrfs("bytenr=%llu num_bytes=%llu action=%s "
		  "parent=%llu(%s) ref_root=%llu(%s) owner=%llu "
		  "offset=%llu type=%s seq=%llu",
		  __entry->bytenr,
		  __entry->num_bytes,
		  show_ref_action(__entry->action),
		  show_root_type(__entry->parent),
		  show_root_type(__entry->ref_root),
		  __entry->owner,
		  __entry->offset,
		  show_ref_type(__entry->type),
		  __entry->seq)
);

DEFINE_EVENT(btrfs_delayed_data_ref,  add_delayed_data_ref,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_node *ref),

	TP_ARGS(fs_info, ref)
);

DEFINE_EVENT(btrfs_delayed_data_ref,  run_delayed_data_ref,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_node *ref),

	TP_ARGS(fs_info, ref)
);

DECLARE_EVENT_CLASS(btrfs_delayed_ref_head,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_head *head_ref,
		 int action),

	TP_ARGS(fs_info, head_ref, action),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
		__field(	int,  action		) 
		__field(	int,  is_data		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->bytenr		= head_ref->bytenr;
		__entry->num_bytes	= head_ref->num_bytes;
		__entry->action		= action;
		__entry->is_data	= head_ref->is_data;
	),

	TP_printk_btrfs("bytenr=%llu num_bytes=%llu action=%s is_data=%d",
		  __entry->bytenr,
		  __entry->num_bytes,
		  show_ref_action(__entry->action),
		  __entry->is_data)
);

DEFINE_EVENT(btrfs_delayed_ref_head,  add_delayed_ref_head,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_head *head_ref,
		 int action),

	TP_ARGS(fs_info, head_ref, action)
);

DEFINE_EVENT(btrfs_delayed_ref_head,  run_delayed_ref_head,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_delayed_ref_head *head_ref,
		 int action),

	TP_ARGS(fs_info, head_ref, action)
);

#define show_chunk_type(type)					\
	__print_flags(type, "|",				\
		{ BTRFS_BLOCK_GROUP_DATA, 	"DATA"	},	\
		{ BTRFS_BLOCK_GROUP_SYSTEM, 	"SYSTEM"},	\
		{ BTRFS_BLOCK_GROUP_METADATA, 	"METADATA"},	\
		{ BTRFS_BLOCK_GROUP_RAID0, 	"RAID0" },	\
		{ BTRFS_BLOCK_GROUP_RAID1, 	"RAID1" },	\
		{ BTRFS_BLOCK_GROUP_DUP, 	"DUP"	},	\
		{ BTRFS_BLOCK_GROUP_RAID10, 	"RAID10"},	\
		{ BTRFS_BLOCK_GROUP_RAID5, 	"RAID5"	},	\
		{ BTRFS_BLOCK_GROUP_RAID6, 	"RAID6"	})

DECLARE_EVENT_CLASS(btrfs__chunk,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_chunk_map *map, u64 offset, u64 size),

	TP_ARGS(fs_info, map, offset, size),

	TP_STRUCT__entry_btrfs(
		__field(	int,  num_stripes		)
		__field(	u64,  type			)
		__field(	int,  sub_stripes		)
		__field(	u64,  offset			)
		__field(	u64,  size			)
		__field(	u64,  root_objectid		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->num_stripes	= map->num_stripes;
		__entry->type		= map->type;
		__entry->sub_stripes	= map->sub_stripes;
		__entry->offset		= offset;
		__entry->size		= size;
		__entry->root_objectid	= fs_info->chunk_root->root_key.objectid;
	),

	TP_printk_btrfs("root=%llu(%s) offset=%llu size=%llu "
		  "num_stripes=%d sub_stripes=%d type=%s",
		  show_root_type(__entry->root_objectid),
		  __entry->offset,
		  __entry->size,
		  __entry->num_stripes, __entry->sub_stripes,
		  show_chunk_type(__entry->type))
);

DEFINE_EVENT(btrfs__chunk,  btrfs_chunk_alloc,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_chunk_map *map, u64 offset, u64 size),

	TP_ARGS(fs_info, map, offset, size)
);

DEFINE_EVENT(btrfs__chunk,  btrfs_chunk_free,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_chunk_map *map, u64 offset, u64 size),

	TP_ARGS(fs_info, map, offset, size)
);

TRACE_EVENT(btrfs_cow_block,

	TP_PROTO(const struct btrfs_root *root, const struct extent_buffer *buf,
		 const struct extent_buffer *cow),

	TP_ARGS(root, buf, cow),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  root_objectid		)
		__field(	u64,  buf_start			)
		__field(	int,  refs			)
		__field(	u64,  cow_start			)
		__field(	int,  buf_level			)
		__field(	int,  cow_level			)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->root_objectid	= root->root_key.objectid;
		__entry->buf_start	= buf->start;
		__entry->refs		= atomic_read(&buf->refs);
		__entry->cow_start	= cow->start;
		__entry->buf_level	= btrfs_header_level(buf);
		__entry->cow_level	= btrfs_header_level(cow);
	),

	TP_printk_btrfs("root=%llu(%s) refs=%d orig_buf=%llu "
		  "(orig_level=%d) cow_buf=%llu (cow_level=%d)",
		  show_root_type(__entry->root_objectid),
		  __entry->refs,
		  __entry->buf_start,
		  __entry->buf_level,
		  __entry->cow_start,
		  __entry->cow_level)
);

TRACE_EVENT(btrfs_space_reservation,

	TP_PROTO(const struct btrfs_fs_info *fs_info, const char *type, u64 val,
		 u64 bytes, int reserve),

	TP_ARGS(fs_info, type, val, bytes, reserve),

	TP_STRUCT__entry_btrfs(
		__string(	type,	type			)
		__field(	u64,	val			)
		__field(	u64,	bytes			)
		__field(	int,	reserve			)
	),

	TP_fast_assign_btrfs(fs_info,
		__assign_str(type);
		__entry->val		= val;
		__entry->bytes		= bytes;
		__entry->reserve	= reserve;
	),

	TP_printk_btrfs("%s: %llu %s %llu", __get_str(type), __entry->val,
			__entry->reserve ? "reserve" : "release",
			__entry->bytes)
);

TRACE_EVENT(btrfs_trigger_flush,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 flags, u64 bytes,
		 int flush, const char *reason),

	TP_ARGS(fs_info, flags, bytes, flush, reason),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	flags			)
		__field(	u64,	bytes			)
		__field(	int,	flush			)
		__string(	reason,	reason			)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->flags	= flags;
		__entry->bytes	= bytes;
		__entry->flush	= flush;
		__assign_str(reason);
	),

	TP_printk_btrfs("%s: flush=%d(%s) flags=%llu(%s) bytes=%llu",
		  __get_str(reason), __entry->flush,
		  __print_symbolic(__entry->flush, FLUSH_ACTIONS),
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS),
		  __entry->bytes)
);


TRACE_EVENT(btrfs_flush_space,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 flags, u64 num_bytes,
		 int state, int ret, bool for_preempt),

	TP_ARGS(fs_info, flags, num_bytes, state, ret, for_preempt),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	flags			)
		__field(	u64,	num_bytes		)
		__field(	int,	state			)
		__field(	int,	ret			)
		__field(       bool,	for_preempt		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->flags		=	flags;
		__entry->num_bytes	=	num_bytes;
		__entry->state		=	state;
		__entry->ret		=	ret;
		__entry->for_preempt	=	for_preempt;
	),

	TP_printk_btrfs("state=%d(%s) flags=%llu(%s) num_bytes=%llu ret=%d for_preempt=%d",
		  __entry->state,
		  __print_symbolic(__entry->state, FLUSH_STATES),
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS),
		  __entry->num_bytes, __entry->ret, __entry->for_preempt)
);

DECLARE_EVENT_CLASS(btrfs__reserved_extent,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 start, u64 len),

	TP_ARGS(fs_info, start, len),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  start			)
		__field(	u64,  len			)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->start		= start;
		__entry->len		= len;
	),

	TP_printk_btrfs("root=%llu(%s) start=%llu len=%llu",
		  show_root_type(BTRFS_EXTENT_TREE_OBJECTID),
		  __entry->start,
		  __entry->len)
);

DEFINE_EVENT(btrfs__reserved_extent,  btrfs_reserved_extent_alloc,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 start, u64 len),

	TP_ARGS(fs_info, start, len)
);

DEFINE_EVENT(btrfs__reserved_extent,  btrfs_reserved_extent_free,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 start, u64 len),

	TP_ARGS(fs_info, start, len)
);

TRACE_EVENT(find_free_extent,

	TP_PROTO(const struct btrfs_root *root,
		 const struct find_free_extent_ctl *ffe_ctl),

	TP_ARGS(root, ffe_ctl),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	root_objectid		)
		__field(	u64,	num_bytes		)
		__field(	u64,	empty_size		)
		__field(	u64,	flags			)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->root_objectid	= root->root_key.objectid;
		__entry->num_bytes	= ffe_ctl->num_bytes;
		__entry->empty_size	= ffe_ctl->empty_size;
		__entry->flags		= ffe_ctl->flags;
	),

	TP_printk_btrfs("root=%llu(%s) len=%llu empty_size=%llu flags=%llu(%s)",
		  show_root_type(__entry->root_objectid),
		  __entry->num_bytes, __entry->empty_size, __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				 BTRFS_GROUP_FLAGS))
);

TRACE_EVENT(find_free_extent_search_loop,

	TP_PROTO(const struct btrfs_root *root,
		 const struct find_free_extent_ctl *ffe_ctl),

	TP_ARGS(root, ffe_ctl),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	root_objectid		)
		__field(	u64,	num_bytes		)
		__field(	u64,	empty_size		)
		__field(	u64,	flags			)
		__field(	u64,	loop			)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->root_objectid	= root->root_key.objectid;
		__entry->num_bytes	= ffe_ctl->num_bytes;
		__entry->empty_size	= ffe_ctl->empty_size;
		__entry->flags		= ffe_ctl->flags;
		__entry->loop		= ffe_ctl->loop;
	),

	TP_printk_btrfs("root=%llu(%s) len=%llu empty_size=%llu flags=%llu(%s) loop=%llu",
		  show_root_type(__entry->root_objectid),
		  __entry->num_bytes, __entry->empty_size, __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|", BTRFS_GROUP_FLAGS),
		  __entry->loop)
);

TRACE_EVENT(find_free_extent_have_block_group,

	TP_PROTO(const struct btrfs_root *root,
		 const struct find_free_extent_ctl *ffe_ctl,
		 const struct btrfs_block_group *block_group),

	TP_ARGS(root, ffe_ctl, block_group),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	root_objectid		)
		__field(	u64,	num_bytes		)
		__field(	u64,	empty_size		)
		__field(	u64,	flags			)
		__field(	u64,	loop			)
		__field(	bool,	hinted			)
		__field(	u64,	bg_start		)
		__field(	u64,	bg_flags		)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->root_objectid	= root->root_key.objectid;
		__entry->num_bytes	= ffe_ctl->num_bytes;
		__entry->empty_size	= ffe_ctl->empty_size;
		__entry->flags		= ffe_ctl->flags;
		__entry->loop		= ffe_ctl->loop;
		__entry->hinted		= ffe_ctl->hinted;
		__entry->bg_start	= block_group->start;
		__entry->bg_flags	= block_group->flags;
	),

	TP_printk_btrfs(
"root=%llu(%s) len=%llu empty_size=%llu flags=%llu(%s) loop=%llu hinted=%d block_group=%llu bg_flags=%llu(%s)",
		  show_root_type(__entry->root_objectid),
		  __entry->num_bytes, __entry->empty_size, __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|", BTRFS_GROUP_FLAGS),
		  __entry->loop, __entry->hinted,
		  __entry->bg_start, __entry->bg_flags,
		  __print_flags((unsigned long)__entry->bg_flags, "|",
				 BTRFS_GROUP_FLAGS))
);

DECLARE_EVENT_CLASS(btrfs__reserve_extent,

	TP_PROTO(const struct btrfs_block_group *block_group,
		 const struct find_free_extent_ctl *ffe_ctl),

	TP_ARGS(block_group, ffe_ctl),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	bg_objectid		)
		__field(	u64,	flags			)
		__field(	int,	bg_size_class		)
		__field(	u64,	start			)
		__field(	u64,	len			)
		__field(	u64,	loop			)
		__field(	bool,	hinted			)
		__field(	int,	size_class		)
	),

	TP_fast_assign_btrfs(block_group->fs_info,
		__entry->bg_objectid	= block_group->start;
		__entry->flags		= block_group->flags;
		__entry->bg_size_class	= block_group->size_class;
		__entry->start		= ffe_ctl->search_start;
		__entry->len		= ffe_ctl->num_bytes;
		__entry->loop		= ffe_ctl->loop;
		__entry->hinted		= ffe_ctl->hinted;
		__entry->size_class	= ffe_ctl->size_class;
	),

	TP_printk_btrfs(
"root=%llu(%s) block_group=%llu flags=%llu(%s) bg_size_class=%d start=%llu len=%llu loop=%llu hinted=%d size_class=%d",
		  show_root_type(BTRFS_EXTENT_TREE_OBJECTID),
		  __entry->bg_objectid,
		  __entry->flags, __print_flags((unsigned long)__entry->flags,
						"|", BTRFS_GROUP_FLAGS),
		  __entry->bg_size_class, __entry->start, __entry->len,
		  __entry->loop, __entry->hinted, __entry->size_class)
);

DEFINE_EVENT(btrfs__reserve_extent, btrfs_reserve_extent,

	TP_PROTO(const struct btrfs_block_group *block_group,
		 const struct find_free_extent_ctl *ffe_ctl),

	TP_ARGS(block_group, ffe_ctl)
);

DEFINE_EVENT(btrfs__reserve_extent, btrfs_reserve_extent_cluster,

	TP_PROTO(const struct btrfs_block_group *block_group,
		 const struct find_free_extent_ctl *ffe_ctl),

	TP_ARGS(block_group, ffe_ctl)
);

TRACE_EVENT(btrfs_find_cluster,

	TP_PROTO(const struct btrfs_block_group *block_group, u64 start,
		 u64 bytes, u64 empty_size, u64 min_bytes),

	TP_ARGS(block_group, start, bytes, empty_size, min_bytes),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	bg_objectid		)
		__field(	u64,	flags			)
		__field(	u64,	start			)
		__field(	u64,	bytes			)
		__field(	u64,	empty_size		)
		__field(	u64,	min_bytes		)
	),

	TP_fast_assign_btrfs(block_group->fs_info,
		__entry->bg_objectid	= block_group->start;
		__entry->flags		= block_group->flags;
		__entry->start		= start;
		__entry->bytes		= bytes;
		__entry->empty_size	= empty_size;
		__entry->min_bytes	= min_bytes;
	),

	TP_printk_btrfs("block_group=%llu flags=%llu(%s) start=%llu len=%llu "
		  "empty_size=%llu min_bytes=%llu", __entry->bg_objectid,
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS), __entry->start,
		  __entry->bytes, __entry->empty_size,  __entry->min_bytes)
);

TRACE_EVENT(btrfs_failed_cluster_setup,

	TP_PROTO(const struct btrfs_block_group *block_group),

	TP_ARGS(block_group),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	bg_objectid		)
	),

	TP_fast_assign_btrfs(block_group->fs_info,
		__entry->bg_objectid	= block_group->start;
	),

	TP_printk_btrfs("block_group=%llu", __entry->bg_objectid)
);

TRACE_EVENT(btrfs_setup_cluster,

	TP_PROTO(const struct btrfs_block_group *block_group,
		 const struct btrfs_free_cluster *cluster,
		 u64 size, int bitmap),

	TP_ARGS(block_group, cluster, size, bitmap),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	bg_objectid		)
		__field(	u64,	flags			)
		__field(	u64,	start			)
		__field(	u64,	max_size		)
		__field(	u64,	size			)
		__field(	int,	bitmap			)
	),

	TP_fast_assign_btrfs(block_group->fs_info,
		__entry->bg_objectid	= block_group->start;
		__entry->flags		= block_group->flags;
		__entry->start		= cluster->window_start;
		__entry->max_size	= cluster->max_size;
		__entry->size		= size;
		__entry->bitmap		= bitmap;
	),

	TP_printk_btrfs("block_group=%llu flags=%llu(%s) window_start=%llu "
		  "size=%llu max_size=%llu bitmap=%d",
		  __entry->bg_objectid,
		  __entry->flags,
		  __print_flags((unsigned long)__entry->flags, "|",
				BTRFS_GROUP_FLAGS), __entry->start,
		  __entry->size, __entry->max_size, __entry->bitmap)
);

struct extent_state;
TRACE_EVENT(alloc_extent_state,

	TP_PROTO(const struct extent_state *state,
		 gfp_t mask, unsigned long IP),

	TP_ARGS(state, mask, IP),

	TP_STRUCT__entry(
		__field(const struct extent_state *, state)
		__field(unsigned long, mask)
		__field(const void*, ip)
	),

	TP_fast_assign(
		__entry->state	= state,
		__entry->mask	= (__force unsigned long)mask,
		__entry->ip	= (const void *)IP
	),

	TP_printk("state=%p mask=%s caller=%pS", __entry->state,
		  show_gfp_flags(__entry->mask), __entry->ip)
);

TRACE_EVENT(free_extent_state,

	TP_PROTO(const struct extent_state *state, unsigned long IP),

	TP_ARGS(state, IP),

	TP_STRUCT__entry(
		__field(const struct extent_state *, state)
		__field(const void*, ip)
	),

	TP_fast_assign(
		__entry->state	= state,
		__entry->ip = (const void *)IP
	),

	TP_printk("state=%p caller=%pS", __entry->state, __entry->ip)
);

DECLARE_EVENT_CLASS(btrfs__work,

	TP_PROTO(const struct btrfs_work *work),

	TP_ARGS(work),

	TP_STRUCT__entry_btrfs(
		__field(	const void *,	work			)
		__field(	const void *,	wq			)
		__field(	const void *,	func			)
		__field(	const void *,	ordered_func		)
		__field(	const void *,	normal_work		)
	),

	TP_fast_assign_btrfs(btrfs_work_owner(work),
		__entry->work		= work;
		__entry->wq		= work->wq;
		__entry->func		= work->func;
		__entry->ordered_func	= work->ordered_func;
		__entry->normal_work	= &work->normal_work;
	),

	TP_printk_btrfs("work=%p (normal_work=%p) wq=%p func=%ps ordered_func=%p",
		  __entry->work, __entry->normal_work, __entry->wq,
		   __entry->func, __entry->ordered_func)
);

/*
 * For situations when the work is freed, we pass fs_info and a tag that matches
 * the address of the work structure so it can be paired with the scheduling
 * event. DO NOT add anything here that dereferences wtag.
 */
DECLARE_EVENT_CLASS(btrfs__work__done,

	TP_PROTO(const struct btrfs_fs_info *fs_info, const void *wtag),

	TP_ARGS(fs_info, wtag),

	TP_STRUCT__entry_btrfs(
		__field(	const void *,	wtag			)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->wtag		= wtag;
	),

	TP_printk_btrfs("work->%p", __entry->wtag)
);

DEFINE_EVENT(btrfs__work, btrfs_work_queued,

	TP_PROTO(const struct btrfs_work *work),

	TP_ARGS(work)
);

DEFINE_EVENT(btrfs__work, btrfs_work_sched,

	TP_PROTO(const struct btrfs_work *work),

	TP_ARGS(work)
);

DEFINE_EVENT(btrfs__work__done, btrfs_all_work_done,

	TP_PROTO(const struct btrfs_fs_info *fs_info, const void *wtag),

	TP_ARGS(fs_info, wtag)
);

DEFINE_EVENT(btrfs__work, btrfs_ordered_sched,

	TP_PROTO(const struct btrfs_work *work),

	TP_ARGS(work)
);

DECLARE_EVENT_CLASS(btrfs_workqueue,

	TP_PROTO(const struct btrfs_workqueue *wq, const char *name),

	TP_ARGS(wq, name),

	TP_STRUCT__entry_btrfs(
		__field(	const void *,	wq			)
		__string(	name,	name			)
	),

	TP_fast_assign_btrfs(btrfs_workqueue_owner(wq),
		__entry->wq		= wq;
		__assign_str(name);
	),

	TP_printk_btrfs("name=%s wq=%p", __get_str(name),
		  __entry->wq)
);

DEFINE_EVENT(btrfs_workqueue, btrfs_workqueue_alloc,

	TP_PROTO(const struct btrfs_workqueue *wq, const char *name),

	TP_ARGS(wq, name)
);

DECLARE_EVENT_CLASS(btrfs_workqueue_done,

	TP_PROTO(const struct btrfs_workqueue *wq),

	TP_ARGS(wq),

	TP_STRUCT__entry_btrfs(
		__field(	const void *,	wq		)
	),

	TP_fast_assign_btrfs(btrfs_workqueue_owner(wq),
		__entry->wq		= wq;
	),

	TP_printk_btrfs("wq=%p", __entry->wq)
);

DEFINE_EVENT(btrfs_workqueue_done, btrfs_workqueue_destroy,

	TP_PROTO(const struct btrfs_workqueue *wq),

	TP_ARGS(wq)
);

#define BTRFS_QGROUP_OPERATIONS				\
	{ QGROUP_RESERVE,	"reserve"	},	\
	{ QGROUP_RELEASE,	"release"	},	\
	{ QGROUP_FREE,		"free"		}

DECLARE_EVENT_CLASS(btrfs__qgroup_rsv_data,

	TP_PROTO(const struct inode *inode, u64 start, u64 len,
		 u64 reserved, int op),

	TP_ARGS(inode, start, len, reserved, op),

	TP_STRUCT__entry_btrfs(
		__field(	u64,		rootid		)
		__field(	u64,		ino		)
		__field(	u64,		start		)
		__field(	u64,		len		)
		__field(	u64,		reserved	)
		__field(	int,		op		)
	),

	TP_fast_assign_btrfs(btrfs_sb(inode->i_sb),
		__entry->rootid		=
			BTRFS_I(inode)->root->root_key.objectid;
		__entry->ino		= btrfs_ino(BTRFS_I(inode));
		__entry->start		= start;
		__entry->len		= len;
		__entry->reserved	= reserved;
		__entry->op		= op;
	),

	TP_printk_btrfs("root=%llu ino=%llu start=%llu len=%llu reserved=%llu op=%s",
		  __entry->rootid, __entry->ino, __entry->start, __entry->len,
		  __entry->reserved,
		  __print_flags((unsigned long)__entry->op, "",
				BTRFS_QGROUP_OPERATIONS)
	)
);

DEFINE_EVENT(btrfs__qgroup_rsv_data, btrfs_qgroup_reserve_data,

	TP_PROTO(const struct inode *inode, u64 start, u64 len,
		 u64 reserved, int op),

	TP_ARGS(inode, start, len, reserved, op)
);

DEFINE_EVENT(btrfs__qgroup_rsv_data, btrfs_qgroup_release_data,

	TP_PROTO(const struct inode *inode, u64 start, u64 len,
		 u64 reserved, int op),

	TP_ARGS(inode, start, len, reserved, op)
);

DECLARE_EVENT_CLASS(btrfs_qgroup_extent,
	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_qgroup_extent_record *rec,
		 u64 bytenr),

	TP_ARGS(fs_info, rec, bytenr),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  bytenr		)
		__field(	u64,  num_bytes		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->bytenr		= bytenr;
		__entry->num_bytes	= rec->num_bytes;
	),

	TP_printk_btrfs("bytenr=%llu num_bytes=%llu",
		  __entry->bytenr, __entry->num_bytes)
);

DEFINE_EVENT(btrfs_qgroup_extent, btrfs_qgroup_account_extents,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_qgroup_extent_record *rec,
		 u64 bytenr),

	TP_ARGS(fs_info, rec, bytenr)
);

DEFINE_EVENT(btrfs_qgroup_extent, btrfs_qgroup_trace_extent,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_qgroup_extent_record *rec,
		 u64 bytenr),

	TP_ARGS(fs_info, rec, bytenr)
);

TRACE_EVENT(qgroup_num_dirty_extents,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 transid,
		 u64 num_dirty_extents),

	TP_ARGS(fs_info, transid, num_dirty_extents),

	TP_STRUCT__entry_btrfs(
		__field(	u64, transid			)
		__field(	u64, num_dirty_extents		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->transid	   = transid;
		__entry->num_dirty_extents = num_dirty_extents;
	),

	TP_printk_btrfs("transid=%llu num_dirty_extents=%llu",
		__entry->transid, __entry->num_dirty_extents)
);

TRACE_EVENT(btrfs_qgroup_account_extent,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 transid, u64 bytenr,
		 u64 num_bytes, u64 nr_old_roots, u64 nr_new_roots),

	TP_ARGS(fs_info, transid, bytenr, num_bytes, nr_old_roots,
		nr_new_roots),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  transid			)
		__field(	u64,  bytenr			)
		__field(	u64,  num_bytes			)
		__field(	u64,  nr_old_roots		)
		__field(	u64,  nr_new_roots		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->transid	= transid;
		__entry->bytenr		= bytenr;
		__entry->num_bytes	= num_bytes;
		__entry->nr_old_roots	= nr_old_roots;
		__entry->nr_new_roots	= nr_new_roots;
	),

	TP_printk_btrfs(
"transid=%llu bytenr=%llu num_bytes=%llu nr_old_roots=%llu nr_new_roots=%llu",
		__entry->transid,
		__entry->bytenr,
		__entry->num_bytes,
		__entry->nr_old_roots,
		__entry->nr_new_roots)
);

TRACE_EVENT(qgroup_update_counters,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_qgroup *qgroup,
		 u64 cur_old_count, u64 cur_new_count),

	TP_ARGS(fs_info, qgroup, cur_old_count, cur_new_count),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  qgid			)
		__field(	u64,  old_rfer			)
		__field(	u64,  old_excl			)
		__field(	u64,  cur_old_count		)
		__field(	u64,  cur_new_count		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->qgid		= qgroup->qgroupid;
		__entry->old_rfer	= qgroup->rfer;
		__entry->old_excl	= qgroup->excl;
		__entry->cur_old_count	= cur_old_count;
		__entry->cur_new_count	= cur_new_count;
	),

	TP_printk_btrfs("qgid=%llu old_rfer=%llu old_excl=%llu cur_old_count=%llu cur_new_count=%llu",
		  __entry->qgid, __entry->old_rfer, __entry->old_excl,
		  __entry->cur_old_count, __entry->cur_new_count)
);

TRACE_EVENT(qgroup_update_reserve,

	TP_PROTO(const struct btrfs_fs_info *fs_info, const struct btrfs_qgroup *qgroup,
		 s64 diff, int type),

	TP_ARGS(fs_info, qgroup, diff, type),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	qgid			)
		__field(	u64,	cur_reserved		)
		__field(	s64,	diff			)
		__field(	int,	type			)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->qgid		= qgroup->qgroupid;
		__entry->cur_reserved	= qgroup->rsv.values[type];
		__entry->diff		= diff;
		__entry->type		= type;
	),

	TP_printk_btrfs("qgid=%llu type=%s cur_reserved=%llu diff=%lld",
		__entry->qgid, __print_symbolic(__entry->type, QGROUP_RSV_TYPES),
		__entry->cur_reserved, __entry->diff)
);

TRACE_EVENT(qgroup_meta_reserve,

	TP_PROTO(const struct btrfs_root *root, s64 diff, int type),

	TP_ARGS(root, diff, type),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	refroot			)
		__field(	s64,	diff			)
		__field(	int,	type			)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->refroot	= root->root_key.objectid;
		__entry->diff		= diff;
		__entry->type		= type;
	),

	TP_printk_btrfs("refroot=%llu(%s) type=%s diff=%lld",
		show_root_type(__entry->refroot),
		__print_symbolic(__entry->type, QGROUP_RSV_TYPES), __entry->diff)
);

TRACE_EVENT(qgroup_meta_convert,

	TP_PROTO(const struct btrfs_root *root, s64 diff),

	TP_ARGS(root, diff),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	refroot			)
		__field(	s64,	diff			)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->refroot	= root->root_key.objectid;
		__entry->diff		= diff;
	),

	TP_printk_btrfs("refroot=%llu(%s) type=%s->%s diff=%lld",
		show_root_type(__entry->refroot),
		__print_symbolic(BTRFS_QGROUP_RSV_META_PREALLOC, QGROUP_RSV_TYPES),
		__print_symbolic(BTRFS_QGROUP_RSV_META_PERTRANS, QGROUP_RSV_TYPES),
		__entry->diff)
);

TRACE_EVENT(qgroup_meta_free_all_pertrans,

	TP_PROTO(struct btrfs_root *root),

	TP_ARGS(root),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	refroot			)
		__field(	s64,	diff			)
		__field(	int,	type			)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->refroot	= root->root_key.objectid;
		spin_lock(&root->qgroup_meta_rsv_lock);
		__entry->diff		= -(s64)root->qgroup_meta_rsv_pertrans;
		spin_unlock(&root->qgroup_meta_rsv_lock);
		__entry->type		= BTRFS_QGROUP_RSV_META_PERTRANS;
	),

	TP_printk_btrfs("refroot=%llu(%s) type=%s diff=%lld",
		show_root_type(__entry->refroot),
		__print_symbolic(__entry->type, QGROUP_RSV_TYPES), __entry->diff)
);

DECLARE_EVENT_CLASS(btrfs__prelim_ref,
	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct prelim_ref *oldref,
		 const struct prelim_ref *newref, u64 tree_size),
	TP_ARGS(fs_info, oldref, newref, tree_size),

	TP_STRUCT__entry_btrfs(
		__field(	u64,  root_id		)
		__field(	u64,  objectid		)
		__field(	 u8,  type		)
		__field(	u64,  offset		)
		__field(	int,  level		)
		__field(	int,  old_count		)
		__field(	u64,  parent		)
		__field(	u64,  bytenr		)
		__field(	int,  mod_count		)
		__field(	u64,  tree_size		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->root_id	= oldref->root_id;
		__entry->objectid	= oldref->key_for_search.objectid;
		__entry->type		= oldref->key_for_search.type;
		__entry->offset		= oldref->key_for_search.offset;
		__entry->level		= oldref->level;
		__entry->old_count	= oldref->count;
		__entry->parent		= oldref->parent;
		__entry->bytenr		= oldref->wanted_disk_byte;
		__entry->mod_count	= newref ? newref->count : 0;
		__entry->tree_size	= tree_size;
	),

	TP_printk_btrfs("root_id=%llu key=[%llu,%u,%llu] level=%d count=[%d+%d=%d] parent=%llu wanted_disk_byte=%llu nodes=%llu",
			__entry->root_id,
			__entry->objectid, __entry->type,
			__entry->offset, __entry->level,
			__entry->old_count, __entry->mod_count,
			__entry->old_count + __entry->mod_count,
			__entry->parent,
			__entry->bytenr,
			__entry->tree_size)
);

DEFINE_EVENT(btrfs__prelim_ref, btrfs_prelim_ref_merge,
	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct prelim_ref *oldref,
		 const struct prelim_ref *newref, u64 tree_size),
	TP_ARGS(fs_info, oldref, newref, tree_size)
);

DEFINE_EVENT(btrfs__prelim_ref, btrfs_prelim_ref_insert,
	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct prelim_ref *oldref,
		 const struct prelim_ref *newref, u64 tree_size),
	TP_ARGS(fs_info, oldref, newref, tree_size)
);

TRACE_EVENT(btrfs_inode_mod_outstanding_extents,
	TP_PROTO(const struct btrfs_root *root, u64 ino, int mod, unsigned outstanding),

	TP_ARGS(root, ino, mod, outstanding),

	TP_STRUCT__entry_btrfs(
		__field(	u64, root_objectid	)
		__field(	u64, ino		)
		__field(	int, mod		)
		__field(	unsigned, outstanding	)
	),

	TP_fast_assign_btrfs(root->fs_info,
		__entry->root_objectid	= root->root_key.objectid;
		__entry->ino		= ino;
		__entry->mod		= mod;
		__entry->outstanding    = outstanding;
	),

	TP_printk_btrfs("root=%llu(%s) ino=%llu mod=%d outstanding=%u",
			show_root_type(__entry->root_objectid),
			__entry->ino, __entry->mod, __entry->outstanding)
);

DECLARE_EVENT_CLASS(btrfs__block_group,
	TP_PROTO(const struct btrfs_block_group *bg_cache),

	TP_ARGS(bg_cache),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	bytenr		)
		__field(	u64,	len		)
		__field(	u64,	used		)
		__field(	u64,	flags		)
	),

	TP_fast_assign_btrfs(bg_cache->fs_info,
		__entry->bytenr = bg_cache->start,
		__entry->len	= bg_cache->length,
		__entry->used	= bg_cache->used;
		__entry->flags	= bg_cache->flags;
	),

	TP_printk_btrfs("bg bytenr=%llu len=%llu used=%llu flags=%llu(%s)",
		__entry->bytenr, __entry->len, __entry->used, __entry->flags,
		__print_flags(__entry->flags, "|", BTRFS_GROUP_FLAGS))
);

DEFINE_EVENT(btrfs__block_group, btrfs_remove_block_group,
	TP_PROTO(const struct btrfs_block_group *bg_cache),

	TP_ARGS(bg_cache)
);

DEFINE_EVENT(btrfs__block_group, btrfs_add_unused_block_group,
	TP_PROTO(const struct btrfs_block_group *bg_cache),

	TP_ARGS(bg_cache)
);

DEFINE_EVENT(btrfs__block_group, btrfs_add_reclaim_block_group,
	TP_PROTO(const struct btrfs_block_group *bg_cache),

	TP_ARGS(bg_cache)
);

DEFINE_EVENT(btrfs__block_group, btrfs_reclaim_block_group,
	TP_PROTO(const struct btrfs_block_group *bg_cache),

	TP_ARGS(bg_cache)
);

DEFINE_EVENT(btrfs__block_group, btrfs_skip_unused_block_group,
	TP_PROTO(const struct btrfs_block_group *bg_cache),

	TP_ARGS(bg_cache)
);

TRACE_EVENT(btrfs_set_extent_bit,
	TP_PROTO(const struct extent_io_tree *tree,
		 u64 start, u64 len, unsigned set_bits),

	TP_ARGS(tree, start, len, set_bits),

	TP_STRUCT__entry_btrfs(
		__field(	unsigned,	owner	)
		__field(	u64,		ino	)
		__field(	u64,		rootid	)
		__field(	u64,		start	)
		__field(	u64,		len	)
		__field(	unsigned,	set_bits)
	),

	TP_fast_assign_btrfs(extent_io_tree_to_fs_info(tree),
		const struct btrfs_inode *inode = extent_io_tree_to_inode_const(tree);

		__entry->owner		= tree->owner;
		__entry->ino		= inode ? btrfs_ino(inode) : 0;
		__entry->rootid		= inode ? inode->root->root_key.objectid : 0;
		__entry->start		= start;
		__entry->len		= len;
		__entry->set_bits	= set_bits;
	),

	TP_printk_btrfs(
		"io_tree=%s ino=%llu root=%llu start=%llu len=%llu set_bits=%s",
		__print_symbolic(__entry->owner, IO_TREE_OWNER), __entry->ino,
		__entry->rootid, __entry->start, __entry->len,
		__print_flags(__entry->set_bits, "|", EXTENT_FLAGS))
);

TRACE_EVENT(btrfs_clear_extent_bit,
	TP_PROTO(const struct extent_io_tree *tree,
		 u64 start, u64 len, unsigned clear_bits),

	TP_ARGS(tree, start, len, clear_bits),

	TP_STRUCT__entry_btrfs(
		__field(	unsigned,	owner	)
		__field(	u64,		ino	)
		__field(	u64,		rootid	)
		__field(	u64,		start	)
		__field(	u64,		len	)
		__field(	unsigned,	clear_bits)
	),

	TP_fast_assign_btrfs(extent_io_tree_to_fs_info(tree),
		const struct btrfs_inode *inode = extent_io_tree_to_inode_const(tree);

		__entry->owner		= tree->owner;
		__entry->ino		= inode ? btrfs_ino(inode) : 0;
		__entry->rootid		= inode ? inode->root->root_key.objectid : 0;
		__entry->start		= start;
		__entry->len		= len;
		__entry->clear_bits	= clear_bits;
	),

	TP_printk_btrfs(
		"io_tree=%s ino=%llu root=%llu start=%llu len=%llu clear_bits=%s",
		__print_symbolic(__entry->owner, IO_TREE_OWNER), __entry->ino,
		__entry->rootid, __entry->start, __entry->len,
		__print_flags(__entry->clear_bits, "|", EXTENT_FLAGS))
);

TRACE_EVENT(btrfs_convert_extent_bit,
	TP_PROTO(const struct extent_io_tree *tree,
		 u64 start, u64 len, unsigned set_bits, unsigned clear_bits),

	TP_ARGS(tree, start, len, set_bits, clear_bits),

	TP_STRUCT__entry_btrfs(
		__field(	unsigned,	owner	)
		__field(	u64,		ino	)
		__field(	u64,		rootid	)
		__field(	u64,		start	)
		__field(	u64,		len	)
		__field(	unsigned,	set_bits)
		__field(	unsigned,	clear_bits)
	),

	TP_fast_assign_btrfs(extent_io_tree_to_fs_info(tree),
		const struct btrfs_inode *inode = extent_io_tree_to_inode_const(tree);

		__entry->owner		= tree->owner;
		__entry->ino		= inode ? btrfs_ino(inode) : 0;
		__entry->rootid		= inode ? inode->root->root_key.objectid : 0;
		__entry->start		= start;
		__entry->len		= len;
		__entry->set_bits	= set_bits;
		__entry->clear_bits	= clear_bits;
	),

	TP_printk_btrfs(
"io_tree=%s ino=%llu root=%llu start=%llu len=%llu set_bits=%s clear_bits=%s",
		  __print_symbolic(__entry->owner, IO_TREE_OWNER), __entry->ino,
		  __entry->rootid, __entry->start, __entry->len,
		  __print_flags(__entry->set_bits , "|", EXTENT_FLAGS),
		  __print_flags(__entry->clear_bits, "|", EXTENT_FLAGS))
);

DECLARE_EVENT_CLASS(btrfs_dump_space_info,
	TP_PROTO(struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo),

	TP_ARGS(fs_info, sinfo),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	flags			)
		__field(	u64,	total_bytes		)
		__field(	u64,	bytes_used		)
		__field(	u64,	bytes_pinned		)
		__field(	u64,	bytes_reserved		)
		__field(	u64,	bytes_may_use		)
		__field(	u64,	bytes_readonly		)
		__field(	u64,	reclaim_size		)
		__field(	int,	clamp			)
		__field(	u64,	global_reserved		)
		__field(	u64,	trans_reserved		)
		__field(	u64,	delayed_refs_reserved	)
		__field(	u64,	delayed_reserved	)
		__field(	u64,	free_chunk_space	)
		__field(	u64,	delalloc_bytes		)
		__field(	u64,	ordered_bytes		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->flags			=	sinfo->flags;
		__entry->total_bytes		=	sinfo->total_bytes;
		__entry->bytes_used		=	sinfo->bytes_used;
		__entry->bytes_pinned		=	sinfo->bytes_pinned;
		__entry->bytes_reserved		=	sinfo->bytes_reserved;
		__entry->bytes_may_use		=	sinfo->bytes_may_use;
		__entry->bytes_readonly		=	sinfo->bytes_readonly;
		__entry->reclaim_size		=	sinfo->reclaim_size;
		__entry->clamp			=	sinfo->clamp;
		__entry->global_reserved	=	fs_info->global_block_rsv.reserved;
		__entry->trans_reserved		=	fs_info->trans_block_rsv.reserved;
		__entry->delayed_refs_reserved	=	fs_info->delayed_refs_rsv.reserved;
		__entry->delayed_reserved	=	fs_info->delayed_block_rsv.reserved;
		__entry->free_chunk_space	=	atomic64_read(&fs_info->free_chunk_space);
		__entry->delalloc_bytes		=	percpu_counter_sum_positive(&fs_info->delalloc_bytes);
		__entry->ordered_bytes		=	percpu_counter_sum_positive(&fs_info->ordered_bytes);
	),

	TP_printk_btrfs("flags=%s total_bytes=%llu bytes_used=%llu "
			"bytes_pinned=%llu bytes_reserved=%llu "
			"bytes_may_use=%llu bytes_readonly=%llu "
			"reclaim_size=%llu clamp=%d global_reserved=%llu "
			"trans_reserved=%llu delayed_refs_reserved=%llu "
			"delayed_reserved=%llu chunk_free_space=%llu "
			"delalloc_bytes=%llu ordered_bytes=%llu",
			__print_flags(__entry->flags, "|", BTRFS_GROUP_FLAGS),
			__entry->total_bytes, __entry->bytes_used,
			__entry->bytes_pinned, __entry->bytes_reserved,
			__entry->bytes_may_use, __entry->bytes_readonly,
			__entry->reclaim_size, __entry->clamp,
			__entry->global_reserved, __entry->trans_reserved,
			__entry->delayed_refs_reserved,
			__entry->delayed_reserved, __entry->free_chunk_space,
			__entry->delalloc_bytes, __entry->ordered_bytes)
);

DEFINE_EVENT(btrfs_dump_space_info, btrfs_done_preemptive_reclaim,
	TP_PROTO(struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo),
	TP_ARGS(fs_info, sinfo)
);

DEFINE_EVENT(btrfs_dump_space_info, btrfs_fail_all_tickets,
	TP_PROTO(struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo),
	TP_ARGS(fs_info, sinfo)
);

TRACE_EVENT(btrfs_reserve_ticket,
	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 flags, u64 bytes,
		 u64 start_ns, int flush, int error),

	TP_ARGS(fs_info, flags, bytes, start_ns, flush, error),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	flags		)
		__field(	u64,	bytes		)
		__field(	u64,	start_ns	)
		__field(	int,	flush		)
		__field(	int,	error		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->flags		= flags;
		__entry->bytes		= bytes;
		__entry->start_ns	= start_ns;
		__entry->flush		= flush;
		__entry->error		= error;
	),

	TP_printk_btrfs("flags=%s bytes=%llu start_ns=%llu flush=%s error=%d",
			__print_flags(__entry->flags, "|", BTRFS_GROUP_FLAGS),
			__entry->bytes, __entry->start_ns,
			__print_symbolic(__entry->flush, FLUSH_ACTIONS),
			__entry->error)
);

DECLARE_EVENT_CLASS(btrfs_sleep_tree_lock,
	TP_PROTO(const struct extent_buffer *eb, u64 start_ns),

	TP_ARGS(eb, start_ns),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	block		)
		__field(	u64,	generation	)
		__field(	u64,	start_ns	)
		__field(	u64,	end_ns		)
		__field(	u64,	diff_ns		)
		__field(	u64,	owner		)
		__field(	int,	is_log_tree	)
	),

	TP_fast_assign_btrfs(eb->fs_info,
		__entry->block		= eb->start;
		__entry->generation	= btrfs_header_generation(eb);
		__entry->start_ns	= start_ns;
		__entry->end_ns		= ktime_get_ns();
		__entry->diff_ns	= __entry->end_ns - start_ns;
		__entry->owner		= btrfs_header_owner(eb);
		__entry->is_log_tree	= (eb->log_index >= 0);
	),

	TP_printk_btrfs(
"block=%llu generation=%llu start_ns=%llu end_ns=%llu diff_ns=%llu owner=%llu is_log_tree=%d",
		__entry->block, __entry->generation,
		__entry->start_ns, __entry->end_ns, __entry->diff_ns,
		__entry->owner, __entry->is_log_tree)
);

DEFINE_EVENT(btrfs_sleep_tree_lock, btrfs_tree_read_lock,
	TP_PROTO(const struct extent_buffer *eb, u64 start_ns),

	TP_ARGS(eb, start_ns)
);

DEFINE_EVENT(btrfs_sleep_tree_lock, btrfs_tree_lock,
	TP_PROTO(const struct extent_buffer *eb, u64 start_ns),

	TP_ARGS(eb, start_ns)
);

DECLARE_EVENT_CLASS(btrfs_locking_events,
	TP_PROTO(const struct extent_buffer *eb),

	TP_ARGS(eb),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	block		)
		__field(	u64,	generation	)
		__field(	u64,	owner		)
		__field(	int,	is_log_tree	)
	),

	TP_fast_assign_btrfs(eb->fs_info,
		__entry->block		= eb->start;
		__entry->generation	= btrfs_header_generation(eb);
		__entry->owner		= btrfs_header_owner(eb);
		__entry->is_log_tree	= (eb->log_index >= 0);
	),

	TP_printk_btrfs("block=%llu generation=%llu owner=%llu is_log_tree=%d",
		__entry->block, __entry->generation,
		__entry->owner, __entry->is_log_tree)
);

#define DEFINE_BTRFS_LOCK_EVENT(name)				\
DEFINE_EVENT(btrfs_locking_events, name,			\
		TP_PROTO(const struct extent_buffer *eb),	\
								\
		TP_ARGS(eb)					\
)

DEFINE_BTRFS_LOCK_EVENT(btrfs_tree_unlock);
DEFINE_BTRFS_LOCK_EVENT(btrfs_tree_read_unlock);
DEFINE_BTRFS_LOCK_EVENT(btrfs_tree_read_unlock_blocking);
DEFINE_BTRFS_LOCK_EVENT(btrfs_set_lock_blocking_read);
DEFINE_BTRFS_LOCK_EVENT(btrfs_set_lock_blocking_write);
DEFINE_BTRFS_LOCK_EVENT(btrfs_try_tree_read_lock);
DEFINE_BTRFS_LOCK_EVENT(btrfs_tree_read_lock_atomic);

DECLARE_EVENT_CLASS(btrfs__space_info_update,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo, u64 old, s64 diff),

	TP_ARGS(fs_info, sinfo, old, diff),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	type		)
		__field(	u64,	old		)
		__field(	s64,	diff		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->type	= sinfo->flags;
		__entry->old	= old;
		__entry->diff	= diff;
	),
	TP_printk_btrfs("type=%s old=%llu diff=%lld",
		__print_flags(__entry->type, "|", BTRFS_GROUP_FLAGS),
		__entry->old, __entry->diff)
);

DEFINE_EVENT(btrfs__space_info_update, update_bytes_may_use,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo, u64 old, s64 diff),

	TP_ARGS(fs_info, sinfo, old, diff)
);

DEFINE_EVENT(btrfs__space_info_update, update_bytes_pinned,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo, u64 old, s64 diff),

	TP_ARGS(fs_info, sinfo, old, diff)
);

DEFINE_EVENT(btrfs__space_info_update, update_bytes_zone_unusable,

	TP_PROTO(const struct btrfs_fs_info *fs_info,
		 const struct btrfs_space_info *sinfo, u64 old, s64 diff),

	TP_ARGS(fs_info, sinfo, old, diff)
);

DECLARE_EVENT_CLASS(btrfs_raid56_bio,

	TP_PROTO(const struct btrfs_raid_bio *rbio,
		 const struct bio *bio,
		 const struct raid56_bio_trace_info *trace_info),

	TP_ARGS(rbio, bio, trace_info),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	full_stripe	)
		__field(	u64,	physical	)
		__field(	u64,	devid		)
		__field(	u32,	offset		)
		__field(	u32,	len		)
		__field(	u8,	opf		)
		__field(	u8,	total_stripes	)
		__field(	u8,	real_stripes	)
		__field(	u8,	nr_data		)
		__field(	u8,	stripe_nr	)
	),

	TP_fast_assign_btrfs(rbio->bioc->fs_info,
		__entry->full_stripe	= rbio->bioc->full_stripe_logical;
		__entry->physical	= bio->bi_iter.bi_sector << SECTOR_SHIFT;
		__entry->len		= bio->bi_iter.bi_size;
		__entry->opf		= bio_op(bio);
		__entry->devid		= trace_info->devid;
		__entry->offset		= trace_info->offset;
		__entry->stripe_nr	= trace_info->stripe_nr;
		__entry->total_stripes	= rbio->bioc->num_stripes;
		__entry->real_stripes	= rbio->real_stripes;
		__entry->nr_data	= rbio->nr_data;
	),
	/*
	 * For type output, we need to output things like "DATA1"
	 * (the first data stripe), "DATA2" (the second data stripe),
	 * "PQ1" (P stripe),"PQ2" (Q stripe), "REPLACE0" (replace target device).
	 */
	TP_printk_btrfs(
"full_stripe=%llu devid=%lld type=%s%d offset=%d opf=0x%x physical=%llu len=%u",
		__entry->full_stripe, __entry->devid,
		(__entry->stripe_nr < __entry->nr_data) ? "DATA" :
			((__entry->stripe_nr < __entry->real_stripes) ? "PQ" :
			 "REPLACE"),
		(__entry->stripe_nr < __entry->nr_data) ?
			(__entry->stripe_nr + 1) :
			((__entry->stripe_nr < __entry->real_stripes) ?
			 (__entry->stripe_nr - __entry->nr_data + 1) : 0),
		__entry->offset, __entry->opf, __entry->physical, __entry->len)
);

DEFINE_EVENT(btrfs_raid56_bio, raid56_read,
	TP_PROTO(const struct btrfs_raid_bio *rbio,
		 const struct bio *bio,
		 const struct raid56_bio_trace_info *trace_info),

	TP_ARGS(rbio, bio, trace_info)
);

DEFINE_EVENT(btrfs_raid56_bio, raid56_write,
	TP_PROTO(const struct btrfs_raid_bio *rbio,
		 const struct bio *bio,
		 const struct raid56_bio_trace_info *trace_info),

	TP_ARGS(rbio, bio, trace_info)
);

TRACE_EVENT(btrfs_insert_one_raid_extent,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 logical, u64 length,
		 int num_stripes),

	TP_ARGS(fs_info, logical, length, num_stripes),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	logical		)
		__field(	u64,	length		)
		__field(	int,	num_stripes	)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->logical	= logical;
		__entry->length		= length;
		__entry->num_stripes	= num_stripes;
	),

	TP_printk_btrfs("logical=%llu length=%llu num_stripes=%d",
			__entry->logical, __entry->length,
			__entry->num_stripes)
);

TRACE_EVENT(btrfs_raid_extent_delete,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 start, u64 end,
		 u64 found_start, u64 found_end),

	TP_ARGS(fs_info, start, end, found_start, found_end),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	start		)
		__field(	u64,	end		)
		__field(	u64,	found_start	)
		__field(	u64,	found_end	)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->start		= start;
		__entry->end		= end;
		__entry->found_start	= found_start;
		__entry->found_end	= found_end;
	),

	TP_printk_btrfs("start=%llu end=%llu found_start=%llu found_end=%llu",
			__entry->start, __entry->end, __entry->found_start,
			__entry->found_end)
);

TRACE_EVENT(btrfs_get_raid_extent_offset,

	TP_PROTO(const struct btrfs_fs_info *fs_info, u64 logical, u64 length,
		 u64 physical, u64 devid),

	TP_ARGS(fs_info, logical, length, physical, devid),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	logical		)
		__field(	u64,	length		)
		__field(	u64,	physical	)
		__field(	u64,	devid		)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->logical	= logical;
		__entry->length		= length;
		__entry->physical	= physical;
		__entry->devid		= devid;
	),

	TP_printk_btrfs("logical=%llu length=%llu physical=%llu devid=%llu",
			__entry->logical, __entry->length, __entry->physical,
			__entry->devid)
);

TRACE_EVENT(btrfs_extent_map_shrinker_count,

	TP_PROTO(const struct btrfs_fs_info *fs_info, long nr),

	TP_ARGS(fs_info, nr),

	TP_STRUCT__entry_btrfs(
		__field(	long,	nr	)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->nr		= nr;
	),

	TP_printk_btrfs("nr=%ld", __entry->nr)
);

TRACE_EVENT(btrfs_extent_map_shrinker_scan_enter,

	TP_PROTO(const struct btrfs_fs_info *fs_info, long nr),

	TP_ARGS(fs_info, nr),

	TP_STRUCT__entry_btrfs(
		__field(	long,	nr_to_scan	)
		__field(	long,	nr		)
		__field(	u64,	last_root_id	)
		__field(	u64,	last_ino	)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->nr_to_scan	= \
		     atomic64_read(&fs_info->em_shrinker_nr_to_scan);
		__entry->nr		= nr;
		__entry->last_root_id	= fs_info->em_shrinker_last_root;
		__entry->last_ino	= fs_info->em_shrinker_last_ino;
	),

	TP_printk_btrfs("nr_to_scan=%ld nr=%ld last_root=%llu(%s) last_ino=%llu",
			__entry->nr_to_scan, __entry->nr,
			show_root_type(__entry->last_root_id), __entry->last_ino)
);

TRACE_EVENT(btrfs_extent_map_shrinker_scan_exit,

	TP_PROTO(const struct btrfs_fs_info *fs_info, long nr_dropped, long nr),

	TP_ARGS(fs_info, nr_dropped, nr),

	TP_STRUCT__entry_btrfs(
		__field(	long,	nr_dropped	)
		__field(	long,	nr		)
		__field(	u64,	last_root_id	)
		__field(	u64,	last_ino	)
	),

	TP_fast_assign_btrfs(fs_info,
		__entry->nr_dropped	= nr_dropped;
		__entry->nr		= nr;
		__entry->last_root_id	= fs_info->em_shrinker_last_root;
		__entry->last_ino	= fs_info->em_shrinker_last_ino;
	),

	TP_printk_btrfs("nr_dropped=%ld nr=%ld last_root=%llu(%s) last_ino=%llu",
			__entry->nr_dropped, __entry->nr,
			show_root_type(__entry->last_root_id), __entry->last_ino)
);

TRACE_EVENT(btrfs_extent_map_shrinker_remove_em,

	TP_PROTO(const struct btrfs_inode *inode, const struct extent_map *em),

	TP_ARGS(inode, em),

	TP_STRUCT__entry_btrfs(
		__field(	u64,	ino		)
		__field(	u64,	root_id		)
		__field(	u64,	start		)
		__field(	u64,	len		)
		__field(	u32,	flags		)
	),

	TP_fast_assign_btrfs(inode->root->fs_info,
		__entry->ino		= btrfs_ino(inode);
		__entry->root_id	= inode->root->root_key.objectid;
		__entry->start		= em->start;
		__entry->len		= em->len;
		__entry->flags		= em->flags;
	),

	TP_printk_btrfs("ino=%llu root=%llu(%s) start=%llu len=%llu flags=%s",
			__entry->ino, show_root_type(__entry->root_id),
			__entry->start, __entry->len,
			show_map_flags(__entry->flags))
);

#endif /* _TRACE_BTRFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
