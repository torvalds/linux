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
/*****************************************************************************/
/** \file
 *
 * This file contains initiator CB functions
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <dev/pms/config.h>

#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

#include <dev/pms/RefTisa/sallsdk/api/sa.h>
#include <dev/pms/RefTisa/sallsdk/api/saapi.h>
#include <dev/pms/RefTisa/sallsdk/api/saosapi.h>

#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>

#ifdef FDS_SM
#include <dev/pms/RefTisa/sat/api/sm.h>
#include <dev/pms/RefTisa/sat/api/smapi.h>
#include <dev/pms/RefTisa/sat/api/tdsmapi.h>
#endif

#ifdef FDS_DM
#include <dev/pms/RefTisa/discovery/api/dm.h>
#include <dev/pms/RefTisa/discovery/api/dmapi.h>
#include <dev/pms/RefTisa/discovery/api/tddmapi.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/sas/common/tdtypes.h>
#include <dev/pms/freebsd/driver/common/osstring.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdutil.h>

#ifdef INITIATOR_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdtypes.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itddefs.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/ini/itdglobl.h>
#endif

#ifdef TARGET_DRIVER
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdglobl.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdxchg.h>
#include <dev/pms/RefTisa/tisa/sassata/sas/tgt/ttdtypes.h>
#endif

#include <dev/pms/RefTisa/tisa/sassata/common/tdsatypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdproto.h>

/*****************************************************************************
*!  \brief  itdssTaskCompleted
*
*  Purpose: This routine is called to complete an task management request
*           previously issued to the LL Layer. All task management completes with
*           this function except query task management.
*
*   \param  agRoot:         Pointer to driver Instance.
*   \param  agIORequest:    Pointer to the I/O Request data structure for
*                           this I/O.
*   \param  agIOStatus:     Status of I/O just completed.
*   \param  agIOInfoLen:    Length of the I/O information associated with this
*                           I/O request
*   \param   agParam        A Handle used to refer to the response frame or handle
*                           of abort request
*   \param  agOtherInfo        Residual count
*   \return:                None
*
*   \note - This is a initiator specific function called by the jump table.
*
*****************************************************************************/
osGLOBAL void
itdssTaskCompleted(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus,
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 )
{
  tdsaRootOsData_t            *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                    *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiIORequest_t               *taskTag = agNULL, *currentTaskTag = agNULL;
  tdIORequestBody_t           *tdIORequestBody = agNULL;
  tdIORequestBody_t           *TMtdIORequestBody = agNULL;
  tdIORequestBody_t           *AborttdIORequestBody = agNULL;
  agsaIORequest_t             *agTaskedIORequest;
  agsaSSPResponseInfoUnit_t   agSSPRespIU;
  bit8                        respData[128];
  bit32                       respLen;
#ifdef  TD_DEBUG_ENABLE
  bit32                       data_status;
#endif
  agsaSASRequestBody_t        *agSASRequestBody = agNULL;
  agsaSSPScsiTaskMgntReq_t    *agSSPTaskMgntRequest = agNULL;
  agsaIORequest_t             *agAbortIORequest;
  tdIORequestBody_t           *tdAbortIORequestBody;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  void                        *osMemHandle;
  bit32                       abortOrquery = agTRUE;
  tiDeviceHandle_t            *tiDeviceHandle = agNULL;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  agsaDevHandle_t             *agDevHandle = agNULL;
  bit32                        status = AGSA_RC_FAILURE;

  TI_DBG2(("itdssTaskCompleted: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  /* check the agIOStatus */
  currentTaskTag = tdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag;

  if (currentTaskTag == agNULL)
  {
    TI_DBG1(("itdssTaskCompleted: currentTaskTag is NULL \n"));
	/* as the currentTaskTag is agNULL, shall not call ostiInitiatorEvent */
	#if 0
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );
    #endif
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    TI_DBG1(("itdssTaskCompleted: agIOStatus failed and tiTMFailed\n"));
    if (agIOStatus == OSSA_IO_TM_TAG_NOT_FOUND)
    {
      TI_DBG1(("itdssTaskCompleted: agIOStatus OSSA_IO_TM_TAG_NOT_FOUND\n"));
    }
    else
    if (agIOStatus == OSSA_IO_ABORTED)
    {
      TI_DBG1(("itdssTaskCompleted: agIOStatus OSSA_IO_ABORTED\n"));
    }
    else
    {
      TI_DBG1(("itdssTaskCompleted: agIOStatus 0x%x\n", agIOStatus));
    }
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  /* parse the task management response */
  /* reads agsaSSPResponseInfoUnit_t */
  saFrameReadBlock(agRoot, agParam, 0, &agSSPRespIU, sizeof(agsaSSPResponseInfoUnit_t));
#ifdef  TD_DEBUG_ENABLE
  data_status = SA_SSPRESP_GET_DATAPRES(&agSSPRespIU);
#endif
  respLen = SA_SSPRESP_GET_RESPONSEDATALEN(&agSSPRespIU);
  TI_DBG6(("itdssTaskCompleted: dataPres %d. should be 1\n", data_status));
  /* reads response data */
  saFrameReadBlock(agRoot, agParam,
                   sizeof(agsaSSPResponseInfoUnit_t),
                   respData, respLen);
  TI_DBG6(("itdssTaskCompleted: res code %d. should be 0\n", respData[3]));

  taskTag = tdIORequestBody->IOType.InitiatorTMIO.TaskTag;
  if (taskTag == agNULL)
  {
    /* other than Abort Task or Query Task */
    TI_DBG1(("itdssTaskCompleted: taskTag is NULL\n"));

    abortOrquery = agFALSE;
    TMtdIORequestBody = (tdIORequestBody_t *)currentTaskTag->tdData;
  }
  else
  {
    /* Abort Task or Query Task */
    TI_DBG2(("itdssTaskCompleted: taskTag is NOT NULL\n"));
    abortOrquery = agTRUE;
    TMtdIORequestBody = (tdIORequestBody_t *)currentTaskTag->tdData;
  }

  TI_DBG2(("itdssTaskCompleted: TMtdIORequestBody %p\n", TMtdIORequestBody));

  if (TMtdIORequestBody == agNULL)
  {
    TI_DBG1(("itdssTaskCompleted: TMtdIORequestBody is NULL \n"));
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  if (agIOStatus == OSSA_IO_SUCCESS && agIOInfoLen == 0)
  {
    TI_DBG1(("itdssTaskCompleted: agIOInfoLen is zero, wrong\n"));
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  agSASRequestBody = (agsaSASRequestBody_t *)&(TMtdIORequestBody->transport.SAS.agSASRequestBody);
  agSSPTaskMgntRequest = (agsaSSPScsiTaskMgntReq_t *)&(agSASRequestBody->sspTaskMgntReq);
  TI_DBG2(("itdssTaskCompleted: agSSPTaskMgntRequest->taskMgntFunction 0x%x\n", agSSPTaskMgntRequest->taskMgntFunction));

  if ( (agSSPTaskMgntRequest->taskMgntFunction == AGSA_ABORT_TASK ||
        agSSPTaskMgntRequest->taskMgntFunction == AGSA_QUERY_TASK) &&
        abortOrquery == agFALSE
      )
  {
    TI_DBG1(("itdssTaskCompleted: incorrect tasktag, first\n"));
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  if ((agSSPTaskMgntRequest->taskMgntFunction == AGSA_ABORT_TASK_SET ||
       agSSPTaskMgntRequest->taskMgntFunction == AGSA_CLEAR_TASK_SET ||
       agSSPTaskMgntRequest->taskMgntFunction == AGSA_LOGICAL_UNIT_RESET ||
       agSSPTaskMgntRequest->taskMgntFunction == AGSA_CLEAR_ACA ) &&
       abortOrquery == agTRUE
     )
  {
    TI_DBG1(("itdssTaskCompleted: incorrect tasktag, second\n"));
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }


  if (agSSPTaskMgntRequest->taskMgntFunction == AGSA_ABORT_TASK)
  {
    TI_DBG2(("itdssTaskCompleted: calling saSSPAbort()\n"));
    AborttdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
    if (AborttdIORequestBody == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, AborttdIORequestBody is NULL\n"));
      return;
    }

    tiDeviceHandle = AborttdIORequestBody->tiDevHandle;
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, tiDeviceHandle is NULL\n"));
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, oneDeviceData is NULL\n"));
      return;
    }
    agDevHandle = oneDeviceData->agDevHandle;
    if (agDevHandle == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, agDevHandle is NULL\n"));
    }

    agTaskedIORequest = (agsaIORequest_t *)&(AborttdIORequestBody->agIORequest);
    if (agTaskedIORequest == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: agTaskedIORequest is NULL \n"));
      ostiInitiatorEvent( tiRoot,
                          NULL,
                          NULL,
                          tiIntrEventTypeTaskManagement,
                          tiTMFailed,
                          currentTaskTag );
      /* free up allocated memory */
      ostiFreeMemory(
                     tiRoot,
                     tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
      return;
    }


    /* allocating agIORequest for abort itself */
    memAllocStatus = ostiAllocMemory(
                                     tiRoot,
                                     &osMemHandle,
                                     (void **)&tdAbortIORequestBody,
                                     &PhysUpper32,
                                     &PhysLower32,
                                     8,
                                     sizeof(tdIORequestBody_t),
                                     agTRUE
                                     );
    if (memAllocStatus != tiSuccess)
    {
      /* let os process IO */
      TI_DBG1(("itdssTaskCompleted: ostiAllocMemory failed...\n"));
      return;
    }

    if (tdAbortIORequestBody == agNULL)
    {
      /* let os process IO */
      TI_DBG1(("itdssTaskCompleted: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
      return;
    }

    /* setup task management structure */
    tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
    tdAbortIORequestBody->tiDevHandle = tiDeviceHandle;
    /* setting callback */
    tdAbortIORequestBody->IOCompletionFunc = itdssIOAbortedHandler;

    /* setting to NULL because the local abort is triggered by TD layer */
    tdAbortIORequestBody->tiIOToBeAbortedRequest = agNULL;   
    /* initialize agIORequest */
    agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
    agAbortIORequest->osData = (void *) tdAbortIORequestBody;
    agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

    status = saSSPAbort(agRoot, agAbortIORequest, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, 0, agTaskedIORequest, agNULL);
    if (status != AGSA_RC_SUCCESS)
    {
      TI_DBG1(("itdssTaskCompleted: saSSPAbort failed agIOInfoLen is zero, wrong\n"));
      ostiFreeMemory(
                     tiRoot,
                     tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                     sizeof(tdIORequestBody_t)
                     );
    }
  }

  /*
    parse the response and based on the parse,
    set the flag
  */
  if (respData[3] == AGSA_TASK_MANAGEMENT_FUNCTION_COMPLETE ||
      respData[3] == AGSA_TASK_MANAGEMENT_FUNCTION_SUCCEEDED)
  {
    TI_DBG2(("itdssTaskCompleted: tiTMOK\n"));
    tiDeviceHandle = TMtdIORequestBody->tiDevHandle;
    if (tiDeviceHandle == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, tiDeviceHandle is NULL\n"));
      return;
    }
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    if (oneDeviceData == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, oneDeviceData is NULL\n"));
      return;
    }
    agDevHandle = oneDeviceData->agDevHandle;
    if (agDevHandle == agNULL)
    {
      TI_DBG1(("itdssTaskCompleted: wrong, agDevHandle is NULL\n"));
    }
    TI_DBG2(("itdssTaskCompleted: setting Device state to SA_DS_OPERATIONAL\n"));

    saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_OPERATIONAL);

    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMOK,
                        currentTaskTag );
  }
  else
  {
    TI_DBG1(("itdssTaskCompleted: tiTMFailed\n"));
    ostiInitiatorEvent( tiRoot,
                        NULL,
                        NULL,
                        tiIntrEventTypeTaskManagement,
                        tiTMFailed,
                        currentTaskTag );

  }

  /* free up allocated memory */
  ostiFreeMemory(
                 tiRoot,
                 tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                 sizeof(tdIORequestBody_t)
                 );
  return;
}

#ifdef INITIATOR_DRIVER

/*****************************************************************************
*!  \brief  itdssQueryTaskCompleted
*
*  Purpose: This routine is called to complete an query task management request
*           previously issued to the LL Layer.
*
*   \param  agRoot:         Pointer to driver Instance.
*   \param  agIORequest:    Pointer to the I/O Request data structure for
*                           this I/O.
*   \param  agIOStatus:     Status of I/O just completed.
*   \param  agIOInfoLen:    Length of the I/O information associated with this
*                           I/O request
*   \param   agParam        A Handle used to refer to the response frame or handle
*                           of abort request
*
*   \return:                None
*
*   \note - This is a initiator specific function called by the jump table.
*
*****************************************************************************/
osGLOBAL void
itdssQueryTaskCompleted(
                        agsaRoot_t             *agRoot,
                        agsaIORequest_t        *agIORequest,
                        bit32                  agIOStatus,
                        bit32                  agIOInfoLen,
                        void                   *agParam,
                        bit32                  agOtherInfo
                        )
{
  tdsaRootOsData_t            *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                    *tiRoot = (tiRoot_t *)osData->tiRoot;
  tiIORequest_t               *taskTag = agNULL;
  tdIORequestBody_t           *tdIORequestBody = agNULL;  /* query task */
  tdIORequestBody_t           *TMtdIORequestBody = agNULL; /* IO being query tasked */
  agsaIORequest_t             *agTaskedIORequest = agNULL;
  agsaSSPResponseInfoUnit_t   agSSPRespIU;
  bit8                        respData[128];
  bit32                       respLen;
#ifdef  TD_DEBUG_ENABLE
  bit32                       data_status;
#endif
  agsaSASRequestBody_t        *agSASRequestBody = agNULL;
  agsaSSPScsiTaskMgntReq_t    *agSSPTaskMgntRequest = agNULL;
  bit32                       status;
  agsaIORequest_t             *agAbortIORequest = agNULL;
  tdIORequestBody_t           *tdAbortIORequestBody = agNULL;
  bit32                       PhysUpper32;
  bit32                       PhysLower32;
  bit32                       memAllocStatus;
  void                        *osMemHandle = agNULL;
  tiDeviceHandle_t            *tiDeviceHandle = agNULL;
  tdsaDeviceData_t            *oneDeviceData = agNULL;
  agsaDevHandle_t             *agDevHandle = agNULL;

  TI_DBG2(("itdssQueryTaskComplted: start\n"));

  /* query task management IORequestBody */
  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  /* OS's tiIORequest for this query taks, which is agNULL */
  //currentTaskTag = tdIORequestBody->IOType.InitiatorTMIO.CurrentTaskTag;

  /*
    currentTaskTag is agNULL for query task since it is generated by
    TD layer
  */
  if (agIOStatus != OSSA_IO_SUCCESS)
  {
    /* let os process IO */
    TI_DBG1(("itdssQueryTaskComplted: agIOStatus failed and tiTMFailed\n"));
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }
  /* parse the task management response */
  /* reads agsaSSPResponseInfoUnit_t */
  saFrameReadBlock(agRoot, agParam, 0, &agSSPRespIU, sizeof(agsaSSPResponseInfoUnit_t));
#ifdef  TD_DEBUG_ENABLE
  data_status = SA_SSPRESP_GET_DATAPRES(&agSSPRespIU);
#endif
  respLen = SA_SSPRESP_GET_RESPONSEDATALEN(&agSSPRespIU);

  TI_DBG6(("itdssQueryTaskCompleted: dataPres %d. should be 1\n", data_status));
  /* reads response data */
  saFrameReadBlock(agRoot, agParam,
                   sizeof(agsaSSPResponseInfoUnit_t),
                   respData, respLen);

  TI_DBG6(("itdssQueryTaskCompleted: res code %d. should be 0\n", respData[3]));

  /* IO being query tasked */
  taskTag = tdIORequestBody->IOType.InitiatorTMIO.TaskTag;
  if (taskTag == agNULL)
  {
    TI_DBG1(("itdssQueryTaskComplted: taskTag is NULL \n"));
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  /* request body of IO being query tasked  */
  TMtdIORequestBody = (tdIORequestBody_t *)taskTag->tdData;
  if (TMtdIORequestBody == agNULL)
  {
    TI_DBG1(("itdssQueryTaskComplted: TMtdIORequestBody is NULL \n"));
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  agTaskedIORequest = &(TMtdIORequestBody->agIORequest);
  if (agTaskedIORequest == agNULL)
  {
    TI_DBG1(("itdssQueryTaskComplted: agTaskedIORequest is NULL \n"));
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
    return;
  }

  if (agIOStatus == OSSA_IO_SUCCESS && agIOInfoLen == 0)
  {
    TI_DBG1(("itdssQueryTaskCompleted: agIOInfoLen is zero, wrong\n"));
    /* free up allocated memory */
    ostiFreeMemory(
                   tiRoot,
                   tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                   sizeof(tdIORequestBody_t)
                   );
   return;
  }
  /* this is query task itself */
  agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
  agSSPTaskMgntRequest = &(agSASRequestBody->sspTaskMgntReq);
  if (agSSPTaskMgntRequest->taskMgntFunction == AGSA_QUERY_TASK)
  {
    /*
      process response for query task
      For query task, response code must be either
      TASK MANAGEMENT FUNCTION COMPLETE or TASK MANAGEMENT FUNCTION SUCCEEDED by
      SAM

      1. If TASK MANAGEMENT FUNCTION SUCCEEDE, do nothing

      2. If TASK MANAGEMENT FUNCTION COMPLETE and IO is not completed,
      retry by saSSPAbort()
    */
    if (respData[3] == AGSA_TASK_MANAGEMENT_FUNCTION_SUCCEEDED)
    {
      /* OK; IO is being process at the target; do nothing */
    }
    else if (respData[3] == AGSA_TASK_MANAGEMENT_FUNCTION_COMPLETE)
    {
      tiDeviceHandle = TMtdIORequestBody->tiDevHandle;
      if (tiDeviceHandle == agNULL)
      {
        TI_DBG1(("itdssQueryTaskCompleted: wrong, tiDeviceHandle is NULL\n"));
        /* free up allocated memory */
        ostiFreeMemory(
                       tiRoot,
                       tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                       sizeof(tdIORequestBody_t)
                       );
        return;
      }
      oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      if (oneDeviceData == agNULL)
      {
        TI_DBG1(("itdssQueryTaskCompleted: wrong, oneDeviceData is NULL\n"));
        /* free up allocated memory */
        ostiFreeMemory(
                       tiRoot,
                       tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                       sizeof(tdIORequestBody_t)
                       );

        return;
      }
      agDevHandle = oneDeviceData->agDevHandle;
      if (agDevHandle == agNULL)
      {
        TI_DBG1(("itdssQueryTaskCompleted: wrong, agDevHandle is NULL\n"));
      }
      /* if IO is not completed, retry IO by saSSPAbort() */
      if (TMtdIORequestBody->ioCompleted != agTRUE)
      {
        /* allocating agIORequest for abort itself */
        memAllocStatus = ostiAllocMemory(
                                         tiRoot,
                                         &osMemHandle,
                                         (void **)&tdAbortIORequestBody,
                                         &PhysUpper32,
                                         &PhysLower32,
                                         8,
                                         sizeof(tdIORequestBody_t),
                                         agTRUE
                                         );
        if (memAllocStatus != tiSuccess)
        {
          /* let os process IO */
          TI_DBG1(("itdssQueryTaskCompleted: ostiAllocMemory failed...\n"));
          /* free up allocated memory */
          ostiFreeMemory(
                         tiRoot,
                         tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                         sizeof(tdIORequestBody_t)
                         );

          return;
        }
        if (tdAbortIORequestBody == agNULL)
        {
          /* let os process IO */
          TI_DBG1(("itdssQueryTaskCompleted: ostiAllocMemory returned NULL tdAbortIORequestBody\n"));
          /* free up allocated memory */
          ostiFreeMemory(
                         tiRoot,
                         tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                         sizeof(tdIORequestBody_t)
                         );

          return;
        }

        /* setup task management structure */
        tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle = osMemHandle;
        tdAbortIORequestBody->tiDevHandle = tdIORequestBody->tiDevHandle;
        tdAbortIORequestBody->tiIOToBeAbortedRequest = agNULL;

        /* setting callback */
        tdAbortIORequestBody->IOCompletionFunc = itdssIOAbortedHandler;

        /* initialize agIORequest */
        agAbortIORequest = &(tdAbortIORequestBody->agIORequest);
        agAbortIORequest->osData = (void *) tdAbortIORequestBody;
        agAbortIORequest->sdkData = agNULL; /* LL takes care of this */

        TI_DBG2(("itdssQueryTaskCompleted: issuing saSSPAbort()\n"));
        status = saSSPAbort(agRoot, agAbortIORequest, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, 0, agTaskedIORequest, agNULL);
        if (status != AGSA_RC_SUCCESS)
        {
          TI_DBG1(("itdssQueryTaskCompleted: saSSPAbort failed agIOInfoLen is zero, wrong\n"));
          ostiFreeMemory(
                         tiRoot,
                         tdAbortIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                         sizeof(tdIORequestBody_t)
                         );
        }
      }
    }
    else
    {
      TI_DBG1(("itdssQueryTaskComplted: not expected response 0x%x\n",respData[3]));
    }
  }
  else
  {
    TI_DBG1(("itdssQueryTaskCompleted: not expected task management fn %d\n",agSSPTaskMgntRequest->taskMgntFunction));
  }

  /* free up allocated memory */
  ostiFreeMemory(
                 tiRoot,
                 tdIORequestBody->IOType.InitiatorTMIO.osMemHandle,
                 sizeof(tdIORequestBody_t)
                 );
  return;
}
#endif

/*****************************************************************************
*!  \brief  itssdosIOCompleted
*
*  Purpose: This routine is called to complete an I/O request previously
*           issued to the LL Layer in saSSPStart().
*
*   \param  agRoot:       Pointer to driver Instance.
*   \param  agIORequest:  Pointer to the I/O Request data structure for
*                         this I/O.
*   \param  agIOStatus:   Status of I/O just completed.
*   \param  agIOInfoLen:  Length of the I/O information associated with this
*                         I/O request
*   \param   agParam      A Handle used to refer to the response frame or handle
*                         of abort request
*  \param  agOtherInfo    Residual count
*   \return:              None
*
*   \note - This is a initiator specific function called by the jump table.
*
*****************************************************************************/
FORCEINLINE void
itdssIOCompleted(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                   agIOStatus,
                 bit32                   agIOInfoLen,
                 void                   *agParam,
                 bit32                   agOtherInfo
                 )
{
  tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
  itdsaIni_t                *Initiator = (itdsaIni_t *)osData->itdsaIni;
  tdIORequestBody_t         *tdIORequestBody  = agNULL;
  agsaSASRequestBody_t      *agSASRequestBody = agNULL;
  agsaSSPInitiatorRequest_t *agSSPInitiatorRequest = agNULL;
  agsaSSPResponseInfoUnit_t  agSSPRespIU;

  bit32 scsi_status = 0;

  tiDeviceHandle_t          *tiDeviceHandle = agNULL;
  tdsaDeviceData_t          *oneDeviceData  = agNULL;

  TI_DBG6(("itdssIOCompleted: start\n"));
  TI_DBG6(("itdssIOCompleted: agIOInfoLen %d\n", agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  TD_ASSERT((NULL != tdIORequestBody), "itdssIOCompleted:tdIORequestBody NULL");
  if ( NULL == tdIORequestBody )  // handle windows assert case
  {
    return;
  }
  Initiator->NumIOsActive--;

#ifdef DBG
  if (tdIORequestBody->ioCompleted == agTRUE)
  {
#ifdef  TD_DEBUG_ENABLE
    tiDeviceHandle = tdIORequestBody->tiDevHandle;
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
#endif /*TD_DEBUG_ENABLE*/
    TI_DBG1(("itdssIOCompleted: Error!!!!!! double completion\n"));
#ifdef  TD_DEBUG_ENABLE
    TI_DBG1(("itdssIOCompleted: did %d \n", oneDeviceData->id));
#endif /*TD_DEBUG_ENABLE*/
  }

  if (Initiator->NumIOsActive == 0)
  {
    /* so far, no timer assocaicated here */
    TI_DBG6(("itdssIOCompleted: no acitve IO's. Kill timers\n"));
  }

  if (tdIORequestBody->tiIORequest->osData == agNULL)
  {
    TI_DBG1( ("itdssIOCompleted: pos 1; "
              "tdIORequestBody->tiIORequest->osData is null, wrong\n") );
  }
#endif /*DBG*/

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  /* Process completion for debugging, printing cbd */
  agSASRequestBody = &(tdIORequestBody->transport.SAS.agSASRequestBody);
  agSSPInitiatorRequest = &(agSASRequestBody->sspInitiatorReq);

  TI_DBG6( ("itdssIOCompleted: CDB 0x%x\n",
            agSSPInitiatorRequest->sspCmdIU.cdb[0]) );

  /* no respsonse or sense data; data has been processed */
  if((agIOStatus == OSSA_IO_SUCCESS) && (agIOInfoLen == 0))
  {
    // if this is a standard Inquiry command, notify Stoport to set the
    // device queue depth to maximize oustanding IO
    if ( (agSSPInitiatorRequest->sspCmdIU.cdb[0] == SCSIOPC_INQUIRY) &&
         ((agSSPInitiatorRequest->sspCmdIU.cdb[1] & 0x01) == 0))
    {
      bit32 qdepth = 32;
      tiDeviceHandle = tdIORequestBody->tiDevHandle;
      if( tiDeviceHandle )
      {
        oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
        if( oneDeviceData->DeviceType == TD_SAS_DEVICE )
        {
          qdepth = MAX_OUTSTANDING_IO_PER_LUN;
        }
        if( oneDeviceData->DeviceType == TD_SATA_DEVICE )
        {
          qdepth = 63;
        }
      }

      if ( ostiSetDeviceQueueDepth( tiRoot,
                                    tdIORequestBody->tiIORequest,
                                    MAX_OUTSTANDING_IO_PER_LUN ) == agFALSE )
      {
        TI_DBG1( ( "itdssIOCompleted: failed to call "
                   "ostiSetDeviceQueueDepth() Q=%d !!!\n", qdepth ) );
      }
      else
      {
        TI_DBG2(("itdssIOCompleted: set ostiSetDeviceQueueDepth() Q=%d\n",qdepth));
      }
    }
    // SCSI command was completed OK, this is the normal path. Now call the
    // OS Specific module about this completion.
    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest,
                             tiIOSuccess,
                             SCSI_STAT_GOOD,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }

  else
  {
    TI_DBG6(("itdssIOCompleted: SUCCESS but data returned \n"));
    TI_DBG6( ("itdssIOCompleted: agIOStatus SUCCESS but data returned 0x%x\n",
              agIOStatus) );
    if(tdIORequestBody)
    {
      tiDeviceHandle = tdIORequestBody->tiDevHandle;
      if(tiDeviceHandle)
      {
        oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      }
    }

    osti_memset(&agSSPRespIU, 0, sizeof(agsaSSPResponseInfoUnit_t));

    saFrameReadBlock( agRoot,
                      agParam,
                      0,
                      &agSSPRespIU,
                      sizeof(agsaSSPResponseInfoUnit_t) );
    scsi_status = agSSPRespIU.status;

    switch (scsi_status)
    {
      case SCSI_STAT_GOOD:
        TI_DBG2( ("itdssIOCompleted: SCSI_STAT_GOOD %d\n",
                  Initiator->ScsiStatusCounts.GoodStatus) );
        Initiator->ScsiStatusCounts.GoodStatus++;
        break;
       case SCSI_STAT_CHECK_CONDITION:
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_CHECK_CONDITION %d\n",
                  Initiator->ScsiStatusCounts.CheckCondition) );
        Initiator->ScsiStatusCounts.CheckCondition++;
        break;
      case SCSI_STAT_BUSY:
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_BUSY %d\n",
                  Initiator->ScsiStatusCounts.BusyStatus) );
        Initiator->ScsiStatusCounts.BusyStatus++;
        break;
      case SCSI_STAT_RESV_CONFLICT:
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_RESV_CONFLICT %d\n",
                  Initiator->ScsiStatusCounts.ResvConflict) );
        Initiator->ScsiStatusCounts.ResvConflict++;
        break;
      case SCSI_STAT_TASK_SET_FULL:
        Initiator->ScsiStatusCounts.TaskSetFull++;
        //agIOStatus =  OSSA_IO_FAILED;
        //agOtherInfo = tiDetailBusy;
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_TASK_SET_FULL %d\n",
                  Initiator->ScsiStatusCounts.TaskSetFull) );
        break;
      case SCSI_STAT_ACA_ACTIVE:
        Initiator->ScsiStatusCounts.AcaActive++;
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_ACA_ACTIVE %d\n",
                  Initiator->ScsiStatusCounts.AcaActive) );
        break;
      case SCSI_STAT_TASK_ABORTED:
        Initiator->ScsiStatusCounts.TaskAborted++;
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_TASK_ABORTED %d\n",
                  Initiator->ScsiStatusCounts.TaskAborted) );
        break;
      case SCSI_STAT_CONDITION_MET:
        Initiator->ScsiStatusCounts.ConditionMet++;
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_CONDITION_MET %d\n",
                  Initiator->ScsiStatusCounts.ConditionMet) );
        break;
      case SCSI_STAT_INTERMEDIATE:
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_INTERMEDIATE %d\n",
                  Initiator->ScsiStatusCounts.ObsoleteStatus) );
        Initiator->ScsiStatusCounts.ObsoleteStatus++;
        break;
      case SCSI_STAT_INTER_CONDIT_MET:
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_INTER_CONDIT_MET %d\n",
                  Initiator->ScsiStatusCounts.ObsoleteStatus) );
        Initiator->ScsiStatusCounts.ObsoleteStatus++;
        break;
      case SCSI_STAT_COMMANDTERMINATED:
        TI_DBG1( ("itdssIOCompleted: SCSI_STAT_COMMANDTERMINATED %d\n",
                  Initiator->ScsiStatusCounts.ObsoleteStatus) );
        Initiator->ScsiStatusCounts.ObsoleteStatus++;
        break;
      default:
        Initiator->ScsiStatusCounts.ObsoleteStatus++;
        TI_DBG1( ("itdssIOCompleted: Unknown scsi_status %d 0x%x\n",
                  scsi_status,Initiator->ScsiStatusCounts.ObsoleteStatus) );
    }

    switch (agIOStatus)
    {
    case OSSA_IO_SUCCESS:
      itdssIOSuccessHandler( agRoot,
                             agIORequest,
                             agIOStatus,
                             agIOInfoLen,
                             agParam,
                             agOtherInfo );
      break;
    case OSSA_IO_ABORTED:
      itdssIOAbortedHandler( agRoot,
                             agIORequest,
                             agIOStatus,
                             agIOInfoLen,
                             agParam,
                             agOtherInfo );
      break;
    case OSSA_IO_UNDERFLOW:
      itdssIOUnderFlowHandler( agRoot,
                               agIORequest,
                               agIOStatus,
                               agIOInfoLen,
                               agParam,
                               agOtherInfo );
      break;
    case OSSA_IO_FAILED:
      itdssIOFailedHandler( agRoot,
                            agIORequest,
                            agIOStatus,
                            agIOInfoLen,
                            agParam,
                            agOtherInfo );
      break;
    case OSSA_IO_ABORT_RESET:
      itdssIOAbortResetHandler( agRoot,
                                agIORequest,
                                agIOStatus,
                                agIOInfoLen,
                                agParam,
                                agOtherInfo );
      break;
    case OSSA_IO_NO_DEVICE:
      itdssIONoDeviceHandler( agRoot,
                              agIORequest,
                              agIOStatus,
                              agIOInfoLen,
                              agParam,
                              agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_BREAK:
      itdssXferErrorBreakHandler( agRoot,
                                  agIORequest,
                                  agIOStatus,
                                  agIOInfoLen,
                                  agParam,
                                  agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_PHY_NOT_READY:
      itdssXferErrorPhyNotReadyHandler( agRoot,
                                        agIORequest,
                                        agIOStatus,
                                        agIOInfoLen,
                                        agParam,
                                        agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED:
      itdssOpenCnxErrorProtocolNotSupprotedHandler( agRoot,
                                                    agIORequest,
                                                    agIOStatus,
                                                    agIOInfoLen,
                                                    agParam,
                                                    agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION:
      itdssOpenCnxErrorZoneViolationHandler( agRoot,
                                             agIORequest,
                                             agIOStatus,
                                             agIOInfoLen,
                                             agParam,
                                             agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_BREAK:
      itdssOpenCnxErrorBreakHandler( agRoot,
                                     agIORequest,
                                     agIOStatus,
                                     agIOInfoLen,
                                     agParam,
                                     agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS:
      itdssOpenCnxErrorITNexusLossHandler( agRoot,
                                           agIORequest,
                                           agIOStatus,
                                           agIOInfoLen,
                                           agParam,
                                           agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION:
      itdssOpenCnxErrorBadDestinationHandler( agRoot,
                                              agIORequest,
                                              agIOStatus,
                                              agIOInfoLen,
                                              agParam,
                                              agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED:
      itdssOpenCnxErrorConnectionRateNotSupportedHandler( agRoot,
                                                          agIORequest,
                                                          agIOStatus,
                                                          agIOInfoLen,
                                                          agParam,
                                                          agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION:
      itdssOpenCnxErrorWrongDestinationHandler( agRoot,
                                                agIORequest,
                                                agIOStatus,
                                                agIOInfoLen,
                                                agParam,
                                                agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR:
      itdssOpenCnxErrorUnknownErrorHandler( agRoot,
                                            agIORequest,
                                            agIOStatus,
                                            agIOInfoLen,
                                            agParam,
                                            agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_NAK_RECEIVED:
      itdssXferErrorNAKReceivedHandler( agRoot,
                                        agIORequest,
                                        agIOStatus,
                                        agIOInfoLen,
                                        agParam,
                                        agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT:
      itdssXferErrorACKNAKTimeoutHandler( agRoot,
                                          agIORequest,
                                          agIOStatus,
                                          agIOInfoLen,
                                          agParam,
                                          agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_DMA:
      itdssXferErrorDMAHandler( agRoot,
                                agIORequest,
                                agIOStatus,
                                agIOInfoLen,
                                agParam,
                                agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_OFFSET_MISMATCH:
      itdssXferErrorOffsetMismatchHandler( agRoot,
                                           agIORequest,
                                           agIOStatus,
                                           agIOInfoLen,
                                           agParam,
                                           agOtherInfo );
      break;
    case OSSA_IO_XFER_OPEN_RETRY_TIMEOUT:
      itdssXferOpenRetryTimeoutHandler( agRoot,
                                        agIORequest,
                                        agIOStatus,
                                        agIOInfoLen,
                                        agParam,
                                        agOtherInfo );
      break;
    case OSSA_IO_PORT_IN_RESET:
      itdssPortInResetHandler( agRoot,
                               agIORequest,
                               agIOStatus,
                               agIOInfoLen,
                               agParam,
                               agOtherInfo );
      break;
    case OSSA_IO_DS_NON_OPERATIONAL:
      itdssDsNonOperationalHandler( agRoot,
                                    agIORequest,
                                    agIOStatus,
                                    agIOInfoLen,
                                    agParam,
                                    agOtherInfo );
      break;
    case OSSA_IO_DS_IN_RECOVERY:
      itdssDsInRecoveryHandler( agRoot,
                                agIORequest,
                                agIOStatus,
                                agIOInfoLen,
                                agParam,
                                agOtherInfo );
      break;
    case OSSA_IO_TM_TAG_NOT_FOUND:
      itdssTmTagNotFoundHandler( agRoot,
                                 agIORequest,
                                 agIOStatus,
                                 agIOInfoLen,
                                 agParam,
                                 agOtherInfo );
      break;
    case OSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR:
      itdssSSPExtIUZeroLenHandler( agRoot,
                                   agIORequest,
                                   agIOStatus,
                                   agIOInfoLen,
                                   agParam,
                                   agOtherInfo );
      break;
    case OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE:
      itdssXferErrorUnexpectedPhaseHandler( agRoot,
                                            agIORequest,
                                            agIOStatus,
                                            agIOInfoLen,
                                            agParam,
                                            agOtherInfo );
      break;
//new
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED:
      itdssXferOpenRetryBackoffThresholdReachedHandler( agRoot,
                                                        agIORequest,
                                                        agIOStatus,
                                                        agIOInfoLen,
                                                        agParam,
                                                        agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO:
      itdssOpenCnxErrorItNexusLossOpenTmoHandler( agRoot,
                                                  agIORequest,
                                                  agIOStatus,
                                                  agIOInfoLen,
                                                  agParam,
                                                  agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST:
      itdssOpenCnxErrorItNexusLossNoDestHandler( agRoot,
                                                 agIORequest,
                                                 agIOStatus,
                                                 agIOInfoLen,
                                                 agParam,
                                                 agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE:
      itdssOpenCnxErrorItNexusLossOpenCollideHandler( agRoot,
                                                      agIORequest,
                                                      agIOStatus,
                                                      agIOInfoLen,
                                                      agParam,
                                                      agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED:
      itdssOpenCnxErrorItNexusLossOpenPathwayBlockedHandler( agRoot,
                                                             agIORequest,
                                                             agIOStatus,
                                                             agIOInfoLen,
                                                             agParam,
                                                             agOtherInfo );
      break;
      // encryption IO error handling
    case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
    case OSSA_IO_XFR_ERROR_DEK_KEY_TAG_MISMATCH:
    case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID:
    case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH:
    case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR:
    case OSSA_IO_XFR_ERROR_INTERNAL_RAM:
    case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
      itdssEncryptionHandler( agRoot,
                              agIORequest,
                              agIOStatus,
                              agIOInfoLen,
                              agParam,
                              agOtherInfo );
      break;

    /* DIF IO error handling */
    case OSSA_IO_XFR_ERROR_DIF_MISMATCH:
    case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
    case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
    case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
      itdssDifHandler( agRoot,
                       agIORequest,
                       agIOStatus,
                       agIOInfoLen,
                       agParam,
                       agOtherInfo );
      break;
    case OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE:
      itdssIOResourceUnavailableHandler( agRoot,
                                         agIORequest,
                                         agIOStatus,
                                         agIOInfoLen,
                                         agParam,
                                         agOtherInfo );
      break;
    case OSSA_MPI_IO_RQE_BUSY_FULL:
      itdssIORQEBusyFullHandler( agRoot,
                                 agIORequest,
                                 agIOStatus,
                                 agIOInfoLen,
                                 agParam,
                                 agOtherInfo );
      break;
    case OSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME:
      itdssXferErrorInvalidSSPRspFrameHandler( agRoot,
                                               agIORequest,
                                               agIOStatus,
                                               agIOInfoLen,
                                               agParam,
                                               agOtherInfo );
      break;
    case OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN:
      itdssXferErrorEOBDataOverrunHandler( agRoot,
                                           agIORequest,
                                           agIOStatus,
                                           agIOInfoLen,
                                           agParam,
                                           agOtherInfo );
      break;
    case OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED:
      itdssOpenCnxErrorOpenPreemptedHandler( agRoot,
                                             agIORequest,
                                             agIOStatus,
                                             agIOInfoLen,
                                             agParam,
                                             agOtherInfo );
      break;
    default:
      TI_DBG1( ("itdssIOCompleted: Unknown agIOStatus 0x%x\n",agIOStatus) );
      itdssIODefaultHandler( agRoot,
                             agIORequest,
                             agIOStatus,
                             agIOInfoLen,
                             agParam,
                             agOtherInfo );
      break;
    }
  }
  return;
}

#ifdef TD_DISCOVER
/*****************************************************************************
*!  \brief  itdssSMPCompleted
*
*  Purpose: This routine is called to complete an SMP request previously
*           issued to the LL Layer in saSMPStart().
*
*   \param  agRoot:         Pointer to driver Instance.
*   \param  agIORequest:    Pointer to the I/O Request data structure for
*                           this I/O.
*   \param  agIOStatus:     Status of I/O just completed.
*   \param  agIOInfoLen:    Length of the I/O information associated with this
*                           I/O request
*   \param   agFrameHandle  A Handle used to refer to the response frame
*
*   \return:                None
*
*   \note - This is a initiator specific function called by the jump table.
*
*****************************************************************************/
osGLOBAL void
itdssSMPCompleted (
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   bit32                 agIOStatus,
                   bit32                 agIOInfoLen,
                   agsaFrameHandle_t     agFrameHandle
                   )
{
  tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
#ifdef REMOVED
  tdsaRoot_t                *tdsaRoot = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t             *tdsaAllShared = (tdsaContext_t *)&(tdsaRoot->tdsaAllShared);
#endif
  tdssSMPRequestBody_t      *tdSMPRequestBody;
  agsaSASRequestBody_t      *agSASRequestBody;
  agsaSMPFrame_t            *agSMPFrame;
  tdsaDeviceData_t          *oneDeviceData;
  tiIORequest_t             *CurrentTaskTag;
  tdsaPortContext_t         *onePortContext;
  tdsaPortContext_t         *oldonePortContext;
  smpReqPhyControl_t        *smpPhyControlReq;
  bit8                      smpHeader[4];
  tdssSMPFrameHeader_t      *tdSMPFrameHeader;
  bit8                      *tdSMPPayload;
  agsaDevHandle_t           *agDevHandle;
  bit32                     status;
#ifndef DIRECT_SMP
  tdssSMPFrameHeader_t      *tdRequestSMPFrameHeader;
  bit8                      smpRequestHeader[4];
#endif
  bit8                      SMPRequestFunction;

  TI_DBG3(("itdssSMPCompleted: start\n"));


  tdSMPRequestBody = (tdssSMPRequestBody_t *)agIORequest->osData;
  CurrentTaskTag  = tdSMPRequestBody->CurrentTaskTag;

  oneDeviceData = tdSMPRequestBody->tdDevice;
  onePortContext = oneDeviceData->tdPortContext;
  agDevHandle = oneDeviceData->agDevHandle;


  agSASRequestBody = &(tdSMPRequestBody->agSASRequestBody);
  agSMPFrame = &(agSASRequestBody->smpFrame);

#ifdef DIRECT_SMP
  SMPRequestFunction = tdSMPRequestBody->smpPayload[1];
#else
  saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 0, smpRequestHeader, 4);
  tdRequestSMPFrameHeader = (tdssSMPFrameHeader_t *)smpRequestHeader;
  SMPRequestFunction = tdRequestSMPFrameHeader->smpFunction;
#endif

  TI_DBG3(("itdssSMPCompleted: agIORequest %p\n", agIORequest));
  TI_DBG3(("itdssSMPCompleted: SMPRequestbody %p\n", tdSMPRequestBody));

  if (onePortContext != agNULL)
  {
    TI_DBG3(("itdssSMPCompleted: pid %d\n", onePortContext->id));
  }
  else
  {
    TI_DBG1(("itdssSMPCompleted: Wrong!!! onePortContext is NULL\n"));
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->osMemHandle,
                 sizeof(tdssSMPRequestBody_t)
                 );
#ifndef DIRECT_SMP
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                 );
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                 );
#endif
    return;
  }

  oldonePortContext = tdSMPRequestBody->tdPortContext;
  if (oldonePortContext != agNULL)
  {
    TI_DBG3(("itdssSMPCompleted: old pid %d\n", oldonePortContext->id));
  }
  else
  {
    TI_DBG1(("itdssSMPCompleted: Wrong!!! oldonePortContext is NULL\n"));
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->osMemHandle,
                 sizeof(tdssSMPRequestBody_t)
                 );
#ifndef DIRECT_SMP
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                 );
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                 );
#endif
    return;
  }


  /* decrement the number of pending SMP */
  onePortContext->discovery.pendingSMP--;

  /* for port invalid case;
     full discovery -> full discovery; incremental discovery -> full discovery
   */
  if (onePortContext != oldonePortContext)
  {
    TI_DBG1(("itdssSMPCompleted: portcontext has changed!!!\n"));
    if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
        SMPRequestFunction == SMP_REPORT_PHY_SATA ||
        SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION )
    {
      /* stop SMP timer */
      if (onePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tdsaKillTimer(
                      tiRoot,
                      &(onePortContext->discovery.DiscoverySMPTimer)
                     );
      }
      if (oldonePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tdsaKillTimer(
                      tiRoot,
                      &(oldonePortContext->discovery.DiscoverySMPTimer)
                     );
      }
    }

    /* clean up expanders data strucures; move to free exp when device is cleaned */
    tdsaCleanAllExp(tiRoot, oldonePortContext);
    /* remove devices */
    tdssInternalRemovals(oldonePortContext->agRoot,
                         oldonePortContext
                         );

    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->osMemHandle,
                 sizeof(tdssSMPRequestBody_t)
                 );
#ifndef DIRECT_SMP
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                 );
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                 );
#endif
    return;
  }

  if (onePortContext->valid == agFALSE)
  {
    if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
        SMPRequestFunction == SMP_REPORT_PHY_SATA ||
        SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION )
    {
      /* stop SMP timer */
      if (onePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tdsaKillTimer(
                      tiRoot,
                      &(onePortContext->discovery.DiscoverySMPTimer)
                      );
      }
      if (oldonePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
      {
        tdsaKillTimer(
                      tiRoot,
                      &(oldonePortContext->discovery.DiscoverySMPTimer)
                      );
      }
    }

    if (onePortContext->discovery.pendingSMP == 0)
    {
      TI_DBG1(("itdssSMPCompleted: aborting discovery\n"));
      tdsaSASDiscoverAbort(tiRoot, onePortContext);
    }
    else
    {
      TI_DBG1(("itdssSMPCompleted: not yet abort; non zero pendingSMP %d\n", onePortContext->discovery.pendingSMP));
    }
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->osMemHandle,
                 sizeof(tdssSMPRequestBody_t)
                 );
#ifndef DIRECT_SMP
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                 );
    ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                 );
#endif
    return;
  }


  if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
      SMPRequestFunction == SMP_REPORT_PHY_SATA ||
      SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION )
  {
    /* stop SMP timer */
    if (onePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &(onePortContext->discovery.DiscoverySMPTimer)
                    );
    }
    if (oldonePortContext->discovery.DiscoverySMPTimer.timerRunning == agTRUE)
    {
      tdsaKillTimer(
                    tiRoot,
                    &(oldonePortContext->discovery.DiscoverySMPTimer)
                    );
    }
  }

  /* the host as of 4/16/08 does not use indirect SMP. So, check only OSSA_IO_SUCCESS status*/
  if (agIOStatus == OSSA_IO_SUCCESS)
  {
    //tdhexdump("itdssSMPCompleted", (bit8*)agFrameHandle, agIOInfoLen);
    /* parsing SMP payload */
#ifdef DIRECT_SMP
    saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
#else
    saFrameReadBlock(agRoot, tdSMPRequestBody->IndirectSMPResp, 0, smpHeader, 4);
#endif
    tdSMPFrameHeader = (tdssSMPFrameHeader_t *)smpHeader;

    /* SMP function dependent payload */
    switch (tdSMPFrameHeader->smpFunction)
    {
    case SMP_REPORT_GENERAL:
      TI_DBG3(("itdssSMPCompleted: report general\n"));
      if (agIOInfoLen != sizeof(smpRespReportGeneral_t) + 4 &&
          tdSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
      {
        TI_DBG1(("itdssSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (unsigned int)sizeof(smpRespReportGeneral_t) + 4));
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->osMemHandle,
                       sizeof(tdssSMPRequestBody_t)
                      );
#ifndef DIRECT_SMP
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPReqosMemHandle,
                       tdSMPRequestBody->IndirectSMPReqLen
                      );
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPResposMemHandle,
                       tdSMPRequestBody->IndirectSMPRespLen
                      );
#endif
        return;
      }
      tdsaReportGeneralRespRcvd(
                                tiRoot,
                                agRoot,
                                agIORequest,
                                oneDeviceData,
                                tdSMPFrameHeader,
                                agFrameHandle
                                );

      break;
    case SMP_DISCOVER:
      TI_DBG3(("itdssSMPCompleted: discover\n"));
      if (agIOInfoLen != sizeof(smpRespDiscover_t) + 4 &&
          tdSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
      {
        TI_DBG1(("itdssSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (unsigned int)sizeof(smpRespDiscover_t) + 4));
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->osMemHandle,
                       sizeof(tdssSMPRequestBody_t)
                      );
#ifndef DIRECT_SMP
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPReqosMemHandle,
                       tdSMPRequestBody->IndirectSMPReqLen
                      );
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPResposMemHandle,
                       tdSMPRequestBody->IndirectSMPRespLen
                      );
#endif
        return;
      }
      tdsaDiscoverRespRcvd(
                           tiRoot,
                           agRoot,
                           agIORequest,
                           oneDeviceData,
                           tdSMPFrameHeader,
                           agFrameHandle
                           );
      break;
    case SMP_REPORT_PHY_SATA:
      TI_DBG3(("itdssSMPCompleted: report phy sata\n"));
      if (agIOInfoLen != sizeof(smpRespReportPhySata_t) + 4 &&
          tdSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
      {
        TI_DBG1(("itdssSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, (unsigned int)sizeof(smpRespReportPhySata_t) + 4));
        tdsaSATADiscoverDone(tiRoot, onePortContext, tiError);
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->osMemHandle,
                       sizeof(tdssSMPRequestBody_t)
                      );
#ifndef DIRECT_SMP
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPReqosMemHandle,
                       tdSMPRequestBody->IndirectSMPReqLen
                      );
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPResposMemHandle,
                       tdSMPRequestBody->IndirectSMPRespLen
                      );
#endif
        return;
      }
      tdsaReportPhySataRcvd(
                            tiRoot,
                            agRoot,
                            agIORequest,
                            oneDeviceData,
                            tdSMPFrameHeader,
                            agFrameHandle
                            );
      break;
    case SMP_CONFIGURE_ROUTING_INFORMATION:
      TI_DBG1(("itdssSMPCompleted: configure routing information\n"));
      if (agIOInfoLen != 4 &&
          tdSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED)
      {
        TI_DBG1(("itdssSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->osMemHandle,
                       sizeof(tdssSMPRequestBody_t)
                      );
#ifndef DIRECT_SMP
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPReqosMemHandle,
                       tdSMPRequestBody->IndirectSMPReqLen
                      );
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPResposMemHandle,
                       tdSMPRequestBody->IndirectSMPRespLen
                      );
#endif
        return;
      }
      tdsaConfigRoutingInfoRespRcvd(
                                    tiRoot,
                                    agRoot,
                                    agIORequest,
                                    oneDeviceData,
                                    tdSMPFrameHeader,
                                    agFrameHandle
                                    );

      break;
    case SMP_PHY_CONTROL:
      TI_DBG3(("itdssSMPCompleted: phy control\n"));
      if (agIOInfoLen != 4 &&
          tdSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED) /*zero length is expected */
      {
        TI_DBG1(("itdssSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->osMemHandle,
                       sizeof(tdssSMPRequestBody_t)
                      );
#ifndef DIRECT_SMP
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPReqosMemHandle,
                       tdSMPRequestBody->IndirectSMPReqLen
                      );
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPResposMemHandle,
                       tdSMPRequestBody->IndirectSMPRespLen
                      );
#endif
        return;
      }
      tdsaPhyControlRespRcvd(
                             tiRoot,
                             agRoot,
                             agIORequest,
                             oneDeviceData,
                             tdSMPFrameHeader,
                             agFrameHandle,
                             CurrentTaskTag
                             );

      break;
#ifdef REMOVED
//temp for testing
     case SMP_REPORT_MANUFACTURE_INFORMATION:
      TI_DBG1(("itdssSMPCompleted: REPORT_MANUFACTURE_INFORMATION\n"));
      if (agIOInfoLen != sizeof(smpRespReportManufactureInfo_t) + 4 &&
          tdSMPFrameHeader->smpFunctionResult == SMP_FUNCTION_ACCEPTED) /*zero length is expected */
      {
        TI_DBG1(("itdssSMPCompleted: mismatch len agIOInfoLen 0x%x 0x%x\n", agIOInfoLen, 4));
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->osMemHandle,
                       sizeof(tdssSMPRequestBody_t)
                      );
#ifndef DIRECT_SMP
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPReqosMemHandle,
                       tdSMPRequestBody->IndirectSMPReqLen
                      );
        ostiFreeMemory(
                       tiRoot,
                       tdSMPRequestBody->IndirectSMPResposMemHandle,
                       tdSMPRequestBody->IndirectSMPRespLen
                      );
