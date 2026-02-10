// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   SMB/CIFS session setup handling routines
 *
 *   Copyright (c) International Business Machines  Corp., 2006, 2009
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#include "cifsproto.h"
#include "smb1proto.h"
#include "ntlmssp.h"
#include "nterr.h"
#include "cifs_spnego.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"

struct sess_data {
	unsigned int xid;
	struct cifs_ses *ses;
	struct TCP_Server_Info *server;
	struct nls_table *nls_cp;
	void (*func)(struct sess_data *);
	int result;
	unsigned int in_len;

	/* we will send the SMB in three pieces:
	 * a fixed length beginning part, an optional
	 * SPNEGO blob (which can be zero length), and a
	 * last part which will include the strings
	 * and rest of bcc area. This allows us to avoid
	 * a large buffer 17K allocation
	 */
	int buf0_type;
	struct kvec iov[3];
};

static __u32 cifs_ssetup_hdr(struct cifs_ses *ses,
			     struct TCP_Server_Info *server,
			     SESSION_SETUP_ANDX *pSMB)
{
	__u32 capabilities = 0;

	/* init fields common to all four types of SessSetup */
	/* Note that offsets for first seven fields in req struct are same  */
	/*	in CIFS Specs so does not matter which of 3 forms of struct */
	/*	that we use in next few lines                               */
	/* Note that header is initialized to zero in header_assemble */
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(min_t(u32,
					CIFSMaxBufSize + MAX_CIFS_HDR_SIZE - 4,
					USHRT_MAX));
	pSMB->req.MaxMpxCount = cpu_to_le16(server->maxReq);
	pSMB->req.VcNumber = cpu_to_le16(1);
	pSMB->req.SessionKey = server->session_key_id;

	/* Now no need to set SMBFLG_CASELESS or obsolete CANONICAL PATH */

	/* BB verify whether signing required on neg or just auth frame (and NTLM case) */

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;

	if (server->sign)
		pSMB->req.hdr.Flags2 |= SMBFLG2_SECURITY_SIGNATURE;

	if (ses->capabilities & CAP_UNICODE) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_UNICODE;
		capabilities |= CAP_UNICODE;
	}
	if (ses->capabilities & CAP_STATUS32) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_ERR_STATUS;
		capabilities |= CAP_STATUS32;
	}
	if (ses->capabilities & CAP_DFS) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_DFS;
		capabilities |= CAP_DFS;
	}
	if (ses->capabilities & CAP_UNIX)
		capabilities |= CAP_UNIX;

	return capabilities;
}

