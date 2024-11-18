// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/ethtool.h>
#include <linux/falloc.h>
#include <linux/mount.h>
#include <linux/filelock.h>

#include "glob.h"
#include "smbfsctl.h"
#include "oplock.h"
#include "smbacl.h"

#include "auth.h"
#include "asn1.h"
#include "connection.h"
#include "transport_ipc.h"
#include "transport_rdma.h"
#include "vfs.h"
#include "vfs_cache.h"
#include "misc.h"

#include "server.h"
#include "smb_common.h"
#include "../common/smb2status.h"
#include "ksmbd_work.h"
#include "mgmt/user_config.h"
#include "mgmt/share_config.h"
#include "mgmt/tree_connect.h"
#include "mgmt/user_session.h"
#include "mgmt/ksmbd_ida.h"
#include "ndr.h"

static void __wbuf(struct ksmbd_work *work, void **req, void **rsp)
{
	if (work->next_smb2_rcv_hdr_off) {
		*req = ksmbd_req_buf_next(work);
		*rsp = ksmbd_resp_buf_next(work);
	} else {
		*req = smb2_get_msg(work->request_buf);
		*rsp = smb2_get_msg(work->response_buf);
	}
}

#define WORK_BUFFERS(w, rq, rs)	__wbuf((w), (void **)&(rq), (void **)&(rs))

/**
 * check_session_id() - check for valid session id in smb header
 * @conn:	connection instance
 * @id:		session id from smb header
 *
 * Return:      1 if valid session id, otherwise 0
 */
static inline bool check_session_id(struct ksmbd_conn *conn, u64 id)
{
	struct ksmbd_session *sess;

	if (id == 0 || id == -1)
		return false;

	sess = ksmbd_session_lookup_all(conn, id);
	if (sess)
		return true;
	pr_err("Invalid user session id: %llu\n", id);
	return false;
}

struct channel *lookup_chann_list(struct ksmbd_session *sess, struct ksmbd_conn *conn)
{
	return xa_load(&sess->ksmbd_chann_list, (long)conn);
}

/**
 * smb2_get_ksmbd_tcon() - get tree connection information using a tree id.
 * @work:	smb work
 *
 * Return:	0 if there is a tree connection matched or these are
 *		skipable commands, otherwise error
 */
int smb2_get_ksmbd_tcon(struct ksmbd_work *work)
{
	struct smb2_hdr *req_hdr = ksmbd_req_buf_next(work);
	unsigned int cmd = le16_to_cpu(req_hdr->Command);
	unsigned int tree_id;

	if (cmd == SMB2_TREE_CONNECT_HE ||
	    cmd ==  SMB2_CANCEL_HE ||
	    cmd ==  SMB2_LOGOFF_HE) {
		ksmbd_debug(SMB, "skip to check tree connect request\n");
		return 0;
	}

	if (xa_empty(&work->sess->tree_conns)) {
		ksmbd_debug(SMB, "NO tree connected\n");
		return -ENOENT;
	}

	tree_id = le32_to_cpu(req_hdr->Id.SyncId.TreeId);

	/*
	 * If request is not the first in Compound request,
	 * Just validate tree id in header with work->tcon->id.
	 */
	if (work->next_smb2_rcv_hdr_off) {
		if (!work->tcon) {
			pr_err("The first operation in the compound does not have tcon\n");
			return -EINVAL;
		}
		if (tree_id != UINT_MAX && work->tcon->id != tree_id) {
			pr_err("tree id(%u) is different with id(%u) in first operation\n",
					tree_id, work->tcon->id);
			return -EINVAL;
		}
		return 1;
	}

	work->tcon = ksmbd_tree_conn_lookup(work->sess, tree_id);
	if (!work->tcon) {
		pr_err("Invalid tid %d\n", tree_id);
		return -ENOENT;
	}

	return 1;
}

/**
 * smb2_set_err_rsp() - set error response code on smb response
 * @work:	smb work containing response buffer
 */
void smb2_set_err_rsp(struct ksmbd_work *work)
{
	struct smb2_err_rsp *err_rsp;

	if (work->next_smb2_rcv_hdr_off)
		err_rsp = ksmbd_resp_buf_next(work);
	else
		err_rsp = smb2_get_msg(work->response_buf);

	if (err_rsp->hdr.Status != STATUS_STOPPED_ON_SYMLINK) {
		int err;

		err_rsp->StructureSize = SMB2_ERROR_STRUCTURE_SIZE2_LE;
		err_rsp->ErrorContextCount = 0;
		err_rsp->Reserved = 0;
		err_rsp->ByteCount = 0;
		err_rsp->ErrorData[0] = 0;
		err = ksmbd_iov_pin_rsp(work, (void *)err_rsp,
					__SMB2_HEADER_STRUCTURE_SIZE +
						SMB2_ERROR_STRUCTURE_SIZE2);
		if (err)
			work->send_no_response = 1;
	}
}

/**
 * is_smb2_neg_cmd() - is it smb2 negotiation command
 * @work:	smb work containing smb header
 *
 * Return:      true if smb2 negotiation command, otherwise false
 */
bool is_smb2_neg_cmd(struct ksmbd_work *work)
{
	struct smb2_hdr *hdr = smb2_get_msg(work->request_buf);

	/* is it SMB2 header ? */
	if (hdr->ProtocolId != SMB2_PROTO_NUMBER)
		return false;

	/* make sure it is request not response message */
	if (hdr->Flags & SMB2_FLAGS_SERVER_TO_REDIR)
		return false;

	if (hdr->Command != SMB2_NEGOTIATE)
		return false;

	return true;
}

/**
 * is_smb2_rsp() - is it smb2 response
 * @work:	smb work containing smb response buffer
 *
 * Return:      true if smb2 response, otherwise false
 */
bool is_smb2_rsp(struct ksmbd_work *work)
{
	struct smb2_hdr *hdr = smb2_get_msg(work->response_buf);

	/* is it SMB2 header ? */
	if (hdr->ProtocolId != SMB2_PROTO_NUMBER)
		return false;

	/* make sure it is response not request message */
	if (!(hdr->Flags & SMB2_FLAGS_SERVER_TO_REDIR))
		return false;

	return true;
}

/**
 * get_smb2_cmd_val() - get smb command code from smb header
 * @work:	smb work containing smb request buffer
 *
 * Return:      smb2 request command value
 */
u16 get_smb2_cmd_val(struct ksmbd_work *work)
{
	struct smb2_hdr *rcv_hdr;

	if (work->next_smb2_rcv_hdr_off)
		rcv_hdr = ksmbd_req_buf_next(work);
	else
		rcv_hdr = smb2_get_msg(work->request_buf);
	return le16_to_cpu(rcv_hdr->Command);
}

/**
 * set_smb2_rsp_status() - set error response code on smb2 header
 * @work:	smb work containing response buffer
 * @err:	error response code
 */
void set_smb2_rsp_status(struct ksmbd_work *work, __le32 err)
{
	struct smb2_hdr *rsp_hdr;

	rsp_hdr = smb2_get_msg(work->response_buf);
	rsp_hdr->Status = err;

	work->iov_idx = 0;
	work->iov_cnt = 0;
	work->next_smb2_rcv_hdr_off = 0;
	smb2_set_err_rsp(work);
}

/**
 * init_smb2_neg_rsp() - initialize smb2 response for negotiate command
 * @work:	smb work containing smb request buffer
 *
 * smb2 negotiate response is sent in reply of smb1 negotiate command for
 * dialect auto-negotiation.
 */
int init_smb2_neg_rsp(struct ksmbd_work *work)
{
	struct smb2_hdr *rsp_hdr;
	struct smb2_negotiate_rsp *rsp;
	struct ksmbd_conn *conn = work->conn;
	int err;

	rsp_hdr = smb2_get_msg(work->response_buf);
	memset(rsp_hdr, 0, sizeof(struct smb2_hdr) + 2);
	rsp_hdr->ProtocolId = SMB2_PROTO_NUMBER;
	rsp_hdr->StructureSize = SMB2_HEADER_STRUCTURE_SIZE;
	rsp_hdr->CreditRequest = cpu_to_le16(2);
	rsp_hdr->Command = SMB2_NEGOTIATE;
	rsp_hdr->Flags = (SMB2_FLAGS_SERVER_TO_REDIR);
	rsp_hdr->NextCommand = 0;
	rsp_hdr->MessageId = 0;
	rsp_hdr->Id.SyncId.ProcessId = 0;
	rsp_hdr->Id.SyncId.TreeId = 0;
	rsp_hdr->SessionId = 0;
	memset(rsp_hdr->Signature, 0, 16);

	rsp = smb2_get_msg(work->response_buf);

	WARN_ON(ksmbd_conn_good(conn));

	rsp->StructureSize = cpu_to_le16(65);
	ksmbd_debug(SMB, "conn->dialect 0x%x\n", conn->dialect);
	rsp->DialectRevision = cpu_to_le16(conn->dialect);
	/* Not setting conn guid rsp->ServerGUID, as it
	 * not used by client for identifying connection
	 */
	rsp->Capabilities = cpu_to_le32(conn->vals->capabilities);
	/* Default Max Message Size till SMB2.0, 64K*/
	rsp->MaxTransactSize = cpu_to_le32(conn->vals->max_trans_size);
	rsp->MaxReadSize = cpu_to_le32(conn->vals->max_read_size);
	rsp->MaxWriteSize = cpu_to_le32(conn->vals->max_write_size);

	rsp->SystemTime = cpu_to_le64(ksmbd_systime());
	rsp->ServerStartTime = 0;

	rsp->SecurityBufferOffset = cpu_to_le16(128);
	rsp->SecurityBufferLength = cpu_to_le16(AUTH_GSS_LENGTH);
	ksmbd_copy_gss_neg_header((char *)(&rsp->hdr) +
		le16_to_cpu(rsp->SecurityBufferOffset));
	rsp->SecurityMode = SMB2_NEGOTIATE_SIGNING_ENABLED_LE;
	if (server_conf.signing == KSMBD_CONFIG_OPT_MANDATORY)
		rsp->SecurityMode |= SMB2_NEGOTIATE_SIGNING_REQUIRED_LE;
	err = ksmbd_iov_pin_rsp(work, rsp,
				sizeof(struct smb2_negotiate_rsp) + AUTH_GSS_LENGTH);
	if (err)
		return err;
	conn->use_spnego = true;

	ksmbd_conn_set_need_negotiate(conn);
	return 0;
}

/**
 * smb2_set_rsp_credits() - set number of credits in response buffer
 * @work:	smb work containing smb response buffer
 */
int smb2_set_rsp_credits(struct ksmbd_work *work)
{
	struct smb2_hdr *req_hdr = ksmbd_req_buf_next(work);
	struct smb2_hdr *hdr = ksmbd_resp_buf_next(work);
	struct ksmbd_conn *conn = work->conn;
	unsigned short credits_requested, aux_max;
	unsigned short credit_charge, credits_granted = 0;

	if (work->send_no_response)
		return 0;

	hdr->CreditCharge = req_hdr->CreditCharge;

	if (conn->total_credits > conn->vals->max_credits) {
		hdr->CreditRequest = 0;
		pr_err("Total credits overflow: %d\n", conn->total_credits);
		return -EINVAL;
	}

	credit_charge = max_t(unsigned short,
			      le16_to_cpu(req_hdr->CreditCharge), 1);
	if (credit_charge > conn->total_credits) {
		ksmbd_debug(SMB, "Insufficient credits granted, given: %u, granted: %u\n",
			    credit_charge, conn->total_credits);
		return -EINVAL;
	}

	conn->total_credits -= credit_charge;
	conn->outstanding_credits -= credit_charge;
	credits_requested = max_t(unsigned short,
				  le16_to_cpu(req_hdr->CreditRequest), 1);

	/* according to smb2.credits smbtorture, Windows server
	 * 2016 or later grant up to 8192 credits at once.
	 *
	 * TODO: Need to adjuct CreditRequest value according to
	 * current cpu load
	 */
	if (hdr->Command == SMB2_NEGOTIATE)
		aux_max = 1;
	else
		aux_max = conn->vals->max_credits - conn->total_credits;
	credits_granted = min_t(unsigned short, credits_requested, aux_max);

	conn->total_credits += credits_granted;
	work->credits_granted += credits_granted;

	if (!req_hdr->NextCommand) {
		/* Update CreditRequest in last request */
		hdr->CreditRequest = cpu_to_le16(work->credits_granted);
	}
	ksmbd_debug(SMB,
		    "credits: requested[%d] granted[%d] total_granted[%d]\n",
		    credits_requested, credits_granted,
		    conn->total_credits);
	return 0;
}

/**
 * init_chained_smb2_rsp() - initialize smb2 chained response
 * @work:	smb work containing smb response buffer
 */
static void init_chained_smb2_rsp(struct ksmbd_work *work)
{
	struct smb2_hdr *req = ksmbd_req_buf_next(work);
	struct smb2_hdr *rsp = ksmbd_resp_buf_next(work);
	struct smb2_hdr *rsp_hdr;
	struct smb2_hdr *rcv_hdr;
	int next_hdr_offset = 0;
	int len, new_len;

	/* Len of this response = updated RFC len - offset of previous cmd
	 * in the compound rsp
	 */

	/* Storing the current local FID which may be needed by subsequent
	 * command in the compound request
	 */
	if (req->Command == SMB2_CREATE && rsp->Status == STATUS_SUCCESS) {
		work->compound_fid = ((struct smb2_create_rsp *)rsp)->VolatileFileId;
		work->compound_pfid = ((struct smb2_create_rsp *)rsp)->PersistentFileId;
		work->compound_sid = le64_to_cpu(rsp->SessionId);
	}

	len = get_rfc1002_len(work->response_buf) - work->next_smb2_rsp_hdr_off;
	next_hdr_offset = le32_to_cpu(req->NextCommand);

	new_len = ALIGN(len, 8);
	work->iov[work->iov_idx].iov_len += (new_len - len);
	inc_rfc1001_len(work->response_buf, new_len - len);
	rsp->NextCommand = cpu_to_le32(new_len);

	work->next_smb2_rcv_hdr_off += next_hdr_offset;
	work->curr_smb2_rsp_hdr_off = work->next_smb2_rsp_hdr_off;
	work->next_smb2_rsp_hdr_off += new_len;
	ksmbd_debug(SMB,
		    "Compound req new_len = %d rcv off = %d rsp off = %d\n",
		    new_len, work->next_smb2_rcv_hdr_off,
		    work->next_smb2_rsp_hdr_off);

	rsp_hdr = ksmbd_resp_buf_next(work);
	rcv_hdr = ksmbd_req_buf_next(work);

	if (!(rcv_hdr->Flags & SMB2_FLAGS_RELATED_OPERATIONS)) {
		ksmbd_debug(SMB, "related flag should be set\n");
		work->compound_fid = KSMBD_NO_FID;
		work->compound_pfid = KSMBD_NO_FID;
	}
	memset((char *)rsp_hdr, 0, sizeof(struct smb2_hdr) + 2);
	rsp_hdr->ProtocolId = SMB2_PROTO_NUMBER;
	rsp_hdr->StructureSize = SMB2_HEADER_STRUCTURE_SIZE;
	rsp_hdr->Command = rcv_hdr->Command;

	/*
	 * Message is response. We don't grant oplock yet.
	 */
	rsp_hdr->Flags = (SMB2_FLAGS_SERVER_TO_REDIR |
				SMB2_FLAGS_RELATED_OPERATIONS);
	rsp_hdr->NextCommand = 0;
	rsp_hdr->MessageId = rcv_hdr->MessageId;
	rsp_hdr->Id.SyncId.ProcessId = rcv_hdr->Id.SyncId.ProcessId;
	rsp_hdr->Id.SyncId.TreeId = rcv_hdr->Id.SyncId.TreeId;
	rsp_hdr->SessionId = rcv_hdr->SessionId;
	memcpy(rsp_hdr->Signature, rcv_hdr->Signature, 16);
}

/**
 * is_chained_smb2_message() - check for chained command
 * @work:	smb work containing smb request buffer
 *
 * Return:      true if chained request, otherwise false
 */
bool is_chained_smb2_message(struct ksmbd_work *work)
{
	struct smb2_hdr *hdr = smb2_get_msg(work->request_buf);
	unsigned int len, next_cmd;

	if (hdr->ProtocolId != SMB2_PROTO_NUMBER)
		return false;

	hdr = ksmbd_req_buf_next(work);
	next_cmd = le32_to_cpu(hdr->NextCommand);
	if (next_cmd > 0) {
		if ((u64)work->next_smb2_rcv_hdr_off + next_cmd +
			__SMB2_HEADER_STRUCTURE_SIZE >
		    get_rfc1002_len(work->request_buf)) {
			pr_err("next command(%u) offset exceeds smb msg size\n",
			       next_cmd);
			return false;
		}

		if ((u64)get_rfc1002_len(work->response_buf) + MAX_CIFS_SMALL_BUFFER_SIZE >
		    work->response_sz) {
			pr_err("next response offset exceeds response buffer size\n");
			return false;
		}

		ksmbd_debug(SMB, "got SMB2 chained command\n");
		init_chained_smb2_rsp(work);
		return true;
	} else if (work->next_smb2_rcv_hdr_off) {
		/*
		 * This is last request in chained command,
		 * align response to 8 byte
		 */
		len = ALIGN(get_rfc1002_len(work->response_buf), 8);
		len = len - get_rfc1002_len(work->response_buf);
		if (len) {
			ksmbd_debug(SMB, "padding len %u\n", len);
			work->iov[work->iov_idx].iov_len += len;
			inc_rfc1001_len(work->response_buf, len);
		}
		work->curr_smb2_rsp_hdr_off = work->next_smb2_rsp_hdr_off;
	}
	return false;
}

/**
 * init_smb2_rsp_hdr() - initialize smb2 response
 * @work:	smb work containing smb request buffer
 *
 * Return:      0
 */
int init_smb2_rsp_hdr(struct ksmbd_work *work)
{
	struct smb2_hdr *rsp_hdr = smb2_get_msg(work->response_buf);
	struct smb2_hdr *rcv_hdr = smb2_get_msg(work->request_buf);

	memset(rsp_hdr, 0, sizeof(struct smb2_hdr) + 2);
	rsp_hdr->ProtocolId = rcv_hdr->ProtocolId;
	rsp_hdr->StructureSize = SMB2_HEADER_STRUCTURE_SIZE;
	rsp_hdr->Command = rcv_hdr->Command;

	/*
	 * Message is response. We don't grant oplock yet.
	 */
	rsp_hdr->Flags = (SMB2_FLAGS_SERVER_TO_REDIR);
	rsp_hdr->NextCommand = 0;
	rsp_hdr->MessageId = rcv_hdr->MessageId;
	rsp_hdr->Id.SyncId.ProcessId = rcv_hdr->Id.SyncId.ProcessId;
	rsp_hdr->Id.SyncId.TreeId = rcv_hdr->Id.SyncId.TreeId;
	rsp_hdr->SessionId = rcv_hdr->SessionId;
	memcpy(rsp_hdr->Signature, rcv_hdr->Signature, 16);

	return 0;
}

/**
 * smb2_allocate_rsp_buf() - allocate smb2 response buffer
 * @work:	smb work containing smb request buffer
 *
 * Return:      0 on success, otherwise error
 */
int smb2_allocate_rsp_buf(struct ksmbd_work *work)
{
	struct smb2_hdr *hdr = smb2_get_msg(work->request_buf);
	size_t small_sz = MAX_CIFS_SMALL_BUFFER_SIZE;
	size_t large_sz = small_sz + work->conn->vals->max_trans_size;
	size_t sz = small_sz;
	int cmd = le16_to_cpu(hdr->Command);

	if (cmd == SMB2_IOCTL_HE || cmd == SMB2_QUERY_DIRECTORY_HE)
		sz = large_sz;

	if (cmd == SMB2_QUERY_INFO_HE) {
		struct smb2_query_info_req *req;

		if (get_rfc1002_len(work->request_buf) <
		    offsetof(struct smb2_query_info_req, OutputBufferLength))
			return -EINVAL;

		req = smb2_get_msg(work->request_buf);
		if ((req->InfoType == SMB2_O_INFO_FILE &&
		     (req->FileInfoClass == FILE_FULL_EA_INFORMATION ||
		     req->FileInfoClass == FILE_ALL_INFORMATION)) ||
		    req->InfoType == SMB2_O_INFO_SECURITY)
			sz = large_sz;
	}

	/* allocate large response buf for chained commands */
	if (le32_to_cpu(hdr->NextCommand) > 0)
		sz = large_sz;

	work->response_buf = kvzalloc(sz, GFP_KERNEL);
	if (!work->response_buf)
		return -ENOMEM;

	work->response_sz = sz;
	return 0;
}

/**
 * smb2_check_user_session() - check for valid session for a user
 * @work:	smb work containing smb request buffer
 *
 * Return:      0 on success, otherwise error
 */
int smb2_check_user_session(struct ksmbd_work *work)
{
	struct smb2_hdr *req_hdr = ksmbd_req_buf_next(work);
	struct ksmbd_conn *conn = work->conn;
	unsigned int cmd = le16_to_cpu(req_hdr->Command);
	unsigned long long sess_id;

	/*
	 * SMB2_ECHO, SMB2_NEGOTIATE, SMB2_SESSION_SETUP command do not
	 * require a session id, so no need to validate user session's for
	 * these commands.
	 */
	if (cmd == SMB2_ECHO_HE || cmd == SMB2_NEGOTIATE_HE ||
	    cmd == SMB2_SESSION_SETUP_HE)
		return 0;

	if (!ksmbd_conn_good(conn))
		return -EIO;

	sess_id = le64_to_cpu(req_hdr->SessionId);

	/*
	 * If request is not the first in Compound request,
	 * Just validate session id in header with work->sess->id.
	 */
	if (work->next_smb2_rcv_hdr_off) {
		if (!work->sess) {
			pr_err("The first operation in the compound does not have sess\n");
			return -EINVAL;
		}
		if (sess_id != ULLONG_MAX && work->sess->id != sess_id) {
			pr_err("session id(%llu) is different with the first operation(%lld)\n",
					sess_id, work->sess->id);
			return -EINVAL;
		}
		return 1;
	}

	/* Check for validity of user session */
	work->sess = ksmbd_session_lookup_all(conn, sess_id);
	if (work->sess) {
		ksmbd_user_session_get(work->sess);
		return 1;
	}
	ksmbd_debug(SMB, "Invalid user session, Uid %llu\n", sess_id);
	return -ENOENT;
}

/**
 * smb2_get_name() - get filename string from on the wire smb format
 * @src:	source buffer
 * @maxlen:	maxlen of source string
 * @local_nls:	nls_table pointer
 *
 * Return:      matching converted filename on success, otherwise error ptr
 */
static char *
smb2_get_name(const char *src, const int maxlen, struct nls_table *local_nls)
{
	char *name;

	name = smb_strndup_from_utf16(src, maxlen, 1, local_nls);
	if (IS_ERR(name)) {
		pr_err("failed to get name %ld\n", PTR_ERR(name));
		return name;
	}

	if (*name == '\\') {
		pr_err("not allow directory name included leading slash\n");
		kfree(name);
		return ERR_PTR(-EINVAL);
	}

	ksmbd_conv_path_to_unix(name);
	ksmbd_strip_last_slash(name);
	return name;
}

int setup_async_work(struct ksmbd_work *work, void (*fn)(void **), void **arg)
{
	struct ksmbd_conn *conn = work->conn;
	int id;

	id = ksmbd_acquire_async_msg_id(&conn->async_ida);
	if (id < 0) {
		pr_err("Failed to alloc async message id\n");
		return id;
	}
	work->asynchronous = true;
	work->async_id = id;

	ksmbd_debug(SMB,
		    "Send interim Response to inform async request id : %d\n",
		    work->async_id);

	work->cancel_fn = fn;
	work->cancel_argv = arg;

	if (list_empty(&work->async_request_entry)) {
		spin_lock(&conn->request_lock);
		list_add_tail(&work->async_request_entry, &conn->async_requests);
		spin_unlock(&conn->request_lock);
	}

	return 0;
}

void release_async_work(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;

	spin_lock(&conn->request_lock);
	list_del_init(&work->async_request_entry);
	spin_unlock(&conn->request_lock);

	work->asynchronous = 0;
	work->cancel_fn = NULL;
	kfree(work->cancel_argv);
	work->cancel_argv = NULL;
	if (work->async_id) {
		ksmbd_release_id(&conn->async_ida, work->async_id);
		work->async_id = 0;
	}
}

void smb2_send_interim_resp(struct ksmbd_work *work, __le32 status)
{
	struct smb2_hdr *rsp_hdr;
	struct ksmbd_work *in_work = ksmbd_alloc_work_struct();

	if (allocate_interim_rsp_buf(in_work)) {
		pr_err("smb_allocate_rsp_buf failed!\n");
		ksmbd_free_work_struct(in_work);
		return;
	}

	in_work->conn = work->conn;
	memcpy(smb2_get_msg(in_work->response_buf), ksmbd_resp_buf_next(work),
	       __SMB2_HEADER_STRUCTURE_SIZE);

	rsp_hdr = smb2_get_msg(in_work->response_buf);
	rsp_hdr->Flags |= SMB2_FLAGS_ASYNC_COMMAND;
	rsp_hdr->Id.AsyncId = cpu_to_le64(work->async_id);
	smb2_set_err_rsp(in_work);
	rsp_hdr->Status = status;

	ksmbd_conn_write(in_work);
	ksmbd_free_work_struct(in_work);
}

static __le32 smb2_get_reparse_tag_special_file(umode_t mode)
{
	if (S_ISDIR(mode) || S_ISREG(mode))
		return 0;

	if (S_ISLNK(mode))
		return IO_REPARSE_TAG_LX_SYMLINK_LE;
	else if (S_ISFIFO(mode))
		return IO_REPARSE_TAG_LX_FIFO_LE;
	else if (S_ISSOCK(mode))
		return IO_REPARSE_TAG_AF_UNIX_LE;
	else if (S_ISCHR(mode))
		return IO_REPARSE_TAG_LX_CHR_LE;
	else if (S_ISBLK(mode))
		return IO_REPARSE_TAG_LX_BLK_LE;

	return 0;
}

/**
 * smb2_get_dos_mode() - get file mode in dos format from unix mode
 * @stat:	kstat containing file mode
 * @attribute:	attribute flags
 *
 * Return:      converted dos mode
 */
static int smb2_get_dos_mode(struct kstat *stat, int attribute)
{
	int attr = 0;

	if (S_ISDIR(stat->mode)) {
		attr = FILE_ATTRIBUTE_DIRECTORY |
			(attribute & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
	} else {
		attr = (attribute & 0x00005137) | FILE_ATTRIBUTE_ARCHIVE;
		attr &= ~(FILE_ATTRIBUTE_DIRECTORY);
		if (S_ISREG(stat->mode) && (server_conf.share_fake_fscaps &
				FILE_SUPPORTS_SPARSE_FILES))
			attr |= FILE_ATTRIBUTE_SPARSE_FILE;

		if (smb2_get_reparse_tag_special_file(stat->mode))
			attr |= FILE_ATTRIBUTE_REPARSE_POINT;
	}

	return attr;
}

static void build_preauth_ctxt(struct smb2_preauth_neg_context *pneg_ctxt,
			       __le16 hash_id)
{
	pneg_ctxt->ContextType = SMB2_PREAUTH_INTEGRITY_CAPABILITIES;
	pneg_ctxt->DataLength = cpu_to_le16(38);
	pneg_ctxt->HashAlgorithmCount = cpu_to_le16(1);
	pneg_ctxt->Reserved = cpu_to_le32(0);
	pneg_ctxt->SaltLength = cpu_to_le16(SMB311_SALT_SIZE);
	get_random_bytes(pneg_ctxt->Salt, SMB311_SALT_SIZE);
	pneg_ctxt->HashAlgorithms = hash_id;
}

static void build_encrypt_ctxt(struct smb2_encryption_neg_context *pneg_ctxt,
			       __le16 cipher_type)
{
	pneg_ctxt->ContextType = SMB2_ENCRYPTION_CAPABILITIES;
	pneg_ctxt->DataLength = cpu_to_le16(4);
	pneg_ctxt->Reserved = cpu_to_le32(0);
	pneg_ctxt->CipherCount = cpu_to_le16(1);
	pneg_ctxt->Ciphers[0] = cipher_type;
}

static void build_sign_cap_ctxt(struct smb2_signing_capabilities *pneg_ctxt,
				__le16 sign_algo)
{
	pneg_ctxt->ContextType = SMB2_SIGNING_CAPABILITIES;
	pneg_ctxt->DataLength =
		cpu_to_le16((sizeof(struct smb2_signing_capabilities) + 2)
			- sizeof(struct smb2_neg_context));
	pneg_ctxt->Reserved = cpu_to_le32(0);
	pneg_ctxt->SigningAlgorithmCount = cpu_to_le16(1);
	pneg_ctxt->SigningAlgorithms[0] = sign_algo;
}

static void build_posix_ctxt(struct smb2_posix_neg_context *pneg_ctxt)
{
	pneg_ctxt->ContextType = SMB2_POSIX_EXTENSIONS_AVAILABLE;
	pneg_ctxt->DataLength = cpu_to_le16(POSIX_CTXT_DATA_LEN);
	/* SMB2_CREATE_TAG_POSIX is "0x93AD25509CB411E7B42383DE968BCD7C" */
	pneg_ctxt->Name[0] = 0x93;
	pneg_ctxt->Name[1] = 0xAD;
	pneg_ctxt->Name[2] = 0x25;
	pneg_ctxt->Name[3] = 0x50;
	pneg_ctxt->Name[4] = 0x9C;
	pneg_ctxt->Name[5] = 0xB4;
	pneg_ctxt->Name[6] = 0x11;
	pneg_ctxt->Name[7] = 0xE7;
	pneg_ctxt->Name[8] = 0xB4;
	pneg_ctxt->Name[9] = 0x23;
	pneg_ctxt->Name[10] = 0x83;
	pneg_ctxt->Name[11] = 0xDE;
	pneg_ctxt->Name[12] = 0x96;
	pneg_ctxt->Name[13] = 0x8B;
	pneg_ctxt->Name[14] = 0xCD;
	pneg_ctxt->Name[15] = 0x7C;
}

static unsigned int assemble_neg_contexts(struct ksmbd_conn *conn,
				  struct smb2_negotiate_rsp *rsp)
{
	char * const pneg_ctxt = (char *)rsp +
			le32_to_cpu(rsp->NegotiateContextOffset);
	int neg_ctxt_cnt = 1;
	int ctxt_size;

	ksmbd_debug(SMB,
		    "assemble SMB2_PREAUTH_INTEGRITY_CAPABILITIES context\n");
	build_preauth_ctxt((struct smb2_preauth_neg_context *)pneg_ctxt,
			   conn->preauth_info->Preauth_HashId);
	ctxt_size = sizeof(struct smb2_preauth_neg_context);

	if (conn->cipher_type) {
		/* Round to 8 byte boundary */
		ctxt_size = round_up(ctxt_size, 8);
		ksmbd_debug(SMB,
			    "assemble SMB2_ENCRYPTION_CAPABILITIES context\n");
		build_encrypt_ctxt((struct smb2_encryption_neg_context *)
				   (pneg_ctxt + ctxt_size),
				   conn->cipher_type);
		neg_ctxt_cnt++;
		ctxt_size += sizeof(struct smb2_encryption_neg_context) + 2;
	}

	/* compression context not yet supported */
	WARN_ON(conn->compress_algorithm != SMB3_COMPRESS_NONE);

	if (conn->posix_ext_supported) {
		ctxt_size = round_up(ctxt_size, 8);
		ksmbd_debug(SMB,
			    "assemble SMB2_POSIX_EXTENSIONS_AVAILABLE context\n");
		build_posix_ctxt((struct smb2_posix_neg_context *)
				 (pneg_ctxt + ctxt_size));
		neg_ctxt_cnt++;
		ctxt_size += sizeof(struct smb2_posix_neg_context);
	}

	if (conn->signing_negotiated) {
		ctxt_size = round_up(ctxt_size, 8);
		ksmbd_debug(SMB,
			    "assemble SMB2_SIGNING_CAPABILITIES context\n");
		build_sign_cap_ctxt((struct smb2_signing_capabilities *)
				    (pneg_ctxt + ctxt_size),
				    conn->signing_algorithm);
		neg_ctxt_cnt++;
		ctxt_size += sizeof(struct smb2_signing_capabilities) + 2;
	}

	rsp->NegotiateContextCount = cpu_to_le16(neg_ctxt_cnt);
	return ctxt_size + AUTH_GSS_PADDING;
}

static __le32 decode_preauth_ctxt(struct ksmbd_conn *conn,
				  struct smb2_preauth_neg_context *pneg_ctxt,
				  int ctxt_len)
{
	/*
	 * sizeof(smb2_preauth_neg_context) assumes SMB311_SALT_SIZE Salt,
	 * which may not be present. Only check for used HashAlgorithms[1].
	 */
	if (ctxt_len <
	    sizeof(struct smb2_neg_context) + MIN_PREAUTH_CTXT_DATA_LEN)
		return STATUS_INVALID_PARAMETER;

	if (pneg_ctxt->HashAlgorithms != SMB2_PREAUTH_INTEGRITY_SHA512)
		return STATUS_NO_PREAUTH_INTEGRITY_HASH_OVERLAP;

	conn->preauth_info->Preauth_HashId = SMB2_PREAUTH_INTEGRITY_SHA512;
	return STATUS_SUCCESS;
}

static void decode_encrypt_ctxt(struct ksmbd_conn *conn,
				struct smb2_encryption_neg_context *pneg_ctxt,
				int ctxt_len)
{
	int cph_cnt;
	int i, cphs_size;

	if (sizeof(struct smb2_encryption_neg_context) > ctxt_len) {
		pr_err("Invalid SMB2_ENCRYPTION_CAPABILITIES context size\n");
		return;
	}

	conn->cipher_type = 0;

	cph_cnt = le16_to_cpu(pneg_ctxt->CipherCount);
	cphs_size = cph_cnt * sizeof(__le16);

	if (sizeof(struct smb2_encryption_neg_context) + cphs_size >
	    ctxt_len) {
		pr_err("Invalid cipher count(%d)\n", cph_cnt);
		return;
	}

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION_OFF)
		return;

	for (i = 0; i < cph_cnt; i++) {
		if (pneg_ctxt->Ciphers[i] == SMB2_ENCRYPTION_AES128_GCM ||
		    pneg_ctxt->Ciphers[i] == SMB2_ENCRYPTION_AES128_CCM ||
		    pneg_ctxt->Ciphers[i] == SMB2_ENCRYPTION_AES256_CCM ||
		    pneg_ctxt->Ciphers[i] == SMB2_ENCRYPTION_AES256_GCM) {
			ksmbd_debug(SMB, "Cipher ID = 0x%x\n",
				    pneg_ctxt->Ciphers[i]);
			conn->cipher_type = pneg_ctxt->Ciphers[i];
			break;
		}
	}
}

/**
 * smb3_encryption_negotiated() - checks if server and client agreed on enabling encryption
 * @conn:	smb connection
 *
 * Return:	true if connection should be encrypted, else false
 */
bool smb3_encryption_negotiated(struct ksmbd_conn *conn)
{
	if (!conn->ops->generate_encryptionkey)
		return false;

	/*
	 * SMB 3.0 and 3.0.2 dialects use the SMB2_GLOBAL_CAP_ENCRYPTION flag.
	 * SMB 3.1.1 uses the cipher_type field.
	 */
	return (conn->vals->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION) ||
	    conn->cipher_type;
}

static void decode_compress_ctxt(struct ksmbd_conn *conn,
				 struct smb2_compression_capabilities_context *pneg_ctxt)
{
	conn->compress_algorithm = SMB3_COMPRESS_NONE;
}

static void decode_sign_cap_ctxt(struct ksmbd_conn *conn,
				 struct smb2_signing_capabilities *pneg_ctxt,
				 int ctxt_len)
{
	int sign_algo_cnt;
	int i, sign_alos_size;

	if (sizeof(struct smb2_signing_capabilities) > ctxt_len) {
		pr_err("Invalid SMB2_SIGNING_CAPABILITIES context length\n");
		return;
	}

	conn->signing_negotiated = false;
	sign_algo_cnt = le16_to_cpu(pneg_ctxt->SigningAlgorithmCount);
	sign_alos_size = sign_algo_cnt * sizeof(__le16);

	if (sizeof(struct smb2_signing_capabilities) + sign_alos_size >
	    ctxt_len) {
		pr_err("Invalid signing algorithm count(%d)\n", sign_algo_cnt);
		return;
	}

	for (i = 0; i < sign_algo_cnt; i++) {
		if (pneg_ctxt->SigningAlgorithms[i] == SIGNING_ALG_HMAC_SHA256_LE ||
		    pneg_ctxt->SigningAlgorithms[i] == SIGNING_ALG_AES_CMAC_LE) {
			ksmbd_debug(SMB, "Signing Algorithm ID = 0x%x\n",
				    pneg_ctxt->SigningAlgorithms[i]);
			conn->signing_negotiated = true;
			conn->signing_algorithm =
				pneg_ctxt->SigningAlgorithms[i];
			break;
		}
	}
}

static __le32 deassemble_neg_contexts(struct ksmbd_conn *conn,
				      struct smb2_negotiate_req *req,
				      unsigned int len_of_smb)
{
	/* +4 is to account for the RFC1001 len field */
	struct smb2_neg_context *pctx = (struct smb2_neg_context *)req;
	int i = 0, len_of_ctxts;
	unsigned int offset = le32_to_cpu(req->NegotiateContextOffset);
	unsigned int neg_ctxt_cnt = le16_to_cpu(req->NegotiateContextCount);
	__le32 status = STATUS_INVALID_PARAMETER;

	ksmbd_debug(SMB, "decoding %d negotiate contexts\n", neg_ctxt_cnt);
	if (len_of_smb <= offset) {
		ksmbd_debug(SMB, "Invalid response: negotiate context offset\n");
		return status;
	}

	len_of_ctxts = len_of_smb - offset;

	while (i++ < neg_ctxt_cnt) {
		int clen, ctxt_len;

		if (len_of_ctxts < (int)sizeof(struct smb2_neg_context))
			break;

		pctx = (struct smb2_neg_context *)((char *)pctx + offset);
		clen = le16_to_cpu(pctx->DataLength);
		ctxt_len = clen + sizeof(struct smb2_neg_context);

		if (ctxt_len > len_of_ctxts)
			break;

		if (pctx->ContextType == SMB2_PREAUTH_INTEGRITY_CAPABILITIES) {
			ksmbd_debug(SMB,
				    "deassemble SMB2_PREAUTH_INTEGRITY_CAPABILITIES context\n");
			if (conn->preauth_info->Preauth_HashId)
				break;

			status = decode_preauth_ctxt(conn,
						     (struct smb2_preauth_neg_context *)pctx,
						     ctxt_len);
			if (status != STATUS_SUCCESS)
				break;
		} else if (pctx->ContextType == SMB2_ENCRYPTION_CAPABILITIES) {
			ksmbd_debug(SMB,
				    "deassemble SMB2_ENCRYPTION_CAPABILITIES context\n");
			if (conn->cipher_type)
				break;

			decode_encrypt_ctxt(conn,
					    (struct smb2_encryption_neg_context *)pctx,
					    ctxt_len);
		} else if (pctx->ContextType == SMB2_COMPRESSION_CAPABILITIES) {
			ksmbd_debug(SMB,
				    "deassemble SMB2_COMPRESSION_CAPABILITIES context\n");
			if (conn->compress_algorithm)
				break;

			decode_compress_ctxt(conn,
					     (struct smb2_compression_capabilities_context *)pctx);
		} else if (pctx->ContextType == SMB2_NETNAME_NEGOTIATE_CONTEXT_ID) {
			ksmbd_debug(SMB,
				    "deassemble SMB2_NETNAME_NEGOTIATE_CONTEXT_ID context\n");
		} else if (pctx->ContextType == SMB2_POSIX_EXTENSIONS_AVAILABLE) {
			ksmbd_debug(SMB,
				    "deassemble SMB2_POSIX_EXTENSIONS_AVAILABLE context\n");
			conn->posix_ext_supported = true;
		} else if (pctx->ContextType == SMB2_SIGNING_CAPABILITIES) {
			ksmbd_debug(SMB,
				    "deassemble SMB2_SIGNING_CAPABILITIES context\n");

			decode_sign_cap_ctxt(conn,
					     (struct smb2_signing_capabilities *)pctx,
					     ctxt_len);
		}

		/* offsets must be 8 byte aligned */
		offset = (ctxt_len + 7) & ~0x7;
		len_of_ctxts -= offset;
	}
	return status;
}

