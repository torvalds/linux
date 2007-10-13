/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>
#include <linux/parser.h>

#include "gfs2.h"
#include "incore.h"
#include "mount.h"
#include "sys.h"
#include "util.h"

enum {
	Opt_lockproto,
	Opt_locktable,
	Opt_hostdata,
	Opt_spectator,
	Opt_ignore_local_fs,
	Opt_localflocks,
	Opt_localcaching,
	Opt_debug,
	Opt_nodebug,
	Opt_upgrade,
	Opt_num_glockd,
	Opt_acl,
	Opt_noacl,
	Opt_quota_off,
	Opt_quota_account,
	Opt_quota_on,
	Opt_suiddir,
	Opt_nosuiddir,
	Opt_data_writeback,
	Opt_data_ordered,
	Opt_err,
};

static match_table_t tokens = {
	{Opt_lockproto, "lockproto=%s"},
	{Opt_locktable, "locktable=%s"},
	{Opt_hostdata, "hostdata=%s"},
	{Opt_spectator, "spectator"},
	{Opt_ignore_local_fs, "ignore_local_fs"},
	{Opt_localflocks, "localflocks"},
	{Opt_localcaching, "localcaching"},
	{Opt_debug, "debug"},
	{Opt_nodebug, "nodebug"},
	{Opt_upgrade, "upgrade"},
	{Opt_num_glockd, "num_glockd=%d"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_quota_off, "quota=off"},
	{Opt_quota_account, "quota=account"},
	{Opt_quota_on, "quota=on"},
	{Opt_suiddir, "suiddir"},
	{Opt_nosuiddir, "nosuiddir"},
	{Opt_data_writeback, "data=writeback"},
	{Opt_data_ordered, "data=ordered"},
	{Opt_err, NULL}
};

/**
 * gfs2_mount_args - Parse mount options
 * @sdp:
 * @data:
 *
 * Return: errno
 */

int gfs2_mount_args(struct gfs2_sbd *sdp, char *data_arg, int remount)
{
	struct gfs2_args *args = &sdp->sd_args;
	char *data = data_arg;
	char *options, *o, *v;
	int error = 0;

	if (!remount) {
		/*  If someone preloaded options, use those instead  */
		spin_lock(&gfs2_sys_margs_lock);
		if (gfs2_sys_margs) {
			data = gfs2_sys_margs;
			gfs2_sys_margs = NULL;
		}
		spin_unlock(&gfs2_sys_margs_lock);

		/*  Set some defaults  */
		args->ar_num_glockd = GFS2_GLOCKD_DEFAULT;
		args->ar_quota = GFS2_QUOTA_DEFAULT;
		args->ar_data = GFS2_DATA_DEFAULT;
	}

	/* Split the options into tokens with the "," character and
	   process them */

	for (options = data; (o = strsep(&options, ",")); ) {
		int token, option;
		substring_t tmp[MAX_OPT_ARGS];

		if (!*o)
			continue;

		token = match_token(o, tokens, tmp);
		switch (token) {
		case Opt_lockproto:
			v = match_strdup(&tmp[0]);
			if (!v) {
				fs_info(sdp, "no memory for lockproto\n");
				error = -ENOMEM;
				goto out_error;
			}

			if (remount && strcmp(v, args->ar_lockproto)) {
				kfree(v);
				goto cant_remount;
			}
			
			strncpy(args->ar_lockproto, v, GFS2_LOCKNAME_LEN);
			args->ar_lockproto[GFS2_LOCKNAME_LEN - 1] = 0;
			kfree(v);
			break;
		case Opt_locktable:
			v = match_strdup(&tmp[0]);
			if (!v) {
				fs_info(sdp, "no memory for locktable\n");
				error = -ENOMEM;
				goto out_error;
			}

			if (remount && strcmp(v, args->ar_locktable)) {
				kfree(v);
				goto cant_remount;
			}

			strncpy(args->ar_locktable, v, GFS2_LOCKNAME_LEN);
			args->ar_locktable[GFS2_LOCKNAME_LEN - 1]  = 0;
			kfree(v);
			break;
		case Opt_hostdata:
			v = match_strdup(&tmp[0]);
			if (!v) {
				fs_info(sdp, "no memory for hostdata\n");
				error = -ENOMEM;
				goto out_error;
			}

			if (remount && strcmp(v, args->ar_hostdata)) {
				kfree(v);
				goto cant_remount;
			}

			strncpy(args->ar_hostdata, v, GFS2_LOCKNAME_LEN);
			args->ar_hostdata[GFS2_LOCKNAME_LEN - 1] = 0;
			kfree(v);
			break;
		case Opt_spectator:
			if (remount && !args->ar_spectator)
				goto cant_remount;
			args->ar_spectator = 1;
			sdp->sd_vfs->s_flags |= MS_RDONLY;
			break;
		case Opt_ignore_local_fs:
			if (remount && !args->ar_ignore_local_fs)
				goto cant_remount;
			args->ar_ignore_local_fs = 1;
			break;
		case Opt_localflocks:
			if (remount && !args->ar_localflocks)
				goto cant_remount;
			args->ar_localflocks = 1;
			break;
		case Opt_localcaching:
			if (remount && !args->ar_localcaching)
				goto cant_remount;
			args->ar_localcaching = 1;
			break;
		case Opt_debug:
			args->ar_debug = 1;
			break;
		case Opt_nodebug:
			args->ar_debug = 0;
			break;
		case Opt_upgrade:
			if (remount && !args->ar_upgrade)
				goto cant_remount;
			args->ar_upgrade = 1;
			break;
		case Opt_num_glockd:
			if ((error = match_int(&tmp[0], &option))) {
				fs_info(sdp, "problem getting num_glockd\n");
				goto out_error;
			}

			if (remount && option != args->ar_num_glockd)
				goto cant_remount;
			if (!option || option > GFS2_GLOCKD_MAX) {
				fs_info(sdp, "0 < num_glockd <= %u  (not %u)\n",
				        GFS2_GLOCKD_MAX, option);
				error = -EINVAL;
				goto out_error;
			}
			args->ar_num_glockd = option;
			break;
		case Opt_acl:
			args->ar_posix_acl = 1;
			sdp->sd_vfs->s_flags |= MS_POSIXACL;
			break;
		case Opt_noacl:
			args->ar_posix_acl = 0;
			sdp->sd_vfs->s_flags &= ~MS_POSIXACL;
			break;
		case Opt_quota_off:
			args->ar_quota = GFS2_QUOTA_OFF;
			break;
		case Opt_quota_account:
			args->ar_quota = GFS2_QUOTA_ACCOUNT;
			break;
		case Opt_quota_on:
			args->ar_quota = GFS2_QUOTA_ON;
			break;
		case Opt_suiddir:
			args->ar_suiddir = 1;
			break;
		case Opt_nosuiddir:
			args->ar_suiddir = 0;
			break;
		case Opt_data_writeback:
			args->ar_data = GFS2_DATA_WRITEBACK;
			break;
		case Opt_data_ordered:
			args->ar_data = GFS2_DATA_ORDERED;
			break;
		case Opt_err:
		default:
			fs_info(sdp, "unknown option: %s\n", o);
			error = -EINVAL;
			goto out_error;
		}
	}

out_error:
	if (error)
		fs_info(sdp, "invalid mount option(s)\n");

	if (data != data_arg)
		kfree(data);

	return error;

cant_remount:
	fs_info(sdp, "can't remount with option %s\n", o);
	return -EINVAL;
}

