/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/uio.h>
#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/cpuregs.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <opencrypto/cryptodev.h>

#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/cop2.h>
#include <mips/nlm/hal/fmn.h>
#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/nlmsaelib.h>
#include <mips/nlm/dev/sec/nlmseclib.h>

static int
nlm_crypto_complete_sec_request(struct xlp_sec_softc *sc,
    struct xlp_sec_command *cmd)
{
	unsigned int fbvc;
	struct nlm_fmn_msg m;
	int ret;

	fbvc = nlm_cpuid() / CMS_MAX_VCPU_VC;
	m.msg[0] = m.msg[1] = m.msg[2] = m.msg[3] = 0;

	m.msg[0] = nlm_crypto_form_pkt_fmn_entry0(fbvc, 0, 0,
	    cmd->ctrlp->cipherkeylen, vtophys(cmd->ctrlp));

	m.msg[1] = nlm_crypto_form_pkt_fmn_entry1(0, cmd->ctrlp->hashkeylen,
	    NLM_CRYPTO_PKT_DESC_SIZE(cmd->nsegs), vtophys(cmd->paramp));

	/* Software scratch pad */
	m.msg[2] = (uintptr_t)cmd;
	sc->sec_msgsz = 3;

	/* Send the message to sec/rsa engine vc */
	ret = nlm_fmn_msgsend(sc->sec_vc_start, sc->sec_msgsz,
	    FMN_SWCODE_CRYPTO, &m);
	if (ret != 0) {
#ifdef NLM_SEC_DEBUG
		printf("%s: msgsnd failed (%x)\n", __func__, ret);
#endif
		return (ERESTART);
	}
	return (0);
}

int
nlm_crypto_form_srcdst_segs(struct xlp_sec_command *cmd)
{
	unsigned int srcseg = 0, dstseg = 0;
	struct cryptodesc *cipdesc = NULL;
	struct cryptop *crp = NULL;

	crp = cmd->crp;
	cipdesc = cmd->enccrd;

	if (cipdesc != NULL) {
		/* IV is given as ONE segment to avoid copy */
		if (cipdesc->crd_flags & CRD_F_IV_EXPLICIT) {
			srcseg = nlm_crypto_fill_src_seg(cmd->paramp, srcseg,
			    cmd->iv, cmd->ivlen);
			dstseg = nlm_crypto_fill_dst_seg(cmd->paramp, dstseg,
			    cmd->iv, cmd->ivlen);
		}
	}

	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		struct mbuf *m = NULL;

		m  = (struct mbuf *)crp->crp_buf;
		while (m != NULL) {
			srcseg = nlm_crypto_fill_src_seg(cmd->paramp, srcseg,
			    mtod(m,caddr_t), m->m_len);
			if (cipdesc != NULL) {
				dstseg = nlm_crypto_fill_dst_seg(cmd->paramp,
				    dstseg, mtod(m,caddr_t), m->m_len);
			}
			m = m->m_next;
		}
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		struct uio *uio = NULL;
		struct iovec *iov = NULL;
	        int iol = 0;

		uio = (struct uio *)crp->crp_buf;
		iov = (struct iovec *)uio->uio_iov;
		iol = uio->uio_iovcnt;

		while (iol > 0) {
			srcseg = nlm_crypto_fill_src_seg(cmd->paramp, srcseg,
			    (caddr_t)iov->iov_base, iov->iov_len);
			if (cipdesc != NULL) {
				dstseg = nlm_crypto_fill_dst_seg(cmd->paramp,
				    dstseg, (caddr_t)iov->iov_base,
				    iov->iov_len);
			}
			iov++;
			iol--;
		}
	} else {
		srcseg = nlm_crypto_fill_src_seg(cmd->paramp, srcseg,
		    ((caddr_t)crp->crp_buf), crp->crp_ilen);
		if (cipdesc != NULL) {
			dstseg = nlm_crypto_fill_dst_seg(cmd->paramp, dstseg,
			    ((caddr_t)crp->crp_buf), crp->crp_ilen);
		}
	}
	return (0);
}

int
nlm_crypto_do_cipher(struct xlp_sec_softc *sc, struct xlp_sec_command *cmd)
{
	struct cryptodesc *cipdesc = NULL;
	unsigned char *cipkey = NULL;
	int ret = 0;

	cipdesc = cmd->enccrd;
	cipkey = (unsigned char *)cipdesc->crd_key;
	if (cmd->cipheralg == NLM_CIPHER_3DES) {
		if (!(cipdesc->crd_flags & CRD_F_ENCRYPT)) {
                        uint64_t *k, *tkey;
			k = (uint64_t *)cipdesc->crd_key;
			tkey = (uint64_t *)cmd->des3key;
			tkey[2] = k[0];
			tkey[1] = k[1];
			tkey[0] = k[2];
			cipkey = (unsigned char *)tkey;
		}
	}
	nlm_crypto_fill_pkt_ctrl(cmd->ctrlp, 0, NLM_HASH_BYPASS, 0,
	    cmd->cipheralg, cmd->ciphermode, cipkey,
	    (cipdesc->crd_klen >> 3), NULL, 0);

	nlm_crypto_fill_cipher_pkt_param(cmd->ctrlp, cmd->paramp,
	    (cipdesc->crd_flags & CRD_F_ENCRYPT) ? 1 : 0, cmd->ivoff,
	    cmd->ivlen, cmd->cipheroff, cmd->cipherlen);
	nlm_crypto_form_srcdst_segs(cmd);

	ret = nlm_crypto_complete_sec_request(sc, cmd);
	return (ret);
}

