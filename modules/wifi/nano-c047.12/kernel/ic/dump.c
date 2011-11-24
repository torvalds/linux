/* $Id: dump.c 15417 2010-05-24 14:05:47Z joda $ */
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#include <linux/config.h>
#endif
#include <linux/module.h>

#include "nanoutil.h"
#include "nanoparam.h"
#include "nanonet.h"
#include "wifi_engine.h"
#include "we_dump.h"
#include "driverenv.h"

#include "px.h"

static int
core_init(struct nrx_px_softc *psc)
{
   nrx_px_setsize(psc, 0);
   return 0;
}

static void
trim_corefiles(struct nrx_softc *sc)
{
   struct nrx_px_entry *pe;
   uint32_t corecount = 0;
   
   WEI_TQ_FOREACH(pe, &sc->corefiles, next) {
      corecount++;
   }
   while(corecount > sc->maxcorecount) {
      pe = WEI_TQ_FIRST(&sc->corefiles);
      nrx_px_remove(pe, sc->core_dir);
      corecount--;
   }
}

uint32_t nrx_get_corecount(struct net_device *dev)
{
   struct nrx_softc *sc = netdev_priv(dev);
   return sc->maxcorecount;
}

void nrx_set_corecount(struct net_device *dev, uint32_t count)
{
   struct nrx_softc *sc = netdev_priv(dev);
   sc->maxcorecount = count;
   trim_corefiles(sc);
}

int
nrx_create_coredump(struct net_device *dev, uint8_t objid, uint8_t err_code, char* data, size_t len)
{
   struct nrx_softc *sc = netdev_priv(dev);
   struct nrx_px_entry *pe;
   char name[sizeof(pe->name)];
   struct nrx_px_softc *psc;

   KDEBUG(TRACE, "coredump %u.%u %zu bytes", objid, err_code, len);

   snprintf(name, sizeof(name), "%u.%u.%u", sc->coreindex++, objid, err_code);

   if(sc->maxcorecount > 0 && 
      (pe = nrx_px_create_dynamic(dev, name, NULL, 0,
                                  NRX_PX_REMOVABLE,
                                  core_init, NULL, NULL, 
                                  sc->core_dir)) != NULL) {
      psc = nrx_px_lookup(pe, sc->core_dir);
      nrx_px_append(psc, data, len);
      pe->list = &sc->corefiles;
      WEI_TQ_INSERT_TAIL(pe->list, pe, next);
      trim_corefiles(sc);
   }

   return 0;
}




