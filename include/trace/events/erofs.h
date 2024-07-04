/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM erofs

#if !defined(_TRACE_EROFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EROFS_H

#include <linux/tracepoint.h>
#include <linux/fs.h>

struct erofs_map_blocks;

#define show_dev(dev)		MAJOR(dev), MINOR(dev)
#define show_dev_nid(entry)	show_dev(entry->dev), entry->nid

#define show_file_type(type)						\
	__print_symbolic(type,						\
		{ 0,		"FILE" },				\
		{ 1,		"DIR" })

#define show_map_flags(flags) __print_flags(flags, "|",	\
	{ EROFS_GET_BLOCKS_FIEMAP,	"FIEMAP" },	\
	{ EROFS_GET_BLOCKS_READMORE,	"READMORE" },	\
	{ EROFS_GET_BLOCKS_FINDTAIL,	"FINDTAIL" })

#define show_mflags(flags) __print_flags(flags, "",	\
	{ EROFS_MAP_MAPPED,		"M" },		\
	{ EROFS_MAP_META,		"I" },		\
	{ EROFS_MAP_ENCODED,		"E" },		\
	{ EROFS_MAP_FULL_MAPPED,	"F" },		\
	{ EROFS_MAP_FRAGMENT,		"R" },		\
	{ EROFS_MAP_PARTIAL_REF,	"P" })

TRACE_EVENT(erofs_lookup,

	TP_PROTO(struct inode *dir, struct dentry *dentry, unsigned int flags),

	TP_ARGS(dir, dentry, flags),

	TP_STRUCT__entry(
		__field(dev_t,		dev	)
		__field(erofs_nid_t,	nid	)
		__string(name,		dentry->d_name.name	)
		__field(unsigned int,	flags	)
	),

	TP_fast_assign(
		__entry->dev	= dir->i_sb->s_dev;
		__entry->nid	= EROFS_I(dir)->nid;
		__assign_str(name);
		__entry->flags	= flags;
	),

	TP_printk("dev = (%d,%d), pnid = %llu, name:%s, flags:%x",
		show_dev_nid(__entry),
		__get_str(name),
		__entry->flags)
);

TRACE_EVENT(erofs_fill_inode,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(dev_t,		dev	)
		__field(erofs_nid_t,	nid	)
		__field(erofs_blk_t,	blkaddr )
		__field(unsigned int,	ofs	)
	),

	TP_fast_assign(
		__entry->dev		= inode->i_sb->s_dev;
		__entry->nid		= EROFS_I(inode)->nid;
		__entry->blkaddr	= erofs_blknr(inode->i_sb, erofs_iloc(inode));
		__entry->ofs		= erofs_blkoff(inode->i_sb, erofs_iloc(inode));
	),

	TP_printk("dev = (%d,%d), nid = %llu, blkaddr %u ofs %u",
		  show_dev_nid(__entry),
		  __entry->blkaddr, __entry->ofs)
);

TRACE_EVENT(erofs_read_folio,

	TP_PROTO(struct folio *folio, bool raw),

	TP_ARGS(folio, raw),

	TP_STRUCT__entry(
		__field(dev_t,		dev	)
		__field(erofs_nid_t,    nid     )
		__field(int,		dir	)
		__field(pgoff_t,	index	)
		__field(int,		uptodate)
		__field(bool,		raw	)
	),

	TP_fast_assign(
		__entry->dev	= folio->mapping->host->i_sb->s_dev;
		__entry->nid	= EROFS_I(folio->mapping->host)->nid;
		__entry->dir	= S_ISDIR(folio->mapping->host->i_mode);
		__entry->index	= folio->index;
		__entry->uptodate = folio_test_uptodate(folio);
		__entry->raw = raw;
	),

	TP_printk("dev = (%d,%d), nid = %llu, %s, index = %lu, uptodate = %d "
		"raw = %d",
		show_dev_nid(__entry),
		show_file_type(__entry->dir),
		(unsigned long)__entry->index,
		__entry->uptodate,
		__entry->raw)
);

TRACE_EVENT(erofs_readpages,

	TP_PROTO(struct inode *inode, pgoff_t start, unsigned int nrpage,
		bool raw),

	TP_ARGS(inode, start, nrpage, raw),

	TP_STRUCT__entry(
		__field(dev_t,		dev	)
		__field(erofs_nid_t,	nid	)
		__field(pgoff_t,	start	)
		__field(unsigned int,	nrpage	)
		__field(bool,		raw	)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->nid	= EROFS_I(inode)->nid;
		__entry->start	= start;
		__entry->nrpage	= nrpage;
		__entry->raw	= raw;
	),

	TP_printk("dev = (%d,%d), nid = %llu, start = %lu nrpage = %u raw = %d",
		show_dev_nid(__entry),
		(unsigned long)__entry->start,
		__entry->nrpage,
		__entry->raw)
);

