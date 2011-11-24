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

     Samsung S3C6400 (SMDK6400) SPI hardware driver layer

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
#include <nkintr.h>


#define WORKER_THREAD_PRIORITY      160
#define SPI_IRQ_THREAD_PRIORITY     120

static void IOCompleteWork(PVOID pContext);
static DWORD SpiGpioIRQInterruptThread(LPVOID pContext);

SDHCD_HW_DEVICE   g_HWDevice;

/* Implemenation for SMC6400 SPI driver */
#ifdef HCD_SMDK6400
#include <s3c6400.h>    // for 6400
#include <s3c6400_dma_controller_macro.h>
#include <s3c6400_dma_controller.h>
#define S3C64XX_BASE_REG_PA_DMA1   S3C6400_BASE_REG_PA_DMA1
#define S3C64XX_BASE_REG_PA_GPIO   S3C6400_BASE_REG_PA_GPIO
#define S3C64XX_BASE_REG_PA_SYSCON S3C6400_BASE_REG_PA_SYSCON
#define S3C64XX_BASE_REG_PA_DMA0   S3C6400_BASE_REG_PA_DMA0
#define S3C64XX_BASE_REG_PA_SPI0   S3C6400_BASE_REG_PA_SPI0
#define S3C64XX_BASE_REG_PA_SPI1   S3C6400_BASE_REG_PA_SPI1
#define S3C64XX_GPIO_REG           S3C6400_GPIO_REG
#define S3C64XX_SPI_REG            S3C6400_SPI_REG 
#define S3C64XX_SYSCON_REG         S3C6400_SYSCON_REG
#define S3C64XX_DMAC_REG           S3C6400_DMAC_REG
#define S3C64XX_DMA_CH_REG         S3C6400_DMA_CH_REG
#define S3C64XX_ECLK               S3C6400_ECLK
#define S3C64XX_PCLK               S3C6400_PCLK
#elif defined(HCD_SMDK6410)
#include <s3c6410.h>    // for 6400
#include <s3c6410_dma_controller_macro.h>
#include <s3c6410_dma_controller.h>
#define S3C64XX_BASE_REG_PA_DMA1   S3C6410_BASE_REG_PA_DMA1
#define S3C64XX_BASE_REG_PA_GPIO   S3C6410_BASE_REG_PA_GPIO
#define S3C64XX_BASE_REG_PA_SYSCON S3C6410_BASE_REG_PA_SYSCON
#define S3C64XX_BASE_REG_PA_DMA0   S3C6410_BASE_REG_PA_DMA0
#define S3C64XX_BASE_REG_PA_SPI0   S3C6410_BASE_REG_PA_SPI0
#define S3C64XX_BASE_REG_PA_SPI1   S3C6410_BASE_REG_PA_SPI1
#define S3C64XX_GPIO_REG           S3C6410_GPIO_REG
#define S3C64XX_SPI_REG            S3C6410_SPI_REG 
#define S3C64XX_SYSCON_REG         S3C6410_SYSCON_REG
#define S3C64XX_DMAC_REG           S3C6410_DMAC_REG
#define S3C64XX_DMA_CH_REG         S3C6410_DMA_CH_REG
#define S3C64XX_ECLK               S3C6410_ECLK
#define S3C64XX_PCLK               S3C6410_PCLK
#else
#error "Not SMDK6410 nor SMDK6400"
#endif 

#include <DrvLib.h>
#include <bsp_cfg.h>
#include "oal_intr.h"

//#define TARGET_TJET 1 /* enable for TJET phone setting */

#if TARGET_TJET 
#undef S3C64XX_ECLK
#define S3C64XX_ECLK 96000000 /* TJET is using 96Mhz as EPLL */
#endif 

/* Configure the following define if any changed */
//#define ENABLE_SCATTER_DMA 1
//#define ENABLE_SPI_DEBUG 1
//#define ENABLE_INT_MODE 1
#define ENABLE_MASTER_CS 1     /* manual CS */
//#define HCD_EMULATE_DMA 1
#define SPI_WLAN_INTR IRQ_EINT10 /* IRQ_EINT4 */
#define USE_SPI_ID 1 /* SPI 1*/
#define SPI_CLOCK            EPLL_CLOCK /* PCLOCK(25), USB_HOST_CLOCK(48), EPLL_CLOCK(84.67)*/
#define MAX_BYTES_PER_DMA 2048
#define DMA_ALIGNMENT_BYTES 32
/* ----------------------------------------------*/

// Debug MSG Flags
#define SPI_MSG        0
#define SPI_INIT    1

#define WRITE_TIME_OT_CONSTANT    5000
#define WRITE_TIME_OUT_MULTIPLIER    1

#define READ_TIME_OUT_CONSTANT  5000
#define READ_TIME_OUT_MULTIPLIER    1

#ifdef ENABLE_MASTER_CS
#define MASTER_CS_ENABLE    pSPIregs->SLAVE_SEL = 0
#define MASTER_CS_DISABLE   pSPIregs->SLAVE_SEL = 1
#else
#define MASTER_CS_ENABLE
#define MASTER_CS_DISABLE
#endif 

#define TRAIL_CNT(n)    (((n)&0x3FF)<<19)

#if (USE_SPI_ID==0)
    #define SPI_IRQ_NUM         IRQ_SPI0
    #define SPI_POWER_ON        (1<<21)
    #define SPI_SCLK_ON         (1<<20)
    #define SPI_USBHOST_ON      (1<<22)
    #define SPI_ADDR            S3C64XX_BASE_REG_PA_SPI0
    #define SPI_DMA_TX          DMA_SPI0_TX
    #define SPI_DMA_RX          DMA_SPI0_RX
    #define SPI_TX_DATA_PHY_ADDR    0x7F00B018
    #define SPI_RX_DATA_PHY_ADDR    0x7F00B01C
    #define SPI_SETUP_GPIO(_r) do { \
    (_r)->GPCPUD = (_r)->GPCPUD & ~(0xFF<<0); \
    (_r)->GPCCON = (_r)->GPCCON & ~(0xFFFF<<0) | (2<<0) | (2<<4) | (2<<8) |(2<<12); } while (0)
#elif (USE_SPI_ID==1)
    #define SPI_IRQ_NUM         IRQ_SPI1
    #define SPI_POWER_ON        (1<<22)    // SPI1
    #define SPI_SCLK_ON         (1<<21)    // SPI1
    #define SPI_USBHOST_ON      (1<<23)    // SPI1 48
    #define SPI_ADDR            S3C64XX_BASE_REG_PA_SPI1
    #define SPI_DMA_TX          DMA_SPI1_TX
    #define SPI_DMA_RX          DMA_SPI1_RX
    #define SPI_TX_DATA_PHY_ADDR    0x7F00C018
    #define SPI_RX_DATA_PHY_ADDR    0x7F00C01C
    #define SPI_SETUP_GPIO(_r) do { \
    (_r)->GPCPUD = (_r)->GPCPUD & ~(0xFF00<<0); \
    (_r)->GPCCON = (_r)->GPCCON & ~(0xFFFF0000<<0) | (2<<16) | (2<<20) | (2<<24) |(2<<28); } while (0)
#else
    #error "No available SPI device"
#endif 

#define PFN_TO_PHYS_ADDR(_pfn) ( (_pfn) << UserKInfo[KINX_PFN_SHIFT] )

#define SPI_TRANSFER_TYPE (CPOL_FALLING|CPHA_FORMAT_B|SPI_MASTER)

#define S3C64XX_USBCLK      (48000000)
#define USB_SIG_MASK        (1<<16)
#define PCLOCK                (0)   /* 65.5 Mhz */
#define USB_HOST_CLOCK        (1)   /* 48Mhz */
#define EPLL_CLOCK            (2)   /* 84.666667MHz */

#define FIFO_SIZE             0x40
#define FIFO_HALF_SIZE        0x20

#define FIFO_FULL            0x40
#define FIFO_EMPTY            0x0

//#define    RX_TRIG_LEVEL        0x8
//#define    TX_TRIG_LEVEL        0x14
#define    RX_TRIG_LEVEL        01
#define    TX_TRIG_LEVEL        63

#define    HIGH_SPEED_MASK		(1<<6)
#define    HIGH_SPEED_DIS		(0<<6)
#define    HIGH_SPEED_EN		(1<<6)
#define    SW_RST               (1<<5)
#define    SPI_MASTER           (0<<4)
#define    SPI_SLAVE            (1<<4)

#define    CPOL_RISING          (0<<3)
#define    CPOL_FALLING         (1<<3)
#define    CPHA_FORMAT_A        (0<<2)
#define    CPHA_FORMAT_B        (1<<2)
#define    RX_CH_OFF            (0<<1)
#define    RX_CH_ON             (1<<1)
#define    TX_CH_OFF            (0<<0)
#define    TX_CH_ON             (1<<0)
           
#define    CLKSEL_PCLK          (0<<9)
#define    CLKSEL_USBCLK        (1<<9)
#define    CLKSEL_EPLL          (2<<9)
#define    ENCLK_DISABLE        (0<<8)
#define    ENCLK_ENABLE         (1<<8)

#define    CH_SIZE_BYTE         (0<<29)
#define    CH_SIZE_HALF         (1<<29)
#define    CH_SIZE_WORD         (2<<29)
#define    BUS_SIZE_BYTE        (0<<17)
#define    BUS_SIZE_HALF        (1<<17)
#define    BUS_SIZE_WORD        (2<<17)
#define    DMA_SINGLE           (0<<0)
#define    DMA_4BURST           (1<<0)
#define    RX_DMA_ON            (1<<2)
#define    TX_DMA_ON            (1<<1)
#define    MODE_DEFAULT         (0)

