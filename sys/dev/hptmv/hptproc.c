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
 * hptproc.c  sysctl support
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <machine/stdarg.h>

#ifndef __KERNEL__
#define __KERNEL__
#endif

#include <dev/hptmv/global.h>
#include <dev/hptmv/hptintf.h>
#include <dev/hptmv/osbsd.h>
#include <dev/hptmv/access601.h>

int hpt_rescan_all(void);

/***************************************************************************/

static char hptproc_buffer[256];
extern char DRIVER_VERSION[];

typedef struct sysctl_req HPT_GET_INFO;

static int
hpt_set_asc_info(IAL_ADAPTER_T *pAdapter, char *buffer,int length)
{
	int orig_length = length+4;
	PVBus _vbus_p = &pAdapter->VBus;
	PVDevice	 pArray;
	PVDevice pSubArray, pVDev;
	UINT	i, iarray, ichan;
	struct cam_periph *periph = NULL;

	mtx_lock(&pAdapter->lock);
#ifdef SUPPORT_ARRAY	
	if (length>=8 && strncmp(buffer, "rebuild ", 8)==0) 
	{
		buffer+=8;
		length-=8;
		if (length>=5 && strncmp(buffer, "start", 5)==0) 
		{
			for(i = 0; i < MAX_ARRAY_PER_VBUS; i++)
				if ((pArray=ArrayTables(i))->u.array.dArStamp==0)
					continue; 
				else{
					if (pArray->u.array.rf_need_rebuild && !pArray->u.array.rf_rebuilding)
	                    hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pArray, 
							(UCHAR)((pArray->u.array.CriticalMembers || pArray->VDeviceType == VD_RAID_1)? DUPLICATE : REBUILD_PARITY));
				}
			mtx_unlock(&pAdapter->lock);
			return orig_length;
		}
		else if (length>=4 && strncmp(buffer, "stop", 4)==0) 
		{
			for(i = 0; i < MAX_ARRAY_PER_VBUS; i++)
				if ((pArray=ArrayTables(i))->u.array.dArStamp==0)
					continue; 
				else{
					if (pArray->u.array.rf_rebuilding)
					    pArray->u.array.rf_abort_rebuild = 1;
				}
			mtx_unlock(&pAdapter->lock);
			return orig_length;
		}	
		else if (length>=3 && buffer[1]==','&& buffer[0]>='1'&& buffer[2]>='1')	
		{
			iarray = buffer[0]-'1';
	        ichan = buffer[2]-'1';

            if(iarray >= MAX_VDEVICE_PER_VBUS || ichan >= MV_SATA_CHANNELS_NUM) return -EINVAL;

			pArray = _vbus_p->pVDevice[iarray];
			if (!pArray || (pArray->vf_online == 0)) {
				mtx_unlock(&pAdapter->lock);
				return -EINVAL;
			}

            for (i=0;i<MV_SATA_CHANNELS_NUM;i++)
				if(i == ichan)
				    goto rebuild;

	        mtx_unlock(&pAdapter->lock);
	        return -EINVAL;

rebuild:
	        pVDev = &pAdapter->VDevices[ichan];
	        if(!pVDev->u.disk.df_on_line || pVDev->pParent) {
			mtx_unlock(&pAdapter->lock);
			return -EINVAL;
		}

	        /* Not allow to use a mounted disk ??? test*/
			for(i = 0; i < MAX_VDEVICE_PER_VBUS; i++)
			    if(pVDev == _vbus_p->pVDevice[i])
			    {
					periph = hpt_get_periph(pAdapter->mvSataAdapter.adapterId,i);
					if (periph != NULL && periph->refcount >= 1)
					{
						hpt_printk(("Can not use disk used by OS!\n"));
			    mtx_unlock(&pAdapter->lock);
	                    return -EINVAL;	
					}
					/* the Mounted Disk isn't delete */
				} 
			
			switch(pArray->VDeviceType)
			{
				case VD_RAID_1:
				case VD_RAID_5:
				{
					pSubArray = pArray;
loop:
					if(hpt_add_disk_to_array(_VBUS_P VDEV_TO_ID(pSubArray), VDEV_TO_ID(pVDev)) == -1) {
						mtx_unlock(&pAdapter->lock);
						return -EINVAL;
					}
					pSubArray->u.array.rf_auto_rebuild = 0;
					pSubArray->u.array.rf_abort_rebuild = 0;
					hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pSubArray, DUPLICATE);
					break;
				}
				case VD_RAID_0:
					for (i = 0; (UCHAR)i < pArray->u.array.bArnMember; i++) 
						if(pArray->u.array.pMember[i] && mIsArray(pArray->u.array.pMember[i]) &&
						   (pArray->u.array.pMember[i]->u.array.rf_broken == 1))
						{
							  pSubArray = pArray->u.array.pMember[i];
							  goto loop;
						}
				default:
					mtx_unlock(&pAdapter->lock);
					return -EINVAL;
			}
			mtx_unlock(&pAdapter->lock);
			return orig_length;
		}
	}
	else if (length>=7 && strncmp(buffer, "verify ", 7)==0)
	{
		buffer+=7;
		length-=7;
        if (length>=6 && strncmp(buffer, "start ", 6)==0) 
		{
            buffer+=6;
		    length-=6;
            if (length>=1 && *buffer>='1') 
			{
				iarray = *buffer-'1';
				if(iarray >= MAX_VDEVICE_PER_VBUS) {
					mtx_unlock(&pAdapter->lock);
					return -EINVAL;
				}

				pArray = _vbus_p->pVDevice[iarray];
				if (!pArray || (pArray->vf_online == 0)) {
					mtx_unlock(&pAdapter->lock);
					return -EINVAL;
				}
				
				if(pArray->VDeviceType != VD_RAID_1 && pArray->VDeviceType != VD_RAID_5) {
					mtx_unlock(&pAdapter->lock);
					return -EINVAL;
				}

				if (!(pArray->u.array.rf_need_rebuild ||
					pArray->u.array.rf_rebuilding ||
					pArray->u.array.rf_verifying ||
					pArray->u.array.rf_initializing))
				{
					pArray->u.array.RebuildSectors = 0;
					hpt_queue_dpc((HPT_DPC)hpt_rebuild_data_block, pAdapter, pArray, VERIFY);
				}
		mtx_unlock(&pAdapter->lock);
                return orig_length;
			}
		}
		else if (length>=5 && strncmp(buffer, "stop ", 5)==0)
		{
			buffer+=5;
		    length-=5;
            if (length>=1 && *buffer>='1') 
			{
				iarray = *buffer-'1';
				if(iarray >= MAX_VDEVICE_PER_VBUS) {
					mtx_unlock(&pAdapter->lock);
					return -EINVAL;
				}

				pArray = _vbus_p->pVDevice[iarray];
				if (!pArray || (pArray->vf_online == 0)) {
					mtx_unlock(&pAdapter->lock);
					return -EINVAL;
				}
				if(pArray->u.array.rf_verifying) 
				{
				    pArray->u.array.rf_abort_rebuild = 1;
				}
			    mtx_unlock(&pAdapter->lock);
			    return orig_length;
			}
		}
	}
	else
