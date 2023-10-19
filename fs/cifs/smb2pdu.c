// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Copyright (C) International Business Machines  Corp., 2009, 2013
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
 *
 *   Contains the routines for constructing the SMB2 PDUs themselves
 *
 */

 /* SMB2 PDU handling routines here - except for leftovers (eg session setup) */
 /* Note that there are handle based routines which must be		      */
 /* treated slightly differently for reconnection purposes since we never     */
 /* want to reuse a stale file handle and only the caller knows the file info */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/vfs.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/uaccess.h>
#include <linux/uuid.h>
#include <linux/pagemap.h>
#include <linux/xattr.h>
#include "cifsglob.h"
#include "cifsacl.h"
#include "cifsproto.h"
#include "smb2proto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "ntlmssp.h"
#include "smb2status.h"
#include "smb2glob.h"
#include "cifspdu.h"
#include "cifs_spnego.h"
#include "smbdirect.h"
#include "trace.h"
#ifdef CONFIG_CIFS_DFS_UPCALL
#include "dfs_cache.h"
#endif
#include "cached_dir.h"

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

int smb3_encryption_required(const struct cifs_tcon *tcon)
{
	if (!tcon || !tcon->ses)
		return 0;
	if ((tcon->ses->session_flags & SMB2_SESSION_FLAG_ENCRYPT_DATA) ||
	    (tcon->share_flags & SHI1005_FLAGS_ENCRYPT_DATA))
		return 1;
	if (tcon->seal &&
	    (tcon->ses->server->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION))
		return 1;
	return 0;
}

static void
smb2_hdr_assemble(struct smb2_hdr *shdr, __le16 smb2_cmd,
		  const struct cifs_tcon *tcon,
		  struct TCP_Server_Info *server)
{
	shdr->ProtocolId = SMB2_PROTO_NUMBER;
	shdr->StructureSize = cpu_to_le16(64);
	shdr->Command = smb2_cmd;
	if (server) {
		spin_lock(&server->req_lock);
		/* Request up to 10 credits but don't go over the limit. */
		if (server->credits >= server->max_credits)
			shdr->CreditRequest = cpu_to_le16(0);
		else
			shdr->CreditRequest = cpu_to_le16(
				min_t(int, server->max_credits -
						server->credits, 10));
		spin_unlock(&server->req_lock);
	} else {
		shdr->CreditRequest = cpu_to_le16(2);
	}
	shdr->Id.SyncId.ProcessId = cpu_to_le32((__u16)current->tgid);

	if (!tcon)
		goto out;

	/* GLOBAL_CAP_LARGE_MTU will only be set if dialect > SMB2.02 */
	/* See sections 2.2.4 and 3.2.4.1.5 of MS-SMB2 */
	if (server && (server->capabilities & SMB2_GLOBAL_CAP_LARGE_MTU))
		shdr->CreditCharge = cpu_to_le16(1);
	/* else CreditCharge MBZ */

	shdr->Id.SyncId.TreeId = cpu_to_le32(tcon->tid);
	/* Uid is not converted */
	if (tcon->ses)
		shdr->SessionId = cpu_to_le64(tcon->ses->Suid);

	/*
	 * If we would set SMB2_FLAGS_DFS_OPERATIONS on open we also would have
	 * to pass the path on the Open SMB prefixed by \\server\share.
	 * Not sure when we would need to do the augmented path (if ever) and
	 * setting this flag breaks the SMB2 open operation since it is
	 * illegal to send an empty path name (without \\server\share prefix)
	 * when the DFS flag is set in the SMB open header. We could
	 * consider setting the flag on all operations other than open
	 * but it is safer to net set it for now.
	 */
/*	if (tcon->share_flags & SHI1005_FLAGS_DFS)
		shdr->Flags |= SMB2_FLAGS_DFS_OPERATIONS; */

	if (server && server->sign && !smb3_encryption_required(tcon))
		shdr->Flags |= SMB2_FLAGS_SIGNED;
out:
	return;
}

static int
smb2_reconnect(__le16 smb2_command, struct cifs_tcon *tcon,
	       struct TCP_Server_Info *server)
{
	int rc = 0;
	struct nls_table *nls_codepage = NULL;
	struct cifs_ses *ses;

	/*
	 * SMB2s NegProt, SessSetup, Logoff do not have tcon yet so
	 * check for tcp and smb session status done differently
	 * for those three - in the calling routine.
	 */
	if (tcon == NULL)
		return 0;

	/*
	 * Need to also skip SMB2_IOCTL because it is used for checking nested dfs links in
	 * cifs_tree_connect().
	 */
	if (smb2_command == SMB2_TREE_CONNECT || smb2_command == SMB2_IOCTL)
		return 0;

	spin_lock(&tcon->tc_lock);
	if (tcon->status == TID_EXITING) {
		/*
		 * only tree disconnect allowed when disconnecting ...
		 */
		if (smb2_command != SMB2_TREE_DISCONNECT) {
			spin_unlock(&tcon->tc_lock);
			cifs_dbg(FYI, "can not send cmd %d while umounting\n",
				 smb2_command);
			return -ENODEV;
		}
	}
	spin_unlock(&tcon->tc_lock);
	if ((!tcon->ses) || (tcon->ses->ses_status == SES_EXITING) ||
	    (!tcon->ses->server) || !server)
		return -EIO;

	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsNeedReconnect) {
		/*
		 * Return to caller for TREE_DISCONNECT and LOGOFF and CLOSE
		 * here since they are implicitly done when session drops.
		 */
		switch (smb2_command) {
		/*
		 * BB Should we keep oplock break and add flush to exceptions?
		 */
		case SMB2_TREE_DISCONNECT:
		case SMB2_CANCEL:
		case SMB2_CLOSE:
		case SMB2_OPLOCK_BREAK:
			spin_unlock(&server->srv_lock);
			return -EAGAIN;
		}
	}
	spin_unlock(&server->srv_lock);

again:
	rc = cifs_wait_for_server_reconnect(server, tcon->retry);
	if (rc)
		return rc;

	ses = tcon->ses;

	spin_lock(&ses->chan_lock);
	if (!cifs_chan_needs_reconnect(ses, server) && !tcon->need_reconnect) {
		spin_unlock(&ses->chan_lock);
		return 0;
	}
	spin_unlock(&ses->chan_lock);
	cifs_dbg(FYI, "sess reconnect mask: 0x%lx, tcon reconnect: %d",
		 tcon->ses->chans_need_reconnect,
		 tcon->need_reconnect);

	mutex_lock(&ses->session_mutex);
	/*
	 * Recheck after acquire mutex. If another thread is negotiating
	 * and the server never sends an answer the socket will be closed
	 * and tcpStatus set to reconnect.
	 */
	spin_lock(&server->srv_lock);
	if (server->tcpStatus == CifsNeedReconnect) {
		spin_unlock(&server->srv_lock);
		mutex_unlock(&ses->session_mutex);

		if (tcon->retry)
			goto again;

		rc = -EHOSTDOWN;
		goto out;
	}
	spin_unlock(&server->srv_lock);

	nls_codepage = load_nls_default();

	/*
	 * need to prevent multiple threads trying to simultaneously
	 * reconnect the same SMB session
	 */
	spin_lock(&ses->ses_lock);
	spin_lock(&ses->chan_lock);
	if (!cifs_chan_needs_reconnect(ses, server) &&
	    ses->ses_status == SES_GOOD) {
		spin_unlock(&ses->chan_lock);
		spin_unlock(&ses->ses_lock);
		/* this means that we only need to tree connect */
		if (tcon->need_reconnect)
			goto skip_sess_setup;

		mutex_unlock(&ses->session_mutex);
		goto out;
	}
	spin_unlock(&ses->chan_lock);
	spin_unlock(&ses->ses_lock);

	rc = cifs_negotiate_protocol(0, ses, server);
	if (!rc) {
		rc = cifs_setup_session(0, ses, server, nls_codepage);
		if ((rc == -EACCES) && !tcon->retry) {
			mutex_unlock(&ses->session_mutex);
			rc = -EHOSTDOWN;
			goto failed;
		} else if (rc) {
			mutex_unlock(&ses->session_mutex);
			goto out;
		}
	} else {
		mutex_unlock(&ses->session_mutex);
		goto out;
	}

skip_sess_setup:
	if (!tcon->need_reconnect) {
		mutex_unlock(&ses->session_mutex);
		goto out;
	}
	cifs_mark_open_files_invalid(tcon);
	if (tcon->use_persistent)
		tcon->need_reopen_files = true;

	rc = cifs_tree_connect(0, tcon, nls_codepage);
	mutex_unlock(&ses->session_mutex);

	cifs_dbg(FYI, "reconnect tcon rc = %d\n", rc);
	if (rc) {
		/* If sess reconnected but tcon didn't, something strange ... */
		cifs_dbg(VFS, "reconnect tcon failed rc = %d\n", rc);
		goto out;
	}

	if (smb2_command != SMB2_INTERNAL_CMD)
		mod_delayed_work(cifsiod_wq, &server->reconnect, 0);

	atomic_inc(&tconInfoReconnectCount);
out:
	/*
	 * Check if handle based operation so we know whether we can continue
	 * or not without returning to caller to reset file handle.
	 */
	/*
	 * BB Is flush done by server on drop of tcp session? Should we special
	 * case it and skip above?
	 */
	switch (smb2_command) {
	case SMB2_FLUSH:
	case SMB2_READ:
	case SMB2_WRITE:
	case SMB2_LOCK:
	case SMB2_IOCTL:
	case SMB2_QUERY_DIRECTORY:
	case SMB2_CHANGE_NOTIFY:
	case SMB2_QUERY_INFO:
	case SMB2_SET_INFO:
		rc = -EAGAIN;
	}
failed:
	unload_nls(nls_codepage);
	return rc;
}

static void
fill_small_buf(__le16 smb2_command, struct cifs_tcon *tcon,
	       struct TCP_Server_Info *server,
	       void *buf,
	       unsigned int *total_len)
{
	struct smb2_pdu *spdu = buf;
	/* lookup word count ie StructureSize from table */
	__u16 parmsize = smb2_req_struct_sizes[le16_to_cpu(smb2_command)];

	/*
	 * smaller than SMALL_BUFFER_SIZE but bigger than fixed area of
	 * largest operations (Create)
	 */
	memset(buf, 0, 256);

	smb2_hdr_assemble(&spdu->hdr, smb2_command, tcon, server);
	spdu->StructureSize2 = cpu_to_le16(parmsize);

	*total_len = parmsize + sizeof(struct smb2_hdr);
}

/*
 * Allocate and return pointer to an SMB request hdr, and set basic
 * SMB information in the SMB header. If the return code is zero, this
 * function must have filled in request_buf pointer.
 */
static int __smb2_plain_req_init(__le16 smb2_command, struct cifs_tcon *tcon,
				 struct TCP_Server_Info *server,
				 void **request_buf, unsigned int *total_len)
{
	/* BB eventually switch this to SMB2 specific small buf size */
	if (smb2_command == SMB2_SET_INFO)
		*request_buf = cifs_buf_get();
	else
		*request_buf = cifs_small_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}

	fill_small_buf(smb2_command, tcon, server,
		       (struct smb2_hdr *)(*request_buf),
		       total_len);

	if (tcon != NULL) {
		uint16_t com_code = le16_to_cpu(smb2_command);
		cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_sent[com_code]);
		cifs_stats_inc(&tcon->num_smbs_sent);
	}

	return 0;
}

static int smb2_plain_req_init(__le16 smb2_command, struct cifs_tcon *tcon,
			       struct TCP_Server_Info *server,
			       void **request_buf, unsigned int *total_len)
{
	int rc;

	rc = smb2_reconnect(smb2_command, tcon, server);
	if (rc)
		return rc;

	return __smb2_plain_req_init(smb2_command, tcon, server, request_buf,
				     total_len);
}

static int smb2_ioctl_req_init(u32 opcode, struct cifs_tcon *tcon,
			       struct TCP_Server_Info *server,
			       void **request_buf, unsigned int *total_len)
{
	/* Skip reconnect only for FSCTL_VALIDATE_NEGOTIATE_INFO IOCTLs */
	if (opcode == FSCTL_VALIDATE_NEGOTIATE_INFO) {
		return __smb2_plain_req_init(SMB2_IOCTL, tcon, server,
					     request_buf, total_len);
	}
	return smb2_plain_req_init(SMB2_IOCTL, tcon, server,
				   request_buf, total_len);
}

/* For explanation of negotiate contexts see MS-SMB2 section 2.2.3.1 */

static void
build_preauth_ctxt(struct smb2_preauth_neg_context *pneg_ctxt)
{
	pneg_ctxt->ContextType = SMB2_PREAUTH_INTEGRITY_CAPABILITIES;
	pneg_ctxt->DataLength = cpu_to_le16(38);
	pneg_ctxt->HashAlgorithmCount = cpu_to_le16(1);
	pneg_ctxt->SaltLength = cpu_to_le16(SMB311_SALT_SIZE);
	get_random_bytes(pneg_ctxt->Salt, SMB311_SALT_SIZE);
	pneg_ctxt->HashAlgorithms = SMB2_PREAUTH_INTEGRITY_SHA512;
}

static void
build_compression_ctxt(struct smb2_compression_capabilities_context *pneg_ctxt)
{
	pneg_ctxt->ContextType = SMB2_COMPRESSION_CAPABILITIES;
	pneg_ctxt->DataLength =
		cpu_to_le16(sizeof(struct smb2_compression_capabilities_context)
			  - sizeof(struct smb2_neg_context));
	pneg_ctxt->CompressionAlgorithmCount = cpu_to_le16(3);
	pneg_ctxt->CompressionAlgorithms[0] = SMB3_COMPRESS_LZ77;
	pneg_ctxt->CompressionAlgorithms[1] = SMB3_COMPRESS_LZ77_HUFF;
	pneg_ctxt->CompressionAlgorithms[2] = SMB3_COMPRESS_LZNT1;
}

static unsigned int
build_signing_ctxt(struct smb2_signing_capabilities *pneg_ctxt)
{
	unsigned int ctxt_len = sizeof(struct smb2_signing_capabilities);
	unsigned short num_algs = 1; /* number of signing algorithms sent */

	pneg_ctxt->ContextType = SMB2_SIGNING_CAPABILITIES;
	/*
	 * Context Data length must be rounded to multiple of 8 for some servers
	 */
	pneg_ctxt->DataLength = cpu_to_le16(ALIGN(sizeof(struct smb2_signing_capabilities) -
					    sizeof(struct smb2_neg_context) +
					    (num_algs * sizeof(u16)), 8));
	pneg_ctxt->SigningAlgorithmCount = cpu_to_le16(num_algs);
	pneg_ctxt->SigningAlgorithms[0] = cpu_to_le16(SIGNING_ALG_AES_CMAC);

	ctxt_len += sizeof(__le16) * num_algs;
	ctxt_len = ALIGN(ctxt_len, 8);
	return ctxt_len;
	/* TBD add SIGNING_ALG_AES_GMAC and/or SIGNING_ALG_HMAC_SHA256 */
}

static void
build_encrypt_ctxt(struct smb2_encryption_neg_context *pneg_ctxt)
{
	pneg_ctxt->ContextType = SMB2_ENCRYPTION_CAPABILITIES;
	if (require_gcm_256) {
		pneg_ctxt->DataLength = cpu_to_le16(4); /* Cipher Count + 1 cipher */
		pneg_ctxt->CipherCount = cpu_to_le16(1);
		pneg_ctxt->Ciphers[0] = SMB2_ENCRYPTION_AES256_GCM;
	} else if (enable_gcm_256) {
		pneg_ctxt->DataLength = cpu_to_le16(8); /* Cipher Count + 3 ciphers */
		pneg_ctxt->CipherCount = cpu_to_le16(3);
		pneg_ctxt->Ciphers[0] = SMB2_ENCRYPTION_AES128_GCM;
		pneg_ctxt->Ciphers[1] = SMB2_ENCRYPTION_AES256_GCM;
		pneg_ctxt->Ciphers[2] = SMB2_ENCRYPTION_AES128_CCM;
	} else {
		pneg_ctxt->DataLength = cpu_to_le16(6); /* Cipher Count + 2 ciphers */
		pneg_ctxt->CipherCount = cpu_to_le16(2);
		pneg_ctxt->Ciphers[0] = SMB2_ENCRYPTION_AES128_GCM;
		pneg_ctxt->Ciphers[1] = SMB2_ENCRYPTION_AES128_CCM;
	}
}

static unsigned int
build_netname_ctxt(struct smb2_netname_neg_context *pneg_ctxt, char *hostname)
{
	struct nls_table *cp = load_nls_default();

	pneg_ctxt->ContextType = SMB2_NETNAME_NEGOTIATE_CONTEXT_ID;

	/* copy up to max of first 100 bytes of server name to NetName field */
	pneg_ctxt->DataLength = cpu_to_le16(2 * cifs_strtoUTF16(pneg_ctxt->NetName, hostname, 100, cp));
	/* context size is DataLength + minimal smb2_neg_context */
	return ALIGN(le16_to_cpu(pneg_ctxt->DataLength) + sizeof(struct smb2_neg_context), 8);
}

static void
build_posix_ctxt(struct smb2_posix_neg_context *pneg_ctxt)
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

static void
assemble_neg_contexts(struct smb2_negotiate_req *req,
		      struct TCP_Server_Info *server, unsigned int *total_len)
{
	unsigned int ctxt_len, neg_context_count;
	struct TCP_Server_Info *pserver;
	char *pneg_ctxt;
	char *hostname;

	if (*total_len > 200) {
		/* In case length corrupted don't want to overrun smb buffer */
		cifs_server_dbg(VFS, "Bad frame length assembling neg contexts\n");
		return;
	}

	/*
	 * round up total_len of fixed part of SMB3 negotiate request to 8
	 * byte boundary before adding negotiate contexts
	 */
	*total_len = ALIGN(*total_len, 8);

	pneg_ctxt = (*total_len) + (char *)req;
	req->NegotiateContextOffset = cpu_to_le32(*total_len);

	build_preauth_ctxt((struct smb2_preauth_neg_context *)pneg_ctxt);
	ctxt_len = ALIGN(sizeof(struct smb2_preauth_neg_context), 8);
	*total_len += ctxt_len;
	pneg_ctxt += ctxt_len;

	build_encrypt_ctxt((struct smb2_encryption_neg_context *)pneg_ctxt);
	ctxt_len = ALIGN(sizeof(struct smb2_encryption_neg_context), 8);
	*total_len += ctxt_len;
	pneg_ctxt += ctxt_len;

	/*
	 * secondary channels don't have the hostname field populated
	 * use the hostname field in the primary channel instead
	 */
	pserver = CIFS_SERVER_IS_CHAN(server) ? server->primary_server : server;
	cifs_server_lock(pserver);
	hostname = pserver->hostname;
	if (hostname && (hostname[0] != 0)) {
		ctxt_len = build_netname_ctxt((struct smb2_netname_neg_context *)pneg_ctxt,
					      hostname);
		*total_len += ctxt_len;
		pneg_ctxt += ctxt_len;
		neg_context_count = 3;
	} else
		neg_context_count = 2;
	cifs_server_unlock(pserver);

	build_posix_ctxt((struct smb2_posix_neg_context *)pneg_ctxt);
	*total_len += sizeof(struct smb2_posix_neg_context);
	pneg_ctxt += sizeof(struct smb2_posix_neg_context);
	neg_context_count++;

	if (server->compress_algorithm) {
		build_compression_ctxt((struct smb2_compression_capabilities_context *)
				pneg_ctxt);
		ctxt_len = ALIGN(sizeof(struct smb2_compression_capabilities_context), 8);
		*total_len += ctxt_len;
		pneg_ctxt += ctxt_len;
		neg_context_count++;
	}

	if (enable_negotiate_signing) {
		ctxt_len = build_signing_ctxt((struct smb2_signing_capabilities *)
				pneg_ctxt);
		*total_len += ctxt_len;
		pneg_ctxt += ctxt_len;
		neg_context_count++;
	}

	/* check for and add transport_capabilities and signing capabilities */
	req->NegotiateContextCount = cpu_to_le16(neg_context_count);

}

/* If invalid preauth context warn but use what we requested, SHA-512 */
static void decode_preauth_context(struct smb2_preauth_neg_context *ctxt)
{
	unsigned int len = le16_to_cpu(ctxt->DataLength);

	/*
	 * Caller checked that DataLength remains within SMB boundary. We still
	 * need to confirm that one HashAlgorithms member is accounted for.
	 */
	if (len < MIN_PREAUTH_CTXT_DATA_LEN) {
		pr_warn_once("server sent bad preauth context\n");
		return;
	} else if (len < MIN_PREAUTH_CTXT_DATA_LEN + le16_to_cpu(ctxt->SaltLength)) {
		pr_warn_once("server sent invalid SaltLength\n");
		return;
	}
	if (le16_to_cpu(ctxt->HashAlgorithmCount) != 1)
		pr_warn_once("Invalid SMB3 hash algorithm count\n");
	if (ctxt->HashAlgorithms != SMB2_PREAUTH_INTEGRITY_SHA512)
		pr_warn_once("unknown SMB3 hash algorithm\n");
}

static void decode_compress_ctx(struct TCP_Server_Info *server,
			 struct smb2_compression_capabilities_context *ctxt)
{
	unsigned int len = le16_to_cpu(ctxt->DataLength);

	/*
	 * Caller checked that DataLength remains within SMB boundary. We still
	 * need to confirm that one CompressionAlgorithms member is accounted
	 * for.
	 */
	if (len < 10) {
		pr_warn_once("server sent bad compression cntxt\n");
		return;
	}
	if (le16_to_cpu(ctxt->CompressionAlgorithmCount) != 1) {
		pr_warn_once("Invalid SMB3 compress algorithm count\n");
		return;
	}
	if (le16_to_cpu(ctxt->CompressionAlgorithms[0]) > 3) {
		pr_warn_once("unknown compression algorithm\n");
		return;
	}
	server->compress_algorithm = ctxt->CompressionAlgorithms[0];
}

static int decode_encrypt_ctx(struct TCP_Server_Info *server,
			      struct smb2_encryption_neg_context *ctxt)
{
	unsigned int len = le16_to_cpu(ctxt->DataLength);

	cifs_dbg(FYI, "decode SMB3.11 encryption neg context of len %d\n", len);
	/*
	 * Caller checked that DataLength remains within SMB boundary. We still
	 * need to confirm that one Cipher flexible array member is accounted
	 * for.
	 */
	if (len < MIN_ENCRYPT_CTXT_DATA_LEN) {
		pr_warn_once("server sent bad crypto ctxt len\n");
		return -EINVAL;
	}

	if (le16_to_cpu(ctxt->CipherCount) != 1) {
		pr_warn_once("Invalid SMB3.11 cipher count\n");
		return -EINVAL;
	}
	cifs_dbg(FYI, "SMB311 cipher type:%d\n", le16_to_cpu(ctxt->Ciphers[0]));
	if (require_gcm_256) {
		if (ctxt->Ciphers[0] != SMB2_ENCRYPTION_AES256_GCM) {
			cifs_dbg(VFS, "Server does not support requested encryption type (AES256 GCM)\n");
			return -EOPNOTSUPP;
		}
	} else if (ctxt->Ciphers[0] == 0) {
		/*
		 * e.g. if server only supported AES256_CCM (very unlikely)
		 * or server supported no encryption types or had all disabled.
		 * Since GLOBAL_CAP_ENCRYPTION will be not set, in the case
		 * in which mount requested encryption ("seal") checks later
		 * on during tree connection will return proper rc, but if
		 * seal not requested by client, since server is allowed to
		 * return 0 to indicate no supported cipher, we can't fail here
		 */
		server->cipher_type = 0;
		server->capabilities &= ~SMB2_GLOBAL_CAP_ENCRYPTION;
		pr_warn_once("Server does not support requested encryption types\n");
		return 0;
	} else if ((ctxt->Ciphers[0] != SMB2_ENCRYPTION_AES128_CCM) &&
		   (ctxt->Ciphers[0] != SMB2_ENCRYPTION_AES128_GCM) &&
		   (ctxt->Ciphers[0] != SMB2_ENCRYPTION_AES256_GCM)) {
		/* server returned a cipher we didn't ask for */
		pr_warn_once("Invalid SMB3.11 cipher returned\n");
		return -EINVAL;
	}
	server->cipher_type = ctxt->Ciphers[0];
	server->capabilities |= SMB2_GLOBAL_CAP_ENCRYPTION;
	return 0;
}