#define    INT_TRAILING        (1<<6)
#define    INT_RX_OVERRUN      (1<<5)
#define    INT_RX_UNDERRUN     (1<<4)
#define    INT_TX_OVERRUN      (1<<3)
#define    INT_TX_UNDERRUN     (1<<2)
#define    INT_RX_FIFORDY      (1<<1)
#define    INT_TX_FIFORDY      (1<<0)

#define    TX_DONE             (1<<21)
#define    TRAILCNT_ZERO       (1<<20)
#define    RX_OVERRUN          (1<<5)
#define    RX_UNDERRUN         (1<<4)
#define    TX_OVERRUN          (1<<3)
#define    TX_UNDERRUN         (1<<2)
#define    RX_FIFORDY          (1<<1)
#define    TX_FIFORDY          (1<<0)

#define    PACKET_CNT_EN       (1<<16)

#define    TX_UNDERRUN_CLR     (1<<4)
#define    TX_OVERRUN_CLR      (1<<3)
#define    RX_UNDERRUN_CLR     (1<<2)
#define    RX_OVERRUN_CLR      (1<<1)
#define    TRAILING_CLR        (1<<0)

#define    RX_HALF_SWAP        (1<<7)
#define    RX_BYTE_SWAP        (1<<6)
#define    RX_BIT_SWAP         (1<<5)
#define    RX_SWAP_EN          (1<<4)
#define    TX_HALF_SWAP        (1<<3)
#define    TX_BYTE_SWAP        (1<<2)
#define    TX_BIT_SWAP         (1<<1)
#define    TX_SWAP_EN          (1<<0)

#define    SPI_0NS_DELAY        (0x0)
#define    SPI_2NS_DELAY        (0x1)
#define    SPI_4NS_DELAY        (0x2)
#define    SPI_6NS_DELAY        (0x3)

#define MAX_DMA_LINK 32

typedef struct {
    volatile S3C64XX_GPIO_REG       *pGPIOregs;
    volatile S3C64XX_SPI_REG        *pSPIregs;  //    For HS-SPI
    volatile S3C64XX_SYSCON_REG     *pSYSCONregs;
    volatile S3C64XX_DMAC_REG       *pDMAC0regs;
    volatile S3C64XX_DMAC_REG       *pDMAC1regs;
    UINT32                      ClockCfg;    
    DWORD                       dwWlanSpiSysintr;
#ifdef ENABLE_INT_MODE
    DWORD                       dwSpiSysIntr;
    HANDLE                      hSpiEvent;
    HANDLE                      hSpiDoneEvent;
    HANDLE                      hSpiThread;
#endif 

#ifdef HCD_EMULATE_DMA
    HANDLE                      hRxDmaDoneEvent;
    HANDLE                      hRxDmaDoneThread;
#else
    DWORD                       dwRxDmaDoneSysIntr;
    HANDLE                      hRxDmaDoneEvent;
    HANDLE                      hRxDmaDoneThread;

    DWORD                       dwTxDmaDoneSysIntr;
    HANDLE                      hTxDmaDoneEvent;
    HANDLE                      hTxDmaDoneThread;
#endif 
    S3C64XX_SPI_REG             RestoreSPIregs; 
} SPI_CONTEXT, *PSPI_CONTEXT;

static DMA_CH_CONTEXT                  g_OutputDma;
static DMA_CH_CONTEXT                  g_InputDma;

#ifdef HCD_EMULATE_DMA
static void DoEmulatedDMA(PSDHCD_DEVICE    pDevice);
#endif;

#ifdef ENABLE_INT_MODE
DWORD ThreadForSpi(PSDHCD_HW_DEVICE pHWDevice)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG *pSPIregs = pSpi->pSPIregs;
    PSDHCD_DEVICE pDevice = pHWDevice->pDevice;    

    RETAILMSG(SPI_MSG,(TEXT("[SPI] ThreadForSpi thread is created \r\n")));
    do
    {
        WaitForSingleObject(pSpi->hSpiEvent, INFINITE);
        if (pSpi->dwSpiSysIntr==0) {
            break;
        }
        pSPIregs->SPI_INT_EN    =    0;
        InterruptDone(pSpi->dwSpiSysIntr);
        SetEvent(pSpi->hSpiDoneEvent);
        
    } while (1);
    return 0;
}
#endif /* ENABLE_INT_MODE */

static BOOL CheckDMAActive(DMA_CH_CONTEXT *pCtxt)
{
    volatile S3C64XX_DMA_CH_REG *pDMACHReg;    
    pDMACHReg = (S3C64XX_DMA_CH_REG *)pCtxt->pCHReg;

    if (pDMACHReg->Configuration & ACTIVE) {
        return TRUE;   
    }
    return FALSE;
}

#ifdef HCD_EMULATE_DMA
static DWORD ThreadForDmaEmulation(PSDHCD_HW_DEVICE pHWDevice)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    
    do
    {
        WaitForSingleObject(pSpi->hRxDmaDoneEvent, INFINITE);
        if (pHWDevice->ShutDown) {
            if (pHWDevice->pDevice->DMAHWTransferInProgress) {
                HcdDmaCompletion(pHWDevice->pDevice, SDIO_STATUS_CANCELED);    
            }
            break;
        }
        
        DoEmulatedDMA(pHWDevice->pDevice);
           
    } while(TRUE);
    
    return 0;
}
#else /* HCD_EMULATE_DMA */
static DWORD ThreadForRxDmaDone(PSDHCD_HW_DEVICE pHWDevice)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG *pSPIregs = pSpi->pSPIregs;
    PSDHCD_DEVICE pDevice = pHWDevice->pDevice;
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
#ifdef ENABLE_SPI_DEBUG
    DMA_INT_STATUS  dmaStatus;
#endif
    do
    {
        WaitForSingleObject(pSpi->hRxDmaDoneEvent, INFINITE);
        if (!g_InputDma.bValid) {
            break;
        }
        status = SDIO_STATUS_SUCCESS;
#ifdef ENABLE_SPI_DEBUG
        if (CheckDMAActive(&g_InputDma)) {
            RETAILMSG(1,(TEXT(" SPI RX transfer still in progress!!!  \r\n")));
            status = SDIO_STATUS_BUS_READ_ERROR;
        }

        /* check for DMA errors */
        dmaStatus =  DMA_get_interrupt_status(&g_InputDma);
                
        if (dmaStatus & ERR_INT_PEND) {
            RETAILMSG(1,(TEXT(" SPI RX transfer error: dma status = 0x%X  \r\n"), dmaStatus));
            status = SDIO_STATUS_BUS_READ_ERROR;       
        }
         
         if (!(dmaStatus & TC_INT_PEND)) {
            RETAILMSG(1,(TEXT(" SPI RX did not reach terminal count !\r\n")));
            status = SDIO_STATUS_BUS_READ_ERROR;       
        }
#endif /* ENABLE_SPI_DEBUG */
        if (pSPIregs->SPI_STATUS & (RX_OVERRUN | RX_UNDERRUN)) {
            RETAILMSG(1,(TEXT(" SPI RX transfer error: status = 0x%X\r\n"),pSPIregs->SPI_STATUS));
            pSPIregs->PENDING_CLEAR |= (0x1<<1 || 0x1<<2);
            status = SDIO_STATUS_BUS_READ_ERROR;    
        }

        DMA_channel_stop(&g_InputDma); 
        MASTER_CS_DISABLE;
        pSPIregs->PACKET_COUNT = 0;        

        InterruptDone(pSpi->dwRxDmaDoneSysIntr);
        DMA_clear_interrupt_mask(&g_InputDma);
        if (SDIO_SUCCESS(status)) {
#ifdef ENABLE_SCATTER_DMA
            if (!pHWDevice->CommonBufferDMA) {       
                PVOID addr = pDevice->pCurrentBuffer;
                pHWDevice->CommonBufferDMA = TRUE;
                UnlockPages(addr, pDevice->CurrentTransferLength);
                CacheRangeFlush(addr, pDevice->CurrentTransferLength, CACHE_SYNC_DISCARD );
            } 
            else
#endif
            {
                /* copy common buffer back for RX */                                     
                HcdCommonBufferCopy(pDevice->CurrentDmaWidth,
                                    pDevice->pCurrentBuffer,
                                    pHWDevice->pDmaCommonBuffer,
                                    pDevice->CurrentTransferLength,
                                    pDevice->HostDMABufferCopyMode);            
            }                
        }    

        HcdDmaCompletion(pDevice, status);      
    } while(TRUE);
    return 0;
}

static DWORD ThreadForTxDmaDone(PSDHCD_HW_DEVICE pHWDevice)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG *pSPIregs = pSpi->pSPIregs;
    PSDHCD_DEVICE pDevice = pHWDevice->pDevice; 
    SDIO_STATUS     status = SDIO_STATUS_SUCCESS;
#ifdef ENABLE_SPI_DEBUG
    DMA_INT_STATUS  dmaStatus;
