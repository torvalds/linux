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

/*
**	Virtual Channel support
**		These are support functions to communication
**		between the VF and PF drivers.
*/

#include "ixl.h"
#include "iavf.h"

/* busy wait delay in msec */
#define IAVF_BUSY_WAIT_DELAY 10
#define IAVF_BUSY_WAIT_COUNT 50

/*
** iavf_send_pf_msg
**
** Send message to PF and print status if failure.
*/
static int
iavf_send_pf_msg(struct iavf_sc *sc,
	enum virtchnl_ops op, u8 *msg, u16 len)
{
	struct i40e_hw *hw = &sc->hw;
	device_t dev = sc->dev;
	i40e_status status;
	int val_err;

	/* Validating message before sending it to the PF */
	val_err = virtchnl_vc_validate_vf_msg(&sc->version, op, msg, len);
	if (val_err)
		device_printf(dev, "Error validating msg to PF for op %d,"
		    " msglen %d: error %d\n", op, len, val_err);

	if (!i40e_check_asq_alive(hw)) {
		if (op != VIRTCHNL_OP_GET_STATS)
			device_printf(dev, "Unable to send opcode %s to PF, "
			    "ASQ is not alive\n", ixl_vc_opcode_str(op));
		return (0);
	}

	if (op != VIRTCHNL_OP_GET_STATS)
		iavf_dbg_vc(sc,
		    "Sending msg (op=%s[%d]) to PF\n",
		    ixl_vc_opcode_str(op), op);

	status = i40e_aq_send_msg_to_pf(hw, op, I40E_SUCCESS, msg, len, NULL);
	if (status && op != VIRTCHNL_OP_GET_STATS)
		device_printf(dev, "Unable to send opcode %s to PF, "
		    "status %s, aq error %s\n",
		    ixl_vc_opcode_str(op),
		    i40e_stat_str(hw, status),
		    i40e_aq_str(hw, hw->aq.asq_last_status));

	return (status);
}

/*
** iavf_send_api_ver
**
** Send API version admin queue message to the PF. The reply is not checked
** in this function. Returns 0 if the message was successfully
** sent, or one of the I40E_ADMIN_QUEUE_ERROR_ statuses if not.
*/
int
iavf_send_api_ver(struct iavf_sc *sc)
{
	struct virtchnl_version_info vvi;

	vvi.major = VIRTCHNL_VERSION_MAJOR;
	vvi.minor = VIRTCHNL_VERSION_MINOR;

	return iavf_send_pf_msg(sc, VIRTCHNL_OP_VERSION,
	    (u8 *)&vvi, sizeof(vvi));
}

/*
** iavf_verify_api_ver
**
** Compare API versions with the PF. Must be called after admin queue is
** initialized. Returns 0 if API versions match, EIO if
** they do not, or I40E_ERR_ADMIN_QUEUE_NO_WORK if the admin queue is empty.
*/
int
iavf_verify_api_ver(struct iavf_sc *sc)
{
	struct virtchnl_version_info *pf_vvi;
	struct i40e_hw *hw = &sc->hw;
	struct i40e_arq_event_info event;
	device_t dev = sc->dev;
	i40e_status err;
	int retries = 0;

	event.buf_len = IXL_AQ_BUF_SZ;
	event.msg_buf = malloc(event.buf_len, M_IAVF, M_WAITOK);

	for (;;) {
		if (++retries > IAVF_AQ_MAX_ERR)
			goto out_alloc;

		/* Initial delay here is necessary */
		i40e_msec_pause(100);
		err = i40e_clean_arq_element(hw, &event, NULL);
		if (err == I40E_ERR_ADMIN_QUEUE_NO_WORK)
			continue;
		else if (err) {
			err = EIO;
			goto out_alloc;
		}

		if ((enum virtchnl_ops)le32toh(event.desc.cookie_high) !=
		    VIRTCHNL_OP_VERSION) {
			DDPRINTF(dev, "Received unexpected op response: %d\n",
			    le32toh(event.desc.cookie_high));
		    	/* Don't stop looking for expected response */
			continue;
		}

		err = (i40e_status)le32toh(event.desc.cookie_low);
		if (err) {
			err = EIO;
			goto out_alloc;
		} else
			break;
	}

	pf_vvi = (struct virtchnl_version_info *)event.msg_buf;
	if ((pf_vvi->major > VIRTCHNL_VERSION_MAJOR) ||
	    ((pf_vvi->major == VIRTCHNL_VERSION_MAJOR) &&
	    (pf_vvi->minor > VIRTCHNL_VERSION_MINOR))) {
		device_printf(dev, "Critical PF/VF API version mismatch!\n");
		err = EIO;
	} else {
		sc->version.major = pf_vvi->major;
		sc->version.minor = pf_vvi->minor;
	}
	
	/* Log PF/VF api versions */
	device_printf(dev, "PF API %d.%d / VF API %d.%d\n",
	    pf_vvi->major, pf_vvi->minor,
	    VIRTCHNL_VERSION_MAJOR, VIRTCHNL_VERSION_MINOR);

out_alloc:
	free(event.msg_buf, M_IAVF);
	return (err);
}

