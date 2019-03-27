/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2008-2009 Semihalf, Piotr Ziecik
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SEC_H
#define _SEC_H

/*
 * Each SEC channel can hold up to 24 descriptors. All 4 channels can be
 * simultaneously active holding 96 descriptors. Each descriptor can use 0 or
 * more link table entries depending of size and granulation of input/output
 * data. One link table entry is needed for each 65535 bytes of data.
 */

/* Driver settings */
#define SEC_TIMEOUT			100000
#define SEC_MAX_SESSIONS		256
#define SEC_DESCRIPTORS			256	/* Must be power of 2 */
#define SEC_LT_ENTRIES			1024	/* Must be power of 2 */
#define SEC_MAX_IV_LEN			16
#define SEC_MAX_KEY_LEN			64

/* SEC information */
#define SEC_20_ID			0x0000000000000040ULL
#define SEC_30_ID			0x0030030000000000ULL
#define SEC_31_ID			0x0030030100000000ULL
#define SEC_CHANNELS			4
#define SEC_POINTERS			7
#define SEC_MAX_DMA_BLOCK_SIZE		0xFFFF
#define SEC_MAX_FIFO_LEVEL		24
#define SEC_DMA_ALIGNMENT		8

#define __packed__			__attribute__ ((__packed__))

struct sec_softc;
struct sec_session;

/* SEC descriptor definition */
struct sec_hw_desc_ptr {
	u_int		shdp_length		: 16;
	u_int		shdp_j			: 1;
	u_int		shdp_extent		: 7;
	u_int		__padding0		: 4;
	uint64_t	shdp_ptr		: 36;
} __packed__;

struct sec_hw_desc {
	union __packed__ {
		struct __packed__ {
			u_int	eu_sel0		: 4;
			u_int	mode0		: 8;
			u_int	eu_sel1		: 4;
			u_int	mode1		: 8;
			u_int	desc_type	: 5;
			u_int	__padding0	: 1;
			u_int	dir		: 1;
			u_int	dn		: 1;
			u_int	__padding1	: 32;
		} request;
		struct __packed__ {
			u_int	done		: 8;
			u_int	__padding0	: 27;
			u_int	iccr0		: 2;
			u_int	__padding1	: 6;
			u_int	iccr1		: 2;
			u_int	__padding2	: 19;
		} feedback;
	} shd_control;

	struct sec_hw_desc_ptr	shd_pointer[SEC_POINTERS];

	/* Data below is mapped to descriptor pointers */
	uint8_t			shd_iv[SEC_MAX_IV_LEN];
	uint8_t			shd_key[SEC_MAX_KEY_LEN];
	uint8_t			shd_mkey[SEC_MAX_KEY_LEN];
} __packed__;

#define shd_eu_sel0		shd_control.request.eu_sel0
#define shd_mode0		shd_control.request.mode0
#define shd_eu_sel1		shd_control.request.eu_sel1
#define shd_mode1		shd_control.request.mode1
#define shd_desc_type		shd_control.request.desc_type
#define shd_dir			shd_control.request.dir
#define shd_dn			shd_control.request.dn
#define shd_done		shd_control.feedback.done
#define shd_iccr0		shd_control.feedback.iccr0
#define shd_iccr1		shd_control.feedback.iccr1

/* SEC link table entries definition */
struct sec_hw_lt {
	u_int			shl_length	: 16;
	u_int			__padding0	: 6;
	u_int			shl_r		: 1;
	u_int			shl_n		: 1;
	u_int			__padding1	: 4;
	uint64_t		shl_ptr		: 36;
} __packed__;

struct sec_dma_mem {
	void			*dma_vaddr;
	bus_addr_t		dma_paddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	u_int			dma_is_map;
};

struct sec_desc {
	struct sec_hw_desc	*sd_desc;
	bus_addr_t		sd_desc_paddr;
	struct sec_dma_mem	sd_ptr_dmem[SEC_POINTERS];
	struct cryptop		*sd_crp;
	u_int			sd_lt_used;
	u_int			sd_error;
};

struct sec_lt {
	struct sec_hw_lt	*sl_lt;
	bus_addr_t		sl_lt_paddr;
};

struct sec_eu_methods {
	int	(*sem_newsession)(struct sec_softc *sc,
	    struct sec_session *ses, struct cryptoini *enc,
	    struct cryptoini *mac);
	int	(*sem_make_desc)(struct sec_softc *sc,
	    struct sec_session *ses, struct sec_desc *desc,
	    struct cryptop *crp, int buftype);
};

