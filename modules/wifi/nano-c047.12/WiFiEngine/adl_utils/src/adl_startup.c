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
Contains platform independent code for starting and stoping WIFI.

*****************************************************************************/

#include "driverenv.h"
#include "adl_utils.h"


/******************************************************************************/
/*                              STATIC VARIABLES                              */
/******************************************************************************/

static callback_fuctions* s_callbacks; //pointer to the callback-structure 
                                       //supplied by the app


/******************************************************************************/
/*                              STATIC DECLARATIONS                           */
/******************************************************************************/

static int driver_started(we_cb_container_t *cbc);

/******************************************************************************/
/*                              PUBLIC API                                    */
/******************************************************************************/

/*!
 * @brief Starts WiFi-driver and hardware.
 * 
 * The driver will start and then make a callback when it is ready for action!
 *
 * @param mode          Selects which mode to start in (test or normal)
 * @param callback_fn   callback functions
 *                      The contents of the struct will not be copied
 *                      You must keep it durring the drivers lifetime
 *
 * @return WIFI_ENGINE_SUCCESS if the procedure was successfully started.
 */
int nrwifi_startup(driver_mode mode, callback_fuctions* callback_fn)
{
   int status;
   fw_type type;
   we_cb_container_t *cbc;

   if(callback_fn == NULL)
   {
      DE_TRACE_STATIC(TR_ALWAYS, "ERROR, callback_fn must not be NULL\n");
      return WIFI_ENGINE_FAILURE_INVALID_DATA;
   }

   s_callbacks = callback_fn;

   

   switch(mode)
   {
      case WLAN_MODE_NORMAL:
         type = FW_TYPE_X_MAC;
         break;

      case WLAN_MODE_RFTEST:
         type = FW_TYPE_X_TEST;
         break;
         
         default:
         DE_TRACE_INT(TR_ALWAYS, "Error, bad mode in nrwifi_startup(mode= %u)\n",mode);
         if(s_callbacks->startup_done_cb)
            s_callbacks->startup_done_cb(WIFI_ENGINE_FAILURE_NOT_ACCEPTED,mode);
         return  WIFI_ENGINE_FAILURE_NOT_ACCEPTED;
   }


   status = WiFiEngine_Initialize(NULL);

   if(status != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_INT(TR_WARN, "WiFiEngine_Initialize() failed with code %u\n",status);
      if(s_callbacks->startup_done_cb)s_callbacks->startup_done_cb(status,mode);
      return status;
   }

   status = Transport_Initialize(s_callbacks->data_from_target_cb,
                                 s_callbacks->free_tx_data_cb,
                                 NULL);

   if(status == FALSE)
   {
      DE_TRACE_STATIC(TR_WARN, "Transport_Initialize() failed\n");
      WiFiEngine_Shutdown(0);
      if(s_callbacks->startup_done_cb)
         s_callbacks->startup_done_cb(WIFI_ENGINE_FAILURE_DATA_PATH,mode);
      return WIFI_ENGINE_FAILURE_DATA_PATH;
   }

#if DE_MIB_TABLE_SUPPORT == CFG_ON
   WiFiEngine_Firmware_LoadMIBTable(type);
#endif
   status = WiFiEngine_DownloadFirmware(type);

   if(status != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_WARN, "WiFiEngine_DownloadFirmware() failed\n");
      Transport_Shutdown();
      WiFiEngine_Shutdown(0);
      if(s_callbacks->startup_done_cb)
         s_callbacks->startup_done_cb(WIFI_ENGINE_FAILURE,mode);
      return WIFI_ENGINE_FAILURE;      
   }

   status = WiFiEngine_SetMsgSizeAlignment(  DE_MSG_MIN_SIZE,
                                             DE_PACKET_ALIGN,
                                             0,
                                             0,
                                             DE_IRQ_TYPE,
                                             HIC_CTRL_ALIGN_SWAP_NO_BYTESWAP,
                                             0xFF, //no host-wakeup
                                             0xFF);
   if(status != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_WARN, "WiFiEngine_SetMsgSizeAlignment() failed\n");
      Transport_Shutdown();
      WiFiEngine_Shutdown(0);
      if(s_callbacks->startup_done_cb)
         s_callbacks->startup_done_cb(WIFI_ENGINE_FAILURE,mode);
      return WIFI_ENGINE_FAILURE;
   }


   status = WiFiEngine_Plug();

   if(status != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_WARN, "WiFiEngine_Plug() failed\n");
      Transport_Shutdown();
      WiFiEngine_Shutdown(0);
      if(s_callbacks->startup_done_cb)
         s_callbacks->startup_done_cb(WIFI_ENGINE_FAILURE,mode);
      return WIFI_ENGINE_FAILURE;
   }

   //If the application have supplied a callback for extra initialization-code
   //This is where MAC-address will be set, mibs will be loaded.... 
   if(s_callbacks)
   {
      if(s_callbacks->before_config_cb)
         s_callbacks->before_config_cb(mode);
   }

   //Start sending the settings to target
   status = WiFiEngine_Configure_Device(mode == WLAN_MODE_RFTEST);
   if(status != WIFI_ENGINE_SUCCESS)
   {
      DE_TRACE_STATIC(TR_WARN, "WiFiEngine_Configure_Device() failed\n");
      WiFiEngine_Unplug();
      Transport_Shutdown();
      WiFiEngine_Shutdown(0);
      if(s_callbacks->startup_done_cb)
         s_callbacks->startup_done_cb(WIFI_ENGINE_FAILURE,mode);
      return WIFI_ENGINE_FAILURE;
   }

   //Dummy function... ask for MAC-address. We are not realy interested, but
   //when we receive the callback we can be sure all other settings have been
   //sent to target
   cbc = WiFiEngine_BuildCBC(driver_started, (void*) mode, 0, 0);
   WiFiEngine_GetMIBAsynch(MIB_dot11MACAddress, cbc);
      
   return WIFI_ENGINE_SUCCESS;
}

int nrwifi_shutdown(void)
{
    WiFiEngine_Unplug();
    Transport_Shutdown();
    WiFiEngine_Shutdown(0);
    return 0;
}

static int driver_started(we_cb_container_t *cbc)
{
   int status = WIFI_ENGINE_FAILURE;
   if(cbc != NULL)
   {
      if(cbc->status == 0)
         status = WIFI_ENGINE_SUCCESS;
   }
   if(s_callbacks) s_callbacks->startup_done_cb(status, (driver_mode)cbc->ctx);
   return 0;
}

