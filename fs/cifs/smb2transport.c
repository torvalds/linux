/*
 *   fs/cifs/smb2transport.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002, 2011
 *                 Etersoft, 2012
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org) 2006
 *              Pavel Shilovsky (pshilovsky@samba.org) 2012
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
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/net.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <linux/mempool.h>
#include <linux/highmem.h>
#include "smb2pdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "smb2proto.h"
#include "cifs_debug.h"
#include "smb2status.h"
#include "smb2glob.h"

static int
smb2_crypto_shash_allocate(struct TCP_Server_Info *server)
{
	int rc;
	unsigned int size;

	if (server->secmech.sdeschmacsha256 != NULL)
		return 0; /* already allocated */

	server->secmech.hmacsha256 = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(server->secmech.hmacsha256)) {
		cifs_dbg(VFS, "could not allocate crypto hmacsha256\n");
		rc = PTR_ERR(server->secmech.hmacsha256);
		server->secmech.hmacsha256 = NULL;
		return rc;
	}

	size = sizeof(struct shash_desc) +
			crypto_shash_descsize(server->secmech.hmacsha256);
	server->secmech.sdeschmacsha256 = kmalloc(size, GFP_KERNEL);
	if (!server->secmech.sdeschmacsha256) {
		crypto_free_shash(server->secmech.hmacsha256);
		server->secmech.hmacsha256 = NULL;
		return -ENOMEM;
	}
	server->secmech.sdeschmacsha256->shash.tfm = server->secmech.hmacsha256;
	server->secmech.sdeschmacsha256->shash.flags = 0x0;

	return 0;
}

static int
smb3_crypto_shash_allocate(struct TCP_Server_Info *server)
{
	unsigned int size;
	int rc;

	if (server->secmech.sdesccmacaes != NULL)
		return 0;  /* already allocated */

	rc = smb2_crypto_shash_allocate(server);
	if (rc)
		return rc;

	server->secmech.cmacaes = crypto_alloc_shash("cmac(aes)", 0, 0);
	if (IS_ERR(server->secmech.cmacaes)) {
		cifs_dbg(VFS, "could not allocate crypto cmac-aes");
		kfree(server->secmech.sdeschmacsha256);
		server->secmech.sdeschmacsha256 = NULL;
		crypto_free_shash(server->secmech.hmacsha256);
		server->secmech.hmacsha256 = NULL;
		rc = PTR_ERR(server->secmech.cmacaes);
		server->secmech.cmacaes = NULL;
		return rc;
	}

	size = sizeof(struct shash_desc) +
			crypto_shash_descsize(server->secmech.cmacaes);
	server->secmech.sdesccmacaes = kmalloc(size, GFP_KERNEL);
	if (!server->secmech.sdesccmacaes) {
		cifs_dbg(VFS, "%s: Can't alloc cmacaes\n", __func__);
		kfree(server->secmech.sdeschmacsha256);
		server->secmech.sdeschmacsha256 = NULL;
		crypto_free_shash(server->secmech.hmacsha256);
		crypto_free_shash(server->secmech.cmacaes);
		server->secmech.hmacsha256 = NULL;
		server->secmech.cmacaes = NULL;
		return -ENOMEM;
	}
	server->secmech.sdesccmacaes->shash.tfm = server->secmech.cmacaes;
	server->secmech.sdesccmacaes->shash.flags = 0x0;

	return 0;
}