#ifdef _RAID5N_
	if (length>=10 && strncmp(buffer, "writeback ", 10)==0) {
	    	buffer+=10;
		length-=10;
		if (length>=1 && *buffer>='0' && *buffer<='1') {
			_vbus_(r5.enable_write_back) = *buffer-'0';
			if (_vbus_(r5.enable_write_back))
				hpt_printk(("RAID5 write back enabled"));
			mtx_unlock(&pAdapter->lock);
			return orig_length;
		}
	}
	else
#endif
#endif
	if (0) {} /* just to compile */
#ifdef DEBUG
	else if (length>=9 && strncmp(buffer, "dbglevel ", 9)==0) {
	    	buffer+=9;
		length-=9;
		if (length>=1 && *buffer>='0' && *buffer<='3') {
			hpt_dbg_level = *buffer-'0';
			mtx_unlock(&pAdapter->lock);
			return orig_length;
		}
	}
	else if (length>=8 && strncmp(buffer, "disable ", 8)==0) {
		/* TO DO */
	}
#endif
	mtx_unlock(&pAdapter->lock);

	return -EINVAL;
}

/*
 * Since we have only one sysctl node, add adapter ID in the command 
 * line string: e.g. "hpt 0 rebuild start"
 */
static int
hpt_set_info(int length)
{
	int retval;

#ifdef SUPPORT_IOCTL
	PUCHAR ke_area;
	int err;
	DWORD dwRet;
	PHPT_IOCTL_PARAM piop;
#endif
	char *buffer = hptproc_buffer;
	if (length >= 6) {
		if (strncmp(buffer,"hpt ",4) == 0) {
			IAL_ADAPTER_T *pAdapter;
			retval = buffer[4]-'0';
			for (pAdapter=gIal_Adapter; pAdapter; pAdapter=pAdapter->next) {
				if (pAdapter->mvSataAdapter.adapterId==retval)
					return (retval = hpt_set_asc_info(pAdapter, buffer+6, length-6)) >= 0? retval : -EINVAL;
			}
			return -EINVAL;
		}
#ifdef SUPPORT_IOCTL	
		piop = (PHPT_IOCTL_PARAM)buffer;
		if (piop->Magic == HPT_IOCTL_MAGIC || 
			piop->Magic == HPT_IOCTL_MAGIC32) 	{
			KdPrintE(("ioctl=%d in=%p len=%d out=%p len=%d\n", 
				piop->dwIoControlCode,
        			piop->lpInBuffer,
        			piop->nInBufferSize,
        			piop->lpOutBuffer,
	        		piop->nOutBufferSize));

			/*
        	 	 * map buffer to kernel.
        	 	 */
        		if (piop->nInBufferSize > PAGE_SIZE ||
        			piop->nOutBufferSize > PAGE_SIZE ||
        			piop->nInBufferSize+piop->nOutBufferSize > PAGE_SIZE) {
        			KdPrintE(("User buffer too large\n"));
        			return -EINVAL;
        		}
        		
        		ke_area = malloc(piop->nInBufferSize+piop->nOutBufferSize, M_DEVBUF, M_NOWAIT);
				if (ke_area == NULL) {
					KdPrintE(("Couldn't allocate kernel mem.\n"));
					return -EINVAL;
				}

			if (piop->nInBufferSize) {
				if (copyin((void*)(ULONG_PTR)piop->lpInBuffer, ke_area, piop->nInBufferSize) != 0) {
					KdPrintE(("Failed to copyin from lpInBuffer\n"));
					free(ke_area, M_DEVBUF);
					return -EFAULT;
				}
			}

			/*
			  * call kernel handler.
			  */    
			err = Kernel_DeviceIoControl(&gIal_Adapter->VBus,
				piop->dwIoControlCode, ke_area, piop->nInBufferSize,
				ke_area + piop->nInBufferSize, piop->nOutBufferSize, &dwRet);    
			
			if (err==0) {
				if (piop->nOutBufferSize)
					copyout(ke_area + piop->nInBufferSize, (void*)(ULONG_PTR)piop->lpOutBuffer, piop->nOutBufferSize);
				
				if (piop->lpBytesReturned)
					copyout(&dwRet, (void*)(ULONG_PTR)piop->lpBytesReturned, sizeof(DWORD));
			
				free(ke_area, M_DEVBUF);
				return length;
			}
			else  KdPrintW(("Kernel_ioctl(): return %d\n", err));

			free(ke_area, M_DEVBUF);
			return -EINVAL;
		} else 	{
    		KdPrintW(("Wrong signature: %x\n", piop->Magic));
    		return -EINVAL;
		}
#endif
	}

	return -EINVAL;
}

