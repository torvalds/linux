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
This module implements the WiFiEngine 802.11 multicast filter

*****************************************************************************/
/** @defgroup we_mcast WiFiEngine multicast filter interface
 *
 * @brief WiFiEngine interface to firmware multicast filter
 *
 *  @{
 */
#include "driverenv.h"
#include "wifi_engine_internal.h"

#define TARGET_FILTER_SIZE 16
struct mcast_range {
   m80211_mac_addr_t address;
   WEI_TQ_ENTRY(mcast_range) next;
};

static WEI_TQ_HEAD(, mcast_range) multicast_list;

static unsigned int multicast_flags;

#undef FLAGS_SET
#define FLAGS_SET(F) ((multicast_flags & (F)) == (F))

/* init multicast subsystem */
void
wei_multicast_init(void)
{
   WEI_TQ_INIT(&multicast_list);
   multicast_flags = 0;
}

/* add an address to multicast list */
int
wei_multicast_add(m80211_mac_addr_t addr)
{
   struct mcast_range *m;

   WEI_TQ_FOREACH(m, &multicast_list, next) {
      if(DE_MEMCMP(addr.octet, m->address.octet, sizeof(addr.octet)) == 0)
	 return WIFI_ENGINE_SUCCESS;
   }
   m = (struct mcast_range *)DriverEnvironment_Nonpaged_Malloc(sizeof(*m));
   if(m == NULL)
   {
      DE_TRACE_INT(TR_SEVERE, "unable to allocate non-paged memory of size " TR_FSIZE_T "\n", 
                   TR_ASIZE_T(sizeof(*m)));
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   m->address = addr;
   WEI_TQ_INSERT_TAIL(&multicast_list, m, next);
   return WIFI_ENGINE_SUCCESS;
}

/* remove an address from multicast list */
int
wei_multicast_remove(m80211_mac_addr_t addr)
{
   struct mcast_range *m;

   WEI_TQ_FOREACH(m, &multicast_list, next) {
      if(DE_MEMCMP(addr.octet, m->address.octet, sizeof(addr.octet)) == 0) {
	 WEI_TQ_REMOVE(&multicast_list, m, next);
         DriverEnvironment_Nonpaged_Free(m);
	 return WIFI_ENGINE_SUCCESS;
      }
   }
   return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
}

/* remove all addresses from list */
int
wei_multicast_clear(void)
{
   struct mcast_range *m;

   while((m = WEI_TQ_FIRST(&multicast_list)) != NULL) {
      WEI_TQ_REMOVE(&multicast_list, m, next);
      DriverEnvironment_Nonpaged_Free(m);
   }
   return WIFI_ENGINE_SUCCESS;
}

/* replace list of multicast addresses with given array */
int
wei_multicast_set(const m80211_mac_addr_t *addr, unsigned int count)
{
   int status;
   unsigned int i;

   status = wei_multicast_clear();
   if(status != WIFI_ENGINE_SUCCESS)
      return status;
   
   for(i = 0; i < count; i++) {
      status = wei_multicast_add(addr[i]);
      if(status != WIFI_ENGINE_SUCCESS)
	 return status;
   }
   return WIFI_ENGINE_SUCCESS;
}

/* retrieve current multicast address list */
int
wei_multicast_get(m80211_mac_addr_t *addr, unsigned int *count)
{
   unsigned int i;
   struct mcast_range *m;

   i = 0;
   WEI_TQ_FOREACH(m, &multicast_list, next) {
      if(addr != NULL && i < *count)
	 addr[i] = m->address;
      i++;
   }
   if(i <= *count) {
      *count = i;
      return WIFI_ENGINE_SUCCESS;
   }
   *count = i;
   return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
}

/* sets multicast filter flags */
void
wei_multicast_set_flags(unsigned int flags)
{
   multicast_flags |= flags;
}

/* clears multicast filter flags */
void
wei_multicast_clear_flags(unsigned int flags)
{
   multicast_flags &= ~flags;
}

/* retrieve current multicast filter mode */
unsigned int
wei_multicast_get_flags(void)
{
   return multicast_flags;
}

/* update the firmware with current multicast list */
int
wei_multicast_update(void)
{
   int status;
   uint8_t macaddrs[TARGET_FILTER_SIZE * M80211_ADDRESS_SIZE];
   int num_addrs = 0;
   struct mcast_range *m;
   unsigned char discard;

   discard = FLAGS_SET(WE_MCAST_FLAG_EXCLUDE);
   if(FLAGS_SET(WE_MCAST_FLAG_ALLMULTI)) {
      /* zero length discard list */
      discard = 1;
   } else {
      if(!FLAGS_SET(WE_MCAST_FLAG_EXCLUDE)) {
	 /* implicitly include the broadcast address, this matches the
	    behaviour of most OS:es */
	 DE_MEMSET(macaddrs + M80211_ADDRESS_SIZE * num_addrs,
		   0xff,
		   M80211_ADDRESS_SIZE);
	 num_addrs++;
      }
      WEI_TQ_FOREACH(m, &multicast_list, next) {
	 if(num_addrs == TARGET_FILTER_SIZE) {
	    /* too many addresses */
	    if(!FLAGS_SET(WE_MCAST_FLAG_EXCLUDE)) {
	       /* if we are using a positive match list, we have to
		  switch to receiving all multicast */
	       num_addrs = 0;
	    }
	    /* but if it's a negative list (filter out addresses), we can
	       just go ahead and filter out as many addresses as
	       possible */
	    break;
	 }
	 DE_MEMCPY(macaddrs + M80211_ADDRESS_SIZE * num_addrs,
		   m->address.octet,
		   M80211_ADDRESS_SIZE);
	 num_addrs++;
      }
   }
   status = WiFiEngine_SendMIBSet(MIB_dot11trafficFilterDiscard, 
                                  NULL,
                                  (char*)&discard,
                                  sizeof(discard));
   if(status != WIFI_ENGINE_SUCCESS)
      return status;
   return WiFiEngine_SendMIBSet(MIB_dot11trafficFilterList, 
				NULL,
				(char*)macaddrs,
				num_addrs * M80211_ADDRESS_SIZE);
}

/*!
 * @brief Add an address to the current multicast filter list.
 *
 * @param [in] addr  the address to filter for
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @retval WIFI_ENGINE_FAILURE_RESOURCES there was not enough resources to complete the operation
 * @return This function can also return anything WiFiEngine_SendMIBSet() returns.
 *
 * @note There is a limit of (currently) 16 multicast addresses in the
 * firmware, if this number is exceeded, no filtering will be
 * performed.
 */
int
WiFiEngine_MulticastAdd(m80211_mac_addr_t addr)
{
   int status;
   status = wei_multicast_add(addr);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   if(FLAGS_SET(WE_MCAST_FLAG_HOLD))
      return WIFI_ENGINE_SUCCESS;
   return wei_multicast_update();
}

/*!
 * @brief Remove an address from the current multicast filter list.
 *
 * @param [in] addr  the address to remove from the list
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @retval WIFI_ENGINE_FAILURE_NOT_ACCEPTED the address was not on the list
 * @return This function can also return anything WiFiEngine_SendMIBSet returns.
 */
int
WiFiEngine_MulticastRemove(m80211_mac_addr_t addr)
{
   int status;
   status = wei_multicast_remove(addr);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   if(FLAGS_SET(WE_MCAST_FLAG_HOLD))
      return WIFI_ENGINE_SUCCESS;
   return wei_multicast_update();
}

/*!
 * @brief Clears the multicast filter list.
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @retval WIFI_ENGINE_FAILURE_NOT_ACCEPTED the address was not on the list
 * @return This function can also return anything WiFiEngine_SendMIBSet returns.
 */
int
WiFiEngine_MulticastClear(void)
{
   int status;
   status = wei_multicast_clear();
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   if(FLAGS_SET(WE_MCAST_FLAG_HOLD))
      return WIFI_ENGINE_SUCCESS;
   return wei_multicast_update();
}


/*!
 * @brief Sets flags affecting multicast filter operation.
 *
 * This can be used if multiple multicast list changes will be
 * performed at once.
 *
 * @param [in] flags set of flags to set, current flags are
 *   @arg WE_MCAST_FLAG_EXCLUDE   addresses in multicast list is filtered out
 *   @arg WE_MCAST_FLAG_ALLMULTI  receive all multicast addresses
 *   @arg WE_MCAST_FLAG_HOLD      hold firmware updates
 *
 * @retval WIFI_ENGINE_SUCCESS this function always succeeds
 */
int
WiFiEngine_MulticastSetFlags(unsigned int flags)
{
   wei_multicast_set_flags(flags);
   if(FLAGS_SET(WE_MCAST_FLAG_HOLD))
      return WIFI_ENGINE_SUCCESS;
   return wei_multicast_update();
}

/*!
 * @brief Clears flags affecting multicast filter operation.
 *
 * This can be used if multiple multicast list changes will be
 * performed at once.
 *
 * @param [in] flags set of flags to clear, see
 * WiFiEngine_MulticastSetFlags() for list.
 *
 * @retval WIFI_ENGINE_SUCCESS this function always succeeds
 */
int
WiFiEngine_MulticastClearFlags(unsigned int flags)
{
   wei_multicast_clear_flags(flags);
   if(FLAGS_SET(WE_MCAST_FLAG_HOLD))
      return WIFI_ENGINE_SUCCESS;
   return wei_multicast_update();
}

/*!
 * @brief Set the adapter multicast filter list
 * 
 * @param filter An array of multicast addresses
 * @param count Number of addresses in filter
 * @param discard Indicates how the list should interpreted
 *                zero: forward only addresses in filter
 *                non-zero: forward addresses not in filter
 *
 * @retval WIFI_ENGINE_SUCCESS on success.
 * @retval WIFI_ENGINE_FAILURE_INVALID_LENGTH if count is too big
 * (currently 16).
 *
 * @note Remember to include the broadcast address in the list, if
 * that is what you want.
 */
int
WiFiEngine_SetMulticastFilter(m80211_mac_addr_t *filter, 
                              size_t count, 
                              int discard)
{
   int status;

   WIFI_LOCK();
   if(discard)
      wei_multicast_set_flags(WE_MCAST_FLAG_EXCLUDE);
   else
      wei_multicast_clear_flags(WE_MCAST_FLAG_EXCLUDE);
   status = wei_multicast_set(filter, count);
   if(status != WIFI_ENGINE_SUCCESS) {
      WIFI_UNLOCK();
      return status;
   }
   status = wei_multicast_update();
   WIFI_UNLOCK();
   return status;
}

/** @} */ /* End of we_mcast group */

