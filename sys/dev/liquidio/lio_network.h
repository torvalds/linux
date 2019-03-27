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

/* \file  lio_network.h
 * \brief Host NIC Driver: Structure and Macro definitions used by NIC Module.
 */

#ifndef __LIO_NETWORK_H__
#define __LIO_NETWORK_H__

#include "lio_rss.h"

#define LIO_MIN_MTU_SIZE	72
#define LIO_MAX_MTU_SIZE	(LIO_MAX_FRM_SIZE - LIO_FRM_HEADER_SIZE)

#define LIO_MAX_SG		64
#define LIO_MAX_FRAME_SIZE	60000

struct lio_fw_stats_resp {
	uint64_t	rh;
	struct octeon_link_stats stats;
	uint64_t	status;
};

/* LiquidIO per-interface network private data */
struct lio {
	/* State of the interface. Rx/Tx happens only in the RUNNING state.  */
	int	ifstate;

	/*
	 * Octeon Interface index number. This device will be represented as
	 * oct<ifidx> in the system.
	 */
	int	ifidx;

	/* Octeon Input queue to use to transmit for this network interface. */
	int	txq;

	/*
	 * Octeon Output queue from which pkts arrive
	 * for this network interface.
	 */
	int	rxq;

	/* Guards each glist */
	struct mtx	*glist_lock;

#define LIO_DEFAULT_STATS_INTERVAL 10000
	/* callout timer for stats */
	struct callout	stats_timer;

	/* Stats Update Interval in milli Seconds */
	uint16_t	stats_interval;

	/* IRQ coalescing driver stats */
	struct octeon_intrmod_cfg intrmod_cfg;

	/* Array of gather component linked lists */
	struct lio_stailq_head	*ghead;
	void	**glists_virt_base;
	vm_paddr_t	*glists_dma_base;
	uint32_t	glist_entry_size;

	/* Pointer to the octeon device structure. */
	struct octeon_device	*oct_dev;

	struct ifnet	*ifp;
	struct ifmedia	ifmedia;
	int		if_flags;

	/* Link information sent by the core application for this interface. */
	struct octeon_link_info	linfo;

	/* counter of link changes */
	uint64_t	link_changes;

	/* Size of Tx queue for this octeon device. */
	uint32_t	tx_qsize;

	/* Size of Rx queue for this octeon device. */
	uint32_t	rx_qsize;

	/* Size of MTU this octeon device. */
	uint32_t	mtu;

	/* msg level flag per interface. */
	uint32_t	msg_enable;

	/* Interface info */
	uint32_t	intf_open;

	/* task queue for  rx oom status */
	struct lio_tq	rx_status_tq;

	/* VLAN Filtering related */
	eventhandler_tag	vlan_attach;
	eventhandler_tag	vlan_detach;
#ifdef RSS
	struct lio_rss_params_set rss_set;
#endif	/* RSS */
};

#define LIO_MAX_CORES	12

/*
 * \brief Enable or disable feature
 * @param ifp       pointer to network device
 * @param cmd       Command that just requires acknowledgment
 * @param param1    Parameter to command
 */
int	lio_set_feature(struct ifnet *ifp, int cmd, uint16_t param1);

/*
 * \brief Link control command completion callback
 * @param nctrl_ptr pointer to control packet structure
 *
 * This routine is called by the callback function when a ctrl pkt sent to
 * core app completes. The nctrl_ptr contains a copy of the command type
 * and data sent to the core app. This routine is only called if the ctrl
 * pkt was sent successfully to the core app.
 */
void	lio_ctrl_cmd_completion(void *nctrl_ptr);

int	lio_setup_io_queues(struct octeon_device *octeon_dev, int ifidx,
			    uint32_t num_iqs, uint32_t num_oqs);

int	lio_setup_interrupt(struct octeon_device *oct, uint32_t num_ioqs);

static inline void *
lio_recv_buffer_alloc(uint32_t size)
{
	struct mbuf	*mb = NULL;

	mb = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, size);
	if (mb != NULL)
		mb->m_pkthdr.len = mb->m_len = size;

	return ((void *)mb);
}

static inline void
lio_recv_buffer_free(void *buffer)
{

	m_freem((struct mbuf *)buffer);
}

static inline int
lio_get_order(unsigned long size)
{
	int	order;

	size = (size - 1) >> PAGE_SHIFT;
	order = 0;
	while (size) {
		order++;
		size >>= 1;
	}

	return (order);
}

static inline void *
lio_dma_alloc(size_t size, vm_paddr_t *dma_handle)
{
	size_t	align;
	void	*mem;

	align = PAGE_SIZE << lio_get_order(size);
	mem = (void *)kmem_alloc_contig(size, M_WAITOK, 0, ~0ul, align, 0,
	    VM_MEMATTR_DEFAULT);
	if (mem != NULL)
		*dma_handle = vtophys(mem);
	else
		*dma_handle = 0;

	return (mem);
}

static inline void
lio_dma_free(size_t size, void *cpu_addr)
{

	kmem_free((vm_offset_t)cpu_addr, size);
}

static inline uint64_t
lio_map_ring(device_t dev, void *buf, uint32_t size)
{
	vm_paddr_t	dma_addr;

	dma_addr = vtophys(((struct mbuf *)buf)->m_data);
	return ((uint64_t)dma_addr);
}

/*
 * \brief check interface state
 * @param lio per-network private data
 * @param state_flag flag state to check
 */
static inline int
lio_ifstate_check(struct lio *lio, int state_flag)
{

	return (atomic_load_acq_int(&lio->ifstate) & state_flag);
}

/*
 * \brief set interface state
 * @param lio per-network private data
 * @param state_flag flag state to set
 */
static inline void
lio_ifstate_set(struct lio *lio, int state_flag)
{

	atomic_store_rel_int(&lio->ifstate,
			     (atomic_load_acq_int(&lio->ifstate) | state_flag));
}

/*
 * \brief clear interface state
 * @param lio per-network private data
 * @param state_flag flag state to clear
 */
static inline void
lio_ifstate_reset(struct lio *lio, int state_flag)
{

	atomic_store_rel_int(&lio->ifstate,
			     (atomic_load_acq_int(&lio->ifstate) &
			      ~(state_flag)));
}

/*
 * \brief wait for all pending requests to complete
 * @param oct Pointer to Octeon device
 *
 * Called during shutdown sequence
 */
static inline int
lio_wait_for_pending_requests(struct octeon_device *oct)
{
	int	i, pcount = 0;

	for (i = 0; i < 100; i++) {
		pcount = atomic_load_acq_int(
				     &oct->response_list[LIO_ORDERED_SC_LIST].
					     pending_req_count);
		if (pcount)
			lio_sleep_timeout(100);
		else
			break;
	}

	if (pcount)
		return (1);

	return (0);
}

#endif	/* __LIO_NETWORK_H__ */
