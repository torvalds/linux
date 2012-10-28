/*
 *   fs/cifs/cifssmb.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2010
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   Contains the routines for constructing the SMB PDUs themselves
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

 /* SMB/CIFS PDU handling routines here - except for leftovers in connect.c   */
 /* These are mostly routines that operate on a pathname, or on a tree id     */
 /* (mounted volume), but there are eight handle based routines which must be */
 /* treated slightly differently for reconnection purposes since we never     */
 /* want to reuse a stale file handle and only the caller knows the file info */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/vfs.h>
#include <linux/slab.h>
#include <linux/posix_acl_xattr.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/task_io_accounting_ops.h>
#include <asm/uaccess.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsacl.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "fscache.h"

#ifdef CONFIG_CIFS_POSIX
static struct {
	int index;
	char *name;
} protocols[] = {
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	{LANMAN_PROT, "\2LM1.2X002"},
	{LANMAN2_PROT, "\2LANMAN2.1"},
#endif /* weak password hashing for legacy clients */
	{CIFS_PROT, "\2NT LM 0.12"},
	{POSIX_PROT, "\2POSIX 2"},
	{BAD_PROT, "\2"}
};
#else
static struct {
	int index;
	char *name;
} protocols[] = {
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	{LANMAN_PROT, "\2LM1.2X002"},
	{LANMAN2_PROT, "\2LANMAN2.1"},
#endif /* weak password hashing for legacy clients */
	{CIFS_PROT, "\2NT LM 0.12"},
	{BAD_PROT, "\2"}
};
#endif

/* define the number of elements in the cifs dialect array */
#ifdef CONFIG_CIFS_POSIX
#ifdef CONFIG_CIFS_WEAK_PW_HASH
#define CIFS_NUM_PROT 4
#else
#define CIFS_NUM_PROT 2
#endif /* CIFS_WEAK_PW_HASH */
#else /* not posix */
#ifdef CONFIG_CIFS_WEAK_PW_HASH
#define CIFS_NUM_PROT 3
#else
#define CIFS_NUM_PROT 1
#endif /* CONFIG_CIFS_WEAK_PW_HASH */
#endif /* CIFS_POSIX */

/*
 * Mark as invalid, all open files on tree connections since they
 * were closed when session to server was lost.
 */
void
cifs_mark_open_files_invalid(struct cifs_tcon *tcon)
{
	struct cifsFileInfo *open_file = NULL;
	struct list_head *tmp;
	struct list_head *tmp1;

	/* list all files open on tree connection and mark them invalid */
	spin_lock(&cifs_file_list_lock);
	list_for_each_safe(tmp, tmp1, &tcon->openFileList) {
		open_file = list_entry(tmp, struct cifsFileInfo, tlist);
		open_file->invalidHandle = true;
		open_file->oplock_break_cancelled = true;
	}
	spin_unlock(&cifs_file_list_lock);
	/*
	 * BB Add call to invalidate_inodes(sb) for all superblocks mounted
	 * to this tcon.
	 */
}

/* reconnect the socket, tcon, and smb session if needed */
static int
cifs_reconnect_tcon(struct cifs_tcon *tcon, int smb_command)
{
	int rc;
	struct cifs_ses *ses;
	struct TCP_Server_Info *server;
	struct nls_table *nls_codepage;

	/*
	 * SMBs NegProt, SessSetup, uLogoff do not have tcon yet so check for
	 * tcp and smb session status done differently for those three - in the
	 * calling routine
	 */
	if (!tcon)
		return 0;

	ses = tcon->ses;
	server = ses->server;

	/*
	 * only tree disconnect, open, and write, (and ulogoff which does not
	 * have tcon) are allowed as we start force umount
	 */
	if (tcon->tidStatus == CifsExiting) {
		if (smb_command != SMB_COM_WRITE_ANDX &&
		    smb_command != SMB_COM_OPEN_ANDX &&
		    smb_command != SMB_COM_TREE_DISCONNECT) {
			cFYI(1, "can not send cmd %d while umounting",
				smb_command);
			return -ENODEV;
		}
	}

	/*
	 * Give demultiplex thread up to 10 seconds to reconnect, should be
	 * greater than cifs socket timeout which is 7 seconds
	 */
	while (server->tcpStatus == CifsNeedReconnect) {
		wait_event_interruptible_timeout(server->response_q,
			(server->tcpStatus != CifsNeedReconnect), 10 * HZ);

		/* are we still trying to reconnect? */
		if (server->tcpStatus != CifsNeedReconnect)
			break;

		/*
		 * on "soft" mounts we wait once. Hard mounts keep
		 * retrying until process is killed or server comes
		 * back on-line
		 */
		if (!tcon->retry) {
			cFYI(1, "gave up waiting on reconnect in smb_init");
			return -EHOSTDOWN;
		}
	}

	if (!ses->need_reconnect && !tcon->need_reconnect)
		return 0;

	nls_codepage = load_nls_default();

	/*
	 * need to prevent multiple threads trying to simultaneously
	 * reconnect the same SMB session
	 */
	mutex_lock(&ses->session_mutex);
	rc = cifs_negotiate_protocol(0, ses);
	if (rc == 0 && ses->need_reconnect)
		rc = cifs_setup_session(0, ses, nls_codepage);

	/* do we need to reconnect tcon? */
	if (rc || !tcon->need_reconnect) {
		mutex_unlock(&ses->session_mutex);
		goto out;
	}

	cifs_mark_open_files_invalid(tcon);
	rc = CIFSTCon(0, ses, tcon->treeName, tcon, nls_codepage);
	mutex_unlock(&ses->session_mutex);
	cFYI(1, "reconnect tcon rc = %d", rc);

	if (rc)
		goto out;

	/*
	 * FIXME: check if wsize needs updated due to negotiated smb buffer
	 * 	  size shrinking
	 */
	atomic_inc(&tconInfoReconnectCount);

	/* tell server Unix caps we support */
	if (ses->capabilities & CAP_UNIX)
		reset_cifs_unix_caps(0, tcon, NULL, NULL);

	/*
	 * Removed call to reopen open files here. It is safer (and faster) to
	 * reopen files one at a time as needed in read and write.
	 *
	 * FIXME: what about file locks? don't we need to reclaim them ASAP?
	 */

out:
	/*
	 * Check if handle based operation so we know whether we can continue
	 * or not without returning to caller to reset file handle
	 */
	switch (smb_command) {
	case SMB_COM_READ_ANDX:
	case SMB_COM_WRITE_ANDX:
	case SMB_COM_CLOSE:
	case SMB_COM_FIND_CLOSE2:
	case SMB_COM_LOCKING_ANDX:
		rc = -EAGAIN;
	}

	unload_nls(nls_codepage);
	return rc;
}

/* Allocate and return pointer to an SMB request buffer, and set basic
   SMB information in the SMB header.  If the return code is zero, this
   function must have filled in request_buf pointer */
static int
small_smb_init(int smb_command, int wct, struct cifs_tcon *tcon,
		void **request_buf)
{
	int rc;

	rc = cifs_reconnect_tcon(tcon, smb_command);
	if (rc)
		return rc;

	*request_buf = cifs_small_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}

	header_assemble((struct smb_hdr *) *request_buf, smb_command,
			tcon, wct);

	if (tcon != NULL)
		cifs_stats_inc(&tcon->num_smbs_sent);

	return 0;
}

int
small_smb_init_no_tc(const int smb_command, const int wct,
		     struct cifs_ses *ses, void **request_buf)
{
	int rc;
	struct smb_hdr *buffer;

	rc = small_smb_init(smb_command, wct, NULL, request_buf);
	if (rc)
		return rc;

	buffer = (struct smb_hdr *)*request_buf;
	buffer->Mid = get_next_mid(ses->server);
	if (ses->capabilities & CAP_UNICODE)
		buffer->Flags2 |= SMBFLG2_UNICODE;
	if (ses->capabilities & CAP_STATUS32)
		buffer->Flags2 |= SMBFLG2_ERR_STATUS;

	/* uid, tid can stay at zero as set in header assemble */

	/* BB add support for turning on the signing when
	this function is used after 1st of session setup requests */

	return rc;
}

/* If the return code is zero, this function must fill in request_buf pointer */
static int
__smb_init(int smb_command, int wct, struct cifs_tcon *tcon,
			void **request_buf, void **response_buf)
{
	*request_buf = cifs_buf_get();
	if (*request_buf == NULL) {
		/* BB should we add a retry in here if not a writepage? */
		return -ENOMEM;
	}
    /* Although the original thought was we needed the response buf for  */
    /* potential retries of smb operations it turns out we can determine */
    /* from the mid flags when the request buffer can be resent without  */
    /* having to use a second distinct buffer for the response */
	if (response_buf)
		*response_buf = *request_buf;

	header_assemble((struct smb_hdr *) *request_buf, smb_command, tcon,
			wct);

	if (tcon != NULL)
		cifs_stats_inc(&tcon->num_smbs_sent);

	return 0;
}

/* If the return code is zero, this function must fill in request_buf pointer */
static int
smb_init(int smb_command, int wct, struct cifs_tcon *tcon,
	 void **request_buf, void **response_buf)
{
	int rc;

	rc = cifs_reconnect_tcon(tcon, smb_command);
	if (rc)
		return rc;

	return __smb_init(smb_command, wct, tcon, request_buf, response_buf);
}

static int
smb_init_no_reconnect(int smb_command, int wct, struct cifs_tcon *tcon,
			void **request_buf, void **response_buf)
{
	if (tcon->ses->need_reconnect || tcon->need_reconnect)
		return -EHOSTDOWN;

	return __smb_init(smb_command, wct, tcon, request_buf, response_buf);
}

static int validate_t2(struct smb_t2_rsp *pSMB)
{
	unsigned int total_size;

	/* check for plausible wct */
	if (pSMB->hdr.WordCount < 10)
		goto vt2_err;

	/* check for parm and data offset going beyond end of smb */
	if (get_unaligned_le16(&pSMB->t2_rsp.ParameterOffset) > 1024 ||
	    get_unaligned_le16(&pSMB->t2_rsp.DataOffset) > 1024)
		goto vt2_err;

	total_size = get_unaligned_le16(&pSMB->t2_rsp.ParameterCount);
	if (total_size >= 512)
		goto vt2_err;

	/* check that bcc is at least as big as parms + data, and that it is
	 * less than negotiated smb buffer
	 */
	total_size += get_unaligned_le16(&pSMB->t2_rsp.DataCount);
	if (total_size > get_bcc(&pSMB->hdr) ||
	    total_size >= CIFSMaxBufSize + MAX_CIFS_HDR_SIZE)
		goto vt2_err;

	return 0;
vt2_err:
	cifs_dump_mem("Invalid transact2 SMB: ", (char *)pSMB,
		sizeof(struct smb_t2_rsp) + 16);
	return -EINVAL;
}

int
CIFSSMBNegotiate(const unsigned int xid, struct cifs_ses *ses)
{
	NEGOTIATE_REQ *pSMB;
	NEGOTIATE_RSP *pSMBr;
	int rc = 0;
	int bytes_returned;
	int i;
	struct TCP_Server_Info *server;
	u16 count;
	unsigned int secFlags;

	if (ses->server)
		server = ses->server;
	else {
		rc = -EIO;
		return rc;
	}
	rc = smb_init(SMB_COM_NEGOTIATE, 0, NULL /* no tcon yet */ ,
		      (void **) &pSMB, (void **) &pSMBr);
	if (rc)
		return rc;

	/* if any of auth flags (ie not sign or seal) are overriden use them */
	if (ses->overrideSecFlg & (~(CIFSSEC_MUST_SIGN | CIFSSEC_MUST_SEAL)))
		secFlags = ses->overrideSecFlg;  /* BB FIXME fix sign flags? */
	else /* if override flags set only sign/seal OR them with global auth */
		secFlags = global_secflags | ses->overrideSecFlg;

	cFYI(1, "secFlags 0x%x", secFlags);

	pSMB->hdr.Mid = get_next_mid(server);
	pSMB->hdr.Flags2 |= (SMBFLG2_UNICODE | SMBFLG2_ERR_STATUS);

	if ((secFlags & CIFSSEC_MUST_KRB5) == CIFSSEC_MUST_KRB5)
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;
	else if ((secFlags & CIFSSEC_AUTH_MASK) == CIFSSEC_MAY_KRB5) {
		cFYI(1, "Kerberos only mechanism, enable extended security");
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;
	} else if ((secFlags & CIFSSEC_MUST_NTLMSSP) == CIFSSEC_MUST_NTLMSSP)
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;
	else if ((secFlags & CIFSSEC_AUTH_MASK) == CIFSSEC_MAY_NTLMSSP) {
		cFYI(1, "NTLMSSP only mechanism, enable extended security");
		pSMB->hdr.Flags2 |= SMBFLG2_EXT_SEC;
	}

	count = 0;
	for (i = 0; i < CIFS_NUM_PROT; i++) {
		strncpy(pSMB->DialectsArray+count, protocols[i].name, 16);
		count += strlen(protocols[i].name) + 1;
		/* null at end of source and target buffers anyway */
	}
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc != 0)
		goto neg_err_exit;

	server->dialect = le16_to_cpu(pSMBr->DialectIndex);
	cFYI(1, "Dialect: %d", server->dialect);
	/* Check wct = 1 error case */
	if ((pSMBr->hdr.WordCount < 13) || (server->dialect == BAD_PROT)) {
		/* core returns wct = 1, but we do not ask for core - otherwise
		small wct just comes when dialect index is -1 indicating we
		could not negotiate a common dialect */
		rc = -EOPNOTSUPP;
		goto neg_err_exit;
#ifdef CONFIG_CIFS_WEAK_PW_HASH
	} else if ((pSMBr->hdr.WordCount == 13)
			&& ((server->dialect == LANMAN_PROT)
				|| (server->dialect == LANMAN2_PROT))) {
		__s16 tmp;
		struct lanman_neg_rsp *rsp = (struct lanman_neg_rsp *)pSMBr;

		if ((secFlags & CIFSSEC_MAY_LANMAN) ||
			(secFlags & CIFSSEC_MAY_PLNTXT))
			server->secType = LANMAN;
		else {
			cERROR(1, "mount failed weak security disabled"
				   " in /proc/fs/cifs/SecurityFlags");
			rc = -EOPNOTSUPP;
			goto neg_err_exit;
		}
		server->sec_mode = le16_to_cpu(rsp->SecurityMode);
		server->maxReq = min_t(unsigned int,
				       le16_to_cpu(rsp->MaxMpxCount),
				       cifs_max_pending);
		set_credits(server, server->maxReq);
		server->maxBuf = le16_to_cpu(rsp->MaxBufSize);
		server->max_vcs = le16_to_cpu(rsp->MaxNumberVcs);
		/* even though we do not use raw we might as well set this
		accurately, in case we ever find a need for it */
		if ((le16_to_cpu(rsp->RawMode) & RAW_ENABLE) == RAW_ENABLE) {
			server->max_rw = 0xFF00;
			server->capabilities = CAP_MPX_MODE | CAP_RAW_MODE;
		} else {
			server->max_rw = 0;/* do not need to use raw anyway */
			server->capabilities = CAP_MPX_MODE;
		}
		tmp = (__s16)le16_to_cpu(rsp->ServerTimeZone);
		if (tmp == -1) {
			/* OS/2 often does not set timezone therefore
			 * we must use server time to calc time zone.
			 * Could deviate slightly from the right zone.
			 * Smallest defined timezone difference is 15 minutes
			 * (i.e. Nepal).  Rounding up/down is done to match
			 * this requirement.
			 */
			int val, seconds, remain, result;
			struct timespec ts, utc;
			utc = CURRENT_TIME;
			ts = cnvrtDosUnixTm(rsp->SrvTime.Date,
					    rsp->SrvTime.Time, 0);
			cFYI(1, "SrvTime %d sec since 1970 (utc: %d) diff: %d",
				(int)ts.tv_sec, (int)utc.tv_sec,
				(int)(utc.tv_sec - ts.tv_sec));
			val = (int)(utc.tv_sec - ts.tv_sec);
			seconds = abs(val);
			result = (seconds / MIN_TZ_ADJ) * MIN_TZ_ADJ;
			remain = seconds % MIN_TZ_ADJ;
			if (remain >= (MIN_TZ_ADJ / 2))
				result += MIN_TZ_ADJ;
			if (val < 0)
				result = -result;
			server->timeAdj = result;
		} else {
			server->timeAdj = (int)tmp;
			server->timeAdj *= 60; /* also in seconds */
		}
		cFYI(1, "server->timeAdj: %d seconds", server->timeAdj);


		/* BB get server time for time conversions and add
		code to use it and timezone since this is not UTC */

		if (rsp->EncryptionKeyLength ==
				cpu_to_le16(CIFS_CRYPTO_KEY_SIZE)) {
			memcpy(ses->server->cryptkey, rsp->EncryptionKey,
				CIFS_CRYPTO_KEY_SIZE);
		} else if (server->sec_mode & SECMODE_PW_ENCRYPT) {
			rc = -EIO; /* need cryptkey unless plain text */
			goto neg_err_exit;
		}

		cFYI(1, "LANMAN negotiated");
		/* we will not end up setting signing flags - as no signing
		was in LANMAN and server did not return the flags on */
		goto signing_check;
#else /* weak security disabled */
	} else if (pSMBr->hdr.WordCount == 13) {
		cERROR(1, "mount failed, cifs module not built "
			  "with CIFS_WEAK_PW_HASH support");
		rc = -EOPNOTSUPP;
#endif /* WEAK_PW_HASH */
		goto neg_err_exit;
	} else if (pSMBr->hdr.WordCount != 17) {
		/* unknown wct */
		rc = -EOPNOTSUPP;
		goto neg_err_exit;
	}
	/* else wct == 17 NTLM */
	server->sec_mode = pSMBr->SecurityMode;
	if ((server->sec_mode & SECMODE_USER) == 0)
		cFYI(1, "share mode security");

	if ((server->sec_mode & SECMODE_PW_ENCRYPT) == 0)
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		if ((secFlags & CIFSSEC_MAY_PLNTXT) == 0)
#endif /* CIFS_WEAK_PW_HASH */
			cERROR(1, "Server requests plain text password"
				  " but client support disabled");

	if ((secFlags & CIFSSEC_MUST_NTLMV2) == CIFSSEC_MUST_NTLMV2)
		server->secType = NTLMv2;
	else if (secFlags & CIFSSEC_MAY_NTLM)
		server->secType = NTLM;
	else if (secFlags & CIFSSEC_MAY_NTLMV2)
		server->secType = NTLMv2;
	else if (secFlags & CIFSSEC_MAY_KRB5)
		server->secType = Kerberos;
	else if (secFlags & CIFSSEC_MAY_NTLMSSP)
		server->secType = RawNTLMSSP;
	else if (secFlags & CIFSSEC_MAY_LANMAN)
		server->secType = LANMAN;
	else {
		rc = -EOPNOTSUPP;
		cERROR(1, "Invalid security type");
		goto neg_err_exit;
	}
	/* else ... any others ...? */

	/* one byte, so no need to convert this or EncryptionKeyLen from
	   little endian */
	server->maxReq = min_t(unsigned int, le16_to_cpu(pSMBr->MaxMpxCount),
			       cifs_max_pending);
	set_credits(server, server->maxReq);
	/* probably no need to store and check maxvcs */
	server->maxBuf = le32_to_cpu(pSMBr->MaxBufferSize);
	server->max_rw = le32_to_cpu(pSMBr->MaxRawSize);
	cFYI(DBG2, "Max buf = %d", ses->server->maxBuf);
	server->capabilities = le32_to_cpu(pSMBr->Capabilities);
	server->timeAdj = (int)(__s16)le16_to_cpu(pSMBr->ServerTimeZone);
	server->timeAdj *= 60;
	if (pSMBr->EncryptionKeyLength == CIFS_CRYPTO_KEY_SIZE) {
		memcpy(ses->server->cryptkey, pSMBr->u.EncryptionKey,
		       CIFS_CRYPTO_KEY_SIZE);
	} else if ((pSMBr->hdr.Flags2 & SMBFLG2_EXT_SEC ||
			server->capabilities & CAP_EXTENDED_SECURITY) &&
				(pSMBr->EncryptionKeyLength == 0)) {
		/* decode security blob */
		count = get_bcc(&pSMBr->hdr);
		if (count < 16) {
			rc = -EIO;
			goto neg_err_exit;
		}
		spin_lock(&cifs_tcp_ses_lock);
		if (server->srv_count > 1) {
			spin_unlock(&cifs_tcp_ses_lock);
			if (memcmp(server->server_GUID,
				   pSMBr->u.extended_response.
				   GUID, 16) != 0) {
				cFYI(1, "server UID changed");
				memcpy(server->server_GUID,
					pSMBr->u.extended_response.GUID,
					16);
			}
		} else {
			spin_unlock(&cifs_tcp_ses_lock);
			memcpy(server->server_GUID,
			       pSMBr->u.extended_response.GUID, 16);
		}

		if (count == 16) {
			server->secType = RawNTLMSSP;
		} else {
			rc = decode_negTokenInit(pSMBr->u.extended_response.
						 SecurityBlob, count - 16,
						 server);
			if (rc == 1)
				rc = 0;
			else
				rc = -EINVAL;
			if (server->secType == Kerberos) {
				if (!server->sec_kerberos &&
						!server->sec_mskerberos)
					rc = -EOPNOTSUPP;
			} else if (server->secType == RawNTLMSSP) {
				if (!server->sec_ntlmssp)
					rc = -EOPNOTSUPP;
			} else
					rc = -EOPNOTSUPP;
		}
	} else if (server->sec_mode & SECMODE_PW_ENCRYPT) {
		rc = -EIO; /* no crypt key only if plain text pwd */
		goto neg_err_exit;
	} else
		server->capabilities &= ~CAP_EXTENDED_SECURITY;

