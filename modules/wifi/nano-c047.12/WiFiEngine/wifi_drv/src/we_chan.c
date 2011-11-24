/* $Id: we_chan.c,v 1.10 2008-04-07 14:26:29 joda Exp $ */
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
This module implements frequency/channel conversion functions

*****************************************************************************/
/** @defgroup we_chan WiFiEngine frequency/channel conversion functions
 *
 * @brief WiFiEngine frequency/channel conversion functions
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
#include "hmg_defs.h"
#include "mlme_proxy.h"
#include "hicWrapper.h"
#include "macWrapper.h"
#include "wei_asscache.h"
#include "pkt_debug.h"
#include "state_trace.h"

/* #define HAVE_802_11a	1 */
/* #define HAVE_802_11h	1 */
#define HAVE_802_11bg	1
/* #define HAVE_802_11j	1 */

#define F(N) ((N) * 1000)

#define F11b(C) { (C), F(2412 + ((C) - 1) * 5) }
#define F11a(C) { (C), F(5000 + (C) * 5) }
#define F11j(C) { (C), F(4000 + (C) * 5) }

/* US Public Safety 4.9GHz band defines 
   Fps(C) = F(4920 + 0.5 * C) C = 5, 10, ..., 95 
*/

static const struct ftable { 
   uint8_t channel; 
   unsigned long khz;
} ftable[] = {
#if defined(HAVE_802_11bg)
   /* 802.11b/g */
   F11b(1),
   F11b(2),
   F11b(3),
   F11b(4),
   F11b(5),
   F11b(6),
   F11b(7),
   F11b(8),
   F11b(9),
   F11b(10),
   F11b(11),
   F11b(12),
   F11b(13),
   { 14, F(2484) },
#endif

#if defined(HAVE_802_11a) || defined(HAVE_802_11h)
   /* 802.11a/h */
   F11a(36),
   F11a(40),
   F11a(44),
   F11a(48),
   F11a(52),
   F11a(56),
   F11a(60),
   F11a(64),
#endif

#if defined(HAVE_80211h)
   /* 802.11h */
   F11a(100),
   F11a(104),
   F11a(108),
   F11a(112),
   F11a(116),
   F11a(120),
   F11a(124),
   F11a(128),
   F11a(132),
   F11a(136),
   F11a(140),
#endif

#if defined(HAVE_80211a)
   /* 802.11a */
   F11a(149),
   F11a(153),
   F11a(157),
   F11a(161),
#endif

#if defined(HAVE_80211j)
   /* 802.11j */
   F11j(184),
   F11j(188),
   F11j(192),
   F11j(196)
#endif
};


/*! 
 *  @brief Convert a channel number to a frequency
 *
 *  @param channel IN  the channel number
 *  @param khz     OUT returned frequency in kHz
 *
 *  @return Returns WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE on unknown channel numbers
 */
int
WiFiEngine_Channel2Frequency(uint8_t channel, unsigned long *khz)
{
   unsigned int i;

   for(i = 0; i < DE_ARRAY_SIZE(ftable); i++) {
      if(channel == ftable[i].channel) {
         *khz = ftable[i].khz;
         return WIFI_ENGINE_SUCCESS;
      }
   }
   
   return WIFI_ENGINE_FAILURE;
}

/*! 
 *  @brief Convert a frequency to a channel number
 *
 *  @param khz     IN  frequency in kHz
 *  @param channel OUT channel number
 *
 *  @return Returns WIFI_ENGINE_SUCCESS on success. WIFI_ENGINE_FAILURE on unknown frequencies
 */
int
WiFiEngine_Frequency2Channel(unsigned long khz, uint8_t *channel)
{
   unsigned int i;
   
   for(i = 0; i < DE_ARRAY_SIZE(ftable); i++) {
      if(khz == ftable[i].khz) {
         *channel = ftable[i].channel;
         return WIFI_ENGINE_SUCCESS;
      }
   }
   return WIFI_ENGINE_FAILURE;
}

/** @} */ /* End of we_chan group */