#endif    
    do
    {
        WaitForSingleObject(pSpi->hTxDmaDoneEvent, INFINITE);
        if (!g_OutputDma.bValid) {
            break;
        }
        status = SDIO_STATUS_SUCCESS;
#ifdef ENABLE_SPI_DEBUG
        /* check for DMA errors */
        dmaStatus =  DMA_get_interrupt_status(&g_OutputDma);
                
        if (dmaStatus & ERR_INT_PEND) {
            RETAILMSG(1,(TEXT(" SPI TX transfer error: dma status = 0x%X  \r\n"), dmaStatus));
            status = SDIO_STATUS_BUS_WRITE_ERROR;       
        }
         
         if (!(dmaStatus & TC_INT_PEND)) {
            RETAILMSG(1,(TEXT(" SPI TX did not reach terminal count !\r\n")));
            status = SDIO_STATUS_BUS_WRITE_ERROR;      
        }
#endif /* ENABLE_SPI_DEBUG */
         /* check for SPI controller errors */      
        if (pSPIregs->SPI_STATUS & (TX_OVERRUN | TX_UNDERRUN)) {
            RETAILMSG(1,(TEXT(" SPI TX transfer error: status = 0x%X  \r\n"),pSPIregs->SPI_STATUS ));
            status = SDIO_STATUS_BUS_WRITE_ERROR;    
        }
        
        /* Polling for pending tx data juts in case. It is necessary due to 6400 tx dma bug */
        if (!(pSPIregs->SPI_STATUS & TX_DONE) || ((pSPIregs ->SPI_STATUS>>6) & 0x7f)>0) { 
            ULONG waitCount = 1000000;
            do {
                if ((pSPIregs->SPI_STATUS & TX_DONE) && !((pSPIregs->SPI_STATUS>>6) & 0x7f)) {
                    break;
                }
                --waitCount;
            } while (waitCount);
            if (waitCount == 0) {
                RETAILMSG (TRUE, (TEXT("Tx Dma Done pending data %d timeout!!!\r\n"),
                                  ((pSPIregs ->SPI_STATUS>>6) & 0x7f)));
                status = SDIO_STATUS_IO_TIMEOUT;
            }
        }

        DMA_channel_stop(&g_OutputDma); 
        
        MASTER_CS_DISABLE;
        InterruptDone(pSpi->dwTxDmaDoneSysIntr);
        DMA_clear_interrupt_mask(&g_OutputDma);
#ifdef ENABLE_SCATTER_DMA
        if (!pHWDevice->CommonBufferDMA) {
            pHWDevice->CommonBufferDMA = TRUE;
            UnlockPages(pDevice->pCurrentBuffer, pDevice->CurrentTransferLength);        
        }
#endif
        HcdDmaCompletion(pDevice, status);
    } while(TRUE);
    
    
    return 0;
}
#endif /* HCD_EMULATE_DMA */

static void HW_DeInit(PSDHCD_HW_DEVICE pHWDevice)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    if (!pSpi) {
        return;
    }
    //DMA Channel Stop
    DMA_channel_stop(&g_OutputDma);
    DMA_channel_stop(&g_InputDma);
    DMA_release_channel(&g_OutputDma);
    DMA_release_channel(&g_InputDma);

    /* make sure interrupt asssociated with the event is disabled */
#if !defined(HCD_EMULATE_DMA)
    if (pSpi->dwTxDmaDoneSysIntr != 0) {
        InterruptDisable(pSpi->dwTxDmaDoneSysIntr);  
        KernelIoControl(IOCTL_HAL_RELEASE_SYSINTR, &pSpi->dwTxDmaDoneSysIntr, sizeof(DWORD), NULL, 0, NULL);
        pSpi->dwTxDmaDoneSysIntr = 0;
    }
    if (pSpi->dwRxDmaDoneSysIntr != 0) {
        InterruptDisable(pSpi->dwRxDmaDoneSysIntr);  
        KernelIoControl(IOCTL_HAL_RELEASE_SYSINTR, &pSpi->dwRxDmaDoneSysIntr, sizeof(DWORD), NULL, 0, NULL);
        pSpi->dwRxDmaDoneSysIntr = 0;
    }
#endif /* !HCD_EMULATE_DMA */

#ifdef ENABLE_INT_MODE
    if (pSpi->dwSpiSysIntr != 0) {
        InterruptDisable(pSpi->dwSpiSysIntr);  
        KernelIoControl(IOCTL_HAL_RELEASE_SYSINTR, &pSpi->dwSpiSysIntr, sizeof(DWORD), NULL, 0, NULL);
        pSpi->dwSpiSysIntr = 0;
    }

    if (pSpi->hSpiEvent != NULL) {
        if (pSpi->hSpiThread != NULL) {
                /* wake IST */
            SetEvent(pSpi->hSpiEvent);
                /* wait for IST to exit */
            WaitForSingleObject(pSpi->hSpiThread, INFINITE);
            CloseHandle(pSpi->hSpiThread);
            pSpi->hSpiThread = NULL;
        }
        CloseHandle(pSpi->hSpiEvent);
        pSpi->hSpiEvent = NULL;
    }

    if (pSpi->hSpiDoneEvent != NULL) {
        CloseHandle(pSpi->hSpiDoneEvent);
    }
#endif  /* ENABLE_INT_MODE  */

    /* Terminate the Dma thread */
#if !defined(HCD_EMULATE_DMA)
    if (pSpi->hTxDmaDoneEvent != NULL) {
        if (pSpi->hTxDmaDoneThread != NULL) {
                /* wake IST */
            SetEvent(pSpi->hTxDmaDoneEvent);
                /* wait for IST to exit */
            WaitForSingleObject(pSpi->hTxDmaDoneThread, INFINITE);
            CloseHandle(pSpi->hTxDmaDoneThread);
            pSpi->hTxDmaDoneThread = NULL;
        }
        CloseHandle(pSpi->hTxDmaDoneEvent);
        pSpi->hTxDmaDoneEvent = NULL;
    }
#endif /* !HCD_EMULATE_DMA */

    if (pSpi->hRxDmaDoneEvent != NULL) {
        if (pSpi->hRxDmaDoneThread != NULL) {
                /* wake IST */
            SetEvent(pSpi->hRxDmaDoneEvent);
                /* wait for IST to exit */
            WaitForSingleObject(pSpi->hRxDmaDoneThread, INFINITE);
            CloseHandle(pSpi->hRxDmaDoneThread);
            pSpi->hRxDmaDoneThread = NULL;
        }
        CloseHandle(pSpi->hRxDmaDoneEvent);
        pSpi->hRxDmaDoneEvent = NULL;
    }

    if (pSpi->dwWlanSpiSysintr!=0) {
        KernelIoControl(IOCTL_HAL_RELEASE_SYSINTR, &pSpi->dwWlanSpiSysintr, sizeof(DWORD), NULL, 0, NULL);
        pSpi->dwWlanSpiSysintr = 0;
    }

    //Close Handle
    if (pHWDevice->pDmaCommonBuffer) {
        PHYSICAL_ADDRESS PhysicalAddress;
        PhysicalAddress.LowPart = (DWORD)pHWDevice->pDmaCommonPhysicalBuffer;
        HalFreeCommonBuffer(0, 0, PhysicalAddress, (PVOID)pHWDevice->pDmaCommonBuffer, FALSE);
        pHWDevice->pDmaCommonBuffer = NULL;
        pHWDevice->pDmaCommonPhysicalBuffer = NULL;
    }

    if (pSpi->pGPIOregs)
    {
        DrvLib_UnmapIoSpace((PVOID)pSpi->pGPIOregs);
        pSpi->pGPIOregs = NULL;
    }

    if (pSpi->pSPIregs)
    {
        DrvLib_UnmapIoSpace((PVOID)pSpi->pSPIregs);
        pSpi->pSPIregs = NULL;
    }

    if (pSpi->pDMAC0regs)
    {
        DrvLib_UnmapIoSpace((PVOID)pSpi->pDMAC0regs);
        pSpi->pDMAC0regs = NULL;
    }

    if (pSpi->pDMAC1regs)
    {
        DrvLib_UnmapIoSpace((PVOID)pSpi->pDMAC1regs);
        pSpi->pDMAC1regs = NULL;
    }

    if (pSpi->pSYSCONregs)
    {
        DrvLib_UnmapIoSpace((PVOID)pSpi->pSYSCONregs);
        pSpi->pSYSCONregs = NULL;
    }
    LocalFree(pSpi);
    pHWDevice->pSpiContext = NULL;
}

