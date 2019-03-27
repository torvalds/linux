/*-
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/sglist.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>

#include "cryptodev_if.h"

#include "common/common.h"
#include "crypto/t4_crypto.h"

/*
 * Requests consist of:
 *
 * +-------------------------------+
 * | struct fw_crypto_lookaside_wr |
 * +-------------------------------+
 * | struct ulp_txpkt              |
 * +-------------------------------+
 * | struct ulptx_idata            |
 * +-------------------------------+
 * | struct cpl_tx_sec_pdu         |
 * +-------------------------------+
 * | struct cpl_tls_tx_scmd_fmt    |
 * +-------------------------------+
 * | key context header            |
 * +-------------------------------+
 * | AES key                       |  ----- For requests with AES
 * +-------------------------------+
 * | Hash state                    |  ----- For hash-only requests
 * +-------------------------------+ -
 * | IPAD (16-byte aligned)        |  \
 * +-------------------------------+  +---- For requests with HMAC
 * | OPAD (16-byte aligned)        |  /
 * +-------------------------------+ -
 * | GMAC H                        |  ----- For AES-GCM
 * +-------------------------------+ -
 * | struct cpl_rx_phys_dsgl       |  \
 * +-------------------------------+  +---- Destination buffer for
 * | PHYS_DSGL entries             |  /     non-hash-only requests
 * +-------------------------------+ -
 * | 16 dummy bytes                |  ----- Only for HMAC/hash-only requests
 * +-------------------------------+
 * | IV                            |  ----- If immediate IV
 * +-------------------------------+
 * | Payload                       |  ----- If immediate Payload
 * +-------------------------------+ -
 * | struct ulptx_sgl              |  \
 * +-------------------------------+  +---- If payload via SGL
 * | SGL entries                   |  /
 * +-------------------------------+ -
 *
 * Note that the key context must be padded to ensure 16-byte alignment.
 * For HMAC requests, the key consists of the partial hash of the IPAD
 * followed by the partial hash of the OPAD.
 *
 * Replies consist of:
 *
 * +-------------------------------+
 * | struct cpl_fw6_pld            |
 * +-------------------------------+
 * | hash digest                   |  ----- For HMAC request with
 * +-------------------------------+        'hash_size' set in work request
 *
 * A 32-bit big-endian error status word is supplied in the last 4
 * bytes of data[0] in the CPL_FW6_PLD message.  bit 0 indicates a
 * "MAC" error and bit 1 indicates a "PAD" error.
 *
 * The 64-bit 'cookie' field from the fw_crypto_lookaside_wr message
 * in the request is returned in data[1] of the CPL_FW6_PLD message.
 *
 * For block cipher replies, the updated IV is supplied in data[2] and
 * data[3] of the CPL_FW6_PLD message.
 *
 * For hash replies where the work request set 'hash_size' to request
 * a copy of the hash in the reply, the hash digest is supplied
 * immediately following the CPL_FW6_PLD message.
 */

/*
 * The crypto engine supports a maximum AAD size of 511 bytes.
 */
#define	MAX_AAD_LEN		511

/*
 * The documentation for CPL_RX_PHYS_DSGL claims a maximum of 32 SG
 * entries.  While the CPL includes a 16-bit length field, the T6 can
 * sometimes hang if an error occurs while processing a request with a
 * single DSGL entry larger than 2k.
 */
#define	MAX_RX_PHYS_DSGL_SGE	32
#define	DSGL_SGE_MAXLEN		2048

/*
 * The adapter only supports requests with a total input or output
 * length of 64k-1 or smaller.  Longer requests either result in hung
 * requests or incorrect results.
 */
#define	MAX_REQUEST_SIZE	65535

static MALLOC_DEFINE(M_CCR, "ccr", "Chelsio T6 crypto");

struct ccr_session_hmac {
	struct auth_hash *auth_hash;
	int hash_len;
	unsigned int partial_digest_len;
	unsigned int auth_mode;
	unsigned int mk_size;
	char ipad[CHCR_HASH_MAX_BLOCK_SIZE_128];
	char opad[CHCR_HASH_MAX_BLOCK_SIZE_128];
};

struct ccr_session_gmac {
	int hash_len;
	char ghash_h[GMAC_BLOCK_LEN];
};

struct ccr_session_blkcipher {
	unsigned int cipher_mode;
	unsigned int key_len;
	unsigned int iv_len;
	__be32 key_ctx_hdr;
	char enckey[CHCR_AES_MAX_KEY_LEN];
	char deckey[CHCR_AES_MAX_KEY_LEN];
};

struct ccr_session {
	bool active;
	int pending;
	enum { HASH, HMAC, BLKCIPHER, AUTHENC, GCM } mode;
	union {
		struct ccr_session_hmac hmac;
		struct ccr_session_gmac gmac;
	};
	struct ccr_session_blkcipher blkcipher;
};

struct ccr_softc {
	struct adapter *adapter;
	device_t dev;
	uint32_t cid;
	int tx_channel_id;
	struct mtx lock;
	bool detaching;
	struct sge_wrq *txq;
	struct sge_rxq *rxq;

	/*
	 * Pre-allocate S/G lists used when preparing a work request.
	 * 'sg_crp' contains an sglist describing the entire buffer
	 * for a 'struct cryptop'.  'sg_ulptx' is used to describe
	 * the data the engine should DMA as input via ULPTX_SGL.
	 * 'sg_dsgl' is used to describe the destination that cipher
	 * text and a tag should be written to.
	 */
	struct sglist *sg_crp;
	struct sglist *sg_ulptx;
	struct sglist *sg_dsgl;

	/*
	 * Pre-allocate a dummy output buffer for the IV and AAD for
	 * AEAD requests.
	 */
	char *iv_aad_buf;
	struct sglist *sg_iv_aad;

	/* Statistics. */
	uint64_t stats_blkcipher_encrypt;
	uint64_t stats_blkcipher_decrypt;
	uint64_t stats_hash;
	uint64_t stats_hmac;
	uint64_t stats_authenc_encrypt;
	uint64_t stats_authenc_decrypt;
	uint64_t stats_gcm_encrypt;
	uint64_t stats_gcm_decrypt;
	uint64_t stats_wr_nomem;
	uint64_t stats_inflight;
	uint64_t stats_mac_error;
	uint64_t stats_pad_error;
	uint64_t stats_bad_session;
	uint64_t stats_sglist_error;
	uint64_t stats_process_error;
	uint64_t stats_sw_fallback;
};

/*
 * Crypto requests involve two kind of scatter/gather lists.
 *
 * Non-hash-only requests require a PHYS_DSGL that describes the
 * location to store the results of the encryption or decryption
 * operation.  This SGL uses a different format (PHYS_DSGL) and should
 * exclude the crd_skip bytes at the start of the data as well as
 * any AAD or IV.  For authenticated encryption requests it should
 * cover include the destination of the hash or tag.
 *
 * The input payload may either be supplied inline as immediate data,
 * or via a standard ULP_TX SGL.  This SGL should include AAD,
 * ciphertext, and the hash or tag for authenticated decryption
 * requests.
 *
 * These scatter/gather lists can describe different subsets of the
 * buffer described by the crypto operation.  ccr_populate_sglist()
 * generates a scatter/gather list that covers the entire crypto
 * operation buffer that is then used to construct the other
 * scatter/gather lists.
 */
static int
ccr_populate_sglist(struct sglist *sg, struct cryptop *crp)
{
	int error;

	sglist_reset(sg);
	if (crp->crp_flags & CRYPTO_F_IMBUF)
		error = sglist_append_mbuf(sg, (struct mbuf *)crp->crp_buf);
	else if (crp->crp_flags & CRYPTO_F_IOV)
		error = sglist_append_uio(sg, (struct uio *)crp->crp_buf);
	else
		error = sglist_append(sg, crp->crp_buf, crp->crp_ilen);
	return (error);
}

/*
 * Segments in 'sg' larger than 'maxsegsize' are counted as multiple
 * segments.
 */
static int
ccr_count_sgl(struct sglist *sg, int maxsegsize)
{
	int i, nsegs;

	nsegs = 0;
	for (i = 0; i < sg->sg_nseg; i++)
		nsegs += howmany(sg->sg_segs[i].ss_len, maxsegsize);
	return (nsegs);
}

/* These functions deal with PHYS_DSGL for the reply buffer. */
static inline int
ccr_phys_dsgl_len(int nsegs)
{
	int len;

	len = (nsegs / 8) * sizeof(struct phys_sge_pairs);
	if ((nsegs % 8) != 0) {
		len += sizeof(uint16_t) * 8;
		len += roundup2(nsegs % 8, 2) * sizeof(uint64_t);
	}
	return (len);
}

static void
ccr_write_phys_dsgl(struct ccr_softc *sc, void *dst, int nsegs)
{
	struct sglist *sg;
	struct cpl_rx_phys_dsgl *cpl;
	struct phys_sge_pairs *sgl;
	vm_paddr_t paddr;
	size_t seglen;
	u_int i, j;

	sg = sc->sg_dsgl;
	cpl = dst;
	cpl->op_to_tid = htobe32(V_CPL_RX_PHYS_DSGL_OPCODE(CPL_RX_PHYS_DSGL) |
	    V_CPL_RX_PHYS_DSGL_ISRDMA(0));
	cpl->pcirlxorder_to_noofsgentr = htobe32(
	    V_CPL_RX_PHYS_DSGL_PCIRLXORDER(0) |
	    V_CPL_RX_PHYS_DSGL_PCINOSNOOP(0) |
	    V_CPL_RX_PHYS_DSGL_PCITPHNTENB(0) | V_CPL_RX_PHYS_DSGL_DCAID(0) |
	    V_CPL_RX_PHYS_DSGL_NOOFSGENTR(nsegs));
	cpl->rss_hdr_int.opcode = CPL_RX_PHYS_ADDR;
	cpl->rss_hdr_int.qid = htobe16(sc->rxq->iq.abs_id);
	cpl->rss_hdr_int.hash_val = 0;
	sgl = (struct phys_sge_pairs *)(cpl + 1);
	j = 0;
	for (i = 0; i < sg->sg_nseg; i++) {
		seglen = sg->sg_segs[i].ss_len;
		paddr = sg->sg_segs[i].ss_paddr;
		do {
			sgl->addr[j] = htobe64(paddr);
			if (seglen > DSGL_SGE_MAXLEN) {
				sgl->len[j] = htobe16(DSGL_SGE_MAXLEN);
				paddr += DSGL_SGE_MAXLEN;
				seglen -= DSGL_SGE_MAXLEN;
			} else {
				sgl->len[j] = htobe16(seglen);
				seglen = 0;
			}
			j++;
			if (j == 8) {
				sgl++;
				j = 0;
			}
		} while (seglen != 0);
	}
	MPASS(j + 8 * (sgl - (struct phys_sge_pairs *)(cpl + 1)) == nsegs);
}

/* These functions deal with the ULPTX_SGL for input payload. */
static inline int
ccr_ulptx_sgl_len(int nsegs)
{
	u_int n;

	nsegs--; /* first segment is part of ulptx_sgl */
	n = sizeof(struct ulptx_sgl) + 8 * ((3 * nsegs) / 2 + (nsegs & 1));
	return (roundup2(n, 16));
}

