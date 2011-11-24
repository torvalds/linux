// Copyright (c) 2004 Atheros Communications Inc.
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

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_pciellen_hcd.h

@abstract: include file for Tokyo Electron PCI Ellen host controller, OS independent code
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef __SDIO_PCIELLEN_HCD_H___
#define __SDIO_PCIELLEN_HCD_H___

#include "../../include/ctsystem.h"

#include "../../include/sdio_busdriver.h"
#include "../../include/sdio_lib.h"
#ifdef VXWORKS
/* Wind River VxWorks support */
#include "vxworks/sdio_hcd_vxworks.h"
#endif /* VXWORKS */

/* QNX Neutrino suppot */
#ifdef QNX
#include "nto/sdio_hcd_nto.h"
#endif /* QNX */

#if defined(LINUX) || defined(__linux__)
#include "linux/sdio_hcd_linux.h"
#endif /* LINUX */

enum PXA_TRACE_ENUM {
    PXA_TRACE_CARD_INSERT = (SDDBG_TRACE + 1),
    PXA_TRACE_DATA = (SDDBG_TRACE + 2),       
    PXA_TRACE_REQUESTS,   
    PXA_TRACE_CONFIG,     
    PXA_TRACE_MMC_INT,    
    PXA_TRACE_CLOCK,
    PXA_TRACE_SDIO_INT,
    PXA_TRACE_LAST
};

#define HOST_REG_BLOCK_SIZE                         0x04

#define HOST_REG_BLOCK_COUNT                        0x06

#define HOST_REG_ARGUMENT                           0x08

#define HOST_REG_TRANSFER_MODE                      0x0C
#define HOST_REG_TRANSFER_MODE_MULTI_BLOCK          (1 << 5)
#define HOST_REG_TRANSFER_MODE_READ                 (1 << 4)
#define HOST_REG_TRANSFER_MODE_AUTOCMD12            (1 << 2)
#define HOST_REG_TRANSFER_MODE_BLOCKCOUNT_ENABLE    (1 << 1)
#define HOST_REG_TRANSFER_MODE_DMA_ENABLE           (1 << 0)

#define HOST_REG_COMMAND_REGISTER                   0x0E
#define HOST_REG_COMMAND_REGISTER_CMD_SHIFT         8
#define HOST_REG_COMMAND_REGISTER_DATA_PRESENT      (1 << 5)
#define HOST_REG_COMMAND_REGISTER_CMD_INDEX_CHECK_ENABLE (1 << 4)
#define HOST_REG_COMMAND_REGISTER_CRC_CHECK_ENABLE  (1 << 3)


#define HOST_REG_RESPONSE                           0x10  /* 32-bit reguisters 0x10 through 0x1C */

#define HOST_REG_BUFFER_DATA_PORT                   0x20

#define HOST_REG_PRESENT_STATE                      0x24
#define HOST_REG_PRESENT_STATE_WRITE_ENABLED        (1 << 19)
#define HOST_REG_PRESENT_STATE_CARD_DETECT          (1 << 18)
#define HOST_REG_PRESENT_STATE_CARD_STATE_STABLE    (1 << 17)
#define HOST_REG_PRESENT_STATE_CARD_INSERTED        (1 << 16)
#define HOST_REG_PRESENT_STATE_BUFFER_READ_ENABLE   (1 << 11)
#define HOST_REG_PRESENT_STATE_BUFFER_WRITE_ENABLE  (1 << 10)
#define HOST_REG_PRESENT_STATE_BUFFER_READ_TRANSFER_ACTIVE (1 << 9)
#define HOST_REG_PRESENT_STATE_BUFFER_WRITE_TRANSFER_ACTIVE (1 << 8)
#define HOST_REG_PRESENT_STATE_BUFFER_DAT_LINE_ACTIVE (1 << 2)
#define HOST_REG_PRESENT_STATE_BUFFER_COMMAND_INHIBIT_DAT (1 << 1)
#define HOST_REG_PRESENT_STATE_BUFFER_COMMAND_INHIBIT_CMD (1 << 0)


#define HOST_REG_CONTROL                        0x28
#define HOST_REG_CONTROL_LED_ON                 (1 << 0)
#define HOST_REG_CONTROL_1BIT_WIDTH             0x00
#define HOST_REG_CONTROL_4BIT_WIDTH             (1 << 1)
#define HOST_REG_CONTROL_HI_SPEED               (1 << 2)

#define HOST_REG_POWER_CONTROL                      0x29
#define HOST_REG_POWER_CONTROL_ON                   (1 << 0)
#define HOST_REG_POWER_CONTROL_VOLT_3_3             (7 << 1)
#define HOST_REG_POWER_CONTROL_VOLT_3_0             (6 << 1)
#define HOST_REG_POWER_CONTROL_VOLT_1_8             (5 << 1)

#define HOST_REG_BLOCK_GAP                          0x2A
#define HOST_REG_INT_DETECT_AT_BLOCK_GAP             (1 << 3)

