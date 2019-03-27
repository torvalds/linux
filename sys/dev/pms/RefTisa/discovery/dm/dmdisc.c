/*******************************************************************************
**
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
**
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
/*! \brief dmDiscover
 *  
 *
 *  Purpose: A discovery is started by this function 
 *  
 *  \param   dmRoot:              DM context handle.
 *  \param   dmPortContext:       Pointer to this instance of port context 
 *  \param   option:              Discovery option 
 * 
 *  \return: 
 *          DM_RC_SUCCESS
 *          DM_RC_FAILURE
 *
 */
/*****************************************************************************/
osGLOBAL bit32 	
dmDiscover(  
           dmRoot_t 		*dmRoot,
           dmPortContext_t	*dmPortContext,
           bit32 		option)
{
  dmIntPortContext_t        *onePortContext = agNULL;
  bit32                     ret = DM_RC_FAILURE;
  
  DM_DBG3(("dmDiscover: start\n"));
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmDiscover: onePortContext is NULL!!!\n"));
    return DM_RC_FAILURE;
  }
  
  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmDiscover: invalid port!!!\n"));
    return DM_RC_FAILURE;
  }
  
  if (onePortContext->RegFailed == agTRUE)
  {
    DM_DBG1(("dmDiscover: Registration failed!!!\n"));
    return DM_RC_FAILURE;
  }
  
  switch ( option )
  {
  case DM_DISCOVERY_OPTION_FULL_START:
    DM_DBG3(("dmDiscover: full, pid %d\n", onePortContext->id));
    onePortContext->discovery.type = DM_DISCOVERY_OPTION_FULL_START;
    dmDiscoveryResetMCN(dmRoot, onePortContext);
    ret = dmFullDiscover(dmRoot, onePortContext);
    break;
  case DM_DISCOVERY_OPTION_INCREMENTAL_START:
    DM_DBG3(("dmDiscover: incremental, pid %d\n", onePortContext->id));
    onePortContext->discovery.type = DM_DISCOVERY_OPTION_INCREMENTAL_START;
    dmDiscoveryResetMCN(dmRoot, onePortContext);
    ret = dmIncrementalDiscover(dmRoot, onePortContext, agFALSE);
    break;
  case DM_DISCOVERY_OPTION_ABORT:
    DM_DBG3(("dmDiscover: abort\n"));
    if (onePortContext->DiscoveryState != DM_DSTATE_COMPLETED)
    {
      if (onePortContext->discovery.pendingSMP == 0)
      {
        dmDiscoverAbort(dmRoot, onePortContext);
        tddmDiscoverCB(
                       dmRoot,
                       onePortContext->dmPortContext,
                       dmDiscAborted
                       );
      }
      else
      {
        DM_DBG3(("dmDiscover: abortInProgress\n"));
        onePortContext->DiscoveryAbortInProgress = agTRUE;
        tddmDiscoverCB(
                       dmRoot,
                       dmPortContext,
                       dmDiscAbortInProgress
                       );
      }
    }
    else
    {
      DM_DBG3(("dmDiscover: no discovery to abort\n"));
      tddmDiscoverCB(
                     dmRoot,
                     dmPortContext,
                     dmDiscAbortInvalid
                     );
    }
    ret = DM_RC_SUCCESS;
    break;
  default:
    break;
  }  
  return ret;
}	   				
				
osGLOBAL bit32
dmFullDiscover(
               dmRoot_t 	    	*dmRoot, 
               dmIntPortContext_t       *onePortContext	
              )
{
  dmExpander_t              *oneExpander = agNULL;
  dmSASSubID_t              dmSASSubID;
  dmDeviceData_t            *oneExpDeviceData = agNULL;
    
  DM_DBG1(("dmFullDiscover: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmFullDiscover: invalid port!!!\n"));
    return DM_RC_FAILURE;
  }
  
  if (onePortContext->DiscoveryState == DM_DSTATE_STARTED)
  {
    DM_DBG1(("dmFullDiscover: no two instances of discovery allowed!!!\n"));
    return DM_RC_FAILURE;
  }
  
  onePortContext->DiscoveryState = DM_DSTATE_STARTED;
  
  dmSASSubID.sasAddressHi = onePortContext->sasRemoteAddressHi;
  dmSASSubID.sasAddressLo = onePortContext->sasRemoteAddressLo;
  
  /* check OnePortContext->discovery.discoveringExpanderList */
  oneExpander = dmExpFind(dmRoot, onePortContext, dmSASSubID.sasAddressHi, dmSASSubID.sasAddressLo);
  if (oneExpander != agNULL)
  {
    oneExpDeviceData = oneExpander->dmDevice;
  }
  else
  {
    /* check dmAllShared->mainExpanderList */
    oneExpander = dmExpMainListFind(dmRoot, onePortContext, dmSASSubID.sasAddressHi, dmSASSubID.sasAddressLo);
    if (oneExpander != agNULL)
    {
      oneExpDeviceData = oneExpander->dmDevice;
    }
  }
  
  if (oneExpDeviceData != agNULL)
  {
    dmSASSubID.initiator_ssp_stp_smp = oneExpDeviceData->initiator_ssp_stp_smp;
    dmSASSubID.target_ssp_stp_smp = oneExpDeviceData->target_ssp_stp_smp;
    oneExpDeviceData->registered = agTRUE;    
    dmAddSASToSharedcontext(dmRoot, onePortContext, &dmSASSubID, oneExpDeviceData, 0xFF);  
  }
  else
  {
    DM_DBG1(("dmFullDiscover:oneExpDeviceData is NULL!!!\n"));
    return DM_RC_FAILURE;
  }
  
  dmUpStreamDiscoverStart(dmRoot, onePortContext);
  
  return DM_RC_SUCCESS;
}	   		      

osGLOBAL bit32
dmIncrementalDiscover(
                      dmRoot_t 	    	      *dmRoot, 
                      dmIntPortContext_t      *onePortContext,
		      bit32                   flag	
                     )
{
  dmExpander_t              *oneExpander = agNULL;
  dmSASSubID_t              dmSASSubID;
  dmDeviceData_t            *oneExpDeviceData = agNULL;
  
  DM_DBG1(("dmIncrementalDiscover: start\n"));

  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmIncrementalDiscover: invalid port!!!\n"));
    return DM_RC_FAILURE;
  }
  
  /* TDM triggerred; let go DM triggerred */
  if (flag == agFALSE)
  {
    if (onePortContext->DiscoveryState == DM_DSTATE_STARTED)
    {
      DM_DBG1(("dmIncrementalDiscover: no two instances of discovery allowed!!!\n"));
      return DM_RC_FAILURE;
    }
  }
  
  onePortContext->DiscoveryState = DM_DSTATE_STARTED;
  onePortContext->discovery.type = DM_DISCOVERY_OPTION_INCREMENTAL_START;
  
  dmSASSubID.sasAddressHi = onePortContext->sasRemoteAddressHi;
  dmSASSubID.sasAddressLo = onePortContext->sasRemoteAddressLo;
  
  /* check OnePortContext->discovery.discoveringExpanderList */
  oneExpander = dmExpFind(dmRoot, onePortContext, dmSASSubID.sasAddressHi, dmSASSubID.sasAddressLo);
  if (oneExpander != agNULL)
  {
    oneExpDeviceData = oneExpander->dmDevice;
  }
  else
  {
    /* check dmAllShared->mainExpanderList */
    oneExpander = dmExpMainListFind(dmRoot, onePortContext, dmSASSubID.sasAddressHi, dmSASSubID.sasAddressLo);
    if (oneExpander != agNULL)
    {
      oneExpDeviceData = oneExpander->dmDevice;
    }
  }
  
  if (oneExpDeviceData != agNULL)
  {
    dmSASSubID.initiator_ssp_stp_smp = oneExpDeviceData->initiator_ssp_stp_smp;
    dmSASSubID.target_ssp_stp_smp = oneExpDeviceData->target_ssp_stp_smp;
    oneExpDeviceData->registered = agTRUE;    
    dmAddSASToSharedcontext(dmRoot, onePortContext, &dmSASSubID, oneExpDeviceData, 0xFF);  
  }
  else
  {
    DM_DBG1(("dmIncrementalDiscover:oneExpDeviceData is NULL!!!\n"));
    return DM_RC_FAILURE;
  }
  
  dmUpStreamDiscoverStart(dmRoot, onePortContext);
  
  return DM_RC_SUCCESS;
}	   			     

osGLOBAL void
dmUpStreamDiscoverStart(
                        dmRoot_t             *dmRoot,
                        dmIntPortContext_t   *onePortContext		
                       )
{
//  dmExpander_t              *oneExpander = agNULL;
  bit32                     sasAddressHi, sasAddressLo;
  dmDeviceData_t            *oneDeviceData;
  dmExpander_t              *oneExpander = agNULL;
  
  DM_DBG3(("dmUpStreamDiscoverStart: start\n"));
  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmUpStreamDiscoverStart: invalid port!!!\n"));
    return;
  }
  /*
    at this point, the 1st expander should have been registered.
    find an expander from onePortContext 
  */
  sasAddressHi = onePortContext->sasRemoteAddressHi;
  sasAddressLo = onePortContext->sasRemoteAddressLo;
  DM_DBG3(("dmUpStreamDiscoverStart: Port Remote AddrHi 0x%08x Remote AddrLo 0x%08x\n", sasAddressHi, sasAddressLo));

  oneDeviceData = dmDeviceFind(dmRoot, onePortContext, sasAddressHi, sasAddressLo);

//  oneDeviceData = oneExpander->dmDevice; 
// start here
  onePortContext->discovery.status = DISCOVERY_UP_STREAM;
  if (oneDeviceData == agNULL)
  {
    DM_DBG1(("dmUpStreamDiscoverStart: oneExpander is NULL, wrong!!!\n"));
    return;
  }
  else
  {
    if ( (oneDeviceData->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
         ||
         (oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE)
	 ||
	 DEVICE_IS_SMP_TARGET(oneDeviceData)
        )
    {
#if 1  /* for incremental discovery */  
      /* start here: if not on discoveringExpanderList, alloc and add 
      dmNewEXPorNot()
      */
      oneExpander = dmExpFind(dmRoot, onePortContext, sasAddressHi, sasAddressLo);
      if ( oneExpander == agNULL)
      {
        /* alloc and add */
        oneExpander = dmDiscoveringExpanderAlloc(dmRoot, onePortContext, oneDeviceData);
        if ( oneExpander != agNULL)
        {
          dmDiscoveringExpanderAdd(dmRoot, onePortContext, oneExpander);      
        }       
        else
	{
          DM_DBG1(("dmUpStreamDiscoverStart: failed to allocate expander or discovey aborted!!!\n"));
          return;
	}
      }
#endif
 
      dmUpStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }
    else
    {
      DM_DBG1(("dmUpStreamDiscoverStart: oneDeviceData is not an Expander did %d, wrong!!!\n", oneDeviceData->id));
      return;
    }
  }
  return;
}  				

/* sends report general */
osGLOBAL void
dmUpStreamDiscovering(
                      dmRoot_t              *dmRoot,
                      dmIntPortContext_t    *onePortContext,
                      dmDeviceData_t        *oneDeviceData
                     )
{
  dmList_t          *ExpanderList;
  dmExpander_t      *oneNextExpander = agNULL;
  
  DM_DBG3(("dmUpStreamDiscovering: start\n"));
  
  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmUpStreamDiscovering: invalid port!!!\n"));
    return;
  }
  
  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (DMLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    DM_DBG3(("dmUpStreamDiscovering: should be the end\n"));
    oneNextExpander = agNULL;
  }
  else
  {
    DMLIST_DEQUEUE_FROM_HEAD(&ExpanderList, &(onePortContext->discovery.discoveringExpanderList));
    oneNextExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if ( oneNextExpander != agNULL)
    {
      DMLIST_ENQUEUE_AT_HEAD(&(oneNextExpander->linkNode), &(onePortContext->discovery.discoveringExpanderList));
      DM_DBG3(("dmUpStreamDiscovering tdsaSASUpStreamDiscovering: dequeue head\n"));
      DM_DBG3(("dmUpStreamDiscovering: expander id %d\n", oneNextExpander->id));
    }
    else
    {
      DM_DBG1(("dmUpStreamDiscovering: oneNextExpander is NULL!!!\n"));
    }
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);

  }
  
  if (oneNextExpander != agNULL)
  {
    dmReportGeneralSend(dmRoot, oneNextExpander->dmDevice);
  }
  else
  {
    DM_DBG3(("dmUpStreamDiscovering: No more expander list\n"));
    dmDownStreamDiscoverStart(dmRoot, onePortContext, oneDeviceData);
  }
  
  return;
}				