static void
ccr_write_ulptx_sgl(struct ccr_softc *sc, void *dst, int nsegs)
{
	struct ulptx_sgl *usgl;
	struct sglist *sg;
	struct sglist_seg *ss;
	int i;

	sg = sc->sg_ulptx;
	MPASS(nsegs == sg->sg_nseg);
	ss = &sg->sg_segs[0];
	usgl = dst;
	usgl->cmd_nsge = htobe32(V_ULPTX_CMD(ULP_TX_SC_DSGL) |
	    V_ULPTX_NSGE(nsegs));
	usgl->len0 = htobe32(ss->ss_len);
	usgl->addr0 = htobe64(ss->ss_paddr);
	ss++;
	for (i = 0; i < sg->sg_nseg - 1; i++) {
		usgl->sge[i / 2].len[i & 1] = htobe32(ss->ss_len);
		usgl->sge[i / 2].addr[i & 1] = htobe64(ss->ss_paddr);
		ss++;
	}
	
}

static bool
ccr_use_imm_data(u_int transhdr_len, u_int input_len)
{

	if (input_len > CRYPTO_MAX_IMM_TX_PKT_LEN)
		return (false);
	if (roundup2(transhdr_len, 16) + roundup2(input_len, 16) >
	    SGE_MAX_WR_LEN)
		return (false);
	return (true);
}

static void
ccr_populate_wreq(struct ccr_softc *sc, struct chcr_wr *crwr, u_int kctx_len,
    u_int wr_len, u_int imm_len, u_int sgl_len, u_int hash_size,
    struct cryptop *crp)
{
	u_int cctx_size;

	cctx_size = sizeof(struct _key_ctx) + kctx_len;
	crwr->wreq.op_to_cctx_size = htobe32(
	    V_FW_CRYPTO_LOOKASIDE_WR_OPCODE(FW_CRYPTO_LOOKASIDE_WR) |
	    V_FW_CRYPTO_LOOKASIDE_WR_COMPL(0) |
	    V_FW_CRYPTO_LOOKASIDE_WR_IMM_LEN(imm_len) |
	    V_FW_CRYPTO_LOOKASIDE_WR_CCTX_LOC(1) |
	    V_FW_CRYPTO_LOOKASIDE_WR_CCTX_SIZE(cctx_size >> 4));
	crwr->wreq.len16_pkd = htobe32(
	    V_FW_CRYPTO_LOOKASIDE_WR_LEN16(wr_len / 16));
	crwr->wreq.session_id = 0;
	crwr->wreq.rx_chid_to_rx_q_id = htobe32(
	    V_FW_CRYPTO_LOOKASIDE_WR_RX_CHID(sc->tx_channel_id) |
	    V_FW_CRYPTO_LOOKASIDE_WR_LCB(0) |
	    V_FW_CRYPTO_LOOKASIDE_WR_PHASH(0) |
	    V_FW_CRYPTO_LOOKASIDE_WR_IV(IV_NOP) |
	    V_FW_CRYPTO_LOOKASIDE_WR_FQIDX(0) |
	    V_FW_CRYPTO_LOOKASIDE_WR_TX_CH(0) |
	    V_FW_CRYPTO_LOOKASIDE_WR_RX_Q_ID(sc->rxq->iq.abs_id));
	crwr->wreq.key_addr = 0;
	crwr->wreq.pld_size_hash_size = htobe32(
	    V_FW_CRYPTO_LOOKASIDE_WR_PLD_SIZE(sgl_len) |
	    V_FW_CRYPTO_LOOKASIDE_WR_HASH_SIZE(hash_size));
	crwr->wreq.cookie = htobe64((uintptr_t)crp);

	crwr->ulptx.cmd_dest = htobe32(V_ULPTX_CMD(ULP_TX_PKT) |
	    V_ULP_TXPKT_DATAMODIFY(0) |
	    V_ULP_TXPKT_CHANNELID(sc->tx_channel_id) | V_ULP_TXPKT_DEST(0) |
	    V_ULP_TXPKT_FID(0) | V_ULP_TXPKT_RO(1));
	crwr->ulptx.len = htobe32(
	    ((wr_len - sizeof(struct fw_crypto_lookaside_wr)) / 16));

	crwr->sc_imm.cmd_more = htobe32(V_ULPTX_CMD(ULP_TX_SC_IMM) |
	    V_ULP_TX_SC_MORE(imm_len != 0 ? 0 : 1));
	crwr->sc_imm.len = htobe32(wr_len - offsetof(struct chcr_wr, sec_cpl) -
	    sgl_len);
}

static int
ccr_hash(struct ccr_softc *sc, struct ccr_session *s, struct cryptop *crp)
{
	struct chcr_wr *crwr;
	struct wrqe *wr;
	struct auth_hash *axf;
	struct cryptodesc *crd;
	char *dst;
	u_int hash_size_in_response, kctx_flits, kctx_len, transhdr_len, wr_len;
	u_int hmac_ctrl, imm_len, iopad_size;
	int error, sgl_nsegs, sgl_len, use_opad;

	crd = crp->crp_desc;

	/* Reject requests with too large of an input buffer. */
	if (crd->crd_len > MAX_REQUEST_SIZE)
		return (EFBIG);

	axf = s->hmac.auth_hash;

	if (s->mode == HMAC) {
		use_opad = 1;
		hmac_ctrl = SCMD_HMAC_CTRL_NO_TRUNC;
	} else {
		use_opad = 0;
		hmac_ctrl = SCMD_HMAC_CTRL_NOP;
	}

	/* PADs must be 128-bit aligned. */
	iopad_size = roundup2(s->hmac.partial_digest_len, 16);

	/*
	 * The 'key' part of the context includes the aligned IPAD and
	 * OPAD.
	 */
	kctx_len = iopad_size;
	if (use_opad)
		kctx_len += iopad_size;
	hash_size_in_response = axf->hashsize;
	transhdr_len = HASH_TRANSHDR_SIZE(kctx_len);

	if (crd->crd_len == 0) {
		imm_len = axf->blocksize;
		sgl_nsegs = 0;
		sgl_len = 0;
	} else if (ccr_use_imm_data(transhdr_len, crd->crd_len)) {
		imm_len = crd->crd_len;
		sgl_nsegs = 0;
		sgl_len = 0;
	} else {
		imm_len = 0;
		sglist_reset(sc->sg_ulptx);
		error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
		    crd->crd_skip, crd->crd_len);
		if (error)
			return (error);
		sgl_nsegs = sc->sg_ulptx->sg_nseg;
		sgl_len = ccr_ulptx_sgl_len(sgl_nsegs);
	}

	wr_len = roundup2(transhdr_len, 16) + roundup2(imm_len, 16) + sgl_len;
	if (wr_len > SGE_MAX_WR_LEN)
		return (EFBIG);
	wr = alloc_wrqe(wr_len, sc->txq);
	if (wr == NULL) {
		sc->stats_wr_nomem++;
		return (ENOMEM);
	}
	crwr = wrtod(wr);
	memset(crwr, 0, wr_len);

	ccr_populate_wreq(sc, crwr, kctx_len, wr_len, imm_len, sgl_len,
	    hash_size_in_response, crp);

	/* XXX: Hardcodes SGE loopback channel of 0. */
	crwr->sec_cpl.op_ivinsrtofst = htobe32(
	    V_CPL_TX_SEC_PDU_OPCODE(CPL_TX_SEC_PDU) |
	    V_CPL_TX_SEC_PDU_RXCHID(sc->tx_channel_id) |
	    V_CPL_TX_SEC_PDU_ACKFOLLOWS(0) | V_CPL_TX_SEC_PDU_ULPTXLPBK(1) |
	    V_CPL_TX_SEC_PDU_CPLLEN(2) | V_CPL_TX_SEC_PDU_PLACEHOLDER(0) |
	    V_CPL_TX_SEC_PDU_IVINSRTOFST(0));

	crwr->sec_cpl.pldlen = htobe32(crd->crd_len == 0 ? axf->blocksize :
	    crd->crd_len);

	crwr->sec_cpl.cipherstop_lo_authinsert = htobe32(
	    V_CPL_TX_SEC_PDU_AUTHSTART(1) | V_CPL_TX_SEC_PDU_AUTHSTOP(0));

	/* These two flits are actually a CPL_TLS_TX_SCMD_FMT. */
	crwr->sec_cpl.seqno_numivs = htobe32(
	    V_SCMD_SEQ_NO_CTRL(0) |
	    V_SCMD_PROTO_VERSION(SCMD_PROTO_VERSION_GENERIC) |
	    V_SCMD_CIPH_MODE(SCMD_CIPH_MODE_NOP) |
	    V_SCMD_AUTH_MODE(s->hmac.auth_mode) |
	    V_SCMD_HMAC_CTRL(hmac_ctrl));
	crwr->sec_cpl.ivgen_hdrlen = htobe32(
	    V_SCMD_LAST_FRAG(0) |
	    V_SCMD_MORE_FRAGS(crd->crd_len == 0 ? 1 : 0) | V_SCMD_MAC_ONLY(1));

	memcpy(crwr->key_ctx.key, s->hmac.ipad, s->hmac.partial_digest_len);
	if (use_opad)
		memcpy(crwr->key_ctx.key + iopad_size, s->hmac.opad,
		    s->hmac.partial_digest_len);

	/* XXX: F_KEY_CONTEXT_SALT_PRESENT set, but 'salt' not set. */
	kctx_flits = (sizeof(struct _key_ctx) + kctx_len) / 16;
	crwr->key_ctx.ctx_hdr = htobe32(V_KEY_CONTEXT_CTX_LEN(kctx_flits) |
	    V_KEY_CONTEXT_OPAD_PRESENT(use_opad) |
	    V_KEY_CONTEXT_SALT_PRESENT(1) |
	    V_KEY_CONTEXT_CK_SIZE(CHCR_KEYCTX_NO_KEY) |
	    V_KEY_CONTEXT_MK_SIZE(s->hmac.mk_size) | V_KEY_CONTEXT_VALID(1));

	dst = (char *)(crwr + 1) + kctx_len + DUMMY_BYTES;
	if (crd->crd_len == 0) {
		dst[0] = 0x80;
		*(uint64_t *)(dst + axf->blocksize - sizeof(uint64_t)) =
		    htobe64(axf->blocksize << 3);
	} else if (imm_len != 0)
		crypto_copydata(crp->crp_flags, crp->crp_buf, crd->crd_skip,
		    crd->crd_len, dst);
	else
		ccr_write_ulptx_sgl(sc, dst, sgl_nsegs);

	/* XXX: TODO backpressure */
	t4_wrq_tx(sc->adapter, wr);

	return (0);
}

static int
ccr_hash_done(struct ccr_softc *sc, struct ccr_session *s, struct cryptop *crp,
    const struct cpl_fw6_pld *cpl, int error)
{
	struct cryptodesc *crd;

	crd = crp->crp_desc;
	if (error == 0) {
		crypto_copyback(crp->crp_flags, crp->crp_buf, crd->crd_inject,
		    s->hmac.hash_len, (c_caddr_t)(cpl + 1));
	}

	return (error);
}