#ifdef CONFIG_CIFS_WEAK_PW_HASH
signing_check:
#endif
	if ((secFlags & CIFSSEC_MAY_SIGN) == 0) {
		/* MUST_SIGN already includes the MAY_SIGN FLAG
		   so if this is zero it means that signing is disabled */
		cFYI(1, "Signing disabled");
		if (server->sec_mode & SECMODE_SIGN_REQUIRED) {
			cERROR(1, "Server requires "
				   "packet signing to be enabled in "
				   "/proc/fs/cifs/SecurityFlags.");
			rc = -EOPNOTSUPP;
		}
		server->sec_mode &=
			~(SECMODE_SIGN_ENABLED | SECMODE_SIGN_REQUIRED);
	} else if ((secFlags & CIFSSEC_MUST_SIGN) == CIFSSEC_MUST_SIGN) {
		/* signing required */
		cFYI(1, "Must sign - secFlags 0x%x", secFlags);
		if ((server->sec_mode &
			(SECMODE_SIGN_ENABLED | SECMODE_SIGN_REQUIRED)) == 0) {
			cERROR(1, "signing required but server lacks support");
			rc = -EOPNOTSUPP;
		} else
			server->sec_mode |= SECMODE_SIGN_REQUIRED;
	} else {
		/* signing optional ie CIFSSEC_MAY_SIGN */
		if ((server->sec_mode & SECMODE_SIGN_REQUIRED) == 0)
			server->sec_mode &=
				~(SECMODE_SIGN_ENABLED | SECMODE_SIGN_REQUIRED);
	}

neg_err_exit:
	cifs_buf_release(pSMB);

	cFYI(1, "negprot rc %d", rc);
	return rc;
}

int
CIFSSMBTDis(const unsigned int xid, struct cifs_tcon *tcon)
{
	struct smb_hdr *smb_buffer;
	int rc = 0;

	cFYI(1, "In tree disconnect");

	/* BB: do we need to check this? These should never be NULL. */
	if ((tcon->ses == NULL) || (tcon->ses->server == NULL))
		return -EIO;

	/*
	 * No need to return error on this operation if tid invalidated and
	 * closed on server already e.g. due to tcp session crashing. Also,
	 * the tcon is no longer on the list, so no need to take lock before
	 * checking this.
	 */
	if ((tcon->need_reconnect) || (tcon->ses->need_reconnect))
		return 0;

	rc = small_smb_init(SMB_COM_TREE_DISCONNECT, 0, tcon,
			    (void **)&smb_buffer);
	if (rc)
		return rc;

	rc = SendReceiveNoRsp(xid, tcon->ses, (char *)smb_buffer, 0);
	if (rc)
		cFYI(1, "Tree disconnect failed %d", rc);

	/* No need to return error on this operation if tid invalidated and
	   closed on server already e.g. due to tcp session crashing */
	if (rc == -EAGAIN)
		rc = 0;

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
cifs_echo_callback(struct mid_q_entry *mid)
{
	struct TCP_Server_Info *server = mid->callback_data;

	DeleteMidQEntry(mid);
	add_credits(server, 1, CIFS_ECHO_OP);
}

int
CIFSSMBEcho(struct TCP_Server_Info *server)
{
	ECHO_REQ *smb;
	int rc = 0;
	struct kvec iov;
	struct smb_rqst rqst = { .rq_iov = &iov,
				 .rq_nvec = 1 };

	cFYI(1, "In echo request");

	rc = small_smb_init(SMB_COM_ECHO, 0, NULL, (void **)&smb);
	if (rc)
		return rc;

	/* set up echo request */
	smb->hdr.Tid = 0xffff;
	smb->hdr.WordCount = 1;
	put_unaligned_le16(1, &smb->EchoCount);
	put_bcc(1, &smb->hdr);
	smb->Data[0] = 'a';
	inc_rfc1001_len(smb, 3);
	iov.iov_base = smb;
	iov.iov_len = be32_to_cpu(smb->hdr.smb_buf_length) + 4;

	rc = cifs_call_async(server, &rqst, NULL, cifs_echo_callback,
			     server, CIFS_ASYNC_OP | CIFS_ECHO_OP);
	if (rc)
		cFYI(1, "Echo request failed: %d", rc);

	cifs_small_buf_release(smb);

	return rc;
}

int
CIFSSMBLogoff(const unsigned int xid, struct cifs_ses *ses)
{
	LOGOFF_ANDX_REQ *pSMB;
	int rc = 0;

	cFYI(1, "In SMBLogoff for session disconnect");

	/*
	 * BB: do we need to check validity of ses and server? They should
	 * always be valid since we have an active reference. If not, that
	 * should probably be a BUG()
	 */
	if (!ses || !ses->server)
		return -EIO;

	mutex_lock(&ses->session_mutex);
	if (ses->need_reconnect)
		goto session_already_dead; /* no need to send SMBlogoff if uid
					      already closed due to reconnect */
	rc = small_smb_init(SMB_COM_LOGOFF_ANDX, 2, NULL, (void **)&pSMB);
	if (rc) {
		mutex_unlock(&ses->session_mutex);
		return rc;
	}

	pSMB->hdr.Mid = get_next_mid(ses->server);

	if (ses->server->sec_mode &
		   (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
			pSMB->hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	pSMB->hdr.Uid = ses->Suid;

	pSMB->AndXCommand = 0xFF;
	rc = SendReceiveNoRsp(xid, ses, (char *) pSMB, 0);
session_already_dead:
	mutex_unlock(&ses->session_mutex);

	/* if session dead then we do not need to do ulogoff,
		since server closed smb session, no sense reporting
		error */
	if (rc == -EAGAIN)
		rc = 0;
	return rc;
}

int
CIFSPOSIXDelFile(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *fileName, __u16 type,
		 const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	struct unlink_psx_rq *pRqD;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, "In POSIX delete");
PsxDelete:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, fileName,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else { /* BB add path length overrun check */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	params = 6 + name_len;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = 0; /* BB double check this with jra */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;

	/* Setup pointer to Request Data (inode type) */
	pRqD = (struct unlink_psx_rq *)(((char *)&pSMB->hdr.Protocol) + offset);
	pRqD->type = cpu_to_le16(type);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + sizeof(struct unlink_psx_rq);

	pSMB->DataCount = cpu_to_le16(sizeof(struct unlink_psx_rq));
	pSMB->TotalDataCount = cpu_to_le16(sizeof(struct unlink_psx_rq));
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_POSIX_UNLINK);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "Posix delete returned %d", rc);
	cifs_buf_release(pSMB);

	cifs_stats_inc(&tcon->stats.cifs_stats.num_deletes);

	if (rc == -EAGAIN)
		goto PsxDelete;

	return rc;
}

int
CIFSSMBDelFile(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	       struct cifs_sb_info *cifs_sb)
{
	DELETE_FILE_REQ *pSMB = NULL;
	DELETE_FILE_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	int remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

DelFileRetry:
	rc = smb_init(SMB_COM_DELETE, 1, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUTF16((__le16 *) pSMB->fileName, name,
					      PATH_MAX, cifs_sb->local_nls,
					      remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve check for buffer overruns BB */
		name_len = strnlen(name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->fileName, name, name_len);
	}
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM);
	pSMB->BufferFormat = 0x04;
	inc_rfc1001_len(pSMB, name_len + 1);
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_deletes);
	if (rc)
		cFYI(1, "Error in RMFile = %d", rc);

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto DelFileRetry;

	return rc;
}

int
CIFSSMBRmDir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	     struct cifs_sb_info *cifs_sb)
{
	DELETE_DIRECTORY_REQ *pSMB = NULL;
	DELETE_DIRECTORY_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	int remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

	cFYI(1, "In CIFSSMBRmDir");
RmDirRetry:
	rc = smb_init(SMB_COM_DELETE_DIRECTORY, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUTF16((__le16 *) pSMB->DirName, name,
					      PATH_MAX, cifs_sb->local_nls,
					      remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve check for buffer overruns BB */
		name_len = strnlen(name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->DirName, name, name_len);
	}

	pSMB->BufferFormat = 0x04;
	inc_rfc1001_len(pSMB, name_len + 1);
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_rmdirs);
	if (rc)
		cFYI(1, "Error in RMDir = %d", rc);

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto RmDirRetry;
	return rc;
}

int
CIFSSMBMkDir(const unsigned int xid, struct cifs_tcon *tcon, const char *name,
	     struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	CREATE_DIRECTORY_REQ *pSMB = NULL;
	CREATE_DIRECTORY_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;
	int remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

	cFYI(1, "In CIFSSMBMkDir");
MkDirRetry:
	rc = smb_init(SMB_COM_CREATE_DIRECTORY, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUTF16((__le16 *) pSMB->DirName, name,
					      PATH_MAX, cifs_sb->local_nls,
					      remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {		/* BB improve check for buffer overruns BB */
		name_len = strnlen(name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->DirName, name, name_len);
	}

	pSMB->BufferFormat = 0x04;
	inc_rfc1001_len(pSMB, name_len + 1);
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_mkdirs);
	if (rc)
		cFYI(1, "Error in Mkdir = %d", rc);

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto MkDirRetry;
	return rc;
}

int
CIFSPOSIXCreate(const unsigned int xid, struct cifs_tcon *tcon,
		__u32 posix_flags, __u64 mode, __u16 *netfid,
		FILE_UNIX_BASIC_INFO *pRetData, __u32 *pOplock,
		const char *name, const struct nls_table *nls_codepage,
		int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count, count;
	OPEN_PSX_REQ *pdata;
	OPEN_PSX_RSP *psx_rsp;

	cFYI(1, "In POSIX Create");
PsxCreat:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, name,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, name, name_len);
	}

	params = 6 + name_len;
	count = sizeof(OPEN_PSX_REQ);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);	/* large enough */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;
	pdata = (OPEN_PSX_REQ *)(((char *)&pSMB->hdr.Protocol) + offset);
	pdata->Level = cpu_to_le16(SMB_QUERY_FILE_UNIX_BASIC);
	pdata->Permissions = cpu_to_le64(mode);
	pdata->PosixOpenFlags = cpu_to_le32(posix_flags);
	pdata->OpenFlags =  cpu_to_le32(*pOplock);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;

	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_POSIX_OPEN);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Posix create returned %d", rc);
		goto psx_create_err;
	}

	cFYI(1, "copying inode info");
	rc = validate_t2((struct smb_t2_rsp *)pSMBr);

	if (rc || get_bcc(&pSMBr->hdr) < sizeof(OPEN_PSX_RSP)) {
		rc = -EIO;	/* bad smb */
		goto psx_create_err;
	}

	/* copy return information to pRetData */
	psx_rsp = (OPEN_PSX_RSP *)((char *) &pSMBr->hdr.Protocol
			+ le16_to_cpu(pSMBr->t2.DataOffset));

	*pOplock = le16_to_cpu(psx_rsp->OplockFlags);
	if (netfid)
		*netfid = psx_rsp->Fid;   /* cifs fid stays in le */
	/* Let caller know file was created so we can set the mode. */
	/* Do we care about the CreateAction in any other cases? */
	if (cpu_to_le32(FILE_CREATE) == psx_rsp->CreateAction)
		*pOplock |= CIFS_CREATE_ACTION;
	/* check to make sure response data is there */
	if (psx_rsp->ReturnedLevel != cpu_to_le16(SMB_QUERY_FILE_UNIX_BASIC)) {
		pRetData->Type = cpu_to_le32(-1); /* unknown */
		cFYI(DBG2, "unknown type");
	} else {
		if (get_bcc(&pSMBr->hdr) < sizeof(OPEN_PSX_RSP)
					+ sizeof(FILE_UNIX_BASIC_INFO)) {
			cERROR(1, "Open response data too small");
			pRetData->Type = cpu_to_le32(-1);
			goto psx_create_err;
		}
		memcpy((char *) pRetData,
			(char *)psx_rsp + sizeof(OPEN_PSX_RSP),
			sizeof(FILE_UNIX_BASIC_INFO));
	}

psx_create_err:
	cifs_buf_release(pSMB);

	if (posix_flags & SMB_O_DIRECTORY)
		cifs_stats_inc(&tcon->stats.cifs_stats.num_posixmkdirs);
	else
		cifs_stats_inc(&tcon->stats.cifs_stats.num_posixopens);

	if (rc == -EAGAIN)
		goto PsxCreat;

	return rc;
}

static __u16 convert_disposition(int disposition)
{
	__u16 ofun = 0;

	switch (disposition) {
		case FILE_SUPERSEDE:
			ofun = SMBOPEN_OCREATE | SMBOPEN_OTRUNC;
			break;
		case FILE_OPEN:
			ofun = SMBOPEN_OAPPEND;
			break;
		case FILE_CREATE:
			ofun = SMBOPEN_OCREATE;
			break;
		case FILE_OPEN_IF:
			ofun = SMBOPEN_OCREATE | SMBOPEN_OAPPEND;
			break;
		case FILE_OVERWRITE:
			ofun = SMBOPEN_OTRUNC;
			break;
		case FILE_OVERWRITE_IF:
			ofun = SMBOPEN_OCREATE | SMBOPEN_OTRUNC;
			break;
		default:
			cFYI(1, "unknown disposition %d", disposition);
			ofun =  SMBOPEN_OAPPEND; /* regular open */
	}
	return ofun;
}

static int
access_flags_to_smbopen_mode(const int access_flags)
{
	int masked_flags = access_flags & (GENERIC_READ | GENERIC_WRITE);

	if (masked_flags == GENERIC_READ)
		return SMBOPEN_READ;
	else if (masked_flags == GENERIC_WRITE)
		return SMBOPEN_WRITE;

	/* just go for read/write */
	return SMBOPEN_READWRITE;
}