static void HW_SetupWlanGpioIntr(PSPI_CONTEXT pSpi)
{
    volatile S3C64XX_GPIO_REG *pGPIOregs = pSpi->pGPIOregs;
    //Set GPIO for WLAN Interrupt 
#if (SPI_WLAN_INTR == IRQ_EINT4)

    /* disable interrupt first */
    pGPIOregs->EINT34MASK |= ( 1<<21 ); /* disable EINT4[5] interrupt */
    pGPIOregs->EINT0MASK  |= ( 1<<4 );  /* disable EINT4 interrupt */
    
    /* setup interrupt Camera I/F data0 pin as interrupt */
    pGPIOregs->GPFCONSLP = pGPIOregs->GPFCONSLP & ~(0xC00<<0) | (3<<10); /* Previous state */
    pGPIOregs->GPFPUDSLP = pGPIOregs->GPFPUDSLP & ~(0xC00<<0) | (2<<10); /* pull-up enabled */
    pGPIOregs->GPFPUD = pGPIOregs->GPFPUD & ~(0xC00<<0) | (2<<10);       /* pull-up enabled */
    pGPIOregs->GPFCON = pGPIOregs->GPFCON & ~(0xC00<<0) | (3<<10);       /* GPF5 -> External Interrupt Group 4[5] */

    /* setup EINT4 pin as interrupt */
    pGPIOregs->GPNCON = pGPIOregs->GPNCON & ~(3<<8) | (2<<8);
    pGPIOregs->GPNPUD = pGPIOregs->GPNPUD & ~(3<<8) | (2<<8);   

    /* Interrupt Siganl Method and Filtering */
    pGPIOregs->EINT34CON &= ~( (1<<22) | (1<<21) | (1<<20) ); /* active low for EINT4[7:4] */
    pGPIOregs->EINT0CON0 &= ~( 7<<8 ); /* EINT4, EINT5 active low */    
    pGPIOregs->EINT34FLTCON &= ~(1<<23); /* disable filter for EINT4[7-0] */
    pGPIOregs->EINT0FLTCON0 &= ~(1<<23); /* disable filter for EINT4, 5 */
    
    // Clear Interrupt Pending
    pGPIOregs->EINT0PEND |= (1<<4);
    pGPIOregs->EINT34PEND &= ~( 1 << (16+5));

    /* enable the interrupt */
    pGPIOregs->EINT0MASK  &= ~( 1<<4);
    pGPIOregs->EINT34MASK &= ~( 1<<21 ); /* Enable EINT4[5] interrupt */
#elif (SPI_WLAN_INTR == IRQ_EINT5)
    /* disable interrupt first */
    //pGPIOregs->EINT56MASK |= ( 0x7f<<0 ); /* disable EINT5[x] interrupt */
    pGPIOregs->EINT0MASK |= (0x1<<5);    // Mask EINT5    
    
    /* setup EINT5 pin as interrupt */
    pGPIOregs->GPNCON = pGPIOregs->GPNCON & ~(3<<10) | (2<<10);
    pGPIOregs->GPNPUD = pGPIOregs->GPNPUD & ~(3<<10) | (2<<10);    
        
    /* Interrupt Siganl Method and Filtering */
    pGPIOregs->EINT0CON0 &= ~(0x7<<8);
    pGPIOregs->EINT0FLTCON0 &= ~(0x1<<23);

    //pGPIOregs->GPGPUD = pGPIOregs->GPGPUD & ~(0x3fff<<0) | (0x2aaa);       /* pull-up enabled */
    //pGPIOregs->GPGCON = pGPIOregs->GPGCON & ~(0xfffffff<<0) | (0x7777777<<0);       /* GPF5 -> External Interrupt Group 4[5] */
    //pGPIOregs->GPGCONSLP = pGPIOregs->GPGCONSLP & ~(0x3fff<<0) | (0x3fff); /* Previous state */
    //pGPIOregs->GPGPUDSLP = pGPIOregs->GPGPUDSLP & ~(0x3fff<<0) | (0x2aaa); /* pull-up enabled */

    //pGPIOregs->EINT56CON &= ~( 0x7f<<0 ); /* active low for EINT5[7:4] */
    //pGPIOregs->EINT56FLTCON &= ~(1<<7); /* disable filter for EINT4[7-0] */

    pGPIOregs->EINT0PEND |= (0x1<<5);        // Clear pending EINT5[x]  
    pGPIOregs->EINT0MASK &= ~(0x1<<5);    // Enable EINT5[x] interrupt

    //pGPIOregs->EINT56PEND &= ~( 0x7f<<0 );
    //pGPIOregs->EINT56MASK &= ~( 0x7f<<0 ); /* disable EINT4[5] interrupt */
#elif (SPI_WLAN_INTR == IRQ_EINT10)
    /* disable interrupt first */
    pGPIOregs->EINT0MASK |= (0x1<<10);    // Mask EINT10
    /* setup EINT10 pin as interrupt */
    pGPIOregs->GPNCON = pGPIOregs->GPNCON & ~(3<<20) | (2<<20);
    pGPIOregs->GPNPUD = pGPIOregs->GPNPUD & ~(3<<20) | (2<<20);    
    /* Interrupt Siganl Method and Filtering */
    pGPIOregs->EINT0CON0 &= ~(0x7<<20);
    pGPIOregs->EINT0FLTCON1 &= ~(0x1<<15);
    /*Clear Interrupt Pending*/
    pGPIOregs->EINT0PEND |= (0x1<<10);        // Clear pending EINT10
    /* enable the interrupt */
    pGPIOregs->EINT0MASK &= ~(0x1<<10);    // Enable EINT10 interrupt
#elif (SPI_WLAN_INTR == IRQ_EINT13)   
    /* disable interrupt first */
    pGPIOregs->EINT0MASK |= (0x1<<13);    // Mask EINT13
    /* setup EINT10 pin as interrupt */
    pGPIOregs->GPNCON = pGPIOregs->GPNCON & ~(3<<26) | (2<<26);
    pGPIOregs->GPNPUD = pGPIOregs->GPNPUD & ~(3<<26) | (2<<26);    
    /* Interrupt Siganl Method and Filtering */
    pGPIOregs->EINT0CON0 &= ~(0x7<<24);
    pGPIOregs->EINT0FLTCON1 &= ~(0x1<<23);
    /*Clear Interrupt Pending*/
    pGPIOregs->EINT0PEND |= (0x1<<13);        // Clear pending EINT10
    /* enable the interrupt */
    pGPIOregs->EINT0MASK &= ~(0x1<<13);    // Enable EINT10 interrupt 
#else
#error "Please change the following setting if SPI_WLAN_INTR != IRQ_EINT4"
#endif 
}

static SDIO_STATUS HW_Init(PSDHCD_HW_DEVICE pHWDevice)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)LocalAlloc(LPTR, sizeof(SPI_CONTEXT)); 
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    DWORD dwHwIntr;
    DWORD dwThreadId;
    HW_UsecDelay(pHWDevice->pDevice, 100);
    do {
        if ( !pSpi )
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] Can't not allocate for SPI Context\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
        pHWDevice->pSpiContext = pSpi;

        // GPIO Virtual alloc
        pSpi->pGPIOregs = (volatile S3C64XX_GPIO_REG *)DrvLib_MapIoSpace(S3C64XX_BASE_REG_PA_GPIO, sizeof(S3C64XX_GPIO_REG), FALSE);
        if (pSpi->pGPIOregs == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] For pGPIOregs: DrvLib_MapIoSpace failed!\r\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        // HS-SPI Virtual alloc (SPI-1)
        pSpi->pSPIregs = (volatile S3C64XX_SPI_REG *)DrvLib_MapIoSpace(SPI_ADDR, sizeof(S3C64XX_SPI_REG), FALSE);
        if (pSpi->pSPIregs == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] For pSPIregs: DrvLib_MapIoSpace failed!\r\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        // Syscon Virtual alloc
        pSpi->pSYSCONregs = (volatile S3C64XX_SYSCON_REG *)DrvLib_MapIoSpace(S3C64XX_BASE_REG_PA_SYSCON, sizeof(S3C64XX_SYSCON_REG), FALSE);
        if (pSpi->pSYSCONregs == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] For pSYSCONregs: DrvLib_MapIoSpace failed!\r\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        // DMAC0 Virtual alloc
        pSpi->pDMAC0regs = (volatile S3C64XX_DMAC_REG *)DrvLib_MapIoSpace(S3C64XX_BASE_REG_PA_DMA0, sizeof(S3C64XX_DMAC_REG), FALSE);
        if (pSpi->pDMAC0regs == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] For pDMAC0regs: DrvLib_MapIoSpace failed!\r\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        // DMAC1 Virtual alloc
        pSpi->pDMAC1regs = (volatile S3C64XX_DMAC_REG *)DrvLib_MapIoSpace(S3C64XX_BASE_REG_PA_DMA1, sizeof(S3C64XX_DMAC_REG), FALSE);
        if (pSpi->pDMAC1regs == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] For pDMAC1regs: DrvLib_MapIoSpace failed!\r\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }

        if (DMA_SUCCESS!=DMA_initialize_register_address((void *)pSpi->pDMAC0regs, (void *)pSpi->pDMAC1regs, (void *)pSpi->pSYSCONregs))
        {
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
        // Request DMA Channel
        // DMA context have Virtual IRQ Number of Allocated DMA Channel
        // You Should initialize DMA Interrupt Thread after "Request DMA Channel"
        if (!DMA_request_channel(&g_OutputDma, SPI_DMA_TX) || !DMA_request_channel(&g_InputDma, SPI_DMA_RX)) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        } else {
            DMA_ADAPTER_OBJECT Adapter;
            PHYSICAL_ADDRESS physicalAddress;
            memset(&Adapter, 0, sizeof(DMA_ADAPTER_OBJECT));
            Adapter.ObjectSize = sizeof(DMA_ADAPTER_OBJECT);
            Adapter.InterfaceType = Internal;
            pHWDevice->CommonBufferDMA = TRUE;
            pHWDevice->pDmaCommonBuffer = HalAllocateCommonBuffer(&Adapter, MAX_BYTES_PER_DMA, &physicalAddress, FALSE);
            if (pHWDevice->pDmaCommonBuffer==NULL) {
                status = SDIO_STATUS_NO_RESOURCES;
                break;
            }
            pHWDevice->pDmaCommonPhysicalBuffer = (UINT8*)physicalAddress.LowPart;
            pHWDevice->pDevice->MaxBytesPerDMARequest = MAX_BYTES_PER_DMA;
        }

#ifdef ENABLE_INT_MODE
        //Spi ISR
        pSpi->dwSpiSysIntr = SYSINTR_NOP;
        dwHwIntr = SPI_IRQ_NUM;        //HS-SPI

        pSpi->hSpiEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        pSpi->hSpiDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

        if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwHwIntr, sizeof(DWORD), &pSpi->dwSpiSysIntr, sizeof(DWORD), NULL))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] Failed to request the SPI sysintr.\n")));
            pSpi->dwSpiSysIntr = SYSINTR_UNDEFINED;
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }

        if (!InterruptInitialize(pSpi->dwSpiSysIntr, pSpi->hSpiEvent, NULL, 0))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] SPI Interrupt Initialization failed!!!\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }

        pSpi->hSpiThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ThreadForSpi, (LPVOID)pHWDevice, 0, (LPDWORD)&dwThreadId);
        if (pSpi->hSpiThread == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] SPI ISR Thread creation error!!!\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
#endif /* ENABLE_INT_MODE */