static int
ccr_blkcipher(struct ccr_softc *sc, struct ccr_session *s, struct cryptop *crp)
{
	char iv[CHCR_MAX_CRYPTO_IV_LEN];
	struct chcr_wr *crwr;
	struct wrqe *wr;
	struct cryptodesc *crd;
	char *dst;
	u_int kctx_len, key_half, op_type, transhdr_len, wr_len;
	u_int imm_len;
	int dsgl_nsegs, dsgl_len;
	int sgl_nsegs, sgl_len;
	int error;

	crd = crp->crp_desc;

	if (s->blkcipher.key_len == 0 || crd->crd_len == 0)
		return (EINVAL);
	if (crd->crd_alg == CRYPTO_AES_CBC &&
	    (crd->crd_len % AES_BLOCK_LEN) != 0)
		return (EINVAL);

	/* Reject requests with too large of an input buffer. */
	if (crd->crd_len > MAX_REQUEST_SIZE)
		return (EFBIG);

	if (crd->crd_flags & CRD_F_ENCRYPT)
		op_type = CHCR_ENCRYPT_OP;
	else
		op_type = CHCR_DECRYPT_OP;
	
	sglist_reset(sc->sg_dsgl);
	error = sglist_append_sglist(sc->sg_dsgl, sc->sg_crp, crd->crd_skip,
	    crd->crd_len);
	if (error)
		return (error);
	dsgl_nsegs = ccr_count_sgl(sc->sg_dsgl, DSGL_SGE_MAXLEN);
	if (dsgl_nsegs > MAX_RX_PHYS_DSGL_SGE)
		return (EFBIG);
	dsgl_len = ccr_phys_dsgl_len(dsgl_nsegs);

	/* The 'key' must be 128-bit aligned. */
	kctx_len = roundup2(s->blkcipher.key_len, 16);
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dsgl_len);

	if (ccr_use_imm_data(transhdr_len, crd->crd_len +
	    s->blkcipher.iv_len)) {
		imm_len = crd->crd_len;
		sgl_nsegs = 0;
		sgl_len = 0;
	} else {
		imm_len = 0;
		sglist_reset(sc->sg_ulptx);
		error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
		    crd->crd_skip, crd->crd_len);
		if (error)
			return (error);
		sgl_nsegs = sc->sg_ulptx->sg_nseg;
		sgl_len = ccr_ulptx_sgl_len(sgl_nsegs);
	}

	wr_len = roundup2(transhdr_len, 16) + s->blkcipher.iv_len +
	    roundup2(imm_len, 16) + sgl_len;
	if (wr_len > SGE_MAX_WR_LEN)
		return (EFBIG);
	wr = alloc_wrqe(wr_len, sc->txq);
	if (wr == NULL) {
		sc->stats_wr_nomem++;
		return (ENOMEM);
	}
	crwr = wrtod(wr);
	memset(crwr, 0, wr_len);

	/*
	 * Read the existing IV from the request or generate a random
	 * one if none is provided.  Optionally copy the generated IV
	 * into the output buffer if requested.
	 */
	if (op_type == CHCR_ENCRYPT_OP) {
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crd->crd_iv, s->blkcipher.iv_len);
		else
			arc4rand(iv, s->blkcipher.iv_len, 0);
		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, s->blkcipher.iv_len, iv);
	} else {
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crd->crd_iv, s->blkcipher.iv_len);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crd->crd_inject, s->blkcipher.iv_len, iv);
	}

	ccr_populate_wreq(sc, crwr, kctx_len, wr_len, imm_len, sgl_len, 0,
	    crp);

	/* XXX: Hardcodes SGE loopback channel of 0. */
	crwr->sec_cpl.op_ivinsrtofst = htobe32(
	    V_CPL_TX_SEC_PDU_OPCODE(CPL_TX_SEC_PDU) |
	    V_CPL_TX_SEC_PDU_RXCHID(sc->tx_channel_id) |
	    V_CPL_TX_SEC_PDU_ACKFOLLOWS(0) | V_CPL_TX_SEC_PDU_ULPTXLPBK(1) |
	    V_CPL_TX_SEC_PDU_CPLLEN(2) | V_CPL_TX_SEC_PDU_PLACEHOLDER(0) |
	    V_CPL_TX_SEC_PDU_IVINSRTOFST(1));

	crwr->sec_cpl.pldlen = htobe32(s->blkcipher.iv_len + crd->crd_len);

	crwr->sec_cpl.aadstart_cipherstop_hi = htobe32(
	    V_CPL_TX_SEC_PDU_CIPHERSTART(s->blkcipher.iv_len + 1) |
	    V_CPL_TX_SEC_PDU_CIPHERSTOP_HI(0));
	crwr->sec_cpl.cipherstop_lo_authinsert = htobe32(
	    V_CPL_TX_SEC_PDU_CIPHERSTOP_LO(0));

	/* These two flits are actually a CPL_TLS_TX_SCMD_FMT. */
	crwr->sec_cpl.seqno_numivs = htobe32(
	    V_SCMD_SEQ_NO_CTRL(0) |
	    V_SCMD_PROTO_VERSION(SCMD_PROTO_VERSION_GENERIC) |
	    V_SCMD_ENC_DEC_CTRL(op_type) |
	    V_SCMD_CIPH_MODE(s->blkcipher.cipher_mode) |
	    V_SCMD_AUTH_MODE(SCMD_AUTH_MODE_NOP) |
	    V_SCMD_HMAC_CTRL(SCMD_HMAC_CTRL_NOP) |
	    V_SCMD_IV_SIZE(s->blkcipher.iv_len / 2) |
	    V_SCMD_NUM_IVS(0));
	crwr->sec_cpl.ivgen_hdrlen = htobe32(
	    V_SCMD_IV_GEN_CTRL(0) |
	    V_SCMD_MORE_FRAGS(0) | V_SCMD_LAST_FRAG(0) | V_SCMD_MAC_ONLY(0) |
	    V_SCMD_AADIVDROP(1) | V_SCMD_HDR_LEN(dsgl_len));

	crwr->key_ctx.ctx_hdr = s->blkcipher.key_ctx_hdr;
	switch (crd->crd_alg) {
	case CRYPTO_AES_CBC:
		if (crd->crd_flags & CRD_F_ENCRYPT)
			memcpy(crwr->key_ctx.key, s->blkcipher.enckey,
			    s->blkcipher.key_len);
		else
			memcpy(crwr->key_ctx.key, s->blkcipher.deckey,
			    s->blkcipher.key_len);
		break;
	case CRYPTO_AES_ICM:
		memcpy(crwr->key_ctx.key, s->blkcipher.enckey,
		    s->blkcipher.key_len);
		break;
	case CRYPTO_AES_XTS:
		key_half = s->blkcipher.key_len / 2;
		memcpy(crwr->key_ctx.key, s->blkcipher.enckey + key_half,
		    key_half);
		if (crd->crd_flags & CRD_F_ENCRYPT)
			memcpy(crwr->key_ctx.key + key_half,
			    s->blkcipher.enckey, key_half);
		else
			memcpy(crwr->key_ctx.key + key_half,
			    s->blkcipher.deckey, key_half);
		break;
	}

	dst = (char *)(crwr + 1) + kctx_len;
	ccr_write_phys_dsgl(sc, dst, dsgl_nsegs);
	dst += sizeof(struct cpl_rx_phys_dsgl) + dsgl_len;
	memcpy(dst, iv, s->blkcipher.iv_len);
	dst += s->blkcipher.iv_len;
	if (imm_len != 0)
		crypto_copydata(crp->crp_flags, crp->crp_buf, crd->crd_skip,
		    crd->crd_len, dst);
	else
		ccr_write_ulptx_sgl(sc, dst, sgl_nsegs);

	/* XXX: TODO backpressure */
	t4_wrq_tx(sc->adapter, wr);

	return (0);
}

static int
ccr_blkcipher_done(struct ccr_softc *sc, struct ccr_session *s,
    struct cryptop *crp, const struct cpl_fw6_pld *cpl, int error)
{

	/*
	 * The updated IV to permit chained requests is at
	 * cpl->data[2], but OCF doesn't permit chained requests.
	 */
	return (error);
}

/*
 * 'hashsize' is the length of a full digest.  'authsize' is the
 * requested digest length for this operation which may be less
 * than 'hashsize'.
 */
static int
ccr_hmac_ctrl(unsigned int hashsize, unsigned int authsize)
{

	if (authsize == 10)
		return (SCMD_HMAC_CTRL_TRUNC_RFC4366);
	if (authsize == 12)
		return (SCMD_HMAC_CTRL_IPSEC_96BIT);
	if (authsize == hashsize / 2)
		return (SCMD_HMAC_CTRL_DIV2);
	return (SCMD_HMAC_CTRL_NO_TRUNC);
}

