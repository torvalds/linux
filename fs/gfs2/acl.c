/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "eaops.h"
#include "eattr.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "trans.h"
#include "util.h"

#define ACL_ACCESS 1
#define ACL_DEFAULT 0

int gfs2_acl_validate_set(struct gfs2_inode *ip, int access,
		      struct gfs2_ea_request *er,
		      int *remove, mode_t *mode)
{
	struct posix_acl *acl;
	int error;

	error = gfs2_acl_validate_remove(ip, access);
	if (error)
		return error;

	if (!er->er_data)
		return -EINVAL;

	acl = posix_acl_from_xattr(er->er_data, er->er_data_len);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (!acl) {
		*remove = 1;
		return 0;
	}

	error = posix_acl_valid(acl);
	if (error)
		goto out;

	if (access) {
		error = posix_acl_equiv_mode(acl, mode);
		if (!error)
			*remove = 1;
		else if (error > 0)
			error = 0;
	}

out:
	posix_acl_release(acl);
	return error;
}

int gfs2_acl_validate_remove(struct gfs2_inode *ip, int access)
{
	if (!GFS2_SB(&ip->i_inode)->sd_args.ar_posix_acl)
		return -EOPNOTSUPP;
	if (current->fsuid != ip->i_di.di_uid && !capable(CAP_FOWNER))
		return -EPERM;
	if (S_ISLNK(ip->i_di.di_mode))
		return -EOPNOTSUPP;
	if (!access && !S_ISDIR(ip->i_di.di_mode))
		return -EACCES;

	return 0;
}

static int acl_get(struct gfs2_inode *ip, int access, struct posix_acl **acl,
		   struct gfs2_ea_location *el, char **data, unsigned int *len)
{
	struct gfs2_ea_request er;
	struct gfs2_ea_location el_this;
	int error;

	if (!ip->i_di.di_eattr)
		return 0;

	memset(&er, 0, sizeof(struct gfs2_ea_request));
	if (access) {
		er.er_name = GFS2_POSIX_ACL_ACCESS;
		er.er_name_len = GFS2_POSIX_ACL_ACCESS_LEN;
	} else {
		er.er_name = GFS2_POSIX_ACL_DEFAULT;
		er.er_name_len = GFS2_POSIX_ACL_DEFAULT_LEN;
	}
	er.er_type = GFS2_EATYPE_SYS;

	if (!el)
		el = &el_this;

	error = gfs2_ea_find(ip, &er, el);
	if (error)
		return error;
	if (!el->el_ea)
		return 0;
	if (!GFS2_EA_DATA_LEN(el->el_ea))
		goto out;

	er.er_data_len = GFS2_EA_DATA_LEN(el->el_ea);
	er.er_data = kmalloc(er.er_data_len, GFP_KERNEL);
	error = -ENOMEM;
	if (!er.er_data)
		goto out;

	error = gfs2_ea_get_copy(ip, el, er.er_data);
	if (error)
		goto out_kfree;

	if (acl) {
		*acl = posix_acl_from_xattr(er.er_data, er.er_data_len);
		if (IS_ERR(*acl))
			error = PTR_ERR(*acl);
	}

out_kfree:
	if (error || !data)
		kfree(er.er_data);
	else {
		*data = er.er_data;
		*len = er.er_data_len;
	}
out:
	if (error || el == &el_this)
		brelse(el->el_bh);
	return error;
}

/**
 * gfs2_check_acl_locked - Check an ACL to see if we're allowed to do something
 * @inode: the file we want to do something to
 * @mask: what we want to do
 *
 * Returns: errno
 */

int gfs2_check_acl_locked(struct inode *inode, int mask)
{
	struct posix_acl *acl = NULL;
	int error;

	error = acl_get(GFS2_I(inode), ACL_ACCESS, &acl, NULL, NULL, NULL);
	if (error)
		return error;

	if (acl) {
		error = posix_acl_permission(inode, acl, mask);
		posix_acl_release(acl);
		return error;
	}

	return -EAGAIN;
}

int gfs2_check_acl(struct inode *inode, int mask)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder i_gh;
	int error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (!error) {
		error = gfs2_check_acl_locked(inode, mask);
		gfs2_glock_dq_uninit(&i_gh);
	}

	return error;
}

static int munge_mode(struct gfs2_inode *ip, mode_t mode)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *dibh;
	int error;

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		gfs2_assert_withdraw(sdp,
				(ip->i_di.di_mode & S_IFMT) == (mode & S_IFMT));
		ip->i_di.di_mode = mode;
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
		brelse(dibh);
	}

	gfs2_trans_end(sdp);

	return 0;
}

int gfs2_acl_create(struct gfs2_inode *dip, struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct posix_acl *acl = NULL, *clone;
	struct gfs2_ea_request er;
	mode_t mode = ip->i_di.di_mode;
	int error;

	if (!sdp->sd_args.ar_posix_acl)
		return 0;
	if (S_ISLNK(ip->i_di.di_mode))
		return 0;

	memset(&er, 0, sizeof(struct gfs2_ea_request));
	er.er_type = GFS2_EATYPE_SYS;

	error = acl_get(dip, ACL_DEFAULT, &acl, NULL,
			&er.er_data, &er.er_data_len);
	if (error)
		return error;
	if (!acl) {
		mode &= ~current->fs->umask;
		if (mode != ip->i_di.di_mode)
			error = munge_mode(ip, mode);
		return error;
	}

	clone = posix_acl_clone(acl, GFP_KERNEL);
	error = -ENOMEM;
	if (!clone)
		goto out;
	posix_acl_release(acl);
	acl = clone;

	if (S_ISDIR(ip->i_di.di_mode)) {
		er.er_name = GFS2_POSIX_ACL_DEFAULT;
		er.er_name_len = GFS2_POSIX_ACL_DEFAULT_LEN;
		error = gfs2_system_eaops.eo_set(ip, &er);
		if (error)
			goto out;
	}

	error = posix_acl_create_masq(acl, &mode);
	if (error < 0)
		goto out;
	if (error > 0) {
		er.er_name = GFS2_POSIX_ACL_ACCESS;
		er.er_name_len = GFS2_POSIX_ACL_ACCESS_LEN;
		posix_acl_to_xattr(acl, er.er_data, er.er_data_len);
		er.er_mode = mode;
		er.er_flags = GFS2_ERF_MODE;
		error = gfs2_system_eaops.eo_set(ip, &er);
		if (error)
			goto out;
	} else
		munge_mode(ip, mode);

out:
	posix_acl_release(acl);
	kfree(er.er_data);
	return error;
}

int gfs2_acl_chmod(struct gfs2_inode *ip, struct iattr *attr)
{
	struct posix_acl *acl = NULL, *clone;
	struct gfs2_ea_location el;
	char *data;
	unsigned int len;
	int error;

	error = acl_get(ip, ACL_ACCESS, &acl, &el, &data, &len);
	if (error)
		return error;
	if (!acl)
		return gfs2_setattr_simple(ip, attr);

	clone = posix_acl_clone(acl, GFP_KERNEL);
	error = -ENOMEM;
	if (!clone)
		goto out;
	posix_acl_release(acl);
	acl = clone;

	error = posix_acl_chmod_masq(acl, attr->ia_mode);
	if (!error) {
		posix_acl_to_xattr(acl, data, len);
		error = gfs2_ea_acl_chmod(ip, &el, attr, data);
	}

out:
	posix_acl_release(acl);
	brelse(el.el_bh);
	kfree(data);
	return error;
}

