/******************************************************************************

  Copyright (c) 2013-2018, Intel Corporation
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

#include "ixl_pf_iov.h"

/* Private functions */
static void	ixl_vf_map_vsi_queue(struct i40e_hw *hw, struct ixl_vf *vf, int qnum, uint32_t val);
static void	ixl_vf_disable_queue_intr(struct i40e_hw *hw, uint32_t vfint_reg);
static void	ixl_vf_unregister_intr(struct i40e_hw *hw, uint32_t vpint_reg);

static bool	ixl_zero_mac(const uint8_t *addr);
static bool	ixl_bcast_mac(const uint8_t *addr);

static int	ixl_vc_opcode_level(uint16_t opcode);

static int	ixl_vf_mac_valid(struct ixl_vf *vf, const uint8_t *addr);

static int	ixl_vf_alloc_vsi(struct ixl_pf *pf, struct ixl_vf *vf);
static int	ixl_vf_setup_vsi(struct ixl_pf *pf, struct ixl_vf *vf);
static void	ixl_vf_map_queues(struct ixl_pf *pf, struct ixl_vf *vf);
static void	ixl_vf_vsi_release(struct ixl_pf *pf, struct ixl_vsi *vsi);
static void	ixl_vf_release_resources(struct ixl_pf *pf, struct ixl_vf *vf);
static int	ixl_flush_pcie(struct ixl_pf *pf, struct ixl_vf *vf);
static void	ixl_reset_vf(struct ixl_pf *pf, struct ixl_vf *vf);
static void	ixl_reinit_vf(struct ixl_pf *pf, struct ixl_vf *vf);
static void	ixl_send_vf_msg(struct ixl_pf *pf, struct ixl_vf *vf, uint16_t op, enum i40e_status_code status, void *msg, uint16_t len);
static void	ixl_send_vf_ack(struct ixl_pf *pf, struct ixl_vf *vf, uint16_t op);
static void	ixl_send_vf_nack_msg(struct ixl_pf *pf, struct ixl_vf *vf, uint16_t op, enum i40e_status_code status, const char *file, int line);
static void	ixl_vf_version_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_reset_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_get_resources_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static int	ixl_vf_config_tx_queue(struct ixl_pf *pf, struct ixl_vf *vf, struct virtchnl_txq_info *info);
static int	ixl_vf_config_rx_queue(struct ixl_pf *pf, struct ixl_vf *vf, struct virtchnl_rxq_info *info);
static void	ixl_vf_config_vsi_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_set_qctl(struct ixl_pf *pf, const struct virtchnl_vector_map *vector, enum i40e_queue_type cur_type, uint16_t cur_queue,
    enum i40e_queue_type *last_type, uint16_t *last_queue);
static void	ixl_vf_config_vector(struct ixl_pf *pf, struct ixl_vf *vf, const struct virtchnl_vector_map *vector);
static void	ixl_vf_config_irq_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_enable_queues_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_disable_queues_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_add_mac_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_del_mac_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static enum i40e_status_code	ixl_vf_enable_vlan_strip(struct ixl_pf *pf, struct ixl_vf *vf);
static void	ixl_vf_add_vlan_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_del_vlan_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_config_promisc_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static void	ixl_vf_get_stats_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg, uint16_t msg_size);
static int	ixl_vf_reserve_queues(struct ixl_pf *pf, struct ixl_vf *vf, int num_queues);
static int	ixl_config_pf_vsi_loopback(struct ixl_pf *pf, bool enable);

static int	ixl_adminq_err_to_errno(enum i40e_admin_queue_err err);

/*
 * TODO: Move pieces of this into iflib and call the rest in a handler?
 *
 * e.g. ixl_if_iov_set_schema
 *
 * It's odd to do pci_iov_detach() there while doing pci_iov_attach()
 * in the driver.
 */
void
ixl_initialize_sriov(struct ixl_pf *pf)
{
	device_t dev = pf->dev;
	struct i40e_hw *hw = &pf->hw;
	nvlist_t	*pf_schema, *vf_schema;
	int		iov_error;

	pf_schema = pci_iov_schema_alloc_node();
	vf_schema = pci_iov_schema_alloc_node();
	pci_iov_schema_add_unicast_mac(vf_schema, "mac-addr", 0, NULL);
	pci_iov_schema_add_bool(vf_schema, "mac-anti-spoof",
	    IOV_SCHEMA_HASDEFAULT, TRUE);
	pci_iov_schema_add_bool(vf_schema, "allow-set-mac",
	    IOV_SCHEMA_HASDEFAULT, FALSE);
	pci_iov_schema_add_bool(vf_schema, "allow-promisc",
	    IOV_SCHEMA_HASDEFAULT, FALSE);
	pci_iov_schema_add_uint16(vf_schema, "num-queues",
	    IOV_SCHEMA_HASDEFAULT,
	    max(1, min(hw->func_caps.num_msix_vectors_vf - 1, IAVF_MAX_QUEUES)));

	iov_error = pci_iov_attach(dev, pf_schema, vf_schema);
	if (iov_error != 0) {
		device_printf(dev,
		    "Failed to initialize SR-IOV (error=%d)\n",
		    iov_error);
	} else
		device_printf(dev, "SR-IOV ready\n");
}


/*
 * Allocate the VSI for a VF.
 */
static int
ixl_vf_alloc_vsi(struct ixl_pf *pf, struct ixl_vf *vf)
{
	device_t dev;
	struct i40e_hw *hw;
	struct ixl_vsi *vsi;
	struct i40e_vsi_context vsi_ctx;
	int i;
	enum i40e_status_code code;

	hw = &pf->hw;
	vsi = &pf->vsi;
	dev = pf->dev;

	vsi_ctx.pf_num = hw->pf_id;
	vsi_ctx.uplink_seid = pf->veb_seid;
	vsi_ctx.connection_type = IXL_VSI_DATA_PORT;
	vsi_ctx.vf_num = hw->func_caps.vf_base_id + vf->vf_num;
	vsi_ctx.flags = I40E_AQ_VSI_TYPE_VF;

	bzero(&vsi_ctx.info, sizeof(vsi_ctx.info));

	vsi_ctx.info.valid_sections = htole16(I40E_AQ_VSI_PROP_SWITCH_VALID);
	if (pf->enable_vf_loopback)
		vsi_ctx.info.switch_id =
		   htole16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);

	vsi_ctx.info.valid_sections |= htole16(I40E_AQ_VSI_PROP_SECURITY_VALID);
	vsi_ctx.info.sec_flags = 0;
	if (vf->vf_flags & VF_FLAG_MAC_ANTI_SPOOF)
		vsi_ctx.info.sec_flags |= I40E_AQ_VSI_SEC_FLAG_ENABLE_MAC_CHK;

	vsi_ctx.info.valid_sections |= htole16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi_ctx.info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL |
	    I40E_AQ_VSI_PVLAN_EMOD_NOTHING;

	vsi_ctx.info.valid_sections |=
	    htole16(I40E_AQ_VSI_PROP_QUEUE_MAP_VALID);
	vsi_ctx.info.mapping_flags = htole16(I40E_AQ_VSI_QUE_MAP_NONCONTIG);

	/* XXX: Only scattered allocation is supported for VFs right now */
	for (i = 0; i < vf->qtag.num_active; i++)
		vsi_ctx.info.queue_mapping[i] = vf->qtag.qidx[i];
	for (; i < nitems(vsi_ctx.info.queue_mapping); i++)
		vsi_ctx.info.queue_mapping[i] = htole16(I40E_AQ_VSI_QUEUE_MASK);

	vsi_ctx.info.tc_mapping[0] = htole16(
	    (0 << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT) |
	    ((fls(vf->qtag.num_allocated) - 1) << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT));

	code = i40e_aq_add_vsi(hw, &vsi_ctx, NULL);
	if (code != I40E_SUCCESS)
		return (ixl_adminq_err_to_errno(hw->aq.asq_last_status));
	vf->vsi.seid = vsi_ctx.seid;
	vf->vsi.vsi_num = vsi_ctx.vsi_number;
	vf->vsi.num_rx_queues = vf->qtag.num_active;
	vf->vsi.num_tx_queues = vf->qtag.num_active;

	code = i40e_aq_get_vsi_params(hw, &vsi_ctx, NULL);
	if (code != I40E_SUCCESS)
		return (ixl_adminq_err_to_errno(hw->aq.asq_last_status));

	code = i40e_aq_config_vsi_bw_limit(hw, vf->vsi.seid, 0, 0, NULL);
	if (code != I40E_SUCCESS) {
		device_printf(dev, "Failed to disable BW limit: %d\n",
		    ixl_adminq_err_to_errno(hw->aq.asq_last_status));
		return (ixl_adminq_err_to_errno(hw->aq.asq_last_status));
	}

	memcpy(&vf->vsi.info, &vsi_ctx.info, sizeof(vf->vsi.info));
	return (0);
}