static int
ccr_authenc(struct ccr_softc *sc, struct ccr_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde)
{
	char iv[CHCR_MAX_CRYPTO_IV_LEN];
	struct chcr_wr *crwr;
	struct wrqe *wr;
	struct auth_hash *axf;
	char *dst;
	u_int kctx_len, key_half, op_type, transhdr_len, wr_len;
	u_int hash_size_in_response, imm_len, iopad_size;
	u_int aad_start, aad_len, aad_stop;
	u_int auth_start, auth_stop, auth_insert;
	u_int cipher_start, cipher_stop;
	u_int hmac_ctrl, input_len;
	int dsgl_nsegs, dsgl_len;
	int sgl_nsegs, sgl_len;
	int error;

	/*
	 * If there is a need in the future, requests with an empty
	 * payload could be supported as HMAC-only requests.
	 */
	if (s->blkcipher.key_len == 0 || crde->crd_len == 0)
		return (EINVAL);
	if (crde->crd_alg == CRYPTO_AES_CBC &&
	    (crde->crd_len % AES_BLOCK_LEN) != 0)
		return (EINVAL);

	/*
	 * Compute the length of the AAD (data covered by the
	 * authentication descriptor but not the encryption
	 * descriptor).  To simplify the logic, AAD is only permitted
	 * before the cipher/plain text, not after.  This is true of
	 * all currently-generated requests.
	 */
	if (crda->crd_len + crda->crd_skip > crde->crd_len + crde->crd_skip)
		return (EINVAL);
	if (crda->crd_skip < crde->crd_skip) {
		if (crda->crd_skip + crda->crd_len > crde->crd_skip)
			aad_len = (crde->crd_skip - crda->crd_skip);
		else
			aad_len = crda->crd_len;
	} else
		aad_len = 0;
	if (aad_len + s->blkcipher.iv_len > MAX_AAD_LEN)
		return (EINVAL);

	axf = s->hmac.auth_hash;
	hash_size_in_response = s->hmac.hash_len;
	if (crde->crd_flags & CRD_F_ENCRYPT)
		op_type = CHCR_ENCRYPT_OP;
	else
		op_type = CHCR_DECRYPT_OP;

	/*
	 * The output buffer consists of the cipher text followed by
	 * the hash when encrypting.  For decryption it only contains
	 * the plain text.
	 *
	 * Due to a firmware bug, the output buffer must include a
	 * dummy output buffer for the IV and AAD prior to the real
	 * output buffer.
	 */
	if (op_type == CHCR_ENCRYPT_OP) {
		if (s->blkcipher.iv_len + aad_len + crde->crd_len +
		    hash_size_in_response > MAX_REQUEST_SIZE)
			return (EFBIG);
	} else {
		if (s->blkcipher.iv_len + aad_len + crde->crd_len >
		    MAX_REQUEST_SIZE)
			return (EFBIG);
	}
	sglist_reset(sc->sg_dsgl);
	error = sglist_append_sglist(sc->sg_dsgl, sc->sg_iv_aad, 0,
	    s->blkcipher.iv_len + aad_len);
	if (error)
		return (error);
	error = sglist_append_sglist(sc->sg_dsgl, sc->sg_crp, crde->crd_skip,
	    crde->crd_len);
	if (error)
		return (error);
	if (op_type == CHCR_ENCRYPT_OP) {
		error = sglist_append_sglist(sc->sg_dsgl, sc->sg_crp,
		    crda->crd_inject, hash_size_in_response);
		if (error)
			return (error);
	}
	dsgl_nsegs = ccr_count_sgl(sc->sg_dsgl, DSGL_SGE_MAXLEN);
	if (dsgl_nsegs > MAX_RX_PHYS_DSGL_SGE)
		return (EFBIG);
	dsgl_len = ccr_phys_dsgl_len(dsgl_nsegs);

	/* PADs must be 128-bit aligned. */
	iopad_size = roundup2(s->hmac.partial_digest_len, 16);

	/*
	 * The 'key' part of the key context consists of the key followed
	 * by the IPAD and OPAD.
	 */
	kctx_len = roundup2(s->blkcipher.key_len, 16) + iopad_size * 2;
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dsgl_len);

	/*
	 * The input buffer consists of the IV, any AAD, and then the
	 * cipher/plain text.  For decryption requests the hash is
	 * appended after the cipher text.
	 *
	 * The IV is always stored at the start of the input buffer
	 * even though it may be duplicated in the payload.  The
	 * crypto engine doesn't work properly if the IV offset points
	 * inside of the AAD region, so a second copy is always
	 * required.
	 */
	input_len = aad_len + crde->crd_len;

	/*
	 * The firmware hangs if sent a request which is a
	 * bit smaller than MAX_REQUEST_SIZE.  In particular, the
	 * firmware appears to require 512 - 16 bytes of spare room
	 * along with the size of the hash even if the hash isn't
	 * included in the input buffer.
	 */
	if (input_len + roundup2(axf->hashsize, 16) + (512 - 16) >
	    MAX_REQUEST_SIZE)
		return (EFBIG);
	if (op_type == CHCR_DECRYPT_OP)
		input_len += hash_size_in_response;
	if (ccr_use_imm_data(transhdr_len, s->blkcipher.iv_len + input_len)) {
		imm_len = input_len;
		sgl_nsegs = 0;
		sgl_len = 0;
	} else {
		imm_len = 0;
		sglist_reset(sc->sg_ulptx);
		if (aad_len != 0) {
			error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
			    crda->crd_skip, aad_len);
			if (error)
				return (error);
		}
		error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
		    crde->crd_skip, crde->crd_len);
		if (error)
			return (error);
		if (op_type == CHCR_DECRYPT_OP) {
			error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
			    crda->crd_inject, hash_size_in_response);
			if (error)
				return (error);
		}
		sgl_nsegs = sc->sg_ulptx->sg_nseg;
		sgl_len = ccr_ulptx_sgl_len(sgl_nsegs);
	}

	/*
	 * Any auth-only data before the cipher region is marked as AAD.
	 * Auth-data that overlaps with the cipher region is placed in
	 * the auth section.
	 */
	if (aad_len != 0) {
		aad_start = s->blkcipher.iv_len + 1;
		aad_stop = aad_start + aad_len - 1;
	} else {
		aad_start = 0;
		aad_stop = 0;
	}
	cipher_start = s->blkcipher.iv_len + aad_len + 1;
	if (op_type == CHCR_DECRYPT_OP)
		cipher_stop = hash_size_in_response;
	else
		cipher_stop = 0;
	if (aad_len == crda->crd_len) {
		auth_start = 0;
		auth_stop = 0;
	} else {
		if (aad_len != 0)
			auth_start = cipher_start;
		else
			auth_start = s->blkcipher.iv_len + crda->crd_skip -
			    crde->crd_skip + 1;
		auth_stop = (crde->crd_skip + crde->crd_len) -
		    (crda->crd_skip + crda->crd_len) + cipher_stop;
	}
	if (op_type == CHCR_DECRYPT_OP)
		auth_insert = hash_size_in_response;
	else
		auth_insert = 0;

	wr_len = roundup2(transhdr_len, 16) + s->blkcipher.iv_len +
	    roundup2(imm_len, 16) + sgl_len;
	if (wr_len > SGE_MAX_WR_LEN)
		return (EFBIG);
	wr = alloc_wrqe(wr_len, sc->txq);
	if (wr == NULL) {
		sc->stats_wr_nomem++;
		return (ENOMEM);
	}
	crwr = wrtod(wr);
	memset(crwr, 0, wr_len);

	/*
	 * Read the existing IV from the request or generate a random
	 * one if none is provided.  Optionally copy the generated IV
	 * into the output buffer if requested.
	 */
	if (op_type == CHCR_ENCRYPT_OP) {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, s->blkcipher.iv_len);
		else
			arc4rand(iv, s->blkcipher.iv_len, 0);
		if ((crde->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, s->blkcipher.iv_len, iv);
	} else {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, s->blkcipher.iv_len);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, s->blkcipher.iv_len, iv);
	}

	ccr_populate_wreq(sc, crwr, kctx_len, wr_len, imm_len, sgl_len,
	    op_type == CHCR_DECRYPT_OP ? hash_size_in_response : 0, crp);

	/* XXX: Hardcodes SGE loopback channel of 0. */
	crwr->sec_cpl.op_ivinsrtofst = htobe32(
	    V_CPL_TX_SEC_PDU_OPCODE(CPL_TX_SEC_PDU) |
	    V_CPL_TX_SEC_PDU_RXCHID(sc->tx_channel_id) |
	    V_CPL_TX_SEC_PDU_ACKFOLLOWS(0) | V_CPL_TX_SEC_PDU_ULPTXLPBK(1) |
	    V_CPL_TX_SEC_PDU_CPLLEN(2) | V_CPL_TX_SEC_PDU_PLACEHOLDER(0) |
	    V_CPL_TX_SEC_PDU_IVINSRTOFST(1));

	crwr->sec_cpl.pldlen = htobe32(s->blkcipher.iv_len + input_len);

	crwr->sec_cpl.aadstart_cipherstop_hi = htobe32(
	    V_CPL_TX_SEC_PDU_AADSTART(aad_start) |
	    V_CPL_TX_SEC_PDU_AADSTOP(aad_stop) |
	    V_CPL_TX_SEC_PDU_CIPHERSTART(cipher_start) |
	    V_CPL_TX_SEC_PDU_CIPHERSTOP_HI(cipher_stop >> 4));
	crwr->sec_cpl.cipherstop_lo_authinsert = htobe32(
	    V_CPL_TX_SEC_PDU_CIPHERSTOP_LO(cipher_stop & 0xf) |
	    V_CPL_TX_SEC_PDU_AUTHSTART(auth_start) |
	    V_CPL_TX_SEC_PDU_AUTHSTOP(auth_stop) |
	    V_CPL_TX_SEC_PDU_AUTHINSERT(auth_insert));

	/* These two flits are actually a CPL_TLS_TX_SCMD_FMT. */
	hmac_ctrl = ccr_hmac_ctrl(axf->hashsize, hash_size_in_response);
	crwr->sec_cpl.seqno_numivs = htobe32(
	    V_SCMD_SEQ_NO_CTRL(0) |
	    V_SCMD_PROTO_VERSION(SCMD_PROTO_VERSION_GENERIC) |
	    V_SCMD_ENC_DEC_CTRL(op_type) |
	    V_SCMD_CIPH_AUTH_SEQ_CTRL(op_type == CHCR_ENCRYPT_OP ? 1 : 0) |
	    V_SCMD_CIPH_MODE(s->blkcipher.cipher_mode) |
	    V_SCMD_AUTH_MODE(s->hmac.auth_mode) |
	    V_SCMD_HMAC_CTRL(hmac_ctrl) |
	    V_SCMD_IV_SIZE(s->blkcipher.iv_len / 2) |
	    V_SCMD_NUM_IVS(0));
	crwr->sec_cpl.ivgen_hdrlen = htobe32(
	    V_SCMD_IV_GEN_CTRL(0) |
	    V_SCMD_MORE_FRAGS(0) | V_SCMD_LAST_FRAG(0) | V_SCMD_MAC_ONLY(0) |
	    V_SCMD_AADIVDROP(0) | V_SCMD_HDR_LEN(dsgl_len));

	crwr->key_ctx.ctx_hdr = s->blkcipher.key_ctx_hdr;
	switch (crde->crd_alg) {
	case CRYPTO_AES_CBC:
		if (crde->crd_flags & CRD_F_ENCRYPT)
			memcpy(crwr->key_ctx.key, s->blkcipher.enckey,
			    s->blkcipher.key_len);
		else
			memcpy(crwr->key_ctx.key, s->blkcipher.deckey,
			    s->blkcipher.key_len);
		break;
	case CRYPTO_AES_ICM:
		memcpy(crwr->key_ctx.key, s->blkcipher.enckey,
		    s->blkcipher.key_len);
		break;
	case CRYPTO_AES_XTS:
		key_half = s->blkcipher.key_len / 2;
		memcpy(crwr->key_ctx.key, s->blkcipher.enckey + key_half,
		    key_half);
		if (crde->crd_flags & CRD_F_ENCRYPT)
			memcpy(crwr->key_ctx.key + key_half,
			    s->blkcipher.enckey, key_half);
		else
			memcpy(crwr->key_ctx.key + key_half,
			    s->blkcipher.deckey, key_half);
		break;
	}

	dst = crwr->key_ctx.key + roundup2(s->blkcipher.key_len, 16);
	memcpy(dst, s->hmac.ipad, s->hmac.partial_digest_len);
	memcpy(dst + iopad_size, s->hmac.opad, s->hmac.partial_digest_len);

	dst = (char *)(crwr + 1) + kctx_len;
	ccr_write_phys_dsgl(sc, dst, dsgl_nsegs);
	dst += sizeof(struct cpl_rx_phys_dsgl) + dsgl_len;
	memcpy(dst, iv, s->blkcipher.iv_len);
	dst += s->blkcipher.iv_len;
	if (imm_len != 0) {
		if (aad_len != 0) {
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crda->crd_skip, aad_len, dst);
			dst += aad_len;
		}
		crypto_copydata(crp->crp_flags, crp->crp_buf, crde->crd_skip,
		    crde->crd_len, dst);
		dst += crde->crd_len;
		if (op_type == CHCR_DECRYPT_OP)
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crda->crd_inject, hash_size_in_response, dst);
	} else
		ccr_write_ulptx_sgl(sc, dst, sgl_nsegs);

	/* XXX: TODO backpressure */
	t4_wrq_tx(sc->adapter, wr);

	return (0);
}

static int
ccr_authenc_done(struct ccr_softc *sc, struct ccr_session *s,
    struct cryptop *crp, const struct cpl_fw6_pld *cpl, int error)
{
	struct cryptodesc *crd;

	/*
	 * The updated IV to permit chained requests is at
	 * cpl->data[2], but OCF doesn't permit chained requests.
	 *
	 * For a decryption request, the hardware may do a verification
	 * of the HMAC which will fail if the existing HMAC isn't in the
	 * buffer.  If that happens, clear the error and copy the HMAC
	 * from the CPL reply into the buffer.
	 *
	 * For encryption requests, crd should be the cipher request
	 * which will have CRD_F_ENCRYPT set.  For decryption
	 * requests, crp_desc will be the HMAC request which should
	 * not have this flag set.
	 */
	crd = crp->crp_desc;
	if (error == EBADMSG && !CHK_PAD_ERR_BIT(be64toh(cpl->data[0])) &&
	    !(crd->crd_flags & CRD_F_ENCRYPT)) {
		crypto_copyback(crp->crp_flags, crp->crp_buf, crd->crd_inject,
		    s->hmac.hash_len, (c_caddr_t)(cpl + 1));
		error = 0;
	}
	return (error);
}

