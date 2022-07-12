// SPDX-License-Identifier: GPL-2.0
/*
  File: fs/ext4/xattr.h

  On-disk format of extended attributes for the ext4 filesystem.

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/

#include <linux/xattr.h>

/* Magic value in attribute blocks */
#define EXT4_XATTR_MAGIC		0xEA020000

/* Maximum number of references to one attribute block */
#define EXT4_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define EXT4_XATTR_INDEX_USER			1
#define EXT4_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define EXT4_XATTR_INDEX_TRUSTED		4
#define	EXT4_XATTR_INDEX_LUSTRE			5
#define EXT4_XATTR_INDEX_SECURITY	        6
#define EXT4_XATTR_INDEX_SYSTEM			7
#define EXT4_XATTR_INDEX_RICHACL		8
#define EXT4_XATTR_INDEX_ENCRYPTION		9
#define EXT4_XATTR_INDEX_HURD			10 /* Reserved for Hurd */

struct ext4_xattr_header {
	__le32	h_magic;	/* magic number for identification */
	__le32	h_refcount;	/* reference count */
	__le32	h_blocks;	/* number of disk blocks used */
	__le32	h_hash;		/* hash value of all attributes */
	__le32	h_checksum;	/* crc32c(uuid+id+xattrblock) */
				/* id = inum if refcount=1, blknum otherwise */
	__u32	h_reserved[3];	/* zero right now */
};

struct ext4_xattr_ibody_header {
	__le32	h_magic;	/* magic number for identification */
};

struct ext4_xattr_entry {
	__u8	e_name_len;	/* length of name */
	__u8	e_name_index;	/* attribute name index */
	__le16	e_value_offs;	/* offset in disk block of value */
	__le32	e_value_inum;	/* inode in which the value is stored */
	__le32	e_value_size;	/* size of attribute value */
	__le32	e_hash;		/* hash value of name and value */
	char	e_name[];	/* attribute name */
};

#define EXT4_XATTR_PAD_BITS		2
#define EXT4_XATTR_PAD		(1<<EXT4_XATTR_PAD_BITS)
#define EXT4_XATTR_ROUND		(EXT4_XATTR_PAD-1)
#define EXT4_XATTR_LEN(name_len) \
	(((name_len) + EXT4_XATTR_ROUND + \
	sizeof(struct ext4_xattr_entry)) & ~EXT4_XATTR_ROUND)
#define EXT4_XATTR_NEXT(entry) \
	((struct ext4_xattr_entry *)( \
	 (char *)(entry) + EXT4_XATTR_LEN((entry)->e_name_len)))
#define EXT4_XATTR_SIZE(size) \
	(((size) + EXT4_XATTR_ROUND) & ~EXT4_XATTR_ROUND)

#define IHDR(inode, raw_inode) \
	((struct ext4_xattr_ibody_header *) \
		((void *)raw_inode + \
		EXT4_GOOD_OLD_INODE_SIZE + \
		EXT4_I(inode)->i_extra_isize))
#define IFIRST(hdr) ((struct ext4_xattr_entry *)((hdr)+1))

/*
 * XATTR_SIZE_MAX is currently 64k, but for the purposes of checking
 * for file system consistency errors, we use a somewhat bigger value.
 * This allows XATTR_SIZE_MAX to grow in the future, but by using this
 * instead of INT_MAX for certain consistency checks, we don't need to
 * worry about arithmetic overflows.  (Actually XATTR_SIZE_MAX is
 * defined in include/uapi/linux/limits.h, so changing it is going
 * not going to be trivial....)
 */
#define EXT4_XATTR_SIZE_MAX (1 << 24)

/*
 * The minimum size of EA value when you start storing it in an external inode
 * size of block - size of header - size of 1 entry - 4 null bytes
*/
#define EXT4_XATTR_MIN_LARGE_EA_SIZE(b)					\
	((b) - EXT4_XATTR_LEN(3) - sizeof(struct ext4_xattr_header) - 4)

#define BHDR(bh) ((struct ext4_xattr_header *)((bh)->b_data))
#define ENTRY(ptr) ((struct ext4_xattr_entry *)(ptr))
#define BFIRST(bh) ENTRY(BHDR(bh)+1)
#define IS_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)

#define EXT4_ZERO_XATTR_VALUE ((void *)-1)

/*
 * If we want to add an xattr to the inode, we should make sure that
 * i_extra_isize is not 0 and that the inode size is not less than
 * EXT4_GOOD_OLD_INODE_SIZE + extra_isize + pad.
 *   EXT4_GOOD_OLD_INODE_SIZE   extra_isize header   entry   pad  data
 * |--------------------------|------------|------|---------|---|-------|
 */
