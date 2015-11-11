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

#define EXT4_ZERO_XATTR_VALUE ((void *)-1)

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

extern const struct xattr_handler ext4_xattr_user_handler;
extern const struct xattr_handler ext4_xattr_trusted_handler;
extern const struct xattr_handler ext4_xattr_security_handler;

#define EXT4_XATTR_NAME_ENCRYPTION_CONTEXT "c"

extern ssize_t ext4_listxattr(struct dentry *, char *, size_t);

extern int ext4_xattr_get(struct inode *, int, const char *, void *, size_t);
extern int ext4_xattr_set(struct inode *, int, const char *, const void *, size_t, int);
extern int ext4_xattr_set_handle(handle_t *, struct inode *, int, const char *, const void *, size_t, int);

extern void ext4_xattr_delete_inode(handle_t *, struct inode *);
extern void ext4_xattr_put_super(struct super_block *);

extern int ext4_expand_extra_isize_ea(struct inode *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, handle_t *handle);

extern const struct xattr_handler *ext4_xattr_handlers[];

extern int ext4_xattr_ibody_find(struct inode *inode, struct ext4_xattr_info *i,
				 struct ext4_xattr_ibody_find *is);
extern int ext4_xattr_ibody_get(struct inode *inode, int name_index,
				const char *name,
				void *buffer, size_t buffer_size);
extern int ext4_xattr_ibody_inline_set(handle_t *handle, struct inode *inode,
				       struct ext4_xattr_info *i,
				       struct ext4_xattr_ibody_find *is);

extern struct mb_cache *ext4_xattr_create_cache(char *name);
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
