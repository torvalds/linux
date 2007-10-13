/*
 *   fs/cifs/cifsencrypt.c
 *
 *   Copyright (C) International Business Machines  Corp., 2005,2006
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

#include <linux/fs.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "md5.h"
#include "cifs_unicode.h"
#include "cifsproto.h"
#include <linux/ctype.h>
#include <linux/random.h>

/* Calculate and return the CIFS signature based on the mac key and SMB PDU */
/* the 16 byte signature must be allocated by the caller  */
/* Note we only use the 1st eight bytes */
/* Note that the smb header signature field on input contains the
	sequence number before this function is called */

extern void mdfour(unsigned char *out, unsigned char *in, int n);
extern void E_md4hash(const unsigned char *passwd, unsigned char *p16);
extern void SMBencrypt(unsigned char *passwd, unsigned char *c8,
		       unsigned char *p24);

static int cifs_calculate_signature(const struct smb_hdr *cifs_pdu,
				    const struct mac_key *key, char *signature)
{
	struct	MD5Context context;

	if ((cifs_pdu == NULL) || (signature == NULL) || (key == NULL))
		return -EINVAL;

	MD5Init(&context);
	MD5Update(&context, (char *)&key->data, key->len);
	MD5Update(&context, cifs_pdu->Protocol, cifs_pdu->smb_buf_length);

	MD5Final(signature, &context);
	return 0;
}

