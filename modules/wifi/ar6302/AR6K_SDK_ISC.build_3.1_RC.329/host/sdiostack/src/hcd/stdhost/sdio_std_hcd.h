/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
@file: sdio_std_hcd.h

@abstract: OS Independent standard host header file
 
@notice: Copyright (c), 2006 Atheros Communications, Inc.


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
#ifndef __SDIO_STD_HCD_H___
#define __SDIO_STD_HCD_H___

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
#include "linux/sdio_std_hcd_linux.h"
#endif /* LINUX */

#ifdef UNDER_CE
#include "wince/sdio_std_hcd_wince.h"
#endif /* LINUX */

enum STD_HOST_TRACE_ENUM {
    STD_HOST_TRACE_CARD_INSERT = (SDDBG_TRACE + 1),
    STD_HOST_TRACE_DATA = (SDDBG_TRACE + 2),       
    STD_HOST_TRACE_REQUESTS,  
    STD_HOST_TRACE_DATA_DUMP,  
    STD_HOST_TRACE_CONFIG,     
    STD_HOST_TRACE_INT,    
    STD_HOST_TRACE_CLOCK,
    STD_HOST_TRACE_SDIO_INT,
    STD_HOST_TRACE_LAST
};

typedef enum _STD_HCD_DMA_MODE {
    STD_HCD_DMA_NONE = 0,
    STD_HCD_DMA_COMMON = 1,
    STD_HCD_DMA_SG    
} STD_HCD_DMA_MODE;

        /* Host Controller register definitions */
#define HOST_REG_SYSTEM_ADDRESS                     0x00

#define HOST_REG_BLOCK_SIZE                         0x04
#define HOST_REG_BLOCK_SIZE_LEN_MASK                0x0FFF
#define HOST_REG_BLOCK_SIZE_DMA_MASK                0x7000
#define HOST_REG_BLOCK_SIZE_DMA_SHIFT               12
#define HOST_REG_BLOCK_SIZE_DMA_512K_BOUNDARY       (7 << HOST_REG_BLOCK_SIZE_DMA_SHIFT)

#define SDHC_SDMA_512K_BOUNDARY_LENGTH              (512*1024)  

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


#define HOST_REG_CONTROL                            0x28
#define HOST_REG_CONTROL_LED_ON                     (1 << 0)
#define HOST_REG_CONTROL_1BIT_WIDTH                 0x00
#define HOST_REG_CONTROL_4BIT_WIDTH                 (1 << 1)
#define HOST_REG_CONTROL_HI_SPEED                   (1 << 2)
#define HOST_REG_CONTROL_DMA_NONE                   (0 << 3)
#define HOST_REG_CONTROL_DMA_32BIT                  (1 << 3)
#define HOST_REG_CONTROL_DMA_64BIT                  (2 << 3)
#define HOST_REG_CONTROL_DMA_MASK                   (3 << 3)
#define HOST_REG_CONTROL_EXTENDED_DATA              (1 << 5)
#define HOST_REG_CONTROL_CARD_DETECT_TEST           (1 << 6)
#define HOST_REG_CONTROL_CARD_DETECT_SELECT         (1 << 7)
#define HOST_REG_CONTROL_BUSWIDTH_BITS \
    (HOST_REG_CONTROL_1BIT_WIDTH | HOST_REG_CONTROL_4BIT_WIDTH | HOST_REG_CONTROL_EXTENDED_DATA)


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
#define HOST_REG_CLOCK_CONTROL_FREQ_SELECT_MASK     (0xFF00)             
 
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
#define HOST_REG_NORMAL_INT_STATUS_CLEAR_ALL        0xFFFF