#define shortswap(w) ((WORD)((w)>>8 | ((w) & 0xFF)<<8))

static void
get_disk_name(char *name, PDevice pDev)
{
	int i;
	MV_SATA_CHANNEL *pMvSataChannel = pDev->mv;
	IDENTIFY_DATA2 *pIdentifyData = (IDENTIFY_DATA2 *)pMvSataChannel->identifyDevice;
	
	for (i = 0; i < 10; i++) 
		((WORD*)name)[i] = shortswap(pIdentifyData->ModelNumber[i]);
	name[20] = '\0';
}

static int
hpt_copy_info(HPT_GET_INFO *pinfo, char *fmt, ...) 
{
	int printfretval;
	va_list ap;
	
	if(fmt == NULL) {
		*hptproc_buffer = 0;
		return (SYSCTL_OUT(pinfo, hptproc_buffer, 1));
	}
	else 
	{
		va_start(ap, fmt);
		printfretval = vsnprintf(hptproc_buffer, sizeof(hptproc_buffer), fmt, ap);
		va_end(ap);
		return(SYSCTL_OUT(pinfo, hptproc_buffer, strlen(hptproc_buffer)));
	}
}

static void
hpt_copy_disk_info(HPT_GET_INFO *pinfo, PVDevice pVDev, UINT iChan)
{
	char name[32], arrayname[16], *status;

	get_disk_name(name, &pVDev->u.disk);
	
	if (!pVDev->u.disk.df_on_line)
		status = "Disabled";
	else if (pVDev->VDeviceType==VD_SPARE)
		status = "Spare   ";
	else
		status = "Normal  ";

#ifdef SUPPORT_ARRAY
	if(pVDev->pParent) {
		memcpy(arrayname, pVDev->pParent->u.array.ArrayName, MAX_ARRAY_NAME);
		if (pVDev->pParent->u.array.CriticalMembers & (1<<pVDev->bSerialNumber))
			status = "Degraded";
	}
	else
#endif
		arrayname[0]=0;
	
	hpt_copy_info(pinfo, "Channel %d  %s  %5dMB  %s %s\n",
		iChan+1, 
		name, pVDev->VDeviceCapacity>>11, status, arrayname);
}