static int
ixl_vf_setup_vsi(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_hw *hw;
	int error;

	hw = &pf->hw;

	error = ixl_vf_alloc_vsi(pf, vf);
	if (error != 0)
		return (error);

	/* Let VF receive broadcast Ethernet frames */
	error = i40e_aq_set_vsi_broadcast(hw, vf->vsi.seid, TRUE, NULL);
	if (error)
		device_printf(pf->dev, "Error configuring VF VSI for broadcast promiscuous\n");
	/* Re-add VF's MAC/VLAN filters to its VSI */
	ixl_reconfigure_filters(&vf->vsi);
	/* Reset stats? */
	vf->vsi.hw_filters_add = 0;
	vf->vsi.hw_filters_del = 0;

	return (0);
}

static void
ixl_vf_map_vsi_queue(struct i40e_hw *hw, struct ixl_vf *vf, int qnum,
    uint32_t val)
{
	uint32_t qtable;
	int index, shift;

	/*
	 * Two queues are mapped in a single register, so we have to do some
	 * gymnastics to convert the queue number into a register index and
	 * shift.
	 */
	index = qnum / 2;
	shift = (qnum % 2) * I40E_VSILAN_QTABLE_QINDEX_1_SHIFT;

	qtable = i40e_read_rx_ctl(hw, I40E_VSILAN_QTABLE(index, vf->vsi.vsi_num));
	qtable &= ~(I40E_VSILAN_QTABLE_QINDEX_0_MASK << shift);
	qtable |= val << shift;
	i40e_write_rx_ctl(hw, I40E_VSILAN_QTABLE(index, vf->vsi.vsi_num), qtable);
}

static void
ixl_vf_map_queues(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_hw *hw;
	uint32_t qtable;
	int i;

	hw = &pf->hw;

	/*
	 * Contiguous mappings aren't actually supported by the hardware,
	 * so we have to use non-contiguous mappings.
	 */
	i40e_write_rx_ctl(hw, I40E_VSILAN_QBASE(vf->vsi.vsi_num),
	     I40E_VSILAN_QBASE_VSIQTABLE_ENA_MASK);

	/* Enable LAN traffic on this VF */
	wr32(hw, I40E_VPLAN_MAPENA(vf->vf_num),
	    I40E_VPLAN_MAPENA_TXRX_ENA_MASK);

	/* Program index of each VF queue into PF queue space
	 * (This is only needed if QTABLE is enabled) */
	for (i = 0; i < vf->vsi.num_tx_queues; i++) {
		qtable = ixl_pf_qidx_from_vsi_qidx(&vf->qtag, i) <<
		    I40E_VPLAN_QTABLE_QINDEX_SHIFT;

		wr32(hw, I40E_VPLAN_QTABLE(i, vf->vf_num), qtable);
	}
	for (; i < IXL_MAX_VSI_QUEUES; i++)
		wr32(hw, I40E_VPLAN_QTABLE(i, vf->vf_num),
		    I40E_VPLAN_QTABLE_QINDEX_MASK);

	/* Map queues allocated to VF to its VSI;
	 * This mapping matches the VF-wide mapping since the VF
	 * is only given a single VSI */
	for (i = 0; i < vf->vsi.num_tx_queues; i++)
		ixl_vf_map_vsi_queue(hw, vf, i,
		    ixl_pf_qidx_from_vsi_qidx(&vf->qtag, i));

	/* Set rest of VSI queues as unused. */
	for (; i < IXL_MAX_VSI_QUEUES; i++)
		ixl_vf_map_vsi_queue(hw, vf, i,
		    I40E_VSILAN_QTABLE_QINDEX_0_MASK);

	ixl_flush(hw);
}

static void
ixl_vf_vsi_release(struct ixl_pf *pf, struct ixl_vsi *vsi)
{
	struct i40e_hw *hw;

	hw = &pf->hw;

	if (vsi->seid == 0)
		return;

	i40e_aq_delete_element(hw, vsi->seid, NULL);
}

static void
ixl_vf_disable_queue_intr(struct i40e_hw *hw, uint32_t vfint_reg)
{

	wr32(hw, vfint_reg, I40E_VFINT_DYN_CTLN_CLEARPBA_MASK);
	ixl_flush(hw);
}

static void
ixl_vf_unregister_intr(struct i40e_hw *hw, uint32_t vpint_reg)
{

	wr32(hw, vpint_reg, I40E_VPINT_LNKLSTN_FIRSTQ_TYPE_MASK |
	    I40E_VPINT_LNKLSTN_FIRSTQ_INDX_MASK);
	ixl_flush(hw);
}

static void
ixl_vf_release_resources(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_hw *hw;
	uint32_t vfint_reg, vpint_reg;
	int i;

	hw = &pf->hw;

	ixl_vf_vsi_release(pf, &vf->vsi);

	/* Index 0 has a special register. */
	ixl_vf_disable_queue_intr(hw, I40E_VFINT_DYN_CTL0(vf->vf_num));

	for (i = 1; i < hw->func_caps.num_msix_vectors_vf; i++) {
		vfint_reg = IXL_VFINT_DYN_CTLN_REG(hw, i , vf->vf_num);
		ixl_vf_disable_queue_intr(hw, vfint_reg);
	}

	/* Index 0 has a special register. */
	ixl_vf_unregister_intr(hw, I40E_VPINT_LNKLST0(vf->vf_num));

	for (i = 1; i < hw->func_caps.num_msix_vectors_vf; i++) {
		vpint_reg = IXL_VPINT_LNKLSTN_REG(hw, i, vf->vf_num);
		ixl_vf_unregister_intr(hw, vpint_reg);
	}

	vf->vsi.num_tx_queues = 0;
	vf->vsi.num_rx_queues = 0;
}

static int
ixl_flush_pcie(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_hw *hw;
	int i;
	uint16_t global_vf_num;
	uint32_t ciad;

	hw = &pf->hw;
	global_vf_num = hw->func_caps.vf_base_id + vf->vf_num;

	wr32(hw, I40E_PF_PCI_CIAA, IXL_PF_PCI_CIAA_VF_DEVICE_STATUS |
	     (global_vf_num << I40E_PF_PCI_CIAA_VF_NUM_SHIFT));
	for (i = 0; i < IXL_VF_RESET_TIMEOUT; i++) {
		ciad = rd32(hw, I40E_PF_PCI_CIAD);
		if ((ciad & IXL_PF_PCI_CIAD_VF_TRANS_PENDING_MASK) == 0)
			return (0);
		DELAY(1);
	}

	return (ETIMEDOUT);
}

static void
ixl_reset_vf(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_hw *hw;
	uint32_t vfrtrig;

	hw = &pf->hw;

	ixl_dbg_iov(pf, "Resetting VF-%d\n", vf->vf_num);

	vfrtrig = rd32(hw, I40E_VPGEN_VFRTRIG(vf->vf_num));
	vfrtrig |= I40E_VPGEN_VFRTRIG_VFSWR_MASK;
	wr32(hw, I40E_VPGEN_VFRTRIG(vf->vf_num), vfrtrig);
	ixl_flush(hw);

	ixl_reinit_vf(pf, vf);

	ixl_dbg_iov(pf, "Resetting VF-%d done.\n", vf->vf_num);
}

static void
ixl_reinit_vf(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_hw *hw;
	uint32_t vfrstat, vfrtrig;
	int i, error;

	hw = &pf->hw;

	error = ixl_flush_pcie(pf, vf);
	if (error != 0)
		device_printf(pf->dev,
		    "Timed out waiting for PCIe activity to stop on VF-%d\n",
		    vf->vf_num);

	for (i = 0; i < IXL_VF_RESET_TIMEOUT; i++) {
		DELAY(10);

		vfrstat = rd32(hw, I40E_VPGEN_VFRSTAT(vf->vf_num));
		if (vfrstat & I40E_VPGEN_VFRSTAT_VFRD_MASK)
			break;
	}

	if (i == IXL_VF_RESET_TIMEOUT)
		device_printf(pf->dev, "VF %d failed to reset\n", vf->vf_num);

	wr32(hw, I40E_VFGEN_RSTAT1(vf->vf_num), VIRTCHNL_VFR_COMPLETED);

	vfrtrig = rd32(hw, I40E_VPGEN_VFRTRIG(vf->vf_num));
	vfrtrig &= ~I40E_VPGEN_VFRTRIG_VFSWR_MASK;
	wr32(hw, I40E_VPGEN_VFRTRIG(vf->vf_num), vfrtrig);

	if (vf->vsi.seid != 0)
		ixl_disable_rings(pf, &vf->vsi, &vf->qtag);
	ixl_pf_qmgr_clear_queue_flags(&vf->qtag);

	ixl_vf_release_resources(pf, vf);
	ixl_vf_setup_vsi(pf, vf);
	ixl_vf_map_queues(pf, vf);

	wr32(hw, I40E_VFGEN_RSTAT1(vf->vf_num), VIRTCHNL_VFR_VFACTIVE);
	ixl_flush(hw);
}