#define EXT4_INODE_HAS_XATTR_SPACE(inode)				\
	((EXT4_I(inode)->i_extra_isize != 0) &&				\
	 (EXT4_GOOD_OLD_INODE_SIZE + EXT4_I(inode)->i_extra_isize +	\
	  sizeof(struct ext4_xattr_ibody_header) + EXT4_XATTR_PAD <=	\
	  EXT4_INODE_SIZE((inode)->i_sb)))

struct ext4_xattr_info {
	const char *name;
	const void *value;
	size_t value_len;
	int name_index;
	int in_inode;
};

struct ext4_xattr_search {
	struct ext4_xattr_entry *first;
	void *base;
	void *end;
	struct ext4_xattr_entry *here;
	int not_found;
};

struct ext4_xattr_ibody_find {
	struct ext4_xattr_search s;
	struct ext4_iloc iloc;
};

struct ext4_xattr_inode_array {
	unsigned int count;		/* # of used items in the array */
	struct inode *inodes[];
};

extern const struct xattr_handler ext4_xattr_user_handler;
extern const struct xattr_handler ext4_xattr_trusted_handler;
extern const struct xattr_handler ext4_xattr_security_handler;
extern const struct xattr_handler ext4_xattr_hurd_handler;

#define EXT4_XATTR_NAME_ENCRYPTION_CONTEXT "c"

/*
 * The EXT4_STATE_NO_EXPAND is overloaded and used for two purposes.
 * The first is to signal that there the inline xattrs and data are
 * taking up so much space that we might as well not keep trying to
 * expand it.  The second is that xattr_sem is taken for writing, so
 * we shouldn't try to recurse into the inode expansion.  For this
 * second case, we need to make sure that we take save and restore the
 * NO_EXPAND state flag appropriately.
 */
static inline void ext4_write_lock_xattr(struct inode *inode, int *save)
{
	down_write(&EXT4_I(inode)->xattr_sem);
	*save = ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND);
	ext4_set_inode_state(inode, EXT4_STATE_NO_EXPAND);
}

static inline int ext4_write_trylock_xattr(struct inode *inode, int *save)
{
	if (down_write_trylock(&EXT4_I(inode)->xattr_sem) == 0)
		return 0;
	*save = ext4_test_inode_state(inode, EXT4_STATE_NO_EXPAND);
	ext4_set_inode_state(inode, EXT4_STATE_NO_EXPAND);
	return 1;
}

static inline void ext4_write_unlock_xattr(struct inode *inode, int *save)
{
	if (*save == 0)
		ext4_clear_inode_state(inode, EXT4_STATE_NO_EXPAND);
	up_write(&EXT4_I(inode)->xattr_sem);
}

extern ssize_t ext4_listxattr(struct dentry *, char *, size_t);

extern int ext4_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int ext4_xattr_set(struct inode *, int, const char *, const void *, size_t, int);
extern int ext4_xattr_set_handle(handle_t *, struct inode *, int, const char *, const void *, size_t, int);
extern int ext4_xattr_set_credits(struct inode *inode, size_t value_len,
				  bool is_create, int *credits);
extern int __ext4_xattr_set_credits(struct super_block *sb, struct inode *inode,
				struct buffer_head *block_bh, size_t value_len,
				bool is_create);

extern int ext4_xattr_delete_inode(handle_t *handle, struct inode *inode,
				   struct ext4_xattr_inode_array **array,
				   int extra_credits);
extern void ext4_xattr_inode_array_free(struct ext4_xattr_inode_array *array);

extern int ext4_expand_extra_isize_ea(struct inode *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, handle_t *handle);
extern void ext4_evict_ea_inode(struct inode *inode);

extern const struct xattr_handler *ext4_xattr_handlers[];

extern int ext4_xattr_ibody_find(struct inode *inode, struct ext4_xattr_info *i,
				 struct ext4_xattr_ibody_find *is);
extern int ext4_xattr_ibody_get(struct inode *inode, int name_index,
				const char *name,
				void *buffer, size_t buffer_size);
extern int ext4_xattr_ibody_inline_set(handle_t *handle, struct inode *inode,
				       struct ext4_xattr_info *i,
				       struct ext4_xattr_ibody_find *is);

extern struct mb_cache *ext4_xattr_create_cache(void);
extern void ext4_xattr_destroy_cache(struct mb_cache *);

#ifdef CONFIG_EXT4_FS_SECURITY
extern int ext4_init_security(handle_t *handle, struct inode *inode,
			      struct inode *dir, const struct qstr *qstr);
#else
static inline int ext4_init_security(handle_t *handle, struct inode *inode,
				     struct inode *dir, const struct qstr *qstr)
{
	return 0;
}
#endif

#ifdef CONFIG_LOCKDEP
extern void ext4_xattr_inode_set_class(struct inode *ea_inode);
#else
static inline void ext4_xattr_inode_set_class(struct inode *ea_inode) { }
#endif

extern int ext4_get_inode_usage(struct inode *inode, qsize_t *usage);
