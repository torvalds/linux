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
#include <linux/xattr.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "eaops.h"
#include "eattr.h"
#include "util.h"

/**
 * gfs2_ea_name2type - get the type of the ea, and truncate type from the name
 * @namep: ea name, possibly with type appended
 *
 * Returns: GFS2_EATYPE_XXX
 */

unsigned int gfs2_ea_name2type(const char *name, const char **truncated_name)
{
	unsigned int type;

	if (strncmp(name, "system.", 7) == 0) {
		type = GFS2_EATYPE_SYS;
		if (truncated_name)
			*truncated_name = name + sizeof("system.") - 1;
	} else if (strncmp(name, "user.", 5) == 0) {
		type = GFS2_EATYPE_USR;
		if (truncated_name)
			*truncated_name = name + sizeof("user.") - 1;
	} else if (strncmp(name, "security.", 9) == 0) {
		type = GFS2_EATYPE_SECURITY;
		if (truncated_name)
			*truncated_name = name + sizeof("security.") - 1;
	} else {
		type = GFS2_EATYPE_UNUSED;
		if (truncated_name)
			*truncated_name = NULL;
	}

	return type;
}

static int user_eo_get(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct inode *inode = &ip->i_inode;
	int error = permission(inode, MAY_READ, NULL);
	if (error)
		return error;

	return gfs2_ea_get_i(ip, er);
}

static int user_eo_set(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct inode *inode = &ip->i_inode;

	if (S_ISREG(inode->i_mode) ||
	    (S_ISDIR(inode->i_mode) && !(inode->i_mode & S_ISVTX))) {
		int error = permission(inode, MAY_WRITE, NULL);
		if (error)
			return error;
	} else
		return -EPERM;

	return gfs2_ea_set_i(ip, er);
}

static int user_eo_remove(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct inode *inode = &ip->i_inode;

	if (S_ISREG(inode->i_mode) ||
	    (S_ISDIR(inode->i_mode) && !(inode->i_mode & S_ISVTX))) {
		int error = permission(inode, MAY_WRITE, NULL);
		if (error)
			return error;
	} else
		return -EPERM;

	return gfs2_ea_remove_i(ip, er);
}

static int system_eo_get(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	if (!GFS2_ACL_IS_ACCESS(er->er_name, er->er_name_len) &&
	    !GFS2_ACL_IS_DEFAULT(er->er_name, er->er_name_len) &&
	    !capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (GFS2_SB(&ip->i_inode)->sd_args.ar_posix_acl == 0 &&
	    (GFS2_ACL_IS_ACCESS(er->er_name, er->er_name_len) ||
	     GFS2_ACL_IS_DEFAULT(er->er_name, er->er_name_len)))
		return -EOPNOTSUPP;



	return gfs2_ea_get_i(ip, er);
}

static int system_eo_set(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	int remove = 0;
	int error;

	if (GFS2_ACL_IS_ACCESS(er->er_name, er->er_name_len)) {
		if (!(er->er_flags & GFS2_ERF_MODE)) {
			er->er_mode = ip->i_di.di_mode;
			er->er_flags |= GFS2_ERF_MODE;
		}
		error = gfs2_acl_validate_set(ip, 1, er,
					      &remove, &er->er_mode);
		if (error)
			return error;
		error = gfs2_ea_set_i(ip, er);
		if (error)
			return error;
		if (remove)
			gfs2_ea_remove_i(ip, er);
		return 0;

	} else if (GFS2_ACL_IS_DEFAULT(er->er_name, er->er_name_len)) {
		error = gfs2_acl_validate_set(ip, 0, er,
					      &remove, NULL);
		if (error)
			return error;
		if (!remove)
			error = gfs2_ea_set_i(ip, er);
		else {
			error = gfs2_ea_remove_i(ip, er);
			if (error == -ENODATA)
				error = 0;
		}
		return error;
	}

	return -EPERM;
}

static int system_eo_remove(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	if (GFS2_ACL_IS_ACCESS(er->er_name, er->er_name_len)) {
		int error = gfs2_acl_validate_remove(ip, 1);
		if (error)
			return error;

	} else if (GFS2_ACL_IS_DEFAULT(er->er_name, er->er_name_len)) {
		int error = gfs2_acl_validate_remove(ip, 0);
		if (error)
			return error;

	} else
		return -EPERM;

	return gfs2_ea_remove_i(ip, er);
}

static int security_eo_get(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct inode *inode = &ip->i_inode;
	int error = permission(inode, MAY_READ, NULL);
	if (error)
		return error;

	return gfs2_ea_get_i(ip, er);
}

static int security_eo_set(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct inode *inode = &ip->i_inode;
	int error = permission(inode, MAY_WRITE, NULL);
	if (error)
		return error;

	return gfs2_ea_set_i(ip, er);
}

static int security_eo_remove(struct gfs2_inode *ip, struct gfs2_ea_request *er)
{
	struct inode *inode = &ip->i_inode;
	int error = permission(inode, MAY_WRITE, NULL);
	if (error)
		return error;

	return gfs2_ea_remove_i(ip, er);
}

static struct gfs2_eattr_operations gfs2_user_eaops = {
	.eo_get = user_eo_get,
	.eo_set = user_eo_set,
	.eo_remove = user_eo_remove,
	.eo_name = "user",
};

struct gfs2_eattr_operations gfs2_system_eaops = {
	.eo_get = system_eo_get,
	.eo_set = system_eo_set,
	.eo_remove = system_eo_remove,
	.eo_name = "system",
};

static struct gfs2_eattr_operations gfs2_security_eaops = {
	.eo_get = security_eo_get,
	.eo_set = security_eo_set,
	.eo_remove = security_eo_remove,
	.eo_name = "security",
};

struct gfs2_eattr_operations *gfs2_ea_ops[] = {
	NULL,
	&gfs2_user_eaops,
	&gfs2_system_eaops,
	&gfs2_security_eaops,
};

