// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>
#include <linux/uio.h>
#include <linux/xattr.h>
#include <crypto/hash.h>
#include <crypto/aead.h>
#include <linux/random.h>
#include <linux/scatterlist.h>

#include "auth.h"
#include "glob.h"

#include <linux/fips.h>
#include <crypto/des.h>

#include "server.h"
#include "smb_common.h"
#include "connection.h"
#include "mgmt/user_session.h"
#include "mgmt/user_config.h"
#include "crypto_ctx.h"
#include "transport_ipc.h"

/*
 * Fixed format data defining GSS header and fixed string
 * "not_defined_in_RFC4178@please_ignore".
 * So sec blob data in neg phase could be generated statically.
 */
static char NEGOTIATE_GSS_HEADER[AUTH_GSS_LENGTH] = {
#ifdef CONFIG_SMB_SERVER_KERBEROS5
	0x60, 0x5e, 0x06, 0x06, 0x2b, 0x06, 0x01, 0x05,
	0x05, 0x02, 0xa0, 0x54, 0x30, 0x52, 0xa0, 0x24,
	0x30, 0x22, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x12, 0x01, 0x02, 0x02, 0x06, 0x09, 0x2a,
	0x86, 0x48, 0x82, 0xf7, 0x12, 0x01, 0x02, 0x02,
	0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82,
	0x37, 0x02, 0x02, 0x0a, 0xa3, 0x2a, 0x30, 0x28,
	0xa0, 0x26, 0x1b, 0x24, 0x6e, 0x6f, 0x74, 0x5f,
	0x64, 0x65, 0x66, 0x69, 0x6e, 0x65, 0x64, 0x5f,
	0x69, 0x6e, 0x5f, 0x52, 0x46, 0x43, 0x34, 0x31,
	0x37, 0x38, 0x40, 0x70, 0x6c, 0x65, 0x61, 0x73,
	0x65, 0x5f, 0x69, 0x67, 0x6e, 0x6f, 0x72, 0x65
#else
	0x60, 0x48, 0x06, 0x06, 0x2b, 0x06, 0x01, 0x05,
	0x05, 0x02, 0xa0, 0x3e, 0x30, 0x3c, 0xa0, 0x0e,
	0x30, 0x0c, 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04,
	0x01, 0x82, 0x37, 0x02, 0x02, 0x0a, 0xa3, 0x2a,
	0x30, 0x28, 0xa0, 0x26, 0x1b, 0x24, 0x6e, 0x6f,
	0x74, 0x5f, 0x64, 0x65, 0x66, 0x69, 0x6e, 0x65,
	0x64, 0x5f, 0x69, 0x6e, 0x5f, 0x52, 0x46, 0x43,
	0x34, 0x31, 0x37, 0x38, 0x40, 0x70, 0x6c, 0x65,
	0x61, 0x73, 0x65, 0x5f, 0x69, 0x67, 0x6e, 0x6f,
	0x72, 0x65
#endif
};

void ksmbd_copy_gss_neg_header(void *buf)
{
	memcpy(buf, NEGOTIATE_GSS_HEADER, AUTH_GSS_LENGTH);
}

/**
 * ksmbd_gen_sess_key() - function to generate session key
 * @sess:	session of connection
 * @hash:	source hash value to be used for find session key
 * @hmac:	source hmac value to be used for finding session key
 *
 */
static int ksmbd_gen_sess_key(struct ksmbd_session *sess, char *hash,
			      char *hmac)
{
	struct ksmbd_crypto_ctx *ctx;
	int rc;

	ctx = ksmbd_crypto_ctx_find_hmacmd5();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not crypto alloc hmacmd5\n");
		return -ENOMEM;
	}

	rc = crypto_shash_setkey(CRYPTO_HMACMD5_TFM(ctx),
				 hash,
				 CIFS_HMAC_MD5_HASH_SIZE);
	if (rc) {
		ksmbd_debug(AUTH, "hmacmd5 set key fail error %d\n", rc);
		goto out;
	}

	rc = crypto_shash_init(CRYPTO_HMACMD5(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "could not init hmacmd5 error %d\n", rc);
		goto out;
	}

	rc = crypto_shash_update(CRYPTO_HMACMD5(ctx),
				 hmac,
				 SMB2_NTLMV2_SESSKEY_SIZE);
	if (rc) {
		ksmbd_debug(AUTH, "Could not update with response error %d\n", rc);
		goto out;
	}

	rc = crypto_shash_final(CRYPTO_HMACMD5(ctx), sess->sess_key);
	if (rc) {
		ksmbd_debug(AUTH, "Could not generate hmacmd5 hash error %d\n", rc);
		goto out;
	}

out:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}