int
SMBLegacyOpen(const unsigned int xid, struct cifs_tcon *tcon,
	    const char *fileName, const int openDisposition,
	    const int access_flags, const int create_options, __u16 *netfid,
	    int *pOplock, FILE_ALL_INFO *pfile_info,
	    const struct nls_table *nls_codepage, int remap)
{
	int rc = -EACCES;
	OPENX_REQ *pSMB = NULL;
	OPENX_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;
	__u16 count;

OldOpenRetry:
	rc = smb_init(SMB_COM_OPEN_ANDX, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;       /* none */

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		count = 1;      /* account for one byte pad to word boundary */
		name_len =
		   cifsConvertToUTF16((__le16 *) (pSMB->fileName + 1),
				      fileName, PATH_MAX, nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {                /* BB improve check for buffer overruns BB */
		count = 0;      /* no pad */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->fileName, fileName, name_len);
	}
	if (*pOplock & REQ_OPLOCK)
		pSMB->OpenFlags = cpu_to_le16(REQ_OPLOCK);
	else if (*pOplock & REQ_BATCHOPLOCK)
		pSMB->OpenFlags = cpu_to_le16(REQ_BATCHOPLOCK);

	pSMB->OpenFlags |= cpu_to_le16(REQ_MORE_INFO);
	pSMB->Mode = cpu_to_le16(access_flags_to_smbopen_mode(access_flags));
	pSMB->Mode |= cpu_to_le16(0x40); /* deny none */
	/* set file as system file if special file such
	   as fifo and server expecting SFU style and
	   no Unix extensions */

	if (create_options & CREATE_OPTION_SPECIAL)
		pSMB->FileAttributes = cpu_to_le16(ATTR_SYSTEM);
	else /* BB FIXME BB */
		pSMB->FileAttributes = cpu_to_le16(0/*ATTR_NORMAL*/);

	if (create_options & CREATE_OPTION_READONLY)
		pSMB->FileAttributes |= cpu_to_le16(ATTR_READONLY);

	/* BB FIXME BB */
/*	pSMB->CreateOptions = cpu_to_le32(create_options &
						 CREATE_OPTIONS_MASK); */
	/* BB FIXME END BB */

	pSMB->Sattr = cpu_to_le16(ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIRECTORY);
	pSMB->OpenFunction = cpu_to_le16(convert_disposition(openDisposition));
	count += name_len;
	inc_rfc1001_len(pSMB, count);

	pSMB->ByteCount = cpu_to_le16(count);
	/* long_op set to 1 to allow for oplock break timeouts */
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			(struct smb_hdr *)pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_opens);
	if (rc) {
		cFYI(1, "Error in Open = %d", rc);
	} else {
	/* BB verify if wct == 15 */

/*		*pOplock = pSMBr->OplockLevel; */ /* BB take from action field*/

		*netfid = pSMBr->Fid;   /* cifs fid stays in le */
		/* Let caller know file was created so we can set the mode. */
		/* Do we care about the CreateAction in any other cases? */
	/* BB FIXME BB */
/*		if (cpu_to_le32(FILE_CREATE) == pSMBr->CreateAction)
			*pOplock |= CIFS_CREATE_ACTION; */
	/* BB FIXME END */

		if (pfile_info) {
			pfile_info->CreationTime = 0; /* BB convert CreateTime*/
			pfile_info->LastAccessTime = 0; /* BB fixme */
			pfile_info->LastWriteTime = 0; /* BB fixme */
			pfile_info->ChangeTime = 0;  /* BB fixme */
			pfile_info->Attributes =
				cpu_to_le32(le16_to_cpu(pSMBr->FileAttributes));
			/* the file_info buf is endian converted by caller */
			pfile_info->AllocationSize =
				cpu_to_le64(le32_to_cpu(pSMBr->EndOfFile));
			pfile_info->EndOfFile = pfile_info->AllocationSize;
			pfile_info->NumberOfLinks = cpu_to_le32(1);
			pfile_info->DeletePending = 0;
		}
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto OldOpenRetry;
	return rc;
}

int
CIFSSMBOpen(const unsigned int xid, struct cifs_tcon *tcon,
	    const char *fileName, const int openDisposition,
	    const int access_flags, const int create_options, __u16 *netfid,
	    int *pOplock, FILE_ALL_INFO *pfile_info,
	    const struct nls_table *nls_codepage, int remap)
{
	int rc = -EACCES;
	OPEN_REQ *pSMB = NULL;
	OPEN_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len;
	__u16 count;

openRetry:
	rc = smb_init(SMB_COM_NT_CREATE_ANDX, 24, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->AndXCommand = 0xFF;	/* none */

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		count = 1;	/* account for one byte pad to word boundary */
		name_len =
		    cifsConvertToUTF16((__le16 *) (pSMB->fileName + 1),
				       fileName, PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->NameLength = cpu_to_le16(name_len);
	} else {		/* BB improve check for buffer overruns BB */
		count = 0;	/* no pad */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		pSMB->NameLength = cpu_to_le16(name_len);
		strncpy(pSMB->fileName, fileName, name_len);
	}
	if (*pOplock & REQ_OPLOCK)
		pSMB->OpenFlags = cpu_to_le32(REQ_OPLOCK);
	else if (*pOplock & REQ_BATCHOPLOCK)
		pSMB->OpenFlags = cpu_to_le32(REQ_BATCHOPLOCK);
	pSMB->DesiredAccess = cpu_to_le32(access_flags);
	pSMB->AllocationSize = 0;
	/* set file as system file if special file such
	   as fifo and server expecting SFU style and
	   no Unix extensions */
	if (create_options & CREATE_OPTION_SPECIAL)
		pSMB->FileAttributes = cpu_to_le32(ATTR_SYSTEM);
	else
		pSMB->FileAttributes = cpu_to_le32(ATTR_NORMAL);

	/* XP does not handle ATTR_POSIX_SEMANTICS */
	/* but it helps speed up case sensitive checks for other
	servers such as Samba */
	if (tcon->ses->capabilities & CAP_UNIX)
		pSMB->FileAttributes |= cpu_to_le32(ATTR_POSIX_SEMANTICS);

	if (create_options & CREATE_OPTION_READONLY)
		pSMB->FileAttributes |= cpu_to_le32(ATTR_READONLY);

	pSMB->ShareAccess = cpu_to_le32(FILE_SHARE_ALL);
	pSMB->CreateDisposition = cpu_to_le32(openDisposition);
	pSMB->CreateOptions = cpu_to_le32(create_options & CREATE_OPTIONS_MASK);
	/* BB Expirement with various impersonation levels and verify */
	pSMB->ImpersonationLevel = cpu_to_le32(SECURITY_IMPERSONATION);
	pSMB->SecurityFlags =
	    SECURITY_CONTEXT_TRACKING | SECURITY_EFFECTIVE_ONLY;

	count += name_len;
	inc_rfc1001_len(pSMB, count);

	pSMB->ByteCount = cpu_to_le16(count);
	/* long_op set to 1 to allow for oplock break timeouts */
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			(struct smb_hdr *)pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_opens);
	if (rc) {
		cFYI(1, "Error in Open = %d", rc);
	} else {
		*pOplock = pSMBr->OplockLevel; /* 1 byte no need to le_to_cpu */
		*netfid = pSMBr->Fid;	/* cifs fid stays in le */
		/* Let caller know file was created so we can set the mode. */
		/* Do we care about the CreateAction in any other cases? */
		if (cpu_to_le32(FILE_CREATE) == pSMBr->CreateAction)
			*pOplock |= CIFS_CREATE_ACTION;
		if (pfile_info) {
			memcpy((char *)pfile_info, (char *)&pSMBr->CreationTime,
				36 /* CreationTime to Attributes */);
			/* the file_info buf is endian converted by caller */
			pfile_info->AllocationSize = pSMBr->AllocationSize;
			pfile_info->EndOfFile = pSMBr->EndOfFile;
			pfile_info->NumberOfLinks = cpu_to_le32(1);
			pfile_info->DeletePending = 0;
		}
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto openRetry;
	return rc;
}

/*
 * Discard any remaining data in the current SMB. To do this, we borrow the
 * current bigbuf.
 */
static int
cifs_readv_discard(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	unsigned int rfclen = get_rfc1002_length(server->smallbuf);
	int remaining = rfclen + 4 - server->total_read;
	struct cifs_readdata *rdata = mid->callback_data;

	while (remaining > 0) {
		int length;

		length = cifs_read_from_socket(server, server->bigbuf,
				min_t(unsigned int, remaining,
				    CIFSMaxBufSize + MAX_HEADER_SIZE(server)));
		if (length < 0)
			return length;
		server->total_read += length;
		remaining -= length;
	}

	dequeue_mid(mid, rdata->result);
	return 0;
}

int
cifs_readv_receive(struct TCP_Server_Info *server, struct mid_q_entry *mid)
{
	int length, len;
	unsigned int data_offset, data_len;
	struct cifs_readdata *rdata = mid->callback_data;
	char *buf = server->smallbuf;
	unsigned int buflen = get_rfc1002_length(buf) + 4;

	cFYI(1, "%s: mid=%llu offset=%llu bytes=%u", __func__,
		mid->mid, rdata->offset, rdata->bytes);

	/*
	 * read the rest of READ_RSP header (sans Data array), or whatever we
	 * can if there's not enough data. At this point, we've read down to
	 * the Mid.
	 */
	len = min_t(unsigned int, buflen, server->vals->read_rsp_size) -
							HEADER_SIZE(server) + 1;

	rdata->iov.iov_base = buf + HEADER_SIZE(server) - 1;
	rdata->iov.iov_len = len;

	length = cifs_readv_from_socket(server, &rdata->iov, 1, len);
	if (length < 0)
		return length;
	server->total_read += length;

	/* Was the SMB read successful? */
	rdata->result = server->ops->map_error(buf, false);
	if (rdata->result != 0) {
		cFYI(1, "%s: server returned error %d", __func__,
			rdata->result);
		return cifs_readv_discard(server, mid);
	}

	/* Is there enough to get to the rest of the READ_RSP header? */
	if (server->total_read < server->vals->read_rsp_size) {
		cFYI(1, "%s: server returned short header. got=%u expected=%zu",
			__func__, server->total_read,
			server->vals->read_rsp_size);
		rdata->result = -EIO;
		return cifs_readv_discard(server, mid);
	}

	data_offset = server->ops->read_data_offset(buf) + 4;
	if (data_offset < server->total_read) {
		/*
		 * win2k8 sometimes sends an offset of 0 when the read
		 * is beyond the EOF. Treat it as if the data starts just after
		 * the header.
		 */
		cFYI(1, "%s: data offset (%u) inside read response header",
			__func__, data_offset);
		data_offset = server->total_read;
	} else if (data_offset > MAX_CIFS_SMALL_BUFFER_SIZE) {
		/* data_offset is beyond the end of smallbuf */
		cFYI(1, "%s: data offset (%u) beyond end of smallbuf",
			__func__, data_offset);
		rdata->result = -EIO;
		return cifs_readv_discard(server, mid);
	}

	cFYI(1, "%s: total_read=%u data_offset=%u", __func__,
		server->total_read, data_offset);

	len = data_offset - server->total_read;
	if (len > 0) {
		/* read any junk before data into the rest of smallbuf */
		rdata->iov.iov_base = buf + server->total_read;
		rdata->iov.iov_len = len;
		length = cifs_readv_from_socket(server, &rdata->iov, 1, len);
		if (length < 0)
			return length;
		server->total_read += length;
	}

	/* set up first iov for signature check */
	rdata->iov.iov_base = buf;
	rdata->iov.iov_len = server->total_read;
	cFYI(1, "0: iov_base=%p iov_len=%zu",
		rdata->iov.iov_base, rdata->iov.iov_len);

	/* how much data is in the response? */
	data_len = server->ops->read_data_length(buf);
	if (data_offset + data_len > buflen) {
		/* data_len is corrupt -- discard frame */
		rdata->result = -EIO;
		return cifs_readv_discard(server, mid);
	}

	length = rdata->read_into_pages(server, rdata, data_len);
	if (length < 0)
		return length;

	server->total_read += length;
	rdata->bytes = length;

	cFYI(1, "total_read=%u buflen=%u remaining=%u", server->total_read,
		buflen, data_len);

	/* discard anything left over */
	if (server->total_read < buflen)
		return cifs_readv_discard(server, mid);

	dequeue_mid(mid, false);
	return length;
}

static void
cifs_readv_callback(struct mid_q_entry *mid)
{
	struct cifs_readdata *rdata = mid->callback_data;
	struct cifs_tcon *tcon = tlink_tcon(rdata->cfile->tlink);
	struct TCP_Server_Info *server = tcon->ses->server;
	struct smb_rqst rqst = { .rq_iov = &rdata->iov,
				 .rq_nvec = 1,
				 .rq_pages = rdata->pages,
				 .rq_npages = rdata->nr_pages,
				 .rq_pagesz = rdata->pagesz,
				 .rq_tailsz = rdata->tailsz };

	cFYI(1, "%s: mid=%llu state=%d result=%d bytes=%u", __func__,
		mid->mid, mid->mid_state, rdata->result, rdata->bytes);

	switch (mid->mid_state) {
	case MID_RESPONSE_RECEIVED:
		/* result already set, check signature */
		if (server->sec_mode &
		    (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED)) {
			int rc = 0;

			rc = cifs_verify_signature(&rqst, server,
						  mid->sequence_number + 1);
			if (rc)
				cERROR(1, "SMB signature verification returned "
				       "error = %d", rc);
		}
		/* FIXME: should this be counted toward the initiating task? */
		task_io_account_read(rdata->bytes);
		cifs_stats_bytes_read(tcon, rdata->bytes);
		break;
	case MID_REQUEST_SUBMITTED:
	case MID_RETRY_NEEDED:
		rdata->result = -EAGAIN;
		break;
	default:
		rdata->result = -EIO;
	}

	queue_work(cifsiod_wq, &rdata->work);
	DeleteMidQEntry(mid);
	add_credits(server, 1, 0);
}

/* cifs_async_readv - send an async write, and set up mid to handle result */
int
cifs_async_readv(struct cifs_readdata *rdata)
{
	int rc;
	READ_REQ *smb = NULL;
	int wct;
	struct cifs_tcon *tcon = tlink_tcon(rdata->cfile->tlink);
	struct smb_rqst rqst = { .rq_iov = &rdata->iov,
				 .rq_nvec = 1 };

	cFYI(1, "%s: offset=%llu bytes=%u", __func__,
		rdata->offset, rdata->bytes);

	if (tcon->ses->capabilities & CAP_LARGE_FILES)
		wct = 12;
	else {
		wct = 10; /* old style read */
		if ((rdata->offset >> 32) > 0)  {
			/* can not handle this big offset for old */
			return -EIO;
		}
	}

	rc = small_smb_init(SMB_COM_READ_ANDX, wct, tcon, (void **)&smb);
	if (rc)
		return rc;

	smb->hdr.Pid = cpu_to_le16((__u16)rdata->pid);
	smb->hdr.PidHigh = cpu_to_le16((__u16)(rdata->pid >> 16));

	smb->AndXCommand = 0xFF;	/* none */
	smb->Fid = rdata->cfile->fid.netfid;
	smb->OffsetLow = cpu_to_le32(rdata->offset & 0xFFFFFFFF);
	if (wct == 12)
		smb->OffsetHigh = cpu_to_le32(rdata->offset >> 32);
	smb->Remaining = 0;
	smb->MaxCount = cpu_to_le16(rdata->bytes & 0xFFFF);
	smb->MaxCountHigh = cpu_to_le32(rdata->bytes >> 16);
	if (wct == 12)
		smb->ByteCount = 0;
	else {
		/* old style read */
		struct smb_com_readx_req *smbr =
			(struct smb_com_readx_req *)smb;
		smbr->ByteCount = 0;
	}

	/* 4 for RFC1001 length + 1 for BCC */
	rdata->iov.iov_base = smb;
	rdata->iov.iov_len = be32_to_cpu(smb->hdr.smb_buf_length) + 4;

	kref_get(&rdata->refcount);
	rc = cifs_call_async(tcon->ses->server, &rqst, cifs_readv_receive,
			     cifs_readv_callback, rdata, 0);

	if (rc == 0)
		cifs_stats_inc(&tcon->stats.cifs_stats.num_reads);
	else
		kref_put(&rdata->refcount, cifs_readdata_release);

	cifs_small_buf_release(smb);
	return rc;
}

int
CIFSSMBRead(const unsigned int xid, struct cifs_io_parms *io_parms,
	    unsigned int *nbytes, char **buf, int *pbuf_type)
{
	int rc = -EACCES;
	READ_REQ *pSMB = NULL;
	READ_RSP *pSMBr = NULL;
	char *pReadData = NULL;
	int wct;
	int resp_buf_type = 0;
	struct kvec iov[1];
	__u32 pid = io_parms->pid;
	__u16 netfid = io_parms->netfid;
	__u64 offset = io_parms->offset;
	struct cifs_tcon *tcon = io_parms->tcon;
	unsigned int count = io_parms->length;

	cFYI(1, "Reading %d bytes on fid %d", count, netfid);
	if (tcon->ses->capabilities & CAP_LARGE_FILES)
		wct = 12;
	else {
		wct = 10; /* old style read */
		if ((offset >> 32) > 0)  {
			/* can not handle this big offset for old */
			return -EIO;
		}
	}

	*nbytes = 0;
	rc = small_smb_init(SMB_COM_READ_ANDX, wct, tcon, (void **) &pSMB);
	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid >> 16));

	/* tcon and ses pointer are checked in smb_init */
	if (tcon->ses->server == NULL)
		return -ECONNABORTED;

	pSMB->AndXCommand = 0xFF;       /* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(offset & 0xFFFFFFFF);
	if (wct == 12)
		pSMB->OffsetHigh = cpu_to_le32(offset >> 32);

	pSMB->Remaining = 0;
	pSMB->MaxCount = cpu_to_le16(count & 0xFFFF);
	pSMB->MaxCountHigh = cpu_to_le32(count >> 16);
	if (wct == 12)
		pSMB->ByteCount = 0;  /* no need to do le conversion since 0 */
	else {
		/* old style read */
		struct smb_com_readx_req *pSMBW =
			(struct smb_com_readx_req *)pSMB;
		pSMBW->ByteCount = 0;
	}

	iov[0].iov_base = (char *)pSMB;
	iov[0].iov_len = be32_to_cpu(pSMB->hdr.smb_buf_length) + 4;
	rc = SendReceive2(xid, tcon->ses, iov, 1 /* num iovecs */,
			 &resp_buf_type, CIFS_LOG_ERROR);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_reads);
	pSMBr = (READ_RSP *)iov[0].iov_base;
	if (rc) {
		cERROR(1, "Send error in read = %d", rc);
	} else {
		int data_length = le16_to_cpu(pSMBr->DataLengthHigh);
		data_length = data_length << 16;
		data_length += le16_to_cpu(pSMBr->DataLength);
		*nbytes = data_length;

		/*check that DataLength would not go beyond end of SMB */
		if ((data_length > CIFSMaxBufSize)
				|| (data_length > count)) {
			cFYI(1, "bad length %d for count %d",
				 data_length, count);
			rc = -EIO;
			*nbytes = 0;
		} else {
			pReadData = (char *) (&pSMBr->hdr.Protocol) +
					le16_to_cpu(pSMBr->DataOffset);
/*			if (rc = copy_to_user(buf, pReadData, data_length)) {
				cERROR(1, "Faulting on read rc = %d",rc);
				rc = -EFAULT;
			}*/ /* can not use copy_to_user when using page cache*/
			if (*buf)
				memcpy(*buf, pReadData, data_length);
		}
	}

/*	cifs_small_buf_release(pSMB); */ /* Freed earlier now in SendReceive2 */
	if (*buf) {
		if (resp_buf_type == CIFS_SMALL_BUFFER)
			cifs_small_buf_release(iov[0].iov_base);
		else if (resp_buf_type == CIFS_LARGE_BUFFER)
			cifs_buf_release(iov[0].iov_base);
	} else if (resp_buf_type != CIFS_NO_BUFFER) {
		/* return buffer to caller to free */
		*buf = iov[0].iov_base;
		if (resp_buf_type == CIFS_SMALL_BUFFER)
			*pbuf_type = CIFS_SMALL_BUFFER;
		else if (resp_buf_type == CIFS_LARGE_BUFFER)
			*pbuf_type = CIFS_LARGE_BUFFER;
	} /* else no valid buffer on return - leave as null */

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */
	return rc;
}


int
CIFSSMBWrite(const unsigned int xid, struct cifs_io_parms *io_parms,
	     unsigned int *nbytes, const char *buf,
	     const char __user *ubuf, const int long_op)
{
	int rc = -EACCES;
	WRITE_REQ *pSMB = NULL;
	WRITE_RSP *pSMBr = NULL;
	int bytes_returned, wct;
	__u32 bytes_sent;
	__u16 byte_count;
	__u32 pid = io_parms->pid;
	__u16 netfid = io_parms->netfid;
	__u64 offset = io_parms->offset;
	struct cifs_tcon *tcon = io_parms->tcon;
	unsigned int count = io_parms->length;

	*nbytes = 0;

	/* cFYI(1, "write at %lld %d bytes", offset, count);*/
	if (tcon->ses == NULL)
		return -ECONNABORTED;

	if (tcon->ses->capabilities & CAP_LARGE_FILES)
		wct = 14;
	else {
		wct = 12;
		if ((offset >> 32) > 0) {
			/* can not handle big offset for old srv */
			return -EIO;
		}
	}

	rc = smb_init(SMB_COM_WRITE_ANDX, wct, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid >> 16));

	/* tcon and ses pointer are checked in smb_init */
	if (tcon->ses->server == NULL)
		return -ECONNABORTED;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(offset & 0xFFFFFFFF);
	if (wct == 14)
		pSMB->OffsetHigh = cpu_to_le32(offset >> 32);

	pSMB->Reserved = 0xFFFFFFFF;
	pSMB->WriteMode = 0;
	pSMB->Remaining = 0;

	/* Can increase buffer size if buffer is big enough in some cases ie we
	can send more if LARGE_WRITE_X capability returned by the server and if
	our buffer is big enough or if we convert to iovecs on socket writes
	and eliminate the copy to the CIFS buffer */
	if (tcon->ses->capabilities & CAP_LARGE_WRITE_X) {
		bytes_sent = min_t(const unsigned int, CIFSMaxBufSize, count);
	} else {
		bytes_sent = (tcon->ses->server->maxBuf - MAX_CIFS_HDR_SIZE)
			 & ~0xFF;
	}

	if (bytes_sent > count)
		bytes_sent = count;
	pSMB->DataOffset =
		cpu_to_le16(offsetof(struct smb_com_write_req, Data) - 4);
	if (buf)
		memcpy(pSMB->Data, buf, bytes_sent);
	else if (ubuf) {
		if (copy_from_user(pSMB->Data, ubuf, bytes_sent)) {
			cifs_buf_release(pSMB);
			return -EFAULT;
		}
	} else if (count != 0) {
		/* No buffer */
		cifs_buf_release(pSMB);
		return -EINVAL;
	} /* else setting file size with write of zero bytes */
	if (wct == 14)
		byte_count = bytes_sent + 1; /* pad */
	else /* wct == 12 */
		byte_count = bytes_sent + 5; /* bigger pad, smaller smb hdr */

	pSMB->DataLengthLow = cpu_to_le16(bytes_sent & 0xFFFF);
	pSMB->DataLengthHigh = cpu_to_le16(bytes_sent >> 16);
	inc_rfc1001_len(pSMB, byte_count);

	if (wct == 14)
		pSMB->ByteCount = cpu_to_le16(byte_count);
	else { /* old style write has byte count 4 bytes earlier
		  so 4 bytes pad  */
		struct smb_com_writex_req *pSMBW =
			(struct smb_com_writex_req *)pSMB;
		pSMBW->ByteCount = cpu_to_le16(byte_count);
	}

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, long_op);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_writes);
	if (rc) {
		cFYI(1, "Send error in write = %d", rc);
	} else {
		*nbytes = le16_to_cpu(pSMBr->CountHigh);
		*nbytes = (*nbytes) << 16;
		*nbytes += le16_to_cpu(pSMBr->Count);

		/*
		 * Mask off high 16 bits when bytes written as returned by the
		 * server is greater than bytes requested by the client. Some
		 * OS/2 servers are known to set incorrect CountHigh values.
		 */
		if (*nbytes > count)
			*nbytes &= 0xFFFF;
	}

	cifs_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

void
cifs_writedata_release(struct kref *refcount)
{
	struct cifs_writedata *wdata = container_of(refcount,
					struct cifs_writedata, refcount);

	if (wdata->cfile)
		cifsFileInfo_put(wdata->cfile);

	kfree(wdata);
}

/*
 * Write failed with a retryable error. Resend the write request. It's also
 * possible that the page was redirtied so re-clean the page.
 */
static void
cifs_writev_requeue(struct cifs_writedata *wdata)
{
	int i, rc;
	struct inode *inode = wdata->cfile->dentry->d_inode;
	struct TCP_Server_Info *server;

	for (i = 0; i < wdata->nr_pages; i++) {
		lock_page(wdata->pages[i]);
		clear_page_dirty_for_io(wdata->pages[i]);
	}

	do {
		server = tlink_tcon(wdata->cfile->tlink)->ses->server;
		rc = server->ops->async_writev(wdata);
	} while (rc == -EAGAIN);

	for (i = 0; i < wdata->nr_pages; i++) {
		if (rc != 0)
			SetPageError(wdata->pages[i]);
		unlock_page(wdata->pages[i]);
	}

	mapping_set_error(inode->i_mapping, rc);
	kref_put(&wdata->refcount, cifs_writedata_release);
}

void
cifs_writev_complete(struct work_struct *work)
{
	struct cifs_writedata *wdata = container_of(work,
						struct cifs_writedata, work);
	struct inode *inode = wdata->cfile->dentry->d_inode;
	int i = 0;

	if (wdata->result == 0) {
		spin_lock(&inode->i_lock);
		cifs_update_eof(CIFS_I(inode), wdata->offset, wdata->bytes);
		spin_unlock(&inode->i_lock);
		cifs_stats_bytes_written(tlink_tcon(wdata->cfile->tlink),
					 wdata->bytes);
	} else if (wdata->sync_mode == WB_SYNC_ALL && wdata->result == -EAGAIN)
		return cifs_writev_requeue(wdata);

	for (i = 0; i < wdata->nr_pages; i++) {
		struct page *page = wdata->pages[i];
		if (wdata->result == -EAGAIN)
			__set_page_dirty_nobuffers(page);
		else if (wdata->result < 0)
			SetPageError(page);
		end_page_writeback(page);
		page_cache_release(page);
	}
	if (wdata->result != -EAGAIN)
		mapping_set_error(inode->i_mapping, wdata->result);
	kref_put(&wdata->refcount, cifs_writedata_release);
}

struct cifs_writedata *
cifs_writedata_alloc(unsigned int nr_pages, work_func_t complete)
{
	struct cifs_writedata *wdata;

	/* this would overflow */
	if (nr_pages == 0) {
		cERROR(1, "%s: called with nr_pages == 0!", __func__);
		return NULL;
	}

	/* writedata + number of page pointers */
	wdata = kzalloc(sizeof(*wdata) +
			sizeof(struct page *) * (nr_pages - 1), GFP_NOFS);
	if (wdata != NULL) {
		kref_init(&wdata->refcount);
		INIT_LIST_HEAD(&wdata->list);
		init_completion(&wdata->done);
		INIT_WORK(&wdata->work, complete);
	}
	return wdata;
}

/*
 * Check the mid_state and signature on received buffer (if any), and queue the
 * workqueue completion task.
 */
static void
cifs_writev_callback(struct mid_q_entry *mid)
{
	struct cifs_writedata *wdata = mid->callback_data;
	struct cifs_tcon *tcon = tlink_tcon(wdata->cfile->tlink);
	unsigned int written;
	WRITE_RSP *smb = (WRITE_RSP *)mid->resp_buf;

	switch (mid->mid_state) {
	case MID_RESPONSE_RECEIVED:
		wdata->result = cifs_check_receive(mid, tcon->ses->server, 0);
		if (wdata->result != 0)
			break;

		written = le16_to_cpu(smb->CountHigh);
		written <<= 16;
		written += le16_to_cpu(smb->Count);
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
	default:
		wdata->result = -EIO;
		break;
	}

	queue_work(cifsiod_wq, &wdata->work);
	DeleteMidQEntry(mid);
	add_credits(tcon->ses->server, 1, 0);
}

/* cifs_async_writev - send an async write, and set up mid to handle result */
int
cifs_async_writev(struct cifs_writedata *wdata)
{
	int rc = -EACCES;
	WRITE_REQ *smb = NULL;
	int wct;
	struct cifs_tcon *tcon = tlink_tcon(wdata->cfile->tlink);
	struct kvec iov;
	struct smb_rqst rqst = { };

	if (tcon->ses->capabilities & CAP_LARGE_FILES) {
		wct = 14;
	} else {
		wct = 12;
		if (wdata->offset >> 32 > 0) {
			/* can not handle big offset for old srv */
			return -EIO;
		}
	}

	rc = small_smb_init(SMB_COM_WRITE_ANDX, wct, tcon, (void **)&smb);
	if (rc)
		goto async_writev_out;

	smb->hdr.Pid = cpu_to_le16((__u16)wdata->pid);
	smb->hdr.PidHigh = cpu_to_le16((__u16)(wdata->pid >> 16));

	smb->AndXCommand = 0xFF;	/* none */
	smb->Fid = wdata->cfile->fid.netfid;
	smb->OffsetLow = cpu_to_le32(wdata->offset & 0xFFFFFFFF);
	if (wct == 14)
		smb->OffsetHigh = cpu_to_le32(wdata->offset >> 32);
	smb->Reserved = 0xFFFFFFFF;
	smb->WriteMode = 0;
	smb->Remaining = 0;

	smb->DataOffset =
	    cpu_to_le16(offsetof(struct smb_com_write_req, Data) - 4);

	/* 4 for RFC1001 length + 1 for BCC */
	iov.iov_len = be32_to_cpu(smb->hdr.smb_buf_length) + 4 + 1;
	iov.iov_base = smb;

	rqst.rq_iov = &iov;
	rqst.rq_nvec = 1;
	rqst.rq_pages = wdata->pages;
	rqst.rq_npages = wdata->nr_pages;
	rqst.rq_pagesz = wdata->pagesz;
	rqst.rq_tailsz = wdata->tailsz;

	cFYI(1, "async write at %llu %u bytes", wdata->offset, wdata->bytes);

	smb->DataLengthLow = cpu_to_le16(wdata->bytes & 0xFFFF);
	smb->DataLengthHigh = cpu_to_le16(wdata->bytes >> 16);

	if (wct == 14) {
		inc_rfc1001_len(&smb->hdr, wdata->bytes + 1);
		put_bcc(wdata->bytes + 1, &smb->hdr);
	} else {
		/* wct == 12 */
		struct smb_com_writex_req *smbw =
				(struct smb_com_writex_req *)smb;
		inc_rfc1001_len(&smbw->hdr, wdata->bytes + 5);
		put_bcc(wdata->bytes + 5, &smbw->hdr);
		iov.iov_len += 4; /* pad bigger by four bytes */
	}

	kref_get(&wdata->refcount);
	rc = cifs_call_async(tcon->ses->server, &rqst, NULL,
				cifs_writev_callback, wdata, 0);

	if (rc == 0)
		cifs_stats_inc(&tcon->stats.cifs_stats.num_writes);
	else
		kref_put(&wdata->refcount, cifs_writedata_release);

async_writev_out:
	cifs_small_buf_release(smb);
	return rc;
}

