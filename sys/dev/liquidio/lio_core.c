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
#include "lio_rxtx.h"
#include "lio_network.h"

int
lio_set_feature(struct ifnet *ifp, int cmd, uint16_t param1)
{
	struct lio_ctrl_pkt	nctrl;
	struct lio		*lio = if_getsoftc(ifp);
	struct octeon_device	*oct = lio->oct_dev;
	int	ret = 0;

	bzero(&nctrl, sizeof(struct lio_ctrl_pkt));

	nctrl.ncmd.cmd64 = 0;
	nctrl.ncmd.s.cmd = cmd;
	nctrl.ncmd.s.param1 = param1;
	nctrl.iq_no = lio->linfo.txpciq[0].s.q_no;
	nctrl.wait_time = 100;
	nctrl.lio = lio;
	nctrl.cb_fn = lio_ctrl_cmd_completion;

	ret = lio_send_ctrl_pkt(lio->oct_dev, &nctrl);
	if (ret < 0) {
		lio_dev_err(oct, "Feature change failed in core (ret: 0x%x)\n",
			    ret);
	}

	return (ret);
}

void
lio_ctrl_cmd_completion(void *nctrl_ptr)
{
	struct lio_ctrl_pkt	*nctrl = (struct lio_ctrl_pkt *)nctrl_ptr;
	struct lio		*lio;
	struct octeon_device	*oct;
	uint8_t	*mac;

	lio = nctrl->lio;

	if (lio->oct_dev == NULL)
		return;

	oct = lio->oct_dev;

	switch (nctrl->ncmd.s.cmd) {
	case LIO_CMD_CHANGE_DEVFLAGS:
	case LIO_CMD_SET_MULTI_LIST:
		break;

	case LIO_CMD_CHANGE_MACADDR:
		mac = ((uint8_t *)&nctrl->udd[0]) + 2;
		if (nctrl->ncmd.s.param1) {
			/* vfidx is 0 based, but vf_num (param1) is 1 based */
			int	vfidx = nctrl->ncmd.s.param1 - 1;
			bool	mac_is_admin_assigned = nctrl->ncmd.s.param2;

			if (mac_is_admin_assigned)
				lio_dev_info(oct, "MAC Address %pM is configured for VF %d\n",
					     mac, vfidx);
		} else {
			lio_dev_info(oct, "MAC Address changed to %02x:%02x:%02x:%02x:%02x:%02x\n",
				     mac[0], mac[1], mac[2], mac[3], mac[4],
				     mac[5]);
		}
		break;

	case LIO_CMD_GPIO_ACCESS:
		lio_dev_info(oct, "LED Flashing visual identification\n");
		break;

	case LIO_CMD_ID_ACTIVE:
		lio_dev_info(oct, "LED Flashing visual identification\n");
		break;

	case LIO_CMD_LRO_ENABLE:
		lio_dev_info(oct, "HW LRO Enabled\n");
		break;

	case LIO_CMD_LRO_DISABLE:
		lio_dev_info(oct, "HW LRO Disabled\n");
		break;

	case LIO_CMD_VERBOSE_ENABLE:
		lio_dev_info(oct, "Firmware debug enabled\n");
		break;

	case LIO_CMD_VERBOSE_DISABLE:
		lio_dev_info(oct, "Firmware debug disabled\n");
		break;

	case LIO_CMD_VLAN_FILTER_CTL:
		if (nctrl->ncmd.s.param1)
			lio_dev_info(oct, "VLAN filter enabled\n");
		else
			lio_dev_info(oct, "VLAN filter disabled\n");
		break;

	case LIO_CMD_ADD_VLAN_FILTER:
		lio_dev_info(oct, "VLAN filter %d added\n",
			     nctrl->ncmd.s.param1);
		break;

	case LIO_CMD_DEL_VLAN_FILTER:
		lio_dev_info(oct, "VLAN filter %d removed\n",
			     nctrl->ncmd.s.param1);
		break;

	case LIO_CMD_SET_SETTINGS:
		lio_dev_info(oct, "Settings changed\n");
		break;

		/*
		 * Case to handle "LIO_CMD_TNL_RX_CSUM_CTL"
		 * Command passed by NIC driver
		 */
	case LIO_CMD_TNL_RX_CSUM_CTL:
		if (nctrl->ncmd.s.param1 == LIO_CMD_RXCSUM_ENABLE) {
			lio_dev_info(oct, "RX Checksum Offload Enabled\n");
		} else if (nctrl->ncmd.s.param1 == LIO_CMD_RXCSUM_DISABLE) {
			lio_dev_info(oct, "RX Checksum Offload Disabled\n");
		}
		break;

		/*
		 * Case to handle "LIO_CMD_TNL_TX_CSUM_CTL"
		 * Command passed by NIC driver
		 */
	case LIO_CMD_TNL_TX_CSUM_CTL:
		if (nctrl->ncmd.s.param1 == LIO_CMD_TXCSUM_ENABLE) {
			lio_dev_info(oct, "TX Checksum Offload Enabled\n");
		} else if (nctrl->ncmd.s.param1 == LIO_CMD_TXCSUM_DISABLE) {
			lio_dev_info(oct, "TX Checksum Offload Disabled\n");
		}
		break;

		/*
		 * Case to handle "LIO_CMD_VXLAN_PORT_CONFIG"
		 * Command passed by NIC driver
		 */
	case LIO_CMD_VXLAN_PORT_CONFIG:
		if (nctrl->ncmd.s.more == LIO_CMD_VXLAN_PORT_ADD) {
			lio_dev_info(oct, "VxLAN Destination UDP PORT:%d ADDED\n",
				     nctrl->ncmd.s.param1);
		} else if (nctrl->ncmd.s.more == LIO_CMD_VXLAN_PORT_DEL) {
			lio_dev_info(oct, "VxLAN Destination UDP PORT:%d DELETED\n",
				     nctrl->ncmd.s.param1);
		}
		break;

	case LIO_CMD_SET_FLOW_CTL:
		lio_dev_info(oct, "Set RX/TX flow control parameters\n");
		break;

	case LIO_CMD_SET_FNV:
		if (nctrl->ncmd.s.param1 == LIO_CMD_FNV_ENABLE)
			lio_dev_info(oct, "FNV Enabled\n");
		else if (nctrl->ncmd.s.param1 == LIO_CMD_FNV_DISABLE)
			lio_dev_info(oct, "FNV Disabled\n");
		break;

	case LIO_CMD_PKT_STEERING_CTL:
		if (nctrl->ncmd.s.param1 == LIO_CMD_PKT_STEERING_ENABLE) {
			lio_dev_info(oct, "Packet Steering Enabled\n");
		} else if (nctrl->ncmd.s.param1 ==
			   LIO_CMD_PKT_STEERING_DISABLE) {
			lio_dev_info(oct, "Packet Steering Disabled\n");
		}

		break;

	case LIO_CMD_QUEUE_COUNT_CTL:
		lio_dev_info(oct, "Queue count updated to %d\n",
			     nctrl->ncmd.s.param1);
		break;

	default:
		lio_dev_err(oct, "%s Unknown cmd %d\n", __func__,
			    nctrl->ncmd.s.cmd);
	}
}


