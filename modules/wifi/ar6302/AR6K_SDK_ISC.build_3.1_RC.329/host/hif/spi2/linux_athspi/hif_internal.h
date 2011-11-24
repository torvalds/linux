/* 
 * @file: hif_internal.h
 * 
 * @abstract: spi mode HIF layer definitions
 * 
 * 
 * @notice: Copyright (c) 2004-2006 Atheros Communications Inc.
 * 
 * 
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
 * 
 */

#include "a_config.h"
#include "ctsystem.h"
#include "sdio_busdriver.h"
#include "sdio_lib.h"
#include "ath_spi_hcd_if.h"
#include "athdefs.h"
#include "a_types.h"
#include "a_osapi.h"
#include "hif.h"

#define HIF_MBOX_BLOCK_SIZE                4  /* pad bytes to a full WORD on SPI */
#define HIF_MBOX_BASE_ADDR                 0x800
#define HIF_MBOX_WIDTH                     0x800
#define HIF_MBOX0_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE
#define HIF_MBOX1_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE
#define HIF_MBOX2_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE
#define HIF_MBOX3_BLOCK_SIZE               HIF_MBOX_BLOCK_SIZE

#define SPI_CLOCK_FREQUENCY_DEFAULT        24000000
#define SPI_CLOCK_FREQUENCY_REDUCED        12000000

#define HIF_MBOX_START_ADDR(mbox)                        \
    HIF_MBOX_BASE_ADDR + mbox * HIF_MBOX_WIDTH

#define HIF_MBOX_END_ADDR(mbox)	                         \
    HIF_MBOX_START_ADDR(mbox) + HIF_MBOX_WIDTH - 1

#define BUS_REQUEST_MAX_NUM_FORDATA                32
#define BUS_REQUEST_MAX_NUM_TOTAL                  40


typedef struct target_function_context {
    SDFUNCTION           function; /* function description of the bus driver */
    OS_SEMAPHORE         instanceSem; /* instance lock. Unused */
    SDLIST               instanceList; /* list of instances. Unused */
} TARGET_FUNCTION_CONTEXT;

typedef struct bus_request {
    struct bus_request *next;
    SDREQUEST          *request;
    struct hif_device  *device;
    void               *context;
} BUS_REQUEST;

struct hif_device {
    SDDEVICE    *handle;
    void        *claimedContext;
    BUS_REQUEST *busrequestfreelist;
    BUS_REQUEST  busrequestblob[BUS_REQUEST_MAX_NUM_TOTAL];
    A_BOOL       shutdownInProgress;
    OS_CRITICALSECTION lock;
    A_UINT16     curBlockSize;
    A_UINT16     enabledSpiInts;
    HTC_CALLBACKS     htcCallbacks;
};


void 
hifRWCompletionHandler(SDREQUEST *request);

void
hifIRQHandler(void *context);

BOOL
hifDeviceInserted(SDFUNCTION *function, SDDEVICE *device);

A_STATUS
hifConfigureSPI(HIF_DEVICE *device);

void
hifDeviceRemoved(SDFUNCTION *function, SDDEVICE *device);


BUS_REQUEST *
hifAllocateBusRequest(HIF_DEVICE *device);

void
hifFreeBusRequest(HIF_DEVICE *device, BUS_REQUEST *request);


void HIFSpiDumpRegs(HIF_DEVICE *device);
                            
#define SPIReadInternal(d,a,p) SPIReadWriteInternal((d),(a),(p),TRUE) 
#define SPIWriteInternal(d,a,p) SPIReadWriteInternal((d),(a),(p),FALSE)      

                         