#define HOST_REG_ERROR_INT_STATUS                   0x32
#define HOST_REG_ERROR_INT_STATUS_VENDOR_MASK       0xE000
#define HOST_REG_ERROR_INT_STATUS_VENDOR_SHIFT      13
#define HOST_REG_ERROR_INT_STATUS_SDMAERR           (1 << 12)
#define HOST_REG_ERROR_INT_STATUS_ADMAERR           (1 << 9)
#define HOST_REG_ERROR_INT_STATUS_AUTOCMD12ERR      (1 << 8)
#define HOST_REG_ERROR_INT_STATUS_CURRENTLIMITERR   (1 << 7)
#define HOST_REG_ERROR_INT_STATUS_DATAENDBITERR     (1 << 6)
#define HOST_REG_ERROR_INT_STATUS_DATACRCERR        (1 << 5)
#define HOST_REG_ERROR_INT_STATUS_DATATIMEOUTERR    (1 << 4)
#define HOST_REG_ERROR_INT_STATUS_CMDINDEXERR       (1 << 3)
#define HOST_REG_ERROR_INT_STATUS_CMDENDBITERR      (1 << 2)
#define HOST_REG_ERROR_INT_STATUS_CRCERR            (1 << 1)
#define HOST_REG_ERROR_INT_STATUS_CMDTIMEOUTERR     (1 << 0)
#define HOST_REG_ERROR_INT_STATUS_ALL_ERR           0x7FF

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
#define HOST_REG_INT_STATUS_ALL                      0x00FB
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
#define HOST_REG_CAPABILITIES_ADMA                  (1 << 20) 
#define HOST_REG_CAPABILITIES_64                    (1 << 19) 
#define HOST_REG_CAPABILITIES_MMC8                  (1 << 18) 

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

#define HOST_REG_ADMA_ERR_STATUS                    0x54
#define HOST_REG_ADMA_ERR_LEN_MISMATCH              (1 << 2)
#define HOST_REG_ADMA_STATE_MASK                    0x03
#define HOST_REG_ADMA_STATE_SHIFT                   0
#define HOST_REG_ADMA_STATE_STOP                    0x0
#define HOST_REG_ADMA_STATE_FDS                     0x1
#define HOST_REG_ADMA_STATE_CADR                    0x2
#define HOST_REG_ADMA_STATE_TFR                     0x3

#define HOST_REG_ADMA_ADDRESS                       0x58

#define HOST_REG_SLOT_INT_STATUS                    0xFC
#define HOST_REG_SLOT_INT_MASK                      0xFF
#define HOST_REG_MAX_INT_SLOTS                      8  

#define HOST_REG_VERSION                            0xFE
#define HOST_REG_VERSION_SPEC_VERSION_MASK          0xFF
#define HOST_REG_VERSION_VENDOR_VERSION_MASK        0xFF00
#define HOST_REG_VERSION_VENDOR_VERSION_SHIFT       8

#define SDIO_BD_MAX_SLOTS                           24
#define SDIO_SD_MAX_BLOCKS                      ((UINT)0xFFFF)

#define SD_DEFAULT_RESPONSE_BYTES 6
#define SD_R2_RESPONSE_BYTES      16
#define STD_HOST_SHORT_TRANSFER_THRESHOLD 32
#define STD_HOST_COMMON_BUFFER_THRESHOLD  256
#define SD_CLOCK_MAX_ENTRIES 9

#define ASYNC_4_BIT_IRQ_CLOCK_RATE  0xFFFFFFFF

typedef struct _SD_CLOCK_TBL_ENTRY {
    UINT      ClockRateDivisor;  /* divisor */
    UINT16    RegisterValue;     /* register value for clock divisor */  
}SD_CLOCK_TBL_ENTRY;

    /* standard host controller instance */
typedef struct _SDHCD_INSTANCE {
    SDLIST  List;                      /* list */
    SDHCD   Hcd;                       /* HCD structure for registration */
    SDDMA_DESCRIPTION DmaDescription;  /* dma description for this HCD if used*/
    UINT32  Caps;                      /* host controller capabilities */
#define SDHC_HW_INIT    0x01
#define SDHC_REGISTERED 0x02
    UINT8        InitStateMask;   /* init state for hardware independent layer */
    BOOL         CardInserted;    /* card inserted flag */
    BOOL         Cancel;          /* cancel flag */
    BOOL         ShuttingDown;    /* indicates shut down of HCD */
    BOOL         StartUpCardCheckDone;
    UINT32       BaseClock;       /* base clock in hz */ 
    UINT32       TimeOut;         /* timeout setting */ 
    UINT32       ClockSpinLimit;  /* clock limit for command spin loops */
    BOOL         KeepClockOn;
    UINT32       BufferReadyWaitLimit;
    UINT32       TransferCompleteWaitLimit;
    UINT32       PresentStateWaitLimit;
    UINT32       ResetWaitLimit;
    BOOL         RequestCompleteQueued;
    PVOID        pRegs;           /* a more direct pointer to the registers */
    UINT16       ClockConfigIdle;      /* clock configuration for idle in 4-bit mode*/
    UINT16       ClockConfigNormal;    /* clock configuration for normal operation */
    UINT32       IdleBusClockRate;     /* clock rate when SDIO bus is idle and in 4-bit mode */
    BOOL         Idle1BitIRQ;          /* when the bus is idle, switch to 1 bit mode for IRQ detection */
    UINT32       CardDetectDebounceMS; /* debounce interval for the slot in milliseconds */
    STD_HCD_DMA_MODE DMAMode;
    UINT8        *pCommonBuffer;       /* common buffer virtual address
                                          This buffer must be cache coherent! */
    UINT32       CommonBufferPhys;     /* common buffer physical address */
    UINT32       CommonBufferLength;   /* length of common buffer */
    UINT32       CommonBufferUserDataOffset; /* user data offset of the common buffer */
    UINT32       FixedMaxSlotCurrent;  /* if non-zero, fixed max current in mA */
    UINT32       NonStdBehaviorFlags;  /* special flags to work around non-standard behavior */
    SDHCD_OS_SPECIFIC OsSpecific;      
}SDHCD_INSTANCE, *PSDHCD_INSTANCE;