/*
 * \brief Setup output queue
 * @param oct octeon device
 * @param q_no which queue
 * @param num_descs how many descriptors
 * @param desc_size size of each descriptor
 * @param app_ctx application context
 */
static int
lio_setup_droq(struct octeon_device *oct, int q_no, int num_descs,
	       int desc_size, void *app_ctx)
{
	int	ret_val = 0;

	lio_dev_dbg(oct, "Creating Droq: %d\n", q_no);
	/* droq creation and local register settings. */
	ret_val = lio_create_droq(oct, q_no, num_descs, desc_size, app_ctx);
	if (ret_val < 0)
		return (ret_val);

	if (ret_val == 1) {
		lio_dev_dbg(oct, "Using default droq %d\n", q_no);
		return (0);
	}

	/*
	 * Send Credit for Octeon Output queues. Credits are always
         * sent after the output queue is enabled.
         */
	lio_write_csr32(oct, oct->droq[q_no]->pkts_credit_reg,
			oct->droq[q_no]->max_count);

	return (ret_val);
}

static void
lio_push_packet(void *m_buff, uint32_t len, union octeon_rh *rh, void *rxq,
		void *arg)
{
	struct mbuf	*mbuf = m_buff;
	struct ifnet	*ifp = arg;
	struct lio_droq	*droq = rxq;

	if (ifp != NULL) {
		struct lio	*lio = if_getsoftc(ifp);

		/* Do not proceed if the interface is not in RUNNING state. */
		if (!lio_ifstate_check(lio, LIO_IFSTATE_RUNNING)) {
			lio_recv_buffer_free(mbuf);
			droq->stats.rx_dropped++;
			return;
		}

		if (rh->r_dh.has_hash) {
			uint32_t	hashtype, hashval;

			if (rh->r_dh.has_hwtstamp) {
				hashval = htobe32(*(uint32_t *)
						  (((uint8_t *)mbuf->m_data) +
						   ((rh->r_dh.len - 2) *
						    BYTES_PER_DHLEN_UNIT)));
				hashtype =
				    htobe32(*(((uint32_t *)
					       (((uint8_t *)mbuf->m_data) +
						((rh->r_dh.len - 2) *
						 BYTES_PER_DHLEN_UNIT))) + 1));
			} else {
				hashval = htobe32(*(uint32_t *)
						  (((uint8_t *)mbuf->m_data) +
						   ((rh->r_dh.len - 1) *
						    BYTES_PER_DHLEN_UNIT)));
				hashtype =
				    htobe32(*(((uint32_t *)
					       (((uint8_t *)mbuf->m_data) +
						((rh->r_dh.len - 1) *
						 BYTES_PER_DHLEN_UNIT))) + 1));
			}

			mbuf->m_pkthdr.flowid = hashval;

			switch (hashtype) {
			case LIO_RSS_HASH_IPV4:
				M_HASHTYPE_SET(mbuf, M_HASHTYPE_RSS_IPV4);
				break;
			case LIO_RSS_HASH_TCP_IPV4:
				M_HASHTYPE_SET(mbuf, M_HASHTYPE_RSS_TCP_IPV4);
				break;
			case LIO_RSS_HASH_IPV6:
				M_HASHTYPE_SET(mbuf, M_HASHTYPE_RSS_IPV6);
				break;
			case LIO_RSS_HASH_TCP_IPV6:
				M_HASHTYPE_SET(mbuf, M_HASHTYPE_RSS_TCP_IPV6);
				break;
			case LIO_RSS_HASH_IPV6_EX:
				M_HASHTYPE_SET(mbuf, M_HASHTYPE_RSS_IPV6_EX);
				break;
			case LIO_RSS_HASH_TCP_IPV6_EX:
				M_HASHTYPE_SET(mbuf,
					       M_HASHTYPE_RSS_TCP_IPV6_EX);
				break;
			default:
				M_HASHTYPE_SET(mbuf, M_HASHTYPE_OPAQUE_HASH);
			}

		} else {
			/*
                         * This case won't hit as FW will always set has_hash
                         * in rh.
                         */
			M_HASHTYPE_SET(mbuf, M_HASHTYPE_OPAQUE);
			mbuf->m_pkthdr.flowid = droq->q_no;
		}

		m_adj(mbuf, rh->r_dh.len * 8);
		len -= rh->r_dh.len * 8;
		mbuf->m_flags |= M_PKTHDR;

		if ((if_getcapenable(ifp) & IFCAP_VLAN_HWTAGGING) &&
		    (rh->r_dh.priority || rh->r_dh.vlan)) {
			uint16_t	priority = rh->r_dh.priority;
			uint16_t	vid = rh->r_dh.vlan;
			uint16_t	vtag;

			vtag = priority << 13 | vid;
			mbuf->m_pkthdr.ether_vtag = vtag;
			mbuf->m_flags |= M_VLANTAG;
		}

		if (rh->r_dh.csum_verified & LIO_IPSUM_VERIFIED)
			mbuf->m_pkthdr.csum_flags |= (CSUM_L3_CALC |
						      CSUM_L3_VALID);

		if (rh->r_dh.csum_verified & LIO_L4SUM_VERIFIED) {
			mbuf->m_pkthdr.csum_flags |= (CSUM_L4_CALC |
						      CSUM_L4_VALID);
			mbuf->m_pkthdr.csum_flags |= (CSUM_DATA_VALID |
						      CSUM_PSEUDO_HDR);
			mbuf->m_pkthdr.csum_data = htons(0xffff);
		}

		mbuf->m_pkthdr.rcvif = ifp;
		mbuf->m_pkthdr.len = len;

		if ((lio_hwlro == 0) &&
		    (if_getcapenable(ifp) & IFCAP_LRO) &&
		    (mbuf->m_pkthdr.csum_flags &
		     (CSUM_L3_VALID | CSUM_L4_VALID | CSUM_DATA_VALID |
		      CSUM_PSEUDO_HDR)) == (CSUM_L3_VALID | CSUM_L4_VALID |
					    CSUM_DATA_VALID |
					    CSUM_PSEUDO_HDR)) {
			if (droq->lro.lro_cnt) {
				if (tcp_lro_rx(&droq->lro, mbuf, 0) == 0) {
					droq->stats.rx_bytes_received += len;
					droq->stats.rx_pkts_received++;
					return;
				}
			}
		}

		if_input(ifp, mbuf);

		droq->stats.rx_bytes_received += len;
		droq->stats.rx_pkts_received++;

	} else {
		lio_recv_buffer_free(mbuf);
		droq->stats.rx_dropped++;
	}
}