#endif
        return;
      }
      tdsaReportManInfoRespRcvd(
                                tiRoot,
                                agRoot,
                                oneDeviceData,
                                tdSMPFrameHeader,
                                agFrameHandle
                                );

       break;
//end temp for testing
#endif
    case SMP_REPORT_ROUTING_INFORMATION:
    case SMP_REPORT_PHY_ERROR_LOG:
    case SMP_PHY_TEST_FUNCTION:
    case SMP_REPORT_MANUFACTURE_INFORMATION:
    case SMP_READ_GPIO_REGISTER:
    case SMP_WRITE_GPIO_REGISTER:
    default:
      TI_DBG1(("itdssSMPCompleted: wrong SMP function 0x%x\n", tdSMPFrameHeader->smpFunction));
      TI_DBG1(("itdssSMPCompleted: smpFrameType 0x%x\n", tdSMPFrameHeader->smpFrameType));
      TI_DBG1(("itdssSMPCompleted: smpFunctionResult 0x%x\n", tdSMPFrameHeader->smpFunctionResult));
      TI_DBG1(("itdssSMPCompleted: smpReserved 0x%x\n", tdSMPFrameHeader->smpReserved));
      tdhexdump("itdssSMPCompleted: SMP payload", (bit8 *)agFrameHandle, agIOInfoLen);
      break;
    }
  }
  else if (agIOStatus == OSSA_IO_ABORTED || agIOStatus == OSSA_IO_INVALID_LENGTH)
  {
    /* no retry this case */
    TI_DBG1(("itdssSMPCompleted: OSSA_IO_ABORTED\n"));
  }
  else if (agIOStatus == OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE)
  {
    TI_DBG1(("itdssSMPCompleted: OSSA_IO_ERROR_INTERNAL_SMP_RESOURCE\n"));
    saFrameReadBlock(agRoot, agFrameHandle, 0, smpHeader, 4);
    tdSMPFrameHeader = (tdssSMPFrameHeader_t *)smpHeader;

    status = saSMPStart(
               agRoot,
               agIORequest,
               tdSMPRequestBody->queueNumber, //tdsaAllShared->SMPQNum, //tdsaRotateQnumber(tiRoot, oneDeviceData),
               agDevHandle,
               AGSA_SMP_INIT_REQ,
               agSASRequestBody,
               &ossaSMPCompleted
               );

    if (status == AGSA_RC_SUCCESS)
    {
      /* increment the number of pending SMP */
      onePortContext->discovery.pendingSMP++;
      if (SMPRequestFunction == SMP_REPORT_GENERAL || SMPRequestFunction == SMP_DISCOVER ||
          SMPRequestFunction == SMP_REPORT_PHY_SATA ||
          SMPRequestFunction == SMP_CONFIGURE_ROUTING_INFORMATION )
      {
        /* start discovery-related SMP timer */
        tdsaDiscoverySMPTimer(tiRoot, onePortContext, (bit32)(tdSMPFrameHeader->smpFunction), tdSMPRequestBody);
      }
      return;
    }
    else if (status == AGSA_RC_BUSY)
    {
      if (tdSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
          tdSMPFrameHeader->smpFunction == SMP_DISCOVER ||
          tdSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
          tdSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION )
      {
        tdsaSMPBusyTimer(tiRoot, onePortContext, oneDeviceData, tdSMPRequestBody);
      }
      else if (tdSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
      {
        /* For taskmanagement SMP, let's fail task management failure */
        tdsaPhyControlFailureRespRcvd(
                                      tiRoot,
                                      agRoot,
                                      oneDeviceData,
                                      tdSMPFrameHeader,
                                      agFrameHandle,
                                      CurrentTaskTag
                                      );
      }
      else
      {
      }
    }
    else /* AGSA_RC_FAILURE */
    {
      if (tdSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
          tdSMPFrameHeader->smpFunction == SMP_DISCOVER ||
          tdSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
          tdSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION )
      {
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
      }
      else if (tdSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
      {
        /* task management failure */
        tdsaPhyControlFailureRespRcvd(
                                      tiRoot,
                                      agRoot,
                                      oneDeviceData,
                                      tdSMPFrameHeader,
                                      agFrameHandle,
                                      CurrentTaskTag
                                      );
      }
      else
      {
      }
    }
  }
  else
  {
    if (tdSMPRequestBody->retries < SMP_RETRIES) /* 5 */
    {
      /* retry the SMP again */
      TI_DBG1(("itdssSMPCompleted: failed! but retries %d agIOStatus 0x%x %d agIOInfoLen %d\n",
               tdSMPRequestBody->retries, agIOStatus, agIOStatus, agIOInfoLen));
      if (agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE ||
          agIOStatus == OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED ||
          agIOStatus == OSSA_IO_DS_NON_OPERATIONAL
         )
      {
        saSetDeviceState(agRoot, agNULL, tdSMPRequestBody->queueNumber, agDevHandle, SA_DS_OPERATIONAL);
      }
      saSMPStart(
                 agRoot,
                 agIORequest,
                 tdSMPRequestBody->queueNumber, //tdsaAllShared->SMPQNum, //tdsaRotateQnumber(tiRoot, oneDeviceData),
                 agDevHandle,
                 AGSA_SMP_INIT_REQ,
                 agSASRequestBody,
                 &ossaSMPCompleted
                 );
      /* increment the number of pending SMP */
      onePortContext->discovery.pendingSMP++;
      tdSMPRequestBody->retries++;
      return;
    }
    else
    {
      tdSMPFrameHeader = (tdssSMPFrameHeader_t *)agSMPFrame->outFrameBuf;
      tdSMPPayload = (bit8 *)agSMPFrame->outFrameBuf + 4;
      TI_DBG1(("itdssSMPCompleted: failed! no more retry! agIOStatus 0x%x %d\n", agIOStatus, agIOStatus));
      if (agIOStatus == OSSA_IO_DS_NON_OPERATIONAL)
      {
        TI_DBG1(("itdssSMPCompleted: failed! agIOStatus is OSSA_IO_DS_NON_OPERATIONAL\n"));
      }

      if (agIOStatus == OSSA_IO_DS_IN_RECOVERY)
      {
        TI_DBG1(("itdssSMPCompleted: failed! agIOStatus is OSSA_IO_DS_IN_RECOVERY\n"));
      }

      if (tdSMPFrameHeader->smpFunction == SMP_REPORT_GENERAL ||
          tdSMPFrameHeader->smpFunction == SMP_DISCOVER ||
          tdSMPFrameHeader->smpFunction == SMP_REPORT_PHY_SATA ||
          tdSMPFrameHeader->smpFunction == SMP_CONFIGURE_ROUTING_INFORMATION
         )
      {
        /* discovery failure */
        TI_DBG1(("itdssSMPCompleted: SMP function 0x%x\n", tdSMPFrameHeader->smpFunction));
        TI_DBG1(("itdssSMPCompleted: discover done with error\n"));
        tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
      }
      else if (tdSMPFrameHeader->smpFunction == SMP_PHY_CONTROL)
      {
        TI_DBG1(("itdssSMPCompleted: SMP_PHY_CONTROL\n"));
        smpPhyControlReq = (smpReqPhyControl_t *)tdSMPPayload;
        if (smpPhyControlReq->phyOperation == SMP_PHY_CONTROL_CLEAR_AFFILIATION)
        {
          TI_DBG1(("itdssSMPCompleted: discover done with error\n"));
          tdsaSASDiscoverDone(tiRoot, onePortContext, tiError);
        }
        else if (smpPhyControlReq->phyOperation == SMP_PHY_CONTROL_HARD_RESET ||
                 smpPhyControlReq->phyOperation == SMP_PHY_CONTROL_LINK_RESET )
        {
          TI_DBG1(("itdssSMPCompleted: device reset failed\n"));
          if (CurrentTaskTag != agNULL )
          {
            TI_DBG1(("itdssSMPCompleted: callback to OS layer with failure\n"));
            ostiInitiatorEvent( tiRoot,
                                NULL,
                                NULL,
                                tiIntrEventTypeTaskManagement,
                                tiTMFailed,
                                CurrentTaskTag );
          }
          else
          {
            /* hard reset was not done with this device */
            oneDeviceData->ResetCnt = 0;
          }
        }
        else
        {
          TI_DBG1(("itdssSMPCompleted: unknown phy operation 0x%x\n", smpPhyControlReq->phyOperation));
        }
      } /* SMP_PHY_CONTROL */
      else
      {
        TI_DBG1(("itdssSMPCompleted: SMP function 0x%x\n", tdSMPFrameHeader->smpFunction));
      }
    } /* else */
  } /* outer else */

  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->osMemHandle,
                 sizeof(tdssSMPRequestBody_t)
                 );
#ifndef DIRECT_SMP
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPReqosMemHandle,
                 tdSMPRequestBody->IndirectSMPReqLen
                 );
  ostiFreeMemory(
                 tiRoot,
                 tdSMPRequestBody->IndirectSMPResposMemHandle,
                 tdSMPRequestBody->IndirectSMPRespLen
                 );