/*
** iavf_send_vf_config_msg
**
** Send VF configuration request admin queue message to the PF. The reply
** is not checked in this function. Returns 0 if the message was
** successfully sent, or one of the I40E_ADMIN_QUEUE_ERROR_ statuses if not.
*/
int
iavf_send_vf_config_msg(struct iavf_sc *sc)
{
	u32	caps;

	caps = VIRTCHNL_VF_OFFLOAD_L2 |
	    VIRTCHNL_VF_OFFLOAD_RSS_PF |
	    VIRTCHNL_VF_OFFLOAD_VLAN;

	iavf_dbg_info(sc, "Sending offload flags: 0x%b\n",
	    caps, IAVF_PRINTF_VF_OFFLOAD_FLAGS);

	if (sc->version.minor == VIRTCHNL_VERSION_MINOR_NO_VF_CAPS)
		return iavf_send_pf_msg(sc, VIRTCHNL_OP_GET_VF_RESOURCES,
				  NULL, 0);
	else
		return iavf_send_pf_msg(sc, VIRTCHNL_OP_GET_VF_RESOURCES,
				  (u8 *)&caps, sizeof(caps));
}

/*
** iavf_get_vf_config
**
** Get VF configuration from PF and populate hw structure. Must be called after
** admin queue is initialized. Busy waits until response is received from PF,
** with maximum timeout. Response from PF is returned in the buffer for further
** processing by the caller.
*/
int
iavf_get_vf_config(struct iavf_sc *sc)
{
	struct i40e_hw	*hw = &sc->hw;
	device_t	dev = sc->dev;
	struct i40e_arq_event_info event;
	u16 len;
	i40e_status err = 0;
	u32 retries = 0;

	/* Note this assumes a single VSI */
	len = sizeof(struct virtchnl_vf_resource) +
	    sizeof(struct virtchnl_vsi_resource);
	event.buf_len = len;
	event.msg_buf = malloc(event.buf_len, M_IAVF, M_WAITOK);

	for (;;) {
		err = i40e_clean_arq_element(hw, &event, NULL);
		if (err == I40E_ERR_ADMIN_QUEUE_NO_WORK) {
			if (++retries <= IAVF_AQ_MAX_ERR)
				i40e_msec_pause(10);
		} else if ((enum virtchnl_ops)le32toh(event.desc.cookie_high) !=
		    VIRTCHNL_OP_GET_VF_RESOURCES) {
			DDPRINTF(dev, "Received a response from PF,"
			    " opcode %d, error %d",
			    le32toh(event.desc.cookie_high),
			    le32toh(event.desc.cookie_low));
			retries++;
			continue;
		} else {
			err = (i40e_status)le32toh(event.desc.cookie_low);
			if (err) {
				device_printf(dev, "%s: Error returned from PF,"
				    " opcode %d, error %d\n", __func__,
				    le32toh(event.desc.cookie_high),
				    le32toh(event.desc.cookie_low));
				err = EIO;
				goto out_alloc;
			}
			/* We retrieved the config message, with no errors */
			break;
		}

		if (retries > IAVF_AQ_MAX_ERR) {
			INIT_DBG_DEV(dev, "Did not receive response after %d tries.",
			    retries);
			err = ETIMEDOUT;
			goto out_alloc;
		}
	}

	memcpy(sc->vf_res, event.msg_buf, min(event.msg_len, len));
	i40e_vf_parse_hw_config(hw, sc->vf_res);

out_alloc:
	free(event.msg_buf, M_IAVF);
	return err;
}

