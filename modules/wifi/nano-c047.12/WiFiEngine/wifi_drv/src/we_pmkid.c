/* $Id: we_pmkid.c 17400 2010-12-15 15:12:18Z joda $ */
/*****************************************************************************

Copyright (c) 2009 by Nanoradio AB

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

/* This module implements PMKID caching and retrieval. */

#include "wifi_engine_internal.h"


#if DE_PMKID_CACHE_SUPPORT == CFG_ON

struct wei_pmkid_entry {
   WEI_TQ_ENTRY(wei_pmkid_entry) next;
   m80211_bssid_info pmkid;
};

static WEI_TQ_HEAD(, wei_pmkid_entry) wei_pmkid_list =
   WEI_TQ_HEAD_INITIALIZER(wei_pmkid_list);

/* lock for all three queues, does not lock the cbc itself */
static driver_lock_t pmkid_lock;

#define PMKID_LOCK() DriverEnvironment_acquire_lock(&pmkid_lock)
#define PMKID_UNLOCK() DriverEnvironment_release_lock(&pmkid_lock);

static int wei_equal_pmkid(const m80211_pmkid_value *pmkid1,
                           const m80211_pmkid_value *pmkid2)
{
   return DE_MEMCMP(pmkid1->octet, pmkid2->octet, sizeof(pmkid1->octet)) == 0;
}

#ifdef WIFI_DEBUG_ON
static char*
wei_print_pmkid(const m80211_pmkid_value *pmkid, char *str, size_t len)
{
   DE_SNPRINTF(str, len,
               "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
               "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
               (unsigned char)pmkid->octet[0],
               (unsigned char)pmkid->octet[1],
               (unsigned char)pmkid->octet[2],
               (unsigned char)pmkid->octet[3],
               (unsigned char)pmkid->octet[4],
               (unsigned char)pmkid->octet[5],
               (unsigned char)pmkid->octet[6],
               (unsigned char)pmkid->octet[7],
               (unsigned char)pmkid->octet[8],
               (unsigned char)pmkid->octet[9],
               (unsigned char)pmkid->octet[10],
               (unsigned char)pmkid->octet[11],
               (unsigned char)pmkid->octet[12],
               (unsigned char)pmkid->octet[13],
               (unsigned char)pmkid->octet[14],
               (unsigned char)pmkid->octet[15]);
   return str;
}
#endif /* WIFI_DEBUG_ON */

static void
wei_pmkid_dump(void)
{
#ifdef WIFI_DEBUG_ON
   struct wei_pmkid_entry *pe;
   unsigned int index = 0;
   char index_str[8];
   char bssid_str[M80211_ADDRESS_SIZE*3];
   char pmkid_str[M80211_PMKID_SIZE*3];

   if(TRACE_ENABLED(TR_DATA_DUMP)) {
      WEI_TQ_FOREACH(pe, &wei_pmkid_list, next) {
         if(TRACE_ENABLED(TR_PMKID))
            DE_SNPRINTF(index_str, sizeof(index_str), "%3u", index);
         DE_TRACE_STRING3(TR_PMKID, "PMKID[%s]: %s %s\n",
                          index_str,
                          wei_print_mac(&pe->pmkid.BSSID,
                                        bssid_str, sizeof(bssid_str)),
                          wei_print_pmkid(&pe->pmkid.PMKID,
                                          pmkid_str, sizeof(pmkid_str)));
         index++;
      }
   }
#endif
}

static void
wei_pmkid_print(const char *op,
                const m80211_mac_addr_t *bssid,
                const m80211_pmkid_value *pmkid)
{
#ifdef WIFI_DEBUG_ON
   char bssid_str[M80211_ADDRESS_SIZE*3];
   char pmkid_str[M80211_PMKID_SIZE*3];

   if(TRACE_ENABLED(TR_PMKID)) {
      if(bssid == NULL)
         DE_STRCPY(bssid_str, "*");
      else
         wei_print_mac((m80211_mac_addr_t *)bssid, bssid_str, sizeof(bssid_str));
      if(pmkid == NULL)
         DE_STRCPY(pmkid_str, "*");
      else
         wei_print_pmkid(pmkid, pmkid_str, sizeof(pmkid_str));
      DE_TRACE_STRING3(TR_PMKID, "PMKID %s %s/%s\n", op, bssid_str, pmkid_str);
   }
#endif
}

