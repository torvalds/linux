// Copyright (c) 2004-2006 Atheros Communications Inc.
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
@file: sdio_busdriver.h

@abstract: include file for registration of SDIO function drivers
  and SDIO host controller bus drivers.
 
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#ifndef __SDIO_BUSDRIVER_H___
#define __SDIO_BUSDRIVER_H___

typedef UINT8      CT_VERSION_CODE;
#define CT_SDIO_STACK_VERSION_CODE ((CT_VERSION_CODE)0x26)   /* version code that must be set in various structures */
#define CT_SDIO_STACK_VERSION_MAJOR(v) (((v) & 0xF0) >> 4)
#define CT_SDIO_STACK_VERSION_MINOR(v) (((v) & 0x0F))
#define SET_SDIO_STACK_VERSION(p) (p)->Version = CT_SDIO_STACK_VERSION_CODE
#define GET_SDIO_STACK_VERSION(p) (p)->Version
#define GET_SDIO_STACK_VERSION_MAJOR(p) CT_SDIO_STACK_VERSION_MAJOR(GET_SDIO_STACK_VERSION(p))
#define GET_SDIO_STACK_VERSION_MINOR(p) CT_SDIO_STACK_VERSION_MINOR(GET_SDIO_STACK_VERSION(p))
#include "sdlist.h"

/* card flags */
typedef UINT16      CARD_INFO_FLAGS;
#define CARD_MMC        0x0001    /* Multi-media card */
#define CARD_SD         0x0002    /* SD-Memory present */
#define CARD_SDIO       0x0004    /* SDIO present */
#define CARD_RAW        0x0008    /* Raw card */
#define CARD_COMBO      (CARD_SD | CARD_SDIO)  /* SDIO with SD */
#define CARD_TYPE_MASK  0x000F    /* card type mask */
#define CARD_SD_WP      0x0010    /* SD WP on */
#define CARD_PSEUDO     0x0020    /* pseudo card (internal use) */
#define CARD_HIPWR      0x0040    /* card can use more than 200mA (SDIO 1.1 or greater)*/
#define GET_CARD_TYPE(flags) ((flags) & CARD_TYPE_MASK)

/* bus mode and clock rate */
typedef UINT32  SD_BUSCLOCK_RATE;       /* clock rate in hz */
typedef UINT16  SD_BUSMODE_FLAGS;  
#define SDCONFIG_BUS_WIDTH_RESERVED           0x00
#define SDCONFIG_BUS_WIDTH_SPI                0x01
#define SDCONFIG_BUS_WIDTH_1_BIT              0x02
#define SDCONFIG_BUS_WIDTH_4_BIT              0x03
#define SDCONFIG_BUS_WIDTH_MMC8_BIT           0x04
#define SDCONFIG_BUS_WIDTH_MASK               0x0F
#define SDCONFIG_SET_BUS_WIDTH(flags,width) \
{                       \
    (flags) &= ~SDCONFIG_BUS_WIDTH_MASK; \
    (flags) |= (width);                  \
} 
#define SDCONFIG_GET_BUSWIDTH(flags) ((flags) & SDCONFIG_BUS_WIDTH_MASK)
#define SDCONFIG_BUS_MODE_SPI_NO_CRC         0x40   /* SPI bus is operating with NO CRC */
#define SDCONFIG_BUS_MODE_SD_HS              0x80   /* set interface to SD high speed mode  */
#define SDCONFIG_BUS_MODE_MMC_HS             0x20   /* set interface to MMC high speed mode */
 
typedef UINT16 SD_SLOT_CURRENT;      /* slot current in mA */

typedef UINT8 SLOT_VOLTAGE_MASK;     /* slot voltage */
#define SLOT_POWER_3_3V  0x01
#define SLOT_POWER_3_0V  0x02
#define SLOT_POWER_2_8V  0x04
#define SLOT_POWER_2_0V  0x08
#define SLOT_POWER_1_8V  0x10
#define SLOT_POWER_1_6V  0x20

#define MAX_CARD_RESPONSE_BYTES 17

/* plug and play information for SD cards */
typedef struct _SD_PNP_INFO {
    UINT16 SDIO_ManufacturerCode;  /* JEDEC Code */
    UINT16 SDIO_ManufacturerID;    /* manf-specific ID */
    UINT8  SDIO_FunctionNo;        /* function number 1-7 */ 
    UINT8  SDIO_FunctionClass;     /* function class */
    UINT8  SDMMC_ManfacturerID;    /* card CID's MANF-ID */
    UINT16 SDMMC_OEMApplicationID; /* card CID's OEMAPP-ID */
    CARD_INFO_FLAGS CardFlags;     /* card flags */                                                              		
}SD_PNP_INFO, *PSD_PNP_INFO;

#define IS_LAST_SDPNPINFO_ENTRY(id)\
    (((id)->SDIO_ManufacturerCode == 0) &&\
     ((id)->SDIO_ManufacturerID == 0) &&\
     ((id)->SDIO_FunctionNo == 0) &&\
     ((id)->SDIO_FunctionClass == 0) &&\
     ((id)->SDMMC_OEMApplicationID == 0) && \
     ((id)->CardFlags == 0))
     
/* card properties */
typedef struct _CARD_PROPERTIES {
    UINT8              IOFnCount;      /* number of I/O functions */
    UINT8              SDIORevision;   /* SDIO revision */
#define SDIO_REVISION_1_00 0x00
#define SDIO_REVISION_1_10 0x01
#define SDIO_REVISION_1_20 0x02
#define SDIO_REVISION_2_00 0x03
    UINT8              SD_MMC_Revision; /* SD or MMC revision */
#define SD_REVISION_1_01  0x00
#define SD_REVISION_1_10  0x01
#define MMC_REVISION_1_0_2_2 0x00
#define MMC_REVISION_3_1  0x01
#define MMC_REVISION_4_0  0x02
    UINT16 SDIO_ManufacturerCode;      /* JEDEC Code */
    UINT16 SDIO_ManufacturerID;        /* manf-specific ID */
    UINT32             CommonCISPtr;   /* common CIS ptr */
    UINT16             RCA;            /* relative card address */
    UINT8              SDIOCaps;       /* SDIO card capabilities (refer to SDIO spec for decoding) */
    UINT8              CardCSD[MAX_CARD_RESPONSE_BYTES];    /* for SD/MMC cards */
    CARD_INFO_FLAGS    Flags;          /* card flags */
    SD_BUSCLOCK_RATE   OperBusClock;   /* operational bus clock (based on HCD limit)*/
    SD_BUSMODE_FLAGS   BusMode;        /* current card bus mode */
    UINT16             OperBlockLenLimit; /* operational bytes per block length limit*/
    UINT16             OperBlockCountLimit; /* operational number of blocks per transfer limit */
    UINT8              CardState;      /* card state flags */
    SLOT_VOLTAGE_MASK  CardVoltage;    /* card operational voltage */
#define CARD_STATE_REMOVED 0x01
}CARD_PROPERTIES, *PCARD_PROPERTIES;