static int calc_ntlmv2_hash(struct ksmbd_session *sess, char *ntlmv2_hash,
			    char *dname)
{
	int ret, len, conv_len;
	wchar_t *domain = NULL;
	__le16 *uniname = NULL;
	struct ksmbd_crypto_ctx *ctx;

	ctx = ksmbd_crypto_ctx_find_hmacmd5();
	if (!ctx) {
		ksmbd_debug(AUTH, "can't generate ntlmv2 hash\n");
		return -ENOMEM;
	}

	ret = crypto_shash_setkey(CRYPTO_HMACMD5_TFM(ctx),
				  user_passkey(sess->user),
				  CIFS_ENCPWD_SIZE);
	if (ret) {
		ksmbd_debug(AUTH, "Could not set NT Hash as a key\n");
		goto out;
	}

	ret = crypto_shash_init(CRYPTO_HMACMD5(ctx));
	if (ret) {
		ksmbd_debug(AUTH, "could not init hmacmd5\n");
		goto out;
	}

	/* convert user_name to unicode */
	len = strlen(user_name(sess->user));
	uniname = kzalloc(2 + UNICODE_LEN(len), GFP_KERNEL);
	if (!uniname) {
		ret = -ENOMEM;
		goto out;
	}

	conv_len = smb_strtoUTF16(uniname, user_name(sess->user), len,
				  sess->conn->local_nls);
	if (conv_len < 0 || conv_len > len) {
		ret = -EINVAL;
		goto out;
	}
	UniStrupr(uniname);

	ret = crypto_shash_update(CRYPTO_HMACMD5(ctx),
				  (char *)uniname,
				  UNICODE_LEN(conv_len));
	if (ret) {
		ksmbd_debug(AUTH, "Could not update with user\n");
		goto out;
	}

	/* Convert domain name or conn name to unicode and uppercase */
	len = strlen(dname);
	domain = kzalloc(2 + UNICODE_LEN(len), GFP_KERNEL);
	if (!domain) {
		ret = -ENOMEM;
		goto out;
	}

	conv_len = smb_strtoUTF16((__le16 *)domain, dname, len,
				  sess->conn->local_nls);
	if (conv_len < 0 || conv_len > len) {
		ret = -EINVAL;
		goto out;
	}

	ret = crypto_shash_update(CRYPTO_HMACMD5(ctx),
				  (char *)domain,
				  UNICODE_LEN(conv_len));
	if (ret) {
		ksmbd_debug(AUTH, "Could not update with domain\n");
		goto out;
	}

	ret = crypto_shash_final(CRYPTO_HMACMD5(ctx), ntlmv2_hash);
	if (ret)
		ksmbd_debug(AUTH, "Could not generate md5 hash\n");
out:
	kfree(uniname);
	kfree(domain);
	ksmbd_release_crypto_ctx(ctx);
	return ret;
}

/**
 * ksmbd_auth_ntlmv2() - NTLMv2 authentication handler
 * @sess:	session of connection
 * @ntlmv2:		NTLMv2 challenge response
 * @blen:		NTLMv2 blob length
 * @domain_name:	domain name
 *
 * Return:	0 on success, error number on error
 */
int ksmbd_auth_ntlmv2(struct ksmbd_session *sess, struct ntlmv2_resp *ntlmv2,
		      int blen, char *domain_name)
{
	char ntlmv2_hash[CIFS_ENCPWD_SIZE];
	char ntlmv2_rsp[CIFS_HMAC_MD5_HASH_SIZE];
	struct ksmbd_crypto_ctx *ctx;
	char *construct = NULL;
	int rc, len;

	ctx = ksmbd_crypto_ctx_find_hmacmd5();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not crypto alloc hmacmd5\n");
		return -ENOMEM;
	}

	rc = calc_ntlmv2_hash(sess, ntlmv2_hash, domain_name);
	if (rc) {
		ksmbd_debug(AUTH, "could not get v2 hash rc %d\n", rc);
		goto out;
	}

	rc = crypto_shash_setkey(CRYPTO_HMACMD5_TFM(ctx),
				 ntlmv2_hash,
				 CIFS_HMAC_MD5_HASH_SIZE);
	if (rc) {
		ksmbd_debug(AUTH, "Could not set NTLMV2 Hash as a key\n");
		goto out;
	}

	rc = crypto_shash_init(CRYPTO_HMACMD5(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "Could not init hmacmd5\n");
		goto out;
	}

	len = CIFS_CRYPTO_KEY_SIZE + blen;
	construct = kzalloc(len, GFP_KERNEL);
	if (!construct) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(construct, sess->ntlmssp.cryptkey, CIFS_CRYPTO_KEY_SIZE);
	memcpy(construct + CIFS_CRYPTO_KEY_SIZE, &ntlmv2->blob_signature, blen);

	rc = crypto_shash_update(CRYPTO_HMACMD5(ctx), construct, len);
	if (rc) {
		ksmbd_debug(AUTH, "Could not update with response\n");
		goto out;
	}

	rc = crypto_shash_final(CRYPTO_HMACMD5(ctx), ntlmv2_rsp);
	if (rc) {
		ksmbd_debug(AUTH, "Could not generate md5 hash\n");
		goto out;
	}

	rc = ksmbd_gen_sess_key(sess, ntlmv2_hash, ntlmv2_rsp);
	if (rc) {
		ksmbd_debug(AUTH, "Could not generate sess key\n");
		goto out;
	}

	if (memcmp(ntlmv2->ntlmv2_hash, ntlmv2_rsp, CIFS_HMAC_MD5_HASH_SIZE) != 0)
		rc = -EINVAL;