/**
 * smb2_handle_negotiate() - handler for smb2 negotiate command
 * @work:	smb work containing smb request buffer
 *
 * Return:      0
 */
int smb2_handle_negotiate(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_negotiate_req *req = smb2_get_msg(work->request_buf);
	struct smb2_negotiate_rsp *rsp = smb2_get_msg(work->response_buf);
	int rc = 0;
	unsigned int smb2_buf_len, smb2_neg_size, neg_ctxt_len = 0;
	__le32 status;

	ksmbd_debug(SMB, "Received negotiate request\n");
	conn->need_neg = false;
	if (ksmbd_conn_good(conn)) {
		pr_err("conn->tcp_status is already in CifsGood State\n");
		work->send_no_response = 1;
		return rc;
	}

	smb2_buf_len = get_rfc1002_len(work->request_buf);
	smb2_neg_size = offsetof(struct smb2_negotiate_req, Dialects);
	if (smb2_neg_size > smb2_buf_len) {
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		rc = -EINVAL;
		goto err_out;
	}

	if (req->DialectCount == 0) {
		pr_err("malformed packet\n");
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		rc = -EINVAL;
		goto err_out;
	}

	if (conn->dialect == SMB311_PROT_ID) {
		unsigned int nego_ctxt_off = le32_to_cpu(req->NegotiateContextOffset);

		if (smb2_buf_len < nego_ctxt_off) {
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			rc = -EINVAL;
			goto err_out;
		}

		if (smb2_neg_size > nego_ctxt_off) {
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			rc = -EINVAL;
			goto err_out;
		}

		if (smb2_neg_size + le16_to_cpu(req->DialectCount) * sizeof(__le16) >
		    nego_ctxt_off) {
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			rc = -EINVAL;
			goto err_out;
		}
	} else {
		if (smb2_neg_size + le16_to_cpu(req->DialectCount) * sizeof(__le16) >
		    smb2_buf_len) {
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			rc = -EINVAL;
			goto err_out;
		}
	}

	conn->cli_cap = le32_to_cpu(req->Capabilities);
	switch (conn->dialect) {
	case SMB311_PROT_ID:
		conn->preauth_info =
			kzalloc(sizeof(struct preauth_integrity_info),
				GFP_KERNEL);
		if (!conn->preauth_info) {
			rc = -ENOMEM;
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			goto err_out;
		}

		status = deassemble_neg_contexts(conn, req,
						 get_rfc1002_len(work->request_buf));
		if (status != STATUS_SUCCESS) {
			pr_err("deassemble_neg_contexts error(0x%x)\n",
			       status);
			rsp->hdr.Status = status;
			rc = -EINVAL;
			kfree(conn->preauth_info);
			conn->preauth_info = NULL;
			goto err_out;
		}

		rc = init_smb3_11_server(conn);
		if (rc < 0) {
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			kfree(conn->preauth_info);
			conn->preauth_info = NULL;
			goto err_out;
		}

		ksmbd_gen_preauth_integrity_hash(conn,
						 work->request_buf,
						 conn->preauth_info->Preauth_HashValue);
		rsp->NegotiateContextOffset =
				cpu_to_le32(OFFSET_OF_NEG_CONTEXT);
		neg_ctxt_len = assemble_neg_contexts(conn, rsp);
		break;
	case SMB302_PROT_ID:
		init_smb3_02_server(conn);
		break;
	case SMB30_PROT_ID:
		init_smb3_0_server(conn);
		break;
	case SMB21_PROT_ID:
		init_smb2_1_server(conn);
		break;
	case SMB2X_PROT_ID:
	case BAD_PROT_ID:
	default:
		ksmbd_debug(SMB, "Server dialect :0x%x not supported\n",
			    conn->dialect);
		rsp->hdr.Status = STATUS_NOT_SUPPORTED;
		rc = -EINVAL;
		goto err_out;
	}
	rsp->Capabilities = cpu_to_le32(conn->vals->capabilities);

	/* For stats */
	conn->connection_type = conn->dialect;

	rsp->MaxTransactSize = cpu_to_le32(conn->vals->max_trans_size);
	rsp->MaxReadSize = cpu_to_le32(conn->vals->max_read_size);
	rsp->MaxWriteSize = cpu_to_le32(conn->vals->max_write_size);

	memcpy(conn->ClientGUID, req->ClientGUID,
			SMB2_CLIENT_GUID_SIZE);
	conn->cli_sec_mode = le16_to_cpu(req->SecurityMode);

	rsp->StructureSize = cpu_to_le16(65);
	rsp->DialectRevision = cpu_to_le16(conn->dialect);
	/* Not setting conn guid rsp->ServerGUID, as it
	 * not used by client for identifying server
	 */
	memset(rsp->ServerGUID, 0, SMB2_CLIENT_GUID_SIZE);

	rsp->SystemTime = cpu_to_le64(ksmbd_systime());
	rsp->ServerStartTime = 0;
	ksmbd_debug(SMB, "negotiate context offset %d, count %d\n",
		    le32_to_cpu(rsp->NegotiateContextOffset),
		    le16_to_cpu(rsp->NegotiateContextCount));

	rsp->SecurityBufferOffset = cpu_to_le16(128);
	rsp->SecurityBufferLength = cpu_to_le16(AUTH_GSS_LENGTH);
	ksmbd_copy_gss_neg_header((char *)(&rsp->hdr) +
				  le16_to_cpu(rsp->SecurityBufferOffset));

	rsp->SecurityMode = SMB2_NEGOTIATE_SIGNING_ENABLED_LE;
	conn->use_spnego = true;

	if ((server_conf.signing == KSMBD_CONFIG_OPT_AUTO ||
	     server_conf.signing == KSMBD_CONFIG_OPT_DISABLED) &&
	    req->SecurityMode & SMB2_NEGOTIATE_SIGNING_REQUIRED_LE)
		conn->sign = true;
	else if (server_conf.signing == KSMBD_CONFIG_OPT_MANDATORY) {
		server_conf.enforced_signing = true;
		rsp->SecurityMode |= SMB2_NEGOTIATE_SIGNING_REQUIRED_LE;
		conn->sign = true;
	}

	conn->srv_sec_mode = le16_to_cpu(rsp->SecurityMode);
	ksmbd_conn_set_need_negotiate(conn);

err_out:
	if (rc)
		rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;

	if (!rc)
		rc = ksmbd_iov_pin_rsp(work, rsp,
				       sizeof(struct smb2_negotiate_rsp) +
					AUTH_GSS_LENGTH + neg_ctxt_len);
	if (rc < 0)
		smb2_set_err_rsp(work);
	return rc;
}

static int alloc_preauth_hash(struct ksmbd_session *sess,
			      struct ksmbd_conn *conn)
{
	if (sess->Preauth_HashValue)
		return 0;

	sess->Preauth_HashValue = kmemdup(conn->preauth_info->Preauth_HashValue,
					  PREAUTH_HASHVALUE_SIZE, GFP_KERNEL);
	if (!sess->Preauth_HashValue)
		return -ENOMEM;

	return 0;
}

static int generate_preauth_hash(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	u8 *preauth_hash;

	if (conn->dialect != SMB311_PROT_ID)
		return 0;

	if (conn->binding) {
		struct preauth_session *preauth_sess;

		preauth_sess = ksmbd_preauth_session_lookup(conn, sess->id);
		if (!preauth_sess) {
			preauth_sess = ksmbd_preauth_session_alloc(conn, sess->id);
			if (!preauth_sess)
				return -ENOMEM;
		}

		preauth_hash = preauth_sess->Preauth_HashValue;
	} else {
		if (!sess->Preauth_HashValue)
			if (alloc_preauth_hash(sess, conn))
				return -ENOMEM;
		preauth_hash = sess->Preauth_HashValue;
	}

	ksmbd_gen_preauth_integrity_hash(conn, work->request_buf, preauth_hash);
	return 0;
}

static int decode_negotiation_token(struct ksmbd_conn *conn,
				    struct negotiate_message *negblob,
				    size_t sz)
{
	if (!conn->use_spnego)
		return -EINVAL;

	if (ksmbd_decode_negTokenInit((char *)negblob, sz, conn)) {
		if (ksmbd_decode_negTokenTarg((char *)negblob, sz, conn)) {
			conn->auth_mechs |= KSMBD_AUTH_NTLMSSP;
			conn->preferred_auth_mech = KSMBD_AUTH_NTLMSSP;
			conn->use_spnego = false;
		}
	}
	return 0;
}

static int ntlm_negotiate(struct ksmbd_work *work,
			  struct negotiate_message *negblob,
			  size_t negblob_len, struct smb2_sess_setup_rsp *rsp)
{
	struct challenge_message *chgblob;
	unsigned char *spnego_blob = NULL;
	u16 spnego_blob_len;
	char *neg_blob;
	int sz, rc;

	ksmbd_debug(SMB, "negotiate phase\n");
	rc = ksmbd_decode_ntlmssp_neg_blob(negblob, negblob_len, work->conn);
	if (rc)
		return rc;

	sz = le16_to_cpu(rsp->SecurityBufferOffset);
	chgblob = (struct challenge_message *)rsp->Buffer;
	memset(chgblob, 0, sizeof(struct challenge_message));

	if (!work->conn->use_spnego) {
		sz = ksmbd_build_ntlmssp_challenge_blob(chgblob, work->conn);
		if (sz < 0)
			return -ENOMEM;

		rsp->SecurityBufferLength = cpu_to_le16(sz);
		return 0;
	}

	sz = sizeof(struct challenge_message);
	sz += (strlen(ksmbd_netbios_name()) * 2 + 1 + 4) * 6;

	neg_blob = kzalloc(sz, GFP_KERNEL);
	if (!neg_blob)
		return -ENOMEM;

	chgblob = (struct challenge_message *)neg_blob;
	sz = ksmbd_build_ntlmssp_challenge_blob(chgblob, work->conn);
	if (sz < 0) {
		rc = -ENOMEM;
		goto out;
	}

	rc = build_spnego_ntlmssp_neg_blob(&spnego_blob, &spnego_blob_len,
					   neg_blob, sz);
	if (rc) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(rsp->Buffer, spnego_blob, spnego_blob_len);
	rsp->SecurityBufferLength = cpu_to_le16(spnego_blob_len);

out:
	kfree(spnego_blob);
	kfree(neg_blob);
	return rc;
}

static struct authenticate_message *user_authblob(struct ksmbd_conn *conn,
						  struct smb2_sess_setup_req *req)
{
	int sz;

	if (conn->use_spnego && conn->mechToken)
		return (struct authenticate_message *)conn->mechToken;

	sz = le16_to_cpu(req->SecurityBufferOffset);
	return (struct authenticate_message *)((char *)&req->hdr.ProtocolId
					       + sz);
}

static struct ksmbd_user *session_user(struct ksmbd_conn *conn,
				       struct smb2_sess_setup_req *req)
{
	struct authenticate_message *authblob;
	struct ksmbd_user *user;
	char *name;
	unsigned int name_off, name_len, secbuf_len;

	if (conn->use_spnego && conn->mechToken)
		secbuf_len = conn->mechTokenLen;
	else
		secbuf_len = le16_to_cpu(req->SecurityBufferLength);
	if (secbuf_len < sizeof(struct authenticate_message)) {
		ksmbd_debug(SMB, "blob len %d too small\n", secbuf_len);
		return NULL;
	}
	authblob = user_authblob(conn, req);
	name_off = le32_to_cpu(authblob->UserName.BufferOffset);
	name_len = le16_to_cpu(authblob->UserName.Length);

	if (secbuf_len < (u64)name_off + name_len)
		return NULL;

	name = smb_strndup_from_utf16((const char *)authblob + name_off,
				      name_len,
				      true,
				      conn->local_nls);
	if (IS_ERR(name)) {
		pr_err("cannot allocate memory\n");
		return NULL;
	}

	ksmbd_debug(SMB, "session setup request for user %s\n", name);
	user = ksmbd_login_user(name);
	kfree(name);
	return user;
}

static int ntlm_authenticate(struct ksmbd_work *work,
			     struct smb2_sess_setup_req *req,
			     struct smb2_sess_setup_rsp *rsp)
{
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	struct channel *chann = NULL;
	struct ksmbd_user *user;
	u64 prev_id;
	int sz, rc;

	ksmbd_debug(SMB, "authenticate phase\n");
	if (conn->use_spnego) {
		unsigned char *spnego_blob;
		u16 spnego_blob_len;

		rc = build_spnego_ntlmssp_auth_blob(&spnego_blob,
						    &spnego_blob_len,
						    0);
		if (rc)
			return -ENOMEM;

		memcpy(rsp->Buffer, spnego_blob, spnego_blob_len);
		rsp->SecurityBufferLength = cpu_to_le16(spnego_blob_len);
		kfree(spnego_blob);
	}

	user = session_user(conn, req);
	if (!user) {
		ksmbd_debug(SMB, "Unknown user name or an error\n");
		return -EPERM;
	}

	/* Check for previous session */
	prev_id = le64_to_cpu(req->PreviousSessionId);
	if (prev_id && prev_id != sess->id)
		destroy_previous_session(conn, user, prev_id);

	if (sess->state == SMB2_SESSION_VALID) {
		/*
		 * Reuse session if anonymous try to connect
		 * on reauthetication.
		 */
		if (conn->binding == false && ksmbd_anonymous_user(user)) {
			ksmbd_free_user(user);
			return 0;
		}

		if (!ksmbd_compare_user(sess->user, user)) {
			ksmbd_free_user(user);
			return -EPERM;
		}
		ksmbd_free_user(user);
	} else {
		sess->user = user;
	}

	if (conn->binding == false && user_guest(sess->user)) {
		rsp->SessionFlags = SMB2_SESSION_FLAG_IS_GUEST_LE;
	} else {
		struct authenticate_message *authblob;

		authblob = user_authblob(conn, req);
		if (conn->use_spnego && conn->mechToken)
			sz = conn->mechTokenLen;
		else
			sz = le16_to_cpu(req->SecurityBufferLength);
		rc = ksmbd_decode_ntlmssp_auth_blob(authblob, sz, conn, sess);
		if (rc) {
			set_user_flag(sess->user, KSMBD_USER_FLAG_BAD_PASSWORD);
			ksmbd_debug(SMB, "authentication failed\n");
			return -EPERM;
		}
	}

	/*
	 * If session state is SMB2_SESSION_VALID, We can assume
	 * that it is reauthentication. And the user/password
	 * has been verified, so return it here.
	 */
	if (sess->state == SMB2_SESSION_VALID) {
		if (conn->binding)
			goto binding_session;
		return 0;
	}

	if ((rsp->SessionFlags != SMB2_SESSION_FLAG_IS_GUEST_LE &&
	     (conn->sign || server_conf.enforced_signing)) ||
	    (req->SecurityMode & SMB2_NEGOTIATE_SIGNING_REQUIRED))
		sess->sign = true;

	if (smb3_encryption_negotiated(conn) &&
			!(req->Flags & SMB2_SESSION_REQ_FLAG_BINDING)) {
		rc = conn->ops->generate_encryptionkey(conn, sess);
		if (rc) {
			ksmbd_debug(SMB,
					"SMB3 encryption key generation failed\n");
			return -EINVAL;
		}
		sess->enc = true;
		if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION)
			rsp->SessionFlags = SMB2_SESSION_FLAG_ENCRYPT_DATA_LE;
		/*
		 * signing is disable if encryption is enable
		 * on this session
		 */
		sess->sign = false;
	}

binding_session:
	if (conn->dialect >= SMB30_PROT_ID) {
		chann = lookup_chann_list(sess, conn);
		if (!chann) {
			chann = kmalloc(sizeof(struct channel), GFP_KERNEL);
			if (!chann)
				return -ENOMEM;

			chann->conn = conn;
			xa_store(&sess->ksmbd_chann_list, (long)conn, chann, GFP_KERNEL);
		}
	}

	if (conn->ops->generate_signingkey) {
		rc = conn->ops->generate_signingkey(sess, conn);
		if (rc) {
			ksmbd_debug(SMB, "SMB3 signing key generation failed\n");
			return -EINVAL;
		}
	}

	if (!ksmbd_conn_lookup_dialect(conn)) {
		pr_err("fail to verify the dialect\n");
		return -ENOENT;
	}
	return 0;
}

#ifdef CONFIG_SMB_SERVER_KERBEROS5
static int krb5_authenticate(struct ksmbd_work *work,
			     struct smb2_sess_setup_req *req,
			     struct smb2_sess_setup_rsp *rsp)
{
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	char *in_blob, *out_blob;
	struct channel *chann = NULL;
	u64 prev_sess_id;
	int in_len, out_len;
	int retval;

	in_blob = (char *)&req->hdr.ProtocolId +
		le16_to_cpu(req->SecurityBufferOffset);
	in_len = le16_to_cpu(req->SecurityBufferLength);
	out_blob = (char *)&rsp->hdr.ProtocolId +
		le16_to_cpu(rsp->SecurityBufferOffset);
	out_len = work->response_sz -
		(le16_to_cpu(rsp->SecurityBufferOffset) + 4);

	/* Check previous session */
	prev_sess_id = le64_to_cpu(req->PreviousSessionId);
	if (prev_sess_id && prev_sess_id != sess->id)
		destroy_previous_session(conn, sess->user, prev_sess_id);

	if (sess->state == SMB2_SESSION_VALID)
		ksmbd_free_user(sess->user);

	retval = ksmbd_krb5_authenticate(sess, in_blob, in_len,
					 out_blob, &out_len);
	if (retval) {
		ksmbd_debug(SMB, "krb5 authentication failed\n");
		return -EINVAL;
	}
	rsp->SecurityBufferLength = cpu_to_le16(out_len);

	if ((conn->sign || server_conf.enforced_signing) ||
	    (req->SecurityMode & SMB2_NEGOTIATE_SIGNING_REQUIRED))
		sess->sign = true;

	if (smb3_encryption_negotiated(conn)) {
		retval = conn->ops->generate_encryptionkey(conn, sess);
		if (retval) {
			ksmbd_debug(SMB,
				    "SMB3 encryption key generation failed\n");
			return -EINVAL;
		}
		sess->enc = true;
		if (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB2_ENCRYPTION)
			rsp->SessionFlags = SMB2_SESSION_FLAG_ENCRYPT_DATA_LE;
		sess->sign = false;
	}

	if (conn->dialect >= SMB30_PROT_ID) {
		chann = lookup_chann_list(sess, conn);
		if (!chann) {
			chann = kmalloc(sizeof(struct channel), GFP_KERNEL);
			if (!chann)
				return -ENOMEM;

			chann->conn = conn;
			xa_store(&sess->ksmbd_chann_list, (long)conn, chann, GFP_KERNEL);
		}
	}

	if (conn->ops->generate_signingkey) {
		retval = conn->ops->generate_signingkey(sess, conn);
		if (retval) {
			ksmbd_debug(SMB, "SMB3 signing key generation failed\n");
			return -EINVAL;
		}
	}

	if (!ksmbd_conn_lookup_dialect(conn)) {
		pr_err("fail to verify the dialect\n");
		return -ENOENT;
	}
	return 0;
}
#else
static int krb5_authenticate(struct ksmbd_work *work,
			     struct smb2_sess_setup_req *req,
			     struct smb2_sess_setup_rsp *rsp)
{
	return -EOPNOTSUPP;
}
#endif

int smb2_sess_setup(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_sess_setup_req *req;
	struct smb2_sess_setup_rsp *rsp;
	struct ksmbd_session *sess;
	struct negotiate_message *negblob;
	unsigned int negblob_len, negblob_off;
	int rc = 0;

	ksmbd_debug(SMB, "Received request for session setup\n");

	WORK_BUFFERS(work, req, rsp);

	rsp->StructureSize = cpu_to_le16(9);
	rsp->SessionFlags = 0;
	rsp->SecurityBufferOffset = cpu_to_le16(72);
	rsp->SecurityBufferLength = 0;

	ksmbd_conn_lock(conn);
	if (!req->hdr.SessionId) {
		sess = ksmbd_smb2_session_create();
		if (!sess) {
			rc = -ENOMEM;
			goto out_err;
		}
		rsp->hdr.SessionId = cpu_to_le64(sess->id);
		rc = ksmbd_session_register(conn, sess);
		if (rc)
			goto out_err;

		conn->binding = false;
	} else if (conn->dialect >= SMB30_PROT_ID &&
		   (server_conf.flags & KSMBD_GLOBAL_FLAG_SMB3_MULTICHANNEL) &&
		   req->Flags & SMB2_SESSION_REQ_FLAG_BINDING) {
		u64 sess_id = le64_to_cpu(req->hdr.SessionId);

		sess = ksmbd_session_lookup_slowpath(sess_id);
		if (!sess) {
			rc = -ENOENT;
			goto out_err;
		}

		if (conn->dialect != sess->dialect) {
			rc = -EINVAL;
			goto out_err;
		}

		if (!(req->hdr.Flags & SMB2_FLAGS_SIGNED)) {
			rc = -EINVAL;
			goto out_err;
		}

		if (strncmp(conn->ClientGUID, sess->ClientGUID,
			    SMB2_CLIENT_GUID_SIZE)) {
			rc = -ENOENT;
			goto out_err;
		}

		if (sess->state == SMB2_SESSION_IN_PROGRESS) {
			rc = -EACCES;
			goto out_err;
		}

		if (sess->state == SMB2_SESSION_EXPIRED) {
			rc = -EFAULT;
			goto out_err;
		}

		if (ksmbd_conn_need_reconnect(conn)) {
			rc = -EFAULT;
			sess = NULL;
			goto out_err;
		}

		if (ksmbd_session_lookup(conn, sess_id)) {
			rc = -EACCES;
			goto out_err;
		}

		if (user_guest(sess->user)) {
			rc = -EOPNOTSUPP;
			goto out_err;
		}

		conn->binding = true;
		ksmbd_user_session_get(sess);
	} else if ((conn->dialect < SMB30_PROT_ID ||
		    server_conf.flags & KSMBD_GLOBAL_FLAG_SMB3_MULTICHANNEL) &&
		   (req->Flags & SMB2_SESSION_REQ_FLAG_BINDING)) {
		sess = NULL;
		rc = -EACCES;
		goto out_err;
	} else {
		sess = ksmbd_session_lookup(conn,
					    le64_to_cpu(req->hdr.SessionId));
		if (!sess) {
			rc = -ENOENT;
			goto out_err;
		}

		if (sess->state == SMB2_SESSION_EXPIRED) {
			rc = -EFAULT;
			goto out_err;
		}

		if (ksmbd_conn_need_reconnect(conn)) {
			rc = -EFAULT;
			sess = NULL;
			goto out_err;
		}

		conn->binding = false;
		ksmbd_user_session_get(sess);
	}
	work->sess = sess;

	negblob_off = le16_to_cpu(req->SecurityBufferOffset);
	negblob_len = le16_to_cpu(req->SecurityBufferLength);
	if (negblob_off < offsetof(struct smb2_sess_setup_req, Buffer)) {
		rc = -EINVAL;
		goto out_err;
	}

	negblob = (struct negotiate_message *)((char *)&req->hdr.ProtocolId +
			negblob_off);

	if (decode_negotiation_token(conn, negblob, negblob_len) == 0) {
		if (conn->mechToken) {
			negblob = (struct negotiate_message *)conn->mechToken;
			negblob_len = conn->mechTokenLen;
		}
	}

	if (negblob_len < offsetof(struct negotiate_message, NegotiateFlags)) {
		rc = -EINVAL;
		goto out_err;
	}

	if (server_conf.auth_mechs & conn->auth_mechs) {
		rc = generate_preauth_hash(work);
		if (rc)
			goto out_err;

		if (conn->preferred_auth_mech &
				(KSMBD_AUTH_KRB5 | KSMBD_AUTH_MSKRB5)) {
			rc = krb5_authenticate(work, req, rsp);
			if (rc) {
				rc = -EINVAL;
				goto out_err;
			}

			if (!ksmbd_conn_need_reconnect(conn)) {
				ksmbd_conn_set_good(conn);
				sess->state = SMB2_SESSION_VALID;
			}
			kfree(sess->Preauth_HashValue);
			sess->Preauth_HashValue = NULL;
		} else if (conn->preferred_auth_mech == KSMBD_AUTH_NTLMSSP) {
			if (negblob->MessageType == NtLmNegotiate) {
				rc = ntlm_negotiate(work, negblob, negblob_len, rsp);
				if (rc)
					goto out_err;
				rsp->hdr.Status =
					STATUS_MORE_PROCESSING_REQUIRED;
			} else if (negblob->MessageType == NtLmAuthenticate) {
				rc = ntlm_authenticate(work, req, rsp);
				if (rc)
					goto out_err;

				if (!ksmbd_conn_need_reconnect(conn)) {
					ksmbd_conn_set_good(conn);
					sess->state = SMB2_SESSION_VALID;
				}
				if (conn->binding) {
					struct preauth_session *preauth_sess;

					preauth_sess =
						ksmbd_preauth_session_lookup(conn, sess->id);
					if (preauth_sess) {
						list_del(&preauth_sess->preauth_entry);
						kfree(preauth_sess);
					}
				}
				kfree(sess->Preauth_HashValue);
				sess->Preauth_HashValue = NULL;
			} else {
				pr_info_ratelimited("Unknown NTLMSSP message type : 0x%x\n",
						le32_to_cpu(negblob->MessageType));
				rc = -EINVAL;
			}
		} else {
			/* TODO: need one more negotiation */
			pr_err("Not support the preferred authentication\n");
			rc = -EINVAL;
		}
	} else {
		pr_err("Not support authentication\n");
		rc = -EINVAL;
	}

out_err:
	if (rc == -EINVAL)
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
	else if (rc == -ENOENT)
		rsp->hdr.Status = STATUS_USER_SESSION_DELETED;
	else if (rc == -EACCES)
		rsp->hdr.Status = STATUS_REQUEST_NOT_ACCEPTED;
	else if (rc == -EFAULT)
		rsp->hdr.Status = STATUS_NETWORK_SESSION_EXPIRED;
	else if (rc == -ENOMEM)
		rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
	else if (rc == -EOPNOTSUPP)
		rsp->hdr.Status = STATUS_NOT_SUPPORTED;
	else if (rc)
		rsp->hdr.Status = STATUS_LOGON_FAILURE;

	if (conn->use_spnego && conn->mechToken) {
		kfree(conn->mechToken);
		conn->mechToken = NULL;
	}

	if (rc < 0) {
		/*
		 * SecurityBufferOffset should be set to zero
		 * in session setup error response.
		 */
		rsp->SecurityBufferOffset = 0;

		if (sess) {
			bool try_delay = false;

			/*
			 * To avoid dictionary attacks (repeated session setups rapidly sent) to
			 * connect to server, ksmbd make a delay of a 5 seconds on session setup
			 * failure to make it harder to send enough random connection requests
			 * to break into a server.
			 */
			if (sess->user && sess->user->flags & KSMBD_USER_FLAG_DELAY_SESSION)
				try_delay = true;

			sess->last_active = jiffies;
			sess->state = SMB2_SESSION_EXPIRED;
			if (try_delay) {
				ksmbd_conn_set_need_reconnect(conn);
				ssleep(5);
				ksmbd_conn_set_need_negotiate(conn);
			}
		}
		smb2_set_err_rsp(work);
	} else {
		unsigned int iov_len;

		if (rsp->SecurityBufferLength)
			iov_len = offsetof(struct smb2_sess_setup_rsp, Buffer) +
				le16_to_cpu(rsp->SecurityBufferLength);
		else
			iov_len = sizeof(struct smb2_sess_setup_rsp);
		rc = ksmbd_iov_pin_rsp(work, rsp, iov_len);
		if (rc)
			rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
	}

	ksmbd_conn_unlock(conn);
	return rc;
}

/**
 * smb2_tree_connect() - handler for smb2 tree connect command
 * @work:	smb work containing smb request buffer
 *
 * Return:      0 on success, otherwise error
 */
int smb2_tree_connect(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_tree_connect_req *req;
	struct smb2_tree_connect_rsp *rsp;
	struct ksmbd_session *sess = work->sess;
	char *treename = NULL, *name = NULL;
	struct ksmbd_tree_conn_status status;
	struct ksmbd_share_config *share = NULL;
	int rc = -EINVAL;

	WORK_BUFFERS(work, req, rsp);

	treename = smb_strndup_from_utf16((char *)req + le16_to_cpu(req->PathOffset),
					  le16_to_cpu(req->PathLength), true,
					  conn->local_nls);
	if (IS_ERR(treename)) {
		pr_err("treename is NULL\n");
		status.ret = KSMBD_TREE_CONN_STATUS_ERROR;
		goto out_err1;
	}

	name = ksmbd_extract_sharename(conn->um, treename);
	if (IS_ERR(name)) {
		status.ret = KSMBD_TREE_CONN_STATUS_ERROR;
		goto out_err1;
	}

	ksmbd_debug(SMB, "tree connect request for tree %s treename %s\n",
		    name, treename);

	status = ksmbd_tree_conn_connect(work, name);
	if (status.ret == KSMBD_TREE_CONN_STATUS_OK)
		rsp->hdr.Id.SyncId.TreeId = cpu_to_le32(status.tree_conn->id);
	else
		goto out_err1;

	share = status.tree_conn->share_conf;
	if (test_share_config_flag(share, KSMBD_SHARE_FLAG_PIPE)) {
		ksmbd_debug(SMB, "IPC share path request\n");
		rsp->ShareType = SMB2_SHARE_TYPE_PIPE;
		rsp->MaximalAccess = FILE_READ_DATA_LE | FILE_READ_EA_LE |
			FILE_EXECUTE_LE | FILE_READ_ATTRIBUTES_LE |
			FILE_DELETE_LE | FILE_READ_CONTROL_LE |
			FILE_WRITE_DAC_LE | FILE_WRITE_OWNER_LE |
			FILE_SYNCHRONIZE_LE;
	} else {
		rsp->ShareType = SMB2_SHARE_TYPE_DISK;
		rsp->MaximalAccess = FILE_READ_DATA_LE | FILE_READ_EA_LE |
			FILE_EXECUTE_LE | FILE_READ_ATTRIBUTES_LE;
		if (test_tree_conn_flag(status.tree_conn,
					KSMBD_TREE_CONN_FLAG_WRITABLE)) {
			rsp->MaximalAccess |= FILE_WRITE_DATA_LE |
				FILE_APPEND_DATA_LE | FILE_WRITE_EA_LE |
				FILE_DELETE_LE | FILE_WRITE_ATTRIBUTES_LE |
				FILE_DELETE_CHILD_LE | FILE_READ_CONTROL_LE |
				FILE_WRITE_DAC_LE | FILE_WRITE_OWNER_LE |
				FILE_SYNCHRONIZE_LE;
		}
	}

	status.tree_conn->maximal_access = le32_to_cpu(rsp->MaximalAccess);
	if (conn->posix_ext_supported)
		status.tree_conn->posix_extensions = true;

	write_lock(&sess->tree_conns_lock);
	status.tree_conn->t_state = TREE_CONNECTED;
	write_unlock(&sess->tree_conns_lock);
	rsp->StructureSize = cpu_to_le16(16);
out_err1:
	if (server_conf.flags & KSMBD_GLOBAL_FLAG_DURABLE_HANDLE && share &&
	    test_share_config_flag(share,
				   KSMBD_SHARE_FLAG_CONTINUOUS_AVAILABILITY))
		rsp->Capabilities = SMB2_SHARE_CAP_CONTINUOUS_AVAILABILITY;
	else
		rsp->Capabilities = 0;
	rsp->Reserved = 0;
	/* default manual caching */
	rsp->ShareFlags = SMB2_SHAREFLAG_MANUAL_CACHING;

	rc = ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_tree_connect_rsp));
	if (rc)
		status.ret = KSMBD_TREE_CONN_STATUS_NOMEM;

	if (!IS_ERR(treename))
		kfree(treename);
	if (!IS_ERR(name))
		kfree(name);

	switch (status.ret) {
	case KSMBD_TREE_CONN_STATUS_OK:
		rsp->hdr.Status = STATUS_SUCCESS;
		rc = 0;
		break;
	case -ESTALE:
	case -ENOENT:
	case KSMBD_TREE_CONN_STATUS_NO_SHARE:
		rsp->hdr.Status = STATUS_BAD_NETWORK_NAME;
		break;
	case -ENOMEM:
	case KSMBD_TREE_CONN_STATUS_NOMEM:
		rsp->hdr.Status = STATUS_NO_MEMORY;
		break;
	case KSMBD_TREE_CONN_STATUS_ERROR:
	case KSMBD_TREE_CONN_STATUS_TOO_MANY_CONNS:
	case KSMBD_TREE_CONN_STATUS_TOO_MANY_SESSIONS:
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
		break;
	case -EINVAL:
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		break;
	default:
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
	}

	if (status.ret != KSMBD_TREE_CONN_STATUS_OK)
		smb2_set_err_rsp(work);

	return rc;
}

/**
 * smb2_create_open_flags() - convert smb open flags to unix open flags
 * @file_present:	is file already present
 * @access:		file access flags
 * @disposition:	file disposition flags
 * @may_flags:		set with MAY_ flags
 * @coptions:		file creation options
 * @mode:		file mode
 *
 * Return:      file open flags
 */
static int smb2_create_open_flags(bool file_present, __le32 access,
				  __le32 disposition,
				  int *may_flags,
				  __le32 coptions,
				  umode_t mode)
{
	int oflags = O_NONBLOCK | O_LARGEFILE;

	if (coptions & FILE_DIRECTORY_FILE_LE || S_ISDIR(mode)) {
		access &= ~FILE_WRITE_DESIRE_ACCESS_LE;
		ksmbd_debug(SMB, "Discard write access to a directory\n");
	}

	if (access & FILE_READ_DESIRED_ACCESS_LE &&
	    access & FILE_WRITE_DESIRE_ACCESS_LE) {
		oflags |= O_RDWR;
		*may_flags = MAY_OPEN | MAY_READ | MAY_WRITE;
	} else if (access & FILE_WRITE_DESIRE_ACCESS_LE) {
		oflags |= O_WRONLY;
		*may_flags = MAY_OPEN | MAY_WRITE;
	} else {
		oflags |= O_RDONLY;
		*may_flags = MAY_OPEN | MAY_READ;
	}

	if (access == FILE_READ_ATTRIBUTES_LE || S_ISBLK(mode) || S_ISCHR(mode))
		oflags |= O_PATH;

	if (file_present) {
		switch (disposition & FILE_CREATE_MASK_LE) {
		case FILE_OPEN_LE:
		case FILE_CREATE_LE:
			break;
		case FILE_SUPERSEDE_LE:
		case FILE_OVERWRITE_LE:
		case FILE_OVERWRITE_IF_LE:
			oflags |= O_TRUNC;
			break;
		default:
			break;
		}
	} else {
		switch (disposition & FILE_CREATE_MASK_LE) {
		case FILE_SUPERSEDE_LE:
		case FILE_CREATE_LE:
		case FILE_OPEN_IF_LE:
		case FILE_OVERWRITE_IF_LE:
			oflags |= O_CREAT;
			break;
		case FILE_OPEN_LE:
		case FILE_OVERWRITE_LE:
			oflags &= ~O_CREAT;
			break;
		default:
			break;
		}
	}

	return oflags;
}

/**
 * smb2_tree_disconnect() - handler for smb tree connect request
 * @work:	smb work containing request buffer
 *
 * Return:      0
 */
int smb2_tree_disconnect(struct ksmbd_work *work)
{
	struct smb2_tree_disconnect_rsp *rsp;
	struct smb2_tree_disconnect_req *req;
	struct ksmbd_session *sess = work->sess;
	struct ksmbd_tree_connect *tcon = work->tcon;
	int err;

	WORK_BUFFERS(work, req, rsp);

	ksmbd_debug(SMB, "request\n");

	if (!tcon) {
		ksmbd_debug(SMB, "Invalid tid %d\n", req->hdr.Id.SyncId.TreeId);

		rsp->hdr.Status = STATUS_NETWORK_NAME_DELETED;
		err = -ENOENT;
		goto err_out;
	}

	ksmbd_close_tree_conn_fds(work);

	write_lock(&sess->tree_conns_lock);
	if (tcon->t_state == TREE_DISCONNECTED) {
		write_unlock(&sess->tree_conns_lock);
		rsp->hdr.Status = STATUS_NETWORK_NAME_DELETED;
		err = -ENOENT;
		goto err_out;
	}

	WARN_ON_ONCE(atomic_dec_and_test(&tcon->refcount));
	tcon->t_state = TREE_DISCONNECTED;
	write_unlock(&sess->tree_conns_lock);

	err = ksmbd_tree_conn_disconnect(sess, tcon);
	if (err) {
		rsp->hdr.Status = STATUS_NETWORK_NAME_DELETED;
		goto err_out;
	}

	work->tcon = NULL;

	rsp->StructureSize = cpu_to_le16(4);
	err = ksmbd_iov_pin_rsp(work, rsp,
				sizeof(struct smb2_tree_disconnect_rsp));
	if (err) {
		rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
		goto err_out;
	}

	return 0;

err_out:
	smb2_set_err_rsp(work);
	return err;

}

/**
 * smb2_session_logoff() - handler for session log off request
 * @work:	smb work containing request buffer
 *
 * Return:      0
 */
