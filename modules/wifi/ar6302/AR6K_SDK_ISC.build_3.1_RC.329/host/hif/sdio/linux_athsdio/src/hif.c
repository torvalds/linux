//------------------------------------------------------------------------------
// <copyright file="hif.c" company="Atheros">
//    Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
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
// HIF layer reference implementation for Atheros SDIO stack
//
// Author(s): ="Atheros"
//==============================================================================
#include "hif_internal.h"

void hif_register_tbl_attach(A_UINT32 hif_type);

#ifdef DEBUG

ATH_DEBUG_INSTANTIATE_MODULE_VAR(hif,
                                 "hif",
                                 "(Atheros SDIO) Host Interconnect Framework",
                                 ATH_DEBUG_MASK_DEFAULTS,
                                 0,
                                 NULL);
                                 
#endif

/* ------ Static Variables ------ */

/* ------ Global Variable Declarations ------- */
SD_PNP_INFO Ids[] = {
#ifdef AR6003_HEADERS_DEF
    {
        .SDIO_ManufacturerID = MANUFACTURER_ID_AR6003_BASE | 0x0,
        .SDIO_ManufacturerCode = MANUFACTURER_CODE,
        .SDIO_FunctionClass = FUNCTION_CLASS,
        .SDIO_FunctionNo = 1
    },
    {
        .SDIO_ManufacturerID = MANUFACTURER_ID_AR6003_BASE | 0x1,
        .SDIO_ManufacturerCode = MANUFACTURER_CODE,
        .SDIO_FunctionClass = FUNCTION_CLASS,
        .SDIO_FunctionNo = 1
    },
#endif
#ifdef MCKINLEY_HEADERS_DEF
    {
        .SDIO_ManufacturerID = MANUFACTURER_ID_MCKINLEY_BASE | 0x0,
        .SDIO_ManufacturerCode = MANUFACTURER_CODE,
        .SDIO_FunctionClass = FUNCTION_CLASS,
        .SDIO_FunctionNo = 1
    },
#endif
    {
    }                      //list is null termintaed
};

TARGET_FUNCTION_CONTEXT FunctionContext = {
    .function.Version    = CT_SDIO_STACK_VERSION_CODE,
    .function.pName      = "sdio_wlan",
    .function.MaxDevices = 1,
    .function.NumDevices = 0,
    .function.pIds       = Ids,
    .function.pProbe     = hifDeviceInserted,
    .function.pRemove    = hifDeviceRemoved,
    .function.pSuspend   = NULL,
    .function.pResume    = NULL,
    .function.pWake      = NULL,
    .function.pContext   = &FunctionContext,
};

HIF_DEVICE hifDevice[HIF_MAX_DEVICES];
OSDRV_CALLBACKS osdrvCallbacks;
BUS_REQUEST busRequest[BUS_REQUEST_MAX_NUM];
static BUS_REQUEST *s_busRequestFreeQueue = NULL;
OS_CRITICALSECTION lock;
extern A_UINT32 onebitmode;
extern A_UINT32 busspeedlow;
extern A_UINT32 irqprocmode;
extern A_UINT32 nohifscattersupport;
extern unsigned int rtc_reset_only_on_exit;

static BUS_REQUEST *hifAllocateBusRequest(void);
static void hifFreeBusRequest(BUS_REQUEST *busrequest);
static THREAD_RETURN insert_helper_func(POSKERNEL_HELPER pHelper);
static void ResetAllCards(void);

/* ------ Functions ------ */
A_STATUS HIFInit(OSDRV_CALLBACKS *callbacks)
{
    SDIO_STATUS status;
    DBG_ASSERT(callbacks != NULL);

    A_REGISTER_MODULE_DEBUG_INFO(hif);

    /* store the callback handlers */
    osdrvCallbacks = *callbacks;    
    CriticalSectionInit(&lock);

    /* Register with bus driver core */
    status = SDIO_RegisterFunction(&FunctionContext.function);
    DBG_ASSERT(SDIO_SUCCESS(status));

    return SDIO_SUCCESS(status) ? A_OK : A_ERROR;
}