out:
	ksmbd_release_crypto_ctx(ctx);
	kfree(construct);
	return rc;
}

/**
 * ksmbd_decode_ntlmssp_auth_blob() - helper function to construct
 * authenticate blob
 * @authblob:	authenticate blob source pointer
 * @usr:	user details
 * @sess:	session of connection
 *
 * Return:	0 on success, error number on error
 */
int ksmbd_decode_ntlmssp_auth_blob(struct authenticate_message *authblob,
				   int blob_len, struct ksmbd_session *sess)
{
	char *domain_name;
	unsigned int lm_off, nt_off;
	unsigned short nt_len;
	int ret;

	if (blob_len < sizeof(struct authenticate_message)) {
		ksmbd_debug(AUTH, "negotiate blob len %d too small\n",
			    blob_len);
		return -EINVAL;
	}

	if (memcmp(authblob->Signature, "NTLMSSP", 8)) {
		ksmbd_debug(AUTH, "blob signature incorrect %s\n",
			    authblob->Signature);
		return -EINVAL;
	}

	lm_off = le32_to_cpu(authblob->LmChallengeResponse.BufferOffset);
	nt_off = le32_to_cpu(authblob->NtChallengeResponse.BufferOffset);
	nt_len = le16_to_cpu(authblob->NtChallengeResponse.Length);

	/* TODO : use domain name that imported from configuration file */
	domain_name = smb_strndup_from_utf16((const char *)authblob +
			le32_to_cpu(authblob->DomainName.BufferOffset),
			le16_to_cpu(authblob->DomainName.Length), true,
			sess->conn->local_nls);
	if (IS_ERR(domain_name))
		return PTR_ERR(domain_name);

	/* process NTLMv2 authentication */
	ksmbd_debug(AUTH, "decode_ntlmssp_authenticate_blob dname%s\n",
		    domain_name);
	ret = ksmbd_auth_ntlmv2(sess, (struct ntlmv2_resp *)((char *)authblob + nt_off),
				nt_len - CIFS_ENCPWD_SIZE,
				domain_name);
	kfree(domain_name);
	return ret;
}

/**
 * ksmbd_decode_ntlmssp_neg_blob() - helper function to construct
 * negotiate blob
 * @negblob: negotiate blob source pointer
 * @rsp:     response header pointer to be updated
 * @sess:    session of connection
 *
 */
int ksmbd_decode_ntlmssp_neg_blob(struct negotiate_message *negblob,
				  int blob_len, struct ksmbd_session *sess)
{
	if (blob_len < sizeof(struct negotiate_message)) {
		ksmbd_debug(AUTH, "negotiate blob len %d too small\n",
			    blob_len);
		return -EINVAL;
	}

	if (memcmp(negblob->Signature, "NTLMSSP", 8)) {
		ksmbd_debug(AUTH, "blob signature incorrect %s\n",
			    negblob->Signature);
		return -EINVAL;
	}

	sess->ntlmssp.client_flags = le32_to_cpu(negblob->NegotiateFlags);
	return 0;
}

/**
 * ksmbd_build_ntlmssp_challenge_blob() - helper function to construct
 * challenge blob
 * @chgblob: challenge blob source pointer to initialize
 * @rsp:     response header pointer to be updated
 * @sess:    session of connection
 *
 */
unsigned int
ksmbd_build_ntlmssp_challenge_blob(struct challenge_message *chgblob,
				   struct ksmbd_session *sess)
{
	struct target_info *tinfo;
	wchar_t *name;
	__u8 *target_name;
	unsigned int flags, blob_off, blob_len, type, target_info_len = 0;
	int len, uni_len, conv_len;
	int cflags = sess->ntlmssp.client_flags;

	memcpy(chgblob->Signature, NTLMSSP_SIGNATURE, 8);
	chgblob->MessageType = NtLmChallenge;

	flags = NTLMSSP_NEGOTIATE_UNICODE |
		NTLMSSP_NEGOTIATE_NTLM | NTLMSSP_TARGET_TYPE_SERVER |
		NTLMSSP_NEGOTIATE_TARGET_INFO;

	if (cflags & NTLMSSP_NEGOTIATE_SIGN) {
		flags |= NTLMSSP_NEGOTIATE_SIGN;
		flags |= cflags & (NTLMSSP_NEGOTIATE_128 |
				   NTLMSSP_NEGOTIATE_56);
	}

	if (cflags & NTLMSSP_NEGOTIATE_ALWAYS_SIGN)
		flags |= NTLMSSP_NEGOTIATE_ALWAYS_SIGN;

	if (cflags & NTLMSSP_REQUEST_TARGET)
		flags |= NTLMSSP_REQUEST_TARGET;

