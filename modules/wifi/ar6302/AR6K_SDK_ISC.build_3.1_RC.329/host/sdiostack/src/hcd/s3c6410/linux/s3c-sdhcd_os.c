/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_hcd_os.c

@abstract: S3C6410 SDIO Host Controller Driver

#notes: includes module load and unload functions
 
@notice: Copyright (c), 2009 Atheros Communications, Inc.


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
// Portions of this code were developed with information supplied from the 
// SD Card Association Simplified Specifications. The following conditions and disclaimers may apply:
//
//  The following conditions apply to the release of the SD simplified specification (“Simplified
//  Specification”) by the SD Card Association. The Simplified Specification is a subset of the complete 
//  SD Specification which is owned by the SD Card Association. This Simplified Specification is provided 
//  on a non-confidential basis subject to the disclaimers below. Any implementation of the Simplified 
//  Specification may require a license from the SD Card Association or other third parties.
//  Disclaimers:
//  The information contained in the Simplified Specification is presented only as a standard 
//  specification for SD Cards and SD Host/Ancillary products and is provided "AS-IS" without any 
//  representations or warranties of any kind. No responsibility is assumed by the SD Card Association for 
//  any damages, any infringements of patents or other right of the SD Card Association or any third 
//  parties, which may result from its use. No license is granted by implication, estoppel or otherwise 
//  under any patent or other rights of the SD Card Association or any third party. Nothing herein shall 
//  be construed as an obligation by the SD Card Association to disclose or distribute any technical 
//  information, know-how or other confidential information to any third party.
//
//
// The initial developers of the original code are Seung Yi and Paul Lever
//
// sdio@atheros.com
//
//
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/proc_fs.h>

#include <plat/map-base.h>
#include <plat/regs-sdhci.h>
#include <plat/sdhci.h>
#include <plat/regs-clock.h>
#include "../../../include/ctsystem.h"
#include "../../../include/sdio_busdriver.h"
#include "../../stdhost/linux/sdio_std_hcd_linux.h"
#include "../../stdhost/sdio_std_hcd.h"
#include "../../stdhost/linux/sdio_std_hcd_linux_lib.h"

#define DESCRIPTION "SDIO S3C6410 HCD"
#define AUTHOR "Atheros Communications, Inc."

static INT ForceSDMA = 0;
module_param(ForceSDMA, int, 0444);
MODULE_PARM_DESC(ForceSDMA, "Force Host controller to use simple DMA if available");

static INT NoDMA = 0;
module_param(NoDMA, int, 0444);
MODULE_PARM_DESC(NoDMA, "Force Host controller to use PIO mode");

#define SDIO_HCD_MAPPED            0x01

#define S3C6410_SDIO_IRQ_SET 0x01

static irqreturn_t hcd_sdio_irq(int irq, void *context
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
, struct pt_regs * r);
#else
);
#ifndef SA_SHIRQ
#define SA_SHIRQ           IRQF_SHARED
#endif

#endif /* LINUX_VERSION_CODE */

#define S3C_CONTROL_REG2                 0x80
#define S3C_CONTROL_BASECLK_SELECT_MASK  (0x3 << 4)
#define S3C_CONTROL_BASECLK_SHIFT        4


    /* Advanced DMA description */
SDDMA_DESCRIPTION HcdADMADefaults = {
    .Flags = SDDMA_DESCRIPTION_FLAG_SGDMA,
    .MaxDescriptors = SDHCD_MAX_ADMA_DESCRIPTOR,
    .MaxBytesPerDescriptor = SDHCD_MAX_ADMA_LENGTH,
    .Mask = SDHCD_ADMA_ADDRESS_MASK, 
    .AddressAlignment = SDHCD_ADMA_ALIGNMENT,
    .LengthAlignment = SDHCD_ADMA_LENGTH_ALIGNMENT,
};

    /* simple DMA descriptions */