A_STATUS
HIFReadWrite(HIF_DEVICE *device,
             A_UINT32 address,
             A_UCHAR *buffer,
             A_UINT32 length,
             A_UINT32 request,
             void *context)
{
    A_UINT8 rw;
    A_UINT8 mode;
    A_UINT8 funcNo;
    A_UINT8 opcode;
    A_UINT16 count;
    SDREQUEST *sdrequest;
    SDIO_STATUS sdiostatus;
    BUS_REQUEST *busrequest;
    A_STATUS    status = A_OK;

    DBG_ASSERT(device != NULL);
    DBG_ASSERT(device->handle != NULL);

    AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Device: %p\n", device));

    do {
        busrequest = hifAllocateBusRequest();
        if (busrequest == NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR, ("HIF Unable to allocate bus request\n"));
            status = A_NO_RESOURCE;
            break;
        }

        sdrequest = busrequest->request;
        busrequest->context = context;

        sdrequest->pDataBuffer = buffer;
        if (request & HIF_SYNCHRONOUS) {
            sdrequest->Flags = SDREQ_FLAGS_RESP_SDIO_R5 | SDREQ_FLAGS_DATA_TRANS;
            sdrequest->pCompleteContext = NULL;
            sdrequest->pCompletion = NULL;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Execution mode: Synchronous\n"));
        } else if (request & HIF_ASYNCHRONOUS) {
            sdrequest->Flags = SDREQ_FLAGS_RESP_SDIO_R5 | SDREQ_FLAGS_DATA_TRANS |
                               SDREQ_FLAGS_TRANS_ASYNC;
            busrequest->hifDevice = device;                   
            sdrequest->pCompleteContext = busrequest;
            sdrequest->pCompletion = hifRWCompletionHandler;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Execution mode: Asynchronous\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid execution mode: 0x%08x\n", request));
            status = A_EINVAL;
            break;
        }

        if (request & HIF_EXTENDED_IO) {
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Command type: CMD53\n"));
            sdrequest->Command = CMD53;
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid command type: 0x%08x\n", request));
            status = A_EINVAL;
            break;
        }

        if (request & HIF_BLOCK_BASIS) {
            mode = CMD53_BLOCK_BASIS;
            sdrequest->BlockLen = HIF_MBOX_BLOCK_SIZE;
            sdrequest->BlockCount = length / HIF_MBOX_BLOCK_SIZE;
            count = sdrequest->BlockCount;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                            ("Block mode (BlockLen: %d, BlockCount: %d)\n",
                            sdrequest->BlockLen, sdrequest->BlockCount));
        } else if (request & HIF_BYTE_BASIS) {
            mode = CMD53_BYTE_BASIS;
            sdrequest->BlockLen = length;
            sdrequest->BlockCount = 1;
            count = sdrequest->BlockLen;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                            ("Byte mode (BlockLen: %d, BlockCount: %d)\n",
                            sdrequest->BlockLen, sdrequest->BlockCount));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid data mode: 0x%08x\n", request));
            status = A_EINVAL;
            break;
        }

#if 0
        /* useful for checking register accesses */
        if (length & 0x3) {
            A_PRINTF(KERN_ALERT"HIF (%s) is not a multiple of 4 bytes, addr:0x%X, len:%d\n",
                                request & HIF_WRITE ? "write":"read", address, length);
        }
