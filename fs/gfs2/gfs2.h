/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __GFS2_DOT_H__
#define __GFS2_DOT_H__

#include <linux/gfs2_ondisk.h>

#include "lm_interface.h"
#include "lvb.h"
#include "incore.h"
#include "util.h"

enum {
	NO_CREATE = 0,
	CREATE = 1,
};

enum {
	NO_WAIT = 0,
	WAIT = 1,
};

enum {
	NO_FORCE = 0,
	FORCE = 1,
};

/*  Divide num by den.  Round up if there is a remainder.  */
#define DIV_RU(num, den) (((num) + (den) - 1) / (den))

#define GFS2_FAST_NAME_SIZE 8

#define get_v2sdp(sb) ((struct gfs2_sbd *)(sb)->s_fs_info)
#define set_v2sdp(sb, sdp) (sb)->s_fs_info = (sdp)
#define get_v2ip(inode) ((struct gfs2_inode *)(inode)->u.generic_ip)
#define set_v2ip(inode, ip) (inode)->u.generic_ip = (ip)
#define get_v2fp(file) ((struct gfs2_file *)(file)->private_data)
#define set_v2fp(file, fp) (file)->private_data = (fp)
#define get_v2bd(bh) ((struct gfs2_bufdata *)(bh)->b_private)
#define set_v2bd(bh, bd) (bh)->b_private = (bd)
#define get_v2db(bh) ((struct gfs2_databuf *)(bh)->b_private)
#define set_v2db(bh, db) (bh)->b_private = (db)

#define get_transaction ((struct gfs2_trans *)(current->journal_info))
#define set_transaction(tr) (current->journal_info) = (tr)

#define get_gl2ip(gl) ((struct gfs2_inode *)(gl)->gl_object)
#define set_gl2ip(gl, ip) (gl)->gl_object = (ip)
#define get_gl2rgd(gl) ((struct gfs2_rgrpd *)(gl)->gl_object)
#define set_gl2rgd(gl, rgd) (gl)->gl_object = (rgd)
#define get_gl2gl(gl) ((struct gfs2_glock *)(gl)->gl_object)
#define set_gl2gl(gl, gl2) (gl)->gl_object = (gl2)

#endif /* __GFS2_DOT_H__ */

