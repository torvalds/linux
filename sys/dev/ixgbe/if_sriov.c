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
#include "ixgbe_sriov.h"

#ifdef PCI_IOV

MALLOC_DEFINE(M_IXGBE_SRIOV, "ix_sriov", "ix SR-IOV allocations");

/************************************************************************
 * ixgbe_pci_iov_detach
 ************************************************************************/
int
ixgbe_pci_iov_detach(device_t dev)
{
	return pci_iov_detach(dev);
}

/************************************************************************
 * ixgbe_define_iov_schemas
 ************************************************************************/
void
ixgbe_define_iov_schemas(device_t dev, int *error)
{
	nvlist_t *pf_schema, *vf_schema;

	pf_schema = pci_iov_schema_alloc_node();
	vf_schema = pci_iov_schema_alloc_node();
	pci_iov_schema_add_unicast_mac(vf_schema, "mac-addr", 0, NULL);
	pci_iov_schema_add_bool(vf_schema, "mac-anti-spoof",
	    IOV_SCHEMA_HASDEFAULT, TRUE);
	pci_iov_schema_add_bool(vf_schema, "allow-set-mac",
	    IOV_SCHEMA_HASDEFAULT, FALSE);
	pci_iov_schema_add_bool(vf_schema, "allow-promisc",
	    IOV_SCHEMA_HASDEFAULT, FALSE);
	*error = pci_iov_attach(dev, pf_schema, vf_schema);
	if (*error != 0) {
		device_printf(dev,
		    "Error %d setting up SR-IOV\n", *error);
	}
} /* ixgbe_define_iov_schemas */

/************************************************************************
 * ixgbe_align_all_queue_indices
 ************************************************************************/
inline void
ixgbe_align_all_queue_indices(struct adapter *adapter)
{
	int i;
	int index;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		index = ixgbe_vf_que_index(adapter->iov_mode, adapter->pool, i);
		adapter->rx_queues[i].rxr.me = index;
	}

	for (i = 0; i < adapter->num_tx_queues; i++) {
		index = ixgbe_vf_que_index(adapter->iov_mode, adapter->pool, i);
		adapter->tx_queues[i].txr.me = index;
	}
}

/* Support functions for SR-IOV/VF management */
static inline void
ixgbe_send_vf_msg(struct adapter *adapter, struct ixgbe_vf *vf, u32 msg)
{
	if (vf->flags & IXGBE_VF_CTS)
		msg |= IXGBE_VT_MSGTYPE_CTS;

	adapter->hw.mbx.ops.write(&adapter->hw, &msg, 1, vf->pool);
}

static inline void
ixgbe_send_vf_ack(struct adapter *adapter, struct ixgbe_vf *vf, u32 msg)
{
	msg &= IXGBE_VT_MSG_MASK;
	ixgbe_send_vf_msg(adapter, vf, msg | IXGBE_VT_MSGTYPE_ACK);
}

static inline void
ixgbe_send_vf_nack(struct adapter *adapter, struct ixgbe_vf *vf, u32 msg)
{
	msg &= IXGBE_VT_MSG_MASK;
	ixgbe_send_vf_msg(adapter, vf, msg | IXGBE_VT_MSGTYPE_NACK);
}

static inline void
ixgbe_process_vf_ack(struct adapter *adapter, struct ixgbe_vf *vf)
{
	if (!(vf->flags & IXGBE_VF_CTS))
		ixgbe_send_vf_nack(adapter, vf, 0);
}

static inline boolean_t
ixgbe_vf_mac_changed(struct ixgbe_vf *vf, const uint8_t *mac)
{
	return (bcmp(mac, vf->ether_addr, ETHER_ADDR_LEN) != 0);
}

static inline int
ixgbe_vf_queues(int mode)
{
	switch (mode) {
	case IXGBE_64_VM:
		return (2);
	case IXGBE_32_VM:
		return (4);
	case IXGBE_NO_VM:
	default:
		return (0);
	}
}

