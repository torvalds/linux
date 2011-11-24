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
*****************************************************************************/

#include <wifi_engine_internal.h>

#define TR_WLP TR_INITIALIZE

/*------------------------------------------------------------*/
struct sconf {
   WEI_TQ_ENTRY(sconf) next;
   void (*conf_func)(void *user_data, const void *data, size_t data_len);
   void *user_data;
   size_t data_len;
   unsigned char data[1];
};

static WEI_TQ_HEAD(, sconf) sconf_head = WEI_TQ_HEAD_INITIALIZER(sconf_head);

/*------------------------------------------------------------*/
static void
mib_set_foo(void *user_data, const void *data, size_t data_len)
{
   int status;
   mib_id_t m;

   if(data_len < sizeof(m.octets)) {
      DE_TRACE_STATIC(TR_WLP, "internal error\n");
      return;
   }
   DE_MEMCPY(m.octets, data, sizeof(m.octets));
   status = wei_send_mib_set_binary(m, NULL, 
                                    (const unsigned char*)data + sizeof(m.octets),
                                    data_len - sizeof(m.octets),
                                    NULL);
   if(status != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_INT(TR_WLP, "failed to send mib (%d)\n", status);
   }
}

int
wei_add_startup_mib_set(const mib_id_t *mib_id, 
                        const void *data, 
                        size_t data_len)
{
   struct sconf *ss;

   /* remove existing mib sets */
   wei_remove_startup_mib_set(mib_id);

   ss = (struct sconf *)DriverEnvironment_Malloc(sizeof(*ss) - 1 + 
                                                 sizeof(mib_id->octets) + 
                                                 data_len);
   if(ss == NULL) {
      DE_TRACE_STATIC(TR_WLP, "failed to allocate memory for MIB\n");
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   ss->conf_func = mib_set_foo;
   ss->user_data = NULL;
   ss->data_len = sizeof(mib_id->octets) + data_len;
   DE_MEMCPY(ss->data, mib_id->octets, sizeof(mib_id->octets));
   DE_MEMCPY(ss->data + sizeof(mib_id->octets), data, data_len);

   WEI_TQ_INSERT_TAIL(&sconf_head, ss, next);
   
   return WIFI_ENGINE_SUCCESS;
}

int
wei_remove_startup_mib_set(const mib_id_t *mib_id)
{
   struct sconf *ss, *tmp;

   ss = WEI_TQ_FIRST(&sconf_head);
   while(ss != NULL) {
      if(ss->conf_func == mib_set_foo 
         && ss->data_len >= sizeof(mib_id->octets) 
         && DE_MEMCMP(ss->data, mib_id->octets, sizeof(mib_id->octets)) == 0) {
         tmp = WEI_TQ_NEXT(ss, next);
         WEI_TQ_REMOVE(&sconf_head, ss, next);
         DriverEnvironment_Free(ss);
         ss = tmp;
      } else {
         ss = WEI_TQ_NEXT(ss, next);
      }
   }
   
   return WIFI_ENGINE_SUCCESS;
}

/*------------------------------------------------------------*/

static void
bt_conf(void *user_data, const void *data, size_t data_len)
{
   int status;
   const unsigned char *pp = (const unsigned char *)data;

   if(data_len == 9) {
      uint8_t pta_def[5];
      DE_MEMCPY(pta_def, pp + 3, sizeof(pta_def));
      status = WiFiEngine_ConfigBTCoex(pp[1],  /* bt_vendor */
                                       pp[2],  /* pta_mode */
                                       pta_def,
                                       5, /* len */
                                       pp[8] & 1, /* antenna_dual */
                                       pp[8] & 2, /* antenna_sel0 */
                                       pp[8] & 4, /* antenna_sel1 */
                                       pp[8] & 8, /* antenna_level0 */
                                       pp[8] & 16); /* antenna_level1 */
      if(status != WIFI_ENGINE_SUCCESS) {
         DE_TRACE_INT(TR_WLP, "failed to configure BT coex: %d\n", status);
         return;
      }
   }
   if(data_len == 1 || data_len == 9) {
      status = WiFiEngine_EnableBTCoex(pp[0]);
      if(status != WIFI_ENGINE_SUCCESS)
         DE_TRACE_INT(TR_WLP, "failed to enable BT coex: %d\n", status);
      return;
   }
}

/*------------------------------------------------------------*/

#define NVMEM_CFG_IN_NVM_TAG 0xFE
#define NVMEM_VERSION_IN_FLASH 0x01

/*
 * Parses format used in firmware EEPROM
 *
 * mib-data    ::= *mib-entry
 * mib-entry   ::= mib-header *mib-record mib-trailer
 * mib-header  ::= TAG[1] VERSION[1] PAD[6]
 * mib-record  ::= LEN[2] MIB[8] DATA[LEN]
 * mib-trailer ::= END[2]
 * TAG         ::= 0xfe
 * VERSION     ::= 0x01
 * END         ::= 0xff, 0xff
 */
static int
parse_nvmem_mib_format(const unsigned char *pp, size_t len)
{
   mib_id_t mib_id;
   unsigned int mib_len;

   while(len >= 8
         && pp[0] == NVMEM_CFG_IN_NVM_TAG
         && pp[1] == NVMEM_VERSION_IN_FLASH) {
      pp += 8;
      len -= 8;
      while(len >= 2) {
         int mib_data_len;
         if(pp[0] == 0xff && pp[1] == 0xff){
            /* end tag */
            pp += 2;
            len -= 2;
            break;
         }
         mib_len = pp[0] | (pp[1] << 8);
         if(len < 2 + mib_len) {
            len = 0;
            break;
         }
         pp += 2;
         len -= 2;
         DE_MEMCPY(mib_id.octets, pp, sizeof(mib_id.octets));
         pp += sizeof(mib_id.octets);
         len -= sizeof(mib_id.octets);
         mib_data_len = mib_len - sizeof(mib_id.octets);
         wei_add_startup_mib_set(&mib_id, pp, mib_data_len);
         pp += mib_data_len;
         len -= mib_data_len;
      }
   }
   return WIFI_ENGINE_SUCCESS;
}

/*------------------------------------------------------------*/
#define WE_WLP_TAG       0x87
#define WE_WLP_VERSION_1 1

#define WE_WLP_CONTINUATION_BIT 0x80

#define WE_WLP_EMPTY       0
#define WE_WLP_PATH_LOSS   1
#define WE_WLP_END         15
#define WE_WLP_NVMEM_MIB   128
#define WE_WLP_BTCOEX      129
#define WE_WLP_MIB         130

/*
 * Parses format used by WiFiEngine. Main difference with the firmware
 * MIB format is that it supports configuration parameters that are
 * not MIBs.
 */
static int
parse_wlp_v1_format(const unsigned char *pp, size_t param_len)
{
   mib_id_t mib_id;
   struct sconf *ss;

   if(param_len < 2 || pp[0] != WE_WLP_TAG || pp[1] != WE_WLP_VERSION_1)
      return WIFI_ENGINE_SUCCESS;
   pp += 2;
   param_len -= 2;
   while(param_len > 0) {
      unsigned int tag;
      unsigned int len;
      unsigned int cont;
    
      /* the tag is either contained in the first byte along with the
         length, allowing for tags 0-15, and lengths 0-7 */ 
      if((*pp & WE_WLP_CONTINUATION_BIT) == 0) {
         /* 0 | TAG[4] | LEN[3] */
         tag = *pp >> 3; /* 0 - 15 */
         len = *pp & 7;
         cont = 0;
      } else {
         /* or the first byte contains the tag, followed by a variable
            length encoded length */
         /* 1 | TAG[7] */
         tag = *pp; /* 128 - 255 */
         len = 0;
         cont = 1;
      }
      pp++;
      param_len--;
      /* as long as the contiuation bit is set, we shift in more
       * bits in the length */
      while(param_len > 0 && cont) {
         /* C | LEN[7] */
         len <<= 7;
         len += *pp & 0x7f;
         cont = *pp & WE_WLP_CONTINUATION_BIT;
         pp++;
         param_len--;
      }
      if(cont) {
         /* if contiunation bit it still set, it means we exhausted
            the buffer */
         DE_TRACE_INT(TR_WLP, "tag %u has bad encoding\n", tag);
         break;
      }
      if(len > param_len) {
         DE_TRACE_INT(TR_WLP, "tag %u specifies too much data\n", tag);
         break;
      }
      switch(tag) {
         default:
            DE_TRACE_INT(TR_WLP, "unexpected tag %u\n", tag);
            return WIFI_ENGINE_FAILURE;
         case WE_WLP_END:
            DE_TRACE_STATIC(TR_WLP, "WE_WLP_END\n");
            return WIFI_ENGINE_SUCCESS;
         case WE_WLP_EMPTY:
            /* this is a filler, 1 to 8 bytes long */
            DE_TRACE_STATIC(TR_WLP, "WE_WLP_EMPTY\n");
            break;
         case WE_WLP_BTCOEX:
            DE_TRACE_STATIC(TR_WLP, "WE_WLP_BTCOEX\n");
            if(len == 1 || len == 9) {
               /* see if there already is one bt entry */
               WEI_TQ_FOREACH(ss, &sconf_head, next) {
                  if(ss->conf_func == bt_conf)
                     break;
               }
               if(ss == NULL) {
                  /* always allocate 9 bytes so we can reuse this slot */
                  ss = (struct sconf *)DriverEnvironment_Malloc(sizeof(*ss) - 1 + 9);
                  WEI_TQ_INSERT_TAIL(&sconf_head, ss, next);
               }
               ss->data_len = len;
               ss->conf_func = bt_conf;
               DE_MEMCPY(ss->data, pp, len);
            } else {
               DE_TRACE_INT(TR_WLP, "bad BT coex parameter length: %u\n", len);
            }
            break;
         case WE_WLP_MIB:
            DE_TRACE_STATIC(TR_WLP, "WE_WLP_MIB\n");
            if(len >= sizeof(mib_id.octets)) {
               DE_MEMCPY(mib_id.octets, pp, sizeof(mib_id.octets));
               wei_add_startup_mib_set(&mib_id, 
                                       pp + sizeof(mib_id.octets), 
                                       len - sizeof(mib_id.octets));
            } else {
               DE_TRACE_INT(TR_WLP, "MIB parameter too short: %u\n", len);
            }
            break;
         case WE_WLP_NVMEM_MIB:
            DE_TRACE_STATIC(TR_WLP, "WE_WLP_NVMEM_MIB\n");
            parse_nvmem_mib_format(pp, len);
            break;
         case WE_WLP_PATH_LOSS:
            DE_TRACE_STATIC(TR_WLP, "WE_WLP_PATH_LOSS\n");
            /* decode integer here */
            break;
      }
      pp += len;
      param_len -= len;
   }
   return WIFI_ENGINE_SUCCESS;
}

int
wei_apply_wlan_parameters(void)
{
   struct sconf *ss;

   WEI_TQ_FOREACH(ss, &sconf_head, next) {
      (*ss->conf_func)(ss->user_data, ss->data, ss->data_len);
   }
   return WIFI_ENGINE_SUCCESS;
}

/*------------------------------------------------------------*/
int
we_wlp_shutdown(void)
{
   struct sconf *ss;

   while((ss = WEI_TQ_FIRST(&sconf_head)) != NULL) {
      WEI_TQ_REMOVE(&sconf_head, ss, next);
      DriverEnvironment_Free(ss);
   }
   return WIFI_ENGINE_SUCCESS;
}

int
we_wlp_configure_device(void)
{
   return wei_apply_wlan_parameters();
}
/*------------------------------------------------------------*/

/*!
 * @brief Loads WLAN parameters from a memory area.
 *
 * @param [in] param Pointer to the memory are to load from 
 * @param [in] size  Size of param
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE if the parameter area is not in a known format
 */
int
WiFiEngine_LoadWLANParameters(const void *param, size_t size)
{
   const unsigned char *pp;

   pp = (const unsigned char *)param;
   /* First check for MAC address. We treat anything that is exactly
    * six bytes, does not have the multicast bit set, and is not all
    * zeros as a valid MAC address. Note that both the NVMEM magic and
    * the WLP magic have the multicast bit set. */
   if(size == M802_ADDRESS_SIZE 
      && (pp[0] & M80211_MAC_MULTICAST) == 0
      && (pp[0] | pp[1] | pp[2] | pp[3] | pp[4] | pp[5]) != 0) {
      /* treat this as a mac address */
      static const mib_id_t macaddress_mibid = { 
         { 2, 1, 1, 1, 0, 0, 0, 0 } /* MIB_dot11MACAddress */
      };
      wei_add_startup_mib_set(&macaddress_mibid, param, size);
      return WIFI_ENGINE_SUCCESS;
   }
   /* check magic */
   if(size < 2 
      || (pp[0] == 0 && pp[1] == 0) 
      || (pp[0] == 0xff && pp[1] == 0xff)) {
      /* uninitialised area, silently treat this as success */
      return WIFI_ENGINE_SUCCESS;
   }
   if(pp[0] == NVMEM_CFG_IN_NVM_TAG && pp[1] == NVMEM_VERSION_IN_FLASH) {
      parse_nvmem_mib_format((const unsigned char *)param, size);
   } else if(pp[0] == WE_WLP_TAG && pp[1] == WE_WLP_VERSION_1) {
      parse_wlp_v1_format((const unsigned char *)param, size);
   } else {
      DE_TRACE_INT2(TR_WLP, "unexpected magic in wlan parameters (0x%02x%02x)\n", 
                    pp[0], pp[1]);
      return WIFI_ENGINE_FAILURE;
   }

   return WIFI_ENGINE_SUCCESS;
}

/*!
 * @brief Parse one or several consequtive Nvmem (MIB flash data) 
 * structure(s) and generate all corresponding MIBSet calls.
 *
 * @param inbuf Input buffer
 * @param len Length of the input buffer
  *
 * @return 
 * - WIFI_ENGINE_SUCCESS on success
 * - WIFI_ENGINE_FAILURE on failure
 */
int
WiFiEngine_SendMIBSetFromNvmem(const void *inbuf, size_t len)
{
   WiFiEngine_LoadWLANParameters(inbuf, len);

   if (WES_TEST_FLAG(WES_FLAG_HW_PRESENT)) 
      wei_apply_wlan_parameters();

   return WIFI_ENGINE_SUCCESS;
}