#define HOST_REG_CLOCK_CONTROL                      0x2C
#define HOST_REG_CLOCK_CONTROL_CLOCK_ENABLE         (1 << 0)
#define HOST_REG_CLOCK_CONTROL_CLOCK_STABLE         (1 << 1)
#define HOST_REG_CLOCK_CONTROL_SD_ENABLE            (1 << 2)

#define HOST_REG_TIMEOUT_CONTROL                    0x2E
#define HOST_REG_TIMEOUT_CONTROL_DEFAULT            0x0C

#define HOST_REG_SW_RESET                           0x2F
#define HOST_REG_SW_RESET_ALL                       (1 << 0)
#define HOST_REG_SW_RST_CMD_LINE                    (1 << 1)
#define HOST_REG_SW_RST_DAT_LINE                    (1 << 2)

#define HOST_REG_NORMAL_INT_STATUS                  0x30
#define HOST_REG_NORMAL_INT_STATUS_ERROR            (1 << 15)
#define HOST_REG_NORMAL_INT_STATUS_CARD_INTERRUPT   (1 << 8)
#define HOST_REG_NORMAL_INT_STATUS_CARD_REMOVAL     (1 << 7)
#define HOST_REG_NORMAL_INT_STATUS_CARD_INSERT      (1 << 6)
#define HOST_REG_NORMAL_INT_STATUS_BUFFER_READ_RDY  (1 << 5)
#define HOST_REG_NORMAL_INT_STATUS_BUFFER_WRITE_RDY (1 << 4)
#define HOST_REG_NORMAL_INT_STATUS_DMA_INT          (1 << 3)
#define HOST_REG_NORMAL_INT_STATUS_BLOCK_GAP        (1 << 2)
#define HOST_REG_NORMAL_INT_STATUS_TRANSFER_COMPLETE (1 << 1)
#define HOST_REG_NORMAL_INT_STATUS_CMD_COMPLETE     (1 << 0)
#define HOST_REG_NORMAL_INT_STATUS_ALL_ERR          0xFFFF

#define HOST_REG_ERROR_INT_STATUS                   0x32
#define HOST_REG_ERROR_INT_STATUS_VENDOR_MASK       0xF000
#define HOST_REG_ERROR_INT_STATUS_VENDOR_SHIFT      12
#define HOST_REG_ERROR_INT_STATUS_AUTOCMD12ERR      (1 << 8)
#define HOST_REG_ERROR_INT_STATUS_CURRENTLIMITERR   (1 << 7)
#define HOST_REG_ERROR_INT_STATUS_DATAENDBITERR     (1 << 6)
#define HOST_REG_ERROR_INT_STATUS_DATACRCERR        (1 << 5)
#define HOST_REG_ERROR_INT_STATUS_DATATIMEOUTERR    (1 << 4)
#define HOST_REG_ERROR_INT_STATUS_CMDINDEXERR       (1 << 3)
#define HOST_REG_ERROR_INT_STATUS_CMDENDBITERR      (1 << 2)
#define HOST_REG_ERROR_INT_STATUS_CRCERR            (1 << 1)
#define HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR     (1 << 0)
#define HOST_REG_ERROR_INT_STATUS_ALL_ERR           0xFFFF

#define HOST_REG_INT_STATUS_ENABLE                  0x34
#define HOST_REG_INT_STATUS_CARD_INT_STAT_ENABLE    (1 << 8)
#define HOST_REG_INT_STATUS_CARD_REMOVAL_ENABLE     (1 << 7)
#define HOST_REG_INT_STATUS_CARD_INSERT_ENABLE      (1 << 6)
#define HOST_REG_INT_STATUS_BUFFER_READ_RDY_ENABLE  (1 << 5)
#define HOST_REG_INT_STATUS_BUFFER_WRITE_RDY_ENABLE (1 << 4)
#define HOST_REG_INT_STATUS_DMA_ENABLE              (1 << 3)
#define HOST_REG_INT_STATUS_BLOCK_GAP_ENABLE        (1 << 2)
#define HOST_REG_INT_STATUS_TRANSFER_COMPLETE_ENABLE (1 << 1)
#define HOST_REG_INT_STATUS_CMD_COMPLETE_ENABLE     (1 << 0)
#define HOST_REG_INT_STATUS_ALL                      0x00F3
#define HOST_REG_INT_STATUS_ALLOW_INSERT_REMOVE_ONLY 0x00C0

#define HOST_REG_ERR_STATUS_ENABLE                  0x36
/* same bits as HOST_REG_ERROR_INT_STATUS */

#define HOST_REG_INT_SIGNAL_ENABLE                  0x38
/* same bits as HOST_REG_INT_STATUS_ENABLE */

#define HOST_REG_INT_ERR_SIGNAL_ENABLE              0x3A
/* same bits as HOST_REG_ERR_STATUS_ENABLE */

