//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
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
// HCI bridge implementation
//
// Author(s): ="Atheros"
//==============================================================================
#include <a_config.h>
#include <athdefs.h>
#include "a_types.h"
#include "a_osapi.h"
#include "htc_api.h"
#include "a_drv.h"
#include "hif.h"
#include "common_drv.h"
#include "a_debug.h"
#include "hci_transport_api.h"


HCI_TRANSPORT_HANDLE (*_HCI_TransportAttach)(void *HTCHandle, HCI_TRANSPORT_CONFIG_INFO *pInfo);
void (*_HCI_TransportDetach)(HCI_TRANSPORT_HANDLE HciTrans);
A_STATUS    (*_HCI_TransportAddReceivePkts)(HCI_TRANSPORT_HANDLE HciTrans, HTC_PACKET_QUEUE *pQueue);
A_STATUS    (*_HCI_TransportSendPkt)(HCI_TRANSPORT_HANDLE HciTrans, HTC_PACKET *pPacket, A_BOOL Synchronous);
void        (*_HCI_TransportStop)(HCI_TRANSPORT_HANDLE HciTrans);
A_STATUS    (*_HCI_TransportStart)(HCI_TRANSPORT_HANDLE HciTrans);
A_STATUS    (*_HCI_TransportEnableDisableAsyncRecv)(HCI_TRANSPORT_HANDLE HciTrans, A_BOOL Enable);
A_STATUS    (*_HCI_TransportRecvHCIEventSync)(HCI_TRANSPORT_HANDLE HciTrans, 
                                          HTC_PACKET           *pPacket,
                                          int                  MaxPollMS);
A_STATUS    (*_HCI_TransportSetBaudRate)(HCI_TRANSPORT_HANDLE HciTrans, A_UINT32 Baud);
A_STATUS    (*_HCI_TransportEnablePowerMgmt)(HCI_TRANSPORT_HANDLE HciTrans, A_BOOL Enable);

extern HCI_TRANSPORT_CALLBACKS ar6kHciTransCallbacks;

A_STATUS ar6000_register_hci_transport(HCI_TRANSPORT_CALLBACKS *hciTransCallbacks)
{
    ar6kHciTransCallbacks = *hciTransCallbacks;

    _HCI_TransportAttach = HCI_TransportAttach;
    _HCI_TransportDetach = HCI_TransportDetach;
    _HCI_TransportAddReceivePkts = HCI_TransportAddReceivePkts;
    _HCI_TransportSendPkt = HCI_TransportSendPkt;
    _HCI_TransportStop = HCI_TransportStop;
    _HCI_TransportStart = HCI_TransportStart;
    _HCI_TransportEnableDisableAsyncRecv = HCI_TransportEnableDisableAsyncRecv;
    _HCI_TransportRecvHCIEventSync = HCI_TransportRecvHCIEventSync;
    _HCI_TransportSetBaudRate = HCI_TransportSetBaudRate;
    _HCI_TransportEnablePowerMgmt = HCI_TransportEnablePowerMgmt;

    return A_OK;
}

A_STATUS
ar6000_get_hif_dev(HIF_DEVICE *device, void *config)
{
    A_STATUS status;

    status = HIFConfigureDevice(device,
                                HIF_DEVICE_GET_OS_DEVICE,
                                (HIF_DEVICE_OS_DEVICE_INFO *)config, 
                                sizeof(HIF_DEVICE_OS_DEVICE_INFO));
    return status;
}

A_STATUS ar6000_set_uart_config(HIF_DEVICE *hifDevice, 
                                A_UINT32 scale, 
                                A_UINT32 step)
{
    A_UINT32 regAddress;
    A_UINT32 regVal;
    A_STATUS status;

    regAddress = WLAN_UART_BASE_ADDRESS | UART_CLKDIV_ADDRESS;
    regVal = ((A_UINT32)scale << 16) | step;
    /* change the HCI UART scale/step values through the diagnostic window */
    status = ar6000_WriteRegDiag(hifDevice, &regAddress, &regVal);                     

    return status;
}

A_STATUS ar6000_get_core_clock_config(HIF_DEVICE *hifDevice, A_UINT32 *data)
{
    A_UINT32 regAddress;
    A_STATUS status;

    regAddress = WLAN_RTC_BASE_ADDRESS | WLAN_CPU_CLOCK_ADDRESS;
    /* read CPU clock settings*/
    status = ar6000_ReadRegDiag(hifDevice, &regAddress, data);

    return status;
}

EXPORT_SYMBOL(ar6000_register_hci_transport);
EXPORT_SYMBOL(ar6000_get_hif_dev);
EXPORT_SYMBOL(ar6000_set_uart_config);
EXPORT_SYMBOL(ar6000_get_core_clock_config);
EXPORT_SYMBOL(_HCI_TransportAttach);
EXPORT_SYMBOL(_HCI_TransportDetach);
EXPORT_SYMBOL(_HCI_TransportAddReceivePkts);
EXPORT_SYMBOL(_HCI_TransportSendPkt);
EXPORT_SYMBOL(_HCI_TransportStop);
EXPORT_SYMBOL(_HCI_TransportStart);
EXPORT_SYMBOL(_HCI_TransportEnableDisableAsyncRecv);
EXPORT_SYMBOL(_HCI_TransportRecvHCIEventSync);
EXPORT_SYMBOL(_HCI_TransportSetBaudRate);
EXPORT_SYMBOL(_HCI_TransportEnablePowerMgmt);
