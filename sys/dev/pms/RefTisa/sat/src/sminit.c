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

#include <dev/pms/RefTisa/tisa/api/titypes.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>

#include <dev/pms/RefTisa/sat/src/smdefs.h>
#include <dev/pms/RefTisa/sat/src/smproto.h>
#include <dev/pms/RefTisa/sat/src/smtypes.h>

#ifdef SM_DEBUG
bit32 gSMDebugLevel = 1;
#endif
smRoot_t *gsmRoot = agNULL;

/* start smapi defined APIS */
osGLOBAL void
smGetRequirements(
                  smRoot_t 	  		*smRoot,
                  smSwConfig_t			*swConfig,
                  smMemoryRequirement_t		*memoryRequirement,
                  bit32                         *usecsPerTick,
                  bit32				*maxNumLocks
                 )
{
  bit32               memoryReqCount = 0;
  bit32               i; 
  bit32               max_dev = SM_MAX_DEV; 
  char                *buffer;
  bit32               buffLen;
  bit32               lenRecv = 0;
  static char         tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char                *pLastUsedChar = agNULL;
  char                globalStr[]     = "Global";
  char                iniParmsStr[]   = "InitiatorParms";
  SM_DBG2(("smGetRequirements: start\n"));  
  
  /* sanity check */
  SM_ASSERT((agNULL != swConfig), "");
  SM_ASSERT((agNULL != memoryRequirement), "");
  SM_ASSERT((agNULL != usecsPerTick), "");
  SM_ASSERT((agNULL != maxNumLocks), ""); 
  
  /* memory requirement for smRoot, CACHE memory */
  memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].singleElementLength = sizeof(smIntRoot_t);
  memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].numElements = 1;
  memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].totalLength = 
      (memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].singleElementLength) * (memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].numElements);
  memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].alignment = 4;
  memoryRequirement->smMemory[SM_ROOT_MEM_INDEX].type = SM_CACHED_MEM;
  memoryReqCount++;
  
  /* reading the configurable parameter of MaxTargets */
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);
  sm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  if ((tdsmGetTransportParam(
                             smRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxTargets",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == SM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      max_dev = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      max_dev = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
  }
  SM_DBG3(("smGetRequirements: max_expander %d\n", max_dev));
  /* memory requirement for Device Links, CACHE memory */
  memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].singleElementLength = sizeof(smDeviceData_t);
  memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].numElements = max_dev; 
  memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].totalLength = 
      (memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].singleElementLength) * (memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].numElements);
  memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].alignment = 4;
  memoryRequirement->smMemory[SM_DEVICE_MEM_INDEX].type = SM_CACHED_MEM;
  memoryReqCount++;
  
  /* memory requirement for IO inks, CACHE memory */
  memoryRequirement->smMemory[SM_IO_MEM_INDEX].singleElementLength = sizeof(smIORequestBody_t);
  memoryRequirement->smMemory[SM_IO_MEM_INDEX].numElements = SM_MAX_IO; 
  memoryRequirement->smMemory[SM_IO_MEM_INDEX].totalLength = 
      (memoryRequirement->smMemory[SM_IO_MEM_INDEX].singleElementLength) * (memoryRequirement->smMemory[SM_IO_MEM_INDEX].numElements);
  memoryRequirement->smMemory[SM_IO_MEM_INDEX].alignment = 4;
  memoryRequirement->smMemory[SM_IO_MEM_INDEX].type = SM_CACHED_MEM;
  memoryReqCount++;
  
  /* for debugging */
  for (i=0;i< memoryReqCount;i++)
  {
    SM_DBG3(("smGetRequirements: index %d numElements %d totalLength %d singleElementLength %d alignment %d\n", i
    , memoryRequirement->smMemory[i].numElements, memoryRequirement->smMemory[i].totalLength, 
    memoryRequirement->smMemory[i].singleElementLength,memoryRequirement->smMemory[i].alignment ));
  }
  /* set up memory requirement count */
  memoryRequirement->count = memoryReqCount;
  
  /* requirement for locks */
  *maxNumLocks = SM_MAX_LOCKS;   

  /* setup the time tick */  
  *usecsPerTick = SM_USECS_PER_TICK;

  /* set up the number of active IOs */
  swConfig->maxActiveIOs = SM_MAX_IO;
  
  /* set up the number of device handles */
  swConfig->numDevHandles = SM_MAX_DEV;
  
  
  return;
}		

