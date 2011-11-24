/* $Id: we_rate.c,v 1.9 2007-11-13 17:21:31 ulla Exp $ */
/*****************************************************************************

Copyright (c) 2007 by Nanoradio AB

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
This module implements bit rate conversion functions

*****************************************************************************/
/** @defgroup we_rate WiFiEngine bit rate conversion functions
 *
 * @brief WiFiEngine bit rate conversion functions
 *
 *  @{
 */

#include "wifi_engine_internal.h"
#include "registryAccess.h"

#define WEI_RATE_UNMASK_BASIC(x) (x & (~(1<<7)))

#ifdef __GNUC__ /* really C99 */
/* C99 supports designated initializers, but apparently the Windows
 * WDK compiler it still living la vida loca of the nineties */
#define DI(I, V) [I] = V
#else
/* this still works as long as the indices are sorted */
#define DI(I, V) V
#endif

static void wei_set_enabled_ht_rates_from_registry(void);

/*!
 * @brief Translate a WiFiEngine native rate to bits per second.
 * @param rate WiFiEngine rate code
 * @return Bitrate (in b/s), or zero if there was no matching rate.
 */
uint32_t
WiFiEngine_rate_native2bps(we_xmit_rate_t rate)
{
   return (WEI_RATE_UNMASK_BASIC(rate)*500000);
}

/*!
 * @brief Translate a rate in bits per second to WiFiEngine native rate code.
 * @param rate Rate in b/s.
 * @return A WiFiEngine bit rate code, or WE_XMIT_RATE_INVALID if no match.
 */
we_xmit_rate_t
WiFiEngine_rate_bps2native(uint32_t rate)
{
   return (rate/500000);
}

/*!
 * @brief Translate a WiFiEngine native rate to IEEE 802.11 rate code.
 * @param rate WiFiEngine rate code
 * @return Bitrate (in 500kb/s), or zero if there was no matching rate.
 */
uint8_t
WiFiEngine_rate_native2ieee(we_xmit_rate_t rate)
{
   return (WEI_RATE_UNMASK_BASIC(rate));
}

/*!
 * Translate a rate in IEEE 802.11 format to WiFiEngine native rate code.
 * @param rate Bitrate in 500kb/s units (with or without basic rate bit set).
 * @return A WiFiEngine bit rate code, or WE_XMIT_RATE_INVALID if no match.
 */
we_xmit_rate_t
WiFiEngine_rate_ieee2native(uint8_t rate)
{
   return rate;
}

/* 
 * This table provide a mapping between internal rate masks and
 * protocol rate codes.  Each rate is coded as a 32-bit integer,
 * currently with three fields: domain, mask bit position, and
 * ratecode. The remaining 17 bits are reserved:
 * 
 *  domain[2] | reserved[17] | bitposition[5] | code[8]
 *   BG-rates: domain == 0, code == speed in 500kbit/s units
 *   HT-rates: domain == 1, code == MCS index
 */
static const uint32_t supported_rate_table[] = {
   WE_RATE_ENTRY_BG(1,    2),
   WE_RATE_ENTRY_BG(2,    4),
   WE_RATE_ENTRY_BG(5_5, 11),
   WE_RATE_ENTRY_BG(11,  22),

   WE_RATE_ENTRY_BG(6,   12),
   WE_RATE_ENTRY_BG(9,   18),
   WE_RATE_ENTRY_BG(12,  24),
   WE_RATE_ENTRY_BG(18,  36),
   WE_RATE_ENTRY_BG(22,  44),
   WE_RATE_ENTRY_BG(24,  48),
   WE_RATE_ENTRY_BG(33,  66),
   WE_RATE_ENTRY_BG(36,  72),
   WE_RATE_ENTRY_BG(48,  96),
   WE_RATE_ENTRY_BG(54, 108),

#ifdef M80211_XMIT_RATE_6_5MBIT
   WE_RATE_ENTRY_HT(6_5,  0),
   WE_RATE_ENTRY_HT(13,   1),
   WE_RATE_ENTRY_HT(19_5, 2),
   WE_RATE_ENTRY_HT(26,   3),
   WE_RATE_ENTRY_HT(39,   4),
   WE_RATE_ENTRY_HT(52,   5),
   WE_RATE_ENTRY_HT(58_5, 6),
   WE_RATE_ENTRY_HT(65,   7),
#endif
};

static void wei_update_rate_table(const void *table, size_t len)
{
   unsigned int i;
   uint32_t brates = 0;
   uint32_t grates = 0;
#if DE_ENABLE_HT_RATES == CFG_ON
   uint32_t htrates = 0;
#endif

   wifiEngineState.rate_table_len = 0;
   for(i = 0; i + sizeof(uint32_t) <= len; i += sizeof(uint32_t)) {
      uint32_t rate;
      /* XXX BYTEORDER: the built-in table above is in native
   byteorder, but what we get from firmware is in LE
   byteorder */
   DE_MEMCPY(&rate, (const unsigned char*)table + i, sizeof(uint32_t));
   wifiEngineState.rate_table[wifiEngineState.rate_table_len++] = rate;

   if(WE_RATE_ENTRY_IS_BG(rate)) {
      if(WE_RATE_ENTRY_CODE(rate) == 2
       || WE_RATE_ENTRY_CODE(rate) == 4
       || WE_RATE_ENTRY_CODE(rate) == 11
       || WE_RATE_ENTRY_CODE(rate) == 22)
         brates |= (1 << WE_RATE_ENTRY_BITPOS(rate));
      }
      else
      {
         grates |= (1 << WE_RATE_ENTRY_BITPOS(rate));
      }

#if DE_ENABLE_HT_RATES == CFG_ON
      if(WE_RATE_ENTRY_IS_HT(rate)) {
         htrates |= (1 << WE_RATE_ENTRY_BITPOS(rate));
      }
#endif
   }
   wifiEngineState.rate_bmask = brates;
   wifiEngineState.rate_gmask = grates;
#if DE_ENABLE_HT_RATES == CFG_ON
   wifiEngineState.rate_htmask = htrates;
#endif

   wei_set_enabled_ht_rates_from_registry();
}