#endif


  return;
}

#else

osGLOBAL void
itdssSMPCompleted (
                   agsaRoot_t            *agRoot,
                   agsaIORequest_t       *agIORequest,
                   bit32                 agIOStatus,
                   bit32                 agIOInfoLen,
                   agsaFrameHandle_t     agFrameHandle
                   )
{
  /* pass the payload to OS layer */
  TI_DBG3(("itdssSMPCompleted: start\n"));
}
#endif


/*****************************************************************************
*! \brief itdIoSuccessHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_SUCCESS
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOSuccessHandler(
                      agsaRoot_t           *agRoot,
                      agsaIORequest_t      *agIORequest,
                      bit32                agIOStatus,
                      bit32                agIOInfoLen,
                      void                 *agParam,
                      bit32                agOtherInfo
                      )
{
  tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
  itdsaIni_t                *Initiator = (itdsaIni_t *)osData->itdsaIni;
  tdIORequestBody_t         *tdIORequestBody;
  agsaSSPResponseInfoUnit_t agSSPRespIU;
  tiSenseData_t             senseData;
  bit8                      senseDataPayload[256];
  bit8                      respData[128];
  bit32                     scsi_status;
  bit32                     senseLen;
  bit32                     respLen;
  bit32                     data_status;
  bit32                     i;
  tiDeviceHandle_t          *tiDeviceHandle = agNULL;
  tdsaDeviceData_t          *oneDeviceData = agNULL;

  TI_DBG2(("itdssIOSuccessHandler: start\n"));
  TI_DBG2(("itdssIOSuccessHandler: agIOInfoLen %d\n", agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  /*
    agIOInfoLen must be >= sizeof(agsaSSPResponseInfoUnit_t), which is minimum
    date length
  */
  if (agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t))
  {
    TI_DBG1(("itdssIOSuccessHandler: First agIOInfoLen does not match!!!\n"));
    TI_DBG1(("itdssIOSuccessHandler: First agIOInfoLen 0x%x IU 0x%x\n", agIOInfoLen, (unsigned int)sizeof(agsaSSPResponseInfoUnit_t)));
    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }
  /* reads agsaSSPResponseInfoUnit_t */
  saFrameReadBlock(agRoot, agParam, 0, &agSSPRespIU, sizeof(agsaSSPResponseInfoUnit_t));

  data_status = SA_SSPRESP_GET_DATAPRES(&agSSPRespIU);
  scsi_status = agSSPRespIU.status;
  /* endianess is invovled here */
  senseLen = SA_SSPRESP_GET_SENSEDATALEN(&agSSPRespIU);
  respLen = SA_SSPRESP_GET_RESPONSEDATALEN(&agSSPRespIU);

  TI_DBG2(("itdssIOSuccessHandler: dataPres=%x\n", data_status));
  TI_DBG2(("itdssIOSuccessHandler: scsi status=0x%x, senselen=0x%x resplen 0x%x\n", scsi_status, senseLen, respLen));

  /*
    sanity check: do not go beyond of agIOInfoLen. if happens, return error
    agIOInfoLen >= sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen -> OK
    because frame must be divisible by 4, so there can be extra padding
    agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen -> NOT OK
  */
  if (agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen)
  {
    TI_DBG1(("itdssIOSuccessHandler: Second agIOInfoLen does not match!!!\n"));
    TI_DBG1(("itdssIOSuccessHandler: Second agIOInfoLen 0x%x IU 0x%x senselen 0x%x resplen 0x%x\n", agIOInfoLen, (unsigned int)sizeof(agsaSSPResponseInfoUnit_t), senseLen, respLen));

    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }

  /* reads response data */
  saFrameReadBlock(agRoot, agParam,
                   sizeof(agsaSSPResponseInfoUnit_t),
                   respData, respLen);
  /* reads sense data */
  saFrameReadBlock(agRoot, agParam,
                   sizeof(agsaSSPResponseInfoUnit_t)
                   + respLen,
                   senseDataPayload, senseLen);

  if (data_status == 0)
  {
    /* NO_DATA */
    TI_DBG1(("itdssIOSuccessHandler: no data scsi_status 0x%x\n",scsi_status));

    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOSuccess,
                             scsi_status,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );

    return;
  }

  if (data_status == 1)
  {
    /* RESPONSE_DATA */
    TI_DBG1(("itdssIOSuccessHandler: response data \n"));

    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOSuccess,
                             0,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }

  if (data_status == 2)
  {
    /* SENSE_DATA */
    TI_DBG2(("itdssIOSuccessHandler: sense data \n"));

    senseData.senseData = &senseDataPayload;
    senseData.senseLen = MIN(256, senseLen);
    /* debugging */
    tdhexdump("ResponseIU I", (bit8 *)&agSSPRespIU, sizeof(agsaSSPResponseInfoUnit_t));

    tiDeviceHandle = tdIORequestBody->tiDevHandle;
    oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
    TI_DBG1(("sense data Sense Key 0x%2X ASC(Code) 0x%2X ASCQ(Qualifier) 0x%2X, did 0x%x\n",*(senseDataPayload+ 2),*(senseDataPayload + 12),*(senseDataPayload + 13),
             oneDeviceData->id));
    tdhexdump("sense data I", (bit8 *)senseDataPayload, senseLen);
//    tdhexdump("sense data II", (bit8 *)senseData.senseData, senseData.senseLen);

    if (senseDataPayload[2] == SCSI_SENSE_KEY_RECOVERED_ERROR)
    {
      Initiator->SenseKeyCounter.SoftError ++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_NOT_READY)
    {
      Initiator->SenseKeyCounter.MediumNotReady++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_MEDIUM_ERROR)
    {
      Initiator->SenseKeyCounter.MediumError++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_HARDWARE_ERROR)
    {
      Initiator->SenseKeyCounter.HardwareError++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_ILLEGAL_REQUEST)
    {
      Initiator->SenseKeyCounter.IllegalRequest++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_UNIT_ATTENTION)
    {
      Initiator->SenseKeyCounter.UnitAttention++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_ABORTED_COMMAND)
    {
      Initiator->SenseKeyCounter.AbortCommand++;
    }
    else
    {
      Initiator->SenseKeyCounter.OtherKeyType++;
    }

    /* when ASQ and ASCQ 0x04 0x11, does saLocalPhyControl for notify spinup */
    if ((senseDataPayload[12] == 0x04 && senseDataPayload[13] == 0x11))
    {
      TI_DBG2(("itdssIOSuccessHandler: sending notfify spinup\n"));
      tiDeviceHandle = tdIORequestBody->tiDevHandle;
      oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        for (i=0;i<TD_MAX_NUM_NOTIFY_SPINUP;i++)
        {
          saLocalPhyControl(agRoot, agNULL, 0, oneDeviceData->phyID, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
        }
      }
    }
    ostiInitiatorIOCompleted(
                             tiRoot,
                             /* tiIORequest */
                             tdIORequestBody->tiIORequest,
                             tiIOSuccess,
                             scsi_status,
                             &senseData,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }
  if (data_status == 3)
  {
    /* RESERVED */
    TI_DBG1(("itdssIOSuccessHandler: reserved wrong!!!\n"));
    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOFailed,
                             scsi_status,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }

}

