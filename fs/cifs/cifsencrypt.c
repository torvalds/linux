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

/* Calculate and return the CIFS signature based on the mac key and the smb pdu */
/* the 16 byte signature must be allocated by the caller  */
/* Note we only use the 1st eight bytes */
/* Note that the smb header signature field on input contains the  
	sequence number before this function is called */

extern void mdfour(unsigned char *out, unsigned char *in, int n);
extern void E_md4hash(const unsigned char *passwd, unsigned char *p16);
	
static int cifs_calculate_signature(const struct smb_hdr * cifs_pdu, 
				    const char * key, char * signature)
{
	struct	MD5Context context;

	if((cifs_pdu == NULL) || (signature == NULL))
		return -EINVAL;

	MD5Init(&context);
	MD5Update(&context,key,CIFS_SESSION_KEY_SIZE+16);
	MD5Update(&context,cifs_pdu->Protocol,cifs_pdu->smb_buf_length);
	MD5Final(signature,&context);
	return 0;
}

int cifs_sign_smb(struct smb_hdr * cifs_pdu, struct TCP_Server_Info * server,
	__u32 * pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];

	if((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	if((cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) == 0) 
		return rc;

	spin_lock(&GlobalMid_Lock);
	cifs_pdu->Signature.Sequence.SequenceNumber = cpu_to_le32(server->sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;
	
	*pexpected_response_sequence_number = server->sequence_number++;
	server->sequence_number++;
	spin_unlock(&GlobalMid_Lock);

	rc = cifs_calculate_signature(cifs_pdu, server->mac_signing_key,smb_signature);
	if(rc)
		memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

static int cifs_calc_signature2(const struct kvec * iov, int n_vec,
				const char * key, char * signature)
{
	struct  MD5Context context;
	int i;

	if((iov == NULL) || (signature == NULL))
		return -EINVAL;

	MD5Init(&context);
	MD5Update(&context,key,CIFS_SESSION_KEY_SIZE+16);
	for(i=0;i<n_vec;i++) {
		if(iov[i].iov_base == NULL) {
			cERROR(1,("null iovec entry"));
			return -EIO;
		} else if(iov[i].iov_len == 0)
			break; /* bail out if we are sent nothing to sign */
		/* The first entry includes a length field (which does not get 
		   signed that occupies the first 4 bytes before the header */
		if(i==0) {
			if (iov[0].iov_len <= 8 ) /* cmd field at offset 9 */
				break; /* nothing to sign or corrupt header */
			MD5Update(&context,iov[0].iov_base+4, iov[0].iov_len-4);
		} else
			MD5Update(&context,iov[i].iov_base, iov[i].iov_len);
	}

	MD5Final(signature,&context);

	return 0;
}


int cifs_sign_smb2(struct kvec * iov, int n_vec, struct TCP_Server_Info *server,
		   __u32 * pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];
	struct smb_hdr * cifs_pdu = iov[0].iov_base;

	if((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	if((cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) == 0)
		return rc;

        spin_lock(&GlobalMid_Lock);
        cifs_pdu->Signature.Sequence.SequenceNumber = 
				cpu_to_le32(server->sequence_number);
        cifs_pdu->Signature.Sequence.Reserved = 0;

        *pexpected_response_sequence_number = server->sequence_number++;
        server->sequence_number++;
        spin_unlock(&GlobalMid_Lock);

        rc = cifs_calc_signature2(iov, n_vec, server->mac_signing_key,
				      smb_signature);
        if(rc)
                memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
        else
                memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

        return rc;

}

int cifs_verify_signature(struct smb_hdr * cifs_pdu, const char * mac_key,
	__u32 expected_sequence_number)
{
	unsigned int rc;
	char server_response_sig[8];
	char what_we_think_sig_should_be[20];

	if((cifs_pdu == NULL) || (mac_key == NULL))
		return -EINVAL;

	if (cifs_pdu->Command == SMB_COM_NEGOTIATE)
		return 0;

	if (cifs_pdu->Command == SMB_COM_LOCKING_ANDX) {
		struct smb_com_lock_req * pSMB = (struct smb_com_lock_req *)cifs_pdu;
	    if(pSMB->LockType & LOCKING_ANDX_OPLOCK_RELEASE)
			return 0;
	}

	/* BB what if signatures are supposed to be on for session but server does not
		send one? BB */
	
	/* Do not need to verify session setups with signature "BSRSPYL "  */
	if(memcmp(cifs_pdu->Signature.SecuritySignature,"BSRSPYL ",8)==0)
		cFYI(1,("dummy signature received for smb command 0x%x",cifs_pdu->Command));

	/* save off the origiginal signature so we can modify the smb and check
		its signature against what the server sent */
	memcpy(server_response_sig,cifs_pdu->Signature.SecuritySignature,8);

	cifs_pdu->Signature.Sequence.SequenceNumber = cpu_to_le32(expected_sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	rc = cifs_calculate_signature(cifs_pdu, mac_key,
		what_we_think_sig_should_be);

	if(rc)
		return rc;

	
/*	cifs_dump_mem("what we think it should be: ",what_we_think_sig_should_be,16); */

	if(memcmp(server_response_sig, what_we_think_sig_should_be, 8))
		return -EACCES;
	else
		return 0;

}

/* We fill in key by putting in 40 byte array which was allocated by caller */
int cifs_calculate_mac_key(char * key, const char * rn, const char * password)
{
	char temp_key[16];
	if ((key == NULL) || (rn == NULL))
		return -EINVAL;

	E_md4hash(password, temp_key);
	mdfour(key,temp_key,16);
	memcpy(key+16,rn, CIFS_SESSION_KEY_SIZE);
	return 0;
}

int CalcNTLMv2_partial_mac_key(struct cifsSesInfo * ses, struct nls_table * nls_info)
{
	char temp_hash[16];
	struct HMACMD5Context ctx;
	char * ucase_buf;
	__le16 * unicode_buf;
	unsigned int i,user_name_len,dom_name_len;

	if(ses == NULL)
		return -EINVAL;

	E_md4hash(ses->password, temp_hash);

	hmac_md5_init_limK_to_64(temp_hash, 16, &ctx);
	user_name_len = strlen(ses->userName);
	if(user_name_len > MAX_USERNAME_SIZE)
		return -EINVAL;
	dom_name_len = strlen(ses->domainName);
	if(dom_name_len > MAX_USERNAME_SIZE)
		return -EINVAL;
  
	ucase_buf = kmalloc((MAX_USERNAME_SIZE+1), GFP_KERNEL);
	if(ucase_buf == NULL)
		return -ENOMEM;
	unicode_buf = kmalloc((MAX_USERNAME_SIZE+1)*4, GFP_KERNEL);
	if(unicode_buf == NULL) {
		kfree(ucase_buf);
		return -ENOMEM;
	}
   
	for(i=0;i<user_name_len;i++)
		ucase_buf[i] = nls_info->charset2upper[(int)ses->userName[i]];
	ucase_buf[i] = 0;
	user_name_len = cifs_strtoUCS(unicode_buf, ucase_buf, MAX_USERNAME_SIZE*2, nls_info);
	unicode_buf[user_name_len] = 0;
	user_name_len++;

	for(i=0;i<dom_name_len;i++)
		ucase_buf[i] = nls_info->charset2upper[(int)ses->domainName[i]];
	ucase_buf[i] = 0;
	dom_name_len = cifs_strtoUCS(unicode_buf+user_name_len, ucase_buf, MAX_USERNAME_SIZE*2, nls_info);

	unicode_buf[user_name_len + dom_name_len] = 0;
	hmac_md5_update((const unsigned char *) unicode_buf,
		(user_name_len+dom_name_len)*2,&ctx);

	hmac_md5_final(ses->server->mac_signing_key,&ctx);
	kfree(ucase_buf);
	kfree(unicode_buf);
	return 0;
}
void CalcNTLMv2_response(const struct cifsSesInfo * ses,char * v2_session_response)
{
	struct HMACMD5Context context;
	memcpy(v2_session_response + 8, ses->server->cryptKey,8);
	/* gen_blob(v2_session_response + 16); */
	hmac_md5_init_limK_to_64(ses->server->mac_signing_key, 16, &context);

	hmac_md5_update(ses->server->cryptKey,8,&context);
/*	hmac_md5_update(v2_session_response+16)client thing,8,&context); */ /* BB fix */

	hmac_md5_final(v2_session_response,&context);
	cifs_dump_mem("v2_sess_rsp: ", v2_session_response, 32); /* BB removeme BB */
}