osGLOBAL void
dmDownStreamDiscoverStart(
                          dmRoot_t              *dmRoot,
                          dmIntPortContext_t    *onePortContext,
                          dmDeviceData_t        *oneDeviceData
                         )
{
  dmExpander_t        *UpStreamExpander;
  dmExpander_t        *oneExpander;
  
  DM_DBG3(("dmDownStreamDiscoverStart: start\n"));
  
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmDownStreamDiscoverStart: invalid port or aborted discovery!!!\n"));  
    return;
  }

  /* set discovery status */
  onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;

  /* If it's an expander */    
  if ( (oneDeviceData->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
       || (oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE)
       || DEVICE_IS_SMP_TARGET(oneDeviceData)
       )
  {
    oneExpander = oneDeviceData->dmExpander;    
    UpStreamExpander = oneExpander->dmUpStreamExpander;
    
    /* If the two expanders are the root of two edge sets; sub-to-sub */
    if ( (UpStreamExpander != agNULL) && ( UpStreamExpander->dmUpStreamExpander == oneExpander ) )
    {
      DM_DBG3(("dmDownStreamDiscoverStart: Root found pExpander=%p pUpStreamExpander=%p\n", 
               oneExpander, UpStreamExpander));
      //Saves the root expander
      onePortContext->discovery.RootExp = oneExpander;
      DM_DBG3(("dmDownStreamDiscoverStart: Root exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG3(("dmDownStreamDiscoverStart: Root exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
               
      /* reset up stream inform for pExpander */
      oneExpander->dmUpStreamExpander = agNULL;      
      /* Add the pExpander to discovering list */
      dmDiscoveringExpanderAdd(dmRoot, onePortContext, oneExpander);

      /* reset up stream inform for oneExpander */
      UpStreamExpander->dmUpStreamExpander = agNULL;      
      /* Add the UpStreamExpander to discovering list */
      dmDiscoveringExpanderAdd(dmRoot, onePortContext, UpStreamExpander);
    }
    /* If the two expanders are not the root of two edge sets. eg) one root */
    else
    {
      //Saves the root expander
      onePortContext->discovery.RootExp = oneExpander;

      DM_DBG3(("dmDownStreamDiscoverStart: NO Root pExpander=%p\n", oneExpander));
      DM_DBG3(("dmDownStreamDiscoverStart: Root exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG3(("dmDownStreamDiscoverStart: Root exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
      
      /* (2.2.2.1) Add the pExpander to discovering list */
      dmDiscoveringExpanderAdd(dmRoot, onePortContext, oneExpander);      
    }
  }

  /* Continue down stream discovering */
  dmDownStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
  
  return;
}			 

osGLOBAL void
dmDownStreamDiscovering(
                        dmRoot_t              *dmRoot,
                        dmIntPortContext_t    *onePortContext,
                        dmDeviceData_t        *oneDeviceData
                       )
{
  dmExpander_t      *NextExpander = agNULL;
  dmList_t          *ExpanderList;
  
  DM_DBG3(("dmDownStreamDiscovering: start\n"));
  
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmDownStreamDiscovering: invalid port or aborted discovery!!!\n"));  
    return;
  }

  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (DMLIST_EMPTY(&(onePortContext->discovery.discoveringExpanderList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    DM_DBG3(("dmDownStreamDiscovering: should be the end\n"));
    NextExpander = agNULL;
  }
  else
  {
    DMLIST_DEQUEUE_FROM_HEAD(&ExpanderList, &(onePortContext->discovery.discoveringExpanderList));;
    NextExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if ( NextExpander != agNULL)
    {
      DMLIST_ENQUEUE_AT_HEAD(&(NextExpander->linkNode), &(onePortContext->discovery.discoveringExpanderList));;
      DM_DBG3(("dmDownStreamDiscovering tdsaSASDownStreamDiscovering: dequeue head\n"));
      DM_DBG3(("dmDownStreamDiscovering: expander id %d\n", NextExpander->id));
    }
    else
    {
     DM_DBG1(("dmDownStreamDiscovering: NextExpander is NULL!!!\n"));  
    }
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    
  }
  
  /* If there is an expander for continue discoving */
  if ( NextExpander != agNULL)
  {
    DM_DBG3(("dmDownStreamDiscovering: Found pNextExpander=%p discoveryStatus=0x%x\n", 
             NextExpander, onePortContext->discovery.status));

    switch (onePortContext->discovery.status)
    {
      /* If the discovery status is DISCOVERY_DOWN_STREAM */
    case DISCOVERY_DOWN_STREAM:
      /* Send report general for the next expander */
      DM_DBG3(("dmDownStreamDiscovering: DownStream pNextExpander=%p\n", NextExpander));
      DM_DBG3(("dmDownStreamDiscovering: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
      DM_DBG3(("dmDownStreamDiscovering: oneExpander %p did %d\n", oneDeviceData->dmExpander, oneDeviceData->dmExpander->id));
      
      DM_DBG3(("dmDownStreamDiscovering: 2nd oneDeviceData %p did %d\n", NextExpander->dmDevice, NextExpander->dmDevice->id));
      DM_DBG3(("dmDownStreamDiscovering: 2nd oneExpander %p did %d\n", NextExpander, NextExpander->id));
      DM_DBG3(("dmDownStreamDiscovering: 2nd used oneExpander %p did %d\n", NextExpander->dmDevice->dmExpander, NextExpander->dmDevice->dmExpander->id));
      
      if (NextExpander != NextExpander->dmDevice->dmExpander)
      {
        DM_DBG3(("dmDownStreamDiscovering: wrong!!!\n"));
      }
      
	          
      dmReportGeneralSend(dmRoot, NextExpander->dmDevice);            
      break;
      /* If the discovery status is DISCOVERY_CONFIG_ROUTING */
    case DISCOVERY_CONFIG_ROUTING:
    case DISCOVERY_REPORT_PHY_SATA:

      /* set discovery status */
      onePortContext->discovery.status = DISCOVERY_DOWN_STREAM;
      
      DM_DBG3(("dmDownStreamDiscovering: pPort->discovery.status=DISCOVERY_CONFIG_ROUTING, make it DOWN_STREAM\n"));
      /* If not the last phy */    
      if ( NextExpander->discoveringPhyId < NextExpander->dmDevice->numOfPhys )
      {      
        DM_DBG3(("dmDownStreamDiscovering: pNextExpander->discoveringPhyId=0x%x pNextExpander->numOfPhys=0x%x.  Send More Discover\n",
                 NextExpander->discoveringPhyId, NextExpander->dmDevice->numOfPhys));
        /* Send discover for the next expander */
        dmDiscoverSend(dmRoot, NextExpander->dmDevice);                  
        }
      /* If it's the last phy */    
      else
      {
        DM_DBG3(("dmDownStreamDiscovering: Last Phy, remove expander%p  start DownStream=%p\n",
                 NextExpander, NextExpander->dmDevice));
        dmDiscoveringExpanderRemove(dmRoot, onePortContext, NextExpander);
        dmDownStreamDiscovering(dmRoot, onePortContext, NextExpander->dmDevice);
      }
      break;
      
    default:
      DM_DBG3(("dmDownStreamDiscovering: *** Unknown pPort->discovery.status=0x%x\n", onePortContext->discovery.status));
    }
  }
  /* If no expander for continue discoving */
  else
  {
    DM_DBG3(("dmDownStreamDiscovering: No more expander DONE\n"));
    /* discover done */
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_SUCCESS);
  }  
  
  
  return;
}		       

osGLOBAL void
dmUpStreamDiscoverExpanderPhy(
                              dmRoot_t              *dmRoot,
                              dmIntPortContext_t    *onePortContext,
                              dmExpander_t          *oneExpander,
                              smpRespDiscover_t     *pDiscoverResp
                             )
{
  agsaSASIdentify_t       sasIdentify;
  dmSASSubID_t            dmSASSubID;
  bit32                   attachedSasHi, attachedSasLo;
  dmExpander_t            *AttachedExpander = agNULL;
  bit8                    connectionRate;
  dmDeviceData_t          *oneDeviceData = agNULL;
  dmDeviceData_t          *AttachedDevice = agNULL;
  dmIntRoot_t             *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t          *dmAllShared  = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  
  
  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: start\n"));
  
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmUpStreamDiscoverExpanderPhy: invalid port or aborted discovery!!!\n"));  
    return;
  }
  
  if (oneExpander != oneExpander->dmDevice->dmExpander)
  {
    DM_DBG1(("dmUpStreamDiscoverExpanderPhy: wrong!!!\n"));
  }
  
  dm_memset(&sasIdentify, 0, sizeof(agsaSASIdentify_t));
    
  oneDeviceData = oneExpander->dmDevice;
 
  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: Phy #%d of SAS %08x-%08x\n",
           oneExpander->discoveringPhyId,
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));
  
  DM_DBG3(("   Attached device: %s\n",
           ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 0 ? "No Device" : 
             (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 1 ? "End Device" : 
              (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 2 ? "Edge Expander" : "Fanout Expander")))));
  
  
  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
  {
    DM_DBG3(("   SAS address    : %08x-%08x\n",
      DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp), 
              DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp)));
    DM_DBG3(("   SSP Target     : %d\n", DISCRSP_IS_SSP_TARGET(pDiscoverResp)?1:0));
    DM_DBG3(("   STP Target     : %d\n", DISCRSP_IS_STP_TARGET(pDiscoverResp)?1:0));
    DM_DBG3(("   SMP Target     : %d\n", DISCRSP_IS_SMP_TARGET(pDiscoverResp)?1:0));
    DM_DBG3(("   SATA DEVICE    : %d\n", DISCRSP_IS_SATA_DEVICE(pDiscoverResp)?1:0));
    DM_DBG3(("   SSP Initiator  : %d\n", DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG3(("   STP Initiator  : %d\n", DISCRSP_IS_STP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG3(("   SMP Initiator  : %d\n", DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG3(("   Phy ID         : %d\n", pDiscoverResp->phyIdentifier));
    DM_DBG3(("   Attached Phy ID: %d\n", pDiscoverResp->attachedPhyIdentifier)); 
  }
  
  /* for debugging */
  if (oneExpander->discoveringPhyId != pDiscoverResp->phyIdentifier)
  {
    DM_DBG1(("dmUpStreamDiscoverExpanderPhy: !!! Incorrect SMP response !!!\n"));
    DM_DBG1(("dmUpStreamDiscoverExpanderPhy: Request PhyID #%d Response PhyID #%d !!!\n", oneExpander->discoveringPhyId, pDiscoverResp->phyIdentifier));
    dmhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover_t));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
    return;
  }
  
  /* saving routing attribute for non self-configuring expanders */
  oneExpander->routingAttribute[pDiscoverResp->phyIdentifier] = (bit8)DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp);
  
  if ( oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE )
  {
    DM_DBG3(("dmUpStreamDiscoverExpanderPhy: SA_SAS_DEV_TYPE_FANOUT_EXPANDER\n"));
    if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
    {
      DM_DBG1(("dmUpStreamDiscoverExpanderPhy: **** Topology Error subtractive routing on fanout expander device!!!\n"));

      /* discovery error */
      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
        = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
        = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
      DM_DBG1(("dmUpStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));

      /* (2.1.3) discovery done */
      dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      return;        
    }    
  }
  else
  {
    DM_DBG3(("dmUpStreamDiscoverExpanderPhy: SA_SAS_DEV_TYPE_EDGE_EXPANDER\n"));
    if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
    {
      /* Setup sasIdentify for the attached device */
      sasIdentify.phyIdentifier = pDiscoverResp->phyIdentifier;
      sasIdentify.deviceType_addressFrameType = (bit8)(pDiscoverResp->attachedDeviceType & 0x70);
      sasIdentify.initiator_ssp_stp_smp = pDiscoverResp->attached_Ssp_Stp_Smp_Sata_Initiator;
      sasIdentify.target_ssp_stp_smp = pDiscoverResp->attached_SataPS_Ssp_Stp_Smp_Sata_Target;
      *(bit32*)sasIdentify.sasAddressHi = *(bit32*)pDiscoverResp->attachedSasAddressHi;
      *(bit32*)sasIdentify.sasAddressLo = *(bit32*)pDiscoverResp->attachedSasAddressLo;

      /* incremental discovery */       
      dmSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
      dmSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
      dmSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
      dmSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;
       
      attachedSasHi = DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp);
      attachedSasLo = DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp);
 
      /* If the phy has subtractive routing attribute */
      if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
      {       
        DM_DBG3(("dmUpStreamDiscoverExpanderPhy: SA_SAS_ROUTING_SUBTRACTIVE\n"));
        /* Setup upstream phys */
        dmExpanderUpStreamPhyAdd(dmRoot, oneExpander, (bit8) pDiscoverResp->attachedPhyIdentifier);
        /* If the expander already has an upsteam device set up */
        if (oneExpander->hasUpStreamDevice == agTRUE)
        {
          /* just to update MCN */          
          dmPortSASDeviceFind(dmRoot, onePortContext, attachedSasLo, attachedSasHi, oneDeviceData);
          /* If the sas address doesn't match */
          if ( ((oneExpander->upStreamSASAddressHi != attachedSasHi) ||
                (oneExpander->upStreamSASAddressLo != attachedSasLo)) &&
               (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE ||
                DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
              )
          {
            /* TODO: discovery error, callback */
            DM_DBG1(("dmUpStreamDiscoverExpanderPhy: **** Topology Error subtractive routing error - inconsistent SAS address!!!\n"));
            /* call back to notify discovery error */
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            DM_DBG1(("dmUpStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
            /* discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
        }
        else
        {
          /* Setup SAS address for up stream device */
          oneExpander->hasUpStreamDevice = agTRUE;
          oneExpander->upStreamSASAddressHi = attachedSasHi;
          oneExpander->upStreamSASAddressLo = attachedSasLo;
          if ( (onePortContext->sasLocalAddressHi != attachedSasHi)
              || (onePortContext->sasLocalAddressLo != attachedSasLo) )
          {
            /* Find the device from the discovered list */
            AttachedDevice = dmPortSASDeviceFind(dmRoot, onePortContext, attachedSasLo, attachedSasHi, oneDeviceData);
            /* New device, If the device has been discovered before */
            if ( AttachedDevice != agNULL) /* old device */
            {
              DM_DBG3(("dmUpStreamDiscoverExpanderPhy: Seen This Device Before\n"));
              /* If attached device is an edge expander */
              if ( AttachedDevice->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
              {
                /* The attached device is an expander */
                AttachedExpander = AttachedDevice->dmExpander;
                /* If the two expanders are the root of the two edge expander sets */
                if ( (AttachedExpander->upStreamSASAddressHi ==
                      DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo))
                     && (AttachedExpander->upStreamSASAddressLo ==
                        DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo)) )
                {
                  /* Setup upstream expander for the pExpander */
                  oneExpander->dmUpStreamExpander = AttachedExpander;                
                }
                /* If the two expanders are not the root of the two edge expander sets */
                else
                {
                  /* TODO: loop found, discovery error, callback */
                  DM_DBG1(("dmUpStreamDiscoverExpanderPhy: **** Topology Error loop detection!!!\n"));
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                  DM_DBG1(("dmUpStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                           onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                           onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                           onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));                  
		                /* discovery done */
                  dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
                }  
              }
              /* If attached device is not an edge expander */
              else
              {
                /*TODO: should not happen, ASSERT */
                DM_DBG3(("dmUpStreamDiscoverExpanderPhy, *** Attached Device is not Edge. Confused!!!\n"));
              }
            } /* AttachedExpander != agNULL */
            /* New device, If the device has not been discovered before */
            else /* new device */
            {
              /* Add the device */    
              DM_DBG3(("dmUpStreamDiscoverExpanderPhy: New device\n"));
              /* read minimum rate from the configuration 
                 onePortContext->LinkRate is SPC's local link rate
              */
              connectionRate = (bit8)MIN(onePortContext->LinkRate, DISCRSP_GET_LINKRATE(pDiscoverResp));
              DM_DBG3(("dmUpStreamDiscoverExpanderPhy: link rate 0x%x\n", onePortContext->LinkRate));
              DM_DBG3(("dmUpStreamDiscoverExpanderPhy: negotiatedPhyLinkRate 0x%x\n", DISCRSP_GET_LINKRATE(pDiscoverResp))); 
              DM_DBG3(("dmUpStreamDiscoverExpanderPhy: connectionRate 0x%x\n", connectionRate));
              if (DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp))    
              {
                /* incremental discovery */
                if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
                {
                  AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                }
                else
                {
                  /* incremental discovery */
                  AttachedDevice = dmFindRegNValid(
                                                     dmRoot,
                                                     onePortContext,
                                                     &dmSASSubID
                                                     );
                  /* not registered and not valid; add this*/                                   
                  if (AttachedDevice == agNULL)
                  {
                    AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                  }
                }
              } /* DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp) */
              else
              {
                /* incremental discovery */
                if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
                {            
                  AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                }
                else
                {
                  /* incremental discovery */
                  AttachedDevice = dmFindRegNValid(
                                                     dmRoot,
                                                     onePortContext,
                                                     &dmSASSubID
                                                     );
                  /* not registered and not valid; add this*/
                  if (AttachedDevice == agNULL)
                  {
                    AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                  }                    
                }                                                    
              }
               /* If the device is added successfully */    
              if ( AttachedDevice != agNULL)
              {

                 /* (3.1.2.3.2.3.2.1) callback about new device */
                if ( DISCRSP_IS_SSP_TARGET(pDiscoverResp) 
                    || DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)
                    || DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)
                    || DISCRSP_IS_SMP_INITIATOR(pDiscoverResp) )
                {
                  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: Found SSP/SMP SAS %08x-%08x\n",
                      attachedSasHi, attachedSasLo));
                }
                else
                {
                  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: Found a SAS STP device.\n"));
                }
                 /* If the attached device is an expander */
                if ( (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) 
                    || (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE) )
                {
                  /* Allocate an expander data structure */
                  AttachedExpander = dmDiscoveringExpanderAlloc(
                                                                dmRoot,
                                                                onePortContext,
                                                                AttachedDevice
								                                                       );
    
                  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: Found expander=%p\n", AttachedExpander));
                  /* If allocate successfully */
                  if ( AttachedExpander != agNULL)
                  {                  
                    /* Add the pAttachedExpander to discovering list */
                    dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
                    /* Setup upstream expander for the pExpander */
                    oneExpander->dmUpStreamExpander = AttachedExpander;
                  }
                  /* If failed to allocate */
                  else
                  {
                    DM_DBG1(("dmUpStreamDiscoverExpanderPhy: Failed to allocate expander data structure!!!\n"));
                    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
                  }
                }
                /* If the attached device is an end device */
                else
                {
                  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: Found end device\n"));
                  /* LP2006-05-26 added upstream device to the newly found device */
                  AttachedDevice->dmExpander = oneExpander;
                  oneExpander->dmUpStreamExpander = agNULL;
                }
              }
              else
              {
                DM_DBG1(("dmUpStreamDiscoverExpanderPhy: Failed to add a device!!!\n"));
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
              }
   
    
      
            } /* else, new device */
          } /* onePortContext->sasLocalAddressLo != attachedSasLo */
        } /* else */
      } /* DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE */
    } /* DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE */
  } /* big else */
  
  
  
   oneExpander->discoveringPhyId ++;
   if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
     {
       if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
       {
         DM_DBG3(("dmUpStreamDiscoverExpanderPhy: DISCOVERY_UP_STREAM find more ...\n"));
         /* continue discovery for the next phy */  
         dmDiscoverSend(dmRoot, oneDeviceData);
       }
       else
       {
         DM_DBG3(("dmUpStreamDiscoverExpanderPhy: DISCOVERY_UP_STREAM last phy continue upstream..\n"));

         /* for MCN */
         dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);         
         /* remove the expander from the discovering list */
         dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
         /* continue upstream discovering */  
         dmUpStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
       }
   }
   else
   {
      DM_DBG3(("dmUpStreamDiscoverExpanderPhy: onePortContext->discovery.status not in DISCOVERY_UP_STREAM; status %d\n", onePortContext->discovery.status));  
   
   }
   
  DM_DBG3(("dmUpStreamDiscoverExpanderPhy: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
  
  return;
}	   				

osGLOBAL void
dmUpStreamDiscover2ExpanderPhy(
                              dmRoot_t              *dmRoot,
                              dmIntPortContext_t    *onePortContext,
                              dmExpander_t          *oneExpander,
                              smpRespDiscover2_t    *pDiscoverResp
                              )
{
  dmDeviceData_t          *oneDeviceData;
  dmDeviceData_t          *AttachedDevice = agNULL;
  dmExpander_t            *AttachedExpander;    
  agsaSASIdentify_t       sasIdentify;
  bit8                    connectionRate;
  bit32                   attachedSasHi, attachedSasLo;
  dmSASSubID_t            dmSASSubID;
  dmIntRoot_t             *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t          *dmAllShared  = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  
  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: start\n"));
  
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: invalid port or aborted discovery!!!\n"));  
    return;
  }
  
  if (oneExpander != oneExpander->dmDevice->dmExpander)
  {
    DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: wrong!!!\n"));
  }
  
  dm_memset(&sasIdentify, 0, sizeof(agsaSASIdentify_t));
    
  oneDeviceData = oneExpander->dmDevice;
  
  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: Phy #%d of SAS %08x-%08x\n",
           oneExpander->discoveringPhyId,
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));
  
  DM_DBG2(("   Attached device: %s\n",
           ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 0 ? "No Device" : 
             (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 1 ? "End Device" : 
              (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 2 ? "Edge Expander" : "Fanout Expander")))));
  

  if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
  {
    DM_DBG2(("   SAS address    : %08x-%08x\n",
      SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp), 
              SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp)));
    DM_DBG2(("   SSP Target     : %d\n", SAS2_DISCRSP_IS_SSP_TARGET(pDiscoverResp)?1:0));
    DM_DBG2(("   STP Target     : %d\n", SAS2_DISCRSP_IS_STP_TARGET(pDiscoverResp)?1:0));
    DM_DBG2(("   SMP Target     : %d\n", SAS2_DISCRSP_IS_SMP_TARGET(pDiscoverResp)?1:0));
    DM_DBG2(("   SATA DEVICE    : %d\n", SAS2_DISCRSP_IS_SATA_DEVICE(pDiscoverResp)?1:0));
    DM_DBG2(("   SSP Initiator  : %d\n", SAS2_DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG2(("   STP Initiator  : %d\n", SAS2_DISCRSP_IS_STP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG2(("   SMP Initiator  : %d\n", SAS2_DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG2(("   Phy ID         : %d\n", pDiscoverResp->phyIdentifier));
    DM_DBG2(("   Attached Phy ID: %d\n", pDiscoverResp->attachedPhyIdentifier)); 
  }
  
  if (oneExpander->discoveringPhyId != pDiscoverResp->phyIdentifier)
  {
    DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: !!! Incorrect SMP response !!!\n"));
    DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: Request PhyID #%d Response PhyID #%d\n", oneExpander->discoveringPhyId, pDiscoverResp->phyIdentifier));
    dmhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover2_t));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
    return;
  }
 
  /* saving routing attribute for non self-configuring expanders */
  oneExpander->routingAttribute[pDiscoverResp->phyIdentifier] = SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp);
  
  if ( oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE )
  {
    DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: SA_SAS_DEV_TYPE_FANOUT_EXPANDER\n"));
    if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
    {
      DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: **** Topology Error subtractive routing on fanout expander device!!!\n"));

      /* discovery error */
      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
        = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
        = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
      DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));

      /* (2.1.3) discovery done */
      dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
      return;        
    }    
  }
  else
  {
    DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: SA_SAS_DEV_TYPE_EDGE_EXPANDER\n"));
    
    if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
    {
      /* Setup sasIdentify for the attached device */
      sasIdentify.phyIdentifier = pDiscoverResp->phyIdentifier;
      sasIdentify.deviceType_addressFrameType = pDiscoverResp->attachedDeviceTypeReason & 0x70;
      sasIdentify.initiator_ssp_stp_smp = pDiscoverResp->attached_Ssp_Stp_Smp_Sata_Initiator;
      sasIdentify.target_ssp_stp_smp = pDiscoverResp->attached_SataPS_Ssp_Stp_Smp_Sata_Target;
      *(bit32*)sasIdentify.sasAddressHi = *(bit32*)pDiscoverResp->attachedSasAddressHi;
      *(bit32*)sasIdentify.sasAddressLo = *(bit32*)pDiscoverResp->attachedSasAddressLo;

      /* incremental discovery */       
      dmSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
      dmSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
      dmSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
      dmSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;
       
      attachedSasHi = SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp);
      attachedSasLo = SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp);
      
      /* If the phy has subtractive routing attribute */
      if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
      {       
        DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: SA_SAS_ROUTING_SUBTRACTIVE\n"));
        /* Setup upstream phys */
        dmExpanderUpStreamPhyAdd(dmRoot, oneExpander, (bit8) pDiscoverResp->attachedPhyIdentifier);
        /* If the expander already has an upsteam device set up */
        if (oneExpander->hasUpStreamDevice == agTRUE)
        {
          /* just to update MCN */          
          dmPortSASDeviceFind(dmRoot, onePortContext, attachedSasLo, attachedSasHi, oneDeviceData);          
          /* If the sas address doesn't match */
          if ( ((oneExpander->upStreamSASAddressHi != attachedSasHi) ||
                (oneExpander->upStreamSASAddressLo != attachedSasLo)) &&
               (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE ||
                SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
              )
          {
            /* TODO: discovery error, callback */
            DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: **** Topology Error subtractive routing error - inconsistent SAS address!!!\n"));
            /* call back to notify discovery error */
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
            /* discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
        }
        else
        {
          /* Setup SAS address for up stream device */
          oneExpander->hasUpStreamDevice = agTRUE;
          oneExpander->upStreamSASAddressHi = attachedSasHi;
          oneExpander->upStreamSASAddressLo = attachedSasLo;

          if ( (onePortContext->sasLocalAddressHi != attachedSasHi)
              || (onePortContext->sasLocalAddressLo != attachedSasLo) )
          {
            /* Find the device from the discovered list */
            AttachedDevice = dmPortSASDeviceFind(dmRoot, onePortContext, attachedSasLo, attachedSasHi, oneDeviceData);
            /* If the device has been discovered before */
            if ( AttachedDevice != agNULL)
            {
              DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: Seen This Device Before\n"));
              /* If attached device is an edge expander */
              if ( AttachedDevice->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE)
              {
                /* The attached device is an expander */
                AttachedExpander = AttachedDevice->dmExpander;
                /* If the two expanders are the root of the two edge expander sets */
                if ( (AttachedExpander->upStreamSASAddressHi ==
                      DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo))
                     && (AttachedExpander->upStreamSASAddressLo ==
                        DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo)) )
                {
                  /* Setup upstream expander for the pExpander */
                  oneExpander->dmUpStreamExpander = AttachedExpander;                
                }
                /* If the two expanders are not the root of the two edge expander sets */
                else
                {
                  /* TODO: loop found, discovery error, callback */
                  DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: **** Topology Error loop detection!!!\n"));
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                  DM_DBG1(("dmUpStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
                  /* discovery done */
                  dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
                }  
              }
              /* If attached device is not an edge expander */
              else
              {
                /*TODO: should not happen, ASSERT */
                DM_DBG1(("dmUpStreamDiscover2ExpanderPhy, *** Attached Device is not Edge. Confused!!!\n"));
              }
            }
            /* If the device has not been discovered before */
            else
            {
              /* Add the device */    
              DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: New device\n"));
              /* read minimum rate from the configuration 
                 onePortContext->LinkRate is SPC's local link rate
              */
              connectionRate = MIN(onePortContext->LinkRate, SAS2_DISCRSP_GET_LOGICAL_LINKRATE(pDiscoverResp));
              DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: link rate 0x%x\n", onePortContext->LinkRate));
              DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: negotiatedPhyLinkRate 0x%x\n", SAS2_DISCRSP_GET_LINKRATE(pDiscoverResp))); 
              DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: connectionRate 0x%x\n", connectionRate));
              //hhhhhhhh
              if (SAS2_DISCRSP_IS_STP_TARGET(pDiscoverResp) || SAS2_DISCRSP_IS_SATA_DEVICE(pDiscoverResp))    
              {
                /* incremental discovery */
                if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
                {
                  AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                }
                else
                {
                  /* incremental discovery */
                  AttachedDevice = dmFindRegNValid(
                                                     dmRoot,
                                                     onePortContext,
                                                     &dmSASSubID
                                                     );
                  /* not registered and not valid; add this*/                                   
                  if (AttachedDevice == agNULL)
                  {
                    AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                  }
                }
              }
              else
              {
                /* incremental discovery */
                if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
                {            
                  AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                }
                else
                {
                  /* incremental discovery */
                  AttachedDevice = dmFindRegNValid(
                                                     dmRoot,
                                                     onePortContext,
                                                     &dmSASSubID
                                                     );
                  /* not registered and not valid; add this*/
                  if (AttachedDevice == agNULL)
                  {
                    AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
                  }                    
                }                                                    
              }
              /* If the device is added successfully */    
              if ( AttachedDevice != agNULL)
              {

                 /* (3.1.2.3.2.3.2.1) callback about new device */
                if ( SAS2_DISCRSP_IS_SSP_TARGET(pDiscoverResp) 
                    || SAS2_DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)
                    || SAS2_DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)
                    || SAS2_DISCRSP_IS_SMP_INITIATOR(pDiscoverResp) )
                {
                  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: Found SSP/SMP SAS %08x-%08x\n",
                      attachedSasHi, attachedSasLo));
                }
                else
                {
                  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: Found a SAS STP device.\n"));
                }
                 /* If the attached device is an expander */
                if ( (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) 
                    || (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE) )
                {
                  /* Allocate an expander data structure */
                  AttachedExpander = dmDiscoveringExpanderAlloc(
                                                                dmRoot,
                                                                onePortContext,
                                                                AttachedDevice
                                                               );
    
                  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: Found expander=%p\n", AttachedExpander));
                  /* If allocate successfully */
                  if ( AttachedExpander != agNULL)
                  {                  
                    /* Add the pAttachedExpander to discovering list */
                    dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
                    /* Setup upstream expander for the pExpander */
                    oneExpander->dmUpStreamExpander = AttachedExpander;
                  }
                  /* If failed to allocate */
                  else
                  {
                    DM_DBG1(("dmUpStreamDiscover2ExpanderPhy, Failed to allocate expander data structure!!!\n"));
                    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
                  }
                }
                /* If the attached device is an end device */
                else
                {
                  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: Found end device\n"));
                  /* LP2006-05-26 added upstream device to the newly found device */
                  AttachedDevice->dmExpander = oneExpander;
                  oneExpander->dmUpStreamExpander = agNULL;
                }
              }
              else
              {
                DM_DBG1(("dmUpStreamDiscover2ExpanderPhy, Failed to add a device!!!\n"));
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
              }
            }
          }
        }
      } /* substractive routing */
    }
  }
  
   oneExpander->discoveringPhyId ++;
   if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
     {
       if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
       {
         DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: DISCOVERY_UP_STREAM find more ...\n"));
         /* continue discovery for the next phy */  
         dmDiscoverSend(dmRoot, oneDeviceData);
       }
       else
       {
         DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: DISCOVERY_UP_STREAM last phy continue upstream..\n"));

         /* for MCN */
         dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);         
         /* remove the expander from the discovering list */
         dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
         /* continue upstream discovering */  
         dmUpStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
       }
   }
   else
   {
      DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: onePortContext->discovery.status not in DISCOVERY_UP_STREAM; status %d\n", onePortContext->discovery.status));  
   
   }
   
  DM_DBG2(("dmUpStreamDiscover2ExpanderPhy: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
  
  return;
}			     


osGLOBAL void
dmDownStreamDiscoverExpanderPhy(
                                dmRoot_t              *dmRoot,
                                dmIntPortContext_t    *onePortContext,
                                dmExpander_t          *oneExpander,
                                smpRespDiscover_t     *pDiscoverResp
                               )
{
  agsaSASIdentify_t       sasIdentify;
  dmSASSubID_t            dmSASSubID;
  bit32                   attachedSasHi, attachedSasLo;
  dmExpander_t            *AttachedExpander;
  dmExpander_t            *UpStreamExpander;
  dmExpander_t            *ConfigurableExpander = agNULL;
  bit8                    connectionRate, negotiatedPhyLinkRate;
  bit32                   configSASAddressHi;
  bit32                   configSASAddressLo;
  bit32                   dupConfigSASAddr = agFALSE;
  dmDeviceData_t          *oneDeviceData;
  dmDeviceData_t          *AttachedDevice = agNULL;
  bit32                   SAS2SAS11Check = agFALSE;
  dmIntRoot_t             *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t          *dmAllShared  = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  
  
  
  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: start\n"));
  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));	
  
  DM_ASSERT(dmRoot, "(dmDownStreamDiscoverExpanderPhy) dmRoot NULL");
  DM_ASSERT(onePortContext, "(dmDownStreamDiscoverExpanderPhy) pPort NULL");
  DM_ASSERT(oneExpander, "(dmDownStreamDiscoverExpanderPhy) pExpander NULL");
  DM_ASSERT(pDiscoverResp, "(dmDownStreamDiscoverExpanderPhy) pDiscoverResp NULL");

  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: onePortContxt=%p  oneExpander=%p\n", onePortContext, oneExpander));
           
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmDownStreamDiscoverExpanderPhy: invalid port or aborted discovery!!!\n"));  
    return;
  }
  
  if (oneExpander != oneExpander->dmDevice->dmExpander)
  {
    DM_DBG1(("dmDownStreamDiscoverExpanderPhy: wrong!!!\n"));
  }
  
  /* (1) Find the device structure of the expander */
  oneDeviceData = oneExpander->dmDevice;
  
  DM_ASSERT(oneDeviceData, "(dmDownStreamDiscoverExpanderPhy) pDevice NULL");
  
  /* for debugging */
  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Phy #%d of SAS %08x-%08x\n",
           oneExpander->discoveringPhyId,
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));
  
  DM_DBG3(("   Attached device: %s\n",
           ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 0 ? "No Device" : 
             (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 1 ? "End Device" : 
              (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 2 ? "Edge Expander" : "Fanout Expander")))));
  
  
  /* for debugging */
  if (oneExpander->discoveringPhyId != pDiscoverResp->phyIdentifier)
  {
    DM_DBG1(("dmDownStreamDiscoverExpanderPhy: !!! Incorrect SMP response !!!\n"));
    DM_DBG1(("dmDownStreamDiscoverExpanderPhy: Request PhyID #%d Response PhyID #%d !!!\n", oneExpander->discoveringPhyId, pDiscoverResp->phyIdentifier));
    dmhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover_t));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
    return;
  }
  
  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
  {
    DM_DBG3(("   SAS address    : %08x-%08x\n",
      DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp), 
              DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp)));
    DM_DBG3(("   SSP Target     : %d\n", DISCRSP_IS_SSP_TARGET(pDiscoverResp)?1:0));
    DM_DBG3(("   STP Target     : %d\n", DISCRSP_IS_STP_TARGET(pDiscoverResp)?1:0));
    DM_DBG3(("   SMP Target     : %d\n", DISCRSP_IS_SMP_TARGET(pDiscoverResp)?1:0));
    DM_DBG3(("   SATA DEVICE    : %d\n", DISCRSP_IS_SATA_DEVICE(pDiscoverResp)?1:0));
    DM_DBG3(("   SSP Initiator  : %d\n", DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG3(("   STP Initiator  : %d\n", DISCRSP_IS_STP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG3(("   SMP Initiator  : %d\n", DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG3(("   Phy ID         : %d\n", pDiscoverResp->phyIdentifier));
    DM_DBG3(("   Attached Phy ID: %d\n", pDiscoverResp->attachedPhyIdentifier));
    
  }
  /* end for debugging */
  
  /* saving routing attribute for non self-configuring expanders */
  oneExpander->routingAttribute[pDiscoverResp->phyIdentifier] = DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp);
  
  oneExpander->discoverSMPAllowed = agTRUE;
  
  /* If a device is attached */
  if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) !=  SAS_NO_DEVICE)
  {
    /* Setup sasIdentify for the attached device */
    sasIdentify.phyIdentifier = pDiscoverResp->phyIdentifier;
    sasIdentify.deviceType_addressFrameType = pDiscoverResp->attachedDeviceType & 0x70;
    sasIdentify.initiator_ssp_stp_smp = pDiscoverResp->attached_Ssp_Stp_Smp_Sata_Initiator;
    sasIdentify.target_ssp_stp_smp = pDiscoverResp->attached_SataPS_Ssp_Stp_Smp_Sata_Target;
    *(bit32*)sasIdentify.sasAddressHi = *(bit32*)pDiscoverResp->attachedSasAddressHi;
    *(bit32*)sasIdentify.sasAddressLo = *(bit32*)pDiscoverResp->attachedSasAddressLo;

    /* incremental discovery */       
    dmSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
    dmSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
    dmSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
    dmSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;
        
    attachedSasHi = DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp);
    attachedSasLo = DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp);
  
    /* If it's a direct routing */
    if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_DIRECT)
    {
      /* If the attached device is an expander */
      if ( (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
          || (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) )

      {
        DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error direct routing can't connect to expander!!!\n"));
        onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
           = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
        onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
          = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
        onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
        DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));

        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
        return;
      }
    }
  
    /* If the expander's attached device is not myself */
    if ( (attachedSasHi != onePortContext->sasLocalAddressHi)
         || (attachedSasLo != onePortContext->sasLocalAddressLo) ) 
    {
      /* Find the attached device from discovered list */
      AttachedDevice = dmPortSASDeviceFind(dmRoot, onePortContext, attachedSasLo, attachedSasHi, oneDeviceData);
      /* If the device has not been discovered before */
      if ( AttachedDevice == agNULL) //11
      {
        /* If the phy has subtractive routing attribute */
        if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE &&
             (DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE ||
              DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
           )
        {
          /* TODO: discovery error, callback */
          DM_DBG1(("dmDownStreamDiscoverExpanderPhy: Deferred!!! **** Topology Error subtractive routing error - inconsistent SAS address!!!\n"));
          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
            = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
            = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
          onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
          DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));

          onePortContext->discovery.DeferredError = agTRUE;
        }
        else /* 11 */
        {
          /* Add the device */
          /* read minimum rate from the configuration 
             onePortContext->LinkRate is SPC's local link rate
          */
          connectionRate = MIN(onePortContext->LinkRate, DISCRSP_GET_LINKRATE(pDiscoverResp)); 
          DM_DBG3(("dmDownStreamDiscoverExpanderPhy: link rate 0x%x\n", DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo)));
          DM_DBG3(("dmDownStreamDiscoverExpanderPhy: negotiatedPhyLinkRate 0x%x\n", DISCRSP_GET_LINKRATE(pDiscoverResp)));
          DM_DBG3(("dmDownStreamDiscoverExpanderPhy: connectionRate 0x%x\n", connectionRate));
          if (DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp))    
          {
            if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
            {
              AttachedDevice = dmPortSASDeviceAdd(
                                                  dmRoot,
                                                  onePortContext,
                                                  sasIdentify,
                                                  agFALSE,
                                                  connectionRate,
                                                  dmAllShared->itNexusTimeout,
                                                  0,
                                                  STP_DEVICE_TYPE,
                                                  oneDeviceData,
                                                  oneExpander,
                                                  pDiscoverResp->phyIdentifier
                                                  );
            }
            else
            {
              /* incremental discovery */
              AttachedDevice = dmFindRegNValid(
                                                 dmRoot,
                                                 onePortContext,
                                                 &dmSASSubID
                                                 );
              /* not registered and not valid; add this*/                                   
              if (AttachedDevice == agNULL)
              {
                AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    STP_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
              }
            }
	  } /* DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp) */
          else /* 22 */
          {
            if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
            {            
              AttachedDevice = dmPortSASDeviceAdd(
                                                  dmRoot,
                                                  onePortContext,
                                                  sasIdentify,
                                                  agFALSE,
                                                  connectionRate,
                                                  dmAllShared->itNexusTimeout,
                                                  0,
                                                  SAS_DEVICE_TYPE,
                                                  oneDeviceData,
                                                  oneExpander,
                                                  pDiscoverResp->phyIdentifier
                                                  );
            }
            else
            {
              /* incremental discovery */
              AttachedDevice = dmFindRegNValid(
                                              dmRoot,
                                              onePortContext,
                                              &dmSASSubID
                                              );
              /* not registered and not valid; add this*/
              if (AttachedDevice == agNULL)
              {
                AttachedDevice = dmPortSASDeviceAdd(
                                                   dmRoot,
                                                   onePortContext,
                                                   sasIdentify,
                                                   agFALSE,
                                                   connectionRate,
                                                   dmAllShared->itNexusTimeout,
                                                   0,
                                                   SAS_DEVICE_TYPE,
                                                   oneDeviceData,
                                                   oneExpander,
                                                   pDiscoverResp->phyIdentifier
                                                   );
              }                    
            }                                                    
	  } /* else 22 */
          DM_DBG3(("dmDownStreamDiscoverExpanderPhy: newDevice  pDevice=%p\n", AttachedDevice));
          /* If the device is added successfully */    
          if ( AttachedDevice != agNULL)
          {
            if ( SA_IDFRM_IS_SSP_TARGET(&sasIdentify) 
                 || SA_IDFRM_IS_SMP_TARGET(&sasIdentify)
                 || SA_IDFRM_IS_SSP_INITIATOR(&sasIdentify)
                 || SA_IDFRM_IS_SMP_INITIATOR(&sasIdentify) )
            {
              DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Report a new SAS device !!\n"));  
               
            }
            else
            {
              if ( SA_IDFRM_IS_STP_TARGET(&sasIdentify) || 
                   SA_IDFRM_IS_SATA_DEVICE(&sasIdentify) )
              {
                
                DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Found an STP or SATA device.\n"));
              }
              else
              {
                DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Found Other type of device.\n"));
              }
            }
	    
            /* LP2006-05-26 added upstream device to the newly found device */
            AttachedDevice->dmExpander = oneExpander;
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: AttachedDevice %p did %d\n", AttachedDevice, AttachedDevice->id));
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Attached oneExpander %p did %d\n",  AttachedDevice->dmExpander,  AttachedDevice->dmExpander->id));
	    
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: oneExpander %p did %d\n",  oneDeviceData->dmExpander,  oneDeviceData->dmExpander->id));
            
	    /* If the phy has table routing attribute */
            if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE)
            {
              /* If the attached device is a fan out expander */
              if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
              {
                /* TODO: discovery error, callback */
                DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error two table routing phys are connected!!!\n"));
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                          onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
                /* discovery done */
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
              }	
              else if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) 
              {
                /* Allocate an expander data structure */
                AttachedExpander = dmDiscoveringExpanderAlloc(dmRoot, onePortContext, AttachedDevice);
                 
                DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Found a EDGE exp device.%p\n", AttachedExpander));
                /* If allocate successfully */
                if ( AttachedExpander != agNULL)
                {
                  /* set up downstream information on configurable expander */              
                  dmExpanderDownStreamPhyAdd(dmRoot, oneExpander, (bit8) oneExpander->discoveringPhyId); 
                  /* Setup upstream information */
                  dmExpanderUpStreamPhyAdd(dmRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
                  AttachedExpander->hasUpStreamDevice = agTRUE;
                  AttachedExpander->upStreamSASAddressHi 
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  AttachedExpander->upStreamSASAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  AttachedExpander->dmUpStreamExpander = oneExpander;
                  /* (2.3.2.2.2.2.2.2.2) Add the pAttachedExpander to discovering list */
                  dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
                }	
                /* If failed to allocate */
                else
                {
                  DM_DBG1(("dmDownStreamDiscoverExpanderPhy: Failed to allocate expander data structure!!!\n"));
                  /*  discovery done */
                  dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
                }
              }	
	    } /* DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE */
            /* If status is still DISCOVERY_DOWN_STREAM */        
            if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
            {
              DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 1st before\n"));
              dmDumpAllUpExp(dmRoot, onePortContext, oneExpander); 
              UpStreamExpander = oneExpander->dmUpStreamExpander;
              ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
              configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
              configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
              if (ConfigurableExpander)
              { 
                if ( (ConfigurableExpander->dmDevice->SASAddressID.sasAddressHi 
                      == DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo)) &&
                     (ConfigurableExpander->dmDevice->SASAddressID.sasAddressLo 
                      == DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo))
                   )
                { /* directly attached between oneExpander and ConfigurableExpander */       
                  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 1st before loc 1\n"));
                  configSASAddressHi = oneExpander->dmDevice->SASAddressID.sasAddressHi;
                  configSASAddressLo = oneExpander->dmDevice->SASAddressID.sasAddressLo; 
                }
                else
                {
                  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 1st before loc 2\n"));
                  configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
                  configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
                }                                             
              } /* if !ConfigurableExpander */
	  
              dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot, 
                                                          ConfigurableExpander,   
                                                          configSASAddressHi,
                                                          configSASAddressLo
                                                          );
	  
              if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
              {
                DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 1st q123\n"));
                UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
                ConfigurableExpander->currentDownStreamPhyIndex = 
                        dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
                ConfigurableExpander->dmReturnginExpander = oneExpander;
                dmRoutingEntryAdd(dmRoot,
                                  ConfigurableExpander, 
                                  ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                                  configSASAddressHi,
                                  configSASAddressLo
                                 );
              }                       
            } /* onePortContext->discovery.status == DISCOVERY_DOWN_STREAM */
          } /* AttachedDevice != agNULL */  
          /*  If fail to add the device */    
          else
          {
            DM_DBG1(("dmDownStreamDiscoverExpanderPhy: Failed to add a device!!!\n"));
            /*  discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
        } /* else 11 */
      } /* AttachedDevice == agNULL */
      /* If the device has been discovered before */
      else /* haha discovered before 33 */
      {
        /* If the phy has subtractive routing attribute */
        if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
        {
          /* If the expander doesn't have up stream device */
          if ( oneExpander->hasUpStreamDevice == agFALSE)
          {
            /* TODO: discovery error, callback */
            DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error loop, or end device connects to two expanders!!!\n"));
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
            /* discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          /* If the expander has up stream device */
          else /* 44 */
          {
            /* If sas address doesn't match */
            if ( (oneExpander->upStreamSASAddressHi != attachedSasHi)
                 || (oneExpander->upStreamSASAddressLo != attachedSasLo) )
            {
              /* TODO: discovery error, callback */
              DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error two subtractive phys!!!\n"));
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
              DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                       onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                       onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                       onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
              /* discovery done */
              dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
            }
          } /* else 44 */	  
        } /* DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE */      
        /* If the phy has table routing attribute */
        else if ( DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE)
        {
          /* If the attached device is a fan out expander */
          if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
          {
            /* (2.3.3.2.1.1) TODO: discovery error, callback */
            DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error fan out expander to routing table phy!!!\n"));
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                     onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                     onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                     onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
            /* discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          /* If the attached device is an edge expander */
          else if ( DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) 
          {
            /* Setup up stream inform */
            AttachedExpander = AttachedDevice->dmExpander;
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Found edge expander=%p\n", AttachedExpander));
            /* If the attached expander has up stream device */
            if ( AttachedExpander->hasUpStreamDevice == agTRUE)
            {
              /* compare the sas address */
              if ( (AttachedExpander->upStreamSASAddressHi
                    != DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo))
                   || (AttachedExpander->upStreamSASAddressLo
                       != DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo)))
              {
                /* TODO: discovery error, callback */
                SAS2SAS11Check = dmSAS2SAS11ErrorCheck(dmRoot, onePortContext, AttachedExpander, oneExpander, oneExpander);
                if (SAS2SAS11Check == agTRUE)
                {
                   DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error SAS2 and SAS1.1!!!\n"));
                }
                else
                {
                  DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error two table routing phys connected (1)!!!\n"));
                }
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                         onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                         onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                         onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
                /* discovery done */
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
              }
              else
              {
                DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Add edge expander=%p\n", AttachedExpander));
                /* set up downstream information on configurable expander */

                dmExpanderDownStreamPhyAdd(dmRoot, oneExpander, (bit8) oneExpander->discoveringPhyId); 
                /* haha */
                dmExpanderUpStreamPhyAdd(dmRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
                /* Add the pAttachedExpander to discovering list */
                dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
              }
            } /* AttachedExpander->hasUpStreamDevice == agTRUE */      
            /* If the attached expander doesn't have up stream device */
            else
            {
              /* TODO: discovery error, callback */
              DM_DBG1(("dmDownStreamDiscoverExpanderPhy: **** Topology Error two table routing phys connected (2)!!!\n"));
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
              DM_DBG1(("dmDownStreamDiscoverExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                       onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                       onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                       onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
              /* discovery done */
              dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
            }
          } /* DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE */      
        } /* DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE */      
        /* do this regradless of sub or table */
        /* If status is still DISCOVERY_DOWN_STREAM */            
        if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
        {
          DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 2nd before\n"));
          dmDumpAllUpExp(dmRoot, onePortContext, oneExpander); 

          UpStreamExpander = oneExpander->dmUpStreamExpander;
          ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
          configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
          configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
          if (ConfigurableExpander)
          { 
            if ( (ConfigurableExpander->dmDevice->SASAddressID.sasAddressHi 
                 == DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo)) &&
                 (ConfigurableExpander->dmDevice->SASAddressID.sasAddressLo 
                   == DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo))
               )
            { /* directly attached between oneExpander and ConfigurableExpander */       
              DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 2nd before loc 1\n"));
              configSASAddressHi = oneExpander->dmDevice->SASAddressID.sasAddressHi;
              configSASAddressLo = oneExpander->dmDevice->SASAddressID.sasAddressLo; 
            }
            else
            {
              DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 2nd before loc 2\n"));
              configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
              configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
            }                                             
          } /* if !ConfigurableExpander */
          dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot, 
                                                      ConfigurableExpander,   
                                                      configSASAddressHi,
                                                      configSASAddressLo
                                                      );
          if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
          {
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 2nd q123 \n"));
            UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
            ConfigurableExpander->currentDownStreamPhyIndex = 
                        dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
            ConfigurableExpander->dmReturnginExpander = oneExpander;
            dmRoutingEntryAdd(dmRoot,
                              ConfigurableExpander, 
                              ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                              configSASAddressHi,
                              configSASAddressLo
                             );
          }                                        
        } /* onePortContext->discovery.status == DISCOVERY_DOWN_STREAM */	  
        /* incremental discovery */
        if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_INCREMENTAL_START)
        {
          connectionRate = MIN(onePortContext->LinkRate, DISCRSP_GET_LINKRATE(pDiscoverResp)); 

          if (DISCRSP_IS_STP_TARGET(pDiscoverResp) || DISCRSP_IS_SATA_DEVICE(pDiscoverResp))    
          {
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: incremental SATA_STP\n"));

            dmPortSASDeviceAdd(
                              dmRoot,
                              onePortContext,
                              sasIdentify,
                              agFALSE,
                              connectionRate,
                              dmAllShared->itNexusTimeout,
                              0,
                              STP_DEVICE_TYPE,
                              oneDeviceData,
                              oneExpander,
                              pDiscoverResp->phyIdentifier
                              );
          }
          else
          {
            DM_DBG3(("dmDownStreamDiscoverExpanderPhy: incremental SAS\n"));


             dmPortSASDeviceAdd(
                               dmRoot,
                               onePortContext,
                               sasIdentify,
                               agFALSE,
                               connectionRate,
                               dmAllShared->itNexusTimeout,
                               0,
                               SAS_DEVICE_TYPE,
                               oneDeviceData,
                               oneExpander,
                               pDiscoverResp->phyIdentifier
                               );
        
          }
        } /* onePortContext->discovery.type == DM_DISCOVERY_OPTION_INCREMENTAL_START */	
      } /* else 33 */  	    
    } /* (attachedSasLo != onePortContext->sasLocalAddressLo) */  
  
    else /* else 44 */
    {
      DM_DBG3(("dmDownStreamDiscoverExpanderPhy: Found Self\n"));
      DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 3rd before\n"));
      dmDumpAllUpExp(dmRoot, onePortContext, oneExpander); 

      UpStreamExpander = oneExpander->dmUpStreamExpander;
      ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
      dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot, 
                                                  ConfigurableExpander,   
                                                  onePortContext->sasLocalAddressHi,
                                                  onePortContext->sasLocalAddressLo
                                                  );
      
      if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
      {
        DM_DBG3(("dmDownStreamDiscoverExpanderPhy: 3rd q123 Setup routing table\n"));
        UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
        ConfigurableExpander->currentDownStreamPhyIndex = 
                        dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
        ConfigurableExpander->dmReturnginExpander = oneExpander;
        dmRoutingEntryAdd(dmRoot,
                          ConfigurableExpander, 
                          ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                          onePortContext->sasLocalAddressHi,
                          onePortContext->sasLocalAddressLo
                         );
      } 
    } /* else 44 */  
  } /* DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) !=  SAS_NO_DEVICE */
  /* If no device is attached */
  else
  {

   DM_DBG2(("!!!!!!!!!!!!!!!!!!!!! SPIN SATA !!!!!!!!!!!!!!!!!!!!!!!!!!!\n"));
   negotiatedPhyLinkRate =	DISCRSP_GET_LINKRATE(pDiscoverResp); // added by thenil

     if (negotiatedPhyLinkRate == 0x03)
     {

        DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: SPIN SATA sent reset\n"));
		dmPhyControlSend(dmRoot,
                            oneDeviceData, 
                            SMP_PHY_CONTROL_HARD_RESET, 
                                                           pDiscoverResp->phyIdentifier
                           );
    }
       
    /* do nothing */
  }
  
  
  /* Increment the discovering phy id */
  oneExpander->discoveringPhyId ++;
  
  /* If the discovery status is DISCOVERY_DOWN_STREAM */
  if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM )
  {
    /* If not the last phy */  
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      DM_DBG3(("dmDownStreamDiscoverExpanderPhy: More Phys to discover\n"));
      /* continue discovery for the next phy */
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
    /* If the last phy */
    else
    {
      DM_DBG3(("dmDownStreamDiscoverExpanderPhy: No More Phys\n"));

      /* for MCN */
      dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);  
      /* remove the expander from the discovering list */
      dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
      /* continue downstream discovering */
      dmDownStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }  
  }
  else
  {
    DM_DBG3(("dmDownStreamDiscoverExpanderPhy: onePortContext->discovery.status not in DISCOVERY_DOWN_STREAM; status %d\n", onePortContext->discovery.status));  
  }  
  DM_DBG3(("dmDownStreamDiscoverExpanderPhy: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
 
  return;
}	   				


/* works at SAS2 expander (called in dmDownStreamDiscover2ExpanderPhy())
   if currentExpander is SAS2, called in dmDownStreamDiscover2ExpanderPhy()
   if currentExpander is SAS1.1, called in dmDownStreamDiscoverExpanderPhy()
*/
osGLOBAL bit32
dmSAS2SAS11ErrorCheck(
                      dmRoot_t              *dmRoot,
                      dmIntPortContext_t    *onePortContext,
                      dmExpander_t          *topExpander,
                      dmExpander_t          *bottomExpander,
		      dmExpander_t          *currentExpander
                     )
{
  bit32                   result = agFALSE, i = 0;
  bit8                    downStreamPhyID, upStreamPhyID; 
  
  DM_DBG2(("dmSAS2SAS11ErrorCheck: start\n"));
  
  if (topExpander == agNULL)
  {
    DM_DBG2(("dmSAS2SAS11ErrorCheck: topExpander is NULL\n"));
    return result;
  }
  if (bottomExpander == agNULL)
  {
    DM_DBG2(("dmSAS2SAS11ErrorCheck: bottomExpander is NULL\n"));
    return result;
  }
  
  if (currentExpander == agNULL)
  {
    DM_DBG2(("dmSAS2SAS11ErrorCheck: currentExpander is NULL\n"));
    return result;
  }
  
  DM_DBG2(("dmSAS2SAS11ErrorCheck: topExpander addrHi 0x%08x addrLo 0x%08x\n", 
            topExpander->dmDevice->SASAddressID.sasAddressHi, topExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG2(("dmSAS2SAS11ErrorCheck: bottomExpander addrHi 0x%08x addrLo 0x%08x\n", 
            bottomExpander->dmDevice->SASAddressID.sasAddressHi, bottomExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG2(("dmSAS2SAS11ErrorCheck: currentExpander addrHi 0x%08x addrLo 0x%08x\n", 
            currentExpander->dmDevice->SASAddressID.sasAddressHi, currentExpander->dmDevice->SASAddressID.sasAddressLo));
	    
  for (i=0;i<DM_MAX_EXPANDER_PHYS;i++)
  {
    downStreamPhyID = topExpander->downStreamPhys[i];
    upStreamPhyID = bottomExpander->upStreamPhys[i];
    if (currentExpander->SAS2 == 1)
    {
      if ( downStreamPhyID ==  upStreamPhyID &&
           topExpander->routingAttribute[downStreamPhyID] == SAS_ROUTING_TABLE &&
           bottomExpander->routingAttribute[i] == SAS_ROUTING_SUBTRACTIVE && 
           topExpander->SAS2 == 0 &&
           bottomExpander->SAS2 == 1
         )
      {
        result = agTRUE;
        break;
      }
    }	 
    else if (currentExpander->SAS2 == 0)
    {
      if ( downStreamPhyID ==  upStreamPhyID &&
           topExpander->routingAttribute[downStreamPhyID] == SAS_ROUTING_SUBTRACTIVE &&
           bottomExpander->routingAttribute[i] == SAS_ROUTING_TABLE &&
           topExpander->SAS2 == 1 &&
           bottomExpander->SAS2 == 0
         )
      {
        result = agTRUE;
        break;
      }
    }
  }
  return result;
}		     		     

osGLOBAL void
dmDownStreamDiscover2ExpanderPhy(
                                dmRoot_t              *dmRoot,
                                dmIntPortContext_t    *onePortContext,
                                dmExpander_t          *oneExpander,
                                smpRespDiscover2_t     *pDiscoverResp
                                )
{
  dmDeviceData_t          *oneDeviceData;
  dmExpander_t            *UpStreamExpander;
  dmDeviceData_t          *AttachedDevice = agNULL;
  dmExpander_t            *AttachedExpander;
  agsaSASIdentify_t       sasIdentify;
  bit8                    connectionRate;
  bit32                   attachedSasHi, attachedSasLo;
  dmSASSubID_t            dmSASSubID;
  dmExpander_t            *ConfigurableExpander = agNULL;
  bit32                   dupConfigSASAddr = agFALSE;
  bit32                   configSASAddressHi;
  bit32                   configSASAddressLo;
  bit32                   SAS2SAS11Check = agFALSE;
  dmIntRoot_t             *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t          *dmAllShared  = (dmIntContext_t *)&dmIntRoot->dmAllShared;

  
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: start\n"));
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));	
	
  DM_ASSERT(dmRoot, "(dmDownStreamDiscover2ExpanderPhy) dmRoot NULL");
  DM_ASSERT(onePortContext, "(dmDownStreamDiscover2ExpanderPhy) pPort NULL");
  DM_ASSERT(oneExpander, "(dmDownStreamDiscover2ExpanderPhy) pExpander NULL");
  DM_ASSERT(pDiscoverResp, "(dmDownStreamDiscover2ExpanderPhy) pDiscoverResp NULL");

  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: onePortContxt=%p  oneExpander=%p  oneDeviceData=%p\n", onePortContext, oneExpander, oneExpander->dmDevice));
  
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: invalid port or aborted discovery!!!\n"));  
    return;
  }

  if (oneExpander != oneExpander->dmDevice->dmExpander)
  {
    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: wrong!!!\n"));
  }
      	              
           
  /* (1) Find the device structure of the expander */
  oneDeviceData = oneExpander->dmDevice;
  
  DM_ASSERT(oneDeviceData, "(dmDownStreamDiscover2ExpanderPhy) pDevice NULL");

  /* for debugging */
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Phy #%d of SAS %08x-%08x\n",
           oneExpander->discoveringPhyId,
           oneDeviceData->SASAddressID.sasAddressHi,
           oneDeviceData->SASAddressID.sasAddressLo));
  
  DM_DBG2(("   Attached device: %s\n",
           ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 0 ? "No Device" : 
             (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 1 ? "End Device" : 
              (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == 2 ? "Edge Expander" : "Fanout Expander")))));
              
  
  /* for debugging */
  if (oneExpander->discoveringPhyId != pDiscoverResp->phyIdentifier)
  {
    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: !!! Incorrect SMP response !!!\n"));
    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: Request PhyID #%d Response PhyID #%d\n", oneExpander->discoveringPhyId, pDiscoverResp->phyIdentifier));
    dmhexdump("NO_DEVICE", (bit8*)pDiscoverResp, sizeof(smpRespDiscover2_t));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
    return;
  }
  
  if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) != SAS_NO_DEVICE)
  {
    DM_DBG2(("   SAS address    : %08x-%08x\n",
      SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp), 
              SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp)));
    DM_DBG2(("   SSP Target     : %d\n", SAS2_DISCRSP_IS_SSP_TARGET(pDiscoverResp)?1:0));
    DM_DBG2(("   STP Target     : %d\n", SAS2_DISCRSP_IS_STP_TARGET(pDiscoverResp)?1:0));
    DM_DBG2(("   SMP Target     : %d\n", SAS2_DISCRSP_IS_SMP_TARGET(pDiscoverResp)?1:0));
    DM_DBG2(("   SATA DEVICE    : %d\n", SAS2_DISCRSP_IS_SATA_DEVICE(pDiscoverResp)?1:0));
    DM_DBG2(("   SSP Initiator  : %d\n", SAS2_DISCRSP_IS_SSP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG2(("   STP Initiator  : %d\n", SAS2_DISCRSP_IS_STP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG2(("   SMP Initiator  : %d\n", SAS2_DISCRSP_IS_SMP_INITIATOR(pDiscoverResp)?1:0));
    DM_DBG2(("   Phy ID         : %d\n", pDiscoverResp->phyIdentifier));
    DM_DBG2(("   Attached Phy ID: %d\n", pDiscoverResp->attachedPhyIdentifier));
    
  }

    /* saving routing attribute for non self-configuring expanders */
  oneExpander->routingAttribute[pDiscoverResp->phyIdentifier] = SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp);
 
  
  oneExpander->discoverSMPAllowed = agTRUE;
  
  /* If a device is attached */
  if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) !=  SAS_NO_DEVICE)
  {
    /* Setup sasIdentify for the attached device */
    sasIdentify.phyIdentifier = pDiscoverResp->phyIdentifier;
    sasIdentify.deviceType_addressFrameType = pDiscoverResp->attachedDeviceTypeReason & 0x70;
    sasIdentify.initiator_ssp_stp_smp = pDiscoverResp->attached_Ssp_Stp_Smp_Sata_Initiator;
    sasIdentify.target_ssp_stp_smp = pDiscoverResp->attached_SataPS_Ssp_Stp_Smp_Sata_Target;
    *(bit32*)sasIdentify.sasAddressHi = *(bit32*)pDiscoverResp->attachedSasAddressHi;
    *(bit32*)sasIdentify.sasAddressLo = *(bit32*)pDiscoverResp->attachedSasAddressLo;

    /* incremental discovery */       
    dmSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
    dmSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
    dmSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
    dmSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;
        
    attachedSasHi = SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSHI(pDiscoverResp);
    attachedSasLo = SAS2_DISCRSP_GET_ATTACHED_SAS_ADDRESSLO(pDiscoverResp);

    /* If it's a direct routing */
    if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_DIRECT)
    {
      /* If the attached device is an expander */
      if ( (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
          || (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) )

      {
        DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error direct routing can't connect to expander!!!\n"));
        onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
           = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
        onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
          = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
        onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;

        DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
        dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
        
        return;
      }
    }
    
    /* If the expander's attached device is not myself */
    if ( (attachedSasHi != onePortContext->sasLocalAddressHi)
         || (attachedSasLo != onePortContext->sasLocalAddressLo) ) 
    {
      /* Find the attached device from discovered list */
      AttachedDevice = dmPortSASDeviceFind(dmRoot, onePortContext, attachedSasLo, attachedSasHi, oneDeviceData);
      /* If the device has not been discovered before */
      if ( AttachedDevice == agNULL) //11
      {
        //qqqqqq
        if (0)	   
        {
	      DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error subtractive routing error - inconsistent SAS address!!!\n"));
          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
            = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
            = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
          onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
          DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                    onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                    onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                    onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
          /* discovery done */
          dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
        }
        else 
        {
          /* Add the device */
          /* read minimum rate from the configuration 
             onePortContext->LinkRate is SPC's local link rate
          */
          connectionRate = MIN(onePortContext->LinkRate, SAS2_DISCRSP_GET_LOGICAL_LINKRATE(pDiscoverResp)); 
          DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: link rate 0x%x\n", DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo)));
          DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: negotiatedPhyLinkRate 0x%x\n", SAS2_DISCRSP_GET_LINKRATE(pDiscoverResp)));
          DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: connectionRate 0x%x\n", connectionRate));

          if (SAS2_DISCRSP_IS_STP_TARGET(pDiscoverResp) || SAS2_DISCRSP_IS_SATA_DEVICE(pDiscoverResp))    
          {
            if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
            {
              AttachedDevice = dmPortSASDeviceAdd(
                                                 dmRoot,
                                                 onePortContext,
                                                 sasIdentify,
                                                 agFALSE,
                                                 connectionRate,
                                                 dmAllShared->itNexusTimeout,
                                                 0,
                                                 STP_DEVICE_TYPE,
                                                 oneDeviceData,
                                                 oneExpander,
                                                 pDiscoverResp->phyIdentifier
                                                 );
            }
            else
            {
              /* incremental discovery */
              AttachedDevice = dmFindRegNValid(
                                               dmRoot,
                                               onePortContext,
                                               &dmSASSubID
                                               );
              /* not registered and not valid; add this*/                                   
              if (AttachedDevice == agNULL)
              {
                AttachedDevice = dmPortSASDeviceAdd(
                                                   dmRoot,
                                                   onePortContext,
                                                   sasIdentify,
                                                   agFALSE,
                                                   connectionRate,
                                                   dmAllShared->itNexusTimeout,
                                                   0,
                                                   STP_DEVICE_TYPE,
                                                   oneDeviceData,
                                                   oneExpander,
                                                   pDiscoverResp->phyIdentifier
                                                   );
              }
            }
          }
          else
          {
            if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
            {            
              AttachedDevice = dmPortSASDeviceAdd(
                                                 dmRoot,
                                                 onePortContext,
                                                 sasIdentify,
                                                 agFALSE,
                                                 connectionRate,
                                                 dmAllShared->itNexusTimeout,
                                                 0,
                                                 SAS_DEVICE_TYPE,
                                                 oneDeviceData,
                                                 oneExpander,
                                                 pDiscoverResp->phyIdentifier
                                                 );
            }
            else
            {
              /* incremental discovery */
              AttachedDevice = dmFindRegNValid(
                                               dmRoot,
                                               onePortContext,
                                               &dmSASSubID
                                               );
              /* not registered and not valid; add this*/
              if (AttachedDevice == agNULL)
              {
                AttachedDevice = dmPortSASDeviceAdd(
                                                    dmRoot,
                                                    onePortContext,
                                                    sasIdentify,
                                                    agFALSE,
                                                    connectionRate,
                                                    dmAllShared->itNexusTimeout,
                                                    0,
                                                    SAS_DEVICE_TYPE,
                                                    oneDeviceData,
                                                    oneExpander,
                                                    pDiscoverResp->phyIdentifier
                                                    );
              }                    
            }                                                    
          }
          DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: newDevice  pDevice=%p\n", AttachedDevice));
          /* If the device is added successfully */    
          if ( AttachedDevice != agNULL)
          {
            if ( SA_IDFRM_IS_SSP_TARGET(&sasIdentify) 
                 || SA_IDFRM_IS_SMP_TARGET(&sasIdentify)
                 || SA_IDFRM_IS_SSP_INITIATOR(&sasIdentify)
                 || SA_IDFRM_IS_SMP_INITIATOR(&sasIdentify) )
            {
              DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Report a new SAS device !!\n"));  
               
            }
            else
            {
              if ( SA_IDFRM_IS_STP_TARGET(&sasIdentify) || 
                   SA_IDFRM_IS_SATA_DEVICE(&sasIdentify) )
              {
                
                DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Found an STP or SATA device.\n"));
              }
              else
              {
                DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Found Other type of device.\n"));
              }
            }
            
            /* LP2006-05-26 added upstream device to the newly found device */
            AttachedDevice->dmExpander = oneExpander;
            DM_DBG3(("dmDownStreamDiscover2ExpanderPhy: AttachedDevice %p did %d\n", AttachedDevice, AttachedDevice->id));
            DM_DBG3(("dmDownStreamDiscover2ExpanderPhy: Attached oneExpander %p did %d\n",  AttachedDevice->dmExpander,  AttachedDevice->dmExpander->id));
	    
            DM_DBG3(("dmDownStreamDiscover2ExpanderPhy: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
            DM_DBG3(("dmDownStreamDiscover2ExpanderPhy: oneExpander %p did %d\n",  oneDeviceData->dmExpander,  oneDeviceData->dmExpander->id));
						 						 
            /* If the phy has table routing attribute */
            if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE)
            {
              /* If the attached device is a fan out expander */
              if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
              {
                /* TODO: discovery error, callback */
                DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error two table routing phys are connected!!!\n"));
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
		          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
		          onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
                /* discovery done */
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
              }
              else if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) 
              {
                /* Allocate an expander data structure */
                AttachedExpander = dmDiscoveringExpanderAlloc(dmRoot, onePortContext, AttachedDevice);
                 
                DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Found a EDGE exp device.%p\n", AttachedExpander));
                /* If allocate successfully */
                if ( AttachedExpander != agNULL)
                {
                  /* set up downstream information on configurable expander */
 
                  dmExpanderDownStreamPhyAdd(dmRoot, oneExpander, (bit8) oneExpander->discoveringPhyId); 
             
                  /* Setup upstream information */
                  dmExpanderUpStreamPhyAdd(dmRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
//qqqqq		  
                  AttachedExpander->hasUpStreamDevice = agTRUE;
                  AttachedExpander->upStreamSASAddressHi 
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  AttachedExpander->upStreamSASAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  AttachedExpander->dmUpStreamExpander = oneExpander;
                  /* (2.3.2.2.2.2.2.2.2) Add the pAttachedExpander to discovering list */
                  dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
                }	
                /* If failed to allocate */
                else
                {
                  DM_DBG1(("dmDownStreamDiscover2ExpanderPhy, Failed to allocate expander data structure!!!\n"));
                  /*  discovery done */
                  dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
                }
              }	
            }
	    //qqqqq
	    else if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE &&
                       (SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE ||
                        SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE)		     	    
	            )
	    {
              /* Allocate an expander data structure */
              AttachedExpander = dmDiscoveringExpanderAlloc(dmRoot, onePortContext, AttachedDevice);
                 
              DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Found a EDGE/FANOUT exp device.%p\n", AttachedExpander));
              /* If allocate successfully */
              if ( AttachedExpander != agNULL)
              {
                /* set up downstream information on configurable expander */
                dmExpanderDownStreamPhyAdd(dmRoot, oneExpander, (bit8) oneExpander->discoveringPhyId); 
                
                /* Setup upstream information */
                dmExpanderUpStreamPhyAdd(dmRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
                AttachedExpander->hasUpStreamDevice = agTRUE;
                AttachedExpander->upStreamSASAddressHi 
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                AttachedExpander->upStreamSASAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                AttachedExpander->dmUpStreamExpander = oneExpander;
                /* (2.3.2.2.2.2.2.2.2) Add the pAttachedExpander to discovering list */
                dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
              }	
              /* If failed to allocate */
              else
              {
                DM_DBG1(("dmDownStreamDiscover2ExpanderPhy, Failed to allocate expander data structure (2)!!!\n"));
                /*  discovery done */
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
              }
		
		
	    }
            /* If status is still DISCOVERY_DOWN_STREAM */        
            if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM &&
	         onePortContext->discovery.ConfiguresOthers == agFALSE)
            {
              DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 1st before\n"));
              dmDumpAllUpExp(dmRoot, onePortContext, oneExpander); 
              UpStreamExpander = oneExpander->dmUpStreamExpander;
              ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
              configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
              configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
              if (ConfigurableExpander)
              { 
                if ( (ConfigurableExpander->dmDevice->SASAddressID.sasAddressHi 
                      == DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo)) &&
                     (ConfigurableExpander->dmDevice->SASAddressID.sasAddressLo 
                      == DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo))
                   )
                { /* directly attached between oneExpander and ConfigurableExpander */       
                  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 1st before loc 1\n"));
                  configSASAddressHi = oneExpander->dmDevice->SASAddressID.sasAddressHi;
                  configSASAddressLo = oneExpander->dmDevice->SASAddressID.sasAddressLo; 
                }
                else
                {
                  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 1st before loc 2\n"));
                  configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
                  configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
                }                                             
              } /* if !ConfigurableExpander */
              dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot, 
                                                            ConfigurableExpander,   
                                                            configSASAddressHi,
                                                            configSASAddressLo
                                                            );
              
                                                        
              if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
              {
                DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 1st q123\n"));
                UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
                ConfigurableExpander->currentDownStreamPhyIndex = 
                        dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
                ConfigurableExpander->dmReturnginExpander = oneExpander;
                dmRoutingEntryAdd(dmRoot,
                                  ConfigurableExpander, 
                                  ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                                  configSASAddressHi,
                                  configSASAddressLo
                                 );
              }                       
            }
          } 
          /*  If fail to add the device */    
          else
          {
            DM_DBG1(("dmDownStreamDiscover2ExpanderPhy, Failed to add a device!!!\n"));
            /*  discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
        }
      }
      /* If the device has been discovered before */
      else /* discovered before */
      {
        /* If the phy has subtractive routing attribute */
        if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_SUBTRACTIVE)
        {
          /* If the expander doesn't have up stream device */
          if ( oneExpander->hasUpStreamDevice == agFALSE)
          {
            /* TODO: discovery error, callback */
            DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error loop, or end device connects to two expanders!!!\n"));
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
            /* discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          /* If the expander has up stream device */
          else
          {
	    
//qqqqq
            /* If sas address doesn't match */
            if ( (oneExpander->upStreamSASAddressHi != attachedSasHi)
                 || (oneExpander->upStreamSASAddressLo != attachedSasLo) )
            {
              /* TODO: discovery error, callback */
              DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** two subtractive phys!!! Allowed in SAS2!!!\n"));
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
              onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
	      onePortContext->discovery.DeferredError = agTRUE;
     
            }
          }
        }
        /* If the phy has table routing attribute */
        else if ( SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE)
        {
          /* If the attached device is a fan out expander */
          if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_FANOUT_EXPANDER_DEVICE)
          {
            /* (2.3.3.2.1.1) TODO: discovery error, callback */
            DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error fan out expander to routing table phy!!!\n"));
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
              = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
              = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
            DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                      onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                      onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
            /* discovery done */
            dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
          }
          /* If the attached device is an edge expander */
          else if ( SAS2_DISCRSP_GET_ATTACHED_DEVTYPE(pDiscoverResp) == SAS_EDGE_EXPANDER_DEVICE) 
          {
            /* Setup up stream inform */
            AttachedExpander = AttachedDevice->dmExpander;
            DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Found edge expander=%p\n", AttachedExpander));
            //hhhhhh
            /* If the attached expander has up stream device */
            if ( AttachedExpander->hasUpStreamDevice == agTRUE)
            {
              /* compare the sas address */
              if ( (AttachedExpander->upStreamSASAddressHi
                    != DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo))
                   || (AttachedExpander->upStreamSASAddressLo
                       != DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo)))
              {
	        if (AttachedExpander->TTTSupported && oneExpander->TTTSupported)
		{
                  /*
		     needs further error checking 
		     UpstreamExpanderOfAttachedExpander = AttachedExpander->UpStreamExpander
		     for (i=0;i<DM_MAX_EXPANDER_PHYS;i++)
		     {
		       if (UpstreamExpanderOfAttachedExpander->downStreamPhys[i] != 0 &&
		     } 
		  */
		  SAS2SAS11Check = dmSAS2SAS11ErrorCheck(dmRoot, onePortContext, AttachedExpander->dmUpStreamExpander, AttachedExpander, oneExpander);                  
		  if (SAS2SAS11Check == agTRUE)
		  {
                    
		    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error SAS2 and SAS1.1!!!\n"));                    
		    onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                      = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);                    
		    onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                      = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);                    
		    onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;                    
		    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                              onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                              onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));                    
		    /* discovery done */                    
		    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
		  }
		  else
		  {
		    DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: Allowed Table to Table (1)\n"));
		    /* move on to the next phys but should be not proceed after oneExpander */
		    oneExpander->UndoDueToTTTSupported = agTRUE;
		    onePortContext->discovery.DeferredError = agFALSE;
		  }
		}
		else
		{
                  /* TODO: discovery error, callback */
                  DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error two table routing phys connected (1)!!!\n"));
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                    = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                    = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                  onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                  DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                            onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                            onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
                  /* discovery done */
                  dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
		}
              }
              else
              {
                DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Add edge expander=%p\n", AttachedExpander));
                /* set up downstream information on configurable expander */
       
                dmExpanderDownStreamPhyAdd(dmRoot, oneExpander, (bit8) oneExpander->discoveringPhyId); 
                /* haha */
                dmExpanderUpStreamPhyAdd(dmRoot, AttachedExpander, (bit8) oneExpander->discoveringPhyId);
                /* Add the pAttachedExpander to discovering list */
                dmDiscoveringExpanderAdd(dmRoot, onePortContext, AttachedExpander);
              }
            }
            /* If the attached expander doesn't have up stream device */
            else
            {
	      if (AttachedExpander->TTTSupported && oneExpander->TTTSupported)
              {
                DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: Allowed Table to Table (2)\n"));
		/* move on to the next phys but should be not proceed after oneExpander */
                oneExpander->UndoDueToTTTSupported = agTRUE;
		onePortContext->discovery.DeferredError = agFALSE;
              }
              else
              {
                /* TODO: discovery error, callback */
                DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: **** Topology Error two table routing phys connected (2)!!!\n"));
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo
                  = DEVINFO_GET_SAS_ADDRESSLO(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi
                  = DEVINFO_GET_SAS_ADDRESSHI(&oneDeviceData->agDeviceInfo);
                onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier = oneExpander->discoveringPhyId;
                DM_DBG1(("dmDownStreamDiscover2ExpanderPhy: sasAddressHi 0x%08x sasAddressLo 0x%08x phyid 0x%x\n", 
                          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressHi, 
                          onePortContext->discovery.sasAddressIDDiscoverError.sasAddressLo,
                          onePortContext->discovery.sasAddressIDDiscoverError.phyIdentifier));
                /* discovery done */
                dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
	      }
            }
          }  
        } /* for else if (SAS2_DISCRSP_GET_ROUTINGATTRIB(pDiscoverResp) == SAS_ROUTING_TABLE) */
          
        /* do this regradless of sub or table */
        /* If status is still DISCOVERY_DOWN_STREAM */            
        if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM &&
             onePortContext->discovery.ConfiguresOthers == agFALSE)
        {
          DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 2nd before\n"));
          dmDumpAllUpExp(dmRoot, onePortContext, oneExpander); 

          UpStreamExpander = oneExpander->dmUpStreamExpander;
          ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
          configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
          configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
          if (ConfigurableExpander)
          { 
            if ( (ConfigurableExpander->dmDevice->SASAddressID.sasAddressHi 
                 == DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo)) &&
                 (ConfigurableExpander->dmDevice->SASAddressID.sasAddressLo 
                   == DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo))
               )
            { /* directly attached between oneExpander and ConfigurableExpander */       
              DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 2nd before loc 1\n"));
              configSASAddressHi = oneExpander->dmDevice->SASAddressID.sasAddressHi;
              configSASAddressLo = oneExpander->dmDevice->SASAddressID.sasAddressLo; 
            }
            else
            {
              DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 2nd before loc 2\n"));
              configSASAddressHi = DEVINFO_GET_SAS_ADDRESSHI(&AttachedDevice->agDeviceInfo);
              configSASAddressLo = DEVINFO_GET_SAS_ADDRESSLO(&AttachedDevice->agDeviceInfo); 
            }                                             
          } /* if !ConfigurableExpander */
          dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot, 
                                                        ConfigurableExpander,   
                                                        configSASAddressHi,
                                                        configSASAddressLo
                                                        );
            
          if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
          {
            DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 2nd q123 \n"));
            UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
            ConfigurableExpander->currentDownStreamPhyIndex = 
                        dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
            ConfigurableExpander->dmReturnginExpander = oneExpander;
            dmRoutingEntryAdd(dmRoot,
                              ConfigurableExpander, 
                              ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                              configSASAddressHi,
                              configSASAddressLo
                             );
          }                                        
        } /* if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM) */          
        /* incremental discovery */
        if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_INCREMENTAL_START)
        {
          connectionRate = MIN(onePortContext->LinkRate, SAS2_DISCRSP_GET_LOGICAL_LINKRATE(pDiscoverResp)); 

          if (SAS2_DISCRSP_IS_STP_TARGET(pDiscoverResp) || SAS2_DISCRSP_IS_SATA_DEVICE(pDiscoverResp))    
          {
            DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: incremental SATA_STP\n"));

            dmPortSASDeviceAdd(
                              dmRoot,
                              onePortContext,
                              sasIdentify,
                              agFALSE,
                              connectionRate,
                              dmAllShared->itNexusTimeout,
                              0,
                              STP_DEVICE_TYPE,
                              oneDeviceData,
                              oneExpander,
                              pDiscoverResp->phyIdentifier
                              );
          }
          else
          {
            DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: incremental SAS\n"));

             dmPortSASDeviceAdd(
                               dmRoot,
                               onePortContext,
                               sasIdentify,
                               agFALSE,
                               connectionRate,
                               dmAllShared->itNexusTimeout,
                               0,
                               SAS_DEVICE_TYPE,
                               oneDeviceData,
                               oneExpander,
                               pDiscoverResp->phyIdentifier
                               );
        
          }
        }
        
        
      }/* else; existing devce */
    } /* not attached to myself */
    /* If the attached device is myself */
    else
    {
      DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Found Self\n"));
      DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 3rd before\n"));
      dmDumpAllUpExp(dmRoot, onePortContext, oneExpander); 

      if (onePortContext->discovery.ConfiguresOthers == agFALSE)
      {	  
        UpStreamExpander = oneExpander->dmUpStreamExpander;
        ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
        dupConfigSASAddr = dmDuplicateConfigSASAddr(dmRoot, 
                                                      ConfigurableExpander,   
                                                      onePortContext->sasLocalAddressHi,
                                                      onePortContext->sasLocalAddressLo
                                                      );
      
        if ( ConfigurableExpander && dupConfigSASAddr == agFALSE)
        {
          DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: 3rd q123 Setup routing table\n"));
          UpStreamExpander->dmCurrentDownStreamExpander = oneExpander;
          ConfigurableExpander->currentDownStreamPhyIndex = 
                          dmFindCurrentDownStreamPhyIndex(dmRoot, ConfigurableExpander);
          ConfigurableExpander->dmReturnginExpander = oneExpander;
          dmRoutingEntryAdd(dmRoot,
                            ConfigurableExpander, 
                            ConfigurableExpander->downStreamPhys[ConfigurableExpander->currentDownStreamPhyIndex],
                            onePortContext->sasLocalAddressHi,
                            onePortContext->sasLocalAddressLo
                           );
        }
      } 
    }
  }
  /* If no device is attached */
  else
  {
  }


  /* Increment the discovering phy id */
  oneExpander->discoveringPhyId ++;
  
  /* If the discovery status is DISCOVERY_DOWN_STREAM */
  if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM )
  {
    /* If not the last phy */  
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: More Phys to discover\n"));
      /* continue discovery for the next phy */
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
    /* If the last phy */
    else
    {
      DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: No More Phys\n"));
     
      /* for MCN */
      dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);  
      ConfigurableExpander = dmFindConfigurableExp(dmRoot, onePortContext, oneExpander);
      if (oneExpander->UndoDueToTTTSupported == agTRUE && ConfigurableExpander != agNULL)
//      if (oneExpander->UndoDueToTTTSupported == agTRUE)
      {
        DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: Not sure!!!\n"));
        dmDiscoveringUndoAdd(dmRoot, onePortContext, oneExpander);       
        oneExpander->UndoDueToTTTSupported = agFALSE;
      }
      
      /* remove the expander from the discovering list */
      dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
      /* continue downstream discovering */
      dmDownStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }  
  }
  else
  {
    DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: onePortContext->discovery.status not in DISCOVERY_DOWN_STREAM; status %d\n", onePortContext->discovery.status));  
  }  
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhy: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
 
  return;
}			     