static void decode_signing_ctx(struct TCP_Server_Info *server,
			       struct smb2_signing_capabilities *pctxt)
{
	unsigned int len = le16_to_cpu(pctxt->DataLength);

	/*
	 * Caller checked that DataLength remains within SMB boundary. We still
	 * need to confirm that one SigningAlgorithms flexible array member is
	 * accounted for.
	 */
	if ((len < 4) || (len > 16)) {
		pr_warn_once("server sent bad signing negcontext\n");
		return;
	}
	if (le16_to_cpu(pctxt->SigningAlgorithmCount) != 1) {
		pr_warn_once("Invalid signing algorithm count\n");
		return;
	}
	if (le16_to_cpu(pctxt->SigningAlgorithms[0]) > 2) {
		pr_warn_once("unknown signing algorithm\n");
		return;
	}

	server->signing_negotiated = true;
	server->signing_algorithm = le16_to_cpu(pctxt->SigningAlgorithms[0]);
	cifs_dbg(FYI, "signing algorithm %d chosen\n",
		     server->signing_algorithm);
}


static int smb311_decode_neg_context(struct smb2_negotiate_rsp *rsp,
				     struct TCP_Server_Info *server,
				     unsigned int len_of_smb)
{
	struct smb2_neg_context *pctx;
	unsigned int offset = le32_to_cpu(rsp->NegotiateContextOffset);
	unsigned int ctxt_cnt = le16_to_cpu(rsp->NegotiateContextCount);
	unsigned int len_of_ctxts, i;
	int rc = 0;

	cifs_dbg(FYI, "decoding %d negotiate contexts\n", ctxt_cnt);
	if (len_of_smb <= offset) {
		cifs_server_dbg(VFS, "Invalid response: negotiate context offset\n");
		return -EINVAL;
	}

	len_of_ctxts = len_of_smb - offset;

	for (i = 0; i < ctxt_cnt; i++) {
		int clen;
		/* check that offset is not beyond end of SMB */
		if (len_of_ctxts < sizeof(struct smb2_neg_context))
			break;

		pctx = (struct smb2_neg_context *)(offset + (char *)rsp);
		clen = sizeof(struct smb2_neg_context)
			+ le16_to_cpu(pctx->DataLength);
		/*
		 * 2.2.4 SMB2 NEGOTIATE Response
		 * Subsequent negotiate contexts MUST appear at the first 8-byte
		 * aligned offset following the previous negotiate context.
		 */
		if (i + 1 != ctxt_cnt)
			clen = ALIGN(clen, 8);
		if (clen > len_of_ctxts)
			break;

		if (pctx->ContextType == SMB2_PREAUTH_INTEGRITY_CAPABILITIES)
			decode_preauth_context(
				(struct smb2_preauth_neg_context *)pctx);
		else if (pctx->ContextType == SMB2_ENCRYPTION_CAPABILITIES)
			rc = decode_encrypt_ctx(server,
				(struct smb2_encryption_neg_context *)pctx);
		else if (pctx->ContextType == SMB2_COMPRESSION_CAPABILITIES)
			decode_compress_ctx(server,
				(struct smb2_compression_capabilities_context *)pctx);
		else if (pctx->ContextType == SMB2_POSIX_EXTENSIONS_AVAILABLE)
			server->posix_ext_supported = true;
		else if (pctx->ContextType == SMB2_SIGNING_CAPABILITIES)
			decode_signing_ctx(server,
				(struct smb2_signing_capabilities *)pctx);
		else
			cifs_server_dbg(VFS, "unknown negcontext of type %d ignored\n",
				le16_to_cpu(pctx->ContextType));
		if (rc)
			break;

		offset += clen;
		len_of_ctxts -= clen;
	}
	return rc;
}

static struct create_posix *
create_posix_buf(umode_t mode)
{
	struct create_posix *buf;

	buf = kzalloc(sizeof(struct create_posix),
			GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset =
		cpu_to_le16(offsetof(struct create_posix, Mode));
	buf->ccontext.DataLength = cpu_to_le32(4);
	buf->ccontext.NameOffset =
		cpu_to_le16(offsetof(struct create_posix, Name));
	buf->ccontext.NameLength = cpu_to_le16(16);

	/* SMB2_CREATE_TAG_POSIX is "0x93AD25509CB411E7B42383DE968BCD7C" */
	buf->Name[0] = 0x93;
	buf->Name[1] = 0xAD;
	buf->Name[2] = 0x25;
	buf->Name[3] = 0x50;
	buf->Name[4] = 0x9C;
	buf->Name[5] = 0xB4;
	buf->Name[6] = 0x11;
	buf->Name[7] = 0xE7;
	buf->Name[8] = 0xB4;
	buf->Name[9] = 0x23;
	buf->Name[10] = 0x83;
	buf->Name[11] = 0xDE;
	buf->Name[12] = 0x96;
	buf->Name[13] = 0x8B;
	buf->Name[14] = 0xCD;
	buf->Name[15] = 0x7C;
	buf->Mode = cpu_to_le32(mode);
	cifs_dbg(FYI, "mode on posix create 0%o\n", mode);
	return buf;
}

static int
add_posix_context(struct kvec *iov, unsigned int *num_iovec, umode_t mode)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	iov[num].iov_base = create_posix_buf(mode);
	if (mode == ACL_NO_MODE)
		cifs_dbg(FYI, "Invalid mode\n");
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = sizeof(struct create_posix);
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset = cpu_to_le32(
				sizeof(struct smb2_create_req) +
				iov[num - 1].iov_len);
	le32_add_cpu(&req->CreateContextsLength, sizeof(struct create_posix));
	*num_iovec = num + 1;
	return 0;
}


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
SMB2_negotiate(const unsigned int xid,
	       struct cifs_ses *ses,
	       struct TCP_Server_Info *server)
{
	struct smb_rqst rqst;
	struct smb2_negotiate_req *req;
	struct smb2_negotiate_rsp *rsp;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int rc;
	int resp_buftype;
	int blob_offset, blob_length;
	char *security_blob;
	int flags = CIFS_NEG_OP;
	unsigned int total_len;

	cifs_dbg(FYI, "Negotiate protocol\n");

	if (!server) {
		WARN(1, "%s: server is NULL!\n", __func__);
		return -EIO;
	}