SDDMA_DESCRIPTION HcdSDMADefaults = {
    .Flags = SDDMA_DESCRIPTION_FLAG_DMA,
    .MaxDescriptors = 1,
    .MaxBytesPerDescriptor = SDHCD_MAX_SDMA_LENGTH,
    .Mask = SDHCD_SDMA_ADDRESS_MASK, 
    .AddressAlignment = SDHCD_SDMA_ALIGNMENT,
    .LengthAlignment = SDHCD_SDMA_LENGTH_ALIGNMENT,
};

/*
 * MapAddress - sets up the address for a given device
*/
static int MapAddress(struct platform_device *pdev, char *pName, PSDHCD_MEMORY pAddress)
{
    struct resource         *mem;
    
    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    
    if (!mem ) {
        printk(KERN_ERR "SDIO S3C6410 HCD: MapAddress, Failed to get io memory region resouce.\n");
        return -ENOENT;
    } 
    pAddress->Raw = mem->start;
    pAddress->Length = mem->end - mem->start + 1;
    
    if (!request_mem_region (pAddress->Raw, pAddress->Length, pName)) {
        printk(KERN_ERR "SDIO S3C6410 HCD: MapAddress - memory in use: 0x%X(0x%X)\n",
                               (UINT)pAddress->Raw, (UINT)pAddress->Length);
        return -EBUSY;
    }
    pAddress->pMapped = ioremap_nocache(pAddress->Raw, pAddress->Length);
    if (pAddress->pMapped == NULL) {
        printk(KERN_ERR "SDIO S3C6410 HCD: MapAddress - unable to map memory\n");
        /* cleanup region */
        release_mem_region (pAddress->Raw, pAddress->Length);
        return -EFAULT;
    }

    printk(KERN_ERR "SDIO S3C6410 HCD: MapAddress - mapped memory: 0x%X(0x%X) to 0x%X\n", 
                            (UINT)pAddress->Raw, (UINT)pAddress->Length, (UINT)pAddress->pMapped);

    //?? _WRITE_DWORD_REG(pAddress->pMapped + 0x84, plat_data->ctrl3[0]); /* Up 2 25MHz only! */
    //?? hsmmc_set_gpio(plat_data->hwport, 4 /*plat_data->bus_width*/);
    return 0;
}


/*
 * UnmapAddress - unmaps the address 
*/
static void UnmapAddress(PSDHCD_MEMORY pAddress) {
    iounmap(pAddress->pMapped);
    release_mem_region(pAddress->Raw, pAddress->Length);
    pAddress->pMapped = NULL;
}


struct clk *g_clk = NULL;
struct clk *g_bestclk = NULL;
int    g_bestclk_index = 0;

static int configure_clocks(struct platform_device *pdev, PSDHCD_INSTANCE  pHcInstance)
{
    int            retVal = 1;
    struct device *dev = &pdev->dev;
    struct clk     *clk_io;
    struct s3c_sdhci_platdata *pPlatData = pdev->dev.platform_data;
    int            i;
    
    for (i = 0; i < 4; i++) {
        char *clockName = pPlatData->clocks[i];
        
        if (clockName == NULL) {
            continue;    
        }
        
        clk_io = clk_get(dev,clockName);
        if (IS_ERR(clk_io)) {
            printk(KERN_ERR"Failed to clock : %s, index=%d \n", clockName, i);
            continue;    
        }
        
        printk(KERN_ERR"Found clock : %s, index=%d, rate:%d \n", clockName, i, (int)clk_get_rate(clk_io));
        
    }
    
    do {
        
        g_clk = clk_get(dev, "hsmmc");
  
        if (IS_ERR(g_clk)) {
            printk(KERN_ERR"Failed to get clock for controller\n");
            break;
        }
        
        pHcInstance->BaseClock = clk_get_rate(g_clk);
        clk_enable(g_clk);
                         
#if 1        
        g_bestclk = clk_get(dev, "mmc_bus");        
        if (IS_ERR(g_bestclk)) {
            printk(KERN_ERR"Failed to get best clock for controller\n");
            break;
        }
        g_bestclk_index = 2;   //?? hardcoded...             
        clk_enable(g_bestclk);
#endif
                      
        retVal = 0;
        
    } while (FALSE);  

    return retVal;
}