static int
ixl_vc_opcode_level(uint16_t opcode)
{
	switch (opcode) {
	case VIRTCHNL_OP_GET_STATS:
		return (10);
	default:
		return (5);
	}
}

static void
ixl_send_vf_msg(struct ixl_pf *pf, struct ixl_vf *vf, uint16_t op,
    enum i40e_status_code status, void *msg, uint16_t len)
{
	struct i40e_hw *hw;
	int global_vf_id;

	hw = &pf->hw;
	global_vf_id = hw->func_caps.vf_base_id + vf->vf_num;

	I40E_VC_DEBUG(pf, ixl_vc_opcode_level(op),
	    "Sending msg (op=%s[%d], status=%d) to VF-%d\n",
	    ixl_vc_opcode_str(op), op, status, vf->vf_num);

	i40e_aq_send_msg_to_vf(hw, global_vf_id, op, status, msg, len, NULL);
}

static void
ixl_send_vf_ack(struct ixl_pf *pf, struct ixl_vf *vf, uint16_t op)
{

	ixl_send_vf_msg(pf, vf, op, I40E_SUCCESS, NULL, 0);
}

static void
ixl_send_vf_nack_msg(struct ixl_pf *pf, struct ixl_vf *vf, uint16_t op,
    enum i40e_status_code status, const char *file, int line)
{

	I40E_VC_DEBUG(pf, 1,
	    "Sending NACK (op=%s[%d], err=%s[%d]) to VF-%d from %s:%d\n",
	    ixl_vc_opcode_str(op), op, i40e_stat_str(&pf->hw, status),
	    status, vf->vf_num, file, line);
	ixl_send_vf_msg(pf, vf, op, status, NULL, 0);
}

static void
ixl_vf_version_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_version_info reply;

	if (msg_size != sizeof(struct virtchnl_version_info)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_VERSION,
		    I40E_ERR_PARAM);
		return;
	}

	vf->version = ((struct virtchnl_version_info *)msg)->minor;

	reply.major = VIRTCHNL_VERSION_MAJOR;
	reply.minor = VIRTCHNL_VERSION_MINOR;
	ixl_send_vf_msg(pf, vf, VIRTCHNL_OP_VERSION, I40E_SUCCESS, &reply,
	    sizeof(reply));
}

static void
ixl_vf_reset_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{

	if (msg_size != 0) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_RESET_VF,
		    I40E_ERR_PARAM);
		return;
	}

	ixl_reset_vf(pf, vf);

	/* No response to a reset message. */
}

static void
ixl_vf_get_resources_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_vf_resource reply;

	if ((vf->version == 0 && msg_size != 0) ||
	    (vf->version == 1 && msg_size != 4)) {
		device_printf(pf->dev, "Invalid GET_VF_RESOURCES message size,"
		    " for VF version %d.%d\n", VIRTCHNL_VERSION_MAJOR,
		    vf->version);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_GET_VF_RESOURCES,
		    I40E_ERR_PARAM);
		return;
	}

	bzero(&reply, sizeof(reply));

	if (vf->version == VIRTCHNL_VERSION_MINOR_NO_VF_CAPS)
		reply.vf_cap_flags = VIRTCHNL_VF_OFFLOAD_L2 |
					 VIRTCHNL_VF_OFFLOAD_RSS_REG |
					 VIRTCHNL_VF_OFFLOAD_VLAN;
	else
		/* Force VF RSS setup by PF in 1.1+ VFs */
		reply.vf_cap_flags = *(u32 *)msg & (
					 VIRTCHNL_VF_OFFLOAD_L2 |
					 VIRTCHNL_VF_OFFLOAD_RSS_PF |
					 VIRTCHNL_VF_OFFLOAD_VLAN);

	reply.num_vsis = 1;
	reply.num_queue_pairs = vf->vsi.num_tx_queues;
	reply.max_vectors = pf->hw.func_caps.num_msix_vectors_vf;
	reply.rss_key_size = 52;
	reply.rss_lut_size = 64;
	reply.vsi_res[0].vsi_id = vf->vsi.vsi_num;
	reply.vsi_res[0].vsi_type = VIRTCHNL_VSI_SRIOV;
	reply.vsi_res[0].num_queue_pairs = vf->vsi.num_tx_queues;
	memcpy(reply.vsi_res[0].default_mac_addr, vf->mac, ETHER_ADDR_LEN);

	ixl_send_vf_msg(pf, vf, VIRTCHNL_OP_GET_VF_RESOURCES,
	    I40E_SUCCESS, &reply, sizeof(reply));
}

static int
ixl_vf_config_tx_queue(struct ixl_pf *pf, struct ixl_vf *vf,
    struct virtchnl_txq_info *info)
{
	struct i40e_hw *hw;
	struct i40e_hmc_obj_txq txq;
	uint16_t global_queue_num, global_vf_num;
	enum i40e_status_code status;
	uint32_t qtx_ctl;

	hw = &pf->hw;
	global_queue_num = ixl_pf_qidx_from_vsi_qidx(&vf->qtag, info->queue_id);
	global_vf_num = hw->func_caps.vf_base_id + vf->vf_num;
	bzero(&txq, sizeof(txq));

	DDPRINTF(pf->dev, "VF %d: PF TX queue %d / VF TX queue %d (Global VF %d)\n",
	    vf->vf_num, global_queue_num, info->queue_id, global_vf_num);

	status = i40e_clear_lan_tx_queue_context(hw, global_queue_num);
	if (status != I40E_SUCCESS)
		return (EINVAL);

	txq.base = info->dma_ring_addr / IXL_TX_CTX_BASE_UNITS;

	txq.head_wb_ena = info->headwb_enabled;
	txq.head_wb_addr = info->dma_headwb_addr;
	txq.qlen = info->ring_len;
	txq.rdylist = le16_to_cpu(vf->vsi.info.qs_handle[0]);
	txq.rdylist_act = 0;

	status = i40e_set_lan_tx_queue_context(hw, global_queue_num, &txq);
	if (status != I40E_SUCCESS)
		return (EINVAL);

	qtx_ctl = I40E_QTX_CTL_VF_QUEUE |
	    (hw->pf_id << I40E_QTX_CTL_PF_INDX_SHIFT) |
	    (global_vf_num << I40E_QTX_CTL_VFVM_INDX_SHIFT);
	wr32(hw, I40E_QTX_CTL(global_queue_num), qtx_ctl);
	ixl_flush(hw);

	ixl_pf_qmgr_mark_queue_configured(&vf->qtag, info->queue_id, true);

	return (0);
}

static int
ixl_vf_config_rx_queue(struct ixl_pf *pf, struct ixl_vf *vf,
    struct virtchnl_rxq_info *info)
{
	struct i40e_hw *hw;
	struct i40e_hmc_obj_rxq rxq;
	uint16_t global_queue_num;
	enum i40e_status_code status;

	hw = &pf->hw;
	global_queue_num = ixl_pf_qidx_from_vsi_qidx(&vf->qtag, info->queue_id);
	bzero(&rxq, sizeof(rxq));

	DDPRINTF(pf->dev, "VF %d: PF RX queue %d / VF RX queue %d\n",
	    vf->vf_num, global_queue_num, info->queue_id);

	if (info->databuffer_size > IXL_VF_MAX_BUFFER)
		return (EINVAL);

	if (info->max_pkt_size > IXL_VF_MAX_FRAME ||
	    info->max_pkt_size < ETHER_MIN_LEN)
		return (EINVAL);

	if (info->splithdr_enabled) {
		if (info->hdr_size > IXL_VF_MAX_HDR_BUFFER)
			return (EINVAL);

		rxq.hsplit_0 = info->rx_split_pos &
		    (I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_L2 |
		     I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_IP |
		     I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_TCP_UDP |
		     I40E_HMC_OBJ_RX_HSPLIT_0_SPLIT_SCTP);
		rxq.hbuff = info->hdr_size >> I40E_RXQ_CTX_HBUFF_SHIFT;

		rxq.dtype = 2;
	}

	status = i40e_clear_lan_rx_queue_context(hw, global_queue_num);
	if (status != I40E_SUCCESS)
		return (EINVAL);

