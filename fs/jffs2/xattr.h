/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2006  NEC Corporation
 *
 * Created by KaiGai Kohei <kaigai@ak.jp.nec.com>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */
#ifndef _JFFS2_FS_XATTR_H_
#define _JFFS2_FS_XATTR_H_

#include <linux/xattr.h>
#include <linux/list.h>

#define JFFS2_XFLAGS_HOT	(0x01)	/* This datum is HOT */
#define JFFS2_XFLAGS_BIND	(0x02)	/* This datum is not reclaimed */

struct jffs2_xattr_datum
{
	void *always_null;
	struct jffs2_raw_node_ref *node;
	uint8_t class;
	uint8_t flags;
	uint16_t xprefix;			/* see JFFS2_XATTR_PREFIX_* */

	struct list_head xindex;	/* chained from c->xattrindex[n] */
	uint32_t refcnt;		/* # of xattr_ref refers this */
	uint32_t xid;
	uint32_t version;

	uint32_t data_crc;
	uint32_t hashkey;
	char *xname;		/* XATTR name without prefix */
	uint32_t name_len;	/* length of xname */
	char *xvalue;		/* XATTR value */
	uint32_t value_len;	/* length of xvalue */
};

struct jffs2_inode_cache;
struct jffs2_xattr_ref
{
	void *always_null;
	struct jffs2_raw_node_ref *node;
	uint8_t class;
	uint8_t flags;		/* Currently unused */
	u16 unused;

	union {
		struct jffs2_inode_cache *ic;	/* reference to jffs2_inode_cache */
		uint32_t ino;			/* only used in scanning/building  */
	};
	union {
		struct jffs2_xattr_datum *xd;	/* reference to jffs2_xattr_datum */
		uint32_t xid;			/* only used in sccanning/building */
	};
	struct jffs2_xattr_ref *next;		/* chained from ic->xref_list */
};

#ifdef CONFIG_JFFS2_FS_XATTR

extern void jffs2_init_xattr_subsystem(struct jffs2_sb_info *c);
extern void jffs2_build_xattr_subsystem(struct jffs2_sb_info *c);
extern void jffs2_clear_xattr_subsystem(struct jffs2_sb_info *c);

extern struct jffs2_xattr_datum *jffs2_setup_xattr_datum(struct jffs2_sb_info *c,
                                                  uint32_t xid, uint32_t version);

extern void jffs2_xattr_delete_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic);
extern void jffs2_xattr_free_inode(struct jffs2_sb_info *c, struct jffs2_inode_cache *ic);

extern int jffs2_garbage_collect_xattr_datum(struct jffs2_sb_info *c, struct jffs2_xattr_datum *xd);
extern int jffs2_garbage_collect_xattr_ref(struct jffs2_sb_info *c, struct jffs2_xattr_ref *ref);
extern int jffs2_verify_xattr(struct jffs2_sb_info *c);

extern int do_jffs2_getxattr(struct inode *inode, int xprefix, const char *xname,
			     char *buffer, size_t size);
extern int do_jffs2_setxattr(struct inode *inode, int xprefix, const char *xname,
			     const char *buffer, size_t size, int flags);

extern struct xattr_handler *jffs2_xattr_handlers[];
extern struct xattr_handler jffs2_user_xattr_handler;
extern struct xattr_handler jffs2_trusted_xattr_handler;

extern ssize_t jffs2_listxattr(struct dentry *, char *, size_t);
#define jffs2_getxattr		generic_getxattr
#define jffs2_setxattr		generic_setxattr
#define jffs2_removexattr	generic_removexattr

#else

#define jffs2_init_xattr_subsystem(c)
#define jffs2_build_xattr_subsystem(c)
#define jffs2_clear_xattr_subsystem(c)

#define jffs2_xattr_delete_inode(c, ic)
#define jffs2_xattr_free_inode(c, ic)
#define jffs2_verify_xattr(c)			(1)

#define jffs2_xattr_handlers	NULL
#define jffs2_listxattr		NULL
#define jffs2_getxattr		NULL
#define jffs2_setxattr		NULL
#define jffs2_removexattr	NULL

#endif /* CONFIG_JFFS2_FS_XATTR */

#ifdef CONFIG_JFFS2_FS_SECURITY
extern int jffs2_init_security(struct inode *inode, struct inode *dir);
extern struct xattr_handler jffs2_security_xattr_handler;
#else
#define jffs2_init_security(inode,dir)	(0)
#endif /* CONFIG_JFFS2_FS_SECURITY */

#endif /* _JFFS2_FS_XATTR_H_ */
