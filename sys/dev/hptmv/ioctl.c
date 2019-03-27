/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 HighPoint Technologies, Inc.
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
/*
 * ioctl.c   ioctl interface implementation
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <dev/hptmv/global.h>
#include <dev/hptmv/hptintf.h>
#include <dev/hptmv/osbsd.h>
#include <dev/hptmv/access601.h>

#pragma pack(1)

typedef struct _HPT_REBUILD_PARAM
{
	DEVICEID idMirror;
	DWORD Lba;
	UCHAR nSector;
} HPT_REBUILD_PARAM, *PHPT_REBUILD_PARAM;

#pragma pack()

#define MAX_EVENTS 10
static HPT_EVENT hpt_event_queue[MAX_EVENTS];
static int event_queue_head=0, event_queue_tail=0;

static int hpt_get_event(PHPT_EVENT pEvent);
static int hpt_set_array_state(DEVICEID idArray, DWORD state);
static void lock_driver_idle(IAL_ADAPTER_T *pAdapter);
static void HPTLIBAPI thread_io_done(_VBUS_ARG PCommand pCmd);
static int HPTLIBAPI R1ControlSgl(_VBUS_ARG PCommand pCmd,
    FPSCAT_GATH pSgTable, int logical);

static void
get_disk_location(PDevice pDev, int *controller, int *channel)
{
	IAL_ADAPTER_T *pAdapTemp;
	int i, j;

	*controller = *channel = 0;

	for (i=1, pAdapTemp = gIal_Adapter; pAdapTemp; pAdapTemp = pAdapTemp->next, i++) {
		for (j=0; j<MV_SATA_CHANNELS_NUM; j++) {
			if (pDev == &pAdapTemp->VDevices[j].u.disk) {
				*controller = i;
				*channel = j;
				return;
			}
		}
	}
}

static int
event_queue_add(PHPT_EVENT pEvent)
{
	int p;
	p = (event_queue_tail + 1) % MAX_EVENTS;
	if (p==event_queue_head)
	{
		return -1;
	}
	hpt_event_queue[event_queue_tail] = *pEvent;
	event_queue_tail = p;
	return 0;
}

static int
event_queue_remove(PHPT_EVENT pEvent)
{
	if (event_queue_head != event_queue_tail)
	{
		*pEvent = hpt_event_queue[event_queue_head];
		event_queue_head++;
		event_queue_head %= MAX_EVENTS;
		return 0;
	}
	return -1;
}

void HPTLIBAPI
ioctl_ReportEvent(UCHAR event, PVOID param)
{
	HPT_EVENT e;
	ZeroMemory(&e, sizeof(e));
	e.EventType = event;
	switch(event)
	{
		case ET_INITIALIZE_ABORTED:
		case ET_INITIALIZE_FAILED:
			memcpy(e.Data, ((PVDevice)param)->u.array.ArrayName, MAX_ARRAY_NAME);
		case ET_INITIALIZE_STARTED:
		case ET_INITIALIZE_FINISHED:

		case ET_REBUILD_STARTED:
		case ET_REBUILD_ABORTED:
		case ET_REBUILD_FAILED:
		case ET_REBUILD_FINISHED:
		
		case ET_VERIFY_STARTED:
		case ET_VERIFY_ABORTED:
		case ET_VERIFY_FAILED:
		case ET_VERIFY_FINISHED:
		case ET_VERIFY_DATA_ERROR:

		case ET_SPARE_TOOK_OVER:
		case ET_DEVICE_REMOVED:
		case ET_DEVICE_PLUGGED:
		case ET_DEVICE_ERROR:
			e.DeviceID = VDEV_TO_ID((PVDevice)param);
			break;

		default:
			break;
	}
	event_queue_add(&e);
	if (event==ET_DEVICE_REMOVED) {
		int controller, channel;
		get_disk_location(&((PVDevice)param)->u.disk, &controller, &channel);
		hpt_printk(("Device removed: controller %d channel %d\n", controller, channel));
	}
	wakeup(param);
}

static int
hpt_delete_array(_VBUS_ARG DEVICEID id, DWORD options)
{
	PVDevice	pArray = ID_TO_VDEV(id);
	BOOLEAN	del_block0 = (options & DAF_KEEP_DATA_IF_POSSIBLE)?0:1;
	int i;
	PVDevice pa;

	if ((id==0) || check_VDevice_valid(pArray))
		return -1;

	if(!mIsArray(pArray)) return -1;
	
	if (pArray->u.array.rf_rebuilding || pArray->u.array.rf_verifying ||
		pArray->u.array.rf_initializing)
		return -1;

	for(i=0; i<pArray->u.array.bArnMember; i++) {
		pa = pArray->u.array.pMember[i];
		if (pa && mIsArray(pa)) {
			if (pa->u.array.rf_rebuilding || pa->u.array.rf_verifying || 
				pa->u.array.rf_initializing)
				return -1;
		}
	}

	if (pArray->pVBus!=_vbus_p) { HPT_ASSERT(0); return -1;}
	fDeleteArray(_VBUS_P pArray, del_block0);
	return 0;

}

/* just to prevent driver from sending more commands */
static void HPTLIBAPI nothing(_VBUS_ARG void *notused){}