osGLOBAL bit32
smInitialize(
             smRoot_t				*smRoot,
             agsaRoot_t                         *agRoot,
             smMemoryRequirement_t		*memoryAllocated,
             smSwConfig_t			*swConfig,
             bit32				usecsPerTick 
            )
{
  smIntRoot_t               *smIntRoot;
  smDeviceData_t            *smDevice;  
  smIORequestBody_t         *smIORequest; 
  smIntContext_t            *smAllShared;
  bit32                     i;
  bit32                     max_dev = SM_MAX_DEV; 
  char                      *buffer;
  bit32                     buffLen;
  bit32                     lenRecv = 0;
  static char               tmpBuffer[DEFAULT_KEY_BUFFER_SIZE];
  char                      *pLastUsedChar = agNULL;
  char                      globalStr[]     = "Global";
  char                      iniParmsStr[]   = "InitiatorParms";
  
  SM_DBG2(("smInitialize: start\n"));  
  
  /* sanity check */  
  SM_ASSERT((agNULL != smRoot), "");
  SM_ASSERT((agNULL != agRoot), "");
  SM_ASSERT((agNULL != memoryAllocated), "");
  SM_ASSERT((agNULL != swConfig), "");
  SM_ASSERT((SM_ROOT_MEM_INDEX < memoryAllocated->count), "");
  SM_ASSERT((SM_DEVICE_MEM_INDEX < memoryAllocated->count), "");
  SM_ASSERT((SM_IO_MEM_INDEX < memoryAllocated->count), "");
  
  /* Check the memory allocated */
  for ( i = 0; i < memoryAllocated->count; i ++ )
  {
    /* If memory allocatation failed  */
    if (memoryAllocated->smMemory[i].singleElementLength &&
        memoryAllocated->smMemory[i].numElements)
    {
      if ( (0 != memoryAllocated->smMemory[i].numElements)
          && (0 == memoryAllocated->smMemory[i].totalLength) )
      {
        /* return failure */
        SM_DBG1(("smInitialize: Memory[%d]  singleElementLength = 0x%x  numElements = 0x%x NOT allocated!!!\n",
          i,
          memoryAllocated->smMemory[i].singleElementLength,
          memoryAllocated->smMemory[i].numElements));
        return SM_RC_FAILURE;
      }
    }
  }
  
  /* for debugging */
  for ( i = 0; i < memoryAllocated->count; i ++ )
  {
    SM_DBG3(("smInitialize: index %d virtPtr %p osHandle%p\n",i, memoryAllocated->smMemory[i].virtPtr, memoryAllocated->smMemory[i].osHandle)); 
    SM_DBG3(("smInitialize: index %d phyAddrUpper 0x%x phyAddrLower 0x%x totalLength %d numElements %d\n", i, 
    memoryAllocated->smMemory[i].physAddrUpper, 
    memoryAllocated->smMemory[i].physAddrLower, 
    memoryAllocated->smMemory[i].totalLength, 
    memoryAllocated->smMemory[i].numElements));
    SM_DBG3(("smInitialize: index %d singleElementLength 0x%x alignment 0x%x type %d reserved %d\n", i, 
    memoryAllocated->smMemory[i].singleElementLength, 
    memoryAllocated->smMemory[i].alignment, 
    memoryAllocated->smMemory[i].type, 
    memoryAllocated->smMemory[i].reserved));
  }  
  
  /* SM's internal root */
  smIntRoot  = (smIntRoot_t *) (memoryAllocated->smMemory[SM_ROOT_MEM_INDEX].virtPtr);
  smRoot->smData = (void *) smIntRoot;
  
  smAllShared = (smIntContext_t *)&(smIntRoot->smAllShared);
  /**<  Initialize the TDM data part of the interrupt context */
  smAllShared->smRootOsData.smRoot     = smRoot;
  smAllShared->smRootOsData.smAllShared   = (void *) smAllShared;
  gsmRoot = smRoot; 
  smAllShared->FCA = agTRUE;
  
  /* Devices */
  smDevice = (smDeviceData_t *) (memoryAllocated->smMemory[SM_DEVICE_MEM_INDEX].virtPtr);
  smAllShared->DeviceMem = (smDeviceData_t *)smDevice;
  
  /* IOs */
  smIORequest = (smIORequestBody_t *) (memoryAllocated->smMemory[SM_IO_MEM_INDEX].virtPtr);
  smAllShared->IOMem = (smIORequestBody_t *)smIORequest;
  
  smAllShared->agRoot = agRoot;
  
  smAllShared->usecsPerTick = usecsPerTick;	   
  
  /**< initializes timers */
  smInitTimers(smRoot);
  
  /**< initializes devices */
  buffer = tmpBuffer;
  buffLen = sizeof(tmpBuffer);
  sm_memset(buffer, 0, buffLen);
  lenRecv = 0;
  if ((tdsmGetTransportParam(
                             smRoot, 
                             globalStr,
                             iniParmsStr,
                             agNULL,
                             agNULL,
                             agNULL, 
                             agNULL, 
                             "MaxTargets",
                             buffer, 
                             buffLen, 
                             &lenRecv
                             ) == SM_RC_SUCCESS) && (lenRecv != 0))
  {
    if (osti_strncmp(buffer, "0x", 2) == 0)
    { 
      max_dev = osti_strtoul (buffer, &pLastUsedChar, 0);
    }
    else
    {
      max_dev = osti_strtoul (buffer, &pLastUsedChar, 10);
    }
   SM_DBG1(("smInitialize: MaxTargets %d\n", max_dev));
 }  

  smDeviceDataInit(smRoot, max_dev);
  
  /**< initializes IOs */
  smIOInit(smRoot);

#ifdef SM_DEBUG
  gSMDebugLevel = swConfig->SMDebugLevel;
#endif    
  
  return SM_RC_SUCCESS;
}		