osGLOBAL void
dmDiscoveringUndoAdd(
                     dmRoot_t                 *dmRoot,
                     dmIntPortContext_t       *onePortContext,
                     dmExpander_t             *oneExpander
                    )
{
  dmIntRoot_t        *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t     *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmList_t           *ExpanderList;
  dmExpander_t       *tempExpander;
  dmIntPortContext_t *tmpOnePortContext = onePortContext;
  
  DM_DBG2(("dmDiscoveringUndoAdd: start\n"));
  if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    DM_DBG2(("dmDiscoveringUndoAdd: empty discoveringExpanderList\n"));
    return;
  }

//  DM_DBG2(("dmDiscoveringUndoAdd: before\n"));
//  dmDumpAllExp(dmRoot, onePortContext, oneExpander);

  ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if ( tempExpander == agNULL)
    {
      DM_DBG1(("dmDiscoveringUndoAdd: tempExpander is NULL!!!\n"));    
      return;    
    }
    if (tempExpander->dmUpStreamExpander == oneExpander)
    {
      DM_DBG2(("dmDiscoveringUndoAdd: match!!! expander id %d\n", tempExpander->id));
      DM_DBG2(("dmDiscoveringUndoAdd: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG2(("dmDiscoveringUndoAdd: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
      tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
      DMLIST_DEQUEUE_THIS(&(tempExpander->linkNode));
//      DMLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(dmAllShared->freeExpanderList));
      DMLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(dmAllShared->mainExpanderList));
      tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
      ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;      
    }
    if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
    {
      DM_DBG2(("dmDiscoveringUndoAdd: hitting break\n"));
      break;
    }
    ExpanderList = ExpanderList->flink;
  }
  