#endif

        if (request & HIF_WRITE) {
            if ((address >= HIF_MBOX_START_ADDR(0)) &&
                (address <= HIF_MBOX_END_ADDR(3)))
            {
    
                DBG_ASSERT(length <= HIF_MBOX_WIDTH);
    
                /*
                 * Mailbox write. Adjust the address so that the last byte
                 * falls on the EOM address.
                 */
                address += (HIF_MBOX_WIDTH - length);
            }
        }



        if (request & HIF_WRITE) {
            rw = CMD53_WRITE;
            sdrequest->Flags |= SDREQ_FLAGS_DATA_WRITE;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Direction: Write\n"));
        } else if (request & HIF_READ) {
            rw = CMD53_READ;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Direction: Read\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid direction: 0x%08x\n", request));
            status = A_EINVAL;
            break;
        }

        if (request & HIF_FIXED_ADDRESS) {
            opcode = CMD53_FIXED_ADDRESS;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Address mode: Fixed\n"));
        } else if (request & HIF_INCREMENTAL_ADDRESS) {
            opcode = CMD53_INCR_ADDRESS;
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Address mode: Incremental\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Invalid address mode: 0x%08x\n", request));
            status = A_EINVAL;
            break;
        }

        funcNo = SDDEVICE_GET_SDIO_FUNCNO(device->handle);
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Function number: %d\n", funcNo));
        SDIO_SET_CMD53_ARG(sdrequest->Argument, rw, funcNo,
                           mode, opcode, address, count);

        /* Send the command out */
        sdiostatus = SDDEVICE_CALL_REQUEST_FUNC(device->handle, sdrequest);

        if (!SDIO_SUCCESS(sdiostatus)) {
            status = A_ERROR;
        }

    } while (FALSE);

    if (A_FAILED(status) || (request & HIF_SYNCHRONOUS)) {
        if (busrequest != NULL) {
            hifFreeBusRequest(busrequest);
        }
    }

    if (A_FAILED(status) && (request & HIF_ASYNCHRONOUS)) {
            /* call back async handler on failure */
        device->htcCallbacks.rwCompletionHandler(context, status);
    }

    return status;
}

A_STATUS
HIFConfigureDevice(HIF_DEVICE *device, HIF_DEVICE_CONFIG_OPCODE opcode,
                   void *config, A_UINT32 configLen)
{
    A_UINT32 count;

    switch(opcode) {
        case HIF_DEVICE_GET_MBOX_BLOCK_SIZE:
            ((A_UINT32 *)config)[0] = HIF_MBOX0_BLOCK_SIZE;
            ((A_UINT32 *)config)[1] = HIF_MBOX1_BLOCK_SIZE;
            ((A_UINT32 *)config)[2] = HIF_MBOX2_BLOCK_SIZE;
            ((A_UINT32 *)config)[3] = HIF_MBOX3_BLOCK_SIZE;
            break;

        case HIF_DEVICE_GET_MBOX_ADDR:
            for (count = 0; count < 4; count ++) {
                ((A_UINT32 *)config)[count] = HIF_MBOX_START_ADDR(count);
            }     

            if (configLen >= sizeof(HIF_DEVICE_MBOX_INFO)) {    
                SetExtendedMboxWindowInfo(SDDEVICE_GET_SDIO_MANFID(device->handle),
                                          (HIF_DEVICE_MBOX_INFO *)config);
            }
              
            break;
        case HIF_DEVICE_GET_IRQ_PROC_MODE:
                /* the SDIO stack allows the interrupts to be processed either way, ASYNC or SYNC */
            *((HIF_DEVICE_IRQ_PROCESSING_MODE *)config) = irqprocmode;
            break;
        case HIF_CONFIGURE_QUERY_SCATTER_REQUEST_SUPPORT:
            if (nohifscattersupport) {
                return A_ERROR;    
            }
            return SetupHIFScatterSupport(device, (HIF_DEVICE_SCATTER_SUPPORT_INFO *)config);
        case HIF_DEVICE_GET_OS_DEVICE:
            if (SD_GET_HCD_OS_DEVICE(device->handle) == NULL) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("**** HCD OS device is NULL \n"));
                return A_ERROR;
            }
            ((HIF_DEVICE_OS_DEVICE_INFO *)config)->pOSDevice = SD_GET_HCD_OS_DEVICE(device->handle);
            break;    
        case HIF_DEVICE_DEBUG_BUS_STATE:            
            SDLIB_IssueConfig(device->handle,SDCONFIG_DUMP_HCD_STATE,NULL,0);        
            break;           
        default:
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                            ("Unsupported configuration opcode: %d\n", opcode));
            return A_ERROR;
    }

    return A_OK;
}