int cifs_sign_smb(struct smb_hdr *cifs_pdu, struct TCP_Server_Info *server,
		  __u32 *pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];

	if ((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	if ((cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) == 0)
		return rc;

	spin_lock(&GlobalMid_Lock);
	cifs_pdu->Signature.Sequence.SequenceNumber =
			cpu_to_le32(server->sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	*pexpected_response_sequence_number = server->sequence_number++;
	server->sequence_number++;
	spin_unlock(&GlobalMid_Lock);

	rc = cifs_calculate_signature(cifs_pdu, &server->mac_signing_key,
				      smb_signature);
	if (rc)
		memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

static int cifs_calc_signature2(const struct kvec *iov, int n_vec,
				const struct mac_key *key, char *signature)
{
	struct  MD5Context context;
	int i;

	if ((iov == NULL) || (signature == NULL) || (key == NULL))
		return -EINVAL;

	MD5Init(&context);
	MD5Update(&context, (char *)&key->data, key->len);
	for (i = 0; i < n_vec; i++) {
		if (iov[i].iov_base == NULL) {
			cERROR(1, ("null iovec entry"));
			return -EIO;
		} else if (iov[i].iov_len == 0)
			break; /* bail out if we are sent nothing to sign */
		/* The first entry includes a length field (which does not get
		   signed that occupies the first 4 bytes before the header */
		if (i == 0) {
			if (iov[0].iov_len <= 8 ) /* cmd field at offset 9 */
				break; /* nothing to sign or corrupt header */
			MD5Update(&context, iov[0].iov_base+4,
				  iov[0].iov_len-4);
		} else
			MD5Update(&context, iov[i].iov_base, iov[i].iov_len);
	}

	MD5Final(signature, &context);

	return 0;
}


int cifs_sign_smb2(struct kvec *iov, int n_vec, struct TCP_Server_Info *server,
		   __u32 * pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];
	struct smb_hdr *cifs_pdu = iov[0].iov_base;

	if ((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	if ((cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) == 0)
		return rc;

	spin_lock(&GlobalMid_Lock);
	cifs_pdu->Signature.Sequence.SequenceNumber =
				cpu_to_le32(server->sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	*pexpected_response_sequence_number = server->sequence_number++;
	server->sequence_number++;
	spin_unlock(&GlobalMid_Lock);

	rc = cifs_calc_signature2(iov, n_vec, &server->mac_signing_key,
				      smb_signature);
	if (rc)
		memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

int cifs_verify_signature(struct smb_hdr *cifs_pdu,
			  const struct mac_key *mac_key,
			  __u32 expected_sequence_number)
{
	unsigned int rc;
	char server_response_sig[8];
	char what_we_think_sig_should_be[20];

	if ((cifs_pdu == NULL) || (mac_key == NULL))
		return -EINVAL;

	if (cifs_pdu->Command == SMB_COM_NEGOTIATE)
		return 0;

	if (cifs_pdu->Command == SMB_COM_LOCKING_ANDX) {
		struct smb_com_lock_req *pSMB =
			(struct smb_com_lock_req *)cifs_pdu;
	    if (pSMB->LockType & LOCKING_ANDX_OPLOCK_RELEASE)
			return 0;
	}

	/* BB what if signatures are supposed to be on for session but
	   server does not send one? BB */

	/* Do not need to verify session setups with signature "BSRSPYL "  */
	if (memcmp(cifs_pdu->Signature.SecuritySignature, "BSRSPYL ", 8) == 0)
		cFYI(1, ("dummy signature received for smb command 0x%x",
			cifs_pdu->Command));

	/* save off the origiginal signature so we can modify the smb and check
		its signature against what the server sent */
	memcpy(server_response_sig, cifs_pdu->Signature.SecuritySignature, 8);

	cifs_pdu->Signature.Sequence.SequenceNumber =
					cpu_to_le32(expected_sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	rc = cifs_calculate_signature(cifs_pdu, mac_key,
		what_we_think_sig_should_be);

	if (rc)
		return rc;

/*	cifs_dump_mem("what we think it should be: ",
		      what_we_think_sig_should_be, 16); */

	if (memcmp(server_response_sig, what_we_think_sig_should_be, 8))
		return -EACCES;
	else
		return 0;

}

/* We fill in key by putting in 40 byte array which was allocated by caller */
int cifs_calculate_mac_key(struct mac_key *key, const char *rn,
			   const char *password)
{
	char temp_key[16];
	if ((key == NULL) || (rn == NULL))
		return -EINVAL;

	E_md4hash(password, temp_key);
	mdfour(key->data.ntlm, temp_key, 16);
	memcpy(key->data.ntlm+16, rn, CIFS_SESS_KEY_SIZE);
	key->len = 40;
	return 0;
}

int CalcNTLMv2_partial_mac_key(struct cifsSesInfo *ses,
			       const struct nls_table *nls_info)
{
	char temp_hash[16];
	struct HMACMD5Context ctx;
	char *ucase_buf;
	__le16 *unicode_buf;
	unsigned int i, user_name_len, dom_name_len;

	if (ses == NULL)
		return -EINVAL;

	E_md4hash(ses->password, temp_hash);

	hmac_md5_init_limK_to_64(temp_hash, 16, &ctx);
	user_name_len = strlen(ses->userName);
	if (user_name_len > MAX_USERNAME_SIZE)
		return -EINVAL;
	if (ses->domainName == NULL)
		return -EINVAL; /* BB should we use CIFS_LINUX_DOM */
	dom_name_len = strlen(ses->domainName);
	if (dom_name_len > MAX_USERNAME_SIZE)
		return -EINVAL;

	ucase_buf = kmalloc((MAX_USERNAME_SIZE+1), GFP_KERNEL);
	if (ucase_buf == NULL)
		return -ENOMEM;
	unicode_buf = kmalloc((MAX_USERNAME_SIZE+1)*4, GFP_KERNEL);
	if (unicode_buf == NULL) {
		kfree(ucase_buf);
		return -ENOMEM;
	}

	for (i = 0; i < user_name_len; i++)
		ucase_buf[i] = nls_info->charset2upper[(int)ses->userName[i]];
	ucase_buf[i] = 0;
	user_name_len = cifs_strtoUCS(unicode_buf, ucase_buf,
				      MAX_USERNAME_SIZE*2, nls_info);
	unicode_buf[user_name_len] = 0;
	user_name_len++;

	for (i = 0; i < dom_name_len; i++)
		ucase_buf[i] = nls_info->charset2upper[(int)ses->domainName[i]];
	ucase_buf[i] = 0;
	dom_name_len = cifs_strtoUCS(unicode_buf+user_name_len, ucase_buf,
				     MAX_USERNAME_SIZE*2, nls_info);

	unicode_buf[user_name_len + dom_name_len] = 0;
	hmac_md5_update((const unsigned char *) unicode_buf,
		(user_name_len+dom_name_len)*2, &ctx);

	hmac_md5_final(ses->server->ntlmv2_hash, &ctx);
	kfree(ucase_buf);
	kfree(unicode_buf);
	return 0;
}

#ifdef CONFIG_CIFS_WEAK_PW_HASH
void calc_lanman_hash(struct cifsSesInfo *ses, char *lnm_session_key)
{
	int i;
	char password_with_pad[CIFS_ENCPWD_SIZE];

	if (ses->server == NULL)
		return;

	memset(password_with_pad, 0, CIFS_ENCPWD_SIZE);
	if (ses->password)
		strncpy(password_with_pad, ses->password, CIFS_ENCPWD_SIZE);

	if ((ses->server->secMode & SECMODE_PW_ENCRYPT) == 0)
		if (extended_security & CIFSSEC_MAY_PLNTXT) {
			memcpy(lnm_session_key, password_with_pad,
				CIFS_ENCPWD_SIZE);
			return;
		}

	/* calculate old style session key */
	/* calling toupper is less broken than repeatedly
	calling nls_toupper would be since that will never
	work for UTF8, but neither handles multibyte code pages
	but the only alternative would be converting to UCS-16 (Unicode)
	(using a routine something like UniStrupr) then
	uppercasing and then converting back from Unicode - which
	would only worth doing it if we knew it were utf8. Basically
	utf8 and other multibyte codepages each need their own strupper
	function since a byte at a time will ont work. */

	for (i = 0; i < CIFS_ENCPWD_SIZE; i++) {
		password_with_pad[i] = toupper(password_with_pad[i]);
	}

	SMBencrypt(password_with_pad, ses->server->cryptKey, lnm_session_key);
	/* clear password before we return/free memory */
	memset(password_with_pad, 0, CIFS_ENCPWD_SIZE);
}
#endif /* CIFS_WEAK_PW_HASH */

static int calc_ntlmv2_hash(struct cifsSesInfo *ses,
			    const struct nls_table *nls_cp)
{
	int rc = 0;
	int len;
	char nt_hash[16];
	struct HMACMD5Context *pctxt;
	wchar_t *user;
	wchar_t *domain;

	pctxt = kmalloc(sizeof(struct HMACMD5Context), GFP_KERNEL);

	if (pctxt == NULL)
		return -ENOMEM;

	/* calculate md4 hash of password */
	E_md4hash(ses->password, nt_hash);

	/* convert Domainname to unicode and uppercase */
	hmac_md5_init_limK_to_64(nt_hash, 16, pctxt);

	/* convert ses->userName to unicode and uppercase */
	len = strlen(ses->userName);
	user = kmalloc(2 + (len * 2), GFP_KERNEL);
	if (user == NULL)
		goto calc_exit_2;
	len = cifs_strtoUCS(user, ses->userName, len, nls_cp);
	UniStrupr(user);
	hmac_md5_update((char *)user, 2*len, pctxt);

	/* convert ses->domainName to unicode and uppercase */
	if (ses->domainName) {
		len = strlen(ses->domainName);

		domain = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (domain == NULL)
			goto calc_exit_1;
		len = cifs_strtoUCS(domain, ses->domainName, len, nls_cp);
		/* the following line was removed since it didn't work well
		   with lower cased domain name that passed as an option.
		   Maybe converting the domain name earlier makes sense */
		/* UniStrupr(domain); */

		hmac_md5_update((char *)domain, 2*len, pctxt);

		kfree(domain);
	}
calc_exit_1:
	kfree(user);
calc_exit_2:
	/* BB FIXME what about bytes 24 through 40 of the signing key?
	   compare with the NTLM example */
	hmac_md5_final(ses->server->ntlmv2_hash, pctxt);

	return rc;
}

void setup_ntlmv2_rsp(struct cifsSesInfo *ses, char *resp_buf,
		      const struct nls_table *nls_cp)
{
	int rc;
	struct ntlmv2_resp *buf = (struct ntlmv2_resp *)resp_buf;
	struct HMACMD5Context context;

	buf->blob_signature = cpu_to_le32(0x00000101);
	buf->reserved = 0;
	buf->time = cpu_to_le64(cifs_UnixTimeToNT(CURRENT_TIME));
	get_random_bytes(&buf->client_chal, sizeof(buf->client_chal));
	buf->reserved2 = 0;
	buf->names[0].type = cpu_to_le16(NTLMSSP_DOMAIN_TYPE);
	buf->names[0].length = 0;
	buf->names[1].type = 0;
	buf->names[1].length = 0;

	/* calculate buf->ntlmv2_hash */
	rc = calc_ntlmv2_hash(ses, nls_cp);
	if (rc)
		cERROR(1, ("could not get v2 hash rc %d", rc));
	CalcNTLMv2_response(ses, resp_buf);

	/* now calculate the MAC key for NTLMv2 */
	hmac_md5_init_limK_to_64(ses->server->ntlmv2_hash, 16, &context);
	hmac_md5_update(resp_buf, 16, &context);
	hmac_md5_final(ses->server->mac_signing_key.data.ntlmv2.key, &context);

	memcpy(&ses->server->mac_signing_key.data.ntlmv2.resp, resp_buf,
	       sizeof(struct ntlmv2_resp));
	ses->server->mac_signing_key.len = 16 + sizeof(struct ntlmv2_resp);
}

void CalcNTLMv2_response(const struct cifsSesInfo *ses,
			 char *v2_session_response)
{
	struct HMACMD5Context context;
	/* rest of v2 struct already generated */
	memcpy(v2_session_response + 8, ses->server->cryptKey, 8);
	hmac_md5_init_limK_to_64(ses->server->ntlmv2_hash, 16, &context);

	hmac_md5_update(v2_session_response+8,
			sizeof(struct ntlmv2_resp) - 8, &context);

	hmac_md5_final(v2_session_response, &context);
/*	cifs_dump_mem("v2_sess_rsp: ", v2_session_response, 32); */
}
