/*
 *   fs/cifs/sess.c
 *
 *   SMB/CIFS session setup handling routines
 *
 *   Copyright (c) International Business Machines  Corp., 2006, 2009
 *   Author(s): Steve French (sfrench@us.ibm.com)
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

#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "ntlmssp.h"
#include "nterr.h"
#include <linux/utsname.h>
#include <linux/slab.h>
#include "cifs_spnego.h"

/*
 * Checks if this is the first smb session to be reconnected after
 * the socket has been reestablished (so we know whether to use vc 0).
 * Called while holding the cifs_tcp_ses_lock, so do not block
 */
static bool is_first_ses_reconnect(struct cifsSesInfo *ses)
{
	struct list_head *tmp;
	struct cifsSesInfo *tmp_ses;

	list_for_each(tmp, &ses->server->smb_ses_list) {
		tmp_ses = list_entry(tmp, struct cifsSesInfo,
				     smb_ses_list);
		if (tmp_ses->need_reconnect == false)
			return false;
	}
	/* could not find a session that was already connected,
	   this must be the first one we are reconnecting */
	return true;
}

/*
 *	vc number 0 is treated specially by some servers, and should be the
 *      first one we request.  After that we can use vcnumbers up to maxvcs,
 *	one for each smb session (some Windows versions set maxvcs incorrectly
 *	so maxvc=1 can be ignored).  If we have too many vcs, we can reuse
 *	any vc but zero (some servers reset the connection on vcnum zero)
 *
 */
static __le16 get_next_vcnum(struct cifsSesInfo *ses)
{
	__u16 vcnum = 0;
	struct list_head *tmp;
	struct cifsSesInfo *tmp_ses;
	__u16 max_vcs = ses->server->max_vcs;
	__u16 i;
	int free_vc_found = 0;

	/* Quoting the MS-SMB specification: "Windows-based SMB servers set this
	field to one but do not enforce this limit, which allows an SMB client
	to establish more virtual circuits than allowed by this value ... but
	other server implementations can enforce this limit." */
	if (max_vcs < 2)
		max_vcs = 0xFFFF;

	spin_lock(&cifs_tcp_ses_lock);
	if ((ses->need_reconnect) && is_first_ses_reconnect(ses))
			goto get_vc_num_exit;  /* vcnum will be zero */
	for (i = ses->server->srv_count - 1; i < max_vcs; i++) {
		if (i == 0) /* this is the only connection, use vc 0 */
			break;

		free_vc_found = 1;

		list_for_each(tmp, &ses->server->smb_ses_list) {
			tmp_ses = list_entry(tmp, struct cifsSesInfo,
					     smb_ses_list);
			if (tmp_ses->vcnum == i) {
				free_vc_found = 0;
				break; /* found duplicate, try next vcnum */
			}
		}
		if (free_vc_found)
			break; /* we found a vcnumber that will work - use it */
	}

	if (i == 0)
		vcnum = 0; /* for most common case, ie if one smb session, use
			      vc zero.  Also for case when no free vcnum, zero
			      is safest to send (some clients only send zero) */
	else if (free_vc_found == 0)
		vcnum = 1;  /* we can not reuse vc=0 safely, since some servers
				reset all uids on that, but 1 is ok. */
	else
		vcnum = i;
	ses->vcnum = vcnum;
get_vc_num_exit:
	spin_unlock(&cifs_tcp_ses_lock);

	return cpu_to_le16(vcnum);
}