//  DM_DBG2(("dmDiscoveringUndoAdd: after\n"));
//  dmDumpAllExp(dmRoot, onePortContext, oneExpander);
  return;
}			     

osGLOBAL void
dmHandleZoneViolation(
                      dmRoot_t              *dmRoot,
                      agsaRoot_t            *agRoot,
                      agsaIORequest_t       *agIORequest,
                      dmDeviceData_t        *oneDeviceData,
                      dmSMPFrameHeader_t    *frameHeader,
                      agsaFrameHandle_t     frameHandle
                     )
{
  dmIntPortContext_t           *onePortContext = agNULL;
  dmExpander_t                 *oneExpander = agNULL;

  DM_DBG1(("dmHandleZoneViolation: start\n"));  
  DM_DBG1(("dmHandleZoneViolation: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG1(("dmHandleZoneViolation: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  onePortContext = oneDeviceData->dmPortContext;
  oneExpander = oneDeviceData->dmExpander;
  if (dmDiscoverCheck(dmRoot, onePortContext) == agTRUE)
  {
    DM_DBG1(("dmHandleZoneViolation: invalid port or aborted discovery!!!\n"));
    return;
  }
  /* for MCN */
  dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);
  /* remove the expander from the discovering list */
  dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
  if ( onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {
    /* continue upstream discovering */
    dmUpStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
  }
  else /* DISCOVERY_DOWN_STREAM or DISCOVERY_CONFIG_ROUTING */
  {
    /* continue downstream discovering */
    dmDownStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
  }
  return;
}


osGLOBAL void
dmUpStreamDiscoverExpanderPhySkip(
                                   dmRoot_t              *dmRoot,
                                   dmIntPortContext_t    *onePortContext,
                                   dmExpander_t          *oneExpander
                                   )

{
  dmDeviceData_t          *oneDeviceData;
  DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: start\n"));
  
  oneDeviceData = oneExpander->dmDevice;
  DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
  
  oneExpander->discoveringPhyId++;
  if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: More Phys to discover\n"));
      /* continue discovery for the next phy */  
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
    else
    {
      DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: No More Phys\n"));

      /* for MCN */
      dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);  
      /* remove the expander from the discovering list */
      dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
      /* continue upstream discovering */  
      dmUpStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }
  }
  else
  {
    DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: onePortContext->discovery.status not in DISCOVERY_UP_STREAM; status %d\n", onePortContext->discovery.status));  
   
  }
   
  DM_DBG3(("dmUpStreamDiscoverExpanderPhySkip: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
  
  return;
}	   				


osGLOBAL void
dmUpStreamDiscover2ExpanderPhySkip(
                                   dmRoot_t              *dmRoot,
                                   dmIntPortContext_t    *onePortContext,
                                   dmExpander_t          *oneExpander
                                   )
{
  dmDeviceData_t          *oneDeviceData;
  
  DM_DBG2(("dmUpStreamDiscover2ExpanderPhySkip: start\n"));
  oneDeviceData = oneExpander->dmDevice;
  
  oneExpander->discoveringPhyId++;
  if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      DM_DBG2(("dmUpStreamDiscover2ExpanderPhySkip: DISCOVERY_UP_STREAM find more ...\n"));
      /* continue discovery for the next phy */  
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
    else
    {
      DM_DBG2(("dmUpStreamDiscover2ExpanderPhySkip: DISCOVERY_UP_STREAM last phy continue upstream..\n"));

      /* for MCN */
      dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);  
      /* remove the expander from the discovering list */
      dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
      /* continue upstream discovering */  
      dmUpStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }
  }
  else
  {
    DM_DBG2(("dmUpStreamDiscover2ExpanderPhySkip: onePortContext->discovery.status not in DISCOVERY_UP_STREAM; status %d\n", onePortContext->discovery.status));     
  }
   
  DM_DBG2(("dmUpStreamDiscover2ExpanderPhySkip: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
  

  return;
}			     

osGLOBAL void
dmDownStreamDiscoverExpanderPhySkip(
                                     dmRoot_t              *dmRoot,
                                     dmIntPortContext_t    *onePortContext,
                                     dmExpander_t          *oneExpander
                                     )
{
  dmDeviceData_t          *oneDeviceData;
  DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: start\n"));
  
  oneDeviceData = oneExpander->dmDevice;
  DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  /* Increment the discovering phy id */
  oneExpander->discoveringPhyId ++;
  
  /* If the discovery status is DISCOVERY_DOWN_STREAM */
  if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM )
  {
    /* If not the last phy */  
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: More Phys to discover\n"));
      /* continue discovery for the next phy */
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
    /* If the last phy */
    else
    {
      DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: No More Phys\n"));

      /* for MCN */
      dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);  
      /* remove the expander from the discovering list */
      dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
      /* continue downstream discovering */
      dmDownStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }  
  }
  else
  {
    DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: onePortContext->discovery.status not in DISCOVERY_DOWN_STREAM; status %d\n", onePortContext->discovery.status));  
  }  
  DM_DBG3(("dmDownStreamDiscoverExpanderPhySkip: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
  
  
  return;
}	   				

osGLOBAL void
dmDownStreamDiscover2ExpanderPhySkip(
                                     dmRoot_t              *dmRoot,
                                     dmIntPortContext_t    *onePortContext,
                                     dmExpander_t          *oneExpander
                                     )
{
  dmDeviceData_t          *oneDeviceData;
  
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhySkip: start\n"));
  
  oneDeviceData = oneExpander->dmDevice;
  /* Increment the discovering phy id */
  oneExpander->discoveringPhyId ++;
  
  /* If the discovery status is DISCOVERY_DOWN_STREAM */
  if ( onePortContext->discovery.status == DISCOVERY_DOWN_STREAM )
  {
    /* If not the last phy */  
    if ( oneExpander->discoveringPhyId < oneDeviceData->numOfPhys )
    {
      DM_DBG2(("dmDownStreamDiscover2ExpanderPhySkip: More Phys to discover\n"));
      /* continue discovery for the next phy */
      dmDiscoverSend(dmRoot, oneDeviceData);
    }
    /* If the last phy */
    else
    {
      DM_DBG2(("dmDownStreamDiscover2ExpanderPhySkip: No More Phys\n"));

      /* for MCN */
      dmUpdateAllAdjacent(dmRoot, onePortContext, oneDeviceData);  
      /* remove the expander from the discovering list */
      dmDiscoveringExpanderRemove(dmRoot, onePortContext, oneExpander);
      /* continue downstream discovering */
      dmDownStreamDiscovering(dmRoot, onePortContext, oneDeviceData);
    }  
  }
  else
  {
    DM_DBG2(("dmDownStreamDiscover2ExpanderPhySkip: onePortContext->discovery.status not in DISCOVERY_DOWN_STREAM; status %d\n", onePortContext->discovery.status));  
  }  
  DM_DBG2(("dmDownStreamDiscover2ExpanderPhySkip: end return phyID#%d\n", oneExpander->discoveringPhyId - 1));
  return;
}			     

osGLOBAL void
dmExpanderUpStreamPhyAdd(
                         dmRoot_t              *dmRoot,
                         dmExpander_t          *oneExpander,
                         bit8                  phyId
                         )
{
  bit32   i;
  bit32   hasSet = agFALSE;

  DM_DBG3(("dmExpanderUpStreamPhyAdd: start, phyid %d\n", phyId));
  DM_DBG3(("dmExpanderUpStreamPhyAdd: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmExpanderUpStreamPhyAdd: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG3(("dmExpanderUpStreamPhyAdd: phyid %d  numOfUpStreamPhys %d\n", phyId, oneExpander->numOfUpStreamPhys));

  for ( i = 0; i < oneExpander->numOfUpStreamPhys; i ++ )
  {
    if ( oneExpander->upStreamPhys[i] == phyId )
    {
      hasSet = agTRUE;
      break;
    }
  }
  
  if ( hasSet == agFALSE )
  {
    oneExpander->upStreamPhys[oneExpander->numOfUpStreamPhys ++] = phyId;    
  }

  DM_DBG3(("dmExpanderUpStreamPhyAdd: AFTER phyid %d  numOfUpStreamPhys %d\n", phyId, oneExpander->numOfUpStreamPhys));

  /* for debugging */
  for ( i = 0; i < oneExpander->numOfUpStreamPhys; i ++ )
  {
    DM_DBG3(("dmExpanderUpStreamPhyAdd: index %d upstream[index] %d\n", i, oneExpander->upStreamPhys[i]));
  }
  return;
}	   				

osGLOBAL void
dmExpanderDownStreamPhyAdd(
                           dmRoot_t              *dmRoot,
                           dmExpander_t          *oneExpander,
                           bit8                  phyId
                          )
{
  bit32   i;
  bit32   hasSet = agFALSE;

  DM_DBG3(("dmExpanderDownStreamPhyAdd: start, phyid %d\n", phyId));
  DM_DBG3(("dmExpanderDownStreamPhyAdd: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmExpanderDownStreamPhyAdd: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG3(("dmExpanderDownStreamPhyAdd: phyid %d  numOfDownStreamPhys %d\n", phyId, oneExpander->numOfDownStreamPhys));

  for ( i = 0; i < oneExpander->numOfDownStreamPhys; i ++ )
  {
    if ( oneExpander->downStreamPhys[i] == phyId )
    {
      hasSet = agTRUE;
      break;
    }
  }
  
  if ( hasSet == agFALSE )
  {
    oneExpander->downStreamPhys[oneExpander->numOfDownStreamPhys ++] = phyId;    
  }

  DM_DBG3(("dmExpanderDownStreamPhyAdd: AFTER phyid %d  numOfDownStreamPhys %d\n", phyId, oneExpander->numOfDownStreamPhys));

  /* for debugging */
  for ( i = 0; i < oneExpander->numOfDownStreamPhys; i ++ )
  {
     DM_DBG3(("dmExpanderDownStreamPhyAdd: index %d downstream[index] %d\n", i, oneExpander->downStreamPhys[i]));
  }
  return;
}	   				

osGLOBAL void
dmDiscoveryReportMCN(
                    dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext
                   )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  bit16             extension = 0;
  dmDeviceData_t    *oneAttachedExpDeviceData = agNULL;
    
  DM_DBG2(("dmDiscoveryReportMCN: start\n"));

/*
  if full disocvery, report all devices using MCN
  if incremental discovery, 
  1. compare MCN and PrevMCN
  2. report the changed ones; report MCN
  3. set PrevMCN to MCN
     PrevMCN = MCN
*/

  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if ( oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDiscoveryReportMCN: oneDeviceData is NULL!!!\n"));
      return;
    }        
    DM_DBG3(("dmDiscoveryReportMCN: loop did %d\n", oneDeviceData->id));
    if (oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG2(("dmDiscoveryReportMCN: oneDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
      oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         
      DM_DBG2(("dmDiscoveryReportMCN: MCN 0x%08x PrevMCN 0x%08x\n", oneDeviceData->MCN, oneDeviceData->PrevMCN));
      
      if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
      {
        DM_DBG2(("dmDiscoveryReportMCN: FULL_START\n"));
      }
      else
      {
        DM_DBG2(("dmDiscoveryReportMCN: INCREMENTAL_START\n"));
      }
      /*
        if MCN is 0, the device is removed 
      */
      if (oneDeviceData->MCN != oneDeviceData->PrevMCN && oneDeviceData->MCN != 0)
      {
        DM_DBG2(("dmDiscoveryReportMCN: reporting \n"));
        extension = oneDeviceData->dmDeviceInfo.ext;
        /* zero out MCN in extension */
        extension = extension & 0x7FF;
        /* sets MCN in extension */	
        extension = extension | (oneDeviceData->MCN << 11);
        DEVINFO_PUT_EXT(&(oneDeviceData->dmDeviceInfo), extension);
        DM_DBG5(("dmDiscoveryReportMCN: MCN 0x%08x PrevMCN 0x%08x\n", DEVINFO_GET_EXT_MCN(&(oneDeviceData->dmDeviceInfo)), oneDeviceData->PrevMCN));
        if (oneDeviceData->ExpDevice != agNULL)
        {
          DM_DBG2(("dmDiscoveryReportMCN: attached expander case\n"));
          oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
          tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, &oneAttachedExpDeviceData->dmDeviceInfo, dmDeviceMCNChange);
        }
	else
	{
          DM_DBG2(("dmDiscoveryReportMCN: No attached expander case\n"));
          tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, agNULL, dmDeviceMCNChange);
	}        
        oneDeviceData->PrevMCN = oneDeviceData->MCN;          	
      }
      else
      {
        DM_DBG2(("dmDiscoveryReportMCN: No change; no reporting \n"));
	if (oneDeviceData->MCN == 0)
	{
          oneDeviceData->PrevMCN = oneDeviceData->MCN;          	
	}
      }
      
    }
    DeviceListList = DeviceListList->flink;  
  }
  
  return;
}		   

osGLOBAL void
dmDiscoveryDumpMCN(
                    dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext
                   )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG3(("dmDiscoveryDumpMCN: start\n"));
  
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDiscoveryDumpMCN: oneDeviceData is NULL!!!\n"));
      return;   
    }
    DM_DBG3(("dmDiscoveryDumpMCN: loop did %d\n", oneDeviceData->id));
    if (oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmDiscoveryDumpMCN: oneDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
      oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         
      DM_DBG3(("dmDiscoveryDumpMCN: MCN 0x%08x PrevMCN 0x%08x\n", oneDeviceData->MCN, oneDeviceData->PrevMCN));
    }
    DeviceListList = DeviceListList->flink;  
  }
  
  return;
}		   

osGLOBAL void
dmDiscoveryResetMCN(
                    dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext
                   )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG2(("dmDiscoveryResetMCN: start\n"));
  
  /* reinitialize the device data belonging to this portcontext */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDiscoveryResetMCN: oneDeviceData is NULL!!!\n"));
      return;   
    }
    DM_DBG3(("dmDiscoveryResetMCN: loop did %d\n", oneDeviceData->id));
    if (oneDeviceData->dmPortContext == onePortContext)
    {
      if (oneDeviceData->ExpDevice != agNULL)
      {
        DM_DBG2(("dmDiscoveryResetMCN: resetting oneDeviceData->ExpDevice\n"));
        oneDeviceData->ExpDevice = agNULL;
      }	
      DM_DBG3(("dmDiscoveryResetMCN: resetting MCN and MCNdone\n"));
      oneDeviceData->MCN = 0;

      oneDeviceData->MCNDone = agFALSE;
      DM_DBG2(("dmDiscoveryResetMCN: oneDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
      oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         
    }
    DeviceListList = DeviceListList->flink;  
  }
  
  return;
}		   


/*
do min(oneDeviceData, found-one) in all upstream and downstream
find ajcanent expanders and mark it done; sees only ajcacent targets
*/
osGLOBAL void
dmUpdateAllAdjacent(
                    dmRoot_t            *dmRoot,
                    dmIntPortContext_t  *onePortContext,
                    dmDeviceData_t      *oneDeviceData /* current one */
                   )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *tmponeDeviceData = agNULL;
  dmList_t          *DeviceListList;
    
  DM_DBG2(("dmUpdateAllAdjacent: start\n"));  
  if (oneDeviceData == agNULL)
  {
    DM_DBG1(("dmUpdateAllAdjacent: oneDeviceData is NULL!!!\n"));
    return;      
  }    
  
  oneDeviceData->MCNDone = agTRUE;
  
  DM_DBG2(("dmUpdateAllAdjacent: oneDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
  oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         

 
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    tmponeDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if ( tmponeDeviceData == agNULL)
    {
      DM_DBG1(("dmUpdateAllAdjacent: tmponeDeviceData is NULL!!!\n"));
      return;
    }
    DM_DBG3(("dmUpdateAllAdjacent: loop did %d\n", tmponeDeviceData->id));
    if (tmponeDeviceData->dmPortContext == onePortContext && tmponeDeviceData->ExpDevice == oneDeviceData)
    {
      DM_DBG2(("dmUpdateAllAdjacent: setting MCN DONE\n"));
      DM_DBG2(("dmUpdateAllAdjacent: tmponeDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
      tmponeDeviceData->SASAddressID.sasAddressHi, tmponeDeviceData->SASAddressID.sasAddressLo));         
      tmponeDeviceData->MCNDone = agTRUE;
      if (oneDeviceData->directlyAttached == agFALSE)
      {
        DM_DBG2(("dmUpdateAllAdjacent: tmponeDeviceData MCN 0x%x\n", tmponeDeviceData->MCN));
        DM_DBG2(("dmUpdateAllAdjacent: oneDeviceData MCN 0x%x\n", oneDeviceData->MCN));
        tmponeDeviceData->MCN = MIN(oneDeviceData->MCN, tmponeDeviceData->MCN);
      }
    
    }
    DeviceListList = DeviceListList->flink;  
  }
  
  return;

}		   