static void
unicode_oslm_strings(char **pbcc_area, const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* Copy OS version */
	bytes_ret = cifs_strtoUTF16((__le16 *)bcc_ptr, "Linux version ", 32,
				    nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, init_utsname()->release,
				    32, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* trailing null */

	bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				    32, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* trailing null */

	*pbcc_area = bcc_ptr;
}

static void
ascii_oslm_strings(char **pbcc_area, const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;

	strcpy(bcc_ptr, "Linux version ");
	bcc_ptr += strlen("Linux version ");
	strcpy(bcc_ptr, init_utsname()->release);
	bcc_ptr += strlen(init_utsname()->release) + 1;

	strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
	bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;

	*pbcc_area = bcc_ptr;
}

static void unicode_domain_string(char **pbcc_area, struct cifs_ses *ses,
				   const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* copy domain */
	if (ses->domainName == NULL) {
		/*
		 * Sending null domain better than using a bogus domain name (as
		 * we did briefly in 2.6.18) since server will use its default
		 */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
		bytes_ret = 0;
	} else
		bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, ses->domainName,
					    CIFS_MAX_DOMAINNAME_LEN, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2;  /* account for null terminator */

	*pbcc_area = bcc_ptr;
}

static void ascii_domain_string(char **pbcc_area, struct cifs_ses *ses,
				const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int len;

	/* copy domain */
	if (ses->domainName != NULL) {
		len = strscpy(bcc_ptr, ses->domainName, CIFS_MAX_DOMAINNAME_LEN);
		if (WARN_ON_ONCE(len < 0))
			len = CIFS_MAX_DOMAINNAME_LEN - 1;
		bcc_ptr += len;
	} /* else we send a null domain name so server will default to its own domain */
	*bcc_ptr = 0;
	bcc_ptr++;

	*pbcc_area = bcc_ptr;
}

static void unicode_ssetup_strings(char **pbcc_area, struct cifs_ses *ses,
				   const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* BB FIXME add check that strings less than 335 or will need to send as arrays */

	/* copy user */
	if (ses->user_name == NULL) {
		/* null user mount */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
	} else {
		bytes_ret = cifs_strtoUTF16((__le16 *) bcc_ptr, ses->user_name,
					    CIFS_MAX_USERNAME_LEN, nls_cp);
	}
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* account for null termination */

	unicode_domain_string(&bcc_ptr, ses, nls_cp);
	unicode_oslm_strings(&bcc_ptr, nls_cp);

	*pbcc_area = bcc_ptr;
}

static void ascii_ssetup_strings(char **pbcc_area, struct cifs_ses *ses,
				 const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int len;

	/* copy user */
	/* BB what about null user mounts - check that we do this BB */
	/* copy user */
	if (ses->user_name != NULL) {
		len = strscpy(bcc_ptr, ses->user_name, CIFS_MAX_USERNAME_LEN);
		if (WARN_ON_ONCE(len < 0))
			len = CIFS_MAX_USERNAME_LEN - 1;
		bcc_ptr += len;
	}
	/* else null user mount */
	*bcc_ptr = 0;
	bcc_ptr++; /* account for null termination */

	/* BB check for overflow here */

	ascii_domain_string(&bcc_ptr, ses, nls_cp);
	ascii_oslm_strings(&bcc_ptr, nls_cp);

	*pbcc_area = bcc_ptr;
}

static void
decode_unicode_ssetup(char **pbcc_area, int bleft, struct cifs_ses *ses,
		      const struct nls_table *nls_cp)
{
	int len;
	char *data = *pbcc_area;

	cifs_dbg(FYI, "bleft %d\n", bleft);

	kfree(ses->serverOS);
	ses->serverOS = cifs_strndup_from_utf16(data, bleft, true, nls_cp);
	cifs_dbg(FYI, "serverOS=%s\n", ses->serverOS);
	len = (UniStrnlen((wchar_t *) data, bleft / 2) * 2) + 2;
	data += len;
	bleft -= len;
	if (bleft <= 0)
		return;

	kfree(ses->serverNOS);
	ses->serverNOS = cifs_strndup_from_utf16(data, bleft, true, nls_cp);
	cifs_dbg(FYI, "serverNOS=%s\n", ses->serverNOS);
	len = (UniStrnlen((wchar_t *) data, bleft / 2) * 2) + 2;
	data += len;
	bleft -= len;
	if (bleft <= 0)
		return;

	kfree(ses->serverDomain);
	ses->serverDomain = cifs_strndup_from_utf16(data, bleft, true, nls_cp);
	cifs_dbg(FYI, "serverDomain=%s\n", ses->serverDomain);

	return;
}

static void decode_ascii_ssetup(char **pbcc_area, __u16 bleft,
				struct cifs_ses *ses,
				const struct nls_table *nls_cp)
{
	int len;
	char *bcc_ptr = *pbcc_area;

	cifs_dbg(FYI, "decode sessetup ascii. bleft %d\n", bleft);

	len = strnlen(bcc_ptr, bleft);
	if (len >= bleft)
		return;

	kfree(ses->serverOS);

	ses->serverOS = kmalloc(len + 1, GFP_KERNEL);
	if (ses->serverOS) {
		memcpy(ses->serverOS, bcc_ptr, len);
		ses->serverOS[len] = 0;
		if (strncmp(ses->serverOS, "OS/2", 4) == 0)
			cifs_dbg(FYI, "OS/2 server\n");
	}

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if (len >= bleft)
		return;

	kfree(ses->serverNOS);

	ses->serverNOS = kmalloc(len + 1, GFP_KERNEL);
	if (ses->serverNOS) {
		memcpy(ses->serverNOS, bcc_ptr, len);
		ses->serverNOS[len] = 0;
	}

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if (len > bleft)
		return;

	/*
	 * No domain field in LANMAN case. Domain is
	 * returned by old servers in the SMB negprot response
	 *
	 * BB For newer servers which do not support Unicode,
	 * but thus do return domain here, we could add parsing
	 * for it later, but it is not very important
	 */
	cifs_dbg(FYI, "ascii: bytes left %d\n", bleft);
}

static int
sess_alloc_buffer(struct sess_data *sess_data, int wct)
{
	int rc;
	struct cifs_ses *ses = sess_data->ses;
	struct smb_hdr *smb_buf;

	rc = small_smb_init_no_tc(SMB_COM_SESSION_SETUP_ANDX, wct, ses,
				  (void **)&smb_buf);

	if (rc < 0)
		return rc;

	sess_data->in_len = rc;
	sess_data->iov[0].iov_base = (char *)smb_buf;
	sess_data->iov[0].iov_len = sess_data->in_len;
	/*
	 * This variable will be used to clear the buffer
	 * allocated above in case of any error in the calling function.
	 */
	sess_data->buf0_type = CIFS_SMALL_BUFFER;

	/* 2000 big enough to fit max user, domain, NOS name etc. */
	sess_data->iov[2].iov_base = kmalloc(2000, GFP_KERNEL);
	if (!sess_data->iov[2].iov_base) {
		rc = -ENOMEM;
		goto out_free_smb_buf;
	}

	return 0;

out_free_smb_buf:
	cifs_small_buf_release(smb_buf);
	sess_data->iov[0].iov_base = NULL;
	sess_data->iov[0].iov_len = 0;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	return rc;
}

static void
sess_free_buffer(struct sess_data *sess_data)
{
	struct kvec *iov = sess_data->iov;

	/*
	 * Zero the session data before freeing, as it might contain sensitive info (keys, etc).
	 * Note that iov[1] is already freed by caller.
	 */
	if (sess_data->buf0_type != CIFS_NO_BUFFER && iov[0].iov_base)
		memzero_explicit(iov[0].iov_base, iov[0].iov_len);

	free_rsp_buf(sess_data->buf0_type, iov[0].iov_base);
	sess_data->buf0_type = CIFS_NO_BUFFER;
	kfree_sensitive(iov[2].iov_base);
}

static int
sess_establish_session(struct sess_data *sess_data)
{
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;

	cifs_server_lock(server);
	if (!server->session_estab) {
		if (server->sign) {
			server->session_key.response =
				kmemdup(ses->auth_key.response,
				ses->auth_key.len, GFP_KERNEL);
			if (!server->session_key.response) {
				cifs_server_unlock(server);
				return -ENOMEM;
			}
			server->session_key.len =
						ses->auth_key.len;
		}
		server->sequence_number = 0x2;
		server->session_estab = true;
	}
	cifs_server_unlock(server);

	cifs_dbg(FYI, "CIFS session established successfully\n");
	return 0;
}

static int
sess_sendreceive(struct sess_data *sess_data)
{
	int rc;
	struct smb_hdr *smb_buf = (struct smb_hdr *) sess_data->iov[0].iov_base;
	__u16 count;
	struct kvec rsp_iov = { NULL, 0 };

	count = sess_data->iov[1].iov_len + sess_data->iov[2].iov_len;
	sess_data->in_len += count;
	put_bcc(count, smb_buf);

	rc = SendReceive2(sess_data->xid, sess_data->ses,
			  sess_data->iov, 3 /* num_iovecs */,
			  &sess_data->buf0_type,
			  CIFS_LOG_ERROR, &rsp_iov);
	cifs_small_buf_release(sess_data->iov[0].iov_base);
	memcpy(&sess_data->iov[0], &rsp_iov, sizeof(struct kvec));

	return rc;
}

static void
sess_auth_ntlmv2(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u32 capabilities;
	__u16 bytes_remaining;

	/* old style NTLM sessionsetup */
	/* wct = 13 */
	rc = sess_alloc_buffer(sess_data, 13);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	capabilities = cifs_ssetup_hdr(ses, server, pSMB);

	pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);

	/* LM2 password would be here if we supported it */
	pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;

	if (ses->user_name != NULL) {
		/* calculate nlmv2 response and session key */
		rc = setup_ntlmv2_rsp(ses, sess_data->nls_cp);
		if (rc) {
			cifs_dbg(VFS, "Error %d during NTLMv2 authentication\n", rc);
			goto out;
		}

		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				ses->auth_key.len - CIFS_SESS_KEY_SIZE);
		bcc_ptr += ses->auth_key.len - CIFS_SESS_KEY_SIZE;

		/* set case sensitive password length after tilen may get
		 * assigned, tilen is 0 otherwise.
		 */
		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);
	} else {
		pSMB->req_no_secext.CaseSensitivePasswordLength = 0;
	}

	if (ses->capabilities & CAP_UNICODE) {
		if (!IS_ALIGNED(sess_data->iov[0].iov_len, 2)) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		unicode_ssetup_strings(&bcc_ptr, ses, sess_data->nls_cp);
	} else {
		ascii_ssetup_strings(&bcc_ptr, ses, sess_data->nls_cp);
	}


	sess_data->iov[2].iov_len = (long) bcc_ptr -
			(long) sess_data->iov[2].iov_base;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	if (smb_buf->WordCount != 3) {
		rc = smb_EIO1(smb_eio_trace_sess_nl2_wcc, smb_buf->WordCount);
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out;
	}

	if (le16_to_cpu(pSMB->resp.Action) & GUEST_LOGIN)
		cifs_dbg(FYI, "Guest login\n"); /* BB mark SesInfo struct? */

	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cifs_dbg(FYI, "UID = %llu\n", ses->Suid);

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	/* BB check if Unicode and decode strings */
	if (bytes_remaining == 0) {
		/* no string area to decode, do nothing */
	} else if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (!IS_ALIGNED((unsigned long)bcc_ptr - (unsigned long)smb_buf, 2)) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses,
				      sess_data->nls_cp);
	} else {
		decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,
				    sess_data->nls_cp);
	}

	rc = sess_establish_session(sess_data);
