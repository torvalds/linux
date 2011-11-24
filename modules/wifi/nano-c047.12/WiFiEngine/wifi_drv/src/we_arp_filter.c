/* $Id: we_arp_filter.c 18958 2011-05-03 09:14:18Z johe $ */
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
This module implements the WiFiEngine ARP filter

*****************************************************************************/
/** @defgroup we_arp_filter WiFiEngine ARP filter interface
 *
 * @brief WiFiEngine interface to firmware ARP filter
 *
 * Normally a link layer network driver has no idea about what upper
 * layer addresses are assigned to it, and doesn't care about them.
 * However, for power save issues it might be a good idea to let
 * hardware filter out as much traffic as possible, that we're not
 * interested in, this includes ARP requests for other hosts.
 *
 * By enabling this interface and configuring a set if IPv4 addresses,
 * the firmware will filter out any ARP request that is not bound for
 * us. The quantitative value of this is unclear.
 *
 *  @{
 */
#include "wifi_engine_internal.h"

#define TR_ARP_FILTER TR_MIB /* XXX */

struct arp_range {
   ip_addr_t arp_address;
   unsigned int arp_mode;
   WEI_TQ_ENTRY(arp_range) arp_next;
};

static WEI_TQ_HEAD(, arp_range) arp_list;

static int arp_filter_enabled;

/* init ARP subsystem */
void
wei_arp_filter_init(void)
{
   arp_filter_enabled = FALSE;
   WEI_TQ_INIT(&arp_list);
}

/* shutdown ARP subsystem */
void
wei_arp_filter_shutdown(void)
{
   wei_arp_filter_clear();
}

/* shutdown ARP subsystem */
void
wei_arp_filter_unplug(void)
{
   wei_arp_filter_clear();
}

/* configure ARP subsystem */
void
wei_arp_filter_configure(void)
{
   wei_arp_filter_update();
}

/* add an address to arp filter list */
int
wei_arp_filter_add(ip_addr_t addr, unsigned int mode)
{
   struct arp_range *m;

   WEI_TQ_FOREACH(m, &arp_list, arp_next) {
      if(addr == m->arp_address) {
         m->arp_mode = mode;
	 return WIFI_ENGINE_SUCCESS;
      }
   }
   m = (struct arp_range *)DriverEnvironment_Malloc(sizeof(*m));
   if(m == NULL)
      return WIFI_ENGINE_FAILURE_RESOURCES;
   m->arp_address = addr;
   m->arp_mode = mode;
   WEI_TQ_INSERT_TAIL(&arp_list, m, arp_next);
   return WIFI_ENGINE_SUCCESS;
}

/* remove an address from ARP filter list */
int
wei_arp_filter_remove(ip_addr_t addr)
{
   struct arp_range *m;

   WEI_TQ_FOREACH(m, &arp_list, arp_next) {
      if(m->arp_address == addr) {
	 WEI_TQ_REMOVE(&arp_list, m, arp_next);
	 DriverEnvironment_Free(m);
	 return WIFI_ENGINE_SUCCESS;
      }
   }
   return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
}

/* remove all addresses from list */
int
wei_arp_filter_clear(void)
{
   struct arp_range *m;

   while((m = WEI_TQ_FIRST(&arp_list)) != NULL) {
      WEI_TQ_REMOVE(&arp_list, m, arp_next);
      DriverEnvironment_Free(m);
   }
   return WIFI_ENGINE_SUCCESS;
}

struct fw_arp_filter {
   uint8_t arp_policy;
   uint8_t reserved[3];
   ip_addr_t addresses[1];
};


static int wei_arp_filter_forward_all(int trace_message)
{
   struct fw_arp_filter filter;

   if(trace_message)
      DE_TRACE_STATIC(TR_ARP_FILTER, "reverting to forward-all mode\n");

   DE_MEMSET(&filter, 0, sizeof(filter));
   filter.arp_policy = ARP_HANDLE_NONE_FORWARD_ALL;

   return WiFiEngine_SendMIBSet(MIB_dot11ARPFilterMode,
                                NULL,
                                (char*)&filter,
                                sizeof(filter) - sizeof(filter.addresses));
}