	rc = smb2_plain_req_init(SMB2_NEGOTIATE, NULL, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->hdr.SessionId = 0;

	memset(server->preauth_sha_hash, 0, SMB2_PREAUTH_HASH_SIZE);
	memset(ses->preauth_sha_hash, 0, SMB2_PREAUTH_HASH_SIZE);

	if (strcmp(server->vals->version_string,
		   SMB3ANY_VERSION_STRING) == 0) {
		req->Dialects[0] = cpu_to_le16(SMB30_PROT_ID);
		req->Dialects[1] = cpu_to_le16(SMB302_PROT_ID);
		req->Dialects[2] = cpu_to_le16(SMB311_PROT_ID);
		req->DialectCount = cpu_to_le16(3);
		total_len += 6;
	} else if (strcmp(server->vals->version_string,
		   SMBDEFAULT_VERSION_STRING) == 0) {
		req->Dialects[0] = cpu_to_le16(SMB21_PROT_ID);
		req->Dialects[1] = cpu_to_le16(SMB30_PROT_ID);
		req->Dialects[2] = cpu_to_le16(SMB302_PROT_ID);
		req->Dialects[3] = cpu_to_le16(SMB311_PROT_ID);
		req->DialectCount = cpu_to_le16(4);
		total_len += 8;
	} else {
		/* otherwise send specific dialect */
		req->Dialects[0] = cpu_to_le16(server->vals->protocol_id);
		req->DialectCount = cpu_to_le16(1);
		total_len += 2;
	}

	/* only one of SMB2 signing flags may be set in SMB2 request */
	if (ses->sign)
		req->SecurityMode = cpu_to_le16(SMB2_NEGOTIATE_SIGNING_REQUIRED);
	else if (global_secflags & CIFSSEC_MAY_SIGN)
		req->SecurityMode = cpu_to_le16(SMB2_NEGOTIATE_SIGNING_ENABLED);
	else
		req->SecurityMode = 0;

	req->Capabilities = cpu_to_le32(server->vals->req_capabilities);
	if (ses->chan_max > 1)
		req->Capabilities |= cpu_to_le32(SMB2_GLOBAL_CAP_MULTI_CHANNEL);

	/* ClientGUID must be zero for SMB2.02 dialect */
	if (server->vals->protocol_id == SMB20_PROT_ID)
		memset(req->ClientGUID, 0, SMB2_CLIENT_GUID_SIZE);
	else {
		memcpy(req->ClientGUID, server->client_guid,
			SMB2_CLIENT_GUID_SIZE);
		if ((server->vals->protocol_id == SMB311_PROT_ID) ||
		    (strcmp(server->vals->version_string,
		     SMB3ANY_VERSION_STRING) == 0) ||
		    (strcmp(server->vals->version_string,
		     SMBDEFAULT_VERSION_STRING) == 0))
			assemble_neg_contexts(req, server, &total_len);
	}
	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	cifs_small_buf_release(req);
	rsp = (struct smb2_negotiate_rsp *)rsp_iov.iov_base;
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */
	if (rc == -EOPNOTSUPP) {
		cifs_server_dbg(VFS, "Dialect not supported by server. Consider  specifying vers=1.0 or vers=2.0 on mount for accessing older servers\n");
		goto neg_exit;
	} else if (rc != 0)
		goto neg_exit;

	rc = -EIO;
	if (strcmp(server->vals->version_string,
		   SMB3ANY_VERSION_STRING) == 0) {
		if (rsp->DialectRevision == cpu_to_le16(SMB20_PROT_ID)) {
			cifs_server_dbg(VFS,
				"SMB2 dialect returned but not requested\n");
			goto neg_exit;
		} else if (rsp->DialectRevision == cpu_to_le16(SMB21_PROT_ID)) {
			cifs_server_dbg(VFS,
				"SMB2.1 dialect returned but not requested\n");
			goto neg_exit;
		} else if (rsp->DialectRevision == cpu_to_le16(SMB311_PROT_ID)) {
			/* ops set to 3.0 by default for default so update */
			server->ops = &smb311_operations;
			server->vals = &smb311_values;
		}
	} else if (strcmp(server->vals->version_string,
		   SMBDEFAULT_VERSION_STRING) == 0) {
		if (rsp->DialectRevision == cpu_to_le16(SMB20_PROT_ID)) {
			cifs_server_dbg(VFS,
				"SMB2 dialect returned but not requested\n");
			goto neg_exit;
		} else if (rsp->DialectRevision == cpu_to_le16(SMB21_PROT_ID)) {
			/* ops set to 3.0 by default for default so update */
			server->ops = &smb21_operations;
			server->vals = &smb21_values;
		} else if (rsp->DialectRevision == cpu_to_le16(SMB311_PROT_ID)) {
			server->ops = &smb311_operations;
			server->vals = &smb311_values;
		}
	} else if (le16_to_cpu(rsp->DialectRevision) !=
				server->vals->protocol_id) {
		/* if requested single dialect ensure returned dialect matched */
		cifs_server_dbg(VFS, "Invalid 0x%x dialect returned: not requested\n",
				le16_to_cpu(rsp->DialectRevision));
		goto neg_exit;
	}

	cifs_dbg(FYI, "mode 0x%x\n", rsp->SecurityMode);

	if (rsp->DialectRevision == cpu_to_le16(SMB20_PROT_ID))
		cifs_dbg(FYI, "negotiated smb2.0 dialect\n");
	else if (rsp->DialectRevision == cpu_to_le16(SMB21_PROT_ID))
		cifs_dbg(FYI, "negotiated smb2.1 dialect\n");
	else if (rsp->DialectRevision == cpu_to_le16(SMB30_PROT_ID))
		cifs_dbg(FYI, "negotiated smb3.0 dialect\n");
	else if (rsp->DialectRevision == cpu_to_le16(SMB302_PROT_ID))
		cifs_dbg(FYI, "negotiated smb3.02 dialect\n");
	else if (rsp->DialectRevision == cpu_to_le16(SMB311_PROT_ID))
		cifs_dbg(FYI, "negotiated smb3.1.1 dialect\n");
	else {
		cifs_server_dbg(VFS, "Invalid dialect returned by server 0x%x\n",
				le16_to_cpu(rsp->DialectRevision));
		goto neg_exit;
	}

	rc = 0;
	server->dialect = le16_to_cpu(rsp->DialectRevision);

	/*
	 * Keep a copy of the hash after negprot. This hash will be
	 * the starting hash value for all sessions made from this
	 * server.
	 */
	memcpy(server->preauth_sha_hash, ses->preauth_sha_hash,
	       SMB2_PREAUTH_HASH_SIZE);

	/* SMB2 only has an extended negflavor */
	server->negflavor = CIFS_NEGFLAVOR_EXTENDED;
	/* set it to the maximum buffer size value we can send with 1 credit */
	server->maxBuf = min_t(unsigned int, le32_to_cpu(rsp->MaxTransactSize),
			       SMB2_MAX_BUFFER_SIZE);
	server->max_read = le32_to_cpu(rsp->MaxReadSize);
	server->max_write = le32_to_cpu(rsp->MaxWriteSize);
	server->sec_mode = le16_to_cpu(rsp->SecurityMode);
	if ((server->sec_mode & SMB2_SEC_MODE_FLAGS_ALL) != server->sec_mode)
		cifs_dbg(FYI, "Server returned unexpected security mode 0x%x\n",
				server->sec_mode);
	server->capabilities = le32_to_cpu(rsp->Capabilities);
	/* Internal types */
	server->capabilities |= SMB2_NT_FIND | SMB2_LARGE_FILES;

	/*
	 * SMB3.0 supports only 1 cipher and doesn't have a encryption neg context
	 * Set the cipher type manually.
	 */
	if (server->dialect == SMB30_PROT_ID && (server->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION))
		server->cipher_type = SMB2_ENCRYPTION_AES128_CCM;

	security_blob = smb2_get_data_area_len(&blob_offset, &blob_length,
					       (struct smb2_hdr *)rsp);
	/*
	 * See MS-SMB2 section 2.2.4: if no blob, client picks default which
	 * for us will be
	 *	ses->sectype = RawNTLMSSP;
	 * but for time being this is our only auth choice so doesn't matter.
	 * We just found a server which sets blob length to zero expecting raw.
	 */
	if (blob_length == 0) {
		cifs_dbg(FYI, "missing security blob on negprot\n");
		server->sec_ntlmssp = true;
	}

	rc = cifs_enable_signing(server, ses->sign);
	if (rc)
		goto neg_exit;
	if (blob_length) {
		rc = decode_negTokenInit(security_blob, blob_length, server);
		if (rc == 1)
			rc = 0;
		else if (rc == 0)
			rc = -EIO;
	}

	if (rsp->DialectRevision == cpu_to_le16(SMB311_PROT_ID)) {
		if (rsp->NegotiateContextCount)
			rc = smb311_decode_neg_context(rsp, server,
						       rsp_iov.iov_len);
		else
			cifs_server_dbg(VFS, "Missing expected negotiate contexts\n");
	}
neg_exit:
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

int smb3_validate_negotiate(const unsigned int xid, struct cifs_tcon *tcon)
{
	int rc;
	struct validate_negotiate_info_req *pneg_inbuf;
	struct validate_negotiate_info_rsp *pneg_rsp = NULL;
	u32 rsplen;
	u32 inbuflen; /* max of 4 dialects */
	struct TCP_Server_Info *server = tcon->ses->server;

	cifs_dbg(FYI, "validate negotiate\n");

	/* In SMB3.11 preauth integrity supersedes validate negotiate */
	if (server->dialect == SMB311_PROT_ID)
		return 0;

	/*
	 * validation ioctl must be signed, so no point sending this if we
	 * can not sign it (ie are not known user).  Even if signing is not
	 * required (enabled but not negotiated), in those cases we selectively
	 * sign just this, the first and only signed request on a connection.
	 * Having validation of negotiate info  helps reduce attack vectors.
	 */
	if (tcon->ses->session_flags & SMB2_SESSION_FLAG_IS_GUEST)
		return 0; /* validation requires signing */

	if (tcon->ses->user_name == NULL) {
		cifs_dbg(FYI, "Can't validate negotiate: null user mount\n");
		return 0; /* validation requires signing */
	}

	if (tcon->ses->session_flags & SMB2_SESSION_FLAG_IS_NULL)
		cifs_tcon_dbg(VFS, "Unexpected null user (anonymous) auth flag sent by server\n");

	pneg_inbuf = kmalloc(sizeof(*pneg_inbuf), GFP_NOFS);
	if (!pneg_inbuf)
		return -ENOMEM;

	pneg_inbuf->Capabilities =
			cpu_to_le32(server->vals->req_capabilities);
	if (tcon->ses->chan_max > 1)
		pneg_inbuf->Capabilities |= cpu_to_le32(SMB2_GLOBAL_CAP_MULTI_CHANNEL);

	memcpy(pneg_inbuf->Guid, server->client_guid,
					SMB2_CLIENT_GUID_SIZE);

	if (tcon->ses->sign)
		pneg_inbuf->SecurityMode =
			cpu_to_le16(SMB2_NEGOTIATE_SIGNING_REQUIRED);
	else if (global_secflags & CIFSSEC_MAY_SIGN)
		pneg_inbuf->SecurityMode =
			cpu_to_le16(SMB2_NEGOTIATE_SIGNING_ENABLED);
	else
		pneg_inbuf->SecurityMode = 0;


	if (strcmp(server->vals->version_string,
		SMB3ANY_VERSION_STRING) == 0) {
		pneg_inbuf->Dialects[0] = cpu_to_le16(SMB30_PROT_ID);
		pneg_inbuf->Dialects[1] = cpu_to_le16(SMB302_PROT_ID);
		pneg_inbuf->Dialects[2] = cpu_to_le16(SMB311_PROT_ID);
		pneg_inbuf->DialectCount = cpu_to_le16(3);
		/* SMB 2.1 not included so subtract one dialect from len */
		inbuflen = sizeof(*pneg_inbuf) -
				(sizeof(pneg_inbuf->Dialects[0]));
	} else if (strcmp(server->vals->version_string,
		SMBDEFAULT_VERSION_STRING) == 0) {
		pneg_inbuf->Dialects[0] = cpu_to_le16(SMB21_PROT_ID);
		pneg_inbuf->Dialects[1] = cpu_to_le16(SMB30_PROT_ID);
		pneg_inbuf->Dialects[2] = cpu_to_le16(SMB302_PROT_ID);
		pneg_inbuf->Dialects[3] = cpu_to_le16(SMB311_PROT_ID);
		pneg_inbuf->DialectCount = cpu_to_le16(4);
		/* structure is big enough for 4 dialects */
		inbuflen = sizeof(*pneg_inbuf);
	} else {
		/* otherwise specific dialect was requested */
		pneg_inbuf->Dialects[0] =
			cpu_to_le16(server->vals->protocol_id);
		pneg_inbuf->DialectCount = cpu_to_le16(1);
		/* structure is big enough for 4 dialects, sending only 1 */
		inbuflen = sizeof(*pneg_inbuf) -
				sizeof(pneg_inbuf->Dialects[0]) * 3;
	}

	rc = SMB2_ioctl(xid, tcon, NO_FILE_ID, NO_FILE_ID,
		FSCTL_VALIDATE_NEGOTIATE_INFO,
		(char *)pneg_inbuf, inbuflen, CIFSMaxBufSize,
		(char **)&pneg_rsp, &rsplen);
	if (rc == -EOPNOTSUPP) {
		/*
		 * Old Windows versions or Netapp SMB server can return
		 * not supported error. Client should accept it.
		 */
		cifs_tcon_dbg(VFS, "Server does not support validate negotiate\n");
		rc = 0;
		goto out_free_inbuf;
	} else if (rc != 0) {
		cifs_tcon_dbg(VFS, "validate protocol negotiate failed: %d\n",
			      rc);
		rc = -EIO;
		goto out_free_inbuf;
	}

	rc = -EIO;
	if (rsplen != sizeof(*pneg_rsp)) {
		cifs_tcon_dbg(VFS, "Invalid protocol negotiate response size: %d\n",
			      rsplen);

		/* relax check since Mac returns max bufsize allowed on ioctl */
		if (rsplen > CIFSMaxBufSize || rsplen < sizeof(*pneg_rsp))
			goto out_free_rsp;
	}

	/* check validate negotiate info response matches what we got earlier */
	if (pneg_rsp->Dialect != cpu_to_le16(server->dialect))
		goto vneg_out;

	if (pneg_rsp->SecurityMode != cpu_to_le16(server->sec_mode))
		goto vneg_out;

	/* do not validate server guid because not saved at negprot time yet */

	if ((le32_to_cpu(pneg_rsp->Capabilities) | SMB2_NT_FIND |
	      SMB2_LARGE_FILES) != server->capabilities)
		goto vneg_out;

	/* validate negotiate successful */
	rc = 0;
	cifs_dbg(FYI, "validate negotiate info successful\n");
	goto out_free_rsp;

vneg_out:
	cifs_tcon_dbg(VFS, "protocol revalidation - security settings mismatch\n");
out_free_rsp:
	kfree(pneg_rsp);
out_free_inbuf:
	kfree(pneg_inbuf);
	return rc;
}

enum securityEnum
smb2_select_sectype(struct TCP_Server_Info *server, enum securityEnum requested)
{
	switch (requested) {
	case Kerberos:
	case RawNTLMSSP:
		return requested;
	case NTLMv2:
		return RawNTLMSSP;
	case Unspecified:
		if (server->sec_ntlmssp &&
			(global_secflags & CIFSSEC_MAY_NTLMSSP))
			return RawNTLMSSP;
		if ((server->sec_kerberos || server->sec_mskerberos) &&
			(global_secflags & CIFSSEC_MAY_KRB5))
			return Kerberos;
		fallthrough;
	default:
		return Unspecified;
	}
}

struct SMB2_sess_data {
	unsigned int xid;
	struct cifs_ses *ses;
	struct TCP_Server_Info *server;
	struct nls_table *nls_cp;
	void (*func)(struct SMB2_sess_data *);
	int result;
	u64 previous_session;

	/* we will send the SMB in three pieces:
	 * a fixed length beginning part, an optional
	 * SPNEGO blob (which can be zero length), and a
	 * last part which will include the strings
	 * and rest of bcc area. This allows us to avoid
	 * a large buffer 17K allocation
	 */
	int buf0_type;
	struct kvec iov[2];
};

static int
SMB2_sess_alloc_buffer(struct SMB2_sess_data *sess_data)
{
	int rc;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	struct smb2_sess_setup_req *req;
	unsigned int total_len;
	bool is_binding = false;

	rc = smb2_plain_req_init(SMB2_SESSION_SETUP, NULL, server,
				 (void **) &req,
				 &total_len);
	if (rc)
		return rc;

	spin_lock(&ses->ses_lock);
	is_binding = (ses->ses_status == SES_GOOD);
	spin_unlock(&ses->ses_lock);

	if (is_binding) {
		req->hdr.SessionId = cpu_to_le64(ses->Suid);
		req->hdr.Flags |= SMB2_FLAGS_SIGNED;
		req->PreviousSessionId = 0;
		req->Flags = SMB2_SESSION_REQ_FLAG_BINDING;
		cifs_dbg(FYI, "Binding to sess id: %llx\n", ses->Suid);
	} else {
		/* First session, not a reauthenticate */
		req->hdr.SessionId = 0;
		/*
		 * if reconnect, we need to send previous sess id
		 * otherwise it is 0
		 */
		req->PreviousSessionId = cpu_to_le64(sess_data->previous_session);
		req->Flags = 0; /* MBZ */
		cifs_dbg(FYI, "Fresh session. Previous: %llx\n",
			 sess_data->previous_session);
	}

	/* enough to enable echos and oplocks and one max size write */
	req->hdr.CreditRequest = cpu_to_le16(130);

	/* only one of SMB2 signing flags may be set in SMB2 request */
	if (server->sign)
		req->SecurityMode = SMB2_NEGOTIATE_SIGNING_REQUIRED;
	else if (global_secflags & CIFSSEC_MAY_SIGN) /* one flag unlike MUST_ */
		req->SecurityMode = SMB2_NEGOTIATE_SIGNING_ENABLED;
	else
		req->SecurityMode = 0;

#ifdef CONFIG_CIFS_DFS_UPCALL
	req->Capabilities = cpu_to_le32(SMB2_GLOBAL_CAP_DFS);
#else
	req->Capabilities = 0;
#endif /* DFS_UPCALL */

	req->Channel = 0; /* MBZ */

	sess_data->iov[0].iov_base = (char *)req;
	/* 1 for pad */
	sess_data->iov[0].iov_len = total_len - 1;
	/*
	 * This variable will be used to clear the buffer
	 * allocated above in case of any error in the calling function.
	 */
	sess_data->buf0_type = CIFS_SMALL_BUFFER;

	return 0;
}

static void
SMB2_sess_free_buffer(struct SMB2_sess_data *sess_data)
{
	struct kvec *iov = sess_data->iov;

	/* iov[1] is already freed by caller */
	if (sess_data->buf0_type != CIFS_NO_BUFFER && iov[0].iov_base)
		memzero_explicit(iov[0].iov_base, iov[0].iov_len);

	free_rsp_buf(sess_data->buf0_type, iov[0].iov_base);
	sess_data->buf0_type = CIFS_NO_BUFFER;
}

static int
SMB2_sess_sendreceive(struct SMB2_sess_data *sess_data)
{
	int rc;
	struct smb_rqst rqst;
	struct smb2_sess_setup_req *req = sess_data->iov[0].iov_base;
	struct kvec rsp_iov = { NULL, 0 };

	/* Testing shows that buffer offset must be at location of Buffer[0] */
	req->SecurityBufferOffset =
		cpu_to_le16(sizeof(struct smb2_sess_setup_req) - 1 /* pad */);
	req->SecurityBufferLength = cpu_to_le16(sess_data->iov[1].iov_len);

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = sess_data->iov;
	rqst.rq_nvec = 2;

	/* BB add code to build os and lm fields */
	rc = cifs_send_recv(sess_data->xid, sess_data->ses,
			    sess_data->server,
			    &rqst,
			    &sess_data->buf0_type,
			    CIFS_LOG_ERROR | CIFS_SESS_OP, &rsp_iov);
	cifs_small_buf_release(sess_data->iov[0].iov_base);
	memcpy(&sess_data->iov[0], &rsp_iov, sizeof(struct kvec));

	return rc;
}

static int
SMB2_sess_establish_session(struct SMB2_sess_data *sess_data)
{
	int rc = 0;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;

	cifs_server_lock(server);
	if (server->ops->generate_signingkey) {
		rc = server->ops->generate_signingkey(ses, server);
		if (rc) {
			cifs_dbg(FYI,
				"SMB3 session key generation failed\n");
			cifs_server_unlock(server);
			return rc;
		}
	}
	if (!server->session_estab) {
		server->sequence_number = 0x2;
		server->session_estab = true;
	}
	cifs_server_unlock(server);

	cifs_dbg(FYI, "SMB2/3 session established successfully\n");
	return rc;
}

#ifdef CONFIG_CIFS_UPCALL
static void
SMB2_auth_kerberos(struct SMB2_sess_data *sess_data)
{
	int rc;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	struct cifs_spnego_msg *msg;
	struct key *spnego_key = NULL;
	struct smb2_sess_setup_rsp *rsp = NULL;
	bool is_binding = false;

	rc = SMB2_sess_alloc_buffer(sess_data);
	if (rc)
		goto out;

	spnego_key = cifs_get_spnego_key(ses, server);
	if (IS_ERR(spnego_key)) {
		rc = PTR_ERR(spnego_key);
		if (rc == -ENOKEY)
			cifs_dbg(VFS, "Verify user has a krb5 ticket and keyutils is installed\n");
		spnego_key = NULL;
		goto out;
	}

	msg = spnego_key->payload.data[0];
	/*
	 * check version field to make sure that cifs.upcall is
	 * sending us a response in an expected form
	 */
	if (msg->version != CIFS_SPNEGO_UPCALL_VERSION) {
		cifs_dbg(VFS, "bad cifs.upcall version. Expected %d got %d\n",
			 CIFS_SPNEGO_UPCALL_VERSION, msg->version);
		rc = -EKEYREJECTED;
		goto out_put_spnego_key;
	}

	spin_lock(&ses->ses_lock);
	is_binding = (ses->ses_status == SES_GOOD);
	spin_unlock(&ses->ses_lock);

	/* keep session key if binding */
	if (!is_binding) {
		kfree_sensitive(ses->auth_key.response);
		ses->auth_key.response = kmemdup(msg->data, msg->sesskey_len,
						 GFP_KERNEL);
		if (!ses->auth_key.response) {
			cifs_dbg(VFS, "Kerberos can't allocate (%u bytes) memory\n",
				 msg->sesskey_len);
			rc = -ENOMEM;
			goto out_put_spnego_key;
		}
		ses->auth_key.len = msg->sesskey_len;
	}

	sess_data->iov[1].iov_base = msg->data + msg->sesskey_len;
	sess_data->iov[1].iov_len = msg->secblob_len;

	rc = SMB2_sess_sendreceive(sess_data);
	if (rc)
		goto out_put_spnego_key;

	rsp = (struct smb2_sess_setup_rsp *)sess_data->iov[0].iov_base;
	/* keep session id and flags if binding */
	if (!is_binding) {
		ses->Suid = le64_to_cpu(rsp->hdr.SessionId);
		ses->session_flags = le16_to_cpu(rsp->SessionFlags);
	}

	rc = SMB2_sess_establish_session(sess_data);
out_put_spnego_key:
	key_invalidate(spnego_key);
	key_put(spnego_key);
	if (rc) {
		kfree_sensitive(ses->auth_key.response);
		ses->auth_key.response = NULL;
		ses->auth_key.len = 0;
	}
out:
	sess_data->result = rc;
	sess_data->func = NULL;
	SMB2_sess_free_buffer(sess_data);
}
#else
static void
SMB2_auth_kerberos(struct SMB2_sess_data *sess_data)
{
	cifs_dbg(VFS, "Kerberos negotiated but upcall support disabled!\n");
	sess_data->result = -EOPNOTSUPP;
	sess_data->func = NULL;
}
#endif

static void
SMB2_sess_auth_rawntlmssp_authenticate(struct SMB2_sess_data *sess_data);

static void
SMB2_sess_auth_rawntlmssp_negotiate(struct SMB2_sess_data *sess_data)
{
	int rc;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	struct smb2_sess_setup_rsp *rsp = NULL;
	unsigned char *ntlmssp_blob = NULL;
	bool use_spnego = false; /* else use raw ntlmssp */
	u16 blob_length = 0;
	bool is_binding = false;

	/*
	 * If memory allocation is successful, caller of this function
	 * frees it.
	 */
	ses->ntlmssp = kmalloc(sizeof(struct ntlmssp_auth), GFP_KERNEL);
	if (!ses->ntlmssp) {
		rc = -ENOMEM;
		goto out_err;
	}
	ses->ntlmssp->sesskey_per_smbsess = true;

	rc = SMB2_sess_alloc_buffer(sess_data);
	if (rc)
		goto out_err;

	rc = build_ntlmssp_smb3_negotiate_blob(&ntlmssp_blob,
					  &blob_length, ses, server,
					  sess_data->nls_cp);
	if (rc)
		goto out;

	if (use_spnego) {
		/* BB eventually need to add this */
		cifs_dbg(VFS, "spnego not supported for SMB2 yet\n");
		rc = -EOPNOTSUPP;
		goto out;
	}
	sess_data->iov[1].iov_base = ntlmssp_blob;
	sess_data->iov[1].iov_len = blob_length;

	rc = SMB2_sess_sendreceive(sess_data);
	rsp = (struct smb2_sess_setup_rsp *)sess_data->iov[0].iov_base;

	/* If true, rc here is expected and not an error */
	if (sess_data->buf0_type != CIFS_NO_BUFFER &&
		rsp->hdr.Status == STATUS_MORE_PROCESSING_REQUIRED)
		rc = 0;

	if (rc)
		goto out;

	if (offsetof(struct smb2_sess_setup_rsp, Buffer) !=
			le16_to_cpu(rsp->SecurityBufferOffset)) {
		cifs_dbg(VFS, "Invalid security buffer offset %d\n",
			le16_to_cpu(rsp->SecurityBufferOffset));
		rc = -EIO;
		goto out;
	}
	rc = decode_ntlmssp_challenge(rsp->Buffer,
			le16_to_cpu(rsp->SecurityBufferLength), ses);
	if (rc)
		goto out;

	cifs_dbg(FYI, "rawntlmssp session setup challenge phase\n");

	spin_lock(&ses->ses_lock);
	is_binding = (ses->ses_status == SES_GOOD);
	spin_unlock(&ses->ses_lock);

	/* keep existing ses id and flags if binding */
	if (!is_binding) {
		ses->Suid = le64_to_cpu(rsp->hdr.SessionId);
		ses->session_flags = le16_to_cpu(rsp->SessionFlags);
	}

out:
	kfree_sensitive(ntlmssp_blob);
	SMB2_sess_free_buffer(sess_data);
	if (!rc) {
		sess_data->result = 0;
		sess_data->func = SMB2_sess_auth_rawntlmssp_authenticate;
		return;
	}
out_err:
	kfree_sensitive(ses->ntlmssp);
	ses->ntlmssp = NULL;
	sess_data->result = rc;
	sess_data->func = NULL;
}

static void
SMB2_sess_auth_rawntlmssp_authenticate(struct SMB2_sess_data *sess_data)
{
	int rc;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	struct smb2_sess_setup_req *req;
	struct smb2_sess_setup_rsp *rsp = NULL;
	unsigned char *ntlmssp_blob = NULL;
	bool use_spnego = false; /* else use raw ntlmssp */
	u16 blob_length = 0;
	bool is_binding = false;

	rc = SMB2_sess_alloc_buffer(sess_data);
	if (rc)
		goto out;

	req = (struct smb2_sess_setup_req *) sess_data->iov[0].iov_base;
	req->hdr.SessionId = cpu_to_le64(ses->Suid);

	rc = build_ntlmssp_auth_blob(&ntlmssp_blob, &blob_length,
				     ses, server,
				     sess_data->nls_cp);
	if (rc) {
		cifs_dbg(FYI, "build_ntlmssp_auth_blob failed %d\n", rc);
		goto out;
	}

	if (use_spnego) {
		/* BB eventually need to add this */
		cifs_dbg(VFS, "spnego not supported for SMB2 yet\n");
		rc = -EOPNOTSUPP;
		goto out;
	}
	sess_data->iov[1].iov_base = ntlmssp_blob;
	sess_data->iov[1].iov_len = blob_length;

	rc = SMB2_sess_sendreceive(sess_data);
	if (rc)
		goto out;

	rsp = (struct smb2_sess_setup_rsp *)sess_data->iov[0].iov_base;

	spin_lock(&ses->ses_lock);
	is_binding = (ses->ses_status == SES_GOOD);
	spin_unlock(&ses->ses_lock);

	/* keep existing ses id and flags if binding */
	if (!is_binding) {
		ses->Suid = le64_to_cpu(rsp->hdr.SessionId);
		ses->session_flags = le16_to_cpu(rsp->SessionFlags);
	}

	rc = SMB2_sess_establish_session(sess_data);
#ifdef CONFIG_CIFS_DEBUG_DUMP_KEYS
	if (ses->server->dialect < SMB30_PROT_ID) {
		cifs_dbg(VFS, "%s: dumping generated SMB2 session keys\n", __func__);
		/*
		 * The session id is opaque in terms of endianness, so we can't
		 * print it as a long long. we dump it as we got it on the wire
		 */
		cifs_dbg(VFS, "Session Id    %*ph\n", (int)sizeof(ses->Suid),
			 &ses->Suid);
		cifs_dbg(VFS, "Session Key   %*ph\n",
			 SMB2_NTLMV2_SESSKEY_SIZE, ses->auth_key.response);
		cifs_dbg(VFS, "Signing Key   %*ph\n",
			 SMB3_SIGN_KEY_SIZE, ses->auth_key.response);
	}
#endif
out:
	kfree_sensitive(ntlmssp_blob);
	SMB2_sess_free_buffer(sess_data);
	kfree_sensitive(ses->ntlmssp);
	ses->ntlmssp = NULL;
	sess_data->result = rc;
	sess_data->func = NULL;
}

static int
SMB2_select_sec(struct SMB2_sess_data *sess_data)
{
	int type;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;

	type = smb2_select_sectype(server, ses->sectype);
	cifs_dbg(FYI, "sess setup type %d\n", type);
	if (type == Unspecified) {
		cifs_dbg(VFS, "Unable to select appropriate authentication method!\n");
		return -EINVAL;
	}

	switch (type) {
	case Kerberos:
		sess_data->func = SMB2_auth_kerberos;
		break;
	case RawNTLMSSP:
		sess_data->func = SMB2_sess_auth_rawntlmssp_negotiate;
		break;
	default:
		cifs_dbg(VFS, "secType %d not supported!\n", type);
		return -EOPNOTSUPP;
	}

	return 0;
}

int
SMB2_sess_setup(const unsigned int xid, struct cifs_ses *ses,
		struct TCP_Server_Info *server,
		const struct nls_table *nls_cp)
{
	int rc = 0;
	struct SMB2_sess_data *sess_data;

	cifs_dbg(FYI, "Session Setup\n");

	if (!server) {
		WARN(1, "%s: server is NULL!\n", __func__);
		return -EIO;
	}

	sess_data = kzalloc(sizeof(struct SMB2_sess_data), GFP_KERNEL);
	if (!sess_data)
		return -ENOMEM;

	sess_data->xid = xid;
	sess_data->ses = ses;
	sess_data->server = server;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	sess_data->nls_cp = (struct nls_table *) nls_cp;
	sess_data->previous_session = ses->Suid;

	rc = SMB2_select_sec(sess_data);
	if (rc)
		goto out;

	/*
	 * Initialize the session hash with the server one.
	 */
	memcpy(ses->preauth_sha_hash, server->preauth_sha_hash,
	       SMB2_PREAUTH_HASH_SIZE);

	while (sess_data->func)
		sess_data->func(sess_data);

	if ((ses->session_flags & SMB2_SESSION_FLAG_IS_GUEST) && (ses->sign))
		cifs_server_dbg(VFS, "signing requested but authenticated as guest\n");
	rc = sess_data->result;
out:
	kfree_sensitive(sess_data);
	return rc;
}

int
SMB2_logoff(const unsigned int xid, struct cifs_ses *ses)
{
	struct smb_rqst rqst;
	struct smb2_logoff_req *req; /* response is also trivial struct */
	int rc = 0;
	struct TCP_Server_Info *server;
	int flags = 0;
	unsigned int total_len;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int resp_buf_type;

	cifs_dbg(FYI, "disconnect session %p\n", ses);

	if (ses && (ses->server))
		server = ses->server;
	else
		return -EIO;

	/* no need to send SMB logoff if uid already closed due to reconnect */
	spin_lock(&ses->chan_lock);
	if (CIFS_ALL_CHANS_NEED_RECONNECT(ses)) {
		spin_unlock(&ses->chan_lock);
		goto smb2_session_already_dead;
	}
	spin_unlock(&ses->chan_lock);

	rc = smb2_plain_req_init(SMB2_LOGOFF, NULL, ses->server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	 /* since no tcon, smb2_init can not do this, so do here */
	req->hdr.SessionId = cpu_to_le64(ses->Suid);

	if (ses->session_flags & SMB2_SESSION_FLAG_ENCRYPT_DATA)
		flags |= CIFS_TRANSFORM_REQ;
	else if (server->sign)
		req->hdr.Flags |= SMB2_FLAGS_SIGNED;

	flags |= CIFS_NO_RSP_BUF;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, ses->server,
			    &rqst, &resp_buf_type, flags, &rsp_iov);
	cifs_small_buf_release(req);
	/*
	 * No tcon so can't do
	 * cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_fail[SMB2...]);
	 */

smb2_session_already_dead:
	return rc;
}

static inline void cifs_stats_fail_inc(struct cifs_tcon *tcon, uint16_t code)
{
	cifs_stats_inc(&tcon->stats.smb2_stats.smb2_com_failed[code]);
}

#define MAX_SHARENAME_LENGTH (255 /* server */ + 80 /* share */ + 1 /* NULL */)

/* These are similar values to what Windows uses */
static inline void init_copy_chunk_defaults(struct cifs_tcon *tcon)
{
	tcon->max_chunks = 256;
	tcon->max_bytes_chunk = 1048576;
	tcon->max_bytes_copy = 16777216;
}

int
SMB2_tcon(const unsigned int xid, struct cifs_ses *ses, const char *tree,
	  struct cifs_tcon *tcon, const struct nls_table *cp)
{
	struct smb_rqst rqst;
	struct smb2_tree_connect_req *req;
	struct smb2_tree_connect_rsp *rsp = NULL;
	struct kvec iov[2];
	struct kvec rsp_iov = { NULL, 0 };
	int rc = 0;
	int resp_buftype;
	int unc_path_len;
	__le16 *unc_path = NULL;
	int flags = 0;
	unsigned int total_len;
	struct TCP_Server_Info *server;

	/* always use master channel */
	server = ses->server;

	cifs_dbg(FYI, "TCON\n");

	if (!server || !tree)
		return -EIO;

	unc_path = kmalloc(MAX_SHARENAME_LENGTH * 2, GFP_KERNEL);
	if (unc_path == NULL)
		return -ENOMEM;

	unc_path_len = cifs_strtoUTF16(unc_path, tree, strlen(tree), cp) + 1;
	unc_path_len *= 2;
	if (unc_path_len < 2) {
		kfree(unc_path);
		return -EINVAL;
	}

	/* SMB2 TREE_CONNECT request must be called with TreeId == 0 */
	tcon->tid = 0;
	atomic_set(&tcon->num_remote_opens, 0);
	rc = smb2_plain_req_init(SMB2_TREE_CONNECT, tcon, server,
				 (void **) &req, &total_len);
	if (rc) {
		kfree(unc_path);
		return rc;
	}

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	iov[0].iov_base = (char *)req;
	/* 1 for pad */
	iov[0].iov_len = total_len - 1;

	/* Testing shows that buffer offset must be at location of Buffer[0] */
	req->PathOffset = cpu_to_le16(sizeof(struct smb2_tree_connect_req)
			- 1 /* pad */);
	req->PathLength = cpu_to_le16(unc_path_len - 2);
	iov[1].iov_base = unc_path;
	iov[1].iov_len = unc_path_len;

	/*
	 * 3.11 tcon req must be signed if not encrypted. See MS-SMB2 3.2.4.1.1
	 * unless it is guest or anonymous user. See MS-SMB2 3.2.5.3.1
	 * (Samba servers don't always set the flag so also check if null user)
	 */
	if ((server->dialect == SMB311_PROT_ID) &&
	    !smb3_encryption_required(tcon) &&
	    !(ses->session_flags &
		    (SMB2_SESSION_FLAG_IS_GUEST|SMB2_SESSION_FLAG_IS_NULL)) &&
	    ((ses->user_name != NULL) || (ses->sectype == Kerberos)))
		req->hdr.Flags |= SMB2_FLAGS_SIGNED;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 2;

	/* Need 64 for max size write so ask for more in case not there yet */
	req->hdr.CreditRequest = cpu_to_le16(64);

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	cifs_small_buf_release(req);
	rsp = (struct smb2_tree_connect_rsp *)rsp_iov.iov_base;
	trace_smb3_tcon(xid, tcon->tid, ses->Suid, tree, rc);
	if ((rc != 0) || (rsp == NULL)) {
		cifs_stats_fail_inc(tcon, SMB2_TREE_CONNECT_HE);
		tcon->need_reconnect = true;
		goto tcon_error_exit;
	}

	switch (rsp->ShareType) {
	case SMB2_SHARE_TYPE_DISK:
		cifs_dbg(FYI, "connection to disk share\n");
		break;
	case SMB2_SHARE_TYPE_PIPE:
		tcon->pipe = true;
		cifs_dbg(FYI, "connection to pipe share\n");
		break;
	case SMB2_SHARE_TYPE_PRINT:
		tcon->print = true;
		cifs_dbg(FYI, "connection to printer\n");
		break;
	default:
		cifs_server_dbg(VFS, "unknown share type %d\n", rsp->ShareType);
		rc = -EOPNOTSUPP;
		goto tcon_error_exit;
	}

	tcon->share_flags = le32_to_cpu(rsp->ShareFlags);
	tcon->capabilities = rsp->Capabilities; /* we keep caps little endian */
	tcon->maximal_access = le32_to_cpu(rsp->MaximalAccess);
	tcon->tid = le32_to_cpu(rsp->hdr.Id.SyncId.TreeId);
	strscpy(tcon->tree_name, tree, sizeof(tcon->tree_name));

	if ((rsp->Capabilities & SMB2_SHARE_CAP_DFS) &&
	    ((tcon->share_flags & SHI1005_FLAGS_DFS) == 0))
		cifs_tcon_dbg(VFS, "DFS capability contradicts DFS flag\n");

	if (tcon->seal &&
	    !(server->capabilities & SMB2_GLOBAL_CAP_ENCRYPTION))
		cifs_tcon_dbg(VFS, "Encryption is requested but not supported\n");

	init_copy_chunk_defaults(tcon);
	if (server->ops->validate_negotiate)
		rc = server->ops->validate_negotiate(xid, tcon);
tcon_exit:

	free_rsp_buf(resp_buftype, rsp);
	kfree(unc_path);
	return rc;

tcon_error_exit:
	if (rsp && rsp->hdr.Status == STATUS_BAD_NETWORK_NAME)
		cifs_tcon_dbg(VFS, "BAD_NETWORK_NAME: %s\n", tree);
	goto tcon_exit;
}

int
SMB2_tdis(const unsigned int xid, struct cifs_tcon *tcon)
{
	struct smb_rqst rqst;
	struct smb2_tree_disconnect_req *req; /* response is trivial */
	int rc = 0;
	struct cifs_ses *ses = tcon->ses;
	int flags = 0;
	unsigned int total_len;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int resp_buf_type;

	cifs_dbg(FYI, "Tree Disconnect\n");

	if (!ses || !(ses->server))
		return -EIO;

	trace_smb3_tdis_enter(xid, tcon->tid, ses->Suid, tcon->tree_name);
	spin_lock(&ses->chan_lock);
	if ((tcon->need_reconnect) ||
	    (CIFS_ALL_CHANS_NEED_RECONNECT(tcon->ses))) {
		spin_unlock(&ses->chan_lock);
		return 0;
	}
	spin_unlock(&ses->chan_lock);

	invalidate_all_cached_dirs(tcon);

	rc = smb2_plain_req_init(SMB2_TREE_DISCONNECT, tcon, ses->server,
				 (void **) &req,
				 &total_len);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	flags |= CIFS_NO_RSP_BUF;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, ses->server,
			    &rqst, &resp_buf_type, flags, &rsp_iov);
	cifs_small_buf_release(req);
	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_TREE_DISCONNECT_HE);
		trace_smb3_tdis_err(xid, tcon->tid, ses->Suid, rc);
	}
	trace_smb3_tdis_done(xid, tcon->tid, ses->Suid);

	return rc;
}


