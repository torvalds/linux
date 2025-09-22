/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)amq_subr.c	8.1 (Berkeley) 6/6/93
 *	$Id: amq_subr.c,v 1.20 2024/04/23 13:34:51 jsg Exp $
 */

/*
 * Auxiliary routines for amq tool
 */

#include "am.h"
#include "amq.h"
#include <ctype.h>

bool_t	xdr_amq_mount_info_list(XDR *, amq_mount_info_list *);

void *
amqproc_null_57_svc(void *argp, struct svc_req *rqstp)
{
	static char res;

	return &res;
}

/*
 * Return a sub-tree of mounts
 */
amq_mount_tree_p *
amqproc_mnttree_57_svc(amq_string *argp, struct svc_req *rqstp)
{
	static am_node *mp;

	mp = find_ap(*argp);
	return (amq_mount_tree_p *) &mp;
}

/*
 * Unmount a single node
 */
void *
amqproc_umnt_57_svc(amq_string *argp, struct svc_req *rqstp)
{
	static char res;

	am_node *mp = find_ap(*argp);
	if (mp)
		forcibly_timeout_mp(mp);

	return &res;
}

/*
 * Return global statistics
 */
amq_mount_stats *
amqproc_stats_57_svc(void *argp, struct svc_req *rqstp)
{
	return (amq_mount_stats *) &amd_stats;
}

/*
 * Return the entire tree of mount nodes
 */
amq_mount_tree_list *
amqproc_export_57_svc(void *argp, struct svc_req *rqstp)
{
	static amq_mount_tree_list aml;

	aml.amq_mount_tree_list_val = (amq_mount_tree_p *) &exported_ap[0];
	aml.amq_mount_tree_list_len = 1;	/* XXX */

	return &aml;
}

int *
amqproc_setopt_57_svc(amq_setopt *argp, struct svc_req *rqstp)
{
	static int rc;

	rc = 0;
	switch (argp->as_opt) {
	case AMOPT_DEBUG:
#ifdef DEBUG
		if (debug_option(argp->as_str))
			rc = EINVAL;
#else
		rc = EINVAL;
#endif /* DEBUG */
		break;

	case AMOPT_LOGFILE:
#ifdef not_yet
		if (switch_to_logfile(argp->as_str))
			rc = EINVAL;
#else
		rc = EACCES;
#endif /* not_yet */
		break;

	case AMOPT_XLOG:
		if (switch_option(argp->as_str))
			rc = EINVAL;
		break;

	case AMOPT_FLUSHMAPC:
		if (amd_state == Run) {
			plog(XLOG_INFO, "amq says flush cache");
			do_mapc_reload = 0;
			flush_nfs_fhandle_cache((fserver *) 0);
			flush_srvr_nfs_cache();
		}
		break;
	}
	return &rc;
}

amq_mount_info_list *
amqproc_getmntfs_57_svc(void *argp, struct svc_req *rqstp)
{
	extern qelem mfhead;
	return (amq_mount_info_list *) &mfhead;	/* XXX */
}

amq_string *
amqproc_getvers_57_svc(void *argp, struct svc_req *rqstp)
{
	static amq_string res;

	res = "amd 1.1.1.1 of 1995/10/18 08:47:13 bsd44";
	return &res;
}

/*
 * XDR routines.
 */
bool_t
xdr_amq_string(XDR *xdrs, amq_string *objp)
{
	if (!xdr_string(xdrs, objp, AMQ_STRLEN)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_amq_setopt(XDR *xdrs, amq_setopt *objp)
{
	if (!xdr_enum(xdrs, (enum_t *)&objp->as_opt)) {
		return (FALSE);
	}
	if (!xdr_string(xdrs, &objp->as_str, AMQ_STRLEN)) {
		return (FALSE);
	}
	return (TRUE);
}

/*
 * More XDR routines  - Should be used for OUTPUT ONLY.
 */
static bool_t
xdr_amq_mount_tree_node(XDR *xdrs, amq_mount_tree *objp)
{
	am_node *mp = (am_node *) objp;
	long long mounttime = mp->am_stats.s_mtime;

	if (!xdr_amq_string(xdrs, &mp->am_mnt->mf_info)) {
		return (FALSE);
	}
	if (!xdr_amq_string(xdrs, &mp->am_path)) {
		return (FALSE);
	}
	if (!xdr_amq_string(xdrs, mp->am_link ? &mp->am_link : &mp->am_mnt->mf_mount)) {
		return (FALSE);
	}
	if (!xdr_amq_string(xdrs, &mp->am_mnt->mf_ops->fs_type)) {
		return (FALSE);
	}
	if (!xdr_int64_t(xdrs, &mounttime)) {
		return (FALSE);
	}
	if (!xdr_u_short(xdrs, &mp->am_stats.s_uid)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &mp->am_stats.s_getattr)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &mp->am_stats.s_lookup)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &mp->am_stats.s_readdir)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &mp->am_stats.s_readlink)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &mp->am_stats.s_statfs)) {
		return (FALSE);
	}
	return (TRUE);
}

