//------------------------------------------------------------------------------
// <copyright file="ath_spi_omap2420_hw.c" company="Atheros">
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
/* debug level for this module*/
#define DBG_DECLARE 4;
#include <ctsystem.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <asm/arch/dma.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <asm/arch/mux.h>
#include <linux/dma-mapping.h>
#include <asm/arch/board.h>
#include <asm/arch/gpio.h>
#include <asm/arch/tps65010.h>
#endif
#include <asm/arch/irq.h>
#include <asm/arch/ck.h>
#include <asm/arch/gpio.h>
#include <asm/irq.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <asm/hardware/clock.h>
#endif

#include "ath_spi_hcd.h"
#include "ath_spi_linux.h"
#include "../omap2420_spi_hw.h"

#define DESCRIPTION "OMAP SPI RAW SPI HCD"
#define AUTHOR "Atheros Communications, Inc."
#define ATH_WRITE_PIPELINED
#define ATH_USE_OPTIMAL_DMA

/* mapped memory address */
typedef struct _SDHCD_MEMORY {
    ULONG Raw;      /* start of address range */
    ULONG Length;   /* length of range */
    PVOID pMapped;  /* the mapped address */
}SDHCD_MEMORY, *PSDHCD_MEMORY;

typedef struct _SDHCD_DRIVER_CONTEXT {
    SDLIST            DeviceList;         /* the list of current devices handled by this driver */
    OS_SEMAPHORE      DeviceListSem;      /* protection for the DeviceList */
    UINT              DeviceCount;        /* number of devices currently installed */     
    OS_PNPDEVICE      HcdDevice;          /* the OS device for this HCD */
    OS_PNPDRIVER      HcdDriver;          /* the OS driver for this HCD */ 
    SDDMA_DESCRIPTION Dma;                /* driver DMA description */
}SDHCD_DRIVER_CONTEXT, *PSDHCD_DRIVER_CONTEXT;

  /* TODO, we allow only 1 scatter entry for simplicity. Under linux
     network buffers are always contigous (they are kernel-malloc'd) */
#define MAX_DMA_SCATTER_ENTRIES 1
    
typedef struct _SDHCD_HW_DEVICE {
    SDHCD_MEMORY    Address;          /* memory address of this device */
    UINT32          PollWait;
    PVOID           pChannelConf;
    PVOID           pChannelStat;
    PVOID           pChannelCtrl;
    PVOID           pChannelTx;
    PVOID           pChannelRx;
    BOOL            WaitTxDone;
    UINT32          ChannelConfShadow;
    DMA_ADDRESS     hDmaBuffer;       /* handle for data buffer */
    PUINT8          pDmaBuffer;       /* virtual address of read/write buffer */
    int             CommonBufferSize;      /* commonbuffer size */
    PUINT8          pDmaDummyBuffer;  /* virtual address of dummy FF buffer */
    DMA_ADDRESS     hDmaDummyBuffer;       /* handle for data buffer */
    int             DmaRxChan;       /* allocated DMA channel */  
    int             DmaTxChan;       /* allocated DMA channel */
    INT             SPIChannel;
    int             SpiIntGPIOPin;
    volatile POMAP_DMA_REGS pTxDmaRegs;
    volatile POMAP_DMA_REGS pRxDmaRegs;
    UINT8           LastDmaSize;
    UINT32          PadConfigBase;
    POS_PNPDEVICE   pBusDevice;      /* our device registered with bus driver */
    UINT8           InitStateMask;
#define SDIO_BASE_MAPPED           0x01
#define SDIO_IRQ_INTERRUPT_INIT    0x04
#define SDHC_REGISTERED            0x10
#define SDHC_COMMON_INIT           0x40
#define SDHC_HW_INIT               0x80
    spinlock_t      Lock;                   /* lock against the ISR */
    struct work_struct iocomplete_work;     /* work item for completeting deferred work */
    struct work_struct procirq_work;        /* work item for IRQ processing */
    struct work_struct dmacomplete_work;    /* work item for DMA completion */
    int             Interrupt;       /* GPIO IRQ line */
    int             DmaRxId;         /* receive DMA channel */
    int             DmaTxId;         /* transmit DMA channel */
    BOOL            D0Swap;
    BOOL            DummyTxDmaActive;   /* the fake TX is active */
    SDIO_STATUS     DMACompleteStatus;  /* DMA status passed to work item */
    int             DmaStopCount;
    PSDHCD_DEVICE   pDevice;            /* back pointer to the common layer */
    BOOL            IrqEnabled;
#ifdef CHECK_DMA_TIMEOUT
    struct timer_list DMATimer;         /* DMA timeout timer */
    BOOL              DMATimerQueued;
    BOOL              DMATimerCancelled;
#endif
    struct timer_list Timer;            /* Generic Timer */
    BOOL              TimerQueued;
    BOOL              TimerCancelled;
    int               TimerCallbackContext; 
    BOOL            DMASg;              /* dma is scatter gather */
    struct scatterlist DMAMapList[MAX_DMA_SCATTER_ENTRIES];
    int             CurrentScatterEntries;
}SDHCD_HW_DEVICE, *PSDHCD_HW_DEVICE;

#define GET_HW_DEVICE(pDevice) ((PSDHCD_HW_DEVICE)((pDevice)->pHWDevice))

#define TX_DUMMY_BUFFER_SIZE 4
#define SDHCD_MAX_DEVICE_NAME (sizeof(SDIO_RAW_BD_BASE) + 8)
 

#define READ_HOST_REG32(pHWDevice, OFFSET)  \
    _READ_DWORD_REG((((UINT32)((pHWDevice)->Address.pMapped))) + (OFFSET))
#define WRITE_HOST_REG32(pHWDevice, OFFSET, VALUE) \
    _WRITE_DWORD_REG((((UINT32)((pHWDevice)->Address.pMapped))) + (OFFSET),(VALUE))
#define READ_HOST_REG16(pHWDevice, OFFSET)  \
    _READ_WORD_REG((((UINT32)((pHWDevice)->Address.pMapped))) + (OFFSET))
#define WRITE_HOST_REG16(pHWDevice, OFFSET, VALUE) \
    _WRITE_WORD_REG((((UINT32)((pHWDevice)->Address.pMapped))) + (OFFSET),(VALUE))

#define READ_CHCONF_REG32(pHWDevice)  \
    _READ_DWORD_REG((pHWDevice)->pChannelConf)
#define WRITE_CHCONF_REG32(pHWDevice, VALUE) \
    _WRITE_DWORD_REG((pHWDevice)->pChannelConf,(VALUE))

#define READ_CHSTAT_REG32(pHWDevice)  \
    _READ_DWORD_REG((pHWDevice)->pChannelStat)
#define WRITE_CHSTAT_REG32(pHWDevice, VALUE) \
    _WRITE_DWORD_REG((pHWDevice)->pChannelStat,(VALUE))
    
#define READ_CHCTRL_REG32(pHWDevice)  \
    _READ_DWORD_REG((pHWDevice)->pChannelCtrl)
#define WRITE_CHCTRL_REG32(pHWDevice, VALUE) \
    _WRITE_DWORD_REG((pHWDevice)->pChannelCtrl,(VALUE))   

#define WRITE_CHTX_REG32(pHWDevice, VALUE) \
    _WRITE_DWORD_REG((pHWDevice)->pChannelTx,(VALUE))
#define READ_CHRX_REG32(pHWDevice)  \
    _READ_DWORD_REG((pHWDevice)->pChannelRx)
    
#define GET_HOST_REG_BASE(pHWDevice) (pHWDevice)->Address.pMapped

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int Probe(struct pnp_dev *pBusDevice, const struct pnp_device_id *pId);
static void Remove(struct pnp_dev *pBusDevice);
#else
static int Probe(POS_PNPDEVICE pBusDevice, const PUINT pId);
static void Remove(POS_PNPDEVICE pBusDevice);
#endif

static void RemoveDevice(POS_PNPDEVICE pBusDevice, PSDHCD_DRIVER_CONTEXT pHcdContext);
static void hcd_iocomplete_wqueue_handler(void *context);
static void hcd_procirq_wqueue_handler(void *context);
static void hcd_dmacomplete_wqueue_handler(void *context);

static void DeinitOmapSpiHw(PSDHCD_HW_DEVICE pHWDevice);
static SDIO_STATUS InitOmapSpiHw(PSDHCD_HW_DEVICE pHWDevice, UINT deviceNumber);
static void hcd_spi_irq(int irq, void *context, struct pt_regs * r);
static void OmapPadConfig(PSDHCD_HW_DEVICE pHWDevice, UINT32 Offset, UINT8 BytePos, UINT8 PadValue);

static void TimerTimeout(unsigned long Context);

/* this is a place holder for the UnprepareSG, in the event we need
 * to do any cleanup of scatter gather resources, we can define it 
 * as a function, for now it is an empty function */
#define UnPrepareSG(s,E,dir)
/*static void UnPrepareSG(struct scatterlist *pSg, int Entries, BOOL ToDevice);*/

#ifdef CHECK_DMA_TIMEOUT
static void DMATimeout(unsigned long Context);
static void SetDMATimer(PSDHCD_HW_DEVICE pHWDevice, UINT32 TimeOut);
static void CancelDMATimer(PSDHCD_HW_DEVICE pHWDevice);
#define OMAP_SET_DMA_TIMER(p,t) SetDmaTimer((p),(t)
#define OMAP_CANCEL_DMA_TIMER(p) CancelDMATimer((p))
#else
#define OMAP_SET_DMA_TIMER(p,t)
#define OMAP_CANCEL_DMA_TIMER(p)
#endif

static INLINE SDIO_STATUS WaitLastOp(PSDHCD_HW_DEVICE pHWDevice);

/* debug print parameter */ 
module_param(debuglevel, int, 0644);
MODULE_PARM_DESC(debuglevel, "debuglevel 0-7, controls debug prints");
#define DEFAULT_ATTRIBUTES (SDHCD_ATTRIB_RAW_MODE) 
                            
static UINT32 hcdattributes = DEFAULT_ATTRIBUTES;
module_param(hcdattributes, int, 0644);
MODULE_PARM_DESC(hcdattributes, "OMAP Attributes");
static INT base_clock = OMAP_SPI_MODULE_CLOCK;
module_param(base_clock, int, 0444);
MODULE_PARM_DESC(base_clock, "Base Clock Hz ");

static INT op_clock = OMAP_SPI_MODULE_CLOCK;
module_param(op_clock, int, 0444);
MODULE_PARM_DESC(op_clock, "Operational Clock Hz ");

static INT powerupdelay= 1;
module_param(powerupdelay, int, 0444);
MODULE_PARM_DESC(powerupdelay, "Powerup delay on core wakeup");

#define MAX_BYTES_DMA_BUFFER       2040    /* largest DMA buffer possible */
#define DEF_BYTES_DMA_BUFFER       2040    /* default */

INT MaxBytesPerDMARequest = DEF_BYTES_DMA_BUFFER;
module_param(MaxBytesPerDMARequest, int, 0644);
MODULE_PARM_DESC(MaxBytesPerDMARequest, "OMAP Max Bytes Per DMA Request");

INT gpiodebug = 0;
module_param(gpiodebug, int, 0444);
MODULE_PARM_DESC(gpiodebug, "Special GPIO debug");

INT enableautoidle = 0;
module_param(enableautoidle, int, 0444);
MODULE_PARM_DESC(enableautoidle, "Enable SPI module Auto-Idle");

INT spimodule = 1;
module_param(spimodule, int, 0444);
MODULE_PARM_DESC(spimodule, "Spi module instance");

INT spichan = 2;
module_param(spichan, int, 0444);
MODULE_PARM_DESC(spichan, "Spi Channel");

INT autoendian = 0;
module_param(autoendian, int, 0444);
MODULE_PARM_DESC(autoendian, "Spi DMA endian conversion");

INT srambuffer = 0;
module_param(srambuffer, int, 0444);
MODULE_PARM_DESC(srambuffer, "Spi DMA SRAM buffer");

INT d0swap = 0;
module_param(d0swap, int, 0444);
MODULE_PARM_DESC(d0swap, "Spi D0 pin swap");

INT int_gpio = 98;
module_param(int_gpio, int, 0444);
MODULE_PARM_DESC(int_gpio, "Spi interrupt GPIO pin");