static struct create_durable *
create_durable_buf(void)
{
	struct create_durable *buf;

	buf = kzalloc(sizeof(struct create_durable), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset = cpu_to_le16(offsetof
					(struct create_durable, Data));
	buf->ccontext.DataLength = cpu_to_le32(16);
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct create_durable, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	/* SMB2_CREATE_DURABLE_HANDLE_REQUEST is "DHnQ" */
	buf->Name[0] = 'D';
	buf->Name[1] = 'H';
	buf->Name[2] = 'n';
	buf->Name[3] = 'Q';
	return buf;
}

static struct create_durable *
create_reconnect_durable_buf(struct cifs_fid *fid)
{
	struct create_durable *buf;

	buf = kzalloc(sizeof(struct create_durable), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset = cpu_to_le16(offsetof
					(struct create_durable, Data));
	buf->ccontext.DataLength = cpu_to_le32(16);
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct create_durable, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	buf->Data.Fid.PersistentFileId = fid->persistent_fid;
	buf->Data.Fid.VolatileFileId = fid->volatile_fid;
	/* SMB2_CREATE_DURABLE_HANDLE_RECONNECT is "DHnC" */
	buf->Name[0] = 'D';
	buf->Name[1] = 'H';
	buf->Name[2] = 'n';
	buf->Name[3] = 'C';
	return buf;
}

static void
parse_query_id_ctxt(struct create_context *cc, struct smb2_file_all_info *buf)
{
	struct create_on_disk_id *pdisk_id = (struct create_on_disk_id *)cc;

	cifs_dbg(FYI, "parse query id context 0x%llx 0x%llx\n",
		pdisk_id->DiskFileId, pdisk_id->VolumeId);
	buf->IndexNumber = pdisk_id->DiskFileId;
}

static void
parse_posix_ctxt(struct create_context *cc, struct smb2_file_all_info *info,
		 struct create_posix_rsp *posix)
{
	int sid_len;
	u8 *beg = (u8 *)cc + le16_to_cpu(cc->DataOffset);
	u8 *end = beg + le32_to_cpu(cc->DataLength);
	u8 *sid;

	memset(posix, 0, sizeof(*posix));

	posix->nlink = le32_to_cpu(*(__le32 *)(beg + 0));
	posix->reparse_tag = le32_to_cpu(*(__le32 *)(beg + 4));
	posix->mode = le32_to_cpu(*(__le32 *)(beg + 8));

	sid = beg + 12;
	sid_len = posix_info_sid_size(sid, end);
	if (sid_len < 0) {
		cifs_dbg(VFS, "bad owner sid in posix create response\n");
		return;
	}
	memcpy(&posix->owner, sid, sid_len);

	sid = sid + sid_len;
	sid_len = posix_info_sid_size(sid, end);
	if (sid_len < 0) {
		cifs_dbg(VFS, "bad group sid in posix create response\n");
		return;
	}
	memcpy(&posix->group, sid, sid_len);

	cifs_dbg(FYI, "nlink=%d mode=%o reparse_tag=%x\n",
		 posix->nlink, posix->mode, posix->reparse_tag);
}

void
smb2_parse_contexts(struct TCP_Server_Info *server,
		    struct smb2_create_rsp *rsp,
		    unsigned int *epoch, char *lease_key, __u8 *oplock,
		    struct smb2_file_all_info *buf,
		    struct create_posix_rsp *posix)
{
	char *data_offset;
	struct create_context *cc;
	unsigned int next;
	unsigned int remaining;
	char *name;
	static const char smb3_create_tag_posix[] = {
		0x93, 0xAD, 0x25, 0x50, 0x9C,
		0xB4, 0x11, 0xE7, 0xB4, 0x23, 0x83,
		0xDE, 0x96, 0x8B, 0xCD, 0x7C
	};

	*oplock = 0;
	data_offset = (char *)rsp + le32_to_cpu(rsp->CreateContextsOffset);
	remaining = le32_to_cpu(rsp->CreateContextsLength);
	cc = (struct create_context *)data_offset;

	/* Initialize inode number to 0 in case no valid data in qfid context */
	if (buf)
		buf->IndexNumber = 0;

	while (remaining >= sizeof(struct create_context)) {
		name = le16_to_cpu(cc->NameOffset) + (char *)cc;
		if (le16_to_cpu(cc->NameLength) == 4 &&
		    strncmp(name, SMB2_CREATE_REQUEST_LEASE, 4) == 0)
			*oplock = server->ops->parse_lease_buf(cc, epoch,
							   lease_key);
		else if (buf && (le16_to_cpu(cc->NameLength) == 4) &&
		    strncmp(name, SMB2_CREATE_QUERY_ON_DISK_ID, 4) == 0)
			parse_query_id_ctxt(cc, buf);
		else if ((le16_to_cpu(cc->NameLength) == 16)) {
			if (posix &&
			    memcmp(name, smb3_create_tag_posix, 16) == 0)
				parse_posix_ctxt(cc, buf, posix);
		}
		/* else {
			cifs_dbg(FYI, "Context not matched with len %d\n",
				le16_to_cpu(cc->NameLength));
			cifs_dump_mem("Cctxt name: ", name, 4);
		} */

		next = le32_to_cpu(cc->Next);
		if (!next)
			break;
		remaining -= next;
		cc = (struct create_context *)((char *)cc + next);
	}

	if (rsp->OplockLevel != SMB2_OPLOCK_LEVEL_LEASE)
		*oplock = rsp->OplockLevel;

	return;
}

static int
add_lease_context(struct TCP_Server_Info *server, struct kvec *iov,
		  unsigned int *num_iovec, u8 *lease_key, __u8 *oplock)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	iov[num].iov_base = server->ops->create_lease_buf(lease_key, *oplock);
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = server->vals->create_lease_size;
	req->RequestedOplockLevel = SMB2_OPLOCK_LEVEL_LEASE;
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset = cpu_to_le32(
				sizeof(struct smb2_create_req) +
				iov[num - 1].iov_len);
	le32_add_cpu(&req->CreateContextsLength,
		     server->vals->create_lease_size);
	*num_iovec = num + 1;
	return 0;
}

static struct create_durable_v2 *
create_durable_v2_buf(struct cifs_open_parms *oparms)
{
	struct cifs_fid *pfid = oparms->fid;
	struct create_durable_v2 *buf;

	buf = kzalloc(sizeof(struct create_durable_v2), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset = cpu_to_le16(offsetof
					(struct create_durable_v2, dcontext));
	buf->ccontext.DataLength = cpu_to_le32(sizeof(struct durable_context_v2));
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct create_durable_v2, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);

	/*
	 * NB: Handle timeout defaults to 0, which allows server to choose
	 * (most servers default to 120 seconds) and most clients default to 0.
	 * This can be overridden at mount ("handletimeout=") if the user wants
	 * a different persistent (or resilient) handle timeout for all opens
	 * opens on a particular SMB3 mount.
	 */
	buf->dcontext.Timeout = cpu_to_le32(oparms->tcon->handle_timeout);
	buf->dcontext.Flags = cpu_to_le32(SMB2_DHANDLE_FLAG_PERSISTENT);
	generate_random_uuid(buf->dcontext.CreateGuid);
	memcpy(pfid->create_guid, buf->dcontext.CreateGuid, 16);

	/* SMB2_CREATE_DURABLE_HANDLE_REQUEST is "DH2Q" */
	buf->Name[0] = 'D';
	buf->Name[1] = 'H';
	buf->Name[2] = '2';
	buf->Name[3] = 'Q';
	return buf;
}

static struct create_durable_handle_reconnect_v2 *
create_reconnect_durable_v2_buf(struct cifs_fid *fid)
{
	struct create_durable_handle_reconnect_v2 *buf;

	buf = kzalloc(sizeof(struct create_durable_handle_reconnect_v2),
			GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset =
		cpu_to_le16(offsetof(struct create_durable_handle_reconnect_v2,
				     dcontext));
	buf->ccontext.DataLength =
		cpu_to_le32(sizeof(struct durable_reconnect_context_v2));
	buf->ccontext.NameOffset =
		cpu_to_le16(offsetof(struct create_durable_handle_reconnect_v2,
			    Name));
	buf->ccontext.NameLength = cpu_to_le16(4);

	buf->dcontext.Fid.PersistentFileId = fid->persistent_fid;
	buf->dcontext.Fid.VolatileFileId = fid->volatile_fid;
	buf->dcontext.Flags = cpu_to_le32(SMB2_DHANDLE_FLAG_PERSISTENT);
	memcpy(buf->dcontext.CreateGuid, fid->create_guid, 16);

	/* SMB2_CREATE_DURABLE_HANDLE_RECONNECT_V2 is "DH2C" */
	buf->Name[0] = 'D';
	buf->Name[1] = 'H';
	buf->Name[2] = '2';
	buf->Name[3] = 'C';
	return buf;
}

static int
add_durable_v2_context(struct kvec *iov, unsigned int *num_iovec,
		    struct cifs_open_parms *oparms)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	iov[num].iov_base = create_durable_v2_buf(oparms);
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = sizeof(struct create_durable_v2);
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset =
			cpu_to_le32(sizeof(struct smb2_create_req) +
								iov[1].iov_len);
	le32_add_cpu(&req->CreateContextsLength, sizeof(struct create_durable_v2));
	*num_iovec = num + 1;
	return 0;
}

static int
add_durable_reconnect_v2_context(struct kvec *iov, unsigned int *num_iovec,
		    struct cifs_open_parms *oparms)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	/* indicate that we don't need to relock the file */
	oparms->reconnect = false;

	iov[num].iov_base = create_reconnect_durable_v2_buf(oparms->fid);
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = sizeof(struct create_durable_handle_reconnect_v2);
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset =
			cpu_to_le32(sizeof(struct smb2_create_req) +
								iov[1].iov_len);
	le32_add_cpu(&req->CreateContextsLength,
			sizeof(struct create_durable_handle_reconnect_v2));
	*num_iovec = num + 1;
	return 0;
}

static int
add_durable_context(struct kvec *iov, unsigned int *num_iovec,
		    struct cifs_open_parms *oparms, bool use_persistent)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	if (use_persistent) {
		if (oparms->reconnect)
			return add_durable_reconnect_v2_context(iov, num_iovec,
								oparms);
		else
			return add_durable_v2_context(iov, num_iovec, oparms);
	}

	if (oparms->reconnect) {
		iov[num].iov_base = create_reconnect_durable_buf(oparms->fid);
		/* indicate that we don't need to relock the file */
		oparms->reconnect = false;
	} else
		iov[num].iov_base = create_durable_buf();
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = sizeof(struct create_durable);
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset =
			cpu_to_le32(sizeof(struct smb2_create_req) +
								iov[1].iov_len);
	le32_add_cpu(&req->CreateContextsLength, sizeof(struct create_durable));
	*num_iovec = num + 1;
	return 0;
}

/* See MS-SMB2 2.2.13.2.7 */
static struct crt_twarp_ctxt *
create_twarp_buf(__u64 timewarp)
{
	struct crt_twarp_ctxt *buf;

	buf = kzalloc(sizeof(struct crt_twarp_ctxt), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset = cpu_to_le16(offsetof
					(struct crt_twarp_ctxt, Timestamp));
	buf->ccontext.DataLength = cpu_to_le32(8);
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct crt_twarp_ctxt, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	/* SMB2_CREATE_TIMEWARP_TOKEN is "TWrp" */
	buf->Name[0] = 'T';
	buf->Name[1] = 'W';
	buf->Name[2] = 'r';
	buf->Name[3] = 'p';
	buf->Timestamp = cpu_to_le64(timewarp);
	return buf;
}

/* See MS-SMB2 2.2.13.2.7 */
static int
add_twarp_context(struct kvec *iov, unsigned int *num_iovec, __u64 timewarp)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	iov[num].iov_base = create_twarp_buf(timewarp);
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = sizeof(struct crt_twarp_ctxt);
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset = cpu_to_le32(
				sizeof(struct smb2_create_req) +
				iov[num - 1].iov_len);
	le32_add_cpu(&req->CreateContextsLength, sizeof(struct crt_twarp_ctxt));
	*num_iovec = num + 1;
	return 0;
}

/* See See http://technet.microsoft.com/en-us/library/hh509017(v=ws.10).aspx */
static void setup_owner_group_sids(char *buf)
{
	struct owner_group_sids *sids = (struct owner_group_sids *)buf;

	/* Populate the user ownership fields S-1-5-88-1 */
	sids->owner.Revision = 1;
	sids->owner.NumAuth = 3;
	sids->owner.Authority[5] = 5;
	sids->owner.SubAuthorities[0] = cpu_to_le32(88);
	sids->owner.SubAuthorities[1] = cpu_to_le32(1);
	sids->owner.SubAuthorities[2] = cpu_to_le32(current_fsuid().val);

	/* Populate the group ownership fields S-1-5-88-2 */
	sids->group.Revision = 1;
	sids->group.NumAuth = 3;
	sids->group.Authority[5] = 5;
	sids->group.SubAuthorities[0] = cpu_to_le32(88);
	sids->group.SubAuthorities[1] = cpu_to_le32(2);
	sids->group.SubAuthorities[2] = cpu_to_le32(current_fsgid().val);

	cifs_dbg(FYI, "owner S-1-5-88-1-%d, group S-1-5-88-2-%d\n", current_fsuid().val, current_fsgid().val);
}

/* See MS-SMB2 2.2.13.2.2 and MS-DTYP 2.4.6 */
static struct crt_sd_ctxt *
create_sd_buf(umode_t mode, bool set_owner, unsigned int *len)
{
	struct crt_sd_ctxt *buf;
	__u8 *ptr, *aclptr;
	unsigned int acelen, acl_size, ace_count;
	unsigned int owner_offset = 0;
	unsigned int group_offset = 0;
	struct smb3_acl acl = {};

	*len = round_up(sizeof(struct crt_sd_ctxt) + (sizeof(struct cifs_ace) * 4), 8);

	if (set_owner) {
		/* sizeof(struct owner_group_sids) is already multiple of 8 so no need to round */
		*len += sizeof(struct owner_group_sids);
	}

	buf = kzalloc(*len, GFP_KERNEL);
	if (buf == NULL)
		return buf;

	ptr = (__u8 *)&buf[1];
	if (set_owner) {
		/* offset fields are from beginning of security descriptor not of create context */
		owner_offset = ptr - (__u8 *)&buf->sd;
		buf->sd.OffsetOwner = cpu_to_le32(owner_offset);
		group_offset = owner_offset + offsetof(struct owner_group_sids, group);
		buf->sd.OffsetGroup = cpu_to_le32(group_offset);

		setup_owner_group_sids(ptr);
		ptr += sizeof(struct owner_group_sids);
	} else {
		buf->sd.OffsetOwner = 0;
		buf->sd.OffsetGroup = 0;
	}

	buf->ccontext.DataOffset = cpu_to_le16(offsetof(struct crt_sd_ctxt, sd));
	buf->ccontext.NameOffset = cpu_to_le16(offsetof(struct crt_sd_ctxt, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	/* SMB2_CREATE_SD_BUFFER_TOKEN is "SecD" */
	buf->Name[0] = 'S';
	buf->Name[1] = 'e';
	buf->Name[2] = 'c';
	buf->Name[3] = 'D';
	buf->sd.Revision = 1;  /* Must be one see MS-DTYP 2.4.6 */

	/*
	 * ACL is "self relative" ie ACL is stored in contiguous block of memory
	 * and "DP" ie the DACL is present
	 */
	buf->sd.Control = cpu_to_le16(ACL_CONTROL_SR | ACL_CONTROL_DP);

	/* offset owner, group and Sbz1 and SACL are all zero */
	buf->sd.OffsetDacl = cpu_to_le32(ptr - (__u8 *)&buf->sd);
	/* Ship the ACL for now. we will copy it into buf later. */
	aclptr = ptr;
	ptr += sizeof(struct smb3_acl);

	/* create one ACE to hold the mode embedded in reserved special SID */
	acelen = setup_special_mode_ACE((struct cifs_ace *)ptr, (__u64)mode);
	ptr += acelen;
	acl_size = acelen + sizeof(struct smb3_acl);
	ace_count = 1;

	if (set_owner) {
		/* we do not need to reallocate buffer to add the two more ACEs. plenty of space */
		acelen = setup_special_user_owner_ACE((struct cifs_ace *)ptr);
		ptr += acelen;
		acl_size += acelen;
		ace_count += 1;
	}

	/* and one more ACE to allow access for authenticated users */
	acelen = setup_authusers_ACE((struct cifs_ace *)ptr);
	ptr += acelen;
	acl_size += acelen;
	ace_count += 1;

	acl.AclRevision = ACL_REVISION; /* See 2.4.4.1 of MS-DTYP */
	acl.AclSize = cpu_to_le16(acl_size);
	acl.AceCount = cpu_to_le16(ace_count);
	/* acl.Sbz1 and Sbz2 MBZ so are not set here, but initialized above */
	memcpy(aclptr, &acl, sizeof(struct smb3_acl));

	buf->ccontext.DataLength = cpu_to_le32(ptr - (__u8 *)&buf->sd);
	*len = round_up((unsigned int)(ptr - (__u8 *)buf), 8);

	return buf;
}

static int
add_sd_context(struct kvec *iov, unsigned int *num_iovec, umode_t mode, bool set_owner)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;
	unsigned int len = 0;

	iov[num].iov_base = create_sd_buf(mode, set_owner, &len);
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = len;
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset = cpu_to_le32(
				sizeof(struct smb2_create_req) +
				iov[num - 1].iov_len);
	le32_add_cpu(&req->CreateContextsLength, len);
	*num_iovec = num + 1;
	return 0;
}

static struct crt_query_id_ctxt *
create_query_id_buf(void)
{
	struct crt_query_id_ctxt *buf;

	buf = kzalloc(sizeof(struct crt_query_id_ctxt), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->ccontext.DataOffset = cpu_to_le16(0);
	buf->ccontext.DataLength = cpu_to_le32(0);
	buf->ccontext.NameOffset = cpu_to_le16(offsetof
				(struct crt_query_id_ctxt, Name));
	buf->ccontext.NameLength = cpu_to_le16(4);
	/* SMB2_CREATE_QUERY_ON_DISK_ID is "QFid" */
	buf->Name[0] = 'Q';
	buf->Name[1] = 'F';
	buf->Name[2] = 'i';
	buf->Name[3] = 'd';
	return buf;
}

/* See MS-SMB2 2.2.13.2.9 */
static int
add_query_id_context(struct kvec *iov, unsigned int *num_iovec)
{
	struct smb2_create_req *req = iov[0].iov_base;
	unsigned int num = *num_iovec;

	iov[num].iov_base = create_query_id_buf();
	if (iov[num].iov_base == NULL)
		return -ENOMEM;
	iov[num].iov_len = sizeof(struct crt_query_id_ctxt);
	if (!req->CreateContextsOffset)
		req->CreateContextsOffset = cpu_to_le32(
				sizeof(struct smb2_create_req) +
				iov[num - 1].iov_len);
	le32_add_cpu(&req->CreateContextsLength, sizeof(struct crt_query_id_ctxt));
	*num_iovec = num + 1;
	return 0;
}

static int
alloc_path_with_tree_prefix(__le16 **out_path, int *out_size, int *out_len,
			    const char *treename, const __le16 *path)
{
	int treename_len, path_len;
	struct nls_table *cp;
	const __le16 sep[] = {cpu_to_le16('\\'), cpu_to_le16(0x0000)};

	/*
	 * skip leading "\\"
	 */
	treename_len = strlen(treename);
	if (treename_len < 2 || !(treename[0] == '\\' && treename[1] == '\\'))
		return -EINVAL;

	treename += 2;
	treename_len -= 2;

	path_len = UniStrnlen((wchar_t *)path, PATH_MAX);

	/* make room for one path separator only if @path isn't empty */
	*out_len = treename_len + (path[0] ? 1 : 0) + path_len;

	/*
	 * final path needs to be 8-byte aligned as specified in
	 * MS-SMB2 2.2.13 SMB2 CREATE Request.
	 */
	*out_size = round_up(*out_len * sizeof(__le16), 8);
	*out_path = kzalloc(*out_size + sizeof(__le16) /* null */, GFP_KERNEL);
	if (!*out_path)
		return -ENOMEM;

	cp = load_nls_default();
	cifs_strtoUTF16(*out_path, treename, treename_len, cp);

	/* Do not append the separator if the path is empty */
	if (path[0] != cpu_to_le16(0x0000)) {
		UniStrcat(*out_path, sep);
		UniStrcat(*out_path, path);
	}

	unload_nls(cp);

	return 0;
}

int smb311_posix_mkdir(const unsigned int xid, struct inode *inode,
			       umode_t mode, struct cifs_tcon *tcon,
			       const char *full_path,
			       struct cifs_sb_info *cifs_sb)
{
	struct smb_rqst rqst;
	struct smb2_create_req *req;
	struct smb2_create_rsp *rsp = NULL;
	struct cifs_ses *ses = tcon->ses;
	struct kvec iov[3]; /* make sure at least one for each open context */
	struct kvec rsp_iov = {NULL, 0};
	int resp_buftype;
	int uni_path_len;
	__le16 *copy_path = NULL;
	int copy_size;
	int rc = 0;
	unsigned int n_iov = 2;
	__u32 file_attributes = 0;
	char *pc_buf = NULL;
	int flags = 0;
	unsigned int total_len;
	__le16 *utf16_path = NULL;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);

	cifs_dbg(FYI, "mkdir\n");

	/* resource #1: path allocation */
	utf16_path = cifs_convert_path_to_utf16(full_path, cifs_sb);
	if (!utf16_path)
		return -ENOMEM;

	if (!ses || !server) {
		rc = -EIO;
		goto err_free_path;
	}

	/* resource #2: request */
	rc = smb2_plain_req_init(SMB2_CREATE, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		goto err_free_path;


	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	req->ImpersonationLevel = IL_IMPERSONATION;
	req->DesiredAccess = cpu_to_le32(FILE_WRITE_ATTRIBUTES);
	/* File attributes ignored on open (used in create though) */
	req->FileAttributes = cpu_to_le32(file_attributes);
	req->ShareAccess = FILE_SHARE_ALL_LE;
	req->CreateDisposition = cpu_to_le32(FILE_CREATE);
	req->CreateOptions = cpu_to_le32(CREATE_NOT_FILE);

	iov[0].iov_base = (char *)req;
	/* -1 since last byte is buf[0] which is sent below (path) */
	iov[0].iov_len = total_len - 1;

	req->NameOffset = cpu_to_le16(sizeof(struct smb2_create_req));

	/* [MS-SMB2] 2.2.13 NameOffset:
	 * If SMB2_FLAGS_DFS_OPERATIONS is set in the Flags field of
	 * the SMB2 header, the file name includes a prefix that will
	 * be processed during DFS name normalization as specified in
	 * section 3.3.5.9. Otherwise, the file name is relative to
	 * the share that is identified by the TreeId in the SMB2
	 * header.
	 */
	if (tcon->share_flags & SHI1005_FLAGS_DFS) {
		int name_len;

		req->hdr.Flags |= SMB2_FLAGS_DFS_OPERATIONS;
		rc = alloc_path_with_tree_prefix(&copy_path, &copy_size,
						 &name_len,
						 tcon->tree_name, utf16_path);
		if (rc)
			goto err_free_req;

		req->NameLength = cpu_to_le16(name_len * 2);
		uni_path_len = copy_size;
		/* free before overwriting resource */
		kfree(utf16_path);
		utf16_path = copy_path;
	} else {
		uni_path_len = (2 * UniStrnlen((wchar_t *)utf16_path, PATH_MAX)) + 2;
		/* MUST set path len (NameLength) to 0 opening root of share */
		req->NameLength = cpu_to_le16(uni_path_len - 2);
		if (uni_path_len % 8 != 0) {
			copy_size = roundup(uni_path_len, 8);
			copy_path = kzalloc(copy_size, GFP_KERNEL);
			if (!copy_path) {
				rc = -ENOMEM;
				goto err_free_req;
			}
			memcpy((char *)copy_path, (const char *)utf16_path,
			       uni_path_len);
			uni_path_len = copy_size;
			/* free before overwriting resource */
			kfree(utf16_path);
			utf16_path = copy_path;
		}
	}

	iov[1].iov_len = uni_path_len;
	iov[1].iov_base = utf16_path;
	req->RequestedOplockLevel = SMB2_OPLOCK_LEVEL_NONE;

	if (tcon->posix_extensions) {
		/* resource #3: posix buf */
		rc = add_posix_context(iov, &n_iov, mode);
		if (rc)
			goto err_free_req;
		pc_buf = iov[n_iov-1].iov_base;
	}


	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = n_iov;

	/* no need to inc num_remote_opens because we close it just below */
	trace_smb3_posix_mkdir_enter(xid, tcon->tid, ses->Suid, full_path, CREATE_NOT_FILE,
				    FILE_WRITE_ATTRIBUTES);
	/* resource #4: response buffer */
	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_CREATE_HE);
		trace_smb3_posix_mkdir_err(xid, tcon->tid, ses->Suid,
					   CREATE_NOT_FILE,
					   FILE_WRITE_ATTRIBUTES, rc);
		goto err_free_rsp_buf;
	}

	/*
	 * Although unlikely to be possible for rsp to be null and rc not set,
	 * adding check below is slightly safer long term (and quiets Coverity
	 * warning)
	 */
	rsp = (struct smb2_create_rsp *)rsp_iov.iov_base;
	if (rsp == NULL) {
		rc = -EIO;
		kfree(pc_buf);
		goto err_free_req;
	}

	trace_smb3_posix_mkdir_done(xid, rsp->PersistentFileId, tcon->tid, ses->Suid,
				    CREATE_NOT_FILE, FILE_WRITE_ATTRIBUTES);

	SMB2_close(xid, tcon, rsp->PersistentFileId, rsp->VolatileFileId);

	/* Eventually save off posix specific response info and timestaps */

err_free_rsp_buf:
	free_rsp_buf(resp_buftype, rsp);
	kfree(pc_buf);
err_free_req:
	cifs_small_buf_release(req);
err_free_path:
	kfree(utf16_path);
	return rc;
}

