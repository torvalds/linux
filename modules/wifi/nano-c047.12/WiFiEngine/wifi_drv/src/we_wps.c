/*
 *
 * Copyright (c) 2007-2009 Nanoradio AB. All rights reserved.
 *
 */
#include "driverenv.h"
#include "wifi_engine.h"

int
m80211_tlv_pars_next(
   uint8_t *buf,
   size_t len,
   size_t *start,
   m80211_tlv_t *tlv,
   void **tlv_data)
{
   if (len<*start+4)
      return 0;

   tlv->type = DE_GET_UBE16(buf + *start);
   tlv->len  = DE_GET_UBE16(buf + *start + 2);

   /* sanity */
   if (tlv->len > (len - 4 - (*start)))
   {
      // DE_ASSERT(FALSE);
      return FALSE;
   }

   if (tlv_data)
   {
      *tlv_data = (&buf[0] + (*start+2+2));
   }

   *start += (2+2+ (tlv->len));

   return ( *start <= len );
}


int
m80211_tlv_find(
   uint8_t *buf,
   size_t len,
   uint16_t tlv_type,
   m80211_tlv_t *tlv,
   void **tlv_data)
{
   size_t tlv_off = 0;

   while (m80211_tlv_pars_next(buf, len, &tlv_off, tlv, tlv_data))
   {
      if (tlv->type==tlv_type)
      {
         return TRUE;
      }
   }
   return FALSE;
}


#define WSC_ID_ASSOC_STATE          0x1002
#define WSC_ID_AUTHENTICATOR        0x1005
#define WSC_ID_AUTH_TYPE            0x1003
#define WSC_ID_AUTH_TYPE_FLAGS      0x1004
#define WSC_ID_CONFIG_ERROR         0x1009
#define WSC_ID_CONFIG_METHODS       0x1008
#define WSC_ID_CONN_TYPE_FLAGS      0x100D
#define WSC_ID_CREDENTIAL           0x100E
#define WSC_ID_DEVICE_NAME          0x1011
#define WSC_ID_DEVICE_PWD_ID        0x1012
#define WSC_ID_E_HASH1              0x1014
#define WSC_ID_E_HASH2              0x1015
#define WSC_ID_ENCR_SETTINGS        0x1018
#define WSC_ID_ENCR_TYPE            0x100F
#define WSC_ID_ENCR_TYPE_FLAGS      0x1010
#define WSC_ID_ENROLLEE_NONCE       0x101A
#define WSC_ID_KEY_WRAP_AUTH        0x101E
#define WSC_ID_MAC_ADDR             0x1020
#define WSC_ID_MANUFACTURER         0x1021
#define WSC_ID_MODEL_NAME           0x1023
#define WSC_ID_MODEL_NUMBER         0x1024
#define WSC_ID_MSG_TYPE             0x1022
#define WSC_ID_NW_INDEX             0x1026
#define WSC_ID_NW_KEY               0x1027
#define WSC_ID_NW_KEY_INDEX         0x1028
#define WSC_ID_OS_VERSION           0x102D
#define WSC_ID_PRIM_DEV_TYPE        0x1054
#define WSC_ID_PUBLIC_KEY           0x1032
#define WSC_ID_REGISTRAR_NONCE      0x1039
#define WSC_ID_RF_BAND              0x103C
#define WSC_ID_R_HASH1              0x103D
#define WSC_ID_R_HASH2              0x103E
#define WSC_ID_SC_STATE             0x1044
#define WSC_ID_SELECTED_REGISTRAR   0x1041
#define WSC_ID_SERIAL_NUM           0x1042
#define WSC_ID_SSID                 0x1045
#define WSC_ID_UUID_E               0x1047
#define WSC_ID_UUID_R               0x1048
#define WSC_ID_VERSION              0x104A