inline int
ixgbe_vf_que_index(int mode, int vfnum, int num)
{
	return ((vfnum * ixgbe_vf_queues(mode)) + num);
}

static inline void
ixgbe_update_max_frame(struct adapter * adapter, int max_frame)
{
	if (adapter->max_frame_size < max_frame)
		adapter->max_frame_size = max_frame;
}

inline u32
ixgbe_get_mrqc(int iov_mode)
{
	u32 mrqc;

	switch (iov_mode) {
	case IXGBE_64_VM:
		mrqc = IXGBE_MRQC_VMDQRSS64EN;
		break;
	case IXGBE_32_VM:
		mrqc = IXGBE_MRQC_VMDQRSS32EN;
		break;
	case IXGBE_NO_VM:
		mrqc = 0;
		break;
	default:
		panic("Unexpected SR-IOV mode %d", iov_mode);
	}

	return mrqc;
}


inline u32
ixgbe_get_mtqc(int iov_mode)
{
	uint32_t mtqc;

	switch (iov_mode) {
	case IXGBE_64_VM:
		mtqc = IXGBE_MTQC_64VF | IXGBE_MTQC_VT_ENA;
		break;
	case IXGBE_32_VM:
		mtqc = IXGBE_MTQC_32VF | IXGBE_MTQC_VT_ENA;
		break;
	case IXGBE_NO_VM:
		mtqc = IXGBE_MTQC_64Q_1PB;
		break;
	default:
		panic("Unexpected SR-IOV mode %d", iov_mode);
	}

	return mtqc;
}

void
ixgbe_ping_all_vfs(struct adapter *adapter)
{
	struct ixgbe_vf *vf;

	for (int i = 0; i < adapter->num_vfs; i++) {
		vf = &adapter->vfs[i];
		if (vf->flags & IXGBE_VF_ACTIVE)
			ixgbe_send_vf_msg(adapter, vf, IXGBE_PF_CONTROL_MSG);
	}
} /* ixgbe_ping_all_vfs */


static void
ixgbe_vf_set_default_vlan(struct adapter *adapter, struct ixgbe_vf *vf,
                          uint16_t tag)
{
	struct ixgbe_hw *hw;
	uint32_t vmolr, vmvir;

	hw = &adapter->hw;

	vf->vlan_tag = tag;

	vmolr = IXGBE_READ_REG(hw, IXGBE_VMOLR(vf->pool));

	/* Do not receive packets that pass inexact filters. */
	vmolr &= ~(IXGBE_VMOLR_ROMPE | IXGBE_VMOLR_ROPE);

	/* Disable Multicast Promicuous Mode. */
	vmolr &= ~IXGBE_VMOLR_MPE;

	/* Accept broadcasts. */
	vmolr |= IXGBE_VMOLR_BAM;

	if (tag == 0) {
		/* Accept non-vlan tagged traffic. */
		vmolr |= IXGBE_VMOLR_AUPE;

		/* Allow VM to tag outgoing traffic; no default tag. */
		vmvir = 0;
	} else {
		/* Require vlan-tagged traffic. */
		vmolr &= ~IXGBE_VMOLR_AUPE;

		/* Tag all traffic with provided vlan tag. */
		vmvir = (tag | IXGBE_VMVIR_VLANA_DEFAULT);
	}
	IXGBE_WRITE_REG(hw, IXGBE_VMOLR(vf->pool), vmolr);
	IXGBE_WRITE_REG(hw, IXGBE_VMVIR(vf->pool), vmvir);
} /* ixgbe_vf_set_default_vlan */


