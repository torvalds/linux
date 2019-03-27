/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#include "ixgbe.h"

#ifdef IXGBE_FDIR

void
ixgbe_init_fdir(struct adapter *adapter)
{
	u32 hdrm = 32 << fdir_pballoc;

	if (!(adapter->feat_en & IXGBE_FEATURE_FDIR))
		return;

	adapter->hw.mac.ops.setup_rxpba(&adapter->hw, 0, hdrm,
	    PBA_STRATEGY_EQUAL);
	ixgbe_init_fdir_signature_82599(&adapter->hw, fdir_pballoc);
} /* ixgbe_init_fdir */

void
ixgbe_reinit_fdir(void *context)
{
	if_ctx_t       ctx = context;
	struct adapter *adapter = iflib_get_softc(ctx);
	struct ifnet   *ifp = iflib_get_ifp(ctx);

	if (!(adapter->feat_en & IXGBE_FEATURE_FDIR))
		return;
	if (adapter->fdir_reinit != 1) /* Shouldn't happen */
		return;
	ixgbe_reinit_fdir_tables_82599(&adapter->hw);
	adapter->fdir_reinit = 0;
	/* re-enable flow director interrupts */
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_EIMS, IXGBE_EIMS_FLOW_DIR);
	/* Restart the interface */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
} /* ixgbe_reinit_fdir */

/************************************************************************
 * ixgbe_atr
 *
 *   Parse packet headers so that Flow Director can make
 *   a hashed filter table entry allowing traffic flows
 *   to be identified and kept on the same cpu.  This
 *   would be a performance hit, but we only do it at
 *   IXGBE_FDIR_RATE of packets.
 ************************************************************************/
void
ixgbe_atr(struct tx_ring *txr, struct mbuf *mp)
{
	struct adapter             *adapter = txr->adapter;
	struct ix_queue            *que;
	struct ip                  *ip;
	struct tcphdr              *th;
	struct udphdr              *uh;
	struct ether_vlan_header   *eh;
	union ixgbe_atr_hash_dword input = {.dword = 0};
	union ixgbe_atr_hash_dword common = {.dword = 0};
	int                        ehdrlen, ip_hlen;
	u16                        etype;

	eh = mtod(mp, struct ether_vlan_header *);
	if (eh->evl_encap_proto == htons(ETHERTYPE_VLAN)) {
		ehdrlen = ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
		etype = eh->evl_proto;
	} else {
		ehdrlen = ETHER_HDR_LEN;
		etype = eh->evl_encap_proto;
	}

	/* Only handling IPv4 */
	if (etype != htons(ETHERTYPE_IP))
		return;

	ip = (struct ip *)(mp->m_data + ehdrlen);
	ip_hlen = ip->ip_hl << 2;

	/* check if we're UDP or TCP */
	switch (ip->ip_p) {
	case IPPROTO_TCP:
		th = (struct tcphdr *)((caddr_t)ip + ip_hlen);
		/* src and dst are inverted */
		common.port.dst ^= th->th_sport;
		common.port.src ^= th->th_dport;
		input.formatted.flow_type ^= IXGBE_ATR_FLOW_TYPE_TCPV4;
		break;
	case IPPROTO_UDP:
		uh = (struct udphdr *)((caddr_t)ip + ip_hlen);
		/* src and dst are inverted */
		common.port.dst ^= uh->uh_sport;
		common.port.src ^= uh->uh_dport;
		input.formatted.flow_type ^= IXGBE_ATR_FLOW_TYPE_UDPV4;
		break;
	default:
		return;
	}

	input.formatted.vlan_id = htobe16(mp->m_pkthdr.ether_vtag);
	if (mp->m_pkthdr.ether_vtag)
		common.flex_bytes ^= htons(ETHERTYPE_VLAN);
	else
		common.flex_bytes ^= etype;
	common.ip ^= ip->ip_src.s_addr ^ ip->ip_dst.s_addr;

	que = &adapter->queues[txr->me];
	/*
	 * This assumes the Rx queue and Tx
	 * queue are bound to the same CPU
	 */
	ixgbe_fdir_add_signature_filter_82599(&adapter->hw,
	    input, common, que->msix);
} /* ixgbe_atr */

#else

/* TASK_INIT needs this function defined regardless if it's enabled */
void
ixgbe_reinit_fdir(void *context)
{
	UNREFERENCED_PARAMETER(context);
} /* ixgbe_reinit_fdir */

void
ixgbe_atr(struct tx_ring *txr, struct mbuf *mp)
{
	UNREFERENCED_2PARAMETER(txr, mp);
} /* ixgbe_atr */

#endif