	if (sess->conn->use_spnego &&
	    (cflags & NTLMSSP_NEGOTIATE_EXTENDED_SEC))
		flags |= NTLMSSP_NEGOTIATE_EXTENDED_SEC;

	chgblob->NegotiateFlags = cpu_to_le32(flags);
	len = strlen(ksmbd_netbios_name());
	name = kmalloc(2 + UNICODE_LEN(len), GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	conv_len = smb_strtoUTF16((__le16 *)name, ksmbd_netbios_name(), len,
				  sess->conn->local_nls);
	if (conv_len < 0 || conv_len > len) {
		kfree(name);
		return -EINVAL;
	}

	uni_len = UNICODE_LEN(conv_len);

	blob_off = sizeof(struct challenge_message);
	blob_len = blob_off + uni_len;

	chgblob->TargetName.Length = cpu_to_le16(uni_len);
	chgblob->TargetName.MaximumLength = cpu_to_le16(uni_len);
	chgblob->TargetName.BufferOffset = cpu_to_le32(blob_off);

	/* Initialize random conn challenge */
	get_random_bytes(sess->ntlmssp.cryptkey, sizeof(__u64));
	memcpy(chgblob->Challenge, sess->ntlmssp.cryptkey,
	       CIFS_CRYPTO_KEY_SIZE);

	/* Add Target Information to security buffer */
	chgblob->TargetInfoArray.BufferOffset = cpu_to_le32(blob_len);

	target_name = (__u8 *)chgblob + blob_off;
	memcpy(target_name, name, uni_len);
	tinfo = (struct target_info *)(target_name + uni_len);

	chgblob->TargetInfoArray.Length = 0;
	/* Add target info list for NetBIOS/DNS settings */
	for (type = NTLMSSP_AV_NB_COMPUTER_NAME;
	     type <= NTLMSSP_AV_DNS_DOMAIN_NAME; type++) {
		tinfo->Type = cpu_to_le16(type);
		tinfo->Length = cpu_to_le16(uni_len);
		memcpy(tinfo->Content, name, uni_len);
		tinfo = (struct target_info *)((char *)tinfo + 4 + uni_len);
		target_info_len += 4 + uni_len;
	}

	/* Add terminator subblock */
	tinfo->Type = 0;
	tinfo->Length = 0;
	target_info_len += 4;

	chgblob->TargetInfoArray.Length = cpu_to_le16(target_info_len);
	chgblob->TargetInfoArray.MaximumLength = cpu_to_le16(target_info_len);
	blob_len += target_info_len;
	kfree(name);
	ksmbd_debug(AUTH, "NTLMSSP SecurityBufferLength %d\n", blob_len);
	return blob_len;
}

#ifdef CONFIG_SMB_SERVER_KERBEROS5
int ksmbd_krb5_authenticate(struct ksmbd_session *sess, char *in_blob,
			    int in_len, char *out_blob, int *out_len)
{
	struct ksmbd_spnego_authen_response *resp;
	struct ksmbd_user *user = NULL;
	int retval;

	resp = ksmbd_ipc_spnego_authen_request(in_blob, in_len);
	if (!resp) {
		ksmbd_debug(AUTH, "SPNEGO_AUTHEN_REQUEST failure\n");
		return -EINVAL;
	}

	if (!(resp->login_response.status & KSMBD_USER_FLAG_OK)) {
		ksmbd_debug(AUTH, "krb5 authentication failure\n");
		retval = -EPERM;
		goto out;
	}

	if (*out_len <= resp->spnego_blob_len) {
		ksmbd_debug(AUTH, "buf len %d, but blob len %d\n",
			    *out_len, resp->spnego_blob_len);
		retval = -EINVAL;
		goto out;
	}

	if (resp->session_key_len > sizeof(sess->sess_key)) {
		ksmbd_debug(AUTH, "session key is too long\n");
		retval = -EINVAL;
		goto out;
	}

	user = ksmbd_alloc_user(&resp->login_response);
	if (!user) {
		ksmbd_debug(AUTH, "login failure\n");
		retval = -ENOMEM;
		goto out;
	}
	sess->user = user;

	memcpy(sess->sess_key, resp->payload, resp->session_key_len);
	memcpy(out_blob, resp->payload + resp->session_key_len,
	       resp->spnego_blob_len);
	*out_len = resp->spnego_blob_len;
	retval = 0;
out:
	kvfree(resp);
	return retval;
}
#else
int ksmbd_krb5_authenticate(struct ksmbd_session *sess, char *in_blob,
			    int in_len, char *out_blob, int *out_len)
{
	return -EOPNOTSUPP;
}
#endif

/**
 * ksmbd_sign_smb2_pdu() - function to generate packet signing
 * @conn:	connection
 * @key:	signing key
 * @iov:        buffer iov array
 * @n_vec:	number of iovecs
 * @sig:	signature value generated for client request packet
 *
 */
int ksmbd_sign_smb2_pdu(struct ksmbd_conn *conn, char *key, struct kvec *iov,
			int n_vec, char *sig)
{
	struct ksmbd_crypto_ctx *ctx;
	int rc, i;

	ctx = ksmbd_crypto_ctx_find_hmacsha256();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not crypto alloc hmacmd5\n");
		return -ENOMEM;
	}