/* perform additional controller tunning on the I/O pads */
static void special_setup(PSDHCD_INSTANCE  pHcInstance)
{
    UINT32 ctrl2;

    WRITE_HOST_REG32(pHcInstance, S3C64XX_SDHCI_CONTROL4, S3C64XX_SDHCI_CONTROL4_DRIVE_9mA);
    
    ctrl2 = READ_HOST_REG32(pHcInstance, S3C_SDHCI_CONTROL2);
    ctrl2 &= S3C_SDHCI_CTRL2_SELBASECLK_MASK;
    ctrl2 |= (S3C64XX_SDHCI_CTRL2_ENSTAASYNCCLR |
              S3C64XX_SDHCI_CTRL2_ENCMDCNFMSK |
              S3C_SDHCI_CTRL2_ENFBCLKRX |
              S3C_SDHCI_CTRL2_DFCNT_NONE |
              S3C_SDHCI_CTRL2_ENCLKOUTHOLD);
   
    WRITE_HOST_REG32(pHcInstance,S3C_SDHCI_CONTROL2,ctrl2);
    WRITE_HOST_REG32(pHcInstance,S3C_SDHCI_CONTROL3,
             S3C_SDHCI_CTRL3_FCSEL3 |
             S3C_SDHCI_CTRL3_FCSEL2 |
             S3C_SDHCI_CTRL3_FCSEL1 |
             S3C_SDHCI_CTRL3_FCSEL0);
    
}

/* switch to the best clock (which is actually 24 Mhz), the controller has to be initialized
 * with the default clock @ 133Mhz, however this can only result in 16 and 33 mhz clock rates on
 * the SDIO bus, a secondary clock (EPLL) provides a real 24 Mhz clock which can provide a true
 * 24 Mhz SDIO bus clock
 */
static void switch_to_best_clock(PSDHCD_INSTANCE  pHcInstance)
{
    UINT32 regVal;
    
    if (g_bestclk != NULL) {
        regVal = READ_HOST_REG32(pHcInstance, S3C_CONTROL_REG2);
        regVal &= ~S3C_CONTROL_BASECLK_SELECT_MASK;
        regVal |= (g_bestclk_index << S3C_CONTROL_BASECLK_SHIFT) & S3C_CONTROL_BASECLK_SELECT_MASK;
        WRITE_HOST_REG32(pHcInstance, S3C_CONTROL_REG2,regVal);
        pHcInstance->BaseClock = clk_get_rate(g_bestclk);
        printk(KERN_ERR"S3C Clock Control Set :0x%X (base:%d) \n", 
                  READ_HOST_REG32(pHcInstance,S3C_CONTROL_REG2),pHcInstance->BaseClock);
    }
    
    printk(KERN_ERR"S3C_SCLK_GATE : 0x%X \n",readl(S3C_SCLK_GATE));
    printk(KERN_ERR"S3C_HCLK_GATE : 0x%X \n",readl(S3C_HCLK_GATE));
    printk(KERN_ERR"S3C_PWR_CFG : 0x%X \n",readl(S3C_PWR_CFG));
    printk(KERN_ERR"S3C_NORMAL_CFG : 0x%X \n",readl(S3C_NORMAL_CFG));
    
    regVal = readl(S3C_SCLK_GATE);
    regVal |= S3C_CLKCON_SCLK_MMC1 | S3C_CLKCON_SCLK_MMC0 | S3C_CLKCON_SCLK_MMC1_48 | S3C_CLKCON_SCLK_MMC0_48;
    writel(regVal,S3C_SCLK_GATE);
    
    printk(KERN_ERR"New S3C_SCLK_GATE : 0x%X \n",readl(S3C_SCLK_GATE));
}

static void release_clocks(struct platform_device *pdev)
{
    if (g_clk != NULL) {
        clk_disable(g_clk);
        clk_put(g_clk);   
    }    
    
    if (g_bestclk != NULL) {
        clk_disable(g_bestclk);
        clk_put(g_bestclk);    
    }
    
    g_bestclk = NULL;
    g_clk = NULL;
}