#if !defined(HCD_EMULATE_DMA)
        //Tx DMA Done ISR
        pSpi->dwTxDmaDoneSysIntr = SYSINTR_NOP;
        dwHwIntr = g_OutputDma.dwIRQ;
        pSpi->hTxDmaDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (NULL == pSpi->hTxDmaDoneEvent) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }     
    
        if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwHwIntr, sizeof(DWORD), &pSpi->dwTxDmaDoneSysIntr, sizeof(DWORD), NULL))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] Failed to request the SPI_DMA sysintr.\n")));
            pSpi->dwTxDmaDoneSysIntr = SYSINTR_UNDEFINED;
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        if (!InterruptInitialize(pSpi->dwTxDmaDoneSysIntr, pSpi->hTxDmaDoneEvent, NULL, 0))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] DMA Interrupt Initialization failed!!!\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
        
        pSpi->hTxDmaDoneThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ThreadForTxDmaDone, (LPVOID)pHWDevice, 0, (LPDWORD)&dwThreadId);
        if (pSpi->hTxDmaDoneThread == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] SPI Dma Thread creation error!!!\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        //Rx DMA Done ISR
        pSpi->dwRxDmaDoneSysIntr = SYSINTR_NOP;
        dwHwIntr = g_InputDma.dwIRQ;
    
        pSpi->hRxDmaDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (NULL == pSpi->hRxDmaDoneEvent) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }     
    
        if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwHwIntr, sizeof(DWORD), &pSpi->dwRxDmaDoneSysIntr, sizeof(DWORD), NULL))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] Failed to request the SPI_DMA sysintr.\n")));
            pSpi->dwRxDmaDoneSysIntr = SYSINTR_UNDEFINED;
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        if (!InterruptInitialize(pSpi->dwRxDmaDoneSysIntr, pSpi->hRxDmaDoneEvent, NULL, 0))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] DMA Interrupt Initialization failed!!!\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    
        pSpi->hRxDmaDoneThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ThreadForRxDmaDone, (LPVOID)pHWDevice, 0, (LPDWORD)&dwThreadId);
#else   /* HCD_EMULATE_DMA */
        pSpi->hRxDmaDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (NULL == pSpi->hRxDmaDoneEvent) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        } 
        pSpi->hRxDmaDoneThread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)ThreadForDmaEmulation, (LPVOID)pHWDevice, 0, (LPDWORD)&dwThreadId);
#endif  /* HCD_EMULATE_DMA */
        if (pSpi->hRxDmaDoneThread == NULL)
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] SPI Dma Thread creation error!!!\n")));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }

        dwHwIntr = SPI_WLAN_INTR;
        if (!KernelIoControl(IOCTL_HAL_REQUEST_SYSINTR, &dwHwIntr, sizeof(DWORD), &pSpi->dwWlanSpiSysintr, sizeof(DWORD), NULL))
        {
            RETAILMSG(SPI_INIT,(TEXT("[SPI] Failed to request the SPI_WLAN sysintr.\n")));
            pSpi->dwWlanSpiSysintr = SYSINTR_UNDEFINED;
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
    } while (0);

    if (status!=SDIO_STATUS_SUCCESS)
    {            
        return status;
    }

    //Set GPIO for MISO, MOSI, SPICLK, SS
    SPI_SETUP_GPIO(pSpi->pGPIOregs);

    HW_SetupWlanGpioIntr(pSpi);

    //Configure HS-SPI Port Drive Strength
	//  pSpi->pGPIOregs->SPCON = pSpi->pGPIOregs->SPCON & ~(0x3<<28) | (2<<28); /* 7mA */
    pSpi->pGPIOregs->SPCON = pSpi->pGPIOregs->SPCON & ~(0x3<<28) | (3<<28); /* 9mA */

    // SPI Clock On
    pSpi->pSYSCONregs->PCLK_GATE |= SPI_POWER_ON;
#if (SPI_CLOCK == EPLL_CLOCK)
    pSpi->pSYSCONregs->SCLK_GATE |= SPI_SCLK_ON;
#elif (SPI_CLOCK == USB_HOST_CLOCK)
    pSpi->pSYSCONregs->SCLK_GATE |= SPI_USBHOST_ON;
    pSpi->pSYSCONregs->OTHERS |= USB_SIG_MASK;
#endif

    /* it is better to reset all SPI registers */
    pSpi->pSPIregs->CH_CFG |= SW_RST; //Reset    
    Sleep(5);
    pSpi->pSPIregs->CH_CFG &= ~SW_RST; 
    pSpi->pSPIregs->CLK_CFG = 0;
    pSpi->pSPIregs->MODE_CFG = 0;
    pSpi->pSPIregs->SLAVE_SEL = 1; /* inactive */
    pSpi->pSPIregs->SPI_INT_EN = 0;
    pSpi->pSPIregs->PACKET_COUNT = 0;
    pSpi->pSPIregs->PENDING_CLEAR = 0x1f;
#ifndef ENABLE_MASTER_CS
    pSpi->pSPIregs->SLAVE_SEL = (1<<1);
#else
    pSpi->pSPIregs->SLAVE_SEL &= ~(1<<1);
#endif 

    pSpi->RestoreSPIregs = *pSpi->pSPIregs;
    HW_PowerUpDown(pHWDevice, TRUE);
    return status;
}


/* END of SMC6400 */

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
        //pDevice->OperationalClock = 12000000;  /* 12 mhz, usb/4 */
        pDevice->OperationalClock = 21166666;  /* 21.1 mhz, epll/4 */
        //pDevice->OperationalClock = 42333333;  /* 42.3 mhz, epll/2 */
        pDevice->Hcd.MaxBytesPerBlock = MAX_BYTES_PER_DMA;  /* used as a hint to indicate max size of common buffer */
        pDevice->Hcd.MaxBlocksPerTrans = 1;    /* must be one*/
        pDevice->Hcd.MaxClockRate = 48000000;  /* 48 Mhz */
        pDevice->PowerUpDelay = 100;
            /* set all the supported frame widths the controller can do
             * 8/16/24/32 bit frames */        
        pDevice->SpiHWCapabilitiesFlags = HW_SPI_FRAME_WIDTH_8   | 
                                          HW_SPI_FRAME_WIDTH_16  |                                     
                                          //HW_SPI_FRAME_WIDTH_24 |
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
        if ( (status=HW_Init(pHWDevice))!=SDIO_STATUS_SUCCESS) {
            break;
        }
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
        pHWDevice->SysIntrSPIGpioIRQ = ((PSPI_CONTEXT)pHWDevice->pSpiContext)->dwWlanSpiSysintr;

        /* TODO : uncomment the following to associate the GPIO IRQ to the interrupt event :*/ 
        if (!InterruptInitialize(pHWDevice->SysIntrSPIGpioIRQ,
                                 pHWDevice->hIstEventSPIGpioIRQ,
                                 NULL,
                                 0)) {
            DBG_PRINT(SDDBG_ERROR,("SPI HCD: Failed to initialize Interrupt! \n"));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
                
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
        RETAILMSG(1, (TEXT("SPI HCD:SPI GPIO thread %p sysirq %d phyirq %d\n"), pHWDevice->hIstSPIGpioIRQ, pHWDevice->SysIntrSPIGpioIRQ, SPI_WLAN_INTR));
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
        PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
        DBG_PRINT(SDDBG_ERROR, ("SPI - HCD ready! \n"));
    }
    
    DBG_PRINT(SDDBG_TRACE, ("-SPI HCD: Setup - status : %d\n", status));
    return SDIO_SUCCESS(status) ? pHWDevice : NULL;  
}    

void CleanupSPIHW(SDHCD_HW_DEVICE *pHWDevice)
{
    pHWDevice->pDevice->ShuttingDown = TRUE;
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
    HW_DeInit(pHWDevice);

    /*********************************/
    
    ZERO_OBJECT(g_HWDevice);
    DBG_PRINT(SDDBG_TRACE, ("SPI HCD: CleanupDevice\n"));
}

typedef struct ModeParams {
    UINT32 mask;
    UINT32 mode;
    UINT32 bytes; 
    TRANSFER_UNIT dmaUnit;
} ModeParams;

static const ModeParams gModeParams[] = {
    {       0xff, (CH_SIZE_BYTE|BUS_SIZE_BYTE), 1, BYTE_UNIT }, 
    {     0xffff, (CH_SIZE_HALF|BUS_SIZE_HALF), 2, HWORD_UNIT }, 
    {   0xffffff, 0, 0, 0 }, 
    { 0xffffffff, (CH_SIZE_WORD|BUS_SIZE_WORD), 4, WORD_UNIT }, 
};

 /* map the request buffer to DMA
 * this function builds the scatter gather list which the hardware layer can use
 * to translate into DMA descriptor entries.  The hardware-specific layer is required to
 * perform cache/bus sync operations to make the buffer dma-coherent */
