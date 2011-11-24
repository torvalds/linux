//------------------------------------------------------------------------------
// <copyright file="abtfilt_bluez_glib.h" company="Atheros">
//    Copyright (c) 2007 Atheros Corporation.  All rights reserved.
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

#ifndef ABTFILT_BTSTACK_DBUS_GLIB_H_
#define ABTFILT_BTSTACK_DBUS_GLIB_H_
#include "abtfilt_int.h"
#include <dbus/dbus-glib.h>

/*-----------------------------------------------------------------------*/
/* BT Section */
#define STRING_SIZE_MAX             64
#define BD_ADDR_SIZE                6

typedef struct _ABF_BT_INFO {
    ATHBT_FILTER_INFO              *pInfo;
    A_COND_OBJECT                   hWaitEvent;
    A_MUTEX_OBJECT                  hWaitEventLock;
    A_BOOL                          Loop;
    A_BOOL                          AdapterAvailable;
    GMainLoop                      *Mainloop;
    DBusGConnection                *Bus;
    DBusGProxy                     *DeviceAdapter;
    DBusGProxy                     *DeviceManager;
    DBusGProxy                     *AudioManager;
    DBusGProxy                     *AudioHeadset;
    DBusGProxy                     *AudioGateway;
    DBusGProxy                     *AudioSink;
    DBusGProxy                     *AudioSource;
    DBusGProxy                     *AudioDevice;
    A_UINT8                         HCIVersion;
    A_UINT16                        HCIRevision;
    A_UINT8                         LMPVersion;
    A_UINT16                        LMPSubVersion;
    A_UINT8                         RemoteDevice[BD_ADDR_SIZE];
    A_UINT8                         DeviceAddress[BD_ADDR_SIZE];
    A_CHAR                          AdapterName[STRING_SIZE_MAX];
    A_CHAR                          DeviceName[STRING_SIZE_MAX];
    A_CHAR                          ManufacturerName[STRING_SIZE_MAX];
    A_CHAR                          ProtocolVersion[STRING_SIZE_MAX];
    A_BOOL                          AdapterCbRegistered;
    A_CHAR                          DefaultAudioDeviceName[STRING_SIZE_MAX];
    A_BOOL                          DefaultAudioDeviceAvailable;
    A_BOOL                          AudioCbRegistered;
    A_UCHAR                         CurrentSCOLinkType;
    int                             AdapterId;
    int                             HCIEventListenerSocket;
    A_UINT32                        Flags;
} ABF_BT_INFO;

#endif /*ABTFILT_BTSTACK_DBUS_GLIB_H_*/