osGLOBAL void
dmUpdateMCN(
            dmRoot_t            *dmRoot,
            dmIntPortContext_t  *onePortContext,
            dmDeviceData_t      *AdjacentDeviceData, /* adjacent expander */ 		    
            dmDeviceData_t      *oneDeviceData /* current one */
           )
{
  
  DM_DBG2(("dmUpdateMCN: start\n"));  
  
  if (AdjacentDeviceData == agNULL)
  {
    DM_DBG1(("dmUpdateMCN: AdjacentDeviceData is NULL!!!\n"));
    return;      
  }    
  
  if (oneDeviceData == agNULL)
  {
    DM_DBG1(("dmUpdateMCN: oneDeviceData is NULL!!!\n"));
    return;      
  }    
  
  DM_DBG2(("dmUpdateMCN: Current sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
  oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         
  
  DM_DBG2(("dmUpdateMCN: AdjacentDeviceData one sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
  AdjacentDeviceData->SASAddressID.sasAddressHi, AdjacentDeviceData->SASAddressID.sasAddressLo));         
  
  if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {   
    DM_DBG2(("dmUpdateMCN: DISCOVERY_UP_STREAM\n"));  
  }  
  
  if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
  {   
    DM_DBG2(("dmUpdateMCN: DISCOVERY_DOWN_STREAM\n"));  
  }  
  
  
  /* MCN */

  /* directly attached one does not have MCN 
     update only adjacent device data
  */
  
  if (oneDeviceData->directlyAttached == agTRUE && AdjacentDeviceData->MCNDone == agFALSE)
  {
    AdjacentDeviceData->MCN++;
    DM_DBG2(("dmUpdateMCN: case 1 oneDeviceData MCN 0x%x\n", oneDeviceData->MCN));
    DM_DBG2(("dmUpdateMCN: case 1 AdjacentDeviceData MCN 0x%x\n", AdjacentDeviceData->MCN));
  }
  else if (AdjacentDeviceData->MCNDone == agFALSE)
  {
    AdjacentDeviceData->MCN++;
    AdjacentDeviceData->MCN = MIN(oneDeviceData->MCN, AdjacentDeviceData->MCN);      
    DM_DBG2(("dmUpdateMCN: case 2 oneDeviceData MCN 0x%x\n", oneDeviceData->MCN));
    DM_DBG2(("dmUpdateMCN: case 2 AdjacentDeviceData MCN 0x%x\n", AdjacentDeviceData->MCN));
  }
  
 	
  return;
}
/* go through expander list and device list array ??? */
osGLOBAL dmDeviceData_t *
dmPortSASDeviceFind(
                    dmRoot_t            *dmRoot,
                    dmIntPortContext_t  *onePortContext,
                    bit32               sasAddrLo,
                    bit32               sasAddrHi,
                    dmDeviceData_t      *CurrentDeviceData /* current expander */ 		    
                    )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t            *oneDeviceData, *RetDeviceData=agNULL;
  dmList_t                  *DeviceListList;
      
  DM_DBG3(("dmPortSASDeviceFind: start\n"));  
  DM_DBG3(("dmPortSASDeviceFind: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", sasAddrHi, sasAddrLo));         
 
  DM_ASSERT((agNULL != dmRoot), "");
  DM_ASSERT((agNULL != onePortContext), "");

  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  
  /* find a device's existence */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
  {
    DM_DBG3(("dmPortSASDeviceFind: Full discovery\n"));
    while (DeviceListList != &(dmAllShared->MainDeviceList))
    {
      oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        DM_DBG1(("dmPortSASDeviceFind: oneDeviceData is NULL!!!\n"));  
        return agNULL;
      }      
      if ((oneDeviceData->SASAddressID.sasAddressHi == sasAddrHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == sasAddrLo) &&
          (oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->dmPortContext == onePortContext)
        )
      {
        DM_DBG3(("dmPortSASDeviceFind: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        DM_DBG3(("dmPortSASDeviceFind: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));         
        DM_DBG3(("dmPortSASDeviceFind: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        RetDeviceData = oneDeviceData;
        dmUpdateMCN(dmRoot, onePortContext, RetDeviceData, CurrentDeviceData);
       	break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }
  else
  {
    /* incremental discovery */
    DM_DBG3(("dmPortSASDeviceFind: Incremental discovery\n"));
    while (DeviceListList != &(dmAllShared->MainDeviceList))
    {
      oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        DM_DBG1(("dmPortSASDeviceFind: oneDeviceData is NULL!!!\n"));  
        return agNULL;
      }      
      if ((oneDeviceData->SASAddressID.sasAddressHi == sasAddrHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == sasAddrLo) &&
          (oneDeviceData->valid2 == agTRUE) &&
          (oneDeviceData->dmPortContext == onePortContext)
          )
      {
        DM_DBG3(("dmPortSASDeviceFind: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        DM_DBG3(("dmPortSASDeviceFind: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));         
        DM_DBG3(("dmPortSASDeviceFind: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        RetDeviceData = oneDeviceData;
        dmUpdateMCN(dmRoot, onePortContext, RetDeviceData, CurrentDeviceData);
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }
  
  tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);

  return RetDeviceData;
}		      

bit32
dmNewEXPorNot(
              dmRoot_t              *dmRoot,
              dmIntPortContext_t    *onePortContext,
              dmSASSubID_t          *dmSASSubID
             )
{
//  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
//  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmExpander_t      *oneExpander = agNULL;
  dmList_t          *ExpanderList;
  bit32             ret = agTRUE;
  dmDeviceData_t    *oneDeviceData = agNULL;
  
  DM_DBG3(("dmNewEXPorNot: start\n"));
  
  /* find a device's existence */
  ExpanderList = onePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(onePortContext->discovery.discoveringExpanderList))
  {
    oneExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if ( oneExpander == agNULL)
    {
      DM_DBG1(("dmNewEXPorNot: oneExpander is NULL!!!\n"));    
      return agFALSE;
    }    
    oneDeviceData = oneExpander->dmDevice;
    if ((oneDeviceData->SASAddressID.sasAddressHi == dmSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == dmSASSubID->sasAddressLo) &&
        (oneDeviceData->dmPortContext == onePortContext)
        )
    {
      DM_DBG3(("dmNewEXPorNot: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      ret = agFALSE;
      break;
    }
    ExpanderList = ExpanderList->flink;
  }
    
  return ret;
}


bit32
dmNewSASorNot(
              dmRoot_t              *dmRoot,
              dmIntPortContext_t    *onePortContext,
              dmSASSubID_t          *dmSASSubID
             )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  bit32             ret = agTRUE;
  
  DM_DBG3(("dmNewSASorNot: start\n"));
  
  /* find a device's existence */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmNewSASorNot: oneDeviceData is NULL!!!\n"));
      return agFALSE;
    }    
    if ((oneDeviceData->SASAddressID.sasAddressHi == dmSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == dmSASSubID->sasAddressLo) &&
        (oneDeviceData->dmPortContext == onePortContext) &&
        (oneDeviceData->registered == agTRUE)	
       )
    {
      DM_DBG3(("dmNewSASorNot: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      ret = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }
    
  return ret;
}
/* 
call
osGLOBAL bit32 
tddmReportDevice(
                 dmRoot_t 		*dmRoot,
                 dmPortContext_t	*dmPortContext,
                 dmDeviceInfo_t		*dmDeviceInfo
                 )
if not reported, report Device to TDM
*/
osGLOBAL dmDeviceData_t *
dmPortSASDeviceAdd(
                   dmRoot_t            *dmRoot,
                   dmIntPortContext_t  *onePortContext,
                   agsaSASIdentify_t   sasIdentify,
                   bit32               sasInitiator,
                   bit8                connectionRate,
                   bit32               itNexusTimeout,
                   bit32               firstBurstSize,
                   bit32               deviceType,
                   dmDeviceData_t      *oneExpDeviceData,
                   dmExpander_t        *dmExpander,
                   bit8                phyID
                  )
{
  dmDeviceData_t    *oneDeviceData = agNULL;
  bit8              dev_s_rate = 0;
  bit8              sasorsata = 1;
  dmSASSubID_t      dmSASSubID;
  bit8              ExpanderConnectionRate = connectionRate;
  dmDeviceData_t    *oneAttachedExpDeviceData = agNULL;
  bit16             extension = 0;
  bit32             current_link_rate = 0;
  
  DM_DBG3(("dmPortSASDeviceAdd: start\n"));
  DM_DBG3(("dmPortSASDeviceAdd: connectionRate %d\n", connectionRate));
  
  dmSASSubID.sasAddressHi = SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify);
  dmSASSubID.sasAddressLo = SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify);
  dmSASSubID.initiator_ssp_stp_smp = sasIdentify.initiator_ssp_stp_smp;
  dmSASSubID.target_ssp_stp_smp = sasIdentify.target_ssp_stp_smp;
  
  if (oneExpDeviceData != agNULL)
  {
    ExpanderConnectionRate =   DEVINFO_GET_LINKRATE(&oneExpDeviceData->agDeviceInfo);
    DM_DBG3(("dmPortSASDeviceAdd: ExpanderConnectionRate 0x%x\n", ExpanderConnectionRate));
  }
  if (oneExpDeviceData != agNULL)
  {
    if (oneExpDeviceData->SASAddressID.sasAddressHi == 0x0 &&
        oneExpDeviceData->SASAddressID.sasAddressLo == 0x0)
    {
      DM_DBG1(("dmPortSASDeviceAdd: 1st Wrong expander!!!\n"));    
    }    	    
  }
  /* old device and already reported to TDM */
  if ( agFALSE == dmNewSASorNot(
                                 dmRoot,
                                 onePortContext,
                                 &dmSASSubID
                                )
       ) /* old device */
  {
    DM_DBG3(("dmPortSASDeviceAdd: OLD qqqq initiator_ssp_stp_smp %d target_ssp_stp_smp %d\n", dmSASSubID.initiator_ssp_stp_smp, dmSASSubID.target_ssp_stp_smp));
    /* allocate a new device and set the valid bit */ 
    oneDeviceData = dmAddSASToSharedcontext(
                                               dmRoot,
                                               onePortContext,
                                               &dmSASSubID,
                                               oneExpDeviceData,
                                               phyID
                                               );
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmPortSASDeviceAdd: no more device, oneDeviceData is null!!!\n"));
    }
    /* If a device is allocated */
    if ( oneDeviceData != agNULL )
    {


      if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
      {
        DM_DBG3(("dmPortSASDeviceAdd: OLD, UP_STREAM\n"));
      }    
      if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
      {      
        DM_DBG3(("dmPortSASDeviceAdd: OLD, DOWN_STREAM\n"));
      }
      
      if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
      {
        DM_DBG3(("dmPortSASDeviceAdd: FULL_START\n"));  
        oneDeviceData->MCN++;
      }
      else
      {
        /* incremental */
        DM_DBG3(("dmPortSASDeviceAdd: INCREMENTAL_START\n"));  
        if (oneDeviceData->MCN == 0 && oneDeviceData->directlyAttached == agFALSE)
        {
          oneDeviceData->MCN++;	  
        }
      }      	
      
      DM_DBG3(("dmPortSASDeviceAdd: oneDeviceData MCN 0x%08x\n", oneDeviceData->MCN));
      DM_DBG3(("dmPortSASDeviceAdd: oneDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
      oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         


      DM_DBG3(("dmPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify)));
      DM_DBG3(("dmPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify)));
      
//      oneDeviceData->sasIdentify = sasIdentify;
      dm_memcpy(&(oneDeviceData->sasIdentify), &sasIdentify, sizeof(agsaSASIdentify_t));
      
      DM_DBG3(("dmPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)));
      DM_DBG3(("dmPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)));
      
      /* parse sasIDframe to fill in agDeviceInfo */
      DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
      DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)itNexusTimeout);
      DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, (bit16)firstBurstSize);
      DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 1);
      
      oneDeviceData->SASSpecDeviceType = SA_IDFRM_GET_DEVICETTYPE(&sasIdentify);
      
      /* adjusting connectionRate */
      oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
      if (oneAttachedExpDeviceData != agNULL)
      {
        connectionRate = MIN(connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo));
        DM_DBG3(("dmPortSASDeviceAdd: 1st connectionRate 0x%x  DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo) 0x%x\n",
	       connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
      }
      else
      {
       DM_DBG3(("dmPortSASDeviceAdd: 1st oneAttachedExpDeviceData is NULL\n"));
      }
      
      /* Device Type, SAS or SATA, connection rate; bit7 --- bit0 */
      sasorsata = (bit8)deviceType;
      /* sTSDK spec device typ */
      dev_s_rate = dev_s_rate | (sasorsata << 4);
      dev_s_rate = dev_s_rate | MIN(connectionRate, ExpanderConnectionRate);
      /* detect link rate change */
      current_link_rate = DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo);
      if (current_link_rate != (bit32)MIN(connectionRate, ExpanderConnectionRate))
      {
        DM_DBG1(("dmPortSASDeviceAdd: link rate changed current 0x%x new 0x%x\n", current_link_rate, MIN(connectionRate, ExpanderConnectionRate)));
        DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->dmDeviceInfo, dev_s_rate);
        if (oneDeviceData->ExpDevice != agNULL)
        {
          oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
          tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, &oneAttachedExpDeviceData->dmDeviceInfo, dmDeviceRateChange);
        }
        else
        {
          tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, agNULL, dmDeviceArrival);
        }	  
      }
      
      DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);
      
    
      DEVINFO_PUT_SAS_ADDRESSLO(
                                &oneDeviceData->agDeviceInfo,
                                SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)
                                );
      DEVINFO_PUT_SAS_ADDRESSHI(
                                &oneDeviceData->agDeviceInfo,
                                SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)
                                );
      oneDeviceData->agContext.osData = oneDeviceData;
      oneDeviceData->agContext.sdkData = agNULL;
   
      
    }	
    return oneDeviceData;		
  }  /* old device */
  
  
  /* new device */
	
  DM_DBG3(("dmPortSASDeviceAdd: NEW qqqq initiator_ssp_stp_smp %d target_ssp_stp_smp %d\n", dmSASSubID.initiator_ssp_stp_smp, dmSASSubID.target_ssp_stp_smp));
  
  /* allocate a new device and set the valid bit */ 
  oneDeviceData = dmAddSASToSharedcontext(
                                               dmRoot,
                                               onePortContext,
                                               &dmSASSubID,
                                               oneExpDeviceData,
                                               phyID
                                               );
  if (oneDeviceData == agNULL)
  {
    DM_DBG1(("dmPortSASDeviceAdd: no more device, oneDeviceData is null !!!\n"));
  }
  
   /* If a device is allocated */
  if ( oneDeviceData != agNULL )
  {

//    DM_DBG3(("dmPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&sasIdentify)));
//    DM_DBG3(("dmPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&sasIdentify)));
  
//    oneDeviceData->sasIdentify = sasIdentify;
    dm_memcpy(&(oneDeviceData->sasIdentify), &sasIdentify, sizeof(agsaSASIdentify_t));
    
    if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      DM_DBG3(("dmPortSASDeviceAdd: NEW, UP_STREAM\n"));
    }    
    if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {      
      DM_DBG3(("dmPortSASDeviceAdd: NEW, DOWN_STREAM\n"));
    }
        
    if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
    {
      DM_DBG3(("dmPortSASDeviceAdd: FULL_START\n"));  
      oneDeviceData->MCN++;
    }
    else
    {
      /* incremental */
      DM_DBG3(("dmPortSASDeviceAdd: INCREMENTAL_START\n"));  
      if (oneDeviceData->MCN == 0 && oneDeviceData->directlyAttached == agFALSE)
      {
        oneDeviceData->MCN++;	  
      }
    }      
    DM_DBG3(("dmPortSASDeviceAdd: oneDeviceData MCN 0x%08x\n", oneDeviceData->MCN));
    DM_DBG3(("dmPortSASDeviceAdd: oneDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
    oneDeviceData->SASAddressID.sasAddressHi, oneDeviceData->SASAddressID.sasAddressLo));         
    
    DM_DBG3(("dmPortSASDeviceAdd: sasAddressHi 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)));
    DM_DBG3(("dmPortSASDeviceAdd: sasAddressLo 0x%08x\n", SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)));

    /* parse sasIDframe to fill in agDeviceInfo */
    DEVINFO_PUT_SMPTO(&oneDeviceData->agDeviceInfo, DEFAULT_SMP_TIMEOUT);
    DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->agDeviceInfo, (bit16)itNexusTimeout);
    DEVINFO_PUT_FBS(&oneDeviceData->agDeviceInfo, (bit16)firstBurstSize);
    DEVINFO_PUT_FLAG(&oneDeviceData->agDeviceInfo, 1);
    
    oneDeviceData->SASSpecDeviceType = SA_IDFRM_GET_DEVICETTYPE(&sasIdentify);
    
    /* adjusting connectionRate */
    oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
    if (oneAttachedExpDeviceData != agNULL)
    {
      connectionRate = MIN(connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo));
      DM_DBG3(("dmPortSASDeviceAdd: 2nd connectionRate 0x%x  DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo) 0x%x\n",
                connectionRate, DEVINFO_GET_LINKRATE(&oneAttachedExpDeviceData->agDeviceInfo)));
    }
    else
    {
     DM_DBG3(("dmPortSASDeviceAdd: 2nd oneAttachedExpDeviceData is NULL\n"));
    }
    
    /* Device Type, SAS or SATA, connection rate; bit7 --- bit0 */
    sasorsata = (bit8)deviceType;
    dev_s_rate = dev_s_rate | (sasorsata << 4);
    dev_s_rate = dev_s_rate | MIN(connectionRate, ExpanderConnectionRate);
    DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->agDeviceInfo, dev_s_rate);

    
    DEVINFO_PUT_SAS_ADDRESSLO(
                              &oneDeviceData->agDeviceInfo,
                              SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)
                              );
    DEVINFO_PUT_SAS_ADDRESSHI(
                              &oneDeviceData->agDeviceInfo,
                              SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)
                              );
    oneDeviceData->agContext.osData = oneDeviceData;
    oneDeviceData->agContext.sdkData = agNULL;
		
    DM_DBG3(("dmPortSASDeviceAdd: did %d\n", oneDeviceData->id));
  
    
    /* reporting to TDM; setting dmDeviceInfo */
    DEVINFO_PUT_SMPTO(&oneDeviceData->dmDeviceInfo, DEFAULT_SMP_TIMEOUT);
    DEVINFO_PUT_ITNEXUSTO(&oneDeviceData->dmDeviceInfo, (bit16)itNexusTimeout);
    DEVINFO_PUT_FBS(&oneDeviceData->dmDeviceInfo, (bit16)firstBurstSize);
    DEVINFO_PUT_FLAG(&oneDeviceData->dmDeviceInfo, 1);
    DEVINFO_PUT_INITIATOR_SSP_STP_SMP(&oneDeviceData->dmDeviceInfo, dmSASSubID.initiator_ssp_stp_smp);
    DEVINFO_PUT_TARGET_SSP_STP_SMP(&oneDeviceData->dmDeviceInfo, dmSASSubID.target_ssp_stp_smp);
    extension = phyID;
      
    /* setting 6th bit of dev_s_rate */
    if (oneDeviceData->SASSpecDeviceType == SAS_EDGE_EXPANDER_DEVICE ||
        oneDeviceData->SASSpecDeviceType == SAS_FANOUT_EXPANDER_DEVICE )
    {
      extension = (bit16)(extension | (1 << 8));
    }
    DEVINFO_PUT_EXT(&oneDeviceData->dmDeviceInfo, extension);
      
    DEVINFO_PUT_DEV_S_RATE(&oneDeviceData->dmDeviceInfo, dev_s_rate);
    
    DEVINFO_PUT_SAS_ADDRESSLO(
                              &oneDeviceData->dmDeviceInfo,
                              SA_IDFRM_GET_SAS_ADDRESSLO(&oneDeviceData->sasIdentify)
                              );
    DEVINFO_PUT_SAS_ADDRESSHI(
                              &oneDeviceData->dmDeviceInfo,
                              SA_IDFRM_GET_SAS_ADDRESSHI(&oneDeviceData->sasIdentify)
                              );
    
    if (oneDeviceData->ExpDevice != agNULL)
    {
      DM_DBG3(("dmPortSASDeviceAdd: attached expander case\n"));
      oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
      /*
        Puts attached expander's SAS address into dmDeviceInfo
      */
      DEVINFO_PUT_SAS_ADDRESSLO(
                                &oneAttachedExpDeviceData->dmDeviceInfo,
                                oneAttachedExpDeviceData->SASAddressID.sasAddressLo
                                );
      DEVINFO_PUT_SAS_ADDRESSHI(
                                &oneAttachedExpDeviceData->dmDeviceInfo,
                                oneAttachedExpDeviceData->SASAddressID.sasAddressHi
                                );
      DM_DBG3(("dmPortSASDeviceAdd: oneAttachedExpDeviceData addrHi 0x%08x addrLo 0x%08x PhyID 0x%x ext 0x%x\n", 
      DM_GET_SAS_ADDRESSHI(oneAttachedExpDeviceData->dmDeviceInfo.sasAddressHi), 
      DM_GET_SAS_ADDRESSLO(oneAttachedExpDeviceData->dmDeviceInfo.sasAddressLo),
      phyID, extension));    
            
      if (oneAttachedExpDeviceData->SASAddressID.sasAddressHi == 0x0 &&
          oneAttachedExpDeviceData->SASAddressID.sasAddressLo == 0x0)
      {
        DM_DBG1(("dmPortSASDeviceAdd: 2nd Wrong expander!!!\n"));    
      }    	    
      if (oneDeviceData->reported == agFALSE)
      {
        oneDeviceData->registered = agTRUE;
        oneDeviceData->reported = agTRUE;
        if (deviceType == STP_DEVICE_TYPE)
        {
            /*STP device, DM need send SMP Report Phy SATA to get the SATA device type */
            oneAttachedExpDeviceData->dmExpander->dmDeviceToProcess = oneDeviceData;
            dmReportPhySataSend(dmRoot, oneAttachedExpDeviceData, phyID);  
        }
        else
        {
            /* SAS or SMP device */
            tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, &oneAttachedExpDeviceData->dmDeviceInfo, dmDeviceArrival);
        }
      }      
    }
    else
    {
      DM_DBG3(("dmPortSASDeviceAdd: NO attached expander case\n"));
      if (oneDeviceData->reported == agFALSE)
      {
        oneDeviceData->registered = agTRUE;
        oneDeviceData->reported = agTRUE;
        tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, agNULL, dmDeviceArrival);
      }      
    }	
  }  
  
  return oneDeviceData;
}		      

