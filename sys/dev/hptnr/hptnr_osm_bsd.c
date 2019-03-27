/* $Id: osm_bsd.c,v 1.36 2010/05/11 03:12:11 lcn Exp $ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * HighPoint RAID Driver for FreeBSD
 * Copyright (C) 2005-2011 HighPoint Technologies, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#include <dev/hptnr/hptnr_config.h>
#include <dev/hptnr/os_bsd.h>
#include <dev/hptnr/hptintf.h>
int msi = 0;
int debug_flag = 0;
static HIM *hpt_match(device_t dev, int scan)
{
	PCI_ID pci_id;
	HIM *him;
	int i;

	for (him = him_list; him; him = him->next) {
		for (i=0; him->get_supported_device_id(i, &pci_id); i++) {
			if (scan && him->get_controller_count)
				him->get_controller_count(&pci_id,0,0);
			if ((pci_get_vendor(dev) == pci_id.vid) &&
				(pci_get_device(dev) == pci_id.did)){
				return (him);
			}
		}
	}

	return (NULL);
}

static int hpt_probe(device_t dev)
{
	HIM *him;

	him = hpt_match(dev, 0);
	if (him != NULL) {
		KdPrint(("hpt_probe: adapter at PCI %d:%d:%d, IRQ %d",
			pci_get_bus(dev), pci_get_slot(dev), pci_get_function(dev), pci_get_irq(dev)
			));
		device_set_desc(dev, him->name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int hpt_attach(device_t dev)
{
	PHBA hba = (PHBA)device_get_softc(dev);
	HIM *him;
	PCI_ID pci_id;
	HPT_UINT size;
	PVBUS vbus;
	PVBUS_EXT vbus_ext;
	
	KdPrint(("hpt_attach(%d/%d/%d)", pci_get_bus(dev), pci_get_slot(dev), pci_get_function(dev)));

	him = hpt_match(dev, 1);
	hba->ext_type = EXT_TYPE_HBA;
	hba->ldm_adapter.him = him;

	pci_enable_busmaster(dev);

	pci_id.vid = pci_get_vendor(dev);
	pci_id.did = pci_get_device(dev);
	pci_id.rev = pci_get_revid(dev);
	pci_id.subsys = (HPT_U32)(pci_get_subdevice(dev)) << 16 | pci_get_subvendor(dev);

	size = him->get_adapter_size(&pci_id);
	hba->ldm_adapter.him_handle = malloc(size, M_DEVBUF, M_WAITOK);

	hba->pcidev = dev;
	hba->pciaddr.tree = 0;
	hba->pciaddr.bus = pci_get_bus(dev);
	hba->pciaddr.device = pci_get_slot(dev);
	hba->pciaddr.function = pci_get_function(dev);

	if (!him->create_adapter(&pci_id, hba->pciaddr, hba->ldm_adapter.him_handle, hba)) {
		free(hba->ldm_adapter.him_handle, M_DEVBUF);
		return ENXIO;
	}

	os_printk("adapter at PCI %d:%d:%d, IRQ %d",
		hba->pciaddr.bus, hba->pciaddr.device, hba->pciaddr.function, pci_get_irq(dev));

	if (!ldm_register_adapter(&hba->ldm_adapter)) {
		size = ldm_get_vbus_size();
		vbus_ext = malloc(sizeof(VBUS_EXT) + size, M_DEVBUF, M_WAITOK |
			M_ZERO);
		vbus_ext->ext_type = EXT_TYPE_VBUS;
		ldm_create_vbus((PVBUS)vbus_ext->vbus, vbus_ext);
		ldm_register_adapter(&hba->ldm_adapter);
	}

	ldm_for_each_vbus(vbus, vbus_ext) {
		if (hba->ldm_adapter.vbus==vbus) {
			hba->vbus_ext = vbus_ext;
			hba->next = vbus_ext->hba_list;
			vbus_ext->hba_list = hba;
			break;
		}
	}	
	return 0;
}

/*
 * Maybe we'd better to use the bus_dmamem_alloc to alloc DMA memory,
 * but there are some problems currently (alignment, etc).
 */
static __inline void *__get_free_pages(int order)
{
	/* don't use low memory - other devices may get starved */
	return contigmalloc(PAGE_SIZE<<order, 
			M_DEVBUF, M_WAITOK, BUS_SPACE_MAXADDR_24BIT, BUS_SPACE_MAXADDR, PAGE_SIZE, 0);
}

static __inline void free_pages(void *p, int order)
{
	contigfree(p, PAGE_SIZE<<order, M_DEVBUF);
}

static int hpt_alloc_mem(PVBUS_EXT vbus_ext)
{
	PHBA hba;
	struct freelist *f;
	HPT_UINT i;
	void **p;

	for (hba = vbus_ext->hba_list; hba; hba = hba->next)
		hba->ldm_adapter.him->get_meminfo(hba->ldm_adapter.him_handle);

	ldm_get_mem_info((PVBUS)vbus_ext->vbus, 0);

	for (f=vbus_ext->freelist_head; f; f=f->next) {
		KdPrint(("%s: %d*%d=%d bytes",
			f->tag, f->count, f->size, f->count*f->size));
		for (i=0; i<f->count; i++) {
			p = (void **)malloc(f->size, M_DEVBUF, M_WAITOK);
			if (!p)	return (ENXIO);
			*p = f->head;
			f->head = p;
		}
	}

	for (f=vbus_ext->freelist_dma_head; f; f=f->next) {
		int order, size, j;

		HPT_ASSERT((f->size & (f->alignment-1))==0);

		for (order=0, size=PAGE_SIZE; size<f->size; order++, size<<=1)
			;

		KdPrint(("%s: %d*%d=%d bytes, order %d",
			f->tag, f->count, f->size, f->count*f->size, order));
		HPT_ASSERT(f->alignment<=PAGE_SIZE);

		for (i=0; i<f->count;) {
			p = (void **)__get_free_pages(order);
			if (!p) return -1;
			for (j = size/f->size; j && i<f->count; i++,j--) {
				*p = f->head;
				*(BUS_ADDRESS *)(p+1) = (BUS_ADDRESS)vtophys(p);
				f->head = p;
				p = (void **)((unsigned long)p + f->size);
			}
		}
	}
	
	HPT_ASSERT(PAGE_SIZE==DMAPOOL_PAGE_SIZE);

	for (i=0; i<os_max_cache_pages; i++) {
		p = (void **)__get_free_pages(0);
		if (!p) return -1;
		HPT_ASSERT(((HPT_UPTR)p & (DMAPOOL_PAGE_SIZE-1))==0);
		dmapool_put_page((PVBUS)vbus_ext->vbus, p, (BUS_ADDRESS)vtophys(p));
	}

	return 0;
}

