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
#include "../common/arc4.h"
#include <crypto/aead.h>

/*
 * Hash data from a BVEC-type iterator.
 */
static int cifs_shash_bvec(const struct iov_iter *iter, ssize_t maxsize,
			   struct shash_desc *shash)
{
	const struct bio_vec *bv = iter->bvec;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	void *p;
	int ret;

	for (i = 0; i < iter->nr_segs; i++) {
		size_t off, len;

		len = bv[i].bv_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		len = min_t(size_t, maxsize, len - start);
		off = bv[i].bv_offset + start;

		p = kmap_local_page(bv[i].bv_page);
		ret = crypto_shash_update(shash, p + off, len);
		kunmap_local(p);
		if (ret < 0)
			return ret;

		maxsize -= len;
		if (maxsize <= 0)
			break;
		start = 0;
	}

	return 0;
}

/*
 * Hash data from a KVEC-type iterator.
 */
static int cifs_shash_kvec(const struct iov_iter *iter, ssize_t maxsize,
			   struct shash_desc *shash)
{
	const struct kvec *kv = iter->kvec;
	unsigned long start = iter->iov_offset;
	unsigned int i;
	int ret;

	for (i = 0; i < iter->nr_segs; i++) {
		size_t len;

		len = kv[i].iov_len;
		if (start >= len) {
			start -= len;
			continue;
		}

		len = min_t(size_t, maxsize, len - start);
		ret = crypto_shash_update(shash, kv[i].iov_base + start, len);
		if (ret < 0)
			return ret;
		maxsize -= len;

		if (maxsize <= 0)
			break;
		start = 0;
	}

	return 0;
}

/*
 * Hash data from an XARRAY-type iterator.
 */
static ssize_t cifs_shash_xarray(const struct iov_iter *iter, ssize_t maxsize,
				 struct shash_desc *shash)
{
	struct folio *folios[16], *folio;
	unsigned int nr, i, j, npages;
	loff_t start = iter->xarray_start + iter->iov_offset;
	pgoff_t last, index = start / PAGE_SIZE;
	ssize_t ret = 0;
	size_t len, offset, foffset;
	void *p;

	if (maxsize == 0)
		return 0;

	last = (start + maxsize - 1) / PAGE_SIZE;
	do {
		nr = xa_extract(iter->xarray, (void **)folios, index, last,
				ARRAY_SIZE(folios), XA_PRESENT);
		if (nr == 0)
			return -EIO;

		for (i = 0; i < nr; i++) {
			folio = folios[i];
			npages = folio_nr_pages(folio);
			foffset = start - folio_pos(folio);
			offset = foffset % PAGE_SIZE;
			for (j = foffset / PAGE_SIZE; j < npages; j++) {
				len = min_t(size_t, maxsize, PAGE_SIZE - offset);
				p = kmap_local_page(folio_page(folio, j));
				ret = crypto_shash_update(shash, p + offset, len);
				kunmap_local(p);
				if (ret < 0)
					return ret;
				maxsize -= len;
				if (maxsize <= 0)
					return 0;
				start += len;
				offset = 0;
				index++;
			}
		}
	} while (nr == ARRAY_SIZE(folios));
	return 0;
}

/*
 * Pass the data from an iterator into a hash.
 */
static int cifs_shash_iter(const struct iov_iter *iter, size_t maxsize,
			   struct shash_desc *shash)
{
	if (maxsize == 0)
		return 0;

	switch (iov_iter_type(iter)) {
	case ITER_BVEC:
		return cifs_shash_bvec(iter, maxsize, shash);
	case ITER_KVEC:
		return cifs_shash_kvec(iter, maxsize, shash);
	case ITER_XARRAY:
		return cifs_shash_xarray(iter, maxsize, shash);
	default:
		pr_err("cifs_shash_iter(%u) unsupported\n", iov_iter_type(iter));
		WARN_ON_ONCE(1);
		return -EIO;
	}
}