/*
** iavf_configure_queues
**
** Request that the PF set up our queues.
*/
int
iavf_configure_queues(struct iavf_sc *sc)
{
	device_t		dev = sc->dev;
	struct ixl_vsi		*vsi = &sc->vsi;
	if_softc_ctx_t		scctx = iflib_get_softc_ctx(vsi->ctx);
	struct ixl_tx_queue	*tx_que = vsi->tx_queues;
	struct ixl_rx_queue	*rx_que = vsi->rx_queues;
	struct tx_ring		*txr;
	struct rx_ring		*rxr;
	int			len, pairs;

	struct virtchnl_vsi_queue_config_info *vqci;
	struct virtchnl_queue_pair_info *vqpi;

	/* XXX: Linux PF driver wants matching ids in each tx/rx struct, so both TX/RX
	 * queues of a pair need to be configured */
	pairs = max(vsi->num_tx_queues, vsi->num_rx_queues);
	len = sizeof(struct virtchnl_vsi_queue_config_info) +
		       (sizeof(struct virtchnl_queue_pair_info) * pairs);
	vqci = malloc(len, M_IAVF, M_NOWAIT | M_ZERO);
	if (!vqci) {
		device_printf(dev, "%s: unable to allocate memory\n", __func__);
		return (ENOMEM);
	}
	vqci->vsi_id = sc->vsi_res->vsi_id;
	vqci->num_queue_pairs = pairs;
	vqpi = vqci->qpair;
	/* Size check is not needed here - HW max is 16 queue pairs, and we
	 * can fit info for 31 of them into the AQ buffer before it overflows.
	 */
	// TODO: the above is wrong now; X722 VFs can have 256 queues
	for (int i = 0; i < pairs; i++, tx_que++, rx_que++, vqpi++) {
		txr = &tx_que->txr;
		rxr = &rx_que->rxr;

		vqpi->txq.vsi_id = vqci->vsi_id;
		vqpi->txq.queue_id = i;
		vqpi->txq.ring_len = scctx->isc_ntxd[0];
		vqpi->txq.dma_ring_addr = txr->tx_paddr;
		/* Enable Head writeback */
		if (!vsi->enable_head_writeback) {
			vqpi->txq.headwb_enabled = 0;
			vqpi->txq.dma_headwb_addr = 0;
		} else {
			vqpi->txq.headwb_enabled = 1;
			vqpi->txq.dma_headwb_addr = txr->tx_paddr +
			    sizeof(struct i40e_tx_desc) * scctx->isc_ntxd[0];
		}

		vqpi->rxq.vsi_id = vqci->vsi_id;
		vqpi->rxq.queue_id = i;
		vqpi->rxq.ring_len = scctx->isc_nrxd[0];
		vqpi->rxq.dma_ring_addr = rxr->rx_paddr;
		vqpi->rxq.max_pkt_size = scctx->isc_max_frame_size;
		vqpi->rxq.databuffer_size = rxr->mbuf_sz;
		vqpi->rxq.splithdr_enabled = 0;
	}

	iavf_send_pf_msg(sc, VIRTCHNL_OP_CONFIG_VSI_QUEUES,
			   (u8 *)vqci, len);
	free(vqci, M_IAVF);

	return (0);
}

/*
** iavf_enable_queues
**
** Request that the PF enable all of our queues.
*/
int
iavf_enable_queues(struct iavf_sc *sc)
{
	struct virtchnl_queue_select vqs;

	vqs.vsi_id = sc->vsi_res->vsi_id;
	/* XXX: In Linux PF, as long as neither of these is 0,
	 * every queue in VF VSI is enabled. */
	vqs.tx_queues = (1 << sc->vsi.num_tx_queues) - 1;
	vqs.rx_queues = vqs.tx_queues;
	iavf_send_pf_msg(sc, VIRTCHNL_OP_ENABLE_QUEUES,
			   (u8 *)&vqs, sizeof(vqs));
	return (0);
}

/*
** iavf_disable_queues
**
** Request that the PF disable all of our queues.
*/
int
iavf_disable_queues(struct iavf_sc *sc)
{
	struct virtchnl_queue_select vqs;

	vqs.vsi_id = sc->vsi_res->vsi_id;
	/* XXX: In Linux PF, as long as neither of these is 0,
	 * every queue in VF VSI is disabled. */
	vqs.tx_queues = (1 << sc->vsi.num_tx_queues) - 1;
	vqs.rx_queues = vqs.tx_queues;
	iavf_send_pf_msg(sc, VIRTCHNL_OP_DISABLE_QUEUES,
			   (u8 *)&vqs, sizeof(vqs));
	return (0);
}

