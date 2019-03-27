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
*
* $FreeBSD$
*
*******************************************************************************/
/******************************************************************************
PMC-Sierra TISA Initiator Device Driver for Linux 2.x.x.

Module Name:  
  osapi.c
Abstract:  
  Linux iSCSI/FC Initiator driver module itsdk required OS functions
Environment:  
  Part of oslayer module, Kernel or loadable module  

*******************************************************************************
ostiInitiatorEvent()

Purpose:
  TI layer call back to OSlayer to inform events 
Parameters: 
  tiRoot_t *ptiRoot (IN)               Pointer to HBA data structure  
  tiDeviceHandle_t *ptiDevHandle (IN)  Pointer to device handle
  tiIntrEvenType_t evenType (IN)       Event type
  tiIntrEventStatus_t evetStatus (IN)  Event status
  void *parm (IN)                      pointer to even specific data
Return:
Note:    
  TBD, further event process required.
******************************************************************************/
void ostiInitiatorEvent( tiRoot_t *ptiRoot,
                         tiPortalContext_t *ptiPortalContext,
                         tiDeviceHandle_t *ptiDevHandle,
                         tiIntrEventType_t eventType,
                         U32 eventStatus,
                         void *parm )
{
  ag_portal_data_t *pPortalData;
  ag_portal_info_t *pPortalInfo;
  struct agtiapi_softc *pCard = TIROOT_TO_CARD( ptiRoot );
  ccb_t     *pccb;
  ccb_t     *pTMccb;
  ccb_t     *ccbIO;

#ifdef  AGTIAPI_EVENT_LOG
  AGTIAPI_PRINTK("Initiator Event:\n");
  AGTIAPI_PRINTK("DevHandle %p, eventType 0x%x, eventStatus 0x%x\n", 
                 ptiDevHandle, eventType, eventStatus);
  AGTIAPI_PRINTK("Parameter: %s\n", (char *)parm);
#endif

  AGTIAPI_PRINTK("ostiInitiatorEvent: eventType 0x%x eventStatus 0x%x\n", eventType, eventStatus);

  switch (eventType)
  {
  case tiIntrEventTypeCnxError:
       if (eventStatus == tiCnxUp)
       {
         AGTIAPI_PRINTK("tiIntrEventTypeCnxError - tiCnxUp!\n");
       } 
       if (eventStatus == tiCnxDown)
       {
         AGTIAPI_PRINTK("tiIntrEventTypeCnxError - tiCnxDown!\n");
       } 
       break;
  case tiIntrEventTypeDiscovery:
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(ptiPortalContext);
       pCard->flags |= AGTIAPI_CB_DONE;
       if (eventStatus == tiDiscOK)
       {
         AGTIAPI_PRINTK("eventStatus - tiDiscOK\n");
         AGTIAPI_PRINTK("ostiInitiatorEvent: pcard %d eventStatus - tiDiscOK\n", pCard->cardNo );
         PORTAL_STATUS(pPortalData) |= AGTIAPI_DISC_COMPLETE;
#ifndef HOTPLUG_SUPPORT
         if (!(pCard->flags & AGTIAPI_INIT_TIME))
#else
         if (TRUE)
#endif
         {

           agtiapi_GetDevHandle(pCard, &pPortalData->portalInfo, 
                                tiIntrEventTypeDiscovery, tiDiscOK);
           PORTAL_STATUS(pPortalData) |=
             (AGTIAPI_DISC_DONE | AGTIAPI_PORT_LINK_UP);
         }
         /* Trigger CheckIOTimeout */
         callout_reset(&pCard->IO_timer, 20*hz, agtiapi_CheckIOTimeout, pCard);
       }
       else if (eventStatus == tiDiscFailed)
       {
         AGTIAPI_PRINTK("eventStatus - tiDiscFailed\n");
         agtiapi_GetDevHandle(pCard, &pPortalData->portalInfo, 
                              tiIntrEventTypeDiscovery, tiDiscFailed);
         PORTAL_STATUS(pPortalData) &= ~AGTIAPI_DISC_DONE;
       }
       AGTIAPI_PRINTK("tiIntrEventTypeDiscovery - portal %p, status 0x%x\n",
         pPortalData,
         PORTAL_STATUS(pPortalData));
       break;
  case tiIntrEventTypeDeviceChange:
       AGTIAPI_PRINTK("tiIntrEventTypeDeviceChange - portal %p es %d\n",
                      ptiPortalContext->osData, eventStatus);
       pPortalData = PORTAL_CONTEXT_TO_PORTALDATA(ptiPortalContext);
       pPortalInfo = &pPortalData->portalInfo;
#ifndef HOTPLUG_SUPPORT
       if (!(pCard->flags & AGTIAPI_INIT_TIME))
#else
       if (TRUE)
#endif
       {
         agtiapi_GetDevHandle(pCard, pPortalInfo, tiIntrEventTypeDeviceChange, 
                              eventStatus);
//         agtiapi_StartIO(pCard);
       }
       break;
  case tiIntrEventTypeTransportRecovery:
       AGTIAPI_PRINTK("tiIntrEventTypeTransportRecovery!\n");
       break;
  case tiIntrEventTypeTaskManagement:
       AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement!\n");
       pccb = (pccb_t)((tiIORequest_t *)parm)->osData;
       if (pccb->flags & TASK_TIMEOUT)
       {
         AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: TM timeout!\n");
         agtiapi_FreeTMCCB(pCard, pccb);
       }
       else
       {
         pccb->flags |= AGTIAPI_CB_DONE;
         if (eventStatus == tiTMOK)
         {
           pccb->flags |= TASK_SUCCESS;
           AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: pTMccb %p flag %x \n",
                          pccb, pccb->flags);

           /* Incase of TM_DEV_RESET, issue LocalAbort to abort pending IO */
           if (pccb->flags & DEV_RESET) 
           {
               AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: Target Reset\n");
               ccbIO = pccb->pccbIO;
               AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: IO to be aborted locally %p flag %x \n",
                          ccbIO, ccbIO->flags);
               if (ccbIO->startTime == 0) /* IO has been completed. No local abort */
               {
               }			  
               else if (tiINIIOAbort(&pCard->tiRoot, &ccbIO->tiIORequest) != tiSuccess)
               {
                   AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: Local Abort failed\n");
                   /* TODO: call Soft reset here */
               }
           }
          else if (eventStatus == tiTMFailed) 
          {
               ccbIO = pccb->pccbIO;               
               if (ccbIO->startTime == 0) /* IO has been completed. */
               {
                   AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: TM failed because IO has been completed! pTMccb %p flag %x \n",
                                   pccb, pccb->flags);
               }
               else
               {	       	       		  
              AGTIAPI_PRINTK("tiIntrEventTypeTaskManagement: TM failed! pTMccb %p flag %x \n",
                             pccb, pccb->flags);
               /* TODO:*/
              /* if TM_ABORT_TASK, call TM_TARGET_RESET */
              /* if TM_TARGET_RESET, call Soft_Reset */
               }	      
          }
          /* Free TM_DEV_RESET ccb */
          agtiapi_FreeTMCCB(pCard, pccb);
         }
        }
       break;
  case tiIntrEventTypeLocalAbort:
        AGTIAPI_PRINTK("tiIntrEventTypeLocalAbort!\n");
        pccb = (pccb_t)((tiIORequest_t *)parm)->osData;
        pccb->flags |= AGTIAPI_CB_DONE;
        if (eventStatus == tiAbortOK)
        {
            AGTIAPI_PRINTK("tiIntrEventTypeLocalAbort: taskTag pccb %p flag %x \n",
                           pccb, pccb->flags);
            /* If this was LocalAbort for TM ABORT_TASK, issue TM_DEV_RESET */
            if (pccb->flags & TASK_MANAGEMENT)
            {
                if ((pTMccb = agtiapi_GetCCB(pCard)) == NULL)
                {
                    AGTIAPI_PRINTK("tiIntrEventTypeLocalAbort: TM resource unavailable!\n");
                    /* TODO: SoftReset here? */
                }
                pTMccb->pmcsc = pCard;
                pTMccb->targetId = pccb->targetId;
                pTMccb->devHandle = pccb->devHandle;

                /* save pending io to issue local abort at Task mgmt CB */
                pTMccb->pccbIO = pccb->pccbIO;
                pTMccb->flags &= ~(TASK_SUCCESS | ACTIVE);
                pTMccb->flags |= DEV_RESET;
                if (tiINITaskManagement(&pCard->tiRoot, 
                                        pccb->devHandle,
                                        AG_TARGET_WARM_RESET,
                                        &pccb->tiSuperScsiRequest.scsiCmnd.lun,
                                        &pccb->tiIORequest, 
                                        &pTMccb->tiIORequest) 
                    == tiSuccess)
                {
                    AGTIAPI_PRINTK("tiIntrEventTypeLocalAbort: TM_TARGET_RESET request success ccb %p, pTMccb %p\n", 
                                   pccb, pTMccb);
                    pTMccb->startTime = ticks;
                }
                else
                {
                    AGTIAPI_PRINTK("tiIntrEventTypeLocalAbort: TM_TARGET_RESET request failed ccb %p, pTMccb %p\n", 
                                   pccb, pTMccb);
                    agtiapi_FreeTMCCB(pCard, pTMccb);
                    /* TODO: SoftReset here? */
                }
                /* Free ABORT_TASK TM ccb */
                agtiapi_FreeTMCCB(pCard, pccb);
            }
        }
        else if (eventStatus == tiAbortFailed) 
        {
            /* TODO: */
            /* If TM_ABORT_TASK fails, issue TM_DEV_RESET */
            /* if TM_DEV_RESET fails, issue Soft_Reset */
            AGTIAPI_PRINTK("tiIntrEventTypeLocalAbort: Abort Failed pccb %p\n", pccb);
       }
       break;
  default:
       AGTIAPI_PRINTK("tiIntrEventType default!\n");
       break;
  }
}