static boolean_t
ixgbe_vf_frame_size_compatible(struct adapter *adapter, struct ixgbe_vf *vf)
{

	/*
	 * Frame size compatibility between PF and VF is only a problem on
	 * 82599-based cards.  X540 and later support any combination of jumbo
	 * frames on PFs and VFs.
	 */
	if (adapter->hw.mac.type != ixgbe_mac_82599EB)
		return (TRUE);

	switch (vf->api_ver) {
	case IXGBE_API_VER_1_0:
	case IXGBE_API_VER_UNKNOWN:
		/*
		 * On legacy (1.0 and older) VF versions, we don't support jumbo
		 * frames on either the PF or the VF.
		 */
		if (adapter->max_frame_size > ETHER_MAX_LEN ||
		    vf->maximum_frame_size > ETHER_MAX_LEN)
			return (FALSE);

		return (TRUE);

		break;
	case IXGBE_API_VER_1_1:
	default:
		/*
		 * 1.1 or later VF versions always work if they aren't using
		 * jumbo frames.
		 */
		if (vf->maximum_frame_size <= ETHER_MAX_LEN)
			return (TRUE);

		/*
		 * Jumbo frames only work with VFs if the PF is also using jumbo
		 * frames.
		 */
		if (adapter->max_frame_size <= ETHER_MAX_LEN)
			return (TRUE);

		return (FALSE);
	}
} /* ixgbe_vf_frame_size_compatible */


static void
ixgbe_process_vf_reset(struct adapter *adapter, struct ixgbe_vf *vf)
{
	ixgbe_vf_set_default_vlan(adapter, vf, vf->default_vlan);

	// XXX clear multicast addresses

	ixgbe_clear_rar(&adapter->hw, vf->rar_index);

	vf->api_ver = IXGBE_API_VER_UNKNOWN;
} /* ixgbe_process_vf_reset */


static void
ixgbe_vf_enable_transmit(struct adapter *adapter, struct ixgbe_vf *vf)
{
	struct ixgbe_hw *hw;
	uint32_t vf_index, vfte;

	hw = &adapter->hw;

	vf_index = IXGBE_VF_INDEX(vf->pool);
	vfte = IXGBE_READ_REG(hw, IXGBE_VFTE(vf_index));
	vfte |= IXGBE_VF_BIT(vf->pool);
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(vf_index), vfte);
} /* ixgbe_vf_enable_transmit */


static void
ixgbe_vf_enable_receive(struct adapter *adapter, struct ixgbe_vf *vf)
{
	struct ixgbe_hw *hw;
	uint32_t vf_index, vfre;

	hw = &adapter->hw;

	vf_index = IXGBE_VF_INDEX(vf->pool);
	vfre = IXGBE_READ_REG(hw, IXGBE_VFRE(vf_index));
	if (ixgbe_vf_frame_size_compatible(adapter, vf))
		vfre |= IXGBE_VF_BIT(vf->pool);
	else
		vfre &= ~IXGBE_VF_BIT(vf->pool);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(vf_index), vfre);
} /* ixgbe_vf_enable_receive */


static void
ixgbe_vf_reset_msg(struct adapter *adapter, struct ixgbe_vf *vf, uint32_t *msg)
{
	struct ixgbe_hw *hw;
	uint32_t ack;
	uint32_t resp[IXGBE_VF_PERMADDR_MSG_LEN];

	hw = &adapter->hw;

	ixgbe_process_vf_reset(adapter, vf);

	if (ixgbe_validate_mac_addr(vf->ether_addr) == 0) {
		ixgbe_set_rar(&adapter->hw, vf->rar_index, vf->ether_addr,
		    vf->pool, TRUE);
		ack = IXGBE_VT_MSGTYPE_ACK;
	} else
		ack = IXGBE_VT_MSGTYPE_NACK;

	ixgbe_vf_enable_transmit(adapter, vf);
	ixgbe_vf_enable_receive(adapter, vf);

	vf->flags |= IXGBE_VF_CTS;

	resp[0] = IXGBE_VF_RESET | ack | IXGBE_VT_MSGTYPE_CTS;
	bcopy(vf->ether_addr, &resp[1], ETHER_ADDR_LEN);
	resp[3] = hw->mac.mc_filter_type;
	hw->mbx.ops.write(hw, resp, IXGBE_VF_PERMADDR_MSG_LEN, vf->pool);
} /* ixgbe_vf_reset_msg */


