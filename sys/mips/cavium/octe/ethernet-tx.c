/*************************************************************************
SPDX-License-Identifier: BSD-3-Clause

Copyright (c) 2003-2007  Cavium Networks (support@cavium.com). All rights
reserved.


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Cavium Networks nor the names of
      its contributors may be used to endorse or promote products
      derived from this software without specific prior written
      permission.

This Software, including technical data, may be subject to U.S. export  control laws, including the U.S. Export Administration Act and its  associated regulations, and may be subject to export or import  regulations in other countries.

TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
AND WITH ALL FAULTS AND CAVIUM  NETWORKS MAKES NO PROMISES, REPRESENTATIONS OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES WITH YOU.

*************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>

#include "wrapper-cvmx-includes.h"
#include "ethernet-headers.h"

/* You can define GET_MBUF_QOS() to override how the mbuf output function
   determines which output queue is used. The default implementation
   always uses the base queue for the port. If, for example, you wanted
   to use the m->priority fieid, define GET_MBUF_QOS as:
   #define GET_MBUF_QOS(m) ((m)->priority) */
#ifndef GET_MBUF_QOS
    #define GET_MBUF_QOS(m) 0
#endif


/**
 * Packet transmit
 *
 * @param m    Packet to send
 * @param dev    Device info structure
 * @return Always returns zero
 */