/******************************************************************************
ostiInitiatorIOCompleted()

Purpose:
  IO request completion call back 
Parameters:
  tiRoot_t *ptiRoot (IN)               Pointer to the HBA tiRoot
  tiIORequest_t *ptiIORequest (IN)     Pointer to the tiIORequest structure
  tiIOStatus_t IOStatus (IN)           I/O complated status  
  U32 statusDetail (IN)                Additional information on status
  tiSenseData_t *pSensedata (IN)       Sense data buffer pointer
  U32 context (IN)                     Interrupt dealing context
Returns:
Note:
******************************************************************************/
void
ostiInitiatorIOCompleted(tiRoot_t      *ptiRoot, 
                               tiIORequest_t *ptiIORequest,
                               tiIOStatus_t  IOStatus,
                               U32           statusDetail,
                               tiSenseData_t *pSenseData,
                               U32           context )
{
  struct agtiapi_softc  *pCard;
  ccb_t      *pccb;

  pCard = TIROOT_TO_CARD(ptiRoot);
  pccb = (ccb_t *)ptiIORequest->osData;

  AGTIAPI_IO( "ostiInitiatorIOCompleted: start\n" );

  if (IOStatus == tiIODifError)
  {
    return;
  }
  OSTI_OUT_ENTER(ptiRoot);

  pccb->ccbStatus  = (U16)IOStatus;
  pccb->scsiStatus = statusDetail;

  if ((IOStatus == tiIOSuccess) && (statusDetail == SCSI_CHECK_CONDITION)) 
  {
    if (pSenseData == (tiSenseData_t *)agNULL) 
    {
      AGTIAPI_PRINTK( "ostiInitiatorIOCompleted: "
                      "check condition without sense data!\n" );
    }
    else 
    {
      union ccb *ccb = pccb->ccb;
      struct ccb_scsiio *csio = &ccb->csio;
      int sense_len = 0;
      if (pccb->senseLen > pSenseData->senseLen)
      {
        csio->sense_resid = pccb->senseLen - pSenseData->senseLen;
      }
      else
      {
        csio->sense_resid = 0;
      }
      sense_len = MIN( pSenseData->senseLen,
                       pccb->senseLen - csio->sense_resid );
      bzero(&csio->sense_data, sizeof(csio->sense_data));
      AGTIAPI_PRINTK("ostiInitiatorIOCompleted: check condition copying\n");
      memcpy( (void *)pccb->pSenseData,
              pSenseData->senseData,
              sense_len );
      agtiapi_hexdump( "ostiInitiatorIOCompleted check condition",
                       (bit8 *)&csio->sense_data, sense_len );
    }
  }
  if ((IOStatus == tiIOFailed) && (statusDetail == tiDetailAborted))
  {
    AGTIAPI_PRINTK("ostiInitiatorIOCompleted - aborted ccb %p, flag %x\n",
                   pccb, pccb->flags);
    /* indicate aborted IO completion */
    pccb->startTime = 0;     
    agtiapi_Done(pCard, pccb);
  }
  else
  {
#ifdef AGTIAPI_SA
    /* 
     * SAS no data command does not trigger interrupt.
     * Command is completed in tdlayer and IO completion is called directly.
     * The completed IO therefore is not post processed.
     * Flag is raised and TDTimer will check and process IO for SAS.
     * This is a temporary solution. - Eddie, 07-17-2006
     */ 
    pCard->flags |= AGTIAPI_FLAG_UP;
#endif
    pccb->flags  |= REQ_DONE;
    agtiapi_QueueCCB(pCard, &pCard->ccbDoneHead, &pCard->ccbDoneTail
                     AG_CARD_LOCAL_LOCK(&pCard->doneLock), pccb);
  }
  OSTI_OUT_LEAVE(ptiRoot);
  return;
}
#ifdef HIALEAH_ENCRYPTION
osGLOBAL void
ostidisableEncryption(tiRoot_t *ptiRoot)
{
  struct agtiapi_softc  *pCard;
  pCard = TIROOT_TO_CARD(ptiRoot);
  pCard->encrypt=agFALSE;
}
#endif
/* device Handle */
osGLOBAL //FORCEINLINE
tiDeviceHandle_t*
ostiGetDevHandleFromSasAddr(
  tiRoot_t    *root,
  unsigned char *sas_addr
)
{
  int i;
  unsigned long x;
  
  ag_portal_data_t           *pPortal = NULL;
  tiDeviceHandle_t *devHandle = NULL;
  struct agtiapi_softc *pCard = TIROOT_TO_CARD(root);
  bit8 sas_addr_hi[4], sas_addr_lo[4];


  for(i=0; i<4; i++)
  {
  	sas_addr_hi[i] = sas_addr[3-i];
  }

  for(i=0; i<4; i++)
  {
  	sas_addr_lo[i] = sas_addr[7-i];
  }
  
    /* Retrieve the handles for each portal */
  for (x=0; x < pCard->portCount; x++)
  {
    pPortal = &pCard->pPortalData[x];
    devHandle = tiINIGetExpDeviceHandleBySasAddress(&pCard->tiRoot, 
                    &pPortal->portalInfo.tiPortalContext,
					*(bit32*)sas_addr_hi,
					*(bit32*)sas_addr_lo,
					(bit32)1024/*gMaxTargets*/);
	if(devHandle != NULL)
		break;
  }
  return devHandle;

  return NULL;
}
/******************************************************************************
ostiInitiatorSMPCompleted()

Purpose:
  IO request completion call back 
Parameters:
  tiRoot_t *ptiRoot (IN)               Pointer to the HBA tiRoot
  tiIORequest_t *ptiSMPRequest (IN)    Pointer to the SMP request structure
  tiIOStatus_t IOStatus (IN)           I/O complated status  
  U32 tiSMPInfoLen (IN)                Number of bytes of response frame len
  tiFrameHandle    (IN)                Handle that referes to response frame
  U32 context (IN)                     Interrupt dealing context
Returns:
Note:
******************************************************************************/
void
ostiInitiatorSMPCompleted(tiRoot_t      *ptiRoot, 
                          tiIORequest_t *ptiSMPRequest, 
                          tiSMPStatus_t  smpStatus, 
                          bit32          tiSMPInfoLen,
                          void           *tiFrameHandle,
                          bit32          context)
{
  struct agtiapi_softc  *pCard;
  ccb_t      *pccb;
  pCard = TIROOT_TO_CARD(ptiRoot);
  pccb = (ccb_t *)ptiSMPRequest->osData;
  
  AGTIAPI_PRINTK("ostiInitiatorSMPCompleted: start\n");
  
  OSTI_OUT_ENTER(ptiRoot);
  pccb->ccbStatus  = (U16)smpStatus;
  if(smpStatus != tiSMPSuccess)
  {
    AGTIAPI_PRINTK("ostiInitiatorSMPCompleted: SMP Error\n");
  }
  else
  {
    union ccb *ccb = pccb->ccb;
    struct ccb_smpio *csmpio = &ccb->smpio;
    memcpy(csmpio->smp_response, tiFrameHandle, tiSMPInfoLen);
    csmpio->smp_response_len = tiSMPInfoLen;
    agtiapi_hexdump("ostiInitiatorSMPCompleted: Response Payload in CAM", (bit8 *)csmpio->smp_response, csmpio->smp_response_len);  
  }
  pccb->flags  |= REQ_DONE;
  agtiapi_QueueCCB(pCard, &pCard->smpDoneHead, &pCard->smpDoneTail
                     AG_CARD_LOCAL_LOCK(&pCard->doneSMPLock), pccb);
  AGTIAPI_PRINTK("ostiInitiatorSMPCompleted: Done\n");
  OSTI_OUT_LEAVE(ptiRoot);
  
  return;  
}