int
SMB2_open_init(struct cifs_tcon *tcon, struct TCP_Server_Info *server,
	       struct smb_rqst *rqst, __u8 *oplock,
	       struct cifs_open_parms *oparms, __le16 *path)
{
	struct smb2_create_req *req;
	unsigned int n_iov = 2;
	__u32 file_attributes = 0;
	int copy_size;
	int uni_path_len;
	unsigned int total_len;
	struct kvec *iov = rqst->rq_iov;
	__le16 *copy_path;
	int rc;

	rc = smb2_plain_req_init(SMB2_CREATE, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	iov[0].iov_base = (char *)req;
	/* -1 since last byte is buf[0] which is sent below (path) */
	iov[0].iov_len = total_len - 1;

	if (oparms->create_options & CREATE_OPTION_READONLY)
		file_attributes |= ATTR_READONLY;
	if (oparms->create_options & CREATE_OPTION_SPECIAL)
		file_attributes |= ATTR_SYSTEM;

	req->ImpersonationLevel = IL_IMPERSONATION;
	req->DesiredAccess = cpu_to_le32(oparms->desired_access);
	/* File attributes ignored on open (used in create though) */
	req->FileAttributes = cpu_to_le32(file_attributes);
	req->ShareAccess = FILE_SHARE_ALL_LE;

	req->CreateDisposition = cpu_to_le32(oparms->disposition);
	req->CreateOptions = cpu_to_le32(oparms->create_options & CREATE_OPTIONS_MASK);
	req->NameOffset = cpu_to_le16(sizeof(struct smb2_create_req));

	/* [MS-SMB2] 2.2.13 NameOffset:
	 * If SMB2_FLAGS_DFS_OPERATIONS is set in the Flags field of
	 * the SMB2 header, the file name includes a prefix that will
	 * be processed during DFS name normalization as specified in
	 * section 3.3.5.9. Otherwise, the file name is relative to
	 * the share that is identified by the TreeId in the SMB2
	 * header.
	 */
	if (tcon->share_flags & SHI1005_FLAGS_DFS) {
		int name_len;

		req->hdr.Flags |= SMB2_FLAGS_DFS_OPERATIONS;
		rc = alloc_path_with_tree_prefix(&copy_path, &copy_size,
						 &name_len,
						 tcon->tree_name, path);
		if (rc)
			return rc;
		req->NameLength = cpu_to_le16(name_len * 2);
		uni_path_len = copy_size;
		path = copy_path;
	} else {
		uni_path_len = (2 * UniStrnlen((wchar_t *)path, PATH_MAX)) + 2;
		/* MUST set path len (NameLength) to 0 opening root of share */
		req->NameLength = cpu_to_le16(uni_path_len - 2);
		copy_size = round_up(uni_path_len, 8);
		copy_path = kzalloc(copy_size, GFP_KERNEL);
		if (!copy_path)
			return -ENOMEM;
		memcpy((char *)copy_path, (const char *)path,
		       uni_path_len);
		uni_path_len = copy_size;
		path = copy_path;
	}

	iov[1].iov_len = uni_path_len;
	iov[1].iov_base = path;

	if ((!server->oplocks) || (tcon->no_lease))
		*oplock = SMB2_OPLOCK_LEVEL_NONE;

	if (!(server->capabilities & SMB2_GLOBAL_CAP_LEASING) ||
	    *oplock == SMB2_OPLOCK_LEVEL_NONE)
		req->RequestedOplockLevel = *oplock;
	else if (!(server->capabilities & SMB2_GLOBAL_CAP_DIRECTORY_LEASING) &&
		  (oparms->create_options & CREATE_NOT_FILE))
		req->RequestedOplockLevel = *oplock; /* no srv lease support */
	else {
		rc = add_lease_context(server, iov, &n_iov,
				       oparms->fid->lease_key, oplock);
		if (rc)
			return rc;
	}

	if (*oplock == SMB2_OPLOCK_LEVEL_BATCH) {
		/* need to set Next field of lease context if we request it */
		if (server->capabilities & SMB2_GLOBAL_CAP_LEASING) {
			struct create_context *ccontext =
			    (struct create_context *)iov[n_iov-1].iov_base;
			ccontext->Next =
				cpu_to_le32(server->vals->create_lease_size);
		}

		rc = add_durable_context(iov, &n_iov, oparms,
					tcon->use_persistent);
		if (rc)
			return rc;
	}

	if (tcon->posix_extensions) {
		if (n_iov > 2) {
			struct create_context *ccontext =
			    (struct create_context *)iov[n_iov-1].iov_base;
			ccontext->Next =
				cpu_to_le32(iov[n_iov-1].iov_len);
		}

		rc = add_posix_context(iov, &n_iov, oparms->mode);
		if (rc)
			return rc;
	}

	if (tcon->snapshot_time) {
		cifs_dbg(FYI, "adding snapshot context\n");
		if (n_iov > 2) {
			struct create_context *ccontext =
			    (struct create_context *)iov[n_iov-1].iov_base;
			ccontext->Next =
				cpu_to_le32(iov[n_iov-1].iov_len);
		}

		rc = add_twarp_context(iov, &n_iov, tcon->snapshot_time);
		if (rc)
			return rc;
	}

	if ((oparms->disposition != FILE_OPEN) && (oparms->cifs_sb)) {
		bool set_mode;
		bool set_owner;

		if ((oparms->cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID) &&
		    (oparms->mode != ACL_NO_MODE))
			set_mode = true;
		else {
			set_mode = false;
			oparms->mode = ACL_NO_MODE;
		}

		if (oparms->cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UID_FROM_ACL)
			set_owner = true;
		else
			set_owner = false;

		if (set_owner | set_mode) {
			if (n_iov > 2) {
				struct create_context *ccontext =
				    (struct create_context *)iov[n_iov-1].iov_base;
				ccontext->Next = cpu_to_le32(iov[n_iov-1].iov_len);
			}

			cifs_dbg(FYI, "add sd with mode 0x%x\n", oparms->mode);
			rc = add_sd_context(iov, &n_iov, oparms->mode, set_owner);
			if (rc)
				return rc;
		}
	}

	if (n_iov > 2) {
		struct create_context *ccontext =
			(struct create_context *)iov[n_iov-1].iov_base;
		ccontext->Next = cpu_to_le32(iov[n_iov-1].iov_len);
	}
	add_query_id_context(iov, &n_iov);

	rqst->rq_nvec = n_iov;
	return 0;
}

/* rq_iov[0] is the request and is released by cifs_small_buf_release().
 * All other vectors are freed by kfree().
 */
void
SMB2_open_free(struct smb_rqst *rqst)
{
	int i;

	if (rqst && rqst->rq_iov) {
		cifs_small_buf_release(rqst->rq_iov[0].iov_base);
		for (i = 1; i < rqst->rq_nvec; i++)
			if (rqst->rq_iov[i].iov_base != smb2_padding)
				kfree(rqst->rq_iov[i].iov_base);
	}
}

int
SMB2_open(const unsigned int xid, struct cifs_open_parms *oparms, __le16 *path,
	  __u8 *oplock, struct smb2_file_all_info *buf,
	  struct create_posix_rsp *posix,
	  struct kvec *err_iov, int *buftype)
{
	struct smb_rqst rqst;
	struct smb2_create_rsp *rsp = NULL;
	struct cifs_tcon *tcon = oparms->tcon;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	struct kvec iov[SMB2_CREATE_IOV_SIZE];
	struct kvec rsp_iov = {NULL, 0};
	int resp_buftype = CIFS_NO_BUFFER;
	int rc = 0;
	int flags = 0;

	cifs_dbg(FYI, "create/open\n");
	if (!ses || !server)
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	rqst.rq_iov = iov;
	rqst.rq_nvec = SMB2_CREATE_IOV_SIZE;

	rc = SMB2_open_init(tcon, server,
			    &rqst, oplock, oparms, path);
	if (rc)
		goto creat_exit;

	trace_smb3_open_enter(xid, tcon->tid, tcon->ses->Suid, oparms->path,
		oparms->create_options, oparms->desired_access);

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags,
			    &rsp_iov);
	rsp = (struct smb2_create_rsp *)rsp_iov.iov_base;

	if (rc != 0) {
		cifs_stats_fail_inc(tcon, SMB2_CREATE_HE);
		if (err_iov && rsp) {
			*err_iov = rsp_iov;
			*buftype = resp_buftype;
			resp_buftype = CIFS_NO_BUFFER;
			rsp = NULL;
		}
		trace_smb3_open_err(xid, tcon->tid, ses->Suid,
				    oparms->create_options, oparms->desired_access, rc);
		if (rc == -EREMCHG) {
			pr_warn_once("server share %s deleted\n",
				     tcon->tree_name);
			tcon->need_reconnect = true;
		}
		goto creat_exit;
	} else if (rsp == NULL) /* unlikely to happen, but safer to check */
		goto creat_exit;
	else
		trace_smb3_open_done(xid, rsp->PersistentFileId, tcon->tid, ses->Suid,
				     oparms->create_options, oparms->desired_access);

	atomic_inc(&tcon->num_remote_opens);
	oparms->fid->persistent_fid = rsp->PersistentFileId;
	oparms->fid->volatile_fid = rsp->VolatileFileId;
	oparms->fid->access = oparms->desired_access;
#ifdef CONFIG_CIFS_DEBUG2
	oparms->fid->mid = le64_to_cpu(rsp->hdr.MessageId);
#endif /* CIFS_DEBUG2 */

	if (buf) {
		buf->CreationTime = rsp->CreationTime;
		buf->LastAccessTime = rsp->LastAccessTime;
		buf->LastWriteTime = rsp->LastWriteTime;
		buf->ChangeTime = rsp->ChangeTime;
		buf->AllocationSize = rsp->AllocationSize;
		buf->EndOfFile = rsp->EndofFile;
		buf->Attributes = rsp->FileAttributes;
		buf->NumberOfLinks = cpu_to_le32(1);
		buf->DeletePending = 0;
	}


