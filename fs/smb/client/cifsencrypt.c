// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Encryption and hashing operations relating to NTLM, NTLMv2.  See MS-NLMP
 *   for more detailed information
 *
 *   Copyright (C) International Business Machines  Corp., 2005,2013
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "cifsproto.h"
#include "ntlmssp.h"
#include <linux/ctype.h>
#include <linux/random.h>
#include <linux/highmem.h>
#include <linux/fips.h>
#include <linux/iov_iter.h>
#include <crypto/aead.h>
#include <crypto/arc4.h>
#include <crypto/md5.h>
#include <crypto/sha2.h>

static int cifs_sig_update(struct cifs_calc_sig_ctx *ctx,
			   const u8 *data, size_t len)
{
	if (ctx->md5) {
		md5_update(ctx->md5, data, len);
		return 0;
	}
	if (ctx->hmac) {
		hmac_sha256_update(ctx->hmac, data, len);
		return 0;
	}
	return crypto_shash_update(ctx->shash, data, len);
}

static int cifs_sig_final(struct cifs_calc_sig_ctx *ctx, u8 *out)
{
	if (ctx->md5) {
		md5_final(ctx->md5, out);
		return 0;
	}
	if (ctx->hmac) {
		hmac_sha256_final(ctx->hmac, out);
		return 0;
	}
	return crypto_shash_final(ctx->shash, out);
}

static size_t cifs_sig_step(void *iter_base, size_t progress, size_t len,
			    void *priv, void *priv2)
{
	struct cifs_calc_sig_ctx *ctx = priv;
	int ret, *pret = priv2;

	ret = cifs_sig_update(ctx, iter_base, len);
	if (ret < 0) {
		*pret = ret;
		return len;
	}
	return 0;
}

/*
 * Pass the data from an iterator into a hash.
 */
static int cifs_sig_iter(const struct iov_iter *iter, size_t maxsize,
			 struct cifs_calc_sig_ctx *ctx)
{
	struct iov_iter tmp_iter = *iter;
	int err = -EIO;

	if (iterate_and_advance_kernel(&tmp_iter, maxsize, ctx, &err,
				       cifs_sig_step) != maxsize)
		return err;
	return 0;
}

int __cifs_calc_signature(struct smb_rqst *rqst, struct TCP_Server_Info *server,
			  char *signature, struct cifs_calc_sig_ctx *ctx)
{
	int i;
	ssize_t rc;
	struct kvec *iov = rqst->rq_iov;
	int n_vec = rqst->rq_nvec;

	/* iov[0] is actual data and not the rfc1002 length for SMB2+ */
	if (!is_smb1(server)) {
		if (iov[0].iov_len <= 4)
			return -EIO;
		i = 0;
	} else {
		if (n_vec < 2 || iov[0].iov_len != 4)
			return -EIO;
		i = 1; /* skip rfc1002 length */
	}

	for (; i < n_vec; i++) {
		if (iov[i].iov_len == 0)
			continue;
		if (iov[i].iov_base == NULL) {
			cifs_dbg(VFS, "null iovec entry\n");
			return -EIO;
		}

		rc = cifs_sig_update(ctx, iov[i].iov_base, iov[i].iov_len);
		if (rc) {
			cifs_dbg(VFS, "%s: Could not update with payload\n",
				 __func__);
			return rc;
		}
	}

	rc = cifs_sig_iter(&rqst->rq_iter, iov_iter_count(&rqst->rq_iter), ctx);
	if (rc < 0)
		return rc;

	rc = cifs_sig_final(ctx, signature);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate hash\n", __func__);

	return rc;
}

/*
 * Calculate and return the CIFS signature based on the mac key and SMB PDU.
 * The 16 byte signature must be allocated by the caller. Note we only use the
 * 1st eight bytes and that the smb header signature field on input contains
 * the sequence number before this function is called. Also, this function
 * should be called with the server->srv_mutex held.
 */
static int cifs_calc_signature(struct smb_rqst *rqst,
			struct TCP_Server_Info *server, char *signature)
{
	struct md5_ctx ctx;

