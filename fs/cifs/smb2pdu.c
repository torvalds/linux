/*
 *   fs/cifs/smb2pdu.c
 *
 *   Copyright (C) International Business Machines  Corp., 2009, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 *   Contains the routines for constructing the SMB2 PDUs themselves
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

 /* SMB2 PDU handling routines here - except for leftovers (eg session setup) */
 /* Note that there are handle based routines which must be		      */
 /* treated slightly differently for reconnection purposes since we never     */
 /* want to reuse a stale file handle and only the caller knows the file info */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/vfs.h>
#include <linux/uaccess.h>
#include <linux/xattr.h>
#include "smb2pdu.h"
#include "cifsglob.h"
#include "cifsacl.h"
#include "cifsproto.h"
#include "smb2proto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "ntlmssp.h"
#include "smb2status.h"

/*
 *  The following table defines the expected "StructureSize" of SMB2 requests
 *  in order by SMB2 command.  This is similar to "wct" in SMB/CIFS requests.
 *
 *  Note that commands are defined in smb2pdu.h in le16 but the array below is
 *  indexed by command in host byte order.
 */
static const int smb2_req_struct_sizes[NUMBER_OF_SMB2_COMMANDS] = {
	/* SMB2_NEGOTIATE */ 36,
	/* SMB2_SESSION_SETUP */ 25,
	/* SMB2_LOGOFF */ 4,
	/* SMB2_TREE_CONNECT */	9,
	/* SMB2_TREE_DISCONNECT */ 4,
	/* SMB2_CREATE */ 57,
	/* SMB2_CLOSE */ 24,
	/* SMB2_FLUSH */ 24,
	/* SMB2_READ */	49,
	/* SMB2_WRITE */ 49,
	/* SMB2_LOCK */	48,
	/* SMB2_IOCTL */ 57,
	/* SMB2_CANCEL */ 4,
	/* SMB2_ECHO */ 4,
	/* SMB2_QUERY_DIRECTORY */ 33,
	/* SMB2_CHANGE_NOTIFY */ 32,
	/* SMB2_QUERY_INFO */ 41,
	/* SMB2_SET_INFO */ 33,
	/* SMB2_OPLOCK_BREAK */ 24 /* BB this is 36 for LEASE_BREAK variant */
};


static void
smb2_hdr_assemble(struct smb2_hdr *hdr, __le16 smb2_cmd /* command */ ,
		  const struct cifs_tcon *tcon)
{
	struct smb2_pdu *pdu = (struct smb2_pdu *)hdr;
	char *temp = (char *)hdr;
	/* lookup word count ie StructureSize from table */
	__u16 parmsize = smb2_req_struct_sizes[le16_to_cpu(smb2_cmd)];

	/*
	 * smaller than SMALL_BUFFER_SIZE but bigger than fixed area of
	 * largest operations (Create)
	 */
	memset(temp, 0, 256);

	/* Note this is only network field converted to big endian */
	hdr->smb2_buf_length = cpu_to_be32(parmsize + sizeof(struct smb2_hdr)
			- 4 /*  RFC 1001 length field itself not counted */);

	hdr->ProtocolId[0] = 0xFE;
	hdr->ProtocolId[1] = 'S';
	hdr->ProtocolId[2] = 'M';
	hdr->ProtocolId[3] = 'B';
	hdr->StructureSize = cpu_to_le16(64);
	hdr->Command = smb2_cmd;
	hdr->CreditRequest = cpu_to_le16(2); /* BB make this dynamic */
	hdr->ProcessId = cpu_to_le32((__u16)current->tgid);

	if (!tcon)
		goto out;

	hdr->TreeId = tcon->tid;
	/* Uid is not converted */
	if (tcon->ses)
		hdr->SessionId = tcon->ses->Suid;
	/* BB check following DFS flags BB */
	/* BB do we have to add check for SHI1005_FLAGS_DFS_ROOT too? */
	/* if (tcon->share_flags & SHI1005_FLAGS_DFS)
		hdr->Flags |= SMB2_FLAGS_DFS_OPERATIONS; */
	/* BB how does SMB2 do case sensitive? */
	/* if (tcon->nocase)
		hdr->Flags |= SMBFLG_CASELESS; */
	/* if (tcon->ses && tcon->ses->server &&
	    (tcon->ses->server->sec_mode & SECMODE_SIGN_REQUIRED))
		hdr->Flags |= SMB2_FLAGS_SIGNED; */
out:
	pdu->StructureSize2 = cpu_to_le16(parmsize);
	return;
}

