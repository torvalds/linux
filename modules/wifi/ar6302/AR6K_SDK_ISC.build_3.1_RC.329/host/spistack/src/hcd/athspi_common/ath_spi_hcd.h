//------------------------------------------------------------------------------
// <copyright file="ath_spi.h" company="Atheros">
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
#ifndef __ATH_SPI_HCD_H___
#define __ATH_SPI_HCD_H___

#include <ctsystem.h>
#include <sdio_busdriver.h>
#include <sdio_lib.h>
#include "ath_spi_hcd_if.h"
#if 0
#undef DBG_PRINT
#undef DBG_ASSERT
#undef DBG_ASSERT_WITH_MSG
#define DBG_PRINT(lvl, str)
#define DBG_ASSERT(test)
#define DBG_ASSERT_WITH_MSG(test,s)
#define ATH_HCD_NODEBUG
#endif

#define EXTERNAL_ACCESS_DONE_RETRY_COUNT 3
#define ATH_SPI_BYTES_AVAIL_POLL_RETRY_LIMIT 100

enum ATH_SPI_TRACE_ENUM {
    ATH_SPI_TRACE_SPI_INT = (SDDBG_TRACE + 1),
    ATH_SPI_TRACE_REQUESTS,  
    ATH_SPI_TRACE_DATA,
    ATH_SPI_TRACE_DATA_DUMP,
    ATH_SPI_TRACE_DMA_DUMP,
    ATH_SPI_TRACE_INFO,  /*very verbose */
    ATH_SPI_TRACE_LAST
};

typedef enum {
    ATH_SPI_AR6002 = 0,
    ATH_SPI_AR6003 = 1,
} ATH_SPI_CHIP_TYPE;

#define AR6003_WRITE_BUFFER_SIZE   3163

typedef struct _SDHCD_DEVICE {
    SDLIST        List;                   /* linked list */
    BOOL          ShuttingDown;           /* indicates shut down of HCD) */
    UINT32        PollWait;               /* poll timeout for an operation */
    UINT8         CurrentDMADataMode;     /* current data mode */
    UINT16        SpiIntEnableShadow;     /* shadow copy of interrupt enables */
    UINT16        SpiConfigShadow;        /* shadow copy of configuration register */
    OS_CRITICALSECTION CritSection;
    BOOL          ExternalIOPending;      /* flag indicating that external host I/O access is pending */
    UINT8         HostAccessDataWidth;    /* data width to use for host access */
    UINT8         DMADataWidth;           /* data width to use for DMA access */
    BOOL          DMAWriteWaitingForBuffer; /* DMA operation is waiting for buffer space */
    BOOL          DMAHWTransferInProgress;  /* DMA hardware transfer is running */
    UINT32        WriteBufferSpace;         /* cached copy of space remaining in the SPI
                                               write buffer */
    UINT32        MaxWriteBufferSpace;      /* max write buffer space that the SPI interface supports */
    UINT32        PktsInSPIWriteBuffer;     /* number of packets in SPI write buffer so far */  
    ATH_SPI_CHIP_TYPE ChipType;             /* chip type */          
    /********************************************
     *  the following fields are filled in by the common layer and used by the HW layer 
     * to process a DMA request
     * 
     ********************************************/
    PUINT8        pCurrentBuffer;         /* current buffer position for DMA */
    UINT32        CurrentTransferLength;  /* current transfer length for common buffer DMA */
    BOOL          CurrentTransferDirRx;   /* current transfer is RX direction */
    UINT8         CurrentDmaWidth;        /* current DMA transfer width */
    BOOL          HostDMABufferCopyMode;  /* DMA transfer copy mode, passed to hardware layer for 
                                             common buffer copies */
    BOOL          HostAccessCopyMode;     /* host access copy mode */
#define BYTE_SWAP    TRUE
#define NO_BYTE_SWAP FALSE

    /*******************************************
     * 
     * the following fields must be filled in by the hardware specific layer 
     * 
     ********************************************/
    UINT32        MaxBytesPerDMARequest;  /* maximum number of bytes per DMA request */
    UINT32        PowerUpDelay;           /* delay before the common layer should initialize over spi */
    UINT32        OperationalClock;       /* spi module operational clock */
    PVOID         pHWDevice;              /* hardware device portion*/
    SDHCD         Hcd;                    /* HCD description for bus driver */
    UINT8         SpiHWCapabilitiesFlags; /* SPI hardware capabilities flags */
    #define       HW_SPI_FRAME_WIDTH_8    0x01
    #define       HW_SPI_FRAME_WIDTH_16   0x02
    #define       HW_SPI_FRAME_WIDTH_24   0x04
    #define       HW_SPI_FRAME_WIDTH_32   0x08
    #define       HW_SPI_INT_EDGE_DETECT  0x80   
    #define       HW_SPI_NO_DMA           0x40  
    UINT8         MiscFlags;    
    #define       MISC_FLAG_SPI_SLEEP_WAR          0x04
    #define       MISC_FLAG_RESET_SPI_IF_SHUTDOWN  0x02
    #define       MISC_FLAG_DUMP_STATE_ON_SHUTDOWN 0x01
    BOOL          FatalError;
} SDHCD_DEVICE, *PSDHCD_DEVICE;