int __cifs_calc_signature(struct smb_rqst *rqst,
			  struct TCP_Server_Info *server, char *signature,
			  struct shash_desc *shash)
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

		rc = crypto_shash_update(shash,
					 iov[i].iov_base, iov[i].iov_len);
		if (rc) {
			cifs_dbg(VFS, "%s: Could not update with payload\n",
				 __func__);
			return rc;
		}
	}

	rc = cifs_shash_iter(&rqst->rq_iter, iov_iter_count(&rqst->rq_iter), shash);
	if (rc < 0)
		return rc;

	rc = crypto_shash_final(shash, signature);
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
	int rc;

	if (!rqst->rq_iov || !signature || !server)
		return -EINVAL;

	rc = cifs_alloc_hash("md5", &server->secmech.md5);
	if (rc)
		return -1;

	rc = crypto_shash_init(server->secmech.md5);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init md5\n", __func__);
		return rc;
	}

	rc = crypto_shash_update(server->secmech.md5,
		server->session_key.response, server->session_key.len);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with response\n", __func__);
		return rc;
	}

	return __cifs_calc_signature(rqst, server, signature, server->secmech.md5);
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

	/* save off the origiginal signature so we can modify the smb and check
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
find_domain_name(struct cifs_ses *ses, const struct nls_table *nls_cp)
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
			if (!attrsize || attrsize >= CIFS_MAX_DOMAINNAME_LEN)
				break;
			if (!ses->domainName) {
				ses->domainName =
					kmalloc(attrsize + 1, GFP_KERNEL);
				if (!ses->domainName)
						return -ENOMEM;
				cifs_from_utf16(ses->domainName,
					(__le16 *)blobptr, attrsize, attrsize,
					nls_cp, NO_MAP_UNI_RSVD);
				break;
			}
		}
		blobptr += attrsize; /* advance attr  value */
	}

	return 0;
}

/* Server has provided av pairs/target info in the type 2 challenge
 * packet and we have plucked it and stored within smb session.
 * We parse that blob here to find the server given timestamp
 * as part of ntlmv2 authentication (or local current time as
 * default in case of failure)
 */
static __le64
find_timestamp(struct cifs_ses *ses)
{
	unsigned int attrsize;
	unsigned int type;
	unsigned int onesize = sizeof(struct ntlmssp2_name);
	unsigned char *blobptr;
	unsigned char *blobend;
	struct ntlmssp2_name *attrptr;
	struct timespec64 ts;

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
		if (type == NTLMSSP_AV_TIMESTAMP) {
			if (attrsize == sizeof(u64))
				return *((__le64 *)blobptr);
		}
		blobptr += attrsize; /* advance attr value */
	}

	ktime_get_real_ts64(&ts);
	return cpu_to_le64(cifs_UnixTimeToNT(ts));
}