static void
ixgbe_vf_set_mac(struct adapter *adapter, struct ixgbe_vf *vf, uint32_t *msg)
{
	uint8_t *mac;

	mac = (uint8_t*)&msg[1];

	/* Check that the VF has permission to change the MAC address. */
	if (!(vf->flags & IXGBE_VF_CAP_MAC) && ixgbe_vf_mac_changed(vf, mac)) {
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		return;
	}

	if (ixgbe_validate_mac_addr(mac) != 0) {
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		return;
	}

	bcopy(mac, vf->ether_addr, ETHER_ADDR_LEN);

	ixgbe_set_rar(&adapter->hw, vf->rar_index, vf->ether_addr, vf->pool,
	    TRUE);

	ixgbe_send_vf_ack(adapter, vf, msg[0]);
} /* ixgbe_vf_set_mac */


/*
 * VF multicast addresses are set by using the appropriate bit in
 * 1 of 128 32 bit addresses (4096 possible).
 */
static void
ixgbe_vf_set_mc_addr(struct adapter *adapter, struct ixgbe_vf *vf, u32 *msg)
{
	u16	*list = (u16*)&msg[1];
	int	entries;
	u32	vmolr, vec_bit, vec_reg, mta_reg;

	entries = (msg[0] & IXGBE_VT_MSGINFO_MASK) >> IXGBE_VT_MSGINFO_SHIFT;
	entries = min(entries, IXGBE_MAX_VF_MC);

	vmolr = IXGBE_READ_REG(&adapter->hw, IXGBE_VMOLR(vf->pool));

	vf->num_mc_hashes = entries;

	/* Set the appropriate MTA bit */
	for (int i = 0; i < entries; i++) {
		vf->mc_hash[i] = list[i];
		vec_reg = (vf->mc_hash[i] >> 5) & 0x7F;
		vec_bit = vf->mc_hash[i] & 0x1F;
		mta_reg = IXGBE_READ_REG(&adapter->hw, IXGBE_MTA(vec_reg));
		mta_reg |= (1 << vec_bit);
		IXGBE_WRITE_REG(&adapter->hw, IXGBE_MTA(vec_reg), mta_reg);
	}

	vmolr |= IXGBE_VMOLR_ROMPE;
	IXGBE_WRITE_REG(&adapter->hw, IXGBE_VMOLR(vf->pool), vmolr);
	ixgbe_send_vf_ack(adapter, vf, msg[0]);
} /* ixgbe_vf_set_mc_addr */


static void
ixgbe_vf_set_vlan(struct adapter *adapter, struct ixgbe_vf *vf, uint32_t *msg)
{
	struct ixgbe_hw *hw;
	int enable;
	uint16_t tag;

	hw = &adapter->hw;
	enable = IXGBE_VT_MSGINFO(msg[0]);
	tag = msg[1] & IXGBE_VLVF_VLANID_MASK;

	if (!(vf->flags & IXGBE_VF_CAP_VLAN)) {
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		return;
	}

	/* It is illegal to enable vlan tag 0. */
	if (tag == 0 && enable != 0) {
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		return;
	}

	ixgbe_set_vfta(hw, tag, vf->pool, enable, false);
	ixgbe_send_vf_ack(adapter, vf, msg[0]);
} /* ixgbe_vf_set_vlan */


