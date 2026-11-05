/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ntfs3

#if !defined(_TRACE_NTFS3_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NTFS3_H

#include <linux/fs.h>
#include <linux/tracepoint.h>

TRACE_EVENT(ntfs3_fill_super,
	TP_PROTO(struct super_block *sb, bool ro, u32 cluster_size,
		 u32 record_size, u32 index_size, int err),
	TP_ARGS(sb, ro, cluster_size, record_size, index_size, err),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, cluster_size)
		__field(u32, record_size)
		__field(u32, index_size)
		__field(int, err)
		__field(bool, ro)
	),
	TP_fast_assign(
		__entry->dev = sb->s_bdev->bd_dev;
		__entry->cluster_size = cluster_size;
		__entry->record_size = record_size;
		__entry->index_size = index_size;
		__entry->err = err;
		__entry->ro = ro;
	),
	TP_printk("dev=(%d,%d) ro=%d cluster=%u record=%u index=%u err=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ro,
		  __entry->cluster_size, __entry->record_size,
		  __entry->index_size, __entry->err)
);

TRACE_EVENT(ntfs3_init_from_boot,
	TP_PROTO(struct super_block *sb, u32 media_sector_size,
		 u32 boot_sector_size, bool used_alt_boot, int err),
	TP_ARGS(sb, media_sector_size, boot_sector_size, used_alt_boot, err),
	TP_STRUCT__entry(
		__field(dev_t, dev)
		__field(u32, media_sector_size)
		__field(u32, boot_sector_size)
		__field(int, err)
		__field(bool, used_alt_boot)
	),
	TP_fast_assign(
		__entry->dev = sb->s_bdev->bd_dev;
		__entry->media_sector_size = media_sector_size;
		__entry->boot_sector_size = boot_sector_size;
		__entry->err = err;
		__entry->used_alt_boot = used_alt_boot;
	),
	TP_printk("dev=(%d,%d) media_sector=%u boot_sector=%u alt_boot=%d err=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->media_sector_size, __entry->boot_sector_size,
		  __entry->used_alt_boot, __entry->err)
);

TRACE_EVENT(ntfs3_log_replay,
	TP_PROTO(struct inode *inode, bool initialized, int err),
	TP_ARGS(inode, initialized, err),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(loff_t, size)
		__field(dev_t, dev)
		__field(int, err)
		__field(bool, initialized)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->size = i_size_read(inode);
		__entry->dev = inode->i_sb->s_dev;
		__entry->err = err;
		__entry->initialized = initialized;
	),
	TP_printk("dev=(%d,%d) ino=%lu size=%lld initialized=%d err=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		  __entry->size, __entry->initialized, __entry->err)
);

