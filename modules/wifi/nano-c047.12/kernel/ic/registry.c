/* $Id: registry.c 15507 2010-06-03 07:46:32Z joda $ */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>

#include "nanoutil.h"
#include "nanoparam.h"
#include "nanonet.h"
#include "wifi_engine.h"
#include "driverenv.h"

#include "px.h"

static int
nrx_reg_open(struct nrx_px_softc *sc, struct inode *inode, struct file *file)
{
   int ret;
   size_t i;
   char *buf;

   ret = nrx_px_setsize(sc, 4096);
   if(ret < 0)
      return ret;
      
   buf = nrx_px_data(sc);
   memset(buf, 0xff, nrx_px_size(sc));

   WiFiEngine_Registry_Write(buf);

   for(i = 0; i < nrx_px_size(sc) && buf[i] != '\xff'; i++) {
      /* strings are \n\0 terminated */
      if(buf[i] == '\0') {
         if(i > 0 && buf[i-1] == '\n')
            buf[i - 1] = ' ';
         buf[i] = '\n';
      }
   }
   return nrx_px_setsize(sc, i);
}

static int
nrx_reg_release(struct nrx_px_softc *sc, struct inode *inode, struct file *file)
{
   size_t i;
   char *buf;

   if(!nrx_px_dirty(sc))
      return 0;

   buf = nrx_px_data(sc);
   for(i = 0; i < nrx_px_size(sc); i++) {
      if(buf[i] == '\n')
         buf[i] = '\0';
   }

   WiFiEngine_Registry_Read(nrx_px_data(sc));

   return 0;
}

struct nrx_px_entry reg_px_entry = {
   .name = "registry", 
   .mode = S_IRUSR|S_IWUSR, 
   .blocksize = 1024, 
   .init = NULL,
   .open = nrx_reg_open,
   .release = nrx_reg_release
};

