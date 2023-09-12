/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_ACCESSORS_H
#define BTRFS_ACCESSORS_H

#include <linux/stddef.h>

struct btrfs_map_token {
	struct extent_buffer *eb;
	char *kaddr;
	unsigned long offset;
};

void btrfs_init_map_token(struct btrfs_map_token *token, struct extent_buffer *eb);

/*
 * Some macros to generate set/get functions for the struct fields.  This
 * assumes there is a lefoo_to_cpu for every type, so lets make a simple one
 * for u8:
 */
#define le8_to_cpu(v) (v)
#define cpu_to_le8(v) (v)
#define __le8 u8

static inline u8 get_unaligned_le8(const void *p)
{
       return *(u8 *)p;
}

static inline void put_unaligned_le8(u8 val, void *p)
{
       *(u8 *)p = val;
}

#define read_eb_member(eb, ptr, type, member, result) (\
	read_extent_buffer(eb, (char *)(result),			\
			   ((unsigned long)(ptr)) +			\
			    offsetof(type, member),			\
			    sizeof_field(type, member)))

#define write_eb_member(eb, ptr, type, member, result) (\
	write_extent_buffer(eb, (char *)(result),			\
			   ((unsigned long)(ptr)) +			\
			    offsetof(type, member),			\
			    sizeof_field(type, member)))

#define DECLARE_BTRFS_SETGET_BITS(bits)					\
u##bits btrfs_get_token_##bits(struct btrfs_map_token *token,		\
			       const void *ptr, unsigned long off);	\
void btrfs_set_token_##bits(struct btrfs_map_token *token,		\
			    const void *ptr, unsigned long off,		\
			    u##bits val);				\
u##bits btrfs_get_##bits(const struct extent_buffer *eb,		\
			 const void *ptr, unsigned long off);		\
void btrfs_set_##bits(const struct extent_buffer *eb, void *ptr,	\
		      unsigned long off, u##bits val);

DECLARE_BTRFS_SETGET_BITS(8)
DECLARE_BTRFS_SETGET_BITS(16)
DECLARE_BTRFS_SETGET_BITS(32)
DECLARE_BTRFS_SETGET_BITS(64)