void
HIFShutDownDevice(HIF_DEVICE *device)
{
    SDIO_STATUS status;
    SDCONFIG_BUS_MODE_DATA busSettings;
    SDCONFIG_FUNC_ENABLE_DISABLE_DATA fData;

    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("rtc_reset_only_on_exit : %d\n",rtc_reset_only_on_exit));
    
    if (device != NULL) {
        DBG_ASSERT(device->handle != NULL);

        /* Remove the allocated current if any */
        status = SDLIB_IssueConfig(device->handle,
                                   SDCONFIG_FUNC_FREE_SLOT_CURRENT, NULL, 0);
        DBG_ASSERT(SDIO_SUCCESS(status));
    
        if (!rtc_reset_only_on_exit) {
            /* Disable the card */
            fData.EnableFlags = SDCONFIG_DISABLE_FUNC;
            fData.TimeOut = 1;
            status = SDLIB_IssueConfig(device->handle, SDCONFIG_FUNC_ENABLE_DISABLE,
                                       &fData, sizeof(fData));
            DBG_ASSERT(SDIO_SUCCESS(status));
        }

        /* Don't perform a soft I/O reset now
         * It will be done later from ResetALlCards during module cleanup
         */

        /*
         * WAR - Codetelligence driver does not seem to shutdown correctly in 1
         * bit mode. By default it configures the HC in the 4 bit. Its later in
         * our driver that we switch to 1 bit mode. If we try to shutdown, the
         * driver hangs so we revert to 4 bit mode, to be transparent to the
         * underlying bus driver.
         */
        if (onebitmode) {
            ZERO_OBJECT(busSettings);
            busSettings.BusModeFlags = SDDEVICE_GET_BUSMODE_FLAGS(device->handle);
            SDCONFIG_SET_BUS_WIDTH(busSettings.BusModeFlags,
                                   SDCONFIG_BUS_WIDTH_4_BIT);

            /* Issue config request to change the bus width to 4 bit */
            status = SDLIB_IssueConfig(device->handle, SDCONFIG_BUS_MODE_CTRL,
                                       &busSettings,
                                       sizeof(SDCONFIG_BUS_MODE_DATA));
            DBG_ASSERT(SDIO_SUCCESS(status));
        }

    } else {
        if (!rtc_reset_only_on_exit) {
                /* since we are unloading the driver anyways, reset all cards in case the SDIO card
                 * is externally powered and we are unloading the SDIO stack.  This avoids the problem when
                 * the SDIO stack is reloaded and attempts are made to re-enumerate a card that is already
                 * enumerated */
            ResetAllCards();
        }
        /* Unregister with bus driver core */
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                        ("Unregistering with the bus driver\n"));
        status = SDIO_UnregisterFunction(&FunctionContext.function);
        DBG_ASSERT(SDIO_SUCCESS(status));
    }
}

void
hifRWCompletionHandler(SDREQUEST *request)
{
    A_STATUS status;
    void *context;
    BUS_REQUEST *busrequest;
    HIF_DEVICE  *device;

    if (SDIO_SUCCESS(request->Status)) {
        status = A_OK;
    } else {
        status = A_ERROR;
    }

    DBG_ASSERT(status == A_OK);
    busrequest = (BUS_REQUEST *) request->pCompleteContext;
    context = (void *) busrequest->context;
    device = busrequest->hifDevice;
        /* free the request before calling the callback, in case the
         * callback submits another request, this guarantees that
         * there is at least 1 free request available everytime the callback
         * is invoked */
    hifFreeBusRequest(busrequest);
    device->htcCallbacks.rwCompletionHandler(context, status);
}

void
hifIRQHandler(void *context)
{
    A_STATUS status;
    HIF_DEVICE *device;

    device = (HIF_DEVICE *)context;
    AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Device: %p\n", device));
    status = device->htcCallbacks.dsrHandler(device->htcCallbacks.context);
    DBG_ASSERT(status == A_OK);
}


static void hifAssignTargetHeaders(A_UINT16 SDIO_ManufacturerID)
{
     switch (SDIO_ManufacturerID) {
         case MANUFACTURER_ID_AR6003_BASE: 
             hif_register_tbl_attach(HIF_TYPE_AR6003);
         break;

         case MANUFACTURER_ID_MCKINLEY_BASE: 
             hif_register_tbl_attach(HIF_TYPE_MCKINLEY);
         break;
         default: 
             hif_register_tbl_attach(HIF_TYPE_AR6003);
         break;
    }
}