static void
ixgbe_vf_set_lpe(struct adapter *adapter, struct ixgbe_vf *vf, uint32_t *msg)
{
	struct ixgbe_hw *hw;
	uint32_t vf_max_size, pf_max_size, mhadd;

	hw = &adapter->hw;
	vf_max_size = msg[1];

	if (vf_max_size < ETHER_CRC_LEN) {
		/* We intentionally ACK invalid LPE requests. */
		ixgbe_send_vf_ack(adapter, vf, msg[0]);
		return;
	}

	vf_max_size -= ETHER_CRC_LEN;

	if (vf_max_size > IXGBE_MAX_FRAME_SIZE) {
		/* We intentionally ACK invalid LPE requests. */
		ixgbe_send_vf_ack(adapter, vf, msg[0]);
		return;
	}

	vf->maximum_frame_size = vf_max_size;
	ixgbe_update_max_frame(adapter, vf->maximum_frame_size);

	/*
	 * We might have to disable reception to this VF if the frame size is
	 * not compatible with the config on the PF.
	 */
	ixgbe_vf_enable_receive(adapter, vf);

	mhadd = IXGBE_READ_REG(hw, IXGBE_MHADD);
	pf_max_size = (mhadd & IXGBE_MHADD_MFS_MASK) >> IXGBE_MHADD_MFS_SHIFT;

	if (pf_max_size < adapter->max_frame_size) {
		mhadd &= ~IXGBE_MHADD_MFS_MASK;
		mhadd |= adapter->max_frame_size << IXGBE_MHADD_MFS_SHIFT;
		IXGBE_WRITE_REG(hw, IXGBE_MHADD, mhadd);
	}

	ixgbe_send_vf_ack(adapter, vf, msg[0]);
} /* ixgbe_vf_set_lpe */


static void
ixgbe_vf_set_macvlan(struct adapter *adapter, struct ixgbe_vf *vf,
                     uint32_t *msg)
{
	//XXX implement this
	ixgbe_send_vf_nack(adapter, vf, msg[0]);
} /* ixgbe_vf_set_macvlan */


static void
ixgbe_vf_api_negotiate(struct adapter *adapter, struct ixgbe_vf *vf,
    uint32_t *msg)
{

	switch (msg[1]) {
	case IXGBE_API_VER_1_0:
	case IXGBE_API_VER_1_1:
		vf->api_ver = msg[1];
		ixgbe_send_vf_ack(adapter, vf, msg[0]);
		break;
	default:
		vf->api_ver = IXGBE_API_VER_UNKNOWN;
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		break;
	}
} /* ixgbe_vf_api_negotiate */


static void
ixgbe_vf_get_queues(struct adapter *adapter, struct ixgbe_vf *vf, uint32_t *msg)
{
	struct ixgbe_hw *hw;
	uint32_t resp[IXGBE_VF_GET_QUEUES_RESP_LEN];
	int num_queues;

	hw = &adapter->hw;

	/* GET_QUEUES is not supported on pre-1.1 APIs. */
	switch (msg[0]) {
	case IXGBE_API_VER_1_0:
	case IXGBE_API_VER_UNKNOWN:
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		return;
	}

	resp[0] = IXGBE_VF_GET_QUEUES | IXGBE_VT_MSGTYPE_ACK |
	    IXGBE_VT_MSGTYPE_CTS;

	num_queues = ixgbe_vf_queues(adapter->iov_mode);
	resp[IXGBE_VF_TX_QUEUES] = num_queues;
	resp[IXGBE_VF_RX_QUEUES] = num_queues;
	resp[IXGBE_VF_TRANS_VLAN] = (vf->default_vlan != 0);
	resp[IXGBE_VF_DEF_QUEUE] = 0;

	hw->mbx.ops.write(hw, resp, IXGBE_VF_GET_QUEUES_RESP_LEN, vf->pool);
} /* ixgbe_vf_get_queues */


