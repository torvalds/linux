//------------------------------------------------------------------------------
// <copyright file="abtfilt_bt.c" company="Atheros">
//    Copyright (c) 2008 Atheros Corporation.  All rights reserved.
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

/*
 * Bluetooth Filter - BT module
 *
 */
static const char athId[] __attribute__ ((unused)) = "$Id: //depot/sw/releases/olca3.1-RC/host/tools/athbtfilter/bluez/abtfilt_bluez_dbus_glib.c#1 $";

#include "abtfilt_bluez_dbus_glib.h"

#include <dbus/dbus-glib.h>
#undef HCI_INQUIRY
#include <bluetooth.h>
#include <hci.h>
#include <hci_lib.h>
#include <sys/poll.h>

/* Definitions */
#define BLUEZ_NAME                        "org.bluez"
#define BLUEZ_PATH                        "/org/bluez"
#define ADAPTER_INTERFACE                 "org.bluez.Adapter"
#define MANAGER_INTERFACE                 "org.bluez.Manager"
#define AUDIO_MANAGER_PATH                "/org/bluez/audio"
#define AUDIO_SINK_INTERFACE              "org.bluez.audio.Sink"
#define AUDIO_SOURCE_INTERFACE            "org.bluez.audio.Source"
#define AUDIO_MANAGER_INTERFACE           "org.bluez.audio.Manager"
#define AUDIO_HEADSET_INTERFACE           "org.bluez.audio.Headset"
#define AUDIO_GATEWAY_INTERFACE           "org.bluez.audio.Gateway"
#define AUDIO_DEVICE_INTERFACE            "org.bluez.audio.Device"

#define BTEV_GET_BT_CONN_LINK_TYPE(p)   ((p)[9])
#define BTEV_GET_TRANS_INTERVAL(p)      ((p)[10])
#define BTEV_GET_RETRANS_INTERVAL(p)    ((p)[11])
#define BTEV_GET_RX_PKT_LEN(p)          ((A_UINT16)((p)[12]) | (((A_UINT16)((p)[13])) << 8))
#define BTEV_GET_TX_PKT_LEN(p)          ((A_UINT16)((p)[14]) | (((A_UINT16)((p)[15])) << 8))
#define BTEV_CMD_COMPLETE_GET_OPCODE(p) ((A_UINT16)((p)[1]) | (((A_UINT16)((p)[2])) << 8))          
#define BTEV_CMD_COMPLETE_GET_STATUS(p) ((p)[3])

typedef enum {
    BT_ADAPTER_ADDED = 0,
    BT_ADAPTER_REMOVED,
    DEVICE_DISCOVERY_STARTED,
    DEVICE_DISCOVERY_FINISHED,
    REMOTE_DEVICE_CONNECTED,
    REMOTE_DEVICE_DISCONNECTED,
    AUDIO_DEVICE_ADDED,
    AUDIO_DEVICE_REMOVED,
    AUDIO_HEADSET_CONNECTED,
    AUDIO_HEADSET_DISCONNECTED,
    AUDIO_HEADSET_STREAM_STARTED,
    AUDIO_HEADSET_STREAM_STOPPED,
    AUDIO_GATEWAY_CONNECTED, /* Not Implemented */
    AUDIO_GATEWAY_DISCONNECTED, /* Not Implemented */
    AUDIO_SINK_CONNECTED,
    AUDIO_SINK_DISCONNECTED,
    AUDIO_SINK_STREAM_STARTED,
    AUDIO_SINK_STREAM_STOPPED,
    AUDIO_SOURCE_CONNECTED, /* Not Implemented */
    AUDIO_SOURCE_DISCONNECTED, /* Not Implemented */
    BT_EVENTS_NUM_MAX,
} BT_STACK_EVENT;

typedef enum {
    PROXY_INVALID = 0,
    DEVICE_MANAGER,
    DEVICE_ADAPTER,
    AUDIO_MANAGER,
    AUDIO_HEADSET,
    AUDIO_GATEWAY,
    AUDIO_SOURCE,
    AUDIO_SINK,
} BT_PROXY_TYPE;

typedef enum {
    ARG_INVALID = 0,
    ARG_NONE,
    ARG_STRING,
} BT_CB_TYPE;

typedef struct _BT_NOTIFICATION_CONFIG_PARAMS {
    const char        *name;
    BT_PROXY_TYPE      proxy;
    BT_CB_TYPE         arg;
} BT_NOTIFICATION_CONFIG_PARAMS;

static BT_NOTIFICATION_CONFIG_PARAMS g_NotificationConfig[BT_EVENTS_NUM_MAX] =
{
    /* BT_ADAPTER_ADDED */
    {"AdapterAdded", DEVICE_MANAGER, ARG_STRING},
    /* BT_ADAPTER_REMOVED */
    {"AdapterRemoved", DEVICE_MANAGER, ARG_STRING},
    /* DEVICE_DISCOVERY_STARTED */
    {"DiscoveryStarted", DEVICE_ADAPTER, ARG_NONE},
    /* DEVICE_DISCOVERY_FINISHED */
    {"DiscoveryCompleted", DEVICE_ADAPTER, ARG_NONE},
    /* REMOTE_DEVICE_CONNECTED */
    {"RemoteDeviceConnected", DEVICE_ADAPTER, ARG_STRING},
    /* REMOTE_DEVICE_DISCONNECTED */
    {"RemoteDeviceDisconnected", DEVICE_ADAPTER, ARG_STRING},
    /* AUDIO_DEVICE_ADDED */
    {"DeviceCreated", AUDIO_MANAGER, ARG_STRING},
    /* AUDIO_DEVICE_REMOVED */
    {"DeviceRemoved", AUDIO_MANAGER, ARG_STRING},
    /* AUDIO_HEADSET_CONNECTED */
    {"Connected", AUDIO_HEADSET, ARG_NONE},
    /* AUDIO_HEADSET_DISCONNECTED */
    {"Disconnected", AUDIO_HEADSET, ARG_NONE},
    /* AUDIO_HEADSET_STREAM_STARTED */
    {"Playing", AUDIO_HEADSET, ARG_NONE},
    /* AUDIO_HEADSET_STREAM_STOPPED */
    {"Stopped", AUDIO_HEADSET, ARG_NONE},
    /* AUDIO_GATEWAY_CONNECTED */
    {NULL, PROXY_INVALID, ARG_INVALID},
    /* AUDIO_GATEWAY_DISCONNECTED */
    {NULL, PROXY_INVALID, ARG_INVALID},
    /* AUDIO_SINK_CONNECTED */
    {"Connected", AUDIO_SINK, ARG_NONE},
    /* AUDIO_SINK_DISCONNECTED */
    {"Disconnected", AUDIO_SINK, ARG_NONE},
    /* AUDIO_SINK_STREAM_STARTED */
    {"Playing", AUDIO_SINK, ARG_NONE},
    /* AUDIO_SINK_STREAM_STOPPED */
    {"Stopped", AUDIO_SINK, ARG_NONE},
    /* AUDIO_SOURCE_CONNECTED */
    {NULL, PROXY_INVALID, ARG_INVALID},
    /* AUDIO_SOURCE_DISCONNECTED */
    {NULL, PROXY_INVALID, ARG_INVALID},
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
static void BtAdapterAdded(DBusGProxy *proxy, const char *string, 
                           gpointer user_data);
static void BtAdapterRemoved(DBusGProxy *proxy, const char *string, 
                             gpointer user_data);
static A_STATUS AcquireBtAdapter(ABF_BT_INFO *pAbfBtInfo);
static void ReleaseBTAdapter(ABF_BT_INFO *pAbfBtInfo);
static void *BtEventThread(void *arg);
static void RegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, 
                                   BT_STACK_EVENT event, GCallback handler);
