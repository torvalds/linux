// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2008 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "rcom.h"
#include "util.h"

#define DLM_ERRNO_EDEADLK		35
#define DLM_ERRNO_EBADR			53
#define DLM_ERRNO_EBADSLT		57
#define DLM_ERRNO_EPROTO		71
#define DLM_ERRNO_EOPNOTSUPP		95
#define DLM_ERRNO_ETIMEDOUT	       110
#define DLM_ERRNO_EINPROGRESS	       115

void header_out(struct dlm_header *hd)
{
	hd->h_version		= cpu_to_le32(hd->h_version);
	/* does it for others u32 in union as well */
	hd->u.h_lockspace	= cpu_to_le32(hd->u.h_lockspace);
	hd->h_nodeid		= cpu_to_le32(hd->h_nodeid);
	hd->h_length		= cpu_to_le16(hd->h_length);
}

void header_in(struct dlm_header *hd)
{
	hd->h_version		= le32_to_cpu(hd->h_version);
	/* does it for others u32 in union as well */
	hd->u.h_lockspace	= le32_to_cpu(hd->u.h_lockspace);
	hd->h_nodeid		= le32_to_cpu(hd->h_nodeid);
	hd->h_length		= le16_to_cpu(hd->h_length);
}

/* higher errno values are inconsistent across architectures, so select
   one set of values for on the wire */

static int to_dlm_errno(int err)
{
	switch (err) {
	case -EDEADLK:
		return -DLM_ERRNO_EDEADLK;
	case -EBADR:
		return -DLM_ERRNO_EBADR;
	case -EBADSLT:
		return -DLM_ERRNO_EBADSLT;
	case -EPROTO:
		return -DLM_ERRNO_EPROTO;
	case -EOPNOTSUPP:
		return -DLM_ERRNO_EOPNOTSUPP;
	case -ETIMEDOUT:
		return -DLM_ERRNO_ETIMEDOUT;
	case -EINPROGRESS:
		return -DLM_ERRNO_EINPROGRESS;
	}
	return err;
}

static int from_dlm_errno(int err)
{
	switch (err) {
	case -DLM_ERRNO_EDEADLK:
		return -EDEADLK;
	case -DLM_ERRNO_EBADR:
		return -EBADR;
	case -DLM_ERRNO_EBADSLT:
		return -EBADSLT;
	case -DLM_ERRNO_EPROTO:
		return -EPROTO;
	case -DLM_ERRNO_EOPNOTSUPP:
		return -EOPNOTSUPP;
	case -DLM_ERRNO_ETIMEDOUT:
		return -ETIMEDOUT;
	case -DLM_ERRNO_EINPROGRESS:
		return -EINPROGRESS;
	}
	return err;
}

void dlm_message_out(struct dlm_message *ms)
{
	header_out(&ms->m_header);

	ms->m_type		= cpu_to_le32(ms->m_type);
	ms->m_nodeid		= cpu_to_le32(ms->m_nodeid);
	ms->m_pid		= cpu_to_le32(ms->m_pid);
	ms->m_lkid		= cpu_to_le32(ms->m_lkid);
	ms->m_remid		= cpu_to_le32(ms->m_remid);
	ms->m_parent_lkid	= cpu_to_le32(ms->m_parent_lkid);
	ms->m_parent_remid	= cpu_to_le32(ms->m_parent_remid);
	ms->m_exflags		= cpu_to_le32(ms->m_exflags);
	ms->m_sbflags		= cpu_to_le32(ms->m_sbflags);
	ms->m_flags		= cpu_to_le32(ms->m_flags);
	ms->m_lvbseq		= cpu_to_le32(ms->m_lvbseq);
	ms->m_hash		= cpu_to_le32(ms->m_hash);
	ms->m_status		= cpu_to_le32(ms->m_status);
	ms->m_grmode		= cpu_to_le32(ms->m_grmode);
	ms->m_rqmode		= cpu_to_le32(ms->m_rqmode);
	ms->m_bastmode		= cpu_to_le32(ms->m_bastmode);
	ms->m_asts		= cpu_to_le32(ms->m_asts);
	ms->m_result		= cpu_to_le32(to_dlm_errno(ms->m_result));
}

void dlm_message_in(struct dlm_message *ms)
{
	header_in(&ms->m_header);

	ms->m_type		= le32_to_cpu(ms->m_type);
	ms->m_nodeid		= le32_to_cpu(ms->m_nodeid);
	ms->m_pid		= le32_to_cpu(ms->m_pid);
	ms->m_lkid		= le32_to_cpu(ms->m_lkid);
	ms->m_remid		= le32_to_cpu(ms->m_remid);
	ms->m_parent_lkid	= le32_to_cpu(ms->m_parent_lkid);
	ms->m_parent_remid	= le32_to_cpu(ms->m_parent_remid);
	ms->m_exflags		= le32_to_cpu(ms->m_exflags);
	ms->m_sbflags		= le32_to_cpu(ms->m_sbflags);
	ms->m_flags		= le32_to_cpu(ms->m_flags);
	ms->m_lvbseq		= le32_to_cpu(ms->m_lvbseq);
	ms->m_hash		= le32_to_cpu(ms->m_hash);
	ms->m_status		= le32_to_cpu(ms->m_status);
	ms->m_grmode		= le32_to_cpu(ms->m_grmode);
	ms->m_rqmode		= le32_to_cpu(ms->m_rqmode);
	ms->m_bastmode		= le32_to_cpu(ms->m_bastmode);
	ms->m_asts		= le32_to_cpu(ms->m_asts);
	ms->m_result		= from_dlm_errno(le32_to_cpu(ms->m_result));
}

void dlm_rcom_out(struct dlm_rcom *rc)
{
	header_out(&rc->rc_header);

	rc->rc_type		= cpu_to_le32(rc->rc_type);
	rc->rc_result		= cpu_to_le32(rc->rc_result);
	rc->rc_id		= cpu_to_le64(rc->rc_id);
	rc->rc_seq		= cpu_to_le64(rc->rc_seq);
	rc->rc_seq_reply	= cpu_to_le64(rc->rc_seq_reply);
}

void dlm_rcom_in(struct dlm_rcom *rc)
{
	header_in(&rc->rc_header);

	rc->rc_type		= le32_to_cpu(rc->rc_type);
	rc->rc_result		= le32_to_cpu(rc->rc_result);
	rc->rc_id		= le64_to_cpu(rc->rc_id);
	rc->rc_seq		= le64_to_cpu(rc->rc_seq);
	rc->rc_seq_reply	= le64_to_cpu(rc->rc_seq_reply);
}