#ifdef FAST_IO_TEST
void
osti_FastIOCb(tiRoot_t      *ptiRoot,
              void          *arg,
              tiIOStatus_t  IOStatus,
              U32           statusDetail)
{
  ccb_t     *pccb = (ccb_t*)arg;
  ag_card_t *pCard;

  static int callNum = 0;

  callNum++;

  BUG_ON(!pccb);

  if ((callNum % CMDS_PER_IO_DUP) != 0)
  {
    goto err;
  }

  pccb->ccbStatus = IOStatus;
  pccb->scsiStatus = statusDetail;

  /* pccb->pSenseData is copied already */

  if (pccb->flags & AGTIAPI_ABORT)
  {
    AGTIAPI_PRINTK("agtiapi_SuperIOCb: aborted ccb %p, flag %x\n",
                   pccb, pccb->flags);
    pccb->startTime = 0;     /* indicate aborted IO completion */
    BUG_ON(1);
    goto err;
  }
  pCard = TIROOT_TO_CARD(ptiRoot);
  pccb->flags |= REQ_DONE;
  agtiapi_QueueCCB(pCard, &pCard->ccbDoneHead, &pCard->ccbDoneTail
                   AG_CARD_LOCAL_LOCK(&pCard->doneLock), pccb);
err:
  return;
} /* osti_FastIOCb */
#endif