/*
 * \brief Setup input and output queues
 * @param octeon_dev octeon device
 * @param ifidx  Interface Index
 *
 * Note: Queues are with respect to the octeon device. Thus
 * an input queue is for egress packets, and output queues
 * are for ingress packets.
 */
int
lio_setup_io_queues(struct octeon_device *octeon_dev, int ifidx,
		    uint32_t num_iqs, uint32_t num_oqs)
{
	struct lio_droq_ops	droq_ops;
	struct ifnet		*ifp;
	struct lio_droq		*droq;
	struct lio		*lio;
	static int		cpu_id, cpu_id_modulus;
	int	num_tx_descs, q, q_no, retval = 0;

	ifp = octeon_dev->props.ifp;

	lio = if_getsoftc(ifp);

	bzero(&droq_ops, sizeof(struct lio_droq_ops));

	droq_ops.fptr = lio_push_packet;
	droq_ops.farg = (void *)ifp;

	cpu_id = 0;
	cpu_id_modulus = mp_ncpus;
	/* set up DROQs. */
	for (q = 0; q < num_oqs; q++) {
		q_no = lio->linfo.rxpciq[q].s.q_no;
		lio_dev_dbg(octeon_dev, "lio_setup_io_queues index:%d linfo.rxpciq.s.q_no:%d\n",
			    q, q_no);
		retval = lio_setup_droq(octeon_dev, q_no,
					LIO_GET_NUM_RX_DESCS_NIC_IF_CFG(
						     lio_get_conf(octeon_dev),
								  lio->ifidx),
					LIO_GET_NUM_RX_BUF_SIZE_NIC_IF_CFG(
						     lio_get_conf(octeon_dev),
							   lio->ifidx), NULL);
		if (retval) {
			lio_dev_err(octeon_dev, "%s : Runtime DROQ(RxQ) creation failed.\n",
				    __func__);
			return (1);
		}

		droq = octeon_dev->droq[q_no];

		/* designate a CPU for this droq */
		droq->cpu_id = cpu_id;
		cpu_id++;
		if (cpu_id >= cpu_id_modulus)
			cpu_id = 0;

		lio_register_droq_ops(octeon_dev, q_no, &droq_ops);
	}

	/* set up IQs. */
	for (q = 0; q < num_iqs; q++) {
		num_tx_descs = LIO_GET_NUM_TX_DESCS_NIC_IF_CFG(
						     lio_get_conf(octeon_dev),
							       lio->ifidx);
		retval = lio_setup_iq(octeon_dev, ifidx, q,
				      lio->linfo.txpciq[q], num_tx_descs);
		if (retval) {
			lio_dev_err(octeon_dev, " %s : Runtime IQ(TxQ) creation failed.\n",
				    __func__);
			return (1);
		}
	}

	return (0);
}

