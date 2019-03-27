/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "xgbe.h"
#include "xgbe-common.h"

static void xgbe_unmap_rdata(struct xgbe_prv_data *, struct xgbe_ring_data *);

static void xgbe_free_ring(struct xgbe_prv_data *pdata,
			   struct xgbe_ring *ring)
{
	struct xgbe_ring_data *rdata;
	unsigned int i;

	if (!ring)
		return;

	bus_dmamap_destroy(ring->mbuf_dmat, ring->mbuf_map);
	bus_dma_tag_destroy(ring->mbuf_dmat);

	ring->mbuf_map = NULL;
	ring->mbuf_dmat = NULL;

	if (ring->rdata) {
		for (i = 0; i < ring->rdesc_count; i++) {
			rdata = XGBE_GET_DESC_DATA(ring, i);
			xgbe_unmap_rdata(pdata, rdata);
		}

		free(ring->rdata, M_AXGBE);
		ring->rdata = NULL;
	}

	bus_dmamap_unload(ring->rdesc_dmat, ring->rdesc_map);
	bus_dmamem_free(ring->rdesc_dmat, ring->rdesc, ring->rdesc_map);
	bus_dma_tag_destroy(ring->rdesc_dmat);

	ring->rdesc_map = NULL;
	ring->rdesc_dmat = NULL;
	ring->rdesc = NULL;
}

static void xgbe_free_ring_resources(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;

	DBGPR("-->xgbe_free_ring_resources\n");

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		xgbe_free_ring(pdata, channel->tx_ring);
		xgbe_free_ring(pdata, channel->rx_ring);
	}

	DBGPR("<--xgbe_free_ring_resources\n");
}

static void xgbe_ring_dmamap_cb(void *arg, bus_dma_segment_t * segs, int nseg,
                                int error)
{
	if (error)
		return;
	*(bus_addr_t *) arg = segs->ds_addr;
}

static int xgbe_init_ring(struct xgbe_prv_data *pdata,
			  struct xgbe_ring *ring, unsigned int rdesc_count)
{
	bus_size_t len;
	int err, flags;

	DBGPR("-->xgbe_init_ring\n");

	if (!ring)
		return 0;

	flags = 0;
	if (pdata->coherent)
		flags = BUS_DMA_COHERENT;

	/* Descriptors */
	ring->rdesc_count = rdesc_count;
	len = sizeof(struct xgbe_ring_desc) * rdesc_count;
	err = bus_dma_tag_create(pdata->dmat, 512, 0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, len, 1, len, flags, NULL, NULL,
	    &ring->rdesc_dmat);
	if (err != 0) {
		printf("Unable to create the DMA tag: %d\n", err);
		return -err;
	}

	err = bus_dmamem_alloc(ring->rdesc_dmat, (void **)&ring->rdesc,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT, &ring->rdesc_map);
	if (err != 0) {
		bus_dma_tag_destroy(ring->rdesc_dmat);
		printf("Unable to allocate DMA memory: %d\n", err);
		return -err;
	}
	err = bus_dmamap_load(ring->rdesc_dmat, ring->rdesc_map, ring->rdesc,
	    len, xgbe_ring_dmamap_cb, &ring->rdesc_paddr, 0);
	if (err != 0) {
		bus_dmamem_free(ring->rdesc_dmat, ring->rdesc, ring->rdesc_map);
		bus_dma_tag_destroy(ring->rdesc_dmat);
		printf("Unable to load DMA memory\n");
		return -err;
	}

	/* Descriptor information */
	ring->rdata = malloc(rdesc_count * sizeof(struct xgbe_ring_data),
	    M_AXGBE, M_WAITOK | M_ZERO);

	/* Create the space DMA tag for mbufs */
	err = bus_dma_tag_create(pdata->dmat, 1, 0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, XGBE_TX_MAX_BUF_SIZE * rdesc_count,
	    rdesc_count, XGBE_TX_MAX_BUF_SIZE, flags, NULL, NULL,
	    &ring->mbuf_dmat);
	if (err != 0)
		return -err;

	err = bus_dmamap_create(ring->mbuf_dmat, 0, &ring->mbuf_map);
	if (err != 0)
		return -err;

	DBGPR("<--xgbe_init_ring\n");

	return 0;
}

static int xgbe_alloc_ring_resources(struct xgbe_prv_data *pdata)
{
	struct xgbe_channel *channel;
	unsigned int i;
	int ret;

	DBGPR("-->xgbe_alloc_ring_resources\n");

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		ret = xgbe_init_ring(pdata, channel->tx_ring,
				     pdata->tx_desc_count);
		if (ret) {
			printf("error initializing Tx ring\n");
			goto err_ring;
		}

		ret = xgbe_init_ring(pdata, channel->rx_ring,
				     pdata->rx_desc_count);
		if (ret) {
			printf("error initializing Rx ring\n");
			goto err_ring;
		}
	}

	DBGPR("<--xgbe_alloc_ring_resources\n");

	return 0;

