
//------------------------------------------------------------------------------
// <copyright file="abtfilt_bluez_dbus.c" company="Atheros">
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

/*
 * Bluetooth Filter - BT module
 *
 */

#include "abtfilt_bluez_dbus.h"

#undef HCI_INQUIRY
#include <bluetooth.h>
#include <hci.h>
#include <hci_lib.h>
#include <sys/poll.h>

/* Definitions */

#define BLUEZ_NAME                        "org.bluez"
#define ADAPTER_INTERFACE                 "org.bluez.Adapter"
#define MANAGER_INTERFACE                 "org.bluez.Manager"

#ifndef ANDROID
/* This should be undef'ed if compiled at FC9 using BlueZ 3.x */
#define BLUEZ4_3
#endif /* ANDROID */

#define ABTH_MAX_CONNECTIONS 16

#ifdef BLUEZ4_3

#define BLUEZ_PATH                        "/"
#define AUDIO_MANAGER_PATH                "/org/bluez/"
#define AUDIO_MANAGER_INTERFACE           "org.bluez"
#define AUDIO_SINK_INTERFACE              "org.bluez.AudioSink"
#define AUDIO_SOURCE_INTERFACE            "org.bluez.AudioSource"
#define AUDIO_HEADSET_INTERFACE           "org.bluez.Headset"
#define AUDIO_GATEWAY_INTERFACE           "org.bluez.Gateway"
#define AUDIO_DEVICE_INTERFACE            "org.bluez.Device"

#else

#define BLUEZ_PATH                        "/org/bluez"
#define AUDIO_MANAGER_PATH                "/org/bluez/audio"
#define AUDIO_SINK_INTERFACE              "org.bluez.audio.Sink"
#define AUDIO_SOURCE_INTERFACE            "org.bluez.audio.Source"
#define AUDIO_HEADSET_INTERFACE           "org.bluez.audio.Headset"
#define AUDIO_GATEWAY_INTERFACE           "org.bluez.audio.Gateway"
#define AUDIO_MANAGER_INTERFACE           "org.bluez.audio.Manager"
#define AUDIO_DEVICE_INTERFACE            "org.bluez.audio.Device"
#endif

#define INVALID_INTERFACE                 NULL

#define BTEV_GET_BT_CONN_LINK_TYPE(p)   ((p)[9])
#define BTEV_GET_TRANS_INTERVAL(p)      ((p)[10])
#define BTEV_GET_RETRANS_INTERVAL(p)    ((p)[11])
#define BTEV_GET_RX_PKT_LEN(p)          ((A_UINT16)((p)[12]) | (((A_UINT16)((p)[13])) << 8))
#define BTEV_GET_TX_PKT_LEN(p)          ((A_UINT16)((p)[14]) | (((A_UINT16)((p)[15])) << 8))
#define BTEV_CMD_COMPLETE_GET_OPCODE(p) ((A_UINT16)((p)[1]) | (((A_UINT16)((p)[2])) << 8))          
#define BTEV_CMD_COMPLETE_GET_STATUS(p) ((p)[3])

#define DBUS_METHOD_CALL_TIMEOUT   (-1)   /* no timeout */
#define DBUS_MESSAGE_RECV_TIMEOUT  (-1)   /* no timeout */

#define USE_DBUS_FOR_HEADSET_PROFILE(pInfo) (!((pInfo)->Flags & ABF_USE_HCI_FILTER_FOR_HEADSET_PROFILE))
#define MAKE_BTSTATE_MASK(state) (1 << (state))

typedef enum {
    ARG_INVALID = 0,
    ARG_NONE,
    ARG_STRING,
} BT_CB_TYPE;

typedef struct _BT_NOTIFICATION_CONFIG_PARAMS {
    const char        *signal_name;
    const char        *interface;
    BT_CB_TYPE         arg;
} BT_NOTIFICATION_CONFIG_PARAMS;


ABF_BT_INFO    * g_pAbfBtInfo = NULL;

static BT_NOTIFICATION_CONFIG_PARAMS g_NotificationConfig[BT_EVENTS_NUM_MAX] =
{
    /* BT_ADAPTER_ADDED */
    {"AdapterAdded", MANAGER_INTERFACE, ARG_STRING},
    /* BT_ADAPTER_REMOVED */
    {"AdapterRemoved", MANAGER_INTERFACE, ARG_STRING},
    /* DEVICE_DISCOVERY_STARTED */
    {"DiscoveryStarted", ADAPTER_INTERFACE, ARG_NONE},
    /* DEVICE_DISCOVERY_FINISHED */
    {"DiscoveryCompleted", ADAPTER_INTERFACE, ARG_NONE},
    /* REMOTE_DEVICE_CONNECTED */
    {"RemoteDeviceConnected", ADAPTER_INTERFACE, ARG_STRING},
    /* REMOTE_DEVICE_DISCONNECTED */
    {"RemoteDeviceDisconnected", ADAPTER_INTERFACE, ARG_STRING},
    /* AUDIO_DEVICE_ADDED */
    {"DeviceCreated", AUDIO_MANAGER_INTERFACE, ARG_STRING},
    /* AUDIO_DEVICE_REMOVED */
    {"DeviceRemoved", AUDIO_MANAGER_INTERFACE, ARG_STRING},
    /* AUDIO_HEADSET_CONNECTED */
    {"Connected", AUDIO_HEADSET_INTERFACE, ARG_NONE},
    /* AUDIO_HEADSET_DISCONNECTED */
    {"Disconnected", AUDIO_HEADSET_INTERFACE, ARG_NONE},
    /* AUDIO_HEADSET_STREAM_STARTED */
    {"Playing", AUDIO_HEADSET_INTERFACE, ARG_NONE},
    /* AUDIO_HEADSET_STREAM_STOPPED */
    {"Stopped", AUDIO_HEADSET_INTERFACE, ARG_NONE},
    /* AUDIO_GATEWAY_CONNECTED */
    {NULL, INVALID_INTERFACE, ARG_INVALID},
    /* AUDIO_GATEWAY_DISCONNECTED */
    {NULL, INVALID_INTERFACE, ARG_INVALID},
    /* AUDIO_SINK_CONNECTED */
    {"Connected", AUDIO_SINK_INTERFACE, ARG_NONE},
    /* AUDIO_SINK_DISCONNECTED */
    {"Disconnected", AUDIO_SINK_INTERFACE, ARG_NONE},
    /* AUDIO_SINK_STREAM_STARTED */
    {"Playing", AUDIO_SINK_INTERFACE, ARG_NONE},
    /* AUDIO_SINK_STREAM_STOPPED */
    {"Stopped", AUDIO_SINK_INTERFACE, ARG_NONE},
    /* AUDIO_SOURCE_CONNECTED */
    {NULL, INVALID_INTERFACE, ARG_INVALID},
    /* AUDIO_SOURCE_DISCONNECTED */
    {NULL, INVALID_INTERFACE, ARG_INVALID},
};

typedef struct {
        char *str;
        unsigned int val;
} hci_map;

static const hci_map ver_map[] = {
        { "1.0b",       0x00 },
        { "1.1",        0x01 },
        { "1.2",        0x02 },
        { "2.0",        0x03 },
        { "2.1",        0x04 },
        { NULL }
};

/* Function Prototypes */
static void BtAdapterAdded(const char *string,
                           void * user_data);
static void BtAdapterRemoved(const char *string,
                             void * user_data);
static A_STATUS AcquireBtAdapter(ABF_BT_INFO *pAbfBtInfo);
static void ReleaseBTAdapter(ABF_BT_INFO *pAbfBtInfo);
static void *BtEventThread(void *arg);
static void RegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo,
                                   BT_STACK_EVENT event, BT_EVENT_HANDLER handler);
static void DeRegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, BT_STACK_EVENT event);
static A_STATUS GetAdapterInfo(ABF_BT_INFO *pAbfBtInfo);
static void RemoteDeviceDisconnected(const char *string,
                                     void * user_data);
static void RemoteDeviceConnected(const char *string,
                                  void * user_data);
static void AudioDeviceAdded(const char *string,
                             void * user_data);
static void AudioDeviceRemoved(const char *string,
                               void * user_data);
static void DeviceDiscoveryStarted(void *arg, void * user_data);
static void DeviceDiscoveryFinished(void *arg, void * user_data);
static void AudioHeadsetConnected(void *arg, void * user_data);
static void AudioHeadsetDisconnected(void *arg, void * user_data);
static void AudioHeadsetStreamStarted(void *arg, void * user_data);
static void AudioHeadsetStreamStopped(void *arg, void * user_data);
static void AudioGatewayConnected(void *arg, void * user_data);
static void AudioGatewayDisconnected(void *arg, void * user_data);
static void AudioSinkConnected(void *arg, void * user_data);
static void AudioSinkDisconnected(void *arg, void * user_data);
static void AudioSinkStreamStarted(void *arg, void * user_data);
static void AudioSinkStreamStopped(void *arg, void * user_data);
static void AudioSourceConnected(void *arg, void * user_data);
static void AudioSourceDisconnected(void *arg, void * user_data);
static A_STATUS CheckAndAcquireDefaultAdapter(ABF_BT_INFO *pAbfBtInfo);
static void ReleaseDefaultAdapter(ABF_BT_INFO *pAbfBtInfo);
static void AcquireDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo);
static void ReleaseDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo);
static void GetBtAudioConnectionProperties(ABF_BT_INFO              *pAbfBtInfo,
                                           ATHBT_STATE_INDICATION   Indication);
static void GetBtAudioDeviceProperties(ABF_BT_INFO  *pAbfBtInfo);

static A_STATUS SetupHciEventFilter(ABF_BT_INFO *pAbfBtInfo);
static void CleanupHciEventFilter(ABF_BT_INFO *pAbfBtInfo);
static void CheckHciEventFilter(ABF_BT_INFO   *pAbfBtInfo);

static A_STATUS IssueHCICommand(ABF_BT_INFO *pAbfBtInfo,
                                A_UINT16    OpCode, 
                                A_UCHAR     *pCmdData, 
                                int         CmdLength,
                                int         EventRecvTimeoutMS,
                                A_UCHAR     *pEventBuffer,
                                int         MaxLength,
                                A_UCHAR     **ppEventPtr,
                                int         *pEventLength);
     
     /* method call that involves only 1 input string (can be NULL) and/or 1 output string */                           
static A_STATUS DoMethodCall(DBusConnection *Bus,
                             char           *BusName,
                             char           *Path,
                             char           *Interface,
                             char           *Method,
                             void           *InputArg,
                             int             InputType,
                             int             MaxInLength,
                             void           *OutputArg,
                             int             OutputType,
                             int             MaxOutLength);

static A_STATUS ProcessDBusMessage(ABF_BT_INFO *pAbfBtInfo, DBusMessage *Msg);
static A_STATUS CheckRemoteDeviceEDRCapable(ABF_BT_INFO *pAbfBtInfo, A_BOOL *pEDRCapable);

/* New function to check Remote LMP version */
static A_STATUS GetRemoteDeviceLMPVersion(ABF_BT_INFO *pAbfBtInfo);

static void *HCIFilterThread(void *arg);

A_BOOL g_AppShutdown = FALSE;


#define ForgetRemoteAudioDevice(pA)     \
{                                       \
    A_MEMZERO((pA)->DefaultRemoteAudioDeviceAddress,sizeof((pA)->DefaultRemoteAudioDeviceAddress)); \
    (pA)->DefaultRemoteAudioDevicePropsValid = FALSE;                                               \
}