	rxq.base = info->dma_ring_addr / IXL_RX_CTX_BASE_UNITS;
	rxq.qlen = info->ring_len;

	rxq.dbuff = info->databuffer_size >> I40E_RXQ_CTX_DBUFF_SHIFT;

	rxq.dsize = 1;
	rxq.crcstrip = 1;
	rxq.l2tsel = 1;

	rxq.rxmax = info->max_pkt_size;
	rxq.tphrdesc_ena = 1;
	rxq.tphwdesc_ena = 1;
	rxq.tphdata_ena = 1;
	rxq.tphhead_ena = 1;
	rxq.lrxqthresh = 1;
	rxq.prefena = 1;

	status = i40e_set_lan_rx_queue_context(hw, global_queue_num, &rxq);
	if (status != I40E_SUCCESS)
		return (EINVAL);

	ixl_pf_qmgr_mark_queue_configured(&vf->qtag, info->queue_id, false);

	return (0);
}

static void
ixl_vf_config_vsi_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_vsi_queue_config_info *info;
	struct virtchnl_queue_pair_info *pair;
	uint16_t expected_msg_size;
	int i;

	if (msg_size < sizeof(*info)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	info = msg;
	if (info->num_queue_pairs == 0 || info->num_queue_pairs > vf->vsi.num_tx_queues) {
		device_printf(pf->dev, "VF %d: invalid # of qpairs (msg has %d, VSI has %d)\n",
		    vf->vf_num, info->num_queue_pairs, vf->vsi.num_tx_queues);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	expected_msg_size = sizeof(*info) + info->num_queue_pairs * sizeof(*pair);
	if (msg_size != expected_msg_size) {
		device_printf(pf->dev, "VF %d: size of recvd message (%d) does not match expected size (%d)\n",
		    vf->vf_num, msg_size, expected_msg_size);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	if (info->vsi_id != vf->vsi.vsi_num) {
		device_printf(pf->dev, "VF %d: VSI id in recvd message (%d) does not match expected id (%d)\n",
		    vf->vf_num, info->vsi_id, vf->vsi.vsi_num);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < info->num_queue_pairs; i++) {
		pair = &info->qpair[i];

		if (pair->txq.vsi_id != vf->vsi.vsi_num ||
		    pair->rxq.vsi_id != vf->vsi.vsi_num ||
		    pair->txq.queue_id != pair->rxq.queue_id ||
		    pair->txq.queue_id >= vf->vsi.num_tx_queues) {

			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_CONFIG_VSI_QUEUES, I40E_ERR_PARAM);
			return;
		}

		if (ixl_vf_config_tx_queue(pf, vf, &pair->txq) != 0) {
			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_CONFIG_VSI_QUEUES, I40E_ERR_PARAM);
			return;
		}

		if (ixl_vf_config_rx_queue(pf, vf, &pair->rxq) != 0) {
			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_CONFIG_VSI_QUEUES, I40E_ERR_PARAM);
			return;
		}
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_CONFIG_VSI_QUEUES);
}

static void
ixl_vf_set_qctl(struct ixl_pf *pf,
    const struct virtchnl_vector_map *vector,
    enum i40e_queue_type cur_type, uint16_t cur_queue,
    enum i40e_queue_type *last_type, uint16_t *last_queue)
{
	uint32_t offset, qctl;
	uint16_t itr_indx;

	if (cur_type == I40E_QUEUE_TYPE_RX) {
		offset = I40E_QINT_RQCTL(cur_queue);
		itr_indx = vector->rxitr_idx;
	} else {
		offset = I40E_QINT_TQCTL(cur_queue);
		itr_indx = vector->txitr_idx;
	}

	qctl = htole32((vector->vector_id << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
	    (*last_type << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT) |
	    (*last_queue << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT) |
	    I40E_QINT_RQCTL_CAUSE_ENA_MASK |
	    (itr_indx << I40E_QINT_RQCTL_ITR_INDX_SHIFT));

	wr32(&pf->hw, offset, qctl);

	*last_type = cur_type;
	*last_queue = cur_queue;
}

static void
ixl_vf_config_vector(struct ixl_pf *pf, struct ixl_vf *vf,
    const struct virtchnl_vector_map *vector)
{
	struct i40e_hw *hw;
	u_int qindex;
	enum i40e_queue_type type, last_type;
	uint32_t lnklst_reg;
	uint16_t rxq_map, txq_map, cur_queue, last_queue;

	hw = &pf->hw;

	rxq_map = vector->rxq_map;
	txq_map = vector->txq_map;

	last_queue = IXL_END_OF_INTR_LNKLST;
	last_type = I40E_QUEUE_TYPE_RX;

	/*
	 * The datasheet says to optimize performance, RX queues and TX queues
	 * should be interleaved in the interrupt linked list, so we process
	 * both at once here.
	 */
	while ((rxq_map != 0) || (txq_map != 0)) {
		if (txq_map != 0) {
			qindex = ffs(txq_map) - 1;
			type = I40E_QUEUE_TYPE_TX;
			cur_queue = ixl_pf_qidx_from_vsi_qidx(&vf->qtag, qindex);
			ixl_vf_set_qctl(pf, vector, type, cur_queue,
			    &last_type, &last_queue);
			txq_map &= ~(1 << qindex);
		}

		if (rxq_map != 0) {
			qindex = ffs(rxq_map) - 1;
			type = I40E_QUEUE_TYPE_RX;
			cur_queue = ixl_pf_qidx_from_vsi_qidx(&vf->qtag, qindex);
			ixl_vf_set_qctl(pf, vector, type, cur_queue,
			    &last_type, &last_queue);
			rxq_map &= ~(1 << qindex);
		}
	}

	if (vector->vector_id == 0)
		lnklst_reg = I40E_VPINT_LNKLST0(vf->vf_num);
	else
		lnklst_reg = IXL_VPINT_LNKLSTN_REG(hw, vector->vector_id,
		    vf->vf_num);
	wr32(hw, lnklst_reg,
	    (last_queue << I40E_VPINT_LNKLST0_FIRSTQ_INDX_SHIFT) |
	    (last_type << I40E_VPINT_LNKLST0_FIRSTQ_TYPE_SHIFT));

	ixl_flush(hw);
}

static void
ixl_vf_config_irq_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_irq_map_info *map;
	struct virtchnl_vector_map *vector;
	struct i40e_hw *hw;
	int i, largest_txq, largest_rxq;

	hw = &pf->hw;

	if (msg_size < sizeof(*map)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_IRQ_MAP,
		    I40E_ERR_PARAM);
		return;
	}

	map = msg;
	if (map->num_vectors == 0) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_IRQ_MAP,
		    I40E_ERR_PARAM);
		return;
	}

	if (msg_size != sizeof(*map) + map->num_vectors * sizeof(*vector)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_IRQ_MAP,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < map->num_vectors; i++) {
		vector = &map->vecmap[i];

		if ((vector->vector_id >= hw->func_caps.num_msix_vectors_vf) ||
		    vector->vsi_id != vf->vsi.vsi_num) {
			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_CONFIG_IRQ_MAP, I40E_ERR_PARAM);
			return;
		}

		if (vector->rxq_map != 0) {
			largest_rxq = fls(vector->rxq_map) - 1;
			if (largest_rxq >= vf->vsi.num_rx_queues) {
				i40e_send_vf_nack(pf, vf,
				    VIRTCHNL_OP_CONFIG_IRQ_MAP,
				    I40E_ERR_PARAM);
				return;
			}
		}

		if (vector->txq_map != 0) {
			largest_txq = fls(vector->txq_map) - 1;
			if (largest_txq >= vf->vsi.num_tx_queues) {
				i40e_send_vf_nack(pf, vf,
				    VIRTCHNL_OP_CONFIG_IRQ_MAP,
				    I40E_ERR_PARAM);
				return;
			}
		}

		if (vector->rxitr_idx > IXL_MAX_ITR_IDX ||
		    vector->txitr_idx > IXL_MAX_ITR_IDX) {
			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_CONFIG_IRQ_MAP,
			    I40E_ERR_PARAM);
			return;
		}

		ixl_vf_config_vector(pf, vf, vector);
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_CONFIG_IRQ_MAP);
}