void
lock_driver_idle(IAL_ADAPTER_T *pAdapter)
{
	_VBUS_INST(&pAdapter->VBus)
	mtx_lock(&pAdapter->lock);
	while (pAdapter->outstandingCommands) {
		KdPrint(("outstandingCommands is %d, wait..\n", pAdapter->outstandingCommands));
		if (!mWaitingForIdle(_VBUS_P0)) CallWhenIdle(_VBUS_P nothing, 0);
		mtx_sleep(pAdapter, &pAdapter->lock, 0, "hptidle", 0);
	}
	CheckIdleCall(_VBUS_P0);
}

int Kernel_DeviceIoControl(_VBUS_ARG
							DWORD dwIoControlCode,       	/* operation control code */
							PVOID lpInBuffer,            	/* input data buffer */
							DWORD nInBufferSize,         	/* size of input data buffer */
							PVOID lpOutBuffer,           	/* output data buffer */
							DWORD nOutBufferSize,        	/* size of output data buffer */
							PDWORD lpBytesReturned      	/* byte count */
						)
{
	IAL_ADAPTER_T *pAdapter;

	switch(dwIoControlCode)	{
		case HPT_IOCTL_DELETE_ARRAY:
		{
			DEVICEID idArray;
			int iSuccess;			
	        int i;
			PVDevice pArray;
			PVBus _vbus_p;
			struct cam_periph *periph = NULL;

			if (nInBufferSize!=sizeof(DEVICEID)+sizeof(DWORD)) return -1;
			if (nOutBufferSize!=sizeof(int)) return -1;
			idArray = *(DEVICEID *)lpInBuffer;

			pArray = ID_TO_VDEV(idArray);

			if((idArray == 0) || check_VDevice_valid(pArray))	
		       	return -1;
		
        	if(!mIsArray(pArray))
			return -1;

			_vbus_p=pArray->pVBus;
			pAdapter = (IAL_ADAPTER_T *)_vbus_p->OsExt;

	        for(i = 0; i < MAX_VDEVICE_PER_VBUS; i++) {
				if(pArray == _vbus_p->pVDevice[i])
				{
					periph = hpt_get_periph(pAdapter->mvSataAdapter.adapterId, i);
					if (periph != NULL && periph->refcount >= 1)
					{
						hpt_printk(("Can not delete a mounted device.\n"));
	                    return -1;	
					}
				}
				/* the Mounted Disk isn't delete */
			} 

			iSuccess = hpt_delete_array(_VBUS_P idArray, *(DWORD*)((DEVICEID *)lpInBuffer+1));

			*(int*)lpOutBuffer = iSuccess;

			if(iSuccess != 0)
				return -1;
			break;
		}

		case HPT_IOCTL_GET_EVENT:
		{
			PHPT_EVENT pInfo;

			if (nInBufferSize!=0) return -1;
			if (nOutBufferSize!=sizeof(HPT_EVENT)) return -1;

			pInfo = (PHPT_EVENT)lpOutBuffer;

			if (hpt_get_event(pInfo)!=0)
				return -1;
		}
		break;

		case HPT_IOCTL_SET_ARRAY_STATE:
		{
			DEVICEID idArray;
			DWORD state;

			if (nInBufferSize!=sizeof(HPT_SET_STATE_PARAM)) return -1;
			if (nOutBufferSize!=0) return -1;

			idArray = ((PHPT_SET_STATE_PARAM)lpInBuffer)->idArray;
			state = ((PHPT_SET_STATE_PARAM)lpInBuffer)->state;

			if(hpt_set_array_state(idArray, state)!=0)
				return -1;
		}
		break;

		case HPT_IOCTL_RESCAN_DEVICES:
		{
			if (nInBufferSize!=0) return -1;
			if (nOutBufferSize!=0) return -1;

#ifndef FOR_DEMO
			/* stop buzzer if user perform rescan */
			for (pAdapter=gIal_Adapter; pAdapter; pAdapter=pAdapter->next) {
				if (pAdapter->beeping) {
					pAdapter->beeping = 0;
					BeepOff(pAdapter->mvSataAdapter.adapterIoBaseAddress);
				}
			}
#endif
		}
		break;

		default:
		{
			PVDevice pVDev;

			switch(dwIoControlCode) {
			/* read-only ioctl functions can be called directly. */
			case HPT_IOCTL_GET_VERSION:
			case HPT_IOCTL_GET_CONTROLLER_IDS:
			case HPT_IOCTL_GET_CONTROLLER_COUNT:
			case HPT_IOCTL_GET_CONTROLLER_INFO:
			case HPT_IOCTL_GET_CHANNEL_INFO:
			case HPT_IOCTL_GET_LOGICAL_DEVICES:
			case HPT_IOCTL_GET_DEVICE_INFO:
			case HPT_IOCTL_GET_DEVICE_INFO_V2:				
			case HPT_IOCTL_GET_EVENT:
			case HPT_IOCTL_GET_DRIVER_CAPABILITIES:
				if(hpt_default_ioctl(_VBUS_P dwIoControlCode, lpInBuffer, nInBufferSize, 
					lpOutBuffer, nOutBufferSize, lpBytesReturned) == -1) return -1;
				break;

			default:
				/* 
				 * GUI always use /proc/scsi/hptmv/0, so the _vbus_p param will be 
				 * wrong for second controller. 
				 */
				switch(dwIoControlCode) {
				case HPT_IOCTL_CREATE_ARRAY:
					pVDev = ID_TO_VDEV(((PCREATE_ARRAY_PARAMS)lpInBuffer)->Members[0]); break;
				case HPT_IOCTL_CREATE_ARRAY_V2:
					pVDev = ID_TO_VDEV(((PCREATE_ARRAY_PARAMS_V2)lpInBuffer)->Members[0]); break;
				case HPT_IOCTL_SET_ARRAY_INFO:
					pVDev = ID_TO_VDEV(((PHPT_SET_ARRAY_INFO)lpInBuffer)->idArray); break;
				case HPT_IOCTL_SET_DEVICE_INFO:
					pVDev = ID_TO_VDEV(((PHPT_SET_DEVICE_INFO)lpInBuffer)->idDisk); break;
				case HPT_IOCTL_SET_DEVICE_INFO_V2:
					pVDev = ID_TO_VDEV(((PHPT_SET_DEVICE_INFO_V2)lpInBuffer)->idDisk); break;
				case HPT_IOCTL_SET_BOOT_MARK:
				case HPT_IOCTL_ADD_SPARE_DISK:
				case HPT_IOCTL_REMOVE_SPARE_DISK:
					pVDev = ID_TO_VDEV(*(DEVICEID *)lpInBuffer); break;
				case HPT_IOCTL_ADD_DISK_TO_ARRAY:
					pVDev = ID_TO_VDEV(((PHPT_ADD_DISK_TO_ARRAY)lpInBuffer)->idArray); break;
				default:
					pVDev = 0;
				}
				
				if (pVDev && !check_VDevice_valid(pVDev)){
					_vbus_p = pVDev->pVBus;

					pAdapter = (IAL_ADAPTER_T *)_vbus_p->OsExt;
					/* 
					 * create_array, and other functions can't be executed while channel is 
					 * perform I/O commands. Wait until driver is idle.
					 */
					lock_driver_idle(pAdapter);
					if (hpt_default_ioctl(_VBUS_P dwIoControlCode, lpInBuffer, nInBufferSize, 
						lpOutBuffer, nOutBufferSize, lpBytesReturned) == -1) {
						mtx_unlock(&pAdapter->lock);
						return -1;
					}
					mtx_unlock(&pAdapter->lock);
				}
				else
					return -1;
				break;
			}
			
#ifdef SUPPORT_ARRAY
			switch(dwIoControlCode)
			{
				case HPT_IOCTL_CREATE_ARRAY:
				{
					pAdapter=(IAL_ADAPTER_T *)(ID_TO_VDEV(*(DEVICEID *)lpOutBuffer))->pVBus->OsExt;
					mtx_lock(&pAdapter->lock);
                    if(((PCREATE_ARRAY_PARAMS)lpInBuffer)->CreateFlags & CAF_CREATE_AND_DUPLICATE)
				    {
						  (ID_TO_VDEV(*(DEVICEID *)lpOutBuffer))->u.array.rf_auto_rebuild = 0;
                          hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, ID_TO_VDEV(*(DEVICEID *)lpOutBuffer), DUPLICATE);
					}
					else if(((PCREATE_ARRAY_PARAMS)lpInBuffer)->CreateFlags & CAF_CREATE_R5_ZERO_INIT)
				    {
                          hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, ID_TO_VDEV(*(DEVICEID *)lpOutBuffer), INITIALIZE);
					}
					else if(((PCREATE_ARRAY_PARAMS)lpInBuffer)->CreateFlags & CAF_CREATE_R5_BUILD_PARITY)
				    {
                          hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, ID_TO_VDEV(*(DEVICEID *)lpOutBuffer), REBUILD_PARITY);
					}
					mtx_unlock(&pAdapter->lock);
                    break;
				}


				case HPT_IOCTL_CREATE_ARRAY_V2:
				{
					pAdapter=(IAL_ADAPTER_T *)(ID_TO_VDEV(*(DEVICEID *)lpOutBuffer))->pVBus->OsExt;
					mtx_lock(&pAdapter->lock);
				             if(((PCREATE_ARRAY_PARAMS_V2)lpInBuffer)->CreateFlags & CAF_CREATE_AND_DUPLICATE) {
						  (ID_TO_VDEV(*(DEVICEID *)lpOutBuffer))->u.array.rf_auto_rebuild = 0;
				                          hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, ID_TO_VDEV(*(DEVICEID *)lpOutBuffer), DUPLICATE);
					} else if(((PCREATE_ARRAY_PARAMS_V2)lpInBuffer)->CreateFlags & CAF_CREATE_R5_ZERO_INIT) {
				                          hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, ID_TO_VDEV(*(DEVICEID *)lpOutBuffer), INITIALIZE);
					} else if(((PCREATE_ARRAY_PARAMS_V2)lpInBuffer)->CreateFlags & CAF_CREATE_R5_BUILD_PARITY) {
				                          hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, ID_TO_VDEV(*(DEVICEID *)lpOutBuffer), REBUILD_PARITY);
					}
					mtx_unlock(&pAdapter->lock);
					break;
				}
				case HPT_IOCTL_ADD_DISK_TO_ARRAY:
				{
					PVDevice pArray = ID_TO_VDEV(((PHPT_ADD_DISK_TO_ARRAY)lpInBuffer)->idArray);
					pAdapter=(IAL_ADAPTER_T *)pArray->pVBus->OsExt;
					if(pArray->u.array.rf_rebuilding == 0)
					{		
						mtx_lock(&pAdapter->lock);
						pArray->u.array.rf_auto_rebuild = 0;
						pArray->u.array.rf_abort_rebuild = 0;
						hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pArray, DUPLICATE);
						while (!pArray->u.array.rf_rebuilding)
						{
							if (mtx_sleep(pArray, &pAdapter->lock, 0, "hptwait", hz * 3) != 0)
								break;
						}
						mtx_unlock(&pAdapter->lock);
					}
					break;
				}
			}