static void DeRegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, 
                                     BT_STACK_EVENT event, GCallback handler);
static A_STATUS GetAdapterInfo(ABF_BT_INFO *pAbfBtInfo);
static void RemoteDeviceDisconnected(DBusGProxy *proxy, const char *string, 
                                     gpointer user_data);
static void RemoteDeviceConnected(DBusGProxy *proxy, const char *string, 
                                  gpointer user_data);
static void AudioDeviceAdded(DBusGProxy *proxy, const char *string, 
                             gpointer user_data);
static void AudioDeviceRemoved(DBusGProxy *proxy, const char *string, 
                               gpointer user_data);
static void DeviceDiscoveryStarted(DBusGProxy *proxy, gpointer user_data);
static void DeviceDiscoveryFinished(DBusGProxy *proxy, gpointer user_data);
static void AudioHeadsetConnected(DBusGProxy *proxy, gpointer user_data);
static void AudioHeadsetDisconnected(DBusGProxy *proxy, gpointer user_data);
static void AudioHeadsetStreamStarted(DBusGProxy *proxy, gpointer user_data);
static void AudioHeadsetStreamStopped(DBusGProxy *proxy, gpointer user_data);
static void AudioGatewayConnected(DBusGProxy *proxy, gpointer user_data);
static void AudioGatewayDisconnected(DBusGProxy *proxy, gpointer user_data);
static void AudioSinkConnected(DBusGProxy *proxy, gpointer user_data);
static void AudioSinkDisconnected(DBusGProxy *proxy, gpointer user_data);
static void AudioSinkStreamStarted(DBusGProxy *proxy, gpointer user_data);
static void AudioSinkStreamStopped(DBusGProxy *proxy, gpointer user_data);
static void AudioSourceConnected(DBusGProxy *proxy, gpointer user_data);
static void AudioSourceDisconnected(DBusGProxy *proxy, gpointer user_data);
static A_STATUS CheckAndAcquireDefaultAdapter(ABF_BT_INFO *pAbfBtInfo);
static void ReleaseDefaultAdapter(ABF_BT_INFO *pAbfBtInfo);
static void AcquireDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo);
static void ReleaseDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo);
static void GetBtAudioConnectionProperties(ABF_BT_INFO              *pAbfBtInfo,
                                           ATHBT_STATE_INDICATION   Indication);
static A_STATUS SetupHciEventFilter(ABF_BT_INFO *pAbfBtInfo);
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
                                                                           
/* APIs exported to other modules */
A_STATUS
Abf_BtStackNotificationInit(ATH_BT_FILTER_INSTANCE *pInstance, A_UINT32 Flags)
{
    A_STATUS status;
    GMainLoop *mainloop;
    ATHBT_FILTER_INFO *pInfo;
    ABF_BT_INFO *pAbfBtInfo;

    pInfo = (ATHBT_FILTER_INFO *)pInstance->pContext;
    if (pInfo->pBtInfo) {
        return A_OK;
    }

    pAbfBtInfo = (ABF_BT_INFO *)A_MALLOC(sizeof(ABF_BT_INFO));
    A_MEMZERO(pAbfBtInfo,sizeof(ABF_BT_INFO));
    
    A_MUTEX_INIT(&pAbfBtInfo->hWaitEventLock);
    A_COND_INIT(&pAbfBtInfo->hWaitEvent);
    A_MEMZERO(pAbfBtInfo, sizeof(ABF_BT_INFO));

    pAbfBtInfo->Flags = Flags;
    
    if (pAbfBtInfo->Flags & ABF_ENABLE_AFH_CHANNEL_CLASSIFICATION) {
        A_INFO("AFH Classification Command will be issued on WLAN connect/disconnect \n");    
    }
    
    /* Set up the main loop */
    mainloop = g_main_loop_new(NULL, FALSE);
    pAbfBtInfo->AdapterAvailable = FALSE;
    pAbfBtInfo->Mainloop = mainloop;
    pAbfBtInfo->Loop = TRUE;
    pAbfBtInfo->pInfo = pInfo;
    pAbfBtInfo->HCIEventListenerSocket = -1;
    
    /* Spawn a thread which will be used to process events from BT */
    status = A_TASK_CREATE(&pInfo->hBtThread, BtEventThread, pAbfBtInfo);
    if (A_FAILED(status)) {
        A_ERR("[%s] Failed to spawn a BT thread\n", __FUNCTION__);
        return A_ERROR;
    }

    pInfo->pBtInfo = pAbfBtInfo;
    A_INFO("BT Stack Notification init complete\n");

    return A_OK;
}

