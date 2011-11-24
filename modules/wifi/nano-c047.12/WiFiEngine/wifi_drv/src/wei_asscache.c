/* $Id: wei_asscache.c,v 1.14 2008-03-14 15:56:33 miwi Exp $ */

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

*****************************************************************************/
/** @defgroup wei_asscache WiFiEngine internal association request/response cache.
 *
 * \brief This module implements a association request/response cache for
 * WPA/RSN support in WiFiEngine.
 *
 * The association request and confirm messages are needed by the supplicant for
 * higher level association protocols. This module implements a cache for those
 * messages so that we can retrieve them when prompted by the supplicant.
 *
 *  @{
 */


#include "driverenv.h"
#include "wei_asscache.h"
#include "hicWrapper.h"
#include "wifi_engine_internal.h"

struct asscache_s {
      m80211_mlme_associate_req_t *req;
      m80211_mlme_associate_cfm_t *cfm;
};

static struct asscache_s asscache;

void wei_asscache_init(void)
{
   WIFI_LOCK();
   asscache.req = NULL;
   asscache.cfm = NULL;
   WIFI_UNLOCK();
}

void wei_asscache_free(void)
{
   WIFI_LOCK();
   if (asscache.req)
   {
      wei_free_assoc_req(asscache.req);
      asscache.req = 0;
   }
   if (asscache.cfm)
   {
      wei_free_assoc_cfm(asscache.cfm);
      asscache.cfm = 0;
   }
   WIFI_UNLOCK();
}

/*!
 * Cache a association request struct. This will discard the previous
 * contents of the request cache. The request is copied.
 */
void wei_asscache_add_req(m80211_mlme_associate_req_t *req)
{
   WIFI_LOCK();
   if (asscache.req)
   {
      wei_free_assoc_req(asscache.req);
   }
   asscache.req = wei_copy_assoc_req(req);
   if (asscache.req == NULL)
   {
      DE_TRACE_STATIC(TR_SEVERE, "Failed to copy association request \n");
   }
   WIFI_UNLOCK();
}

/*!
 * Get a pointer to the cached association request.
 * 
 * @return Pointer to the cache buffer. NULL if the cache was empty.
 */
m80211_mlme_associate_req_t *wei_asscache_get_req(void)
{
   if (asscache.req)
   {
      return asscache.req;
   }

   return NULL;
}

/*!
 * Cache the IEs from bss_desc. This will discard the previous contents of
 * the response cache.
 */
void wei_asscache_add_cfm(m80211_mlme_associate_cfm_t *cfm)
{
   WIFI_LOCK();
   if (asscache.cfm)
   {
      wei_free_assoc_cfm(asscache.cfm);
   }
   asscache.cfm = wei_copy_assoc_cfm(cfm);
   if (asscache.cfm == NULL)
   {
      DE_TRACE_STATIC(TR_SEVERE, "Failed to copy association confim \n");
   }
   WIFI_UNLOCK();
}

/*!
 * Get a pointer to the cached association response.
 * 
 * @return Pointer to the cache buffer. NULL if the cache was empty.
 */
m80211_mlme_associate_cfm_t *wei_asscache_get_cfm(void)
{
   if (asscache.cfm)
   {
      return asscache.cfm;
   }

   return NULL;
}

/** @} */ /* End of wei_asscache group */