/* SDREQUEST request flags */
typedef UINT32 SDREQUEST_FLAGS;
/* write operation */
#define SDREQ_FLAGS_DATA_WRITE         0x8000
/* has data (read or write) */
#define SDREQ_FLAGS_DATA_TRANS         0x4000
/* command is an atomic APP command, requiring CMD55 to be issued */
#define SDREQ_FLAGS_APP_CMD            0x2000
/* transfer should be handled asynchronously */
#define SDREQ_FLAGS_TRANS_ASYNC        0x1000
/* host should skip the SPI response filter for this command */
#define SDREQ_FLAGS_RESP_SKIP_SPI_FILT 0x0800
/* host should skip the response check for this data transfer */
#define SDREQ_FLAGS_DATA_SKIP_RESP_CHK 0x0400
/* flag requesting a CMD12 be automatically issued by host controller */
#define SDREQ_FLAGS_AUTO_CMD12         0x0200 
/* flag indicating that the data buffer meets HCD's DMA restrictions   */
#define SDREQ_FLAGS_DATA_DMA           0x0010 
/* indicate to host that this is a short and quick transfer, the HCD may optimize
 * this request to reduce interrupt overhead */
#define SDREQ_FLAGS_DATA_SHORT_TRANSFER   0x00010000
/* indicate to the host that this is a raw request */
#define SDREQ_FLAGS_RAW                   0x00020000
/* auto data transfer status check for MMC and Memory cards */
#define SDREQ_FLAGS_AUTO_TRANSFER_STATUS  0x00100000

#define SDREQ_FLAGS_UNUSED1               0x00200000
#define SDREQ_FLAGS_UNUSED2               0x00400000
#define SDREQ_FLAGS_UNUSED3               0x00800000
#define SDREQ_FLAGS_UNUSED4               0x01000000
#define SDREQ_FLAGS_UNUSED5               0x02000000

/* the following flags are internal use only */
#define SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE 0x0100
/* flag indicating that response has been converted (internal use) */
#define SDREQ_FLAGS_RESP_SPI_CONVERTED      0x0040
/* request was cancelled - internal use only */
#define SDREQ_FLAGS_CANCELED                0x0020
/* a barrier operation */
#define SDREQ_FLAGS_BARRIER                 0x00040000
/* a pseudo bus request */
#define SDREQ_FLAGS_PSEUDO                  0x00080000
/* queue to the head */
#define SDREQ_FLAGS_QUEUE_HEAD              0x04000000

#define SDREQ_FLAGS_I_UNUSED1               0x08000000
#define SDREQ_FLAGS_I_UNUSED2               0x10000000
#define SDREQ_FLAGS_I_UNUSED3               0x20000000
#define SDREQ_FLAGS_I_UNUSED4               0x40000000
#define SDREQ_FLAGS_I_UNUSED5               0x80000000

/* response type mask */
#define SDREQ_FLAGS_RESP_MASK       0x000F
#define GET_SDREQ_RESP_TYPE(flags)     ((flags) & SDREQ_FLAGS_RESP_MASK)
#define IS_SDREQ_WRITE_DATA(flags)     ((flags) & SDREQ_FLAGS_DATA_WRITE)
#define IS_SDREQ_DATA_TRANS(flags)     ((flags) & SDREQ_FLAGS_DATA_TRANS)
#define IS_SDREQ_RAW(flags)            ((flags) & SDREQ_FLAGS_RAW) 
#define IS_SDREQ_FORCE_DEFERRED_COMPLETE(flags) ((flags) & SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE)
#define SDREQ_FLAGS_NO_RESP         0x0000
#define SDREQ_FLAGS_RESP_R1         0x0001
#define SDREQ_FLAGS_RESP_R1B        0x0002
#define SDREQ_FLAGS_RESP_R2         0x0003
#define SDREQ_FLAGS_RESP_R3         0x0004
#define SDREQ_FLAGS_RESP_MMC_R4     0x0005 /* not supported, for future use */
#define SDREQ_FLAGS_RESP_MMC_R5     0x0006 /* not supported, for future use */
#define SDREQ_FLAGS_RESP_R6         0x0007
#define SDREQ_FLAGS_RESP_SDIO_R4    0x0008
#define SDREQ_FLAGS_RESP_SDIO_R5    0x0009

struct _SDREQUEST;
struct _SDFUNCTION;

typedef void (*PSDEQUEST_COMPLETION)(struct _SDREQUEST *);

/* defines SD/MMC and SDIO requests for the RAW-mode API */
typedef struct _SDREQUEST {
    SDLIST  SDList;             /* internal use list*/
    UINT32  Argument;           /* SD/SDIO/MMC 32 bit argument */
    SDREQUEST_FLAGS Flags;      /* request flags */
    ATOMIC_FLAGS InternalFlags; /* internal use flags */
    UINT8   Command;            /* SD/SDIO/MMC 8 bit command */
    UINT8   Response[MAX_CARD_RESPONSE_BYTES];       /* buffer for CMD response */
    UINT16  BlockCount;         /* number of blocks to send/rcv */
    UINT16  BlockLen;           /* length of each block */
    UINT16  DescriptorCount;    /* number of DMA descriptor entries in pDataBuffer if DMA */
    PVOID   pDataBuffer;        /* starting address of buffer (or ptr to PSDDMA_DESCRIPTOR*/
    UINT32  DataRemaining;      /* number of bytes remaining in the transfer (internal use) */
    PVOID   pHcdContext;        /* internal use context */
    PSDEQUEST_COMPLETION pCompletion; /* function driver completion routine */
    PVOID   pCompleteContext;   /* function driver completion context */
    SDIO_STATUS Status;         /* completion status */
    struct _SDFUNCTION* pFunction; /* function driver that generated request (internal use)*/
    INT     RetryCount;          /* number of times to retry on error, non-data cmds only */
    PVOID   pBdRsv1;        /* reserved */
    PVOID   pBdRsv2;
    PVOID   pBdRsv3;
}SDREQUEST, *PSDREQUEST;

    /* a request queue */
typedef struct _SDREQUESTQUEUE {
    SDLIST        Queue;           /* the queue of requests */
    BOOL          Busy;            /* busy flag */
}SDREQUESTQUEUE, *PSDREQUESTQUEUE;