static int
ccr_gcm(struct ccr_softc *sc, struct ccr_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde)
{
	char iv[CHCR_MAX_CRYPTO_IV_LEN];
	struct chcr_wr *crwr;
	struct wrqe *wr;
	char *dst;
	u_int iv_len, kctx_len, op_type, transhdr_len, wr_len;
	u_int hash_size_in_response, imm_len;
	u_int aad_start, aad_stop, cipher_start, cipher_stop, auth_insert;
	u_int hmac_ctrl, input_len;
	int dsgl_nsegs, dsgl_len;
	int sgl_nsegs, sgl_len;
	int error;

	if (s->blkcipher.key_len == 0)
		return (EINVAL);

	/*
	 * The crypto engine doesn't handle GCM requests with an empty
	 * payload, so handle those in software instead.
	 */
	if (crde->crd_len == 0)
		return (EMSGSIZE);

	/*
	 * AAD is only permitted before the cipher/plain text, not
	 * after.
	 */
	if (crda->crd_len + crda->crd_skip > crde->crd_len + crde->crd_skip)
		return (EMSGSIZE);

	if (crda->crd_len + AES_BLOCK_LEN > MAX_AAD_LEN)
		return (EMSGSIZE);

	hash_size_in_response = s->gmac.hash_len;
	if (crde->crd_flags & CRD_F_ENCRYPT)
		op_type = CHCR_ENCRYPT_OP;
	else
		op_type = CHCR_DECRYPT_OP;

	/*
	 * The IV handling for GCM in OCF is a bit more complicated in
	 * that IPSec provides a full 16-byte IV (including the
	 * counter), whereas the /dev/crypto interface sometimes
	 * provides a full 16-byte IV (if no IV is provided in the
	 * ioctl) and sometimes a 12-byte IV (if the IV was explicit).
	 *
	 * When provided a 12-byte IV, assume the IV is really 16 bytes
	 * with a counter in the last 4 bytes initialized to 1.
	 *
	 * While iv_len is checked below, the value is currently
	 * always set to 12 when creating a GCM session in this driver
	 * due to limitations in OCF (there is no way to know what the
	 * IV length of a given request will be).  This means that the
	 * driver always assumes as 12-byte IV for now.
	 */
	if (s->blkcipher.iv_len == 12)
		iv_len = AES_BLOCK_LEN;
	else
		iv_len = s->blkcipher.iv_len;

	/*
	 * The output buffer consists of the cipher text followed by
	 * the tag when encrypting.  For decryption it only contains
	 * the plain text.
	 *
	 * Due to a firmware bug, the output buffer must include a
	 * dummy output buffer for the IV and AAD prior to the real
	 * output buffer.
	 */
	if (op_type == CHCR_ENCRYPT_OP) {
		if (iv_len + crda->crd_len + crde->crd_len +
		    hash_size_in_response > MAX_REQUEST_SIZE)
			return (EFBIG);
	} else {
		if (iv_len + crda->crd_len + crde->crd_len > MAX_REQUEST_SIZE)
			return (EFBIG);
	}
	sglist_reset(sc->sg_dsgl);
	error = sglist_append_sglist(sc->sg_dsgl, sc->sg_iv_aad, 0, iv_len +
	    crda->crd_len);
	if (error)
		return (error);
	error = sglist_append_sglist(sc->sg_dsgl, sc->sg_crp, crde->crd_skip,
	    crde->crd_len);
	if (error)
		return (error);
	if (op_type == CHCR_ENCRYPT_OP) {
		error = sglist_append_sglist(sc->sg_dsgl, sc->sg_crp,
		    crda->crd_inject, hash_size_in_response);
		if (error)
			return (error);
	}
	dsgl_nsegs = ccr_count_sgl(sc->sg_dsgl, DSGL_SGE_MAXLEN);
	if (dsgl_nsegs > MAX_RX_PHYS_DSGL_SGE)
		return (EFBIG);
	dsgl_len = ccr_phys_dsgl_len(dsgl_nsegs);

	/*
	 * The 'key' part of the key context consists of the key followed
	 * by the Galois hash key.
	 */
	kctx_len = roundup2(s->blkcipher.key_len, 16) + GMAC_BLOCK_LEN;
	transhdr_len = CIPHER_TRANSHDR_SIZE(kctx_len, dsgl_len);

	/*
	 * The input buffer consists of the IV, any AAD, and then the
	 * cipher/plain text.  For decryption requests the hash is
	 * appended after the cipher text.
	 *
	 * The IV is always stored at the start of the input buffer
	 * even though it may be duplicated in the payload.  The
	 * crypto engine doesn't work properly if the IV offset points
	 * inside of the AAD region, so a second copy is always
	 * required.
	 */
	input_len = crda->crd_len + crde->crd_len;
	if (op_type == CHCR_DECRYPT_OP)
		input_len += hash_size_in_response;
	if (input_len > MAX_REQUEST_SIZE)
		return (EFBIG);
	if (ccr_use_imm_data(transhdr_len, iv_len + input_len)) {
		imm_len = input_len;
		sgl_nsegs = 0;
		sgl_len = 0;
	} else {
		imm_len = 0;
		sglist_reset(sc->sg_ulptx);
		if (crda->crd_len != 0) {
			error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
			    crda->crd_skip, crda->crd_len);
			if (error)
				return (error);
		}
		error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
		    crde->crd_skip, crde->crd_len);
		if (error)
			return (error);
		if (op_type == CHCR_DECRYPT_OP) {
			error = sglist_append_sglist(sc->sg_ulptx, sc->sg_crp,
			    crda->crd_inject, hash_size_in_response);
			if (error)
				return (error);
		}
		sgl_nsegs = sc->sg_ulptx->sg_nseg;
		sgl_len = ccr_ulptx_sgl_len(sgl_nsegs);
	}

	if (crda->crd_len != 0) {
		aad_start = iv_len + 1;
		aad_stop = aad_start + crda->crd_len - 1;
	} else {
		aad_start = 0;
		aad_stop = 0;
	}
	cipher_start = iv_len + crda->crd_len + 1;
	if (op_type == CHCR_DECRYPT_OP)
		cipher_stop = hash_size_in_response;
	else
		cipher_stop = 0;
	if (op_type == CHCR_DECRYPT_OP)
		auth_insert = hash_size_in_response;
	else
		auth_insert = 0;

	wr_len = roundup2(transhdr_len, 16) + iv_len + roundup2(imm_len, 16) +
	    sgl_len;
	if (wr_len > SGE_MAX_WR_LEN)
		return (EFBIG);
	wr = alloc_wrqe(wr_len, sc->txq);
	if (wr == NULL) {
		sc->stats_wr_nomem++;
		return (ENOMEM);
	}
	crwr = wrtod(wr);
	memset(crwr, 0, wr_len);

	/*
	 * Read the existing IV from the request or generate a random
	 * one if none is provided.  Optionally copy the generated IV
	 * into the output buffer if requested.
	 *
	 * If the input IV is 12 bytes, append an explicit 4-byte
	 * counter of 1.
	 */
	if (op_type == CHCR_ENCRYPT_OP) {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, s->blkcipher.iv_len);
		else
			arc4rand(iv, s->blkcipher.iv_len, 0);
		if ((crde->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, s->blkcipher.iv_len, iv);
	} else {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, s->blkcipher.iv_len);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, s->blkcipher.iv_len, iv);
	}
	if (s->blkcipher.iv_len == 12)
		*(uint32_t *)&iv[12] = htobe32(1);

	ccr_populate_wreq(sc, crwr, kctx_len, wr_len, imm_len, sgl_len, 0,
	    crp);

	/* XXX: Hardcodes SGE loopback channel of 0. */
	crwr->sec_cpl.op_ivinsrtofst = htobe32(
	    V_CPL_TX_SEC_PDU_OPCODE(CPL_TX_SEC_PDU) |
	    V_CPL_TX_SEC_PDU_RXCHID(sc->tx_channel_id) |
	    V_CPL_TX_SEC_PDU_ACKFOLLOWS(0) | V_CPL_TX_SEC_PDU_ULPTXLPBK(1) |
	    V_CPL_TX_SEC_PDU_CPLLEN(2) | V_CPL_TX_SEC_PDU_PLACEHOLDER(0) |
	    V_CPL_TX_SEC_PDU_IVINSRTOFST(1));

	crwr->sec_cpl.pldlen = htobe32(iv_len + input_len);

	/*
	 * NB: cipherstop is explicitly set to 0.  On encrypt it
	 * should normally be set to 0 anyway (as the encrypt crd ends
	 * at the end of the input).  However, for decrypt the cipher
	 * ends before the tag in the AUTHENC case (and authstop is
	 * set to stop before the tag), but for GCM the cipher still
	 * runs to the end of the buffer.  Not sure if this is
	 * intentional or a firmware quirk, but it is required for
	 * working tag validation with GCM decryption.
	 */
	crwr->sec_cpl.aadstart_cipherstop_hi = htobe32(
	    V_CPL_TX_SEC_PDU_AADSTART(aad_start) |
	    V_CPL_TX_SEC_PDU_AADSTOP(aad_stop) |
	    V_CPL_TX_SEC_PDU_CIPHERSTART(cipher_start) |
	    V_CPL_TX_SEC_PDU_CIPHERSTOP_HI(0));
	crwr->sec_cpl.cipherstop_lo_authinsert = htobe32(
	    V_CPL_TX_SEC_PDU_CIPHERSTOP_LO(0) |
	    V_CPL_TX_SEC_PDU_AUTHSTART(cipher_start) |
	    V_CPL_TX_SEC_PDU_AUTHSTOP(cipher_stop) |
	    V_CPL_TX_SEC_PDU_AUTHINSERT(auth_insert));

	/* These two flits are actually a CPL_TLS_TX_SCMD_FMT. */
	hmac_ctrl = ccr_hmac_ctrl(AES_GMAC_HASH_LEN, hash_size_in_response);
	crwr->sec_cpl.seqno_numivs = htobe32(
	    V_SCMD_SEQ_NO_CTRL(0) |
	    V_SCMD_PROTO_VERSION(SCMD_PROTO_VERSION_GENERIC) |
	    V_SCMD_ENC_DEC_CTRL(op_type) |
	    V_SCMD_CIPH_AUTH_SEQ_CTRL(op_type == CHCR_ENCRYPT_OP ? 1 : 0) |
	    V_SCMD_CIPH_MODE(SCMD_CIPH_MODE_AES_GCM) |
	    V_SCMD_AUTH_MODE(SCMD_AUTH_MODE_GHASH) |
	    V_SCMD_HMAC_CTRL(hmac_ctrl) |
	    V_SCMD_IV_SIZE(iv_len / 2) |
	    V_SCMD_NUM_IVS(0));
	crwr->sec_cpl.ivgen_hdrlen = htobe32(
	    V_SCMD_IV_GEN_CTRL(0) |
	    V_SCMD_MORE_FRAGS(0) | V_SCMD_LAST_FRAG(0) | V_SCMD_MAC_ONLY(0) |
	    V_SCMD_AADIVDROP(0) | V_SCMD_HDR_LEN(dsgl_len));

	crwr->key_ctx.ctx_hdr = s->blkcipher.key_ctx_hdr;
	memcpy(crwr->key_ctx.key, s->blkcipher.enckey, s->blkcipher.key_len);
	dst = crwr->key_ctx.key + roundup2(s->blkcipher.key_len, 16);
	memcpy(dst, s->gmac.ghash_h, GMAC_BLOCK_LEN);

	dst = (char *)(crwr + 1) + kctx_len;
	ccr_write_phys_dsgl(sc, dst, dsgl_nsegs);
	dst += sizeof(struct cpl_rx_phys_dsgl) + dsgl_len;
	memcpy(dst, iv, iv_len);
	dst += iv_len;
	if (imm_len != 0) {
		if (crda->crd_len != 0) {
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crda->crd_skip, crda->crd_len, dst);
			dst += crda->crd_len;
		}
		crypto_copydata(crp->crp_flags, crp->crp_buf, crde->crd_skip,
		    crde->crd_len, dst);
		dst += crde->crd_len;
		if (op_type == CHCR_DECRYPT_OP)
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crda->crd_inject, hash_size_in_response, dst);
	} else
		ccr_write_ulptx_sgl(sc, dst, sgl_nsegs);

	/* XXX: TODO backpressure */
	t4_wrq_tx(sc->adapter, wr);

	return (0);
}

