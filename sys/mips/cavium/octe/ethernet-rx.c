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
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/taskqueue.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>

#include "wrapper-cvmx-includes.h"
#include "ethernet-headers.h"

extern int pow_receive_group;
extern struct ifnet *cvm_oct_device[];

static struct task cvm_oct_task;
static struct taskqueue *cvm_oct_taskq;

/**
 * Interrupt handler. The interrupt occurs whenever the POW
 * transitions from 0->1 packets in our group.
 *
 * @param cpl
 * @param dev_id
 * @param regs
 * @return
 */
int cvm_oct_do_interrupt(void *dev_id)
{
	/* Acknowledge the interrupt */
	if (INTERRUPT_LIMIT)
		cvmx_write_csr(CVMX_POW_WQ_INT, 1<<pow_receive_group);
	else
		cvmx_write_csr(CVMX_POW_WQ_INT, 0x10001<<pow_receive_group);

	/*
	 * Schedule task.
	 */
	taskqueue_enqueue(cvm_oct_taskq, &cvm_oct_task);

	return FILTER_HANDLED;
}


/**
 * This is called on receive errors, and determines if the packet
 * can be dropped early-on in cvm_oct_tasklet_rx().
 *
 * @param work Work queue entry pointing to the packet.
 * @return Non-zero if the packet can be dropped, zero otherwise.
 */
static inline int cvm_oct_check_rcv_error(cvmx_wqe_t *work)
{
	if ((work->word2.snoip.err_code == 10) && (work->word1.s.len <= 64)) {
		/* Ignore length errors on min size packets. Some equipment
		   incorrectly pads packets to 64+4FCS instead of 60+4FCS.
		   Note these packets still get counted as frame errors. */
	} else
	if (USE_10MBPS_PREAMBLE_WORKAROUND && ((work->word2.snoip.err_code == 5) || (work->word2.snoip.err_code == 7))) {

		/* We received a packet with either an alignment error or a
		   FCS error. This may be signalling that we are running
		   10Mbps with GMXX_RXX_FRM_CTL[PRE_CHK} off. If this is the
		   case we need to parse the packet to determine if we can
		   remove a non spec preamble and generate a correct packet */
		int interface = cvmx_helper_get_interface_num(work->word1.cn38xx.ipprt);
		int index = cvmx_helper_get_interface_index_num(work->word1.cn38xx.ipprt);
		cvmx_gmxx_rxx_frm_ctl_t gmxx_rxx_frm_ctl;
		gmxx_rxx_frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(index, interface));
		if (gmxx_rxx_frm_ctl.s.pre_chk == 0) {

			uint8_t *ptr = cvmx_phys_to_ptr(work->packet_ptr.s.addr);
			int i = 0;

			while (i < work->word1.s.len-1) {
				if (*ptr != 0x55)
					break;
				ptr++;
				i++;
			}

			if (*ptr == 0xd5) {
				/*
				DEBUGPRINT("Port %d received 0xd5 preamble\n", work->word1.cn38xx.ipprt);
				*/
				work->packet_ptr.s.addr += i+1;
				work->word1.s.len -= i+5;
			} else
			if ((*ptr & 0xf) == 0xd) {
				/*
				DEBUGPRINT("Port %d received 0x?d preamble\n", work->word1.cn38xx.ipprt);
				*/
				work->packet_ptr.s.addr += i;
				work->word1.s.len -= i+4;
				for (i = 0; i < work->word1.s.len; i++) {
					*ptr = ((*ptr&0xf0)>>4) | ((*(ptr+1)&0xf)<<4);
					ptr++;
				}
			} else {
				DEBUGPRINT("Port %d unknown preamble, packet dropped\n", work->word1.cn38xx.ipprt);
				/*
				cvmx_helper_dump_packet(work);
				*/
				cvm_oct_free_work(work);
				return 1;
			}
		}
	} else {
		DEBUGPRINT("Port %d receive error code %d, packet dropped\n", work->word1.cn38xx.ipprt, work->word2.snoip.err_code);
		cvm_oct_free_work(work);
		return 1;
	}

	return 0;
}

/**
 * Tasklet function that is scheduled on a core when an interrupt occurs.
 *
 * @param unused
 */