/* AR6002 uses SPI mode 3 */
INT rxclkmode = OMAP_SPI_CHCON_SPI_MODE_3; /* clock mode */
module_param(rxclkmode, int, 0444);
MODULE_PARM_DESC(rxclkmode, "Spi RX clock mode (0-3)");
INT txclkmode = OMAP_SPI_CHCON_SPI_MODE_3; /* clock mode */
module_param(txclkmode, int, 0444);
MODULE_PARM_DESC(txclkmode, "Spi TX clock mode (0-3)");

INT dump_state = 0;
module_param(dump_state, int, 0444);
MODULE_PARM_DESC(dump_state, "dump SPI internal state on driver shutdown");

INT allow_sg_dma = 1;
module_param(allow_sg_dma, int, 0444);
MODULE_PARM_DESC(allow_sg_dma, "use scatter gather DMA when possible");

static INT reset_spi_on_shutdown = 1;
module_param(reset_spi_on_shutdown, int, 0444);
MODULE_PARM_DESC(reset_spi_on_shutdown, "reset SPI interface on driver shutdown");

#define CONTROL_PADCONF_N19       0x0110

INT int_gpio_pad_conf_offset = CONTROL_PADCONF_N19;
module_param(int_gpio_pad_conf_offset, int, 0444);
MODULE_PARM_DESC(int_gpio_pad_conf_offset, "Spi interrupt GPIO pin pad config offset");

INT int_gpio_pad_conf_byte = 0;
module_param(int_gpio_pad_conf_byte, int, 0444);
MODULE_PARM_DESC(int_gpio_pad_conf_byte, "Spi interrupt GPIO pin pad byte number");

INT int_gpio_pad_mode_value = 0x3;
module_param(int_gpio_pad_mode_value, int, 0444);
MODULE_PARM_DESC(int_gpio_pad_mode_value, "Spi interrupt GPIO pin mode value");

static INT sleep_war = 0;
module_param(sleep_war, int, 0444);
MODULE_PARM_DESC(sleep_war, "Spi HW Sleep bug workaround");

#if 0
#define SPI_DBG1_PIN                  16     /* gpio 16 */
#define SPI_DBG1_PIN_PAD_CONF_OFFSET  0x00e8 /* Y11 ball */
#define SPI_DBG1_PIN_PAD_CONF_BYTEPOS 0
#define SPI_DBG1_PIN_PAD_CONF_MODE    0x3 
#endif

#define SPI_DBG1_PIN                  118    /* gpio 118 */
#define SPI_DBG1_PIN_PAD_CONF_OFFSET  0x0128 /* W16 ball */
#define SPI_DBG1_PIN_PAD_CONF_BYTEPOS 1
#define SPI_DBG1_PIN_PAD_CONF_MODE    0x3 

#define OMAP_DMA_MASK                 0xFFFFFFFF
#define OMAP_CPU_CACHE_LINE_SIZE      32
#define OMAP_CPU_CACHE_ALIGN_MASK     (OMAP_CPU_CACHE_LINE_SIZE - 1)

/* the driver context data */
static SDHCD_DRIVER_CONTEXT HcdContext = {
   .DeviceCount   = 0,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
   .HcdDevice.name = "sdio_omap_hcd",
   .HcdDriver.name = "sdio_omap_hcd",
   .HcdDriver.probe  = Probe,
   .HcdDriver.remove = Remove,    
#endif
   .Dma.Mask = OMAP_DMA_MASK,
   .Dma.Flags = SDDMA_DESCRIPTION_FLAG_DMA,
   .Dma.MaxBytesPerDescriptor = 0x8000,
   .Dma.AddressAlignment = 0x01,
   .Dma.LengthAlignment = 0x01,
   .Dma.MaxDescriptors = 1,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define DBG_ASSERT_DMA_BUSY(p,s)  \
    DBG_ASSERT_WITH_MSG((p)->DmaChannel == -1, (s))
#else
#define DBG_ASSERT_DMA_BUSY(p,s)  \
    DBG_ASSERT_WITH_MSG((p)->DmaChannel == NULL, (s))
#endif


OMAP_SPIF_CLK_RATE_ENTRY SPIFClockTable[MAX_CLOCK_ENTRIES] = {
 {0x00,0},
 {0x01,0},
 {0x02,0},
 {0x03,0},
 {0x04,0},   
 {0x05,0},
 {0x06,0},
 {0x07,0},
 {0x08,0},
 {0x09,0},
 {0x0a,0}, 
 {0x0b,0}, 
 {0x0c,0},
 {0x0d,0}, 
 {0x0e,0},
 {0x0f,0}, 
};   

static UINT32 first_device = 0;
static UINT32 device_count = 1;
/*
 * Probe - probe to setup our device, if present
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static int Probe(struct pnp_dev *pBusDevice, const struct pnp_device_id *pId)
#else
static int Probe(POS_PNPDEVICE pBusDevice, const PUINT pId)
#endif
{
    SYSTEM_STATUS err = 0;
    SDIO_STATUS   status = SDIO_STATUS_SUCCESS;
    PSDHCD_DEVICE pDevice = NULL;
    PSDHCD_HW_DEVICE pHWDevice;
    int ii;
    PSDHCD_DRIVER_CONTEXT pHcdContext = &HcdContext;
    
    DBG_PRINT(SDDBG_TRACE, ("+OMAP SPI HCD: Probe - probing for new device\n"));

    if (enableautoidle) {
        DBG_PRINT(SDDBG_TRACE, ("OMAP Enabled OCP gating\n"));
    } else {
        DBG_PRINT(SDDBG_TRACE, ("OMAP Disabled OCP gating\n"));    
    }
    
    DBG_PRINT(SDDBG_TRACE, ("OMAP RX Clk Mode: 0x%X TX Clk Mode: 0x%X \n",rxclkmode,txclkmode));

#ifdef ATH_USE_OPTIMAL_DMA
    DBG_PRINT(SDDBG_TRACE, ("** OMAP SPI using optimal DMA...\n"));
#else
    DBG_PRINT(SDDBG_TRACE, ("** OMAP SPI using normal DMA...\n"));
#endif  
    
    MaxBytesPerDMARequest = min(MaxBytesPerDMARequest, (INT)MAX_BYTES_DMA_BUFFER);
     
    if (0 == MaxBytesPerDMARequest) {
        MaxBytesPerDMARequest =  DEF_BYTES_DMA_BUFFER; 
    }

    for (ii = first_device; ii < device_count+first_device; ii++) {
        /* allocate a device instance for this new device */
        pDevice =  (PSDHCD_DEVICE)KernelAlloc(sizeof(SDHCD_DEVICE) + sizeof(SDHCD_HW_DEVICE));
        if (pDevice == NULL) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI HCD: Probe - no memory for device context\n"));
            err = -ENOMEM;
            break;
        }
        ZERO_POBJECT(pDevice);
        SDLIST_INIT(&pDevice->List);
            /* set the HW portion */
        pDevice->pHWDevice = (PUINT8)pDevice + sizeof(SDHCD_DEVICE);
        pHWDevice = GET_HW_DEVICE(pDevice);
            /* set the bus context */
        pHWDevice->pBusDevice = pBusDevice;
            /* save a back pointer to the common layer */
        pHWDevice->pDevice = pDevice;
        
        if (d0swap) {
            pHWDevice->D0Swap = TRUE;
        }
        SET_SDIO_STACK_VERSION(&pDevice->Hcd);
        pDevice->Hcd.pName = (PTEXT)KernelAlloc(SDHCD_MAX_DEVICE_NAME);
        pDevice->Hcd.Attributes = hcdattributes;
        pDevice->Hcd.pContext = pDevice;
        pDevice->Hcd.pRequest = HcdRequest;
        pDevice->Hcd.pConfigure = HcdConfig;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        pDevice->Hcd.pDevice = &pBusDevice->dev;
#endif        
        pDevice->OperationalClock = op_clock;
        pDevice->Hcd.MaxBytesPerBlock = MaxBytesPerDMARequest;
        pDevice->Hcd.MaxBlocksPerTrans = 1;
        pDevice->Hcd.MaxClockRate = 48000000; 
        pDevice->Hcd.pModule = THIS_MODULE;  
        pDevice->Hcd.pDmaDescription = &HcdContext.Dma;
        pDevice->PowerUpDelay = powerupdelay;
            /* set all the supported frame widths the OMAP controller can do
             * 8/16/24/32 bit frames */
        pDevice->SpiHWCapabilitiesFlags = HW_SPI_FRAME_WIDTH_8  | 
                                          HW_SPI_FRAME_WIDTH_16 | 
                                          HW_SPI_FRAME_WIDTH_24 |
                                          HW_SPI_FRAME_WIDTH_32;
         
        if (dump_state) {    
            pDevice->MiscFlags |= MISC_FLAG_DUMP_STATE_ON_SHUTDOWN;
        }  
        
        if (reset_spi_on_shutdown) {
            pDevice->MiscFlags |= MISC_FLAG_RESET_SPI_IF_SHUTDOWN;    
        }               
        
        if (sleep_war) {
            pDevice->MiscFlags |= MISC_FLAG_SPI_SLEEP_WAR;        
        }
        
        /* add device to our list of devices */
            /* protect the devicelist */
        if (!SDIO_SUCCESS(status = SemaphorePendInterruptable(&pHcdContext->DeviceListSem))) {
            break;;   /* wait interrupted */
        }
        SDListInsertTail(&pHcdContext->DeviceList, &pDevice->List);
        snprintf(pDevice->Hcd.pName, SDHCD_MAX_DEVICE_NAME, SDIO_RAW_BD_BASE"%i:%i",
                 pHcdContext->DeviceCount++, ii);
        SemaphorePost(&pHcdContext->DeviceListSem);
        
        /* initialize work items */
        INIT_WORK(&(pHWDevice->iocomplete_work), hcd_iocomplete_wqueue_handler, pHWDevice);
        INIT_WORK(&(pHWDevice->procirq_work), hcd_procirq_wqueue_handler, pHWDevice);
        INIT_WORK(&(pHWDevice->dmacomplete_work),hcd_dmacomplete_wqueue_handler,pHWDevice);
        
        if (!SDIO_SUCCESS((status = InitOmapSpiHw(pHWDevice, ii - first_device)))) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI Probe - failed to init OMAP HW, status =%d\n", status));
            err = SDIOErrorToOSError(status);
            break;
        } 

        if (!SDIO_SUCCESS((status = HcdInitialize(pDevice)))) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI Probe - failed to init common layer, status =%d\n", status));
            err = SDIOErrorToOSError(status);
            break;
        } 
               
        pHWDevice->InitStateMask |= SDHC_COMMON_INIT;
        
           /* register with the SDIO bus driver */
        if (!SDIO_SUCCESS((status = SDIO_RegisterHostController(&pDevice->Hcd)))) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI HCD: Probe - failed to register with host, status =%d\n",status));
            err = SDIOErrorToOSError(status);
            break;
        }   
          
        pHWDevice->InitStateMask |= SDHC_REGISTERED;
                
            /* notify that module is installed */
        SDIO_HandleHcdEvent(&pDevice->Hcd, EVENT_HCD_ATTACH);
            
            /* disable pullup on GPIO pin */
        OmapPadConfig(pHWDevice, 
                      int_gpio_pad_conf_offset, 
                      int_gpio_pad_conf_byte,
                      int_gpio_pad_mode_value);                     
    }   
     
    if (err < 0) {
        Remove(pBusDevice);
    } else {
        DBG_PRINT(SDDBG_ERROR, ("OMAP SPI Probe - HCD ready! \n"));
    }
    
    DBG_PRINT(SDDBG_TRACE, ("-OMAP SPI HCD: Probe - err:%d\n", err));
    return err;  
}    