/* APIs exported to other modules */
A_STATUS
Abf_BtStackNotificationInit(ATH_BT_FILTER_INSTANCE *pInstance, A_UINT32 Flags)
{
    A_STATUS status = A_ERROR;
    ATHBT_FILTER_INFO *pInfo;
    ABF_BT_INFO *pAbfBtInfo;
    DBusError       error;

    pInfo = (ATHBT_FILTER_INFO *)pInstance->pContext;
    if (pInfo->pBtInfo) {
        return A_OK;
    }

    if (g_AppShutdown) {
        A_ERR (" App was already shutdown cannot call Abf_BtStackNotificationInit again!!!");
        return A_ERROR;
    }

    pAbfBtInfo = (ABF_BT_INFO *)A_MALLOC(sizeof(ABF_BT_INFO));
    A_MEMZERO(pAbfBtInfo,sizeof(ABF_BT_INFO));

    A_MUTEX_INIT(&pAbfBtInfo->hWaitEventLock);
    A_MEMZERO(pAbfBtInfo, sizeof(ABF_BT_INFO));

    pInfo->Flags = Flags;

    if (pInfo->Flags & ABF_ENABLE_AFH_CHANNEL_CLASSIFICATION) {
        A_INFO("AFH Classification Command will be issued on WLAN connect/disconnect \n");
    }

    if (pInfo->Flags & ABF_USE_HCI_FILTER_FOR_HEADSET_PROFILE) {
        A_INFO("Headset Profile notifications will use HCI filter instead of DBUS \n");

#ifdef BLUEZ4_3
            /* We don't want to ignore INQUIRY message to implement "DiscoveryStarted" (deprecated in BlueZ 4.x) */
        pInfo->FilterCore.StateFilterIgnore = MAKE_BTSTATE_MASK(ATH_BT_A2DP); //| MAKE_BTSTATE_MASK(ATH_BT_CONNECT);
#else
            /* ignore certain state detections that we can handle through dbus */
        pInfo->FilterCore.StateFilterIgnore = MAKE_BTSTATE_MASK(ATH_BT_CONNECT) |
                                              MAKE_BTSTATE_MASK(ATH_BT_INQUIRY) |
                                              MAKE_BTSTATE_MASK(ATH_BT_A2DP);
#endif

    }

    pAbfBtInfo->AdapterAvailable = FALSE;
    pAbfBtInfo->pInfo = pInfo;
    pAbfBtInfo->HCIEventListenerSocket = -1;
    pInfo->pBtInfo = pAbfBtInfo;

    dbus_error_init(&error);

    do {

        pAbfBtInfo->Bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
        if (NULL == pAbfBtInfo->Bus ) {
            A_ERR("[%s] Couldn't connect to system bus: %s\n",
                  __FUNCTION__, error.message);
            break;
        }

            /* check for default adapter at startup */
        CheckAndAcquireDefaultAdapter(pAbfBtInfo);
        RegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_ADDED, (BT_EVENT_HANDLER)BtAdapterAdded);
        RegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_REMOVED, (BT_EVENT_HANDLER)BtAdapterRemoved);

        if(pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING) {
    	    Abf_RegisterToHciLib(pAbfBtInfo);
        }

        /* Spawn a thread which will be used to process events from dbus */
        status = A_TASK_CREATE(&pInfo->hBtThread, BtEventThread, pAbfBtInfo);
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to spawn a BT thread\n", __FUNCTION__);
            break;
        }

        pAbfBtInfo->ThreadCreated = TRUE;
        status = A_OK;

    } while (FALSE);

    dbus_error_free(&error);

    if (A_FAILED(status)) {
        Abf_BtStackNotificationDeInit(pInstance);
    }
    A_INFO("BT Stack Notification init complete\n");

    return status;
}

void
Abf_BtStackNotificationDeInit(ATH_BT_FILTER_INSTANCE *pInstance)
{
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pInstance->pContext;
    ABF_BT_INFO *pAbfBtInfo = pInfo->pBtInfo;

    g_AppShutdown = TRUE;

    if (!pAbfBtInfo) return;

        /* acquire lock to sync with thread */
    A_MUTEX_LOCK(&pAbfBtInfo->hWaitEventLock);

    if (pAbfBtInfo->Bus != NULL) {
        A_INFO("Cleaning up dbus connection ... \n");

        /* NOTE: there is really no need to de-register callbacks and cleanup state since we
         * are exiting the application and cleaning up the bus connection.  When the dbus conneciont
         * is cleaned up, all signal registrations are cleanedup by the dbus library anyways.
         *
        DeRegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_ADDED);
        DeRegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_REMOVED);
        ReleaseDefaultAdapter(pAbfBtInfo);
        */

        /* Release the system bus */
        dbus_connection_unref(pAbfBtInfo->Bus);
        pAbfBtInfo->Bus = NULL;
    }

    Abf_UnRegisterToHciLib(pAbfBtInfo);

    /* Flush all the BT actions from the filter core TODO */

    /* Free the remaining resources */
    pAbfBtInfo->AdapterAvailable = FALSE;
    pInfo->pBtInfo = NULL;
    A_MUTEX_DEINIT(&pAbfBtInfo->hWaitEventLock);
    A_MEMZERO(pAbfBtInfo, sizeof(ABF_BT_INFO));
    A_FREE(pAbfBtInfo);

    A_INFO("BT Stack Notification de-init complete\n");
}

static void *
BtEventThread(void *arg)
{
    ABF_BT_INFO     *pAbfBtInfo = (ABF_BT_INFO *)arg;
    DBusMessage     *msg = NULL;
    A_BOOL          error = FALSE;

    g_pAbfBtInfo = (ABF_BT_INFO *) arg;
    A_INFO("Starting the BT Event Handler task\n");

    A_INFO("Entering DBus Message loop \n");

    Abf_WlanGetSleepState(pAbfBtInfo->pInfo);

    while (!g_AppShutdown && !error) {

        dbus_connection_read_write(pAbfBtInfo->Bus, DBUS_MESSAGE_RECV_TIMEOUT);

        if (g_AppShutdown) {
            break;
        }
            /* while we retrieve and process messages we don't want this thread killed */
        A_MUTEX_LOCK(&pAbfBtInfo->hWaitEventLock);

        while (!error) {
            msg = dbus_connection_pop_message(pAbfBtInfo->Bus);
            if (NULL == msg) {
                break;
            }
            A_DEBUG(" Got DBus Message ... \n");
            if (A_FAILED(ProcessDBusMessage(pAbfBtInfo, msg))) {
                error = TRUE;
            }
            dbus_message_unref(msg);
            msg = NULL;
        }

        A_MUTEX_UNLOCK(&pAbfBtInfo->hWaitEventLock);

    }

    A_INFO("Leaving DBus Message loop \n");
    A_INFO("Leaving the BT Event Handler task\n");

    return NULL;
}

static A_STATUS
CheckAndAcquireDefaultAdapter(ABF_BT_INFO *pAbfBtInfo)
{
    A_STATUS status = A_OK;

    do {

        if (pAbfBtInfo->AdapterAvailable) {
                /* already available */
            break;
        }

            /* acquire the adapter */
        status = AcquireBtAdapter(pAbfBtInfo);

    } while (FALSE);

    return status;
}

static void ReleaseDefaultAdapter(ABF_BT_INFO *pAbfBtInfo)
{

    if (pAbfBtInfo->AdapterAvailable) {
            /* Release the BT adapter */
        ReleaseBTAdapter(pAbfBtInfo);
        A_INFO("[%s] BT Adapter Removed\n",pAbfBtInfo->HCI_AdapterName);
    }

    A_MEMZERO(pAbfBtInfo->HCI_AdapterName, sizeof(pAbfBtInfo->HCI_AdapterName));

}
/* Event Notifications */
static void
BtAdapterAdded(const char *string, void * user_data)
{
    A_DEBUG("BtAdapterAdded (%s) Callback ... \n", (string != NULL) ? string : "UNKNOWN");

    /* BUG!!!, the BtAdapterAdded callback is indicated too early by the BT service, on some systems
     * the method call to "DefaultAdapter" through the Manager interface will fail because no
     * default adapter exist yet even though this callback was indicated (there should be a default)
     *
     * Workaround is to delay before acquiring the default adapter.
     * Acquiring the BT adapter should not be very infrequent though.
     *
     * */
    sleep(5);
    CheckAndAcquireDefaultAdapter((ABF_BT_INFO *)user_data);
}


static void
BtAdapterRemoved(const char *string, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;

    A_DEBUG("BtAdapterRemoved (%s) Callback ... \n", (string != NULL) ? string : "UNKNOWN");
    
    if (!pAbfBtInfo->AdapterAvailable) return;

    if ((string != NULL) && strcmp(string,pAbfBtInfo->HCI_AdapterName) == 0) {
            /* the adapter we are watching has been removed */
        ReleaseDefaultAdapter(pAbfBtInfo);
    }

}

static void
DeviceDiscoveryStarted(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Inquiry Started\n");
    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_ON);
}

static void
DeviceDiscoveryFinished(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Inquiry Completed\n");
    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
}

static void
RemoteDeviceConnected(const char *string, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Connected: %s\n", string);
    A_STR2ADDR(string, pAbfBtInfo->RemoteDevice);
    AthBtIndicateState(pInstance, ATH_BT_CONNECT, STATE_ON);
}

static void
RemoteDeviceDisconnected(const char *string, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Disconnected: %s\n", string);
    A_MEMZERO(pAbfBtInfo->RemoteDevice, sizeof(pAbfBtInfo->RemoteDevice));
    AthBtIndicateState(pInstance, ATH_BT_CONNECT, STATE_OFF);
}

static void ReleaseDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo)
{
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;

    if (pAbfBtInfo->AudioCbRegistered) {
        if (USE_DBUS_FOR_HEADSET_PROFILE(pInfo)) {
            DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_CONNECTED);
            DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_DISCONNECTED);
            DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STARTED);
            DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STOPPED);
        }
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_CONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_DISCONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_CONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_DISCONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STARTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STOPPED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_CONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_DISCONNECTED);
        pAbfBtInfo->AudioCbRegistered = FALSE;
    }
    
    if (pAbfBtInfo->DefaultAudioDeviceAvailable) {
        pAbfBtInfo->DefaultAudioDeviceAvailable = FALSE;
        A_DEBUG("Default Audio Device Removed: %s\n", pAbfBtInfo->DefaultAudioDeviceName);
        A_MEMZERO(pAbfBtInfo->DefaultAudioDeviceName,sizeof(pAbfBtInfo->DefaultAudioDeviceName));
    }
    
}