err_ring:
	xgbe_free_ring_resources(pdata);

	return ret;
}

static int xgbe_map_rx_buffer(struct xgbe_prv_data *pdata,
			      struct xgbe_ring *ring,
			      struct xgbe_ring_data *rdata)
{
	bus_dmamap_t mbuf_map;
	bus_dma_segment_t segs[2];
	struct mbuf *m0, *m1;
	int err, nsegs;

	m0 = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MCLBYTES);
	if (m0 == NULL)
		return (-ENOBUFS);

	m1 = m_getjcl(M_NOWAIT, MT_DATA, 0, MCLBYTES);
	if (m1 == NULL) {
		m_freem(m0);
		return (-ENOBUFS);
	}

	m0->m_next = m1;
	m0->m_flags |= M_PKTHDR;
	m0->m_len = MHLEN;
	m0->m_pkthdr.len = MHLEN + MCLBYTES;

	m1->m_len = MCLBYTES;
	m1->m_next = NULL;
	m1->m_pkthdr.len = MCLBYTES;

	err = bus_dmamap_create(ring->mbuf_dmat, 0, &mbuf_map);
	if (err != 0) {
		m_freem(m0);
		return (-err);
	}

	err = bus_dmamap_load_mbuf_sg(ring->mbuf_dmat, mbuf_map, m0, segs,
	    &nsegs, BUS_DMA_NOWAIT);
	if (err != 0) {
		m_freem(m0);
		bus_dmamap_destroy(ring->mbuf_dmat, mbuf_map);
		return (-err);
	}

	KASSERT(nsegs == 2,
	    ("xgbe_map_rx_buffer: Unable to handle multiple segments %d",
	    nsegs));

	rdata->mb = m0;
	rdata->mbuf_free = 0;
	rdata->mbuf_dmat = ring->mbuf_dmat;
	rdata->mbuf_map = mbuf_map;
	rdata->mbuf_hdr_paddr = segs[0].ds_addr;
	rdata->mbuf_data_paddr = segs[1].ds_addr;

	return 0;
}

static void xgbe_wrapper_tx_descriptor_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_channel *channel;
	struct xgbe_ring *ring;
	struct xgbe_ring_data *rdata;
	struct xgbe_ring_desc *rdesc;
	bus_addr_t rdesc_paddr;
	unsigned int i, j;

	DBGPR("-->xgbe_wrapper_tx_descriptor_init\n");

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		ring = channel->tx_ring;
		if (!ring)
			break;

		rdesc = ring->rdesc;
		rdesc_paddr = ring->rdesc_paddr;

		for (j = 0; j < ring->rdesc_count; j++) {
			rdata = XGBE_GET_DESC_DATA(ring, j);

			rdata->rdesc = rdesc;
			rdata->rdata_paddr = rdesc_paddr;

			rdesc++;
			rdesc_paddr += sizeof(struct xgbe_ring_desc);
		}

		ring->cur = 0;
		ring->dirty = 0;
		memset(&ring->tx, 0, sizeof(ring->tx));

		hw_if->tx_desc_init(channel);
	}

	DBGPR("<--xgbe_wrapper_tx_descriptor_init\n");
}

static void xgbe_wrapper_rx_descriptor_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_hw_if *hw_if = &pdata->hw_if;
	struct xgbe_channel *channel;
	struct xgbe_ring *ring;
	struct xgbe_ring_desc *rdesc;
	struct xgbe_ring_data *rdata;
	bus_addr_t rdesc_paddr;
	unsigned int i, j;

	DBGPR("-->xgbe_wrapper_rx_descriptor_init\n");

	channel = pdata->channel;
	for (i = 0; i < pdata->channel_count; i++, channel++) {
		ring = channel->rx_ring;
		if (!ring)
			break;

		rdesc = ring->rdesc;
		rdesc_paddr = ring->rdesc_paddr;

		for (j = 0; j < ring->rdesc_count; j++) {
			rdata = XGBE_GET_DESC_DATA(ring, j);

			rdata->rdesc = rdesc;
			rdata->rdata_paddr = rdesc_paddr;

			if (xgbe_map_rx_buffer(pdata, ring, rdata))
				break;

			rdesc++;
			rdesc_paddr += sizeof(struct xgbe_ring_desc);
		}

		ring->cur = 0;
		ring->dirty = 0;

		hw_if->rx_desc_init(channel);
	}
}