/*
 * cleanup_resources - cleanup resources
*/
static void cleanup_resources(struct platform_device *pdev, 
                                PSDHCD_INSTANCE pHcInstance)
{    
    if (pHcInstance->OsSpecific.InitMask & SDIO_HCD_MAPPED) {
        UnmapAddress(&pHcInstance->OsSpecific.Address);
        pHcInstance->OsSpecific.InitMask &= ~SDIO_HCD_MAPPED;
    }
    
    release_clocks(pdev);
}

SDIO_STATUS SetUpOneSlotController(PSDHCD_CORE_CONTEXT pStdCore,
                                   struct  platform_device *pdev,
                                   UINT       SlotNumber,
                                   BOOL       AllowDMA)
{
    SDIO_STATUS              status = SDIO_STATUS_ERROR;
    TEXT                     nameBuffer[SDHCD_MAX_DEVICE_NAME];
    PSDHCD_INSTANCE          pHcInstance = NULL;
    UINT                     startFlags = 0;
    struct s3c_sdhci_platdata *pPlatData = pdev->dev.platform_data;
    
    do {   
        
        if (pPlatData->cfg_gpio) {
            pPlatData->cfg_gpio(pdev,4);     
        }
                
            /* setup the name */
        snprintf(nameBuffer, SDHCD_MAX_DEVICE_NAME, "s3c6410_sdio:%i",
                 SlotNumber);
         
            /* create the instance */        
        pHcInstance = CreateStdHcdInstance(&pdev->dev, 
                                           SlotNumber, 
                                           nameBuffer);
      
        if (NULL == pHcInstance) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;     
        }

        status = MapAddress(pdev, 
                            pHcInstance->Hcd.pName, 
                            &pHcInstance->OsSpecific.Address);
                            
        if (!SDIO_SUCCESS(status)) {
            printk(KERN_ERR 
               "SDIO S3C6410 HCD: Probe - failed to map device memory address %s 0x%X, status %d\n",
                pHcInstance->Hcd.pName, (unsigned int)pHcInstance->OsSpecific.Address.Raw, status); 
            break;                  
        }
        pHcInstance->OsSpecific.InitMask |= SDIO_HCD_MAPPED;  
        pHcInstance->pRegs = pHcInstance->OsSpecific.Address.pMapped;      
        pHcInstance->FixedMaxSlotCurrent = 1000;
        pHcInstance->NonStdBehaviorFlags |= NON_STD_WAIT_CMD_DONE;

        if (configure_clocks(pdev,pHcInstance) != 0) {
            status = SDIO_STATUS_NO_RESOURCES;
            break;    
        }
                        
        if (!AllowDMA) {
            startFlags |= START_HCD_FLAGS_FORCE_NO_DMA;
        }
        
        if (ForceSDMA) {
            startFlags |= START_HCD_FLAGS_FORCE_SDMA;
        }
        
        startFlags |= START_HCD_FLAGS_ALLOW_CBDMA;
        pHcInstance->CommonBufferLength = 32768;
        
        /* reset controller */
        WRITE_HOST_REG8(pHcInstance, HOST_REG_SW_RESET, HOST_REG_SW_RESET_ALL );
        mmiowb();
        printk(KERN_ERR "SDIO S3C6410 HCD:  Reset HCD\n");
        while(  READ_HOST_REG8(pHcInstance, HOST_REG_SW_RESET) & HOST_REG_SW_RESET_ALL )
          mdelay(1);
                
        WRITE_HOST_REG32(pHcInstance, HOST_REG_INT_SIGNAL_ENABLE, 0x0);
        WRITE_HOST_REG8(pHcInstance, HOST_REG_TIMEOUT_CONTROL, 0x0E);
                   
        {
            unsigned int i;
            
            i = READ_HOST_REG32(pHcInstance, HOST_REG_PRESENT_STATE);
            
            printk( KERN_ERR "State is %x, Card is %s present\n", i,
                    ( i & 0x40000 ) ? "" : "NOT" );
        }
          
        
            /* startup this instance */
        status = AddStdHcdInstance(pStdCore,
                                   pHcInstance,
                                   startFlags,
                                   NULL,
                                   &HcdSDMADefaults,
                                   &HcdADMADefaults); 
                                   
         if (!SDIO_SUCCESS(status)) {
            break;   
         }
        
         switch_to_best_clock(pHcInstance);
         special_setup(pHcInstance);
         
    } while (FALSE);     
    
    if (!SDIO_SUCCESS(status)) {
        if (pHcInstance != NULL) {
            cleanup_resources(pdev,pHcInstance);
            DeleteStdHcdInstance(pHcInstance);
        }    
    } else {
        printk(KERN_ERR "SDIO S3C6410 Probe - HCD:0x%x @ 0x%x ready! \n",(UINT)pHcInstance, (UINT)pHcInstance->pRegs);
    }  
    
    return status;
}