static void hpt_free_mem(PVBUS_EXT vbus_ext)
{
	struct freelist *f;
	void *p;
	int i;
	BUS_ADDRESS bus;

	for (f=vbus_ext->freelist_head; f; f=f->next) {
#if DBG
		if (f->count!=f->reserved_count) {
			KdPrint(("memory leak for freelist %s (%d/%d)", f->tag, f->count, f->reserved_count));
		}
#endif
		while ((p=freelist_get(f)))
			free(p, M_DEVBUF);
	}

	for (i=0; i<os_max_cache_pages; i++) {
		p = dmapool_get_page((PVBUS)vbus_ext->vbus, &bus);
		HPT_ASSERT(p);
		free_pages(p, 0);
	}

	for (f=vbus_ext->freelist_dma_head; f; f=f->next) {
		int order, size;
#if DBG
		if (f->count!=f->reserved_count) {
			KdPrint(("memory leak for dma freelist %s (%d/%d)", f->tag, f->count, f->reserved_count));
		}
#endif
		for (order=0, size=PAGE_SIZE; size<f->size; order++, size<<=1) ;

		while ((p=freelist_get_dma(f, &bus))) {
			if (order)
				free_pages(p, order);
			else {
			/* can't free immediately since other blocks in this page may still be in the list */
				if (((HPT_UPTR)p & (PAGE_SIZE-1))==0)
					dmapool_put_page((PVBUS)vbus_ext->vbus, p, bus);
			}
		}
	}
	
	while ((p = dmapool_get_page((PVBUS)vbus_ext->vbus, &bus)))
		free_pages(p, 0);
}

static int hpt_init_vbus(PVBUS_EXT vbus_ext)
{
	PHBA hba;

	for (hba = vbus_ext->hba_list; hba; hba = hba->next)
		if (!hba->ldm_adapter.him->initialize(hba->ldm_adapter.him_handle)) {
			KdPrint(("fail to initialize %p", hba));
			return -1;
		}

	ldm_initialize_vbus((PVBUS)vbus_ext->vbus, &vbus_ext->hba_list->ldm_adapter);
	return 0;
}

static void hpt_flush_done(PCOMMAND pCmd)
{
	PVDEV vd = pCmd->target;

	if (mIsArray(vd->type) && vd->u.array.transform && vd!=vd->u.array.transform->target) {
		vd = vd->u.array.transform->target;
		HPT_ASSERT(vd);
		pCmd->target = vd;
		pCmd->Result = RETURN_PENDING;
		vdev_queue_cmd(pCmd);
		return;
	}

	*(int *)pCmd->priv = 1;
	wakeup(pCmd);
}

/*
 * flush a vdev (without retry).
 */
static int hpt_flush_vdev(PVBUS_EXT vbus_ext, PVDEV vd)
{
	PCOMMAND pCmd;
	int result = 0, done;
	HPT_UINT count;

	KdPrint(("flusing dev %p", vd));

	hpt_assert_vbus_locked(vbus_ext);

	if (mIsArray(vd->type) && vd->u.array.transform)
		count = max(vd->u.array.transform->source->cmds_per_request,
					vd->u.array.transform->target->cmds_per_request);
	else
		count = vd->cmds_per_request;

	pCmd = ldm_alloc_cmds(vd->vbus, count);

	if (!pCmd) {
		return -1;
	}

	pCmd->type = CMD_TYPE_FLUSH;
	pCmd->flags.hard_flush = 1;
	pCmd->target = vd;
	pCmd->done = hpt_flush_done;
	done = 0;
	pCmd->priv = &done;

	ldm_queue_cmd(pCmd);
	
	if (!done) {
		while (hpt_sleep(vbus_ext, pCmd, PPAUSE, "hptfls", HPT_OSM_TIMEOUT)) {
			ldm_reset_vbus(vd->vbus);
		}
	}

	KdPrint(("flush result %d", pCmd->Result));

	if (pCmd->Result!=RETURN_SUCCESS)
		result = -1;

	ldm_free_cmds(pCmd);

	return result;
}

static void hpt_stop_tasks(PVBUS_EXT vbus_ext);
static void hpt_shutdown_vbus(PVBUS_EXT vbus_ext, int howto)
{
	PVBUS     vbus = (PVBUS)vbus_ext->vbus;
	PHBA hba;
	int i;
	
	KdPrint(("hpt_shutdown_vbus"));

	/* stop all ctl tasks and disable the worker taskqueue */
	hpt_stop_tasks(vbus_ext);
	hpt_lock_vbus(vbus_ext);
	vbus_ext->worker.ta_context = 0;

	/* flush devices */
	for (i=0; i<osm_max_targets; i++) {
		PVDEV vd = ldm_find_target(vbus, i);
		if (vd) {
			/* retry once */
			if (hpt_flush_vdev(vbus_ext, vd))
				hpt_flush_vdev(vbus_ext, vd);
		}
	}

	ldm_shutdown(vbus);
	hpt_unlock_vbus(vbus_ext);

	ldm_release_vbus(vbus);

	for (hba=vbus_ext->hba_list; hba; hba=hba->next)
		bus_teardown_intr(hba->pcidev, hba->irq_res, hba->irq_handle);

	hpt_free_mem(vbus_ext);

	while ((hba=vbus_ext->hba_list)) {
		vbus_ext->hba_list = hba->next;
		free(hba->ldm_adapter.him_handle, M_DEVBUF);
	}

	callout_drain(&vbus_ext->timer);
	mtx_destroy(&vbus_ext->lock);
	free(vbus_ext, M_DEVBUF);
	KdPrint(("hpt_shutdown_vbus done"));
}

static void __hpt_do_tasks(PVBUS_EXT vbus_ext)
{
	OSM_TASK *tasks;

	tasks = vbus_ext->tasks;
	vbus_ext->tasks = 0;

	while (tasks) {
		OSM_TASK *t = tasks;
		tasks = t->next;
		t->next = 0;
		t->func(vbus_ext->vbus, t->data);
	}
}

static void hpt_do_tasks(PVBUS_EXT vbus_ext, int pending)
{
	if(vbus_ext){
		hpt_lock_vbus(vbus_ext);
		__hpt_do_tasks(vbus_ext);
		hpt_unlock_vbus(vbus_ext);
	}
}

static void hpt_action(struct cam_sim *sim, union ccb *ccb);
static void hpt_poll(struct cam_sim *sim);
static void hpt_async(void * callback_arg, u_int32_t code, struct cam_path * path, void * arg);
static void hpt_pci_intr(void *arg);

static __inline POS_CMDEXT cmdext_get(PVBUS_EXT vbus_ext)
{
	POS_CMDEXT p = vbus_ext->cmdext_list;
	if (p)
		vbus_ext->cmdext_list = p->next;
	return p;
}

static __inline void cmdext_put(POS_CMDEXT p)
{
	p->next = p->vbus_ext->cmdext_list;
	p->vbus_ext->cmdext_list = p;
}

static void hpt_timeout(void *arg)
{
	PCOMMAND pCmd = (PCOMMAND)arg;
	POS_CMDEXT ext = (POS_CMDEXT)pCmd->priv;
	
	KdPrint(("pCmd %p timeout", pCmd));
	
	ldm_reset_vbus((PVBUS)ext->vbus_ext->vbus);
}