/*
** iavf_map_queues
**
** Request that the PF map queues to interrupt vectors. Misc causes, including
** admin queue, are always mapped to vector 0.
*/
int
iavf_map_queues(struct iavf_sc *sc)
{
	struct virtchnl_irq_map_info *vm;
	int 			i, q, len;
	struct ixl_vsi		*vsi = &sc->vsi;
	struct ixl_rx_queue	*rx_que = vsi->rx_queues;
	if_softc_ctx_t		scctx = vsi->shared;
	device_t		dev = sc->dev;

	// XXX: What happens if we only get 1 MSI-X vector?
	MPASS(scctx->isc_vectors > 1);

	/* How many queue vectors, adminq uses one */
	// XXX: How do we know how many interrupt vectors we have?
	q = scctx->isc_vectors - 1;

	len = sizeof(struct virtchnl_irq_map_info) +
	      (scctx->isc_vectors * sizeof(struct virtchnl_vector_map));
	vm = malloc(len, M_IAVF, M_NOWAIT);
	if (!vm) {
		device_printf(dev, "%s: unable to allocate memory\n", __func__);
		return (ENOMEM);
	}

	vm->num_vectors = scctx->isc_vectors;
	/* Queue vectors first */
	for (i = 0; i < q; i++, rx_que++) {
		vm->vecmap[i].vsi_id = sc->vsi_res->vsi_id;
		vm->vecmap[i].vector_id = i + 1; /* first is adminq */
		// TODO: Re-examine this
		vm->vecmap[i].txq_map = (1 << rx_que->rxr.me);
		vm->vecmap[i].rxq_map = (1 << rx_que->rxr.me);
		vm->vecmap[i].rxitr_idx = 0;
		vm->vecmap[i].txitr_idx = 1;
	}

	/* Misc vector last - this is only for AdminQ messages */
	vm->vecmap[i].vsi_id = sc->vsi_res->vsi_id;
	vm->vecmap[i].vector_id = 0;
	vm->vecmap[i].txq_map = 0;
	vm->vecmap[i].rxq_map = 0;
	vm->vecmap[i].rxitr_idx = 0;
	vm->vecmap[i].txitr_idx = 0;

	iavf_send_pf_msg(sc, VIRTCHNL_OP_CONFIG_IRQ_MAP,
	    (u8 *)vm, len);
	free(vm, M_IAVF);

	return (0);
}

/*
** Scan the Filter List looking for vlans that need
** to be added, then create the data to hand to the AQ
** for handling.
*/
int
iavf_add_vlans(struct iavf_sc *sc)
{
	struct virtchnl_vlan_filter_list *v;
	struct iavf_vlan_filter *f, *ftmp;
	device_t	dev = sc->dev;
	int		len, i = 0, cnt = 0;

	/* Get count of VLAN filters to add */
	SLIST_FOREACH(f, sc->vlan_filters, next) {
		if (f->flags & IXL_FILTER_ADD)
			cnt++;
	}

	if (!cnt) /* no work... */
		return (ENOENT);

	len = sizeof(struct virtchnl_vlan_filter_list) +
	      (cnt * sizeof(u16));

	if (len > IXL_AQ_BUF_SZ) {
		device_printf(dev, "%s: Exceeded Max AQ Buf size\n",
			__func__);
		return (EFBIG);
	}

	v = malloc(len, M_IAVF, M_NOWAIT);
	if (!v) {
		device_printf(dev, "%s: unable to allocate memory\n",
			__func__);
		return (ENOMEM);
	}

	v->vsi_id = sc->vsi_res->vsi_id;
	v->num_elements = cnt;

	/* Scan the filter array */
	SLIST_FOREACH_SAFE(f, sc->vlan_filters, next, ftmp) {
                if (f->flags & IXL_FILTER_ADD) {
                        bcopy(&f->vlan, &v->vlan_id[i], sizeof(u16));
			f->flags = IXL_FILTER_USED;
                        i++;
                }
                if (i == cnt)
                        break;
	}

	iavf_send_pf_msg(sc, VIRTCHNL_OP_ADD_VLAN, (u8 *)v, len);
	free(v, M_IAVF);
	/* add stats? */
	return (0);
}

