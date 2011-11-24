/* $Id: we_auth.c,v 1.65 2008-05-19 14:57:31 peek Exp $ */
/*****************************************************************************

Copyright (c) 2004 by Nanoradio AB

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
This module implements the WiFiEngine authentication and encryption interface

*****************************************************************************/
/** @defgroup we_auth WiFiEngine authentication and encryption interface
 *
 * @brief This module implements the WiFiEngine authentication and encryption
 * interface. It is used to set keys, control encryption and authentication
 * types, PMKIDs and the WiFiEngine 802.1X port.
 *
 *  @{
 */
#include "driverenv.h"
#include "ucos.h"
#include "m80211_stddefs.h"
#include "registry.h"
#include "packer.h"
#include "registryAccess.h"
#include "wifi_engine_internal.h"
#include "hmg_traffic.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"

static int cache_add_key(uint32_t key_idx, size_t key_len, const void *key, 
                            m80211_key_type_t key_type,
                            m80211_protect_type_t prot,
                            int authenticator_configured,
                            m80211_mac_addr_t *bssid, 
                            receive_seq_cnt_t *rsc, int tx_key);

static int cache_del_key(uint32_t key_idx, m80211_key_type_t key_type, 
                              m80211_mac_addr_t *bssid);

static void cache_new_tx_key(unsigned int index);
static int cache_clear(void);


static int open_8021x_port_cb(we_cb_container_t *cbc)
{
   DE_TRACE_STATIC(TR_AUTH, "Opening 802.1x port on callback\n");
   if ( cbc && WIFI_ENGINE_FAILURE_ABORT == cbc->status )
   {
      DE_TRACE_STATIC(TR_MIB, "received ABORT status. Not opening port\n");
      return 0;
   }

   WiFiEngine_8021xPortOpen();
   
   return 1;
}

/*!
 * @brief Set the RSNA Enable mode
 *
 * @param mode RSNA Enable mode. 1 to enable, 0 to disable.
 * @param cb Pointer to a callback function that will be executed when
 *           the RSNA cfm arrives.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE on error
 */