static BOOL HcdMapCurrentRequestBuffer(PSDHCD_DEVICE pDevice, DMA_CH_CONTEXT *pDmaCtx, 
                                       UINT src, UINT dst, BURST_SIZE burstSize,
                                       ADDRESS_UPDATE srcUpdate, ADDRESS_UPDATE dstUpdate, 
                                       int fOptions)
{
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)GET_HW_DEVICE(pDevice)->pSpiContext;
    const ModeParams *param = &gModeParams[pDevice->CurrentDmaWidth];  
    UINT32 vaddress = (UINT32)pDevice->pCurrentBuffer;
    UINT *pAddr = (srcUpdate==INCREASE) ? &src : &dst;
    DWORD byteCount  = (pDevice->CurrentTransferLength & 0xFFFFF);
    DWORD dwMaximumPages = COMPUTE_PAGES_SPANNED(vaddress, byteCount);
    DWORD pfns[32];
    DWORD pageSize = PAGE_SIZE;
    int LLICount = pDmaCtx->LLICount;        
    volatile S3C64XX_DMA_CH_REG *pDMACHReg = (S3C64XX_DMA_CH_REG *)pDmaCtx->pCHReg;
    if (!dwMaximumPages || dwMaximumPages>MAX_DMA_LINK) {
        goto cleanup_exit;
    }

    if (LockPages((PVOID)vaddress, byteCount, pfns, fOptions)) {      
        UINT offset=vaddress % pageSize;
        UINT length = pageSize - offset;
        UINT32 Control0;
        *pAddr = PFN_TO_PHYS_ADDR(pfns[0]) + offset;
        if( length > byteCount ) {
            length = byteCount;
        }       
        DMA_initialize_channel(pDmaCtx, TRUE);
        Control0 = (srcUpdate<<26) |(pDmaCtx->SrcAHBM<<24)|(param->dmaUnit<<18)|(burstSize<<12)|
                              (dstUpdate<<27) |(pDmaCtx->DstAHBM<<25)|(param->dmaUnit<<21)|(burstSize<<15);
        pDMACHReg->SrcAddr = src;
        pDMACHReg->DestAddr = dst;
        pDMACHReg->Control0 = Control0;
        pDMACHReg->Control1 = length / param->bytes;           
        if (--dwMaximumPages == 0) {
            pDMACHReg->Control0 |= TCINT_ENABLE;
            pDmaCtx->LLICount = LLICount;
        } else {
            DWORD i;
            DMA_LLI_ENTRY *pLLIEntry;
            byteCount -= length;
            if (LLICount>=(int)dwMaximumPages) {
                pDmaCtx->LLICount = LLICount;
            } else {
                if (DMA_initialize_LLI(pDmaCtx, dwMaximumPages)!=DMA_SUCCESS) {
                    goto err_exit;
                }
            }

            pLLIEntry = (DMA_LLI_ENTRY *)(pDmaCtx->LLIVirAddr);
            for (i=0; byteCount>0; ++i, pLLIEntry+=sizeof(DMA_LLI_ENTRY)) {
                length = (pageSize>byteCount) ? byteCount : pageSize;
                *pAddr = PFN_TO_PHYS_ADDR(pfns[i+1]);
                pLLIEntry->SrcAddr = src;
                pLLIEntry->DestAddr = dst;
                pLLIEntry->Control0 = Control0;
                pLLIEntry->Control1 = length / param->bytes;
                if (i+1<dwMaximumPages) {
                    pLLIEntry->LLI = NEXT_LLI_ITEM((UINT)(pLLIEntry+sizeof(DMA_LLI_ENTRY))) | pDmaCtx->LLIAHBM;
                } else {
                    pLLIEntry->LLI = 0;
                    pLLIEntry->Control0 |= TCINT_ENABLE;
                }
                byteCount -= length;
            }
            if ( DMA_set_initial_LLI(pDmaCtx, 0)!=DMA_SUCCESS) {
                    goto err_exit;
            }
        }
        GET_HW_DEVICE(pDevice)->CommonBufferDMA = FALSE;
        return TRUE;
    } 
    return FALSE;
err_exit:
    UnlockPages(pDevice->pCurrentBuffer, pDevice->CurrentTransferLength);
    pDMACHReg->LLI = 0;		// Disable LLI
cleanup_exit:
    if (!(pDMACHReg->Control0 & TCINT_ENABLE)) {
        pDMACHReg->Control0 |= TCINT_ENABLE;
    }
    return FALSE;
}

/* set up SPI host controller DMA */
SDIO_STATUS HW_SpiSetUpDMA(PSDHCD_DEVICE    pDevice)
{
#ifdef HCD_EMULATE_DMA   
    return SDIO_STATUS_PENDING;
#else /* HCD_EMULATE_DMA */
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG    *pSPIregs = pSpi->pSPIregs;  
    const ModeParams *param;
    UINT32 dmaMode, burstSize, packetCount;
    DWORD dwDmaLen  = (pDevice->CurrentTransferLength & 0xFFFFF);
    if (dwDmaLen == 0 || pDevice->CurrentDmaWidth == ATH_TRANS_DS_24) {
        return SDIO_STATUS_INVALID_PARAMETER;
    }

    /* TODO see PSDHCD_DEVICE definition to get buffer variables to do 
     * the DMA transfer .  
     * 
     * Setup DMA hardware to do the transfer (direction, length, common buffer or direct)
     * If the driver performs common-buffer DMA, the hardware layer is responsible
     * for copying the data to/from pCurrentBuffer (see below) */

    pSPIregs->CH_CFG |= SW_RST;
    pSPIregs->CH_CFG = SPI_TRANSFER_TYPE; /* Master Mode and disable reset */
    pSPIregs->CLK_CFG = pSpi->ClockCfg;
           
    param = &gModeParams[pDevice->CurrentDmaWidth];    

    packetCount = dwDmaLen / param->bytes;
    if (packetCount & 3) {
        dmaMode = DMA_SINGLE;
        burstSize = BURST_1;
    } else {
        dmaMode = DMA_4BURST;
        burstSize = BURST_4;
    }
    if (pDevice->CurrentTransferDirRx) {       
#ifdef ENABLE_SCATTER_DMA
        if ( 
             ((UINT)pDevice->pCurrentBuffer & (DMA_ALIGNMENT_BYTES - 1)) ||
             ((UINT)(pDevice->pCurrentBuffer+pDevice->CurrentTransferLength) & (DMA_ALIGNMENT_BYTES - 1)) ||
             !HcdMapCurrentRequestBuffer(pDevice, &g_InputDma, 
                                         SPI_RX_DATA_PHY_ADDR, (UINT)pDevice->pCurrentBuffer, 
                                         burstSize, FIXED, INCREASE, LOCKFLAG_READ)) 
#endif
        {            
            DMA_initialize_channel(&g_InputDma, TRUE);
            DMA_set_channel_source(&g_InputDma, (UINT)SPI_RX_DATA_PHY_ADDR, param->dmaUnit, burstSize, FIXED);
            DMA_set_channel_destination(&g_InputDma, (UINT)pHWDevice->pDmaCommonPhysicalBuffer, param->dmaUnit, burstSize, INCREASE);
            DMA_set_channel_transfer_size(&g_InputDma, dwDmaLen);    
        }
        pSPIregs->MODE_CFG     =  param->mode|RX_DMA_ON|dmaMode;
#ifdef ENABLE_INT_MODE
        pSPIregs->SPI_INT_EN = 0; /*disable interrupt */
#endif
        pSPIregs->PACKET_COUNT = PACKET_CNT_EN | (packetCount) ;        
        pSPIregs->CH_CFG        |=  RX_CH_ON;
                
        //DMA_initialize_LLI(&g_InputDma, 0);
    } else {
        /* write direction using common buffer DMA , must transfer to common buffer now */
        pSPIregs->MODE_CFG     =  param->mode|TX_DMA_ON|dmaMode;
#ifdef ENABLE_INT_MODE
        pSPIregs->SPI_INT_EN = 0; /*disable interrupt */
#endif 
        pSPIregs->CH_CFG        |=  TX_CH_ON;    
#ifdef ENABLE_SCATTER_DMA
        if (HcdMapCurrentRequestBuffer(pDevice, &g_OutputDma,
                                         (UINT)pDevice->pCurrentBuffer, SPI_TX_DATA_PHY_ADDR,
                                         burstSize, INCREASE, FIXED, LOCKFLAG_WRITE)) {
            CacheRangeFlush(pDevice->pCurrentBuffer, pDevice->CurrentTransferLength, CACHE_SYNC_WRITEBACK );
        } 
        else 
#endif
        {
            HcdCommonBufferCopy(pDevice->CurrentDmaWidth,
                                pHWDevice->pDmaCommonBuffer,
                                pDevice->pCurrentBuffer,
                                pDevice->CurrentTransferLength,
                                pDevice->HostDMABufferCopyMode);
    
            DMA_initialize_channel(&g_OutputDma, TRUE);
            DMA_set_channel_source(&g_OutputDma, (UINT)pHWDevice->pDmaCommonPhysicalBuffer, param->dmaUnit, burstSize, INCREASE);
            DMA_set_channel_destination(&g_OutputDma, (UINT)SPI_TX_DATA_PHY_ADDR, param->dmaUnit, burstSize, FIXED);
            DMA_set_channel_transfer_size(&g_OutputDma, dwDmaLen);
            //DMA_initialize_LLI(&g_OutputDma, 0);
        }
    }

    return SDIO_STATUS_PENDING;   
#endif /* HCD_EMULATE_DMA */
}

    /* set the clock rate for the SPI transactions */
void HW_SetClock(PSDHCD_DEVICE pDevice, PUINT32 pClockRate)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    DWORD dwPrescaler = 0;
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    /* TODO set the clock rate to the closest (or less) rate*/
#if (SPI_CLOCK == EPLL_CLOCK)  /* 84.666667MHz */ 
    DWORD clockRate = S3C64XX_ECLK;
    UINT32 clockFlag = CLKSEL_EPLL;
#elif (SPI_CLOCK == USB_HOST_CLOCK) /* 48MHz */
    DWORD clockRate = S3C64XX_USBCLK;
    UINT32 clockFlag = CLKSEL_USBCLK;
#elif (SPI_CLOCK == PCLOCK) /* 66.5Mhz  */
    DWORD clockRate = S3C64XX_PCLK;
    UINT32 clockFlag = CLKSEL_PCLK;
#endif

    dwPrescaler = (clockRate / *pClockRate+1) / 2;
    dwPrescaler = (dwPrescaler < 1) ? 0 : (dwPrescaler-1);
    pSpi->ClockCfg = clockFlag|(dwPrescaler);
    pSpi->ClockCfg |= ENCLK_ENABLE;
    *pClockRate = clockRate / (2*(dwPrescaler+1));
    RETAILMSG(1, (TEXT("Clock running at %d\r\n"), *pClockRate));

}

    /* enable disable SPI (via GPIO) interrupt detection */