int
smb2_calc_signature(struct smb_rqst *rqst, struct TCP_Server_Info *server)
{
	int i, rc;
	unsigned char smb2_signature[SMB2_HMACSHA256_SIZE];
	unsigned char *sigptr = smb2_signature;
	struct kvec *iov = rqst->rq_iov;
	int n_vec = rqst->rq_nvec;
	struct smb2_hdr *smb2_pdu = (struct smb2_hdr *)iov[0].iov_base;

	memset(smb2_signature, 0x0, SMB2_HMACSHA256_SIZE);
	memset(smb2_pdu->Signature, 0x0, SMB2_SIGNATURE_SIZE);

	rc = smb2_crypto_shash_allocate(server);
	if (rc) {
		cifs_dbg(VFS, "%s: shah256 alloc failed\n", __func__);
		return rc;
	}

	rc = crypto_shash_setkey(server->secmech.hmacsha256,
		server->session_key.response, SMB2_NTLMV2_SESSKEY_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with response\n", __func__);
		return rc;
	}

	rc = crypto_shash_init(&server->secmech.sdeschmacsha256->shash);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init sha256", __func__);
		return rc;
	}

	for (i = 0; i < n_vec; i++) {
		if (iov[i].iov_len == 0)
			continue;
		if (iov[i].iov_base == NULL) {
			cifs_dbg(VFS, "null iovec entry\n");
			return -EIO;
		}
		/*
		 * The first entry includes a length field (which does not get
		 * signed that occupies the first 4 bytes before the header).
		 */
		if (i == 0) {
			if (iov[0].iov_len <= 8) /* cmd field at offset 9 */
				break; /* nothing to sign or corrupt header */
			rc =
			crypto_shash_update(
				&server->secmech.sdeschmacsha256->shash,
				iov[i].iov_base + 4, iov[i].iov_len - 4);
		} else {
			rc =
			crypto_shash_update(
				&server->secmech.sdeschmacsha256->shash,
				iov[i].iov_base, iov[i].iov_len);
		}
		if (rc) {
			cifs_dbg(VFS, "%s: Could not update with payload\n",
				 __func__);
			return rc;
		}
	}

	/* now hash over the rq_pages array */
	for (i = 0; i < rqst->rq_npages; i++) {
		struct kvec p_iov;

		cifs_rqst_page_to_kvec(rqst, i, &p_iov);
		crypto_shash_update(&server->secmech.sdeschmacsha256->shash,
					p_iov.iov_base, p_iov.iov_len);
		kunmap(rqst->rq_pages[i]);
	}

	rc = crypto_shash_final(&server->secmech.sdeschmacsha256->shash,
				sigptr);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate sha256 hash\n", __func__);

	memcpy(smb2_pdu->Signature, sigptr, SMB2_SIGNATURE_SIZE);

	return rc;
}

void
generate_smb3signingkey(struct TCP_Server_Info *server)
{
	unsigned char zero = 0x0;
	__u8 i[4] = {0, 0, 0, 1};
	__u8 L[4] = {0, 0, 0, 128};
	int rc = 0;
	unsigned char prfhash[SMB2_HMACSHA256_SIZE];
	unsigned char *hashptr = prfhash;

	memset(prfhash, 0x0, SMB2_HMACSHA256_SIZE);
	memset(server->smb3signingkey, 0x0, SMB3_SIGNKEY_SIZE);

	rc = smb3_crypto_shash_allocate(server);
	if (rc) {
		cifs_dbg(VFS, "%s: crypto alloc failed\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_setkey(server->secmech.hmacsha256,
		server->session_key.response, SMB2_NTLMV2_SESSKEY_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not set with session key\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_init(&server->secmech.sdeschmacsha256->shash);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init sign hmac\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(&server->secmech.sdeschmacsha256->shash,
				i, 4);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with n\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(&server->secmech.sdeschmacsha256->shash,
				"SMB2AESCMAC", 12);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with label\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(&server->secmech.sdeschmacsha256->shash,
				&zero, 1);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with zero\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(&server->secmech.sdeschmacsha256->shash,
				"SmbSign", 8);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with context\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_update(&server->secmech.sdeschmacsha256->shash,
				L, 4);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not update with L\n", __func__);
		goto smb3signkey_ret;
	}

	rc = crypto_shash_final(&server->secmech.sdeschmacsha256->shash,
				hashptr);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not generate sha256 hash\n", __func__);
		goto smb3signkey_ret;
	}

	memcpy(server->smb3signingkey, hashptr, SMB3_SIGNKEY_SIZE);

smb3signkey_ret:
	return;
}