/*
** Scan the Filter Table looking for vlans that need
** to be removed, then create the data to hand to the AQ
** for handling.
*/
int
iavf_del_vlans(struct iavf_sc *sc)
{
	struct virtchnl_vlan_filter_list *v;
	struct iavf_vlan_filter *f, *ftmp;
	device_t dev = sc->dev;
	int len, i = 0, cnt = 0;

	/* Get count of VLAN filters to delete */
	SLIST_FOREACH(f, sc->vlan_filters, next) {
		if (f->flags & IXL_FILTER_DEL)
			cnt++;
	}

	if (!cnt) /* no work... */
		return (ENOENT);

	len = sizeof(struct virtchnl_vlan_filter_list) +
	      (cnt * sizeof(u16));

	if (len > IXL_AQ_BUF_SZ) {
		device_printf(dev, "%s: Exceeded Max AQ Buf size\n",
			__func__);
		return (EFBIG);
	}

	v = malloc(len, M_IAVF, M_NOWAIT | M_ZERO);
	if (!v) {
		device_printf(dev, "%s: unable to allocate memory\n",
			__func__);
		return (ENOMEM);
	}

	v->vsi_id = sc->vsi_res->vsi_id;
	v->num_elements = cnt;

	/* Scan the filter array */
	SLIST_FOREACH_SAFE(f, sc->vlan_filters, next, ftmp) {
                if (f->flags & IXL_FILTER_DEL) {
                        bcopy(&f->vlan, &v->vlan_id[i], sizeof(u16));
                        i++;
                        SLIST_REMOVE(sc->vlan_filters, f, iavf_vlan_filter, next);
                        free(f, M_IAVF);
                }
                if (i == cnt)
                        break;
	}

	iavf_send_pf_msg(sc, VIRTCHNL_OP_DEL_VLAN, (u8 *)v, len);
	free(v, M_IAVF);
	/* add stats? */
	return (0);
}


/*
** This routine takes additions to the vsi filter
** table and creates an Admin Queue call to create
** the filters in the hardware.
*/
int
iavf_add_ether_filters(struct iavf_sc *sc)
{
	struct virtchnl_ether_addr_list *a;
	struct iavf_mac_filter	*f;
	device_t dev = sc->dev;
	int len, j = 0, cnt = 0;
	enum i40e_status_code status;

	/* Get count of MAC addresses to add */
	SLIST_FOREACH(f, sc->mac_filters, next) {
		if (f->flags & IXL_FILTER_ADD)
			cnt++;
	}
	if (cnt == 0) { /* Should not happen... */
		iavf_dbg_vc(sc, "%s: cnt == 0, exiting...\n", __func__);
		return (ENOENT);
	}

	len = sizeof(struct virtchnl_ether_addr_list) +
	    (cnt * sizeof(struct virtchnl_ether_addr));

	a = malloc(len, M_IAVF, M_NOWAIT | M_ZERO);
	if (a == NULL) {
		device_printf(dev, "%s: Failed to get memory for "
		    "virtchnl_ether_addr_list\n", __func__);
		return (ENOMEM);
	}
	a->vsi_id = sc->vsi.id;
	a->num_elements = cnt;

	/* Scan the filter array */
	SLIST_FOREACH(f, sc->mac_filters, next) {
		if (f->flags & IXL_FILTER_ADD) {
			bcopy(f->macaddr, a->list[j].addr, ETHER_ADDR_LEN);
			f->flags &= ~IXL_FILTER_ADD;
			j++;

			iavf_dbg_vc(sc, "ADD: " MAC_FORMAT "\n",
			    MAC_FORMAT_ARGS(f->macaddr));
		}
		if (j == cnt)
			break;
	}
	DDPRINTF(dev, "len %d, j %d, cnt %d",
	    len, j, cnt);

	status = iavf_send_pf_msg(sc,
	    VIRTCHNL_OP_ADD_ETH_ADDR, (u8 *)a, len);
	/* add stats? */
	free(a, M_IAVF);
	return (status);
}