#endif
            return 0;
		}
	}

	if (lpBytesReturned)
		*lpBytesReturned = nOutBufferSize;
	return 0;
}

static int
hpt_get_event(PHPT_EVENT pEvent)
{
	int ret = event_queue_remove(pEvent);
	return ret;
}

static int
hpt_set_array_state(DEVICEID idArray, DWORD state)
{
	IAL_ADAPTER_T *pAdapter;
	PVDevice pVDevice = ID_TO_VDEV(idArray);
	int	i;

	if(idArray == 0 || check_VDevice_valid(pVDevice))	return -1;
	if(!mIsArray(pVDevice))
		return -1;
	if(!pVDevice->vf_online || pVDevice->u.array.rf_broken) return -1;

	pAdapter=(IAL_ADAPTER_T *)pVDevice->pVBus->OsExt;
	
	switch(state)
	{
		case MIRROR_REBUILD_START:
		{
			mtx_lock(&pAdapter->lock);
			if (pVDevice->u.array.rf_rebuilding ||
				pVDevice->u.array.rf_verifying ||
				pVDevice->u.array.rf_initializing) {
				mtx_unlock(&pAdapter->lock);
				return -1;
			}
			
			pVDevice->u.array.rf_auto_rebuild = 0;
			pVDevice->u.array.rf_abort_rebuild = 0;

			hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pVDevice, 
				(UCHAR)((pVDevice->u.array.CriticalMembers || pVDevice->VDeviceType == VD_RAID_1)? DUPLICATE : REBUILD_PARITY));

			while (!pVDevice->u.array.rf_rebuilding)
			{
				if (mtx_sleep(pVDevice, &pAdapter->lock, 0,
				    "hptwait", hz * 20) != 0)
					break;
			}
			mtx_unlock(&pAdapter->lock);
		}

		break;

		case MIRROR_REBUILD_ABORT:
		{
			for(i = 0; i < pVDevice->u.array.bArnMember; i++) {
				if(pVDevice->u.array.pMember[i] != 0 && pVDevice->u.array.pMember[i]->VDeviceType == VD_RAID_1)
					hpt_set_array_state(VDEV_TO_ID(pVDevice->u.array.pMember[i]), state);
			}

			mtx_lock(&pAdapter->lock);
			if(pVDevice->u.array.rf_rebuilding != 1) {
				mtx_unlock(&pAdapter->lock);
				return -1;
			}
			
			pVDevice->u.array.rf_abort_rebuild = 1;
			
			while (pVDevice->u.array.rf_abort_rebuild)
			{
				if (mtx_sleep(pVDevice, &pAdapter->lock, 0,
				    "hptabrt", hz * 20) != 0)
					break;
			}
			mtx_unlock(&pAdapter->lock);
		}
		break;

		case AS_VERIFY_START:
		{
			/*if(pVDevice->u.array.rf_verifying)
				return -1;*/
			mtx_lock(&pAdapter->lock);
			if (pVDevice->u.array.rf_rebuilding ||
				pVDevice->u.array.rf_verifying ||
				pVDevice->u.array.rf_initializing) {
				mtx_unlock(&pAdapter->lock);
				return -1;
			}

            pVDevice->u.array.RebuildSectors = 0;
			hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pVDevice, VERIFY);
			
			while (!pVDevice->u.array.rf_verifying)
			{
				if (mtx_sleep(pVDevice, &pAdapter->lock, 0,
				    "hptvrfy", hz * 20) != 0)
					break;
			}
			mtx_unlock(&pAdapter->lock);
		}
		break;

		case AS_VERIFY_ABORT:
		{
			mtx_lock(&pAdapter->lock);
			if(pVDevice->u.array.rf_verifying != 1) {
				mtx_unlock(&pAdapter->lock);
				return -1;
			}
				
			pVDevice->u.array.rf_abort_rebuild = 1;
			
			while (pVDevice->u.array.rf_abort_rebuild)
			{
				if (mtx_sleep(pVDevice, &pAdapter->lock, 0,
				    "hptvrfy", hz * 80) != 0)
					break;
			}
			mtx_unlock(&pAdapter->lock);
		}
		break;

		case AS_INITIALIZE_START:
		{
			mtx_lock(&pAdapter->lock);
			if (pVDevice->u.array.rf_rebuilding ||
				pVDevice->u.array.rf_verifying ||
				pVDevice->u.array.rf_initializing) {
				mtx_unlock(&pAdapter->lock);
				return -1;
			}

			hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pVDevice, VERIFY);
			
			while (!pVDevice->u.array.rf_initializing)
			{
				if (mtx_sleep(pVDevice, &pAdapter->lock, 0,
				    "hptinit", hz * 80) != 0)
					break;
			}
			mtx_unlock(&pAdapter->lock);
		}
		break;

		case AS_INITIALIZE_ABORT:
		{
			mtx_lock(&pAdapter->lock);
			if(pVDevice->u.array.rf_initializing != 1) {
				mtx_unlock(&pAdapter->lock);
				return -1;
			}
				
			pVDevice->u.array.rf_abort_rebuild = 1;
			
			while (pVDevice->u.array.rf_abort_rebuild)
			{
				if (mtx_sleep(pVDevice, &pAdapter->lock, 0,
				    "hptinit", hz * 80) != 0)
					break;
			}
			mtx_unlock(&pAdapter->lock);
		}
		break;

		default:
			return -1;
	}

	return 0;
}