static int
arp_filter_cb(we_cb_container_t *cbc)
{
   DE_ASSERT(cbc != NULL);
   if(cbc == NULL)
      return 1;

   if(cbc->status < 0) {
      DE_TRACE_INT(TR_ARP_FILTER, "CBC returned with exit status %d\n", 
                   cbc->status);
   } else if(cbc->status == MIB_RESULT_OK) {
      /* ok */
   } else if(cbc->status == MIB_RESULT_SIZE_ERROR) {
      DE_TRACE_STATIC(TR_ARP_FILTER, "too many addresses configured\n");
      wei_arp_filter_forward_all(TRUE);
   } else if(cbc->status > 0) {
      DE_TRACE_INT(TR_ARP_FILTER, "MIB set returned with exit status %d\n", 
                   cbc->status);
      wei_arp_filter_forward_all(TRUE);
   }
   return 1;
}

/* update the firmware with current ARP filter list */
int
wei_arp_filter_update(void)
{
   int status;
   uint32_t arp_policy = 0;
   struct fw_arp_filter *filter;
   int num_addrs = 0;
   struct arp_range *m;
   size_t mib_size;
   we_cb_container_t *cbc;

   if(!WES_TEST_FLAG(WES_FLAG_HW_PRESENT))
      return WIFI_ENGINE_SUCCESS;

   if(!arp_filter_enabled) {
      return wei_arp_filter_forward_all(FALSE);
   }

   cbc = WiFiEngine_BuildCBC(arp_filter_cb, NULL, 0, 0);
   if(cbc == NULL) {
      DE_TRACE_STATIC(TR_ARP_FILTER, "failed to allocate memory for CBC\n");
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }

   num_addrs = 0;
   WEI_TQ_FOREACH(m, &arp_list, arp_next) {
      num_addrs++;
   }
   
   if(num_addrs == 0) {
      mib_size = sizeof(arp_policy);
      filter = (struct fw_arp_filter*)&arp_policy;
   } else {
      mib_size = sizeof(*filter) + (num_addrs - 1) * sizeof(ip_addr_t);
      filter = (struct fw_arp_filter *)DriverEnvironment_Malloc(mib_size);
   }
   
   if(filter == NULL) {
      DE_TRACE_STATIC(TR_ARP_FILTER, 
                      "failed to allocate memory for filter MIB\n");
      WiFiEngine_FreeCBC(cbc);
      return wei_arp_filter_forward_all(TRUE);
   }
   DE_MEMSET(filter, 0, mib_size);
   filter->arp_policy = ARP_HANDLE_NONE_FORWARD_MYIP;
   num_addrs = 0;
   WEI_TQ_FOREACH(m, &arp_list, arp_next) {
      filter->addresses[num_addrs] = m->arp_address;
      num_addrs++;
   }
   
   status = WiFiEngine_SetMIBAsynch(MIB_dot11ARPFilterMode,
                                    NULL,
                                    (char*)filter,
                                    mib_size,
                                    cbc);

   if(status != WIFI_ENGINE_SUCCESS) {
      DE_TRACE_INT(TR_ARP_FILTER, 
                   "WiFiEngine_SetMIBAsynch returned %d\n", status);
      WiFiEngine_FreeCBC(cbc);
   }
   if(filter != (void*)&arp_policy) {
      DriverEnvironment_Free(filter);
   }

   return status;
}

static int guess_autoconf(ip_addr_t addr)
{
   unsigned char *p = (unsigned char*)&addr;
   return p[0] == 169 && p[1] == 254;  /* 169.254/16 */
}

/*!
 * @internal
 * @brief Determine is any dynamic address has been configured.
 *
 * This function returns true if there is at least one dynamically
 * configured IPv4 address in the ARP filter cache. 
 *
 * A dynamically configured address is defines as either an address
 * that has been marked as dynamic, or an address with unknown status,
 * but which is not a determined to be a stateless address.
 *
 * @bug This function is flawed. The idea is to disable power save
 * just after connection, to minimise the risk of missing broadcast
 * frames, which are often used by protocols used early on, such as
 * DHCP. The problem is that there is only a very loose connection
 * between "having a dynamically configured IPv4 address", and "it's a
 * good idea to stay awake for now". The decision to enable power save
 * should be left to something with a more informed position, such as
 * an OS level connection manager. Also the extra risk we have of
 * missing broadcast frames while in power save is likely very small.
 *
 * @retval TRUE
 * @retval FALSE
 */