static void xgbe_unmap_rdata(struct xgbe_prv_data *pdata,
			     struct xgbe_ring_data *rdata)
{

	if (rdata->mbuf_map != NULL)
		bus_dmamap_destroy(rdata->mbuf_dmat, rdata->mbuf_map);

	if (rdata->mbuf_free)
		m_freem(rdata->mb);

	rdata->mb = NULL;
	rdata->mbuf_free = 0;
	rdata->mbuf_hdr_paddr = 0;
	rdata->mbuf_data_paddr = 0;
	rdata->mbuf_len = 0;

	memset(&rdata->tx, 0, sizeof(rdata->tx));
	memset(&rdata->rx, 0, sizeof(rdata->rx));
}

struct xgbe_map_tx_skb_data {
	struct xgbe_ring *ring;
	struct xgbe_packet_data *packet;
	unsigned int cur_index;
};

static void xgbe_map_tx_skb_cb(void *callback_arg, bus_dma_segment_t *segs,
    int nseg, bus_size_t mapsize, int error)
{
	struct xgbe_map_tx_skb_data *data;
	struct xgbe_ring_data *rdata;
	struct xgbe_ring *ring;
	int i;

	if (error != 0)
		return;

	data = callback_arg;
	ring = data->ring;

	for (i = 0; i < nseg; i++) {
		rdata = XGBE_GET_DESC_DATA(ring, data->cur_index);

		KASSERT(segs[i].ds_len <= XGBE_TX_MAX_BUF_SIZE,
		    ("%s: Segment size is too large %ld > %d", __func__,
		    segs[i].ds_len, XGBE_TX_MAX_BUF_SIZE));

		if (i == 0) {
			rdata->mbuf_dmat = ring->mbuf_dmat;
			bus_dmamap_create(ring->mbuf_dmat, 0, &ring->mbuf_map);
		}

		rdata->mbuf_hdr_paddr = 0;
		rdata->mbuf_data_paddr = segs[i].ds_addr;
		rdata->mbuf_len = segs[i].ds_len;

		data->packet->length += rdata->mbuf_len;

		data->cur_index++;
	}
}

static int xgbe_map_tx_skb(struct xgbe_channel *channel, struct mbuf *m)
{
	struct xgbe_ring *ring = channel->tx_ring;
	struct xgbe_map_tx_skb_data cbdata;
	struct xgbe_ring_data *rdata;
	struct xgbe_packet_data *packet;
	unsigned int start_index, cur_index;
	int err;

	DBGPR("-->xgbe_map_tx_skb: cur = %d\n", ring->cur);

	start_index = ring->cur;
	cur_index = ring->cur;

	packet = &ring->packet_data;
	packet->rdesc_count = 0;
	packet->length = 0;

	cbdata.ring = ring;
	cbdata.packet = packet;
	cbdata.cur_index = cur_index;

	err = bus_dmamap_load_mbuf(ring->mbuf_dmat, ring->mbuf_map, m,
	    xgbe_map_tx_skb_cb, &cbdata, BUS_DMA_NOWAIT);
	if (err != 0) /* TODO: Undo the mapping */
		return (-err);

	cur_index = cbdata.cur_index;

	/* Save the mbuf address in the last entry. We always have some data
	 * that has been mapped so rdata is always advanced past the last
	 * piece of mapped data - use the entry pointed to by cur_index - 1.
	 */
	rdata = XGBE_GET_DESC_DATA(ring, cur_index - 1);
	rdata->mb = m;
	rdata->mbuf_free = 1;

	/* Save the number of descriptor entries used */
	packet->rdesc_count = cur_index - start_index;

	DBGPR("<--xgbe_map_tx_skb: count=%u\n", packet->rdesc_count);

	return packet->rdesc_count;
}

void xgbe_init_function_ptrs_desc(struct xgbe_desc_if *desc_if)
{
	DBGPR("-->xgbe_init_function_ptrs_desc\n");

	desc_if->alloc_ring_resources = xgbe_alloc_ring_resources;
	desc_if->free_ring_resources = xgbe_free_ring_resources;
	desc_if->map_tx_skb = xgbe_map_tx_skb;
	desc_if->map_rx_buffer = xgbe_map_rx_buffer;
	desc_if->unmap_rdata = xgbe_unmap_rdata;
	desc_if->wrapper_tx_desc_init = xgbe_wrapper_tx_descriptor_init;
	desc_if->wrapper_rx_desc_init = xgbe_wrapper_rx_descriptor_init;

	DBGPR("<--xgbe_init_function_ptrs_desc\n");
}
