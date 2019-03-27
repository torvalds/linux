/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */


#include "oce_if.h"

static int oce_POST(POCE_SOFTC sc);

/**
 * @brief		Function to post status
 * @param sc		software handle to the device
 */
static int
oce_POST(POCE_SOFTC sc)
{
	mpu_ep_semaphore_t post_status;
	int tmo = 60000;

	/* read semaphore CSR */
	post_status.dw0 = OCE_READ_CSR_MPU(sc, csr, MPU_EP_SEMAPHORE(sc));

	/* if host is ready then wait for fw ready else send POST */
	if (post_status.bits.stage <= POST_STAGE_AWAITING_HOST_RDY) {
		post_status.bits.stage = POST_STAGE_CHIP_RESET;
		OCE_WRITE_CSR_MPU(sc, csr, MPU_EP_SEMAPHORE(sc), post_status.dw0);
	}

	/* wait for FW ready */
	for (;;) {
		if (--tmo == 0)
			break;

		DELAY(1000);

		post_status.dw0 = OCE_READ_CSR_MPU(sc, csr, MPU_EP_SEMAPHORE(sc));
		if (post_status.bits.error) {
			device_printf(sc->dev,
				  "POST failed: %x\n", post_status.dw0);
			return ENXIO;
		}
		if (post_status.bits.stage == POST_STAGE_ARMFW_READY)
			return 0;
	}

	device_printf(sc->dev, "POST timed out: %x\n", post_status.dw0);

	return ENXIO;
}

/**
 * @brief		Function for hardware initialization
 * @param sc		software handle to the device
 */
int
oce_hw_init(POCE_SOFTC sc)
{
	int rc = 0;

	rc = oce_POST(sc);
	if (rc)
		return rc;
	
	/* create the bootstrap mailbox */
	rc = oce_dma_alloc(sc, sizeof(struct oce_bmbx), &sc->bsmbx, 0);
	if (rc) {
		device_printf(sc->dev, "Mailbox alloc failed\n");
		return rc;
	}

	rc = oce_reset_fun(sc);
	if (rc)
		goto error;
		

	rc = oce_mbox_init(sc);
	if (rc)
		goto error;


	rc = oce_get_fw_version(sc);
	if (rc)
		goto error;


	rc = oce_get_fw_config(sc);
	if (rc)
		goto error;


	sc->macaddr.size_of_struct = 6;
	rc = oce_read_mac_addr(sc, 0, 1, MAC_ADDRESS_TYPE_NETWORK,
					&sc->macaddr);
	if (rc)
		goto error;
	
	if ((IS_BE(sc) && (sc->flags & OCE_FLAGS_BE3)) || IS_SH(sc)) {
		rc = oce_mbox_check_native_mode(sc);
		if (rc)
			goto error;
	} else
		sc->be3_native = 0;
	
	return rc;

error:
	oce_dma_free(sc, &sc->bsmbx);
	device_printf(sc->dev, "Hardware initialisation failed\n");
	return rc;
}



/**
 * @brief		Releases the obtained pci resources
 * @param sc		software handle to the device
 */
void
oce_hw_pci_free(POCE_SOFTC sc)
{
	int pci_cfg_barnum = 0;

	if (IS_BE(sc) && (sc->flags & OCE_FLAGS_BE2))
		pci_cfg_barnum = OCE_DEV_BE2_CFG_BAR;
	else
		pci_cfg_barnum = OCE_DEV_CFG_BAR;

	if (sc->devcfg_res != NULL) {
		bus_release_resource(sc->dev,
				     SYS_RES_MEMORY,
				     PCIR_BAR(pci_cfg_barnum), sc->devcfg_res);
		sc->devcfg_res = (struct resource *)NULL;
		sc->devcfg_btag = (bus_space_tag_t) 0;
		sc->devcfg_bhandle = (bus_space_handle_t)0;
		sc->devcfg_vhandle = (void *)NULL;
	}

	if (sc->csr_res != NULL) {
		bus_release_resource(sc->dev,
				     SYS_RES_MEMORY,
				     PCIR_BAR(OCE_PCI_CSR_BAR), sc->csr_res);
		sc->csr_res = (struct resource *)NULL;
		sc->csr_btag = (bus_space_tag_t)0;
		sc->csr_bhandle = (bus_space_handle_t)0;
		sc->csr_vhandle = (void *)NULL;
	}

	if (sc->db_res != NULL) {
		bus_release_resource(sc->dev,
				     SYS_RES_MEMORY,
				     PCIR_BAR(OCE_PCI_DB_BAR), sc->db_res);
		sc->db_res = (struct resource *)NULL;
		sc->db_btag = (bus_space_tag_t)0;
		sc->db_bhandle = (bus_space_handle_t)0;
		sc->db_vhandle = (void *)NULL;
	}
}