static __u32 cifs_ssetup_hdr(struct cifsSesInfo *ses, SESSION_SETUP_ANDX *pSMB)
{
	__u32 capabilities = 0;

	/* init fields common to all four types of SessSetup */
	/* Note that offsets for first seven fields in req struct are same  */
	/*	in CIFS Specs so does not matter which of 3 forms of struct */
	/*	that we use in next few lines                               */
	/* Note that header is initialized to zero in header_assemble */
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);
	pSMB->req.VcNumber = get_next_vcnum(ses);

	/* Now no need to set SMBFLG_CASELESS or obsolete CANONICAL PATH */

	/* BB verify whether signing required on neg or just on auth frame
	   (and NTLM case) */

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;

	if (ses->server->secMode &
	    (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
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
	bytes_ret = cifs_strtoUCS((__le16 *)bcc_ptr, "Linux version ", 32,
				  nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bytes_ret = cifs_strtoUCS((__le16 *) bcc_ptr, init_utsname()->release,
				  32, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* trailing null */

	bytes_ret = cifs_strtoUCS((__le16 *) bcc_ptr, CIFS_NETWORK_OPSYS,
				  32, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* trailing null */

	*pbcc_area = bcc_ptr;
}

static void unicode_domain_string(char **pbcc_area, struct cifsSesInfo *ses,
				   const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* copy domain */
	if (ses->domainName == NULL) {
		/* Sending null domain better than using a bogus domain name (as
		we did briefly in 2.6.18) since server will use its default */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
		bytes_ret = 0;
	} else
		bytes_ret = cifs_strtoUCS((__le16 *) bcc_ptr, ses->domainName,
					  256, nls_cp);
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2;  /* account for null terminator */

	*pbcc_area = bcc_ptr;
}


static void unicode_ssetup_strings(char **pbcc_area, struct cifsSesInfo *ses,
				   const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* BB FIXME add check that strings total less
	than 335 or will need to send them as arrays */

	/* unicode strings, must be word aligned before the call */
/*	if ((long) bcc_ptr % 2)	{
		*bcc_ptr = 0;
		bcc_ptr++;
	} */
	/* copy user */
	if (ses->userName == NULL) {
		/* null user mount */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
	} else {
		bytes_ret = cifs_strtoUCS((__le16 *) bcc_ptr, ses->userName,
					  MAX_USERNAME_SIZE, nls_cp);
	}
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* account for null termination */

	unicode_domain_string(&bcc_ptr, ses, nls_cp);
	unicode_oslm_strings(&bcc_ptr, nls_cp);

	*pbcc_area = bcc_ptr;
}

static void ascii_ssetup_strings(char **pbcc_area, struct cifsSesInfo *ses,
				 const struct nls_table *nls_cp)
{
	char *bcc_ptr = *pbcc_area;

	/* copy user */
	/* BB what about null user mounts - check that we do this BB */
	/* copy user */
	if (ses->userName == NULL) {
		/* BB what about null user mounts - check that we do this BB */
	} else {
		strncpy(bcc_ptr, ses->userName, MAX_USERNAME_SIZE);
	}
	bcc_ptr += strnlen(ses->userName, MAX_USERNAME_SIZE);
	*bcc_ptr = 0;
	bcc_ptr++; /* account for null termination */

	/* copy domain */

	if (ses->domainName != NULL) {
		strncpy(bcc_ptr, ses->domainName, 256);
		bcc_ptr += strnlen(ses->domainName, 256);
	} /* else we will send a null domain name
	     so the server will default to its own domain */
	*bcc_ptr = 0;
	bcc_ptr++;

	/* BB check for overflow here */

	strcpy(bcc_ptr, "Linux version ");
	bcc_ptr += strlen("Linux version ");
	strcpy(bcc_ptr, init_utsname()->release);
	bcc_ptr += strlen(init_utsname()->release) + 1;

	strcpy(bcc_ptr, CIFS_NETWORK_OPSYS);
	bcc_ptr += strlen(CIFS_NETWORK_OPSYS) + 1;

	*pbcc_area = bcc_ptr;
}

static void
decode_unicode_ssetup(char **pbcc_area, int bleft, struct cifsSesInfo *ses,
		      const struct nls_table *nls_cp)
{
	int len;
	char *data = *pbcc_area;

	cFYI(1, "bleft %d", bleft);

	/*
	 * Windows servers do not always double null terminate their final
	 * Unicode string. Check to see if there are an uneven number of bytes
	 * left. If so, then add an extra NULL pad byte to the end of the
	 * response.
	 *
	 * See section 2.7.2 in "Implementing CIFS" for details
	 */
	if (bleft % 2) {
		data[bleft] = 0;
		++bleft;
	}

	kfree(ses->serverOS);
	ses->serverOS = cifs_strndup_from_ucs(data, bleft, true, nls_cp);
	cFYI(1, "serverOS=%s", ses->serverOS);
	len = (UniStrnlen((wchar_t *) data, bleft / 2) * 2) + 2;
	data += len;
	bleft -= len;
	if (bleft <= 0)
		return;

	kfree(ses->serverNOS);
	ses->serverNOS = cifs_strndup_from_ucs(data, bleft, true, nls_cp);
	cFYI(1, "serverNOS=%s", ses->serverNOS);
	len = (UniStrnlen((wchar_t *) data, bleft / 2) * 2) + 2;
	data += len;
	bleft -= len;
	if (bleft <= 0)
		return;

	kfree(ses->serverDomain);
	ses->serverDomain = cifs_strndup_from_ucs(data, bleft, true, nls_cp);
	cFYI(1, "serverDomain=%s", ses->serverDomain);

	return;
}

static int decode_ascii_ssetup(char **pbcc_area, int bleft,
			       struct cifsSesInfo *ses,
			       const struct nls_table *nls_cp)
{
	int rc = 0;
	int len;
	char *bcc_ptr = *pbcc_area;

	cFYI(1, "decode sessetup ascii. bleft %d", bleft);

	len = strnlen(bcc_ptr, bleft);
	if (len >= bleft)
		return rc;

	kfree(ses->serverOS);

	ses->serverOS = kzalloc(len + 1, GFP_KERNEL);
	if (ses->serverOS)
		strncpy(ses->serverOS, bcc_ptr, len);
	if (strncmp(ses->serverOS, "OS/2", 4) == 0) {
			cFYI(1, "OS/2 server");
			ses->flags |= CIFS_SES_OS2;
	}

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if (len >= bleft)
		return rc;

	kfree(ses->serverNOS);

	ses->serverNOS = kzalloc(len + 1, GFP_KERNEL);
	if (ses->serverNOS)
		strncpy(ses->serverNOS, bcc_ptr, len);

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if (len > bleft)
		return rc;

	/* No domain field in LANMAN case. Domain is
	   returned by old servers in the SMB negprot response */
	/* BB For newer servers which do not support Unicode,
	   but thus do return domain here we could add parsing
	   for it later, but it is not very important */
	cFYI(1, "ascii: bytes left %d", bleft);

	return rc;
}

static int decode_ntlmssp_challenge(char *bcc_ptr, int blob_len,
				    struct cifsSesInfo *ses)
{
	unsigned int tioffset; /* challenge message target info area */
	unsigned int tilen; /* challenge message target info area length  */

	CHALLENGE_MESSAGE *pblob = (CHALLENGE_MESSAGE *)bcc_ptr;

	if (blob_len < sizeof(CHALLENGE_MESSAGE)) {
		cERROR(1, "challenge blob len %d too small", blob_len);
		return -EINVAL;
	}

	if (memcmp(pblob->Signature, "NTLMSSP", 8)) {
		cERROR(1, "blob signature incorrect %s", pblob->Signature);
		return -EINVAL;
	}
	if (pblob->MessageType != NtLmChallenge) {
		cERROR(1, "Incorrect message type %d", pblob->MessageType);
		return -EINVAL;
	}

	memcpy(ses->ntlmssp->cryptkey, pblob->Challenge, CIFS_CRYPTO_KEY_SIZE);
	/* BB we could decode pblob->NegotiateFlags; some may be useful */
	/* In particular we can examine sign flags */
	/* BB spec says that if AvId field of MsvAvTimestamp is populated then
		we must set the MIC field of the AUTHENTICATE_MESSAGE */
	ses->ntlmssp->server_flags = le32_to_cpu(pblob->NegotiateFlags);
	tioffset = cpu_to_le16(pblob->TargetInfoArray.BufferOffset);
	tilen = cpu_to_le16(pblob->TargetInfoArray.Length);
	if (tilen) {
		ses->auth_key.response = kmalloc(tilen, GFP_KERNEL);
		if (!ses->auth_key.response) {
			cERROR(1, "Challenge target info allocation failure");
			return -ENOMEM;
		}
		memcpy(ses->auth_key.response, bcc_ptr + tioffset, tilen);
		ses->auth_key.len = tilen;
	}

	return 0;
}

#ifdef CONFIG_CIFS_EXPERIMENTAL
/* BB Move to ntlmssp.c eventually */

/* We do not malloc the blob, it is passed in pbuffer, because
   it is fixed size, and small, making this approach cleaner */
static void build_ntlmssp_negotiate_blob(unsigned char *pbuffer,
					 struct cifsSesInfo *ses)
{
	NEGOTIATE_MESSAGE *sec_blob = (NEGOTIATE_MESSAGE *)pbuffer;
	__u32 flags;

	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmNegotiate;

	/* BB is NTLMV2 session security format easier to use here? */
	flags = NTLMSSP_NEGOTIATE_56 |	NTLMSSP_REQUEST_TARGET |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM;
	if (ses->server->secMode &
			(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED)) {
		flags |= NTLMSSP_NEGOTIATE_SIGN;
		if (!ses->server->session_estab)
			flags |= NTLMSSP_NEGOTIATE_KEY_XCH |
				NTLMSSP_NEGOTIATE_EXTENDED_SEC;
	}

	sec_blob->NegotiateFlags |= cpu_to_le32(flags);

	sec_blob->WorkstationName.BufferOffset = 0;
	sec_blob->WorkstationName.Length = 0;
	sec_blob->WorkstationName.MaximumLength = 0;

	/* Domain name is sent on the Challenge not Negotiate NTLMSSP request */
	sec_blob->DomainName.BufferOffset = 0;
	sec_blob->DomainName.Length = 0;
	sec_blob->DomainName.MaximumLength = 0;
}

/* We do not malloc the blob, it is passed in pbuffer, because its
   maximum possible size is fixed and small, making this approach cleaner.
   This function returns the length of the data in the blob */
static int build_ntlmssp_auth_blob(unsigned char *pbuffer,
					u16 *buflen,
				   struct cifsSesInfo *ses,
				   const struct nls_table *nls_cp)
{
	int rc;
	AUTHENTICATE_MESSAGE *sec_blob = (AUTHENTICATE_MESSAGE *)pbuffer;
	__u32 flags;
	unsigned char *tmp;

	memcpy(sec_blob->Signature, NTLMSSP_SIGNATURE, 8);
	sec_blob->MessageType = NtLmAuthenticate;

	flags = NTLMSSP_NEGOTIATE_56 |
		NTLMSSP_REQUEST_TARGET | NTLMSSP_NEGOTIATE_TARGET_INFO |
		NTLMSSP_NEGOTIATE_128 | NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM;
	if (ses->server->secMode &
	   (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
		flags |= NTLMSSP_NEGOTIATE_SIGN;
	if (ses->server->secMode & SECMODE_SIGN_REQUIRED)
		flags |= NTLMSSP_NEGOTIATE_ALWAYS_SIGN;

	tmp = pbuffer + sizeof(AUTHENTICATE_MESSAGE);
	sec_blob->NegotiateFlags |= cpu_to_le32(flags);

	sec_blob->LmChallengeResponse.BufferOffset =
				cpu_to_le32(sizeof(AUTHENTICATE_MESSAGE));
	sec_blob->LmChallengeResponse.Length = 0;
	sec_blob->LmChallengeResponse.MaximumLength = 0;

	sec_blob->NtChallengeResponse.BufferOffset = cpu_to_le32(tmp - pbuffer);
	rc = setup_ntlmv2_rsp(ses, nls_cp);
	if (rc) {
		cERROR(1, "Error %d during NTLMSSP authentication", rc);
		goto setup_ntlmv2_ret;
	}
	memcpy(tmp, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
			ses->auth_key.len - CIFS_SESS_KEY_SIZE);
	tmp += ses->auth_key.len - CIFS_SESS_KEY_SIZE;

	sec_blob->NtChallengeResponse.Length =
			cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);
	sec_blob->NtChallengeResponse.MaximumLength =
			cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);

	if (ses->domainName == NULL) {
		sec_blob->DomainName.BufferOffset = cpu_to_le32(tmp - pbuffer);
		sec_blob->DomainName.Length = 0;
		sec_blob->DomainName.MaximumLength = 0;
		tmp += 2;
	} else {
		int len;
		len = cifs_strtoUCS((__le16 *)tmp, ses->domainName,
				    MAX_USERNAME_SIZE, nls_cp);
		len *= 2; /* unicode is 2 bytes each */
		sec_blob->DomainName.BufferOffset = cpu_to_le32(tmp - pbuffer);
		sec_blob->DomainName.Length = cpu_to_le16(len);
		sec_blob->DomainName.MaximumLength = cpu_to_le16(len);
		tmp += len;
	}

	if (ses->userName == NULL) {
		sec_blob->UserName.BufferOffset = cpu_to_le32(tmp - pbuffer);
		sec_blob->UserName.Length = 0;
		sec_blob->UserName.MaximumLength = 0;
		tmp += 2;
	} else {
		int len;
		len = cifs_strtoUCS((__le16 *)tmp, ses->userName,
				    MAX_USERNAME_SIZE, nls_cp);
		len *= 2; /* unicode is 2 bytes each */
		sec_blob->UserName.BufferOffset = cpu_to_le32(tmp - pbuffer);
		sec_blob->UserName.Length = cpu_to_le16(len);
		sec_blob->UserName.MaximumLength = cpu_to_le16(len);
		tmp += len;
	}

	sec_blob->WorkstationName.BufferOffset = cpu_to_le32(tmp - pbuffer);
	sec_blob->WorkstationName.Length = 0;
	sec_blob->WorkstationName.MaximumLength = 0;
	tmp += 2;

	if ((ses->ntlmssp->server_flags & NTLMSSP_NEGOTIATE_KEY_XCH) &&
			!calc_seckey(ses)) {
		memcpy(tmp, ses->ntlmssp->ciphertext, CIFS_CPHTXT_SIZE);
		sec_blob->SessionKey.BufferOffset = cpu_to_le32(tmp - pbuffer);
		sec_blob->SessionKey.Length = cpu_to_le16(CIFS_CPHTXT_SIZE);
		sec_blob->SessionKey.MaximumLength =
				cpu_to_le16(CIFS_CPHTXT_SIZE);
		tmp += CIFS_CPHTXT_SIZE;
	} else {
		sec_blob->SessionKey.BufferOffset = cpu_to_le32(tmp - pbuffer);
		sec_blob->SessionKey.Length = 0;
		sec_blob->SessionKey.MaximumLength = 0;
	}

setup_ntlmv2_ret:
	*buflen = tmp - pbuffer;
	return rc;
}


static void setup_ntlmssp_neg_req(SESSION_SETUP_ANDX *pSMB,
				 struct cifsSesInfo *ses)
{
	build_ntlmssp_negotiate_blob(&pSMB->req.SecurityBlob[0], ses);
	pSMB->req.SecurityBlobLength = cpu_to_le16(sizeof(NEGOTIATE_MESSAGE));

	return;
}
#endif

int
CIFS_SessSetup(unsigned int xid, struct cifsSesInfo *ses,
	       const struct nls_table *nls_cp)
{
	int rc = 0;
	int wct;
	struct smb_hdr *smb_buf;
	char *bcc_ptr;
	char *str_area;
	SESSION_SETUP_ANDX *pSMB;
	__u32 capabilities;
	int count;
	int resp_buf_type;
	struct kvec iov[3];
	enum securityEnum type;
	__u16 action;
	int bytes_remaining;
	struct key *spnego_key = NULL;
	__le32 phase = NtLmNegotiate; /* NTLMSSP, if needed, is multistage */
	u16 blob_len;
	char *ntlmsspblob = NULL;

	if (ses == NULL)
		return -EINVAL;

	type = ses->server->secType;
	cFYI(1, "sess setup type %d", type);
	if (type == RawNTLMSSP) {
		/* if memory allocation is successful, caller of this function
		 * frees it.
		 */
		ses->ntlmssp = kmalloc(sizeof(struct ntlmssp_auth), GFP_KERNEL);
		if (!ses->ntlmssp)
			return -ENOMEM;
	}

ssetup_ntlmssp_authenticate:
	if (phase == NtLmChallenge)
		phase = NtLmAuthenticate; /* if ntlmssp, now final phase */

	if (type == LANMAN) {
#ifndef CONFIG_CIFS_WEAK_PW_HASH
		/* LANMAN and plaintext are less secure and off by default.
		So we make this explicitly be turned on in kconfig (in the
		build) and turned on at runtime (changed from the default)
		in proc/fs/cifs or via mount parm.  Unfortunately this is
		needed for old Win (e.g. Win95), some obscure NAS and OS/2 */
		return -EOPNOTSUPP;
#endif
		wct = 10; /* lanman 2 style sessionsetup */
	} else if ((type == NTLM) || (type == NTLMv2)) {
		/* For NTLMv2 failures eventually may need to retry NTLM */
		wct = 13; /* old style NTLM sessionsetup */
	} else /* same size: negotiate or auth, NTLMSSP or extended security */
		wct = 12;

	rc = small_smb_init_no_tc(SMB_COM_SESSION_SETUP_ANDX, wct, ses,
			    (void **)&smb_buf);
	if (rc)
		return rc;

	pSMB = (SESSION_SETUP_ANDX *)smb_buf;

	capabilities = cifs_ssetup_hdr(ses, pSMB);

	/* we will send the SMB in three pieces:
	a fixed length beginning part, an optional
	SPNEGO blob (which can be zero length), and a
	last part which will include the strings
	and rest of bcc area. This allows us to avoid
	a large buffer 17K allocation */
	iov[0].iov_base = (char *)pSMB;
	iov[0].iov_len = smb_buf->smb_buf_length + 4;

	/* setting this here allows the code at the end of the function
	   to free the request buffer if there's an error */
	resp_buf_type = CIFS_SMALL_BUFFER;

	/* 2000 big enough to fit max user, domain, NOS name etc. */
	str_area = kmalloc(2000, GFP_KERNEL);
	if (str_area == NULL) {
		rc = -ENOMEM;
		goto ssetup_exit;
	}
	bcc_ptr = str_area;

	ses->flags &= ~CIFS_SES_LANMAN;

	iov[1].iov_base = NULL;
	iov[1].iov_len = 0;

	if (type == LANMAN) {
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		char lnm_session_key[CIFS_SESS_KEY_SIZE];

		pSMB->req.hdr.Flags2 &= ~SMBFLG2_UNICODE;

		/* no capabilities flags in old lanman negotiation */

		pSMB->old_req.PasswordLength = cpu_to_le16(CIFS_SESS_KEY_SIZE);

		/* Calculate hash with password and copy into bcc_ptr.
		 * Encryption Key (stored as in cryptkey) gets used if the
		 * security mode bit in Negottiate Protocol response states
		 * to use challenge/response method (i.e. Password bit is 1).
		 */

		calc_lanman_hash(ses->password, ses->server->cryptkey,
				 ses->server->secMode & SECMODE_PW_ENCRYPT ?
					true : false, lnm_session_key);

		ses->flags |= CIFS_SES_LANMAN;
		memcpy(bcc_ptr, (char *)lnm_session_key, CIFS_SESS_KEY_SIZE);
		bcc_ptr += CIFS_SESS_KEY_SIZE;

		/* can not sign if LANMAN negotiated so no need
		to calculate signing key? but what if server
		changed to do higher than lanman dialect and
		we reconnected would we ever calc signing_key? */

		cFYI(1, "Negotiating LANMAN setting up strings");
		/* Unicode not allowed for LANMAN dialects */
		ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
#endif
	} else if (type == NTLM) {
		pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);
		pSMB->req_no_secext.CaseInsensitivePasswordLength =
			cpu_to_le16(CIFS_AUTH_RESP_SIZE);
		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(CIFS_AUTH_RESP_SIZE);

		/* calculate ntlm response and session key */
		rc = setup_ntlm_response(ses);
		if (rc) {
			cERROR(1, "Error %d during NTLM authentication", rc);
			goto ssetup_exit;
		}

		/* copy ntlm response */
		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				CIFS_AUTH_RESP_SIZE);
		bcc_ptr += CIFS_AUTH_RESP_SIZE;
		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				CIFS_AUTH_RESP_SIZE);
		bcc_ptr += CIFS_AUTH_RESP_SIZE;

		if (ses->capabilities & CAP_UNICODE) {
			/* unicode strings must be word aligned */
			if (iov[0].iov_len % 2) {
				*bcc_ptr = 0;
				bcc_ptr++;
			}
			unicode_ssetup_strings(&bcc_ptr, ses, nls_cp);
		} else
			ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
	} else if (type == NTLMv2) {
		pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);

		/* LM2 password would be here if we supported it */
		pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;

		/* calculate nlmv2 response and session key */
		rc = setup_ntlmv2_rsp(ses, nls_cp);
		if (rc) {
			cERROR(1, "Error %d during NTLMv2 authentication", rc);
			goto ssetup_exit;
		}
		memcpy(bcc_ptr, ses->auth_key.response + CIFS_SESS_KEY_SIZE,
				ses->auth_key.len - CIFS_SESS_KEY_SIZE);
		bcc_ptr += ses->auth_key.len - CIFS_SESS_KEY_SIZE;

		/* set case sensitive password length after tilen may get
		 * assigned, tilen is 0 otherwise.
		 */
		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(ses->auth_key.len - CIFS_SESS_KEY_SIZE);

		if (ses->capabilities & CAP_UNICODE) {
			if (iov[0].iov_len % 2) {
				*bcc_ptr = 0;
				bcc_ptr++;
			}
			unicode_ssetup_strings(&bcc_ptr, ses, nls_cp);
		} else
			ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
	} else if (type == Kerberos) {
#ifdef CONFIG_CIFS_UPCALL
		struct cifs_spnego_msg *msg;

		spnego_key = cifs_get_spnego_key(ses);
		if (IS_ERR(spnego_key)) {
			rc = PTR_ERR(spnego_key);
			spnego_key = NULL;
			goto ssetup_exit;
		}

		msg = spnego_key->payload.data;
		/* check version field to make sure that cifs.upcall is
		   sending us a response in an expected form */
		if (msg->version != CIFS_SPNEGO_UPCALL_VERSION) {
			cERROR(1, "incorrect version of cifs.upcall (expected"
				   " %d but got %d)",
				   CIFS_SPNEGO_UPCALL_VERSION, msg->version);
			rc = -EKEYREJECTED;
			goto ssetup_exit;
		}

		ses->auth_key.response = kmalloc(msg->sesskey_len, GFP_KERNEL);
		if (!ses->auth_key.response) {
			cERROR(1, "Kerberos can't allocate (%u bytes) memory",
					msg->sesskey_len);
			rc = -ENOMEM;
			goto ssetup_exit;
		}
		memcpy(ses->auth_key.response, msg->data, msg->sesskey_len);
		ses->auth_key.len = msg->sesskey_len;

		pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
		capabilities |= CAP_EXTENDED_SECURITY;
		pSMB->req.Capabilities = cpu_to_le32(capabilities);
		iov[1].iov_base = msg->data + msg->sesskey_len;
		iov[1].iov_len = msg->secblob_len;
		pSMB->req.SecurityBlobLength = cpu_to_le16(iov[1].iov_len);

		if (ses->capabilities & CAP_UNICODE) {
			/* unicode strings must be word aligned */
			if ((iov[0].iov_len + iov[1].iov_len) % 2) {
				*bcc_ptr = 0;
				bcc_ptr++;
			}
			unicode_oslm_strings(&bcc_ptr, nls_cp);
			unicode_domain_string(&bcc_ptr, ses, nls_cp);
		} else
		/* BB: is this right? */
			ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
#else /* ! CONFIG_CIFS_UPCALL */
		cERROR(1, "Kerberos negotiated but upcall support disabled!");
		rc = -ENOSYS;
		goto ssetup_exit;
#endif /* CONFIG_CIFS_UPCALL */
	} else {
#ifdef CONFIG_CIFS_EXPERIMENTAL
		if (type == RawNTLMSSP) {
			if ((pSMB->req.hdr.Flags2 & SMBFLG2_UNICODE) == 0) {
				cERROR(1, "NTLMSSP requires Unicode support");
				rc = -ENOSYS;
				goto ssetup_exit;
			}

			cFYI(1, "ntlmssp session setup phase %d", phase);
			pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
			capabilities |= CAP_EXTENDED_SECURITY;
			pSMB->req.Capabilities |= cpu_to_le32(capabilities);
			if (phase == NtLmNegotiate) {
				setup_ntlmssp_neg_req(pSMB, ses);
				iov[1].iov_len = sizeof(NEGOTIATE_MESSAGE);
				iov[1].iov_base = &pSMB->req.SecurityBlob[0];
			} else if (phase == NtLmAuthenticate) {
				/* 5 is an empirical value, large enought to
				 * hold authenticate message, max 10 of
				 * av paris, doamin,user,workstation mames,
				 * flags etc..
				 */
				ntlmsspblob = kmalloc(
					5*sizeof(struct _AUTHENTICATE_MESSAGE),
					GFP_KERNEL);
				if (!ntlmsspblob) {
					cERROR(1, "Can't allocate NTLMSSP");
					rc = -ENOMEM;
					goto ssetup_exit;
				}

				rc = build_ntlmssp_auth_blob(ntlmsspblob,
							&blob_len, ses, nls_cp);
				if (rc)
					goto ssetup_exit;
				iov[1].iov_len = blob_len;
				iov[1].iov_base = ntlmsspblob;
				pSMB->req.SecurityBlobLength =
					cpu_to_le16(blob_len);
				/* Make sure that we tell the server that we
				   are using the uid that it just gave us back
				   on the response (challenge) */
				smb_buf->Uid = ses->Suid;
			} else {
				cERROR(1, "invalid phase %d", phase);
				rc = -ENOSYS;
				goto ssetup_exit;
			}
			/* unicode strings must be word aligned */
			if ((iov[0].iov_len + iov[1].iov_len) % 2) {
				*bcc_ptr = 0;
				bcc_ptr++;
			}
			unicode_oslm_strings(&bcc_ptr, nls_cp);
		} else {
			cERROR(1, "secType %d not supported!", type);
			rc = -ENOSYS;
			goto ssetup_exit;
		}
#else
		cERROR(1, "secType %d not supported!", type);
		rc = -ENOSYS;
		goto ssetup_exit;
#endif
	}

	iov[2].iov_base = str_area;
	iov[2].iov_len = (long) bcc_ptr - (long) str_area;

	count = iov[1].iov_len + iov[2].iov_len;
	smb_buf->smb_buf_length += count;

	BCC_LE(smb_buf) = cpu_to_le16(count);

	rc = SendReceive2(xid, ses, iov, 3 /* num_iovecs */, &resp_buf_type,
			  CIFS_STD_OP /* not long */ | CIFS_LOG_ERROR);
	/* SMB request buf freed in SendReceive2 */

	pSMB = (SESSION_SETUP_ANDX *)iov[0].iov_base;
	smb_buf = (struct smb_hdr *)iov[0].iov_base;

	if ((type == RawNTLMSSP) && (smb_buf->Status.CifsError ==
			cpu_to_le32(NT_STATUS_MORE_PROCESSING_REQUIRED))) {
		if (phase != NtLmNegotiate) {
			cERROR(1, "Unexpected more processing error");
			goto ssetup_exit;
		}
		/* NTLMSSP Negotiate sent now processing challenge (response) */
		phase = NtLmChallenge; /* process ntlmssp challenge */
		rc = 0; /* MORE_PROC rc is not an error here, but expected */
	}
	if (rc)
		goto ssetup_exit;

	if ((smb_buf->WordCount != 3) && (smb_buf->WordCount != 4)) {
		rc = -EIO;
		cERROR(1, "bad word count %d", smb_buf->WordCount);
		goto ssetup_exit;
	}
	action = le16_to_cpu(pSMB->resp.Action);
	if (action & GUEST_LOGIN)
		cFYI(1, "Guest login"); /* BB mark SesInfo struct? */
	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cFYI(1, "UID = %d ", ses->Suid);
	/* response can have either 3 or 4 word count - Samba sends 3 */
	/* and lanman response is 3 */
	bytes_remaining = BCC(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	if (smb_buf->WordCount == 4) {
		blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
		if (blob_len > bytes_remaining) {
			cERROR(1, "bad security blob length %d", blob_len);
			rc = -EINVAL;
			goto ssetup_exit;
		}
		if (phase == NtLmChallenge) {
			rc = decode_ntlmssp_challenge(bcc_ptr, blob_len, ses);
			/* now goto beginning for ntlmssp authenticate phase */
			if (rc)
				goto ssetup_exit;
		}
		bcc_ptr += blob_len;
		bytes_remaining -= blob_len;
	}

	/* BB check if Unicode and decode strings */
	if (smb_buf->Flags2 & SMBFLG2_UNICODE) {
		/* unicode string area must be word-aligned */
		if (((unsigned long) bcc_ptr - (unsigned long) smb_buf) % 2) {
			++bcc_ptr;
			--bytes_remaining;
		}
		decode_unicode_ssetup(&bcc_ptr, bytes_remaining, ses, nls_cp);
	} else {
		rc = decode_ascii_ssetup(&bcc_ptr, bytes_remaining,
					 ses, nls_cp);
	}

ssetup_exit:
	if (spnego_key) {
		key_revoke(spnego_key);
		key_put(spnego_key);
	}
	kfree(str_area);
	kfree(ntlmsspblob);
	ntlmsspblob = NULL;
	if (resp_buf_type == CIFS_SMALL_BUFFER) {
		cFYI(1, "ssetup freeing small buf %p", iov[0].iov_base);
		cifs_small_buf_release(iov[0].iov_base);
	} else if (resp_buf_type == CIFS_LARGE_BUFFER)
		cifs_buf_release(iov[0].iov_base);

	/* if ntlmssp, and negotiate succeeded, proceed to authenticate phase */
	if ((phase == NtLmChallenge) && (rc == 0))
		goto ssetup_ntlmssp_authenticate;

	return rc;
}