int smb2_session_logoff(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_logoff_req *req;
	struct smb2_logoff_rsp *rsp;
	struct ksmbd_session *sess;
	u64 sess_id;
	int err;

	WORK_BUFFERS(work, req, rsp);

	ksmbd_debug(SMB, "request\n");

	ksmbd_conn_lock(conn);
	if (!ksmbd_conn_good(conn)) {
		ksmbd_conn_unlock(conn);
		rsp->hdr.Status = STATUS_NETWORK_NAME_DELETED;
		smb2_set_err_rsp(work);
		return -ENOENT;
	}
	sess_id = le64_to_cpu(req->hdr.SessionId);
	ksmbd_all_conn_set_status(sess_id, KSMBD_SESS_NEED_RECONNECT);
	ksmbd_conn_unlock(conn);

	ksmbd_close_session_fds(work);
	ksmbd_conn_wait_idle(conn);

	/*
	 * Re-lookup session to validate if session is deleted
	 * while waiting request complete
	 */
	sess = ksmbd_session_lookup_all(conn, sess_id);
	if (ksmbd_tree_conn_session_logoff(sess)) {
		ksmbd_debug(SMB, "Invalid tid %d\n", req->hdr.Id.SyncId.TreeId);
		rsp->hdr.Status = STATUS_NETWORK_NAME_DELETED;
		smb2_set_err_rsp(work);
		return -ENOENT;
	}

	ksmbd_destroy_file_table(&sess->file_table);
	down_write(&conn->session_lock);
	sess->state = SMB2_SESSION_EXPIRED;
	up_write(&conn->session_lock);

	ksmbd_free_user(sess->user);
	sess->user = NULL;
	ksmbd_all_conn_set_status(sess_id, KSMBD_SESS_NEED_NEGOTIATE);

	rsp->StructureSize = cpu_to_le16(4);
	err = ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_logoff_rsp));
	if (err) {
		rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
		smb2_set_err_rsp(work);
		return err;
	}
	return 0;
}

/**
 * create_smb2_pipe() - create IPC pipe
 * @work:	smb work containing request buffer
 *
 * Return:      0 on success, otherwise error
 */
static noinline int create_smb2_pipe(struct ksmbd_work *work)
{
	struct smb2_create_rsp *rsp;
	struct smb2_create_req *req;
	int id;
	int err;
	char *name;

	WORK_BUFFERS(work, req, rsp);

	name = smb_strndup_from_utf16(req->Buffer, le16_to_cpu(req->NameLength),
				      1, work->conn->local_nls);
	if (IS_ERR(name)) {
		rsp->hdr.Status = STATUS_NO_MEMORY;
		err = PTR_ERR(name);
		goto out;
	}

	id = ksmbd_session_rpc_open(work->sess, name);
	if (id < 0) {
		pr_err("Unable to open RPC pipe: %d\n", id);
		err = id;
		goto out;
	}

	rsp->hdr.Status = STATUS_SUCCESS;
	rsp->StructureSize = cpu_to_le16(89);
	rsp->OplockLevel = SMB2_OPLOCK_LEVEL_NONE;
	rsp->Flags = 0;
	rsp->CreateAction = cpu_to_le32(FILE_OPENED);

	rsp->CreationTime = cpu_to_le64(0);
	rsp->LastAccessTime = cpu_to_le64(0);
	rsp->ChangeTime = cpu_to_le64(0);
	rsp->AllocationSize = cpu_to_le64(0);
	rsp->EndofFile = cpu_to_le64(0);
	rsp->FileAttributes = FILE_ATTRIBUTE_NORMAL_LE;
	rsp->Reserved2 = 0;
	rsp->VolatileFileId = id;
	rsp->PersistentFileId = 0;
	rsp->CreateContextsOffset = 0;
	rsp->CreateContextsLength = 0;

	err = ksmbd_iov_pin_rsp(work, rsp, offsetof(struct smb2_create_rsp, Buffer));
	if (err)
		goto out;

	kfree(name);
	return 0;

out:
	switch (err) {
	case -EINVAL:
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		break;
	case -ENOSPC:
	case -ENOMEM:
		rsp->hdr.Status = STATUS_NO_MEMORY;
		break;
	}

	if (!IS_ERR(name))
		kfree(name);

	smb2_set_err_rsp(work);
	return err;
}

/**
 * smb2_set_ea() - handler for setting extended attributes using set
 *		info command
 * @eabuf:	set info command buffer
 * @buf_len:	set info command buffer length
 * @path:	dentry path for get ea
 * @get_write:	get write access to a mount
 *
 * Return:	0 on success, otherwise error
 */
static int smb2_set_ea(struct smb2_ea_info *eabuf, unsigned int buf_len,
		       const struct path *path, bool get_write)
{
	struct mnt_idmap *idmap = mnt_idmap(path->mnt);
	char *attr_name = NULL, *value;
	int rc = 0;
	unsigned int next = 0;

	if (buf_len < sizeof(struct smb2_ea_info) + eabuf->EaNameLength +
			le16_to_cpu(eabuf->EaValueLength))
		return -EINVAL;

	attr_name = kmalloc(XATTR_NAME_MAX + 1, GFP_KERNEL);
	if (!attr_name)
		return -ENOMEM;

	do {
		if (!eabuf->EaNameLength)
			goto next;

		ksmbd_debug(SMB,
			    "name : <%s>, name_len : %u, value_len : %u, next : %u\n",
			    eabuf->name, eabuf->EaNameLength,
			    le16_to_cpu(eabuf->EaValueLength),
			    le32_to_cpu(eabuf->NextEntryOffset));

		if (eabuf->EaNameLength >
		    (XATTR_NAME_MAX - XATTR_USER_PREFIX_LEN)) {
			rc = -EINVAL;
			break;
		}

		memcpy(attr_name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN);
		memcpy(&attr_name[XATTR_USER_PREFIX_LEN], eabuf->name,
		       eabuf->EaNameLength);
		attr_name[XATTR_USER_PREFIX_LEN + eabuf->EaNameLength] = '\0';
		value = (char *)&eabuf->name + eabuf->EaNameLength + 1;

		if (!eabuf->EaValueLength) {
			rc = ksmbd_vfs_casexattr_len(idmap,
						     path->dentry,
						     attr_name,
						     XATTR_USER_PREFIX_LEN +
						     eabuf->EaNameLength);

			/* delete the EA only when it exits */
			if (rc > 0) {
				rc = ksmbd_vfs_remove_xattr(idmap,
							    path,
							    attr_name,
							    get_write);

				if (rc < 0) {
					ksmbd_debug(SMB,
						    "remove xattr failed(%d)\n",
						    rc);
					break;
				}
			}

			/* if the EA doesn't exist, just do nothing. */
			rc = 0;
		} else {
			rc = ksmbd_vfs_setxattr(idmap, path, attr_name, value,
						le16_to_cpu(eabuf->EaValueLength),
						0, get_write);
			if (rc < 0) {
				ksmbd_debug(SMB,
					    "ksmbd_vfs_setxattr is failed(%d)\n",
					    rc);
				break;
			}
		}

next:
		next = le32_to_cpu(eabuf->NextEntryOffset);
		if (next == 0 || buf_len < next)
			break;
		buf_len -= next;
		eabuf = (struct smb2_ea_info *)((char *)eabuf + next);
		if (buf_len < sizeof(struct smb2_ea_info)) {
			rc = -EINVAL;
			break;
		}

		if (buf_len < sizeof(struct smb2_ea_info) + eabuf->EaNameLength +
				le16_to_cpu(eabuf->EaValueLength)) {
			rc = -EINVAL;
			break;
		}
	} while (next != 0);

	kfree(attr_name);
	return rc;
}

static noinline int smb2_set_stream_name_xattr(const struct path *path,
					       struct ksmbd_file *fp,
					       char *stream_name, int s_type)
{
	struct mnt_idmap *idmap = mnt_idmap(path->mnt);
	size_t xattr_stream_size;
	char *xattr_stream_name;
	int rc;

	rc = ksmbd_vfs_xattr_stream_name(stream_name,
					 &xattr_stream_name,
					 &xattr_stream_size,
					 s_type);
	if (rc)
		return rc;

	fp->stream.name = xattr_stream_name;
	fp->stream.size = xattr_stream_size;

	/* Check if there is stream prefix in xattr space */
	rc = ksmbd_vfs_casexattr_len(idmap,
				     path->dentry,
				     xattr_stream_name,
				     xattr_stream_size);
	if (rc >= 0)
		return 0;

	if (fp->cdoption == FILE_OPEN_LE) {
		ksmbd_debug(SMB, "XATTR stream name lookup failed: %d\n", rc);
		return -EBADF;
	}

	rc = ksmbd_vfs_setxattr(idmap, path, xattr_stream_name, NULL, 0, 0, false);
	if (rc < 0)
		pr_err("Failed to store XATTR stream name :%d\n", rc);
	return 0;
}

static int smb2_remove_smb_xattrs(const struct path *path)
{
	struct mnt_idmap *idmap = mnt_idmap(path->mnt);
	char *name, *xattr_list = NULL;
	ssize_t xattr_list_len;
	int err = 0;

	xattr_list_len = ksmbd_vfs_listxattr(path->dentry, &xattr_list);
	if (xattr_list_len < 0) {
		goto out;
	} else if (!xattr_list_len) {
		ksmbd_debug(SMB, "empty xattr in the file\n");
		goto out;
	}

	for (name = xattr_list; name - xattr_list < xattr_list_len;
			name += strlen(name) + 1) {
		ksmbd_debug(SMB, "%s, len %zd\n", name, strlen(name));

		if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN) &&
		    !strncmp(&name[XATTR_USER_PREFIX_LEN], STREAM_PREFIX,
			     STREAM_PREFIX_LEN)) {
			err = ksmbd_vfs_remove_xattr(idmap, path,
						     name, true);
			if (err)
				ksmbd_debug(SMB, "remove xattr failed : %s\n",
					    name);
		}
	}
out:
	kvfree(xattr_list);
	return err;
}

static int smb2_create_truncate(const struct path *path)
{
	int rc = vfs_truncate(path, 0);

	if (rc) {
		pr_err("vfs_truncate failed, rc %d\n", rc);
		return rc;
	}

	rc = smb2_remove_smb_xattrs(path);
	if (rc == -EOPNOTSUPP)
		rc = 0;
	if (rc)
		ksmbd_debug(SMB,
			    "ksmbd_truncate_stream_name_xattr failed, rc %d\n",
			    rc);
	return rc;
}

static void smb2_new_xattrs(struct ksmbd_tree_connect *tcon, const struct path *path,
			    struct ksmbd_file *fp)
{
	struct xattr_dos_attrib da = {0};
	int rc;

	if (!test_share_config_flag(tcon->share_conf,
				    KSMBD_SHARE_FLAG_STORE_DOS_ATTRS))
		return;

	da.version = 4;
	da.attr = le32_to_cpu(fp->f_ci->m_fattr);
	da.itime = da.create_time = fp->create_time;
	da.flags = XATTR_DOSINFO_ATTRIB | XATTR_DOSINFO_CREATE_TIME |
		XATTR_DOSINFO_ITIME;

	rc = ksmbd_vfs_set_dos_attrib_xattr(mnt_idmap(path->mnt), path, &da, true);
	if (rc)
		ksmbd_debug(SMB, "failed to store file attribute into xattr\n");
}

static void smb2_update_xattrs(struct ksmbd_tree_connect *tcon,
			       const struct path *path, struct ksmbd_file *fp)
{
	struct xattr_dos_attrib da;
	int rc;

	fp->f_ci->m_fattr &= ~(FILE_ATTRIBUTE_HIDDEN_LE | FILE_ATTRIBUTE_SYSTEM_LE);

	/* get FileAttributes from XATTR_NAME_DOS_ATTRIBUTE */
	if (!test_share_config_flag(tcon->share_conf,
				    KSMBD_SHARE_FLAG_STORE_DOS_ATTRS))
		return;

	rc = ksmbd_vfs_get_dos_attrib_xattr(mnt_idmap(path->mnt),
					    path->dentry, &da);
	if (rc > 0) {
		fp->f_ci->m_fattr = cpu_to_le32(da.attr);
		fp->create_time = da.create_time;
		fp->itime = da.itime;
	}
}

static int smb2_creat(struct ksmbd_work *work, struct path *parent_path,
		      struct path *path, char *name, int open_flags,
		      umode_t posix_mode, bool is_dir)
{
	struct ksmbd_tree_connect *tcon = work->tcon;
	struct ksmbd_share_config *share = tcon->share_conf;
	umode_t mode;
	int rc;

	if (!(open_flags & O_CREAT))
		return -EBADF;

	ksmbd_debug(SMB, "file does not exist, so creating\n");
	if (is_dir == true) {
		ksmbd_debug(SMB, "creating directory\n");

		mode = share_config_directory_mode(share, posix_mode);
		rc = ksmbd_vfs_mkdir(work, name, mode);
		if (rc)
			return rc;
	} else {
		ksmbd_debug(SMB, "creating regular file\n");

		mode = share_config_create_mode(share, posix_mode);
		rc = ksmbd_vfs_create(work, name, mode);
		if (rc)
			return rc;
	}

	rc = ksmbd_vfs_kern_path_locked(work, name, 0, parent_path, path, 0);
	if (rc) {
		pr_err("cannot get linux path (%s), err = %d\n",
		       name, rc);
		return rc;
	}
	return 0;
}

static int smb2_create_sd_buffer(struct ksmbd_work *work,
				 struct smb2_create_req *req,
				 const struct path *path)
{
	struct create_context *context;
	struct create_sd_buf_req *sd_buf;

	if (!req->CreateContextsOffset)
		return -ENOENT;

	/* Parse SD BUFFER create contexts */
	context = smb2_find_context_vals(req, SMB2_CREATE_SD_BUFFER, 4);
	if (!context)
		return -ENOENT;
	else if (IS_ERR(context))
		return PTR_ERR(context);

	ksmbd_debug(SMB,
		    "Set ACLs using SMB2_CREATE_SD_BUFFER context\n");
	sd_buf = (struct create_sd_buf_req *)context;
	if (le16_to_cpu(context->DataOffset) +
	    le32_to_cpu(context->DataLength) <
	    sizeof(struct create_sd_buf_req))
		return -EINVAL;
	return set_info_sec(work->conn, work->tcon, path, &sd_buf->ntsd,
			    le32_to_cpu(sd_buf->ccontext.DataLength), true, false);
}

static void ksmbd_acls_fattr(struct smb_fattr *fattr,
			     struct mnt_idmap *idmap,
			     struct inode *inode)
{
	vfsuid_t vfsuid = i_uid_into_vfsuid(idmap, inode);
	vfsgid_t vfsgid = i_gid_into_vfsgid(idmap, inode);

	fattr->cf_uid = vfsuid_into_kuid(vfsuid);
	fattr->cf_gid = vfsgid_into_kgid(vfsgid);
	fattr->cf_mode = inode->i_mode;
	fattr->cf_acls = NULL;
	fattr->cf_dacls = NULL;

	if (IS_ENABLED(CONFIG_FS_POSIX_ACL)) {
		fattr->cf_acls = get_inode_acl(inode, ACL_TYPE_ACCESS);
		if (S_ISDIR(inode->i_mode))
			fattr->cf_dacls = get_inode_acl(inode, ACL_TYPE_DEFAULT);
	}
}

enum {
	DURABLE_RECONN_V2 = 1,
	DURABLE_RECONN,
	DURABLE_REQ_V2,
	DURABLE_REQ,
};

struct durable_info {
	struct ksmbd_file *fp;
	unsigned short int type;
	bool persistent;
	bool reconnected;
	unsigned int timeout;
	char *CreateGuid;
};

static int parse_durable_handle_context(struct ksmbd_work *work,
					struct smb2_create_req *req,
					struct lease_ctx_info *lc,
					struct durable_info *dh_info)
{
	struct ksmbd_conn *conn = work->conn;
	struct create_context *context;
	int dh_idx, err = 0;
	u64 persistent_id = 0;
	int req_op_level;
	static const char * const durable_arr[] = {"DH2C", "DHnC", "DH2Q", "DHnQ"};

	req_op_level = req->RequestedOplockLevel;
	for (dh_idx = DURABLE_RECONN_V2; dh_idx <= ARRAY_SIZE(durable_arr);
	     dh_idx++) {
		context = smb2_find_context_vals(req, durable_arr[dh_idx - 1], 4);
		if (IS_ERR(context)) {
			err = PTR_ERR(context);
			goto out;
		}
		if (!context)
			continue;

		switch (dh_idx) {
		case DURABLE_RECONN_V2:
		{
			struct create_durable_reconn_v2_req *recon_v2;

			if (dh_info->type == DURABLE_RECONN ||
			    dh_info->type == DURABLE_REQ_V2) {
				err = -EINVAL;
				goto out;
			}

			recon_v2 = (struct create_durable_reconn_v2_req *)context;
			persistent_id = recon_v2->Fid.PersistentFileId;
			dh_info->fp = ksmbd_lookup_durable_fd(persistent_id);
			if (!dh_info->fp) {
				ksmbd_debug(SMB, "Failed to get durable handle state\n");
				err = -EBADF;
				goto out;
			}

			if (memcmp(dh_info->fp->create_guid, recon_v2->CreateGuid,
				   SMB2_CREATE_GUID_SIZE)) {
				err = -EBADF;
				ksmbd_put_durable_fd(dh_info->fp);
				goto out;
			}

			dh_info->type = dh_idx;
			dh_info->reconnected = true;
			ksmbd_debug(SMB,
				"reconnect v2 Persistent-id from reconnect = %llu\n",
					persistent_id);
			break;
		}
		case DURABLE_RECONN:
		{
			struct create_durable_reconn_req *recon;

			if (dh_info->type == DURABLE_RECONN_V2 ||
			    dh_info->type == DURABLE_REQ_V2) {
				err = -EINVAL;
				goto out;
			}

			recon = (struct create_durable_reconn_req *)context;
			persistent_id = recon->Data.Fid.PersistentFileId;
			dh_info->fp = ksmbd_lookup_durable_fd(persistent_id);
			if (!dh_info->fp) {
				ksmbd_debug(SMB, "Failed to get durable handle state\n");
				err = -EBADF;
				goto out;
			}

			dh_info->type = dh_idx;
			dh_info->reconnected = true;
			ksmbd_debug(SMB, "reconnect Persistent-id from reconnect = %llu\n",
				    persistent_id);
			break;
		}
		case DURABLE_REQ_V2:
		{
			struct create_durable_req_v2 *durable_v2_blob;

			if (dh_info->type == DURABLE_RECONN ||
			    dh_info->type == DURABLE_RECONN_V2) {
				err = -EINVAL;
				goto out;
			}

			durable_v2_blob =
				(struct create_durable_req_v2 *)context;
			ksmbd_debug(SMB, "Request for durable v2 open\n");
			dh_info->fp = ksmbd_lookup_fd_cguid(durable_v2_blob->CreateGuid);
			if (dh_info->fp) {
				if (!memcmp(conn->ClientGUID, dh_info->fp->client_guid,
					    SMB2_CLIENT_GUID_SIZE)) {
					if (!(req->hdr.Flags & SMB2_FLAGS_REPLAY_OPERATION)) {
						err = -ENOEXEC;
						goto out;
					}

					dh_info->fp->conn = conn;
					dh_info->reconnected = true;
					goto out;
				}
			}

			if ((lc && (lc->req_state & SMB2_LEASE_HANDLE_CACHING_LE)) ||
			    req_op_level == SMB2_OPLOCK_LEVEL_BATCH) {
				dh_info->CreateGuid =
					durable_v2_blob->CreateGuid;
				dh_info->persistent =
					le32_to_cpu(durable_v2_blob->Flags);
				dh_info->timeout =
					le32_to_cpu(durable_v2_blob->Timeout);
				dh_info->type = dh_idx;
			}
			break;
		}
		case DURABLE_REQ:
			if (dh_info->type == DURABLE_RECONN)
				goto out;
			if (dh_info->type == DURABLE_RECONN_V2 ||
			    dh_info->type == DURABLE_REQ_V2) {
				err = -EINVAL;
				goto out;
			}

			if ((lc && (lc->req_state & SMB2_LEASE_HANDLE_CACHING_LE)) ||
			    req_op_level == SMB2_OPLOCK_LEVEL_BATCH) {
				ksmbd_debug(SMB, "Request for durable open\n");
				dh_info->type = dh_idx;
			}
		}
	}

out:
	return err;
}

/**
 * smb2_open() - handler for smb file open request
 * @work:	smb work containing request buffer
 *
 * Return:      0 on success, otherwise error
 */
int smb2_open(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	struct ksmbd_tree_connect *tcon = work->tcon;
	struct smb2_create_req *req;
	struct smb2_create_rsp *rsp;
	struct path path, parent_path;
	struct ksmbd_share_config *share = tcon->share_conf;
	struct ksmbd_file *fp = NULL;
	struct file *filp = NULL;
	struct mnt_idmap *idmap = NULL;
	struct kstat stat;
	struct create_context *context;
	struct lease_ctx_info *lc = NULL;
	struct create_ea_buf_req *ea_buf = NULL;
	struct oplock_info *opinfo;
	struct durable_info dh_info = {0};
	__le32 *next_ptr = NULL;
	int req_op_level = 0, open_flags = 0, may_flags = 0, file_info = 0;
	int rc = 0;
	int contxt_cnt = 0, query_disk_id = 0;
	int maximal_access_ctxt = 0, posix_ctxt = 0;
	int s_type = 0;
	int next_off = 0;
	char *name = NULL;
	char *stream_name = NULL;
	bool file_present = false, created = false, already_permitted = false;
	int share_ret, need_truncate = 0;
	u64 time;
	umode_t posix_mode = 0;
	__le32 daccess, maximal_access = 0;
	int iov_len = 0;

	WORK_BUFFERS(work, req, rsp);

	if (req->hdr.NextCommand && !work->next_smb2_rcv_hdr_off &&
	    (req->hdr.Flags & SMB2_FLAGS_RELATED_OPERATIONS)) {
		ksmbd_debug(SMB, "invalid flag in chained command\n");
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		smb2_set_err_rsp(work);
		return -EINVAL;
	}

	if (test_share_config_flag(share, KSMBD_SHARE_FLAG_PIPE)) {
		ksmbd_debug(SMB, "IPC pipe create request\n");
		return create_smb2_pipe(work);
	}

	if (req->NameLength) {
		name = smb2_get_name((char *)req + le16_to_cpu(req->NameOffset),
				     le16_to_cpu(req->NameLength),
				     work->conn->local_nls);
		if (IS_ERR(name)) {
			rc = PTR_ERR(name);
			name = NULL;
			goto err_out2;
		}

		ksmbd_debug(SMB, "converted name = %s\n", name);
		if (strchr(name, ':')) {
			if (!test_share_config_flag(work->tcon->share_conf,
						    KSMBD_SHARE_FLAG_STREAMS)) {
				rc = -EBADF;
				goto err_out2;
			}
			rc = parse_stream_name(name, &stream_name, &s_type);
			if (rc < 0)
				goto err_out2;
		}

		rc = ksmbd_validate_filename(name);
		if (rc < 0)
			goto err_out2;

		if (ksmbd_share_veto_filename(share, name)) {
			rc = -ENOENT;
			ksmbd_debug(SMB, "Reject open(), vetoed file: %s\n",
				    name);
			goto err_out2;
		}
	} else {
		name = kstrdup("", GFP_KERNEL);
		if (!name) {
			rc = -ENOMEM;
			goto err_out2;
		}
	}

	req_op_level = req->RequestedOplockLevel;

	if (server_conf.flags & KSMBD_GLOBAL_FLAG_DURABLE_HANDLE &&
	    req->CreateContextsOffset) {
		lc = parse_lease_state(req);
		rc = parse_durable_handle_context(work, req, lc, &dh_info);
		if (rc) {
			ksmbd_debug(SMB, "error parsing durable handle context\n");
			goto err_out2;
		}

		if (dh_info.reconnected == true) {
			rc = smb2_check_durable_oplock(conn, share, dh_info.fp, lc, name);
			if (rc) {
				ksmbd_put_durable_fd(dh_info.fp);
				goto err_out2;
			}

			rc = ksmbd_reopen_durable_fd(work, dh_info.fp);
			if (rc) {
				ksmbd_put_durable_fd(dh_info.fp);
				goto err_out2;
			}

			if (ksmbd_override_fsids(work)) {
				rc = -ENOMEM;
				ksmbd_put_durable_fd(dh_info.fp);
				goto err_out2;
			}

			fp = dh_info.fp;
			file_info = FILE_OPENED;

			rc = ksmbd_vfs_getattr(&fp->filp->f_path, &stat);
			if (rc)
				goto err_out2;

			ksmbd_put_durable_fd(fp);
			goto reconnected_fp;
		}
	} else if (req_op_level == SMB2_OPLOCK_LEVEL_LEASE)
		lc = parse_lease_state(req);

	if (le32_to_cpu(req->ImpersonationLevel) > le32_to_cpu(IL_DELEGATE)) {
		pr_err("Invalid impersonationlevel : 0x%x\n",
		       le32_to_cpu(req->ImpersonationLevel));
		rc = -EIO;
		rsp->hdr.Status = STATUS_BAD_IMPERSONATION_LEVEL;
		goto err_out2;
	}

	if (req->CreateOptions && !(req->CreateOptions & CREATE_OPTIONS_MASK_LE)) {
		pr_err("Invalid create options : 0x%x\n",
		       le32_to_cpu(req->CreateOptions));
		rc = -EINVAL;
		goto err_out2;
	} else {
		if (req->CreateOptions & FILE_SEQUENTIAL_ONLY_LE &&
		    req->CreateOptions & FILE_RANDOM_ACCESS_LE)
			req->CreateOptions = ~(FILE_SEQUENTIAL_ONLY_LE);

		if (req->CreateOptions &
		    (FILE_OPEN_BY_FILE_ID_LE | CREATE_TREE_CONNECTION |
		     FILE_RESERVE_OPFILTER_LE)) {
			rc = -EOPNOTSUPP;
			goto err_out2;
		}

		if (req->CreateOptions & FILE_DIRECTORY_FILE_LE) {
			if (req->CreateOptions & FILE_NON_DIRECTORY_FILE_LE) {
				rc = -EINVAL;
				goto err_out2;
			} else if (req->CreateOptions & FILE_NO_COMPRESSION_LE) {
				req->CreateOptions = ~(FILE_NO_COMPRESSION_LE);
			}
		}
	}

	if (le32_to_cpu(req->CreateDisposition) >
	    le32_to_cpu(FILE_OVERWRITE_IF_LE)) {
		pr_err("Invalid create disposition : 0x%x\n",
		       le32_to_cpu(req->CreateDisposition));
		rc = -EINVAL;
		goto err_out2;
	}

	if (!(req->DesiredAccess & DESIRED_ACCESS_MASK)) {
		pr_err("Invalid desired access : 0x%x\n",
		       le32_to_cpu(req->DesiredAccess));
		rc = -EACCES;
		goto err_out2;
	}

	if (req->FileAttributes && !(req->FileAttributes & FILE_ATTRIBUTE_MASK_LE)) {
		pr_err("Invalid file attribute : 0x%x\n",
		       le32_to_cpu(req->FileAttributes));
		rc = -EINVAL;
		goto err_out2;
	}

	if (req->CreateContextsOffset) {
		/* Parse non-durable handle create contexts */
		context = smb2_find_context_vals(req, SMB2_CREATE_EA_BUFFER, 4);
		if (IS_ERR(context)) {
			rc = PTR_ERR(context);
			goto err_out2;
		} else if (context) {
			ea_buf = (struct create_ea_buf_req *)context;
			if (le16_to_cpu(context->DataOffset) +
			    le32_to_cpu(context->DataLength) <
			    sizeof(struct create_ea_buf_req)) {
				rc = -EINVAL;
				goto err_out2;
			}
			if (req->CreateOptions & FILE_NO_EA_KNOWLEDGE_LE) {
				rsp->hdr.Status = STATUS_ACCESS_DENIED;
				rc = -EACCES;
				goto err_out2;
			}
		}

		context = smb2_find_context_vals(req,
						 SMB2_CREATE_QUERY_MAXIMAL_ACCESS_REQUEST, 4);
		if (IS_ERR(context)) {
			rc = PTR_ERR(context);
			goto err_out2;
		} else if (context) {
			ksmbd_debug(SMB,
				    "get query maximal access context\n");
			maximal_access_ctxt = 1;
		}

		context = smb2_find_context_vals(req,
						 SMB2_CREATE_TIMEWARP_REQUEST, 4);
		if (IS_ERR(context)) {
			rc = PTR_ERR(context);
			goto err_out2;
		} else if (context) {
			ksmbd_debug(SMB, "get timewarp context\n");
			rc = -EBADF;
			goto err_out2;
		}

		if (tcon->posix_extensions) {
			context = smb2_find_context_vals(req,
							 SMB2_CREATE_TAG_POSIX, 16);
			if (IS_ERR(context)) {
				rc = PTR_ERR(context);
				goto err_out2;
			} else if (context) {
				struct create_posix *posix =
					(struct create_posix *)context;
				if (le16_to_cpu(context->DataOffset) +
				    le32_to_cpu(context->DataLength) <
				    sizeof(struct create_posix) - 4) {
					rc = -EINVAL;
					goto err_out2;
				}
				ksmbd_debug(SMB, "get posix context\n");

				posix_mode = le32_to_cpu(posix->Mode);
				posix_ctxt = 1;
			}
		}
	}

	if (ksmbd_override_fsids(work)) {
		rc = -ENOMEM;
		goto err_out2;
	}

	rc = ksmbd_vfs_kern_path_locked(work, name, LOOKUP_NO_SYMLINKS,
					&parent_path, &path, 1);
	if (!rc) {
		file_present = true;

		if (req->CreateOptions & FILE_DELETE_ON_CLOSE_LE) {
			/*
			 * If file exists with under flags, return access
			 * denied error.
			 */
			if (req->CreateDisposition == FILE_OVERWRITE_IF_LE ||
			    req->CreateDisposition == FILE_OPEN_IF_LE) {
				rc = -EACCES;
				goto err_out;
			}

			if (!test_tree_conn_flag(tcon, KSMBD_TREE_CONN_FLAG_WRITABLE)) {
				ksmbd_debug(SMB,
					    "User does not have write permission\n");
				rc = -EACCES;
				goto err_out;
			}
		} else if (d_is_symlink(path.dentry)) {
			rc = -EACCES;
			goto err_out;
		}

		idmap = mnt_idmap(path.mnt);
	} else {
		if (rc != -ENOENT)
			goto err_out;
		ksmbd_debug(SMB, "can not get linux path for %s, rc = %d\n",
			    name, rc);
		rc = 0;
	}

	if (stream_name) {
		if (req->CreateOptions & FILE_DIRECTORY_FILE_LE) {
			if (s_type == DATA_STREAM) {
				rc = -EIO;
				rsp->hdr.Status = STATUS_NOT_A_DIRECTORY;
			}
		} else {
			if (file_present && S_ISDIR(d_inode(path.dentry)->i_mode) &&
			    s_type == DATA_STREAM) {
				rc = -EIO;
				rsp->hdr.Status = STATUS_FILE_IS_A_DIRECTORY;
			}
		}

		if (req->CreateOptions & FILE_DIRECTORY_FILE_LE &&
		    req->FileAttributes & FILE_ATTRIBUTE_NORMAL_LE) {
			rsp->hdr.Status = STATUS_NOT_A_DIRECTORY;
			rc = -EIO;
		}

		if (rc < 0)
			goto err_out;
	}

	if (file_present && req->CreateOptions & FILE_NON_DIRECTORY_FILE_LE &&
	    S_ISDIR(d_inode(path.dentry)->i_mode) &&
	    !(req->CreateOptions & FILE_DELETE_ON_CLOSE_LE)) {
		ksmbd_debug(SMB, "open() argument is a directory: %s, %x\n",
			    name, req->CreateOptions);
		rsp->hdr.Status = STATUS_FILE_IS_A_DIRECTORY;
		rc = -EIO;
		goto err_out;
	}

	if (file_present && (req->CreateOptions & FILE_DIRECTORY_FILE_LE) &&
	    !(req->CreateDisposition == FILE_CREATE_LE) &&
	    !S_ISDIR(d_inode(path.dentry)->i_mode)) {
		rsp->hdr.Status = STATUS_NOT_A_DIRECTORY;
		rc = -EIO;
		goto err_out;
	}

	if (!stream_name && file_present &&
	    req->CreateDisposition == FILE_CREATE_LE) {
		rc = -EEXIST;
		goto err_out;
	}

	daccess = smb_map_generic_desired_access(req->DesiredAccess);

	if (file_present && !(req->CreateOptions & FILE_DELETE_ON_CLOSE_LE)) {
		rc = smb_check_perm_dacl(conn, &path, &daccess,
					 sess->user->uid);
		if (rc)
			goto err_out;
	}

	if (daccess & FILE_MAXIMAL_ACCESS_LE) {
		if (!file_present) {
			daccess = cpu_to_le32(GENERIC_ALL_FLAGS);
		} else {
			ksmbd_vfs_query_maximal_access(idmap,
							    path.dentry,
							    &daccess);
			already_permitted = true;
		}
		maximal_access = daccess;
	}

	open_flags = smb2_create_open_flags(file_present, daccess,
					    req->CreateDisposition,
					    &may_flags,
					    req->CreateOptions,
					    file_present ? d_inode(path.dentry)->i_mode : 0);

	if (!test_tree_conn_flag(tcon, KSMBD_TREE_CONN_FLAG_WRITABLE)) {
		if (open_flags & (O_CREAT | O_TRUNC)) {
			ksmbd_debug(SMB,
				    "User does not have write permission\n");
			rc = -EACCES;
			goto err_out;
		}
	}

	/*create file if not present */
	if (!file_present) {
		rc = smb2_creat(work, &parent_path, &path, name, open_flags,
				posix_mode,
				req->CreateOptions & FILE_DIRECTORY_FILE_LE);
		if (rc) {
			if (rc == -ENOENT) {
				rc = -EIO;
				rsp->hdr.Status = STATUS_OBJECT_PATH_NOT_FOUND;
			}
			goto err_out;
		}

		created = true;
		idmap = mnt_idmap(path.mnt);
		if (ea_buf) {
			if (le32_to_cpu(ea_buf->ccontext.DataLength) <
			    sizeof(struct smb2_ea_info)) {
				rc = -EINVAL;
				goto err_out;
			}

			rc = smb2_set_ea(&ea_buf->ea,
					 le32_to_cpu(ea_buf->ccontext.DataLength),
					 &path, false);
			if (rc == -EOPNOTSUPP)
				rc = 0;
			else if (rc)
				goto err_out;
		}
	} else if (!already_permitted) {
		/* FILE_READ_ATTRIBUTE is allowed without inode_permission,
		 * because execute(search) permission on a parent directory,
		 * is already granted.
		 */
		if (daccess & ~(FILE_READ_ATTRIBUTES_LE | FILE_READ_CONTROL_LE)) {
			rc = inode_permission(idmap,
					      d_inode(path.dentry),
					      may_flags);
			if (rc)
				goto err_out;

			if ((daccess & FILE_DELETE_LE) ||
			    (req->CreateOptions & FILE_DELETE_ON_CLOSE_LE)) {
				rc = inode_permission(idmap,
						      d_inode(path.dentry->d_parent),
						      MAY_EXEC | MAY_WRITE);
				if (rc)
					goto err_out;
			}
		}
	}

	rc = ksmbd_query_inode_status(path.dentry->d_parent);
	if (rc == KSMBD_INODE_STATUS_PENDING_DELETE) {
		rc = -EBUSY;
		goto err_out;
	}

	rc = 0;
	filp = dentry_open(&path, open_flags, current_cred());
	if (IS_ERR(filp)) {
		rc = PTR_ERR(filp);
		pr_err("dentry open for dir failed, rc %d\n", rc);
		goto err_out;
	}

	if (file_present) {
		if (!(open_flags & O_TRUNC))
			file_info = FILE_OPENED;
		else
			file_info = FILE_OVERWRITTEN;

		if ((req->CreateDisposition & FILE_CREATE_MASK_LE) ==
		    FILE_SUPERSEDE_LE)
			file_info = FILE_SUPERSEDED;
	} else if (open_flags & O_CREAT) {
		file_info = FILE_CREATED;
	}

	ksmbd_vfs_set_fadvise(filp, req->CreateOptions);

	/* Obtain Volatile-ID */
	fp = ksmbd_open_fd(work, filp);
	if (IS_ERR(fp)) {
		fput(filp);
		rc = PTR_ERR(fp);
		fp = NULL;
		goto err_out;
	}

	/* Get Persistent-ID */
	ksmbd_open_durable_fd(fp);
	if (!has_file_id(fp->persistent_id)) {
		rc = -ENOMEM;
		goto err_out;
	}

	fp->cdoption = req->CreateDisposition;
	fp->daccess = daccess;
	fp->saccess = req->ShareAccess;
	fp->coption = req->CreateOptions;

	/* Set default windows and posix acls if creating new file */
	if (created) {
		int posix_acl_rc;
		struct inode *inode = d_inode(path.dentry);

		posix_acl_rc = ksmbd_vfs_inherit_posix_acl(idmap,
							   &path,
							   d_inode(path.dentry->d_parent));
		if (posix_acl_rc)
			ksmbd_debug(SMB, "inherit posix acl failed : %d\n", posix_acl_rc);

		if (test_share_config_flag(work->tcon->share_conf,
					   KSMBD_SHARE_FLAG_ACL_XATTR)) {
			rc = smb_inherit_dacl(conn, &path, sess->user->uid,
					      sess->user->gid);
		}

		if (rc) {
			rc = smb2_create_sd_buffer(work, req, &path);
			if (rc) {
				if (posix_acl_rc)
					ksmbd_vfs_set_init_posix_acl(idmap,
								     &path);

				if (test_share_config_flag(work->tcon->share_conf,
							   KSMBD_SHARE_FLAG_ACL_XATTR)) {
					struct smb_fattr fattr;
					struct smb_ntsd *pntsd;
					int pntsd_size, ace_num = 0;

					ksmbd_acls_fattr(&fattr, idmap, inode);
					if (fattr.cf_acls)
						ace_num = fattr.cf_acls->a_count;
					if (fattr.cf_dacls)
						ace_num += fattr.cf_dacls->a_count;

					pntsd = kmalloc(sizeof(struct smb_ntsd) +
							sizeof(struct smb_sid) * 3 +
							sizeof(struct smb_acl) +
							sizeof(struct smb_ace) * ace_num * 2,
							GFP_KERNEL);
					if (!pntsd) {
						posix_acl_release(fattr.cf_acls);
						posix_acl_release(fattr.cf_dacls);
						goto err_out;
					}

					rc = build_sec_desc(idmap,
							    pntsd, NULL, 0,
							    OWNER_SECINFO |
							    GROUP_SECINFO |
							    DACL_SECINFO,
							    &pntsd_size, &fattr);
					posix_acl_release(fattr.cf_acls);
					posix_acl_release(fattr.cf_dacls);
					if (rc) {
						kfree(pntsd);
						goto err_out;
					}

					rc = ksmbd_vfs_set_sd_xattr(conn,
								    idmap,
								    &path,
								    pntsd,
								    pntsd_size,
								    false);
					kfree(pntsd);
					if (rc)
						pr_err("failed to store ntacl in xattr : %d\n",
						       rc);
				}
			}
		}
		rc = 0;
	}

	if (stream_name) {
		rc = smb2_set_stream_name_xattr(&path,
						fp,
						stream_name,
						s_type);
		if (rc)
			goto err_out;
		file_info = FILE_CREATED;
	}

	fp->attrib_only = !(req->DesiredAccess & ~(FILE_READ_ATTRIBUTES_LE |
			FILE_WRITE_ATTRIBUTES_LE | FILE_SYNCHRONIZE_LE));

	/* fp should be searchable through ksmbd_inode.m_fp_list
	 * after daccess, saccess, attrib_only, and stream are
	 * initialized.
	 */
	down_write(&fp->f_ci->m_lock);
	list_add(&fp->node, &fp->f_ci->m_fp_list);
	up_write(&fp->f_ci->m_lock);

	/* Check delete pending among previous fp before oplock break */
	if (ksmbd_inode_pending_delete(fp)) {
		rc = -EBUSY;
		goto err_out;
	}

	if (file_present || created)
		ksmbd_vfs_kern_path_unlock(&parent_path, &path);

	if (!S_ISDIR(file_inode(filp)->i_mode) && open_flags & O_TRUNC &&
	    !fp->attrib_only && !stream_name) {
		smb_break_all_oplock(work, fp);
		need_truncate = 1;
	}

	share_ret = ksmbd_smb_check_shared_mode(fp->filp, fp);
	if (!test_share_config_flag(work->tcon->share_conf, KSMBD_SHARE_FLAG_OPLOCKS) ||
	    (req_op_level == SMB2_OPLOCK_LEVEL_LEASE &&
	     !(conn->vals->capabilities & SMB2_GLOBAL_CAP_LEASING))) {
		if (share_ret < 0 && !S_ISDIR(file_inode(fp->filp)->i_mode)) {
			rc = share_ret;
			goto err_out1;
		}
	} else {
		if (req_op_level == SMB2_OPLOCK_LEVEL_LEASE && lc) {
			if (S_ISDIR(file_inode(filp)->i_mode)) {
				lc->req_state &= ~SMB2_LEASE_WRITE_CACHING_LE;
				lc->is_dir = true;
			}

			/*
			 * Compare parent lease using parent key. If there is no
			 * a lease that has same parent key, Send lease break
			 * notification.
			 */
			smb_send_parent_lease_break_noti(fp, lc);

			req_op_level = smb2_map_lease_to_oplock(lc->req_state);
			ksmbd_debug(SMB,
				    "lease req for(%s) req oplock state 0x%x, lease state 0x%x\n",
				    name, req_op_level, lc->req_state);
			rc = find_same_lease_key(sess, fp->f_ci, lc);
			if (rc)
				goto err_out1;
		} else if (open_flags == O_RDONLY &&
			   (req_op_level == SMB2_OPLOCK_LEVEL_BATCH ||
			    req_op_level == SMB2_OPLOCK_LEVEL_EXCLUSIVE))
			req_op_level = SMB2_OPLOCK_LEVEL_II;

		rc = smb_grant_oplock(work, req_op_level,
				      fp->persistent_id, fp,
				      le32_to_cpu(req->hdr.Id.SyncId.TreeId),
				      lc, share_ret);
		if (rc < 0)
			goto err_out1;
	}

	if (req->CreateOptions & FILE_DELETE_ON_CLOSE_LE)
		ksmbd_fd_set_delete_on_close(fp, file_info);

	if (need_truncate) {
		rc = smb2_create_truncate(&fp->filp->f_path);
		if (rc)
			goto err_out1;
	}

	if (req->CreateContextsOffset) {
		struct create_alloc_size_req *az_req;

		az_req = (struct create_alloc_size_req *)smb2_find_context_vals(req,
					SMB2_CREATE_ALLOCATION_SIZE, 4);
		if (IS_ERR(az_req)) {
			rc = PTR_ERR(az_req);
			goto err_out1;
		} else if (az_req) {
			loff_t alloc_size;
			int err;

			if (le16_to_cpu(az_req->ccontext.DataOffset) +
			    le32_to_cpu(az_req->ccontext.DataLength) <
			    sizeof(struct create_alloc_size_req)) {
				rc = -EINVAL;
				goto err_out1;
			}
			alloc_size = le64_to_cpu(az_req->AllocationSize);
			ksmbd_debug(SMB,
				    "request smb2 create allocate size : %llu\n",
				    alloc_size);
			smb_break_all_levII_oplock(work, fp, 1);
			err = vfs_fallocate(fp->filp, FALLOC_FL_KEEP_SIZE, 0,
					    alloc_size);
			if (err < 0)
				ksmbd_debug(SMB,
					    "vfs_fallocate is failed : %d\n",
					    err);
		}

		context = smb2_find_context_vals(req, SMB2_CREATE_QUERY_ON_DISK_ID, 4);
		if (IS_ERR(context)) {
			rc = PTR_ERR(context);
			goto err_out1;
		} else if (context) {
			ksmbd_debug(SMB, "get query on disk id context\n");
			query_disk_id = 1;
		}
	}

	rc = ksmbd_vfs_getattr(&path, &stat);
	if (rc)
		goto err_out1;

	if (stat.result_mask & STATX_BTIME)
		fp->create_time = ksmbd_UnixTimeToNT(stat.btime);
	else
		fp->create_time = ksmbd_UnixTimeToNT(stat.ctime);
	if (req->FileAttributes || fp->f_ci->m_fattr == 0)
		fp->f_ci->m_fattr =
			cpu_to_le32(smb2_get_dos_mode(&stat, le32_to_cpu(req->FileAttributes)));

	if (!created)
		smb2_update_xattrs(tcon, &path, fp);
	else
		smb2_new_xattrs(tcon, &path, fp);

	memcpy(fp->client_guid, conn->ClientGUID, SMB2_CLIENT_GUID_SIZE);

	if (dh_info.type == DURABLE_REQ_V2 || dh_info.type == DURABLE_REQ) {
		if (dh_info.type == DURABLE_REQ_V2 && dh_info.persistent &&
		    test_share_config_flag(work->tcon->share_conf,
					   KSMBD_SHARE_FLAG_CONTINUOUS_AVAILABILITY))
			fp->is_persistent = true;
		else
			fp->is_durable = true;

		if (dh_info.type == DURABLE_REQ_V2) {
			memcpy(fp->create_guid, dh_info.CreateGuid,
					SMB2_CREATE_GUID_SIZE);
			if (dh_info.timeout)
				fp->durable_timeout =
					min_t(unsigned int, dh_info.timeout,
					      DURABLE_HANDLE_MAX_TIMEOUT);
			else
				fp->durable_timeout = 60;
		}
	}

reconnected_fp:
	rsp->StructureSize = cpu_to_le16(89);
	rcu_read_lock();
	opinfo = rcu_dereference(fp->f_opinfo);
	rsp->OplockLevel = opinfo != NULL ? opinfo->level : 0;
	rcu_read_unlock();
	rsp->Flags = 0;
	rsp->CreateAction = cpu_to_le32(file_info);
	rsp->CreationTime = cpu_to_le64(fp->create_time);
	time = ksmbd_UnixTimeToNT(stat.atime);
	rsp->LastAccessTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.mtime);
	rsp->LastWriteTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.ctime);
	rsp->ChangeTime = cpu_to_le64(time);
	rsp->AllocationSize = S_ISDIR(stat.mode) ? 0 :
		cpu_to_le64(stat.blocks << 9);
	rsp->EndofFile = S_ISDIR(stat.mode) ? 0 : cpu_to_le64(stat.size);
	rsp->FileAttributes = fp->f_ci->m_fattr;

	rsp->Reserved2 = 0;

	rsp->PersistentFileId = fp->persistent_id;
	rsp->VolatileFileId = fp->volatile_id;

	rsp->CreateContextsOffset = 0;
	rsp->CreateContextsLength = 0;
	iov_len = offsetof(struct smb2_create_rsp, Buffer);

	/* If lease is request send lease context response */
	if (opinfo && opinfo->is_lease) {
		struct create_context *lease_ccontext;

		ksmbd_debug(SMB, "lease granted on(%s) lease state 0x%x\n",
			    name, opinfo->o_lease->state);
		rsp->OplockLevel = SMB2_OPLOCK_LEVEL_LEASE;

		lease_ccontext = (struct create_context *)rsp->Buffer;
		contxt_cnt++;
		create_lease_buf(rsp->Buffer, opinfo->o_lease);
		le32_add_cpu(&rsp->CreateContextsLength,
			     conn->vals->create_lease_size);
		iov_len += conn->vals->create_lease_size;
		next_ptr = &lease_ccontext->Next;
		next_off = conn->vals->create_lease_size;
	}

	if (maximal_access_ctxt) {
		struct create_context *mxac_ccontext;

		if (maximal_access == 0)
			ksmbd_vfs_query_maximal_access(idmap,
						       path.dentry,
						       &maximal_access);
		mxac_ccontext = (struct create_context *)(rsp->Buffer +
				le32_to_cpu(rsp->CreateContextsLength));
		contxt_cnt++;
		create_mxac_rsp_buf(rsp->Buffer +
				le32_to_cpu(rsp->CreateContextsLength),
				le32_to_cpu(maximal_access));
		le32_add_cpu(&rsp->CreateContextsLength,
			     conn->vals->create_mxac_size);
		iov_len += conn->vals->create_mxac_size;
		if (next_ptr)
			*next_ptr = cpu_to_le32(next_off);
		next_ptr = &mxac_ccontext->Next;
		next_off = conn->vals->create_mxac_size;
	}

	if (query_disk_id) {
		struct create_context *disk_id_ccontext;

		disk_id_ccontext = (struct create_context *)(rsp->Buffer +
				le32_to_cpu(rsp->CreateContextsLength));
		contxt_cnt++;
		create_disk_id_rsp_buf(rsp->Buffer +
				le32_to_cpu(rsp->CreateContextsLength),
				stat.ino, tcon->id);
		le32_add_cpu(&rsp->CreateContextsLength,
			     conn->vals->create_disk_id_size);
		iov_len += conn->vals->create_disk_id_size;
		if (next_ptr)
			*next_ptr = cpu_to_le32(next_off);
		next_ptr = &disk_id_ccontext->Next;
		next_off = conn->vals->create_disk_id_size;
	}

	if (dh_info.type == DURABLE_REQ || dh_info.type == DURABLE_REQ_V2) {
		struct create_context *durable_ccontext;

		durable_ccontext = (struct create_context *)(rsp->Buffer +
				le32_to_cpu(rsp->CreateContextsLength));
		contxt_cnt++;
		if (dh_info.type == DURABLE_REQ) {
			create_durable_rsp_buf(rsp->Buffer +
					le32_to_cpu(rsp->CreateContextsLength));
			le32_add_cpu(&rsp->CreateContextsLength,
					conn->vals->create_durable_size);
			iov_len += conn->vals->create_durable_size;
		} else {
			create_durable_v2_rsp_buf(rsp->Buffer +
					le32_to_cpu(rsp->CreateContextsLength),
					fp);
			le32_add_cpu(&rsp->CreateContextsLength,
					conn->vals->create_durable_v2_size);
			iov_len += conn->vals->create_durable_v2_size;
		}

		if (next_ptr)
			*next_ptr = cpu_to_le32(next_off);
		next_ptr = &durable_ccontext->Next;
		next_off = conn->vals->create_durable_size;
	}

	if (posix_ctxt) {
		contxt_cnt++;
		create_posix_rsp_buf(rsp->Buffer +
				le32_to_cpu(rsp->CreateContextsLength),
				fp);
		le32_add_cpu(&rsp->CreateContextsLength,
			     conn->vals->create_posix_size);
		iov_len += conn->vals->create_posix_size;
		if (next_ptr)
			*next_ptr = cpu_to_le32(next_off);
	}

	if (contxt_cnt > 0) {
		rsp->CreateContextsOffset =
			cpu_to_le32(offsetof(struct smb2_create_rsp, Buffer));
	}

