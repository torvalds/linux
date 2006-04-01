/*
 *   fs/cifs/ntlmssp.h
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

#ifdef CONFIG_CIFS_EXPERIMENTAL
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

	/* BB check whether to init vcnum BB */
	return capabilities;
}
int 
CIFS_SessSetup(unsigned int xid, struct cifsSesInfo *ses, const int type,
		  int * pNTLMv2_flg, const struct nls_table *nls_cp)
{
	int rc = 0;
	int wct;
	struct smb_hdr *smb_buffer;
	char *bcc_ptr;
	SESSION_SETUP_ANDX *pSMB;
	__u32 capabilities;

	if(ses == NULL)
		return -EINVAL;

	cFYI(1,("SStp type: %d",type));
	if(type < CIFS_NTLM) {
#ifndef CONFIG_CIFS_WEAK_PW_HASH
		/* LANMAN and plaintext are less secure and off by default.
		So we make this explicitly be turned on in kconfig (in the
		build) and turned on at runtime (changed from the default)
		in proc/fs/cifs or via mount parm.  Unfortunately this is
		needed for old Win (e.g. Win95), some obscure NAS and OS/2 */
		return -EOPNOTSUPP;
#endif
		wct = 10; /* lanman 2 style sessionsetup */
	} else if(type < CIFS_NTLMSSP_NEG)
		wct = 13; /* old style NTLM sessionsetup */
	else /* same size for negotiate or auth, NTLMSSP or extended security */
		wct = 12;

	rc = small_smb_init_no_tc(SMB_COM_SESSION_SETUP_ANDX, wct, ses,
			    (void **)&smb_buffer);
	if(rc)
		return rc;

	pSMB = (SESSION_SETUP_ANDX *)smb_buffer;

	capabilities = cifs_ssetup_hdr(ses, pSMB);
	bcc_ptr = pByteArea(smb_buffer);
	if(type > CIFS_NTLM) {
		pSMB->req.hdr.Flags2 |= SMBFLG2_EXT_SEC;
		capabilities |= CAP_EXTENDED_SECURITY;
		pSMB->req.Capabilities = cpu_to_le32(capabilities);
		/* BB set password lengths */
	} else if(type < CIFS_NTLM) /* lanman */ {
		/* no capabilities flags in old lanman negotiation */
		/* pSMB->old_req.PasswordLength = */ /* BB fixme BB */
	} else /* type CIFS_NTLM */ {
		pSMB->req_no_secext.Capabilities = cpu_to_le32(capabilities);
		pSMB->req_no_secext.CaseInsensitivePasswordLength =
			cpu_to_le16(CIFS_SESSION_KEY_SIZE);
		pSMB->req_no_secext.CaseSensitivePasswordLength =
			cpu_to_le16(CIFS_SESSION_KEY_SIZE);
	}


/*	rc = SendReceive2(xid, ses, iov, num_iovecs, &resp_buf_type, 0); */
	/* SMB request buf freed in SendReceive2 */

	return rc;
}
#endif /* CONFIG_CIFS_EXPERIMENTAL */