out:
	sess_data->result = rc;
	sess_data->func = NULL;
	sess_free_buffer(sess_data);
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
}

#ifdef CONFIG_CIFS_UPCALL
static void
sess_auth_kerberos(struct sess_data *sess_data)
{
	int rc = 0;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	char *bcc_ptr;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u32 capabilities;
	__u16 bytes_remaining;
	struct key *spnego_key = NULL;
	struct cifs_spnego_msg *msg;
	u16 blob_len;

	/* extended security */
	/* wct = 12 */
	rc = sess_alloc_buffer(sess_data, 12);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	bcc_ptr = sess_data->iov[2].iov_base;
	capabilities = cifs_ssetup_hdr(ses, server, pSMB);

	spnego_key = cifs_get_spnego_key(ses, server);
	if (IS_ERR(spnego_key)) {
		rc = PTR_ERR(spnego_key);
		spnego_key = NULL;
		goto out;
	}

	msg = spnego_key->payload.data[0];
	/*
	 * check version field to make sure that cifs.upcall is
	 * sending us a response in an expected form
	 */
	if (msg->version != CIFS_SPNEGO_UPCALL_VERSION) {
		cifs_dbg(VFS, "incorrect version of cifs.upcall (expected %d but got %d)\n",
			 CIFS_SPNEGO_UPCALL_VERSION, msg->version);
		rc = -EKEYREJECTED;
		goto out_put_spnego_key;
	}

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

	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	capabilities |= CAP_EXTENDED_SECURITY;
	pSMB->req.Capabilities = cpu_to_le32(capabilities);
	sess_data->iov[1].iov_base = msg->data + msg->sesskey_len;
	sess_data->iov[1].iov_len = msg->secblob_len;
	pSMB->req.SecurityBlobLength = cpu_to_le16(sess_data->iov[1].iov_len);

	if (pSMB->req.hdr.Flags2 & SMBFLG2_UNICODE) {
		/* unicode strings must be word aligned */
		if (!IS_ALIGNED(sess_data->iov[0].iov_len + sess_data->iov[1].iov_len, 2)) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		unicode_oslm_strings(&bcc_ptr, sess_data->nls_cp);
		unicode_domain_string(&bcc_ptr, ses, sess_data->nls_cp);
	} else {
		ascii_oslm_strings(&bcc_ptr, sess_data->nls_cp);
		ascii_domain_string(&bcc_ptr, ses, sess_data->nls_cp);
	}

	sess_data->iov[2].iov_len = (long) bcc_ptr -
			(long) sess_data->iov[2].iov_base;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out_put_spnego_key;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	if (smb_buf->WordCount != 4) {
		rc = smb_EIO1(smb_eio_trace_sess_krb_wcc, smb_buf->WordCount);
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out_put_spnego_key;
	}

	if (le16_to_cpu(pSMB->resp.Action) & GUEST_LOGIN)
		cifs_dbg(FYI, "Guest login\n"); /* BB mark SesInfo struct? */

	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cifs_dbg(FYI, "UID = %llu\n", ses->Suid);

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
	if (blob_len > bytes_remaining) {
		cifs_dbg(VFS, "bad security blob length %d\n",
				blob_len);
		rc = -EINVAL;
		goto out_put_spnego_key;
	}
	bcc_ptr += blob_len;
	bytes_remaining -= blob_len;

	/* BB check if Unicode and decode strings */
	if (bytes_remaining == 0) {
		/* no string area to decode, do nothing */
	} else if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (!IS_ALIGNED((unsigned long)bcc_ptr - (unsigned long)smb_buf, 2)) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses,
				      sess_data->nls_cp);
	} else {
		decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,
				    sess_data->nls_cp);
	}

	rc = sess_establish_session(sess_data);