/*
** This routine takes filters flagged for deletion in the
** sc MAC filter list and creates an Admin Queue call
** to delete those filters in the hardware.
*/
int
iavf_del_ether_filters(struct iavf_sc *sc)
{
	struct virtchnl_ether_addr_list *d;
	struct iavf_mac_filter *f, *f_temp;
	device_t dev = sc->dev;
	int len, j = 0, cnt = 0;

	/* Get count of MAC addresses to delete */
	SLIST_FOREACH(f, sc->mac_filters, next) {
		if (f->flags & IXL_FILTER_DEL)
			cnt++;
	}
	if (cnt == 0) {
		iavf_dbg_vc(sc, "%s: cnt == 0, exiting...\n", __func__);
		return (ENOENT);
	}

	len = sizeof(struct virtchnl_ether_addr_list) +
	    (cnt * sizeof(struct virtchnl_ether_addr));

	d = malloc(len, M_IAVF, M_NOWAIT | M_ZERO);
	if (d == NULL) {
		device_printf(dev, "%s: Failed to get memory for "
		    "virtchnl_ether_addr_list\n", __func__);
		return (ENOMEM);
	}
	d->vsi_id = sc->vsi.id;
	d->num_elements = cnt;

	/* Scan the filter array */
	SLIST_FOREACH_SAFE(f, sc->mac_filters, next, f_temp) {
		if (f->flags & IXL_FILTER_DEL) {
			bcopy(f->macaddr, d->list[j].addr, ETHER_ADDR_LEN);
			iavf_dbg_vc(sc, "DEL: " MAC_FORMAT "\n",
			    MAC_FORMAT_ARGS(f->macaddr));
			j++;
			SLIST_REMOVE(sc->mac_filters, f, iavf_mac_filter, next);
			free(f, M_IAVF);
		}
		if (j == cnt)
			break;
	}
	iavf_send_pf_msg(sc,
	    VIRTCHNL_OP_DEL_ETH_ADDR, (u8 *)d, len);
	/* add stats? */
	free(d, M_IAVF);
	return (0);
}

/*
** iavf_request_reset
** Request that the PF reset this VF. No response is expected.
*/
int
iavf_request_reset(struct iavf_sc *sc)
{
	/*
	** Set the reset status to "in progress" before
	** the request, this avoids any possibility of
	** a mistaken early detection of completion.
	*/
	wr32(&sc->hw, I40E_VFGEN_RSTAT, VIRTCHNL_VFR_INPROGRESS);
	iavf_send_pf_msg(sc, VIRTCHNL_OP_RESET_VF, NULL, 0);
	return (0);
}

/*
** iavf_request_stats
** Request the statistics for this VF's VSI from PF.
*/
int
iavf_request_stats(struct iavf_sc *sc)
{
	struct virtchnl_queue_select vqs;
	int error = 0;

	vqs.vsi_id = sc->vsi_res->vsi_id;
	/* Low priority, we don't need to error check */
	error = iavf_send_pf_msg(sc, VIRTCHNL_OP_GET_STATS,
	    (u8 *)&vqs, sizeof(vqs));
	if (error)
		device_printf(sc->dev, "Error sending stats request to PF: %d\n", error);
	
	return (0);
}

/*
** Updates driver's stats counters with VSI stats returned from PF.
*/
void
iavf_update_stats_counters(struct iavf_sc *sc, struct i40e_eth_stats *es)
{
	struct ixl_vsi *vsi = &sc->vsi;
	uint64_t tx_discards;

	tx_discards = es->tx_discards;

	/* Update ifnet stats */
	IXL_SET_IPACKETS(vsi, es->rx_unicast +
	                   es->rx_multicast +
			   es->rx_broadcast);
	IXL_SET_OPACKETS(vsi, es->tx_unicast +
	                   es->tx_multicast +
			   es->tx_broadcast);
	IXL_SET_IBYTES(vsi, es->rx_bytes);
	IXL_SET_OBYTES(vsi, es->tx_bytes);
	IXL_SET_IMCASTS(vsi, es->rx_multicast);
	IXL_SET_OMCASTS(vsi, es->tx_multicast);

	IXL_SET_OERRORS(vsi, es->tx_errors);
	IXL_SET_IQDROPS(vsi, es->rx_discards);
	IXL_SET_OQDROPS(vsi, tx_discards);
	IXL_SET_NOPROTO(vsi, es->rx_unknown_protocol);
	IXL_SET_COLLISIONS(vsi, 0);

	vsi->eth_stats = *es;
}

int
iavf_config_rss_key(struct iavf_sc *sc)
{
	struct virtchnl_rss_key *rss_key_msg;
	int msg_len, key_length;
	u8		rss_seed[IXL_RSS_KEY_SIZE];

#ifdef RSS
	/* Fetch the configured RSS key */
	rss_getkey((uint8_t *) &rss_seed);
#else
	ixl_get_default_rss_key((u32 *)rss_seed);
#endif

	/* Send the fetched key */
	key_length = IXL_RSS_KEY_SIZE;
	msg_len = sizeof(struct virtchnl_rss_key) + (sizeof(u8) * key_length) - 1;
	rss_key_msg = malloc(msg_len, M_IAVF, M_NOWAIT | M_ZERO);
	if (rss_key_msg == NULL) {
		device_printf(sc->dev, "Unable to allocate msg memory for RSS key msg.\n");
		return (ENOMEM);
	}

	rss_key_msg->vsi_id = sc->vsi_res->vsi_id;
	rss_key_msg->key_len = key_length;
	bcopy(rss_seed, &rss_key_msg->key[0], key_length);

	iavf_dbg_vc(sc, "config_rss: vsi_id %d, key_len %d\n",
	    rss_key_msg->vsi_id, rss_key_msg->key_len);
	
	iavf_send_pf_msg(sc, VIRTCHNL_OP_CONFIG_RSS_KEY,
			  (u8 *)rss_key_msg, msg_len);

	free(rss_key_msg, M_IAVF);
	return (0);
}

