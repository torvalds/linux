/* Copyright 2010 by Nanoradio AB */
/* $Id: mibtable.c 18654 2011-04-11 13:46:16Z joda $ */

#include "wifi_engine_internal.h"
#include "mib_idefs.h"

struct mib_table {
   size_t size;           /* size in bytes of table */
   uint32_t vaddr;        /* target address of first entry */
   uint32_t rootindex;    /* index of the main table */
   uint32_t rootentries;  /* number of entries of main table */
   mib_object_entry_t table[1];
};

static struct mib_table *mibtable;

#define MIBENTRYSIZE 8
#define ADDRVALID(A, T) ((A) >= (T)->vaddr                    \
                         && (A) - (T)->vaddr + MIBENTRYSIZE < (T)->size  \
                         && (((A) - (T)->vaddr) & (MIBENTRYSIZE - 1)) == 0)
#define ADDRTOINDEX(A, T) (((A) - (T)->vaddr) / MIBENTRYSIZE)

#define GETFIELD(N, F) (((N) & MIB_##F##_MASK) >> MIB_##F##_OFFSET)
#define GETADDR(N) ((N) & 0x3fffff)

int wei_have_mibtable(void)
{
   return mibtable != NULL;
}


#if DE_MIB_TABLE_SUPPORT == CFG_ON
int
wei_get_mib_object(const mib_id_t *mib_id, 
                   mib_object_entry_t *entry)
{
   mib_object_reference_type_t type;
   mib_object_size_description_t sdesc;
   uint32_t addr;
   uint32_t size;
   unsigned int mib_id_index = 0;
   unsigned int component;
   unsigned int final_component;
   unsigned int index;
   unsigned int nentries;
   mib_object_entry_t *oe;

   if(mibtable == NULL || mib_id == NULL || entry == NULL)
   {
      return WIFI_ENGINE_FAILURE;
   }

   index = mibtable->rootindex;
   nentries = mibtable->rootentries;
   component = (unsigned int)(mib_id->octets[0]) - 1;

   while(1) {
      final_component = (mib_id_index >= MIB_IDENTIFIER_MAX_LENGTH - 1)
         || (mib_id->octets[mib_id_index + 1] == 0);

      if(component >= nentries) {
         DE_TRACE_STATIC(TR_MIB, "component too large\n");
         return WIFI_ENGINE_FAILURE;
      }
      index += component;
      if(index >= mibtable->size / MIBENTRYSIZE) {
         DE_TRACE_STATIC(TR_MIB, "bad address\n");
         return WIFI_ENGINE_FAILURE;
      }
      oe = &mibtable->table[index];
      type = (mib_object_reference_type_t)GETFIELD(oe->storage_description, REFERENCE_TYPE);
      sdesc = (mib_object_size_description_t)GETFIELD(oe->storage_description, OBJECT_SIZE_DESCRIPTION);
      addr = GETADDR(oe->reference);
      size = GETFIELD(oe->storage_description, OBJECT_SIZE);

      if(type == MIB_OBJECT_REFERENCE_TYPE_MIB_TABLE) {
         if(final_component) {
            DE_TRACE_STATIC(TR_MIB, "non-leaf\n");
            return WIFI_ENGINE_FAILURE;
         }
         if(sdesc != MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE) {
            DE_TRACE_STATIC(TR_MIB, "unexpected size description type\n");
            return WIFI_ENGINE_FAILURE;
         }
         if(!ADDRVALID(addr, mibtable)) {
            DE_TRACE_STATIC(TR_MIB, "bad address\n");
            return WIFI_ENGINE_FAILURE;
         }
         index = ADDRTOINDEX(addr, mibtable);
         nentries = size;
         mib_id_index++;
         component = (unsigned int)(mib_id->octets[mib_id_index]) - 1;
         continue;
      } else if(type == MIB_OBJECT_REFERENCE_TYPE_MIB_SUBTABLE) {
         if(sdesc != MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE) {
            DE_TRACE_STATIC(TR_MIB, "unexpected size description type\n");
            return WIFI_ENGINE_FAILURE;
         }
         if(size != 2) {
            DE_TRACE_INT(TR_MIB, "unexpected subtable size (%u)\n", size);
            return WIFI_ENGINE_FAILURE;
         }
         if(!ADDRVALID(addr, mibtable)) {
            DE_TRACE_STATIC(TR_MIB, "bad address\n");
            return WIFI_ENGINE_FAILURE;
         }
         index = ADDRTOINDEX(addr, mibtable);
         nentries = size;
         if(final_component) {
            component = 0;
         } else {
            component = 1;
         }
         continue;
      } else {
         if(!final_component) {
            DE_TRACE_STATIC(TR_MIB, "leaf with more components\n");
            return WIFI_ENGINE_FAILURE;
         }
         *entry = *oe;
         break;
      }
   }
   return WIFI_ENGINE_SUCCESS;
}
#endif /* DE_MIB_TABLE_SUPPORT */

void wei_free_mibtable(void)
{
   struct mib_table *tmp = mibtable;
   mibtable = NULL;
   if(tmp != NULL)
      DriverEnvironment_Free(tmp);
}

#if DE_MIB_TABLE_SUPPORT == CFG_ON
int WiFiEngine_RegisterMIBTable(const void *table, size_t size, uint32_t vaddr)
{
   struct mib_table *tmp;
   mib_object_reference_type_t type;
   mib_object_size_description_t sdesc;
   uint32_t addr;
   uint32_t esize;

   tmp = DriverEnvironment_Malloc(sizeof(*tmp) - sizeof(tmp->table) + size);
   if(tmp == NULL) {
      DE_TRACE_STATIC(TR_ALWAYS, "failed to allocate memory for MIB table\n");
      return WIFI_ENGINE_FAILURE_RESOURCES;
   }
   DE_MEMCPY(tmp->table, table, size);
   tmp->size = size;
   tmp->vaddr = vaddr;

   type = (mib_object_reference_type_t)GETFIELD(tmp->table[0].storage_description, REFERENCE_TYPE);
   sdesc = (mib_object_size_description_t)GETFIELD(tmp->table[0].storage_description, OBJECT_SIZE_DESCRIPTION);
   addr = GETADDR(tmp->table[0].reference);
   esize = GETFIELD(tmp->table[0].storage_description, OBJECT_SIZE);
   if(type != MIB_OBJECT_REFERENCE_TYPE_MIB_TABLE
      || sdesc != MIB_OBJECT_SIZE_DESCRIPTION_FIXED_SIZE
      || !ADDRVALID(addr, tmp)) {
      DE_TRACE_INT5(TR_ALWAYS, "bad MIB table format t:%x s:%x a:%x e:%x v:%x\n",
            type, sdesc, addr, esize, ADDRVALID(addr, tmp));
      DriverEnvironment_Free(tmp);
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }
   tmp->rootindex = ADDRTOINDEX(addr, tmp);
   tmp->rootentries = esize;

   wei_free_mibtable();

   mibtable = tmp;

   return WIFI_ENGINE_SUCCESS;
}
#endif /* DE_MIB_TABLE_SUPPORT */
