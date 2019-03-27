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

#ifdef DM_DEBUG
bit32 gDMDebugLevel = 1;
#endif

osGLOBAL void	
dmGetRequirements(
                  dmRoot_t 	  		*dmRoot,
                  dmSwConfig_t			*swConfig,
                  dmMemoryRequirement_t		*memoryRequirement,
                  bit32 			*usecsPerTick,
                  bit32				*maxNumLocks)
{
  bit32               memoryReqCount = 0;
  bit32               max_expander = DM_MAX_EXPANDER_DEV; 
  char    *buffer;
  bit32   buffLen;
  bit32   lenRecv = 0;
  static char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char    *pLastUsedChar = agNULL;
  char    globalStr[]     = "Global";
  char    iniParmsStr[]   = "InitiatorParms";
  char    SwParmsStr[]    = "SWParms";    
   
  DM_DBG3(("dmGetRequirements: start\n"));
  /* sanity check */
  DM_ASSERT((agNULL != swConfig), "");
  DM_ASSERT((agNULL != memoryRequirement), "");
  DM_ASSERT((agNULL != usecsPerTick), "");
  DM_ASSERT((agNULL != maxNumLocks), ""); 
  
  /* memory requirement for dmRoot, CACHE memory */
  memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].singleElementLength = sizeof(dmIntRoot_t);
  memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].numElements = 1;
  memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].totalLength = 
      (memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].singleElementLength) * (memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].numElements);
  memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].alignment = 4;
  memoryRequirement->dmMemory[DM_ROOT_MEM_INDEX].type = DM_CACHED_MEM;
  memoryReqCount++;
  
  /* memory requirement for Port Context Links, CACHE memory */
  memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].singleElementLength = sizeof(dmIntPortContext_t);
  memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].numElements = DM_MAX_PORT_CONTEXT; 
  memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].totalLength = 
      (memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].singleElementLength) * (memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].numElements);
  memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].alignment = 4;
  memoryRequirement->dmMemory[DM_PORT_MEM_INDEX].type = DM_CACHED_MEM;
  memoryReqCount++;

  /* memory requirement for Device Links, CACHE memory */
  memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].singleElementLength = sizeof(dmDeviceData_t);
  memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].numElements = DM_MAX_DEV; 
  memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].totalLength = 
      (memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].singleElementLength) * (memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].numElements);
  memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].alignment = 4;
  memoryRequirement->dmMemory[DM_DEVICE_MEM_INDEX].type = DM_CACHED_MEM;
  memoryReqCount++;

  /* memory requirement for Expander Device Links, CACHE memory */
  /*
     Maximum number of expanders are configurable
     The default is DM_MAX_EXPANDER_DEV
  */
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);
  
  dm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((tddmGetTransportParam(
                             dmRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxExpanders",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == DM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      max_expander = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      max_expander = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  DM_DBG3(("dmGetRequirements: max_expander %d\n", max_expander));
  
  
  memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].singleElementLength = sizeof(dmExpander_t);
  memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].numElements = max_expander; 
  memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].totalLength = 
      (memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].singleElementLength) * (memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].numElements);
  memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].alignment = 4;
  memoryRequirement->dmMemory[DM_EXPANDER_MEM_INDEX].type = DM_CACHED_MEM;
  memoryReqCount++;

  /* memory requirement for SMP command Links, CACHE memory */
  memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].singleElementLength = sizeof(dmSMPRequestBody_t);
  memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].numElements = DM_MAX_SMP; 
  memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].totalLength = 
      (memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].singleElementLength) * (memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].numElements);
  memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].alignment = 4;
  memoryRequirement->dmMemory[DM_SMP_MEM_INDEX].type = DM_CACHED_MEM;
  memoryReqCount++;
  
  /* memory requirement for INDIRECT SMP command/response Links, DMA memory */
  memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].singleElementLength = SMP_INDIRECT_PAYLOAD; /* 512 */
  memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].numElements = DM_MAX_INDIRECT_SMP; 
  memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].totalLength = 
      (memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].singleElementLength) * (memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].numElements);
  memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].alignment = 4;
  memoryRequirement->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].type = DM_DMA_MEM;
  memoryReqCount++;
  
  
  /* set up memory requirement count */
  memoryRequirement->count = memoryReqCount;
  
  /* requirement for locks */
  *maxNumLocks = DM_MAX_LOCKS;   

  /* setup the time tick */  
  *usecsPerTick = DM_USECS_PER_TICK;


  /* set up the number of Expander device handles */
  swConfig->numDevHandles = DM_MAX_DEV;
  swConfig->itNexusTimeout = IT_NEXUS_TIMEOUT;   /* default is 2000 ms*/

  dm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((tddmGetTransportParam(
                             dmRoot, 
                             globalStr,
                             SwParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "IT_NEXUS_TIMEOUT",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == DM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      swConfig->itNexusTimeout = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      swConfig->itNexusTimeout = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  DM_DBG1(("dmGetRequirements: swConfig->itNexusTimeout 0x%X\n", swConfig->itNexusTimeout));
  
  DM_DBG3(("dmGetRequirements: memoryReqCount %d\n", memoryRequirement->count));

  return;
}	   				
/*
  ??? processing swConfig
*/
osGLOBAL bit32	
dmInitialize(
             dmRoot_t			*dmRoot,
             agsaRoot_t                 *agRoot,
             dmMemoryRequirement_t	*memoryAllocated,
             dmSwConfig_t		*swConfig,
             bit32			usecsPerTick )
{
  dmIntRoot_t               *dmIntRoot;
  dmIntPortContext_t        *dmIntPortContext;
  dmDeviceData_t            *dmDevice;  
  dmExpander_t              *dmExpander;
  dmSMPRequestBody_t        *dmSMPRequest; 
  bit8                      *dmIndirectSMPRequest; 
  dmIntContext_t            *dmAllShared;
  bit32              i;
  bit32               max_expander = DM_MAX_EXPANDER_DEV; 
  char    *buffer;
  bit32   buffLen;
  bit32   lenRecv = 0;
  static char    tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char    *pLastUsedChar = agNULL;
  char    globalStr[]     = "Global";
  char    iniParmsStr[]   = "InitiatorParms";
  char    SwParmsStr[]    = "SWParms"; 
  
  DM_DBG3(("dmInitialize: start\n"));
  /* sanity check */  
  DM_ASSERT((agNULL != dmRoot), "");
  DM_ASSERT((agNULL != agRoot), "");
  DM_ASSERT((agNULL != memoryAllocated), "");
  DM_ASSERT((agNULL != swConfig), "");
  DM_ASSERT((DM_ROOT_MEM_INDEX < memoryAllocated->count), "");
  DM_ASSERT((DM_PORT_MEM_INDEX < memoryAllocated->count), "");
  DM_ASSERT((DM_DEVICE_MEM_INDEX < memoryAllocated->count), "");
  DM_ASSERT((DM_EXPANDER_MEM_INDEX < memoryAllocated->count), "");
  DM_ASSERT((DM_SMP_MEM_INDEX < memoryAllocated->count), "");  
  DM_ASSERT((DM_INDIRECT_SMP_MEM_INDEX < memoryAllocated->count), "");  

  /* Check the memory allocated */
  for ( i = 0; i < memoryAllocated->count; i ++ )
  {
    /* If memory allocatation failed  */
    if (memoryAllocated->dmMemory[i].singleElementLength &&
        memoryAllocated->dmMemory[i].numElements)
    {
      if ( (0 != memoryAllocated->dmMemory[i].numElements)
          && (0 == memoryAllocated->dmMemory[i].totalLength) )
      {
        /* return failure */
        DM_DBG1(("dmInitialize: Memory[%d]  singleElementLength = 0x%0x  numElements = 0x%x NOT allocated!!!\n",
          i,
          memoryAllocated->dmMemory[i].singleElementLength,
          memoryAllocated->dmMemory[i].numElements));
        return DM_RC_FAILURE;
      }
    }
  }
  
  /* DM's internal root */
  dmIntRoot  = (dmIntRoot_t *) (memoryAllocated->dmMemory[DM_ROOT_MEM_INDEX].virtPtr);
  dmRoot->dmData = (void *) dmIntRoot;
  
  dmAllShared = (dmIntContext_t *)&(dmIntRoot->dmAllShared);
  /**<  Initialize the TDM data part of the interrupt context */
  dmAllShared->dmRootOsData.dmRoot     = dmRoot;
  dmAllShared->dmRootOsData.dmAllShared   = (void *) dmAllShared;
  
  /* Port Contexts */
  dmIntPortContext = (dmIntPortContext_t *) (memoryAllocated->dmMemory[DM_PORT_MEM_INDEX].virtPtr);
  dmAllShared->PortContextMem = (dmIntPortContext_t *)dmIntPortContext;
  
  /* Devices */
  dmDevice = (dmDeviceData_t *) (memoryAllocated->dmMemory[DM_DEVICE_MEM_INDEX].virtPtr);
  dmAllShared->DeviceMem = (dmDeviceData_t *)dmDevice;
  
  /* Expanders */
  dmExpander = (dmExpander_t *) (memoryAllocated->dmMemory[DM_EXPANDER_MEM_INDEX].virtPtr);
  dmAllShared->ExpanderMem = (dmExpander_t *)dmExpander;
  
  /* SMP commands */
  dmSMPRequest = (dmSMPRequestBody_t *) (memoryAllocated->dmMemory[DM_SMP_MEM_INDEX].virtPtr);
  dmAllShared->SMPMem = (dmSMPRequestBody_t *)dmSMPRequest;

  /* DMAable SMP request/reponse pointed by dmSMPRequestBody_t */
  dmIndirectSMPRequest = (bit8 *) (memoryAllocated->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].virtPtr);
  dmAllShared->IndirectSMPMem = (bit8 *)dmIndirectSMPRequest;
  dmAllShared->IndirectSMPUpper32 = memoryAllocated->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].physAddrUpper;
  dmAllShared->IndirectSMPLower32 = memoryAllocated->dmMemory[DM_INDIRECT_SMP_MEM_INDEX].physAddrLower;
    
  dmAllShared->agRoot = agRoot;
  
     
  dmAllShared->usecsPerTick = usecsPerTick;	   
  dmAllShared->itNexusTimeout = IT_NEXUS_TIMEOUT;/*swConfig->itNexusTimeout;*/
  dmAllShared->MaxRetryDiscovery = DISCOVERY_RETRIES;
  dmAllShared->RateAdjust = 0;
  /**< initializes timers */
  dmInitTimers(dmRoot);

  /**< initializes port contexts */
  dmPortContextInit(dmRoot);
  
  /**< initializes devices */
  dmDeviceDataInit(dmRoot);
  
  /**< initializes expander devices */
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);
  
  dm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((tddmGetTransportParam(
                             dmRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxExpanders",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == DM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      max_expander = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      max_expander = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }  

  dm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((tddmGetTransportParam(
                             dmRoot, 
                             globalStr,
                             SwParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "IT_NEXUS_TIMEOUT",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == DM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      dmAllShared->itNexusTimeout = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      dmAllShared->itNexusTimeout = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  DM_DBG1(("dmAllShared->itNexusTimeout %d \n", dmAllShared->itNexusTimeout)); 

  dm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  
  if ((tddmGetTransportParam(
                             dmRoot, 
                             globalStr,
                             SwParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxRetryDiscovery",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == DM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      dmAllShared->MaxRetryDiscovery = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      dmAllShared->MaxRetryDiscovery = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }

  DM_DBG1(("dmAllShared->MaxRetryDiscovery %d \n", dmAllShared->MaxRetryDiscovery)); 

  dm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  if ((tddmGetTransportParam(
                             dmRoot, 
                             globalStr,
                             SwParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "RateAdjust",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == DM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      dmAllShared->RateAdjust = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      dmAllShared->RateAdjust = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  DM_DBG1(("dmAllShared->RateAdjust %d \n", dmAllShared->RateAdjust)); 

  dmExpanderDeviceDataInit(dmRoot, max_expander);
    
  /**< initializes SMP commands */
  dmSMPInit(dmRoot);

#ifdef DM_DEBUG
  gDMDebugLevel = swConfig->DMDebugLevel;
#endif
  return DM_RC_SUCCESS;
}

osGLOBAL void
dmSMPInit(
          dmRoot_t *dmRoot 
         )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmSMPRequestBody_t        *dmSMPCommand = (dmSMPRequestBody_t *)dmAllShared->SMPMem;
  bit8                      *dmIndirectSMPReqRsp = (bit8 *)dmAllShared->IndirectSMPMem;
  bit32                     prev_PhysAddrLower;
  
  int i = 0;
  DM_DBG3(("dmSMPInit: start \n"));
  
  DMLIST_INIT_HDR(&(dmAllShared->freeSMPList));
  
  for(i=0;i<DM_MAX_SMP;i++)
  {
    DMLIST_INIT_ELEMENT(&(dmSMPCommand[i].Link));
    /* initialize expander fields */
    dmSMPCommand[i].dmRoot = agNULL;
    dmSMPCommand[i].dmDevice = agNULL;
    dmSMPCommand[i].dmPortContext = agNULL;
    dmSMPCommand[i].retries = 0;
    dmSMPCommand[i].id = i;
    dm_memset( &(dmSMPCommand[i].smpPayload), 0, sizeof(dmSMPCommand[i].smpPayload));
    /* indirect SMP related */
    dmSMPCommand[i].IndirectSMPResponse = agNULL;
    dmSMPCommand[i].IndirectSMP = ((bit8 *)dmIndirectSMPReqRsp) + (i*SMP_INDIRECT_PAYLOAD);
    dmSMPCommand[i].IndirectSMPUpper32 = dmAllShared->IndirectSMPUpper32;
    dmSMPCommand[i].IndirectSMPLower32 = dmAllShared->IndirectSMPLower32;
    
    prev_PhysAddrLower = dmAllShared->IndirectSMPLower32;
    dmAllShared->IndirectSMPLower32 = dmAllShared->IndirectSMPLower32 + SMP_INDIRECT_PAYLOAD;
    if (dmAllShared->IndirectSMPLower32 <= prev_PhysAddrLower)
    {
      dmAllShared->IndirectSMPUpper32++;    
    }
    
    DMLIST_ENQUEUE_AT_TAIL(&(dmSMPCommand[i].Link), &(dmAllShared->freeSMPList)); 
  }
  return;
  
}

osGLOBAL void
dmDeviceDataInit(
                 dmRoot_t *dmRoot 
                )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;  
  dmDeviceData_t            *dmDeviceData = (dmDeviceData_t *)dmAllShared->DeviceMem;
  int i;
  
  DM_DBG3(("dmDeviceDataInit: start \n"));
  
  DMLIST_INIT_HDR(&(dmAllShared->MainDeviceList));
  DMLIST_INIT_HDR(&(dmAllShared->FreeDeviceList));
  
  for(i=0;i<DM_MAX_DEV;i++)
  {
    DMLIST_INIT_ELEMENT(&(dmDeviceData[i].FreeLink));
    DMLIST_INIT_ELEMENT(&(dmDeviceData[i].MainLink));
    DMLIST_INIT_ELEMENT(&(dmDeviceData[i].IncDisLink));
    dmDeviceData[i].id = i;
    dmDeviceData[i].DeviceType = DM_DEFAULT_DEVICE;
    dmDeviceData[i].dmRoot = agNULL;
//    dmDeviceData[i].agDevHandle = agNULL;
    
    dmDeviceData[i].dmPortContext = agNULL;
    dmDeviceData[i].dmExpander = agNULL;
    dmDeviceData[i].ExpDevice = agNULL;
    dmDeviceData[i].phyID = 0xFF;
    dmDeviceData[i].SASAddressID.sasAddressHi = 0;
    dmDeviceData[i].SASAddressID.sasAddressLo = 0;
    dmDeviceData[i].valid = agFALSE;
    dmDeviceData[i].valid2 = agFALSE;
    dmDeviceData[i].processed = agFALSE;
    dmDeviceData[i].initiator_ssp_stp_smp = 0;
    dmDeviceData[i].target_ssp_stp_smp = 0;
    dmDeviceData[i].numOfPhys = 0;
//    dmDeviceData[i].registered = agFALSE;
    dmDeviceData[i].directlyAttached = agFALSE;
    dmDeviceData[i].SASSpecDeviceType = 0xFF;
    dmDeviceData[i].IOStart = 0;
    dmDeviceData[i].IOResponse = 0;
    dmDeviceData[i].agDeviceResetContext.osData = agNULL;
    dmDeviceData[i].agDeviceResetContext.sdkData = agNULL;
    dmDeviceData[i].TRflag = agFALSE;
    dmDeviceData[i].ResetCnt = 0;
    dmDeviceData[i].registered = agFALSE;
    dmDeviceData[i].reported = agFALSE;
  
    dmDeviceData[i].MCN = 0;
    dmDeviceData[i].MCNDone = agFALSE;
    dmDeviceData[i].PrevMCN = 0;
    
    dm_memset( &(dmDeviceData[i].dmDeviceInfo), 0, sizeof(dmDeviceInfo_t));
    /* some other variables */
    DMLIST_ENQUEUE_AT_TAIL(&(dmDeviceData[i].FreeLink), &(dmAllShared->FreeDeviceList)); 
  }  
  
  return;
}
osGLOBAL void
dmDeviceDataReInit(
                   dmRoot_t		  *dmRoot,
                   dmDeviceData_t         *oneDeviceData		     
                  )
{
  DM_DBG3(("dmDeviceDataReInit: start \n"));
  
  oneDeviceData->DeviceType = DM_DEFAULT_DEVICE;
//  oneDeviceData->agDevHandle = agNULL;
    
  oneDeviceData->dmPortContext = agNULL;
  oneDeviceData->dmExpander = agNULL;
  oneDeviceData->ExpDevice = agNULL;
  oneDeviceData->phyID = 0xFF;
  oneDeviceData->SASAddressID.sasAddressHi = 0;
  oneDeviceData->SASAddressID.sasAddressLo = 0;
  oneDeviceData->valid = agFALSE;
  oneDeviceData->valid2 = agFALSE;
  oneDeviceData->processed = agFALSE;
  oneDeviceData->initiator_ssp_stp_smp = 0;
  oneDeviceData->target_ssp_stp_smp = 0;
  oneDeviceData->numOfPhys = 0;
//  oneDeviceData->registered = agFALSE;
  oneDeviceData->directlyAttached = agFALSE;
  oneDeviceData->SASSpecDeviceType = 0xFF;
  oneDeviceData->IOStart = 0;
  oneDeviceData->IOResponse = 0;
  oneDeviceData->agDeviceResetContext.osData = agNULL;
  oneDeviceData->agDeviceResetContext.sdkData = agNULL;
  oneDeviceData->TRflag = agFALSE;
  oneDeviceData->ResetCnt = 0;   
  oneDeviceData->registered = agFALSE;
  oneDeviceData->reported = agFALSE;
  
  oneDeviceData->MCN = 0;
  oneDeviceData->MCNDone = agFALSE;
  oneDeviceData->PrevMCN = 0;
    
  dm_memset( &(oneDeviceData->dmDeviceInfo), 0, sizeof(dmDeviceInfo_t));
  
  return;
}


osGLOBAL void
dmExpanderDeviceDataInit(
                         dmRoot_t *dmRoot,
                         bit32    max_exp  
                        )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmExpander_t              *dmExpData = (dmExpander_t *)dmAllShared->ExpanderMem;
  bit32 i = 0;
  DM_DBG3(("dmExpanderDeviceDataInit: start \n"));
  
  DMLIST_INIT_HDR(&(dmAllShared->freeExpanderList));
  DMLIST_INIT_HDR(&(dmAllShared->mainExpanderList));
  
  for(i=0;i<max_exp;i++)
  {
    DMLIST_INIT_ELEMENT(&(dmExpData[i].linkNode));
    DMLIST_INIT_ELEMENT(&(dmExpData[i].upNode));
    /* initialize expander fields */
    dmExpData[i].dmRoot = agNULL;
    dmExpData[i].agDevHandle = agNULL;
    dmExpData[i].dmDevice = agNULL;
    dmExpData[i].dmUpStreamExpander = agNULL;
    dmExpData[i].dmCurrentDownStreamExpander = agNULL;
    dmExpData[i].hasUpStreamDevice = agFALSE;
    dmExpData[i].numOfUpStreamPhys = 0;
    dmExpData[i].currentUpStreamPhyIndex = 0;
    dmExpData[i].numOfDownStreamPhys = 0;
    dmExpData[i].currentDownStreamPhyIndex = 0;
    dmExpData[i].discoveringPhyId = 0;
    dmExpData[i].underDiscovering = agFALSE;
    dmExpData[i].id = i;
    DM_DBG3(("dmExpanderDeviceDataInit: exp id %d\n", i));
    
    dmExpData[i].dmReturnginExpander = agNULL;
    dmExpData[i].discoverSMPAllowed = agTRUE;
    dm_memset( &(dmExpData[i].currentIndex), 0, sizeof(dmExpData[i].currentIndex));
    dm_memset( &(dmExpData[i].upStreamPhys), 0, sizeof(dmExpData[i].upStreamPhys));
    dm_memset( &(dmExpData[i].downStreamPhys), 0, sizeof(dmExpData[i].downStreamPhys));
    dm_memset( &(dmExpData[i].routingAttribute), 0, sizeof(dmExpData[i].routingAttribute));
    dmExpData[i].configSASAddrTableIndex = 0;
    dm_memset( &(dmExpData[i].configSASAddressHiTable), 0, sizeof(dmExpData[i].configSASAddressHiTable));
    dm_memset( &(dmExpData[i].configSASAddressLoTable), 0, sizeof(dmExpData[i].configSASAddressLoTable));
    dmExpData[i].SAS2 = 0;  /* default is SAS 1.1 spec */ 
    dmExpData[i].TTTSupported = agFALSE;  /* Table to Table is supported */
    dmExpData[i].UndoDueToTTTSupported = agFALSE;
    
       
    DMLIST_ENQUEUE_AT_TAIL(&(dmExpData[i].linkNode), &(dmAllShared->freeExpanderList)); 
  }
  return;
}

/* re-intialize an expander */
osGLOBAL void
dmExpanderDeviceDataReInit(
                           dmRoot_t         *dmRoot, 
                           dmExpander_t     *oneExpander
                          )
{
  DM_DBG3(("dmExpanderDeviceDataReInit: start \n"));
  oneExpander->dmRoot = agNULL;
  oneExpander->agDevHandle = agNULL;
  oneExpander->dmDevice = agNULL;
  oneExpander->dmUpStreamExpander = agNULL;
  oneExpander->dmCurrentDownStreamExpander = agNULL;
  oneExpander->hasUpStreamDevice = agFALSE;
  oneExpander->numOfUpStreamPhys = 0;
  oneExpander->currentUpStreamPhyIndex = 0;
  oneExpander->numOfDownStreamPhys = 0;
  oneExpander->currentDownStreamPhyIndex = 0;
  oneExpander->discoveringPhyId = 0;
  oneExpander->underDiscovering = agFALSE;
  oneExpander->dmReturnginExpander = agNULL;
  oneExpander->discoverSMPAllowed = agTRUE;
  dm_memset( &(oneExpander->currentIndex), 0, sizeof(oneExpander->currentIndex));
  dm_memset( &(oneExpander->upStreamPhys), 0, sizeof(oneExpander->upStreamPhys));
  dm_memset( &(oneExpander->downStreamPhys), 0, sizeof(oneExpander->downStreamPhys));
  dm_memset( &(oneExpander->routingAttribute), 0, sizeof(oneExpander->routingAttribute));
  oneExpander->configSASAddrTableIndex = 0;
  dm_memset( &(oneExpander->configSASAddressHiTable), 0, sizeof(oneExpander->configSASAddressHiTable));
  dm_memset( &(oneExpander->configSASAddressLoTable), 0, sizeof(oneExpander->configSASAddressLoTable));
  oneExpander->SAS2 = 0;  /* default is SAS 1.1 spec */ 
  oneExpander->TTTSupported = agFALSE;  /* Table to Table is supported */
  oneExpander->UndoDueToTTTSupported = agFALSE;
  
  return;
}			  

osGLOBAL void
dmPortContextInit(
                  dmRoot_t *dmRoot 
                 )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  dmIntPortContext_t        *dmPortContext = (dmIntPortContext_t *)dmAllShared->PortContextMem;
  int i = 0;
#ifdef TBD  
  int j = 0;
#endif  
  
  DM_DBG3(("dmPortContextInit: start \n"));
  
  DMLIST_INIT_HDR(&(dmAllShared->MainPortContextList));
  DMLIST_INIT_HDR(&(dmAllShared->FreePortContextList));
  for(i=0;i<DM_MAX_PORT_CONTEXT;i++)
  {
    DMLIST_INIT_ELEMENT(&(dmPortContext[i].FreeLink));
    DMLIST_INIT_ELEMENT(&(dmPortContext[i].MainLink));

    DMLIST_INIT_HDR(&(dmPortContext[i].discovery.discoveringExpanderList));
    DMLIST_INIT_HDR(&(dmPortContext[i].discovery.UpdiscoveringExpanderList));
    dmPortContext[i].discovery.type = DM_DISCOVERY_OPTION_FULL_START;
    dmInitTimerRequest(dmRoot, &(dmPortContext[i].discovery.discoveryTimer));
    dmInitTimerRequest(dmRoot, &(dmPortContext[i].discovery.configureRouteTimer));
    dmInitTimerRequest(dmRoot, &(dmPortContext[i].discovery.deviceRegistrationTimer));
    dmInitTimerRequest(dmRoot, &(dmPortContext[i].discovery.SMPBusyTimer));
    dmInitTimerRequest(dmRoot, &(dmPortContext[i].discovery.BCTimer));
    dmInitTimerRequest(dmRoot, &(dmPortContext[i].discovery.DiscoverySMPTimer));
    dmPortContext[i].discovery.retries = 0;  
    dmPortContext[i].discovery.configureRouteRetries = 0;  
    dmPortContext[i].discovery.deviceRetistrationRetries = 0;  
    dmPortContext[i].discovery.pendingSMP = 0;  
    dmPortContext[i].discovery.SeenBC = agFALSE;  
    dmPortContext[i].discovery.forcedOK = agFALSE;  
    dmPortContext[i].discovery.SMPRetries = 0;  
    dmPortContext[i].discovery.DeferredError = agFALSE;  
    dmPortContext[i].discovery.ConfiguresOthers = agFALSE;  
    dmPortContext[i].discovery.ResetTriggerred = agFALSE;  

#ifdef INITIATOR_DRIVER  
    dmPortContext[i].DiscoveryState = DM_DSTATE_NOT_STARTED;
    dmPortContext[i].DiscoveryAbortInProgress = agFALSE;
    dmPortContext[i].directAttatchedSAS = agFALSE;
    dmPortContext[i].DiscoveryRdyGiven = agFALSE;
    dmPortContext[i].SeenLinkUp = agFALSE;
    
#endif      
    dmPortContext[i].id = i;
#ifdef TBD    
    dmPortContext[i].agPortContext = agNULL;
#endif    
    dmPortContext[i].LinkRate = 0;
    dmPortContext[i].Count = 0;
    dmPortContext[i].valid = agFALSE;
    dmPortContext[i].RegFailed = agFALSE;
    
#ifdef TBD    
    for (j=0;j<DM_MAX_NUM_PHYS;j++)
    {
      dmPortContext[i].PhyIDList[j] = agFALSE;
    }
#endif    
    dmPortContext[i].RegisteredDevNums = 0;
    dmPortContext[i].eventPhyID = 0xFF;
    dmPortContext[i].Transient = agFALSE;

    /* add more variables later */
    DMLIST_ENQUEUE_AT_TAIL(&(dmPortContext[i].FreeLink), &(dmAllShared->FreePortContextList));
  }

#ifdef DM_INTERNAL_DEBUG  /* for debugging only */
  for(i=0;i<DM_MAX_PORT_CONTEXT;i++)
  {
    DM_DBG6(("dmPortContextInit: index %d  &tdsaPortContext[] %p\n", i, &(dmPortContext[i])));
  }
  DM_DBG6(("dmPortContextInit: sizeof(tdsaPortContext_t) %d 0x%x\n", sizeof(dmIntPortContext_t), sizeof(dmIntPortContext_t)));
#endif

  return;
}		 

osGLOBAL void
dmPortContextReInit(
                    dmRoot_t		  *dmRoot,
                    dmIntPortContext_t    *onePortContext		     
                    )
{
  dmDiscovery_t   *discovery;
  
  DM_DBG3(("dmPortContextReInit: start \n"));
  
  discovery = &(onePortContext->discovery);

  onePortContext->discovery.type = DM_DISCOVERY_OPTION_FULL_START;
  onePortContext->discovery.retries = 0;  
  onePortContext->discovery.configureRouteRetries = 0;  
  onePortContext->discovery.deviceRetistrationRetries = 0;  
  onePortContext->discovery.pendingSMP = 0;  
  onePortContext->discovery.SeenBC = agFALSE;  
  onePortContext->discovery.forcedOK = agFALSE;  
  onePortContext->discovery.SMPRetries = 0;  
  onePortContext->discovery.DeferredError = agFALSE;
  onePortContext->discovery.ConfiguresOthers = agFALSE;
  onePortContext->discovery.ResetTriggerred = agFALSE;
  
  /* free expander lists */
  dmCleanAllExp(dmRoot, onePortContext);
    
  /* kill the discovery-related timers if they are running */  
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
  
  
  tddmSingleThreadedEnter(dmRoot, DM_TIMER_LOCK);
  if (discovery->deviceRegistrationTimer.timerRunning == agTRUE)
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
    dmKillTimer(
                dmRoot,
                &discovery->deviceRegistrationTimer
               );
  }
  else
  {
    tddmSingleThreadedLeave(dmRoot, DM_TIMER_LOCK);
  }
  
  
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

  onePortContext->DiscoveryState = DM_DSTATE_NOT_STARTED;
  onePortContext->DiscoveryAbortInProgress = agFALSE;
  onePortContext->directAttatchedSAS = agFALSE;
  onePortContext->DiscoveryRdyGiven = agFALSE;
  onePortContext->SeenLinkUp = agFALSE;
  
  onePortContext->dmPortContext->dmData = agNULL;
  onePortContext->dmPortContext = agNULL;
  onePortContext->dmRoot = agNULL;
  
  onePortContext->LinkRate = 0;
  onePortContext->Count = 0;
  onePortContext->valid = agFALSE;
  onePortContext->RegisteredDevNums = 0;
  onePortContext->eventPhyID = 0xFF;
  onePortContext->Transient = agFALSE;
    
  return;
}		    


osGLOBAL void
dmInitTimers(
               dmRoot_t *dmRoot 
               )
{
  dmIntRoot_t               *dmIntRoot    = (dmIntRoot_t *)dmRoot->dmData;
  dmIntContext_t            *dmAllShared = (dmIntContext_t *)&dmIntRoot->dmAllShared;
  
#ifdef DM_DEBUG_ENABLE
  dmIntPortContext_t *dmPortContext = (dmIntPortContext_t *)dmAllShared->PortContextMem;
  
  DM_DBG6(("dmInitTimers: start \n"));
  DM_DBG6(("dmInitTimers: ******* tdsaRoot %p \n", dmIntRoot));
  DM_DBG6(("dmInitTimers: ******* tdsaPortContext %p \n",dmPortContext));
#endif
  
  /* initialize the timerlist */
  DMLIST_INIT_HDR(&(dmAllShared->timerlist));

  return;
}
#endif /* FDS_ DM */