static int set_rsna_enable_with_cb(uint8_t mode, we_callback_t cb)
{
   we_cb_container_t *cbc;

   BAIL_IF_UNPLUGGED;
   DE_TRACE_INT(TR_AUTH, "Setting RSNAEnable mode to %d\n", mode);

   cbc = WiFiEngine_BuildCBC(cb, NULL, 0, FALSE);
   if (NULL == cbc)
   {
      return WIFI_ENGINE_FAILURE;
   }
   
   if ( WiFiEngine_SetMIBAsynch(MIB_dot11RSNAEnable,
                         NULL, (char *)&mode, sizeof mode, cbc)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Add an encryption key to the device.
 *
 * Configure a key into the device. The key type (WEP-40, WEP-104, TKIP or CCMP)
 * is determined by the size of the key (5 bytes for WEP-40, 13 bytes for WEP-104).
 * This will enable encryption in the device if the driver has a valid
 * encryption mode configured.
 *
 * @param key_idx The key index to set. (0-3)
 * @param key_len Length of key in bytes.
 * @param key Key input buffer.
 * @param key_type Key type (group, sta or pairwise)
 * @param prot Key protection mode (rx or rx_tx)
 * @param authenticator_configured Flag that indicates wether the key was 
 *        configured by a 802.1X authenticator (1) or supplicant (0).
 *        The impact of this flag is how the last half of a TKIP key will
 *        be used for MIC calculations. If the key set is the "raw" key
 *        then this flag makes the device extract the proper fields from
 *        the key for Rx and Tx MIC calculations. However, if the key 
 *        is "preswapped"; if the Rx and Tx MIC part has been swapped if
 *        the entity generating the key is a supplicant (wpa_supplicant does this),
 *        then this bit should always be 1 (authenticator) to prevent 
 *        swapping in the device.
 * @param bssid BSSID that the key applies to.
 * @param rsc Receive sequence counter value to use.
 * @param tx_key Flag indicating if the key is a tx/default key.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_INVALID_LENGTH if the key length is larger than the
 *         device allows.
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if key_idx is too large or if the cipher
 *         suite for the net is disallowed.
 * - WIFI_ENGINE_FAILURE on other failures.
 */
int WiFiEngine_AddKey(uint32_t key_idx, size_t key_len, const void *key, 
                      m80211_key_type_t key_type, m80211_protect_type_t prot,
                      int authenticator_configured, m80211_mac_addr_t *bssid, 
                      receive_seq_cnt_t *rsc, int tx_key)
{
   hic_message_context_t     msg_ref;
   m80211_mlme_set_key_req_t set_req;
   int                       status;
   WiFiEngine_Encryption_t   enc;
   m80211_cipher_suite_t     cipher_suite = M80211_CIPHER_SUITE_NONE;
   WiFiEngine_bss_type_t     bss_type;
   m80211_mac_addr_t pairwise_bssid;

   BAIL_IF_UNPLUGGED;
   if (key_len > sizeof set_req.set_key_descriptor.key)
   {
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }
   if (key_idx > WIFI_ENGINE_MAX_WEP_KEYS) 
   {
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }

   status = WiFiEngine_GetEncryptionMode(&enc);
   if(status != WIFI_ENGINE_SUCCESS) {
     DE_TRACE_STATIC(TR_AUTH, "ERROR Getting Encryption Mode!\n");
     return WIFI_ENGINE_FAILURE;
   }
   
   if(enc != Encryption_SMS4) {
     /* 802.11i-2004 specify that Key ID subfield shall be set to 0 on */
     /* transmit and ignored on receive. Cisco Aironet 1200 however ignore */
     /* this and use non 0 key id with wep and built in radius server */
     if (key_len > 13 && M80211_KEY_TYPE_PAIRWISE == key_type && 0 != key_idx)
     {
       DE_TRACE_INT(TR_AUTH, "Tried to set invalid pairwise key (%d != 0, type pairwise).\n",key_idx);
       return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
     }
   }

   WiFiEngine_GetBSSType(&bss_type);
   Mlme_CreateMessageContext(msg_ref);

   /* Infer the cipher_suite from the key length */
   switch(key_len)
   {
   /* WEP keys are 40 or 104 bits long */
      case 5:
      case 13:
         cipher_suite = M80211_CIPHER_SUITE_WEP;
         DE_TRACE_STATIC(TR_AUTH, "Setting WEP key\n");
         wifiEngineState.key_state.key_enc_type = Encryption_WEP;
         break;
      case 16:
         cipher_suite = M80211_CIPHER_SUITE_CCMP;
         DE_TRACE_STATIC(TR_AUTH, "Setting CCMP key\n");
         wifiEngineState.key_state.key_enc_type = Encryption_CCMP;
         break;
      case 32:
	if(enc == Encryption_SMS4) {
	  cipher_suite = M80211_CIPHER_SUITE_WPI;
          DE_TRACE_STATIC(TR_AUTH, "Setting WAPI key\n");
          wifiEngineState.key_state.key_enc_type = Encryption_SMS4;
        }
        else {
          cipher_suite = M80211_CIPHER_SUITE_TKIP;
          DE_TRACE_STATIC(TR_AUTH, "Setting TKIP key\n");
          wifiEngineState.key_state.key_enc_type = Encryption_TKIP;
        }
         break;
      default:
         DE_TRACE_STATIC(TR_AUTH, "Bad key length\n");
         return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }

   enc = wei_cipher_suite2encryption(cipher_suite);
   if (Mlme_CreateSetKeyRequest(&msg_ref, key_idx, key_len, key, cipher_suite, key_type,
                                authenticator_configured, bssid, rsc)) 
   { 
      DE_TRACE_STATIC(TR_AUTH, "Sending AddKey message\n"); 
      status = wei_send_cmd(&msg_ref);
      Mlme_ReleaseMessageContext(msg_ref);
      if (status != WIFI_ENGINE_SUCCESS) 
      { 
         return WIFI_ENGINE_FAILURE; 
      } 
   } 
   else
   {
      DE_TRACE_STATIC(TR_AUTH, "Failed to build AddKey message!\n");
      Mlme_ReleaseMessageContext(msg_ref);
      return WIFI_ENGINE_FAILURE;
   }

   /* Set the default key index (group key or WEP)... */
   if(M80211_KEY_TYPE_GROUP == key_type && tx_key)
   {
      WiFiEngine_SetDefaultKeyIndex(key_idx);
   }

   /* ... set protection if encryption is enabled... */
   WiFiEngine_GetEncryptionMode(&enc);

   if (enc > Encryption_Disabled)
   {
      /* WAPI group key handshake is also unecrypted so the pairwise protection
       *   should be set after group key installation. 
       */
      if(enc == Encryption_SMS4)
      {
         /* wapi case */
         if (M80211_KEY_TYPE_GROUP == key_type)
         {
            WiFiEngine_GetBSSID(&pairwise_bssid);
            WiFiEngine_SetRSNProtection(&pairwise_bssid, M80211_PROTECT_TYPE_RX_TX, M80211_KEY_TYPE_PAIRWISE);
            WiFiEngine_SetRSNProtection(bssid, prot, key_type);
         }
      } else {
         /* wpa/wpa2 case */
         WiFiEngine_SetRSNProtection(bssid, prot, key_type);
      }
   }

   /* ... save key key parameters... */
   DE_MEMCPY(&(wifiEngineState.key_state.bssid), bssid, sizeof *bssid);
   DE_MEMCPY(&(wifiEngineState.key_state.prot), &prot, sizeof prot);
   DE_MEMCPY(&(wifiEngineState.key_state.key_type), &key_type, sizeof key_type);
   wifiEngineState.key_state.used = 1;
   /* ... adn set RSNA Enable and open the 802.1x port. The port will open only
    * when RSNA has been enabled. */
   set_rsna_enable_with_cb(1, open_8021x_port_cb);

   /* Skip adding keys to cache if we are currently reconfiguring the device */
   if(WES_TEST_FLAG(WES_DEVICE_CONFIGURED))
      cache_add_key(key_idx, key_len, key, key_type, prot,
                    authenticator_configured, bssid, rsc, tx_key);
   
   
   return WIFI_ENGINE_SUCCESS; 
}

/*!
 * @brief Delete a key from the device
 *
 * @param key_idx The index of the key to be deleted
 * @param key_type Key type (group or pairwise)
 * @param bssid BSSID for the key (for pairwise keys, otherwise this should be
 *              the broadcast BSSID).
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if key_idx is too large.
 * - WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_DeleteKey(uint32_t key_idx, m80211_key_type_t key_type, 
                         m80211_mac_addr_t *bssid)
{
   hic_message_context_t msg_ref;
   int status;

   BAIL_IF_UNPLUGGED;
   if (key_idx > WIFI_ENGINE_MAX_WEP_KEYS && key_idx != 0xFF) 
   {
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }

/*    WiFiEngine_SetRSNProtection(bssid, M80211_PROTECT_TYPE_NONE, M80211_KEY_TYPE_PAIRWISE);  */
   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateDeleteKeyRequest(&msg_ref, key_idx, key_type, bssid)) 
   { 
      DE_TRACE_STATIC(TR_AUTH, "Sending DeleteKey message\n"); 
      status = wei_send_cmd(&msg_ref);
      Mlme_ReleaseMessageContext(msg_ref);
      if (status == WIFI_ENGINE_SUCCESS) 
      {
         /* Skip touching the cache if we are currently reconfiguring the
          * device.
          */
         if(WES_TEST_FLAG(WES_DEVICE_CONFIGURED))
            cache_del_key(key_idx, key_type, bssid);

         return WIFI_ENGINE_SUCCESS; 
      } 
      return WIFI_ENGINE_FAILURE; 
   } 
   else 
   { 
      return WIFI_ENGINE_FAILURE; 
   } 
}

