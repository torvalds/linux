/*
 *   fs/cifs/smb2transport.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org) 2006
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <linux/mempool.h>
#include "smb2pdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "smb2proto.h"
#include "cifs_debug.h"
#include "smb2status.h"

/*
 * Set message id for the request. Should be called after wait_for_free_request
 * and when srv_mutex is held.
 */
static inline void
smb2_seq_num_into_buf(struct TCP_Server_Info *server, struct smb2_hdr *hdr)
{
	hdr->MessageId = get_next_mid(server);
}

static struct mid_q_entry *
smb2_mid_entry_alloc(const struct smb2_hdr *smb_buffer,
		     struct TCP_Server_Info *server)
{
	struct mid_q_entry *temp;

	if (server == NULL) {
		cERROR(1, "Null TCP session in smb2_mid_entry_alloc");
		return NULL;
	}

	temp = mempool_alloc(cifs_mid_poolp, GFP_NOFS);
	if (temp == NULL)
		return temp;
	else {
		memset(temp, 0, sizeof(struct mid_q_entry));
		temp->mid = smb_buffer->MessageId;	/* always LE */
		temp->pid = current->pid;
		temp->command = smb_buffer->Command;	/* Always LE */
		temp->when_alloc = jiffies;
		temp->server = server;

		/*
		 * The default is for the mid to be synchronous, so the
		 * default callback just wakes up the current task.
		 */
		temp->callback = cifs_wake_up_task;
		temp->callback_data = current;
	}

	atomic_inc(&midCount);
	temp->mid_state = MID_REQUEST_ALLOCATED;
	return temp;
}

static int
smb2_get_mid_entry(struct cifs_ses *ses, struct smb2_hdr *buf,
		   struct mid_q_entry **mid)
{
	if (ses->server->tcpStatus == CifsExiting)
		return -ENOENT;

	if (ses->server->tcpStatus == CifsNeedReconnect) {
		cFYI(1, "tcp session dead - return to caller to retry");
		return -EAGAIN;
	}

	if (ses->status != CifsGood) {
		/* check if SMB2 session is bad because we are setting it up */
		if ((buf->Command != SMB2_SESSION_SETUP) &&
		    (buf->Command != SMB2_NEGOTIATE))
			return -EAGAIN;
		/* else ok - we are setting up session */
	}
	*mid = smb2_mid_entry_alloc(buf, ses->server);
	if (*mid == NULL)
		return -ENOMEM;
	spin_lock(&GlobalMid_Lock);
	list_add_tail(&(*mid)->qhead, &ses->server->pending_mid_q);
	spin_unlock(&GlobalMid_Lock);
	return 0;
}

int
smb2_check_receive(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		   bool log_error)
{
	unsigned int len = get_rfc1002_length(mid->resp_buf);

	dump_smb(mid->resp_buf, min_t(u32, 80, len));
	/* convert the length into a more usable form */
	/* BB - uncomment with SMB2 signing implementation */
	/* if ((len > 24) &&
	    (server->sec_mode & (SECMODE_SIGN_REQUIRED|SECMODE_SIGN_ENABLED))) {
		if (smb2_verify_signature(mid->resp_buf, server))
			cERROR(1, "Unexpected SMB signature");
	} */

	return map_smb2_to_linux_error(mid->resp_buf, log_error);
}

int
smb2_setup_request(struct cifs_ses *ses, struct kvec *iov,
		   unsigned int nvec, struct mid_q_entry **ret_mid)
{
	int rc;
	struct smb2_hdr *hdr = (struct smb2_hdr *)iov[0].iov_base;
	struct mid_q_entry *mid;

	smb2_seq_num_into_buf(ses->server, hdr);

	rc = smb2_get_mid_entry(ses, hdr, &mid);
	if (rc)
		return rc;
	/* rc = smb2_sign_smb2(iov, nvec, ses->server);
	if (rc)
		delete_mid(mid); */
	*ret_mid = mid;
	return rc;
}

int
smb2_setup_async_request(struct TCP_Server_Info *server, struct kvec *iov,
			 unsigned int nvec, struct mid_q_entry **ret_mid)
{
	int rc = 0;
	struct smb2_hdr *hdr = (struct smb2_hdr *)iov[0].iov_base;
	struct mid_q_entry *mid;

	smb2_seq_num_into_buf(server, hdr);

	mid = smb2_mid_entry_alloc(hdr, server);
	if (mid == NULL)
		return -ENOMEM;

	/* rc = smb2_sign_smb2(iov, nvec, server);
	if (rc) {
		DeleteMidQEntry(mid);
		return rc;
	}*/
	*ret_mid = mid;
	return rc;
}
