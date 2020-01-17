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
	struct jffs2_acl_entry	a_entries[];
};

#ifdef CONFIG_JFFS2_FS_POSIX_ACL

struct posix_acl *jffs2_get_acl(struct iyesde *iyesde, int type);
int jffs2_set_acl(struct iyesde *iyesde, struct posix_acl *acl, int type);
extern int jffs2_init_acl_pre(struct iyesde *, struct iyesde *, umode_t *);
extern int jffs2_init_acl_post(struct iyesde *);

#else

#define jffs2_get_acl				(NULL)
#define jffs2_set_acl				(NULL)
#define jffs2_init_acl_pre(dir_i,iyesde,mode)	(0)
#define jffs2_init_acl_post(iyesde)		(0)

#endif	/* CONFIG_JFFS2_FS_POSIX_ACL */