static void
ixgbe_process_vf_msg(if_ctx_t ctx, struct ixgbe_vf *vf)
{
	struct adapter  *adapter = iflib_get_softc(ctx);
#ifdef KTR
	struct ifnet	*ifp = iflib_get_ifp(ctx);
#endif
	struct ixgbe_hw *hw;
	uint32_t msg[IXGBE_VFMAILBOX_SIZE];
	int error;

	hw = &adapter->hw;

	error = hw->mbx.ops.read(hw, msg, IXGBE_VFMAILBOX_SIZE, vf->pool);

	if (error != 0)
		return;

	CTR3(KTR_MALLOC, "%s: received msg %x from %d", ifp->if_xname,
	    msg[0], vf->pool);
	if (msg[0] == IXGBE_VF_RESET) {
		ixgbe_vf_reset_msg(adapter, vf, msg);
		return;
	}

	if (!(vf->flags & IXGBE_VF_CTS)) {
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
		return;
	}

	switch (msg[0] & IXGBE_VT_MSG_MASK) {
	case IXGBE_VF_SET_MAC_ADDR:
		ixgbe_vf_set_mac(adapter, vf, msg);
		break;
	case IXGBE_VF_SET_MULTICAST:
		ixgbe_vf_set_mc_addr(adapter, vf, msg);
		break;
	case IXGBE_VF_SET_VLAN:
		ixgbe_vf_set_vlan(adapter, vf, msg);
		break;
	case IXGBE_VF_SET_LPE:
		ixgbe_vf_set_lpe(adapter, vf, msg);
		break;
	case IXGBE_VF_SET_MACVLAN:
		ixgbe_vf_set_macvlan(adapter, vf, msg);
		break;
	case IXGBE_VF_API_NEGOTIATE:
		ixgbe_vf_api_negotiate(adapter, vf, msg);
		break;
	case IXGBE_VF_GET_QUEUES:
		ixgbe_vf_get_queues(adapter, vf, msg);
		break;
	default:
		ixgbe_send_vf_nack(adapter, vf, msg[0]);
	}
} /* ixgbe_process_vf_msg */


/* Tasklet for handling VF -> PF mailbox messages */
void
ixgbe_handle_mbx(void *context)
{
	if_ctx_t        ctx = context;
	struct adapter  *adapter = iflib_get_softc(ctx);
	struct ixgbe_hw *hw;
	struct ixgbe_vf *vf;
	int i;

	hw = &adapter->hw;

	for (i = 0; i < adapter->num_vfs; i++) {
		vf = &adapter->vfs[i];

		if (vf->flags & IXGBE_VF_ACTIVE) {
			if (hw->mbx.ops.check_for_rst(hw, vf->pool) == 0)
				ixgbe_process_vf_reset(adapter, vf);

			if (hw->mbx.ops.check_for_msg(hw, vf->pool) == 0)
				ixgbe_process_vf_msg(ctx, vf);

			if (hw->mbx.ops.check_for_ack(hw, vf->pool) == 0)
				ixgbe_process_vf_ack(adapter, vf);
		}
	}
} /* ixgbe_handle_mbx */

int
ixgbe_if_iov_init(if_ctx_t ctx, u16 num_vfs, const nvlist_t *config)
{
	struct adapter *adapter;
	int retval = 0;

	adapter = iflib_get_softc(ctx);
	adapter->iov_mode = IXGBE_NO_VM;

	if (num_vfs == 0) {
		/* Would we ever get num_vfs = 0? */
		retval = EINVAL;
		goto err_init_iov;
	}

	/*
	 * We've got to reserve a VM's worth of queues for the PF,
	 * thus we go into "64 VF mode" if 32+ VFs are requested.
	 * With 64 VFs, you can only have two queues per VF.
	 * With 32 VFs, you can have up to four queues per VF.
	 */
	if (num_vfs >= IXGBE_32_VM)
		adapter->iov_mode = IXGBE_64_VM;
	else
		adapter->iov_mode = IXGBE_32_VM;

	/* Again, reserving 1 VM's worth of queues for the PF */
	adapter->pool = adapter->iov_mode - 1;

	if ((num_vfs > adapter->pool) || (num_vfs >= IXGBE_64_VM)) {
		retval = ENOSPC;
		goto err_init_iov;
	}

	adapter->vfs = malloc(sizeof(*adapter->vfs) * num_vfs, M_IXGBE_SRIOV,
	    M_NOWAIT | M_ZERO);

	if (adapter->vfs == NULL) {
		retval = ENOMEM;
		goto err_init_iov;
	}

	adapter->num_vfs = num_vfs;
	ixgbe_if_init(adapter->ctx);
	adapter->feat_en |= IXGBE_FEATURE_SRIOV;

	return (retval);

err_init_iov:
	adapter->num_vfs = 0;
	adapter->pool = 0;
	adapter->iov_mode = IXGBE_NO_VM;

	return (retval);
} /* ixgbe_if_iov_init */