static void os_cmddone(PCOMMAND pCmd)
{
	POS_CMDEXT ext = (POS_CMDEXT)pCmd->priv;
	union ccb *ccb = ext->ccb;
	HPT_U8 *cdb;
		
	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		cdb = ccb->csio.cdb_io.cdb_ptr;
	else
		cdb = ccb->csio.cdb_io.cdb_bytes;

	KdPrint(("os_cmddone(%p, %d)", pCmd, pCmd->Result));

	callout_stop(&ext->timeout);
	switch(cdb[0]) {
		case 0x85: /*ATA_16*/
		case 0xA1: /*ATA_12*/
		{
			PassthroughCmd *passthru = &pCmd->uCmd.Passthrough;
			HPT_U8 *sense_buffer = (HPT_U8 *)&ccb->csio.sense_data;
			memset(&ccb->csio.sense_data, 0,sizeof(ccb->csio.sense_data));

			sense_buffer[0] = 0x72; /* Response Code */
			sense_buffer[7] = 14; /* Additional Sense Length */
	
			sense_buffer[8] = 0x9; /* ATA Return Descriptor */
			sense_buffer[9] = 0xc; /* Additional Descriptor Length */
			sense_buffer[11] = (HPT_U8)passthru->bFeaturesReg; /* Error */
			sense_buffer[13] = (HPT_U8)passthru->bSectorCountReg;  /* Sector Count (7:0) */
			sense_buffer[15] = (HPT_U8)passthru->bLbaLowReg; /* LBA Low (7:0) */
			sense_buffer[17] = (HPT_U8)passthru->bLbaMidReg; /* LBA Mid (7:0) */
			sense_buffer[19] = (HPT_U8)passthru->bLbaHighReg; /* LBA High (7:0) */
	
			if ((cdb[0] == 0x85) && (cdb[1] & 0x1))
			{
				sense_buffer[10] = 1;
				sense_buffer[12] = (HPT_U8)(passthru->bSectorCountReg >> 8); /* Sector Count (15:8) */
				sense_buffer[14] = (HPT_U8)(passthru->bLbaLowReg >> 8);	/* LBA Low (15:8) */
				sense_buffer[16] = (HPT_U8)(passthru->bLbaMidReg >> 8); /* LBA Mid (15:8) */
				sense_buffer[18] = (HPT_U8)(passthru->bLbaHighReg >> 8); /* LBA High (15:8) */
			}
	
			sense_buffer[20] = (HPT_U8)passthru->bDriveHeadReg; /* Device */
			sense_buffer[21] = (HPT_U8)passthru->bCommandReg; /* Status */
			KdPrint(("sts 0x%x err 0x%x low 0x%x mid 0x%x hig 0x%x dh 0x%x sc 0x%x",
					 passthru->bCommandReg,
					 passthru->bFeaturesReg,
					 passthru->bLbaLowReg,
					 passthru->bLbaMidReg,
					 passthru->bLbaHighReg,
					 passthru->bDriveHeadReg,
					 passthru->bSectorCountReg));
			KdPrint(("result:0x%x,bFeaturesReg:0x%04x,bSectorCountReg:0x%04x,LBA:0x%04x%04x%04x ",
				pCmd->Result,passthru->bFeaturesReg,passthru->bSectorCountReg,
				passthru->bLbaHighReg,passthru->bLbaMidReg,passthru->bLbaLowReg));
		}
		default:
			break;
	}

	switch(pCmd->Result) {
	case RETURN_SUCCESS:
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case RETURN_BAD_DEVICE:
		ccb->ccb_h.status = CAM_DEV_NOT_THERE;
		break;
	case RETURN_DEVICE_BUSY:
		ccb->ccb_h.status = CAM_BUSY;
		break;
	case RETURN_INVALID_REQUEST:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case RETURN_SELECTION_TIMEOUT:
		ccb->ccb_h.status = CAM_SEL_TIMEOUT;
		break;
	case RETURN_RETRY:
		ccb->ccb_h.status = CAM_BUSY;
		break;
	default:
		ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
		break;
	}

	if (pCmd->flags.data_in) {
		bus_dmamap_sync(ext->vbus_ext->io_dmat, ext->dma_map, BUS_DMASYNC_POSTREAD);
	}
	else if (pCmd->flags.data_out) {
		bus_dmamap_sync(ext->vbus_ext->io_dmat, ext->dma_map, BUS_DMASYNC_POSTWRITE);
	}
	
	bus_dmamap_unload(ext->vbus_ext->io_dmat, ext->dma_map);

	cmdext_put(ext);
	ldm_free_cmds(pCmd);
	xpt_done(ccb);
}

static int os_buildsgl(PCOMMAND pCmd, PSG pSg, int logical)
{
	/* since we have provided physical sg, nobody will ask us to build physical sg */
	HPT_ASSERT(0);
	return FALSE;
}

static void hpt_io_dmamap_callback(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	PCOMMAND pCmd = (PCOMMAND)arg;
	POS_CMDEXT ext = (POS_CMDEXT)pCmd->priv;
	PSG psg = pCmd->psg;
	int idx;
	
	HPT_ASSERT(pCmd->flags.physical_sg);
	
	if (error)
		panic("busdma error");
		
	HPT_ASSERT(nsegs<=os_max_sg_descriptors);

	if (nsegs != 0) {
		for (idx = 0; idx < nsegs; idx++, psg++) {
			psg->addr.bus = segs[idx].ds_addr;
			psg->size = segs[idx].ds_len;
			psg->eot = 0;
		}
		psg[-1].eot = 1;
	
		if (pCmd->flags.data_in) {
			bus_dmamap_sync(ext->vbus_ext->io_dmat, ext->dma_map,
			    BUS_DMASYNC_PREREAD);
		}
		else if (pCmd->flags.data_out) {
			bus_dmamap_sync(ext->vbus_ext->io_dmat, ext->dma_map,
			    BUS_DMASYNC_PREWRITE);
		}
	}

	callout_reset(&ext->timeout, HPT_OSM_TIMEOUT, hpt_timeout, pCmd);
	ldm_queue_cmd(pCmd);
}