	rc = crypto_shash_setkey(CRYPTO_HMACSHA256_TFM(ctx),
				 key,
				 SMB2_NTLMV2_SESSKEY_SIZE);
	if (rc)
		goto out;

	rc = crypto_shash_init(CRYPTO_HMACSHA256(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "hmacsha256 init error %d\n", rc);
		goto out;
	}

	for (i = 0; i < n_vec; i++) {
		rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx),
					 iov[i].iov_base,
					 iov[i].iov_len);
		if (rc) {
			ksmbd_debug(AUTH, "hmacsha256 update error %d\n", rc);
			goto out;
		}
	}

	rc = crypto_shash_final(CRYPTO_HMACSHA256(ctx), sig);
	if (rc)
		ksmbd_debug(AUTH, "hmacsha256 generation error %d\n", rc);
out:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}

/**
 * ksmbd_sign_smb3_pdu() - function to generate packet signing
 * @conn:	connection
 * @key:	signing key
 * @iov:        buffer iov array
 * @n_vec:	number of iovecs
 * @sig:	signature value generated for client request packet
 *
 */
int ksmbd_sign_smb3_pdu(struct ksmbd_conn *conn, char *key, struct kvec *iov,
			int n_vec, char *sig)
{
	struct ksmbd_crypto_ctx *ctx;
	int rc, i;

	ctx = ksmbd_crypto_ctx_find_cmacaes();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not crypto alloc cmac\n");
		return -ENOMEM;
	}

	rc = crypto_shash_setkey(CRYPTO_CMACAES_TFM(ctx),
				 key,
				 SMB2_CMACAES_SIZE);
	if (rc)
		goto out;

	rc = crypto_shash_init(CRYPTO_CMACAES(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "cmaces init error %d\n", rc);
		goto out;
	}

	for (i = 0; i < n_vec; i++) {
		rc = crypto_shash_update(CRYPTO_CMACAES(ctx),
					 iov[i].iov_base,
					 iov[i].iov_len);
		if (rc) {
			ksmbd_debug(AUTH, "cmaces update error %d\n", rc);
			goto out;
		}
	}

	rc = crypto_shash_final(CRYPTO_CMACAES(ctx), sig);
	if (rc)
		ksmbd_debug(AUTH, "cmaces generation error %d\n", rc);
out:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}

struct derivation {
	struct kvec label;
	struct kvec context;
	bool binding;
};

static int generate_key(struct ksmbd_session *sess, struct kvec label,
			struct kvec context, __u8 *key, unsigned int key_size)
{
	unsigned char zero = 0x0;
	__u8 i[4] = {0, 0, 0, 1};
	__u8 L128[4] = {0, 0, 0, 128};
	__u8 L256[4] = {0, 0, 1, 0};
	int rc;
	unsigned char prfhash[SMB2_HMACSHA256_SIZE];
	unsigned char *hashptr = prfhash;
	struct ksmbd_crypto_ctx *ctx;

	memset(prfhash, 0x0, SMB2_HMACSHA256_SIZE);
	memset(key, 0x0, key_size);

	ctx = ksmbd_crypto_ctx_find_hmacsha256();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not crypto alloc hmacmd5\n");
		return -ENOMEM;
	}

	rc = crypto_shash_setkey(CRYPTO_HMACSHA256_TFM(ctx),
				 sess->sess_key,
				 SMB2_NTLMV2_SESSKEY_SIZE);
	if (rc)
		goto smb3signkey_ret;

	rc = crypto_shash_init(CRYPTO_HMACSHA256(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "hmacsha256 init error %d\n", rc);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx), i, 4);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with n\n");
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx),
				 label.iov_base,
				 label.iov_len);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with label\n");
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx), &zero, 1);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with zero\n");
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx),
				 context.iov_base,
				 context.iov_len);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with context\n");
		goto smb3signkey_ret;
	}

	if (sess->conn->cipher_type == SMB2_ENCRYPTION_AES256_CCM ||
	    sess->conn->cipher_type == SMB2_ENCRYPTION_AES256_GCM)
		rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx), L256, 4);
	else
		rc = crypto_shash_update(CRYPTO_HMACSHA256(ctx), L128, 4);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with L\n");
		goto smb3signkey_ret;
	}

	rc = crypto_shash_final(CRYPTO_HMACSHA256(ctx), hashptr);
	if (rc) {
		ksmbd_debug(AUTH, "Could not generate hmacmd5 hash error %d\n",
			    rc);
		goto smb3signkey_ret;
	}

	memcpy(key, hashptr, key_size);

smb3signkey_ret:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}