/******************************************************************************
ostiSingleThreadedEnter()

Purpose:
  Critical region code excution protection.
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to tiRoot data structure
  U32 queueId (IN)     spinlock Id
Returns:
Note:
  Lock is held by oslayer.
******************************************************************************/
void
ostiSingleThreadedEnter(tiRoot_t *ptiRoot, U32 queueId)
{
  struct agtiapi_softc *pCard = TIROOT_TO_CARD(ptiRoot);
  mtx_lock( &pCard->STLock[queueId] ); // review: need irq save? ##
}


/******************************************************************************
ostiSingleThreadedLeave()

Purpose:
  Restore multi-threading environment.
Parameters:
  tiRoot_t *ptiRoot (IN)  Pointer to the tiRoot data structure
  U32 queueId (IN)     spinlock Id
Returns:
Note:
  Lock is held by oslayer.
******************************************************************************/
void
ostiSingleThreadedLeave(tiRoot_t *ptiRoot, U32 queueId)
{
  struct agtiapi_softc *pCard = TIROOT_TO_CARD(ptiRoot);
  mtx_unlock( &pCard->STLock[queueId] ); // review: need irq restore? ##
}


osGLOBAL tiDeviceHandle_t*
ostiMapToDevHandle(tiRoot_t  *root, 
                          bit8      pathId,
                          bit8      targetId,
                          bit8      LUN
                          )
{
  tiDeviceHandle_t    *dev      = NULL;
  struct agtiapi_softc          *pCard;
  bit32               offset;

  pCard = TIROOT_TO_CARD(root);
  
  offset = pathId * pCard->tgtCount + targetId;

  if (offset > (pCard->tgtCount - 1) )
  {
    dev = NULL;
  }
  else
  {
    dev = pCard->pDevList[offset].pDevHandle;
  }
  
  return dev;
}