static void CleanUpHcdCore(struct platform_device *pdev, PSDHCD_CORE_CONTEXT pStdCore)
{   
    PSDHCD_INSTANCE pHcInstance;
    int             irq;
    
    printk(KERN_ERR "+ SDIO S3C6410 HCD: CleanUpHcdCore\n");
        /* make sure interrupts are disabled */
    if (pStdCore->CoreReserved1 & S3C6410_SDIO_IRQ_SET) {
        pStdCore->CoreReserved1 &= ~S3C6410_SDIO_IRQ_SET; 
        irq = platform_get_irq(pdev, 0);
        free_irq(irq, pStdCore);
    }
    
        /* remove all hcd instances associated with this device  */
    while (1) {
        pHcInstance = RemoveStdHcdInstance(pStdCore);
        if (NULL == pHcInstance) {
                /* no more instances */
            break;    
        }
        printk(KERN_ERR " SDIO S3C6410 HCD: Remove - removed HC Instance:0x%X, HCD:0x%X\n",
            (UINT)pHcInstance, (UINT)&pHcInstance->Hcd);
            /* hcd is now removed, we can clean it up */            
        cleanup_resources(pdev,pHcInstance); 
        DeleteStdHcdInstance(pHcInstance);    
    }
    
    DeleteStdHostCore(pStdCore);     
    printk(KERN_ERR "- SDIO S3C6410 HCD: CleanUpHcdCore\n");
}

/*
 * Probe - probe to setup our device, if present
*/
static int s3c6410_setuphcd(struct platform_device *pdev)
{
    int status = SDIO_STATUS_SUCCESS;
    int ii = 0;
    int irq;
    PSDHCD_CORE_CONTEXT pStdCore = NULL;
    SYSTEM_STATUS err = 0;
    
    printk(KERN_ERR "SDIO S3C6410 HCD: Probe - probing for new device (NoDMA=%d) \n",NoDMA);
    
    if (NoDMA) {
         printk(KERN_ERR "SDIO S3C6410 HCD: WARNING!!!! 6410 has read FIFO overrun bugs (mult-block) in PIO Mode!! \n");   
    }
    
    do {
        
        pStdCore = CreateStdHostCore(pdev);
        
        if (NULL == pStdCore) {
            err = -ENOMEM; 
            break;  
        }        
            
        status = SetUpOneSlotController(pStdCore,
                                        pdev,          /* device instance */
                                        ii,            /* std host slot number */
                                        !NoDMA         /* enabled DMA */
                                        );       
        if (!SDIO_SUCCESS(status)) {
            err= -ENODEV;
            break;    
        }
                
        irq = platform_get_irq(pdev, 0);
                
            /* enable the single controller interrupt 
               Interrupts can be called from this point on */
        err = request_irq(irq, hcd_sdio_irq, SA_SHIRQ,
                          "s3c6410hcd", pStdCore);
                          
        if (err < 0) {
            printk(KERN_ERR "SDIO S3C6410 - probe, unable to map interrupt \n");
            break;
        } 
        
        pStdCore->CoreReserved1 |= S3C6410_SDIO_IRQ_SET; 
        
            /* startup the hosts..., this will enable interrupts for card detect */
        status = StartStdHostCore(pStdCore);
        
        if (!SDIO_SUCCESS(status)) {
            printk( KERN_ERR "SDIO S3C6410 HCD: Unable to start core\n");
            err = -ENODEV;  
            break;
        }
               
    } while (FALSE);
    
    if (err < 0) {
        if (pStdCore != NULL) {
            CleanUpHcdCore(pdev,pStdCore);    
        }
    }
    printk(KERN_ERR "SDIO S3C6410 HCD: Exit probe\n");
    return err;  
}