out_put_spnego_key:
	key_invalidate(spnego_key);
	key_put(spnego_key);
out:
	sess_data->result = rc;
	sess_data->func = NULL;
	sess_free_buffer(sess_data);
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
}

#endif /* ! CONFIG_CIFS_UPCALL */

/*
 * The required kvec buffers have to be allocated before calling this
 * function.
 */
static int
_sess_auth_rawntlmssp_assemble_req(struct sess_data *sess_data)
{
	SESSION_SETUP_ANDX *pSMB;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u32 capabilities;
	char *bcc_ptr;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;

	capabilities = cifs_ssetup_hdr(ses, server, pSMB);
	pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
	capabilities |= CAP_EXTENDED_SECURITY;
	pSMB->req.Capabilities |= cpu_to_le32(capabilities);

	bcc_ptr = sess_data->iov[2].iov_base;

	if (pSMB->req.hdr.Flags2 & SMBFLG2_UNICODE) {
		/* unicode strings must be word aligned */
		if (!IS_ALIGNED(sess_data->iov[0].iov_len + sess_data->iov[1].iov_len, 2)) {
			*bcc_ptr = 0;
			bcc_ptr++;
		}
		unicode_oslm_strings(&bcc_ptr, sess_data->nls_cp);
	} else {
		ascii_oslm_strings(&bcc_ptr, sess_data->nls_cp);
	}

	sess_data->iov[2].iov_len = (long) bcc_ptr -
					(long) sess_data->iov[2].iov_base;

	return 0;
}