static void
ixl_vf_enable_queues_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_queue_select *select;
	int error = 0;

	if (msg_size != sizeof(*select)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ENABLE_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	select = msg;
	if (select->vsi_id != vf->vsi.vsi_num ||
	    select->rx_queues == 0 || select->tx_queues == 0) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ENABLE_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	/* Enable TX rings selected by the VF */
	for (int i = 0; i < 32; i++) {
		if ((1 << i) & select->tx_queues) {
			/* Warn if queue is out of VF allocation range */
			if (i >= vf->vsi.num_tx_queues) {
				device_printf(pf->dev, "VF %d: TX ring %d is outside of VF VSI allocation!\n",
				    vf->vf_num, i);
				break;
			}
			/* Skip this queue if it hasn't been configured */
			if (!ixl_pf_qmgr_is_queue_configured(&vf->qtag, i, true))
				continue;
			/* Warn if this queue is already marked as enabled */
			if (ixl_pf_qmgr_is_queue_enabled(&vf->qtag, i, true))
				ixl_dbg_iov(pf, "VF %d: TX ring %d is already enabled!\n",
				    vf->vf_num, i);

			error = ixl_enable_tx_ring(pf, &vf->qtag, i);
			if (error)
				break;
			else
				ixl_pf_qmgr_mark_queue_enabled(&vf->qtag, i, true);
		}
	}

	/* Enable RX rings selected by the VF */
	for (int i = 0; i < 32; i++) {
		if ((1 << i) & select->rx_queues) {
			/* Warn if queue is out of VF allocation range */
			if (i >= vf->vsi.num_rx_queues) {
				device_printf(pf->dev, "VF %d: RX ring %d is outside of VF VSI allocation!\n",
				    vf->vf_num, i);
				break;
			}
			/* Skip this queue if it hasn't been configured */
			if (!ixl_pf_qmgr_is_queue_configured(&vf->qtag, i, false))
				continue;
			/* Warn if this queue is already marked as enabled */
			if (ixl_pf_qmgr_is_queue_enabled(&vf->qtag, i, false))
				ixl_dbg_iov(pf, "VF %d: RX ring %d is already enabled!\n",
				    vf->vf_num, i);
			error = ixl_enable_rx_ring(pf, &vf->qtag, i);
			if (error)
				break;
			else
				ixl_pf_qmgr_mark_queue_enabled(&vf->qtag, i, false);
		}
	}

	if (error) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ENABLE_QUEUES,
		    I40E_ERR_TIMEOUT);
		return;
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_ENABLE_QUEUES);
}

static void
ixl_vf_disable_queues_msg(struct ixl_pf *pf, struct ixl_vf *vf,
    void *msg, uint16_t msg_size)
{
	struct virtchnl_queue_select *select;
	int error = 0;

	if (msg_size != sizeof(*select)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_DISABLE_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	select = msg;
	if (select->vsi_id != vf->vsi.vsi_num ||
	    select->rx_queues == 0 || select->tx_queues == 0) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_DISABLE_QUEUES,
		    I40E_ERR_PARAM);
		return;
	}

	/* Disable TX rings selected by the VF */
	for (int i = 0; i < 32; i++) {
		if ((1 << i) & select->tx_queues) {
			/* Warn if queue is out of VF allocation range */
			if (i >= vf->vsi.num_tx_queues) {
				device_printf(pf->dev, "VF %d: TX ring %d is outside of VF VSI allocation!\n",
				    vf->vf_num, i);
				break;
			}
			/* Skip this queue if it hasn't been configured */
			if (!ixl_pf_qmgr_is_queue_configured(&vf->qtag, i, true))
				continue;
			/* Warn if this queue is already marked as disabled */
			if (!ixl_pf_qmgr_is_queue_enabled(&vf->qtag, i, true)) {
				ixl_dbg_iov(pf, "VF %d: TX ring %d is already disabled!\n",
				    vf->vf_num, i);
				continue;
			}
			error = ixl_disable_tx_ring(pf, &vf->qtag, i);
			if (error)
				break;
			else
				ixl_pf_qmgr_mark_queue_disabled(&vf->qtag, i, true);
		}
	}

	/* Enable RX rings selected by the VF */
	for (int i = 0; i < 32; i++) {
		if ((1 << i) & select->rx_queues) {
			/* Warn if queue is out of VF allocation range */
			if (i >= vf->vsi.num_rx_queues) {
				device_printf(pf->dev, "VF %d: RX ring %d is outside of VF VSI allocation!\n",
				    vf->vf_num, i);
				break;
			}
			/* Skip this queue if it hasn't been configured */
			if (!ixl_pf_qmgr_is_queue_configured(&vf->qtag, i, false))
				continue;
			/* Warn if this queue is already marked as disabled */
			if (!ixl_pf_qmgr_is_queue_enabled(&vf->qtag, i, false)) {
				ixl_dbg_iov(pf, "VF %d: RX ring %d is already disabled!\n",
				    vf->vf_num, i);
				continue;
			}
			error = ixl_disable_rx_ring(pf, &vf->qtag, i);
			if (error)
				break;
			else
				ixl_pf_qmgr_mark_queue_disabled(&vf->qtag, i, false);
		}
	}

	if (error) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_DISABLE_QUEUES,
		    I40E_ERR_TIMEOUT);
		return;
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_DISABLE_QUEUES);
}

static bool
ixl_zero_mac(const uint8_t *addr)
{
	uint8_t zero[ETHER_ADDR_LEN] = {0, 0, 0, 0, 0, 0};

	return (cmp_etheraddr(addr, zero));
}

static bool
ixl_bcast_mac(const uint8_t *addr)
{
	static uint8_t ixl_bcast_addr[ETHER_ADDR_LEN] =
	    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	return (cmp_etheraddr(addr, ixl_bcast_addr));
}

static int
ixl_vf_mac_valid(struct ixl_vf *vf, const uint8_t *addr)
{

	if (ixl_zero_mac(addr) || ixl_bcast_mac(addr))
		return (EINVAL);

	/*
	 * If the VF is not allowed to change its MAC address, don't let it
	 * set a MAC filter for an address that is not a multicast address and
	 * is not its assigned MAC.
	 */
	if (!(vf->vf_flags & VF_FLAG_SET_MAC_CAP) &&
	    !(ETHER_IS_MULTICAST(addr) || cmp_etheraddr(addr, vf->mac)))
		return (EPERM);

	return (0);
}

static void
ixl_vf_add_mac_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_ether_addr_list *addr_list;
	struct virtchnl_ether_addr *addr;
	struct ixl_vsi *vsi;
	int i;
	size_t expected_size;

	vsi = &vf->vsi;

	if (msg_size < sizeof(*addr_list)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_ETH_ADDR,
		    I40E_ERR_PARAM);
		return;
	}

	addr_list = msg;
	expected_size = sizeof(*addr_list) +
	    addr_list->num_elements * sizeof(*addr);

	if (addr_list->num_elements == 0 ||
	    addr_list->vsi_id != vsi->vsi_num ||
	    msg_size != expected_size) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_ETH_ADDR,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < addr_list->num_elements; i++) {
		if (ixl_vf_mac_valid(vf, addr_list->list[i].addr) != 0) {
			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_ADD_ETH_ADDR, I40E_ERR_PARAM);
			return;
		}
	}

	for (i = 0; i < addr_list->num_elements; i++) {
		addr = &addr_list->list[i];
		ixl_add_filter(vsi, addr->addr, IXL_VLAN_ANY);
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_ADD_ETH_ADDR);
}

static void
ixl_vf_del_mac_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_ether_addr_list *addr_list;
	struct virtchnl_ether_addr *addr;
	size_t expected_size;
	int i;

	if (msg_size < sizeof(*addr_list)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_ETH_ADDR,
		    I40E_ERR_PARAM);
		return;
	}

	addr_list = msg;
	expected_size = sizeof(*addr_list) +
	    addr_list->num_elements * sizeof(*addr);

	if (addr_list->num_elements == 0 ||
	    addr_list->vsi_id != vf->vsi.vsi_num ||
	    msg_size != expected_size) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_ETH_ADDR,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < addr_list->num_elements; i++) {
		addr = &addr_list->list[i];
		if (ixl_zero_mac(addr->addr) || ixl_bcast_mac(addr->addr)) {
			i40e_send_vf_nack(pf, vf,
			    VIRTCHNL_OP_ADD_ETH_ADDR, I40E_ERR_PARAM);
			return;
		}
	}

	for (i = 0; i < addr_list->num_elements; i++) {
		addr = &addr_list->list[i];
		ixl_del_filter(&vf->vsi, addr->addr, IXL_VLAN_ANY);
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_DEL_ETH_ADDR);
}

static enum i40e_status_code
ixl_vf_enable_vlan_strip(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct i40e_vsi_context vsi_ctx;

	vsi_ctx.seid = vf->vsi.seid;

	bzero(&vsi_ctx.info, sizeof(vsi_ctx.info));
	vsi_ctx.info.valid_sections = htole16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi_ctx.info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL |
	    I40E_AQ_VSI_PVLAN_EMOD_STR_BOTH;
	return (i40e_aq_update_vsi_params(&pf->hw, &vsi_ctx, NULL));
}

