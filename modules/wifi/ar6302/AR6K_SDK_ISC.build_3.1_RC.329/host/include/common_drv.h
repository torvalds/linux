//------------------------------------------------------------------------------
// Copyright (c) 2010 Atheros Corporation.  All rights reserved.
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
#ifndef COMMON_DRV_H_
#define COMMON_DRV_H_

#include "hif.h"
#include "htc_packet.h"
#include "htc_api.h"

/* structure that is the state information for the default credit distribution callback
 * drivers should instantiate (zero-init as well) this structure in their driver instance
 * and pass it as a context to the HTC credit distribution functions */
typedef struct _COMMON_CREDIT_STATE_INFO {
    int TotalAvailableCredits;      /* total credits in the system at startup */
    int CurrentFreeCredits;         /* credits available in the pool that have not been
                                       given out to endpoints */
    HTC_ENDPOINT_CREDIT_DIST *pLowestPriEpDist;  /* pointer to the lowest priority endpoint dist struct */
} COMMON_CREDIT_STATE_INFO;

typedef struct {
    A_INT32 (*setupTransport)(void *ar);
    void (*cleanupTransport)(void *ar);
} HCI_TRANSPORT_CALLBACKS;

typedef struct {
   void *netDevice;
   void *hifDevice;
   void *htcHandle;
} HCI_TRANSPORT_MISC_HANDLES;

/* HTC TX packet tagging definitions */
#define AR6K_CONTROL_PKT_TAG    HTC_TX_PACKET_TAG_USER_DEFINED
#define AR6K_DATA_PKT_TAG       (AR6K_CONTROL_PKT_TAG + 1)

#define AR6002_VERSION_REV2     0x20000188
#define AR6003_VERSION_REV2     0x30000384

#define AR6002_CUST_DATA_SIZE 112
#define AR6003_CUST_DATA_SIZE 16
#define MCKINLEY_CUST_DATA_SIZE 16

#ifdef __cplusplus
extern "C" {
#endif

/* OS-independent APIs */
A_STATUS ar6000_setup_credit_dist(HTC_HANDLE HTCHandle, COMMON_CREDIT_STATE_INFO *pCredInfo);

A_STATUS ar6000_ReadRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data);

A_STATUS ar6000_WriteRegDiag(HIF_DEVICE *hifDevice, A_UINT32 *address, A_UINT32 *data);

A_STATUS ar6000_ReadDataDiag(HIF_DEVICE *hifDevice, A_UINT32 address,  A_UCHAR *data, A_UINT32 length);

A_STATUS ar6000_reset_device(HIF_DEVICE *hifDevice, A_UINT32 TargetType, A_BOOL waitForCompletion, A_BOOL coldReset);

void ar6000_dump_target_assert_info(HIF_DEVICE *hifDevice, A_UINT32 TargetType);

A_STATUS ar6000_set_htc_params(HIF_DEVICE *hifDevice,
                               A_UINT32    TargetType,
                               A_UINT32    MboxIsrYieldValue,
                               A_UINT8     HtcControlBuffers);

A_STATUS ar6000_prepare_target(HIF_DEVICE *hifDevice,
                               A_UINT32    TargetType,
                               A_UINT32    TargetVersion);

A_STATUS ar6000_set_hci_bridge_flags(HIF_DEVICE *hifDevice,
                                     A_UINT32    TargetType,
                                     A_UINT32    Flags);

void ar6000_copy_cust_data_from_target(HIF_DEVICE *hifDevice, A_UINT32 TargetType);

A_UINT8 *ar6000_get_cust_data_buffer(A_UINT32 TargetType);

A_STATUS ar6000_setBTState(void *context, A_UINT8 *pInBuf, A_UINT32 InBufSize);

A_STATUS ar6000_setDevicePowerState(void *context, A_UINT8 *pInBuf, A_UINT32 InBufSize);

A_STATUS ar6000_setWowMode(void *context, A_UINT8 *pInBuf, A_UINT32 InBufSize);

A_STATUS ar6000_setHostMode(void *context, A_UINT8 *pInBuf, A_UINT32 InBufSize);

#ifdef __cplusplus
}
#endif

#endif /*COMMON_DRV_H_*/
