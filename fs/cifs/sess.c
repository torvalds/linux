/*
 *   fs/cifs/sess.c
 *
 *   SMB/CIFS session setup handling routines
 *
 *   Copyright (c) International Business Machines  Corp., 2006
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

extern void SMBNTencrypt(unsigned char *passwd, unsigned char *c8,
                         unsigned char *p24);

static __u32 cifs_ssetup_hdr(struct cifsSesInfo *ses, SESSION_SETUP_ANDX *pSMB)
{
	__u32 capabilities = 0;

	/* init fields common to all four types of SessSetup */
	/* note that header is initialized to zero in header_assemble */
	pSMB->req.AndXCommand = 0xFF;
	pSMB->req.MaxBufferSize = cpu_to_le16(ses->server->maxBuf);
	pSMB->req.MaxMpxCount = cpu_to_le16(ses->server->maxReq);

	/* Now no need to set SMBFLG_CASELESS or obsolete CANONICAL PATH */

	/* BB verify whether signing required on neg or just on auth frame 
	   (and NTLM case) */

	capabilities = CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS |
			CAP_LARGE_WRITE_X | CAP_LARGE_READ_X;

	if(ses->server->secMode & (SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
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
	if (ses->capabilities & CAP_UNIX) {
		capabilities |= CAP_UNIX;
	}

	/* BB check whether to init vcnum BB */
	return capabilities;
}

static void unicode_ssetup_strings(char ** pbcc_area, struct cifsSesInfo *ses,
			    const struct nls_table * nls_cp)
{
	char * bcc_ptr = *pbcc_area;
	int bytes_ret = 0;

	/* BB FIXME add check that strings total less
	than 335 or will need to send them as arrays */

	/* unicode strings, must be word aligned before the call */
/*	if ((long) bcc_ptr % 2)	{
		*bcc_ptr = 0;
		bcc_ptr++;
	} */
	/* copy user */
	if(ses->userName == NULL) {
		/* null user mount */
		*bcc_ptr = 0;
		*(bcc_ptr+1) = 0;
	} else { /* 300 should be long enough for any conceivable user name */
		bytes_ret = cifs_strtoUCS((__le16 *) bcc_ptr, ses->userName,
					  300, nls_cp);
	}
	bcc_ptr += 2 * bytes_ret;
	bcc_ptr += 2; /* account for null termination */
	/* copy domain */
	if(ses->domainName == NULL) {
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

static void ascii_ssetup_strings(char ** pbcc_area, struct cifsSesInfo *ses,
			  const struct nls_table * nls_cp)
{
	char * bcc_ptr = *pbcc_area;

	/* copy user */
	/* BB what about null user mounts - check that we do this BB */
        /* copy user */
        if(ses->userName == NULL) {
                /* BB what about null user mounts - check that we do this BB */
        } else { /* 300 should be long enough for any conceivable user name */
                strncpy(bcc_ptr, ses->userName, 300);
        }
	/* BB improve check for overflow */
        bcc_ptr += strnlen(ses->userName, 300);
	*bcc_ptr = 0;
        bcc_ptr++; /* account for null termination */

        /* copy domain */
	
        if(ses->domainName != NULL) {
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

static int decode_unicode_ssetup(char ** pbcc_area, int bleft, struct cifsSesInfo *ses,
                            const struct nls_table * nls_cp)
{
	int rc = 0;
	int words_left, len;
	char * data = *pbcc_area;



	cFYI(1,("bleft %d",bleft));


	/* SMB header is unaligned, so cifs servers word align start of
	   Unicode strings */
	data++;
	bleft--; /* Windows servers do not always double null terminate
		    their final Unicode string - in which case we
		    now will not attempt to decode the byte of junk
		    which follows it */
		    
	words_left = bleft / 2;

	/* save off server operating system */
	len = UniStrnlen((wchar_t *) data, words_left);

/* We look for obvious messed up bcc or strings in response so we do not go off
   the end since (at least) WIN2K and Windows XP have a major bug in not null
   terminating last Unicode string in response  */
	if(len >= words_left)
		return rc;

	if(ses->serverOS)
		kfree(ses->serverOS);
	/* UTF-8 string will not grow more than four times as big as UCS-16 */
	ses->serverOS = kzalloc(4 * len, GFP_KERNEL);
	if(ses->serverOS != NULL) {
		cifs_strfromUCS_le(ses->serverOS, (__le16 *)data, len,
				   nls_cp);
	}
	data += 2 * (len + 1);
	words_left -= len + 1;

	/* save off server network operating system */
	len = UniStrnlen((wchar_t *) data, words_left);

	if(len >= words_left)
		return rc;

	if(ses->serverNOS)
		kfree(ses->serverNOS);
	ses->serverNOS = kzalloc(4 * len, GFP_KERNEL); /* BB this is wrong length FIXME BB */
	if(ses->serverNOS != NULL) {
		cifs_strfromUCS_le(ses->serverNOS, (__le16 *)data, len,
				   nls_cp);
		if(strncmp(ses->serverNOS, "NT LAN Manager 4",16) == 0) {
			cFYI(1,("NT4 server"));
			ses->flags |= CIFS_SES_NT4;
		}
	}
	data += 2 * (len + 1);
	words_left -= len + 1;

        /* save off server domain */
        len = UniStrnlen((wchar_t *) data, words_left);

        if(len > words_left)
                return rc;

        if(ses->serverDomain)
                kfree(ses->serverDomain);
        ses->serverDomain = kzalloc(2 * (len + 1), GFP_KERNEL); /* BB FIXME wrong length */
        if(ses->serverDomain != NULL) {
                cifs_strfromUCS_le(ses->serverDomain, (__le16 *)data, len,
                                   nls_cp);
                ses->serverDomain[2*len] = 0;
                ses->serverDomain[(2*len) + 1] = 0;
        }
        data += 2 * (len + 1);
        words_left -= len + 1;
	
	cFYI(1,("words left: %d",words_left));

	return rc;
}

static int decode_ascii_ssetup(char ** pbcc_area, int bleft, struct cifsSesInfo *ses,
                            const struct nls_table * nls_cp)
{
	int rc = 0;
	int len;
	char * bcc_ptr = *pbcc_area;

	cFYI(1,("decode sessetup ascii. bleft %d", bleft));
	
	len = strnlen(bcc_ptr, bleft);
	if(len >= bleft)
		return rc;
	
	if(ses->serverOS)
		kfree(ses->serverOS);

	ses->serverOS = kzalloc(len + 1, GFP_KERNEL);
	if(ses->serverOS)
		strncpy(ses->serverOS, bcc_ptr, len);
	if(strncmp(ses->serverOS, "OS/2",4) == 0) {
			cFYI(1,("OS/2 server"));
			ses->flags |= CIFS_SES_OS2;
	}

	bcc_ptr += len + 1;
	bleft -= len + 1;

	len = strnlen(bcc_ptr, bleft);
	if(len >= bleft)
		return rc;

	if(ses->serverNOS)
		kfree(ses->serverNOS);

	ses->serverNOS = kzalloc(len + 1, GFP_KERNEL);
	if(ses->serverNOS)
		strncpy(ses->serverNOS, bcc_ptr, len);

	bcc_ptr += len + 1;
	bleft -= len + 1;

        len = strnlen(bcc_ptr, bleft);
        if(len > bleft)
                return rc;

	/* No domain field in LANMAN case. Domain is
	   returned by old servers in the SMB negprot response */
	/* BB For newer servers which do not support Unicode,
	   but thus do return domain here we could add parsing
	   for it later, but it is not very important */
	cFYI(1,("ascii: bytes left %d",bleft));

	return rc;
}

int 
CIFS_SessSetup(unsigned int xid, struct cifsSesInfo *ses, int first_time,
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
	int resp_buf_type = 0;
	struct kvec iov[2];
	enum securityEnum type;
	__u16 action;
	int bytes_remaining;

	if(ses == NULL)
		return -EINVAL;

	type = ses->server->secType;

	cFYI(1,("sess setup type %d",type));
	if(type == LANMAN) {
#ifndef CONFIG_CIFS_WEAK_PW_HASH
		/* LANMAN and plaintext are less secure and off by default.
		So we make this explicitly be turned on in kconfig (in the
		build) and turned on at runtime (changed from the default)
		in proc/fs/cifs or via mount parm.  Unfortunately this is
		needed for old Win (e.g. Win95), some obscure NAS and OS/2 */
		return -EOPNOTSUPP;
#endif
		wct = 10; /* lanman 2 style sessionsetup */
	} else if((type == NTLM) || (type == NTLMv2)) { 
		/* For NTLMv2 failures eventually may need to retry NTLM */
		wct = 13; /* old style NTLM sessionsetup */
	} else /* same size for negotiate or auth, NTLMSSP or extended security */
		wct = 12;

	rc = small_smb_init_no_tc(SMB_COM_SESSION_SETUP_ANDX, wct, ses,
			    (void **)&smb_buf);
	if(rc)
		return rc;

	pSMB = (SESSION_SETUP_ANDX *)smb_buf;

	capabilities = cifs_ssetup_hdr(ses, pSMB);

	/* we will send the SMB in two pieces,
	a fixed length beginning part, and a
	second part which will include the strings
	and rest of bcc area, in order to avoid having
	to do a large buffer 17K allocation */
        iov[0].iov_base = (char *)pSMB;
        iov[0].iov_len = smb_buf->smb_buf_length + 4;

	/* 2000 big enough to fit max user, domain, NOS name etc. */
	str_area = kmalloc(2000, GFP_KERNEL);
	bcc_ptr = str_area;

	ses->flags &= ~CIFS_SES_LANMAN;

	if(type == LANMAN) {
#ifdef CONFIG_CIFS_WEAK_PW_HASH
		char lnm_session_key[CIFS_SESS_KEY_SIZE];

		/* no capabilities flags in old lanman negotiation */

		pSMB->old_req.PasswordLength = cpu_to_le16(CIFS_SESS_KEY_SIZE); 
		/* BB calculate hash with password */
		/* and copy into bcc */

		calc_lanman_hash(ses, lnm_session_key);
		ses->flags |= CIFS_SES_LANMAN; 
/* #ifdef CONFIG_CIFS_DEBUG2
		cifs_dump_mem("cryptkey: ",ses->server->cryptKey,
			CIFS_SESS_KEY_SIZE);
#endif */
		memcpy(bcc_ptr, (char *)lnm_session_key, CIFS_SESS_KEY_SIZE);
		bcc_ptr += CIFS_SESS_KEY_SIZE;

		/* can not sign if LANMAN negotiated so no need
		to calculate signing key? but what if server
		changed to do higher than lanman dialect and
		we reconnected would we ever calc signing_key? */

		cFYI(1,("Negotiating LANMAN setting up strings"));
		/* Unicode not allowed for LANMAN dialects */
		ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
#endif    
	} else if (type == NTLM) {
		char ntlm_session_key[CIFS_SESS_KEY_SIZE];

		pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);
		pSMB->req_no_secext.CaseInsensitivePasswordLength =
			cpu_to_le16(CIFS_SESS_KEY_SIZE);
		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(CIFS_SESS_KEY_SIZE);
	
		/* calculate session key */
		SMBNTencrypt(ses->password, ses->server->cryptKey,
			     ntlm_session_key);

		if(first_time) /* should this be moved into common code 
				  with similar ntlmv2 path? */
			cifs_calculate_mac_key(ses->server->mac_signing_key,
				ntlm_session_key, ses->password);
		/* copy session key */

		memcpy(bcc_ptr, (char *)ntlm_session_key,CIFS_SESS_KEY_SIZE);
		bcc_ptr += CIFS_SESS_KEY_SIZE;
		memcpy(bcc_ptr, (char *)ntlm_session_key,CIFS_SESS_KEY_SIZE);
		bcc_ptr += CIFS_SESS_KEY_SIZE;
		if(ses->capabilities & CAP_UNICODE) {
			/* unicode strings must be word aligned */
			if (iov[0].iov_len % 2) {
				*bcc_ptr = 0;
				bcc_ptr++;		
			}	
			unicode_ssetup_strings(&bcc_ptr, ses, nls_cp);
		} else
			ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
	} else if (type == NTLMv2) {
		char * v2_sess_key = 
			kmalloc(sizeof(struct ntlmv2_resp), GFP_KERNEL);

		/* BB FIXME change all users of v2_sess_key to
		   struct ntlmv2_resp */

		if(v2_sess_key == NULL) {
			cifs_small_buf_release(smb_buf);
			return -ENOMEM;
		}

		pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);

		/* LM2 password would be here if we supported it */
		pSMB->req_no_secext.CaseInsensitivePasswordLength = 0;
		/*	cpu_to_le16(LM2_SESS_KEY_SIZE); */

		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(sizeof(struct ntlmv2_resp));

		/* calculate session key */
		setup_ntlmv2_rsp(ses, v2_sess_key, nls_cp);
		if(first_time) /* should this be moved into common code
			          with similar ntlmv2 path? */
		/*   cifs_calculate_ntlmv2_mac_key(ses->server->mac_signing_key,
				response BB FIXME, v2_sess_key); */

		/* copy session key */

	/*	memcpy(bcc_ptr, (char *)ntlm_session_key,LM2_SESS_KEY_SIZE);
		bcc_ptr += LM2_SESS_KEY_SIZE; */
		memcpy(bcc_ptr, (char *)v2_sess_key, sizeof(struct ntlmv2_resp));
		bcc_ptr += sizeof(struct ntlmv2_resp);
		kfree(v2_sess_key);
		if(ses->capabilities & CAP_UNICODE) {
			if(iov[0].iov_len % 2) {
				*bcc_ptr = 0;
			}	bcc_ptr++;
			unicode_ssetup_strings(&bcc_ptr, ses, nls_cp);
		} else
			ascii_ssetup_strings(&bcc_ptr, ses, nls_cp);
	} else /* NTLMSSP or SPNEGO */ {
		pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
		capabilities |= CAP_EXTENDED_SECURITY;
		pSMB->req.Capabilities = cpu_to_le32(capabilities);
		/* BB set password lengths */
	}

	count = (long) bcc_ptr - (long) str_area;
	smb_buf->smb_buf_length += count;

	BCC_LE(smb_buf) = cpu_to_le16(count);

	iov[1].iov_base = str_area;
	iov[1].iov_len = count; 
	rc = SendReceive2(xid, ses, iov, 2 /* num_iovecs */, &resp_buf_type, 0);
	/* SMB request buf freed in SendReceive2 */

	cFYI(1,("ssetup rc from sendrecv2 is %d",rc));
	if(rc)
		goto ssetup_exit;

	pSMB = (SESSION_SETUP_ANDX *)iov[0].iov_base;
	smb_buf = (struct smb_hdr *)iov[0].iov_base;

	if((smb_buf->WordCount != 3) && (smb_buf->WordCount != 4)) {
		rc = -EIO;
		cERROR(1,("bad word count %d", smb_buf->WordCount));
		goto ssetup_exit;
	}
	action = le16_to_cpu(pSMB->resp.Action);
	if (action & GUEST_LOGIN)
		cFYI(1, ("Guest login")); /* BB mark SesInfo struct? */
	ses->Suid = smb_buf->Uid;   /* UID left in wire format (le) */
	cFYI(1, ("UID = %d ", ses->Suid));
	/* response can have either 3 or 4 word count - Samba sends 3 */
	/* and lanman response is 3 */
	bytes_remaining = BCC(smb_buf);
	bcc_ptr = pByteArea(smb_buf);

	if(smb_buf->WordCount == 4) {
		__u16 blob_len;
		blob_len = le16_to_cpu(pSMB->resp.SecurityBlobLength);
		bcc_ptr += blob_len;
		if(blob_len > bytes_remaining) {
			cERROR(1,("bad security blob length %d", blob_len));
			rc = -EINVAL;
			goto ssetup_exit;
		}
		bytes_remaining -= blob_len;
	}	

	/* BB check if Unicode and decode strings */
	if(smb_buf->Flags2 & SMBFLG2_UNICODE)
		rc = decode_unicode_ssetup(&bcc_ptr, bytes_remaining,
						   ses, nls_cp);
	else
		rc = decode_ascii_ssetup(&bcc_ptr, bytes_remaining, ses,nls_cp);
	
ssetup_exit:
	kfree(str_area);
	if(resp_buf_type == CIFS_SMALL_BUFFER) {
		cFYI(1,("ssetup freeing small buf %p", iov[0].iov_base));
		cifs_small_buf_release(iov[0].iov_base);
	} else if(resp_buf_type == CIFS_LARGE_BUFFER)
		cifs_buf_release(iov[0].iov_base);

	return rc;
}