/*!
 * @brief Delete a group (multicast) key.
 * @param key_idx 0-based key index to delete.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS
 * - WIFI_ENGINE_FAILURE_DEFER if the send queue is full,
 * try the send again later.
 * - WIFI_ENGINE_FAILURE if the send failed (such as when no hardware is present).
 */
int WiFiEngine_DeleteGroupKey(int key_idx)
{
   m80211_mac_addr_t bcast_bssid;
   int status = WIFI_ENGINE_SUCCESS;
   int i;

   BAIL_IF_UNPLUGGED;
   for (i = 0; i < M80211_ADDRESS_SIZE; i++)
   {
      bcast_bssid.octet[i] = (char)0xFF;
   }
   WiFiEngine_DeleteKey(key_idx, M80211_KEY_TYPE_GROUP, &bcast_bssid);

   /* Skip touching the cache if we are currently reconfiguring the
    * device.
    */
   if(WES_TEST_FLAG(WES_DEVICE_CONFIGURED))
      cache_del_key(key_idx, M80211_KEY_TYPE_GROUP, &bcast_bssid);
   
   return status;
}

/*!
 * @brief Delete all encryption keys in the device.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_DeleteAllKeys(void)
{
   int               i;
   m80211_mac_addr_t bcast_bssid;
   WiFiEngine_net_t  *net;

   BAIL_IF_UNPLUGGED;
   for (i = 0; i < M80211_ADDRESS_SIZE; i++)
   {
      bcast_bssid.octet[i] = (char)0xFF;
   }

   net = wei_netlist_get_current_net();
   if (net)
   {
      WiFiEngine_SetRSNProtection(&net->bssId_AP, M80211_PROTECT_TYPE_NONE, M80211_KEY_TYPE_ALL); 
   }
   else
   {
      WiFiEngine_SetRSNProtection(&bcast_bssid, M80211_PROTECT_TYPE_NONE, M80211_KEY_TYPE_ALL); 
   }
   WiFiEngine_DeleteGroupKey(1);

   WiFiEngine_DeleteKey(0, M80211_KEY_TYPE_ALL, &bcast_bssid);
   WIFI_LOCK();
   wifiEngineState.key_state.used = 0;
   WIFI_UNLOCK();

   /* Skip touching the cache if we are currently reconfiguring the
    * device.
    */
   if(WES_TEST_FLAG(WES_DEVICE_CONFIGURED))
      cache_clear();

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the index of the default key for the associated net (if any).
 *
 * @param index Output buffer. 
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no key is currently in use.
 */