static int
ccr_gcm_done(struct ccr_softc *sc, struct ccr_session *s,
    struct cryptop *crp, const struct cpl_fw6_pld *cpl, int error)
{

	/*
	 * The updated IV to permit chained requests is at
	 * cpl->data[2], but OCF doesn't permit chained requests.
	 *
	 * Note that the hardware should always verify the GMAC hash.
	 */
	return (error);
}

/*
 * Handle a GCM request that is not supported by the crypto engine by
 * performing the operation in software.  Derived from swcr_authenc().
 */
static void
ccr_gcm_soft(struct ccr_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde)
{
	struct auth_hash *axf;
	struct enc_xform *exf;
	void *auth_ctx;
	uint8_t *kschedule;
	char block[GMAC_BLOCK_LEN];
	char digest[GMAC_DIGEST_LEN];
	char iv[AES_BLOCK_LEN];
	int error, i, len;

	auth_ctx = NULL;
	kschedule = NULL;

	/* Initialize the MAC. */
	switch (s->blkcipher.key_len) {
	case 16:
		axf = &auth_hash_nist_gmac_aes_128;
		break;
	case 24:
		axf = &auth_hash_nist_gmac_aes_192;
		break;
	case 32:
		axf = &auth_hash_nist_gmac_aes_256;
		break;
	default:
		error = EINVAL;
		goto out;
	}
	auth_ctx = malloc(axf->ctxsize, M_CCR, M_NOWAIT);
	if (auth_ctx == NULL) {
		error = ENOMEM;
		goto out;
	}
	axf->Init(auth_ctx);
	axf->Setkey(auth_ctx, s->blkcipher.enckey, s->blkcipher.key_len);

	/* Initialize the cipher. */
	exf = &enc_xform_aes_nist_gcm;
	error = exf->setkey(&kschedule, s->blkcipher.enckey,
	    s->blkcipher.key_len);
	if (error)
		goto out;

	/*
	 * This assumes a 12-byte IV from the crp.  See longer comment
	 * above in ccr_gcm() for more details.
	 */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, 12);
		else
			arc4rand(iv, 12, 0);
		if ((crde->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, 12, iv);
	} else {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, 12);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, 12, iv);
	}
	*(uint32_t *)&iv[12] = htobe32(1);

	axf->Reinit(auth_ctx, iv, sizeof(iv));

	/* MAC the AAD. */
	for (i = 0; i < crda->crd_len; i += sizeof(block)) {
		len = imin(crda->crd_len - i, sizeof(block));
		crypto_copydata(crp->crp_flags, crp->crp_buf, crda->crd_skip +
		    i, len, block);
		bzero(block + len, sizeof(block) - len);
		axf->Update(auth_ctx, block, sizeof(block));
	}

	exf->reinit(kschedule, iv);

	/* Do encryption with MAC */
	for (i = 0; i < crde->crd_len; i += sizeof(block)) {
		len = imin(crde->crd_len - i, sizeof(block));
		crypto_copydata(crp->crp_flags, crp->crp_buf, crde->crd_skip +
		    i, len, block);
		bzero(block + len, sizeof(block) - len);
		if (crde->crd_flags & CRD_F_ENCRYPT) {
			exf->encrypt(kschedule, block);
			axf->Update(auth_ctx, block, len);
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crde->crd_skip + i, len, block);
		} else {
			axf->Update(auth_ctx, block, len);
		}
	}

	/* Length block. */
	bzero(block, sizeof(block));
	((uint32_t *)block)[1] = htobe32(crda->crd_len * 8);
	((uint32_t *)block)[3] = htobe32(crde->crd_len * 8);
	axf->Update(auth_ctx, block, sizeof(block));

	/* Finalize MAC. */
	axf->Final(digest, auth_ctx);

	/* Inject or validate tag. */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		crypto_copyback(crp->crp_flags, crp->crp_buf, crda->crd_inject,
		    sizeof(digest), digest);
		error = 0;
	} else {
		char digest2[GMAC_DIGEST_LEN];

		crypto_copydata(crp->crp_flags, crp->crp_buf, crda->crd_inject,
		    sizeof(digest2), digest2);
		if (timingsafe_bcmp(digest, digest2, sizeof(digest)) == 0) {
			error = 0;

			/* Tag matches, decrypt data. */
			for (i = 0; i < crde->crd_len; i += sizeof(block)) {
				len = imin(crde->crd_len - i, sizeof(block));
				crypto_copydata(crp->crp_flags, crp->crp_buf,
				    crde->crd_skip + i, len, block);
				bzero(block + len, sizeof(block) - len);
				exf->decrypt(kschedule, block);
				crypto_copyback(crp->crp_flags, crp->crp_buf,
				    crde->crd_skip + i, len, block);
			}
		} else
			error = EBADMSG;
	}

	exf->zerokey(&kschedule);
out:
	if (auth_ctx != NULL) {
		memset(auth_ctx, 0, axf->ctxsize);
		free(auth_ctx, M_CCR);
	}
	crp->crp_etype = error;
	crypto_done(crp);
}

static void
ccr_identify(driver_t *driver, device_t parent)
{
	struct adapter *sc;

	sc = device_get_softc(parent);
	if (sc->cryptocaps & FW_CAPS_CONFIG_CRYPTO_LOOKASIDE &&
	    device_find_child(parent, "ccr", -1) == NULL)
		device_add_child(parent, "ccr", -1);
}

static int
ccr_probe(device_t dev)
{

	device_set_desc(dev, "Chelsio Crypto Accelerator");
	return (BUS_PROBE_DEFAULT);
}

static void
ccr_sysctls(struct ccr_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *oid;
	struct sysctl_oid_list *children;

	ctx = device_get_sysctl_ctx(sc->dev);

	/*
	 * dev.ccr.X.
	 */
	oid = device_get_sysctl_tree(sc->dev);
	children = SYSCTL_CHILDREN(oid);

	/*
	 * dev.ccr.X.stats.
	 */
	oid = SYSCTL_ADD_NODE(ctx, children, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "statistics");
	children = SYSCTL_CHILDREN(oid);

	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "hash", CTLFLAG_RD,
	    &sc->stats_hash, 0, "Hash requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "hmac", CTLFLAG_RD,
	    &sc->stats_hmac, 0, "HMAC requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "cipher_encrypt", CTLFLAG_RD,
	    &sc->stats_blkcipher_encrypt, 0,
	    "Cipher encryption requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "cipher_decrypt", CTLFLAG_RD,
	    &sc->stats_blkcipher_decrypt, 0,
	    "Cipher decryption requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "authenc_encrypt", CTLFLAG_RD,
	    &sc->stats_authenc_encrypt, 0,
	    "Combined AES+HMAC encryption requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "authenc_decrypt", CTLFLAG_RD,
	    &sc->stats_authenc_decrypt, 0,
	    "Combined AES+HMAC decryption requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "gcm_encrypt", CTLFLAG_RD,
	    &sc->stats_gcm_encrypt, 0, "AES-GCM encryption requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "gcm_decrypt", CTLFLAG_RD,
	    &sc->stats_gcm_decrypt, 0, "AES-GCM decryption requests submitted");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "wr_nomem", CTLFLAG_RD,
	    &sc->stats_wr_nomem, 0, "Work request memory allocation failures");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "inflight", CTLFLAG_RD,
	    &sc->stats_inflight, 0, "Requests currently pending");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "mac_error", CTLFLAG_RD,
	    &sc->stats_mac_error, 0, "MAC errors");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "pad_error", CTLFLAG_RD,
	    &sc->stats_pad_error, 0, "Padding errors");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "bad_session", CTLFLAG_RD,
	    &sc->stats_bad_session, 0, "Requests with invalid session ID");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "sglist_error", CTLFLAG_RD,
	    &sc->stats_sglist_error, 0,
	    "Requests for which DMA mapping failed");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "process_error", CTLFLAG_RD,
	    &sc->stats_process_error, 0, "Requests failed during queueing");
	SYSCTL_ADD_U64(ctx, children, OID_AUTO, "sw_fallback", CTLFLAG_RD,
	    &sc->stats_sw_fallback, 0,
	    "Requests processed by falling back to software");
}

static int
ccr_attach(device_t dev)
{
	struct ccr_softc *sc;
	int32_t cid;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->adapter = device_get_softc(device_get_parent(dev));
	sc->txq = &sc->adapter->sge.ctrlq[0];
	sc->rxq = &sc->adapter->sge.rxq[0];
	cid = crypto_get_driverid(dev, sizeof(struct ccr_session),
	    CRYPTOCAP_F_HARDWARE);
	if (cid < 0) {
		device_printf(dev, "could not get crypto driver id\n");
		return (ENXIO);
	}
	sc->cid = cid;
	sc->adapter->ccr_softc = sc;

	/* XXX: TODO? */
	sc->tx_channel_id = 0;

	mtx_init(&sc->lock, "ccr", NULL, MTX_DEF);
	sc->sg_crp = sglist_alloc(TX_SGL_SEGS, M_WAITOK);
	sc->sg_ulptx = sglist_alloc(TX_SGL_SEGS, M_WAITOK);
	sc->sg_dsgl = sglist_alloc(MAX_RX_PHYS_DSGL_SGE, M_WAITOK);
	sc->iv_aad_buf = malloc(MAX_AAD_LEN, M_CCR, M_WAITOK);
	sc->sg_iv_aad = sglist_build(sc->iv_aad_buf, MAX_AAD_LEN, M_WAITOK);
	ccr_sysctls(sc);

	crypto_register(cid, CRYPTO_SHA1, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_224, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_256, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_384, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_512, 0, 0);
	crypto_register(cid, CRYPTO_SHA1_HMAC, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_224_HMAC, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_256_HMAC, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_384_HMAC, 0, 0);
	crypto_register(cid, CRYPTO_SHA2_512_HMAC, 0, 0);
	crypto_register(cid, CRYPTO_AES_CBC, 0, 0);
	crypto_register(cid, CRYPTO_AES_ICM, 0, 0);
	crypto_register(cid, CRYPTO_AES_NIST_GCM_16, 0, 0);
	crypto_register(cid, CRYPTO_AES_128_NIST_GMAC, 0, 0);
	crypto_register(cid, CRYPTO_AES_192_NIST_GMAC, 0, 0);
	crypto_register(cid, CRYPTO_AES_256_NIST_GMAC, 0, 0);
	crypto_register(cid, CRYPTO_AES_XTS, 0, 0);
	return (0);
}

