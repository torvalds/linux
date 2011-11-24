/* $Id: globals.c,v 1.3 2007/09/27 08:42:16 maso Exp $ */
/*****************************************************************************

Copyright (c) 2006 by Nanoradio AB

This software is copyrighted by and is the sole property of Nanoradio AB.
All rights, title, ownership, or other interests in the
software remain the property of Nanoradio AB.  This software may
only be used in accordance with the corresponding license agreement.  Any
unauthorized use, duplication, transmission, distribution, or disclosure of
this software is expressly forbidden.

This Copyright notice may not be removed or modified without prior written
consent of Nanoradio AB.

Nanoradio AB reserves the right to modify this software without
notice.

Nanoradio AB
Torshamnsgatan 39                       info@nanoradio.se
164 40 Kista                            http://www.nanoradio.se
SWEDEN

Module Description :
==================
Contains platform independent code for simplify the connect procedure on most
platforms.

*****************************************************************************/

#include "driverenv.h"
#include "adl_utils.h"


#if (DE_BUILTIN_SUPPLICANT == CFG_NOT_INCLUDED)
#error adl_connect is designed to work with GNU-supplicant only
#endif


typedef enum _state_t{
   NOT_CONNECTED,
   CONNECTING,
   CONNECTED
}state_t;


static state_t curr_state = NOT_CONNECTED;


//timer used to limit how long we will try to connect
static driver_timer_id_t s_connect_timeout_timer = 0xFFFFFFFF;

static iobject_t *s_connect_failed_event = NULL;
static iobject_t *s_connected_event = NULL;
static iobject_t *s_disconnected_event = NULL;


void wpa_set_ssid_psk(const char *name, const char *psk);

static int connect_timeout_cb(void* ptr, size_t len);
static void connect_failed_cb(wi_msg_param_t param, void* priv);
static void connected_cb(wi_msg_param_t param, void* priv);
static void disconnected_cb(wi_msg_param_t param, void* priv);

static nrwifi_auto_connect_cb callback = NULL;

/*!
 * @brief Automaticlay finds and connect to a network.
 * 
 * Connects to a network with a minimum of input. It is assumed that only 
 * "normal" encryption/authentication settings are used.
 *
 * @param ssid          Pointer to the SSID as a null-terminated string.
 * @param security_type Type of security (WPA/WPA2/WEP/none....)
 * @param timeout       Connection timeout in ms. (zero means no timeout)
 * @param security_key  Security key (string for WPA, key for WEP)
 * @param callback_fn   callback function for status-callbacks.
 *
 * @return WIFI_ENGINE_SUCCESS if connection procedure was successfully started
 */
int nrwifi_auto_connect(const char* ssid,
                        wlan_security_type security_type,
                        int timeout,
                        const char* security_key,
                        nrwifi_auto_connect_cb callback_fn)
{
   int wep_key_len = 5; //40bit
   
   WiFiEngine_sac_stop();

   callback = callback_fn;

   switch(security_type)
   {
      case WLAN_SECURITY_OPEN:
         WiFiEngine_SetAuthenticationMode(Authentication_Open);
         WiFiEngine_SetEncryptionMode(Encryption_Disabled);
         WiFiEngine_SetProtectedFrameBit(0);
         break;

      case WLAN_SECURITY_WEP128:
         wep_key_len = 13;
      case WLAN_SECURITY_WEP40:
         {
            m80211_mac_addr_t bssid = {{ 0xff,0xff,0xff,0xff,0xff,0xff}};
      
            WiFiEngine_SetAuthenticationMode(Authentication_Open);
            WiFiEngine_SetEncryptionMode(Encryption_WEP);
            WiFiEngine_AddKey(0, wep_key_len, security_key, M80211_KEY_TYPE_GROUP, M80211_PROTECT_TYPE_RX_TX, 1, &bssid, NULL, 1);
            WiFiEngine_AddKey(1, wep_key_len, security_key, M80211_KEY_TYPE_GROUP, M80211_PROTECT_TYPE_RX_TX, 1, &bssid, NULL, 0);
            WiFiEngine_AddKey(2, wep_key_len, security_key, M80211_KEY_TYPE_GROUP, M80211_PROTECT_TYPE_RX_TX, 1, &bssid, NULL, 0);
            WiFiEngine_AddKey(3, wep_key_len, security_key, M80211_KEY_TYPE_GROUP, M80211_PROTECT_TYPE_RX_TX, 1, &bssid, NULL, 0);
            
            WiFiEngine_SetDefaultKeyIndex(0);
            WiFiEngine_SetProtectedFrameBit(1);
         }
         break;


      case WLAN_SECURITY_WPA_PSK:
         WiFiEngine_SetAuthenticationMode(Authentication_WPA_PSK);
         WiFiEngine_SetEncryptionMode(Encryption_CCMP);
         wpa_set_ssid_psk(ssid, security_key);
         break;

      case WLAN_SECURITY_WPA2_PSK:
         WiFiEngine_SetAuthenticationMode(Authentication_WPA_2_PSK);
         WiFiEngine_SetEncryptionMode(Encryption_CCMP);
         wpa_set_ssid_psk(ssid, security_key);
         break;

      default:
         DE_TRACE_INT(TR_WARN, "Unknown security type (%d)\n", security_type);
         WiFiEngine_SetAuthenticationMode(Authentication_WPA_2_PSK); //use best-authentication (better safe than sorry)
         break;
   }


   if(timeout)
   {
      if(s_connect_timeout_timer == 0xFFFFFFFF)
      {
         if(DriverEnvironment_GetNewTimer(&s_connect_timeout_timer, FALSE)
                                           != DRIVERENVIRONMENT_SUCCESS)
         {
            DE_TRACE_STATIC(TR_WARN, "Failed to allocate connect_timeout_timer\n");
            return WIFI_ENGINE_FAILURE;
         }
      }

      if(DriverEnvironment_RegisterTimerCallback(timeout,
                                 s_connect_timeout_timer,
                                 connect_timeout_cb,
                                 FALSE) != DRIVERENVIRONMENT_SUCCESS)
      {
         DE_TRACE_STATIC(TR_WARN, "Faild to start connect_timeout_timer\n");
         DriverEnvironment_FreeTimer(s_connect_timeout_timer);
         s_connect_timeout_timer = 0xFFFFFFFF;
         return WIFI_ENGINE_FAILURE;
      }

   }

   if (WiFiEngine_SetSSID((char *)ssid, strlen(ssid)) != WIFI_ENGINE_SUCCESS)
   {
      DriverEnvironment_FreeTimer(s_connect_timeout_timer);
      s_connect_timeout_timer = 0xFFFFFFFF;
      return WIFI_ENGINE_FAILURE;
   }

   we_ind_cond_register(&s_connect_failed_event, WE_IND_CM_CONNECT_FAILED,
                                             (char*)NULL,
                                             connect_failed_cb,
                                             NULL, /* release */
                                             RELEASE_IND_AFTER_EVENT | RELEASE_IND_ON_UNPLUG,
                                             NULL);
   
   we_ind_cond_register(&s_connected_event, WE_IND_CM_CONNECTED,
                                             (char*)NULL,
                                             connected_cb,
                                             NULL, /* release */
                                             RELEASE_IND_AFTER_EVENT | RELEASE_IND_ON_UNPLUG,
                                             NULL);

   curr_state = CONNECTING;
   
   WiFiEngine_sac_start();

   return WIFI_ENGINE_SUCCESS;
   
}


