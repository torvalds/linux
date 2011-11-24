/* $Id: firmware_load.c,v 1.4 2007/10/24 13:18:38 miwi Exp $ */
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
Contains functions for loading FW to Target

*****************************************************************************/

#include "driverenv.h"

#include "transport.h"
#include "transport_platform.h"
#include "firmware_load.h"
#include "we_dlm.h"
#include "axf_parser.h"

#include "logger_io.h"
#include "pcap.h"

static unsigned char g_TargetFWimage[DE_FIRMWARE_SIZE];
static char fw_version[128] = {0};
static size_t g_fwSize;
static char fw_path[128];

int dbg_enable_pcap = 1;

#if (DE_BUILTIN_FIRMWARE == CFG_ON)
extern const struct nr_fw_image nr_builtin_firmware_nrx511c;
extern const struct nr_fw_image nr_builtin_firmware_nrx511c_test;

static const struct nr_fw_image *const fw_images[] = {
   &nr_builtin_firmware_nrx511c,
   &nr_builtin_firmware_nrx511c_test,
};
#endif /* (DE_BUILTIN_FIRMWARE == CFG_ON) */


const char* nr_get_fw_patch_version(void)
{
   return fw_version;
}

#if (DE_BUILTIN_FIRMWARE == CFG_ON)
static void load_builtin_dlm(const struct nr_fw_image* image)
{
    int i;
    
    for (i=0; i< image->dlm_count; i++)
    {
        const struct nr_fw_dlm *dlm;
        char *p;
         
        dlm = &image->dlms[i];

        if(DE_STRCMP(dlm->dlm_name, "mib_table") == 0) 
        {
           int res = WiFiEngine_RegisterMIBTable(dlm->dlm_data, 
                                          dlm->dlm_size, 
                                          dlm->dlm_id);
           
           if(res != WIFI_ENGINE_SUCCESS)
           {
              DE_TRACE_INT(TR_INITIALIZE, "Loading mib_table failed. errorcode:%d", res);
              continue;
           }
           
           DE_TRACE_INT(TR_INITIALIZE, "Loading mib_table. size:%d", dlm->dlm_size);
           continue;
        }
        p = we_dlm_register(
              dlm->dlm_name,
              dlm->dlm_id,
              dlm->dlm_size);
        if(p)
        {
            DE_MEMCPY(p, dlm->dlm_data, dlm->dlm_size);
        }
    }
}
#endif

static const char* load_fw_file_into_mem(const char *hw, 
					 const char **paths, 
					 unsigned int npaths)
{
    unsigned int read;
    int i;
#if (DE_BUILTIN_FIRMWARE == CFG_ON)    
    int j;
#endif

    we_dlm_flush();
    wei_free_mibtable();
    DE_STRNCPY(fw_version, "Loaded from file", sizeof(fw_version));
    
    
    for(i = 0; i < npaths; i++) 
    {
       DE_SNPRINTF(fw_path, sizeof(fw_path), paths[i], hw);

       read = nr_read_firmware(fw_path, g_TargetFWimage, DE_FIRMWARE_SIZE);

       if (read > 0) 
       {
           g_fwSize = read;
           return fw_path;
       }    
    }

   /* see if we should load this from internal memory */
#if (DE_BUILTIN_FIRMWARE == CFG_ON)
    DE_TRACE_STATIC(TR_API, "Load from static memory");
    for(j = 0; j < DE_ARRAY_SIZE(fw_images); j++) 
    {
        if(DE_STRNCMP(fw_path, fw_images[j]->fw_name, ~0) == 0) 
        {
            DE_ASSERT(fw_images[j]->fw_size <= sizeof(g_TargetFWimage));
            DE_MEMCPY(g_TargetFWimage, 
      		       fw_images[j]->fw_data, 
      		       fw_images[j]->fw_size);
	         g_fwSize = fw_images[j]->fw_size;
	         
            DE_TRACE_INT(TR_INITIALIZE, "builtin. FW_size:%d\n", g_fwSize);
            load_builtin_dlm(fw_images[j]);
            
#if (DE_CHIP_VERSION == NRX_600)
            // Save the version number, since the version  is not
            // in the chip.
            DE_STRNCPY(fw_version, fw_images[j]->fw_version, sizeof(fw_version));
            fw_version[sizeof(fw_version)-1] = '\0';
#endif            
            return fw_path;
        }
    }
#endif /* (DE_BUILTIN_FIRMWARE == CFG_ON) */
    return NULL;
}

#define P(X) X

static const char* nr_load_firmware_regular(void)
{
   const char *hw = nr_rf_get_name();
   const char *paths[] = {
      P("nrx%s.bin"),
      P("%s.bin"),
      P("nrx%s.axf"),
      P("%s.axf"),
      P("x_mac.bin"),
      P("x_mac.axf"),
      "builtin-nrx%s",
   };
    
   return load_fw_file_into_mem(hw, paths, DE_ARRAY_SIZE(paths));
}

static const char* nr_load_firmware_test(void)
{
   const char *hw = nr_rf_get_name();
   const char *paths[] = {
      P("nrx%s_test.bin"),
      P("%s_test.bin"),
      P("nrx%s_test.axf"),
      P("%s_test.axf"),
      P("x_test.bin"),
      P("x_test.axf"),
      "builtin-nrx%s-test",
   };
    
   return load_fw_file_into_mem(hw, paths, DE_ARRAY_SIZE(paths));
}

#undef P

const char* nr_load_firmware(fw_type fw)
{
   if(fw == FW_TYPE_X_MAC)
      return nr_load_firmware_regular();
   else
      return nr_load_firmware_test();
}

/* index is not used in the current version */
int firmware_load(unsigned int index) 
{
    unsigned int    left2send;
    unsigned int    idx = 0;
    unsigned char*  ptrs[2] = {0,0};
    unsigned int    sizes[2] = {0,0};
    unsigned char*  startPtr;
    unsigned int    saved_g_IsReady = g_IsReady;

    g_IsReady = TRUE;

    startPtr = g_TargetFWimage;
    left2send = g_fwSize;

    // Need to fiddle with this one to make sure Transport_SendBuffers will work.
    g_IsReady = TRUE;

    while (left2send) 
    {
        //Sending the FW in 1500byte chunks
        unsigned int ssize = left2send < 1500 ? left2send : 1500;
        ptrs[0] = startPtr + idx;
        sizes[0] = ssize;

        if (Transport_SendBuffers(ptrs, sizes) == FALSE) break;

        idx += ssize;
        left2send -= ssize;
    }

    if (left2send) 
    {
        DE_TRACE_STATIC(TR_TRANSPORT, "Failed due to error in Transport_SendBuffers");
        g_IsReady = FALSE;
        return FALSE;
    }

    g_IsReady = saved_g_IsReady;

    if(dbg_enable_pcap) 
    { 
       nrx_pcap_open(NR_PATH_PCAP_LOG); 
    }

    return TRUE;
}