/**
 * @brief 		Function to get the PCI capabilities
 * @param sc		software handle to the device
 */
static
void oce_get_pci_capabilities(POCE_SOFTC sc)
{
	uint32_t val;

#if __FreeBSD_version >= 1000000
	#define pci_find_extcap pci_find_cap
#endif

	if (pci_find_extcap(sc->dev, PCIY_PCIX, &val) == 0) {
		if (val != 0) 
			sc->flags |= OCE_FLAGS_PCIX;
	}

	if (pci_find_extcap(sc->dev, PCIY_EXPRESS, &val) == 0) {
		if (val != 0) {
			uint16_t link_status =
			    pci_read_config(sc->dev, val + 0x12, 2);

			sc->flags |= OCE_FLAGS_PCIE;
			sc->pcie_link_speed = link_status & 0xf;
			sc->pcie_link_width = (link_status >> 4) & 0x3f;
		}
	}

	if (pci_find_extcap(sc->dev, PCIY_MSI, &val) == 0) {
		if (val != 0)
			sc->flags |= OCE_FLAGS_MSI_CAPABLE;
	}

	if (pci_find_extcap(sc->dev, PCIY_MSIX, &val) == 0) {
		if (val != 0) {
			val = pci_msix_count(sc->dev);
			sc->flags |= OCE_FLAGS_MSIX_CAPABLE;
		}
	}
}

/**
 * @brief	Allocate PCI resources.
 *
 * @param sc		software handle to the device
 * @returns		0 if successful, or error
 */
int
oce_hw_pci_alloc(POCE_SOFTC sc)
{
	int rr, pci_cfg_barnum = 0;
	pci_sli_intf_t intf;

	pci_enable_busmaster(sc->dev);

	oce_get_pci_capabilities(sc);

	sc->fn = pci_get_function(sc->dev);

	/* setup the device config region */
	if (IS_BE(sc) && (sc->flags & OCE_FLAGS_BE2))
		pci_cfg_barnum = OCE_DEV_BE2_CFG_BAR;
	else
		pci_cfg_barnum = OCE_DEV_CFG_BAR;
		
	rr = PCIR_BAR(pci_cfg_barnum);

	if (IS_BE(sc) || IS_SH(sc)) 
		sc->devcfg_res = bus_alloc_resource_any(sc->dev,
				SYS_RES_MEMORY, &rr,
				RF_ACTIVE|RF_SHAREABLE);
	else
		sc->devcfg_res = bus_alloc_resource_anywhere(sc->dev,
				SYS_RES_MEMORY, &rr, 32768,
				RF_ACTIVE|RF_SHAREABLE);

	if (!sc->devcfg_res)
		goto error;

	sc->devcfg_btag = rman_get_bustag(sc->devcfg_res);
	sc->devcfg_bhandle = rman_get_bushandle(sc->devcfg_res);
	sc->devcfg_vhandle = rman_get_virtual(sc->devcfg_res);

	/* Read the SLI_INTF register and determine whether we
	 * can use this port and its features
	 */
	intf.dw0 = pci_read_config((sc)->dev,OCE_INTF_REG_OFFSET,4);

	if (intf.bits.sli_valid != OCE_INTF_VALID_SIG)
		goto error;
	
	if (intf.bits.sli_rev != OCE_INTF_SLI_REV4) {
		device_printf(sc->dev, "Adapter doesnt support SLI4\n");
		goto error;
	}

	if (intf.bits.sli_if_type == OCE_INTF_IF_TYPE_1)
		sc->flags |= OCE_FLAGS_MBOX_ENDIAN_RQD;

	if (intf.bits.sli_hint1 == OCE_INTF_FUNC_RESET_REQD)
		sc->flags |= OCE_FLAGS_FUNCRESET_RQD;

	if (intf.bits.sli_func_type == OCE_INTF_VIRT_FUNC)
		sc->flags |= OCE_FLAGS_VIRTUAL_PORT;

	/* Lancer has one BAR (CFG) but BE3 has three (CFG, CSR, DB) */
	if (IS_BE(sc) || IS_SH(sc)) {
		/* set up CSR region */
		rr = PCIR_BAR(OCE_PCI_CSR_BAR);
		sc->csr_res = bus_alloc_resource_any(sc->dev,
				SYS_RES_MEMORY, &rr, RF_ACTIVE|RF_SHAREABLE);
		if (!sc->csr_res)
			goto error;
		sc->csr_btag = rman_get_bustag(sc->csr_res);
		sc->csr_bhandle = rman_get_bushandle(sc->csr_res);
		sc->csr_vhandle = rman_get_virtual(sc->csr_res);
		
		/* set up DB doorbell region */
		rr = PCIR_BAR(OCE_PCI_DB_BAR);
		sc->db_res = bus_alloc_resource_any(sc->dev,
				SYS_RES_MEMORY, &rr, RF_ACTIVE|RF_SHAREABLE);
		if (!sc->db_res)
			goto error;
		sc->db_btag = rman_get_bustag(sc->db_res);
		sc->db_bhandle = rman_get_bushandle(sc->db_res);
		sc->db_vhandle = rman_get_virtual(sc->db_res);
	}

	return 0;

error:	
	oce_hw_pci_free(sc);
	return ENXIO;
}