/* Remove - remove  device
 * perform the undo of the Probe
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
static void Remove(struct pnp_dev *pBusDevice)
#else
static void Remove(POS_PNPDEVICE pBusDevice)
#endif
{
    PSDHCD_DRIVER_CONTEXT pHcdContext = &HcdContext;
    
    DBG_PRINT(SDDBG_TRACE, ("+OMAP SPI HCD: Remove - removing device\n"));
    RemoveDevice(pBusDevice, pHcdContext);
    
    DBG_PRINT(SDDBG_TRACE, ("-OMAP SPI HCD: Remove\n"));
}

/*
 * RemoveDevice - remove all devices associated with bus device
*/
static void RemoveDevice(POS_PNPDEVICE pBusDevice, PSDHCD_DRIVER_CONTEXT pHcdContext)
{
    PSDHCD_DEVICE pDevice; 
    DBG_PRINT(SDDBG_TRACE, ("+OMAP SPI HCD: RemoveDevice\n"));
    
        /* protect the devicelist */
    if (!SDIO_SUCCESS(SemaphorePendInterruptable(&pHcdContext->DeviceListSem))) {
        return;   /* wait interrupted */
    }
    
    SDITERATE_OVER_LIST_ALLOW_REMOVE(&pHcdContext->DeviceList, pDevice, SDHCD_DEVICE, List)
        PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
        if (pHWDevice->pBusDevice == pBusDevice) {
                /* remove from list */            
            SDListRemove(&pDevice->List);            
            pHcdContext->DeviceCount--;
            pDevice->ShuttingDown = TRUE; 
            
            HW_StopDMATransfer(pDevice);            
            
            if (pHWDevice->InitStateMask & SDHC_COMMON_INIT) {
                    /* deinit common layer */
                HcdDeinitialize(pDevice);
            }
            
                /* wait for any of our work items to run */
            flush_scheduled_work();
            
            if (pHWDevice->InitStateMask & SDHC_REGISTERED) {
                SDIO_UnregisterHostController(&pDevice->Hcd);
            }
            
            DeinitOmapSpiHw(pHWDevice); 
            
            if (pDevice->Hcd.pName != NULL) {
                KernelFree(pDevice->Hcd.pName);
                pDevice->Hcd.pName = NULL;
            }   
            KernelFree(pDevice); 
        }
    SDITERATE_END;
    SemaphorePost(&pHcdContext->DeviceListSem);
    DBG_PRINT(SDDBG_TRACE, ("-OMAP SPI HCD: RemoveDevice\n"));
}


/*
 * Queue Work - queue a work item
 * 
*/
static SDIO_STATUS QueueWork(PSDHCD_HW_DEVICE pHWDevice, struct work_struct *work)
{
        /* we use the default kernel work queue */
    if (schedule_work(work) > 0) {
        return SDIO_STATUS_SUCCESS;
    } else {
        DBG_PRINT(SDDBG_ERROR, ("-OMAP SPI QueueWork - Error scheduling work\n"));
        return SDIO_STATUS_PENDING;
    }
}

/*
 * work handler for io completion
*/
static void hcd_iocomplete_wqueue_handler(void *context) 
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)context;
    DBG_PRINT(ATH_SPI_TRACE_REQUESTS, ("OMAP SPI hcd_iocomplete_wqueue_handler \n"));
    SDIO_HandleHcdEvent(&pHWDevice->pDevice->Hcd, EVENT_HCD_TRANSFER_DONE);
}

/* work handler for SPI module interrupt processing */
static void hcd_procirq_wqueue_handler(void *context)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)context;
    HcdSpiInterrupt(pHWDevice->pDevice);
}

/* work handler for DMA completions */
static void hcd_dmacomplete_wqueue_handler(void *context)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)context;
    
    if (pHWDevice->DMASg) {
            /* unprep the scatter entries */
        UnPrepareSG(pHWDevice->DMAMapList, 
                    pHWDevice->CurrentScatterEntries,
                    pHWDevice->pDevice->CurrentTransferDirRx ? FALSE : TRUE); 
    }
    
    HcdDmaCompletion(pHWDevice->pDevice,pHWDevice->DMACompleteStatus);
}

typedef enum _DMA_TRANSFER_TYPE {
    DMA_RX_TYPE,
    DMA_TX_TYPE,
    DMA_TX_DUMMY_TYPE
}DMA_TRANSFER_TYPE, *PDMA_TRANSFER_TYPE;

static void SD_TxDMACallback(int lch, UINT16 DMAStatus, PVOID pContext);
static void SD_RxDMACallback(int lch, UINT16 DMAStatus, PVOID pContext);


#ifdef ATH_USE_OPTIMAL_DMA
#define SET_UP_OMAP_DMA SetupOMAPDMAOpt
#else
#define SET_UP_OMAP_DMA SetupOMAPDMA
#endif

static void DumpRegisters(PSDHCD_HW_DEVICE pHWDevice)
{
    DBG_PRINT(SDDBG_TRACE, ("OMAP SPI Register Dump \n"));
    
    DBG_PRINT(SDDBG_TRACE, ("    REG_BASE:0x%X\n",
        (INT)GET_HOST_REG_BASE(pHWDevice))); 
    DBG_PRINT(SDDBG_TRACE, ("    CHCONF_BASE:0x%X\n",
        (INT)pHWDevice->pChannelConf)); 
    DBG_PRINT(SDDBG_TRACE, ("    CHCTRL_BASE:0x%X\n",
        (INT)pHWDevice->pChannelCtrl));
    DBG_PRINT(SDDBG_TRACE, ("    CHSTAT_BASE:0x%X\n",
        (INT)pHWDevice->pChannelStat));
    DBG_PRINT(SDDBG_TRACE, ("    CHTX_BASE:0x%X\n",
        (INT)pHWDevice->pChannelTx));
    DBG_PRINT(SDDBG_TRACE, ("    CHRX_BASE:0x%X\n",
        (INT)pHWDevice->pChannelRx));
     
    DBG_PRINT(SDDBG_TRACE, ("    SCR:0x%X\n",
            READ_HOST_REG32(pHWDevice, OMAP_SPIF_SCR_REG))); 
    DBG_PRINT(SDDBG_TRACE, ("    SSR:0x%X\n",
            READ_HOST_REG32(pHWDevice, OMAP_SPIF_SSR_REG)));                   
    DBG_PRINT(SDDBG_TRACE, ("    SYST:0x%X\n",
            READ_HOST_REG32(pHWDevice, OMAP_SPIF_SYST_REG))); 
    DBG_PRINT(SDDBG_TRACE, ("    MCTRL:0x%X\n",
            READ_HOST_REG32(pHWDevice, OMAP_SPIF_MCTRL_REG)));
    DBG_PRINT(SDDBG_TRACE, ("    ISR:0x%X\n",
            READ_HOST_REG32(pHWDevice, OMAP_SPIF_ISR_REG)));
    DBG_PRINT(SDDBG_TRACE, ("    CHCONF:0x%X\n",
            READ_CHCONF_REG32(pHWDevice))); 
    DBG_PRINT(SDDBG_TRACE, ("    CHSTAT:0x%X\n",
            READ_CHSTAT_REG32(pHWDevice))); 
    DBG_PRINT(SDDBG_TRACE, ("    CHCTRL:0x%X\n",
            READ_CHCTRL_REG32(pHWDevice))); 
}

/* reset SPI host controller */
static SDIO_STATUS ResetController(PSDHCD_HW_DEVICE pHWDevice)
{
    UINT32 temp = RESET_MAX_COUNT;

    WRITE_HOST_REG32(pHWDevice, OMAP_SPIF_SCR_REG,OMAP_SPIF_SCR_RESET);
    
    while (temp) {
        if (READ_HOST_REG32(pHWDevice, OMAP_SPIF_SSR_REG) & 
            OMAP_SPIF_SSR_RESET_DONE) {
            break;        
        }
        temp--;    
    }
    
    if (0 == temp) {
        DBG_PRINT(SDDBG_ERROR, ("OMAP SPI Reset timeout\n"));     
        DumpRegisters(pHWDevice);    
        return SDIO_STATUS_DEVICE_ERROR;
    }
        
    if (enableautoidle) {
            /* force no IDLE but allow clock gating if the module is idle */
        WRITE_HOST_REG32(pHWDevice, OMAP_SPIF_SCR_REG, 
                    OMAP_SPIF_SCR_NO_IDLE |  OMAP_SPIF_SCR_OCP_GATE |
                    OMAP_SPIF_SCR_OCP_FUNC_CLK_MAIN);
    } else {
           /* force no IDLE and do not allow OCP clock gating */
        WRITE_HOST_REG32(pHWDevice, OMAP_SPIF_SCR_REG, 
                         OMAP_SPIF_SCR_NO_IDLE | OMAP_SPIF_SCR_OCP_FUNC_CLK_MAIN);    
    }
             
        /* setup pin directions */ 
    WRITE_HOST_REG32(pHWDevice, OMAP_SPIF_SYST_REG,
                     OMAP_SPIF_SYST_D1_IN | OMAP_SPIF_SYST_DO_OUT | OMAP_SPIF_SYST_CS_CLK_OUT); 

    WRITE_HOST_REG32(pHWDevice, OMAP_SPIF_MCTRL_REG ,0);
      
        /* restore configuration */    
    WRITE_CHCONF_REG32(pHWDevice, pHWDevice->ChannelConfShadow);
    
        /* enable channel */                          
    WRITE_CHCTRL_REG32(pHWDevice, (OMAP_SPIF_CHCTRL_ENABLED | OMAP_SPIF_CHCTRL_BIG_ENDIAN));
                 
    return SDIO_STATUS_SUCCESS;
    
    
}

/* initialize SPI controller */
static SDIO_STATUS InitSpiController(PSDHCD_HW_DEVICE pHWDevice)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    UINT32      temp;
   
    temp = (UINT32)GET_HOST_REG_BASE(pHWDevice);
    
        /* setup base addresses */
    pHWDevice->pChannelConf = (PVOID)(temp + OMAP_SPIF_CHCONFx_OFFSET(pHWDevice->SPIChannel));
    pHWDevice->pChannelStat = (PVOID)(temp + OMAP_SPIF_CHSTATx_OFFSET(pHWDevice->SPIChannel));
    pHWDevice->pChannelCtrl = (PVOID)(temp + OMAP_SPIF_CHCTRLx_OFFSET(pHWDevice->SPIChannel));
    pHWDevice->pChannelTx = (PVOID)(temp + OMAP_SPIF_CHTXx_OFFSET(pHWDevice->SPIChannel));   
    pHWDevice->pChannelRx = (PVOID)(temp + OMAP_SPIF_CHRXx_OFFSET(pHWDevice->SPIChannel));
        
    pHWDevice->PollWait = 1000000;
   
    
    if (pHWDevice->D0Swap) {
            /* swap module's D0/D1 functionality */
        pHWDevice->ChannelConfShadow = OMAP_SPI_CHCON_D0_TX | OMAP_SPI_CHCON_D1_RX | 
                                     OMAP_SPI_CHCON_D1_NO_TX | OMAP_SPI_CHCON_CS_ACTIVE_LOW |
                                     OMAP_SPI_CHCON_SPI_MODE_0;
    } else {
            /* setup base configuration */
        pHWDevice->ChannelConfShadow = OMAP_SPI_CHCON_D0_RX | OMAP_SPI_CHCON_D1_TX | 
                                     OMAP_SPI_CHCON_D0_NO_TX | OMAP_SPI_CHCON_CS_ACTIVE_LOW |
                                     OMAP_SPI_CHCON_SPI_MODE_0;
    }
  
    DBG_PRINT(SDDBG_TRACE, ("OMAP SPI CHCONF Initial: 0x%X \n", pHWDevice->ChannelConfShadow));
                      
    do {
        
        status = ResetController(pHWDevice);
        
        if (!SDIO_SUCCESS(status)) {
            break;   
        } 
                
        temp = READ_HOST_REG32(pHWDevice,OMAP_SPIF_REV); 
        DBG_PRINT(SDDBG_TRACE, ("OMAP SPI Module Rev: %d.%d\n", 
                (temp & 0xF0) >> 4, (temp & 0x0F)));  
    
    } while (FALSE);
    
    return status;
}