TRACE_EVENT(ntfs3_lookup,
	TP_PROTO(struct inode *dir, struct dentry *dentry),
	TP_ARGS(dir, dentry),
	TP_STRUCT__entry(
		__field(unsigned long, parent_ino)
		__field(dev_t, dev)
		__string(name, dentry->d_name.name)
	),
	TP_fast_assign(
		__entry->parent_ino = dir->i_ino;
		__entry->dev = dir->i_sb->s_dev;
		__assign_str(name);
	),
	TP_printk("dev=(%d,%d) parent=%lu name=%s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->parent_ino, __get_str(name))
);

TRACE_EVENT(ntfs3_rename,
	TP_PROTO(struct inode *dir, struct dentry *dentry,
		 struct inode *new_dir, struct dentry *new_dentry),
	TP_ARGS(dir, dentry, new_dir, new_dentry),
	TP_STRUCT__entry(
		__field(unsigned long, dir_ino)
		__field(unsigned long, new_dir_ino)
		__field(unsigned long, ino)
		__field(dev_t, dev)
		__string(old_name, dentry->d_name.name)
		__string(new_name, new_dentry->d_name.name)
	),
	TP_fast_assign(
		__entry->dir_ino = dir->i_ino;
		__entry->new_dir_ino = new_dir->i_ino;
		__entry->ino = d_inode(dentry)->i_ino;
		__entry->dev = dir->i_sb->s_dev;
		__assign_str(old_name);
		__assign_str(new_name);
	),
	TP_printk("dev=(%d,%d) dir=%lu new_dir=%lu ino=%lu old=%s new=%s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dir_ino, __entry->new_dir_ino, __entry->ino,
		  __get_str(old_name), __get_str(new_name))
);

TRACE_EVENT(ntfs3_create_inode,
	TP_PROTO(struct inode *dir, struct dentry *dentry),
	TP_ARGS(dir, dentry),
	TP_STRUCT__entry(
		__field(unsigned long, parent_ino)
		__field(dev_t, dev)
		__string(name, dentry->d_name.name)
	),
	TP_fast_assign(
		__entry->parent_ino = dir->i_ino;
		__entry->dev = dir->i_sb->s_dev;
		__assign_str(name);
	),
	TP_printk("dev=(%d,%d) parent=%lu name=%s",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->parent_ino, __get_str(name))
);

TRACE_EVENT(ntfs3_dir_search_u,
	TP_PROTO(struct inode *dir, unsigned int name_len),
	TP_ARGS(dir, name_len),
	TP_STRUCT__entry(
		__field(unsigned long, dir_ino)
		__field(dev_t, dev)
		__field(unsigned int, name_len)
	),
	TP_fast_assign(
		__entry->dir_ino = dir->i_ino;
		__entry->dev = dir->i_sb->s_dev;
		__entry->name_len = name_len;
	),
	TP_printk("dev=(%d,%d) dir=%lu name_len=%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->dir_ino, __entry->name_len)
);

TRACE_EVENT(ntfs3_indx_find,
	TP_PROTO(struct inode *inode, u8 type, size_t key_len),
	TP_ARGS(inode, type, key_len),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(size_t, key_len)
		__field(dev_t, dev)
		__field(u8, type)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->key_len = key_len;
		__entry->dev = inode->i_sb->s_dev;
		__entry->type = type;
	),
	TP_printk("dev=(%d,%d) ino=%lu type=%u key_len=%zu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->type, __entry->key_len)
);

TRACE_EVENT(ntfs3_indx_insert_entry,
	TP_PROTO(struct inode *inode, u8 type, u16 key_len, bool undo),
	TP_ARGS(inode, type, key_len, undo),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(dev_t, dev)
		__field(u16, key_len)
		__field(u8, type)
		__field(bool, undo)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->dev = inode->i_sb->s_dev;
		__entry->key_len = key_len;
		__entry->type = type;
		__entry->undo = undo;
	),
	TP_printk("dev=(%d,%d) ino=%lu type=%u key_len=%u undo=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->type, __entry->key_len,
		  __entry->undo)
);

TRACE_EVENT(ntfs3_indx_delete_entry,
	TP_PROTO(struct inode *inode, u8 type, u32 key_len),
	TP_ARGS(inode, type, key_len),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(u32, key_len)
		__field(dev_t, dev)
		__field(u8, type)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->key_len = key_len;
		__entry->dev = inode->i_sb->s_dev;
		__entry->type = type;
	),
	TP_printk("dev=(%d,%d) ino=%lu type=%u key_len=%u",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->type, __entry->key_len)
);