/*
 * \brief Droq packet processor sceduler
 * @param oct octeon device
 */
static void
lio_schedule_droq_pkt_handlers(struct octeon_device *oct)
{
	struct lio_droq	*droq;
	uint64_t	oq_no;

	if (oct->int_status & LIO_DEV_INTR_PKT_DATA) {
		for (oq_no = 0; oq_no < LIO_MAX_OUTPUT_QUEUES(oct); oq_no++) {
			if (!(oct->io_qmask.oq & BIT_ULL(oq_no)))
				continue;

			droq = oct->droq[oq_no];

			taskqueue_enqueue(droq->droq_taskqueue,
					  &droq->droq_task);
		}
	}
}

static void
lio_msix_intr_handler(void *vector)
{
	struct lio_ioq_vector	*ioq_vector = (struct lio_ioq_vector *)vector;
	struct octeon_device	*oct = ioq_vector->oct_dev;
	struct lio_droq		*droq = oct->droq[ioq_vector->droq_index];
	uint64_t		ret;

	ret = oct->fn_list.msix_interrupt_handler(ioq_vector);

	if ((ret & LIO_MSIX_PO_INT) || (ret & LIO_MSIX_PI_INT)) {
		struct lio_instr_queue *iq = oct->instr_queue[droq->q_no];
		int	reschedule, tx_done = 1;

		reschedule = lio_droq_process_packets(oct, droq, oct->rx_budget);

		if (atomic_load_acq_int(&iq->instr_pending))
			tx_done = lio_flush_iq(oct, iq, oct->tx_budget);

		if ((oct->props.ifp != NULL) && (iq->br != NULL)) {
			if (mtx_trylock(&iq->enq_lock)) {
				if (!drbr_empty(oct->props.ifp, iq->br))
					lio_mq_start_locked(oct->props.ifp,
							    iq);
				mtx_unlock(&iq->enq_lock);
			}
		}

		if (reschedule || !tx_done)
			taskqueue_enqueue(droq->droq_taskqueue, &droq->droq_task);
		else
			lio_enable_irq(droq, iq);
	}
}