	smb2_parse_contexts(server, rsp, &oparms->fid->epoch,
			    oparms->fid->lease_key, oplock, buf, posix);
creat_exit:
	SMB2_open_free(&rqst);
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

int
SMB2_ioctl_init(struct cifs_tcon *tcon, struct TCP_Server_Info *server,
		struct smb_rqst *rqst,
		u64 persistent_fid, u64 volatile_fid, u32 opcode,
		char *in_data, u32 indatalen,
		__u32 max_response_size)
{
	struct smb2_ioctl_req *req;
	struct kvec *iov = rqst->rq_iov;
	unsigned int total_len;
	int rc;
	char *in_data_buf;

	rc = smb2_ioctl_req_init(opcode, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	if (indatalen) {
		/*
		 * indatalen is usually small at a couple of bytes max, so
		 * just allocate through generic pool
		 */
		in_data_buf = kmemdup(in_data, indatalen, GFP_NOFS);
		if (!in_data_buf) {
			cifs_small_buf_release(req);
			return -ENOMEM;
		}
	}

	req->CtlCode = cpu_to_le32(opcode);
	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;

	iov[0].iov_base = (char *)req;
	/*
	 * If no input data, the size of ioctl struct in
	 * protocol spec still includes a 1 byte data buffer,
	 * but if input data passed to ioctl, we do not
	 * want to double count this, so we do not send
	 * the dummy one byte of data in iovec[0] if sending
	 * input data (in iovec[1]).
	 */
	if (indatalen) {
		req->InputCount = cpu_to_le32(indatalen);
		/* do not set InputOffset if no input data */
		req->InputOffset =
		       cpu_to_le32(offsetof(struct smb2_ioctl_req, Buffer));
		rqst->rq_nvec = 2;
		iov[0].iov_len = total_len - 1;
		iov[1].iov_base = in_data_buf;
		iov[1].iov_len = indatalen;
	} else {
		rqst->rq_nvec = 1;
		iov[0].iov_len = total_len;
	}

	req->OutputOffset = 0;
	req->OutputCount = 0; /* MBZ */

	/*
	 * In most cases max_response_size is set to 16K (CIFSMaxBufSize)
	 * We Could increase default MaxOutputResponse, but that could require
	 * more credits. Windows typically sets this smaller, but for some
	 * ioctls it may be useful to allow server to send more. No point
	 * limiting what the server can send as long as fits in one credit
	 * We can not handle more than CIFS_MAX_BUF_SIZE yet but may want
	 * to increase this limit up in the future.
	 * Note that for snapshot queries that servers like Azure expect that
	 * the first query be minimal size (and just used to get the number/size
	 * of previous versions) so response size must be specified as EXACTLY
	 * sizeof(struct snapshot_array) which is 16 when rounded up to multiple
	 * of eight bytes.  Currently that is the only case where we set max
	 * response size smaller.
	 */
	req->MaxOutputResponse = cpu_to_le32(max_response_size);
	req->hdr.CreditCharge =
		cpu_to_le16(DIV_ROUND_UP(max(indatalen, max_response_size),
					 SMB2_MAX_BUFFER_SIZE));
	/* always an FSCTL (for now) */
	req->Flags = cpu_to_le32(SMB2_0_IOCTL_IS_FSCTL);

	/* validate negotiate request must be signed - see MS-SMB2 3.2.5.5 */
	if (opcode == FSCTL_VALIDATE_NEGOTIATE_INFO)
		req->hdr.Flags |= SMB2_FLAGS_SIGNED;

	return 0;
}

void
SMB2_ioctl_free(struct smb_rqst *rqst)
{
	int i;
	if (rqst && rqst->rq_iov) {
		cifs_small_buf_release(rqst->rq_iov[0].iov_base); /* request */
		for (i = 1; i < rqst->rq_nvec; i++)
			if (rqst->rq_iov[i].iov_base != smb2_padding)
				kfree(rqst->rq_iov[i].iov_base);
	}
}


/*
 *	SMB2 IOCTL is used for both IOCTLs and FSCTLs
 */
int
SMB2_ioctl(const unsigned int xid, struct cifs_tcon *tcon, u64 persistent_fid,
	   u64 volatile_fid, u32 opcode, char *in_data, u32 indatalen,
	   u32 max_out_data_len, char **out_data,
	   u32 *plen /* returned data len */)
{
	struct smb_rqst rqst;
	struct smb2_ioctl_rsp *rsp = NULL;
	struct cifs_ses *ses;
	struct TCP_Server_Info *server;
	struct kvec iov[SMB2_IOCTL_IOV_SIZE];
	struct kvec rsp_iov = {NULL, 0};
	int resp_buftype = CIFS_NO_BUFFER;
	int rc = 0;
	int flags = 0;

	cifs_dbg(FYI, "SMB2 IOCTL\n");

	if (out_data != NULL)
		*out_data = NULL;

	/* zero out returned data len, in case of error */
	if (plen)
		*plen = 0;

	if (!tcon)
		return -EIO;

	ses = tcon->ses;
	if (!ses)
		return -EIO;

	server = cifs_pick_channel(ses);
	if (!server)
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	rqst.rq_iov = iov;
	rqst.rq_nvec = SMB2_IOCTL_IOV_SIZE;

	rc = SMB2_ioctl_init(tcon, server,
			     &rqst, persistent_fid, volatile_fid, opcode,
			     in_data, indatalen, max_out_data_len);
	if (rc)
		goto ioctl_exit;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags,
			    &rsp_iov);
	rsp = (struct smb2_ioctl_rsp *)rsp_iov.iov_base;

	if (rc != 0)
		trace_smb3_fsctl_err(xid, persistent_fid, tcon->tid,
				ses->Suid, 0, opcode, rc);

	if ((rc != 0) && (rc != -EINVAL) && (rc != -E2BIG)) {
		cifs_stats_fail_inc(tcon, SMB2_IOCTL_HE);
		goto ioctl_exit;
	} else if (rc == -EINVAL) {
		if ((opcode != FSCTL_SRV_COPYCHUNK_WRITE) &&
		    (opcode != FSCTL_SRV_COPYCHUNK)) {
			cifs_stats_fail_inc(tcon, SMB2_IOCTL_HE);
			goto ioctl_exit;
		}
	} else if (rc == -E2BIG) {
		if (opcode != FSCTL_QUERY_ALLOCATED_RANGES) {
			cifs_stats_fail_inc(tcon, SMB2_IOCTL_HE);
			goto ioctl_exit;
		}
	}

	/* check if caller wants to look at return data or just return rc */
	if ((plen == NULL) || (out_data == NULL))
		goto ioctl_exit;

	/*
	 * Although unlikely to be possible for rsp to be null and rc not set,
	 * adding check below is slightly safer long term (and quiets Coverity
	 * warning)
	 */
	if (rsp == NULL) {
		rc = -EIO;
		goto ioctl_exit;
	}

	*plen = le32_to_cpu(rsp->OutputCount);

	/* We check for obvious errors in the output buffer length and offset */
	if (*plen == 0)
		goto ioctl_exit; /* server returned no data */
	else if (*plen > rsp_iov.iov_len || *plen > 0xFF00) {
		cifs_tcon_dbg(VFS, "srv returned invalid ioctl length: %d\n", *plen);
		*plen = 0;
		rc = -EIO;
		goto ioctl_exit;
	}

	if (rsp_iov.iov_len - *plen < le32_to_cpu(rsp->OutputOffset)) {
		cifs_tcon_dbg(VFS, "Malformed ioctl resp: len %d offset %d\n", *plen,
			le32_to_cpu(rsp->OutputOffset));
		*plen = 0;
		rc = -EIO;
		goto ioctl_exit;
	}

	*out_data = kmemdup((char *)rsp + le32_to_cpu(rsp->OutputOffset),
			    *plen, GFP_KERNEL);
	if (*out_data == NULL) {
		rc = -ENOMEM;
		goto ioctl_exit;
	}

ioctl_exit:
	SMB2_ioctl_free(&rqst);
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

/*
 *   Individual callers to ioctl worker function follow
 */

int
SMB2_set_compression(const unsigned int xid, struct cifs_tcon *tcon,
		     u64 persistent_fid, u64 volatile_fid)
{
	int rc;
	struct  compress_ioctl fsctl_input;
	char *ret_data = NULL;

	fsctl_input.CompressionState =
			cpu_to_le16(COMPRESSION_FORMAT_DEFAULT);

	rc = SMB2_ioctl(xid, tcon, persistent_fid, volatile_fid,
			FSCTL_SET_COMPRESSION,
			(char *)&fsctl_input /* data input */,
			2 /* in data len */, CIFSMaxBufSize /* max out data */,
			&ret_data /* out data */, NULL);

	cifs_dbg(FYI, "set compression rc %d\n", rc);

	return rc;
}

int
SMB2_close_init(struct cifs_tcon *tcon, struct TCP_Server_Info *server,
		struct smb_rqst *rqst,
		u64 persistent_fid, u64 volatile_fid, bool query_attrs)
{
	struct smb2_close_req *req;
	struct kvec *iov = rqst->rq_iov;
	unsigned int total_len;
	int rc;

	rc = smb2_plain_req_init(SMB2_CLOSE, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;
	if (query_attrs)
		req->Flags = SMB2_CLOSE_FLAG_POSTQUERY_ATTRIB;
	else
		req->Flags = 0;
	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	return 0;
}

void
SMB2_close_free(struct smb_rqst *rqst)
{
	if (rqst && rqst->rq_iov)
		cifs_small_buf_release(rqst->rq_iov[0].iov_base); /* request */
}

int
__SMB2_close(const unsigned int xid, struct cifs_tcon *tcon,
	     u64 persistent_fid, u64 volatile_fid,
	     struct smb2_file_network_open_info *pbuf)
{
	struct smb_rqst rqst;
	struct smb2_close_rsp *rsp = NULL;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	struct kvec iov[1];
	struct kvec rsp_iov;
	int resp_buftype = CIFS_NO_BUFFER;
	int rc = 0;
	int flags = 0;
	bool query_attrs = false;

	cifs_dbg(FYI, "Close\n");

	if (!ses || !server)
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	/* check if need to ask server to return timestamps in close response */
	if (pbuf)
		query_attrs = true;

	trace_smb3_close_enter(xid, persistent_fid, tcon->tid, ses->Suid);
	rc = SMB2_close_init(tcon, server,
			     &rqst, persistent_fid, volatile_fid,
			     query_attrs);
	if (rc)
		goto close_exit;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	rsp = (struct smb2_close_rsp *)rsp_iov.iov_base;

	if (rc != 0) {
		cifs_stats_fail_inc(tcon, SMB2_CLOSE_HE);
		trace_smb3_close_err(xid, persistent_fid, tcon->tid, ses->Suid,
				     rc);
		goto close_exit;
	} else {
		trace_smb3_close_done(xid, persistent_fid, tcon->tid,
				      ses->Suid);
		/*
		 * Note that have to subtract 4 since struct network_open_info
		 * has a final 4 byte pad that close response does not have
		 */
		if (pbuf)
			memcpy(pbuf, (char *)&rsp->CreationTime, sizeof(*pbuf) - 4);
	}

	atomic_dec(&tcon->num_remote_opens);
close_exit:
	SMB2_close_free(&rqst);
	free_rsp_buf(resp_buftype, rsp);

	/* retry close in a worker thread if this one is interrupted */
	if (is_interrupt_error(rc)) {
		int tmp_rc;

		tmp_rc = smb2_handle_cancelled_close(tcon, persistent_fid,
						     volatile_fid);
		if (tmp_rc)
			cifs_dbg(VFS, "handle cancelled close fid 0x%llx returned error %d\n",
				 persistent_fid, tmp_rc);
	}
	return rc;
}

int
SMB2_close(const unsigned int xid, struct cifs_tcon *tcon,
		u64 persistent_fid, u64 volatile_fid)
{
	return __SMB2_close(xid, tcon, persistent_fid, volatile_fid, NULL);
}

int
smb2_validate_iov(unsigned int offset, unsigned int buffer_length,
		  struct kvec *iov, unsigned int min_buf_size)
{
	unsigned int smb_len = iov->iov_len;
	char *end_of_smb = smb_len + (char *)iov->iov_base;
	char *begin_of_buf = offset + (char *)iov->iov_base;
	char *end_of_buf = begin_of_buf + buffer_length;


	if (buffer_length < min_buf_size) {
		cifs_dbg(VFS, "buffer length %d smaller than minimum size %d\n",
			 buffer_length, min_buf_size);
		return -EINVAL;
	}

	/* check if beyond RFC1001 maximum length */
	if ((smb_len > 0x7FFFFF) || (buffer_length > 0x7FFFFF)) {
		cifs_dbg(VFS, "buffer length %d or smb length %d too large\n",
			 buffer_length, smb_len);
		return -EINVAL;
	}

	if ((begin_of_buf > end_of_smb) || (end_of_buf > end_of_smb)) {
		cifs_dbg(VFS, "Invalid server response, bad offset to data\n");
		return -EINVAL;
	}

	return 0;
}

/*
 * If SMB buffer fields are valid, copy into temporary buffer to hold result.
 * Caller must free buffer.
 */
int
smb2_validate_and_copy_iov(unsigned int offset, unsigned int buffer_length,
			   struct kvec *iov, unsigned int minbufsize,
			   char *data)
{
	char *begin_of_buf = offset + (char *)iov->iov_base;
	int rc;

	if (!data)
		return -EINVAL;

	rc = smb2_validate_iov(offset, buffer_length, iov, minbufsize);
	if (rc)
		return rc;

	memcpy(data, begin_of_buf, minbufsize);

	return 0;
}

int
SMB2_query_info_init(struct cifs_tcon *tcon, struct TCP_Server_Info *server,
		     struct smb_rqst *rqst,
		     u64 persistent_fid, u64 volatile_fid,
		     u8 info_class, u8 info_type, u32 additional_info,
		     size_t output_len, size_t input_len, void *input)
{
	struct smb2_query_info_req *req;
	struct kvec *iov = rqst->rq_iov;
	unsigned int total_len;
	int rc;

	rc = smb2_plain_req_init(SMB2_QUERY_INFO, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->InfoType = info_type;
	req->FileInfoClass = info_class;
	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;
	req->AdditionalInformation = cpu_to_le32(additional_info);

	req->OutputBufferLength = cpu_to_le32(output_len);
	if (input_len) {
		req->InputBufferLength = cpu_to_le32(input_len);
		/* total_len for smb query request never close to le16 max */
		req->InputBufferOffset = cpu_to_le16(total_len - 1);
		memcpy(req->Buffer, input, input_len);
	}

	iov[0].iov_base = (char *)req;
	/* 1 for Buffer */
	iov[0].iov_len = total_len - 1 + input_len;
	return 0;
}

void
SMB2_query_info_free(struct smb_rqst *rqst)
{
	if (rqst && rqst->rq_iov)
		cifs_small_buf_release(rqst->rq_iov[0].iov_base); /* request */
}

static int
query_info(const unsigned int xid, struct cifs_tcon *tcon,
	   u64 persistent_fid, u64 volatile_fid, u8 info_class, u8 info_type,
	   u32 additional_info, size_t output_len, size_t min_len, void **data,
		u32 *dlen)
{
	struct smb_rqst rqst;
	struct smb2_query_info_rsp *rsp = NULL;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int rc = 0;
	int resp_buftype = CIFS_NO_BUFFER;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server;
	int flags = 0;
	bool allocated = false;

	cifs_dbg(FYI, "Query Info\n");

	if (!ses)
		return -EIO;
	server = cifs_pick_channel(ses);
	if (!server)
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = SMB2_query_info_init(tcon, server,
				  &rqst, persistent_fid, volatile_fid,
				  info_class, info_type, additional_info,
				  output_len, 0, NULL);
	if (rc)
		goto qinf_exit;

	trace_smb3_query_info_enter(xid, persistent_fid, tcon->tid,
				    ses->Suid, info_class, (__u32)info_type);

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;

	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_QUERY_INFO_HE);
		trace_smb3_query_info_err(xid, persistent_fid, tcon->tid,
				ses->Suid, info_class, (__u32)info_type, rc);
		goto qinf_exit;
	}

	trace_smb3_query_info_done(xid, persistent_fid, tcon->tid,
				ses->Suid, info_class, (__u32)info_type);

	if (dlen) {
		*dlen = le32_to_cpu(rsp->OutputBufferLength);
		if (!*data) {
			*data = kmalloc(*dlen, GFP_KERNEL);
			if (!*data) {
				cifs_tcon_dbg(VFS,
					"Error %d allocating memory for acl\n",
					rc);
				*dlen = 0;
				rc = -ENOMEM;
				goto qinf_exit;
			}
			allocated = true;
		}
	}

	rc = smb2_validate_and_copy_iov(le16_to_cpu(rsp->OutputBufferOffset),
					le32_to_cpu(rsp->OutputBufferLength),
					&rsp_iov, dlen ? *dlen : min_len, *data);
	if (rc && allocated) {
		kfree(*data);
		*data = NULL;
		*dlen = 0;
	}

qinf_exit:
	SMB2_query_info_free(&rqst);
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

int SMB2_query_info(const unsigned int xid, struct cifs_tcon *tcon,
	u64 persistent_fid, u64 volatile_fid, struct smb2_file_all_info *data)
{
	return query_info(xid, tcon, persistent_fid, volatile_fid,
			  FILE_ALL_INFORMATION, SMB2_O_INFO_FILE, 0,
			  sizeof(struct smb2_file_all_info) + PATH_MAX * 2,
			  sizeof(struct smb2_file_all_info), (void **)&data,
			  NULL);
}

#if 0
/* currently unused, as now we are doing compounding instead (see smb311_posix_query_path_info) */
int
SMB311_posix_query_info(const unsigned int xid, struct cifs_tcon *tcon,
		u64 persistent_fid, u64 volatile_fid, struct smb311_posix_qinfo *data, u32 *plen)
{
	size_t output_len = sizeof(struct smb311_posix_qinfo *) +
			(sizeof(struct cifs_sid) * 2) + (PATH_MAX * 2);
	*plen = 0;

	return query_info(xid, tcon, persistent_fid, volatile_fid,
			  SMB_FIND_FILE_POSIX_INFO, SMB2_O_INFO_FILE, 0,
			  output_len, sizeof(struct smb311_posix_qinfo), (void **)&data, plen);
	/* Note caller must free "data" (passed in above). It may be allocated in query_info call */
}
#endif

int
SMB2_query_acl(const unsigned int xid, struct cifs_tcon *tcon,
	       u64 persistent_fid, u64 volatile_fid,
	       void **data, u32 *plen, u32 extra_info)
{
	__u32 additional_info = OWNER_SECINFO | GROUP_SECINFO | DACL_SECINFO |
				extra_info;
	*plen = 0;

	return query_info(xid, tcon, persistent_fid, volatile_fid,
			  0, SMB2_O_INFO_SECURITY, additional_info,
			  SMB2_MAX_BUFFER_SIZE, MIN_SEC_DESC_LEN, data, plen);
}

int
SMB2_get_srv_num(const unsigned int xid, struct cifs_tcon *tcon,
		 u64 persistent_fid, u64 volatile_fid, __le64 *uniqueid)
{
	return query_info(xid, tcon, persistent_fid, volatile_fid,
			  FILE_INTERNAL_INFORMATION, SMB2_O_INFO_FILE, 0,
			  sizeof(struct smb2_file_internal_info),
			  sizeof(struct smb2_file_internal_info),
			  (void **)&uniqueid, NULL);
}

/*
 * CHANGE_NOTIFY Request is sent to get notifications on changes to a directory
 * See MS-SMB2 2.2.35 and 2.2.36
 */

static int
SMB2_notify_init(const unsigned int xid, struct smb_rqst *rqst,
		 struct cifs_tcon *tcon, struct TCP_Server_Info *server,
		 u64 persistent_fid, u64 volatile_fid,
		 u32 completion_filter, bool watch_tree)
{
	struct smb2_change_notify_req *req;
	struct kvec *iov = rqst->rq_iov;
	unsigned int total_len;
	int rc;

	rc = smb2_plain_req_init(SMB2_CHANGE_NOTIFY, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;
	/* See note 354 of MS-SMB2, 64K max */
	req->OutputBufferLength =
		cpu_to_le32(SMB2_MAX_BUFFER_SIZE - MAX_SMB2_HDR_SIZE);
	req->CompletionFilter = cpu_to_le32(completion_filter);
	if (watch_tree)
		req->Flags = cpu_to_le16(SMB2_WATCH_TREE);
	else
		req->Flags = 0;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	return 0;
}

int
SMB2_change_notify(const unsigned int xid, struct cifs_tcon *tcon,
		u64 persistent_fid, u64 volatile_fid, bool watch_tree,
		u32 completion_filter, u32 max_out_data_len, char **out_data,
		u32 *plen /* returned data len */)
{
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	struct smb_rqst rqst;
	struct smb2_change_notify_rsp *smb_rsp;
	struct kvec iov[1];
	struct kvec rsp_iov = {NULL, 0};
	int resp_buftype = CIFS_NO_BUFFER;
	int flags = 0;
	int rc = 0;

	cifs_dbg(FYI, "change notify\n");
	if (!ses || !server)
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	if (plen)
		*plen = 0;

	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = SMB2_notify_init(xid, &rqst, tcon, server,
			      persistent_fid, volatile_fid,
			      completion_filter, watch_tree);
	if (rc)
		goto cnotify_exit;

	trace_smb3_notify_enter(xid, persistent_fid, tcon->tid, ses->Suid,
				(u8)watch_tree, completion_filter);
	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);

	if (rc != 0) {
		cifs_stats_fail_inc(tcon, SMB2_CHANGE_NOTIFY_HE);
		trace_smb3_notify_err(xid, persistent_fid, tcon->tid, ses->Suid,
				(u8)watch_tree, completion_filter, rc);
	} else {
		trace_smb3_notify_done(xid, persistent_fid, tcon->tid,
			ses->Suid, (u8)watch_tree, completion_filter);
		/* validate that notify information is plausible */
		if ((rsp_iov.iov_base == NULL) ||
		    (rsp_iov.iov_len < sizeof(struct smb2_change_notify_rsp)))
			goto cnotify_exit;

		smb_rsp = (struct smb2_change_notify_rsp *)rsp_iov.iov_base;

		smb2_validate_iov(le16_to_cpu(smb_rsp->OutputBufferOffset),
				le32_to_cpu(smb_rsp->OutputBufferLength), &rsp_iov,
				sizeof(struct file_notify_information));

		*out_data = kmemdup((char *)smb_rsp + le16_to_cpu(smb_rsp->OutputBufferOffset),
				le32_to_cpu(smb_rsp->OutputBufferLength), GFP_KERNEL);
		if (*out_data == NULL) {
			rc = -ENOMEM;
			goto cnotify_exit;
		} else
			*plen = le32_to_cpu(smb_rsp->OutputBufferLength);
	}

 cnotify_exit:
	if (rqst.rq_iov)
		cifs_small_buf_release(rqst.rq_iov[0].iov_base); /* request */
	free_rsp_buf(resp_buftype, rsp_iov.iov_base);
	return rc;
}



/*
 * This is a no-op for now. We're not really interested in the reply, but
 * rather in the fact that the server sent one and that server->lstrp
 * gets updated.
 *
 * FIXME: maybe we should consider checking that the reply matches request?
 */
static void
smb2_echo_callback(struct mid_q_entry *mid)
{
	struct TCP_Server_Info *server = mid->callback_data;
	struct smb2_echo_rsp *rsp = (struct smb2_echo_rsp *)mid->resp_buf;
	struct cifs_credits credits = { .value = 0, .instance = 0 };

	if (mid->mid_state == MID_RESPONSE_RECEIVED
	    || mid->mid_state == MID_RESPONSE_MALFORMED) {
		credits.value = le16_to_cpu(rsp->hdr.CreditRequest);
		credits.instance = server->reconnect_instance;
	}

	release_mid(mid);
	add_credits(server, &credits, CIFS_ECHO_OP);
}

void smb2_reconnect_server(struct work_struct *work)
{
	struct TCP_Server_Info *server = container_of(work,
					struct TCP_Server_Info, reconnect.work);
	struct TCP_Server_Info *pserver;
	struct cifs_ses *ses, *ses2;
	struct cifs_tcon *tcon, *tcon2;
	struct list_head tmp_list, tmp_ses_list;
	bool tcon_exist = false, ses_exist = false;
	bool tcon_selected = false;
	int rc;
	bool resched = false;

	/* If server is a channel, select the primary channel */
	pserver = CIFS_SERVER_IS_CHAN(server) ? server->primary_server : server;

	/* Prevent simultaneous reconnects that can corrupt tcon->rlist list */
	mutex_lock(&pserver->reconnect_mutex);

	INIT_LIST_HEAD(&tmp_list);
	INIT_LIST_HEAD(&tmp_ses_list);
	cifs_dbg(FYI, "Reconnecting tcons and channels\n");

	spin_lock(&cifs_tcp_ses_lock);
	list_for_each_entry(ses, &pserver->smb_ses_list, smb_ses_list) {

		tcon_selected = false;

		list_for_each_entry(tcon, &ses->tcon_list, tcon_list) {
			if (tcon->need_reconnect || tcon->need_reopen_files) {
				tcon->tc_count++;
				list_add_tail(&tcon->rlist, &tmp_list);
				tcon_selected = tcon_exist = true;
			}
		}
		/*
		 * IPC has the same lifetime as its session and uses its
		 * refcount.
		 */
		if (ses->tcon_ipc && ses->tcon_ipc->need_reconnect) {
			list_add_tail(&ses->tcon_ipc->rlist, &tmp_list);
			tcon_selected = tcon_exist = true;
			ses->ses_count++;
		}
		/*
		 * handle the case where channel needs to reconnect
		 * binding session, but tcon is healthy (some other channel
		 * is active)
		 */
		spin_lock(&ses->chan_lock);
		if (!tcon_selected && cifs_chan_needs_reconnect(ses, server)) {
			list_add_tail(&ses->rlist, &tmp_ses_list);
			ses_exist = true;
			ses->ses_count++;
		}
		spin_unlock(&ses->chan_lock);
	}
	/*
	 * Get the reference to server struct to be sure that the last call of
	 * cifs_put_tcon() in the loop below won't release the server pointer.
	 */
	if (tcon_exist || ses_exist)
		server->srv_count++;

	spin_unlock(&cifs_tcp_ses_lock);

	list_for_each_entry_safe(tcon, tcon2, &tmp_list, rlist) {
		rc = smb2_reconnect(SMB2_INTERNAL_CMD, tcon, server);
		if (!rc)
			cifs_reopen_persistent_handles(tcon);
		else
			resched = true;
		list_del_init(&tcon->rlist);
		if (tcon->ipc)
			cifs_put_smb_ses(tcon->ses);
		else
			cifs_put_tcon(tcon);
	}

	if (!ses_exist)
		goto done;

	/* allocate a dummy tcon struct used for reconnect */
	tcon = tconInfoAlloc();
	if (!tcon) {
		resched = true;
		list_for_each_entry_safe(ses, ses2, &tmp_ses_list, rlist) {
			list_del_init(&ses->rlist);
			cifs_put_smb_ses(ses);
		}
		goto done;
	}

	tcon->status = TID_GOOD;
	tcon->retry = false;
	tcon->need_reconnect = false;

	/* now reconnect sessions for necessary channels */
	list_for_each_entry_safe(ses, ses2, &tmp_ses_list, rlist) {
		tcon->ses = ses;
		rc = smb2_reconnect(SMB2_INTERNAL_CMD, tcon, server);
		if (rc)
			resched = true;
		list_del_init(&ses->rlist);
		cifs_put_smb_ses(ses);
	}
	tconInfoFree(tcon);

done:
	cifs_dbg(FYI, "Reconnecting tcons and channels finished\n");
	if (resched)
		queue_delayed_work(cifsiod_wq, &server->reconnect, 2 * HZ);
	mutex_unlock(&pserver->reconnect_mutex);

	/* now we can safely release srv struct */
	if (tcon_exist || ses_exist)
		cifs_put_tcp_session(server, 1);
}

int
SMB2_echo(struct TCP_Server_Info *server)
{
	struct smb2_echo_req *req;
	int rc = 0;
	struct kvec iov[1];
	struct smb_rqst rqst = { .rq_iov = iov,
				 .rq_nvec = 1 };
	unsigned int total_len;

	cifs_dbg(FYI, "In echo request for conn_id %lld\n", server->conn_id);

	spin_lock(&server->srv_lock);
	if (server->ops->need_neg &&
	    server->ops->need_neg(server)) {
		spin_unlock(&server->srv_lock);
		/* No need to send echo on newly established connections */
		mod_delayed_work(cifsiod_wq, &server->reconnect, 0);
		return rc;
	}
	spin_unlock(&server->srv_lock);

	rc = smb2_plain_req_init(SMB2_ECHO, NULL, server,
				 (void **)&req, &total_len);
	if (rc)
		return rc;

	req->hdr.CreditRequest = cpu_to_le16(1);

	iov[0].iov_len = total_len;
	iov[0].iov_base = (char *)req;

	rc = cifs_call_async(server, &rqst, NULL, smb2_echo_callback, NULL,
			     server, CIFS_ECHO_OP, NULL);
	if (rc)
		cifs_dbg(FYI, "Echo request failed: %d\n", rc);

	cifs_small_buf_release(req);
	return rc;
}

void
SMB2_flush_free(struct smb_rqst *rqst)
{
	if (rqst && rqst->rq_iov)
		cifs_small_buf_release(rqst->rq_iov[0].iov_base); /* request */
}

int
SMB2_flush_init(const unsigned int xid, struct smb_rqst *rqst,
		struct cifs_tcon *tcon, struct TCP_Server_Info *server,
		u64 persistent_fid, u64 volatile_fid)
{
	struct smb2_flush_req *req;
	struct kvec *iov = rqst->rq_iov;
	unsigned int total_len;
	int rc;

	rc = smb2_plain_req_init(SMB2_FLUSH, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	return 0;
}

int
SMB2_flush(const unsigned int xid, struct cifs_tcon *tcon, u64 persistent_fid,
	   u64 volatile_fid)
{
	struct cifs_ses *ses = tcon->ses;
	struct smb_rqst rqst;
	struct kvec iov[1];
	struct kvec rsp_iov = {NULL, 0};
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	int resp_buftype = CIFS_NO_BUFFER;
	int flags = 0;
	int rc = 0;

	cifs_dbg(FYI, "flush\n");
	if (!ses || !(ses->server))
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = SMB2_flush_init(xid, &rqst, tcon, server,
			     persistent_fid, volatile_fid);
	if (rc)
		goto flush_exit;

	trace_smb3_flush_enter(xid, persistent_fid, tcon->tid, ses->Suid);
	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);

	if (rc != 0) {
		cifs_stats_fail_inc(tcon, SMB2_FLUSH_HE);
		trace_smb3_flush_err(xid, persistent_fid, tcon->tid, ses->Suid,
				     rc);
	} else
		trace_smb3_flush_done(xid, persistent_fid, tcon->tid,
				      ses->Suid);

 flush_exit:
	SMB2_flush_free(&rqst);
	free_rsp_buf(resp_buftype, rsp_iov.iov_base);
	return rc;
}

#ifdef CONFIG_CIFS_SMB_DIRECT
static inline bool smb3_use_rdma_offload(struct cifs_io_parms *io_parms)
{
	struct TCP_Server_Info *server = io_parms->server;
	struct cifs_tcon *tcon = io_parms->tcon;

	/* we can only offload if we're connected */
	if (!server || !tcon)
		return false;

	/* we can only offload on an rdma connection */
	if (!server->rdma || !server->smbd_conn)
		return false;

	/* we don't support signed offload yet */
	if (server->sign)
		return false;

	/* we don't support encrypted offload yet */
	if (smb3_encryption_required(tcon))
		return false;

	/* offload also has its overhead, so only do it if desired */
	if (io_parms->length < server->smbd_conn->rdma_readwrite_threshold)
		return false;

	return true;
}
#endif /* CONFIG_CIFS_SMB_DIRECT */

/*
 * To form a chain of read requests, any read requests after the first should
 * have the end_of_chain boolean set to true.
 */
static int
smb2_new_read_req(void **buf, unsigned int *total_len,
	struct cifs_io_parms *io_parms, struct cifs_readdata *rdata,
	unsigned int remaining_bytes, int request_type)
{
	int rc = -EACCES;
	struct smb2_read_req *req = NULL;
	struct smb2_hdr *shdr;
	struct TCP_Server_Info *server = io_parms->server;

	rc = smb2_plain_req_init(SMB2_READ, io_parms->tcon, server,
				 (void **) &req, total_len);
	if (rc)
		return rc;

	if (server == NULL)
		return -ECONNABORTED;

	shdr = &req->hdr;
	shdr->Id.SyncId.ProcessId = cpu_to_le32(io_parms->pid);

	req->PersistentFileId = io_parms->persistent_fid;
	req->VolatileFileId = io_parms->volatile_fid;
	req->ReadChannelInfoOffset = 0; /* reserved */
	req->ReadChannelInfoLength = 0; /* reserved */
	req->Channel = 0; /* reserved */
	req->MinimumCount = 0;
	req->Length = cpu_to_le32(io_parms->length);
	req->Offset = cpu_to_le64(io_parms->offset);

	trace_smb3_read_enter(0 /* xid */,
			io_parms->persistent_fid,
			io_parms->tcon->tid, io_parms->tcon->ses->Suid,
			io_parms->offset, io_parms->length);
#ifdef CONFIG_CIFS_SMB_DIRECT
	/*
	 * If we want to do a RDMA write, fill in and append
	 * smbd_buffer_descriptor_v1 to the end of read request
	 */
	if (smb3_use_rdma_offload(io_parms)) {
		struct smbd_buffer_descriptor_v1 *v1;
		bool need_invalidate = server->dialect == SMB30_PROT_ID;

		rdata->mr = smbd_register_mr(
				server->smbd_conn, rdata->pages,
				rdata->nr_pages, rdata->page_offset,
				rdata->tailsz, true, need_invalidate);
		if (!rdata->mr)
			return -EAGAIN;

		req->Channel = SMB2_CHANNEL_RDMA_V1_INVALIDATE;
		if (need_invalidate)
			req->Channel = SMB2_CHANNEL_RDMA_V1;
		req->ReadChannelInfoOffset =
			cpu_to_le16(offsetof(struct smb2_read_req, Buffer));
		req->ReadChannelInfoLength =
			cpu_to_le16(sizeof(struct smbd_buffer_descriptor_v1));
		v1 = (struct smbd_buffer_descriptor_v1 *) &req->Buffer[0];
		v1->offset = cpu_to_le64(rdata->mr->mr->iova);
		v1->token = cpu_to_le32(rdata->mr->mr->rkey);
		v1->length = cpu_to_le32(rdata->mr->mr->length);

		*total_len += sizeof(*v1) - 1;
	}
#endif
	if (request_type & CHAINED_REQUEST) {
		if (!(request_type & END_OF_CHAIN)) {
			/* next 8-byte aligned request */
			*total_len = ALIGN(*total_len, 8);
			shdr->NextCommand = cpu_to_le32(*total_len);
		} else /* END_OF_CHAIN */
			shdr->NextCommand = 0;
		if (request_type & RELATED_REQUEST) {
			shdr->Flags |= SMB2_FLAGS_RELATED_OPERATIONS;
			/*
			 * Related requests use info from previous read request
			 * in chain.
			 */
			shdr->SessionId = cpu_to_le64(0xFFFFFFFFFFFFFFFF);
			shdr->Id.SyncId.TreeId = cpu_to_le32(0xFFFFFFFF);
			req->PersistentFileId = (u64)-1;
			req->VolatileFileId = (u64)-1;
		}
	}
	if (remaining_bytes > io_parms->length)
		req->RemainingBytes = cpu_to_le32(remaining_bytes);
	else
		req->RemainingBytes = 0;

	*buf = req;
	return rc;
}

static void
smb2_readv_callback(struct mid_q_entry *mid)
{
	struct cifs_readdata *rdata = mid->callback_data;
	struct cifs_tcon *tcon = tlink_tcon(rdata->cfile->tlink);
	struct TCP_Server_Info *server = rdata->server;
	struct smb2_hdr *shdr =
				(struct smb2_hdr *)rdata->iov[0].iov_base;
	struct cifs_credits credits = { .value = 0, .instance = 0 };
	struct smb_rqst rqst = { .rq_iov = &rdata->iov[1],
				 .rq_nvec = 1, };

	if (rdata->got_bytes) {
		rqst.rq_pages = rdata->pages;
		rqst.rq_offset = rdata->page_offset;
		rqst.rq_npages = rdata->nr_pages;
		rqst.rq_pagesz = rdata->pagesz;
		rqst.rq_tailsz = rdata->tailsz;
	}

	WARN_ONCE(rdata->server != mid->server,
		  "rdata server %p != mid server %p",
		  rdata->server, mid->server);

	cifs_dbg(FYI, "%s: mid=%llu state=%d result=%d bytes=%u\n",
		 __func__, mid->mid, mid->mid_state, rdata->result,
		 rdata->bytes);

	switch (mid->mid_state) {
	case MID_RESPONSE_RECEIVED:
		credits.value = le16_to_cpu(shdr->CreditRequest);
		credits.instance = server->reconnect_instance;
		/* result already set, check signature */
		if (server->sign && !mid->decrypted) {
			int rc;

			rc = smb2_verify_signature(&rqst, server);
			if (rc)
				cifs_tcon_dbg(VFS, "SMB signature verification returned error = %d\n",
					 rc);
		}
		/* FIXME: should this be counted toward the initiating task? */
		task_io_account_read(rdata->got_bytes);
		cifs_stats_bytes_read(tcon, rdata->got_bytes);
		break;
	case MID_REQUEST_SUBMITTED:
	case MID_RETRY_NEEDED:
		rdata->result = -EAGAIN;
		if (server->sign && rdata->got_bytes)
			/* reset bytes number since we can not check a sign */
			rdata->got_bytes = 0;
		/* FIXME: should this be counted toward the initiating task? */
		task_io_account_read(rdata->got_bytes);
		cifs_stats_bytes_read(tcon, rdata->got_bytes);
		break;
	case MID_RESPONSE_MALFORMED:
		credits.value = le16_to_cpu(shdr->CreditRequest);
		credits.instance = server->reconnect_instance;
		fallthrough;
	default:
		rdata->result = -EIO;
	}
#ifdef CONFIG_CIFS_SMB_DIRECT
	/*
	 * If this rdata has a memmory registered, the MR can be freed
	 * MR needs to be freed as soon as I/O finishes to prevent deadlock
	 * because they have limited number and are used for future I/Os
	 */
	if (rdata->mr) {
		smbd_deregister_mr(rdata->mr);
		rdata->mr = NULL;
	}
#endif
	if (rdata->result && rdata->result != -ENODATA) {
		cifs_stats_fail_inc(tcon, SMB2_READ_HE);
		trace_smb3_read_err(0 /* xid */,
				    rdata->cfile->fid.persistent_fid,
				    tcon->tid, tcon->ses->Suid, rdata->offset,
				    rdata->bytes, rdata->result);
	} else
		trace_smb3_read_done(0 /* xid */,
				     rdata->cfile->fid.persistent_fid,
				     tcon->tid, tcon->ses->Suid,
				     rdata->offset, rdata->got_bytes);

	queue_work(cifsiod_wq, &rdata->work);
	release_mid(mid);
	add_credits(server, &credits, 0);
}

/* smb2_async_readv - send an async read, and set up mid to handle result */
int
smb2_async_readv(struct cifs_readdata *rdata)
{
	int rc, flags = 0;
	char *buf;
	struct smb2_hdr *shdr;
	struct cifs_io_parms io_parms;
	struct smb_rqst rqst = { .rq_iov = rdata->iov,
				 .rq_nvec = 1 };
	struct TCP_Server_Info *server;
	struct cifs_tcon *tcon = tlink_tcon(rdata->cfile->tlink);
	unsigned int total_len;

	cifs_dbg(FYI, "%s: offset=%llu bytes=%u\n",
		 __func__, rdata->offset, rdata->bytes);

	if (!rdata->server)
		rdata->server = cifs_pick_channel(tcon->ses);

	io_parms.tcon = tlink_tcon(rdata->cfile->tlink);
	io_parms.server = server = rdata->server;
	io_parms.offset = rdata->offset;
	io_parms.length = rdata->bytes;
	io_parms.persistent_fid = rdata->cfile->fid.persistent_fid;
	io_parms.volatile_fid = rdata->cfile->fid.volatile_fid;
	io_parms.pid = rdata->pid;

	rc = smb2_new_read_req(
		(void **) &buf, &total_len, &io_parms, rdata, 0, 0);
	if (rc)
		return rc;

	if (smb3_encryption_required(io_parms.tcon))
		flags |= CIFS_TRANSFORM_REQ;

	rdata->iov[0].iov_base = buf;
	rdata->iov[0].iov_len = total_len;

	shdr = (struct smb2_hdr *)buf;

	if (rdata->credits.value > 0) {
		shdr->CreditCharge = cpu_to_le16(DIV_ROUND_UP(rdata->bytes,
						SMB2_MAX_BUFFER_SIZE));
		shdr->CreditRequest = cpu_to_le16(le16_to_cpu(shdr->CreditCharge) + 8);

		rc = adjust_credits(server, &rdata->credits, rdata->bytes);
		if (rc)
			goto async_readv_out;

		flags |= CIFS_HAS_CREDITS;
	}

	kref_get(&rdata->refcount);
	rc = cifs_call_async(server, &rqst,
			     cifs_readv_receive, smb2_readv_callback,
			     smb3_handle_read_data, rdata, flags,
			     &rdata->credits);
	if (rc) {
		kref_put(&rdata->refcount, cifs_readdata_release);
		cifs_stats_fail_inc(io_parms.tcon, SMB2_READ_HE);
		trace_smb3_read_err(0 /* xid */, io_parms.persistent_fid,
				    io_parms.tcon->tid,
				    io_parms.tcon->ses->Suid,
				    io_parms.offset, io_parms.length, rc);
	}

async_readv_out:
	cifs_small_buf_release(buf);
	return rc;
}

int
SMB2_read(const unsigned int xid, struct cifs_io_parms *io_parms,
	  unsigned int *nbytes, char **buf, int *buf_type)
{
	struct smb_rqst rqst;
	int resp_buftype, rc;
	struct smb2_read_req *req = NULL;
	struct smb2_read_rsp *rsp = NULL;
	struct kvec iov[1];
	struct kvec rsp_iov;
	unsigned int total_len;
	int flags = CIFS_LOG_ERROR;
	struct cifs_ses *ses = io_parms->tcon->ses;

	if (!io_parms->server)
		io_parms->server = cifs_pick_channel(io_parms->tcon->ses);

	*nbytes = 0;
	rc = smb2_new_read_req((void **)&req, &total_len, io_parms, NULL, 0, 0);
	if (rc)
		return rc;

	if (smb3_encryption_required(io_parms->tcon))
		flags |= CIFS_TRANSFORM_REQ;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, io_parms->server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	rsp = (struct smb2_read_rsp *)rsp_iov.iov_base;

	if (rc) {
		if (rc != -ENODATA) {
			cifs_stats_fail_inc(io_parms->tcon, SMB2_READ_HE);
			cifs_dbg(VFS, "Send error in read = %d\n", rc);
			trace_smb3_read_err(xid,
					    req->PersistentFileId,
					    io_parms->tcon->tid, ses->Suid,
					    io_parms->offset, io_parms->length,
					    rc);
		} else
			trace_smb3_read_done(xid, req->PersistentFileId, io_parms->tcon->tid,
					     ses->Suid, io_parms->offset, 0);
		free_rsp_buf(resp_buftype, rsp_iov.iov_base);
		cifs_small_buf_release(req);
		return rc == -ENODATA ? 0 : rc;
	} else
		trace_smb3_read_done(xid,
				    req->PersistentFileId,
				    io_parms->tcon->tid, ses->Suid,
				    io_parms->offset, io_parms->length);

	cifs_small_buf_release(req);

	*nbytes = le32_to_cpu(rsp->DataLength);
	if ((*nbytes > CIFS_MAX_MSGSIZE) ||
	    (*nbytes > io_parms->length)) {
		cifs_dbg(FYI, "bad length %d for count %d\n",
			 *nbytes, io_parms->length);
		rc = -EIO;
		*nbytes = 0;
	}

	if (*buf) {
		memcpy(*buf, (char *)rsp + rsp->DataOffset, *nbytes);
		free_rsp_buf(resp_buftype, rsp_iov.iov_base);
	} else if (resp_buftype != CIFS_NO_BUFFER) {
		*buf = rsp_iov.iov_base;
		if (resp_buftype == CIFS_SMALL_BUFFER)
			*buf_type = CIFS_SMALL_BUFFER;
		else if (resp_buftype == CIFS_LARGE_BUFFER)
			*buf_type = CIFS_LARGE_BUFFER;
	}
	return rc;
}

/*
 * Check the mid_state and signature on received buffer (if any), and queue the
 * workqueue completion task.
 */
static void
smb2_writev_callback(struct mid_q_entry *mid)
{
	struct cifs_writedata *wdata = mid->callback_data;
	struct cifs_tcon *tcon = tlink_tcon(wdata->cfile->tlink);
	struct TCP_Server_Info *server = wdata->server;
	unsigned int written;
	struct smb2_write_rsp *rsp = (struct smb2_write_rsp *)mid->resp_buf;
	struct cifs_credits credits = { .value = 0, .instance = 0 };

	WARN_ONCE(wdata->server != mid->server,
		  "wdata server %p != mid server %p",
		  wdata->server, mid->server);

	switch (mid->mid_state) {
	case MID_RESPONSE_RECEIVED:
		credits.value = le16_to_cpu(rsp->hdr.CreditRequest);
		credits.instance = server->reconnect_instance;
		wdata->result = smb2_check_receive(mid, server, 0);
		if (wdata->result != 0)
			break;

		written = le32_to_cpu(rsp->DataLength);
		/*
		 * Mask off high 16 bits when bytes written as returned
		 * by the server is greater than bytes requested by the
		 * client. OS/2 servers are known to set incorrect
		 * CountHigh values.
		 */
		if (written > wdata->bytes)
			written &= 0xFFFF;

		if (written < wdata->bytes)
			wdata->result = -ENOSPC;
		else
			wdata->bytes = written;
		break;
	case MID_REQUEST_SUBMITTED:
	case MID_RETRY_NEEDED:
		wdata->result = -EAGAIN;
		break;
	case MID_RESPONSE_MALFORMED:
		credits.value = le16_to_cpu(rsp->hdr.CreditRequest);
		credits.instance = server->reconnect_instance;
		fallthrough;
	default:
		wdata->result = -EIO;
		break;
	}
#ifdef CONFIG_CIFS_SMB_DIRECT
	/*
	 * If this wdata has a memory registered, the MR can be freed
	 * The number of MRs available is limited, it's important to recover
	 * used MR as soon as I/O is finished. Hold MR longer in the later
	 * I/O process can possibly result in I/O deadlock due to lack of MR
	 * to send request on I/O retry
	 */
	if (wdata->mr) {
		smbd_deregister_mr(wdata->mr);
		wdata->mr = NULL;
	}
#endif
	if (wdata->result) {
		cifs_stats_fail_inc(tcon, SMB2_WRITE_HE);
		trace_smb3_write_err(0 /* no xid */,
				     wdata->cfile->fid.persistent_fid,
				     tcon->tid, tcon->ses->Suid, wdata->offset,
				     wdata->bytes, wdata->result);
		if (wdata->result == -ENOSPC)
			pr_warn_once("Out of space writing to %s\n",
				     tcon->tree_name);
	} else
		trace_smb3_write_done(0 /* no xid */,
				      wdata->cfile->fid.persistent_fid,
				      tcon->tid, tcon->ses->Suid,
				      wdata->offset, wdata->bytes);

	queue_work(cifsiod_wq, &wdata->work);
	release_mid(mid);
	add_credits(server, &credits, 0);
}

/* smb2_async_writev - send an async write, and set up mid to handle result */
int
smb2_async_writev(struct cifs_writedata *wdata,
		  void (*release)(struct kref *kref))
{
	int rc = -EACCES, flags = 0;
	struct smb2_write_req *req = NULL;
	struct smb2_hdr *shdr;
	struct cifs_tcon *tcon = tlink_tcon(wdata->cfile->tlink);
	struct TCP_Server_Info *server = wdata->server;
	struct kvec iov[1];
	struct smb_rqst rqst = { };
	unsigned int total_len;
	struct cifs_io_parms _io_parms;
	struct cifs_io_parms *io_parms = NULL;

	if (!wdata->server)
		server = wdata->server = cifs_pick_channel(tcon->ses);

	/*
	 * in future we may get cifs_io_parms passed in from the caller,
	 * but for now we construct it here...
	 */
	_io_parms = (struct cifs_io_parms) {
		.tcon = tcon,
		.server = server,
		.offset = wdata->offset,
		.length = wdata->bytes,
		.persistent_fid = wdata->cfile->fid.persistent_fid,
		.volatile_fid = wdata->cfile->fid.volatile_fid,
		.pid = wdata->pid,
	};
	io_parms = &_io_parms;

	rc = smb2_plain_req_init(SMB2_WRITE, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	shdr = (struct smb2_hdr *)req;
	shdr->Id.SyncId.ProcessId = cpu_to_le32(io_parms->pid);

	req->PersistentFileId = io_parms->persistent_fid;
	req->VolatileFileId = io_parms->volatile_fid;
	req->WriteChannelInfoOffset = 0;
	req->WriteChannelInfoLength = 0;
	req->Channel = 0;
	req->Offset = cpu_to_le64(io_parms->offset);
	req->DataOffset = cpu_to_le16(
				offsetof(struct smb2_write_req, Buffer));
	req->RemainingBytes = 0;

	trace_smb3_write_enter(0 /* xid */,
			       io_parms->persistent_fid,
			       io_parms->tcon->tid,
			       io_parms->tcon->ses->Suid,
			       io_parms->offset,
			       io_parms->length);

#ifdef CONFIG_CIFS_SMB_DIRECT
	/*
	 * If we want to do a server RDMA read, fill in and append
	 * smbd_buffer_descriptor_v1 to the end of write request
	 */
	if (smb3_use_rdma_offload(io_parms)) {
		struct smbd_buffer_descriptor_v1 *v1;
		bool need_invalidate = server->dialect == SMB30_PROT_ID;

		wdata->mr = smbd_register_mr(
				server->smbd_conn, wdata->pages,
				wdata->nr_pages, wdata->page_offset,
				wdata->tailsz, false, need_invalidate);
		if (!wdata->mr) {
			rc = -EAGAIN;
			goto async_writev_out;
		}
		req->Length = 0;
		req->DataOffset = 0;
		if (wdata->nr_pages > 1)
			req->RemainingBytes =
				cpu_to_le32(
					(wdata->nr_pages - 1) * wdata->pagesz -
					wdata->page_offset + wdata->tailsz
				);
		else
			req->RemainingBytes = cpu_to_le32(wdata->tailsz);
		req->Channel = SMB2_CHANNEL_RDMA_V1_INVALIDATE;
		if (need_invalidate)
			req->Channel = SMB2_CHANNEL_RDMA_V1;
		req->WriteChannelInfoOffset =
			cpu_to_le16(offsetof(struct smb2_write_req, Buffer));
		req->WriteChannelInfoLength =
			cpu_to_le16(sizeof(struct smbd_buffer_descriptor_v1));
		v1 = (struct smbd_buffer_descriptor_v1 *) &req->Buffer[0];
		v1->offset = cpu_to_le64(wdata->mr->mr->iova);
		v1->token = cpu_to_le32(wdata->mr->mr->rkey);
		v1->length = cpu_to_le32(wdata->mr->mr->length);
	}
#endif
	iov[0].iov_len = total_len - 1;
	iov[0].iov_base = (char *)req;

	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;
	rqst.rq_pages = wdata->pages;
	rqst.rq_offset = wdata->page_offset;
	rqst.rq_npages = wdata->nr_pages;
	rqst.rq_pagesz = wdata->pagesz;
	rqst.rq_tailsz = wdata->tailsz;
#ifdef CONFIG_CIFS_SMB_DIRECT
	if (wdata->mr) {
		iov[0].iov_len += sizeof(struct smbd_buffer_descriptor_v1);
		rqst.rq_npages = 0;
	}
#endif
	cifs_dbg(FYI, "async write at %llu %u bytes\n",
		 io_parms->offset, io_parms->length);

#ifdef CONFIG_CIFS_SMB_DIRECT
	/* For RDMA read, I/O size is in RemainingBytes not in Length */
	if (!wdata->mr)
		req->Length = cpu_to_le32(io_parms->length);
#else
	req->Length = cpu_to_le32(io_parms->length);
#endif

	if (wdata->credits.value > 0) {
		shdr->CreditCharge = cpu_to_le16(DIV_ROUND_UP(wdata->bytes,
						    SMB2_MAX_BUFFER_SIZE));
		shdr->CreditRequest = cpu_to_le16(le16_to_cpu(shdr->CreditCharge) + 8);

		rc = adjust_credits(server, &wdata->credits, io_parms->length);
		if (rc)
			goto async_writev_out;

		flags |= CIFS_HAS_CREDITS;
	}

	kref_get(&wdata->refcount);
	rc = cifs_call_async(server, &rqst, NULL, smb2_writev_callback, NULL,
			     wdata, flags, &wdata->credits);

	if (rc) {
		trace_smb3_write_err(0 /* no xid */,
				     io_parms->persistent_fid,
				     io_parms->tcon->tid,
				     io_parms->tcon->ses->Suid,
				     io_parms->offset,
				     io_parms->length,
				     rc);
		kref_put(&wdata->refcount, release);
		cifs_stats_fail_inc(tcon, SMB2_WRITE_HE);
	}

async_writev_out:
	cifs_small_buf_release(req);
	return rc;
}

/*
 * SMB2_write function gets iov pointer to kvec array with n_vec as a length.
 * The length field from io_parms must be at least 1 and indicates a number of
 * elements with data to write that begins with position 1 in iov array. All
 * data length is specified by count.
 */
int
SMB2_write(const unsigned int xid, struct cifs_io_parms *io_parms,
	   unsigned int *nbytes, struct kvec *iov, int n_vec)
{
	struct smb_rqst rqst;
	int rc = 0;
	struct smb2_write_req *req = NULL;
	struct smb2_write_rsp *rsp = NULL;
	int resp_buftype;
	struct kvec rsp_iov;
	int flags = 0;
	unsigned int total_len;
	struct TCP_Server_Info *server;

	*nbytes = 0;

	if (n_vec < 1)
		return rc;

	if (!io_parms->server)
		io_parms->server = cifs_pick_channel(io_parms->tcon->ses);
	server = io_parms->server;
	if (server == NULL)
		return -ECONNABORTED;

	rc = smb2_plain_req_init(SMB2_WRITE, io_parms->tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	if (smb3_encryption_required(io_parms->tcon))
		flags |= CIFS_TRANSFORM_REQ;

	req->hdr.Id.SyncId.ProcessId = cpu_to_le32(io_parms->pid);

	req->PersistentFileId = io_parms->persistent_fid;
	req->VolatileFileId = io_parms->volatile_fid;
	req->WriteChannelInfoOffset = 0;
	req->WriteChannelInfoLength = 0;
	req->Channel = 0;
	req->Length = cpu_to_le32(io_parms->length);
	req->Offset = cpu_to_le64(io_parms->offset);
	req->DataOffset = cpu_to_le16(
				offsetof(struct smb2_write_req, Buffer));
	req->RemainingBytes = 0;

	trace_smb3_write_enter(xid, io_parms->persistent_fid,
		io_parms->tcon->tid, io_parms->tcon->ses->Suid,
		io_parms->offset, io_parms->length);

	iov[0].iov_base = (char *)req;
	/* 1 for Buffer */
	iov[0].iov_len = total_len - 1;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = n_vec + 1;

	rc = cifs_send_recv(xid, io_parms->tcon->ses, server,
			    &rqst,
			    &resp_buftype, flags, &rsp_iov);
	rsp = (struct smb2_write_rsp *)rsp_iov.iov_base;

	if (rc) {
		trace_smb3_write_err(xid,
				     req->PersistentFileId,
				     io_parms->tcon->tid,
				     io_parms->tcon->ses->Suid,
				     io_parms->offset, io_parms->length, rc);
		cifs_stats_fail_inc(io_parms->tcon, SMB2_WRITE_HE);
		cifs_dbg(VFS, "Send error in write = %d\n", rc);
	} else {
		*nbytes = le32_to_cpu(rsp->DataLength);
		trace_smb3_write_done(xid,
				      req->PersistentFileId,
				      io_parms->tcon->tid,
				      io_parms->tcon->ses->Suid,
				      io_parms->offset, *nbytes);
	}

	cifs_small_buf_release(req);
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

int posix_info_sid_size(const void *beg, const void *end)
{
	size_t subauth;
	int total;

	if (beg + 1 > end)
		return -1;

	subauth = *(u8 *)(beg+1);
	if (subauth < 1 || subauth > 15)
		return -1;

	total = 1 + 1 + 6 + 4*subauth;
	if (beg + total > end)
		return -1;

	return total;
}

int posix_info_parse(const void *beg, const void *end,
		     struct smb2_posix_info_parsed *out)

{
	int total_len = 0;
	int owner_len, group_len;
	int name_len;
	const void *owner_sid;
	const void *group_sid;
	const void *name;

	/* if no end bound given, assume payload to be correct */
	if (!end) {
		const struct smb2_posix_info *p = beg;

		end = beg + le32_to_cpu(p->NextEntryOffset);
		/* last element will have a 0 offset, pick a sensible bound */
		if (end == beg)
			end += 0xFFFF;
	}

	/* check base buf */
	if (beg + sizeof(struct smb2_posix_info) > end)
		return -1;
	total_len = sizeof(struct smb2_posix_info);

	/* check owner sid */
	owner_sid = beg + total_len;
	owner_len = posix_info_sid_size(owner_sid, end);
	if (owner_len < 0)
		return -1;
	total_len += owner_len;

	/* check group sid */
	group_sid = beg + total_len;
	group_len = posix_info_sid_size(group_sid, end);
	if (group_len < 0)
		return -1;
	total_len += group_len;

	/* check name len */
	if (beg + total_len + 4 > end)
		return -1;
	name_len = le32_to_cpu(*(__le32 *)(beg + total_len));
	if (name_len < 1 || name_len > 0xFFFF)
		return -1;
	total_len += 4;

	/* check name */
	name = beg + total_len;
	if (name + name_len > end)
		return -1;
	total_len += name_len;

	if (out) {
		out->base = beg;
		out->size = total_len;
		out->name_len = name_len;
		out->name = name;
		memcpy(&out->owner, owner_sid, owner_len);
		memcpy(&out->group, group_sid, group_len);
	}
	return total_len;
}

static int posix_info_extra_size(const void *beg, const void *end)
{
	int len = posix_info_parse(beg, end, NULL);

	if (len < 0)
		return -1;
	return len - sizeof(struct smb2_posix_info);
}

static unsigned int
num_entries(int infotype, char *bufstart, char *end_of_buf, char **lastentry,
	    size_t size)
{
	int len;
	unsigned int entrycount = 0;
	unsigned int next_offset = 0;
	char *entryptr;
	FILE_DIRECTORY_INFO *dir_info;

	if (bufstart == NULL)
		return 0;

	entryptr = bufstart;

	while (1) {
		if (entryptr + next_offset < entryptr ||
		    entryptr + next_offset > end_of_buf ||
		    entryptr + next_offset + size > end_of_buf) {
			cifs_dbg(VFS, "malformed search entry would overflow\n");
			break;
		}

		entryptr = entryptr + next_offset;
		dir_info = (FILE_DIRECTORY_INFO *)entryptr;

		if (infotype == SMB_FIND_FILE_POSIX_INFO)
			len = posix_info_extra_size(entryptr, end_of_buf);
		else
			len = le32_to_cpu(dir_info->FileNameLength);

		if (len < 0 ||
		    entryptr + len < entryptr ||
		    entryptr + len > end_of_buf ||
		    entryptr + len + size > end_of_buf) {
			cifs_dbg(VFS, "directory entry name would overflow frame end of buf %p\n",
				 end_of_buf);
			break;
		}

		*lastentry = entryptr;
		entrycount++;

		next_offset = le32_to_cpu(dir_info->NextEntryOffset);
		if (!next_offset)
			break;
	}

	return entrycount;
}

/*
 * Readdir/FindFirst
 */
int SMB2_query_directory_init(const unsigned int xid,
			      struct cifs_tcon *tcon,
			      struct TCP_Server_Info *server,
			      struct smb_rqst *rqst,
			      u64 persistent_fid, u64 volatile_fid,
			      int index, int info_level)
{
	struct smb2_query_directory_req *req;
	unsigned char *bufptr;
	__le16 asteriks = cpu_to_le16('*');
	unsigned int output_size = CIFSMaxBufSize -
		MAX_SMB2_CREATE_RESPONSE_SIZE -
		MAX_SMB2_CLOSE_RESPONSE_SIZE;
	unsigned int total_len;
	struct kvec *iov = rqst->rq_iov;
	int len, rc;

	rc = smb2_plain_req_init(SMB2_QUERY_DIRECTORY, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	switch (info_level) {
	case SMB_FIND_FILE_DIRECTORY_INFO:
		req->FileInformationClass = FILE_DIRECTORY_INFORMATION;
		break;
	case SMB_FIND_FILE_ID_FULL_DIR_INFO:
		req->FileInformationClass = FILEID_FULL_DIRECTORY_INFORMATION;
		break;
	case SMB_FIND_FILE_POSIX_INFO:
		req->FileInformationClass = SMB_FIND_FILE_POSIX_INFO;
		break;
	default:
		cifs_tcon_dbg(VFS, "info level %u isn't supported\n",
			info_level);
		return -EINVAL;
	}

	req->FileIndex = cpu_to_le32(index);
	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;

	len = 0x2;
	bufptr = req->Buffer;
	memcpy(bufptr, &asteriks, len);

	req->FileNameOffset =
		cpu_to_le16(sizeof(struct smb2_query_directory_req) - 1);
	req->FileNameLength = cpu_to_le16(len);
	/*
	 * BB could be 30 bytes or so longer if we used SMB2 specific
	 * buffer lengths, but this is safe and close enough.
	 */
	output_size = min_t(unsigned int, output_size, server->maxBuf);
	output_size = min_t(unsigned int, output_size, 2 << 15);
	req->OutputBufferLength = cpu_to_le32(output_size);

	iov[0].iov_base = (char *)req;
	/* 1 for Buffer */
	iov[0].iov_len = total_len - 1;

	iov[1].iov_base = (char *)(req->Buffer);
	iov[1].iov_len = len;

	trace_smb3_query_dir_enter(xid, persistent_fid, tcon->tid,
			tcon->ses->Suid, index, output_size);

	return 0;
}

void SMB2_query_directory_free(struct smb_rqst *rqst)
{
	if (rqst && rqst->rq_iov) {
		cifs_small_buf_release(rqst->rq_iov[0].iov_base); /* request */
	}
}

int
smb2_parse_query_directory(struct cifs_tcon *tcon,
			   struct kvec *rsp_iov,
			   int resp_buftype,
			   struct cifs_search_info *srch_inf)
{
	struct smb2_query_directory_rsp *rsp;
	size_t info_buf_size;
	char *end_of_smb;
	int rc;

	rsp = (struct smb2_query_directory_rsp *)rsp_iov->iov_base;

	switch (srch_inf->info_level) {
	case SMB_FIND_FILE_DIRECTORY_INFO:
		info_buf_size = sizeof(FILE_DIRECTORY_INFO) - 1;
		break;
	case SMB_FIND_FILE_ID_FULL_DIR_INFO:
		info_buf_size = sizeof(SEARCH_ID_FULL_DIR_INFO) - 1;
		break;
	case SMB_FIND_FILE_POSIX_INFO:
		/* note that posix payload are variable size */
		info_buf_size = sizeof(struct smb2_posix_info);
		break;
	default:
		cifs_tcon_dbg(VFS, "info level %u isn't supported\n",
			 srch_inf->info_level);
		return -EINVAL;
	}

	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength), rsp_iov,
			       info_buf_size);
	if (rc) {
		cifs_tcon_dbg(VFS, "bad info payload");
		return rc;
	}

	srch_inf->unicode = true;

	if (srch_inf->ntwrk_buf_start) {
		if (srch_inf->smallBuf)
			cifs_small_buf_release(srch_inf->ntwrk_buf_start);
		else
			cifs_buf_release(srch_inf->ntwrk_buf_start);
	}
	srch_inf->ntwrk_buf_start = (char *)rsp;
	srch_inf->srch_entries_start = srch_inf->last_entry =
		(char *)rsp + le16_to_cpu(rsp->OutputBufferOffset);
	end_of_smb = rsp_iov->iov_len + (char *)rsp;

	srch_inf->entries_in_buffer = num_entries(
		srch_inf->info_level,
		srch_inf->srch_entries_start,
		end_of_smb,
		&srch_inf->last_entry,
		info_buf_size);

	srch_inf->index_of_last_entry += srch_inf->entries_in_buffer;
	cifs_dbg(FYI, "num entries %d last_index %lld srch start %p srch end %p\n",
		 srch_inf->entries_in_buffer, srch_inf->index_of_last_entry,
		 srch_inf->srch_entries_start, srch_inf->last_entry);
	if (resp_buftype == CIFS_LARGE_BUFFER)
		srch_inf->smallBuf = false;
	else if (resp_buftype == CIFS_SMALL_BUFFER)
		srch_inf->smallBuf = true;
	else
		cifs_tcon_dbg(VFS, "Invalid search buffer type\n");

	return 0;
}

int
SMB2_query_directory(const unsigned int xid, struct cifs_tcon *tcon,
		     u64 persistent_fid, u64 volatile_fid, int index,
		     struct cifs_search_info *srch_inf)
{
	struct smb_rqst rqst;
	struct kvec iov[SMB2_QUERY_DIRECTORY_IOV_SIZE];
	struct smb2_query_directory_rsp *rsp = NULL;
	int resp_buftype = CIFS_NO_BUFFER;
	struct kvec rsp_iov;
	int rc = 0;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	int flags = 0;

	if (!ses || !(ses->server))
		return -EIO;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	memset(&iov, 0, sizeof(iov));
	rqst.rq_iov = iov;
	rqst.rq_nvec = SMB2_QUERY_DIRECTORY_IOV_SIZE;

	rc = SMB2_query_directory_init(xid, tcon, server,
				       &rqst, persistent_fid,
				       volatile_fid, index,
				       srch_inf->info_level);
	if (rc)
		goto qdir_exit;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	rsp = (struct smb2_query_directory_rsp *)rsp_iov.iov_base;

	if (rc) {
		if (rc == -ENODATA &&
		    rsp->hdr.Status == STATUS_NO_MORE_FILES) {
			trace_smb3_query_dir_done(xid, persistent_fid,
				tcon->tid, tcon->ses->Suid, index, 0);
			srch_inf->endOfSearch = true;
			rc = 0;
		} else {
			trace_smb3_query_dir_err(xid, persistent_fid, tcon->tid,
				tcon->ses->Suid, index, 0, rc);
			cifs_stats_fail_inc(tcon, SMB2_QUERY_DIRECTORY_HE);
		}
		goto qdir_exit;
	}

	rc = smb2_parse_query_directory(tcon, &rsp_iov,	resp_buftype,
					srch_inf);
	if (rc) {
		trace_smb3_query_dir_err(xid, persistent_fid, tcon->tid,
			tcon->ses->Suid, index, 0, rc);
		goto qdir_exit;
	}
	resp_buftype = CIFS_NO_BUFFER;

	trace_smb3_query_dir_done(xid, persistent_fid, tcon->tid,
			tcon->ses->Suid, index, srch_inf->entries_in_buffer);

qdir_exit:
	SMB2_query_directory_free(&rqst);
	free_rsp_buf(resp_buftype, rsp);
	return rc;
}

int
SMB2_set_info_init(struct cifs_tcon *tcon, struct TCP_Server_Info *server,
		   struct smb_rqst *rqst,
		   u64 persistent_fid, u64 volatile_fid, u32 pid,
		   u8 info_class, u8 info_type, u32 additional_info,
		   void **data, unsigned int *size)
{
	struct smb2_set_info_req *req;
	struct kvec *iov = rqst->rq_iov;
	unsigned int i, total_len;
	int rc;

	rc = smb2_plain_req_init(SMB2_SET_INFO, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->hdr.Id.SyncId.ProcessId = cpu_to_le32(pid);
	req->InfoType = info_type;
	req->FileInfoClass = info_class;
	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;
	req->AdditionalInformation = cpu_to_le32(additional_info);

	req->BufferOffset =
			cpu_to_le16(sizeof(struct smb2_set_info_req) - 1);
	req->BufferLength = cpu_to_le32(*size);

	memcpy(req->Buffer, *data, *size);
	total_len += *size;

	iov[0].iov_base = (char *)req;
	/* 1 for Buffer */
	iov[0].iov_len = total_len - 1;

	for (i = 1; i < rqst->rq_nvec; i++) {
		le32_add_cpu(&req->BufferLength, size[i]);
		iov[i].iov_base = (char *)data[i];
		iov[i].iov_len = size[i];
	}

	return 0;
}

void
SMB2_set_info_free(struct smb_rqst *rqst)
{
	if (rqst && rqst->rq_iov)
		cifs_buf_release(rqst->rq_iov[0].iov_base); /* request */
}

static int
send_set_info(const unsigned int xid, struct cifs_tcon *tcon,
	       u64 persistent_fid, u64 volatile_fid, u32 pid, u8 info_class,
	       u8 info_type, u32 additional_info, unsigned int num,
		void **data, unsigned int *size)
{
	struct smb_rqst rqst;
	struct smb2_set_info_rsp *rsp = NULL;
	struct kvec *iov;
	struct kvec rsp_iov;
	int rc = 0;
	int resp_buftype;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	int flags = 0;

	if (!ses || !server)
		return -EIO;

	if (!num)
		return -EINVAL;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	iov = kmalloc_array(num, sizeof(struct kvec), GFP_KERNEL);
	if (!iov)
		return -ENOMEM;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = num;

	rc = SMB2_set_info_init(tcon, server,
				&rqst, persistent_fid, volatile_fid, pid,
				info_class, info_type, additional_info,
				data, size);
	if (rc) {
		kfree(iov);
		return rc;
	}


	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags,
			    &rsp_iov);
	SMB2_set_info_free(&rqst);
	rsp = (struct smb2_set_info_rsp *)rsp_iov.iov_base;

	if (rc != 0) {
		cifs_stats_fail_inc(tcon, SMB2_SET_INFO_HE);
		trace_smb3_set_info_err(xid, persistent_fid, tcon->tid,
				ses->Suid, info_class, (__u32)info_type, rc);
	}

	free_rsp_buf(resp_buftype, rsp);
	kfree(iov);
	return rc;
}

int
SMB2_set_eof(const unsigned int xid, struct cifs_tcon *tcon, u64 persistent_fid,
	     u64 volatile_fid, u32 pid, __le64 *eof)
{
	struct smb2_file_eof_info info;
	void *data;
	unsigned int size;

	info.EndOfFile = *eof;

	data = &info;
	size = sizeof(struct smb2_file_eof_info);

	trace_smb3_set_eof(xid, persistent_fid, tcon->tid, tcon->ses->Suid, le64_to_cpu(*eof));

	return send_set_info(xid, tcon, persistent_fid, volatile_fid,
			pid, FILE_END_OF_FILE_INFORMATION, SMB2_O_INFO_FILE,
			0, 1, &data, &size);
}

int
SMB2_set_acl(const unsigned int xid, struct cifs_tcon *tcon,
		u64 persistent_fid, u64 volatile_fid,
		struct cifs_ntsd *pnntsd, int pacllen, int aclflag)
{
	return send_set_info(xid, tcon, persistent_fid, volatile_fid,
			current->tgid, 0, SMB2_O_INFO_SECURITY, aclflag,
			1, (void **)&pnntsd, &pacllen);
}

int
SMB2_set_ea(const unsigned int xid, struct cifs_tcon *tcon,
	    u64 persistent_fid, u64 volatile_fid,
	    struct smb2_file_full_ea_info *buf, int len)
{
	return send_set_info(xid, tcon, persistent_fid, volatile_fid,
		current->tgid, FILE_FULL_EA_INFORMATION, SMB2_O_INFO_FILE,
		0, 1, (void **)&buf, &len);
}

int
SMB2_oplock_break(const unsigned int xid, struct cifs_tcon *tcon,
		  const u64 persistent_fid, const u64 volatile_fid,
		  __u8 oplock_level)
{
	struct smb_rqst rqst;
	int rc;
	struct smb2_oplock_break *req = NULL;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	int flags = CIFS_OBREAK_OP;
	unsigned int total_len;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int resp_buf_type;

	cifs_dbg(FYI, "SMB2_oplock_break\n");
	rc = smb2_plain_req_init(SMB2_OPLOCK_BREAK, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	req->VolatileFid = volatile_fid;
	req->PersistentFid = persistent_fid;
	req->OplockLevel = oplock_level;
	req->hdr.CreditRequest = cpu_to_le16(1);

	flags |= CIFS_NO_RSP_BUF;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buf_type, flags, &rsp_iov);
	cifs_small_buf_release(req);

	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_OPLOCK_BREAK_HE);
		cifs_dbg(FYI, "Send error in Oplock Break = %d\n", rc);
	}

	return rc;
}

void
smb2_copy_fs_info_to_kstatfs(struct smb2_fs_full_size_info *pfs_inf,
			     struct kstatfs *kst)
{
	kst->f_bsize = le32_to_cpu(pfs_inf->BytesPerSector) *
			  le32_to_cpu(pfs_inf->SectorsPerAllocationUnit);
	kst->f_blocks = le64_to_cpu(pfs_inf->TotalAllocationUnits);
	kst->f_bfree  = kst->f_bavail =
			le64_to_cpu(pfs_inf->CallerAvailableAllocationUnits);
	return;
}

static void
copy_posix_fs_info_to_kstatfs(FILE_SYSTEM_POSIX_INFO *response_data,
			struct kstatfs *kst)
{
	kst->f_bsize = le32_to_cpu(response_data->BlockSize);
	kst->f_blocks = le64_to_cpu(response_data->TotalBlocks);
	kst->f_bfree =  le64_to_cpu(response_data->BlocksAvail);
	if (response_data->UserBlocksAvail == cpu_to_le64(-1))
		kst->f_bavail = kst->f_bfree;
	else
		kst->f_bavail = le64_to_cpu(response_data->UserBlocksAvail);
	if (response_data->TotalFileNodes != cpu_to_le64(-1))
		kst->f_files = le64_to_cpu(response_data->TotalFileNodes);
	if (response_data->FreeFileNodes != cpu_to_le64(-1))
		kst->f_ffree = le64_to_cpu(response_data->FreeFileNodes);

	return;
}

static int
build_qfs_info_req(struct kvec *iov, struct cifs_tcon *tcon,
		   struct TCP_Server_Info *server,
		   int level, int outbuf_len, u64 persistent_fid,
		   u64 volatile_fid)
{
	int rc;
	struct smb2_query_info_req *req;
	unsigned int total_len;

	cifs_dbg(FYI, "Query FSInfo level %d\n", level);

	if ((tcon->ses == NULL) || server == NULL)
		return -EIO;

	rc = smb2_plain_req_init(SMB2_QUERY_INFO, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	req->InfoType = SMB2_O_INFO_FILESYSTEM;
	req->FileInfoClass = level;
	req->PersistentFileId = persistent_fid;
	req->VolatileFileId = volatile_fid;
	/* 1 for pad */
	req->InputBufferOffset =
			cpu_to_le16(sizeof(struct smb2_query_info_req) - 1);
	req->OutputBufferLength = cpu_to_le32(
		outbuf_len + sizeof(struct smb2_query_info_rsp) - 1);

	iov->iov_base = (char *)req;
	iov->iov_len = total_len;
	return 0;
}

int
SMB311_posix_qfs_info(const unsigned int xid, struct cifs_tcon *tcon,
	      u64 persistent_fid, u64 volatile_fid, struct kstatfs *fsdata)
{
	struct smb_rqst rqst;
	struct smb2_query_info_rsp *rsp = NULL;
	struct kvec iov;
	struct kvec rsp_iov;
	int rc = 0;
	int resp_buftype;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	FILE_SYSTEM_POSIX_INFO *info = NULL;
	int flags = 0;

	rc = build_qfs_info_req(&iov, tcon, server,
				FS_POSIX_INFORMATION,
				sizeof(FILE_SYSTEM_POSIX_INFO),
				persistent_fid, volatile_fid);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = &iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	cifs_small_buf_release(iov.iov_base);
	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_QUERY_INFO_HE);
		goto posix_qfsinf_exit;
	}
	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;

	info = (FILE_SYSTEM_POSIX_INFO *)(
		le16_to_cpu(rsp->OutputBufferOffset) + (char *)rsp);
	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength), &rsp_iov,
			       sizeof(FILE_SYSTEM_POSIX_INFO));
	if (!rc)
		copy_posix_fs_info_to_kstatfs(info, fsdata);

posix_qfsinf_exit:
	free_rsp_buf(resp_buftype, rsp_iov.iov_base);
	return rc;
}