err_out:
	if (rc && (file_present || created))
		ksmbd_vfs_kern_path_unlock(&parent_path, &path);

err_out1:
	ksmbd_revert_fsids(work);

err_out2:
	if (!rc) {
		ksmbd_update_fstate(&work->sess->file_table, fp, FP_INITED);
		rc = ksmbd_iov_pin_rsp(work, (void *)rsp, iov_len);
	}
	if (rc) {
		if (rc == -EINVAL)
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		else if (rc == -EOPNOTSUPP)
			rsp->hdr.Status = STATUS_NOT_SUPPORTED;
		else if (rc == -EACCES || rc == -ESTALE || rc == -EXDEV)
			rsp->hdr.Status = STATUS_ACCESS_DENIED;
		else if (rc == -ENOENT)
			rsp->hdr.Status = STATUS_OBJECT_NAME_INVALID;
		else if (rc == -EPERM)
			rsp->hdr.Status = STATUS_SHARING_VIOLATION;
		else if (rc == -EBUSY)
			rsp->hdr.Status = STATUS_DELETE_PENDING;
		else if (rc == -EBADF)
			rsp->hdr.Status = STATUS_OBJECT_NAME_NOT_FOUND;
		else if (rc == -ENOEXEC)
			rsp->hdr.Status = STATUS_DUPLICATE_OBJECTID;
		else if (rc == -ENXIO)
			rsp->hdr.Status = STATUS_NO_SUCH_DEVICE;
		else if (rc == -EEXIST)
			rsp->hdr.Status = STATUS_OBJECT_NAME_COLLISION;
		else if (rc == -EMFILE)
			rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
		if (!rsp->hdr.Status)
			rsp->hdr.Status = STATUS_UNEXPECTED_IO_ERROR;

		if (fp)
			ksmbd_fd_put(work, fp);
		smb2_set_err_rsp(work);
		ksmbd_debug(SMB, "Error response: %x\n", rsp->hdr.Status);
	}

	kfree(name);
	kfree(lc);

	return rc;
}

static int readdir_info_level_struct_sz(int info_level)
{
	switch (info_level) {
	case FILE_FULL_DIRECTORY_INFORMATION:
		return sizeof(struct file_full_directory_info);
	case FILE_BOTH_DIRECTORY_INFORMATION:
		return sizeof(struct file_both_directory_info);
	case FILE_DIRECTORY_INFORMATION:
		return sizeof(struct file_directory_info);
	case FILE_NAMES_INFORMATION:
		return sizeof(struct file_names_info);
	case FILEID_FULL_DIRECTORY_INFORMATION:
		return sizeof(struct file_id_full_dir_info);
	case FILEID_BOTH_DIRECTORY_INFORMATION:
		return sizeof(struct file_id_both_directory_info);
	case SMB_FIND_FILE_POSIX_INFO:
		return sizeof(struct smb2_posix_info);
	default:
		return -EOPNOTSUPP;
	}
}

static int dentry_name(struct ksmbd_dir_info *d_info, int info_level)
{
	switch (info_level) {
	case FILE_FULL_DIRECTORY_INFORMATION:
	{
		struct file_full_directory_info *ffdinfo;

		ffdinfo = (struct file_full_directory_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(ffdinfo->NextEntryOffset);
		d_info->name = ffdinfo->FileName;
		d_info->name_len = le32_to_cpu(ffdinfo->FileNameLength);
		return 0;
	}
	case FILE_BOTH_DIRECTORY_INFORMATION:
	{
		struct file_both_directory_info *fbdinfo;

		fbdinfo = (struct file_both_directory_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(fbdinfo->NextEntryOffset);
		d_info->name = fbdinfo->FileName;
		d_info->name_len = le32_to_cpu(fbdinfo->FileNameLength);
		return 0;
	}
	case FILE_DIRECTORY_INFORMATION:
	{
		struct file_directory_info *fdinfo;

		fdinfo = (struct file_directory_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(fdinfo->NextEntryOffset);
		d_info->name = fdinfo->FileName;
		d_info->name_len = le32_to_cpu(fdinfo->FileNameLength);
		return 0;
	}
	case FILE_NAMES_INFORMATION:
	{
		struct file_names_info *fninfo;

		fninfo = (struct file_names_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(fninfo->NextEntryOffset);
		d_info->name = fninfo->FileName;
		d_info->name_len = le32_to_cpu(fninfo->FileNameLength);
		return 0;
	}
	case FILEID_FULL_DIRECTORY_INFORMATION:
	{
		struct file_id_full_dir_info *dinfo;

		dinfo = (struct file_id_full_dir_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(dinfo->NextEntryOffset);
		d_info->name = dinfo->FileName;
		d_info->name_len = le32_to_cpu(dinfo->FileNameLength);
		return 0;
	}
	case FILEID_BOTH_DIRECTORY_INFORMATION:
	{
		struct file_id_both_directory_info *fibdinfo;

		fibdinfo = (struct file_id_both_directory_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(fibdinfo->NextEntryOffset);
		d_info->name = fibdinfo->FileName;
		d_info->name_len = le32_to_cpu(fibdinfo->FileNameLength);
		return 0;
	}
	case SMB_FIND_FILE_POSIX_INFO:
	{
		struct smb2_posix_info *posix_info;

		posix_info = (struct smb2_posix_info *)d_info->rptr;
		d_info->rptr += le32_to_cpu(posix_info->NextEntryOffset);
		d_info->name = posix_info->name;
		d_info->name_len = le32_to_cpu(posix_info->name_len);
		return 0;
	}
	default:
		return -EINVAL;
	}
}

/**
 * smb2_populate_readdir_entry() - encode directory entry in smb2 response
 * buffer
 * @conn:	connection instance
 * @info_level:	smb information level
 * @d_info:	structure included variables for query dir
 * @ksmbd_kstat:	ksmbd wrapper of dirent stat information
 *
 * if directory has many entries, find first can't read it fully.
 * find next might be called multiple times to read remaining dir entries
 *
 * Return:	0 on success, otherwise error
 */
static int smb2_populate_readdir_entry(struct ksmbd_conn *conn, int info_level,
				       struct ksmbd_dir_info *d_info,
				       struct ksmbd_kstat *ksmbd_kstat)
{
	int next_entry_offset = 0;
	char *conv_name;
	int conv_len;
	void *kstat;
	int struct_sz, rc = 0;

	conv_name = ksmbd_convert_dir_info_name(d_info,
						conn->local_nls,
						&conv_len);
	if (!conv_name)
		return -ENOMEM;

	/* Somehow the name has only terminating NULL bytes */
	if (conv_len < 0) {
		rc = -EINVAL;
		goto free_conv_name;
	}

	struct_sz = readdir_info_level_struct_sz(info_level) + conv_len;
	next_entry_offset = ALIGN(struct_sz, KSMBD_DIR_INFO_ALIGNMENT);
	d_info->last_entry_off_align = next_entry_offset - struct_sz;

	if (next_entry_offset > d_info->out_buf_len) {
		d_info->out_buf_len = 0;
		rc = -ENOSPC;
		goto free_conv_name;
	}

	kstat = d_info->wptr;
	if (info_level != FILE_NAMES_INFORMATION)
		kstat = ksmbd_vfs_init_kstat(&d_info->wptr, ksmbd_kstat);

	switch (info_level) {
	case FILE_FULL_DIRECTORY_INFORMATION:
	{
		struct file_full_directory_info *ffdinfo;

		ffdinfo = (struct file_full_directory_info *)kstat;
		ffdinfo->FileNameLength = cpu_to_le32(conv_len);
		ffdinfo->EaSize =
			smb2_get_reparse_tag_special_file(ksmbd_kstat->kstat->mode);
		if (ffdinfo->EaSize)
			ffdinfo->ExtFileAttributes = FILE_ATTRIBUTE_REPARSE_POINT_LE;
		if (d_info->hide_dot_file && d_info->name[0] == '.')
			ffdinfo->ExtFileAttributes |= FILE_ATTRIBUTE_HIDDEN_LE;
		memcpy(ffdinfo->FileName, conv_name, conv_len);
		ffdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILE_BOTH_DIRECTORY_INFORMATION:
	{
		struct file_both_directory_info *fbdinfo;

		fbdinfo = (struct file_both_directory_info *)kstat;
		fbdinfo->FileNameLength = cpu_to_le32(conv_len);
		fbdinfo->EaSize =
			smb2_get_reparse_tag_special_file(ksmbd_kstat->kstat->mode);
		if (fbdinfo->EaSize)
			fbdinfo->ExtFileAttributes = FILE_ATTRIBUTE_REPARSE_POINT_LE;
		fbdinfo->ShortNameLength = 0;
		fbdinfo->Reserved = 0;
		if (d_info->hide_dot_file && d_info->name[0] == '.')
			fbdinfo->ExtFileAttributes |= FILE_ATTRIBUTE_HIDDEN_LE;
		memcpy(fbdinfo->FileName, conv_name, conv_len);
		fbdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILE_DIRECTORY_INFORMATION:
	{
		struct file_directory_info *fdinfo;

		fdinfo = (struct file_directory_info *)kstat;
		fdinfo->FileNameLength = cpu_to_le32(conv_len);
		if (d_info->hide_dot_file && d_info->name[0] == '.')
			fdinfo->ExtFileAttributes |= FILE_ATTRIBUTE_HIDDEN_LE;
		memcpy(fdinfo->FileName, conv_name, conv_len);
		fdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILE_NAMES_INFORMATION:
	{
		struct file_names_info *fninfo;

		fninfo = (struct file_names_info *)kstat;
		fninfo->FileNameLength = cpu_to_le32(conv_len);
		memcpy(fninfo->FileName, conv_name, conv_len);
		fninfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILEID_FULL_DIRECTORY_INFORMATION:
	{
		struct file_id_full_dir_info *dinfo;

		dinfo = (struct file_id_full_dir_info *)kstat;
		dinfo->FileNameLength = cpu_to_le32(conv_len);
		dinfo->EaSize =
			smb2_get_reparse_tag_special_file(ksmbd_kstat->kstat->mode);
		if (dinfo->EaSize)
			dinfo->ExtFileAttributes = FILE_ATTRIBUTE_REPARSE_POINT_LE;
		dinfo->Reserved = 0;
		dinfo->UniqueId = cpu_to_le64(ksmbd_kstat->kstat->ino);
		if (d_info->hide_dot_file && d_info->name[0] == '.')
			dinfo->ExtFileAttributes |= FILE_ATTRIBUTE_HIDDEN_LE;
		memcpy(dinfo->FileName, conv_name, conv_len);
		dinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILEID_BOTH_DIRECTORY_INFORMATION:
	{
		struct file_id_both_directory_info *fibdinfo;

		fibdinfo = (struct file_id_both_directory_info *)kstat;
		fibdinfo->FileNameLength = cpu_to_le32(conv_len);
		fibdinfo->EaSize =
			smb2_get_reparse_tag_special_file(ksmbd_kstat->kstat->mode);
		if (fibdinfo->EaSize)
			fibdinfo->ExtFileAttributes = FILE_ATTRIBUTE_REPARSE_POINT_LE;
		fibdinfo->UniqueId = cpu_to_le64(ksmbd_kstat->kstat->ino);
		fibdinfo->ShortNameLength = 0;
		fibdinfo->Reserved = 0;
		fibdinfo->Reserved2 = cpu_to_le16(0);
		if (d_info->hide_dot_file && d_info->name[0] == '.')
			fibdinfo->ExtFileAttributes |= FILE_ATTRIBUTE_HIDDEN_LE;
		memcpy(fibdinfo->FileName, conv_name, conv_len);
		fibdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case SMB_FIND_FILE_POSIX_INFO:
	{
		struct smb2_posix_info *posix_info;
		u64 time;

		posix_info = (struct smb2_posix_info *)kstat;
		posix_info->Ignored = 0;
		posix_info->CreationTime = cpu_to_le64(ksmbd_kstat->create_time);
		time = ksmbd_UnixTimeToNT(ksmbd_kstat->kstat->ctime);
		posix_info->ChangeTime = cpu_to_le64(time);
		time = ksmbd_UnixTimeToNT(ksmbd_kstat->kstat->atime);
		posix_info->LastAccessTime = cpu_to_le64(time);
		time = ksmbd_UnixTimeToNT(ksmbd_kstat->kstat->mtime);
		posix_info->LastWriteTime = cpu_to_le64(time);
		posix_info->EndOfFile = cpu_to_le64(ksmbd_kstat->kstat->size);
		posix_info->AllocationSize = cpu_to_le64(ksmbd_kstat->kstat->blocks << 9);
		posix_info->DeviceId = cpu_to_le32(ksmbd_kstat->kstat->rdev);
		posix_info->HardLinks = cpu_to_le32(ksmbd_kstat->kstat->nlink);
		posix_info->Mode = cpu_to_le32(ksmbd_kstat->kstat->mode & 0777);
		posix_info->Inode = cpu_to_le64(ksmbd_kstat->kstat->ino);
		posix_info->DosAttributes =
			S_ISDIR(ksmbd_kstat->kstat->mode) ?
				FILE_ATTRIBUTE_DIRECTORY_LE : FILE_ATTRIBUTE_ARCHIVE_LE;
		if (d_info->hide_dot_file && d_info->name[0] == '.')
			posix_info->DosAttributes |= FILE_ATTRIBUTE_HIDDEN_LE;
		/*
		 * SidBuffer(32) contain two sids(Domain sid(16), UNIX group sid(16)).
		 * UNIX sid(16) = revision(1) + num_subauth(1) + authority(6) +
		 *		  sub_auth(4 * 1(num_subauth)) + RID(4).
		 */
		id_to_sid(from_kuid_munged(&init_user_ns, ksmbd_kstat->kstat->uid),
			  SIDUNIX_USER, (struct smb_sid *)&posix_info->SidBuffer[0]);
		id_to_sid(from_kgid_munged(&init_user_ns, ksmbd_kstat->kstat->gid),
			  SIDUNIX_GROUP, (struct smb_sid *)&posix_info->SidBuffer[16]);
		memcpy(posix_info->name, conv_name, conv_len);
		posix_info->name_len = cpu_to_le32(conv_len);
		posix_info->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}

	} /* switch (info_level) */

	d_info->last_entry_offset = d_info->data_count;
	d_info->data_count += next_entry_offset;
	d_info->out_buf_len -= next_entry_offset;
	d_info->wptr += next_entry_offset;

	ksmbd_debug(SMB,
		    "info_level : %d, buf_len :%d, next_offset : %d, data_count : %d\n",
		    info_level, d_info->out_buf_len,
		    next_entry_offset, d_info->data_count);

free_conv_name:
	kfree(conv_name);
	return rc;
}

struct smb2_query_dir_private {
	struct ksmbd_work	*work;
	char			*search_pattern;
	struct ksmbd_file	*dir_fp;

	struct ksmbd_dir_info	*d_info;
	int			info_level;
};

static void lock_dir(struct ksmbd_file *dir_fp)
{
	struct dentry *dir = dir_fp->filp->f_path.dentry;

	inode_lock_nested(d_inode(dir), I_MUTEX_PARENT);
}

static void unlock_dir(struct ksmbd_file *dir_fp)
{
	struct dentry *dir = dir_fp->filp->f_path.dentry;

	inode_unlock(d_inode(dir));
}

static int process_query_dir_entries(struct smb2_query_dir_private *priv)
{
	struct mnt_idmap	*idmap = file_mnt_idmap(priv->dir_fp->filp);
	struct kstat		kstat;
	struct ksmbd_kstat	ksmbd_kstat;
	int			rc;
	int			i;

	for (i = 0; i < priv->d_info->num_entry; i++) {
		struct dentry *dent;

		if (dentry_name(priv->d_info, priv->info_level))
			return -EINVAL;

		lock_dir(priv->dir_fp);
		dent = lookup_one(idmap, priv->d_info->name,
				  priv->dir_fp->filp->f_path.dentry,
				  priv->d_info->name_len);
		unlock_dir(priv->dir_fp);

		if (IS_ERR(dent)) {
			ksmbd_debug(SMB, "Cannot lookup `%s' [%ld]\n",
				    priv->d_info->name,
				    PTR_ERR(dent));
			continue;
		}
		if (unlikely(d_is_negative(dent))) {
			dput(dent);
			ksmbd_debug(SMB, "Negative dentry `%s'\n",
				    priv->d_info->name);
			continue;
		}

		ksmbd_kstat.kstat = &kstat;
		if (priv->info_level != FILE_NAMES_INFORMATION) {
			rc = ksmbd_vfs_fill_dentry_attrs(priv->work,
							 idmap,
							 dent,
							 &ksmbd_kstat);
			if (rc) {
				dput(dent);
				continue;
			}
		}

		rc = smb2_populate_readdir_entry(priv->work->conn,
						 priv->info_level,
						 priv->d_info,
						 &ksmbd_kstat);
		dput(dent);
		if (rc)
			return rc;
	}
	return 0;
}

static int reserve_populate_dentry(struct ksmbd_dir_info *d_info,
				   int info_level)
{
	int struct_sz;
	int conv_len;
	int next_entry_offset;

	struct_sz = readdir_info_level_struct_sz(info_level);
	if (struct_sz == -EOPNOTSUPP)
		return -EOPNOTSUPP;

	conv_len = (d_info->name_len + 1) * 2;
	next_entry_offset = ALIGN(struct_sz + conv_len,
				  KSMBD_DIR_INFO_ALIGNMENT);

	if (next_entry_offset > d_info->out_buf_len) {
		d_info->out_buf_len = 0;
		return -ENOSPC;
	}

	switch (info_level) {
	case FILE_FULL_DIRECTORY_INFORMATION:
	{
		struct file_full_directory_info *ffdinfo;

		ffdinfo = (struct file_full_directory_info *)d_info->wptr;
		memcpy(ffdinfo->FileName, d_info->name, d_info->name_len);
		ffdinfo->FileName[d_info->name_len] = 0x00;
		ffdinfo->FileNameLength = cpu_to_le32(d_info->name_len);
		ffdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILE_BOTH_DIRECTORY_INFORMATION:
	{
		struct file_both_directory_info *fbdinfo;

		fbdinfo = (struct file_both_directory_info *)d_info->wptr;
		memcpy(fbdinfo->FileName, d_info->name, d_info->name_len);
		fbdinfo->FileName[d_info->name_len] = 0x00;
		fbdinfo->FileNameLength = cpu_to_le32(d_info->name_len);
		fbdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILE_DIRECTORY_INFORMATION:
	{
		struct file_directory_info *fdinfo;

		fdinfo = (struct file_directory_info *)d_info->wptr;
		memcpy(fdinfo->FileName, d_info->name, d_info->name_len);
		fdinfo->FileName[d_info->name_len] = 0x00;
		fdinfo->FileNameLength = cpu_to_le32(d_info->name_len);
		fdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILE_NAMES_INFORMATION:
	{
		struct file_names_info *fninfo;

		fninfo = (struct file_names_info *)d_info->wptr;
		memcpy(fninfo->FileName, d_info->name, d_info->name_len);
		fninfo->FileName[d_info->name_len] = 0x00;
		fninfo->FileNameLength = cpu_to_le32(d_info->name_len);
		fninfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILEID_FULL_DIRECTORY_INFORMATION:
	{
		struct file_id_full_dir_info *dinfo;

		dinfo = (struct file_id_full_dir_info *)d_info->wptr;
		memcpy(dinfo->FileName, d_info->name, d_info->name_len);
		dinfo->FileName[d_info->name_len] = 0x00;
		dinfo->FileNameLength = cpu_to_le32(d_info->name_len);
		dinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case FILEID_BOTH_DIRECTORY_INFORMATION:
	{
		struct file_id_both_directory_info *fibdinfo;

		fibdinfo = (struct file_id_both_directory_info *)d_info->wptr;
		memcpy(fibdinfo->FileName, d_info->name, d_info->name_len);
		fibdinfo->FileName[d_info->name_len] = 0x00;
		fibdinfo->FileNameLength = cpu_to_le32(d_info->name_len);
		fibdinfo->NextEntryOffset = cpu_to_le32(next_entry_offset);
		break;
	}
	case SMB_FIND_FILE_POSIX_INFO:
	{
		struct smb2_posix_info *posix_info;

		posix_info = (struct smb2_posix_info *)d_info->wptr;
		memcpy(posix_info->name, d_info->name, d_info->name_len);
		posix_info->name[d_info->name_len] = 0x00;
		posix_info->name_len = cpu_to_le32(d_info->name_len);
		posix_info->NextEntryOffset =
			cpu_to_le32(next_entry_offset);
		break;
	}
	} /* switch (info_level) */

	d_info->num_entry++;
	d_info->out_buf_len -= next_entry_offset;
	d_info->wptr += next_entry_offset;
	return 0;
}

static bool __query_dir(struct dir_context *ctx, const char *name, int namlen,
		       loff_t offset, u64 ino, unsigned int d_type)
{
	struct ksmbd_readdir_data	*buf;
	struct smb2_query_dir_private	*priv;
	struct ksmbd_dir_info		*d_info;
	int				rc;

	buf	= container_of(ctx, struct ksmbd_readdir_data, ctx);
	priv	= buf->private;
	d_info	= priv->d_info;

	/* dot and dotdot entries are already reserved */
	if (!strcmp(".", name) || !strcmp("..", name))
		return true;
	if (ksmbd_share_veto_filename(priv->work->tcon->share_conf, name))
		return true;
	if (!match_pattern(name, namlen, priv->search_pattern))
		return true;

	d_info->name		= name;
	d_info->name_len	= namlen;
	rc = reserve_populate_dentry(d_info, priv->info_level);
	if (rc)
		return false;
	if (d_info->flags & SMB2_RETURN_SINGLE_ENTRY)
		d_info->out_buf_len = 0;
	return true;
}

static int verify_info_level(int info_level)
{
	switch (info_level) {
	case FILE_FULL_DIRECTORY_INFORMATION:
	case FILE_BOTH_DIRECTORY_INFORMATION:
	case FILE_DIRECTORY_INFORMATION:
	case FILE_NAMES_INFORMATION:
	case FILEID_FULL_DIRECTORY_INFORMATION:
	case FILEID_BOTH_DIRECTORY_INFORMATION:
	case SMB_FIND_FILE_POSIX_INFO:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int smb2_resp_buf_len(struct ksmbd_work *work, unsigned short hdr2_len)
{
	int free_len;

	free_len = (int)(work->response_sz -
		(get_rfc1002_len(work->response_buf) + 4)) - hdr2_len;
	return free_len;
}

static int smb2_calc_max_out_buf_len(struct ksmbd_work *work,
				     unsigned short hdr2_len,
				     unsigned int out_buf_len)
{
	int free_len;

	if (out_buf_len > work->conn->vals->max_trans_size)
		return -EINVAL;

	free_len = smb2_resp_buf_len(work, hdr2_len);
	if (free_len < 0)
		return -EINVAL;

	return min_t(int, out_buf_len, free_len);
}

int smb2_query_dir(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_query_directory_req *req;
	struct smb2_query_directory_rsp *rsp;
	struct ksmbd_share_config *share = work->tcon->share_conf;
	struct ksmbd_file *dir_fp = NULL;
	struct ksmbd_dir_info d_info;
	int rc = 0;
	char *srch_ptr = NULL;
	unsigned char srch_flag;
	int buffer_sz;
	struct smb2_query_dir_private query_dir_private = {NULL, };

	WORK_BUFFERS(work, req, rsp);

	if (ksmbd_override_fsids(work)) {
		rsp->hdr.Status = STATUS_NO_MEMORY;
		smb2_set_err_rsp(work);
		return -ENOMEM;
	}

	rc = verify_info_level(req->FileInformationClass);
	if (rc) {
		rc = -EFAULT;
		goto err_out2;
	}

	dir_fp = ksmbd_lookup_fd_slow(work, req->VolatileFileId, req->PersistentFileId);
	if (!dir_fp) {
		rc = -EBADF;
		goto err_out2;
	}

	if (!(dir_fp->daccess & FILE_LIST_DIRECTORY_LE) ||
	    inode_permission(file_mnt_idmap(dir_fp->filp),
			     file_inode(dir_fp->filp),
			     MAY_READ | MAY_EXEC)) {
		pr_err("no right to enumerate directory (%pD)\n", dir_fp->filp);
		rc = -EACCES;
		goto err_out2;
	}

	if (!S_ISDIR(file_inode(dir_fp->filp)->i_mode)) {
		pr_err("can't do query dir for a file\n");
		rc = -EINVAL;
		goto err_out2;
	}

	srch_flag = req->Flags;
	srch_ptr = smb_strndup_from_utf16((char *)req + le16_to_cpu(req->FileNameOffset),
					  le16_to_cpu(req->FileNameLength), 1,
					  conn->local_nls);
	if (IS_ERR(srch_ptr)) {
		ksmbd_debug(SMB, "Search Pattern not found\n");
		rc = -EINVAL;
		goto err_out2;
	} else {
		ksmbd_debug(SMB, "Search pattern is %s\n", srch_ptr);
	}

	if (srch_flag & SMB2_REOPEN || srch_flag & SMB2_RESTART_SCANS) {
		ksmbd_debug(SMB, "Restart directory scan\n");
		generic_file_llseek(dir_fp->filp, 0, SEEK_SET);
	}

	memset(&d_info, 0, sizeof(struct ksmbd_dir_info));
	d_info.wptr = (char *)rsp->Buffer;
	d_info.rptr = (char *)rsp->Buffer;
	d_info.out_buf_len =
		smb2_calc_max_out_buf_len(work, 8,
					  le32_to_cpu(req->OutputBufferLength));
	if (d_info.out_buf_len < 0) {
		rc = -EINVAL;
		goto err_out;
	}
	d_info.flags = srch_flag;

	/*
	 * reserve dot and dotdot entries in head of buffer
	 * in first response
	 */
	rc = ksmbd_populate_dot_dotdot_entries(work, req->FileInformationClass,
					       dir_fp, &d_info, srch_ptr,
					       smb2_populate_readdir_entry);
	if (rc == -ENOSPC)
		rc = 0;
	else if (rc)
		goto err_out;

	if (test_share_config_flag(share, KSMBD_SHARE_FLAG_HIDE_DOT_FILES))
		d_info.hide_dot_file = true;

	buffer_sz				= d_info.out_buf_len;
	d_info.rptr				= d_info.wptr;
	query_dir_private.work			= work;
	query_dir_private.search_pattern	= srch_ptr;
	query_dir_private.dir_fp		= dir_fp;
	query_dir_private.d_info		= &d_info;
	query_dir_private.info_level		= req->FileInformationClass;
	dir_fp->readdir_data.private		= &query_dir_private;
	set_ctx_actor(&dir_fp->readdir_data.ctx, __query_dir);

	rc = iterate_dir(dir_fp->filp, &dir_fp->readdir_data.ctx);
	/*
	 * req->OutputBufferLength is too small to contain even one entry.
	 * In this case, it immediately returns OutputBufferLength 0 to client.
	 */
	if (!d_info.out_buf_len && !d_info.num_entry)
		goto no_buf_len;
	if (rc > 0 || rc == -ENOSPC)
		rc = 0;
	else if (rc)
		goto err_out;

	d_info.wptr = d_info.rptr;
	d_info.out_buf_len = buffer_sz;
	rc = process_query_dir_entries(&query_dir_private);
	if (rc)
		goto err_out;

	if (!d_info.data_count && d_info.out_buf_len >= 0) {
		if (srch_flag & SMB2_RETURN_SINGLE_ENTRY && !is_asterisk(srch_ptr)) {
			rsp->hdr.Status = STATUS_NO_SUCH_FILE;
		} else {
			dir_fp->dot_dotdot[0] = dir_fp->dot_dotdot[1] = 0;
			rsp->hdr.Status = STATUS_NO_MORE_FILES;
		}
		rsp->StructureSize = cpu_to_le16(9);
		rsp->OutputBufferOffset = cpu_to_le16(0);
		rsp->OutputBufferLength = cpu_to_le32(0);
		rsp->Buffer[0] = 0;
		rc = ksmbd_iov_pin_rsp(work, (void *)rsp,
				       offsetof(struct smb2_query_directory_rsp, Buffer)
				       + 1);
		if (rc)
			goto err_out;
	} else {
no_buf_len:
		((struct file_directory_info *)
		((char *)rsp->Buffer + d_info.last_entry_offset))
		->NextEntryOffset = 0;
		if (d_info.data_count >= d_info.last_entry_off_align)
			d_info.data_count -= d_info.last_entry_off_align;

		rsp->StructureSize = cpu_to_le16(9);
		rsp->OutputBufferOffset = cpu_to_le16(72);
		rsp->OutputBufferLength = cpu_to_le32(d_info.data_count);
		rc = ksmbd_iov_pin_rsp(work, (void *)rsp,
				       offsetof(struct smb2_query_directory_rsp, Buffer) +
				       d_info.data_count);
		if (rc)
			goto err_out;
	}

	kfree(srch_ptr);
	ksmbd_fd_put(work, dir_fp);
	ksmbd_revert_fsids(work);
	return 0;

err_out:
	pr_err("error while processing smb2 query dir rc = %d\n", rc);
	kfree(srch_ptr);

err_out2:
	if (rc == -EINVAL)
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
	else if (rc == -EACCES)
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
	else if (rc == -ENOENT)
		rsp->hdr.Status = STATUS_NO_SUCH_FILE;
	else if (rc == -EBADF)
		rsp->hdr.Status = STATUS_FILE_CLOSED;
	else if (rc == -ENOMEM)
		rsp->hdr.Status = STATUS_NO_MEMORY;
	else if (rc == -EFAULT)
		rsp->hdr.Status = STATUS_INVALID_INFO_CLASS;
	else if (rc == -EIO)
		rsp->hdr.Status = STATUS_FILE_CORRUPT_ERROR;
	if (!rsp->hdr.Status)
		rsp->hdr.Status = STATUS_UNEXPECTED_IO_ERROR;

	smb2_set_err_rsp(work);
	ksmbd_fd_put(work, dir_fp);
	ksmbd_revert_fsids(work);
	return 0;
}

/**
 * buffer_check_err() - helper function to check buffer errors
 * @reqOutputBufferLength:	max buffer length expected in command response
 * @rsp:		query info response buffer contains output buffer length
 * @rsp_org:		base response buffer pointer in case of chained response
 *
 * Return:	0 on success, otherwise error
 */
static int buffer_check_err(int reqOutputBufferLength,
			    struct smb2_query_info_rsp *rsp,
			    void *rsp_org)
{
	if (reqOutputBufferLength < le32_to_cpu(rsp->OutputBufferLength)) {
		pr_err("Invalid Buffer Size Requested\n");
		rsp->hdr.Status = STATUS_INFO_LENGTH_MISMATCH;
		*(__be32 *)rsp_org = cpu_to_be32(sizeof(struct smb2_hdr));
		return -EINVAL;
	}
	return 0;
}

static void get_standard_info_pipe(struct smb2_query_info_rsp *rsp,
				   void *rsp_org)
{
	struct smb2_file_standard_info *sinfo;

	sinfo = (struct smb2_file_standard_info *)rsp->Buffer;

	sinfo->AllocationSize = cpu_to_le64(4096);
	sinfo->EndOfFile = cpu_to_le64(0);
	sinfo->NumberOfLinks = cpu_to_le32(1);
	sinfo->DeletePending = 1;
	sinfo->Directory = 0;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_standard_info));
}

static void get_internal_info_pipe(struct smb2_query_info_rsp *rsp, u64 num,
				   void *rsp_org)
{
	struct smb2_file_internal_info *file_info;

	file_info = (struct smb2_file_internal_info *)rsp->Buffer;

	/* any unique number */
	file_info->IndexNumber = cpu_to_le64(num | (1ULL << 63));
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_internal_info));
}

static int smb2_get_info_file_pipe(struct ksmbd_session *sess,
				   struct smb2_query_info_req *req,
				   struct smb2_query_info_rsp *rsp,
				   void *rsp_org)
{
	u64 id;
	int rc;

	/*
	 * Windows can sometime send query file info request on
	 * pipe without opening it, checking error condition here
	 */
	id = req->VolatileFileId;
	if (!ksmbd_session_rpc_method(sess, id))
		return -ENOENT;

	ksmbd_debug(SMB, "FileInfoClass %u, FileId 0x%llx\n",
		    req->FileInfoClass, req->VolatileFileId);

	switch (req->FileInfoClass) {
	case FILE_STANDARD_INFORMATION:
		get_standard_info_pipe(rsp, rsp_org);
		rc = buffer_check_err(le32_to_cpu(req->OutputBufferLength),
				      rsp, rsp_org);
		break;
	case FILE_INTERNAL_INFORMATION:
		get_internal_info_pipe(rsp, id, rsp_org);
		rc = buffer_check_err(le32_to_cpu(req->OutputBufferLength),
				      rsp, rsp_org);
		break;
	default:
		ksmbd_debug(SMB, "smb2_info_file_pipe for %u not supported\n",
			    req->FileInfoClass);
		rc = -EOPNOTSUPP;
	}
	return rc;
}

/**
 * smb2_get_ea() - handler for smb2 get extended attribute command
 * @work:	smb work containing query info command buffer
 * @fp:		ksmbd_file pointer
 * @req:	get extended attribute request
 * @rsp:	response buffer pointer
 * @rsp_org:	base response buffer pointer in case of chained response
 *
 * Return:	0 on success, otherwise error
 */
static int smb2_get_ea(struct ksmbd_work *work, struct ksmbd_file *fp,
		       struct smb2_query_info_req *req,
		       struct smb2_query_info_rsp *rsp, void *rsp_org)
{
	struct smb2_ea_info *eainfo, *prev_eainfo;
	char *name, *ptr, *xattr_list = NULL, *buf;
	int rc, name_len, value_len, xattr_list_len, idx;
	ssize_t buf_free_len, alignment_bytes, next_offset, rsp_data_cnt = 0;
	struct smb2_ea_info_req *ea_req = NULL;
	const struct path *path;
	struct mnt_idmap *idmap = file_mnt_idmap(fp->filp);

	if (!(fp->daccess & FILE_READ_EA_LE)) {
		pr_err("Not permitted to read ext attr : 0x%x\n",
		       fp->daccess);
		return -EACCES;
	}

	path = &fp->filp->f_path;
	/* single EA entry is requested with given user.* name */
	if (req->InputBufferLength) {
		if (le32_to_cpu(req->InputBufferLength) <=
		    sizeof(struct smb2_ea_info_req))
			return -EINVAL;

		ea_req = (struct smb2_ea_info_req *)((char *)req +
						     le16_to_cpu(req->InputBufferOffset));
	} else {
		/* need to send all EAs, if no specific EA is requested*/
		if (le32_to_cpu(req->Flags) & SL_RETURN_SINGLE_ENTRY)
			ksmbd_debug(SMB,
				    "All EAs are requested but need to send single EA entry in rsp flags 0x%x\n",
				    le32_to_cpu(req->Flags));
	}

	buf_free_len =
		smb2_calc_max_out_buf_len(work, 8,
					  le32_to_cpu(req->OutputBufferLength));
	if (buf_free_len < 0)
		return -EINVAL;

	rc = ksmbd_vfs_listxattr(path->dentry, &xattr_list);
	if (rc < 0) {
		rsp->hdr.Status = STATUS_INVALID_HANDLE;
		goto out;
	} else if (!rc) { /* there is no EA in the file */
		ksmbd_debug(SMB, "no ea data in the file\n");
		goto done;
	}
	xattr_list_len = rc;

	ptr = (char *)rsp->Buffer;
	eainfo = (struct smb2_ea_info *)ptr;
	prev_eainfo = eainfo;
	idx = 0;

	while (idx < xattr_list_len) {
		name = xattr_list + idx;
		name_len = strlen(name);

		ksmbd_debug(SMB, "%s, len %d\n", name, name_len);
		idx += name_len + 1;

		/*
		 * CIFS does not support EA other than user.* namespace,
		 * still keep the framework generic, to list other attrs
		 * in future.
		 */
		if (strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
			continue;

		if (!strncmp(&name[XATTR_USER_PREFIX_LEN], STREAM_PREFIX,
			     STREAM_PREFIX_LEN))
			continue;

		if (req->InputBufferLength &&
		    strncmp(&name[XATTR_USER_PREFIX_LEN], ea_req->name,
			    ea_req->EaNameLength))
			continue;

		if (!strncmp(&name[XATTR_USER_PREFIX_LEN],
			     DOS_ATTRIBUTE_PREFIX, DOS_ATTRIBUTE_PREFIX_LEN))
			continue;

		if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
			name_len -= XATTR_USER_PREFIX_LEN;

		ptr = eainfo->name + name_len + 1;
		buf_free_len -= (offsetof(struct smb2_ea_info, name) +
				name_len + 1);
		/* bailout if xattr can't fit in buf_free_len */
		value_len = ksmbd_vfs_getxattr(idmap, path->dentry,
					       name, &buf);
		if (value_len <= 0) {
			rc = -ENOENT;
			rsp->hdr.Status = STATUS_INVALID_HANDLE;
			goto out;
		}

		buf_free_len -= value_len;
		if (buf_free_len < 0) {
			kfree(buf);
			break;
		}

		memcpy(ptr, buf, value_len);
		kfree(buf);

		ptr += value_len;
		eainfo->Flags = 0;
		eainfo->EaNameLength = name_len;

		if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
			memcpy(eainfo->name, &name[XATTR_USER_PREFIX_LEN],
			       name_len);
		else
			memcpy(eainfo->name, name, name_len);

		eainfo->name[name_len] = '\0';
		eainfo->EaValueLength = cpu_to_le16(value_len);
		next_offset = offsetof(struct smb2_ea_info, name) +
			name_len + 1 + value_len;

		/* align next xattr entry at 4 byte bundary */
		alignment_bytes = ((next_offset + 3) & ~3) - next_offset;
		if (alignment_bytes) {
			memset(ptr, '\0', alignment_bytes);
			ptr += alignment_bytes;
			next_offset += alignment_bytes;
			buf_free_len -= alignment_bytes;
		}
		eainfo->NextEntryOffset = cpu_to_le32(next_offset);
		prev_eainfo = eainfo;
		eainfo = (struct smb2_ea_info *)ptr;
		rsp_data_cnt += next_offset;

		if (req->InputBufferLength) {
			ksmbd_debug(SMB, "single entry requested\n");
			break;
		}
	}

	/* no more ea entries */
	prev_eainfo->NextEntryOffset = 0;
done:
	rc = 0;
	if (rsp_data_cnt == 0)
		rsp->hdr.Status = STATUS_NO_EAS_ON_FILE;
	rsp->OutputBufferLength = cpu_to_le32(rsp_data_cnt);
out:
	kvfree(xattr_list);
	return rc;
}

static void get_file_access_info(struct smb2_query_info_rsp *rsp,
				 struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_access_info *file_info;

	file_info = (struct smb2_file_access_info *)rsp->Buffer;
	file_info->AccessFlags = fp->daccess;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_access_info));
}

static int get_file_basic_info(struct smb2_query_info_rsp *rsp,
			       struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_basic_info *basic_info;
	struct kstat stat;
	u64 time;
	int ret;

	if (!(fp->daccess & FILE_READ_ATTRIBUTES_LE)) {
		pr_err("no right to read the attributes : 0x%x\n",
		       fp->daccess);
		return -EACCES;
	}

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	basic_info = (struct smb2_file_basic_info *)rsp->Buffer;
	basic_info->CreationTime = cpu_to_le64(fp->create_time);
	time = ksmbd_UnixTimeToNT(stat.atime);
	basic_info->LastAccessTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.mtime);
	basic_info->LastWriteTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.ctime);
	basic_info->ChangeTime = cpu_to_le64(time);
	basic_info->Attributes = fp->f_ci->m_fattr;
	basic_info->Pad1 = 0;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_basic_info));
	return 0;
}

static int get_file_standard_info(struct smb2_query_info_rsp *rsp,
				  struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_standard_info *sinfo;
	unsigned int delete_pending;
	struct kstat stat;
	int ret;

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	sinfo = (struct smb2_file_standard_info *)rsp->Buffer;
	delete_pending = ksmbd_inode_pending_delete(fp);

	sinfo->AllocationSize = cpu_to_le64(stat.blocks << 9);
	sinfo->EndOfFile = S_ISDIR(stat.mode) ? 0 : cpu_to_le64(stat.size);
	sinfo->NumberOfLinks = cpu_to_le32(get_nlink(&stat) - delete_pending);
	sinfo->DeletePending = delete_pending;
	sinfo->Directory = S_ISDIR(stat.mode) ? 1 : 0;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_standard_info));

	return 0;
}

static void get_file_alignment_info(struct smb2_query_info_rsp *rsp,
				    void *rsp_org)
{
	struct smb2_file_alignment_info *file_info;

	file_info = (struct smb2_file_alignment_info *)rsp->Buffer;
	file_info->AlignmentRequirement = 0;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_alignment_info));
}

static int get_file_all_info(struct ksmbd_work *work,
			     struct smb2_query_info_rsp *rsp,
			     struct ksmbd_file *fp,
			     void *rsp_org)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_file_all_info *file_info;
	unsigned int delete_pending;
	struct kstat stat;
	int conv_len;
	char *filename;
	u64 time;
	int ret;

	if (!(fp->daccess & FILE_READ_ATTRIBUTES_LE)) {
		ksmbd_debug(SMB, "no right to read the attributes : 0x%x\n",
			    fp->daccess);
		return -EACCES;
	}

	filename = convert_to_nt_pathname(work->tcon->share_conf, &fp->filp->f_path);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	ksmbd_debug(SMB, "filename = %s\n", filename);
	delete_pending = ksmbd_inode_pending_delete(fp);
	file_info = (struct smb2_file_all_info *)rsp->Buffer;

	file_info->CreationTime = cpu_to_le64(fp->create_time);
	time = ksmbd_UnixTimeToNT(stat.atime);
	file_info->LastAccessTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.mtime);
	file_info->LastWriteTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.ctime);
	file_info->ChangeTime = cpu_to_le64(time);
	file_info->Attributes = fp->f_ci->m_fattr;
	file_info->Pad1 = 0;
	file_info->AllocationSize =
		cpu_to_le64(stat.blocks << 9);
	file_info->EndOfFile = S_ISDIR(stat.mode) ? 0 : cpu_to_le64(stat.size);
	file_info->NumberOfLinks =
			cpu_to_le32(get_nlink(&stat) - delete_pending);
	file_info->DeletePending = delete_pending;
	file_info->Directory = S_ISDIR(stat.mode) ? 1 : 0;
	file_info->Pad2 = 0;
	file_info->IndexNumber = cpu_to_le64(stat.ino);
	file_info->EASize = 0;
	file_info->AccessFlags = fp->daccess;
	file_info->CurrentByteOffset = cpu_to_le64(fp->filp->f_pos);
	file_info->Mode = fp->coption;
	file_info->AlignmentRequirement = 0;
	conv_len = smbConvertToUTF16((__le16 *)file_info->FileName, filename,
				     PATH_MAX, conn->local_nls, 0);
	conv_len *= 2;
	file_info->FileNameLength = cpu_to_le32(conv_len);
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_all_info) + conv_len - 1);
	kfree(filename);
	return 0;
}

static void get_file_alternate_info(struct ksmbd_work *work,
				    struct smb2_query_info_rsp *rsp,
				    struct ksmbd_file *fp,
				    void *rsp_org)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_file_alt_name_info *file_info;
	struct dentry *dentry = fp->filp->f_path.dentry;
	int conv_len;

	spin_lock(&dentry->d_lock);
	file_info = (struct smb2_file_alt_name_info *)rsp->Buffer;
	conv_len = ksmbd_extract_shortname(conn,
					   dentry->d_name.name,
					   file_info->FileName);
	spin_unlock(&dentry->d_lock);
	file_info->FileNameLength = cpu_to_le32(conv_len);
	rsp->OutputBufferLength =
		cpu_to_le32(struct_size(file_info, FileName, conv_len));
}

