/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2000-2001, Boris Popov
 * All rights reserved.
 *
 * Copyright (c) 2003, 2004 Tim J. Robbins.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/mchain.h>
#include <sys/md4.h>
#include <sys/md5.h>
#include <sys/iconv.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_rq.h>
#include <netsmb/smb_dev.h>

#include <crypto/des/des.h>

#include "opt_netsmb.h"

static u_char N8[] = {0x4b, 0x47, 0x53, 0x21, 0x40, 0x23, 0x24, 0x25};


static void
smb_E(const u_char *key, u_char *data, u_char *dest)
{
	des_key_schedule *ksp;
	u_char kk[8];

	kk[0] = key[0] & 0xfe;
	kk[1] = key[0] << 7 | (key[1] >> 1 & 0xfe);
	kk[2] = key[1] << 6 | (key[2] >> 2 & 0xfe);
	kk[3] = key[2] << 5 | (key[3] >> 3 & 0xfe);
	kk[4] = key[3] << 4 | (key[4] >> 4 & 0xfe);
	kk[5] = key[4] << 3 | (key[5] >> 5 & 0xfe);
	kk[6] = key[5] << 2 | (key[6] >> 6 & 0xfe);
	kk[7] = key[6] << 1;
	ksp = malloc(sizeof(des_key_schedule), M_SMBTEMP, M_WAITOK);
	des_set_key((des_cblock *)kk, *ksp);
	des_ecb_encrypt((des_cblock *)data, (des_cblock *)dest, *ksp, 1);
	free(ksp, M_SMBTEMP);
}


int
smb_encrypt(const u_char *apwd, u_char *C8, u_char *RN)
{
	u_char *p, *P14, *S21;

	p = malloc(14 + 21, M_SMBTEMP, M_WAITOK);
	bzero(p, 14 + 21);
	P14 = p;
	S21 = p + 14;
	bcopy(apwd, P14, min(14, strlen(apwd)));
	/*
	 * S21 = concat(Ex(P14, N8), zeros(5));
	 */
	smb_E(P14, N8, S21);
	smb_E(P14 + 7, N8, S21 + 8);

	smb_E(S21, C8, RN);
	smb_E(S21 + 7, C8, RN + 8);
	smb_E(S21 + 14, C8, RN + 16);
	free(p, M_SMBTEMP);
	return 0;
}

int
smb_ntencrypt(const u_char *apwd, u_char *C8, u_char *RN)
{
	u_char S21[21];
	u_int16_t *unipwd;
	MD4_CTX *ctxp;
	u_int len;

	len = strlen(apwd);
	unipwd = malloc((len + 1) * sizeof(u_int16_t), M_SMBTEMP, M_WAITOK);
	/*
	 * S21 = concat(MD4(U(apwd)), zeros(5));
	 */
	smb_strtouni(unipwd, apwd);
	ctxp = malloc(sizeof(MD4_CTX), M_SMBTEMP, M_WAITOK);
	MD4Init(ctxp);
	MD4Update(ctxp, (u_char*)unipwd, len * sizeof(u_int16_t));
	free(unipwd, M_SMBTEMP);
	bzero(S21, 21);
	MD4Final(S21, ctxp);
	free(ctxp, M_SMBTEMP);

	smb_E(S21, C8, RN);
	smb_E(S21 + 7, C8, RN + 8);
	smb_E(S21 + 14, C8, RN + 16);
	return 0;
}

/*
 * Calculate message authentication code (MAC) key for virtual circuit.
 */
int
smb_calcmackey(struct smb_vc *vcp)
{
	const char *pwd;
	u_int16_t *unipwd;
	u_int len;
	MD4_CTX md4;
	u_char S16[16], S21[21];

	KASSERT(vcp->vc_hflags2 & SMB_FLAGS2_SECURITY_SIGNATURE,
	    ("signatures not enabled"));

	if (vcp->vc_mackey != NULL) {
		free(vcp->vc_mackey, M_SMBTEMP);
		vcp->vc_mackey = NULL;
		vcp->vc_mackeylen = 0;
		vcp->vc_seqno = 0;
	}

	/*
	 * The partial MAC key is the concatenation of the 16 byte session
	 * key and the 24 byte challenge response.
	 */
	vcp->vc_mackeylen = 16 + 24;
	vcp->vc_mackey = malloc(vcp->vc_mackeylen, M_SMBTEMP, M_WAITOK);

	/*
	 * Calculate session key:
	 *	MD4(MD4(U(PN)))
	 */
	pwd = smb_vc_getpass(vcp);
	len = strlen(pwd);
	unipwd = malloc((len + 1) * sizeof(u_int16_t), M_SMBTEMP, M_WAITOK);
	smb_strtouni(unipwd, pwd);
	MD4Init(&md4);
	MD4Update(&md4, (u_char *)unipwd, len * sizeof(u_int16_t));
	MD4Final(S16, &md4);
	MD4Init(&md4);
	MD4Update(&md4, S16, 16);
	MD4Final(vcp->vc_mackey, &md4);
	free(unipwd, M_SMBTEMP);

	/*
	 * Calculate response to challenge:
	 *	Ex(concat(MD4(U(pass)), zeros(5)), C8)
	 */
	bzero(S21, 21);
	bcopy(S16, S21, 16);
	smb_E(S21, vcp->vc_ch, vcp->vc_mackey + 16);
	smb_E(S21 + 7, vcp->vc_ch, vcp->vc_mackey + 24);
	smb_E(S21 + 14, vcp->vc_ch, vcp->vc_mackey + 32);

	return (0);
}