#ifdef PERF_COUNT

#ifdef AGTIAPI_LOCAL_LOCK
#define OSTI_SPIN_LOCK(lock)              spin_lock(lock)
#define OSTI_SPIN_UNLOCK(lock)            spin_unlock(lock)
#else
#define OSTI_SPIN_LOCK(lock)
#define OSTI_SPIN_UNLOCK(lock)
#endif


void
ostiEnter(tiRoot_t *ptiRoot, U32 layer, int io)
{
  ag_card_t *pCard = ((ag_card_info_t*)ptiRoot->osData)->pCard;
  int ini = ((pCard->flags & AGTIAPI_INIT_TIME) == AGTIAPI_INIT_TIME);

  BUG_ON((io != 0 && io != 1) || (layer != 0 && layer != 1 && layer != 2));
  if (!ini)
  {
    unsigned long long cycles = get_cycles();

    OSTI_SPIN_LOCK(&pCard->latLock);
    BUG_ON(pCard->callLevel[io] >= sizeof(pCard->layer[0]) /
                                     sizeof(pCard->layer[0][0]));
    if (pCard->callLevel[io] > 0)
    {
      unsigned int prev_layer = pCard->layer[io][pCard->callLevel[io] - 1];

      pCard->totalCycles[io][prev_layer] += cycles - 
                                             pCard->enterCycles[io][prev_layer];
    }
    pCard->enterCycles[io][layer] = cycles;
    pCard->layer[io][pCard->callLevel[io]] = layer;
    pCard->callLevel[io]++;
    OSTI_SPIN_UNLOCK(&pCard->latLock);
  }
}