#ifdef SUPPORT_ARRAY
static void
hpt_copy_array_info(HPT_GET_INFO *pinfo, int nld, PVDevice pArray)
{
	int i;
	char *sType = NULL, *sStatus = NULL;
	char buf[32];
    PVDevice pTmpArray;

	switch (pArray->VDeviceType) {
		case VD_RAID_0:
			for (i = 0; (UCHAR)i < pArray->u.array.bArnMember; i++) 
		  		if(pArray->u.array.pMember[i])	{
			  		if(mIsArray(pArray->u.array.pMember[i]))
				 		sType = "RAID 1/0   ";
			  			/* TO DO */
			  		else
				 		sType = "RAID 0     ";
			  		break;
		  		}
			break;
			
		case VD_RAID_1:
			sType = "RAID 1     ";
			break;
			
		case VD_JBOD:
			sType = "JBOD       ";
			break;
			
		case VD_RAID_5:
       		sType = "RAID 5     ";
			break;
			
		default:
			sType = "N/A        ";
			break;
	}
	
	if (pArray->vf_online == 0)
		sStatus = "Disabled";
	else if (pArray->u.array.rf_broken)
		sStatus = "Critical";
	for (i = 0; (UCHAR)i < pArray->u.array.bArnMember; i++)
	{
		if (!sStatus) 
		{
			if(mIsArray(pArray->u.array.pMember[i]))
                		pTmpArray = pArray->u.array.pMember[i];
			else
			   	pTmpArray = pArray;
			
			if (pTmpArray->u.array.rf_rebuilding) {
#ifdef DEBUG
				sprintf(buf, "Rebuilding %lldMB", (pTmpArray->u.array.RebuildSectors>>11));
#else 
				sprintf(buf, "Rebuilding %d%%", (UINT)((pTmpArray->u.array.RebuildSectors>>11)*100/((pTmpArray->VDeviceCapacity/(pTmpArray->u.array.bArnMember-1))>>11)));
#endif
				sStatus = buf;
			}
			else if (pTmpArray->u.array.rf_verifying) {
				sprintf(buf, "Verifying %d%%", (UINT)((pTmpArray->u.array.RebuildSectors>>11)*100/((pTmpArray->VDeviceCapacity/(pTmpArray->u.array.bArnMember-1))>>11)));
				sStatus = buf;
			}
			else if (pTmpArray->u.array.rf_need_rebuild)
				sStatus = "Critical";
			else if (pTmpArray->u.array.rf_broken)
				sStatus = "Critical";
			
			if(pTmpArray == pArray) goto out;
		}
		else
			goto out;
	}
out:	
	if (!sStatus) sStatus = "Normal";
	hpt_copy_info(pinfo, "%2d  %11s  %-20s  %5lldMB  %-16s", nld, sType, pArray->u.array.ArrayName, pArray->VDeviceCapacity>>11, sStatus);
}
#endif

