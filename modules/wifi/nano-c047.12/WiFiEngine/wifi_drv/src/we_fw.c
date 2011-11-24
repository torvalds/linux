/* $Id:  $ */
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

Module Description :
==================
This module implements functionality for handling download of fw to Target.

*****************************************************************************/
/** @defgroup we_fw WiFiEngine firmware handling
 * 
 * @brief This module implements functionality for handling download of fw to Target.
 *
 *  @{
 */
#include "driverenv.h"
#include "transport.h"
#include "wifi_engine_internal.h"
#include "we_dlm.h"


static unsigned int calc_checksum( const void *src, unsigned int len)
{
   const unsigned char *p = (const unsigned char*)src;
   unsigned int checksum = 0;
   unsigned int i;

   for(i=0; i<len; i++)
   {
      checksum += p[i];
   }
   return checksum;
}

#if DE_MIB_TABLE_SUPPORT == CFG_ON
int   WiFiEngine_Firmware_LoadMIBTable(fw_type type) 
{
   de_file_ref_t f;
   int ret = WIFI_ENGINE_SUCCESS;
   unsigned char *mib_buf = NULL;
   int total_read;
   int read;
   int mib_table_max_size = 2500;

#ifdef WE_DLM_DYNAMIC
   we_dlm_flush();
#endif

   f = de_fopen("MIBT", DE_FRDONLY);

   if(f == INVALID_FILE_HANDLE)
   {
      DE_TRACE_STATIC(TR_WARN, "Failed to open FW MIB TABLE\n");
      return WIFI_ENGINE_FAILURE;
   }

   mib_buf = (unsigned char*)DriverEnvironment_Malloc(mib_table_max_size);
   if(mib_buf == NULL)
   {
      DE_TRACE_STATIC(TR_SEVERE, "mib_table malloc\n");
      de_fclose(f);
      return WIFI_ENGINE_FAILURE;
   }

   // TODO: add protection against reading more then mib_table_max_size
   total_read = 0;
   do {
      read = de_fread(f, (char*)(mib_buf+total_read), 2*512);
      if(read > 0) total_read += read;
   } while (read>0);

   de_fclose(f);

   DE_TRACE_INT2(TR_ALWAYS, "mib_table, read %d checksum %x\n", total_read, 
         calc_checksum(mib_buf, total_read));
   if(mib_buf == NULL || total_read >= mib_table_max_size)
   {
      return WIFI_ENGINE_FAILURE;
   }

   WiFiEngine_RegisterMIBTable(mib_buf, total_read, 0x00040000);
   DriverEnvironment_Free(mib_buf);
   return WIFI_ENGINE_SUCCESS;
}
#endif /* DE_MIB_TABLE_SUPPORT */

/*! 
 * @brief Downloads FW and starts x_mac
 *
 * @param none
 *
 * @return WIFI_ENGINE_SUCCESS on success.
 */
int   WiFiEngine_DownloadFirmware(fw_type type) 
{
   de_file_ref_t f;
   size_t bytes_read = 123;
   int total_size = 0;
   int ret = WIFI_ENGINE_SUCCESS;
   int checksum = 0;

//tune this parameter to speed up FW download
#define BUFFER_SIZE (512*2)


   if (DriverEnvironment_acquire_trylock(&wifiEngineState.send_lock) == LOCK_LOCKED)
   {
      return WIFI_ENGINE_FAILURE_LOCK;
   }

   switch(type)
   {
      case FW_TYPE_X_MAC:
         f = de_fopen("FWXM", DE_FRDONLY);
         break;
      case FW_TYPE_X_TEST:
         f = de_fopen("FWXT", DE_FRDONLY);
         break;
      default:
         f = INVALID_FILE_HANDLE;
         break;
   }


   if(f == INVALID_FILE_HANDLE)
   {
      DE_TRACE_STATIC(TR_WARN, "Failed to open FW image\n");
      DriverEnvironment_release_trylock(&wifiEngineState.send_lock);
      return WIFI_ENGINE_FAILURE;
   }

   while(bytes_read)
   {
      char* sendBuffer;
      sendBuffer = DriverEnvironment_TX_Alloc(BUFFER_SIZE);
      if(sendBuffer == NULL)
      {
         DE_TRACE_INT(TR_WARN, "Failed to allocate %u bytes buffer for FW download\n",BUFFER_SIZE);
         ret = WIFI_ENGINE_FAILURE_RESOURCES;
         break;
      }

      bytes_read = de_fread(f, sendBuffer, BUFFER_SIZE);

      if(bytes_read > 0) checksum += calc_checksum(sendBuffer, bytes_read);

      if(bytes_read)
      {
         if(DriverEnvironment_HIC_Send(sendBuffer, bytes_read)
             != DRIVERENVIRONMENT_SUCCESS)
         {
            DE_TRACE_INT(TR_WARN, "Failde to send %u bytes of firmware to target\n", bytes_read);
            ret = WIFI_ENGINE_FAILURE;
            break;
         }
      }
      else
      {
         //if we do not call DriverEnvironment_HIC_Send() we must free the data ourself
         DriverEnvironment_TX_Free(sendBuffer);
      }

      total_size += bytes_read;
   }

   de_fclose(f);

   DE_TRACE_INT2(TR_ALWAYS, "fw loaded: %d checksum %x\n", total_size, checksum);

   if(total_size < 100000)
   {
      DE_TRACE_INT(TR_WARN, "FW-image to small? Only %u bytes\n", total_size);
   }

   //After FW-download target need some time to boot
   //tune this parameter to speed up FW download
   DriverEnvironment_Yield(100);


//Latest intel-report claims this is not needed...

   //During boot target usualy reset the SDIO-interface.
   //Therefore we must re-initialize it.
//   if(Transport_IntiTargetInterface() == FALSE)
//   {
//      DE_TRACE_STATIC(TR_WARN, "Failed to init target interface after FW-download\n");
//      ret = WIFI_ENGINE_FAILURE;
//   }

   DriverEnvironment_release_trylock(&wifiEngineState.send_lock);
   return ret;
}

/** @} */ /* End of we_util group */