int
CIFSSMBWrite2(const unsigned int xid, struct cifs_io_parms *io_parms,
	      unsigned int *nbytes, struct kvec *iov, int n_vec)
{
	int rc = -EACCES;
	WRITE_REQ *pSMB = NULL;
	int wct;
	int smb_hdr_len;
	int resp_buf_type = 0;
	__u32 pid = io_parms->pid;
	__u16 netfid = io_parms->netfid;
	__u64 offset = io_parms->offset;
	struct cifs_tcon *tcon = io_parms->tcon;
	unsigned int count = io_parms->length;

	*nbytes = 0;

	cFYI(1, "write2 at %lld %d bytes", (long long)offset, count);

	if (tcon->ses->capabilities & CAP_LARGE_FILES) {
		wct = 14;
	} else {
		wct = 12;
		if ((offset >> 32) > 0) {
			/* can not handle big offset for old srv */
			return -EIO;
		}
	}
	rc = small_smb_init(SMB_COM_WRITE_ANDX, wct, tcon, (void **) &pSMB);
	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid >> 16));

	/* tcon and ses pointer are checked in smb_init */
	if (tcon->ses->server == NULL)
		return -ECONNABORTED;

	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = netfid;
	pSMB->OffsetLow = cpu_to_le32(offset & 0xFFFFFFFF);
	if (wct == 14)
		pSMB->OffsetHigh = cpu_to_le32(offset >> 32);
	pSMB->Reserved = 0xFFFFFFFF;
	pSMB->WriteMode = 0;
	pSMB->Remaining = 0;

	pSMB->DataOffset =
	    cpu_to_le16(offsetof(struct smb_com_write_req, Data) - 4);

	pSMB->DataLengthLow = cpu_to_le16(count & 0xFFFF);
	pSMB->DataLengthHigh = cpu_to_le16(count >> 16);
	/* header + 1 byte pad */
	smb_hdr_len = be32_to_cpu(pSMB->hdr.smb_buf_length) + 1;
	if (wct == 14)
		inc_rfc1001_len(pSMB, count + 1);
	else /* wct == 12 */
		inc_rfc1001_len(pSMB, count + 5); /* smb data starts later */
	if (wct == 14)
		pSMB->ByteCount = cpu_to_le16(count + 1);
	else /* wct == 12 */ /* bigger pad, smaller smb hdr, keep offset ok */ {
		struct smb_com_writex_req *pSMBW =
				(struct smb_com_writex_req *)pSMB;
		pSMBW->ByteCount = cpu_to_le16(count + 5);
	}
	iov[0].iov_base = pSMB;
	if (wct == 14)
		iov[0].iov_len = smb_hdr_len + 4;
	else /* wct == 12 pad bigger by four bytes */
		iov[0].iov_len = smb_hdr_len + 8;


	rc = SendReceive2(xid, tcon->ses, iov, n_vec + 1, &resp_buf_type, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_writes);
	if (rc) {
		cFYI(1, "Send error Write2 = %d", rc);
	} else if (resp_buf_type == 0) {
		/* presumably this can not happen, but best to be safe */
		rc = -EIO;
	} else {
		WRITE_RSP *pSMBr = (WRITE_RSP *)iov[0].iov_base;
		*nbytes = le16_to_cpu(pSMBr->CountHigh);
		*nbytes = (*nbytes) << 16;
		*nbytes += le16_to_cpu(pSMBr->Count);

		/*
		 * Mask off high 16 bits when bytes written as returned by the
		 * server is greater than bytes requested by the client. OS/2
		 * servers are known to set incorrect CountHigh values.
		 */
		if (*nbytes > count)
			*nbytes &= 0xFFFF;
	}

/*	cifs_small_buf_release(pSMB); */ /* Freed earlier now in SendReceive2 */
	if (resp_buf_type == CIFS_SMALL_BUFFER)
		cifs_small_buf_release(iov[0].iov_base);
	else if (resp_buf_type == CIFS_LARGE_BUFFER)
		cifs_buf_release(iov[0].iov_base);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

int cifs_lockv(const unsigned int xid, struct cifs_tcon *tcon,
	       const __u16 netfid, const __u8 lock_type, const __u32 num_unlock,
	       const __u32 num_lock, LOCKING_ANDX_RANGE *buf)
{
	int rc = 0;
	LOCK_REQ *pSMB = NULL;
	struct kvec iov[2];
	int resp_buf_type;
	__u16 count;

	cFYI(1, "cifs_lockv num lock %d num unlock %d", num_lock, num_unlock);

	rc = small_smb_init(SMB_COM_LOCKING_ANDX, 8, tcon, (void **) &pSMB);
	if (rc)
		return rc;

	pSMB->Timeout = 0;
	pSMB->NumberOfLocks = cpu_to_le16(num_lock);
	pSMB->NumberOfUnlocks = cpu_to_le16(num_unlock);
	pSMB->LockType = lock_type;
	pSMB->AndXCommand = 0xFF; /* none */
	pSMB->Fid = netfid; /* netfid stays le */

	count = (num_unlock + num_lock) * sizeof(LOCKING_ANDX_RANGE);
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	iov[0].iov_base = (char *)pSMB;
	iov[0].iov_len = be32_to_cpu(pSMB->hdr.smb_buf_length) + 4 -
			 (num_unlock + num_lock) * sizeof(LOCKING_ANDX_RANGE);
	iov[1].iov_base = (char *)buf;
	iov[1].iov_len = (num_unlock + num_lock) * sizeof(LOCKING_ANDX_RANGE);

	cifs_stats_inc(&tcon->stats.cifs_stats.num_locks);
	rc = SendReceive2(xid, tcon->ses, iov, 2, &resp_buf_type, CIFS_NO_RESP);
	if (rc)
		cFYI(1, "Send error in cifs_lockv = %d", rc);

	return rc;
}

int
CIFSSMBLock(const unsigned int xid, struct cifs_tcon *tcon,
	    const __u16 smb_file_id, const __u32 netpid, const __u64 len,
	    const __u64 offset, const __u32 numUnlock,
	    const __u32 numLock, const __u8 lockType,
	    const bool waitFlag, const __u8 oplock_level)
{
	int rc = 0;
	LOCK_REQ *pSMB = NULL;
/*	LOCK_RSP *pSMBr = NULL; */ /* No response data other than rc to parse */
	int bytes_returned;
	int flags = 0;
	__u16 count;

	cFYI(1, "CIFSSMBLock timeout %d numLock %d", (int)waitFlag, numLock);
	rc = small_smb_init(SMB_COM_LOCKING_ANDX, 8, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	if (lockType == LOCKING_ANDX_OPLOCK_RELEASE) {
		/* no response expected */
		flags = CIFS_ASYNC_OP | CIFS_OBREAK_OP;
		pSMB->Timeout = 0;
	} else if (waitFlag) {
		flags = CIFS_BLOCKING_OP; /* blocking operation, no timeout */
		pSMB->Timeout = cpu_to_le32(-1);/* blocking - do not time out */
	} else {
		pSMB->Timeout = 0;
	}

	pSMB->NumberOfLocks = cpu_to_le16(numLock);
	pSMB->NumberOfUnlocks = cpu_to_le16(numUnlock);
	pSMB->LockType = lockType;
	pSMB->OplockLevel = oplock_level;
	pSMB->AndXCommand = 0xFF;	/* none */
	pSMB->Fid = smb_file_id; /* netfid stays le */

	if ((numLock != 0) || (numUnlock != 0)) {
		pSMB->Locks[0].Pid = cpu_to_le16(netpid);
		/* BB where to store pid high? */
		pSMB->Locks[0].LengthLow = cpu_to_le32((u32)len);
		pSMB->Locks[0].LengthHigh = cpu_to_le32((u32)(len>>32));
		pSMB->Locks[0].OffsetLow = cpu_to_le32((u32)offset);
		pSMB->Locks[0].OffsetHigh = cpu_to_le32((u32)(offset>>32));
		count = sizeof(LOCKING_ANDX_RANGE);
	} else {
		/* oplock break */
		count = 0;
	}
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	if (waitFlag) {
		rc = SendReceiveBlockingLock(xid, tcon, (struct smb_hdr *) pSMB,
			(struct smb_hdr *) pSMB, &bytes_returned);
		cifs_small_buf_release(pSMB);
	} else {
		rc = SendReceiveNoRsp(xid, tcon->ses, (char *)pSMB, flags);
		/* SMB buffer freed by function above */
	}
	cifs_stats_inc(&tcon->stats.cifs_stats.num_locks);
	if (rc)
		cFYI(1, "Send error in Lock = %d", rc);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
	since file handle passed in no longer valid */
	return rc;
}

int
CIFSSMBPosixLock(const unsigned int xid, struct cifs_tcon *tcon,
		const __u16 smb_file_id, const __u32 netpid,
		const loff_t start_offset, const __u64 len,
		struct file_lock *pLockData, const __u16 lock_type,
		const bool waitFlag)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct smb_com_transaction2_sfi_rsp *pSMBr = NULL;
	struct cifs_posix_lock *parm_data;
	int rc = 0;
	int timeout = 0;
	int bytes_returned = 0;
	int resp_buf_type = 0;
	__u16 params, param_offset, offset, byte_count, count;
	struct kvec iov[1];

	cFYI(1, "Posix Lock");

	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMBr = (struct smb_com_transaction2_sfi_rsp *)pSMB;

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	count = sizeof(struct cifs_posix_lock);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000); /* BB find max SMB from sess */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	if (pLockData)
		pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FILE_INFORMATION);
	else
		pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	parm_data = (struct cifs_posix_lock *)
			(((char *) &pSMB->hdr.Protocol) + offset);

	parm_data->lock_type = cpu_to_le16(lock_type);
	if (waitFlag) {
		timeout = CIFS_BLOCKING_OP; /* blocking operation, no timeout */
		parm_data->lock_flags = cpu_to_le16(1);
		pSMB->Timeout = cpu_to_le32(-1);
	} else
		pSMB->Timeout = 0;

	parm_data->pid = cpu_to_le32(netpid);
	parm_data->start = cpu_to_le64(start_offset);
	parm_data->length = cpu_to_le64(len);  /* normalize negative numbers */

	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->Fid = smb_file_id;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_POSIX_LOCK);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	if (waitFlag) {
		rc = SendReceiveBlockingLock(xid, tcon, (struct smb_hdr *) pSMB,
			(struct smb_hdr *) pSMBr, &bytes_returned);
	} else {
		iov[0].iov_base = (char *)pSMB;
		iov[0].iov_len = be32_to_cpu(pSMB->hdr.smb_buf_length) + 4;
		rc = SendReceive2(xid, tcon->ses, iov, 1 /* num iovecs */,
				&resp_buf_type, timeout);
		pSMB = NULL; /* request buf already freed by SendReceive2. Do
				not try to free it twice below on exit */
		pSMBr = (struct smb_com_transaction2_sfi_rsp *)iov[0].iov_base;
	}

	if (rc) {
		cFYI(1, "Send error in Posix Lock = %d", rc);
	} else if (pLockData) {
		/* lock structure can be returned on get */
		__u16 data_offset;
		__u16 data_count;
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < sizeof(*parm_data)) {
			rc = -EIO;      /* bad smb */
			goto plk_err_exit;
		}
		data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
		data_count  = le16_to_cpu(pSMBr->t2.DataCount);
		if (data_count < sizeof(struct cifs_posix_lock)) {
			rc = -EIO;
			goto plk_err_exit;
		}
		parm_data = (struct cifs_posix_lock *)
			((char *)&pSMBr->hdr.Protocol + data_offset);
		if (parm_data->lock_type == __constant_cpu_to_le16(CIFS_UNLCK))
			pLockData->fl_type = F_UNLCK;
		else {
			if (parm_data->lock_type ==
					__constant_cpu_to_le16(CIFS_RDLCK))
				pLockData->fl_type = F_RDLCK;
			else if (parm_data->lock_type ==
					__constant_cpu_to_le16(CIFS_WRLCK))
				pLockData->fl_type = F_WRLCK;

			pLockData->fl_start = le64_to_cpu(parm_data->start);
			pLockData->fl_end = pLockData->fl_start +
					le64_to_cpu(parm_data->length) - 1;
			pLockData->fl_pid = le32_to_cpu(parm_data->pid);
		}
	}

plk_err_exit:
	if (pSMB)
		cifs_small_buf_release(pSMB);

	if (resp_buf_type == CIFS_SMALL_BUFFER)
		cifs_small_buf_release(iov[0].iov_base);
	else if (resp_buf_type == CIFS_LARGE_BUFFER)
		cifs_buf_release(iov[0].iov_base);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
	   since file handle passed in no longer valid */

	return rc;
}


int
CIFSSMBClose(const unsigned int xid, struct cifs_tcon *tcon, int smb_file_id)
{
	int rc = 0;
	CLOSE_REQ *pSMB = NULL;
	cFYI(1, "In CIFSSMBClose");

/* do not retry on dead session on close */
	rc = small_smb_init(SMB_COM_CLOSE, 3, tcon, (void **) &pSMB);
	if (rc == -EAGAIN)
		return 0;
	if (rc)
		return rc;

	pSMB->FileID = (__u16) smb_file_id;
	pSMB->LastWriteTime = 0xFFFFFFFF;
	pSMB->ByteCount = 0;
	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_closes);
	if (rc) {
		if (rc != -EINTR) {
			/* EINTR is expected when user ctl-c to kill app */
			cERROR(1, "Send error in Close = %d", rc);
		}
	}

	/* Since session is dead, file will be closed on server already */
	if (rc == -EAGAIN)
		rc = 0;

	return rc;
}

int
CIFSSMBFlush(const unsigned int xid, struct cifs_tcon *tcon, int smb_file_id)
{
	int rc = 0;
	FLUSH_REQ *pSMB = NULL;
	cFYI(1, "In CIFSSMBFlush");

	rc = small_smb_init(SMB_COM_FLUSH, 1, tcon, (void **) &pSMB);
	if (rc)
		return rc;

	pSMB->FileID = (__u16) smb_file_id;
	pSMB->ByteCount = 0;
	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_flushes);
	if (rc)
		cERROR(1, "Send error in Flush = %d", rc);

	return rc;
}

int
CIFSSMBRename(const unsigned int xid, struct cifs_tcon *tcon,
	      const char *from_name, const char *to_name,
	      struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	RENAME_REQ *pSMB = NULL;
	RENAME_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;
	__u16 count;
	int remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

	cFYI(1, "In CIFSSMBRename");
renameRetry:
	rc = smb_init(SMB_COM_RENAME, 1, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->BufferFormat = 0x04;
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUTF16((__le16 *) pSMB->OldFileName,
					      from_name, PATH_MAX,
					      cifs_sb->local_nls, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0x04;	/* pad */
	/* protocol requires ASCII signature byte on Unicode string */
		pSMB->OldFileName[name_len + 1] = 0x00;
		name_len2 =
		    cifsConvertToUTF16((__le16 *)&pSMB->OldFileName[name_len+2],
				       to_name, PATH_MAX, cifs_sb->local_nls,
				       remap);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2;	/* convert to bytes */
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(from_name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->OldFileName, from_name, name_len);
		name_len2 = strnlen(to_name, PATH_MAX);
		name_len2++;	/* trailing null */
		pSMB->OldFileName[name_len] = 0x04;  /* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], to_name, name_len2);
		name_len2++;	/* trailing null */
		name_len2++;	/* signature byte */
	}

	count = 1 /* 1st signature byte */  + name_len + name_len2;
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_renames);
	if (rc)
		cFYI(1, "Send error in rename = %d", rc);

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto renameRetry;

	return rc;
}

int CIFSSMBRenameOpenFile(const unsigned int xid, struct cifs_tcon *pTcon,
		int netfid, const char *target_name,
		const struct nls_table *nls_codepage, int remap)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct smb_com_transaction2_sfi_rsp *pSMBr = NULL;
	struct set_file_rename *rename_info;
	char *data_offset;
	char dummy_string[30];
	int rc = 0;
	int bytes_returned = 0;
	int len_of_str;
	__u16 params, param_offset, offset, count, byte_count;

	cFYI(1, "Rename to File by handle");
	rc = smb_init(SMB_COM_TRANSACTION2, 15, pTcon, (void **) &pSMB,
			(void **) &pSMBr);
	if (rc)
		return rc;

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	rename_info = (struct set_file_rename *) data_offset;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000); /* BB find max SMB from sess */
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	/* construct random name ".cifs_tmp<inodenum><mid>" */
	rename_info->overwrite = cpu_to_le32(1);
	rename_info->root_fid  = 0;
	/* unicode only call */
	if (target_name == NULL) {
		sprintf(dummy_string, "cifs%x", pSMB->hdr.Mid);
		len_of_str =
			cifsConvertToUTF16((__le16 *)rename_info->target_name,
					dummy_string, 24, nls_codepage, remap);
	} else {
		len_of_str =
			cifsConvertToUTF16((__le16 *)rename_info->target_name,
					target_name, PATH_MAX, nls_codepage,
					remap);
	}
	rename_info->target_name_len = cpu_to_le32(2 * len_of_str);
	count = 12 /* sizeof(struct set_file_rename) */ + (2 * len_of_str);
	byte_count += count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->Fid = netfid;
	pSMB->InformationLevel =
		cpu_to_le16(SMB_SET_FILE_RENAME_INFORMATION);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, pTcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&pTcon->stats.cifs_stats.num_t2renames);
	if (rc)
		cFYI(1, "Send error in Rename (by file handle) = %d", rc);

	cifs_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

int
CIFSSMBCopy(const unsigned int xid, struct cifs_tcon *tcon,
	    const char *fromName, const __u16 target_tid, const char *toName,
	    const int flags, const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	COPY_REQ *pSMB = NULL;
	COPY_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;
	__u16 count;

	cFYI(1, "In CIFSSMBCopy");
copyRetry:
	rc = smb_init(SMB_COM_COPY, 1, tcon, (void **) &pSMB,
			(void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->BufferFormat = 0x04;
	pSMB->Tid2 = target_tid;

	pSMB->Flags = cpu_to_le16(flags & COPY_TREE);

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUTF16((__le16 *) pSMB->OldFileName,
					      fromName, PATH_MAX, nls_codepage,
					      remap);
		name_len++;     /* trailing null */
		name_len *= 2;
		pSMB->OldFileName[name_len] = 0x04;     /* pad */
		/* protocol requires ASCII signature byte on Unicode string */
		pSMB->OldFileName[name_len + 1] = 0x00;
		name_len2 =
		    cifsConvertToUTF16((__le16 *)&pSMB->OldFileName[name_len+2],
				       toName, PATH_MAX, nls_codepage, remap);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2; /* convert to bytes */
	} else { 	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->OldFileName, fromName, name_len);
		name_len2 = strnlen(toName, PATH_MAX);
		name_len2++;    /* trailing null */
		pSMB->OldFileName[name_len] = 0x04;  /* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], toName, name_len2);
		name_len2++;    /* trailing null */
		name_len2++;    /* signature byte */
	}

	count = 1 /* 1st signature byte */  + name_len + name_len2;
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in copy = %d with %d files copied",
			rc, le16_to_cpu(pSMBr->CopyCount));
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto copyRetry;

	return rc;
}

int
CIFSUnixCreateSymLink(const unsigned int xid, struct cifs_tcon *tcon,
		      const char *fromName, const char *toName,
		      const struct nls_table *nls_codepage)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	char *data_offset;
	int name_len;
	int name_len_target;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, "In Symlink Unix style");
createSymLinkRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifs_strtoUTF16((__le16 *) pSMB->FileName, fromName,
				    /* find define for this maxpathcomponent */
				    PATH_MAX, nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;

	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fromName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fromName, name_len);
	}
	params = 6 + name_len;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len_target =
		    cifs_strtoUTF16((__le16 *) data_offset, toName, PATH_MAX
				    /* find define for this maxpathcomponent */
				    , nls_codepage);
		name_len_target++;	/* trailing null */
		name_len_target *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len_target = strnlen(toName, PATH_MAX);
		name_len_target++;	/* trailing null */
		strncpy(data_offset, toName, name_len_target);
	}

	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max on data count below from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + name_len_target;
	pSMB->DataCount = cpu_to_le16(name_len_target);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_LINK);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_symlinks);
	if (rc)
		cFYI(1, "Send error in SetPathInfo create symlink = %d", rc);

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto createSymLinkRetry;

	return rc;
}

int
CIFSUnixCreateHardLink(const unsigned int xid, struct cifs_tcon *tcon,
		       const char *fromName, const char *toName,
		       const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	char *data_offset;
	int name_len;
	int name_len_target;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, "In Create Hard link Unix style");
createHardLinkRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len = cifsConvertToUTF16((__le16 *) pSMB->FileName, toName,
					      PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;

	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(toName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, toName, name_len);
	}
	params = 6 + name_len;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len_target =
		    cifsConvertToUTF16((__le16 *) data_offset, fromName,
				       PATH_MAX, nls_codepage, remap);
		name_len_target++;	/* trailing null */
		name_len_target *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len_target = strnlen(fromName, PATH_MAX);
		name_len_target++;	/* trailing null */
		strncpy(data_offset, fromName, name_len_target);
	}

	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max on data count below from sess*/
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + name_len_target;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->DataCount = cpu_to_le16(name_len_target);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_HLINK);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_hardlinks);
	if (rc)
		cFYI(1, "Send error in SetPathInfo (hard link) = %d", rc);

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto createHardLinkRetry;

	return rc;
}