typedef void (*PSDHC_IRQ_SYNC_CALLBACK)(PSDHCD_DEVICE, UINT32 Param1, UINT32 Param2);

    /* function driver IRQ sources */
#define ATH_SPI_FUNC_DRIVER_IRQ_SOURCES (ATH_SPI_INTR_CPU_INTR | \
                                         ATH_SPI_INTR_PKT_AVAIL)

    /* host driver error IRQs */
#define ATH_SPI_HCD_ERROR_IRQS (ATH_SPI_INTR_ADDRESS_ERROR | \
                                ATH_SPI_INTR_WRBUF_ERROR | \
                                ATH_SPI_INTR_RDBUF_ERROR)
    /* host driver IRQ sources */                             
#define ATH_SPI_HCD_IRQ_SOURCES (ATH_SPI_INTR_WRBUF_BELOW_WMARK | ATH_SPI_HCD_ERROR_IRQS)
                                
/* functions implemented in the common layer */
SDIO_STATUS HcdRequest(PSDHCD pHcd);
SDIO_STATUS HcdConfig(PSDHCD pHcd, PSDCONFIG pReq);
SDIO_STATUS HcdInitialize(PSDHCD_DEVICE pDevice);
void        HcdDeinitialize(PSDHCD_DEVICE pDevice);
void        HcdCommonBufferCopy(UINT8         DataSize, 
                                PVOID         pDest, 
                                PVOID         pSrc, 
                                INT           Bytes,
                                BOOL          ManualConvert);
                         
/* common layer callbacks for SPI module interrupt events and DMA completions, these
 * have to be called in a schedulable context */
BOOL        HcdSpiInterrupt(PSDHCD_DEVICE pDevice); 
void        HcdDmaCompletion(PSDHCD_DEVICE pDevice, SDIO_STATUS status);
void        HcdTimerCallback(PSDHCD_DEVICE pDevice, int Context);                      
/* functions that must be implemented in the hardware specific layer */

/* 
 * Function: TODO
 * Inputs:
 * Outputs:
 * Notes:
 * 
 * 
 */
SDIO_STATUS HW_QueueDeferredCompletion(PSDHCD_DEVICE pDevice);
#define HW_FROM_ISR_CONTEXT    TRUE
#define HW_FROM_NORMAL_CONTEXT FALSE
void HW_EnableDisableSPIIRQ(PSDHCD_DEVICE pDevice, BOOL Enable, BOOL FromIrq);
#define SETUP_SPI_DMA_WRITE FALSE
#define SETUP_SPI_DMA_READ  TRUE                                       
SDIO_STATUS HW_SpiSetUpDMA(PSDHCD_DEVICE pDevice);
void HW_StopDMATransfer(PSDHCD_DEVICE pDevice);
void HW_ToggleDebugSignal(PSDHCD_DEVICE pDevice, INT PinNo);
void HW_SetDebugSignal(PSDHCD_DEVICE pDevice, INT PinNo, BOOL ON);
SDIO_STATUS HW_InOut_Token(PSDHCD_DEVICE pDevice,
                        UINT32        OutToken,
                        UINT8         DataSize,
                        PUINT32       pInToken);               
void HW_SetClock(PSDHCD_DEVICE pDevice, PUINT32 pClockRate);
void HW_StartDMA(PSDHCD_DEVICE pDevice);
void HW_StartTimer(PSDHCD_DEVICE pDevice, int TimeoutMS, int Context);
void HW_StopTimer(PSDHCD_DEVICE pDevice);
void HW_UsecDelay(PSDHCD_DEVICE pDevice, UINT32 uSeconds);
void HW_PowerUpDown(PVOID pHWDevice, BOOL powerUp);

#define AR6002_MAX_SPI_CLOCK_RATE    48000000
#define AR6002_NORMAL_CLOCK_RATE     24000000

#endif /* __SDIO_OMAP_HCD_H___ */
