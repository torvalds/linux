/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2017 Cavium, Inc.. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Cavium, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER(S) OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*$FreeBSD$*/

#include "lio_bsd.h"
#include "lio_common.h"
#include "lio_droq.h"
#include "lio_iq.h"
#include "lio_response_manager.h"
#include "lio_device.h"
#include "lio_ctrl.h"
#include "lio_main.h"
#include "lio_network.h"
#include "lio_rxtx.h"

int
lio_xmit(struct lio *lio, struct lio_instr_queue *iq,
	 struct mbuf **m_headp)
{
	struct lio_data_pkt		ndata;
	union lio_cmd_setup		cmdsetup;
	struct lio_mbuf_free_info	*finfo = NULL;
	struct octeon_device		*oct = iq->oct_dev;
	struct lio_iq_stats		*stats;
	struct octeon_instr_irh		*irh;
	struct lio_request_list		*tx_buf;
	union lio_tx_info		*tx_info;
	struct mbuf			*m_head;
	bus_dma_segment_t		segs[LIO_MAX_SG];
	bus_dmamap_t			map;
	uint64_t	dptr = 0;
	uint32_t	tag = 0;
	int		iq_no = 0;
	int		nsegs;
	int		status = 0;

	iq_no = iq->txpciq.s.q_no;
	tag = iq_no;
	stats = &oct->instr_queue[iq_no]->stats;
	tx_buf = iq->request_list + iq->host_write_index;

	/*
	 * Check for all conditions in which the current packet cannot be
	 * transmitted.
	 */
	if (!(atomic_load_acq_int(&lio->ifstate) & LIO_IFSTATE_RUNNING) ||
	    (!lio->linfo.link.s.link_up)) {
		lio_dev_info(oct, "Transmit failed link_status : %d\n",
			     lio->linfo.link.s.link_up);
		status = ENETDOWN;
		goto drop_packet;
	}

	if (lio_iq_is_full(oct, iq_no)) {
		/* Defer sending if queue is full */
		lio_dev_dbg(oct, "Transmit failed iq:%d full\n", iq_no);
		stats->tx_iq_busy++;
		return (ENOBUFS);
	}

	map = tx_buf->map;
	status = bus_dmamap_load_mbuf_sg(iq->txtag, map, *m_headp, segs, &nsegs,
					 BUS_DMA_NOWAIT);
	if (status == EFBIG) {
		struct mbuf	*m;

		m = m_defrag(*m_headp, M_NOWAIT);
		if (m == NULL) {
			stats->mbuf_defrag_failed++;
			goto drop_packet;
		}

		*m_headp = m;
		status = bus_dmamap_load_mbuf_sg(iq->txtag, map,
						 *m_headp, segs, &nsegs,
						 BUS_DMA_NOWAIT);
	}

	if (status == ENOMEM) {
		goto retry;
	} else if (status) {
		stats->tx_dmamap_fail++;
		lio_dev_dbg(oct, "bus_dmamap_load_mbuf_sg failed with error %d. iq:%d",
			    status, iq_no);
		goto drop_packet;
	}

	m_head = *m_headp;

	/* Info used to unmap and free the buffers. */
	finfo = &tx_buf->finfo;
	finfo->map = map;
	finfo->mb = m_head;

	/* Prepare the attributes for the data to be passed to OSI. */
	bzero(&ndata, sizeof(struct lio_data_pkt));

	ndata.buf = (void *)finfo;
	ndata.q_no = iq_no;
	ndata.datasize = m_head->m_pkthdr.len;

	cmdsetup.cmd_setup64 = 0;
	cmdsetup.s.iq_no = iq_no;

	if (m_head->m_pkthdr.csum_flags & CSUM_IP)
		cmdsetup.s.ip_csum = 1;

	if ((m_head->m_pkthdr.csum_flags & (CSUM_IP_TCP | CSUM_IP6_TCP)) ||
	    (m_head->m_pkthdr.csum_flags & (CSUM_IP_UDP | CSUM_IP6_UDP)))
		cmdsetup.s.transport_csum = 1;

	if (nsegs == 1) {
		cmdsetup.s.u.datasize = segs[0].ds_len;
		lio_prepare_pci_cmd(oct, &ndata.cmd, &cmdsetup, tag);

		dptr = segs[0].ds_addr;
		ndata.cmd.cmd3.dptr = dptr;
		ndata.reqtype = LIO_REQTYPE_NORESP_NET;

	} else {
		struct lio_gather	*g;
		int	i;

		mtx_lock(&lio->glist_lock[iq_no]);
		g = (struct lio_gather *)
			lio_delete_first_node(&lio->ghead[iq_no]);
		mtx_unlock(&lio->glist_lock[iq_no]);

		if (g == NULL) {
			lio_dev_err(oct,
				    "Transmit scatter gather: glist null!\n");
			goto retry;
		}

		cmdsetup.s.gather = 1;
		cmdsetup.s.u.gatherptrs = nsegs;
		lio_prepare_pci_cmd(oct, &ndata.cmd, &cmdsetup, tag);

		bzero(g->sg, g->sg_size);

		i = 0;
		while (nsegs--) {
			g->sg[(i >> 2)].ptr[(i & 3)] = segs[i].ds_addr;
			lio_add_sg_size(&g->sg[(i >> 2)], segs[i].ds_len,
					(i & 3));
			i++;
		}

		dptr = g->sg_dma_ptr;

		ndata.cmd.cmd3.dptr = dptr;
		finfo->g = g;

		ndata.reqtype = LIO_REQTYPE_NORESP_NET_SG;
	}

