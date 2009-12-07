/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#ifndef __EATTR_DOT_H__
#define __EATTR_DOT_H__

struct gfs2_inode;
struct iattr;

#define GFS2_EA_REC_LEN(ea) be32_to_cpu((ea)->ea_rec_len)
#define GFS2_EA_DATA_LEN(ea) be32_to_cpu((ea)->ea_data_len)

#define GFS2_EA_SIZE(ea) \
ALIGN(sizeof(struct gfs2_ea_header) + (ea)->ea_name_len + \
      ((GFS2_EA_IS_STUFFED(ea)) ? GFS2_EA_DATA_LEN(ea) : \
				  (sizeof(__be64) * (ea)->ea_num_ptrs)), 8)

#define GFS2_EA_IS_STUFFED(ea) (!(ea)->ea_num_ptrs)
#define GFS2_EA_IS_LAST(ea) ((ea)->ea_flags & GFS2_EAFLAG_LAST)

#define GFS2_EAREQ_SIZE_STUFFED(er) \
ALIGN(sizeof(struct gfs2_ea_header) + (er)->er_name_len + (er)->er_data_len, 8)

#define GFS2_EA2NAME(ea) ((char *)((struct gfs2_ea_header *)(ea) + 1))
#define GFS2_EA2DATA(ea) (GFS2_EA2NAME(ea) + (ea)->ea_name_len)

#define GFS2_EA2DATAPTRS(ea) \
((__be64 *)(GFS2_EA2NAME(ea) + ALIGN((ea)->ea_name_len, 8)))

#define GFS2_EA2NEXT(ea) \
((struct gfs2_ea_header *)((char *)(ea) + GFS2_EA_REC_LEN(ea)))

#define GFS2_EA_BH2FIRST(bh) \
((struct gfs2_ea_header *)((bh)->b_data + sizeof(struct gfs2_meta_header)))

struct gfs2_ea_request {
	const char *er_name;
	char *er_data;
	unsigned int er_name_len;
	unsigned int er_data_len;
	unsigned int er_type; /* GFS2_EATYPE_... */
};

struct gfs2_ea_location {
	struct buffer_head *el_bh;
	struct gfs2_ea_header *el_ea;
	struct gfs2_ea_header *el_prev;
};

extern int gfs2_xattr_get(struct inode *inode, int type, const char *name,
			  void *buffer, size_t size);
extern int gfs2_xattr_set(struct inode *inode, int type, const char *name,
			  const void *value, size_t size, int flags);
extern ssize_t gfs2_listxattr(struct dentry *dentry, char *buffer, size_t size);
extern int gfs2_ea_dealloc(struct gfs2_inode *ip);

/* Exported to acl.c */

extern int gfs2_xattr_acl_get(struct gfs2_inode *ip, const char *name, char **data);
extern int gfs2_xattr_acl_chmod(struct gfs2_inode *ip, struct iattr *attr, char *data);

#endif /* __EATTR_DOT_H__ */