int
iavf_set_rss_hena(struct iavf_sc *sc)
{
	struct virtchnl_rss_hena hena;
	struct i40e_hw *hw = &sc->hw;

	if (hw->mac.type == I40E_MAC_X722_VF)
		hena.hena = IXL_DEFAULT_RSS_HENA_X722;
	else
		hena.hena = IXL_DEFAULT_RSS_HENA_XL710;

	iavf_send_pf_msg(sc, VIRTCHNL_OP_SET_RSS_HENA,
			  (u8 *)&hena, sizeof(hena));
	return (0);
}

int
iavf_config_rss_lut(struct iavf_sc *sc)
{
	struct virtchnl_rss_lut *rss_lut_msg;
	int msg_len;
	u16 lut_length;
	u32 lut;
	int i, que_id;

	lut_length = IXL_RSS_VSI_LUT_SIZE;
	msg_len = sizeof(struct virtchnl_rss_lut) + (lut_length * sizeof(u8)) - 1;
	rss_lut_msg = malloc(msg_len, M_IAVF, M_NOWAIT | M_ZERO);
	if (rss_lut_msg == NULL) {
		device_printf(sc->dev, "Unable to allocate msg memory for RSS lut msg.\n");
		return (ENOMEM);
	}

	rss_lut_msg->vsi_id = sc->vsi_res->vsi_id;
	/* Each LUT entry is a max of 1 byte, so this is easy */
	rss_lut_msg->lut_entries = lut_length;

	/* Populate the LUT with max no. of queues in round robin fashion */
	for (i = 0; i < lut_length; i++) {
#ifdef RSS
		/*
		 * Fetch the RSS bucket id for the given indirection entry.
		 * Cap it at the number of configured buckets (which is
		 * num_rx_queues.)
		 */
		que_id = rss_get_indirection_to_bucket(i);
		que_id = que_id % sc->vsi.num_rx_queues;
#else
		que_id = i % sc->vsi.num_rx_queues;
#endif
		lut = que_id & IXL_RSS_VSI_LUT_ENTRY_MASK;
		rss_lut_msg->lut[i] = lut;
	}

	iavf_send_pf_msg(sc, VIRTCHNL_OP_CONFIG_RSS_LUT,
			  (u8 *)rss_lut_msg, msg_len);

	free(rss_lut_msg, M_IAVF);
	return (0);
}

int
iavf_config_promisc_mode(struct iavf_sc *sc)
{
	struct virtchnl_promisc_info pinfo;

	pinfo.vsi_id = sc->vsi_res->vsi_id;
	pinfo.flags = sc->promisc_flags;

	iavf_send_pf_msg(sc, VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE,
	    (u8 *)&pinfo, sizeof(pinfo));
	return (0);
}

