/*	$OpenBSD: ldc.c,v 1.14 2016/10/13 18:40:47 tom Exp $	*/
/*
 * Copyright (c) 2009 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/hypervisor.h>

#include <sparc64/dev/ldcvar.h>

#ifdef LDC_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

void	ldc_rx_ctrl_vers(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_ctrl_rtr(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_ctrl_rts(struct ldc_conn *, struct ldc_pkt *);
void	ldc_rx_ctrl_rdx(struct ldc_conn *, struct ldc_pkt *);

void	ldc_send_ack(struct ldc_conn *);
void	ldc_send_rtr(struct ldc_conn *);
void	ldc_send_rts(struct ldc_conn *);
void	ldc_send_rdx(struct ldc_conn *);

void
ldc_rx_ctrl(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->ctrl) {
	case LDC_VERS:
		ldc_rx_ctrl_vers(lc, lp);
		break;

	case LDC_RTS:
		ldc_rx_ctrl_rts(lc, lp);
		break;

	case LDC_RTR:
		ldc_rx_ctrl_rtr(lc, lp);
		break;

	case LDC_RDX:
		ldc_rx_ctrl_rdx(lc, lp);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/0x%02x\n", lp->stype, lp->ctrl));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_vers(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		DPRINTF(("CTRL/INFO/VERS\n"));
		if (lp->major == LDC_VERSION_MAJOR &&
		    lp->minor == LDC_VERSION_MINOR)
			ldc_send_ack(lc);
		else
			/* XXX do nothing for now. */
			;
		break;

	case LDC_ACK:
		if (lc->lc_state != LDC_SND_VERS) {
			DPRINTF(("Spurious CTRL/ACK/VERS: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/ACK/VERS\n"));
		ldc_send_rts(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/VERS\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/VERS\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_rts(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state != LDC_RCV_VERS) {
			DPRINTF(("Spurious CTRL/INFO/RTS: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/INFO/RTS\n"));
		ldc_send_rtr(lc);
		break;

	case LDC_ACK:
		DPRINTF(("CTRL/ACK/RTS\n"));
		ldc_reset(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/RTS\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RTS\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_rtr(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state != LDC_SND_RTS) {
			DPRINTF(("Spurious CTRL/INFO/RTR: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/INFO/RTR\n"));
		ldc_send_rdx(lc);
		lc->lc_start(lc);
		break;

	case LDC_ACK:
		DPRINTF(("CTRL/ACK/RTR\n"));
		ldc_reset(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/RTR\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RTR\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_ctrl_rdx(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	switch (lp->stype) {
	case LDC_INFO:
		if (lc->lc_state != LDC_SND_RTR) {
			DPRINTF(("Spurious CTRL/INFO/RTR: state %d\n",
			    lc->lc_state));
			ldc_reset(lc);
			return;
		}
		DPRINTF(("CTRL/INFO/RDX\n"));
		lc->lc_start(lc);
		break;

	case LDC_ACK:
		DPRINTF(("CTRL/ACK/RDX\n"));
		ldc_reset(lc);
		break;

	case LDC_NACK:
		DPRINTF(("CTRL/NACK/RDX\n"));
		ldc_reset(lc);
		break;

	default:
		DPRINTF(("CTRL/0x%02x/RDX\n", lp->stype));
		ldc_reset(lc);
		break;
	}
}

void
ldc_rx_data(struct ldc_conn *lc, struct ldc_pkt *lp)
{
	size_t len;

	if (lp->stype != LDC_INFO) {
		DPRINTF(("DATA/0x%02x\n", lp->stype));
		ldc_reset(lc);
		return;
	}

	if (lc->lc_state != LDC_SND_RTR &&
	    lc->lc_state != LDC_SND_RDX) {
		DPRINTF(("Spurious DATA/INFO: state %d\n", lc->lc_state));
		ldc_reset(lc);
		return;
	}

	if (lp->env & LDC_FRAG_START) {
		lc->lc_len = (lp->env & LDC_LEN_MASK) + 8;
		KASSERT(lc->lc_len <= sizeof(lc->lc_msg));
		memcpy((uint8_t *)lc->lc_msg, lp, lc->lc_len);
	} else {
		len = (lp->env & LDC_LEN_MASK);
		if (lc->lc_len + len > sizeof(lc->lc_msg)) {
			DPRINTF(("Buffer overrun\n"));
			ldc_reset(lc);
			return;
		}
		memcpy(((uint8_t *)lc->lc_msg) + lc->lc_len, &lp->major, len);
		lc->lc_len += len;
	}

	if (lp->env & LDC_FRAG_STOP)
		lc->lc_rx_data(lc, (struct ldc_pkt *)lc->lc_msg);
}

void
ldc_send_vers(struct ldc_conn *lc)
{
	struct ldc_pkt *lp;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lp = (struct ldc_pkt *)(lc->lc_txq->lq_va + tx_tail);
	bzero(lp, sizeof(struct ldc_pkt));
	lp->type = LDC_CTRL;
	lp->stype = LDC_INFO;
	lp->ctrl = LDC_VERS;
	lp->major = 1;
	lp->minor = 0;

	tx_tail += sizeof(*lp);
	tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lc->lc_state = LDC_SND_VERS;
	mtx_leave(&lc->lc_txq->lq_mtx);
}

void
ldc_send_ack(struct ldc_conn *lc)
{
	struct ldc_pkt *lp;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lp = (struct ldc_pkt *)(lc->lc_txq->lq_va + tx_tail);
	bzero(lp, sizeof(struct ldc_pkt));
	lp->type = LDC_CTRL;
	lp->stype = LDC_ACK;
	lp->ctrl = LDC_VERS;
	lp->major = 1;
	lp->minor = 0;

	tx_tail += sizeof(*lp);
	tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lc->lc_state = LDC_RCV_VERS;
	mtx_leave(&lc->lc_txq->lq_mtx);
}

void
ldc_send_rts(struct ldc_conn *lc)
{
	struct ldc_pkt *lp;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lp = (struct ldc_pkt *)(lc->lc_txq->lq_va + tx_tail);
	bzero(lp, sizeof(struct ldc_pkt));
	lp->type = LDC_CTRL;
	lp->stype = LDC_INFO;
	lp->ctrl = LDC_RTS;
	lp->env = LDC_MODE_UNRELIABLE;
	lp->seqid = lc->lc_tx_seqid++;

	tx_tail += sizeof(*lp);
	tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lc->lc_state = LDC_SND_RTS;
	mtx_leave(&lc->lc_txq->lq_mtx);
}

void
ldc_send_rtr(struct ldc_conn *lc)
{
	struct ldc_pkt *lp;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lp = (struct ldc_pkt *)(lc->lc_txq->lq_va + tx_tail);
	bzero(lp, sizeof(struct ldc_pkt));
	lp->type = LDC_CTRL;
	lp->stype = LDC_INFO;
	lp->ctrl = LDC_RTR;
	lp->env = LDC_MODE_UNRELIABLE;
	lp->seqid = lc->lc_tx_seqid++;

	tx_tail += sizeof(*lp);
	tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lc->lc_state = LDC_SND_RTR;
	mtx_leave(&lc->lc_txq->lq_mtx);
}

void
ldc_send_rdx(struct ldc_conn *lc)
{
	struct ldc_pkt *lp;
	uint64_t tx_head, tx_tail, tx_state;
	int err;

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lp = (struct ldc_pkt *)(lc->lc_txq->lq_va + tx_tail);
	bzero(lp, sizeof(struct ldc_pkt));
	lp->type = LDC_CTRL;
	lp->stype = LDC_INFO;
	lp->ctrl = LDC_RDX;
	lp->env = LDC_MODE_UNRELIABLE;
	lp->seqid = lc->lc_tx_seqid++;

	tx_tail += sizeof(*lp);
	tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*lp)) - 1);
	err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
	if (err != H_EOK) {
		printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
		mtx_leave(&lc->lc_txq->lq_mtx);
		return;
	}

	lc->lc_state = LDC_SND_RDX;
	mtx_leave(&lc->lc_txq->lq_mtx);
}

int
ldc_send_unreliable(struct ldc_conn *lc, void *msg, size_t len)
{
	struct ldc_pkt *lp;
	uint64_t tx_head, tx_tail, tx_state;
	uint64_t tx_avail;
	uint8_t *p = msg;
	int err;

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_get_state(lc->lc_id, &tx_head, &tx_tail, &tx_state);
	if (err != H_EOK || tx_state != LDC_CHANNEL_UP) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return (EIO);
	}

	tx_avail = (tx_head - tx_tail) / sizeof(*lp) +
	    lc->lc_txq->lq_nentries - 1;
	tx_avail %= lc->lc_txq->lq_nentries;
	if (len > tx_avail * LDC_PKT_PAYLOAD) {
		mtx_leave(&lc->lc_txq->lq_mtx);
		return (EWOULDBLOCK);
	}

	while (len > 0) {
		lp = (struct ldc_pkt *)(lc->lc_txq->lq_va + tx_tail);
		bzero(lp, sizeof(struct ldc_pkt));
		lp->type = LDC_DATA;
		lp->stype = LDC_INFO;
		lp->env = min(len, LDC_PKT_PAYLOAD);
		if (p == msg)
			lp->env |= LDC_FRAG_START;
		if (len <= LDC_PKT_PAYLOAD)
			lp->env |= LDC_FRAG_STOP;
		lp->seqid = lc->lc_tx_seqid++;
		bcopy(p, &lp->major, min(len, LDC_PKT_PAYLOAD));

		tx_tail += sizeof(*lp);
		tx_tail &= ((lc->lc_txq->lq_nentries * sizeof(*lp)) - 1);
		err = hv_ldc_tx_set_qtail(lc->lc_id, tx_tail);
		if (err != H_EOK) {
			printf("%s: hv_ldc_tx_set_qtail: %d\n", __func__, err);
			mtx_leave(&lc->lc_txq->lq_mtx);
			return (EIO);
		}
		p += min(len, LDC_PKT_PAYLOAD);
		len -= min(len, LDC_PKT_PAYLOAD);
	}

	mtx_leave(&lc->lc_txq->lq_mtx);
	return (0);
}

void
ldc_reset(struct ldc_conn *lc)
{
	int err;

	DPRINTF(("Resetting connection\n"));

	mtx_enter(&lc->lc_txq->lq_mtx);
	err = hv_ldc_tx_qconf(lc->lc_id,
	    lc->lc_txq->lq_map->dm_segs[0].ds_addr, lc->lc_txq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_tx_qconf %d\n", __func__, err);

	err = hv_ldc_rx_qconf(lc->lc_id,
	    lc->lc_rxq->lq_map->dm_segs[0].ds_addr, lc->lc_rxq->lq_nentries);
	if (err != H_EOK)
		printf("%s: hv_ldc_rx_qconf %d\n", __func__, err);

	lc->lc_tx_seqid = 0;
	lc->lc_state = 0;
	lc->lc_tx_state = lc->lc_rx_state = LDC_CHANNEL_DOWN;
	mtx_leave(&lc->lc_txq->lq_mtx);

	lc->lc_reset(lc);
}

struct ldc_queue *
ldc_queue_alloc(bus_dma_tag_t t, int nentries)
{
	struct ldc_queue *lq;
	bus_size_t size;
	caddr_t va;
	int nsegs;

	lq = malloc(sizeof(struct ldc_queue), M_DEVBUF, M_NOWAIT);
	if (lq == NULL)
		return NULL;

	mtx_init(&lq->lq_mtx, IPL_TTY);

	size = roundup(nentries * sizeof(struct ldc_pkt), PAGE_SIZE);

	if (bus_dmamap_create(t, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &lq->lq_map) != 0)
		goto error;

	if (bus_dmamem_alloc(t, size, PAGE_SIZE, 0, &lq->lq_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT) != 0)
		goto destroy;

	if (bus_dmamem_map(t, &lq->lq_seg, 1, size, &va,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(t, lq->lq_map, va, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	lq->lq_va = va;
	lq->lq_nentries = nentries;
	return (lq);

unmap:
	bus_dmamem_unmap(t, va, size);
free:
	bus_dmamem_free(t, &lq->lq_seg, 1);
destroy:
	bus_dmamap_destroy(t, lq->lq_map);
error:
	free(lq, M_DEVBUF, sizeof(struct ldc_queue));

	return (NULL);
}

void
ldc_queue_free(bus_dma_tag_t t, struct ldc_queue *lq)
{
	bus_size_t size;

	size = roundup(lq->lq_nentries * sizeof(struct ldc_pkt), PAGE_SIZE);

	bus_dmamap_unload(t, lq->lq_map);
	bus_dmamem_unmap(t, lq->lq_va, size);
	bus_dmamem_free(t, &lq->lq_seg, 1);
	bus_dmamap_destroy(t, lq->lq_map);
	free(lq, M_DEVBUF, 0);
}

struct ldc_map *
ldc_map_alloc(bus_dma_tag_t t, int nentries)
{
	struct ldc_map *lm;
	bus_size_t size;
	caddr_t va;
	int nsegs;

	lm = malloc(sizeof(struct ldc_map), M_DEVBUF, M_NOWAIT);
	if (lm == NULL)
		return NULL;

	size = roundup(nentries * sizeof(struct ldc_map_slot), PAGE_SIZE);

	if (bus_dmamap_create(t, size, 1, size, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &lm->lm_map) != 0)
		goto error;

	if (bus_dmamem_alloc(t, size, PAGE_SIZE, 0, &lm->lm_seg, 1,
	    &nsegs, BUS_DMA_NOWAIT) != 0)
		goto destroy;

	if (bus_dmamem_map(t, &lm->lm_seg, 1, size, &va,
	    BUS_DMA_NOWAIT) != 0)
		goto free;

	if (bus_dmamap_load(t, lm->lm_map, va, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto unmap;

	lm->lm_slot = (struct ldc_map_slot *)va;
	lm->lm_nentries = nentries;
	bzero(lm->lm_slot, nentries * sizeof(struct ldc_map_slot));
	return (lm);

unmap:
	bus_dmamem_unmap(t, va, size);
free:
	bus_dmamem_free(t, &lm->lm_seg, 1);
destroy:
	bus_dmamap_destroy(t, lm->lm_map);
error:
	free(lm, M_DEVBUF, sizeof(struct ldc_map));

	return (NULL);
}

void
ldc_map_free(bus_dma_tag_t t, struct ldc_map *lm)
{
	bus_size_t size;

	size = lm->lm_nentries * sizeof(struct ldc_map_slot);
	size = roundup(size, PAGE_SIZE);

	bus_dmamap_unload(t, lm->lm_map);
	bus_dmamem_unmap(t, (caddr_t)lm->lm_slot, size);
	bus_dmamem_free(t, &lm->lm_seg, 1);
	bus_dmamap_destroy(t, lm->lm_map);
	free(lm, M_DEVBUF, 0);
}