int
CIFSCreateHardLink(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *from_name, const char *to_name,
		   struct cifs_sb_info *cifs_sb)
{
	int rc = 0;
	NT_RENAME_REQ *pSMB = NULL;
	RENAME_RSP *pSMBr = NULL;
	int bytes_returned;
	int name_len, name_len2;
	__u16 count;
	int remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

	cFYI(1, "In CIFSCreateHardLink");
winCreateHardLinkRetry:

	rc = smb_init(SMB_COM_NT_RENAME, 4, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->Flags = cpu_to_le16(CREATE_HARD_LINK);
	pSMB->ClusterCount = 0;

	pSMB->BufferFormat = 0x04;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->OldFileName, from_name,
				       PATH_MAX, cifs_sb->local_nls, remap);
		name_len++;	/* trailing null */
		name_len *= 2;

		/* protocol specifies ASCII buffer format (0x04) for unicode */
		pSMB->OldFileName[name_len] = 0x04;
		pSMB->OldFileName[name_len + 1] = 0x00; /* pad */
		name_len2 =
		    cifsConvertToUTF16((__le16 *)&pSMB->OldFileName[name_len+2],
				       to_name, PATH_MAX, cifs_sb->local_nls,
				       remap);
		name_len2 += 1 /* trailing null */  + 1 /* Signature word */ ;
		name_len2 *= 2;	/* convert to bytes */
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(from_name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->OldFileName, from_name, name_len);
		name_len2 = strnlen(to_name, PATH_MAX);
		name_len2++;	/* trailing null */
		pSMB->OldFileName[name_len] = 0x04;	/* 2nd buffer format */
		strncpy(&pSMB->OldFileName[name_len + 1], to_name, name_len2);
		name_len2++;	/* trailing null */
		name_len2++;	/* signature byte */
	}

	count = 1 /* string type byte */  + name_len + name_len2;
	inc_rfc1001_len(pSMB, count);
	pSMB->ByteCount = cpu_to_le16(count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_hardlinks);
	if (rc)
		cFYI(1, "Send error in hard link (NT rename) = %d", rc);

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto winCreateHardLinkRetry;

	return rc;
}

int
CIFSSMBUnixQuerySymLink(const unsigned int xid, struct cifs_tcon *tcon,
			const unsigned char *searchName, char **symlinkinfo,
			const struct nls_table *nls_codepage)
{
/* SMB_QUERY_FILE_UNIX_LINK */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;
	char *data_start;

	cFYI(1, "In QPathSymLinkInfo (Unix) for path %s", searchName);

querySymLinkRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifs_strtoUTF16((__le16 *) pSMB->FileName, searchName,
					PATH_MAX, nls_codepage);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(CIFSMaxBufSize);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	struct smb_com_transaction2_qpi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_LINK);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QuerySymLinkInfo = %d", rc);
	} else {
		/* decode response */

		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		/* BB also check enough total bytes returned */
		if (rc || get_bcc(&pSMBr->hdr) < 2)
			rc = -EIO;
		else {
			bool is_unicode;
			u16 count = le16_to_cpu(pSMBr->t2.DataCount);

			data_start = ((char *) &pSMBr->hdr.Protocol) +
					   le16_to_cpu(pSMBr->t2.DataOffset);

			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
				is_unicode = true;
			else
				is_unicode = false;

			/* BB FIXME investigate remapping reserved chars here */
			*symlinkinfo = cifs_strndup_from_utf16(data_start,
					count, is_unicode, nls_codepage);
			if (!*symlinkinfo)
				rc = -ENOMEM;
		}
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto querySymLinkRetry;
	return rc;
}

#ifdef CONFIG_CIFS_SYMLINK_EXPERIMENTAL
/*
 *	Recent Windows versions now create symlinks more frequently
 *	and they use the "reparse point" mechanism below.  We can of course
 *	do symlinks nicely to Samba and other servers which support the
 *	CIFS Unix Extensions and we can also do SFU symlinks and "client only"
 *	"MF" symlinks optionally, but for recent Windows we really need to
 *	reenable the code below and fix the cifs_symlink callers to handle this.
 *	In the interim this code has been moved to its own config option so
 *	it is not compiled in by default until callers fixed up and more tested.
 */
int
CIFSSMBQueryReparseLinkInfo(const unsigned int xid, struct cifs_tcon *tcon,
			const unsigned char *searchName,
			char *symlinkinfo, const int buflen, __u16 fid,
			const struct nls_table *nls_codepage)
{
	int rc = 0;
	int bytes_returned;
	struct smb_com_transaction_ioctl_req *pSMB;
	struct smb_com_transaction_ioctl_rsp *pSMBr;

	cFYI(1, "In Windows reparse style QueryLink for path %s", searchName);
	rc = smb_init(SMB_COM_NT_TRANSACT, 23, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 0 ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le32(2);
	/* BB find exact data count max from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le32(CIFSMaxBufSize & 0xFFFFFF00);
	pSMB->MaxSetupCount = 4;
	pSMB->Reserved = 0;
	pSMB->ParameterOffset = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 4;
	pSMB->SubCommand = cpu_to_le16(NT_TRANSACT_IOCTL);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->FunctionCode = cpu_to_le32(FSCTL_GET_REPARSE_POINT);
	pSMB->IsFsctl = 1; /* FSCTL */
	pSMB->IsRootFlag = 0;
	pSMB->Fid = fid; /* file handle always le */
	pSMB->ByteCount = 0;

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QueryReparseLinkInfo = %d", rc);
	} else {		/* decode response */
		__u32 data_offset = le32_to_cpu(pSMBr->DataOffset);
		__u32 data_count = le32_to_cpu(pSMBr->DataCount);
		if (get_bcc(&pSMBr->hdr) < 2 || data_offset > 512) {
			/* BB also check enough total bytes returned */
			rc = -EIO;	/* bad smb */
			goto qreparse_out;
		}
		if (data_count && (data_count < 2048)) {
			char *end_of_smb = 2 /* sizeof byte count */ +
			       get_bcc(&pSMBr->hdr) + (char *)&pSMBr->ByteCount;

			struct reparse_data *reparse_buf =
						(struct reparse_data *)
						((char *)&pSMBr->hdr.Protocol
								 + data_offset);
			if ((char *)reparse_buf >= end_of_smb) {
				rc = -EIO;
				goto qreparse_out;
			}
			if ((reparse_buf->LinkNamesBuf +
				reparse_buf->TargetNameOffset +
				reparse_buf->TargetNameLen) > end_of_smb) {
				cFYI(1, "reparse buf beyond SMB");
				rc = -EIO;
				goto qreparse_out;
			}

			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE) {
				cifs_from_ucs2(symlinkinfo, (__le16 *)
						(reparse_buf->LinkNamesBuf +
						reparse_buf->TargetNameOffset),
						buflen,
						reparse_buf->TargetNameLen,
						nls_codepage, 0);
			} else { /* ASCII names */
				strncpy(symlinkinfo,
					reparse_buf->LinkNamesBuf +
					reparse_buf->TargetNameOffset,
					min_t(const int, buflen,
					   reparse_buf->TargetNameLen));
			}
		} else {
			rc = -EIO;
			cFYI(1, "Invalid return data count on "
				 "get reparse info ioctl");
		}
		symlinkinfo[buflen] = 0; /* just in case so the caller
					does not go off the end of the buffer */
		cFYI(1, "readlink result - %s", symlinkinfo);
	}

qreparse_out:
	cifs_buf_release(pSMB);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}
#endif /* CIFS_SYMLINK_EXPERIMENTAL */ /* BB temporarily unused */

#ifdef CONFIG_CIFS_POSIX

/*Convert an Access Control Entry from wire format to local POSIX xattr format*/
static void cifs_convert_ace(posix_acl_xattr_entry *ace,
			     struct cifs_posix_ace *cifs_ace)
{
	/* u8 cifs fields do not need le conversion */
	ace->e_perm = cpu_to_le16(cifs_ace->cifs_e_perm);
	ace->e_tag  = cpu_to_le16(cifs_ace->cifs_e_tag);
	ace->e_id   = cpu_to_le32(le64_to_cpu(cifs_ace->cifs_uid));
	/* cFYI(1, "perm %d tag %d id %d",ace->e_perm,ace->e_tag,ace->e_id); */

	return;
}

/* Convert ACL from CIFS POSIX wire format to local Linux POSIX ACL xattr */
static int cifs_copy_posix_acl(char *trgt, char *src, const int buflen,
			       const int acl_type, const int size_of_data_area)
{
	int size =  0;
	int i;
	__u16 count;
	struct cifs_posix_ace *pACE;
	struct cifs_posix_acl *cifs_acl = (struct cifs_posix_acl *)src;
	posix_acl_xattr_header *local_acl = (posix_acl_xattr_header *)trgt;

	if (le16_to_cpu(cifs_acl->version) != CIFS_ACL_VERSION)
		return -EOPNOTSUPP;

	if (acl_type & ACL_TYPE_ACCESS) {
		count = le16_to_cpu(cifs_acl->access_entry_count);
		pACE = &cifs_acl->ace_array[0];
		size = sizeof(struct cifs_posix_acl);
		size += sizeof(struct cifs_posix_ace) * count;
		/* check if we would go beyond end of SMB */
		if (size_of_data_area < size) {
			cFYI(1, "bad CIFS POSIX ACL size %d vs. %d",
				size_of_data_area, size);
			return -EINVAL;
		}
	} else if (acl_type & ACL_TYPE_DEFAULT) {
		count = le16_to_cpu(cifs_acl->access_entry_count);
		size = sizeof(struct cifs_posix_acl);
		size += sizeof(struct cifs_posix_ace) * count;
/* skip past access ACEs to get to default ACEs */
		pACE = &cifs_acl->ace_array[count];
		count = le16_to_cpu(cifs_acl->default_entry_count);
		size += sizeof(struct cifs_posix_ace) * count;
		/* check if we would go beyond end of SMB */
		if (size_of_data_area < size)
			return -EINVAL;
	} else {
		/* illegal type */
		return -EINVAL;
	}

	size = posix_acl_xattr_size(count);
	if ((buflen == 0) || (local_acl == NULL)) {
		/* used to query ACL EA size */
	} else if (size > buflen) {
		return -ERANGE;
	} else /* buffer big enough */ {
		local_acl->a_version = cpu_to_le32(POSIX_ACL_XATTR_VERSION);
		for (i = 0; i < count ; i++) {
			cifs_convert_ace(&local_acl->a_entries[i], pACE);
			pACE++;
		}
	}
	return size;
}

static __u16 convert_ace_to_cifs_ace(struct cifs_posix_ace *cifs_ace,
				     const posix_acl_xattr_entry *local_ace)
{
	__u16 rc = 0; /* 0 = ACL converted ok */

	cifs_ace->cifs_e_perm = le16_to_cpu(local_ace->e_perm);
	cifs_ace->cifs_e_tag =  le16_to_cpu(local_ace->e_tag);
	/* BB is there a better way to handle the large uid? */
	if (local_ace->e_id == cpu_to_le32(-1)) {
	/* Probably no need to le convert -1 on any arch but can not hurt */
		cifs_ace->cifs_uid = cpu_to_le64(-1);
	} else
		cifs_ace->cifs_uid = cpu_to_le64(le32_to_cpu(local_ace->e_id));
	/*cFYI(1, "perm %d tag %d id %d",ace->e_perm,ace->e_tag,ace->e_id);*/
	return rc;
}

/* Convert ACL from local Linux POSIX xattr to CIFS POSIX ACL wire format */
static __u16 ACL_to_cifs_posix(char *parm_data, const char *pACL,
			       const int buflen, const int acl_type)
{
	__u16 rc = 0;
	struct cifs_posix_acl *cifs_acl = (struct cifs_posix_acl *)parm_data;
	posix_acl_xattr_header *local_acl = (posix_acl_xattr_header *)pACL;
	int count;
	int i;

	if ((buflen == 0) || (pACL == NULL) || (cifs_acl == NULL))
		return 0;

	count = posix_acl_xattr_count((size_t)buflen);
	cFYI(1, "setting acl with %d entries from buf of length %d and "
		"version of %d",
		count, buflen, le32_to_cpu(local_acl->a_version));
	if (le32_to_cpu(local_acl->a_version) != 2) {
		cFYI(1, "unknown POSIX ACL version %d",
		     le32_to_cpu(local_acl->a_version));
		return 0;
	}
	cifs_acl->version = cpu_to_le16(1);
	if (acl_type == ACL_TYPE_ACCESS)
		cifs_acl->access_entry_count = cpu_to_le16(count);
	else if (acl_type == ACL_TYPE_DEFAULT)
		cifs_acl->default_entry_count = cpu_to_le16(count);
	else {
		cFYI(1, "unknown ACL type %d", acl_type);
		return 0;
	}
	for (i = 0; i < count; i++) {
		rc = convert_ace_to_cifs_ace(&cifs_acl->ace_array[i],
					&local_acl->a_entries[i]);
		if (rc != 0) {
			/* ACE not converted */
			break;
		}
	}
	if (rc == 0) {
		rc = (__u16)(count * sizeof(struct cifs_posix_ace));
		rc += sizeof(struct cifs_posix_acl);
		/* BB add check to make sure ACL does not overflow SMB */
	}
	return rc;
}

int
CIFSSMBGetPosixACL(const unsigned int xid, struct cifs_tcon *tcon,
		   const unsigned char *searchName,
		   char *acl_inf, const int buflen, const int acl_type,
		   const struct nls_table *nls_codepage, int remap)
{
/* SMB_QUERY_POSIX_ACL */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;

	cFYI(1, "In GetPosixACL (Unix) for path %s", searchName);

queryAclRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		(void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUTF16((__le16 *) pSMB->FileName,
					   searchName, PATH_MAX, nls_codepage,
					   remap);
		name_len++;     /* trailing null */
		name_len *= 2;
		pSMB->FileName[name_len] = 0;
		pSMB->FileName[name_len+1] = 0;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max data count below from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(
		offsetof(struct smb_com_transaction2_qpi_req,
			 InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_POSIX_ACL);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_acl_get);
	if (rc) {
		cFYI(1, "Send error in Query POSIX ACL = %d", rc);
	} else {
		/* decode response */

		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		/* BB also check enough total bytes returned */
		if (rc || get_bcc(&pSMBr->hdr) < 2)
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			__u16 count = le16_to_cpu(pSMBr->t2.DataCount);
			rc = cifs_copy_posix_acl(acl_inf,
				(char *)&pSMBr->hdr.Protocol+data_offset,
				buflen, acl_type, count);
		}
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto queryAclRetry;
	return rc;
}

int
CIFSSMBSetPosixACL(const unsigned int xid, struct cifs_tcon *tcon,
		   const unsigned char *fileName,
		   const char *local_acl, const int buflen,
		   const int acl_type,
		   const struct nls_table *nls_codepage, int remap)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	char *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count, data_count, param_offset, offset;

	cFYI(1, "In SetPosixACL (Unix) for path %s", fileName);
setAclRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;
	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUTF16((__le16 *) pSMB->FileName, fileName,
					   PATH_MAX, nls_codepage, remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}
	params = 6 + name_len;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB size from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;
	parm_data = ((char *) &pSMB->hdr.Protocol) + offset;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);

	/* convert to on the wire format for POSIX ACL */
	data_count = ACL_to_cifs_posix(parm_data, local_acl, buflen, acl_type);

	if (data_count == 0) {
		rc = -EOPNOTSUPP;
		goto setACLerrorExit;
	}
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_POSIX_ACL);
	byte_count = 3 /* pad */  + params + data_count;
	pSMB->DataCount = cpu_to_le16(data_count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "Set POSIX ACL returned %d", rc);

setACLerrorExit:
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto setAclRetry;
	return rc;
}

/* BB fix tabs in this function FIXME BB */
int
CIFSGetExtAttr(const unsigned int xid, struct cifs_tcon *tcon,
	       const int netfid, __u64 *pExtAttrBits, __u64 *pMask)
{
	int rc = 0;
	struct smb_t2_qfi_req *pSMB = NULL;
	struct smb_t2_qfi_rsp *pSMBr = NULL;
	int bytes_returned;
	__u16 params, byte_count;

	cFYI(1, "In GetExtAttr");
	if (tcon == NULL)
		return -ENODEV;

GetExtAttrRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
			(void **) &pSMBr);
	if (rc)
		return rc;

	params = 2 /* level */ + 2 /* fid */;
	pSMB->t2.TotalDataCount = 0;
	pSMB->t2.MaxParameterCount = cpu_to_le16(4);
	/* BB find exact max data count below from sess structure BB */
	pSMB->t2.MaxDataCount = cpu_to_le16(4000);
	pSMB->t2.MaxSetupCount = 0;
	pSMB->t2.Reserved = 0;
	pSMB->t2.Flags = 0;
	pSMB->t2.Timeout = 0;
	pSMB->t2.Reserved2 = 0;
	pSMB->t2.ParameterOffset = cpu_to_le16(offsetof(struct smb_t2_qfi_req,
					       Fid) - 4);
	pSMB->t2.DataCount = 0;
	pSMB->t2.DataOffset = 0;
	pSMB->t2.SetupCount = 1;
	pSMB->t2.Reserved3 = 0;
	pSMB->t2.SubCommand = cpu_to_le16(TRANS2_QUERY_FILE_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->t2.TotalParameterCount = cpu_to_le16(params);
	pSMB->t2.ParameterCount = pSMB->t2.TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_ATTR_FLAGS);
	pSMB->Pad = 0;
	pSMB->Fid = netfid;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->t2.ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "error %d in GetExtAttr", rc);
	} else {
		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		/* BB also check enough total bytes returned */
		if (rc || get_bcc(&pSMBr->hdr) < 2)
			/* If rc should we check for EOPNOSUPP and
			   disable the srvino flag? or in caller? */
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			__u16 count = le16_to_cpu(pSMBr->t2.DataCount);
			struct file_chattr_info *pfinfo;
			/* BB Do we need a cast or hash here ? */
			if (count != 16) {
				cFYI(1, "Illegal size ret in GetExtAttr");
				rc = -EIO;
				goto GetExtAttrOut;
			}
			pfinfo = (struct file_chattr_info *)
				 (data_offset + (char *) &pSMBr->hdr.Protocol);
			*pExtAttrBits = le64_to_cpu(pfinfo->mode);
			*pMask = le64_to_cpu(pfinfo->mask);
		}
	}
GetExtAttrOut:
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto GetExtAttrRetry;
	return rc;
}

#endif /* CONFIG_POSIX */

#ifdef CONFIG_CIFS_ACL
/*
 * Initialize NT TRANSACT SMB into small smb request buffer.  This assumes that
 * all NT TRANSACTS that we init here have total parm and data under about 400
 * bytes (to fit in small cifs buffer size), which is the case so far, it
 * easily fits. NB: Setup words themselves and ByteCount MaxSetupCount (size of
 * returned setup area) and MaxParameterCount (returned parms size) must be set
 * by caller
 */
static int
smb_init_nttransact(const __u16 sub_command, const int setup_count,
		   const int parm_len, struct cifs_tcon *tcon,
		   void **ret_buf)
{
	int rc;
	__u32 temp_offset;
	struct smb_com_ntransact_req *pSMB;

	rc = small_smb_init(SMB_COM_NT_TRANSACT, 19 + setup_count, tcon,
				(void **)&pSMB);
	if (rc)
		return rc;
	*ret_buf = (void *)pSMB;
	pSMB->Reserved = 0;
	pSMB->TotalParameterCount = cpu_to_le32(parm_len);
	pSMB->TotalDataCount  = 0;
	pSMB->MaxDataCount = cpu_to_le32(CIFSMaxBufSize & 0xFFFFFF00);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->DataCount  = pSMB->TotalDataCount;
	temp_offset = offsetof(struct smb_com_ntransact_req, Parms) +
			(setup_count * 2) - 4 /* for rfc1001 length itself */;
	pSMB->ParameterOffset = cpu_to_le32(temp_offset);
	pSMB->DataOffset = cpu_to_le32(temp_offset + parm_len);
	pSMB->SetupCount = setup_count; /* no need to le convert byte fields */
	pSMB->SubCommand = cpu_to_le16(sub_command);
	return 0;
}

static int
validate_ntransact(char *buf, char **ppparm, char **ppdata,
		   __u32 *pparmlen, __u32 *pdatalen)
{
	char *end_of_smb;
	__u32 data_count, data_offset, parm_count, parm_offset;
	struct smb_com_ntransact_rsp *pSMBr;
	u16 bcc;

	*pdatalen = 0;
	*pparmlen = 0;

	if (buf == NULL)
		return -EINVAL;

	pSMBr = (struct smb_com_ntransact_rsp *)buf;

	bcc = get_bcc(&pSMBr->hdr);
	end_of_smb = 2 /* sizeof byte count */ + bcc +
			(char *)&pSMBr->ByteCount;

	data_offset = le32_to_cpu(pSMBr->DataOffset);
	data_count = le32_to_cpu(pSMBr->DataCount);
	parm_offset = le32_to_cpu(pSMBr->ParameterOffset);
	parm_count = le32_to_cpu(pSMBr->ParameterCount);

	*ppparm = (char *)&pSMBr->hdr.Protocol + parm_offset;
	*ppdata = (char *)&pSMBr->hdr.Protocol + data_offset;

	/* should we also check that parm and data areas do not overlap? */
	if (*ppparm > end_of_smb) {
		cFYI(1, "parms start after end of smb");
		return -EINVAL;
	} else if (parm_count + *ppparm > end_of_smb) {
		cFYI(1, "parm end after end of smb");
		return -EINVAL;
	} else if (*ppdata > end_of_smb) {
		cFYI(1, "data starts after end of smb");
		return -EINVAL;
	} else if (data_count + *ppdata > end_of_smb) {
		cFYI(1, "data %p + count %d (%p) past smb end %p start %p",
			*ppdata, data_count, (data_count + *ppdata),
			end_of_smb, pSMBr);
		return -EINVAL;
	} else if (parm_count + data_count > bcc) {
		cFYI(1, "parm count and data count larger than SMB");
		return -EINVAL;
	}
	*pdatalen = data_count;
	*pparmlen = parm_count;
	return 0;
}

