/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __FILE_DOT_H__
#define __FILE_DOT_H__

int gfs2_jdata_get_buffer(struct gfs2_inode *ip, uint64_t block, int new,
			  struct buffer_head **bhp);

typedef int (*read_copy_fn_t) (struct buffer_head *bh, char **buf,
			       unsigned int offset, unsigned int size);
typedef int (*write_copy_fn_t) (struct gfs2_inode *ip,
				struct buffer_head *bh, const char **buf,
				unsigned int offset, unsigned int size);

int gfs2_copy2mem(struct buffer_head *bh, char **buf,
		  unsigned int offset, unsigned int size);
int gfs2_copy2user(struct buffer_head *bh, char __user **buf,
		   unsigned int offset, unsigned int size);
int gfs2_jdata_read(struct gfs2_inode *ip, char __user *buf,
		    uint64_t offset, unsigned int size,
		    read_copy_fn_t copy_fn);

int gfs2_copy_from_mem(struct gfs2_inode *ip,
		       struct buffer_head *bh, const char **buf,
		       unsigned int offset, unsigned int size);
int gfs2_copy_from_user(struct gfs2_inode *ip,
			struct buffer_head *bh, const char __user **buf,
			unsigned int offset, unsigned int size);
int gfs2_jdata_write(struct gfs2_inode *ip, const char __user *buf,
		     uint64_t offset, unsigned int size,
		     write_copy_fn_t copy_fn);

static inline int gfs2_jdata_read_mem(struct gfs2_inode *ip, char *buf,
				      uint64_t offset, unsigned int size)
{
	return gfs2_jdata_read(ip, (__force char __user *)buf, offset, size, gfs2_copy2mem);
}

static inline int gfs2_jdata_write_mem(struct gfs2_inode *ip, const char *buf,
				       uint64_t offset, unsigned int size)
{
	return gfs2_jdata_write(ip, (__force const char __user *)buf, offset, size, gfs2_copy_from_mem);
}

#endif /* __FILE_DOT_H__ */