static int calc_ntlmv2_hash(struct cifs_ses *ses, char *ntlmv2_hash,
			    const struct nls_table *nls_cp)
{
	int rc = 0;
	int len;
	char nt_hash[CIFS_NTHASH_SIZE];
	__le16 *user;
	wchar_t *domain;
	wchar_t *server;

	if (!ses->server->secmech.hmacmd5) {
		cifs_dbg(VFS, "%s: can't generate ntlmv2 hash\n", __func__);
		return -1;
	}

	/* calculate md4 hash of password */
	E_md4hash(ses->password, nt_hash, nls_cp);

	rc = crypto_shash_setkey(ses->server->secmech.hmacmd5->tfm, nt_hash,
				CIFS_NTHASH_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not set NT Hash as a key\n", __func__);
		return rc;
	}

	rc = crypto_shash_init(ses->server->secmech.hmacmd5);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init hmacmd5\n", __func__);
		return rc;
	}

	/* convert ses->user_name to unicode */
	len = ses->user_name ? strlen(ses->user_name) : 0;
	user = kmalloc(2 + (len * 2), GFP_KERNEL);
	if (user == NULL) {
		rc = -ENOMEM;
		return rc;
	}

	if (len) {
		len = cifs_strtoUTF16(user, ses->user_name, len, nls_cp);
		UniStrupr(user);
	} else {
		*(u16 *)user = 0;
	}

	rc = crypto_shash_update(ses->server->secmech.hmacmd5,
				(char *)user, 2 * len);
	kfree(user);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with user\n", __func__);
		return rc;
	}

	/* convert ses->domainName to unicode and uppercase */
	if (ses->domainName) {
		len = strlen(ses->domainName);

		domain = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (domain == NULL) {
			rc = -ENOMEM;
			return rc;
		}
		len = cifs_strtoUTF16((__le16 *)domain, ses->domainName, len,
				      nls_cp);
		rc =
		crypto_shash_update(ses->server->secmech.hmacmd5,
					(char *)domain, 2 * len);
		kfree(domain);
		if (rc) {
			cifs_dbg(VFS, "%s: Could not update with domain\n",
				 __func__);
			return rc;
		}
	} else {
		/* We use ses->ip_addr if no domain name available */
		len = strlen(ses->ip_addr);

		server = kmalloc(2 + (len * 2), GFP_KERNEL);
		if (server == NULL) {
			rc = -ENOMEM;
			return rc;
		}
		len = cifs_strtoUTF16((__le16 *)server, ses->ip_addr, len,
					nls_cp);
		rc =
		crypto_shash_update(ses->server->secmech.hmacmd5,
					(char *)server, 2 * len);
		kfree(server);
		if (rc) {
			cifs_dbg(VFS, "%s: Could not update with server\n",
				 __func__);
			return rc;
		}
	}

	rc = crypto_shash_final(ses->server->secmech.hmacmd5,
					ntlmv2_hash);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate md5 hash\n", __func__);

	return rc;
}

static int
CalcNTLMv2_response(const struct cifs_ses *ses, char *ntlmv2_hash)
{
	int rc;
	struct ntlmv2_resp *ntlmv2 = (struct ntlmv2_resp *)
	    (ses->auth_key.response + CIFS_SESS_KEY_SIZE);
	unsigned int hash_len;

	/* The MD5 hash starts at challenge_key.key */
	hash_len = ses->auth_key.len - (CIFS_SESS_KEY_SIZE +
		offsetof(struct ntlmv2_resp, challenge.key[0]));

	if (!ses->server->secmech.hmacmd5) {
		cifs_dbg(VFS, "%s: can't generate ntlmv2 hash\n", __func__);
		return -1;
	}

	rc = crypto_shash_setkey(ses->server->secmech.hmacmd5->tfm,
				 ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not set NTLMV2 Hash as a key\n",
			 __func__);
		return rc;
	}

	rc = crypto_shash_init(ses->server->secmech.hmacmd5);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init hmacmd5\n", __func__);
		return rc;
	}

	if (ses->server->negflavor == CIFS_NEGFLAVOR_EXTENDED)
		memcpy(ntlmv2->challenge.key,
		       ses->ntlmssp->cryptkey, CIFS_SERVER_CHALLENGE_SIZE);
	else
		memcpy(ntlmv2->challenge.key,
		       ses->server->cryptkey, CIFS_SERVER_CHALLENGE_SIZE);
	rc = crypto_shash_update(ses->server->secmech.hmacmd5,
				 ntlmv2->challenge.key, hash_len);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with response\n", __func__);
		return rc;
	}

	/* Note that the MD5 digest over writes anon.challenge_key.key */
	rc = crypto_shash_final(ses->server->secmech.hmacmd5,
				ntlmv2->ntlmv2_hash);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate md5 hash\n", __func__);

	return rc;
}