/*
** iavf_vc_completion
**
** Asynchronous completion function for admin queue messages. Rather than busy
** wait, we fire off our requests and assume that no errors will be returned.
** This function handles the reply messages.
*/
void
iavf_vc_completion(struct iavf_sc *sc,
    enum virtchnl_ops v_opcode,
    enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
{
	device_t	dev = sc->dev;

	if (v_opcode != VIRTCHNL_OP_GET_STATS)
		iavf_dbg_vc(sc, "%s: opcode %s\n", __func__,
		    ixl_vc_opcode_str(v_opcode));

	if (v_opcode == VIRTCHNL_OP_EVENT) {
		struct virtchnl_pf_event *vpe =
			(struct virtchnl_pf_event *)msg;

		switch (vpe->event) {
		case VIRTCHNL_EVENT_LINK_CHANGE:
			iavf_dbg_vc(sc, "Link change: status %d, speed %s\n",
			    vpe->event_data.link_event.link_status,
			    iavf_vc_speed_to_string(vpe->event_data.link_event.link_speed));
			sc->link_up =
				vpe->event_data.link_event.link_status;
			sc->link_speed =
				vpe->event_data.link_event.link_speed;
			iavf_update_link_status(sc);
			break;
		case VIRTCHNL_EVENT_RESET_IMPENDING:
			device_printf(dev, "PF initiated reset!\n");
			sc->init_state = IAVF_RESET_PENDING;
			iavf_if_init(sc->vsi.ctx);
			break;
		default:
			iavf_dbg_vc(sc, "Unknown event %d from AQ\n",
				vpe->event);
			break;
		}

		return;
	}

	/* Catch-all error response */
	if (v_retval) {
		device_printf(dev,
		    "%s: AQ returned error %s to our request %s!\n",
		    __func__, i40e_vc_stat_str(&sc->hw, v_retval), ixl_vc_opcode_str(v_opcode));
	}

	switch (v_opcode) {
	case VIRTCHNL_OP_GET_STATS:
		iavf_update_stats_counters(sc, (struct i40e_eth_stats *)msg);
		break;
	case VIRTCHNL_OP_ADD_ETH_ADDR:
		if (v_retval) {
			device_printf(dev, "WARNING: Error adding VF mac filter!\n");
			device_printf(dev, "WARNING: Device may not receive traffic!\n");
		}
		break;
	case VIRTCHNL_OP_DEL_ETH_ADDR:
		break;
	case VIRTCHNL_OP_CONFIG_PROMISCUOUS_MODE:
		break;
	case VIRTCHNL_OP_ADD_VLAN:
		break;
	case VIRTCHNL_OP_DEL_VLAN:
		break;
	case VIRTCHNL_OP_ENABLE_QUEUES:
		atomic_store_rel_32(&sc->queues_enabled, 1);
		wakeup_one(&sc->enable_queues_chan);
		break;
	case VIRTCHNL_OP_DISABLE_QUEUES:
		atomic_store_rel_32(&sc->queues_enabled, 0);
		wakeup_one(&sc->disable_queues_chan);
		break;
	case VIRTCHNL_OP_CONFIG_VSI_QUEUES:
		break;
	case VIRTCHNL_OP_CONFIG_IRQ_MAP:
		break;
	case VIRTCHNL_OP_CONFIG_RSS_KEY:
		break;
	case VIRTCHNL_OP_SET_RSS_HENA:
		break;
	case VIRTCHNL_OP_CONFIG_RSS_LUT:
		break;
	default:
		iavf_dbg_vc(sc,
		    "Received unexpected message %s from PF.\n",
		    ixl_vc_opcode_str(v_opcode));
		break;
	}
}

int
ixl_vc_send_cmd(struct iavf_sc *sc, uint32_t request)
{

	switch (request) {
	case IAVF_FLAG_AQ_MAP_VECTORS:
		return iavf_map_queues(sc);

	case IAVF_FLAG_AQ_ADD_MAC_FILTER:
		return iavf_add_ether_filters(sc);

	case IAVF_FLAG_AQ_ADD_VLAN_FILTER:
		return iavf_add_vlans(sc);

	case IAVF_FLAG_AQ_DEL_MAC_FILTER:
		return iavf_del_ether_filters(sc);

	case IAVF_FLAG_AQ_DEL_VLAN_FILTER:
		return iavf_del_vlans(sc);

	case IAVF_FLAG_AQ_CONFIGURE_QUEUES:
		return iavf_configure_queues(sc);

	case IAVF_FLAG_AQ_DISABLE_QUEUES:
		return iavf_disable_queues(sc);

	case IAVF_FLAG_AQ_ENABLE_QUEUES:
		return iavf_enable_queues(sc);

	case IAVF_FLAG_AQ_CONFIG_RSS_KEY:
		return iavf_config_rss_key(sc);

	case IAVF_FLAG_AQ_SET_RSS_HENA:
		return iavf_set_rss_hena(sc);

	case IAVF_FLAG_AQ_CONFIG_RSS_LUT:
		return iavf_config_rss_lut(sc);

	case IAVF_FLAG_AQ_CONFIGURE_PROMISC:
		return iavf_config_promisc_mode(sc);
	}

	return (0);
}

void *
ixl_vc_get_op_chan(struct iavf_sc *sc, uint32_t request)
{
	switch (request) {
	case IAVF_FLAG_AQ_ENABLE_QUEUES:
		return (&sc->enable_queues_chan);
	case IAVF_FLAG_AQ_DISABLE_QUEUES:
		return (&sc->disable_queues_chan);
	default:
		return (NULL);
	}
}
