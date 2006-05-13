/*-------------------------------------------------------------------------*
 *  File: fs/jffs2/acl.h
 *  POSIX ACL support on JFFS2 FileSystem
 *
 *  Implemented by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *  Copyright (C) 2006 NEC Corporation
 *
 *  For licensing information, see the file 'LICENCE' in the jffs2 directory.
 *-------------------------------------------------------------------------*/
typedef struct {
	jint16_t	e_tag;
	jint16_t	e_perm;
	jint32_t	e_id;
} jffs2_acl_entry;

typedef struct {
	jint16_t	e_tag;
	jint16_t	e_perm;
} jffs2_acl_entry_short;

typedef struct {
	jint32_t	a_version;
} jffs2_acl_header;

#ifdef __KERNEL__
#ifdef CONFIG_JFFS2_FS_POSIX_ACL

#define JFFS2_ACL_NOT_CACHED ((void *)-1)

extern int jffs2_permission(struct inode *, int, struct nameidata *);
extern int jffs2_acl_chmod(struct inode *);
extern int jffs2_init_acl(struct inode *, struct inode *);
extern void jffs2_clear_acl(struct inode *);

extern struct xattr_handler jffs2_acl_access_xattr_handler;
extern struct xattr_handler jffs2_acl_default_xattr_handler;

#else

#define jffs2_permission NULL
#define jffs2_acl_chmod(inode)		(0)
#define jffs2_init_acl(inode,dir)	(0)
#define jffs2_clear_acl(inode)

#endif	/* CONFIG_JFFS2_FS_POSIX_ACL */
#endif	/* __KERNEL__ */