static int
smb2_reconnect(__le16 smb2_command, struct cifs_tcon *tcon)
{
	int rc = 0;
	/* BB add missing code here */
	return rc;
}

/*
 * Allocate and return pointer to an SMB request hdr, and set basic
 * SMB information in the SMB header. If the return code is zero, this
 * function must have filled in request_buf pointer.
 */
static int
small_smb2_init(__le16 smb2_command, struct cifs_tcon *tcon,
		void **request_buf)
{
	int rc = 0;

	rc = smb2_reconnect(smb2_command, tcon);
	if (rc)
		return rc;

	/* BB eventually switch this to SMB2 specific small buf size */
	*request_buf = cifs_small_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}

	smb2_hdr_assemble((struct smb2_hdr *) *request_buf, smb2_command, tcon);

	if (tcon != NULL) {
#ifdef CONFIG_CIFS_STATS2
		/*
		uint16_t com_code = le16_to_cpu(smb2_command);
		cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_sent[com_code]);
		*/
#endif
		cifs_stats_inc(&tcon->num_smbs_sent);
	}

	return rc;
}

static void
free_rsp_buf(int resp_buftype, void *rsp)
{
	if (resp_buftype == CIFS_SMALL_BUFFER)
		cifs_small_buf_release(rsp);
	else if (resp_buftype == CIFS_LARGE_BUFFER)
		cifs_buf_release(rsp);
}

#define SMB2_NUM_PROT 1

#define SMB2_PROT   0
#define SMB21_PROT  1
#define BAD_PROT 0xFFFF

#define SMB2_PROT_ID  0x0202
#define SMB21_PROT_ID 0x0210
#define BAD_PROT_ID   0xFFFF

static struct {
	int index;
	__le16 name;
} smb2protocols[] = {
	{SMB2_PROT,  cpu_to_le16(SMB2_PROT_ID)},
	{SMB21_PROT, cpu_to_le16(SMB21_PROT_ID)},
	{BAD_PROT,   cpu_to_le16(BAD_PROT_ID)}
};

/*
 *
 *	SMB2 Worker functions follow:
 *
 *	The general structure of the worker functions is:
 *	1) Call smb2_init (assembles SMB2 header)
 *	2) Initialize SMB2 command specific fields in fixed length area of SMB
 *	3) Call smb_sendrcv2 (sends request on socket and waits for response)
 *	4) Decode SMB2 command specific fields in the fixed length area
 *	5) Decode variable length data area (if any for this SMB2 command type)
 *	6) Call free smb buffer
 *	7) return
 *
 */