static A_STATUS DoMethodCall(DBusConnection *Bus,
                             char           *BusName,
                             char           *Path,
                             char           *Interface,
                             char           *Method,                                  
                             void           *InputArg,
                             int             InputType,
                             int             InputLength,
                             void           *OutputArg,
                             int             OutputType,
                             int             MaxOutLength)
{
    A_STATUS    status = A_ERROR;
    DBusError   error;
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    void        *replyData;
    DBusMessageIter args;
    
    dbus_error_init(&error);

    do { 
        msg = dbus_message_new_method_call(BusName, Path, Interface, Method);
                                           
        if (NULL == msg) {
            A_ERR("[%s] failed new method call line \n", __FUNCTION__);
            break;    
        }
        
            /* see if caller is providing an argument */
        if (InputArg != NULL) {           
            dbus_message_iter_init_append(msg, &args);
            if (InputType == DBUS_TYPE_STRING) { 
                if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, (char **)&InputArg)) { 
                    A_ERR("[%s] Failed to add string input argument \n", __FUNCTION__);
                    break;  
                }
            } else {
                A_ERR("[%s] unsupported input arg type: %c \n", __FUNCTION__, (char)InputType);
                break;      
            }
        }
              
        reply = dbus_connection_send_with_reply_and_block(Bus, msg, DBUS_METHOD_CALL_TIMEOUT, &error);

       
        if (dbus_error_is_set(&error)) {
            A_ERR("[%s] Failed to invoke method call (%s : method : %s) %s \n",
                     __FUNCTION__, Interface, Method, error.message);
            break;    
        }

            /* check if caller expects a return string */
        if (OutputArg != NULL) {

            replyData = NULL;


            if (OutputType == DBUS_TYPE_ARRAY) {

                DBusMessageIter iter,subIter;
                dbus_message_iter_init (reply, &iter);
                if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {

                        /* recurse into the array */
                    dbus_message_iter_recurse(&iter, &subIter);
                        /* get a pointer to the start of the array */

                    dbus_message_iter_get_fixed_array(&subIter, &replyData, &MaxOutLength);
                } else {
                    A_ERR("[%s] ARG Type (%s) is not an array! \n", __FUNCTION__,
                                                (char)dbus_message_iter_get_arg_type(&iter));
                    break;
                }

            } else if (OutputType == DBUS_TYPE_STRING) {

                dbus_message_get_args(reply, &error, OutputType, &replyData, DBUS_TYPE_INVALID);

                if (dbus_error_is_set(&error)) {
                    A_ERR("[%s] dbus_message_get_args failed (%s : method : %s) %s \n",
                         __FUNCTION__, Interface, Method, error.message);
                    break;
                }

            } else if (OutputType == DBUS_TYPE_OBJECT_PATH) {

                dbus_message_get_args(reply, &error, OutputType, &replyData, DBUS_TYPE_INVALID);

                if (dbus_error_is_set(&error)) {
                    A_ERR("[%s] dbus_message_get_args failed (%s : method : %s) %s \n", 
                         __FUNCTION__, Interface, Method, error.message);
                    break;
                }

            } else {
                A_ERR("[%s] unsupported output arg type: %c \n", __FUNCTION__,(char)OutputType);
                break;
            }

            if (NULL == replyData) {
                A_ERR("[%s] type %c data pointer was not returned from method call \n", 
                    __FUNCTION__, (char)OutputType);
                break;
            }

            if (OutputType == DBUS_TYPE_STRING) {
                strncpy(OutputArg, (char *)replyData, MaxOutLength);
            } else if (OutputType == DBUS_TYPE_ARRAY) {
                A_ERR("  (p:0x%X) Array has %d elements: \n", replyData, MaxOutLength);
                    /* just copy what the caller expects for data */

                A_MEMCPY(OutputArg, replyData, MaxOutLength);  

            } else if (OutputType == DBUS_TYPE_OBJECT_PATH) { /* Added by YG, November 19, 2009 */
                strncpy(OutputArg, (char *)replyData, MaxOutLength);
            }
        }
       
        status = A_OK;     
           
    } while (FALSE);
     
    if (msg != NULL) {
        dbus_message_unref(msg);
    }
    
    if (reply != NULL) {
        dbus_message_unref(reply);    
    }

    dbus_error_free(&error);
   
    return status;   
}

static void AcquireDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo)
{
    A_BOOL     success = FALSE;
#ifndef BLUEZ4_3
    A_STATUS   status;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
#endif

    A_INFO("[%s]StartRegister\n", __FUNCTION__);
    do {

        if (pAbfBtInfo->DefaultAudioDeviceAvailable) {
                /* already acquired */
            success = TRUE;
            break;
        }
#ifdef BLUEZ4_3
        pAbfBtInfo->DefaultAudioDeviceAvailable = FALSE;
#else
        A_INFO("Checking for a default audio device .. \n");

        status = DoMethodCall(pAbfBtInfo->Bus,
                            BLUEZ_NAME,
                            AUDIO_MANAGER_PATH,
                            AUDIO_MANAGER_INTERFACE,
                            "DefaultDevice",
                            NULL,
                            DBUS_TYPE_INVALID,
                            0,
                            pAbfBtInfo->DefaultAudioDeviceName,
                            DBUS_TYPE_STRING,
                            sizeof(pAbfBtInfo->DefaultAudioDeviceName));

        if (A_FAILED(status)) {
            break;
        }

        A_INFO("Default Audio Device: %s \n", pAbfBtInfo->DefaultAudioDeviceName);

        pAbfBtInfo->DefaultAudioDeviceAvailable = TRUE;
#endif /* BLUEZ4_3  */

            /* Register for audio specific events */
#ifdef BLUEZ4_3
        if (1) {
#else
        if (USE_DBUS_FOR_HEADSET_PROFILE(pInfo)) {
#endif
            RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_CONNECTED, AudioHeadsetConnected);
            RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_DISCONNECTED, AudioHeadsetDisconnected);
            RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STARTED, AudioHeadsetStreamStarted);
            RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STOPPED, AudioHeadsetStreamStopped);
        }
#ifndef BLUEZ4_3
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_CONNECTED, AudioGatewayConnected);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_DISCONNECTED, AudioGatewayDisconnected);
#endif /* !BLUEZ4_3 */
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_CONNECTED,AudioSinkConnected);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_DISCONNECTED, AudioSinkDisconnected);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STARTED,AudioSinkStreamStarted);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STOPPED, AudioSinkStreamStopped);
#ifndef BLUEZ4_3
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_CONNECTED, AudioSourceConnected);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_DISCONNECTED, AudioSourceDisconnected);
#endif /* !BLUEZ4_3 */

        pAbfBtInfo->AudioCbRegistered = TRUE;

        success = TRUE;

        A_INFO("[%s]EndRegister\n", __FUNCTION__);
    } while (FALSE);

    if (!success) {
            /* cleanup */
        ReleaseDefaultAudioDevice(pAbfBtInfo);
    }
}

static void
AudioDeviceAdded(const char *string, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;

    A_DEBUG("Audio Device Added: %s\n", string);
        /* release current one if any */
    ReleaseDefaultAudioDevice(pAbfBtInfo);
        /* re-acquire the new default, it could be the same one */
    AcquireDefaultAudioDevice(pAbfBtInfo);

}

static void
AudioDeviceRemoved(const char *string, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;

    A_DEBUG("Audio Device Removed: %s\n", string);
    if (strcmp(string,pAbfBtInfo->DefaultAudioDeviceName) == 0) {
            /* release current one  */
        ReleaseDefaultAudioDevice(pAbfBtInfo);
            /* re-acquire the new default (if any) */
        AcquireDefaultAudioDevice(pAbfBtInfo);
    }

}


static void
AudioHeadsetConnected(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;

    A_DEBUG("Audio Headset Connected \n");

    if(!(pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING)) {
        GetBtAudioDeviceProperties(pAbfBtInfo);
    }
}

static void
AudioHeadsetDisconnected(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO  *)user_data;
    A_DEBUG("Audio Headset (%s) Disconnected\n",pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
    ForgetRemoteAudioDevice(pAbfBtInfo);
}

