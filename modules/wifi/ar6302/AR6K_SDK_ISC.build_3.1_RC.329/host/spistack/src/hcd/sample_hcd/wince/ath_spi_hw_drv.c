//------------------------------------------------------------------------------
// <copyright file="ath_spi_hw_drv.c" company="Atheros">
//    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
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

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

     sample SPI hardware driver layer

+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define MODULE_NAME  ATHSPI
/* debug level for this module*/
#define DBG_DECLARE 7
#include <ctsystem.h>
#include <sdio_busdriver.h>
#include <sdio_lib.h>
#include "ath_spi_hcd_if.h"
#include "ath_spi_hcd.h"
#include "ath_spi_hw.h"

#define WORKER_THREAD_PRIORITY      160
#define SPI_IRQ_THREAD_PRIORITY     120

static void IOCompleteWork(PVOID pContext);
static DWORD SpiGpioIRQInterruptThread(LPVOID pContext);

SDHCD_HW_DEVICE   g_HWDevice;

SDHCD_HW_DEVICE *InitializeSPIHW(PTSTR pRegPath)
{
    
    PSDHCD_DEVICE   pDevice;   
    SDHCD_HW_DEVICE *pHWDevice;                  
    DWORD           threadId;  
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
     
    do {      
            /* for now this is a static, single instance allocation */
        pHWDevice = &g_HWDevice;
        ZERO_POBJECT(pHWDevice);       
        pDevice = &pHWDevice->SpiCommon; 
        pHWDevice->pDevice = pDevice;
            /* set the HW portion */
        pDevice->pHWDevice = pHWDevice; 
        pHWDevice = GET_HW_DEVICE(pDevice);         
        SET_SDIO_STACK_VERSION(&pDevice->Hcd);
        pDevice->Hcd.pName = SDIO_RAW_BD_BASE;
        pDevice->Hcd.Attributes = 0;
        pDevice->Hcd.pContext = pDevice;
        pDevice->Hcd.pRequest = HcdRequest;
        pDevice->Hcd.pConfigure = HcdConfig;
        
            /* TODO : adjust these to match controller hardware */
        pDevice->OperationalClock = 12000000;  /* 12 mhz */
        pDevice->Hcd.MaxBytesPerBlock = 4096;  /* used as a hint to indicate max size of common buffer */
        pDevice->Hcd.MaxBlocksPerTrans = 1;    /* must be one*/
        pDevice->Hcd.MaxClockRate = 48000000;  /* 48 Mhz */
        pDevice->PowerUpDelay = 100;
            /* set all the supported frame widths the controller can do
             * 8/16/24/32 bit frames */
        pDevice->SpiHWCapabilitiesFlags = HW_SPI_FRAME_WIDTH_8  | 
                                          HW_SPI_FRAME_WIDTH_16 | 
                                          HW_SPI_FRAME_WIDTH_24 |
                                          HW_SPI_FRAME_WIDTH_32;

  
        pDevice->MiscFlags |= MISC_FLAG_DUMP_STATE_ON_SHUTDOWN | MISC_FLAG_RESET_SPI_IF_SHUTDOWN;
        
        SDLIB_InitializeWorkerTask(&pHWDevice->IOCompleteWorkTask,
                                   IOCompleteWork,
                                   pHWDevice);
        
        pHWDevice->pWorker = SDLIB_CreateWorker(WORKER_THREAD_PRIORITY);
        
        if (NULL == pHWDevice->pWorker) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }
        
        /* TODO : allocate hardware resources (I/O , interrupt, DMA etc..) */



        /*************************************/
        
        status = HcdInitialize(pDevice);
   
            /* initialize common layer */
        if (!SDIO_SUCCESS(status)) {
            DBG_PRINT(SDDBG_ERROR, ("SPI - failed to init common layer, status =%d\n", status));
            break;
        } 
                       
        pHWDevice->InitStateMask |= SDHC_COMMON_INIT;
        
             /* create the interrupt event */
        pHWDevice->hIstEventSPIGpioIRQ = CreateEvent(NULL, FALSE, FALSE, NULL);
        
        if (NULL == pHWDevice->hIstEventSPIGpioIRQ) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }

            /* TODO set appropriate system interrupt for GPIO IRQ,
             * GPIO IRQ must be level sensitive, active LOW */
        pHWDevice->SysIntrSPIGpioIRQ = 0;

        /* TODO : uncomment the following to associate the GPIO IRQ to the interrupt event :
        if (!InterruptInitialize(pHWDevice->SysIntrSPIGpioIRQ,
                                 pHWDevice->hIstEventSPIGpioIRQ,
                                 NULL,
                                 0)) {
            DBG_PRINT(SDDBG_ERROR,("SPI HCD: Failed to initialize Interrupt! \n"));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
        */
        
            /* create the IST thread */
        pHWDevice->hIstSPIGpioIRQ = CreateThread(NULL,
                                     0,
                                     (LPTHREAD_START_ROUTINE)SpiGpioIRQInterruptThread,
                                     (LPVOID)pHWDevice,
                                     0,
                                     &threadId);
                                     
        if (NULL == pHWDevice->hIstSPIGpioIRQ) {
            status = SDIO_STATUS_NO_RESOURCES;
            DBG_PRINT(SDDBG_ERROR,("SPI HCD: Failed to Create IST! \n"));
            break;
        }
        
           /* register with the SDIO bus driver */
        if (!SDIO_SUCCESS((status = SDIO_RegisterHostController(&pDevice->Hcd)))) {
            DBG_PRINT(SDDBG_ERROR, ("SPI HCD: Probe - failed to register with host, status =%d\n",status));
            break;
        }   
          
        pHWDevice->InitStateMask |= SDHC_REGISTERED;
                           
    } while (FALSE);
     
    if (!SDIO_SUCCESS(status)) {
        if (pHWDevice != NULL) {
            CleanupSPIHW(pHWDevice);
            pHWDevice = NULL;
        }
    } else {
        DBG_PRINT(SDDBG_ERROR, ("SPI - HCD ready! \n"));
    }
    
    DBG_PRINT(SDDBG_TRACE, ("-SPI HCD: Setup - status : %d\n", status));
    return SDIO_SUCCESS(status) ? pHWDevice : NULL;  
}    