static void
ixl_vf_add_vlan_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_vlan_filter_list *filter_list;
	enum i40e_status_code code;
	size_t expected_size;
	int i;

	if (msg_size < sizeof(*filter_list)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
		    I40E_ERR_PARAM);
		return;
	}

	filter_list = msg;
	expected_size = sizeof(*filter_list) +
	    filter_list->num_elements * sizeof(uint16_t);
	if (filter_list->num_elements == 0 ||
	    filter_list->vsi_id != vf->vsi.vsi_num ||
	    msg_size != expected_size) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
		    I40E_ERR_PARAM);
		return;
	}

	if (!(vf->vf_flags & VF_FLAG_VLAN_CAP)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < filter_list->num_elements; i++) {
		if (filter_list->vlan_id[i] > EVL_VLID_MASK) {
			i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
			    I40E_ERR_PARAM);
			return;
		}
	}

	code = ixl_vf_enable_vlan_strip(pf, vf);
	if (code != I40E_SUCCESS) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
		    I40E_ERR_PARAM);
	}

	for (i = 0; i < filter_list->num_elements; i++)
		ixl_add_filter(&vf->vsi, vf->mac, filter_list->vlan_id[i]);

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_ADD_VLAN);
}

static void
ixl_vf_del_vlan_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_vlan_filter_list *filter_list;
	int i;
	size_t expected_size;

	if (msg_size < sizeof(*filter_list)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_DEL_VLAN,
		    I40E_ERR_PARAM);
		return;
	}

	filter_list = msg;
	expected_size = sizeof(*filter_list) +
	    filter_list->num_elements * sizeof(uint16_t);
	if (filter_list->num_elements == 0 ||
	    filter_list->vsi_id != vf->vsi.vsi_num ||
	    msg_size != expected_size) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_DEL_VLAN,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < filter_list->num_elements; i++) {
		if (filter_list->vlan_id[i] > EVL_VLID_MASK) {
			i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
			    I40E_ERR_PARAM);
			return;
		}
	}

	if (!(vf->vf_flags & VF_FLAG_VLAN_CAP)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_ADD_VLAN,
		    I40E_ERR_PARAM);
		return;
	}

	for (i = 0; i < filter_list->num_elements; i++)
		ixl_del_filter(&vf->vsi, vf->mac, filter_list->vlan_id[i]);

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_DEL_VLAN);
}

static void
ixl_vf_config_promisc_msg(struct ixl_pf *pf, struct ixl_vf *vf,
    void *msg, uint16_t msg_size)
{
	struct virtchnl_promisc_info *info;
	struct i40e_hw *hw = &pf->hw;
	enum i40e_status_code code;

	if (msg_size != sizeof(*info)) {
		i40e_send_vf_nack(pf, vf,
		    VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE, I40E_ERR_PARAM);
		return;
	}

	if (!(vf->vf_flags & VF_FLAG_PROMISC_CAP)) {
		/*
		 * Do the same thing as the Linux PF driver -- lie to the VF
		 */
		ixl_send_vf_ack(pf, vf,
		    VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE);
		return;
	}

	info = msg;
	if (info->vsi_id != vf->vsi.vsi_num) {
		i40e_send_vf_nack(pf, vf,
		    VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE, I40E_ERR_PARAM);
		return;
	}

	code = i40e_aq_set_vsi_unicast_promiscuous(hw, vf->vsi.seid,
	    info->flags & FLAG_VF_UNICAST_PROMISC, NULL, TRUE);
	if (code != I40E_SUCCESS) {
		device_printf(pf->dev, "i40e_aq_set_vsi_unicast_promiscuous (seid %d) failed: status %s,"
		    " error %s\n", vf->vsi.seid, i40e_stat_str(hw, code),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		i40e_send_vf_nack(pf, vf,
		    VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE, I40E_ERR_PARAM);
		return;
	}

	code = i40e_aq_set_vsi_multicast_promiscuous(hw, vf->vsi.seid,
	    info->flags & FLAG_VF_MULTICAST_PROMISC, NULL);
	if (code != I40E_SUCCESS) {
		device_printf(pf->dev, "i40e_aq_set_vsi_multicast_promiscuous (seid %d) failed: status %s,"
		    " error %s\n", vf->vsi.seid, i40e_stat_str(hw, code),
		    i40e_aq_str(hw, hw->aq.asq_last_status));
		i40e_send_vf_nack(pf, vf,
		    VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE, I40E_ERR_PARAM);
		return;
	}

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE);
}

static void
ixl_vf_get_stats_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct virtchnl_queue_select *queue;

	if (msg_size != sizeof(*queue)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_GET_STATS,
		    I40E_ERR_PARAM);
		return;
	}

	queue = msg;
	if (queue->vsi_id != vf->vsi.vsi_num) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_GET_STATS,
		    I40E_ERR_PARAM);
		return;
	}

	ixl_update_eth_stats(&vf->vsi);

	ixl_send_vf_msg(pf, vf, VIRTCHNL_OP_GET_STATS,
	    I40E_SUCCESS, &vf->vsi.eth_stats, sizeof(vf->vsi.eth_stats));
}

static void
ixl_vf_config_rss_key_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct i40e_hw *hw;
	struct virtchnl_rss_key *key;
	struct i40e_aqc_get_set_rss_key_data key_data;
	enum i40e_status_code status;

	hw = &pf->hw;

	if (msg_size < sizeof(*key)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_KEY,
		    I40E_ERR_PARAM);
		return;
	}

	key = msg;

	if (key->key_len > 52) {
		device_printf(pf->dev, "VF %d: Key size in msg (%d) is greater than max key size (%d)\n",
		    vf->vf_num, key->key_len, 52);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_KEY,
		    I40E_ERR_PARAM);
		return;
	}

	if (key->vsi_id != vf->vsi.vsi_num) {
		device_printf(pf->dev, "VF %d: VSI id in recvd message (%d) does not match expected id (%d)\n",
		    vf->vf_num, key->vsi_id, vf->vsi.vsi_num);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_KEY,
		    I40E_ERR_PARAM);
		return;
	}

	/* Fill out hash using MAC-dependent method */
	if (hw->mac.type == I40E_MAC_X722) {
		bzero(&key_data, sizeof(key_data));
		if (key->key_len <= 40)
			bcopy(key->key, key_data.standard_rss_key, key->key_len);
		else {
			bcopy(key->key, key_data.standard_rss_key, 40);
			bcopy(&key->key[40], key_data.extended_hash_key, key->key_len - 40);
		}
		status = i40e_aq_set_rss_key(hw, vf->vsi.vsi_num, &key_data);
		if (status) {
			device_printf(pf->dev, "i40e_aq_set_rss_key status %s, error %s\n",
			    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
			i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_KEY,
			    I40E_ERR_ADMIN_QUEUE_ERROR);
			return;
		}
	} else {
		for (int i = 0; i < (key->key_len / 4); i++)
			i40e_write_rx_ctl(hw, I40E_VFQF_HKEY1(i, vf->vf_num), ((u32 *)key->key)[i]);
	}

	DDPRINTF(pf->dev, "VF %d: Programmed key starting with 0x%x ok!",
	    vf->vf_num, key->key[0]);

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_KEY);
}

static void
ixl_vf_config_rss_lut_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct i40e_hw *hw;
	struct virtchnl_rss_lut *lut;
	enum i40e_status_code status;

	hw = &pf->hw;

	if (msg_size < sizeof(*lut)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_LUT,
		    I40E_ERR_PARAM);
		return;
	}

	lut = msg;

	if (lut->lut_entries > 64) {
		device_printf(pf->dev, "VF %d: # of LUT entries in msg (%d) is greater than max (%d)\n",
		    vf->vf_num, lut->lut_entries, 64);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_LUT,
		    I40E_ERR_PARAM);
		return;
	}

	if (lut->vsi_id != vf->vsi.vsi_num) {
		device_printf(pf->dev, "VF %d: VSI id in recvd message (%d) does not match expected id (%d)\n",
		    vf->vf_num, lut->vsi_id, vf->vsi.vsi_num);
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_LUT,
		    I40E_ERR_PARAM);
		return;
	}

	/* Fill out LUT using MAC-dependent method */
	if (hw->mac.type == I40E_MAC_X722) {
		status = i40e_aq_set_rss_lut(hw, vf->vsi.vsi_num, false, lut->lut, lut->lut_entries);
		if (status) {
			device_printf(pf->dev, "i40e_aq_set_rss_lut status %s, error %s\n",
			    i40e_stat_str(hw, status), i40e_aq_str(hw, hw->aq.asq_last_status));
			i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_LUT,
			    I40E_ERR_ADMIN_QUEUE_ERROR);
			return;
		}
	} else {
		for (int i = 0; i < (lut->lut_entries / 4); i++)
			i40e_write_rx_ctl(hw, I40E_VFQF_HLUT1(i, vf->vf_num), ((u32 *)lut->lut)[i]);
	}

	DDPRINTF(pf->dev, "VF %d: Programmed LUT starting with 0x%x and length %d ok!",
	    vf->vf_num, lut->lut[0], lut->lut_entries);

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_CONFIG_RSS_LUT);
}