osGLOBAL void
smInitTimers(
             smRoot_t *smRoot 
            )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;
  
  SM_DBG2(("smInitTimers: start\n"));  
  
  /* initialize the timerlist */
  SMLIST_INIT_HDR(&(smAllShared->timerlist));

  return;
}

osGLOBAL void
smDeviceDataReInit(
                   smRoot_t		  *smRoot,
                   smDeviceData_t         *oneDeviceData		     
                  )
{
  int               j=0;
  smSatInternalIo_t   *satIntIO;
  
  SM_DBG2(("smDeviceDataReInit: start \n"));
  
  if (oneDeviceData->satPendingIO != 0)
  {
    SM_DBG1(("smDeviceDataReInit: did %d\n", oneDeviceData->id));
    SM_DBG1(("smDeviceDataReInit: satPendingIO %d satNCQMaxIO %d!!!\n", oneDeviceData->satPendingIO, oneDeviceData->satNCQMaxIO ));
    SM_DBG1(("smDeviceDataReInit: satPendingNCQIO %d satPendingNONNCQIO %d!!!\n", oneDeviceData->satPendingNCQIO, oneDeviceData->satPendingNONNCQIO));
  }

//  oneDeviceData->smRoot = agNULL;
  oneDeviceData->agDevHandle = agNULL;
  oneDeviceData->valid = agFALSE;
  oneDeviceData->SMAbortAll = agFALSE;
  oneDeviceData->smDevHandle = agNULL;
  oneDeviceData->directlyAttached = agFALSE;
  oneDeviceData->agExpDevHandle = agNULL;
  oneDeviceData->phyID = 0xFF;
  oneDeviceData->SMNumOfFCA = 0;
  
  /* default */
  oneDeviceData->satDriveState = SAT_DEV_STATE_NORMAL;
  oneDeviceData->satNCQMaxIO =SAT_NCQ_MAX;
  oneDeviceData->satPendingIO = 0;
  oneDeviceData->satPendingNCQIO = 0;
  oneDeviceData->satPendingNONNCQIO = 0;
  oneDeviceData->IDDeviceValid = agFALSE;
  oneDeviceData->freeSATAFDMATagBitmap = 0;
  oneDeviceData->NumOfFCA = 0;
  oneDeviceData->NumOfIDRetries = 0;
  oneDeviceData->ID_Retries = 0;
  oneDeviceData->OSAbortAll = agFALSE;
    
  sm_memset(oneDeviceData->satMaxLBA, 0, sizeof(oneDeviceData->satMaxLBA));
  sm_memset(&(oneDeviceData->satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));

  oneDeviceData->satSaDeviceData = oneDeviceData;
    
  satIntIO = (smSatInternalIo_t *)&(oneDeviceData->satIntIo[0]);
  for (j = 0; j < SAT_MAX_INT_IO; j++)
  {
    SM_DBG2(("tdsaDeviceDataReInit: in loop of internal io free, id %d\n", satIntIO->id));
    smsatFreeIntIoResource(smRoot, oneDeviceData, satIntIO);    
    satIntIO = satIntIO + 1;    
  }
  
  return;
}	    
osGLOBAL void
smDeviceDataInit(
                 smRoot_t *smRoot,
                 bit32    max_dev		  
                )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;  
  smDeviceData_t            *smDeviceData = (smDeviceData_t *)smAllShared->DeviceMem;
  int                       i,j;
  smSatInternalIo_t           *satIntIO;
  
  SM_DBG2(("smDeviceDataInit: start \n"));
  
  SMLIST_INIT_HDR(&(smAllShared->MainDeviceList));
  SMLIST_INIT_HDR(&(smAllShared->FreeDeviceList));
  
  for(i=0;i<(int)max_dev;i++)
  {
    SMLIST_INIT_ELEMENT(&(smDeviceData[i].FreeLink));
    SMLIST_INIT_ELEMENT(&(smDeviceData[i].MainLink));
    smDeviceData[i].id = i;
    smDeviceData[i].smRoot = agNULL;
    smDeviceData[i].agDevHandle = agNULL;
    smDeviceData[i].valid = agFALSE;
    smDeviceData[i].SMAbortAll = agFALSE;
    smDeviceData[i].smDevHandle = agNULL;
    smDeviceData[i].directlyAttached = agFALSE;
    smDeviceData[i].agExpDevHandle = agNULL;
    smDeviceData[i].phyID = 0xFF;
    smDeviceData[i].SMNumOfFCA = 0;
 
    
    SMLIST_INIT_HDR(&(smDeviceData[i].satIoLinkList));
    SMLIST_INIT_HDR(&(smDeviceData[i].satFreeIntIoLinkList));
    SMLIST_INIT_HDR(&(smDeviceData[i].satActiveIntIoLinkList));
    
    /* default */
    smDeviceData[i].satDriveState = SAT_DEV_STATE_NORMAL;
    smDeviceData[i].satNCQMaxIO =SAT_NCQ_MAX;
    smDeviceData[i].satPendingIO = 0;
    smDeviceData[i].satPendingNCQIO = 0;
    smDeviceData[i].satPendingNONNCQIO = 0;
    smDeviceData[i].IDDeviceValid = agFALSE;
    smDeviceData[i].freeSATAFDMATagBitmap = 0;
    smDeviceData[i].NumOfFCA = 0;
    smDeviceData[i].NumOfIDRetries = 0;
    smDeviceData[i].ID_Retries = 0;
    smDeviceData[i].OSAbortAll = agFALSE;
    smInitTimerRequest(smRoot, &(smDeviceData[i].SATAIDDeviceTimer));
   
    sm_memset(&(smDeviceData[i].satIdentifyData), 0xFF, sizeof(agsaSATAIdentifyData_t));
    sm_memset(smDeviceData[i].satMaxLBA, 0, sizeof(smDeviceData[i].satMaxLBA));

    smDeviceData[i].satSaDeviceData = &smDeviceData[i];
    
#if 1    
    satIntIO = &smDeviceData[i].satIntIo[0];
    for (j = 0; j < SAT_MAX_INT_IO; j++)
    {
      SMLIST_INIT_ELEMENT (&satIntIO->satIntIoLink);
      SMLIST_ENQUEUE_AT_TAIL (&satIntIO->satIntIoLink, 
                              &smDeviceData[i].satFreeIntIoLinkList);
      satIntIO->satOrgSmIORequest = agNULL;
      satIntIO->id = j;
      satIntIO = satIntIO + 1;
    }
#endif
    
    /* some other variables */
    SMLIST_ENQUEUE_AT_TAIL(&(smDeviceData[i].FreeLink), &(smAllShared->FreeDeviceList)); 
  }  
  
  return;
}