void CleanupSPIHW(SDHCD_HW_DEVICE *pHWDevice)
{
    pHWDevice->ShutDown = TRUE;
                       
    if (pHWDevice->InitStateMask & SDHC_COMMON_INIT) {
            /* deinit common layer */
        HcdDeinitialize(pHWDevice->pDevice);
    }
    
    if (pHWDevice->InitStateMask & SDHC_REGISTERED) {
        SDIO_UnregisterHostController(&pHWDevice->pDevice->Hcd);
    }
    
    if (pHWDevice->pWorker != NULL) {
        SDLIB_FlushWorkTask(pHWDevice->pWorker,&pHWDevice->IOCompleteWorkTask);    
        SDLIB_DestroyWorker(pHWDevice->pWorker);
    }
    
    if (pHWDevice->hIstEventSPIGpioIRQ != NULL) {
            /* make sure interrupt asssociated with the event is disabled */
        if (pHWDevice->SysIntrSPIGpioIRQ != 0) {
            InterruptDisable(pHWDevice->SysIntrSPIGpioIRQ);            
        }
        if (pHWDevice->hIstSPIGpioIRQ != NULL) {
                /* wake IST */
            SetEvent(pHWDevice->hIstEventSPIGpioIRQ);
                /* wait for IST to exit */
            WaitForSingleObject(pHWDevice->hIstSPIGpioIRQ, INFINITE);
            CloseHandle(pHWDevice->hIstSPIGpioIRQ);
            pHWDevice->hIstSPIGpioIRQ = NULL;
        }
        CloseHandle(pHWDevice->hIstEventSPIGpioIRQ);
        pHWDevice->hIstEventSPIGpioIRQ = NULL;
    }
    
    /* TODO : free hardware resources */


    /*********************************/
    
    ZERO_OBJECT(g_HWDevice);
    DBG_PRINT(SDDBG_TRACE, ("SPI HCD: CleanupDevice\n"));
}