static void
ixl_vf_set_rss_hena_msg(struct ixl_pf *pf, struct ixl_vf *vf, void *msg,
    uint16_t msg_size)
{
	struct i40e_hw *hw;
	struct virtchnl_rss_hena *hena;

	hw = &pf->hw;

	if (msg_size < sizeof(*hena)) {
		i40e_send_vf_nack(pf, vf, VIRTCHNL_OP_SET_RSS_HENA,
		    I40E_ERR_PARAM);
		return;
	}

	hena = msg;

	/* Set HENA */
	i40e_write_rx_ctl(hw, I40E_VFQF_HENA1(0, vf->vf_num), (u32)hena->hena);
	i40e_write_rx_ctl(hw, I40E_VFQF_HENA1(1, vf->vf_num), (u32)(hena->hena >> 32));

	DDPRINTF(pf->dev, "VF %d: Programmed HENA with 0x%016lx",
	    vf->vf_num, hena->hena);

	ixl_send_vf_ack(pf, vf, VIRTCHNL_OP_SET_RSS_HENA);
}

static void
ixl_notify_vf_link_state(struct ixl_pf *pf, struct ixl_vf *vf)
{
	struct virtchnl_pf_event event;
	struct i40e_hw *hw;

	hw = &pf->hw;
	event.event = VIRTCHNL_EVENT_LINK_CHANGE;
	event.severity = PF_EVENT_SEVERITY_INFO;
	event.event_data.link_event.link_status = pf->vsi.link_active;
	event.event_data.link_event.link_speed =
		(enum virtchnl_link_speed)hw->phy.link_info.link_speed;

	ixl_send_vf_msg(pf, vf, VIRTCHNL_OP_EVENT, I40E_SUCCESS, &event,
			sizeof(event));
}

void
ixl_broadcast_link_state(struct ixl_pf *pf)
{
	int i;

	for (i = 0; i < pf->num_vfs; i++)
		ixl_notify_vf_link_state(pf, &pf->vfs[i]);
}

void
ixl_handle_vf_msg(struct ixl_pf *pf, struct i40e_arq_event_info *event)
{
	struct ixl_vf *vf;
	void *msg;
	uint16_t vf_num, msg_size;
	uint32_t opcode;

	vf_num = le16toh(event->desc.retval) - pf->hw.func_caps.vf_base_id;
	opcode = le32toh(event->desc.cookie_high);

	if (vf_num >= pf->num_vfs) {
		device_printf(pf->dev, "Got msg from illegal VF: %d\n", vf_num);
		return;
	}

	vf = &pf->vfs[vf_num];
	msg = event->msg_buf;
	msg_size = event->msg_len;

	I40E_VC_DEBUG(pf, ixl_vc_opcode_level(opcode),
	    "Got msg %s(%d) from%sVF-%d of size %d\n",
	    ixl_vc_opcode_str(opcode), opcode,
	    (vf->vf_flags & VF_FLAG_ENABLED) ? " " : " disabled ",
	    vf_num, msg_size);

	/* This must be a stray msg from a previously destroyed VF. */
	if (!(vf->vf_flags & VF_FLAG_ENABLED))
		return;

	switch (opcode) {
	case VIRTCHNL_OP_VERSION:
		ixl_vf_version_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_RESET_VF:
		ixl_vf_reset_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_GET_VF_RESOURCES:
		ixl_vf_get_resources_msg(pf, vf, msg, msg_size);
		/* Notify VF of link state after it obtains queues, as this is
		 * the last thing it will do as part of initialization
		 */
		ixl_notify_vf_link_state(pf, vf);
		break;
	case VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		ixl_vf_config_vsi_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		ixl_vf_config_irq_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		ixl_vf_enable_queues_msg(pf, vf, msg, msg_size);
		/* Notify VF of link state after it obtains queues, as this is
		 * the last thing it will do as part of initialization
		 */
		ixl_notify_vf_link_state(pf, vf);
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		ixl_vf_disable_queues_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		ixl_vf_add_mac_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_DEL_ETH_ADDR:
		ixl_vf_del_mac_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_ADD_VLAN:
		ixl_vf_add_vlan_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_DEL_VLAN:
		ixl_vf_del_vlan_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
		ixl_vf_config_promisc_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_GET_STATS:
		ixl_vf_get_stats_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_KEY:
		ixl_vf_config_rss_key_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_CONFIG_RSS_LUT:
		ixl_vf_config_rss_lut_msg(pf, vf, msg, msg_size);
		break;
	case VIRTCHNL_OP_SET_RSS_HENA:
		ixl_vf_set_rss_hena_msg(pf, vf, msg, msg_size);
		break;

	/* These two opcodes have been superseded by CONFIG_VSI_QUEUES. */
	case VIRTCHNL_OP_CONFIG_TX_QUEUE:
	case VIRTCHNL_OP_CONFIG_RX_QUEUE:
	default:
		i40e_send_vf_nack(pf, vf, opcode, I40E_ERR_NOT_IMPLEMENTED);
		break;
	}
}

/* Handle any VFs that have reset themselves via a Function Level Reset(FLR). */
void
ixl_handle_vflr(struct ixl_pf *pf)
{
	struct ixl_vf *vf;
	struct i40e_hw *hw;
	uint16_t global_vf_num;
	uint32_t vflrstat_index, vflrstat_mask, vflrstat, icr0;
	int i;

	hw = &pf->hw;

	ixl_dbg_iov(pf, "%s: begin\n", __func__);

	/* Re-enable VFLR interrupt cause so driver doesn't miss a
	 * reset interrupt for another VF */
	icr0 = rd32(hw, I40E_PFINT_ICR0_ENA);
	icr0 |= I40E_PFINT_ICR0_ENA_VFLR_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, icr0);
	ixl_flush(hw);

	for (i = 0; i < pf->num_vfs; i++) {
		global_vf_num = hw->func_caps.vf_base_id + i;

		vf = &pf->vfs[i];
		if (!(vf->vf_flags & VF_FLAG_ENABLED))
			continue;

		vflrstat_index = IXL_GLGEN_VFLRSTAT_INDEX(global_vf_num);
		vflrstat_mask = IXL_GLGEN_VFLRSTAT_MASK(global_vf_num);
		vflrstat = rd32(hw, I40E_GLGEN_VFLRSTAT(vflrstat_index));
		if (vflrstat & vflrstat_mask) {
			wr32(hw, I40E_GLGEN_VFLRSTAT(vflrstat_index),
			    vflrstat_mask);

			ixl_dbg_iov(pf, "Reinitializing VF-%d\n", i);
			ixl_reinit_vf(pf, vf);
			ixl_dbg_iov(pf, "Reinitializing VF-%d done\n", i);
		}
	}

}

static int
ixl_adminq_err_to_errno(enum i40e_admin_queue_err err)
{

	switch (err) {
	case I40E_AQ_RC_EPERM:
		return (EPERM);
	case I40E_AQ_RC_ENOENT:
		return (ENOENT);
	case I40E_AQ_RC_ESRCH:
		return (ESRCH);
	case I40E_AQ_RC_EINTR:
		return (EINTR);
	case I40E_AQ_RC_EIO:
		return (EIO);
	case I40E_AQ_RC_ENXIO:
		return (ENXIO);
	case I40E_AQ_RC_E2BIG:
		return (E2BIG);
	case I40E_AQ_RC_EAGAIN:
		return (EAGAIN);
	case I40E_AQ_RC_ENOMEM:
		return (ENOMEM);
	case I40E_AQ_RC_EACCES:
		return (EACCES);
	case I40E_AQ_RC_EFAULT:
		return (EFAULT);
	case I40E_AQ_RC_EBUSY:
		return (EBUSY);
	case I40E_AQ_RC_EEXIST:
		return (EEXIST);
	case I40E_AQ_RC_EINVAL:
		return (EINVAL);
	case I40E_AQ_RC_ENOTTY:
		return (ENOTTY);
	case I40E_AQ_RC_ENOSPC:
		return (ENOSPC);
	case I40E_AQ_RC_ENOSYS:
		return (ENOSYS);
	case I40E_AQ_RC_ERANGE:
		return (ERANGE);
	case I40E_AQ_RC_EFLUSHED:
		return (EINVAL);	/* No exact equivalent in errno.h */
	case I40E_AQ_RC_BAD_ADDR:
		return (EFAULT);
	case I40E_AQ_RC_EMODE:
		return (EPERM);
	case I40E_AQ_RC_EFBIG:
		return (EFBIG);
	default:
		return (EINVAL);
	}
}