void HW_EnableDisableSPIIRQ(PSDHCD_DEVICE pDevice, BOOL Enable, BOOL FromIrq)
{
   PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
   PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
   /* TODO */

#if (SPI_WLAN_INTR == IRQ_EINT4)
   if (Enable) {
       pSpi->pGPIOregs->EINT0MASK  &= ~( 1<<4);
       pSpi->pGPIOregs->EINT34MASK &= ~( 1<<21 ); /* Enable EINT4[5] interrupt */       
   } else {
       pSpi->pGPIOregs->EINT34MASK |= ( 1<<21 ); /* disable EINT4[5] interrupt */
       pSpi->pGPIOregs->EINT0MASK  |= ( 1<<4 );  /* disable EINT4 interrupt */
       pSpi->pGPIOregs->EINT0PEND |= (1<<4);     // Clear pending
       pSpi->pGPIOregs->EINT34PEND &= ~( 1 << (16+5)); // Clear pending
   }  
#elif (SPI_WLAN_INTR == IRQ_EINT5)
   if (Enable) {       
       pSpi->pGPIOregs->EINT0MASK &= ~(0x1<<5);    // unMask EINT5
       //pSpi->pGPIOregs->EINT56MASK &= ~( 0x7f<<0 ); /* enable EINT5[x] interrupt */
   } else {
       //pSpi->pGPIOregs->EINT56MASK |= ( 0x7f<<0 ); /* disable EINT5[x] interrupt */
       //pSpi->pGPIOregs->EINT56PEND |= ( 0x7f<<0 );
       pSpi->pGPIOregs->EINT0MASK |= (0x1<<5);    // Mask EINT5
       pSpi->pGPIOregs->EINT0PEND |= (0x1<<5); // Clear pending EINT5
   } 
#elif (SPI_WLAN_INTR == IRQ_EINT10)
   if (Enable) {
       pSpi->pGPIOregs->EINT0MASK &= ~(0x1<<10);    // unMask EINT10
   } else {
       pSpi->pGPIOregs->EINT0MASK |= (0x1<<10);    // Mask EINT10
       pSpi->pGPIOregs->EINT0PEND |= (0x1<<10); // Clear pending EINT10
   } 
#elif (SPI_WLAN_INTR == IRQ_EINT13)
   if (Enable) {
       pSpi->pGPIOregs->EINT0MASK &= ~(0x1<<13);    // unMask EINT10
   } else {
       pSpi->pGPIOregs->EINT0MASK |= (0x1<<13);    // Mask EINT10
       pSpi->pGPIOregs->EINT0PEND |= (0x1<<13); // Clear pending EINT10
   }        
#else
#error "Please change the following setting if SPI_WLAN_INTR != IRQ_EINT4"
#endif 
    
}

    /* start the DMA operation on the SPI host controller */
void HW_StartDMA(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG *pSPIregs = pSpi->pSPIregs;
    /* TODO The hardware driver should enable DMA hardware.  DMA should complete asynchronously
     * through an interrupt or some callback implemented in the platform.  When the DMA
     * completes, the HW layer must indicate DMA completion using HcdDmaCompletion()
     * If common buffer was employed, the HW layer must call HcdCommonBufferCopy() which
     * copies from the HW's DMA buffer to the bus request buffer and performs any necessary
     * byte swapping see SampleCompleteDMATransferCallback() */
#ifdef HCD_EMULATE_DMA
        /* wake up thread to do the emulated DMA, we use the rx DMA thread here */
    SetEvent(pSpi->hRxDmaDoneEvent);
#else   /* HCD_EMULATE_DMA */
    do {

        MASTER_CS_ENABLE;
        if (pHWDevice->CommonBufferDMA) {
                /* common buffer DMA case */            
            if (pDevice->CurrentTransferDirRx) {
                /* handle read dma */    
                DMA_channel_start(&g_InputDma);
            } else {
                /* handle write dma */    
                DMA_channel_start(&g_OutputDma);
            }
            
        } else {
                 /* scatter gather DMA case */
                 
            if (pDevice->CurrentTransferDirRx) {
                /* handle read dma */    
                DMA_channel_start(&g_InputDma);
            } else {
                /* handle write dma */    
                DMA_channel_start(&g_OutputDma);
            }            
        }
        
    } while (FALSE);
#endif /* HCD_EMULATE_DMA */
}
    
/*
 * StopDMATransfer - stop DMA transfer
*/
void HW_StopDMATransfer(PSDHCD_DEVICE pDevice)
{
#if !defined(HCD_EMULATE_DMA)
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    RETAILMSG(SPI_MSG,(TEXT("[SPI] Stop DMA\r\n")));
    if (pDevice->CurrentTransferDirRx) {
        /* handle read dma */    
        DMA_channel_stop(&g_InputDma);
    } else {
        /* handle write dma */    
        DMA_channel_stop(&g_OutputDma);
    }
#endif /* HCD_EMULATE_DMA */
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
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG *pSPIregs = pSpi->pSPIregs;
    SDIO_STATUS      status = SDIO_STATUS_SUCCESS;
    UINT32           inTokenVal;
    const ModeParams *param;
    UINT32 rxTrigger;
#ifdef ENABLE_INT_MODE
    UINT32 WaitReturn;
#endif

    /* TODO , this function must issue the token WITHOUT interrupts (polling).  Token
     * issue must be fast and low overhead.  For larger transfers, the common layer will
     * call the HW DMA setup and start APIs */

    /* Master Mode */
    pSPIregs->CH_CFG   |= SW_RST;
    pSPIregs->CH_CFG    = SPI_TRANSFER_TYPE;
    pSPIregs->CLK_CFG   = pSpi->ClockCfg;
    
    do {
        if (DataSize==ATH_TRANS_DS_24 || DataSize>ATH_TRANS_DS_32) {
            DBG_ASSERT(FALSE); 
            status = SDIO_STATUS_INVALID_PARAMETER;
            break;  
        } else {
            param = &gModeParams[DataSize];
            rxTrigger = param->bytes;
        }    

        /* TODO .. issue SPI frame, inTokenVal is the SPI value returned from the device */
#ifdef ENABLE_INT_MODE
        pSPIregs->MODE_CFG     = param->mode| (rxTrigger << 11);
        pSPIregs->SPI_INT_EN   = RX_FIFORDY;
#else
        pSPIregs->MODE_CFG = param->mode;
#endif         
        pSPIregs->PACKET_COUNT = PACKET_CNT_EN | (1) ;
        pSPIregs->CH_CFG   |=  TX_CH_ON|RX_CH_ON;
        
        MASTER_CS_ENABLE;

        pSPIregs->SPI_TX_DATA = OutToken; /* write data */

#ifdef ENABLE_INT_MODE
        WaitReturn = WaitForSingleObject(pSpi->hSpiDoneEvent, 5000);
        if ( WAIT_TIMEOUT == WaitReturn ) {                
            RETAILMSG (TRUE, (TEXT("Read Rx interrrupt timeout!!!\r\n")));
                status = SDIO_STATUS_IO_TIMEOUT;
                break;
            }
        }
#else
        /* Poll for completion, we wait for RX data to fully shift in 
         * NOTE: we do not use GetTickCount to a timeout, because each call to GetTickCount 
         * traps into the kernel! , we use an approzximate timeout */
        if (((pSPIregs->SPI_STATUS >> 13) & 0x7f) < rxTrigger) {
            ULONG  waitCount = 1000000;
            do {
                if (((pSPIregs->SPI_STATUS >> 13) & 0x7f) >= rxTrigger) {
                        /* FIFO has data we want */
                    break;    
                }                
                waitCount--;
            } while (waitCount);

            if (waitCount == 0) {
                RETAILMSG (TRUE, (TEXT("InOut_Token polled timeout!!!\r\n")));
                status = SDIO_STATUS_IO_TIMEOUT;
                break;
            }
        }             
#endif /* ENABLE_INT_MODE */

        inTokenVal = pSPIregs->SPI_RX_DATA;       

        if (pInToken != NULL) {
            *pInToken = inTokenVal & param->mask;
        }
        
    } while (FALSE);    
    
    MASTER_CS_DISABLE;
    pSPIregs->PACKET_COUNT = 0;
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
    LARGE_INTEGER liDelay;
    
    if (uSeconds == 0) {
        return;    
    }
    
    // Query number of ticks per second
    if (QueryPerformanceFrequency(&liDelay))
    {        
        LARGE_INTEGER liTimeOut;
        liDelay.QuadPart =  liDelay.QuadPart * uSeconds / 1000000;

        if (QueryPerformanceCounter(&liTimeOut))
        {
            LARGE_INTEGER liCurrent;            
            liTimeOut.QuadPart += liDelay.QuadPart;
            do { // Delay until timeout
                QueryPerformanceCounter(&liCurrent);                
            } while (liCurrent.QuadPart<liTimeOut.QuadPart);
        }
    }
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
    RETAILMSG(1, ( TEXT("SpiGpioIRQInterruptThread: Initializing. Context 0x%X \n"), pContext) );
    CeSetThreadPriority(GetCurrentThread(), SPI_IRQ_THREAD_PRIORITY);
    
    while (TRUE) {
        WaitForSingleObject(pHWDevice->hIstEventSPIGpioIRQ,INFINITE); 
        if (pHWDevice->ShutDown) {
            DBG_PRINT(SDDBG_TRACE, ("SpiGpioIRQInterruptThread: Shutting down \n"));            
            break;
        }
        HW_EnableDisableSPIIRQ(pHWDevice->pDevice, FALSE, TRUE);
        HcdSpiInterrupt(pHWDevice->pDevice);
            
        /* ack kernel/OAL that interrupt has been acknowledged */
        InterruptDone(pHWDevice->SysIntrSPIGpioIRQ); 
    }

    return 0;
}

