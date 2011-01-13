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
#include <linux/slab.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "md5.h"
#include "cifs_unicode.h"
#include "cifsproto.h"
#include "ntlmssp.h"
#include <linux/ctype.h>
#include <linux/random.h>

/* Calculate and return the CIFS signature based on the mac key and SMB PDU */
/* the 16 byte signature must be allocated by the caller  */
/* Note we only use the 1st eight bytes */
/* Note that the smb header signature field on input contains the
	sequence number before this function is called */

extern void mdfour(unsigned char *out, unsigned char *in, int n);
extern void E_md4hash(const unsigned char *passwd, unsigned char *p16);
extern void SMBencrypt(unsigned char *passwd, const unsigned char *c8,
		       unsigned char *p24);

static int cifs_calculate_signature(const struct smb_hdr *cifs_pdu,
				struct TCP_Server_Info *server, char *signature)
{
	int rc;

	if (cifs_pdu == NULL || signature == NULL || server == NULL)
		return -EINVAL;

	if (!server->secmech.sdescmd5) {
		cERROR(1, "%s: Can't generate signature\n", __func__);
		return -1;
	}

	rc = crypto_shash_init(&server->secmech.sdescmd5->shash);
	if (rc) {
		cERROR(1, "%s: Oould not init md5\n", __func__);
		return rc;
	}

	crypto_shash_update(&server->secmech.sdescmd5->shash,
		server->session_key.response, server->session_key.len);

	crypto_shash_update(&server->secmech.sdescmd5->shash,
		cifs_pdu->Protocol, cifs_pdu->smb_buf_length);

	rc = crypto_shash_final(&server->secmech.sdescmd5->shash, signature);

	return 0;
}