int
smb3_calc_signature(struct smb_rqst *rqst, struct TCP_Server_Info *server)
{
	int i, rc;
	unsigned char smb3_signature[SMB2_CMACAES_SIZE];
	unsigned char *sigptr = smb3_signature;
	struct kvec *iov = rqst->rq_iov;
	int n_vec = rqst->rq_nvec;
	struct smb2_hdr *smb2_pdu = (struct smb2_hdr *)iov[0].iov_base;

	memset(smb3_signature, 0x0, SMB2_CMACAES_SIZE);
	memset(smb2_pdu->Signature, 0x0, SMB2_SIGNATURE_SIZE);

	rc = crypto_shash_setkey(server->secmech.cmacaes,
		server->smb3signingkey, SMB2_CMACAES_SIZE);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not set key for cmac aes\n", __func__);
		return rc;
	}

	/*
	 * we already allocate sdesccmacaes when we init smb3 signing key,
	 * so unlike smb2 case we do not have to check here if secmech are
	 * initialized
	 */
	rc = crypto_shash_init(&server->secmech.sdesccmacaes->shash);
	if (rc) {
		cifs_dbg(VFS, "%s: Could not init cmac aes\n", __func__);
		return rc;
	}

	for (i = 0; i < n_vec; i++) {
		if (iov[i].iov_len == 0)
			continue;
		if (iov[i].iov_base == NULL) {
			cifs_dbg(VFS, "null iovec entry");
			return -EIO;
		}
		/*
		 * The first entry includes a length field (which does not get
		 * signed that occupies the first 4 bytes before the header).
		 */
		if (i == 0) {
			if (iov[0].iov_len <= 8) /* cmd field at offset 9 */
				break; /* nothing to sign or corrupt header */
			rc =
			crypto_shash_update(
				&server->secmech.sdesccmacaes->shash,
				iov[i].iov_base + 4, iov[i].iov_len - 4);
		} else {
			rc =
			crypto_shash_update(
				&server->secmech.sdesccmacaes->shash,
				iov[i].iov_base, iov[i].iov_len);
		}
		if (rc) {
			cifs_dbg(VFS, "%s: Couldn't update cmac aes with payload\n",
							__func__);
			return rc;
		}
	}

	/* now hash over the rq_pages array */
	for (i = 0; i < rqst->rq_npages; i++) {
		struct kvec p_iov;

		cifs_rqst_page_to_kvec(rqst, i, &p_iov);
		crypto_shash_update(&server->secmech.sdesccmacaes->shash,
					p_iov.iov_base, p_iov.iov_len);
		kunmap(rqst->rq_pages[i]);
	}

	rc = crypto_shash_final(&server->secmech.sdesccmacaes->shash,
						sigptr);
	if (rc)
		cifs_dbg(VFS, "%s: Could not generate cmac aes\n", __func__);

	memcpy(smb2_pdu->Signature, sigptr, SMB2_SIGNATURE_SIZE);

	return rc;
}

/* must be called with server->srv_mutex held */
static int
smb2_sign_rqst(struct smb_rqst *rqst, struct TCP_Server_Info *server)
{
	int rc = 0;
	struct smb2_hdr *smb2_pdu = rqst->rq_iov[0].iov_base;

	if (!(smb2_pdu->Flags & SMB2_FLAGS_SIGNED) ||
	    server->tcpStatus == CifsNeedNegotiate)
		return rc;

	if (!server->session_estab) {
		strncpy(smb2_pdu->Signature, "BSRSPYL", 8);
		return rc;
	}

	rc = server->ops->calc_signature(rqst, server);

	return rc;
}

int
smb2_verify_signature(struct smb_rqst *rqst, struct TCP_Server_Info *server)
{
	unsigned int rc;
	char server_response_sig[16];
	struct smb2_hdr *smb2_pdu = (struct smb2_hdr *)rqst->rq_iov[0].iov_base;

	if ((smb2_pdu->Command == SMB2_NEGOTIATE) ||
	    (smb2_pdu->Command == SMB2_OPLOCK_BREAK) ||
	    (!server->session_estab))
		return 0;

	/*
	 * BB what if signatures are supposed to be on for session but
	 * server does not send one? BB
	 */

	/* Do not need to verify session setups with signature "BSRSPYL " */
	if (memcmp(smb2_pdu->Signature, "BSRSPYL ", 8) == 0)
		cifs_dbg(FYI, "dummy signature received for smb command 0x%x\n",
			 smb2_pdu->Command);

	/*
	 * Save off the origiginal signature so we can modify the smb and check
	 * our calculated signature against what the server sent.
	 */
	memcpy(server_response_sig, smb2_pdu->Signature, SMB2_SIGNATURE_SIZE);

	memset(smb2_pdu->Signature, 0, SMB2_SIGNATURE_SIZE);

	mutex_lock(&server->srv_mutex);
	rc = server->ops->calc_signature(rqst, server);
	mutex_unlock(&server->srv_mutex);

	if (rc)
		return rc;

	if (memcmp(server_response_sig, smb2_pdu->Signature,
		   SMB2_SIGNATURE_SIZE))
		return -EACCES;
	else
		return 0;
}

/*
 * Set message id for the request. Should be called after wait_for_free_request
 * and when srv_mutex is held.
 */
static inline void
smb2_seq_num_into_buf(struct TCP_Server_Info *server, struct smb2_hdr *hdr)
{
	hdr->MessageId = get_next_mid(server);
}