void
Abf_BtStackNotificationDeInit(ATH_BT_FILTER_INSTANCE *pInstance)
{
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pInstance->pContext;
    ABF_BT_INFO *pAbfBtInfo = pInfo->pBtInfo;

    if (!pAbfBtInfo) return;

    if (pAbfBtInfo->Mainloop != NULL) {
            /* Terminate and wait for the BT Event Handler task to finish */
        A_MUTEX_LOCK(&pAbfBtInfo->hWaitEventLock);
        if (pAbfBtInfo->Loop) {
            pAbfBtInfo->Loop = FALSE;
            A_COND_WAIT(&pAbfBtInfo->hWaitEvent, &pAbfBtInfo->hWaitEventLock, 
                        WAITFOREVER);
        }
        A_MUTEX_UNLOCK(&pAbfBtInfo->hWaitEventLock);
    }
    
    /* Flush all the BT actions from the filter core TODO */

    /* Free the remaining resources */
    g_main_loop_unref(pAbfBtInfo->Mainloop);
    pAbfBtInfo->AdapterAvailable = FALSE;   
    pInfo->pBtInfo = NULL;
    A_MUTEX_DEINIT(&pAbfBtInfo->hWaitEventLock);
    A_COND_DEINIT(&pAbfBtInfo->hWaitEvent);
    A_MEMZERO(pAbfBtInfo, sizeof(ABF_BT_INFO));
    A_FREE(pAbfBtInfo);

    A_INFO("BT Stack Notification de-init complete\n");
}

/* Internal functions */

static gboolean MainLoopQuitCheck(gpointer arg)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)arg;

    /* this is the only way to end a glib main loop
       without creating an external g_source, this check is periodically
       made to check the shutdown flag  */
    if (!pAbfBtInfo->Loop) {
        g_main_loop_quit(pAbfBtInfo->Mainloop);
        return FALSE;
    }
    
        /* reschedule */
    return TRUE;
}

static void *
BtEventThread(void *arg)
{
    DBusGConnection *bus;
    GError *error = NULL;
    DBusGProxy *manager;
    GLogLevelFlags fatal_mask;
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)arg;

    A_INFO("Starting the BT Event Handler task\n");

    g_type_init();

    fatal_mask = g_log_set_always_fatal(G_LOG_FATAL_MASK);
    fatal_mask |= G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL;
    g_log_set_always_fatal(fatal_mask);

    do {
        bus = dbus_g_bus_get(DBUS_BUS_SYSTEM, &error);
        if (!bus) {
            A_ERR("[%s] Couldn't connect to system bus: %d\n", 
                  __FUNCTION__, error);
            break;
        }

        pAbfBtInfo->Bus = bus;
        manager = dbus_g_proxy_new_for_name(bus, BLUEZ_NAME, BLUEZ_PATH, 
                                            MANAGER_INTERFACE);
        if (!manager) {
            A_ERR("[%s] Failed to get name owner\n", __FUNCTION__);
            dbus_g_connection_unref(bus);
            pAbfBtInfo->Bus = NULL;
            break;
        }
        pAbfBtInfo->DeviceManager = manager;

            /* check for default adapter at startup */
        CheckAndAcquireDefaultAdapter(pAbfBtInfo);
        
        RegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_ADDED, 
                               G_CALLBACK(BtAdapterAdded));
        RegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_REMOVED, 
                               G_CALLBACK(BtAdapterRemoved));     
        g_timeout_add(1000, MainLoopQuitCheck, pAbfBtInfo);                       
        g_main_loop_run(pAbfBtInfo->Mainloop);
        
        DeRegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_ADDED, 
                                 G_CALLBACK(BtAdapterAdded));
        DeRegisterBtStackEventCb(pAbfBtInfo, BT_ADAPTER_REMOVED, 
                                 G_CALLBACK(BtAdapterRemoved));
        
        ReleaseDefaultAdapter(pAbfBtInfo);
        
        g_object_unref(pAbfBtInfo->DeviceManager);
        pAbfBtInfo->DeviceManager = NULL;
 
        /* Release the system bus */
        dbus_g_connection_unref(bus);
        pAbfBtInfo->Bus = NULL;
    } while (FALSE);

    /* Clean up the resources allocated in this task */
    A_INFO("Terminating the BT Event Handler task\n");
    A_MUTEX_LOCK(&pAbfBtInfo->hWaitEventLock);
    pAbfBtInfo->Loop = FALSE;
    A_COND_SIGNAL(&pAbfBtInfo->hWaitEvent);
    A_MUTEX_UNLOCK(&pAbfBtInfo->hWaitEventLock);

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
        A_INFO("[%s] BT Adapter Removed\n",pAbfBtInfo->AdapterName);
    }
            
    A_MEMZERO(pAbfBtInfo->AdapterName, sizeof(pAbfBtInfo->AdapterName));
   
}
/* Event Notifications */
static void
BtAdapterAdded(DBusGProxy *proxy, const char *string, gpointer user_data)
{
    A_DEBUG("BtAdapterAdded Proxy Callback ... \n");
    
    /* BUG!!!, the BtAdapterAdded callback is indicated too early by the BT service, on some systems
     * the method call to "DefaultAdapter" through the Manager interface will fail because no 
     * default adapter exist yet even though this callback was indicated (there should be a default)
     * 
     * Workaround is to delay before acquiring the default adapter. 
     * Acquiring the BT adapter should not be very infrequent though.
     * 
     * */
    sleep(1);
    CheckAndAcquireDefaultAdapter((ABF_BT_INFO *)user_data);
}


static void
BtAdapterRemoved(DBusGProxy *proxy, const char *string, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;

    A_DEBUG("BtAdapterRemoved Proxy Callback ... \n");
    
    if (!pAbfBtInfo->AdapterAvailable) return;

    if (strcmp(string,pAbfBtInfo->AdapterName) == 0) {
            /* the adapter we are watching has been removed */
        ReleaseDefaultAdapter(pAbfBtInfo);
    }

}

static void
DeviceDiscoveryStarted(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Inquiry Started\n");
    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_ON);
}

static void
DeviceDiscoveryFinished(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Inquiry Completed\n");
    AthBtIndicateState(pInstance, ATH_BT_INQUIRY, STATE_OFF);
}