static void
sess_auth_rawntlmssp_authenticate(struct sess_data *sess_data);

static void
sess_auth_rawntlmssp_negotiate(struct sess_data *sess_data)
{
	int rc;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u16 bytes_remaining;
	char *bcc_ptr;
	unsigned char *ntlmsspblob = NULL;
	u16 blob_len;

	cifs_dbg(FYI, "rawntlmssp session setup negotiate phase\n");

	/*
	 * if memory allocation is successful, caller of this function
	 * frees it.
	 */
	ses->ntlmssp = kmalloc(sizeof(struct ntlmssp_auth), GFP_KERNEL);
	if (!ses->ntlmssp) {
		rc = -ENOMEM;
		goto out;
	}
	ses->ntlmssp->sesskey_per_smbsess = false;

	/* wct = 12 */
	rc = sess_alloc_buffer(sess_data, 12);
	if (rc)
		goto out;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;

	/* Build security blob before we assemble the request */
	rc = build_ntlmssp_negotiate_blob(&ntlmsspblob,
				     &blob_len, ses, server,
				     sess_data->nls_cp);
	if (rc)
		goto out_free_ntlmsspblob;

	sess_data->iov[1].iov_len = blob_len;
	sess_data->iov[1].iov_base = ntlmsspblob;
	pSMB->req.SecurityBlobLength = cpu_to_le16(blob_len);

	rc = _sess_auth_rawntlmssp_assemble_req(sess_data);
	if (rc)
		goto out_free_ntlmsspblob;

	rc = sess_sendreceive(sess_data);

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;

	/* If true, rc here is expected and not an error */
	if (sess_data->buf0_type != CIFS_NO_BUFFER &&
	    smb_buf->Status.CifsError ==
			cpu_to_le32(NT_STATUS_MORE_PROCESSING_REQUIRED))
		rc = 0;

	if (rc)
		goto out_free_ntlmsspblob;

	cifs_dbg(FYI, "rawntlmssp session setup challenge phase\n");

	if (smb_buf->WordCount != 4) {
		rc = smb_EIO1(smb_eio_trace_sess_rawnl_neg_wcc, smb_buf->WordCount);
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out_free_ntlmsspblob;
	}

	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cifs_dbg(FYI, "UID = %llu\n", ses->Suid);

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
	if (blob_len > bytes_remaining) {
		cifs_dbg(VFS, "bad security blob length %d\n",
				blob_len);
		rc = -EINVAL;
		goto out_free_ntlmsspblob;
	}

	rc = decode_ntlmssp_challenge(bcc_ptr, blob_len, ses);

out_free_ntlmsspblob:
	kfree_sensitive(ntlmsspblob);
out:
	sess_free_buffer(sess_data);

	if (!rc) {
		sess_data->func = sess_auth_rawntlmssp_authenticate;
		return;
	}

	/* Else error. Cleanup */
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
	kfree_sensitive(ses->ntlmssp);
	ses->ntlmssp = NULL;

	sess_data->func = NULL;
	sess_data->result = rc;
}