int
SMB2_negotiate(const unsigned int xid, struct cifs_ses *ses)
{
	struct smb2_negotiate_req *req;
	struct smb2_negotiate_rsp *rsp;
	struct kvec iov[1];
	int rc = 0;
	int resp_buftype;
	struct TCP_Server_Info *server;
	unsigned int sec_flags;
	u16 i;
	u16 temp = 0;
	int blob_offset, blob_length;
	char *security_blob;
	int flags = CIFS_NEG_OP;

	cFYI(1, "Negotiate protocol");

	if (ses->server)
		server = ses->server;
	else {
		rc = -EIO;
		return rc;
	}

	rc = small_smb2_init(SMB2_NEGOTIATE, NULL, (void **) &req);
	if (rc)
		return rc;

	/* if any of auth flags (ie not sign or seal) are overriden use them */
	if (ses->overrideSecFlg & (~(CIFSSEC_MUST_SIGN | CIFSSEC_MUST_SEAL)))
		sec_flags = ses->overrideSecFlg;  /* BB FIXME fix sign flags?*/
	else /* if override flags set only sign/seal OR them with global auth */
		sec_flags = global_secflags | ses->overrideSecFlg;

	cFYI(1, "sec_flags 0x%x", sec_flags);

	req->hdr.SessionId = 0;

	for (i = 0; i < SMB2_NUM_PROT; i++)
		req->Dialects[i] = smb2protocols[i].name;

	req->DialectCount = cpu_to_le16(i);
	inc_rfc1001_len(req, i * 2);

	/* only one of SMB2 signing flags may be set in SMB2 request */
	if ((sec_flags & CIFSSEC_MUST_SIGN) == CIFSSEC_MUST_SIGN)
		temp = SMB2_NEGOTIATE_SIGNING_REQUIRED;
	else if (sec_flags & CIFSSEC_MAY_SIGN) /* MAY_SIGN is a single flag */
		temp = SMB2_NEGOTIATE_SIGNING_ENABLED;

	req->SecurityMode = cpu_to_le16(temp);

	req->Capabilities = cpu_to_le32(SMB2_GLOBAL_CAP_DFS);

	iov[0].iov_base = (char *)req;
	/* 4 for rfc1002 length field */
	iov[0].iov_len = get_rfc1002_length(req) + 4;

	rc = SendReceive2(xid, ses, iov, 1, &resp_buftype, flags);

	rsp = (struct smb2_negotiate_rsp *)iov[0].iov_base;
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */
	if (rc != 0)
		goto neg_exit;

	if (rsp == NULL) {
		rc = -EIO;
		goto neg_exit;
	}

	cFYI(1, "mode 0x%x", rsp->SecurityMode);

	if (rsp->DialectRevision == smb2protocols[SMB21_PROT].name)
		cFYI(1, "negotiated smb2.1 dialect");
	else if (rsp->DialectRevision == smb2protocols[SMB2_PROT].name)
		cFYI(1, "negotiated smb2 dialect");
	else {
		cERROR(1, "Illegal dialect returned by server %d",
			   le16_to_cpu(rsp->DialectRevision));
		rc = -EIO;
		goto neg_exit;
	}
	server->dialect = le16_to_cpu(rsp->DialectRevision);

	server->maxBuf = le32_to_cpu(rsp->MaxTransactSize);
	server->max_read = le32_to_cpu(rsp->MaxReadSize);
	server->max_write = le32_to_cpu(rsp->MaxWriteSize);
	/* BB Do we need to validate the SecurityMode? */
	server->sec_mode = le16_to_cpu(rsp->SecurityMode);
	server->capabilities = le32_to_cpu(rsp->Capabilities);

	security_blob = smb2_get_data_area_len(&blob_offset, &blob_length,
					       &rsp->hdr);
	if (blob_length == 0) {
		cERROR(1, "missing security blob on negprot");
		rc = -EIO;
		goto neg_exit;
	}
#ifdef CONFIG_SMB2_ASN1  /* BB REMOVEME when updated asn1.c ready */
	rc = decode_neg_token_init(security_blob, blob_length,
				   &server->sec_type);
	if (rc == 1)
		rc = 0;
	else if (rc == 0) {
		rc = -EIO;
		goto neg_exit;
	}
#endif

neg_exit:
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

int
SMB2_sess_setup(const unsigned int xid, struct cifs_ses *ses,
		const struct nls_table *nls_cp)
{
	struct smb2_sess_setup_req *req;
	struct smb2_sess_setup_rsp *rsp = NULL;
	struct kvec iov[2];
	int rc = 0;
	int resp_buftype;
	__le32 phase = NtLmNegotiate; /* NTLMSSP, if needed, is multistage */
	struct TCP_Server_Info *server;
	unsigned int sec_flags;
	u8 temp = 0;
	u16 blob_length = 0;
	char *security_blob;
	char *ntlmssp_blob = NULL;
	bool use_spnego = false; /* else use raw ntlmssp */

	cFYI(1, "Session Setup");

	if (ses->server)
		server = ses->server;
	else {
		rc = -EIO;
		return rc;
	}

	/*
	 * If memory allocation is successful, caller of this function
	 * frees it.
	 */
	ses->ntlmssp = kmalloc(sizeof(struct ntlmssp_auth), GFP_KERNEL);
	if (!ses->ntlmssp)
		return -ENOMEM;

	ses->server->secType = RawNTLMSSP;

ssetup_ntlmssp_authenticate:
	if (phase == NtLmChallenge)
		phase = NtLmAuthenticate; /* if ntlmssp, now final phase */

	rc = small_smb2_init(SMB2_SESSION_SETUP, NULL, (void **) &req);
	if (rc)
		return rc;

	/* if any of auth flags (ie not sign or seal) are overriden use them */
	if (ses->overrideSecFlg & (~(CIFSSEC_MUST_SIGN | CIFSSEC_MUST_SEAL)))
		sec_flags = ses->overrideSecFlg;  /* BB FIXME fix sign flags?*/
	else /* if override flags set only sign/seal OR them with global auth */
		sec_flags = global_secflags | ses->overrideSecFlg;

	cFYI(1, "sec_flags 0x%x", sec_flags);

	req->hdr.SessionId = 0; /* First session, not a reauthenticate */
	req->VcNumber = 0; /* MBZ */
	/* to enable echos and oplocks */
	req->hdr.CreditRequest = cpu_to_le16(3);

	/* only one of SMB2 signing flags may be set in SMB2 request */
	if ((sec_flags & CIFSSEC_MUST_SIGN) == CIFSSEC_MUST_SIGN)
		temp = SMB2_NEGOTIATE_SIGNING_REQUIRED;
	else if (ses->server->sec_mode & SMB2_NEGOTIATE_SIGNING_REQUIRED)
		temp = SMB2_NEGOTIATE_SIGNING_REQUIRED;
	else if (sec_flags & CIFSSEC_MAY_SIGN) /* MAY_SIGN is a single flag */
		temp = SMB2_NEGOTIATE_SIGNING_ENABLED;

	req->SecurityMode = temp;
	req->Capabilities = 0;
	req->Channel = 0; /* MBZ */

	iov[0].iov_base = (char *)req;
	/* 4 for rfc1002 length field and 1 for pad */
	iov[0].iov_len = get_rfc1002_length(req) + 4 - 1;
	if (phase == NtLmNegotiate) {
		ntlmssp_blob = kmalloc(sizeof(struct _NEGOTIATE_MESSAGE),
				       GFP_KERNEL);
		if (ntlmssp_blob == NULL) {
			rc = -ENOMEM;
			goto ssetup_exit;
		}
		build_ntlmssp_negotiate_blob(ntlmssp_blob, ses);
		if (use_spnego) {
			/* blob_length = build_spnego_ntlmssp_blob(
					&security_blob,
					sizeof(struct _NEGOTIATE_MESSAGE),
					ntlmssp_blob); */
			/* BB eventually need to add this */
			cERROR(1, "spnego not supported for SMB2 yet");
			rc = -EOPNOTSUPP;
			kfree(ntlmssp_blob);
			goto ssetup_exit;
		} else {
			blob_length = sizeof(struct _NEGOTIATE_MESSAGE);
			/* with raw NTLMSSP we don't encapsulate in SPNEGO */
			security_blob = ntlmssp_blob;
		}
	} else if (phase == NtLmAuthenticate) {
		req->hdr.SessionId = ses->Suid;
		ntlmssp_blob = kzalloc(sizeof(struct _NEGOTIATE_MESSAGE) + 500,
				       GFP_KERNEL);
		if (ntlmssp_blob == NULL) {
			cERROR(1, "failed to malloc ntlmssp blob");
			rc = -ENOMEM;
			goto ssetup_exit;
		}
		rc = build_ntlmssp_auth_blob(ntlmssp_blob, &blob_length, ses,
					     nls_cp);
		if (rc) {
			cFYI(1, "build_ntlmssp_auth_blob failed %d", rc);
			goto ssetup_exit; /* BB double check error handling */
		}
		if (use_spnego) {
			/* blob_length = build_spnego_ntlmssp_blob(
							&security_blob,
							blob_length,
							ntlmssp_blob); */
			cERROR(1, "spnego not supported for SMB2 yet");
			rc = -EOPNOTSUPP;
			kfree(ntlmssp_blob);
			goto ssetup_exit;
		} else {
			security_blob = ntlmssp_blob;
		}
	} else {
		cERROR(1, "illegal ntlmssp phase");
		rc = -EIO;
		goto ssetup_exit;
	}

	/* Testing shows that buffer offset must be at location of Buffer[0] */
	req->SecurityBufferOffset =
				cpu_to_le16(sizeof(struct smb2_sess_setup_req) -
					    1 /* pad */ - 4 /* rfc1001 len */);
	req->SecurityBufferLength = cpu_to_le16(blob_length);
	iov[1].iov_base = security_blob;
	iov[1].iov_len = blob_length;

	inc_rfc1001_len(req, blob_length - 1 /* pad */);

	/* BB add code to build os and lm fields */

	rc = SendReceive2(xid, ses, iov, 2, &resp_buftype, CIFS_LOG_ERROR);

	kfree(security_blob);
	rsp = (struct smb2_sess_setup_rsp *)iov[0].iov_base;
	if (rsp->hdr.Status == STATUS_MORE_PROCESSING_REQUIRED) {
		if (phase != NtLmNegotiate) {
			cERROR(1, "Unexpected more processing error");
			goto ssetup_exit;
		}
		if (offsetof(struct smb2_sess_setup_rsp, Buffer) - 4 !=
			le16_to_cpu(rsp->SecurityBufferOffset)) {
			cERROR(1, "Invalid security buffer offset %d",
				  le16_to_cpu(rsp->SecurityBufferOffset));
			rc = -EIO;
			goto ssetup_exit;
		}

		/* NTLMSSP Negotiate sent now processing challenge (response) */
		phase = NtLmChallenge; /* process ntlmssp challenge */
		rc = 0; /* MORE_PROCESSING is not an error here but expected */
		ses->Suid = rsp->hdr.SessionId;
		rc = decode_ntlmssp_challenge(rsp->Buffer,
				le16_to_cpu(rsp->SecurityBufferLength), ses);
	}

	/*
	 * BB eventually add code for SPNEGO decoding of NtlmChallenge blob,
	 * but at least the raw NTLMSSP case works.
	 */
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */
	if (rc != 0)
		goto ssetup_exit;

	if (rsp == NULL) {
		rc = -EIO;
		goto ssetup_exit;
	}

	ses->session_flags = le16_to_cpu(rsp->SessionFlags);
ssetup_exit:
	free_rsp_buf(resp_buftype, rsp);

	/* if ntlmssp, and negotiate succeeded, proceed to authenticate phase */
	if ((phase == NtLmChallenge) && (rc == 0))
		goto ssetup_ntlmssp_authenticate;
	return rc;
}

int
SMB2_logoff(const unsigned int xid, struct cifs_ses *ses)
{
	struct smb2_logoff_req *req; /* response is also trivial struct */
	int rc = 0;
	struct TCP_Server_Info *server;

	cFYI(1, "disconnect session %p", ses);

	if (ses && (ses->server))
		server = ses->server;
	else
		return -EIO;

	rc = small_smb2_init(SMB2_LOGOFF, NULL, (void **) &req);
	if (rc)
		return rc;

	 /* since no tcon, smb2_init can not do this, so do here */
	req->hdr.SessionId = ses->Suid;

	rc = SendReceiveNoRsp(xid, ses, (char *) &req->hdr, 0);
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */
	return rc;
}