#define NON_STD_WAIT_CMD_DONE (1 << 0)


#include "../../include/ctstartpack.h" 

/* scatter-gather tables, as we use it in 32-bit mode */
struct _SDHCD_SGDMA_DESCRIPTOR {
    UINT32      Length;
    UINT32      Address;
}CT_PACK_STRUCT; 

#include "../../include/ctendpack.h" 

typedef struct _SDHCD_SGDMA_DESCRIPTOR SDHCD_SGDMA_DESCRIPTOR;
typedef struct _SDHCD_SGDMA_DESCRIPTOR *PSDHCD_SGDMA_DESCRIPTOR; 

#define SDDMA_VALID         0x1
#define SDDMA_END           0x2
#define SDDMA_INT           0x4
#define SDDMA_LENGTH        0x10
#define SDDMA_TRANSFER      0x20
#define SDDMA_DESCRIP_LINK  0x30

#define SET_DMA_LENGTH(d, l)\
    ((d)->Length = ((l) << 12) | SDDMA_LENGTH | SDDMA_VALID)
#define SET_DMA_ADDRESS(d, l)\
    ((d)->Address = ((l) & 0xFFFFF000) | SDDMA_TRANSFER | SDDMA_VALID)
#define SET_DMA_END_OF_TRANSFER(d)\
    ((d)->Address |= SDDMA_END);

#define SDHCD_ADMA_BUFFER_PAGE_ALIGN   4096
    
/* prototypes */
SDIO_STATUS HcdRequest(PSDHCD pHcd);
SDIO_STATUS HcdConfig(PSDHCD pHcd, PSDCONFIG pReq);
SDIO_STATUS HcdInitialize(PSDHCD_INSTANCE pHcInstance);
void HcdDeinitialize(PSDHCD_INSTANCE pHcInstance);
BOOL HcdSDInterrupt(PSDHCD_INSTANCE pHcInstance);
void ProcessDeferredCardDetect(PSDHCD_INSTANCE pHcInstance);
SDIO_STATUS QueueEventResponse(PSDHCD_INSTANCE pHcInstance, INT WorkItemID);
BOOL HcdTransferTxData(PSDHCD_INSTANCE pHcInstance, PSDREQUEST pReq);
void HcdTransferRxData(PSDHCD_INSTANCE pHcInstance, PSDREQUEST pReq);
void SetPowerOn(PSDHCD_INSTANCE pHcInstance, BOOL On);
UINT16 MaskIrq(PSDHCD_INSTANCE pHcInstance, UINT32 Mask, BOOL FromIsr);
UINT16 UnmaskIrq(PSDHCD_INSTANCE pHcInstance, UINT32 Mask, BOOL FromIsr);
#define MaskIrqFromIsr(p,m) MaskIrq((p),(m),TRUE)
#define UnmaskIrqFromIsr(p,m) UnmaskIrq((p),(m),TRUE)

void EnableDisableSDIOIRQ(PSDHCD_INSTANCE pHcInstance, BOOL Enable, BOOL FromIsr);
SDIO_STATUS SetUpHCDDMA(PSDHCD_INSTANCE pHcInstance, PSDREQUEST pReq);
void HcdTransferDataDMAEnd(PSDHCD_INSTANCE pHcInstance, PSDREQUEST pReq);
void DumpStdHcdRegisters(PSDHCD_INSTANCE pHcInstance);
void DumpDMADescriptorsInfo(PSDHCD_INSTANCE pHcInstance);
void DumpCurrentRequestInfo(PSDHCD_INSTANCE pHcInstance);

#endif