/**
 * @brief		Function for device shutdown
 * @param sc		software handle to the device
 * @returns		0 on success, error otherwise
 */
void
oce_hw_shutdown(POCE_SOFTC sc)
{

	oce_stats_free(sc);
	/* disable hardware interrupts */
	oce_hw_intr_disable(sc);
#if defined(INET6) || defined(INET)
	/* Free LRO resources */
	oce_free_lro(sc);
#endif
	/* Release queue*/
	oce_queue_release_all(sc);
	/*Delete Network Interface*/
	oce_delete_nw_interface(sc);
	/* After fw clean we dont send any cmds to fw.*/
	oce_fw_clean(sc);
	/* release intr resources */
	oce_intr_free(sc);
	/* release PCI resources */
	oce_hw_pci_free(sc);
	/* free mbox specific resources */
	LOCK_DESTROY(&sc->bmbx_lock);
	LOCK_DESTROY(&sc->dev_lock);

	oce_dma_free(sc, &sc->bsmbx);
}


/**
 * @brief		Function for creating nw interface.
 * @param sc		software handle to the device
 * @returns		0 on success, error otherwise
 */
int
oce_create_nw_interface(POCE_SOFTC sc)
{
	int rc;
	uint32_t capab_flags;
	uint32_t capab_en_flags;

	/* interface capabilities to give device when creating interface */
	capab_flags = OCE_CAPAB_FLAGS;

	/* capabilities to enable by default (others set dynamically) */
	capab_en_flags = OCE_CAPAB_ENABLE;

	if (IS_XE201(sc)) {
		/* LANCER A0 workaround */
		capab_en_flags &= ~MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR;
		capab_flags &= ~MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR;
	}

	if (IS_SH(sc) || IS_XE201(sc))
		capab_flags |= MBX_RX_IFACE_FLAGS_MULTICAST;

        if (sc->enable_hwlro) {
                capab_flags |= MBX_RX_IFACE_FLAGS_LRO;
                capab_en_flags |= MBX_RX_IFACE_FLAGS_LRO;
        }

	/* enable capabilities controlled via driver startup parameters */
	if (is_rss_enabled(sc))
		capab_en_flags |= MBX_RX_IFACE_FLAGS_RSS;
	else {
		capab_en_flags &= ~MBX_RX_IFACE_FLAGS_RSS;
		capab_flags &= ~MBX_RX_IFACE_FLAGS_RSS;
	}

	rc = oce_if_create(sc,
			   capab_flags,
			   capab_en_flags,
			   0, &sc->macaddr.mac_addr[0], &sc->if_id);
	if (rc)
		return rc;

	atomic_inc_32(&sc->nifs);

	sc->if_cap_flags = capab_en_flags;

	/* set default flow control */
	rc = oce_set_flow_control(sc, sc->flow_control);
	if (rc)
		goto error;

	rc = oce_rxf_set_promiscuous(sc, sc->promisc);
	if (rc)
		goto error;

	return rc;

error:
	oce_delete_nw_interface(sc);
	return rc;

}

/**
 * @brief		Function to delete a nw interface.
 * @param sc		software handle to the device
 */
void
oce_delete_nw_interface(POCE_SOFTC sc)
{
	/* currently only single interface is implmeneted */
	if (sc->nifs > 0) {
		oce_if_del(sc, sc->if_id);
		atomic_dec_32(&sc->nifs);
	}
}