static int generate_smb3signingkey(struct ksmbd_session *sess,
				   struct ksmbd_conn *conn,
				   const struct derivation *signing)
{
	int rc;
	struct channel *chann;
	char *key;

	chann = lookup_chann_list(sess, conn);
	if (!chann)
		return 0;

	if (sess->conn->dialect >= SMB30_PROT_ID && signing->binding)
		key = chann->smb3signingkey;
	else
		key = sess->smb3signingkey;

	rc = generate_key(sess, signing->label, signing->context, key,
			  SMB3_SIGN_KEY_SIZE);
	if (rc)
		return rc;

	if (!(sess->conn->dialect >= SMB30_PROT_ID && signing->binding))
		memcpy(chann->smb3signingkey, key, SMB3_SIGN_KEY_SIZE);

	ksmbd_debug(AUTH, "dumping generated AES signing keys\n");
	ksmbd_debug(AUTH, "Session Id    %llu\n", sess->id);
	ksmbd_debug(AUTH, "Session Key   %*ph\n",
		    SMB2_NTLMV2_SESSKEY_SIZE, sess->sess_key);
	ksmbd_debug(AUTH, "Signing Key   %*ph\n",
		    SMB3_SIGN_KEY_SIZE, key);
	return 0;
}

int ksmbd_gen_smb30_signingkey(struct ksmbd_session *sess,
			       struct ksmbd_conn *conn)
{
	struct derivation d;

	d.label.iov_base = "SMB2AESCMAC";
	d.label.iov_len = 12;
	d.context.iov_base = "SmbSign";
	d.context.iov_len = 8;
	d.binding = conn->binding;

	return generate_smb3signingkey(sess, conn, &d);
}

int ksmbd_gen_smb311_signingkey(struct ksmbd_session *sess,
				struct ksmbd_conn *conn)
{
	struct derivation d;

	d.label.iov_base = "SMBSigningKey";
	d.label.iov_len = 14;
	if (conn->binding) {
		struct preauth_session *preauth_sess;

		preauth_sess = ksmbd_preauth_session_lookup(conn, sess->id);
		if (!preauth_sess)
			return -ENOENT;
		d.context.iov_base = preauth_sess->Preauth_HashValue;
	} else {
		d.context.iov_base = sess->Preauth_HashValue;
	}
	d.context.iov_len = 64;
	d.binding = conn->binding;

	return generate_smb3signingkey(sess, conn, &d);
}

struct derivation_twin {
	struct derivation encryption;
	struct derivation decryption;
};

static int generate_smb3encryptionkey(struct ksmbd_session *sess,
				      const struct derivation_twin *ptwin)
{
	int rc;

	rc = generate_key(sess, ptwin->encryption.label,
			  ptwin->encryption.context, sess->smb3encryptionkey,
			  SMB3_ENC_DEC_KEY_SIZE);
	if (rc)
		return rc;

	rc = generate_key(sess, ptwin->decryption.label,
			  ptwin->decryption.context,
			  sess->smb3decryptionkey, SMB3_ENC_DEC_KEY_SIZE);
	if (rc)
		return rc;

	ksmbd_debug(AUTH, "dumping generated AES encryption keys\n");
	ksmbd_debug(AUTH, "Cipher type   %d\n", sess->conn->cipher_type);
	ksmbd_debug(AUTH, "Session Id    %llu\n", sess->id);
	ksmbd_debug(AUTH, "Session Key   %*ph\n",
		    SMB2_NTLMV2_SESSKEY_SIZE, sess->sess_key);
	if (sess->conn->cipher_type == SMB2_ENCRYPTION_AES256_CCM ||
	    sess->conn->cipher_type == SMB2_ENCRYPTION_AES256_GCM) {
		ksmbd_debug(AUTH, "ServerIn Key  %*ph\n",
			    SMB3_GCM256_CRYPTKEY_SIZE, sess->smb3encryptionkey);
		ksmbd_debug(AUTH, "ServerOut Key %*ph\n",
			    SMB3_GCM256_CRYPTKEY_SIZE, sess->smb3decryptionkey);
	} else {
		ksmbd_debug(AUTH, "ServerIn Key  %*ph\n",
			    SMB3_GCM128_CRYPTKEY_SIZE, sess->smb3encryptionkey);
		ksmbd_debug(AUTH, "ServerOut Key %*ph\n",
			    SMB3_GCM128_CRYPTKEY_SIZE, sess->smb3decryptionkey);
	}
	return 0;
}

int ksmbd_gen_smb30_encryptionkey(struct ksmbd_session *sess)
{
	struct derivation_twin twin;
	struct derivation *d;

	d = &twin.encryption;
	d->label.iov_base = "SMB2AESCCM";
	d->label.iov_len = 11;
	d->context.iov_base = "ServerOut";
	d->context.iov_len = 10;

	d = &twin.decryption;
	d->label.iov_base = "SMB2AESCCM";
	d->label.iov_len = 11;
	d->context.iov_base = "ServerIn ";
	d->context.iov_len = 10;

	return generate_smb3encryptionkey(sess, &twin);
}