static int
wei_pmkid_add(const m80211_mac_addr_t *bssid, const m80211_pmkid_value *pmkid)
{
   struct wei_pmkid_entry *pe;

   wei_pmkid_print("ADD", bssid, pmkid);

   WEI_TQ_FOREACH(pe, &wei_pmkid_list, next) {
      if(wei_equal_bssid(*bssid, pe->pmkid.BSSID)
         && wei_equal_pmkid(pmkid, &pe->pmkid.PMKID)) {
         return WIFI_ENGINE_SUCCESS;
      }
   }
   pe = DriverEnvironment_Malloc(sizeof(*pe));
   if(pe == NULL) {
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   pe->pmkid.BSSID = *bssid;
   pe->pmkid.PMKID = *pmkid;
   WEI_TQ_INSERT_TAIL(&wei_pmkid_list, pe, next);

   wei_pmkid_dump();

   return WIFI_ENGINE_SUCCESS;
}

static int
wei_pmkid_remove(const m80211_mac_addr_t *bssid,
                 const m80211_pmkid_value *pmkid)
{
   struct wei_pmkid_entry *pe, *pn;

   wei_pmkid_print("REMOVE", bssid, pmkid);

   pe = WEI_TQ_FIRST(&wei_pmkid_list);
   while(pe != NULL) {
      pn = WEI_TQ_NEXT(pe, next);
      if((bssid == NULL || wei_equal_bssid(*bssid, pe->pmkid.BSSID))
         && (pmkid == NULL || wei_equal_pmkid(pmkid, &pe->pmkid.PMKID))) {
         WEI_TQ_REMOVE(&wei_pmkid_list, pe, next);
         DriverEnvironment_Free(pe);
      }
      pe = pn;
   }

   wei_pmkid_dump();

   return WIFI_ENGINE_SUCCESS;
}
#endif /* DE_PMKID_CACHE_SUPPORT == CFG_NO */

int wei_pmkid_init(void)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
   DriverEnvironment_initialize_lock(&pmkid_lock);
#endif
   return WIFI_ENGINE_SUCCESS;
}

int wei_pmkid_unplug(void)
{
   return WiFiEngine_PMKID_Clear();
}

/*!
 * Add one entry to PMKID cache table.
 *
 * If the tuple is already present, nothing is added.
 *
 * @param [in]  bssid  BSSID of the entry
 * @param [in]  pmkid  PMKID of the entry
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE_RESOURCES on memory shortage
 */
int
WiFiEngine_PMKID_Add(const m80211_mac_addr_t *bssid,
                     const m80211_pmkid_value *pmkid)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
   int status;

   PMKID_LOCK();
   status = wei_pmkid_add(bssid, pmkid);
   PMKID_UNLOCK();

   return status;
#else
   return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
#endif
}

/*!
 * Remove one or more entries from PMKID cache table.
 *
 * @param [in]  bssid  BSSID of the entries to remove
 * @param [in]  pmkid  PMKID of the entries to remove
 *
 * This will remove all entries matching bssid and pmkid. A NULL bssid
 * or pmkid matches any entry, so it's possible to remove all entries
 * for one BSSID by passing that BSSID, and NULL for PMKID, or remove
 * all entries by passing both as NULL.
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 */
int
WiFiEngine_PMKID_Remove(const m80211_mac_addr_t *bssid,
                        const m80211_pmkid_value *pmkid)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
   int status;

   PMKID_LOCK();
   status = wei_pmkid_remove(bssid, pmkid);
   PMKID_UNLOCK();

   return status;
#else
   return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
#endif
}

/*!
 * Clear PMKID cache table.
 *
 * Removes all entries from the PMKID cache.
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 */
int WiFiEngine_PMKID_Clear(void)
{
   return WiFiEngine_PMKID_Remove(NULL, NULL);
}

/*!
 * Retrieve PMKID cache table.
 *
 * @param [out] pmkid   array of to hold pmkid table
 * @param [in]  npmkid  number of elements in pmkid
 * @param [in]  bssid   BSSID to filter on, or NULL to include all BSSID:s
 *
 * @return number of entries required for complete table, if this is
 * larger than npmkid, the extra entries are not returned.
 */
unsigned int
WiFiEngine_PMKID_Get(m80211_bssid_info *pmkid,
                     unsigned int npmkid,
                     const m80211_mac_addr_t *bssid)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
   unsigned int n = 0;
   struct wei_pmkid_entry *pe;

   PMKID_LOCK();

   WEI_TQ_FOREACH(pe, &wei_pmkid_list, next) {
      if(bssid == NULL || wei_equal_bssid(*bssid, pe->pmkid.BSSID)) {
         if(n < npmkid)
            pmkid[n] = pe->pmkid;
         n++;
      }
   }
   PMKID_UNLOCK();

   return n;
#else
   return 0;
#endif
}

/*!
 * Set PMKID cache table.
 *
 * This function replaces the whole PMKID table.
 *
 * @param [in]  pmkid   array of to hold pmkid table
 * @param [in]  npmkid  number of elements in pmkid
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval WIFI_ENGINE_FAILURE_RESOURCES on memory shortage
 */
int
WiFiEngine_PMKID_Set(const m80211_bssid_info *pmkid,
                     unsigned int npmkid)
{
#if DE_PMKID_CACHE_SUPPORT == CFG_ON
   unsigned int i;
   int status;

   PMKID_LOCK();

   status = wei_pmkid_remove(NULL, NULL);
   if(status != WIFI_ENGINE_SUCCESS) {
      PMKID_UNLOCK();
      return status;
   }

   for(i = 0; i < npmkid; i++) {
      status = wei_pmkid_add(&pmkid->BSSID, &pmkid->PMKID);
      if(status != WIFI_ENGINE_SUCCESS)
         break;
   }
   PMKID_UNLOCK();

   return status;
#else
   return WIFI_ENGINE_FAILURE_NOT_IMPLEMENTED;
#endif
}
