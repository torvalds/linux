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
	__le32	e_value_block;	/* disk block attribute is stored on (n/i) */
	__le32	e_value_size;	/* size of attribute value */
	__le32	e_hash;		/* hash value of name and value */
	char	e_name[0];	/* attribute name */
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

#define BHDR(bh) ((struct ext4_xattr_header *)((bh)->b_data))
#define ENTRY(ptr) ((struct ext4_xattr_entry *)(ptr))
#define BFIRST(bh) ENTRY(BHDR(bh)+1)
#define IS_LAST_ENTRY(entry) (*(__u32 *)(entry) == 0)


struct ext4_xattr_info {
	int name_index;
	const char *name;
	const void *value;
	size_t value_len;
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

# ifdef CONFIG_EXT4_FS_XATTR

extern const struct xattr_handler ext4_xattr_user_handler;
extern const struct xattr_handler ext4_xattr_trusted_handler;
extern const struct xattr_handler ext4_xattr_acl_access_handler;
extern const struct xattr_handler ext4_xattr_acl_default_handler;
extern const struct xattr_handler ext4_xattr_security_handler;

extern ssize_t ext4_listxattr(struct dentry *, char *, size_t);

extern int ext4_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int ext4_xattr_set(struct inode *, int, const char *, const void *, size_t, int);
extern int ext4_xattr_set_handle(handle_t *, struct inode *, int, const char *, const void *, size_t, int);

extern void ext4_xattr_delete_inode(handle_t *, struct inode *);
extern void ext4_xattr_put_super(struct super_block *);

extern int ext4_expand_extra_isize_ea(struct inode *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, handle_t *handle);

extern int __init ext4_init_xattr(void);
extern void ext4_exit_xattr(void);

extern const struct xattr_handler *ext4_xattr_handlers[];

extern int ext4_xattr_ibody_find(struct inode *inode, struct ext4_xattr_info *i,
				 struct ext4_xattr_ibody_find *is);
extern int ext4_xattr_ibody_get(struct inode *inode, int name_index,
				const char *name,
				void *buffer, size_t buffer_size);
extern int ext4_xattr_ibody_set(handle_t *handle, struct inode *inode,
				struct ext4_xattr_info *i,
				struct ext4_xattr_ibody_find *is);

extern int ext4_has_inline_data(struct inode *inode);
extern int ext4_get_inline_size(struct inode *inode);
extern int ext4_get_max_inline_size(struct inode *inode);
extern int ext4_find_inline_data_nolock(struct inode *inode);
extern void ext4_write_inline_data(struct inode *inode,
				   struct ext4_iloc *iloc,
				   void *buffer, loff_t pos,
				   unsigned int len);
extern int ext4_prepare_inline_data(handle_t *handle, struct inode *inode,
				    unsigned int len);
extern int ext4_init_inline_data(handle_t *handle, struct inode *inode,
				 unsigned int len);
extern int ext4_destroy_inline_data(handle_t *handle, struct inode *inode);

extern int ext4_readpage_inline(struct inode *inode, struct page *page);
extern int ext4_try_to_write_inline_data(struct address_space *mapping,
					 struct inode *inode,
					 loff_t pos, unsigned len,
					 unsigned flags,
					 struct page **pagep);
extern int ext4_write_inline_data_end(struct inode *inode,
				      loff_t pos, unsigned len,
				      unsigned copied,
				      struct page *page);
extern struct buffer_head *
ext4_journalled_write_inline_data(struct inode *inode,
				  unsigned len,
				  struct page *page);
extern int ext4_da_write_inline_data_begin(struct address_space *mapping,
					   struct inode *inode,
					   loff_t pos, unsigned len,
					   unsigned flags,
					   struct page **pagep,
					   void **fsdata);
extern int ext4_da_write_inline_data_end(struct inode *inode, loff_t pos,
					 unsigned len, unsigned copied,
					 struct page *page);
# else  /* CONFIG_EXT4_FS_XATTR */

static inline int
ext4_xattr_get(struct inode *inode, int name_index, const char *name,
	       void *buffer, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline int
ext4_xattr_set(struct inode *inode, int name_index, const char *name,
	       const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline int
ext4_xattr_set_handle(handle_t *handle, struct inode *inode, int name_index,
	       const char *name, const void *value, size_t size, int flags)
{
	return -EOPNOTSUPP;
}

static inline void
ext4_xattr_delete_inode(handle_t *handle, struct inode *inode)
{
}

static inline void
ext4_xattr_put_super(struct super_block *sb)
{
}

static __init inline int
ext4_init_xattr(void)
{
	return 0;
}

static inline void
ext4_exit_xattr(void)
{
}

static inline int
ext4_expand_extra_isize_ea(struct inode *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, handle_t *handle)
{
	return -EOPNOTSUPP;
}

#define ext4_xattr_handlers	NULL

static inline int
ext4_xattr_ibody_find(struct inode *inode, struct ext4_xattr_info *i,
		      struct ext4_xattr_ibody_find *is)
{
	return -EOPNOTSUPP;
}

static inline int
ext4_xattr_ibody_set(handle_t *handle, struct inode *inode,
		     struct ext4_xattr_info *i,
		     struct ext4_xattr_ibody_find *is)
{
	return -EOPNOTSUPP;
}

static inline int
ext4_xattr_ibody_get(struct inode *inode, int name_index,
		     const char *name,
		     void *buffer, size_t buffer_size)
{
	return -EOPNOTSUPP;
}

static inline int ext4_find_inline_data_nolock(struct inode *inode)
{
	return 0;
}

static inline int ext4_has_inline_data(struct inode *inode)
{
	return 0;
}

static inline int ext4_get_inline_size(struct inode *inode)
{
	return 0;
}

static inline int ext4_get_max_inline_size(struct inode *inode)
{
	return 0;
}

static inline void ext4_write_inline_data(struct inode *inode,
					  struct ext4_iloc *iloc,
					  void *buffer, loff_t pos,
					  unsigned int len)
{
	return;
}

static inline int ext4_init_inline_data(handle_t *handle,
					struct inode *inode,
					unsigned int len)
{
	return 0;
}

static inline int ext4_destroy_inline_data(handle_t *handle,
					   struct inode *inode)
{
	return 0;
}

static inline int ext4_readpage_inline(struct inode *inode, struct page *page)
{
	return 0;
}

static inline int ext4_try_to_write_inline_data(struct address_space *mapping,
						struct inode *inode,
						loff_t pos, unsigned len,
						unsigned flags,
						struct page **pagep)
{
	return 0;
}

static inline int ext4_write_inline_data_end(struct inode *inode,
					     loff_t pos, unsigned len,
					     unsigned copied,
					     struct page *page)
{
	return 0;
}

static inline struct buffer_head *
ext4_journalled_write_inline_data(struct inode *inode,
				  unsigned len,
				  struct page *page)
{
	return NULL;
}

static inline int
ext4_da_write_inline_data_begin(struct address_space *mapping,
				struct inode *inode,
				loff_t pos, unsigned len,
				unsigned flags,
				struct page **pagep,
				void **fsdata)
{
	return 0;
}

static inline int ext4_da_write_inline_data_end(struct inode *inode, loff_t pos,
						unsigned len, unsigned copied,
						struct page *page)
{
	return 0;
}
# endif  /* CONFIG_EXT4_FS_XATTR */

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
