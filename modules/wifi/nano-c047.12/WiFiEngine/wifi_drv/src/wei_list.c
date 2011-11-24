/* $Id: wei_list.c,v 1.9 2007-10-01 11:11:10 ulla Exp $ */
/** @defgroup wei_list WiFiEngine internal list implementation.
 *
 * \brief This module provides a simple, static size, linked list implementation
 *
 *
 *  @{
 */

#include "sysdef.h"
#include "wei_list.h"
#include "wifi_engine_internal.h"

void __wei_list_insert(wei_list_head_t *old_e, wei_list_head_t *new_e)
{
   wei_list_head_t *tmp;

   if (old_e == new_e)
   {
      DE_BUG_ON(1, "Recursion in __wei_list_insert()\n");
      return;
   }
   if (new_e->next || new_e->prev)
   {
      DE_BUG_ON(1, "Bad new list entry (non-null pointers). Infinite loop possible\n");
   }
   WIFI_LOCK();
   tmp = old_e->next;
   old_e->next = new_e;
   new_e->prev = old_e;
   new_e->next = tmp;
   if (tmp)
   {
      tmp->prev = new_e;
   }
   WIFI_UNLOCK();
}

void __wei_list_remove(wei_list_head_t *el)
{
   wei_list_head_t *prev;
   
   DE_ASSERT(el != NULL);
   WIFI_LOCK();
   prev = el->prev;
   if (prev)
      prev->next = el->next;
   if (el->next)
      el->next->prev = prev;
   el->next = el->prev = NULL;
   WIFI_UNLOCK();
}

/** @} */ /* End of wei_list group */