int WiFiEngine_SetDefaultKeyIndex(int8_t index)
{
   int              status;

   BAIL_IF_UNPLUGGED;
   if (index >= WIFI_ENGINE_MAX_WEP_KEYS) 
   {
      return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }

   cache_new_tx_key(index);

   DE_TRACE_INT(TR_AUTH, "Setting WEP/group default key_idx %u\n", index);
   status = WiFiEngine_SendMIBSet(MIB_dot11DefaultKeyID, NULL, (char*)&index, sizeof(index));
   if (status == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   DE_TRACE_INT(TR_SEVERE, "SetDefaultKeyIndex %d failed\n",index);
   return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
}


/*!
 * @brief Get the ExcludeUnencrypted flag
 * 
 * Gets the ExcludeUnencrypted MIB variable state. Note that the target
 * always (should) let through 802.1x EAPOL packets even if they are
 * unencrypted and the ExludeUnencrypted flag is set.
 *
 * @param i Output buffer. 0 means the flag is disabled. 1 means the flag is enabled.
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS.
 */
int WiFiEngine_GetExcludeUnencryptedFlag(int *i)
{
   BAIL_IF_UNPLUGGED;
   *i = wifiEngineState.excludeUnencryptedFlag;
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the ExcludeUnencrypted flag
 * 
 * Sets the ExcludeUnencrypted MIB variable state. Note that the target
 * always (should) let through 802.1x EAPOL packets even if they are
 * unencrypted and the ExludeUnencrypted flag is set.
 *
 * @param i Input buffer. 1 to enabled the flag. 0 to disable it.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE otherwise.
 */
int WiFiEngine_SetExcludeUnencryptedFlag(int i)
{
   uint8_t flag;

   BAIL_IF_UNPLUGGED;
   flag = (uint8_t)i;
   wifiEngineState.excludeUnencryptedFlag = i;
   if ( WiFiEngine_SendMIBSet((char *)&MIB_dot11ExcludeUnencrypted[0],
                         NULL, (char *)&flag, sizeof(flag))
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Set the protected frame bit
 *
 * Set the protected frame (formerly called the privacy-bit) bit to be
 * used for data and management frames.
 *
 * @param mode Protected frame bit value. 1 to set the bit, 0 to clear it.
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetProtectedFrameBit(uint8_t mode)
{
   BAIL_IF_UNPLUGGED;
   DE_TRACE_INT(TR_AUTH, "Setting protected frame bit to %d\n", mode);
   if ( WiFiEngine_SendMIBSet(MIB_dot11PrivacyInvoked,
                         NULL, (char *)&mode, sizeof mode)
        == WIFI_ENGINE_SUCCESS)
   {
      wifiEngineState.key_state.dot11PrivacyInvoked = mode;
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

int WiFiEngine_GetProtectedFrameBit(uint8_t *mode)
{
   *mode = wifiEngineState.key_state.dot11PrivacyInvoked;
   return WIFI_ENGINE_SUCCESS;
}


/*!
 * @brief Set the RSNA Enable mode
 *
 * @param mode RSNA Enable mode. 1 to enable, 0 to disable.
 * @return 
 * - Always returns WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetRSNAEnable(uint8_t mode)
{
   BAIL_IF_UNPLUGGED;
   DE_TRACE_INT(TR_AUTH, "Setting RSNAEnable mode to %d\n", mode);
   if ( WiFiEngine_SendMIBSet(MIB_dot11RSNAEnable,
                         NULL, (char *)&mode, sizeof mode)
        == WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_SUCCESS;
   }

   return WIFI_ENGINE_FAILURE;
}

/*!
 * @brief Generate a MLME-SETPROTECTION.request message.
 *
 * Indicates wether encryption/protection is to be used for frames sent to/from
 * the given mac address.
 *
 * @param bssid    The bssid for which to set the protection status.
 * @param prot     The protection type (None, RX, TX or RX_TX).
 * @param key_type The key type (Group, Pairwise or STA).
 * @return 
 * - WIFI_ENGINE_SUCCESS on success,
 * - WIFI_ENGINE_FAILURE on transmission failure.
 */
int WiFiEngine_SetRSNProtection(m80211_mac_addr_t *bssid, m80211_protect_type_t prot, 
                                m80211_key_type_t key_type)
{
   int success = WIFI_ENGINE_FAILURE;
   hic_message_context_t   msg_ref;

   BAIL_IF_UNPLUGGED;
   DE_TRACE_STATIC(TR_AUTH, "Setting RSN Protection\n");
   Mlme_CreateMessageContext(msg_ref);
   if (Mlme_CreateSetProtectionReq(&msg_ref, bssid, &prot, &key_type))
   {
      success = wei_send_cmd(&msg_ref);
   }
   else
   {
      success = WIFI_ENGINE_FAILURE;
   }
   Mlme_ReleaseMessageContext(msg_ref);
   if (success != WIFI_ENGINE_SUCCESS)
   {
      return WIFI_ENGINE_FAILURE;
   }
   
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Open the 802.1x port 
 *
 * @return 
 * - Return the previous value.
 */
int WiFiEngine_8021xPortOpen(void)
{
   int r;
   
   BAIL_IF_UNPLUGGED;
   WIFI_LOCK();
   r = WES_TEST_FLAG(WES_FLAG_8021X_PORT_OPEN);
   WES_SET_FLAG(WES_FLAG_8021X_PORT_OPEN);
   WIFI_UNLOCK();
   
   return r;
}

/*!
 * @brief Close the 802.1x port
 *
 * @return 
 * - Return the previous value.
 */
int WiFiEngine_8021xPortClose(void)
{
   int r;

   BAIL_IF_UNPLUGGED;

   WIFI_LOCK();
   r = WES_TEST_FLAG(WES_FLAG_8021X_PORT_OPEN);
   WES_CLEAR_FLAG(WES_FLAG_8021X_PORT_OPEN);
   WIFI_UNLOCK();

   return r;
}

/*!
 * @brief Return the state of the 802.1x port
 *
 * @return 
 * - 1 if the port is closed.
 * - 0 otherwise.
int WiFiEngine_Is8021xPortClosed(void)
{
   BAIL_IF_UNPLUGGED;
   return ! WES_TEST_FLAG(WES_FLAG_8021X_PORT_OPEN);
}
 */

/*!
 * @brief Check if a Tx encryption key has been set
 *
 * @return 
 * - 1 if a Tx key has been set. 
 * - 0 otherwise.
 */
int WiFiEngine_IsTxKeyPresent(void)
{
   int8_t ignore;

   if(WiFiEngine_GetDefaultKeyIndex(&ignore)
         == WIFI_ENGINE_SUCCESS)
   {
      return 1;
   }
   return 0;
}


/*!
 * @brief Get the current encryption mode
 *
 * @param encryptionMode Output buffer.
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetEncryptionMode(WiFiEngine_Encryption_t* encryptionMode)
{
   BAIL_IF_UNPLUGGED;
   *encryptionMode = wifiEngineState.config.encryptionLimit;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the encryption mode to use 
 *
 * This will enable encryption in the device if a key is already present 
 * (and the encryption mode is different from Encryption_Disabled).
 *
 * @param encryptionMode Input buffer.
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetEncryptionMode(WiFiEngine_Encryption_t encryptionMode)
{
   WiFiEngine_bss_type_t bssType;

   BAIL_IF_UNPLUGGED;
   WiFiEngine_GetBSSType(&bssType);  

#if 0
   if((bssType == Independent_BSS) && (encryptionMode > Encryption_WEP))
   {
      return WIFI_ENGINE_FAILURE;
   }
#endif

   if(wifiEngineState.config.encryptionLimit != encryptionMode)
   {
      DE_TRACE_INT2(TR_AUTH, "encryption mode change %d=>%d\n", 
            wifiEngineState.config.encryptionLimit,
            encryptionMode);
   }

   wifiEngineState.config.encryptionLimit = encryptionMode;

   if (wifiEngineState.key_state.used)
   {
      WiFiEngine_SetRSNProtection(&wifiEngineState.key_state.bssid, 
                                  wifiEngineState.key_state.prot, 
                                  wifiEngineState.key_state.key_type);
      wifiEngineState.key_state.used = 0;
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Get the current authentication mode
 *
 * @param authenticationMode Output buffer.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_GetAuthenticationMode(WiFiEngine_Auth_t* authenticationMode)
{
   BAIL_IF_UNPLUGGED;
   *authenticationMode = wifiEngineState.config.authenticationMode;
   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Set the authentication mode to use
 *
 * @param authenticationMode Input buffer.
 *
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_SetAuthenticationMode(WiFiEngine_Auth_t authenticationMode)
{
   BAIL_IF_UNPLUGGED;
   
   if(wifiEngineState.config.authenticationMode != authenticationMode)
   {
      DE_TRACE_INT2(TR_ASSOC, "auth mode change %d=>%d\n", 
            wifiEngineState.config.authenticationMode,
            authenticationMode);
   }

   wifiEngineState.config.authenticationMode = authenticationMode;
   return WIFI_ENGINE_SUCCESS;
}

int assoc_holdoff_cb(void *data, size_t len)
{
   DE_TRACE_STATIC(TR_AUTH, "Association hold off callback invoked.\n");
   WIFI_LOCK();
   WES_CLEAR_FLAG(WES_FLAG_ASSOC_BLOCKED);
   WIFI_UNLOCK();

   return 0;
}

int mic_failure_cb(void *data, size_t len)
{
   DE_TRACE_STATIC(TR_AUTH, "MIC failure callback invoked. Clearing MIC failure flag.\n");
   WIFI_LOCK();
   WES_CLEAR_FLAG(WES_FLAG_MIC_FAILURE_RCVD);
   WIFI_UNLOCK();

   return 0;
}

/*!
 * Handle a Michael MIC failure. Trigger countermeasures if appropriate.
 *
 * @param key_type Key type (pairwise of group).
 * @return 
 * - WIFI_ENGINE_SUCCESS
 */
int WiFiEngine_HandleMICFailure(m80211_michael_mic_failure_ind_descriptor_t *ind_p)
{
   WiFiEngine_net_t *net;

   BAIL_IF_UNPLUGGED;
   DE_TRACE_STATIC(TR_AUTH, "Indicating mic error\n");
   net = wei_netlist_get_current_net();
   if (net == NULL)
   {
      DE_TRACE_STATIC(TR_AUTH, "Got Michael MIC failure indication but we're not associated.\n");
      return WIFI_ENGINE_SUCCESS;
   }

   if (WES_TEST_FLAG(WES_FLAG_MIC_FAILURE_RCVD))
   {
      DE_TRACE_STATIC(TR_AUTH, "Second MIC failure within 60 seconds. Fire countermeasures!\n");

      DE_TRACE_STATIC(TR_AUTH, "Closing 8021x port\n");
      WiFiEngine_8021xPortClose();
      ind_p->count = 2;
      
      /* Deauth after next EAPOL message && refuse auth for 60 secs */
      WES_SET_FLAG(WES_FLAG_ASSOC_BLOCKED);
      if (DriverEnvironment_RegisterTimerCallback(MIC_CM_TIMEOUT, wifiEngineState.mic_cm_assoc_holdoff_timer_id, assoc_holdoff_cb, 0) != 1)
      {
         DE_TRACE_STATIC(TR_AUTH, "No hold off timer callback registered, DE was busy\n");
      }
   }
   else
   {
      DE_TRACE_STATIC(TR_AUTH, "First MIC failure detected. All hands on deck!\n");
      ind_p->count = 1;
      WES_SET_FLAG(WES_FLAG_MIC_FAILURE_RCVD);
      if (DriverEnvironment_RegisterTimerCallback(MIC_CM_TIMEOUT, wifiEngineState.mic_cm_detect_timer_id, mic_failure_cb, 0)
          != 1)
      {
         DE_TRACE_STATIC(TR_AUTH, "No cm_detect timer callback registered, DE was busy\n");
      }
   }
   switch (ind_p->key_type)
   {
      case M80211_KEY_TYPE_GROUP:
         DriverEnvironment_indicate(WE_IND_GROUP_MIC_ERROR, 
                &net->bssId_AP, 
                sizeof(net->bssId_AP));
         break;
      case M80211_KEY_TYPE_PAIRWISE:
         DriverEnvironment_indicate(WE_IND_PAIRWISE_MIC_ERROR, 
                &net->bssId_AP, 
                sizeof(net->bssId_AP));
         break;
      default:
         DE_BUG_ON(1, "Unexpected key type in WiFiEngine_HandleMICFailure()\n");
         break;
   }

   return WIFI_ENGINE_SUCCESS;
}



struct key_entry_t {
   bool_t valid;
   m80211_set_key_descriptor_t desc;
   m80211_protect_type_t prot;
   int tx_key;
};

struct auth_state_t {
   struct key_entry_t keys[WIFI_ENGINE_MAX_WEP_KEYS];
};

static struct auth_state_t auth_state;

/*!
 * Get the index of the default key for the associated net (if any).
 *
 * @param index Output buffer.
 * @return 
 * - WIFI_ENGINE_SUCCESS on success.
 * - WIFI_ENGINE_FAILURE_NOT_ACCEPTED if no key is currently in use.
 */
int WiFiEngine_GetDefaultKeyIndex(int8_t *index)
{
   unsigned int i;
   
   for(i = 0; i < DE_ARRAY_SIZE(auth_state.keys); i++)
   {
      if(auth_state.keys[i].valid && 
         auth_state.keys[i].tx_key)
      {
         *index = i;
         return WIFI_ENGINE_SUCCESS;
      }
   }
   
   return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
}


/*!
 * @brief Get key information
 *
 * Get information about current keys, i.e get a cached key and key
 * length given a key index.
 *
 * @return
 * - WIFI_ENGINE_FAILURE if the index is invalid or if the key at the given
 *   index is not set.
 *
 */
int WiFiEngine_GetKey(uint32_t key_idx, char* key, size_t* key_len)
{
   if(key_idx >= DE_ARRAY_SIZE(auth_state.keys))
      return WIFI_ENGINE_FAILURE;

   if(!auth_state.keys[key_idx].valid)
      return WIFI_ENGINE_FAILURE;

   if(*key_len < auth_state.keys[key_idx].desc.key_len)
      return WIFI_ENGINE_FAILURE;
      
   *key_len = auth_state.keys[key_idx].desc.key_len;
   if(key)
      DE_MEMCPY(key, auth_state.keys[key_idx].desc.key.part, *key_len);
   return WIFI_ENGINE_SUCCESS;
}


/*
 * Initialize auth cache
 *
 * This function should typically be called from WiFiEngine_Initialize().
 * Clear the cache and make necessary preparations to use it.
 *
 * return
 * - WIFI_ENGINE_SUCCESS
 */
int wei_initialize_auth(void)
{
   cache_clear();
   return WIFI_ENGINE_SUCCESS;
}


/*
 * Reconfigure any static WEP keys and auth state
 *
 * This function should typically be called from WiFiEngine_Plug(). 
 * All static WEP keys that were previously set will be restored.
 * Encryption settings that is not availble in wifiEngineState will also
 * be restored.
 *
 * The purpose of this function is to be able to reconnect to an AP after a
 * firmware crash when static WEP keys have been used. Encryption methods
 * that uses other kind's of keys might not be entirely recovered by this
 * function. Those cases should be handled by a supplicant.
 *
 * Anyway, all the keys with a valid key index is stored in the cache in order
 * to provide e.g TKIP and CCMP keys by WiFiEngine_GetKey().
 *
 * return
 * - WIFI_ENGINE_SUCCESS if all cached keys could be set.
 * - WIFI_ENGINE_FAILURE is any key could not be set.
 */
int wei_reconfigure_auth(void)
{
   int status;
   unsigned int i;
   
   for(i = 0; i < DE_ARRAY_SIZE(auth_state.keys); i++) {
      struct key_entry_t* e = &auth_state.keys[i];
      if(!e->valid)
         continue;
      
      status = WiFiEngine_AddKey(e->desc.key_id, e->desc.key_len,
                                 (char *)e->desc.key.part, e->desc.key_type, e->prot,
                                 e->desc.config_by_authenticator,
                                 &e->desc.mac_addr, 
                                 &e->desc.receive_seq_cnt, e->tx_key);
      if(status != WIFI_ENGINE_SUCCESS) {
         DE_TRACE_INT(TR_AUTH, "Could not configure key[%d]\n", i);
         return WIFI_ENGINE_FAILURE;
      }

      DE_TRACE_INT(TR_AUTH, "Key[%d] configured successfully\n", i);
   }

   WiFiEngine_SetExcludeUnencryptedFlag(wifiEngineState.excludeUnencryptedFlag);
   WiFiEngine_SetAuthenticationMode(wifiEngineState.config.authenticationMode);
   WiFiEngine_SetEncryptionMode(wifiEngineState.config.encryptionMode);

   return WIFI_ENGINE_SUCCESS;
}


/*
 * Finalize auth cache
 *
 * This function should typically be called from WiFiEngine_Shutdown().
 * Release any dynamic memory allocations used by the auth cache.
 *
 * return
 * - WIFI_ENGINE_SUCCESS
 */
int wei_shutdown_auth(void)
{
   return WIFI_ENGINE_SUCCESS;
}

static void cache_new_tx_key(unsigned int index)
{
   unsigned int i;

   DE_ASSERT(index < DE_ARRAY_SIZE(auth_state.keys));

   for(i = 0; i < DE_ARRAY_SIZE(auth_state.keys); i++)
   {
      if(index==i) {
         auth_state.keys[index].tx_key = 1;
      } else {
         auth_state.keys[i].tx_key = 0;
      }
   }
}   


/*
 * Add an encryption key to the cache. Currently all the keys with a valid
 * key index will be added to the cache. There are only 4 slots in the cache,
 * so if the slot corresponding to key_idx is occupied, it's current contents
 * will be overwritten.
 *
 */
static int cache_add_key(uint32_t key_idx, size_t key_len, const void *key, 
                            m80211_key_type_t key_type,
                            m80211_protect_type_t prot,
                            int authenticator_configured,
                            m80211_mac_addr_t *bssid, 
                            receive_seq_cnt_t *rsc, int tx_key)
{
   struct key_entry_t* e;
   
   if(key_idx >= DE_ARRAY_SIZE(auth_state.keys))
      return WIFI_ENGINE_FAILURE;

   e = &auth_state.keys[key_idx];
   DE_MEMSET(e, 0, sizeof(*e));
   e->desc.key_id = (uint8_t) key_idx;
   e->desc.key_len = key_len;
   DE_MEMCPY(&e->desc.key, key, key_len);
   e->desc.config_by_authenticator = authenticator_configured;
   e->desc.mac_addr = *bssid;

   if(rsc)
      e->desc.receive_seq_cnt = *rsc;
   e->prot = prot;
   e->tx_key = tx_key;
   e->valid = 1;

   if(e->tx_key)
      cache_new_tx_key(key_idx);
   
   return WIFI_ENGINE_SUCCESS;
}

/* 
 * Delete an encryption key from the cache. Currently, only the key_idx
 * parameter is used to simply mark the corresponding slot as invalid.
 *
 */
static int cache_del_key(uint32_t key_idx, m80211_key_type_t key_type, 
                              m80211_mac_addr_t *bssid)
{
   if(key_idx >= DE_ARRAY_SIZE(auth_state.keys))
      return WIFI_ENGINE_FAILURE;

   /* Mark the key as invalid */
   auth_state.keys[key_idx].valid = 0;
   auth_state.keys[key_idx].tx_key = 0;
   return WIFI_ENGINE_SUCCESS;
}

/*
 * Clear cache.
 *
 */
static int cache_clear(void)
{
   DE_MEMSET(&auth_state.keys, 0, sizeof(auth_state.keys));
   return WIFI_ENGINE_SUCCESS;
}


/** @} */ /* End of we_auth group */

