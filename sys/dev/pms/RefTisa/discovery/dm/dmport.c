/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE

********************************************************************************/
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>

#include <dev/pms/RefTisa/discovery/dm/dmdefs.h>
#include <dev/pms/RefTisa/discovery/dm/dmtypes.h>
#include <dev/pms/RefTisa/discovery/dm/dmproto.h>

/*****************************************************************************/
/*! \brief dmCreatePort
 *  
 *
 *  Purpose: A port context is created by this function 
 *  
 *  \param   dmRoot:              DM context handle.
 *  \param   dmPortContext:       Pointer to this instance of port context 
 * 
 *  \return: 
 *          DM_RC_SUCCESS
 *          DM_RC_FAILURE
 *
 */
/*****************************************************************************/
osGLOBAL bit32  
dmCreatePort(  
             dmRoot_t        *dmRoot,
             dmPortContext_t *dmPortContext,
             dmPortInfo_t    *dmPortInfo)
{
  dmIntRoot_t               *dmIntRoot    = agNULL;
  dmIntContext_t            *dmAllShared = agNULL;
  dmIntPortContext_t        *onePortContext = agNULL;
  dmList_t                  *PortContextList = agNULL;
    
  DM_DBG3(("dmCreatePort: start\n"));
  
  if (dmRoot == agNULL)
  {
    DM_DBG1(("dmCreatePort: dmRoot is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }
  
  if (dmPortContext == agNULL)
  {
    DM_DBG1(("dmCreatePort: dmPortContext is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }
  
  /* the duplicacy of a port is checked */
  if (dmPortContext->dmData != agNULL)
  {
    DM_DBG1(("dmCreatePort: dmPortContext->dmData is not NULL, wrong, Already created!!!\n"));
    return DM_RC_FAILURE;	
  }
  
  if (dmPortInfo == agNULL)
  {
    DM_DBG1(("dmCreatePort: dmPortInfo is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }
  
  dmIntRoot = (dmIntRoot_t *)dmRoot->dmData;
  
  if (dmIntRoot == agNULL)
  {
    DM_DBG1(("dmCreatePort: dmIntRoot is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }

  dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;

  if (dmAllShared == agNULL)
  {
    DM_DBG1(("dmCreatePort: dmAllShared is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }

  tddmSingleThreadedEnter(dmRoot, DM_PORT_LOCK);
  if (DMLIST_NOT_EMPTY(&(dmAllShared->FreePortContextList)))
  {
    DMLIST_DEQUEUE_FROM_HEAD(&PortContextList, &(dmAllShared->FreePortContextList));
    tddmSingleThreadedLeave(dmRoot, DM_PORT_LOCK);
    onePortContext = DMLIST_OBJECT_BASE(dmIntPortContext_t, FreeLink, PortContextList);
    if (onePortContext == agNULL)
    {
      DM_DBG1(("dmCreatePort: onePortContext is NULL in allocation, wrong!!!\n"));
      return DM_RC_FAILURE;	
    }
    
    dmPortContext->dmData =  onePortContext;  
    onePortContext->DiscoveryState = DM_DSTATE_NOT_STARTED;
    onePortContext->discoveryOptions = DM_DISCOVERY_OPTION_FULL_START;

    onePortContext->dmRoot = dmRoot;
    onePortContext->dmPortContext = dmPortContext;
    onePortContext->valid = agTRUE;
    onePortContext->RegFailed = agFALSE;
    
    onePortContext->LinkRate = DM_GET_LINK_RATE(dmPortInfo->flag);
    DM_DBG3(("dmCreatePort: linkrate %0x\n", onePortContext->LinkRate));

    onePortContext->sasRemoteAddressHi = DM_GET_SAS_ADDRESSHI(dmPortInfo->sasRemoteAddressHi);
    onePortContext->sasRemoteAddressLo = DM_GET_SAS_ADDRESSLO(dmPortInfo->sasRemoteAddressLo);
    onePortContext->sasLocalAddressHi = DM_GET_SAS_ADDRESSHI(dmPortInfo->sasLocalAddressHi);
    onePortContext->sasLocalAddressLo = DM_GET_SAS_ADDRESSLO(dmPortInfo->sasLocalAddressLo);
    DM_DBG3(("dmCreatePort: pid %d\n", onePortContext->id));
    DM_DBG3(("dmCreatePort: RemoteAddrHi 0x%08x RemoteAddrLo 0x%08x\n", onePortContext->sasRemoteAddressHi, onePortContext->sasRemoteAddressLo));
    DM_DBG3(("dmCreatePort: LocalAddrHi 0x%08x LocaAddrLo 0x%08x\n", onePortContext->sasLocalAddressHi, onePortContext->sasLocalAddressLo));
 
    tddmSingleThreadedEnter(dmRoot, DM_PORT_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(onePortContext->MainLink), &(dmAllShared->MainPortContextList));
    tddmSingleThreadedLeave(dmRoot, DM_PORT_LOCK);
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_PORT_LOCK);
    DM_DBG1(("dmCreatePort: Attention. no more free PortContext!!!\n"));
    return DM_RC_FAILURE;
  }
  
  return DM_RC_SUCCESS;
}	     	     

/*****************************************************************************/
/*! \brief dmDestroyPort
 *  
 *
 *  Purpose: A port context is destroyed by this function 
 *  
 *  \param   dmRoot:              DM context handle.
 *  \param   dmPortContext:       Pointer to this instance of port context 
 * 
 *  \return: 
 *          DM_RC_SUCCESS
 *          DM_RC_FAILURE
 *
 */
/*****************************************************************************/
osGLOBAL bit32  
dmDestroyPort(
          dmRoot_t        *dmRoot,
          dmPortContext_t *dmPortContext,
          dmPortInfo_t    *dmPortInfo)       
{
  dmIntRoot_t               *dmIntRoot    = agNULL;
  dmIntContext_t            *dmAllShared = agNULL;
  dmIntPortContext_t        *onePortContext = agNULL;
  
  DM_DBG1(("dmDestroyPort: start\n"));
  if (dmRoot == agNULL)
  {
    DM_DBG1(("dmDestroyPort: dmRoot is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }
  
  if (dmPortContext == agNULL)
  {
    DM_DBG1(("dmDestroyPort: dmPortContext is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }
  
  if (dmPortInfo == agNULL)
  {
    DM_DBG1(("dmDestroyPort: dmPortInfo is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }

  dmIntRoot = (dmIntRoot_t *)dmRoot->dmData;
  
  if (dmIntRoot == agNULL)
  {
    DM_DBG1(("dmDestroyPort: dmIntRoot is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }

  dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;

  if (dmAllShared == agNULL)
  {
    DM_DBG1(("dmDestroyPort: dmAllShared is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;	
  }

  /*
    no device(expander) to be removed since all devices should
    be in freelist at the end of discovery
    But if the discovery is in progress, abort it and clean up
  */
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmDestroyPort: onePortContext is NULL, wrong!!!\n"));
    return DM_RC_FAILURE;
  }
  
#if 1
  if (onePortContext->DiscoveryState != DM_DSTATE_COMPLETED)
  {
    dmDiscoverAbort(dmRoot, onePortContext);
  }
  else
  {
    /* move devices from dmAllShared->MainDeviceList to dmAllShared->FreeDeviceList; dmDiscoveryDeviceCleanUp()
       move from dmAllShared->mainExpanderList to dmAllShared->freeExpanderList; dmDiscoveryExpanderCleanUp()
    */
  }
#endif
  
  if (onePortContext->DiscoveryState != DM_DSTATE_COMPLETED)
  {
    /* move from dmAllShared->discoveringExpanderList to dmAllShared->mainExpanderList 
       move from dmAllShared->UpdiscoveringExpanderList to dmAllShared->mainExpanderList     
    */
    dmCleanAllExp(dmRoot, onePortContext);
  }
  
  /* move mainExpanderList then MainDeviceList */
  DM_DBG3(("dmDestroyPort: before dmDiscoveryExpanderCleanUp\n"));
  dmDumpAllMainExp(dmRoot, onePortContext);
  
  /* move from dmAllShared->mainExpanderList to dmAllShared->freeExpanderList */
  dmDiscoveryExpanderCleanUp(dmRoot, onePortContext);
  
  DM_DBG3(("dmDestroyPort: after dmDiscoveryExpanderCleanUp\n"));
  dmDumpAllMainExp(dmRoot, onePortContext);
  
  DM_DBG3(("dmDestroyPort: before dmDiscoveryDeviceCleanUp\n"));
  dmDumpAllMainDevice(dmRoot, onePortContext);
  /* move devices from dmAllShared->MainDeviceList to dmAllShared->FreeDeviceList */
  dmDiscoveryDeviceCleanUp(dmRoot, onePortContext);
  
  DM_DBG3(("dmDestroyPort: after dmDiscoveryDeviceCleanUp\n"));
  dmDumpAllMainDevice(dmRoot, onePortContext);  
  
  dmPortContextReInit(dmRoot, onePortContext);
  
  tddmSingleThreadedEnter(dmRoot, DM_PORT_LOCK);

  if (DMLIST_NOT_EMPTY(&(onePortContext->MainLink)))
  {
    DMLIST_DEQUEUE_THIS(&(onePortContext->MainLink));
  }
  else
  {
    DM_DBG1(("dmDestroyPort: onePortContext->MainLink is NULL, wrong!!!\n"));
  }

  if (DMLIST_NOT_EMPTY(&(onePortContext->FreeLink)) && DMLIST_NOT_EMPTY(&(dmAllShared->FreePortContextList)))
  {
    DMLIST_ENQUEUE_AT_TAIL(&(onePortContext->FreeLink), &(dmAllShared->FreePortContextList));
  }
  else
  {
    DM_DBG1(("dmDestroyPort: onePortContext->FreeLink or dmAllShared->FreePortContextList is NULL, wrong!!!\n"));
  }
  
  tddmSingleThreadedLeave(dmRoot, DM_PORT_LOCK);

  return DM_RC_SUCCESS;
}	     	     
#endif /* FDS_ DM */