BOOL
hifDeviceInserted(SDFUNCTION *function, SDDEVICE *handle)
{
    BOOL enabled;
    A_UINT8 data;
    A_UINT32 count;
    SDIO_STATUS status;
    A_UINT16 maxBlocks;
    A_UINT16 maxBlockSize;
    SDCONFIG_BUS_MODE_DATA busSettings;
    SDCONFIG_FUNC_ENABLE_DISABLE_DATA fData;
    TARGET_FUNCTION_CONTEXT *functionContext;
    SDCONFIG_FUNC_SLOT_CURRENT_DATA slotCurrent;
    SD_BUSCLOCK_RATE                currentBusClock;

    DBG_ASSERT(function != NULL);
    DBG_ASSERT(handle != NULL);

    functionContext =  (TARGET_FUNCTION_CONTEXT *)function->pContext;  

    /*
     * Issue commands to get the manufacturer ID and stuff and compare it
     * against the rev Id derived from the ID registered during the
     * initialization process. Report the device only in the case there
     * is a match. In the case od SDIO, the bus driver has already queried
     * these details so we just need to use their data structures to get the
     * relevant values. Infact, the driver has already matched it against
     * the Ids that we registered with it so we dont need to the step here.
     */
    /* Configure the SDIO Bus Width */
    if (onebitmode) {
        data = SDIO_BUS_WIDTH_1_BIT;
        status = SDLIB_IssueCMD52(handle, 0, SDIO_BUS_IF_REG, &data, 1, 1);
        if (!SDIO_SUCCESS(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Unable to set the bus width to 1 bit\n"));
            return FALSE;
        }
    }

    /* Get current bus flags */
    ZERO_OBJECT(busSettings);

    busSettings.BusModeFlags = SDDEVICE_GET_BUSMODE_FLAGS(handle);
    if (onebitmode) {
        SDCONFIG_SET_BUS_WIDTH(busSettings.BusModeFlags,
                               SDCONFIG_BUS_WIDTH_1_BIT);
    }

        /* get the current operating clock, the bus driver sets us up based
         * on what our CIS reports and what the host controller can handle
         * we can use this to determine whether we want to drop our clock rate
         * down */
    currentBusClock = SDDEVICE_GET_OPER_CLOCK(handle);
    busSettings.ClockRate = currentBusClock;

    hifAssignTargetHeaders(handle->pId[0].SDIO_ManufacturerID);

    AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,("HIF currently running at: %d MANFID:0x%X \n",
                                            currentBusClock, SDDEVICE_GET_SDIO_MANFID(handle)));

    if (busSettings.BusModeFlags & SDCONFIG_BUS_MODE_SD_HS) {
            /* operating in high speed mode */
        if (currentBusClock > (SDIO_CLOCK_HS_FREQUENCY_DEFAULT >> busspeedlow)) {
            busSettings.ClockRate = SDIO_CLOCK_HS_FREQUENCY_DEFAULT >> busspeedlow;
                /* drop out of HS mode */
            busSettings.BusModeFlags &= ~SDCONFIG_BUS_MODE_SD_HS;
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                            ("HIF overriding clock to %d , disabling HS \n",busSettings.ClockRate));
        }        
        
    } else {
            /* see if HIF wants to run at a lower clock speed, we may already be
             * at that lower clock speed */
        if (currentBusClock > (SDIO_CLOCK_FREQUENCY_DEFAULT >> busspeedlow)) {
            busSettings.ClockRate = SDIO_CLOCK_FREQUENCY_DEFAULT >> busspeedlow;
            AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                            ("HIF overriding clock to %d \n",busSettings.ClockRate));
        }
    }

    if ((SDDEVICE_GET_SDIO_MANFID(handle) & 
            MANUFACTURER_ID_AR6K_BASE_MASK) == MANUFACTURER_ID_AR6003_BASE) {
            /* for AR6003, enable 4-bit ASYNC I/O interrupts if we are running in 4-bit mode  */
        if (SDDEVICE_GET_BUSWIDTH(handle) == SDCONFIG_BUS_WIDTH_4_BIT) {               
            data = SDIO_IRQ_MODE_ASYNC_4BIT_IRQ;
                /* write to FUNC0 (CCCR) register space */
            status = SDLIB_IssueCMD52(handle, 0, CCCR_SDIO_IRQ_MODE_REG, &data, 1, TRUE);
            if (!SDIO_SUCCESS(status)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                                ("HIF: Unable to set ASYNC 4-bit IRQ mode\n"));
                return FALSE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_WARN,("HIF: ASYNC 4-bit IRQ enabled \n"));    
            }  
        }
    }

    /* Issue config request to override clock rate */
    status = SDLIB_IssueConfig(handle, SDCONFIG_FUNC_CHANGE_BUS_MODE, &busSettings,
                               sizeof(SDCONFIG_BUS_MODE_DATA));
    if (!SDIO_SUCCESS(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                        ("Unable to configure the host clock\n"));
        return FALSE;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                        ("Configured clock: %d, Maximum clock: %d\n",
                        busSettings.ActualClockRate,
                        SDDEVICE_GET_MAX_CLOCK(handle)));
    }

    /*
     * Check if the target supports block mode. This result of this check
     * can be used to implement the HIFReadWrite API.
     */
    if (SDDEVICE_GET_SDIO_FUNC_MAXBLKSIZE(handle)) {
        /* Limit block size to operational block limit or card function
           capability */
        maxBlockSize = min(SDDEVICE_GET_OPER_BLOCK_LEN(handle),
                           SDDEVICE_GET_SDIO_FUNC_MAXBLKSIZE(handle));

        /* check if the card support multi-block transfers */
        if (!(SDDEVICE_GET_SDIOCARD_CAPS(handle) & SDIO_CAPS_MULTI_BLOCK)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Byte basis only\n"));

            /* Limit block size to max byte basis */
            maxBlockSize =  min(maxBlockSize,
                                (A_UINT16)SDIO_MAX_LENGTH_BYTE_BASIS);
            maxBlocks = 1;
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Multi-block capable\n"));
            maxBlocks = SDDEVICE_GET_OPER_BLOCKS(handle);
            status = SDLIB_SetFunctionBlockSize(handle, HIF_MBOX_BLOCK_SIZE);
            if (!SDIO_SUCCESS(status)) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                                ("Failed to set block size. Err:%d\n", status));
                return FALSE;
            }
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                        ("Bytes Per Block: %d bytes, Block Count:%d \n",
                        maxBlockSize, maxBlocks));
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                        ("Function does not support Block Mode!\n"));
        return FALSE;
    }

    /* Allocate the slot current */
    status = SDLIB_GetDefaultOpCurrent(handle, &slotCurrent.SlotCurrent);
    if (SDIO_SUCCESS(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Allocating Slot current: %d mA\n",
                                slotCurrent.SlotCurrent));
        status = SDLIB_IssueConfig(handle, SDCONFIG_FUNC_ALLOC_SLOT_CURRENT,
                                   &slotCurrent, sizeof(slotCurrent));
        if (!SDIO_SUCCESS(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                            ("Failed to allocate slot current %d\n", status));
            return FALSE;
        }
    }

        /* if we are a single function device, the bus driver can just call our IRQ handler
         * without checking the common INT_PENDING register */
    SDLIB_IssueConfig(handle,
                      SDCONFIG_FUNC_NO_IRQ_PEND_CHECK,
                      NULL,
                      0);
                                   
    /* Enable the I/O function */
    count = 0;
    enabled = FALSE;
    fData.TimeOut = 1;
    fData.EnableFlags = SDCONFIG_ENABLE_FUNC;
    while ((count++ < SDWLAN_ENABLE_DISABLE_TIMEOUT) && !enabled)
    {
        status = SDLIB_IssueConfig(handle, SDCONFIG_FUNC_ENABLE_DISABLE,
                                   &fData, sizeof(fData));
        if (!SDIO_SUCCESS(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                            ("Attempting to enable the card again\n"));
            continue;
        }

        /* Mark the status as enabled */
        enabled = TRUE;
    }

    /* Check if we were succesful in enabling the target */
    if (!enabled) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERROR,
                        ("Failed to communicate with the target\n"));
        return FALSE;
    }

    /* Allocate the bus requests to be used later */
    A_MEMZERO(busRequest, sizeof(busRequest));
    for (count = 0; count < BUS_REQUEST_MAX_NUM; count ++) {
        if ((busRequest[count].request = SDDeviceAllocRequest(handle)) == NULL){
            AR_DEBUG_PRINTF(ATH_DEBUG_ERROR, ("Unable to allocate memory\n"));
            /* TODO: Free the memory that has already been allocated */
            return FALSE;
        }
        hifFreeBusRequest(&busRequest[count]);

        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                ("0x%08x = busRequest[%d].request = 0x%08x\n",
				(unsigned int) &busRequest[count], count,
				(unsigned int) busRequest[count].request));
    }

    {
        /* Add a device handle only when we claim the device */
        HIF_DEVICE *device;
    
        device = addHifDevice(handle);
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Device: %p\n", device));
        DL_LIST_INIT(&device->ScatterReqHead);
        /* Schedule a worker to handle device inserted, this is a temporary workaround
         * to fix a deadlock if the device fails to intialize in the insertion handler
         * The failure causes the instance to shutdown the HIF layer and unregister the
         * function driver within the busdriver probe context which can deadlock
         *
         * NOTE: we cannot use the default work queue because that would block
         * SD bus request processing for all synchronous I/O. We must use a kernel
         * thread that is creating using the helper library.
         * */

        if (SDIO_SUCCESS(SDLIB_OSCreateHelper(&device->insert_helper,
                             insert_helper_func,
                             device))) {
            device->helper_started = TRUE;
        }
    }

    return TRUE;
}