void cvm_oct_tasklet_rx(void *context, int pending)
{
	int                 coreid;
	uint64_t            old_group_mask;
	int                 rx_count = 0;
	int                 number_to_free;
	int                 num_freed;
	int                 packet_not_copied;

	coreid = cvmx_get_core_num();

	/* Prefetch cvm_oct_device since we know we need it soon */
	CVMX_PREFETCH(cvm_oct_device, 0);

	/* Only allow work for our group (and preserve priorities) */
	old_group_mask = cvmx_read_csr(CVMX_POW_PP_GRP_MSKX(coreid));
	cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(coreid),
		       (old_group_mask & ~0xFFFFull) | 1<<pow_receive_group);

	while (1) {
		struct mbuf *m = NULL;
		int mbuf_in_hw;
		cvmx_wqe_t *work;

		if ((INTERRUPT_LIMIT == 0) || (rx_count < MAX_RX_PACKETS))
			work = cvmx_pow_work_request_sync(CVMX_POW_NO_WAIT);
		else
			work = NULL;
		CVMX_PREFETCH(work, 0);
		if (work == NULL)
			break;

		mbuf_in_hw = work->word2.s.bufs == 1;
		if ((mbuf_in_hw)) {
			m = *(struct mbuf **)(cvm_oct_get_buffer_ptr(work->packet_ptr) - sizeof(void *));
			CVMX_PREFETCH(m, offsetof(struct mbuf, m_data));
			CVMX_PREFETCH(m, offsetof(struct mbuf, m_pkthdr));
		}
		CVMX_PREFETCH(cvm_oct_device[work->word1.cn38xx.ipprt], 0);
		//CVMX_PREFETCH(m, 0);


		rx_count++;
		/* Immediately throw away all packets with receive errors */
		if ((work->word2.snoip.rcv_error)) {
			if (cvm_oct_check_rcv_error(work))
				continue;
		}

		/* We can only use the zero copy path if mbufs are in the FPA pool
		   and the packet fits in a single buffer */
		if ((mbuf_in_hw)) {
			CVMX_PREFETCH(m->m_data, 0);

			m->m_pkthdr.len = m->m_len = work->word1.s.len;

			packet_not_copied = 1;

			/*
			 * Adjust the data pointer based on the offset
			 * of the packet within the buffer.
			 */
			m->m_data += (work->packet_ptr.s.back << 7) + (work->packet_ptr.s.addr & 0x7f);
		} else {

			/* We have to copy the packet. First allocate an
			   mbuf for it */
			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m == NULL) {
				DEBUGPRINT("Port %d failed to allocate mbuf, packet dropped\n", work->word1.cn38xx.ipprt);
				cvm_oct_free_work(work);
				continue;
			}

			/* Check if we've received a packet that was entirely
			   stored in the work entry. This is untested */
			if ((work->word2.s.bufs == 0)) {
				uint8_t *ptr = work->packet_data;

				if (cvmx_likely(!work->word2.s.not_IP)) {
					/* The beginning of the packet moves
					   for IP packets */
					if (work->word2.s.is_v6)
						ptr += 2;
					else
						ptr += 6;
				}
				panic("%s: not yet implemented; copy in small packet.", __func__);
				/* No packet buffers to free */
			} else {
				int segments = work->word2.s.bufs;
				cvmx_buf_ptr_t segment_ptr = work->packet_ptr;
				int len = work->word1.s.len;

				while (segments--) {
					cvmx_buf_ptr_t next_ptr = *(cvmx_buf_ptr_t *)cvmx_phys_to_ptr(segment_ptr.s.addr-8);
					/* Octeon Errata PKI-100: The segment
					   size is wrong. Until it is fixed,
					   calculate the segment size based on
					   the packet pool buffer size. When
					   it is fixed, the following line
					   should be replaced with this one:
					int segment_size = segment_ptr.s.size; */
					int segment_size = CVMX_FPA_PACKET_POOL_SIZE - (segment_ptr.s.addr - (((segment_ptr.s.addr >> 7) - segment_ptr.s.back) << 7));
					/* Don't copy more than what is left
					   in the packet */
					if (segment_size > len)
						segment_size = len;
					/* Copy the data into the packet */
					panic("%s: not yet implemented; copy in packet segments.", __func__);
#if 0
					memcpy(m_put(m, segment_size), cvmx_phys_to_ptr(segment_ptr.s.addr), segment_size);
#endif
					/* Reduce the amount of bytes left
					   to copy */
					len -= segment_size;
					segment_ptr = next_ptr;
				}
			}
			packet_not_copied = 0;
		}

		if (((work->word1.cn38xx.ipprt < TOTAL_NUMBER_OF_PORTS) &&
		    cvm_oct_device[work->word1.cn38xx.ipprt])) {
			struct ifnet *ifp = cvm_oct_device[work->word1.cn38xx.ipprt];

			/* Only accept packets for devices
			   that are currently up */
			if ((ifp->if_flags & IFF_UP)) {
				m->m_pkthdr.rcvif = ifp;

				if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
					if ((work->word2.s.not_IP || work->word2.s.IP_exc || work->word2.s.L4_error))
						m->m_pkthdr.csum_flags = 0; /* XXX */
					else {
						m->m_pkthdr.csum_flags = CSUM_IP_CHECKED | CSUM_IP_VALID | CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
						m->m_pkthdr.csum_data = 0xffff;
					}
				} else {
					m->m_pkthdr.csum_flags = 0; /* XXX */
				}

				if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

				(*ifp->if_input)(ifp, m);
			} else {
				/* Drop any packet received for a device that isn't up */
				/*
				DEBUGPRINT("%s: Device not up, packet dropped\n",
					   if_name(ifp));
				*/
				m_freem(m);
			}
		} else {
			/* Drop any packet received for a device that
			   doesn't exist */
			DEBUGPRINT("Port %d not controlled by FreeBSD, packet dropped\n", work->word1.cn38xx.ipprt);
			m_freem(m);
		}

		/* Check to see if the mbuf and work share
		   the same packet buffer */
		if ((packet_not_copied)) {
			/* This buffer needs to be replaced, increment
			the number of buffers we need to free by one */
			cvmx_fau_atomic_add32(
				FAU_NUM_PACKET_BUFFERS_TO_FREE, 1);

			cvmx_fpa_free(work, CVMX_FPA_WQE_POOL,
				      DONT_WRITEBACK(1));
		} else
			cvm_oct_free_work(work);
	}

	/*
	 * If we hit our limit, schedule another task while we clean up.
	 */
	if (INTERRUPT_LIMIT != 0 && rx_count == MAX_RX_PACKETS) {
		taskqueue_enqueue(cvm_oct_taskq, &cvm_oct_task);
	}

	/* Restore the original POW group mask */
	cvmx_write_csr(CVMX_POW_PP_GRP_MSKX(coreid), old_group_mask);

	/* Refill the packet buffer pool */
	number_to_free =
	  cvmx_fau_fetch_and_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE, 0);

	if (number_to_free > 0) {
		cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
				      -number_to_free);
		num_freed =
			cvm_oct_mem_fill_fpa(CVMX_FPA_PACKET_POOL,
					     CVMX_FPA_PACKET_POOL_SIZE,
					     number_to_free);
		if (num_freed != number_to_free) {
			cvmx_fau_atomic_add32(FAU_NUM_PACKET_BUFFERS_TO_FREE,
					      number_to_free - num_freed);
		}
	}
}



void cvm_oct_rx_initialize(void)
{
	int cpu;
	TASK_INIT(&cvm_oct_task, 0, cvm_oct_tasklet_rx, NULL);

	cvm_oct_taskq = taskqueue_create_fast("oct_rx", M_NOWAIT,
					      taskqueue_thread_enqueue,
					      &cvm_oct_taskq);

	CPU_FOREACH(cpu) {
		cpuset_t cpu_mask;
		CPU_SETOF(cpu, &cpu_mask);
		taskqueue_start_threads_cpuset(&cvm_oct_taskq, 1, PI_NET,
		    &cpu_mask, "octe taskq");
	}
}

void cvm_oct_rx_shutdown(void)
{
	panic("%s: not yet implemented.", __func__);
}

