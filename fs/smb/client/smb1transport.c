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
#include "cifsglob.h"
#include "cifsproto.h"
#include "smb1proto.h"
#include "smb2proto.h"
#include "cifs_debug.h"
#include "smbdirect.h"
#include "compress.h"
#include "cifs_debug.h"

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

	temp = mempool_alloc(&cifs_mid_pool, GFP_NOFS);
	memset(temp, 0, sizeof(struct mid_q_entry));
	refcount_set(&temp->refcount, 1);
	spin_lock_init(&temp->mid_lock);
	temp->mid = get_mid(smb_buffer);
	temp->pid = current->pid;
	temp->command = cpu_to_le16(smb_buffer->Command);
	cifs_dbg(FYI, "For smb_command %d\n", smb_buffer->Command);
	/* easier to use jiffies */
	/* when mid allocated can be before when sent */
	temp->when_alloc = jiffies;

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
		release_mid(server, mid);
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
	return map_and_check_smb_error(server, mid, log_error);
}

struct mid_q_entry *
cifs_setup_request(struct cifs_ses *ses, struct TCP_Server_Info *server,
		   struct smb_rqst *rqst)
{
	int rc;
	struct smb_hdr *hdr = (struct smb_hdr *)rqst->rq_iov[0].iov_base;
	struct mid_q_entry *mid;