int
SMB2_QFS_info(const unsigned int xid, struct cifs_tcon *tcon,
	      u64 persistent_fid, u64 volatile_fid, struct kstatfs *fsdata)
{
	struct smb_rqst rqst;
	struct smb2_query_info_rsp *rsp = NULL;
	struct kvec iov;
	struct kvec rsp_iov;
	int rc = 0;
	int resp_buftype;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	struct smb2_fs_full_size_info *info = NULL;
	int flags = 0;

	rc = build_qfs_info_req(&iov, tcon, server,
				FS_FULL_SIZE_INFORMATION,
				sizeof(struct smb2_fs_full_size_info),
				persistent_fid, volatile_fid);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = &iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	cifs_small_buf_release(iov.iov_base);
	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_QUERY_INFO_HE);
		goto qfsinf_exit;
	}
	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;

	info = (struct smb2_fs_full_size_info *)(
		le16_to_cpu(rsp->OutputBufferOffset) + (char *)rsp);
	rc = smb2_validate_iov(le16_to_cpu(rsp->OutputBufferOffset),
			       le32_to_cpu(rsp->OutputBufferLength), &rsp_iov,
			       sizeof(struct smb2_fs_full_size_info));
	if (!rc)
		smb2_copy_fs_info_to_kstatfs(info, fsdata);