static THREAD_RETURN insert_helper_func(POSKERNEL_HELPER pHelper)
{
        /* Inform HTC */
    if ((osdrvCallbacks.deviceInsertedHandler(osdrvCallbacks.context,SD_GET_OS_HELPER_CONTEXT(pHelper))) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_TRACE, ("Device rejected\n"));
    }

    return 0;
}

void
HIFAckInterrupt(HIF_DEVICE *device)
{
    SDIO_STATUS status;
    DBG_ASSERT(device != NULL);
    DBG_ASSERT(device->handle != NULL);

    /* Acknowledge our function IRQ */
    status = SDLIB_IssueConfig(device->handle, SDCONFIG_FUNC_ACK_IRQ,
                               NULL, 0);
    DBG_ASSERT(SDIO_SUCCESS(status));
}

void
HIFUnMaskInterrupt(HIF_DEVICE *device)
{
    SDIO_STATUS status;

    DBG_ASSERT(device != NULL);
    DBG_ASSERT(device->handle != NULL);

    /* Register the IRQ Handler */
    SDDEVICE_SET_IRQ_HANDLER(device->handle, hifIRQHandler, device);

    /* Unmask our function IRQ */
    status = SDLIB_IssueConfig(device->handle, SDCONFIG_FUNC_UNMASK_IRQ,
                               NULL, 0);
    DBG_ASSERT(SDIO_SUCCESS(status));
}