#define BTRFS_SETGET_FUNCS(name, type, member, bits)			\
static inline u##bits btrfs_##name(const struct extent_buffer *eb,	\
				   const type *s)			\
{									\
	static_assert(sizeof(u##bits) == sizeof_field(type, member));	\
	return btrfs_get_##bits(eb, s, offsetof(type, member));		\
}									\
static inline void btrfs_set_##name(const struct extent_buffer *eb, type *s, \
				    u##bits val)			\
{									\
	static_assert(sizeof(u##bits) == sizeof_field(type, member));	\
	btrfs_set_##bits(eb, s, offsetof(type, member), val);		\
}									\
static inline u##bits btrfs_token_##name(struct btrfs_map_token *token,	\
					 const type *s)			\
{									\
	static_assert(sizeof(u##bits) == sizeof_field(type, member));	\
	return btrfs_get_token_##bits(token, s, offsetof(type, member));\
}									\
static inline void btrfs_set_token_##name(struct btrfs_map_token *token,\
					  type *s, u##bits val)		\
{									\
	static_assert(sizeof(u##bits) == sizeof_field(type, member));	\
	btrfs_set_token_##bits(token, s, offsetof(type, member), val);	\
}

#define BTRFS_SETGET_HEADER_FUNCS(name, type, member, bits)		\
static inline u##bits btrfs_##name(const struct extent_buffer *eb)	\
{									\
	const type *p = page_address(eb->pages[0]) +			\
			offset_in_page(eb->start);			\
	return get_unaligned_le##bits(&p->member);			\
}									\
static inline void btrfs_set_##name(const struct extent_buffer *eb,	\
				    u##bits val)			\
{									\
	type *p = page_address(eb->pages[0]) + offset_in_page(eb->start); \
	put_unaligned_le##bits(val, &p->member);			\
}

#define BTRFS_SETGET_STACK_FUNCS(name, type, member, bits)		\
static inline u##bits btrfs_##name(const type *s)			\
{									\
	return get_unaligned_le##bits(&s->member);			\
}									\
static inline void btrfs_set_##name(type *s, u##bits val)		\
{									\
	put_unaligned_le##bits(val, &s->member);			\
}

static inline u64 btrfs_device_total_bytes(const struct extent_buffer *eb,
					   struct btrfs_dev_item *s)
{
	static_assert(sizeof(u64) == sizeof_field(struct btrfs_dev_item, total_bytes));
	return btrfs_get_64(eb, s, offsetof(struct btrfs_dev_item, total_bytes));
}
static inline void btrfs_set_device_total_bytes(const struct extent_buffer *eb,
						struct btrfs_dev_item *s,
						u64 val)
{
	static_assert(sizeof(u64) == sizeof_field(struct btrfs_dev_item, total_bytes));
	WARN_ON(!IS_ALIGNED(val, eb->fs_info->sectorsize));
	btrfs_set_64(eb, s, offsetof(struct btrfs_dev_item, total_bytes), val);
}

BTRFS_SETGET_FUNCS(device_type, struct btrfs_dev_item, type, 64);
BTRFS_SETGET_FUNCS(device_bytes_used, struct btrfs_dev_item, bytes_used, 64);
BTRFS_SETGET_FUNCS(device_io_align, struct btrfs_dev_item, io_align, 32);
BTRFS_SETGET_FUNCS(device_io_width, struct btrfs_dev_item, io_width, 32);
BTRFS_SETGET_FUNCS(device_start_offset, struct btrfs_dev_item, start_offset, 64);
BTRFS_SETGET_FUNCS(device_sector_size, struct btrfs_dev_item, sector_size, 32);
BTRFS_SETGET_FUNCS(device_id, struct btrfs_dev_item, devid, 64);
BTRFS_SETGET_FUNCS(device_group, struct btrfs_dev_item, dev_group, 32);
BTRFS_SETGET_FUNCS(device_seek_speed, struct btrfs_dev_item, seek_speed, 8);
BTRFS_SETGET_FUNCS(device_bandwidth, struct btrfs_dev_item, bandwidth, 8);
BTRFS_SETGET_FUNCS(device_generation, struct btrfs_dev_item, generation, 64);

BTRFS_SETGET_STACK_FUNCS(stack_device_type, struct btrfs_dev_item, type, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_total_bytes, struct btrfs_dev_item,
			 total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_bytes_used, struct btrfs_dev_item,
			 bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_io_align, struct btrfs_dev_item,
			 io_align, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_io_width, struct btrfs_dev_item,
			 io_width, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_sector_size, struct btrfs_dev_item,
			 sector_size, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_id, struct btrfs_dev_item, devid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_device_group, struct btrfs_dev_item, dev_group, 32);
BTRFS_SETGET_STACK_FUNCS(stack_device_seek_speed, struct btrfs_dev_item,
			 seek_speed, 8);
BTRFS_SETGET_STACK_FUNCS(stack_device_bandwidth, struct btrfs_dev_item,
			 bandwidth, 8);
BTRFS_SETGET_STACK_FUNCS(stack_device_generation, struct btrfs_dev_item,
			 generation, 64);

static inline unsigned long btrfs_device_uuid(struct btrfs_dev_item *d)
{
	return (unsigned long)d + offsetof(struct btrfs_dev_item, uuid);
}

static inline unsigned long btrfs_device_fsid(struct btrfs_dev_item *d)
{
	return (unsigned long)d + offsetof(struct btrfs_dev_item, fsid);
}

BTRFS_SETGET_FUNCS(chunk_length, struct btrfs_chunk, length, 64);
BTRFS_SETGET_FUNCS(chunk_owner, struct btrfs_chunk, owner, 64);
BTRFS_SETGET_FUNCS(chunk_stripe_len, struct btrfs_chunk, stripe_len, 64);
BTRFS_SETGET_FUNCS(chunk_io_align, struct btrfs_chunk, io_align, 32);
BTRFS_SETGET_FUNCS(chunk_io_width, struct btrfs_chunk, io_width, 32);
BTRFS_SETGET_FUNCS(chunk_sector_size, struct btrfs_chunk, sector_size, 32);
BTRFS_SETGET_FUNCS(chunk_type, struct btrfs_chunk, type, 64);
BTRFS_SETGET_FUNCS(chunk_num_stripes, struct btrfs_chunk, num_stripes, 16);
BTRFS_SETGET_FUNCS(chunk_sub_stripes, struct btrfs_chunk, sub_stripes, 16);
BTRFS_SETGET_FUNCS(stripe_devid, struct btrfs_stripe, devid, 64);
BTRFS_SETGET_FUNCS(stripe_offset, struct btrfs_stripe, offset, 64);

static inline char *btrfs_stripe_dev_uuid(struct btrfs_stripe *s)
{
	return (char *)s + offsetof(struct btrfs_stripe, dev_uuid);
}

BTRFS_SETGET_STACK_FUNCS(stack_chunk_length, struct btrfs_chunk, length, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_owner, struct btrfs_chunk, owner, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_stripe_len, struct btrfs_chunk,
			 stripe_len, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_io_align, struct btrfs_chunk, io_align, 32);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_io_width, struct btrfs_chunk, io_width, 32);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_sector_size, struct btrfs_chunk,
			 sector_size, 32);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_type, struct btrfs_chunk, type, 64);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_num_stripes, struct btrfs_chunk,
			 num_stripes, 16);
BTRFS_SETGET_STACK_FUNCS(stack_chunk_sub_stripes, struct btrfs_chunk,
			 sub_stripes, 16);
BTRFS_SETGET_STACK_FUNCS(stack_stripe_devid, struct btrfs_stripe, devid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_stripe_offset, struct btrfs_stripe, offset, 64);

static inline struct btrfs_stripe *btrfs_stripe_nr(struct btrfs_chunk *c, int nr)
{
	unsigned long offset = (unsigned long)c;

	offset += offsetof(struct btrfs_chunk, stripe);
	offset += nr * sizeof(struct btrfs_stripe);
	return (struct btrfs_stripe *)offset;
}

static inline char *btrfs_stripe_dev_uuid_nr(struct btrfs_chunk *c, int nr)
{
	return btrfs_stripe_dev_uuid(btrfs_stripe_nr(c, nr));
}

static inline u64 btrfs_stripe_offset_nr(const struct extent_buffer *eb,
					 struct btrfs_chunk *c, int nr)
{
	return btrfs_stripe_offset(eb, btrfs_stripe_nr(c, nr));
}

static inline void btrfs_set_stripe_offset_nr(struct extent_buffer *eb,
					      struct btrfs_chunk *c, int nr,
					      u64 val)
{
	btrfs_set_stripe_offset(eb, btrfs_stripe_nr(c, nr), val);
}

static inline u64 btrfs_stripe_devid_nr(const struct extent_buffer *eb,
					 struct btrfs_chunk *c, int nr)
{
	return btrfs_stripe_devid(eb, btrfs_stripe_nr(c, nr));
}

static inline void btrfs_set_stripe_devid_nr(struct extent_buffer *eb,
					     struct btrfs_chunk *c, int nr,
					     u64 val)
{
	btrfs_set_stripe_devid(eb, btrfs_stripe_nr(c, nr), val);
}

/* struct btrfs_block_group_item */
BTRFS_SETGET_STACK_FUNCS(stack_block_group_used, struct btrfs_block_group_item,
			 used, 64);
BTRFS_SETGET_FUNCS(block_group_used, struct btrfs_block_group_item, used, 64);
BTRFS_SETGET_STACK_FUNCS(stack_block_group_chunk_objectid,
			struct btrfs_block_group_item, chunk_objectid, 64);

BTRFS_SETGET_FUNCS(block_group_chunk_objectid,
		   struct btrfs_block_group_item, chunk_objectid, 64);
BTRFS_SETGET_FUNCS(block_group_flags, struct btrfs_block_group_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(stack_block_group_flags,
			struct btrfs_block_group_item, flags, 64);

/* struct btrfs_free_space_info */
BTRFS_SETGET_FUNCS(free_space_extent_count, struct btrfs_free_space_info,
		   extent_count, 32);
BTRFS_SETGET_FUNCS(free_space_flags, struct btrfs_free_space_info, flags, 32);

/* struct btrfs_inode_ref */
BTRFS_SETGET_FUNCS(inode_ref_name_len, struct btrfs_inode_ref, name_len, 16);
BTRFS_SETGET_FUNCS(inode_ref_index, struct btrfs_inode_ref, index, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_ref_name_len, struct btrfs_inode_ref, name_len, 16);
BTRFS_SETGET_STACK_FUNCS(stack_inode_ref_index, struct btrfs_inode_ref, index, 64);

/* struct btrfs_inode_extref */
BTRFS_SETGET_FUNCS(inode_extref_parent, struct btrfs_inode_extref,
		   parent_objectid, 64);
BTRFS_SETGET_FUNCS(inode_extref_name_len, struct btrfs_inode_extref,
		   name_len, 16);
BTRFS_SETGET_FUNCS(inode_extref_index, struct btrfs_inode_extref, index, 64);

/* struct btrfs_inode_item */
BTRFS_SETGET_FUNCS(inode_generation, struct btrfs_inode_item, generation, 64);
BTRFS_SETGET_FUNCS(inode_sequence, struct btrfs_inode_item, sequence, 64);
BTRFS_SETGET_FUNCS(inode_transid, struct btrfs_inode_item, transid, 64);
BTRFS_SETGET_FUNCS(inode_size, struct btrfs_inode_item, size, 64);
BTRFS_SETGET_FUNCS(inode_nbytes, struct btrfs_inode_item, nbytes, 64);
BTRFS_SETGET_FUNCS(inode_block_group, struct btrfs_inode_item, block_group, 64);
BTRFS_SETGET_FUNCS(inode_nlink, struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_FUNCS(inode_uid, struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_FUNCS(inode_gid, struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_FUNCS(inode_mode, struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_FUNCS(inode_rdev, struct btrfs_inode_item, rdev, 64);
BTRFS_SETGET_FUNCS(inode_flags, struct btrfs_inode_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_generation, struct btrfs_inode_item,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_sequence, struct btrfs_inode_item,
			 sequence, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_transid, struct btrfs_inode_item,
			 transid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_size, struct btrfs_inode_item, size, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nbytes, struct btrfs_inode_item, nbytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_block_group, struct btrfs_inode_item,
			 block_group, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_nlink, struct btrfs_inode_item, nlink, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_uid, struct btrfs_inode_item, uid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_gid, struct btrfs_inode_item, gid, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_mode, struct btrfs_inode_item, mode, 32);
BTRFS_SETGET_STACK_FUNCS(stack_inode_rdev, struct btrfs_inode_item, rdev, 64);
BTRFS_SETGET_STACK_FUNCS(stack_inode_flags, struct btrfs_inode_item, flags, 64);
BTRFS_SETGET_FUNCS(timespec_sec, struct btrfs_timespec, sec, 64);
BTRFS_SETGET_FUNCS(timespec_nsec, struct btrfs_timespec, nsec, 32);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_sec, struct btrfs_timespec, sec, 64);
BTRFS_SETGET_STACK_FUNCS(stack_timespec_nsec, struct btrfs_timespec, nsec, 32);

/* struct btrfs_dev_extent */
BTRFS_SETGET_FUNCS(dev_extent_chunk_tree, struct btrfs_dev_extent, chunk_tree, 64);
BTRFS_SETGET_FUNCS(dev_extent_chunk_objectid, struct btrfs_dev_extent,
		   chunk_objectid, 64);
BTRFS_SETGET_FUNCS(dev_extent_chunk_offset, struct btrfs_dev_extent,
		   chunk_offset, 64);
BTRFS_SETGET_FUNCS(dev_extent_length, struct btrfs_dev_extent, length, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_extent_chunk_tree, struct btrfs_dev_extent,
			 chunk_tree, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_extent_chunk_objectid, struct btrfs_dev_extent,
			 chunk_objectid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_extent_chunk_offset, struct btrfs_dev_extent,
			 chunk_offset, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_extent_length, struct btrfs_dev_extent, length, 64);

BTRFS_SETGET_FUNCS(extent_refs, struct btrfs_extent_item, refs, 64);
BTRFS_SETGET_FUNCS(extent_generation, struct btrfs_extent_item, generation, 64);
BTRFS_SETGET_FUNCS(extent_flags, struct btrfs_extent_item, flags, 64);

BTRFS_SETGET_FUNCS(tree_block_level, struct btrfs_tree_block_info, level, 8);

static inline void btrfs_tree_block_key(const struct extent_buffer *eb,
					struct btrfs_tree_block_info *item,
					struct btrfs_disk_key *key)
{
	read_eb_member(eb, item, struct btrfs_tree_block_info, key, key);
}

static inline void btrfs_set_tree_block_key(const struct extent_buffer *eb,
					    struct btrfs_tree_block_info *item,
					    struct btrfs_disk_key *key)
{
	write_eb_member(eb, item, struct btrfs_tree_block_info, key, key);
}

BTRFS_SETGET_FUNCS(extent_data_ref_root, struct btrfs_extent_data_ref, root, 64);
BTRFS_SETGET_FUNCS(extent_data_ref_objectid, struct btrfs_extent_data_ref,
		   objectid, 64);
BTRFS_SETGET_FUNCS(extent_data_ref_offset, struct btrfs_extent_data_ref,
		   offset, 64);
BTRFS_SETGET_FUNCS(extent_data_ref_count, struct btrfs_extent_data_ref, count, 32);

BTRFS_SETGET_FUNCS(shared_data_ref_count, struct btrfs_shared_data_ref, count, 32);

BTRFS_SETGET_FUNCS(extent_inline_ref_type, struct btrfs_extent_inline_ref,
		   type, 8);
BTRFS_SETGET_FUNCS(extent_inline_ref_offset, struct btrfs_extent_inline_ref,
		   offset, 64);

static inline u32 btrfs_extent_inline_ref_size(int type)
{
	if (type == BTRFS_TREE_BLOCK_REF_KEY ||
	    type == BTRFS_SHARED_BLOCK_REF_KEY)
		return sizeof(struct btrfs_extent_inline_ref);
	if (type == BTRFS_SHARED_DATA_REF_KEY)
		return sizeof(struct btrfs_shared_data_ref) +
		       sizeof(struct btrfs_extent_inline_ref);
	if (type == BTRFS_EXTENT_DATA_REF_KEY)
		return sizeof(struct btrfs_extent_data_ref) +
		       offsetof(struct btrfs_extent_inline_ref, offset);
	return 0;
}

/* struct btrfs_node */
BTRFS_SETGET_FUNCS(key_blockptr, struct btrfs_key_ptr, blockptr, 64);
BTRFS_SETGET_FUNCS(key_generation, struct btrfs_key_ptr, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_key_blockptr, struct btrfs_key_ptr, blockptr, 64);
BTRFS_SETGET_STACK_FUNCS(stack_key_generation, struct btrfs_key_ptr,
			 generation, 64);

static inline u64 btrfs_node_blockptr(const struct extent_buffer *eb, int nr)
{
	unsigned long ptr;

	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	return btrfs_key_blockptr(eb, (struct btrfs_key_ptr *)ptr);
}

static inline void btrfs_set_node_blockptr(const struct extent_buffer *eb,
					   int nr, u64 val)
{
	unsigned long ptr;

	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	btrfs_set_key_blockptr(eb, (struct btrfs_key_ptr *)ptr, val);
}

static inline u64 btrfs_node_ptr_generation(const struct extent_buffer *eb, int nr)
{
	unsigned long ptr;

	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	return btrfs_key_generation(eb, (struct btrfs_key_ptr *)ptr);
}

static inline void btrfs_set_node_ptr_generation(const struct extent_buffer *eb,
						 int nr, u64 val)
{
	unsigned long ptr;

	ptr = offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
	btrfs_set_key_generation(eb, (struct btrfs_key_ptr *)ptr, val);
}

static inline unsigned long btrfs_node_key_ptr_offset(const struct extent_buffer *eb, int nr)
{
	return offsetof(struct btrfs_node, ptrs) +
		sizeof(struct btrfs_key_ptr) * nr;
}

void btrfs_node_key(const struct extent_buffer *eb,
		    struct btrfs_disk_key *disk_key, int nr);

static inline void btrfs_set_node_key(const struct extent_buffer *eb,
				      struct btrfs_disk_key *disk_key, int nr)
{
	unsigned long ptr;

	ptr = btrfs_node_key_ptr_offset(eb, nr);
	write_eb_member(eb, (struct btrfs_key_ptr *)ptr,
		        struct btrfs_key_ptr, key, disk_key);
}

/* struct btrfs_item */
BTRFS_SETGET_FUNCS(raw_item_offset, struct btrfs_item, offset, 32);
BTRFS_SETGET_FUNCS(raw_item_size, struct btrfs_item, size, 32);
BTRFS_SETGET_STACK_FUNCS(stack_item_offset, struct btrfs_item, offset, 32);
BTRFS_SETGET_STACK_FUNCS(stack_item_size, struct btrfs_item, size, 32);

static inline unsigned long btrfs_item_nr_offset(const struct extent_buffer *eb, int nr)
{
	return offsetof(struct btrfs_leaf, items) +
		sizeof(struct btrfs_item) * nr;
}

static inline struct btrfs_item *btrfs_item_nr(const struct extent_buffer *eb, int nr)
{
	return (struct btrfs_item *)btrfs_item_nr_offset(eb, nr);
}

#define BTRFS_ITEM_SETGET_FUNCS(member)						\
static inline u32 btrfs_item_##member(const struct extent_buffer *eb, int slot)	\
{										\
	return btrfs_raw_item_##member(eb, btrfs_item_nr(eb, slot));		\
}										\
static inline void btrfs_set_item_##member(const struct extent_buffer *eb,	\
					   int slot, u32 val)			\
{										\
	btrfs_set_raw_item_##member(eb, btrfs_item_nr(eb, slot), val);		\
}										\
static inline u32 btrfs_token_item_##member(struct btrfs_map_token *token,	\
					    int slot)				\
{										\
	struct btrfs_item *item = btrfs_item_nr(token->eb, slot);		\
	return btrfs_token_raw_item_##member(token, item);			\
}										\
static inline void btrfs_set_token_item_##member(struct btrfs_map_token *token,	\
						 int slot, u32 val)		\
{										\
	struct btrfs_item *item = btrfs_item_nr(token->eb, slot);		\
	btrfs_set_token_raw_item_##member(token, item, val);			\
}

BTRFS_ITEM_SETGET_FUNCS(offset)
BTRFS_ITEM_SETGET_FUNCS(size);

static inline u32 btrfs_item_data_end(const struct extent_buffer *eb, int nr)
{
	return btrfs_item_offset(eb, nr) + btrfs_item_size(eb, nr);
}

static inline void btrfs_item_key(const struct extent_buffer *eb,
			   struct btrfs_disk_key *disk_key, int nr)
{
	struct btrfs_item *item = btrfs_item_nr(eb, nr);

	read_eb_member(eb, item, struct btrfs_item, key, disk_key);
}

static inline void btrfs_set_item_key(struct extent_buffer *eb,
				      struct btrfs_disk_key *disk_key, int nr)
{
	struct btrfs_item *item = btrfs_item_nr(eb, nr);

	write_eb_member(eb, item, struct btrfs_item, key, disk_key);
}

BTRFS_SETGET_FUNCS(dir_log_end, struct btrfs_dir_log_item, end, 64);

/* struct btrfs_root_ref */
BTRFS_SETGET_FUNCS(root_ref_dirid, struct btrfs_root_ref, dirid, 64);
BTRFS_SETGET_FUNCS(root_ref_sequence, struct btrfs_root_ref, sequence, 64);
BTRFS_SETGET_FUNCS(root_ref_name_len, struct btrfs_root_ref, name_len, 16);
BTRFS_SETGET_STACK_FUNCS(stack_root_ref_dirid, struct btrfs_root_ref, dirid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_root_ref_sequence, struct btrfs_root_ref, sequence, 64);
BTRFS_SETGET_STACK_FUNCS(stack_root_ref_name_len, struct btrfs_root_ref, name_len, 16);

/* struct btrfs_dir_item */
BTRFS_SETGET_FUNCS(dir_data_len, struct btrfs_dir_item, data_len, 16);
BTRFS_SETGET_FUNCS(dir_flags, struct btrfs_dir_item, type, 8);
BTRFS_SETGET_FUNCS(dir_name_len, struct btrfs_dir_item, name_len, 16);
BTRFS_SETGET_FUNCS(dir_transid, struct btrfs_dir_item, transid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dir_flags, struct btrfs_dir_item, type, 8);
BTRFS_SETGET_STACK_FUNCS(stack_dir_data_len, struct btrfs_dir_item, data_len, 16);
BTRFS_SETGET_STACK_FUNCS(stack_dir_name_len, struct btrfs_dir_item, name_len, 16);
BTRFS_SETGET_STACK_FUNCS(stack_dir_transid, struct btrfs_dir_item, transid, 64);

static inline u8 btrfs_dir_ftype(const struct extent_buffer *eb,
				 const struct btrfs_dir_item *item)
{
	return btrfs_dir_flags_to_ftype(btrfs_dir_flags(eb, item));
}

static inline u8 btrfs_stack_dir_ftype(const struct btrfs_dir_item *item)
{
	return btrfs_dir_flags_to_ftype(btrfs_stack_dir_flags(item));
}

static inline void btrfs_dir_item_key(const struct extent_buffer *eb,
				      const struct btrfs_dir_item *item,
				      struct btrfs_disk_key *key)
{
	read_eb_member(eb, item, struct btrfs_dir_item, location, key);
}

static inline void btrfs_set_dir_item_key(struct extent_buffer *eb,
					  struct btrfs_dir_item *item,
					  const struct btrfs_disk_key *key)
{
	write_eb_member(eb, item, struct btrfs_dir_item, location, key);
}

BTRFS_SETGET_FUNCS(free_space_entries, struct btrfs_free_space_header,
		   num_entries, 64);
BTRFS_SETGET_FUNCS(free_space_bitmaps, struct btrfs_free_space_header,
		   num_bitmaps, 64);
BTRFS_SETGET_FUNCS(free_space_generation, struct btrfs_free_space_header,
		   generation, 64);

static inline void btrfs_free_space_key(const struct extent_buffer *eb,
					const struct btrfs_free_space_header *h,
					struct btrfs_disk_key *key)
{
	read_eb_member(eb, h, struct btrfs_free_space_header, location, key);
}

static inline void btrfs_set_free_space_key(struct extent_buffer *eb,
					    struct btrfs_free_space_header *h,
					    const struct btrfs_disk_key *key)
{
	write_eb_member(eb, h, struct btrfs_free_space_header, location, key);
}

/* struct btrfs_disk_key */
BTRFS_SETGET_STACK_FUNCS(disk_key_objectid, struct btrfs_disk_key, objectid, 64);
BTRFS_SETGET_STACK_FUNCS(disk_key_offset, struct btrfs_disk_key, offset, 64);
BTRFS_SETGET_STACK_FUNCS(disk_key_type, struct btrfs_disk_key, type, 8);

#ifdef __LITTLE_ENDIAN

/*
 * Optimized helpers for little-endian architectures where CPU and on-disk
 * structures have the same endianness and we can skip conversions.
 */

static inline void btrfs_disk_key_to_cpu(struct btrfs_key *cpu_key,
					 const struct btrfs_disk_key *disk_key)
{
	memcpy(cpu_key, disk_key, sizeof(struct btrfs_key));
}

static inline void btrfs_cpu_key_to_disk(struct btrfs_disk_key *disk_key,
					 const struct btrfs_key *cpu_key)
{
	memcpy(disk_key, cpu_key, sizeof(struct btrfs_key));
}

static inline void btrfs_node_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *cpu_key, int nr)
{
	struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)cpu_key;

	btrfs_node_key(eb, disk_key, nr);
}

static inline void btrfs_item_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *cpu_key, int nr)
{
	struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)cpu_key;

	btrfs_item_key(eb, disk_key, nr);
}