static int
hpt_get_info(IAL_ADAPTER_T *pAdapter, HPT_GET_INFO *pinfo)
{
	PVBus _vbus_p = &pAdapter->VBus;
	struct cam_periph *periph = NULL;
	UINT channel,j,i;
	PVDevice pVDev;

#ifndef FOR_DEMO
	mtx_lock(&pAdapter->lock);
	if (pAdapter->beeping) {
		pAdapter->beeping = 0;
		BeepOff(pAdapter->mvSataAdapter.adapterIoBaseAddress);
	}
	mtx_unlock(&pAdapter->lock);
#endif

	hpt_copy_info(pinfo, "Controller #%d:\n\n", pAdapter->mvSataAdapter.adapterId);
	
	hpt_copy_info(pinfo, "Physical device list\n");
	hpt_copy_info(pinfo, "Channel    Model                Capacity  Status   Array\n");
	hpt_copy_info(pinfo, "-------------------------------------------------------------------\n");

    for (channel = 0; channel < MV_SATA_CHANNELS_NUM; channel++)
	{
		pVDev = &(pAdapter->VDevices[channel]);
		if(pVDev->u.disk.df_on_line)
			 hpt_copy_disk_info(pinfo, pVDev, channel);
	}
	
	hpt_copy_info(pinfo, "\nLogical device list\n");
	hpt_copy_info(pinfo, "No. Type         Name                 Capacity  Status            OsDisk\n");
	hpt_copy_info(pinfo, "--------------------------------------------------------------------------\n");

	j=1;
	for(i = 0; i < MAX_VDEVICE_PER_VBUS; i++){
        pVDev = _vbus_p->pVDevice[i];
		if(pVDev){
			j=i+1;
#ifdef SUPPORT_ARRAY
			if (mIsArray(pVDev))
			{
		is_array:
				hpt_copy_array_info(pinfo, j, pVDev);
			}
			else
#endif
			{
				char name[32];
				/* it may be add to an array after driver loaded, check it */
#ifdef SUPPORT_ARRAY
				if (pVDev->pParent)
					/* in this case, pVDev can only be a RAID 1 source disk. */
					if (pVDev->pParent->VDeviceType==VD_RAID_1 && pVDev==pVDev->pParent->u.array.pMember[0]) 
						goto is_array;
#endif
				get_disk_name(name, &pVDev->u.disk);
				
				hpt_copy_info(pinfo, "%2d  %s  %s  %5dMB  %-16s",
					j, "Single disk", name, pVDev->VDeviceCapacity>>11, 
					/* gmm 2001-6-19: Check if pDev has been added to an array. */
					((pVDev->pParent) ? "Unavailable" : "Normal"));
			}
			periph = hpt_get_periph(pAdapter->mvSataAdapter.adapterId, i);
			if (periph == NULL)
				hpt_copy_info(pinfo,"  %s\n","not registered");
			else
				hpt_copy_info(pinfo,"  %s%d\n", periph->periph_name, periph->unit_number);
		 }
	}
	return 0;
}

static __inline int
hpt_proc_in(SYSCTL_HANDLER_ARGS, int *len)
{
	int i, error=0;

	*len = 0;
	if ((req->newlen - req->newidx) >= sizeof(hptproc_buffer)) {
		error = EINVAL;
	} else {
		i = (req->newlen - req->newidx);
		error = SYSCTL_IN(req, hptproc_buffer, i);
		if (!error)
			*len = i;
		(hptproc_buffer)[i] = '\0';
	}
	return (error);
}

static int
hpt_status(SYSCTL_HANDLER_ARGS)
{
	int length, error=0, retval=0;
	IAL_ADAPTER_T *pAdapter;

	error = hpt_proc_in(oidp, arg1, arg2, req, &length);
	
    if (req->newptr != NULL) 	
	{
		if (error || length == 0) 	
		{
    		KdPrint(("error!\n"));
    		retval = EINVAL;
    		goto out;
		}
    		
		if (hpt_set_info(length) >= 0)
			retval = 0;
		else
			retval = EINVAL;
		goto out;
    }

	hpt_copy_info(req, "%s Version %s\n", DRIVER_NAME, DRIVER_VERSION);
	for (pAdapter=gIal_Adapter; pAdapter; pAdapter=pAdapter->next) {
		if (hpt_get_info(pAdapter, req) < 0) {
			retval = EINVAL;
			break;
		}
	}
	
	hpt_copy_info(req, NULL);
	goto out;

out:
	return (retval);
}


#define xhptregister_node(name) hptregister_node(name)

#if __FreeBSD_version >= 1100024
#define hptregister_node(name) \
	SYSCTL_ROOT_NODE(OID_AUTO, name, CTLFLAG_RW, 0, "Get/Set " #name " state root node"); \
	SYSCTL_OID(_ ## name, OID_AUTO, status, CTLTYPE_STRING|CTLFLAG_RW, \
	NULL, 0, hpt_status, "A", "Get/Set " #name " state")
#else
#define hptregister_node(name) \
	SYSCTL_NODE(, OID_AUTO, name, CTLFLAG_RW, 0, "Get/Set " #name " state root node"); \
	SYSCTL_OID(_ ## name, OID_AUTO, status, CTLTYPE_STRING|CTLFLAG_RW, \
	NULL, 0, hpt_status, "A", "Get/Set " #name " state")
#endif
	
xhptregister_node(PROC_DIR_NAME);