/*****************************************************************************
*! \brief itdssIOAbortedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_ABORTED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
/* see itdosIOCompleted() and itdinit.c and  itdIoAbortedHandler in itdio.c*/
osGLOBAL void
itdssIOAbortedHandler (
                       agsaRoot_t              *agRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  tiDeviceHandle_t       *tiDeviceHandle = agNULL;
  tdsaDeviceData_t       *oneDeviceData = agNULL;

  TI_DBG2(("itdssIOAbortedHandler: start\n"));
  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  if (agIOStatus != OSSA_IO_ABORTED)
  {
    TI_DBG1(("itdssIOAbortedHandler: incorrect agIOStatus 0x%x\n", agIOStatus));
  }

  if (tdIORequestBody == agNULL)
  {
    TI_DBG1(("itdssIOAbortedHandler: start\n"));
    return;
  }

  if (tdIORequestBody != agNULL)
  {
    tiDeviceHandle = tdIORequestBody->tiDevHandle;
  }
  if (tiDeviceHandle != agNULL)
  {
    oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  }
  if (oneDeviceData != agNULL)
  {
    TI_DBG2(("itdssIOAbortedHandler: did %d \n", oneDeviceData->id));
  }
  else
  {
    TI_DBG1(("itdssIOAbortedHandler: oneDeviceData is NULL\n"));
  }


  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailAborted,
                            agNULL,
                            intContext
                            );

  return;
}