	rc = allocate_mid(ses, hdr, &mid);
	if (rc)
		return ERR_PTR(rc);
	rc = cifs_sign_rqst(rqst, server, &mid->sequence_number);
	if (rc) {
		delete_mid(server, mid);
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
		return smb_EIO1(smb_eio_trace_tx_too_long, in_len);
	if (ses == NULL) {
		cifs_dbg(VFS, "Null smb session\n");
		return smb_EIO(smb_eio_trace_null_pointers);
	}
	server = ses->server;
	if (server == NULL) {
		cifs_dbg(VFS, "Null tcp session\n");
		return smb_EIO(smb_eio_trace_null_pointers);
	}

	/* Ensure that we do not send more than 50 overlapping requests
	   to the same server. We may make this configurable later or
	   use ses->maxReq */

	if (in_len > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cifs_server_dbg(VFS, "Invalid length, greater than maximum frame, %d\n",
				in_len);
		return smb_EIO1(smb_eio_trace_tx_too_long, in_len);
	}

	rc = cifs_send_recv(xid, ses, ses->server,
			    &rqst, &resp_buf_type, flags, &resp_iov);
	if (rc < 0)
		goto out;

	if (out_buf) {
		*pbytes_returned = resp_iov.iov_len;
		if (resp_iov.iov_len)
			memcpy(out_buf, resp_iov.iov_base, resp_iov.iov_len);
	}

out:
	free_rsp_buf(resp_buf_type, resp_iov.iov_base);
	return rc;
}

/*
	return codes:
		0	not a transact2, or all data present
		>0	transact2 with that much data missing
		-EINVAL	invalid transact2
 */
static int
check2ndT2(char *buf)
{
	struct smb_hdr *pSMB = (struct smb_hdr *)buf;
	struct smb_t2_rsp *pSMBt;
	int remaining;
	__u16 total_data_size, data_in_this_rsp;

	if (pSMB->Command != SMB_COM_TRANSACTION2)
		return 0;

	/* check for plausible wct, bcc and t2 data and parm sizes */
	/* check for parm and data offset going beyond end of smb */
	if (pSMB->WordCount != 10) { /* coalesce_t2 depends on this */
		cifs_dbg(FYI, "Invalid transact2 word count\n");
		return -EINVAL;
	}

	pSMBt = (struct smb_t2_rsp *)pSMB;

	total_data_size = get_unaligned_le16(&pSMBt->t2_rsp.TotalDataCount);
	data_in_this_rsp = get_unaligned_le16(&pSMBt->t2_rsp.DataCount);

	if (total_data_size == data_in_this_rsp)
		return 0;
	else if (total_data_size < data_in_this_rsp) {
		cifs_dbg(FYI, "total data %d smaller than data in frame %d\n",
			 total_data_size, data_in_this_rsp);
		return -EINVAL;
	}

	remaining = total_data_size - data_in_this_rsp;

	cifs_dbg(FYI, "missing %d bytes from transact2, check next response\n",
		 remaining);
	if (total_data_size > CIFSMaxBufSize) {
		cifs_dbg(VFS, "TotalDataSize %d is over maximum buffer %d\n",
			 total_data_size, CIFSMaxBufSize);
		return -EINVAL;
	}
	return remaining;
}

static int
coalesce_t2(char *second_buf, struct smb_hdr *target_hdr, unsigned int *pdu_len)
{
	struct smb_t2_rsp *pSMBs = (struct smb_t2_rsp *)second_buf;
	struct smb_t2_rsp *pSMBt  = (struct smb_t2_rsp *)target_hdr;
	char *data_area_of_tgt;
	char *data_area_of_src;
	int remaining;
	unsigned int byte_count, total_in_tgt;
	__u16 tgt_total_cnt, src_total_cnt, total_in_src;

	src_total_cnt = get_unaligned_le16(&pSMBs->t2_rsp.TotalDataCount);
	tgt_total_cnt = get_unaligned_le16(&pSMBt->t2_rsp.TotalDataCount);

	if (tgt_total_cnt != src_total_cnt)
		cifs_dbg(FYI, "total data count of primary and secondary t2 differ source=%hu target=%hu\n",
			 src_total_cnt, tgt_total_cnt);

	total_in_tgt = get_unaligned_le16(&pSMBt->t2_rsp.DataCount);

	remaining = tgt_total_cnt - total_in_tgt;

	if (remaining < 0) {
		cifs_dbg(FYI, "Server sent too much data. tgt_total_cnt=%hu total_in_tgt=%u\n",
			 tgt_total_cnt, total_in_tgt);
		return -EPROTO;
	}

	if (remaining == 0) {
		/* nothing to do, ignore */
		cifs_dbg(FYI, "no more data remains\n");
		return 0;
	}

	total_in_src = get_unaligned_le16(&pSMBs->t2_rsp.DataCount);
	if (remaining < total_in_src)
		cifs_dbg(FYI, "transact2 2nd response contains too much data\n");

	/* find end of first SMB data area */
	data_area_of_tgt = (char *)&pSMBt->hdr.Protocol +
				get_unaligned_le16(&pSMBt->t2_rsp.DataOffset);

	/* validate target area */
	data_area_of_src = (char *)&pSMBs->hdr.Protocol +
				get_unaligned_le16(&pSMBs->t2_rsp.DataOffset);

	data_area_of_tgt += total_in_tgt;

	total_in_tgt += total_in_src;
	/* is the result too big for the field? */
	if (total_in_tgt > USHRT_MAX) {
		cifs_dbg(FYI, "coalesced DataCount too large (%u)\n",
			 total_in_tgt);
		return -EPROTO;
	}
	put_unaligned_le16(total_in_tgt, &pSMBt->t2_rsp.DataCount);

	/* fix up the BCC */
	byte_count = get_bcc(target_hdr);
	byte_count += total_in_src;
	/* is the result too big for the field? */
	if (byte_count > USHRT_MAX) {
		cifs_dbg(FYI, "coalesced BCC too large (%u)\n", byte_count);
		return -EPROTO;
	}
	put_bcc(byte_count, target_hdr);

	byte_count = *pdu_len;
	byte_count += total_in_src;
	/* don't allow buffer to overflow */
	if (byte_count > CIFSMaxBufSize + MAX_CIFS_HDR_SIZE) {
		cifs_dbg(FYI, "coalesced BCC exceeds buffer size (%u)\n",
			 byte_count);
		return -ENOBUFS;
	}
	*pdu_len = byte_count;

	/* copy second buffer into end of first buffer */
	memcpy(data_area_of_tgt, data_area_of_src, total_in_src);

	if (remaining != total_in_src) {
		/* more responses to go */
		cifs_dbg(FYI, "waiting for more secondary responses\n");
		return 1;
	}

	/* we are done */
	cifs_dbg(FYI, "found the last secondary response\n");
	return 0;
}

bool
cifs_check_trans2(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		  char *buf, int malformed)
{
	if (malformed)
		return false;
	if (check2ndT2(buf) <= 0)
		return false;
	mid->multiRsp = true;
	if (mid->resp_buf) {
		/* merge response - fix up 1st*/
		malformed = coalesce_t2(buf, mid->resp_buf, &mid->response_pdu_len);
		if (malformed > 0)
			return true;
		/* All parts received or packet is malformed. */
		mid->multiEnd = true;
		dequeue_mid(server, mid, malformed);
		return true;
	}
	if (!server->large_buf) {
		/*FIXME: switch to already allocated largebuf?*/
		cifs_dbg(VFS, "1st trans2 resp needs bigbuf\n");
	} else {
		/* Have first buffer */
		mid->resp_buf = buf;
		mid->large_buf = true;
		server->bigbuf = NULL;
	}
	return true;
}

static int
check_smb_hdr(struct smb_hdr *smb)
{
	/* does it have the right SMB "signature" ? */
	if (*(__le32 *) smb->Protocol != SMB1_PROTO_NUMBER) {
		cifs_dbg(VFS, "Bad protocol string signature header 0x%x\n",
			 *(unsigned int *)smb->Protocol);
		return 1;
	}

	/* if it's a response then accept */
	if (smb->Flags & SMBFLG_RESPONSE)
		return 0;

	/* only one valid case where server sends us request */
	if (smb->Command == SMB_COM_LOCKING_ANDX)
		return 0;

	/*
	 * Windows NT server returns error resposne (e.g. STATUS_DELETE_PENDING
	 * or STATUS_OBJECT_NAME_NOT_FOUND or ERRDOS/ERRbadfile or any other)
	 * for some TRANS2 requests without the RESPONSE flag set in header.
	 */
	if (smb->Command == SMB_COM_TRANSACTION2 && smb->Status.CifsError != 0)
		return 0;

	cifs_dbg(VFS, "Server sent request, not response. mid=%u\n",
		 get_mid(smb));
	return 1;
}

int
checkSMB(char *buf, unsigned int pdu_len, unsigned int total_read,
	 struct TCP_Server_Info *server)
{
	struct smb_hdr *smb = (struct smb_hdr *)buf;
	__u32 rfclen = pdu_len;
	__u32 clc_len;  /* calculated length */
	cifs_dbg(FYI, "checkSMB Length: 0x%x, smb_buf_length: 0x%x\n",
		 total_read, rfclen);