static int get_file_stream_info(struct ksmbd_work *work,
				struct smb2_query_info_rsp *rsp,
				struct ksmbd_file *fp,
				void *rsp_org)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_file_stream_info *file_info;
	char *stream_name, *xattr_list = NULL, *stream_buf;
	struct kstat stat;
	const struct path *path = &fp->filp->f_path;
	ssize_t xattr_list_len;
	int nbytes = 0, streamlen, stream_name_len, next, idx = 0;
	int buf_free_len;
	struct smb2_query_info_req *req = ksmbd_req_buf_next(work);
	int ret;

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	file_info = (struct smb2_file_stream_info *)rsp->Buffer;

	buf_free_len =
		smb2_calc_max_out_buf_len(work, 8,
					  le32_to_cpu(req->OutputBufferLength));
	if (buf_free_len < 0)
		goto out;

	xattr_list_len = ksmbd_vfs_listxattr(path->dentry, &xattr_list);
	if (xattr_list_len < 0) {
		goto out;
	} else if (!xattr_list_len) {
		ksmbd_debug(SMB, "empty xattr in the file\n");
		goto out;
	}

	while (idx < xattr_list_len) {
		stream_name = xattr_list + idx;
		streamlen = strlen(stream_name);
		idx += streamlen + 1;

		ksmbd_debug(SMB, "%s, len %d\n", stream_name, streamlen);

		if (strncmp(&stream_name[XATTR_USER_PREFIX_LEN],
			    STREAM_PREFIX, STREAM_PREFIX_LEN))
			continue;

		stream_name_len = streamlen - (XATTR_USER_PREFIX_LEN +
				STREAM_PREFIX_LEN);
		streamlen = stream_name_len;

		/* plus : size */
		streamlen += 1;
		stream_buf = kmalloc(streamlen + 1, GFP_KERNEL);
		if (!stream_buf)
			break;

		streamlen = snprintf(stream_buf, streamlen + 1,
				     ":%s", &stream_name[XATTR_NAME_STREAM_LEN]);

		next = sizeof(struct smb2_file_stream_info) + streamlen * 2;
		if (next > buf_free_len) {
			kfree(stream_buf);
			break;
		}

		file_info = (struct smb2_file_stream_info *)&rsp->Buffer[nbytes];
		streamlen  = smbConvertToUTF16((__le16 *)file_info->StreamName,
					       stream_buf, streamlen,
					       conn->local_nls, 0);
		streamlen *= 2;
		kfree(stream_buf);
		file_info->StreamNameLength = cpu_to_le32(streamlen);
		file_info->StreamSize = cpu_to_le64(stream_name_len);
		file_info->StreamAllocationSize = cpu_to_le64(stream_name_len);

		nbytes += next;
		buf_free_len -= next;
		file_info->NextEntryOffset = cpu_to_le32(next);
	}

out:
	if (!S_ISDIR(stat.mode) &&
	    buf_free_len >= sizeof(struct smb2_file_stream_info) + 7 * 2) {
		file_info = (struct smb2_file_stream_info *)
			&rsp->Buffer[nbytes];
		streamlen = smbConvertToUTF16((__le16 *)file_info->StreamName,
					      "::$DATA", 7, conn->local_nls, 0);
		streamlen *= 2;
		file_info->StreamNameLength = cpu_to_le32(streamlen);
		file_info->StreamSize = cpu_to_le64(stat.size);
		file_info->StreamAllocationSize = cpu_to_le64(stat.blocks << 9);
		nbytes += sizeof(struct smb2_file_stream_info) + streamlen;
	}

	/* last entry offset should be 0 */
	file_info->NextEntryOffset = 0;
	kvfree(xattr_list);

	rsp->OutputBufferLength = cpu_to_le32(nbytes);

	return 0;
}

static int get_file_internal_info(struct smb2_query_info_rsp *rsp,
				  struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_internal_info *file_info;
	struct kstat stat;
	int ret;

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	file_info = (struct smb2_file_internal_info *)rsp->Buffer;
	file_info->IndexNumber = cpu_to_le64(stat.ino);
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_internal_info));

	return 0;
}

static int get_file_network_open_info(struct smb2_query_info_rsp *rsp,
				      struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_ntwrk_info *file_info;
	struct kstat stat;
	u64 time;
	int ret;

	if (!(fp->daccess & FILE_READ_ATTRIBUTES_LE)) {
		pr_err("no right to read the attributes : 0x%x\n",
		       fp->daccess);
		return -EACCES;
	}

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	file_info = (struct smb2_file_ntwrk_info *)rsp->Buffer;

	file_info->CreationTime = cpu_to_le64(fp->create_time);
	time = ksmbd_UnixTimeToNT(stat.atime);
	file_info->LastAccessTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.mtime);
	file_info->LastWriteTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.ctime);
	file_info->ChangeTime = cpu_to_le64(time);
	file_info->Attributes = fp->f_ci->m_fattr;
	file_info->AllocationSize = cpu_to_le64(stat.blocks << 9);
	file_info->EndOfFile = S_ISDIR(stat.mode) ? 0 : cpu_to_le64(stat.size);
	file_info->Reserved = cpu_to_le32(0);
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_ntwrk_info));
	return 0;
}

static void get_file_ea_info(struct smb2_query_info_rsp *rsp, void *rsp_org)
{
	struct smb2_file_ea_info *file_info;

	file_info = (struct smb2_file_ea_info *)rsp->Buffer;
	file_info->EASize = 0;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_ea_info));
}

static void get_file_position_info(struct smb2_query_info_rsp *rsp,
				   struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_pos_info *file_info;

	file_info = (struct smb2_file_pos_info *)rsp->Buffer;
	file_info->CurrentByteOffset = cpu_to_le64(fp->filp->f_pos);
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_pos_info));
}

static void get_file_mode_info(struct smb2_query_info_rsp *rsp,
			       struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_mode_info *file_info;

	file_info = (struct smb2_file_mode_info *)rsp->Buffer;
	file_info->Mode = fp->coption & FILE_MODE_INFO_MASK;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_mode_info));
}

static int get_file_compression_info(struct smb2_query_info_rsp *rsp,
				     struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_comp_info *file_info;
	struct kstat stat;
	int ret;

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	file_info = (struct smb2_file_comp_info *)rsp->Buffer;
	file_info->CompressedFileSize = cpu_to_le64(stat.blocks << 9);
	file_info->CompressionFormat = COMPRESSION_FORMAT_NONE;
	file_info->CompressionUnitShift = 0;
	file_info->ChunkShift = 0;
	file_info->ClusterShift = 0;
	memset(&file_info->Reserved[0], 0, 3);

	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_comp_info));

	return 0;
}

static int get_file_attribute_tag_info(struct smb2_query_info_rsp *rsp,
				       struct ksmbd_file *fp, void *rsp_org)
{
	struct smb2_file_attr_tag_info *file_info;

	if (!(fp->daccess & FILE_READ_ATTRIBUTES_LE)) {
		pr_err("no right to read the attributes : 0x%x\n",
		       fp->daccess);
		return -EACCES;
	}

	file_info = (struct smb2_file_attr_tag_info *)rsp->Buffer;
	file_info->FileAttributes = fp->f_ci->m_fattr;
	file_info->ReparseTag = 0;
	rsp->OutputBufferLength =
		cpu_to_le32(sizeof(struct smb2_file_attr_tag_info));
	return 0;
}

static int find_file_posix_info(struct smb2_query_info_rsp *rsp,
				struct ksmbd_file *fp, void *rsp_org)
{
	struct smb311_posix_qinfo *file_info;
	struct inode *inode = file_inode(fp->filp);
	struct mnt_idmap *idmap = file_mnt_idmap(fp->filp);
	vfsuid_t vfsuid = i_uid_into_vfsuid(idmap, inode);
	vfsgid_t vfsgid = i_gid_into_vfsgid(idmap, inode);
	struct kstat stat;
	u64 time;
	int out_buf_len = sizeof(struct smb311_posix_qinfo) + 32;
	int ret;

	ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		return ret;

	file_info = (struct smb311_posix_qinfo *)rsp->Buffer;
	file_info->CreationTime = cpu_to_le64(fp->create_time);
	time = ksmbd_UnixTimeToNT(stat.atime);
	file_info->LastAccessTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.mtime);
	file_info->LastWriteTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(stat.ctime);
	file_info->ChangeTime = cpu_to_le64(time);
	file_info->DosAttributes = fp->f_ci->m_fattr;
	file_info->Inode = cpu_to_le64(stat.ino);
	file_info->EndOfFile = cpu_to_le64(stat.size);
	file_info->AllocationSize = cpu_to_le64(stat.blocks << 9);
	file_info->HardLinks = cpu_to_le32(stat.nlink);
	file_info->Mode = cpu_to_le32(stat.mode & 0777);
	file_info->DeviceId = cpu_to_le32(stat.rdev);

	/*
	 * Sids(32) contain two sids(Domain sid(16), UNIX group sid(16)).
	 * UNIX sid(16) = revision(1) + num_subauth(1) + authority(6) +
	 *		  sub_auth(4 * 1(num_subauth)) + RID(4).
	 */
	id_to_sid(from_kuid_munged(&init_user_ns, vfsuid_into_kuid(vfsuid)),
		  SIDUNIX_USER, (struct smb_sid *)&file_info->Sids[0]);
	id_to_sid(from_kgid_munged(&init_user_ns, vfsgid_into_kgid(vfsgid)),
		  SIDUNIX_GROUP, (struct smb_sid *)&file_info->Sids[16]);

	rsp->OutputBufferLength = cpu_to_le32(out_buf_len);

	return 0;
}

static int smb2_get_info_file(struct ksmbd_work *work,
			      struct smb2_query_info_req *req,
			      struct smb2_query_info_rsp *rsp)
{
	struct ksmbd_file *fp;
	int fileinfoclass = 0;
	int rc = 0;
	unsigned int id = KSMBD_NO_FID, pid = KSMBD_NO_FID;

	if (test_share_config_flag(work->tcon->share_conf,
				   KSMBD_SHARE_FLAG_PIPE)) {
		/* smb2 info file called for pipe */
		return smb2_get_info_file_pipe(work->sess, req, rsp,
					       work->response_buf);
	}

	if (work->next_smb2_rcv_hdr_off) {
		if (!has_file_id(req->VolatileFileId)) {
			ksmbd_debug(SMB, "Compound request set FID = %llu\n",
				    work->compound_fid);
			id = work->compound_fid;
			pid = work->compound_pfid;
		}
	}

	if (!has_file_id(id)) {
		id = req->VolatileFileId;
		pid = req->PersistentFileId;
	}

	fp = ksmbd_lookup_fd_slow(work, id, pid);
	if (!fp)
		return -ENOENT;

	fileinfoclass = req->FileInfoClass;

	switch (fileinfoclass) {
	case FILE_ACCESS_INFORMATION:
		get_file_access_info(rsp, fp, work->response_buf);
		break;

	case FILE_BASIC_INFORMATION:
		rc = get_file_basic_info(rsp, fp, work->response_buf);
		break;

	case FILE_STANDARD_INFORMATION:
		rc = get_file_standard_info(rsp, fp, work->response_buf);
		break;

	case FILE_ALIGNMENT_INFORMATION:
		get_file_alignment_info(rsp, work->response_buf);
		break;

	case FILE_ALL_INFORMATION:
		rc = get_file_all_info(work, rsp, fp, work->response_buf);
		break;

	case FILE_ALTERNATE_NAME_INFORMATION:
		get_file_alternate_info(work, rsp, fp, work->response_buf);
		break;

	case FILE_STREAM_INFORMATION:
		rc = get_file_stream_info(work, rsp, fp, work->response_buf);
		break;

	case FILE_INTERNAL_INFORMATION:
		rc = get_file_internal_info(rsp, fp, work->response_buf);
		break;

	case FILE_NETWORK_OPEN_INFORMATION:
		rc = get_file_network_open_info(rsp, fp, work->response_buf);
		break;

	case FILE_EA_INFORMATION:
		get_file_ea_info(rsp, work->response_buf);
		break;

	case FILE_FULL_EA_INFORMATION:
		rc = smb2_get_ea(work, fp, req, rsp, work->response_buf);
		break;

	case FILE_POSITION_INFORMATION:
		get_file_position_info(rsp, fp, work->response_buf);
		break;

	case FILE_MODE_INFORMATION:
		get_file_mode_info(rsp, fp, work->response_buf);
		break;

	case FILE_COMPRESSION_INFORMATION:
		rc = get_file_compression_info(rsp, fp, work->response_buf);
		break;

	case FILE_ATTRIBUTE_TAG_INFORMATION:
		rc = get_file_attribute_tag_info(rsp, fp, work->response_buf);
		break;
	case SMB_FIND_FILE_POSIX_INFO:
		if (!work->tcon->posix_extensions) {
			pr_err("client doesn't negotiate with SMB3.1.1 POSIX Extensions\n");
			rc = -EOPNOTSUPP;
		} else {
			rc = find_file_posix_info(rsp, fp, work->response_buf);
		}
		break;
	default:
		ksmbd_debug(SMB, "fileinfoclass %d not supported yet\n",
			    fileinfoclass);
		rc = -EOPNOTSUPP;
	}
	if (!rc)
		rc = buffer_check_err(le32_to_cpu(req->OutputBufferLength),
				      rsp, work->response_buf);
	ksmbd_fd_put(work, fp);
	return rc;
}

static int smb2_get_info_filesystem(struct ksmbd_work *work,
				    struct smb2_query_info_req *req,
				    struct smb2_query_info_rsp *rsp)
{
	struct ksmbd_session *sess = work->sess;
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_share_config *share = work->tcon->share_conf;
	int fsinfoclass = 0;
	struct kstatfs stfs;
	struct path path;
	int rc = 0, len;

	if (!share->path)
		return -EIO;

	rc = kern_path(share->path, LOOKUP_NO_SYMLINKS, &path);
	if (rc) {
		pr_err("cannot create vfs path\n");
		return -EIO;
	}

	rc = vfs_statfs(&path, &stfs);
	if (rc) {
		pr_err("cannot do stat of path %s\n", share->path);
		path_put(&path);
		return -EIO;
	}

	fsinfoclass = req->FileInfoClass;

	switch (fsinfoclass) {
	case FS_DEVICE_INFORMATION:
	{
		struct filesystem_device_info *info;

		info = (struct filesystem_device_info *)rsp->Buffer;

		info->DeviceType = cpu_to_le32(FILE_DEVICE_DISK);
		info->DeviceCharacteristics =
			cpu_to_le32(FILE_DEVICE_IS_MOUNTED);
		if (!test_tree_conn_flag(work->tcon,
					 KSMBD_TREE_CONN_FLAG_WRITABLE))
			info->DeviceCharacteristics |=
				cpu_to_le32(FILE_READ_ONLY_DEVICE);
		rsp->OutputBufferLength = cpu_to_le32(8);
		break;
	}
	case FS_ATTRIBUTE_INFORMATION:
	{
		struct filesystem_attribute_info *info;
		size_t sz;

		info = (struct filesystem_attribute_info *)rsp->Buffer;
		info->Attributes = cpu_to_le32(FILE_SUPPORTS_OBJECT_IDS |
					       FILE_PERSISTENT_ACLS |
					       FILE_UNICODE_ON_DISK |
					       FILE_CASE_PRESERVED_NAMES |
					       FILE_CASE_SENSITIVE_SEARCH |
					       FILE_SUPPORTS_BLOCK_REFCOUNTING);

		info->Attributes |= cpu_to_le32(server_conf.share_fake_fscaps);

		if (test_share_config_flag(work->tcon->share_conf,
		    KSMBD_SHARE_FLAG_STREAMS))
			info->Attributes |= cpu_to_le32(FILE_NAMED_STREAMS);

		info->MaxPathNameComponentLength = cpu_to_le32(stfs.f_namelen);
		len = smbConvertToUTF16((__le16 *)info->FileSystemName,
					"NTFS", PATH_MAX, conn->local_nls, 0);
		len = len * 2;
		info->FileSystemNameLen = cpu_to_le32(len);
		sz = sizeof(struct filesystem_attribute_info) + len;
		rsp->OutputBufferLength = cpu_to_le32(sz);
		break;
	}
	case FS_VOLUME_INFORMATION:
	{
		struct filesystem_vol_info *info;
		size_t sz;
		unsigned int serial_crc = 0;

		info = (struct filesystem_vol_info *)(rsp->Buffer);
		info->VolumeCreationTime = 0;
		serial_crc = crc32_le(serial_crc, share->name,
				      strlen(share->name));
		serial_crc = crc32_le(serial_crc, share->path,
				      strlen(share->path));
		serial_crc = crc32_le(serial_crc, ksmbd_netbios_name(),
				      strlen(ksmbd_netbios_name()));
		/* Taking dummy value of serial number*/
		info->SerialNumber = cpu_to_le32(serial_crc);
		len = smbConvertToUTF16((__le16 *)info->VolumeLabel,
					share->name, PATH_MAX,
					conn->local_nls, 0);
		len = len * 2;
		info->VolumeLabelSize = cpu_to_le32(len);
		info->Reserved = 0;
		sz = sizeof(struct filesystem_vol_info) + len;
		rsp->OutputBufferLength = cpu_to_le32(sz);
		break;
	}
	case FS_SIZE_INFORMATION:
	{
		struct filesystem_info *info;

		info = (struct filesystem_info *)(rsp->Buffer);
		info->TotalAllocationUnits = cpu_to_le64(stfs.f_blocks);
		info->FreeAllocationUnits = cpu_to_le64(stfs.f_bfree);
		info->SectorsPerAllocationUnit = cpu_to_le32(1);
		info->BytesPerSector = cpu_to_le32(stfs.f_bsize);
		rsp->OutputBufferLength = cpu_to_le32(24);
		break;
	}
	case FS_FULL_SIZE_INFORMATION:
	{
		struct smb2_fs_full_size_info *info;

		info = (struct smb2_fs_full_size_info *)(rsp->Buffer);
		info->TotalAllocationUnits = cpu_to_le64(stfs.f_blocks);
		info->CallerAvailableAllocationUnits =
					cpu_to_le64(stfs.f_bavail);
		info->ActualAvailableAllocationUnits =
					cpu_to_le64(stfs.f_bfree);
		info->SectorsPerAllocationUnit = cpu_to_le32(1);
		info->BytesPerSector = cpu_to_le32(stfs.f_bsize);
		rsp->OutputBufferLength = cpu_to_le32(32);
		break;
	}
	case FS_OBJECT_ID_INFORMATION:
	{
		struct object_id_info *info;

		info = (struct object_id_info *)(rsp->Buffer);

		if (!user_guest(sess->user))
			memcpy(info->objid, user_passkey(sess->user), 16);
		else
			memset(info->objid, 0, 16);

		info->extended_info.magic = cpu_to_le32(EXTENDED_INFO_MAGIC);
		info->extended_info.version = cpu_to_le32(1);
		info->extended_info.release = cpu_to_le32(1);
		info->extended_info.rel_date = 0;
		memcpy(info->extended_info.version_string, "1.1.0", strlen("1.1.0"));
		rsp->OutputBufferLength = cpu_to_le32(64);
		break;
	}
	case FS_SECTOR_SIZE_INFORMATION:
	{
		struct smb3_fs_ss_info *info;
		unsigned int sector_size =
			min_t(unsigned int, path.mnt->mnt_sb->s_blocksize, 4096);

		info = (struct smb3_fs_ss_info *)(rsp->Buffer);

		info->LogicalBytesPerSector = cpu_to_le32(sector_size);
		info->PhysicalBytesPerSectorForAtomicity =
				cpu_to_le32(sector_size);
		info->PhysicalBytesPerSectorForPerf = cpu_to_le32(sector_size);
		info->FSEffPhysicalBytesPerSectorForAtomicity =
				cpu_to_le32(sector_size);
		info->Flags = cpu_to_le32(SSINFO_FLAGS_ALIGNED_DEVICE |
				    SSINFO_FLAGS_PARTITION_ALIGNED_ON_DEVICE);
		info->ByteOffsetForSectorAlignment = 0;
		info->ByteOffsetForPartitionAlignment = 0;
		rsp->OutputBufferLength = cpu_to_le32(28);
		break;
	}
	case FS_CONTROL_INFORMATION:
	{
		/*
		 * TODO : The current implementation is based on
		 * test result with win7(NTFS) server. It's need to
		 * modify this to get valid Quota values
		 * from Linux kernel
		 */
		struct smb2_fs_control_info *info;

		info = (struct smb2_fs_control_info *)(rsp->Buffer);
		info->FreeSpaceStartFiltering = 0;
		info->FreeSpaceThreshold = 0;
		info->FreeSpaceStopFiltering = 0;
		info->DefaultQuotaThreshold = cpu_to_le64(SMB2_NO_FID);
		info->DefaultQuotaLimit = cpu_to_le64(SMB2_NO_FID);
		info->Padding = 0;
		rsp->OutputBufferLength = cpu_to_le32(48);
		break;
	}
	case FS_POSIX_INFORMATION:
	{
		struct filesystem_posix_info *info;

		if (!work->tcon->posix_extensions) {
			pr_err("client doesn't negotiate with SMB3.1.1 POSIX Extensions\n");
			rc = -EOPNOTSUPP;
		} else {
			info = (struct filesystem_posix_info *)(rsp->Buffer);
			info->OptimalTransferSize = cpu_to_le32(stfs.f_bsize);
			info->BlockSize = cpu_to_le32(stfs.f_bsize);
			info->TotalBlocks = cpu_to_le64(stfs.f_blocks);
			info->BlocksAvail = cpu_to_le64(stfs.f_bfree);
			info->UserBlocksAvail = cpu_to_le64(stfs.f_bavail);
			info->TotalFileNodes = cpu_to_le64(stfs.f_files);
			info->FreeFileNodes = cpu_to_le64(stfs.f_ffree);
			rsp->OutputBufferLength = cpu_to_le32(56);
		}
		break;
	}
	default:
		path_put(&path);
		return -EOPNOTSUPP;
	}
	rc = buffer_check_err(le32_to_cpu(req->OutputBufferLength),
			      rsp, work->response_buf);
	path_put(&path);
	return rc;
}

static int smb2_get_info_sec(struct ksmbd_work *work,
			     struct smb2_query_info_req *req,
			     struct smb2_query_info_rsp *rsp)
{
	struct ksmbd_file *fp;
	struct mnt_idmap *idmap;
	struct smb_ntsd *pntsd = (struct smb_ntsd *)rsp->Buffer, *ppntsd = NULL;
	struct smb_fattr fattr = {{0}};
	struct inode *inode;
	__u32 secdesclen = 0;
	unsigned int id = KSMBD_NO_FID, pid = KSMBD_NO_FID;
	int addition_info = le32_to_cpu(req->AdditionalInformation);
	int rc = 0, ppntsd_size = 0;

	if (addition_info & ~(OWNER_SECINFO | GROUP_SECINFO | DACL_SECINFO |
			      PROTECTED_DACL_SECINFO |
			      UNPROTECTED_DACL_SECINFO)) {
		ksmbd_debug(SMB, "Unsupported addition info: 0x%x)\n",
		       addition_info);

		pntsd->revision = cpu_to_le16(1);
		pntsd->type = cpu_to_le16(SELF_RELATIVE | DACL_PROTECTED);
		pntsd->osidoffset = 0;
		pntsd->gsidoffset = 0;
		pntsd->sacloffset = 0;
		pntsd->dacloffset = 0;

		secdesclen = sizeof(struct smb_ntsd);
		rsp->OutputBufferLength = cpu_to_le32(secdesclen);

		return 0;
	}