/* Remove - remove  device
 * perform the undo of the Probe
*/
static int s3c6410_cleanuphcd(struct platform_device *pdev) 
{
    PSDHCD_CORE_CONTEXT  pStdCore;
    
    printk(KERN_ERR "+SDIO S3C6410 HCD: Remove - removing device\n");

    pStdCore = GetStdHostCore(pdev);
    
    if (NULL == pStdCore) {
        DBG_ASSERT(FALSE);
        return -1;    
    }

    CleanUpHcdCore(pdev, pStdCore);
    
    printk(KERN_ERR "-SDIO S3C6410 HCD: Remove\n");
    return 0;
}

/* SDIO interrupt request */
static irqreturn_t hcd_sdio_irq(int irq, void *context
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
, struct pt_regs * r
#endif
)
{
    irqreturn_t retStat;
     
        /* call shared handling ISR in case this is a mult-slot controller using 1 IRQ.
         * if this was not a mult-slot controller or each controller has it's own system
         * interrupt, we could call HcdSDInterrupt((PSDHCD_INSTANCE)context)) instead */
    //if (HcdSDInterrupt((PSDHCD_INSTANCE)context)) {
    if (HandleSharedStdHostInterrupt((PSDHCD_CORE_CONTEXT)context)) {
        retStat = IRQ_HANDLED;
    } else {
        retStat = IRQ_NONE;
    }    
    
    return retStat;
}

static int __devinit sdhci_s3c_probe(struct platform_device *pDev)
{
    struct s3c_sdhci_platdata *pdata = pDev->dev.platform_data;
    
    if (!pdata) {
        printk(KERN_ERR"no device data specified\n");
        return -ENOENT;
    }

    return s3c6410_setuphcd(pDev);
}


static int __devexit sdhci_s3c_remove(struct platform_device *pdev)
{
    s3c6410_cleanuphcd(pdev);
    return 0;
}

static struct platform_driver s3c6410_sdio_driver = {
    .probe      = sdhci_s3c_probe,
    .remove     = __devexit_p(sdhci_s3c_remove),
    .driver     = {
        .owner  = THIS_MODULE,
        .name   = "s3c-sdhci",
    },
};


/*
 * module init
*/
static int __init sdio_s3c6410_hcd_init(void) {
    SYSTEM_STATUS err;
    
    printk(KERN_ERR "+SDIO S3C6410 HCD: loading....\n");
    InitStdHostLib();
    
    /* register platform driver */
    err = platform_driver_register(&s3c6410_sdio_driver);
    if (err < 0) {
        printk(KERN_ERR "SDIO S3C6410 HCD: failed to register platform driver, %d\n",
                                err);
    }
    printk(KERN_ERR "-SDIO S3C6410 HCD \n");
    return err;
}

/*
 * module cleanup
*/
static void __exit sdio_s3c6410_hcd_cleanup(void) {
    printk(KERN_ERR "+SDIO S3C6410 HCD: unload\n");
    platform_driver_unregister(&s3c6410_sdio_driver);
    DeinitStdHostLib();
    printk(KERN_ERR "-SDIO S3C6410 HCD: leave sdio_s3c6410_hcd_cleanup\n");
}



MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_AUTHOR(AUTHOR);

module_init(sdio_s3c6410_hcd_init);
module_exit(sdio_s3c6410_hcd_cleanup);