static int
ccr_detach(device_t dev)
{
	struct ccr_softc *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->lock);
	sc->detaching = true;
	mtx_unlock(&sc->lock);

	crypto_unregister_all(sc->cid);

	mtx_destroy(&sc->lock);
	sglist_free(sc->sg_iv_aad);
	free(sc->iv_aad_buf, M_CCR);
	sglist_free(sc->sg_dsgl);
	sglist_free(sc->sg_ulptx);
	sglist_free(sc->sg_crp);
	sc->adapter->ccr_softc = NULL;
	return (0);
}

static void
ccr_copy_partial_hash(void *dst, int cri_alg, union authctx *auth_ctx)
{
	uint32_t *u32;
	uint64_t *u64;
	u_int i;

	u32 = (uint32_t *)dst;
	u64 = (uint64_t *)dst;
	switch (cri_alg) {
	case CRYPTO_SHA1:
	case CRYPTO_SHA1_HMAC:
		for (i = 0; i < SHA1_HASH_LEN / 4; i++)
			u32[i] = htobe32(auth_ctx->sha1ctx.h.b32[i]);
		break;
	case CRYPTO_SHA2_224:
	case CRYPTO_SHA2_224_HMAC:
		for (i = 0; i < SHA2_256_HASH_LEN / 4; i++)
			u32[i] = htobe32(auth_ctx->sha224ctx.state[i]);
		break;
	case CRYPTO_SHA2_256:
	case CRYPTO_SHA2_256_HMAC:
		for (i = 0; i < SHA2_256_HASH_LEN / 4; i++)
			u32[i] = htobe32(auth_ctx->sha256ctx.state[i]);
		break;
	case CRYPTO_SHA2_384:
	case CRYPTO_SHA2_384_HMAC:
		for (i = 0; i < SHA2_512_HASH_LEN / 8; i++)
			u64[i] = htobe64(auth_ctx->sha384ctx.state[i]);
		break;
	case CRYPTO_SHA2_512:
	case CRYPTO_SHA2_512_HMAC:
		for (i = 0; i < SHA2_512_HASH_LEN / 8; i++)
			u64[i] = htobe64(auth_ctx->sha512ctx.state[i]);
		break;
	}
}

static void
ccr_init_hash_digest(struct ccr_session *s, int cri_alg)
{
	union authctx auth_ctx;
	struct auth_hash *axf;

	axf = s->hmac.auth_hash;
	axf->Init(&auth_ctx);
	ccr_copy_partial_hash(s->hmac.ipad, cri_alg, &auth_ctx);
}

static void
ccr_init_hmac_digest(struct ccr_session *s, int cri_alg, char *key,
    int klen)
{
	union authctx auth_ctx;
	struct auth_hash *axf;
	u_int i;

	/*
	 * If the key is larger than the block size, use the digest of
	 * the key as the key instead.
	 */
	axf = s->hmac.auth_hash;
	klen /= 8;
	if (klen > axf->blocksize) {
		axf->Init(&auth_ctx);
		axf->Update(&auth_ctx, key, klen);
		axf->Final(s->hmac.ipad, &auth_ctx);
		klen = axf->hashsize;
	} else
		memcpy(s->hmac.ipad, key, klen);

	memset(s->hmac.ipad + klen, 0, axf->blocksize - klen);
	memcpy(s->hmac.opad, s->hmac.ipad, axf->blocksize);

	for (i = 0; i < axf->blocksize; i++) {
		s->hmac.ipad[i] ^= HMAC_IPAD_VAL;
		s->hmac.opad[i] ^= HMAC_OPAD_VAL;
	}

	/*
	 * Hash the raw ipad and opad and store the partial result in
	 * the same buffer.
	 */
	axf->Init(&auth_ctx);
	axf->Update(&auth_ctx, s->hmac.ipad, axf->blocksize);
	ccr_copy_partial_hash(s->hmac.ipad, cri_alg, &auth_ctx);

	axf->Init(&auth_ctx);
	axf->Update(&auth_ctx, s->hmac.opad, axf->blocksize);
	ccr_copy_partial_hash(s->hmac.opad, cri_alg, &auth_ctx);
}

/*
 * Borrowed from AES_GMAC_Setkey().
 */
static void
ccr_init_gmac_hash(struct ccr_session *s, char *key, int klen)
{
	static char zeroes[GMAC_BLOCK_LEN];
	uint32_t keysched[4 * (RIJNDAEL_MAXNR + 1)];
	int rounds;

	rounds = rijndaelKeySetupEnc(keysched, key, klen);
	rijndaelEncrypt(keysched, rounds, zeroes, s->gmac.ghash_h);
}