struct sec_session {
	struct sec_eu_methods	*ss_eu;
	uint8_t			ss_key[SEC_MAX_KEY_LEN];
	uint8_t			ss_mkey[SEC_MAX_KEY_LEN];
	u_int			ss_klen;
	u_int			ss_mklen;
	u_int			ss_ivlen;
};

struct sec_desc_map_info {
	struct sec_softc	*sdmi_sc;
	bus_size_t		sdmi_size;
	bus_size_t		sdmi_offset;
	struct sec_lt		*sdmi_lt_first;
	struct sec_lt		*sdmi_lt_last;
	u_int			sdmi_lt_used;
};

struct sec_softc {
	device_t		sc_dev;
	int32_t			sc_cid;
	int			sc_blocked;
	int			sc_shutdown;
	u_int			sc_version;

	uint64_t		sc_int_error_mask;
	uint64_t		sc_channel_idle_mask;

	struct mtx		sc_controller_lock;
	struct mtx		sc_descriptors_lock;

	struct sec_desc		sc_desc[SEC_DESCRIPTORS];
	u_int			sc_free_desc_get_cnt;
	u_int			sc_free_desc_put_cnt;
	u_int			sc_ready_desc_get_cnt;
	u_int			sc_ready_desc_put_cnt;
	u_int			sc_queued_desc_get_cnt;
	u_int			sc_queued_desc_put_cnt;

	struct sec_lt		sc_lt[SEC_LT_ENTRIES + 1];
	u_int			sc_lt_alloc_cnt;
	u_int			sc_lt_free_cnt;

	struct sec_dma_mem	sc_desc_dmem;	/* descriptors DMA memory */
	struct sec_dma_mem	sc_lt_dmem;	/* link tables DMA memory */

	struct resource		*sc_rres;	/* register resource */
        int			sc_rrid;	/* register rid */
	struct {
		bus_space_tag_t	bst;
		bus_space_handle_t bsh;
	} sc_bas;

	struct resource		*sc_pri_ires;	/* primary irq resource */
	void			*sc_pri_ihand;	/* primary irq handler */
	int			sc_pri_irid;	/* primary irq resource id */

	struct resource		*sc_sec_ires;	/* secondary irq resource */
	void			*sc_sec_ihand;	/* secondary irq handler */
	int			sc_sec_irid;	/* secondary irq resource id */
};

