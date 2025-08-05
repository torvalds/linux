// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2008
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *   Jeremy Allison (jra@samba.org) 2006.
 *
 */

#include <linux/fs.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/tcp.h>
#include <linux/bvec.h>
#include <linux/highmem.h>
#include <linux/uaccess.h>
#include <linux/processor.h>
#include <linux/mempool.h>
#include <linux/sched/signal.h>
#include <linux/task_io_accounting_ops.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "smb2proto.h"
#include "smbdirect.h"
#include "compress.h"

/* Max number of iovectors we can use off the stack when sending requests. */
#define CIFS_MAX_IOV_SIZE 8

static struct mid_q_entry *
alloc_mid(const struct smb_hdr *smb_buffer, struct TCP_Server_Info *server)
{
	struct mid_q_entry *temp;

	if (server == NULL) {
		cifs_dbg(VFS, "%s: null TCP session\n", __func__);
		return NULL;
	}

	temp = mempool_alloc(cifs_mid_poolp, GFP_NOFS);
	memset(temp, 0, sizeof(struct mid_q_entry));
	kref_init(&temp->refcount);
	spin_lock_init(&temp->mid_lock);
	temp->mid = get_mid(smb_buffer);
	temp->pid = current->pid;
	temp->command = cpu_to_le16(smb_buffer->Command);
	cifs_dbg(FYI, "For smb_command %d\n", smb_buffer->Command);
	/* easier to use jiffies */
	/* when mid allocated can be before when sent */
	temp->when_alloc = jiffies;
	temp->server = server;

	/*
	 * The default is for the mid to be synchronous, so the
	 * default callback just wakes up the current task.
	 */
	get_task_struct(current);
	temp->creator = current;
	temp->callback = cifs_wake_up_task;
	temp->callback_data = current;

	atomic_inc(&mid_count);
	temp->mid_state = MID_REQUEST_ALLOCATED;
	return temp;
}

int
smb_send(struct TCP_Server_Info *server, struct smb_hdr *smb_buffer,
	 unsigned int smb_buf_length)
{
	struct kvec iov[1] = {
		[0].iov_base = smb_buffer,
		[0].iov_len = smb_buf_length,
	};
	struct smb_rqst rqst = {
		.rq_iov = iov,
		.rq_nvec = ARRAY_SIZE(iov),
	};

	return __smb_send_rqst(server, 1, &rqst);
}

static int allocate_mid(struct cifs_ses *ses, struct smb_hdr *in_buf,
			struct mid_q_entry **ppmidQ)
{
	spin_lock(&ses->ses_lock);
	if (ses->ses_status == SES_NEW) {
		if ((in_buf->Command != SMB_COM_SESSION_SETUP_ANDX) &&
			(in_buf->Command != SMB_COM_NEGOTIATE)) {
			spin_unlock(&ses->ses_lock);
			return -EAGAIN;
		}
		/* else ok - we are setting up session */
	}

	if (ses->ses_status == SES_EXITING) {
		/* check if SMB session is bad because we are setting it up */
		if (in_buf->Command != SMB_COM_LOGOFF_ANDX) {
			spin_unlock(&ses->ses_lock);
			return -EAGAIN;
		}
		/* else ok - we are shutting down session */
	}
	spin_unlock(&ses->ses_lock);

	*ppmidQ = alloc_mid(in_buf, ses->server);
	if (*ppmidQ == NULL)
		return -ENOMEM;
	spin_lock(&ses->server->mid_queue_lock);
	list_add_tail(&(*ppmidQ)->qhead, &ses->server->pending_mid_q);
	spin_unlock(&ses->server->mid_queue_lock);
	return 0;
}

struct mid_q_entry *
cifs_setup_async_request(struct TCP_Server_Info *server, struct smb_rqst *rqst)
{
	int rc;
	struct smb_hdr *hdr = (struct smb_hdr *)rqst->rq_iov[0].iov_base;
	struct mid_q_entry *mid;

	/* enable signing if server requires it */
	if (server->sign)
		hdr->Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	mid = alloc_mid(hdr, server);
	if (mid == NULL)
		return ERR_PTR(-ENOMEM);

	rc = cifs_sign_rqst(rqst, server, &mid->sequence_number);
	if (rc) {
		release_mid(mid);
		return ERR_PTR(rc);
	}

	return mid;
}

/*
 *
 * Send an SMB Request.  No response info (other than return code)
 * needs to be parsed.
 *
 * flags indicate the type of request buffer and how long to wait
 * and whether to log NT STATUS code (error) before mapping it to POSIX error
 *
 */
int
SendReceiveNoRsp(const unsigned int xid, struct cifs_ses *ses,
		 char *in_buf, unsigned int in_len, int flags)
{
	int rc;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int resp_buf_type;

	iov[0].iov_base = in_buf;
	iov[0].iov_len = in_len;
	flags |= CIFS_NO_RSP_BUF;
	rc = SendReceive2(xid, ses, iov, 1, &resp_buf_type, flags, &rsp_iov);
	cifs_dbg(NOISY, "SendRcvNoRsp flags %d rc %d\n", flags, rc);

	return rc;
}

