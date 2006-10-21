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
                                  (sizeof(u64) * (ea)->ea_num_ptrs)), 8)

#define GFS2_EA_IS_STUFFED(ea) (!(ea)->ea_num_ptrs)
#define GFS2_EA_IS_LAST(ea) ((ea)->ea_flags & GFS2_EAFLAG_LAST)

#define GFS2_EAREQ_SIZE_STUFFED(er) \
ALIGN(sizeof(struct gfs2_ea_header) + (er)->er_name_len + (er)->er_data_len, 8)

#define GFS2_EAREQ_SIZE_UNSTUFFED(sdp, er) \
ALIGN(sizeof(struct gfs2_ea_header) + (er)->er_name_len + \
      sizeof(u64) * DIV_ROUND_UP((er)->er_data_len, (sdp)->sd_jbsize), 8)

#define GFS2_EA2NAME(ea) ((char *)((struct gfs2_ea_header *)(ea) + 1))
#define GFS2_EA2DATA(ea) (GFS2_EA2NAME(ea) + (ea)->ea_name_len)

#define GFS2_EA2DATAPTRS(ea) \
((u64 *)(GFS2_EA2NAME(ea) + ALIGN((ea)->ea_name_len, 8)))

#define GFS2_EA2NEXT(ea) \
((struct gfs2_ea_header *)((char *)(ea) + GFS2_EA_REC_LEN(ea)))

#define GFS2_EA_BH2FIRST(bh) \
((struct gfs2_ea_header *)((bh)->b_data + sizeof(struct gfs2_meta_header)))

#define GFS2_ERF_MODE 0x80000000

struct gfs2_ea_request {
	const char *er_name;
	char *er_data;
	unsigned int er_name_len;
	unsigned int er_data_len;
	unsigned int er_type; /* GFS2_EATYPE_... */
	int er_flags;
	mode_t er_mode;
};

struct gfs2_ea_location {
	struct buffer_head *el_bh;
	struct gfs2_ea_header *el_ea;
	struct gfs2_ea_header *el_prev;
};

int gfs2_ea_get_i(struct gfs2_inode *ip, struct gfs2_ea_request *er);
int gfs2_ea_set_i(struct gfs2_inode *ip, struct gfs2_ea_request *er);
int gfs2_ea_remove_i(struct gfs2_inode *ip, struct gfs2_ea_request *er);

int gfs2_ea_list(struct gfs2_inode *ip, struct gfs2_ea_request *er);
int gfs2_ea_get(struct gfs2_inode *ip, struct gfs2_ea_request *er);
int gfs2_ea_set(struct gfs2_inode *ip, struct gfs2_ea_request *er);
int gfs2_ea_remove(struct gfs2_inode *ip, struct gfs2_ea_request *er);

int gfs2_ea_dealloc(struct gfs2_inode *ip);

/* Exported to acl.c */

int gfs2_ea_find(struct gfs2_inode *ip,
		 struct gfs2_ea_request *er,
		 struct gfs2_ea_location *el);
int gfs2_ea_get_copy(struct gfs2_inode *ip,
		     struct gfs2_ea_location *el,
		     char *data);
int gfs2_ea_acl_chmod(struct gfs2_inode *ip, struct gfs2_ea_location *el,
		      struct iattr *attr, char *data);

static inline unsigned int gfs2_ea_strlen(struct gfs2_ea_header *ea)
{
	switch (ea->ea_type) {
	case GFS2_EATYPE_USR:
		return 5 + ea->ea_name_len + 1;
	case GFS2_EATYPE_SYS:
		return 7 + ea->ea_name_len + 1;
	case GFS2_EATYPE_SECURITY:
		return 9 + ea->ea_name_len + 1;
	default:
		return 0;
	}
}

#endif /* __EATTR_DOT_H__ */