int HPTLIBAPI
R1ControlSgl(_VBUS_ARG PCommand pCmd, FPSCAT_GATH pSgTable, int logical)
{
	ULONG bufferSize = SECTOR_TO_BYTE(pCmd->uCmd.R1Control.nSectors);
	if (pCmd->uCmd.R1Control.Command==CTRL_CMD_VERIFY)
		bufferSize<<=1;
	if (logical) {
		pSgTable->dSgAddress = (ULONG_PTR)pCmd->uCmd.R1Control.Buffer;
		pSgTable->wSgSize = (USHORT)bufferSize;
		pSgTable->wSgFlag = SG_FLAG_EOT;
	}
	else {
		/* build physical SG table for pCmd->uCmd.R1Control.Buffer */
		ADDRESS dataPointer, v, nextpage, currvaddr, nextvaddr, currphypage, nextphypage;
		ULONG length;
		int idx = 0;

		v = pCmd->uCmd.R1Control.Buffer;
		dataPointer = (ADDRESS)fOsPhysicalAddress(v);

		if ((ULONG_PTR)dataPointer & 0x1)
			return FALSE;

		#define ON64KBOUNDARY(x) (((ULONG_PTR)(x) & 0xFFFF) == 0)
		#define NOTNEIGHBORPAGE(highvaddr, lowvaddr) ((ULONG_PTR)(highvaddr) - (ULONG_PTR)(lowvaddr) != PAGE_SIZE)

		do {
			if (idx >= MAX_SG_DESCRIPTORS)  return FALSE;

			pSgTable[idx].dSgAddress = fOsPhysicalAddress(v);
			currvaddr = v;
			currphypage = (ADDRESS)fOsPhysicalAddress((void*)trunc_page((ULONG_PTR)currvaddr));


			do {
				nextpage = (ADDRESS)trunc_page(((ULONG_PTR)currvaddr + PAGE_SIZE));
				nextvaddr = (ADDRESS)MIN(((ULONG_PTR)v + bufferSize), (ULONG_PTR)(nextpage));

				if (nextvaddr == (ADDRESS)((ULONG_PTR)v + bufferSize)) break;
				nextphypage = (ADDRESS)fOsPhysicalAddress(nextpage);

				if (NOTNEIGHBORPAGE(nextphypage, currphypage) || ON64KBOUNDARY(nextphypage)) {
					nextvaddr = nextpage;
					break;
				}

				currvaddr = nextvaddr;
				currphypage = nextphypage;
			}while (1);

			length = (ULONG_PTR)nextvaddr - (ULONG_PTR)v;
			v = nextvaddr;
			bufferSize -= length;

			pSgTable[idx].wSgSize = (USHORT)length;
			pSgTable[idx].wSgFlag = (bufferSize)? 0 : SG_FLAG_EOT;
			idx++;

		}while (bufferSize);
	}
	return 1;
}