/* Locking macros */
#define SEC_LOCK(sc, what)						\
	mtx_lock(&(sc)->sc_ ## what ## _lock)
#define SEC_UNLOCK(sc, what)						\
	mtx_unlock(&(sc)->sc_ ## what ## _lock)
#define SEC_LOCK_ASSERT(sc, what)					\
	mtx_assert(&(sc)->sc_ ## what ## _lock, MA_OWNED)

/* Read/Write definitions */
#define SEC_READ(sc, reg)						\
	bus_space_read_8((sc)->sc_bas.bst, (sc)->sc_bas.bsh, (reg))
#define SEC_WRITE(sc, reg, val)						\
	bus_space_write_8((sc)->sc_bas.bst, (sc)->sc_bas.bsh, (reg), (val))

/* Base allocation macros (warning: wrap must be 2^n) */
#define SEC_CNT_INIT(sc, cnt, wrap)					\
	(((sc)->cnt) = ((wrap) - 1))
#define SEC_ADD(sc, cnt, wrap, val)					\
	((sc)->cnt = (((sc)->cnt) + (val)) & ((wrap) - 1))
#define SEC_INC(sc, cnt, wrap)						\
	SEC_ADD(sc, cnt, wrap, 1)
#define SEC_DEC(sc, cnt, wrap)						\
	SEC_ADD(sc, cnt, wrap, -1)
#define SEC_GET_GENERIC(sc, tab, cnt, wrap)				\
	((sc)->tab[SEC_INC(sc, cnt, wrap)])
#define SEC_PUT_GENERIC(sc, tab, cnt, wrap, val)			\
	((sc)->tab[SEC_INC(sc, cnt, wrap)] = val)

/* Interface for descriptors */
#define SEC_GET_FREE_DESC(sc)						\
	&SEC_GET_GENERIC(sc, sc_desc, sc_free_desc_get_cnt, SEC_DESCRIPTORS)

#define SEC_PUT_BACK_FREE_DESC(sc)					\
	SEC_DEC(sc, sc_free_desc_get_cnt, SEC_DESCRIPTORS)

#define SEC_DESC_FREE2READY(sc)						\
	SEC_INC(sc, sc_ready_desc_put_cnt, SEC_DESCRIPTORS)

#define SEC_GET_READY_DESC(sc)						\
	&SEC_GET_GENERIC(sc, sc_desc, sc_ready_desc_get_cnt, SEC_DESCRIPTORS)

#define SEC_PUT_BACK_READY_DESC(sc)					\
	SEC_DEC(sc, sc_ready_desc_get_cnt, SEC_DESCRIPTORS)

#define SEC_DESC_READY2QUEUED(sc)					\
	SEC_INC(sc, sc_queued_desc_put_cnt, SEC_DESCRIPTORS)

#define SEC_GET_QUEUED_DESC(sc)						\
	&SEC_GET_GENERIC(sc, sc_desc, sc_queued_desc_get_cnt, SEC_DESCRIPTORS)

#define SEC_PUT_BACK_QUEUED_DESC(sc)					\
	SEC_DEC(sc, sc_queued_desc_get_cnt, SEC_DESCRIPTORS)

#define SEC_DESC_QUEUED2FREE(sc)					\
	SEC_INC(sc, sc_free_desc_put_cnt, SEC_DESCRIPTORS)

#define SEC_FREE_DESC_CNT(sc)						\
	(((sc)->sc_free_desc_put_cnt - (sc)->sc_free_desc_get_cnt - 1)	\
	& (SEC_DESCRIPTORS - 1))

#define SEC_READY_DESC_CNT(sc)						\
	(((sc)->sc_ready_desc_put_cnt - (sc)->sc_ready_desc_get_cnt) &	\
	(SEC_DESCRIPTORS - 1))

#define SEC_QUEUED_DESC_CNT(sc)						\
	(((sc)->sc_queued_desc_put_cnt - (sc)->sc_queued_desc_get_cnt)	\
	& (SEC_DESCRIPTORS - 1))

#define SEC_DESC_SYNC(sc, mode) do {					\
	sec_sync_dma_mem(&((sc)->sc_desc_dmem), (mode));		\
	sec_sync_dma_mem(&((sc)->sc_lt_dmem), (mode));			\
} while (0)

#define SEC_DESC_SYNC_POINTERS(desc, mode) do {				\
	u_int i;							\
	for (i = 0; i < SEC_POINTERS; i++)				\
		sec_sync_dma_mem(&((desc)->sd_ptr_dmem[i]), (mode));	\
} while (0)

#define SEC_DESC_FREE_POINTERS(desc) do {				\
	u_int i;							\
	for (i = 0; i < SEC_POINTERS; i++)				\
		sec_free_dma_mem(&(desc)->sd_ptr_dmem[i]);		\
} while (0);

#define SEC_DESC_PUT_BACK_LT(sc, desc)					\
	SEC_PUT_BACK_LT(sc, (desc)->sd_lt_used)

#define SEC_DESC_FREE_LT(sc, desc)					\
	SEC_FREE_LT(sc, (desc)->sd_lt_used)

/* Interface for link tables */
#define SEC_ALLOC_LT_ENTRY(sc)						\
	&SEC_GET_GENERIC(sc, sc_lt, sc_lt_alloc_cnt, SEC_LT_ENTRIES)

#define SEC_PUT_BACK_LT(sc, num)					\
	SEC_ADD(sc, sc_lt_alloc_cnt, SEC_LT_ENTRIES, -(num))

#define SEC_FREE_LT(sc, num)						\
	SEC_ADD(sc, sc_lt_free_cnt, SEC_LT_ENTRIES, num)

#define SEC_FREE_LT_CNT(sc)						\
	(((sc)->sc_lt_free_cnt - (sc)->sc_lt_alloc_cnt - 1)		\
	& (SEC_LT_ENTRIES - 1))

/* DMA Maping defines */
#define SEC_MEMORY		0
#define SEC_UIO			1
#define SEC_MBUF		2

/* Size of SEC registers area */
#define SEC_IO_SIZE		0x10000

/* SEC Controller registers */
#define SEC_IER			0x1008
#define SEC_INT_CH_DN(n)	(1ULL << (((n) * 2) + 32))
#define SEC_INT_CH_ERR(n)	(1ULL << (((n) * 2) + 33))
#define SEC_INT_ITO		(1ULL << 55)

#define SEC_ISR			0x1010
#define SEC_ICR			0x1018
#define SEC_ID			0x1020

#define SEC_EUASR		0x1028
#define SEC_EUASR_RNGU(r)	(((r) >> 0) & 0xF)
#define SEC_EUASR_PKEU(r)	(((r) >> 8) & 0xF)
#define SEC_EUASR_KEU(r)	(((r) >> 16) & 0xF)
#define SEC_EUASR_CRCU(r)	(((r) >> 20) & 0xF)
#define SEC_EUASR_DEU(r)	(((r) >> 32) & 0xF)
#define SEC_EUASR_AESU(r)	(((r) >> 40) & 0xF)
#define SEC_EUASR_MDEU(r)	(((r) >> 48) & 0xF)
#define SEC_EUASR_AFEU(r)	(((r) >> 56) & 0xF)

