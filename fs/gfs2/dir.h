/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __DIR_DOT_H__
#define __DIR_DOT_H__

/**
 * gfs2_filldir_t - Report a directory entry to the caller of gfs2_dir_read()
 * @opaque: opaque data used by the function
 * @name: the name of the directory entry
 * @length: the length of the name
 * @offset: the entry's offset in the directory
 * @inum: the inode number the entry points to
 * @type: the type of inode the entry points to
 *
 * Returns: 0 on success, 1 if buffer full
 */

typedef int (*gfs2_filldir_t) (void *opaque,
			      const char *name, unsigned int length,
			      uint64_t offset,
			      struct gfs2_inum *inum, unsigned int type);

int gfs2_filecmp(struct qstr *file1, char *file2, int len_of_file2);
int gfs2_dirent_alloc(struct gfs2_inode *dip, struct buffer_head *bh,
		     int name_len, struct gfs2_dirent **dent_out);

int gfs2_dir_search(struct gfs2_inode *dip, struct qstr *filename,
		   struct gfs2_inum *inum, unsigned int *type);
int gfs2_dir_add(struct gfs2_inode *dip, struct qstr *filename,
		struct gfs2_inum *inum, unsigned int type);
int gfs2_dir_del(struct gfs2_inode *dip, struct qstr *filename);
int gfs2_dir_read(struct gfs2_inode *dip, uint64_t * offset, void *opaque,
		 gfs2_filldir_t filldir);
int gfs2_dir_mvino(struct gfs2_inode *dip, struct qstr *filename,
		  struct gfs2_inum *new_inum, unsigned int new_type);

int gfs2_dir_exhash_dealloc(struct gfs2_inode *dip);

int gfs2_diradd_alloc_required(struct gfs2_inode *dip, struct qstr *filename,
			      int *alloc_required);

int gfs2_get_dir_meta(struct gfs2_inode *ip, struct gfs2_user_buffer *ub);

#endif /* __DIR_DOT_H__ */