static void
AudioHeadsetStreamStarted(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    if(!(pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING)) {
        if (!pAbfBtInfo->DefaultRemoteAudioDevicePropsValid) {
        GetBtAudioDeviceProperties(pAbfBtInfo);
        }
    }
    A_DEBUG("Audio Headset (%s) Stream Started \n",pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
        /* make the indication */
        /* get properties of this headset connection */
    GetBtAudioConnectionProperties(pAbfBtInfo, ATH_BT_SCO);

    AthBtIndicateState(pInstance,
                       pAbfBtInfo->CurrentSCOLinkType == SCO_LINK ? ATH_BT_SCO : ATH_BT_ESCO,
                       STATE_ON);
}

static void
AudioHeadsetStreamStopped(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

        /* This event can also be used to indicate the SCO state */
    A_DEBUG("Audio Headset (%s) Stream Stopped \n", pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
    AthBtIndicateState(pInstance,
                       pAbfBtInfo->CurrentSCOLinkType == SCO_LINK ? ATH_BT_SCO : ATH_BT_ESCO,
                       STATE_OFF);
}

static void
AudioGatewayConnected(void *arg, void * user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Gateway Connected\n");
}

static void
AudioGatewayDisconnected(void *arg, void * user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Gateway disconnected\n");
}

static void
AudioSinkConnected(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    A_DEBUG("Audio Sink Connected \n");

    if(!(pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING)) {
        GetBtAudioDeviceProperties(pAbfBtInfo);
    }
}

static void
AudioSinkDisconnected(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;
    A_DEBUG("Audio Sink (%s) Disconnected \n",pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
    AthBtIndicateState(pInstance, ATH_BT_A2DP, STATE_OFF);
    ForgetRemoteAudioDevice(pAbfBtInfo);
}

static void
AudioSinkStreamStarted(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    if (!pAbfBtInfo->DefaultRemoteAudioDevicePropsValid) {
        GetBtAudioDeviceProperties(pAbfBtInfo);
    }

    A_DEBUG("Audio Sink (%s) Stream Started \n",pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
        /* get connection properties */
    GetBtAudioConnectionProperties(pAbfBtInfo, ATH_BT_A2DP);
    AthBtIndicateState(pInstance, ATH_BT_A2DP, STATE_ON);
}

static void
AudioSinkStreamStopped(void *arg, void * user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Audio Sink (%s) Stream Stopped \n",pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
    AthBtIndicateState(pInstance, ATH_BT_A2DP, STATE_OFF);
}

static void
AudioSourceConnected(void *arg, void * user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Source Connected \n");
}

static void
AudioSourceDisconnected(void *arg, void * user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Source Disconnected \n");
}


static A_STATUS ProcessDBusMessage(ABF_BT_INFO *pAbfBtInfo, DBusMessage *Msg)
{
    A_STATUS                        status = A_OK;
    BT_NOTIFICATION_CONFIG_PARAMS   *pNotificationConfig;
    int                             i, argType;
    DBusMessageIter                 msgiter;
    char                            *argString;

    pNotificationConfig = &g_NotificationConfig[0];

    for (i = 0; i < BT_EVENTS_NUM_MAX; i++, pNotificationConfig++) {
        if ((pNotificationConfig->interface != NULL) &&
            (pNotificationConfig->signal_name != NULL)) {

            if (dbus_message_is_signal(Msg,
                                       pNotificationConfig->interface,
                                       pNotificationConfig->signal_name)) {

                    /* found a match */
                break;
            }
        }

    }

    if (i >= BT_EVENTS_NUM_MAX) {
        /* not a signal we registered for */
        return A_OK;
    }

    do {
        if (pAbfBtInfo->SignalHandlers[i] == NULL) {
                /* no registered handler for this signal, just ignore */
            break;
        }
        if (pNotificationConfig->arg == ARG_NONE) {
                /* call zero-argument handler */
            pAbfBtInfo->SignalHandlers[i](NULL, pAbfBtInfo);
        } else if (pNotificationConfig->arg == ARG_STRING) {
                /* we are expecting a string argument */
            if (!dbus_message_iter_init(Msg, &msgiter)) {
                A_ERR("[%s] event: %d (if:%s , sig: %s) expecting a string argument but there are none \n",
                        __FUNCTION__, i, pNotificationConfig->interface,
                        pNotificationConfig->signal_name );
                break;
            }

            argType = dbus_message_iter_get_arg_type(&msgiter);
            if(!((argType == DBUS_TYPE_STRING) || argType == DBUS_TYPE_OBJECT_PATH)) {
                 A_ERR("[%s] event: %d (if:%s , sig: %s) expecting a string /object path argument !\n",
                        __FUNCTION__, i, pNotificationConfig->interface,
                        pNotificationConfig->signal_name);
                break;
            }

            dbus_message_iter_get_basic(&msgiter, &argString);
                /* call string arg handler */
            pAbfBtInfo->SignalHandlers[i](argString, pAbfBtInfo);
        }

    } while (FALSE);

    return status;
}

static void
RegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, BT_STACK_EVENT event,
                       BT_EVENT_HANDLER handler)
{
    BT_NOTIFICATION_CONFIG_PARAMS *pNotificationConfig;
    DBusError                     error;
    char                          tempStr[STRING_SIZE_MAX];
    dbus_error_init(&error);

    do {

        if (event >= BT_EVENTS_NUM_MAX) {
            A_ERR("[%s] Invalid Event: %d\n", __FUNCTION__, event);
            break;
        }
        pNotificationConfig = &g_NotificationConfig[event];

        if (pAbfBtInfo->SignalHandlers[event] != NULL) {
            A_ERR("[%s] event: %d already in use \n", __FUNCTION__);
            break;
        }
        if (pNotificationConfig->interface == NULL) {
                /* no interface, may not be implemented yet */
            A_ERR("[%s] Event: %d not implemented yet \n", __FUNCTION__, event);
            break;
        }

        snprintf(tempStr, sizeof(tempStr), "type='signal',interface='%s'",pNotificationConfig->interface);

        A_DEBUG(" rule to add: %s \n", tempStr);


            /* add signal */
        dbus_bus_add_match(pAbfBtInfo->Bus, tempStr, &error);

        if (dbus_error_is_set(&error)) {
            A_ERR("[%s] dbus_bus_add_match failed: %s n", __FUNCTION__, error.message);
            break;
        }
        dbus_connection_flush(pAbfBtInfo->Bus);

            /* install handler */
        pAbfBtInfo->SignalHandlers[event] = handler;

    } while (FALSE);

    dbus_error_free(&error);
}


static void
DeRegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, BT_STACK_EVENT event)
{
    BT_NOTIFICATION_CONFIG_PARAMS *pNotificationConfig;
    DBusError                     error;
    char                          tempStr[STRING_SIZE_MAX];
    
    dbus_error_init(&error);
    
    do {
        
        if (event >= BT_EVENTS_NUM_MAX) {
            A_ERR("[%s] Invalid Event: %d\n", __FUNCTION__, event);
            break;    
        }
       
        pNotificationConfig = &g_NotificationConfig[event];
           
        if (pAbfBtInfo->SignalHandlers[event] == NULL) {
            A_ERR("[%s] event: %d is not in use! \n", __FUNCTION__, event);
            break;    
        }
        
        snprintf(tempStr, sizeof(tempStr),"type='signal',interface='%s'",pNotificationConfig->interface);
        
        A_DEBUG(" rule to remove : %s \n", tempStr);
        
            /* remove rule */
        dbus_bus_remove_match(pAbfBtInfo->Bus, tempStr, &error);
        
        if (dbus_error_is_set(&error)) { 
            A_ERR("[%s] dbus_bus_remove_match failed: %s n", __FUNCTION__, error.message);
            break;
        }
                    
        dbus_connection_flush(pAbfBtInfo->Bus);

            /* delete handler */
        pAbfBtInfo->SignalHandlers[event] = NULL;

    } while (FALSE);

    dbus_error_free(&error);

}

/* Misc */
static A_STATUS
AcquireBtAdapter(ABF_BT_INFO *pAbfBtInfo)
{
    A_STATUS        status = A_ERROR;
    char            *hciName;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;

    do {

        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              BLUEZ_PATH,
                              MANAGER_INTERFACE,
                              "DefaultAdapter",
                              NULL,
                              DBUS_TYPE_INVALID,
                              0,
                              pAbfBtInfo->HCI_AdapterName,
#ifdef BLUEZ4_3
                              DBUS_TYPE_OBJECT_PATH,
#else
                              DBUS_TYPE_STRING,
#endif
                              sizeof(pAbfBtInfo->HCI_AdapterName));

        if (A_FAILED(status)) {
            A_ERR("[%s] Get Default Adapter failed \n", __FUNCTION__);
            break;
        }

            /* assume ID 0 */
        pAbfBtInfo->AdapterId = 0;

        if ((hciName = strstr(pAbfBtInfo->HCI_AdapterName, "hci")) != NULL) {
                /* get the number following the hci name, this is the ID used for
                 * socket calls to the HCI layer */
            pAbfBtInfo->AdapterId = (int)hciName[3] - (int)'0';
            if (pAbfBtInfo->AdapterId < 0) {
                pAbfBtInfo->AdapterId = 0;
            }
        }

        if(!(pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING)) {
            if (!A_SUCCESS(SetupHciEventFilter(pAbfBtInfo))) {
                break;
            }
            GetAdapterInfo(pAbfBtInfo);
        }


        pAbfBtInfo->pInfo->LMPVersion = pAbfBtInfo->HCI_LMPVersion;

        pAbfBtInfo->AdapterAvailable = TRUE;
        /* Register to get notified of different stack events */
        RegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_STARTED, DeviceDiscoveryStarted);
        RegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_FINISHED, DeviceDiscoveryFinished);
        RegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_CONNECTED, (BT_EVENT_HANDLER)RemoteDeviceConnected);
        RegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_DISCONNECTED, (BT_EVENT_HANDLER)RemoteDeviceDisconnected);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_ADDED, (BT_EVENT_HANDLER)AudioDeviceAdded);
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_REMOVED, (BT_EVENT_HANDLER)AudioDeviceRemoved);

        pAbfBtInfo->AdapterCbRegistered = TRUE;

        A_INFO("[%s] BT Adapter Added\n",pAbfBtInfo->HCI_AdapterName);

            /* acquire default audio device */
        AcquireDefaultAudioDevice(pAbfBtInfo);

        status = A_OK;

    } while (FALSE);


    return status;
}

static void
ReleaseBTAdapter(ABF_BT_INFO *pAbfBtInfo)
{

    if (pAbfBtInfo->AdapterCbRegistered) {
        pAbfBtInfo->AdapterCbRegistered = FALSE;
            /* Free the resources held for the event handlers */
        DeRegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_STARTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_FINISHED);
        DeRegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_CONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_DISCONNECTED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_ADDED);
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_REMOVED);    
    }
   
    ReleaseDefaultAudioDevice(pAbfBtInfo);  
     
    CleanupHciEventFilter(pAbfBtInfo);
    
    A_MEMZERO(pAbfBtInfo->HCI_DeviceAddress, 
              sizeof(pAbfBtInfo->HCI_DeviceAddress));
    A_MEMZERO(pAbfBtInfo->HCI_DeviceName, 
              sizeof(pAbfBtInfo->HCI_DeviceName));
    A_MEMZERO(pAbfBtInfo->HCI_ManufacturerName, 
              sizeof(pAbfBtInfo->HCI_ManufacturerName));
    A_MEMZERO(pAbfBtInfo->HCI_ProtocolVersion, 
              sizeof(pAbfBtInfo->HCI_ProtocolVersion));
    pAbfBtInfo->HCI_LMPVersion = 0;

    pAbfBtInfo->AdapterAvailable = FALSE;
}

#ifdef BLUEZ4_3

static A_STATUS
GetAdapterInfo(ABF_BT_INFO *pAbfBtInfo)
{

    A_STATUS status;

    int i;
    A_UCHAR eventBuffer[HCI_MAX_EVENT_SIZE];
    A_UCHAR *eventPtr;
    int eventLen; 
        
        /* Get adapter/device address by issuing HCI command, Read_BD_ADDR */
    status = IssueHCICommand(pAbfBtInfo,
                cmd_opcode_pack(OGF_INFO_PARAM, OCF_READ_BD_ADDR), 
                0,
                0,
                2000,
                eventBuffer,
                sizeof(eventBuffer),
                &eventPtr,
                &eventLen);          

    if (A_FAILED(status)) {
        A_ERR("[%s] Failed to get BD_ADDR \n", __FUNCTION__);
        return status;
    }

    if (eventBuffer[6] == 0) { /* READ_BD_ADDR was a success */
        for (i = 0; i < BD_ADDR_SIZE; i++) { 
            pAbfBtInfo->HCI_DeviceAddress[i] = eventBuffer[i+7];
        }
        A_DEBUG("BT-HCI Device Address: (%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X)\n", 
               pAbfBtInfo->HCI_DeviceAddress[0], pAbfBtInfo->HCI_DeviceAddress[1], 
               pAbfBtInfo->HCI_DeviceAddress[2], pAbfBtInfo->HCI_DeviceAddress[3], 
               pAbfBtInfo->HCI_DeviceAddress[4], pAbfBtInfo->HCI_DeviceAddress[5]);
    }

    status = A_OK; 

    return status;
}

#else

static A_STATUS
GetAdapterInfo(ABF_BT_INFO *pAbfBtInfo)
{
    int         count;
    A_STATUS    status = A_OK;
    char        tempStr[STRING_SIZE_MAX];

    do {
            /* device name */       
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->HCI_AdapterName,
                              ADAPTER_INTERFACE,
                              "GetName",
                              NULL,
                              DBUS_TYPE_INVALID,
                              0,
                              pAbfBtInfo->HCI_DeviceName,
                              DBUS_TYPE_STRING,
                              sizeof(pAbfBtInfo->HCI_DeviceName));
        
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to complete GetName \n", __FUNCTION__);
            break;    
        }

            /* Manufacturer name */
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->HCI_AdapterName,
                              ADAPTER_INTERFACE,
                              "GetManufacturer",
                              NULL,
                              DBUS_TYPE_INVALID,
                              0,
                              pAbfBtInfo->HCI_ManufacturerName,
                              DBUS_TYPE_STRING,
                              sizeof(pAbfBtInfo->HCI_ManufacturerName));
        
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to complete GetManufacturer \n", __FUNCTION__);
            break;    
        }
 
            /* get LMP version */ 
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->HCI_AdapterName,
                              ADAPTER_INTERFACE,
                              "GetVersion",
                              NULL,
                              DBUS_TYPE_INVALID,
                              0,
                              pAbfBtInfo->HCI_ProtocolVersion,
                              DBUS_TYPE_STRING,
                              sizeof(pAbfBtInfo->HCI_ProtocolVersion));
        
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to complete GetVersion \n", __FUNCTION__);
            break;    
        }


        for (count = 0; 
             ((count < sizeof(ver_map)/sizeof(hci_map)) && (ver_map[count].str)); 
             count++)
        {
            if (strstr(pAbfBtInfo->HCI_ProtocolVersion, ver_map[count].str)) {
                pAbfBtInfo->HCI_LMPVersion = ver_map[count].val;
                break;
            }
        }

            /* Device address */        
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->HCI_AdapterName,
                              ADAPTER_INTERFACE,
                              "GetAddress",
                              NULL,
                              DBUS_TYPE_INVALID,
                              0,
                              tempStr,
                              DBUS_TYPE_STRING,
                              sizeof(tempStr));
        
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to get Address \n", __FUNCTION__);
            break;    
        }
        
        A_STR2ADDR(tempStr, pAbfBtInfo->HCI_DeviceAddress);
   
    } while (FALSE);

    
    if (A_SUCCESS(status)) {
        A_INFO("BT-HCI Device Address: (%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X)\n", 
               pAbfBtInfo->HCI_DeviceAddress[0], pAbfBtInfo->HCI_DeviceAddress[1], 
               pAbfBtInfo->HCI_DeviceAddress[2], pAbfBtInfo->HCI_DeviceAddress[3], 
               pAbfBtInfo->HCI_DeviceAddress[4], pAbfBtInfo->HCI_DeviceAddress[5]);
        A_INFO("BT-HCI Device Name: %s\n", pAbfBtInfo->HCI_DeviceName);
        A_INFO("BT-HCI Manufacturer Name: %s\n", pAbfBtInfo->HCI_ManufacturerName);
        A_INFO("BT-HCI Protocol Version: %s\n", pAbfBtInfo->HCI_ProtocolVersion);
        A_INFO("BT-HCI LMP Version: %d\n", pAbfBtInfo->HCI_LMPVersion);

    }

    return status;
}
#endif