int ksmbd_gen_smb311_encryptionkey(struct ksmbd_session *sess)
{
	struct derivation_twin twin;
	struct derivation *d;

	d = &twin.encryption;
	d->label.iov_base = "SMBS2CCipherKey";
	d->label.iov_len = 16;
	d->context.iov_base = sess->Preauth_HashValue;
	d->context.iov_len = 64;

	d = &twin.decryption;
	d->label.iov_base = "SMBC2SCipherKey";
	d->label.iov_len = 16;
	d->context.iov_base = sess->Preauth_HashValue;
	d->context.iov_len = 64;

	return generate_smb3encryptionkey(sess, &twin);
}

int ksmbd_gen_preauth_integrity_hash(struct ksmbd_conn *conn, char *buf,
				     __u8 *pi_hash)
{
	int rc;
	struct smb2_hdr *rcv_hdr = (struct smb2_hdr *)buf;
	char *all_bytes_msg = (char *)&rcv_hdr->ProtocolId;
	int msg_size = be32_to_cpu(rcv_hdr->smb2_buf_length);
	struct ksmbd_crypto_ctx *ctx = NULL;

	if (conn->preauth_info->Preauth_HashId !=
	    SMB2_PREAUTH_INTEGRITY_SHA512)
		return -EINVAL;

	ctx = ksmbd_crypto_ctx_find_sha512();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not alloc sha512\n");
		return -ENOMEM;
	}

	rc = crypto_shash_init(CRYPTO_SHA512(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "could not init shashn");
		goto out;
	}

	rc = crypto_shash_update(CRYPTO_SHA512(ctx), pi_hash, 64);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with n\n");
		goto out;
	}

	rc = crypto_shash_update(CRYPTO_SHA512(ctx), all_bytes_msg, msg_size);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with n\n");
		goto out;
	}

	rc = crypto_shash_final(CRYPTO_SHA512(ctx), pi_hash);
	if (rc) {
		ksmbd_debug(AUTH, "Could not generate hash err : %d\n", rc);
		goto out;
	}
out:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}

int ksmbd_gen_sd_hash(struct ksmbd_conn *conn, char *sd_buf, int len,
		      __u8 *pi_hash)
{
	int rc;
	struct ksmbd_crypto_ctx *ctx = NULL;

	ctx = ksmbd_crypto_ctx_find_sha256();
	if (!ctx) {
		ksmbd_debug(AUTH, "could not alloc sha256\n");
		return -ENOMEM;
	}

	rc = crypto_shash_init(CRYPTO_SHA256(ctx));
	if (rc) {
		ksmbd_debug(AUTH, "could not init shashn");
		goto out;
	}

	rc = crypto_shash_update(CRYPTO_SHA256(ctx), sd_buf, len);
	if (rc) {
		ksmbd_debug(AUTH, "could not update with n\n");
		goto out;
	}

	rc = crypto_shash_final(CRYPTO_SHA256(ctx), pi_hash);
	if (rc) {
		ksmbd_debug(AUTH, "Could not generate hash err : %d\n", rc);
		goto out;
	}
out:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}

static int ksmbd_get_encryption_key(struct ksmbd_conn *conn, __u64 ses_id,
				    int enc, u8 *key)
{
	struct ksmbd_session *sess;
	u8 *ses_enc_key;

	sess = ksmbd_session_lookup_all(conn, ses_id);
	if (!sess)
		return -EINVAL;

	ses_enc_key = enc ? sess->smb3encryptionkey :
		sess->smb3decryptionkey;
	memcpy(key, ses_enc_key, SMB3_ENC_DEC_KEY_SIZE);

	return 0;
}

static inline void smb2_sg_set_buf(struct scatterlist *sg, const void *buf,
				   unsigned int buflen)
{
	void *addr;

	if (is_vmalloc_addr(buf))
		addr = vmalloc_to_page(buf);
	else
		addr = virt_to_page(buf);
	sg_set_page(sg, addr, buflen, offset_in_page(buf));
}

static struct scatterlist *ksmbd_init_sg(struct kvec *iov, unsigned int nvec,
					 u8 *sign)
{
	struct scatterlist *sg;
	unsigned int assoc_data_len = sizeof(struct smb2_transform_hdr) - 24;
	int i, nr_entries[3] = {0}, total_entries = 0, sg_idx = 0;

	if (!nvec)
		return NULL;

	for (i = 0; i < nvec - 1; i++) {
		unsigned long kaddr = (unsigned long)iov[i + 1].iov_base;

		if (is_vmalloc_addr(iov[i + 1].iov_base)) {
			nr_entries[i] = ((kaddr + iov[i + 1].iov_len +
					PAGE_SIZE - 1) >> PAGE_SHIFT) -
				(kaddr >> PAGE_SHIFT);
		} else {
			nr_entries[i]++;
		}
		total_entries += nr_entries[i];
	}

	/* Add two entries for transform header and signature */
	total_entries += 2;

	sg = kmalloc_array(total_entries, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg)
		return NULL;

	sg_init_table(sg, total_entries);
	smb2_sg_set_buf(&sg[sg_idx++], iov[0].iov_base + 24, assoc_data_len);
	for (i = 0; i < nvec - 1; i++) {
		void *data = iov[i + 1].iov_base;
		int len = iov[i + 1].iov_len;

		if (is_vmalloc_addr(data)) {
			int j, offset = offset_in_page(data);

			for (j = 0; j < nr_entries[i]; j++) {
				unsigned int bytes = PAGE_SIZE - offset;

				if (!len)
					break;

				if (bytes > len)
					bytes = len;

				sg_set_page(&sg[sg_idx++],
					    vmalloc_to_page(data), bytes,
					    offset_in_page(data));

				data += bytes;
				len -= bytes;
				offset = 0;
			}
		} else {
			sg_set_page(&sg[sg_idx++], virt_to_page(data), len,
				    offset_in_page(data));
		}
	}
	smb2_sg_set_buf(&sg[sg_idx], sign, SMB2_SIGNATURE_SIZE);
	return sg;
}