#ifdef REMOVED
/*****************************************************************************
*! \brief itdssIOOverFlowHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OVERFLOW
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOOverFlowHandler(
                       agsaRoot_t              *agRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;

  TI_DBG2(("itdssIOOverFlowHandler: start\n"));
  TI_DBG2(("itdssIOOverFlowHandler: not transferred byte 0x%x\n", agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOOverRun,
                            agIOInfoLen,
                            agNULL,
                            intContext
                            );

  return;
}
#endif


/*****************************************************************************
*! \brief itdssIOUnderFlowHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_UNDERFLOW
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOUnderFlowHandler(
                        agsaRoot_t              *agRoot,
                        agsaIORequest_t         *agIORequest,
                        bit32                   agIOStatus,
                        bit32                   agIOInfoLen,
                        void                    *agParam,
                        bit32                   agOtherInfo
                        )
{
  tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                     intContext = osData->IntContext;
  tdIORequestBody_t         *tdIORequestBody;

  TI_DBG6(("itdssIOUnderFlowHandler: start\n"));
  TI_DBG6(("itdssIOUnderFlowHandler: agIOInfoLen 0x%x\n", agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOUnderRun,
                            agIOInfoLen,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssIOFailedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_FAILED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOFailedHandler(
                     agsaRoot_t              *agRoot,
                     agsaIORequest_t         *agIORequest,
                     bit32                   agIOStatus,
                     bit32                   agIOInfoLen,
                     void                    *agParam,
                     bit32                   agOtherInfo
                     )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;

  TI_DBG1(("itdssIOFailedHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssIOAbortResetHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_ABORT_RESET
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOAbortResetHandler(
                         agsaRoot_t              *agRoot,
                         agsaIORequest_t         *agIORequest,
                         bit32                   agIOStatus,
                         bit32                   agIOInfoLen,
                         void                    *agParam,
                         bit32                   agOtherInfo
                         )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIOAbortResetHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailAbortReset,
                            agNULL,
                            intContext
                            );


  return;
}

/*****************************************************************************
*! \brief itdssIONotValidHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_NOT_VALID
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIONotValidHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIONotValidHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailNotValid,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssIONoDeviceHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_NO_DEVICE
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIONoDeviceHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIONoDeviceHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailNoLogin,
                            agNULL,
                            intContext
                            );
  return;
}

#ifdef REMOVED /* to do: removed from spec */
/*****************************************************************************
*! \brief itdssIllegalParameterHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_ILLEGAL_PARAMETER
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIllegalParameterHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIllegalParameterHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}
#endif

/*****************************************************************************
*! \brief itdssLinkFailureHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_LINK_FAILURE
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssLinkFailureHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssLinkFailureHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssProgErrorHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_PROG_ERROR
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssProgErrorHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssProgErrorHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorBreakHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_BREAK
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorBreakHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorBreakHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorPhyNotReadyHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_PHY_NOT_READY
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorPhyNotReadyHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorPhyNotReadyHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorProtocolNotSupprotedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_PROTOCOL_NOT_SUPPORTED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorProtocolNotSupprotedHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorProtocolNotSupprotedHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorZoneViolationHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_ZONE_VIOLATION
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorZoneViolationHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorZoneViolationHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorBreakHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_BREAK
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorBreakHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssOpenCnxErrorBreakHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorITNexusLossHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorITNexusLossHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssOpenCnxErrorITNexusLossHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorBadDestinationHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_BAD_DESTINATION
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorBadDestinationHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssOpenCnxErrorBadDestinationHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorConnectionRateNotSupportedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_CONNECTION_RATE_NOT_SUPPORTED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorConnectionRateNotSupportedHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t             *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t          *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  agsaDevHandle_t        *agDevHandle = agNULL;
  tiDeviceHandle_t       *tiDeviceHandle = agNULL;
  tdsaDeviceData_t       *oneDeviceData = agNULL;
  bit32                  ConnRate = SAS_CONNECTION_RATE_12_0G;
  agsaContext_t          *agContext = agNULL;
  TI_DBG1(("itdssOpenCnxErrorConnectionRateNotSupportedHandler: start\n"));

  /* we retry by lowering link rate
     retry should be in ossaSetDeviceInfoCB()
  */
  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  tiDeviceHandle = tdIORequestBody->tiDevHandle;
  oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  agDevHandle = oneDeviceData->agDevHandle;

  if (tdsaAllShared->RateAdjust)
  {
    if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
        oneDeviceData->tdPortContext != agNULL )
    {
      ConnRate = DEVINFO_GET_LINKRATE(&oneDeviceData->agDeviceInfo);
      if (ConnRate == SAS_CONNECTION_RATE_1_5G)
      {
        /* no retry; completes IO */
        ostiInitiatorIOCompleted(
                                 tiRoot,
                                 tdIORequestBody->tiIORequest,
                                 tiIOFailed,
                                 tiDetailOtherError,
                                 agNULL,
                                 intContext
                                 );
      }
      else
      {
        ConnRate = ConnRate - 1;
      }
      agContext = &(tdIORequestBody->agContext);
      agContext->osData = agIORequest;
      saSetDeviceInfo(agRoot, agContext, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, 32, ConnRate << 28, ossaIniSetDeviceInfoCB);
    }
  }
  else
  {
    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest,
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             intContext
                             );
  }

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorSTPResourceBusyHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_STP_RESOURCES_BUSY
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorSTPResourceBusyHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorSTPResourceBusyHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorWrongDestinationHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_WRONG_DESTINATION
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorWrongDestinationHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssOpenCnxErrorWrongDestinationHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorUnknownErrorHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_UNKNOWN_ERROR
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorUnknownErrorHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssOpenCnxErrorUnknownErrorHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorNAKReceivedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_NAK_RECEIVED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorNAKReceivedHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorNAKReceivedHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorACKNAKTimeoutHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_ACK_NAK_TIMEOUT
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorACKNAKTimeoutHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorACKNAKTimeoutHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorPeerAbortedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_PEER_ABORTED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorPeerAbortedHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorPeerAbortedHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorRxFrameHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_RX_FRAME
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorRxFrameHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorRxFrameHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorDMAHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_DMA
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorDMAHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorDMAHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherErrorNoRetry,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorCreditTimeoutHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_CREDIT_TIMEOUT
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorCreditTimeoutHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorCreditTimeoutHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorCMDIssueACKNAKTimeoutHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_CMD_ISSUE_ACK_NAK_TIMEOUT
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorCMDIssueACKNAKTimeoutHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorCMDIssueACKNAKTimeoutHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorCMDIssueBreakBeforeACKNAKHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_CMD_ISSUE_BREAK_BEFORE_ACK_NAK
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorCMDIssueBreakBeforeACKNAKHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorCMDIssueBreakBeforeACKNAKHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorCMDIssuePhyDownBeforeACKNAKHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_CMD_ISSUE_PHY_DOWN_BEFORE_ACK_NAK
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorCMDIssuePhyDownBeforeACKNAKHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorCMDIssuePhyDownBeforeACKNAKHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorDisruptedPhyDownHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_DISRUPTED_PHY_DOWN
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorDisruptedPhyDownHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorDisruptedPhyDownHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorOffsetMismatchHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_OFFSET_MISMATCH
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorOffsetMismatchHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssXferErrorOffsetMismatchHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorXferZeroDataLenHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_XFER_ZERO_DATA_LEN
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorXferZeroDataLenHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorXferZeroDataLenHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferOpenRetryTimeoutHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_OPEN_RETRY_TIMEOUT
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferOpenRetryTimeoutHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdsaRoot_t             *tdsaRoot      = (tdsaRoot_t *) tiRoot->tdData;
  tdsaContext_t          *tdsaAllShared = (tdsaContext_t *)&tdsaRoot->tdsaAllShared;
  itdsaIni_t             *Initiator = (itdsaIni_t *)tdsaAllShared->itdsaIni;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  agsaDevHandle_t        *agDevHandle = agNULL;
  tiDeviceHandle_t       *tiDeviceHandle = agNULL;
  tdsaDeviceData_t       *oneDeviceData = agNULL;
  bit32                  saStatus = AGSA_RC_FAILURE;

  TI_DBG2(("itdssXferOpenRetryTimeoutHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
  tiDeviceHandle = tdIORequestBody->tiDevHandle;
  oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  agDevHandle = oneDeviceData->agDevHandle;

  if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
      oneDeviceData->tdPortContext != agNULL )
  {
    if (tdIORequestBody->reTries < OPEN_RETRY_RETRIES) /* 10 */
    {
      saStatus = saSSPStart(agRoot,
                            agIORequest,
                            tdsaRotateQnumber(tiRoot, oneDeviceData),
                            agDevHandle,
                            tdIORequestBody->agRequestType,
                            &(tdIORequestBody->transport.SAS.agSASRequestBody),
                            agNULL,
                            &ossaSSPCompleted);

      if (saStatus == AGSA_RC_SUCCESS)
      {
        TI_DBG2(("itdssXferOpenRetryTimeoutHandler: retried\n"));
        Initiator->NumIOsActive++;
        tdIORequestBody->ioStarted = agTRUE;
        tdIORequestBody->ioCompleted = agFALSE;
        tdIORequestBody->reTries++;
        return;
      }
      else
      {
        TI_DBG1(("itdssXferOpenRetryTimeoutHandler: retry failed\n"));
        tdIORequestBody->ioStarted = agFALSE;
        tdIORequestBody->ioCompleted = agTRUE;
        tdIORequestBody->reTries = 0;
      }
    }
    else
    {
      TI_DBG1(("itdssXferOpenRetryTimeoutHandler: retry is over and fail\n"));
      tdIORequestBody->reTries = 0;
    }
  }
  else
  {
    TI_DBG1(("itdssXferOpenRetryTimeoutHandler: not valid deivce no retry\n"));
    tdIORequestBody->reTries = 0;
  }
  ostiInitiatorIOCompleted(
                           tiRoot,
                           tdIORequestBody->tiIORequest,
                           tiIOFailed,
                           tiDetailOtherError,
                           agNULL,
                           intContext
                           );
  return;
}