/* An AP announcing a selected registrar */
int m80211_ie_is_wps_configured(m80211_ie_wps_parameter_set_t *ie)
{
   size_t start = 0;
   int wps_pool_len;
   m80211_tlv_t tlv;
   void *void_ptr;

   int selected_registrar_found = 0;
   int push_button_configured = 0;

   /* sanity */
   if (ie->hdr.hdr.id == M80211_IE_ID_NOT_USED)
      return FALSE;

   if (ie->hdr.OUI_type != WPS_IE_OUI_TYPE)
      return FALSE;

   /* sizeof(m80211_ie_vendor_specific_hdr_t) - sizeof(m80211_ie_hdr_t) == 4 bytes */
   wps_pool_len = ie->hdr.hdr.len - M80211_IE_ID_VENDOR_SPECIFIC_HDR_SIZE;

   if (wps_pool_len < 4) // at least one tlv pair
      return FALSE;

   while (m80211_tlv_pars_next(
             (unsigned char*)ie->wps_pool,
             wps_pool_len,
             &start,
             &tlv,
             &void_ptr))
   {
      unsigned char *tlv_data = (unsigned char *)void_ptr;
      switch (tlv.type)
      {

      /* in scan before cred. neg. */
      case WSC_ID_SELECTED_REGISTRAR:
      {
         if ((tlv.len == 1) &&
               ((*tlv_data) == 1))
         {
            selected_registrar_found = 1;
         }
         break;
      }

      /* in scan before cred. neg. */
      case WSC_ID_DEVICE_PWD_ID:
      {
         if (tlv.len == 2)
         {
            uint16_t m;

            m = DE_GET_UBE16(tlv_data);
            if(m == 4)
            {
               push_button_configured = 1;
            }
         }
         break;
      }

      /********************************/

      /* in scan after cred. neg. success, ie in idle mode */
      /* safe to ignore */
      case WSC_ID_VERSION:
      case WSC_ID_SC_STATE:         /* 1: 0x02 */
      case 0x103b:                  /* type AP 1: 0x03 */
      case WSC_ID_UUID_E:           /* 0x10: ... */
      case WSC_ID_CONFIG_METHODS:   /* 2: 0x008c */
      /* and some vendor tlv's */
      case WSC_ID_MANUFACTURER:
      case WSC_ID_MODEL_NAME:
      case WSC_ID_MODEL_NUMBER:
      case WSC_ID_SERIAL_NUM:
      case WSC_ID_PRIM_DEV_TYPE:
      case WSC_ID_DEVICE_NAME:
         break;

      default:
#if 0
         DE_TRACE_INT3(TR_ALWAYS, "ignore type 0x%X len %d at offset %d\n",
               tlv.type,
               tlv.len,
               ((uint8_t*)tlv_data - (uint8_t*)ie->wps_pool));
#endif
         break;
      }
   }

   if (selected_registrar_found == 0)
      return FALSE;

   return TRUE;
}

/* 
 * Workaround for some platforms
 *
 * WARNING: THIS WILL MODIFY THE IE
 *
 * return 1 if something was modified
 *
 */
#ifdef WPS_REMOVE_CONFIGURED_BIT
int m80211_ie_remove_wps_configured(m80211_ie_wps_parameter_set_t *ie)
{
   int wps_pool_len;
   m80211_tlv_t tlv;
   unsigned char *tlv_data;

   if(FALSE==m80211_ie_is_wps_configured(ie))
   {
      /* nop */
      return 0;
   }

   wps_pool_len = ie->hdr.hdr.len - M80211_IE_ID_VENDOR_SPECIFIC_HDR_SIZE;

   m80211_tlv_find(
         (unsigned char*)ie->wps_pool,
         wps_pool_len,
         WSC_ID_SELECTED_REGISTRAR,
         &tlv, 
         (void**)&tlv_data);

   *tlv_data = 0;

   return 1;
}
int we_net_wps_remove_configured(WiFiEngine_net_t* net)
{
   m80211_ie_wps_parameter_set_t *ie;
   ie = &net->bss_p->bss_description_p->ie.wps_parameter_set;
   return m80211_ie_remove_wps_configured(ie);
}
#endif