typedef UINT16 SDCONFIG_COMMAND;
/* SDCONFIG request flags */
/* get operation */
#define SDCONFIG_FLAGS_DATA_GET       0x8000
/* put operation */
#define SDCONFIG_FLAGS_DATA_PUT       0x4000
/* host controller */
#define SDCONFIG_FLAGS_HC_CONFIG      0x2000
/* both */
#define SDCONFIG_FLAGS_DATA_BOTH      (SDCONFIG_FLAGS_DATA_GET | SDCONFIG_FLAGS_DATA_PUT)
/* no data */
#define SDCONFIG_FLAGS_DATA_NONE      0x0000

/* SDCONFIG commands */
#define SDCONFIG_GET_HCD_DEBUG   (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_GET  | 275)
#define SDCONFIG_SET_HCD_DEBUG   (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_PUT  | 276)
#define SDCONFIG_DUMP_HCD_STATE  (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_NONE | 277)

/* custom hcd commands */
#define SDCONFIG_GET_HOST_CUSTOM   (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_GET  | 300)
#define SDCONFIG_PUT_HOST_CUSTOM   (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_PUT  | 301)

/* function commands */
#define SDCONFIG_FUNC_ENABLE_DISABLE         (SDCONFIG_FLAGS_DATA_PUT  | 18)
#define SDCONFIG_FUNC_UNMASK_IRQ             (SDCONFIG_FLAGS_DATA_NONE | 21)
#define SDCONFIG_FUNC_MASK_IRQ               (SDCONFIG_FLAGS_DATA_NONE | 22)
#define SDCONFIG_FUNC_ACK_IRQ                (SDCONFIG_FLAGS_DATA_NONE | 23)
#define SDCONFIG_FUNC_SPI_MODE_DISABLE_CRC   (SDCONFIG_FLAGS_DATA_NONE | 24)
#define SDCONFIG_FUNC_SPI_MODE_ENABLE_CRC    (SDCONFIG_FLAGS_DATA_NONE | 25)
#define SDCONFIG_FUNC_ALLOC_SLOT_CURRENT     (SDCONFIG_FLAGS_DATA_PUT  | 26)   
#define SDCONFIG_FUNC_FREE_SLOT_CURRENT      (SDCONFIG_FLAGS_DATA_NONE | 27) 
#define SDCONFIG_FUNC_CHANGE_BUS_MODE        (SDCONFIG_FLAGS_DATA_BOTH | 28)
#define SDCONFIG_FUNC_CHANGE_BUS_MODE_ASYNC  (SDCONFIG_FLAGS_DATA_BOTH | 29)  
#define SDCONFIG_FUNC_NO_IRQ_PEND_CHECK      (SDCONFIG_FLAGS_DATA_NONE | 30) 

typedef UINT8  FUNC_ENABLE_DISABLE_FLAGS;
typedef UINT32 FUNC_ENABLE_TIMEOUT;

    /* function enable */
typedef struct _SDCONFIG_FUNC_ENABLE_DISABLE_DATA {
#define SDCONFIG_DISABLE_FUNC   0x0000
#define SDCONFIG_ENABLE_FUNC    0x0001
    FUNC_ENABLE_DISABLE_FLAGS    EnableFlags;     /* enable flags*/
    FUNC_ENABLE_TIMEOUT          TimeOut;         /* timeout in milliseconds */ 
    void (*pOpComplete)(PVOID Context, SDIO_STATUS status); /* reserved */
    PVOID                        pOpCompleteContext;        /* reserved */   
}SDCONFIG_FUNC_ENABLE_DISABLE_DATA, *PSDCONFIG_FUNC_ENABLE_DISABLE_DATA;

    /* slot current allocation data */
typedef struct _SDCONFIG_FUNC_SLOT_CURRENT_DATA {
    SD_SLOT_CURRENT     SlotCurrent;    /* slot current to request in mA*/   
}SDCONFIG_FUNC_SLOT_CURRENT_DATA, *PSDCONFIG_FUNC_SLOT_CURRENT_DATA;

/* slot bus mode configuration */
typedef struct _SDCONFIG_BUS_MODE_DATA {
    SD_BUSCLOCK_RATE   ClockRate;       /* clock rate in Hz */
    SD_BUSMODE_FLAGS   BusModeFlags;    /* bus mode flags */
    SD_BUSCLOCK_RATE   ActualClockRate; /* actual rate in KHz */
}SDCONFIG_BUS_MODE_DATA, *PSDCONFIG_BUS_MODE_DATA;

/* defines configuration requests for the HCD */
typedef struct _SDCONFIG {
    SDCONFIG_COMMAND  Cmd;          /* configuration command */
    PVOID   pData;        /* configuration data */
    INT     DataLength;   /* config data length */
}SDCONFIG, *PSDCONFIG;