static void DeinitOmapSpiHw(PSDHCD_HW_DEVICE pHWDevice)
{   
    if (pHWDevice->InitStateMask & SDIO_IRQ_INTERRUPT_INIT) { 
        pHWDevice->InitStateMask &= ~SDIO_IRQ_INTERRUPT_INIT;
        HW_EnableDisableSPIIRQ(pHWDevice->pDevice,FALSE, HW_FROM_NORMAL_CONTEXT); 
        free_irq(pHWDevice->Interrupt, pHWDevice); 
        DBG_PRINT(SDDBG_TRACE, ("OMAP SPI HCD: Free IRQ %d \n", pHWDevice->Interrupt));
    }  
     
    if (pHWDevice->InitStateMask & SDHC_HW_INIT) {
            /* put the controller in reset */
        ResetController(pHWDevice);    
    }
    
    if (pHWDevice->DmaTxChan != -1) {
        omap_free_dma(pHWDevice->DmaTxChan);
        pHWDevice->DmaTxChan = -1;    
    }
    
    if (pHWDevice->DmaRxChan != -1) {
        omap_free_dma(pHWDevice->DmaRxChan);
        pHWDevice->DmaRxChan = -1;    
    }
    
    if (0 == srambuffer) {  
        if (pHWDevice->pDmaBuffer != NULL) {
            consistent_free(pHWDevice->pDmaBuffer, pHWDevice->CommonBufferSize, pHWDevice->hDmaBuffer);
            pHWDevice->pDmaBuffer = NULL; 
        } 
        
        if (pHWDevice->pDmaDummyBuffer != NULL) {
            consistent_free(pHWDevice->pDmaDummyBuffer, TX_DUMMY_BUFFER_SIZE, pHWDevice->hDmaDummyBuffer);
            pHWDevice->pDmaDummyBuffer = NULL;
        }
    }  
}


/////////////////  DMA optimization /////////////////////////////
#ifdef ATH_USE_OPTIMAL_DMA
#define OMAP_DMA4_CCR_EN (1 << 7)

void OmapInitializeDmaSettings(volatile POMAP_DMA_REGS pRegs, 
                               INT    DmaId, 
                               BOOL   SrcSync,
                               UINT32 DestAddressMode,
                               UINT32 SrcAddressMode,
                               UINT32 DestAddress,
                               UINT32 SrcAddress)
{  
    UINT32  temp; 
    volatile PUINT32 pCCR = &pRegs->CCR;
    volatile PUINT32 pCFN = &pRegs->CFN;
    volatile PUINT32 pCDSA = &pRegs->CDSA;
    volatile PUINT32 pCSSA = &pRegs->CSSA;
     
    temp = *pCCR;
    temp &= ~((0x03 << 14) | (0x03 << 12)) ;
    temp |= DestAddressMode << 14;
    temp |= SrcAddressMode << 12;
    
    *pCDSA = DestAddress;
    *pCSSA = SrcAddress;
    
    if (DmaId & (1 << 6)) {
        temp |= 1 << 20;
    }
    if (DmaId & (1 << 5)) {
        temp |= 1 << 19;
    } 

    temp |= (DmaId & 0x1f);

    if (SrcSync) {
        temp |= 1 << 24; 
    } else {
        temp &= ~(1 << 24);
    } 
    
    *pCCR = temp;
    *pCFN = 1;
}

static inline void OmapSetDmaDataType(volatile POMAP_DMA_REGS pRegs, UINT8 Type)
{
    UINT32 temp;
    
    volatile PUINT32 pCSDP = &pRegs->CSDP;
    temp = *pCSDP; 
    temp &= ~0x03;
    
    if (Type == ATH_TRANS_DS_32) {
         temp |= OMAP_DMA_DATA_TYPE_S32;
    } else if (Type == ATH_TRANS_DS_16) {
         temp |= OMAP_DMA_DATA_TYPE_S16;
    } else {
         temp |= OMAP_DMA_DATA_TYPE_S8;
    }
    
    *pCSDP = temp;                          
}

static inline void OmapStartDmaTransferRx(volatile POMAP_DMA_REGS pRegs,
                                          UINT32   DestAddress,
                                          UINT32   Length) 
{
    UINT32 temp;
    volatile PUINT32 pCCR = &pRegs->CCR;
    volatile PUINT32 pCEN = &pRegs->CEN;
    volatile PUINT32 pCDSA = &pRegs->CDSA;
    
    *pCEN = Length;     
    temp = *pCCR;
    temp |= OMAP_DMA4_CCR_EN;
    *pCCR = temp;
    *pCDSA = DestAddress;
    
}

static inline void OmapStartDmaTransferTx(volatile POMAP_DMA_REGS pRegs, 
                                          UINT32   SrcAddress,
                                          UINT32   Length,
                                          BOOL     TxDummy) 
{
    UINT32 temp;
    volatile PUINT32 pCCR = &pRegs->CCR;
    volatile PUINT32 pCEN = &pRegs->CEN;
    volatile PUINT32 pCSSA = &pRegs->CSSA;
   
    temp = *pCCR;
        /* clear out source address mode */
    temp &= ~(0x03 << 12) ;       
    if (TxDummy) {
        temp |= OMAP_DMA_AMODE_CONSTANT << 12;
    } else {
        temp |= OMAP_DMA_AMODE_POST_INC << 12;
    }
    temp |= OMAP_DMA4_CCR_EN;
    
    *pCSSA = SrcAddress; 
    *pCEN = Length;
    /* start DMA */
    *pCCR = temp;
}
#endif

#define OMAP_CONTROL_PADCONF_BASE_ADDRESS 0x48000000
#define OMAP_CONTROL_PADCONF_SIZE 0x0400  
#define CONTROL_PADCONF_SPI1_NCS2 0x0104
#define CONTROL_PADCONF_Y11       0x00E8

#define OMAP_PAD_PULLUPDWN_ENABLE (1 << 3)
#define OMAP_PAD_PULLUP_TYPE      (1 << 4)
#define OMAP_PAD_PULLDOWN_TYPE    (0 << 4)

static void OmapPadConfig(PSDHCD_HW_DEVICE pHWDevice, UINT32 Offset, UINT8 ByteOffset, UINT8 PadValue)
{
    UINT32 value;
    value = readl(pHWDevice->PadConfigBase+Offset);
    value &= ~((UINT32)0xff << (ByteOffset*8));
    value |= (UINT32)PadValue << (ByteOffset*8);
    writel(value,pHWDevice->PadConfigBase+Offset);
}

/*
 * setup the OMAP SPI Hardware  
*/
static SDIO_STATUS InitOmapSpiHw(PSDHCD_HW_DEVICE pHWDevice, UINT deviceNumber)
{
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
    ULONG       baseAddress = 0;
    INT         err;
          
    do { 

        pHWDevice->PadConfigBase = (UINT32)ioremap(OMAP_CONTROL_PADCONF_BASE_ADDRESS,
                                                 OMAP_CONTROL_PADCONF_SIZE); 
        
        DBG_ASSERT(pHWDevice->PadConfigBase != 0);
               
            /* configure GPIO pad, initially we enable pullups/pulldowns until the SPI module
             * is initialized in the common layer */
        OmapPadConfig(pHWDevice, 
                      int_gpio_pad_conf_offset,
                      int_gpio_pad_conf_byte, 
                      int_gpio_pad_mode_value | OMAP_PAD_PULLUPDWN_ENABLE | OMAP_PAD_PULLUP_TYPE);
                          
        pHWDevice->DmaTxChan = -1;
        pHWDevice->DmaRxChan = -1;
        pHWDevice->SpiIntGPIOPin = int_gpio;
           
        pHWDevice->Interrupt = OMAP_GPIO_IRQ_NO(pHWDevice->SpiIntGPIOPin);
        
         DBG_PRINT(SDDBG_TRACE, 
            ("OMAP SPI  HCD: GpioPin:%d (IRQ:%d), CONF_OFFSET:0x%X, CONF_BYTES:%d, CONF_MODE:%d\n",
            pHWDevice->SpiIntGPIOPin, 
            pHWDevice->Interrupt,
            int_gpio_pad_conf_offset,
            int_gpio_pad_conf_byte, 
            int_gpio_pad_mode_value));   
            
        pHWDevice->pDevice->MaxBytesPerDMARequest = MaxBytesPerDMARequest;        
            /* allocate a DMA buffer large enough for the common buffer. This buffer
             * has to be sized to 2x the desired size because 8-bit SPI mode uses
             * 16 bit DMA */                            
        pHWDevice->CommonBufferSize = 2*MaxBytesPerDMARequest;
        
        if (0 == srambuffer) {         
            /* allocate a DMA buffer large enough for the command buffers and the data buffers */
            pHWDevice->pDmaBuffer = consistent_alloc(GFP_KERNEL | GFP_DMA | GFP_ATOMIC,
                                                   pHWDevice->CommonBufferSize, &pHWDevice->hDmaBuffer);
            
                  
            if (pHWDevice->pDmaBuffer == NULL) {
                DBG_PRINT(SDDBG_ERROR, ("OMAP SPI  HCD: InitOmap - unable to get DMA buffer\n"));
                status = SDIO_STATUS_NO_RESOURCES;
                break;
            }
            
            pHWDevice->pDmaDummyBuffer =  consistent_alloc(GFP_KERNEL | GFP_DMA | GFP_ATOMIC,
                                                         TX_DUMMY_BUFFER_SIZE, &pHWDevice->hDmaDummyBuffer);
            
            if (pHWDevice->pDmaDummyBuffer == NULL) {
                DBG_PRINT(SDDBG_ERROR, ("OMAP SPI  HCD: InitOmap - unable to get Dummy DMA buffer\n"));
                status = SDIO_STATUS_NO_RESOURCES;
                break;
            }
                /* this is the dummy buffer holding FFs */
            memset(pHWDevice->pDmaDummyBuffer, 0xFF, TX_DUMMY_BUFFER_SIZE);
            
        } else {
            pHWDevice->pDmaBuffer = ioremap(srambuffer,pHWDevice->CommonBufferSize + TX_DUMMY_BUFFER_SIZE); 
                                    //(PVOID)IO_ADDRESS(srambuffer);
            pHWDevice->hDmaBuffer = (DMA_ADDRESS)srambuffer;
            pHWDevice->pDmaDummyBuffer = (PVOID)((UINT32)pHWDevice->pDmaBuffer + pHWDevice->CommonBufferSize);
            pHWDevice->hDmaDummyBuffer = (DMA_ADDRESS)(srambuffer + pHWDevice->CommonBufferSize);
        }
        
        DBG_PRINT(SDDBG_TRACE, ("OMAP SPI  HCD: InitOmap - pDmaBuffer: 0x%X, hDmaBuffer: 0x%X from %s \n",
                     (UINT)pHWDevice->pDmaBuffer , (UINT)pHWDevice->hDmaBuffer,
                     srambuffer ? "SRAM" : "System Memory"));
                                                        
        if (gpiodebug) {
            OmapPadConfig(pHWDevice, 
                          SPI_DBG1_PIN_PAD_CONF_OFFSET, 
                          SPI_DBG1_PIN_PAD_CONF_BYTEPOS, 
                          SPI_DBG1_PIN_PAD_CONF_MODE| OMAP_PAD_PULLUPDWN_ENABLE | OMAP_PAD_PULLUP_TYPE);
            omap_set_gpio_direction(SPI_DBG1_PIN, OMAP2420_DIR_OUTPUT);
            omap_set_gpio_dataout(SPI_DBG1_PIN,FALSE);
            DBG_PRINT(SDDBG_TRACE, ("OMAP SPI : GPIO DEBUG Enabled \n"));
            HW_ToggleDebugSignal(pHWDevice->pDevice, 1);
        }
        
        pHWDevice->SPIChannel = spichan;
         
        if (1 == spimodule) {
            baseAddress = OMAP_SPIF1_BASE;
            
            OmapPadConfig(pHWDevice, 
                          CONTROL_PADCONF_SPI1_NCS2, 
                          0, 
                          0x0); /* make sure pullup is disabled on CS pin */
                      
            switch (pHWDevice->SPIChannel) {
                case 0:
                    pHWDevice->DmaRxId = OMAP_DMA_SPI1_RX0;
                    pHWDevice->DmaTxId = OMAP_DMA_SPI1_TX0;
                    break;
                case 1:
                    pHWDevice->DmaRxId = OMAP_DMA_SPI1_RX1;
                    pHWDevice->DmaTxId = OMAP_DMA_SPI1_TX1;
                    break;  
                case 2:
                    pHWDevice->DmaRxId = OMAP_DMA_SPI1_RX2;
                    pHWDevice->DmaTxId = OMAP_DMA_SPI1_TX2;
                    break;  
                case 3:
                    pHWDevice->DmaRxId = OMAP_DMA_SPI1_RX3;
                    pHWDevice->DmaTxId = OMAP_DMA_SPI1_TX3;
                    break;  
                default:
                    DBG_ASSERT(FALSE);
                    break;  
            }
        } else if (2 == spimodule) {
            baseAddress = OMAP_SPIF2_BASE;
            //??? TODO, the SPI2 chip select could be on different PADs, need to 
            //?? configure the pin setting         
            switch (pHWDevice->SPIChannel) {
                case 0:
                    pHWDevice->DmaRxId = OMAP_DMA_SPI2_RX0;
                    pHWDevice->DmaTxId = OMAP_DMA_SPI2_TX0;
                    break;
                case 1:
                    pHWDevice->DmaRxId = OMAP_DMA_SPI2_RX1;
                    pHWDevice->DmaTxId = OMAP_DMA_SPI2_TX1;
                    break;  
                default:
                    DBG_ASSERT(FALSE);
                    break;  
            }   
        } else {
            DBG_ASSERT(FALSE);    
        }
        
        DBG_PRINT(SDDBG_TRACE, ("OMAP SPI : spi module:%d, channel:%d\n",
            spimodule, pHWDevice->SPIChannel));
          
            /* map the memory address for the control registers */
        pHWDevice->Address.pMapped = (PVOID)IO_ADDRESS(baseAddress);
        pHWDevice->Address.Raw = baseAddress;
        DBG_PRINT(SDDBG_TRACE , ("OMAP SPI  - InitOMAP 0x%X\n", (UINT)pHWDevice->Address.pMapped));
        pHWDevice->InitStateMask |= SDIO_BASE_MAPPED;
   
        err = omap_request_dma(pHWDevice->DmaTxId, 
                               "RAWSPITX", 
                               SD_TxDMACallback, 
                               pHWDevice, 
                               &pHWDevice->DmaTxChan);
    
        if (err < 0) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI , can't get TX DMA channel: %d\n",
                      err));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
        
        err = omap_request_dma(pHWDevice->DmaRxId, 
                               "RAWSPIRX", 
                               SD_RxDMACallback, 
                               pHWDevice, 
                               &pHWDevice->DmaRxChan);
        
        if (err < 0) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI , can't get RX DMA channel: %d, err:%d\n",
                      pHWDevice->DmaRxId, err));
            status = SDIO_STATUS_NO_RESOURCES;
            break;
        }
         
        pHWDevice->pTxDmaRegs = (volatile POMAP_DMA_REGS)OMAP_DMA4_CCR_REG(pHWDevice->DmaTxChan);  
        pHWDevice->pRxDmaRegs = (volatile POMAP_DMA_REGS)OMAP_DMA4_CCR_REG(pHWDevice->DmaRxChan); 

