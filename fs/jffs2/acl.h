/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2006  NEC Corporation
 *
 * Created by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

struct jffs2_acl_entry {
	jint16_t	e_tag;
	jint16_t	e_perm;
	jint32_t	e_id;
};

struct jffs2_acl_entry_short {
	jint16_t	e_tag;
	jint16_t	e_perm;
};

struct jffs2_acl_header {
	jint32_t	a_version;
};

#ifdef CONFIG_JFFS2_FS_POSIX_ACL

#define JFFS2_ACL_NOT_CACHED ((void *)-1)

extern struct posix_acl *jffs2_get_acl(struct inode *inode, int type);
extern int jffs2_permission(struct inode *, int, struct nameidata *);
extern int jffs2_acl_chmod(struct inode *);
extern int jffs2_init_acl(struct inode *, struct posix_acl *);
extern void jffs2_clear_acl(struct jffs2_inode_info *);

extern struct xattr_handler jffs2_acl_access_xattr_handler;
extern struct xattr_handler jffs2_acl_default_xattr_handler;

#else

#define jffs2_get_acl(inode, type)	(NULL)
#define jffs2_permission NULL
#define jffs2_acl_chmod(inode)		(0)
#define jffs2_init_acl(inode,dir)	(0)
#define jffs2_clear_acl(f)

#endif	/* CONFIG_JFFS2_FS_POSIX_ACL */