int
nlm_crypto_do_digest(struct xlp_sec_softc *sc, struct xlp_sec_command *cmd)
{
	struct cryptodesc *digdesc = NULL;
	int ret=0;

	digdesc = cmd->maccrd;

	nlm_crypto_fill_pkt_ctrl(cmd->ctrlp, (digdesc->crd_klen) ? 1 : 0,
	    cmd->hashalg, cmd->hashmode, NLM_CIPHER_BYPASS, 0,
	    NULL, 0, digdesc->crd_key, digdesc->crd_klen >> 3);

	nlm_crypto_fill_auth_pkt_param(cmd->ctrlp, cmd->paramp,
	    cmd->hashoff, cmd->hashlen, cmd->hmacpad,
	    (unsigned char *)cmd->hashdest);

	nlm_crypto_form_srcdst_segs(cmd);

	ret = nlm_crypto_complete_sec_request(sc, cmd);

	return (ret);
}

int
nlm_crypto_do_cipher_digest(struct xlp_sec_softc *sc,
    struct xlp_sec_command *cmd)
{
	struct cryptodesc *cipdesc=NULL, *digdesc=NULL;
	unsigned char *cipkey = NULL;
	int ret=0;

	cipdesc = cmd->enccrd;
	digdesc = cmd->maccrd;

	cipkey = (unsigned char *)cipdesc->crd_key;
	if (cmd->cipheralg == NLM_CIPHER_3DES) {
		if (!(cipdesc->crd_flags & CRD_F_ENCRYPT)) {
			uint64_t *k, *tkey;
			k = (uint64_t *)cipdesc->crd_key;
			tkey = (uint64_t *)cmd->des3key;
			tkey[2] = k[0];
			tkey[1] = k[1];
			tkey[0] = k[2];
			cipkey = (unsigned char *)tkey;
		}
	}
	nlm_crypto_fill_pkt_ctrl(cmd->ctrlp, (digdesc->crd_klen) ? 1 : 0,
	    cmd->hashalg, cmd->hashmode, cmd->cipheralg, cmd->ciphermode,
	    cipkey, (cipdesc->crd_klen >> 3),
	    digdesc->crd_key, (digdesc->crd_klen >> 3));

	nlm_crypto_fill_cipher_auth_pkt_param(cmd->ctrlp, cmd->paramp,
	    (cipdesc->crd_flags & CRD_F_ENCRYPT) ? 1 : 0, cmd->hashsrc,
	    cmd->ivoff, cmd->ivlen, cmd->hashoff, cmd->hashlen,
	    cmd->hmacpad, cmd->cipheroff, cmd->cipherlen,
	    (unsigned char *)cmd->hashdest);

	nlm_crypto_form_srcdst_segs(cmd);

	ret = nlm_crypto_complete_sec_request(sc, cmd);
	return (ret);
}

int
nlm_get_digest_param(struct xlp_sec_command *cmd)
{
	switch(cmd->maccrd->crd_alg) {
	case CRYPTO_MD5:
		cmd->hashalg  = NLM_HASH_MD5;
		cmd->hashmode = NLM_HASH_MODE_SHA1;
		break;
	case CRYPTO_SHA1:
		cmd->hashalg  = NLM_HASH_SHA;
		cmd->hashmode = NLM_HASH_MODE_SHA1;
		break;
	case CRYPTO_MD5_HMAC:
		cmd->hashalg  = NLM_HASH_MD5;
		cmd->hashmode = NLM_HASH_MODE_SHA1;
		break;
	case CRYPTO_SHA1_HMAC:
		cmd->hashalg  = NLM_HASH_SHA;
		cmd->hashmode = NLM_HASH_MODE_SHA1;
		break;
	default:
		/* Not supported */
		return (-1);
	}
	return (0);
}
int
nlm_get_cipher_param(struct xlp_sec_command *cmd)
{
	switch(cmd->enccrd->crd_alg) {
	case CRYPTO_DES_CBC:
		cmd->cipheralg  = NLM_CIPHER_DES;
		cmd->ciphermode = NLM_CIPHER_MODE_CBC;
		cmd->ivlen	= XLP_SEC_DES_IV_LENGTH;
		break;
	case CRYPTO_3DES_CBC:
		cmd->cipheralg  = NLM_CIPHER_3DES;
		cmd->ciphermode = NLM_CIPHER_MODE_CBC;
		cmd->ivlen	= XLP_SEC_DES_IV_LENGTH;
		break;
	case CRYPTO_AES_CBC:
		cmd->cipheralg  = NLM_CIPHER_AES128;
		cmd->ciphermode = NLM_CIPHER_MODE_CBC;
		cmd->ivlen	= XLP_SEC_AES_IV_LENGTH;
		break;
	case CRYPTO_ARC4:
		cmd->cipheralg  = NLM_CIPHER_ARC4;
		cmd->ciphermode = NLM_CIPHER_MODE_ECB;
		cmd->ivlen	= XLP_SEC_ARC4_IV_LENGTH;
		break;
	default:
		/* Not supported */
		return (-1);
	}
	return (0);
}
