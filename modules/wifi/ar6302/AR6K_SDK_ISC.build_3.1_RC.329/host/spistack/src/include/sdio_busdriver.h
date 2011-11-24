//------------------------------------------------------------------------------
// <copyright file="sdio_busdriver.h" company="Atheros">
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
#ifndef __SDIO_BUSDRIVER_H___
#define __SDIO_BUSDRIVER_H___

typedef UINT8      CT_VERSION_CODE;
#define CT_SDIO_STACK_VERSION_CODE ((CT_VERSION_CODE)0x27)   /* version code that must be set in various structures */
#define CT_SDIO_STACK_VERSION_MAJOR(v) (((v) & 0xF0) >> 4)
#define CT_SDIO_STACK_VERSION_MINOR(v) (((v) & 0x0F))
#define SET_SDIO_STACK_VERSION(p) (p)->Version = CT_SDIO_STACK_VERSION_CODE
#define GET_SDIO_STACK_VERSION(p) (p)->Version
#define GET_SDIO_STACK_VERSION_MAJOR(p) CT_SDIO_STACK_VERSION_MAJOR(GET_SDIO_STACK_VERSION(p))
#define GET_SDIO_STACK_VERSION_MINOR(p) CT_SDIO_STACK_VERSION_MINOR(GET_SDIO_STACK_VERSION(p))
#include "sdlist.h"

/* card flags */
typedef UINT16      CARD_INFO_FLAGS;
#define CARD_RAW        0x0008    /* Raw card */
#define CARD_TYPE_MASK  0x000F    /* card type mask */
#define CARD_PSEUDO     0x0020    /* pseudo card (internal use) */
#define GET_CARD_TYPE(flags) ((flags) & CARD_TYPE_MASK)

/* bus mode and clock rate */
typedef UINT32  SD_BUSCLOCK_RATE;       /* clock rate in hz */
typedef UINT16  SD_BUSMODE_FLAGS; 

/* plug and play information for SD cards */
typedef struct _SD_PNP_INFO {
    CARD_INFO_FLAGS CardFlags;     /* card flags */   
    UINT8     FunctionNo;                                                      		
}SD_PNP_INFO, *PSD_PNP_INFO;

#define IS_LAST_SDPNPINFO_ENTRY(id) \
     ((id)->CardFlags == 0)
     
/* card properties */
typedef struct _CARD_PROPERTIES {
    UINT8              IOFnCount;      /* number of I/O functions */
    CARD_INFO_FLAGS    Flags;          /* card flags */
    SD_BUSCLOCK_RATE   OperBusClock;   /* operational bus clock (based on HCD limit)*/
    UINT16             OperBlockLenLimit;
    UINT16             OperBlockCountLimit;
    SD_BUSMODE_FLAGS   BusMode;        /* current card bus mode */
    UINT8              CardState;      /* card state flags */
#define CARD_STATE_REMOVED 0x01
}CARD_PROPERTIES, *PCARD_PROPERTIES;

/* SDREQUEST request flags */
typedef UINT32 SDREQUEST_FLAGS;
/* write operation */
#define SDREQ_FLAGS_DATA_WRITE         0x8000
/* has data (read or write) */
#define SDREQ_FLAGS_DATA_TRANS         0x4000
/* transfer should be handled asynchronously */
#define SDREQ_FLAGS_TRANS_ASYNC        0x1000
/* flag indicating that the data buffer meets HCD's DMA restrictions   */
#define SDREQ_FLAGS_DATA_DMA           0x0010 
/* indicate to the host that this is a raw request */
#define SDREQ_FLAGS_RAW                0x00020000

#define SDREQ_FLAGS_UNUSED1               0x00200000
#define SDREQ_FLAGS_UNUSED2               0x00400000
#define SDREQ_FLAGS_UNUSED3               0x00800000
#define SDREQ_FLAGS_UNUSED4               0x01000000
#define SDREQ_FLAGS_UNUSED5               0x02000000

/* the following flags are internal use only */
#define SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE 0x0100
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

#define IS_SDREQ_WRITE_DATA(flags)     ((flags) & SDREQ_FLAGS_DATA_WRITE)
#define IS_SDREQ_DATA_TRANS(flags)     ((flags) & SDREQ_FLAGS_DATA_TRANS)
#define IS_SDREQ_RAW(flags)            ((flags) & SDREQ_FLAGS_RAW) 
#define IS_SDREQ_FORCE_DEFERRED_COMPLETE(flags) ((flags) & SDREQ_FLAGS_FORCE_DEFERRED_COMPLETE)

struct _SDREQUEST;
struct _SDFUNCTION;

typedef void (*PSDEQUEST_COMPLETION)(struct _SDREQUEST *);

#define MAX_SDREQUEST_PARAMS 6

typedef union _SDREQUEST_PARAM {
    UINT16  As16bit[2];
    UINT32  As32bit;    
}SDREQUEST_PARAM, *PSDREQUEST_PARAM;

/* defines requests for the RAW-mode API */
typedef struct _SDREQUEST {
    SDLIST  SDList;             /* internal use list*/
    SDREQUEST_FLAGS Flags;      /* request flags */
    ATOMIC_FLAGS InternalFlags; /* internal use flags */
    SDREQUEST_PARAM Parameters[MAX_SDREQUEST_PARAMS]; /* opaque parameters passed from FD to HCD */    
    UINT16  DescriptorCount;    /* number of DMA descriptor entries in pDataBuffer if DMA */
    PVOID   pDataBuffer;        /* starting address of buffer (or ptr to PSDDMA_DESCRIPTOR*/
    UINT32  DataRemaining;      /* number of bytes remaining in the transfer (internal use) */
    PVOID   pHcdContext;        /* internal use context */
    PSDEQUEST_COMPLETION pCompletion; /* function driver completion routine */
    PVOID   pCompleteContext;   /* function driver completion context */
    SDIO_STATUS Status;         /* completion status */
    struct _SDFUNCTION* pFunction; /* function driver that generated request (internal use)*/
    INT     RetryCount;          /* number of times to retry on error, non-data cmds only */
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

/* custom hcd commands */
#define SDCONFIG_GET_HOST_CUSTOM   (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_GET  | 300)
#define SDCONFIG_PUT_HOST_CUSTOM   (SDCONFIG_FLAGS_HC_CONFIG | SDCONFIG_FLAGS_DATA_PUT  | 301)

/* function commands */
#define SDCONFIG_FUNC_UNMASK_IRQ             (SDCONFIG_FLAGS_DATA_NONE | 21)
#define SDCONFIG_FUNC_MASK_IRQ               (SDCONFIG_FLAGS_DATA_NONE | 22)
#define SDCONFIG_FUNC_ACK_IRQ                (SDCONFIG_FLAGS_DATA_NONE | 23)

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
    SD_PNP_INFO pId[1];             /* id of this device  */
    OS_PNPDEVICE Device;            /* device registration with base system */
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
   
    .. set params...
    
    //send the request to the target
    status = SDDEVICE_CALL_REQUEST_FUNC(pDevice,pReq);
    if (!SDIO_SUCCESS(status)) {
        break;   
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


#define SDDEVICE_SET_SDIO_FUNCNO(pDev,Num) (pDev)->pId[0].FunctionNo = (Num)
#define SDDEVICE_GET_SDIO_FUNCNO(pDev)     (pDev)->pId[0].FunctionNo
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
    UINT8   SlotNumber;         /* sequential slot number for this HCD, set by bus driver */
    SD_BUSCLOCK_RATE    MaxClockRate;         /* max clock rate in hz */
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