int
setup_ntlmv2_rsp(struct cifs_ses *ses, const struct nls_table *nls_cp)
{
	int rc;
	int baselen;
	unsigned int tilen;
	struct ntlmv2_resp *ntlmv2;
	char ntlmv2_hash[16];
	unsigned char *tiblob = NULL; /* target info blob */
	__le64 rsp_timestamp;

	if (nls_cp == NULL) {
		cifs_dbg(VFS, "%s called with nls_cp==NULL\n", __func__);
		return -EINVAL;
	}

	if (ses->server->negflavor == CIFS_NEGFLAVOR_EXTENDED) {
		if (!ses->domainName) {
			if (ses->domainAuto) {
				rc = find_domain_name(ses, nls_cp);
				if (rc) {
					cifs_dbg(VFS, "error %d finding domain name\n",
						 rc);
					goto setup_ntlmv2_rsp_ret;
				}
			} else {
				ses->domainName = kstrdup("", GFP_KERNEL);
			}
		}
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

	baselen = CIFS_SESS_KEY_SIZE + sizeof(struct ntlmv2_resp);
	tilen = ses->auth_key.len;
	tiblob = ses->auth_key.response;

	ses->auth_key.response = kmalloc(baselen + tilen, GFP_KERNEL);
	if (!ses->auth_key.response) {
		rc = -ENOMEM;
		ses->auth_key.len = 0;
		goto setup_ntlmv2_rsp_ret;
	}
	ses->auth_key.len += baselen;

	ntlmv2 = (struct ntlmv2_resp *)
			(ses->auth_key.response + CIFS_SESS_KEY_SIZE);
	ntlmv2->blob_signature = cpu_to_le32(0x00000101);
	ntlmv2->reserved = 0;
	ntlmv2->time = rsp_timestamp;

	get_random_bytes(&ntlmv2->client_chal, sizeof(ntlmv2->client_chal));
	ntlmv2->reserved2 = 0;

	memcpy(ses->auth_key.response + baselen, tiblob, tilen);

	cifs_server_lock(ses->server);

	rc = cifs_alloc_hash("hmac(md5)", &ses->server->secmech.hmacmd5);
	if (rc) {
		goto unlock;
	}

	/* calculate ntlmv2_hash */
	rc = calc_ntlmv2_hash(ses, ntlmv2_hash, nls_cp);
	if (rc) {
		cifs_dbg(VFS, "Could not get v2 hash rc %d\n", rc);
		goto unlock;
	}

	/* calculate first part of the client response (CR1) */
	rc = CalcNTLMv2_response(ses, ntlmv2_hash);
	if (rc) {
		cifs_dbg(VFS, "Could not calculate CR1 rc: %d\n", rc);
		goto unlock;
	}

	/* now calculate the session key for NTLMv2 */
	rc = crypto_shash_setkey(ses->server->secmech.hmacmd5->tfm,
		ntlmv2_hash, CIFS_HMAC_MD5_HASH_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not set NTLMV2 Hash as a key\n",
			 __func__);
		goto unlock;
	}

	rc = crypto_shash_init(ses->server->secmech.hmacmd5);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init hmacmd5\n", __func__);
		goto unlock;
	}

	rc = crypto_shash_update(ses->server->secmech.hmacmd5,
		ntlmv2->ntlmv2_hash,
		CIFS_HMAC_MD5_HASH_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with response\n", __func__);
		goto unlock;
	}

	rc = crypto_shash_final(ses->server->secmech.hmacmd5,
		ses->auth_key.response);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate md5 hash\n", __func__);

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

	cifs_arc4_setkey(ctx_arc4, ses->auth_key.response, CIFS_SESS_KEY_SIZE);
	cifs_arc4_crypt(ctx_arc4, ses->ntlmssp->ciphertext, sec_key,
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
	cifs_free_hash(&server->secmech.hmacsha256);
	cifs_free_hash(&server->secmech.md5);
	cifs_free_hash(&server->secmech.sha512);
	cifs_free_hash(&server->secmech.hmacmd5);

	if (server->secmech.enc) {
		crypto_free_aead(server->secmech.enc);
		server->secmech.enc = NULL;
	}

	if (server->secmech.dec) {
		crypto_free_aead(server->secmech.dec);
		server->secmech.dec = NULL;
	}
}