static void
lio_intr_handler(void *dev)
{
	struct octeon_device	*oct = (struct octeon_device *)dev;

	/* Disable our interrupts for the duration of ISR */
	oct->fn_list.disable_interrupt(oct, OCTEON_ALL_INTR);

	oct->fn_list.process_interrupt_regs(oct);

	lio_schedule_droq_pkt_handlers(oct);

	/* Re-enable our interrupts  */
	if (!(atomic_load_acq_int(&oct->status) == LIO_DEV_IN_RESET))
		oct->fn_list.enable_interrupt(oct, OCTEON_ALL_INTR);
}

int
lio_setup_interrupt(struct octeon_device *oct, uint32_t num_ioqs)
{
	device_t		device;
	struct lio_ioq_vector	*ioq_vector;
	int	cpu_id, err, i;
	int	num_alloc_ioq_vectors;
	int	num_ioq_vectors;
	int	res_id;

	if (!oct->msix_on)
		return (1);

	ioq_vector = oct->ioq_vector;

#ifdef RSS
	if (oct->sriov_info.num_pf_rings != rss_getnumbuckets()) {
		lio_dev_info(oct, "IOQ vectors (%d) are not equal number of RSS buckets (%d)\n",
			     oct->sriov_info.num_pf_rings, rss_getnumbuckets());
	}
#endif

	device = oct->device;

	oct->num_msix_irqs = num_ioqs;
	/* one non ioq interrupt for handling sli_mac_pf_int_sum */
	oct->num_msix_irqs += 1;
	num_alloc_ioq_vectors = oct->num_msix_irqs;

	if (pci_alloc_msix(device, &num_alloc_ioq_vectors) ||
	    (num_alloc_ioq_vectors != oct->num_msix_irqs))
		goto err;

	num_ioq_vectors = oct->num_msix_irqs;

	/* For PF, there is one non-ioq interrupt handler */
	for (i = 0; i < num_ioq_vectors - 1; i++, ioq_vector++) {
		res_id = i + 1;

		ioq_vector->msix_res =
		    bus_alloc_resource_any(device, SYS_RES_IRQ, &res_id,
					   RF_SHAREABLE | RF_ACTIVE);
		if (ioq_vector->msix_res == NULL) {
			lio_dev_err(oct,
				    "Unable to allocate bus res msix[%d]\n", i);
			goto err_1;
		}

		err = bus_setup_intr(device, ioq_vector->msix_res,
				     INTR_TYPE_NET | INTR_MPSAFE, NULL,
				     lio_msix_intr_handler, ioq_vector,
				     &ioq_vector->tag);
		if (err) {
			bus_release_resource(device, SYS_RES_IRQ, res_id,
					     ioq_vector->msix_res);
			ioq_vector->msix_res = NULL;
			lio_dev_err(oct, "Failed to register intr handler");
			goto err_1;
		}

		bus_describe_intr(device, ioq_vector->msix_res, ioq_vector->tag,
				  "rxtx%u", i);
		ioq_vector->vector = res_id;

#ifdef RSS
		cpu_id = rss_getcpu(i % rss_getnumbuckets());
#else
		cpu_id = i % mp_ncpus;
#endif
		CPU_SETOF(cpu_id, &ioq_vector->affinity_mask);

		/* Setting the IRQ affinity. */
		err = bus_bind_intr(device, ioq_vector->msix_res, cpu_id);
		if (err)
			lio_dev_err(oct, "bus bind interrupt fail");
#ifdef RSS
		lio_dev_dbg(oct, "Bound RSS bucket %d to CPU %d\n", i, cpu_id);
#else
		lio_dev_dbg(oct, "Bound Queue %d to CPU %d\n", i, cpu_id);
#endif
	}

	lio_dev_dbg(oct, "MSI-X enabled\n");

	res_id = num_ioq_vectors;
	oct->msix_res = bus_alloc_resource_any(device, SYS_RES_IRQ, &res_id,
					       RF_SHAREABLE | RF_ACTIVE);
	if (oct->msix_res == NULL) {
		lio_dev_err(oct, "Unable to allocate bus res msix for non-ioq interrupt\n");
		goto err_1;
	}

	err = bus_setup_intr(device, oct->msix_res, INTR_TYPE_NET | INTR_MPSAFE,
			     NULL, lio_intr_handler, oct, &oct->tag);
	if (err) {
		bus_release_resource(device, SYS_RES_IRQ, res_id,
				     oct->msix_res);
		oct->msix_res = NULL;
		lio_dev_err(oct, "Failed to register intr handler");
		goto err_1;
	}

	bus_describe_intr(device, oct->msix_res, oct->tag, "aux");
	oct->aux_vector = res_id;

	return (0);
err_1:
	if (oct->tag != NULL) {
		bus_teardown_intr(device, oct->msix_res, oct->tag);
		oct->tag = NULL;
	}

	while (i) {
		i--;
		ioq_vector--;

		if (ioq_vector->tag != NULL) {
			bus_teardown_intr(device, ioq_vector->msix_res,
					  ioq_vector->tag);
			ioq_vector->tag = NULL;
		}

		if (ioq_vector->msix_res != NULL) {
			bus_release_resource(device, SYS_RES_IRQ,
					     ioq_vector->vector,
					     ioq_vector->msix_res);
			ioq_vector->msix_res = NULL;
		}
	}

	if (oct->msix_res != NULL) {
		bus_release_resource(device, SYS_RES_IRQ, oct->aux_vector,
				     oct->msix_res);
		oct->msix_res = NULL;
	}
err:
	pci_release_msi(device);
	lio_dev_err(oct, "MSI-X disabled\n");
	return (1);
}