void HW_PowerUpDown(PVOID pContext, BOOL powerUp)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext;
    PSPI_CONTEXT pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_GPIO_REG *pGPIOregs = pSpi->pGPIOregs;
    volatile S3C64XX_SPI_REG *pSPIregs = pSpi->pSPIregs;
    volatile S3C64XX_SPI_REG *pRestoreSPIregs = &pSpi->RestoreSPIregs;
    RETAILMSG(1, (_T("SPI HW: %s power\n"), powerUp ? _T("Enable") : _T("Disable")));
    if (powerUp) {
#if TARGET_TJET
        /* Setup GPL13->IO, GPC4->32KHz, GPI9->CHIP_PWD_L*/        
        pGPIOregs->GPLCON1 = pGPIOregs->GPLCON1 & ~(0xf<<20) | (1<<20);
        pGPIOregs->GPLPUD = pGPIOregs->GPLPUD & ~(0x3<<26) | (0<<26);
        pGPIOregs->GPCCON = pGPIOregs->GPCCON & ~(0xf<<16) | (1<<16);
        pGPIOregs->GPCPUD = pGPIOregs->GPCPUD & ~(0x3<<8) | (0<<8);
        pGPIOregs->GPICON = pGPIOregs->GPICON & ~(0x3<<18) | (1<<18);     
        pGPIOregs->GPIPUD = pGPIOregs->GPIPUD & ~(0x3<<18) | (0<<18);
              
        pGPIOregs->GPIDAT &= ~(0x1<<9); /* disable  the CHIP_PWD_L */   
        pGPIOregs->GPLDAT |= (0x1<<13); /* enable io suppiles, GPL13_Output */        
        pGPIOregs->GPCDAT |= (0x1<<4); /* enable the 32KHz clock, GPC4_Output */  
        HW_UsecDelay(pHWDevice->pDevice, 1000);
        pGPIOregs->GPIDAT |= (0x1<<9); /* enable the CHIP_PWD_L */  
        HW_UsecDelay(pHWDevice->pDevice, 1000);
        pGPIOregs->GPIDAT &= ~(0x1<<9); /* disable  the CHIP_PWD_L */   
        HW_UsecDelay(pHWDevice->pDevice, 1000);
        pGPIOregs->GPIDAT |= (0x1<<9); /* enable the CHIP_PWD_L */  
#endif 
        // SPI Clock On
        pSpi->pSYSCONregs->PCLK_GATE |= SPI_POWER_ON;
#if (SPI_CLOCK == EPLL_CLOCK)
        pSpi->pSYSCONregs->SCLK_GATE |= SPI_SCLK_ON;
#elif (SPI_CLOCK == USB_HOST_CLOCK)
        pSpi->pSYSCONregs->SCLK_GATE |= SPI_USBHOST_ON;
        pSpi->pSYSCONregs->OTHERS |= USB_SIG_MASK;
#endif

        //Restore SPI Reg
        pSPIregs->CH_CFG     = pRestoreSPIregs->CH_CFG;
        pSPIregs->CLK_CFG    = pRestoreSPIregs->CLK_CFG;
        pSPIregs->MODE_CFG   = pRestoreSPIregs->MODE_CFG;
        pSPIregs->SPI_INT_EN = pRestoreSPIregs->SPI_INT_EN;
        pSPIregs->SWAP_CFG   = pRestoreSPIregs->SWAP_CFG;
        pSPIregs->FB_CLK_SEL = pRestoreSPIregs->FB_CLK_SEL;

        HW_SetupWlanGpioIntr(pSpi);
        if (pHWDevice->pDevice->ShuttingDown) {
            HcdInitialize(pHWDevice->pDevice);
            pHWDevice->pDevice->ShuttingDown = FALSE;
        }        
    } else {
        HW_EnableDisableSPIIRQ(pHWDevice->pDevice, FALSE, FALSE);
        HcdDeinitialize(pHWDevice->pDevice);
        //Save SPI Reg
        pRestoreSPIregs->CH_CFG     = pSPIregs->CH_CFG;
        pRestoreSPIregs->CLK_CFG    = pSPIregs->CLK_CFG;
        pRestoreSPIregs->MODE_CFG   = pSPIregs->MODE_CFG;
        pRestoreSPIregs->SPI_INT_EN = pSPIregs->SPI_INT_EN;
        pRestoreSPIregs->SWAP_CFG   = pSPIregs->SWAP_CFG;
        pRestoreSPIregs->FB_CLK_SEL = pSPIregs->FB_CLK_SEL;

        // Clock Off
        pSpi->pSYSCONregs->PCLK_GATE &= ~SPI_POWER_ON;
#if (SPI_CLOCK == EPLL_CLOCK)
        pSpi->pSYSCONregs->SCLK_GATE &= ~SPI_SCLK_ON;
#elif (SPI_CLOCK == USB_HOST_CLOCK)
        pSpi->pSYSCONregs->SCLK_GATE &= ~SPI_USBHOST_ON;
#endif
        
#if TARGET_TJET
        pGPIOregs->GPICON = pGPIOregs->GPICON & ~(0x3<<18) | (1<<18);
        pGPIOregs->GPIPUD = pGPIOregs->GPIPUD & ~(0x3<<18) | (0<<18);
        pGPIOregs->GPIDAT &= ~(0x1<<9); /* Assert for CHIP_PWD_L */        
#endif 
    }
}

#ifdef HCD_EMULATE_DMA
static void DoEmulatedDMA(PSDHCD_DEVICE    pDevice)
{
    PSDHCD_HW_DEVICE            pHWDevice = GET_HW_DEVICE(pDevice);
    PSPI_CONTEXT                pSpi = (PSPI_CONTEXT)pHWDevice->pSpiContext;
    volatile S3C64XX_SPI_REG   *pSPIregs = pSpi->pSPIregs;
    UINT32                      inTokenVal, outTokenVal;
    UINT32                      mode, bytesPerFrame;
    UINT32                      frameCount = pDevice->CurrentTransferLength;
    SDIO_STATUS                 status = SDIO_STATUS_SUCCESS;
    BOOL                        alignedBuffer = FALSE;
    PUINT8                      pBuffer = pDevice->pCurrentBuffer;
         
    if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_16) {        
        /* do 16 bit frame */   
        mode  = (CH_SIZE_HALF|BUS_SIZE_HALF); 
        bytesPerFrame = 2;
        if (((UINT32)pBuffer & 0x1) == 0) {
            alignedBuffer = TRUE;
        }
        frameCount >>= 1;        
    } else if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_32) {   
        /* do 32 bit frame */           
        mode  = (CH_SIZE_WORD|BUS_SIZE_WORD); 
        bytesPerFrame = 4;
        if (((UINT32)pBuffer & 0x3) == 0) {
            alignedBuffer = TRUE;
        }
        frameCount >>= 2; 
    } else if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_8) { 
        /* do 8 bit frame */         
        mode = (CH_SIZE_BYTE|BUS_SIZE_BYTE); 
        bytesPerFrame = 1;
        alignedBuffer = TRUE;
    } else {
        DBG_ASSERT(FALSE);
        return;
    }   
           
    while (frameCount) {
        pSPIregs->CH_CFG |= SW_RST;
        pSPIregs->CH_CFG = SPI_TRANSFER_TYPE;
        pSPIregs->CLK_CFG = pSpi->ClockCfg;
        pSPIregs->MODE_CFG = mode;
#ifdef ENABLE_INT_MODE
        pSPIregs->SPI_INT_EN = 0
#endif 
        pSPIregs->CH_CFG |= TX_CH_ON|RX_CH_ON;
        pSPIregs->PACKET_COUNT = PACKET_CNT_EN | (1) ;
        MASTER_CS_ENABLE;
            
        if (pDevice->CurrentTransferDirRx) {
             outTokenVal = 0xFFFFFFFF;
        } else {
            if (alignedBuffer) {
                if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_32) {        
                    outTokenVal = *((UINT32 *)pBuffer);
                    pBuffer += 4;   
                } else if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_16) {   
                    outTokenVal = *((UINT16 *)pBuffer);
                    pBuffer += 2;   
                } else if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_8) { 
                    outTokenVal = *pBuffer;
                    pBuffer++; 
                } else {
                    outTokenVal = 0;    
                }
            } else {
                memcpy(&outTokenVal,pBuffer,bytesPerFrame);   
                pBuffer += bytesPerFrame;
            }
        }    
        
        pSPIregs->SPI_TX_DATA = outTokenVal;
        
        {
            ULONG count = 1000000;
            while (count) {
                if (((pSPIregs->SPI_STATUS >> 13) & 0x7f) >= bytesPerFrame) {
                        /* FIFO has data we want */
                    break;    
                }                  
                count--;
            }
            
            if (count == 0) {
                RETAILMSG (TRUE, (TEXT("xx Read timeout!!!\r\n")));
                status = SDIO_STATUS_IO_TIMEOUT;
            }
        }
        
        if (!SDIO_SUCCESS(status)) {
            MASTER_CS_DISABLE;
            pSPIregs->PACKET_COUNT = (0) ; /* disable packet count */
            pSPIregs->CH_CFG &=  ~(TX_CH_ON|RX_CH_ON);
            break;
        }
        
        inTokenVal = pSPIregs->SPI_RX_DATA;
        
        if (pDevice->CurrentTransferDirRx) {      
            if (alignedBuffer) {
                if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_32) {        
                    *((UINT32 *)pBuffer) = inTokenVal;
                    pBuffer += 4;   
                } else if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_16) {   
                    *((UINT16 *)pBuffer) = (UINT16)inTokenVal;
                    pBuffer += 2;   
                } else if (pDevice->CurrentDmaWidth  == ATH_TRANS_DS_8) { 
                    *pBuffer = (UINT8)inTokenVal;
                    pBuffer++; 
                } else {
                }
            } else {
                memcpy(pBuffer,&inTokenVal,bytesPerFrame);   
                pBuffer += bytesPerFrame;
            }
        }           
                
        MASTER_CS_DISABLE;

        frameCount--;
        pSPIregs->PACKET_COUNT = (0) ; /* disable packet count */
        pSPIregs->CH_CFG &=  ~(TX_CH_ON|RX_CH_ON);
    }
    
               
    HcdDmaCompletion(pHWDevice->pDevice, status);

}
#endif