osGLOBAL dmDeviceData_t *
dmFindRegNValid(
                dmRoot_t             *dmRoot,
                dmIntPortContext_t   *onePortContext,
                dmSASSubID_t         *dmSASSubID
               )								
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  bit32             found = agFALSE;
  DM_DBG3(("dmFindRegNValid: start\n"));
  
  /* find a device's existence */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
  {
    DM_DBG3(("dmFindRegNValid: Full discovery\n"));
    while (DeviceListList != &(dmAllShared->MainDeviceList))
    {
      oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        DM_DBG1(("dmFindRegNValid: oneDeviceData is NULL!!!\n"));
        return agFALSE;
      }    
      if ((oneDeviceData->SASAddressID.sasAddressHi == dmSASSubID->sasAddressHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == dmSASSubID->sasAddressLo) &&
          (oneDeviceData->valid == agTRUE) &&
          (oneDeviceData->dmPortContext == onePortContext)
          )
      {
        DM_DBG3(("dmFindRegNValid: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        DM_DBG3(("dmFindRegNValid: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));         
        DM_DBG3(("dmFindRegNValid: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        found = agTRUE;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }
  else
  {
    /* incremental discovery */
    DM_DBG3(("dmFindRegNValid: Incremental discovery\n"));
    while (DeviceListList != &(dmAllShared->MainDeviceList))
    {
      oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        DM_DBG1(("dmFindRegNValid: oneDeviceData is NULL!!!\n"));
        return agFALSE;
      }    
      if ((oneDeviceData->SASAddressID.sasAddressHi == dmSASSubID->sasAddressHi) &&
          (oneDeviceData->SASAddressID.sasAddressLo == dmSASSubID->sasAddressLo) &&
          (oneDeviceData->valid2 == agTRUE) &&
          (oneDeviceData->dmPortContext == onePortContext)
          )
      {
        DM_DBG3(("dmFindRegNValid: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
        DM_DBG3(("dmFindRegNValid: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));         
        DM_DBG3(("dmFindRegNValid: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
        found = agTRUE;
        break;
      }
      DeviceListList = DeviceListList->flink;
    }
  }
    
        
                
  if (found == agFALSE)
  {
    DM_DBG3(("dmFindRegNValid: end returning NULL\n"));
    return agNULL;
  }
  else
  {
    DM_DBG3(("dmFindRegNValid: end returning NOT NULL\n"));
    return oneDeviceData;
  }
}		      

osGLOBAL void	
dmNotifyBC(
           dmRoot_t			*dmRoot,
           dmPortContext_t		*dmPortContext,
           bit32 			type)
{
  dmIntPortContext_t        *onePortContext = agNULL;
  
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  
  DM_DBG3(("dmNotifyBC: start\n"));
      
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmNotifyBC: onePortContext is NULL, wrong!!!\n"));  
    return;
  }
  
  if (type == OSSA_HW_EVENT_BROADCAST_CHANGE)
  {
    if (onePortContext->DiscoveryAbortInProgress == agFALSE)
    {
    if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED)
    {
      DM_DBG3(("dmNotifyBC: BROADCAST_CHANGE\n"));
      onePortContext->DiscoveryState = DM_DSTATE_NOT_STARTED;
      onePortContext->discoveryOptions = DM_DISCOVERY_OPTION_INCREMENTAL_START;
      /* processed broadcast change */
      onePortContext->discovery.SeenBC = agFALSE;       
    }
    else
    {
      DM_DBG3(("dmNotifyBC: pid %d BROADCAST_CHANGE; updating SeenBC. Do nothing.\n", onePortContext->id));      
      onePortContext->discovery.SeenBC = agTRUE;
    }
    }                 
  }
  else if (type == OSSA_HW_EVENT_BROADCAST_SES)
  {
    DM_DBG3(("dmNotifyBC: OSSA_HW_EVENT_BROADCAST_SES\n"));      
  }
  else if (type == OSSA_HW_EVENT_BROADCAST_EXP)
  {
    DM_DBG3(("dmNotifyBC: OSSA_HW_EVENT_BROADCAST_EXP\n"));      
  }
  else 
  {
    DM_DBG3(("dmNotifyBC: unspecified broadcast type 0x%x\n", type));      
  }
  return;
}	   				


#ifdef WORKED
/* triggers incremental discovery */
osGLOBAL void	
dmNotifyBC(
           dmRoot_t			*dmRoot,
           dmPortContext_t		*dmPortContext,
           bit32 			type)
{
  dmIntPortContext_t        *onePortContext = agNULL;
  
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  
  DM_DBG3(("dmNotifyBC: start\n"));
      
  
  if (type == OSSA_HW_EVENT_BROADCAST_CHANGE)
  {
    if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED)
    {
      DM_DBG3(("dmNotifyBC: BROADCAST_CHANGE; does incremental discovery\n"));
      onePortContext->DiscoveryState = DM_DSTATE_NOT_STARTED;
      onePortContext->discoveryOptions = DM_DISCOVERY_OPTION_INCREMENTAL_START;
      /* processed broadcast change */
      onePortContext->discovery.SeenBC = agFALSE;       
      if (onePortContext->discovery.ResetTriggerred == agTRUE)
      {
        DM_DBG3(("dmNotifyBC: tdsaBCTimer\n"));
        dmBCTimer(dmRoot, onePortContext);
      }
      else
      {
        dmDiscover(
                   dmRoot,
                   dmPortContext,
                   DM_DISCOVERY_OPTION_INCREMENTAL_START
                  );
      }
    }
    else
    {
      DM_DBG3(("dmNotifyBC: pid %d BROADCAST_CHANGE; updating SeenBC. Do nothing.\n", onePortContext->id));      
      onePortContext->discovery.SeenBC = agTRUE;
    }                 
  }
  else if (type == OSSA_HW_EVENT_BROADCAST_SES)
  {
    DM_DBG3(("dmNotifyBC: OSSA_HW_EVENT_BROADCAST_SES\n"));      
  }
  else if (type == OSSA_HW_EVENT_BROADCAST_EXP)
  {
    DM_DBG3(("dmNotifyBC: OSSA_HW_EVENT_BROADCAST_EXP\n"));      
  }
  else 
  {
    DM_DBG3(("dmNotifyBC: unspecified broadcast type 0x%x\n", type));      
  }
  return;
}	   				
#endif				
				
osGLOBAL bit32 	
dmResetFailedDiscovery(  
                 dmRoot_t               *dmRoot,
                 dmPortContext_t        *dmPortContext)
{
  dmIntPortContext_t        *onePortContext = agNULL;
  
  DM_DBG1(("dmResetFailedDiscovery: start\n"));
  
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmResetFailedDiscovery: onePortContext is NULL, wrong!!!\n"));  
    return DM_RC_FAILURE;
  }
  
  if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED_WITH_FAILURE)
  {
    onePortContext->DiscoveryState = DM_DSTATE_COMPLETED;
  }
  else
  {
    DM_DBG1(("dmResetFailedDiscovery: discovery is NOT DM_DSTATE_COMPLETED_WITH_FAILURE. It is 0x%x\n", onePortContext->DiscoveryState));  
    return DM_RC_FAILURE;
  }
  
  return DM_RC_SUCCESS;
}	   				

osGLOBAL bit32 	
dmQueryDiscovery(  
                 dmRoot_t 		*dmRoot,
                 dmPortContext_t	*dmPortContext)
{
  dmIntPortContext_t        *onePortContext = agNULL;
  
  DM_DBG3(("dmQueryDiscovery: start\n"));
  
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmQueryDiscovery: onePortContext is NULL, wrong!!!\n"));  
    return DM_RC_FAILURE;
  }
  
  /* call tddmQueryDiscoveryCB() */
  if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED)
  {
    tddmQueryDiscoveryCB(dmRoot, dmPortContext,  onePortContext->discoveryOptions, dmDiscCompleted); 
  }
  else if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED_WITH_FAILURE)
  {
    tddmQueryDiscoveryCB(dmRoot, dmPortContext,  onePortContext->discoveryOptions, dmDiscFailed); 
  }
  else
  {
    tddmQueryDiscoveryCB(dmRoot, dmPortContext,  onePortContext->discoveryOptions, dmDiscInProgress); 
  }  
  
  return DM_RC_SUCCESS;
}	   				

								
/* 
  should only for an expander
*/
osGLOBAL bit32 	
dmRegisterDevice(  
		 dmRoot_t 		*dmRoot,
		 dmPortContext_t	*dmPortContext,
		 dmDeviceInfo_t		*dmDeviceInfo,
                 agsaDevHandle_t        *agDevHandle
		 )
{

  dmIntPortContext_t        *onePortContext = agNULL;
  dmExpander_t              *oneExpander = agNULL;
  bit32                     sasAddressHi, sasAddressLo;
  dmDeviceData_t            *oneDeviceData = agNULL;
  dmSASSubID_t              dmSASSubID;
  
  DM_DBG3(("dmRegisterDevice: start\n"));
  
  onePortContext = (dmIntPortContext_t *)dmPortContext->dmData;
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmRegisterDevice: onePortContext is NULL!!!\n"));
    return DM_RC_FAILURE;
  }
  
  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmRegisterDevice: invalid port!!!\n"));
    return DM_RC_FAILURE;
  }
  
  onePortContext->RegFailed = agFALSE;
  
  /* tdssAddSASToSharedcontext() from ossaHwCB()
osGLOBAL void
tdssAddSASToSharedcontext(
                          tdsaPortContext_t    *tdsaPortContext_Instance,
                          agsaRoot_t           *agRoot,
                          agsaDevHandle_t      *agDevHandle,
                          tdsaSASSubID_t       *agSASSubID,
                          bit32                registered,
                          bit8                 phyID,
                          bit32                flag
                          );
from discovery  
osGLOBAL tdsaDeviceData_t *
tdssNewAddSASToSharedcontext(
                                 agsaRoot_t           *agRoot,
                                 tdsaPortContext_t    *onePortContext,
                                 tdsaSASSubID_t       *agSASSubID,
                                 tdsaDeviceData_t     *oneExpDeviceData,
                                 bit8                 phyID
                                 );
  
  */
  /* start here */
  dmSASSubID.sasAddressHi = DM_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
  dmSASSubID.sasAddressLo = DM_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressLo);
  dmSASSubID.initiator_ssp_stp_smp = dmDeviceInfo->initiator_ssp_stp_smp;
  dmSASSubID.target_ssp_stp_smp = dmDeviceInfo->target_ssp_stp_smp;
 
  oneDeviceData = dmAddSASToSharedcontext(dmRoot, onePortContext, &dmSASSubID, agNULL, 0xFF);  
  if (oneDeviceData == agNULL)
  {
    DM_DBG1(("dmRegisterDevice: oneDeviceData is NULL!!!\n"));
    return DM_RC_FAILURE;
  }
  oneDeviceData->agDeviceInfo.devType_S_Rate = dmDeviceInfo->devType_S_Rate;
  dm_memcpy(oneDeviceData->agDeviceInfo.sasAddressHi, dmDeviceInfo->sasAddressHi, 4);
  dm_memcpy(oneDeviceData->agDeviceInfo.sasAddressLo, dmDeviceInfo->sasAddressLo, 4);
  /* finds the type of expanders */
  if (DEVINFO_GET_EXT_SMP(dmDeviceInfo))
  {
    if (DEVINFO_GET_EXT_EXPANDER_TYPE(dmDeviceInfo) == SAS_EDGE_EXPANDER_DEVICE)
    {
      oneDeviceData->SASSpecDeviceType = SAS_EDGE_EXPANDER_DEVICE;
    }
    else if (DEVINFO_GET_EXT_EXPANDER_TYPE(dmDeviceInfo) == SAS_FANOUT_EXPANDER_DEVICE)
    {
      oneDeviceData->SASSpecDeviceType = SAS_FANOUT_EXPANDER_DEVICE;
    }
    else
    {
      /* default */
      DM_DBG4(("dmRegisterDevice: no expander type. default to edge expander\n"));
      oneDeviceData->SASSpecDeviceType = SAS_EDGE_EXPANDER_DEVICE;
    }
  }
  
  if (DEVINFO_GET_EXT_MCN(dmDeviceInfo) == 0xF)
  {
    DM_DBG1(("dmRegisterDevice: directly attached expander\n"));
    oneDeviceData->directlyAttached = agTRUE;
    oneDeviceData->dmDeviceInfo.ext =  (bit16)(oneDeviceData->dmDeviceInfo.ext | (0xF << 11));
  }
  else
  {
    DM_DBG1(("dmRegisterDevice: NOT directly attached expander\n"));
    oneDeviceData->directlyAttached = agFALSE;
  }      
  
  if (onePortContext->DiscoveryState == DM_DSTATE_NOT_STARTED)
  {
    DM_DBG3(("dmRegisterDevice: DM_DSTATE_NOT_STARTED\n"));
    /* before the discovery is started */
    oneExpander = dmDiscoveringExpanderAlloc(dmRoot, onePortContext, oneDeviceData);
    if ( oneExpander != agNULL)
    {
      oneExpander->agDevHandle = agDevHandle;
      /* update SAS address field */
      oneExpander->dmDevice->SASAddressID.sasAddressHi = DM_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
      oneExpander->dmDevice->SASAddressID.sasAddressLo = DM_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
      DM_DBG3(("dmRegisterDevice: AddrHi 0x%08x AddrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi, oneExpander->dmDevice->SASAddressID.sasAddressLo));
      dmDiscoveringExpanderAdd(dmRoot, onePortContext, oneExpander);      
    }
    else
    {
      DM_DBG1(("dmRegisterDevice: failed to allocate expander !!!\n"));
      /* remember that the registration failed so that a discovery can't be started */
      onePortContext->RegFailed = agTRUE;
      return DM_RC_FAILURE;
    }
  }
  else
  {
    /*
      the discovery has started. Alloc and add have been done.
      find an expander using dmDeviceInfo, and update the expander's agDevHandle
      call dmExpFind()
    */
    DM_DBG3(("dmRegisterDevice: NOT DM_DSTATE_NOT_STARTED\n"));
    sasAddressHi = DM_GET_SAS_ADDRESSHI(dmDeviceInfo->sasAddressHi);
    sasAddressLo = DM_GET_SAS_ADDRESSLO(dmDeviceInfo->sasAddressLo);
    DM_DBG3(("dmRegisterDevice: AddrHi 0x%08x AddrLo 0x%08x\n", sasAddressHi, sasAddressLo));
    oneExpander = dmExpFind(dmRoot, onePortContext, sasAddressHi, sasAddressLo);
    if ( oneExpander != agNULL)
    {
      oneExpander->agDevHandle = agDevHandle;
    }
    else
    {
      DM_DBG1(("dmRegisterDevice: not allowed case, wrong !!!\n"));
      return DM_RC_FAILURE;
    }
  }

  return DM_RC_SUCCESS;
}	   			 

osGLOBAL dmExpander_t *
dmDiscoveringExpanderAlloc(
                           dmRoot_t                 *dmRoot,
                           dmIntPortContext_t       *onePortContext,
                           dmDeviceData_t           *oneDeviceData
                          )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmExpander_t              *oneExpander = agNULL;
  dmList_t                  *ExpanderList;
  
  DM_DBG3(("dmDiscoveringExpanderAlloc: start\n"));
  DM_DBG3(("dmDiscoveringExpanderAlloc: did %d\n", oneDeviceData->id));
  DM_DBG3(("dmDiscoveringExpanderAlloc: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDiscoveringExpanderAlloc: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));

  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmDiscoveringExpanderAlloc: invalid port!!!\n"));
    return agNULL;
  }
  
 
  /* check exitence in dmAllShared->mainExpanderList */
  oneExpander = dmExpMainListFind(dmRoot, 
                                  onePortContext, 
				  oneDeviceData->SASAddressID.sasAddressHi, 
				  oneDeviceData->SASAddressID.sasAddressLo); 
  
  if (oneExpander == agNULL)
  {
    tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
    if (DMLIST_EMPTY(&(dmAllShared->freeExpanderList)))
    {
      DM_DBG1(("dmDiscoveringExpanderAlloc: no free expanders pid %d!!!\n", onePortContext->id));
      tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
      return agNULL;
    }
    else
    {
      tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    }
  
    tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
    DMLIST_DEQUEUE_FROM_HEAD(&ExpanderList, &(dmAllShared->freeExpanderList));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
  
    oneExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
  }
  
  if (oneExpander != agNULL)
  {
    DM_DBG1(("dmDiscoveringExpanderAlloc: pid %d exp id %d \n", onePortContext->id, oneExpander->id));

    tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
    DMLIST_DEQUEUE_THIS(&(oneExpander->linkNode));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
     
    oneExpander->dmDevice = oneDeviceData;
    oneExpander->dmUpStreamExpander = agNULL;
    oneExpander->dmCurrentDownStreamExpander = agNULL;
    oneExpander->dmReturnginExpander = agNULL;
    oneExpander->hasUpStreamDevice = agFALSE;
    oneExpander->numOfUpStreamPhys = 0;
    oneExpander->currentUpStreamPhyIndex = 0;
    oneExpander->discoveringPhyId = 0;    
    oneExpander->underDiscovering = agFALSE; 
    dm_memset( &(oneExpander->currentIndex), 0, sizeof(oneExpander->currentIndex));
    
    oneDeviceData->dmExpander = oneExpander;
    DM_DBG3(("dmDiscoveringExpanderAlloc: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
    DM_DBG3(("dmDiscoveringExpanderAlloc: oneExpander %p did %d\n",  oneDeviceData->dmExpander,  oneDeviceData->dmExpander->id));
    
  }
  
  return oneExpander;
}

osGLOBAL void
dmDiscoveringExpanderAdd(
                         dmRoot_t                 *dmRoot,
                         dmIntPortContext_t       *onePortContext,
                         dmExpander_t             *oneExpander
                        )
{
  DM_DBG3(("dmDiscoveringExpanderAdd: start\n"));
  DM_DBG3(("dmDiscoveringExpanderAdd: expander id %d\n", oneExpander->id));
  DM_DBG3(("dmDiscoveringExpanderAdd: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDiscoveringExpanderAdd: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));  
  
  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmDiscoveringExpanderAdd: invalid port!!!\n"));
    return;
  }
  if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
  {
    DM_DBG3(("dmDiscoveringExpanderAdd: UPSTREAM\n"));
  }
  else if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
  {
    DM_DBG3(("dmDiscoveringExpanderAdd: DOWNSTREAM\n"));
  }
  else
  {
    DM_DBG3(("dmDiscoveringExpanderAdd: status %d\n", onePortContext->discovery.status));
  }

  if ( oneExpander->underDiscovering == agFALSE)
  {
    DM_DBG3(("dmDiscoveringExpanderAdd: ADDED \n"));
  
    oneExpander->underDiscovering = agTRUE;
    tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
    DMLIST_ENQUEUE_AT_TAIL(&(oneExpander->linkNode), &(onePortContext->discovery.discoveringExpanderList));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
  }
  
  return;
}		  

osGLOBAL dmExpander_t *
dmFindConfigurableExp(
                      dmRoot_t                  *dmRoot,
                      dmIntPortContext_t        *onePortContext,
                      dmExpander_t              *oneExpander
                     )
{
  dmExpander_t            *tempExpander;
  dmIntPortContext_t      *tmpOnePortContext = onePortContext;
  dmExpander_t            *ret = agNULL;
  DM_DBG3(("dmFindConfigurableExp: start\n"));
  
  if (oneExpander == agNULL)
  {
    DM_DBG3(("dmFindConfigurableExp: NULL expander\n"));
    return agNULL;
  }
  
  DM_DBG3(("dmFindConfigurableExp: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmFindConfigurableExp: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));	
  
  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    DM_DBG3(("dmFindConfigurableExp: empty UpdiscoveringExpanderList\n"));
    return agNULL;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
  }
  tempExpander = oneExpander->dmUpStreamExpander;
  while (tempExpander)
  {
    DM_DBG3(("dmFindConfigurableExp: loop exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
    DM_DBG3(("dmFindConfigurableExp: loop exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
    if (tempExpander->configRouteTable)
    {
      DM_DBG3(("dmFindConfigurableExp: found configurable expander\n"));
      ret = tempExpander;
      break;
    }
   tempExpander = tempExpander->dmUpStreamExpander;
  }
  
  return ret;
}

osGLOBAL bit32
dmDuplicateConfigSASAddr(
                         dmRoot_t                 *dmRoot,
                         dmExpander_t             *oneExpander,
                         bit32                    configSASAddressHi,
                         bit32                    configSASAddressLo
                        )
{
  bit32 i;
  bit32 ret = agFALSE;
  DM_DBG3(("dmDuplicateConfigSASAddr: start\n"));
  
  if (oneExpander == agNULL)
  {
    DM_DBG3(("dmDuplicateConfigSASAddr: NULL expander\n"));
    return agTRUE;
  }  
  
  if (oneExpander->dmDevice->SASAddressID.sasAddressHi == configSASAddressHi &&
      oneExpander->dmDevice->SASAddressID.sasAddressLo == configSASAddressLo
     )
  {
    DM_DBG3(("dmDuplicateConfigSASAddr: unnecessary\n"));
    return agTRUE;
  }	

  DM_DBG3(("dmDuplicateConfigSASAddr: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDuplicateConfigSASAddr: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG3(("dmDuplicateConfigSASAddr: configsasAddressHi 0x%08x\n", configSASAddressHi));
  DM_DBG3(("dmDuplicateConfigSASAddr: configsasAddressLo 0x%08x\n", configSASAddressLo));
  DM_DBG3(("dmDuplicateConfigSASAddr: configSASAddrTableIndex %d\n", oneExpander->configSASAddrTableIndex));    	
  for(i=0;i<oneExpander->configSASAddrTableIndex;i++)
  {
    if (oneExpander->configSASAddressHiTable[i] == configSASAddressHi &&
        oneExpander->configSASAddressLoTable[i] == configSASAddressLo
        )
    {
      DM_DBG3(("dmDuplicateConfigSASAddr: FOUND\n"));
      ret = agTRUE;
      break;
    }
  }
  /* new one; let's add it */
  if (ret == agFALSE)
  {
    DM_DBG3(("dmDuplicateConfigSASAddr: adding configSAS Addr\n"));
    DM_DBG3(("dmDuplicateConfigSASAddr: configSASAddrTableIndex %d\n", oneExpander->configSASAddrTableIndex));   
    oneExpander->configSASAddressHiTable[oneExpander->configSASAddrTableIndex] = configSASAddressHi;
    oneExpander->configSASAddressLoTable[oneExpander->configSASAddrTableIndex] = configSASAddressLo;
    oneExpander->configSASAddrTableIndex++;
  }
  
  return ret;
}

osGLOBAL bit16
dmFindCurrentDownStreamPhyIndex(
                                dmRoot_t          *dmRoot,
                                dmExpander_t      *oneExpander
                                )
{
  dmExpander_t       *DownStreamExpander;
  bit16              index = 0;
  bit16              i;
  bit8               phyId = 0;
  
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: start\n"));
  
  if (oneExpander == agNULL)
  {
    DM_DBG1(("dmFindCurrentDownStreamPhyIndex: wrong, oneExpander is NULL!!!\n"));
    return 0;
  }
  
  DownStreamExpander = oneExpander->dmCurrentDownStreamExpander;
  
  if (DownStreamExpander == agNULL)
  {
    DM_DBG1(("dmFindCurrentDownStreamPhyIndex: wrong, DownStreamExpander is NULL!!!\n"));
    return 0;
  }
  
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: downstream exp addrHi 0x%08x\n", DownStreamExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: downstream exp addrLo 0x%08x\n", DownStreamExpander->dmDevice->SASAddressID.sasAddressLo));
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: numOfDownStreamPhys %d\n", oneExpander->numOfDownStreamPhys));
  
  phyId = DownStreamExpander->upStreamPhys[0];
  
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: phyId %d\n", phyId));
  
  for (i=0; i<oneExpander->numOfDownStreamPhys;i++)
  {
    if (oneExpander->downStreamPhys[i] == phyId)
    {
      index = i;
      break;
    }
  }
  DM_DBG3(("dmFindCurrentDownStreamPhyIndex: index %d\n", index));
  return index;
}

osGLOBAL bit32
dmFindDiscoveringExpander(
                          dmRoot_t                  *dmRoot,
                          dmIntPortContext_t        *onePortContext,
                          dmExpander_t              *oneExpander
                         )
{
  dmList_t                *ExpanderList;
  dmExpander_t            *tempExpander;
  dmIntPortContext_t      *tmpOnePortContext = onePortContext;
  bit32                   ret = agFALSE;
  
  
  DM_DBG3(("dmFindDiscoveringExpander: start\n"));
  
  DM_DBG3(("dmFindDiscoveringExpander: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmFindDiscoveringExpander: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo));

  if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    DM_DBG3(("dmFindDiscoveringExpander: empty discoveringExpanderList\n"));
    return ret;
  }
  ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if (tempExpander == oneExpander)
    {
      if (tempExpander != agNULL)
      {
        DM_DBG3(("dmFindDiscoveringExpander: match, expander id %d\n", tempExpander->id));
        DM_DBG3(("dmFindDiscoveringExpander: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
        DM_DBG3(("dmFindDiscoveringExpander: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
      }
      ret = agTRUE;
      break;
    }
    
    ExpanderList = ExpanderList->flink;
  }	
  
  
  return ret;
}			 


osGLOBAL void
dmDiscoveringExpanderRemove(
                            dmRoot_t                 *dmRoot,
                            dmIntPortContext_t       *onePortContext,
                            dmExpander_t             *oneExpander
                           )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  
  DM_DBG3(("dmDiscoveringExpanderRemove: start\n"));
  DM_DBG3(("dmDiscoveringExpanderRemove: expander id %d\n", oneExpander->id));
  DM_DBG3(("dmDiscoveringExpanderRemove: exp addrHi 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressHi));
  DM_DBG3(("dmDiscoveringExpanderRemove: exp addrLo 0x%08x\n", oneExpander->dmDevice->SASAddressID.sasAddressLo)); 
  
  DM_DBG3(("dmDiscoveringExpanderRemove: BEFORE\n"));
  dmDumpAllExp(dmRoot, onePortContext, oneExpander);
  dmDumpAllUpExp(dmRoot, onePortContext, oneExpander);
  dmDumpAllFreeExp(dmRoot);
  
  // if is temporary till smp problem is fixed
  if (dmFindDiscoveringExpander(dmRoot, onePortContext, oneExpander) == agTRUE)
  {
    DM_DBG3(("dmDiscoveringExpanderRemove: oneDeviceData %p did %d\n", oneExpander->dmDevice, oneExpander->dmDevice->id));
    DM_DBG3(("dmDiscoveringExpanderRemove: oneExpander %p did %d\n", oneExpander, oneExpander->id));
    
    if (oneExpander != oneExpander->dmDevice->dmExpander)
    {
      DM_DBG3(("dmDiscoveringExpanderRemove: before !!! wrong !!!\n"));  
    }
    oneExpander->underDiscovering = agFALSE;
    oneExpander->discoveringPhyId = 0;
    tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
    DMLIST_DEQUEUE_THIS(&(oneExpander->linkNode));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);

    if (onePortContext->discovery.status == DISCOVERY_UP_STREAM)
    {
      DM_DBG3(("dmDiscoveringExpanderRemove: DISCOVERY_UP_STREAM\n"));
      tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
      DMLIST_ENQUEUE_AT_TAIL(&(oneExpander->upNode), &(onePortContext->discovery.UpdiscoveringExpanderList)); 
      tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
      onePortContext->discovery.NumOfUpExp++;
    }
    else
    {
      DM_DBG3(("dmDiscoveringExpanderRemove: Status %d\n", onePortContext->discovery.status));
      tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
      DMLIST_ENQUEUE_AT_TAIL(&(oneExpander->linkNode), &(dmAllShared->mainExpanderList));
//      DMLIST_ENQUEUE_AT_TAIL(&(oneExpander->linkNode), &(dmAllShared->freeExpanderList));
      tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    }
    // error checking
    if (oneExpander != oneExpander->dmDevice->dmExpander)
    {
      DM_DBG3(("dmDiscoveringExpanderRemove: after !!! wrong !!!\n"));  
    }
      
  } //end temp if
  else
  {
    DM_DBG1(("dmDiscoveringExpanderRemove: !!! problem !!!\n"));
  }

  DM_DBG3(("dmDiscoveringExpanderRemove: AFTER\n"));
  
  dmDumpAllExp(dmRoot, onePortContext, oneExpander);
  dmDumpAllUpExp(dmRoot, onePortContext, oneExpander);
  dmDumpAllFreeExp(dmRoot);
  
  return;
}

/*
  returns an expander with sasAddrLo, sasAddrHi from dmAllShared->mainExpanderList
*/
osGLOBAL dmExpander_t *
dmExpMainListFind(
                  dmRoot_t            *dmRoot,
                  dmIntPortContext_t  *onePortContext,
                  bit32               sasAddrHi,
                  bit32               sasAddrLo
                 )
{
  dmIntRoot_t        *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t     *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmList_t           *ExpanderList;
  dmExpander_t       *tempExpander;

  DM_DBG3(("dmExpMainListFind: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->mainExpanderList)))
  {
    DM_DBG1(("dmExpMainListFind: empty mainExpanderList\n"));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    return agNULL;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
  }
  ExpanderList = dmAllShared->mainExpanderList.flink;
  while (ExpanderList != &(dmAllShared->mainExpanderList))
  {
    tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if (tempExpander == agNULL)
    {
      DM_DBG1(("dmExpMainListFind: tempExpander is NULL!!!\n"));
      return agNULL;
    }    
    DM_DBG3(("dmExpMainListFind: expander id %d\n", tempExpander->id));
    DM_DBG3(("dmExpMainListFind: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
    DM_DBG3(("dmExpMainListFind: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
    if ((tempExpander->dmDevice->SASAddressID.sasAddressHi == sasAddrHi) &&
        (tempExpander->dmDevice->SASAddressID.sasAddressLo == sasAddrLo) &&
        (tempExpander->dmDevice->dmPortContext == onePortContext)
       )
    {
      DM_DBG3(("dmExpMainListFind: found expander id %d\n", tempExpander->id));
      DM_DBG3(("dmExpMainListFind: found exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG3(("dmExpMainListFind: found exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
      return tempExpander;
    }       	
    ExpanderList = ExpanderList->flink;
  }
  return agNULL;

}

/*
  returns an expander with sasAddrLo, sasAddrHi from discoveringExpanderList
*/
osGLOBAL dmExpander_t *
dmExpFind(
          dmRoot_t            *dmRoot,
          dmIntPortContext_t  *onePortContext,
          bit32               sasAddrHi,
          bit32               sasAddrLo
         )
{
  dmList_t           *ExpanderList;
  dmExpander_t       *tempExpander;
  dmIntPortContext_t *tmpOnePortContext = onePortContext;
  DM_DBG3(("dmExpFind: start\n"));

  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    DM_DBG3(("dmExpFind tdsaDumpAllExp: empty discoveringExpanderList\n"));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    return agNULL;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
  }
  ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
  while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
  {
    tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if (tempExpander == agNULL)
    {
      DM_DBG1(("dmExpFind: tempExpander is NULL!!!\n"));
      return agNULL;
    }
    DM_DBG3(("dmExpFind: expander id %d\n", tempExpander->id));
    DM_DBG3(("dmExpFind: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
    DM_DBG3(("dmExpFind: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
    if ((tempExpander->dmDevice->SASAddressID.sasAddressHi == sasAddrHi) &&
        (tempExpander->dmDevice->SASAddressID.sasAddressLo == sasAddrLo) &&
        (tempExpander->dmDevice->dmPortContext == onePortContext)
       )
    {
      DM_DBG3(("dmExpFind: found\n"));
      return tempExpander;
    }       	
    ExpanderList = ExpanderList->flink;
  }
  return agNULL;
}
			     
osGLOBAL bit32
dmDiscoverCheck(
                dmRoot_t 	    	*dmRoot, 
                dmIntPortContext_t      *onePortContext	
                )
{
  DM_DBG3(("dmDiscoverCheck: start\n"));
  
  if (onePortContext == agNULL)
  {
    DM_DBG1(("dmDiscoverCheck: onePortContext is NULL!!!\n"));
    return agTRUE;
  }
  if (onePortContext->valid == agFALSE)
  {
    DM_DBG1(("dmDiscoverCheck: invalid port!!!\n"));
    return agTRUE;
  }
  if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED ||
      onePortContext->discovery.status == DISCOVERY_SAS_DONE  
     )
  {
    DM_DBG1(("dmDiscoverCheck: aborted discovery!!!\n"));
    tddmDiscoverCB(
                   dmRoot,
                   onePortContext->dmPortContext,
                   dmDiscAborted
	          );
    return agTRUE;
  }
  
  return agFALSE;
}

/* ??? needs to handle pending SMPs 
   move from dmAllShared->discoveringExpanderList to dmAllShared->mainExpanderList  
*/
osGLOBAL void
dmDiscoverAbort(
                dmRoot_t 	    	*dmRoot, 
                dmIntPortContext_t      *onePortContext	
                )
{
  DM_DBG1(("dmDiscoverAbort: start\n"));
  
  if (onePortContext->DiscoveryState == DM_DSTATE_COMPLETED ||
      onePortContext->discovery.status == DISCOVERY_SAS_DONE)
  {
    DM_DBG1(("dmDiscoverAbort: not allowed case!!! onePortContext->DiscoveryState 0x%x onePortContext->discovery.status 0x%x\n", 
    onePortContext->DiscoveryState, onePortContext->discovery.status));
    return;  
  }      
  
  onePortContext->DiscoveryState = DM_DSTATE_COMPLETED;
  onePortContext->discovery.status = DISCOVERY_SAS_DONE;                
  
  /* move from dmAllShared->discoveringExpanderList to dmAllShared->mainExpanderList */
  dmCleanAllExp(dmRoot, onePortContext);

 
  return;

			  
}						  		  	  

/* move from dmAllShared->discoveringExpanderList to dmAllShared->mainExpanderList */
osGLOBAL void
dmCleanAllExp(
              dmRoot_t                 *dmRoot,
              dmIntPortContext_t       *onePortContext
             )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmList_t                  *ExpanderList;
  dmExpander_t              *tempExpander;
  dmExpander_t              *oneExpander = agNULL;
  dmIntPortContext_t        *tmpOnePortContext = onePortContext;
  
  DM_DBG3(("dmCleanAllExp: start\n"));
  DM_DBG3(("dmCleanAllExp: pid %d\n", onePortContext->id));
  
  DM_DBG3(("dmCleanAllExp: before all clean up\n")); 
  dmDumpAllFreeExp(dmRoot);
  
  /* clean up UpdiscoveringExpanderList*/
  DM_DBG3(("dmCleanAllExp: clean discoveringExpanderList\n"));
  if (!DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
  {
    ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
    while (ExpanderList != &(tmpOnePortContext->discovery.discoveringExpanderList))
    {
      tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
      if (tempExpander == agNULL)
      {
        DM_DBG1(("dmCleanAllExp: tempExpander is NULL!!!\n"));
        return;
      }    
      DM_DBG3(("dmCleanAllExp: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG3(("dmCleanAllExp: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
      DM_DBG3(("dmCleanAllExp: exp id %d\n", tempExpander->id));
      
      oneExpander = dmExpMainListFind(dmRoot, 
                                      tmpOnePortContext, 
                                      tempExpander->dmDevice->SASAddressID.sasAddressHi, 
                                      tempExpander->dmDevice->SASAddressID.sasAddressLo);      
      if (oneExpander == agNULL)
      {      
        DM_DBG3(("dmCleanAllExp: moving\n"));
        DM_DBG3(("dmCleanAllExp: moving, exp id %d\n", tempExpander->id));
        /* putting back to the free pool */
        tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
        DMLIST_DEQUEUE_THIS(&(tempExpander->linkNode));
//      DMLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(dmAllShared->freeExpanderList));
        DMLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(dmAllShared->mainExpanderList));

        if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.discoveringExpanderList)))
        {
          tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
          break;
        }
        else
        {
          tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);   
        }
        ExpanderList = tmpOnePortContext->discovery.discoveringExpanderList.flink;
      }
      else
      {
        DM_DBG3(("dmCleanAllExp: in mainExpanderList; skippig\n"));              
        ExpanderList =  ExpanderList->flink;    
      }                  
    }
  }
  else
  {
    DM_DBG3(("dmCleanAllExp: empty discoveringExpanderList\n")); 
  }
  
  /* reset discoveringExpanderList */
  DMLIST_INIT_HDR(&(tmpOnePortContext->discovery.discoveringExpanderList));    

  /* clean up UpdiscoveringExpanderList*/
  DM_DBG3(("dmCleanAllExp: clean UpdiscoveringExpanderList\n"));
  if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.UpdiscoveringExpanderList)))
  {
    DM_DBG3(("dmCleanAllExp: empty UpdiscoveringExpanderList\n"));
    return;
  }
  ExpanderList = tmpOnePortContext->discovery.UpdiscoveringExpanderList.flink;
  while (ExpanderList != &(tmpOnePortContext->discovery.UpdiscoveringExpanderList))
  {
    tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, upNode, ExpanderList);
    if (tempExpander == agNULL)
    {
      DM_DBG1(("dmCleanAllExp: tempExpander is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmCleanAllExp: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
    DM_DBG3(("dmCleanAllExp: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
    DM_DBG3(("dmCleanAllExp: exp id %d\n", tempExpander->id));
    oneExpander = dmExpMainListFind(dmRoot, 
                                    tmpOnePortContext, 
                                    tempExpander->dmDevice->SASAddressID.sasAddressHi, 
                                    tempExpander->dmDevice->SASAddressID.sasAddressLo);      
    if (oneExpander == agNULL)
    {      
      DM_DBG3(("dmCleanAllExp: moving\n"));
      DM_DBG3(("dmCleanAllExp: moving exp id %d\n", tempExpander->id));
      tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
      DMLIST_DEQUEUE_THIS(&(tempExpander->upNode));
      DMLIST_ENQUEUE_AT_TAIL(&(tempExpander->linkNode), &(dmAllShared->mainExpanderList));

      if (DMLIST_EMPTY(&(tmpOnePortContext->discovery.UpdiscoveringExpanderList)))
      {
        tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
        break;
      }
      else
      {
        tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);   
      }
      ExpanderList = tmpOnePortContext->discovery.UpdiscoveringExpanderList.flink;
    }
    else
    {
      DM_DBG3(("dmCleanAllExp: in mainExpanderList; skippig\n"));              
      ExpanderList =  ExpanderList->flink;    
    }                
  }
  
  /* reset UpdiscoveringExpanderList */
  DMLIST_INIT_HDR(&(tmpOnePortContext->discovery.UpdiscoveringExpanderList));    
  
  DM_DBG3(("dmCleanAllExp: after all clean up\n")); 
  dmDumpAllFreeExp(dmRoot);
  
  return;
}

osGLOBAL void
dmInternalRemovals(
                   dmRoot_t                 *dmRoot,
                   dmIntPortContext_t       *onePortContext
                   )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t            *oneDeviceData = agNULL;
  dmList_t                  *DeviceListList;
  
  
  DM_DBG3(("dmInternalRemovals: start\n"));
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmInternalRemovals: empty device list\n"));
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }
  
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmInternalRemovals: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmInternalRemovals: loop did %d\n", oneDeviceData->id));
    DM_DBG3(("dmInternalRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmInternalRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    DM_DBG3(("dmInternalRemovals: valid %d\n", oneDeviceData->valid));    
    DM_DBG3(("dmInternalRemovals: valid2 %d\n", oneDeviceData->valid2));    
    DM_DBG3(("dmInternalRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));    
    if ( oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmInternalRemovals: right portcontext pid %d\n", onePortContext->id));
      if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_INCREMENTAL_START)
      {
        DM_DBG3(("dmInternalRemovals: incremental discovery\n"));
        oneDeviceData->valid2 = agFALSE;
      }
      else
      {
        DM_DBG3(("dmInternalRemovals: full discovery\n"));
        oneDeviceData->valid = agFALSE;
      }
      DeviceListList = DeviceListList->flink;
    }    
    else
    {
      if (oneDeviceData->dmPortContext != agNULL)
      {
        DM_DBG3(("dmInternalRemovals: different portcontext; oneDeviceData->dmPortContext pid %d oneportcontext pid %d\n", oneDeviceData->dmPortContext->id, onePortContext->id));
      }
      else
      {
        DM_DBG3(("dmInternalRemovals: different portcontext; oneDeviceData->dmPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }  
  }
  
  
  return;
}

osGLOBAL void
dmDiscoveryResetProcessed(
                          dmRoot_t                 *dmRoot,
                          dmIntPortContext_t       *onePortContext
                         )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG3(("dmDiscoveryResetProcessed: start\n"));
  
  /* reinitialize the device data belonging to this portcontext */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDiscoveryResetProcessed: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmDiscoveryResetProcessed: loop did %d\n", oneDeviceData->id));
    if (oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmDiscoveryResetProcessed: resetting procssed flag\n"));
      oneDeviceData->processed = agFALSE;
    }
    DeviceListList = DeviceListList->flink;  
  }
  
  return;
}			 

/*
  calls
osGLOBAL void 
tddmDiscoverCB(
               dmRoot_t 		*dmRoot,
               dmPortContext_t		*dmPortContext,
               bit32			eventStatus
              )
  
*/
osGLOBAL void
dmDiscoverDone(
               dmRoot_t                 *dmRoot,
               dmIntPortContext_t       *onePortContext,
               bit32                    flag
              )
{
 
  DM_DBG3(("dmDiscoverDone: start\n"));
  DM_DBG3(("dmDiscoverDone: pid %d\n", onePortContext->id));

  /* Set discovery status */
  onePortContext->discovery.status = DISCOVERY_SAS_DONE;                
  
 
  /* clean up expanders data strucures; move to free exp when device is cleaned */
  dmCleanAllExp(dmRoot, onePortContext);
  
  dmDumpAllMainExp(dmRoot, onePortContext);

  dmDiscoveryResetProcessed(dmRoot, onePortContext);
  
  dmDiscoveryDumpMCN(dmRoot, onePortContext);
  
  if (onePortContext->discovery.SeenBC == agTRUE)
  {
    DM_DBG3(("dmDiscoverDone: broadcast change; discover again\n"));
    dmDiscoveryResetMCN(dmRoot, onePortContext);
    
    dmInternalRemovals(dmRoot, onePortContext);

    /* processed broadcast change */
    onePortContext->discovery.SeenBC = agFALSE;
    if (onePortContext->discovery.ResetTriggerred == agTRUE)
    {
      DM_DBG3(("dmDiscoverDone: dmBCTimer\n"));
      dmBCTimer(dmRoot, onePortContext);
    }
    else
    {

      dmIncrementalDiscover(dmRoot, onePortContext, agTRUE); 		  
    }
  }
  else
  {
    onePortContext->DiscoveryState = DM_DSTATE_COMPLETED;
  
    if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_FULL_START)
    { 
      if (flag == DM_RC_SUCCESS)
      {

       dmResetReported(dmRoot,
                       onePortContext
                      );
		      
       dmDiscoveryReportMCN(dmRoot,
                            onePortContext
                           );
                           		      

       /* call tddmDiscoverCB() */
       tddmDiscoverCB(
                       dmRoot,
                       onePortContext->dmPortContext,
                       dmDiscCompleted
                      );
      }
      else if (flag != DM_RC_SUCCESS || onePortContext->discovery.DeferredError == agTRUE)
      {
        onePortContext->DiscoveryState = DM_DSTATE_COMPLETED_WITH_FAILURE;
        DM_DBG1(("dmDiscoverDone: Error; clean up!!!\n"));
	
        dmDiscoveryInvalidateDevices(dmRoot,
                                     onePortContext
                                    );
			
        tddmDiscoverCB(
                       dmRoot,
                       onePortContext->dmPortContext,
                       dmDiscFailed
                      );
      }
    }
    else
    {
      if (flag == DM_RC_SUCCESS)
      { 
        dmReportChanges(dmRoot,
                        onePortContext
                       );
        dmDiscoveryReportMCN(dmRoot,
                             onePortContext
                            );
        tddmDiscoverCB(
                       dmRoot,
                       onePortContext->dmPortContext,
                       dmDiscCompleted
                      );
      }
      else if (flag != DM_RC_SUCCESS || onePortContext->discovery.DeferredError == agTRUE)
      {
        onePortContext->DiscoveryState = DM_DSTATE_COMPLETED_WITH_FAILURE;
        dmDiscoveryInvalidateDevices(dmRoot,
                                     onePortContext
                                    );
		
        tddmDiscoverCB(
                       dmRoot,
                       onePortContext->dmPortContext,
                       dmDiscFailed
                      );
      }                        
    }
  }
  return;
}

/* called by dmDiscoveryErrorRemovals() or dmReportRemovals() on discovery failure */
osGLOBAL void
dmSubReportRemovals(
                   dmRoot_t                  *dmRoot,
                   dmIntPortContext_t        *onePortContext,
                   dmDeviceData_t            *oneDeviceData,
                   bit32                     flag
                  )
{
  dmDeviceData_t    *oneAttachedExpDeviceData = agNULL;
  DM_DBG3(("dmSubReportRemovals: start\n"));
  
  DM_DBG3(("dmSubReportRemovals: flag 0x%x\n", flag));
  if (flag == dmDeviceRemoval)
  {
    oneDeviceData->registered = agFALSE;
  }
  
  if (oneDeviceData->ExpDevice != agNULL)
  {
    DM_DBG3(("dmSubReportRemovals: attached expander case\n"));
    oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
    tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, &oneAttachedExpDeviceData->dmDeviceInfo, flag);
  }
  else
  {
    DM_DBG3(("dmSubReportRemovals: NO attached expander case\n"));
    tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, agNULL, flag);
  }
 
  
  /* this function is called at the end of discovery; reinitalizes oneDeviceData->reported */
  oneDeviceData->reported = agFALSE;
  return;
}


/* called by dmReportChanges() on discovery success */
osGLOBAL void
dmSubReportChanges(
                   dmRoot_t                  *dmRoot,
                   dmIntPortContext_t        *onePortContext,
		   dmDeviceData_t            *oneDeviceData,
                   bit32                     flag
                  )
{
  dmDeviceData_t    *oneAttachedExpDeviceData = agNULL;
  DM_DBG3(("dmSubReportChanges: start\n"));
  
  DM_DBG3(("dmSubReportChanges: flag 0x%x\n", flag));
  if (flag == dmDeviceRemoval)
  {
    oneDeviceData->registered = agFALSE;
  }
  if (oneDeviceData->reported == agFALSE)
  {
    if (oneDeviceData->ExpDevice != agNULL)
    {
      DM_DBG3(("dmSubReportChanges: attached expander case\n"));
      oneAttachedExpDeviceData = oneDeviceData->ExpDevice;
      tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, &oneAttachedExpDeviceData->dmDeviceInfo, flag);
    }
    else
    {
      DM_DBG3(("dmSubReportChanges: NO attached expander case\n"));
      tddmReportDevice(dmRoot, onePortContext->dmPortContext, &oneDeviceData->dmDeviceInfo, agNULL, flag);
    }
  }
  else
  {
    DM_DBG3(("dmSubReportChanges: skip; been reported\n"));  
  }
 
  
  /* this function is called at the end of discovery; reinitalizes oneDeviceData->reported */
  oneDeviceData->reported = agFALSE;
  return;
}

/* 
 should add or remove be reported per device???
*/
osGLOBAL void
dmReportChanges(
                dmRoot_t                  *dmRoot,
                dmIntPortContext_t        *onePortContext
               )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  bit32             added = agFALSE, removed = agFALSE;
//  dmDeviceData_t    *oneAttachedExpDeviceData = agNULL;
  
  DM_DBG3(("dmReportChanges: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmReportChanges: empty device list\n"));
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }
  
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmReportChanges: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmReportChanges: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmReportChanges: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    if ( oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmReportChanges: right portcontext\n"));
      if (oneDeviceData->SASAddressID.sasAddressHi == onePortContext->sasRemoteAddressHi &&
          oneDeviceData->SASAddressID.sasAddressLo == onePortContext->sasRemoteAddressLo      
         )
      {
        DM_DBG1(("dmReportChanges: keep, not reporting did 0x%x\n", oneDeviceData->id));
        oneDeviceData->valid = agTRUE;
        oneDeviceData->valid2 = agFALSE;
      }      
      else if ( (oneDeviceData->valid == agTRUE) && (oneDeviceData->valid2 == agTRUE) )
      {
        DM_DBG3(("dmReportChanges: same\n"));
        /* reset valid bit */
        oneDeviceData->valid = oneDeviceData->valid2;
        oneDeviceData->valid2 = agFALSE;      
        dmSubReportChanges(dmRoot, onePortContext, oneDeviceData, dmDeviceNoChange);
      }
      else if ( (oneDeviceData->valid == agTRUE) && (oneDeviceData->valid2 == agFALSE) )
      {
        DM_DBG3(("dmReportChanges: removed\n"));
        removed = agTRUE;
        /* reset valid bit */
        oneDeviceData->valid = oneDeviceData->valid2;
        oneDeviceData->valid2 = agFALSE;
      
        onePortContext->RegisteredDevNums--;
        dmSubReportChanges(dmRoot, onePortContext, oneDeviceData, dmDeviceRemoval);
      }
      else if ( (oneDeviceData->valid == agFALSE) && (oneDeviceData->valid2 == agTRUE) )
      {
        DM_DBG3(("dmReportChanges: added\n"));
        added = agTRUE;
        /* reset valid bit */      
        oneDeviceData->valid = oneDeviceData->valid2;
        oneDeviceData->valid2 = agFALSE;
        dmSubReportChanges(dmRoot, onePortContext, oneDeviceData, dmDeviceArrival);
      }
      else
      {
        DM_DBG3(("dmReportChanges: else\n"));
      }
    }
    else
    {
      DM_DBG3(("dmReportChanges: different portcontext\n"));
    }  
    DeviceListList = DeviceListList->flink;
  }
  /*
  osGLOBAL void 
tddmReportDevice(
                 dmRoot_t 		*dmRoot,
                 dmPortContext_t	*dmPortContext,
                 dmDeviceInfo_t		*dmDeviceInfo,
                 dmDeviceInfo_t		*dmExpDeviceInfo,
		 bit32                   flag
		 
                 )

  */
  
  /* arrival or removal at once */
  if (added == agTRUE)
  {
    DM_DBG3(("dmReportChanges: added at the end\n"));
#if 0  /* TBD */  
    ostiInitiatorEvent(
                         tiRoot,
                         onePortContext->tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceArrival,
                         agNULL
                         );
#endif			 
  
  }
  if (removed == agTRUE)
  {
    DM_DBG3(("dmReportChanges: removed at the end\n"));
#if 0  /* TBD */  
    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDeviceChange,
                       tiDeviceRemoval,
                       agNULL
                       );
#endif  
  }
  
  if (onePortContext->discovery.forcedOK == agTRUE && added == agFALSE && removed == agFALSE)
  {
    DM_DBG3(("dmReportChanges: missed chance to report. forced to report OK\n"));
    onePortContext->discovery.forcedOK = agFALSE;
#if 0  /* TBD */  
    ostiInitiatorEvent(
                       tiRoot,
                       onePortContext->tiPortalContext,
                       agNULL,
                       tiIntrEventTypeDiscovery, 
                       tiDiscOK, 
                       agNULL
                       );
#endif		           
  }
  
  if (added == agFALSE && removed == agFALSE)
  {
    DM_DBG3(("dmReportChanges: the same\n"));
  }
  
  return;
}

osGLOBAL void
dmReportRemovals(
                 dmRoot_t                  *dmRoot,
                 dmIntPortContext_t        *onePortContext,
                 bit32                     flag
                )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  bit32             removed = agFALSE;
  
  DM_DBG1(("dmReportRemovals: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmReportRemovals: empty device list\n"));
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }
  
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmReportRemovals: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmReportRemovals: loop did %d\n", oneDeviceData->id));
    DM_DBG3(("dmReportRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmReportRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    DM_DBG3(("dmReportRemovals: valid %d\n", oneDeviceData->valid));    
    DM_DBG3(("dmReportRemovals: valid2 %d\n", oneDeviceData->valid2));    
    DM_DBG3(("dmReportRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));    
    if ( oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmReportRemovals: right portcontext pid %d\n", onePortContext->id));
      if (oneDeviceData->SASAddressID.sasAddressHi == onePortContext->sasRemoteAddressHi &&
          oneDeviceData->SASAddressID.sasAddressLo == onePortContext->sasRemoteAddressLo      
         )
      {
        DM_DBG1(("dmReportRemovals: keeping\n"));
        oneDeviceData->valid = agTRUE;
        oneDeviceData->valid2 = agFALSE;
      }
      else if (oneDeviceData->valid == agTRUE)
      {    
        DM_DBG3(("dmReportRemovals: removing\n"));
       
        /* notify only reported devices to OS layer*/
        if ( DEVICE_IS_SSP_TARGET(oneDeviceData) || 
             DEVICE_IS_STP_TARGET(oneDeviceData) ||
             DEVICE_IS_SATA_DEVICE(oneDeviceData)
            )
        {
          removed = agTRUE;
        }

        /* all targets except expanders */
        DM_DBG3(("dmReportRemovals: did %d\n", oneDeviceData->id));
        DM_DBG3(("dmReportRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        DM_DBG3(("dmReportRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        onePortContext->RegisteredDevNums--;
        dmSubReportRemovals(dmRoot, onePortContext, oneDeviceData, dmDeviceRemoval);

        
        /* reset valid bit */
        oneDeviceData->valid = agFALSE;
        oneDeviceData->valid2 = agFALSE;
      
       
      }
      /* called by port invalid case */
      if (flag == agTRUE)
      {
        oneDeviceData->dmPortContext = agNULL;
      }
      DeviceListList = DeviceListList->flink;
    }    
    else
    {
      if (oneDeviceData->dmPortContext != agNULL)
      {
        DM_DBG3(("dmReportRemovals: different portcontext; oneDeviceData->dmPortContext pid %d oneportcontext pid %d\n", oneDeviceData->dmPortContext->id, onePortContext->id));
      }
      else
      {
        DM_DBG3(("dmReportRemovals: different portcontext; oneDeviceData->dmPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }  
  }
  
  if (removed == agTRUE)
  {
    DM_DBG3(("dmReportRemovals: removed at the end\n"));
#if 0 /* TBD */      
      ostiInitiatorEvent(
                         tiRoot,
                         onePortContext->tiPortalContext,
                         agNULL,
                         tiIntrEventTypeDeviceChange,
                         tiDeviceRemoval,
                         agNULL
                         );
#endif    
  }
  
  return;
}

osGLOBAL void
dmResetReported(
                dmRoot_t                  *dmRoot,
                dmIntPortContext_t        *onePortContext
               )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG3(("dmResetReported: start\n"));

  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmResetReported: empty device list\n"));
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }

  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmResetReported: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmResetReported: loop did %d\n", oneDeviceData->id));
    DM_DBG3(("dmResetReported: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmResetReported: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    DM_DBG3(("dmResetReported: valid %d\n", oneDeviceData->valid));    
    DM_DBG3(("dmResetReported: valid2 %d\n", oneDeviceData->valid2));    
    DM_DBG3(("dmResetReported: directlyAttached %d\n", oneDeviceData->directlyAttached));    
    if ( oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmResetReported: right portcontext pid %d\n", onePortContext->id));
      oneDeviceData->reported = agFALSE;
      DeviceListList = DeviceListList->flink;
    }    
    else
    {
      if (oneDeviceData->dmPortContext != agNULL)
      {
        DM_DBG3(("dmResetReported: different portcontext; oneDeviceData->dmPortContext pid %d oneportcontext pid %d\n", oneDeviceData->dmPortContext->id, onePortContext->id));
      }
      else
      {
        DM_DBG3(("dmResetReported: different portcontext; oneDeviceData->dmPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }  
  }
  
  return;
}	       

/* called on discover failure */
osGLOBAL void
dmDiscoveryInvalidateDevices(
                             dmRoot_t                  *dmRoot,
                             dmIntPortContext_t        *onePortContext
                            )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG1(("dmDiscoveryInvalidateDevices: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmDiscoveryInvalidateDevices: empty device list\n"));
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDiscoveryInvalidateDevices: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmDiscoveryInvalidateDevices: loop did %d\n", oneDeviceData->id));
    DM_DBG3(("dmDiscoveryInvalidateDevices: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmDiscoveryInvalidateDevices: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    DM_DBG3(("dmDiscoveryInvalidateDevices: valid %d\n", oneDeviceData->valid));    
    DM_DBG3(("dmDiscoveryInvalidateDevices: valid2 %d\n", oneDeviceData->valid2));    
    DM_DBG3(("dmDiscoveryInvalidateDevices: directlyAttached %d\n", oneDeviceData->directlyAttached));    
    if ( oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmDiscoveryInvalidateDevices: right portcontext pid %d\n", onePortContext->id));
      if (oneDeviceData->SASAddressID.sasAddressHi == onePortContext->sasRemoteAddressHi &&
          oneDeviceData->SASAddressID.sasAddressLo == onePortContext->sasRemoteAddressLo      
         )
      {
        DM_DBG1(("dmDiscoveryInvalidateDevices: keeping\n"));
        oneDeviceData->valid = agTRUE;
        oneDeviceData->valid2 = agFALSE;
      }
      else
      {            
        oneDeviceData->valid = agFALSE;
        oneDeviceData->valid2 = agFALSE;
        oneDeviceData->registered = agFALSE;
        oneDeviceData->reported = agFALSE;
        /* all targets other than expanders */
        DM_DBG3(("dmDiscoveryInvalidateDevices: did %d\n", oneDeviceData->id));
        DM_DBG3(("dmDiscoveryInvalidateDevices: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        DM_DBG3(("dmDiscoveryInvalidateDevices: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        onePortContext->RegisteredDevNums--;
      }
      DeviceListList = DeviceListList->flink;
    }    
    else
    {
      if (oneDeviceData->dmPortContext != agNULL)
      {
        DM_DBG3(("dmDiscoveryInvalidateDevices: different portcontext; oneDeviceData->dmPortContext pid %d oneportcontext pid %d\n", oneDeviceData->dmPortContext->id, onePortContext->id));
      }
      else
      {
        DM_DBG3(("dmDiscoveryInvalidateDevices: different portcontext; oneDeviceData->dmPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }  
  }
  
  return;
}


/* 
 should DM report the device removal to TDM on an error case?
 or
 DM simply removes the devices
 For now, the second option.
*/
osGLOBAL void
dmDiscoveryErrorRemovals(
                         dmRoot_t                  *dmRoot,
                         dmIntPortContext_t        *onePortContext
                        )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG1(("dmDiscoveryErrorRemovals: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmDiscoveryErrorRemovals: empty device list\n"));
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDiscoveryErrorRemovals: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmDiscoveryErrorRemovals: loop did %d\n", oneDeviceData->id));
    DM_DBG3(("dmDiscoveryErrorRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmDiscoveryErrorRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
    DM_DBG3(("dmDiscoveryErrorRemovals: valid %d\n", oneDeviceData->valid));    
    DM_DBG3(("dmDiscoveryErrorRemovals: valid2 %d\n", oneDeviceData->valid2));    
    DM_DBG3(("dmDiscoveryErrorRemovals: directlyAttached %d\n", oneDeviceData->directlyAttached));    
    if ( oneDeviceData->dmPortContext == onePortContext)
    {
      DM_DBG3(("dmDiscoveryErrorRemovals: right portcontext pid %d\n", onePortContext->id));
      if (oneDeviceData->SASAddressID.sasAddressHi == onePortContext->sasRemoteAddressHi &&
          oneDeviceData->SASAddressID.sasAddressLo == onePortContext->sasRemoteAddressLo      
         )
      {
        DM_DBG1(("dmDiscoveryErrorRemovals: keeping\n"));
        oneDeviceData->valid = agTRUE;
        oneDeviceData->valid2 = agFALSE;
      }
      else
      {            
        oneDeviceData->valid = agFALSE;
        oneDeviceData->valid2 = agFALSE;
      
        /* all targets other than expanders */
        DM_DBG3(("dmDiscoveryErrorRemovals: did %d\n", oneDeviceData->id));
        DM_DBG3(("dmDiscoveryErrorRemovals: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        DM_DBG3(("dmDiscoveryErrorRemovals: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        onePortContext->RegisteredDevNums--;
        dmSubReportRemovals(dmRoot, onePortContext, oneDeviceData, dmDeviceRemoval);
     
      }
      DeviceListList = DeviceListList->flink;
    }    
    else
    {
      if (oneDeviceData->dmPortContext != agNULL)
      {
        DM_DBG3(("dmDiscoveryErrorRemovals: different portcontext; oneDeviceData->dmPortContext pid %d oneportcontext pid %d\n", oneDeviceData->dmPortContext->id, onePortContext->id));
      }
      else
      {
        DM_DBG3(("dmDiscoveryErrorRemovals: different portcontext; oneDeviceData->dmPortContext pid NULL oneportcontext pid %d\n", onePortContext->id));
      }
      DeviceListList = DeviceListList->flink;
    }  
  }
  
  return;
}

/* move from dmAllShared->mainExpanderList to dmAllShared->freeExpanderList */
osGLOBAL void
dmDiscoveryExpanderCleanUp(
                           dmRoot_t                  *dmRoot,
                           dmIntPortContext_t        *onePortContext
                          )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmExpander_t      *oneExpander = agNULL;
  dmList_t          *ExpanderList = agNULL;
  dmDeviceData_t    *oneDeviceData = agNULL;
  
  DM_DBG3(("dmDiscoveryExpanderCleanUp: start\n"));
  /*
    be sure to call
    osGLOBAL void
    dmExpanderDeviceDataReInit(
                           dmRoot_t 	    *dmRoot, 
                           dmExpander_t     *oneExpander
                          );

  */
  
  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (!DMLIST_EMPTY(&(dmAllShared->mainExpanderList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    ExpanderList = dmAllShared->mainExpanderList.flink;
    while (ExpanderList != &(dmAllShared->mainExpanderList))
    {
      oneExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
      if (oneExpander == agNULL)
      {
        DM_DBG1(("dmDiscoveryExpanderCleanUp: oneExpander is NULL!!!\n"));
        return;
      }    
      oneDeviceData = oneExpander->dmDevice;
      DM_DBG3(("dmDiscoveryExpanderCleanUp: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      DM_DBG3(("dmDiscoveryExpanderCleanUp: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      if ( oneDeviceData->dmPortContext == onePortContext)
      {
        dmExpanderDeviceDataReInit(dmRoot, oneExpander);
        tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
        DMLIST_DEQUEUE_THIS(&(oneExpander->linkNode));
        DMLIST_ENQUEUE_AT_TAIL(&(oneExpander->linkNode), &(dmAllShared->freeExpanderList));
        
        if (DMLIST_EMPTY(&(dmAllShared->mainExpanderList)))
        {
          tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
          break;
        }
        else
        {
          tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);   
        }
        ExpanderList = dmAllShared->mainExpanderList.flink;
      }
      else
      {
        ExpanderList = ExpanderList->flink;
      }   
    }
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    DM_DBG3(("dmDiscoveryExpanderCleanUp: empty mainExpanderList\n")); 
  }  
  return;
  
}			


/* moves all devices from dmAllShared->MainDeviceList to dmAllShared->FreeDeviceList */
osGLOBAL void
dmDiscoveryDeviceCleanUp(
                         dmRoot_t                  *dmRoot,
                         dmIntPortContext_t        *onePortContext
                        )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  
  DM_DBG3(("dmDiscoveryDeviceCleanUp: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (!DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DeviceListList = dmAllShared->MainDeviceList.flink;
    while (DeviceListList != &(dmAllShared->MainDeviceList))
    {
      oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
      if (oneDeviceData == agNULL)
      {
        DM_DBG1(("dmDiscoveryDeviceCleanUp: oneDeviceData is NULL!!!\n"));
        return;
      }    
      DM_DBG3(("dmDiscoveryDeviceCleanUp: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      DM_DBG3(("dmDiscoveryDeviceCleanUp: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      if ( oneDeviceData->dmPortContext == onePortContext)
      {
        dmDeviceDataReInit(dmRoot, oneDeviceData);
        tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
        DMLIST_DEQUEUE_THIS(&(oneDeviceData->MainLink));
        DMLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->FreeLink), &(dmAllShared->FreeDeviceList));
        
        if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
        {
          tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
          break;
        }
        else
        {
          tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);   
        }
	onePortContext->RegisteredDevNums--;
        DeviceListList = dmAllShared->MainDeviceList.flink;
      }
      else
      {
        DeviceListList = DeviceListList->flink;
      }   
    }
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    DM_DBG3(("dmDiscoveryDeviceCleanUp: empty MainDeviceList\n")); 
  }  
  return;
}			
			


osGLOBAL void
dmDumpAllExp(
             dmRoot_t                  *dmRoot,
             dmIntPortContext_t        *onePortContext,
             dmExpander_t              *oneExpander
            )
{
  DM_DBG3(("dmDumpAllExp: start\n"));
  return;
}


osGLOBAL void
dmDumpAllUpExp(
               dmRoot_t                  *dmRoot,
               dmIntPortContext_t        *onePortContext,
               dmExpander_t              *oneExpander
              )
{
  DM_DBG3(("dmDumpAllUpExp: start\n"));
  return;
}
		    		    
osGLOBAL void
dmDumpAllFreeExp(
                 dmRoot_t                  *dmRoot
                )
{
  DM_DBG3(("dmDumpAllFreeExp: start\n"));
  return;
}

osGLOBAL void
dmDumpAllMainExp(
                 dmRoot_t                 *dmRoot,
                 dmIntPortContext_t       *onePortContext
                )
{
  dmIntRoot_t        *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t     *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmList_t           *ExpanderList;
  dmExpander_t       *tempExpander;
  
  DM_DBG3(("dmDumpAllMainExp: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_EXPANDER_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->mainExpanderList)))
  {
    DM_DBG3(("dmDumpAllMainExp: empty discoveringExpanderList\n"));
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_EXPANDER_LOCK);
  }
  
  ExpanderList = dmAllShared->mainExpanderList.flink;
  while (ExpanderList != &(dmAllShared->mainExpanderList))
  {
    tempExpander = DMLIST_OBJECT_BASE(dmExpander_t, linkNode, ExpanderList);
    if (tempExpander == agNULL)
    {
      DM_DBG1(("dmDumpAllMainExp: tempExpander is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmDumpAllMainExp: expander id %d\n", tempExpander->id));
    DM_DBG3(("dmDumpAllMainExp: exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
    DM_DBG3(("dmDumpAllMainExp: exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
    if ((tempExpander->dmDevice->dmPortContext == onePortContext)
       )
    {
      DM_DBG3(("dmDumpAllMainExp: found expander id %d\n", tempExpander->id));
      DM_DBG3(("dmDumpAllMainExp: found exp addrHi 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressHi));
      DM_DBG3(("dmDumpAllMainExp: found exp addrLo 0x%08x\n", tempExpander->dmDevice->SASAddressID.sasAddressLo));
    }       	
    ExpanderList = ExpanderList->flink;
  }
  return;
}


osGLOBAL void
dmDumpAllMainDevice(
                 dmRoot_t                 *dmRoot,
                 dmIntPortContext_t       *onePortContext
                )
{
  dmIntRoot_t        *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t     *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t     *oneDeviceData = agNULL;
  dmList_t           *DeviceListList;
  bit32              total = 0, port_total = 0;
  
  DM_DBG3(("dmDumpAllMainDevice: start\n"));
  
  tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
  if (DMLIST_EMPTY(&(dmAllShared->MainDeviceList)))
  {
    DM_DBG3(("dmDumpAllMainDevice: empty discoveringExpanderList\n"));
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    return;
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
  }
  
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG3(("dmDumpAllMainDevice: oneDeviceData is NULL!!!\n"));
      return;
    }    
    DM_DBG3(("dmDumpAllMainDevice: oneDeviceData id %d\n", oneDeviceData->id));
    DM_DBG3(("dmDumpAllMainDevice: addrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
    DM_DBG3(("dmDumpAllMainDevice: addrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
    total++;
    if ((oneDeviceData->dmPortContext == onePortContext)
       )
    {
      DM_DBG3(("dmDumpAllMainDevice: found oneDeviceData id %d\n", oneDeviceData->id));
      DM_DBG3(("dmDumpAllMainDevice: found addrHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));
      DM_DBG3(("dmDumpAllMainDevice: found addrLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
      port_total++;
    }       	
    DeviceListList = DeviceListList->flink;
  }
  DM_DBG3(("dmDumpAllMainDevice: total %d port_totaol %d\n", total, port_total));
  
  return;
}



osGLOBAL dmDeviceData_t *
dmAddSASToSharedcontext(
                         dmRoot_t              *dmRoot,
                         dmIntPortContext_t    *onePortContext,
                         dmSASSubID_t          *dmSASSubID,
                         dmDeviceData_t        *oneExpDeviceData,
                         bit8                   phyID
                        )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t    *oneDeviceData = agNULL;
  dmList_t          *DeviceListList;
  bit32             new_device = agTRUE;
  
  
  DM_DBG3(("dmAddSASToSharedcontext: start\n"));
  DM_DBG3(("dmAddSASToSharedcontext: oneportContext ID %d\n", onePortContext->id));
  
  if (oneExpDeviceData != agNULL)
  {
    DM_DBG3(("dmAddSASToSharedcontext: oneExpDeviceData sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
    oneExpDeviceData->SASAddressID.sasAddressHi, oneExpDeviceData->SASAddressID.sasAddressLo));       
  }
  else
  {
    DM_DBG3(("dmAddSASToSharedcontext: oneExpDeviceData is NULL\n"));
  }        
  /* find a device's existence */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmAddSASToSharedcontext: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }    
    if ((oneDeviceData->SASAddressID.sasAddressHi == dmSASSubID->sasAddressHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == dmSASSubID->sasAddressLo) &&
        (oneDeviceData->dmPortContext == onePortContext)
        )
    {
      DM_DBG3(("dmAddSASToSharedcontext: pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      new_device = agFALSE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }
  
  /* new device */
  if (new_device == agTRUE)
  {
    DM_DBG3(("dmAddSASToSharedcontext: new device\n"));
    DM_DBG3(("dmAddSASToSharedcontext: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
    dmSASSubID->sasAddressHi, dmSASSubID->sasAddressLo));         
    tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
    if (!DMLIST_NOT_EMPTY(&(dmAllShared->FreeDeviceList)))
    {
      tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
      DM_DBG1(("dmAddSASToSharedcontext: empty DeviceData FreeLink\n"));
      dmDumpAllMainDevice(dmRoot, onePortContext); 
      return agNULL;
    }
      
    DMLIST_DEQUEUE_FROM_HEAD(&DeviceListList, &(dmAllShared->FreeDeviceList));
    tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, FreeLink, DeviceListList);

    if (oneDeviceData != agNULL)
    {
      DM_DBG3(("dmAddSASToSharedcontext: oneDeviceData %p pid %d did %d\n", oneDeviceData, onePortContext->id, oneDeviceData->id));

      onePortContext->Count++;
      oneDeviceData->dmRoot = dmRoot;
      /* saving sas address */
      oneDeviceData->SASAddressID.sasAddressLo = dmSASSubID->sasAddressLo;
      oneDeviceData->SASAddressID.sasAddressHi = dmSASSubID->sasAddressHi;
      oneDeviceData->initiator_ssp_stp_smp = dmSASSubID->initiator_ssp_stp_smp;
      oneDeviceData->target_ssp_stp_smp = dmSASSubID->target_ssp_stp_smp;
      oneDeviceData->dmPortContext = onePortContext;
      /* handles both SAS target and STP-target, SATA-device */
      if (!DEVICE_IS_SATA_DEVICE(oneDeviceData) && !DEVICE_IS_STP_TARGET(oneDeviceData))
      {
        oneDeviceData->DeviceType = DM_SAS_DEVICE;
      }
      else
      {
        oneDeviceData->DeviceType = DM_SATA_DEVICE;
      }

      if (oneExpDeviceData != agNULL)
      {
        oneDeviceData->ExpDevice = oneExpDeviceData;
      }      
    
      /* set phyID only when it has initial value of 0xFF */
      if (oneDeviceData->phyID == 0xFF)
      {
        oneDeviceData->phyID = phyID;
      }
      /* incremental discovery */
      /* add device to incremental-related link. Report using this link 
         when incremental discovery is done */
      if (onePortContext->DiscoveryState == DM_DSTATE_NOT_STARTED)
      {
        DM_DBG3(("dmAddSASToSharedcontext: DM_DSTATE_NOT_STARTED\n"));
        DM_DBG3(("dmAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        DM_DBG3(("dmAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        oneDeviceData->valid = agTRUE;
      }
      else
      {
        if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_INCREMENTAL_START)
        {
          DM_DBG3(("dmAddSASToSharedcontext: incremental discovery\n"));
          DM_DBG3(("dmAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
          DM_DBG3(("dmAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
          oneDeviceData->valid2 = agTRUE;
        }
        else
        {
          DM_DBG3(("dmAddSASToSharedcontext: full discovery\n"));
          DM_DBG3(("dmAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
          DM_DBG3(("dmAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
         oneDeviceData->valid = agTRUE;
        }
      }
      /* add the devicedata to the portcontext */    
      tddmSingleThreadedEnter(dmRoot, DM_DEVICE_LOCK);
      DMLIST_ENQUEUE_AT_TAIL(&(oneDeviceData->MainLink), &(dmAllShared->MainDeviceList));
      tddmSingleThreadedLeave(dmRoot, DM_DEVICE_LOCK);
      DM_DBG3(("dmAddSASToSharedcontext: one case pid %d did %d \n", onePortContext->id, oneDeviceData->id));
      DM_DBG3(("dmAddSASToSharedcontext: new case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));
      }
  }
  else /* old device */
  {
    DM_DBG3(("dmAddSASToSharedcontext: old device\n"));
    DM_DBG3(("dmAddSASToSharedcontext: oneDeviceData %p did %d\n", oneDeviceData, oneDeviceData->id));
    DM_DBG3(("dmAddSASToSharedcontext: sasAddressHi 0x%08x sasAddressLo 0x%08x\n", 
    dmSASSubID->sasAddressHi, dmSASSubID->sasAddressLo));         

    oneDeviceData->dmRoot = dmRoot;
    /* saving sas address */
    oneDeviceData->SASAddressID.sasAddressLo = dmSASSubID->sasAddressLo;
    oneDeviceData->SASAddressID.sasAddressHi = dmSASSubID->sasAddressHi;
    oneDeviceData->initiator_ssp_stp_smp = dmSASSubID->initiator_ssp_stp_smp;
    oneDeviceData->target_ssp_stp_smp = dmSASSubID->target_ssp_stp_smp;
    oneDeviceData->dmPortContext = onePortContext;
    /* handles both SAS target and STP-target, SATA-device */
    if (!DEVICE_IS_SATA_DEVICE(oneDeviceData) && !DEVICE_IS_STP_TARGET(oneDeviceData))
    {
      oneDeviceData->DeviceType = DM_SAS_DEVICE;
    }
    else
    {
      oneDeviceData->DeviceType = DM_SATA_DEVICE;
    }
    
    if (oneExpDeviceData != agNULL)
    {
      oneDeviceData->ExpDevice = oneExpDeviceData;
    }      
    
    /* set phyID only when it has initial value of 0xFF */
    if (oneDeviceData->phyID == 0xFF)
    {
      oneDeviceData->phyID = phyID;
    }
    
    if (onePortContext->DiscoveryState == DM_DSTATE_NOT_STARTED)
    {
      DM_DBG3(("dmAddSASToSharedcontext: DM_DSTATE_NOT_STARTED\n"));
      DM_DBG3(("dmAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
      DM_DBG3(("dmAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
      oneDeviceData->valid = agTRUE;
    }
    else
    {
      if (onePortContext->discovery.type == DM_DISCOVERY_OPTION_INCREMENTAL_START)
      {
        DM_DBG3(("dmAddSASToSharedcontext: incremental discovery\n"));
        DM_DBG3(("dmAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        DM_DBG3(("dmAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        oneDeviceData->valid2 = agTRUE;
      }
      else
      {
        DM_DBG3(("dmAddSASToSharedcontext: full discovery\n"));
        DM_DBG3(("dmAddSASToSharedcontext: sasAddrHi 0x%08x \n", oneDeviceData->SASAddressID.sasAddressHi));
        DM_DBG3(("dmAddSASToSharedcontext: sasAddrLo 0x%08x \n", oneDeviceData->SASAddressID.sasAddressLo));
        oneDeviceData->valid = agTRUE;
      }
    }
    DM_DBG3(("dmAddSASToSharedcontext: old case pid %d did %d phyID %d\n", onePortContext->id, oneDeviceData->id, oneDeviceData->phyID));
     
  }
  return oneDeviceData;
}

/* no checking of valid and valid2 */
osGLOBAL dmDeviceData_t *
dmDeviceFind(
             dmRoot_t            *dmRoot,
             dmIntPortContext_t  *onePortContext,
             bit32               sasAddrHi,
             bit32               sasAddrLo
            )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDeviceData_t            *oneDeviceData = agNULL;
  dmList_t                  *DeviceListList;
  bit32                     found = agFALSE;
  
  DM_DBG3(("dmDeviceFind: start\n"));
  /* find a device's existence */
  DeviceListList = dmAllShared->MainDeviceList.flink;
  
  while (DeviceListList != &(dmAllShared->MainDeviceList))
  {
    oneDeviceData = DMLIST_OBJECT_BASE(dmDeviceData_t, MainLink, DeviceListList);
    if (oneDeviceData == agNULL)
    {
      DM_DBG1(("dmDeviceFind: oneDeviceData is NULL!!!\n"));
      return agNULL;
    }    
    if ((oneDeviceData->SASAddressID.sasAddressHi == sasAddrHi) &&
        (oneDeviceData->SASAddressID.sasAddressLo == sasAddrLo) &&
//        (oneDeviceData->valid == agTRUE) &&
        (oneDeviceData->dmPortContext == onePortContext)
        )
    {
      DM_DBG3(("dmDeviceFind: Found pid %d did %d\n", onePortContext->id, oneDeviceData->id));
      DM_DBG3(("dmDeviceFind: sasAddressHi 0x%08x\n", oneDeviceData->SASAddressID.sasAddressHi));         
      DM_DBG3(("dmDeviceFind: sasAddressLo 0x%08x\n", oneDeviceData->SASAddressID.sasAddressLo));
      found = agTRUE;
      break;
    }
    DeviceListList = DeviceListList->flink;
  }
  
  if (found == agFALSE)
  {
    DM_DBG3(("dmDeviceFind: end returning NULL\n"));
    return agNULL;
  }
  else
  {
    DM_DBG3(("dmDeviceFind: end returning NOT NULL\n"));
    return oneDeviceData;
  }
  
}	    


osGLOBAL void                          
dmBCTimer(
          dmRoot_t                 *dmRoot,
          dmIntPortContext_t       *onePortContext
         )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDiscovery_t     *discovery;
  
  DM_DBG3(("dmBCTimer: start\n"));
  
  discovery = &(onePortContext->discovery);
  
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->BCTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
                dmRoot,
                &discovery->BCTimer
               );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  if (onePortContext->valid == agTRUE)
  {
    dmSetTimerRequest(
                      dmRoot,
                      &discovery->BCTimer,
                      BC_TIMER_VALUE/dmAllShared->usecsPerTick,
                      dmBCTimerCB,
                      onePortContext,
                      agNULL,
                      agNULL
                      );
  
    dmAddTimer(
               dmRoot,
               &dmAllShared->timerlist, 
               &discovery->BCTimer
              );
  
  }
  
  
  return;
}	 


osGLOBAL void
dmBCTimerCB(
              dmRoot_t    * dmRoot, 
              void        * timerData1,
              void        * timerData2,
              void        * timerData3
              )
{
  dmIntPortContext_t        *onePortContext;
  dmDiscovery_t             *discovery;
  
  DM_DBG3(("dmBCTimerCB: start\n"));
  
  onePortContext = (dmIntPortContext_t *)timerData1;
  discovery = &(onePortContext->discovery);

  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->BCTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
               dmRoot,
               &discovery->BCTimer
               );
  }       
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  discovery->ResetTriggerred = agFALSE;
  
  if (onePortContext->valid == agTRUE)
  {
    dmDiscover(dmRoot,
               onePortContext->dmPortContext,
               DM_DISCOVERY_OPTION_INCREMENTAL_START
               );
  }  
  return;
}	      

/* discovery related SMP timers */
osGLOBAL void
dmDiscoverySMPTimer(dmRoot_t                 *dmRoot,
                    dmIntPortContext_t       *onePortContext,
                    bit32                    functionCode,
                    dmSMPRequestBody_t       *dmSMPRequestBody
                   )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDiscovery_t     *discovery;
  
  DM_DBG3(("dmDiscoverySMPTimer: start\n"));
  DM_DBG3(("dmDiscoverySMPTimer: pid %d SMPFn 0x%x\n", onePortContext->id, functionCode));
  
  /* start the SMP timer which works as SMP application timer */
  discovery = &(onePortContext->discovery);
  
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->DiscoverySMPTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
              dmRoot,
              &discovery->DiscoverySMPTimer
              );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  
  dmSetTimerRequest(
                    dmRoot,
                    &discovery->DiscoverySMPTimer,
                    SMP_TIMER_VALUE/dmAllShared->usecsPerTick,
                    dmDiscoverySMPTimerCB,
                    onePortContext,
                    dmSMPRequestBody,
                    agNULL
                   );
  
  dmAddTimer (
              dmRoot,
              &dmAllShared->timerlist, 
              &discovery->DiscoverySMPTimer
              );
  
  return;
}


osGLOBAL void
dmDiscoverySMPTimerCB(
                        dmRoot_t    * dmRoot, 
                        void        * timerData1,
                        void        * timerData2,
                        void        * timerData3
                       )
{
  agsaRoot_t                  *agRoot;
  dmIntPortContext_t          *onePortContext;
  bit8                        SMPFunction;  
#ifndef DIRECT_SMP
  dmSMPFrameHeader_t          *dmSMPFrameHeader;
  bit8                        smpHeader[4];
#endif  
  dmSMPRequestBody_t          *dmSMPRequestBody;
  dmDiscovery_t               *discovery;
  dmDeviceData_t              *oneDeviceData;
  agsaIORequest_t             *agAbortIORequest = agNULL;  
  agsaIORequest_t             *agToBeAbortIORequest = agNULL;  
  dmIntRoot_t                 *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t              *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmExpander_t                *oneExpander = agNULL;
  dmSMPRequestBody_t          *dmAbortSMPRequestBody = agNULL;
  dmList_t                    *SMPList;
  
  DM_DBG1(("dmDiscoverySMPTimerCB: start!!!\n"));
  
  onePortContext = (dmIntPortContext_t *)timerData1;
  dmSMPRequestBody = (dmSMPRequestBody_t *)timerData2;
  
  discovery = &(onePortContext->discovery);
  oneDeviceData = dmSMPRequestBody->dmDevice;
  agToBeAbortIORequest = &(dmSMPRequestBody->agIORequest);
  agRoot = dmAllShared->agRoot;
  
#ifdef DIRECT_SMP
  SMPFunction = dmSMPRequestBody->smpPayload[1];
#else
  saFrameReadBlock(agRoot, dmSMPRequestBody->IndirectSMP, 0, smpHeader, 4);
  dmSMPFrameHeader = (dmSMPFrameHeader_t *)smpHeader;
  SMPFunction = dmSMPFrameHeader->smpFunction;
#endif
  
  DM_DBG3(("dmDiscoverySMPTimerCB: SMP function 0x%x\n", SMPFunction));
  
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->DiscoverySMPTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
                  dmRoot,
                  &discovery->DiscoverySMPTimer
                 );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
//for debugging
//  saGetPendingPICI(agRoot); 		     
  
  switch (SMPFunction)
  {
  case SMP_REPORT_GENERAL: /* fall through */
  case SMP_DISCOVER:  /* fall through */
  case SMP_CONFIGURE_ROUTING_INFORMATION:  /* fall through */
    DM_DBG1(("dmDiscoverySMPTimerCB: failing discovery, SMP function 0x%x !!!\n", SMPFunction));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
    return; /* no more things to do */
  case SMP_REPORT_PHY_SATA:
    DM_DBG1(("dmDiscoverySMPTimerCB: failing discovery, SMP function SMP_REPORT_PHY_SATA !!!\n"));
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
    break;
  default:
    /* do nothing */
    DM_DBG1(("dmDiscoverySMPTimerCB: Error, not allowed case!!!\n"));
    break;
  }
  
  if (oneDeviceData->registered == agTRUE && (oneDeviceData->valid == agTRUE || oneDeviceData->valid2 == agTRUE) )
  {  
    /* call to saSMPAbort(one) */
    /* get an smp REQUEST from the free list */
    tddmSingleThreadedEnter(dmRoot, DM_SMP_LOCK);
    if (DMLIST_EMPTY(&(dmAllShared->freeSMPList)))
    {
      DM_DBG1(("dmDiscoverySMPTimerCB: no free SMP, can't abort SMP!!!\n"));
      tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
      return;
    }
    else
    {
      DMLIST_DEQUEUE_FROM_HEAD(&SMPList, &(dmAllShared->freeSMPList));
      tddmSingleThreadedLeave(dmRoot, DM_SMP_LOCK);
      dmAbortSMPRequestBody = DMLIST_OBJECT_BASE(dmSMPRequestBody_t, Link, SMPList);
      if (dmAbortSMPRequestBody == agNULL)
      {
        DM_DBG1(("dmDiscoverySMPTimerCB: dmAbortSMPRequestBody is NULL!!!\n"));
        return;
      }    
      DM_DBG5(("dmDiscoverySMPTimerCB: SMP id %d\n", dmAbortSMPRequestBody->id));
    }
    
    dmAbortSMPRequestBody->dmRoot = dmRoot;

    agAbortIORequest = &(dmAbortSMPRequestBody->agIORequest);
    agAbortIORequest->osData = (void *) dmAbortSMPRequestBody;
    agAbortIORequest->sdkData = agNULL; /* SALL takes care of this */
				     
    oneExpander = oneDeviceData->dmExpander;
								     				     
    DM_DBG1(("dmDiscoverySMPTimerCB: calling saSMPAbort!!!\n"));
    saSMPAbort(agRoot, 
               agAbortIORequest,
               0,
               oneExpander->agDevHandle,
               0, /* abort one */
               agToBeAbortIORequest,
               dmSMPAbortCB
              );
  }    
  return;
}
		       



osGLOBAL void                          
dmSMPBusyTimer(dmRoot_t             *dmRoot,
               dmIntPortContext_t   *onePortContext,
               dmDeviceData_t       *oneDeviceData,
               dmSMPRequestBody_t   *dmSMPRequestBody
              )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDiscovery_t     *discovery;
  
  DM_DBG3(("dmSMPBusyTimer: start\n"));
  DM_DBG3(("dmSMPBusyTimer: pid %d\n", onePortContext->id));
  
  discovery = &(onePortContext->discovery);
  
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->SMPBusyTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
              dmRoot,
              &discovery->SMPBusyTimer
              );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  dmSetTimerRequest(
                    dmRoot,
                    &discovery->SMPBusyTimer,
                    SMP_BUSY_TIMER_VALUE/dmAllShared->usecsPerTick,
                    dmSMPBusyTimerCB,
                    onePortContext,
                    oneDeviceData, 
                    dmSMPRequestBody
                    );
  
  dmAddTimer (
              dmRoot,
              &dmAllShared->timerlist, 
              &discovery->SMPBusyTimer
              );
  
  
  return;
}

osGLOBAL void
dmSMPBusyTimerCB(
                 dmRoot_t    * dmRoot, 
                 void        * timerData1,
                 void        * timerData2,
                 void        * timerData3
                )
{
  dmIntRoot_t                 *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t              *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  agsaRoot_t                  *agRoot;
  dmIntPortContext_t          *onePortContext;
  dmDeviceData_t              *oneDeviceData;
  dmSMPRequestBody_t          *dmSMPRequestBody;
  agsaSASRequestBody_t        *agSASRequestBody;
  agsaIORequest_t             *agIORequest;
  agsaDevHandle_t             *agDevHandle;
  dmDiscovery_t               *discovery;
  bit32                       status = AGSA_RC_FAILURE;
  dmExpander_t                *oneExpander = agNULL;
  
  
  DM_DBG3(("dmSMPBusyTimerCB: start\n"));
  
  onePortContext = (dmIntPortContext_t *)timerData1;
  oneDeviceData = (dmDeviceData_t *)timerData2;
  dmSMPRequestBody = (dmSMPRequestBody_t *)timerData3;
  agRoot = dmAllShared->agRoot;
  agIORequest = &(dmSMPRequestBody->agIORequest);
  oneExpander = oneDeviceData->dmExpander;
  agDevHandle = oneExpander->agDevHandle;
  agSASRequestBody = &(dmSMPRequestBody->agSASRequestBody);
  discovery = &(onePortContext->discovery);

  discovery->SMPRetries++;
  
  if (discovery->SMPRetries < SMP_BUSY_RETRIES)
  {    
    status = saSMPStart(
                         agRoot,
                         agIORequest,
                         0,             
                         agDevHandle,
                         AGSA_SMP_INIT_REQ,
                         agSASRequestBody,
                         &dmsaSMPCompleted
                         );
  }

  if (status == AGSA_RC_SUCCESS)
  {
    discovery->SMPRetries = 0;
    tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
    if (discovery->SMPBusyTimer.timerRunning == agTRUE)
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      dmKillTimer(
                    dmRoot,
                    &discovery->SMPBusyTimer
                   );
    }		     
    else
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    }
  }		       
  else if (status == AGSA_RC_FAILURE)
  {  
    tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
    if (discovery->SMPBusyTimer.timerRunning == agTRUE)
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      dmKillTimer(
                    dmRoot,
                    &discovery->SMPBusyTimer
                   );
    }		     
    else
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    }

    discovery->SMPRetries = 0;
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);
  }
  else /* AGSA_RC_BUSY */
  {
    if (discovery->SMPRetries >= SMP_BUSY_RETRIES)
    {
      /* done with retris; give up */
      DM_DBG3(("dmSMPBusyTimerCB: retries are over\n"));

      tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
      if (discovery->SMPBusyTimer.timerRunning == agTRUE)
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
        dmKillTimer(
                      dmRoot,
                      &discovery->SMPBusyTimer
                     );
      }		     
      else
      {
        tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      }

      discovery->SMPRetries = 0;
      dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

    }
    else
    {
      /* keep retrying */
      dmSMPBusyTimer(dmRoot, onePortContext, oneDeviceData, dmSMPRequestBody);
    }
  }
  
  return;
}  


/* expander configuring timer */
osGLOBAL void                          
dmDiscoveryConfiguringTimer(dmRoot_t                 *dmRoot,
                            dmIntPortContext_t       *onePortContext,
                            dmDeviceData_t           *oneDeviceData
                           )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDiscovery_t     *discovery;
  
  DM_DBG3(("dmDiscoveryConfiguringTimer: start\n"));
  DM_DBG3(("dmDiscoveryConfiguringTimer: pid %d\n", onePortContext->id));
  
  discovery = &(onePortContext->discovery);
 
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->discoveryTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
              dmRoot,
              &discovery->discoveryTimer
              );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  DM_DBG3(("dmDiscoveryConfiguringTimer: UsecsPerTick %d\n", dmAllShared->usecsPerTick));
  DM_DBG3(("dmDiscoveryConfiguringTimer: Timervalue %d\n", DISCOVERY_CONFIGURING_TIMER_VALUE/dmAllShared->usecsPerTick));
  
  dmSetTimerRequest(
                    dmRoot,
                    &discovery->discoveryTimer,
                    DISCOVERY_CONFIGURING_TIMER_VALUE/dmAllShared->usecsPerTick,
                    dmDiscoveryConfiguringTimerCB,
                    onePortContext, 
                    oneDeviceData,
                    agNULL
                   );
                   
  dmAddTimer (
              dmRoot,
              &dmAllShared->timerlist, 
              &discovery->discoveryTimer
              );
  
  
  return;
}

		
osGLOBAL void
dmDiscoveryConfiguringTimerCB(
                              dmRoot_t    * dmRoot, 
                              void        * timerData1,
                              void        * timerData2,
                              void        * timerData3
                             )
{
  dmIntPortContext_t     *onePortContext = agNULL;
  dmDiscovery_t          *discovery      = agNULL;
  dmDeviceData_t         *oneDeviceData  = agNULL;

  onePortContext = (dmIntPortContext_t *)timerData1;
  oneDeviceData  = (dmDeviceData_t *)timerData2;
  discovery = &(onePortContext->discovery);

  DM_DBG3(("dmDiscoveryConfiguringTimerCB: start\n"));  

  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->discoveryTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
               dmRoot,
               &discovery->discoveryTimer
               );
  }       
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  if (oneDeviceData->valid == agTRUE || oneDeviceData->valid2 == agTRUE)
  {
    dmReportGeneralSend(dmRoot, oneDeviceData);
  }
  return;
}
						
osGLOBAL void                          
dmConfigureRouteTimer(dmRoot_t                 *dmRoot,
                      dmIntPortContext_t       *onePortContext,
                      dmExpander_t             *oneExpander,
                      smpRespDiscover_t        *pdmSMPDiscoverResp,
                      smpRespDiscover2_t       *pdmSMPDiscover2Resp
                     )
{
  dmIntRoot_t       *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t    *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmDiscovery_t     *discovery;
  
  DM_DBG3(("dmConfigureRouteTimer: start\n"));
  
  DM_DBG3(("dmConfigureRouteTimer: pid %d\n", onePortContext->id));
  
  discovery = &(onePortContext->discovery);
 
  DM_DBG3(("dmConfigureRouteTimer: onePortContext %p oneExpander %p pdmSMPDiscoverResp %p\n", onePortContext, oneExpander, pdmSMPDiscoverResp));
  
  DM_DBG3(("dmConfigureRouteTimer: discovery %p \n", discovery));
  
  DM_DBG3(("dmConfigureRouteTimer:  pid %d configureRouteRetries %d\n", onePortContext->id, discovery->configureRouteRetries));
  
  DM_DBG3(("dmConfigureRouteTimer: discovery->status %d\n", discovery->status));
      
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->configureRouteTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
              dmRoot,
              &discovery->configureRouteTimer
              );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  DM_DBG3(("dmConfigureRouteTimer: UsecsPerTick %d\n", dmAllShared->usecsPerTick));
  DM_DBG3(("dmConfigureRouteTimer: Timervalue %d\n", CONFIGURE_ROUTE_TIMER_VALUE/dmAllShared->usecsPerTick));
  
  if (oneExpander->SAS2 == 0)
  {
    /* SAS 1.1 */
    dmSetTimerRequest(
                      dmRoot,
                      &discovery->configureRouteTimer,
                      CONFIGURE_ROUTE_TIMER_VALUE/dmAllShared->usecsPerTick,
                      dmConfigureRouteTimerCB,
                      (void *)onePortContext, 
                      (void *)oneExpander,
                      (void *)pdmSMPDiscoverResp
                     );
  }                   
  else
  { 
    /* SAS 2 */
    dmSetTimerRequest(
                      dmRoot,
                      &discovery->configureRouteTimer,
                      CONFIGURE_ROUTE_TIMER_VALUE/dmAllShared->usecsPerTick,
                      dmConfigureRouteTimerCB,
                      (void *)onePortContext, 
                      (void *)oneExpander,
                      (void *)pdmSMPDiscover2Resp
                     );
  }		     
  dmAddTimer (
              dmRoot,
              &dmAllShared->timerlist, 
              &discovery->configureRouteTimer
              );
   
  return;
}


osGLOBAL void
dmConfigureRouteTimerCB(
                        dmRoot_t    * dmRoot, 
                        void        * timerData1,
                        void        * timerData2,
                        void        * timerData3
                       )
{
  dmIntRoot_t         *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t      *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmIntPortContext_t  *onePortContext;
  dmExpander_t        *oneExpander;
  smpRespDiscover_t   *pdmSMPDiscoverResp = agNULL;
  smpRespDiscover2_t  *pdmSMPDiscover2Resp = agNULL;
  dmDiscovery_t       *discovery;
  
  
  DM_DBG3(("dmConfigureRouteTimerCB: start\n"));
  
  onePortContext = (dmIntPortContext_t *)timerData1;
  oneExpander = (dmExpander_t *)timerData2;
  if (oneExpander->SAS2 == 0)
  {
    pdmSMPDiscoverResp = (smpRespDiscover_t *)timerData3;
  }
  else
  {
    pdmSMPDiscover2Resp = (smpRespDiscover2_t *)timerData3;
  }
  discovery = &(onePortContext->discovery);
  
  DM_DBG3(("dmConfigureRouteTimerCB: onePortContext %p oneExpander %p pdmSMPDiscoverResp %p\n", onePortContext, oneExpander, pdmSMPDiscoverResp));
  
  DM_DBG3(("dmConfigureRouteTimerCB: discovery %p\n", discovery));

  DM_DBG3(("dmConfigureRouteTimerCB: pid %d configureRouteRetries %d\n", onePortContext->id, discovery->configureRouteRetries));
  
  DM_DBG3(("dmConfigureRouteTimerCB: discovery.status %d\n", discovery->status));
   
  discovery->configureRouteRetries++; 
  if (discovery->configureRouteRetries >= dmAllShared->MaxRetryDiscovery)
  {
    DM_DBG3(("dmConfigureRouteTimerCB: retries are over\n"));

    tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
    if (discovery->configureRouteTimer.timerRunning == agTRUE)
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
      dmKillTimer(
                  dmRoot,
                  &discovery->configureRouteTimer
                  );
    }
    else
    {
      tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    }

    discovery->configureRouteRetries = 0;
    /* failed the discovery */
    dmDiscoverDone(dmRoot, onePortContext, DM_RC_FAILURE);

    return;
  }

  
  if (oneExpander->SAS2 == 0)
  {
    if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      DM_DBG3(("dmConfigureRouteTimerCB: proceed by calling dmDownStreamDiscoverExpanderPhy\n"));
      dmhexdump("dmConfigureRouteTimerCB", (bit8*)pdmSMPDiscoverResp, sizeof(smpRespDiscover_t));
      discovery->configureRouteRetries = 0;

      dmDownStreamDiscoverExpanderPhy(dmRoot, onePortContext, oneExpander, pdmSMPDiscoverResp);  
    }
    else
    {
      DM_DBG3(("dmConfigureRouteTimerCB: setting timer again\n"));
      /* set the timer again */
      dmSetTimerRequest(
                        dmRoot,
                        &discovery->configureRouteTimer,
                        CONFIGURE_ROUTE_TIMER_VALUE/dmAllShared->usecsPerTick,
                        dmConfigureRouteTimerCB,
                        (void *)onePortContext, 
                        (void *)oneExpander,
                        (void *)pdmSMPDiscoverResp
                       );
                   
      dmAddTimer (
                  dmRoot,
                  &dmAllShared->timerlist, 
                  &discovery->configureRouteTimer
                  );
    }
  } /* SAS 1.1 */
  else
  {
    /* SAS 2 */
    if (onePortContext->discovery.status == DISCOVERY_DOWN_STREAM)
    {
      DM_DBG2(("dmConfigureRouteTimerCB: proceed by calling dmDownStreamDiscover2ExpanderPhy\n"));
      dmhexdump("dmConfigureRouteTimerCB", (bit8*)pdmSMPDiscover2Resp, sizeof(smpRespDiscover2_t));

      dmDownStreamDiscover2ExpanderPhy(dmRoot, onePortContext, oneExpander, pdmSMPDiscover2Resp);  
    }
    else
    {
      DM_DBG2(("dmConfigureRouteTimerCB: setting timer again\n"));
      /* set the timer again */
      dmSetTimerRequest(
                        dmRoot,
                        &discovery->configureRouteTimer,
                        CONFIGURE_ROUTE_TIMER_VALUE/dmAllShared->usecsPerTick,
                        dmConfigureRouteTimerCB,
                        (void *)onePortContext, 
                        (void *)oneExpander,
                        (void *)pdmSMPDiscover2Resp
                       );
                   
      dmAddTimer (
                  dmRoot,
                  &dmAllShared->timerlist, 
                  &discovery->configureRouteTimer
                 );
    }
  }
  
  return;
}		       
#endif /* FDS_ DM */