/* set up SPI host controller DMA */
SDIO_STATUS HW_SpiSetUpDMA(PSDHCD_DEVICE    pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
  
    /* TODO see PSDHCD_DEVICE definition to get buffer variables to do 
     * the DMA transfer .  
     * 
     * Setup DMA hardware to do the transfer (direction, length, common buffer or direct)
     * If the driver performs common-buffer DMA, the hardware layer is responsible
     * for copying the data to/from pCurrentBuffer (see below) */
   
    if (pHWDevice->CommonBufferDMA && !pDevice->CurrentTransferDirRx) {
            /* write direction using common buffer DMA , must transfer to common buffer now */
        HcdCommonBufferCopy(pDevice->CurrentDmaWidth,
                            pHWDevice->pDmaCommonBuffer,
                            pDevice->pCurrentBuffer,
                            pDevice->CurrentTransferLength,
                            pDevice->HostDMABufferCopyMode);
        
    }
    
    return SDIO_STATUS_PENDING;   
}

    /* set the clock rate for the SPI transactions */
void HW_SetClock(PSDHCD_DEVICE pDevice, PUINT32 pClockRate)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    
    /* TODO set the clock rate to the closest (or less) rate*/
    
}

    /* enable disable SPI (via GPIO) interrupt detection */
void HW_EnableDisableSPIIRQ(PSDHCD_DEVICE pDevice, BOOL Enable, BOOL FromIrq)
{
   PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
   /* TODO */
    
}

    /* start the DMA operation on the SPI host controller */
void HW_StartDMA(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    
    /* TODO The hardware driver should enable DMA hardware.  DMA should complete asynchronously
     * through an interrupt or some callback implemented in the platform.  When the DMA
     * completes, the HW layer must indicate DMA completion using HcdDmaCompletion()
     * If common buffer was employed, the HW layer must call HcdCommonBufferCopy() which
     * copies from the HW's DMA buffer to the bus request buffer and performs any necessary
     * byte swapping see SampleCompleteDMATransferCallback() */
    
    do {
        
        if (pHWDevice->CommonBufferDMA) {
                /* common buffer DMA case */
            if (pDevice->CurrentTransferDirRx) {
                /* handle read dma */    
            } else {
                /* handle write dma */    
            }
            
        } else {
                 /* scatter gather DMA case */
                 
            if (pDevice->CurrentTransferDirRx) {
                /* handle read dma */    
            } else {
                /* handle write dma */    
            }
            
        }
        
    } while (FALSE);
}

void SampleCompleteDMATransferCallback(pContext)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext;
    PSDHCD_DEVICE    pDevice;  
    SDIO_STATUS      status = SDIO_STATUS_SUCCESS;

    DBG_PRINT(ATH_SPI_TRACE_DATA, ("SPI  DMA COMPLETE - \n"));
   
    /* TODO : driver should check if DMA completed successfully, this is platform dependent */
    
    pDevice = pHWDevice->pDevice;  
    
    do {
        if (!SDIO_SUCCESS(status)) {
            break;
        }        
        
        if (pHWDevice->CommonBufferDMA && pDevice->CurrentTransferDirRx) {
                /* copy common buffer back for RX */
            HcdCommonBufferCopy(pDevice->CurrentDmaWidth,
                                pDevice->pCurrentBuffer,
                                pHWDevice->pDmaCommonBuffer,
                                pDevice->CurrentTransferLength,
                                pDevice->HostDMABufferCopyMode);
            
        }
        
    } while (FALSE);
            
    return;    
}   
    
/*
 * StopDMATransfer - stop DMA transfer
*/
void HW_StopDMATransfer(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
   /* TODO */
}