static void hpt_scsi_io(PVBUS_EXT vbus_ext, union ccb *ccb)
{
	PVBUS vbus = (PVBUS)vbus_ext->vbus;
	PVDEV vd;
	PCOMMAND pCmd;
	POS_CMDEXT ext;
	HPT_U8 *cdb;

	if (ccb->ccb_h.flags & CAM_CDB_POINTER)
		cdb = ccb->csio.cdb_io.cdb_ptr;
	else
		cdb = ccb->csio.cdb_io.cdb_bytes;
	
	KdPrint(("hpt_scsi_io: ccb %x id %d lun %d cdb %x-%x-%x",
		ccb,
		ccb->ccb_h.target_id, ccb->ccb_h.target_lun,
		*(HPT_U32 *)&cdb[0], *(HPT_U32 *)&cdb[4], *(HPT_U32 *)&cdb[8]
	));

	/* ccb->ccb_h.path_id is not our bus id - don't check it */
	if (ccb->ccb_h.target_lun != 0 ||
		ccb->ccb_h.target_id >= osm_max_targets ||
		(ccb->ccb_h.flags & CAM_CDB_PHYS))
	{
		ccb->ccb_h.status = CAM_TID_INVALID;
		xpt_done(ccb);
		return;
	}

	vd = ldm_find_target(vbus, ccb->ccb_h.target_id);

	if (!vd) {
		ccb->ccb_h.status = CAM_SEL_TIMEOUT;
		xpt_done(ccb);
		return;
	}
   
	switch (cdb[0]) {
	case TEST_UNIT_READY:
	case START_STOP_UNIT:
	case SYNCHRONIZE_CACHE:
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case 0x85: /*ATA_16*/
	case 0xA1: /*ATA_12*/
	{
		int error;
		HPT_U8 prot;
		PassthroughCmd *passthru;	
		
		if (mIsArray(vd->type)) {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		}
		
		HPT_ASSERT(vd->type == VD_RAW && vd->u.raw.legacy_disk);
		
		prot = (cdb[1] & 0x1e) >> 1;
		
		
		if (prot < 3 || prot > 5) 
		{
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		}
		
		pCmd = ldm_alloc_cmds(vbus, vd->cmds_per_request);
		if (!pCmd) {
			HPT_ASSERT(0);
			ccb->ccb_h.status = CAM_BUSY;
			break;
		}
		
		passthru = &pCmd->uCmd.Passthrough;
		if (cdb[0] == 0x85/*ATA_16*/) {
			if (cdb[1] & 0x1) {
				passthru->bFeaturesReg =
					((HPT_U16)cdb[3] << 8)
						| cdb[4];
				passthru->bSectorCountReg =
					((HPT_U16)cdb[5] << 8) |
						cdb[6];
				passthru->bLbaLowReg =
					((HPT_U16)cdb[7] << 8) |
						cdb[8];
				passthru->bLbaMidReg =
					((HPT_U16)cdb[9] << 8) |
						cdb[10];
				passthru->bLbaHighReg =
					((HPT_U16)cdb[11] << 8) |
						cdb[12];
			} else {
				passthru->bFeaturesReg = cdb[4];
				passthru->bSectorCountReg = cdb[6];
				passthru->bLbaLowReg = cdb[8];
				passthru->bLbaMidReg = cdb[10];
				passthru->bLbaHighReg = cdb[12];
			}
			passthru->bDriveHeadReg = cdb[13];
			passthru->bCommandReg = cdb[14];
		
		} else { /*ATA_12*/
		
			passthru->bFeaturesReg = cdb[3];
			passthru->bSectorCountReg = cdb[4];
			passthru->bLbaLowReg = cdb[5];
			passthru->bLbaMidReg = cdb[6];
			passthru->bLbaHighReg = cdb[7];
			passthru->bDriveHeadReg = cdb[8];
			passthru->bCommandReg = cdb[9];
		}
		
		if (cdb[1] & 0xe0) {
			
		
			if (!(passthru->bCommandReg == ATA_CMD_READ_MULTI ||
				passthru->bCommandReg == ATA_CMD_READ_MULTI_EXT ||
				passthru->bCommandReg == ATA_CMD_WRITE_MULTI ||
				passthru->bCommandReg == ATA_CMD_WRITE_MULTI_EXT ||
				passthru->bCommandReg == ATA_CMD_WRITE_MULTI_FUA_EXT)
				) {
				goto error;
			}
		}
		
		
		if (passthru->bFeaturesReg == ATA_SET_FEATURES_XFER &&
			passthru->bCommandReg == ATA_CMD_SET_FEATURES) {
			goto error;
		}

		
		passthru->nSectors = ccb->csio.dxfer_len/ATA_SECTOR_SIZE;
		switch (prot) {
			default: /*None data*/
				break;
			case 4: /*PIO data in, T_DIR=1 match check*/
				if ((cdb[2] & 3) && 
					(cdb[2] & 0x8) == 0)
				{
					OsPrint(("PIO data in, T_DIR=1 match check"));
					goto error;
				}
				pCmd->flags.data_in = 1;
						break;
			case 5: /*PIO data out, T_DIR=0 match check*/
				if ((cdb[2] & 3) && 
					(cdb[2] & 0x8))
				{
					OsPrint(("PIO data out, T_DIR=0 match check"));
					goto error;
				}

				pCmd->flags.data_out = 1;
				break;
		}
		pCmd->type = CMD_TYPE_PASSTHROUGH;
		pCmd->priv = ext = cmdext_get(vbus_ext);
		HPT_ASSERT(ext);
		ext->ccb = ccb;
		pCmd->target = vd;
		pCmd->done = os_cmddone;
		pCmd->buildsgl = os_buildsgl;
		pCmd->psg = ext->psg;

		if(!ccb->csio.dxfer_len)
		{
			ldm_queue_cmd(pCmd);
			return;
		}
		pCmd->flags.physical_sg = 1;
		error = bus_dmamap_load_ccb(vbus_ext->io_dmat, 
					ext->dma_map, ccb, 
					hpt_io_dmamap_callback, pCmd,
				    	BUS_DMA_WAITOK
					);
		KdPrint(("bus_dmamap_load return %d", error));
		if (error && error!=EINPROGRESS) {
			os_printk("bus_dmamap_load error %d", error);
			cmdext_put(ext);
			ldm_free_cmds(pCmd);
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			xpt_done(ccb);
		}
		return;
error:
		ldm_free_cmds(pCmd);
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	case INQUIRY:
	{
		PINQUIRYDATA inquiryData;
		HIM_DEVICE_CONFIG devconf;
		HPT_U8 *rbuf;

		memset(ccb->csio.data_ptr, 0, ccb->csio.dxfer_len);
		inquiryData = (PINQUIRYDATA)ccb->csio.data_ptr;

		if (cdb[1] & 1) {
			rbuf = (HPT_U8 *)inquiryData;
			switch(cdb[2]) {
			case 0:
				rbuf[0] = 0;
				rbuf[1] = 0;
				rbuf[2] = 0;
				rbuf[3] = 3;
				rbuf[4] = 0;
				rbuf[5] = 0x80;
				rbuf[6] = 0x83;
				ccb->ccb_h.status = CAM_REQ_CMP;
				break;
			case 0x80: {
				rbuf[0] = 0;
				rbuf[1] = 0x80;
				rbuf[2] = 0;
				if (vd->type == VD_RAW) {
					rbuf[3] = 20;
					vd->u.raw.him->get_device_config(vd->u.raw.phy_dev,&devconf);
					memcpy(&rbuf[4], devconf.pIdentifyData->SerialNumber, 20);
					ldm_ide_fixstring(&rbuf[4], 20);
				} else {
					rbuf[3] = 1;
					rbuf[4] = 0x20;
				}
				ccb->ccb_h.status = CAM_REQ_CMP;				
				break;
			}
			case 0x83:
				rbuf[0] = 0;
				rbuf[1] = 0x83;
				rbuf[2] = 0;
				rbuf[3] = 12; 
				rbuf[4] = 1;
				rbuf[5] = 2; 
				rbuf[6] = 0;
				rbuf[7] = 8; 
				rbuf[8] = 0; 
				rbuf[9] = 0x19;
				rbuf[10] = 0x3C;
				rbuf[11] = 0;
				rbuf[12] = 0;
				rbuf[13] = 0;
				rbuf[14] = 0;
				rbuf[15] = 0;
				ccb->ccb_h.status = CAM_REQ_CMP;			
				break;
			default:
				ccb->ccb_h.status = CAM_REQ_INVALID;
				break;
			}

			break;
		} 
		else if (cdb[2]) {	
			ccb->ccb_h.status = CAM_REQ_INVALID;
			break;
		}

		inquiryData->DeviceType = 0; /*DIRECT_ACCESS_DEVICE*/
		inquiryData->Versions = 5; /*SPC-3*/
		inquiryData->ResponseDataFormat = 2;
		inquiryData->AdditionalLength = 0x5b;
		inquiryData->CommandQueue = 1;

		if (ccb->csio.dxfer_len > 63) {
			rbuf = (HPT_U8 *)inquiryData;			
			rbuf[58] = 0x60;
			rbuf[59] = 0x3;
			
			rbuf[64] = 0x3; 
			rbuf[66] = 0x3; 
			rbuf[67] = 0x20;
			
		}
		
		if (vd->type == VD_RAW) {
			vd->u.raw.him->get_device_config(vd->u.raw.phy_dev,&devconf);

			if ((devconf.pIdentifyData->GeneralConfiguration & 0x80))
				inquiryData->RemovableMedia = 1;

			
			memcpy(&inquiryData->VendorId, "ATA     ", 8);
			memcpy(&inquiryData->ProductId, devconf.pIdentifyData->ModelNumber, 16);
			ldm_ide_fixstring((HPT_U8 *)&inquiryData->ProductId, 16);
			memcpy(&inquiryData->ProductRevisionLevel, devconf.pIdentifyData->FirmwareRevision, 4);
			ldm_ide_fixstring((HPT_U8 *)&inquiryData->ProductRevisionLevel, 4);
			if (inquiryData->ProductRevisionLevel[0] == 0 || inquiryData->ProductRevisionLevel[0] == ' ')
				memcpy(&inquiryData->ProductRevisionLevel, "n/a ", 4);
		} else {
			memcpy(&inquiryData->VendorId, "HPT     ", 8);
			snprintf((char *)&inquiryData->ProductId, 16, "DISK_%d_%d        ",
				os_get_vbus_seq(vbus_ext), vd->target_id);
			inquiryData->ProductId[15] = ' ';
			memcpy(&inquiryData->ProductRevisionLevel, "4.00", 4);
		}

		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case READ_CAPACITY:
	{
		HPT_U8 *rbuf = ccb->csio.data_ptr;
		HPT_U32 cap;
		HPT_U8 sector_size_shift = 0;
		HPT_U64 new_cap;
		HPT_U32 sector_size = 0;

		if (mIsArray(vd->type))
			sector_size_shift = vd->u.array.sector_size_shift;
		else{
			if(vd->type == VD_RAW){
				sector_size = vd->u.raw.logical_sector_size;
			}
		
			switch (sector_size) {
				case 0x1000:
					KdPrint(("set 4k setctor size in READ_CAPACITY"));
					sector_size_shift = 3;
					break;
				default:
					break;
			}			
		}
		new_cap = vd->capacity >> sector_size_shift;
		
		if (new_cap > 0xfffffffful)
			cap = 0xffffffff;
		else
			cap = new_cap - 1;
			
		rbuf[0] = (HPT_U8)(cap>>24);
		rbuf[1] = (HPT_U8)(cap>>16);
		rbuf[2] = (HPT_U8)(cap>>8);
		rbuf[3] = (HPT_U8)cap;
		rbuf[4] = 0;
		rbuf[5] = 0;
		rbuf[6] = 2 << sector_size_shift;
		rbuf[7] = 0;

		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	
	case REPORT_LUNS:
	{
		HPT_U8 *rbuf = ccb->csio.data_ptr;
		memset(rbuf, 0, 16);
		rbuf[3] = 8;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;				
	}
	case SERVICE_ACTION_IN: 
	{
		HPT_U8 *rbuf = ccb->csio.data_ptr;
		HPT_U64	cap = 0;
		HPT_U8 sector_size_shift = 0;
		HPT_U32 sector_size = 0;

		if(mIsArray(vd->type))
			sector_size_shift = vd->u.array.sector_size_shift;
		else{
			if(vd->type == VD_RAW){
				sector_size = vd->u.raw.logical_sector_size;
			}
		
			switch (sector_size) {
				case 0x1000:
					KdPrint(("set 4k setctor size in SERVICE_ACTION_IN"));
					sector_size_shift = 3;
					break;
				default:
					break;
			}			
		}
		cap = (vd->capacity >> sector_size_shift) - 1;
					
		rbuf[0] = (HPT_U8)(cap>>56);
		rbuf[1] = (HPT_U8)(cap>>48);
		rbuf[2] = (HPT_U8)(cap>>40);
		rbuf[3] = (HPT_U8)(cap>>32);
		rbuf[4] = (HPT_U8)(cap>>24);
		rbuf[5] = (HPT_U8)(cap>>16);
		rbuf[6] = (HPT_U8)(cap>>8);
		rbuf[7] = (HPT_U8)cap;
		rbuf[8] = 0;
		rbuf[9] = 0;
		rbuf[10] = 2 << sector_size_shift;
		rbuf[11] = 0;
		
		if(!mIsArray(vd->type)){
			rbuf[13] = vd->u.raw.logicalsectors_per_physicalsector;
			rbuf[14] = (HPT_U8)((vd->u.raw.lowest_aligned >> 8) & 0x3f);
			rbuf[15] = (HPT_U8)(vd->u.raw.lowest_aligned);
		}
		
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;	
	}
	
	case READ_6:
	case READ_10:
	case READ_16:
	case WRITE_6:
	case WRITE_10:
	case WRITE_16:
	case 0x13:
	case 0x2f:
	case 0x8f: /* VERIFY_16 */
	{
		int error;
		HPT_U8 sector_size_shift = 0;
		HPT_U32 sector_size = 0;
		pCmd = ldm_alloc_cmds(vbus, vd->cmds_per_request);
		if(!pCmd){
			KdPrint(("Failed to allocate command!"));
			ccb->ccb_h.status = CAM_BUSY;
			break;
		}

		switch (cdb[0])	{
		case READ_6:
		case WRITE_6:
		case 0x13:
			pCmd->uCmd.Ide.Lba =  ((HPT_U32)cdb[1] << 16) | ((HPT_U32)cdb[2] << 8) | (HPT_U32)cdb[3];
			pCmd->uCmd.Ide.nSectors = (HPT_U16) cdb[4];
			break;
		case READ_16:
		case WRITE_16:
		case 0x8f: /* VERIFY_16 */
		{
			HPT_U64 block =
				((HPT_U64)cdb[2]<<56) |
				((HPT_U64)cdb[3]<<48) |
				((HPT_U64)cdb[4]<<40) |
				((HPT_U64)cdb[5]<<32) |
				((HPT_U64)cdb[6]<<24) |
				((HPT_U64)cdb[7]<<16) |
				((HPT_U64)cdb[8]<<8) |
				((HPT_U64)cdb[9]);
			pCmd->uCmd.Ide.Lba = block;
			pCmd->uCmd.Ide.nSectors = (HPT_U16)cdb[13] | ((HPT_U16)cdb[12]<<8);
			break;
		}
		
		default:
			pCmd->uCmd.Ide.Lba = (HPT_U32)cdb[5] | ((HPT_U32)cdb[4] << 8) | ((HPT_U32)cdb[3] << 16) | ((HPT_U32)cdb[2] << 24);
			pCmd->uCmd.Ide.nSectors = (HPT_U16) cdb[8] | ((HPT_U16)cdb[7]<<8);
			break;
		}

		if(mIsArray(vd->type)) {
			sector_size_shift = vd->u.array.sector_size_shift;
		}
		else{
			if(vd->type == VD_RAW){
				sector_size = vd->u.raw.logical_sector_size;
			}
	  		
			switch (sector_size) {
				case 0x1000:
					KdPrint(("<8>resize sector size from 4k to 512"));
					sector_size_shift = 3;
					break;
				default:
					break;
	 		}			
		}
		pCmd->uCmd.Ide.Lba <<= sector_size_shift;
		pCmd->uCmd.Ide.nSectors <<= sector_size_shift;

		
		switch (cdb[0]) {
		case READ_6:
		case READ_10:
		case READ_16:
			pCmd->flags.data_in = 1;
			break;
		case WRITE_6:
		case WRITE_10:
		case WRITE_16:
			pCmd->flags.data_out = 1;
			break;
		}
		pCmd->priv = ext = cmdext_get(vbus_ext);
		HPT_ASSERT(ext);
		ext->ccb = ccb;
		pCmd->target = vd;
		pCmd->done = os_cmddone;
		pCmd->buildsgl = os_buildsgl;
		pCmd->psg = ext->psg;
		pCmd->flags.physical_sg = 1;
		error = bus_dmamap_load_ccb(vbus_ext->io_dmat, 
					ext->dma_map, ccb, 
					hpt_io_dmamap_callback, pCmd,
				    	BUS_DMA_WAITOK
					);
		KdPrint(("bus_dmamap_load return %d", error));
		if (error && error!=EINPROGRESS) {
			os_printk("bus_dmamap_load error %d", error);
			cmdext_put(ext);
			ldm_free_cmds(pCmd);
			ccb->ccb_h.status = CAM_REQ_CMP_ERR;
			xpt_done(ccb);
		}
		return;
	}

	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	xpt_done(ccb);
	return;
}

static void hpt_action(struct cam_sim *sim, union ccb *ccb)
{
	PVBUS_EXT vbus_ext = (PVBUS_EXT)cam_sim_softc(sim);

	KdPrint(("hpt_action(fn=%d, id=%d)", ccb->ccb_h.func_code, ccb->ccb_h.target_id));

	hpt_assert_vbus_locked(vbus_ext);
	switch (ccb->ccb_h.func_code) {
	
	case XPT_SCSI_IO:
		hpt_scsi_io(vbus_ext, ccb);
		return;

	case XPT_RESET_BUS:
		ldm_reset_vbus((PVBUS)vbus_ext->vbus);
		break;

	case XPT_GET_TRAN_SETTINGS:
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;

	case XPT_CALC_GEOMETRY:
		ccb->ccg.heads = 255;
		ccb->ccg.secs_per_track = 63;
		ccb->ccg.cylinders = ccb->ccg.volume_size / (ccb->ccg.heads * ccb->ccg.secs_per_track);
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;

	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_SDTR_ABLE;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = osm_max_targets;
		cpi->max_lun = 0;
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->initiator_id = osm_max_targets;
		cpi->base_transfer_speed = 3300;

		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "HPT   ", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	xpt_done(ccb);
	return;
}

static void hpt_pci_intr(void *arg)
{	
	PVBUS_EXT vbus_ext = (PVBUS_EXT)arg;
	hpt_lock_vbus(vbus_ext);
	ldm_intr((PVBUS)vbus_ext->vbus);
	hpt_unlock_vbus(vbus_ext);
}

static void hpt_poll(struct cam_sim *sim)
{
	PVBUS_EXT vbus_ext = cam_sim_softc(sim);
	hpt_assert_vbus_locked(vbus_ext);
	ldm_intr((PVBUS)vbus_ext->vbus);
}

static void hpt_async(void * callback_arg, u_int32_t code, struct cam_path * path, void * arg)
{
	KdPrint(("hpt_async"));
}

static int hpt_shutdown(device_t dev)
{
	KdPrint(("hpt_shutdown(dev=%p)", dev));
	return 0;
}

static int hpt_detach(device_t dev)
{
	/* we don't allow the driver to be unloaded. */
	return EBUSY;
}

static void hpt_ioctl_done(struct _IOCTL_ARG *arg)
{
	arg->ioctl_cmnd = 0;
	wakeup(arg);
}

static void __hpt_do_ioctl(PVBUS_EXT vbus_ext, IOCTL_ARG *ioctl_args)
{
	ioctl_args->result = -1;
	ioctl_args->done = hpt_ioctl_done;
	ioctl_args->ioctl_cmnd = (void *)1;

	hpt_lock_vbus(vbus_ext);
	ldm_ioctl((PVBUS)vbus_ext->vbus, ioctl_args);

	while (ioctl_args->ioctl_cmnd) {
		if (hpt_sleep(vbus_ext, ioctl_args, PPAUSE, "hptctl", HPT_OSM_TIMEOUT)==0)
			break;
		ldm_reset_vbus((PVBUS)vbus_ext->vbus);
		__hpt_do_tasks(vbus_ext);
	}

	/* KdPrint(("ioctl %x result %d", ioctl_args->dwIoControlCode, ioctl_args->result)); */

	hpt_unlock_vbus(vbus_ext);
}

static void hpt_do_ioctl(IOCTL_ARG *ioctl_args)
{
	PVBUS vbus;
	PVBUS_EXT vbus_ext;
	
	ldm_for_each_vbus(vbus, vbus_ext) {
		__hpt_do_ioctl(vbus_ext, ioctl_args);
		if (ioctl_args->result!=HPT_IOCTL_RESULT_WRONG_VBUS)
			return;
	}
}

#define HPT_DO_IOCTL(code, inbuf, insize, outbuf, outsize) ({\
	IOCTL_ARG arg;\
	arg.dwIoControlCode = code;\
	arg.lpInBuffer = inbuf;\
	arg.lpOutBuffer = outbuf;\
	arg.nInBufferSize = insize;\
	arg.nOutBufferSize = outsize;\
	arg.lpBytesReturned = 0;\
	hpt_do_ioctl(&arg);\
	arg.result;\
})

#define DEVICEID_VALID(id) ((id) && ((HPT_U32)(id)!=0xffffffff))

static int hpt_get_logical_devices(DEVICEID * pIds, int nMaxCount)
{
	int i;
	HPT_U32 count = nMaxCount-1;
	
	if (HPT_DO_IOCTL(HPT_IOCTL_GET_LOGICAL_DEVICES,
			&count, sizeof(HPT_U32), pIds, sizeof(DEVICEID)*nMaxCount))
		return -1;

	nMaxCount = (int)pIds[0];
	for (i=0; i<nMaxCount; i++) pIds[i] = pIds[i+1];
	return nMaxCount;
}

static int hpt_get_device_info_v3(DEVICEID id, PLOGICAL_DEVICE_INFO_V3 pInfo)
{
	return HPT_DO_IOCTL(HPT_IOCTL_GET_DEVICE_INFO_V3,
				&id, sizeof(DEVICEID), pInfo, sizeof(LOGICAL_DEVICE_INFO_V3));
}

/* not belong to this file logically, but we want to use ioctl interface */
static int __hpt_stop_tasks(PVBUS_EXT vbus_ext, DEVICEID id)
{
	LOGICAL_DEVICE_INFO_V3 devinfo;
	int i, result;
	DEVICEID param[2] = { id, 0 };
	
	if (hpt_get_device_info_v3(id, &devinfo))
		return -1;
		
	if (devinfo.Type!=LDT_ARRAY)
		return -1;
		
	if (devinfo.u.array.Flags & ARRAY_FLAG_REBUILDING)
		param[1] = AS_REBUILD_ABORT;
	else if (devinfo.u.array.Flags & ARRAY_FLAG_VERIFYING)
		param[1] = AS_VERIFY_ABORT;
	else if (devinfo.u.array.Flags & ARRAY_FLAG_INITIALIZING)
		param[1] = AS_INITIALIZE_ABORT;
	else if (devinfo.u.array.Flags & ARRAY_FLAG_TRANSFORMING)
		param[1] = AS_TRANSFORM_ABORT;
	else
		return -1;

	KdPrint(("SET_ARRAY_STATE(%x, %d)", param[0], param[1]));
	result = HPT_DO_IOCTL(HPT_IOCTL_SET_ARRAY_STATE,
				param, sizeof(param), 0, 0);
				
	for (i=0; i<devinfo.u.array.nDisk; i++)
		if (DEVICEID_VALID(devinfo.u.array.Members[i]))
			__hpt_stop_tasks(vbus_ext, devinfo.u.array.Members[i]);
			
	return result;
}

static void hpt_stop_tasks(PVBUS_EXT vbus_ext)
{
	DEVICEID ids[32];
	int i, count;

	count = hpt_get_logical_devices((DEVICEID *)&ids, sizeof(ids)/sizeof(ids[0]));
	
	for (i=0; i<count; i++)
		__hpt_stop_tasks(vbus_ext, ids[i]);
}

static	d_open_t	hpt_open;
static	d_close_t	hpt_close;
static	d_ioctl_t	hpt_ioctl;
static  int 		hpt_rescan_bus(void);

static struct cdevsw hpt_cdevsw = {
	.d_open =	hpt_open,
	.d_close =	hpt_close,
	.d_ioctl =	hpt_ioctl,
	.d_name =	driver_name,
	.d_version =	D_VERSION,
};

static struct intr_config_hook hpt_ich;

/*
 * hpt_final_init will be called after all hpt_attach.
 */
static void hpt_final_init(void *dummy)
{
	int       i,unit_number=0;
	PVBUS_EXT vbus_ext;
	PVBUS vbus;
	PHBA hba;

	/* Clear the config hook */
	config_intrhook_disestablish(&hpt_ich);

	/* allocate memory */
	i = 0;
	ldm_for_each_vbus(vbus, vbus_ext) {
		if (hpt_alloc_mem(vbus_ext)) {
			os_printk("out of memory");
			return;
		}
		i++;
	}

	if (!i) {
		if (bootverbose)
			os_printk("no controller detected.");
		return;
	}

	/* initializing hardware */
	ldm_for_each_vbus(vbus, vbus_ext) {
		/* make timer available here */
		mtx_init(&vbus_ext->lock, "hptsleeplock", NULL, MTX_DEF);
		callout_init_mtx(&vbus_ext->timer, &vbus_ext->lock, 0);
		if (hpt_init_vbus(vbus_ext)) {
			os_printk("fail to initialize hardware");
			break; /* FIXME */
		}
	}

	/* register CAM interface */
	ldm_for_each_vbus(vbus, vbus_ext) {
		struct cam_devq *devq;
		struct ccb_setasync	ccb;
		
		if (bus_dma_tag_create(NULL,/* parent */
				4,	/* alignment */
				BUS_SPACE_MAXADDR_32BIT+1, /* boundary */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR, 	/* highaddr */
				NULL, NULL, 		/* filter, filterarg */
				PAGE_SIZE * (os_max_sg_descriptors-1),	/* maxsize */
				os_max_sg_descriptors,	/* nsegments */
				0x10000,	/* maxsegsize */
				BUS_DMA_WAITOK,		/* flags */
				busdma_lock_mutex,	/* lockfunc */
				&vbus_ext->lock,		/* lockfuncarg */
				&vbus_ext->io_dmat	/* tag */))
		{
			return ;
		}

		for (i=0; i<os_max_queue_comm; i++) {
			POS_CMDEXT ext = (POS_CMDEXT)malloc(sizeof(OS_CMDEXT), M_DEVBUF, M_WAITOK);
			if (!ext) {
				os_printk("Can't alloc cmdext(%d)", i);
				return ;
			}
			ext->vbus_ext = vbus_ext;
			ext->next = vbus_ext->cmdext_list;
			vbus_ext->cmdext_list = ext;
	
			if (bus_dmamap_create(vbus_ext->io_dmat, 0, &ext->dma_map)) {
				os_printk("Can't create dma map(%d)", i);
				return ;
			}
			callout_init_mtx(&ext->timeout, &vbus_ext->lock, 0);
		}

		if ((devq = cam_simq_alloc(os_max_queue_comm)) == NULL) {
			os_printk("cam_simq_alloc failed");
			return ;
		}

		hpt_lock_vbus(vbus_ext);
		vbus_ext->sim = cam_sim_alloc(hpt_action, hpt_poll, driver_name,
				vbus_ext, unit_number, &vbus_ext->lock,
				os_max_queue_comm, /*tagged*/8,  devq);
		unit_number++;
		if (!vbus_ext->sim) {
			os_printk("cam_sim_alloc failed");
			cam_simq_free(devq);
			hpt_unlock_vbus(vbus_ext);
			return ;
		}

		if (xpt_bus_register(vbus_ext->sim, NULL, 0) != CAM_SUCCESS) {
			os_printk("xpt_bus_register failed");
			cam_sim_free(vbus_ext->sim, /*free devq*/ TRUE);
			vbus_ext->sim = NULL;
			return ;
		}
	
		if (xpt_create_path(&vbus_ext->path, /*periph */ NULL,
				cam_sim_path(vbus_ext->sim), CAM_TARGET_WILDCARD,
				CAM_LUN_WILDCARD) != CAM_REQ_CMP)
		{
			os_printk("xpt_create_path failed");
			xpt_bus_deregister(cam_sim_path(vbus_ext->sim));
			cam_sim_free(vbus_ext->sim, /*free_devq*/TRUE);
			hpt_unlock_vbus(vbus_ext);
			vbus_ext->sim = NULL;
			return ;
		}
		hpt_unlock_vbus(vbus_ext);

		xpt_setup_ccb(&ccb.ccb_h, vbus_ext->path, /*priority*/5);
		ccb.ccb_h.func_code = XPT_SASYNC_CB;
		ccb.event_enable = AC_LOST_DEVICE;
		ccb.callback = hpt_async;
		ccb.callback_arg = vbus_ext;
		xpt_action((union ccb *)&ccb);

		for (hba = vbus_ext->hba_list; hba; hba = hba->next) {
			int rid = 0;
			if ((hba->irq_res = bus_alloc_resource_any(hba->pcidev,
				SYS_RES_IRQ, &rid, RF_SHAREABLE | RF_ACTIVE)) == NULL)
			{
				os_printk("can't allocate interrupt");
				return ;
			}
			
			if (bus_setup_intr(hba->pcidev, hba->irq_res, INTR_TYPE_CAM | INTR_MPSAFE,
				NULL, hpt_pci_intr, vbus_ext, &hba->irq_handle)) 
			{
				os_printk("can't set up interrupt");
				return ;
			}
			hba->ldm_adapter.him->intr_control(hba->ldm_adapter.him_handle, HPT_TRUE);

		}

		vbus_ext->shutdown_eh = EVENTHANDLER_REGISTER(shutdown_final, 
									hpt_shutdown_vbus, vbus_ext, SHUTDOWN_PRI_DEFAULT);
		if (!vbus_ext->shutdown_eh)
			os_printk("Shutdown event registration failed");
	}
	
	ldm_for_each_vbus(vbus, vbus_ext) {
		TASK_INIT(&vbus_ext->worker, 0, (task_fn_t *)hpt_do_tasks, vbus_ext);
		if (vbus_ext->tasks)
			TASK_ENQUEUE(&vbus_ext->worker);
	}	

	make_dev(&hpt_cdevsw, DRIVER_MINOR, UID_ROOT, GID_OPERATOR,
	    S_IRUSR | S_IWUSR, "%s", driver_name);
}

#if defined(KLD_MODULE)

typedef struct driverlink *driverlink_t;
struct driverlink {
	kobj_class_t	driver;
	TAILQ_ENTRY(driverlink) link;	/* list of drivers in devclass */
};

typedef TAILQ_HEAD(driver_list, driverlink) driver_list_t;

struct devclass {
	TAILQ_ENTRY(devclass) link;
	devclass_t	parent;		/* parent in devclass hierarchy */
	driver_list_t	drivers;     /* bus devclasses store drivers for bus */
	char		*name;
	device_t	*devices;	/* array of devices indexed by unit */
	int		maxunit;	/* size of devices array */
};

static void override_kernel_driver(void)
{
	driverlink_t dl, dlfirst;
	driver_t *tmpdriver;
	devclass_t dc = devclass_find("pci");
	
	if (dc){
		dlfirst = TAILQ_FIRST(&dc->drivers);
		for (dl = dlfirst; dl; dl = TAILQ_NEXT(dl, link)) {
			if(strcmp(dl->driver->name, driver_name) == 0) {
				tmpdriver=dl->driver;
				dl->driver=dlfirst->driver;
				dlfirst->driver=tmpdriver;
				break;
			}
		}
	}
}

#else 
#define override_kernel_driver()
#endif

static void hpt_init(void *dummy)
{
	if (bootverbose)
		os_printk("%s %s", driver_name_long, driver_ver);

	override_kernel_driver();
	init_config();

	hpt_ich.ich_func = hpt_final_init;
	hpt_ich.ich_arg = NULL;
	if (config_intrhook_establish(&hpt_ich) != 0) {
		printf("%s: cannot establish configuration hook\n",
		    driver_name_long);
	}

}
SYSINIT(hptinit, SI_SUB_CONFIGURE, SI_ORDER_FIRST, hpt_init, NULL);

/*
 * CAM driver interface
 */
static device_method_t driver_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		hpt_probe),
	DEVMETHOD(device_attach,	hpt_attach),
	DEVMETHOD(device_detach,	hpt_detach),
	DEVMETHOD(device_shutdown,	hpt_shutdown),
	{ 0, 0 }
};

static driver_t hpt_pci_driver = {
	driver_name,
	driver_methods,
	sizeof(HBA)
};

static devclass_t	hpt_devclass;

#ifndef TARGETNAME
#error "no TARGETNAME found"
#endif

/* use this to make TARGETNAME be expanded */
#define __DRIVER_MODULE(p1, p2, p3, p4, p5, p6) DRIVER_MODULE(p1, p2, p3, p4, p5, p6)
#define __MODULE_VERSION(p1, p2) MODULE_VERSION(p1, p2)
#define __MODULE_DEPEND(p1, p2, p3, p4, p5) MODULE_DEPEND(p1, p2, p3, p4, p5)
__DRIVER_MODULE(TARGETNAME, pci, hpt_pci_driver, hpt_devclass, 0, 0);
__MODULE_VERSION(TARGETNAME, 1);
__MODULE_DEPEND(TARGETNAME, cam, 1, 1, 1);

static int hpt_open(struct cdev *dev, int flags, int devtype, struct thread *td)
{
	return 0;
}

static int hpt_close(struct cdev *dev, int flags, int devtype, struct thread *td)
{
	return 0;
}

static int hpt_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td)
{
	PHPT_IOCTL_PARAM piop=(PHPT_IOCTL_PARAM)data;
	IOCTL_ARG ioctl_args;
	HPT_U32 bytesReturned = 0;

	switch (cmd){
	case HPT_DO_IOCONTROL:
	{	
		if (piop->Magic == HPT_IOCTL_MAGIC || piop->Magic == HPT_IOCTL_MAGIC32) {
			KdPrint(("ioctl=%x in=%p len=%d out=%p len=%d\n",
				piop->dwIoControlCode,
				piop->lpInBuffer,
				piop->nInBufferSize,
				piop->lpOutBuffer,
				piop->nOutBufferSize));
			
		memset(&ioctl_args, 0, sizeof(ioctl_args));
		
		ioctl_args.dwIoControlCode = piop->dwIoControlCode;
		ioctl_args.nInBufferSize = piop->nInBufferSize;
		ioctl_args.nOutBufferSize = piop->nOutBufferSize;
		ioctl_args.lpBytesReturned = &bytesReturned;

		if (ioctl_args.nInBufferSize) {
			ioctl_args.lpInBuffer = malloc(ioctl_args.nInBufferSize, M_DEVBUF, M_WAITOK);
			if (!ioctl_args.lpInBuffer)
				goto invalid;
			if (copyin((void*)piop->lpInBuffer,
					ioctl_args.lpInBuffer, piop->nInBufferSize))
				goto invalid;
		}
	
		if (ioctl_args.nOutBufferSize) {
			ioctl_args.lpOutBuffer = malloc(ioctl_args.nOutBufferSize, M_DEVBUF, M_WAITOK | M_ZERO);
			if (!ioctl_args.lpOutBuffer)
				goto invalid;
		}
		
		hpt_do_ioctl(&ioctl_args);
	
		if (ioctl_args.result==HPT_IOCTL_RESULT_OK) {
			if (piop->nOutBufferSize) {
				if (copyout(ioctl_args.lpOutBuffer,
					(void*)piop->lpOutBuffer, piop->nOutBufferSize))
					goto invalid;
			}
			if (piop->lpBytesReturned) {
				if (copyout(&bytesReturned,
					(void*)piop->lpBytesReturned, sizeof(HPT_U32)))
					goto invalid;
			}
			if (ioctl_args.lpInBuffer) free(ioctl_args.lpInBuffer, M_DEVBUF);
			if (ioctl_args.lpOutBuffer) free(ioctl_args.lpOutBuffer, M_DEVBUF);
			return 0;
		}
invalid:
		if (ioctl_args.lpInBuffer) free(ioctl_args.lpInBuffer, M_DEVBUF);
		if (ioctl_args.lpOutBuffer) free(ioctl_args.lpOutBuffer, M_DEVBUF);
		return EFAULT;
	}
	return EFAULT;
	}

	case HPT_SCAN_BUS:
	{
		return hpt_rescan_bus();
	}
	default:
		KdPrint(("invalid command!"));
		return EFAULT;
	}	

}

static int	hpt_rescan_bus(void)
{
	union ccb			*ccb;
	PVBUS 				vbus;
	PVBUS_EXT			vbus_ext;	
		
	ldm_for_each_vbus(vbus, vbus_ext) {
		if ((ccb = xpt_alloc_ccb()) == NULL)
		{
			return(ENOMEM);
		}
		if (xpt_create_path(&ccb->ccb_h.path, NULL, cam_sim_path(vbus_ext->sim),
			CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP)	
		{
			xpt_free_ccb(ccb);
			return(EIO);
		}
		xpt_rescan(ccb);
	}
	return(0);	
}