static int End_Job=0;
void HPTLIBAPI
thread_io_done(_VBUS_ARG PCommand pCmd)
{
	End_Job = 1;
	wakeup((caddr_t)pCmd);
}

void
hpt_rebuild_data_block(IAL_ADAPTER_T *pAdapter, PVDevice pArray, UCHAR flags)
{
    ULONG capacity = pArray->VDeviceCapacity / (pArray->u.array.bArnMember-1);
    PCommand pCmd;
	UINT result;
	int needsync=0, retry=0, needdelete=0;
	void *buffer = NULL;

	_VBUS_INST(&pAdapter->VBus)

	if (pArray->u.array.rf_broken==1 ||
    	pArray->u.array.RebuildSectors>=capacity)
		return;

	mtx_lock(&pAdapter->lock);
	
	switch(flags)
	{
		case DUPLICATE:
		case REBUILD_PARITY:
			if(pArray->u.array.rf_rebuilding == 0)
			{
				pArray->u.array.rf_rebuilding = 1;
				hpt_printk(("Rebuilding started.\n"));
				ioctl_ReportEvent(ET_REBUILD_STARTED, pArray);
			}
			break;

		case INITIALIZE:
			if(pArray->u.array.rf_initializing == 0)
			{
				pArray->u.array.rf_initializing = 1;
				hpt_printk(("Initializing started.\n"));
				ioctl_ReportEvent(ET_INITIALIZE_STARTED, pArray);
			}
			break;

		case VERIFY:
			if(pArray->u.array.rf_verifying == 0)
			{
				pArray->u.array.rf_verifying = 1;
				hpt_printk(("Verifying started.\n"));
				ioctl_ReportEvent(ET_VERIFY_STARTED, pArray);
			}
			break;
	}
	
retry_cmd:
	pCmd = AllocateCommand(_VBUS_P0);
	HPT_ASSERT(pCmd);
	pCmd->cf_control = 1;
	End_Job = 0;

	if (pArray->VDeviceType==VD_RAID_1) 
	{
		#define MAX_REBUILD_SECTORS 0x40

		/* take care for discontinuous buffer in R1ControlSgl */
		buffer = malloc(SECTOR_TO_BYTE(MAX_REBUILD_SECTORS), M_DEVBUF, M_NOWAIT);
		if(!buffer) {
			FreeCommand(_VBUS_P pCmd);
			hpt_printk(("can't allocate rebuild buffer\n"));
			goto fail;
		}
		switch(flags) 
		{
			case DUPLICATE:
				pCmd->uCmd.R1Control.Command = CTRL_CMD_REBUILD;
				pCmd->uCmd.R1Control.nSectors = MAX_REBUILD_SECTORS;
				break;

			case VERIFY:
				pCmd->uCmd.R1Control.Command = CTRL_CMD_VERIFY;
				pCmd->uCmd.R1Control.nSectors = MAX_REBUILD_SECTORS/2;
				break;

			case INITIALIZE:
				pCmd->uCmd.R1Control.Command = CTRL_CMD_REBUILD; 
				pCmd->uCmd.R1Control.nSectors = MAX_REBUILD_SECTORS;
				break;
		}

		pCmd->uCmd.R1Control.Lba = pArray->u.array.RebuildSectors;

		if (capacity - pArray->u.array.RebuildSectors < pCmd->uCmd.R1Control.nSectors)
			pCmd->uCmd.R1Control.nSectors = capacity - pArray->u.array.RebuildSectors;

		pCmd->uCmd.R1Control.Buffer = buffer;
		pCmd->pfnBuildSgl = R1ControlSgl;
	}
	else if (pArray->VDeviceType==VD_RAID_5)
	{
		switch(flags)
		{
			case DUPLICATE:
			case REBUILD_PARITY:
				pCmd->uCmd.R5Control.Command = CTRL_CMD_REBUILD; break;
			case VERIFY:
				pCmd->uCmd.R5Control.Command = CTRL_CMD_VERIFY; break;
			case INITIALIZE:
				pCmd->uCmd.R5Control.Command = CTRL_CMD_INIT; break;
		}
		pCmd->uCmd.R5Control.StripeLine=pArray->u.array.RebuildSectors>>pArray->u.array.bArBlockSizeShift;
	}
	else
		HPT_ASSERT(0);

	pCmd->pVDevice = pArray;
	pCmd->pfnCompletion = thread_io_done;
	pArray->pfnSendCommand(_VBUS_P pCmd);
	CheckPendingCall(_VBUS_P0);

	if (!End_Job) {
		mtx_sleep(pCmd, &pAdapter->lock, 0, "hptrbld", hz * 60);
		if (!End_Job) {
			hpt_printk(("timeout, reset\n"));
			fResetVBus(_VBUS_P0);
		}
	}

	result = pCmd->Result;
	FreeCommand(_VBUS_P pCmd);
	if (buffer) free(buffer, M_DEVBUF);
	KdPrintI(("cmd finished %d", result));

	switch(result)
	{
		case RETURN_SUCCESS:
			if (!pArray->u.array.rf_abort_rebuild)
			{
				if(pArray->u.array.RebuildSectors < capacity)
				{
					hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pArray, flags);
				}
				else
				{
					switch (flags)
					{
						case DUPLICATE:
						case REBUILD_PARITY:
							needsync = 1;
							pArray->u.array.rf_rebuilding = 0;
							pArray->u.array.rf_need_rebuild = 0;
							pArray->u.array.CriticalMembers = 0;
							pArray->u.array.RebuildSectors = MAX_LBA_T;
							pArray->u.array.rf_duplicate_and_create = 0;
							hpt_printk(("Rebuilding finished.\n"));
							ioctl_ReportEvent(ET_REBUILD_FINISHED, pArray);
							break;
						case INITIALIZE:
							needsync = 1;
							pArray->u.array.rf_initializing = 0;
							pArray->u.array.rf_need_rebuild = 0;
							pArray->u.array.RebuildSectors = MAX_LBA_T;
							hpt_printk(("Initializing finished.\n"));
							ioctl_ReportEvent(ET_INITIALIZE_FINISHED, pArray);
							break;
						case VERIFY:
							pArray->u.array.rf_verifying = 0;
							hpt_printk(("Verifying finished.\n"));
							ioctl_ReportEvent(ET_VERIFY_FINISHED, pArray);
							break;
					}
				}
			}
			else
			{
				pArray->u.array.rf_abort_rebuild = 0;
				if (pArray->u.array.rf_rebuilding) 
				{
					hpt_printk(("Abort rebuilding.\n"));
					pArray->u.array.rf_rebuilding = 0;
					pArray->u.array.rf_duplicate_and_create = 0;
					ioctl_ReportEvent(ET_REBUILD_ABORTED, pArray);
				}
				else if (pArray->u.array.rf_verifying) 
				{
					hpt_printk(("Abort verifying.\n"));
					pArray->u.array.rf_verifying = 0;
					ioctl_ReportEvent(ET_VERIFY_ABORTED, pArray);
				}
				else if (pArray->u.array.rf_initializing) 
				{
					hpt_printk(("Abort initializing.\n"));
					pArray->u.array.rf_initializing = 0;
					ioctl_ReportEvent(ET_INITIALIZE_ABORTED, pArray);
				}
				needdelete=1;
			}
			break;

		case RETURN_DATA_ERROR:
			if (flags==VERIFY)
			{
				needsync = 1;
				pArray->u.array.rf_verifying = 0;
				pArray->u.array.rf_need_rebuild = 1;
				hpt_printk(("Verifying failed: found inconsistency\n"));
				ioctl_ReportEvent(ET_VERIFY_DATA_ERROR, pArray);
				ioctl_ReportEvent(ET_VERIFY_FAILED, pArray);

				if (!pArray->vf_online || pArray->u.array.rf_broken) break;

				pArray->u.array.rf_auto_rebuild = 0;
				pArray->u.array.rf_abort_rebuild = 0;
				hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pArray,
					(pArray->VDeviceType == VD_RAID_1) ? DUPLICATE : REBUILD_PARITY);
			}
			break;

		default:
			hpt_printk(("command failed with error %d\n", result));
			if (++retry<3)
			{
				hpt_printk(("retry (%d)\n", retry));
				goto retry_cmd;
			}