#define SEC_MCR			0x1030
#define SEC_MCR_SWR		(1ULL << 32)

/* SEC Channel registers */
#define SEC_CHAN_CCR(n)		(((n) * 0x100) + 0x1108)
#define SEC_CHAN_CCR_CDIE	(1ULL << 1)
#define SEC_CHAN_CCR_NT		(1ULL << 2)
#define SEC_CHAN_CCR_AWSE	(1ULL << 3)
#define SEC_CHAN_CCR_CDWE	(1ULL << 4)
#define SEC_CHAN_CCR_BS		(1ULL << 8)
#define SEC_CHAN_CCR_WGN	(1ULL << 13)
#define SEC_CHAN_CCR_R		(1ULL << 32)
#define SEC_CHAN_CCR_CON	(1ULL << 33)

#define SEC_CHAN_CSR(n)		(((n) * 0x100) + 0x1110)
#define SEC_CHAN_CSR2_FFLVL_M	0x1FULL
#define SEC_CHAN_CSR2_FFLVL_S	56
#define SEC_CHAN_CSR2_GSTATE_M	0x0FULL
#define SEC_CHAN_CSR2_GSTATE_S	48
#define SEC_CHAN_CSR2_PSTATE_M	0x0FULL
#define SEC_CHAN_CSR2_PSTATE_S	40
#define SEC_CHAN_CSR2_MSTATE_M	0x3FULL
#define SEC_CHAN_CSR2_MSTATE_S	32
#define SEC_CHAN_CSR3_FFLVL_M	0x1FULL
#define SEC_CHAN_CSR3_FFLVL_S	24
#define SEC_CHAN_CSR3_MSTATE_M	0x1FFULL
#define SEC_CHAN_CSR3_MSTATE_S	32
#define SEC_CHAN_CSR3_PSTATE_M	0x7FULL
#define SEC_CHAN_CSR3_PSTATE_S	48
#define SEC_CHAN_CSR3_GSTATE_M	0x7FULL
#define SEC_CHAN_CSR3_GSTATE_S	56

#define SEC_CHAN_CDPR(n)	(((n) * 0x100) + 0x1140)
#define SEC_CHAN_FF(n)		(((n) * 0x100) + 0x1148)

/* SEC Execution Units numbers */
#define SEC_EU_NONE		0x0
#define SEC_EU_AFEU		0x1
#define SEC_EU_DEU		0x2
#define SEC_EU_MDEU_A		0x3
#define SEC_EU_MDEU_B		0xB
#define SEC_EU_RNGU		0x4
#define SEC_EU_PKEU		0x5
#define SEC_EU_AESU		0x6
#define SEC_EU_KEU		0x7
#define SEC_EU_CRCU		0x8

/* SEC descriptor types */
#define SEC_DT_COMMON_NONSNOOP	0x02
#define SEC_DT_HMAC_SNOOP	0x04

/* SEC AESU declarations and definitions */
#define SEC_AESU_MODE_ED	(1ULL << 0)
#define SEC_AESU_MODE_CBC	(1ULL << 1)

/* SEC DEU declarations and definitions */
#define SEC_DEU_MODE_ED		(1ULL << 0)
#define SEC_DEU_MODE_TS		(1ULL << 1)
#define SEC_DEU_MODE_CBC	(1ULL << 2)

/* SEC MDEU declarations and definitions */
#define SEC_HMAC_HASH_LEN	12
#define SEC_MDEU_MODE_SHA1	0x00	/* MDEU A */
#define SEC_MDEU_MODE_SHA384	0x00	/* MDEU B */
#define SEC_MDEU_MODE_SHA256	0x01
#define SEC_MDEU_MODE_MD5	0x02	/* MDEU A */
#define SEC_MDEU_MODE_SHA512	0x02	/* MDEU B */
#define SEC_MDEU_MODE_SHA224	0x03
#define SEC_MDEU_MODE_PD	(1ULL << 2)
#define SEC_MDEU_MODE_HMAC	(1ULL << 3)
#define SEC_MDEU_MODE_INIT	(1ULL << 4)
#define SEC_MDEU_MODE_SMAC	(1ULL << 5)
#define SEC_MDEU_MODE_CICV	(1ULL << 6)
#define SEC_MDEU_MODE_CONT	(1ULL << 7)

#endif