static void
sess_auth_rawntlmssp_authenticate(struct sess_data *sess_data)
{
	int rc;
	struct smb_hdr *smb_buf;
	SESSION_SETUP_ANDX *pSMB;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;
	__u16 bytes_remaining;
	char *bcc_ptr;
	unsigned char *ntlmsspblob = NULL;
	u16 blob_len;

	cifs_dbg(FYI, "rawntlmssp session setup authenticate phase\n");

	/* wct = 12 */
	rc = sess_alloc_buffer(sess_data, 12);
	if (rc)
		goto out;

	/* Build security blob before we assemble the request */
	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)pSMB;
	rc = build_ntlmssp_auth_blob(&ntlmsspblob,
					&blob_len, ses, server,
					sess_data->nls_cp);
	if (rc)
		goto out_free_ntlmsspblob;
	sess_data->iov[1].iov_len = blob_len;
	sess_data->iov[1].iov_base = ntlmsspblob;
	pSMB->req.SecurityBlobLength = cpu_to_le16(blob_len);
	/*
	 * Make sure that we tell the server that we are using
	 * the uid that it just gave us back on the response
	 * (challenge)
	 */
	smb_buf->Uid = ses->Suid;

	rc = _sess_auth_rawntlmssp_assemble_req(sess_data);
	if (rc)
		goto out_free_ntlmsspblob;

	rc = sess_sendreceive(sess_data);
	if (rc)
		goto out_free_ntlmsspblob;

	pSMB = (SESSION_SETUP_ANDX *)sess_data->iov[0].iov_base;
	smb_buf = (struct smb_hdr *)sess_data->iov[0].iov_base;
	if (smb_buf->WordCount != 4) {
		rc = smb_EIO1(smb_eio_trace_sess_rawnl_auth_wcc, smb_buf->WordCount);
		cifs_dbg(VFS, "bad word count %d\n", smb_buf->WordCount);
		goto out_free_ntlmsspblob;
	}

	if (le16_to_cpu(pSMB->resp.Action) & GUEST_LOGIN)
		cifs_dbg(FYI, "Guest login\n"); /* BB mark SesInfo struct? */

	if (ses->Suid != smb_buf->Uid) {
		ses->Suid = smb_buf->Uid;
		cifs_dbg(FYI, "UID changed! new UID = %llu\n", ses->Suid);
	}

	bytes_remaining = get_bcc(smb_buf);
	bcc_ptr = pByteArea(smb_buf);
	blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
	if (blob_len > bytes_remaining) {
		cifs_dbg(VFS, "bad security blob length %d\n",
				blob_len);
		rc = -EINVAL;
		goto out_free_ntlmsspblob;
	}
	bcc_ptr += blob_len;
	bytes_remaining -= blob_len;


	/* BB check if Unicode and decode strings */
	if (bytes_remaining == 0) {
		/* no string area to decode, do nothing */
	} else if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (!IS_ALIGNED((unsigned long)bcc_ptr - (unsigned long)smb_buf, 2)) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses,
				      sess_data->nls_cp);
	} else {
		decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,
				    sess_data->nls_cp);
	}