int
cifs_check_receive(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		   bool log_error)
{
	unsigned int len = mid->response_pdu_len;

	dump_smb(mid->resp_buf, min_t(u32, 92, len));

	/* convert the length into a more usable form */
	if (server->sign) {
		struct kvec iov[1];
		int rc = 0;
		struct smb_rqst rqst = { .rq_iov = iov,
					 .rq_nvec = ARRAY_SIZE(iov) };

		iov[0].iov_base = mid->resp_buf;
		iov[0].iov_len = len;
		/* FIXME: add code to kill session */
		rc = cifs_verify_signature(&rqst, server,
					   mid->sequence_number);
		if (rc)
			cifs_server_dbg(VFS, "SMB signature verification returned error = %d\n",
				 rc);
	}

	/* BB special case reconnect tid and uid here? */
	return map_and_check_smb_error(mid, log_error);
}

struct mid_q_entry *
cifs_setup_request(struct cifs_ses *ses, struct TCP_Server_Info *ignored,
		   struct smb_rqst *rqst)
{
	int rc;
	struct smb_hdr *hdr = (struct smb_hdr *)rqst->rq_iov[0].iov_base;
	struct mid_q_entry *mid;

	rc = allocate_mid(ses, hdr, &mid);
	if (rc)
		return ERR_PTR(rc);
	rc = cifs_sign_rqst(rqst, ses->server, &mid->sequence_number);
	if (rc) {
		delete_mid(mid);
		return ERR_PTR(rc);
	}
	return mid;
}

int
SendReceive2(const unsigned int xid, struct cifs_ses *ses,
	     struct kvec *iov, int n_vec, int *resp_buf_type /* ret */,
	     const int flags, struct kvec *resp_iov)
{
	struct smb_rqst rqst = {
		.rq_iov = iov,
		.rq_nvec = n_vec,
	};

	return cifs_send_recv(xid, ses, ses->server,
			      &rqst, resp_buf_type, flags, resp_iov);
}

int
SendReceive(const unsigned int xid, struct cifs_ses *ses,
	    struct smb_hdr *in_buf, unsigned int in_len,
	    struct smb_hdr *out_buf, int *pbytes_returned, const int flags)
{
	struct TCP_Server_Info *server;
	struct kvec resp_iov = {};
	struct kvec iov = { .iov_base = in_buf, .iov_len = in_len };
	struct smb_rqst rqst = { .rq_iov = &iov, .rq_nvec = 1 };
	int resp_buf_type;
	int rc = 0;

	if (WARN_ON_ONCE(in_len > 0xffffff))
		return -EIO;
	if (ses == NULL) {
		cifs_dbg(VFS, "Null smb session\n");
		return -EIO;
	}
	server = ses->server;
	if (server == NULL) {
		cifs_dbg(VFS, "Null tcp session\n");
		return -EIO;
	}

	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsExiting) {
		spin_unlock(&server->srv_lock);
		return -ENOENT;
	}
	spin_unlock(&server->srv_lock);

	/* Ensure that we do not send more than 50 overlapping requests
	   to the same server. We may make this configurable later or
	   use ses->maxReq */

	if (in_len > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cifs_server_dbg(VFS, "Invalid length, greater than maximum frame, %d\n",
				in_len);
		return -EIO;
	}

	rc = cifs_send_recv(xid, ses, ses->server,
			    &rqst, &resp_buf_type, flags, &resp_iov);
	if (rc < 0)
		return rc;

	*pbytes_returned = resp_iov.iov_len;
	if (resp_iov.iov_len)
		memcpy(out_buf, resp_iov.iov_base, resp_iov.iov_len);
	free_rsp_buf(resp_buf_type, resp_iov.iov_base);
	return rc;
}

/* We send a LOCKINGX_CANCEL_LOCK to cause the Windows
   blocking lock to return. */

static int
send_lock_cancel(const unsigned int xid, struct cifs_tcon *tcon,
		 struct smb_hdr *in_buf, unsigned int in_len,
		 struct smb_hdr *out_buf)
{
	int bytes_returned;
	struct cifs_ses *ses = tcon->ses;
	LOCK_REQ *pSMB = (LOCK_REQ *)in_buf;

	/* We just modify the current in_buf to change
	   the type of lock from LOCKING_ANDX_SHARED_LOCK
	   or LOCKING_ANDX_EXCLUSIVE_LOCK to
	   LOCKING_ANDX_CANCEL_LOCK. */

	pSMB->LockType = LOCKING_ANDX_CANCEL_LOCK|LOCKING_ANDX_LARGE_FILES;
	pSMB->Timeout = 0;
	pSMB->hdr.Mid = get_next_mid(ses->server);

	return SendReceive(xid, ses, in_buf, in_len, out_buf,
			&bytes_returned, 0);
}