int
wei_arp_filter_have_dynamic_address(void)
{
   struct arp_range *m;

   WEI_TQ_FOREACH(m, &arp_list, arp_next) {
      if(m->arp_mode == ARP_FILTER_DYNAMIC)
         return TRUE;
      if(m->arp_mode == ARP_FILTER_UNKNOWN && 
         !guess_autoconf(m->arp_address))
         return TRUE;
   }
   return FALSE;
}


/*!
 * @brief Add an IP address to the ARP filter list.
 *
 * @param [in] addr  the IP address to filter for
 * @param [in] type  the type of address
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @retval WIFI_ENGINE_FAILURE_RESOURCES there was not enough
 *         resources to complete the operation
 * @return This function can also return anything
 *         WiFiEngine_SetMIBAsynch() returns.
 */
int
WiFiEngine_ARPFilterAdd(ip_addr_t addr, unsigned int type)
{
   int status;
   status = wei_arp_filter_add(addr, type);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   return wei_arp_filter_update();
}

/*!
 * @brief Remove an IP address from the current ARP filter list.
 *
 * @param [in] addr  the IP address to remove from the list
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @retval WIFI_ENGINE_FAILURE_NOT_ACCEPTED the address was not on the
 *         list
 * @return This function can also return anything
 *         WiFiEngine_SendMIBSet returns.
 */
int
WiFiEngine_ARPFilterRemove(ip_addr_t addr)
{
   int status;
   status = wei_arp_filter_remove(addr);
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   return wei_arp_filter_update();
}

/*!
 * @brief Clears the ARP filter.
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @retval WIFI_ENGINE_FAILURE_NOT_ACCEPTED the address was not on the
 *         list
 * @return This function can also return anything
 *         WiFiEngine_SendMIBSet returns.
 */
int
WiFiEngine_ARPFilterClear(void)
{
   int status;
   status = wei_arp_filter_clear();
   if(status != WIFI_ENGINE_SUCCESS)
      return status;

   return wei_arp_filter_update();
}

/*!
 * @brief Enable ARP request filtering in firmware.
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @return This function can also return anything
 *         WiFiEngine_SendMIBSet returns.
 */
int
WiFiEngine_ARPFilterEnable(void)
{
   arp_filter_enabled = TRUE;
   return wei_arp_filter_update();
}

/*!
 * @brief Disable ARP request filtering in firmware.
 *
 * @retval WIFI_ENGINE_SUCCESS if the operation was successful
 * @return This function can also return anything
 *         WiFiEngine_SendMIBSet returns.
 */
int
WiFiEngine_ARPFilterDisable(void)
{
   arp_filter_enabled = FALSE;
   return wei_arp_filter_update();
}


/*------------------------------------------------------------*/
/*!
 * @brief Configure ARP filtering in FW
 *
 * To avoid that ARP requests wake up host during power save, it is
 * possible to let fw handle these requests to different degrees.
 *
 * @param mode What type of filter to use.
 * @param ip My ip address.
 *
 * This function imlements an older API, that only allowed a single IP
 * address to be configured.
 *
 * \a mode must be either ARP_HANDLE_NONE_FORWARD_ALL or
 * ARP_HANDLE_NONE_FORWARD_MYIP.
 *
 * \see WiFiEngine_ARPFilterAdd
 *
 * @retval WIFI_ENGINE_SUCCESS on success
 * @retval return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
 * @return This function can also return anything
 *         WiFiEngine_SendMIBSet returns.
 */
int WiFiEngine_ConfigARPFilter(arp_policy_t mode, 
                               ip_addr_t ip)
{
   int status;
   
   if(mode == ARP_HANDLE_NONE_FORWARD_ALL) {
      return WiFiEngine_ARPFilterDisable();
   }
   if(mode == ARP_HANDLE_NONE_FORWARD_MYIP) {
         status = WiFiEngine_ARPFilterClear();
         if(status != WIFI_ENGINE_SUCCESS)
            return status;
         status = WiFiEngine_ARPFilterAdd(ip, ARP_FILTER_UNKNOWN);
         if(status != WIFI_ENGINE_SUCCESS)
            return status;
         return WiFiEngine_ARPFilterEnable();
   }
   /* The two modes not handled here (replies handled by firmware) are
    * basically flawed, since if someone ARP:s for us, there is a good
    * chance they will send us a frame just after the ARP response, in
    * which case we will have to wake up anyway.
    * 
    * Leave these as unsupported until a really good use case can be
    * provided. */
   return WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
}
/*------------------------------------------------------------*/

/** @} */ /* End of we_arp_filter group */