void HIFMaskInterrupt(HIF_DEVICE *device)
{
    SDIO_STATUS status;
    DBG_ASSERT(device != NULL);
    DBG_ASSERT(device->handle != NULL);

    /* Mask our function IRQ */
    status = SDLIB_IssueConfig(device->handle, SDCONFIG_FUNC_MASK_IRQ,
                               NULL, 0);
    DBG_ASSERT(SDIO_SUCCESS(status));

    /* Unregister the IRQ Handler */
    SDDEVICE_SET_IRQ_HANDLER(device->handle, NULL, NULL);
}

static BUS_REQUEST *hifAllocateBusRequest(void)
{
    BUS_REQUEST *busrequest;

    /* Acquire lock */
    CriticalSectionAcquire(&lock);

    /* Remove first in list */
    if((busrequest = s_busRequestFreeQueue) != NULL)
    {
        s_busRequestFreeQueue = busrequest->next;
    }

    /* Release lock */
    CriticalSectionRelease(&lock);

    return busrequest;
}

static void
hifFreeBusRequest(BUS_REQUEST *busrequest)
{
    DBG_ASSERT(busrequest != NULL);

    /* Acquire lock */
    CriticalSectionAcquire(&lock);

    /* Insert first in list */
    busrequest->next = s_busRequestFreeQueue;
    s_busRequestFreeQueue = busrequest;
    /* Release lock */
    CriticalSectionRelease(&lock);
}

