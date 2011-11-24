#include <stdio.h>
#include "host_flash.h"

/* open flash for reading/writing */
void * host_flash_open(char * filename,uint32_t flags)
{
   //APP_DEBUG("host_flash_write_open\n");
   FILE * handle;

   if(flags & HOST_FLASH_WRITE_FLAG)
      handle = fopen(filename,"w+");
   else if (flags & HOST_FLASH_READ_FLAG)
      handle = fopen(filename,"r");
   else return NULL;
 
   return handle;
}

/* write */
int host_flash_write(void * buf, size_t size, void * handle)
{
   int status;
   FILE * file;
   
   file = (FILE*) handle;
   //APP_DEBUG("host_flash_write\n");
   
   if (file == NULL)
      {
         printf("NULL write\n");
         exit(0);
      }


   status = fwrite(buf,1,size,file);

   if (status == size)
      return HOST_FLASH_STATUS_OK;
   else return HOST_FLASH_STATUS_ERROR;
}

/* read */
int host_flash_read(void * buf, size_t max, void * handle)
{
   FILE * file;
   file = (FILE*) handle;
   //APP_DEBUG("host_flash_read\n");
   return fread(buf,1,max,file);
}

/* close/end */
int host_flash_close(void * handle)
{
   FILE * file;
   file = (FILE*) handle;
   //APP_DEBUG("host_flash_close\n");
   if (!fclose(file)) return HOST_FLASH_STATUS_OK;
   else return HOST_FLASH_STATUS_ERROR;
}