static void
RemoteDeviceConnected(DBusGProxy *proxy, const char *string, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Device Connected: %s\n", string);
    A_STR2ADDR(string, pAbfBtInfo->RemoteDevice);
    AthBtIndicateState(pInstance, ATH_BT_CONNECT, STATE_ON);
}

static void
RemoteDeviceDisconnected(DBusGProxy *proxy, const char *string, gpointer user_data)
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
        
    if (pAbfBtInfo->AudioCbRegistered) {
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_CONNECTED, 
                                 G_CALLBACK(AudioHeadsetConnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_DISCONNECTED, 
                                 G_CALLBACK(AudioHeadsetDisconnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STARTED, 
                                 G_CALLBACK(AudioHeadsetStreamStarted));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STOPPED, 
                                 G_CALLBACK(AudioHeadsetStreamStopped));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_CONNECTED, 
                                 G_CALLBACK(AudioGatewayConnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_DISCONNECTED, 
                                 G_CALLBACK(AudioGatewayDisconnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_CONNECTED, 
                                 G_CALLBACK(AudioSinkConnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_DISCONNECTED, 
                                 G_CALLBACK(AudioSinkDisconnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STARTED, 
                                 G_CALLBACK(AudioSinkStreamStarted));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STOPPED, 
                                 G_CALLBACK(AudioSinkStreamStopped));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_CONNECTED, 
                                 G_CALLBACK(AudioSourceConnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_DISCONNECTED, 
                                 G_CALLBACK(AudioSourceDisconnected));
        pAbfBtInfo->AudioCbRegistered = FALSE;
    }

    if (pAbfBtInfo->AudioHeadset != NULL) {
        g_object_unref(pAbfBtInfo->AudioHeadset);
        pAbfBtInfo->AudioHeadset = NULL;
    }

    if (pAbfBtInfo->AudioGateway != NULL) {
        g_object_unref(pAbfBtInfo->AudioGateway);
        pAbfBtInfo->AudioGateway = NULL;
    }
    
    if (pAbfBtInfo->AudioSource != NULL) {
        g_object_unref(pAbfBtInfo->AudioSource);
        pAbfBtInfo->AudioSource = NULL;
    }
    
    if (pAbfBtInfo->AudioSink != NULL) {
        g_object_unref(pAbfBtInfo->AudioSink);
        pAbfBtInfo->AudioSink = NULL;
    }
    
    if (pAbfBtInfo->AudioDevice != NULL) {
        g_object_unref(pAbfBtInfo->AudioDevice);
        pAbfBtInfo->AudioDevice = NULL;
    }
    
    if (pAbfBtInfo->DefaultAudioDeviceAvailable) {
        pAbfBtInfo->DefaultAudioDeviceAvailable = FALSE;
        A_DEBUG("Default Audio Device Removed: %s\n", pAbfBtInfo->DefaultAudioDeviceName);
        A_MEMZERO(pAbfBtInfo->DefaultAudioDeviceName,sizeof(pAbfBtInfo->DefaultAudioDeviceName));
    }
    
}

static void AcquireDefaultAudioDevice(ABF_BT_INFO *pAbfBtInfo)
{
    A_BOOL          success = FALSE;
    char            *audioDevice;
    GError          *error = NULL;
    
    do {
        
        if (pAbfBtInfo->DefaultAudioDeviceAvailable) {
                /* already acquired */
            success = TRUE;
            break;    
        }
        
        A_INFO("Checking for a default audio device .. \n");
             
        if (!dbus_g_proxy_call(pAbfBtInfo->AudioManager, 
                               "DefaultDevice", 
                               &error, 
                               G_TYPE_INVALID, 
                               G_TYPE_STRING, 
                               &audioDevice, 
                               G_TYPE_INVALID)) {
            A_ERR("[%s] DefaultDevice method call failed \n", __FUNCTION__);
            break;                     
        }
        
        if (error != NULL) {
            A_ERR("[%s] Failed to get default audio device: %s \n", __FUNCTION__, error->message);
            g_free(error);
            break;    
        }
                
        strncpy(pAbfBtInfo->DefaultAudioDeviceName, 
                audioDevice, 
                sizeof(pAbfBtInfo->DefaultAudioDeviceName));
        
        g_free(audioDevice);
       
        A_INFO("Default Audio Device: %s \n", pAbfBtInfo->DefaultAudioDeviceName);
        
        pAbfBtInfo->DefaultAudioDeviceAvailable = TRUE;
        
        /* get various proxies for the audio device */
                               
        pAbfBtInfo->AudioHeadset = dbus_g_proxy_new_for_name(pAbfBtInfo->Bus, 
                                                             BLUEZ_NAME, 
                                                             pAbfBtInfo->DefaultAudioDeviceName, 
                                                             AUDIO_HEADSET_INTERFACE);
        if (NULL == pAbfBtInfo->AudioHeadset) {
            A_ERR("[%s] Failed to get audio headset interface \n", __FUNCTION__);
            break;    
        }
        
        pAbfBtInfo->AudioGateway = dbus_g_proxy_new_for_name(pAbfBtInfo->Bus, 
                                                             BLUEZ_NAME, 
                                                             pAbfBtInfo->DefaultAudioDeviceName, 
                                                             AUDIO_GATEWAY_INTERFACE);
        if (NULL == pAbfBtInfo->AudioGateway) {
            A_ERR("[%s] Failed to get audio gateway interface \n", __FUNCTION__);
            break;    
        }
        
        pAbfBtInfo->AudioSource = dbus_g_proxy_new_for_name(pAbfBtInfo->Bus, 
                                                            BLUEZ_NAME, 
                                                            pAbfBtInfo->DefaultAudioDeviceName, 
                                                            AUDIO_SOURCE_INTERFACE);
                                                            
        if (NULL == pAbfBtInfo->AudioSource) {
            A_ERR("[%s] Failed to get audio source interface \n", __FUNCTION__);
            break;    
        }
           
        pAbfBtInfo->AudioSink = dbus_g_proxy_new_for_name(pAbfBtInfo->Bus, 
                                                          BLUEZ_NAME, 
                                                          pAbfBtInfo->DefaultAudioDeviceName, 
                                                          AUDIO_SINK_INTERFACE);

        if (NULL == pAbfBtInfo->AudioSink) {
            A_ERR("[%s] Failed to get audio sink interface \n", __FUNCTION__);
            break;    
        }
        
        pAbfBtInfo->AudioDevice = dbus_g_proxy_new_for_name(pAbfBtInfo->Bus, 
                                                            BLUEZ_NAME, 
                                                            pAbfBtInfo->DefaultAudioDeviceName, 
                                                            AUDIO_DEVICE_INTERFACE);

        if (NULL == pAbfBtInfo->AudioDevice) {
            A_ERR("[%s] Failed to get audio device interface \n", __FUNCTION__);
            break;    
        }
 
            /* Register for audio specific events */
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_CONNECTED, 
                               G_CALLBACK(AudioHeadsetConnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_DISCONNECTED, 
                               G_CALLBACK(AudioHeadsetDisconnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STARTED, 
                               G_CALLBACK(AudioHeadsetStreamStarted));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_HEADSET_STREAM_STOPPED, 
                               G_CALLBACK(AudioHeadsetStreamStopped));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_CONNECTED, 
                               G_CALLBACK(AudioGatewayConnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_GATEWAY_DISCONNECTED, 
                               G_CALLBACK(AudioGatewayDisconnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_CONNECTED,
                               G_CALLBACK(AudioSinkConnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_DISCONNECTED, 
                               G_CALLBACK(AudioSinkDisconnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STARTED,
                               G_CALLBACK(AudioSinkStreamStarted));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SINK_STREAM_STOPPED, 
                               G_CALLBACK(AudioSinkStreamStopped));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_CONNECTED, 
                               G_CALLBACK(AudioSourceConnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_SOURCE_DISCONNECTED, 
                               G_CALLBACK(AudioSourceDisconnected));
                               
        pAbfBtInfo->AudioCbRegistered = TRUE;
        
        success = TRUE;
        
    } while (FALSE);
    
    if (!success) {
            /* cleanup */
        ReleaseDefaultAudioDevice(pAbfBtInfo);       
    }
}

static void
AudioDeviceAdded(DBusGProxy *proxy, const char *string, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    
    A_DEBUG("Audio Device Added: %s\n", string);
        /* release current one if any */
    ReleaseDefaultAudioDevice(pAbfBtInfo);
        /* re-acquire the new default, it could be the same one */
    AcquireDefaultAudioDevice(pAbfBtInfo);    
    
}

static void
AudioDeviceRemoved(DBusGProxy *proxy, const char *string, gpointer user_data)
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
AudioHeadsetConnected(DBusGProxy *proxy, gpointer user_data)
{
    A_DEBUG("Audio Headset Connected\n");
}

static void
AudioHeadsetDisconnected(DBusGProxy *proxy, gpointer user_data)
{
    A_DEBUG("Audio Headset Disconnected\n");
}

static void
AudioHeadsetStreamStarted(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;
    
    A_DEBUG("Audio Headset Stream Started\n");   
        /* get properties of this headset connection */
    GetBtAudioConnectionProperties(pAbfBtInfo, ATH_BT_SCO);
        /* make the indication */
    AthBtIndicateState(pInstance, 
                       pAbfBtInfo->CurrentSCOLinkType == SCO_LINK ? ATH_BT_SCO : ATH_BT_ESCO, 
                       STATE_ON);
}

static void
AudioHeadsetStreamStopped(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;
    
        /* This event can also be used to indicate the SCO state */
    A_DEBUG("Audio Headset Stream Stopped\n");
    AthBtIndicateState(pInstance, 
                       pAbfBtInfo->CurrentSCOLinkType == SCO_LINK ? ATH_BT_SCO : ATH_BT_ESCO, 
                       STATE_OFF);
}

static void
AudioGatewayConnected(DBusGProxy *proxy, gpointer user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Gateway Connected\n");
}

static void
AudioGatewayDisconnected(DBusGProxy *proxy, gpointer user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Gateway disconnected\n");
}

static void
AudioSinkConnected(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    A_DEBUG("Audio Sink Connected\n");    
        /* get connection properties */
    GetBtAudioConnectionProperties(pAbfBtInfo, ATH_BT_A2DP);
}

static void
AudioSinkDisconnected(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Audio Sink Disconnected\n");
    AthBtIndicateState(pInstance, ATH_BT_A2DP, STATE_OFF);
}

static void
AudioSinkStreamStarted(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;
    A_DEBUG("Audio Sink Stream Started\n");
    
    AthBtIndicateState(pInstance, ATH_BT_A2DP, STATE_ON);
}

static void
AudioSinkStreamStopped(DBusGProxy *proxy, gpointer user_data)
{
    ABF_BT_INFO *pAbfBtInfo = (ABF_BT_INFO *)user_data;
    ATHBT_FILTER_INFO *pInfo = (ATHBT_FILTER_INFO *)pAbfBtInfo->pInfo;
    ATH_BT_FILTER_INSTANCE *pInstance = pInfo->pInstance;

    A_DEBUG("Audio Sink Stream Stopped\n");
    AthBtIndicateState(pInstance, ATH_BT_A2DP, STATE_OFF);   
}

static void
AudioSourceConnected(DBusGProxy *proxy, gpointer user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Source Connected\n");
}

static void
AudioSourceDisconnected(DBusGProxy *proxy, gpointer user_data)
{
    /* Not yet implemented */
    A_DEBUG("Audio Source Disconnected\n");
}

/* (De)Registration */
static DBusGProxy *
GetDBusProxy(ABF_BT_INFO *pAbfBtInfo, BT_STACK_EVENT event)
{
    DBusGProxy *proxy = NULL;
    BT_NOTIFICATION_CONFIG_PARAMS *pNotificationConfig;

    pNotificationConfig = &g_NotificationConfig[event];
    if (pNotificationConfig->proxy == DEVICE_MANAGER) {
        proxy = pAbfBtInfo->DeviceManager;
    } else if (pNotificationConfig->proxy == DEVICE_ADAPTER) {
        proxy = pAbfBtInfo->DeviceAdapter;
    } else if (pNotificationConfig->proxy == AUDIO_MANAGER) {
        proxy = pAbfBtInfo->AudioManager;
    } else if (pNotificationConfig->proxy == AUDIO_HEADSET) {
        proxy = pAbfBtInfo->AudioHeadset;
    } else if (pNotificationConfig->proxy == AUDIO_SINK) {
        proxy = pAbfBtInfo->AudioSink;
    } else {
        A_ERR("[%s] Unknown proxy %d for event : %d \n", __FUNCTION__, pNotificationConfig->proxy, event);    
    }

    return proxy;
}

static void
RegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, BT_STACK_EVENT event, 
                       GCallback handler)
{
    const char *name;
    DBusGProxy *proxy;
    BT_NOTIFICATION_CONFIG_PARAMS *pNotificationConfig;

    pNotificationConfig = &g_NotificationConfig[event];
    name = pNotificationConfig->name;

    if (event >= BT_EVENTS_NUM_MAX) {
        A_ERR("[%s] Unknown Event: %d\n", __FUNCTION__, event);
        return;
    }

    if (pNotificationConfig->proxy == PROXY_INVALID) {
            /* not supported yet, so ignore registration */
        return;    
    }
    
    if ((proxy = GetDBusProxy(pAbfBtInfo, event)) == NULL) {
        A_ERR("[%s] Unknown Proxy: %d (event:%d) \n", __FUNCTION__, 
              pNotificationConfig->proxy, event);
        return;
    }

    if (pNotificationConfig->arg == ARG_NONE) {
        dbus_g_proxy_add_signal(proxy, name, G_TYPE_INVALID);
    } else if (pNotificationConfig->arg == ARG_STRING) {
        dbus_g_proxy_add_signal(proxy, name, G_TYPE_STRING, 
                                G_TYPE_INVALID);
    } else {
        A_ERR("[%s] Unkown Arg Type: %d\n", __FUNCTION__, 
              pNotificationConfig->arg);
        return;
    }

    dbus_g_proxy_connect_signal(proxy, name, handler, (void *)pAbfBtInfo, 
                                NULL);
}

static void
DeRegisterBtStackEventCb(ABF_BT_INFO *pAbfBtInfo, BT_STACK_EVENT event, 
                         GCallback handler)
{
    const char *name;
    DBusGProxy *proxy;
    BT_NOTIFICATION_CONFIG_PARAMS *pNotificationConfig;

    pNotificationConfig = &g_NotificationConfig[event];
    name = pNotificationConfig->name;

    if (event >= BT_EVENTS_NUM_MAX) {
        A_ERR("[%s] Unknown Event: %d\n", __FUNCTION__, event);
        return;
    }

    if (pNotificationConfig->proxy == PROXY_INVALID) {
            /* not supported yet, so ignore de-registration */
        return;    
    }
    
    if ((proxy = GetDBusProxy(pAbfBtInfo, event)) == NULL) {
        A_ERR("[%s] Unknown Proxy: %d\n", __FUNCTION__, 
              pNotificationConfig->proxy);
        return;
    }

    dbus_g_proxy_disconnect_signal(proxy, name, handler, (void *)pAbfBtInfo);
}

/* Misc */
static A_STATUS
AcquireBtAdapter(ABF_BT_INFO *pAbfBtInfo)
{
    DBusGProxy *DeviceAdapter, *AudioManager;
    DBusGConnection *bus = pAbfBtInfo->Bus;
    A_STATUS        status = A_ERROR;
    char            *adapterName;
    GError          *error = NULL;
    char            *hciName;
    
    do {
                
        if (!dbus_g_proxy_call(pAbfBtInfo->DeviceManager, 
                               "DefaultAdapter", 
                               &error, 
                               G_TYPE_INVALID, 
                               G_TYPE_STRING, 
                               &adapterName, 
                               G_TYPE_INVALID)) {
            A_ERR("[%s] DefaultAdapter Method call failure \n", __FUNCTION__);
            break;                     
        }
        
        if (error != NULL) {
            A_ERR("[%s] Failed to get default adapter: %s \n", __FUNCTION__, error->message);
            g_free(error);
            break;    
        }
        
        strcpy(pAbfBtInfo->AdapterName, adapterName);   
            
            /* assume ID 0 */
        pAbfBtInfo->AdapterId = 0;
        
        if ((hciName = strstr(pAbfBtInfo->AdapterName, "hci")) != NULL) {
                /* get the number following the hci name, this is the ID used for
                 * socket calls to the HCI layer */
            pAbfBtInfo->AdapterId = (int)hciName[3] - (int)'0';
            if (pAbfBtInfo->AdapterId < 0) {
                pAbfBtInfo->AdapterId = 0;   
            }
        }
        
        if (!A_SUCCESS(SetupHciEventFilter(pAbfBtInfo))) {
            break;    
        }
        
        g_free(adapterName);
               
        DeviceAdapter = dbus_g_proxy_new_for_name(bus, BLUEZ_NAME,
                                                  pAbfBtInfo->AdapterName, 
                                                  ADAPTER_INTERFACE);
        if (!DeviceAdapter) {
            A_ERR("[%s] Failed to get device adapter (%s) \n", __FUNCTION__, pAbfBtInfo->AdapterName);
            break;
        }
    
        AudioManager = dbus_g_proxy_new_for_name(bus, BLUEZ_NAME, 
                                                 AUDIO_MANAGER_PATH, 
                                                 AUDIO_MANAGER_INTERFACE);
        if (!AudioManager) {
            A_ERR("[%s] Failed to get name owner\n", __FUNCTION__);
            break;
        }
    
        pAbfBtInfo->DeviceAdapter = DeviceAdapter;
        pAbfBtInfo->AudioManager = AudioManager;
    
        GetAdapterInfo(pAbfBtInfo);
        
        pAbfBtInfo->pInfo->LMPVersion = pAbfBtInfo->LMPVersion;
        pAbfBtInfo->AdapterAvailable = TRUE;
    
        /* Register to get notified of different stack events */
        RegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_STARTED, 
                               G_CALLBACK(DeviceDiscoveryStarted));
        RegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_FINISHED, 
                               G_CALLBACK(DeviceDiscoveryFinished));
        RegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_CONNECTED, 
                               G_CALLBACK(RemoteDeviceConnected));
        RegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_DISCONNECTED, 
                               G_CALLBACK(RemoteDeviceDisconnected));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_ADDED, 
                               G_CALLBACK(AudioDeviceAdded));
        RegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_REMOVED, 
                               G_CALLBACK(AudioDeviceRemoved));

        pAbfBtInfo->AdapterCbRegistered = TRUE;
        
        A_INFO("[%s] BT Adapter Added\n",pAbfBtInfo->AdapterName);
        
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
        DeRegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_STARTED, 
                                 G_CALLBACK(DeviceDiscoveryStarted));
        DeRegisterBtStackEventCb(pAbfBtInfo, DEVICE_DISCOVERY_FINISHED, 
                                 G_CALLBACK(DeviceDiscoveryFinished));
        DeRegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_CONNECTED, 
                                 G_CALLBACK(RemoteDeviceConnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, REMOTE_DEVICE_DISCONNECTED, 
                                 G_CALLBACK(RemoteDeviceDisconnected));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_ADDED, 
                                 G_CALLBACK(AudioDeviceAdded));
        DeRegisterBtStackEventCb(pAbfBtInfo, AUDIO_DEVICE_REMOVED, 
                                 G_CALLBACK(AudioDeviceRemoved));    
    }
   
    ReleaseDefaultAudioDevice(pAbfBtInfo);
    
    if (pAbfBtInfo->HCIEventListenerSocket >= 0) {
        close(pAbfBtInfo->HCIEventListenerSocket);
        pAbfBtInfo->HCIEventListenerSocket = -1;    
    }
    
    if (pAbfBtInfo->AudioManager != NULL) {
        g_object_unref(pAbfBtInfo->AudioManager);
        pAbfBtInfo->AudioManager = NULL;
    }
    
    A_MEMZERO(pAbfBtInfo->DeviceAddress, 
              sizeof(pAbfBtInfo->DeviceAddress));
    A_MEMZERO(pAbfBtInfo->DeviceName, 
              sizeof(pAbfBtInfo->DeviceName));
    A_MEMZERO(pAbfBtInfo->ManufacturerName, 
              sizeof(pAbfBtInfo->ManufacturerName));
    A_MEMZERO(pAbfBtInfo->ProtocolVersion, 
              sizeof(pAbfBtInfo->ProtocolVersion));
    pAbfBtInfo->LMPVersion = 0;

    if (pAbfBtInfo->DeviceAdapter != NULL) {
        g_object_unref(pAbfBtInfo->DeviceAdapter);
        pAbfBtInfo->DeviceAdapter = NULL;
    }
    pAbfBtInfo->AdapterAvailable = FALSE;
}