/*****************************************************************************
*! \brief itdssPortInResetHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_PORT_IN_RESET
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssPortInResetHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssPortInResetHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssDsNonOperationalHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_DS_NON_OPERATIONAL
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssDsNonOperationalHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  agsaDevHandle_t        *agDevHandle = agNULL;
  tiDeviceHandle_t       *tiDeviceHandle = agNULL;
  tdsaDeviceData_t       *oneDeviceData = agNULL;


  TI_DBG2(("itdssDsNonOperationalHandler: start\n"));


  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

#if 1 /* TBD */
  /* let's do it only once ????? */
  tiDeviceHandle = tdIORequestBody->tiDevHandle;
  oneDeviceData  = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
  agDevHandle = oneDeviceData->agDevHandle;
  if (oneDeviceData->valid == agTRUE && oneDeviceData->registered == agTRUE &&
      oneDeviceData->tdPortContext != agNULL )
  {
    saSetDeviceState(agRoot, agNULL, tdsaRotateQnumber(tiRoot, oneDeviceData), agDevHandle, SA_DS_OPERATIONAL);
  }
#endif

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssDsInRecoveryHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_DS_IN_RECOVERY
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssDsInRecoveryHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssDsInRecoveryHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssTmTagNotFoundHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_TM_TAG_NOT_FOUND
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssTmTagNotFoundHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssTmTagNotFoundHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssSSPExtIUZeroLenHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_SSP_EXT_IU_ZERO_LEN_ERROR
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssSSPExtIUZeroLenHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssSSPExtIUZeroLenHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssXferErrorUnexpectedPhaseHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERROR_UNEXPECTED_PHASE
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorUnexpectedPhaseHandler(
                       agsaRoot_t           *agRoot,
                       agsaIORequest_t      *agIORequest,
                       bit32                agIOStatus,
                       bit32                agIOInfoLen,
                       void                 *agParam,
                       bit32                agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorUnexpectedPhaseHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

#ifdef REMOVED
/*****************************************************************************
*! \brief itdssIOUnderFlowWithChkConditionHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_UNDERFLOW_WITH_CHK_COND
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \return: None
*
*
*****************************************************************************/
/*
  How to report SCSI_STAT_CHECK_CONDITION and tiIOUnderRun simultaneoulsy???
  ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest,
                             tiIOSuccess,
                             SCSI_STAT_CHECK_CONDITION,
                             &senseData,
                             agTRUE
                             );

                 vs

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOUnderRun,
                            agIOInfoLen,
                            agNULL,
                            intContext
                            );

  For now, SCSI_STAT_CHECK_CONDITION is reported until TISA changes (as of 1/6/09)
  In other words, this handler is the practically same as itdssIOSuccessHandler()
*/
osGLOBAL void
itdssIOUnderFlowWithChkConditionHandler(
                        agsaRoot_t              *agRoot,
                        agsaIORequest_t         *agIORequest,
                        bit32                   agIOStatus,
                        bit32                   agIOInfoLen,
                        void                    *agParam
                        )
{
  tdsaRootOsData_t          *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t                  *tiRoot = (tiRoot_t *)osData->tiRoot;
  tdIORequestBody_t         *tdIORequestBody;
  agsaSSPResponseInfoUnit_t agSSPRespIU;
  tiSenseData_t             senseData;
  bit8                      senseDataPayload[256];
  bit8                      respData[128];
  bit32                     scsi_status;
  bit32                     senseLen;
  bit32                     respLen;
  bit32                     data_status;
  bit32                     i;
  tiDeviceHandle_t          *tiDeviceHandle = agNULL;
  tdsaDeviceData_t          *oneDeviceData = agNULL;

  TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: start\n"));
  TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: agIOInfoLen 0x%x\n", agIOInfoLen));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  tdIORequestBody->ioCompleted = agTRUE;
  tdIORequestBody->ioStarted = agFALSE;

  /*
    agIOInfoLen must be >= sizeof(agsaSSPResponseInfoUnit_t), which is minimum
    date length
  */
  if (agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t))
  {
    TI_DBG1(("itdssIOUnderFlowWithChkConditionHandler: First agIOInfoLen does not match!!!\n"));
    TI_DBG1(("itdssIOUnderFlowWithChkConditionHandler: First agIOInfoLen 0x%x IU 0x%x\n", agIOInfoLen, (unsigned int)sizeof(agsaSSPResponseInfoUnit_t)));
    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }
  /* reads agsaSSPResponseInfoUnit_t */
  saFrameReadBlock(agRoot, agParam, 0, &agSSPRespIU, sizeof(agsaSSPResponseInfoUnit_t));

  data_status = SA_SSPRESP_GET_DATAPRES(&agSSPRespIU);
  scsi_status = agSSPRespIU.status;
  /* endianess is invovled here */
  senseLen = SA_SSPRESP_GET_SENSEDATALEN(&agSSPRespIU);
  respLen = SA_SSPRESP_GET_RESPONSEDATALEN(&agSSPRespIU);

  TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: dataPres=%x\n", data_status));
  TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: scsi status=0x%x, senselen=0x%x resplen 0x%x\n", scsi_status, senseLen, respLen));

  /*
    sanity check: do not go beyond of agIOInfoLen. if happens, return error
    agIOInfoLen >= sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen -> OK
    because frame must be divisible by 4, so there can be extra padding
    agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen -> NOT OK
  */
  if (agIOInfoLen < sizeof(agsaSSPResponseInfoUnit_t) + senseLen + respLen)
  {
    TI_DBG1(("itdssIOUnderFlowWithChkConditionHandler: Second agIOInfoLen does not match!!!\n"));
    TI_DBG1(("itdssIOUnderFlowWithChkConditionHandler: Second agIOInfoLen 0x%x IU 0x%x senselen 0x%x resplen 0x%x\n", agIOInfoLen, (unsigned int)sizeof(agsaSSPResponseInfoUnit_t), senseLen, respLen));

    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOFailed,
                             tiDetailOtherError,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }

  /* reads response data */
  saFrameReadBlock(agRoot, agParam,
                   sizeof(agsaSSPResponseInfoUnit_t),
                   respData, respLen);
  /* reads sense data */
  saFrameReadBlock(agRoot, agParam,
                   sizeof(agsaSSPResponseInfoUnit_t)
                   + respLen,
                   senseDataPayload, senseLen);

  if (data_status == 0)
  {
    /* NO_DATA */
    TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: no data\n"));

    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOSuccess,
                             scsi_status,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );

    return;
  }

  if (data_status == 1)
  {
    /* RESPONSE_DATA */
    TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: response data \n"));

    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOSuccess,
                             0,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }

  if (data_status == 2)
  {
    /* SENSE_DATA */
    TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: sense data \n"));

    senseData.senseData = &senseDataPayload;
    senseData.senseLen = MIN(256, senseLen);
    /* debugging */
    tdhexdump("ResponseIU I", (bit8 *)&agSSPRespIU, sizeof(agsaSSPResponseInfoUnit_t));

    tdhexdump("sense data I", (bit8 *)senseDataPayload, senseLen);
    tdhexdump("sense data II", (bit8 *)senseData.senseData, senseData.senseLen);

    if (senseDataPayload[2] == SCSI_SENSE_KEY_RECOVERED_ERROR)
    {
      Initiator->SenseKeyCounter.SoftError ++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_NOT_READY)
    {
      Initiator->SenseKeyCounter.MediumNotReady++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_MEDIUM_ERROR)
    {
      Initiator->SenseKeyCounter.MediumError++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_HARDWARE_ERROR)
    {
      Initiator->SenseKeyCounter.HardwareError++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_ILLEGAL_REQUEST)
    {
      Initiator->SenseKeyCounter.IllegalRequest++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_UNIT_ATTENTION)
    {
      Initiator->SenseKeyCounter.UnitAttention++;
    }
    else if (senseDataPayload[2] == SCSI_SENSE_KEY_ABORTED_COMMAND)
    {
      Initiator->SenseKeyCounter.AbortCommand++;
    }
    else
    {
      Initiator->SenseKeyCounter.OtherKeyType++;
    }

    /* when ASQ and ASCQ 0x04 0x11, does saLocalPhyControl for notify spinup */
    if ((senseDataPayload[12] == 0x04 && senseDataPayload[13] == 0x11))
    {
      TI_DBG2(("itdssIOUnderFlowWithChkConditionHandler: sending notfify spinup\n"));
      tiDeviceHandle = tdIORequestBody->tiDevHandle;
      oneDeviceData = (tdsaDeviceData_t *)tiDeviceHandle->tdData;
      if (oneDeviceData->directlyAttached == agTRUE)
      {
        for (i=0;i<TD_MAX_NUM_NOTIFY_SPINUP;i++)
        {
          saLocalPhyControl(agRoot, agNULL, 0, oneDeviceData->phyID, AGSA_PHY_NOTIFY_ENABLE_SPINUP, agNULL);
        }
      }
    }
    ostiInitiatorIOCompleted(
                             tiRoot,
                             /* tiIORequest */
                             tdIORequestBody->tiIORequest,
                             tiIOSuccess,
                             scsi_status,
                             &senseData,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }
  if (data_status == 3)
  {
    /* RESERVED */
    TI_DBG1(("itdssIOUnderFlowWithChkConditionHandler: reserved wrong!!!\n"));
    ostiInitiatorIOCompleted(
                             tiRoot,
                             tdIORequestBody->tiIORequest, /* tiIORequest */
                             tiIOFailed,
                             scsi_status,
                             agNULL,
                             agTRUE /* intContext; is not being used */
                             );
    return;
  }


  return;
}
#endif

