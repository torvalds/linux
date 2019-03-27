/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2013 Xin Li <delphij@FreeBSD.org>. All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 * Portions Copyright 2005, 2010, Oracle and/or its affiliates.
 * All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/cred.h>
#include <sys/dmu.h>
#include <sys/zio.h>
#include <sys/nvpair.h>
#include <sys/dsl_deleg.h>
#include <sys/zfs_ioctl.h>
#include "zfs_namecheck.h"
#include "zfs_ioctl_compat.h"

static int zfs_version_ioctl = ZFS_IOCVER_CURRENT;
SYSCTL_DECL(_vfs_zfs_version);
SYSCTL_INT(_vfs_zfs_version, OID_AUTO, ioctl, CTLFLAG_RD, &zfs_version_ioctl,
    0, "ZFS_IOCTL_VERSION");

/*
 * FreeBSD zfs_cmd compatibility with older binaries
 * appropriately remap/extend the zfs_cmd_t structure
 */
void
zfs_cmd_compat_get(zfs_cmd_t *zc, caddr_t addr, const int cflag)
{
	zfs_cmd_v15_t *zc_c;
	zfs_cmd_v28_t *zc28_c;
	zfs_cmd_deadman_t *zcdm_c;
	zfs_cmd_zcmd_t *zcmd_c;
	zfs_cmd_edbp_t *edbp_c;
	zfs_cmd_resume_t *resume_c;
	zfs_cmd_inlanes_t *inlanes_c;

	switch (cflag) {
	case ZFS_CMD_COMPAT_INLANES:
		inlanes_c = (void *)addr;
		/* zc */
		strlcpy(zc->zc_name, inlanes_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, inlanes_c->zc_value, MAXPATHLEN * 2);
		strlcpy(zc->zc_string, inlanes_c->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zc->field = inlanes_c->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		FIELD_COPY(zc_begin_record);
		FIELD_COPY(zc_inject_record);
		FIELD_COPY(zc_defer_destroy);
		FIELD_COPY(zc_flags);
		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		FIELD_COPY(zc_resumable);
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_RESUME:
		resume_c = (void *)addr;
		/* zc */
		strlcpy(zc->zc_name, resume_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, resume_c->zc_value, MAXPATHLEN * 2);
		strlcpy(zc->zc_string, resume_c->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zc->field = resume_c->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		FIELD_COPY(zc_begin_record);
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(zc->zc_inject_record.zi_func,
		    resume_c->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		zc->zc_inject_record.zi_nlanes = 1;
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);
		FIELD_COPY(zc_defer_destroy);
		FIELD_COPY(zc_flags);
		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		FIELD_COPY(zc_resumable);
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_EDBP:
		edbp_c = (void *)addr;
		/* zc */
		strlcpy(zc->zc_name, edbp_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, edbp_c->zc_value, MAXPATHLEN * 2);
		strlcpy(zc->zc_string, edbp_c->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zc->field = edbp_c->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		zc->zc_begin_record.drr_u.drr_begin = edbp_c->zc_begin_record;
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(zc->zc_inject_record.zi_func,
		    edbp_c->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		zc->zc_inject_record.zi_nlanes = 1;
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);
		FIELD_COPY(zc_defer_destroy);
		FIELD_COPY(zc_flags);
		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		zc->zc_resumable = B_FALSE;
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_ZCMD:
		zcmd_c = (void *)addr;
		/* zc */
		strlcpy(zc->zc_name, zcmd_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, zcmd_c->zc_value, MAXPATHLEN * 2);
		strlcpy(zc->zc_string, zcmd_c->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zc->field = zcmd_c->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		zc->zc_begin_record.drr_u.drr_begin = zcmd_c->zc_begin_record;
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(zc->zc_inject_record.zi_func,
		    zcmd_c->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		zc->zc_inject_record.zi_nlanes = 1;
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);

		/* boolean_t -> uint32_t */
		zc->zc_defer_destroy = (uint32_t)(zcmd_c->zc_defer_destroy);
		zc->zc_flags = 0;

		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		zc->zc_resumable = B_FALSE;
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY

		break;

	case ZFS_CMD_COMPAT_DEADMAN:
		zcdm_c = (void *)addr;
		/* zc */
		strlcpy(zc->zc_name, zcdm_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, zcdm_c->zc_value, MAXPATHLEN * 2);
		strlcpy(zc->zc_string, zcdm_c->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zc->field = zcdm_c->field
		zc->zc_guid = zcdm_c->zc_guid;
		zc->zc_nvlist_conf = zcdm_c->zc_nvlist_conf;
		zc->zc_nvlist_conf_size = zcdm_c->zc_nvlist_conf_size;
		zc->zc_nvlist_src = zcdm_c->zc_nvlist_src;
		zc->zc_nvlist_src_size = zcdm_c->zc_nvlist_src_size;
		zc->zc_nvlist_dst = zcdm_c->zc_nvlist_dst;
		zc->zc_nvlist_dst_size = zcdm_c->zc_nvlist_dst_size;
		zc->zc_cookie = zcdm_c->zc_cookie;
		zc->zc_objset_type = zcdm_c->zc_objset_type;
		zc->zc_perm_action = zcdm_c->zc_perm_action;
		zc->zc_history = zcdm_c->zc_history;
		zc->zc_history_len = zcdm_c->zc_history_len;
		zc->zc_history_offset = zcdm_c->zc_history_offset;
		zc->zc_obj = zcdm_c->zc_obj;
		zc->zc_iflags = zcdm_c->zc_iflags;
		zc->zc_share = zcdm_c->zc_share;
		zc->zc_jailid = zcdm_c->zc_jailid;
		zc->zc_objset_stats = zcdm_c->zc_objset_stats;
		zc->zc_begin_record.drr_u.drr_begin = zcdm_c->zc_begin_record;
		zc->zc_defer_destroy = zcdm_c->zc_defer_destroy;
		(void)zcdm_c->zc_temphold;
		zc->zc_action_handle = zcdm_c->zc_action_handle;
		zc->zc_cleanup_fd = zcdm_c->zc_cleanup_fd;
		zc->zc_simple = zcdm_c->zc_simple;
		zc->zc_resumable = B_FALSE;
		zc->zc_sendobj = zcdm_c->zc_sendobj;
		zc->zc_fromobj = zcdm_c->zc_fromobj;
		zc->zc_createtxg = zcdm_c->zc_createtxg;
		zc->zc_stat = zcdm_c->zc_stat;
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(zc->zc_inject_record.zi_func,
		    resume_c->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		zc->zc_inject_record.zi_nlanes = 1;
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);

		/* we always assume zc_nvlist_dst_filled is true */
		zc->zc_nvlist_dst_filled = B_TRUE;
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_V28:
		zc28_c = (void *)addr;

		/* zc */
		strlcpy(zc->zc_name, zc28_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, zc28_c->zc_value, MAXPATHLEN * 2);
		strlcpy(zc->zc_string, zc28_c->zc_string, MAXPATHLEN);
		zc->zc_guid = zc28_c->zc_guid;
		zc->zc_nvlist_conf = zc28_c->zc_nvlist_conf;
		zc->zc_nvlist_conf_size = zc28_c->zc_nvlist_conf_size;
		zc->zc_nvlist_src = zc28_c->zc_nvlist_src;
		zc->zc_nvlist_src_size = zc28_c->zc_nvlist_src_size;
		zc->zc_nvlist_dst = zc28_c->zc_nvlist_dst;
		zc->zc_nvlist_dst_size = zc28_c->zc_nvlist_dst_size;
		zc->zc_cookie = zc28_c->zc_cookie;
		zc->zc_objset_type = zc28_c->zc_objset_type;
		zc->zc_perm_action = zc28_c->zc_perm_action;
		zc->zc_history = zc28_c->zc_history;
		zc->zc_history_len = zc28_c->zc_history_len;
		zc->zc_history_offset = zc28_c->zc_history_offset;
		zc->zc_obj = zc28_c->zc_obj;
		zc->zc_iflags = zc28_c->zc_iflags;
		zc->zc_share = zc28_c->zc_share;
		zc->zc_jailid = zc28_c->zc_jailid;
		zc->zc_objset_stats = zc28_c->zc_objset_stats;
		zc->zc_begin_record.drr_u.drr_begin = zc28_c->zc_begin_record;
		zc->zc_defer_destroy = zc28_c->zc_defer_destroy;
		(void)zc28_c->zc_temphold;
		zc->zc_action_handle = zc28_c->zc_action_handle;
		zc->zc_cleanup_fd = zc28_c->zc_cleanup_fd;
		zc->zc_simple = zc28_c->zc_simple;
		zc->zc_resumable = B_FALSE;
		zc->zc_sendobj = zc28_c->zc_sendobj;
		zc->zc_fromobj = zc28_c->zc_fromobj;
		zc->zc_createtxg = zc28_c->zc_createtxg;
		zc->zc_stat = zc28_c->zc_stat;

		/* zc->zc_inject_record */
		zc->zc_inject_record.zi_objset =
		    zc28_c->zc_inject_record.zi_objset;
		zc->zc_inject_record.zi_object =
		    zc28_c->zc_inject_record.zi_object;
		zc->zc_inject_record.zi_start =
		    zc28_c->zc_inject_record.zi_start;
		zc->zc_inject_record.zi_end =
		    zc28_c->zc_inject_record.zi_end;
		zc->zc_inject_record.zi_guid =
		    zc28_c->zc_inject_record.zi_guid;
		zc->zc_inject_record.zi_level =
		    zc28_c->zc_inject_record.zi_level;
		zc->zc_inject_record.zi_error =
		    zc28_c->zc_inject_record.zi_error;
		zc->zc_inject_record.zi_type =
		    zc28_c->zc_inject_record.zi_type;
		zc->zc_inject_record.zi_freq =
		    zc28_c->zc_inject_record.zi_freq;
		zc->zc_inject_record.zi_failfast =
		    zc28_c->zc_inject_record.zi_failfast;
		strlcpy(zc->zc_inject_record.zi_func,
		    zc28_c->zc_inject_record.zi_func, MAXNAMELEN);
		zc->zc_inject_record.zi_iotype =
		    zc28_c->zc_inject_record.zi_iotype;
		zc->zc_inject_record.zi_duration =
		    zc28_c->zc_inject_record.zi_duration;
		zc->zc_inject_record.zi_timer =
		    zc28_c->zc_inject_record.zi_timer;
		zc->zc_inject_record.zi_nlanes = 1;
		zc->zc_inject_record.zi_cmd = ZINJECT_UNINITIALIZED;
		zc->zc_inject_record.zi_pad = 0;
		break;

	case ZFS_CMD_COMPAT_V15:
		zc_c = (void *)addr;

		/* zc */
		strlcpy(zc->zc_name, zc_c->zc_name, MAXPATHLEN);
		strlcpy(zc->zc_value, zc_c->zc_value, MAXPATHLEN);
		strlcpy(zc->zc_string, zc_c->zc_string, MAXPATHLEN);
		zc->zc_guid = zc_c->zc_guid;
		zc->zc_nvlist_conf = zc_c->zc_nvlist_conf;
		zc->zc_nvlist_conf_size = zc_c->zc_nvlist_conf_size;
		zc->zc_nvlist_src = zc_c->zc_nvlist_src;
		zc->zc_nvlist_src_size = zc_c->zc_nvlist_src_size;
		zc->zc_nvlist_dst = zc_c->zc_nvlist_dst;
		zc->zc_nvlist_dst_size = zc_c->zc_nvlist_dst_size;
		zc->zc_cookie = zc_c->zc_cookie;
		zc->zc_objset_type = zc_c->zc_objset_type;
		zc->zc_perm_action = zc_c->zc_perm_action;
		zc->zc_history = zc_c->zc_history;
		zc->zc_history_len = zc_c->zc_history_len;
		zc->zc_history_offset = zc_c->zc_history_offset;
		zc->zc_obj = zc_c->zc_obj;
		zc->zc_share = zc_c->zc_share;
		zc->zc_jailid = zc_c->zc_jailid;
		zc->zc_objset_stats = zc_c->zc_objset_stats;
		zc->zc_begin_record.drr_u.drr_begin = zc_c->zc_begin_record;

		/* zc->zc_inject_record */
		zc->zc_inject_record.zi_objset =
		    zc_c->zc_inject_record.zi_objset;
		zc->zc_inject_record.zi_object =
		    zc_c->zc_inject_record.zi_object;
		zc->zc_inject_record.zi_start =
		    zc_c->zc_inject_record.zi_start;
		zc->zc_inject_record.zi_end =
		    zc_c->zc_inject_record.zi_end;
		zc->zc_inject_record.zi_guid =
		    zc_c->zc_inject_record.zi_guid;
		zc->zc_inject_record.zi_level =
		    zc_c->zc_inject_record.zi_level;
		zc->zc_inject_record.zi_error =
		    zc_c->zc_inject_record.zi_error;
		zc->zc_inject_record.zi_type =
		    zc_c->zc_inject_record.zi_type;
		zc->zc_inject_record.zi_freq =
		    zc_c->zc_inject_record.zi_freq;
		zc->zc_inject_record.zi_failfast =
		    zc_c->zc_inject_record.zi_failfast;
		break;
	}
}

void
zfs_cmd_compat_put(zfs_cmd_t *zc, caddr_t addr, const int request,
    const int cflag)
{
	zfs_cmd_v15_t *zc_c;
	zfs_cmd_v28_t *zc28_c;
	zfs_cmd_deadman_t *zcdm_c;
	zfs_cmd_zcmd_t *zcmd_c;
	zfs_cmd_edbp_t *edbp_c;
	zfs_cmd_resume_t *resume_c;
	zfs_cmd_inlanes_t *inlanes_c;

	switch (cflag) {
	case ZFS_CMD_COMPAT_INLANES:
		inlanes_c = (void *)addr;
		strlcpy(inlanes_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(inlanes_c->zc_value, zc->zc_value, MAXPATHLEN * 2);
		strlcpy(inlanes_c->zc_string, zc->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) inlanes_c->field = zc->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		FIELD_COPY(zc_begin_record);
		FIELD_COPY(zc_inject_record);
		FIELD_COPY(zc_defer_destroy);
		FIELD_COPY(zc_flags);
		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_RESUME:
		resume_c = (void *)addr;
		strlcpy(resume_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(resume_c->zc_value, zc->zc_value, MAXPATHLEN * 2);
		strlcpy(resume_c->zc_string, zc->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) resume_c->field = zc->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		FIELD_COPY(zc_begin_record);
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(resume_c->zc_inject_record.zi_func,
		    zc->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);
		FIELD_COPY(zc_defer_destroy);
		FIELD_COPY(zc_flags);
		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_EDBP:
		edbp_c = (void *)addr;
		strlcpy(edbp_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(edbp_c->zc_value, zc->zc_value, MAXPATHLEN * 2);
		strlcpy(edbp_c->zc_string, zc->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) edbp_c->field = zc->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		edbp_c->zc_begin_record = zc->zc_begin_record.drr_u.drr_begin;
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(resume_c->zc_inject_record.zi_func,
		    zc->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);
		FIELD_COPY(zc_defer_destroy);
		FIELD_COPY(zc_flags);
		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY
		break;

	case ZFS_CMD_COMPAT_ZCMD:
		zcmd_c = (void *)addr;
		/* zc */
		strlcpy(zcmd_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(zcmd_c->zc_value, zc->zc_value, MAXPATHLEN * 2);
		strlcpy(zcmd_c->zc_string, zc->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zcmd_c->field = zc->field
		FIELD_COPY(zc_nvlist_src);
		FIELD_COPY(zc_nvlist_src_size);
		FIELD_COPY(zc_nvlist_dst);
		FIELD_COPY(zc_nvlist_dst_size);
		FIELD_COPY(zc_nvlist_dst_filled);
		FIELD_COPY(zc_pad2);
		FIELD_COPY(zc_history);
		FIELD_COPY(zc_guid);
		FIELD_COPY(zc_nvlist_conf);
		FIELD_COPY(zc_nvlist_conf_size);
		FIELD_COPY(zc_cookie);
		FIELD_COPY(zc_objset_type);
		FIELD_COPY(zc_perm_action);
		FIELD_COPY(zc_history_len);
		FIELD_COPY(zc_history_offset);
		FIELD_COPY(zc_obj);
		FIELD_COPY(zc_iflags);
		FIELD_COPY(zc_share);
		FIELD_COPY(zc_jailid);
		FIELD_COPY(zc_objset_stats);
		zcmd_c->zc_begin_record = zc->zc_begin_record.drr_u.drr_begin;
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(resume_c->zc_inject_record.zi_func,
		    zc->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);

		/* boolean_t -> uint32_t */
		zcmd_c->zc_defer_destroy = (uint32_t)(zc->zc_defer_destroy);
		zcmd_c->zc_temphold = 0;

		FIELD_COPY(zc_action_handle);
		FIELD_COPY(zc_cleanup_fd);
		FIELD_COPY(zc_simple);
		FIELD_COPY(zc_sendobj);
		FIELD_COPY(zc_fromobj);
		FIELD_COPY(zc_createtxg);
		FIELD_COPY(zc_stat);
#undef FIELD_COPY

		break;

	case ZFS_CMD_COMPAT_DEADMAN:
		zcdm_c = (void *)addr;

		strlcpy(zcdm_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(zcdm_c->zc_value, zc->zc_value, MAXPATHLEN * 2);
		strlcpy(zcdm_c->zc_string, zc->zc_string, MAXPATHLEN);

#define FIELD_COPY(field) zcdm_c->field = zc->field
		zcdm_c->zc_guid = zc->zc_guid;
		zcdm_c->zc_nvlist_conf = zc->zc_nvlist_conf;
		zcdm_c->zc_nvlist_conf_size = zc->zc_nvlist_conf_size;
		zcdm_c->zc_nvlist_src = zc->zc_nvlist_src;
		zcdm_c->zc_nvlist_src_size = zc->zc_nvlist_src_size;
		zcdm_c->zc_nvlist_dst = zc->zc_nvlist_dst;
		zcdm_c->zc_nvlist_dst_size = zc->zc_nvlist_dst_size;
		zcdm_c->zc_cookie = zc->zc_cookie;
		zcdm_c->zc_objset_type = zc->zc_objset_type;
		zcdm_c->zc_perm_action = zc->zc_perm_action;
		zcdm_c->zc_history = zc->zc_history;
		zcdm_c->zc_history_len = zc->zc_history_len;
		zcdm_c->zc_history_offset = zc->zc_history_offset;
		zcdm_c->zc_obj = zc->zc_obj;
		zcdm_c->zc_iflags = zc->zc_iflags;
		zcdm_c->zc_share = zc->zc_share;
		zcdm_c->zc_jailid = zc->zc_jailid;
		zcdm_c->zc_objset_stats = zc->zc_objset_stats;
		zcdm_c->zc_begin_record = zc->zc_begin_record.drr_u.drr_begin;
		zcdm_c->zc_defer_destroy = zc->zc_defer_destroy;
		zcdm_c->zc_temphold = 0;
		zcdm_c->zc_action_handle = zc->zc_action_handle;
		zcdm_c->zc_cleanup_fd = zc->zc_cleanup_fd;
		zcdm_c->zc_simple = zc->zc_simple;
		zcdm_c->zc_sendobj = zc->zc_sendobj;
		zcdm_c->zc_fromobj = zc->zc_fromobj;
		zcdm_c->zc_createtxg = zc->zc_createtxg;
		zcdm_c->zc_stat = zc->zc_stat;
		FIELD_COPY(zc_inject_record.zi_objset);
		FIELD_COPY(zc_inject_record.zi_object);
		FIELD_COPY(zc_inject_record.zi_start);
		FIELD_COPY(zc_inject_record.zi_end);
		FIELD_COPY(zc_inject_record.zi_guid);
		FIELD_COPY(zc_inject_record.zi_level);
		FIELD_COPY(zc_inject_record.zi_error);
		FIELD_COPY(zc_inject_record.zi_type);
		FIELD_COPY(zc_inject_record.zi_freq);
		FIELD_COPY(zc_inject_record.zi_failfast);
		strlcpy(resume_c->zc_inject_record.zi_func,
		    zc->zc_inject_record.zi_func, MAXNAMELEN);
		FIELD_COPY(zc_inject_record.zi_iotype);
		FIELD_COPY(zc_inject_record.zi_duration);
		FIELD_COPY(zc_inject_record.zi_timer);
		FIELD_COPY(zc_inject_record.zi_cmd);
		FIELD_COPY(zc_inject_record.zi_pad);
#undef FIELD_COPY
#ifndef _KERNEL
		if (request == ZFS_IOC_RECV)
			strlcpy(zcdm_c->zc_top_ds,
			    zc->zc_value + strlen(zc->zc_value) + 1,
			    (MAXPATHLEN * 2) - strlen(zc->zc_value) - 1);
#endif
		break;

	case ZFS_CMD_COMPAT_V28:
		zc28_c = (void *)addr;

		strlcpy(zc28_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(zc28_c->zc_value, zc->zc_value, MAXPATHLEN * 2);
		strlcpy(zc28_c->zc_string, zc->zc_string, MAXPATHLEN);
		zc28_c->zc_guid = zc->zc_guid;
		zc28_c->zc_nvlist_conf = zc->zc_nvlist_conf;
		zc28_c->zc_nvlist_conf_size = zc->zc_nvlist_conf_size;
		zc28_c->zc_nvlist_src = zc->zc_nvlist_src;
		zc28_c->zc_nvlist_src_size = zc->zc_nvlist_src_size;
		zc28_c->zc_nvlist_dst = zc->zc_nvlist_dst;
		zc28_c->zc_nvlist_dst_size = zc->zc_nvlist_dst_size;
		zc28_c->zc_cookie = zc->zc_cookie;
		zc28_c->zc_objset_type = zc->zc_objset_type;
		zc28_c->zc_perm_action = zc->zc_perm_action;
		zc28_c->zc_history = zc->zc_history;
		zc28_c->zc_history_len = zc->zc_history_len;
		zc28_c->zc_history_offset = zc->zc_history_offset;
		zc28_c->zc_obj = zc->zc_obj;
		zc28_c->zc_iflags = zc->zc_iflags;
		zc28_c->zc_share = zc->zc_share;
		zc28_c->zc_jailid = zc->zc_jailid;
		zc28_c->zc_objset_stats = zc->zc_objset_stats;
		zc28_c->zc_begin_record = zc->zc_begin_record.drr_u.drr_begin;
		zc28_c->zc_defer_destroy = zc->zc_defer_destroy;
		zc28_c->zc_temphold = 0;
		zc28_c->zc_action_handle = zc->zc_action_handle;
		zc28_c->zc_cleanup_fd = zc->zc_cleanup_fd;
		zc28_c->zc_simple = zc->zc_simple;
		zc28_c->zc_sendobj = zc->zc_sendobj;
		zc28_c->zc_fromobj = zc->zc_fromobj;
		zc28_c->zc_createtxg = zc->zc_createtxg;
		zc28_c->zc_stat = zc->zc_stat;
#ifndef _KERNEL
		if (request == ZFS_IOC_RECV)
			strlcpy(zc28_c->zc_top_ds,
			    zc->zc_value + strlen(zc->zc_value) + 1,
			    MAXPATHLEN * 2 - strlen(zc->zc_value) - 1);
#endif
		/* zc_inject_record */
		zc28_c->zc_inject_record.zi_objset =
		    zc->zc_inject_record.zi_objset;
		zc28_c->zc_inject_record.zi_object =
		    zc->zc_inject_record.zi_object;
		zc28_c->zc_inject_record.zi_start =
		    zc->zc_inject_record.zi_start;
		zc28_c->zc_inject_record.zi_end =
		    zc->zc_inject_record.zi_end;
		zc28_c->zc_inject_record.zi_guid =
		    zc->zc_inject_record.zi_guid;
		zc28_c->zc_inject_record.zi_level =
		    zc->zc_inject_record.zi_level;
		zc28_c->zc_inject_record.zi_error =
		    zc->zc_inject_record.zi_error;
		zc28_c->zc_inject_record.zi_type =
		    zc->zc_inject_record.zi_type;
		zc28_c->zc_inject_record.zi_freq =
		    zc->zc_inject_record.zi_freq;
		zc28_c->zc_inject_record.zi_failfast =
		    zc->zc_inject_record.zi_failfast;
		strlcpy(zc28_c->zc_inject_record.zi_func,
		    zc->zc_inject_record.zi_func, MAXNAMELEN);
		zc28_c->zc_inject_record.zi_iotype =
		    zc->zc_inject_record.zi_iotype;
		zc28_c->zc_inject_record.zi_duration =
		    zc->zc_inject_record.zi_duration;
		zc28_c->zc_inject_record.zi_timer =
		    zc->zc_inject_record.zi_timer;
		break;

	case ZFS_CMD_COMPAT_V15:
		zc_c = (void *)addr;

		/* zc */
		strlcpy(zc_c->zc_name, zc->zc_name, MAXPATHLEN);
		strlcpy(zc_c->zc_value, zc->zc_value, MAXPATHLEN);
		strlcpy(zc_c->zc_string, zc->zc_string, MAXPATHLEN);
		zc_c->zc_guid = zc->zc_guid;
		zc_c->zc_nvlist_conf = zc->zc_nvlist_conf;
		zc_c->zc_nvlist_conf_size = zc->zc_nvlist_conf_size;
		zc_c->zc_nvlist_src = zc->zc_nvlist_src;
		zc_c->zc_nvlist_src_size = zc->zc_nvlist_src_size;
		zc_c->zc_nvlist_dst = zc->zc_nvlist_dst;
		zc_c->zc_nvlist_dst_size = zc->zc_nvlist_dst_size;
		zc_c->zc_cookie = zc->zc_cookie;
		zc_c->zc_objset_type = zc->zc_objset_type;
		zc_c->zc_perm_action = zc->zc_perm_action;
		zc_c->zc_history = zc->zc_history;
		zc_c->zc_history_len = zc->zc_history_len;
		zc_c->zc_history_offset = zc->zc_history_offset;
		zc_c->zc_obj = zc->zc_obj;
		zc_c->zc_share = zc->zc_share;
		zc_c->zc_jailid = zc->zc_jailid;
		zc_c->zc_objset_stats = zc->zc_objset_stats;
		zc_c->zc_begin_record = zc->zc_begin_record.drr_u.drr_begin;

		/* zc_inject_record */
		zc_c->zc_inject_record.zi_objset =
		    zc->zc_inject_record.zi_objset;
		zc_c->zc_inject_record.zi_object =
		    zc->zc_inject_record.zi_object;
		zc_c->zc_inject_record.zi_start =
		    zc->zc_inject_record.zi_start;
		zc_c->zc_inject_record.zi_end =
		    zc->zc_inject_record.zi_end;
		zc_c->zc_inject_record.zi_guid =
		    zc->zc_inject_record.zi_guid;
		zc_c->zc_inject_record.zi_level =
		    zc->zc_inject_record.zi_level;
		zc_c->zc_inject_record.zi_error =
		    zc->zc_inject_record.zi_error;
		zc_c->zc_inject_record.zi_type =
		    zc->zc_inject_record.zi_type;
		zc_c->zc_inject_record.zi_freq =
		    zc->zc_inject_record.zi_freq;
		zc_c->zc_inject_record.zi_failfast =
		    zc->zc_inject_record.zi_failfast;

		break;
	}
}

static int
zfs_ioctl_compat_get_nvlist(uint64_t nvl, size_t size, int iflag,
    nvlist_t **nvp)
{
	char *packed;
	int error;
	nvlist_t *list = NULL;

	/*
	 * Read in and unpack the user-supplied nvlist.
	 */
	if (size == 0)
		return (EINVAL);

#ifdef _KERNEL
	packed = kmem_alloc(size, KM_SLEEP);
	if ((error = ddi_copyin((void *)(uintptr_t)nvl, packed, size,
	    iflag)) != 0) {
		kmem_free(packed, size);
		return (error);
	}
#else
	packed = (void *)(uintptr_t)nvl;
#endif

	error = nvlist_unpack(packed, size, &list, 0);

#ifdef _KERNEL
	kmem_free(packed, size);
#endif

	if (error != 0)
		return (error);

	*nvp = list;
	return (0);
}

static int
zfs_ioctl_compat_put_nvlist(zfs_cmd_t *zc, nvlist_t *nvl)
{
	char *packed = NULL;
	int error = 0;
	size_t size;

	VERIFY(nvlist_size(nvl, &size, NV_ENCODE_NATIVE) == 0);

#ifdef _KERNEL
	packed = kmem_alloc(size, KM_SLEEP);
	VERIFY(nvlist_pack(nvl, &packed, &size, NV_ENCODE_NATIVE,
	    KM_SLEEP) == 0);

	if (ddi_copyout(packed,
	    (void *)(uintptr_t)zc->zc_nvlist_dst, size, zc->zc_iflags) != 0)
		error = EFAULT;
	kmem_free(packed, size);
#else
	packed = (void *)(uintptr_t)zc->zc_nvlist_dst;
	VERIFY(nvlist_pack(nvl, &packed, &size, NV_ENCODE_NATIVE,
	    0) == 0);
#endif

	zc->zc_nvlist_dst_size = size;
	return (error);
}

static void
zfs_ioctl_compat_fix_stats_nvlist(nvlist_t *nvl)
{
	nvlist_t **child;
	nvlist_t *nvroot = NULL;
	vdev_stat_t *vs;
	uint_t c, children, nelem;

	if (nvlist_lookup_nvlist_array(nvl, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++) {
			zfs_ioctl_compat_fix_stats_nvlist(child[c]);
		}
	}

	if (nvlist_lookup_nvlist(nvl, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0)
		zfs_ioctl_compat_fix_stats_nvlist(nvroot);
#ifdef _KERNEL
	if ((nvlist_lookup_uint64_array(nvl, ZPOOL_CONFIG_VDEV_STATS,
#else
	if ((nvlist_lookup_uint64_array(nvl, "stats",
#endif

	    (uint64_t **)&vs, &nelem) == 0)) {
		nvlist_add_uint64_array(nvl,
#ifdef _KERNEL
		    "stats",
#else
		    ZPOOL_CONFIG_VDEV_STATS,
#endif
		    (uint64_t *)vs, nelem);
#ifdef _KERNEL
		nvlist_remove(nvl, ZPOOL_CONFIG_VDEV_STATS,
#else
		nvlist_remove(nvl, "stats",
#endif
		    DATA_TYPE_UINT64_ARRAY);
	}
}

static int
zfs_ioctl_compat_fix_stats(zfs_cmd_t *zc, const int nc)
{
	nvlist_t *nv, *nvp = NULL;
	nvpair_t *elem;
	int error;

	if ((error = zfs_ioctl_compat_get_nvlist(zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, zc->zc_iflags, &nv)) != 0)
		return (error);

	if (nc == 5) { /* ZFS_IOC_POOL_STATS */
		elem = NULL;
		while ((elem = nvlist_next_nvpair(nv, elem)) != NULL) {
			if (nvpair_value_nvlist(elem, &nvp) == 0)
				zfs_ioctl_compat_fix_stats_nvlist(nvp);
		}
		elem = NULL;
	} else
		zfs_ioctl_compat_fix_stats_nvlist(nv);

	error = zfs_ioctl_compat_put_nvlist(zc, nv);

	nvlist_free(nv);

	return (error);
}

static int
zfs_ioctl_compat_pool_get_props(zfs_cmd_t *zc)
{
	nvlist_t *nv, *nva = NULL;
	int error;

	if ((error = zfs_ioctl_compat_get_nvlist(zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, zc->zc_iflags, &nv)) != 0)
		return (error);

#ifdef _KERNEL
	if (nvlist_lookup_nvlist(nv, "allocated", &nva) == 0) {
		nvlist_add_nvlist(nv, "used", nva);
		nvlist_remove(nv, "allocated", DATA_TYPE_NVLIST);
	}

	if (nvlist_lookup_nvlist(nv, "free", &nva) == 0) {
		nvlist_add_nvlist(nv, "available", nva);
		nvlist_remove(nv, "free", DATA_TYPE_NVLIST);
	}
#else
	if (nvlist_lookup_nvlist(nv, "used", &nva) == 0) {
		nvlist_add_nvlist(nv, "allocated", nva);
		nvlist_remove(nv, "used", DATA_TYPE_NVLIST);
	}

	if (nvlist_lookup_nvlist(nv, "available", &nva) == 0) {
		nvlist_add_nvlist(nv, "free", nva);
		nvlist_remove(nv, "available", DATA_TYPE_NVLIST);
	}
#endif

	error = zfs_ioctl_compat_put_nvlist(zc, nv);

	nvlist_free(nv);

	return (error);
}

#ifndef _KERNEL
int
zcmd_ioctl_compat(int fd, int request, zfs_cmd_t *zc, const int cflag)
{
	int nc, ret;
	void *zc_c;
	unsigned long ncmd;
	zfs_iocparm_t zp;

	switch (cflag) {
	case ZFS_CMD_COMPAT_NONE:
		ncmd = _IOWR('Z', request, struct zfs_iocparm);
		zp.zfs_cmd = (uint64_t)zc;
		zp.zfs_cmd_size = sizeof(zfs_cmd_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_CURRENT;
		return (ioctl(fd, ncmd, &zp));
	case ZFS_CMD_COMPAT_INLANES:
		ncmd = _IOWR('Z', request, struct zfs_iocparm);
		zp.zfs_cmd = (uint64_t)zc;
		zp.zfs_cmd_size = sizeof(zfs_cmd_inlanes_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_INLANES;
		return (ioctl(fd, ncmd, &zp));
	case ZFS_CMD_COMPAT_RESUME:
		ncmd = _IOWR('Z', request, struct zfs_iocparm);
		zp.zfs_cmd = (uint64_t)zc;
		zp.zfs_cmd_size = sizeof(zfs_cmd_resume_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_RESUME;
		return (ioctl(fd, ncmd, &zp));
	case ZFS_CMD_COMPAT_EDBP:
		ncmd = _IOWR('Z', request, struct zfs_iocparm);
		zp.zfs_cmd = (uint64_t)zc;
		zp.zfs_cmd_size = sizeof(zfs_cmd_edbp_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_EDBP;
		return (ioctl(fd, ncmd, &zp));
	case ZFS_CMD_COMPAT_ZCMD:
		ncmd = _IOWR('Z', request, struct zfs_iocparm);
		zp.zfs_cmd = (uint64_t)zc;
		zp.zfs_cmd_size = sizeof(zfs_cmd_zcmd_t);
		zp.zfs_ioctl_version = ZFS_IOCVER_ZCMD;
		return (ioctl(fd, ncmd, &zp));
	case ZFS_CMD_COMPAT_LZC:
		ncmd = _IOWR('Z', request, struct zfs_cmd);
		return (ioctl(fd, ncmd, zc));
	case ZFS_CMD_COMPAT_DEADMAN:
		zc_c = malloc(sizeof(zfs_cmd_deadman_t));
		ncmd = _IOWR('Z', request, struct zfs_cmd_deadman);
		break;
	case ZFS_CMD_COMPAT_V28:
		zc_c = malloc(sizeof(zfs_cmd_v28_t));
		ncmd = _IOWR('Z', request, struct zfs_cmd_v28);
		break;
	case ZFS_CMD_COMPAT_V15:
		nc = zfs_ioctl_v28_to_v15[request];
		zc_c = malloc(sizeof(zfs_cmd_v15_t));
		ncmd = _IOWR('Z', nc, struct zfs_cmd_v15);
		break;
	default:
		return (EINVAL);
	}

	if (ZFS_IOCREQ(ncmd) == ZFS_IOC_COMPAT_FAIL)
		return (ENOTSUP);

	zfs_cmd_compat_put(zc, (caddr_t)zc_c, request, cflag);

	ret = ioctl(fd, ncmd, zc_c);
	if (cflag == ZFS_CMD_COMPAT_V15 &&
	    nc == ZFS_IOC_POOL_IMPORT)
		ret = ioctl(fd, _IOWR('Z', ZFS_IOC_POOL_CONFIGS,
		    struct zfs_cmd_v15), zc_c);
	zfs_cmd_compat_get(zc, (caddr_t)zc_c, cflag);
	free(zc_c);

	if (cflag == ZFS_CMD_COMPAT_V15) {
		switch (nc) {
		case ZFS_IOC_POOL_IMPORT:
		case ZFS_IOC_POOL_CONFIGS:
		case ZFS_IOC_POOL_STATS:
		case ZFS_IOC_POOL_TRYIMPORT:
			zfs_ioctl_compat_fix_stats(zc, nc);
			break;
		case 41: /* ZFS_IOC_POOL_GET_PROPS (v15) */
			zfs_ioctl_compat_pool_get_props(zc);
			break;
		}
	}

	return (ret);
}
#else /* _KERNEL */
int
zfs_ioctl_compat_pre(zfs_cmd_t *zc, int *vec, const int cflag)
{
	int error = 0;

	/* are we creating a clone? */
	if (*vec == ZFS_IOC_CREATE && zc->zc_value[0] != '\0')
		*vec = ZFS_IOC_CLONE;

	if (cflag == ZFS_CMD_COMPAT_V15) {
		switch (*vec) {

		case 7: /* ZFS_IOC_POOL_SCRUB (v15) */
			zc->zc_cookie = POOL_SCAN_SCRUB;
			break;
		}
	}

	return (error);
}

void
zfs_ioctl_compat_post(zfs_cmd_t *zc, int vec, const int cflag)
{
	if (cflag == ZFS_CMD_COMPAT_V15) {
		switch (vec) {
		case ZFS_IOC_POOL_CONFIGS:
		case ZFS_IOC_POOL_STATS:
		case ZFS_IOC_POOL_TRYIMPORT:
			zfs_ioctl_compat_fix_stats(zc, vec);
			break;
		case 41: /* ZFS_IOC_POOL_GET_PROPS (v15) */
			zfs_ioctl_compat_pool_get_props(zc);
			break;
		}
	}
}

nvlist_t *
zfs_ioctl_compat_innvl(zfs_cmd_t *zc, nvlist_t * innvl, const int vec,
    const int cflag)
{
	nvlist_t *nvl, *tmpnvl, *hnvl;
	nvpair_t *elem;
	char *poolname, *snapname;
	int err;

	if (cflag == ZFS_CMD_COMPAT_NONE || cflag == ZFS_CMD_COMPAT_LZC ||
	    cflag == ZFS_CMD_COMPAT_ZCMD || cflag == ZFS_CMD_COMPAT_EDBP ||
	    cflag == ZFS_CMD_COMPAT_RESUME || cflag == ZFS_CMD_COMPAT_INLANES)
		goto out;

	switch (vec) {
	case ZFS_IOC_CREATE:
		nvl = fnvlist_alloc();
		fnvlist_add_int32(nvl, "type", zc->zc_objset_type);
		if (innvl != NULL) {
			fnvlist_add_nvlist(nvl, "props", innvl);
			nvlist_free(innvl);
		}
		return (nvl);
	break;
	case ZFS_IOC_CLONE:
		nvl = fnvlist_alloc();
		fnvlist_add_string(nvl, "origin", zc->zc_value);
		if (innvl != NULL) {
			fnvlist_add_nvlist(nvl, "props", innvl);
			nvlist_free(innvl);
		}
		return (nvl);
	break;
	case ZFS_IOC_SNAPSHOT:
		if (innvl == NULL)
			goto out;
		nvl = fnvlist_alloc();
		fnvlist_add_nvlist(nvl, "props", innvl);
		tmpnvl = fnvlist_alloc();
		snapname = kmem_asprintf("%s@%s", zc->zc_name, zc->zc_value);
		fnvlist_add_boolean(tmpnvl, snapname);
		kmem_free(snapname, strlen(snapname + 1));
		/* check if we are doing a recursive snapshot */
		if (zc->zc_cookie)
			dmu_get_recursive_snaps_nvl(zc->zc_name, zc->zc_value,
			    tmpnvl);
		fnvlist_add_nvlist(nvl, "snaps", tmpnvl);
		fnvlist_free(tmpnvl);
		nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	case ZFS_IOC_SPACE_SNAPS:
		nvl = fnvlist_alloc();
		fnvlist_add_string(nvl, "firstsnap", zc->zc_value);
		if (innvl != NULL)
			nvlist_free(innvl);
		return (nvl);
	break;
	case ZFS_IOC_DESTROY_SNAPS:
		if (innvl == NULL && cflag == ZFS_CMD_COMPAT_DEADMAN)
			goto out;
		nvl = fnvlist_alloc();
		if (innvl != NULL) {
			fnvlist_add_nvlist(nvl, "snaps", innvl);
		} else {
			/*
			 * We are probably called by even older binaries,
			 * allocate and populate nvlist with recursive
			 * snapshots
			 */
			if (zfs_component_namecheck(zc->zc_value, NULL,
			    NULL) == 0) {
				tmpnvl = fnvlist_alloc();
				if (dmu_get_recursive_snaps_nvl(zc->zc_name,
				    zc->zc_value, tmpnvl) == 0)
					fnvlist_add_nvlist(nvl, "snaps",
					    tmpnvl);
				nvlist_free(tmpnvl);
			}
		}
		if (innvl != NULL)
			nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	case ZFS_IOC_HOLD:
		nvl = fnvlist_alloc();
		tmpnvl = fnvlist_alloc();
		if (zc->zc_cleanup_fd != -1)
			fnvlist_add_int32(nvl, "cleanup_fd",
			    (int32_t)zc->zc_cleanup_fd);
		if (zc->zc_cookie) {
			hnvl = fnvlist_alloc();
			if (dmu_get_recursive_snaps_nvl(zc->zc_name,
			    zc->zc_value, hnvl) == 0) {
				elem = NULL;
				while ((elem = nvlist_next_nvpair(hnvl,
				    elem)) != NULL) {
					nvlist_add_string(tmpnvl,
					    nvpair_name(elem), zc->zc_string);
				}
			}
			nvlist_free(hnvl);
		} else {
			snapname = kmem_asprintf("%s@%s", zc->zc_name,
			    zc->zc_value);
			nvlist_add_string(tmpnvl, snapname, zc->zc_string);
			kmem_free(snapname, strlen(snapname + 1));
		}
		fnvlist_add_nvlist(nvl, "holds", tmpnvl);
		nvlist_free(tmpnvl);
		if (innvl != NULL)
			nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	case ZFS_IOC_RELEASE:
		nvl = fnvlist_alloc();
		tmpnvl = fnvlist_alloc();
		if (zc->zc_cookie) {
			hnvl = fnvlist_alloc();
			if (dmu_get_recursive_snaps_nvl(zc->zc_name,
			    zc->zc_value, hnvl) == 0) {
				elem = NULL;
				while ((elem = nvlist_next_nvpair(hnvl,
				    elem)) != NULL) {
					fnvlist_add_boolean(tmpnvl,
					    zc->zc_string);
					fnvlist_add_nvlist(nvl,
					    nvpair_name(elem), tmpnvl);
				}
			}
			nvlist_free(hnvl);
		} else {
			snapname = kmem_asprintf("%s@%s", zc->zc_name,
			    zc->zc_value);
			fnvlist_add_boolean(tmpnvl, zc->zc_string);
			fnvlist_add_nvlist(nvl, snapname, tmpnvl);
			kmem_free(snapname, strlen(snapname + 1));
		}
		nvlist_free(tmpnvl);
		if (innvl != NULL)
			nvlist_free(innvl);
		/* strip dataset part from zc->zc_name */
		zc->zc_name[strcspn(zc->zc_name, "/@")] = '\0';
		return (nvl);
	break;
	}
out:
	return (innvl);
}

nvlist_t *
zfs_ioctl_compat_outnvl(zfs_cmd_t *zc, nvlist_t * outnvl, const int vec,
    const int cflag)
{
	nvlist_t *tmpnvl;

	if (cflag == ZFS_CMD_COMPAT_NONE || cflag == ZFS_CMD_COMPAT_LZC ||
	    cflag == ZFS_CMD_COMPAT_ZCMD || cflag == ZFS_CMD_COMPAT_EDBP ||
	    cflag == ZFS_CMD_COMPAT_RESUME || cflag == ZFS_CMD_COMPAT_INLANES)
		return (outnvl);

	switch (vec) {
	case ZFS_IOC_SPACE_SNAPS:
		(void) nvlist_lookup_uint64(outnvl, "used", &zc->zc_cookie);
		(void) nvlist_lookup_uint64(outnvl, "compressed",
		    &zc->zc_objset_type);
		(void) nvlist_lookup_uint64(outnvl, "uncompressed",
		    &zc->zc_perm_action);
		nvlist_free(outnvl);
		/* return empty outnvl */
		tmpnvl = fnvlist_alloc();
		return (tmpnvl);
	break;
	case ZFS_IOC_CREATE:
	case ZFS_IOC_CLONE:
	case ZFS_IOC_HOLD:
	case ZFS_IOC_RELEASE:
		nvlist_free(outnvl);
		/* return empty outnvl */
		tmpnvl = fnvlist_alloc();
		return (tmpnvl);
	break;
	}

	return (outnvl);
}
#endif /* KERNEL */
