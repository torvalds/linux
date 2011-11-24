//------------------------------------------------------------------------------
// <copyright file="sdio_bus_misc.c" company="Atheros">
//    Copyright (c) 2007-2008 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#define MODULE_NAME  SDBUSDRIVER
#include "../include/ctsystem.h"
#include "../include/sdio_busdriver.h"
#include "../include/sdio_lib.h"
#include "_busdriver.h"
        
     
static void RawHcdIrqControl(PSDHCD pHcd, BOOL Enable)
{
    SDIO_STATUS status;
    SDCONFIG_SDIO_INT_CTRL_DATA irqData;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    ZERO_OBJECT(irqData);  
    
    status = _AcquireHcdLock(pHcd);
    if (!SDIO_SUCCESS(status)) { 
        return;
    }
          
    do {
            /* for raw devices, we simply enable/disable in the HCD only */
        if (Enable) {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver (RAW) Unmasking Int \n"));
            irqData.SlotIRQEnable = TRUE; 
        } else {
            DBG_PRINT(SDDBG_TRACE, ("SDIO Bus Driver (RAW) Masking Int \n"));
            irqData.SlotIRQEnable = FALSE; 
        }
        
        status = _IssueConfig(pHcd,SDCONFIG_SDIO_INT_CTRL,
                              (PVOID)&irqData, sizeof(irqData));
                              
        if (!SDIO_SUCCESS(status)){
            DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver failed to enable/disable IRQ in (RAW) hcd :%d\n", 
                                    status)); 
        }       
        
    } while (FALSE);
  
    status = _ReleaseHcdLock(pHcd); 
}

static void RawHcdEnableIrqPseudoComplete(PSDREQUEST pReq)
{
    if (SDIO_SUCCESS(pReq->Status)) {
        RawHcdIrqControl((PSDHCD)pReq->pCompleteContext, TRUE);  
    }
    FreeRequest(pReq);
}

static void RawHcdDisableIrqPseudoComplete(PSDREQUEST pReq)
{
    RawHcdIrqControl((PSDHCD)pReq->pCompleteContext, FALSE);  
    FreeRequest(pReq);
}


static void HcdAckComplete(PSDREQUEST pReq)
{
    SDIO_STATUS status;
    DBG_PRINT(SDIODBG_FUNC_IRQ, ("SDIO Bus Driver: Hcd (0x%X) Irq Ack \n", 
                    (INT)pReq->pCompleteContext));
        /* re-arm the HCD */
    status = _IssueConfig((PSDHCD)pReq->pCompleteContext,SDCONFIG_SDIO_REARM_INT,NULL,0);  
  
    if (!SDIO_SUCCESS(status)) {
        DBG_PRINT(SDDBG_ERROR, ("SDIO Bus Driver: HCD Re-Arm failed : %d\n", 
                    status));       
    }
    FreeRequest(pReq);
}
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDFunctionAckInterrupt - handle device interrupt acknowledgement
  Input:  pDevice - the device 
  Output: 
  Return: 
  Notes: 
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDFunctionAckInterrupt(PSDDEVICE pDevice)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UCHAR       mask;
    PSDREQUEST  pReq = NULL;
    BOOL        setHcd = FALSE;
    SDIO_STATUS status2;
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    pReq = AllocateRequest();        
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;
    }
    
    status = _AcquireHcdLock(pDevice->pHcd);
    
    if (!SDIO_SUCCESS(status)) {
        FreeRequest(pReq);
        return status; 
    } 
    
    do {
        
        mask = 1 << SDDEVICE_GET_SDIO_FUNCNO(pDevice);
        if (pDevice->pHcd->PendingIrqAcks & mask) { 
                /* clear the ack bit in question */
            pDevice->pHcd->PendingIrqAcks &= ~mask;      
            if (0 == pDevice->pHcd->PendingIrqAcks) {
                pDevice->pHcd->IrqProcState = SDHCD_IDLE;
                    /* no pending acks, so re-arm if irqs are stilled enabled */
                if (pDevice->pHcd->IrqsEnabled) {
                    setHcd = TRUE;
                        /* issue pseudo request to sync this with bus requests */
                    pReq->Status = SDIO_STATUS_SUCCESS;
                    pReq->pCompletion = HcdAckComplete;
                    pReq->pCompleteContext = pDevice->pHcd;  
                    pReq->Flags = SD_PSEUDO_REQ_FLAGS;
                }     
            } 
        } else {
            DBG_PRINT(SDDBG_WARN, ("SDIO Bus Driver: AckInterrupt: no IRQ pending on Function :%d, \n", 
                        SDDEVICE_GET_SDIO_FUNCNO(pDevice)));
        }     
    } while (FALSE); 
        
    status2 = ReleaseHcdLock(pDevice);
    
    if (pReq != NULL) {
        if (SDIO_SUCCESS(status) && (setHcd)) {
                /* issue request */
            IssueRequestToHCD(pDevice->pHcd,pReq);       
        } else {
            FreeRequest(pReq);   
        }  
    }  
    
    return status;  
}

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  SDMaskUnmaskFunctionIRQ - mask/unmask function IRQ
  Input:  pDevice - the device/function
          MaskInt - mask interrupt
  Output: 
  Return: status
  Notes:  Note, this function can be called from an ISR or completion context
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
SDIO_STATUS SDMaskUnmaskFunctionIRQ(PSDDEVICE pDevice, BOOL MaskInt)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    BOOL        setHcd;
    PSDREQUEST  pReq = NULL;
    SDIO_STATUS status2;
    
    CT_DECLARE_IRQ_SYNC_CONTEXT();
    
    setHcd = FALSE;
    
    pReq = AllocateRequest();        
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;
    }
    
    status = _AcquireHcdLock(pDevice->pHcd);
    
    if (!SDIO_SUCCESS(status)) {
        FreeRequest(pReq);
        return status; 
    } 
    
    if (!MaskInt) {
        if (!pDevice->pHcd->IrqsEnabled) {
            pReq->pCompletion = RawHcdEnableIrqPseudoComplete; 
            setHcd = TRUE; 
            pDevice->pHcd->IrqsEnabled = 1 << 1;   
        } 
    } else {
        if (pDevice->pHcd->IrqsEnabled) {
            pReq->pCompletion = RawHcdDisableIrqPseudoComplete; 
            setHcd = TRUE; 
            pDevice->pHcd->IrqsEnabled = 0;   
        } 
    }                       
    
    if (setHcd) {
            /* hcd IRQ control requests must be synched with outstanding 
             * bus requests so we issue a pseudo bus request  */
        pReq->pCompleteContext = pDevice->pHcd;  
        pReq->Flags = SD_PSEUDO_REQ_FLAGS;
        pReq->Status = SDIO_STATUS_SUCCESS;
    } else {
            /* no request to submit, just free it */
        FreeRequest(pReq);   
        pReq = NULL;  
    }
    
    status2 = _ReleaseHcdLock(pDevice->pHcd);
        
    if (pReq != NULL) {
            /* issue request */
        IssueRequestToHCD(pDevice->pHcd,pReq);   
    }  
            
    return status;
}