static A_STATUS GetConnectedDeviceRole(ABF_BT_INFO   *pAbfBtInfo,
                                       A_CHAR        *Address,
                                       A_BOOL        IsSCO,
                                       A_UCHAR       *pRole)
{
    A_STATUS                    status = A_ERROR;
    struct hci_conn_list_req    *connList = NULL;
    struct hci_conn_info        *connInfo = NULL;
    int                         i, sk = -1;
    int                         len;

    do {

        sk = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);

        if (sk < 0) {
            A_ERR("[%s] Failed to get raw BT socket: %d \n", __FUNCTION__, errno);
            break;
        }

        len = (sizeof(*connInfo)) * ABTH_MAX_CONNECTIONS + sizeof(*connList);

        connList = (struct hci_conn_list_req *)A_MALLOC(len);
        if (connList == NULL) {
            break;
        }

        A_MEMZERO(connList,len);

        connList->dev_id = pAbfBtInfo->AdapterId;
        connList->conn_num = ABTH_MAX_CONNECTIONS;
        connInfo = connList->conn_info;

        if (ioctl(sk, HCIGETCONNLIST, (void *)connList)) {
            A_ERR("[%s] Failed to get connection list %d \n", __FUNCTION__, errno);
            break;
        }
           /* walk through connection list */
        for (i = 0; i < connList->conn_num; i++, connInfo++) {
            char addr[32];

                /* convert to a string to compare */
            ba2str(&connInfo->bdaddr, addr);

            if (strcmp(addr,Address) != 0) {
                continue;
            }

            if (IsSCO) {
                    /* look for first non-ACL connection */
                if (connInfo->type == ACL_LINK) {
                    continue;
                }
                pAbfBtInfo->CurrentSCOLinkType = connInfo->type;
            } else {
                    /* look for first ACL connection */
                if (connInfo->type != ACL_LINK) {
                    continue;
                }
            }
            /* if we get here we have a connection we are interested in */
            if (connInfo->link_mode & HCI_LM_MASTER) {
                    /* master */
                *pRole = 0;
            }  else {
                    /* slave */
                *pRole = 1;
            }

            A_INFO("[%s] Found Connection (Link-Type : %d), found role:%d \n",
                    Address, connInfo->type, *pRole);
            break;
        }

        if (i == connList->conn_num) {
            A_ERR("[%s] Could not find connection info for %s %d \n", __FUNCTION__, Address);
            break;
        }
        status = A_OK;

    } while (FALSE);

    if (sk >= 0) {
        close(sk);
    }

    if (connList != NULL) {
        A_FREE(connList);
    }

    return status;
}

#ifdef BLUEZ4_3

static void GetBtAudioDeviceProperties(ABF_BT_INFO  *pAbfBtInfo)
{
    A_STATUS    status;

    pAbfBtInfo->DefaultRemoteAudioDevicePropsValid = FALSE;

    do {

            /* Need RemoteDeviceAddress */


        status = GetRemoteDeviceLMPVersion(pAbfBtInfo);

            /* assume 2.1 or later */
        pAbfBtInfo->DefaultAudioDeviceLmpVersion = 4;

        if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"1.0") != NULL) {
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 0;
        } else if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"1.1") != NULL) {
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 1;
        } else if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"1.2") != NULL) {
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 2;
        } else if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"2.0") != NULL) {
            /* NOTE: contrary to what the DBUS documentation says, the BT string will
             * not indicate +EDR to indiate the remote device is EDR capable!
             * */
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 3;
        }

        if (pAbfBtInfo->DefaultAudioDeviceLmpVersion >= 3) {
            A_BOOL EDRCapable = FALSE;
                /* double check that the device is EDR capable, a 2.0 device can be EDR or non EDR */
            status = CheckRemoteDeviceEDRCapable(pAbfBtInfo, &EDRCapable);
            if (A_SUCCESS(status)) {
                if (!EDRCapable) {
                     A_INFO("Remote Audio Device (%s) is not EDR Capable, downgrading lmp version.. \n",
                                pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
                        /* for audio coex, treat this like a 1.2 device */
                    pAbfBtInfo->DefaultAudioDeviceLmpVersion = 2;
                }
            }
        }
        pAbfBtInfo->DefaultRemoteAudioDevicePropsValid = TRUE;

    } while (FALSE);

}

#else

static void GetBtAudioDeviceProperties(ABF_BT_INFO  *pAbfBtInfo)
{
    A_STATUS    status;

    pAbfBtInfo->DefaultRemoteAudioDevicePropsValid = FALSE;

    do {
            /* Device address */
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->DefaultAudioDeviceName,
                              AUDIO_DEVICE_INTERFACE,
                              "GetAddress",
                              NULL,
                              DBUS_TYPE_INVALID,
                              0,
                              pAbfBtInfo->DefaultRemoteAudioDeviceAddress,
                              DBUS_TYPE_STRING,
                              sizeof(pAbfBtInfo->DefaultRemoteAudioDeviceAddress));

        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to get address \n", __FUNCTION__);
            break;
        }

        A_INFO("Connected audio device address: %s  \n", pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->HCI_AdapterName,
                              ADAPTER_INTERFACE,
                              "GetRemoteVersion",
                              pAbfBtInfo->DefaultRemoteAudioDeviceAddress,
                              DBUS_TYPE_STRING,
                              strlen(pAbfBtInfo->DefaultRemoteAudioDeviceAddress) + 1,
                              pAbfBtInfo->DefaultRemoteAudioDeviceVersion,
                              DBUS_TYPE_STRING,
                              sizeof(pAbfBtInfo->DefaultRemoteAudioDeviceVersion));

        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to get remote version \n", __FUNCTION__);
            break;
        }

        A_INFO("Connected audio device remote version: %s \n",
                pAbfBtInfo->DefaultRemoteAudioDeviceVersion);


            /* assume 2.1 or later */
        pAbfBtInfo->DefaultAudioDeviceLmpVersion = 4;

        if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"1.0") != NULL) {
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 0;
        } else if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"1.1") != NULL) {
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 1;
        } else if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"1.2") != NULL) {
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 2;
        } else if (strstr(pAbfBtInfo->DefaultRemoteAudioDeviceVersion,"2.0") != NULL) {
            /* NOTE: contrary to what the DBUS documentation says, the BT string will
             * not indicate +EDR to indiate the remote device is EDR capable!
             * */
            pAbfBtInfo->DefaultAudioDeviceLmpVersion = 3;
        }

        if (pAbfBtInfo->DefaultAudioDeviceLmpVersion >= 3) {
            A_BOOL EDRCapable = FALSE;
                /* double check that the device is EDR capable, a 2.0 device can be EDR or non EDR */
            status = CheckRemoteDeviceEDRCapable(pAbfBtInfo, &EDRCapable);
            if (A_SUCCESS(status)) {
                if (!EDRCapable) {
                     A_INFO("Remote Audio Device (%s) is not EDR Capable, downgrading lmp version.. \n",
                                pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
                        /* for audio coex, treat this like a 1.2 device */
                    pAbfBtInfo->DefaultAudioDeviceLmpVersion = 2;
                }
            }
        }

        pAbfBtInfo->DefaultRemoteAudioDevicePropsValid = TRUE;

    } while (FALSE);

}

#endif

#define LMP_FEATURE_ACL_EDR_2MBPS_BYTE_INDEX  3
#define LMP_FEATURE_ACL_EDR_2MBPS_BIT_MASK    0x2
#define LMP_FEATURE_ACL_EDR_3MBPS_BYTE_INDEX  3
#define LMP_FEATURE_ACL_EDR_3MBPS_BIT_MASK    0x4
#define LMP_FEATURES_LENGTH                   8

#ifdef BLUEZ4_3

#define HCI_REMOTE_COMMAND_TIMEOUT 2000
static A_STATUS GetRemoteAclDeviceHandle(ABF_BT_INFO *pAbfBtInfo, A_UINT16 *pConn_handle)
{
    A_STATUS status = A_OK;
    int i, len, sk = -1;
    struct hci_conn_list_req *connList = NULL;
    struct hci_conn_info *connInfo = NULL;
    do {
        sk = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);

        if (sk < 0) {
            A_ERR("[%s] Failed to get raw BT socket: %d\n", __FUNCTION__, errno);
            status = A_NO_RESOURCE;
            break;
        }

        len = (sizeof(*connInfo)) * ABTH_MAX_CONNECTIONS + sizeof(*connList);
        connList = (struct hci_conn_list_req *)A_MALLOC(len);

        if (connList == NULL) {
            A_DEBUG("No connection found during calling function [%s]\n", __FUNCTION__);
            status = A_NO_MEMORY;
            break;
        }

        A_MEMZERO(connList, len);

        connList->dev_id = pAbfBtInfo->AdapterId;
        connList->conn_num = ABTH_MAX_CONNECTIONS;
        connInfo = connList->conn_info;

        if (ioctl(sk, HCIGETCONNLIST, (void *)connList)) {
            A_ERR("[%s] Failed to get connection list: %d\n", __FUNCTION__, errno);
            status = A_EPERM;
            break;
        }

        for (i = 0; i < connList->conn_num; i++, connInfo++) {
            if (connInfo->type == ACL_LINK) {
                *pConn_handle = connInfo->handle;
                break;
            }
        }

        if (i==connList->conn_num) {
            status = A_ENOENT;
            break;
        }
    } while (0);
    if (connList != NULL) {
        A_FREE(connList);
    }
    if (sk>=0) {
        close(sk);
    }
    return status;
}