	if (work->next_smb2_rcv_hdr_off) {
		if (!has_file_id(req->VolatileFileId)) {
			ksmbd_debug(SMB, "Compound request set FID = %llu\n",
				    work->compound_fid);
			id = work->compound_fid;
			pid = work->compound_pfid;
		}
	}

	if (!has_file_id(id)) {
		id = req->VolatileFileId;
		pid = req->PersistentFileId;
	}

	fp = ksmbd_lookup_fd_slow(work, id, pid);
	if (!fp)
		return -ENOENT;

	idmap = file_mnt_idmap(fp->filp);
	inode = file_inode(fp->filp);
	ksmbd_acls_fattr(&fattr, idmap, inode);

	if (test_share_config_flag(work->tcon->share_conf,
				   KSMBD_SHARE_FLAG_ACL_XATTR))
		ppntsd_size = ksmbd_vfs_get_sd_xattr(work->conn, idmap,
						     fp->filp->f_path.dentry,
						     &ppntsd);

	/* Check if sd buffer size exceeds response buffer size */
	if (smb2_resp_buf_len(work, 8) > ppntsd_size)
		rc = build_sec_desc(idmap, pntsd, ppntsd, ppntsd_size,
				    addition_info, &secdesclen, &fattr);
	posix_acl_release(fattr.cf_acls);
	posix_acl_release(fattr.cf_dacls);
	kfree(ppntsd);
	ksmbd_fd_put(work, fp);
	if (rc)
		return rc;

	rsp->OutputBufferLength = cpu_to_le32(secdesclen);
	return 0;
}

/**
 * smb2_query_info() - handler for smb2 query info command
 * @work:	smb work containing query info request buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_query_info(struct ksmbd_work *work)
{
	struct smb2_query_info_req *req;
	struct smb2_query_info_rsp *rsp;
	int rc = 0;

	WORK_BUFFERS(work, req, rsp);

	ksmbd_debug(SMB, "GOT query info request\n");

	if (ksmbd_override_fsids(work)) {
		rc = -ENOMEM;
		goto err_out;
	}

	switch (req->InfoType) {
	case SMB2_O_INFO_FILE:
		ksmbd_debug(SMB, "GOT SMB2_O_INFO_FILE\n");
		rc = smb2_get_info_file(work, req, rsp);
		break;
	case SMB2_O_INFO_FILESYSTEM:
		ksmbd_debug(SMB, "GOT SMB2_O_INFO_FILESYSTEM\n");
		rc = smb2_get_info_filesystem(work, req, rsp);
		break;
	case SMB2_O_INFO_SECURITY:
		ksmbd_debug(SMB, "GOT SMB2_O_INFO_SECURITY\n");
		rc = smb2_get_info_sec(work, req, rsp);
		break;
	default:
		ksmbd_debug(SMB, "InfoType %d not supported yet\n",
			    req->InfoType);
		rc = -EOPNOTSUPP;
	}
	ksmbd_revert_fsids(work);

	if (!rc) {
		rsp->StructureSize = cpu_to_le16(9);
		rsp->OutputBufferOffset = cpu_to_le16(72);
		rc = ksmbd_iov_pin_rsp(work, (void *)rsp,
				       offsetof(struct smb2_query_info_rsp, Buffer) +
					le32_to_cpu(rsp->OutputBufferLength));
	}

err_out:
	if (rc < 0) {
		if (rc == -EACCES)
			rsp->hdr.Status = STATUS_ACCESS_DENIED;
		else if (rc == -ENOENT)
			rsp->hdr.Status = STATUS_FILE_CLOSED;
		else if (rc == -EIO)
			rsp->hdr.Status = STATUS_UNEXPECTED_IO_ERROR;
		else if (rc == -ENOMEM)
			rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
		else if (rc == -EOPNOTSUPP || rsp->hdr.Status == 0)
			rsp->hdr.Status = STATUS_INVALID_INFO_CLASS;
		smb2_set_err_rsp(work);

		ksmbd_debug(SMB, "error while processing smb2 query rc = %d\n",
			    rc);
		return rc;
	}
	return 0;
}

/**
 * smb2_close_pipe() - handler for closing IPC pipe
 * @work:	smb work containing close request buffer
 *
 * Return:	0
 */
static noinline int smb2_close_pipe(struct ksmbd_work *work)
{
	u64 id;
	struct smb2_close_req *req;
	struct smb2_close_rsp *rsp;

	WORK_BUFFERS(work, req, rsp);

	id = req->VolatileFileId;
	ksmbd_session_rpc_close(work->sess, id);

	rsp->StructureSize = cpu_to_le16(60);
	rsp->Flags = 0;
	rsp->Reserved = 0;
	rsp->CreationTime = 0;
	rsp->LastAccessTime = 0;
	rsp->LastWriteTime = 0;
	rsp->ChangeTime = 0;
	rsp->AllocationSize = 0;
	rsp->EndOfFile = 0;
	rsp->Attributes = 0;

	return ksmbd_iov_pin_rsp(work, (void *)rsp,
				 sizeof(struct smb2_close_rsp));
}

/**
 * smb2_close() - handler for smb2 close file command
 * @work:	smb work containing close request buffer
 *
 * Return:	0
 */
int smb2_close(struct ksmbd_work *work)
{
	u64 volatile_id = KSMBD_NO_FID;
	u64 sess_id;
	struct smb2_close_req *req;
	struct smb2_close_rsp *rsp;
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_file *fp;
	u64 time;
	int err = 0;

	WORK_BUFFERS(work, req, rsp);

	if (test_share_config_flag(work->tcon->share_conf,
				   KSMBD_SHARE_FLAG_PIPE)) {
		ksmbd_debug(SMB, "IPC pipe close request\n");
		return smb2_close_pipe(work);
	}

	sess_id = le64_to_cpu(req->hdr.SessionId);
	if (req->hdr.Flags & SMB2_FLAGS_RELATED_OPERATIONS)
		sess_id = work->compound_sid;

	work->compound_sid = 0;
	if (check_session_id(conn, sess_id)) {
		work->compound_sid = sess_id;
	} else {
		rsp->hdr.Status = STATUS_USER_SESSION_DELETED;
		if (req->hdr.Flags & SMB2_FLAGS_RELATED_OPERATIONS)
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		err = -EBADF;
		goto out;
	}

	if (work->next_smb2_rcv_hdr_off &&
	    !has_file_id(req->VolatileFileId)) {
		if (!has_file_id(work->compound_fid)) {
			/* file already closed, return FILE_CLOSED */
			ksmbd_debug(SMB, "file already closed\n");
			rsp->hdr.Status = STATUS_FILE_CLOSED;
			err = -EBADF;
			goto out;
		} else {
			ksmbd_debug(SMB,
				    "Compound request set FID = %llu:%llu\n",
				    work->compound_fid,
				    work->compound_pfid);
			volatile_id = work->compound_fid;

			/* file closed, stored id is not valid anymore */
			work->compound_fid = KSMBD_NO_FID;
			work->compound_pfid = KSMBD_NO_FID;
		}
	} else {
		volatile_id = req->VolatileFileId;
	}
	ksmbd_debug(SMB, "volatile_id = %llu\n", volatile_id);

	rsp->StructureSize = cpu_to_le16(60);
	rsp->Reserved = 0;

	if (req->Flags == SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB) {
		struct kstat stat;
		int ret;

		fp = ksmbd_lookup_fd_fast(work, volatile_id);
		if (!fp) {
			err = -ENOENT;
			goto out;
		}

		ret = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
				  AT_STATX_SYNC_AS_STAT);
		if (ret) {
			ksmbd_fd_put(work, fp);
			goto out;
		}

		rsp->Flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
		rsp->AllocationSize = S_ISDIR(stat.mode) ? 0 :
			cpu_to_le64(stat.blocks << 9);
		rsp->EndOfFile = cpu_to_le64(stat.size);
		rsp->Attributes = fp->f_ci->m_fattr;
		rsp->CreationTime = cpu_to_le64(fp->create_time);
		time = ksmbd_UnixTimeToNT(stat.atime);
		rsp->LastAccessTime = cpu_to_le64(time);
		time = ksmbd_UnixTimeToNT(stat.mtime);
		rsp->LastWriteTime = cpu_to_le64(time);
		time = ksmbd_UnixTimeToNT(stat.ctime);
		rsp->ChangeTime = cpu_to_le64(time);
		ksmbd_fd_put(work, fp);
	} else {
		rsp->Flags = 0;
		rsp->AllocationSize = 0;
		rsp->EndOfFile = 0;
		rsp->Attributes = 0;
		rsp->CreationTime = 0;
		rsp->LastAccessTime = 0;
		rsp->LastWriteTime = 0;
		rsp->ChangeTime = 0;
	}

	err = ksmbd_close_fd(work, volatile_id);
out:
	if (!err)
		err = ksmbd_iov_pin_rsp(work, (void *)rsp,
					sizeof(struct smb2_close_rsp));

	if (err) {
		if (rsp->hdr.Status == 0)
			rsp->hdr.Status = STATUS_FILE_CLOSED;
		smb2_set_err_rsp(work);
	}

	return err;
}

/**
 * smb2_echo() - handler for smb2 echo(ping) command
 * @work:	smb work containing echo request buffer
 *
 * Return:	0
 */
int smb2_echo(struct ksmbd_work *work)
{
	struct smb2_echo_rsp *rsp = smb2_get_msg(work->response_buf);

	if (work->next_smb2_rcv_hdr_off)
		rsp = ksmbd_resp_buf_next(work);

	rsp->StructureSize = cpu_to_le16(4);
	rsp->Reserved = 0;
	return ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_echo_rsp));
}

static int smb2_rename(struct ksmbd_work *work,
		       struct ksmbd_file *fp,
		       struct smb2_file_rename_info *file_info,
		       struct nls_table *local_nls)
{
	struct ksmbd_share_config *share = fp->tcon->share_conf;
	char *new_name = NULL;
	int rc, flags = 0;

	ksmbd_debug(SMB, "setting FILE_RENAME_INFO\n");
	new_name = smb2_get_name(file_info->FileName,
				 le32_to_cpu(file_info->FileNameLength),
				 local_nls);
	if (IS_ERR(new_name))
		return PTR_ERR(new_name);

	if (strchr(new_name, ':')) {
		int s_type;
		char *xattr_stream_name, *stream_name = NULL;
		size_t xattr_stream_size;
		int len;

		rc = parse_stream_name(new_name, &stream_name, &s_type);
		if (rc < 0)
			goto out;

		len = strlen(new_name);
		if (len > 0 && new_name[len - 1] != '/') {
			pr_err("not allow base filename in rename\n");
			rc = -ESHARE;
			goto out;
		}

		rc = ksmbd_vfs_xattr_stream_name(stream_name,
						 &xattr_stream_name,
						 &xattr_stream_size,
						 s_type);
		if (rc)
			goto out;

		rc = ksmbd_vfs_setxattr(file_mnt_idmap(fp->filp),
					&fp->filp->f_path,
					xattr_stream_name,
					NULL, 0, 0, true);
		if (rc < 0) {
			pr_err("failed to store stream name in xattr: %d\n",
			       rc);
			rc = -EINVAL;
			goto out;
		}

		goto out;
	}

	ksmbd_debug(SMB, "new name %s\n", new_name);
	if (ksmbd_share_veto_filename(share, new_name)) {
		rc = -ENOENT;
		ksmbd_debug(SMB, "Can't rename vetoed file: %s\n", new_name);
		goto out;
	}

	if (!file_info->ReplaceIfExists)
		flags = RENAME_NOREPLACE;

	rc = ksmbd_vfs_rename(work, &fp->filp->f_path, new_name, flags);
	if (!rc)
		smb_break_all_levII_oplock(work, fp, 0);
out:
	kfree(new_name);
	return rc;
}

static int smb2_create_link(struct ksmbd_work *work,
			    struct ksmbd_share_config *share,
			    struct smb2_file_link_info *file_info,
			    unsigned int buf_len, struct file *filp,
			    struct nls_table *local_nls)
{
	char *link_name = NULL, *target_name = NULL, *pathname = NULL;
	struct path path, parent_path;
	bool file_present = false;
	int rc;

	if (buf_len < (u64)sizeof(struct smb2_file_link_info) +
			le32_to_cpu(file_info->FileNameLength))
		return -EINVAL;

	ksmbd_debug(SMB, "setting FILE_LINK_INFORMATION\n");
	pathname = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!pathname)
		return -ENOMEM;

	link_name = smb2_get_name(file_info->FileName,
				  le32_to_cpu(file_info->FileNameLength),
				  local_nls);
	if (IS_ERR(link_name) || S_ISDIR(file_inode(filp)->i_mode)) {
		rc = -EINVAL;
		goto out;
	}

	ksmbd_debug(SMB, "link name is %s\n", link_name);
	target_name = file_path(filp, pathname, PATH_MAX);
	if (IS_ERR(target_name)) {
		rc = -EINVAL;
		goto out;
	}

	ksmbd_debug(SMB, "target name is %s\n", target_name);
	rc = ksmbd_vfs_kern_path_locked(work, link_name, LOOKUP_NO_SYMLINKS,
					&parent_path, &path, 0);
	if (rc) {
		if (rc != -ENOENT)
			goto out;
	} else
		file_present = true;

	if (file_info->ReplaceIfExists) {
		if (file_present) {
			rc = ksmbd_vfs_remove_file(work, &path);
			if (rc) {
				rc = -EINVAL;
				ksmbd_debug(SMB, "cannot delete %s\n",
					    link_name);
				goto out;
			}
		}
	} else {
		if (file_present) {
			rc = -EEXIST;
			ksmbd_debug(SMB, "link already exists\n");
			goto out;
		}
	}

	rc = ksmbd_vfs_link(work, target_name, link_name);
	if (rc)
		rc = -EINVAL;
out:
	if (file_present)
		ksmbd_vfs_kern_path_unlock(&parent_path, &path);

	if (!IS_ERR(link_name))
		kfree(link_name);
	kfree(pathname);
	return rc;
}

static int set_file_basic_info(struct ksmbd_file *fp,
			       struct smb2_file_basic_info *file_info,
			       struct ksmbd_share_config *share)
{
	struct iattr attrs;
	struct file *filp;
	struct inode *inode;
	struct mnt_idmap *idmap;
	int rc = 0;

	if (!(fp->daccess & FILE_WRITE_ATTRIBUTES_LE))
		return -EACCES;

	attrs.ia_valid = 0;
	filp = fp->filp;
	inode = file_inode(filp);
	idmap = file_mnt_idmap(filp);

	if (file_info->CreationTime)
		fp->create_time = le64_to_cpu(file_info->CreationTime);

	if (file_info->LastAccessTime) {
		attrs.ia_atime = ksmbd_NTtimeToUnix(file_info->LastAccessTime);
		attrs.ia_valid |= (ATTR_ATIME | ATTR_ATIME_SET);
	}

	attrs.ia_valid |= ATTR_CTIME;
	if (file_info->ChangeTime)
		attrs.ia_ctime = ksmbd_NTtimeToUnix(file_info->ChangeTime);
	else
		attrs.ia_ctime = inode_get_ctime(inode);

	if (file_info->LastWriteTime) {
		attrs.ia_mtime = ksmbd_NTtimeToUnix(file_info->LastWriteTime);
		attrs.ia_valid |= (ATTR_MTIME | ATTR_MTIME_SET);
	}

	if (file_info->Attributes) {
		if (!S_ISDIR(inode->i_mode) &&
		    file_info->Attributes & FILE_ATTRIBUTE_DIRECTORY_LE) {
			pr_err("can't change a file to a directory\n");
			return -EINVAL;
		}

		if (!(S_ISDIR(inode->i_mode) && file_info->Attributes == FILE_ATTRIBUTE_NORMAL_LE))
			fp->f_ci->m_fattr = file_info->Attributes |
				(fp->f_ci->m_fattr & FILE_ATTRIBUTE_DIRECTORY_LE);
	}

	if (test_share_config_flag(share, KSMBD_SHARE_FLAG_STORE_DOS_ATTRS) &&
	    (file_info->CreationTime || file_info->Attributes)) {
		struct xattr_dos_attrib da = {0};

		da.version = 4;
		da.itime = fp->itime;
		da.create_time = fp->create_time;
		da.attr = le32_to_cpu(fp->f_ci->m_fattr);
		da.flags = XATTR_DOSINFO_ATTRIB | XATTR_DOSINFO_CREATE_TIME |
			XATTR_DOSINFO_ITIME;

		rc = ksmbd_vfs_set_dos_attrib_xattr(idmap, &filp->f_path, &da,
				true);
		if (rc)
			ksmbd_debug(SMB,
				    "failed to restore file attribute in EA\n");
		rc = 0;
	}

	if (attrs.ia_valid) {
		struct dentry *dentry = filp->f_path.dentry;
		struct inode *inode = d_inode(dentry);

		if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
			return -EACCES;

		inode_lock(inode);
		inode_set_ctime_to_ts(inode, attrs.ia_ctime);
		attrs.ia_valid &= ~ATTR_CTIME;
		rc = notify_change(idmap, dentry, &attrs, NULL);
		inode_unlock(inode);
	}
	return rc;
}

static int set_file_allocation_info(struct ksmbd_work *work,
				    struct ksmbd_file *fp,
				    struct smb2_file_alloc_info *file_alloc_info)
{
	/*
	 * TODO : It's working fine only when store dos attributes
	 * is not yes. need to implement a logic which works
	 * properly with any smb.conf option
	 */

	loff_t alloc_blks;
	struct inode *inode;
	struct kstat stat;
	int rc;

	if (!(fp->daccess & FILE_WRITE_DATA_LE))
		return -EACCES;

	rc = vfs_getattr(&fp->filp->f_path, &stat, STATX_BASIC_STATS,
			 AT_STATX_SYNC_AS_STAT);
	if (rc)
		return rc;

	alloc_blks = (le64_to_cpu(file_alloc_info->AllocationSize) + 511) >> 9;
	inode = file_inode(fp->filp);

	if (alloc_blks > stat.blocks) {
		smb_break_all_levII_oplock(work, fp, 1);
		rc = vfs_fallocate(fp->filp, FALLOC_FL_KEEP_SIZE, 0,
				   alloc_blks * 512);
		if (rc && rc != -EOPNOTSUPP) {
			pr_err("vfs_fallocate is failed : %d\n", rc);
			return rc;
		}
	} else if (alloc_blks < stat.blocks) {
		loff_t size;

		/*
		 * Allocation size could be smaller than original one
		 * which means allocated blocks in file should be
		 * deallocated. use truncate to cut out it, but inode
		 * size is also updated with truncate offset.
		 * inode size is retained by backup inode size.
		 */
		size = i_size_read(inode);
		rc = ksmbd_vfs_truncate(work, fp, alloc_blks * 512);
		if (rc) {
			pr_err("truncate failed!, err %d\n", rc);
			return rc;
		}
		if (size < alloc_blks * 512)
			i_size_write(inode, size);
	}
	return 0;
}

static int set_end_of_file_info(struct ksmbd_work *work, struct ksmbd_file *fp,
				struct smb2_file_eof_info *file_eof_info)
{
	loff_t newsize;
	struct inode *inode;
	int rc;

	if (!(fp->daccess & FILE_WRITE_DATA_LE))
		return -EACCES;

	newsize = le64_to_cpu(file_eof_info->EndOfFile);
	inode = file_inode(fp->filp);

	/*
	 * If FILE_END_OF_FILE_INFORMATION of set_info_file is called
	 * on FAT32 shared device, truncate execution time is too long
	 * and network error could cause from windows client. because
	 * truncate of some filesystem like FAT32 fill zero data in
	 * truncated range.
	 */
	if (inode->i_sb->s_magic != MSDOS_SUPER_MAGIC) {
		ksmbd_debug(SMB, "truncated to newsize %lld\n", newsize);
		rc = ksmbd_vfs_truncate(work, fp, newsize);
		if (rc) {
			ksmbd_debug(SMB, "truncate failed!, err %d\n", rc);
			if (rc != -EAGAIN)
				rc = -EBADF;
			return rc;
		}
	}
	return 0;
}

static int set_rename_info(struct ksmbd_work *work, struct ksmbd_file *fp,
			   struct smb2_file_rename_info *rename_info,
			   unsigned int buf_len)
{
	if (!(fp->daccess & FILE_DELETE_LE)) {
		pr_err("no right to delete : 0x%x\n", fp->daccess);
		return -EACCES;
	}

	if (buf_len < (u64)sizeof(struct smb2_file_rename_info) +
			le32_to_cpu(rename_info->FileNameLength))
		return -EINVAL;

	if (!le32_to_cpu(rename_info->FileNameLength))
		return -EINVAL;

	return smb2_rename(work, fp, rename_info, work->conn->local_nls);
}

static int set_file_disposition_info(struct ksmbd_file *fp,
				     struct smb2_file_disposition_info *file_info)
{
	struct inode *inode;

	if (!(fp->daccess & FILE_DELETE_LE)) {
		pr_err("no right to delete : 0x%x\n", fp->daccess);
		return -EACCES;
	}

	inode = file_inode(fp->filp);
	if (file_info->DeletePending) {
		if (S_ISDIR(inode->i_mode) &&
		    ksmbd_vfs_empty_dir(fp) == -ENOTEMPTY)
			return -EBUSY;
		ksmbd_set_inode_pending_delete(fp);
	} else {
		ksmbd_clear_inode_pending_delete(fp);
	}
	return 0;
}

static int set_file_position_info(struct ksmbd_file *fp,
				  struct smb2_file_pos_info *file_info)
{
	loff_t current_byte_offset;
	unsigned long sector_size;
	struct inode *inode;

	inode = file_inode(fp->filp);
	current_byte_offset = le64_to_cpu(file_info->CurrentByteOffset);
	sector_size = inode->i_sb->s_blocksize;

	if (current_byte_offset < 0 ||
	    (fp->coption == FILE_NO_INTERMEDIATE_BUFFERING_LE &&
	     current_byte_offset & (sector_size - 1))) {
		pr_err("CurrentByteOffset is not valid : %llu\n",
		       current_byte_offset);
		return -EINVAL;
	}

	fp->filp->f_pos = current_byte_offset;
	return 0;
}

static int set_file_mode_info(struct ksmbd_file *fp,
			      struct smb2_file_mode_info *file_info)
{
	__le32 mode;

	mode = file_info->Mode;

	if ((mode & ~FILE_MODE_INFO_MASK)) {
		pr_err("Mode is not valid : 0x%x\n", le32_to_cpu(mode));
		return -EINVAL;
	}

	/*
	 * TODO : need to implement consideration for
	 * FILE_SYNCHRONOUS_IO_ALERT and FILE_SYNCHRONOUS_IO_NONALERT
	 */
	ksmbd_vfs_set_fadvise(fp->filp, mode);
	fp->coption = mode;
	return 0;
}

/**
 * smb2_set_info_file() - handler for smb2 set info command
 * @work:	smb work containing set info command buffer
 * @fp:		ksmbd_file pointer
 * @req:	request buffer pointer
 * @share:	ksmbd_share_config pointer
 *
 * Return:	0 on success, otherwise error
 * TODO: need to implement an error handling for STATUS_INFO_LENGTH_MISMATCH
 */
static int smb2_set_info_file(struct ksmbd_work *work, struct ksmbd_file *fp,
			      struct smb2_set_info_req *req,
			      struct ksmbd_share_config *share)
{
	unsigned int buf_len = le32_to_cpu(req->BufferLength);
	char *buffer = (char *)req + le16_to_cpu(req->BufferOffset);

	switch (req->FileInfoClass) {
	case FILE_BASIC_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_basic_info))
			return -EINVAL;

		return set_file_basic_info(fp, (struct smb2_file_basic_info *)buffer, share);
	}
	case FILE_ALLOCATION_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_alloc_info))
			return -EINVAL;

		return set_file_allocation_info(work, fp,
						(struct smb2_file_alloc_info *)buffer);
	}
	case FILE_END_OF_FILE_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_eof_info))
			return -EINVAL;

		return set_end_of_file_info(work, fp,
					    (struct smb2_file_eof_info *)buffer);
	}
	case FILE_RENAME_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_rename_info))
			return -EINVAL;

		return set_rename_info(work, fp,
				       (struct smb2_file_rename_info *)buffer,
				       buf_len);
	}
	case FILE_LINK_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_link_info))
			return -EINVAL;

		return smb2_create_link(work, work->tcon->share_conf,
					(struct smb2_file_link_info *)buffer,
					buf_len, fp->filp,
					work->conn->local_nls);
	}
	case FILE_DISPOSITION_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_disposition_info))
			return -EINVAL;

		return set_file_disposition_info(fp,
						 (struct smb2_file_disposition_info *)buffer);
	}
	case FILE_FULL_EA_INFORMATION:
	{
		if (!(fp->daccess & FILE_WRITE_EA_LE)) {
			pr_err("Not permitted to write ext  attr: 0x%x\n",
			       fp->daccess);
			return -EACCES;
		}

		if (buf_len < sizeof(struct smb2_ea_info))
			return -EINVAL;

		return smb2_set_ea((struct smb2_ea_info *)buffer,
				   buf_len, &fp->filp->f_path, true);
	}
	case FILE_POSITION_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_pos_info))
			return -EINVAL;

		return set_file_position_info(fp, (struct smb2_file_pos_info *)buffer);
	}
	case FILE_MODE_INFORMATION:
	{
		if (buf_len < sizeof(struct smb2_file_mode_info))
			return -EINVAL;

		return set_file_mode_info(fp, (struct smb2_file_mode_info *)buffer);
	}
	}

	pr_err("Unimplemented Fileinfoclass :%d\n", req->FileInfoClass);
	return -EOPNOTSUPP;
}

static int smb2_set_info_sec(struct ksmbd_file *fp, int addition_info,
			     char *buffer, int buf_len)
{
	struct smb_ntsd *pntsd = (struct smb_ntsd *)buffer;

	fp->saccess |= FILE_SHARE_DELETE_LE;

	return set_info_sec(fp->conn, fp->tcon, &fp->filp->f_path, pntsd,
			buf_len, false, true);
}

/**
 * smb2_set_info() - handler for smb2 set info command handler
 * @work:	smb work containing set info request buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_set_info(struct ksmbd_work *work)
{
	struct smb2_set_info_req *req;
	struct smb2_set_info_rsp *rsp;
	struct ksmbd_file *fp = NULL;
	int rc = 0;
	unsigned int id = KSMBD_NO_FID, pid = KSMBD_NO_FID;

	ksmbd_debug(SMB, "Received set info request\n");

	if (work->next_smb2_rcv_hdr_off) {
		req = ksmbd_req_buf_next(work);
		rsp = ksmbd_resp_buf_next(work);
		if (!has_file_id(req->VolatileFileId)) {
			ksmbd_debug(SMB, "Compound request set FID = %llu\n",
				    work->compound_fid);
			id = work->compound_fid;
			pid = work->compound_pfid;
		}
	} else {
		req = smb2_get_msg(work->request_buf);
		rsp = smb2_get_msg(work->response_buf);
	}

	if (!test_tree_conn_flag(work->tcon, KSMBD_TREE_CONN_FLAG_WRITABLE)) {
		ksmbd_debug(SMB, "User does not have write permission\n");
		pr_err("User does not have write permission\n");
		rc = -EACCES;
		goto err_out;
	}

	if (!has_file_id(id)) {
		id = req->VolatileFileId;
		pid = req->PersistentFileId;
	}

	fp = ksmbd_lookup_fd_slow(work, id, pid);
	if (!fp) {
		ksmbd_debug(SMB, "Invalid id for close: %u\n", id);
		rc = -ENOENT;
		goto err_out;
	}

	switch (req->InfoType) {
	case SMB2_O_INFO_FILE:
		ksmbd_debug(SMB, "GOT SMB2_O_INFO_FILE\n");
		rc = smb2_set_info_file(work, fp, req, work->tcon->share_conf);
		break;
	case SMB2_O_INFO_SECURITY:
		ksmbd_debug(SMB, "GOT SMB2_O_INFO_SECURITY\n");
		if (ksmbd_override_fsids(work)) {
			rc = -ENOMEM;
			goto err_out;
		}
		rc = smb2_set_info_sec(fp,
				       le32_to_cpu(req->AdditionalInformation),
				       (char *)req + le16_to_cpu(req->BufferOffset),
				       le32_to_cpu(req->BufferLength));
		ksmbd_revert_fsids(work);
		break;
	default:
		rc = -EOPNOTSUPP;
	}

	if (rc < 0)
		goto err_out;

	rsp->StructureSize = cpu_to_le16(2);
	rc = ksmbd_iov_pin_rsp(work, (void *)rsp,
			       sizeof(struct smb2_set_info_rsp));
	if (rc)
		goto err_out;
	ksmbd_fd_put(work, fp);
	return 0;

err_out:
	if (rc == -EACCES || rc == -EPERM || rc == -EXDEV)
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
	else if (rc == -EINVAL)
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
	else if (rc == -ESHARE)
		rsp->hdr.Status = STATUS_SHARING_VIOLATION;
	else if (rc == -ENOENT)
		rsp->hdr.Status = STATUS_OBJECT_NAME_INVALID;
	else if (rc == -EBUSY || rc == -ENOTEMPTY)
		rsp->hdr.Status = STATUS_DIRECTORY_NOT_EMPTY;
	else if (rc == -EAGAIN)
		rsp->hdr.Status = STATUS_FILE_LOCK_CONFLICT;
	else if (rc == -EBADF || rc == -ESTALE)
		rsp->hdr.Status = STATUS_INVALID_HANDLE;
	else if (rc == -EEXIST)
		rsp->hdr.Status = STATUS_OBJECT_NAME_COLLISION;
	else if (rsp->hdr.Status == 0 || rc == -EOPNOTSUPP)
		rsp->hdr.Status = STATUS_INVALID_INFO_CLASS;
	smb2_set_err_rsp(work);
	ksmbd_fd_put(work, fp);
	ksmbd_debug(SMB, "error while processing smb2 query rc = %d\n", rc);
	return rc;
}

/**
 * smb2_read_pipe() - handler for smb2 read from IPC pipe
 * @work:	smb work containing read IPC pipe command buffer
 *
 * Return:	0 on success, otherwise error
 */
static noinline int smb2_read_pipe(struct ksmbd_work *work)
{
	int nbytes = 0, err;
	u64 id;
	struct ksmbd_rpc_command *rpc_resp;
	struct smb2_read_req *req;
	struct smb2_read_rsp *rsp;

	WORK_BUFFERS(work, req, rsp);

	id = req->VolatileFileId;

	rpc_resp = ksmbd_rpc_read(work->sess, id);
	if (rpc_resp) {
		void *aux_payload_buf;

		if (rpc_resp->flags != KSMBD_RPC_OK) {
			err = -EINVAL;
			goto out;
		}

		aux_payload_buf =
			kvmalloc(rpc_resp->payload_sz, GFP_KERNEL);
		if (!aux_payload_buf) {
			err = -ENOMEM;
			goto out;
		}

		memcpy(aux_payload_buf, rpc_resp->payload, rpc_resp->payload_sz);

		nbytes = rpc_resp->payload_sz;
		err = ksmbd_iov_pin_rsp_read(work, (void *)rsp,
					     offsetof(struct smb2_read_rsp, Buffer),
					     aux_payload_buf, nbytes);
		if (err) {
			kvfree(aux_payload_buf);
			goto out;
		}
		kvfree(rpc_resp);
	} else {
		err = ksmbd_iov_pin_rsp(work, (void *)rsp,
					offsetof(struct smb2_read_rsp, Buffer));
		if (err)
			goto out;
	}

	rsp->StructureSize = cpu_to_le16(17);
	rsp->DataOffset = 80;
	rsp->Reserved = 0;
	rsp->DataLength = cpu_to_le32(nbytes);
	rsp->DataRemaining = 0;
	rsp->Flags = 0;
	return 0;

out:
	rsp->hdr.Status = STATUS_UNEXPECTED_IO_ERROR;
	smb2_set_err_rsp(work);
	kvfree(rpc_resp);
	return err;
}

static int smb2_set_remote_key_for_rdma(struct ksmbd_work *work,
					struct smb2_buffer_desc_v1 *desc,
					__le32 Channel,
					__le16 ChannelInfoLength)
{
	unsigned int i, ch_count;

	if (work->conn->dialect == SMB30_PROT_ID &&
	    Channel != SMB2_CHANNEL_RDMA_V1)
		return -EINVAL;

	ch_count = le16_to_cpu(ChannelInfoLength) / sizeof(*desc);
	if (ksmbd_debug_types & KSMBD_DEBUG_RDMA) {
		for (i = 0; i < ch_count; i++) {
			pr_info("RDMA r/w request %#x: token %#x, length %#x\n",
				i,
				le32_to_cpu(desc[i].token),
				le32_to_cpu(desc[i].length));
		}
	}
	if (!ch_count)
		return -EINVAL;

	work->need_invalidate_rkey =
		(Channel == SMB2_CHANNEL_RDMA_V1_INVALIDATE);
	if (Channel == SMB2_CHANNEL_RDMA_V1_INVALIDATE)
		work->remote_key = le32_to_cpu(desc->token);
	return 0;
}

static ssize_t smb2_read_rdma_channel(struct ksmbd_work *work,
				      struct smb2_read_req *req, void *data_buf,
				      size_t length)
{
	int err;

	err = ksmbd_conn_rdma_write(work->conn, data_buf, length,
				    (struct smb2_buffer_desc_v1 *)
				    ((char *)req + le16_to_cpu(req->ReadChannelInfoOffset)),
				    le16_to_cpu(req->ReadChannelInfoLength));
	if (err)
		return err;

	return length;
}

/**
 * smb2_read() - handler for smb2 read from file
 * @work:	smb work containing read command buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_read(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_read_req *req;
	struct smb2_read_rsp *rsp;
	struct ksmbd_file *fp = NULL;
	loff_t offset;
	size_t length, mincount;
	ssize_t nbytes = 0, remain_bytes = 0;
	int err = 0;
	bool is_rdma_channel = false;
	unsigned int max_read_size = conn->vals->max_read_size;
	unsigned int id = KSMBD_NO_FID, pid = KSMBD_NO_FID;
	void *aux_payload_buf;

	if (test_share_config_flag(work->tcon->share_conf,
				   KSMBD_SHARE_FLAG_PIPE)) {
		ksmbd_debug(SMB, "IPC pipe read request\n");
		return smb2_read_pipe(work);
	}

	if (work->next_smb2_rcv_hdr_off) {
		req = ksmbd_req_buf_next(work);
		rsp = ksmbd_resp_buf_next(work);
		if (!has_file_id(req->VolatileFileId)) {
			ksmbd_debug(SMB, "Compound request set FID = %llu\n",
					work->compound_fid);
			id = work->compound_fid;
			pid = work->compound_pfid;
		}
	} else {
		req = smb2_get_msg(work->request_buf);
		rsp = smb2_get_msg(work->response_buf);
	}

	if (!has_file_id(id)) {
		id = req->VolatileFileId;
		pid = req->PersistentFileId;
	}

	if (req->Channel == SMB2_CHANNEL_RDMA_V1_INVALIDATE ||
	    req->Channel == SMB2_CHANNEL_RDMA_V1) {
		is_rdma_channel = true;
		max_read_size = get_smbd_max_read_write_size();
	}

	if (is_rdma_channel == true) {
		unsigned int ch_offset = le16_to_cpu(req->ReadChannelInfoOffset);

		if (ch_offset < offsetof(struct smb2_read_req, Buffer)) {
			err = -EINVAL;
			goto out;
		}
		err = smb2_set_remote_key_for_rdma(work,
						   (struct smb2_buffer_desc_v1 *)
						   ((char *)req + ch_offset),
						   req->Channel,
						   req->ReadChannelInfoLength);
		if (err)
			goto out;
	}

	fp = ksmbd_lookup_fd_slow(work, id, pid);
	if (!fp) {
		err = -ENOENT;
		goto out;
	}

	if (!(fp->daccess & (FILE_READ_DATA_LE | FILE_READ_ATTRIBUTES_LE))) {
		pr_err("Not permitted to read : 0x%x\n", fp->daccess);
		err = -EACCES;
		goto out;
	}

	offset = le64_to_cpu(req->Offset);
	length = le32_to_cpu(req->Length);
	mincount = le32_to_cpu(req->MinimumCount);

	if (length > max_read_size) {
		ksmbd_debug(SMB, "limiting read size to max size(%u)\n",
			    max_read_size);
		err = -EINVAL;
		goto out;
	}

	ksmbd_debug(SMB, "filename %pD, offset %lld, len %zu\n",
		    fp->filp, offset, length);

	aux_payload_buf = kvzalloc(length, GFP_KERNEL);
	if (!aux_payload_buf) {
		err = -ENOMEM;
		goto out;
	}

	nbytes = ksmbd_vfs_read(work, fp, length, &offset, aux_payload_buf);
	if (nbytes < 0) {
		err = nbytes;
		goto out;
	}

	if ((nbytes == 0 && length != 0) || nbytes < mincount) {
		kvfree(aux_payload_buf);
		rsp->hdr.Status = STATUS_END_OF_FILE;
		smb2_set_err_rsp(work);
		ksmbd_fd_put(work, fp);
		return 0;
	}

	ksmbd_debug(SMB, "nbytes %zu, offset %lld mincount %zu\n",
		    nbytes, offset, mincount);

	if (is_rdma_channel == true) {
		/* write data to the client using rdma channel */
		remain_bytes = smb2_read_rdma_channel(work, req,
						      aux_payload_buf,
						      nbytes);
		kvfree(aux_payload_buf);
		aux_payload_buf = NULL;
		nbytes = 0;
		if (remain_bytes < 0) {
			err = (int)remain_bytes;
			goto out;
		}
	}

	rsp->StructureSize = cpu_to_le16(17);
	rsp->DataOffset = 80;
	rsp->Reserved = 0;
	rsp->DataLength = cpu_to_le32(nbytes);
	rsp->DataRemaining = cpu_to_le32(remain_bytes);
	rsp->Flags = 0;
	err = ksmbd_iov_pin_rsp_read(work, (void *)rsp,
				     offsetof(struct smb2_read_rsp, Buffer),
				     aux_payload_buf, nbytes);
	if (err) {
		kvfree(aux_payload_buf);
		goto out;
	}
	ksmbd_fd_put(work, fp);
	return 0;

out:
	if (err) {
		if (err == -EISDIR)
			rsp->hdr.Status = STATUS_INVALID_DEVICE_REQUEST;
		else if (err == -EAGAIN)
			rsp->hdr.Status = STATUS_FILE_LOCK_CONFLICT;
		else if (err == -ENOENT)
			rsp->hdr.Status = STATUS_FILE_CLOSED;
		else if (err == -EACCES)
			rsp->hdr.Status = STATUS_ACCESS_DENIED;
		else if (err == -ESHARE)
			rsp->hdr.Status = STATUS_SHARING_VIOLATION;
		else if (err == -EINVAL)
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		else
			rsp->hdr.Status = STATUS_INVALID_HANDLE;

		smb2_set_err_rsp(work);
	}
	ksmbd_fd_put(work, fp);
	return err;
}