osGLOBAL void
smIOInit(
         smRoot_t *smRoot 
        )
{
  smIntRoot_t               *smIntRoot    = (smIntRoot_t *)smRoot->smData;
  smIntContext_t            *smAllShared = (smIntContext_t *)&smIntRoot->smAllShared;  
  smIORequestBody_t         *smIOCommand = (smIORequestBody_t *)smAllShared->IOMem;
  int                       i = 0;
  
  SM_DBG3(("smIOInit: start\n"));  
  
  SMLIST_INIT_HDR(&(smAllShared->freeIOList));
  SMLIST_INIT_HDR(&(smAllShared->mainIOList));
  
  for(i=0;i<SM_MAX_IO;i++)
  {
    SMLIST_INIT_ELEMENT(&(smIOCommand[i].satIoBodyLink));
    smIOCommand[i].id = i;
    smIOCommand[i].InUse = agFALSE;
    smIOCommand[i].ioStarted = agFALSE;
    smIOCommand[i].ioCompleted = agFALSE;
    smIOCommand[i].reTries = 0;

    smIOCommand[i].smDevHandle = agNULL;
    smIOCommand[i].smIORequest = agNULL;
    smIOCommand[i].smIOToBeAbortedRequest = agNULL;
    smIOCommand[i].transport.SATA.satIOContext.satOrgIOContext = agNULL;
        
    sm_memset(&(smIOCommand[i].transport.SATA.agSATARequestBody), 0, sizeof(agsaSATAInitiatorRequest_t));   
    
    
    SMLIST_ENQUEUE_AT_TAIL(&(smIOCommand[i].satIoBodyLink), &(smAllShared->freeIOList)); 
  }
  
  return;
}
	    	    
FORCEINLINE void
smIOReInit(
          smRoot_t          *smRoot,
          smIORequestBody_t *smIORequestBody
          )
{
  SM_DBG3(("smIOReInit: start\n"));  
  smIORequestBody->InUse = agTRUE;
  smIORequestBody->ioStarted = agFALSE;
  smIORequestBody->ioCompleted = agFALSE;
  smIORequestBody->reTries = 0;
  smIORequestBody->smDevHandle = agNULL;
  smIORequestBody->smIORequest = agNULL;
  smIORequestBody->smIOToBeAbortedRequest = agNULL;
  smIORequestBody->transport.SATA.satIOContext.satOrgIOContext = agNULL;
  /*sm_memset(&(smIORequestBody->transport.SATA.agSATARequestBody), 0, sizeof(agsaSATAInitiatorRequest_t));*/
  return;
}

/* end smapi defined APIS */