static A_STATUS CheckRemoteDeviceEDRCapable(ABF_BT_INFO *pAbfBtInfo, A_BOOL *pEDRCapable)
{
    A_STATUS status;
    A_UINT16 conn_handle;
    A_UCHAR evtBuffer[HCI_MAX_EVENT_SIZE];
    A_UCHAR *eventPtr;
    int eventLen;
    A_UINT8 *lmp_features;
    do {
        status = GetRemoteAclDeviceHandle(pAbfBtInfo, &conn_handle);
        if (A_FAILED(status)) {
            break;
        }
        status = IssueHCICommand(pAbfBtInfo,
                        cmd_opcode_pack(OGF_LINK_CTL, OCF_READ_REMOTE_FEATURES),
                        (A_UCHAR *)&conn_handle,
                        2,
                        HCI_REMOTE_COMMAND_TIMEOUT,
                        evtBuffer,
                        sizeof(evtBuffer),
                        &eventPtr,
                        &eventLen);          

        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to get remote features \n", __FUNCTION__);
            break;
        }

        /* Process LMP Features */
        lmp_features = &eventPtr[3];

        A_DUMP_BUFFER(lmp_features,sizeof(lmp_features),"Remote Device LMP Features:");

        if ((lmp_features[LMP_FEATURE_ACL_EDR_2MBPS_BYTE_INDEX] & LMP_FEATURE_ACL_EDR_2MBPS_BIT_MASK)  ||    
                (lmp_features[LMP_FEATURE_ACL_EDR_3MBPS_BYTE_INDEX] & LMP_FEATURE_ACL_EDR_3MBPS_BIT_MASK)) {
            A_INFO("Device (%s) is EDR capable \n", pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
            *pEDRCapable = TRUE;          
        } else {
            A_INFO("Device (%s) is NOT EDR capable \n", pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
            *pEDRCapable = FALSE;
        }
    } while (0);

    return status;
}

/* This is new function to check remote device LMP version */
static A_STATUS GetRemoteDeviceLMPVersion(ABF_BT_INFO *pAbfBtInfo)
{
    A_STATUS status;    
    A_UINT16 conn_handle;
    A_UCHAR evtBuffer[HCI_MAX_EVENT_SIZE];
    A_UCHAR *eventPtr;
    int eventLen; 

    do {
        status = GetRemoteAclDeviceHandle(pAbfBtInfo, &conn_handle);
        if (A_FAILED(status)) {
            break;
        }
        status = IssueHCICommand(pAbfBtInfo,
                        cmd_opcode_pack(OGF_LINK_CTL, OCF_READ_REMOTE_VERSION),
                        (A_UCHAR *)&conn_handle,
                        2,
                        HCI_REMOTE_COMMAND_TIMEOUT,
                        evtBuffer,
                        sizeof(evtBuffer),
                        &eventPtr,
                        &eventLen);          

        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to get remote Version \n", __FUNCTION__);
            break;
        }

        /* Process LMP Version */

        if (eventPtr[3] == 0) {
            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "1.0");
        } else if (eventPtr[3] == 1) {
            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "1.1");
        } else if (eventPtr[3] == 2) {
            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "1.2");
        } else if (eventPtr[3] == 3) {
            strcpy(&pAbfBtInfo->DefaultRemoteAudioDeviceVersion[0], "2.0");
        }

        A_INFO("[%s], Remote Device LMP Version: %d, in string format this is: %s\n", __FUNCTION__, eventPtr[3], 
               pAbfBtInfo->DefaultRemoteAudioDeviceVersion);
    } while (0);

    return status;
}


#else

static A_STATUS CheckRemoteDeviceEDRCapable(ABF_BT_INFO *pAbfBtInfo, A_BOOL *pEDRCapable)
{
    A_STATUS  status = A_OK; 
    A_UINT8   lmp_features[LMP_FEATURES_LENGTH];
    
    do {
        
        A_MEMZERO(lmp_features,sizeof(lmp_features));
        
        status = DoMethodCall(pAbfBtInfo->Bus,
                              BLUEZ_NAME,
                              pAbfBtInfo->HCI_AdapterName,
                              ADAPTER_INTERFACE,
                              "GetRemoteFeatures",
                              pAbfBtInfo->DefaultRemoteAudioDeviceAddress,
                              DBUS_TYPE_STRING,
                              strlen(pAbfBtInfo->DefaultRemoteAudioDeviceAddress) + 1,
                              lmp_features,
                              DBUS_TYPE_ARRAY,
                              sizeof(lmp_features));
        
        if (A_FAILED(status)) {
            A_ERR("[%s] Failed to get remote features \n", __FUNCTION__);
            break;    
        }
        
        A_DUMP_BUFFER(lmp_features,sizeof(lmp_features),"Remote Device LMP Features:");
        
        if ((lmp_features[LMP_FEATURE_ACL_EDR_2MBPS_BYTE_INDEX] & LMP_FEATURE_ACL_EDR_2MBPS_BIT_MASK)  ||    
                (lmp_features[LMP_FEATURE_ACL_EDR_3MBPS_BYTE_INDEX] & LMP_FEATURE_ACL_EDR_3MBPS_BIT_MASK)) {
            A_INFO("Device (%s) is EDR capable \n", pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
            *pEDRCapable = TRUE;          
        } else {
            A_INFO("Device (%s) is NOT EDR capable \n", pAbfBtInfo->DefaultRemoteAudioDeviceAddress);
            *pEDRCapable = FALSE;
        }
        
    } while (FALSE);

    return status;  
}

#endif

static void GetBtAudioConnectionProperties(ABF_BT_INFO              *pAbfBtInfo,
                                           ATHBT_STATE_INDICATION   Indication)
{
    A_UCHAR     role = 0;
    A_CHAR      *pDescr = NULL;
    A_STATUS    status;

    do {
        if (!pAbfBtInfo->DefaultRemoteAudioDevicePropsValid) {
            break;
        }

        /* Incases where HciX is not supported, don't check for the role */
        if((pAbfBtInfo->pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING)) {
	        return;
        }
            /* get role */
        status = GetConnectedDeviceRole(pAbfBtInfo,
                                        pAbfBtInfo->DefaultRemoteAudioDeviceAddress,
                                        Indication == ATH_BT_A2DP ?FALSE : TRUE,
                                        &role);
        if (A_FAILED(status)) {
            role = 0;
        }
        if (Indication == ATH_BT_A2DP) {
            pDescr = "A2DP";
            pAbfBtInfo->pInfo->A2DPConnection_LMPVersion = pAbfBtInfo->DefaultAudioDeviceLmpVersion;
            pAbfBtInfo->pInfo->A2DPConnection_Role = role;
        } else if (Indication == ATH_BT_SCO) {
            if (pAbfBtInfo->CurrentSCOLinkType == SCO_LINK) {
                pDescr = "SCO";
            } else {
                pDescr = "eSCO";
            }
            pAbfBtInfo->pInfo->SCOConnection_LMPVersion = pAbfBtInfo->DefaultAudioDeviceLmpVersion;
            pAbfBtInfo->pInfo->SCOConnection_Role = role;

            if((pAbfBtInfo->pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING)) {

                pAbfBtInfo->pInfo->SCOConnectInfo.Valid = TRUE;
            }else{
                /* for SCO connections check if the event filter captured
                 * the SYNCH connection complete event */
                CheckHciEventFilter(pAbfBtInfo);
            }
        } else {
            pDescr = "UNKNOWN!!";
        }

        A_INFO("BT Audio connection properties:  (%s) (role: %s, lmp version: %d) \n",
               pDescr, role ? "SLAVE" : "MASTER", pAbfBtInfo->DefaultAudioDeviceLmpVersion);

    } while (FALSE);

}


static A_STATUS WaitForHCIEvent(int         Socket, 
                                int         TimeoutMs, 
                                A_UCHAR     *pBuffer,
                                int         MaxLength,
                                A_UCHAR     EventCode, 
                                A_UINT16    OpCode,
                                A_UCHAR     **ppEventPtr,
                                int         *pEventLength)
{
    
    int                     eventLen;
    hci_event_hdr           *eventHdr;
    struct pollfd           pfd;
    int                     result;
    A_UCHAR                 *eventPtr; 
    A_STATUS                status = A_OK;
    
    *ppEventPtr = NULL;
    A_MEMZERO(&pfd,sizeof(pfd));
    pfd.fd = Socket;
    pfd.events = POLLIN;

    if (EventCode == EVT_CMD_COMPLETE) {
        A_INFO("Waiting for HCI CMD Complete Event, Opcode: 0x%4.4X (%d MS) \n",OpCode, TimeoutMs);     
    } else {
        A_INFO("Waiting for HCI Event: %d (%d MS) \n",EventCode, TimeoutMs);
    }

    while (1) {

            /* check socket for a captured event using a short timeout
             * the caller usually calls this function when it knows there
             * is an event that is likely to be captured */
        result = poll(&pfd, 1, TimeoutMs);
        
        if (result < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                /* interrupted */
            } else {
                A_ERR("[%s] Socket Poll Failed! : %d \n", __FUNCTION__, errno);
                status = A_ERROR;
            }
            break;
        }
        
        if (result == 0) {
            A_ERR("[%s], poll returned with 0 \n",__FUNCTION__);
            status = A_ERROR;
            break;
        }

        if (!(pfd.revents & POLLIN)) {
            A_ERR("[%s], POLLIN check failed\n",__FUNCTION__);
            status = A_ERROR;
            break;
        }
            /* get the packet */
        eventLen = read(Socket, pBuffer, MaxLength);
        if (eventLen == 0) {
            /* no event */
            A_INFO("[%s], No Event\n",__FUNCTION__);
            status = A_ERROR;
            break;
        }
        if(eventLen > MaxLength) {
            A_ERR("[%s] Length longer than expected (%d) : %d \n", __FUNCTION__, MaxLength,
                                                                  eventLen);
            status = A_ERROR;
            break;
        }
        if (eventLen < (1 + HCI_EVENT_HDR_SIZE)) {
            A_ERR("[%s] Unknown receive packet! len : %d \n", __FUNCTION__, eventLen);
            status = A_ERROR;
            break;
        }
        if (pBuffer[0] != HCI_EVENT_PKT) {
            A_ERR("[%s] Unsupported packet type : %d \n", __FUNCTION__, pBuffer[0]);
            status = A_ERROR;
            break;
        }

        eventPtr = &pBuffer[1];
        eventLen--;
        eventHdr = (hci_event_hdr *)eventPtr;
        eventPtr += HCI_EVENT_HDR_SIZE;
        eventLen -= HCI_EVENT_HDR_SIZE;

        if (eventHdr->evt != EventCode) {
                /* not interested in this one */
            continue;
        }
        if(eventPtr == NULL) {
            A_ERR("[%s] Socket read points to NULL\n", __FUNCTION__);
            status = A_ERROR;
            break;
        }
        if (eventHdr->evt == EVT_CMD_COMPLETE) {
            if (eventLen < sizeof(evt_cmd_complete)) {
                A_ERR("[%s] EVT_CMD_COMPLETE event is too small! len=%d \n", __FUNCTION__, eventLen);
                status = A_ERROR;
                break;
            } else {
                A_UINT16 evOpCode = btohs(BTEV_CMD_COMPLETE_GET_OPCODE(eventPtr));
                    /* check for opCode match */
                if (OpCode != evOpCode) {
                    /* keep searching */
                    continue;
                }
            }
        }
         /* found it */
        *ppEventPtr = eventPtr;
        *pEventLength = eventLen;

        break;

    }

    return status;
}

static void CheckHciEventFilter(ABF_BT_INFO   *pAbfBtInfo)
{
    A_UCHAR     buffer[HCI_MAX_EVENT_SIZE];
    A_STATUS    status;
    A_UCHAR     *eventPtr;
    int         eventLen;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;

    do {

        if (!USE_DBUS_FOR_HEADSET_PROFILE(pInfo)) {
            A_ERR("Calling CheckHciEventFilter is not valid in this mode! \n");
            break;
        }

        status = WaitForHCIEvent(pAbfBtInfo->HCIEventListenerSocket,
                                 100,
                                 buffer,
                                 sizeof(buffer),
                                 EVT_SYNC_CONN_COMPLETE,
                                 0,
                                 &eventPtr,
                                 &eventLen);

        if (A_FAILED(status)) {
            break;
        }

        if (eventPtr == NULL) {
            break;
        }

        if (eventLen < sizeof(evt_sync_conn_complete)) {
            A_ERR("SYNC_CONN_COMPLETE Event is too small! : %d \n", eventLen);
            break;
        }

        pAbfBtInfo->pInfo->SCOConnectInfo.LinkType = BTEV_GET_BT_CONN_LINK_TYPE(eventPtr);
        pAbfBtInfo->pInfo->SCOConnectInfo.TransmissionInterval = BTEV_GET_TRANS_INTERVAL(eventPtr);
        pAbfBtInfo->pInfo->SCOConnectInfo.RetransmissionInterval = BTEV_GET_RETRANS_INTERVAL(eventPtr);
        pAbfBtInfo->pInfo->SCOConnectInfo.RxPacketLength = BTEV_GET_RX_PKT_LEN(eventPtr);
        pAbfBtInfo->pInfo->SCOConnectInfo.TxPacketLength = BTEV_GET_TX_PKT_LEN(eventPtr);

        A_INFO("HCI SYNC_CONN_COMPLETE event captured, conn info (%d, %d, %d, %d, %d) \n",
                pAbfBtInfo->pInfo->SCOConnectInfo.LinkType,
                pAbfBtInfo->pInfo->SCOConnectInfo.TransmissionInterval,
                pAbfBtInfo->pInfo->SCOConnectInfo.RetransmissionInterval,
                pAbfBtInfo->pInfo->SCOConnectInfo.RxPacketLength,
                pAbfBtInfo->pInfo->SCOConnectInfo.TxPacketLength);

            /* now valid */
        pAbfBtInfo->pInfo->SCOConnectInfo.Valid = TRUE;

    } while (FALSE);

}