/**
 * smb2_write_pipe() - handler for smb2 write on IPC pipe
 * @work:	smb work containing write IPC pipe command buffer
 *
 * Return:	0 on success, otherwise error
 */
static noinline int smb2_write_pipe(struct ksmbd_work *work)
{
	struct smb2_write_req *req;
	struct smb2_write_rsp *rsp;
	struct ksmbd_rpc_command *rpc_resp;
	u64 id = 0;
	int err = 0, ret = 0;
	char *data_buf;
	size_t length;

	WORK_BUFFERS(work, req, rsp);

	length = le32_to_cpu(req->Length);
	id = req->VolatileFileId;

	if ((u64)le16_to_cpu(req->DataOffset) + length >
	    get_rfc1002_len(work->request_buf)) {
		pr_err("invalid write data offset %u, smb_len %u\n",
		       le16_to_cpu(req->DataOffset),
		       get_rfc1002_len(work->request_buf));
		err = -EINVAL;
		goto out;
	}

	data_buf = (char *)(((char *)&req->hdr.ProtocolId) +
			   le16_to_cpu(req->DataOffset));

	rpc_resp = ksmbd_rpc_write(work->sess, id, data_buf, length);
	if (rpc_resp) {
		if (rpc_resp->flags == KSMBD_RPC_ENOTIMPLEMENTED) {
			rsp->hdr.Status = STATUS_NOT_SUPPORTED;
			kvfree(rpc_resp);
			smb2_set_err_rsp(work);
			return -EOPNOTSUPP;
		}
		if (rpc_resp->flags != KSMBD_RPC_OK) {
			rsp->hdr.Status = STATUS_INVALID_HANDLE;
			smb2_set_err_rsp(work);
			kvfree(rpc_resp);
			return ret;
		}
		kvfree(rpc_resp);
	}

	rsp->StructureSize = cpu_to_le16(17);
	rsp->DataOffset = 0;
	rsp->Reserved = 0;
	rsp->DataLength = cpu_to_le32(length);
	rsp->DataRemaining = 0;
	rsp->Reserved2 = 0;
	err = ksmbd_iov_pin_rsp(work, (void *)rsp,
				offsetof(struct smb2_write_rsp, Buffer));
out:
	if (err) {
		rsp->hdr.Status = STATUS_INVALID_HANDLE;
		smb2_set_err_rsp(work);
	}

	return err;
}

static ssize_t smb2_write_rdma_channel(struct ksmbd_work *work,
				       struct smb2_write_req *req,
				       struct ksmbd_file *fp,
				       loff_t offset, size_t length, bool sync)
{
	char *data_buf;
	int ret;
	ssize_t nbytes;

	data_buf = kvzalloc(length, GFP_KERNEL);
	if (!data_buf)
		return -ENOMEM;

	ret = ksmbd_conn_rdma_read(work->conn, data_buf, length,
				   (struct smb2_buffer_desc_v1 *)
				   ((char *)req + le16_to_cpu(req->WriteChannelInfoOffset)),
				   le16_to_cpu(req->WriteChannelInfoLength));
	if (ret < 0) {
		kvfree(data_buf);
		return ret;
	}

	ret = ksmbd_vfs_write(work, fp, data_buf, length, &offset, sync, &nbytes);
	kvfree(data_buf);
	if (ret < 0)
		return ret;

	return nbytes;
}

/**
 * smb2_write() - handler for smb2 write from file
 * @work:	smb work containing write command buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_write(struct ksmbd_work *work)
{
	struct smb2_write_req *req;
	struct smb2_write_rsp *rsp;
	struct ksmbd_file *fp = NULL;
	loff_t offset;
	size_t length;
	ssize_t nbytes;
	char *data_buf;
	bool writethrough = false, is_rdma_channel = false;
	int err = 0;
	unsigned int max_write_size = work->conn->vals->max_write_size;

	WORK_BUFFERS(work, req, rsp);

	if (test_share_config_flag(work->tcon->share_conf, KSMBD_SHARE_FLAG_PIPE)) {
		ksmbd_debug(SMB, "IPC pipe write request\n");
		return smb2_write_pipe(work);
	}

	offset = le64_to_cpu(req->Offset);
	length = le32_to_cpu(req->Length);

	if (req->Channel == SMB2_CHANNEL_RDMA_V1 ||
	    req->Channel == SMB2_CHANNEL_RDMA_V1_INVALIDATE) {
		is_rdma_channel = true;
		max_write_size = get_smbd_max_read_write_size();
		length = le32_to_cpu(req->RemainingBytes);
	}

	if (is_rdma_channel == true) {
		unsigned int ch_offset = le16_to_cpu(req->WriteChannelInfoOffset);

		if (req->Length != 0 || req->DataOffset != 0 ||
		    ch_offset < offsetof(struct smb2_write_req, Buffer)) {
			err = -EINVAL;
			goto out;
		}
		err = smb2_set_remote_key_for_rdma(work,
						   (struct smb2_buffer_desc_v1 *)
						   ((char *)req + ch_offset),
						   req->Channel,
						   req->WriteChannelInfoLength);
		if (err)
			goto out;
	}

	if (!test_tree_conn_flag(work->tcon, KSMBD_TREE_CONN_FLAG_WRITABLE)) {
		ksmbd_debug(SMB, "User does not have write permission\n");
		err = -EACCES;
		goto out;
	}

	fp = ksmbd_lookup_fd_slow(work, req->VolatileFileId, req->PersistentFileId);
	if (!fp) {
		err = -ENOENT;
		goto out;
	}

	if (!(fp->daccess & (FILE_WRITE_DATA_LE | FILE_READ_ATTRIBUTES_LE))) {
		pr_err("Not permitted to write : 0x%x\n", fp->daccess);
		err = -EACCES;
		goto out;
	}

	if (length > max_write_size) {
		ksmbd_debug(SMB, "limiting write size to max size(%u)\n",
			    max_write_size);
		err = -EINVAL;
		goto out;
	}

	ksmbd_debug(SMB, "flags %u\n", le32_to_cpu(req->Flags));
	if (le32_to_cpu(req->Flags) & SMB2_WRITEFLAG_WRITE_THROUGH)
		writethrough = true;

	if (is_rdma_channel == false) {
		if (le16_to_cpu(req->DataOffset) <
		    offsetof(struct smb2_write_req, Buffer)) {
			err = -EINVAL;
			goto out;
		}

		data_buf = (char *)(((char *)&req->hdr.ProtocolId) +
				    le16_to_cpu(req->DataOffset));

		ksmbd_debug(SMB, "filename %pD, offset %lld, len %zu\n",
			    fp->filp, offset, length);
		err = ksmbd_vfs_write(work, fp, data_buf, length, &offset,
				      writethrough, &nbytes);
		if (err < 0)
			goto out;
	} else {
		/* read data from the client using rdma channel, and
		 * write the data.
		 */
		nbytes = smb2_write_rdma_channel(work, req, fp, offset, length,
						 writethrough);
		if (nbytes < 0) {
			err = (int)nbytes;
			goto out;
		}
	}

	rsp->StructureSize = cpu_to_le16(17);
	rsp->DataOffset = 0;
	rsp->Reserved = 0;
	rsp->DataLength = cpu_to_le32(nbytes);
	rsp->DataRemaining = 0;
	rsp->Reserved2 = 0;
	err = ksmbd_iov_pin_rsp(work, rsp, offsetof(struct smb2_write_rsp, Buffer));
	if (err)
		goto out;
	ksmbd_fd_put(work, fp);
	return 0;

out:
	if (err == -EAGAIN)
		rsp->hdr.Status = STATUS_FILE_LOCK_CONFLICT;
	else if (err == -ENOSPC || err == -EFBIG)
		rsp->hdr.Status = STATUS_DISK_FULL;
	else if (err == -ENOENT)
		rsp->hdr.Status = STATUS_FILE_CLOSED;
	else if (err == -EACCES)
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
	else if (err == -ESHARE)
		rsp->hdr.Status = STATUS_SHARING_VIOLATION;
	else if (err == -EINVAL)
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
	else
		rsp->hdr.Status = STATUS_INVALID_HANDLE;

	smb2_set_err_rsp(work);
	ksmbd_fd_put(work, fp);
	return err;
}

/**
 * smb2_flush() - handler for smb2 flush file - fsync
 * @work:	smb work containing flush command buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_flush(struct ksmbd_work *work)
{
	struct smb2_flush_req *req;
	struct smb2_flush_rsp *rsp;
	int err;

	WORK_BUFFERS(work, req, rsp);

	ksmbd_debug(SMB, "SMB2_FLUSH called for fid %llu\n", req->VolatileFileId);

	err = ksmbd_vfs_fsync(work, req->VolatileFileId, req->PersistentFileId);
	if (err)
		goto out;

	rsp->StructureSize = cpu_to_le16(4);
	rsp->Reserved = 0;
	return ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_flush_rsp));

out:
	rsp->hdr.Status = STATUS_INVALID_HANDLE;
	smb2_set_err_rsp(work);
	return err;
}

/**
 * smb2_cancel() - handler for smb2 cancel command
 * @work:	smb work containing cancel command buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_cancel(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_hdr *hdr = smb2_get_msg(work->request_buf);
	struct smb2_hdr *chdr;
	struct ksmbd_work *iter;
	struct list_head *command_list;

	if (work->next_smb2_rcv_hdr_off)
		hdr = ksmbd_resp_buf_next(work);

	ksmbd_debug(SMB, "smb2 cancel called on mid %llu, async flags 0x%x\n",
		    hdr->MessageId, hdr->Flags);

	if (hdr->Flags & SMB2_FLAGS_ASYNC_COMMAND) {
		command_list = &conn->async_requests;

		spin_lock(&conn->request_lock);
		list_for_each_entry(iter, command_list,
				    async_request_entry) {
			chdr = smb2_get_msg(iter->request_buf);

			if (iter->async_id !=
			    le64_to_cpu(hdr->Id.AsyncId))
				continue;

			ksmbd_debug(SMB,
				    "smb2 with AsyncId %llu cancelled command = 0x%x\n",
				    le64_to_cpu(hdr->Id.AsyncId),
				    le16_to_cpu(chdr->Command));
			iter->state = KSMBD_WORK_CANCELLED;
			if (iter->cancel_fn)
				iter->cancel_fn(iter->cancel_argv);
			break;
		}
		spin_unlock(&conn->request_lock);
	} else {
		command_list = &conn->requests;

		spin_lock(&conn->request_lock);
		list_for_each_entry(iter, command_list, request_entry) {
			chdr = smb2_get_msg(iter->request_buf);

			if (chdr->MessageId != hdr->MessageId ||
			    iter == work)
				continue;

			ksmbd_debug(SMB,
				    "smb2 with mid %llu cancelled command = 0x%x\n",
				    le64_to_cpu(hdr->MessageId),
				    le16_to_cpu(chdr->Command));
			iter->state = KSMBD_WORK_CANCELLED;
			break;
		}
		spin_unlock(&conn->request_lock);
	}

	/* For SMB2_CANCEL command itself send no response*/
	work->send_no_response = 1;
	return 0;
}

struct file_lock *smb_flock_init(struct file *f)
{
	struct file_lock *fl;

	fl = locks_alloc_lock();
	if (!fl)
		goto out;

	locks_init_lock(fl);

	fl->c.flc_owner = f;
	fl->c.flc_pid = current->tgid;
	fl->c.flc_file = f;
	fl->c.flc_flags = FL_POSIX;
	fl->fl_ops = NULL;
	fl->fl_lmops = NULL;

out:
	return fl;
}

static int smb2_set_flock_flags(struct file_lock *flock, int flags)
{
	int cmd = -EINVAL;

	/* Checking for wrong flag combination during lock request*/
	switch (flags) {
	case SMB2_LOCKFLAG_SHARED:
		ksmbd_debug(SMB, "received shared request\n");
		cmd = F_SETLKW;
		flock->c.flc_type = F_RDLCK;
		flock->c.flc_flags |= FL_SLEEP;
		break;
	case SMB2_LOCKFLAG_EXCLUSIVE:
		ksmbd_debug(SMB, "received exclusive request\n");
		cmd = F_SETLKW;
		flock->c.flc_type = F_WRLCK;
		flock->c.flc_flags |= FL_SLEEP;
		break;
	case SMB2_LOCKFLAG_SHARED | SMB2_LOCKFLAG_FAIL_IMMEDIATELY:
		ksmbd_debug(SMB,
			    "received shared & fail immediately request\n");
		cmd = F_SETLK;
		flock->c.flc_type = F_RDLCK;
		break;
	case SMB2_LOCKFLAG_EXCLUSIVE | SMB2_LOCKFLAG_FAIL_IMMEDIATELY:
		ksmbd_debug(SMB,
			    "received exclusive & fail immediately request\n");
		cmd = F_SETLK;
		flock->c.flc_type = F_WRLCK;
		break;
	case SMB2_LOCKFLAG_UNLOCK:
		ksmbd_debug(SMB, "received unlock request\n");
		flock->c.flc_type = F_UNLCK;
		cmd = F_SETLK;
		break;
	}

	return cmd;
}

static struct ksmbd_lock *smb2_lock_init(struct file_lock *flock,
					 unsigned int cmd, int flags,
					 struct list_head *lock_list)
{
	struct ksmbd_lock *lock;

	lock = kzalloc(sizeof(struct ksmbd_lock), GFP_KERNEL);
	if (!lock)
		return NULL;

	lock->cmd = cmd;
	lock->fl = flock;
	lock->start = flock->fl_start;
	lock->end = flock->fl_end;
	lock->flags = flags;
	if (lock->start == lock->end)
		lock->zero_len = 1;
	INIT_LIST_HEAD(&lock->clist);
	INIT_LIST_HEAD(&lock->flist);
	INIT_LIST_HEAD(&lock->llist);
	list_add_tail(&lock->llist, lock_list);

	return lock;
}

static void smb2_remove_blocked_lock(void **argv)
{
	struct file_lock *flock = (struct file_lock *)argv[0];

	ksmbd_vfs_posix_lock_unblock(flock);
	locks_wake_up(flock);
}

static inline bool lock_defer_pending(struct file_lock *fl)
{
	/* check pending lock waiters */
	return waitqueue_active(&fl->c.flc_wait);
}

/**
 * smb2_lock() - handler for smb2 file lock command
 * @work:	smb work containing lock command buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_lock(struct ksmbd_work *work)
{
	struct smb2_lock_req *req;
	struct smb2_lock_rsp *rsp;
	struct smb2_lock_element *lock_ele;
	struct ksmbd_file *fp = NULL;
	struct file_lock *flock = NULL;
	struct file *filp = NULL;
	int lock_count;
	int flags = 0;
	int cmd = 0;
	int err = -EIO, i, rc = 0;
	u64 lock_start, lock_length;
	struct ksmbd_lock *smb_lock = NULL, *cmp_lock, *tmp, *tmp2;
	struct ksmbd_conn *conn;
	int nolock = 0;
	LIST_HEAD(lock_list);
	LIST_HEAD(rollback_list);
	int prior_lock = 0;

	WORK_BUFFERS(work, req, rsp);

	ksmbd_debug(SMB, "Received lock request\n");
	fp = ksmbd_lookup_fd_slow(work, req->VolatileFileId, req->PersistentFileId);
	if (!fp) {
		ksmbd_debug(SMB, "Invalid file id for lock : %llu\n", req->VolatileFileId);
		err = -ENOENT;
		goto out2;
	}

	filp = fp->filp;
	lock_count = le16_to_cpu(req->LockCount);
	lock_ele = req->locks;

	ksmbd_debug(SMB, "lock count is %d\n", lock_count);
	if (!lock_count) {
		err = -EINVAL;
		goto out2;
	}

	for (i = 0; i < lock_count; i++) {
		flags = le32_to_cpu(lock_ele[i].Flags);

		flock = smb_flock_init(filp);
		if (!flock)
			goto out;

		cmd = smb2_set_flock_flags(flock, flags);

		lock_start = le64_to_cpu(lock_ele[i].Offset);
		lock_length = le64_to_cpu(lock_ele[i].Length);
		if (lock_start > U64_MAX - lock_length) {
			pr_err("Invalid lock range requested\n");
			rsp->hdr.Status = STATUS_INVALID_LOCK_RANGE;
			locks_free_lock(flock);
			goto out;
		}

		if (lock_start > OFFSET_MAX)
			flock->fl_start = OFFSET_MAX;
		else
			flock->fl_start = lock_start;

		lock_length = le64_to_cpu(lock_ele[i].Length);
		if (lock_length > OFFSET_MAX - flock->fl_start)
			lock_length = OFFSET_MAX - flock->fl_start;

		flock->fl_end = flock->fl_start + lock_length;

		if (flock->fl_end < flock->fl_start) {
			ksmbd_debug(SMB,
				    "the end offset(%llx) is smaller than the start offset(%llx)\n",
				    flock->fl_end, flock->fl_start);
			rsp->hdr.Status = STATUS_INVALID_LOCK_RANGE;
			locks_free_lock(flock);
			goto out;
		}

		/* Check conflict locks in one request */
		list_for_each_entry(cmp_lock, &lock_list, llist) {
			if (cmp_lock->fl->fl_start <= flock->fl_start &&
			    cmp_lock->fl->fl_end >= flock->fl_end) {
				if (cmp_lock->fl->c.flc_type != F_UNLCK &&
				    flock->c.flc_type != F_UNLCK) {
					pr_err("conflict two locks in one request\n");
					err = -EINVAL;
					locks_free_lock(flock);
					goto out;
				}
			}
		}

		smb_lock = smb2_lock_init(flock, cmd, flags, &lock_list);
		if (!smb_lock) {
			err = -EINVAL;
			locks_free_lock(flock);
			goto out;
		}
	}

	list_for_each_entry_safe(smb_lock, tmp, &lock_list, llist) {
		if (smb_lock->cmd < 0) {
			err = -EINVAL;
			goto out;
		}

		if (!(smb_lock->flags & SMB2_LOCKFLAG_MASK)) {
			err = -EINVAL;
			goto out;
		}

		if ((prior_lock & (SMB2_LOCKFLAG_EXCLUSIVE | SMB2_LOCKFLAG_SHARED) &&
		     smb_lock->flags & SMB2_LOCKFLAG_UNLOCK) ||
		    (prior_lock == SMB2_LOCKFLAG_UNLOCK &&
		     !(smb_lock->flags & SMB2_LOCKFLAG_UNLOCK))) {
			err = -EINVAL;
			goto out;
		}

		prior_lock = smb_lock->flags;

		if (!(smb_lock->flags & SMB2_LOCKFLAG_UNLOCK) &&
		    !(smb_lock->flags & SMB2_LOCKFLAG_FAIL_IMMEDIATELY))
			goto no_check_cl;

		nolock = 1;
		/* check locks in connection list */
		down_read(&conn_list_lock);
		list_for_each_entry(conn, &conn_list, conns_list) {
			spin_lock(&conn->llist_lock);
			list_for_each_entry_safe(cmp_lock, tmp2, &conn->lock_list, clist) {
				if (file_inode(cmp_lock->fl->c.flc_file) !=
				    file_inode(smb_lock->fl->c.flc_file))
					continue;

				if (lock_is_unlock(smb_lock->fl)) {
					if (cmp_lock->fl->c.flc_file == smb_lock->fl->c.flc_file &&
					    cmp_lock->start == smb_lock->start &&
					    cmp_lock->end == smb_lock->end &&
					    !lock_defer_pending(cmp_lock->fl)) {
						nolock = 0;
						list_del(&cmp_lock->flist);
						list_del(&cmp_lock->clist);
						spin_unlock(&conn->llist_lock);
						up_read(&conn_list_lock);

						locks_free_lock(cmp_lock->fl);
						kfree(cmp_lock);
						goto out_check_cl;
					}
					continue;
				}

				if (cmp_lock->fl->c.flc_file == smb_lock->fl->c.flc_file) {
					if (smb_lock->flags & SMB2_LOCKFLAG_SHARED)
						continue;
				} else {
					if (cmp_lock->flags & SMB2_LOCKFLAG_SHARED)
						continue;
				}

				/* check zero byte lock range */
				if (cmp_lock->zero_len && !smb_lock->zero_len &&
				    cmp_lock->start > smb_lock->start &&
				    cmp_lock->start < smb_lock->end) {
					spin_unlock(&conn->llist_lock);
					up_read(&conn_list_lock);
					pr_err("previous lock conflict with zero byte lock range\n");
					goto out;
				}

				if (smb_lock->zero_len && !cmp_lock->zero_len &&
				    smb_lock->start > cmp_lock->start &&
				    smb_lock->start < cmp_lock->end) {
					spin_unlock(&conn->llist_lock);
					up_read(&conn_list_lock);
					pr_err("current lock conflict with zero byte lock range\n");
					goto out;
				}

				if (((cmp_lock->start <= smb_lock->start &&
				      cmp_lock->end > smb_lock->start) ||
				     (cmp_lock->start < smb_lock->end &&
				      cmp_lock->end >= smb_lock->end)) &&
				    !cmp_lock->zero_len && !smb_lock->zero_len) {
					spin_unlock(&conn->llist_lock);
					up_read(&conn_list_lock);
					pr_err("Not allow lock operation on exclusive lock range\n");
					goto out;
				}
			}
			spin_unlock(&conn->llist_lock);
		}
		up_read(&conn_list_lock);
out_check_cl:
		if (lock_is_unlock(smb_lock->fl) && nolock) {
			pr_err("Try to unlock nolocked range\n");
			rsp->hdr.Status = STATUS_RANGE_NOT_LOCKED;
			goto out;
		}

no_check_cl:
		if (smb_lock->zero_len) {
			err = 0;
			goto skip;
		}

		flock = smb_lock->fl;
		list_del(&smb_lock->llist);
retry:
		rc = vfs_lock_file(filp, smb_lock->cmd, flock, NULL);
skip:
		if (flags & SMB2_LOCKFLAG_UNLOCK) {
			if (!rc) {
				ksmbd_debug(SMB, "File unlocked\n");
			} else if (rc == -ENOENT) {
				rsp->hdr.Status = STATUS_NOT_LOCKED;
				goto out;
			}
			locks_free_lock(flock);
			kfree(smb_lock);
		} else {
			if (rc == FILE_LOCK_DEFERRED) {
				void **argv;

				ksmbd_debug(SMB,
					    "would have to wait for getting lock\n");
				list_add(&smb_lock->llist, &rollback_list);

				argv = kmalloc(sizeof(void *), GFP_KERNEL);
				if (!argv) {
					err = -ENOMEM;
					goto out;
				}
				argv[0] = flock;

				rc = setup_async_work(work,
						      smb2_remove_blocked_lock,
						      argv);
				if (rc) {
					kfree(argv);
					err = -ENOMEM;
					goto out;
				}
				spin_lock(&fp->f_lock);
				list_add(&work->fp_entry, &fp->blocked_works);
				spin_unlock(&fp->f_lock);

				smb2_send_interim_resp(work, STATUS_PENDING);

				ksmbd_vfs_posix_lock_wait(flock);

				spin_lock(&fp->f_lock);
				list_del(&work->fp_entry);
				spin_unlock(&fp->f_lock);

				if (work->state != KSMBD_WORK_ACTIVE) {
					list_del(&smb_lock->llist);
					locks_free_lock(flock);

					if (work->state == KSMBD_WORK_CANCELLED) {
						rsp->hdr.Status =
							STATUS_CANCELLED;
						kfree(smb_lock);
						smb2_send_interim_resp(work,
								       STATUS_CANCELLED);
						work->send_no_response = 1;
						goto out;
					}

					rsp->hdr.Status =
						STATUS_RANGE_NOT_LOCKED;
					kfree(smb_lock);
					goto out2;
				}

				list_del(&smb_lock->llist);
				release_async_work(work);
				goto retry;
			} else if (!rc) {
				list_add(&smb_lock->llist, &rollback_list);
				spin_lock(&work->conn->llist_lock);
				list_add_tail(&smb_lock->clist,
					      &work->conn->lock_list);
				list_add_tail(&smb_lock->flist,
					      &fp->lock_list);
				spin_unlock(&work->conn->llist_lock);
				ksmbd_debug(SMB, "successful in taking lock\n");
			} else {
				goto out;
			}
		}
	}

	if (atomic_read(&fp->f_ci->op_count) > 1)
		smb_break_all_oplock(work, fp);

	rsp->StructureSize = cpu_to_le16(4);
	ksmbd_debug(SMB, "successful in taking lock\n");
	rsp->hdr.Status = STATUS_SUCCESS;
	rsp->Reserved = 0;
	err = ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_lock_rsp));
	if (err)
		goto out;

	ksmbd_fd_put(work, fp);
	return 0;

out:
	list_for_each_entry_safe(smb_lock, tmp, &lock_list, llist) {
		locks_free_lock(smb_lock->fl);
		list_del(&smb_lock->llist);
		kfree(smb_lock);
	}

	list_for_each_entry_safe(smb_lock, tmp, &rollback_list, llist) {
		struct file_lock *rlock = NULL;

		rlock = smb_flock_init(filp);
		rlock->c.flc_type = F_UNLCK;
		rlock->fl_start = smb_lock->start;
		rlock->fl_end = smb_lock->end;

		rc = vfs_lock_file(filp, F_SETLK, rlock, NULL);
		if (rc)
			pr_err("rollback unlock fail : %d\n", rc);

		list_del(&smb_lock->llist);
		spin_lock(&work->conn->llist_lock);
		if (!list_empty(&smb_lock->flist))
			list_del(&smb_lock->flist);
		list_del(&smb_lock->clist);
		spin_unlock(&work->conn->llist_lock);

		locks_free_lock(smb_lock->fl);
		locks_free_lock(rlock);
		kfree(smb_lock);
	}
out2:
	ksmbd_debug(SMB, "failed in taking lock(flags : %x), err : %d\n", flags, err);

	if (!rsp->hdr.Status) {
		if (err == -EINVAL)
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		else if (err == -ENOMEM)
			rsp->hdr.Status = STATUS_INSUFFICIENT_RESOURCES;
		else if (err == -ENOENT)
			rsp->hdr.Status = STATUS_FILE_CLOSED;
		else
			rsp->hdr.Status = STATUS_LOCK_NOT_GRANTED;
	}

	smb2_set_err_rsp(work);
	ksmbd_fd_put(work, fp);
	return err;
}

static int fsctl_copychunk(struct ksmbd_work *work,
			   struct copychunk_ioctl_req *ci_req,
			   unsigned int cnt_code,
			   unsigned int input_count,
			   unsigned long long volatile_id,
			   unsigned long long persistent_id,
			   struct smb2_ioctl_rsp *rsp)
{
	struct copychunk_ioctl_rsp *ci_rsp;
	struct ksmbd_file *src_fp = NULL, *dst_fp = NULL;
	struct srv_copychunk *chunks;
	unsigned int i, chunk_count, chunk_count_written = 0;
	unsigned int chunk_size_written = 0;
	loff_t total_size_written = 0;
	int ret = 0;

	ci_rsp = (struct copychunk_ioctl_rsp *)&rsp->Buffer[0];

	rsp->VolatileFileId = volatile_id;
	rsp->PersistentFileId = persistent_id;
	ci_rsp->ChunksWritten =
		cpu_to_le32(ksmbd_server_side_copy_max_chunk_count());
	ci_rsp->ChunkBytesWritten =
		cpu_to_le32(ksmbd_server_side_copy_max_chunk_size());
	ci_rsp->TotalBytesWritten =
		cpu_to_le32(ksmbd_server_side_copy_max_total_size());

	chunk_count = le32_to_cpu(ci_req->ChunkCount);
	if (chunk_count == 0)
		goto out;
	total_size_written = 0;

	/* verify the SRV_COPYCHUNK_COPY packet */
	if (chunk_count > ksmbd_server_side_copy_max_chunk_count() ||
	    input_count < struct_size(ci_req, Chunks, chunk_count)) {
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		return -EINVAL;
	}

	chunks = &ci_req->Chunks[0];
	for (i = 0; i < chunk_count; i++) {
		if (le32_to_cpu(chunks[i].Length) == 0 ||
		    le32_to_cpu(chunks[i].Length) > ksmbd_server_side_copy_max_chunk_size())
			break;
		total_size_written += le32_to_cpu(chunks[i].Length);
	}

	if (i < chunk_count ||
	    total_size_written > ksmbd_server_side_copy_max_total_size()) {
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		return -EINVAL;
	}

	src_fp = ksmbd_lookup_foreign_fd(work,
					 le64_to_cpu(ci_req->ResumeKey[0]));
	dst_fp = ksmbd_lookup_fd_slow(work, volatile_id, persistent_id);
	ret = -EINVAL;
	if (!src_fp ||
	    src_fp->persistent_id != le64_to_cpu(ci_req->ResumeKey[1])) {
		rsp->hdr.Status = STATUS_OBJECT_NAME_NOT_FOUND;
		goto out;
	}

	if (!dst_fp) {
		rsp->hdr.Status = STATUS_FILE_CLOSED;
		goto out;
	}

	/*
	 * FILE_READ_DATA should only be included in
	 * the FSCTL_COPYCHUNK case
	 */
	if (cnt_code == FSCTL_COPYCHUNK &&
	    !(dst_fp->daccess & (FILE_READ_DATA_LE | FILE_GENERIC_READ_LE))) {
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
		goto out;
	}

	ret = ksmbd_vfs_copy_file_ranges(work, src_fp, dst_fp,
					 chunks, chunk_count,
					 &chunk_count_written,
					 &chunk_size_written,
					 &total_size_written);
	if (ret < 0) {
		if (ret == -EACCES)
			rsp->hdr.Status = STATUS_ACCESS_DENIED;
		if (ret == -EAGAIN)
			rsp->hdr.Status = STATUS_FILE_LOCK_CONFLICT;
		else if (ret == -EBADF)
			rsp->hdr.Status = STATUS_INVALID_HANDLE;
		else if (ret == -EFBIG || ret == -ENOSPC)
			rsp->hdr.Status = STATUS_DISK_FULL;
		else if (ret == -EINVAL)
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		else if (ret == -EISDIR)
			rsp->hdr.Status = STATUS_FILE_IS_A_DIRECTORY;
		else if (ret == -E2BIG)
			rsp->hdr.Status = STATUS_INVALID_VIEW_SIZE;
		else
			rsp->hdr.Status = STATUS_UNEXPECTED_IO_ERROR;
	}

	ci_rsp->ChunksWritten = cpu_to_le32(chunk_count_written);
	ci_rsp->ChunkBytesWritten = cpu_to_le32(chunk_size_written);
	ci_rsp->TotalBytesWritten = cpu_to_le32(total_size_written);
out:
	ksmbd_fd_put(work, src_fp);
	ksmbd_fd_put(work, dst_fp);
	return ret;
}

static __be32 idev_ipv4_address(struct in_device *idev)
{
	__be32 addr = 0;

	struct in_ifaddr *ifa;

	rcu_read_lock();
	in_dev_for_each_ifa_rcu(ifa, idev) {
		if (ifa->ifa_flags & IFA_F_SECONDARY)
			continue;

		addr = ifa->ifa_address;
		break;
	}
	rcu_read_unlock();
	return addr;
}

static int fsctl_query_iface_info_ioctl(struct ksmbd_conn *conn,
					struct smb2_ioctl_rsp *rsp,
					unsigned int out_buf_len)
{
	struct network_interface_info_ioctl_rsp *nii_rsp = NULL;
	int nbytes = 0;
	struct net_device *netdev;
	struct sockaddr_storage_rsp *sockaddr_storage;
	unsigned int flags;
	unsigned long long speed;

	rtnl_lock();
	for_each_netdev(&init_net, netdev) {
		bool ipv4_set = false;

		if (netdev->type == ARPHRD_LOOPBACK)
			continue;

		flags = dev_get_flags(netdev);
		if (!(flags & IFF_RUNNING))
			continue;
ipv6_retry:
		if (out_buf_len <
		    nbytes + sizeof(struct network_interface_info_ioctl_rsp)) {
			rtnl_unlock();
			return -ENOSPC;
		}

		nii_rsp = (struct network_interface_info_ioctl_rsp *)
				&rsp->Buffer[nbytes];
		nii_rsp->IfIndex = cpu_to_le32(netdev->ifindex);

		nii_rsp->Capability = 0;
		if (netdev->real_num_tx_queues > 1)
			nii_rsp->Capability |= cpu_to_le32(RSS_CAPABLE);
		if (ksmbd_rdma_capable_netdev(netdev))
			nii_rsp->Capability |= cpu_to_le32(RDMA_CAPABLE);

		nii_rsp->Next = cpu_to_le32(152);
		nii_rsp->Reserved = 0;

		if (netdev->ethtool_ops->get_link_ksettings) {
			struct ethtool_link_ksettings cmd;

			netdev->ethtool_ops->get_link_ksettings(netdev, &cmd);
			speed = cmd.base.speed;
		} else {
			ksmbd_debug(SMB, "%s %s\n", netdev->name,
				    "speed is unknown, defaulting to 1Gb/sec");
			speed = SPEED_1000;
		}

		speed *= 1000000;
		nii_rsp->LinkSpeed = cpu_to_le64(speed);

		sockaddr_storage = (struct sockaddr_storage_rsp *)
					nii_rsp->SockAddr_Storage;
		memset(sockaddr_storage, 0, 128);

		if (!ipv4_set) {
			struct in_device *idev;

			sockaddr_storage->Family = cpu_to_le16(INTERNETWORK);
			sockaddr_storage->addr4.Port = 0;

			idev = __in_dev_get_rtnl(netdev);
			if (!idev)
				continue;
			sockaddr_storage->addr4.IPv4address =
						idev_ipv4_address(idev);
			nbytes += sizeof(struct network_interface_info_ioctl_rsp);
			ipv4_set = true;
			goto ipv6_retry;
		} else {
			struct inet6_dev *idev6;
			struct inet6_ifaddr *ifa;
			__u8 *ipv6_addr = sockaddr_storage->addr6.IPv6address;

			sockaddr_storage->Family = cpu_to_le16(INTERNETWORKV6);
			sockaddr_storage->addr6.Port = 0;
			sockaddr_storage->addr6.FlowInfo = 0;

			idev6 = __in6_dev_get(netdev);
			if (!idev6)
				continue;

			list_for_each_entry(ifa, &idev6->addr_list, if_list) {
				if (ifa->flags & (IFA_F_TENTATIVE |
							IFA_F_DEPRECATED))
					continue;
				memcpy(ipv6_addr, ifa->addr.s6_addr, 16);
				break;
			}
			sockaddr_storage->addr6.ScopeId = 0;
			nbytes += sizeof(struct network_interface_info_ioctl_rsp);
		}
	}
	rtnl_unlock();

	/* zero if this is last one */
	if (nii_rsp)
		nii_rsp->Next = 0;

	rsp->PersistentFileId = SMB2_NO_FID;
	rsp->VolatileFileId = SMB2_NO_FID;
	return nbytes;
}

static int fsctl_validate_negotiate_info(struct ksmbd_conn *conn,
					 struct validate_negotiate_info_req *neg_req,
					 struct validate_negotiate_info_rsp *neg_rsp,
					 unsigned int in_buf_len)
{
	int ret = 0;
	int dialect;

	if (in_buf_len < offsetof(struct validate_negotiate_info_req, Dialects) +
			le16_to_cpu(neg_req->DialectCount) * sizeof(__le16))
		return -EINVAL;

	dialect = ksmbd_lookup_dialect_by_id(neg_req->Dialects,
					     neg_req->DialectCount);
	if (dialect == BAD_PROT_ID || dialect != conn->dialect) {
		ret = -EINVAL;
		goto err_out;
	}

	if (strncmp(neg_req->Guid, conn->ClientGUID, SMB2_CLIENT_GUID_SIZE)) {
		ret = -EINVAL;
		goto err_out;
	}

	if (le16_to_cpu(neg_req->SecurityMode) != conn->cli_sec_mode) {
		ret = -EINVAL;
		goto err_out;
	}

	if (le32_to_cpu(neg_req->Capabilities) != conn->cli_cap) {
		ret = -EINVAL;
		goto err_out;
	}

	neg_rsp->Capabilities = cpu_to_le32(conn->vals->capabilities);
	memset(neg_rsp->Guid, 0, SMB2_CLIENT_GUID_SIZE);
	neg_rsp->SecurityMode = cpu_to_le16(conn->srv_sec_mode);
	neg_rsp->Dialect = cpu_to_le16(conn->dialect);
err_out:
	return ret;
}

static int fsctl_query_allocated_ranges(struct ksmbd_work *work, u64 id,
					struct file_allocated_range_buffer *qar_req,
					struct file_allocated_range_buffer *qar_rsp,
					unsigned int in_count, unsigned int *out_count)
{
	struct ksmbd_file *fp;
	loff_t start, length;
	int ret = 0;

	*out_count = 0;
	if (in_count == 0)
		return -EINVAL;

	start = le64_to_cpu(qar_req->file_offset);
	length = le64_to_cpu(qar_req->length);

	if (start < 0 || length < 0)
		return -EINVAL;

	fp = ksmbd_lookup_fd_fast(work, id);
	if (!fp)
		return -ENOENT;

	ret = ksmbd_vfs_fqar_lseek(fp, start, length,
				   qar_rsp, in_count, out_count);
	if (ret && ret != -E2BIG)
		*out_count = 0;

	ksmbd_fd_put(work, fp);
	return ret;
}

static int fsctl_pipe_transceive(struct ksmbd_work *work, u64 id,
				 unsigned int out_buf_len,
				 struct smb2_ioctl_req *req,
				 struct smb2_ioctl_rsp *rsp)
{
	struct ksmbd_rpc_command *rpc_resp;
	char *data_buf = (char *)req + le32_to_cpu(req->InputOffset);
	int nbytes = 0;

	rpc_resp = ksmbd_rpc_ioctl(work->sess, id, data_buf,
				   le32_to_cpu(req->InputCount));
	if (rpc_resp) {
		if (rpc_resp->flags == KSMBD_RPC_SOME_NOT_MAPPED) {
			/*
			 * set STATUS_SOME_NOT_MAPPED response
			 * for unknown domain sid.
			 */
			rsp->hdr.Status = STATUS_SOME_NOT_MAPPED;
		} else if (rpc_resp->flags == KSMBD_RPC_ENOTIMPLEMENTED) {
			rsp->hdr.Status = STATUS_NOT_SUPPORTED;
			goto out;
		} else if (rpc_resp->flags != KSMBD_RPC_OK) {
			rsp->hdr.Status = STATUS_INVALID_PARAMETER;
			goto out;
		}

		nbytes = rpc_resp->payload_sz;
		if (rpc_resp->payload_sz > out_buf_len) {
			rsp->hdr.Status = STATUS_BUFFER_OVERFLOW;
			nbytes = out_buf_len;
		}

		if (!rpc_resp->payload_sz) {
			rsp->hdr.Status =
				STATUS_UNEXPECTED_IO_ERROR;
			goto out;
		}

		memcpy((char *)rsp->Buffer, rpc_resp->payload, nbytes);
	}
out:
	kvfree(rpc_resp);
	return nbytes;
}

static inline int fsctl_set_sparse(struct ksmbd_work *work, u64 id,
				   struct file_sparse *sparse)
{
	struct ksmbd_file *fp;
	struct mnt_idmap *idmap;
	int ret = 0;
	__le32 old_fattr;