#define HOST_REG_CAPABILITIES                       0x40
#define HOST_REG_CAPABILITIES_VOLT_1_8              (1 << 26)
#define HOST_REG_CAPABILITIES_VOLT_3_0              (1 << 25)
#define HOST_REG_CAPABILITIES_VOLT_3_3              (1 << 24)
#define HOST_REG_CAPABILITIES_SUSPEND_RESUME        (1 << 23)
#define HOST_REG_CAPABILITIES_DMA                   (1 << 22)
#define HOST_REG_CAPABILITIES_HIGH_SPEED            (1 << 21)
#define HOST_REG_CAPABILITIES_SUSPEND_RESUME        (1 << 23)
#define HOST_REG_CAPABILITIES_MAX_BLOCK_LEN_MASK    0x30000
#define HOST_REG_CAPABILITIES_MAX_BLOCK_LEN_SHIFT   16
#define HOST_REG_CAPABILITIES_CLOCK_MASK            0x3F00
#define HOST_REG_CAPABILITIES_CLOCK_SHIFT           8
#define HOST_REG_CAPABILITIES_TIMEOUT_CLOCK_UNITS   (1 << 7)
#define HOST_REG_CAPABILITIES_TIMEOUT_FREQ_MASK     0x3F
#define HOST_REG_CAPABILITIES_TIMEOUT_FREQ_SHIFT    0

#define HOST_REG_MAX_CURRENT_CAPABILITIES           0x48
#define HOST_REG_MAX_CURRENT_CAPABILITIES_1_8_MASK  0xFF0000
#define HOST_REG_MAX_CURRENT_CAPABILITIES_1_8_SHIFT 16
#define HOST_REG_MAX_CURRENT_CAPABILITIES_3_0_MASK  0x00FF00
#define HOST_REG_MAX_CURRENT_CAPABILITIES_3_0_SHIFT 8
#define HOST_REG_MAX_CURRENT_CAPABILITIES_3_3_MASK  0x0000FF
#define HOST_REG_MAX_CURRENT_CAPABILITIES_3_3_SHIFT 0
#define HOST_REG_MAX_CURRENT_CAPABILITIES_SCALER    4

#define HOST_REG_VERSION                            0xFE
#define HOST_REG_VERSION_SPEC_VERSION_MASK          0xFF
#define HOST_REG_VERSION_VENDOR_VERSION_MASK        0xFF00
#define HOST_REG_VERSION_VENDOR_VERSION_SHIFT       8

#define SDIO_BD_MAX_SLOTS                           24
#define SDIO_SD_MAX_BLOCKS                      ((UINT)0xFFFF)
#define SDMMC_RESP_TIMEOUT_CLOCKS          64
#define SDMMC_DATA_TIMEOUT_CLOCKS          0xFFFF

#define SPI_ENABLE_WITH_CRC  (MMC_SPI_CS_ENABLE | MMC_SPI_ENABLE | \
                              MMC_SPI_CRC_ENABLE | MMC_SPI_SEL_CS0)
#define SPI_ENABLE_NO_CRC  (MMC_SPI_CS_ENABLE | MMC_SPI_ENABLE | \
                            MMC_SPI_SEL_CS0)

#define SD_DEFAULT_RESPONSE_BYTES 6
#define SD_R2_RESPONSE_BYTES      16

#define SD_CLOCK_MAX_ENTRIES 9

typedef struct _SD_CLOCK_TBL_ENTRY {
    UINT      ClockRateDivisor;  /* divisor */
    UINT16    RegisterValue;     /* register value for clock divisor */  
}SD_CLOCK_TBL_ENTRY;

/* driver wide data, this driver only supports one device, 
 * so we include the per device data here also */
typedef struct _SDHCD_DRIVER_CONTEXT {
    PTEXT        pDescription;       /* human readable device decsription */
    SDLIST       DeviceList;         /* the list of current devices handled by this driver */
    OS_SEMAPHORE DeviceListSem;      /* protection for the DeviceList */
    UINT         DeviceCount;        /* number of devices currently installed */     
}SDHCD_DRIVER_CONTEXT, *PSDHCD_DRIVER_CONTEXT;


/* prototypes */
SDIO_STATUS HcdRequest(PSDHCD pHcd);
SDIO_STATUS HcdConfig(PSDHCD pHcd, PSDCONFIG pReq);
SDIO_STATUS HcdInitialize(PSDHCD_DEVICE pDeviceContext);
void HcdDeinitialize(PSDHCD_DEVICE pDeviceContext);
BOOL HcdSDInterrupt(PSDHCD_DEVICE pDeviceContext);
SDIO_STATUS QueueEventResponse(PSDHCD_DEVICE pDeviceContext, INT WorkItemID);
BOOL HcdTransferTxData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq);
void HcdTransferRxData(PSDHCD_DEVICE pDevice, PSDREQUEST pReq);
void SetPowerOn(PSDHCD_DEVICE pDeviceContext, BOOL On);
UINT16 MaskIrq(PSDHCD_DEVICE pDevice, UINT32 Mask);
UINT16 UnmaskIrq(PSDHCD_DEVICE pDevice, UINT32 Mask);
UINT16 MaskIrqFromIsr(PSDHCD_DEVICE pDevice, UINT32 Mask);
UINT16 UnmaskIrqFromIsr(PSDHCD_DEVICE pDevice, UINT32 Mask);
void EnableDisableSDIOIRQ(PSDHCD_DEVICE pDevice, BOOL Enable, BOOL FromIsr);


#endif /* __SDIO_PCIELLEN_HCD_H___ */