#ifdef ATH_USE_OPTIMAL_DMA    
        OmapInitializeDmaSettings(pHWDevice->pTxDmaRegs,
                                  pHWDevice->DmaTxId, 
                                  FALSE,
                                  OMAP_DMA_AMODE_CONSTANT,
                                  OMAP_DMA_AMODE_POST_INC,
                                  (UINT32)(pHWDevice->Address.Raw + OMAP_SPIF_CHTXx_OFFSET(pHWDevice->SPIChannel)),
                                  (UINT32)pHWDevice->hDmaBuffer);
        
        OmapSetDmaDataType(pHWDevice->pTxDmaRegs, OMAP_DMA_DATA_TYPE_S16);
        
        OmapInitializeDmaSettings(pHWDevice->pRxDmaRegs,
                                  pHWDevice->DmaRxId, 
                                  TRUE,
                                  OMAP_DMA_AMODE_POST_INC,
                                  OMAP_DMA_AMODE_CONSTANT,
                                  (UINT32)pHWDevice->hDmaBuffer,
                                  (UINT32)(pHWDevice->Address.Raw + OMAP_SPIF_CHRXx_OFFSET(pHWDevice->SPIChannel)));
        
        OmapSetDmaDataType(pHWDevice->pRxDmaRegs, OMAP_DMA_DATA_TYPE_S16);
 #endif
     
        pHWDevice->LastDmaSize = OMAP_DMA_DATA_TYPE_S16;
 
#ifdef CHECK_DMA_TIMEOUT   
        init_timer(&pHWDevice->DMATimer);       
        pHWDevice->DMATimer.function = DMATimeout;
        pHWDevice->DMATimer.data = (unsigned long)pHWDevice; 
#endif
        init_timer(&pHWDevice->Timer);       
        pHWDevice->Timer.function = TimerTimeout;
        pHWDevice->Timer.data = (unsigned long)pHWDevice; 
        
            /* init SPI host controller */
        status = InitSpiController(pHWDevice);
        
        if (!SDIO_SUCCESS(status)) {
            break;    
        }
        
        pHWDevice->InitStateMask |= SDHC_HW_INIT;
         
            /* map the GPIO interrupt  */
        err = request_irq(pHWDevice->Interrupt, hcd_spi_irq, 0,
                          pHWDevice->pDevice->Hcd.pName, pHWDevice);
                          
        if (err < 0) {
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI  HCD: OmapInit, unable to map interrupt (%d) \n",
               err));
            status = SDIO_STATUS_ERROR;
            break;
        }
            /* disable it for now */
        omap_set_gpio_direction(pHWDevice->SpiIntGPIOPin, OMAP2420_DIR_INPUT);
        omap_set_gpio_edge_ctrl(pHWDevice->SpiIntGPIOPin, OMAP_GPIO_NO_EDGE);
                            
        pHWDevice->IrqEnabled = FALSE;
        pHWDevice->InitStateMask |= SDIO_IRQ_INTERRUPT_INIT;     
       
    } while (FALSE);
    
    if (!SDIO_SUCCESS(status)) {
        DeinitOmapSpiHw(pHWDevice);    
    }
    
    return status;
}
            
/*
 * unsetup the OMAP registers 
*/