int ksmbd_crypt_message(struct ksmbd_conn *conn, struct kvec *iov,
			unsigned int nvec, int enc)
{
	struct smb2_transform_hdr *tr_hdr =
		(struct smb2_transform_hdr *)iov[0].iov_base;
	unsigned int assoc_data_len = sizeof(struct smb2_transform_hdr) - 24;
	int rc;
	struct scatterlist *sg;
	u8 sign[SMB2_SIGNATURE_SIZE] = {};
	u8 key[SMB3_ENC_DEC_KEY_SIZE];
	struct aead_request *req;
	char *iv;
	unsigned int iv_len;
	struct crypto_aead *tfm;
	unsigned int crypt_len = le32_to_cpu(tr_hdr->OriginalMessageSize);
	struct ksmbd_crypto_ctx *ctx;

	rc = ksmbd_get_encryption_key(conn,
				      le64_to_cpu(tr_hdr->SessionId),
				      enc,
				      key);
	if (rc) {
		pr_err("Could not get %scryption key\n", enc ? "en" : "de");
		return rc;
	}

	if (conn->cipher_type == SMB2_ENCRYPTION_AES128_GCM ||
	    conn->cipher_type == SMB2_ENCRYPTION_AES256_GCM)
		ctx = ksmbd_crypto_ctx_find_gcm();
	else
		ctx = ksmbd_crypto_ctx_find_ccm();
	if (!ctx) {
		pr_err("crypto alloc failed\n");
		return -ENOMEM;
	}

	if (conn->cipher_type == SMB2_ENCRYPTION_AES128_GCM ||
	    conn->cipher_type == SMB2_ENCRYPTION_AES256_GCM)
		tfm = CRYPTO_GCM(ctx);
	else
		tfm = CRYPTO_CCM(ctx);

	if (conn->cipher_type == SMB2_ENCRYPTION_AES256_CCM ||
	    conn->cipher_type == SMB2_ENCRYPTION_AES256_GCM)
		rc = crypto_aead_setkey(tfm, key, SMB3_GCM256_CRYPTKEY_SIZE);
	else
		rc = crypto_aead_setkey(tfm, key, SMB3_GCM128_CRYPTKEY_SIZE);
	if (rc) {
		pr_err("Failed to set aead key %d\n", rc);
		goto free_ctx;
	}

	rc = crypto_aead_setauthsize(tfm, SMB2_SIGNATURE_SIZE);
	if (rc) {
		pr_err("Failed to set authsize %d\n", rc);
		goto free_ctx;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		rc = -ENOMEM;
		goto free_ctx;
	}

	if (!enc) {
		memcpy(sign, &tr_hdr->Signature, SMB2_SIGNATURE_SIZE);
		crypt_len += SMB2_SIGNATURE_SIZE;
	}

	sg = ksmbd_init_sg(iov, nvec, sign);
	if (!sg) {
		pr_err("Failed to init sg\n");
		rc = -ENOMEM;
		goto free_req;
	}

	iv_len = crypto_aead_ivsize(tfm);
	iv = kzalloc(iv_len, GFP_KERNEL);
	if (!iv) {
		rc = -ENOMEM;
		goto free_sg;
	}

	if (conn->cipher_type == SMB2_ENCRYPTION_AES128_GCM ||
	    conn->cipher_type == SMB2_ENCRYPTION_AES256_GCM) {
		memcpy(iv, (char *)tr_hdr->Nonce, SMB3_AES_GCM_NONCE);
	} else {
		iv[0] = 3;
		memcpy(iv + 1, (char *)tr_hdr->Nonce, SMB3_AES_CCM_NONCE);
	}

	aead_request_set_crypt(req, sg, sg, crypt_len, iv);
	aead_request_set_ad(req, assoc_data_len);
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);

	if (enc)
		rc = crypto_aead_encrypt(req);
	else
		rc = crypto_aead_decrypt(req);
	if (rc)
		goto free_iv;

	if (enc)
		memcpy(&tr_hdr->Signature, sign, SMB2_SIGNATURE_SIZE);

free_iv:
	kfree(iv);
free_sg:
	kfree(sg);
free_req:
	kfree(req);
free_ctx:
	ksmbd_release_crypto_ctx(ctx);
	return rc;
}