/* Get Security Descriptor (by handle) from remote server for a file or dir */
int
CIFSSMBGetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon, __u16 fid,
		  struct cifs_ntsd **acl_inf, __u32 *pbuflen)
{
	int rc = 0;
	int buf_type = 0;
	QUERY_SEC_DESC_REQ *pSMB;
	struct kvec iov[1];

	cFYI(1, "GetCifsACL");

	*pbuflen = 0;
	*acl_inf = NULL;

	rc = smb_init_nttransact(NT_TRANSACT_QUERY_SECURITY_DESC, 0,
			8 /* parm len */, tcon, (void **) &pSMB);
	if (rc)
		return rc;

	pSMB->MaxParameterCount = cpu_to_le32(4);
	/* BB TEST with big acls that might need to be e.g. larger than 16K */
	pSMB->MaxSetupCount = 0;
	pSMB->Fid = fid; /* file handle always le */
	pSMB->AclFlags = cpu_to_le32(CIFS_ACL_OWNER | CIFS_ACL_GROUP |
				     CIFS_ACL_DACL);
	pSMB->ByteCount = cpu_to_le16(11); /* 3 bytes pad + 8 bytes parm */
	inc_rfc1001_len(pSMB, 11);
	iov[0].iov_base = (char *)pSMB;
	iov[0].iov_len = be32_to_cpu(pSMB->hdr.smb_buf_length) + 4;

	rc = SendReceive2(xid, tcon->ses, iov, 1 /* num iovec */, &buf_type,
			 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_acl_get);
	if (rc) {
		cFYI(1, "Send error in QuerySecDesc = %d", rc);
	} else {                /* decode response */
		__le32 *parm;
		__u32 parm_len;
		__u32 acl_len;
		struct smb_com_ntransact_rsp *pSMBr;
		char *pdata;

/* validate_nttransact */
		rc = validate_ntransact(iov[0].iov_base, (char **)&parm,
					&pdata, &parm_len, pbuflen);
		if (rc)
			goto qsec_out;
		pSMBr = (struct smb_com_ntransact_rsp *)iov[0].iov_base;

		cFYI(1, "smb %p parm %p data %p", pSMBr, parm, *acl_inf);

		if (le32_to_cpu(pSMBr->ParameterCount) != 4) {
			rc = -EIO;      /* bad smb */
			*pbuflen = 0;
			goto qsec_out;
		}

/* BB check that data area is minimum length and as big as acl_len */

		acl_len = le32_to_cpu(*parm);
		if (acl_len != *pbuflen) {
			cERROR(1, "acl length %d does not match %d",
				   acl_len, *pbuflen);
			if (*pbuflen > acl_len)
				*pbuflen = acl_len;
		}

		/* check if buffer is big enough for the acl
		   header followed by the smallest SID */
		if ((*pbuflen < sizeof(struct cifs_ntsd) + 8) ||
		    (*pbuflen >= 64 * 1024)) {
			cERROR(1, "bad acl length %d", *pbuflen);
			rc = -EINVAL;
			*pbuflen = 0;
		} else {
			*acl_inf = kmalloc(*pbuflen, GFP_KERNEL);
			if (*acl_inf == NULL) {
				*pbuflen = 0;
				rc = -ENOMEM;
			}
			memcpy(*acl_inf, pdata, *pbuflen);
		}
	}
qsec_out:
	if (buf_type == CIFS_SMALL_BUFFER)
		cifs_small_buf_release(iov[0].iov_base);
	else if (buf_type == CIFS_LARGE_BUFFER)
		cifs_buf_release(iov[0].iov_base);
/*	cifs_small_buf_release(pSMB); */ /* Freed earlier now in SendReceive2 */
	return rc;
}

int
CIFSSMBSetCIFSACL(const unsigned int xid, struct cifs_tcon *tcon, __u16 fid,
			struct cifs_ntsd *pntsd, __u32 acllen, int aclflag)
{
	__u16 byte_count, param_count, data_count, param_offset, data_offset;
	int rc = 0;
	int bytes_returned = 0;
	SET_SEC_DESC_REQ *pSMB = NULL;
	void *pSMBr;

setCifsAclRetry:
	rc = smb_init(SMB_COM_NT_TRANSACT, 19, tcon, (void **) &pSMB, &pSMBr);
	if (rc)
		return rc;

	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;

	param_count = 8;
	param_offset = offsetof(struct smb_com_transaction_ssec_req, Fid) - 4;
	data_count = acllen;
	data_offset = param_offset + param_count;
	byte_count = 3 /* pad */  + param_count;

	pSMB->DataCount = cpu_to_le32(data_count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->MaxParameterCount = cpu_to_le32(4);
	pSMB->MaxDataCount = cpu_to_le32(16384);
	pSMB->ParameterCount = cpu_to_le32(param_count);
	pSMB->ParameterOffset = cpu_to_le32(param_offset);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->DataOffset = cpu_to_le32(data_offset);
	pSMB->SetupCount = 0;
	pSMB->SubCommand = cpu_to_le16(NT_TRANSACT_SET_SECURITY_DESC);
	pSMB->ByteCount = cpu_to_le16(byte_count+data_count);

	pSMB->Fid = fid; /* file handle always le */
	pSMB->Reserved2 = 0;
	pSMB->AclFlags = cpu_to_le32(aclflag);

	if (pntsd && acllen) {
		memcpy((char *)pSMBr + offsetof(struct smb_hdr, Protocol) +
				data_offset, pntsd, acllen);
		inc_rfc1001_len(pSMB, byte_count + data_count);
	} else
		inc_rfc1001_len(pSMB, byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);

	cFYI(1, "SetCIFSACL bytes_returned: %d, rc: %d", bytes_returned, rc);
	if (rc)
		cFYI(1, "Set CIFS ACL returned %d", rc);
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto setCifsAclRetry;

	return (rc);
}

#endif /* CONFIG_CIFS_ACL */

/* Legacy Query Path Information call for lookup to old servers such
   as Win9x/WinME */
int
SMBQueryInformation(const unsigned int xid, struct cifs_tcon *tcon,
		    const char *search_name, FILE_ALL_INFO *data,
		    const struct nls_table *nls_codepage, int remap)
{
	QUERY_INFORMATION_REQ *pSMB;
	QUERY_INFORMATION_RSP *pSMBr;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, "In SMBQPath path %s", search_name);
QInfRetry:
	rc = smb_init(SMB_COM_QUERY_INFORMATION, 0, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUTF16((__le16 *) pSMB->FileName,
					   search_name, PATH_MAX, nls_codepage,
					   remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {
		name_len = strnlen(search_name, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, search_name, name_len);
	}
	pSMB->BufferFormat = 0x04;
	name_len++; /* account for buffer type byte */
	inc_rfc1001_len(pSMB, (__u16)name_len);
	pSMB->ByteCount = cpu_to_le16(name_len);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QueryInfo = %d", rc);
	} else if (data) {
		struct timespec ts;
		__u32 time = le32_to_cpu(pSMBr->last_write_time);

		/* decode response */
		/* BB FIXME - add time zone adjustment BB */
		memset(data, 0, sizeof(FILE_ALL_INFO));
		ts.tv_nsec = 0;
		ts.tv_sec = time;
		/* decode time fields */
		data->ChangeTime = cpu_to_le64(cifs_UnixTimeToNT(ts));
		data->LastWriteTime = data->ChangeTime;
		data->LastAccessTime = 0;
		data->AllocationSize =
			cpu_to_le64(le32_to_cpu(pSMBr->size));
		data->EndOfFile = data->AllocationSize;
		data->Attributes =
			cpu_to_le32(le16_to_cpu(pSMBr->attr));
	} else
		rc = -EIO; /* bad buffer passed in */

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QInfRetry;

	return rc;
}

int
CIFSSMBQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		 u16 netfid, FILE_ALL_INFO *pFindData)
{
	struct smb_t2_qfi_req *pSMB = NULL;
	struct smb_t2_qfi_rsp *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	__u16 params, byte_count;

QFileInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2 /* level */ + 2 /* fid */;
	pSMB->t2.TotalDataCount = 0;
	pSMB->t2.MaxParameterCount = cpu_to_le16(4);
	/* BB find exact max data count below from sess structure BB */
	pSMB->t2.MaxDataCount = cpu_to_le16(CIFSMaxBufSize);
	pSMB->t2.MaxSetupCount = 0;
	pSMB->t2.Reserved = 0;
	pSMB->t2.Flags = 0;
	pSMB->t2.Timeout = 0;
	pSMB->t2.Reserved2 = 0;
	pSMB->t2.ParameterOffset = cpu_to_le16(offsetof(struct smb_t2_qfi_req,
					       Fid) - 4);
	pSMB->t2.DataCount = 0;
	pSMB->t2.DataOffset = 0;
	pSMB->t2.SetupCount = 1;
	pSMB->t2.Reserved3 = 0;
	pSMB->t2.SubCommand = cpu_to_le16(TRANS2_QUERY_FILE_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->t2.TotalParameterCount = cpu_to_le16(params);
	pSMB->t2.ParameterCount = pSMB->t2.TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_ALL_INFO);
	pSMB->Pad = 0;
	pSMB->Fid = netfid;
	inc_rfc1001_len(pSMB, byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QPathInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc) /* BB add auto retry on EOPNOTSUPP? */
			rc = -EIO;
		else if (get_bcc(&pSMBr->hdr) < 40)
			rc = -EIO;	/* bad smb */
		else if (pFindData) {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset, sizeof(FILE_ALL_INFO));
		} else
		    rc = -ENOMEM;
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto QFileInfoRetry;

	return rc;
}

int
CIFSSMBQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		 const char *search_name, FILE_ALL_INFO *data,
		 int legacy /* old style infolevel */,
		 const struct nls_table *nls_codepage, int remap)
{
	/* level 263 SMB_QUERY_FILE_ALL_INFO */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;

	/* cFYI(1, "In QPathInfo path %s", search_name); */
QPathInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, search_name,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(search_name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, search_name, name_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + name_len /* includes NUL */;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	struct smb_com_transaction2_qpi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	if (legacy)
		pSMB->InformationLevel = cpu_to_le16(SMB_INFO_STANDARD);
	else
		pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_ALL_INFO);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QPathInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc) /* BB add auto retry on EOPNOTSUPP? */
			rc = -EIO;
		else if (!legacy && get_bcc(&pSMBr->hdr) < 40)
			rc = -EIO;	/* bad smb */
		else if (legacy && get_bcc(&pSMBr->hdr) < 24)
			rc = -EIO;  /* 24 or 26 expected but we do not read
					last field */
		else if (data) {
			int size;
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);

			/*
			 * On legacy responses we do not read the last field,
			 * EAsize, fortunately since it varies by subdialect and
			 * also note it differs on Set vs Get, ie two bytes or 4
			 * bytes depending but we don't care here.
			 */
			if (legacy)
				size = sizeof(FILE_INFO_STANDARD);
			else
				size = sizeof(FILE_ALL_INFO);
			memcpy((char *) data, (char *) &pSMBr->hdr.Protocol +
			       data_offset, size);
		} else
		    rc = -ENOMEM;
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto QPathInfoRetry;

	return rc;
}

int
CIFSSMBUnixQFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		 u16 netfid, FILE_UNIX_BASIC_INFO *pFindData)
{
	struct smb_t2_qfi_req *pSMB = NULL;
	struct smb_t2_qfi_rsp *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	__u16 params, byte_count;

UnixQFileInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2 /* level */ + 2 /* fid */;
	pSMB->t2.TotalDataCount = 0;
	pSMB->t2.MaxParameterCount = cpu_to_le16(4);
	/* BB find exact max data count below from sess structure BB */
	pSMB->t2.MaxDataCount = cpu_to_le16(CIFSMaxBufSize);
	pSMB->t2.MaxSetupCount = 0;
	pSMB->t2.Reserved = 0;
	pSMB->t2.Flags = 0;
	pSMB->t2.Timeout = 0;
	pSMB->t2.Reserved2 = 0;
	pSMB->t2.ParameterOffset = cpu_to_le16(offsetof(struct smb_t2_qfi_req,
					       Fid) - 4);
	pSMB->t2.DataCount = 0;
	pSMB->t2.DataOffset = 0;
	pSMB->t2.SetupCount = 1;
	pSMB->t2.Reserved3 = 0;
	pSMB->t2.SubCommand = cpu_to_le16(TRANS2_QUERY_FILE_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->t2.TotalParameterCount = cpu_to_le16(params);
	pSMB->t2.ParameterCount = pSMB->t2.TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_BASIC);
	pSMB->Pad = 0;
	pSMB->Fid = netfid;
	inc_rfc1001_len(pSMB, byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QPathInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < sizeof(FILE_UNIX_BASIC_INFO)) {
			cERROR(1, "Malformed FILE_UNIX_BASIC_INFO response. "
				   "Unix Extensions can be disabled on mount "
				   "by specifying the nosfu mount option.");
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset,
			       sizeof(FILE_UNIX_BASIC_INFO));
		}
	}

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto UnixQFileInfoRetry;

	return rc;
}

int
CIFSSMBUnixQPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		     const unsigned char *searchName,
		     FILE_UNIX_BASIC_INFO *pFindData,
		     const struct nls_table *nls_codepage, int remap)
{
/* SMB_QUERY_FILE_UNIX_BASIC */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned = 0;
	int name_len;
	__u16 params, byte_count;

	cFYI(1, "In QPathInfo (Unix) the path %s", searchName);
UnixQPathInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, searchName,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(searchName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, name_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + name_len /* includes NUL */;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	struct smb_com_transaction2_qpi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QPathInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < sizeof(FILE_UNIX_BASIC_INFO)) {
			cERROR(1, "Malformed FILE_UNIX_BASIC_INFO response. "
				   "Unix Extensions can be disabled on mount "
				   "by specifying the nosfu mount option.");
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			memcpy((char *) pFindData,
			       (char *) &pSMBr->hdr.Protocol +
			       data_offset,
			       sizeof(FILE_UNIX_BASIC_INFO));
		}
	}
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto UnixQPathInfoRetry;

	return rc;
}

/* xid, tcon, searchName and codepage are input parms, rest are returned */
int
CIFSFindFirst(const unsigned int xid, struct cifs_tcon *tcon,
	      const char *searchName, struct cifs_sb_info *cifs_sb,
	      __u16 *pnetfid, __u16 search_flags,
	      struct cifs_search_info *psrch_inf, bool msearch)
{
/* level 257 SMB_ */
	TRANSACTION2_FFIRST_REQ *pSMB = NULL;
	TRANSACTION2_FFIRST_RSP *pSMBr = NULL;
	T2_FFIRST_RSP_PARMS *parms;
	int rc = 0;
	int bytes_returned = 0;
	int name_len, remap;
	__u16 params, byte_count;
	struct nls_table *nls_codepage;

	cFYI(1, "In FindFirst for %s", searchName);

findFirstRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	nls_codepage = cifs_sb->local_nls;
	remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, searchName,
				       PATH_MAX, nls_codepage, remap);
		/* We can not add the asterik earlier in case
		it got remapped to 0xF03A as if it were part of the
		directory name instead of a wildcard */
		name_len *= 2;
		if (msearch) {
			pSMB->FileName[name_len] = CIFS_DIR_SEP(cifs_sb);
			pSMB->FileName[name_len+1] = 0;
			pSMB->FileName[name_len+2] = '*';
			pSMB->FileName[name_len+3] = 0;
			name_len += 4; /* now the trailing null */
			/* null terminate just in case */
			pSMB->FileName[name_len] = 0;
			pSMB->FileName[name_len+1] = 0;
			name_len += 2;
		}
	} else {	/* BB add check for overrun of SMB buf BB */
		name_len = strnlen(searchName, PATH_MAX);
/* BB fix here and in unicode clause above ie
		if (name_len > buffersize-header)
			free buffer exit; BB */
		strncpy(pSMB->FileName, searchName, name_len);
		if (msearch) {
			pSMB->FileName[name_len] = CIFS_DIR_SEP(cifs_sb);
			pSMB->FileName[name_len+1] = '*';
			pSMB->FileName[name_len+2] = 0;
			name_len += 3;
		}
	}

	params = 12 + name_len /* includes null */ ;
	pSMB->TotalDataCount = 0;	/* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(10);
	pSMB->MaxDataCount = cpu_to_le16(CIFSMaxBufSize & 0xFFFFFF00);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(
	      offsetof(struct smb_com_transaction2_ffirst_req, SearchAttributes)
		- 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;	/* one byte, no need to make endian neutral */
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_FIRST);
	pSMB->SearchAttributes =
	    cpu_to_le16(ATTR_READONLY | ATTR_HIDDEN | ATTR_SYSTEM |
			ATTR_DIRECTORY);
	pSMB->SearchCount = cpu_to_le16(CIFSMaxBufSize/sizeof(FILE_UNIX_INFO));
	pSMB->SearchFlags = cpu_to_le16(search_flags);
	pSMB->InformationLevel = cpu_to_le16(psrch_inf->info_level);

	/* BB what should we set StorageType to? Does it matter? BB */
	pSMB->SearchStorageType = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_ffirst);

	if (rc) {/* BB add logic to retry regular search if Unix search
			rejected unexpectedly by server */
		/* BB Add code to handle unsupported level rc */
		cFYI(1, "Error in FindFirst = %d", rc);

		cifs_buf_release(pSMB);

		/* BB eventually could optimize out free and realloc of buf */
		/*    for this case */
		if (rc == -EAGAIN)
			goto findFirstRetry;
	} else { /* decode response */
		/* BB remember to free buffer if error BB */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if (rc == 0) {
			unsigned int lnoff;

			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
				psrch_inf->unicode = true;
			else
				psrch_inf->unicode = false;

			psrch_inf->ntwrk_buf_start = (char *)pSMBr;
			psrch_inf->smallBuf = 0;
			psrch_inf->srch_entries_start =
				(char *) &pSMBr->hdr.Protocol +
					le16_to_cpu(pSMBr->t2.DataOffset);
			parms = (T2_FFIRST_RSP_PARMS *)((char *) &pSMBr->hdr.Protocol +
			       le16_to_cpu(pSMBr->t2.ParameterOffset));

			if (parms->EndofSearch)
				psrch_inf->endOfSearch = true;
			else
				psrch_inf->endOfSearch = false;

			psrch_inf->entries_in_buffer =
					le16_to_cpu(parms->SearchCount);
			psrch_inf->index_of_last_entry = 2 /* skip . and .. */ +
				psrch_inf->entries_in_buffer;
			lnoff = le16_to_cpu(parms->LastNameOffset);
			if (CIFSMaxBufSize < lnoff) {
				cERROR(1, "ignoring corrupt resume name");
				psrch_inf->last_entry = NULL;
				return rc;
			}

			psrch_inf->last_entry = psrch_inf->srch_entries_start +
							lnoff;

			if (pnetfid)
				*pnetfid = parms->SearchHandle;
		} else {
			cifs_buf_release(pSMB);
		}
	}

	return rc;
}

int CIFSFindNext(const unsigned int xid, struct cifs_tcon *tcon,
		 __u16 searchHandle, __u16 search_flags,
		 struct cifs_search_info *psrch_inf)
{
	TRANSACTION2_FNEXT_REQ *pSMB = NULL;
	TRANSACTION2_FNEXT_RSP *pSMBr = NULL;
	T2_FNEXT_RSP_PARMS *parms;
	char *response_data;
	int rc = 0;
	int bytes_returned;
	unsigned int name_len;
	__u16 params, byte_count;

	cFYI(1, "In FindNext");

	if (psrch_inf->endOfSearch)
		return -ENOENT;

	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		(void **) &pSMBr);
	if (rc)
		return rc;

	params = 14; /* includes 2 bytes of null string, converted to LE below*/
	byte_count = 0;
	pSMB->TotalDataCount = 0;       /* no EAs */
	pSMB->MaxParameterCount = cpu_to_le16(8);
	pSMB->MaxDataCount = cpu_to_le16(CIFSMaxBufSize & 0xFFFFFF00);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset =  cpu_to_le16(
	      offsetof(struct smb_com_transaction2_fnext_req,SearchHandle) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_FIND_NEXT);
	pSMB->SearchHandle = searchHandle;      /* always kept as le */
	pSMB->SearchCount =
		cpu_to_le16(CIFSMaxBufSize / sizeof(FILE_UNIX_INFO));
	pSMB->InformationLevel = cpu_to_le16(psrch_inf->info_level);
	pSMB->ResumeKey = psrch_inf->resume_key;
	pSMB->SearchFlags = cpu_to_le16(search_flags);

	name_len = psrch_inf->resume_name_len;
	params += name_len;
	if (name_len < PATH_MAX) {
		memcpy(pSMB->ResumeFileName, psrch_inf->presume_name, name_len);
		byte_count += name_len;
		/* 14 byte parm len above enough for 2 byte null terminator */
		pSMB->ResumeFileName[name_len] = 0;
		pSMB->ResumeFileName[name_len+1] = 0;
	} else {
		rc = -EINVAL;
		goto FNext2_err_exit;
	}
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	cifs_stats_inc(&tcon->stats.cifs_stats.num_fnext);
	if (rc) {
		if (rc == -EBADF) {
			psrch_inf->endOfSearch = true;
			cifs_buf_release(pSMB);
			rc = 0; /* search probably was closed at end of search*/
		} else
			cFYI(1, "FindNext returned = %d", rc);
	} else {                /* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc == 0) {
			unsigned int lnoff;

			/* BB fixme add lock for file (srch_info) struct here */
			if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
				psrch_inf->unicode = true;
			else
				psrch_inf->unicode = false;
			response_data = (char *) &pSMBr->hdr.Protocol +
			       le16_to_cpu(pSMBr->t2.ParameterOffset);
			parms = (T2_FNEXT_RSP_PARMS *)response_data;
			response_data = (char *)&pSMBr->hdr.Protocol +
				le16_to_cpu(pSMBr->t2.DataOffset);
			if (psrch_inf->smallBuf)
				cifs_small_buf_release(
					psrch_inf->ntwrk_buf_start);
			else
				cifs_buf_release(psrch_inf->ntwrk_buf_start);
			psrch_inf->srch_entries_start = response_data;
			psrch_inf->ntwrk_buf_start = (char *)pSMB;
			psrch_inf->smallBuf = 0;
			if (parms->EndofSearch)
				psrch_inf->endOfSearch = true;
			else
				psrch_inf->endOfSearch = false;
			psrch_inf->entries_in_buffer =
						le16_to_cpu(parms->SearchCount);
			psrch_inf->index_of_last_entry +=
				psrch_inf->entries_in_buffer;
			lnoff = le16_to_cpu(parms->LastNameOffset);
			if (CIFSMaxBufSize < lnoff) {
				cERROR(1, "ignoring corrupt resume name");
				psrch_inf->last_entry = NULL;
				return rc;
			} else
				psrch_inf->last_entry =
					psrch_inf->srch_entries_start + lnoff;

/*  cFYI(1, "fnxt2 entries in buf %d index_of_last %d",
	    psrch_inf->entries_in_buffer, psrch_inf->index_of_last_entry); */

			/* BB fixme add unlock here */
		}

	}

	/* BB On error, should we leave previous search buf (and count and
	last entry fields) intact or free the previous one? */

	/* Note: On -EAGAIN error only caller can retry on handle based calls
	since file handle passed in no longer valid */