int cvm_oct_xmit(struct mbuf *m, struct ifnet *ifp)
{
	cvmx_pko_command_word0_t    pko_command;
	cvmx_buf_ptr_t              hw_buffer;
	int                         dropped;
	int                         qos;
	cvm_oct_private_t          *priv = (cvm_oct_private_t *)ifp->if_softc;
	int32_t in_use;
	int32_t buffers_to_free;
	cvmx_wqe_t *work;

	/* Prefetch the private data structure.
	   It is larger that one cache line */
	CVMX_PREFETCH(priv, 0);

	/* Start off assuming no drop */
	dropped = 0;

	/* The check on CVMX_PKO_QUEUES_PER_PORT_* is designed to completely
	   remove "qos" in the event neither interface supports multiple queues
	   per port */
	if ((CVMX_PKO_QUEUES_PER_PORT_INTERFACE0 > 1) ||
	    (CVMX_PKO_QUEUES_PER_PORT_INTERFACE1 > 1)) {
		qos = GET_MBUF_QOS(m);
		if (qos <= 0)
			qos = 0;
		else if (qos >= cvmx_pko_get_num_queues(priv->port))
			qos = 0;
	} else
		qos = 0;

	/* The CN3XXX series of parts has an errata (GMX-401) which causes the
	   GMX block to hang if a collision occurs towards the end of a
	   <68 byte packet. As a workaround for this, we pad packets to be
	   68 bytes whenever we are in half duplex mode. We don't handle
	   the case of having a small packet but no room to add the padding.
	   The kernel should always give us at least a cache line */
	if (__predict_false(m->m_pkthdr.len < 64) && OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
		cvmx_gmxx_prtx_cfg_t gmx_prt_cfg;
		int interface = INTERFACE(priv->port);
		int index = INDEX(priv->port);

		if (interface < 2) {
			/* We only need to pad packet in half duplex mode */
			gmx_prt_cfg.u64 = cvmx_read_csr(CVMX_GMXX_PRTX_CFG(index, interface));
			if (gmx_prt_cfg.s.duplex == 0) {
				static uint8_t pad[64];

				if (!m_append(m, sizeof pad - m->m_pkthdr.len, pad))
					printf("%s: unable to padd small packet.", __func__);
			}
		}
	}

#ifdef OCTEON_VENDOR_RADISYS
	/*
	 * The RSYS4GBE will hang if asked to transmit a packet less than 60 bytes.
	 */
	if (__predict_false(m->m_pkthdr.len < 60) &&
	    cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_CUST_RADISYS_RSYS4GBE) {
		static uint8_t pad[60];

		if (!m_append(m, sizeof pad - m->m_pkthdr.len, pad))
			printf("%s: unable to pad small packet.", __func__);
	}
#endif

	/*
	 * If the packet is not fragmented.
	 */
	if (m->m_pkthdr.len == m->m_len) {
		/* Build the PKO buffer pointer */
		hw_buffer.u64 = 0;
		hw_buffer.s.addr = cvmx_ptr_to_phys(m->m_data);
		hw_buffer.s.pool = 0;
		hw_buffer.s.size = m->m_len;

		/* Build the PKO command */
		pko_command.u64 = 0;
		pko_command.s.segs = 1;
		pko_command.s.dontfree = 1; /* Do not put this buffer into the FPA.  */

		work = NULL;
	} else {
		struct mbuf *n;
		unsigned segs;
		uint64_t *gp;

		/*
		 * The packet is fragmented, we need to send a list of segments
		 * in memory we borrow from the WQE pool.
		 */
		work = cvmx_fpa_alloc(CVMX_FPA_WQE_POOL);
		if (work == NULL) {
			m_freem(m);
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			return 1;
		}

		segs = 0;
		gp = (uint64_t *)work;
		for (n = m; n != NULL; n = n->m_next) {
			if (segs == CVMX_FPA_WQE_POOL_SIZE / sizeof (uint64_t))
				panic("%s: too many segments in packet; call m_collapse().", __func__);

			/* Build the PKO buffer pointer */
			hw_buffer.u64 = 0;
			hw_buffer.s.i = 1; /* Do not put this buffer into the FPA.  */
			hw_buffer.s.addr = cvmx_ptr_to_phys(n->m_data);
			hw_buffer.s.pool = 0;
			hw_buffer.s.size = n->m_len;

			*gp++ = hw_buffer.u64;
			segs++;
		}

		/* Build the PKO buffer gather list pointer */
		hw_buffer.u64 = 0;
		hw_buffer.s.addr = cvmx_ptr_to_phys(work);
		hw_buffer.s.pool = CVMX_FPA_WQE_POOL;
		hw_buffer.s.size = segs;

		/* Build the PKO command */
		pko_command.u64 = 0;
		pko_command.s.segs = segs;
		pko_command.s.gather = 1;
		pko_command.s.dontfree = 0; /* Put the WQE above back into the FPA.  */
	}

	/* Finish building the PKO command */
	pko_command.s.n2 = 1; /* Don't pollute L2 with the outgoing packet */
	pko_command.s.reg0 = priv->fau+qos*4;
	pko_command.s.total_bytes = m->m_pkthdr.len;
	pko_command.s.size0 = CVMX_FAU_OP_SIZE_32;
	pko_command.s.subone0 = 1;

	/* Check if we can use the hardware checksumming */
	if ((m->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP)) != 0) {
		/* Use hardware checksum calc */
		pko_command.s.ipoffp1 = ETHER_HDR_LEN + 1;
	}

	/*
	 * XXX
	 * Could use a different free queue (and different FAU address) per
	 * core instead of per QoS, to reduce contention here.
	 */
	IF_LOCK(&priv->tx_free_queue[qos]);
	/* Get the number of mbufs in use by the hardware */
	in_use = cvmx_fau_fetch_and_add32(priv->fau+qos*4, 1);
	buffers_to_free = cvmx_fau_fetch_and_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);

	cvmx_pko_send_packet_prepare(priv->port, priv->queue + qos, CVMX_PKO_LOCK_CMD_QUEUE);

	/* Drop this packet if we have too many already queued to the HW */
	if (_IF_QFULL(&priv->tx_free_queue[qos])) {
		dropped = 1;
	}
	/* Send the packet to the output queue */
	else
	if (__predict_false(cvmx_pko_send_packet_finish(priv->port, priv->queue + qos, pko_command, hw_buffer, CVMX_PKO_LOCK_CMD_QUEUE))) {
		DEBUGPRINT("%s: Failed to send the packet\n", if_name(ifp));
		dropped = 1;
	}

	if (__predict_false(dropped)) {
		m_freem(m);
		cvmx_fau_atomic_add32(priv->fau+qos*4, -1);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	} else {
		/* Put this packet on the queue to be freed later */
		_IF_ENQUEUE(&priv->tx_free_queue[qos], m);

		/* Pass it to any BPF listeners.  */
		ETHER_BPF_MTAP(ifp, m);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if_inc_counter(ifp, IFCOUNTER_OBYTES, m->m_pkthdr.len);
	}

	/* Free mbufs not in use by the hardware */
	if (_IF_QLEN(&priv->tx_free_queue[qos]) > in_use) {
		while (_IF_QLEN(&priv->tx_free_queue[qos]) > in_use) {
			_IF_DEQUEUE(&priv->tx_free_queue[qos], m);
			m_freem(m);
		}
	}
	IF_UNLOCK(&priv->tx_free_queue[qos]);

	return dropped;
}


/**
 * This function frees all mbufs that are currenty queued for TX.
 *
 * @param dev    Device being shutdown
 */
void cvm_oct_tx_shutdown(struct ifnet *ifp)
{
	cvm_oct_private_t *priv = (cvm_oct_private_t *)ifp->if_softc;
	int qos;

	for (qos = 0; qos < 16; qos++) {
		IF_DRAIN(&priv->tx_free_queue[qos]);
	}
}