void
ostiLeave(tiRoot_t *ptiRoot, U32 layer, int io)
{
  ag_card_t *pCard = ((ag_card_info_t*)ptiRoot->osData)->pCard;
  int ini = ((pCard->flags & AGTIAPI_INIT_TIME) == AGTIAPI_INIT_TIME);

  BUG_ON((io != 0 && io != 1) || (layer != 0 && layer != 1 && layer != 2));
  if (!ini)
  {
    unsigned long long cycles = get_cycles();

    OSTI_SPIN_LOCK(&pCard->latLock);
    pCard->callLevel[io]--;

    BUG_ON(pCard->callLevel[io] < 0);
    BUG_ON(pCard->layer[io][pCard->callLevel[io]] != layer);

    pCard->totalCycles[io][layer] += cycles - pCard->enterCycles[io][layer];
    if (pCard->callLevel[io] > 0)
      pCard->enterCycles[io][pCard->layer[io][pCard->callLevel[io] - 1]] = 
        cycles;
    OSTI_SPIN_UNLOCK(&pCard->latLock);
  }
}
#endif



osGLOBAL FORCEINLINE bit8 
ostiBitScanForward(
                  tiRoot_t   *root,
                  bit32      *Index,
                  bit32       Mask
                  )
{
  return 1;
  
}

#ifdef REMOVED
osGLOBAL sbit32 
ostiAtomicIncrement(
                   tiRoot_t        *root,
                   sbit32 volatile *Addend
                   )
{
  return 1;

}

osGLOBAL sbit32 
ostiAtomicDecrement(
                   tiRoot_t        *root,
                   sbit32 volatile *Addend
                   )
{
 
  return 1;

}

osGLOBAL sbit32 
ostiAtomicBitClear(
                 tiRoot_t         *root,
                 sbit32 volatile  *Destination,
                 sbit32            Value
                 )
{
 
  return 0;
 
}

osGLOBAL sbit32 
ostiAtomicBitSet(
                tiRoot_t         *root,
                sbit32 volatile  *Destination,
                sbit32            Value
                )
{
  return 0;

  /*
   set_bit(Value, (volatile unsigned long *)Destination);
   return 0;
  */
}

osGLOBAL sbit32 
ostiAtomicExchange(
                   tiRoot_t        *root,
                   sbit32 volatile *Target,
                   sbit32           Value
                   )
{
  return 0;
  
}
#endif