static void CleanupHciEventFilter(ABF_BT_INFO *pAbfBtInfo)
{
    A_STATUS status;

    if (pAbfBtInfo->HCIEventListenerSocket >= 0) {
        pAbfBtInfo->HCIFilterThreadShutdown = TRUE;
            /* close socket, if there is a thread waiting on this socket, it will error and then exit */
        close(pAbfBtInfo->HCIEventListenerSocket);
        pAbfBtInfo->HCIEventListenerSocket = -1;

        if (pAbfBtInfo->HCIFilterThreadCreated) {
            A_INFO("[%s] Waiting for HCI filter thread to exit... \n", 
                      __FUNCTION__);
                /* wait for thread to exit 
                 * note: JOIN cleans up thread resources as per POSIX spec. */
            status = A_TASK_JOIN(&pAbfBtInfo->hBtHCIFilterThread);
            if (A_FAILED(status)) {
                A_ERR("[%s] Failed to JOIN HCI filter thread \n", 
                      __FUNCTION__);
            }
            A_MEMZERO(&pAbfBtInfo->hBtHCIFilterThread,sizeof(pAbfBtInfo->hBtHCIFilterThread));
            pAbfBtInfo->HCIFilterThreadCreated = FALSE;     
        }   
        
        pAbfBtInfo->HCIFilterThreadShutdown = FALSE; 
    }    
    
}


static A_STATUS SetupHciEventFilter(ABF_BT_INFO *pAbfBtInfo)
{
    A_STATUS            status = A_ERROR;
    struct hci_filter   filterSetting;
    struct sockaddr_hci addr;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    do {

        if (pAbfBtInfo->HCIEventListenerSocket >= 0) {
                /* close previous */
            CleanupHciEventFilter(pAbfBtInfo);
        }

        pAbfBtInfo->HCIEventListenerSocket = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);

        if (pAbfBtInfo->HCIEventListenerSocket < 0) {
            A_ERR("[%s] Failed to get raw BT socket: %d \n", __FUNCTION__, errno);
            break;
        }

        hci_filter_clear(&filterSetting);
        hci_filter_set_ptype(HCI_EVENT_PKT,  &filterSetting);
            /* To caputre INQUIRY command capture */
        hci_filter_set_ptype(HCI_COMMAND_PKT, &filterSetting);

        /* capture SYNC_CONN Complete */
        hci_filter_set_event(EVT_SYNC_CONN_COMPLETE, &filterSetting);

        /* Capture INQUIRY_COMPLETE event  */
        hci_filter_set_event(EVT_INQUIRY_COMPLETE, &filterSetting);
        hci_filter_set_event(EVT_CONN_REQUEST, &filterSetting);
        hci_filter_set_event(EVT_PIN_CODE_REQ, &filterSetting);
        hci_filter_set_event(EVT_LINK_KEY_REQ, &filterSetting);
        hci_filter_set_event(EVT_CONN_COMPLETE, &filterSetting);
        hci_filter_set_event(EVT_LINK_KEY_NOTIFY, &filterSetting);



        if (!USE_DBUS_FOR_HEADSET_PROFILE(pInfo)) {
                /* if we are not using DBUS for the headset profile, we need
                 * to capture other HCI event packets */
            hci_filter_set_event(EVT_DISCONN_COMPLETE, &filterSetting);
            hci_filter_set_event(EVT_CONN_COMPLETE, &filterSetting);
        }

        if (setsockopt(pAbfBtInfo->HCIEventListenerSocket,
                       SOL_HCI,
                       HCI_FILTER,
                       &filterSetting,
                       sizeof(filterSetting)) < 0) {
            A_ERR("[%s] Failed to set socket opt: %d \n", __FUNCTION__, errno);
            break;
        }

        A_MEMZERO(&addr,sizeof(addr));
            /* bind to the current adapter */
        addr.hci_family = AF_BLUETOOTH;
        addr.hci_dev = pAbfBtInfo->AdapterId;

        if (bind(pAbfBtInfo->HCIEventListenerSocket,
                 (struct sockaddr *)&addr,
                 sizeof(addr)) < 0) {
            A_ERR("[%s] Can't bind to hci:%d (err:%d) \n", __FUNCTION__, pAbfBtInfo->AdapterId, errno);
            break;
        }

        A_INFO("BT Event Filter Set, Mask: 0x%8.8X:%8.8X \n",
            filterSetting.event_mask[1], filterSetting.event_mask[0]);
#ifdef BLUEZ4_3
        if (1) {
#else
        if (!USE_DBUS_FOR_HEADSET_PROFILE(pInfo)) {
#endif
            /* spawn a thread that will capture HCI events from the adapter */
            status = A_TASK_CREATE(&pAbfBtInfo->hBtHCIFilterThread, HCIFilterThread, pAbfBtInfo);
            if (A_FAILED(status)) {
                A_ERR("[%s] Failed to spawn a BT thread\n", __FUNCTION__);
                break;
            }
            pAbfBtInfo->HCIFilterThreadCreated = TRUE;
        }

        status = A_OK;

    } while (FALSE);

    if (A_FAILED(status)) {
        CleanupHciEventFilter(pAbfBtInfo);
    }

    return status;
}


    /* issue HCI command, currently this ONLY supports simple commands that
     * only expect a command complete, the event pointer returned points to the command
     * complete event structure for the caller to decode */
static A_STATUS IssueHCICommand(ABF_BT_INFO *pAbfBtInfo,
                                A_UINT16    OpCode,
                                A_UCHAR     *pCmdData,
                                int         CmdLength,
                                int         EventRecvTimeoutMS,
                                A_UCHAR     *pEventBuffer,
                                int         MaxLength,
                                A_UCHAR     **ppEventPtr,
                                int         *pEventLength)
{
    A_STATUS            status = A_ERROR;
    A_UCHAR             hciType = HCI_COMMAND_PKT;
    hci_command_hdr     hciCommandHdr;
    struct  iovec       iv[3];
    int                 ivcount = 0;
    int                 sk,result;
    struct hci_filter   filterSetting;
    struct sockaddr_hci addr;

    do {

        sk = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);

        if (sk < 0) {
            A_ERR("[%s] Failed to get raw BT socket: %d \n", __FUNCTION__, errno);
            break;
        }

        hciCommandHdr.opcode = htobs(OpCode);
        hciCommandHdr.plen= CmdLength;

        iv[0].iov_base = &hciType;
        iv[0].iov_len  = 1;
        ivcount++;
        iv[1].iov_base = &hciCommandHdr;
        iv[1].iov_len  = HCI_COMMAND_HDR_SIZE;
        ivcount++;

        if (pCmdData != NULL) {
            iv[2].iov_base = pCmdData;
            iv[2].iov_len  = CmdLength;
            ivcount++;
        }

            /* setup socket to capture the event */
        if (OpCode == cmd_opcode_pack(OGF_LINK_CTL, OCF_READ_REMOTE_FEATURES)) {
            hci_filter_clear(&filterSetting);
            hci_filter_set_ptype(HCI_EVENT_PKT, &filterSetting);
            hci_filter_set_event(EVT_READ_REMOTE_FEATURES_COMPLETE, &filterSetting);
        } else if (OpCode == cmd_opcode_pack(OGF_LINK_CTL, OCF_READ_REMOTE_VERSION)) {
            hci_filter_clear(&filterSetting);
            hci_filter_set_ptype(HCI_EVENT_PKT, &filterSetting);
            hci_filter_set_event(EVT_READ_REMOTE_VERSION_COMPLETE, &filterSetting); 
        }
        else {
            hci_filter_clear(&filterSetting);
            hci_filter_set_ptype(HCI_EVENT_PKT,  &filterSetting);
            hci_filter_set_event(EVT_CMD_COMPLETE, &filterSetting);
        }
    
        if (setsockopt(sk, SOL_HCI, HCI_FILTER, &filterSetting, sizeof(filterSetting)) < 0) {
            A_ERR("[%s] Failed to set socket opt: %d \n", __FUNCTION__, errno);
            break;
        }
    
        A_MEMZERO(&addr,sizeof(addr));
        addr.hci_family = AF_BLUETOOTH;
        addr.hci_dev = pAbfBtInfo->AdapterId;
        
        if (bind(sk,(struct sockaddr *)&addr, sizeof(addr)) < 0) {
            A_ERR("[%s] Can't bind to hci:%d (err:%d) \n", __FUNCTION__, pAbfBtInfo->AdapterId, errno);
            break;
        }
        
        while ((result = writev(sk, iv, ivcount)) < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            break;            
        }
        
        if (result <= 0) {
            A_ERR("[%s] Failed to write to hci:%d (err:%d) \n", __FUNCTION__, pAbfBtInfo->AdapterId, errno);
            break;    
        }
        
        /* To support new HCI Commands */
        switch (OpCode) {
        case cmd_opcode_pack(OGF_LINK_CTL, OCF_READ_REMOTE_FEATURES):
            status = WaitForHCIEvent(sk,
                                     EventRecvTimeoutMS,
                                     pEventBuffer,
                                     MaxLength,
                                     EVT_READ_REMOTE_FEATURES_COMPLETE,
                                     OpCode,
                                     ppEventPtr,
                                     pEventLength); 
            break;
        case cmd_opcode_pack(OGF_LINK_CTL, OCF_READ_REMOTE_VERSION):
            status = WaitForHCIEvent(sk,
                                     EventRecvTimeoutMS,
                                     pEventBuffer,
                                     MaxLength,
                                     EVT_READ_REMOTE_VERSION_COMPLETE,
                                     OpCode,
                                     ppEventPtr,
                                     pEventLength); 
            break;
        default:              
            status = WaitForHCIEvent(sk,
                                     EventRecvTimeoutMS,
                                     pEventBuffer,
                                     MaxLength,
                                     EVT_CMD_COMPLETE, 
                                     OpCode,
                                     ppEventPtr,
                                     pEventLength);
            break;
        }
                    
        if (A_FAILED(status)) {
            break;    
        }
        
        status = A_OK;
        
    } while (FALSE);
    
    if (sk >= 0) {
        close(sk);    
    }
    
    return status;
}

#define AFH_CHANNEL_MAP_BYTES  10

typedef struct _WLAN_CHANNEL_MAP {
    A_UCHAR  Map[AFH_CHANNEL_MAP_BYTES];
} WLAN_CHANNEL_MAP;

#define MAX_WLAN_CHANNELS 14

typedef struct _WLAN_CHANNEL_RANGE {
    int    ChannelNumber;
    int    Center;       /* in Mhz */
} WLAN_CHANNEL_RANGE;