/*
 * Sign request with MAC.
 */
int
smb_rq_sign(struct smb_rq *rqp)
{
	struct smb_vc *vcp = rqp->sr_vc;
	struct mbchain *mbp;
	struct mbuf *mb;
	MD5_CTX md5;
	u_char digest[16];

	KASSERT(vcp->vc_hflags2 & SMB_FLAGS2_SECURITY_SIGNATURE,
	    ("signatures not enabled"));

	if (vcp->vc_mackey == NULL)
		/* XXX Should assert that cmd == SMB_COM_NEGOTIATE. */
		return (0);

	/*
	 * This is a bit of a kludge. If the request is non-TRANSACTION,
	 * or it is the first request of a transaction, give it the next
	 * sequence number, and expect the reply to have the sequence number
	 * following that one. Otherwise, it is a secondary request in
	 * a transaction, and it gets the same sequence numbers as the
	 * primary request.
	 */
	if (rqp->sr_t2 == NULL ||
	    (rqp->sr_t2->t2_flags & SMBT2_SECONDARY) == 0) {
		rqp->sr_seqno = vcp->vc_seqno++;
		rqp->sr_rseqno = vcp->vc_seqno++;
	} else {
		/*
		 * Sequence numbers are already in the struct because
		 * smb_t2_request_int() uses the same one for all the
		 * requests in the transaction.
		 * (At least we hope so.)
		 */
		KASSERT(rqp->sr_t2 == NULL ||
		    (rqp->sr_t2->t2_flags & SMBT2_SECONDARY) == 0 ||
		    rqp->sr_t2->t2_rq == rqp,
		    ("sec t2 rq not using same smb_rq"));
	}

	/* Initialize sec. signature field to sequence number + zeros. */
	le32enc(rqp->sr_rqsig, rqp->sr_seqno);
	le32enc(rqp->sr_rqsig + 4, 0);

	/*
	 * Compute HMAC-MD5 of packet data, keyed by MAC key.
	 * Store the first 8 bytes in the sec. signature field.
	 */
	smb_rq_getrequest(rqp, &mbp);
	MD5Init(&md5);
	MD5Update(&md5, vcp->vc_mackey, vcp->vc_mackeylen);
	for (mb = mbp->mb_top; mb != NULL; mb = mb->m_next)
		MD5Update(&md5, mtod(mb, void *), mb->m_len);
	MD5Final(digest, &md5);
	bcopy(digest, rqp->sr_rqsig, 8);

	return (0);
}

/*
 * Verify reply signature.
 */
int
smb_rq_verify(struct smb_rq *rqp)
{
	struct smb_vc *vcp = rqp->sr_vc;
	struct mdchain *mdp;
	u_char sigbuf[8];
	MD5_CTX md5;
	u_char digest[16];
	struct mbuf *mb;

	KASSERT(vcp->vc_hflags2 & SMB_FLAGS2_SECURITY_SIGNATURE,
	    ("signatures not enabled"));

	if (vcp->vc_mackey == NULL)
		/* XXX Should check that this is a SMB_COM_NEGOTIATE reply. */
		return (0);

	/*
	 * Compute HMAC-MD5 of packet data, keyed by MAC key.
	 * We play games to pretend the security signature field
	 * contains their sequence number, to avoid modifying
	 * the packet itself.
	 */
	smb_rq_getreply(rqp, &mdp);
	mb = mdp->md_top;
	KASSERT(mb->m_len >= SMB_HDRLEN, ("forgot to m_pullup"));
	MD5Init(&md5);
	MD5Update(&md5, vcp->vc_mackey, vcp->vc_mackeylen);
	MD5Update(&md5, mtod(mb, void *), 14);
	*(u_int32_t *)sigbuf = htole32(rqp->sr_rseqno);
	*(u_int32_t *)(sigbuf + 4) = 0;
	MD5Update(&md5, sigbuf, 8);
	MD5Update(&md5, mtod(mb, u_char *) + 22, mb->m_len - 22);
	for (mb = mb->m_next; mb != NULL; mb = mb->m_next)
		MD5Update(&md5, mtod(mb, void *), mb->m_len);
	MD5Final(digest, &md5);

	/*
	 * Now verify the signature.
	 */
	if (bcmp(mtod(mdp->md_top, u_char *) + 14, digest, 8) != 0)
		return (EAUTH);

	return (0);
}