DECLARE_EVENT_CLASS(erofs__map_blocks_enter,
	TP_PROTO(struct inode *inode, struct erofs_map_blocks *map,
		 unsigned int flags),

	TP_ARGS(inode, map, flags),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	erofs_nid_t,	nid		)
		__field(	erofs_off_t,	la		)
		__field(	u64,		llen		)
		__field(	unsigned int,	flags		)
	),

	TP_fast_assign(
		__entry->dev    = inode->i_sb->s_dev;
		__entry->nid    = EROFS_I(inode)->nid;
		__entry->la	= map->m_la;
		__entry->llen	= map->m_llen;
		__entry->flags	= flags;
	),

	TP_printk("dev = (%d,%d), nid = %llu, la %llu llen %llu flags %s",
		  show_dev_nid(__entry),
		  __entry->la, __entry->llen,
		  __entry->flags ? show_map_flags(__entry->flags) : "NULL")
);

DEFINE_EVENT(erofs__map_blocks_enter, erofs_map_blocks_enter,
	TP_PROTO(struct inode *inode, struct erofs_map_blocks *map,
		 unsigned flags),

	TP_ARGS(inode, map, flags)
);

DEFINE_EVENT(erofs__map_blocks_enter, z_erofs_map_blocks_iter_enter,
	TP_PROTO(struct inode *inode, struct erofs_map_blocks *map,
		 unsigned int flags),

	TP_ARGS(inode, map, flags)
);

DECLARE_EVENT_CLASS(erofs__map_blocks_exit,
	TP_PROTO(struct inode *inode, struct erofs_map_blocks *map,
		 unsigned int flags, int ret),

	TP_ARGS(inode, map, flags, ret),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	erofs_nid_t,	nid		)
		__field(        unsigned int,   flags           )
		__field(	erofs_off_t,	la		)
		__field(	erofs_off_t,	pa		)
		__field(	u64,		llen		)
		__field(	u64,		plen		)
		__field(        unsigned int,	mflags		)
		__field(	int,		ret		)
	),

	TP_fast_assign(
		__entry->dev    = inode->i_sb->s_dev;
		__entry->nid    = EROFS_I(inode)->nid;
		__entry->flags	= flags;
		__entry->la	= map->m_la;
		__entry->pa	= map->m_pa;
		__entry->llen	= map->m_llen;
		__entry->plen	= map->m_plen;
		__entry->mflags	= map->m_flags;
		__entry->ret	= ret;
	),

	TP_printk("dev = (%d,%d), nid = %llu, flags %s "
		  "la %llu pa %llu llen %llu plen %llu mflags %s ret %d",
		  show_dev_nid(__entry),
		  __entry->flags ? show_map_flags(__entry->flags) : "NULL",
		  __entry->la, __entry->pa, __entry->llen, __entry->plen,
		  show_mflags(__entry->mflags), __entry->ret)
);

DEFINE_EVENT(erofs__map_blocks_exit, erofs_map_blocks_exit,
	TP_PROTO(struct inode *inode, struct erofs_map_blocks *map,
		 unsigned flags, int ret),

	TP_ARGS(inode, map, flags, ret)
);

DEFINE_EVENT(erofs__map_blocks_exit, z_erofs_map_blocks_iter_exit,
	TP_PROTO(struct inode *inode, struct erofs_map_blocks *map,
		 unsigned int flags, int ret),

	TP_ARGS(inode, map, flags, ret)
);

TRACE_EVENT(erofs_destroy_inode,
	TP_PROTO(struct inode *inode),

	TP_ARGS(inode),

	TP_STRUCT__entry(
		__field(	dev_t,		dev		)
		__field(	erofs_nid_t,	nid		)
	),

	TP_fast_assign(
		__entry->dev	= inode->i_sb->s_dev;
		__entry->nid	= EROFS_I(inode)->nid;
	),

	TP_printk("dev = (%d,%d), nid = %llu", show_dev_nid(__entry))
);

#endif /* _TRACE_EROFS_H */

 /* This part must be outside protection */
#include <trace/define_trace.h>