static inline void btrfs_dir_item_key_to_cpu(const struct extent_buffer *eb,
					     const struct btrfs_dir_item *item,
					     struct btrfs_key *cpu_key)
{
	struct btrfs_disk_key *disk_key = (struct btrfs_disk_key *)cpu_key;

	btrfs_dir_item_key(eb, item, disk_key);
}

#else

static inline void btrfs_disk_key_to_cpu(struct btrfs_key *cpu,
					 const struct btrfs_disk_key *disk)
{
	cpu->offset = le64_to_cpu(disk->offset);
	cpu->type = disk->type;
	cpu->objectid = le64_to_cpu(disk->objectid);
}

static inline void btrfs_cpu_key_to_disk(struct btrfs_disk_key *disk,
					 const struct btrfs_key *cpu)
{
	disk->offset = cpu_to_le64(cpu->offset);
	disk->type = cpu->type;
	disk->objectid = cpu_to_le64(cpu->objectid);
}

static inline void btrfs_node_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *key, int nr)
{
	struct btrfs_disk_key disk_key;

	btrfs_node_key(eb, &disk_key, nr);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

static inline void btrfs_item_key_to_cpu(const struct extent_buffer *eb,
					 struct btrfs_key *key, int nr)
{
	struct btrfs_disk_key disk_key;

	btrfs_item_key(eb, &disk_key, nr);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

static inline void btrfs_dir_item_key_to_cpu(const struct extent_buffer *eb,
					     const struct btrfs_dir_item *item,
					     struct btrfs_key *key)
{
	struct btrfs_disk_key disk_key;

	btrfs_dir_item_key(eb, item, &disk_key);
	btrfs_disk_key_to_cpu(key, &disk_key);
}

#endif

/* struct btrfs_header */
BTRFS_SETGET_HEADER_FUNCS(header_bytenr, struct btrfs_header, bytenr, 64);
BTRFS_SETGET_HEADER_FUNCS(header_generation, struct btrfs_header, generation, 64);
BTRFS_SETGET_HEADER_FUNCS(header_owner, struct btrfs_header, owner, 64);
BTRFS_SETGET_HEADER_FUNCS(header_nritems, struct btrfs_header, nritems, 32);
BTRFS_SETGET_HEADER_FUNCS(header_flags, struct btrfs_header, flags, 64);
BTRFS_SETGET_HEADER_FUNCS(header_level, struct btrfs_header, level, 8);
BTRFS_SETGET_STACK_FUNCS(stack_header_generation, struct btrfs_header,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_header_owner, struct btrfs_header, owner, 64);
BTRFS_SETGET_STACK_FUNCS(stack_header_nritems, struct btrfs_header, nritems, 32);
BTRFS_SETGET_STACK_FUNCS(stack_header_bytenr, struct btrfs_header, bytenr, 64);

static inline int btrfs_header_flag(const struct extent_buffer *eb, u64 flag)
{
	return (btrfs_header_flags(eb) & flag) == flag;
}

static inline void btrfs_set_header_flag(struct extent_buffer *eb, u64 flag)
{
	u64 flags = btrfs_header_flags(eb);

	btrfs_set_header_flags(eb, flags | flag);
}

static inline void btrfs_clear_header_flag(struct extent_buffer *eb, u64 flag)
{
	u64 flags = btrfs_header_flags(eb);

	btrfs_set_header_flags(eb, flags & ~flag);
}

static inline int btrfs_header_backref_rev(const struct extent_buffer *eb)
{
	u64 flags = btrfs_header_flags(eb);

	return flags >> BTRFS_BACKREF_REV_SHIFT;
}

static inline void btrfs_set_header_backref_rev(struct extent_buffer *eb, int rev)
{
	u64 flags = btrfs_header_flags(eb);

	flags &= ~BTRFS_BACKREF_REV_MASK;
	flags |= (u64)rev << BTRFS_BACKREF_REV_SHIFT;
	btrfs_set_header_flags(eb, flags);
}

static inline int btrfs_is_leaf(const struct extent_buffer *eb)
{
	return btrfs_header_level(eb) == 0;
}

/* struct btrfs_root_item */
BTRFS_SETGET_FUNCS(disk_root_generation, struct btrfs_root_item, generation, 64);
BTRFS_SETGET_FUNCS(disk_root_refs, struct btrfs_root_item, refs, 32);
BTRFS_SETGET_FUNCS(disk_root_bytenr, struct btrfs_root_item, bytenr, 64);
BTRFS_SETGET_FUNCS(disk_root_level, struct btrfs_root_item, level, 8);

BTRFS_SETGET_STACK_FUNCS(root_generation, struct btrfs_root_item, generation, 64);
BTRFS_SETGET_STACK_FUNCS(root_bytenr, struct btrfs_root_item, bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(root_drop_level, struct btrfs_root_item, drop_level, 8);
BTRFS_SETGET_STACK_FUNCS(root_level, struct btrfs_root_item, level, 8);
BTRFS_SETGET_STACK_FUNCS(root_dirid, struct btrfs_root_item, root_dirid, 64);
BTRFS_SETGET_STACK_FUNCS(root_refs, struct btrfs_root_item, refs, 32);
BTRFS_SETGET_STACK_FUNCS(root_flags, struct btrfs_root_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(root_used, struct btrfs_root_item, bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(root_limit, struct btrfs_root_item, byte_limit, 64);
BTRFS_SETGET_STACK_FUNCS(root_last_snapshot, struct btrfs_root_item,
			 last_snapshot, 64);
BTRFS_SETGET_STACK_FUNCS(root_generation_v2, struct btrfs_root_item,
			 generation_v2, 64);
BTRFS_SETGET_STACK_FUNCS(root_ctransid, struct btrfs_root_item, ctransid, 64);
BTRFS_SETGET_STACK_FUNCS(root_otransid, struct btrfs_root_item, otransid, 64);
BTRFS_SETGET_STACK_FUNCS(root_stransid, struct btrfs_root_item, stransid, 64);
BTRFS_SETGET_STACK_FUNCS(root_rtransid, struct btrfs_root_item, rtransid, 64);

/* struct btrfs_root_backup */
BTRFS_SETGET_STACK_FUNCS(backup_tree_root, struct btrfs_root_backup,
		   tree_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_tree_root_gen, struct btrfs_root_backup,
		   tree_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_tree_root_level, struct btrfs_root_backup,
		   tree_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_chunk_root, struct btrfs_root_backup,
		   chunk_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_chunk_root_gen, struct btrfs_root_backup,
		   chunk_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_chunk_root_level, struct btrfs_root_backup,
		   chunk_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_extent_root, struct btrfs_root_backup,
		   extent_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_extent_root_gen, struct btrfs_root_backup,
		   extent_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_extent_root_level, struct btrfs_root_backup,
		   extent_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_fs_root, struct btrfs_root_backup,
		   fs_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_fs_root_gen, struct btrfs_root_backup,
		   fs_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_fs_root_level, struct btrfs_root_backup,
		   fs_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_dev_root, struct btrfs_root_backup,
		   dev_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_dev_root_gen, struct btrfs_root_backup,
		   dev_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_dev_root_level, struct btrfs_root_backup,
		   dev_root_level, 8);

BTRFS_SETGET_STACK_FUNCS(backup_csum_root, struct btrfs_root_backup,
		   csum_root, 64);
BTRFS_SETGET_STACK_FUNCS(backup_csum_root_gen, struct btrfs_root_backup,
		   csum_root_gen, 64);
BTRFS_SETGET_STACK_FUNCS(backup_csum_root_level, struct btrfs_root_backup,
		   csum_root_level, 8);
BTRFS_SETGET_STACK_FUNCS(backup_total_bytes, struct btrfs_root_backup,
		   total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(backup_bytes_used, struct btrfs_root_backup,
		   bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(backup_num_devices, struct btrfs_root_backup,
		   num_devices, 64);

/* struct btrfs_balance_item */
BTRFS_SETGET_FUNCS(balance_flags, struct btrfs_balance_item, flags, 64);

static inline void btrfs_balance_data(const struct extent_buffer *eb,
				      const struct btrfs_balance_item *bi,
				      struct btrfs_disk_balance_args *ba)
{
	read_eb_member(eb, bi, struct btrfs_balance_item, data, ba);
}

static inline void btrfs_set_balance_data(struct extent_buffer *eb,
					  struct btrfs_balance_item *bi,
					  const struct btrfs_disk_balance_args *ba)
{
	write_eb_member(eb, bi, struct btrfs_balance_item, data, ba);
}

static inline void btrfs_balance_meta(const struct extent_buffer *eb,
				      const struct btrfs_balance_item *bi,
				      struct btrfs_disk_balance_args *ba)
{
	read_eb_member(eb, bi, struct btrfs_balance_item, meta, ba);
}

static inline void btrfs_set_balance_meta(struct extent_buffer *eb,
					  struct btrfs_balance_item *bi,
					  const struct btrfs_disk_balance_args *ba)
{
	write_eb_member(eb, bi, struct btrfs_balance_item, meta, ba);
}

static inline void btrfs_balance_sys(const struct extent_buffer *eb,
				     const struct btrfs_balance_item *bi,
				     struct btrfs_disk_balance_args *ba)
{
	read_eb_member(eb, bi, struct btrfs_balance_item, sys, ba);
}

static inline void btrfs_set_balance_sys(struct extent_buffer *eb,
					 struct btrfs_balance_item *bi,
					 const struct btrfs_disk_balance_args *ba)
{
	write_eb_member(eb, bi, struct btrfs_balance_item, sys, ba);
}

static inline void btrfs_disk_balance_args_to_cpu(struct btrfs_balance_args *cpu,
			       const struct btrfs_disk_balance_args *disk)
{
	memset(cpu, 0, sizeof(*cpu));

	cpu->profiles = le64_to_cpu(disk->profiles);
	cpu->usage = le64_to_cpu(disk->usage);
	cpu->devid = le64_to_cpu(disk->devid);
	cpu->pstart = le64_to_cpu(disk->pstart);
	cpu->pend = le64_to_cpu(disk->pend);
	cpu->vstart = le64_to_cpu(disk->vstart);
	cpu->vend = le64_to_cpu(disk->vend);
	cpu->target = le64_to_cpu(disk->target);
	cpu->flags = le64_to_cpu(disk->flags);
	cpu->limit = le64_to_cpu(disk->limit);
	cpu->stripes_min = le32_to_cpu(disk->stripes_min);
	cpu->stripes_max = le32_to_cpu(disk->stripes_max);
}

static inline void btrfs_cpu_balance_args_to_disk(
				struct btrfs_disk_balance_args *disk,
				const struct btrfs_balance_args *cpu)
{
	memset(disk, 0, sizeof(*disk));

	disk->profiles = cpu_to_le64(cpu->profiles);
	disk->usage = cpu_to_le64(cpu->usage);
	disk->devid = cpu_to_le64(cpu->devid);
	disk->pstart = cpu_to_le64(cpu->pstart);
	disk->pend = cpu_to_le64(cpu->pend);
	disk->vstart = cpu_to_le64(cpu->vstart);
	disk->vend = cpu_to_le64(cpu->vend);
	disk->target = cpu_to_le64(cpu->target);
	disk->flags = cpu_to_le64(cpu->flags);
	disk->limit = cpu_to_le64(cpu->limit);
	disk->stripes_min = cpu_to_le32(cpu->stripes_min);
	disk->stripes_max = cpu_to_le32(cpu->stripes_max);
}

/* struct btrfs_super_block */
BTRFS_SETGET_STACK_FUNCS(super_bytenr, struct btrfs_super_block, bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(super_flags, struct btrfs_super_block, flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_generation, struct btrfs_super_block,
			 generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_root, struct btrfs_super_block, root, 64);
BTRFS_SETGET_STACK_FUNCS(super_sys_array_size,
			 struct btrfs_super_block, sys_chunk_array_size, 32);
BTRFS_SETGET_STACK_FUNCS(super_chunk_root_generation,
			 struct btrfs_super_block, chunk_root_generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_root_level, struct btrfs_super_block,
			 root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_chunk_root, struct btrfs_super_block,
			 chunk_root, 64);
BTRFS_SETGET_STACK_FUNCS(super_chunk_root_level, struct btrfs_super_block,
			 chunk_root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_log_root, struct btrfs_super_block, log_root, 64);
BTRFS_SETGET_STACK_FUNCS(super_log_root_level, struct btrfs_super_block,
			 log_root_level, 8);
BTRFS_SETGET_STACK_FUNCS(super_total_bytes, struct btrfs_super_block,
			 total_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(super_bytes_used, struct btrfs_super_block,
			 bytes_used, 64);
BTRFS_SETGET_STACK_FUNCS(super_sectorsize, struct btrfs_super_block,
			 sectorsize, 32);
BTRFS_SETGET_STACK_FUNCS(super_nodesize, struct btrfs_super_block,
			 nodesize, 32);
BTRFS_SETGET_STACK_FUNCS(super_stripesize, struct btrfs_super_block,
			 stripesize, 32);
BTRFS_SETGET_STACK_FUNCS(super_root_dir, struct btrfs_super_block,
			 root_dir_objectid, 64);
BTRFS_SETGET_STACK_FUNCS(super_num_devices, struct btrfs_super_block,
			 num_devices, 64);
BTRFS_SETGET_STACK_FUNCS(super_compat_flags, struct btrfs_super_block,
			 compat_flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_compat_ro_flags, struct btrfs_super_block,
			 compat_ro_flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_incompat_flags, struct btrfs_super_block,
			 incompat_flags, 64);
BTRFS_SETGET_STACK_FUNCS(super_csum_type, struct btrfs_super_block,
			 csum_type, 16);
BTRFS_SETGET_STACK_FUNCS(super_cache_generation, struct btrfs_super_block,
			 cache_generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_magic, struct btrfs_super_block, magic, 64);
BTRFS_SETGET_STACK_FUNCS(super_uuid_tree_generation, struct btrfs_super_block,
			 uuid_tree_generation, 64);
BTRFS_SETGET_STACK_FUNCS(super_nr_global_roots, struct btrfs_super_block,
			 nr_global_roots, 64);

/* struct btrfs_file_extent_item */
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_type, struct btrfs_file_extent_item,
			 type, 8);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_disk_bytenr,
			 struct btrfs_file_extent_item, disk_bytenr, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_offset,
			 struct btrfs_file_extent_item, offset, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_generation,
			 struct btrfs_file_extent_item, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_num_bytes,
			 struct btrfs_file_extent_item, num_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_ram_bytes,
			 struct btrfs_file_extent_item, ram_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_disk_num_bytes,
			 struct btrfs_file_extent_item, disk_num_bytes, 64);
BTRFS_SETGET_STACK_FUNCS(stack_file_extent_compression,
			 struct btrfs_file_extent_item, compression, 8);


BTRFS_SETGET_FUNCS(file_extent_type, struct btrfs_file_extent_item, type, 8);
BTRFS_SETGET_FUNCS(file_extent_disk_bytenr, struct btrfs_file_extent_item,
		   disk_bytenr, 64);
BTRFS_SETGET_FUNCS(file_extent_generation, struct btrfs_file_extent_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(file_extent_disk_num_bytes, struct btrfs_file_extent_item,
		   disk_num_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_offset, struct btrfs_file_extent_item,
		  offset, 64);
BTRFS_SETGET_FUNCS(file_extent_num_bytes, struct btrfs_file_extent_item,
		   num_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_ram_bytes, struct btrfs_file_extent_item,
		   ram_bytes, 64);
BTRFS_SETGET_FUNCS(file_extent_compression, struct btrfs_file_extent_item,
		   compression, 8);
BTRFS_SETGET_FUNCS(file_extent_encryption, struct btrfs_file_extent_item,
		   encryption, 8);
BTRFS_SETGET_FUNCS(file_extent_other_encoding, struct btrfs_file_extent_item,
		   other_encoding, 16);

/* btrfs_qgroup_status_item */
BTRFS_SETGET_FUNCS(qgroup_status_generation, struct btrfs_qgroup_status_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(qgroup_status_version, struct btrfs_qgroup_status_item,
		   version, 64);
BTRFS_SETGET_FUNCS(qgroup_status_flags, struct btrfs_qgroup_status_item,
		   flags, 64);
BTRFS_SETGET_FUNCS(qgroup_status_rescan, struct btrfs_qgroup_status_item,
		   rescan, 64);

/* btrfs_qgroup_info_item */
BTRFS_SETGET_FUNCS(qgroup_info_generation, struct btrfs_qgroup_info_item,
		   generation, 64);
BTRFS_SETGET_FUNCS(qgroup_info_rfer, struct btrfs_qgroup_info_item, rfer, 64);
BTRFS_SETGET_FUNCS(qgroup_info_rfer_cmpr, struct btrfs_qgroup_info_item,
		   rfer_cmpr, 64);
BTRFS_SETGET_FUNCS(qgroup_info_excl, struct btrfs_qgroup_info_item, excl, 64);
BTRFS_SETGET_FUNCS(qgroup_info_excl_cmpr, struct btrfs_qgroup_info_item,
		   excl_cmpr, 64);

BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_generation,
			 struct btrfs_qgroup_info_item, generation, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_rfer, struct btrfs_qgroup_info_item,
			 rfer, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_rfer_cmpr,
			 struct btrfs_qgroup_info_item, rfer_cmpr, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_excl, struct btrfs_qgroup_info_item,
			 excl, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_info_excl_cmpr,
			 struct btrfs_qgroup_info_item, excl_cmpr, 64);

/* btrfs_qgroup_limit_item */
BTRFS_SETGET_FUNCS(qgroup_limit_flags, struct btrfs_qgroup_limit_item, flags, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_max_rfer, struct btrfs_qgroup_limit_item,
		   max_rfer, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_max_excl, struct btrfs_qgroup_limit_item,
		   max_excl, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_rsv_rfer, struct btrfs_qgroup_limit_item,
		   rsv_rfer, 64);
BTRFS_SETGET_FUNCS(qgroup_limit_rsv_excl, struct btrfs_qgroup_limit_item,
		   rsv_excl, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_limit_flags,
			 struct btrfs_qgroup_limit_item, flags, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_limit_max_rfer,
			 struct btrfs_qgroup_limit_item, max_rfer, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_limit_max_excl,
			 struct btrfs_qgroup_limit_item, max_excl, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_limit_rsv_rfer,
			 struct btrfs_qgroup_limit_item, rsv_rfer, 64);
BTRFS_SETGET_STACK_FUNCS(stack_qgroup_limit_rsv_excl,
			 struct btrfs_qgroup_limit_item, rsv_excl, 64);

/* btrfs_dev_replace_item */
BTRFS_SETGET_FUNCS(dev_replace_src_devid,
		   struct btrfs_dev_replace_item, src_devid, 64);
BTRFS_SETGET_FUNCS(dev_replace_cont_reading_from_srcdev_mode,
		   struct btrfs_dev_replace_item, cont_reading_from_srcdev_mode,
		   64);
BTRFS_SETGET_FUNCS(dev_replace_replace_state, struct btrfs_dev_replace_item,
		   replace_state, 64);
BTRFS_SETGET_FUNCS(dev_replace_time_started, struct btrfs_dev_replace_item,
		   time_started, 64);
BTRFS_SETGET_FUNCS(dev_replace_time_stopped, struct btrfs_dev_replace_item,
		   time_stopped, 64);
BTRFS_SETGET_FUNCS(dev_replace_num_write_errors, struct btrfs_dev_replace_item,
		   num_write_errors, 64);
BTRFS_SETGET_FUNCS(dev_replace_num_uncorrectable_read_errors,
		   struct btrfs_dev_replace_item, num_uncorrectable_read_errors,
		   64);
BTRFS_SETGET_FUNCS(dev_replace_cursor_left, struct btrfs_dev_replace_item,
		   cursor_left, 64);
BTRFS_SETGET_FUNCS(dev_replace_cursor_right, struct btrfs_dev_replace_item,
		   cursor_right, 64);

BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_src_devid,
			 struct btrfs_dev_replace_item, src_devid, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_cont_reading_from_srcdev_mode,
			 struct btrfs_dev_replace_item,
			 cont_reading_from_srcdev_mode, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_replace_state,
			 struct btrfs_dev_replace_item, replace_state, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_time_started,
			 struct btrfs_dev_replace_item, time_started, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_time_stopped,
			 struct btrfs_dev_replace_item, time_stopped, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_num_write_errors,
			 struct btrfs_dev_replace_item, num_write_errors, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_num_uncorrectable_read_errors,
			 struct btrfs_dev_replace_item,
			 num_uncorrectable_read_errors, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_cursor_left,
			 struct btrfs_dev_replace_item, cursor_left, 64);
BTRFS_SETGET_STACK_FUNCS(stack_dev_replace_cursor_right,
			 struct btrfs_dev_replace_item, cursor_right, 64);

/* btrfs_verity_descriptor_item */
BTRFS_SETGET_FUNCS(verity_descriptor_encryption, struct btrfs_verity_descriptor_item,
		   encryption, 8);
BTRFS_SETGET_FUNCS(verity_descriptor_size, struct btrfs_verity_descriptor_item,
		   size, 64);
BTRFS_SETGET_STACK_FUNCS(stack_verity_descriptor_encryption,
			 struct btrfs_verity_descriptor_item, encryption, 8);
BTRFS_SETGET_STACK_FUNCS(stack_verity_descriptor_size,
			 struct btrfs_verity_descriptor_item, size, 64);

/* Cast into the data area of the leaf. */
#define btrfs_item_ptr(leaf, slot, type)				\
	((type *)(btrfs_item_nr_offset(leaf, 0) + btrfs_item_offset(leaf, slot)))

#define btrfs_item_ptr_offset(leaf, slot)				\
	((unsigned long)(btrfs_item_nr_offset(leaf, 0) + btrfs_item_offset(leaf, slot)))

#endif