#define SET_SDCONFIG_CMD_INFO(pHdr,cmd,pC,len) \
{           \
  (pHdr)->Cmd = (cmd);                     \
  (pHdr)->pData = (PVOID)(pC);             \
  (pHdr)->DataLength = (len);              \
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get a pointer to the configuration command data.

  @function name: GET_SDCONFIG_CMD
  @prototype: UNIT16 GET_SDCONFIG_CMD (PSDCONFIG pCommand) 
  @category: HD_Reference
 
  @input:  pCommand - config command structure.
           
  @return: command code
 
  @notes: Implemented as a macro. This macro returns the command code for this
          configuration request.
           
  @example: getting the command code: 
    cmd = GET_SDCONFIG_CMD(pConfig);
    switch (cmd) {
        case SDCONFIG_GET_WP:
             .. get write protect switch position
           break;
        ...
    }
        
  @see also: GET_SDCONFIG_CMD_LEN, GET_SDCONFIG_CMD_DATA
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define GET_SDCONFIG_CMD(pBuffer)     ((pBuffer)->Cmd)
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get a pointer to the configuration command data.

  @function name: GET_SDCONFIG_CMD_LEN
  @prototype: INT GET_SDCONFIG_CMD_LEN (PSDCONFIG pCommand) 
  @category: HD_Reference
 
  @input:  pCommand - config command structure.
           
  @return: length of config command data
 
  @notes: Implemented as a macro. Host controller drivers can use this macro to extract 
          the number of bytes of command specific data. This can be used to validate the
          config data buffer size.
           
  @example: getting the data length: 
    length = GET_SDCONFIG_CMD_LEN(pConfig);
    if (length < CUSTOM_COMMAND_XXX_SIZE) {
       ... invalid length
    } 
        
  @see also: GET_SDCONFIG_CMD, GET_SDCONFIG_CMD_DATA
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define GET_SDCONFIG_CMD_LEN(pBuffer) ((pBuffer)->DataLength)
/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get a pointer to the configuration command data.

  @function name: GET_SDCONFIG_CMD_DATA
  @prototype: (casted ptr) GET_SDCONFIG_CMD_DATA (type, PSDCONFIG pCommand) 
  @category: HD_Reference
 
  @input:  type - pointer type to cast the returned pointer to.
           pCommand - config command structure.
           
  @return: type-casted pointer to the command's data
 
  @notes: Implemented as a macro.  Host controller drivers can use this macro to extract 
          a pointer to the command specific data in an HCD configuration request.
           
  @example: getting the pointer: 
        // get interrupt control data
    pIntControl = GET_SDCONFIG_CMD_DATA(PSDCONFIG_SDIO_INT_CTRL_DATA,pConfig);
    if (pIntControl->SlotIRQEnable) {
       ... enable slot IRQ detection
    } 
        
  @see also: GET_SDCONFIG_CMD, GET_SDCONFIG_CMD_LEN
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define GET_SDCONFIG_CMD_DATA(type,pBuffer) ((type)((pBuffer)->pData))
#define IS_SDCONFIG_CMD_GET(pBuffer)  ((pBuffer)->Cmd & SDCONFIG_FLAGS_DATA_GET)
#define IS_SDCONFIG_CMD_PUT(pBuffer)  ((pBuffer)->Cmd & SDCONFIG_FLAGS_DATA_PUT)

struct _SDDEVICE;
struct _SDHCD;

typedef UINT8   SD_FUNCTION_FLAGS;  
#define SDFUNCTION_FLAG_REMOVING       0x01

/* function driver registration structure */
typedef struct _SDFUNCTION {
    CT_VERSION_CODE Version;    /* version code of the SDIO stack */
	SDLIST     SDList;          /* internal use list*/
    PTEXT      pName;           /* name of registering driver */
    UINT       MaxDevices;      /* maximum number of devices supported by this function */
    UINT       NumDevices;      /* number of devices supported by this function */
    PSD_PNP_INFO pIds;          /* null terminated table of supported devices*/
    BOOL (*pProbe)(struct _SDFUNCTION *pFunction, struct _SDDEVICE *pDevice);/* New device inserted */
                                /* Device removed (NULL if not a hot-plug capable driver) */
    void (*pRemove)(struct _SDFUNCTION *pFunction, struct _SDDEVICE *pDevice);   
    SDIO_STATUS (*pSuspend)(struct _SDFUNCTION *pFunction, SDPOWER_STATE state); /* Device suspended */
    SDIO_STATUS (*pResume)(struct _SDFUNCTION *pFunction); /* Device woken up */
                                /* Enable wake event */
    SDIO_STATUS (*pWake) (struct _SDFUNCTION *pFunction, SDPOWER_STATE state, BOOL enable); 
	PVOID      pContext;        /* function driver use data */
	OS_PNPDRIVER Driver;	    /* driver registration with base system */
	SDLIST     DeviceList;	    /* the list of devices this driver is using*/
    OS_SIGNAL   CleanupReqSig;  /* wait for requests completion on cleanup (internal use) */
    SD_FUNCTION_FLAGS Flags;    /* internal flags (internal use) */
}SDFUNCTION, *PSDFUNCTION;

typedef UINT8  HCD_EVENT;

    /* device info for SDIO functions */
typedef struct _SDIO_DEVICE_INFO {
    UINT32  FunctionCISPtr;         /* function's CIS ptr */
    UINT32  FunctionCSAPtr;         /* function's CSA ptr */ 
    UINT16  FunctionMaxBlockSize;   /* function's reported max block size */
}SDIO_DEVICE_INFO, *PSDIO_DEVICE_INFO;

    /* device info for SD/MMC card functions */
typedef struct _SDMMC_INFO{
    UINT8  Unused;    /* reserved */
}SDMMC_INFO, *PSDMMC_INFO;

    /* union of SDIO function and device info */
typedef union _SDDEVICE_INFO {
    SDIO_DEVICE_INFO AsSDIOInfo; 
    SDMMC_INFO       AsSDMMCInfo;
}SDDEVICE_INFO, *PSDDEVICE_INFO;


typedef UINT8   SD_DEVICE_FLAGS;  
#define SDDEVICE_FLAG_REMOVING       0x01

/* inserted device description, describes an inserted card */
typedef struct _SDDEVICE {
    SDLIST      SDList;             /* internal use list*/
    SDLIST      FuncListLink;       /* internal use list */
                                    /* read/write request function */
    SDIO_STATUS (*pRequest)(struct _SDDEVICE *pDev, PSDREQUEST req); 
                                    /* get/set configuration */
    SDIO_STATUS (*pConfigure)(struct _SDDEVICE *pDev, PSDCONFIG config);
    PSDREQUEST  (*AllocRequest)(struct _SDDEVICE *pDev);      /* allocate a request */
    void        (*FreeRequest)(struct _SDDEVICE *pDev, PSDREQUEST pReq); /* free the request */
    void        (*pIrqFunction)(PVOID pContext);       /* interrupt routine, synchronous calls allowed */
    void        (*pIrqAsyncFunction)(PVOID pContext); /* async IRQ function , asynch only calls */
    PVOID       IrqContext;         /* irq context */  
    PVOID       IrqAsyncContext;    /* irq async context */ 
    PSDFUNCTION pFunction;          /* function driver supporting this device */
    struct _SDHCD  *pHcd;           /* host controller this device is on (internal use) */
    SDDEVICE_INFO   DeviceInfo;     /* device info */
    SD_PNP_INFO pId[1];             /* id of this device  */
    OS_PNPDEVICE Device;            /* device registration with base system */
    SD_SLOT_CURRENT  SlotCurrentAlloc; /* allocated slot current for this device/function (internal use) */
    SD_DEVICE_FLAGS Flags;          /* internal use flags */
    CT_VERSION_CODE Version;        /* version code of the bus driver */
}SDDEVICE, *PSDDEVICE;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get SDIO Bus Driver Version Major number
  
  @function name: SDDEVICE_GET_VERSION_MAJOR
  @prototype: INT SDDEVICE_GET_VERSION_MAJOR(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: integer value for the major version
 
  @notes: Implemented as a macro.
                
  @see also: SDDEVICE_GET_VERSION_MINOR
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_VERSION_MAJOR(pDev) (GET_SDIO_STACK_VERSION_MAJOR(pDev))

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get SDIO Bus Driver Version Minor number
  
  @function name: SDDEVICE_GET_VERSION_MINOR
  @prototype: INT SDDEVICE_GET_VERSION_MINOR(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: integer value for the minor version
 
  @notes: Implemented as a macro.
                
  @see also: SDDEVICE_GET_VERSION_MAJOR
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_VERSION_MINOR(pDev) (GET_SDIO_STACK_VERSION_MINOR(pDev))

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Test the SDIO revision for greater than or equal to 1.10
  
  @function name: SDDEVICE_IS_SDIO_REV_GTEQ_1_10
  @prototype: BOOL SDDEVICE_IS_SDIO_REV_GTEQ_1_10(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: TRUE if the revision is greater than or equal to 1.10
 
  @notes: Implemented as a macro.
                
  @see also: SDDEVICE_IS_SD_REV_GTEQ_1_10
  @see also: SDDEVICE_IS_MMC_REV_GTEQ_4_0
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_IS_SDIO_REV_GTEQ_1_10(pDev) ((pDev)->pHcd->CardProperties.SDIORevision >= SDIO_REVISION_1_10)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Test the SDIO revision for greater than or equal to 1.20
  
  @function name: SDDEVICE_IS_SDIO_REV_GTEQ_1_20
  @prototype: BOOL SDDEVICE_IS_SDIO_REV_GTEQ_1_20(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: TRUE if the revision is greater than or equal to 1.20
 
  @notes: Implemented as a macro.
                
  @see also: SDDEVICE_IS_SD_REV_GTEQ_1_10
  @see also: SDDEVICE_IS_SDIO_REV_GTEQ_1_10
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_IS_SDIO_REV_GTEQ_1_20(pDev) ((pDev)->pHcd->CardProperties.SDIORevision >= SDIO_REVISION_1_20)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Test the SD revision for greater than or equal to 1.10
  
  @function name: SDDEVICE_IS_SD_REV_GTEQ_1_10
  @prototype: BOOL SDDEVICE_IS_SD_REV_GTEQ_1_10(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: TRUE if the revision is greater than or equal to 1.10
 
  @notes: Implemented as a macro.
                
  @see also: SDDEVICE_IS_SDIO_REV_GTEQ_1_10
  @see also: SDDEVICE_IS_MMC_REV_GTEQ_4_0
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_IS_SD_REV_GTEQ_1_10(pDev) ((pDev)->pHcd->CardProperties.SD_MMC_Revision >= SD_REVISION_1_10)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Test the MMC revision for greater than or equal to 4.0
  
  @function name: SDDEVICE_IS_MMC_REV_GTEQ_4_0
  @prototype: BOOL SDDEVICE_IS_MMC_REV_GTEQ_4_0(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: TRUE if the revision is greater than or equal to 4.0
 
  @notes: Implemented as a macro.
                
  @see also: SDDEVICE_IS_SDIO_REV_GTEQ_1_10
  @see also: SDDEVICE_IS_SD_REV_GTEQ_1_10
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_IS_MMC_REV_GTEQ_4_0(pDev) ((pDev)->pHcd->CardProperties.SD_MMC_Revision >= MMC_REVISION_4_0)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Test for write protect enabled
  
  @function name: SDDEVICE_IS_CARD_WP_ON
  @prototype: BOOL SDDEVICE_IS_CARD_WP_ON(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: TRUE if device is write protected.
 
  @notes: Implemented as a macro.
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_IS_CARD_WP_ON(pDev)       ((pDev)->pHcd->CardProperties.Flags & CARD_SD_WP)


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the device's manufacturer specific ID
  
  @function name: SDDEVICE_GET_SDIO_MANFID
  @prototype: UINT16 SDDEVICE_GET_SDIO_MANFID(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: function number
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_MANFCODE
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_MANFID(pDev)     (pDev)->pId[0].SDIO_ManufacturerID

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the device's manufacturer code
  
  @function name: SDDEVICE_GET_SDIO_MANFCODE
  @prototype: UINT16 SDDEVICE_GET_SDIO_MANFCODE(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: function number
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_MANFID
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_MANFCODE(pDev)     (pDev)->pId[0].SDIO_ManufacturerCode

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the device's function number
  
  @function name: SDDEVICE_GET_SDIO_FUNCNO
  @prototype: UINT8 SDDEVICE_GET_SDIO_FUNCNO(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: function number
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_FUNC_CLASS
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_FUNCNO(pDev)     (pDev)->pId[0].SDIO_FunctionNo

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the functions's class
  
  @function name: SDDEVICE_GET_SDIO_FUNC_CLASS
  @prototype: UINT8 SDDEVICE_GET_SDIO_FUNC_CLASS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: class number
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_FUNCNO
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_FUNC_CLASS(pDev) (pDev)->pId[0].SDIO_FunctionClass

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the functions's Card Information Structure pointer
  
  @function name: SDDEVICE_GET_SDIO_FUNC_CISPTR
  @prototype: UINT32 SDDEVICE_GET_SDIO_FUNC_CISPTR(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: CIS offset
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_FUNC_CSAPTR
  @see also: SDDEVICE_GET_SDIO_COMMON_CISPTR
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_FUNC_CISPTR(pDev)(pDev)->DeviceInfo.AsSDIOInfo.FunctionCISPtr

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the functions's Code Stoarge Area pointer
  
  @function name: SDDEVICE_GET_SDIO_FUNC_CSAPTR
  @prototype: UINT32 SDDEVICE_GET_SDIO_FUNC_CSAPTR(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: CSA offset
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_FUNC_CISPTR
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_FUNC_CSAPTR(pDev)(pDev)->DeviceInfo.AsSDIOInfo.FunctionCSAPtr

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the functions's maximum reported block size
  
  @function name: SDDEVICE_GET_SDIO_FUNC_MAXBLKSIZE
  @prototype: UINT16 SDDEVICE_GET_SDIO_FUNC_MAXBLKSIZE(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: block size
 
  @notes: Implemented as a macro.
  
  @see also: 
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_FUNC_MAXBLKSIZE(pDev) (pDev)->DeviceInfo.AsSDIOInfo.FunctionMaxBlockSize

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the common Card Information Structure pointer
  
  @function name: SDDEVICE_GET_SDIO_COMMON_CISPTR
  @prototype: UINT32 SDDEVICE_GET_SDIO_COMMON_CISPTR(PSDDEVICE pDevice)
  @category: PD_Reference
   
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: Common CIS Address (in SDIO address space)
 
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_SDIO_FUNC_CSAPTR
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_COMMON_CISPTR(pDev) (pDev)->pHcd->CardProperties.CommonCISPtr

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the card capabilities
  
  @function name: SDDEVICE_GET_SDIO_CARD_CAPS
  @prototype: UINT8 SDDEVICE_GET_SDIO_CARD_CAPS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: 8-bit card capabilities register
 
  @notes: Implemented as a macro. Refer to SDIO spec for decoding.
  
  @see also: SDDEVICE_GET_CARD_FLAGS
  @see also: SDDEVICE_GET_SDIOCARD_CAPS
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIO_CARD_CAPS(pDev)     (pDev)->pHcd->CardProperties.SDIOCaps

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the card flags
  
  @function name: SDDEVICE_GET_CARD_FLAGS
  @prototype: CARD_INFO_FLAGS SDDEVICE_GET_CARD_FLAGS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: flags
 
  @notes: Implemented as a macro. 

  @example: Get card type:
        CARD_INFO_FLAGS flags;
        flags = SDDEVICE_GET_CARD_FLAGS(pDevice);
        switch(GET_CARD_TYPE(flags)) {
            case CARD_MMC: // Multi-media card
                ... 
            case CARD_SD:  // SD-Memory present
                ...
            case CARD_SDIO: // SDIO card present
                ...
            case CARD_COMBO: //SDIO card with SD
                ...
        }
        if (flags & CARD_SD_WP) {
            ...SD write protect on
        }
  
  @see also: SDDEVICE_GET_SDIO_CARD_CAPS
                
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_CARD_FLAGS(pDev)      (pDev)->pHcd->CardProperties.Flags

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the Relative Card Address register
  
  @function name: SDDEVICE_GET_CARD_RCA
  @prototype: UINT16 SDDEVICE_GET_CARD_RCA(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: register address
 
  @notes: Implemented as a macro. Refer to SDIO spec for decoding.
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_CARD_RCA(pDev)        (pDev)->pHcd->CardProperties.RCA

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get operational bus clock 
  
  @function name: SDDEVICE_GET_OPER_CLOCK
  @prototype: SD_BUSCLOCK_RATE SDDEVICE_GET_OPER_CLOCK(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: clock rate
 
  @notes: Implemented as a macro. Returns the current bus clock rate. 
          This may be lower than reported by the card due to Host Controller,
          Bus driver, or power management limitations.
  
  @see also: SDDEVICE_GET_MAX_CLOCK
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_OPER_CLOCK(pDev)      (pDev)->pHcd->CardProperties.OperBusClock

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get maximum bus clock 
  
  @function name: SDDEVICE_GET_MAX_CLOCK
  @prototype: SD_BUSCLOCK_RATE SDDEVICE_GET_MAX_CLOCK(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: clock rate
 
  @notes: To obtain the current maximum clock rate use SDDEVICE_GET_OPER_CLOCK().
          This rate my be lower than the host controllers maximum obtained using
          SDDEVICE_GET_MAX_CLOCK().
  
  @see also: SDDEVICE_GET_OPER_CLOCK
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_MAX_CLOCK(pDev)       (pDev)->pHcd->MaxClockRate

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get operational maximum block length.
  
  @function name: SDDEVICE_GET_OPER_BLOCK_LEN
  @prototype: UINT16 SDDEVICE_GET_OPER_BLOCK_LEN(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: block size in bytes
 
  @notes: Implemented as a macro. Returns the maximum current block length. 
          This may be lower than reported by the card due to Host Controller,
          Bus driver, or power management limitations.
  
  @see also: SDDEVICE_GET_MAX_BLOCK_LEN
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_OPER_BLOCK_LEN(pDev)  (pDev)->pHcd->CardProperties.OperBlockLenLimit

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get maximum block length.
  
  @function name: SDDEVICE_GET_MAX_BLOCK_LEN
  @prototype: UINT16 SDDEVICE_GET_MAX_BLOCK_LEN(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: block size in bytes
 
  @notes: Implemented as a macro. Use SDDEVICE_GET_OPER_BLOCK_LEN to obtain
          the current block length.
  
  @see also: SDDEVICE_GET_OPER_BLOCK_LEN
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_MAX_BLOCK_LEN(pDev)   (pDev)->pHcd->MaxBytesPerBlock

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get operational maximum block count.
  
  @function name: SDDEVICE_GET_OPER_BLOCKS
  @prototype: UINT16 SDDEVICE_GET_OPER_BLOCKS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: maximum number of blocks per transaction.
 
  @notes: Implemented as a macro. Returns the maximum current block count. 
          This may be lower than reported by the card due to Host Controller,
          Bus driver, or power management limitations.
  
  @see also: SDDEVICE_GET_MAX_BLOCK_LEN
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_OPER_BLOCKS(pDev)     (pDev)->pHcd->CardProperties.OperBlockCountLimit

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get maximum block count.
  
  @function name: SDDEVICE_GET_MAX_BLOCKS
  @prototype: UINT16 SDDEVICE_GET_MAX_BLOCKS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: maximum number of blocks per transaction.
 
  @notes: Implemented as a macro. Use SDDEVICE_GET_OPER_BLOCKS to obtain
          the current block count.
  
  @see also: SDDEVICE_GET_OPER_BLOCKS
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_MAX_BLOCKS(pDev)      (pDev)->pHcd->MaxBlocksPerTrans

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get applied slot voltage
  
  @function name: SDDEVICE_GET_SLOT_VOLTAGE_MASK
  @prototype: SLOT_VOLTAGE_MASK SDDEVICE_GET_SLOT_VOLTAGE_MASK(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: slot voltage mask
 
  @notes: This function returns the applied voltage on the slot. The voltage value is a 
          mask having the following values:
          SLOT_POWER_3_3V   
          SLOT_POWER_3_0V  
          SLOT_POWER_2_8V  
          SLOT_POWER_2_0V  
          SLOT_POWER_1_8V  
          SLOT_POWER_1_6V              
           
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SLOT_VOLTAGE_MASK(pDev)   (pDev)->pHcd->CardProperties.CardVoltage

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the Card Specific Data Register.
  
  @function name: SDDEVICE_GET_CARDCSD
  @prototype: PUINT8 SDDEVICE_GET_CARDCSD(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return:  UINT8 CardCSD[MAX_CARD_RESPONSE_BYTES] array of CSD data.
 
  @notes: Implemented as a macro.
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_CARDCSD(pDev)         (pDev)->pHcd->CardProperties.CardCSD

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the bus mode flags
  
  @function name: SDDEVICE_GET_BUSMODE_FLAGS
  @prototype: SD_BUSMODE_FLAGS SDDEVICE_GET_BUSMODE_FLAGS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: 
  
  @notes: Implemented as a macro.  This function returns the raw bus mode flags.  This
          is useful for function drivers that wish to override the bus clock without
          modifying the current bus mode.
  
  @see also: SDDEVICE_GET_BUSWIDTH
  @see also: SDCONFIG_BUS_MODE_CTRL
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_BUSMODE_FLAGS(pDev)  (pDev)->pHcd->CardProperties.BusMode


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the bus width.
  
  @function name: SDDEVICE_GET_BUSWIDTH
  @prototype: UINT8 SDDEVICE_GET_BUSWIDTH(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return:  bus width: SDCONFIG_BUS_WIDTH_SPI, SDCONFIG_BUS_WIDTH_1_BIT, SDCONFIG_BUS_WIDTH_4_BIT
   
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_IS_BUSMODE_SPI
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_BUSWIDTH(pDev)        SDCONFIG_GET_BUSWIDTH((pDev)->pHcd->CardProperties.BusMode)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Is bus in SPI mode.
  
  @function name: SDDEVICE_IS_BUSMODE_SPI
  @prototype: BOOL SDDEVICE_IS_BUSMODE_SPI(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return:  TRUE, SPI mode.
   
  @notes: Implemented as a macro.
  
  @see also: SDDEVICE_GET_BUSWIDTH
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_IS_BUSMODE_SPI(pDev) (SDDEVICE_GET_BUSWIDTH(pDev) == SDCONFIG_BUS_WIDTH_SPI)

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Send a request to a device.
  
  @function name: SDDEVICE_CALL_REQUEST_FUNC
  @prototype: SDIO_STATUS SDDEVICE_CALL_REQUEST_FUNC(PSDDEVICE pDevice, PSDREQUEST pRequest)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
  @input:  pRequest  - the request to be sent
 
  @output: none

  @return: SDIO_STATUS 
 
  @notes: Sends a request to the specified device. If the request is successfully sent, then
          the response flags can be checked to detemine the result of the request.
  
  @example: Example of sending a request to a device:
    PSDREQUEST  pReq = NULL;
    //allocate a request
    pReq = SDDeviceAllocRequest(pDevice);
    if (NULL == pReq) {
        return SDIO_STATUS_NO_RESOURCES;    
    }
    //initialize the request
    SDLIB_SetupCMD52Request(FuncNo, Address, Write, *pData, pReq);
    //send the request to the target
    status = SDDEVICE_CALL_REQUEST_FUNC(pDevice,pReq);
    if (!SDIO_SUCCESS(status)) {
        break;   
    }
    //check the request response (based on the request type)
    if (SD_R5_GET_RESP_FLAGS(pReq->Response) & SD_R5_ERRORS) {
        ...
    }
    if (!Write) {
            // store the byte
        *pData =  SD_R5_GET_READ_DATA(pReq->Response);
    }
    //free the request
    SDDeviceFreeRequest(pDevice,pReq);
    ...
                
  @see also: SDDeviceAllocRequest
  @see also: SDDEVICE_CALL_CONFIG_FUNC
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_CALL_REQUEST_FUNC(pDev,pReq)  (pDev)->pRequest((pDev),(pReq))

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Send configuration to a device.
  
  @function name: SDDEVICE_CALL_CONFIG_FUNC
  @prototype: SDIO_STATUS SDDEVICE_CALL_CONFIG_FUNC(PSDDEVICE pDevice, PSDCONFIG pConfigure)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
  @input:  pConfigure - configuration request
 
  @output: none

  @return: SDIO_STATUS 
 
  @notes: Sends a configuration request to the specified device. 
  
  @example: Example of sending a request to a device:
        SDCONFIG  configHdr; 
        SDCONFIG_FUNC_ENABLE_DISABLE_DATA fData;
        fData.EnableFlags = SDCONFIG_ENABLE_FUNC;
        fData.TimeOut = 500;
        SET_SDCONFIG_CMD_INFO(&configHdr, SDCONFIG_FUNC_ENABLE_DISABLE, fData, sizeof(fData));
        return SDDEVICE_CALL_CONFIG_FUNC(pDevice, &configHdr);
                
  @see also: SDLIB_IssueConfig
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_CALL_CONFIG_FUNC(pDev,pCfg)   (pDev)->pConfigure((pDev),(pCfg))

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Allocate a request structure.
  
  @function name: SDDeviceAllocRequest
  @prototype: PSDREQUEST SDDeviceAllocRequest(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: request pointer or NULL if not available.
 
  @notes:  This function must not be called in a non-schedulable (interrupts off) context.  
           Allocating memory on some OSes may block.
                
  @see also: SDDEVICE_CALL_REQUEST_FUNC
  @see also: SDDeviceFreeRequest
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDeviceAllocRequest(pDev)        (pDev)->AllocRequest((pDev))

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Free a request structure.
  
  @function name: SDDeviceFreeRequest
  @prototype: void SDDeviceFreeRequest(PSDDEVICE pDevice, PSDREQUEST pRequest)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
  @input:  pRequest  - request allocated by SDDeviceAllocRequest().
 
  @output: none

  @return: none
 
  @notes: This function must not be called in a non-schedulable (interrupts off) context.  
          Freeing memory on some OSes may block.
                
  @see also: SDDEVICE_CALL_REQUEST_FUNC
  @see also: SDDeviceAllocRequest
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDeviceFreeRequest(pDev,pReq)    (pDev)->FreeRequest((pDev),pReq) 

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Register an interrupt handler for a device.
  
  @function name: SDDEVICE_SET_IRQ_HANDLER
  @prototype: void SDDEVICE_SET_IRQ_HANDLER(PSDDEVICE pDevice, 
                                            void (*pIrqFunction)(PVOID pContext),
                                            PVOID pContext)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
  @input:  pIrqFunction  - the interrupt function to execute.
  @input:  pContext  - context value passed into interrupt routine.
 
  @output: none

  @return: none
 
  @notes: The registered routine will be called upon each card interrupt.
          The interrupt function should acknowledge the interrupt when it is
          ready to handle more interrupts using:
          SDLIB_IssueConfig(pDevice, SDCONFIG_FUNC_ACK_IRQ, NULL, 0);
          The interrupt handler can perform synchronous request calls.
                
  @see also: SDDEVICE_SET_ASYNC_IRQ_HANDLER
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_SET_IRQ_HANDLER(pDev,pFn,pContext)  \
{                                                    \
    (pDev)->pIrqFunction = (pFn);                    \
    (pDev)->IrqContext = (PVOID)(pContext);          \
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Register an asynchronous interrupt handler for a device.
  
  @function name: SDDEVICE_SET_ASYNC_IRQ_HANDLER
  @prototype: void SDDEVICE_SET_ASYNC_IRQ_HANDLER(PSDDEVICE pDevice, 
                                            void (*pIrqAsyncFunction)(PVOID pContext),
                                            PVOID pContext)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
  @input:  pIrqAsyncFunction  - the interrupt function to execute.
  @input:  pContext  - context value passed into interrupt routine.
 
  @output: none

  @return: none
 
  @notes: The registered routine will be called upon each card interrupt.
          The interrupt function should acknowledge the interrupt when it is
          ready to handle more interrupts using:
          SDLIB_IssueConfig(pDevice, SDCONFIG_FUNC_ACK_IRQ, NULL, 0);
          The interrupt handler can not perform any synchronous request calls.
          Using this call provides a faster interrupt dispatch, but limits all
          requests to asynchronous mode.
                
  @see also: SDDEVICE_SET_IRQ_HANDLER
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_SET_ASYNC_IRQ_HANDLER(pDev,pFn,pContext)  \
{                                                          \
    (pDev)->pIrqAsyncFunction = (pFn);                     \
    (pDev)->IrqAsyncContext = (PVOID)(pContext);           \
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the SDIO capabilities rgeister.
  
  @function name: SDDEVICE_GET_SDIOCARD_CAPS
  @prototype: UINT8 SDDEVICE_GET_SDIOCARD_CAPS(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return: SD capabilities
 
  @notes: See SD specification for decoding of these capabilities.
                
  @see also: SDDEVICE_GET_SDIO_CARD_CAPS
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SDIOCARD_CAPS(pDev) (pDev)->pHcd->CardProperties.SDIOCaps

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get HCD driver name
  
  @function name: SDDEVICE_GET_HCDNAME
  @prototype: PTEXT SDDEVICE_GET_HCDNAME(PSDDEVICE pDevice)
  @category: PD_Reference
  
  @input:  pDevice   - the target device for this request
 
  @output: none

  @return:  pointer to a string containing the name of the underlying HCD
   
  @notes: Implemented as a macro.
  
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_HCDNAME(pDev)  (pDev)->pHcd->pName


#define SDDEVICE_CALL_IRQ_HANDLER(pDev)       (pDev)->pIrqFunction((pDev)->IrqContext)
#define SDDEVICE_CALL_IRQ_ASYNC_HANDLER(pDev) (pDev)->pIrqAsyncFunction((pDev)->IrqAsyncContext)


#define SDDEVICE_SET_SDIO_FUNCNO(pDev,Num) (pDev)->pId[0].SDIO_FunctionNo = (Num)
#define SDDEVICE_IS_CARD_REMOVED(pDev)     ((pDev)->pHcd->CardProperties.CardState & \
                                             CARD_STATE_REMOVED)


typedef enum _SDHCD_IRQ_PROC_STATE { 
    SDHCD_IDLE = 0,
    SDHCD_IRQ_PENDING = 1,
    SDHCD_IRQ_HELPER  = 2 
}SDHCD_IRQ_PROC_STATE, *PSDHCD_IRQ_PROC_STATE;

/* host controller bus driver registration structure */
typedef struct _SDHCD {
    CT_VERSION_CODE Version;    /* version code of the SDIO stack */
    SDLIST  SDList;             /* internal use list*/
    PTEXT   pName;              /* name of registering host/slot driver */
    UINT32  Attributes;         /* attributes of host controller */
    UINT16  MaxBytesPerBlock;   /* max bytes per block */
    UINT16  MaxBlocksPerTrans;  /* max blocks per transaction */
    SD_SLOT_CURRENT  MaxSlotCurrent;  /* max current per slot in milli-amps */
    UINT8   SlotNumber;         /* sequential slot number for this HCD, set by bus driver */
    SD_BUSCLOCK_RATE    MaxClockRate;         /* max clock rate in hz */
    SLOT_VOLTAGE_MASK   SlotVoltageCaps;      /* slot voltage capabilities */
    SLOT_VOLTAGE_MASK   SlotVoltagePreferred; /* preferred slot voltage */
    PVOID   pContext;                         /* host controller driver use data   */
    SDIO_STATUS (*pRequest)(struct _SDHCD *pHcd); 
                                /* get/set configuration */
    SDIO_STATUS (*pConfigure)(struct _SDHCD *pHcd, PSDCONFIG pConfig); 
        /* everything below this line is for bus driver use */
    OS_SEMAPHORE    ConfigureOpsSem;    /* semaphore to make specific configure ops atomic, internal use */
    OS_CRITICALSECTION HcdCritSection;  /* critical section to protect hcd data structures (internal use) */
    SDREQUESTQUEUE  RequestQueue;       /* request queue, internal use */
    PSDREQUEST      pCurrentRequest;    /* current request we are working on */
    CARD_PROPERTIES CardProperties;     /* properties for the currently inserted card*/
    OSKERNEL_HELPER SDIOIrqHelper;      /* synch IRQ helper, internal use */
    SDDEVICE        *pPseudoDev;        /* pseudo device used for initialization (internal use) */
    UINT8           PendingHelperIrqs;  /* IRQ helper pending IRQs */
    UINT8           PendingIrqAcks;     /* pending IRQ acks from function drivers */
    UINT8           IrqsEnabled;        /* current irq enabled mask */
    SDHCD_IRQ_PROC_STATE IrqProcState;  /* irq processing state */
    POS_DEVICE      pDevice;            /* device registration with base system */
    SD_SLOT_CURRENT SlotCurrentAllocated; /* slot current allocated (internal use ) */
    ATOMIC_FLAGS    HcdFlags;             /* HCD Flags */
#define HCD_REQUEST_CALL_BIT  0
#define HCD_IRQ_NO_PEND_CHECK 1           /* HCD flag to bypass interrupt pending register
                                             check, typically done on single function cards */
    SDREQUESTQUEUE  CompletedRequestQueue; /* completed request queue, internal use */
    PSDDMA_DESCRIPTION pDmaDescription; /* description of HCD's DMA capabilities */  
    POS_MODULE         pModule;         /* OS-specific module information */
    INT                Recursion;       /* recursion level */
    PVOID              Reserved1;
    PVOID              Reserved2;
}SDHCD, *PSDHCD;

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get a pointer to the HCD's DMA description

  @function name: SDGET_DMA_DESCRIPTION
  @prototype: PSDDMA_DESCRIPTION SDGET_DMA_DESCRIPTION(PSDDEVICE pDevice) 
  @category: PD_Reference
 
  @input:  pDevice - device structure
           
  @return: PSDDMA_DESCRIPTION or NULL if no DMA support
 
  @notes: Implemented as a macro. 
           
  @example: getting the current request: 
          PSDDMA_DESCRIPTION pDmaDescrp = SDGET_DMA_DESCRIPTION(pDevice);
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDGET_DMA_DESCRIPTION(pDevice)     (pDevice)->pHcd->pDmaDescription 


/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
  @function: Get the logical slot number the device is assigned to.

  @function name: SDDEVICE_GET_SLOT_NUMBER
  @prototype: UINT8 SDDEVICE_GET_SLOT_NUMBER(PSDDEVICE pDevice) 
  @category: PD_Reference
 
  @input:  pDevice - device structure
           
  @return: unsigned number representing the slot number
 
  @notes: Implemented as a macro. This value is unique for each physical slot in the system
          and assigned by the bus driver. Devices on a multi-function card will share the same
          slot number.
           
  @example: getting the slot number: 
          UINT8 thisSlot = SDDEVICE_GET_SLOT_NUMBER(pDevice);
        
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
#define SDDEVICE_GET_SLOT_NUMBER(pDevice) (pDevice)->pHcd->SlotNumber

/* for function use */
SDIO_STATUS SDIO_RegisterFunction(PSDFUNCTION pFunction);
SDIO_STATUS SDIO_UnregisterFunction(PSDFUNCTION pFunction);

#include "sdio_hcd_defs.h" 
#endif /* __SDIO_BUSDRIVER_H___ */