static int wei_rate_table_mib_callback(we_cb_container_t *cbc)
{
   if(cbc->status == 0) {
      wei_update_rate_table(cbc->data, cbc->data_len);
   }
   return 0;
}

static uint32_t wei_get_ratedef_from_rateindex(uint32_t rate_index)
{
   unsigned int i;
   
   for (i = 0; i < wifiEngineState.rate_table_len; i++) {
      if(rate_index == WE_RATE_ENTRY_BITPOS(wifiEngineState.rate_table[i]))
         return wifiEngineState.rate_table[i];
   }
   return 0xffffffff;
}

static const uint32_t mcs_linkspeed[] = {
   6500,
   13000,
   19500,
   26000,
   39000,
   52000,
   58500,
   65000
};

/* convert a rate bit position to a linkspeed in kbit/s */
int
WiFiEngine_GetRateIndexLinkspeed(uint32_t rate_index, uint32_t *linkspeed)
{
   uint32_t ratedef;
   
   ratedef = wei_get_ratedef_from_rateindex(rate_index);

   if(WE_RATE_ENTRY_IS_BG(ratedef)) {
      *linkspeed = WE_RATE_ENTRY_CODE(ratedef) * 500;
      return WIFI_ENGINE_SUCCESS;
   }
   if(WE_RATE_ENTRY_IS_HT(ratedef)) {
      if(WE_RATE_ENTRY_CODE(ratedef) < DE_ARRAY_SIZE(mcs_linkspeed)) {
         *linkspeed = mcs_linkspeed[WE_RATE_ENTRY_CODE(ratedef)];
         return WIFI_ENGINE_SUCCESS;
      }
   }
   return WIFI_ENGINE_FAILURE_INVALID_DATA;
}

/* this is masked with hw supported rates */
int
WiFiEngine_SetEnabledHTRates(uint32_t ratemask)
{
#if DE_ENABLE_HT_RATES == CFG_ON
   wifiEngineState.enabled_ht_rates = ratemask;
#endif
   return WIFI_ENGINE_SUCCESS;
}

int
WiFiEngine_GetEnabledHTRates(uint32_t *ratemask)
{
#if DE_ENABLE_HT_RATES == CFG_ON
   *ratemask = WE_SUPPORTED_HTRATES();
#else
   *ratemask = 0;
#endif
   return WIFI_ENGINE_SUCCESS;
}

/* convert mcs rate mask to internal rate mask format; only handles
 * lower 32 mcs rates for now, returns TRUE if all rates are supported
 * by firmware */
static int
wei_mcs_rates_to_rate_mask(unsigned int mcs_rates, unsigned int *rate_mask)
{
   int status;
   uint32_t val[1];
   unsigned int mcs_rate;
   unsigned int bit;
   int result = TRUE;

   val[0] = mcs_rates;
   *rate_mask = 0;
   WE_BITMASK_FOREACH_SET(mcs_rate, val) {
      status = WiFiEngine_RateCodeToBitposition(WE_RATE_DOMAIN_HT,
                                                mcs_rate,
                                                &bit);
      if(status != WIFI_ENGINE_SUCCESS) {
         result = FALSE;
         continue;
      }
      *rate_mask |= (1 << bit);
   }
   return result;
}

static void
wei_set_enabled_ht_rates_from_registry(void)
{
   uint32_t val;
   rBasicWiFiProperties *bwp;
   unsigned int rate_mask;

   REGISTRY_RLOCK();
   bwp = (rBasicWiFiProperties*)Registry_GetProperty(ID_basic);  
   val = bwp->ht_rates;
   REGISTRY_RUNLOCK();

   wei_mcs_rates_to_rate_mask(val, &rate_mask);

   WiFiEngine_SetEnabledHTRates(rate_mask);
}

/* called from Configure_Device */
int
wei_rate_configure(void)
{
   we_cb_container_t *cbc;
   int status;

   /* fill in default rate table, this will be overwritten by a mib
    * get, if the mib is present in firmware */
   wei_update_rate_table(supported_rate_table, sizeof(supported_rate_table));


   cbc = WiFiEngine_BuildCBC(wei_rate_table_mib_callback, NULL, 0, FALSE);
   if(cbc == NULL) {
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }

   status = WiFiEngine_GetMIBAsynch(MIB_dot11SupportedRateTable, 
				    cbc);
			 
   return status;
}


int
WiFiEngine_RateCodeToBitposition(unsigned int rate_domain,
                                 unsigned int rate_code,
                                 unsigned int *bitposition)
{
   unsigned int i;

   for(i = 0; i < wifiEngineState.rate_table_len; i++) {
      uint32_t rate = wifiEngineState.rate_table[i];
      if(WE_RATE_ENTRY_FIELD(rate, DOMAIN) == rate_domain
         && WE_RATE_ENTRY_CODE(rate) == rate_code) {
         *bitposition = WE_RATE_ENTRY_BITPOS(rate);
         return WIFI_ENGINE_SUCCESS;
      }
   }
   return WIFI_ENGINE_FAILURE_INVALID_DATA;
}


/** @} */ /* End of we_rate group */