static int connect_timeout_cb(void* ptr, size_t len)
{
   if(s_connect_timeout_timer != 0xFFFFFFFF)
   {
      DriverEnvironment_FreeTimer(s_connect_timeout_timer);
      s_connect_timeout_timer = 0xFFFFFFFF;
   }

   if(curr_state != CONNECTED)
   {
      WiFiEngine_sac_stop();
      WiFiEngine_Disconnect();
      DE_TRACE_STATIC(TR_ALWAYS, "Timeout connecting\n");
      curr_state = NOT_CONNECTED;
      if(callback != NULL)
         callback(WLAN_CONNECT_TIMEOUT);
   }
   
   return 0;
}

static void connect_failed_cb(wi_msg_param_t param, void* priv)
{
   if(s_connect_timeout_timer != 0xFFFFFFFF)
   {
      DriverEnvironment_FreeTimer(s_connect_timeout_timer);
      s_connect_timeout_timer = 0xFFFFFFFF;
   }
   curr_state = NOT_CONNECTED;
   WiFiEngine_sac_stop();
   WiFiEngine_Disconnect();

   if(s_connected_event != NULL)
      we_ind_deregister_null(&s_connected_event);
      

   if(callback != NULL)
      callback(WLAN_CONNECT_FAILED);
}

static void connected_cb(wi_msg_param_t param, void* priv)
{
   if(s_connect_timeout_timer != 0xFFFFFFFF)
   {
      DriverEnvironment_FreeTimer(s_connect_timeout_timer);
      s_connect_timeout_timer = 0xFFFFFFFF;
   }
   curr_state = CONNECTED;

   we_ind_deregister_null(&s_connect_failed_event);

   we_ind_cond_register(&s_disconnected_event, WE_IND_CM_DISCONNECTED,
                                          (char*)NULL,
                                          disconnected_cb,
                                          NULL, /* release */
                                          RELEASE_IND_AFTER_EVENT | RELEASE_IND_ON_UNPLUG,
                                          NULL);
   
   if(callback != NULL)
      callback(WLAN_CONNECTED);
}

static void disconnected_cb(wi_msg_param_t param, void* priv)
{
   if(s_connect_timeout_timer != 0xFFFFFFFF)
   {
      DriverEnvironment_FreeTimer(s_connect_timeout_timer);
      s_connect_timeout_timer = 0xFFFFFFFF;
   }

   curr_state = NOT_CONNECTED;
   WiFiEngine_sac_stop();
   WiFiEngine_Disconnect();

   if(callback != NULL)
      callback(WLAN_CONNECT_DISCONNECTED);
 
}



int nrwifi_auto_disconnect(void)
{
   if(s_connect_timeout_timer != 0xFFFFFFFF)
   {
      DriverEnvironment_FreeTimer(s_connect_timeout_timer);
      s_connect_timeout_timer = 0xFFFFFFFF;
   }

   WiFiEngine_sac_stop();
   curr_state = NOT_CONNECTED;
   WiFiEngine_Disconnect();
   WiFiEngine_SetSSID("", 0);
   if(callback != NULL)
      callback(WLAN_CONNECT_DISCONNECTED);

   return 0;
}


connect_status nrwifi_get_connection_status(void)
{
   switch(curr_state)
   {
      case CONNECTING: return WLAN_CONNECTING;
      case CONNECTED:  return WLAN_CONNECTED;
   }
   return WLAN_DISCONNECTED;
}