FNext2_err_exit:
	if (rc != 0)
		cifs_buf_release(pSMB);
	return rc;
}

int
CIFSFindClose(const unsigned int xid, struct cifs_tcon *tcon,
	      const __u16 searchHandle)
{
	int rc = 0;
	FINDCLOSE_REQ *pSMB = NULL;

	cFYI(1, "In CIFSSMBFindClose");
	rc = small_smb_init(SMB_COM_FIND_CLOSE2, 1, tcon, (void **)&pSMB);

	/* no sense returning error if session restarted
		as file handle has been closed */
	if (rc == -EAGAIN)
		return 0;
	if (rc)
		return rc;

	pSMB->FileID = searchHandle;
	pSMB->ByteCount = 0;
	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	if (rc)
		cERROR(1, "Send error in FindClose = %d", rc);

	cifs_stats_inc(&tcon->stats.cifs_stats.num_fclose);

	/* Since session is dead, search handle closed on server already */
	if (rc == -EAGAIN)
		rc = 0;

	return rc;
}

int
CIFSGetSrvInodeNumber(const unsigned int xid, struct cifs_tcon *tcon,
		      const char *search_name, __u64 *inode_number,
		      const struct nls_table *nls_codepage, int remap)
{
	int rc = 0;
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int name_len, bytes_returned;
	__u16 params, byte_count;

	cFYI(1, "In GetSrvInodeNum for %s", search_name);
	if (tcon == NULL)
		return -ENODEV;

GetInodeNumberRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			cifsConvertToUTF16((__le16 *) pSMB->FileName,
					   search_name, PATH_MAX, nls_codepage,
					   remap);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(search_name, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->FileName, search_name, name_len);
	}

	params = 2 /* level */  + 4 /* rsrvd */  + name_len /* incl null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max data count below from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
		struct smb_com_transaction2_qpi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FILE_INTERNAL_INFO);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "error %d in QueryInternalInfo", rc);
	} else {
		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		/* BB also check enough total bytes returned */
		if (rc || get_bcc(&pSMBr->hdr) < 2)
			/* If rc should we check for EOPNOSUPP and
			disable the srvino flag? or in caller? */
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			__u16 count = le16_to_cpu(pSMBr->t2.DataCount);
			struct file_internal_info *pfinfo;
			/* BB Do we need a cast or hash here ? */
			if (count < 8) {
				cFYI(1, "Illegal size ret in QryIntrnlInf");
				rc = -EIO;
				goto GetInodeNumOut;
			}
			pfinfo = (struct file_internal_info *)
				(data_offset + (char *) &pSMBr->hdr.Protocol);
			*inode_number = le64_to_cpu(pfinfo->UniqueId);
		}
	}
GetInodeNumOut:
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto GetInodeNumberRetry;
	return rc;
}

/* parses DFS refferal V3 structure
 * caller is responsible for freeing target_nodes
 * returns:
 * 	on success - 0
 *	on failure - errno
 */
static int
parse_DFS_referrals(TRANSACTION2_GET_DFS_REFER_RSP *pSMBr,
		unsigned int *num_of_nodes,
		struct dfs_info3_param **target_nodes,
		const struct nls_table *nls_codepage, int remap,
		const char *searchName)
{
	int i, rc = 0;
	char *data_end;
	bool is_unicode;
	struct dfs_referral_level_3 *ref;

	if (pSMBr->hdr.Flags2 & SMBFLG2_UNICODE)
		is_unicode = true;
	else
		is_unicode = false;
	*num_of_nodes = le16_to_cpu(pSMBr->NumberOfReferrals);

	if (*num_of_nodes < 1) {
		cERROR(1, "num_referrals: must be at least > 0,"
			"but we get num_referrals = %d", *num_of_nodes);
		rc = -EINVAL;
		goto parse_DFS_referrals_exit;
	}

	ref = (struct dfs_referral_level_3 *) &(pSMBr->referrals);
	if (ref->VersionNumber != cpu_to_le16(3)) {
		cERROR(1, "Referrals of V%d version are not supported,"
			"should be V3", le16_to_cpu(ref->VersionNumber));
		rc = -EINVAL;
		goto parse_DFS_referrals_exit;
	}

	/* get the upper boundary of the resp buffer */
	data_end = (char *)(&(pSMBr->PathConsumed)) +
				le16_to_cpu(pSMBr->t2.DataCount);

	cFYI(1, "num_referrals: %d dfs flags: 0x%x ...",
			*num_of_nodes,
			le32_to_cpu(pSMBr->DFSFlags));

	*target_nodes = kzalloc(sizeof(struct dfs_info3_param) *
			*num_of_nodes, GFP_KERNEL);
	if (*target_nodes == NULL) {
		cERROR(1, "Failed to allocate buffer for target_nodes");
		rc = -ENOMEM;
		goto parse_DFS_referrals_exit;
	}

	/* collect necessary data from referrals */
	for (i = 0; i < *num_of_nodes; i++) {
		char *temp;
		int max_len;
		struct dfs_info3_param *node = (*target_nodes)+i;

		node->flags = le32_to_cpu(pSMBr->DFSFlags);
		if (is_unicode) {
			__le16 *tmp = kmalloc(strlen(searchName)*2 + 2,
						GFP_KERNEL);
			if (tmp == NULL) {
				rc = -ENOMEM;
				goto parse_DFS_referrals_exit;
			}
			cifsConvertToUTF16((__le16 *) tmp, searchName,
					   PATH_MAX, nls_codepage, remap);
			node->path_consumed = cifs_utf16_bytes(tmp,
					le16_to_cpu(pSMBr->PathConsumed),
					nls_codepage);
			kfree(tmp);
		} else
			node->path_consumed = le16_to_cpu(pSMBr->PathConsumed);

		node->server_type = le16_to_cpu(ref->ServerType);
		node->ref_flag = le16_to_cpu(ref->ReferralEntryFlags);

		/* copy DfsPath */
		temp = (char *)ref + le16_to_cpu(ref->DfsPathOffset);
		max_len = data_end - temp;
		node->path_name = cifs_strndup_from_utf16(temp, max_len,
						is_unicode, nls_codepage);
		if (!node->path_name) {
			rc = -ENOMEM;
			goto parse_DFS_referrals_exit;
		}

		/* copy link target UNC */
		temp = (char *)ref + le16_to_cpu(ref->NetworkAddressOffset);
		max_len = data_end - temp;
		node->node_name = cifs_strndup_from_utf16(temp, max_len,
						is_unicode, nls_codepage);
		if (!node->node_name) {
			rc = -ENOMEM;
			goto parse_DFS_referrals_exit;
		}

		ref++;
	}

parse_DFS_referrals_exit:
	if (rc) {
		free_dfs_info_array(*target_nodes, *num_of_nodes);
		*target_nodes = NULL;
		*num_of_nodes = 0;
	}
	return rc;
}

int
CIFSGetDFSRefer(const unsigned int xid, struct cifs_ses *ses,
		const char *search_name, struct dfs_info3_param **target_nodes,
		unsigned int *num_of_nodes,
		const struct nls_table *nls_codepage, int remap)
{
/* TRANS2_GET_DFS_REFERRAL */
	TRANSACTION2_GET_DFS_REFER_REQ *pSMB = NULL;
	TRANSACTION2_GET_DFS_REFER_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;
	__u16 params, byte_count;
	*num_of_nodes = 0;
	*target_nodes = NULL;

	cFYI(1, "In GetDFSRefer the path %s", search_name);
	if (ses == NULL)
		return -ENODEV;
getDFSRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, NULL, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	/* server pointer checked in called function,
	but should never be null here anyway */
	pSMB->hdr.Mid = get_next_mid(ses->server);
	pSMB->hdr.Tid = ses->ipc_tid;
	pSMB->hdr.Uid = ses->Suid;
	if (ses->capabilities & CAP_STATUS32)
		pSMB->hdr.Flags2 |= SMBFLG2_ERR_STATUS;
	if (ses->capabilities & CAP_DFS)
		pSMB->hdr.Flags2 |= SMBFLG2_DFS;

	if (ses->capabilities & CAP_UNICODE) {
		pSMB->hdr.Flags2 |= SMBFLG2_UNICODE;
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->RequestFileName,
				       search_name, PATH_MAX, nls_codepage,
				       remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(search_name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->RequestFileName, search_name, name_len);
	}

	if (ses->server) {
		if (ses->server->sec_mode &
		   (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
			pSMB->hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;
	}

	pSMB->hdr.Uid = ses->Suid;

	params = 2 /* level */  + name_len /*includes null */ ;
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = 0;
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(4000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	  struct smb_com_transaction2_get_dfs_refer_req, MaxReferralLevel) - 4);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_GET_DFS_REFERRAL);
	byte_count = params + 3 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->MaxReferralLevel = cpu_to_le16(3);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in GetDFSRefer = %d", rc);
		goto GetDFSRefExit;
	}
	rc = validate_t2((struct smb_t2_rsp *)pSMBr);

	/* BB Also check if enough total bytes returned? */
	if (rc || get_bcc(&pSMBr->hdr) < 17) {
		rc = -EIO;      /* bad smb */
		goto GetDFSRefExit;
	}

	cFYI(1, "Decoding GetDFSRefer response BCC: %d  Offset %d",
				get_bcc(&pSMBr->hdr),
				le16_to_cpu(pSMBr->t2.DataOffset));

	/* parse returned result into more usable form */
	rc = parse_DFS_referrals(pSMBr, num_of_nodes,
				 target_nodes, nls_codepage, remap,
				 search_name);

GetDFSRefExit:
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto getDFSRetry;

	return rc;
}

/* Query File System Info such as free space to old servers such as Win 9x */
int
SMBOldQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
	      struct kstatfs *FSData)
{
/* level 0x01 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_ALLOC_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, "OldQFSInfo");
oldQFSInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		(void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;     /* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_INFO_ALLOCATION);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
		(struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QFSInfo = %d", rc);
	} else {                /* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < 18)
			rc = -EIO;      /* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			cFYI(1, "qfsinf resp BCC: %d  Offset %d",
				 get_bcc(&pSMBr->hdr), data_offset);

			response_data = (FILE_SYSTEM_ALLOC_INFO *)
				(((char *) &pSMBr->hdr.Protocol) + data_offset);
			FSData->f_bsize =
				le16_to_cpu(response_data->BytesPerSector) *
				le32_to_cpu(response_data->
					SectorsPerAllocationUnit);
			FSData->f_blocks =
			       le32_to_cpu(response_data->TotalAllocationUnits);
			FSData->f_bfree = FSData->f_bavail =
				le32_to_cpu(response_data->FreeAllocationUnits);
			cFYI(1, "Blocks: %lld  Free: %lld Block size %ld",
			     (unsigned long long)FSData->f_blocks,
			     (unsigned long long)FSData->f_bfree,
			     FSData->f_bsize);
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto oldQFSInfoRetry;

	return rc;
}

int
CIFSSMBQFSInfo(const unsigned int xid, struct cifs_tcon *tcon,
	       struct kstatfs *FSData)
{
/* level 0x103 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, "In QFSInfo");
QFSInfoRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
		struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_SIZE_INFO);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QFSInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < 24)
			rc = -EIO;	/* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);

			response_data =
			    (FILE_SYSTEM_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			FSData->f_bsize =
			    le32_to_cpu(response_data->BytesPerSector) *
			    le32_to_cpu(response_data->
					SectorsPerAllocationUnit);
			FSData->f_blocks =
			    le64_to_cpu(response_data->TotalAllocationUnits);
			FSData->f_bfree = FSData->f_bavail =
			    le64_to_cpu(response_data->FreeAllocationUnits);
			cFYI(1, "Blocks: %lld  Free: %lld Block size %ld",
			     (unsigned long long)FSData->f_blocks,
			     (unsigned long long)FSData->f_bfree,
			     FSData->f_bsize);
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSInfoRetry;

	return rc;
}

int
CIFSSMBQFSAttributeInfo(const unsigned int xid, struct cifs_tcon *tcon)
{
/* level 0x105  SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_ATTRIBUTE_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, "In QFSAttributeInfo");
QFSAttributeRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
		struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_ATTRIBUTE_INFO);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, "Send error in QFSAttributeInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < 13) {
			/* BB also check if enough bytes returned */
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_ATTRIBUTE_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			memcpy(&tcon->fsAttrInfo, response_data,
			       sizeof(FILE_SYSTEM_ATTRIBUTE_INFO));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSAttributeRetry;

	return rc;
}

int
CIFSSMBQFSDeviceInfo(const unsigned int xid, struct cifs_tcon *tcon)
{
/* level 0x104 SMB_QUERY_FILE_SYSTEM_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_DEVICE_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, "In QFSDeviceInfo");
QFSDeviceRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
		struct smb_com_transaction2_qfsi_req, InformationLevel) - 4);

	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_FS_DEVICE_INFO);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QFSDeviceInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) <
			  sizeof(FILE_SYSTEM_DEVICE_INFO))
			rc = -EIO;	/* bad smb */
		else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_DEVICE_INFO *)
				(((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			memcpy(&tcon->fsDevInfo, response_data,
			       sizeof(FILE_SYSTEM_DEVICE_INFO));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSDeviceRetry;

	return rc;
}

int
CIFSSMBQFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon)
{
/* level 0x200  SMB_QUERY_CIFS_UNIX_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_UNIX_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, "In QFSUnixInfo");
QFSUnixRetry:
	rc = smb_init_no_reconnect(SMB_COM_TRANSACTION2, 15, tcon,
				   (void **) &pSMB, (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(100);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(struct
			smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_CIFS_UNIX_INFO);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, "Send error in QFSUnixInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < 13) {
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_UNIX_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			memcpy(&tcon->fsUnixInfo, response_data,
			       sizeof(FILE_SYSTEM_UNIX_INFO));
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSUnixRetry;


	return rc;
}

int
CIFSSMBSetFSUnixInfo(const unsigned int xid, struct cifs_tcon *tcon, __u64 cap)
{
/* level 0x200  SMB_SET_CIFS_UNIX_INFO */
	TRANSACTION2_SETFSI_REQ *pSMB = NULL;
	TRANSACTION2_SETFSI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, offset, byte_count;

	cFYI(1, "In SETFSUnixInfo");
SETFSUnixRetry:
	/* BB switch to small buf init to save memory */
	rc = smb_init_no_reconnect(SMB_COM_TRANSACTION2, 15, tcon,
					(void **) &pSMB, (void **) &pSMBr);
	if (rc)
		return rc;

	params = 4;	/* 2 bytes zero followed by info level. */
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_setfsi_req, FileNum)
				- 4;
	offset = param_offset + params;

	pSMB->MaxParameterCount = cpu_to_le16(4);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(100);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FS_INFORMATION);
	byte_count = 1 /* pad */ + params + 12;

	pSMB->DataCount = cpu_to_le16(12);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);

	/* Params. */
	pSMB->FileNum = 0;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_CIFS_UNIX_INFO);

	/* Data. */
	pSMB->ClientUnixMajor = cpu_to_le16(CIFS_UNIX_MAJOR_VERSION);
	pSMB->ClientUnixMinor = cpu_to_le16(CIFS_UNIX_MINOR_VERSION);
	pSMB->ClientUnixCap = cpu_to_le64(cap);

	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cERROR(1, "Send error in SETFSUnixInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);
		if (rc)
			rc = -EIO;	/* bad smb */
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SETFSUnixRetry;

	return rc;
}



int
CIFSSMBQFSPosixInfo(const unsigned int xid, struct cifs_tcon *tcon,
		   struct kstatfs *FSData)
{
/* level 0x201  SMB_QUERY_CIFS_POSIX_INFO */
	TRANSACTION2_QFSI_REQ *pSMB = NULL;
	TRANSACTION2_QFSI_RSP *pSMBr = NULL;
	FILE_SYSTEM_POSIX_INFO *response_data;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, byte_count;

	cFYI(1, "In QFSPosixInfo");
QFSPosixRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	params = 2;	/* level */
	pSMB->TotalDataCount = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(100);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	byte_count = params + 1 /* pad */ ;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(struct
			smb_com_transaction2_qfsi_req, InformationLevel) - 4);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_FS_INFORMATION);
	pSMB->InformationLevel = cpu_to_le16(SMB_QUERY_POSIX_FS_INFO);
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QFSUnixInfo = %d", rc);
	} else {		/* decode response */
		rc = validate_t2((struct smb_t2_rsp *)pSMBr);

		if (rc || get_bcc(&pSMBr->hdr) < 13) {
			rc = -EIO;	/* bad smb */
		} else {
			__u16 data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
			response_data =
			    (FILE_SYSTEM_POSIX_INFO
			     *) (((char *) &pSMBr->hdr.Protocol) +
				 data_offset);
			FSData->f_bsize =
					le32_to_cpu(response_data->BlockSize);
			FSData->f_blocks =
					le64_to_cpu(response_data->TotalBlocks);
			FSData->f_bfree =
			    le64_to_cpu(response_data->BlocksAvail);
			if (response_data->UserBlocksAvail == cpu_to_le64(-1)) {
				FSData->f_bavail = FSData->f_bfree;
			} else {
				FSData->f_bavail =
				    le64_to_cpu(response_data->UserBlocksAvail);
			}
			if (response_data->TotalFileNodes != cpu_to_le64(-1))
				FSData->f_files =
				     le64_to_cpu(response_data->TotalFileNodes);
			if (response_data->FreeFileNodes != cpu_to_le64(-1))
				FSData->f_ffree =
				      le64_to_cpu(response_data->FreeFileNodes);
		}
	}
	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto QFSPosixRetry;

	return rc;
}


/*
 * We can not use write of zero bytes trick to set file size due to need for
 * large file support. Also note that this SetPathInfo is preferred to
 * SetFileInfo based method in next routine which is only needed to work around
 * a sharing violation bugin Samba which this routine can run into.
 */
int
CIFSSMBSetEOF(const unsigned int xid, struct cifs_tcon *tcon,
	      const char *file_name, __u64 size, struct cifs_sb_info *cifs_sb,
	      bool set_allocation)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	struct file_end_of_file_info *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	int remap = cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR;

	__u16 params, byte_count, data_count, param_offset, offset;

	cFYI(1, "In SetEOF");
SetEOFRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, file_name,
				       PATH_MAX, cifs_sb->local_nls, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(file_name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, file_name, name_len);
	}
	params = 6 + name_len;
	data_count = sizeof(struct file_end_of_file_info);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	pSMB->MaxDataCount = cpu_to_le16(4100);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;
	if (set_allocation) {
		if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
			pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO2);
		else
			pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO);
	} else /* Set File Size */  {
	    if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		    pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO2);
	    else
		    pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO);
	}

	parm_data =
	    (struct file_end_of_file_info *) (((char *) &pSMB->hdr.Protocol) +
				       offset);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + data_count;
	pSMB->DataCount = cpu_to_le16(data_count);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	parm_data->FileSize = cpu_to_le64(size);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "SetPathInfo (file size) returned %d", rc);

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetEOFRetry;

	return rc;
}

int
CIFSSMBSetFileSize(const unsigned int xid, struct cifs_tcon *tcon,
		   struct cifsFileInfo *cfile, __u64 size, bool set_allocation)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	struct file_end_of_file_info *parm_data;
	int rc = 0;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, "SetFileSize (via SetFileInfo) %lld",
			(long long)size);
	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)cfile->pid);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(cfile->pid >> 16));

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	count = sizeof(struct file_end_of_file_info);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	parm_data =
		(struct file_end_of_file_info *) (((char *) &pSMB->hdr.Protocol)
				+ offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	parm_data->FileSize = cpu_to_le64(size);
	pSMB->Fid = cfile->fid.netfid;
	if (set_allocation) {
		if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
			pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO2);
		else
			pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_ALLOCATION_INFO);
	} else /* Set File Size */  {
	    if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		    pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO2);
	    else
		    pSMB->InformationLevel =
				cpu_to_le16(SMB_SET_FILE_END_OF_FILE_INFO);
	}
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	if (rc) {
		cFYI(1, "Send error in SetFileInfo (SetFileSize) = %d", rc);
	}

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

/* Some legacy servers such as NT4 require that the file times be set on
   an open handle, rather than by pathname - this is awkward due to
   potential access conflicts on the open, but it is unavoidable for these
   old servers since the only other choice is to go from 100 nanosecond DCE
   time and resort to the original setpathinfo level which takes the ancient
   DOS time format with 2 second granularity */