int SendReceiveBlockingLock(const unsigned int xid, struct cifs_tcon *tcon,
			    struct smb_hdr *in_buf, unsigned int in_len,
			    struct smb_hdr *out_buf, int *pbytes_returned)
{
	int rc = 0;
	int rstart = 0;
	struct mid_q_entry *mid;
	struct cifs_ses *ses;
	struct kvec iov = { .iov_base = in_buf, .iov_len = in_len };
	struct smb_rqst rqst = { .rq_iov = &iov, .rq_nvec = 1 };
	unsigned int instance;
	struct TCP_Server_Info *server;

	if (WARN_ON_ONCE(in_len > 0xffffff))
		return -EIO;
	if (tcon == NULL || tcon->ses == NULL) {
		cifs_dbg(VFS, "Null smb session\n");
		return -EIO;
	}
	ses = tcon->ses;
	server = ses->server;

	if (server == NULL) {
		cifs_dbg(VFS, "Null tcp session\n");
		return -EIO;
	}

	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsExiting) {
		spin_unlock(&server->srv_lock);
		return -ENOENT;
	}
	spin_unlock(&server->srv_lock);

	/* Ensure that we do not send more than 50 overlapping requests
	   to the same server. We may make this configurable later or
	   use ses->maxReq */

	if (in_len > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cifs_tcon_dbg(VFS, "Invalid length, greater than maximum frame, %d\n",
			      in_len);
		return -EIO;
	}

	rc = wait_for_free_request(server, CIFS_BLOCKING_OP, &instance);
	if (rc)
		return rc;

	/* make sure that we sign in the same order that we send on this socket
	   and avoid races inside tcp sendmsg code that could cause corruption
	   of smb data */

	cifs_server_lock(server);

	rc = allocate_mid(ses, in_buf, &mid);
	if (rc) {
		cifs_server_unlock(server);
		return rc;
	}

	rc = cifs_sign_smb(in_buf, in_len, server, &mid->sequence_number);
	if (rc) {
		delete_mid(mid);
		cifs_server_unlock(server);
		return rc;
	}

	mid->mid_state = MID_REQUEST_SUBMITTED;
	rc = smb_send(server, in_buf, in_len);
	cifs_save_when_sent(mid);

	if (rc < 0)
		server->sequence_number -= 2;

	cifs_server_unlock(server);

	if (rc < 0) {
		delete_mid(mid);
		return rc;
	}

	/* Wait for a reply - allow signals to interrupt. */
	rc = wait_event_interruptible(server->response_q,
		(!(mid->mid_state == MID_REQUEST_SUBMITTED ||
		   mid->mid_state == MID_RESPONSE_RECEIVED)) ||
		((server->tcpStatus != CifsGood) &&
		 (server->tcpStatus != CifsNew)));

	/* Were we interrupted by a signal ? */
	spin_lock(&server->srv_lock);
	if ((rc == -ERESTARTSYS) &&
		(mid->mid_state == MID_REQUEST_SUBMITTED ||
		 mid->mid_state == MID_RESPONSE_RECEIVED) &&
		((server->tcpStatus == CifsGood) ||
		 (server->tcpStatus == CifsNew))) {
		spin_unlock(&server->srv_lock);

		if (in_buf->Command == SMB_COM_TRANSACTION2) {
			/* POSIX lock. We send a NT_CANCEL SMB to cause the
			   blocking lock to return. */
			rc = send_cancel(server, &rqst, mid);
			if (rc) {
				delete_mid(mid);
				return rc;
			}
		} else {
			/* Windows lock. We send a LOCKINGX_CANCEL_LOCK
			   to cause the blocking lock to return. */

			rc = send_lock_cancel(xid, tcon, in_buf, in_len, out_buf);

			/* If we get -ENOLCK back the lock may have
			   already been removed. Don't exit in this case. */
			if (rc && rc != -ENOLCK) {
				delete_mid(mid);
				return rc;
			}
		}

		rc = wait_for_response(server, mid);
		if (rc) {
			send_cancel(server, &rqst, mid);
			spin_lock(&mid->mid_lock);
			if (mid->callback) {
				/* no longer considered to be "in-flight" */
				mid->callback = release_mid;
				spin_unlock(&mid->mid_lock);
				return rc;
			}
			spin_unlock(&mid->mid_lock);
		}

		/* We got the response - restart system call. */
		rstart = 1;
		spin_lock(&server->srv_lock);
	}
	spin_unlock(&server->srv_lock);

	rc = cifs_sync_mid_result(mid, server);
	if (rc != 0)
		return rc;

	/* rcvd frame is ok */
	if (out_buf == NULL || mid->mid_state != MID_RESPONSE_READY) {
		rc = -EIO;
		cifs_tcon_dbg(VFS, "Bad MID state?\n");
		goto out;
	}

	*pbytes_returned = mid->response_pdu_len;
	memcpy(out_buf, mid->resp_buf, *pbytes_returned);
	rc = cifs_check_receive(mid, server, 0);
out:
	delete_mid(mid);
	if (rstart && rc == -EACCES)
		return -ERESTARTSYS;
	return rc;
}