	fp = ksmbd_lookup_fd_fast(work, id);
	if (!fp)
		return -ENOENT;
	idmap = file_mnt_idmap(fp->filp);

	old_fattr = fp->f_ci->m_fattr;
	if (sparse->SetSparse)
		fp->f_ci->m_fattr |= FILE_ATTRIBUTE_SPARSE_FILE_LE;
	else
		fp->f_ci->m_fattr &= ~FILE_ATTRIBUTE_SPARSE_FILE_LE;

	if (fp->f_ci->m_fattr != old_fattr &&
	    test_share_config_flag(work->tcon->share_conf,
				   KSMBD_SHARE_FLAG_STORE_DOS_ATTRS)) {
		struct xattr_dos_attrib da;

		ret = ksmbd_vfs_get_dos_attrib_xattr(idmap,
						     fp->filp->f_path.dentry, &da);
		if (ret <= 0)
			goto out;

		da.attr = le32_to_cpu(fp->f_ci->m_fattr);
		ret = ksmbd_vfs_set_dos_attrib_xattr(idmap,
						     &fp->filp->f_path,
						     &da, true);
		if (ret)
			fp->f_ci->m_fattr = old_fattr;
	}

out:
	ksmbd_fd_put(work, fp);
	return ret;
}

static int fsctl_request_resume_key(struct ksmbd_work *work,
				    struct smb2_ioctl_req *req,
				    struct resume_key_ioctl_rsp *key_rsp)
{
	struct ksmbd_file *fp;

	fp = ksmbd_lookup_fd_slow(work, req->VolatileFileId, req->PersistentFileId);
	if (!fp)
		return -ENOENT;

	memset(key_rsp, 0, sizeof(*key_rsp));
	key_rsp->ResumeKey[0] = req->VolatileFileId;
	key_rsp->ResumeKey[1] = req->PersistentFileId;
	ksmbd_fd_put(work, fp);

	return 0;
}

/**
 * smb2_ioctl() - handler for smb2 ioctl command
 * @work:	smb work containing ioctl command buffer
 *
 * Return:	0 on success, otherwise error
 */
int smb2_ioctl(struct ksmbd_work *work)
{
	struct smb2_ioctl_req *req;
	struct smb2_ioctl_rsp *rsp;
	unsigned int cnt_code, nbytes = 0, out_buf_len, in_buf_len;
	u64 id = KSMBD_NO_FID;
	struct ksmbd_conn *conn = work->conn;
	int ret = 0;
	char *buffer;

	if (work->next_smb2_rcv_hdr_off) {
		req = ksmbd_req_buf_next(work);
		rsp = ksmbd_resp_buf_next(work);
		if (!has_file_id(req->VolatileFileId)) {
			ksmbd_debug(SMB, "Compound request set FID = %llu\n",
				    work->compound_fid);
			id = work->compound_fid;
		}
	} else {
		req = smb2_get_msg(work->request_buf);
		rsp = smb2_get_msg(work->response_buf);
	}

	if (!has_file_id(id))
		id = req->VolatileFileId;

	if (req->Flags != cpu_to_le32(SMB2_0_IOCTL_IS_FSCTL)) {
		rsp->hdr.Status = STATUS_NOT_SUPPORTED;
		goto out;
	}

	buffer = (char *)req + le32_to_cpu(req->InputOffset);

	cnt_code = le32_to_cpu(req->CtlCode);
	ret = smb2_calc_max_out_buf_len(work, 48,
					le32_to_cpu(req->MaxOutputResponse));
	if (ret < 0) {
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		goto out;
	}
	out_buf_len = (unsigned int)ret;
	in_buf_len = le32_to_cpu(req->InputCount);

	switch (cnt_code) {
	case FSCTL_DFS_GET_REFERRALS:
	case FSCTL_DFS_GET_REFERRALS_EX:
		/* Not support DFS yet */
		rsp->hdr.Status = STATUS_FS_DRIVER_REQUIRED;
		goto out;
	case FSCTL_CREATE_OR_GET_OBJECT_ID:
	{
		struct file_object_buf_type1_ioctl_rsp *obj_buf;

		nbytes = sizeof(struct file_object_buf_type1_ioctl_rsp);
		obj_buf = (struct file_object_buf_type1_ioctl_rsp *)
			&rsp->Buffer[0];

		/*
		 * TODO: This is dummy implementation to pass smbtorture
		 * Need to check correct response later
		 */
		memset(obj_buf->ObjectId, 0x0, 16);
		memset(obj_buf->BirthVolumeId, 0x0, 16);
		memset(obj_buf->BirthObjectId, 0x0, 16);
		memset(obj_buf->DomainId, 0x0, 16);

		break;
	}
	case FSCTL_PIPE_TRANSCEIVE:
		out_buf_len = min_t(u32, KSMBD_IPC_MAX_PAYLOAD, out_buf_len);
		nbytes = fsctl_pipe_transceive(work, id, out_buf_len, req, rsp);
		break;
	case FSCTL_VALIDATE_NEGOTIATE_INFO:
		if (conn->dialect < SMB30_PROT_ID) {
			ret = -EOPNOTSUPP;
			goto out;
		}

		if (in_buf_len < offsetof(struct validate_negotiate_info_req,
					  Dialects)) {
			ret = -EINVAL;
			goto out;
		}

		if (out_buf_len < sizeof(struct validate_negotiate_info_rsp)) {
			ret = -EINVAL;
			goto out;
		}

		ret = fsctl_validate_negotiate_info(conn,
			(struct validate_negotiate_info_req *)buffer,
			(struct validate_negotiate_info_rsp *)&rsp->Buffer[0],
			in_buf_len);
		if (ret < 0)
			goto out;

		nbytes = sizeof(struct validate_negotiate_info_rsp);
		rsp->PersistentFileId = SMB2_NO_FID;
		rsp->VolatileFileId = SMB2_NO_FID;
		break;
	case FSCTL_QUERY_NETWORK_INTERFACE_INFO:
		ret = fsctl_query_iface_info_ioctl(conn, rsp, out_buf_len);
		if (ret < 0)
			goto out;
		nbytes = ret;
		break;
	case FSCTL_REQUEST_RESUME_KEY:
		if (out_buf_len < sizeof(struct resume_key_ioctl_rsp)) {
			ret = -EINVAL;
			goto out;
		}

		ret = fsctl_request_resume_key(work, req,
					       (struct resume_key_ioctl_rsp *)&rsp->Buffer[0]);
		if (ret < 0)
			goto out;
		rsp->PersistentFileId = req->PersistentFileId;
		rsp->VolatileFileId = req->VolatileFileId;
		nbytes = sizeof(struct resume_key_ioctl_rsp);
		break;
	case FSCTL_COPYCHUNK:
	case FSCTL_COPYCHUNK_WRITE:
		if (!test_tree_conn_flag(work->tcon, KSMBD_TREE_CONN_FLAG_WRITABLE)) {
			ksmbd_debug(SMB,
				    "User does not have write permission\n");
			ret = -EACCES;
			goto out;
		}

		if (in_buf_len <= sizeof(struct copychunk_ioctl_req)) {
			ret = -EINVAL;
			goto out;
		}

		if (out_buf_len < sizeof(struct copychunk_ioctl_rsp)) {
			ret = -EINVAL;
			goto out;
		}

		nbytes = sizeof(struct copychunk_ioctl_rsp);
		rsp->VolatileFileId = req->VolatileFileId;
		rsp->PersistentFileId = req->PersistentFileId;
		fsctl_copychunk(work,
				(struct copychunk_ioctl_req *)buffer,
				le32_to_cpu(req->CtlCode),
				le32_to_cpu(req->InputCount),
				req->VolatileFileId,
				req->PersistentFileId,
				rsp);
		break;
	case FSCTL_SET_SPARSE:
		if (in_buf_len < sizeof(struct file_sparse)) {
			ret = -EINVAL;
			goto out;
		}

		ret = fsctl_set_sparse(work, id, (struct file_sparse *)buffer);
		if (ret < 0)
			goto out;
		break;
	case FSCTL_SET_ZERO_DATA:
	{
		struct file_zero_data_information *zero_data;
		struct ksmbd_file *fp;
		loff_t off, len, bfz;

		if (!test_tree_conn_flag(work->tcon, KSMBD_TREE_CONN_FLAG_WRITABLE)) {
			ksmbd_debug(SMB,
				    "User does not have write permission\n");
			ret = -EACCES;
			goto out;
		}

		if (in_buf_len < sizeof(struct file_zero_data_information)) {
			ret = -EINVAL;
			goto out;
		}

		zero_data =
			(struct file_zero_data_information *)buffer;

		off = le64_to_cpu(zero_data->FileOffset);
		bfz = le64_to_cpu(zero_data->BeyondFinalZero);
		if (off < 0 || bfz < 0 || off > bfz) {
			ret = -EINVAL;
			goto out;
		}

		len = bfz - off;
		if (len) {
			fp = ksmbd_lookup_fd_fast(work, id);
			if (!fp) {
				ret = -ENOENT;
				goto out;
			}

			ret = ksmbd_vfs_zero_data(work, fp, off, len);
			ksmbd_fd_put(work, fp);
			if (ret < 0)
				goto out;
		}
		break;
	}
	case FSCTL_QUERY_ALLOCATED_RANGES:
		if (in_buf_len < sizeof(struct file_allocated_range_buffer)) {
			ret = -EINVAL;
			goto out;
		}

		ret = fsctl_query_allocated_ranges(work, id,
			(struct file_allocated_range_buffer *)buffer,
			(struct file_allocated_range_buffer *)&rsp->Buffer[0],
			out_buf_len /
			sizeof(struct file_allocated_range_buffer), &nbytes);
		if (ret == -E2BIG) {
			rsp->hdr.Status = STATUS_BUFFER_OVERFLOW;
		} else if (ret < 0) {
			nbytes = 0;
			goto out;
		}

		nbytes *= sizeof(struct file_allocated_range_buffer);
		break;
	case FSCTL_GET_REPARSE_POINT:
	{
		struct reparse_data_buffer *reparse_ptr;
		struct ksmbd_file *fp;

		reparse_ptr = (struct reparse_data_buffer *)&rsp->Buffer[0];
		fp = ksmbd_lookup_fd_fast(work, id);
		if (!fp) {
			pr_err("not found fp!!\n");
			ret = -ENOENT;
			goto out;
		}

		reparse_ptr->ReparseTag =
			smb2_get_reparse_tag_special_file(file_inode(fp->filp)->i_mode);
		reparse_ptr->ReparseDataLength = 0;
		ksmbd_fd_put(work, fp);
		nbytes = sizeof(struct reparse_data_buffer);
		break;
	}
	case FSCTL_DUPLICATE_EXTENTS_TO_FILE:
	{
		struct ksmbd_file *fp_in, *fp_out = NULL;
		struct duplicate_extents_to_file *dup_ext;
		loff_t src_off, dst_off, length, cloned;

		if (in_buf_len < sizeof(struct duplicate_extents_to_file)) {
			ret = -EINVAL;
			goto out;
		}

		dup_ext = (struct duplicate_extents_to_file *)buffer;

		fp_in = ksmbd_lookup_fd_slow(work, dup_ext->VolatileFileHandle,
					     dup_ext->PersistentFileHandle);
		if (!fp_in) {
			pr_err("not found file handle in duplicate extent to file\n");
			ret = -ENOENT;
			goto out;
		}

		fp_out = ksmbd_lookup_fd_fast(work, id);
		if (!fp_out) {
			pr_err("not found fp\n");
			ret = -ENOENT;
			goto dup_ext_out;
		}

		src_off = le64_to_cpu(dup_ext->SourceFileOffset);
		dst_off = le64_to_cpu(dup_ext->TargetFileOffset);
		length = le64_to_cpu(dup_ext->ByteCount);
		/*
		 * XXX: It is not clear if FSCTL_DUPLICATE_EXTENTS_TO_FILE
		 * should fall back to vfs_copy_file_range().  This could be
		 * beneficial when re-exporting nfs/smb mount, but note that
		 * this can result in partial copy that returns an error status.
		 * If/when FSCTL_DUPLICATE_EXTENTS_TO_FILE_EX is implemented,
		 * fall back to vfs_copy_file_range(), should be avoided when
		 * the flag DUPLICATE_EXTENTS_DATA_EX_SOURCE_ATOMIC is set.
		 */
		cloned = vfs_clone_file_range(fp_in->filp, src_off,
					      fp_out->filp, dst_off, length, 0);
		if (cloned == -EXDEV || cloned == -EOPNOTSUPP) {
			ret = -EOPNOTSUPP;
			goto dup_ext_out;
		} else if (cloned != length) {
			cloned = vfs_copy_file_range(fp_in->filp, src_off,
						     fp_out->filp, dst_off,
						     length, 0);
			if (cloned != length) {
				if (cloned < 0)
					ret = cloned;
				else
					ret = -EINVAL;
			}
		}

dup_ext_out:
		ksmbd_fd_put(work, fp_in);
		ksmbd_fd_put(work, fp_out);
		if (ret < 0)
			goto out;
		break;
	}
	default:
		ksmbd_debug(SMB, "not implemented yet ioctl command 0x%x\n",
			    cnt_code);
		ret = -EOPNOTSUPP;
		goto out;
	}

	rsp->CtlCode = cpu_to_le32(cnt_code);
	rsp->InputCount = cpu_to_le32(0);
	rsp->InputOffset = cpu_to_le32(112);
	rsp->OutputOffset = cpu_to_le32(112);
	rsp->OutputCount = cpu_to_le32(nbytes);
	rsp->StructureSize = cpu_to_le16(49);
	rsp->Reserved = cpu_to_le16(0);
	rsp->Flags = cpu_to_le32(0);
	rsp->Reserved2 = cpu_to_le32(0);
	ret = ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_ioctl_rsp) + nbytes);
	if (!ret)
		return ret;

out:
	if (ret == -EACCES)
		rsp->hdr.Status = STATUS_ACCESS_DENIED;
	else if (ret == -ENOENT)
		rsp->hdr.Status = STATUS_OBJECT_NAME_NOT_FOUND;
	else if (ret == -EOPNOTSUPP)
		rsp->hdr.Status = STATUS_NOT_SUPPORTED;
	else if (ret == -ENOSPC)
		rsp->hdr.Status = STATUS_BUFFER_TOO_SMALL;
	else if (ret < 0 || rsp->hdr.Status == 0)
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
	smb2_set_err_rsp(work);
	return 0;
}

/**
 * smb20_oplock_break_ack() - handler for smb2.0 oplock break command
 * @work:	smb work containing oplock break command buffer
 *
 * Return:	0
 */
static void smb20_oplock_break_ack(struct ksmbd_work *work)
{
	struct smb2_oplock_break *req;
	struct smb2_oplock_break *rsp;
	struct ksmbd_file *fp;
	struct oplock_info *opinfo = NULL;
	__le32 err = 0;
	int ret = 0;
	u64 volatile_id, persistent_id;
	char req_oplevel = 0, rsp_oplevel = 0;
	unsigned int oplock_change_type;

	WORK_BUFFERS(work, req, rsp);

	volatile_id = req->VolatileFid;
	persistent_id = req->PersistentFid;
	req_oplevel = req->OplockLevel;
	ksmbd_debug(OPLOCK, "v_id %llu, p_id %llu request oplock level %d\n",
		    volatile_id, persistent_id, req_oplevel);

	fp = ksmbd_lookup_fd_slow(work, volatile_id, persistent_id);
	if (!fp) {
		rsp->hdr.Status = STATUS_FILE_CLOSED;
		smb2_set_err_rsp(work);
		return;
	}

	opinfo = opinfo_get(fp);
	if (!opinfo) {
		pr_err("unexpected null oplock_info\n");
		rsp->hdr.Status = STATUS_INVALID_OPLOCK_PROTOCOL;
		smb2_set_err_rsp(work);
		ksmbd_fd_put(work, fp);
		return;
	}

	if (opinfo->level == SMB2_OPLOCK_LEVEL_NONE) {
		rsp->hdr.Status = STATUS_INVALID_OPLOCK_PROTOCOL;
		goto err_out;
	}

	if (opinfo->op_state == OPLOCK_STATE_NONE) {
		ksmbd_debug(SMB, "unexpected oplock state 0x%x\n", opinfo->op_state);
		rsp->hdr.Status = STATUS_UNSUCCESSFUL;
		goto err_out;
	}

	if ((opinfo->level == SMB2_OPLOCK_LEVEL_EXCLUSIVE ||
	     opinfo->level == SMB2_OPLOCK_LEVEL_BATCH) &&
	    (req_oplevel != SMB2_OPLOCK_LEVEL_II &&
	     req_oplevel != SMB2_OPLOCK_LEVEL_NONE)) {
		err = STATUS_INVALID_OPLOCK_PROTOCOL;
		oplock_change_type = OPLOCK_WRITE_TO_NONE;
	} else if (opinfo->level == SMB2_OPLOCK_LEVEL_II &&
		   req_oplevel != SMB2_OPLOCK_LEVEL_NONE) {
		err = STATUS_INVALID_OPLOCK_PROTOCOL;
		oplock_change_type = OPLOCK_READ_TO_NONE;
	} else if (req_oplevel == SMB2_OPLOCK_LEVEL_II ||
		   req_oplevel == SMB2_OPLOCK_LEVEL_NONE) {
		err = STATUS_INVALID_DEVICE_STATE;
		if ((opinfo->level == SMB2_OPLOCK_LEVEL_EXCLUSIVE ||
		     opinfo->level == SMB2_OPLOCK_LEVEL_BATCH) &&
		    req_oplevel == SMB2_OPLOCK_LEVEL_II) {
			oplock_change_type = OPLOCK_WRITE_TO_READ;
		} else if ((opinfo->level == SMB2_OPLOCK_LEVEL_EXCLUSIVE ||
			    opinfo->level == SMB2_OPLOCK_LEVEL_BATCH) &&
			   req_oplevel == SMB2_OPLOCK_LEVEL_NONE) {
			oplock_change_type = OPLOCK_WRITE_TO_NONE;
		} else if (opinfo->level == SMB2_OPLOCK_LEVEL_II &&
			   req_oplevel == SMB2_OPLOCK_LEVEL_NONE) {
			oplock_change_type = OPLOCK_READ_TO_NONE;
		} else {
			oplock_change_type = 0;
		}
	} else {
		oplock_change_type = 0;
	}

	switch (oplock_change_type) {
	case OPLOCK_WRITE_TO_READ:
		ret = opinfo_write_to_read(opinfo);
		rsp_oplevel = SMB2_OPLOCK_LEVEL_II;
		break;
	case OPLOCK_WRITE_TO_NONE:
		ret = opinfo_write_to_none(opinfo);
		rsp_oplevel = SMB2_OPLOCK_LEVEL_NONE;
		break;
	case OPLOCK_READ_TO_NONE:
		ret = opinfo_read_to_none(opinfo);
		rsp_oplevel = SMB2_OPLOCK_LEVEL_NONE;
		break;
	default:
		pr_err("unknown oplock change 0x%x -> 0x%x\n",
		       opinfo->level, rsp_oplevel);
	}

	if (ret < 0) {
		rsp->hdr.Status = err;
		goto err_out;
	}

	opinfo->op_state = OPLOCK_STATE_NONE;
	wake_up_interruptible_all(&opinfo->oplock_q);
	opinfo_put(opinfo);
	ksmbd_fd_put(work, fp);

	rsp->StructureSize = cpu_to_le16(24);
	rsp->OplockLevel = rsp_oplevel;
	rsp->Reserved = 0;
	rsp->Reserved2 = 0;
	rsp->VolatileFid = volatile_id;
	rsp->PersistentFid = persistent_id;
	ret = ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_oplock_break));
	if (!ret)
		return;

err_out:
	opinfo->op_state = OPLOCK_STATE_NONE;
	wake_up_interruptible_all(&opinfo->oplock_q);

	opinfo_put(opinfo);
	ksmbd_fd_put(work, fp);
	smb2_set_err_rsp(work);
}

static int check_lease_state(struct lease *lease, __le32 req_state)
{
	if ((lease->new_state ==
	     (SMB2_LEASE_READ_CACHING_LE | SMB2_LEASE_HANDLE_CACHING_LE)) &&
	    !(req_state & SMB2_LEASE_WRITE_CACHING_LE)) {
		lease->new_state = req_state;
		return 0;
	}

	if (lease->new_state == req_state)
		return 0;

	return 1;
}

/**
 * smb21_lease_break_ack() - handler for smb2.1 lease break command
 * @work:	smb work containing lease break command buffer
 *
 * Return:	0
 */
static void smb21_lease_break_ack(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_lease_ack *req;
	struct smb2_lease_ack *rsp;
	struct oplock_info *opinfo;
	__le32 err = 0;
	int ret = 0;
	unsigned int lease_change_type;
	__le32 lease_state;
	struct lease *lease;

	WORK_BUFFERS(work, req, rsp);

	ksmbd_debug(OPLOCK, "smb21 lease break, lease state(0x%x)\n",
		    le32_to_cpu(req->LeaseState));
	opinfo = lookup_lease_in_table(conn, req->LeaseKey);
	if (!opinfo) {
		ksmbd_debug(OPLOCK, "file not opened\n");
		smb2_set_err_rsp(work);
		rsp->hdr.Status = STATUS_UNSUCCESSFUL;
		return;
	}
	lease = opinfo->o_lease;

	if (opinfo->op_state == OPLOCK_STATE_NONE) {
		pr_err("unexpected lease break state 0x%x\n",
		       opinfo->op_state);
		rsp->hdr.Status = STATUS_UNSUCCESSFUL;
		goto err_out;
	}

	if (check_lease_state(lease, req->LeaseState)) {
		rsp->hdr.Status = STATUS_REQUEST_NOT_ACCEPTED;
		ksmbd_debug(OPLOCK,
			    "req lease state: 0x%x, expected state: 0x%x\n",
			    req->LeaseState, lease->new_state);
		goto err_out;
	}

	if (!atomic_read(&opinfo->breaking_cnt)) {
		rsp->hdr.Status = STATUS_UNSUCCESSFUL;
		goto err_out;
	}

	/* check for bad lease state */
	if (req->LeaseState &
	    (~(SMB2_LEASE_READ_CACHING_LE | SMB2_LEASE_HANDLE_CACHING_LE))) {
		err = STATUS_INVALID_OPLOCK_PROTOCOL;
		if (lease->state & SMB2_LEASE_WRITE_CACHING_LE)
			lease_change_type = OPLOCK_WRITE_TO_NONE;
		else
			lease_change_type = OPLOCK_READ_TO_NONE;
		ksmbd_debug(OPLOCK, "handle bad lease state 0x%x -> 0x%x\n",
			    le32_to_cpu(lease->state),
			    le32_to_cpu(req->LeaseState));
	} else if (lease->state == SMB2_LEASE_READ_CACHING_LE &&
		   req->LeaseState != SMB2_LEASE_NONE_LE) {
		err = STATUS_INVALID_OPLOCK_PROTOCOL;
		lease_change_type = OPLOCK_READ_TO_NONE;
		ksmbd_debug(OPLOCK, "handle bad lease state 0x%x -> 0x%x\n",
			    le32_to_cpu(lease->state),
			    le32_to_cpu(req->LeaseState));
	} else {
		/* valid lease state changes */
		err = STATUS_INVALID_DEVICE_STATE;
		if (req->LeaseState == SMB2_LEASE_NONE_LE) {
			if (lease->state & SMB2_LEASE_WRITE_CACHING_LE)
				lease_change_type = OPLOCK_WRITE_TO_NONE;
			else
				lease_change_type = OPLOCK_READ_TO_NONE;
		} else if (req->LeaseState & SMB2_LEASE_READ_CACHING_LE) {
			if (lease->state & SMB2_LEASE_WRITE_CACHING_LE)
				lease_change_type = OPLOCK_WRITE_TO_READ;
			else
				lease_change_type = OPLOCK_READ_HANDLE_TO_READ;
		} else {
			lease_change_type = 0;
		}
	}

	switch (lease_change_type) {
	case OPLOCK_WRITE_TO_READ:
		ret = opinfo_write_to_read(opinfo);
		break;
	case OPLOCK_READ_HANDLE_TO_READ:
		ret = opinfo_read_handle_to_read(opinfo);
		break;
	case OPLOCK_WRITE_TO_NONE:
		ret = opinfo_write_to_none(opinfo);
		break;
	case OPLOCK_READ_TO_NONE:
		ret = opinfo_read_to_none(opinfo);
		break;
	default:
		ksmbd_debug(OPLOCK, "unknown lease change 0x%x -> 0x%x\n",
			    le32_to_cpu(lease->state),
			    le32_to_cpu(req->LeaseState));
	}

	if (ret < 0) {
		rsp->hdr.Status = err;
		goto err_out;
	}

	lease_state = lease->state;
	opinfo->op_state = OPLOCK_STATE_NONE;
	wake_up_interruptible_all(&opinfo->oplock_q);
	atomic_dec(&opinfo->breaking_cnt);
	wake_up_interruptible_all(&opinfo->oplock_brk);
	opinfo_put(opinfo);

	rsp->StructureSize = cpu_to_le16(36);
	rsp->Reserved = 0;
	rsp->Flags = 0;
	memcpy(rsp->LeaseKey, req->LeaseKey, 16);
	rsp->LeaseState = lease_state;
	rsp->LeaseDuration = 0;
	ret = ksmbd_iov_pin_rsp(work, rsp, sizeof(struct smb2_lease_ack));
	if (!ret)
		return;

err_out:
	wake_up_interruptible_all(&opinfo->oplock_q);
	atomic_dec(&opinfo->breaking_cnt);
	wake_up_interruptible_all(&opinfo->oplock_brk);

	opinfo_put(opinfo);
	smb2_set_err_rsp(work);
}

/**
 * smb2_oplock_break() - dispatcher for smb2.0 and 2.1 oplock/lease break
 * @work:	smb work containing oplock/lease break command buffer
 *
 * Return:	0
 */
int smb2_oplock_break(struct ksmbd_work *work)
{
	struct smb2_oplock_break *req;
	struct smb2_oplock_break *rsp;

	WORK_BUFFERS(work, req, rsp);

	switch (le16_to_cpu(req->StructureSize)) {
	case OP_BREAK_STRUCT_SIZE_20:
		smb20_oplock_break_ack(work);
		break;
	case OP_BREAK_STRUCT_SIZE_21:
		smb21_lease_break_ack(work);
		break;
	default:
		ksmbd_debug(OPLOCK, "invalid break cmd %d\n",
			    le16_to_cpu(req->StructureSize));
		rsp->hdr.Status = STATUS_INVALID_PARAMETER;
		smb2_set_err_rsp(work);
	}

	return 0;
}

/**
 * smb2_notify() - handler for smb2 notify request
 * @work:   smb work containing notify command buffer
 *
 * Return:      0
 */
int smb2_notify(struct ksmbd_work *work)
{
	struct smb2_change_notify_req *req;
	struct smb2_change_notify_rsp *rsp;

	WORK_BUFFERS(work, req, rsp);

	if (work->next_smb2_rcv_hdr_off && req->hdr.NextCommand) {
		rsp->hdr.Status = STATUS_INTERNAL_ERROR;
		smb2_set_err_rsp(work);
		return 0;
	}

	smb2_set_err_rsp(work);
	rsp->hdr.Status = STATUS_NOT_IMPLEMENTED;
	return 0;
}

/**
 * smb2_is_sign_req() - handler for checking packet signing status
 * @work:	smb work containing notify command buffer
 * @command:	SMB2 command id
 *
 * Return:	true if packed is signed, false otherwise
 */
bool smb2_is_sign_req(struct ksmbd_work *work, unsigned int command)
{
	struct smb2_hdr *rcv_hdr2 = smb2_get_msg(work->request_buf);

	if ((rcv_hdr2->Flags & SMB2_FLAGS_SIGNED) &&
	    command != SMB2_NEGOTIATE_HE &&
	    command != SMB2_SESSION_SETUP_HE &&
	    command != SMB2_OPLOCK_BREAK_HE)
		return true;

	return false;
}

/**
 * smb2_check_sign_req() - handler for req packet sign processing
 * @work:   smb work containing notify command buffer
 *
 * Return:	1 on success, 0 otherwise
 */
int smb2_check_sign_req(struct ksmbd_work *work)
{
	struct smb2_hdr *hdr;
	char signature_req[SMB2_SIGNATURE_SIZE];
	char signature[SMB2_HMACSHA256_SIZE];
	struct kvec iov[1];
	size_t len;

	hdr = smb2_get_msg(work->request_buf);
	if (work->next_smb2_rcv_hdr_off)
		hdr = ksmbd_req_buf_next(work);

	if (!hdr->NextCommand && !work->next_smb2_rcv_hdr_off)
		len = get_rfc1002_len(work->request_buf);
	else if (hdr->NextCommand)
		len = le32_to_cpu(hdr->NextCommand);
	else
		len = get_rfc1002_len(work->request_buf) -
			work->next_smb2_rcv_hdr_off;

	memcpy(signature_req, hdr->Signature, SMB2_SIGNATURE_SIZE);
	memset(hdr->Signature, 0, SMB2_SIGNATURE_SIZE);

	iov[0].iov_base = (char *)&hdr->ProtocolId;
	iov[0].iov_len = len;

	if (ksmbd_sign_smb2_pdu(work->conn, work->sess->sess_key, iov, 1,
				signature))
		return 0;

	if (memcmp(signature, signature_req, SMB2_SIGNATURE_SIZE)) {
		pr_err("bad smb2 signature\n");
		return 0;
	}

	return 1;
}

/**
 * smb2_set_sign_rsp() - handler for rsp packet sign processing
 * @work:   smb work containing notify command buffer
 *
 */
void smb2_set_sign_rsp(struct ksmbd_work *work)
{
	struct smb2_hdr *hdr;
	char signature[SMB2_HMACSHA256_SIZE];
	struct kvec *iov;
	int n_vec = 1;

	hdr = ksmbd_resp_buf_curr(work);
	hdr->Flags |= SMB2_FLAGS_SIGNED;
	memset(hdr->Signature, 0, SMB2_SIGNATURE_SIZE);

	if (hdr->Command == SMB2_READ) {
		iov = &work->iov[work->iov_idx - 1];
		n_vec++;
	} else {
		iov = &work->iov[work->iov_idx];
	}

	if (!ksmbd_sign_smb2_pdu(work->conn, work->sess->sess_key, iov, n_vec,
				 signature))
		memcpy(hdr->Signature, signature, SMB2_SIGNATURE_SIZE);
}

/**
 * smb3_check_sign_req() - handler for req packet sign processing
 * @work:   smb work containing notify command buffer
 *
 * Return:	1 on success, 0 otherwise
 */
int smb3_check_sign_req(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	char *signing_key;
	struct smb2_hdr *hdr;
	struct channel *chann;
	char signature_req[SMB2_SIGNATURE_SIZE];
	char signature[SMB2_CMACAES_SIZE];
	struct kvec iov[1];
	size_t len;

	hdr = smb2_get_msg(work->request_buf);
	if (work->next_smb2_rcv_hdr_off)
		hdr = ksmbd_req_buf_next(work);

	if (!hdr->NextCommand && !work->next_smb2_rcv_hdr_off)
		len = get_rfc1002_len(work->request_buf);
	else if (hdr->NextCommand)
		len = le32_to_cpu(hdr->NextCommand);
	else
		len = get_rfc1002_len(work->request_buf) -
			work->next_smb2_rcv_hdr_off;

	if (le16_to_cpu(hdr->Command) == SMB2_SESSION_SETUP_HE) {
		signing_key = work->sess->smb3signingkey;
	} else {
		chann = lookup_chann_list(work->sess, conn);
		if (!chann) {
			return 0;
		}
		signing_key = chann->smb3signingkey;
	}

	if (!signing_key) {
		pr_err("SMB3 signing key is not generated\n");
		return 0;
	}

	memcpy(signature_req, hdr->Signature, SMB2_SIGNATURE_SIZE);
	memset(hdr->Signature, 0, SMB2_SIGNATURE_SIZE);
	iov[0].iov_base = (char *)&hdr->ProtocolId;
	iov[0].iov_len = len;

	if (ksmbd_sign_smb3_pdu(conn, signing_key, iov, 1, signature))
		return 0;

	if (memcmp(signature, signature_req, SMB2_SIGNATURE_SIZE)) {
		pr_err("bad smb2 signature\n");
		return 0;
	}

	return 1;
}

/**
 * smb3_set_sign_rsp() - handler for rsp packet sign processing
 * @work:   smb work containing notify command buffer
 *
 */
void smb3_set_sign_rsp(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct smb2_hdr *hdr;
	struct channel *chann;
	char signature[SMB2_CMACAES_SIZE];
	struct kvec *iov;
	int n_vec = 1;
	char *signing_key;

	hdr = ksmbd_resp_buf_curr(work);

	if (conn->binding == false &&
	    le16_to_cpu(hdr->Command) == SMB2_SESSION_SETUP_HE) {
		signing_key = work->sess->smb3signingkey;
	} else {
		chann = lookup_chann_list(work->sess, work->conn);
		if (!chann) {
			return;
		}
		signing_key = chann->smb3signingkey;
	}

	if (!signing_key)
		return;

	hdr->Flags |= SMB2_FLAGS_SIGNED;
	memset(hdr->Signature, 0, SMB2_SIGNATURE_SIZE);

	if (hdr->Command == SMB2_READ) {
		iov = &work->iov[work->iov_idx - 1];
		n_vec++;
	} else {
		iov = &work->iov[work->iov_idx];
	}

	if (!ksmbd_sign_smb3_pdu(conn, signing_key, iov, n_vec,
				 signature))
		memcpy(hdr->Signature, signature, SMB2_SIGNATURE_SIZE);
}

/**
 * smb3_preauth_hash_rsp() - handler for computing preauth hash on response
 * @work:   smb work containing response buffer
 *
 */
void smb3_preauth_hash_rsp(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	struct smb2_hdr *req, *rsp;

	if (conn->dialect != SMB311_PROT_ID)
		return;

	WORK_BUFFERS(work, req, rsp);

	if (le16_to_cpu(req->Command) == SMB2_NEGOTIATE_HE &&
	    conn->preauth_info)
		ksmbd_gen_preauth_integrity_hash(conn, work->response_buf,
						 conn->preauth_info->Preauth_HashValue);

	if (le16_to_cpu(rsp->Command) == SMB2_SESSION_SETUP_HE && sess) {
		__u8 *hash_value;

		if (conn->binding) {
			struct preauth_session *preauth_sess;

			preauth_sess = ksmbd_preauth_session_lookup(conn, sess->id);
			if (!preauth_sess)
				return;
			hash_value = preauth_sess->Preauth_HashValue;
		} else {
			hash_value = sess->Preauth_HashValue;
			if (!hash_value)
				return;
		}
		ksmbd_gen_preauth_integrity_hash(conn, work->response_buf,
						 hash_value);
	}
}

static void fill_transform_hdr(void *tr_buf, char *old_buf, __le16 cipher_type)
{
	struct smb2_transform_hdr *tr_hdr = tr_buf + 4;
	struct smb2_hdr *hdr = smb2_get_msg(old_buf);
	unsigned int orig_len = get_rfc1002_len(old_buf);

	/* tr_buf must be cleared by the caller */
	tr_hdr->ProtocolId = SMB2_TRANSFORM_PROTO_NUM;
	tr_hdr->OriginalMessageSize = cpu_to_le32(orig_len);
	tr_hdr->Flags = cpu_to_le16(TRANSFORM_FLAG_ENCRYPTED);
	if (cipher_type == SMB2_ENCRYPTION_AES128_GCM ||
	    cipher_type == SMB2_ENCRYPTION_AES256_GCM)
		get_random_bytes(&tr_hdr->Nonce, SMB3_AES_GCM_NONCE);
	else
		get_random_bytes(&tr_hdr->Nonce, SMB3_AES_CCM_NONCE);
	memcpy(&tr_hdr->SessionId, &hdr->SessionId, 8);
	inc_rfc1001_len(tr_buf, sizeof(struct smb2_transform_hdr));
	inc_rfc1001_len(tr_buf, orig_len);
}

int smb3_encrypt_resp(struct ksmbd_work *work)
{
	struct kvec *iov = work->iov;
	int rc = -ENOMEM;
	void *tr_buf;

	tr_buf = kzalloc(sizeof(struct smb2_transform_hdr) + 4, GFP_KERNEL);
	if (!tr_buf)
		return rc;

	/* fill transform header */
	fill_transform_hdr(tr_buf, work->response_buf, work->conn->cipher_type);

	iov[0].iov_base = tr_buf;
	iov[0].iov_len = sizeof(struct smb2_transform_hdr) + 4;
	work->tr_buf = tr_buf;

	return ksmbd_crypt_message(work, iov, work->iov_idx + 1, 1);
}

bool smb3_is_transform_hdr(void *buf)
{
	struct smb2_transform_hdr *trhdr = smb2_get_msg(buf);

	return trhdr->ProtocolId == SMB2_TRANSFORM_PROTO_NUM;
}

int smb3_decrypt_req(struct ksmbd_work *work)
{
	struct ksmbd_session *sess;
	char *buf = work->request_buf;
	unsigned int pdu_length = get_rfc1002_len(buf);
	struct kvec iov[2];
	int buf_data_size = pdu_length - sizeof(struct smb2_transform_hdr);
	struct smb2_transform_hdr *tr_hdr = smb2_get_msg(buf);
	int rc = 0;

	if (pdu_length < sizeof(struct smb2_transform_hdr) ||
	    buf_data_size < sizeof(struct smb2_hdr)) {
		pr_err("Transform message is too small (%u)\n",
		       pdu_length);
		return -ECONNABORTED;
	}

	if (buf_data_size < le32_to_cpu(tr_hdr->OriginalMessageSize)) {
		pr_err("Transform message is broken\n");
		return -ECONNABORTED;
	}

	sess = ksmbd_session_lookup_all(work->conn, le64_to_cpu(tr_hdr->SessionId));
	if (!sess) {
		pr_err("invalid session id(%llx) in transform header\n",
		       le64_to_cpu(tr_hdr->SessionId));
		return -ECONNABORTED;
	}

	iov[0].iov_base = buf;
	iov[0].iov_len = sizeof(struct smb2_transform_hdr) + 4;
	iov[1].iov_base = buf + sizeof(struct smb2_transform_hdr) + 4;
	iov[1].iov_len = buf_data_size;
	rc = ksmbd_crypt_message(work, iov, 2, 0);
	if (rc)
		return rc;

	memmove(buf + 4, iov[1].iov_base, buf_data_size);
	*(__be32 *)buf = cpu_to_be32(buf_data_size);

	return rc;
}

bool smb3_11_final_sess_setup_resp(struct ksmbd_work *work)
{
	struct ksmbd_conn *conn = work->conn;
	struct ksmbd_session *sess = work->sess;
	struct smb2_hdr *rsp = smb2_get_msg(work->response_buf);

	if (conn->dialect < SMB30_PROT_ID)
		return false;

	if (work->next_smb2_rcv_hdr_off)
		rsp = ksmbd_resp_buf_next(work);

	if (le16_to_cpu(rsp->Command) == SMB2_SESSION_SETUP_HE &&
	    sess->user && !user_guest(sess->user) &&
	    rsp->Status == STATUS_SUCCESS)
		return true;
	return false;
}