static int
ixl_config_pf_vsi_loopback(struct ixl_pf *pf, bool enable)
{
	struct i40e_hw *hw = &pf->hw;
	device_t dev = pf->dev;
	struct ixl_vsi *vsi = &pf->vsi;
	struct i40e_vsi_context	ctxt;
	int error;

	memset(&ctxt, 0, sizeof(ctxt));

	ctxt.seid = vsi->seid;
	if (pf->veb_seid != 0)
		ctxt.uplink_seid = pf->veb_seid;
	ctxt.pf_num = hw->pf_id;
	ctxt.connection_type = IXL_VSI_DATA_PORT;

	ctxt.info.valid_sections = htole16(I40E_AQ_VSI_PROP_SWITCH_VALID);
	ctxt.info.switch_id = (enable) ?
	    htole16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB) : 0;

	/* error is set to 0 on success */
	error = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
	if (error) {
		device_printf(dev, "i40e_aq_update_vsi_params() failed, error %d,"
		    " aq_error %d\n", error, hw->aq.asq_last_status);
	}

	return (error);
}

int
ixl_if_iov_init(if_ctx_t ctx, uint16_t num_vfs, const nvlist_t *params)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	device_t dev = iflib_get_dev(ctx);
	struct i40e_hw *hw;
	struct ixl_vsi *pf_vsi;
	enum i40e_status_code ret;
	int i, error;

	hw = &pf->hw;
	pf_vsi = &pf->vsi;

	pf->vfs = malloc(sizeof(struct ixl_vf) * num_vfs, M_IXL, M_NOWAIT |
	    M_ZERO);
	if (pf->vfs == NULL) {
		error = ENOMEM;
		goto fail;
	}

	for (i = 0; i < num_vfs; i++)
		sysctl_ctx_init(&pf->vfs[i].ctx);

	/*
	 * Add the VEB and ...
	 * - do nothing: VEPA mode
	 * - enable loopback mode on connected VSIs: VEB mode
	 */
	ret = i40e_aq_add_veb(hw, pf_vsi->uplink_seid, pf_vsi->seid,
	    1, FALSE, &pf->veb_seid, FALSE, NULL);
	if (ret != I40E_SUCCESS) {
		error = hw->aq.asq_last_status;
		device_printf(dev, "i40e_aq_add_veb failed; status %s error %s",
		    i40e_stat_str(hw, ret), i40e_aq_str(hw, error));
		goto fail;
	}
	if (pf->enable_vf_loopback)
		ixl_config_pf_vsi_loopback(pf, true);

	/*
	 * Adding a VEB brings back the default MAC filter(s). Remove them,
	 * and let the driver add the proper filters back.
	 */
	ixl_del_default_hw_filters(pf_vsi);
	ixl_reconfigure_filters(pf_vsi);

	pf->num_vfs = num_vfs;
	return (0);

fail:
	free(pf->vfs, M_IXL);
	pf->vfs = NULL;
	return (error);
}

void
ixl_if_iov_uninit(if_ctx_t ctx)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	struct i40e_hw *hw;
	struct ixl_vsi *vsi;
	struct ifnet *ifp;
	struct ixl_vf *vfs;
	int i, num_vfs;

	hw = &pf->hw;
	vsi = &pf->vsi;
	ifp = vsi->ifp;

	for (i = 0; i < pf->num_vfs; i++) {
		if (pf->vfs[i].vsi.seid != 0)
			i40e_aq_delete_element(hw, pf->vfs[i].vsi.seid, NULL);
		ixl_pf_qmgr_release(&pf->qmgr, &pf->vfs[i].qtag);
		ixl_free_mac_filters(&pf->vfs[i].vsi);
		ixl_dbg_iov(pf, "VF %d: %d released\n",
		    i, pf->vfs[i].qtag.num_allocated);
		ixl_dbg_iov(pf, "Unallocated total: %d\n", ixl_pf_qmgr_get_num_free(&pf->qmgr));
	}

	if (pf->veb_seid != 0) {
		i40e_aq_delete_element(hw, pf->veb_seid, NULL);
		pf->veb_seid = 0;
	}
	/* Reset PF VSI loopback mode */
	if (pf->enable_vf_loopback)
		ixl_config_pf_vsi_loopback(pf, false);

	vfs = pf->vfs;
	num_vfs = pf->num_vfs;

	pf->vfs = NULL;
	pf->num_vfs = 0;

	/* sysctl_ctx_free might sleep, but this func is called w/ an sx lock */
	for (i = 0; i < num_vfs; i++)
		sysctl_ctx_free(&vfs[i].ctx);
	free(vfs, M_IXL);
}

static int
ixl_vf_reserve_queues(struct ixl_pf *pf, struct ixl_vf *vf, int num_queues)
{
	device_t dev = pf->dev;
	int error;

	/* Validate, and clamp value if invalid */
	if (num_queues < 1 || num_queues > 16)
		device_printf(dev, "Invalid num-queues (%d) for VF %d\n",
		    num_queues, vf->vf_num);
	if (num_queues < 1) {
		device_printf(dev, "Setting VF %d num-queues to 1\n", vf->vf_num);
		num_queues = 1;
	} else if (num_queues > IAVF_MAX_QUEUES) {
		device_printf(dev, "Setting VF %d num-queues to %d\n", vf->vf_num, IAVF_MAX_QUEUES);
		num_queues = IAVF_MAX_QUEUES;
	}
	error = ixl_pf_qmgr_alloc_scattered(&pf->qmgr, num_queues, &vf->qtag);
	if (error) {
		device_printf(dev, "Error allocating %d queues for VF %d's VSI\n",
		    num_queues, vf->vf_num);
		return (ENOSPC);
	}

	ixl_dbg_iov(pf, "VF %d: %d allocated, %d active\n",
	    vf->vf_num, vf->qtag.num_allocated, vf->qtag.num_active);
	ixl_dbg_iov(pf, "Unallocated total: %d\n", ixl_pf_qmgr_get_num_free(&pf->qmgr));

	return (0);
}

int
ixl_if_iov_vf_add(if_ctx_t ctx, uint16_t vfnum, const nvlist_t *params)
{
	struct ixl_pf *pf = iflib_get_softc(ctx);
	device_t dev = pf->dev;
	char sysctl_name[QUEUE_NAME_LEN];
	struct ixl_vf *vf;
	const void *mac;
	size_t size;
	int error;
	int vf_num_queues;

	vf = &pf->vfs[vfnum];
	vf->vf_num = vfnum;
	vf->vsi.back = pf;
	vf->vf_flags = VF_FLAG_ENABLED;
	SLIST_INIT(&vf->vsi.ftl);

	/* Reserve queue allocation from PF */
	vf_num_queues = nvlist_get_number(params, "num-queues");
	error = ixl_vf_reserve_queues(pf, vf, vf_num_queues);
	if (error != 0)
		goto out;

	error = ixl_vf_setup_vsi(pf, vf);
	if (error != 0)
		goto out;

	if (nvlist_exists_binary(params, "mac-addr")) {
		mac = nvlist_get_binary(params, "mac-addr", &size);
		bcopy(mac, vf->mac, ETHER_ADDR_LEN);

		if (nvlist_get_bool(params, "allow-set-mac"))
			vf->vf_flags |= VF_FLAG_SET_MAC_CAP;
	} else
		/*
		 * If the administrator has not specified a MAC address then
		 * we must allow the VF to choose one.
		 */
		vf->vf_flags |= VF_FLAG_SET_MAC_CAP;

	if (nvlist_get_bool(params, "mac-anti-spoof"))
		vf->vf_flags |= VF_FLAG_MAC_ANTI_SPOOF;

	if (nvlist_get_bool(params, "allow-promisc"))
		vf->vf_flags |= VF_FLAG_PROMISC_CAP;

	vf->vf_flags |= VF_FLAG_VLAN_CAP;

	/* VF needs to be reset before it can be used */
	ixl_reset_vf(pf, vf);
out:
	if (error == 0) {
		snprintf(sysctl_name, sizeof(sysctl_name), "vf%d", vfnum);
		ixl_add_vsi_sysctls(dev, &vf->vsi, &vf->ctx, sysctl_name);
	}

	return (error);
}