static int
ccr_aes_check_keylen(int alg, int klen)
{

	switch (klen) {
	case 128:
	case 192:
		if (alg == CRYPTO_AES_XTS)
			return (EINVAL);
		break;
	case 256:
		break;
	case 512:
		if (alg != CRYPTO_AES_XTS)
			return (EINVAL);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static void
ccr_aes_setkey(struct ccr_session *s, int alg, const void *key, int klen)
{
	unsigned int ck_size, iopad_size, kctx_flits, kctx_len, kbits, mk_size;
	unsigned int opad_present;

	if (alg == CRYPTO_AES_XTS)
		kbits = klen / 2;
	else
		kbits = klen;
	switch (kbits) {
	case 128:
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_128;
		break;
	case 192:
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_192;
		break;
	case 256:
		ck_size = CHCR_KEYCTX_CIPHER_KEY_SIZE_256;
		break;
	default:
		panic("should not get here");
	}

	s->blkcipher.key_len = klen / 8;
	memcpy(s->blkcipher.enckey, key, s->blkcipher.key_len);
	switch (alg) {
	case CRYPTO_AES_CBC:
	case CRYPTO_AES_XTS:
		t4_aes_getdeckey(s->blkcipher.deckey, key, kbits);
		break;
	}

	kctx_len = roundup2(s->blkcipher.key_len, 16);
	switch (s->mode) {
	case AUTHENC:
		mk_size = s->hmac.mk_size;
		opad_present = 1;
		iopad_size = roundup2(s->hmac.partial_digest_len, 16);
		kctx_len += iopad_size * 2;
		break;
	case GCM:
		mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_128;
		opad_present = 0;
		kctx_len += GMAC_BLOCK_LEN;
		break;
	default:
		mk_size = CHCR_KEYCTX_NO_KEY;
		opad_present = 0;
		break;
	}
	kctx_flits = (sizeof(struct _key_ctx) + kctx_len) / 16;
	s->blkcipher.key_ctx_hdr = htobe32(V_KEY_CONTEXT_CTX_LEN(kctx_flits) |
	    V_KEY_CONTEXT_DUAL_CK(alg == CRYPTO_AES_XTS) |
	    V_KEY_CONTEXT_OPAD_PRESENT(opad_present) |
	    V_KEY_CONTEXT_SALT_PRESENT(1) | V_KEY_CONTEXT_CK_SIZE(ck_size) |
	    V_KEY_CONTEXT_MK_SIZE(mk_size) | V_KEY_CONTEXT_VALID(1));
}

static int
ccr_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct ccr_softc *sc;
	struct ccr_session *s;
	struct auth_hash *auth_hash;
	struct cryptoini *c, *hash, *cipher;
	unsigned int auth_mode, cipher_mode, iv_len, mk_size;
	unsigned int partial_digest_len;
	int error;
	bool gcm_hash, hmac;

	if (cri == NULL)
		return (EINVAL);

	gcm_hash = false;
	hmac = false;
	cipher = NULL;
	hash = NULL;
	auth_hash = NULL;
	auth_mode = SCMD_AUTH_MODE_NOP;
	cipher_mode = SCMD_CIPH_MODE_NOP;
	iv_len = 0;
	mk_size = 0;
	partial_digest_len = 0;
	for (c = cri; c != NULL; c = c->cri_next) {
		switch (c->cri_alg) {
		case CRYPTO_SHA1:
		case CRYPTO_SHA2_224:
		case CRYPTO_SHA2_256:
		case CRYPTO_SHA2_384:
		case CRYPTO_SHA2_512:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_224_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			if (hash)
				return (EINVAL);
			hash = c;
			switch (c->cri_alg) {
			case CRYPTO_SHA1:
			case CRYPTO_SHA1_HMAC:
				auth_hash = &auth_hash_hmac_sha1;
				auth_mode = SCMD_AUTH_MODE_SHA1;
				mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_160;
				partial_digest_len = SHA1_HASH_LEN;
				break;
			case CRYPTO_SHA2_224:
			case CRYPTO_SHA2_224_HMAC:
				auth_hash = &auth_hash_hmac_sha2_224;
				auth_mode = SCMD_AUTH_MODE_SHA224;
				mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_256;
				partial_digest_len = SHA2_256_HASH_LEN;
				break;
			case CRYPTO_SHA2_256:
			case CRYPTO_SHA2_256_HMAC:
				auth_hash = &auth_hash_hmac_sha2_256;
				auth_mode = SCMD_AUTH_MODE_SHA256;
				mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_256;
				partial_digest_len = SHA2_256_HASH_LEN;
				break;
			case CRYPTO_SHA2_384:
			case CRYPTO_SHA2_384_HMAC:
				auth_hash = &auth_hash_hmac_sha2_384;
				auth_mode = SCMD_AUTH_MODE_SHA512_384;
				mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_512;
				partial_digest_len = SHA2_512_HASH_LEN;
				break;
			case CRYPTO_SHA2_512:
			case CRYPTO_SHA2_512_HMAC:
				auth_hash = &auth_hash_hmac_sha2_512;
				auth_mode = SCMD_AUTH_MODE_SHA512_512;
				mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_512;
				partial_digest_len = SHA2_512_HASH_LEN;
				break;
			case CRYPTO_AES_128_NIST_GMAC:
			case CRYPTO_AES_192_NIST_GMAC:
			case CRYPTO_AES_256_NIST_GMAC:
				gcm_hash = true;
				auth_mode = SCMD_AUTH_MODE_GHASH;
				mk_size = CHCR_KEYCTX_MAC_KEY_SIZE_128;
				break;
			}
			switch (c->cri_alg) {
			case CRYPTO_SHA1_HMAC:
			case CRYPTO_SHA2_224_HMAC:
			case CRYPTO_SHA2_256_HMAC:
			case CRYPTO_SHA2_384_HMAC:
			case CRYPTO_SHA2_512_HMAC:
				hmac = true;
				break;
			}
			break;
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_NIST_GCM_16:
		case CRYPTO_AES_XTS:
			if (cipher)
				return (EINVAL);
			cipher = c;
			switch (c->cri_alg) {
			case CRYPTO_AES_CBC:
				cipher_mode = SCMD_CIPH_MODE_AES_CBC;
				iv_len = AES_BLOCK_LEN;
				break;
			case CRYPTO_AES_ICM:
				cipher_mode = SCMD_CIPH_MODE_AES_CTR;
				iv_len = AES_BLOCK_LEN;
				break;
			case CRYPTO_AES_NIST_GCM_16:
				cipher_mode = SCMD_CIPH_MODE_AES_GCM;
				iv_len = AES_GCM_IV_LEN;
				break;
			case CRYPTO_AES_XTS:
				cipher_mode = SCMD_CIPH_MODE_AES_XTS;
				iv_len = AES_BLOCK_LEN;
				break;
			}
			if (c->cri_key != NULL) {
				error = ccr_aes_check_keylen(c->cri_alg,
				    c->cri_klen);
				if (error)
					return (error);
			}
			break;
		default:
			return (EINVAL);
		}
	}
	if (gcm_hash != (cipher_mode == SCMD_CIPH_MODE_AES_GCM))
		return (EINVAL);
	if (hash == NULL && cipher == NULL)
		return (EINVAL);
	if (hash != NULL) {
		if ((hmac || gcm_hash) && hash->cri_key == NULL)
			return (EINVAL);
		if (!(hmac || gcm_hash) && hash->cri_key != NULL)
			return (EINVAL);
	}

	sc = device_get_softc(dev);

	/*
	 * XXX: Don't create a session if the queues aren't
	 * initialized.  This is racy as the rxq can be destroyed by
	 * the associated VI detaching.  Eventually ccr should use
	 * dedicated queues.
	 */
	if (sc->rxq->iq.adapter == NULL || sc->txq->adapter == NULL)
		return (ENXIO);
	
	mtx_lock(&sc->lock);
	if (sc->detaching) {
		mtx_unlock(&sc->lock);
		return (ENXIO);
	}

	s = crypto_get_driver_session(cses);

	if (gcm_hash)
		s->mode = GCM;
	else if (hash != NULL && cipher != NULL)
		s->mode = AUTHENC;
	else if (hash != NULL) {
		if (hmac)
			s->mode = HMAC;
		else
			s->mode = HASH;
	} else {
		MPASS(cipher != NULL);
		s->mode = BLKCIPHER;
	}
	if (gcm_hash) {
		if (hash->cri_mlen == 0)
			s->gmac.hash_len = AES_GMAC_HASH_LEN;
		else
			s->gmac.hash_len = hash->cri_mlen;
		ccr_init_gmac_hash(s, hash->cri_key, hash->cri_klen);
	} else if (hash != NULL) {
		s->hmac.auth_hash = auth_hash;
		s->hmac.auth_mode = auth_mode;
		s->hmac.mk_size = mk_size;
		s->hmac.partial_digest_len = partial_digest_len;
		if (hash->cri_mlen == 0)
			s->hmac.hash_len = auth_hash->hashsize;
		else
			s->hmac.hash_len = hash->cri_mlen;
		if (hmac)
			ccr_init_hmac_digest(s, hash->cri_alg, hash->cri_key,
			    hash->cri_klen);
		else
			ccr_init_hash_digest(s, hash->cri_alg);
	}
	if (cipher != NULL) {
		s->blkcipher.cipher_mode = cipher_mode;
		s->blkcipher.iv_len = iv_len;
		if (cipher->cri_key != NULL)
			ccr_aes_setkey(s, cipher->cri_alg, cipher->cri_key,
			    cipher->cri_klen);
	}

	s->active = true;
	mtx_unlock(&sc->lock);
	return (0);
}

static void
ccr_freesession(device_t dev, crypto_session_t cses)
{
	struct ccr_softc *sc;
	struct ccr_session *s;

	sc = device_get_softc(dev);
	s = crypto_get_driver_session(cses);
	mtx_lock(&sc->lock);
	if (s->pending != 0)
		device_printf(dev,
		    "session %p freed with %d pending requests\n", s,
		    s->pending);
	s->active = false;
	mtx_unlock(&sc->lock);
}

static int
ccr_process(device_t dev, struct cryptop *crp, int hint)
{
	struct ccr_softc *sc;
	struct ccr_session *s;
	struct cryptodesc *crd, *crda, *crde;
	int error;

	if (crp == NULL)
		return (EINVAL);

	crd = crp->crp_desc;
	s = crypto_get_driver_session(crp->crp_session);
	sc = device_get_softc(dev);

	mtx_lock(&sc->lock);
	error = ccr_populate_sglist(sc->sg_crp, crp);
	if (error) {
		sc->stats_sglist_error++;
		goto out;
	}

	switch (s->mode) {
	case HASH:
		error = ccr_hash(sc, s, crp);
		if (error == 0)
			sc->stats_hash++;
		break;
	case HMAC:
		if (crd->crd_flags & CRD_F_KEY_EXPLICIT)
			ccr_init_hmac_digest(s, crd->crd_alg, crd->crd_key,
			    crd->crd_klen);
		error = ccr_hash(sc, s, crp);
		if (error == 0)
			sc->stats_hmac++;
		break;
	case BLKCIPHER:
		if (crd->crd_flags & CRD_F_KEY_EXPLICIT) {
			error = ccr_aes_check_keylen(crd->crd_alg,
			    crd->crd_klen);
			if (error)
				break;
			ccr_aes_setkey(s, crd->crd_alg, crd->crd_key,
			    crd->crd_klen);
		}
		error = ccr_blkcipher(sc, s, crp);
		if (error == 0) {
			if (crd->crd_flags & CRD_F_ENCRYPT)
				sc->stats_blkcipher_encrypt++;
			else
				sc->stats_blkcipher_decrypt++;
		}
		break;
	case AUTHENC:
		error = 0;
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_XTS:
			/* Only encrypt-then-authenticate supported. */
			crde = crd;
			crda = crd->crd_next;
			if (!(crde->crd_flags & CRD_F_ENCRYPT)) {
				error = EINVAL;
				break;
			}
			break;
		default:
			crda = crd;
			crde = crd->crd_next;
			if (crde->crd_flags & CRD_F_ENCRYPT) {
				error = EINVAL;
				break;
			}
			break;
		}
		if (error)
			break;
		if (crda->crd_flags & CRD_F_KEY_EXPLICIT)
			ccr_init_hmac_digest(s, crda->crd_alg, crda->crd_key,
			    crda->crd_klen);
		if (crde->crd_flags & CRD_F_KEY_EXPLICIT) {
			error = ccr_aes_check_keylen(crde->crd_alg,
			    crde->crd_klen);
			if (error)
				break;
			ccr_aes_setkey(s, crde->crd_alg, crde->crd_key,
			    crde->crd_klen);
		}
		error = ccr_authenc(sc, s, crp, crda, crde);
		if (error == 0) {
			if (crde->crd_flags & CRD_F_ENCRYPT)
				sc->stats_authenc_encrypt++;
			else
				sc->stats_authenc_decrypt++;
		}
		break;
	case GCM:
		error = 0;
		if (crd->crd_alg == CRYPTO_AES_NIST_GCM_16) {
			crde = crd;
			crda = crd->crd_next;
		} else {
			crda = crd;
			crde = crd->crd_next;
		}
		if (crda->crd_flags & CRD_F_KEY_EXPLICIT)
			ccr_init_gmac_hash(s, crda->crd_key, crda->crd_klen);
		if (crde->crd_flags & CRD_F_KEY_EXPLICIT) {
			error = ccr_aes_check_keylen(crde->crd_alg,
			    crde->crd_klen);
			if (error)
				break;
			ccr_aes_setkey(s, crde->crd_alg, crde->crd_key,
			    crde->crd_klen);
		}
		if (crde->crd_len == 0) {
			mtx_unlock(&sc->lock);
			ccr_gcm_soft(s, crp, crda, crde);
			return (0);
		}
		error = ccr_gcm(sc, s, crp, crda, crde);
		if (error == EMSGSIZE) {
			sc->stats_sw_fallback++;
			mtx_unlock(&sc->lock);
			ccr_gcm_soft(s, crp, crda, crde);
			return (0);
		}
		if (error == 0) {
			if (crde->crd_flags & CRD_F_ENCRYPT)
				sc->stats_gcm_encrypt++;
			else
				sc->stats_gcm_decrypt++;
		}
		break;
	}

	if (error == 0) {
		s->pending++;
		sc->stats_inflight++;
	} else
		sc->stats_process_error++;

out:
	mtx_unlock(&sc->lock);

	if (error) {
		crp->crp_etype = error;
		crypto_done(crp);
	}

	return (0);
}

static int
do_cpl6_fw_pld(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct ccr_softc *sc = iq->adapter->ccr_softc;
	struct ccr_session *s;
	const struct cpl_fw6_pld *cpl;
	struct cryptop *crp;
	uint32_t status;
	int error;

	if (m != NULL)
		cpl = mtod(m, const void *);
	else
		cpl = (const void *)(rss + 1);

	crp = (struct cryptop *)(uintptr_t)be64toh(cpl->data[1]);
	s = crypto_get_driver_session(crp->crp_session);
	status = be64toh(cpl->data[0]);
	if (CHK_MAC_ERR_BIT(status) || CHK_PAD_ERR_BIT(status))
		error = EBADMSG;
	else
		error = 0;

	mtx_lock(&sc->lock);
	s->pending--;
	sc->stats_inflight--;

	switch (s->mode) {
	case HASH:
	case HMAC:
		error = ccr_hash_done(sc, s, crp, cpl, error);
		break;
	case BLKCIPHER:
		error = ccr_blkcipher_done(sc, s, crp, cpl, error);
		break;
	case AUTHENC:
		error = ccr_authenc_done(sc, s, crp, cpl, error);
		break;
	case GCM:
		error = ccr_gcm_done(sc, s, crp, cpl, error);
		break;
	}

	if (error == EBADMSG) {
		if (CHK_MAC_ERR_BIT(status))
			sc->stats_mac_error++;
		if (CHK_PAD_ERR_BIT(status))
			sc->stats_pad_error++;
	}
	mtx_unlock(&sc->lock);
	crp->crp_etype = error;
	crypto_done(crp);
	m_freem(m);
	return (0);
}

static int
ccr_modevent(module_t mod, int cmd, void *arg)
{

	switch (cmd) {
	case MOD_LOAD:
		t4_register_cpl_handler(CPL_FW6_PLD, do_cpl6_fw_pld);
		return (0);
	case MOD_UNLOAD:
		t4_register_cpl_handler(CPL_FW6_PLD, NULL);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static device_method_t ccr_methods[] = {
	DEVMETHOD(device_identify,	ccr_identify),
	DEVMETHOD(device_probe,		ccr_probe),
	DEVMETHOD(device_attach,	ccr_attach),
	DEVMETHOD(device_detach,	ccr_detach),

	DEVMETHOD(cryptodev_newsession,	ccr_newsession),
	DEVMETHOD(cryptodev_freesession, ccr_freesession),
	DEVMETHOD(cryptodev_process,	ccr_process),

	DEVMETHOD_END
};

static driver_t ccr_driver = {
	"ccr",
	ccr_methods,
	sizeof(struct ccr_softc)
};

static devclass_t ccr_devclass;

DRIVER_MODULE(ccr, t6nex, ccr_driver, ccr_devclass, ccr_modevent, NULL);
MODULE_VERSION(ccr, 1);
MODULE_DEPEND(ccr, crypto, 1, 1, 1);
MODULE_DEPEND(ccr, t6nex, 1, 1, 1);