void
ixgbe_if_iov_uninit(if_ctx_t ctx)
{
	struct ixgbe_hw *hw;
	struct adapter *adapter;
	uint32_t pf_reg, vf_reg;

	adapter = iflib_get_softc(ctx);
	hw = &adapter->hw;

	/* Enable rx/tx for the PF and disable it for all VFs. */
	pf_reg = IXGBE_VF_INDEX(adapter->pool);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(pf_reg), IXGBE_VF_BIT(adapter->pool));
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(pf_reg), IXGBE_VF_BIT(adapter->pool));

	if (pf_reg == 0)
		vf_reg = 1;
	else
		vf_reg = 0;
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(vf_reg), 0);
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(vf_reg), 0);

	IXGBE_WRITE_REG(hw, IXGBE_VT_CTL, 0);

	free(adapter->vfs, M_IXGBE_SRIOV);
	adapter->vfs = NULL;
	adapter->num_vfs = 0;
	adapter->feat_en &= ~IXGBE_FEATURE_SRIOV;
} /* ixgbe_if_iov_uninit */

static void
ixgbe_init_vf(struct adapter *adapter, struct ixgbe_vf *vf)
{
	struct ixgbe_hw *hw;
	uint32_t vf_index, pfmbimr;

	hw = &adapter->hw;

	if (!(vf->flags & IXGBE_VF_ACTIVE))
		return;

	vf_index = IXGBE_VF_INDEX(vf->pool);
	pfmbimr = IXGBE_READ_REG(hw, IXGBE_PFMBIMR(vf_index));
	pfmbimr |= IXGBE_VF_BIT(vf->pool);
	IXGBE_WRITE_REG(hw, IXGBE_PFMBIMR(vf_index), pfmbimr);

	ixgbe_vf_set_default_vlan(adapter, vf, vf->vlan_tag);

	// XXX multicast addresses

	if (ixgbe_validate_mac_addr(vf->ether_addr) == 0) {
		ixgbe_set_rar(&adapter->hw, vf->rar_index,
		    vf->ether_addr, vf->pool, TRUE);
	}

	ixgbe_vf_enable_transmit(adapter, vf);
	ixgbe_vf_enable_receive(adapter, vf);

	ixgbe_send_vf_msg(adapter, vf, IXGBE_PF_CONTROL_MSG);
} /* ixgbe_init_vf */

