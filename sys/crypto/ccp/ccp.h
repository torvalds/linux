/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#pragma once

/*
 * Keccak SHAKE128 (if supported by the device?) uses a 1344 bit block.
 * SHA3-224 is the next largest block size, at 1152 bits.  However, crypto(4)
 * doesn't support any SHA3 hash, so SHA2 is the constraint:
 */
#define CCP_HASH_MAX_BLOCK_SIZE	(SHA2_512_BLOCK_LEN)

#define CCP_AES_MAX_KEY_LEN	(AES_XTS_MAX_KEY)
#define CCP_MAX_CRYPTO_IV_LEN	32	/* GCM IV + GHASH context */

#define MAX_HW_QUEUES		5
#define MAX_LSB_REGIONS		8

#ifndef __must_check
#define __must_check __attribute__((__warn_unused_result__))
#endif

/*
 * Internal data structures.
 */
enum sha_version {
	SHA1,
#if 0
	SHA2_224,
#endif
	SHA2_256, SHA2_384, SHA2_512
};

struct ccp_session_hmac {
	struct auth_hash *auth_hash;
	int hash_len;
	unsigned int partial_digest_len;
	unsigned int auth_mode;
	unsigned int mk_size;
	char ipad[CCP_HASH_MAX_BLOCK_SIZE];
	char opad[CCP_HASH_MAX_BLOCK_SIZE];
};

struct ccp_session_gmac {
	int hash_len;
	char final_block[GMAC_BLOCK_LEN];
};

struct ccp_session_blkcipher {
	unsigned cipher_mode;
	unsigned cipher_type;
	unsigned key_len;
	unsigned iv_len;
	char enckey[CCP_AES_MAX_KEY_LEN];
	char iv[CCP_MAX_CRYPTO_IV_LEN];
};

struct ccp_session {
	bool active : 1;
	bool cipher_first : 1;
	int pending;
	enum { HMAC, BLKCIPHER, AUTHENC, GCM } mode;
	unsigned queue;
	union {
		struct ccp_session_hmac hmac;
		struct ccp_session_gmac gmac;
	};
	struct ccp_session_blkcipher blkcipher;
};

struct ccp_softc;
struct ccp_queue {
	struct mtx		cq_lock;
	unsigned		cq_qindex;
	struct ccp_softc	*cq_softc;

	/* Host memory and tracking structures for descriptor ring. */
	bus_dma_tag_t		ring_desc_tag;
	bus_dmamap_t		ring_desc_map;
	struct ccp_desc		*desc_ring;
	bus_addr_t		desc_ring_bus_addr;
	/* Callbacks and arguments ring; indices correspond to above ring. */
	struct ccp_completion_ctx *completions_ring;

	uint32_t		qcontrol;	/* Cached register value */
	unsigned		lsb_mask;	/* LSBs available to queue */
	int			private_lsb;	/* Reserved LSB #, or -1 */

	unsigned		cq_head;
	unsigned		cq_tail;
	unsigned		cq_acq_tail;

	bool			cq_waiting;	/* Thread waiting for space */

	struct sglist		*cq_sg_crp;
	struct sglist		*cq_sg_ulptx;
	struct sglist		*cq_sg_dst;
};

struct ccp_completion_ctx {
	void (*callback_fn)(struct ccp_queue *qp, struct ccp_session *s,
	    void *arg, int error);
	void *callback_arg;
	struct ccp_session *session;
};

struct ccp_softc {
	device_t dev;
	int32_t cid;
	struct mtx lock;
	bool detaching;

	unsigned ring_size_order;

	/*
	 * Each command queue is either public or private.  "Private"
	 * (PSP-only) by default.  PSP grants access to some queues to host via
	 * QMR (Queue Mask Register).  Set bits are host accessible.
	 */
	uint8_t valid_queues;

	uint8_t hw_version;
	uint8_t num_queues;
	uint16_t hw_features;
	uint16_t num_lsb_entries;

	/* Primary BAR (RID 2) used for register access */
	bus_space_tag_t pci_bus_tag;
	bus_space_handle_t pci_bus_handle;
	int pci_resource_id;
	struct resource *pci_resource;

	/* Secondary BAR (RID 5) apparently used for MSI-X */
	int pci_resource_id_msix;
	struct resource *pci_resource_msix;

	/* Interrupt resources */
	void *intr_tag[2];
	struct resource *intr_res[2];
	unsigned intr_count;

	struct ccp_queue queues[MAX_HW_QUEUES];
};

/* Internal globals */
SYSCTL_DECL(_hw_ccp);
MALLOC_DECLARE(M_CCP);
extern bool g_debug_print;
extern struct ccp_softc *g_ccp_softc;

/*
 * Debug macros.
 */
#define DPRINTF(dev, ...)	do {				\
	if (!g_debug_print)					\
		break;						\
	if ((dev) != NULL)					\
		device_printf((dev), "XXX " __VA_ARGS__);	\
	else							\
		printf("ccpXXX: " __VA_ARGS__);			\
} while (0)

#if 0
#define INSECURE_DEBUG(dev, ...)	do {			\
	if (!g_debug_print)					\
		break;						\
	if ((dev) != NULL)					\
		device_printf((dev), "XXX " __VA_ARGS__);	\
	else							\
		printf("ccpXXX: " __VA_ARGS__);			\
} while (0)
#else
#define INSECURE_DEBUG(dev, ...)
#endif

/*
 * Internal hardware manipulation routines.
 */
int ccp_hw_attach(device_t dev);
void ccp_hw_detach(device_t dev);

void ccp_queue_write_tail(struct ccp_queue *qp);

#ifdef DDB
void db_ccp_show_hw(struct ccp_softc *sc);
void db_ccp_show_queue_hw(struct ccp_queue *qp);
#endif

/*
 * Internal hardware crypt-op submission routines.
 */
int ccp_authenc(struct ccp_queue *sc, struct ccp_session *s,
    struct cryptop *crp, struct cryptodesc *crda, struct cryptodesc *crde)
    __must_check;
int ccp_blkcipher(struct ccp_queue *sc, struct ccp_session *s,
    struct cryptop *crp) __must_check;
int ccp_gcm(struct ccp_queue *sc, struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde) __must_check;
int ccp_hmac(struct ccp_queue *sc, struct ccp_session *s, struct cryptop *crp)
    __must_check;

/*
 * Internal hardware TRNG read routine.
 */
u_int random_ccp_read(void *v, u_int c);

/* XXX */
int ccp_queue_acquire_reserve(struct ccp_queue *qp, unsigned n, int mflags)
    __must_check;
void ccp_queue_abort(struct ccp_queue *qp);
void ccp_queue_release(struct ccp_queue *qp);

/*
 * Internal inline routines.
 */
static inline unsigned
ccp_queue_get_active(struct ccp_queue *qp)
{
	struct ccp_softc *sc;

	sc = qp->cq_softc;
	return ((qp->cq_tail - qp->cq_head) & ((1 << sc->ring_size_order) - 1));
}

static inline unsigned
ccp_queue_get_ring_space(struct ccp_queue *qp)
{
	struct ccp_softc *sc;

	sc = qp->cq_softc;
	return ((1 << sc->ring_size_order) - ccp_queue_get_active(qp) - 1);
}
