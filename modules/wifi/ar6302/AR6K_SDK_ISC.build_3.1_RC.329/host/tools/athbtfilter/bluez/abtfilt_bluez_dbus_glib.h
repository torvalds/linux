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