/**
 * @brief Soft reset.
 * @param sc		software handle to the device
 * @returns		0 on success, error otherwise
 */
int
oce_pci_soft_reset(POCE_SOFTC sc)
{
	int rc;
	mpu_ep_control_t ctrl;

	ctrl.dw0 = OCE_READ_CSR_MPU(sc, csr, MPU_EP_CONTROL);
	ctrl.bits.cpu_reset = 1;
	OCE_WRITE_CSR_MPU(sc, csr, MPU_EP_CONTROL, ctrl.dw0);
	DELAY(50);
	rc=oce_POST(sc);

	return rc;
}

/**
 * @brief		Function for hardware start
 * @param sc		software handle to the device
 * @returns		0 on success, error otherwise
 */
int
oce_hw_start(POCE_SOFTC sc)
{
	struct link_status link = { 0 };
	int rc = 0;

	rc = oce_get_link_status(sc, &link);
	if (rc) 
		return 1;
	
	if (link.logical_link_status == NTWK_LOGICAL_LINK_UP) {
		sc->link_status = NTWK_LOGICAL_LINK_UP;
		if_link_state_change(sc->ifp, LINK_STATE_UP);
	} else {
		sc->link_status = NTWK_LOGICAL_LINK_DOWN;
		if_link_state_change(sc->ifp, LINK_STATE_DOWN);
	}

	sc->link_speed = link.phys_port_speed;
	sc->qos_link_speed = (uint32_t )link.qos_link_speed * 10;

	rc = oce_start_mq(sc->mq);
	
	/* we need to get MCC aync events. So enable intrs and arm
	   first EQ, Other EQs will be armed after interface is UP 
	*/
	oce_hw_intr_enable(sc);
	oce_arm_eq(sc, sc->eq[0]->eq_id, 0, TRUE, FALSE);

	/* Send first mcc cmd and after that we get gracious
	   MCC notifications from FW
	*/
	oce_first_mcc_cmd(sc);

	return rc;
}


/**
 * @brief 		Function for hardware enable interupts.
 * @param sc		software handle to the device
 */
void
oce_hw_intr_enable(POCE_SOFTC sc)
{
	uint32_t reg;

	reg = OCE_READ_REG32(sc, devcfg, PCICFG_INTR_CTRL);
	reg |= HOSTINTR_MASK;
	OCE_WRITE_REG32(sc, devcfg, PCICFG_INTR_CTRL, reg);

}


/**
 * @brief 		Function for hardware disable interupts
 * @param sc		software handle to the device
 */
void
oce_hw_intr_disable(POCE_SOFTC sc)
{
	uint32_t reg;
	
	reg = OCE_READ_REG32(sc, devcfg, PCICFG_INTR_CTRL);
	reg &= ~HOSTINTR_MASK;
	OCE_WRITE_REG32(sc, devcfg, PCICFG_INTR_CTRL, reg);
}



/**
 * @brief		Function for hardware update multicast filter
 * @param sc		software handle to the device
 */
int
oce_hw_update_multicast(POCE_SOFTC sc)
{
	struct ifnet    *ifp = sc->ifp;
	struct ifmultiaddr *ifma;
	struct mbx_set_common_iface_multicast *req = NULL;
	OCE_DMA_MEM dma;
	int rc = 0;

	/* Allocate DMA mem*/
	if (oce_dma_alloc(sc, sizeof(struct mbx_set_common_iface_multicast),
							&dma, 0))
		return ENOMEM;

	req = OCE_DMAPTR(&dma, struct mbx_set_common_iface_multicast);
	bzero(req, sizeof(struct mbx_set_common_iface_multicast));

#if __FreeBSD_version > 800000
	if_maddr_rlock(ifp);
#endif
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (req->params.req.num_mac == OCE_MAX_MC_FILTER_SIZE) {
			/*More multicast addresses than our hardware table
			  So Enable multicast promiscus in our hardware to
			  accept all multicat packets
			*/
			req->params.req.promiscuous = 1;
			break;
		}
		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
			&req->params.req.mac[req->params.req.num_mac],
			ETH_ADDR_LEN);
		req->params.req.num_mac = req->params.req.num_mac + 1;
	}
#if __FreeBSD_version > 800000
	if_maddr_runlock(ifp);
#endif
	req->params.req.if_id = sc->if_id;
	rc = oce_update_multicast(sc, &dma);
	oce_dma_free(sc, &dma);
	return rc;
}