void DumpDMASettings(PSDHCD_HW_DEVICE pHWDevice, BOOL TX)
{
#ifndef ATH_HCD_NODEBUG    
    int channel = TX ? pHWDevice->DmaTxChan : pHWDevice->DmaRxChan;
#endif
    
    DBG_PRINT(SDDBG_TRACE, ("OMAP SPI  DMA Reg Dump (%s) Channel:0x%X, DMAREQ:%d \n", 
             TX ? "Transmit":"Receive", channel,
             TX ? pHWDevice->DmaTxId:pHWDevice->DmaRxId));
    DBG_PRINT(SDDBG_TRACE, ("  CCR       : 0x%X \n",_READ_DWORD_REG(OMAP_DMA4_CCR_REG(channel))));           
    DBG_PRINT(SDDBG_TRACE, ("  CLNK_CTRL : 0x%X \n",_READ_DWORD_REG(OMAP_DMA4_CLNK_CTRL_REG(channel))));   
    DBG_PRINT(SDDBG_TRACE, ("  CICR      : 0x%X \n",_READ_DWORD_REG(OMAP_DMA4_CICR_REG(channel))));    
    DBG_PRINT(SDDBG_TRACE, ("  CSR       : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CSR_REG(channel))));
    DBG_PRINT(SDDBG_TRACE, ("  CSDP      : 0x%X \n",_READ_DWORD_REG(OMAP_DMA4_CSDP_REG(channel))));   
    DBG_PRINT(SDDBG_TRACE, ("  CEN       : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CEN_REG(channel)))); 
    DBG_PRINT(SDDBG_TRACE, ("  CFN       : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CFN_REG(channel))));     
    DBG_PRINT(SDDBG_TRACE, ("  CSSA      : 0x%X \n",_READ_DWORD_REG(OMAP_DMA4_CSSA_REG(channel))));    
    DBG_PRINT(SDDBG_TRACE, ("  CDSA      : 0x%X \n",_READ_DWORD_REG(OMAP_DMA4_CDSA_REG(channel))));    
    DBG_PRINT(SDDBG_TRACE, ("  CSEI      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CSEI_REG(channel))));
    DBG_PRINT(SDDBG_TRACE, ("  CSFI      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CSFI_REG(channel))));
    DBG_PRINT(SDDBG_TRACE, ("  CDEI      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CDEI_REG(channel))));
    DBG_PRINT(SDDBG_TRACE, ("  CDFI      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CDFI_REG(channel))));
    DBG_PRINT(SDDBG_TRACE, ("  CSAC      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CSAC_REG(channel)))); 
    DBG_PRINT(SDDBG_TRACE, ("  CDAC      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CDAC_REG(channel))));
    DBG_PRINT(SDDBG_TRACE, ("  CCEN     : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CCEN_REG(channel)))); 
    DBG_PRINT(SDDBG_TRACE, ("  CCFN      : 0x%X \n", _READ_DWORD_REG(OMAP_DMA4_CCFN_REG(channel))));
    
}

#define OMAP_DMA_ERRORS  (OMAP_DMA_TOUT_IRQ | OMAP_DMA_DROP_IRQ)

/* this function stops the DMA when the last callback calls into this function.
 * A count is decremented on each call (from each callback), when count=0, the DMA 
 * channels are stopped */
static INLINE void SyncStopDMA(PSDHCD_HW_DEVICE pHWDevice)
{

    pHWDevice->DmaStopCount--;
    DBG_ASSERT(pHWDevice->DmaStopCount >= 0);   
    
    if (pHWDevice->DmaStopCount <= 0) {
                
        if (pHWDevice->pDevice->CurrentTransferDirRx) {
                /* stop RX channel */
            omap_stop_dma(pHWDevice->DmaRxChan);
        }
        
            /* for TX requests OR RX requests stop the TX channel.
             * RX transfers run the TX channel using a dummy buffer */    
        omap_stop_dma(pHWDevice->DmaTxChan);        
    
            /* disable DMA on SPI channel */ 
        WRITE_CHCONF_REG32(pHWDevice, 
                       (READ_CHCONF_REG32(pHWDevice) & (~(OMAP_SPI_CHCON_DMA_READ | OMAP_SPI_CHCON_DMA_WRITE))));   
     }
     
}


/* DMA callback */
static void SD_TxDMACallback(int lch, UINT16 DMAStatus, PVOID pContext)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext;
    SDIO_STATUS   status = SDIO_STATUS_SUCCESS;

    DBG_PRINT(ATH_SPI_TRACE_DATA, ("OMAP SPI  TX DMA COMPLETE - status: %d, channel: 0x%X\n", 
             (UINT)DMAStatus, lch));
   
        /* override the callback DMA status, the OMAP dma interrupt handler does not
         * pass the correct status */
    DMAStatus = (UINT16)_READ_DWORD_REG(OMAP_DMA4_CSR_REG(pHWDevice->DmaTxChan));
    
   
    if (DMAStatus == OMAP_DMA_SYNC_IRQ) { 
        return; 
    }
    
    
    OMAP_CANCEL_DMA_TIMER(pHWDevice);    
        /* stop DMA */   
    SyncStopDMA(pHWDevice);
    
        /* handle errors */
    if (DMAStatus & OMAP_DMA_ERRORS) {
        status = SDIO_STATUS_DEVICE_ERROR;
    }
    
    if (!(DMAStatus & OMAP_DMA_BLOCK_IRQ)) {
        /* TODO, the dma interrupt handler code in the kernel does not
         * return the correct status 
         * 
         * status = SDIO_STATUS_DEVICE_ERROR; */
    }
    
    if (!SDIO_SUCCESS(status)) {
         DBG_PRINT(SDDBG_WARN, ("OMAP SPI  SD_TxDMACallback DMA error status: 0x%X\n", 
         DMAStatus));   
         DumpDMASettings(pHWDevice, TRUE);
    }

        /* don't call completion for the dummy TX DMA */
    if (!pHWDevice->DummyTxDmaActive) {
            /* for normal TX, queue the bottom half to do the completion */
        pHWDevice->DMACompleteStatus = status;
        QueueWork(pHWDevice, &pHWDevice->dmacomplete_work);
    }
    
    return;    
}

static void SD_RxDMACallback(int lch, UINT16 DMAStatus, PVOID pContext)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)pContext;
    SDIO_STATUS   status = SDIO_STATUS_SUCCESS;

    DBG_PRINT(ATH_SPI_TRACE_DATA, ("OMAP SPI  RX DMA COMPLETE - status: %d, channel: 0x%X\n", 
            (UINT)DMAStatus, lch));
   
        /* override the callback DMA status, the OMAP dma interrupt handler does not
         * pass the correct status */
    DMAStatus = (UINT16)_READ_DWORD_REG(OMAP_DMA4_CSR_REG(pHWDevice->DmaRxChan));
    
    if (DMAStatus == OMAP_DMA_SYNC_IRQ) {
        return; 
    }
    
    OMAP_CANCEL_DMA_TIMER(pHWDevice);
        /* stop DMA */
    SyncStopDMA(pHWDevice);
         
        /* handle errors */
    if (DMAStatus & OMAP_DMA_ERRORS) {
        status = SDIO_STATUS_DEVICE_ERROR;
    }
    
    if (!(DMAStatus & OMAP_DMA_BLOCK_IRQ)) {
        /* TODO, the dma interrupt handler code in the kernel does not
         * return the correct status , so if we do not get the BLOCK_IRQ interrupt, something bad happened
         * 
         * status = SDIO_STATUS_DEVICE_ERROR; */
    }
    
    if (!SDIO_SUCCESS(status)) {
         DBG_PRINT(SDDBG_WARN, ("OMAP SPI  SD_RxCallback DMA error status: 0x%X\n", 
         DMAStatus));
         DumpDMASettings(pHWDevice, FALSE);
    } else {        
        if (!pHWDevice->DMASg) {
            PSDHCD_DEVICE pDevice = pHWDevice->pDevice;  
                /* copy common buffer back */
            if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_8) {
                    /* we transferred this using 16 bit DMA transfers */
                PUINT8  pData8; 
                PUINT16 pDma16; 
                int i;
                pDma16 = (PUINT16)pHWDevice->pDmaBuffer;
                pData8 = (PUINT8)pDevice->pCurrentBuffer;
                for (i = 0 ; i < pDevice->CurrentTransferLength; i++,pDma16++,pData8++) {
                    *pData8 = (UINT8)*pDma16;
                }  
            } else {
                HcdCommonBufferCopy(pDevice->CurrentDmaWidth,
                                    pDevice->pCurrentBuffer,
                                    pHWDevice->pDmaBuffer,
                                    pDevice->CurrentTransferLength,
                                    pDevice->HostDMABufferCopyMode);
            }
        }
    }
    
        /* queue the bottom half to do the completion */
    pHWDevice->DMACompleteStatus = status;
    QueueWork(pHWDevice, &pHWDevice->dmacomplete_work);
        
    return;    
}

#ifdef ATH_USE_OPTIMAL_DMA
static inline void SetupOMAPDMAOpt(PSDHCD_HW_DEVICE  pHWDevice, 
                                   UINT              Type, 
                                   int               Length, 
                                   DMA_ADDRESS       SystemDmaAddress, 
                                   DMA_TRANSFER_TYPE DmaType)
{
    if (Type != pHWDevice->LastDmaSize) {
            /* this is an optimization so we don't have to reset a bunch of registers
             * that do not change between operations */
        OmapSetDmaDataType(pHWDevice->pRxDmaRegs, Type);
        OmapSetDmaDataType(pHWDevice->pTxDmaRegs, Type);   
        pHWDevice->LastDmaSize = Type;
    }
    
    if (Type == ATH_TRANS_DS_32) {
        Length >>= 2;   
    } else if (Type == ATH_TRANS_DS_16) {
        Length >>= 1;
    }
    
    if (DMA_RX_TYPE == DmaType) {
                                                    
        OmapStartDmaTransferRx(pHWDevice->pRxDmaRegs, 
                               (UINT32)SystemDmaAddress,
                               Length);

    } else if (DMA_TX_TYPE == DmaType) {
                       
        OmapStartDmaTransferTx(pHWDevice->pTxDmaRegs, 
                               (UINT32)SystemDmaAddress,
                               Length,
                               FALSE);     
            
    } else {
            /* dummy TX */
        OmapStartDmaTransferTx(pHWDevice->pTxDmaRegs, 
                               (UINT32)SystemDmaAddress,
                               Length,
                               TRUE);     
    }

    DBG_PRINT(ATH_SPI_TRACE_INFO, ("OMAP SPI  DMA channel sync ID: %d \n", 
             (DMA_RX_TYPE == DmaType) ? pHWDevice->DmaRxId : pHWDevice->DmaTxId));
        
    if (DBG_GET_DEBUG_LEVEL() >= ATH_SPI_TRACE_DMA_DUMP) {
        DumpDMASettings(pHWDevice, (DMA_RX_TYPE == DmaType) ? FALSE:TRUE);
    }                                               
}

#else
static inline void SetupOMAPDMA(PSDHCD_HW_DEVICE     pHWDevice, 
                         UINT              Type, 
                         int               Length, 
                         DMA_ADDRESS       SystemDmaAddress, 
                         DMA_TRANSFER_TYPE DmaType)
{
    int channel = (DMA_RX_TYPE == DmaType) ? pHWDevice->DmaRxChan : pHWDevice->DmaTxChan;
    int dataType;
    
    if (Type == ATH_TRANS_DS_32) {
        Length >>= 2;   
        dataType = OMAP_DMA_DATA_TYPE_S32;
    } else if (Type == ATH_TRANS_DS_16) {
        Length >>= 1;
        dataType = OMAP_DMA_DATA_TYPE_S16;
    } else {
        dataType = OMAP_DMA_DATA_TYPE_S8;
    }
     
    omap_set_dma_transfer_params(channel,
                                 dataType,
                                 Length,
                                 1,
                                 OMAP_DMA_SYNC_ELEMENT,
                                 (DMA_RX_TYPE == DmaType) ? pHWDevice->DmaRxId : pHWDevice->DmaTxId,
                                 (DMA_RX_TYPE == DmaType) ? TRUE : FALSE);


    if (DMA_RX_TYPE == DmaType) {
        omap_set_dma_src_params(channel,
                                OMAP_DMA_AMODE_CONSTANT,
                                (int)(pHWDevice->Address.Raw + OMAP_SPIF_CHRXx_OFFSET(pHWDevice->SPIChannel)),
                                0,
                                0); 
      
        omap_set_dma_dest_params(channel,
                                 OMAP_DMA_AMODE_POST_INC,
                                 SystemDmaAddress,
                                 0, 
                                 0);      

    } else {
        
        if (DMA_TX_TYPE == DmaType) {
                /* source is system memory */
            omap_set_dma_src_params(channel,
                                    OMAP_DMA_AMODE_POST_INC,
                                    (int)SystemDmaAddress,
                                    0, 
                                    0);
        } else {
            omap_set_dma_src_params(channel,
                                    OMAP_DMA_AMODE_CONSTANT,
                                    (int)SystemDmaAddress,
                                    0,
                                    0);
        }                  
        omap_set_dma_dest_params(channel,
                                OMAP_DMA_AMODE_CONSTANT,
                                (int)(pHWDevice->Address.Raw + OMAP_SPIF_CHTXx_OFFSET(pHWDevice->SPIChannel)),
                                0,
                                0); 
    }

    DBG_PRINT(ATH_SPI_TRACE_INFO, ("OMAP SPI  DMA channel sync ID: %d \n", 
             (DMA_RX_TYPE == DmaType) ? pHWDevice->DmaRxId : pHWDevice->DmaTxId));
   
    omap_start_dma(channel);
       
    if (DBG_GET_DEBUG_LEVEL() >= ATH_SPI_TRACE_DMA_DUMP) {
        DumpDMASettings(pHWDevice, (DMA_RX_TYPE == DmaType) ? FALSE:TRUE);
    }                                        
}
#endif

/* prepare scatter gather list */
static BOOL PrepareSG(struct scatterlist *pSg, int Entries, BOOL ToDevice)
{
    int           i;
            
    for (i = 0; i < Entries; i++,pSg++) {
        if (pSg->address) {
            if (ToDevice) {
                consistent_sync(pSg->address,pSg->length,PCI_DMA_TODEVICE);
            } else {
                consistent_sync(pSg->address,pSg->length,PCI_DMA_FROMDEVICE);
            }
            /* virt_to_bus is broken on 2.4.20, 
             * pSg->dma_address = virt_to_bus(pSg->address); */
            pSg->dma_address = page_to_phys(pSg->page) + pSg->offset;
        } else {
            DBG_ASSERT(FALSE);
            return FALSE;
        }
    }
    
    return TRUE;
}

/* Check the current request buffer for cache-line hazards. Returns TRUE if a hazard exists
 * 
 * This driver can directly map the incomming request buffer to hardware DMA.
 * Since the driver is checking the virtual address of the request buffer,
 * it cannot know for sure if the function driver has allocated a buffer that
 * has cache-line side effects.  This function checks for hazards and returns 
 * a flag on whether to allow direct-DMA.  There are two cases to check for:
 * 
 * TX - in the TX direction, there are no cacheline hazards to deal with, once
 *      the write buffer is submitted, the caller naturally treats the buffer as read-only.
 *      Cache-line re-fills/evictions will not change the data in system memory (it would
 *      just write the same data).
 * 
 * RX - in the RX direction, the buffer must start and end on a cache-line boundary
 *      otherwise a potential hazard exists.  If any part of this buffer straddles
 *      a cacheline, the CPU could update/modify the cacheline "just" after DMA has updated
 *      the line in system memory.  Here is one possible scenario:
 * 
 *      1. CPU invalidates buffer, invalidating the cache line as it prepares for DMA.
 *      2. Before DMA starts, the CPU references the same cache line (its a very narrow case, but it
 *         is still possible).  The line is re-loaded from system memory, DMA has not started yet, 
 *         so the re-loaded line represents the data present in system memory.
 *      3. DMA starts and data is moved into that line in system memory.  On OMAP, DMA does not snoop
 *         the caches.
 *      4. CPU references the cache-line again, this time it is a series of writes that
 *         force the cacheline to get flushed out (write-back). DMA is still running here. 
 *         The cache-line is not coherent anymore because of step 2. 
 *         The write-back over-writes the DMA data that was present.
 */
static INLINE BOOL CheckCurrentRequestCacheLineHazard(PSDHCD_DEVICE   pDevice)
{
    UINT32 address = (UINT32)pDevice->pCurrentBuffer;
    
    if (!pDevice->CurrentTransferDirRx) {
            /* TX buffers have no cache-line hazards */
        return FALSE;    
    }
    
    if (address & OMAP_CPU_CACHE_ALIGN_MASK) {
            /* start of RX buffer is not cache aligned, there is a cacheline hazard at the front of the buffer */
        DBG_PRINT(ATH_SPI_TRACE_DATA, ("CheckCurrentRequestCacheLineHazard: ALIGN FRONT 0x%X \n",address));
        return TRUE;    
    }
    
        /* get the address that follows the end of this buffer */
    address += pDevice->CurrentTransferLength;
    
        /* check if that address starts on a cache line boundary */
    if (address & OMAP_CPU_CACHE_ALIGN_MASK) {
        /* end of RX buffer has a cache-line hazard, it does not end at the cache-line boundary */
        DBG_PRINT(ATH_SPI_TRACE_DATA, ("CheckCurrentRequestCacheLineHazard: ALIGN END: 0x%X \n", address));
        return TRUE;    
    }    
            
    return FALSE;
}


/* set up SPI host controller DMA */
SDIO_STATUS HW_SpiSetUpDMA(PSDHCD_DEVICE    pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    UINT8            actualDMAWidth;
    int              actualDMATransferBytes;
    SDIO_STATUS      status;
    BOOL             checkSGDma = (BOOL)allow_sg_dma;
    
    DBG_ASSERT(pDevice->CurrentTransferLength != 0);
    DBG_ASSERT(pDevice->pCurrentBuffer != 0);
    
        /* before setting up DMA, make sure any pipelined writes are completed */
    if (pHWDevice->WaitTxDone) {
        pHWDevice->WaitTxDone = FALSE;
        status = WaitLastOp(pHWDevice);            
        if (!SDIO_SUCCESS(status)) {
            return status;   
        }
    }
    
    pHWDevice->DMASg = FALSE;
        
    DBG_PRINT(ATH_SPI_TRACE_DATA, ("OMAP Hw_SpiSetUpDMA (%s): byte length: %d \n",
            pDevice->CurrentTransferDirRx ? "RX":"TX",pDevice->CurrentTransferLength));
   
    if (checkSGDma) {
            /* if the driver is running with SG DMA enabled, make sure buffer is
             * aligned to the data width, the OMAP DMA accesses the SPI controller
             * with the same data-width so the address must be aligned to
             * the width */
        if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_16) {
            if ((UINT32)pDevice->pCurrentBuffer & 0x01) {
                    /* not 16 bit aligned */
                checkSGDma = FALSE;
            }    
        } else if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_32) {
            if ((UINT32)pDevice->pCurrentBuffer & 0x03) {
                    /* not 32 bit aligned */
                checkSGDma = FALSE;
            }       
        }
    }
            
    if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_8) {
        /* note: 8 bit transfers use 16 bit DMA because the OMAP SPI controller only allows 16/32 bit
         * access, thus DMA reads/write in 16 bit mode */    
        actualDMAWidth = ATH_TRANS_DS_16;
        actualDMATransferBytes = pDevice->CurrentTransferLength << 1;
            /* can't use SG dma for this, this has to use common buffer DMA */
        checkSGDma = FALSE;
    } else {
        actualDMAWidth = pDevice->CurrentDmaWidth;
        actualDMATransferBytes = pDevice->CurrentTransferLength;  
    }
       
        /* check DMA and check cache line hazards */   
    if (checkSGDma && !CheckCurrentRequestCacheLineHazard(pDevice)) {        
    
            /* for 16 and 32 bit DMA we can try to map the current request buffer directly */
        pHWDevice->CurrentScatterEntries = HcdMapCurrentRequestBuffer(pDevice, 
                                                                      pHWDevice->DMAMapList,
                                                                      MAX_DMA_SCATTER_ENTRIES);
        if (pHWDevice->CurrentScatterEntries != 0) {
            DBG_ASSERT(pHWDevice->CurrentScatterEntries <= MAX_DMA_SCATTER_ENTRIES);
                /* prepare scatter entries and flush the caches */
            if (PrepareSG(pHWDevice->DMAMapList, 
                          pHWDevice->CurrentScatterEntries,
                          pDevice->CurrentTransferDirRx ? FALSE : TRUE)) {
                pHWDevice->DMASg = TRUE; 
            }  
        }
    }
           
    if (pHWDevice->DMASg) {
        DBG_PRINT(ATH_SPI_TRACE_DATA, 
                  ("OMAP Hw_SpiSetUpDMA (%s): SG DMA: PHYS:0x%X VIRT:0x%X len:%d \n",
                  pDevice->CurrentTransferDirRx ? "RX":"TX",
                  pHWDevice->DMAMapList[0].dma_address,
                  (UINT32)pHWDevice->DMAMapList[0].address,
                  pHWDevice->DMAMapList[0].length));
    }
                     
    if (pDevice->CurrentTransferDirRx) {
        
        pHWDevice->DummyTxDmaActive = TRUE;
        
        if (pHWDevice->DMASg) {
            DBG_ASSERT(actualDMATransferBytes == pHWDevice->DMAMapList[0].length);
            DBG_ASSERT(pHWDevice->DMAMapList[0].dma_address != 0);
            SET_UP_OMAP_DMA(pHWDevice,
                            actualDMAWidth,
                            actualDMATransferBytes,
                            pHWDevice->DMAMapList[0].dma_address, 
                            DMA_RX_TYPE);
        } else {
            SET_UP_OMAP_DMA(pHWDevice,
                            actualDMAWidth,
                            actualDMATransferBytes,
                            pHWDevice->hDmaBuffer, 
                            DMA_RX_TYPE);        
        }
        /* start up the dummy TX DMA to to output FFs on the SPI bus 
          * this is also required to fix a bug in the SPI host controller
          * where it wants to output one more data phase. By turning on the
          * transmit logic, the number of data phases is controlled by the
          * TX count*/
        SET_UP_OMAP_DMA(pHWDevice,
                        actualDMAWidth,
                        actualDMATransferBytes, 
                        pHWDevice->hDmaDummyBuffer, 
                        DMA_TX_DUMMY_TYPE);
        
            /* we need to wait for 2 DMA callbacks */
        pHWDevice->DmaStopCount = 2;
        
    } else {
        
        pHWDevice->DummyTxDmaActive = FALSE;
        
        if (pHWDevice->DMASg) {
                /* we only allow 1 scatter entry in this driver */
            DBG_ASSERT(actualDMATransferBytes == pHWDevice->DMAMapList[0].length);
            DBG_ASSERT(pHWDevice->DMAMapList[0].dma_address != 0);
            SET_UP_OMAP_DMA(pHWDevice,
                            actualDMAWidth,
                            actualDMATransferBytes,
                            pHWDevice->DMAMapList[0].dma_address, 
                            DMA_TX_TYPE);
            
        } else {
            /* load up the common buffer */               
            if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_8) {            
                PUINT8  pData8; 
                PUINT16 pDma16; 
                int i;
                /* 8 bit mode, we use 16 bit DMA */
                pData8 = (PUINT8)pDevice->pCurrentBuffer;
                pDma16 = (PUINT16)pHWDevice->pDmaBuffer;
                for (i = 0 ; i < pDevice->CurrentTransferLength; i++,pDma16++,pData8++) {
                    *pDma16 = (UINT16)*pData8;
                }  
            } else {
                HcdCommonBufferCopy(pDevice->CurrentDmaWidth,
                                    pHWDevice->pDmaBuffer,
                                    pDevice->pCurrentBuffer,
                                    pDevice->CurrentTransferLength,
                                    pDevice->HostDMABufferCopyMode);
            }
            
            SET_UP_OMAP_DMA(pHWDevice,
                            actualDMAWidth,
                            actualDMATransferBytes,
                            pHWDevice->hDmaBuffer, 
                            DMA_TX_TYPE); 
        }
               
            /* we need to wait for 1 callback */
        pHWDevice->DmaStopCount = 1;
    }
 
    return SDIO_STATUS_PENDING;   
}

    /* set the clock rate for the SPI transactions */
void HW_SetClock(PSDHCD_DEVICE pDevice, PUINT32 pClockRate)
{
    INT i;
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    
    DBG_PRINT(SDDBG_TRACE, ("OMAP SPI HCD: SetClock, Desired Clock:%d Hz, BaseClock:%d Hz\n",
            *pClockRate,SPIFClockTable[0].ClockRate));

        /* find a clock rate that is close */
    for (i = 0; i < MAX_CLOCK_ENTRIES; i++) {
        if (*pClockRate >= SPIFClockTable[i].ClockRate) {
            DBG_PRINT(SDDBG_TRACE, ("OMAP SPI HCD: Found operational clock, index:%d, rate:%d hz \n",
                i, SPIFClockTable[i].ClockRate));
            break; 
        }
    }
    
        /* clock enable */
    pHWDevice->ChannelConfShadow &= ~OMAP_SPI_CHCON_CLKD_MASK;
        /* set divisor */
    if (i < MAX_CLOCK_ENTRIES) {
        pHWDevice->ChannelConfShadow |= OMAP_SPI_CHCON_CLKD(SPIFClockTable[i].PTVValue); 
        *pClockRate = SPIFClockTable[i].ClockRate;
    } else { 
            /* use highest divisor */
        pHWDevice->ChannelConfShadow |= OMAP_SPI_CHCON_CLKD(MAX_PTVVALUE); 
        *pClockRate = SPIFClockTable[MAX_CLOCK_ENTRIES-1].ClockRate;
    }
  
    DBG_PRINT(SDDBG_TRACE, ("OMAP SPI HCD: RAW CONF: 0x%X, OpClock:%d Hz\n",
            pHWDevice->ChannelConfShadow, *pClockRate)); 
}

    /* enable disable SPI (via GPIO) interrupt detection */
void HW_EnableDisableSPIIRQ(PSDHCD_DEVICE pDevice, BOOL Enable, BOOL FromIrq)
{
    unsigned long irqflags = 0;
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    
    DBG_PRINT(ATH_SPI_TRACE_INFO, ("OMAP SPI: SPIIRQ detect: %s\n",
              Enable ? "ON" : "OFF" ));
   
    if (!FromIrq) { 
        spin_lock_irqsave(&pHWDevice->Lock, irqflags);
    }
    
    do { 
        if (Enable) { 
            if (!pHWDevice->IrqEnabled) {
                pHWDevice->IrqEnabled = TRUE;
                omap_set_gpio_edge_ctrl(pHWDevice->SpiIntGPIOPin, OMAP_GPIO_LEVEL_LOW);
            }
        } else {  
            if (pHWDevice->IrqEnabled) {
                pHWDevice->IrqEnabled = FALSE;
                omap_set_gpio_edge_ctrl(pHWDevice->SpiIntGPIOPin, OMAP_GPIO_NO_EDGE);
            }
        }     
    } while (FALSE);
     
    if (!FromIrq) { 
        spin_unlock_irqrestore(&pHWDevice->Lock, irqflags);
    }
    
}

    /* start the DMA operation on the SPI host controller */
void HW_StartDMA(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    UINT32 temp;
    
    temp = pHWDevice->ChannelConfShadow & 
                ~(OMAP_SPI_CHCON_WORD_LENGTH_MASK | OMAP_SPI_CHCON_TRM_MASK |
                  OMAP_SPI_CHCON_CLK_DATA_EVEN); /* odd edge clocking unless we need to adjust below */

    if (pDevice->CurrentTransferDirRx) {
        temp |= OMAP_SPI_CHCON_TRM_RX_TX | OMAP_SPI_CHCON_DMA_READ | OMAP_SPI_CHCON_DMA_WRITE |
                rxclkmode;
    } else {
        temp |= OMAP_SPI_CHCON_TRM_TX_ONLY | OMAP_SPI_CHCON_DMA_WRITE |
                txclkmode;      
    }
    
    if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_16) {             
        temp |= OMAP_SPI_CHCON_WORD_LENGTH(16);
    } else if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_32) {                                       
        temp |= OMAP_SPI_CHCON_WORD_LENGTH(32);      
    } else if (pDevice->CurrentDmaWidth == ATH_TRANS_DS_8) {                   
        temp |= OMAP_SPI_CHCON_WORD_LENGTH(8);
    } else {
        DBG_ASSERT(FALSE); 
        return;
    }
    
        /* set the DMA timer if enabled */
    OMAP_SET_DMA_TIMER(pHWDevice, DMA_STALL_TIMEOUT);
    
        /* set configuration */
    WRITE_CHCONF_REG32(pHWDevice, temp);
    
}

/*
 * StopDMATransfer - stop DMA transfer
*/
void HW_StopDMATransfer(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    DBG_PRINT(SDDBG_TRACE, ("OMAP SPI  StopDMATransfer\n"));

    if (pHWDevice->DmaRxChan != -1) {  
        omap_stop_dma(pHWDevice->DmaRxChan);
    }
    
    if (pHWDevice->DmaTxChan != -1) {
        omap_stop_dma(pHWDevice->DmaTxChan);
    }
    
    OMAP_CANCEL_DMA_TIMER(pHWDevice);
}

/* deferred completion */
SDIO_STATUS HW_QueueDeferredCompletion(PSDHCD_DEVICE pDevice)
{
    QueueWork(GET_HW_DEVICE(pDevice),&GET_HW_DEVICE(pDevice)->iocomplete_work);  
    return SDIO_STATUS_SUCCESS;  
}

static INLINE SDIO_STATUS WaitLastOp(PSDHCD_HW_DEVICE pHWDevice)
{
    UINT32 temp = pHWDevice->PollWait;
    
      /* make sure last operation is done*/
    while (temp) {
        if (READ_CHSTAT_REG32(pHWDevice) & 
                    OMAP_SPIF_CHSTAT_RX_FULL) {
            break;             
        }
        temp--; 
    }
    
    if (0 == temp) { 
        DBG_PRINT(SDDBG_ERROR, ("OMAP SPI controller last op timeout: CHCONF:0x%X CHSTAT:0x%X\n",
                    READ_CHCONF_REG32(pHWDevice), READ_CHSTAT_REG32(pHWDevice))); 
        return SDIO_STATUS_ERROR;   
    }
        /* clear receiver */
    temp = READ_CHRX_REG32(pHWDevice);
    return SDIO_STATUS_SUCCESS;
} 
    /* SPI token input output */   
SDIO_STATUS HW_InOut_Token(PSDHCD_DEVICE pDevice,
                           UINT32        OutToken,
                           UINT8         DataSize,
                           PUINT32       pInToken) 
{   
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    UINT32 temp = pHWDevice->PollWait;
    UINT32 controlVal;
    UINT32 mask; 
    SDIO_STATUS status = SDIO_STATUS_SUCCESS;
   
    do {
                 
        if (pHWDevice->WaitTxDone) {
            pHWDevice->WaitTxDone = FALSE;
            status = WaitLastOp(pHWDevice);            
            if (!SDIO_SUCCESS(status)) {
                break;    
            }
        }
        
        controlVal = pHWDevice->ChannelConfShadow & 
                        ~(OMAP_SPI_CHCON_WORD_LENGTH_MASK | OMAP_SPI_CHCON_TRM_MASK);
         
        controlVal |= OMAP_SPI_CHCON_TRM_RX_TX;
        
        if (pInToken != NULL) {
                /* host is interested in the read data */    
            controlVal |= rxclkmode;
        } else {
            controlVal |= txclkmode;    
        }
        
        if (DataSize == ATH_TRANS_DS_16) {             
            controlVal |= OMAP_SPI_CHCON_WORD_LENGTH(16);
            mask = 0xFFFF;
        } else if (DataSize == ATH_TRANS_DS_32) {                                       
            controlVal |= OMAP_SPI_CHCON_WORD_LENGTH(32);
            mask = 0xFFFFFFFF;
        } else if (DataSize == ATH_TRANS_DS_8) {                   
            controlVal |= OMAP_SPI_CHCON_WORD_LENGTH(8);
            mask = 0xFF;
        } else if (DataSize == ATH_TRANS_DS_24) { 
            controlVal |= OMAP_SPI_CHCON_WORD_LENGTH(24);
            mask = 0xFFFFFF;
        } else {
            DBG_ASSERT(FALSE); 
            status = SDIO_STATUS_INVALID_PARAMETER;
            break;  
        }
         
        DBG_PRINT(ATH_SPI_TRACE_INFO, ("OMAP SPI InOut_Token : DS:%d OUT:0x%X\n", 
                DataSize, (OutToken & mask)));
        
        {
                /* the following code fixes a problem that was observed after a DMA operation,
                 * the RX FULL bit becomes stuck and causes the driver to believe that the
                 * last transmit/receive completed before it actually was, an additional dummy
                 * read appears to fix the problem */
            INT count = 0;
            
            while (count < 1000) {
                if (READ_CHSTAT_REG32(pHWDevice) & 
                            OMAP_SPIF_CHSTAT_RX_FULL) {
                    temp = READ_CHRX_REG32(pHWDevice);
                    count++;
                    //DBG_PRINT(SDDBG_TRACE, ("OMAP c:%d\n",count));          
                } else  {
                    break;    
                }
            }
            
            if (count >= 1000) {
                DBG_ASSERT(FALSE);    
            }
        }
        
        WRITE_CHCONF_REG32(pHWDevice, controlVal);   
            /* write token */
        WRITE_CHTX_REG32(pHWDevice, (UINT32)OutToken);

#ifdef ATH_WRITE_PIPELINED 
        if (NULL == pInToken) {
                /*optimization to return immediately */
            pHWDevice->WaitTxDone = TRUE;
            break;   
        }
#endif  

        temp = pHWDevice->PollWait;
        
            /* wait for rx-full */
        while (temp) {
            if (READ_CHSTAT_REG32(pHWDevice) & 
                        OMAP_SPIF_CHSTAT_RX_FULL) {
                break;             
            }
            temp--; 
        }
        
        if (0 == temp) {
            DumpRegisters(pHWDevice);
            DBG_PRINT(SDDBG_ERROR, ("OMAP SPI controller timeout: CHCONF:0x%X CHSTAT:0x%X\n",
                        READ_CHCONF_REG32(pHWDevice), READ_CHSTAT_REG32(pHWDevice))); 
            status = SDIO_STATUS_ERROR;
            break;   
        }
        
            /* read the RX data */ 
        temp = READ_CHRX_REG32(pHWDevice);
                     
        if (pInToken != NULL) {
            DBG_PRINT(ATH_SPI_TRACE_INFO, ("OMAP SPI InOut_Token : DS:%d IN: 0x%X\n", 
                DataSize, temp & mask));
                /* caller wants the data */
            *pInToken = temp & mask; 
        }      
        
    } while (FALSE);
    
    return status;                                                                        
}

    /* debugging from common layer */
void HW_SetDebugSignal(PSDHCD_DEVICE pDevice, INT PinNo, BOOL ON)
{
    if (gpiodebug) {
        switch (PinNo) {
            case 1:
                if (ON) {
                    omap_set_gpio_dataout(SPI_DBG1_PIN,TRUE);
                } else {
                    omap_set_gpio_dataout(SPI_DBG1_PIN,FALSE);
                }
                break;
            default:
                break;    
        }
    }
}

    /* debugging from the common layer, for timming analysis */
void HW_ToggleDebugSignal(PSDHCD_DEVICE pDevice, int PinNo)
{   
    if (gpiodebug) {
        switch (PinNo) {
            case 1:
                omap_set_gpio_dataout(SPI_DBG1_PIN,TRUE);
                omap_set_gpio_dataout(SPI_DBG1_PIN,FALSE);
                break;
            default:
                break;    
        }
    }
}

/* SDIO interrupt request */
static void hcd_spi_irq(int irq, void *context, struct pt_regs * r)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)context;
    
    DBG_PRINT(ATH_SPI_TRACE_INFO, ("+OMAP SPI IRQ \n"));
        /* disable IRQ while we process it */
    HW_EnableDisableSPIIRQ(pHWDevice->pDevice, FALSE, HW_FROM_ISR_CONTEXT);
        /* startup work item to process IRQs */
    QueueWork(pHWDevice,&pHWDevice->procirq_work);
    DBG_PRINT(ATH_SPI_TRACE_INFO, ("-OMAP SPI IRQ \n"));
}


#ifdef CHECK_DMA_TIMEOUT

static void DMATimeout(unsigned long Context)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)Context;
    
    pHWDevice->DMATimerQueued = FALSE;    
    if (!pHWDevice->DMATimerCancelled) {
        DBG_PRINT(SDDBG_ERROR, ("**** OMAP DMA TIMEOUT **** \n"));
        DumpRegisters(pHWDevice);
        DumpDMASettings(pHWDevice, pHWDevice->pDevice->CurrentTransferDirRx ? FALSE : TRUE);  
        pHWDevice->DmaStopCount = 1;
        SyncStopDMA(pHWDevice);
        pHWDevice->DMACompleteStatus = SDIO_STATUS_ERROR;
        QueueWork(pHWDevice, &pHWDevice->dmacomplete_work);
    }
    
}

static void CancelDMATimer(PSDHCD_HW_DEVICE pHWDevice) 
{
    if (pHWDevice->DMATimerQueued) {
        pHWDevice->DMATimerQueued = FALSE;   
        pHWDevice->DMATimerCancelled = TRUE;  
        del_timer(&pHWDevice->DMATimer);
    }
}

static void SetDMATimer(PSDHCD_HW_DEVICE pHWDevice, UINT32 TimeOut)
{
    UINT32 delta;
    
    if (!pHWDevice->DMATimerQueued) {
            /* convert timeout to ticks */
        delta = (TimeOut * HZ)/1000;
        if (delta == 0) {
            delta = 1;  
        }
        pHWDevice->DMATimer.expires = jiffies + delta;
        pHWDevice->DMATimerQueued = TRUE;
        pHWDevice->DMATimerCancelled = FALSE;
        add_timer(&pHWDevice->DMATimer);
    } else {
        DBG_ASSERT(FALSE);    
    }
}
#endif

static void TimerTimeout(unsigned long Context)
{
    PSDHCD_HW_DEVICE pHWDevice = (PSDHCD_HW_DEVICE)Context;
    
    pHWDevice->TimerQueued = FALSE;    
    if (!pHWDevice->TimerCancelled) {
        DBG_PRINT(SDDBG_ERROR, ("**** Timer TIMEOUT (%d)  **** \n",pHWDevice->TimerCallbackContext));
        HcdTimerCallback(pHWDevice->pDevice,pHWDevice->TimerCallbackContext);
    }    
}

void HW_StartTimer(PSDHCD_DEVICE pDevice, int TimeoutMS, int Context)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    UINT32 delta;
    
    if (!pHWDevice->TimerQueued) {
            /* convert timeout to ticks */
        delta = (TimeoutMS * HZ)/1000;
        if (delta == 0) {
            delta = 1;  
        }
        pHWDevice->Timer.expires = jiffies + delta;
        pHWDevice->TimerQueued = TRUE;
        pHWDevice->TimerCancelled = FALSE;
        pHWDevice->TimerCallbackContext = Context;
        add_timer(&pHWDevice->Timer);
    } else {
        DBG_ASSERT(FALSE);    
    }
}
    
void HW_StopTimer(PSDHCD_DEVICE pDevice)
{
    PSDHCD_HW_DEVICE pHWDevice = GET_HW_DEVICE(pDevice);
    unsigned long irqflags = 0;
    
    spin_lock_irqsave(&pHWDevice->Lock, irqflags);
    
    if (pHWDevice->TimerQueued) {
        pHWDevice->TimerQueued = FALSE;   
        pHWDevice->TimerCancelled = TRUE;  
        spin_unlock_irqrestore(&pHWDevice->Lock, irqflags);
        del_timer(&pHWDevice->Timer);
    } else {
        spin_unlock_irqrestore(&pHWDevice->Lock, irqflags);    
    }
    
}

void HW_UsecDelay(PSDHCD_DEVICE pDevice, UINT32 uSeconds)
{
    udelay(uSeconds);   
}

void HW_PowerUpDown(PVOID pHWDevice, BOOL powerUp)
{
    /* nothing to do for OMAP reference board */    
}

/*
 * module init
*/
static int __init ath_spi_init(void) {
    SDIO_STATUS status;
    INT i;
    
    REL_PRINT(SDDBG_TRACE, ("+OMAP SPI HCD: loaded\n"));

    SDLIST_INIT(&HcdContext.DeviceList);
    status = SemaphoreInitialize(&HcdContext.DeviceListSem, 1);
    if (!SDIO_SUCCESS(status)) {
       return SDIOErrorToOSError(status);
    }     
        /* set up clock rate table */
    DBG_PRINT(SDDBG_TRACE, ("Setting up RAW SPI clock table, base_clock:%d \n",
        base_clock));
    for (i = 0; i < MAX_CLOCK_ENTRIES; i++) {
        SPIFClockTable[i].ClockRate = base_clock >> SPIFClockTable[i].PTVValue;
        DBG_PRINT(SDDBG_TRACE, ("  Index: %d, Clock:%d Hz, PTV:%d \n",
          i,SPIFClockTable[i].ClockRate,SPIFClockTable[i].PTVValue));
 
    }
      
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    status = SDIO_BusAddOSDevice(&HcdContext.Driver.Dma, &HcdContext.Driver.HcdDriver, &HcdContext.Driver.HcdDevice);
    return SDIOErrorToOSError(status);
#else
    /* 2.4 */
    return Probe(NULL, NULL);
#endif
}

/*
 * module cleanup
*/
static void __exit ath_spi_cleanup(void) {
    REL_PRINT(SDDBG_TRACE, ("+OMAP SPI HCD: unloading..\n"));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    SDIO_BusRemoveOSDevice(&HcdContext.Driver.HcdDriver, &HcdContext.Driver.HcdDevice);
#else 
    /* 2.4 */
    Remove(NULL); 
#endif
    DBG_PRINT(SDDBG_TRACE, ("-OMAP SPI HCD: leave ath_spi_cleanup\n"));
}

MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

module_init(ath_spi_init);
module_exit(ath_spi_cleanup);