TRACE_EVENT(ntfs3_attr_allocate_clusters,
	TP_PROTO(struct super_block *sb, u64 vcn, u64 lcn, u64 len, u32 opt),
	TP_ARGS(sb, vcn, lcn, len, opt),
	TP_STRUCT__entry(
		__field(u64, vcn)
		__field(u64, lcn)
		__field(u64, len)
		__field(dev_t, dev)
		__field(u32, opt)
	),
	TP_fast_assign(
		__entry->vcn = vcn;
		__entry->lcn = lcn;
		__entry->len = len;
		__entry->dev = sb->s_dev;
		__entry->opt = opt;
	),
	TP_printk("dev=(%d,%d) vcn=%llu lcn=%llu len=%llu opt=0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->vcn, __entry->lcn, __entry->len, __entry->opt)
);

TRACE_EVENT(ntfs3_attr_set_size_ex,
	TP_PROTO(struct inode *inode, u32 type, u64 old_size, u64 new_size,
		 bool keep_prealloc, bool no_da),
	TP_ARGS(inode, type, old_size, new_size, keep_prealloc, no_da),
	TP_STRUCT__entry(
		__field(u64, old_size)
		__field(u64, new_size)
		__field(unsigned long, ino)
		__field(dev_t, dev)
		__field(u32, type)
		__field(bool, keep_prealloc)
		__field(bool, no_da)
	),
	TP_fast_assign(
		__entry->old_size = old_size;
		__entry->new_size = new_size;
		__entry->ino = inode->i_ino;
		__entry->dev = inode->i_sb->s_dev;
		__entry->type = type;
		__entry->keep_prealloc = keep_prealloc;
		__entry->no_da = no_da;
	),
	TP_printk("dev=(%d,%d) ino=%lu type=0x%x old_size=%llu new_size=%llu keep_prealloc=%d no_da=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		  __entry->type, __entry->old_size, __entry->new_size,
		  __entry->keep_prealloc, __entry->no_da)
);

TRACE_EVENT(ntfs3_attr_data_get_block,
	TP_PROTO(struct inode *inode, u64 vcn, u64 clen, bool *new,
		 bool zero, bool no_da),
	TP_ARGS(inode, vcn, clen, new, zero, no_da),
	TP_STRUCT__entry(
		__field(u64, vcn)
		__field(u64, clen)
		__field(unsigned long, ino)
		__field(dev_t, dev)
		__field(bool, create)
		__field(bool, zero)
		__field(bool, no_da)
	),
	TP_fast_assign(
		__entry->vcn = vcn;
		__entry->clen = clen;
		__entry->ino = inode->i_ino;
		__entry->dev = inode->i_sb->s_dev;
		__entry->create = !!new;
		__entry->zero = zero;
		__entry->no_da = no_da;
	),
	TP_printk("dev=(%d,%d) ino=%lu vcn=%llu clen=%llu create=%d zero=%d no_da=%d",
		  MAJOR(__entry->dev), MINOR(__entry->dev), __entry->ino,
		  __entry->vcn, __entry->clen, __entry->create,
		  __entry->zero, __entry->no_da)
);

TRACE_EVENT(ntfs3_iomap_begin,
	TP_PROTO(struct inode *inode, loff_t offset, loff_t length,
		 unsigned int flags),
	TP_ARGS(inode, offset, length, flags),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(loff_t, offset)
		__field(loff_t, length)
		__field(dev_t, dev)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->offset = offset;
		__entry->length = length;
		__entry->dev = inode->i_sb->s_dev;
		__entry->flags = flags;
	),
	TP_printk("dev=(%d,%d) ino=%lu offset=%lld length=%lld flags=0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->offset, __entry->length,
		  __entry->flags)
);

TRACE_EVENT(ntfs3_iomap_end,
	TP_PROTO(struct inode *inode, loff_t pos, loff_t length,
		 ssize_t written, unsigned int flags),
	TP_ARGS(inode, pos, length, written, flags),
	TP_STRUCT__entry(
		__field(unsigned long, ino)
		__field(loff_t, pos)
		__field(loff_t, length)
		__field(ssize_t, written)
		__field(dev_t, dev)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->ino = inode->i_ino;
		__entry->pos = pos;
		__entry->length = length;
		__entry->written = written;
		__entry->dev = inode->i_sb->s_dev;
		__entry->flags = flags;
	),
	TP_printk("dev=(%d,%d) ino=%lu pos=%lld length=%lld written=%zd flags=0x%x",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->pos, __entry->length,
		  __entry->written, __entry->flags)
);

DECLARE_EVENT_CLASS(ntfs3_file_class,
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter),
	TP_ARGS(iocb, iter),
	TP_STRUCT__entry(
		__field(loff_t, size)
		__field(loff_t, offset)
		__field(size_t, count)
		__field(unsigned long, ino)
		__field(dev_t, dev)
	),
	TP_fast_assign(
		__entry->size = i_size_read(file_inode(iocb->ki_filp));
		__entry->offset = iocb->ki_pos;
		__entry->count = iov_iter_count(iter);
		__entry->ino = file_inode(iocb->ki_filp)->i_ino;
		__entry->dev = file_inode(iocb->ki_filp)->i_sb->s_dev;
	),
	TP_printk("dev=(%d,%d) ino=%lu size=%lld pos=%lld bytecount=%zu",
		  MAJOR(__entry->dev), MINOR(__entry->dev),
		  __entry->ino, __entry->size, __entry->offset,
		  __entry->count)
);

DEFINE_EVENT(ntfs3_file_class, ntfs3_file_read_iter,
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter),
	TP_ARGS(iocb, iter)
);

DEFINE_EVENT(ntfs3_file_class, ntfs3_file_write_iter,
	TP_PROTO(struct kiocb *iocb, struct iov_iter *iter),
	TP_ARGS(iocb, iter)
);

#endif /* _TRACE_NTFS3_H */

#include <trace/define_trace.h>