out_free_ntlmsspblob:
	kfree_sensitive(ntlmsspblob);
out:
	sess_free_buffer(sess_data);

	if (!rc)
		rc = sess_establish_session(sess_data);

	/* Cleanup */
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.response = NULL;
	kfree_sensitive(ses->ntlmssp);
	ses->ntlmssp = NULL;

	sess_data->func = NULL;
	sess_data->result = rc;
}

static int select_sec(struct sess_data *sess_data)
{
	int type;
	struct cifs_ses *ses = sess_data->ses;
	struct TCP_Server_Info *server = sess_data->server;

	type = cifs_select_sectype(server, ses->sectype);
	cifs_dbg(FYI, "sess setup type %d\n", type);
	if (type == Unspecified) {
		cifs_dbg(VFS, "Unable to select appropriate authentication method!\n");
		return -EINVAL;
	}

	switch (type) {
	case NTLMv2:
		sess_data->func = sess_auth_ntlmv2;
		break;
	case Kerberos:
#ifdef CONFIG_CIFS_UPCALL
		sess_data->func = sess_auth_kerberos;
		break;
#else
		cifs_dbg(VFS, "Kerberos negotiated but upcall support disabled!\n");
		return -ENOSYS;
#endif /* CONFIG_CIFS_UPCALL */
	case RawNTLMSSP:
		sess_data->func = sess_auth_rawntlmssp_negotiate;
		break;
	default:
		cifs_dbg(VFS, "secType %d not supported!\n", type);
		return -ENOSYS;
	}

	return 0;
}

int CIFS_SessSetup(const unsigned int xid, struct cifs_ses *ses,
		   struct TCP_Server_Info *server,
		   const struct nls_table *nls_cp)
{
	int rc = 0;
	struct sess_data *sess_data;

	if (ses == NULL) {
		WARN(1, "%s: ses == NULL!", __func__);
		return -EINVAL;
	}

	sess_data = kzalloc(sizeof(struct sess_data), GFP_KERNEL);
	if (!sess_data)
		return -ENOMEM;

	sess_data->xid = xid;
	sess_data->ses = ses;
	sess_data->server = server;
	sess_data->buf0_type = CIFS_NO_BUFFER;
	sess_data->nls_cp = (struct nls_table *) nls_cp;

	rc = select_sec(sess_data);
	if (rc)
		goto out;

	while (sess_data->func)
		sess_data->func(sess_data);

	/* Store result before we free sess_data */
	rc = sess_data->result;

out:
	kfree_sensitive(sess_data);
	return rc;
}