void
ixgbe_initialize_iov(struct adapter *adapter)
{
	struct ixgbe_hw *hw = &adapter->hw;
	uint32_t mrqc, mtqc, vt_ctl, vf_reg, gcr_ext, gpie;
	int i;

	if (adapter->iov_mode == IXGBE_NO_VM)
		return;

	/* RMW appropriate registers based on IOV mode */
	/* Read... */
	mrqc    = IXGBE_READ_REG(hw, IXGBE_MRQC);
	gcr_ext = IXGBE_READ_REG(hw, IXGBE_GCR_EXT);
	gpie    = IXGBE_READ_REG(hw, IXGBE_GPIE);
	/* Modify... */
	mrqc    &= ~IXGBE_MRQC_MRQE_MASK;
	mtqc     =  IXGBE_MTQC_VT_ENA;      /* No initial MTQC read needed */
	gcr_ext |=  IXGBE_GCR_EXT_MSIX_EN;
	gcr_ext &= ~IXGBE_GCR_EXT_VT_MODE_MASK;
	gpie    &= ~IXGBE_GPIE_VTMODE_MASK;
	switch (adapter->iov_mode) {
	case IXGBE_64_VM:
		mrqc    |= IXGBE_MRQC_VMDQRSS64EN;
		mtqc    |= IXGBE_MTQC_64VF;
		gcr_ext |= IXGBE_GCR_EXT_VT_MODE_64;
		gpie    |= IXGBE_GPIE_VTMODE_64;
		break;
	case IXGBE_32_VM:
		mrqc    |= IXGBE_MRQC_VMDQRSS32EN;
		mtqc    |= IXGBE_MTQC_32VF;
		gcr_ext |= IXGBE_GCR_EXT_VT_MODE_32;
		gpie    |= IXGBE_GPIE_VTMODE_32;
		break;
	default:
		panic("Unexpected SR-IOV mode %d", adapter->iov_mode);
	}
	/* Write... */
	IXGBE_WRITE_REG(hw, IXGBE_MRQC, mrqc);
	IXGBE_WRITE_REG(hw, IXGBE_MTQC, mtqc);
	IXGBE_WRITE_REG(hw, IXGBE_GCR_EXT, gcr_ext);
	IXGBE_WRITE_REG(hw, IXGBE_GPIE, gpie);

	/* Enable rx/tx for the PF. */
	vf_reg = IXGBE_VF_INDEX(adapter->pool);
	IXGBE_WRITE_REG(hw, IXGBE_VFRE(vf_reg), IXGBE_VF_BIT(adapter->pool));
	IXGBE_WRITE_REG(hw, IXGBE_VFTE(vf_reg), IXGBE_VF_BIT(adapter->pool));

	/* Allow VM-to-VM communication. */
	IXGBE_WRITE_REG(hw, IXGBE_PFDTXGSWC, IXGBE_PFDTXGSWC_VT_LBEN);

	vt_ctl = IXGBE_VT_CTL_VT_ENABLE | IXGBE_VT_CTL_REPLEN;
	vt_ctl |= (adapter->pool << IXGBE_VT_CTL_POOL_SHIFT);
	IXGBE_WRITE_REG(hw, IXGBE_VT_CTL, vt_ctl);

	for (i = 0; i < adapter->num_vfs; i++)
		ixgbe_init_vf(adapter, &adapter->vfs[i]);
} /* ixgbe_initialize_iov */


/* Check the max frame setting of all active VF's */
void
ixgbe_recalculate_max_frame(struct adapter *adapter)
{
	struct ixgbe_vf *vf;

	for (int i = 0; i < adapter->num_vfs; i++) {
		vf = &adapter->vfs[i];
		if (vf->flags & IXGBE_VF_ACTIVE)
			ixgbe_update_max_frame(adapter, vf->maximum_frame_size);
	}
} /* ixgbe_recalculate_max_frame */

int
ixgbe_if_iov_vf_add(if_ctx_t ctx, u16 vfnum, const nvlist_t *config)
{
	struct adapter *adapter;
	struct ixgbe_vf *vf;
	const void *mac;

	adapter = iflib_get_softc(ctx);

	KASSERT(vfnum < adapter->num_vfs, ("VF index %d is out of range %d",
	    vfnum, adapter->num_vfs));

	vf = &adapter->vfs[vfnum];
	vf->pool= vfnum;

	/* RAR[0] is used by the PF so use vfnum + 1 for VF RAR. */
	vf->rar_index = vfnum + 1;
	vf->default_vlan = 0;
	vf->maximum_frame_size = ETHER_MAX_LEN;
	ixgbe_update_max_frame(adapter, vf->maximum_frame_size);

	if (nvlist_exists_binary(config, "mac-addr")) {
		mac = nvlist_get_binary(config, "mac-addr", NULL);
		bcopy(mac, vf->ether_addr, ETHER_ADDR_LEN);
		if (nvlist_get_bool(config, "allow-set-mac"))
			vf->flags |= IXGBE_VF_CAP_MAC;
	} else
		/*
		 * If the administrator has not specified a MAC address then
		 * we must allow the VF to choose one.
		 */
		vf->flags |= IXGBE_VF_CAP_MAC;

	vf->flags |= IXGBE_VF_ACTIVE;

	ixgbe_init_vf(adapter, vf);

	return (0);
} /* ixgbe_if_iov_vf_add */

#else

void
ixgbe_handle_mbx(void *context)
{
	UNREFERENCED_PARAMETER(context);
} /* ixgbe_handle_mbx */

#endif
