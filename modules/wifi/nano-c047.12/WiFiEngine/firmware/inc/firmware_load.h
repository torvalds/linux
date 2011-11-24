/* $Id: firmware_load.h,v 1.1 2007/09/09 13:29:30 peek Exp $ */

#ifndef _FIRMWARE_LOAD_H_
#define _FIRMWARE_LOAD_H_

#include <driverenv.h>
#include <wifi_engine.h>

// Will send firmware to target
int firmware_load(unsigned int index);

struct nr_fw_dlm
{
   const char*   dlm_name;
   unsigned int  dlm_id;
   const void*   dlm_data;
   unsigned int  dlm_size;
};

struct nr_fw_image
{
   const char *            fw_name;
   const char *            fw_version;
   const void *            fw_data;
   unsigned int            fw_size;
   const struct nr_fw_dlm *dlms;
   unsigned int            dlm_count;
};

// load firmware into memory, and init DLM.
const char* nr_load_firmware(fw_type fw);

const char* nr_get_fw_patch_version(void);

#endif /* _FIRMWARE_LOAD_H_ */