const WLAN_CHANNEL_RANGE g_ChannelTable[MAX_WLAN_CHANNELS] = {
    { 1  , 2412},
    { 2  , 2417},
    { 3  , 2422},
    { 4  , 2427},
    { 5  , 2432},
    { 6  , 2437},
    { 7  , 2442},
    { 8  , 2447},
    { 9  , 2452},
    { 10 , 2457},
    { 11 , 2462},
    { 12 , 2467},
    { 13 , 2472},
    { 14 , 2484},
};

static WLAN_CHANNEL_MAP g_ChannelMapTable[MAX_WLAN_CHANNELS + 1] = {
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 0 -- no WLAN */
    { {0x00,0x00,0xC0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 1 */
    { {0x0F,0x00,0x00,0xF8,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 2 */
    { {0xFF,0x01,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 3 */
    { {0xFF,0x3F,0x00,0x00,0xE0,0xFF,0xFF,0xFF,0xFF,0x7F}}, /* 4 */
    { {0xFF,0xFF,0x07,0x00,0x00,0xFC,0xFF,0xFF,0xFF,0x7F}}, /* 5 */
    { {0xFF,0xFF,0xFF,0x00,0x00,0x80,0xFF,0xFF,0xFF,0x7F}}, /* 6 */
    { {0xFF,0xFF,0xFF,0x1F,0x00,0x00,0xF0,0xFF,0xFF,0x7F}}, /* 7 */
    { {0xFF,0xFF,0xFF,0xFF,0x03,0x00,0x00,0xFE,0xFF,0x7F}}, /* 8 */
    { {0xFF,0xFF,0xFF,0xFF,0x7F,0x00,0x00,0xC0,0xFF,0x7F}}, /* 9 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0x00,0x00,0xF8,0x7F}}, /* 10 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x01,0x00,0x00,0x7F}}, /* 11 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x3F,0x00,0x00,0x60}}, /* 12 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0x00,0x00}}, /* 13 */
    { {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x00}}, /* 14 */
};

#define AFH_COMMAND_COMPLETE_TIMEOUT_MS 2000

static int LookUpChannel(int FreqMhz)
{
    int i;
    
    if (FreqMhz == 0) {
            /* not connected */
        return 0;    
    }
    
    for (i = 0; i < MAX_WLAN_CHANNELS; i++) {
        if (FreqMhz <= g_ChannelTable[i].Center) {
            break;
        }
    }
    return (i < MAX_WLAN_CHANNELS) ? g_ChannelTable[i].ChannelNumber : 0;
}

static A_STATUS IssueAFHChannelClassification(ABF_BT_INFO *pAbfBtInfo, int CurrentWLANChannel)
{  
    A_UCHAR     evtBuffer[HCI_MAX_EVENT_SIZE];  
    A_STATUS    status;
    A_UCHAR     *eventPtr;
    int         eventLen; 
    A_UCHAR     *pChannelMap;
    
    A_INFO("WLAN Operating Channel: %d \n", CurrentWLANChannel);
       
    if (CurrentWLANChannel > MAX_WLAN_CHANNELS) {
            /* check if this is expressed in Mhz */
        if (CurrentWLANChannel >= 2412) {
                /* convert Mhz into a channel number */
            CurrentWLANChannel = LookUpChannel(CurrentWLANChannel);    
        } else {
            return A_ERROR;    
        } 
    }
          
    pChannelMap = &(g_ChannelMapTable[CurrentWLANChannel].Map[0]);    
    
    do {
    
        status = IssueHCICommand(pAbfBtInfo,
                                 cmd_opcode_pack(3,0x3F),
                                 pChannelMap, 
                                 AFH_CHANNEL_MAP_BYTES,
                                 AFH_COMMAND_COMPLETE_TIMEOUT_MS,
                                 evtBuffer,
                                 sizeof(evtBuffer),
                                 &eventPtr,
                                 &eventLen);
                    
        
        if (A_FAILED(status)) {
            break;    
        }
        
        status = A_ERROR;
        
        if (eventPtr == NULL) {    
            A_ERR("[%s] Failed to capture AFH command complete event \n", __FUNCTION__);
            break;    
        }
        
        if (eventLen < (sizeof(evt_cmd_complete) + 1)) {
            A_ERR("[%s] not enough bytes in AFH command complete event %d \n", __FUNCTION__, eventLen);
            break;    
        }
        
            /* check status parameter that follows the command complete event body */
        if (eventPtr[sizeof(evt_cmd_complete)] != 0) {
            A_ERR("[%s] AFH command complete event indicated failure : %d \n", __FUNCTION__, 
                eventPtr[sizeof(evt_cmd_complete)]);
            break;
        }
        
        A_INFO(" AFH Command successfully issued \n");
        //A_DUMP_BUFFER(pChannelMap, AFH_CHANNEL_MAP_BYTES, "AFH Channel Classification Map");
                  
        status = A_OK;
         
    } while (FALSE);
                                 
    return status;              
}

void IndicateCurrentWLANOperatingChannel(ATHBT_FILTER_INFO *pFilterInfo, int CurrentWLANChannel)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)pFilterInfo->pBtInfo;
    ATHBT_FILTER_INFO *pInfo = pAbfBtInfo->pInfo;

    if (NULL == pAbfBtInfo) {
        return;
    }

    if (pFilterInfo->Flags & ABF_ENABLE_AFH_CHANNEL_CLASSIFICATION) {
        IssueAFHChannelClassification(pAbfBtInfo,CurrentWLANChannel);
    }

    if(pInfo->Flags & ABF_USE_ONLY_DBUS_FILTERING) {
        Abf_IssueAFHViaHciLib(pAbfBtInfo, CurrentWLANChannel);
    }
}

#ifdef BLUEZ4_3
static void *HCIFilterThread(void *arg)
{
    ABF_BT_INFO            *pAbfBtInfo = (ABF_BT_INFO *)arg;
    ATHBT_FILTER_INFO      *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;
    A_UINT8                 buffer[300];
    A_UINT8                 *pBuffer;
    int                     eventLen;

    A_INFO("[%s] starting up \n", __FUNCTION__);

    while (1) {

        pBuffer = buffer;

        if (pAbfBtInfo->HCIFilterThreadShutdown) {
            break;
        }
            /* get the packet */
        eventLen = read(pAbfBtInfo->HCIEventListenerSocket, pBuffer, sizeof(buffer));

        if (eventLen < 0) {
            if (!pAbfBtInfo->HCIFilterThreadShutdown) {
                A_ERR("[%s] socket error %d \n", __FUNCTION__, eventLen);
            }
            break;
        }

        if (eventLen == 0) {
            /* no event */
            continue;
        }

        if (eventLen < (1 + HCI_EVENT_HDR_SIZE)) {
            A_ERR("[%s] Unknown receive packet! len : %d \n", __FUNCTION__, eventLen);
            continue;
        }

            /* first byte is a tag for the HCI packet type, we only care about events */
        if (pBuffer[0] == HCI_EVENT_PKT) {
            /* pass this raw HCI event to the filter core */
            AthBtFilterHciEvent(pInstance,&pBuffer[1],eventLen - 1);
            A_UINT8 *eventCode = &pBuffer[1];
                /* revive deprecated "DiscoveryCompleted" signal in BlueZ 4.x */
            if (*eventCode == EVT_INQUIRY_COMPLETE) {
                A_DEBUG("Device Inquiry Completed\n");
                pAbfBtInfo->btInquiryState &= ~(1 << 0);
                if(!pAbfBtInfo->btInquiryState) {
                    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
                }
            }
            if (*eventCode == EVT_PIN_CODE_REQ) {
                A_DEBUG("Pin Code Request\n");
                pAbfBtInfo->btInquiryState |= (1 << 0xF);
                AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_ON);
            }
            if (*eventCode == EVT_LINK_KEY_NOTIFY) {
                A_DEBUG("link key notify\n");
                pAbfBtInfo->btInquiryState &= ~(1 << 0xF);
                if(!pAbfBtInfo->btInquiryState) {
                    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
                }
            }

            if(*eventCode == EVT_CONN_COMPLETE) {
                A_DEBUG("Conn complete\n");
                pAbfBtInfo->btInquiryState &= ~(1 << 2);
                if(!pAbfBtInfo->btInquiryState) {
                    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
                }
            }
                /* revive deprecated "DiscoveryStarted" signal by capturing INQUIRY commands */
        } else if (pBuffer[0] == HCI_COMMAND_PKT) {
            A_UINT16 *packedOpCode = (A_UINT16 *)&pBuffer[1];
            if(cmd_opcode_ogf(*packedOpCode) == 0x1 &&
               cmd_opcode_ocf(*packedOpCode) == 0x5 )
            {
                A_DEBUG("Bt-Connect\n");
                pAbfBtInfo->btInquiryState |= (1 << 2);
                AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_ON);
            }


            if (*packedOpCode == cmd_opcode_pack(OGF_LINK_CTL, OCF_INQUIRY)
                || *packedOpCode == cmd_opcode_pack(OGF_LINK_CTL, OCF_PERIODIC_INQUIRY)) {
   //             AthBtFilterHciEvent(pInstance,&pBuffer[1],eventLen - 1);
                A_DEBUG("Device Inquiry Started\n");
                pAbfBtInfo->btInquiryState |= (1 << 0);
                AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_ON);
            } else if (*packedOpCode == cmd_opcode_pack(OGF_LINK_CTL, OCF_CREATE_CONN)) {
                    /* "Connected" signal is deprecated and won't record BD_ADDR of remote device connected */
                ba2str((const bdaddr_t *)&pBuffer[4], &pAbfBtInfo->DefaultRemoteAudioDeviceAddress[0]);
            }
        } else {
            A_ERR("[%s] Unsupported packet type : %d \n", __FUNCTION__, buffer[0]);
            continue;
        }
    }

    A_INFO("[%s] exiting \n", __FUNCTION__);

    return NULL;
}

#else

static void *HCIFilterThread(void *arg)
{
    ABF_BT_INFO            *pAbfBtInfo = (ABF_BT_INFO *)arg;
    ATHBT_FILTER_INFO      *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;
    A_UINT8                 buffer[300];
    A_UINT8                 *pBuffer;
    int                     eventLen;
    
    A_INFO("[%s] starting up \n", __FUNCTION__);
    
    while (1) {
        
        pBuffer = buffer;
        
        if (pAbfBtInfo->HCIFilterThreadShutdown) {
            break;    
        }
        
            /* get the packet */
        eventLen = read(pAbfBtInfo->HCIEventListenerSocket, pBuffer, sizeof(buffer));
        
        if (eventLen < 0) {
            if (!pAbfBtInfo->HCIFilterThreadShutdown) {
                A_ERR("[%s] socket error %d \n", __FUNCTION__, eventLen);
            }
            break;    
        }
        
        if (eventLen == 0) {
            /* no event */
            continue;
        }
                
        if (eventLen < (1 + HCI_EVENT_HDR_SIZE)) {
            A_ERR("[%s] Unknown receive packet! len : %d \n", __FUNCTION__, eventLen);
            continue;
        }
        
            /* first byte is a tag for the HCI packet type, we only care about events */
        if (pBuffer[0] != HCI_EVENT_PKT) {
            A_ERR("[%s] Unsupported packet type : %d \n", __FUNCTION__, buffer[0]);
            continue;
        }
        
            /* pass this raw HCI event to the filter core */
        AthBtFilterHciEvent(pInstance,&pBuffer[1],eventLen - 1); 
    }

    A_INFO("[%s] exiting \n", __FUNCTION__);

    return NULL;
}

#endif