fail:
			pArray->u.array.rf_abort_rebuild = 0;
			switch (flags)
			{
				case DUPLICATE:
				case REBUILD_PARITY:
					needsync = 1;
					pArray->u.array.rf_rebuilding = 0;
					pArray->u.array.rf_duplicate_and_create = 0;
					hpt_printk(((flags==DUPLICATE)? "Duplicating failed.\n":"Rebuilding failed.\n"));
					ioctl_ReportEvent(ET_REBUILD_FAILED, pArray);
					break;

				case INITIALIZE:
					needsync = 1;
					pArray->u.array.rf_initializing = 0;
					hpt_printk(("Initializing failed.\n"));
					ioctl_ReportEvent(ET_INITIALIZE_FAILED, pArray);
					break;

				case VERIFY:
					needsync = 1;
					pArray->u.array.rf_verifying = 0;
					hpt_printk(("Verifying failed.\n"));
					ioctl_ReportEvent(ET_VERIFY_FAILED, pArray);
					break;
			}
			needdelete=1;
	}

	while (pAdapter->outstandingCommands) 
	{
		KdPrintI(("currcmds is %d, wait..\n", pAdapter->outstandingCommands));
		/* put this to have driver stop processing system commands quickly */
		if (!mWaitingForIdle(_VBUS_P0)) CallWhenIdle(_VBUS_P nothing, 0);
		mtx_sleep(pAdapter, &pAdapter->lock, 0, "hptidle", 0);
	}
		
	if (needsync) SyncArrayInfo(pArray);
	if(needdelete && (pArray->u.array.rf_duplicate_must_done || (flags == INITIALIZE)))
		fDeleteArray(_VBUS_P pArray, TRUE);

	Check_Idle_Call(pAdapter);
	mtx_unlock(&pAdapter->lock);
}