	if (!rqst->rq_iov || !signature || !server)
		return -EINVAL;
	if (fips_enabled) {
		cifs_dbg(VFS,
			 "MD5 signature support is disabled due to FIPS\n");
		return -EOPNOTSUPP;
	}

	md5_init(&ctx);
	md5_update(&ctx, server->session_key.response, server->session_key.len);

	return __cifs_calc_signature(
		rqst, server, signature,
		&(struct cifs_calc_sig_ctx){ .md5 = &ctx });
}

/* must be called with server->srv_mutex held */
int cifs_sign_rqst(struct smb_rqst *rqst, struct TCP_Server_Info *server,
		   __u32 *pexpected_response_sequence_number)
{
	int rc = 0;
	char smb_signature[20];
	struct smb_hdr *cifs_pdu = (struct smb_hdr *)rqst->rq_iov[0].iov_base;

	if (rqst->rq_iov[0].iov_len != 4 ||
	    rqst->rq_iov[0].iov_base + 4 != rqst->rq_iov[1].iov_base)
		return -EIO;

	if ((cifs_pdu == NULL) || (server == NULL))
		return -EINVAL;

	spin_lock(&server->srv_lock);
	if (!(cifs_pdu->Flags2 & SMBFLG2_SECURITY_SIGNATURE) ||
	    server->tcpStatus == CifsNeedNegotiate) {
		spin_unlock(&server->srv_lock);
		return rc;
	}
	spin_unlock(&server->srv_lock);

	if (!server->session_estab) {
		memcpy(cifs_pdu->Signature.SecuritySignature, "BSRSPYL", 8);
		return rc;
	}

	cifs_pdu->Signature.Sequence.SequenceNumber =
				cpu_to_le32(server->sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	*pexpected_response_sequence_number = ++server->sequence_number;
	++server->sequence_number;

	rc = cifs_calc_signature(rqst, server, smb_signature);
	if (rc)
		memset(cifs_pdu->Signature.SecuritySignature, 0, 8);
	else
		memcpy(cifs_pdu->Signature.SecuritySignature, smb_signature, 8);

	return rc;
}

int cifs_sign_smbv(struct kvec *iov, int n_vec, struct TCP_Server_Info *server,
		   __u32 *pexpected_response_sequence)
{
	struct smb_rqst rqst = { .rq_iov = iov,
				 .rq_nvec = n_vec };

	return cifs_sign_rqst(&rqst, server, pexpected_response_sequence);
}

/* must be called with server->srv_mutex held */
int cifs_sign_smb(struct smb_hdr *cifs_pdu, struct TCP_Server_Info *server,
		  __u32 *pexpected_response_sequence_number)
{
	struct kvec iov[2];

	iov[0].iov_base = cifs_pdu;
	iov[0].iov_len = 4;
	iov[1].iov_base = (char *)cifs_pdu + 4;
	iov[1].iov_len = be32_to_cpu(cifs_pdu->smb_buf_length);

	return cifs_sign_smbv(iov, 2, server,
			      pexpected_response_sequence_number);
}

int cifs_verify_signature(struct smb_rqst *rqst,
			  struct TCP_Server_Info *server,
			  __u32 expected_sequence_number)
{
	unsigned int rc;
	char server_response_sig[8];
	char what_we_think_sig_should_be[20];
	struct smb_hdr *cifs_pdu = (struct smb_hdr *)rqst->rq_iov[0].iov_base;

	if (rqst->rq_iov[0].iov_len != 4 ||
	    rqst->rq_iov[0].iov_base + 4 != rqst->rq_iov[1].iov_base)
		return -EIO;

	if (cifs_pdu == NULL || server == NULL)
		return -EINVAL;

	if (!server->session_estab)
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
		cifs_dbg(FYI, "dummy signature received for smb command 0x%x\n",
			 cifs_pdu->Command);

	/* save off the original signature so we can modify the smb and check
		its signature against what the server sent */
	memcpy(server_response_sig, cifs_pdu->Signature.SecuritySignature, 8);

	cifs_pdu->Signature.Sequence.SequenceNumber =
					cpu_to_le32(expected_sequence_number);
	cifs_pdu->Signature.Sequence.Reserved = 0;

	cifs_server_lock(server);
	rc = cifs_calc_signature(rqst, server, what_we_think_sig_should_be);
	cifs_server_unlock(server);

	if (rc)
		return rc;

/*	cifs_dump_mem("what we think it should be: ",
		      what_we_think_sig_should_be, 16); */

	if (memcmp(server_response_sig, what_we_think_sig_should_be, 8))
		return -EACCES;
	else
		return 0;

}

/* Build a proper attribute value/target info pairs blob.
 * Fill in netbios and dns domain name and workstation name
 * and client time (total five av pairs and + one end of fields indicator.
 * Allocate domain name which gets freed when session struct is deallocated.
 */
static int
build_avpair_blob(struct cifs_ses *ses, const struct nls_table *nls_cp)
{
	unsigned int dlen;
	unsigned int size = 2 * sizeof(struct ntlmssp2_name);
	char *defdmname = "WORKGROUP";
	unsigned char *blobptr;
	struct ntlmssp2_name *attrptr;

	if (!ses->domainName) {
		ses->domainName = kstrdup(defdmname, GFP_KERNEL);
		if (!ses->domainName)
			return -ENOMEM;
	}

	dlen = strlen(ses->domainName);

	/*
	 * The length of this blob is two times the size of a
	 * structure (av pair) which holds name/size
	 * ( for NTLMSSP_AV_NB_DOMAIN_NAME followed by NTLMSSP_AV_EOL ) +
	 * unicode length of a netbios domain name
	 */
	kfree_sensitive(ses->auth_key.response);
	ses->auth_key.len = size + 2 * dlen;
	ses->auth_key.response = kzalloc(ses->auth_key.len, GFP_KERNEL);
	if (!ses->auth_key.response) {
		ses->auth_key.len = 0;
		return -ENOMEM;
	}

	blobptr = ses->auth_key.response;
	attrptr = (struct ntlmssp2_name *) blobptr;

	/*
	 * As defined in MS-NTLM 3.3.2, just this av pair field
	 * is sufficient as part of the temp
	 */
	attrptr->type = cpu_to_le16(NTLMSSP_AV_NB_DOMAIN_NAME);
	attrptr->length = cpu_to_le16(2 * dlen);
	blobptr = (unsigned char *)attrptr + sizeof(struct ntlmssp2_name);
	cifs_strtoUTF16((__le16 *)blobptr, ses->domainName, dlen, nls_cp);

	return 0;
}

#define AV_TYPE(av)		(le16_to_cpu(av->type))
#define AV_LEN(av)		(le16_to_cpu(av->length))
#define AV_DATA_PTR(av)	((void *)av->data)

#define av_for_each_entry(ses, av) \
	for (av = NULL; (av = find_next_av(ses, av));)

static struct ntlmssp2_name *find_next_av(struct cifs_ses *ses,
					  struct ntlmssp2_name *av)
{
	u16 len;
	u8 *end;

	end = (u8 *)ses->auth_key.response + ses->auth_key.len;
	if (!av) {
		if (unlikely(!ses->auth_key.response || !ses->auth_key.len))
			return NULL;
		av = (void *)ses->auth_key.response;
	} else {
		av = (void *)((u8 *)av + sizeof(*av) + AV_LEN(av));
	}

	if ((u8 *)av + sizeof(*av) > end)
		return NULL;

	len = AV_LEN(av);
	if (AV_TYPE(av) == NTLMSSP_AV_EOL)
		return NULL;
	if ((u8 *)av + sizeof(*av) + len > end)
		return NULL;
	return av;
}

/*
 * Check if server has provided av pair of @type in the NTLMSSP
 * CHALLENGE_MESSAGE blob.
 */
static int find_av_name(struct cifs_ses *ses, u16 type, char **name, u16 maxlen)
{
	const struct nls_table *nlsc = ses->local_nls;
	struct ntlmssp2_name *av;
	u16 len, nlen;

	if (*name)
		return 0;

	av_for_each_entry(ses, av) {
		len = AV_LEN(av);
		if (AV_TYPE(av) != type || !len)
			continue;
		if (!IS_ALIGNED(len, sizeof(__le16))) {
			cifs_dbg(VFS | ONCE, "%s: bad length(%u) for type %u\n",
				 __func__, len, type);
			continue;
		}
		nlen = len / sizeof(__le16);
		if (nlen <= maxlen) {
			++nlen;
			*name = kmalloc(nlen, GFP_KERNEL);
			if (!*name)
				return -ENOMEM;
			cifs_from_utf16(*name, AV_DATA_PTR(av), nlen,
					len, nlsc, NO_MAP_UNI_RSVD);
			break;
		}
	}
	return 0;
}

/* Server has provided av pairs/target info in the type 2 challenge
 * packet and we have plucked it and stored within smb session.
 * We parse that blob here to find the server given timestamp
 * as part of ntlmv2 authentication (or local current time as
 * default in case of failure)
 */
static __le64 find_timestamp(struct cifs_ses *ses)
{
	struct ntlmssp2_name *av;
	struct timespec64 ts;

	av_for_each_entry(ses, av) {
		if (AV_TYPE(av) == NTLMSSP_AV_TIMESTAMP &&
		    AV_LEN(av) == sizeof(u64))
			return *((__le64 *)AV_DATA_PTR(av));
	}
	ktime_get_real_ts64(&ts);
	return cpu_to_le64(cifs_UnixTimeToNT(ts));
}

static int calc_ntlmv2_hash(struct cifs_ses *ses, char *ntlmv2_hash,
			    const struct nls_table *nls_cp)
{
	int len;
	char nt_hash[CIFS_NTHASH_SIZE];
	struct hmac_md5_ctx hmac_ctx;
	__le16 *user;
	wchar_t *domain;
	wchar_t *server;

	/* calculate md4 hash of password */
	E_md4hash(ses->password, nt_hash, nls_cp);

	hmac_md5_init_usingrawkey(&hmac_ctx, nt_hash, CIFS_NTHASH_SIZE);

	/* convert ses->user_name to unicode */
	len = ses->user_name ? strlen(ses->user_name) : 0;
	user = kmalloc(2 + (len * 2), GFP_KERNEL);
	if (user == NULL)
		return -ENOMEM;

	if (len) {
		len = cifs_strtoUTF16(user, ses->user_name, len, nls_cp);
		UniStrupr(user);
	} else {
		*(u16 *)user = 0;
	}

	hmac_md5_update(&hmac_ctx, (const u8 *)user, 2 * len);
	kfree(user);

	/* convert ses->domainName to unicode and uppercase */
	if (ses->domainName) {
		len = strlen(ses->domainName);

		domain = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (domain == NULL)
			return -ENOMEM;

		len = cifs_strtoUTF16((__le16 *)domain, ses->domainName, len,
				      nls_cp);
		hmac_md5_update(&hmac_ctx, (const u8 *)domain, 2 * len);
		kfree(domain);
	} else {
		/* We use ses->ip_addr if no domain name available */
		len = strlen(ses->ip_addr);

		server = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (server == NULL)
			return -ENOMEM;

		len = cifs_strtoUTF16((__le16 *)server, ses->ip_addr, len, nls_cp);
		hmac_md5_update(&hmac_ctx, (const u8 *)server, 2 * len);
		kfree(server);
	}

	hmac_md5_final(&hmac_ctx, ntlmv2_hash);
	return 0;
}

static void CalcNTLMv2_response(const struct cifs_ses *ses, char *ntlmv2_hash)
{
	struct ntlmv2_resp *ntlmv2 = (struct ntlmv2_resp *)
	    (ses->auth_key.response + CIFS_SESS_KEY_SIZE);
	unsigned int hash_len;

	/* The MD5 hash starts at challenge_key.key */
	hash_len = ses->auth_key.len - (CIFS_SESS_KEY_SIZE +
		offsetof(struct ntlmv2_resp, challenge.key[0]));

	if (ses->server->negflavor == CIFS_NEGFLAVOR_EXTENDED)
		memcpy(ntlmv2->challenge.key, ses->ntlmssp->cryptkey, CIFS_SERVER_CHALLENGE_SIZE);
	else
		memcpy(ntlmv2->challenge.key, ses->server->cryptkey, CIFS_SERVER_CHALLENGE_SIZE);

	/* Note that the HMAC-MD5 value overwrites ntlmv2->challenge.key */
	hmac_md5_usingrawkey(ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE,
			     ntlmv2->challenge.key, hash_len,
			     ntlmv2->ntlmv2_hash);
}

/*
 * Set up NTLMv2 response blob with SPN (cifs/<hostname>) appended to the
 * existing list of AV pairs.
 */
static int set_auth_key_response(struct cifs_ses *ses)
{
	size_t baselen = CIFS_SESS_KEY_SIZE + sizeof(struct ntlmv2_resp);
	size_t len, spnlen, tilen = 0, num_avs = 2 /* SPN + EOL */;
	struct TCP_Server_Info *server = ses->server;
	char *spn __free(kfree) = NULL;
	struct ntlmssp2_name *av;
	char *rsp = NULL;
	int rc;

	spnlen = strlen(server->hostname);
	len = sizeof("cifs/") + spnlen;
	spn = kmalloc(len, GFP_KERNEL);
	if (!spn) {
		rc = -ENOMEM;
		goto out;
	}

	spnlen = scnprintf(spn, len, "cifs/%.*s",
			   (int)spnlen, server->hostname);

	av_for_each_entry(ses, av)
		tilen += sizeof(*av) + AV_LEN(av);

	len = baselen + tilen + spnlen * sizeof(__le16) + num_avs * sizeof(*av);
	rsp = kmalloc(len, GFP_KERNEL);
	if (!rsp) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(rsp + baselen, ses->auth_key.response, tilen);
	av = (void *)(rsp + baselen + tilen);
	av->type = cpu_to_le16(NTLMSSP_AV_TARGET_NAME);
	av->length = cpu_to_le16(spnlen * sizeof(__le16));
	cifs_strtoUTF16((__le16 *)av->data, spn, spnlen, ses->local_nls);
	av = (void *)((__u8 *)av + sizeof(*av) + AV_LEN(av));
	av->type = cpu_to_le16(NTLMSSP_AV_EOL);
	av->length = 0;

	rc = 0;
	ses->auth_key.len = len;
out:
	ses->auth_key.response = rsp;
	return rc;
}

int
setup_ntlmv2_rsp(struct cifs_ses *ses, const struct nls_table *nls_cp)
{
	unsigned char *tiblob = NULL; /* target info blob */
	struct ntlmv2_resp *ntlmv2;
	char ntlmv2_hash[16];
	__le64 rsp_timestamp;
	__u64 cc;
	int rc;

	if (nls_cp == NULL) {
		cifs_dbg(VFS, "%s called with nls_cp==NULL\n", __func__);
		return -EINVAL;
	}

	if (ses->server->negflavor == CIFS_NEGFLAVOR_EXTENDED) {
		if (!ses->domainName) {
			if (ses->domainAuto) {
				/*
				 * Domain (workgroup) hasn't been specified in
				 * mount options, so try to find it in
				 * CHALLENGE_MESSAGE message and then use it as
				 * part of NTLMv2 authentication.
				 */
				rc = find_av_name(ses, NTLMSSP_AV_NB_DOMAIN_NAME,
						  &ses->domainName,
						  CIFS_MAX_DOMAINNAME_LEN);
				if (rc)
					goto setup_ntlmv2_rsp_ret;
			} else {
				ses->domainName = kstrdup("", GFP_KERNEL);
				if (!ses->domainName) {
					rc = -ENOMEM;
					goto setup_ntlmv2_rsp_ret;
				}
			}
		}
		rc = find_av_name(ses, NTLMSSP_AV_DNS_DOMAIN_NAME,
				  &ses->dns_dom, CIFS_MAX_DOMAINNAME_LEN);
		if (rc)
			goto setup_ntlmv2_rsp_ret;
	} else {
		rc = build_avpair_blob(ses, nls_cp);
		if (rc) {
			cifs_dbg(VFS, "error %d building av pair blob\n", rc);
			goto setup_ntlmv2_rsp_ret;
		}
	}

	/* Must be within 5 minutes of the server (or in range +/-2h
	 * in case of Mac OS X), so simply carry over server timestamp
	 * (as Windows 7 does)
	 */
	rsp_timestamp = find_timestamp(ses);
	get_random_bytes(&cc, sizeof(cc));

	cifs_server_lock(ses->server);

	tiblob = ses->auth_key.response;
	rc = set_auth_key_response(ses);
	if (rc) {
		ses->auth_key.len = 0;
		goto unlock;
	}

	ntlmv2 = (struct ntlmv2_resp *)
			(ses->auth_key.response + CIFS_SESS_KEY_SIZE);
	ntlmv2->blob_signature = cpu_to_le32(0x00000101);
	ntlmv2->reserved = 0;
	ntlmv2->time = rsp_timestamp;
	ntlmv2->client_chal = cc;
	ntlmv2->reserved2 = 0;

	if (fips_enabled) {
		cifs_dbg(VFS, "NTLMv2 support is disabled due to FIPS\n");
		rc = -EOPNOTSUPP;
		goto unlock;
	}

	/* calculate ntlmv2_hash */
	rc = calc_ntlmv2_hash(ses, ntlmv2_hash, nls_cp);
	if (rc) {
		cifs_dbg(VFS, "Could not get NTLMv2 hash, rc=%d\n", rc);
		goto unlock;
	}

	/* calculate first part of the client response (CR1) */
	CalcNTLMv2_response(ses, ntlmv2_hash);

	/* now calculate the session key for NTLMv2 */
	hmac_md5_usingrawkey(ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE,
			     ntlmv2->ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE,
			     ses->auth_key.response);
	rc = 0;
unlock:
	cifs_server_unlock(ses->server);
setup_ntlmv2_rsp_ret:
	kfree_sensitive(tiblob);

	return rc;
}

int
calc_seckey(struct cifs_ses *ses)
{
	unsigned char sec_key[CIFS_SESS_KEY_SIZE]; /* a nonce */
	struct arc4_ctx *ctx_arc4;

	if (fips_enabled)
		return -ENODEV;

	get_random_bytes(sec_key, CIFS_SESS_KEY_SIZE);

	ctx_arc4 = kmalloc(sizeof(*ctx_arc4), GFP_KERNEL);
	if (!ctx_arc4) {
		cifs_dbg(VFS, "Could not allocate arc4 context\n");
		return -ENOMEM;
	}

	arc4_setkey(ctx_arc4, ses->auth_key.response, CIFS_SESS_KEY_SIZE);
	arc4_crypt(ctx_arc4, ses->ntlmssp->ciphertext, sec_key,
		   CIFS_CPHTXT_SIZE);

	/* make secondary_key/nonce as session key */
	memcpy(ses->auth_key.response, sec_key, CIFS_SESS_KEY_SIZE);
	/* and make len as that of session key only */
	ses->auth_key.len = CIFS_SESS_KEY_SIZE;

	memzero_explicit(sec_key, CIFS_SESS_KEY_SIZE);
	kfree_sensitive(ctx_arc4);
	return 0;
}

void
cifs_crypto_secmech_release(struct TCP_Server_Info *server)
{
	cifs_free_hash(&server->secmech.aes_cmac);

	if (server->secmech.enc) {
		crypto_free_aead(server->secmech.enc);
		server->secmech.enc = NULL;
	}
	if (server->secmech.dec) {
		crypto_free_aead(server->secmech.dec);
		server->secmech.dec = NULL;
	}
}