qfsinf_exit:
	free_rsp_buf(resp_buftype, rsp_iov.iov_base);
	return rc;
}

int
SMB2_QFS_attr(const unsigned int xid, struct cifs_tcon *tcon,
	      u64 persistent_fid, u64 volatile_fid, int level)
{
	struct smb_rqst rqst;
	struct smb2_query_info_rsp *rsp = NULL;
	struct kvec iov;
	struct kvec rsp_iov;
	int rc = 0;
	int resp_buftype, max_len, min_len;
	struct cifs_ses *ses = tcon->ses;
	struct TCP_Server_Info *server = cifs_pick_channel(ses);
	unsigned int rsp_len, offset;
	int flags = 0;

	if (level == FS_DEVICE_INFORMATION) {
		max_len = sizeof(FILE_SYSTEM_DEVICE_INFO);
		min_len = sizeof(FILE_SYSTEM_DEVICE_INFO);
	} else if (level == FS_ATTRIBUTE_INFORMATION) {
		max_len = sizeof(FILE_SYSTEM_ATTRIBUTE_INFO);
		min_len = MIN_FS_ATTR_INFO_SIZE;
	} else if (level == FS_SECTOR_SIZE_INFORMATION) {
		max_len = sizeof(struct smb3_fs_ss_info);
		min_len = sizeof(struct smb3_fs_ss_info);
	} else if (level == FS_VOLUME_INFORMATION) {
		max_len = sizeof(struct smb3_fs_vol_info) + MAX_VOL_LABEL_LEN;
		min_len = sizeof(struct smb3_fs_vol_info);
	} else {
		cifs_dbg(FYI, "Invalid qfsinfo level %d\n", level);
		return -EINVAL;
	}

	rc = build_qfs_info_req(&iov, tcon, server,
				level, max_len,
				persistent_fid, volatile_fid);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = &iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buftype, flags, &rsp_iov);
	cifs_small_buf_release(iov.iov_base);
	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_QUERY_INFO_HE);
		goto qfsattr_exit;
	}
	rsp = (struct smb2_query_info_rsp *)rsp_iov.iov_base;

	rsp_len = le32_to_cpu(rsp->OutputBufferLength);
	offset = le16_to_cpu(rsp->OutputBufferOffset);
	rc = smb2_validate_iov(offset, rsp_len, &rsp_iov, min_len);
	if (rc)
		goto qfsattr_exit;

	if (level == FS_ATTRIBUTE_INFORMATION)
		memcpy(&tcon->fsAttrInfo, offset
			+ (char *)rsp, min_t(unsigned int,
			rsp_len, max_len));
	else if (level == FS_DEVICE_INFORMATION)
		memcpy(&tcon->fsDevInfo, offset
			+ (char *)rsp, sizeof(FILE_SYSTEM_DEVICE_INFO));
	else if (level == FS_SECTOR_SIZE_INFORMATION) {
		struct smb3_fs_ss_info *ss_info = (struct smb3_fs_ss_info *)
			(offset + (char *)rsp);
		tcon->ss_flags = le32_to_cpu(ss_info->Flags);
		tcon->perf_sector_size =
			le32_to_cpu(ss_info->PhysicalBytesPerSectorForPerf);
	} else if (level == FS_VOLUME_INFORMATION) {
		struct smb3_fs_vol_info *vol_info = (struct smb3_fs_vol_info *)
			(offset + (char *)rsp);
		tcon->vol_serial_number = vol_info->VolumeSerialNumber;
		tcon->vol_create_time = vol_info->VolumeCreationTime;
	}

qfsattr_exit:
	free_rsp_buf(resp_buftype, rsp_iov.iov_base);
	return rc;
}

int
smb2_lockv(const unsigned int xid, struct cifs_tcon *tcon,
	   const __u64 persist_fid, const __u64 volatile_fid, const __u32 pid,
	   const __u32 num_lock, struct smb2_lock_element *buf)
{
	struct smb_rqst rqst;
	int rc = 0;
	struct smb2_lock_req *req = NULL;
	struct kvec iov[2];
	struct kvec rsp_iov;
	int resp_buf_type;
	unsigned int count;
	int flags = CIFS_NO_RSP_BUF;
	unsigned int total_len;
	struct TCP_Server_Info *server = cifs_pick_channel(tcon->ses);

	cifs_dbg(FYI, "smb2_lockv num lock %d\n", num_lock);

	rc = smb2_plain_req_init(SMB2_LOCK, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	req->hdr.Id.SyncId.ProcessId = cpu_to_le32(pid);
	req->LockCount = cpu_to_le16(num_lock);

	req->PersistentFileId = persist_fid;
	req->VolatileFileId = volatile_fid;

	count = num_lock * sizeof(struct smb2_lock_element);

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len - sizeof(struct smb2_lock_element);
	iov[1].iov_base = (char *)buf;
	iov[1].iov_len = count;

	cifs_stats_inc(&tcon->stats.cifs_stats.num_locks);

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 2;

	rc = cifs_send_recv(xid, tcon->ses, server,
			    &rqst, &resp_buf_type, flags,
			    &rsp_iov);
	cifs_small_buf_release(req);
	if (rc) {
		cifs_dbg(FYI, "Send error in smb2_lockv = %d\n", rc);
		cifs_stats_fail_inc(tcon, SMB2_LOCK_HE);
		trace_smb3_lock_err(xid, persist_fid, tcon->tid,
				    tcon->ses->Suid, rc);
	}

	return rc;
}

int
SMB2_lock(const unsigned int xid, struct cifs_tcon *tcon,
	  const __u64 persist_fid, const __u64 volatile_fid, const __u32 pid,
	  const __u64 length, const __u64 offset, const __u32 lock_flags,
	  const bool wait)
{
	struct smb2_lock_element lock;

	lock.Offset = cpu_to_le64(offset);
	lock.Length = cpu_to_le64(length);
	lock.Flags = cpu_to_le32(lock_flags);
	if (!wait && lock_flags != SMB2_LOCKFLAG_UNLOCK)
		lock.Flags |= cpu_to_le32(SMB2_LOCKFLAG_FAIL_IMMEDIATELY);

	return smb2_lockv(xid, tcon, persist_fid, volatile_fid, pid, 1, &lock);
}

int
SMB2_lease_break(const unsigned int xid, struct cifs_tcon *tcon,
		 __u8 *lease_key, const __le32 lease_state)
{
	struct smb_rqst rqst;
	int rc;
	struct smb2_lease_ack *req = NULL;
	struct cifs_ses *ses = tcon->ses;
	int flags = CIFS_OBREAK_OP;
	unsigned int total_len;
	struct kvec iov[1];
	struct kvec rsp_iov;
	int resp_buf_type;
	__u64 *please_key_high;
	__u64 *please_key_low;
	struct TCP_Server_Info *server = cifs_pick_channel(tcon->ses);

	cifs_dbg(FYI, "SMB2_lease_break\n");
	rc = smb2_plain_req_init(SMB2_OPLOCK_BREAK, tcon, server,
				 (void **) &req, &total_len);
	if (rc)
		return rc;

	if (smb3_encryption_required(tcon))
		flags |= CIFS_TRANSFORM_REQ;

	req->hdr.CreditRequest = cpu_to_le16(1);
	req->StructureSize = cpu_to_le16(36);
	total_len += 12;

	memcpy(req->LeaseKey, lease_key, 16);
	req->LeaseState = lease_state;

	flags |= CIFS_NO_RSP_BUF;

	iov[0].iov_base = (char *)req;
	iov[0].iov_len = total_len;

	memset(&rqst, 0, sizeof(struct smb_rqst));
	rqst.rq_iov = iov;
	rqst.rq_nvec = 1;

	rc = cifs_send_recv(xid, ses, server,
			    &rqst, &resp_buf_type, flags, &rsp_iov);
	cifs_small_buf_release(req);

	please_key_low = (__u64 *)lease_key;
	please_key_high = (__u64 *)(lease_key+8);
	if (rc) {
		cifs_stats_fail_inc(tcon, SMB2_OPLOCK_BREAK_HE);
		trace_smb3_lease_err(le32_to_cpu(lease_state), tcon->tid,
			ses->Suid, *please_key_low, *please_key_high, rc);
		cifs_dbg(FYI, "Send error in Lease Break = %d\n", rc);
	} else
		trace_smb3_lease_done(le32_to_cpu(lease_state), tcon->tid,
			ses->Suid, *please_key_low, *please_key_high);

	return rc;
}