static A_STATUS
GetAdapterInfo(ABF_BT_INFO *pAbfBtInfo)
{
    int count;
    char *reply;
    GError *error = NULL;
    DBusGProxy *DeviceAdapter;

    if ((DeviceAdapter = pAbfBtInfo->DeviceAdapter) == NULL) return A_ERROR;

    /* Device name */
    if (!dbus_g_proxy_call(DeviceAdapter, "GetName", &error, G_TYPE_INVALID, 
                           G_TYPE_STRING, &reply, G_TYPE_INVALID))
    {
        A_ERR("[%s] Failed to complete GetName: %d\n", __FUNCTION__, error);
        return A_ERROR;
    }
    strcpy(pAbfBtInfo->DeviceName, reply);
    g_free(reply);

    /* Manufacturer name */
    if (!dbus_g_proxy_call(DeviceAdapter, "GetManufacturer", &error, 
                           G_TYPE_INVALID, G_TYPE_STRING, &reply, 
                           G_TYPE_INVALID)) 
    {
        A_ERR("[%s] Failed to complete GetManufacturer: %d\n", 
              __FUNCTION__, error);
        return A_ERROR;
    }
    strcpy(pAbfBtInfo->ManufacturerName, reply);
    g_free(reply);

    /* Bluetooth protocol Version */
    if (!dbus_g_proxy_call(DeviceAdapter, "GetVersion", &error, G_TYPE_INVALID, 
                           G_TYPE_STRING, &reply, G_TYPE_INVALID))
    {
        A_ERR("[%s] Failed to complete GetVersion: %d\n", __FUNCTION__, error);
        return A_ERROR;
    }
    strcpy(pAbfBtInfo->ProtocolVersion, reply);
    for (count = 0; 
         ((count < sizeof(ver_map)/sizeof(hci_map)) && (ver_map[count].str)); 
         count++)
    {
        if (strstr(pAbfBtInfo->ProtocolVersion, ver_map[count].str)) {
            pAbfBtInfo->LMPVersion = ver_map[count].val;
            break;
        }
    }
    g_free(reply);

    /* Device address */
    if (!dbus_g_proxy_call(DeviceAdapter, "GetAddress", &error, G_TYPE_INVALID, 
                           G_TYPE_STRING, &reply, G_TYPE_INVALID))
    {
        A_ERR("[%s] Failed to complete GetAddress: %d\n", __FUNCTION__, error);
        return A_ERROR;
    }
    A_STR2ADDR(reply, pAbfBtInfo->DeviceAddress);
    g_free(reply);

    A_INFO("BT-HCI Device Address: (%2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X)\n", 
           pAbfBtInfo->DeviceAddress[0], pAbfBtInfo->DeviceAddress[1], 
           pAbfBtInfo->DeviceAddress[2], pAbfBtInfo->DeviceAddress[3], 
           pAbfBtInfo->DeviceAddress[4], pAbfBtInfo->DeviceAddress[5]);
    A_INFO("BT-HCI Device Name: %s\n", pAbfBtInfo->DeviceName);
    A_INFO("BT-HCI Manufacturer Name: %s\n", pAbfBtInfo->ManufacturerName);
    A_INFO("BT-HCI Protocol Version: %s\n", pAbfBtInfo->ProtocolVersion);
    A_INFO("BT-HCI LMP Version: %d\n", pAbfBtInfo->LMPVersion);

    return A_OK;
}