	/* is this frame too small to even get to a BCC? */
	if (total_read < 2 + sizeof(struct smb_hdr)) {
		if ((total_read >= sizeof(struct smb_hdr) - 1)
			    && (smb->Status.CifsError != 0)) {
			/* it's an error return */
			smb->WordCount = 0;
			/* some error cases do not return wct and bcc */
			return 0;
		} else if ((total_read == sizeof(struct smb_hdr) + 1) &&
				(smb->WordCount == 0)) {
			char *tmp = (char *)smb;
			/* Need to work around a bug in two servers here */
			/* First, check if the part of bcc they sent was zero */
			if (tmp[sizeof(struct smb_hdr)] == 0) {
				/* some servers return only half of bcc
				 * on simple responses (wct, bcc both zero)
				 * in particular have seen this on
				 * ulogoffX and FindClose. This leaves
				 * one byte of bcc potentially uninitialized
				 */
				/* zero rest of bcc */
				tmp[sizeof(struct smb_hdr)+1] = 0;
				return 0;
			}
			cifs_dbg(VFS, "rcvd invalid byte count (bcc)\n");
			return smb_EIO1(smb_eio_trace_rx_inv_bcc, tmp[sizeof(struct smb_hdr)]);
		} else {
			cifs_dbg(VFS, "Length less than smb header size\n");
			return smb_EIO2(smb_eio_trace_rx_too_short,
					total_read, smb->WordCount);
		}
	} else if (total_read < sizeof(*smb) + 2 * smb->WordCount) {
		cifs_dbg(VFS, "%s: can't read BCC due to invalid WordCount(%u)\n",
			 __func__, smb->WordCount);
		return smb_EIO2(smb_eio_trace_rx_check_rsp,
				total_read, 2 + sizeof(struct smb_hdr));
	}

	/* otherwise, there is enough to get to the BCC */
	if (check_smb_hdr(smb))
		return smb_EIO1(smb_eio_trace_rx_rfc1002_magic, *(u32 *)smb->Protocol);
	clc_len = smbCalcSize(smb);

	if (rfclen != total_read) {
		cifs_dbg(VFS, "Length read does not match RFC1001 length %d/%d\n",
			 rfclen, total_read);
		return smb_EIO2(smb_eio_trace_rx_check_rsp,
				total_read, rfclen);
	}

	if (rfclen != clc_len) {
		__u16 mid = get_mid(smb);
		/* check if bcc wrapped around for large read responses */
		if ((rfclen > 64 * 1024) && (rfclen > clc_len)) {
			/* check if lengths match mod 64K */
			if (((rfclen) & 0xFFFF) == (clc_len & 0xFFFF))
				return 0; /* bcc wrapped */
		}
		cifs_dbg(FYI, "Calculated size %u vs length %u mismatch for mid=%u\n",
			 clc_len, rfclen, mid);

		if (rfclen < clc_len) {
			cifs_dbg(VFS, "RFC1001 size %u smaller than SMB for mid=%u\n",
				 rfclen, mid);
			return smb_EIO2(smb_eio_trace_rx_calc_len_too_big,
					rfclen, clc_len);
		} else if (rfclen > clc_len + 512) {
			/*
			 * Some servers (Windows XP in particular) send more
			 * data than the lengths in the SMB packet would
			 * indicate on certain calls (byte range locks and
			 * trans2 find first calls in particular). While the
			 * client can handle such a frame by ignoring the
			 * trailing data, we choose limit the amount of extra
			 * data to 512 bytes.
			 */
			cifs_dbg(VFS, "RFC1001 size %u more than 512 bytes larger than SMB for mid=%u\n",
				 rfclen, mid);
			return smb_EIO2(smb_eio_trace_rx_overlong,
					rfclen, clc_len + 512);
		}
	}
	return 0;
}