int
CIFSSMBSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		    const FILE_BASIC_INFO *data, __u16 fid, __u32 pid_of_opener)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	char *data_offset;
	int rc = 0;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, "Set Times (via SetFileInfo)");
	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid_of_opener);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid_of_opener >> 16));

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *)pSMB +
			offsetof(struct smb_hdr, Protocol) + offset;

	count = sizeof(FILE_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB PDU from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->Fid = fid;
	if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO2);
	else
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	memcpy(data_offset, data, sizeof(FILE_BASIC_INFO));
	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	if (rc)
		cFYI(1, "Send error in Set Time (SetFileInfo) = %d", rc);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

int
CIFSSMBSetFileDisposition(const unsigned int xid, struct cifs_tcon *tcon,
			  bool delete_file, __u16 fid, __u32 pid_of_opener)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	char *data_offset;
	int rc = 0;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, "Set File Disposition (via SetFileInfo)");
	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid_of_opener);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid_of_opener >> 16));

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;

	count = 1;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB PDU from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->Fid = fid;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_DISPOSITION_INFO);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	*data_offset = delete_file ? 1 : 0;
	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	if (rc)
		cFYI(1, "Send error in SetFileDisposition = %d", rc);

	return rc;
}

int
CIFSSMBSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		   const char *fileName, const FILE_BASIC_INFO *data,
		   const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	char *data_offset;
	__u16 params, param_offset, offset, byte_count, count;

	cFYI(1, "In SetTimes");

SetTimesRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, fileName,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	params = 6 + name_len;
	count = sizeof(FILE_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;
	data_offset = (char *) (&pSMB->hdr.Protocol) + offset;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;

	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	if (tcon->ses->capabilities & CAP_INFOLEVEL_PASSTHRU)
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO2);
	else
		pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_BASIC_INFO);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	memcpy(data_offset, data, sizeof(FILE_BASIC_INFO));
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "SetPathInfo (times) returned %d", rc);

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetTimesRetry;

	return rc;
}

/* Can not be used to set time stamps yet (due to old DOS time format) */
/* Can be used to set attributes */
#if 0  /* Possibly not needed - since it turns out that strangely NT4 has a bug
	  handling it anyway and NT4 was what we thought it would be needed for
	  Do not delete it until we prove whether needed for Win9x though */
int
CIFSSMBSetAttrLegacy(unsigned int xid, struct cifs_tcon *tcon, char *fileName,
		__u16 dos_attrs, const struct nls_table *nls_codepage)
{
	SETATTR_REQ *pSMB = NULL;
	SETATTR_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int name_len;

	cFYI(1, "In SetAttrLegacy");

SetAttrLgcyRetry:
	rc = smb_init(SMB_COM_SETATTR, 8, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
			ConvertToUTF16((__le16 *) pSMB->fileName, fileName,
				       PATH_MAX, nls_codepage);
		name_len++;     /* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;     /* trailing null */
		strncpy(pSMB->fileName, fileName, name_len);
	}
	pSMB->attr = cpu_to_le16(dos_attrs);
	pSMB->BufferFormat = 0x04;
	inc_rfc1001_len(pSMB, name_len + 1);
	pSMB->ByteCount = cpu_to_le16(name_len + 1);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "Error in LegacySetAttr = %d", rc);

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetAttrLgcyRetry;

	return rc;
}
#endif /* temporarily unneeded SetAttr legacy function */

static void
cifs_fill_unix_set_info(FILE_UNIX_BASIC_INFO *data_offset,
			const struct cifs_unix_set_info_args *args)
{
	u64 mode = args->mode;

	/*
	 * Samba server ignores set of file size to zero due to bugs in some
	 * older clients, but we should be precise - we use SetFileSize to
	 * set file size and do not want to truncate file size to zero
	 * accidentally as happened on one Samba server beta by putting
	 * zero instead of -1 here
	 */
	data_offset->EndOfFile = cpu_to_le64(NO_CHANGE_64);
	data_offset->NumOfBytes = cpu_to_le64(NO_CHANGE_64);
	data_offset->LastStatusChange = cpu_to_le64(args->ctime);
	data_offset->LastAccessTime = cpu_to_le64(args->atime);
	data_offset->LastModificationTime = cpu_to_le64(args->mtime);
	data_offset->Uid = cpu_to_le64(args->uid);
	data_offset->Gid = cpu_to_le64(args->gid);
	/* better to leave device as zero when it is  */
	data_offset->DevMajor = cpu_to_le64(MAJOR(args->device));
	data_offset->DevMinor = cpu_to_le64(MINOR(args->device));
	data_offset->Permissions = cpu_to_le64(mode);

	if (S_ISREG(mode))
		data_offset->Type = cpu_to_le32(UNIX_FILE);
	else if (S_ISDIR(mode))
		data_offset->Type = cpu_to_le32(UNIX_DIR);
	else if (S_ISLNK(mode))
		data_offset->Type = cpu_to_le32(UNIX_SYMLINK);
	else if (S_ISCHR(mode))
		data_offset->Type = cpu_to_le32(UNIX_CHARDEV);
	else if (S_ISBLK(mode))
		data_offset->Type = cpu_to_le32(UNIX_BLOCKDEV);
	else if (S_ISFIFO(mode))
		data_offset->Type = cpu_to_le32(UNIX_FIFO);
	else if (S_ISSOCK(mode))
		data_offset->Type = cpu_to_le32(UNIX_SOCKET);
}

int
CIFSSMBUnixSetFileInfo(const unsigned int xid, struct cifs_tcon *tcon,
		       const struct cifs_unix_set_info_args *args,
		       u16 fid, u32 pid_of_opener)
{
	struct smb_com_transaction2_sfi_req *pSMB  = NULL;
	char *data_offset;
	int rc = 0;
	u16 params, param_offset, offset, byte_count, count;

	cFYI(1, "Set Unix Info (via SetFileInfo)");
	rc = small_smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB);

	if (rc)
		return rc;

	pSMB->hdr.Pid = cpu_to_le16((__u16)pid_of_opener);
	pSMB->hdr.PidHigh = cpu_to_le16((__u16)(pid_of_opener >> 16));

	params = 6;
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_sfi_req, Fid) - 4;
	offset = param_offset + params;

	data_offset = (char *)pSMB +
			offsetof(struct smb_hdr, Protocol) + offset;

	count = sizeof(FILE_UNIX_BASIC_INFO);

	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB PDU from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_FILE_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->Fid = fid;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	cifs_fill_unix_set_info((FILE_UNIX_BASIC_INFO *)data_offset, args);

	rc = SendReceiveNoRsp(xid, tcon->ses, (char *) pSMB, 0);
	if (rc)
		cFYI(1, "Send error in Set Time (SetFileInfo) = %d", rc);

	/* Note: On -EAGAIN error only caller can retry on handle based calls
		since file handle passed in no longer valid */

	return rc;
}

int
CIFSSMBUnixSetPathInfo(const unsigned int xid, struct cifs_tcon *tcon,
		       const char *file_name,
		       const struct cifs_unix_set_info_args *args,
		       const struct nls_table *nls_codepage, int remap)
{
	TRANSACTION2_SPI_REQ *pSMB = NULL;
	TRANSACTION2_SPI_RSP *pSMBr = NULL;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	FILE_UNIX_BASIC_INFO *data_offset;
	__u16 params, param_offset, offset, count, byte_count;

	cFYI(1, "In SetUID/GID/Mode");
setPermsRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, file_name,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(file_name, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, file_name, name_len);
	}

	params = 6 + name_len;
	count = sizeof(FILE_UNIX_BASIC_INFO);
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;
	data_offset =
	    (FILE_UNIX_BASIC_INFO *) ((char *) &pSMB->hdr.Protocol +
				      offset);
	memset(data_offset, 0, count);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->DataCount = cpu_to_le16(count);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_SET_FILE_UNIX_BASIC);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);

	cifs_fill_unix_set_info(data_offset, args);

	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "SetPathInfo (perms) returned %d", rc);

	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto setPermsRetry;
	return rc;
}

#ifdef CONFIG_CIFS_XATTR
/*
 * Do a path-based QUERY_ALL_EAS call and parse the result. This is a common
 * function used by listxattr and getxattr type calls. When ea_name is set,
 * it looks for that attribute name and stuffs that value into the EAData
 * buffer. When ea_name is NULL, it stuffs a list of attribute names into the
 * buffer. In both cases, the return value is either the length of the
 * resulting data or a negative error code. If EAData is a NULL pointer then
 * the data isn't copied to it, but the length is returned.
 */
ssize_t
CIFSSMBQAllEAs(const unsigned int xid, struct cifs_tcon *tcon,
		const unsigned char *searchName, const unsigned char *ea_name,
		char *EAData, size_t buf_size,
		const struct nls_table *nls_codepage, int remap)
{
		/* BB assumes one setup word */
	TRANSACTION2_QPI_REQ *pSMB = NULL;
	TRANSACTION2_QPI_RSP *pSMBr = NULL;
	int rc = 0;
	int bytes_returned;
	int list_len;
	struct fealist *ea_response_data;
	struct fea *temp_fea;
	char *temp_ptr;
	char *end_of_smb;
	__u16 params, byte_count, data_offset;
	unsigned int ea_name_len = ea_name ? strlen(ea_name) : 0;

	cFYI(1, "In Query All EAs path %s", searchName);
QAllEAsRetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		list_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, searchName,
				       PATH_MAX, nls_codepage, remap);
		list_len++;	/* trailing null */
		list_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		list_len = strnlen(searchName, PATH_MAX);
		list_len++;	/* trailing null */
		strncpy(pSMB->FileName, searchName, list_len);
	}

	params = 2 /* level */ + 4 /* reserved */ + list_len /* includes NUL */;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find exact max SMB PDU from sess structure BB */
	pSMB->MaxDataCount = cpu_to_le16(CIFSMaxBufSize);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	pSMB->ParameterOffset = cpu_to_le16(offsetof(
	struct smb_com_transaction2_qpi_req, InformationLevel) - 4);
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_QUERY_PATH_INFORMATION);
	byte_count = params + 1 /* pad */ ;
	pSMB->TotalParameterCount = cpu_to_le16(params);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	pSMB->InformationLevel = cpu_to_le16(SMB_INFO_QUERY_ALL_EAS);
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc) {
		cFYI(1, "Send error in QueryAllEAs = %d", rc);
		goto QAllEAsOut;
	}


	/* BB also check enough total bytes returned */
	/* BB we need to improve the validity checking
	of these trans2 responses */

	rc = validate_t2((struct smb_t2_rsp *)pSMBr);
	if (rc || get_bcc(&pSMBr->hdr) < 4) {
		rc = -EIO;	/* bad smb */
		goto QAllEAsOut;
	}

	/* check that length of list is not more than bcc */
	/* check that each entry does not go beyond length
	   of list */
	/* check that each element of each entry does not
	   go beyond end of list */
	/* validate_trans2_offsets() */
	/* BB check if start of smb + data_offset > &bcc+ bcc */

	data_offset = le16_to_cpu(pSMBr->t2.DataOffset);
	ea_response_data = (struct fealist *)
				(((char *) &pSMBr->hdr.Protocol) + data_offset);

	list_len = le32_to_cpu(ea_response_data->list_len);
	cFYI(1, "ea length %d", list_len);
	if (list_len <= 8) {
		cFYI(1, "empty EA list returned from server");
		goto QAllEAsOut;
	}

	/* make sure list_len doesn't go past end of SMB */
	end_of_smb = (char *)pByteArea(&pSMBr->hdr) + get_bcc(&pSMBr->hdr);
	if ((char *)ea_response_data + list_len > end_of_smb) {
		cFYI(1, "EA list appears to go beyond SMB");
		rc = -EIO;
		goto QAllEAsOut;
	}

	/* account for ea list len */
	list_len -= 4;
	temp_fea = ea_response_data->list;
	temp_ptr = (char *)temp_fea;
	while (list_len > 0) {
		unsigned int name_len;
		__u16 value_len;

		list_len -= 4;
		temp_ptr += 4;
		/* make sure we can read name_len and value_len */
		if (list_len < 0) {
			cFYI(1, "EA entry goes beyond length of list");
			rc = -EIO;
			goto QAllEAsOut;
		}

		name_len = temp_fea->name_len;
		value_len = le16_to_cpu(temp_fea->value_len);
		list_len -= name_len + 1 + value_len;
		if (list_len < 0) {
			cFYI(1, "EA entry goes beyond length of list");
			rc = -EIO;
			goto QAllEAsOut;
		}

		if (ea_name) {
			if (ea_name_len == name_len &&
			    memcmp(ea_name, temp_ptr, name_len) == 0) {
				temp_ptr += name_len + 1;
				rc = value_len;
				if (buf_size == 0)
					goto QAllEAsOut;
				if ((size_t)value_len > buf_size) {
					rc = -ERANGE;
					goto QAllEAsOut;
				}
				memcpy(EAData, temp_ptr, value_len);
				goto QAllEAsOut;
			}
		} else {
			/* account for prefix user. and trailing null */
			rc += (5 + 1 + name_len);
			if (rc < (int) buf_size) {
				memcpy(EAData, "user.", 5);
				EAData += 5;
				memcpy(EAData, temp_ptr, name_len);
				EAData += name_len;
				/* null terminate name */
				*EAData = 0;
				++EAData;
			} else if (buf_size == 0) {
				/* skip copy - calc size only */
			} else {
				/* stop before overrun buffer */
				rc = -ERANGE;
				break;
			}
		}
		temp_ptr += name_len + 1 + value_len;
		temp_fea = (struct fea *)temp_ptr;
	}

	/* didn't find the named attribute */
	if (ea_name)
		rc = -ENODATA;

QAllEAsOut:
	cifs_buf_release(pSMB);
	if (rc == -EAGAIN)
		goto QAllEAsRetry;

	return (ssize_t)rc;
}

int
CIFSSMBSetEA(const unsigned int xid, struct cifs_tcon *tcon,
	     const char *fileName, const char *ea_name, const void *ea_value,
	     const __u16 ea_value_len, const struct nls_table *nls_codepage,
	     int remap)
{
	struct smb_com_transaction2_spi_req *pSMB = NULL;
	struct smb_com_transaction2_spi_rsp *pSMBr = NULL;
	struct fealist *parm_data;
	int name_len;
	int rc = 0;
	int bytes_returned = 0;
	__u16 params, param_offset, byte_count, offset, count;

	cFYI(1, "In SetEA");
SetEARetry:
	rc = smb_init(SMB_COM_TRANSACTION2, 15, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	if (pSMB->hdr.Flags2 & SMBFLG2_UNICODE) {
		name_len =
		    cifsConvertToUTF16((__le16 *) pSMB->FileName, fileName,
				       PATH_MAX, nls_codepage, remap);
		name_len++;	/* trailing null */
		name_len *= 2;
	} else {	/* BB improve the check for buffer overruns BB */
		name_len = strnlen(fileName, PATH_MAX);
		name_len++;	/* trailing null */
		strncpy(pSMB->FileName, fileName, name_len);
	}

	params = 6 + name_len;

	/* done calculating parms using name_len of file name,
	now use name_len to calculate length of ea name
	we are going to create in the inode xattrs */
	if (ea_name == NULL)
		name_len = 0;
	else
		name_len = strnlen(ea_name, 255);

	count = sizeof(*parm_data) + ea_value_len + name_len;
	pSMB->MaxParameterCount = cpu_to_le16(2);
	/* BB find max SMB PDU from sess */
	pSMB->MaxDataCount = cpu_to_le16(1000);
	pSMB->MaxSetupCount = 0;
	pSMB->Reserved = 0;
	pSMB->Flags = 0;
	pSMB->Timeout = 0;
	pSMB->Reserved2 = 0;
	param_offset = offsetof(struct smb_com_transaction2_spi_req,
				InformationLevel) - 4;
	offset = param_offset + params;
	pSMB->InformationLevel =
		cpu_to_le16(SMB_SET_FILE_EA);

	parm_data =
		(struct fealist *) (((char *) &pSMB->hdr.Protocol) +
				       offset);
	pSMB->ParameterOffset = cpu_to_le16(param_offset);
	pSMB->DataOffset = cpu_to_le16(offset);
	pSMB->SetupCount = 1;
	pSMB->Reserved3 = 0;
	pSMB->SubCommand = cpu_to_le16(TRANS2_SET_PATH_INFORMATION);
	byte_count = 3 /* pad */  + params + count;
	pSMB->DataCount = cpu_to_le16(count);
	parm_data->list_len = cpu_to_le32(count);
	parm_data->list[0].EA_flags = 0;
	/* we checked above that name len is less than 255 */
	parm_data->list[0].name_len = (__u8)name_len;
	/* EA names are always ASCII */
	if (ea_name)
		strncpy(parm_data->list[0].name, ea_name, name_len);
	parm_data->list[0].name[name_len] = 0;
	parm_data->list[0].value_len = cpu_to_le16(ea_value_len);
	/* caller ensures that ea_value_len is less than 64K but
	we need to ensure that it fits within the smb */

	/*BB add length check to see if it would fit in
	     negotiated SMB buffer size BB */
	/* if (ea_value_len > buffer_size - 512 (enough for header)) */
	if (ea_value_len)
		memcpy(parm_data->list[0].name+name_len+1,
		       ea_value, ea_value_len);

	pSMB->TotalDataCount = pSMB->DataCount;
	pSMB->ParameterCount = cpu_to_le16(params);
	pSMB->TotalParameterCount = pSMB->ParameterCount;
	pSMB->Reserved4 = 0;
	inc_rfc1001_len(pSMB, byte_count);
	pSMB->ByteCount = cpu_to_le16(byte_count);
	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *) pSMBr, &bytes_returned, 0);
	if (rc)
		cFYI(1, "SetPathInfo (EA) returned %d", rc);

	cifs_buf_release(pSMB);

	if (rc == -EAGAIN)
		goto SetEARetry;

	return rc;
}
#endif

#ifdef CONFIG_CIFS_DNOTIFY_EXPERIMENTAL /* BB unused temporarily */
/*
 *	Years ago the kernel added a "dnotify" function for Samba server,
 *	to allow network clients (such as Windows) to display updated
 *	lists of files in directory listings automatically when
 *	files are added by one user when another user has the
 *	same directory open on their desktop.  The Linux cifs kernel
 *	client hooked into the kernel side of this interface for
 *	the same reason, but ironically when the VFS moved from
 *	"dnotify" to "inotify" it became harder to plug in Linux
 *	network file system clients (the most obvious use case
 *	for notify interfaces is when multiple users can update
 *	the contents of the same directory - exactly what network
 *	file systems can do) although the server (Samba) could
 *	still use it.  For the short term we leave the worker
 *	function ifdeffed out (below) until inotify is fixed
 *	in the VFS to make it easier to plug in network file
 *	system clients.  If inotify turns out to be permanently
 *	incompatible for network fs clients, we could instead simply
 *	expose this config flag by adding a future cifs (and smb2) notify ioctl.
 */
int CIFSSMBNotify(const unsigned int xid, struct cifs_tcon *tcon,
		  const int notify_subdirs, const __u16 netfid,
		  __u32 filter, struct file *pfile, int multishot,
		  const struct nls_table *nls_codepage)
{
	int rc = 0;
	struct smb_com_transaction_change_notify_req *pSMB = NULL;
	struct smb_com_ntransaction_change_notify_rsp *pSMBr = NULL;
	struct dir_notify_req *dnotify_req;
	int bytes_returned;

	cFYI(1, "In CIFSSMBNotify for file handle %d", (int)netfid);
	rc = smb_init(SMB_COM_NT_TRANSACT, 23, tcon, (void **) &pSMB,
		      (void **) &pSMBr);
	if (rc)
		return rc;

	pSMB->TotalParameterCount = 0 ;
	pSMB->TotalDataCount = 0;
	pSMB->MaxParameterCount = cpu_to_le32(2);
	pSMB->MaxDataCount = cpu_to_le32(CIFSMaxBufSize & 0xFFFFFF00);
	pSMB->MaxSetupCount = 4;
	pSMB->Reserved = 0;
	pSMB->ParameterOffset = 0;
	pSMB->DataCount = 0;
	pSMB->DataOffset = 0;
	pSMB->SetupCount = 4; /* single byte does not need le conversion */
	pSMB->SubCommand = cpu_to_le16(NT_TRANSACT_NOTIFY_CHANGE);
	pSMB->ParameterCount = pSMB->TotalParameterCount;
	if (notify_subdirs)
		pSMB->WatchTree = 1; /* one byte - no le conversion needed */
	pSMB->Reserved2 = 0;
	pSMB->CompletionFilter = cpu_to_le32(filter);
	pSMB->Fid = netfid; /* file handle always le */
	pSMB->ByteCount = 0;

	rc = SendReceive(xid, tcon->ses, (struct smb_hdr *) pSMB,
			 (struct smb_hdr *)pSMBr, &bytes_returned,
			 CIFS_ASYNC_OP);
	if (rc) {
		cFYI(1, "Error in Notify = %d", rc);
	} else {
		/* Add file to outstanding requests */
		/* BB change to kmem cache alloc */
		dnotify_req = kmalloc(
						sizeof(struct dir_notify_req),
						 GFP_KERNEL);
		if (dnotify_req) {
			dnotify_req->Pid = pSMB->hdr.Pid;
			dnotify_req->PidHigh = pSMB->hdr.PidHigh;
			dnotify_req->Mid = pSMB->hdr.Mid;
			dnotify_req->Tid = pSMB->hdr.Tid;
			dnotify_req->Uid = pSMB->hdr.Uid;
			dnotify_req->netfid = netfid;
			dnotify_req->pfile = pfile;
			dnotify_req->filter = filter;
			dnotify_req->multishot = multishot;
			spin_lock(&GlobalMid_Lock);
			list_add_tail(&dnotify_req->lhead,
					&GlobalDnotifyReqList);
			spin_unlock(&GlobalMid_Lock);
		} else
			rc = -ENOMEM;
	}
	cifs_buf_release(pSMB);
	return rc;
}
#endif /* was needed for dnotify, and will be needed for inotify when VFS fix */