/*****************************************************************************
*! \brief itdssXferOpenRetryBackoffThresholdReachedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus =
*            OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_RETRY_BACKOFF_THRESHOLD_REACHED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferOpenRetryBackoffThresholdReachedHandler(
                                                 agsaRoot_t           *agRoot,
                                                 agsaIORequest_t      *agIORequest,
                                                 bit32                agIOStatus,
                                                 bit32                agIOInfoLen,
                                                 void                 *agParam,
                                                 bit32                agOtherInfo
                                                )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferOpenRetryBackoffThresholdReachedHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorItNexusLossOpenTmoHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_TMO
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorItNexusLossOpenTmoHandler(
                                           agsaRoot_t           *agRoot,
                                           agsaIORequest_t      *agIORequest,
                                           bit32                agIOStatus,
                                           bit32                agIOInfoLen,
                                           void                 *agParam,
                                           bit32                agOtherInfo
                                          )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorItNexusLossOpenTmoHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorItNexusLossNoDestHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_NO_DEST
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorItNexusLossNoDestHandler(
                                          agsaRoot_t           *agRoot,
                                          agsaIORequest_t      *agIORequest,
                                          bit32                agIOStatus,
                                          bit32                agIOInfoLen,
                                          void                 *agParam,
                                          bit32                agOtherInfo
                                         )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorItNexusLossNoDestHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorItNexusLossOpenCollideHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_OPEN_COLLIDE
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorItNexusLossOpenCollideHandler(
                                               agsaRoot_t           *agRoot,
                                               agsaIORequest_t      *agIORequest,
                                               bit32                agIOStatus,
                                               bit32                agIOInfoLen,
                                               void                 *agParam,
                                               bit32                agOtherInfo
                                              )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorItNexusLossOpenCollideHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorItNexusLossOpenPathwayBlockedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_IT_NEXUS_LOSS_PATHWAY_BLOCKED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorItNexusLossOpenPathwayBlockedHandler(
                                                      agsaRoot_t           *agRoot,
                                                      agsaIORequest_t      *agIORequest,
                                                      bit32                agIOStatus,
                                                      bit32                agIOInfoLen,
                                                      void                 *agParam,
                                                      bit32                agOtherInfo
                                                     )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorItNexusLossOpenPathwayBlockedHandler: start\n"));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );

  return;
}

/*****************************************************************************
*! \brief itdssEncryptionHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS lower
*            layer with any encryption specific agIOStatus.
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssEncryptionHandler (
                       agsaRoot_t              *agRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  bit32                  errorDetail = tiDetailOtherError;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG1(("itdssEncryptionHandler: start\n"));
  TI_DBG1(("itdssEncryptionHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  switch (agIOStatus)
  {
  case OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS:
      TI_DBG1(("itdssEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_KEY_CACHE_MISS\n"));
      errorDetail = tiDetailDekKeyCacheMiss;
      break;
  case OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID:
      TI_DBG1(("itdssEncryptionHandler: OSSA_IO_XFR_ERROR_CIPHER_MODE_INVALID\n"));
      errorDetail = tiDetailCipherModeInvalid;
      break;
  case OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH:
      TI_DBG1(("itdssEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_IV_MISMATCH\n"));
      errorDetail = tiDetailDekIVMismatch;
      break;
  case OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR:
      TI_DBG1(("itdssEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_RAM_INTERFACE_ERROR\n"));
      errorDetail = tiDetailDekRamInterfaceError;
      break;
  case OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS:
      TI_DBG1(("itdssEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_INDEX_OUT_OF_BOUNDS\n"));
      errorDetail = tiDetailDekIndexOutofBounds;
      break;
  case OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE:
      TI_DBG1(("itdssEncryptionHandler: OSSA_IO_XFR_ERROR_DEK_ILLEGAL_TABLE\n"));
      errorDetail = tiDetailOtherError;
      break;
  default:
      TI_DBG1(("itdssEncryptionHandler: other error!!! 0x%x\n", agIOStatus));
      errorDetail = tiDetailOtherError;
      break;
  }

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOEncryptError,
                            errorDetail,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssDifHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with any DIF specific agIOStatus
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssDifHandler(
                agsaRoot_t           *agRoot,
                agsaIORequest_t      *agIORequest,
                bit32                agIOStatus,
                bit32                agIOInfoLen,
                void                 *agParam,
                bit32                agOtherInfo
               )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  bit32                  errorDetail = tiDetailOtherError;
  tdIORequestBody_t      *tdIORequestBody;
#ifdef  TD_DEBUG_ENABLE
  agsaDifDetails_t       *DifDetail;
#endif

  TI_DBG1(("itdssDifHandler: start\n"));
  TI_DBG1(("itdssDifHandler: agIOStatus 0x%x\n", agIOStatus));
  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;
#ifdef  TD_DEBUG_ENABLE
  DifDetail = (agsaDifDetails_t *)agParam;
#endif
  switch (agIOStatus)
  {
  case OSSA_IO_XFR_ERROR_DIF_MISMATCH:
      errorDetail = tiDetailDifMismatch;
      TI_DBG1(("itdssDifHandler: OSSA_IO_XFR_ERROR_DIF_MISMATCH\n"));
      break;
  case OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH:
      errorDetail = tiDetailDifAppTagMismatch;
      TI_DBG1(("itdssDifHandler: OSSA_IO_XFR_ERROR_DIF_APPLICATION_TAG_MISMATCH\n"));
      break;
  case OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH:
      errorDetail = tiDetailDifRefTagMismatch;
      TI_DBG1(("itdssDifHandler: OSSA_IO_XFR_ERROR_DIF_REFERENCE_TAG_MISMATCH\n"));
      break;
  case OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH:
      errorDetail = tiDetailDifCrcMismatch;
      TI_DBG1(("itdssDifHandler: OSSA_IO_XFR_ERROR_DIF_CRC_MISMATCH\n"));
      break;
  default:
      errorDetail = tiDetailOtherError;
      TI_DBG1(("itdssDifHandler: other error!!! 0x%x\n", agIOStatus));
      break;
  }
  TI_DBG1(("itdssDifHandler: DIF detail UpperLBA 0x%08x LowerLBA 0x%08x\n", DifDetail->UpperLBA, DifDetail->LowerLBA));
  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIODifError,
                            errorDetail,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssIOResourceUnavailableHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_MPI_ERR_IO_RESOURCE_UNAVAILABLE
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOResourceUnavailableHandler(
                                  agsaRoot_t              *agRoot,
                                  agsaIORequest_t         *agIORequest,
                                  bit32                   agIOStatus,
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIOResourceUnavailableHandler: start\n"));
  TI_DBG2(("itdssIOResourceUnavailableHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailBusy,
                            agNULL,
                            intContext
                            );
  return;
}
/*****************************************************************************
*! \brief itdssIORQEBusyFullHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_MPI_IO_RQE_BUSY_FULL
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIORQEBusyFullHandler(
                                  agsaRoot_t              *agRoot,
                                  agsaIORequest_t         *agIORequest,
                                  bit32                   agIOStatus,
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIORQEBusyFullHandler: start\n"));
  TI_DBG2(("itdssIORQEBusyFullHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailBusy,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssXferErrorInvalidSSPRspFrameHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFR_ERROR_INVALID_SSP_RSP_FRAME
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorInvalidSSPRspFrameHandler(
                                  agsaRoot_t              *agRoot,
                                  agsaIORequest_t         *agIORequest,
                                  bit32                   agIOStatus,
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorInvalidSSPRspFrameHandler: start\n"));
  TI_DBG2(("itdssXferErrorInvalidSSPRspFrameHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssXferErrorEOBDataOverrunHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_XFER_ERR_EOB_DATA_OVERRUN
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssXferErrorEOBDataOverrunHandler(
                                  agsaRoot_t              *agRoot,
                                  agsaIORequest_t         *agIORequest,
                                  bit32                   agIOStatus,
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssXferErrorEOBDataOverrunHandler: start\n"));
  TI_DBG2(("itdssXferErrorEOBDataOverrunHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssOpenCnxErrorOpenPreemptedHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = OSSA_IO_OPEN_CNX_ERROR_OPEN_PREEMPTED
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssOpenCnxErrorOpenPreemptedHandler(
                                  agsaRoot_t              *agRoot,
                                  agsaIORequest_t         *agIORequest,
                                  bit32                   agIOStatus,
                                  bit32                   agIOInfoLen,
                                  void                    *agParam,
                                  bit32                   agOtherInfo
                                 )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssOpenCnxErrorOpenPreemptedHandler: start\n"));
  TI_DBG2(("itdssOpenCnxErrorOpenPreemptedHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );
  return;
}

/* default */
/*****************************************************************************
*! \brief itdssIODefaultHandler
*
*  Purpose:  This function processes I/Os completed and returned by SAS/SATA lower
*            layer with agIOStatus = unspecified
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIODefaultHandler (
                       agsaRoot_t              *agRoot,
                       agsaIORequest_t         *agIORequest,
                       bit32                   agIOStatus,
                       bit32                   agIOInfoLen,
                       void                    *agParam,
                       bit32                   agOtherInfo
                       )
{
  tdsaRootOsData_t       *osData = (tdsaRootOsData_t *)agRoot->osData;
  tiRoot_t               *tiRoot = (tiRoot_t *)osData->tiRoot;
  bit32                  intContext = osData->IntContext;
  tdIORequestBody_t      *tdIORequestBody;
  TI_DBG2(("itdssIODefaultHandler: start\n"));
  TI_DBG2(("itdssIODefaultHandler: agIOStatus 0x%x\n", agIOStatus));

  tdIORequestBody = (tdIORequestBody_t *)agIORequest->osData;

  ostiInitiatorIOCompleted (
                            tiRoot,
                            tdIORequestBody->tiIORequest,
                            tiIOFailed,
                            tiDetailOtherError,
                            agNULL,
                            intContext
                            );
  return;
}

/*****************************************************************************
*! \brief itdssIOForDebugging1Completed
*
*  Purpose:  This function is only for debugging. This function should NOT be
*            called.
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOForDebugging1Completed(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus,
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 )
{
  TI_DBG1(("itdssIOForDebugging1Completed: start, error!!! can't be called. \n"));
}

/*****************************************************************************
*! \brief itdssIOForDebugging2Completed
*
*  Purpose:  This function is only for debugging. This function should NOT be
*            called.
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOForDebugging2Completed(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus,
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 )
{
  TI_DBG1(("itdssIOForDebugging2Completed: start, error!!! can't be called.  \n"));
}

/*****************************************************************************
*! \brief itdssIOForDebugging3Completed
*
*  Purpose:  This function is only for debugging. This function should NOT be
*            called.
*
*  \param  agRoot:            pointer to port instance
*  \param  agIORequest:       pointer to I/O request
*  \param  agIOStatus:        I/O status given by LL layer
*  \param  agIOInfoLen:       lenth of complete SAS RESP frame
*  \param  agParam            A Handle used to refer to the response frame or handle
*                             of abort request
*  \param  agOtherInfo        Residual count
*  \return: None
*
*
*****************************************************************************/
osGLOBAL void
itdssIOForDebugging3Completed(
                 agsaRoot_t             *agRoot,
                 agsaIORequest_t        *agIORequest,
                 bit32                  agIOStatus,
                 bit32                  agIOInfoLen,
                 void                   *agParam,
                 bit32                  agOtherInfo
                 )
{
  TI_DBG1(("itdssIOForDebugging3Completed: start, error!!! can't be called.  \n"));
}