/* deferred completion */
SDIO_STATUS HW_QueueDeferredCompletion(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    
    SDLIB_QueueWorkTask(pHWDevice->pWorker,&pHWDevice->IOCompleteWorkTask);

    return SDIO_STATUS_SUCCESS;  
}

    /* SPI token input output */   
SDIO_STATUS HW_InOut_Token(PSDHCD_DEVICE pDevice,
                           UINT32        OutToken,
                           UINT8         DataSize,
                           PUINT32       pInToken) 
{   
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    SDIO_STATUS      status = SDIO_STATUS_SUCCESS;
    UINT32           inTokenVal = 0xFFFFFFFF;
    UINT32           mask;
    
    /* TODO , this function must issue the token WITHOUT interrupts (polling).  Token
     * issue must be fast and low overhead.  For larger transfers, the common layer will
     * call the HW DMA setup and start APIs */
    
    do {
        if (DataSize == ATH_TRANS_DS_16) {        
            /* do 16 bit frame */     
            mask = 0xFFFF;
        } else if (DataSize == ATH_TRANS_DS_32) {   
            /* do 32 bit frame */           
            mask = 0xFFFFFFFF;
        } else if (DataSize == ATH_TRANS_DS_8) { 
            /* do 8 bit frame */            
            mask = 0xFF;
        } else if (DataSize == ATH_TRANS_DS_24) { 
            /* do 24 bit frame */ 
            mask = 0xFFFFFF;
        } else {
            DBG_ASSERT(FALSE); 
            status = SDIO_STATUS_INVALID_PARAMETER;
            break;  
        }
    
        /* TODO .. issue SPI frame, inTokenVal is the SPI value returned from the device */
        
        if (pInToken != NULL) {
            *pInToken = inTokenVal & mask;
        }
        
    } while (FALSE);
    
    return status;                                                                        
}

    /* debugging from common layer */
void HW_SetDebugSignal(PSDHCD_DEVICE pDevice, INT PinNo, BOOL ON)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    /* OPTIONAL GPIO toggling for timming analysis */
}


void HW_ToggleDebugSignal(PSDHCD_DEVICE pDevice, int PinNo)
{ 
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    /* OPTIONAL GPIO toggling for timming analysis */
}

void HW_UsecDelay(PSDHCD_DEVICE pDevice, UINT32 uSeconds)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    
    /* TODO */
}

void HW_StartTimer(PSDHCD_DEVICE pDevice, int TimeoutMS, int Context)
{
    /* OPTIONAL timer start to trap hung requests, only used for debugging */
}

void HW_StopTimer(PSDHCD_DEVICE pDevice)
{
    /* OPTIONAL timer start to trap hung requests, only used for debugging */
}


static VOID IOCompleteWork(PVOID pContext)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext; 
    
    SDIO_HandleHcdEvent(&pHWDevice->pDevice->Hcd, EVENT_HCD_TRANSFER_DONE);
}



static DWORD SpiGpioIRQInterruptThread(LPVOID pContext)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext;

    DBG_PRINT(SDDBG_TRACE, ("SpiGpioIRQInterruptThread: Initializing. Context 0x%X \n",pContext));
    
    CeSetThreadPriority(GetCurrentThread(), SPI_IRQ_THREAD_PRIORITY);
    
    while (TRUE) {
        WaitForSingleObject(pHWDevice->hIstEventSPIGpioIRQ,INFINITE); 
        if (pHWDevice->ShutDown) {
            DBG_PRINT(SDDBG_TRACE, ("SpiGpioIRQInterruptThread: Shutting down \n"));            
            break;
        }
        
        HcdSpiInterrupt(pHWDevice->pDevice);
            
            /* ack kernel/OAL that interrupt has been acknowledged */
        InterruptDone(pHWDevice->SysIntrSPIGpioIRQ); 
    }

    return 0;
}

void HW_PowerUpDown(PVOID pContext, BOOL powerUp)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext;
	
}