static struct mid_q_entry *
smb2_mid_entry_alloc(const struct smb2_hdr *smb_buffer,
		     struct TCP_Server_Info *server)
{
	struct mid_q_entry *temp;

	if (server == NULL) {
		cifs_dbg(VFS, "Null TCP session in smb2_mid_entry_alloc\n");
		return NULL;
	}

	temp = mempool_alloc(cifs_mid_poolp, GFP_NOFS);
	if (temp == NULL)
		return temp;
	else {
		memset(temp, 0, sizeof(struct mid_q_entry));
		temp->mid = smb_buffer->MessageId;	/* always LE */
		temp->pid = current->pid;
		temp->command = smb_buffer->Command;	/* Always LE */
		temp->when_alloc = jiffies;
		temp->server = server;

		/*
		 * The default is for the mid to be synchronous, so the
		 * default callback just wakes up the current task.
		 */
		temp->callback = cifs_wake_up_task;
		temp->callback_data = current;
	}

	atomic_inc(&midCount);
	temp->mid_state = MID_REQUEST_ALLOCATED;
	return temp;
}

static int
smb2_get_mid_entry(struct cifs_ses *ses, struct smb2_hdr *buf,
		   struct mid_q_entry **mid)
{
	if (ses->server->tcpStatus == CifsExiting)
		return -ENOENT;

	if (ses->server->tcpStatus == CifsNeedReconnect) {
		cifs_dbg(FYI, "tcp session dead - return to caller to retry\n");
		return -EAGAIN;
	}

	if (ses->status != CifsGood) {
		/* check if SMB2 session is bad because we are setting it up */
		if ((buf->Command != SMB2_SESSION_SETUP) &&
		    (buf->Command != SMB2_NEGOTIATE))
			return -EAGAIN;
		/* else ok - we are setting up session */
	}
	*mid = smb2_mid_entry_alloc(buf, ses->server);
	if (*mid == NULL)
		return -ENOMEM;
	spin_lock(&GlobalMid_Lock);
	list_add_tail(&(*mid)->qhead, &ses->server->pending_mid_q);
	spin_unlock(&GlobalMid_Lock);
	return 0;
}

int
smb2_check_receive(struct mid_q_entry *mid, struct TCP_Server_Info *server,
		   bool log_error)
{
	unsigned int len = get_rfc1002_length(mid->resp_buf);
	struct kvec iov;
	struct smb_rqst rqst = { .rq_iov = &iov,
				 .rq_nvec = 1 };

	iov.iov_base = (char *)mid->resp_buf;
	iov.iov_len = get_rfc1002_length(mid->resp_buf) + 4;

	dump_smb(mid->resp_buf, min_t(u32, 80, len));
	/* convert the length into a more usable form */
	if (len > 24 && server->sign) {
		int rc;

		rc = smb2_verify_signature(&rqst, server);
		if (rc)
			cifs_dbg(VFS, "SMB signature verification returned error = %d\n",
				 rc);
	}

	return map_smb2_to_linux_error(mid->resp_buf, log_error);
}

struct mid_q_entry *
smb2_setup_request(struct cifs_ses *ses, struct smb_rqst *rqst)
{
	int rc;
	struct smb2_hdr *hdr = (struct smb2_hdr *)rqst->rq_iov[0].iov_base;
	struct mid_q_entry *mid;

	smb2_seq_num_into_buf(ses->server, hdr);

	rc = smb2_get_mid_entry(ses, hdr, &mid);
	if (rc)
		return ERR_PTR(rc);
	rc = smb2_sign_rqst(rqst, ses->server);
	if (rc) {
		cifs_delete_mid(mid);
		return ERR_PTR(rc);
	}
	return mid;
}

struct mid_q_entry *
smb2_setup_async_request(struct TCP_Server_Info *server, struct smb_rqst *rqst)
{
	int rc;
	struct smb2_hdr *hdr = (struct smb2_hdr *)rqst->rq_iov[0].iov_base;
	struct mid_q_entry *mid;

	smb2_seq_num_into_buf(server, hdr);

	mid = smb2_mid_entry_alloc(hdr, server);
	if (mid == NULL)
		return ERR_PTR(-ENOMEM);

	rc = smb2_sign_rqst(rqst, server);
	if (rc) {
		DeleteMidQEntry(mid);
		return ERR_PTR(rc);
	}

	return mid;
}