#define ABTH_MAX_CONNECTIONS 16

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

static void GetBtAudioConnectionProperties(ABF_BT_INFO              *pAbfBtInfo,
                                           ATHBT_STATE_INDICATION   Indication)
{
    A_UCHAR     role = 0;
    A_UCHAR     lmpversion = 0;
    A_CHAR      *pDescr = NULL;
    char        *address = NULL;
    char        *version = NULL;
    GError      *error = NULL;
    A_STATUS    status;
    
    do {
       
            /* get remote device address */                        
        if (!dbus_g_proxy_call(pAbfBtInfo->AudioDevice, 
                               "GetAddress", 
                               &error, 
                               G_TYPE_INVALID, 
                               G_TYPE_STRING, 
                               &address, 
                               G_TYPE_INVALID)) {
            A_ERR("[%s] GetAddress method call failed \n", __FUNCTION__);
            break;                     
        }
        
        if (error != NULL) {
            A_ERR("[%s] Failed to GetAddress for audio device: %s \n", __FUNCTION__, error->message);
            g_free(error);
            break;    
        }
        
        A_INFO("Connected audio device address: %s  \n", address);
        
        if (!dbus_g_proxy_call(pAbfBtInfo->DeviceAdapter, 
                               "GetRemoteVersion", 
                               &error, 
                               G_TYPE_STRING,
                               address,
                               G_TYPE_INVALID, 
                               G_TYPE_STRING, 
                               &version, 
                               G_TYPE_INVALID)) {
            A_ERR("[%s] GetRemoteVersion method call failed \n", __FUNCTION__);
            break;                     
        }
        
        if (error != NULL) {
            A_ERR("[%s] Failed to GetRemoteVersion for audio device: %s \n", __FUNCTION__, error->message);
            g_free(error);
            break;    
        }
        
        A_INFO("Connected audio device remote version: %s \n", version);

            /* assume 2.1 or later */
        lmpversion = 4;       
            
        if (strstr(version,"1.0") != NULL) {
            lmpversion = 0;    
        } else if (strstr(version,"1.1") != NULL) {
            lmpversion = 1;
        } else if (strstr(version,"1.2") != NULL) {
            lmpversion = 2;
        } else if (strstr(version,"2.0") != NULL) {
            lmpversion = 3;    
        }
           
            /* get role */     
        status = GetConnectedDeviceRole(pAbfBtInfo, 
                                        address, 
                                        Indication == ATH_BT_A2DP ? FALSE : TRUE,
                                        &role);
        
        if (A_FAILED(status)) {
            role = 0;    
        }
                                       
        if (Indication == ATH_BT_A2DP) {
            pDescr = "A2DP";  
            pAbfBtInfo->pInfo->A2DPConnection_LMPVersion = lmpversion;
            pAbfBtInfo->pInfo->A2DPConnection_Role = role;            
        } else if (Indication == ATH_BT_SCO) {
            if (pAbfBtInfo->CurrentSCOLinkType == SCO_LINK) {
                pDescr = "SCO";
            } else {
                pDescr = "eSCO";
            }
            pAbfBtInfo->pInfo->SCOConnection_LMPVersion = lmpversion;
            pAbfBtInfo->pInfo->SCOConnection_Role = role;
            
                /* for SCO connections check if the event filter captured 
                 * the SYNCH connection complete event */
            CheckHciEventFilter(pAbfBtInfo);
            
        } else {
            pDescr = "UNKNOWN!!";    
        }
                
        A_INFO("BT Audio connection properties:  (%s) (role: %s, lmp version: %d) \n", 
               pDescr, role ? "SLAVE" : "MASTER", lmpversion);
                
    } while (FALSE);
                                       
    if (address != NULL) {
        g_free(address);    
    }
    
    if (version != NULL) {
        g_free(version);    
    }
    
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
        A_INFO("Waiting for HCI CMD Complete Event, Opcode:0x%4.4X (%d MS) \n",OpCode, TimeoutMs);     
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
                /* no event*/
            break;
        } 
        
        if (!(pfd.revents & POLLIN)) {
            break;    
        }         
            /* get the packet */
        eventLen = read(Socket, pBuffer, MaxLength);
        
        if (eventLen == 0) {
            /* no event */
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
    
    
    do {
        
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

static A_STATUS SetupHciEventFilter(ABF_BT_INFO *pAbfBtInfo)
{
    A_STATUS            status = A_ERROR;
    struct hci_filter   filterSetting;
    struct sockaddr_hci addr;
        
    do {
        
        if (pAbfBtInfo->HCIEventListenerSocket >= 0) {
                /* close previous */
            close(pAbfBtInfo->HCIEventListenerSocket);    
        }
        
        pAbfBtInfo->HCIEventListenerSocket = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
        
        if (pAbfBtInfo->HCIEventListenerSocket < 0) {
            A_ERR("[%s] Failed to get raw BT socket: %d \n", __FUNCTION__, errno);
            break;    
        }
        
        hci_filter_clear(&filterSetting);
        hci_filter_set_ptype(HCI_EVENT_PKT,  &filterSetting);
        
            /* capture SYNC_CONN Complete */
        hci_filter_set_event(EVT_SYNC_CONN_COMPLETE, &filterSetting);
    
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
        
        status = A_OK;
        
    } while (FALSE);
    
    if (A_FAILED(status)) {
        if (pAbfBtInfo->HCIEventListenerSocket >= 0) {
            close(pAbfBtInfo->HCIEventListenerSocket);
            pAbfBtInfo->HCIEventListenerSocket = -1;    
        }    
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
        hci_filter_clear(&filterSetting);
        hci_filter_set_ptype(HCI_EVENT_PKT,  &filterSetting);
        hci_filter_set_event(EVT_CMD_COMPLETE, &filterSetting);
    
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
        
        
        status = WaitForHCIEvent(sk,
                                 EventRecvTimeoutMS,
                                 pEventBuffer,
                                 MaxLength,
                                 EVT_CMD_COMPLETE, 
                                 OpCode,
                                 ppEventPtr,
                                 pEventLength);
                    
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
    
    if (NULL == pAbfBtInfo) {
        return;    
    }
    
    if (pAbfBtInfo->Flags & ABF_ENABLE_AFH_CHANNEL_CLASSIFICATION) {
        IssueAFHChannelClassification(pAbfBtInfo,CurrentWLANChannel);
    }
}