static bool_t
xdr_amq_mount_subtree(XDR *xdrs, amq_mount_tree *objp)
{
	am_node *mp = (am_node *) objp;

	if (!xdr_amq_mount_tree_node(xdrs, objp)) {
		return (FALSE);
	}
	if (!xdr_pointer(xdrs, (char **)&mp->am_osib, sizeof(amq_mount_tree), xdr_amq_mount_subtree)) {
		return (FALSE);
	}
	if (!xdr_pointer(xdrs, (char **)&mp->am_child, sizeof(amq_mount_tree), xdr_amq_mount_subtree)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_amq_mount_tree(XDR *xdrs, amq_mount_tree *objp)
{
	am_node *mp = (am_node *) objp;
	am_node *mnil = 0;

	if (!xdr_amq_mount_tree_node(xdrs, objp)) {
		return (FALSE);
	}
	if (!xdr_pointer(xdrs, (char **)&mnil, sizeof(amq_mount_tree), xdr_amq_mount_subtree)) {
		return (FALSE);
	}
	if (!xdr_pointer(xdrs, (char **)&mp->am_child, sizeof(amq_mount_tree), xdr_amq_mount_subtree)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_amq_mount_tree_p(XDR *xdrs, amq_mount_tree_p *objp)
{
	if (!xdr_pointer(xdrs, (char **)objp, sizeof(amq_mount_tree), xdr_amq_mount_tree)) {
		return (FALSE);
	}
	return (TRUE);
}


bool_t
xdr_amq_mount_stats(XDR *xdrs, amq_mount_stats *objp)
{
	if (!xdr_int(xdrs, &objp->as_drops)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->as_stale)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->as_mok)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->as_merr)) {
		return (FALSE);
	}
	if (!xdr_int(xdrs, &objp->as_uerr)) {
		return (FALSE);
	}
	return (TRUE);
}


bool_t
xdr_amq_mount_tree_list(XDR *xdrs, amq_mount_tree_list *objp)
{
	if (!xdr_array(xdrs, (char **)&objp->amq_mount_tree_list_val, (u_int *)&objp->amq_mount_tree_list_len, ~0, sizeof(amq_mount_tree_p), xdr_amq_mount_tree_p)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_amq_mount_info_list(XDR *xdrs, amq_mount_info_list *arg)
{
	qelem *qhead = (qelem *)arg;

	/*
	 * Compute length of list
	 */
	mntfs *mf;
	u_int len = 0;

	for (mf = LAST(mntfs, qhead); mf != HEAD(mntfs, qhead); mf = PREV(mntfs, mf)) {
		if (!(mf->mf_ops->fs_flags & FS_AMQINFO))
			continue;
		len++;
	}
	xdr_u_int(xdrs, &len);

	/*
	 * Send individual data items
	 */
	for (mf = LAST(mntfs, qhead); mf != HEAD(mntfs, qhead); mf = PREV(mntfs, mf)) {
		int up;
		if (!(mf->mf_ops->fs_flags & FS_AMQINFO))
			continue;

		if (!xdr_amq_string(xdrs, &mf->mf_ops->fs_type)) {
			return (FALSE);
		}
		if (!xdr_amq_string(xdrs, &mf->mf_mount)) {
			return (FALSE);
		}
		if (!xdr_amq_string(xdrs, &mf->mf_info)) {
			return (FALSE);
		}
		if (!xdr_amq_string(xdrs, &mf->mf_server->fs_host)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &mf->mf_error)) {
			return (FALSE);
		}
		if (!xdr_int(xdrs, &mf->mf_refc)) {
			return (FALSE);
		}
		if (mf->mf_server->fs_flags & FSF_ERROR)
			up = 0;
		else switch (mf->mf_server->fs_flags & (FSF_DOWN|FSF_VALID)) {
		case FSF_DOWN|FSF_VALID: up = 0; break;
		case FSF_VALID: up = 1; break;
		default: up = -1; break;
		}
		if (!xdr_int(xdrs, &up)) {
			return (FALSE);
		}
	}
	return (TRUE);
}