osGLOBAL FORCEINLINE sbit32 
ostiInterlockedExchange(
                       tiRoot_t        *root,
                       sbit32 volatile *Target,
                       sbit32           Value
                       )
{
  return 0;
}

osGLOBAL FORCEINLINE sbit32 
ostiInterlockedIncrement(
                       tiRoot_t        *root,
                       sbit32 volatile *Addend
                       )
{
  return 0;
}

osGLOBAL FORCEINLINE sbit32 
ostiInterlockedDecrement(
                         tiRoot_t         *root,
                         sbit32 volatile  *Addend
                         )
{
  return 0;
}

osGLOBAL FORCEINLINE sbit32 
ostiInterlockedAnd(
                   tiRoot_t         *root,
                   sbit32 volatile  *Destination,
                   sbit32            Value
                   )
{
  return 0;
}

osGLOBAL FORCEINLINE sbit32 
ostiInterlockedOr(
                   tiRoot_t         *root,
                   sbit32 volatile  *Destination,
                   sbit32            Value
                   )
{
  return 0;
}

// this is just stub code to allow compile and use of the module ...
// now that a call to this function has been added with windows specific
// intentions.
osGLOBAL bit32
ostiSetDeviceQueueDepth( tiRoot_t *tiRoot,
                         tiIORequest_t  *tiIORequest,
                         bit32           QueueDepth
                         )
{
  bit32 retVal = 0;
  ccb_t *pccb = (ccb_t *) tiIORequest->osData;
  tiDeviceHandle_t *tiDeviceHandle = pccb->devHandle;
  ag_device_t *pDevice = (ag_device_t *)tiDeviceHandle->osData;
  AGTIAPI_PRINTK( "ostiSetDeviceQueueDepth stub only: root%p, req%p, qdeep%d\n",
                  tiRoot, tiIORequest, QueueDepth );
  pDevice->qdepth = QueueDepth;
  return retVal;
}


// this is just stub code to allow compile and use of the module ...
// now that a call to this function has been added with windows specific
// intentions.
osGLOBAL void
ostiGetSenseKeyCount(tiRoot_t  *root,
                     bit32      fIsClear,
                     void      *SenseKeyCount,
                     bit32      length
                     )
{
  AGTIAPI_PRINTK( "ostiGetSenseKeyCount stub only: rt%p, fcl%d, kyCt%p, ln%d\n",
                  root, fIsClear, SenseKeyCount, length );
}

osGLOBAL void
ostiGetSCSIStatusCount(tiRoot_t  *root, 
                              bit32      fIsClear,
                              void      *ScsiStatusCount,
                              bit32      length
                              )
{
 AGTIAPI_PRINTK( "ostiGetSCSIStatusCount: stub only rt%p, fcl%d, kyCt%p, ln%d\n",
                 root, fIsClear, ScsiStatusCount, length );

}

osGLOBAL void ostiPCI_TRIGGER( tiRoot_t *tiRoot )
{
  ostiChipReadBit32Ext(tiRoot, 0, 0x5C);

}

osGLOBAL bit32 
ostiNumOfLUNIOCTLreq(  tiRoot_t          *root,
                              void              *param1,
                              void              *param2,
                              void              **tiRequestBody,
                              tiIORequest_t     **tiIORequest
                              ) 
{
  bit32		status = IOCTL_CALL_SUCCESS;
  pccb_t pccb;
  AGTIAPI_PRINTK("ostiNumOfLUNIOCTLreq: start\n");
  struct agtiapi_softc *pCard = TIROOT_TO_CARD(root);
    /* get a ccb */
  if ((pccb = agtiapi_GetCCB(pCard)) == NULL)
  {
    printf("ostiNumOfLUNIOCTLreq - GetCCB ERROR\n");
    status = IOCTL_CALL_FAIL;
    //BUG_ON(1);
  }

  *tiIORequest = (tiIORequest_t*)&pccb->tiIORequest;
  *tiRequestBody = &pccb->tdIOReqBody;
  AGTIAPI_PRINTK("ostiNumOfLUNIOCTLreq:end\n");
  return status;
}