void
hifDeviceRemoved(SDFUNCTION *function, SDDEVICE *handle)
{
    HIF_DEVICE *device;
    A_UINT32 count;
    
    DBG_ASSERT(function != NULL);
    DBG_ASSERT(handle != NULL);

    device = getHifDevice(handle);
                            
    if (device->claimedContext != NULL) {
            /* device was claimed, call the removal handler */
        osdrvCallbacks.deviceRemovedHandler(device->claimedContext, device);
    }

        /* cleanup the helper thread */
    if (device->helper_started) {
        SDLIB_OSDeleteHelper(&device->insert_helper);
        device->helper_started = FALSE;
    }

    /* Free the bus requests */
    for (count = 0; count < BUS_REQUEST_MAX_NUM; count ++) {
        if (busRequest[count].request != NULL) {
            SDDeviceFreeRequest(device->handle, busRequest[count].request);
            busRequest[count].request = NULL;
        }
    }
    /* Clean up the queue */
    s_busRequestFreeQueue = NULL;
    
    CleanupHIFScatterResources(device);
        
    delHifDevice(handle);  
}

HIF_DEVICE *
addHifDevice(SDDEVICE *handle)
{
    DBG_ASSERT(handle != NULL);
    hifDevice[0].handle = handle;
    return &hifDevice[0];
}

HIF_DEVICE *
getHifDevice(SDDEVICE *handle)
{
    DBG_ASSERT(handle != NULL);
    return &hifDevice[0];
}

void
delHifDevice(SDDEVICE *handle)
{
    DBG_ASSERT(handle != NULL);
    hifDevice[0].handle = NULL;
}

static void ResetAllCards(void)
{
    UINT8       data;
    SDIO_STATUS status;
    int         i;

    data = SDIO_IO_RESET;

    /* set the I/O CARD reset bit:
     * NOTE: we are exploiting a "feature" of the SDIO core that resets the core when you
     * set the RES bit in the SDIO_IO_ABORT register.  This bit however "normally" resets the
     * I/O functions leaving the SDIO core in the same state (as per SDIO spec).
     * In this design, this reset can be used to reset the SDIO core itself */
    for (i = 0; i < HIF_MAX_DEVICES; i++) {
        if (hifDevice[i].handle != NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_TRACE,
                        ("Issuing I/O Card reset for instance: %d \n",i));
                /* set the I/O Card reset bit */
            status = SDLIB_IssueCMD52(hifDevice[i].handle,
                                      0,                    /* function 0 space */
                                      SDIO_IO_ABORT_REG,
                                      &data,
                                      1,                    /* 1 byte */
                                      TRUE);                /* write */
        }
    }

}

void HIFClaimDevice(HIF_DEVICE  *device, void *context)
{
    device->claimedContext = context;   
}

void HIFReleaseDevice(HIF_DEVICE  *device)
{
    device->claimedContext = NULL;    
}

A_STATUS HIFAttachHTC(HIF_DEVICE *device, HTC_CALLBACKS *callbacks)
{
    if (device->htcCallbacks.context != NULL) {
            /* already in use! */
        return A_ERROR;    
    }
    device->htcCallbacks = *callbacks; 
    return A_OK;
}

void HIFDetachHTC(HIF_DEVICE *device)
{
    A_MEMZERO(&device->htcCallbacks,sizeof(device->htcCallbacks));
}

/*
 * This should be moved to AR6K HTC layer.
 */
A_STATUS hifWaitForPendingRecv(HIF_DEVICE *device)
{
    return A_OK;
}