	irh = (struct octeon_instr_irh *)&ndata.cmd.cmd3.irh;
	tx_info = (union lio_tx_info *)&ndata.cmd.cmd3.ossp[0];

	if (m_head->m_pkthdr.csum_flags & (CSUM_IP_TSO | CSUM_IP6_TSO)) {
		tx_info->s.gso_size = m_head->m_pkthdr.tso_segsz;
		tx_info->s.gso_segs = howmany(m_head->m_pkthdr.len,
					      m_head->m_pkthdr.tso_segsz);
		stats->tx_gso++;
	}

	/* HW insert VLAN tag */
	if (m_head->m_flags & M_VLANTAG) {
		irh->priority = m_head->m_pkthdr.ether_vtag >> 13;
		irh->vlan = m_head->m_pkthdr.ether_vtag & 0xfff;
	}

	status = lio_send_data_pkt(oct, &ndata);
	if (status == LIO_IQ_SEND_FAILED)
		goto retry;

	if (tx_info->s.gso_segs)
		stats->tx_done += tx_info->s.gso_segs;
	else
		stats->tx_done++;

	stats->tx_tot_bytes += ndata.datasize;

	return (0);

retry:
	return (ENOBUFS);

drop_packet:
	stats->tx_dropped++;
	lio_dev_err(oct, "IQ%d Transmit dropped: %llu\n", iq_no,
		    LIO_CAST64(stats->tx_dropped));

	m_freem(*m_headp);
	*m_headp = NULL;

	return (status);
}

int
lio_mq_start_locked(struct ifnet *ifp, struct lio_instr_queue *iq)
{
	struct lio	*lio = if_getsoftc(ifp);
	struct mbuf	*next;
	int		err = 0;

	if (((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0) ||
	    (!lio->linfo.link.s.link_up))
		return (-ENETDOWN);

	/* Process the queue */
	while ((next = drbr_peek(ifp, iq->br)) != NULL) {
		err = lio_xmit(lio, iq, &next);
		if (err) {
			if (next == NULL)
				drbr_advance(ifp, iq->br);
			else
				drbr_putback(ifp, iq->br, next);
			break;
		}
		drbr_advance(ifp, iq->br);
		/* Send a copy of the frame to the BPF listener */
		ETHER_BPF_MTAP(ifp, next);
		if (((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0) ||
		    (!lio->linfo.link.s.link_up))
			break;
	}

	return (err);
}

int
lio_mq_start(struct ifnet *ifp, struct mbuf *m)
{
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	struct lio_instr_queue	*iq;
	int	err = 0, i;
#ifdef RSS
	uint32_t	bucket_id;
#endif

	if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE) {
#ifdef RSS
		if (rss_hash2bucket(m->m_pkthdr.flowid, M_HASHTYPE_GET(m),
				    &bucket_id) == 0) {
			i = bucket_id % oct->num_iqs;
			if (bucket_id > oct->num_iqs)
				lio_dev_dbg(oct,
					    "bucket_id (%d) > num_iqs (%d)\n",
					    bucket_id, oct->num_iqs);
		} else
#endif
			i = m->m_pkthdr.flowid % oct->num_iqs;
	} else
		i = curcpu % oct->num_iqs;

	iq = oct->instr_queue[i];

	err = drbr_enqueue(ifp, iq->br, m);
	if (err)
		return (err);

	if (mtx_trylock(&iq->enq_lock)) {
		lio_mq_start_locked(ifp, iq);
		mtx_unlock(&iq->enq_lock);
	}

	return (err);
}

void
lio_qflush(struct ifnet *ifp)
{
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	struct lio_instr_queue	*iq;
	struct mbuf		*m;
	int	i;

	for (i = 0; i < LIO_MAX_INSTR_QUEUES(oct); i++) {
		if (!(oct->io_qmask.iq & BIT_ULL(i)))
			continue;

		iq = oct->instr_queue[i];

		mtx_lock(&iq->enq_lock);
		while ((m = buf_ring_dequeue_sc(iq->br)) != NULL)
			m_freem(m);

		mtx_unlock(&iq->enq_lock);
	}

	if_qflush(ifp);
}
