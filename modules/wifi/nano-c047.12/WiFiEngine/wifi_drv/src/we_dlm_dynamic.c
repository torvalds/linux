/* $Id: $ */
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
/** @defgroup we_dlm_dynamic dynamically stored dlm interface
 *
 * @brief WiFiEngine interface to fw dynamic loadable modules
 *
 * Used for WPA/WAPI/...
 *
 *  @{
 */
#include "driverenv.h"
#include "we_dlm.h"

#ifdef WE_DLM_DYNAMIC

typedef struct dlm_s
{
   WEI_TQ_ENTRY(dlm_s) entry;
   const char*   dlm_name;
   unsigned int  dlm_id;
   unsigned int  dlm_size;
   char          dlm_data[0];
} dlm_t;

static int dynamic_dlm_ops_get_data(
      struct dlm_ops_t *ops,
      int offset,
      int len,
      char **p,
      uint32_t *remaining);

typedef struct dynamic_dlm_ops_s
{
   struct dlm_ops_t ops;
   dlm_t *current_dlm;
   WEI_TQ_HEAD(dlm_head_s, dlm_s) head;
} dynamic_dlm_ops_t;

static dynamic_dlm_ops_t dynamic_dlm_ops;

const struct dlm_ops_t dynamic_ops = 
{
   dynamic_dlm_ops_get_data,
   NULL, /* free_data */
   (void*)&dynamic_dlm_ops
};

static int dynamic_dlm_ops_get_data(
      struct dlm_ops_t *ops,
      int offset,
      int len,
      char **p,
      uint32_t *remaining)
{
   dlm_t *dlm;

   dlm = dynamic_dlm_ops.current_dlm;

   *p = (char*)&dlm->dlm_data[offset];
   *remaining = dlm->dlm_size - offset;

   return len;
}

char *
dynamic_dlm_ops_find_mib_table(const char* dlm_name)
{
   dlm_t *dlm;

   WEI_TQ_FOREACH(dlm, &dynamic_dlm_ops.head, entry)
   {
      if(!STRCMP(dlm->dlm_name, dlm_name))
      {
         return dlm->dlm_data;
      }
   }

   return NULL;
}


struct dlm_ops_t*
dynamic_dlm_ops_find(uint32_t id, uint32_t size)
{
   dlm_t *dlm;

   WEI_TQ_FOREACH(dlm, &dynamic_dlm_ops.head, entry)
   {
      if(id == dlm->dlm_id)
      {
         dynamic_dlm_ops.current_dlm = dlm;
         return &dynamic_dlm_ops.ops;
      }
   }

   /* debug info after this */

   DE_TRACE_INT(TR_SEVERE, "requested DLM id: %x not found\n", id);
   WEI_TQ_FOREACH(dlm, &dynamic_dlm_ops.head, entry)
   {
      DE_TRACE_INT(TR_ALL, "DLM: %x\n", dlm->dlm_id);
   }

   return NULL;
}

char* we_dlm_register(
      const char*   dlm_name, 
      unsigned int  dlm_id, 
      unsigned int  dlm_size)
{
   dlm_t *dlm;
   DE_ASSERT(dlm_id != 0x40000); /* use WiFiEngine_RegisterMIBTable
                                  * instead of using a magic DLM,
                                  * remove this assert when all
                                  * drivers are updated */
   
   dlm = (dlm_t*)DriverEnvironment_Malloc(sizeof(dlm_t) + dlm_size);
   dlm->dlm_name = dlm_name;
   dlm->dlm_id = dlm_id;
   dlm->dlm_size = dlm_size;

   WEI_TQ_INSERT_TAIL(&dynamic_dlm_ops.head, dlm, entry);
   we_dlm_register_adapter(&dynamic_dlm_ops_find);

   DE_TRACE_INT2(TR_INITIALIZE, "DLM loaded: id 0x%x size %u\n", dlm_id, dlm_size);

   /* dlm_data[] will be written to */
   return &dlm->dlm_data[0];
}
#endif /* WE_DLM_DYNAMIC */

void we_dlm_flush(void)
{
#ifdef WE_DLM_DYNAMIC
   dlm_t *dlm;

   while((dlm = WEI_TQ_FIRST(&dynamic_dlm_ops.head)) != NULL)
   {
      WEI_TQ_REMOVE(&dynamic_dlm_ops.head, dlm, entry);
      DriverEnvironment_Free(dlm);
   }
#endif
}

void we_dlm_dynamic_initialize(void)
{
#ifdef WE_DLM_DYNAMIC
   dynamic_dlm_ops.ops = dynamic_ops;
   WEI_TQ_INIT(&dynamic_dlm_ops.head);
   dynamic_dlm_ops.current_dlm = NULL;
   we_dlm_register_adapter(&dynamic_dlm_ops_find);   
#endif /* WE_DLM_DYNAMIC */
}

void we_dlm_dynamic_shutdown(void)
{
   we_dlm_flush();
}