/* must be called with server->srv_mutex held */
int cifs_sign_smb(struct smb_hdr *cifs_pdu, struct TCP_Server_Info *server,
		  __u32 *pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];

	if ((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	if ((cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) == 0)
		return rc;

	cifs_pdu->Signature.Sequence.SequenceNumber =
			cpu_to_le32(server->sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	*pexpected_response_sequence_number = server->sequence_number++;
	server->sequence_number++;

	rc = cifs_calculate_signature(cifs_pdu, server, smb_signature);
	if (rc)
		memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

static int cifs_calc_signature2(const struct kvec *iov, int n_vec,
				struct TCP_Server_Info *server, char *signature)
{
	int i;
	int rc;

	if (iov == NULL || signature == NULL || server == NULL)
		return -EINVAL;

	if (!server->secmech.sdescmd5) {
		cERROR(1, "%s: Can't generate signature\n", __func__);
		return -1;
	}

	rc = crypto_shash_init(&server->secmech.sdescmd5->shash);
	if (rc) {
		cERROR(1, "%s: Oould not init md5\n", __func__);
		return rc;
	}

	crypto_shash_update(&server->secmech.sdescmd5->shash,
		server->session_key.response, server->session_key.len);

	for (i = 0; i < n_vec; i++) {
		if (iov[i].iov_len == 0)
			continue;
		if (iov[i].iov_base == NULL) {
			cERROR(1, "null iovec entry");
			return -EIO;
		}
		/* The first entry includes a length field (which does not get
		   signed that occupies the first 4 bytes before the header */
		if (i == 0) {
			if (iov[0].iov_len <= 8) /* cmd field at offset 9 */
				break; /* nothing to sign or corrupt header */
			crypto_shash_update(&server->secmech.sdescmd5->shash,
				iov[i].iov_base + 4, iov[i].iov_len - 4);
		} else
			crypto_shash_update(&server->secmech.sdescmd5->shash,
				iov[i].iov_base, iov[i].iov_len);
	}

	rc = crypto_shash_final(&server->secmech.sdescmd5->shash, signature);

	return rc;
}

/* must be called with server->srv_mutex held */
int cifs_sign_smb2(struct kvec *iov, int n_vec, struct TCP_Server_Info *server,
		   __u32 *pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];
	struct smb_hdr *cifs_pdu = iov[0].iov_base;

	if ((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	if ((cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) == 0)
		return rc;

	cifs_pdu->Signature.Sequence.SequenceNumber =
				cpu_to_le32(server->sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	*pexpected_response_sequence_number = server->sequence_number++;
	server->sequence_number++;

	rc = cifs_calc_signature2(iov, n_vec, server, smb_signature);
	if (rc)
		memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

int cifs_verify_signature(struct smb_hdr *cifs_pdu,
			  struct TCP_Server_Info *server,
			  __u32 expected_sequence_number)
{
	unsigned int rc;
	char server_response_sig[8];
	char what_we_think_sig_should_be[20];

	if (cifs_pdu == NULL || server == NULL)
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
		cFYI(1, "dummy signature received for smb command 0x%x",
			cifs_pdu->Command);

	/* save off the origiginal signature so we can modify the smb and check
		its signature against what the server sent */
	memcpy(server_response_sig, cifs_pdu->Signature.SecuritySignature, 8);

	cifs_pdu->Signature.Sequence.SequenceNumber =
					cpu_to_le32(expected_sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	rc = cifs_calculate_signature(cifs_pdu, server,
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

/* first calculate 24 bytes ntlm response and then 16 byte session key */
int setup_ntlm_response(struct cifsSesInfo *ses)
{
	unsigned int temp_len = CIFS_SESS_KEY_SIZE + CIFS_AUTH_RESP_SIZE;
	char temp_key[CIFS_SESS_KEY_SIZE];

	if (!ses)
		return -EINVAL;

	ses->auth_key.response = kmalloc(temp_len, GFP_KERNEL);
	if (!ses->auth_key.response) {
		cERROR(1, "NTLM can't allocate (%u bytes) memory", temp_len);
		return -ENOMEM;
	}
	ses->auth_key.len = temp_len;

	SMBNTencrypt(ses->password, ses->server->cryptkey,
			ses->auth_key.response + CIFS_SESS_KEY_SIZE);

	E_md4hash(ses->password, temp_key);
	mdfour(ses->auth_key.response, temp_key, CIFS_SESS_KEY_SIZE);

	return 0;
}

#ifdef CONFIG_CIFS_WEAK_PW_HASH
void calc_lanman_hash(const char *password, const char *cryptkey, bool encrypt,
			char *lnm_session_key)
{
	int i;
	char password_with_pad[CIFS_ENCPWD_SIZE];

	memset(password_with_pad, 0, CIFS_ENCPWD_SIZE);
	if (password)
		strncpy(password_with_pad, password, CIFS_ENCPWD_SIZE);

	if (!encrypt && global_secflags & CIFSSEC_MAY_PLNTXT) {
		memset(lnm_session_key, 0, CIFS_SESS_KEY_SIZE);
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

	for (i = 0; i < CIFS_ENCPWD_SIZE; i++)
		password_with_pad[i] = toupper(password_with_pad[i]);

	SMBencrypt(password_with_pad, cryptkey, lnm_session_key);

	/* clear password before we return/free memory */
	memset(password_with_pad, 0, CIFS_ENCPWD_SIZE);
}
#endif /* CIFS_WEAK_PW_HASH */

/* Build a proper attribute value/target info pairs blob.
 * Fill in netbios and dns domain name and workstation name
 * and client time (total five av pairs and + one end of fields indicator.
 * Allocate domain name which gets freed when session struct is deallocated.
 */
static int
build_avpair_blob(struct cifsSesInfo *ses, const struct nls_table *nls_cp)
{
	unsigned int dlen;
	unsigned int wlen;
	unsigned int size = 6 * sizeof(struct ntlmssp2_name);
	__le64  curtime;
	char *defdmname = "WORKGROUP";
	unsigned char *blobptr;
	struct ntlmssp2_name *attrptr;

	if (!ses->domainName) {
		ses->domainName = kstrdup(defdmname, GFP_KERNEL);
		if (!ses->domainName)
			return -ENOMEM;
	}

	dlen = strlen(ses->domainName);
	wlen = strlen(ses->server->hostname);

	/* The length of this blob is a size which is
	 * six times the size of a structure which holds name/size +
	 * two times the unicode length of a domain name +
	 * two times the unicode length of a server name +
	 * size of a timestamp (which is 8 bytes).
	 */
	ses->auth_key.len = size + 2 * (2 * dlen) + 2 * (2 * wlen) + 8;
	ses->auth_key.response = kzalloc(ses->auth_key.len, GFP_KERNEL);
	if (!ses->auth_key.response) {
		ses->auth_key.len = 0;
		cERROR(1, "Challenge target info allocation failure");
		return -ENOMEM;
	}

	blobptr = ses->auth_key.response;
	attrptr = (struct ntlmssp2_name *) blobptr;

	attrptr->type = cpu_to_le16(NTLMSSP_AV_NB_DOMAIN_NAME);
	attrptr->length = cpu_to_le16(2 * dlen);
	blobptr = (unsigned char *)attrptr + sizeof(struct ntlmssp2_name);
	cifs_strtoUCS((__le16 *)blobptr, ses->domainName, dlen, nls_cp);

	blobptr += 2 * dlen;
	attrptr = (struct ntlmssp2_name *) blobptr;

	attrptr->type = cpu_to_le16(NTLMSSP_AV_NB_COMPUTER_NAME);
	attrptr->length = cpu_to_le16(2 * wlen);
	blobptr = (unsigned char *)attrptr + sizeof(struct ntlmssp2_name);
	cifs_strtoUCS((__le16 *)blobptr, ses->server->hostname, wlen, nls_cp);

	blobptr += 2 * wlen;
	attrptr = (struct ntlmssp2_name *) blobptr;

	attrptr->type = cpu_to_le16(NTLMSSP_AV_DNS_DOMAIN_NAME);
	attrptr->length = cpu_to_le16(2 * dlen);
	blobptr = (unsigned char *)attrptr + sizeof(struct ntlmssp2_name);
	cifs_strtoUCS((__le16 *)blobptr, ses->domainName, dlen, nls_cp);

	blobptr += 2 * dlen;
	attrptr = (struct ntlmssp2_name *) blobptr;

	attrptr->type = cpu_to_le16(NTLMSSP_AV_DNS_COMPUTER_NAME);
	attrptr->length = cpu_to_le16(2 * wlen);
	blobptr = (unsigned char *)attrptr + sizeof(struct ntlmssp2_name);
	cifs_strtoUCS((__le16 *)blobptr, ses->server->hostname, wlen, nls_cp);

	blobptr += 2 * wlen;
	attrptr = (struct ntlmssp2_name *) blobptr;

	attrptr->type = cpu_to_le16(NTLMSSP_AV_TIMESTAMP);
	attrptr->length = cpu_to_le16(sizeof(__le64));
	blobptr = (unsigned char *)attrptr + sizeof(struct ntlmssp2_name);
	curtime = cpu_to_le64(cifs_UnixTimeToNT(CURRENT_TIME));
	memcpy(blobptr, &curtime, sizeof(__le64));

	return 0;
}

/* Server has provided av pairs/target info in the type 2 challenge
 * packet and we have plucked it and stored within smb session.
 * We parse that blob here to find netbios domain name to be used
 * as part of ntlmv2 authentication (in Target String), if not already
 * specified on the command line.
 * If this function returns without any error but without fetching
 * domain name, authentication may fail against some server but
 * may not fail against other (those who are not very particular
 * about target string i.e. for some, just user name might suffice.
 */
static int
find_domain_name(struct cifsSesInfo *ses, const struct nls_table *nls_cp)
{
	unsigned int attrsize;
	unsigned int type;
	unsigned int onesize = sizeof(struct ntlmssp2_name);
	unsigned char *blobptr;
	unsigned char *blobend;
	struct ntlmssp2_name *attrptr;

	if (!ses->auth_key.len || !ses->auth_key.response)
		return 0;

	blobptr = ses->auth_key.response;
	blobend = blobptr + ses->auth_key.len;

	while (blobptr + onesize < blobend) {
		attrptr = (struct ntlmssp2_name *) blobptr;
		type = le16_to_cpu(attrptr->type);
		if (type == NTLMSSP_AV_EOL)
			break;
		blobptr += 2; /* advance attr type */
		attrsize = le16_to_cpu(attrptr->length);
		blobptr += 2; /* advance attr size */
		if (blobptr + attrsize > blobend)
			break;
		if (type == NTLMSSP_AV_NB_DOMAIN_NAME) {
			if (!attrsize)
				break;
			if (!ses->domainName) {
				ses->domainName =
					kmalloc(attrsize + 1, GFP_KERNEL);
				if (!ses->domainName)
						return -ENOMEM;
				cifs_from_ucs2(ses->domainName,
					(__le16 *)blobptr, attrsize, attrsize,
					nls_cp, false);
				break;
			}
		}
		blobptr += attrsize; /* advance attr  value */
	}

	return 0;
}

static int calc_ntlmv2_hash(struct cifsSesInfo *ses, char *ntlmv2_hash,
			    const struct nls_table *nls_cp)
{
	int rc = 0;
	int len;
	char nt_hash[CIFS_NTHASH_SIZE];
	wchar_t *user;
	wchar_t *domain;
	wchar_t *server;

	if (!ses->server->secmech.sdeschmacmd5) {
		cERROR(1, "calc_ntlmv2_hash: can't generate ntlmv2 hash\n");
		return -1;
	}

	/* calculate md4 hash of password */
	E_md4hash(ses->password, nt_hash);

	crypto_shash_setkey(ses->server->secmech.hmacmd5, nt_hash,
				CIFS_NTHASH_SIZE);

	rc = crypto_shash_init(&ses->server->secmech.sdeschmacmd5->shash);
	if (rc) {
		cERROR(1, "calc_ntlmv2_hash: could not init hmacmd5\n");
		return rc;
	}

	/* convert ses->userName to unicode and uppercase */
	len = strlen(ses->userName);
	user = kmalloc(2 + (len * 2), GFP_KERNEL);
	if (user == NULL) {
		cERROR(1, "calc_ntlmv2_hash: user mem alloc failure\n");
		rc = -ENOMEM;
		goto calc_exit_2;
	}
	len = cifs_strtoUCS((__le16 *)user, ses->userName, len, nls_cp);
	UniStrupr(user);

	crypto_shash_update(&ses->server->secmech.sdeschmacmd5->shash,
				(char *)user, 2 * len);

	/* convert ses->domainName to unicode and uppercase */
	if (ses->domainName) {
		len = strlen(ses->domainName);

		domain = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (domain == NULL) {
			cERROR(1, "calc_ntlmv2_hash: domain mem alloc failure");
			rc = -ENOMEM;
			goto calc_exit_1;
		}
		len = cifs_strtoUCS((__le16 *)domain, ses->domainName, len,
					nls_cp);
		crypto_shash_update(&ses->server->secmech.sdeschmacmd5->shash,
					(char *)domain, 2 * len);
		kfree(domain);
	} else if (ses->serverName) {
		len = strlen(ses->serverName);

		server = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (server == NULL) {
			cERROR(1, "calc_ntlmv2_hash: server mem alloc failure");
			rc = -ENOMEM;
			goto calc_exit_1;
		}
		len = cifs_strtoUCS((__le16 *)server, ses->serverName, len,
					nls_cp);
		crypto_shash_update(&ses->server->secmech.sdeschmacmd5->shash,
					(char *)server, 2 * len);
		kfree(server);
	}

	rc = crypto_shash_final(&ses->server->secmech.sdeschmacmd5->shash,
					ntlmv2_hash);

calc_exit_1:
	kfree(user);
calc_exit_2:
	return rc;
}

static int
CalcNTLMv2_response(const struct cifsSesInfo *ses, char *ntlmv2_hash)
{
	int rc;
	unsigned int offset = CIFS_SESS_KEY_SIZE + 8;

	if (!ses->server->secmech.sdeschmacmd5) {
		cERROR(1, "calc_ntlmv2_hash: can't generate ntlmv2 hash\n");
		return -1;
	}

	crypto_shash_setkey(ses->server->secmech.hmacmd5,
				ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE);

	rc = crypto_shash_init(&ses->server->secmech.sdeschmacmd5->shash);
	if (rc) {
		cERROR(1, "CalcNTLMv2_response: could not init hmacmd5");
		return rc;
	}

	if (ses->server->secType == RawNTLMSSP)
		memcpy(ses->auth_key.response + offset,
			ses->ntlmssp->cryptkey, CIFS_SERVER_CHALLENGE_SIZE);
	else
		memcpy(ses->auth_key.response + offset,
			ses->server->cryptkey, CIFS_SERVER_CHALLENGE_SIZE);
	crypto_shash_update(&ses->server->secmech.sdeschmacmd5->shash,
		ses->auth_key.response + offset, ses->auth_key.len - offset);

	rc = crypto_shash_final(&ses->server->secmech.sdeschmacmd5->shash,
		ses->auth_key.response + CIFS_SESS_KEY_SIZE);

	return rc;
}


int
setup_ntlmv2_rsp(struct cifsSesInfo *ses, const struct nls_table *nls_cp)
{
	int rc;
	int baselen;
	unsigned int tilen;
	struct ntlmv2_resp *buf;
	char ntlmv2_hash[16];
	unsigned char *tiblob = NULL; /* target info blob */

	if (ses->server->secType == RawNTLMSSP) {
		if (!ses->domainName) {
			rc = find_domain_name(ses, nls_cp);
			if (rc) {
				cERROR(1, "error %d finding domain name", rc);
				goto setup_ntlmv2_rsp_ret;
			}
		}
	} else {
		rc = build_avpair_blob(ses, nls_cp);
		if (rc) {
			cERROR(1, "error %d building av pair blob", rc);
			goto setup_ntlmv2_rsp_ret;
		}
	}

	baselen = CIFS_SESS_KEY_SIZE + sizeof(struct ntlmv2_resp);
	tilen = ses->auth_key.len;
	tiblob = ses->auth_key.response;

	ses->auth_key.response = kmalloc(baselen + tilen, GFP_KERNEL);
	if (!ses->auth_key.response) {
		rc = ENOMEM;
		ses->auth_key.len = 0;
		cERROR(1, "%s: Can't allocate auth blob", __func__);
		goto setup_ntlmv2_rsp_ret;
	}
	ses->auth_key.len += baselen;

	buf = (struct ntlmv2_resp *)
			(ses->auth_key.response + CIFS_SESS_KEY_SIZE);
	buf->blob_signature = cpu_to_le32(0x00000101);
	buf->reserved = 0;
	buf->time = cpu_to_le64(cifs_UnixTimeToNT(CURRENT_TIME));
	get_random_bytes(&buf->client_chal, sizeof(buf->client_chal));
	buf->reserved2 = 0;

	memcpy(ses->auth_key.response + baselen, tiblob, tilen);

	/* calculate ntlmv2_hash */
	rc = calc_ntlmv2_hash(ses, ntlmv2_hash, nls_cp);
	if (rc) {
		cERROR(1, "could not get v2 hash rc %d", rc);
		goto setup_ntlmv2_rsp_ret;
	}

	/* calculate first part of the client response (CR1) */
	rc = CalcNTLMv2_response(ses, ntlmv2_hash);
	if (rc) {
		cERROR(1, "Could not calculate CR1  rc: %d", rc);
		goto setup_ntlmv2_rsp_ret;
	}

	/* now calculate the session key for NTLMv2 */
	crypto_shash_setkey(ses->server->secmech.hmacmd5,
		ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE);

	rc = crypto_shash_init(&ses->server->secmech.sdeschmacmd5->shash);
	if (rc) {
		cERROR(1, "%s: Could not init hmacmd5\n", __func__);
		goto setup_ntlmv2_rsp_ret;
	}

	crypto_shash_update(&ses->server->secmech.sdeschmacmd5->shash,
		ses->auth_key.response + CIFS_SESS_KEY_SIZE,
		CIFS_HMAC_MD5_HASH_SIZE);

	rc = crypto_shash_final(&ses->server->secmech.sdeschmacmd5->shash,
		ses->auth_key.response);

setup_ntlmv2_rsp_ret:
	kfree(tiblob);

	return rc;
}

int
calc_seckey(struct cifsSesInfo *ses)
{
	int rc;
	struct crypto_blkcipher *tfm_arc4;
	struct scatterlist sgin, sgout;
	struct blkcipher_desc desc;
	unsigned char sec_key[CIFS_SESS_KEY_SIZE]; /* a nonce */

	get_random_bytes(sec_key, CIFS_SESS_KEY_SIZE);

	tfm_arc4 = crypto_alloc_blkcipher("ecb(arc4)", 0, CRYPTO_ALG_ASYNC);
	if (!tfm_arc4 || IS_ERR(tfm_arc4)) {
		cERROR(1, "could not allocate crypto API arc4\n");
		return PTR_ERR(tfm_arc4);
	}

	desc.tfm = tfm_arc4;

	crypto_blkcipher_setkey(tfm_arc4, ses->auth_key.response,
					CIFS_SESS_KEY_SIZE);

	sg_init_one(&sgin, sec_key, CIFS_SESS_KEY_SIZE);
	sg_init_one(&sgout, ses->ntlmssp->ciphertext, CIFS_CPHTXT_SIZE);

	rc = crypto_blkcipher_encrypt(&desc, &sgout, &sgin, CIFS_CPHTXT_SIZE);
	if (rc) {
		cERROR(1, "could not encrypt session key rc: %d\n", rc);
		crypto_free_blkcipher(tfm_arc4);
		return rc;
	}

	/* make secondary_key/nonce as session key */
	memcpy(ses->auth_key.response, sec_key, CIFS_SESS_KEY_SIZE);
	/* and make len as that of session key only */
	ses->auth_key.len = CIFS_SESS_KEY_SIZE;

	crypto_free_blkcipher(tfm_arc4);

	return 0;
}

void
cifs_crypto_shash_release(struct TCP_Server_Info *server)
{
	if (server->secmech.md5)
		crypto_free_shash(server->secmech.md5);

	if (server->secmech.hmacmd5)
		crypto_free_shash(server->secmech.hmacmd5);

	kfree(server->secmech.sdeschmacmd5);

	kfree(server->secmech.sdescmd5);
}

int
cifs_crypto_shash_allocate(struct TCP_Server_Info *server)
{
	int rc;
	unsigned int size;

	server->secmech.hmacmd5 = crypto_alloc_shash("hmac(md5)", 0, 0);
	if (!server->secmech.hmacmd5 ||
			IS_ERR(server->secmech.hmacmd5)) {
		cERROR(1, "could not allocate crypto hmacmd5\n");
		return PTR_ERR(server->secmech.hmacmd5);
	}

	server->secmech.md5 = crypto_alloc_shash("md5", 0, 0);
	if (!server->secmech.md5 || IS_ERR(server->secmech.md5)) {
		cERROR(1, "could not allocate crypto md5\n");
		rc = PTR_ERR(server->secmech.md5);
		goto crypto_allocate_md5_fail;
	}

	size = sizeof(struct shash_desc) +
			crypto_shash_descsize(server->secmech.hmacmd5);
	server->secmech.sdeschmacmd5 = kmalloc(size, GFP_KERNEL);
	if (!server->secmech.sdeschmacmd5) {
		cERROR(1, "cifs_crypto_shash_allocate: can't alloc hmacmd5\n");
		rc = -ENOMEM;
		goto crypto_allocate_hmacmd5_sdesc_fail;
	}
	server->secmech.sdeschmacmd5->shash.tfm = server->secmech.hmacmd5;
	server->secmech.sdeschmacmd5->shash.flags = 0x0;


	size = sizeof(struct shash_desc) +
			crypto_shash_descsize(server->secmech.md5);
	server->secmech.sdescmd5 = kmalloc(size, GFP_KERNEL);
	if (!server->secmech.sdescmd5) {
		cERROR(1, "cifs_crypto_shash_allocate: can't alloc md5\n");
		rc = -ENOMEM;
		goto crypto_allocate_md5_sdesc_fail;
	}
	server->secmech.sdescmd5->shash.tfm = server->secmech.md5;
	server->secmech.sdescmd5->shash.flags = 0x0;

	return 0;

crypto_allocate_md5_sdesc_fail:
	kfree(server->secmech.sdeschmacmd5);

crypto_allocate_hmacmd5_sdesc_fail:
	crypto_free_shash(server->secmech.md5);

crypto_allocate_md5_fail:
	crypto_free_shash(server->secmech.hmacmd5);

	return rc;
}
